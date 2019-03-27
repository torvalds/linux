//===- TargetLoweringBase.cpp - Implement the TargetLoweringBase class ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This implements the TargetLoweringBase class.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RuntimeLibcalls.h"
#include "llvm/CodeGen/StackMaps.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MachineValueType.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <tuple>
#include <utility>

using namespace llvm;

static cl::opt<bool> JumpIsExpensiveOverride(
    "jump-is-expensive", cl::init(false),
    cl::desc("Do not create extra branches to split comparison logic."),
    cl::Hidden);

static cl::opt<unsigned> MinimumJumpTableEntries
  ("min-jump-table-entries", cl::init(4), cl::Hidden,
   cl::desc("Set minimum number of entries to use a jump table."));

static cl::opt<unsigned> MaximumJumpTableSize
  ("max-jump-table-size", cl::init(0), cl::Hidden,
   cl::desc("Set maximum size of jump tables; zero for no limit."));

/// Minimum jump table density for normal functions.
static cl::opt<unsigned>
    JumpTableDensity("jump-table-density", cl::init(10), cl::Hidden,
                     cl::desc("Minimum density for building a jump table in "
                              "a normal function"));

/// Minimum jump table density for -Os or -Oz functions.
static cl::opt<unsigned> OptsizeJumpTableDensity(
    "optsize-jump-table-density", cl::init(40), cl::Hidden,
    cl::desc("Minimum density for building a jump table in "
             "an optsize function"));

static bool darwinHasSinCos(const Triple &TT) {
  assert(TT.isOSDarwin() && "should be called with darwin triple");
  // Don't bother with 32 bit x86.
  if (TT.getArch() == Triple::x86)
    return false;
  // Macos < 10.9 has no sincos_stret.
  if (TT.isMacOSX())
    return !TT.isMacOSXVersionLT(10, 9) && TT.isArch64Bit();
  // iOS < 7.0 has no sincos_stret.
  if (TT.isiOS())
    return !TT.isOSVersionLT(7, 0);
  // Any other darwin such as WatchOS/TvOS is new enough.
  return true;
}

// Although this default value is arbitrary, it is not random. It is assumed
// that a condition that evaluates the same way by a higher percentage than this
// is best represented as control flow. Therefore, the default value N should be
// set such that the win from N% correct executions is greater than the loss
// from (100 - N)% mispredicted executions for the majority of intended targets.
static cl::opt<int> MinPercentageForPredictableBranch(
    "min-predictable-branch", cl::init(99),
    cl::desc("Minimum percentage (0-100) that a condition must be either true "
             "or false to assume that the condition is predictable"),
    cl::Hidden);

void TargetLoweringBase::InitLibcalls(const Triple &TT) {
#define HANDLE_LIBCALL(code, name) \
  setLibcallName(RTLIB::code, name);
#include "llvm/IR/RuntimeLibcalls.def"
#undef HANDLE_LIBCALL
  // Initialize calling conventions to their default.
  for (int LC = 0; LC < RTLIB::UNKNOWN_LIBCALL; ++LC)
    setLibcallCallingConv((RTLIB::Libcall)LC, CallingConv::C);

  // A few names are different on particular architectures or environments.
  if (TT.isOSDarwin()) {
    // For f16/f32 conversions, Darwin uses the standard naming scheme, instead
    // of the gnueabi-style __gnu_*_ieee.
    // FIXME: What about other targets?
    setLibcallName(RTLIB::FPEXT_F16_F32, "__extendhfsf2");
    setLibcallName(RTLIB::FPROUND_F32_F16, "__truncsfhf2");

    // Some darwins have an optimized __bzero/bzero function.
    switch (TT.getArch()) {
    case Triple::x86:
    case Triple::x86_64:
      if (TT.isMacOSX() && !TT.isMacOSXVersionLT(10, 6))
        setLibcallName(RTLIB::BZERO, "__bzero");
      break;
    case Triple::aarch64:
      setLibcallName(RTLIB::BZERO, "bzero");
      break;
    default:
      break;
    }

    if (darwinHasSinCos(TT)) {
      setLibcallName(RTLIB::SINCOS_STRET_F32, "__sincosf_stret");
      setLibcallName(RTLIB::SINCOS_STRET_F64, "__sincos_stret");
      if (TT.isWatchABI()) {
        setLibcallCallingConv(RTLIB::SINCOS_STRET_F32,
                              CallingConv::ARM_AAPCS_VFP);
        setLibcallCallingConv(RTLIB::SINCOS_STRET_F64,
                              CallingConv::ARM_AAPCS_VFP);
      }
    }
  } else {
    setLibcallName(RTLIB::FPEXT_F16_F32, "__gnu_h2f_ieee");
    setLibcallName(RTLIB::FPROUND_F32_F16, "__gnu_f2h_ieee");
  }

  if (TT.isGNUEnvironment() || TT.isOSFuchsia() ||
      (TT.isAndroid() && !TT.isAndroidVersionLT(9))) {
    setLibcallName(RTLIB::SINCOS_F32, "sincosf");
    setLibcallName(RTLIB::SINCOS_F64, "sincos");
    setLibcallName(RTLIB::SINCOS_F80, "sincosl");
    setLibcallName(RTLIB::SINCOS_F128, "sincosl");
    setLibcallName(RTLIB::SINCOS_PPCF128, "sincosl");
  }

  if (TT.isOSOpenBSD()) {
    setLibcallName(RTLIB::STACKPROTECTOR_CHECK_FAIL, nullptr);
  }
}

/// getFPEXT - Return the FPEXT_*_* value for the given types, or
/// UNKNOWN_LIBCALL if there is none.
RTLIB::Libcall RTLIB::getFPEXT(EVT OpVT, EVT RetVT) {
  if (OpVT == MVT::f16) {
    if (RetVT == MVT::f32)
      return FPEXT_F16_F32;
  } else if (OpVT == MVT::f32) {
    if (RetVT == MVT::f64)
      return FPEXT_F32_F64;
    if (RetVT == MVT::f128)
      return FPEXT_F32_F128;
    if (RetVT == MVT::ppcf128)
      return FPEXT_F32_PPCF128;
  } else if (OpVT == MVT::f64) {
    if (RetVT == MVT::f128)
      return FPEXT_F64_F128;
    else if (RetVT == MVT::ppcf128)
      return FPEXT_F64_PPCF128;
  } else if (OpVT == MVT::f80) {
    if (RetVT == MVT::f128)
      return FPEXT_F80_F128;
  }

  return UNKNOWN_LIBCALL;
}

/// getFPROUND - Return the FPROUND_*_* value for the given types, or
/// UNKNOWN_LIBCALL if there is none.
RTLIB::Libcall RTLIB::getFPROUND(EVT OpVT, EVT RetVT) {
  if (RetVT == MVT::f16) {
    if (OpVT == MVT::f32)
      return FPROUND_F32_F16;
    if (OpVT == MVT::f64)
      return FPROUND_F64_F16;
    if (OpVT == MVT::f80)
      return FPROUND_F80_F16;
    if (OpVT == MVT::f128)
      return FPROUND_F128_F16;
    if (OpVT == MVT::ppcf128)
      return FPROUND_PPCF128_F16;
  } else if (RetVT == MVT::f32) {
    if (OpVT == MVT::f64)
      return FPROUND_F64_F32;
    if (OpVT == MVT::f80)
      return FPROUND_F80_F32;
    if (OpVT == MVT::f128)
      return FPROUND_F128_F32;
    if (OpVT == MVT::ppcf128)
      return FPROUND_PPCF128_F32;
  } else if (RetVT == MVT::f64) {
    if (OpVT == MVT::f80)
      return FPROUND_F80_F64;
    if (OpVT == MVT::f128)
      return FPROUND_F128_F64;
    if (OpVT == MVT::ppcf128)
      return FPROUND_PPCF128_F64;
  } else if (RetVT == MVT::f80) {
    if (OpVT == MVT::f128)
      return FPROUND_F128_F80;
  }

  return UNKNOWN_LIBCALL;
}

/// getFPTOSINT - Return the FPTOSINT_*_* value for the given types, or
/// UNKNOWN_LIBCALL if there is none.
RTLIB::Libcall RTLIB::getFPTOSINT(EVT OpVT, EVT RetVT) {
  if (OpVT == MVT::f32) {
    if (RetVT == MVT::i32)
      return FPTOSINT_F32_I32;
    if (RetVT == MVT::i64)
      return FPTOSINT_F32_I64;
    if (RetVT == MVT::i128)
      return FPTOSINT_F32_I128;
  } else if (OpVT == MVT::f64) {
    if (RetVT == MVT::i32)
      return FPTOSINT_F64_I32;
    if (RetVT == MVT::i64)
      return FPTOSINT_F64_I64;
    if (RetVT == MVT::i128)
      return FPTOSINT_F64_I128;
  } else if (OpVT == MVT::f80) {
    if (RetVT == MVT::i32)
      return FPTOSINT_F80_I32;
    if (RetVT == MVT::i64)
      return FPTOSINT_F80_I64;
    if (RetVT == MVT::i128)
      return FPTOSINT_F80_I128;
  } else if (OpVT == MVT::f128) {
    if (RetVT == MVT::i32)
      return FPTOSINT_F128_I32;
    if (RetVT == MVT::i64)
      return FPTOSINT_F128_I64;
    if (RetVT == MVT::i128)
      return FPTOSINT_F128_I128;
  } else if (OpVT == MVT::ppcf128) {
    if (RetVT == MVT::i32)
      return FPTOSINT_PPCF128_I32;
    if (RetVT == MVT::i64)
      return FPTOSINT_PPCF128_I64;
    if (RetVT == MVT::i128)
      return FPTOSINT_PPCF128_I128;
  }
  return UNKNOWN_LIBCALL;
}

/// getFPTOUINT - Return the FPTOUINT_*_* value for the given types, or
/// UNKNOWN_LIBCALL if there is none.
RTLIB::Libcall RTLIB::getFPTOUINT(EVT OpVT, EVT RetVT) {
  if (OpVT == MVT::f32) {
    if (RetVT == MVT::i32)
      return FPTOUINT_F32_I32;
    if (RetVT == MVT::i64)
      return FPTOUINT_F32_I64;
    if (RetVT == MVT::i128)
      return FPTOUINT_F32_I128;
  } else if (OpVT == MVT::f64) {
    if (RetVT == MVT::i32)
      return FPTOUINT_F64_I32;
    if (RetVT == MVT::i64)
      return FPTOUINT_F64_I64;
    if (RetVT == MVT::i128)
      return FPTOUINT_F64_I128;
  } else if (OpVT == MVT::f80) {
    if (RetVT == MVT::i32)
      return FPTOUINT_F80_I32;
    if (RetVT == MVT::i64)
      return FPTOUINT_F80_I64;
    if (RetVT == MVT::i128)
      return FPTOUINT_F80_I128;
  } else if (OpVT == MVT::f128) {
    if (RetVT == MVT::i32)
      return FPTOUINT_F128_I32;
    if (RetVT == MVT::i64)
      return FPTOUINT_F128_I64;
    if (RetVT == MVT::i128)
      return FPTOUINT_F128_I128;
  } else if (OpVT == MVT::ppcf128) {
    if (RetVT == MVT::i32)
      return FPTOUINT_PPCF128_I32;
    if (RetVT == MVT::i64)
      return FPTOUINT_PPCF128_I64;
    if (RetVT == MVT::i128)
      return FPTOUINT_PPCF128_I128;
  }
  return UNKNOWN_LIBCALL;
}

/// getSINTTOFP - Return the SINTTOFP_*_* value for the given types, or
/// UNKNOWN_LIBCALL if there is none.
RTLIB::Libcall RTLIB::getSINTTOFP(EVT OpVT, EVT RetVT) {
  if (OpVT == MVT::i32) {
    if (RetVT == MVT::f32)
      return SINTTOFP_I32_F32;
    if (RetVT == MVT::f64)
      return SINTTOFP_I32_F64;
    if (RetVT == MVT::f80)
      return SINTTOFP_I32_F80;
    if (RetVT == MVT::f128)
      return SINTTOFP_I32_F128;
    if (RetVT == MVT::ppcf128)
      return SINTTOFP_I32_PPCF128;
  } else if (OpVT == MVT::i64) {
    if (RetVT == MVT::f32)
      return SINTTOFP_I64_F32;
    if (RetVT == MVT::f64)
      return SINTTOFP_I64_F64;
    if (RetVT == MVT::f80)
      return SINTTOFP_I64_F80;
    if (RetVT == MVT::f128)
      return SINTTOFP_I64_F128;
    if (RetVT == MVT::ppcf128)
      return SINTTOFP_I64_PPCF128;
  } else if (OpVT == MVT::i128) {
    if (RetVT == MVT::f32)
      return SINTTOFP_I128_F32;
    if (RetVT == MVT::f64)
      return SINTTOFP_I128_F64;
    if (RetVT == MVT::f80)
      return SINTTOFP_I128_F80;
    if (RetVT == MVT::f128)
      return SINTTOFP_I128_F128;
    if (RetVT == MVT::ppcf128)
      return SINTTOFP_I128_PPCF128;
  }
  return UNKNOWN_LIBCALL;
}

/// getUINTTOFP - Return the UINTTOFP_*_* value for the given types, or
/// UNKNOWN_LIBCALL if there is none.
RTLIB::Libcall RTLIB::getUINTTOFP(EVT OpVT, EVT RetVT) {
  if (OpVT == MVT::i32) {
    if (RetVT == MVT::f32)
      return UINTTOFP_I32_F32;
    if (RetVT == MVT::f64)
      return UINTTOFP_I32_F64;
    if (RetVT == MVT::f80)
      return UINTTOFP_I32_F80;
    if (RetVT == MVT::f128)
      return UINTTOFP_I32_F128;
    if (RetVT == MVT::ppcf128)
      return UINTTOFP_I32_PPCF128;
  } else if (OpVT == MVT::i64) {
    if (RetVT == MVT::f32)
      return UINTTOFP_I64_F32;
    if (RetVT == MVT::f64)
      return UINTTOFP_I64_F64;
    if (RetVT == MVT::f80)
      return UINTTOFP_I64_F80;
    if (RetVT == MVT::f128)
      return UINTTOFP_I64_F128;
    if (RetVT == MVT::ppcf128)
      return UINTTOFP_I64_PPCF128;
  } else if (OpVT == MVT::i128) {
    if (RetVT == MVT::f32)
      return UINTTOFP_I128_F32;
    if (RetVT == MVT::f64)
      return UINTTOFP_I128_F64;
    if (RetVT == MVT::f80)
      return UINTTOFP_I128_F80;
    if (RetVT == MVT::f128)
      return UINTTOFP_I128_F128;
    if (RetVT == MVT::ppcf128)
      return UINTTOFP_I128_PPCF128;
  }
  return UNKNOWN_LIBCALL;
}

RTLIB::Libcall RTLIB::getSYNC(unsigned Opc, MVT VT) {
#define OP_TO_LIBCALL(Name, Enum)                                              \
  case Name:                                                                   \
    switch (VT.SimpleTy) {                                                     \
    default:                                                                   \
      return UNKNOWN_LIBCALL;                                                  \
    case MVT::i8:                                                              \
      return Enum##_1;                                                         \
    case MVT::i16:                                                             \
      return Enum##_2;                                                         \
    case MVT::i32:                                                             \
      return Enum##_4;                                                         \
    case MVT::i64:                                                             \
      return Enum##_8;                                                         \
    case MVT::i128:                                                            \
      return Enum##_16;                                                        \
    }

  switch (Opc) {
    OP_TO_LIBCALL(ISD::ATOMIC_SWAP, SYNC_LOCK_TEST_AND_SET)
    OP_TO_LIBCALL(ISD::ATOMIC_CMP_SWAP, SYNC_VAL_COMPARE_AND_SWAP)
    OP_TO_LIBCALL(ISD::ATOMIC_LOAD_ADD, SYNC_FETCH_AND_ADD)
    OP_TO_LIBCALL(ISD::ATOMIC_LOAD_SUB, SYNC_FETCH_AND_SUB)
    OP_TO_LIBCALL(ISD::ATOMIC_LOAD_AND, SYNC_FETCH_AND_AND)
    OP_TO_LIBCALL(ISD::ATOMIC_LOAD_OR, SYNC_FETCH_AND_OR)
    OP_TO_LIBCALL(ISD::ATOMIC_LOAD_XOR, SYNC_FETCH_AND_XOR)
    OP_TO_LIBCALL(ISD::ATOMIC_LOAD_NAND, SYNC_FETCH_AND_NAND)
    OP_TO_LIBCALL(ISD::ATOMIC_LOAD_MAX, SYNC_FETCH_AND_MAX)
    OP_TO_LIBCALL(ISD::ATOMIC_LOAD_UMAX, SYNC_FETCH_AND_UMAX)
    OP_TO_LIBCALL(ISD::ATOMIC_LOAD_MIN, SYNC_FETCH_AND_MIN)
    OP_TO_LIBCALL(ISD::ATOMIC_LOAD_UMIN, SYNC_FETCH_AND_UMIN)
  }

#undef OP_TO_LIBCALL

  return UNKNOWN_LIBCALL;
}

RTLIB::Libcall RTLIB::getMEMCPY_ELEMENT_UNORDERED_ATOMIC(uint64_t ElementSize) {
  switch (ElementSize) {
  case 1:
    return MEMCPY_ELEMENT_UNORDERED_ATOMIC_1;
  case 2:
    return MEMCPY_ELEMENT_UNORDERED_ATOMIC_2;
  case 4:
    return MEMCPY_ELEMENT_UNORDERED_ATOMIC_4;
  case 8:
    return MEMCPY_ELEMENT_UNORDERED_ATOMIC_8;
  case 16:
    return MEMCPY_ELEMENT_UNORDERED_ATOMIC_16;
  default:
    return UNKNOWN_LIBCALL;
  }
}

RTLIB::Libcall RTLIB::getMEMMOVE_ELEMENT_UNORDERED_ATOMIC(uint64_t ElementSize) {
  switch (ElementSize) {
  case 1:
    return MEMMOVE_ELEMENT_UNORDERED_ATOMIC_1;
  case 2:
    return MEMMOVE_ELEMENT_UNORDERED_ATOMIC_2;
  case 4:
    return MEMMOVE_ELEMENT_UNORDERED_ATOMIC_4;
  case 8:
    return MEMMOVE_ELEMENT_UNORDERED_ATOMIC_8;
  case 16:
    return MEMMOVE_ELEMENT_UNORDERED_ATOMIC_16;
  default:
    return UNKNOWN_LIBCALL;
  }
}

RTLIB::Libcall RTLIB::getMEMSET_ELEMENT_UNORDERED_ATOMIC(uint64_t ElementSize) {
  switch (ElementSize) {
  case 1:
    return MEMSET_ELEMENT_UNORDERED_ATOMIC_1;
  case 2:
    return MEMSET_ELEMENT_UNORDERED_ATOMIC_2;
  case 4:
    return MEMSET_ELEMENT_UNORDERED_ATOMIC_4;
  case 8:
    return MEMSET_ELEMENT_UNORDERED_ATOMIC_8;
  case 16:
    return MEMSET_ELEMENT_UNORDERED_ATOMIC_16;
  default:
    return UNKNOWN_LIBCALL;
  }
}

/// InitCmpLibcallCCs - Set default comparison libcall CC.
static void InitCmpLibcallCCs(ISD::CondCode *CCs) {
  memset(CCs, ISD::SETCC_INVALID, sizeof(ISD::CondCode)*RTLIB::UNKNOWN_LIBCALL);
  CCs[RTLIB::OEQ_F32] = ISD::SETEQ;
  CCs[RTLIB::OEQ_F64] = ISD::SETEQ;
  CCs[RTLIB::OEQ_F128] = ISD::SETEQ;
  CCs[RTLIB::OEQ_PPCF128] = ISD::SETEQ;
  CCs[RTLIB::UNE_F32] = ISD::SETNE;
  CCs[RTLIB::UNE_F64] = ISD::SETNE;
  CCs[RTLIB::UNE_F128] = ISD::SETNE;
  CCs[RTLIB::UNE_PPCF128] = ISD::SETNE;
  CCs[RTLIB::OGE_F32] = ISD::SETGE;
  CCs[RTLIB::OGE_F64] = ISD::SETGE;
  CCs[RTLIB::OGE_F128] = ISD::SETGE;
  CCs[RTLIB::OGE_PPCF128] = ISD::SETGE;
  CCs[RTLIB::OLT_F32] = ISD::SETLT;
  CCs[RTLIB::OLT_F64] = ISD::SETLT;
  CCs[RTLIB::OLT_F128] = ISD::SETLT;
  CCs[RTLIB::OLT_PPCF128] = ISD::SETLT;
  CCs[RTLIB::OLE_F32] = ISD::SETLE;
  CCs[RTLIB::OLE_F64] = ISD::SETLE;
  CCs[RTLIB::OLE_F128] = ISD::SETLE;
  CCs[RTLIB::OLE_PPCF128] = ISD::SETLE;
  CCs[RTLIB::OGT_F32] = ISD::SETGT;
  CCs[RTLIB::OGT_F64] = ISD::SETGT;
  CCs[RTLIB::OGT_F128] = ISD::SETGT;
  CCs[RTLIB::OGT_PPCF128] = ISD::SETGT;
  CCs[RTLIB::UO_F32] = ISD::SETNE;
  CCs[RTLIB::UO_F64] = ISD::SETNE;
  CCs[RTLIB::UO_F128] = ISD::SETNE;
  CCs[RTLIB::UO_PPCF128] = ISD::SETNE;
  CCs[RTLIB::O_F32] = ISD::SETEQ;
  CCs[RTLIB::O_F64] = ISD::SETEQ;
  CCs[RTLIB::O_F128] = ISD::SETEQ;
  CCs[RTLIB::O_PPCF128] = ISD::SETEQ;
}

/// NOTE: The TargetMachine owns TLOF.
TargetLoweringBase::TargetLoweringBase(const TargetMachine &tm) : TM(tm) {
  initActions();

  // Perform these initializations only once.
  MaxStoresPerMemset = MaxStoresPerMemcpy = MaxStoresPerMemmove =
      MaxLoadsPerMemcmp = 8;
  MaxGluedStoresPerMemcpy = 0;
  MaxStoresPerMemsetOptSize = MaxStoresPerMemcpyOptSize =
      MaxStoresPerMemmoveOptSize = MaxLoadsPerMemcmpOptSize = 4;
  UseUnderscoreSetJmp = false;
  UseUnderscoreLongJmp = false;
  HasMultipleConditionRegisters = false;
  HasExtractBitsInsn = false;
  JumpIsExpensive = JumpIsExpensiveOverride;
  PredictableSelectIsExpensive = false;
  EnableExtLdPromotion = false;
  HasFloatingPointExceptions = true;
  StackPointerRegisterToSaveRestore = 0;
  BooleanContents = UndefinedBooleanContent;
  BooleanFloatContents = UndefinedBooleanContent;
  BooleanVectorContents = UndefinedBooleanContent;
  SchedPreferenceInfo = Sched::ILP;
  JumpBufSize = 0;
  JumpBufAlignment = 0;
  MinFunctionAlignment = 0;
  PrefFunctionAlignment = 0;
  PrefLoopAlignment = 0;
  GatherAllAliasesMaxDepth = 18;
  MinStackArgumentAlignment = 1;
  // TODO: the default will be switched to 0 in the next commit, along
  // with the Target-specific changes necessary.
  MaxAtomicSizeInBitsSupported = 1024;

  MinCmpXchgSizeInBits = 0;
  SupportsUnalignedAtomics = false;

  std::fill(std::begin(LibcallRoutineNames), std::end(LibcallRoutineNames), nullptr);

  InitLibcalls(TM.getTargetTriple());
  InitCmpLibcallCCs(CmpLibcallCCs);
}

void TargetLoweringBase::initActions() {
  // All operations default to being supported.
  memset(OpActions, 0, sizeof(OpActions));
  memset(LoadExtActions, 0, sizeof(LoadExtActions));
  memset(TruncStoreActions, 0, sizeof(TruncStoreActions));
  memset(IndexedModeActions, 0, sizeof(IndexedModeActions));
  memset(CondCodeActions, 0, sizeof(CondCodeActions));
  std::fill(std::begin(RegClassForVT), std::end(RegClassForVT), nullptr);
  std::fill(std::begin(TargetDAGCombineArray),
            std::end(TargetDAGCombineArray), 0);

  // Set default actions for various operations.
  for (MVT VT : MVT::all_valuetypes()) {
    // Default all indexed load / store to expand.
    for (unsigned IM = (unsigned)ISD::PRE_INC;
         IM != (unsigned)ISD::LAST_INDEXED_MODE; ++IM) {
      setIndexedLoadAction(IM, VT, Expand);
      setIndexedStoreAction(IM, VT, Expand);
    }

    // Most backends expect to see the node which just returns the value loaded.
    setOperationAction(ISD::ATOMIC_CMP_SWAP_WITH_SUCCESS, VT, Expand);

    // These operations default to expand.
    setOperationAction(ISD::FGETSIGN, VT, Expand);
    setOperationAction(ISD::CONCAT_VECTORS, VT, Expand);
    setOperationAction(ISD::FMINNUM, VT, Expand);
    setOperationAction(ISD::FMAXNUM, VT, Expand);
    setOperationAction(ISD::FMINNUM_IEEE, VT, Expand);
    setOperationAction(ISD::FMAXNUM_IEEE, VT, Expand);
    setOperationAction(ISD::FMINIMUM, VT, Expand);
    setOperationAction(ISD::FMAXIMUM, VT, Expand);
    setOperationAction(ISD::FMAD, VT, Expand);
    setOperationAction(ISD::SMIN, VT, Expand);
    setOperationAction(ISD::SMAX, VT, Expand);
    setOperationAction(ISD::UMIN, VT, Expand);
    setOperationAction(ISD::UMAX, VT, Expand);
    setOperationAction(ISD::ABS, VT, Expand);
    setOperationAction(ISD::FSHL, VT, Expand);
    setOperationAction(ISD::FSHR, VT, Expand);
    setOperationAction(ISD::SADDSAT, VT, Expand);
    setOperationAction(ISD::UADDSAT, VT, Expand);
    setOperationAction(ISD::SSUBSAT, VT, Expand);
    setOperationAction(ISD::USUBSAT, VT, Expand);
    setOperationAction(ISD::SMULFIX, VT, Expand);

    // Overflow operations default to expand
    setOperationAction(ISD::SADDO, VT, Expand);
    setOperationAction(ISD::SSUBO, VT, Expand);
    setOperationAction(ISD::UADDO, VT, Expand);
    setOperationAction(ISD::USUBO, VT, Expand);
    setOperationAction(ISD::SMULO, VT, Expand);
    setOperationAction(ISD::UMULO, VT, Expand);

    // ADDCARRY operations default to expand
    setOperationAction(ISD::ADDCARRY, VT, Expand);
    setOperationAction(ISD::SUBCARRY, VT, Expand);
    setOperationAction(ISD::SETCCCARRY, VT, Expand);

    // ADDC/ADDE/SUBC/SUBE default to expand.
    setOperationAction(ISD::ADDC, VT, Expand);
    setOperationAction(ISD::ADDE, VT, Expand);
    setOperationAction(ISD::SUBC, VT, Expand);
    setOperationAction(ISD::SUBE, VT, Expand);

    // These default to Expand so they will be expanded to CTLZ/CTTZ by default.
    setOperationAction(ISD::CTLZ_ZERO_UNDEF, VT, Expand);
    setOperationAction(ISD::CTTZ_ZERO_UNDEF, VT, Expand);

    setOperationAction(ISD::BITREVERSE, VT, Expand);

    // These library functions default to expand.
    setOperationAction(ISD::FROUND, VT, Expand);
    setOperationAction(ISD::FPOWI, VT, Expand);

    // These operations default to expand for vector types.
    if (VT.isVector()) {
      setOperationAction(ISD::FCOPYSIGN, VT, Expand);
      setOperationAction(ISD::ANY_EXTEND_VECTOR_INREG, VT, Expand);
      setOperationAction(ISD::SIGN_EXTEND_VECTOR_INREG, VT, Expand);
      setOperationAction(ISD::ZERO_EXTEND_VECTOR_INREG, VT, Expand);
    }

    // For most targets @llvm.get.dynamic.area.offset just returns 0.
    setOperationAction(ISD::GET_DYNAMIC_AREA_OFFSET, VT, Expand);
  }

  // Most targets ignore the @llvm.prefetch intrinsic.
  setOperationAction(ISD::PREFETCH, MVT::Other, Expand);

  // Most targets also ignore the @llvm.readcyclecounter intrinsic.
  setOperationAction(ISD::READCYCLECOUNTER, MVT::i64, Expand);

  // ConstantFP nodes default to expand.  Targets can either change this to
  // Legal, in which case all fp constants are legal, or use isFPImmLegal()
  // to optimize expansions for certain constants.
  setOperationAction(ISD::ConstantFP, MVT::f16, Expand);
  setOperationAction(ISD::ConstantFP, MVT::f32, Expand);
  setOperationAction(ISD::ConstantFP, MVT::f64, Expand);
  setOperationAction(ISD::ConstantFP, MVT::f80, Expand);
  setOperationAction(ISD::ConstantFP, MVT::f128, Expand);

  // These library functions default to expand.
  for (MVT VT : {MVT::f32, MVT::f64, MVT::f128}) {
    setOperationAction(ISD::FCBRT,      VT, Expand);
    setOperationAction(ISD::FLOG ,      VT, Expand);
    setOperationAction(ISD::FLOG2,      VT, Expand);
    setOperationAction(ISD::FLOG10,     VT, Expand);
    setOperationAction(ISD::FEXP ,      VT, Expand);
    setOperationAction(ISD::FEXP2,      VT, Expand);
    setOperationAction(ISD::FFLOOR,     VT, Expand);
    setOperationAction(ISD::FNEARBYINT, VT, Expand);
    setOperationAction(ISD::FCEIL,      VT, Expand);
    setOperationAction(ISD::FRINT,      VT, Expand);
    setOperationAction(ISD::FTRUNC,     VT, Expand);
    setOperationAction(ISD::FROUND,     VT, Expand);
  }

  // Default ISD::TRAP to expand (which turns it into abort).
  setOperationAction(ISD::TRAP, MVT::Other, Expand);

  // On most systems, DEBUGTRAP and TRAP have no difference. The "Expand"
  // here is to inform DAG Legalizer to replace DEBUGTRAP with TRAP.
  setOperationAction(ISD::DEBUGTRAP, MVT::Other, Expand);
}

MVT TargetLoweringBase::getScalarShiftAmountTy(const DataLayout &DL,
                                               EVT) const {
  return MVT::getIntegerVT(8 * DL.getPointerSize(0));
}

EVT TargetLoweringBase::getShiftAmountTy(EVT LHSTy, const DataLayout &DL,
                                         bool LegalTypes) const {
  assert(LHSTy.isInteger() && "Shift amount is not an integer type!");
  if (LHSTy.isVector())
    return LHSTy;
  return LegalTypes ? getScalarShiftAmountTy(DL, LHSTy)
                    : getPointerTy(DL);
}

bool TargetLoweringBase::canOpTrap(unsigned Op, EVT VT) const {
  assert(isTypeLegal(VT));
  switch (Op) {
  default:
    return false;
  case ISD::SDIV:
  case ISD::UDIV:
  case ISD::SREM:
  case ISD::UREM:
    return true;
  }
}

void TargetLoweringBase::setJumpIsExpensive(bool isExpensive) {
  // If the command-line option was specified, ignore this request.
  if (!JumpIsExpensiveOverride.getNumOccurrences())
    JumpIsExpensive = isExpensive;
}

TargetLoweringBase::LegalizeKind
TargetLoweringBase::getTypeConversion(LLVMContext &Context, EVT VT) const {
  // If this is a simple type, use the ComputeRegisterProp mechanism.
  if (VT.isSimple()) {
    MVT SVT = VT.getSimpleVT();
    assert((unsigned)SVT.SimpleTy < array_lengthof(TransformToType));
    MVT NVT = TransformToType[SVT.SimpleTy];
    LegalizeTypeAction LA = ValueTypeActions.getTypeAction(SVT);

    assert((LA == TypeLegal || LA == TypeSoftenFloat ||
            ValueTypeActions.getTypeAction(NVT) != TypePromoteInteger) &&
           "Promote may not follow Expand or Promote");

    if (LA == TypeSplitVector)
      return LegalizeKind(LA,
                          EVT::getVectorVT(Context, SVT.getVectorElementType(),
                                           SVT.getVectorNumElements() / 2));
    if (LA == TypeScalarizeVector)
      return LegalizeKind(LA, SVT.getVectorElementType());
    return LegalizeKind(LA, NVT);
  }

  // Handle Extended Scalar Types.
  if (!VT.isVector()) {
    assert(VT.isInteger() && "Float types must be simple");
    unsigned BitSize = VT.getSizeInBits();
    // First promote to a power-of-two size, then expand if necessary.
    if (BitSize < 8 || !isPowerOf2_32(BitSize)) {
      EVT NVT = VT.getRoundIntegerType(Context);
      assert(NVT != VT && "Unable to round integer VT");
      LegalizeKind NextStep = getTypeConversion(Context, NVT);
      // Avoid multi-step promotion.
      if (NextStep.first == TypePromoteInteger)
        return NextStep;
      // Return rounded integer type.
      return LegalizeKind(TypePromoteInteger, NVT);
    }

    return LegalizeKind(TypeExpandInteger,
                        EVT::getIntegerVT(Context, VT.getSizeInBits() / 2));
  }

  // Handle vector types.
  unsigned NumElts = VT.getVectorNumElements();
  EVT EltVT = VT.getVectorElementType();

  // Vectors with only one element are always scalarized.
  if (NumElts == 1)
    return LegalizeKind(TypeScalarizeVector, EltVT);

  // Try to widen vector elements until the element type is a power of two and
  // promote it to a legal type later on, for example:
  // <3 x i8> -> <4 x i8> -> <4 x i32>
  if (EltVT.isInteger()) {
    // Vectors with a number of elements that is not a power of two are always
    // widened, for example <3 x i8> -> <4 x i8>.
    if (!VT.isPow2VectorType()) {
      NumElts = (unsigned)NextPowerOf2(NumElts);
      EVT NVT = EVT::getVectorVT(Context, EltVT, NumElts);
      return LegalizeKind(TypeWidenVector, NVT);
    }

    // Examine the element type.
    LegalizeKind LK = getTypeConversion(Context, EltVT);

    // If type is to be expanded, split the vector.
    //  <4 x i140> -> <2 x i140>
    if (LK.first == TypeExpandInteger)
      return LegalizeKind(TypeSplitVector,
                          EVT::getVectorVT(Context, EltVT, NumElts / 2));

    // Promote the integer element types until a legal vector type is found
    // or until the element integer type is too big. If a legal type was not
    // found, fallback to the usual mechanism of widening/splitting the
    // vector.
    EVT OldEltVT = EltVT;
    while (true) {
      // Increase the bitwidth of the element to the next pow-of-two
      // (which is greater than 8 bits).
      EltVT = EVT::getIntegerVT(Context, 1 + EltVT.getSizeInBits())
                  .getRoundIntegerType(Context);

      // Stop trying when getting a non-simple element type.
      // Note that vector elements may be greater than legal vector element
      // types. Example: X86 XMM registers hold 64bit element on 32bit
      // systems.
      if (!EltVT.isSimple())
        break;

      // Build a new vector type and check if it is legal.
      MVT NVT = MVT::getVectorVT(EltVT.getSimpleVT(), NumElts);
      // Found a legal promoted vector type.
      if (NVT != MVT() && ValueTypeActions.getTypeAction(NVT) == TypeLegal)
        return LegalizeKind(TypePromoteInteger,
                            EVT::getVectorVT(Context, EltVT, NumElts));
    }

    // Reset the type to the unexpanded type if we did not find a legal vector
    // type with a promoted vector element type.
    EltVT = OldEltVT;
  }

  // Try to widen the vector until a legal type is found.
  // If there is no wider legal type, split the vector.
  while (true) {
    // Round up to the next power of 2.
    NumElts = (unsigned)NextPowerOf2(NumElts);

    // If there is no simple vector type with this many elements then there
    // cannot be a larger legal vector type.  Note that this assumes that
    // there are no skipped intermediate vector types in the simple types.
    if (!EltVT.isSimple())
      break;
    MVT LargerVector = MVT::getVectorVT(EltVT.getSimpleVT(), NumElts);
    if (LargerVector == MVT())
      break;

    // If this type is legal then widen the vector.
    if (ValueTypeActions.getTypeAction(LargerVector) == TypeLegal)
      return LegalizeKind(TypeWidenVector, LargerVector);
  }

  // Widen odd vectors to next power of two.
  if (!VT.isPow2VectorType()) {
    EVT NVT = VT.getPow2VectorType(Context);
    return LegalizeKind(TypeWidenVector, NVT);
  }

  // Vectors with illegal element types are expanded.
  EVT NVT = EVT::getVectorVT(Context, EltVT, VT.getVectorNumElements() / 2);
  return LegalizeKind(TypeSplitVector, NVT);
}

static unsigned getVectorTypeBreakdownMVT(MVT VT, MVT &IntermediateVT,
                                          unsigned &NumIntermediates,
                                          MVT &RegisterVT,
                                          TargetLoweringBase *TLI) {
  // Figure out the right, legal destination reg to copy into.
  unsigned NumElts = VT.getVectorNumElements();
  MVT EltTy = VT.getVectorElementType();

  unsigned NumVectorRegs = 1;

  // FIXME: We don't support non-power-of-2-sized vectors for now.  Ideally we
  // could break down into LHS/RHS like LegalizeDAG does.
  if (!isPowerOf2_32(NumElts)) {
    NumVectorRegs = NumElts;
    NumElts = 1;
  }

  // Divide the input until we get to a supported size.  This will always
  // end with a scalar if the target doesn't support vectors.
  while (NumElts > 1 && !TLI->isTypeLegal(MVT::getVectorVT(EltTy, NumElts))) {
    NumElts >>= 1;
    NumVectorRegs <<= 1;
  }

  NumIntermediates = NumVectorRegs;

  MVT NewVT = MVT::getVectorVT(EltTy, NumElts);
  if (!TLI->isTypeLegal(NewVT))
    NewVT = EltTy;
  IntermediateVT = NewVT;

  unsigned NewVTSize = NewVT.getSizeInBits();

  // Convert sizes such as i33 to i64.
  if (!isPowerOf2_32(NewVTSize))
    NewVTSize = NextPowerOf2(NewVTSize);

  MVT DestVT = TLI->getRegisterType(NewVT);
  RegisterVT = DestVT;
  if (EVT(DestVT).bitsLT(NewVT))    // Value is expanded, e.g. i64 -> i16.
    return NumVectorRegs*(NewVTSize/DestVT.getSizeInBits());

  // Otherwise, promotion or legal types use the same number of registers as
  // the vector decimated to the appropriate level.
  return NumVectorRegs;
}

/// isLegalRC - Return true if the value types that can be represented by the
/// specified register class are all legal.
bool TargetLoweringBase::isLegalRC(const TargetRegisterInfo &TRI,
                                   const TargetRegisterClass &RC) const {
  for (auto I = TRI.legalclasstypes_begin(RC); *I != MVT::Other; ++I)
    if (isTypeLegal(*I))
      return true;
  return false;
}

/// Replace/modify any TargetFrameIndex operands with a targte-dependent
/// sequence of memory operands that is recognized by PrologEpilogInserter.
MachineBasicBlock *
TargetLoweringBase::emitPatchPoint(MachineInstr &InitialMI,
                                   MachineBasicBlock *MBB) const {
  MachineInstr *MI = &InitialMI;
  MachineFunction &MF = *MI->getMF();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  // We're handling multiple types of operands here:
  // PATCHPOINT MetaArgs - live-in, read only, direct
  // STATEPOINT Deopt Spill - live-through, read only, indirect
  // STATEPOINT Deopt Alloca - live-through, read only, direct
  // (We're currently conservative and mark the deopt slots read/write in
  // practice.)
  // STATEPOINT GC Spill - live-through, read/write, indirect
  // STATEPOINT GC Alloca - live-through, read/write, direct
  // The live-in vs live-through is handled already (the live through ones are
  // all stack slots), but we need to handle the different type of stackmap
  // operands and memory effects here.

  // MI changes inside this loop as we grow operands.
  for(unsigned OperIdx = 0; OperIdx != MI->getNumOperands(); ++OperIdx) {
    MachineOperand &MO = MI->getOperand(OperIdx);
    if (!MO.isFI())
      continue;

    // foldMemoryOperand builds a new MI after replacing a single FI operand
    // with the canonical set of five x86 addressing-mode operands.
    int FI = MO.getIndex();
    MachineInstrBuilder MIB = BuildMI(MF, MI->getDebugLoc(), MI->getDesc());

    // Copy operands before the frame-index.
    for (unsigned i = 0; i < OperIdx; ++i)
      MIB.add(MI->getOperand(i));
    // Add frame index operands recognized by stackmaps.cpp
    if (MFI.isStatepointSpillSlotObjectIndex(FI)) {
      // indirect-mem-ref tag, size, #FI, offset.
      // Used for spills inserted by StatepointLowering.  This codepath is not
      // used for patchpoints/stackmaps at all, for these spilling is done via
      // foldMemoryOperand callback only.
      assert(MI->getOpcode() == TargetOpcode::STATEPOINT && "sanity");
      MIB.addImm(StackMaps::IndirectMemRefOp);
      MIB.addImm(MFI.getObjectSize(FI));
      MIB.add(MI->getOperand(OperIdx));
      MIB.addImm(0);
    } else {
      // direct-mem-ref tag, #FI, offset.
      // Used by patchpoint, and direct alloca arguments to statepoints
      MIB.addImm(StackMaps::DirectMemRefOp);
      MIB.add(MI->getOperand(OperIdx));
      MIB.addImm(0);
    }
    // Copy the operands after the frame index.
    for (unsigned i = OperIdx + 1; i != MI->getNumOperands(); ++i)
      MIB.add(MI->getOperand(i));

    // Inherit previous memory operands.
    MIB.cloneMemRefs(*MI);
    assert(MIB->mayLoad() && "Folded a stackmap use to a non-load!");

    // Add a new memory operand for this FI.
    assert(MFI.getObjectOffset(FI) != -1);

    auto Flags = MachineMemOperand::MOLoad;
    if (MI->getOpcode() == TargetOpcode::STATEPOINT) {
      Flags |= MachineMemOperand::MOStore;
      Flags |= MachineMemOperand::MOVolatile;
    }
    MachineMemOperand *MMO = MF.getMachineMemOperand(
        MachinePointerInfo::getFixedStack(MF, FI), Flags,
        MF.getDataLayout().getPointerSize(), MFI.getObjectAlignment(FI));
    MIB->addMemOperand(MF, MMO);

    // Replace the instruction and update the operand index.
    MBB->insert(MachineBasicBlock::iterator(MI), MIB);
    OperIdx += (MIB->getNumOperands() - MI->getNumOperands()) - 1;
    MI->eraseFromParent();
    MI = MIB;
  }
  return MBB;
}

MachineBasicBlock *
TargetLoweringBase::emitXRayCustomEvent(MachineInstr &MI,
                                        MachineBasicBlock *MBB) const {
  assert(MI.getOpcode() == TargetOpcode::PATCHABLE_EVENT_CALL &&
         "Called emitXRayCustomEvent on the wrong MI!");
  auto &MF = *MI.getMF();
  auto MIB = BuildMI(MF, MI.getDebugLoc(), MI.getDesc());
  for (unsigned OpIdx = 0; OpIdx != MI.getNumOperands(); ++OpIdx)
    MIB.add(MI.getOperand(OpIdx));

  MBB->insert(MachineBasicBlock::iterator(MI), MIB);
  MI.eraseFromParent();
  return MBB;
}

MachineBasicBlock *
TargetLoweringBase::emitXRayTypedEvent(MachineInstr &MI,
                                       MachineBasicBlock *MBB) const {
  assert(MI.getOpcode() == TargetOpcode::PATCHABLE_TYPED_EVENT_CALL &&
         "Called emitXRayTypedEvent on the wrong MI!");
  auto &MF = *MI.getMF();
  auto MIB = BuildMI(MF, MI.getDebugLoc(), MI.getDesc());
  for (unsigned OpIdx = 0; OpIdx != MI.getNumOperands(); ++OpIdx)
    MIB.add(MI.getOperand(OpIdx));

  MBB->insert(MachineBasicBlock::iterator(MI), MIB);
  MI.eraseFromParent();
  return MBB;
}

/// findRepresentativeClass - Return the largest legal super-reg register class
/// of the register class for the specified type and its associated "cost".
// This function is in TargetLowering because it uses RegClassForVT which would
// need to be moved to TargetRegisterInfo and would necessitate moving
// isTypeLegal over as well - a massive change that would just require
// TargetLowering having a TargetRegisterInfo class member that it would use.
std::pair<const TargetRegisterClass *, uint8_t>
TargetLoweringBase::findRepresentativeClass(const TargetRegisterInfo *TRI,
                                            MVT VT) const {
  const TargetRegisterClass *RC = RegClassForVT[VT.SimpleTy];
  if (!RC)
    return std::make_pair(RC, 0);

  // Compute the set of all super-register classes.
  BitVector SuperRegRC(TRI->getNumRegClasses());
  for (SuperRegClassIterator RCI(RC, TRI); RCI.isValid(); ++RCI)
    SuperRegRC.setBitsInMask(RCI.getMask());

  // Find the first legal register class with the largest spill size.
  const TargetRegisterClass *BestRC = RC;
  for (unsigned i : SuperRegRC.set_bits()) {
    const TargetRegisterClass *SuperRC = TRI->getRegClass(i);
    // We want the largest possible spill size.
    if (TRI->getSpillSize(*SuperRC) <= TRI->getSpillSize(*BestRC))
      continue;
    if (!isLegalRC(*TRI, *SuperRC))
      continue;
    BestRC = SuperRC;
  }
  return std::make_pair(BestRC, 1);
}

/// computeRegisterProperties - Once all of the register classes are added,
/// this allows us to compute derived properties we expose.
void TargetLoweringBase::computeRegisterProperties(
    const TargetRegisterInfo *TRI) {
  static_assert(MVT::LAST_VALUETYPE <= MVT::MAX_ALLOWED_VALUETYPE,
                "Too many value types for ValueTypeActions to hold!");

  // Everything defaults to needing one register.
  for (unsigned i = 0; i != MVT::LAST_VALUETYPE; ++i) {
    NumRegistersForVT[i] = 1;
    RegisterTypeForVT[i] = TransformToType[i] = (MVT::SimpleValueType)i;
  }
  // ...except isVoid, which doesn't need any registers.
  NumRegistersForVT[MVT::isVoid] = 0;

  // Find the largest integer register class.
  unsigned LargestIntReg = MVT::LAST_INTEGER_VALUETYPE;
  for (; RegClassForVT[LargestIntReg] == nullptr; --LargestIntReg)
    assert(LargestIntReg != MVT::i1 && "No integer registers defined!");

  // Every integer value type larger than this largest register takes twice as
  // many registers to represent as the previous ValueType.
  for (unsigned ExpandedReg = LargestIntReg + 1;
       ExpandedReg <= MVT::LAST_INTEGER_VALUETYPE; ++ExpandedReg) {
    NumRegistersForVT[ExpandedReg] = 2*NumRegistersForVT[ExpandedReg-1];
    RegisterTypeForVT[ExpandedReg] = (MVT::SimpleValueType)LargestIntReg;
    TransformToType[ExpandedReg] = (MVT::SimpleValueType)(ExpandedReg - 1);
    ValueTypeActions.setTypeAction((MVT::SimpleValueType)ExpandedReg,
                                   TypeExpandInteger);
  }

  // Inspect all of the ValueType's smaller than the largest integer
  // register to see which ones need promotion.
  unsigned LegalIntReg = LargestIntReg;
  for (unsigned IntReg = LargestIntReg - 1;
       IntReg >= (unsigned)MVT::i1; --IntReg) {
    MVT IVT = (MVT::SimpleValueType)IntReg;
    if (isTypeLegal(IVT)) {
      LegalIntReg = IntReg;
    } else {
      RegisterTypeForVT[IntReg] = TransformToType[IntReg] =
        (MVT::SimpleValueType)LegalIntReg;
      ValueTypeActions.setTypeAction(IVT, TypePromoteInteger);
    }
  }

  // ppcf128 type is really two f64's.
  if (!isTypeLegal(MVT::ppcf128)) {
    if (isTypeLegal(MVT::f64)) {
      NumRegistersForVT[MVT::ppcf128] = 2*NumRegistersForVT[MVT::f64];
      RegisterTypeForVT[MVT::ppcf128] = MVT::f64;
      TransformToType[MVT::ppcf128] = MVT::f64;
      ValueTypeActions.setTypeAction(MVT::ppcf128, TypeExpandFloat);
    } else {
      NumRegistersForVT[MVT::ppcf128] = NumRegistersForVT[MVT::i128];
      RegisterTypeForVT[MVT::ppcf128] = RegisterTypeForVT[MVT::i128];
      TransformToType[MVT::ppcf128] = MVT::i128;
      ValueTypeActions.setTypeAction(MVT::ppcf128, TypeSoftenFloat);
    }
  }

  // Decide how to handle f128. If the target does not have native f128 support,
  // expand it to i128 and we will be generating soft float library calls.
  if (!isTypeLegal(MVT::f128)) {
    NumRegistersForVT[MVT::f128] = NumRegistersForVT[MVT::i128];
    RegisterTypeForVT[MVT::f128] = RegisterTypeForVT[MVT::i128];
    TransformToType[MVT::f128] = MVT::i128;
    ValueTypeActions.setTypeAction(MVT::f128, TypeSoftenFloat);
  }

  // Decide how to handle f64. If the target does not have native f64 support,
  // expand it to i64 and we will be generating soft float library calls.
  if (!isTypeLegal(MVT::f64)) {
    NumRegistersForVT[MVT::f64] = NumRegistersForVT[MVT::i64];
    RegisterTypeForVT[MVT::f64] = RegisterTypeForVT[MVT::i64];
    TransformToType[MVT::f64] = MVT::i64;
    ValueTypeActions.setTypeAction(MVT::f64, TypeSoftenFloat);
  }

  // Decide how to handle f32. If the target does not have native f32 support,
  // expand it to i32 and we will be generating soft float library calls.
  if (!isTypeLegal(MVT::f32)) {
    NumRegistersForVT[MVT::f32] = NumRegistersForVT[MVT::i32];
    RegisterTypeForVT[MVT::f32] = RegisterTypeForVT[MVT::i32];
    TransformToType[MVT::f32] = MVT::i32;
    ValueTypeActions.setTypeAction(MVT::f32, TypeSoftenFloat);
  }

  // Decide how to handle f16. If the target does not have native f16 support,
  // promote it to f32, because there are no f16 library calls (except for
  // conversions).
  if (!isTypeLegal(MVT::f16)) {
    NumRegistersForVT[MVT::f16] = NumRegistersForVT[MVT::f32];
    RegisterTypeForVT[MVT::f16] = RegisterTypeForVT[MVT::f32];
    TransformToType[MVT::f16] = MVT::f32;
    ValueTypeActions.setTypeAction(MVT::f16, TypePromoteFloat);
  }

  // Loop over all of the vector value types to see which need transformations.
  for (unsigned i = MVT::FIRST_VECTOR_VALUETYPE;
       i <= (unsigned)MVT::LAST_VECTOR_VALUETYPE; ++i) {
    MVT VT = (MVT::SimpleValueType) i;
    if (isTypeLegal(VT))
      continue;

    MVT EltVT = VT.getVectorElementType();
    unsigned NElts = VT.getVectorNumElements();
    bool IsLegalWiderType = false;
    LegalizeTypeAction PreferredAction = getPreferredVectorAction(VT);
    switch (PreferredAction) {
    case TypePromoteInteger:
      // Try to promote the elements of integer vectors. If no legal
      // promotion was found, fall through to the widen-vector method.
      for (unsigned nVT = i + 1; nVT <= MVT::LAST_INTEGER_VECTOR_VALUETYPE; ++nVT) {
        MVT SVT = (MVT::SimpleValueType) nVT;
        // Promote vectors of integers to vectors with the same number
        // of elements, with a wider element type.
        if (SVT.getScalarSizeInBits() > EltVT.getSizeInBits() &&
            SVT.getVectorNumElements() == NElts && isTypeLegal(SVT)) {
          TransformToType[i] = SVT;
          RegisterTypeForVT[i] = SVT;
          NumRegistersForVT[i] = 1;
          ValueTypeActions.setTypeAction(VT, TypePromoteInteger);
          IsLegalWiderType = true;
          break;
        }
      }
      if (IsLegalWiderType)
        break;
      LLVM_FALLTHROUGH;

    case TypeWidenVector:
      // Try to widen the vector.
      for (unsigned nVT = i + 1; nVT <= MVT::LAST_VECTOR_VALUETYPE; ++nVT) {
        MVT SVT = (MVT::SimpleValueType) nVT;
        if (SVT.getVectorElementType() == EltVT
            && SVT.getVectorNumElements() > NElts && isTypeLegal(SVT)) {
          TransformToType[i] = SVT;
          RegisterTypeForVT[i] = SVT;
          NumRegistersForVT[i] = 1;
          ValueTypeActions.setTypeAction(VT, TypeWidenVector);
          IsLegalWiderType = true;
          break;
        }
      }
      if (IsLegalWiderType)
        break;
      LLVM_FALLTHROUGH;

    case TypeSplitVector:
    case TypeScalarizeVector: {
      MVT IntermediateVT;
      MVT RegisterVT;
      unsigned NumIntermediates;
      NumRegistersForVT[i] = getVectorTypeBreakdownMVT(VT, IntermediateVT,
          NumIntermediates, RegisterVT, this);
      RegisterTypeForVT[i] = RegisterVT;

      MVT NVT = VT.getPow2VectorType();
      if (NVT == VT) {
        // Type is already a power of 2.  The default action is to split.
        TransformToType[i] = MVT::Other;
        if (PreferredAction == TypeScalarizeVector)
          ValueTypeActions.setTypeAction(VT, TypeScalarizeVector);
        else if (PreferredAction == TypeSplitVector)
          ValueTypeActions.setTypeAction(VT, TypeSplitVector);
        else
          // Set type action according to the number of elements.
          ValueTypeActions.setTypeAction(VT, NElts == 1 ? TypeScalarizeVector
                                                        : TypeSplitVector);
      } else {
        TransformToType[i] = NVT;
        ValueTypeActions.setTypeAction(VT, TypeWidenVector);
      }
      break;
    }
    default:
      llvm_unreachable("Unknown vector legalization action!");
    }
  }

  // Determine the 'representative' register class for each value type.
  // An representative register class is the largest (meaning one which is
  // not a sub-register class / subreg register class) legal register class for
  // a group of value types. For example, on i386, i8, i16, and i32
  // representative would be GR32; while on x86_64 it's GR64.
  for (unsigned i = 0; i != MVT::LAST_VALUETYPE; ++i) {
    const TargetRegisterClass* RRC;
    uint8_t Cost;
    std::tie(RRC, Cost) = findRepresentativeClass(TRI, (MVT::SimpleValueType)i);
    RepRegClassForVT[i] = RRC;
    RepRegClassCostForVT[i] = Cost;
  }
}

EVT TargetLoweringBase::getSetCCResultType(const DataLayout &DL, LLVMContext &,
                                           EVT VT) const {
  assert(!VT.isVector() && "No default SetCC type for vectors!");
  return getPointerTy(DL).SimpleTy;
}

MVT::SimpleValueType TargetLoweringBase::getCmpLibcallReturnType() const {
  return MVT::i32; // return the default value
}

/// getVectorTypeBreakdown - Vector types are broken down into some number of
/// legal first class types.  For example, MVT::v8f32 maps to 2 MVT::v4f32
/// with Altivec or SSE1, or 8 promoted MVT::f64 values with the X86 FP stack.
/// Similarly, MVT::v2i64 turns into 4 MVT::i32 values with both PPC and X86.
///
/// This method returns the number of registers needed, and the VT for each
/// register.  It also returns the VT and quantity of the intermediate values
/// before they are promoted/expanded.
unsigned TargetLoweringBase::getVectorTypeBreakdown(LLVMContext &Context, EVT VT,
                                                EVT &IntermediateVT,
                                                unsigned &NumIntermediates,
                                                MVT &RegisterVT) const {
  unsigned NumElts = VT.getVectorNumElements();

  // If there is a wider vector type with the same element type as this one,
  // or a promoted vector type that has the same number of elements which
  // are wider, then we should convert to that legal vector type.
  // This handles things like <2 x float> -> <4 x float> and
  // <4 x i1> -> <4 x i32>.
  LegalizeTypeAction TA = getTypeAction(Context, VT);
  if (NumElts != 1 && (TA == TypeWidenVector || TA == TypePromoteInteger)) {
    EVT RegisterEVT = getTypeToTransformTo(Context, VT);
    if (isTypeLegal(RegisterEVT)) {
      IntermediateVT = RegisterEVT;
      RegisterVT = RegisterEVT.getSimpleVT();
      NumIntermediates = 1;
      return 1;
    }
  }

  // Figure out the right, legal destination reg to copy into.
  EVT EltTy = VT.getVectorElementType();

  unsigned NumVectorRegs = 1;

  // FIXME: We don't support non-power-of-2-sized vectors for now.  Ideally we
  // could break down into LHS/RHS like LegalizeDAG does.
  if (!isPowerOf2_32(NumElts)) {
    NumVectorRegs = NumElts;
    NumElts = 1;
  }

  // Divide the input until we get to a supported size.  This will always
  // end with a scalar if the target doesn't support vectors.
  while (NumElts > 1 && !isTypeLegal(
                                   EVT::getVectorVT(Context, EltTy, NumElts))) {
    NumElts >>= 1;
    NumVectorRegs <<= 1;
  }

  NumIntermediates = NumVectorRegs;

  EVT NewVT = EVT::getVectorVT(Context, EltTy, NumElts);
  if (!isTypeLegal(NewVT))
    NewVT = EltTy;
  IntermediateVT = NewVT;

  MVT DestVT = getRegisterType(Context, NewVT);
  RegisterVT = DestVT;
  unsigned NewVTSize = NewVT.getSizeInBits();

  // Convert sizes such as i33 to i64.
  if (!isPowerOf2_32(NewVTSize))
    NewVTSize = NextPowerOf2(NewVTSize);

  if (EVT(DestVT).bitsLT(NewVT))   // Value is expanded, e.g. i64 -> i16.
    return NumVectorRegs*(NewVTSize/DestVT.getSizeInBits());

  // Otherwise, promotion or legal types use the same number of registers as
  // the vector decimated to the appropriate level.
  return NumVectorRegs;
}

/// Get the EVTs and ArgFlags collections that represent the legalized return
/// type of the given function.  This does not require a DAG or a return value,
/// and is suitable for use before any DAGs for the function are constructed.
/// TODO: Move this out of TargetLowering.cpp.
void llvm::GetReturnInfo(CallingConv::ID CC, Type *ReturnType,
                         AttributeList attr,
                         SmallVectorImpl<ISD::OutputArg> &Outs,
                         const TargetLowering &TLI, const DataLayout &DL) {
  SmallVector<EVT, 4> ValueVTs;
  ComputeValueVTs(TLI, DL, ReturnType, ValueVTs);
  unsigned NumValues = ValueVTs.size();
  if (NumValues == 0) return;

  for (unsigned j = 0, f = NumValues; j != f; ++j) {
    EVT VT = ValueVTs[j];
    ISD::NodeType ExtendKind = ISD::ANY_EXTEND;

    if (attr.hasAttribute(AttributeList::ReturnIndex, Attribute::SExt))
      ExtendKind = ISD::SIGN_EXTEND;
    else if (attr.hasAttribute(AttributeList::ReturnIndex, Attribute::ZExt))
      ExtendKind = ISD::ZERO_EXTEND;

    // FIXME: C calling convention requires the return type to be promoted to
    // at least 32-bit. But this is not necessary for non-C calling
    // conventions. The frontend should mark functions whose return values
    // require promoting with signext or zeroext attributes.
    if (ExtendKind != ISD::ANY_EXTEND && VT.isInteger()) {
      MVT MinVT = TLI.getRegisterType(ReturnType->getContext(), MVT::i32);
      if (VT.bitsLT(MinVT))
        VT = MinVT;
    }

    unsigned NumParts =
        TLI.getNumRegistersForCallingConv(ReturnType->getContext(), CC, VT);
    MVT PartVT =
        TLI.getRegisterTypeForCallingConv(ReturnType->getContext(), CC, VT);

    // 'inreg' on function refers to return value
    ISD::ArgFlagsTy Flags = ISD::ArgFlagsTy();
    if (attr.hasAttribute(AttributeList::ReturnIndex, Attribute::InReg))
      Flags.setInReg();

    // Propagate extension type if any
    if (attr.hasAttribute(AttributeList::ReturnIndex, Attribute::SExt))
      Flags.setSExt();
    else if (attr.hasAttribute(AttributeList::ReturnIndex, Attribute::ZExt))
      Flags.setZExt();

    for (unsigned i = 0; i < NumParts; ++i)
      Outs.push_back(ISD::OutputArg(Flags, PartVT, VT, /*isFixed=*/true, 0, 0));
  }
}

/// getByValTypeAlignment - Return the desired alignment for ByVal aggregate
/// function arguments in the caller parameter area.  This is the actual
/// alignment, not its logarithm.
unsigned TargetLoweringBase::getByValTypeAlignment(Type *Ty,
                                                   const DataLayout &DL) const {
  return DL.getABITypeAlignment(Ty);
}

bool TargetLoweringBase::allowsMemoryAccess(LLVMContext &Context,
                                            const DataLayout &DL, EVT VT,
                                            unsigned AddrSpace,
                                            unsigned Alignment,
                                            bool *Fast) const {
  // Check if the specified alignment is sufficient based on the data layout.
  // TODO: While using the data layout works in practice, a better solution
  // would be to implement this check directly (make this a virtual function).
  // For example, the ABI alignment may change based on software platform while
  // this function should only be affected by hardware implementation.
  Type *Ty = VT.getTypeForEVT(Context);
  if (Alignment >= DL.getABITypeAlignment(Ty)) {
    // Assume that an access that meets the ABI-specified alignment is fast.
    if (Fast != nullptr)
      *Fast = true;
    return true;
  }

  // This is a misaligned access.
  return allowsMisalignedMemoryAccesses(VT, AddrSpace, Alignment, Fast);
}

BranchProbability TargetLoweringBase::getPredictableBranchThreshold() const {
  return BranchProbability(MinPercentageForPredictableBranch, 100);
}

//===----------------------------------------------------------------------===//
//  TargetTransformInfo Helpers
//===----------------------------------------------------------------------===//

int TargetLoweringBase::InstructionOpcodeToISD(unsigned Opcode) const {
  enum InstructionOpcodes {
#define HANDLE_INST(NUM, OPCODE, CLASS) OPCODE = NUM,
#define LAST_OTHER_INST(NUM) InstructionOpcodesCount = NUM
#include "llvm/IR/Instruction.def"
  };
  switch (static_cast<InstructionOpcodes>(Opcode)) {
  case Ret:            return 0;
  case Br:             return 0;
  case Switch:         return 0;
  case IndirectBr:     return 0;
  case Invoke:         return 0;
  case Resume:         return 0;
  case Unreachable:    return 0;
  case CleanupRet:     return 0;
  case CatchRet:       return 0;
  case CatchPad:       return 0;
  case CatchSwitch:    return 0;
  case CleanupPad:     return 0;
  case FNeg:           return ISD::FNEG;
  case Add:            return ISD::ADD;
  case FAdd:           return ISD::FADD;
  case Sub:            return ISD::SUB;
  case FSub:           return ISD::FSUB;
  case Mul:            return ISD::MUL;
  case FMul:           return ISD::FMUL;
  case UDiv:           return ISD::UDIV;
  case SDiv:           return ISD::SDIV;
  case FDiv:           return ISD::FDIV;
  case URem:           return ISD::UREM;
  case SRem:           return ISD::SREM;
  case FRem:           return ISD::FREM;
  case Shl:            return ISD::SHL;
  case LShr:           return ISD::SRL;
  case AShr:           return ISD::SRA;
  case And:            return ISD::AND;
  case Or:             return ISD::OR;
  case Xor:            return ISD::XOR;
  case Alloca:         return 0;
  case Load:           return ISD::LOAD;
  case Store:          return ISD::STORE;
  case GetElementPtr:  return 0;
  case Fence:          return 0;
  case AtomicCmpXchg:  return 0;
  case AtomicRMW:      return 0;
  case Trunc:          return ISD::TRUNCATE;
  case ZExt:           return ISD::ZERO_EXTEND;
  case SExt:           return ISD::SIGN_EXTEND;
  case FPToUI:         return ISD::FP_TO_UINT;
  case FPToSI:         return ISD::FP_TO_SINT;
  case UIToFP:         return ISD::UINT_TO_FP;
  case SIToFP:         return ISD::SINT_TO_FP;
  case FPTrunc:        return ISD::FP_ROUND;
  case FPExt:          return ISD::FP_EXTEND;
  case PtrToInt:       return ISD::BITCAST;
  case IntToPtr:       return ISD::BITCAST;
  case BitCast:        return ISD::BITCAST;
  case AddrSpaceCast:  return ISD::ADDRSPACECAST;
  case ICmp:           return ISD::SETCC;
  case FCmp:           return ISD::SETCC;
  case PHI:            return 0;
  case Call:           return 0;
  case Select:         return ISD::SELECT;
  case UserOp1:        return 0;
  case UserOp2:        return 0;
  case VAArg:          return 0;
  case ExtractElement: return ISD::EXTRACT_VECTOR_ELT;
  case InsertElement:  return ISD::INSERT_VECTOR_ELT;
  case ShuffleVector:  return ISD::VECTOR_SHUFFLE;
  case ExtractValue:   return ISD::MERGE_VALUES;
  case InsertValue:    return ISD::MERGE_VALUES;
  case LandingPad:     return 0;
  }

  llvm_unreachable("Unknown instruction type encountered!");
}

std::pair<int, MVT>
TargetLoweringBase::getTypeLegalizationCost(const DataLayout &DL,
                                            Type *Ty) const {
  LLVMContext &C = Ty->getContext();
  EVT MTy = getValueType(DL, Ty);

  int Cost = 1;
  // We keep legalizing the type until we find a legal kind. We assume that
  // the only operation that costs anything is the split. After splitting
  // we need to handle two types.
  while (true) {
    LegalizeKind LK = getTypeConversion(C, MTy);

    if (LK.first == TypeLegal)
      return std::make_pair(Cost, MTy.getSimpleVT());

    if (LK.first == TypeSplitVector || LK.first == TypeExpandInteger)
      Cost *= 2;

    // Do not loop with f128 type.
    if (MTy == LK.second)
      return std::make_pair(Cost, MTy.getSimpleVT());

    // Keep legalizing the type.
    MTy = LK.second;
  }
}

Value *TargetLoweringBase::getDefaultSafeStackPointerLocation(IRBuilder<> &IRB,
                                                              bool UseTLS) const {
  // compiler-rt provides a variable with a magic name.  Targets that do not
  // link with compiler-rt may also provide such a variable.
  Module *M = IRB.GetInsertBlock()->getParent()->getParent();
  const char *UnsafeStackPtrVar = "__safestack_unsafe_stack_ptr";
  auto UnsafeStackPtr =
      dyn_cast_or_null<GlobalVariable>(M->getNamedValue(UnsafeStackPtrVar));

  Type *StackPtrTy = Type::getInt8PtrTy(M->getContext());

  if (!UnsafeStackPtr) {
    auto TLSModel = UseTLS ?
        GlobalValue::InitialExecTLSModel :
        GlobalValue::NotThreadLocal;
    // The global variable is not defined yet, define it ourselves.
    // We use the initial-exec TLS model because we do not support the
    // variable living anywhere other than in the main executable.
    UnsafeStackPtr = new GlobalVariable(
        *M, StackPtrTy, false, GlobalValue::ExternalLinkage, nullptr,
        UnsafeStackPtrVar, nullptr, TLSModel);
  } else {
    // The variable exists, check its type and attributes.
    if (UnsafeStackPtr->getValueType() != StackPtrTy)
      report_fatal_error(Twine(UnsafeStackPtrVar) + " must have void* type");
    if (UseTLS != UnsafeStackPtr->isThreadLocal())
      report_fatal_error(Twine(UnsafeStackPtrVar) + " must " +
                         (UseTLS ? "" : "not ") + "be thread-local");
  }
  return UnsafeStackPtr;
}

Value *TargetLoweringBase::getSafeStackPointerLocation(IRBuilder<> &IRB) const {
  if (!TM.getTargetTriple().isAndroid())
    return getDefaultSafeStackPointerLocation(IRB, true);

  // Android provides a libc function to retrieve the address of the current
  // thread's unsafe stack pointer.
  Module *M = IRB.GetInsertBlock()->getParent()->getParent();
  Type *StackPtrTy = Type::getInt8PtrTy(M->getContext());
  Value *Fn = M->getOrInsertFunction("__safestack_pointer_address",
                                     StackPtrTy->getPointerTo(0));
  return IRB.CreateCall(Fn);
}

//===----------------------------------------------------------------------===//
//  Loop Strength Reduction hooks
//===----------------------------------------------------------------------===//

/// isLegalAddressingMode - Return true if the addressing mode represented
/// by AM is legal for this target, for a load/store of the specified type.
bool TargetLoweringBase::isLegalAddressingMode(const DataLayout &DL,
                                               const AddrMode &AM, Type *Ty,
                                               unsigned AS, Instruction *I) const {
  // The default implementation of this implements a conservative RISCy, r+r and
  // r+i addr mode.

  // Allows a sign-extended 16-bit immediate field.
  if (AM.BaseOffs <= -(1LL << 16) || AM.BaseOffs >= (1LL << 16)-1)
    return false;

  // No global is ever allowed as a base.
  if (AM.BaseGV)
    return false;

  // Only support r+r,
  switch (AM.Scale) {
  case 0:  // "r+i" or just "i", depending on HasBaseReg.
    break;
  case 1:
    if (AM.HasBaseReg && AM.BaseOffs)  // "r+r+i" is not allowed.
      return false;
    // Otherwise we have r+r or r+i.
    break;
  case 2:
    if (AM.HasBaseReg || AM.BaseOffs)  // 2*r+r  or  2*r+i is not allowed.
      return false;
    // Allow 2*r as r+r.
    break;
  default: // Don't allow n * r
    return false;
  }

  return true;
}

//===----------------------------------------------------------------------===//
//  Stack Protector
//===----------------------------------------------------------------------===//

// For OpenBSD return its special guard variable. Otherwise return nullptr,
// so that SelectionDAG handle SSP.
Value *TargetLoweringBase::getIRStackGuard(IRBuilder<> &IRB) const {
  if (getTargetMachine().getTargetTriple().isOSOpenBSD()) {
    Module &M = *IRB.GetInsertBlock()->getParent()->getParent();
    PointerType *PtrTy = Type::getInt8PtrTy(M.getContext());
    return M.getOrInsertGlobal("__guard_local", PtrTy);
  }
  return nullptr;
}

// Currently only support "standard" __stack_chk_guard.
// TODO: add LOAD_STACK_GUARD support.
void TargetLoweringBase::insertSSPDeclarations(Module &M) const {
  if (!M.getNamedValue("__stack_chk_guard"))
    new GlobalVariable(M, Type::getInt8PtrTy(M.getContext()), false,
                       GlobalVariable::ExternalLinkage,
                       nullptr, "__stack_chk_guard");
}

// Currently only support "standard" __stack_chk_guard.
// TODO: add LOAD_STACK_GUARD support.
Value *TargetLoweringBase::getSDagStackGuard(const Module &M) const {
  return M.getNamedValue("__stack_chk_guard");
}

Value *TargetLoweringBase::getSSPStackGuardCheck(const Module &M) const {
  return nullptr;
}

unsigned TargetLoweringBase::getMinimumJumpTableEntries() const {
  return MinimumJumpTableEntries;
}

void TargetLoweringBase::setMinimumJumpTableEntries(unsigned Val) {
  MinimumJumpTableEntries = Val;
}

unsigned TargetLoweringBase::getMinimumJumpTableDensity(bool OptForSize) const {
  return OptForSize ? OptsizeJumpTableDensity : JumpTableDensity;
}

unsigned TargetLoweringBase::getMaximumJumpTableSize() const {
  return MaximumJumpTableSize;
}

void TargetLoweringBase::setMaximumJumpTableSize(unsigned Val) {
  MaximumJumpTableSize = Val;
}

//===----------------------------------------------------------------------===//
//  Reciprocal Estimates
//===----------------------------------------------------------------------===//

/// Get the reciprocal estimate attribute string for a function that will
/// override the target defaults.
static StringRef getRecipEstimateForFunc(MachineFunction &MF) {
  const Function &F = MF.getFunction();
  return F.getFnAttribute("reciprocal-estimates").getValueAsString();
}

/// Construct a string for the given reciprocal operation of the given type.
/// This string should match the corresponding option to the front-end's
/// "-mrecip" flag assuming those strings have been passed through in an
/// attribute string. For example, "vec-divf" for a division of a vXf32.
static std::string getReciprocalOpName(bool IsSqrt, EVT VT) {
  std::string Name = VT.isVector() ? "vec-" : "";

  Name += IsSqrt ? "sqrt" : "div";

  // TODO: Handle "half" or other float types?
  if (VT.getScalarType() == MVT::f64) {
    Name += "d";
  } else {
    assert(VT.getScalarType() == MVT::f32 &&
           "Unexpected FP type for reciprocal estimate");
    Name += "f";
  }

  return Name;
}

/// Return the character position and value (a single numeric character) of a
/// customized refinement operation in the input string if it exists. Return
/// false if there is no customized refinement step count.
static bool parseRefinementStep(StringRef In, size_t &Position,
                                uint8_t &Value) {
  const char RefStepToken = ':';
  Position = In.find(RefStepToken);
  if (Position == StringRef::npos)
    return false;

  StringRef RefStepString = In.substr(Position + 1);
  // Allow exactly one numeric character for the additional refinement
  // step parameter.
  if (RefStepString.size() == 1) {
    char RefStepChar = RefStepString[0];
    if (RefStepChar >= '0' && RefStepChar <= '9') {
      Value = RefStepChar - '0';
      return true;
    }
  }
  report_fatal_error("Invalid refinement step for -recip.");
}

/// For the input attribute string, return one of the ReciprocalEstimate enum
/// status values (enabled, disabled, or not specified) for this operation on
/// the specified data type.
static int getOpEnabled(bool IsSqrt, EVT VT, StringRef Override) {
  if (Override.empty())
    return TargetLoweringBase::ReciprocalEstimate::Unspecified;

  SmallVector<StringRef, 4> OverrideVector;
  Override.split(OverrideVector, ',');
  unsigned NumArgs = OverrideVector.size();

  // Check if "all", "none", or "default" was specified.
  if (NumArgs == 1) {
    // Look for an optional setting of the number of refinement steps needed
    // for this type of reciprocal operation.
    size_t RefPos;
    uint8_t RefSteps;
    if (parseRefinementStep(Override, RefPos, RefSteps)) {
      // Split the string for further processing.
      Override = Override.substr(0, RefPos);
    }

    // All reciprocal types are enabled.
    if (Override == "all")
      return TargetLoweringBase::ReciprocalEstimate::Enabled;

    // All reciprocal types are disabled.
    if (Override == "none")
      return TargetLoweringBase::ReciprocalEstimate::Disabled;

    // Target defaults for enablement are used.
    if (Override == "default")
      return TargetLoweringBase::ReciprocalEstimate::Unspecified;
  }

  // The attribute string may omit the size suffix ('f'/'d').
  std::string VTName = getReciprocalOpName(IsSqrt, VT);
  std::string VTNameNoSize = VTName;
  VTNameNoSize.pop_back();
  static const char DisabledPrefix = '!';

  for (StringRef RecipType : OverrideVector) {
    size_t RefPos;
    uint8_t RefSteps;
    if (parseRefinementStep(RecipType, RefPos, RefSteps))
      RecipType = RecipType.substr(0, RefPos);

    // Ignore the disablement token for string matching.
    bool IsDisabled = RecipType[0] == DisabledPrefix;
    if (IsDisabled)
      RecipType = RecipType.substr(1);

    if (RecipType.equals(VTName) || RecipType.equals(VTNameNoSize))
      return IsDisabled ? TargetLoweringBase::ReciprocalEstimate::Disabled
                        : TargetLoweringBase::ReciprocalEstimate::Enabled;
  }

  return TargetLoweringBase::ReciprocalEstimate::Unspecified;
}

/// For the input attribute string, return the customized refinement step count
/// for this operation on the specified data type. If the step count does not
/// exist, return the ReciprocalEstimate enum value for unspecified.
static int getOpRefinementSteps(bool IsSqrt, EVT VT, StringRef Override) {
  if (Override.empty())
    return TargetLoweringBase::ReciprocalEstimate::Unspecified;

  SmallVector<StringRef, 4> OverrideVector;
  Override.split(OverrideVector, ',');
  unsigned NumArgs = OverrideVector.size();

  // Check if "all", "default", or "none" was specified.
  if (NumArgs == 1) {
    // Look for an optional setting of the number of refinement steps needed
    // for this type of reciprocal operation.
    size_t RefPos;
    uint8_t RefSteps;
    if (!parseRefinementStep(Override, RefPos, RefSteps))
      return TargetLoweringBase::ReciprocalEstimate::Unspecified;

    // Split the string for further processing.
    Override = Override.substr(0, RefPos);
    assert(Override != "none" &&
           "Disabled reciprocals, but specifed refinement steps?");

    // If this is a general override, return the specified number of steps.
    if (Override == "all" || Override == "default")
      return RefSteps;
  }

  // The attribute string may omit the size suffix ('f'/'d').
  std::string VTName = getReciprocalOpName(IsSqrt, VT);
  std::string VTNameNoSize = VTName;
  VTNameNoSize.pop_back();

  for (StringRef RecipType : OverrideVector) {
    size_t RefPos;
    uint8_t RefSteps;
    if (!parseRefinementStep(RecipType, RefPos, RefSteps))
      continue;

    RecipType = RecipType.substr(0, RefPos);
    if (RecipType.equals(VTName) || RecipType.equals(VTNameNoSize))
      return RefSteps;
  }

  return TargetLoweringBase::ReciprocalEstimate::Unspecified;
}

int TargetLoweringBase::getRecipEstimateSqrtEnabled(EVT VT,
                                                    MachineFunction &MF) const {
  return getOpEnabled(true, VT, getRecipEstimateForFunc(MF));
}

int TargetLoweringBase::getRecipEstimateDivEnabled(EVT VT,
                                                   MachineFunction &MF) const {
  return getOpEnabled(false, VT, getRecipEstimateForFunc(MF));
}

int TargetLoweringBase::getSqrtRefinementSteps(EVT VT,
                                               MachineFunction &MF) const {
  return getOpRefinementSteps(true, VT, getRecipEstimateForFunc(MF));
}

int TargetLoweringBase::getDivRefinementSteps(EVT VT,
                                              MachineFunction &MF) const {
  return getOpRefinementSteps(false, VT, getRecipEstimateForFunc(MF));
}

void TargetLoweringBase::finalizeLowering(MachineFunction &MF) const {
  MF.getRegInfo().freezeReservedRegs(MF);
}
