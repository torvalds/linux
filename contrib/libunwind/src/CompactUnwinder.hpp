//===-------------------------- CompactUnwinder.hpp -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//
//  Does runtime stack unwinding using compact unwind encodings.
//
//===----------------------------------------------------------------------===//

#ifndef __COMPACT_UNWINDER_HPP__
#define __COMPACT_UNWINDER_HPP__

#include <stdint.h>
#include <stdlib.h>

#include <libunwind.h>
#include <mach-o/compact_unwind_encoding.h>

#include "Registers.hpp"

#define EXTRACT_BITS(value, mask)                                              \
  ((value >> __builtin_ctz(mask)) & (((1 << __builtin_popcount(mask))) - 1))

namespace libunwind {

#if defined(_LIBUNWIND_TARGET_I386)
/// CompactUnwinder_x86 uses a compact unwind info to virtually "step" (aka
/// unwind) by modifying a Registers_x86 register set
template <typename A>
class CompactUnwinder_x86 {
public:

  static int stepWithCompactEncoding(compact_unwind_encoding_t info,
                                     uint32_t functionStart, A &addressSpace,
                                     Registers_x86 &registers);

private:
  typename A::pint_t pint_t;

  static void frameUnwind(A &addressSpace, Registers_x86 &registers);
  static void framelessUnwind(A &addressSpace,
                              typename A::pint_t returnAddressLocation,
                              Registers_x86 &registers);
  static int
      stepWithCompactEncodingEBPFrame(compact_unwind_encoding_t compactEncoding,
                                      uint32_t functionStart, A &addressSpace,
                                      Registers_x86 &registers);
  static int stepWithCompactEncodingFrameless(
      compact_unwind_encoding_t compactEncoding, uint32_t functionStart,
      A &addressSpace, Registers_x86 &registers, bool indirectStackSize);
};

template <typename A>
int CompactUnwinder_x86<A>::stepWithCompactEncoding(
    compact_unwind_encoding_t compactEncoding, uint32_t functionStart,
    A &addressSpace, Registers_x86 &registers) {
  switch (compactEncoding & UNWIND_X86_MODE_MASK) {
  case UNWIND_X86_MODE_EBP_FRAME:
    return stepWithCompactEncodingEBPFrame(compactEncoding, functionStart,
                                           addressSpace, registers);
  case UNWIND_X86_MODE_STACK_IMMD:
    return stepWithCompactEncodingFrameless(compactEncoding, functionStart,
                                            addressSpace, registers, false);
  case UNWIND_X86_MODE_STACK_IND:
    return stepWithCompactEncodingFrameless(compactEncoding, functionStart,
                                            addressSpace, registers, true);
  }
  _LIBUNWIND_ABORT("invalid compact unwind encoding");
}

template <typename A>
int CompactUnwinder_x86<A>::stepWithCompactEncodingEBPFrame(
    compact_unwind_encoding_t compactEncoding, uint32_t functionStart,
    A &addressSpace, Registers_x86 &registers) {
  uint32_t savedRegistersOffset =
      EXTRACT_BITS(compactEncoding, UNWIND_X86_EBP_FRAME_OFFSET);
  uint32_t savedRegistersLocations =
      EXTRACT_BITS(compactEncoding, UNWIND_X86_EBP_FRAME_REGISTERS);

  uint32_t savedRegisters = registers.getEBP() - 4 * savedRegistersOffset;
  for (int i = 0; i < 5; ++i) {
    switch (savedRegistersLocations & 0x7) {
    case UNWIND_X86_REG_NONE:
      // no register saved in this slot
      break;
    case UNWIND_X86_REG_EBX:
      registers.setEBX(addressSpace.get32(savedRegisters));
      break;
    case UNWIND_X86_REG_ECX:
      registers.setECX(addressSpace.get32(savedRegisters));
      break;
    case UNWIND_X86_REG_EDX:
      registers.setEDX(addressSpace.get32(savedRegisters));
      break;
    case UNWIND_X86_REG_EDI:
      registers.setEDI(addressSpace.get32(savedRegisters));
      break;
    case UNWIND_X86_REG_ESI:
      registers.setESI(addressSpace.get32(savedRegisters));
      break;
    default:
      (void)functionStart;
      _LIBUNWIND_DEBUG_LOG("bad register for EBP frame, encoding=%08X for  "
                           "function starting at 0x%X",
                            compactEncoding, functionStart);
      _LIBUNWIND_ABORT("invalid compact unwind encoding");
    }
    savedRegisters += 4;
    savedRegistersLocations = (savedRegistersLocations >> 3);
  }
  frameUnwind(addressSpace, registers);
  return UNW_STEP_SUCCESS;
}

template <typename A>
int CompactUnwinder_x86<A>::stepWithCompactEncodingFrameless(
    compact_unwind_encoding_t encoding, uint32_t functionStart,
    A &addressSpace, Registers_x86 &registers, bool indirectStackSize) {
  uint32_t stackSizeEncoded =
      EXTRACT_BITS(encoding, UNWIND_X86_FRAMELESS_STACK_SIZE);
  uint32_t stackAdjust =
      EXTRACT_BITS(encoding, UNWIND_X86_FRAMELESS_STACK_ADJUST);
  uint32_t regCount =
      EXTRACT_BITS(encoding, UNWIND_X86_FRAMELESS_STACK_REG_COUNT);
  uint32_t permutation =
      EXTRACT_BITS(encoding, UNWIND_X86_FRAMELESS_STACK_REG_PERMUTATION);
  uint32_t stackSize = stackSizeEncoded * 4;
  if (indirectStackSize) {
    // stack size is encoded in subl $xxx,%esp instruction
    uint32_t subl = addressSpace.get32(functionStart + stackSizeEncoded);
    stackSize = subl + 4 * stackAdjust;
  }
  // decompress permutation
  uint32_t permunreg[6];
  switch (regCount) {
  case 6:
    permunreg[0] = permutation / 120;
    permutation -= (permunreg[0] * 120);
    permunreg[1] = permutation / 24;
    permutation -= (permunreg[1] * 24);
    permunreg[2] = permutation / 6;
    permutation -= (permunreg[2] * 6);
    permunreg[3] = permutation / 2;
    permutation -= (permunreg[3] * 2);
    permunreg[4] = permutation;
    permunreg[5] = 0;
    break;
  case 5:
    permunreg[0] = permutation / 120;
    permutation -= (permunreg[0] * 120);
    permunreg[1] = permutation / 24;
    permutation -= (permunreg[1] * 24);
    permunreg[2] = permutation / 6;
    permutation -= (permunreg[2] * 6);
    permunreg[3] = permutation / 2;
    permutation -= (permunreg[3] * 2);
    permunreg[4] = permutation;
    break;
  case 4:
    permunreg[0] = permutation / 60;
    permutation -= (permunreg[0] * 60);
    permunreg[1] = permutation / 12;
    permutation -= (permunreg[1] * 12);
    permunreg[2] = permutation / 3;
    permutation -= (permunreg[2] * 3);
    permunreg[3] = permutation;
    break;
  case 3:
    permunreg[0] = permutation / 20;
    permutation -= (permunreg[0] * 20);
    permunreg[1] = permutation / 4;
    permutation -= (permunreg[1] * 4);
    permunreg[2] = permutation;
    break;
  case 2:
    permunreg[0] = permutation / 5;
    permutation -= (permunreg[0] * 5);
    permunreg[1] = permutation;
    break;
  case 1:
    permunreg[0] = permutation;
    break;
  }
  // re-number registers back to standard numbers
  int registersSaved[6];
  bool used[7] = { false, false, false, false, false, false, false };
  for (uint32_t i = 0; i < regCount; ++i) {
    uint32_t renum = 0;
    for (int u = 1; u < 7; ++u) {
      if (!used[u]) {
        if (renum == permunreg[i]) {
          registersSaved[i] = u;
          used[u] = true;
          break;
        }
        ++renum;
      }
    }
  }
  uint32_t savedRegisters = registers.getSP() + stackSize - 4 - 4 * regCount;
  for (uint32_t i = 0; i < regCount; ++i) {
    switch (registersSaved[i]) {
    case UNWIND_X86_REG_EBX:
      registers.setEBX(addressSpace.get32(savedRegisters));
      break;
    case UNWIND_X86_REG_ECX:
      registers.setECX(addressSpace.get32(savedRegisters));
      break;
    case UNWIND_X86_REG_EDX:
      registers.setEDX(addressSpace.get32(savedRegisters));
      break;
    case UNWIND_X86_REG_EDI:
      registers.setEDI(addressSpace.get32(savedRegisters));
      break;
    case UNWIND_X86_REG_ESI:
      registers.setESI(addressSpace.get32(savedRegisters));
      break;
    case UNWIND_X86_REG_EBP:
      registers.setEBP(addressSpace.get32(savedRegisters));
      break;
    default:
      _LIBUNWIND_DEBUG_LOG("bad register for frameless, encoding=%08X for "
                           "function starting at 0x%X",
                           encoding, functionStart);
      _LIBUNWIND_ABORT("invalid compact unwind encoding");
    }
    savedRegisters += 4;
  }
  framelessUnwind(addressSpace, savedRegisters, registers);
  return UNW_STEP_SUCCESS;
}


template <typename A>
void CompactUnwinder_x86<A>::frameUnwind(A &addressSpace,
                                         Registers_x86 &registers) {
  typename A::pint_t bp = registers.getEBP();
  // ebp points to old ebp
  registers.setEBP(addressSpace.get32(bp));
  // old esp is ebp less saved ebp and return address
  registers.setSP((uint32_t)bp + 8);
  // pop return address into eip
  registers.setIP(addressSpace.get32(bp + 4));
}

template <typename A>
void CompactUnwinder_x86<A>::framelessUnwind(
    A &addressSpace, typename A::pint_t returnAddressLocation,
    Registers_x86 &registers) {
  // return address is on stack after last saved register
  registers.setIP(addressSpace.get32(returnAddressLocation));
  // old esp is before return address
  registers.setSP((uint32_t)returnAddressLocation + 4);
}
#endif // _LIBUNWIND_TARGET_I386


#if defined(_LIBUNWIND_TARGET_X86_64)
/// CompactUnwinder_x86_64 uses a compact unwind info to virtually "step" (aka
/// unwind) by modifying a Registers_x86_64 register set
template <typename A>
class CompactUnwinder_x86_64 {
public:

  static int stepWithCompactEncoding(compact_unwind_encoding_t compactEncoding,
                                     uint64_t functionStart, A &addressSpace,
                                     Registers_x86_64 &registers);

private:
  typename A::pint_t pint_t;

  static void frameUnwind(A &addressSpace, Registers_x86_64 &registers);
  static void framelessUnwind(A &addressSpace, uint64_t returnAddressLocation,
                              Registers_x86_64 &registers);
  static int
      stepWithCompactEncodingRBPFrame(compact_unwind_encoding_t compactEncoding,
                                      uint64_t functionStart, A &addressSpace,
                                      Registers_x86_64 &registers);
  static int stepWithCompactEncodingFrameless(
      compact_unwind_encoding_t compactEncoding, uint64_t functionStart,
      A &addressSpace, Registers_x86_64 &registers, bool indirectStackSize);
};

template <typename A>
int CompactUnwinder_x86_64<A>::stepWithCompactEncoding(
    compact_unwind_encoding_t compactEncoding, uint64_t functionStart,
    A &addressSpace, Registers_x86_64 &registers) {
  switch (compactEncoding & UNWIND_X86_64_MODE_MASK) {
  case UNWIND_X86_64_MODE_RBP_FRAME:
    return stepWithCompactEncodingRBPFrame(compactEncoding, functionStart,
                                           addressSpace, registers);
  case UNWIND_X86_64_MODE_STACK_IMMD:
    return stepWithCompactEncodingFrameless(compactEncoding, functionStart,
                                            addressSpace, registers, false);
  case UNWIND_X86_64_MODE_STACK_IND:
    return stepWithCompactEncodingFrameless(compactEncoding, functionStart,
                                            addressSpace, registers, true);
  }
  _LIBUNWIND_ABORT("invalid compact unwind encoding");
}

template <typename A>
int CompactUnwinder_x86_64<A>::stepWithCompactEncodingRBPFrame(
    compact_unwind_encoding_t compactEncoding, uint64_t functionStart,
    A &addressSpace, Registers_x86_64 &registers) {
  uint32_t savedRegistersOffset =
      EXTRACT_BITS(compactEncoding, UNWIND_X86_64_RBP_FRAME_OFFSET);
  uint32_t savedRegistersLocations =
      EXTRACT_BITS(compactEncoding, UNWIND_X86_64_RBP_FRAME_REGISTERS);

  uint64_t savedRegisters = registers.getRBP() - 8 * savedRegistersOffset;
  for (int i = 0; i < 5; ++i) {
    switch (savedRegistersLocations & 0x7) {
    case UNWIND_X86_64_REG_NONE:
      // no register saved in this slot
      break;
    case UNWIND_X86_64_REG_RBX:
      registers.setRBX(addressSpace.get64(savedRegisters));
      break;
    case UNWIND_X86_64_REG_R12:
      registers.setR12(addressSpace.get64(savedRegisters));
      break;
    case UNWIND_X86_64_REG_R13:
      registers.setR13(addressSpace.get64(savedRegisters));
      break;
    case UNWIND_X86_64_REG_R14:
      registers.setR14(addressSpace.get64(savedRegisters));
      break;
    case UNWIND_X86_64_REG_R15:
      registers.setR15(addressSpace.get64(savedRegisters));
      break;
    default:
      (void)functionStart;
      _LIBUNWIND_DEBUG_LOG("bad register for RBP frame, encoding=%08X for "
                           "function starting at 0x%llX",
                            compactEncoding, functionStart);
      _LIBUNWIND_ABORT("invalid compact unwind encoding");
    }
    savedRegisters += 8;
    savedRegistersLocations = (savedRegistersLocations >> 3);
  }
  frameUnwind(addressSpace, registers);
  return UNW_STEP_SUCCESS;
}

template <typename A>
int CompactUnwinder_x86_64<A>::stepWithCompactEncodingFrameless(
    compact_unwind_encoding_t encoding, uint64_t functionStart, A &addressSpace,
    Registers_x86_64 &registers, bool indirectStackSize) {
  uint32_t stackSizeEncoded =
      EXTRACT_BITS(encoding, UNWIND_X86_64_FRAMELESS_STACK_SIZE);
  uint32_t stackAdjust =
      EXTRACT_BITS(encoding, UNWIND_X86_64_FRAMELESS_STACK_ADJUST);
  uint32_t regCount =
      EXTRACT_BITS(encoding, UNWIND_X86_64_FRAMELESS_STACK_REG_COUNT);
  uint32_t permutation =
      EXTRACT_BITS(encoding, UNWIND_X86_64_FRAMELESS_STACK_REG_PERMUTATION);
  uint32_t stackSize = stackSizeEncoded * 8;
  if (indirectStackSize) {
    // stack size is encoded in subl $xxx,%esp instruction
    uint32_t subl = addressSpace.get32(functionStart + stackSizeEncoded);
    stackSize = subl + 8 * stackAdjust;
  }
  // decompress permutation
  uint32_t permunreg[6];
  switch (regCount) {
  case 6:
    permunreg[0] = permutation / 120;
    permutation -= (permunreg[0] * 120);
    permunreg[1] = permutation / 24;
    permutation -= (permunreg[1] * 24);
    permunreg[2] = permutation / 6;
    permutation -= (permunreg[2] * 6);
    permunreg[3] = permutation / 2;
    permutation -= (permunreg[3] * 2);
    permunreg[4] = permutation;
    permunreg[5] = 0;
    break;
  case 5:
    permunreg[0] = permutation / 120;
    permutation -= (permunreg[0] * 120);
    permunreg[1] = permutation / 24;
    permutation -= (permunreg[1] * 24);
    permunreg[2] = permutation / 6;
    permutation -= (permunreg[2] * 6);
    permunreg[3] = permutation / 2;
    permutation -= (permunreg[3] * 2);
    permunreg[4] = permutation;
    break;
  case 4:
    permunreg[0] = permutation / 60;
    permutation -= (permunreg[0] * 60);
    permunreg[1] = permutation / 12;
    permutation -= (permunreg[1] * 12);
    permunreg[2] = permutation / 3;
    permutation -= (permunreg[2] * 3);
    permunreg[3] = permutation;
    break;
  case 3:
    permunreg[0] = permutation / 20;
    permutation -= (permunreg[0] * 20);
    permunreg[1] = permutation / 4;
    permutation -= (permunreg[1] * 4);
    permunreg[2] = permutation;
    break;
  case 2:
    permunreg[0] = permutation / 5;
    permutation -= (permunreg[0] * 5);
    permunreg[1] = permutation;
    break;
  case 1:
    permunreg[0] = permutation;
    break;
  }
  // re-number registers back to standard numbers
  int registersSaved[6];
  bool used[7] = { false, false, false, false, false, false, false };
  for (uint32_t i = 0; i < regCount; ++i) {
    uint32_t renum = 0;
    for (int u = 1; u < 7; ++u) {
      if (!used[u]) {
        if (renum == permunreg[i]) {
          registersSaved[i] = u;
          used[u] = true;
          break;
        }
        ++renum;
      }
    }
  }
  uint64_t savedRegisters = registers.getSP() + stackSize - 8 - 8 * regCount;
  for (uint32_t i = 0; i < regCount; ++i) {
    switch (registersSaved[i]) {
    case UNWIND_X86_64_REG_RBX:
      registers.setRBX(addressSpace.get64(savedRegisters));
      break;
    case UNWIND_X86_64_REG_R12:
      registers.setR12(addressSpace.get64(savedRegisters));
      break;
    case UNWIND_X86_64_REG_R13:
      registers.setR13(addressSpace.get64(savedRegisters));
      break;
    case UNWIND_X86_64_REG_R14:
      registers.setR14(addressSpace.get64(savedRegisters));
      break;
    case UNWIND_X86_64_REG_R15:
      registers.setR15(addressSpace.get64(savedRegisters));
      break;
    case UNWIND_X86_64_REG_RBP:
      registers.setRBP(addressSpace.get64(savedRegisters));
      break;
    default:
      _LIBUNWIND_DEBUG_LOG("bad register for frameless, encoding=%08X for "
                           "function starting at 0x%llX",
                            encoding, functionStart);
      _LIBUNWIND_ABORT("invalid compact unwind encoding");
    }
    savedRegisters += 8;
  }
  framelessUnwind(addressSpace, savedRegisters, registers);
  return UNW_STEP_SUCCESS;
}


template <typename A>
void CompactUnwinder_x86_64<A>::frameUnwind(A &addressSpace,
                                            Registers_x86_64 &registers) {
  uint64_t rbp = registers.getRBP();
  // ebp points to old ebp
  registers.setRBP(addressSpace.get64(rbp));
  // old esp is ebp less saved ebp and return address
  registers.setSP(rbp + 16);
  // pop return address into eip
  registers.setIP(addressSpace.get64(rbp + 8));
}

template <typename A>
void CompactUnwinder_x86_64<A>::framelessUnwind(A &addressSpace,
                                                uint64_t returnAddressLocation,
                                                Registers_x86_64 &registers) {
  // return address is on stack after last saved register
  registers.setIP(addressSpace.get64(returnAddressLocation));
  // old esp is before return address
  registers.setSP(returnAddressLocation + 8);
}
#endif // _LIBUNWIND_TARGET_X86_64



#if defined(_LIBUNWIND_TARGET_AARCH64)
/// CompactUnwinder_arm64 uses a compact unwind info to virtually "step" (aka
/// unwind) by modifying a Registers_arm64 register set
template <typename A>
class CompactUnwinder_arm64 {
public:

  static int stepWithCompactEncoding(compact_unwind_encoding_t compactEncoding,
                                     uint64_t functionStart, A &addressSpace,
                                     Registers_arm64 &registers);

private:
  typename A::pint_t pint_t;

  static int
      stepWithCompactEncodingFrame(compact_unwind_encoding_t compactEncoding,
                                   uint64_t functionStart, A &addressSpace,
                                   Registers_arm64 &registers);
  static int stepWithCompactEncodingFrameless(
      compact_unwind_encoding_t compactEncoding, uint64_t functionStart,
      A &addressSpace, Registers_arm64 &registers);
};

template <typename A>
int CompactUnwinder_arm64<A>::stepWithCompactEncoding(
    compact_unwind_encoding_t compactEncoding, uint64_t functionStart,
    A &addressSpace, Registers_arm64 &registers) {
  switch (compactEncoding & UNWIND_ARM64_MODE_MASK) {
  case UNWIND_ARM64_MODE_FRAME:
    return stepWithCompactEncodingFrame(compactEncoding, functionStart,
                                        addressSpace, registers);
  case UNWIND_ARM64_MODE_FRAMELESS:
    return stepWithCompactEncodingFrameless(compactEncoding, functionStart,
                                            addressSpace, registers);
  }
  _LIBUNWIND_ABORT("invalid compact unwind encoding");
}

template <typename A>
int CompactUnwinder_arm64<A>::stepWithCompactEncodingFrameless(
    compact_unwind_encoding_t encoding, uint64_t, A &addressSpace,
    Registers_arm64 &registers) {
  uint32_t stackSize =
      16 * EXTRACT_BITS(encoding, UNWIND_ARM64_FRAMELESS_STACK_SIZE_MASK);

  uint64_t savedRegisterLoc = registers.getSP() + stackSize;

  if (encoding & UNWIND_ARM64_FRAME_X19_X20_PAIR) {
    registers.setRegister(UNW_ARM64_X19, addressSpace.get64(savedRegisterLoc));
    savedRegisterLoc -= 8;
    registers.setRegister(UNW_ARM64_X20, addressSpace.get64(savedRegisterLoc));
    savedRegisterLoc -= 8;
  }
  if (encoding & UNWIND_ARM64_FRAME_X21_X22_PAIR) {
    registers.setRegister(UNW_ARM64_X21, addressSpace.get64(savedRegisterLoc));
    savedRegisterLoc -= 8;
    registers.setRegister(UNW_ARM64_X22, addressSpace.get64(savedRegisterLoc));
    savedRegisterLoc -= 8;
  }
  if (encoding & UNWIND_ARM64_FRAME_X23_X24_PAIR) {
    registers.setRegister(UNW_ARM64_X23, addressSpace.get64(savedRegisterLoc));
    savedRegisterLoc -= 8;
    registers.setRegister(UNW_ARM64_X24, addressSpace.get64(savedRegisterLoc));
    savedRegisterLoc -= 8;
  }
  if (encoding & UNWIND_ARM64_FRAME_X25_X26_PAIR) {
    registers.setRegister(UNW_ARM64_X25, addressSpace.get64(savedRegisterLoc));
    savedRegisterLoc -= 8;
    registers.setRegister(UNW_ARM64_X26, addressSpace.get64(savedRegisterLoc));
    savedRegisterLoc -= 8;
  }
  if (encoding & UNWIND_ARM64_FRAME_X27_X28_PAIR) {
    registers.setRegister(UNW_ARM64_X27, addressSpace.get64(savedRegisterLoc));
    savedRegisterLoc -= 8;
    registers.setRegister(UNW_ARM64_X28, addressSpace.get64(savedRegisterLoc));
    savedRegisterLoc -= 8;
  }

  if (encoding & UNWIND_ARM64_FRAME_D8_D9_PAIR) {
    registers.setFloatRegister(UNW_ARM64_D8,
                               addressSpace.getDouble(savedRegisterLoc));
    savedRegisterLoc -= 8;
    registers.setFloatRegister(UNW_ARM64_D9,
                               addressSpace.getDouble(savedRegisterLoc));
    savedRegisterLoc -= 8;
  }
  if (encoding & UNWIND_ARM64_FRAME_D10_D11_PAIR) {
    registers.setFloatRegister(UNW_ARM64_D10,
                               addressSpace.getDouble(savedRegisterLoc));
    savedRegisterLoc -= 8;
    registers.setFloatRegister(UNW_ARM64_D11,
                               addressSpace.getDouble(savedRegisterLoc));
    savedRegisterLoc -= 8;
  }
  if (encoding & UNWIND_ARM64_FRAME_D12_D13_PAIR) {
    registers.setFloatRegister(UNW_ARM64_D12,
                               addressSpace.getDouble(savedRegisterLoc));
    savedRegisterLoc -= 8;
    registers.setFloatRegister(UNW_ARM64_D13,
                               addressSpace.getDouble(savedRegisterLoc));
    savedRegisterLoc -= 8;
  }
  if (encoding & UNWIND_ARM64_FRAME_D14_D15_PAIR) {
    registers.setFloatRegister(UNW_ARM64_D14,
                               addressSpace.getDouble(savedRegisterLoc));
    savedRegisterLoc -= 8;
    registers.setFloatRegister(UNW_ARM64_D15,
                               addressSpace.getDouble(savedRegisterLoc));
    savedRegisterLoc -= 8;
  }

  // subtract stack size off of sp
  registers.setSP(savedRegisterLoc);

  // set pc to be value in lr
  registers.setIP(registers.getRegister(UNW_ARM64_LR));

  return UNW_STEP_SUCCESS;
}

template <typename A>
int CompactUnwinder_arm64<A>::stepWithCompactEncodingFrame(
    compact_unwind_encoding_t encoding, uint64_t, A &addressSpace,
    Registers_arm64 &registers) {
  uint64_t savedRegisterLoc = registers.getFP() - 8;

  if (encoding & UNWIND_ARM64_FRAME_X19_X20_PAIR) {
    registers.setRegister(UNW_ARM64_X19, addressSpace.get64(savedRegisterLoc));
    savedRegisterLoc -= 8;
    registers.setRegister(UNW_ARM64_X20, addressSpace.get64(savedRegisterLoc));
    savedRegisterLoc -= 8;
  }
  if (encoding & UNWIND_ARM64_FRAME_X21_X22_PAIR) {
    registers.setRegister(UNW_ARM64_X21, addressSpace.get64(savedRegisterLoc));
    savedRegisterLoc -= 8;
    registers.setRegister(UNW_ARM64_X22, addressSpace.get64(savedRegisterLoc));
    savedRegisterLoc -= 8;
  }
  if (encoding & UNWIND_ARM64_FRAME_X23_X24_PAIR) {
    registers.setRegister(UNW_ARM64_X23, addressSpace.get64(savedRegisterLoc));
    savedRegisterLoc -= 8;
    registers.setRegister(UNW_ARM64_X24, addressSpace.get64(savedRegisterLoc));
    savedRegisterLoc -= 8;
  }
  if (encoding & UNWIND_ARM64_FRAME_X25_X26_PAIR) {
    registers.setRegister(UNW_ARM64_X25, addressSpace.get64(savedRegisterLoc));
    savedRegisterLoc -= 8;
    registers.setRegister(UNW_ARM64_X26, addressSpace.get64(savedRegisterLoc));
    savedRegisterLoc -= 8;
  }
  if (encoding & UNWIND_ARM64_FRAME_X27_X28_PAIR) {
    registers.setRegister(UNW_ARM64_X27, addressSpace.get64(savedRegisterLoc));
    savedRegisterLoc -= 8;
    registers.setRegister(UNW_ARM64_X28, addressSpace.get64(savedRegisterLoc));
    savedRegisterLoc -= 8;
  }

  if (encoding & UNWIND_ARM64_FRAME_D8_D9_PAIR) {
    registers.setFloatRegister(UNW_ARM64_D8,
                               addressSpace.getDouble(savedRegisterLoc));
    savedRegisterLoc -= 8;
    registers.setFloatRegister(UNW_ARM64_D9,
                               addressSpace.getDouble(savedRegisterLoc));
    savedRegisterLoc -= 8;
  }
  if (encoding & UNWIND_ARM64_FRAME_D10_D11_PAIR) {
    registers.setFloatRegister(UNW_ARM64_D10,
                               addressSpace.getDouble(savedRegisterLoc));
    savedRegisterLoc -= 8;
    registers.setFloatRegister(UNW_ARM64_D11,
                               addressSpace.getDouble(savedRegisterLoc));
    savedRegisterLoc -= 8;
  }
  if (encoding & UNWIND_ARM64_FRAME_D12_D13_PAIR) {
    registers.setFloatRegister(UNW_ARM64_D12,
                               addressSpace.getDouble(savedRegisterLoc));
    savedRegisterLoc -= 8;
    registers.setFloatRegister(UNW_ARM64_D13,
                               addressSpace.getDouble(savedRegisterLoc));
    savedRegisterLoc -= 8;
  }
  if (encoding & UNWIND_ARM64_FRAME_D14_D15_PAIR) {
    registers.setFloatRegister(UNW_ARM64_D14,
                               addressSpace.getDouble(savedRegisterLoc));
    savedRegisterLoc -= 8;
    registers.setFloatRegister(UNW_ARM64_D15,
                               addressSpace.getDouble(savedRegisterLoc));
    savedRegisterLoc -= 8;
  }

  uint64_t fp = registers.getFP();
  // fp points to old fp
  registers.setFP(addressSpace.get64(fp));
  // old sp is fp less saved fp and lr
  registers.setSP(fp + 16);
  // pop return address into pc
  registers.setIP(addressSpace.get64(fp + 8));

  return UNW_STEP_SUCCESS;
}
#endif // _LIBUNWIND_TARGET_AARCH64


} // namespace libunwind

#endif // __COMPACT_UNWINDER_HPP__
