//===- AArch64AddressingModes.h - AArch64 Addressing Modes ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the AArch64 addressing mode implementation stuff.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_MCTARGETDESC_AARCH64ADDRESSINGMODES_H
#define LLVM_LIB_TARGET_AARCH64_MCTARGETDESC_AARCH64ADDRESSINGMODES_H

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/bit.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>

namespace llvm {

/// AArch64_AM - AArch64 Addressing Mode Stuff
namespace AArch64_AM {

//===----------------------------------------------------------------------===//
// Shifts
//

enum ShiftExtendType {
  InvalidShiftExtend = -1,
  LSL = 0,
  LSR,
  ASR,
  ROR,
  MSL,

  UXTB,
  UXTH,
  UXTW,
  UXTX,

  SXTB,
  SXTH,
  SXTW,
  SXTX,
};

/// getShiftName - Get the string encoding for the shift type.
static inline const char *getShiftExtendName(AArch64_AM::ShiftExtendType ST) {
  switch (ST) {
  default: llvm_unreachable("unhandled shift type!");
  case AArch64_AM::LSL: return "lsl";
  case AArch64_AM::LSR: return "lsr";
  case AArch64_AM::ASR: return "asr";
  case AArch64_AM::ROR: return "ror";
  case AArch64_AM::MSL: return "msl";
  case AArch64_AM::UXTB: return "uxtb";
  case AArch64_AM::UXTH: return "uxth";
  case AArch64_AM::UXTW: return "uxtw";
  case AArch64_AM::UXTX: return "uxtx";
  case AArch64_AM::SXTB: return "sxtb";
  case AArch64_AM::SXTH: return "sxth";
  case AArch64_AM::SXTW: return "sxtw";
  case AArch64_AM::SXTX: return "sxtx";
  }
  return nullptr;
}

/// getShiftType - Extract the shift type.
static inline AArch64_AM::ShiftExtendType getShiftType(unsigned Imm) {
  switch ((Imm >> 6) & 0x7) {
  default: return AArch64_AM::InvalidShiftExtend;
  case 0: return AArch64_AM::LSL;
  case 1: return AArch64_AM::LSR;
  case 2: return AArch64_AM::ASR;
  case 3: return AArch64_AM::ROR;
  case 4: return AArch64_AM::MSL;
  }
}

/// getShiftValue - Extract the shift value.
static inline unsigned getShiftValue(unsigned Imm) {
  return Imm & 0x3f;
}

/// getShifterImm - Encode the shift type and amount:
///   imm:     6-bit shift amount
///   shifter: 000 ==> lsl
///            001 ==> lsr
///            010 ==> asr
///            011 ==> ror
///            100 ==> msl
///   {8-6}  = shifter
///   {5-0}  = imm
static inline unsigned getShifterImm(AArch64_AM::ShiftExtendType ST,
                                     unsigned Imm) {
  assert((Imm & 0x3f) == Imm && "Illegal shifted immedate value!");
  unsigned STEnc = 0;
  switch (ST) {
  default:  llvm_unreachable("Invalid shift requested");
  case AArch64_AM::LSL: STEnc = 0; break;
  case AArch64_AM::LSR: STEnc = 1; break;
  case AArch64_AM::ASR: STEnc = 2; break;
  case AArch64_AM::ROR: STEnc = 3; break;
  case AArch64_AM::MSL: STEnc = 4; break;
  }
  return (STEnc << 6) | (Imm & 0x3f);
}

//===----------------------------------------------------------------------===//
// Extends
//

/// getArithShiftValue - get the arithmetic shift value.
static inline unsigned getArithShiftValue(unsigned Imm) {
  return Imm & 0x7;
}

/// getExtendType - Extract the extend type for operands of arithmetic ops.
static inline AArch64_AM::ShiftExtendType getExtendType(unsigned Imm) {
  assert((Imm & 0x7) == Imm && "invalid immediate!");
  switch (Imm) {
  default: llvm_unreachable("Compiler bug!");
  case 0: return AArch64_AM::UXTB;
  case 1: return AArch64_AM::UXTH;
  case 2: return AArch64_AM::UXTW;
  case 3: return AArch64_AM::UXTX;
  case 4: return AArch64_AM::SXTB;
  case 5: return AArch64_AM::SXTH;
  case 6: return AArch64_AM::SXTW;
  case 7: return AArch64_AM::SXTX;
  }
}

static inline AArch64_AM::ShiftExtendType getArithExtendType(unsigned Imm) {
  return getExtendType((Imm >> 3) & 0x7);
}

/// Mapping from extend bits to required operation:
///   shifter: 000 ==> uxtb
///            001 ==> uxth
///            010 ==> uxtw
///            011 ==> uxtx
///            100 ==> sxtb
///            101 ==> sxth
///            110 ==> sxtw
///            111 ==> sxtx
inline unsigned getExtendEncoding(AArch64_AM::ShiftExtendType ET) {
  switch (ET) {
  default: llvm_unreachable("Invalid extend type requested");
  case AArch64_AM::UXTB: return 0; break;
  case AArch64_AM::UXTH: return 1; break;
  case AArch64_AM::UXTW: return 2; break;
  case AArch64_AM::UXTX: return 3; break;
  case AArch64_AM::SXTB: return 4; break;
  case AArch64_AM::SXTH: return 5; break;
  case AArch64_AM::SXTW: return 6; break;
  case AArch64_AM::SXTX: return 7; break;
  }
}

/// getArithExtendImm - Encode the extend type and shift amount for an
///                     arithmetic instruction:
///   imm:     3-bit extend amount
///   {5-3}  = shifter
///   {2-0}  = imm3
static inline unsigned getArithExtendImm(AArch64_AM::ShiftExtendType ET,
                                         unsigned Imm) {
  assert((Imm & 0x7) == Imm && "Illegal shifted immedate value!");
  return (getExtendEncoding(ET) << 3) | (Imm & 0x7);
}

/// getMemDoShift - Extract the "do shift" flag value for load/store
/// instructions.
static inline bool getMemDoShift(unsigned Imm) {
  return (Imm & 0x1) != 0;
}

/// getExtendType - Extract the extend type for the offset operand of
/// loads/stores.
static inline AArch64_AM::ShiftExtendType getMemExtendType(unsigned Imm) {
  return getExtendType((Imm >> 1) & 0x7);
}

/// getExtendImm - Encode the extend type and amount for a load/store inst:
///   doshift:     should the offset be scaled by the access size
///   shifter: 000 ==> uxtb
///            001 ==> uxth
///            010 ==> uxtw
///            011 ==> uxtx
///            100 ==> sxtb
///            101 ==> sxth
///            110 ==> sxtw
///            111 ==> sxtx
///   {3-1}  = shifter
///   {0}  = doshift
static inline unsigned getMemExtendImm(AArch64_AM::ShiftExtendType ET,
                                       bool DoShift) {
  return (getExtendEncoding(ET) << 1) | unsigned(DoShift);
}

static inline uint64_t ror(uint64_t elt, unsigned size) {
  return ((elt & 1) << (size-1)) | (elt >> 1);
}

/// processLogicalImmediate - Determine if an immediate value can be encoded
/// as the immediate operand of a logical instruction for the given register
/// size.  If so, return true with "encoding" set to the encoded value in
/// the form N:immr:imms.
static inline bool processLogicalImmediate(uint64_t Imm, unsigned RegSize,
                                           uint64_t &Encoding) {
  if (Imm == 0ULL || Imm == ~0ULL ||
      (RegSize != 64 &&
        (Imm >> RegSize != 0 || Imm == (~0ULL >> (64 - RegSize)))))
    return false;

  // First, determine the element size.
  unsigned Size = RegSize;

  do {
    Size /= 2;
    uint64_t Mask = (1ULL << Size) - 1;

    if ((Imm & Mask) != ((Imm >> Size) & Mask)) {
      Size *= 2;
      break;
    }
  } while (Size > 2);

  // Second, determine the rotation to make the element be: 0^m 1^n.
  uint32_t CTO, I;
  uint64_t Mask = ((uint64_t)-1LL) >> (64 - Size);
  Imm &= Mask;

  if (isShiftedMask_64(Imm)) {
    I = countTrailingZeros(Imm);
    assert(I < 64 && "undefined behavior");
    CTO = countTrailingOnes(Imm >> I);
  } else {
    Imm |= ~Mask;
    if (!isShiftedMask_64(~Imm))
      return false;

    unsigned CLO = countLeadingOnes(Imm);
    I = 64 - CLO;
    CTO = CLO + countTrailingOnes(Imm) - (64 - Size);
  }

  // Encode in Immr the number of RORs it would take to get *from* 0^m 1^n
  // to our target value, where I is the number of RORs to go the opposite
  // direction.
  assert(Size > I && "I should be smaller than element size");
  unsigned Immr = (Size - I) & (Size - 1);

  // If size has a 1 in the n'th bit, create a value that has zeroes in
  // bits [0, n] and ones above that.
  uint64_t NImms = ~(Size-1) << 1;

  // Or the CTO value into the low bits, which must be below the Nth bit
  // bit mentioned above.
  NImms |= (CTO-1);

  // Extract the seventh bit and toggle it to create the N field.
  unsigned N = ((NImms >> 6) & 1) ^ 1;

  Encoding = (N << 12) | (Immr << 6) | (NImms & 0x3f);
  return true;
}

/// isLogicalImmediate - Return true if the immediate is valid for a logical
/// immediate instruction of the given register size. Return false otherwise.
static inline bool isLogicalImmediate(uint64_t imm, unsigned regSize) {
  uint64_t encoding;
  return processLogicalImmediate(imm, regSize, encoding);
}

/// encodeLogicalImmediate - Return the encoded immediate value for a logical
/// immediate instruction of the given register size.
static inline uint64_t encodeLogicalImmediate(uint64_t imm, unsigned regSize) {
  uint64_t encoding = 0;
  bool res = processLogicalImmediate(imm, regSize, encoding);
  assert(res && "invalid logical immediate");
  (void)res;
  return encoding;
}

/// decodeLogicalImmediate - Decode a logical immediate value in the form
/// "N:immr:imms" (where the immr and imms fields are each 6 bits) into the
/// integer value it represents with regSize bits.
static inline uint64_t decodeLogicalImmediate(uint64_t val, unsigned regSize) {
  // Extract the N, imms, and immr fields.
  unsigned N = (val >> 12) & 1;
  unsigned immr = (val >> 6) & 0x3f;
  unsigned imms = val & 0x3f;

  assert((regSize == 64 || N == 0) && "undefined logical immediate encoding");
  int len = 31 - countLeadingZeros((N << 6) | (~imms & 0x3f));
  assert(len >= 0 && "undefined logical immediate encoding");
  unsigned size = (1 << len);
  unsigned R = immr & (size - 1);
  unsigned S = imms & (size - 1);
  assert(S != size - 1 && "undefined logical immediate encoding");
  uint64_t pattern = (1ULL << (S + 1)) - 1;
  for (unsigned i = 0; i < R; ++i)
    pattern = ror(pattern, size);

  // Replicate the pattern to fill the regSize.
  while (size != regSize) {
    pattern |= (pattern << size);
    size *= 2;
  }
  return pattern;
}

/// isValidDecodeLogicalImmediate - Check to see if the logical immediate value
/// in the form "N:immr:imms" (where the immr and imms fields are each 6 bits)
/// is a valid encoding for an integer value with regSize bits.
static inline bool isValidDecodeLogicalImmediate(uint64_t val,
                                                 unsigned regSize) {
  // Extract the N and imms fields needed for checking.
  unsigned N = (val >> 12) & 1;
  unsigned imms = val & 0x3f;

  if (regSize == 32 && N != 0) // undefined logical immediate encoding
    return false;
  int len = 31 - countLeadingZeros((N << 6) | (~imms & 0x3f));
  if (len < 0) // undefined logical immediate encoding
    return false;
  unsigned size = (1 << len);
  unsigned S = imms & (size - 1);
  if (S == size - 1) // undefined logical immediate encoding
    return false;

  return true;
}

//===----------------------------------------------------------------------===//
// Floating-point Immediates
//
static inline float getFPImmFloat(unsigned Imm) {
  // We expect an 8-bit binary encoding of a floating-point number here.

  uint8_t Sign = (Imm >> 7) & 0x1;
  uint8_t Exp = (Imm >> 4) & 0x7;
  uint8_t Mantissa = Imm & 0xf;

  //   8-bit FP    IEEE Float Encoding
  //   abcd efgh   aBbbbbbc defgh000 00000000 00000000
  //
  // where B = NOT(b);

  uint32_t I = 0;
  I |= Sign << 31;
  I |= ((Exp & 0x4) != 0 ? 0 : 1) << 30;
  I |= ((Exp & 0x4) != 0 ? 0x1f : 0) << 25;
  I |= (Exp & 0x3) << 23;
  I |= Mantissa << 19;
  return bit_cast<float>(I);
}

/// getFP16Imm - Return an 8-bit floating-point version of the 16-bit
/// floating-point value. If the value cannot be represented as an 8-bit
/// floating-point value, then return -1.
static inline int getFP16Imm(const APInt &Imm) {
  uint32_t Sign = Imm.lshr(15).getZExtValue() & 1;
  int32_t Exp = (Imm.lshr(10).getSExtValue() & 0x1f) - 15;  // -14 to 15
  int32_t Mantissa = Imm.getZExtValue() & 0x3ff;  // 10 bits

  // We can handle 4 bits of mantissa.
  // mantissa = (16+UInt(e:f:g:h))/16.
  if (Mantissa & 0x3f)
    return -1;
  Mantissa >>= 6;

  // We can handle 3 bits of exponent: exp == UInt(NOT(b):c:d)-3
  if (Exp < -3 || Exp > 4)
    return -1;
  Exp = ((Exp+3) & 0x7) ^ 4;

  return ((int)Sign << 7) | (Exp << 4) | Mantissa;
}

static inline int getFP16Imm(const APFloat &FPImm) {
  return getFP16Imm(FPImm.bitcastToAPInt());
}

/// getFP32Imm - Return an 8-bit floating-point version of the 32-bit
/// floating-point value. If the value cannot be represented as an 8-bit
/// floating-point value, then return -1.
static inline int getFP32Imm(const APInt &Imm) {
  uint32_t Sign = Imm.lshr(31).getZExtValue() & 1;
  int32_t Exp = (Imm.lshr(23).getSExtValue() & 0xff) - 127;  // -126 to 127
  int64_t Mantissa = Imm.getZExtValue() & 0x7fffff;  // 23 bits

  // We can handle 4 bits of mantissa.
  // mantissa = (16+UInt(e:f:g:h))/16.
  if (Mantissa & 0x7ffff)
    return -1;
  Mantissa >>= 19;
  if ((Mantissa & 0xf) != Mantissa)
    return -1;

  // We can handle 3 bits of exponent: exp == UInt(NOT(b):c:d)-3
  if (Exp < -3 || Exp > 4)
    return -1;
  Exp = ((Exp+3) & 0x7) ^ 4;

  return ((int)Sign << 7) | (Exp << 4) | Mantissa;
}

static inline int getFP32Imm(const APFloat &FPImm) {
  return getFP32Imm(FPImm.bitcastToAPInt());
}

/// getFP64Imm - Return an 8-bit floating-point version of the 64-bit
/// floating-point value. If the value cannot be represented as an 8-bit
/// floating-point value, then return -1.
static inline int getFP64Imm(const APInt &Imm) {
  uint64_t Sign = Imm.lshr(63).getZExtValue() & 1;
  int64_t Exp = (Imm.lshr(52).getSExtValue() & 0x7ff) - 1023;   // -1022 to 1023
  uint64_t Mantissa = Imm.getZExtValue() & 0xfffffffffffffULL;

  // We can handle 4 bits of mantissa.
  // mantissa = (16+UInt(e:f:g:h))/16.
  if (Mantissa & 0xffffffffffffULL)
    return -1;
  Mantissa >>= 48;
  if ((Mantissa & 0xf) != Mantissa)
    return -1;

  // We can handle 3 bits of exponent: exp == UInt(NOT(b):c:d)-3
  if (Exp < -3 || Exp > 4)
    return -1;
  Exp = ((Exp+3) & 0x7) ^ 4;

  return ((int)Sign << 7) | (Exp << 4) | Mantissa;
}

static inline int getFP64Imm(const APFloat &FPImm) {
  return getFP64Imm(FPImm.bitcastToAPInt());
}

//===--------------------------------------------------------------------===//
// AdvSIMD Modified Immediates
//===--------------------------------------------------------------------===//

// 0x00 0x00 0x00 abcdefgh 0x00 0x00 0x00 abcdefgh
static inline bool isAdvSIMDModImmType1(uint64_t Imm) {
  return ((Imm >> 32) == (Imm & 0xffffffffULL)) &&
         ((Imm & 0xffffff00ffffff00ULL) == 0);
}

static inline uint8_t encodeAdvSIMDModImmType1(uint64_t Imm) {
  return (Imm & 0xffULL);
}

static inline uint64_t decodeAdvSIMDModImmType1(uint8_t Imm) {
  uint64_t EncVal = Imm;
  return (EncVal << 32) | EncVal;
}

// 0x00 0x00 abcdefgh 0x00 0x00 0x00 abcdefgh 0x00
static inline bool isAdvSIMDModImmType2(uint64_t Imm) {
  return ((Imm >> 32) == (Imm & 0xffffffffULL)) &&
         ((Imm & 0xffff00ffffff00ffULL) == 0);
}

static inline uint8_t encodeAdvSIMDModImmType2(uint64_t Imm) {
  return (Imm & 0xff00ULL) >> 8;
}

static inline uint64_t decodeAdvSIMDModImmType2(uint8_t Imm) {
  uint64_t EncVal = Imm;
  return (EncVal << 40) | (EncVal << 8);
}

// 0x00 abcdefgh 0x00 0x00 0x00 abcdefgh 0x00 0x00
static inline bool isAdvSIMDModImmType3(uint64_t Imm) {
  return ((Imm >> 32) == (Imm & 0xffffffffULL)) &&
         ((Imm & 0xff00ffffff00ffffULL) == 0);
}

static inline uint8_t encodeAdvSIMDModImmType3(uint64_t Imm) {
  return (Imm & 0xff0000ULL) >> 16;
}

static inline uint64_t decodeAdvSIMDModImmType3(uint8_t Imm) {
  uint64_t EncVal = Imm;
  return (EncVal << 48) | (EncVal << 16);
}

// abcdefgh 0x00 0x00 0x00 abcdefgh 0x00 0x00 0x00
static inline bool isAdvSIMDModImmType4(uint64_t Imm) {
  return ((Imm >> 32) == (Imm & 0xffffffffULL)) &&
         ((Imm & 0x00ffffff00ffffffULL) == 0);
}

static inline uint8_t encodeAdvSIMDModImmType4(uint64_t Imm) {
  return (Imm & 0xff000000ULL) >> 24;
}

static inline uint64_t decodeAdvSIMDModImmType4(uint8_t Imm) {
  uint64_t EncVal = Imm;
  return (EncVal << 56) | (EncVal << 24);
}

// 0x00 abcdefgh 0x00 abcdefgh 0x00 abcdefgh 0x00 abcdefgh
static inline bool isAdvSIMDModImmType5(uint64_t Imm) {
  return ((Imm >> 32) == (Imm & 0xffffffffULL)) &&
         (((Imm & 0x00ff0000ULL) >> 16) == (Imm & 0x000000ffULL)) &&
         ((Imm & 0xff00ff00ff00ff00ULL) == 0);
}

static inline uint8_t encodeAdvSIMDModImmType5(uint64_t Imm) {
  return (Imm & 0xffULL);
}

static inline uint64_t decodeAdvSIMDModImmType5(uint8_t Imm) {
  uint64_t EncVal = Imm;
  return (EncVal << 48) | (EncVal << 32) | (EncVal << 16) | EncVal;
}

// abcdefgh 0x00 abcdefgh 0x00 abcdefgh 0x00 abcdefgh 0x00
static inline bool isAdvSIMDModImmType6(uint64_t Imm) {
  return ((Imm >> 32) == (Imm & 0xffffffffULL)) &&
         (((Imm & 0xff000000ULL) >> 16) == (Imm & 0x0000ff00ULL)) &&
         ((Imm & 0x00ff00ff00ff00ffULL) == 0);
}

static inline uint8_t encodeAdvSIMDModImmType6(uint64_t Imm) {
  return (Imm & 0xff00ULL) >> 8;
}

static inline uint64_t decodeAdvSIMDModImmType6(uint8_t Imm) {
  uint64_t EncVal = Imm;
  return (EncVal << 56) | (EncVal << 40) | (EncVal << 24) | (EncVal << 8);
}

// 0x00 0x00 abcdefgh 0xFF 0x00 0x00 abcdefgh 0xFF
static inline bool isAdvSIMDModImmType7(uint64_t Imm) {
  return ((Imm >> 32) == (Imm & 0xffffffffULL)) &&
         ((Imm & 0xffff00ffffff00ffULL) == 0x000000ff000000ffULL);
}

static inline uint8_t encodeAdvSIMDModImmType7(uint64_t Imm) {
  return (Imm & 0xff00ULL) >> 8;
}

static inline uint64_t decodeAdvSIMDModImmType7(uint8_t Imm) {
  uint64_t EncVal = Imm;
  return (EncVal << 40) | (EncVal << 8) | 0x000000ff000000ffULL;
}

// 0x00 abcdefgh 0xFF 0xFF 0x00 abcdefgh 0xFF 0xFF
static inline bool isAdvSIMDModImmType8(uint64_t Imm) {
  return ((Imm >> 32) == (Imm & 0xffffffffULL)) &&
         ((Imm & 0xff00ffffff00ffffULL) == 0x0000ffff0000ffffULL);
}

static inline uint64_t decodeAdvSIMDModImmType8(uint8_t Imm) {
  uint64_t EncVal = Imm;
  return (EncVal << 48) | (EncVal << 16) | 0x0000ffff0000ffffULL;
}

static inline uint8_t encodeAdvSIMDModImmType8(uint64_t Imm) {
  return (Imm & 0x00ff0000ULL) >> 16;
}

// abcdefgh abcdefgh abcdefgh abcdefgh abcdefgh abcdefgh abcdefgh abcdefgh
static inline bool isAdvSIMDModImmType9(uint64_t Imm) {
  return ((Imm >> 32) == (Imm & 0xffffffffULL)) &&
         ((Imm >> 48) == (Imm & 0x0000ffffULL)) &&
         ((Imm >> 56) == (Imm & 0x000000ffULL));
}

static inline uint8_t encodeAdvSIMDModImmType9(uint64_t Imm) {
  return (Imm & 0xffULL);
}

static inline uint64_t decodeAdvSIMDModImmType9(uint8_t Imm) {
  uint64_t EncVal = Imm;
  EncVal |= (EncVal << 8);
  EncVal |= (EncVal << 16);
  EncVal |= (EncVal << 32);
  return EncVal;
}

// aaaaaaaa bbbbbbbb cccccccc dddddddd eeeeeeee ffffffff gggggggg hhhhhhhh
// cmode: 1110, op: 1
static inline bool isAdvSIMDModImmType10(uint64_t Imm) {
  uint64_t ByteA = Imm & 0xff00000000000000ULL;
  uint64_t ByteB = Imm & 0x00ff000000000000ULL;
  uint64_t ByteC = Imm & 0x0000ff0000000000ULL;
  uint64_t ByteD = Imm & 0x000000ff00000000ULL;
  uint64_t ByteE = Imm & 0x00000000ff000000ULL;
  uint64_t ByteF = Imm & 0x0000000000ff0000ULL;
  uint64_t ByteG = Imm & 0x000000000000ff00ULL;
  uint64_t ByteH = Imm & 0x00000000000000ffULL;

  return (ByteA == 0ULL || ByteA == 0xff00000000000000ULL) &&
         (ByteB == 0ULL || ByteB == 0x00ff000000000000ULL) &&
         (ByteC == 0ULL || ByteC == 0x0000ff0000000000ULL) &&
         (ByteD == 0ULL || ByteD == 0x000000ff00000000ULL) &&
         (ByteE == 0ULL || ByteE == 0x00000000ff000000ULL) &&
         (ByteF == 0ULL || ByteF == 0x0000000000ff0000ULL) &&
         (ByteG == 0ULL || ByteG == 0x000000000000ff00ULL) &&
         (ByteH == 0ULL || ByteH == 0x00000000000000ffULL);
}

static inline uint8_t encodeAdvSIMDModImmType10(uint64_t Imm) {
  uint8_t BitA = (Imm & 0xff00000000000000ULL) != 0;
  uint8_t BitB = (Imm & 0x00ff000000000000ULL) != 0;
  uint8_t BitC = (Imm & 0x0000ff0000000000ULL) != 0;
  uint8_t BitD = (Imm & 0x000000ff00000000ULL) != 0;
  uint8_t BitE = (Imm & 0x00000000ff000000ULL) != 0;
  uint8_t BitF = (Imm & 0x0000000000ff0000ULL) != 0;
  uint8_t BitG = (Imm & 0x000000000000ff00ULL) != 0;
  uint8_t BitH = (Imm & 0x00000000000000ffULL) != 0;

  uint8_t EncVal = BitA;
  EncVal <<= 1;
  EncVal |= BitB;
  EncVal <<= 1;
  EncVal |= BitC;
  EncVal <<= 1;
  EncVal |= BitD;
  EncVal <<= 1;
  EncVal |= BitE;
  EncVal <<= 1;
  EncVal |= BitF;
  EncVal <<= 1;
  EncVal |= BitG;
  EncVal <<= 1;
  EncVal |= BitH;
  return EncVal;
}

static inline uint64_t decodeAdvSIMDModImmType10(uint8_t Imm) {
  uint64_t EncVal = 0;
  if (Imm & 0x80) EncVal |= 0xff00000000000000ULL;
  if (Imm & 0x40) EncVal |= 0x00ff000000000000ULL;
  if (Imm & 0x20) EncVal |= 0x0000ff0000000000ULL;
  if (Imm & 0x10) EncVal |= 0x000000ff00000000ULL;
  if (Imm & 0x08) EncVal |= 0x00000000ff000000ULL;
  if (Imm & 0x04) EncVal |= 0x0000000000ff0000ULL;
  if (Imm & 0x02) EncVal |= 0x000000000000ff00ULL;
  if (Imm & 0x01) EncVal |= 0x00000000000000ffULL;
  return EncVal;
}

// aBbbbbbc defgh000 0x00 0x00 aBbbbbbc defgh000 0x00 0x00
static inline bool isAdvSIMDModImmType11(uint64_t Imm) {
  uint64_t BString = (Imm & 0x7E000000ULL) >> 25;
  return ((Imm >> 32) == (Imm & 0xffffffffULL)) &&
         (BString == 0x1f || BString == 0x20) &&
         ((Imm & 0x0007ffff0007ffffULL) == 0);
}

static inline uint8_t encodeAdvSIMDModImmType11(uint64_t Imm) {
  uint8_t BitA = (Imm & 0x80000000ULL) != 0;
  uint8_t BitB = (Imm & 0x20000000ULL) != 0;
  uint8_t BitC = (Imm & 0x01000000ULL) != 0;
  uint8_t BitD = (Imm & 0x00800000ULL) != 0;
  uint8_t BitE = (Imm & 0x00400000ULL) != 0;
  uint8_t BitF = (Imm & 0x00200000ULL) != 0;
  uint8_t BitG = (Imm & 0x00100000ULL) != 0;
  uint8_t BitH = (Imm & 0x00080000ULL) != 0;

  uint8_t EncVal = BitA;
  EncVal <<= 1;
  EncVal |= BitB;
  EncVal <<= 1;
  EncVal |= BitC;
  EncVal <<= 1;
  EncVal |= BitD;
  EncVal <<= 1;
  EncVal |= BitE;
  EncVal <<= 1;
  EncVal |= BitF;
  EncVal <<= 1;
  EncVal |= BitG;
  EncVal <<= 1;
  EncVal |= BitH;
  return EncVal;
}

static inline uint64_t decodeAdvSIMDModImmType11(uint8_t Imm) {
  uint64_t EncVal = 0;
  if (Imm & 0x80) EncVal |= 0x80000000ULL;
  if (Imm & 0x40) EncVal |= 0x3e000000ULL;
  else            EncVal |= 0x40000000ULL;
  if (Imm & 0x20) EncVal |= 0x01000000ULL;
  if (Imm & 0x10) EncVal |= 0x00800000ULL;
  if (Imm & 0x08) EncVal |= 0x00400000ULL;
  if (Imm & 0x04) EncVal |= 0x00200000ULL;
  if (Imm & 0x02) EncVal |= 0x00100000ULL;
  if (Imm & 0x01) EncVal |= 0x00080000ULL;
  return (EncVal << 32) | EncVal;
}

// aBbbbbbb bbcdefgh 0x00 0x00 0x00 0x00 0x00 0x00
static inline bool isAdvSIMDModImmType12(uint64_t Imm) {
  uint64_t BString = (Imm & 0x7fc0000000000000ULL) >> 54;
  return ((BString == 0xff || BString == 0x100) &&
         ((Imm & 0x0000ffffffffffffULL) == 0));
}

static inline uint8_t encodeAdvSIMDModImmType12(uint64_t Imm) {
  uint8_t BitA = (Imm & 0x8000000000000000ULL) != 0;
  uint8_t BitB = (Imm & 0x0040000000000000ULL) != 0;
  uint8_t BitC = (Imm & 0x0020000000000000ULL) != 0;
  uint8_t BitD = (Imm & 0x0010000000000000ULL) != 0;
  uint8_t BitE = (Imm & 0x0008000000000000ULL) != 0;
  uint8_t BitF = (Imm & 0x0004000000000000ULL) != 0;
  uint8_t BitG = (Imm & 0x0002000000000000ULL) != 0;
  uint8_t BitH = (Imm & 0x0001000000000000ULL) != 0;

  uint8_t EncVal = BitA;
  EncVal <<= 1;
  EncVal |= BitB;
  EncVal <<= 1;
  EncVal |= BitC;
  EncVal <<= 1;
  EncVal |= BitD;
  EncVal <<= 1;
  EncVal |= BitE;
  EncVal <<= 1;
  EncVal |= BitF;
  EncVal <<= 1;
  EncVal |= BitG;
  EncVal <<= 1;
  EncVal |= BitH;
  return EncVal;
}

static inline uint64_t decodeAdvSIMDModImmType12(uint8_t Imm) {
  uint64_t EncVal = 0;
  if (Imm & 0x80) EncVal |= 0x8000000000000000ULL;
  if (Imm & 0x40) EncVal |= 0x3fc0000000000000ULL;
  else            EncVal |= 0x4000000000000000ULL;
  if (Imm & 0x20) EncVal |= 0x0020000000000000ULL;
  if (Imm & 0x10) EncVal |= 0x0010000000000000ULL;
  if (Imm & 0x08) EncVal |= 0x0008000000000000ULL;
  if (Imm & 0x04) EncVal |= 0x0004000000000000ULL;
  if (Imm & 0x02) EncVal |= 0x0002000000000000ULL;
  if (Imm & 0x01) EncVal |= 0x0001000000000000ULL;
  return (EncVal << 32) | EncVal;
}

/// Returns true if Imm is the concatenation of a repeating pattern of type T.
template <typename T>
static inline bool isSVEMaskOfIdenticalElements(int64_t Imm) {
  auto Parts = bit_cast<std::array<T, sizeof(int64_t) / sizeof(T)>>(Imm);
  return all_of(Parts, [&](T Elem) { return Elem == Parts[0]; });
}

/// Returns true if Imm is valid for CPY/DUP.
template <typename T>
static inline bool isSVECpyImm(int64_t Imm) {
  bool IsImm8 = int8_t(Imm) == Imm;
  bool IsImm16 = int16_t(Imm & ~0xff) == Imm;

  if (std::is_same<int8_t, typename std::make_signed<T>::type>::value)
    return IsImm8 || uint8_t(Imm) == Imm;

  if (std::is_same<int16_t, typename std::make_signed<T>::type>::value)
    return IsImm8 || IsImm16 || uint16_t(Imm & ~0xff) == Imm;

  return IsImm8 || IsImm16;
}

/// Returns true if Imm is valid for ADD/SUB.
template <typename T>
static inline bool isSVEAddSubImm(int64_t Imm) {
  bool IsInt8t =
      std::is_same<int8_t, typename std::make_signed<T>::type>::value;
  return uint8_t(Imm) == Imm || (!IsInt8t && uint16_t(Imm & ~0xff) == Imm);
}

/// Return true if Imm is valid for DUPM and has no single CPY/DUP equivalent.
static inline bool isSVEMoveMaskPreferredLogicalImmediate(int64_t Imm) {
  if (isSVECpyImm<int64_t>(Imm))
    return false;

  auto S = bit_cast<std::array<int32_t, 2>>(Imm);
  auto H = bit_cast<std::array<int16_t, 4>>(Imm);
  auto B = bit_cast<std::array<int8_t, 8>>(Imm);

  if (isSVEMaskOfIdenticalElements<int32_t>(Imm) && isSVECpyImm<int32_t>(S[0]))
    return false;
  if (isSVEMaskOfIdenticalElements<int16_t>(Imm) && isSVECpyImm<int16_t>(H[0]))
    return false;
  if (isSVEMaskOfIdenticalElements<int8_t>(Imm) && isSVECpyImm<int8_t>(B[0]))
    return false;
  return isLogicalImmediate(Imm, 64);
}

inline static bool isAnyMOVZMovAlias(uint64_t Value, int RegWidth) {
  for (int Shift = 0; Shift <= RegWidth - 16; Shift += 16)
    if ((Value & ~(0xffffULL << Shift)) == 0)
      return true;

  return false;
}

inline static bool isMOVZMovAlias(uint64_t Value, int Shift, int RegWidth) {
  if (RegWidth == 32)
    Value &= 0xffffffffULL;

  // "lsl #0" takes precedence: in practice this only affects "#0, lsl #0".
  if (Value == 0 && Shift != 0)
    return false;

  return (Value & ~(0xffffULL << Shift)) == 0;
}

inline static bool isMOVNMovAlias(uint64_t Value, int Shift, int RegWidth) {
  // MOVZ takes precedence over MOVN.
  if (isAnyMOVZMovAlias(Value, RegWidth))
    return false;

  Value = ~Value;
  if (RegWidth == 32)
    Value &= 0xffffffffULL;

  return isMOVZMovAlias(Value, Shift, RegWidth);
}

inline static bool isAnyMOVWMovAlias(uint64_t Value, int RegWidth) {
  if (isAnyMOVZMovAlias(Value, RegWidth))
    return true;

  // It's not a MOVZ, but it might be a MOVN.
  Value = ~Value;
  if (RegWidth == 32)
    Value &= 0xffffffffULL;

  return isAnyMOVZMovAlias(Value, RegWidth);
}

} // end namespace AArch64_AM

} // end namespace llvm

#endif
