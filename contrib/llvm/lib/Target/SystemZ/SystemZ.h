//==- SystemZ.h - Top-Level Interface for SystemZ representation -*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in
// the LLVM SystemZ backend.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZ_H
#define LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZ_H

#include "MCTargetDesc/SystemZMCTargetDesc.h"
#include "llvm/Support/CodeGen.h"

namespace llvm {
class SystemZTargetMachine;
class FunctionPass;

namespace SystemZ {
// Condition-code mask values.
const unsigned CCMASK_0 = 1 << 3;
const unsigned CCMASK_1 = 1 << 2;
const unsigned CCMASK_2 = 1 << 1;
const unsigned CCMASK_3 = 1 << 0;
const unsigned CCMASK_ANY = CCMASK_0 | CCMASK_1 | CCMASK_2 | CCMASK_3;

// Condition-code mask assignments for integer and floating-point
// comparisons.
const unsigned CCMASK_CMP_EQ = CCMASK_0;
const unsigned CCMASK_CMP_LT = CCMASK_1;
const unsigned CCMASK_CMP_GT = CCMASK_2;
const unsigned CCMASK_CMP_NE = CCMASK_CMP_LT | CCMASK_CMP_GT;
const unsigned CCMASK_CMP_LE = CCMASK_CMP_EQ | CCMASK_CMP_LT;
const unsigned CCMASK_CMP_GE = CCMASK_CMP_EQ | CCMASK_CMP_GT;

// Condition-code mask assignments for floating-point comparisons only.
const unsigned CCMASK_CMP_UO = CCMASK_3;
const unsigned CCMASK_CMP_O  = CCMASK_ANY ^ CCMASK_CMP_UO;

// All condition-code values produced by comparisons.
const unsigned CCMASK_ICMP = CCMASK_0 | CCMASK_1 | CCMASK_2;
const unsigned CCMASK_FCMP = CCMASK_0 | CCMASK_1 | CCMASK_2 | CCMASK_3;

// Condition-code mask assignments for arithmetical operations.
const unsigned CCMASK_ARITH_EQ       = CCMASK_0;
const unsigned CCMASK_ARITH_LT       = CCMASK_1;
const unsigned CCMASK_ARITH_GT       = CCMASK_2;
const unsigned CCMASK_ARITH_OVERFLOW = CCMASK_3;
const unsigned CCMASK_ARITH          = CCMASK_ANY;

// Condition-code mask assignments for logical operations.
const unsigned CCMASK_LOGICAL_ZERO     = CCMASK_0 | CCMASK_2;
const unsigned CCMASK_LOGICAL_NONZERO  = CCMASK_1 | CCMASK_2;
const unsigned CCMASK_LOGICAL_CARRY    = CCMASK_2 | CCMASK_3;
const unsigned CCMASK_LOGICAL_NOCARRY  = CCMASK_0 | CCMASK_1;
const unsigned CCMASK_LOGICAL_BORROW   = CCMASK_LOGICAL_NOCARRY;
const unsigned CCMASK_LOGICAL_NOBORROW = CCMASK_LOGICAL_CARRY;
const unsigned CCMASK_LOGICAL          = CCMASK_ANY;

// Condition-code mask assignments for CS.
const unsigned CCMASK_CS_EQ = CCMASK_0;
const unsigned CCMASK_CS_NE = CCMASK_1;
const unsigned CCMASK_CS    = CCMASK_0 | CCMASK_1;

// Condition-code mask assignments for a completed SRST loop.
const unsigned CCMASK_SRST_FOUND    = CCMASK_1;
const unsigned CCMASK_SRST_NOTFOUND = CCMASK_2;
const unsigned CCMASK_SRST          = CCMASK_1 | CCMASK_2;

// Condition-code mask assignments for TEST UNDER MASK.
const unsigned CCMASK_TM_ALL_0       = CCMASK_0;
const unsigned CCMASK_TM_MIXED_MSB_0 = CCMASK_1;
const unsigned CCMASK_TM_MIXED_MSB_1 = CCMASK_2;
const unsigned CCMASK_TM_ALL_1       = CCMASK_3;
const unsigned CCMASK_TM_SOME_0      = CCMASK_TM_ALL_1 ^ CCMASK_ANY;
const unsigned CCMASK_TM_SOME_1      = CCMASK_TM_ALL_0 ^ CCMASK_ANY;
const unsigned CCMASK_TM_MSB_0       = CCMASK_0 | CCMASK_1;
const unsigned CCMASK_TM_MSB_1       = CCMASK_2 | CCMASK_3;
const unsigned CCMASK_TM             = CCMASK_ANY;

// Condition-code mask assignments for TRANSACTION_BEGIN.
const unsigned CCMASK_TBEGIN_STARTED       = CCMASK_0;
const unsigned CCMASK_TBEGIN_INDETERMINATE = CCMASK_1;
const unsigned CCMASK_TBEGIN_TRANSIENT     = CCMASK_2;
const unsigned CCMASK_TBEGIN_PERSISTENT    = CCMASK_3;
const unsigned CCMASK_TBEGIN               = CCMASK_ANY;

// Condition-code mask assignments for TRANSACTION_END.
const unsigned CCMASK_TEND_TX   = CCMASK_0;
const unsigned CCMASK_TEND_NOTX = CCMASK_2;
const unsigned CCMASK_TEND      = CCMASK_TEND_TX | CCMASK_TEND_NOTX;

// Condition-code mask assignments for vector comparisons (and similar
// operations).
const unsigned CCMASK_VCMP_ALL       = CCMASK_0;
const unsigned CCMASK_VCMP_MIXED     = CCMASK_1;
const unsigned CCMASK_VCMP_NONE      = CCMASK_3;
const unsigned CCMASK_VCMP           = CCMASK_0 | CCMASK_1 | CCMASK_3;

// Condition-code mask assignments for Test Data Class.
const unsigned CCMASK_TDC_NOMATCH   = CCMASK_0;
const unsigned CCMASK_TDC_MATCH     = CCMASK_1;
const unsigned CCMASK_TDC           = CCMASK_TDC_NOMATCH | CCMASK_TDC_MATCH;

// The position of the low CC bit in an IPM result.
const unsigned IPM_CC = 28;

// Mask assignments for PFD.
const unsigned PFD_READ  = 1;
const unsigned PFD_WRITE = 2;

// Mask assignments for TDC
const unsigned TDCMASK_ZERO_PLUS       = 0x800;
const unsigned TDCMASK_ZERO_MINUS      = 0x400;
const unsigned TDCMASK_NORMAL_PLUS     = 0x200;
const unsigned TDCMASK_NORMAL_MINUS    = 0x100;
const unsigned TDCMASK_SUBNORMAL_PLUS  = 0x080;
const unsigned TDCMASK_SUBNORMAL_MINUS = 0x040;
const unsigned TDCMASK_INFINITY_PLUS   = 0x020;
const unsigned TDCMASK_INFINITY_MINUS  = 0x010;
const unsigned TDCMASK_QNAN_PLUS       = 0x008;
const unsigned TDCMASK_QNAN_MINUS      = 0x004;
const unsigned TDCMASK_SNAN_PLUS       = 0x002;
const unsigned TDCMASK_SNAN_MINUS      = 0x001;

const unsigned TDCMASK_ZERO            = TDCMASK_ZERO_PLUS | TDCMASK_ZERO_MINUS;
const unsigned TDCMASK_POSITIVE        = TDCMASK_NORMAL_PLUS |
                                         TDCMASK_SUBNORMAL_PLUS |
                                         TDCMASK_INFINITY_PLUS;
const unsigned TDCMASK_NEGATIVE        = TDCMASK_NORMAL_MINUS |
                                         TDCMASK_SUBNORMAL_MINUS |
                                         TDCMASK_INFINITY_MINUS;
const unsigned TDCMASK_NAN             = TDCMASK_QNAN_PLUS |
                                         TDCMASK_QNAN_MINUS |
                                         TDCMASK_SNAN_PLUS |
                                         TDCMASK_SNAN_MINUS;
const unsigned TDCMASK_PLUS            = TDCMASK_POSITIVE |
                                         TDCMASK_ZERO_PLUS |
                                         TDCMASK_QNAN_PLUS |
                                         TDCMASK_SNAN_PLUS;
const unsigned TDCMASK_MINUS           = TDCMASK_NEGATIVE |
                                         TDCMASK_ZERO_MINUS |
                                         TDCMASK_QNAN_MINUS |
                                         TDCMASK_SNAN_MINUS;
const unsigned TDCMASK_ALL             = TDCMASK_PLUS | TDCMASK_MINUS;

// Number of bits in a vector register.
const unsigned VectorBits = 128;

// Number of bytes in a vector register (and consequently the number of
// bytes in a general permute vector).
const unsigned VectorBytes = VectorBits / 8;

// Return true if Val fits an LLILL operand.
static inline bool isImmLL(uint64_t Val) {
  return (Val & ~0x000000000000ffffULL) == 0;
}

// Return true if Val fits an LLILH operand.
static inline bool isImmLH(uint64_t Val) {
  return (Val & ~0x00000000ffff0000ULL) == 0;
}

// Return true if Val fits an LLIHL operand.
static inline bool isImmHL(uint64_t Val) {
  return (Val & ~0x00000ffff00000000ULL) == 0;
}

// Return true if Val fits an LLIHH operand.
static inline bool isImmHH(uint64_t Val) {
  return (Val & ~0xffff000000000000ULL) == 0;
}

// Return true if Val fits an LLILF operand.
static inline bool isImmLF(uint64_t Val) {
  return (Val & ~0x00000000ffffffffULL) == 0;
}

// Return true if Val fits an LLIHF operand.
static inline bool isImmHF(uint64_t Val) {
  return (Val & ~0xffffffff00000000ULL) == 0;
}
} // end namespace SystemZ

FunctionPass *createSystemZISelDag(SystemZTargetMachine &TM,
                                   CodeGenOpt::Level OptLevel);
FunctionPass *createSystemZElimComparePass(SystemZTargetMachine &TM);
FunctionPass *createSystemZExpandPseudoPass(SystemZTargetMachine &TM);
FunctionPass *createSystemZShortenInstPass(SystemZTargetMachine &TM);
FunctionPass *createSystemZLongBranchPass(SystemZTargetMachine &TM);
FunctionPass *createSystemZLDCleanupPass(SystemZTargetMachine &TM);
FunctionPass *createSystemZTDCPass();
} // end namespace llvm

#endif
