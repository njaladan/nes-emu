#include <stdio.h>
#include <stdint.h>
#include <iostream>
#include <fstream>


#include "memory.cpp"

using namespace std;

class CPU {
  // TODO: move to private after debugging
  public:
    // CPU registers; reset should initialize to zero unless set
    uint8_t accumulator;
    uint8_t X;
    uint8_t Y;
    uint16_t PC;
    // only holds lower byte of the real SP
    uint8_t SP;

    // CPU flags
    bool carry;
    bool zero;
    bool interrupt_disable;
    bool overflow;
    bool sign;
    bool decimal; // has no effect on NES 6502 bc no decimal mode included
    // 2-bit B flag should be here, but I don't think it has an effect on the CPU

    // for debugging only
    bool valid;

    // "hardware" connections
    Memory memory;

    // running stuff
    void load_program(char*, int);
    void execute_cycle();
    void run();


    // memory functions
    void stack_push(uint8_t);
    uint8_t stack_pop();

    void address_stack_push(uint16_t);
    uint16_t address_stack_pop();


    // opcode implementation
    void compare(uint8_t, uint8_t);
    void sign_zero_flags(uint8_t);
    void rotate_left(uint8_t*);
    void rotate_right(uint8_t*);
    void shift_right(uint8_t*);

    // addressing modes
    uint8_t* zero_page_indexed_X(uint8_t);
    uint8_t* zero_page_indexed_Y(uint8_t);
    uint8_t* absolute_indexed_X(uint16_t);
    uint8_t* absolute_indexed_Y(uint16_t);
    uint8_t* indexed_indirect(uint8_t);
    uint8_t* indirect_indexed(uint8_t);
    uint16_t indirect(uint8_t, uint8_t);

};

// ideally loading would mean a pointer but I'll keep this for now, sigh
// not correct b/c bank switching means needless IOs instead of a simple pointer swap LMAOO
void CPU::load_program(char* program, int size) {
  bool mirror = false;
  if (size == 0x4000) {
    mirror = true;
  }
  int program_offset = 0x4000;
  for (int i = 0; i < size; i++) {
    memory.set_item(0x4000 + i, *(program + i));
    if (mirror) {
      memory.set_item(0xC000 + i, *(program + i));
    }
  }
  // need to cast to (uint16_t) ?
  PC = (memory.get_item(0xfffd) << 8) | memory.get_item(0xfffc);
  SP = 0xff;
  valid = true;
  printf("intializing PC with %x\n", PC);
}

void CPU::address_stack_push(uint16_t addr) {
  uint8_t lower_byte = (uint8_t) addr; // does this cast work?
  uint8_t upper_byte = (uint8_t) addr >> 8;
  stack_push(upper_byte);
  stack_push(lower_byte);
}

// check if little / big endian makes a difference?
uint16_t CPU::address_stack_pop() {
  uint8_t lower_byte = stack_pop();
  uint8_t upper_byte = stack_pop();
  uint16_t val = upper_byte << 8 | lower_byte;
  return val;
}

// TODO: make stack_offset a global variable or something?
void CPU::stack_push(uint8_t value) {
  uint16_t stack_offset = 0x100;
  memory.set_item(stack_offset + SP, value);
  // simulate underflow
  SP -= 1;
}

uint8_t CPU::stack_pop() {
  uint16_t stack_offset = 0x100;
  uint16_t stack_address = stack_offset + SP + 1;
  uint8_t value = memory.get_item(stack_address);
  SP += 1;
  return value;
}

// basically one giant switch case
// how can i make this better?
void CPU::execute_cycle() {
  uint8_t opcode = memory.get_item(PC);
   // these may lead to errors if we're at the end of PRG
  uint8_t arg1 = memory.get_item(PC + 1);
  uint8_t arg2 = memory.get_item(PC + 2);
  uint16_t address = arg2 << 8 | arg1;
  printf("%x: %x %x %x\n", PC, opcode, arg1, arg2);

  // used to avoid scope change in switch - is there a better way to do this?
  uint8_t temp;
  uint8_t* temp_pointer;

  switch(opcode) {

    // JSR: a
    case 0x20:
      address_stack_push(PC + 2); // push next address - 1 to stack
      PC = address;
      break;

    // RTS
    case 0x60:
      PC = address_stack_pop() + 1; // add one to stored address
      break;

    // NOP
    case 0xea:
      PC += 2;
      break;

    // CMP: i
    case 0xc9:
      compare(accumulator, arg1);
      PC += 2;
      break;

    // CMP: zero page
    case 0xc5:
      compare(accumulator, memory.get_item(arg1));
      PC += 2;
      break;

    // CMP: zero page, X
    case 0xd5:
      compare(accumulator, *(zero_page_indexed_X(arg1)));
      PC += 2;
      break;

    // CMP: absolute
    case 0xcd:
      compare(accumulator, memory.get_item(address));
      PC += 3;
      break;

    // CMP: absolute, X
    case 0xdd:
      compare(accumulator, *(absolute_indexed_X(address)));
      PC += 3;
      break;

    // CMP: absolute, Y
    case 0xd9:
      compare(accumulator, *(absolute_indexed_Y(address)));
      PC += 3;
      break;

    // CMP: indirect, X
    case 0xc1:
      compare(accumulator, *(indexed_indirect(arg1)));
      PC += 2;
      break;

    // CMP: indirect, Y
    case 0xd1:
      compare(accumulator, *(indirect_indexed(arg1)));
      PC += 2;
      break;

    // CPX: immediate
    case 0xe0:
      compare(X, arg1);
      PC += 2;
      break;

    // CPX: zero page
    case 0xe4:
      compare(X, memory.get_item(arg1));
      PC += 2;
      break;

    // CPX: absolute
    case 0xec:
      compare(X, memory.get_item(address));
      PC += 3;
      break;

    // CPY: immediate
    case 0xc0:
      compare(Y, arg1);
      PC += 2;
      break;

    // CPY: zero page
    case 0xc4:
      compare(Y, memory.get_item(arg1));
      PC += 2;
      break;

    // CPY: absolute
    case 0xcc:
      compare(Y, memory.get_item(address));
      PC += 3;
      break;

    // BPL
    case 0x10:
      if (!sign) {
        PC += ((int8_t) arg1) + 2;
      } else {
        PC += 2;
      }
      break;

    // BMI
    case 0x30:
      if (sign) {
        PC += ((int8_t) arg1) + 2;
      } else {
        PC += 2;
      }
      break;

    // BVC
    case 0x50:
      if (!overflow) {
        PC += ((int8_t) arg1) + 2;
      } else {
        PC += 2;
      }
      break;

    // BVS
    case 0x70:
      if (overflow) {
        PC += ((int8_t) arg1) + 2;
      } else {
        PC += 2;
      }
      break;

    // BCC
    case 0x90:
      if (!carry) {
        PC += ((int8_t) arg1) + 2;
      } else {
        PC += 2;
      }
      break;

    // BCS
    case 0xB0:
      if (carry) {
        PC += ((int8_t) arg1) + 2;
      } else {
        PC += 2;
      }
      break;

    // BNE
    case 0xD0:
      if (!zero) {
        PC += ((int8_t) arg1) + 2;
      } else {
        PC += 2;
      }
      break;

    // BEQ
    case 0xF0:
      if (zero) {
        PC += ((int8_t) arg1) + 2;
      } else {
        PC += 2;
      }
      break;

    // LDA immediate
    case 0xa9:
      accumulator = arg1;
      sign_zero_flags(accumulator);
      PC += 2;
      break;

    // LDA zero page
    case 0xa5:
      accumulator = memory.get_item(arg1);
      sign_zero_flags(accumulator);
      PC += 2;
      break;

    // LDA zero page X
    case 0xb5:
      accumulator = *(zero_page_indexed_X(arg1));
      sign_zero_flags(accumulator);
      PC += 2;
      break;

    // LDA absolute
    case 0xad:
      accumulator = memory.get_item(address);
      sign_zero_flags(accumulator);
      PC += 3;
      break;

    // LDA Absolute X
    case 0xbd:
      accumulator = *(absolute_indexed_X(address));
      sign_zero_flags(accumulator);
      PC += 3;
      break;

    // LDA absolute Y
    case 0xb9:
      accumulator = *(absolute_indexed_Y(address));
      sign_zero_flags(accumulator);
      PC += 3;
      break;

    // LDA indirect X
    case 0xa1:
      accumulator = *(indexed_indirect(arg1));
      sign_zero_flags(accumulator);
      PC += 2;
      break;

    // LDA indirect Y
    case 0xb1:
      accumulator = *(indirect_indexed(arg1));
      sign_zero_flags(accumulator);
      PC += 2;
      break;

    // LDX immediate
    case 0xa2:
      X = arg1;
      sign_zero_flags(X);
      PC += 2;
      break;

    // LDX zero page
    case 0xa6:
      X = memory.get_item(arg1);
      sign_zero_flags(X);
      PC += 2;
      break;

    // LDX zero page Y
    case 0xb6:
      X = *(zero_page_indexed_Y(arg1));
      sign_zero_flags(X);
      PC += 2;
      break;

    // LDX absolute
    case 0xae:
      X = memory.get_item(address);
      sign_zero_flags(X);
      PC += 3;
      break;

    // LDX absolute Y
    case 0xbe:
      X = *(absolute_indexed_Y(address));
      sign_zero_flags(X);
      PC += 3;
      break;

    // LDY immediate
    case 0xa0:
      Y = arg1;
      sign_zero_flags(Y);
      PC += 2;
      break;

    // LDY zero page
    case 0xa4:
      Y = memory.get_item(arg1);
      sign_zero_flags(Y);
      PC += 2;
      break;

    // LDY zero page X
    case 0xb4:
      Y = *(zero_page_indexed_X(arg1));
      sign_zero_flags(Y);
      PC += 2;
      break;

    // LDY absolute
    case 0xac:
      Y = memory.get_item(address);
      sign_zero_flags(Y);
      PC += 3;
      break;

    // LDY absolute X
    case 0xbc:
      Y = *(absolute_indexed_X(address));
      sign_zero_flags(Y);
      PC += 3;
      break;

    // STA zero page
    case 0x85:
      memory.set_item(arg1, accumulator);
      PC += 2;
      break;

    // STA zero page X
    case 0x95:
      *(zero_page_indexed_X(arg1)) = accumulator;
      PC += 2;
      break;

    // STA absolute
    case 0x8d:
      memory.set_item(address, accumulator);
      PC += 3;
      break;

    // STA absolute X
    case 0x9d:
      *(absolute_indexed_X(address)) = accumulator;
      PC += 3;
      break;

    // STA absolute Y
    case 0x99:
      *(absolute_indexed_Y(address)) = accumulator;
      PC += 3;
      break;

    // STA indirect X
    case 0x81:
      *(indexed_indirect(arg1)) = accumulator;
      PC += 2;
      break;

    // STA indirect Y
    case 0x91:
      *(indirect_indexed(arg1)) = accumulator;
      PC += 2;
      break;

    // STX zero page
    case 0x86:
      memory.set_item(arg1, X);
      PC += 2;
      break;

    // STX zero page Y
    case 0x96:
      *(zero_page_indexed_Y(arg1)) = X;
      PC += 2;
      break;

    // STX absolute
    case 0x8E:
      memory.set_item(address, X);
      PC += 3;
      break;

    // STY zero page
    case 0x84:
      memory.set_item(arg1, Y);
      PC += 2;
      break;

    // STY zero page X
    case 0x94:
      *(zero_page_indexed_X(arg1)) =  Y;
      PC += 2;
      break;

    // STY absolute
    case 0x8c:
      memory.set_item(address, Y);
      PC += 3;
      break;

    // JMP absolute
    case 0x4c:
      PC = address;
      break;

    // JMP indirect
    case 0x6c:
      PC = indirect(arg1, arg2);
      break;

    // CLC (clear carry)
    case 0x18:
      carry = false;
      PC += 1;
      break;

    // SEC (set carry)
    case 0x38:
      carry = true;
      PC += 1;
      break;

    // CLI (clear interrupt)
    case 0x58:
      interrupt_disable = false;
      PC += 1;
      break;

    // SEI (set interrupt)
    case 0x78:
      interrupt_disable = true;
      PC += 1;
      break;

    // CLV (clear overflow)
    case 0xb8:
      overflow = false;
      PC += 1;
      break;

    // CLD (clear decimal)
    case 0xd8:
      decimal = false;
      PC += 1;
      break;

    // SED (set decimal)
    case 0xf8:
      decimal = true;
      PC += 1;
      break;

    // BIT zero page
    case 0x24:
      zero = ((memory.get_item(arg1) & accumulator) == 0);
      overflow = (memory.get_item(arg1) >> 6) & 1;
      sign = (memory.get_item(arg1) >> 7) & 1;
      PC += 2;
      break;

    // BIT absolute
    case 0x2c:
      zero = ((memory.get_item(address) & accumulator) == 0);
      overflow = (memory.get_item(address) >> 6) & 1;
      sign = (memory.get_item(address) >> 7) & 1;
      PC += 3;
      break;

    // TYA
    case 0x98:
      accumulator = Y;
      sign_zero_flags(accumulator);
      PC += 1;
      break;

    // TXS
    case 0x9a:
      SP = X;
      PC += 1;
      break;

    // TXA
    case 0x8a:
      accumulator = X;
      sign_zero_flags(accumulator);
      PC += 1;
      break;

    // TSX
    case 0xba:
      X = SP;
      sign_zero_flags(X);
      PC += 1;
      break;

    // TAY
    case 0xa8:
      Y = accumulator;
      sign_zero_flags(Y);
      PC += 1;
      break;

    // TAX
    case 0xaa:
      X = accumulator;
      sign_zero_flags(X);
      PC += 1;
      break;

    // ROR accumulator
    case 0x6a:
      rotate_right(&accumulator);
      PC += 1;
      break;

    // ROR zero page
    case 0x66:
      rotate_right(memory.get_pointer(arg1));
      PC += 2;
      break;

    // ROR zero page X
    case 0x76:
      rotate_right(zero_page_indexed_X(arg1));
      PC += 2;
      break;

    // ROR absolute
    case 0x6e:
      rotate_right(memory.get_pointer(address));
      PC += 3;
      break;

    // ROR absolute X
    case 0x7e:
      rotate_right(absolute_indexed_X(address));
      PC += 3;
      break;

    // ROL accumulator
    case 0x2a:
      rotate_left(&accumulator);
      PC += 1;
      break;

    // ROL zero page
    case 0x26:
      rotate_left(memory.get_pointer(arg1));
      PC += 2;
      break;

    // ROL zero page X
    case 0x36:
      rotate_left(zero_page_indexed_X(arg1));
      PC += 2;
      break;

    // ROL absolute
    case 0x2e:
      rotate_left(memory.get_pointer(address));
      PC += 3;
      break;

    // ROL absolute X
    case 0x3e:
      rotate_left(absolute_indexed_X(address));
      PC += 3;
      break;

    // LSR accumulator
    case 0x4a:
      shift_right(&accumulator);
      PC += 1;
      break;

    // LSR zero page
    case 0x46:
      shift_right(memory.get_pointer(arg1));
      PC += 2;
      break;

    // LSR zero page X
    case 0x56:
      shift_right(zero_page_indexed_X(arg1));
      PC += 2;
      break;

    // LSR absolute
    case 0x4e:
      shift_right(memory.get_pointer(address));
      PC += 3;
      break;

    // LSR absolute X
    case 0x5e:
      shift_right(absolute_indexed_X(address));
      PC += 3;
      break;

    // PLA
    case 0x68:
      accumulator = stack_pop();
      sign_zero_flags(accumulator);
      PC += 1;
      break;

    // PHA
    case 0x48:
      stack_push(accumulator);
      PC += 1;
      break;

    // INY
    case 0xc8:
      Y += 1;
      sign_zero_flags(Y);
      PC += 1;
      break;

    // INX
    case 0xe8:
      X += 1;
      sign_zero_flags(X);
      PC += 1;
      break;

    // INC zero page
    case 0xe6:
      temp = memory.get_item(arg1) + 1;
      memory.set_item(arg1, temp);
      sign_zero_flags(temp);
      PC += 2;
      break;

    // INC zero page X
    case 0xf6:
      temp_pointer = zero_page_indexed_X(arg1);
      *temp_pointer = *temp_pointer + 1;
      sign_zero_flags(*temp_pointer);
      PC += 2;
      break;

    // INC absolute
    case 0xee:
      temp = memory.get_item(address) + 1;
      memory.set_item(address, temp);
      sign_zero_flags(temp);
      PC += 3;
      break;

    // INC absolute X
    case 0xfe:
      temp_pointer = absolute_indexed_X(address);
      *temp_pointer = *temp_pointer + 1;
      sign_zero_flags(*temp_pointer);
      PC += 3;
      break;

    // EOR immediate
    case 0x49:
      accumulator ^= arg1;
      sign_zero_flags(accumulator);
      PC += 2;
      break;

    // EOR zero page
    case 0x45:
      accumulator ^= memory.get_item(arg1);
      sign_zero_flags(accumulator);
      PC += 2;
      break;

    // EOR zero page X
    case 0x55:
      accumulator ^= *(zero_page_indexed_X(arg1));
      sign_zero_flags(accumulator);
      PC += 2;
      break;

    // EOR absolute
    case 0x4d:
      accumulator ^= memory.get_item(address);
      sign_zero_flags(accumulator);
      PC += 3;
      break;

    // EOR absolute X
    case 0x5d:
      accumulator ^= *(absolute_indexed_X(address));
      sign_zero_flags(accumulator);
      PC += 3;
      break;

    // EOR absolute Y
    case 0x59:
      accumulator ^= *(absolute_indexed_Y(address));
      sign_zero_flags(accumulator);
      PC += 3;
      break;

    // EOR indirect X
    case 0x41:
      accumulator ^= *(indexed_indirect(arg1));
      sign_zero_flags(accumulator);
      PC += 2;
      break;

    // EOR indirect Y
    case 0x51:
      accumulator ^= *(indirect_indexed(arg1));
      sign_zero_flags(accumulator);
      PC += 2;
      break;

    // DEY
    case 0x88:
      Y = Y - 1;
      sign_zero_flags(Y);
      PC += 1;
      break;

    // DEX
    case 0xca:
      X = X - 1;
      sign_zero_flags(X);
      PC += 1;
      break;

    // DEC zero page
    case 0xc6:
      temp = memory.get_item(arg1) - 1;
      memory.set_item(arg1, temp);
      sign_zero_flags(temp);
      PC += 2;
      break;

    // DEC zero page X
    case 0xd6:
      temp_pointer = zero_page_indexed_X(arg1);
      *temp_pointer = *temp_pointer - 1;
      sign_zero_flags(*temp_pointer);
      PC += 2;
      break;

    // DEC absolute
    case 0xce:
      temp = memory.get_item(address) - 1;
      memory.set_item(address, temp);
      sign_zero_flags(temp);
      PC += 3;
      break;

    // DEC absolute X
    case 0xde:
      temp_pointer = absolute_indexed_X(address);
      *temp_pointer = *temp_pointer - 1;
      sign_zero_flags(*temp_pointer);
      PC += 3;
      break;

    // AND immediate
    case 0x29:
      accumulator &= arg1;
      sign_zero_flags(accumulator);
      PC += 2;
      break;

    // AND zero page
    case 0x25:
      accumulator &= memory.get_item(arg1);
      sign_zero_flags(accumulator);
      PC += 2;
      break;

    // AND zero page X
    case 0x35:
      accumulator &= *(zero_page_indexed_X(arg1));
      sign_zero_flags(accumulator);
      PC += 2;
      break;

    // AND absolute
    case 0x2d:
      accumulator &= memory.get_item(address);
      sign_zero_flags(accumulator);
      PC += 3;
      break;

    // AND absolute X
    case 0x3d:
      accumulator &= *(absolute_indexed_X(address));
      sign_zero_flags(accumulator);
      PC += 3;
      break;

    // AND absolute Y
    case 0x39:
      accumulator &= *(absolute_indexed_Y(address));
      sign_zero_flags(accumulator);
      PC += 3;
      break;

    // AND indirect X
    case 0x21:
      accumulator &= *(indexed_indirect(arg1));
      sign_zero_flags(accumulator);
      PC += 2;
      break;

    // AND indirect Y
    case 0x31:
      accumulator &= *(indirect_indexed(arg1));
      sign_zero_flags(accumulator);
      PC += 2;
      break;

    // ORA immediate
    case 0x09:
      accumulator |= arg1;
      sign_zero_flags(accumulator);
      PC += 2;
      break;

    // ORA zero page
    case 0x05:
      accumulator |= memory.get_item(arg1);
      sign_zero_flags(accumulator);
      PC += 2;
      break;

    // ORA zero page X
    case 0x15:
      accumulator |= *(zero_page_indexed_X(arg1));
      sign_zero_flags(accumulator);
      PC += 2;
      break;

    // ORA absolute
    case 0x0d:
      accumulator |= memory.get_item(address);
      sign_zero_flags(accumulator);
      PC += 3;
      break;

    // ORA absolute X
    case 0x1d:
      accumulator |= *(absolute_indexed_X(address));
      sign_zero_flags(accumulator);
      PC += 3;
      break;

    // ORA absolute Y
    case 0x19:
      accumulator |= *(absolute_indexed_Y(address));
      sign_zero_flags(accumulator);
      PC += 3;
      break;

    // ORA indirect X
    case 0x01:
      accumulator |= *(indexed_indirect(arg1));
      sign_zero_flags(accumulator);
      PC += 2;
      break;

    // ORA indirect Y
    case 0x11:
      accumulator |= *(indirect_indexed(arg1));
      sign_zero_flags(accumulator);
      PC += 2;
      break;

    // ADC immediate
    case 0x69:
      add_with_carry(arg1);
      PC += 2;
      break;

    // ADC zero page
    case 0x65:
      add_with_carry(memory.get_item(arg1));
      PC += 2;
      break;

    // ADC zero page X
    case 0x75:
      add_with_carry(*(zero_page_indexed_X(arg1)));
      PC += 2;
      break;

    // ADC absolute
    case 0x6d:
      add_with_carry(memory.get_item(address));
      PC += 3;
      break;

    // ADC absolute X
    case 0x7d:
      add_with_carry(*(absolute_indexed_X(address)));
      PC += 3;
      break;

    // ADC absolute Y
    case 0x79:
      add_with_carry(*(absolute_indexed_Y(address)));
      PC += 3;
      break;

    // ADC indirect X
    case 0x61:
      add_with_carry(*(indexed_indirect(arg1)));
      PC += 2;
      break;

    // ADC indirect Y
    case 0x71:
      add_with_carry(*(indirect_indexed(arg1)));
      PC += 2;
      break;

    // PHP
    case 0x08:
      stack_push(get_flags_as_byte());
      PC += 1;
      break;

    // PLP
    case 0x28:
      set_flags_from_byte(stack_pop());
      PC += 1;
      break;

    // ASL accumulator
    case 0x0a:
      arithmetic_shift_left(&accumulator);
      PC += 1;
      break;

    // ASL zero page
    case 0x06:
      arithmetic_shift_left(memory.get_pointer(arg1));
      PC += 2;
      break;

    // ASL zero page X
    case 0x16:
      arithmetic_shift_left(zero_page_indexed_X(arg1));
      PC += 2;
      break;

    // ASL absolute
    case 0x0e:
      arithmetic_shift_left(memory.get_pointer(address));
      PC += 3;
      break;

    // ASL absolute X
    case 0x1e:
      arithmetic_shift_left(absolute_indexed_X(address));
      PC += 3;
      break;




    default:
      printf("this instruction is not here lmao: %x\n", opcode);
      valid = false;
      break;
  }
}

void CPU::run() {
  while(valid) {
    execute_cycle();
  }
}

// is it proper practice to assume this modifies the accumulator?
void CPU::add_with_carry(uint8_t operand) {
  uint8_t before = accumulator;
  accumulator += operand;
  zero = (accumulator == 0);
  sign = (accumulator >= 0x80);
  // signed overflow
  overflow = ((before ^ accumulator) & (operand ^ accumulator) & 0x80) >> 7;
  // unsigned overflow
  carry = (acccumulator < before) && (accumulator < operand);
}

// no decimal, B flag since NES 6502 doesn't support
uint8_t CPU::get_flags_as_byte() {
  uint8_t flags = 0;
  flags |= (sign << 7);
  flags |= (overflow << 6);
  flags |= (interrupt_disable << 2);
  flags |= (zero << 1);
  flags |= carry;
  return flags;
}

void set_flags_from_byte(uint8_t flags) {
  carry = flags & 0x1;
  zero = (flags >> 1) & 0x1;
  interrupt_disable = (flags >> 2) & 0x1;
  overflow = (flags >> 6) & 0x1;
  sign = (flags >> 7) & 0x1;
}

// POTENTIAL BUG -> zero flag set when A == 0 or new_val == 0 (??)
// going with the second, correct if wrong
void CPU::rotate_right(uint8_t* operand) {
  uint8_t new_value = (carry << 7) | (*operand >> 1);
  carry = *operand & 0x1;
  zero = (new_value == 0);
  sign = (new_value >= 0x80);
  *operand = new_value;
}

void CPU::rotate_left(uint8_t* operand) {
  uint8_t new_value = (*operand << 1) | carry;
  carry = (*operand >> 7) & 0x1;
  zero = (new_value == 0);
  sign = (new_value >= 0x80);
  *operand = new_value;
}

void CPU::shift_right(uint8_t* operand) {
  uint8_t new_value = *operand >> 1;
  carry = *operand & 0x1;
  zero = (new_value == 0);
  sign = (new_value >= 0x80); // this is basically impossible i assume
  *operand = new_value;
}

void CPU::arithmetic_shift_left(uint8_t* operand) {
  uint8_t new_value = *operand << 1;
  carry = (*operand >> 7) & 0x1;
  zero = (new_value == 0);
  sign = (new_value >= 0x80);
  *operand = new_value;
}

void CPU::compare(uint8_t operand, uint8_t argument) {
  carry = (operand >= argument);
  zero = (operand == argument);
  sign = (operand >= 0x80); // negative two's complement
}

void CPU::sign_zero_flags(uint8_t val) {
  sign = (val >= 0x80);
  zero = (val == 0);
}

// ADDRESSING MODES POINTER

uint8_t* CPU::zero_page_indexed_X(uint8_t arg) {
  return memory.get_pointer((arg + X) & 0x100);
}

uint8_t* CPU::zero_page_indexed_Y(uint8_t arg) {
  return memory.get_pointer((arg + Y) & 0x100);
}

uint8_t* CPU::absolute_indexed_X(uint16_t arg) {
  return memory.get_pointer(arg + X);
}

uint8_t* CPU::absolute_indexed_Y(uint16_t arg) {
  return memory.get_pointer(arg + Y);
}

uint8_t* CPU::indexed_indirect(uint8_t arg) {
  uint16_t low_byte = (uint16_t) memory.get_item((arg + X) & 0x100);
  uint16_t high_byte = memory.get_item((arg + X + 1) & 0x100) << 8;
  return memory.get_pointer(high_byte | low_byte);
}

uint8_t* CPU::indirect_indexed(uint8_t arg) {
  uint16_t low_byte = (uint16_t) memory.get_item(arg);
  uint16_t high_byte = (uint16_t) memory.get_item((arg + 1) & 0x100);
  return memory.get_pointer((high_byte | low_byte) + Y);
}

uint16_t CPU::indirect(uint8_t arg1, uint8_t arg2) {
  uint16_t lower_byte_address = arg1 << 8 | arg2;
  uint16_t upper_byte_address = arg1 << 8 | ((arg2 + 1) & 0xff); // because last byte overflow won't carry over
  uint8_t lower_byte = memory.get_item(lower_byte_address);
  uint8_t upper_byte = memory.get_item(upper_byte_address);
  uint16_t new_pc_address = (upper_byte << 8) | lower_byte;
  return new_pc_address;
}



int main() {
  CPU nes;
  ifstream rom("read_test.nes", ios::binary | ios::ate);
  streamsize size = rom.tellg();
  rom.seekg(0, ios::beg);

  char buffer[size];
  if (rom.read(buffer, size)) {
    printf("Read in ROM data\n");
    int prg_size = buffer[4] * 0x4000; // multiplier
    char* prg_data = buffer + 16; // techncially doesn't count for possibility of trainer but that's ok
    nes.load_program(prg_data, prg_size);
    nes.run();
  }


}
