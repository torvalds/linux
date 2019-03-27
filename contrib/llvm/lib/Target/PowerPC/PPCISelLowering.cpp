//===-- PPCISelLowering.cpp - PPC DAG Lowering Implementation -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the PPCISelLowering class.
//
//===----------------------------------------------------------------------===//

#include "PPCISelLowering.h"
#include "MCTargetDesc/PPCPredicates.h"
#include "PPC.h"
#include "PPCCCState.h"
#include "PPCCallingConv.h"
#include "PPCFrameLowering.h"
#include "PPCInstrInfo.h"
#include "PPCMachineFunctionInfo.h"
#include "PPCPerfectShuffle.h"
#include "PPCRegisterInfo.h"
#include "PPCSubtarget.h"
#include "PPCTargetMachine.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RuntimeLibcalls.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/Value.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/MachineValueType.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <list>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "ppc-lowering"

static cl::opt<bool> DisablePPCPreinc("disable-ppc-preinc",
cl::desc("disable preincrement load/store generation on PPC"), cl::Hidden);

static cl::opt<bool> DisableILPPref("disable-ppc-ilp-pref",
cl::desc("disable setting the node scheduling preference to ILP on PPC"), cl::Hidden);

static cl::opt<bool> DisablePPCUnaligned("disable-ppc-unaligned",
cl::desc("disable unaligned load/store generation on PPC"), cl::Hidden);

static cl::opt<bool> DisableSCO("disable-ppc-sco",
cl::desc("disable sibling call optimization on ppc"), cl::Hidden);

static cl::opt<bool> EnableQuadPrecision("enable-ppc-quad-precision",
cl::desc("enable quad precision float support on ppc"), cl::Hidden);

STATISTIC(NumTailCalls, "Number of tail calls");
STATISTIC(NumSiblingCalls, "Number of sibling calls");

static bool isNByteElemShuffleMask(ShuffleVectorSDNode *, unsigned, int);

// FIXME: Remove this once the bug has been fixed!
extern cl::opt<bool> ANDIGlueBug;

PPCTargetLowering::PPCTargetLowering(const PPCTargetMachine &TM,
                                     const PPCSubtarget &STI)
    : TargetLowering(TM), Subtarget(STI) {
  // Use _setjmp/_longjmp instead of setjmp/longjmp.
  setUseUnderscoreSetJmp(true);
  setUseUnderscoreLongJmp(true);

  // On PPC32/64, arguments smaller than 4/8 bytes are extended, so all
  // arguments are at least 4/8 bytes aligned.
  bool isPPC64 = Subtarget.isPPC64();
  setMinStackArgumentAlignment(isPPC64 ? 8:4);

  // Set up the register classes.
  addRegisterClass(MVT::i32, &PPC::GPRCRegClass);
  if (!useSoftFloat()) {
    if (hasSPE()) {
      addRegisterClass(MVT::f32, &PPC::SPE4RCRegClass);
      addRegisterClass(MVT::f64, &PPC::SPERCRegClass);
    } else {
      addRegisterClass(MVT::f32, &PPC::F4RCRegClass);
      addRegisterClass(MVT::f64, &PPC::F8RCRegClass);
    }
  }

  // Match BITREVERSE to customized fast code sequence in the td file.
  setOperationAction(ISD::BITREVERSE, MVT::i32, Legal);
  setOperationAction(ISD::BITREVERSE, MVT::i64, Legal);

  // Sub-word ATOMIC_CMP_SWAP need to ensure that the input is zero-extended.
  setOperationAction(ISD::ATOMIC_CMP_SWAP, MVT::i32, Custom);

  // PowerPC has an i16 but no i8 (or i1) SEXTLOAD.
  for (MVT VT : MVT::integer_valuetypes()) {
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i8, Expand);
  }

  setTruncStoreAction(MVT::f64, MVT::f32, Expand);

  // PowerPC has pre-inc load and store's.
  setIndexedLoadAction(ISD::PRE_INC, MVT::i1, Legal);
  setIndexedLoadAction(ISD::PRE_INC, MVT::i8, Legal);
  setIndexedLoadAction(ISD::PRE_INC, MVT::i16, Legal);
  setIndexedLoadAction(ISD::PRE_INC, MVT::i32, Legal);
  setIndexedLoadAction(ISD::PRE_INC, MVT::i64, Legal);
  setIndexedStoreAction(ISD::PRE_INC, MVT::i1, Legal);
  setIndexedStoreAction(ISD::PRE_INC, MVT::i8, Legal);
  setIndexedStoreAction(ISD::PRE_INC, MVT::i16, Legal);
  setIndexedStoreAction(ISD::PRE_INC, MVT::i32, Legal);
  setIndexedStoreAction(ISD::PRE_INC, MVT::i64, Legal);
  if (!Subtarget.hasSPE()) {
    setIndexedLoadAction(ISD::PRE_INC, MVT::f32, Legal);
    setIndexedLoadAction(ISD::PRE_INC, MVT::f64, Legal);
    setIndexedStoreAction(ISD::PRE_INC, MVT::f32, Legal);
    setIndexedStoreAction(ISD::PRE_INC, MVT::f64, Legal);
  }

  // PowerPC uses ADDC/ADDE/SUBC/SUBE to propagate carry.
  const MVT ScalarIntVTs[] = { MVT::i32, MVT::i64 };
  for (MVT VT : ScalarIntVTs) {
    setOperationAction(ISD::ADDC, VT, Legal);
    setOperationAction(ISD::ADDE, VT, Legal);
    setOperationAction(ISD::SUBC, VT, Legal);
    setOperationAction(ISD::SUBE, VT, Legal);
  }

  if (Subtarget.useCRBits()) {
    setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);

    if (isPPC64 || Subtarget.hasFPCVT()) {
      setOperationAction(ISD::SINT_TO_FP, MVT::i1, Promote);
      AddPromotedToType (ISD::SINT_TO_FP, MVT::i1,
                         isPPC64 ? MVT::i64 : MVT::i32);
      setOperationAction(ISD::UINT_TO_FP, MVT::i1, Promote);
      AddPromotedToType(ISD::UINT_TO_FP, MVT::i1,
                        isPPC64 ? MVT::i64 : MVT::i32);
    } else {
      setOperationAction(ISD::SINT_TO_FP, MVT::i1, Custom);
      setOperationAction(ISD::UINT_TO_FP, MVT::i1, Custom);
    }

    // PowerPC does not support direct load/store of condition registers.
    setOperationAction(ISD::LOAD, MVT::i1, Custom);
    setOperationAction(ISD::STORE, MVT::i1, Custom);

    // FIXME: Remove this once the ANDI glue bug is fixed:
    if (ANDIGlueBug)
      setOperationAction(ISD::TRUNCATE, MVT::i1, Custom);

    for (MVT VT : MVT::integer_valuetypes()) {
      setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i1, Promote);
      setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::i1, Promote);
      setTruncStoreAction(VT, MVT::i1, Expand);
    }

    addRegisterClass(MVT::i1, &PPC::CRBITRCRegClass);
  }

  // Expand ppcf128 to i32 by hand for the benefit of llvm-gcc bootstrap on
  // PPC (the libcall is not available).
  setOperationAction(ISD::FP_TO_SINT, MVT::ppcf128, Custom);
  setOperationAction(ISD::FP_TO_UINT, MVT::ppcf128, Custom);

  // We do not currently implement these libm ops for PowerPC.
  setOperationAction(ISD::FFLOOR, MVT::ppcf128, Expand);
  setOperationAction(ISD::FCEIL,  MVT::ppcf128, Expand);
  setOperationAction(ISD::FTRUNC, MVT::ppcf128, Expand);
  setOperationAction(ISD::FRINT,  MVT::ppcf128, Expand);
  setOperationAction(ISD::FNEARBYINT, MVT::ppcf128, Expand);
  setOperationAction(ISD::FREM, MVT::ppcf128, Expand);

  // PowerPC has no SREM/UREM instructions unless we are on P9
  // On P9 we may use a hardware instruction to compute the remainder.
  // The instructions are not legalized directly because in the cases where the
  // result of both the remainder and the division is required it is more
  // efficient to compute the remainder from the result of the division rather
  // than use the remainder instruction.
  if (Subtarget.isISA3_0()) {
    setOperationAction(ISD::SREM, MVT::i32, Custom);
    setOperationAction(ISD::UREM, MVT::i32, Custom);
    setOperationAction(ISD::SREM, MVT::i64, Custom);
    setOperationAction(ISD::UREM, MVT::i64, Custom);
  } else {
    setOperationAction(ISD::SREM, MVT::i32, Expand);
    setOperationAction(ISD::UREM, MVT::i32, Expand);
    setOperationAction(ISD::SREM, MVT::i64, Expand);
    setOperationAction(ISD::UREM, MVT::i64, Expand);
  }

  // Don't use SMUL_LOHI/UMUL_LOHI or SDIVREM/UDIVREM to lower SREM/UREM.
  setOperationAction(ISD::UMUL_LOHI, MVT::i32, Expand);
  setOperationAction(ISD::SMUL_LOHI, MVT::i32, Expand);
  setOperationAction(ISD::UMUL_LOHI, MVT::i64, Expand);
  setOperationAction(ISD::SMUL_LOHI, MVT::i64, Expand);
  setOperationAction(ISD::UDIVREM, MVT::i32, Expand);
  setOperationAction(ISD::SDIVREM, MVT::i32, Expand);
  setOperationAction(ISD::UDIVREM, MVT::i64, Expand);
  setOperationAction(ISD::SDIVREM, MVT::i64, Expand);

  // We don't support sin/cos/sqrt/fmod/pow
  setOperationAction(ISD::FSIN , MVT::f64, Expand);
  setOperationAction(ISD::FCOS , MVT::f64, Expand);
  setOperationAction(ISD::FSINCOS, MVT::f64, Expand);
  setOperationAction(ISD::FREM , MVT::f64, Expand);
  setOperationAction(ISD::FPOW , MVT::f64, Expand);
  setOperationAction(ISD::FSIN , MVT::f32, Expand);
  setOperationAction(ISD::FCOS , MVT::f32, Expand);
  setOperationAction(ISD::FSINCOS, MVT::f32, Expand);
  setOperationAction(ISD::FREM , MVT::f32, Expand);
  setOperationAction(ISD::FPOW , MVT::f32, Expand);
  if (Subtarget.hasSPE()) {
    setOperationAction(ISD::FMA  , MVT::f64, Expand);
    setOperationAction(ISD::FMA  , MVT::f32, Expand);
  } else {
    setOperationAction(ISD::FMA  , MVT::f64, Legal);
    setOperationAction(ISD::FMA  , MVT::f32, Legal);
  }

  setOperationAction(ISD::FLT_ROUNDS_, MVT::i32, Custom);

  // If we're enabling GP optimizations, use hardware square root
  if (!Subtarget.hasFSQRT() &&
      !(TM.Options.UnsafeFPMath && Subtarget.hasFRSQRTE() &&
        Subtarget.hasFRE()))
    setOperationAction(ISD::FSQRT, MVT::f64, Expand);

  if (!Subtarget.hasFSQRT() &&
      !(TM.Options.UnsafeFPMath && Subtarget.hasFRSQRTES() &&
        Subtarget.hasFRES()))
    setOperationAction(ISD::FSQRT, MVT::f32, Expand);

  if (Subtarget.hasFCPSGN()) {
    setOperationAction(ISD::FCOPYSIGN, MVT::f64, Legal);
    setOperationAction(ISD::FCOPYSIGN, MVT::f32, Legal);
  } else {
    setOperationAction(ISD::FCOPYSIGN, MVT::f64, Expand);
    setOperationAction(ISD::FCOPYSIGN, MVT::f32, Expand);
  }

  if (Subtarget.hasFPRND()) {
    setOperationAction(ISD::FFLOOR, MVT::f64, Legal);
    setOperationAction(ISD::FCEIL,  MVT::f64, Legal);
    setOperationAction(ISD::FTRUNC, MVT::f64, Legal);
    setOperationAction(ISD::FROUND, MVT::f64, Legal);

    setOperationAction(ISD::FFLOOR, MVT::f32, Legal);
    setOperationAction(ISD::FCEIL,  MVT::f32, Legal);
    setOperationAction(ISD::FTRUNC, MVT::f32, Legal);
    setOperationAction(ISD::FROUND, MVT::f32, Legal);
  }

  // PowerPC does not have BSWAP, but we can use vector BSWAP instruction xxbrd
  // to speed up scalar BSWAP64.
  // CTPOP or CTTZ were introduced in P8/P9 respectively
  setOperationAction(ISD::BSWAP, MVT::i32  , Expand);
  if (Subtarget.hasP9Vector())
    setOperationAction(ISD::BSWAP, MVT::i64  , Custom);
  else
    setOperationAction(ISD::BSWAP, MVT::i64  , Expand);
  if (Subtarget.isISA3_0()) {
    setOperationAction(ISD::CTTZ , MVT::i32  , Legal);
    setOperationAction(ISD::CTTZ , MVT::i64  , Legal);
  } else {
    setOperationAction(ISD::CTTZ , MVT::i32  , Expand);
    setOperationAction(ISD::CTTZ , MVT::i64  , Expand);
  }

  if (Subtarget.hasPOPCNTD() == PPCSubtarget::POPCNTD_Fast) {
    setOperationAction(ISD::CTPOP, MVT::i32  , Legal);
    setOperationAction(ISD::CTPOP, MVT::i64  , Legal);
  } else {
    setOperationAction(ISD::CTPOP, MVT::i32  , Expand);
    setOperationAction(ISD::CTPOP, MVT::i64  , Expand);
  }

  // PowerPC does not have ROTR
  setOperationAction(ISD::ROTR, MVT::i32   , Expand);
  setOperationAction(ISD::ROTR, MVT::i64   , Expand);

  if (!Subtarget.useCRBits()) {
    // PowerPC does not have Select
    setOperationAction(ISD::SELECT, MVT::i32, Expand);
    setOperationAction(ISD::SELECT, MVT::i64, Expand);
    setOperationAction(ISD::SELECT, MVT::f32, Expand);
    setOperationAction(ISD::SELECT, MVT::f64, Expand);
  }

  // PowerPC wants to turn select_cc of FP into fsel when possible.
  setOperationAction(ISD::SELECT_CC, MVT::f32, Custom);
  setOperationAction(ISD::SELECT_CC, MVT::f64, Custom);

  // PowerPC wants to optimize integer setcc a bit
  if (!Subtarget.useCRBits())
    setOperationAction(ISD::SETCC, MVT::i32, Custom);

  // PowerPC does not have BRCOND which requires SetCC
  if (!Subtarget.useCRBits())
    setOperationAction(ISD::BRCOND, MVT::Other, Expand);

  setOperationAction(ISD::BR_JT,  MVT::Other, Expand);

  if (Subtarget.hasSPE()) {
    // SPE has built-in conversions
    setOperationAction(ISD::FP_TO_SINT, MVT::i32, Legal);
    setOperationAction(ISD::SINT_TO_FP, MVT::i32, Legal);
    setOperationAction(ISD::UINT_TO_FP, MVT::i32, Legal);
  } else {
    // PowerPC turns FP_TO_SINT into FCTIWZ and some load/stores.
    setOperationAction(ISD::FP_TO_SINT, MVT::i32, Custom);

    // PowerPC does not have [U|S]INT_TO_FP
    setOperationAction(ISD::SINT_TO_FP, MVT::i32, Expand);
    setOperationAction(ISD::UINT_TO_FP, MVT::i32, Expand);
  }

  if (Subtarget.hasDirectMove() && isPPC64) {
    setOperationAction(ISD::BITCAST, MVT::f32, Legal);
    setOperationAction(ISD::BITCAST, MVT::i32, Legal);
    setOperationAction(ISD::BITCAST, MVT::i64, Legal);
    setOperationAction(ISD::BITCAST, MVT::f64, Legal);
  } else {
    setOperationAction(ISD::BITCAST, MVT::f32, Expand);
    setOperationAction(ISD::BITCAST, MVT::i32, Expand);
    setOperationAction(ISD::BITCAST, MVT::i64, Expand);
    setOperationAction(ISD::BITCAST, MVT::f64, Expand);
  }

  // We cannot sextinreg(i1).  Expand to shifts.
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);

  // NOTE: EH_SJLJ_SETJMP/_LONGJMP supported here is NOT intended to support
  // SjLj exception handling but a light-weight setjmp/longjmp replacement to
  // support continuation, user-level threading, and etc.. As a result, no
  // other SjLj exception interfaces are implemented and please don't build
  // your own exception handling based on them.
  // LLVM/Clang supports zero-cost DWARF exception handling.
  setOperationAction(ISD::EH_SJLJ_SETJMP, MVT::i32, Custom);
  setOperationAction(ISD::EH_SJLJ_LONGJMP, MVT::Other, Custom);

  // We want to legalize GlobalAddress and ConstantPool nodes into the
  // appropriate instructions to materialize the address.
  setOperationAction(ISD::GlobalAddress, MVT::i32, Custom);
  setOperationAction(ISD::GlobalTLSAddress, MVT::i32, Custom);
  setOperationAction(ISD::BlockAddress,  MVT::i32, Custom);
  setOperationAction(ISD::ConstantPool,  MVT::i32, Custom);
  setOperationAction(ISD::JumpTable,     MVT::i32, Custom);
  setOperationAction(ISD::GlobalAddress, MVT::i64, Custom);
  setOperationAction(ISD::GlobalTLSAddress, MVT::i64, Custom);
  setOperationAction(ISD::BlockAddress,  MVT::i64, Custom);
  setOperationAction(ISD::ConstantPool,  MVT::i64, Custom);
  setOperationAction(ISD::JumpTable,     MVT::i64, Custom);

  // TRAP is legal.
  setOperationAction(ISD::TRAP, MVT::Other, Legal);

  // TRAMPOLINE is custom lowered.
  setOperationAction(ISD::INIT_TRAMPOLINE, MVT::Other, Custom);
  setOperationAction(ISD::ADJUST_TRAMPOLINE, MVT::Other, Custom);

  // VASTART needs to be custom lowered to use the VarArgsFrameIndex
  setOperationAction(ISD::VASTART           , MVT::Other, Custom);

  if (Subtarget.isSVR4ABI()) {
    if (isPPC64) {
      // VAARG always uses double-word chunks, so promote anything smaller.
      setOperationAction(ISD::VAARG, MVT::i1, Promote);
      AddPromotedToType (ISD::VAARG, MVT::i1, MVT::i64);
      setOperationAction(ISD::VAARG, MVT::i8, Promote);
      AddPromotedToType (ISD::VAARG, MVT::i8, MVT::i64);
      setOperationAction(ISD::VAARG, MVT::i16, Promote);
      AddPromotedToType (ISD::VAARG, MVT::i16, MVT::i64);
      setOperationAction(ISD::VAARG, MVT::i32, Promote);
      AddPromotedToType (ISD::VAARG, MVT::i32, MVT::i64);
      setOperationAction(ISD::VAARG, MVT::Other, Expand);
    } else {
      // VAARG is custom lowered with the 32-bit SVR4 ABI.
      setOperationAction(ISD::VAARG, MVT::Other, Custom);
      setOperationAction(ISD::VAARG, MVT::i64, Custom);
    }
  } else
    setOperationAction(ISD::VAARG, MVT::Other, Expand);

  if (Subtarget.isSVR4ABI() && !isPPC64)
    // VACOPY is custom lowered with the 32-bit SVR4 ABI.
    setOperationAction(ISD::VACOPY            , MVT::Other, Custom);
  else
    setOperationAction(ISD::VACOPY            , MVT::Other, Expand);

  // Use the default implementation.
  setOperationAction(ISD::VAEND             , MVT::Other, Expand);
  setOperationAction(ISD::STACKSAVE         , MVT::Other, Expand);
  setOperationAction(ISD::STACKRESTORE      , MVT::Other, Custom);
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i32  , Custom);
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i64  , Custom);
  setOperationAction(ISD::GET_DYNAMIC_AREA_OFFSET, MVT::i32, Custom);
  setOperationAction(ISD::GET_DYNAMIC_AREA_OFFSET, MVT::i64, Custom);
  setOperationAction(ISD::EH_DWARF_CFA, MVT::i32, Custom);
  setOperationAction(ISD::EH_DWARF_CFA, MVT::i64, Custom);

  // We want to custom lower some of our intrinsics.
  setOperationAction(ISD::INTRINSIC_WO_CHAIN, MVT::Other, Custom);

  // To handle counter-based loop conditions.
  setOperationAction(ISD::INTRINSIC_W_CHAIN, MVT::i1, Custom);

  setOperationAction(ISD::INTRINSIC_VOID, MVT::i8, Custom);
  setOperationAction(ISD::INTRINSIC_VOID, MVT::i16, Custom);
  setOperationAction(ISD::INTRINSIC_VOID, MVT::i32, Custom);
  setOperationAction(ISD::INTRINSIC_VOID, MVT::Other, Custom);

  // Comparisons that require checking two conditions.
  if (Subtarget.hasSPE()) {
    setCondCodeAction(ISD::SETO, MVT::f32, Expand);
    setCondCodeAction(ISD::SETO, MVT::f64, Expand);
    setCondCodeAction(ISD::SETUO, MVT::f32, Expand);
    setCondCodeAction(ISD::SETUO, MVT::f64, Expand);
  }
  setCondCodeAction(ISD::SETULT, MVT::f32, Expand);
  setCondCodeAction(ISD::SETULT, MVT::f64, Expand);
  setCondCodeAction(ISD::SETUGT, MVT::f32, Expand);
  setCondCodeAction(ISD::SETUGT, MVT::f64, Expand);
  setCondCodeAction(ISD::SETUEQ, MVT::f32, Expand);
  setCondCodeAction(ISD::SETUEQ, MVT::f64, Expand);
  setCondCodeAction(ISD::SETOGE, MVT::f32, Expand);
  setCondCodeAction(ISD::SETOGE, MVT::f64, Expand);
  setCondCodeAction(ISD::SETOLE, MVT::f32, Expand);
  setCondCodeAction(ISD::SETOLE, MVT::f64, Expand);
  setCondCodeAction(ISD::SETONE, MVT::f32, Expand);
  setCondCodeAction(ISD::SETONE, MVT::f64, Expand);

  if (Subtarget.has64BitSupport()) {
    // They also have instructions for converting between i64 and fp.
    setOperationAction(ISD::FP_TO_SINT, MVT::i64, Custom);
    setOperationAction(ISD::FP_TO_UINT, MVT::i64, Expand);
    setOperationAction(ISD::SINT_TO_FP, MVT::i64, Custom);
    setOperationAction(ISD::UINT_TO_FP, MVT::i64, Expand);
    // This is just the low 32 bits of a (signed) fp->i64 conversion.
    // We cannot do this with Promote because i64 is not a legal type.
    setOperationAction(ISD::FP_TO_UINT, MVT::i32, Custom);

    if (Subtarget.hasLFIWAX() || Subtarget.isPPC64())
      setOperationAction(ISD::SINT_TO_FP, MVT::i32, Custom);
  } else {
    // PowerPC does not have FP_TO_UINT on 32-bit implementations.
    if (Subtarget.hasSPE())
      setOperationAction(ISD::FP_TO_UINT, MVT::i32, Legal);
    else
      setOperationAction(ISD::FP_TO_UINT, MVT::i32, Expand);
  }

  // With the instructions enabled under FPCVT, we can do everything.
  if (Subtarget.hasFPCVT()) {
    if (Subtarget.has64BitSupport()) {
      setOperationAction(ISD::FP_TO_SINT, MVT::i64, Custom);
      setOperationAction(ISD::FP_TO_UINT, MVT::i64, Custom);
      setOperationAction(ISD::SINT_TO_FP, MVT::i64, Custom);
      setOperationAction(ISD::UINT_TO_FP, MVT::i64, Custom);
    }

    setOperationAction(ISD::FP_TO_SINT, MVT::i32, Custom);
    setOperationAction(ISD::FP_TO_UINT, MVT::i32, Custom);
    setOperationAction(ISD::SINT_TO_FP, MVT::i32, Custom);
    setOperationAction(ISD::UINT_TO_FP, MVT::i32, Custom);
  }

  if (Subtarget.use64BitRegs()) {
    // 64-bit PowerPC implementations can support i64 types directly
    addRegisterClass(MVT::i64, &PPC::G8RCRegClass);
    // BUILD_PAIR can't be handled natively, and should be expanded to shl/or
    setOperationAction(ISD::BUILD_PAIR, MVT::i64, Expand);
    // 64-bit PowerPC wants to expand i128 shifts itself.
    setOperationAction(ISD::SHL_PARTS, MVT::i64, Custom);
    setOperationAction(ISD::SRA_PARTS, MVT::i64, Custom);
    setOperationAction(ISD::SRL_PARTS, MVT::i64, Custom);
  } else {
    // 32-bit PowerPC wants to expand i64 shifts itself.
    setOperationAction(ISD::SHL_PARTS, MVT::i32, Custom);
    setOperationAction(ISD::SRA_PARTS, MVT::i32, Custom);
    setOperationAction(ISD::SRL_PARTS, MVT::i32, Custom);
  }

  if (Subtarget.hasAltivec()) {
    // First set operation action for all vector types to expand. Then we
    // will selectively turn on ones that can be effectively codegen'd.
    for (MVT VT : MVT::vector_valuetypes()) {
      // add/sub are legal for all supported vector VT's.
      setOperationAction(ISD::ADD, VT, Legal);
      setOperationAction(ISD::SUB, VT, Legal);
      setOperationAction(ISD::ABS, VT, Custom);

      // Vector instructions introduced in P8
      if (Subtarget.hasP8Altivec() && (VT.SimpleTy != MVT::v1i128)) {
        setOperationAction(ISD::CTPOP, VT, Legal);
        setOperationAction(ISD::CTLZ, VT, Legal);
      }
      else {
        setOperationAction(ISD::CTPOP, VT, Expand);
        setOperationAction(ISD::CTLZ, VT, Expand);
      }

      // Vector instructions introduced in P9
      if (Subtarget.hasP9Altivec() && (VT.SimpleTy != MVT::v1i128))
        setOperationAction(ISD::CTTZ, VT, Legal);
      else
        setOperationAction(ISD::CTTZ, VT, Expand);

      // We promote all shuffles to v16i8.
      setOperationAction(ISD::VECTOR_SHUFFLE, VT, Promote);
      AddPromotedToType (ISD::VECTOR_SHUFFLE, VT, MVT::v16i8);

      // We promote all non-typed operations to v4i32.
      setOperationAction(ISD::AND   , VT, Promote);
      AddPromotedToType (ISD::AND   , VT, MVT::v4i32);
      setOperationAction(ISD::OR    , VT, Promote);
      AddPromotedToType (ISD::OR    , VT, MVT::v4i32);
      setOperationAction(ISD::XOR   , VT, Promote);
      AddPromotedToType (ISD::XOR   , VT, MVT::v4i32);
      setOperationAction(ISD::LOAD  , VT, Promote);
      AddPromotedToType (ISD::LOAD  , VT, MVT::v4i32);
      setOperationAction(ISD::SELECT, VT, Promote);
      AddPromotedToType (ISD::SELECT, VT, MVT::v4i32);
      setOperationAction(ISD::VSELECT, VT, Legal);
      setOperationAction(ISD::SELECT_CC, VT, Promote);
      AddPromotedToType (ISD::SELECT_CC, VT, MVT::v4i32);
      setOperationAction(ISD::STORE, VT, Promote);
      AddPromotedToType (ISD::STORE, VT, MVT::v4i32);

      // No other operations are legal.
      setOperationAction(ISD::MUL , VT, Expand);
      setOperationAction(ISD::SDIV, VT, Expand);
      setOperationAction(ISD::SREM, VT, Expand);
      setOperationAction(ISD::UDIV, VT, Expand);
      setOperationAction(ISD::UREM, VT, Expand);
      setOperationAction(ISD::FDIV, VT, Expand);
      setOperationAction(ISD::FREM, VT, Expand);
      setOperationAction(ISD::FNEG, VT, Expand);
      setOperationAction(ISD::FSQRT, VT, Expand);
      setOperationAction(ISD::FLOG, VT, Expand);
      setOperationAction(ISD::FLOG10, VT, Expand);
      setOperationAction(ISD::FLOG2, VT, Expand);
      setOperationAction(ISD::FEXP, VT, Expand);
      setOperationAction(ISD::FEXP2, VT, Expand);
      setOperationAction(ISD::FSIN, VT, Expand);
      setOperationAction(ISD::FCOS, VT, Expand);
      setOperationAction(ISD::FABS, VT, Expand);
      setOperationAction(ISD::FFLOOR, VT, Expand);
      setOperationAction(ISD::FCEIL,  VT, Expand);
      setOperationAction(ISD::FTRUNC, VT, Expand);
      setOperationAction(ISD::FRINT,  VT, Expand);
      setOperationAction(ISD::FNEARBYINT, VT, Expand);
      setOperationAction(ISD::EXTRACT_VECTOR_ELT, VT, Expand);
      setOperationAction(ISD::INSERT_VECTOR_ELT, VT, Expand);
      setOperationAction(ISD::BUILD_VECTOR, VT, Expand);
      setOperationAction(ISD::MULHU, VT, Expand);
      setOperationAction(ISD::MULHS, VT, Expand);
      setOperationAction(ISD::UMUL_LOHI, VT, Expand);
      setOperationAction(ISD::SMUL_LOHI, VT, Expand);
      setOperationAction(ISD::UDIVREM, VT, Expand);
      setOperationAction(ISD::SDIVREM, VT, Expand);
      setOperationAction(ISD::SCALAR_TO_VECTOR, VT, Expand);
      setOperationAction(ISD::FPOW, VT, Expand);
      setOperationAction(ISD::BSWAP, VT, Expand);
      setOperationAction(ISD::SIGN_EXTEND_INREG, VT, Expand);
      setOperationAction(ISD::ROTL, VT, Expand);
      setOperationAction(ISD::ROTR, VT, Expand);

      for (MVT InnerVT : MVT::vector_valuetypes()) {
        setTruncStoreAction(VT, InnerVT, Expand);
        setLoadExtAction(ISD::SEXTLOAD, VT, InnerVT, Expand);
        setLoadExtAction(ISD::ZEXTLOAD, VT, InnerVT, Expand);
        setLoadExtAction(ISD::EXTLOAD, VT, InnerVT, Expand);
      }
    }

    // We can custom expand all VECTOR_SHUFFLEs to VPERM, others we can handle
    // with merges, splats, etc.
    setOperationAction(ISD::VECTOR_SHUFFLE, MVT::v16i8, Custom);

    setOperationAction(ISD::AND   , MVT::v4i32, Legal);
    setOperationAction(ISD::OR    , MVT::v4i32, Legal);
    setOperationAction(ISD::XOR   , MVT::v4i32, Legal);
    setOperationAction(ISD::LOAD  , MVT::v4i32, Legal);
    setOperationAction(ISD::SELECT, MVT::v4i32,
                       Subtarget.useCRBits() ? Legal : Expand);
    setOperationAction(ISD::STORE , MVT::v4i32, Legal);
    setOperationAction(ISD::FP_TO_SINT, MVT::v4i32, Legal);
    setOperationAction(ISD::FP_TO_UINT, MVT::v4i32, Legal);
    setOperationAction(ISD::SINT_TO_FP, MVT::v4i32, Legal);
    setOperationAction(ISD::UINT_TO_FP, MVT::v4i32, Legal);
    setOperationAction(ISD::FFLOOR, MVT::v4f32, Legal);
    setOperationAction(ISD::FCEIL, MVT::v4f32, Legal);
    setOperationAction(ISD::FTRUNC, MVT::v4f32, Legal);
    setOperationAction(ISD::FNEARBYINT, MVT::v4f32, Legal);

    // Without hasP8Altivec set, v2i64 SMAX isn't available.
    // But ABS custom lowering requires SMAX support.
    if (!Subtarget.hasP8Altivec())
      setOperationAction(ISD::ABS, MVT::v2i64, Expand);

    addRegisterClass(MVT::v4f32, &PPC::VRRCRegClass);
    addRegisterClass(MVT::v4i32, &PPC::VRRCRegClass);
    addRegisterClass(MVT::v8i16, &PPC::VRRCRegClass);
    addRegisterClass(MVT::v16i8, &PPC::VRRCRegClass);

    setOperationAction(ISD::MUL, MVT::v4f32, Legal);
    setOperationAction(ISD::FMA, MVT::v4f32, Legal);

    if (TM.Options.UnsafeFPMath || Subtarget.hasVSX()) {
      setOperationAction(ISD::FDIV, MVT::v4f32, Legal);
      setOperationAction(ISD::FSQRT, MVT::v4f32, Legal);
    }

    if (Subtarget.hasP8Altivec())
      setOperationAction(ISD::MUL, MVT::v4i32, Legal);
    else
      setOperationAction(ISD::MUL, MVT::v4i32, Custom);

    setOperationAction(ISD::MUL, MVT::v8i16, Custom);
    setOperationAction(ISD::MUL, MVT::v16i8, Custom);

    setOperationAction(ISD::SCALAR_TO_VECTOR, MVT::v4f32, Custom);
    setOperationAction(ISD::SCALAR_TO_VECTOR, MVT::v4i32, Custom);

    setOperationAction(ISD::BUILD_VECTOR, MVT::v16i8, Custom);
    setOperationAction(ISD::BUILD_VECTOR, MVT::v8i16, Custom);
    setOperationAction(ISD::BUILD_VECTOR, MVT::v4i32, Custom);
    setOperationAction(ISD::BUILD_VECTOR, MVT::v4f32, Custom);

    // Altivec does not contain unordered floating-point compare instructions
    setCondCodeAction(ISD::SETUO, MVT::v4f32, Expand);
    setCondCodeAction(ISD::SETUEQ, MVT::v4f32, Expand);
    setCondCodeAction(ISD::SETO,   MVT::v4f32, Expand);
    setCondCodeAction(ISD::SETONE, MVT::v4f32, Expand);

    if (Subtarget.hasVSX()) {
      setOperationAction(ISD::SCALAR_TO_VECTOR, MVT::v2f64, Legal);
      setOperationAction(ISD::EXTRACT_VECTOR_ELT, MVT::v2f64, Legal);
      if (Subtarget.hasP8Vector()) {
        setOperationAction(ISD::SCALAR_TO_VECTOR, MVT::v4f32, Legal);
        setOperationAction(ISD::EXTRACT_VECTOR_ELT, MVT::v4f32, Legal);
      }
      if (Subtarget.hasDirectMove() && isPPC64) {
        setOperationAction(ISD::SCALAR_TO_VECTOR, MVT::v16i8, Legal);
        setOperationAction(ISD::SCALAR_TO_VECTOR, MVT::v8i16, Legal);
        setOperationAction(ISD::SCALAR_TO_VECTOR, MVT::v4i32, Legal);
        setOperationAction(ISD::SCALAR_TO_VECTOR, MVT::v2i64, Legal);
        setOperationAction(ISD::EXTRACT_VECTOR_ELT, MVT::v16i8, Legal);
        setOperationAction(ISD::EXTRACT_VECTOR_ELT, MVT::v8i16, Legal);
        setOperationAction(ISD::EXTRACT_VECTOR_ELT, MVT::v4i32, Legal);
        setOperationAction(ISD::EXTRACT_VECTOR_ELT, MVT::v2i64, Legal);
      }
      setOperationAction(ISD::EXTRACT_VECTOR_ELT, MVT::v2f64, Legal);

      setOperationAction(ISD::FFLOOR, MVT::v2f64, Legal);
      setOperationAction(ISD::FCEIL, MVT::v2f64, Legal);
      setOperationAction(ISD::FTRUNC, MVT::v2f64, Legal);
      setOperationAction(ISD::FNEARBYINT, MVT::v2f64, Legal);
      setOperationAction(ISD::FROUND, MVT::v2f64, Legal);

      setOperationAction(ISD::FROUND, MVT::v4f32, Legal);

      setOperationAction(ISD::MUL, MVT::v2f64, Legal);
      setOperationAction(ISD::FMA, MVT::v2f64, Legal);

      setOperationAction(ISD::FDIV, MVT::v2f64, Legal);
      setOperationAction(ISD::FSQRT, MVT::v2f64, Legal);

      // Share the Altivec comparison restrictions.
      setCondCodeAction(ISD::SETUO, MVT::v2f64, Expand);
      setCondCodeAction(ISD::SETUEQ, MVT::v2f64, Expand);
      setCondCodeAction(ISD::SETO,   MVT::v2f64, Expand);
      setCondCodeAction(ISD::SETONE, MVT::v2f64, Expand);

      setOperationAction(ISD::LOAD, MVT::v2f64, Legal);
      setOperationAction(ISD::STORE, MVT::v2f64, Legal);

      setOperationAction(ISD::VECTOR_SHUFFLE, MVT::v2f64, Legal);

      if (Subtarget.hasP8Vector())
        addRegisterClass(MVT::f32, &PPC::VSSRCRegClass);

      addRegisterClass(MVT::f64, &PPC::VSFRCRegClass);

      addRegisterClass(MVT::v4i32, &PPC::VSRCRegClass);
      addRegisterClass(MVT::v4f32, &PPC::VSRCRegClass);
      addRegisterClass(MVT::v2f64, &PPC::VSRCRegClass);

      if (Subtarget.hasP8Altivec()) {
        setOperationAction(ISD::SHL, MVT::v2i64, Legal);
        setOperationAction(ISD::SRA, MVT::v2i64, Legal);
        setOperationAction(ISD::SRL, MVT::v2i64, Legal);

        // 128 bit shifts can be accomplished via 3 instructions for SHL and
        // SRL, but not for SRA because of the instructions available:
        // VS{RL} and VS{RL}O. However due to direct move costs, it's not worth
        // doing
        setOperationAction(ISD::SHL, MVT::v1i128, Expand);
        setOperationAction(ISD::SRL, MVT::v1i128, Expand);
        setOperationAction(ISD::SRA, MVT::v1i128, Expand);

        setOperationAction(ISD::SETCC, MVT::v2i64, Legal);
      }
      else {
        setOperationAction(ISD::SHL, MVT::v2i64, Expand);
        setOperationAction(ISD::SRA, MVT::v2i64, Expand);
        setOperationAction(ISD::SRL, MVT::v2i64, Expand);

        setOperationAction(ISD::SETCC, MVT::v2i64, Custom);

        // VSX v2i64 only supports non-arithmetic operations.
        setOperationAction(ISD::ADD, MVT::v2i64, Expand);
        setOperationAction(ISD::SUB, MVT::v2i64, Expand);
      }

      setOperationAction(ISD::LOAD, MVT::v2i64, Promote);
      AddPromotedToType (ISD::LOAD, MVT::v2i64, MVT::v2f64);
      setOperationAction(ISD::STORE, MVT::v2i64, Promote);
      AddPromotedToType (ISD::STORE, MVT::v2i64, MVT::v2f64);

      setOperationAction(ISD::VECTOR_SHUFFLE, MVT::v2i64, Legal);

      setOperationAction(ISD::SINT_TO_FP, MVT::v2i64, Legal);
      setOperationAction(ISD::UINT_TO_FP, MVT::v2i64, Legal);
      setOperationAction(ISD::FP_TO_SINT, MVT::v2i64, Legal);
      setOperationAction(ISD::FP_TO_UINT, MVT::v2i64, Legal);

      // Custom handling for partial vectors of integers converted to
      // floating point. We already have optimal handling for v2i32 through
      // the DAG combine, so those aren't necessary.
      setOperationAction(ISD::UINT_TO_FP, MVT::v2i8, Custom);
      setOperationAction(ISD::UINT_TO_FP, MVT::v4i8, Custom);
      setOperationAction(ISD::UINT_TO_FP, MVT::v2i16, Custom);
      setOperationAction(ISD::UINT_TO_FP, MVT::v4i16, Custom);
      setOperationAction(ISD::SINT_TO_FP, MVT::v2i8, Custom);
      setOperationAction(ISD::SINT_TO_FP, MVT::v4i8, Custom);
      setOperationAction(ISD::SINT_TO_FP, MVT::v2i16, Custom);
      setOperationAction(ISD::SINT_TO_FP, MVT::v4i16, Custom);

      setOperationAction(ISD::FNEG, MVT::v4f32, Legal);
      setOperationAction(ISD::FNEG, MVT::v2f64, Legal);
      setOperationAction(ISD::FABS, MVT::v4f32, Legal);
      setOperationAction(ISD::FABS, MVT::v2f64, Legal);

      if (Subtarget.hasDirectMove())
        setOperationAction(ISD::BUILD_VECTOR, MVT::v2i64, Custom);
      setOperationAction(ISD::BUILD_VECTOR, MVT::v2f64, Custom);

      addRegisterClass(MVT::v2i64, &PPC::VSRCRegClass);
    }

    if (Subtarget.hasP8Altivec()) {
      addRegisterClass(MVT::v2i64, &PPC::VRRCRegClass);
      addRegisterClass(MVT::v1i128, &PPC::VRRCRegClass);
    }

    if (Subtarget.hasP9Vector()) {
      setOperationAction(ISD::INSERT_VECTOR_ELT, MVT::v4i32, Custom);
      setOperationAction(ISD::INSERT_VECTOR_ELT, MVT::v4f32, Custom);

      // 128 bit shifts can be accomplished via 3 instructions for SHL and
      // SRL, but not for SRA because of the instructions available:
      // VS{RL} and VS{RL}O.
      setOperationAction(ISD::SHL, MVT::v1i128, Legal);
      setOperationAction(ISD::SRL, MVT::v1i128, Legal);
      setOperationAction(ISD::SRA, MVT::v1i128, Expand);

      if (EnableQuadPrecision) {
        addRegisterClass(MVT::f128, &PPC::VRRCRegClass);
        setOperationAction(ISD::FADD, MVT::f128, Legal);
        setOperationAction(ISD::FSUB, MVT::f128, Legal);
        setOperationAction(ISD::FDIV, MVT::f128, Legal);
        setOperationAction(ISD::FMUL, MVT::f128, Legal);
        setOperationAction(ISD::FP_EXTEND, MVT::f128, Legal);
        // No extending loads to f128 on PPC.
        for (MVT FPT : MVT::fp_valuetypes())
          setLoadExtAction(ISD::EXTLOAD, MVT::f128, FPT, Expand);
        setOperationAction(ISD::FMA, MVT::f128, Legal);
        setCondCodeAction(ISD::SETULT, MVT::f128, Expand);
        setCondCodeAction(ISD::SETUGT, MVT::f128, Expand);
        setCondCodeAction(ISD::SETUEQ, MVT::f128, Expand);
        setCondCodeAction(ISD::SETOGE, MVT::f128, Expand);
        setCondCodeAction(ISD::SETOLE, MVT::f128, Expand);
        setCondCodeAction(ISD::SETONE, MVT::f128, Expand);

        setOperationAction(ISD::FTRUNC, MVT::f128, Legal);
        setOperationAction(ISD::FRINT, MVT::f128, Legal);
        setOperationAction(ISD::FFLOOR, MVT::f128, Legal);
        setOperationAction(ISD::FCEIL, MVT::f128, Legal);
        setOperationAction(ISD::FNEARBYINT, MVT::f128, Legal);
        setOperationAction(ISD::FROUND, MVT::f128, Legal);

        setOperationAction(ISD::SELECT, MVT::f128, Expand);
        setOperationAction(ISD::FP_ROUND, MVT::f64, Legal);
        setOperationAction(ISD::FP_ROUND, MVT::f32, Legal);
        setTruncStoreAction(MVT::f128, MVT::f64, Expand);
        setTruncStoreAction(MVT::f128, MVT::f32, Expand);
        setOperationAction(ISD::BITCAST, MVT::i128, Custom);
        // No implementation for these ops for PowerPC.
        setOperationAction(ISD::FSIN , MVT::f128, Expand);
        setOperationAction(ISD::FCOS , MVT::f128, Expand);
        setOperationAction(ISD::FPOW, MVT::f128, Expand);
        setOperationAction(ISD::FPOWI, MVT::f128, Expand);
        setOperationAction(ISD::FREM, MVT::f128, Expand);
      }

    }

    if (Subtarget.hasP9Altivec()) {
      setOperationAction(ISD::INSERT_VECTOR_ELT, MVT::v8i16, Custom);
      setOperationAction(ISD::INSERT_VECTOR_ELT, MVT::v16i8, Custom);
    }
  }

  if (Subtarget.hasQPX()) {
    setOperationAction(ISD::FADD, MVT::v4f64, Legal);
    setOperationAction(ISD::FSUB, MVT::v4f64, Legal);
    setOperationAction(ISD::FMUL, MVT::v4f64, Legal);
    setOperationAction(ISD::FREM, MVT::v4f64, Expand);

    setOperationAction(ISD::FCOPYSIGN, MVT::v4f64, Legal);
    setOperationAction(ISD::FGETSIGN, MVT::v4f64, Expand);

    setOperationAction(ISD::LOAD  , MVT::v4f64, Custom);
    setOperationAction(ISD::STORE , MVT::v4f64, Custom);

    setTruncStoreAction(MVT::v4f64, MVT::v4f32, Custom);
    setLoadExtAction(ISD::EXTLOAD, MVT::v4f64, MVT::v4f32, Custom);

    if (!Subtarget.useCRBits())
      setOperationAction(ISD::SELECT, MVT::v4f64, Expand);
    setOperationAction(ISD::VSELECT, MVT::v4f64, Legal);

    setOperationAction(ISD::EXTRACT_VECTOR_ELT , MVT::v4f64, Legal);
    setOperationAction(ISD::INSERT_VECTOR_ELT , MVT::v4f64, Expand);
    setOperationAction(ISD::CONCAT_VECTORS , MVT::v4f64, Expand);
    setOperationAction(ISD::EXTRACT_SUBVECTOR , MVT::v4f64, Expand);
    setOperationAction(ISD::VECTOR_SHUFFLE , MVT::v4f64, Custom);
    setOperationAction(ISD::SCALAR_TO_VECTOR, MVT::v4f64, Legal);
    setOperationAction(ISD::BUILD_VECTOR, MVT::v4f64, Custom);

    setOperationAction(ISD::FP_TO_SINT , MVT::v4f64, Legal);
    setOperationAction(ISD::FP_TO_UINT , MVT::v4f64, Expand);

    setOperationAction(ISD::FP_ROUND , MVT::v4f32, Legal);
    setOperationAction(ISD::FP_ROUND_INREG , MVT::v4f32, Expand);
    setOperationAction(ISD::FP_EXTEND, MVT::v4f64, Legal);

    setOperationAction(ISD::FNEG , MVT::v4f64, Legal);
    setOperationAction(ISD::FABS , MVT::v4f64, Legal);
    setOperationAction(ISD::FSIN , MVT::v4f64, Expand);
    setOperationAction(ISD::FCOS , MVT::v4f64, Expand);
    setOperationAction(ISD::FPOW , MVT::v4f64, Expand);
    setOperationAction(ISD::FLOG , MVT::v4f64, Expand);
    setOperationAction(ISD::FLOG2 , MVT::v4f64, Expand);
    setOperationAction(ISD::FLOG10 , MVT::v4f64, Expand);
    setOperationAction(ISD::FEXP , MVT::v4f64, Expand);
    setOperationAction(ISD::FEXP2 , MVT::v4f64, Expand);

    setOperationAction(ISD::FMINNUM, MVT::v4f64, Legal);
    setOperationAction(ISD::FMAXNUM, MVT::v4f64, Legal);

    setIndexedLoadAction(ISD::PRE_INC, MVT::v4f64, Legal);
    setIndexedStoreAction(ISD::PRE_INC, MVT::v4f64, Legal);

    addRegisterClass(MVT::v4f64, &PPC::QFRCRegClass);

    setOperationAction(ISD::FADD, MVT::v4f32, Legal);
    setOperationAction(ISD::FSUB, MVT::v4f32, Legal);
    setOperationAction(ISD::FMUL, MVT::v4f32, Legal);
    setOperationAction(ISD::FREM, MVT::v4f32, Expand);

    setOperationAction(ISD::FCOPYSIGN, MVT::v4f32, Legal);
    setOperationAction(ISD::FGETSIGN, MVT::v4f32, Expand);

    setOperationAction(ISD::LOAD  , MVT::v4f32, Custom);
    setOperationAction(ISD::STORE , MVT::v4f32, Custom);

    if (!Subtarget.useCRBits())
      setOperationAction(ISD::SELECT, MVT::v4f32, Expand);
    setOperationAction(ISD::VSELECT, MVT::v4f32, Legal);

    setOperationAction(ISD::EXTRACT_VECTOR_ELT , MVT::v4f32, Legal);
    setOperationAction(ISD::INSERT_VECTOR_ELT , MVT::v4f32, Expand);
    setOperationAction(ISD::CONCAT_VECTORS , MVT::v4f32, Expand);
    setOperationAction(ISD::EXTRACT_SUBVECTOR , MVT::v4f32, Expand);
    setOperationAction(ISD::VECTOR_SHUFFLE , MVT::v4f32, Custom);
    setOperationAction(ISD::SCALAR_TO_VECTOR, MVT::v4f32, Legal);
    setOperationAction(ISD::BUILD_VECTOR, MVT::v4f32, Custom);

    setOperationAction(ISD::FP_TO_SINT , MVT::v4f32, Legal);
    setOperationAction(ISD::FP_TO_UINT , MVT::v4f32, Expand);

    setOperationAction(ISD::FNEG , MVT::v4f32, Legal);
    setOperationAction(ISD::FABS , MVT::v4f32, Legal);
    setOperationAction(ISD::FSIN , MVT::v4f32, Expand);
    setOperationAction(ISD::FCOS , MVT::v4f32, Expand);
    setOperationAction(ISD::FPOW , MVT::v4f32, Expand);
    setOperationAction(ISD::FLOG , MVT::v4f32, Expand);
    setOperationAction(ISD::FLOG2 , MVT::v4f32, Expand);
    setOperationAction(ISD::FLOG10 , MVT::v4f32, Expand);
    setOperationAction(ISD::FEXP , MVT::v4f32, Expand);
    setOperationAction(ISD::FEXP2 , MVT::v4f32, Expand);

    setOperationAction(ISD::FMINNUM, MVT::v4f32, Legal);
    setOperationAction(ISD::FMAXNUM, MVT::v4f32, Legal);

    setIndexedLoadAction(ISD::PRE_INC, MVT::v4f32, Legal);
    setIndexedStoreAction(ISD::PRE_INC, MVT::v4f32, Legal);

    addRegisterClass(MVT::v4f32, &PPC::QSRCRegClass);

    setOperationAction(ISD::AND , MVT::v4i1, Legal);
    setOperationAction(ISD::OR , MVT::v4i1, Legal);
    setOperationAction(ISD::XOR , MVT::v4i1, Legal);

    if (!Subtarget.useCRBits())
      setOperationAction(ISD::SELECT, MVT::v4i1, Expand);
    setOperationAction(ISD::VSELECT, MVT::v4i1, Legal);

    setOperationAction(ISD::LOAD  , MVT::v4i1, Custom);
    setOperationAction(ISD::STORE , MVT::v4i1, Custom);

    setOperationAction(ISD::EXTRACT_VECTOR_ELT , MVT::v4i1, Custom);
    setOperationAction(ISD::INSERT_VECTOR_ELT , MVT::v4i1, Expand);
    setOperationAction(ISD::CONCAT_VECTORS , MVT::v4i1, Expand);
    setOperationAction(ISD::EXTRACT_SUBVECTOR , MVT::v4i1, Expand);
    setOperationAction(ISD::VECTOR_SHUFFLE , MVT::v4i1, Custom);
    setOperationAction(ISD::SCALAR_TO_VECTOR, MVT::v4i1, Expand);
    setOperationAction(ISD::BUILD_VECTOR, MVT::v4i1, Custom);

    setOperationAction(ISD::SINT_TO_FP, MVT::v4i1, Custom);
    setOperationAction(ISD::UINT_TO_FP, MVT::v4i1, Custom);

    addRegisterClass(MVT::v4i1, &PPC::QBRCRegClass);

    setOperationAction(ISD::FFLOOR, MVT::v4f64, Legal);
    setOperationAction(ISD::FCEIL,  MVT::v4f64, Legal);
    setOperationAction(ISD::FTRUNC, MVT::v4f64, Legal);
    setOperationAction(ISD::FROUND, MVT::v4f64, Legal);

    setOperationAction(ISD::FFLOOR, MVT::v4f32, Legal);
    setOperationAction(ISD::FCEIL,  MVT::v4f32, Legal);
    setOperationAction(ISD::FTRUNC, MVT::v4f32, Legal);
    setOperationAction(ISD::FROUND, MVT::v4f32, Legal);

    setOperationAction(ISD::FNEARBYINT, MVT::v4f64, Expand);
    setOperationAction(ISD::FNEARBYINT, MVT::v4f32, Expand);

    // These need to set FE_INEXACT, and so cannot be vectorized here.
    setOperationAction(ISD::FRINT, MVT::v4f64, Expand);
    setOperationAction(ISD::FRINT, MVT::v4f32, Expand);

    if (TM.Options.UnsafeFPMath) {
      setOperationAction(ISD::FDIV, MVT::v4f64, Legal);
      setOperationAction(ISD::FSQRT, MVT::v4f64, Legal);

      setOperationAction(ISD::FDIV, MVT::v4f32, Legal);
      setOperationAction(ISD::FSQRT, MVT::v4f32, Legal);
    } else {
      setOperationAction(ISD::FDIV, MVT::v4f64, Expand);
      setOperationAction(ISD::FSQRT, MVT::v4f64, Expand);

      setOperationAction(ISD::FDIV, MVT::v4f32, Expand);
      setOperationAction(ISD::FSQRT, MVT::v4f32, Expand);
    }
  }

  if (Subtarget.has64BitSupport())
    setOperationAction(ISD::PREFETCH, MVT::Other, Legal);

  setOperationAction(ISD::READCYCLECOUNTER, MVT::i64, isPPC64 ? Legal : Custom);

  if (!isPPC64) {
    setOperationAction(ISD::ATOMIC_LOAD,  MVT::i64, Expand);
    setOperationAction(ISD::ATOMIC_STORE, MVT::i64, Expand);
  }

  setBooleanContents(ZeroOrOneBooleanContent);

  if (Subtarget.hasAltivec()) {
    // Altivec instructions set fields to all zeros or all ones.
    setBooleanVectorContents(ZeroOrNegativeOneBooleanContent);
  }

  if (!isPPC64) {
    // These libcalls are not available in 32-bit.
    setLibcallName(RTLIB::SHL_I128, nullptr);
    setLibcallName(RTLIB::SRL_I128, nullptr);
    setLibcallName(RTLIB::SRA_I128, nullptr);
  }

  setStackPointerRegisterToSaveRestore(isPPC64 ? PPC::X1 : PPC::R1);

  // We have target-specific dag combine patterns for the following nodes:
  setTargetDAGCombine(ISD::ADD);
  setTargetDAGCombine(ISD::SHL);
  setTargetDAGCombine(ISD::SRA);
  setTargetDAGCombine(ISD::SRL);
  setTargetDAGCombine(ISD::SINT_TO_FP);
  setTargetDAGCombine(ISD::BUILD_VECTOR);
  if (Subtarget.hasFPCVT())
    setTargetDAGCombine(ISD::UINT_TO_FP);
  setTargetDAGCombine(ISD::LOAD);
  setTargetDAGCombine(ISD::STORE);
  setTargetDAGCombine(ISD::BR_CC);
  if (Subtarget.useCRBits())
    setTargetDAGCombine(ISD::BRCOND);
  setTargetDAGCombine(ISD::BSWAP);
  setTargetDAGCombine(ISD::INTRINSIC_WO_CHAIN);
  setTargetDAGCombine(ISD::INTRINSIC_W_CHAIN);
  setTargetDAGCombine(ISD::INTRINSIC_VOID);

  setTargetDAGCombine(ISD::SIGN_EXTEND);
  setTargetDAGCombine(ISD::ZERO_EXTEND);
  setTargetDAGCombine(ISD::ANY_EXTEND);

  setTargetDAGCombine(ISD::TRUNCATE);

  if (Subtarget.useCRBits()) {
    setTargetDAGCombine(ISD::TRUNCATE);
    setTargetDAGCombine(ISD::SETCC);
    setTargetDAGCombine(ISD::SELECT_CC);
  }

  // Use reciprocal estimates.
  if (TM.Options.UnsafeFPMath) {
    setTargetDAGCombine(ISD::FDIV);
    setTargetDAGCombine(ISD::FSQRT);
  }

  if (Subtarget.hasP9Altivec()) {
    setTargetDAGCombine(ISD::ABS);
    setTargetDAGCombine(ISD::VSELECT);
  }

  // Darwin long double math library functions have $LDBL128 appended.
  if (Subtarget.isDarwin()) {
    setLibcallName(RTLIB::COS_PPCF128, "cosl$LDBL128");
    setLibcallName(RTLIB::POW_PPCF128, "powl$LDBL128");
    setLibcallName(RTLIB::REM_PPCF128, "fmodl$LDBL128");
    setLibcallName(RTLIB::SIN_PPCF128, "sinl$LDBL128");
    setLibcallName(RTLIB::SQRT_PPCF128, "sqrtl$LDBL128");
    setLibcallName(RTLIB::LOG_PPCF128, "logl$LDBL128");
    setLibcallName(RTLIB::LOG2_PPCF128, "log2l$LDBL128");
    setLibcallName(RTLIB::LOG10_PPCF128, "log10l$LDBL128");
    setLibcallName(RTLIB::EXP_PPCF128, "expl$LDBL128");
    setLibcallName(RTLIB::EXP2_PPCF128, "exp2l$LDBL128");
  }

  if (EnableQuadPrecision) {
    setLibcallName(RTLIB::LOG_F128, "logf128");
    setLibcallName(RTLIB::LOG2_F128, "log2f128");
    setLibcallName(RTLIB::LOG10_F128, "log10f128");
    setLibcallName(RTLIB::EXP_F128, "expf128");
    setLibcallName(RTLIB::EXP2_F128, "exp2f128");
    setLibcallName(RTLIB::SIN_F128, "sinf128");
    setLibcallName(RTLIB::COS_F128, "cosf128");
    setLibcallName(RTLIB::POW_F128, "powf128");
    setLibcallName(RTLIB::FMIN_F128, "fminf128");
    setLibcallName(RTLIB::FMAX_F128, "fmaxf128");
    setLibcallName(RTLIB::POWI_F128, "__powikf2");
    setLibcallName(RTLIB::REM_F128, "fmodf128");
  }

  // With 32 condition bits, we don't need to sink (and duplicate) compares
  // aggressively in CodeGenPrep.
  if (Subtarget.useCRBits()) {
    setHasMultipleConditionRegisters();
    setJumpIsExpensive();
  }

  setMinFunctionAlignment(2);
  if (Subtarget.isDarwin())
    setPrefFunctionAlignment(4);

  switch (Subtarget.getDarwinDirective()) {
  default: break;
  case PPC::DIR_970:
  case PPC::DIR_A2:
  case PPC::DIR_E500:
  case PPC::DIR_E500mc:
  case PPC::DIR_E5500:
  case PPC::DIR_PWR4:
  case PPC::DIR_PWR5:
  case PPC::DIR_PWR5X:
  case PPC::DIR_PWR6:
  case PPC::DIR_PWR6X:
  case PPC::DIR_PWR7:
  case PPC::DIR_PWR8:
  case PPC::DIR_PWR9:
    setPrefFunctionAlignment(4);
    setPrefLoopAlignment(4);
    break;
  }

  if (Subtarget.enableMachineScheduler())
    setSchedulingPreference(Sched::Source);
  else
    setSchedulingPreference(Sched::Hybrid);

  computeRegisterProperties(STI.getRegisterInfo());

  // The Freescale cores do better with aggressive inlining of memcpy and
  // friends. GCC uses same threshold of 128 bytes (= 32 word stores).
  if (Subtarget.getDarwinDirective() == PPC::DIR_E500mc ||
      Subtarget.getDarwinDirective() == PPC::DIR_E5500) {
    MaxStoresPerMemset = 32;
    MaxStoresPerMemsetOptSize = 16;
    MaxStoresPerMemcpy = 32;
    MaxStoresPerMemcpyOptSize = 8;
    MaxStoresPerMemmove = 32;
    MaxStoresPerMemmoveOptSize = 8;
  } else if (Subtarget.getDarwinDirective() == PPC::DIR_A2) {
    // The A2 also benefits from (very) aggressive inlining of memcpy and
    // friends. The overhead of a the function call, even when warm, can be
    // over one hundred cycles.
    MaxStoresPerMemset = 128;
    MaxStoresPerMemcpy = 128;
    MaxStoresPerMemmove = 128;
    MaxLoadsPerMemcmp = 128;
  } else {
    MaxLoadsPerMemcmp = 8;
    MaxLoadsPerMemcmpOptSize = 4;
  }
}

/// getMaxByValAlign - Helper for getByValTypeAlignment to determine
/// the desired ByVal argument alignment.
static void getMaxByValAlign(Type *Ty, unsigned &MaxAlign,
                             unsigned MaxMaxAlign) {
  if (MaxAlign == MaxMaxAlign)
    return;
  if (VectorType *VTy = dyn_cast<VectorType>(Ty)) {
    if (MaxMaxAlign >= 32 && VTy->getBitWidth() >= 256)
      MaxAlign = 32;
    else if (VTy->getBitWidth() >= 128 && MaxAlign < 16)
      MaxAlign = 16;
  } else if (ArrayType *ATy = dyn_cast<ArrayType>(Ty)) {
    unsigned EltAlign = 0;
    getMaxByValAlign(ATy->getElementType(), EltAlign, MaxMaxAlign);
    if (EltAlign > MaxAlign)
      MaxAlign = EltAlign;
  } else if (StructType *STy = dyn_cast<StructType>(Ty)) {
    for (auto *EltTy : STy->elements()) {
      unsigned EltAlign = 0;
      getMaxByValAlign(EltTy, EltAlign, MaxMaxAlign);
      if (EltAlign > MaxAlign)
        MaxAlign = EltAlign;
      if (MaxAlign == MaxMaxAlign)
        break;
    }
  }
}

/// getByValTypeAlignment - Return the desired alignment for ByVal aggregate
/// function arguments in the caller parameter area.
unsigned PPCTargetLowering::getByValTypeAlignment(Type *Ty,
                                                  const DataLayout &DL) const {
  // Darwin passes everything on 4 byte boundary.
  if (Subtarget.isDarwin())
    return 4;

  // 16byte and wider vectors are passed on 16byte boundary.
  // The rest is 8 on PPC64 and 4 on PPC32 boundary.
  unsigned Align = Subtarget.isPPC64() ? 8 : 4;
  if (Subtarget.hasAltivec() || Subtarget.hasQPX())
    getMaxByValAlign(Ty, Align, Subtarget.hasQPX() ? 32 : 16);
  return Align;
}

unsigned PPCTargetLowering::getNumRegistersForCallingConv(LLVMContext &Context,
                                                          CallingConv:: ID CC,
                                                          EVT VT) const {
  if (Subtarget.hasSPE() && VT == MVT::f64)
    return 2;
  return PPCTargetLowering::getNumRegisters(Context, VT);
}

MVT PPCTargetLowering::getRegisterTypeForCallingConv(LLVMContext &Context,
                                                     CallingConv:: ID CC,
                                                     EVT VT) const {
  if (Subtarget.hasSPE() && VT == MVT::f64)
    return MVT::i32;
  return PPCTargetLowering::getRegisterType(Context, VT);
}

bool PPCTargetLowering::useSoftFloat() const {
  return Subtarget.useSoftFloat();
}

bool PPCTargetLowering::hasSPE() const {
  return Subtarget.hasSPE();
}

const char *PPCTargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch ((PPCISD::NodeType)Opcode) {
  case PPCISD::FIRST_NUMBER:    break;
  case PPCISD::FSEL:            return "PPCISD::FSEL";
  case PPCISD::FCFID:           return "PPCISD::FCFID";
  case PPCISD::FCFIDU:          return "PPCISD::FCFIDU";
  case PPCISD::FCFIDS:          return "PPCISD::FCFIDS";
  case PPCISD::FCFIDUS:         return "PPCISD::FCFIDUS";
  case PPCISD::FCTIDZ:          return "PPCISD::FCTIDZ";
  case PPCISD::FCTIWZ:          return "PPCISD::FCTIWZ";
  case PPCISD::FCTIDUZ:         return "PPCISD::FCTIDUZ";
  case PPCISD::FCTIWUZ:         return "PPCISD::FCTIWUZ";
  case PPCISD::FP_TO_UINT_IN_VSR:
                                return "PPCISD::FP_TO_UINT_IN_VSR,";
  case PPCISD::FP_TO_SINT_IN_VSR:
                                return "PPCISD::FP_TO_SINT_IN_VSR";
  case PPCISD::FRE:             return "PPCISD::FRE";
  case PPCISD::FRSQRTE:         return "PPCISD::FRSQRTE";
  case PPCISD::STFIWX:          return "PPCISD::STFIWX";
  case PPCISD::VMADDFP:         return "PPCISD::VMADDFP";
  case PPCISD::VNMSUBFP:        return "PPCISD::VNMSUBFP";
  case PPCISD::VPERM:           return "PPCISD::VPERM";
  case PPCISD::XXSPLT:          return "PPCISD::XXSPLT";
  case PPCISD::VECINSERT:       return "PPCISD::VECINSERT";
  case PPCISD::XXREVERSE:       return "PPCISD::XXREVERSE";
  case PPCISD::XXPERMDI:        return "PPCISD::XXPERMDI";
  case PPCISD::VECSHL:          return "PPCISD::VECSHL";
  case PPCISD::CMPB:            return "PPCISD::CMPB";
  case PPCISD::Hi:              return "PPCISD::Hi";
  case PPCISD::Lo:              return "PPCISD::Lo";
  case PPCISD::TOC_ENTRY:       return "PPCISD::TOC_ENTRY";
  case PPCISD::ATOMIC_CMP_SWAP_8: return "PPCISD::ATOMIC_CMP_SWAP_8";
  case PPCISD::ATOMIC_CMP_SWAP_16: return "PPCISD::ATOMIC_CMP_SWAP_16";
  case PPCISD::DYNALLOC:        return "PPCISD::DYNALLOC";
  case PPCISD::DYNAREAOFFSET:   return "PPCISD::DYNAREAOFFSET";
  case PPCISD::GlobalBaseReg:   return "PPCISD::GlobalBaseReg";
  case PPCISD::SRL:             return "PPCISD::SRL";
  case PPCISD::SRA:             return "PPCISD::SRA";
  case PPCISD::SHL:             return "PPCISD::SHL";
  case PPCISD::SRA_ADDZE:       return "PPCISD::SRA_ADDZE";
  case PPCISD::CALL:            return "PPCISD::CALL";
  case PPCISD::CALL_NOP:        return "PPCISD::CALL_NOP";
  case PPCISD::MTCTR:           return "PPCISD::MTCTR";
  case PPCISD::BCTRL:           return "PPCISD::BCTRL";
  case PPCISD::BCTRL_LOAD_TOC:  return "PPCISD::BCTRL_LOAD_TOC";
  case PPCISD::RET_FLAG:        return "PPCISD::RET_FLAG";
  case PPCISD::READ_TIME_BASE:  return "PPCISD::READ_TIME_BASE";
  case PPCISD::EH_SJLJ_SETJMP:  return "PPCISD::EH_SJLJ_SETJMP";
  case PPCISD::EH_SJLJ_LONGJMP: return "PPCISD::EH_SJLJ_LONGJMP";
  case PPCISD::MFOCRF:          return "PPCISD::MFOCRF";
  case PPCISD::MFVSR:           return "PPCISD::MFVSR";
  case PPCISD::MTVSRA:          return "PPCISD::MTVSRA";
  case PPCISD::MTVSRZ:          return "PPCISD::MTVSRZ";
  case PPCISD::SINT_VEC_TO_FP:  return "PPCISD::SINT_VEC_TO_FP";
  case PPCISD::UINT_VEC_TO_FP:  return "PPCISD::UINT_VEC_TO_FP";
  case PPCISD::ANDIo_1_EQ_BIT:  return "PPCISD::ANDIo_1_EQ_BIT";
  case PPCISD::ANDIo_1_GT_BIT:  return "PPCISD::ANDIo_1_GT_BIT";
  case PPCISD::VCMP:            return "PPCISD::VCMP";
  case PPCISD::VCMPo:           return "PPCISD::VCMPo";
  case PPCISD::LBRX:            return "PPCISD::LBRX";
  case PPCISD::STBRX:           return "PPCISD::STBRX";
  case PPCISD::LFIWAX:          return "PPCISD::LFIWAX";
  case PPCISD::LFIWZX:          return "PPCISD::LFIWZX";
  case PPCISD::LXSIZX:          return "PPCISD::LXSIZX";
  case PPCISD::STXSIX:          return "PPCISD::STXSIX";
  case PPCISD::VEXTS:           return "PPCISD::VEXTS";
  case PPCISD::SExtVElems:      return "PPCISD::SExtVElems";
  case PPCISD::LXVD2X:          return "PPCISD::LXVD2X";
  case PPCISD::STXVD2X:         return "PPCISD::STXVD2X";
  case PPCISD::ST_VSR_SCAL_INT:
                                return "PPCISD::ST_VSR_SCAL_INT";
  case PPCISD::COND_BRANCH:     return "PPCISD::COND_BRANCH";
  case PPCISD::BDNZ:            return "PPCISD::BDNZ";
  case PPCISD::BDZ:             return "PPCISD::BDZ";
  case PPCISD::MFFS:            return "PPCISD::MFFS";
  case PPCISD::FADDRTZ:         return "PPCISD::FADDRTZ";
  case PPCISD::TC_RETURN:       return "PPCISD::TC_RETURN";
  case PPCISD::CR6SET:          return "PPCISD::CR6SET";
  case PPCISD::CR6UNSET:        return "PPCISD::CR6UNSET";
  case PPCISD::PPC32_GOT:       return "PPCISD::PPC32_GOT";
  case PPCISD::PPC32_PICGOT:    return "PPCISD::PPC32_PICGOT";
  case PPCISD::ADDIS_GOT_TPREL_HA: return "PPCISD::ADDIS_GOT_TPREL_HA";
  case PPCISD::LD_GOT_TPREL_L:  return "PPCISD::LD_GOT_TPREL_L";
  case PPCISD::ADD_TLS:         return "PPCISD::ADD_TLS";
  case PPCISD::ADDIS_TLSGD_HA:  return "PPCISD::ADDIS_TLSGD_HA";
  case PPCISD::ADDI_TLSGD_L:    return "PPCISD::ADDI_TLSGD_L";
  case PPCISD::GET_TLS_ADDR:    return "PPCISD::GET_TLS_ADDR";
  case PPCISD::ADDI_TLSGD_L_ADDR: return "PPCISD::ADDI_TLSGD_L_ADDR";
  case PPCISD::ADDIS_TLSLD_HA:  return "PPCISD::ADDIS_TLSLD_HA";
  case PPCISD::ADDI_TLSLD_L:    return "PPCISD::ADDI_TLSLD_L";
  case PPCISD::GET_TLSLD_ADDR:  return "PPCISD::GET_TLSLD_ADDR";
  case PPCISD::ADDI_TLSLD_L_ADDR: return "PPCISD::ADDI_TLSLD_L_ADDR";
  case PPCISD::ADDIS_DTPREL_HA: return "PPCISD::ADDIS_DTPREL_HA";
  case PPCISD::ADDI_DTPREL_L:   return "PPCISD::ADDI_DTPREL_L";
  case PPCISD::VADD_SPLAT:      return "PPCISD::VADD_SPLAT";
  case PPCISD::SC:              return "PPCISD::SC";
  case PPCISD::CLRBHRB:         return "PPCISD::CLRBHRB";
  case PPCISD::MFBHRBE:         return "PPCISD::MFBHRBE";
  case PPCISD::RFEBB:           return "PPCISD::RFEBB";
  case PPCISD::XXSWAPD:         return "PPCISD::XXSWAPD";
  case PPCISD::SWAP_NO_CHAIN:   return "PPCISD::SWAP_NO_CHAIN";
  case PPCISD::VABSD:           return "PPCISD::VABSD";
  case PPCISD::QVFPERM:         return "PPCISD::QVFPERM";
  case PPCISD::QVGPCI:          return "PPCISD::QVGPCI";
  case PPCISD::QVALIGNI:        return "PPCISD::QVALIGNI";
  case PPCISD::QVESPLATI:       return "PPCISD::QVESPLATI";
  case PPCISD::QBFLT:           return "PPCISD::QBFLT";
  case PPCISD::QVLFSb:          return "PPCISD::QVLFSb";
  case PPCISD::BUILD_FP128:     return "PPCISD::BUILD_FP128";
  case PPCISD::EXTSWSLI:        return "PPCISD::EXTSWSLI";
  }
  return nullptr;
}

EVT PPCTargetLowering::getSetCCResultType(const DataLayout &DL, LLVMContext &C,
                                          EVT VT) const {
  if (!VT.isVector())
    return Subtarget.useCRBits() ? MVT::i1 : MVT::i32;

  if (Subtarget.hasQPX())
    return EVT::getVectorVT(C, MVT::i1, VT.getVectorNumElements());

  return VT.changeVectorElementTypeToInteger();
}

bool PPCTargetLowering::enableAggressiveFMAFusion(EVT VT) const {
  assert(VT.isFloatingPoint() && "Non-floating-point FMA?");
  return true;
}

//===----------------------------------------------------------------------===//
// Node matching predicates, for use by the tblgen matching code.
//===----------------------------------------------------------------------===//

/// isFloatingPointZero - Return true if this is 0.0 or -0.0.
static bool isFloatingPointZero(SDValue Op) {
  if (ConstantFPSDNode *CFP = dyn_cast<ConstantFPSDNode>(Op))
    return CFP->getValueAPF().isZero();
  else if (ISD::isEXTLoad(Op.getNode()) || ISD::isNON_EXTLoad(Op.getNode())) {
    // Maybe this has already been legalized into the constant pool?
    if (ConstantPoolSDNode *CP = dyn_cast<ConstantPoolSDNode>(Op.getOperand(1)))
      if (const ConstantFP *CFP = dyn_cast<ConstantFP>(CP->getConstVal()))
        return CFP->getValueAPF().isZero();
  }
  return false;
}

/// isConstantOrUndef - Op is either an undef node or a ConstantSDNode.  Return
/// true if Op is undef or if it matches the specified value.
static bool isConstantOrUndef(int Op, int Val) {
  return Op < 0 || Op == Val;
}

/// isVPKUHUMShuffleMask - Return true if this is the shuffle mask for a
/// VPKUHUM instruction.
/// The ShuffleKind distinguishes between big-endian operations with
/// two different inputs (0), either-endian operations with two identical
/// inputs (1), and little-endian operations with two different inputs (2).
/// For the latter, the input operands are swapped (see PPCInstrAltivec.td).
bool PPC::isVPKUHUMShuffleMask(ShuffleVectorSDNode *N, unsigned ShuffleKind,
                               SelectionDAG &DAG) {
  bool IsLE = DAG.getDataLayout().isLittleEndian();
  if (ShuffleKind == 0) {
    if (IsLE)
      return false;
    for (unsigned i = 0; i != 16; ++i)
      if (!isConstantOrUndef(N->getMaskElt(i), i*2+1))
        return false;
  } else if (ShuffleKind == 2) {
    if (!IsLE)
      return false;
    for (unsigned i = 0; i != 16; ++i)
      if (!isConstantOrUndef(N->getMaskElt(i), i*2))
        return false;
  } else if (ShuffleKind == 1) {
    unsigned j = IsLE ? 0 : 1;
    for (unsigned i = 0; i != 8; ++i)
      if (!isConstantOrUndef(N->getMaskElt(i),    i*2+j) ||
          !isConstantOrUndef(N->getMaskElt(i+8),  i*2+j))
        return false;
  }
  return true;
}

/// isVPKUWUMShuffleMask - Return true if this is the shuffle mask for a
/// VPKUWUM instruction.
/// The ShuffleKind distinguishes between big-endian operations with
/// two different inputs (0), either-endian operations with two identical
/// inputs (1), and little-endian operations with two different inputs (2).
/// For the latter, the input operands are swapped (see PPCInstrAltivec.td).
bool PPC::isVPKUWUMShuffleMask(ShuffleVectorSDNode *N, unsigned ShuffleKind,
                               SelectionDAG &DAG) {
  bool IsLE = DAG.getDataLayout().isLittleEndian();
  if (ShuffleKind == 0) {
    if (IsLE)
      return false;
    for (unsigned i = 0; i != 16; i += 2)
      if (!isConstantOrUndef(N->getMaskElt(i  ),  i*2+2) ||
          !isConstantOrUndef(N->getMaskElt(i+1),  i*2+3))
        return false;
  } else if (ShuffleKind == 2) {
    if (!IsLE)
      return false;
    for (unsigned i = 0; i != 16; i += 2)
      if (!isConstantOrUndef(N->getMaskElt(i  ),  i*2) ||
          !isConstantOrUndef(N->getMaskElt(i+1),  i*2+1))
        return false;
  } else if (ShuffleKind == 1) {
    unsigned j = IsLE ? 0 : 2;
    for (unsigned i = 0; i != 8; i += 2)
      if (!isConstantOrUndef(N->getMaskElt(i  ),  i*2+j)   ||
          !isConstantOrUndef(N->getMaskElt(i+1),  i*2+j+1) ||
          !isConstantOrUndef(N->getMaskElt(i+8),  i*2+j)   ||
          !isConstantOrUndef(N->getMaskElt(i+9),  i*2+j+1))
        return false;
  }
  return true;
}

/// isVPKUDUMShuffleMask - Return true if this is the shuffle mask for a
/// VPKUDUM instruction, AND the VPKUDUM instruction exists for the
/// current subtarget.
///
/// The ShuffleKind distinguishes between big-endian operations with
/// two different inputs (0), either-endian operations with two identical
/// inputs (1), and little-endian operations with two different inputs (2).
/// For the latter, the input operands are swapped (see PPCInstrAltivec.td).
bool PPC::isVPKUDUMShuffleMask(ShuffleVectorSDNode *N, unsigned ShuffleKind,
                               SelectionDAG &DAG) {
  const PPCSubtarget& Subtarget =
    static_cast<const PPCSubtarget&>(DAG.getSubtarget());
  if (!Subtarget.hasP8Vector())
    return false;

  bool IsLE = DAG.getDataLayout().isLittleEndian();
  if (ShuffleKind == 0) {
    if (IsLE)
      return false;
    for (unsigned i = 0; i != 16; i += 4)
      if (!isConstantOrUndef(N->getMaskElt(i  ),  i*2+4) ||
          !isConstantOrUndef(N->getMaskElt(i+1),  i*2+5) ||
          !isConstantOrUndef(N->getMaskElt(i+2),  i*2+6) ||
          !isConstantOrUndef(N->getMaskElt(i+3),  i*2+7))
        return false;
  } else if (ShuffleKind == 2) {
    if (!IsLE)
      return false;
    for (unsigned i = 0; i != 16; i += 4)
      if (!isConstantOrUndef(N->getMaskElt(i  ),  i*2) ||
          !isConstantOrUndef(N->getMaskElt(i+1),  i*2+1) ||
          !isConstantOrUndef(N->getMaskElt(i+2),  i*2+2) ||
          !isConstantOrUndef(N->getMaskElt(i+3),  i*2+3))
        return false;
  } else if (ShuffleKind == 1) {
    unsigned j = IsLE ? 0 : 4;
    for (unsigned i = 0; i != 8; i += 4)
      if (!isConstantOrUndef(N->getMaskElt(i  ),  i*2+j)   ||
          !isConstantOrUndef(N->getMaskElt(i+1),  i*2+j+1) ||
          !isConstantOrUndef(N->getMaskElt(i+2),  i*2+j+2) ||
          !isConstantOrUndef(N->getMaskElt(i+3),  i*2+j+3) ||
          !isConstantOrUndef(N->getMaskElt(i+8),  i*2+j)   ||
          !isConstantOrUndef(N->getMaskElt(i+9),  i*2+j+1) ||
          !isConstantOrUndef(N->getMaskElt(i+10), i*2+j+2) ||
          !isConstantOrUndef(N->getMaskElt(i+11), i*2+j+3))
        return false;
  }
  return true;
}

/// isVMerge - Common function, used to match vmrg* shuffles.
///
static bool isVMerge(ShuffleVectorSDNode *N, unsigned UnitSize,
                     unsigned LHSStart, unsigned RHSStart) {
  if (N->getValueType(0) != MVT::v16i8)
    return false;
  assert((UnitSize == 1 || UnitSize == 2 || UnitSize == 4) &&
         "Unsupported merge size!");

  for (unsigned i = 0; i != 8/UnitSize; ++i)     // Step over units
    for (unsigned j = 0; j != UnitSize; ++j) {   // Step over bytes within unit
      if (!isConstantOrUndef(N->getMaskElt(i*UnitSize*2+j),
                             LHSStart+j+i*UnitSize) ||
          !isConstantOrUndef(N->getMaskElt(i*UnitSize*2+UnitSize+j),
                             RHSStart+j+i*UnitSize))
        return false;
    }
  return true;
}

/// isVMRGLShuffleMask - Return true if this is a shuffle mask suitable for
/// a VMRGL* instruction with the specified unit size (1,2 or 4 bytes).
/// The ShuffleKind distinguishes between big-endian merges with two
/// different inputs (0), either-endian merges with two identical inputs (1),
/// and little-endian merges with two different inputs (2).  For the latter,
/// the input operands are swapped (see PPCInstrAltivec.td).
bool PPC::isVMRGLShuffleMask(ShuffleVectorSDNode *N, unsigned UnitSize,
                             unsigned ShuffleKind, SelectionDAG &DAG) {
  if (DAG.getDataLayout().isLittleEndian()) {
    if (ShuffleKind == 1) // unary
      return isVMerge(N, UnitSize, 0, 0);
    else if (ShuffleKind == 2) // swapped
      return isVMerge(N, UnitSize, 0, 16);
    else
      return false;
  } else {
    if (ShuffleKind == 1) // unary
      return isVMerge(N, UnitSize, 8, 8);
    else if (ShuffleKind == 0) // normal
      return isVMerge(N, UnitSize, 8, 24);
    else
      return false;
  }
}

/// isVMRGHShuffleMask - Return true if this is a shuffle mask suitable for
/// a VMRGH* instruction with the specified unit size (1,2 or 4 bytes).
/// The ShuffleKind distinguishes between big-endian merges with two
/// different inputs (0), either-endian merges with two identical inputs (1),
/// and little-endian merges with two different inputs (2).  For the latter,
/// the input operands are swapped (see PPCInstrAltivec.td).
bool PPC::isVMRGHShuffleMask(ShuffleVectorSDNode *N, unsigned UnitSize,
                             unsigned ShuffleKind, SelectionDAG &DAG) {
  if (DAG.getDataLayout().isLittleEndian()) {
    if (ShuffleKind == 1) // unary
      return isVMerge(N, UnitSize, 8, 8);
    else if (ShuffleKind == 2) // swapped
      return isVMerge(N, UnitSize, 8, 24);
    else
      return false;
  } else {
    if (ShuffleKind == 1) // unary
      return isVMerge(N, UnitSize, 0, 0);
    else if (ShuffleKind == 0) // normal
      return isVMerge(N, UnitSize, 0, 16);
    else
      return false;
  }
}

/**
 * Common function used to match vmrgew and vmrgow shuffles
 *
 * The indexOffset determines whether to look for even or odd words in
 * the shuffle mask. This is based on the of the endianness of the target
 * machine.
 *   - Little Endian:
 *     - Use offset of 0 to check for odd elements
 *     - Use offset of 4 to check for even elements
 *   - Big Endian:
 *     - Use offset of 0 to check for even elements
 *     - Use offset of 4 to check for odd elements
 * A detailed description of the vector element ordering for little endian and
 * big endian can be found at
 * http://www.ibm.com/developerworks/library/l-ibm-xl-c-cpp-compiler/index.html
 * Targeting your applications - what little endian and big endian IBM XL C/C++
 * compiler differences mean to you
 *
 * The mask to the shuffle vector instruction specifies the indices of the
 * elements from the two input vectors to place in the result. The elements are
 * numbered in array-access order, starting with the first vector. These vectors
 * are always of type v16i8, thus each vector will contain 16 elements of size
 * 8. More info on the shuffle vector can be found in the
 * http://llvm.org/docs/LangRef.html#shufflevector-instruction
 * Language Reference.
 *
 * The RHSStartValue indicates whether the same input vectors are used (unary)
 * or two different input vectors are used, based on the following:
 *   - If the instruction uses the same vector for both inputs, the range of the
 *     indices will be 0 to 15. In this case, the RHSStart value passed should
 *     be 0.
 *   - If the instruction has two different vectors then the range of the
 *     indices will be 0 to 31. In this case, the RHSStart value passed should
 *     be 16 (indices 0-15 specify elements in the first vector while indices 16
 *     to 31 specify elements in the second vector).
 *
 * \param[in] N The shuffle vector SD Node to analyze
 * \param[in] IndexOffset Specifies whether to look for even or odd elements
 * \param[in] RHSStartValue Specifies the starting index for the righthand input
 * vector to the shuffle_vector instruction
 * \return true iff this shuffle vector represents an even or odd word merge
 */
static bool isVMerge(ShuffleVectorSDNode *N, unsigned IndexOffset,
                     unsigned RHSStartValue) {
  if (N->getValueType(0) != MVT::v16i8)
    return false;

  for (unsigned i = 0; i < 2; ++i)
    for (unsigned j = 0; j < 4; ++j)
      if (!isConstantOrUndef(N->getMaskElt(i*4+j),
                             i*RHSStartValue+j+IndexOffset) ||
          !isConstantOrUndef(N->getMaskElt(i*4+j+8),
                             i*RHSStartValue+j+IndexOffset+8))
        return false;
  return true;
}

/**
 * Determine if the specified shuffle mask is suitable for the vmrgew or
 * vmrgow instructions.
 *
 * \param[in] N The shuffle vector SD Node to analyze
 * \param[in] CheckEven Check for an even merge (true) or an odd merge (false)
 * \param[in] ShuffleKind Identify the type of merge:
 *   - 0 = big-endian merge with two different inputs;
 *   - 1 = either-endian merge with two identical inputs;
 *   - 2 = little-endian merge with two different inputs (inputs are swapped for
 *     little-endian merges).
 * \param[in] DAG The current SelectionDAG
 * \return true iff this shuffle mask
 */
bool PPC::isVMRGEOShuffleMask(ShuffleVectorSDNode *N, bool CheckEven,
                              unsigned ShuffleKind, SelectionDAG &DAG) {
  if (DAG.getDataLayout().isLittleEndian()) {
    unsigned indexOffset = CheckEven ? 4 : 0;
    if (ShuffleKind == 1) // Unary
      return isVMerge(N, indexOffset, 0);
    else if (ShuffleKind == 2) // swapped
      return isVMerge(N, indexOffset, 16);
    else
      return false;
  }
  else {
    unsigned indexOffset = CheckEven ? 0 : 4;
    if (ShuffleKind == 1) // Unary
      return isVMerge(N, indexOffset, 0);
    else if (ShuffleKind == 0) // Normal
      return isVMerge(N, indexOffset, 16);
    else
      return false;
  }
  return false;
}

/// isVSLDOIShuffleMask - If this is a vsldoi shuffle mask, return the shift
/// amount, otherwise return -1.
/// The ShuffleKind distinguishes between big-endian operations with two
/// different inputs (0), either-endian operations with two identical inputs
/// (1), and little-endian operations with two different inputs (2).  For the
/// latter, the input operands are swapped (see PPCInstrAltivec.td).
int PPC::isVSLDOIShuffleMask(SDNode *N, unsigned ShuffleKind,
                             SelectionDAG &DAG) {
  if (N->getValueType(0) != MVT::v16i8)
    return -1;

  ShuffleVectorSDNode *SVOp = cast<ShuffleVectorSDNode>(N);

  // Find the first non-undef value in the shuffle mask.
  unsigned i;
  for (i = 0; i != 16 && SVOp->getMaskElt(i) < 0; ++i)
    /*search*/;

  if (i == 16) return -1;  // all undef.

  // Otherwise, check to see if the rest of the elements are consecutively
  // numbered from this value.
  unsigned ShiftAmt = SVOp->getMaskElt(i);
  if (ShiftAmt < i) return -1;

  ShiftAmt -= i;
  bool isLE = DAG.getDataLayout().isLittleEndian();

  if ((ShuffleKind == 0 && !isLE) || (ShuffleKind == 2 && isLE)) {
    // Check the rest of the elements to see if they are consecutive.
    for (++i; i != 16; ++i)
      if (!isConstantOrUndef(SVOp->getMaskElt(i), ShiftAmt+i))
        return -1;
  } else if (ShuffleKind == 1) {
    // Check the rest of the elements to see if they are consecutive.
    for (++i; i != 16; ++i)
      if (!isConstantOrUndef(SVOp->getMaskElt(i), (ShiftAmt+i) & 15))
        return -1;
  } else
    return -1;

  if (isLE)
    ShiftAmt = 16 - ShiftAmt;

  return ShiftAmt;
}

/// isSplatShuffleMask - Return true if the specified VECTOR_SHUFFLE operand
/// specifies a splat of a single element that is suitable for input to
/// VSPLTB/VSPLTH/VSPLTW.
bool PPC::isSplatShuffleMask(ShuffleVectorSDNode *N, unsigned EltSize) {
  assert(N->getValueType(0) == MVT::v16i8 &&
         (EltSize == 1 || EltSize == 2 || EltSize == 4));

  // The consecutive indices need to specify an element, not part of two
  // different elements.  So abandon ship early if this isn't the case.
  if (N->getMaskElt(0) % EltSize != 0)
    return false;

  // This is a splat operation if each element of the permute is the same, and
  // if the value doesn't reference the second vector.
  unsigned ElementBase = N->getMaskElt(0);

  // FIXME: Handle UNDEF elements too!
  if (ElementBase >= 16)
    return false;

  // Check that the indices are consecutive, in the case of a multi-byte element
  // splatted with a v16i8 mask.
  for (unsigned i = 1; i != EltSize; ++i)
    if (N->getMaskElt(i) < 0 || N->getMaskElt(i) != (int)(i+ElementBase))
      return false;

  for (unsigned i = EltSize, e = 16; i != e; i += EltSize) {
    if (N->getMaskElt(i) < 0) continue;
    for (unsigned j = 0; j != EltSize; ++j)
      if (N->getMaskElt(i+j) != N->getMaskElt(j))
        return false;
  }
  return true;
}

/// Check that the mask is shuffling N byte elements. Within each N byte
/// element of the mask, the indices could be either in increasing or
/// decreasing order as long as they are consecutive.
/// \param[in] N the shuffle vector SD Node to analyze
/// \param[in] Width the element width in bytes, could be 2/4/8/16 (HalfWord/
/// Word/DoubleWord/QuadWord).
/// \param[in] StepLen the delta indices number among the N byte element, if
/// the mask is in increasing/decreasing order then it is 1/-1.
/// \return true iff the mask is shuffling N byte elements.
static bool isNByteElemShuffleMask(ShuffleVectorSDNode *N, unsigned Width,
                                   int StepLen) {
  assert((Width == 2 || Width == 4 || Width == 8 || Width == 16) &&
         "Unexpected element width.");
  assert((StepLen == 1 || StepLen == -1) && "Unexpected element width.");

  unsigned NumOfElem = 16 / Width;
  unsigned MaskVal[16]; //  Width is never greater than 16
  for (unsigned i = 0; i < NumOfElem; ++i) {
    MaskVal[0] = N->getMaskElt(i * Width);
    if ((StepLen == 1) && (MaskVal[0] % Width)) {
      return false;
    } else if ((StepLen == -1) && ((MaskVal[0] + 1) % Width)) {
      return false;
    }

    for (unsigned int j = 1; j < Width; ++j) {
      MaskVal[j] = N->getMaskElt(i * Width + j);
      if (MaskVal[j] != MaskVal[j-1] + StepLen) {
        return false;
      }
    }
  }

  return true;
}

bool PPC::isXXINSERTWMask(ShuffleVectorSDNode *N, unsigned &ShiftElts,
                          unsigned &InsertAtByte, bool &Swap, bool IsLE) {
  if (!isNByteElemShuffleMask(N, 4, 1))
    return false;

  // Now we look at mask elements 0,4,8,12
  unsigned M0 = N->getMaskElt(0) / 4;
  unsigned M1 = N->getMaskElt(4) / 4;
  unsigned M2 = N->getMaskElt(8) / 4;
  unsigned M3 = N->getMaskElt(12) / 4;
  unsigned LittleEndianShifts[] = { 2, 1, 0, 3 };
  unsigned BigEndianShifts[] = { 3, 0, 1, 2 };

  // Below, let H and L be arbitrary elements of the shuffle mask
  // where H is in the range [4,7] and L is in the range [0,3].
  // H, 1, 2, 3 or L, 5, 6, 7
  if ((M0 > 3 && M1 == 1 && M2 == 2 && M3 == 3) ||
      (M0 < 4 && M1 == 5 && M2 == 6 && M3 == 7)) {
    ShiftElts = IsLE ? LittleEndianShifts[M0 & 0x3] : BigEndianShifts[M0 & 0x3];
    InsertAtByte = IsLE ? 12 : 0;
    Swap = M0 < 4;
    return true;
  }
  // 0, H, 2, 3 or 4, L, 6, 7
  if ((M1 > 3 && M0 == 0 && M2 == 2 && M3 == 3) ||
      (M1 < 4 && M0 == 4 && M2 == 6 && M3 == 7)) {
    ShiftElts = IsLE ? LittleEndianShifts[M1 & 0x3] : BigEndianShifts[M1 & 0x3];
    InsertAtByte = IsLE ? 8 : 4;
    Swap = M1 < 4;
    return true;
  }
  // 0, 1, H, 3 or 4, 5, L, 7
  if ((M2 > 3 && M0 == 0 && M1 == 1 && M3 == 3) ||
      (M2 < 4 && M0 == 4 && M1 == 5 && M3 == 7)) {
    ShiftElts = IsLE ? LittleEndianShifts[M2 & 0x3] : BigEndianShifts[M2 & 0x3];
    InsertAtByte = IsLE ? 4 : 8;
    Swap = M2 < 4;
    return true;
  }
  // 0, 1, 2, H or 4, 5, 6, L
  if ((M3 > 3 && M0 == 0 && M1 == 1 && M2 == 2) ||
      (M3 < 4 && M0 == 4 && M1 == 5 && M2 == 6)) {
    ShiftElts = IsLE ? LittleEndianShifts[M3 & 0x3] : BigEndianShifts[M3 & 0x3];
    InsertAtByte = IsLE ? 0 : 12;
    Swap = M3 < 4;
    return true;
  }

  // If both vector operands for the shuffle are the same vector, the mask will
  // contain only elements from the first one and the second one will be undef.
  if (N->getOperand(1).isUndef()) {
    ShiftElts = 0;
    Swap = true;
    unsigned XXINSERTWSrcElem = IsLE ? 2 : 1;
    if (M0 == XXINSERTWSrcElem && M1 == 1 && M2 == 2 && M3 == 3) {
      InsertAtByte = IsLE ? 12 : 0;
      return true;
    }
    if (M0 == 0 && M1 == XXINSERTWSrcElem && M2 == 2 && M3 == 3) {
      InsertAtByte = IsLE ? 8 : 4;
      return true;
    }
    if (M0 == 0 && M1 == 1 && M2 == XXINSERTWSrcElem && M3 == 3) {
      InsertAtByte = IsLE ? 4 : 8;
      return true;
    }
    if (M0 == 0 && M1 == 1 && M2 == 2 && M3 == XXINSERTWSrcElem) {
      InsertAtByte = IsLE ? 0 : 12;
      return true;
    }
  }

  return false;
}

bool PPC::isXXSLDWIShuffleMask(ShuffleVectorSDNode *N, unsigned &ShiftElts,
                               bool &Swap, bool IsLE) {
  assert(N->getValueType(0) == MVT::v16i8 && "Shuffle vector expects v16i8");
  // Ensure each byte index of the word is consecutive.
  if (!isNByteElemShuffleMask(N, 4, 1))
    return false;

  // Now we look at mask elements 0,4,8,12, which are the beginning of words.
  unsigned M0 = N->getMaskElt(0) / 4;
  unsigned M1 = N->getMaskElt(4) / 4;
  unsigned M2 = N->getMaskElt(8) / 4;
  unsigned M3 = N->getMaskElt(12) / 4;

  // If both vector operands for the shuffle are the same vector, the mask will
  // contain only elements from the first one and the second one will be undef.
  if (N->getOperand(1).isUndef()) {
    assert(M0 < 4 && "Indexing into an undef vector?");
    if (M1 != (M0 + 1) % 4 || M2 != (M1 + 1) % 4 || M3 != (M2 + 1) % 4)
      return false;

    ShiftElts = IsLE ? (4 - M0) % 4 : M0;
    Swap = false;
    return true;
  }

  // Ensure each word index of the ShuffleVector Mask is consecutive.
  if (M1 != (M0 + 1) % 8 || M2 != (M1 + 1) % 8 || M3 != (M2 + 1) % 8)
    return false;

  if (IsLE) {
    if (M0 == 0 || M0 == 7 || M0 == 6 || M0 == 5) {
      // Input vectors don't need to be swapped if the leading element
      // of the result is one of the 3 left elements of the second vector
      // (or if there is no shift to be done at all).
      Swap = false;
      ShiftElts = (8 - M0) % 8;
    } else if (M0 == 4 || M0 == 3 || M0 == 2 || M0 == 1) {
      // Input vectors need to be swapped if the leading element
      // of the result is one of the 3 left elements of the first vector
      // (or if we're shifting by 4 - thereby simply swapping the vectors).
      Swap = true;
      ShiftElts = (4 - M0) % 4;
    }

    return true;
  } else {                                          // BE
    if (M0 == 0 || M0 == 1 || M0 == 2 || M0 == 3) {
      // Input vectors don't need to be swapped if the leading element
      // of the result is one of the 4 elements of the first vector.
      Swap = false;
      ShiftElts = M0;
    } else if (M0 == 4 || M0 == 5 || M0 == 6 || M0 == 7) {
      // Input vectors need to be swapped if the leading element
      // of the result is one of the 4 elements of the right vector.
      Swap = true;
      ShiftElts = M0 - 4;
    }

    return true;
  }
}

bool static isXXBRShuffleMaskHelper(ShuffleVectorSDNode *N, int Width) {
  assert(N->getValueType(0) == MVT::v16i8 && "Shuffle vector expects v16i8");

  if (!isNByteElemShuffleMask(N, Width, -1))
    return false;

  for (int i = 0; i < 16; i += Width)
    if (N->getMaskElt(i) != i + Width - 1)
      return false;

  return true;
}

bool PPC::isXXBRHShuffleMask(ShuffleVectorSDNode *N) {
  return isXXBRShuffleMaskHelper(N, 2);
}

bool PPC::isXXBRWShuffleMask(ShuffleVectorSDNode *N) {
  return isXXBRShuffleMaskHelper(N, 4);
}

bool PPC::isXXBRDShuffleMask(ShuffleVectorSDNode *N) {
  return isXXBRShuffleMaskHelper(N, 8);
}

bool PPC::isXXBRQShuffleMask(ShuffleVectorSDNode *N) {
  return isXXBRShuffleMaskHelper(N, 16);
}

/// Can node \p N be lowered to an XXPERMDI instruction? If so, set \p Swap
/// if the inputs to the instruction should be swapped and set \p DM to the
/// value for the immediate.
/// Specifically, set \p Swap to true only if \p N can be lowered to XXPERMDI
/// AND element 0 of the result comes from the first input (LE) or second input
/// (BE). Set \p DM to the calculated result (0-3) only if \p N can be lowered.
/// \return true iff the given mask of shuffle node \p N is a XXPERMDI shuffle
/// mask.
bool PPC::isXXPERMDIShuffleMask(ShuffleVectorSDNode *N, unsigned &DM,
                               bool &Swap, bool IsLE) {
  assert(N->getValueType(0) == MVT::v16i8 && "Shuffle vector expects v16i8");

  // Ensure each byte index of the double word is consecutive.
  if (!isNByteElemShuffleMask(N, 8, 1))
    return false;

  unsigned M0 = N->getMaskElt(0) / 8;
  unsigned M1 = N->getMaskElt(8) / 8;
  assert(((M0 | M1) < 4) && "A mask element out of bounds?");

  // If both vector operands for the shuffle are the same vector, the mask will
  // contain only elements from the first one and the second one will be undef.
  if (N->getOperand(1).isUndef()) {
    if ((M0 | M1) < 2) {
      DM = IsLE ? (((~M1) & 1) << 1) + ((~M0) & 1) : (M0 << 1) + (M1 & 1);
      Swap = false;
      return true;
    } else
      return false;
  }

  if (IsLE) {
    if (M0 > 1 && M1 < 2) {
      Swap = false;
    } else if (M0 < 2 && M1 > 1) {
      M0 = (M0 + 2) % 4;
      M1 = (M1 + 2) % 4;
      Swap = true;
    } else
      return false;

    // Note: if control flow comes here that means Swap is already set above
    DM = (((~M1) & 1) << 1) + ((~M0) & 1);
    return true;
  } else { // BE
    if (M0 < 2 && M1 > 1) {
      Swap = false;
    } else if (M0 > 1 && M1 < 2) {
      M0 = (M0 + 2) % 4;
      M1 = (M1 + 2) % 4;
      Swap = true;
    } else
      return false;

    // Note: if control flow comes here that means Swap is already set above
    DM = (M0 << 1) + (M1 & 1);
    return true;
  }
}


/// getVSPLTImmediate - Return the appropriate VSPLT* immediate to splat the
/// specified isSplatShuffleMask VECTOR_SHUFFLE mask.
unsigned PPC::getVSPLTImmediate(SDNode *N, unsigned EltSize,
                                SelectionDAG &DAG) {
  ShuffleVectorSDNode *SVOp = cast<ShuffleVectorSDNode>(N);
  assert(isSplatShuffleMask(SVOp, EltSize));
  if (DAG.getDataLayout().isLittleEndian())
    return (16 / EltSize) - 1 - (SVOp->getMaskElt(0) / EltSize);
  else
    return SVOp->getMaskElt(0) / EltSize;
}

/// get_VSPLTI_elt - If this is a build_vector of constants which can be formed
/// by using a vspltis[bhw] instruction of the specified element size, return
/// the constant being splatted.  The ByteSize field indicates the number of
/// bytes of each element [124] -> [bhw].
SDValue PPC::get_VSPLTI_elt(SDNode *N, unsigned ByteSize, SelectionDAG &DAG) {
  SDValue OpVal(nullptr, 0);

  // If ByteSize of the splat is bigger than the element size of the
  // build_vector, then we have a case where we are checking for a splat where
  // multiple elements of the buildvector are folded together into a single
  // logical element of the splat (e.g. "vsplish 1" to splat {0,1}*8).
  unsigned EltSize = 16/N->getNumOperands();
  if (EltSize < ByteSize) {
    unsigned Multiple = ByteSize/EltSize;   // Number of BV entries per spltval.
    SDValue UniquedVals[4];
    assert(Multiple > 1 && Multiple <= 4 && "How can this happen?");

    // See if all of the elements in the buildvector agree across.
    for (unsigned i = 0, e = N->getNumOperands(); i != e; ++i) {
      if (N->getOperand(i).isUndef()) continue;
      // If the element isn't a constant, bail fully out.
      if (!isa<ConstantSDNode>(N->getOperand(i))) return SDValue();

      if (!UniquedVals[i&(Multiple-1)].getNode())
        UniquedVals[i&(Multiple-1)] = N->getOperand(i);
      else if (UniquedVals[i&(Multiple-1)] != N->getOperand(i))
        return SDValue();  // no match.
    }

    // Okay, if we reached this point, UniquedVals[0..Multiple-1] contains
    // either constant or undef values that are identical for each chunk.  See
    // if these chunks can form into a larger vspltis*.

    // Check to see if all of the leading entries are either 0 or -1.  If
    // neither, then this won't fit into the immediate field.
    bool LeadingZero = true;
    bool LeadingOnes = true;
    for (unsigned i = 0; i != Multiple-1; ++i) {
      if (!UniquedVals[i].getNode()) continue;  // Must have been undefs.

      LeadingZero &= isNullConstant(UniquedVals[i]);
      LeadingOnes &= isAllOnesConstant(UniquedVals[i]);
    }
    // Finally, check the least significant entry.
    if (LeadingZero) {
      if (!UniquedVals[Multiple-1].getNode())
        return DAG.getTargetConstant(0, SDLoc(N), MVT::i32);  // 0,0,0,undef
      int Val = cast<ConstantSDNode>(UniquedVals[Multiple-1])->getZExtValue();
      if (Val < 16)                                   // 0,0,0,4 -> vspltisw(4)
        return DAG.getTargetConstant(Val, SDLoc(N), MVT::i32);
    }
    if (LeadingOnes) {
      if (!UniquedVals[Multiple-1].getNode())
        return DAG.getTargetConstant(~0U, SDLoc(N), MVT::i32); // -1,-1,-1,undef
      int Val =cast<ConstantSDNode>(UniquedVals[Multiple-1])->getSExtValue();
      if (Val >= -16)                            // -1,-1,-1,-2 -> vspltisw(-2)
        return DAG.getTargetConstant(Val, SDLoc(N), MVT::i32);
    }

    return SDValue();
  }

  // Check to see if this buildvec has a single non-undef value in its elements.
  for (unsigned i = 0, e = N->getNumOperands(); i != e; ++i) {
    if (N->getOperand(i).isUndef()) continue;
    if (!OpVal.getNode())
      OpVal = N->getOperand(i);
    else if (OpVal != N->getOperand(i))
      return SDValue();
  }

  if (!OpVal.getNode()) return SDValue();  // All UNDEF: use implicit def.

  unsigned ValSizeInBytes = EltSize;
  uint64_t Value = 0;
  if (ConstantSDNode *CN = dyn_cast<ConstantSDNode>(OpVal)) {
    Value = CN->getZExtValue();
  } else if (ConstantFPSDNode *CN = dyn_cast<ConstantFPSDNode>(OpVal)) {
    assert(CN->getValueType(0) == MVT::f32 && "Only one legal FP vector type!");
    Value = FloatToBits(CN->getValueAPF().convertToFloat());
  }

  // If the splat value is larger than the element value, then we can never do
  // this splat.  The only case that we could fit the replicated bits into our
  // immediate field for would be zero, and we prefer to use vxor for it.
  if (ValSizeInBytes < ByteSize) return SDValue();

  // If the element value is larger than the splat value, check if it consists
  // of a repeated bit pattern of size ByteSize.
  if (!APInt(ValSizeInBytes * 8, Value).isSplat(ByteSize * 8))
    return SDValue();

  // Properly sign extend the value.
  int MaskVal = SignExtend32(Value, ByteSize * 8);

  // If this is zero, don't match, zero matches ISD::isBuildVectorAllZeros.
  if (MaskVal == 0) return SDValue();

  // Finally, if this value fits in a 5 bit sext field, return it
  if (SignExtend32<5>(MaskVal) == MaskVal)
    return DAG.getTargetConstant(MaskVal, SDLoc(N), MVT::i32);
  return SDValue();
}

/// isQVALIGNIShuffleMask - If this is a qvaligni shuffle mask, return the shift
/// amount, otherwise return -1.
int PPC::isQVALIGNIShuffleMask(SDNode *N) {
  EVT VT = N->getValueType(0);
  if (VT != MVT::v4f64 && VT != MVT::v4f32 && VT != MVT::v4i1)
    return -1;

  ShuffleVectorSDNode *SVOp = cast<ShuffleVectorSDNode>(N);

  // Find the first non-undef value in the shuffle mask.
  unsigned i;
  for (i = 0; i != 4 && SVOp->getMaskElt(i) < 0; ++i)
    /*search*/;

  if (i == 4) return -1;  // all undef.

  // Otherwise, check to see if the rest of the elements are consecutively
  // numbered from this value.
  unsigned ShiftAmt = SVOp->getMaskElt(i);
  if (ShiftAmt < i) return -1;
  ShiftAmt -= i;

  // Check the rest of the elements to see if they are consecutive.
  for (++i; i != 4; ++i)
    if (!isConstantOrUndef(SVOp->getMaskElt(i), ShiftAmt+i))
      return -1;

  return ShiftAmt;
}

//===----------------------------------------------------------------------===//
//  Addressing Mode Selection
//===----------------------------------------------------------------------===//

/// isIntS16Immediate - This method tests to see if the node is either a 32-bit
/// or 64-bit immediate, and if the value can be accurately represented as a
/// sign extension from a 16-bit value.  If so, this returns true and the
/// immediate.
bool llvm::isIntS16Immediate(SDNode *N, int16_t &Imm) {
  if (!isa<ConstantSDNode>(N))
    return false;

  Imm = (int16_t)cast<ConstantSDNode>(N)->getZExtValue();
  if (N->getValueType(0) == MVT::i32)
    return Imm == (int32_t)cast<ConstantSDNode>(N)->getZExtValue();
  else
    return Imm == (int64_t)cast<ConstantSDNode>(N)->getZExtValue();
}
bool llvm::isIntS16Immediate(SDValue Op, int16_t &Imm) {
  return isIntS16Immediate(Op.getNode(), Imm);
}

/// SelectAddressRegReg - Given the specified addressed, check to see if it
/// can be represented as an indexed [r+r] operation.  Returns false if it
/// can be more efficiently represented with [r+imm].
bool PPCTargetLowering::SelectAddressRegReg(SDValue N, SDValue &Base,
                                            SDValue &Index,
                                            SelectionDAG &DAG) const {
  int16_t imm = 0;
  if (N.getOpcode() == ISD::ADD) {
    if (isIntS16Immediate(N.getOperand(1), imm))
      return false;    // r+i
    if (N.getOperand(1).getOpcode() == PPCISD::Lo)
      return false;    // r+i

    Base = N.getOperand(0);
    Index = N.getOperand(1);
    return true;
  } else if (N.getOpcode() == ISD::OR) {
    if (isIntS16Immediate(N.getOperand(1), imm))
      return false;    // r+i can fold it if we can.

    // If this is an or of disjoint bitfields, we can codegen this as an add
    // (for better address arithmetic) if the LHS and RHS of the OR are provably
    // disjoint.
    KnownBits LHSKnown = DAG.computeKnownBits(N.getOperand(0));

    if (LHSKnown.Zero.getBoolValue()) {
      KnownBits RHSKnown = DAG.computeKnownBits(N.getOperand(1));
      // If all of the bits are known zero on the LHS or RHS, the add won't
      // carry.
      if (~(LHSKnown.Zero | RHSKnown.Zero) == 0) {
        Base = N.getOperand(0);
        Index = N.getOperand(1);
        return true;
      }
    }
  }

  return false;
}

// If we happen to be doing an i64 load or store into a stack slot that has
// less than a 4-byte alignment, then the frame-index elimination may need to
// use an indexed load or store instruction (because the offset may not be a
// multiple of 4). The extra register needed to hold the offset comes from the
// register scavenger, and it is possible that the scavenger will need to use
// an emergency spill slot. As a result, we need to make sure that a spill slot
// is allocated when doing an i64 load/store into a less-than-4-byte-aligned
// stack slot.
static void fixupFuncForFI(SelectionDAG &DAG, int FrameIdx, EVT VT) {
  // FIXME: This does not handle the LWA case.
  if (VT != MVT::i64)
    return;

  // NOTE: We'll exclude negative FIs here, which come from argument
  // lowering, because there are no known test cases triggering this problem
  // using packed structures (or similar). We can remove this exclusion if
  // we find such a test case. The reason why this is so test-case driven is
  // because this entire 'fixup' is only to prevent crashes (from the
  // register scavenger) on not-really-valid inputs. For example, if we have:
  //   %a = alloca i1
  //   %b = bitcast i1* %a to i64*
  //   store i64* a, i64 b
  // then the store should really be marked as 'align 1', but is not. If it
  // were marked as 'align 1' then the indexed form would have been
  // instruction-selected initially, and the problem this 'fixup' is preventing
  // won't happen regardless.
  if (FrameIdx < 0)
    return;

  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  unsigned Align = MFI.getObjectAlignment(FrameIdx);
  if (Align >= 4)
    return;

  PPCFunctionInfo *FuncInfo = MF.getInfo<PPCFunctionInfo>();
  FuncInfo->setHasNonRISpills();
}

/// Returns true if the address N can be represented by a base register plus
/// a signed 16-bit displacement [r+imm], and if it is not better
/// represented as reg+reg.  If \p Alignment is non-zero, only accept
/// displacements that are multiples of that value.
bool PPCTargetLowering::SelectAddressRegImm(SDValue N, SDValue &Disp,
                                            SDValue &Base,
                                            SelectionDAG &DAG,
                                            unsigned Alignment) const {
  // FIXME dl should come from parent load or store, not from address
  SDLoc dl(N);
  // If this can be more profitably realized as r+r, fail.
  if (SelectAddressRegReg(N, Disp, Base, DAG))
    return false;

  if (N.getOpcode() == ISD::ADD) {
    int16_t imm = 0;
    if (isIntS16Immediate(N.getOperand(1), imm) &&
        (!Alignment || (imm % Alignment) == 0)) {
      Disp = DAG.getTargetConstant(imm, dl, N.getValueType());
      if (FrameIndexSDNode *FI = dyn_cast<FrameIndexSDNode>(N.getOperand(0))) {
        Base = DAG.getTargetFrameIndex(FI->getIndex(), N.getValueType());
        fixupFuncForFI(DAG, FI->getIndex(), N.getValueType());
      } else {
        Base = N.getOperand(0);
      }
      return true; // [r+i]
    } else if (N.getOperand(1).getOpcode() == PPCISD::Lo) {
      // Match LOAD (ADD (X, Lo(G))).
      assert(!cast<ConstantSDNode>(N.getOperand(1).getOperand(1))->getZExtValue()
             && "Cannot handle constant offsets yet!");
      Disp = N.getOperand(1).getOperand(0);  // The global address.
      assert(Disp.getOpcode() == ISD::TargetGlobalAddress ||
             Disp.getOpcode() == ISD::TargetGlobalTLSAddress ||
             Disp.getOpcode() == ISD::TargetConstantPool ||
             Disp.getOpcode() == ISD::TargetJumpTable);
      Base = N.getOperand(0);
      return true;  // [&g+r]
    }
  } else if (N.getOpcode() == ISD::OR) {
    int16_t imm = 0;
    if (isIntS16Immediate(N.getOperand(1), imm) &&
        (!Alignment || (imm % Alignment) == 0)) {
      // If this is an or of disjoint bitfields, we can codegen this as an add
      // (for better address arithmetic) if the LHS and RHS of the OR are
      // provably disjoint.
      KnownBits LHSKnown = DAG.computeKnownBits(N.getOperand(0));

      if ((LHSKnown.Zero.getZExtValue()|~(uint64_t)imm) == ~0ULL) {
        // If all of the bits are known zero on the LHS or RHS, the add won't
        // carry.
        if (FrameIndexSDNode *FI =
              dyn_cast<FrameIndexSDNode>(N.getOperand(0))) {
          Base = DAG.getTargetFrameIndex(FI->getIndex(), N.getValueType());
          fixupFuncForFI(DAG, FI->getIndex(), N.getValueType());
        } else {
          Base = N.getOperand(0);
        }
        Disp = DAG.getTargetConstant(imm, dl, N.getValueType());
        return true;
      }
    }
  } else if (ConstantSDNode *CN = dyn_cast<ConstantSDNode>(N)) {
    // Loading from a constant address.

    // If this address fits entirely in a 16-bit sext immediate field, codegen
    // this as "d, 0"
    int16_t Imm;
    if (isIntS16Immediate(CN, Imm) && (!Alignment || (Imm % Alignment) == 0)) {
      Disp = DAG.getTargetConstant(Imm, dl, CN->getValueType(0));
      Base = DAG.getRegister(Subtarget.isPPC64() ? PPC::ZERO8 : PPC::ZERO,
                             CN->getValueType(0));
      return true;
    }

    // Handle 32-bit sext immediates with LIS + addr mode.
    if ((CN->getValueType(0) == MVT::i32 ||
         (int64_t)CN->getZExtValue() == (int)CN->getZExtValue()) &&
        (!Alignment || (CN->getZExtValue() % Alignment) == 0)) {
      int Addr = (int)CN->getZExtValue();

      // Otherwise, break this down into an LIS + disp.
      Disp = DAG.getTargetConstant((short)Addr, dl, MVT::i32);

      Base = DAG.getTargetConstant((Addr - (signed short)Addr) >> 16, dl,
                                   MVT::i32);
      unsigned Opc = CN->getValueType(0) == MVT::i32 ? PPC::LIS : PPC::LIS8;
      Base = SDValue(DAG.getMachineNode(Opc, dl, CN->getValueType(0), Base), 0);
      return true;
    }
  }

  Disp = DAG.getTargetConstant(0, dl, getPointerTy(DAG.getDataLayout()));
  if (FrameIndexSDNode *FI = dyn_cast<FrameIndexSDNode>(N)) {
    Base = DAG.getTargetFrameIndex(FI->getIndex(), N.getValueType());
    fixupFuncForFI(DAG, FI->getIndex(), N.getValueType());
  } else
    Base = N;
  return true;      // [r+0]
}

/// SelectAddressRegRegOnly - Given the specified addressed, force it to be
/// represented as an indexed [r+r] operation.
bool PPCTargetLowering::SelectAddressRegRegOnly(SDValue N, SDValue &Base,
                                                SDValue &Index,
                                                SelectionDAG &DAG) const {
  // Check to see if we can easily represent this as an [r+r] address.  This
  // will fail if it thinks that the address is more profitably represented as
  // reg+imm, e.g. where imm = 0.
  if (SelectAddressRegReg(N, Base, Index, DAG))
    return true;

  // If the address is the result of an add, we will utilize the fact that the
  // address calculation includes an implicit add.  However, we can reduce
  // register pressure if we do not materialize a constant just for use as the
  // index register.  We only get rid of the add if it is not an add of a
  // value and a 16-bit signed constant and both have a single use.
  int16_t imm = 0;
  if (N.getOpcode() == ISD::ADD &&
      (!isIntS16Immediate(N.getOperand(1), imm) ||
       !N.getOperand(1).hasOneUse() || !N.getOperand(0).hasOneUse())) {
    Base = N.getOperand(0);
    Index = N.getOperand(1);
    return true;
  }

  // Otherwise, do it the hard way, using R0 as the base register.
  Base = DAG.getRegister(Subtarget.isPPC64() ? PPC::ZERO8 : PPC::ZERO,
                         N.getValueType());
  Index = N;
  return true;
}

/// Returns true if we should use a direct load into vector instruction
/// (such as lxsd or lfd), instead of a load into gpr + direct move sequence.
static bool usePartialVectorLoads(SDNode *N) {
  if (!N->hasOneUse())
    return false;

  // If there are any other uses other than scalar to vector, then we should
  // keep it as a scalar load -> direct move pattern to prevent multiple
  // loads.  Currently, only check for i64 since we have lxsd/lfd to do this
  // efficiently, but no update equivalent.
  if (LoadSDNode *LD = dyn_cast<LoadSDNode>(N)) {
    EVT MemVT = LD->getMemoryVT();
    if (MemVT.isSimple() && MemVT.getSimpleVT().SimpleTy == MVT::i64) {
      SDNode *User = *(LD->use_begin());
      if (User->getOpcode() == ISD::SCALAR_TO_VECTOR)
        return true;
    }
  }

  return false;
}

/// getPreIndexedAddressParts - returns true by value, base pointer and
/// offset pointer and addressing mode by reference if the node's address
/// can be legally represented as pre-indexed load / store address.
bool PPCTargetLowering::getPreIndexedAddressParts(SDNode *N, SDValue &Base,
                                                  SDValue &Offset,
                                                  ISD::MemIndexedMode &AM,
                                                  SelectionDAG &DAG) const {
  if (DisablePPCPreinc) return false;

  bool isLoad = true;
  SDValue Ptr;
  EVT VT;
  unsigned Alignment;
  if (LoadSDNode *LD = dyn_cast<LoadSDNode>(N)) {
    Ptr = LD->getBasePtr();
    VT = LD->getMemoryVT();
    Alignment = LD->getAlignment();
  } else if (StoreSDNode *ST = dyn_cast<StoreSDNode>(N)) {
    Ptr = ST->getBasePtr();
    VT  = ST->getMemoryVT();
    Alignment = ST->getAlignment();
    isLoad = false;
  } else
    return false;

  // Do not generate pre-inc forms for specific loads that feed scalar_to_vector
  // instructions because we can fold these into a more efficient instruction
  // instead, (such as LXSD).
  if (isLoad && usePartialVectorLoads(N)) {
    return false;
  }

  // PowerPC doesn't have preinc load/store instructions for vectors (except
  // for QPX, which does have preinc r+r forms).
  if (VT.isVector()) {
    if (!Subtarget.hasQPX() || (VT != MVT::v4f64 && VT != MVT::v4f32)) {
      return false;
    } else if (SelectAddressRegRegOnly(Ptr, Offset, Base, DAG)) {
      AM = ISD::PRE_INC;
      return true;
    }
  }

  if (SelectAddressRegReg(Ptr, Base, Offset, DAG)) {
    // Common code will reject creating a pre-inc form if the base pointer
    // is a frame index, or if N is a store and the base pointer is either
    // the same as or a predecessor of the value being stored.  Check for
    // those situations here, and try with swapped Base/Offset instead.
    bool Swap = false;

    if (isa<FrameIndexSDNode>(Base) || isa<RegisterSDNode>(Base))
      Swap = true;
    else if (!isLoad) {
      SDValue Val = cast<StoreSDNode>(N)->getValue();
      if (Val == Base || Base.getNode()->isPredecessorOf(Val.getNode()))
        Swap = true;
    }

    if (Swap)
      std::swap(Base, Offset);

    AM = ISD::PRE_INC;
    return true;
  }

  // LDU/STU can only handle immediates that are a multiple of 4.
  if (VT != MVT::i64) {
    if (!SelectAddressRegImm(Ptr, Offset, Base, DAG, 0))
      return false;
  } else {
    // LDU/STU need an address with at least 4-byte alignment.
    if (Alignment < 4)
      return false;

    if (!SelectAddressRegImm(Ptr, Offset, Base, DAG, 4))
      return false;
  }

  if (LoadSDNode *LD = dyn_cast<LoadSDNode>(N)) {
    // PPC64 doesn't have lwau, but it does have lwaux.  Reject preinc load of
    // sext i32 to i64 when addr mode is r+i.
    if (LD->getValueType(0) == MVT::i64 && LD->getMemoryVT() == MVT::i32 &&
        LD->getExtensionType() == ISD::SEXTLOAD &&
        isa<ConstantSDNode>(Offset))
      return false;
  }

  AM = ISD::PRE_INC;
  return true;
}

//===----------------------------------------------------------------------===//
//  LowerOperation implementation
//===----------------------------------------------------------------------===//

/// Return true if we should reference labels using a PICBase, set the HiOpFlags
/// and LoOpFlags to the target MO flags.
static void getLabelAccessInfo(bool IsPIC, const PPCSubtarget &Subtarget,
                               unsigned &HiOpFlags, unsigned &LoOpFlags,
                               const GlobalValue *GV = nullptr) {
  HiOpFlags = PPCII::MO_HA;
  LoOpFlags = PPCII::MO_LO;

  // Don't use the pic base if not in PIC relocation model.
  if (IsPIC) {
    HiOpFlags |= PPCII::MO_PIC_FLAG;
    LoOpFlags |= PPCII::MO_PIC_FLAG;
  }

  // If this is a reference to a global value that requires a non-lazy-ptr, make
  // sure that instruction lowering adds it.
  if (GV && Subtarget.hasLazyResolverStub(GV)) {
    HiOpFlags |= PPCII::MO_NLP_FLAG;
    LoOpFlags |= PPCII::MO_NLP_FLAG;

    if (GV->hasHiddenVisibility()) {
      HiOpFlags |= PPCII::MO_NLP_HIDDEN_FLAG;
      LoOpFlags |= PPCII::MO_NLP_HIDDEN_FLAG;
    }
  }
}

static SDValue LowerLabelRef(SDValue HiPart, SDValue LoPart, bool isPIC,
                             SelectionDAG &DAG) {
  SDLoc DL(HiPart);
  EVT PtrVT = HiPart.getValueType();
  SDValue Zero = DAG.getConstant(0, DL, PtrVT);

  SDValue Hi = DAG.getNode(PPCISD::Hi, DL, PtrVT, HiPart, Zero);
  SDValue Lo = DAG.getNode(PPCISD::Lo, DL, PtrVT, LoPart, Zero);

  // With PIC, the first instruction is actually "GR+hi(&G)".
  if (isPIC)
    Hi = DAG.getNode(ISD::ADD, DL, PtrVT,
                     DAG.getNode(PPCISD::GlobalBaseReg, DL, PtrVT), Hi);

  // Generate non-pic code that has direct accesses to the constant pool.
  // The address of the global is just (hi(&g)+lo(&g)).
  return DAG.getNode(ISD::ADD, DL, PtrVT, Hi, Lo);
}

static void setUsesTOCBasePtr(MachineFunction &MF) {
  PPCFunctionInfo *FuncInfo = MF.getInfo<PPCFunctionInfo>();
  FuncInfo->setUsesTOCBasePtr();
}

static void setUsesTOCBasePtr(SelectionDAG &DAG) {
  setUsesTOCBasePtr(DAG.getMachineFunction());
}

static SDValue getTOCEntry(SelectionDAG &DAG, const SDLoc &dl, bool Is64Bit,
                           SDValue GA) {
  EVT VT = Is64Bit ? MVT::i64 : MVT::i32;
  SDValue Reg = Is64Bit ? DAG.getRegister(PPC::X2, VT) :
                DAG.getNode(PPCISD::GlobalBaseReg, dl, VT);

  SDValue Ops[] = { GA, Reg };
  return DAG.getMemIntrinsicNode(
      PPCISD::TOC_ENTRY, dl, DAG.getVTList(VT, MVT::Other), Ops, VT,
      MachinePointerInfo::getGOT(DAG.getMachineFunction()), 0,
      MachineMemOperand::MOLoad);
}

SDValue PPCTargetLowering::LowerConstantPool(SDValue Op,
                                             SelectionDAG &DAG) const {
  EVT PtrVT = Op.getValueType();
  ConstantPoolSDNode *CP = cast<ConstantPoolSDNode>(Op);
  const Constant *C = CP->getConstVal();

  // 64-bit SVR4 ABI code is always position-independent.
  // The actual address of the GlobalValue is stored in the TOC.
  if (Subtarget.isSVR4ABI() && Subtarget.isPPC64()) {
    setUsesTOCBasePtr(DAG);
    SDValue GA = DAG.getTargetConstantPool(C, PtrVT, CP->getAlignment(), 0);
    return getTOCEntry(DAG, SDLoc(CP), true, GA);
  }

  unsigned MOHiFlag, MOLoFlag;
  bool IsPIC = isPositionIndependent();
  getLabelAccessInfo(IsPIC, Subtarget, MOHiFlag, MOLoFlag);

  if (IsPIC && Subtarget.isSVR4ABI()) {
    SDValue GA = DAG.getTargetConstantPool(C, PtrVT, CP->getAlignment(),
                                           PPCII::MO_PIC_FLAG);
    return getTOCEntry(DAG, SDLoc(CP), false, GA);
  }

  SDValue CPIHi =
    DAG.getTargetConstantPool(C, PtrVT, CP->getAlignment(), 0, MOHiFlag);
  SDValue CPILo =
    DAG.getTargetConstantPool(C, PtrVT, CP->getAlignment(), 0, MOLoFlag);
  return LowerLabelRef(CPIHi, CPILo, IsPIC, DAG);
}

// For 64-bit PowerPC, prefer the more compact relative encodings.
// This trades 32 bits per jump table entry for one or two instructions
// on the jump site.
unsigned PPCTargetLowering::getJumpTableEncoding() const {
  if (isJumpTableRelative())
    return MachineJumpTableInfo::EK_LabelDifference32;

  return TargetLowering::getJumpTableEncoding();
}

bool PPCTargetLowering::isJumpTableRelative() const {
  if (Subtarget.isPPC64())
    return true;
  return TargetLowering::isJumpTableRelative();
}

SDValue PPCTargetLowering::getPICJumpTableRelocBase(SDValue Table,
                                                    SelectionDAG &DAG) const {
  if (!Subtarget.isPPC64())
    return TargetLowering::getPICJumpTableRelocBase(Table, DAG);

  switch (getTargetMachine().getCodeModel()) {
  case CodeModel::Small:
  case CodeModel::Medium:
    return TargetLowering::getPICJumpTableRelocBase(Table, DAG);
  default:
    return DAG.getNode(PPCISD::GlobalBaseReg, SDLoc(),
                       getPointerTy(DAG.getDataLayout()));
  }
}

const MCExpr *
PPCTargetLowering::getPICJumpTableRelocBaseExpr(const MachineFunction *MF,
                                                unsigned JTI,
                                                MCContext &Ctx) const {
  if (!Subtarget.isPPC64())
    return TargetLowering::getPICJumpTableRelocBaseExpr(MF, JTI, Ctx);

  switch (getTargetMachine().getCodeModel()) {
  case CodeModel::Small:
  case CodeModel::Medium:
    return TargetLowering::getPICJumpTableRelocBaseExpr(MF, JTI, Ctx);
  default:
    return MCSymbolRefExpr::create(MF->getPICBaseSymbol(), Ctx);
  }
}

SDValue PPCTargetLowering::LowerJumpTable(SDValue Op, SelectionDAG &DAG) const {
  EVT PtrVT = Op.getValueType();
  JumpTableSDNode *JT = cast<JumpTableSDNode>(Op);

  // 64-bit SVR4 ABI code is always position-independent.
  // The actual address of the GlobalValue is stored in the TOC.
  if (Subtarget.isSVR4ABI() && Subtarget.isPPC64()) {
    setUsesTOCBasePtr(DAG);
    SDValue GA = DAG.getTargetJumpTable(JT->getIndex(), PtrVT);
    return getTOCEntry(DAG, SDLoc(JT), true, GA);
  }

  unsigned MOHiFlag, MOLoFlag;
  bool IsPIC = isPositionIndependent();
  getLabelAccessInfo(IsPIC, Subtarget, MOHiFlag, MOLoFlag);

  if (IsPIC && Subtarget.isSVR4ABI()) {
    SDValue GA = DAG.getTargetJumpTable(JT->getIndex(), PtrVT,
                                        PPCII::MO_PIC_FLAG);
    return getTOCEntry(DAG, SDLoc(GA), false, GA);
  }

  SDValue JTIHi = DAG.getTargetJumpTable(JT->getIndex(), PtrVT, MOHiFlag);
  SDValue JTILo = DAG.getTargetJumpTable(JT->getIndex(), PtrVT, MOLoFlag);
  return LowerLabelRef(JTIHi, JTILo, IsPIC, DAG);
}

SDValue PPCTargetLowering::LowerBlockAddress(SDValue Op,
                                             SelectionDAG &DAG) const {
  EVT PtrVT = Op.getValueType();
  BlockAddressSDNode *BASDN = cast<BlockAddressSDNode>(Op);
  const BlockAddress *BA = BASDN->getBlockAddress();

  // 64-bit SVR4 ABI code is always position-independent.
  // The actual BlockAddress is stored in the TOC.
  if (Subtarget.isSVR4ABI() &&
      (Subtarget.isPPC64() || isPositionIndependent())) {
    if (Subtarget.isPPC64())
      setUsesTOCBasePtr(DAG);
    SDValue GA = DAG.getTargetBlockAddress(BA, PtrVT, BASDN->getOffset());
    return getTOCEntry(DAG, SDLoc(BASDN), Subtarget.isPPC64(), GA);
  }

  unsigned MOHiFlag, MOLoFlag;
  bool IsPIC = isPositionIndependent();
  getLabelAccessInfo(IsPIC, Subtarget, MOHiFlag, MOLoFlag);
  SDValue TgtBAHi = DAG.getTargetBlockAddress(BA, PtrVT, 0, MOHiFlag);
  SDValue TgtBALo = DAG.getTargetBlockAddress(BA, PtrVT, 0, MOLoFlag);
  return LowerLabelRef(TgtBAHi, TgtBALo, IsPIC, DAG);
}

SDValue PPCTargetLowering::LowerGlobalTLSAddress(SDValue Op,
                                              SelectionDAG &DAG) const {
  // FIXME: TLS addresses currently use medium model code sequences,
  // which is the most useful form.  Eventually support for small and
  // large models could be added if users need it, at the cost of
  // additional complexity.
  GlobalAddressSDNode *GA = cast<GlobalAddressSDNode>(Op);
  if (DAG.getTarget().useEmulatedTLS())
    return LowerToTLSEmulatedModel(GA, DAG);

  SDLoc dl(GA);
  const GlobalValue *GV = GA->getGlobal();
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  bool is64bit = Subtarget.isPPC64();
  const Module *M = DAG.getMachineFunction().getFunction().getParent();
  PICLevel::Level picLevel = M->getPICLevel();

  TLSModel::Model Model = getTargetMachine().getTLSModel(GV);

  if (Model == TLSModel::LocalExec) {
    SDValue TGAHi = DAG.getTargetGlobalAddress(GV, dl, PtrVT, 0,
                                               PPCII::MO_TPREL_HA);
    SDValue TGALo = DAG.getTargetGlobalAddress(GV, dl, PtrVT, 0,
                                               PPCII::MO_TPREL_LO);
    SDValue TLSReg = is64bit ? DAG.getRegister(PPC::X13, MVT::i64)
                             : DAG.getRegister(PPC::R2, MVT::i32);

    SDValue Hi = DAG.getNode(PPCISD::Hi, dl, PtrVT, TGAHi, TLSReg);
    return DAG.getNode(PPCISD::Lo, dl, PtrVT, TGALo, Hi);
  }

  if (Model == TLSModel::InitialExec) {
    SDValue TGA = DAG.getTargetGlobalAddress(GV, dl, PtrVT, 0, 0);
    SDValue TGATLS = DAG.getTargetGlobalAddress(GV, dl, PtrVT, 0,
                                                PPCII::MO_TLS);
    SDValue GOTPtr;
    if (is64bit) {
      setUsesTOCBasePtr(DAG);
      SDValue GOTReg = DAG.getRegister(PPC::X2, MVT::i64);
      GOTPtr = DAG.getNode(PPCISD::ADDIS_GOT_TPREL_HA, dl,
                           PtrVT, GOTReg, TGA);
    } else
      GOTPtr = DAG.getNode(PPCISD::PPC32_GOT, dl, PtrVT);
    SDValue TPOffset = DAG.getNode(PPCISD::LD_GOT_TPREL_L, dl,
                                   PtrVT, TGA, GOTPtr);
    return DAG.getNode(PPCISD::ADD_TLS, dl, PtrVT, TPOffset, TGATLS);
  }

  if (Model == TLSModel::GeneralDynamic) {
    SDValue TGA = DAG.getTargetGlobalAddress(GV, dl, PtrVT, 0, 0);
    SDValue GOTPtr;
    if (is64bit) {
      setUsesTOCBasePtr(DAG);
      SDValue GOTReg = DAG.getRegister(PPC::X2, MVT::i64);
      GOTPtr = DAG.getNode(PPCISD::ADDIS_TLSGD_HA, dl, PtrVT,
                                   GOTReg, TGA);
    } else {
      if (picLevel == PICLevel::SmallPIC)
        GOTPtr = DAG.getNode(PPCISD::GlobalBaseReg, dl, PtrVT);
      else
        GOTPtr = DAG.getNode(PPCISD::PPC32_PICGOT, dl, PtrVT);
    }
    return DAG.getNode(PPCISD::ADDI_TLSGD_L_ADDR, dl, PtrVT,
                       GOTPtr, TGA, TGA);
  }

  if (Model == TLSModel::LocalDynamic) {
    SDValue TGA = DAG.getTargetGlobalAddress(GV, dl, PtrVT, 0, 0);
    SDValue GOTPtr;
    if (is64bit) {
      setUsesTOCBasePtr(DAG);
      SDValue GOTReg = DAG.getRegister(PPC::X2, MVT::i64);
      GOTPtr = DAG.getNode(PPCISD::ADDIS_TLSLD_HA, dl, PtrVT,
                           GOTReg, TGA);
    } else {
      if (picLevel == PICLevel::SmallPIC)
        GOTPtr = DAG.getNode(PPCISD::GlobalBaseReg, dl, PtrVT);
      else
        GOTPtr = DAG.getNode(PPCISD::PPC32_PICGOT, dl, PtrVT);
    }
    SDValue TLSAddr = DAG.getNode(PPCISD::ADDI_TLSLD_L_ADDR, dl,
                                  PtrVT, GOTPtr, TGA, TGA);
    SDValue DtvOffsetHi = DAG.getNode(PPCISD::ADDIS_DTPREL_HA, dl,
                                      PtrVT, TLSAddr, TGA);
    return DAG.getNode(PPCISD::ADDI_DTPREL_L, dl, PtrVT, DtvOffsetHi, TGA);
  }

  llvm_unreachable("Unknown TLS model!");
}

SDValue PPCTargetLowering::LowerGlobalAddress(SDValue Op,
                                              SelectionDAG &DAG) const {
  EVT PtrVT = Op.getValueType();
  GlobalAddressSDNode *GSDN = cast<GlobalAddressSDNode>(Op);
  SDLoc DL(GSDN);
  const GlobalValue *GV = GSDN->getGlobal();

  // 64-bit SVR4 ABI code is always position-independent.
  // The actual address of the GlobalValue is stored in the TOC.
  if (Subtarget.isSVR4ABI() && Subtarget.isPPC64()) {
    setUsesTOCBasePtr(DAG);
    SDValue GA = DAG.getTargetGlobalAddress(GV, DL, PtrVT, GSDN->getOffset());
    return getTOCEntry(DAG, DL, true, GA);
  }

  unsigned MOHiFlag, MOLoFlag;
  bool IsPIC = isPositionIndependent();
  getLabelAccessInfo(IsPIC, Subtarget, MOHiFlag, MOLoFlag, GV);

  if (IsPIC && Subtarget.isSVR4ABI()) {
    SDValue GA = DAG.getTargetGlobalAddress(GV, DL, PtrVT,
                                            GSDN->getOffset(),
                                            PPCII::MO_PIC_FLAG);
    return getTOCEntry(DAG, DL, false, GA);
  }

  SDValue GAHi =
    DAG.getTargetGlobalAddress(GV, DL, PtrVT, GSDN->getOffset(), MOHiFlag);
  SDValue GALo =
    DAG.getTargetGlobalAddress(GV, DL, PtrVT, GSDN->getOffset(), MOLoFlag);

  SDValue Ptr = LowerLabelRef(GAHi, GALo, IsPIC, DAG);

  // If the global reference is actually to a non-lazy-pointer, we have to do an
  // extra load to get the address of the global.
  if (MOHiFlag & PPCII::MO_NLP_FLAG)
    Ptr = DAG.getLoad(PtrVT, DL, DAG.getEntryNode(), Ptr, MachinePointerInfo());
  return Ptr;
}

SDValue PPCTargetLowering::LowerSETCC(SDValue Op, SelectionDAG &DAG) const {
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(2))->get();
  SDLoc dl(Op);

  if (Op.getValueType() == MVT::v2i64) {
    // When the operands themselves are v2i64 values, we need to do something
    // special because VSX has no underlying comparison operations for these.
    if (Op.getOperand(0).getValueType() == MVT::v2i64) {
      // Equality can be handled by casting to the legal type for Altivec
      // comparisons, everything else needs to be expanded.
      if (CC == ISD::SETEQ || CC == ISD::SETNE) {
        return DAG.getNode(ISD::BITCAST, dl, MVT::v2i64,
                 DAG.getSetCC(dl, MVT::v4i32,
                   DAG.getNode(ISD::BITCAST, dl, MVT::v4i32, Op.getOperand(0)),
                   DAG.getNode(ISD::BITCAST, dl, MVT::v4i32, Op.getOperand(1)),
                   CC));
      }

      return SDValue();
    }

    // We handle most of these in the usual way.
    return Op;
  }

  // If we're comparing for equality to zero, expose the fact that this is
  // implemented as a ctlz/srl pair on ppc, so that the dag combiner can
  // fold the new nodes.
  if (SDValue V = lowerCmpEqZeroToCtlzSrl(Op, DAG))
    return V;

  if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(Op.getOperand(1))) {
    // Leave comparisons against 0 and -1 alone for now, since they're usually
    // optimized.  FIXME: revisit this when we can custom lower all setcc
    // optimizations.
    if (C->isAllOnesValue() || C->isNullValue())
      return SDValue();
  }

  // If we have an integer seteq/setne, turn it into a compare against zero
  // by xor'ing the rhs with the lhs, which is faster than setting a
  // condition register, reading it back out, and masking the correct bit.  The
  // normal approach here uses sub to do this instead of xor.  Using xor exposes
  // the result to other bit-twiddling opportunities.
  EVT LHSVT = Op.getOperand(0).getValueType();
  if (LHSVT.isInteger() && (CC == ISD::SETEQ || CC == ISD::SETNE)) {
    EVT VT = Op.getValueType();
    SDValue Sub = DAG.getNode(ISD::XOR, dl, LHSVT, Op.getOperand(0),
                                Op.getOperand(1));
    return DAG.getSetCC(dl, VT, Sub, DAG.getConstant(0, dl, LHSVT), CC);
  }
  return SDValue();
}

SDValue PPCTargetLowering::LowerVAARG(SDValue Op, SelectionDAG &DAG) const {
  SDNode *Node = Op.getNode();
  EVT VT = Node->getValueType(0);
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  SDValue InChain = Node->getOperand(0);
  SDValue VAListPtr = Node->getOperand(1);
  const Value *SV = cast<SrcValueSDNode>(Node->getOperand(2))->getValue();
  SDLoc dl(Node);

  assert(!Subtarget.isPPC64() && "LowerVAARG is PPC32 only");

  // gpr_index
  SDValue GprIndex = DAG.getExtLoad(ISD::ZEXTLOAD, dl, MVT::i32, InChain,
                                    VAListPtr, MachinePointerInfo(SV), MVT::i8);
  InChain = GprIndex.getValue(1);

  if (VT == MVT::i64) {
    // Check if GprIndex is even
    SDValue GprAnd = DAG.getNode(ISD::AND, dl, MVT::i32, GprIndex,
                                 DAG.getConstant(1, dl, MVT::i32));
    SDValue CC64 = DAG.getSetCC(dl, MVT::i32, GprAnd,
                                DAG.getConstant(0, dl, MVT::i32), ISD::SETNE);
    SDValue GprIndexPlusOne = DAG.getNode(ISD::ADD, dl, MVT::i32, GprIndex,
                                          DAG.getConstant(1, dl, MVT::i32));
    // Align GprIndex to be even if it isn't
    GprIndex = DAG.getNode(ISD::SELECT, dl, MVT::i32, CC64, GprIndexPlusOne,
                           GprIndex);
  }

  // fpr index is 1 byte after gpr
  SDValue FprPtr = DAG.getNode(ISD::ADD, dl, PtrVT, VAListPtr,
                               DAG.getConstant(1, dl, MVT::i32));

  // fpr
  SDValue FprIndex = DAG.getExtLoad(ISD::ZEXTLOAD, dl, MVT::i32, InChain,
                                    FprPtr, MachinePointerInfo(SV), MVT::i8);
  InChain = FprIndex.getValue(1);

  SDValue RegSaveAreaPtr = DAG.getNode(ISD::ADD, dl, PtrVT, VAListPtr,
                                       DAG.getConstant(8, dl, MVT::i32));

  SDValue OverflowAreaPtr = DAG.getNode(ISD::ADD, dl, PtrVT, VAListPtr,
                                        DAG.getConstant(4, dl, MVT::i32));

  // areas
  SDValue OverflowArea =
      DAG.getLoad(MVT::i32, dl, InChain, OverflowAreaPtr, MachinePointerInfo());
  InChain = OverflowArea.getValue(1);

  SDValue RegSaveArea =
      DAG.getLoad(MVT::i32, dl, InChain, RegSaveAreaPtr, MachinePointerInfo());
  InChain = RegSaveArea.getValue(1);

  // select overflow_area if index > 8
  SDValue CC = DAG.getSetCC(dl, MVT::i32, VT.isInteger() ? GprIndex : FprIndex,
                            DAG.getConstant(8, dl, MVT::i32), ISD::SETLT);

  // adjustment constant gpr_index * 4/8
  SDValue RegConstant = DAG.getNode(ISD::MUL, dl, MVT::i32,
                                    VT.isInteger() ? GprIndex : FprIndex,
                                    DAG.getConstant(VT.isInteger() ? 4 : 8, dl,
                                                    MVT::i32));

  // OurReg = RegSaveArea + RegConstant
  SDValue OurReg = DAG.getNode(ISD::ADD, dl, PtrVT, RegSaveArea,
                               RegConstant);

  // Floating types are 32 bytes into RegSaveArea
  if (VT.isFloatingPoint())
    OurReg = DAG.getNode(ISD::ADD, dl, PtrVT, OurReg,
                         DAG.getConstant(32, dl, MVT::i32));

  // increase {f,g}pr_index by 1 (or 2 if VT is i64)
  SDValue IndexPlus1 = DAG.getNode(ISD::ADD, dl, MVT::i32,
                                   VT.isInteger() ? GprIndex : FprIndex,
                                   DAG.getConstant(VT == MVT::i64 ? 2 : 1, dl,
                                                   MVT::i32));

  InChain = DAG.getTruncStore(InChain, dl, IndexPlus1,
                              VT.isInteger() ? VAListPtr : FprPtr,
                              MachinePointerInfo(SV), MVT::i8);

  // determine if we should load from reg_save_area or overflow_area
  SDValue Result = DAG.getNode(ISD::SELECT, dl, PtrVT, CC, OurReg, OverflowArea);

  // increase overflow_area by 4/8 if gpr/fpr > 8
  SDValue OverflowAreaPlusN = DAG.getNode(ISD::ADD, dl, PtrVT, OverflowArea,
                                          DAG.getConstant(VT.isInteger() ? 4 : 8,
                                          dl, MVT::i32));

  OverflowArea = DAG.getNode(ISD::SELECT, dl, MVT::i32, CC, OverflowArea,
                             OverflowAreaPlusN);

  InChain = DAG.getTruncStore(InChain, dl, OverflowArea, OverflowAreaPtr,
                              MachinePointerInfo(), MVT::i32);

  return DAG.getLoad(VT, dl, InChain, Result, MachinePointerInfo());
}

SDValue PPCTargetLowering::LowerVACOPY(SDValue Op, SelectionDAG &DAG) const {
  assert(!Subtarget.isPPC64() && "LowerVACOPY is PPC32 only");

  // We have to copy the entire va_list struct:
  // 2*sizeof(char) + 2 Byte alignment + 2*sizeof(char*) = 12 Byte
  return DAG.getMemcpy(Op.getOperand(0), Op,
                       Op.getOperand(1), Op.getOperand(2),
                       DAG.getConstant(12, SDLoc(Op), MVT::i32), 8, false, true,
                       false, MachinePointerInfo(), MachinePointerInfo());
}

SDValue PPCTargetLowering::LowerADJUST_TRAMPOLINE(SDValue Op,
                                                  SelectionDAG &DAG) const {
  return Op.getOperand(0);
}

SDValue PPCTargetLowering::LowerINIT_TRAMPOLINE(SDValue Op,
                                                SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  SDValue Trmp = Op.getOperand(1); // trampoline
  SDValue FPtr = Op.getOperand(2); // nested function
  SDValue Nest = Op.getOperand(3); // 'nest' parameter value
  SDLoc dl(Op);

  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  bool isPPC64 = (PtrVT == MVT::i64);
  Type *IntPtrTy = DAG.getDataLayout().getIntPtrType(*DAG.getContext());

  TargetLowering::ArgListTy Args;
  TargetLowering::ArgListEntry Entry;

  Entry.Ty = IntPtrTy;
  Entry.Node = Trmp; Args.push_back(Entry);

  // TrampSize == (isPPC64 ? 48 : 40);
  Entry.Node = DAG.getConstant(isPPC64 ? 48 : 40, dl,
                               isPPC64 ? MVT::i64 : MVT::i32);
  Args.push_back(Entry);

  Entry.Node = FPtr; Args.push_back(Entry);
  Entry.Node = Nest; Args.push_back(Entry);

  // Lower to a call to __trampoline_setup(Trmp, TrampSize, FPtr, ctx_reg)
  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(dl).setChain(Chain).setLibCallee(
      CallingConv::C, Type::getVoidTy(*DAG.getContext()),
      DAG.getExternalSymbol("__trampoline_setup", PtrVT), std::move(Args));

  std::pair<SDValue, SDValue> CallResult = LowerCallTo(CLI);
  return CallResult.second;
}

SDValue PPCTargetLowering::LowerVASTART(SDValue Op, SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  PPCFunctionInfo *FuncInfo = MF.getInfo<PPCFunctionInfo>();
  EVT PtrVT = getPointerTy(MF.getDataLayout());

  SDLoc dl(Op);

  if (Subtarget.isDarwinABI() || Subtarget.isPPC64()) {
    // vastart just stores the address of the VarArgsFrameIndex slot into the
    // memory location argument.
    SDValue FR = DAG.getFrameIndex(FuncInfo->getVarArgsFrameIndex(), PtrVT);
    const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
    return DAG.getStore(Op.getOperand(0), dl, FR, Op.getOperand(1),
                        MachinePointerInfo(SV));
  }

  // For the 32-bit SVR4 ABI we follow the layout of the va_list struct.
  // We suppose the given va_list is already allocated.
  //
  // typedef struct {
  //  char gpr;     /* index into the array of 8 GPRs
  //                 * stored in the register save area
  //                 * gpr=0 corresponds to r3,
  //                 * gpr=1 to r4, etc.
  //                 */
  //  char fpr;     /* index into the array of 8 FPRs
  //                 * stored in the register save area
  //                 * fpr=0 corresponds to f1,
  //                 * fpr=1 to f2, etc.
  //                 */
  //  char *overflow_arg_area;
  //                /* location on stack that holds
  //                 * the next overflow argument
  //                 */
  //  char *reg_save_area;
  //               /* where r3:r10 and f1:f8 (if saved)
  //                * are stored
  //                */
  // } va_list[1];

  SDValue ArgGPR = DAG.getConstant(FuncInfo->getVarArgsNumGPR(), dl, MVT::i32);
  SDValue ArgFPR = DAG.getConstant(FuncInfo->getVarArgsNumFPR(), dl, MVT::i32);
  SDValue StackOffsetFI = DAG.getFrameIndex(FuncInfo->getVarArgsStackOffset(),
                                            PtrVT);
  SDValue FR = DAG.getFrameIndex(FuncInfo->getVarArgsFrameIndex(),
                                 PtrVT);

  uint64_t FrameOffset = PtrVT.getSizeInBits()/8;
  SDValue ConstFrameOffset = DAG.getConstant(FrameOffset, dl, PtrVT);

  uint64_t StackOffset = PtrVT.getSizeInBits()/8 - 1;
  SDValue ConstStackOffset = DAG.getConstant(StackOffset, dl, PtrVT);

  uint64_t FPROffset = 1;
  SDValue ConstFPROffset = DAG.getConstant(FPROffset, dl, PtrVT);

  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();

  // Store first byte : number of int regs
  SDValue firstStore =
      DAG.getTruncStore(Op.getOperand(0), dl, ArgGPR, Op.getOperand(1),
                        MachinePointerInfo(SV), MVT::i8);
  uint64_t nextOffset = FPROffset;
  SDValue nextPtr = DAG.getNode(ISD::ADD, dl, PtrVT, Op.getOperand(1),
                                  ConstFPROffset);

  // Store second byte : number of float regs
  SDValue secondStore =
      DAG.getTruncStore(firstStore, dl, ArgFPR, nextPtr,
                        MachinePointerInfo(SV, nextOffset), MVT::i8);
  nextOffset += StackOffset;
  nextPtr = DAG.getNode(ISD::ADD, dl, PtrVT, nextPtr, ConstStackOffset);

  // Store second word : arguments given on stack
  SDValue thirdStore = DAG.getStore(secondStore, dl, StackOffsetFI, nextPtr,
                                    MachinePointerInfo(SV, nextOffset));
  nextOffset += FrameOffset;
  nextPtr = DAG.getNode(ISD::ADD, dl, PtrVT, nextPtr, ConstFrameOffset);

  // Store third word : arguments given in registers
  return DAG.getStore(thirdStore, dl, FR, nextPtr,
                      MachinePointerInfo(SV, nextOffset));
}

#include "PPCGenCallingConv.inc"

// Function whose sole purpose is to kill compiler warnings
// stemming from unused functions included from PPCGenCallingConv.inc.
CCAssignFn *PPCTargetLowering::useFastISelCCs(unsigned Flag) const {
  return Flag ? CC_PPC64_ELF_FIS : RetCC_PPC64_ELF_FIS;
}

bool llvm::CC_PPC32_SVR4_Custom_Dummy(unsigned &ValNo, MVT &ValVT, MVT &LocVT,
                                      CCValAssign::LocInfo &LocInfo,
                                      ISD::ArgFlagsTy &ArgFlags,
                                      CCState &State) {
  return true;
}

bool llvm::CC_PPC32_SVR4_Custom_AlignArgRegs(unsigned &ValNo, MVT &ValVT,
                                             MVT &LocVT,
                                             CCValAssign::LocInfo &LocInfo,
                                             ISD::ArgFlagsTy &ArgFlags,
                                             CCState &State) {
  static const MCPhysReg ArgRegs[] = {
    PPC::R3, PPC::R4, PPC::R5, PPC::R6,
    PPC::R7, PPC::R8, PPC::R9, PPC::R10,
  };
  const unsigned NumArgRegs = array_lengthof(ArgRegs);

  unsigned RegNum = State.getFirstUnallocated(ArgRegs);

  // Skip one register if the first unallocated register has an even register
  // number and there are still argument registers available which have not been
  // allocated yet. RegNum is actually an index into ArgRegs, which means we
  // need to skip a register if RegNum is odd.
  if (RegNum != NumArgRegs && RegNum % 2 == 1) {
    State.AllocateReg(ArgRegs[RegNum]);
  }

  // Always return false here, as this function only makes sure that the first
  // unallocated register has an odd register number and does not actually
  // allocate a register for the current argument.
  return false;
}

bool
llvm::CC_PPC32_SVR4_Custom_SkipLastArgRegsPPCF128(unsigned &ValNo, MVT &ValVT,
                                                  MVT &LocVT,
                                                  CCValAssign::LocInfo &LocInfo,
                                                  ISD::ArgFlagsTy &ArgFlags,
                                                  CCState &State) {
  static const MCPhysReg ArgRegs[] = {
    PPC::R3, PPC::R4, PPC::R5, PPC::R6,
    PPC::R7, PPC::R8, PPC::R9, PPC::R10,
  };
  const unsigned NumArgRegs = array_lengthof(ArgRegs);

  unsigned RegNum = State.getFirstUnallocated(ArgRegs);
  int RegsLeft = NumArgRegs - RegNum;

  // Skip if there is not enough registers left for long double type (4 gpr regs
  // in soft float mode) and put long double argument on the stack.
  if (RegNum != NumArgRegs && RegsLeft < 4) {
    for (int i = 0; i < RegsLeft; i++) {
      State.AllocateReg(ArgRegs[RegNum + i]);
    }
  }

  return false;
}

bool llvm::CC_PPC32_SVR4_Custom_AlignFPArgRegs(unsigned &ValNo, MVT &ValVT,
                                               MVT &LocVT,
                                               CCValAssign::LocInfo &LocInfo,
                                               ISD::ArgFlagsTy &ArgFlags,
                                               CCState &State) {
  static const MCPhysReg ArgRegs[] = {
    PPC::F1, PPC::F2, PPC::F3, PPC::F4, PPC::F5, PPC::F6, PPC::F7,
    PPC::F8
  };

  const unsigned NumArgRegs = array_lengthof(ArgRegs);

  unsigned RegNum = State.getFirstUnallocated(ArgRegs);

  // If there is only one Floating-point register left we need to put both f64
  // values of a split ppc_fp128 value on the stack.
  if (RegNum != NumArgRegs && ArgRegs[RegNum] == PPC::F8) {
    State.AllocateReg(ArgRegs[RegNum]);
  }

  // Always return false here, as this function only makes sure that the two f64
  // values a ppc_fp128 value is split into are both passed in registers or both
  // passed on the stack and does not actually allocate a register for the
  // current argument.
  return false;
}

/// FPR - The set of FP registers that should be allocated for arguments,
/// on Darwin.
static const MCPhysReg FPR[] = {PPC::F1,  PPC::F2,  PPC::F3, PPC::F4, PPC::F5,
                                PPC::F6,  PPC::F7,  PPC::F8, PPC::F9, PPC::F10,
                                PPC::F11, PPC::F12, PPC::F13};

/// QFPR - The set of QPX registers that should be allocated for arguments.
static const MCPhysReg QFPR[] = {
    PPC::QF1, PPC::QF2, PPC::QF3,  PPC::QF4,  PPC::QF5,  PPC::QF6, PPC::QF7,
    PPC::QF8, PPC::QF9, PPC::QF10, PPC::QF11, PPC::QF12, PPC::QF13};

/// CalculateStackSlotSize - Calculates the size reserved for this argument on
/// the stack.
static unsigned CalculateStackSlotSize(EVT ArgVT, ISD::ArgFlagsTy Flags,
                                       unsigned PtrByteSize) {
  unsigned ArgSize = ArgVT.getStoreSize();
  if (Flags.isByVal())
    ArgSize = Flags.getByValSize();

  // Round up to multiples of the pointer size, except for array members,
  // which are always packed.
  if (!Flags.isInConsecutiveRegs())
    ArgSize = ((ArgSize + PtrByteSize - 1)/PtrByteSize) * PtrByteSize;

  return ArgSize;
}

/// CalculateStackSlotAlignment - Calculates the alignment of this argument
/// on the stack.
static unsigned CalculateStackSlotAlignment(EVT ArgVT, EVT OrigVT,
                                            ISD::ArgFlagsTy Flags,
                                            unsigned PtrByteSize) {
  unsigned Align = PtrByteSize;

  // Altivec parameters are padded to a 16 byte boundary.
  if (ArgVT == MVT::v4f32 || ArgVT == MVT::v4i32 ||
      ArgVT == MVT::v8i16 || ArgVT == MVT::v16i8 ||
      ArgVT == MVT::v2f64 || ArgVT == MVT::v2i64 ||
      ArgVT == MVT::v1i128 || ArgVT == MVT::f128)
    Align = 16;
  // QPX vector types stored in double-precision are padded to a 32 byte
  // boundary.
  else if (ArgVT == MVT::v4f64 || ArgVT == MVT::v4i1)
    Align = 32;

  // ByVal parameters are aligned as requested.
  if (Flags.isByVal()) {
    unsigned BVAlign = Flags.getByValAlign();
    if (BVAlign > PtrByteSize) {
      if (BVAlign % PtrByteSize != 0)
          llvm_unreachable(
            "ByVal alignment is not a multiple of the pointer size");

      Align = BVAlign;
    }
  }

  // Array members are always packed to their original alignment.
  if (Flags.isInConsecutiveRegs()) {
    // If the array member was split into multiple registers, the first
    // needs to be aligned to the size of the full type.  (Except for
    // ppcf128, which is only aligned as its f64 components.)
    if (Flags.isSplit() && OrigVT != MVT::ppcf128)
      Align = OrigVT.getStoreSize();
    else
      Align = ArgVT.getStoreSize();
  }

  return Align;
}

/// CalculateStackSlotUsed - Return whether this argument will use its
/// stack slot (instead of being passed in registers).  ArgOffset,
/// AvailableFPRs, and AvailableVRs must hold the current argument
/// position, and will be updated to account for this argument.
static bool CalculateStackSlotUsed(EVT ArgVT, EVT OrigVT,
                                   ISD::ArgFlagsTy Flags,
                                   unsigned PtrByteSize,
                                   unsigned LinkageSize,
                                   unsigned ParamAreaSize,
                                   unsigned &ArgOffset,
                                   unsigned &AvailableFPRs,
                                   unsigned &AvailableVRs, bool HasQPX) {
  bool UseMemory = false;

  // Respect alignment of argument on the stack.
  unsigned Align =
    CalculateStackSlotAlignment(ArgVT, OrigVT, Flags, PtrByteSize);
  ArgOffset = ((ArgOffset + Align - 1) / Align) * Align;
  // If there's no space left in the argument save area, we must
  // use memory (this check also catches zero-sized arguments).
  if (ArgOffset >= LinkageSize + ParamAreaSize)
    UseMemory = true;

  // Allocate argument on the stack.
  ArgOffset += CalculateStackSlotSize(ArgVT, Flags, PtrByteSize);
  if (Flags.isInConsecutiveRegsLast())
    ArgOffset = ((ArgOffset + PtrByteSize - 1)/PtrByteSize) * PtrByteSize;
  // If we overran the argument save area, we must use memory
  // (this check catches arguments passed partially in memory)
  if (ArgOffset > LinkageSize + ParamAreaSize)
    UseMemory = true;

  // However, if the argument is actually passed in an FPR or a VR,
  // we don't use memory after all.
  if (!Flags.isByVal()) {
    if (ArgVT == MVT::f32 || ArgVT == MVT::f64 ||
        // QPX registers overlap with the scalar FP registers.
        (HasQPX && (ArgVT == MVT::v4f32 ||
                    ArgVT == MVT::v4f64 ||
                    ArgVT == MVT::v4i1)))
      if (AvailableFPRs > 0) {
        --AvailableFPRs;
        return false;
      }
    if (ArgVT == MVT::v4f32 || ArgVT == MVT::v4i32 ||
        ArgVT == MVT::v8i16 || ArgVT == MVT::v16i8 ||
        ArgVT == MVT::v2f64 || ArgVT == MVT::v2i64 ||
        ArgVT == MVT::v1i128 || ArgVT == MVT::f128)
      if (AvailableVRs > 0) {
        --AvailableVRs;
        return false;
      }
  }

  return UseMemory;
}

/// EnsureStackAlignment - Round stack frame size up from NumBytes to
/// ensure minimum alignment required for target.
static unsigned EnsureStackAlignment(const PPCFrameLowering *Lowering,
                                     unsigned NumBytes) {
  unsigned TargetAlign = Lowering->getStackAlignment();
  unsigned AlignMask = TargetAlign - 1;
  NumBytes = (NumBytes + AlignMask) & ~AlignMask;
  return NumBytes;
}

SDValue PPCTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  if (Subtarget.isSVR4ABI()) {
    if (Subtarget.isPPC64())
      return LowerFormalArguments_64SVR4(Chain, CallConv, isVarArg, Ins,
                                         dl, DAG, InVals);
    else
      return LowerFormalArguments_32SVR4(Chain, CallConv, isVarArg, Ins,
                                         dl, DAG, InVals);
  } else {
    return LowerFormalArguments_Darwin(Chain, CallConv, isVarArg, Ins,
                                       dl, DAG, InVals);
  }
}

SDValue PPCTargetLowering::LowerFormalArguments_32SVR4(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {

  // 32-bit SVR4 ABI Stack Frame Layout:
  //              +-----------------------------------+
  //        +-->  |            Back chain             |
  //        |     +-----------------------------------+
  //        |     | Floating-point register save area |
  //        |     +-----------------------------------+
  //        |     |    General register save area     |
  //        |     +-----------------------------------+
  //        |     |          CR save word             |
  //        |     +-----------------------------------+
  //        |     |         VRSAVE save word          |
  //        |     +-----------------------------------+
  //        |     |         Alignment padding         |
  //        |     +-----------------------------------+
  //        |     |     Vector register save area     |
  //        |     +-----------------------------------+
  //        |     |       Local variable space        |
  //        |     +-----------------------------------+
  //        |     |        Parameter list area        |
  //        |     +-----------------------------------+
  //        |     |           LR save word            |
  //        |     +-----------------------------------+
  // SP-->  +---  |            Back chain             |
  //              +-----------------------------------+
  //
  // Specifications:
  //   System V Application Binary Interface PowerPC Processor Supplement
  //   AltiVec Technology Programming Interface Manual

  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  PPCFunctionInfo *FuncInfo = MF.getInfo<PPCFunctionInfo>();

  EVT PtrVT = getPointerTy(MF.getDataLayout());
  // Potential tail calls could cause overwriting of argument stack slots.
  bool isImmutable = !(getTargetMachine().Options.GuaranteedTailCallOpt &&
                       (CallConv == CallingConv::Fast));
  unsigned PtrByteSize = 4;

  // Assign locations to all of the incoming arguments.
  SmallVector<CCValAssign, 16> ArgLocs;
  PPCCCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), ArgLocs,
                 *DAG.getContext());

  // Reserve space for the linkage area on the stack.
  unsigned LinkageSize = Subtarget.getFrameLowering()->getLinkageSize();
  CCInfo.AllocateStack(LinkageSize, PtrByteSize);
  if (useSoftFloat() || hasSPE())
    CCInfo.PreAnalyzeFormalArguments(Ins);

  CCInfo.AnalyzeFormalArguments(Ins, CC_PPC32_SVR4);
  CCInfo.clearWasPPCF128();

  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    CCValAssign &VA = ArgLocs[i];

    // Arguments stored in registers.
    if (VA.isRegLoc()) {
      const TargetRegisterClass *RC;
      EVT ValVT = VA.getValVT();

      switch (ValVT.getSimpleVT().SimpleTy) {
        default:
          llvm_unreachable("ValVT not supported by formal arguments Lowering");
        case MVT::i1:
        case MVT::i32:
          RC = &PPC::GPRCRegClass;
          break;
        case MVT::f32:
          if (Subtarget.hasP8Vector())
            RC = &PPC::VSSRCRegClass;
          else if (Subtarget.hasSPE())
            RC = &PPC::SPE4RCRegClass;
          else
            RC = &PPC::F4RCRegClass;
          break;
        case MVT::f64:
          if (Subtarget.hasVSX())
            RC = &PPC::VSFRCRegClass;
          else if (Subtarget.hasSPE())
            RC = &PPC::SPERCRegClass;
          else
            RC = &PPC::F8RCRegClass;
          break;
        case MVT::v16i8:
        case MVT::v8i16:
        case MVT::v4i32:
          RC = &PPC::VRRCRegClass;
          break;
        case MVT::v4f32:
          RC = Subtarget.hasQPX() ? &PPC::QSRCRegClass : &PPC::VRRCRegClass;
          break;
        case MVT::v2f64:
        case MVT::v2i64:
          RC = &PPC::VRRCRegClass;
          break;
        case MVT::v4f64:
          RC = &PPC::QFRCRegClass;
          break;
        case MVT::v4i1:
          RC = &PPC::QBRCRegClass;
          break;
      }

      // Transform the arguments stored in physical registers into virtual ones.
      unsigned Reg = MF.addLiveIn(VA.getLocReg(), RC);
      SDValue ArgValue = DAG.getCopyFromReg(Chain, dl, Reg,
                                            ValVT == MVT::i1 ? MVT::i32 : ValVT);

      if (ValVT == MVT::i1)
        ArgValue = DAG.getNode(ISD::TRUNCATE, dl, MVT::i1, ArgValue);

      InVals.push_back(ArgValue);
    } else {
      // Argument stored in memory.
      assert(VA.isMemLoc());

      // Get the extended size of the argument type in stack
      unsigned ArgSize = VA.getLocVT().getStoreSize();
      // Get the actual size of the argument type
      unsigned ObjSize = VA.getValVT().getStoreSize();
      unsigned ArgOffset = VA.getLocMemOffset();
      // Stack objects in PPC32 are right justified.
      ArgOffset += ArgSize - ObjSize;
      int FI = MFI.CreateFixedObject(ArgSize, ArgOffset, isImmutable);

      // Create load nodes to retrieve arguments from the stack.
      SDValue FIN = DAG.getFrameIndex(FI, PtrVT);
      InVals.push_back(
          DAG.getLoad(VA.getValVT(), dl, Chain, FIN, MachinePointerInfo()));
    }
  }

  // Assign locations to all of the incoming aggregate by value arguments.
  // Aggregates passed by value are stored in the local variable space of the
  // caller's stack frame, right above the parameter list area.
  SmallVector<CCValAssign, 16> ByValArgLocs;
  CCState CCByValInfo(CallConv, isVarArg, DAG.getMachineFunction(),
                      ByValArgLocs, *DAG.getContext());

  // Reserve stack space for the allocations in CCInfo.
  CCByValInfo.AllocateStack(CCInfo.getNextStackOffset(), PtrByteSize);

  CCByValInfo.AnalyzeFormalArguments(Ins, CC_PPC32_SVR4_ByVal);

  // Area that is at least reserved in the caller of this function.
  unsigned MinReservedArea = CCByValInfo.getNextStackOffset();
  MinReservedArea = std::max(MinReservedArea, LinkageSize);

  // Set the size that is at least reserved in caller of this function.  Tail
  // call optimized function's reserved stack space needs to be aligned so that
  // taking the difference between two stack areas will result in an aligned
  // stack.
  MinReservedArea =
      EnsureStackAlignment(Subtarget.getFrameLowering(), MinReservedArea);
  FuncInfo->setMinReservedArea(MinReservedArea);

  SmallVector<SDValue, 8> MemOps;

  // If the function takes variable number of arguments, make a frame index for
  // the start of the first vararg value... for expansion of llvm.va_start.
  if (isVarArg) {
    static const MCPhysReg GPArgRegs[] = {
      PPC::R3, PPC::R4, PPC::R5, PPC::R6,
      PPC::R7, PPC::R8, PPC::R9, PPC::R10,
    };
    const unsigned NumGPArgRegs = array_lengthof(GPArgRegs);

    static const MCPhysReg FPArgRegs[] = {
      PPC::F1, PPC::F2, PPC::F3, PPC::F4, PPC::F5, PPC::F6, PPC::F7,
      PPC::F8
    };
    unsigned NumFPArgRegs = array_lengthof(FPArgRegs);

    if (useSoftFloat() || hasSPE())
       NumFPArgRegs = 0;

    FuncInfo->setVarArgsNumGPR(CCInfo.getFirstUnallocated(GPArgRegs));
    FuncInfo->setVarArgsNumFPR(CCInfo.getFirstUnallocated(FPArgRegs));

    // Make room for NumGPArgRegs and NumFPArgRegs.
    int Depth = NumGPArgRegs * PtrVT.getSizeInBits()/8 +
                NumFPArgRegs * MVT(MVT::f64).getSizeInBits()/8;

    FuncInfo->setVarArgsStackOffset(
      MFI.CreateFixedObject(PtrVT.getSizeInBits()/8,
                            CCInfo.getNextStackOffset(), true));

    FuncInfo->setVarArgsFrameIndex(MFI.CreateStackObject(Depth, 8, false));
    SDValue FIN = DAG.getFrameIndex(FuncInfo->getVarArgsFrameIndex(), PtrVT);

    // The fixed integer arguments of a variadic function are stored to the
    // VarArgsFrameIndex on the stack so that they may be loaded by
    // dereferencing the result of va_next.
    for (unsigned GPRIndex = 0; GPRIndex != NumGPArgRegs; ++GPRIndex) {
      // Get an existing live-in vreg, or add a new one.
      unsigned VReg = MF.getRegInfo().getLiveInVirtReg(GPArgRegs[GPRIndex]);
      if (!VReg)
        VReg = MF.addLiveIn(GPArgRegs[GPRIndex], &PPC::GPRCRegClass);

      SDValue Val = DAG.getCopyFromReg(Chain, dl, VReg, PtrVT);
      SDValue Store =
          DAG.getStore(Val.getValue(1), dl, Val, FIN, MachinePointerInfo());
      MemOps.push_back(Store);
      // Increment the address by four for the next argument to store
      SDValue PtrOff = DAG.getConstant(PtrVT.getSizeInBits()/8, dl, PtrVT);
      FIN = DAG.getNode(ISD::ADD, dl, PtrOff.getValueType(), FIN, PtrOff);
    }

    // FIXME 32-bit SVR4: We only need to save FP argument registers if CR bit 6
    // is set.
    // The double arguments are stored to the VarArgsFrameIndex
    // on the stack.
    for (unsigned FPRIndex = 0; FPRIndex != NumFPArgRegs; ++FPRIndex) {
      // Get an existing live-in vreg, or add a new one.
      unsigned VReg = MF.getRegInfo().getLiveInVirtReg(FPArgRegs[FPRIndex]);
      if (!VReg)
        VReg = MF.addLiveIn(FPArgRegs[FPRIndex], &PPC::F8RCRegClass);

      SDValue Val = DAG.getCopyFromReg(Chain, dl, VReg, MVT::f64);
      SDValue Store =
          DAG.getStore(Val.getValue(1), dl, Val, FIN, MachinePointerInfo());
      MemOps.push_back(Store);
      // Increment the address by eight for the next argument to store
      SDValue PtrOff = DAG.getConstant(MVT(MVT::f64).getSizeInBits()/8, dl,
                                         PtrVT);
      FIN = DAG.getNode(ISD::ADD, dl, PtrOff.getValueType(), FIN, PtrOff);
    }
  }

  if (!MemOps.empty())
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, MemOps);

  return Chain;
}

// PPC64 passes i8, i16, and i32 values in i64 registers. Promote
// value to MVT::i64 and then truncate to the correct register size.
SDValue PPCTargetLowering::extendArgForPPC64(ISD::ArgFlagsTy Flags,
                                             EVT ObjectVT, SelectionDAG &DAG,
                                             SDValue ArgVal,
                                             const SDLoc &dl) const {
  if (Flags.isSExt())
    ArgVal = DAG.getNode(ISD::AssertSext, dl, MVT::i64, ArgVal,
                         DAG.getValueType(ObjectVT));
  else if (Flags.isZExt())
    ArgVal = DAG.getNode(ISD::AssertZext, dl, MVT::i64, ArgVal,
                         DAG.getValueType(ObjectVT));

  return DAG.getNode(ISD::TRUNCATE, dl, ObjectVT, ArgVal);
}

SDValue PPCTargetLowering::LowerFormalArguments_64SVR4(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  // TODO: add description of PPC stack frame format, or at least some docs.
  //
  bool isELFv2ABI = Subtarget.isELFv2ABI();
  bool isLittleEndian = Subtarget.isLittleEndian();
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  PPCFunctionInfo *FuncInfo = MF.getInfo<PPCFunctionInfo>();

  assert(!(CallConv == CallingConv::Fast && isVarArg) &&
         "fastcc not supported on varargs functions");

  EVT PtrVT = getPointerTy(MF.getDataLayout());
  // Potential tail calls could cause overwriting of argument stack slots.
  bool isImmutable = !(getTargetMachine().Options.GuaranteedTailCallOpt &&
                       (CallConv == CallingConv::Fast));
  unsigned PtrByteSize = 8;
  unsigned LinkageSize = Subtarget.getFrameLowering()->getLinkageSize();

  static const MCPhysReg GPR[] = {
    PPC::X3, PPC::X4, PPC::X5, PPC::X6,
    PPC::X7, PPC::X8, PPC::X9, PPC::X10,
  };
  static const MCPhysReg VR[] = {
    PPC::V2, PPC::V3, PPC::V4, PPC::V5, PPC::V6, PPC::V7, PPC::V8,
    PPC::V9, PPC::V10, PPC::V11, PPC::V12, PPC::V13
  };

  const unsigned Num_GPR_Regs = array_lengthof(GPR);
  const unsigned Num_FPR_Regs = useSoftFloat() ? 0 : 13;
  const unsigned Num_VR_Regs  = array_lengthof(VR);
  const unsigned Num_QFPR_Regs = Num_FPR_Regs;

  // Do a first pass over the arguments to determine whether the ABI
  // guarantees that our caller has allocated the parameter save area
  // on its stack frame.  In the ELFv1 ABI, this is always the case;
  // in the ELFv2 ABI, it is true if this is a vararg function or if
  // any parameter is located in a stack slot.

  bool HasParameterArea = !isELFv2ABI || isVarArg;
  unsigned ParamAreaSize = Num_GPR_Regs * PtrByteSize;
  unsigned NumBytes = LinkageSize;
  unsigned AvailableFPRs = Num_FPR_Regs;
  unsigned AvailableVRs = Num_VR_Regs;
  for (unsigned i = 0, e = Ins.size(); i != e; ++i) {
    if (Ins[i].Flags.isNest())
      continue;

    if (CalculateStackSlotUsed(Ins[i].VT, Ins[i].ArgVT, Ins[i].Flags,
                               PtrByteSize, LinkageSize, ParamAreaSize,
                               NumBytes, AvailableFPRs, AvailableVRs,
                               Subtarget.hasQPX()))
      HasParameterArea = true;
  }

  // Add DAG nodes to load the arguments or copy them out of registers.  On
  // entry to a function on PPC, the arguments start after the linkage area,
  // although the first ones are often in registers.

  unsigned ArgOffset = LinkageSize;
  unsigned GPR_idx = 0, FPR_idx = 0, VR_idx = 0;
  unsigned &QFPR_idx = FPR_idx;
  SmallVector<SDValue, 8> MemOps;
  Function::const_arg_iterator FuncArg = MF.getFunction().arg_begin();
  unsigned CurArgIdx = 0;
  for (unsigned ArgNo = 0, e = Ins.size(); ArgNo != e; ++ArgNo) {
    SDValue ArgVal;
    bool needsLoad = false;
    EVT ObjectVT = Ins[ArgNo].VT;
    EVT OrigVT = Ins[ArgNo].ArgVT;
    unsigned ObjSize = ObjectVT.getStoreSize();
    unsigned ArgSize = ObjSize;
    ISD::ArgFlagsTy Flags = Ins[ArgNo].Flags;
    if (Ins[ArgNo].isOrigArg()) {
      std::advance(FuncArg, Ins[ArgNo].getOrigArgIndex() - CurArgIdx);
      CurArgIdx = Ins[ArgNo].getOrigArgIndex();
    }
    // We re-align the argument offset for each argument, except when using the
    // fast calling convention, when we need to make sure we do that only when
    // we'll actually use a stack slot.
    unsigned CurArgOffset, Align;
    auto ComputeArgOffset = [&]() {
      /* Respect alignment of argument on the stack.  */
      Align = CalculateStackSlotAlignment(ObjectVT, OrigVT, Flags, PtrByteSize);
      ArgOffset = ((ArgOffset + Align - 1) / Align) * Align;
      CurArgOffset = ArgOffset;
    };

    if (CallConv != CallingConv::Fast) {
      ComputeArgOffset();

      /* Compute GPR index associated with argument offset.  */
      GPR_idx = (ArgOffset - LinkageSize) / PtrByteSize;
      GPR_idx = std::min(GPR_idx, Num_GPR_Regs);
    }

    // FIXME the codegen can be much improved in some cases.
    // We do not have to keep everything in memory.
    if (Flags.isByVal()) {
      assert(Ins[ArgNo].isOrigArg() && "Byval arguments cannot be implicit");

      if (CallConv == CallingConv::Fast)
        ComputeArgOffset();

      // ObjSize is the true size, ArgSize rounded up to multiple of registers.
      ObjSize = Flags.getByValSize();
      ArgSize = ((ObjSize + PtrByteSize - 1)/PtrByteSize) * PtrByteSize;
      // Empty aggregate parameters do not take up registers.  Examples:
      //   struct { } a;
      //   union  { } b;
      //   int c[0];
      // etc.  However, we have to provide a place-holder in InVals, so
      // pretend we have an 8-byte item at the current address for that
      // purpose.
      if (!ObjSize) {
        int FI = MFI.CreateFixedObject(PtrByteSize, ArgOffset, true);
        SDValue FIN = DAG.getFrameIndex(FI, PtrVT);
        InVals.push_back(FIN);
        continue;
      }

      // Create a stack object covering all stack doublewords occupied
      // by the argument.  If the argument is (fully or partially) on
      // the stack, or if the argument is fully in registers but the
      // caller has allocated the parameter save anyway, we can refer
      // directly to the caller's stack frame.  Otherwise, create a
      // local copy in our own frame.
      int FI;
      if (HasParameterArea ||
          ArgSize + ArgOffset > LinkageSize + Num_GPR_Regs * PtrByteSize)
        FI = MFI.CreateFixedObject(ArgSize, ArgOffset, false, true);
      else
        FI = MFI.CreateStackObject(ArgSize, Align, false);
      SDValue FIN = DAG.getFrameIndex(FI, PtrVT);

      // Handle aggregates smaller than 8 bytes.
      if (ObjSize < PtrByteSize) {
        // The value of the object is its address, which differs from the
        // address of the enclosing doubleword on big-endian systems.
        SDValue Arg = FIN;
        if (!isLittleEndian) {
          SDValue ArgOff = DAG.getConstant(PtrByteSize - ObjSize, dl, PtrVT);
          Arg = DAG.getNode(ISD::ADD, dl, ArgOff.getValueType(), Arg, ArgOff);
        }
        InVals.push_back(Arg);

        if (GPR_idx != Num_GPR_Regs) {
          unsigned VReg = MF.addLiveIn(GPR[GPR_idx++], &PPC::G8RCRegClass);
          FuncInfo->addLiveInAttr(VReg, Flags);
          SDValue Val = DAG.getCopyFromReg(Chain, dl, VReg, PtrVT);
          SDValue Store;

          if (ObjSize==1 || ObjSize==2 || ObjSize==4) {
            EVT ObjType = (ObjSize == 1 ? MVT::i8 :
                           (ObjSize == 2 ? MVT::i16 : MVT::i32));
            Store = DAG.getTruncStore(Val.getValue(1), dl, Val, Arg,
                                      MachinePointerInfo(&*FuncArg), ObjType);
          } else {
            // For sizes that don't fit a truncating store (3, 5, 6, 7),
            // store the whole register as-is to the parameter save area
            // slot.
            Store = DAG.getStore(Val.getValue(1), dl, Val, FIN,
                                 MachinePointerInfo(&*FuncArg));
          }

          MemOps.push_back(Store);
        }
        // Whether we copied from a register or not, advance the offset
        // into the parameter save area by a full doubleword.
        ArgOffset += PtrByteSize;
        continue;
      }

      // The value of the object is its address, which is the address of
      // its first stack doubleword.
      InVals.push_back(FIN);

      // Store whatever pieces of the object are in registers to memory.
      for (unsigned j = 0; j < ArgSize; j += PtrByteSize) {
        if (GPR_idx == Num_GPR_Regs)
          break;

        unsigned VReg = MF.addLiveIn(GPR[GPR_idx], &PPC::G8RCRegClass);
        FuncInfo->addLiveInAttr(VReg, Flags);
        SDValue Val = DAG.getCopyFromReg(Chain, dl, VReg, PtrVT);
        SDValue Addr = FIN;
        if (j) {
          SDValue Off = DAG.getConstant(j, dl, PtrVT);
          Addr = DAG.getNode(ISD::ADD, dl, Off.getValueType(), Addr, Off);
        }
        SDValue Store = DAG.getStore(Val.getValue(1), dl, Val, Addr,
                                     MachinePointerInfo(&*FuncArg, j));
        MemOps.push_back(Store);
        ++GPR_idx;
      }
      ArgOffset += ArgSize;
      continue;
    }

    switch (ObjectVT.getSimpleVT().SimpleTy) {
    default: llvm_unreachable("Unhandled argument type!");
    case MVT::i1:
    case MVT::i32:
    case MVT::i64:
      if (Flags.isNest()) {
        // The 'nest' parameter, if any, is passed in R11.
        unsigned VReg = MF.addLiveIn(PPC::X11, &PPC::G8RCRegClass);
        ArgVal = DAG.getCopyFromReg(Chain, dl, VReg, MVT::i64);

        if (ObjectVT == MVT::i32 || ObjectVT == MVT::i1)
          ArgVal = extendArgForPPC64(Flags, ObjectVT, DAG, ArgVal, dl);

        break;
      }

      // These can be scalar arguments or elements of an integer array type
      // passed directly.  Clang may use those instead of "byval" aggregate
      // types to avoid forcing arguments to memory unnecessarily.
      if (GPR_idx != Num_GPR_Regs) {
        unsigned VReg = MF.addLiveIn(GPR[GPR_idx++], &PPC::G8RCRegClass);
        FuncInfo->addLiveInAttr(VReg, Flags);
        ArgVal = DAG.getCopyFromReg(Chain, dl, VReg, MVT::i64);

        if (ObjectVT == MVT::i32 || ObjectVT == MVT::i1)
          // PPC64 passes i8, i16, and i32 values in i64 registers. Promote
          // value to MVT::i64 and then truncate to the correct register size.
          ArgVal = extendArgForPPC64(Flags, ObjectVT, DAG, ArgVal, dl);
      } else {
        if (CallConv == CallingConv::Fast)
          ComputeArgOffset();

        needsLoad = true;
        ArgSize = PtrByteSize;
      }
      if (CallConv != CallingConv::Fast || needsLoad)
        ArgOffset += 8;
      break;

    case MVT::f32:
    case MVT::f64:
      // These can be scalar arguments or elements of a float array type
      // passed directly.  The latter are used to implement ELFv2 homogenous
      // float aggregates.
      if (FPR_idx != Num_FPR_Regs) {
        unsigned VReg;

        if (ObjectVT == MVT::f32)
          VReg = MF.addLiveIn(FPR[FPR_idx],
                              Subtarget.hasP8Vector()
                                  ? &PPC::VSSRCRegClass
                                  : &PPC::F4RCRegClass);
        else
          VReg = MF.addLiveIn(FPR[FPR_idx], Subtarget.hasVSX()
                                                ? &PPC::VSFRCRegClass
                                                : &PPC::F8RCRegClass);

        ArgVal = DAG.getCopyFromReg(Chain, dl, VReg, ObjectVT);
        ++FPR_idx;
      } else if (GPR_idx != Num_GPR_Regs && CallConv != CallingConv::Fast) {
        // FIXME: We may want to re-enable this for CallingConv::Fast on the P8
        // once we support fp <-> gpr moves.

        // This can only ever happen in the presence of f32 array types,
        // since otherwise we never run out of FPRs before running out
        // of GPRs.
        unsigned VReg = MF.addLiveIn(GPR[GPR_idx++], &PPC::G8RCRegClass);
        FuncInfo->addLiveInAttr(VReg, Flags);
        ArgVal = DAG.getCopyFromReg(Chain, dl, VReg, MVT::i64);

        if (ObjectVT == MVT::f32) {
          if ((ArgOffset % PtrByteSize) == (isLittleEndian ? 4 : 0))
            ArgVal = DAG.getNode(ISD::SRL, dl, MVT::i64, ArgVal,
                                 DAG.getConstant(32, dl, MVT::i32));
          ArgVal = DAG.getNode(ISD::TRUNCATE, dl, MVT::i32, ArgVal);
        }

        ArgVal = DAG.getNode(ISD::BITCAST, dl, ObjectVT, ArgVal);
      } else {
        if (CallConv == CallingConv::Fast)
          ComputeArgOffset();

        needsLoad = true;
      }

      // When passing an array of floats, the array occupies consecutive
      // space in the argument area; only round up to the next doubleword
      // at the end of the array.  Otherwise, each float takes 8 bytes.
      if (CallConv != CallingConv::Fast || needsLoad) {
        ArgSize = Flags.isInConsecutiveRegs() ? ObjSize : PtrByteSize;
        ArgOffset += ArgSize;
        if (Flags.isInConsecutiveRegsLast())
          ArgOffset = ((ArgOffset + PtrByteSize - 1)/PtrByteSize) * PtrByteSize;
      }
      break;
    case MVT::v4f32:
    case MVT::v4i32:
    case MVT::v8i16:
    case MVT::v16i8:
    case MVT::v2f64:
    case MVT::v2i64:
    case MVT::v1i128:
    case MVT::f128:
      if (!Subtarget.hasQPX()) {
        // These can be scalar arguments or elements of a vector array type
        // passed directly.  The latter are used to implement ELFv2 homogenous
        // vector aggregates.
        if (VR_idx != Num_VR_Regs) {
          unsigned VReg = MF.addLiveIn(VR[VR_idx], &PPC::VRRCRegClass);
          ArgVal = DAG.getCopyFromReg(Chain, dl, VReg, ObjectVT);
          ++VR_idx;
        } else {
          if (CallConv == CallingConv::Fast)
            ComputeArgOffset();
          needsLoad = true;
        }
        if (CallConv != CallingConv::Fast || needsLoad)
          ArgOffset += 16;
        break;
      } // not QPX

      assert(ObjectVT.getSimpleVT().SimpleTy == MVT::v4f32 &&
             "Invalid QPX parameter type");
      LLVM_FALLTHROUGH;

    case MVT::v4f64:
    case MVT::v4i1:
      // QPX vectors are treated like their scalar floating-point subregisters
      // (except that they're larger).
      unsigned Sz = ObjectVT.getSimpleVT().SimpleTy == MVT::v4f32 ? 16 : 32;
      if (QFPR_idx != Num_QFPR_Regs) {
        const TargetRegisterClass *RC;
        switch (ObjectVT.getSimpleVT().SimpleTy) {
        case MVT::v4f64: RC = &PPC::QFRCRegClass; break;
        case MVT::v4f32: RC = &PPC::QSRCRegClass; break;
        default:         RC = &PPC::QBRCRegClass; break;
        }

        unsigned VReg = MF.addLiveIn(QFPR[QFPR_idx], RC);
        ArgVal = DAG.getCopyFromReg(Chain, dl, VReg, ObjectVT);
        ++QFPR_idx;
      } else {
        if (CallConv == CallingConv::Fast)
          ComputeArgOffset();
        needsLoad = true;
      }
      if (CallConv != CallingConv::Fast || needsLoad)
        ArgOffset += Sz;
      break;
    }

    // We need to load the argument to a virtual register if we determined
    // above that we ran out of physical registers of the appropriate type.
    if (needsLoad) {
      if (ObjSize < ArgSize && !isLittleEndian)
        CurArgOffset += ArgSize - ObjSize;
      int FI = MFI.CreateFixedObject(ObjSize, CurArgOffset, isImmutable);
      SDValue FIN = DAG.getFrameIndex(FI, PtrVT);
      ArgVal = DAG.getLoad(ObjectVT, dl, Chain, FIN, MachinePointerInfo());
    }

    InVals.push_back(ArgVal);
  }

  // Area that is at least reserved in the caller of this function.
  unsigned MinReservedArea;
  if (HasParameterArea)
    MinReservedArea = std::max(ArgOffset, LinkageSize + 8 * PtrByteSize);
  else
    MinReservedArea = LinkageSize;

  // Set the size that is at least reserved in caller of this function.  Tail
  // call optimized functions' reserved stack space needs to be aligned so that
  // taking the difference between two stack areas will result in an aligned
  // stack.
  MinReservedArea =
      EnsureStackAlignment(Subtarget.getFrameLowering(), MinReservedArea);
  FuncInfo->setMinReservedArea(MinReservedArea);

  // If the function takes variable number of arguments, make a frame index for
  // the start of the first vararg value... for expansion of llvm.va_start.
  if (isVarArg) {
    int Depth = ArgOffset;

    FuncInfo->setVarArgsFrameIndex(
      MFI.CreateFixedObject(PtrByteSize, Depth, true));
    SDValue FIN = DAG.getFrameIndex(FuncInfo->getVarArgsFrameIndex(), PtrVT);

    // If this function is vararg, store any remaining integer argument regs
    // to their spots on the stack so that they may be loaded by dereferencing
    // the result of va_next.
    for (GPR_idx = (ArgOffset - LinkageSize) / PtrByteSize;
         GPR_idx < Num_GPR_Regs; ++GPR_idx) {
      unsigned VReg = MF.addLiveIn(GPR[GPR_idx], &PPC::G8RCRegClass);
      SDValue Val = DAG.getCopyFromReg(Chain, dl, VReg, PtrVT);
      SDValue Store =
          DAG.getStore(Val.getValue(1), dl, Val, FIN, MachinePointerInfo());
      MemOps.push_back(Store);
      // Increment the address by four for the next argument to store
      SDValue PtrOff = DAG.getConstant(PtrByteSize, dl, PtrVT);
      FIN = DAG.getNode(ISD::ADD, dl, PtrOff.getValueType(), FIN, PtrOff);
    }
  }

  if (!MemOps.empty())
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, MemOps);

  return Chain;
}

SDValue PPCTargetLowering::LowerFormalArguments_Darwin(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  // TODO: add description of PPC stack frame format, or at least some docs.
  //
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  PPCFunctionInfo *FuncInfo = MF.getInfo<PPCFunctionInfo>();

  EVT PtrVT = getPointerTy(MF.getDataLayout());
  bool isPPC64 = PtrVT == MVT::i64;
  // Potential tail calls could cause overwriting of argument stack slots.
  bool isImmutable = !(getTargetMachine().Options.GuaranteedTailCallOpt &&
                       (CallConv == CallingConv::Fast));
  unsigned PtrByteSize = isPPC64 ? 8 : 4;
  unsigned LinkageSize = Subtarget.getFrameLowering()->getLinkageSize();
  unsigned ArgOffset = LinkageSize;
  // Area that is at least reserved in caller of this function.
  unsigned MinReservedArea = ArgOffset;

  static const MCPhysReg GPR_32[] = {           // 32-bit registers.
    PPC::R3, PPC::R4, PPC::R5, PPC::R6,
    PPC::R7, PPC::R8, PPC::R9, PPC::R10,
  };
  static const MCPhysReg GPR_64[] = {           // 64-bit registers.
    PPC::X3, PPC::X4, PPC::X5, PPC::X6,
    PPC::X7, PPC::X8, PPC::X9, PPC::X10,
  };
  static const MCPhysReg VR[] = {
    PPC::V2, PPC::V3, PPC::V4, PPC::V5, PPC::V6, PPC::V7, PPC::V8,
    PPC::V9, PPC::V10, PPC::V11, PPC::V12, PPC::V13
  };

  const unsigned Num_GPR_Regs = array_lengthof(GPR_32);
  const unsigned Num_FPR_Regs = useSoftFloat() ? 0 : 13;
  const unsigned Num_VR_Regs  = array_lengthof( VR);

  unsigned GPR_idx = 0, FPR_idx = 0, VR_idx = 0;

  const MCPhysReg *GPR = isPPC64 ? GPR_64 : GPR_32;

  // In 32-bit non-varargs functions, the stack space for vectors is after the
  // stack space for non-vectors.  We do not use this space unless we have
  // too many vectors to fit in registers, something that only occurs in
  // constructed examples:), but we have to walk the arglist to figure
  // that out...for the pathological case, compute VecArgOffset as the
  // start of the vector parameter area.  Computing VecArgOffset is the
  // entire point of the following loop.
  unsigned VecArgOffset = ArgOffset;
  if (!isVarArg && !isPPC64) {
    for (unsigned ArgNo = 0, e = Ins.size(); ArgNo != e;
         ++ArgNo) {
      EVT ObjectVT = Ins[ArgNo].VT;
      ISD::ArgFlagsTy Flags = Ins[ArgNo].Flags;

      if (Flags.isByVal()) {
        // ObjSize is the true size, ArgSize rounded up to multiple of regs.
        unsigned ObjSize = Flags.getByValSize();
        unsigned ArgSize =
                ((ObjSize + PtrByteSize - 1)/PtrByteSize) * PtrByteSize;
        VecArgOffset += ArgSize;
        continue;
      }

      switch(ObjectVT.getSimpleVT().SimpleTy) {
      default: llvm_unreachable("Unhandled argument type!");
      case MVT::i1:
      case MVT::i32:
      case MVT::f32:
        VecArgOffset += 4;
        break;
      case MVT::i64:  // PPC64
      case MVT::f64:
        // FIXME: We are guaranteed to be !isPPC64 at this point.
        // Does MVT::i64 apply?
        VecArgOffset += 8;
        break;
      case MVT::v4f32:
      case MVT::v4i32:
      case MVT::v8i16:
      case MVT::v16i8:
        // Nothing to do, we're only looking at Nonvector args here.
        break;
      }
    }
  }
  // We've found where the vector parameter area in memory is.  Skip the
  // first 12 parameters; these don't use that memory.
  VecArgOffset = ((VecArgOffset+15)/16)*16;
  VecArgOffset += 12*16;

  // Add DAG nodes to load the arguments or copy them out of registers.  On
  // entry to a function on PPC, the arguments start after the linkage area,
  // although the first ones are often in registers.

  SmallVector<SDValue, 8> MemOps;
  unsigned nAltivecParamsAtEnd = 0;
  Function::const_arg_iterator FuncArg = MF.getFunction().arg_begin();
  unsigned CurArgIdx = 0;
  for (unsigned ArgNo = 0, e = Ins.size(); ArgNo != e; ++ArgNo) {
    SDValue ArgVal;
    bool needsLoad = false;
    EVT ObjectVT = Ins[ArgNo].VT;
    unsigned ObjSize = ObjectVT.getSizeInBits()/8;
    unsigned ArgSize = ObjSize;
    ISD::ArgFlagsTy Flags = Ins[ArgNo].Flags;
    if (Ins[ArgNo].isOrigArg()) {
      std::advance(FuncArg, Ins[ArgNo].getOrigArgIndex() - CurArgIdx);
      CurArgIdx = Ins[ArgNo].getOrigArgIndex();
    }
    unsigned CurArgOffset = ArgOffset;

    // Varargs or 64 bit Altivec parameters are padded to a 16 byte boundary.
    if (ObjectVT==MVT::v4f32 || ObjectVT==MVT::v4i32 ||
        ObjectVT==MVT::v8i16 || ObjectVT==MVT::v16i8) {
      if (isVarArg || isPPC64) {
        MinReservedArea = ((MinReservedArea+15)/16)*16;
        MinReservedArea += CalculateStackSlotSize(ObjectVT,
                                                  Flags,
                                                  PtrByteSize);
      } else  nAltivecParamsAtEnd++;
    } else
      // Calculate min reserved area.
      MinReservedArea += CalculateStackSlotSize(Ins[ArgNo].VT,
                                                Flags,
                                                PtrByteSize);

    // FIXME the codegen can be much improved in some cases.
    // We do not have to keep everything in memory.
    if (Flags.isByVal()) {
      assert(Ins[ArgNo].isOrigArg() && "Byval arguments cannot be implicit");

      // ObjSize is the true size, ArgSize rounded up to multiple of registers.
      ObjSize = Flags.getByValSize();
      ArgSize = ((ObjSize + PtrByteSize - 1)/PtrByteSize) * PtrByteSize;
      // Objects of size 1 and 2 are right justified, everything else is
      // left justified.  This means the memory address is adjusted forwards.
      if (ObjSize==1 || ObjSize==2) {
        CurArgOffset = CurArgOffset + (4 - ObjSize);
      }
      // The value of the object is its address.
      int FI = MFI.CreateFixedObject(ObjSize, CurArgOffset, false, true);
      SDValue FIN = DAG.getFrameIndex(FI, PtrVT);
      InVals.push_back(FIN);
      if (ObjSize==1 || ObjSize==2) {
        if (GPR_idx != Num_GPR_Regs) {
          unsigned VReg;
          if (isPPC64)
            VReg = MF.addLiveIn(GPR[GPR_idx], &PPC::G8RCRegClass);
          else
            VReg = MF.addLiveIn(GPR[GPR_idx], &PPC::GPRCRegClass);
          SDValue Val = DAG.getCopyFromReg(Chain, dl, VReg, PtrVT);
          EVT ObjType = ObjSize == 1 ? MVT::i8 : MVT::i16;
          SDValue Store =
              DAG.getTruncStore(Val.getValue(1), dl, Val, FIN,
                                MachinePointerInfo(&*FuncArg), ObjType);
          MemOps.push_back(Store);
          ++GPR_idx;
        }

        ArgOffset += PtrByteSize;

        continue;
      }
      for (unsigned j = 0; j < ArgSize; j += PtrByteSize) {
        // Store whatever pieces of the object are in registers
        // to memory.  ArgOffset will be the address of the beginning
        // of the object.
        if (GPR_idx != Num_GPR_Regs) {
          unsigned VReg;
          if (isPPC64)
            VReg = MF.addLiveIn(GPR[GPR_idx], &PPC::G8RCRegClass);
          else
            VReg = MF.addLiveIn(GPR[GPR_idx], &PPC::GPRCRegClass);
          int FI = MFI.CreateFixedObject(PtrByteSize, ArgOffset, true);
          SDValue FIN = DAG.getFrameIndex(FI, PtrVT);
          SDValue Val = DAG.getCopyFromReg(Chain, dl, VReg, PtrVT);
          SDValue Store = DAG.getStore(Val.getValue(1), dl, Val, FIN,
                                       MachinePointerInfo(&*FuncArg, j));
          MemOps.push_back(Store);
          ++GPR_idx;
          ArgOffset += PtrByteSize;
        } else {
          ArgOffset += ArgSize - (ArgOffset-CurArgOffset);
          break;
        }
      }
      continue;
    }

    switch (ObjectVT.getSimpleVT().SimpleTy) {
    default: llvm_unreachable("Unhandled argument type!");
    case MVT::i1:
    case MVT::i32:
      if (!isPPC64) {
        if (GPR_idx != Num_GPR_Regs) {
          unsigned VReg = MF.addLiveIn(GPR[GPR_idx], &PPC::GPRCRegClass);
          ArgVal = DAG.getCopyFromReg(Chain, dl, VReg, MVT::i32);

          if (ObjectVT == MVT::i1)
            ArgVal = DAG.getNode(ISD::TRUNCATE, dl, MVT::i1, ArgVal);

          ++GPR_idx;
        } else {
          needsLoad = true;
          ArgSize = PtrByteSize;
        }
        // All int arguments reserve stack space in the Darwin ABI.
        ArgOffset += PtrByteSize;
        break;
      }
      LLVM_FALLTHROUGH;
    case MVT::i64:  // PPC64
      if (GPR_idx != Num_GPR_Regs) {
        unsigned VReg = MF.addLiveIn(GPR[GPR_idx], &PPC::G8RCRegClass);
        ArgVal = DAG.getCopyFromReg(Chain, dl, VReg, MVT::i64);

        if (ObjectVT == MVT::i32 || ObjectVT == MVT::i1)
          // PPC64 passes i8, i16, and i32 values in i64 registers. Promote
          // value to MVT::i64 and then truncate to the correct register size.
          ArgVal = extendArgForPPC64(Flags, ObjectVT, DAG, ArgVal, dl);

        ++GPR_idx;
      } else {
        needsLoad = true;
        ArgSize = PtrByteSize;
      }
      // All int arguments reserve stack space in the Darwin ABI.
      ArgOffset += 8;
      break;

    case MVT::f32:
    case MVT::f64:
      // Every 4 bytes of argument space consumes one of the GPRs available for
      // argument passing.
      if (GPR_idx != Num_GPR_Regs) {
        ++GPR_idx;
        if (ObjSize == 8 && GPR_idx != Num_GPR_Regs && !isPPC64)
          ++GPR_idx;
      }
      if (FPR_idx != Num_FPR_Regs) {
        unsigned VReg;

        if (ObjectVT == MVT::f32)
          VReg = MF.addLiveIn(FPR[FPR_idx], &PPC::F4RCRegClass);
        else
          VReg = MF.addLiveIn(FPR[FPR_idx], &PPC::F8RCRegClass);

        ArgVal = DAG.getCopyFromReg(Chain, dl, VReg, ObjectVT);
        ++FPR_idx;
      } else {
        needsLoad = true;
      }

      // All FP arguments reserve stack space in the Darwin ABI.
      ArgOffset += isPPC64 ? 8 : ObjSize;
      break;
    case MVT::v4f32:
    case MVT::v4i32:
    case MVT::v8i16:
    case MVT::v16i8:
      // Note that vector arguments in registers don't reserve stack space,
      // except in varargs functions.
      if (VR_idx != Num_VR_Regs) {
        unsigned VReg = MF.addLiveIn(VR[VR_idx], &PPC::VRRCRegClass);
        ArgVal = DAG.getCopyFromReg(Chain, dl, VReg, ObjectVT);
        if (isVarArg) {
          while ((ArgOffset % 16) != 0) {
            ArgOffset += PtrByteSize;
            if (GPR_idx != Num_GPR_Regs)
              GPR_idx++;
          }
          ArgOffset += 16;
          GPR_idx = std::min(GPR_idx+4, Num_GPR_Regs); // FIXME correct for ppc64?
        }
        ++VR_idx;
      } else {
        if (!isVarArg && !isPPC64) {
          // Vectors go after all the nonvectors.
          CurArgOffset = VecArgOffset;
          VecArgOffset += 16;
        } else {
          // Vectors are aligned.
          ArgOffset = ((ArgOffset+15)/16)*16;
          CurArgOffset = ArgOffset;
          ArgOffset += 16;
        }
        needsLoad = true;
      }
      break;
    }

    // We need to load the argument to a virtual register if we determined above
    // that we ran out of physical registers of the appropriate type.
    if (needsLoad) {
      int FI = MFI.CreateFixedObject(ObjSize,
                                     CurArgOffset + (ArgSize - ObjSize),
                                     isImmutable);
      SDValue FIN = DAG.getFrameIndex(FI, PtrVT);
      ArgVal = DAG.getLoad(ObjectVT, dl, Chain, FIN, MachinePointerInfo());
    }

    InVals.push_back(ArgVal);
  }

  // Allow for Altivec parameters at the end, if needed.
  if (nAltivecParamsAtEnd) {
    MinReservedArea = ((MinReservedArea+15)/16)*16;
    MinReservedArea += 16*nAltivecParamsAtEnd;
  }

  // Area that is at least reserved in the caller of this function.
  MinReservedArea = std::max(MinReservedArea, LinkageSize + 8 * PtrByteSize);

  // Set the size that is at least reserved in caller of this function.  Tail
  // call optimized functions' reserved stack space needs to be aligned so that
  // taking the difference between two stack areas will result in an aligned
  // stack.
  MinReservedArea =
      EnsureStackAlignment(Subtarget.getFrameLowering(), MinReservedArea);
  FuncInfo->setMinReservedArea(MinReservedArea);

  // If the function takes variable number of arguments, make a frame index for
  // the start of the first vararg value... for expansion of llvm.va_start.
  if (isVarArg) {
    int Depth = ArgOffset;

    FuncInfo->setVarArgsFrameIndex(
      MFI.CreateFixedObject(PtrVT.getSizeInBits()/8,
                            Depth, true));
    SDValue FIN = DAG.getFrameIndex(FuncInfo->getVarArgsFrameIndex(), PtrVT);

    // If this function is vararg, store any remaining integer argument regs
    // to their spots on the stack so that they may be loaded by dereferencing
    // the result of va_next.
    for (; GPR_idx != Num_GPR_Regs; ++GPR_idx) {
      unsigned VReg;

      if (isPPC64)
        VReg = MF.addLiveIn(GPR[GPR_idx], &PPC::G8RCRegClass);
      else
        VReg = MF.addLiveIn(GPR[GPR_idx], &PPC::GPRCRegClass);

      SDValue Val = DAG.getCopyFromReg(Chain, dl, VReg, PtrVT);
      SDValue Store =
          DAG.getStore(Val.getValue(1), dl, Val, FIN, MachinePointerInfo());
      MemOps.push_back(Store);
      // Increment the address by four for the next argument to store
      SDValue PtrOff = DAG.getConstant(PtrVT.getSizeInBits()/8, dl, PtrVT);
      FIN = DAG.getNode(ISD::ADD, dl, PtrOff.getValueType(), FIN, PtrOff);
    }
  }

  if (!MemOps.empty())
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, MemOps);

  return Chain;
}

/// CalculateTailCallSPDiff - Get the amount the stack pointer has to be
/// adjusted to accommodate the arguments for the tailcall.
static int CalculateTailCallSPDiff(SelectionDAG& DAG, bool isTailCall,
                                   unsigned ParamSize) {

  if (!isTailCall) return 0;

  PPCFunctionInfo *FI = DAG.getMachineFunction().getInfo<PPCFunctionInfo>();
  unsigned CallerMinReservedArea = FI->getMinReservedArea();
  int SPDiff = (int)CallerMinReservedArea - (int)ParamSize;
  // Remember only if the new adjustment is bigger.
  if (SPDiff < FI->getTailCallSPDelta())
    FI->setTailCallSPDelta(SPDiff);

  return SPDiff;
}

static bool isFunctionGlobalAddress(SDValue Callee);

static bool
callsShareTOCBase(const Function *Caller, SDValue Callee,
                    const TargetMachine &TM) {
  // If !G, Callee can be an external symbol.
  GlobalAddressSDNode *G = dyn_cast<GlobalAddressSDNode>(Callee);
  if (!G)
    return false;

  // The medium and large code models are expected to provide a sufficiently
  // large TOC to provide all data addressing needs of a module with a
  // single TOC. Since each module will be addressed with a single TOC then we
  // only need to check that caller and callee don't cross dso boundaries.
  if (CodeModel::Medium == TM.getCodeModel() ||
      CodeModel::Large == TM.getCodeModel())
    return TM.shouldAssumeDSOLocal(*Caller->getParent(), G->getGlobal());

  // Otherwise we need to ensure callee and caller are in the same section,
  // since the linker may allocate multiple TOCs, and we don't know which
  // sections will belong to the same TOC base.

  const GlobalValue *GV = G->getGlobal();
  if (!GV->isStrongDefinitionForLinker())
    return false;

  // Any explicitly-specified sections and section prefixes must also match.
  // Also, if we're using -ffunction-sections, then each function is always in
  // a different section (the same is true for COMDAT functions).
  if (TM.getFunctionSections() || GV->hasComdat() || Caller->hasComdat() ||
      GV->getSection() != Caller->getSection())
    return false;
  if (const auto *F = dyn_cast<Function>(GV)) {
    if (F->getSectionPrefix() != Caller->getSectionPrefix())
      return false;
  }

  // If the callee might be interposed, then we can't assume the ultimate call
  // target will be in the same section. Even in cases where we can assume that
  // interposition won't happen, in any case where the linker might insert a
  // stub to allow for interposition, we must generate code as though
  // interposition might occur. To understand why this matters, consider a
  // situation where: a -> b -> c where the arrows indicate calls. b and c are
  // in the same section, but a is in a different module (i.e. has a different
  // TOC base pointer). If the linker allows for interposition between b and c,
  // then it will generate a stub for the call edge between b and c which will
  // save the TOC pointer into the designated stack slot allocated by b. If we
  // return true here, and therefore allow a tail call between b and c, that
  // stack slot won't exist and the b -> c stub will end up saving b'c TOC base
  // pointer into the stack slot allocated by a (where the a -> b stub saved
  // a's TOC base pointer). If we're not considering a tail call, but rather,
  // whether a nop is needed after the call instruction in b, because the linker
  // will insert a stub, it might complain about a missing nop if we omit it
  // (although many don't complain in this case).
  if (!TM.shouldAssumeDSOLocal(*Caller->getParent(), GV))
    return false;

  return true;
}

static bool
needStackSlotPassParameters(const PPCSubtarget &Subtarget,
                            const SmallVectorImpl<ISD::OutputArg> &Outs) {
  assert(Subtarget.isSVR4ABI() && Subtarget.isPPC64());

  const unsigned PtrByteSize = 8;
  const unsigned LinkageSize = Subtarget.getFrameLowering()->getLinkageSize();

  static const MCPhysReg GPR[] = {
    PPC::X3, PPC::X4, PPC::X5, PPC::X6,
    PPC::X7, PPC::X8, PPC::X9, PPC::X10,
  };
  static const MCPhysReg VR[] = {
    PPC::V2, PPC::V3, PPC::V4, PPC::V5, PPC::V6, PPC::V7, PPC::V8,
    PPC::V9, PPC::V10, PPC::V11, PPC::V12, PPC::V13
  };

  const unsigned NumGPRs = array_lengthof(GPR);
  const unsigned NumFPRs = 13;
  const unsigned NumVRs = array_lengthof(VR);
  const unsigned ParamAreaSize = NumGPRs * PtrByteSize;

  unsigned NumBytes = LinkageSize;
  unsigned AvailableFPRs = NumFPRs;
  unsigned AvailableVRs = NumVRs;

  for (const ISD::OutputArg& Param : Outs) {
    if (Param.Flags.isNest()) continue;

    if (CalculateStackSlotUsed(Param.VT, Param.ArgVT, Param.Flags,
                               PtrByteSize, LinkageSize, ParamAreaSize,
                               NumBytes, AvailableFPRs, AvailableVRs,
                               Subtarget.hasQPX()))
      return true;
  }
  return false;
}

static bool
hasSameArgumentList(const Function *CallerFn, ImmutableCallSite CS) {
  if (CS.arg_size() != CallerFn->arg_size())
    return false;

  ImmutableCallSite::arg_iterator CalleeArgIter = CS.arg_begin();
  ImmutableCallSite::arg_iterator CalleeArgEnd = CS.arg_end();
  Function::const_arg_iterator CallerArgIter = CallerFn->arg_begin();

  for (; CalleeArgIter != CalleeArgEnd; ++CalleeArgIter, ++CallerArgIter) {
    const Value* CalleeArg = *CalleeArgIter;
    const Value* CallerArg = &(*CallerArgIter);
    if (CalleeArg == CallerArg)
      continue;

    // e.g. @caller([4 x i64] %a, [4 x i64] %b) {
    //        tail call @callee([4 x i64] undef, [4 x i64] %b)
    //      }
    // 1st argument of callee is undef and has the same type as caller.
    if (CalleeArg->getType() == CallerArg->getType() &&
        isa<UndefValue>(CalleeArg))
      continue;

    return false;
  }

  return true;
}

// Returns true if TCO is possible between the callers and callees
// calling conventions.
static bool
areCallingConvEligibleForTCO_64SVR4(CallingConv::ID CallerCC,
                                    CallingConv::ID CalleeCC) {
  // Tail calls are possible with fastcc and ccc.
  auto isTailCallableCC  = [] (CallingConv::ID CC){
      return  CC == CallingConv::C || CC == CallingConv::Fast;
  };
  if (!isTailCallableCC(CallerCC) || !isTailCallableCC(CalleeCC))
    return false;

  // We can safely tail call both fastcc and ccc callees from a c calling
  // convention caller. If the caller is fastcc, we may have less stack space
  // than a non-fastcc caller with the same signature so disable tail-calls in
  // that case.
  return CallerCC == CallingConv::C || CallerCC == CalleeCC;
}

bool
PPCTargetLowering::IsEligibleForTailCallOptimization_64SVR4(
                                    SDValue Callee,
                                    CallingConv::ID CalleeCC,
                                    ImmutableCallSite CS,
                                    bool isVarArg,
                                    const SmallVectorImpl<ISD::OutputArg> &Outs,
                                    const SmallVectorImpl<ISD::InputArg> &Ins,
                                    SelectionDAG& DAG) const {
  bool TailCallOpt = getTargetMachine().Options.GuaranteedTailCallOpt;

  if (DisableSCO && !TailCallOpt) return false;

  // Variadic argument functions are not supported.
  if (isVarArg) return false;

  auto &Caller = DAG.getMachineFunction().getFunction();
  // Check that the calling conventions are compatible for tco.
  if (!areCallingConvEligibleForTCO_64SVR4(Caller.getCallingConv(), CalleeCC))
    return false;

  // Caller contains any byval parameter is not supported.
  if (any_of(Ins, [](const ISD::InputArg &IA) { return IA.Flags.isByVal(); }))
    return false;

  // Callee contains any byval parameter is not supported, too.
  // Note: This is a quick work around, because in some cases, e.g.
  // caller's stack size > callee's stack size, we are still able to apply
  // sibling call optimization. For example, gcc is able to do SCO for caller1
  // in the following example, but not for caller2.
  //   struct test {
  //     long int a;
  //     char ary[56];
  //   } gTest;
  //   __attribute__((noinline)) int callee(struct test v, struct test *b) {
  //     b->a = v.a;
  //     return 0;
  //   }
  //   void caller1(struct test a, struct test c, struct test *b) {
  //     callee(gTest, b); }
  //   void caller2(struct test *b) { callee(gTest, b); }
  if (any_of(Outs, [](const ISD::OutputArg& OA) { return OA.Flags.isByVal(); }))
    return false;

  // If callee and caller use different calling conventions, we cannot pass
  // parameters on stack since offsets for the parameter area may be different.
  if (Caller.getCallingConv() != CalleeCC &&
      needStackSlotPassParameters(Subtarget, Outs))
    return false;

  // No TCO/SCO on indirect call because Caller have to restore its TOC
  if (!isFunctionGlobalAddress(Callee) &&
      !isa<ExternalSymbolSDNode>(Callee))
    return false;

  // If the caller and callee potentially have different TOC bases then we
  // cannot tail call since we need to restore the TOC pointer after the call.
  // ref: https://bugzilla.mozilla.org/show_bug.cgi?id=973977
  if (!callsShareTOCBase(&Caller, Callee, getTargetMachine()))
    return false;

  // TCO allows altering callee ABI, so we don't have to check further.
  if (CalleeCC == CallingConv::Fast && TailCallOpt)
    return true;

  if (DisableSCO) return false;

  // If callee use the same argument list that caller is using, then we can
  // apply SCO on this case. If it is not, then we need to check if callee needs
  // stack for passing arguments.
  if (!hasSameArgumentList(&Caller, CS) &&
      needStackSlotPassParameters(Subtarget, Outs)) {
    return false;
  }

  return true;
}

/// IsEligibleForTailCallOptimization - Check whether the call is eligible
/// for tail call optimization. Targets which want to do tail call
/// optimization should implement this function.
bool
PPCTargetLowering::IsEligibleForTailCallOptimization(SDValue Callee,
                                                     CallingConv::ID CalleeCC,
                                                     bool isVarArg,
                                      const SmallVectorImpl<ISD::InputArg> &Ins,
                                                     SelectionDAG& DAG) const {
  if (!getTargetMachine().Options.GuaranteedTailCallOpt)
    return false;

  // Variable argument functions are not supported.
  if (isVarArg)
    return false;

  MachineFunction &MF = DAG.getMachineFunction();
  CallingConv::ID CallerCC = MF.getFunction().getCallingConv();
  if (CalleeCC == CallingConv::Fast && CallerCC == CalleeCC) {
    // Functions containing by val parameters are not supported.
    for (unsigned i = 0; i != Ins.size(); i++) {
       ISD::ArgFlagsTy Flags = Ins[i].Flags;
       if (Flags.isByVal()) return false;
    }

    // Non-PIC/GOT tail calls are supported.
    if (getTargetMachine().getRelocationModel() != Reloc::PIC_)
      return true;

    // At the moment we can only do local tail calls (in same module, hidden
    // or protected) if we are generating PIC.
    if (GlobalAddressSDNode *G = dyn_cast<GlobalAddressSDNode>(Callee))
      return G->getGlobal()->hasHiddenVisibility()
          || G->getGlobal()->hasProtectedVisibility();
  }

  return false;
}

/// isCallCompatibleAddress - Return the immediate to use if the specified
/// 32-bit value is representable in the immediate field of a BxA instruction.
static SDNode *isBLACompatibleAddress(SDValue Op, SelectionDAG &DAG) {
  ConstantSDNode *C = dyn_cast<ConstantSDNode>(Op);
  if (!C) return nullptr;

  int Addr = C->getZExtValue();
  if ((Addr & 3) != 0 ||  // Low 2 bits are implicitly zero.
      SignExtend32<26>(Addr) != Addr)
    return nullptr;  // Top 6 bits have to be sext of immediate.

  return DAG
      .getConstant(
          (int)C->getZExtValue() >> 2, SDLoc(Op),
          DAG.getTargetLoweringInfo().getPointerTy(DAG.getDataLayout()))
      .getNode();
}

namespace {

struct TailCallArgumentInfo {
  SDValue Arg;
  SDValue FrameIdxOp;
  int FrameIdx = 0;

  TailCallArgumentInfo() = default;
};

} // end anonymous namespace

/// StoreTailCallArgumentsToStackSlot - Stores arguments to their stack slot.
static void StoreTailCallArgumentsToStackSlot(
    SelectionDAG &DAG, SDValue Chain,
    const SmallVectorImpl<TailCallArgumentInfo> &TailCallArgs,
    SmallVectorImpl<SDValue> &MemOpChains, const SDLoc &dl) {
  for (unsigned i = 0, e = TailCallArgs.size(); i != e; ++i) {
    SDValue Arg = TailCallArgs[i].Arg;
    SDValue FIN = TailCallArgs[i].FrameIdxOp;
    int FI = TailCallArgs[i].FrameIdx;
    // Store relative to framepointer.
    MemOpChains.push_back(DAG.getStore(
        Chain, dl, Arg, FIN,
        MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FI)));
  }
}

/// EmitTailCallStoreFPAndRetAddr - Move the frame pointer and return address to
/// the appropriate stack slot for the tail call optimized function call.
static SDValue EmitTailCallStoreFPAndRetAddr(SelectionDAG &DAG, SDValue Chain,
                                             SDValue OldRetAddr, SDValue OldFP,
                                             int SPDiff, const SDLoc &dl) {
  if (SPDiff) {
    // Calculate the new stack slot for the return address.
    MachineFunction &MF = DAG.getMachineFunction();
    const PPCSubtarget &Subtarget = MF.getSubtarget<PPCSubtarget>();
    const PPCFrameLowering *FL = Subtarget.getFrameLowering();
    bool isPPC64 = Subtarget.isPPC64();
    int SlotSize = isPPC64 ? 8 : 4;
    int NewRetAddrLoc = SPDiff + FL->getReturnSaveOffset();
    int NewRetAddr = MF.getFrameInfo().CreateFixedObject(SlotSize,
                                                         NewRetAddrLoc, true);
    EVT VT = isPPC64 ? MVT::i64 : MVT::i32;
    SDValue NewRetAddrFrIdx = DAG.getFrameIndex(NewRetAddr, VT);
    Chain = DAG.getStore(Chain, dl, OldRetAddr, NewRetAddrFrIdx,
                         MachinePointerInfo::getFixedStack(MF, NewRetAddr));

    // When using the 32/64-bit SVR4 ABI there is no need to move the FP stack
    // slot as the FP is never overwritten.
    if (Subtarget.isDarwinABI()) {
      int NewFPLoc = SPDiff + FL->getFramePointerSaveOffset();
      int NewFPIdx = MF.getFrameInfo().CreateFixedObject(SlotSize, NewFPLoc,
                                                         true);
      SDValue NewFramePtrIdx = DAG.getFrameIndex(NewFPIdx, VT);
      Chain = DAG.getStore(Chain, dl, OldFP, NewFramePtrIdx,
                           MachinePointerInfo::getFixedStack(
                               DAG.getMachineFunction(), NewFPIdx));
    }
  }
  return Chain;
}

/// CalculateTailCallArgDest - Remember Argument for later processing. Calculate
/// the position of the argument.
static void
CalculateTailCallArgDest(SelectionDAG &DAG, MachineFunction &MF, bool isPPC64,
                         SDValue Arg, int SPDiff, unsigned ArgOffset,
                     SmallVectorImpl<TailCallArgumentInfo>& TailCallArguments) {
  int Offset = ArgOffset + SPDiff;
  uint32_t OpSize = (Arg.getValueSizeInBits() + 7) / 8;
  int FI = MF.getFrameInfo().CreateFixedObject(OpSize, Offset, true);
  EVT VT = isPPC64 ? MVT::i64 : MVT::i32;
  SDValue FIN = DAG.getFrameIndex(FI, VT);
  TailCallArgumentInfo Info;
  Info.Arg = Arg;
  Info.FrameIdxOp = FIN;
  Info.FrameIdx = FI;
  TailCallArguments.push_back(Info);
}

/// EmitTCFPAndRetAddrLoad - Emit load from frame pointer and return address
/// stack slot. Returns the chain as result and the loaded frame pointers in
/// LROpOut/FPOpout. Used when tail calling.
SDValue PPCTargetLowering::EmitTailCallLoadFPAndRetAddr(
    SelectionDAG &DAG, int SPDiff, SDValue Chain, SDValue &LROpOut,
    SDValue &FPOpOut, const SDLoc &dl) const {
  if (SPDiff) {
    // Load the LR and FP stack slot for later adjusting.
    EVT VT = Subtarget.isPPC64() ? MVT::i64 : MVT::i32;
    LROpOut = getReturnAddrFrameIndex(DAG);
    LROpOut = DAG.getLoad(VT, dl, Chain, LROpOut, MachinePointerInfo());
    Chain = SDValue(LROpOut.getNode(), 1);

    // When using the 32/64-bit SVR4 ABI there is no need to load the FP stack
    // slot as the FP is never overwritten.
    if (Subtarget.isDarwinABI()) {
      FPOpOut = getFramePointerFrameIndex(DAG);
      FPOpOut = DAG.getLoad(VT, dl, Chain, FPOpOut, MachinePointerInfo());
      Chain = SDValue(FPOpOut.getNode(), 1);
    }
  }
  return Chain;
}

/// CreateCopyOfByValArgument - Make a copy of an aggregate at address specified
/// by "Src" to address "Dst" of size "Size".  Alignment information is
/// specified by the specific parameter attribute. The copy will be passed as
/// a byval function parameter.
/// Sometimes what we are copying is the end of a larger object, the part that
/// does not fit in registers.
static SDValue CreateCopyOfByValArgument(SDValue Src, SDValue Dst,
                                         SDValue Chain, ISD::ArgFlagsTy Flags,
                                         SelectionDAG &DAG, const SDLoc &dl) {
  SDValue SizeNode = DAG.getConstant(Flags.getByValSize(), dl, MVT::i32);
  return DAG.getMemcpy(Chain, dl, Dst, Src, SizeNode, Flags.getByValAlign(),
                       false, false, false, MachinePointerInfo(),
                       MachinePointerInfo());
}

/// LowerMemOpCallTo - Store the argument to the stack or remember it in case of
/// tail calls.
static void LowerMemOpCallTo(
    SelectionDAG &DAG, MachineFunction &MF, SDValue Chain, SDValue Arg,
    SDValue PtrOff, int SPDiff, unsigned ArgOffset, bool isPPC64,
    bool isTailCall, bool isVector, SmallVectorImpl<SDValue> &MemOpChains,
    SmallVectorImpl<TailCallArgumentInfo> &TailCallArguments, const SDLoc &dl) {
  EVT PtrVT = DAG.getTargetLoweringInfo().getPointerTy(DAG.getDataLayout());
  if (!isTailCall) {
    if (isVector) {
      SDValue StackPtr;
      if (isPPC64)
        StackPtr = DAG.getRegister(PPC::X1, MVT::i64);
      else
        StackPtr = DAG.getRegister(PPC::R1, MVT::i32);
      PtrOff = DAG.getNode(ISD::ADD, dl, PtrVT, StackPtr,
                           DAG.getConstant(ArgOffset, dl, PtrVT));
    }
    MemOpChains.push_back(
        DAG.getStore(Chain, dl, Arg, PtrOff, MachinePointerInfo()));
    // Calculate and remember argument location.
  } else CalculateTailCallArgDest(DAG, MF, isPPC64, Arg, SPDiff, ArgOffset,
                                  TailCallArguments);
}

static void
PrepareTailCall(SelectionDAG &DAG, SDValue &InFlag, SDValue &Chain,
                const SDLoc &dl, int SPDiff, unsigned NumBytes, SDValue LROp,
                SDValue FPOp,
                SmallVectorImpl<TailCallArgumentInfo> &TailCallArguments) {
  // Emit a sequence of copyto/copyfrom virtual registers for arguments that
  // might overwrite each other in case of tail call optimization.
  SmallVector<SDValue, 8> MemOpChains2;
  // Do not flag preceding copytoreg stuff together with the following stuff.
  InFlag = SDValue();
  StoreTailCallArgumentsToStackSlot(DAG, Chain, TailCallArguments,
                                    MemOpChains2, dl);
  if (!MemOpChains2.empty())
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, MemOpChains2);

  // Store the return address to the appropriate stack slot.
  Chain = EmitTailCallStoreFPAndRetAddr(DAG, Chain, LROp, FPOp, SPDiff, dl);

  // Emit callseq_end just before tailcall node.
  Chain = DAG.getCALLSEQ_END(Chain, DAG.getIntPtrConstant(NumBytes, dl, true),
                             DAG.getIntPtrConstant(0, dl, true), InFlag, dl);
  InFlag = Chain.getValue(1);
}

// Is this global address that of a function that can be called by name? (as
// opposed to something that must hold a descriptor for an indirect call).
static bool isFunctionGlobalAddress(SDValue Callee) {
  if (GlobalAddressSDNode *G = dyn_cast<GlobalAddressSDNode>(Callee)) {
    if (Callee.getOpcode() == ISD::GlobalTLSAddress ||
        Callee.getOpcode() == ISD::TargetGlobalTLSAddress)
      return false;

    return G->getGlobal()->getValueType()->isFunctionTy();
  }

  return false;
}

static unsigned
PrepareCall(SelectionDAG &DAG, SDValue &Callee, SDValue &InFlag, SDValue &Chain,
            SDValue CallSeqStart, const SDLoc &dl, int SPDiff, bool isTailCall,
            bool isPatchPoint, bool hasNest,
            SmallVectorImpl<std::pair<unsigned, SDValue>> &RegsToPass,
            SmallVectorImpl<SDValue> &Ops, std::vector<EVT> &NodeTys,
            ImmutableCallSite CS, const PPCSubtarget &Subtarget) {
  bool isPPC64 = Subtarget.isPPC64();
  bool isSVR4ABI = Subtarget.isSVR4ABI();
  bool isELFv2ABI = Subtarget.isELFv2ABI();

  EVT PtrVT = DAG.getTargetLoweringInfo().getPointerTy(DAG.getDataLayout());
  NodeTys.push_back(MVT::Other);   // Returns a chain
  NodeTys.push_back(MVT::Glue);    // Returns a flag for retval copy to use.

  unsigned CallOpc = PPCISD::CALL;

  bool needIndirectCall = true;
  if (!isSVR4ABI || !isPPC64)
    if (SDNode *Dest = isBLACompatibleAddress(Callee, DAG)) {
      // If this is an absolute destination address, use the munged value.
      Callee = SDValue(Dest, 0);
      needIndirectCall = false;
    }

  // PC-relative references to external symbols should go through $stub, unless
  // we're building with the leopard linker or later, which automatically
  // synthesizes these stubs.
  const TargetMachine &TM = DAG.getTarget();
  const Module *Mod = DAG.getMachineFunction().getFunction().getParent();
  const GlobalValue *GV = nullptr;
  if (auto *G = dyn_cast<GlobalAddressSDNode>(Callee))
    GV = G->getGlobal();
  bool Local = TM.shouldAssumeDSOLocal(*Mod, GV);
  bool UsePlt = !Local && Subtarget.isTargetELF() && !isPPC64;

  if (isFunctionGlobalAddress(Callee)) {
    GlobalAddressSDNode *G = cast<GlobalAddressSDNode>(Callee);
    // A call to a TLS address is actually an indirect call to a
    // thread-specific pointer.
    unsigned OpFlags = 0;
    if (UsePlt)
      OpFlags = PPCII::MO_PLT;

    // If the callee is a GlobalAddress/ExternalSymbol node (quite common,
    // every direct call is) turn it into a TargetGlobalAddress /
    // TargetExternalSymbol node so that legalize doesn't hack it.
    Callee = DAG.getTargetGlobalAddress(G->getGlobal(), dl,
                                        Callee.getValueType(), 0, OpFlags);
    needIndirectCall = false;
  }

  if (ExternalSymbolSDNode *S = dyn_cast<ExternalSymbolSDNode>(Callee)) {
    unsigned char OpFlags = 0;

    if (UsePlt)
      OpFlags = PPCII::MO_PLT;

    Callee = DAG.getTargetExternalSymbol(S->getSymbol(), Callee.getValueType(),
                                         OpFlags);
    needIndirectCall = false;
  }

  if (isPatchPoint) {
    // We'll form an invalid direct call when lowering a patchpoint; the full
    // sequence for an indirect call is complicated, and many of the
    // instructions introduced might have side effects (and, thus, can't be
    // removed later). The call itself will be removed as soon as the
    // argument/return lowering is complete, so the fact that it has the wrong
    // kind of operands should not really matter.
    needIndirectCall = false;
  }

  if (needIndirectCall) {
    // Otherwise, this is an indirect call.  We have to use a MTCTR/BCTRL pair
    // to do the call, we can't use PPCISD::CALL.
    SDValue MTCTROps[] = {Chain, Callee, InFlag};

    if (isSVR4ABI && isPPC64 && !isELFv2ABI) {
      // Function pointers in the 64-bit SVR4 ABI do not point to the function
      // entry point, but to the function descriptor (the function entry point
      // address is part of the function descriptor though).
      // The function descriptor is a three doubleword structure with the
      // following fields: function entry point, TOC base address and
      // environment pointer.
      // Thus for a call through a function pointer, the following actions need
      // to be performed:
      //   1. Save the TOC of the caller in the TOC save area of its stack
      //      frame (this is done in LowerCall_Darwin() or LowerCall_64SVR4()).
      //   2. Load the address of the function entry point from the function
      //      descriptor.
      //   3. Load the TOC of the callee from the function descriptor into r2.
      //   4. Load the environment pointer from the function descriptor into
      //      r11.
      //   5. Branch to the function entry point address.
      //   6. On return of the callee, the TOC of the caller needs to be
      //      restored (this is done in FinishCall()).
      //
      // The loads are scheduled at the beginning of the call sequence, and the
      // register copies are flagged together to ensure that no other
      // operations can be scheduled in between. E.g. without flagging the
      // copies together, a TOC access in the caller could be scheduled between
      // the assignment of the callee TOC and the branch to the callee, which
      // results in the TOC access going through the TOC of the callee instead
      // of going through the TOC of the caller, which leads to incorrect code.

      // Load the address of the function entry point from the function
      // descriptor.
      SDValue LDChain = CallSeqStart.getValue(CallSeqStart->getNumValues()-1);
      if (LDChain.getValueType() == MVT::Glue)
        LDChain = CallSeqStart.getValue(CallSeqStart->getNumValues()-2);

      auto MMOFlags = Subtarget.hasInvariantFunctionDescriptors()
                          ? (MachineMemOperand::MODereferenceable |
                             MachineMemOperand::MOInvariant)
                          : MachineMemOperand::MONone;

      MachinePointerInfo MPI(CS ? CS.getCalledValue() : nullptr);
      SDValue LoadFuncPtr = DAG.getLoad(MVT::i64, dl, LDChain, Callee, MPI,
                                        /* Alignment = */ 8, MMOFlags);

      // Load environment pointer into r11.
      SDValue PtrOff = DAG.getIntPtrConstant(16, dl);
      SDValue AddPtr = DAG.getNode(ISD::ADD, dl, MVT::i64, Callee, PtrOff);
      SDValue LoadEnvPtr =
          DAG.getLoad(MVT::i64, dl, LDChain, AddPtr, MPI.getWithOffset(16),
                      /* Alignment = */ 8, MMOFlags);

      SDValue TOCOff = DAG.getIntPtrConstant(8, dl);
      SDValue AddTOC = DAG.getNode(ISD::ADD, dl, MVT::i64, Callee, TOCOff);
      SDValue TOCPtr =
          DAG.getLoad(MVT::i64, dl, LDChain, AddTOC, MPI.getWithOffset(8),
                      /* Alignment = */ 8, MMOFlags);

      setUsesTOCBasePtr(DAG);
      SDValue TOCVal = DAG.getCopyToReg(Chain, dl, PPC::X2, TOCPtr,
                                        InFlag);
      Chain = TOCVal.getValue(0);
      InFlag = TOCVal.getValue(1);

      // If the function call has an explicit 'nest' parameter, it takes the
      // place of the environment pointer.
      if (!hasNest) {
        SDValue EnvVal = DAG.getCopyToReg(Chain, dl, PPC::X11, LoadEnvPtr,
                                          InFlag);

        Chain = EnvVal.getValue(0);
        InFlag = EnvVal.getValue(1);
      }

      MTCTROps[0] = Chain;
      MTCTROps[1] = LoadFuncPtr;
      MTCTROps[2] = InFlag;
    }

    Chain = DAG.getNode(PPCISD::MTCTR, dl, NodeTys,
                        makeArrayRef(MTCTROps, InFlag.getNode() ? 3 : 2));
    InFlag = Chain.getValue(1);

    NodeTys.clear();
    NodeTys.push_back(MVT::Other);
    NodeTys.push_back(MVT::Glue);
    Ops.push_back(Chain);
    CallOpc = PPCISD::BCTRL;
    Callee.setNode(nullptr);
    // Add use of X11 (holding environment pointer)
    if (isSVR4ABI && isPPC64 && !isELFv2ABI && !hasNest)
      Ops.push_back(DAG.getRegister(PPC::X11, PtrVT));
    // Add CTR register as callee so a bctr can be emitted later.
    if (isTailCall)
      Ops.push_back(DAG.getRegister(isPPC64 ? PPC::CTR8 : PPC::CTR, PtrVT));
  }

  // If this is a direct call, pass the chain and the callee.
  if (Callee.getNode()) {
    Ops.push_back(Chain);
    Ops.push_back(Callee);
  }
  // If this is a tail call add stack pointer delta.
  if (isTailCall)
    Ops.push_back(DAG.getConstant(SPDiff, dl, MVT::i32));

  // Add argument registers to the end of the list so that they are known live
  // into the call.
  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i)
    Ops.push_back(DAG.getRegister(RegsToPass[i].first,
                                  RegsToPass[i].second.getValueType()));

  // All calls, in both the ELF V1 and V2 ABIs, need the TOC register live
  // into the call.
  // We do need to reserve X2 to appease the verifier for the PATCHPOINT.
  if (isSVR4ABI && isPPC64) {
    setUsesTOCBasePtr(DAG);

    // We cannot add X2 as an operand here for PATCHPOINT, because there is no
    // way to mark dependencies as implicit here. We will add the X2 dependency
    // in EmitInstrWithCustomInserter.
    if (!isPatchPoint) 
      Ops.push_back(DAG.getRegister(PPC::X2, PtrVT));
  }

  return CallOpc;
}

SDValue PPCTargetLowering::LowerCallResult(
    SDValue Chain, SDValue InFlag, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCRetInfo(CallConv, isVarArg, DAG.getMachineFunction(), RVLocs,
                    *DAG.getContext());

  CCRetInfo.AnalyzeCallResult(
      Ins, (Subtarget.isSVR4ABI() && CallConv == CallingConv::Cold)
               ? RetCC_PPC_Cold
               : RetCC_PPC);

  // Copy all of the result registers out of their specified physreg.
  for (unsigned i = 0, e = RVLocs.size(); i != e; ++i) {
    CCValAssign &VA = RVLocs[i];
    assert(VA.isRegLoc() && "Can only return in registers!");

    SDValue Val = DAG.getCopyFromReg(Chain, dl,
                                     VA.getLocReg(), VA.getLocVT(), InFlag);
    Chain = Val.getValue(1);
    InFlag = Val.getValue(2);

    switch (VA.getLocInfo()) {
    default: llvm_unreachable("Unknown loc info!");
    case CCValAssign::Full: break;
    case CCValAssign::AExt:
      Val = DAG.getNode(ISD::TRUNCATE, dl, VA.getValVT(), Val);
      break;
    case CCValAssign::ZExt:
      Val = DAG.getNode(ISD::AssertZext, dl, VA.getLocVT(), Val,
                        DAG.getValueType(VA.getValVT()));
      Val = DAG.getNode(ISD::TRUNCATE, dl, VA.getValVT(), Val);
      break;
    case CCValAssign::SExt:
      Val = DAG.getNode(ISD::AssertSext, dl, VA.getLocVT(), Val,
                        DAG.getValueType(VA.getValVT()));
      Val = DAG.getNode(ISD::TRUNCATE, dl, VA.getValVT(), Val);
      break;
    }

    InVals.push_back(Val);
  }

  return Chain;
}

SDValue PPCTargetLowering::FinishCall(
    CallingConv::ID CallConv, const SDLoc &dl, bool isTailCall, bool isVarArg,
    bool isPatchPoint, bool hasNest, SelectionDAG &DAG,
    SmallVector<std::pair<unsigned, SDValue>, 8> &RegsToPass, SDValue InFlag,
    SDValue Chain, SDValue CallSeqStart, SDValue &Callee, int SPDiff,
    unsigned NumBytes, const SmallVectorImpl<ISD::InputArg> &Ins,
    SmallVectorImpl<SDValue> &InVals, ImmutableCallSite CS) const {
  std::vector<EVT> NodeTys;
  SmallVector<SDValue, 8> Ops;
  unsigned CallOpc = PrepareCall(DAG, Callee, InFlag, Chain, CallSeqStart, dl,
                                 SPDiff, isTailCall, isPatchPoint, hasNest,
                                 RegsToPass, Ops, NodeTys, CS, Subtarget);

  // Add implicit use of CR bit 6 for 32-bit SVR4 vararg calls
  if (isVarArg && Subtarget.isSVR4ABI() && !Subtarget.isPPC64())
    Ops.push_back(DAG.getRegister(PPC::CR1EQ, MVT::i32));

  // When performing tail call optimization the callee pops its arguments off
  // the stack. Account for this here so these bytes can be pushed back on in
  // PPCFrameLowering::eliminateCallFramePseudoInstr.
  int BytesCalleePops =
    (CallConv == CallingConv::Fast &&
     getTargetMachine().Options.GuaranteedTailCallOpt) ? NumBytes : 0;

  // Add a register mask operand representing the call-preserved registers.
  const TargetRegisterInfo *TRI = Subtarget.getRegisterInfo();
  const uint32_t *Mask =
      TRI->getCallPreservedMask(DAG.getMachineFunction(), CallConv);
  assert(Mask && "Missing call preserved mask for calling convention");
  Ops.push_back(DAG.getRegisterMask(Mask));

  if (InFlag.getNode())
    Ops.push_back(InFlag);

  // Emit tail call.
  if (isTailCall) {
    assert(((Callee.getOpcode() == ISD::Register &&
             cast<RegisterSDNode>(Callee)->getReg() == PPC::CTR) ||
            Callee.getOpcode() == ISD::TargetExternalSymbol ||
            Callee.getOpcode() == ISD::TargetGlobalAddress ||
            isa<ConstantSDNode>(Callee)) &&
    "Expecting an global address, external symbol, absolute value or register");

    DAG.getMachineFunction().getFrameInfo().setHasTailCall();
    return DAG.getNode(PPCISD::TC_RETURN, dl, MVT::Other, Ops);
  }

  // Add a NOP immediately after the branch instruction when using the 64-bit
  // SVR4 ABI. At link time, if caller and callee are in a different module and
  // thus have a different TOC, the call will be replaced with a call to a stub
  // function which saves the current TOC, loads the TOC of the callee and
  // branches to the callee. The NOP will be replaced with a load instruction
  // which restores the TOC of the caller from the TOC save slot of the current
  // stack frame. If caller and callee belong to the same module (and have the
  // same TOC), the NOP will remain unchanged.

  MachineFunction &MF = DAG.getMachineFunction();
  if (!isTailCall && Subtarget.isSVR4ABI()&& Subtarget.isPPC64() &&
      !isPatchPoint) {
    if (CallOpc == PPCISD::BCTRL) {
      // This is a call through a function pointer.
      // Restore the caller TOC from the save area into R2.
      // See PrepareCall() for more information about calls through function
      // pointers in the 64-bit SVR4 ABI.
      // We are using a target-specific load with r2 hard coded, because the
      // result of a target-independent load would never go directly into r2,
      // since r2 is a reserved register (which prevents the register allocator
      // from allocating it), resulting in an additional register being
      // allocated and an unnecessary move instruction being generated.
      CallOpc = PPCISD::BCTRL_LOAD_TOC;

      EVT PtrVT = getPointerTy(DAG.getDataLayout());
      SDValue StackPtr = DAG.getRegister(PPC::X1, PtrVT);
      unsigned TOCSaveOffset = Subtarget.getFrameLowering()->getTOCSaveOffset();
      SDValue TOCOff = DAG.getIntPtrConstant(TOCSaveOffset, dl);
      SDValue AddTOC = DAG.getNode(ISD::ADD, dl, MVT::i64, StackPtr, TOCOff);

      // The address needs to go after the chain input but before the flag (or
      // any other variadic arguments).
      Ops.insert(std::next(Ops.begin()), AddTOC);
    } else if (CallOpc == PPCISD::CALL &&
      !callsShareTOCBase(&MF.getFunction(), Callee, DAG.getTarget())) {
      // Otherwise insert NOP for non-local calls.
      CallOpc = PPCISD::CALL_NOP;
    }
  }

  Chain = DAG.getNode(CallOpc, dl, NodeTys, Ops);
  InFlag = Chain.getValue(1);

  Chain = DAG.getCALLSEQ_END(Chain, DAG.getIntPtrConstant(NumBytes, dl, true),
                             DAG.getIntPtrConstant(BytesCalleePops, dl, true),
                             InFlag, dl);
  if (!Ins.empty())
    InFlag = Chain.getValue(1);

  return LowerCallResult(Chain, InFlag, CallConv, isVarArg,
                         Ins, dl, DAG, InVals);
}

SDValue
PPCTargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                             SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG                     = CLI.DAG;
  SDLoc &dl                             = CLI.DL;
  SmallVectorImpl<ISD::OutputArg> &Outs = CLI.Outs;
  SmallVectorImpl<SDValue> &OutVals     = CLI.OutVals;
  SmallVectorImpl<ISD::InputArg> &Ins   = CLI.Ins;
  SDValue Chain                         = CLI.Chain;
  SDValue Callee                        = CLI.Callee;
  bool &isTailCall                      = CLI.IsTailCall;
  CallingConv::ID CallConv              = CLI.CallConv;
  bool isVarArg                         = CLI.IsVarArg;
  bool isPatchPoint                     = CLI.IsPatchPoint;
  ImmutableCallSite CS                  = CLI.CS;

  if (isTailCall) {
    if (Subtarget.useLongCalls() && !(CS && CS.isMustTailCall()))
      isTailCall = false;
    else if (Subtarget.isSVR4ABI() && Subtarget.isPPC64())
      isTailCall =
        IsEligibleForTailCallOptimization_64SVR4(Callee, CallConv, CS,
                                                 isVarArg, Outs, Ins, DAG);
    else
      isTailCall = IsEligibleForTailCallOptimization(Callee, CallConv, isVarArg,
                                                     Ins, DAG);
    if (isTailCall) {
      ++NumTailCalls;
      if (!getTargetMachine().Options.GuaranteedTailCallOpt)
        ++NumSiblingCalls;

      assert(isa<GlobalAddressSDNode>(Callee) &&
             "Callee should be an llvm::Function object.");
      LLVM_DEBUG(
          const GlobalValue *GV =
              cast<GlobalAddressSDNode>(Callee)->getGlobal();
          const unsigned Width =
              80 - strlen("TCO caller: ") - strlen(", callee linkage: 0, 0");
          dbgs() << "TCO caller: "
                 << left_justify(DAG.getMachineFunction().getName(), Width)
                 << ", callee linkage: " << GV->getVisibility() << ", "
                 << GV->getLinkage() << "\n");
    }
  }

  if (!isTailCall && CS && CS.isMustTailCall())
    report_fatal_error("failed to perform tail call elimination on a call "
                       "site marked musttail");

  // When long calls (i.e. indirect calls) are always used, calls are always
  // made via function pointer. If we have a function name, first translate it
  // into a pointer.
  if (Subtarget.useLongCalls() && isa<GlobalAddressSDNode>(Callee) &&
      !isTailCall)
    Callee = LowerGlobalAddress(Callee, DAG);

  if (Subtarget.isSVR4ABI()) {
    if (Subtarget.isPPC64())
      return LowerCall_64SVR4(Chain, Callee, CallConv, isVarArg,
                              isTailCall, isPatchPoint, Outs, OutVals, Ins,
                              dl, DAG, InVals, CS);
    else
      return LowerCall_32SVR4(Chain, Callee, CallConv, isVarArg,
                              isTailCall, isPatchPoint, Outs, OutVals, Ins,
                              dl, DAG, InVals, CS);
  }

  return LowerCall_Darwin(Chain, Callee, CallConv, isVarArg,
                          isTailCall, isPatchPoint, Outs, OutVals, Ins,
                          dl, DAG, InVals, CS);
}

SDValue PPCTargetLowering::LowerCall_32SVR4(
    SDValue Chain, SDValue Callee, CallingConv::ID CallConv, bool isVarArg,
    bool isTailCall, bool isPatchPoint,
    const SmallVectorImpl<ISD::OutputArg> &Outs,
    const SmallVectorImpl<SDValue> &OutVals,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals,
    ImmutableCallSite CS) const {
  // See PPCTargetLowering::LowerFormalArguments_32SVR4() for a description
  // of the 32-bit SVR4 ABI stack frame layout.

  assert((CallConv == CallingConv::C ||
          CallConv == CallingConv::Cold ||
          CallConv == CallingConv::Fast) && "Unknown calling convention!");

  unsigned PtrByteSize = 4;

  MachineFunction &MF = DAG.getMachineFunction();

  // Mark this function as potentially containing a function that contains a
  // tail call. As a consequence the frame pointer will be used for dynamicalloc
  // and restoring the callers stack pointer in this functions epilog. This is
  // done because by tail calling the called function might overwrite the value
  // in this function's (MF) stack pointer stack slot 0(SP).
  if (getTargetMachine().Options.GuaranteedTailCallOpt &&
      CallConv == CallingConv::Fast)
    MF.getInfo<PPCFunctionInfo>()->setHasFastCall();

  // Count how many bytes are to be pushed on the stack, including the linkage
  // area, parameter list area and the part of the local variable space which
  // contains copies of aggregates which are passed by value.

  // Assign locations to all of the outgoing arguments.
  SmallVector<CCValAssign, 16> ArgLocs;
  PPCCCState CCInfo(CallConv, isVarArg, MF, ArgLocs, *DAG.getContext());

  // Reserve space for the linkage area on the stack.
  CCInfo.AllocateStack(Subtarget.getFrameLowering()->getLinkageSize(),
                       PtrByteSize);
  if (useSoftFloat())
    CCInfo.PreAnalyzeCallOperands(Outs);

  if (isVarArg) {
    // Handle fixed and variable vector arguments differently.
    // Fixed vector arguments go into registers as long as registers are
    // available. Variable vector arguments always go into memory.
    unsigned NumArgs = Outs.size();

    for (unsigned i = 0; i != NumArgs; ++i) {
      MVT ArgVT = Outs[i].VT;
      ISD::ArgFlagsTy ArgFlags = Outs[i].Flags;
      bool Result;

      if (Outs[i].IsFixed) {
        Result = CC_PPC32_SVR4(i, ArgVT, ArgVT, CCValAssign::Full, ArgFlags,
                               CCInfo);
      } else {
        Result = CC_PPC32_SVR4_VarArg(i, ArgVT, ArgVT, CCValAssign::Full,
                                      ArgFlags, CCInfo);
      }

      if (Result) {
#ifndef NDEBUG
        errs() << "Call operand #" << i << " has unhandled type "
             << EVT(ArgVT).getEVTString() << "\n";
#endif
        llvm_unreachable(nullptr);
      }
    }
  } else {
    // All arguments are treated the same.
    CCInfo.AnalyzeCallOperands(Outs, CC_PPC32_SVR4);
  }
  CCInfo.clearWasPPCF128();

  // Assign locations to all of the outgoing aggregate by value arguments.
  SmallVector<CCValAssign, 16> ByValArgLocs;
  CCState CCByValInfo(CallConv, isVarArg, MF, ByValArgLocs, *DAG.getContext());

  // Reserve stack space for the allocations in CCInfo.
  CCByValInfo.AllocateStack(CCInfo.getNextStackOffset(), PtrByteSize);

  CCByValInfo.AnalyzeCallOperands(Outs, CC_PPC32_SVR4_ByVal);

  // Size of the linkage area, parameter list area and the part of the local
  // space variable where copies of aggregates which are passed by value are
  // stored.
  unsigned NumBytes = CCByValInfo.getNextStackOffset();

  // Calculate by how many bytes the stack has to be adjusted in case of tail
  // call optimization.
  int SPDiff = CalculateTailCallSPDiff(DAG, isTailCall, NumBytes);

  // Adjust the stack pointer for the new arguments...
  // These operations are automatically eliminated by the prolog/epilog pass
  Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, dl);
  SDValue CallSeqStart = Chain;

  // Load the return address and frame pointer so it can be moved somewhere else
  // later.
  SDValue LROp, FPOp;
  Chain = EmitTailCallLoadFPAndRetAddr(DAG, SPDiff, Chain, LROp, FPOp, dl);

  // Set up a copy of the stack pointer for use loading and storing any
  // arguments that may not fit in the registers available for argument
  // passing.
  SDValue StackPtr = DAG.getRegister(PPC::R1, MVT::i32);

  SmallVector<std::pair<unsigned, SDValue>, 8> RegsToPass;
  SmallVector<TailCallArgumentInfo, 8> TailCallArguments;
  SmallVector<SDValue, 8> MemOpChains;

  bool seenFloatArg = false;
  // Walk the register/memloc assignments, inserting copies/loads.
  for (unsigned i = 0, j = 0, e = ArgLocs.size();
       i != e;
       ++i) {
    CCValAssign &VA = ArgLocs[i];
    SDValue Arg = OutVals[i];
    ISD::ArgFlagsTy Flags = Outs[i].Flags;

    if (Flags.isByVal()) {
      // Argument is an aggregate which is passed by value, thus we need to
      // create a copy of it in the local variable space of the current stack
      // frame (which is the stack frame of the caller) and pass the address of
      // this copy to the callee.
      assert((j < ByValArgLocs.size()) && "Index out of bounds!");
      CCValAssign &ByValVA = ByValArgLocs[j++];
      assert((VA.getValNo() == ByValVA.getValNo()) && "ValNo mismatch!");

      // Memory reserved in the local variable space of the callers stack frame.
      unsigned LocMemOffset = ByValVA.getLocMemOffset();

      SDValue PtrOff = DAG.getIntPtrConstant(LocMemOffset, dl);
      PtrOff = DAG.getNode(ISD::ADD, dl, getPointerTy(MF.getDataLayout()),
                           StackPtr, PtrOff);

      // Create a copy of the argument in the local area of the current
      // stack frame.
      SDValue MemcpyCall =
        CreateCopyOfByValArgument(Arg, PtrOff,
                                  CallSeqStart.getNode()->getOperand(0),
                                  Flags, DAG, dl);

      // This must go outside the CALLSEQ_START..END.
      SDValue NewCallSeqStart = DAG.getCALLSEQ_START(MemcpyCall, NumBytes, 0,
                                                     SDLoc(MemcpyCall));
      DAG.ReplaceAllUsesWith(CallSeqStart.getNode(),
                             NewCallSeqStart.getNode());
      Chain = CallSeqStart = NewCallSeqStart;

      // Pass the address of the aggregate copy on the stack either in a
      // physical register or in the parameter list area of the current stack
      // frame to the callee.
      Arg = PtrOff;
    }

    // When useCRBits() is true, there can be i1 arguments.
    // It is because getRegisterType(MVT::i1) => MVT::i1,
    // and for other integer types getRegisterType() => MVT::i32.
    // Extend i1 and ensure callee will get i32.
    if (Arg.getValueType() == MVT::i1)
      Arg = DAG.getNode(Flags.isSExt() ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND,
                        dl, MVT::i32, Arg);

    if (VA.isRegLoc()) {
      seenFloatArg |= VA.getLocVT().isFloatingPoint();
      // Put argument in a physical register.
      RegsToPass.push_back(std::make_pair(VA.getLocReg(), Arg));
    } else {
      // Put argument in the parameter list area of the current stack frame.
      assert(VA.isMemLoc());
      unsigned LocMemOffset = VA.getLocMemOffset();

      if (!isTailCall) {
        SDValue PtrOff = DAG.getIntPtrConstant(LocMemOffset, dl);
        PtrOff = DAG.getNode(ISD::ADD, dl, getPointerTy(MF.getDataLayout()),
                             StackPtr, PtrOff);

        MemOpChains.push_back(
            DAG.getStore(Chain, dl, Arg, PtrOff, MachinePointerInfo()));
      } else {
        // Calculate and remember argument location.
        CalculateTailCallArgDest(DAG, MF, false, Arg, SPDiff, LocMemOffset,
                                 TailCallArguments);
      }
    }
  }

  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, MemOpChains);

  // Build a sequence of copy-to-reg nodes chained together with token chain
  // and flag operands which copy the outgoing args into the appropriate regs.
  SDValue InFlag;
  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i) {
    Chain = DAG.getCopyToReg(Chain, dl, RegsToPass[i].first,
                             RegsToPass[i].second, InFlag);
    InFlag = Chain.getValue(1);
  }

  // Set CR bit 6 to true if this is a vararg call with floating args passed in
  // registers.
  if (isVarArg) {
    SDVTList VTs = DAG.getVTList(MVT::Other, MVT::Glue);
    SDValue Ops[] = { Chain, InFlag };

    Chain = DAG.getNode(seenFloatArg ? PPCISD::CR6SET : PPCISD::CR6UNSET,
                        dl, VTs, makeArrayRef(Ops, InFlag.getNode() ? 2 : 1));

    InFlag = Chain.getValue(1);
  }

  if (isTailCall)
    PrepareTailCall(DAG, InFlag, Chain, dl, SPDiff, NumBytes, LROp, FPOp,
                    TailCallArguments);

  return FinishCall(CallConv, dl, isTailCall, isVarArg, isPatchPoint,
                    /* unused except on PPC64 ELFv1 */ false, DAG,
                    RegsToPass, InFlag, Chain, CallSeqStart, Callee, SPDiff,
                    NumBytes, Ins, InVals, CS);
}

// Copy an argument into memory, being careful to do this outside the
// call sequence for the call to which the argument belongs.
SDValue PPCTargetLowering::createMemcpyOutsideCallSeq(
    SDValue Arg, SDValue PtrOff, SDValue CallSeqStart, ISD::ArgFlagsTy Flags,
    SelectionDAG &DAG, const SDLoc &dl) const {
  SDValue MemcpyCall = CreateCopyOfByValArgument(Arg, PtrOff,
                        CallSeqStart.getNode()->getOperand(0),
                        Flags, DAG, dl);
  // The MEMCPY must go outside the CALLSEQ_START..END.
  int64_t FrameSize = CallSeqStart.getConstantOperandVal(1);
  SDValue NewCallSeqStart = DAG.getCALLSEQ_START(MemcpyCall, FrameSize, 0,
                                                 SDLoc(MemcpyCall));
  DAG.ReplaceAllUsesWith(CallSeqStart.getNode(),
                         NewCallSeqStart.getNode());
  return NewCallSeqStart;
}

SDValue PPCTargetLowering::LowerCall_64SVR4(
    SDValue Chain, SDValue Callee, CallingConv::ID CallConv, bool isVarArg,
    bool isTailCall, bool isPatchPoint,
    const SmallVectorImpl<ISD::OutputArg> &Outs,
    const SmallVectorImpl<SDValue> &OutVals,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals,
    ImmutableCallSite CS) const {
  bool isELFv2ABI = Subtarget.isELFv2ABI();
  bool isLittleEndian = Subtarget.isLittleEndian();
  unsigned NumOps = Outs.size();
  bool hasNest = false;
  bool IsSibCall = false;

  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  unsigned PtrByteSize = 8;

  MachineFunction &MF = DAG.getMachineFunction();

  if (isTailCall && !getTargetMachine().Options.GuaranteedTailCallOpt)
    IsSibCall = true;

  // Mark this function as potentially containing a function that contains a
  // tail call. As a consequence the frame pointer will be used for dynamicalloc
  // and restoring the callers stack pointer in this functions epilog. This is
  // done because by tail calling the called function might overwrite the value
  // in this function's (MF) stack pointer stack slot 0(SP).
  if (getTargetMachine().Options.GuaranteedTailCallOpt &&
      CallConv == CallingConv::Fast)
    MF.getInfo<PPCFunctionInfo>()->setHasFastCall();

  assert(!(CallConv == CallingConv::Fast && isVarArg) &&
         "fastcc not supported on varargs functions");

  // Count how many bytes are to be pushed on the stack, including the linkage
  // area, and parameter passing area.  On ELFv1, the linkage area is 48 bytes
  // reserved space for [SP][CR][LR][2 x unused][TOC]; on ELFv2, the linkage
  // area is 32 bytes reserved space for [SP][CR][LR][TOC].
  unsigned LinkageSize = Subtarget.getFrameLowering()->getLinkageSize();
  unsigned NumBytes = LinkageSize;
  unsigned GPR_idx = 0, FPR_idx = 0, VR_idx = 0;
  unsigned &QFPR_idx = FPR_idx;

  static const MCPhysReg GPR[] = {
    PPC::X3, PPC::X4, PPC::X5, PPC::X6,
    PPC::X7, PPC::X8, PPC::X9, PPC::X10,
  };
  static const MCPhysReg VR[] = {
    PPC::V2, PPC::V3, PPC::V4, PPC::V5, PPC::V6, PPC::V7, PPC::V8,
    PPC::V9, PPC::V10, PPC::V11, PPC::V12, PPC::V13
  };

  const unsigned NumGPRs = array_lengthof(GPR);
  const unsigned NumFPRs = useSoftFloat() ? 0 : 13;
  const unsigned NumVRs  = array_lengthof(VR);
  const unsigned NumQFPRs = NumFPRs;

  // On ELFv2, we can avoid allocating the parameter area if all the arguments
  // can be passed to the callee in registers.
  // For the fast calling convention, there is another check below.
  // Note: We should keep consistent with LowerFormalArguments_64SVR4()
  bool HasParameterArea = !isELFv2ABI || isVarArg || CallConv == CallingConv::Fast;
  if (!HasParameterArea) {
    unsigned ParamAreaSize = NumGPRs * PtrByteSize;
    unsigned AvailableFPRs = NumFPRs;
    unsigned AvailableVRs = NumVRs;
    unsigned NumBytesTmp = NumBytes;
    for (unsigned i = 0; i != NumOps; ++i) {
      if (Outs[i].Flags.isNest()) continue;
      if (CalculateStackSlotUsed(Outs[i].VT, Outs[i].ArgVT, Outs[i].Flags,
                                PtrByteSize, LinkageSize, ParamAreaSize,
                                NumBytesTmp, AvailableFPRs, AvailableVRs,
                                Subtarget.hasQPX()))
        HasParameterArea = true;
    }
  }

  // When using the fast calling convention, we don't provide backing for
  // arguments that will be in registers.
  unsigned NumGPRsUsed = 0, NumFPRsUsed = 0, NumVRsUsed = 0;

  // Avoid allocating parameter area for fastcc functions if all the arguments
  // can be passed in the registers.
  if (CallConv == CallingConv::Fast)
    HasParameterArea = false;

  // Add up all the space actually used.
  for (unsigned i = 0; i != NumOps; ++i) {
    ISD::ArgFlagsTy Flags = Outs[i].Flags;
    EVT ArgVT = Outs[i].VT;
    EVT OrigVT = Outs[i].ArgVT;

    if (Flags.isNest())
      continue;

    if (CallConv == CallingConv::Fast) {
      if (Flags.isByVal()) {
        NumGPRsUsed += (Flags.getByValSize()+7)/8;
        if (NumGPRsUsed > NumGPRs)
          HasParameterArea = true;
      } else {
        switch (ArgVT.getSimpleVT().SimpleTy) {
        default: llvm_unreachable("Unexpected ValueType for argument!");
        case MVT::i1:
        case MVT::i32:
        case MVT::i64:
          if (++NumGPRsUsed <= NumGPRs)
            continue;
          break;
        case MVT::v4i32:
        case MVT::v8i16:
        case MVT::v16i8:
        case MVT::v2f64:
        case MVT::v2i64:
        case MVT::v1i128:
        case MVT::f128:
          if (++NumVRsUsed <= NumVRs)
            continue;
          break;
        case MVT::v4f32:
          // When using QPX, this is handled like a FP register, otherwise, it
          // is an Altivec register.
          if (Subtarget.hasQPX()) {
            if (++NumFPRsUsed <= NumFPRs)
              continue;
          } else {
            if (++NumVRsUsed <= NumVRs)
              continue;
          }
          break;
        case MVT::f32:
        case MVT::f64:
        case MVT::v4f64: // QPX
        case MVT::v4i1:  // QPX
          if (++NumFPRsUsed <= NumFPRs)
            continue;
          break;
        }
        HasParameterArea = true;
      }
    }

    /* Respect alignment of argument on the stack.  */
    unsigned Align =
      CalculateStackSlotAlignment(ArgVT, OrigVT, Flags, PtrByteSize);
    NumBytes = ((NumBytes + Align - 1) / Align) * Align;

    NumBytes += CalculateStackSlotSize(ArgVT, Flags, PtrByteSize);
    if (Flags.isInConsecutiveRegsLast())
      NumBytes = ((NumBytes + PtrByteSize - 1)/PtrByteSize) * PtrByteSize;
  }

  unsigned NumBytesActuallyUsed = NumBytes;

  // In the old ELFv1 ABI,
  // the prolog code of the callee may store up to 8 GPR argument registers to
  // the stack, allowing va_start to index over them in memory if its varargs.
  // Because we cannot tell if this is needed on the caller side, we have to
  // conservatively assume that it is needed.  As such, make sure we have at
  // least enough stack space for the caller to store the 8 GPRs.
  // In the ELFv2 ABI, we allocate the parameter area iff a callee
  // really requires memory operands, e.g. a vararg function.
  if (HasParameterArea)
    NumBytes = std::max(NumBytes, LinkageSize + 8 * PtrByteSize);
  else
    NumBytes = LinkageSize;

  // Tail call needs the stack to be aligned.
  if (getTargetMachine().Options.GuaranteedTailCallOpt &&
      CallConv == CallingConv::Fast)
    NumBytes = EnsureStackAlignment(Subtarget.getFrameLowering(), NumBytes);

  int SPDiff = 0;

  // Calculate by how many bytes the stack has to be adjusted in case of tail
  // call optimization.
  if (!IsSibCall)
    SPDiff = CalculateTailCallSPDiff(DAG, isTailCall, NumBytes);

  // To protect arguments on the stack from being clobbered in a tail call,
  // force all the loads to happen before doing any other lowering.
  if (isTailCall)
    Chain = DAG.getStackArgumentTokenFactor(Chain);

  // Adjust the stack pointer for the new arguments...
  // These operations are automatically eliminated by the prolog/epilog pass
  if (!IsSibCall)
    Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, dl);
  SDValue CallSeqStart = Chain;

  // Load the return address and frame pointer so it can be move somewhere else
  // later.
  SDValue LROp, FPOp;
  Chain = EmitTailCallLoadFPAndRetAddr(DAG, SPDiff, Chain, LROp, FPOp, dl);

  // Set up a copy of the stack pointer for use loading and storing any
  // arguments that may not fit in the registers available for argument
  // passing.
  SDValue StackPtr = DAG.getRegister(PPC::X1, MVT::i64);

  // Figure out which arguments are going to go in registers, and which in
  // memory.  Also, if this is a vararg function, floating point operations
  // must be stored to our stack, and loaded into integer regs as well, if
  // any integer regs are available for argument passing.
  unsigned ArgOffset = LinkageSize;

  SmallVector<std::pair<unsigned, SDValue>, 8> RegsToPass;
  SmallVector<TailCallArgumentInfo, 8> TailCallArguments;

  SmallVector<SDValue, 8> MemOpChains;
  for (unsigned i = 0; i != NumOps; ++i) {
    SDValue Arg = OutVals[i];
    ISD::ArgFlagsTy Flags = Outs[i].Flags;
    EVT ArgVT = Outs[i].VT;
    EVT OrigVT = Outs[i].ArgVT;

    // PtrOff will be used to store the current argument to the stack if a
    // register cannot be found for it.
    SDValue PtrOff;

    // We re-align the argument offset for each argument, except when using the
    // fast calling convention, when we need to make sure we do that only when
    // we'll actually use a stack slot.
    auto ComputePtrOff = [&]() {
      /* Respect alignment of argument on the stack.  */
      unsigned Align =
        CalculateStackSlotAlignment(ArgVT, OrigVT, Flags, PtrByteSize);
      ArgOffset = ((ArgOffset + Align - 1) / Align) * Align;

      PtrOff = DAG.getConstant(ArgOffset, dl, StackPtr.getValueType());

      PtrOff = DAG.getNode(ISD::ADD, dl, PtrVT, StackPtr, PtrOff);
    };

    if (CallConv != CallingConv::Fast) {
      ComputePtrOff();

      /* Compute GPR index associated with argument offset.  */
      GPR_idx = (ArgOffset - LinkageSize) / PtrByteSize;
      GPR_idx = std::min(GPR_idx, NumGPRs);
    }

    // Promote integers to 64-bit values.
    if (Arg.getValueType() == MVT::i32 || Arg.getValueType() == MVT::i1) {
      // FIXME: Should this use ANY_EXTEND if neither sext nor zext?
      unsigned ExtOp = Flags.isSExt() ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND;
      Arg = DAG.getNode(ExtOp, dl, MVT::i64, Arg);
    }

    // FIXME memcpy is used way more than necessary.  Correctness first.
    // Note: "by value" is code for passing a structure by value, not
    // basic types.
    if (Flags.isByVal()) {
      // Note: Size includes alignment padding, so
      //   struct x { short a; char b; }
      // will have Size = 4.  With #pragma pack(1), it will have Size = 3.
      // These are the proper values we need for right-justifying the
      // aggregate in a parameter register.
      unsigned Size = Flags.getByValSize();

      // An empty aggregate parameter takes up no storage and no
      // registers.
      if (Size == 0)
        continue;

      if (CallConv == CallingConv::Fast)
        ComputePtrOff();

      // All aggregates smaller than 8 bytes must be passed right-justified.
      if (Size==1 || Size==2 || Size==4) {
        EVT VT = (Size==1) ? MVT::i8 : ((Size==2) ? MVT::i16 : MVT::i32);
        if (GPR_idx != NumGPRs) {
          SDValue Load = DAG.getExtLoad(ISD::EXTLOAD, dl, PtrVT, Chain, Arg,
                                        MachinePointerInfo(), VT);
          MemOpChains.push_back(Load.getValue(1));
          RegsToPass.push_back(std::make_pair(GPR[GPR_idx++], Load));

          ArgOffset += PtrByteSize;
          continue;
        }
      }

      if (GPR_idx == NumGPRs && Size < 8) {
        SDValue AddPtr = PtrOff;
        if (!isLittleEndian) {
          SDValue Const = DAG.getConstant(PtrByteSize - Size, dl,
                                          PtrOff.getValueType());
          AddPtr = DAG.getNode(ISD::ADD, dl, PtrVT, PtrOff, Const);
        }
        Chain = CallSeqStart = createMemcpyOutsideCallSeq(Arg, AddPtr,
                                                          CallSeqStart,
                                                          Flags, DAG, dl);
        ArgOffset += PtrByteSize;
        continue;
      }
      // Copy entire object into memory.  There are cases where gcc-generated
      // code assumes it is there, even if it could be put entirely into
      // registers.  (This is not what the doc says.)

      // FIXME: The above statement is likely due to a misunderstanding of the
      // documents.  All arguments must be copied into the parameter area BY
      // THE CALLEE in the event that the callee takes the address of any
      // formal argument.  That has not yet been implemented.  However, it is
      // reasonable to use the stack area as a staging area for the register
      // load.

      // Skip this for small aggregates, as we will use the same slot for a
      // right-justified copy, below.
      if (Size >= 8)
        Chain = CallSeqStart = createMemcpyOutsideCallSeq(Arg, PtrOff,
                                                          CallSeqStart,
                                                          Flags, DAG, dl);

      // When a register is available, pass a small aggregate right-justified.
      if (Size < 8 && GPR_idx != NumGPRs) {
        // The easiest way to get this right-justified in a register
        // is to copy the structure into the rightmost portion of a
        // local variable slot, then load the whole slot into the
        // register.
        // FIXME: The memcpy seems to produce pretty awful code for
        // small aggregates, particularly for packed ones.
        // FIXME: It would be preferable to use the slot in the
        // parameter save area instead of a new local variable.
        SDValue AddPtr = PtrOff;
        if (!isLittleEndian) {
          SDValue Const = DAG.getConstant(8 - Size, dl, PtrOff.getValueType());
          AddPtr = DAG.getNode(ISD::ADD, dl, PtrVT, PtrOff, Const);
        }
        Chain = CallSeqStart = createMemcpyOutsideCallSeq(Arg, AddPtr,
                                                          CallSeqStart,
                                                          Flags, DAG, dl);

        // Load the slot into the register.
        SDValue Load =
            DAG.getLoad(PtrVT, dl, Chain, PtrOff, MachinePointerInfo());
        MemOpChains.push_back(Load.getValue(1));
        RegsToPass.push_back(std::make_pair(GPR[GPR_idx++], Load));

        // Done with this argument.
        ArgOffset += PtrByteSize;
        continue;
      }

      // For aggregates larger than PtrByteSize, copy the pieces of the
      // object that fit into registers from the parameter save area.
      for (unsigned j=0; j<Size; j+=PtrByteSize) {
        SDValue Const = DAG.getConstant(j, dl, PtrOff.getValueType());
        SDValue AddArg = DAG.getNode(ISD::ADD, dl, PtrVT, Arg, Const);
        if (GPR_idx != NumGPRs) {
          SDValue Load =
              DAG.getLoad(PtrVT, dl, Chain, AddArg, MachinePointerInfo());
          MemOpChains.push_back(Load.getValue(1));
          RegsToPass.push_back(std::make_pair(GPR[GPR_idx++], Load));
          ArgOffset += PtrByteSize;
        } else {
          ArgOffset += ((Size - j + PtrByteSize-1)/PtrByteSize)*PtrByteSize;
          break;
        }
      }
      continue;
    }

    switch (Arg.getSimpleValueType().SimpleTy) {
    default: llvm_unreachable("Unexpected ValueType for argument!");
    case MVT::i1:
    case MVT::i32:
    case MVT::i64:
      if (Flags.isNest()) {
        // The 'nest' parameter, if any, is passed in R11.
        RegsToPass.push_back(std::make_pair(PPC::X11, Arg));
        hasNest = true;
        break;
      }

      // These can be scalar arguments or elements of an integer array type
      // passed directly.  Clang may use those instead of "byval" aggregate
      // types to avoid forcing arguments to memory unnecessarily.
      if (GPR_idx != NumGPRs) {
        RegsToPass.push_back(std::make_pair(GPR[GPR_idx++], Arg));
      } else {
        if (CallConv == CallingConv::Fast)
          ComputePtrOff();

        assert(HasParameterArea &&
               "Parameter area must exist to pass an argument in memory.");
        LowerMemOpCallTo(DAG, MF, Chain, Arg, PtrOff, SPDiff, ArgOffset,
                         true, isTailCall, false, MemOpChains,
                         TailCallArguments, dl);
        if (CallConv == CallingConv::Fast)
          ArgOffset += PtrByteSize;
      }
      if (CallConv != CallingConv::Fast)
        ArgOffset += PtrByteSize;
      break;
    case MVT::f32:
    case MVT::f64: {
      // These can be scalar arguments or elements of a float array type
      // passed directly.  The latter are used to implement ELFv2 homogenous
      // float aggregates.

      // Named arguments go into FPRs first, and once they overflow, the
      // remaining arguments go into GPRs and then the parameter save area.
      // Unnamed arguments for vararg functions always go to GPRs and
      // then the parameter save area.  For now, put all arguments to vararg
      // routines always in both locations (FPR *and* GPR or stack slot).
      bool NeedGPROrStack = isVarArg || FPR_idx == NumFPRs;
      bool NeededLoad = false;

      // First load the argument into the next available FPR.
      if (FPR_idx != NumFPRs)
        RegsToPass.push_back(std::make_pair(FPR[FPR_idx++], Arg));

      // Next, load the argument into GPR or stack slot if needed.
      if (!NeedGPROrStack)
        ;
      else if (GPR_idx != NumGPRs && CallConv != CallingConv::Fast) {
        // FIXME: We may want to re-enable this for CallingConv::Fast on the P8
        // once we support fp <-> gpr moves.

        // In the non-vararg case, this can only ever happen in the
        // presence of f32 array types, since otherwise we never run
        // out of FPRs before running out of GPRs.
        SDValue ArgVal;

        // Double values are always passed in a single GPR.
        if (Arg.getValueType() != MVT::f32) {
          ArgVal = DAG.getNode(ISD::BITCAST, dl, MVT::i64, Arg);

        // Non-array float values are extended and passed in a GPR.
        } else if (!Flags.isInConsecutiveRegs()) {
          ArgVal = DAG.getNode(ISD::BITCAST, dl, MVT::i32, Arg);
          ArgVal = DAG.getNode(ISD::ANY_EXTEND, dl, MVT::i64, ArgVal);

        // If we have an array of floats, we collect every odd element
        // together with its predecessor into one GPR.
        } else if (ArgOffset % PtrByteSize != 0) {
          SDValue Lo, Hi;
          Lo = DAG.getNode(ISD::BITCAST, dl, MVT::i32, OutVals[i - 1]);
          Hi = DAG.getNode(ISD::BITCAST, dl, MVT::i32, Arg);
          if (!isLittleEndian)
            std::swap(Lo, Hi);
          ArgVal = DAG.getNode(ISD::BUILD_PAIR, dl, MVT::i64, Lo, Hi);

        // The final element, if even, goes into the first half of a GPR.
        } else if (Flags.isInConsecutiveRegsLast()) {
          ArgVal = DAG.getNode(ISD::BITCAST, dl, MVT::i32, Arg);
          ArgVal = DAG.getNode(ISD::ANY_EXTEND, dl, MVT::i64, ArgVal);
          if (!isLittleEndian)
            ArgVal = DAG.getNode(ISD::SHL, dl, MVT::i64, ArgVal,
                                 DAG.getConstant(32, dl, MVT::i32));

        // Non-final even elements are skipped; they will be handled
        // together the with subsequent argument on the next go-around.
        } else
          ArgVal = SDValue();

        if (ArgVal.getNode())
          RegsToPass.push_back(std::make_pair(GPR[GPR_idx++], ArgVal));
      } else {
        if (CallConv == CallingConv::Fast)
          ComputePtrOff();

        // Single-precision floating-point values are mapped to the
        // second (rightmost) word of the stack doubleword.
        if (Arg.getValueType() == MVT::f32 &&
            !isLittleEndian && !Flags.isInConsecutiveRegs()) {
          SDValue ConstFour = DAG.getConstant(4, dl, PtrOff.getValueType());
          PtrOff = DAG.getNode(ISD::ADD, dl, PtrVT, PtrOff, ConstFour);
        }

        assert(HasParameterArea &&
               "Parameter area must exist to pass an argument in memory.");
        LowerMemOpCallTo(DAG, MF, Chain, Arg, PtrOff, SPDiff, ArgOffset,
                         true, isTailCall, false, MemOpChains,
                         TailCallArguments, dl);

        NeededLoad = true;
      }
      // When passing an array of floats, the array occupies consecutive
      // space in the argument area; only round up to the next doubleword
      // at the end of the array.  Otherwise, each float takes 8 bytes.
      if (CallConv != CallingConv::Fast || NeededLoad) {
        ArgOffset += (Arg.getValueType() == MVT::f32 &&
                      Flags.isInConsecutiveRegs()) ? 4 : 8;
        if (Flags.isInConsecutiveRegsLast())
          ArgOffset = ((ArgOffset + PtrByteSize - 1)/PtrByteSize) * PtrByteSize;
      }
      break;
    }
    case MVT::v4f32:
    case MVT::v4i32:
    case MVT::v8i16:
    case MVT::v16i8:
    case MVT::v2f64:
    case MVT::v2i64:
    case MVT::v1i128:
    case MVT::f128:
      if (!Subtarget.hasQPX()) {
      // These can be scalar arguments or elements of a vector array type
      // passed directly.  The latter are used to implement ELFv2 homogenous
      // vector aggregates.

      // For a varargs call, named arguments go into VRs or on the stack as
      // usual; unnamed arguments always go to the stack or the corresponding
      // GPRs when within range.  For now, we always put the value in both
      // locations (or even all three).
      if (isVarArg) {
        assert(HasParameterArea &&
               "Parameter area must exist if we have a varargs call.");
        // We could elide this store in the case where the object fits
        // entirely in R registers.  Maybe later.
        SDValue Store =
            DAG.getStore(Chain, dl, Arg, PtrOff, MachinePointerInfo());
        MemOpChains.push_back(Store);
        if (VR_idx != NumVRs) {
          SDValue Load =
              DAG.getLoad(MVT::v4f32, dl, Store, PtrOff, MachinePointerInfo());
          MemOpChains.push_back(Load.getValue(1));
          RegsToPass.push_back(std::make_pair(VR[VR_idx++], Load));
        }
        ArgOffset += 16;
        for (unsigned i=0; i<16; i+=PtrByteSize) {
          if (GPR_idx == NumGPRs)
            break;
          SDValue Ix = DAG.getNode(ISD::ADD, dl, PtrVT, PtrOff,
                                   DAG.getConstant(i, dl, PtrVT));
          SDValue Load =
              DAG.getLoad(PtrVT, dl, Store, Ix, MachinePointerInfo());
          MemOpChains.push_back(Load.getValue(1));
          RegsToPass.push_back(std::make_pair(GPR[GPR_idx++], Load));
        }
        break;
      }

      // Non-varargs Altivec params go into VRs or on the stack.
      if (VR_idx != NumVRs) {
        RegsToPass.push_back(std::make_pair(VR[VR_idx++], Arg));
      } else {
        if (CallConv == CallingConv::Fast)
          ComputePtrOff();

        assert(HasParameterArea &&
               "Parameter area must exist to pass an argument in memory.");
        LowerMemOpCallTo(DAG, MF, Chain, Arg, PtrOff, SPDiff, ArgOffset,
                         true, isTailCall, true, MemOpChains,
                         TailCallArguments, dl);
        if (CallConv == CallingConv::Fast)
          ArgOffset += 16;
      }

      if (CallConv != CallingConv::Fast)
        ArgOffset += 16;
      break;
      } // not QPX

      assert(Arg.getValueType().getSimpleVT().SimpleTy == MVT::v4f32 &&
             "Invalid QPX parameter type");

      LLVM_FALLTHROUGH;
    case MVT::v4f64:
    case MVT::v4i1: {
      bool IsF32 = Arg.getValueType().getSimpleVT().SimpleTy == MVT::v4f32;
      if (isVarArg) {
        assert(HasParameterArea &&
               "Parameter area must exist if we have a varargs call.");
        // We could elide this store in the case where the object fits
        // entirely in R registers.  Maybe later.
        SDValue Store =
            DAG.getStore(Chain, dl, Arg, PtrOff, MachinePointerInfo());
        MemOpChains.push_back(Store);
        if (QFPR_idx != NumQFPRs) {
          SDValue Load = DAG.getLoad(IsF32 ? MVT::v4f32 : MVT::v4f64, dl, Store,
                                     PtrOff, MachinePointerInfo());
          MemOpChains.push_back(Load.getValue(1));
          RegsToPass.push_back(std::make_pair(QFPR[QFPR_idx++], Load));
        }
        ArgOffset += (IsF32 ? 16 : 32);
        for (unsigned i = 0; i < (IsF32 ? 16U : 32U); i += PtrByteSize) {
          if (GPR_idx == NumGPRs)
            break;
          SDValue Ix = DAG.getNode(ISD::ADD, dl, PtrVT, PtrOff,
                                   DAG.getConstant(i, dl, PtrVT));
          SDValue Load =
              DAG.getLoad(PtrVT, dl, Store, Ix, MachinePointerInfo());
          MemOpChains.push_back(Load.getValue(1));
          RegsToPass.push_back(std::make_pair(GPR[GPR_idx++], Load));
        }
        break;
      }

      // Non-varargs QPX params go into registers or on the stack.
      if (QFPR_idx != NumQFPRs) {
        RegsToPass.push_back(std::make_pair(QFPR[QFPR_idx++], Arg));
      } else {
        if (CallConv == CallingConv::Fast)
          ComputePtrOff();

        assert(HasParameterArea &&
               "Parameter area must exist to pass an argument in memory.");
        LowerMemOpCallTo(DAG, MF, Chain, Arg, PtrOff, SPDiff, ArgOffset,
                         true, isTailCall, true, MemOpChains,
                         TailCallArguments, dl);
        if (CallConv == CallingConv::Fast)
          ArgOffset += (IsF32 ? 16 : 32);
      }

      if (CallConv != CallingConv::Fast)
        ArgOffset += (IsF32 ? 16 : 32);
      break;
      }
    }
  }

  assert((!HasParameterArea || NumBytesActuallyUsed == ArgOffset) &&
         "mismatch in size of parameter area");
  (void)NumBytesActuallyUsed;

  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, MemOpChains);

  // Check if this is an indirect call (MTCTR/BCTRL).
  // See PrepareCall() for more information about calls through function
  // pointers in the 64-bit SVR4 ABI.
  if (!isTailCall && !isPatchPoint &&
      !isFunctionGlobalAddress(Callee) &&
      !isa<ExternalSymbolSDNode>(Callee)) {
    // Load r2 into a virtual register and store it to the TOC save area.
    setUsesTOCBasePtr(DAG);
    SDValue Val = DAG.getCopyFromReg(Chain, dl, PPC::X2, MVT::i64);
    // TOC save area offset.
    unsigned TOCSaveOffset = Subtarget.getFrameLowering()->getTOCSaveOffset();
    SDValue PtrOff = DAG.getIntPtrConstant(TOCSaveOffset, dl);
    SDValue AddPtr = DAG.getNode(ISD::ADD, dl, PtrVT, StackPtr, PtrOff);
    Chain = DAG.getStore(
        Val.getValue(1), dl, Val, AddPtr,
        MachinePointerInfo::getStack(DAG.getMachineFunction(), TOCSaveOffset));
    // In the ELFv2 ABI, R12 must contain the address of an indirect callee.
    // This does not mean the MTCTR instruction must use R12; it's easier
    // to model this as an extra parameter, so do that.
    if (isELFv2ABI && !isPatchPoint)
      RegsToPass.push_back(std::make_pair((unsigned)PPC::X12, Callee));
  }

  // Build a sequence of copy-to-reg nodes chained together with token chain
  // and flag operands which copy the outgoing args into the appropriate regs.
  SDValue InFlag;
  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i) {
    Chain = DAG.getCopyToReg(Chain, dl, RegsToPass[i].first,
                             RegsToPass[i].second, InFlag);
    InFlag = Chain.getValue(1);
  }

  if (isTailCall && !IsSibCall)
    PrepareTailCall(DAG, InFlag, Chain, dl, SPDiff, NumBytes, LROp, FPOp,
                    TailCallArguments);

  return FinishCall(CallConv, dl, isTailCall, isVarArg, isPatchPoint, hasNest,
                    DAG, RegsToPass, InFlag, Chain, CallSeqStart, Callee,
                    SPDiff, NumBytes, Ins, InVals, CS);
}

SDValue PPCTargetLowering::LowerCall_Darwin(
    SDValue Chain, SDValue Callee, CallingConv::ID CallConv, bool isVarArg,
    bool isTailCall, bool isPatchPoint,
    const SmallVectorImpl<ISD::OutputArg> &Outs,
    const SmallVectorImpl<SDValue> &OutVals,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals,
    ImmutableCallSite CS) const {
  unsigned NumOps = Outs.size();

  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  bool isPPC64 = PtrVT == MVT::i64;
  unsigned PtrByteSize = isPPC64 ? 8 : 4;

  MachineFunction &MF = DAG.getMachineFunction();

  // Mark this function as potentially containing a function that contains a
  // tail call. As a consequence the frame pointer will be used for dynamicalloc
  // and restoring the callers stack pointer in this functions epilog. This is
  // done because by tail calling the called function might overwrite the value
  // in this function's (MF) stack pointer stack slot 0(SP).
  if (getTargetMachine().Options.GuaranteedTailCallOpt &&
      CallConv == CallingConv::Fast)
    MF.getInfo<PPCFunctionInfo>()->setHasFastCall();

  // Count how many bytes are to be pushed on the stack, including the linkage
  // area, and parameter passing area.  We start with 24/48 bytes, which is
  // prereserved space for [SP][CR][LR][3 x unused].
  unsigned LinkageSize = Subtarget.getFrameLowering()->getLinkageSize();
  unsigned NumBytes = LinkageSize;

  // Add up all the space actually used.
  // In 32-bit non-varargs calls, Altivec parameters all go at the end; usually
  // they all go in registers, but we must reserve stack space for them for
  // possible use by the caller.  In varargs or 64-bit calls, parameters are
  // assigned stack space in order, with padding so Altivec parameters are
  // 16-byte aligned.
  unsigned nAltivecParamsAtEnd = 0;
  for (unsigned i = 0; i != NumOps; ++i) {
    ISD::ArgFlagsTy Flags = Outs[i].Flags;
    EVT ArgVT = Outs[i].VT;
    // Varargs Altivec parameters are padded to a 16 byte boundary.
    if (ArgVT == MVT::v4f32 || ArgVT == MVT::v4i32 ||
        ArgVT == MVT::v8i16 || ArgVT == MVT::v16i8 ||
        ArgVT == MVT::v2f64 || ArgVT == MVT::v2i64) {
      if (!isVarArg && !isPPC64) {
        // Non-varargs Altivec parameters go after all the non-Altivec
        // parameters; handle those later so we know how much padding we need.
        nAltivecParamsAtEnd++;
        continue;
      }
      // Varargs and 64-bit Altivec parameters are padded to 16 byte boundary.
      NumBytes = ((NumBytes+15)/16)*16;
    }
    NumBytes += CalculateStackSlotSize(ArgVT, Flags, PtrByteSize);
  }

  // Allow for Altivec parameters at the end, if needed.
  if (nAltivecParamsAtEnd) {
    NumBytes = ((NumBytes+15)/16)*16;
    NumBytes += 16*nAltivecParamsAtEnd;
  }

  // The prolog code of the callee may store up to 8 GPR argument registers to
  // the stack, allowing va_start to index over them in memory if its varargs.
  // Because we cannot tell if this is needed on the caller side, we have to
  // conservatively assume that it is needed.  As such, make sure we have at
  // least enough stack space for the caller to store the 8 GPRs.
  NumBytes = std::max(NumBytes, LinkageSize + 8 * PtrByteSize);

  // Tail call needs the stack to be aligned.
  if (getTargetMachine().Options.GuaranteedTailCallOpt &&
      CallConv == CallingConv::Fast)
    NumBytes = EnsureStackAlignment(Subtarget.getFrameLowering(), NumBytes);

  // Calculate by how many bytes the stack has to be adjusted in case of tail
  // call optimization.
  int SPDiff = CalculateTailCallSPDiff(DAG, isTailCall, NumBytes);

  // To protect arguments on the stack from being clobbered in a tail call,
  // force all the loads to happen before doing any other lowering.
  if (isTailCall)
    Chain = DAG.getStackArgumentTokenFactor(Chain);

  // Adjust the stack pointer for the new arguments...
  // These operations are automatically eliminated by the prolog/epilog pass
  Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, dl);
  SDValue CallSeqStart = Chain;

  // Load the return address and frame pointer so it can be move somewhere else
  // later.
  SDValue LROp, FPOp;
  Chain = EmitTailCallLoadFPAndRetAddr(DAG, SPDiff, Chain, LROp, FPOp, dl);

  // Set up a copy of the stack pointer for use loading and storing any
  // arguments that may not fit in the registers available for argument
  // passing.
  SDValue StackPtr;
  if (isPPC64)
    StackPtr = DAG.getRegister(PPC::X1, MVT::i64);
  else
    StackPtr = DAG.getRegister(PPC::R1, MVT::i32);

  // Figure out which arguments are going to go in registers, and which in
  // memory.  Also, if this is a vararg function, floating point operations
  // must be stored to our stack, and loaded into integer regs as well, if
  // any integer regs are available for argument passing.
  unsigned ArgOffset = LinkageSize;
  unsigned GPR_idx = 0, FPR_idx = 0, VR_idx = 0;

  static const MCPhysReg GPR_32[] = {           // 32-bit registers.
    PPC::R3, PPC::R4, PPC::R5, PPC::R6,
    PPC::R7, PPC::R8, PPC::R9, PPC::R10,
  };
  static const MCPhysReg GPR_64[] = {           // 64-bit registers.
    PPC::X3, PPC::X4, PPC::X5, PPC::X6,
    PPC::X7, PPC::X8, PPC::X9, PPC::X10,
  };
  static const MCPhysReg VR[] = {
    PPC::V2, PPC::V3, PPC::V4, PPC::V5, PPC::V6, PPC::V7, PPC::V8,
    PPC::V9, PPC::V10, PPC::V11, PPC::V12, PPC::V13
  };
  const unsigned NumGPRs = array_lengthof(GPR_32);
  const unsigned NumFPRs = 13;
  const unsigned NumVRs  = array_lengthof(VR);

  const MCPhysReg *GPR = isPPC64 ? GPR_64 : GPR_32;

  SmallVector<std::pair<unsigned, SDValue>, 8> RegsToPass;
  SmallVector<TailCallArgumentInfo, 8> TailCallArguments;

  SmallVector<SDValue, 8> MemOpChains;
  for (unsigned i = 0; i != NumOps; ++i) {
    SDValue Arg = OutVals[i];
    ISD::ArgFlagsTy Flags = Outs[i].Flags;

    // PtrOff will be used to store the current argument to the stack if a
    // register cannot be found for it.
    SDValue PtrOff;

    PtrOff = DAG.getConstant(ArgOffset, dl, StackPtr.getValueType());

    PtrOff = DAG.getNode(ISD::ADD, dl, PtrVT, StackPtr, PtrOff);

    // On PPC64, promote integers to 64-bit values.
    if (isPPC64 && Arg.getValueType() == MVT::i32) {
      // FIXME: Should this use ANY_EXTEND if neither sext nor zext?
      unsigned ExtOp = Flags.isSExt() ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND;
      Arg = DAG.getNode(ExtOp, dl, MVT::i64, Arg);
    }

    // FIXME memcpy is used way more than necessary.  Correctness first.
    // Note: "by value" is code for passing a structure by value, not
    // basic types.
    if (Flags.isByVal()) {
      unsigned Size = Flags.getByValSize();
      // Very small objects are passed right-justified.  Everything else is
      // passed left-justified.
      if (Size==1 || Size==2) {
        EVT VT = (Size==1) ? MVT::i8 : MVT::i16;
        if (GPR_idx != NumGPRs) {
          SDValue Load = DAG.getExtLoad(ISD::EXTLOAD, dl, PtrVT, Chain, Arg,
                                        MachinePointerInfo(), VT);
          MemOpChains.push_back(Load.getValue(1));
          RegsToPass.push_back(std::make_pair(GPR[GPR_idx++], Load));

          ArgOffset += PtrByteSize;
        } else {
          SDValue Const = DAG.getConstant(PtrByteSize - Size, dl,
                                          PtrOff.getValueType());
          SDValue AddPtr = DAG.getNode(ISD::ADD, dl, PtrVT, PtrOff, Const);
          Chain = CallSeqStart = createMemcpyOutsideCallSeq(Arg, AddPtr,
                                                            CallSeqStart,
                                                            Flags, DAG, dl);
          ArgOffset += PtrByteSize;
        }
        continue;
      }
      // Copy entire object into memory.  There are cases where gcc-generated
      // code assumes it is there, even if it could be put entirely into
      // registers.  (This is not what the doc says.)
      Chain = CallSeqStart = createMemcpyOutsideCallSeq(Arg, PtrOff,
                                                        CallSeqStart,
                                                        Flags, DAG, dl);

      // For small aggregates (Darwin only) and aggregates >= PtrByteSize,
      // copy the pieces of the object that fit into registers from the
      // parameter save area.
      for (unsigned j=0; j<Size; j+=PtrByteSize) {
        SDValue Const = DAG.getConstant(j, dl, PtrOff.getValueType());
        SDValue AddArg = DAG.getNode(ISD::ADD, dl, PtrVT, Arg, Const);
        if (GPR_idx != NumGPRs) {
          SDValue Load =
              DAG.getLoad(PtrVT, dl, Chain, AddArg, MachinePointerInfo());
          MemOpChains.push_back(Load.getValue(1));
          RegsToPass.push_back(std::make_pair(GPR[GPR_idx++], Load));
          ArgOffset += PtrByteSize;
        } else {
          ArgOffset += ((Size - j + PtrByteSize-1)/PtrByteSize)*PtrByteSize;
          break;
        }
      }
      continue;
    }

    switch (Arg.getSimpleValueType().SimpleTy) {
    default: llvm_unreachable("Unexpected ValueType for argument!");
    case MVT::i1:
    case MVT::i32:
    case MVT::i64:
      if (GPR_idx != NumGPRs) {
        if (Arg.getValueType() == MVT::i1)
          Arg = DAG.getNode(ISD::ZERO_EXTEND, dl, PtrVT, Arg);

        RegsToPass.push_back(std::make_pair(GPR[GPR_idx++], Arg));
      } else {
        LowerMemOpCallTo(DAG, MF, Chain, Arg, PtrOff, SPDiff, ArgOffset,
                         isPPC64, isTailCall, false, MemOpChains,
                         TailCallArguments, dl);
      }
      ArgOffset += PtrByteSize;
      break;
    case MVT::f32:
    case MVT::f64:
      if (FPR_idx != NumFPRs) {
        RegsToPass.push_back(std::make_pair(FPR[FPR_idx++], Arg));

        if (isVarArg) {
          SDValue Store =
              DAG.getStore(Chain, dl, Arg, PtrOff, MachinePointerInfo());
          MemOpChains.push_back(Store);

          // Float varargs are always shadowed in available integer registers
          if (GPR_idx != NumGPRs) {
            SDValue Load =
                DAG.getLoad(PtrVT, dl, Store, PtrOff, MachinePointerInfo());
            MemOpChains.push_back(Load.getValue(1));
            RegsToPass.push_back(std::make_pair(GPR[GPR_idx++], Load));
          }
          if (GPR_idx != NumGPRs && Arg.getValueType() == MVT::f64 && !isPPC64){
            SDValue ConstFour = DAG.getConstant(4, dl, PtrOff.getValueType());
            PtrOff = DAG.getNode(ISD::ADD, dl, PtrVT, PtrOff, ConstFour);
            SDValue Load =
                DAG.getLoad(PtrVT, dl, Store, PtrOff, MachinePointerInfo());
            MemOpChains.push_back(Load.getValue(1));
            RegsToPass.push_back(std::make_pair(GPR[GPR_idx++], Load));
          }
        } else {
          // If we have any FPRs remaining, we may also have GPRs remaining.
          // Args passed in FPRs consume either 1 (f32) or 2 (f64) available
          // GPRs.
          if (GPR_idx != NumGPRs)
            ++GPR_idx;
          if (GPR_idx != NumGPRs && Arg.getValueType() == MVT::f64 &&
              !isPPC64)  // PPC64 has 64-bit GPR's obviously :)
            ++GPR_idx;
        }
      } else
        LowerMemOpCallTo(DAG, MF, Chain, Arg, PtrOff, SPDiff, ArgOffset,
                         isPPC64, isTailCall, false, MemOpChains,
                         TailCallArguments, dl);
      if (isPPC64)
        ArgOffset += 8;
      else
        ArgOffset += Arg.getValueType() == MVT::f32 ? 4 : 8;
      break;
    case MVT::v4f32:
    case MVT::v4i32:
    case MVT::v8i16:
    case MVT::v16i8:
      if (isVarArg) {
        // These go aligned on the stack, or in the corresponding R registers
        // when within range.  The Darwin PPC ABI doc claims they also go in
        // V registers; in fact gcc does this only for arguments that are
        // prototyped, not for those that match the ...  We do it for all
        // arguments, seems to work.
        while (ArgOffset % 16 !=0) {
          ArgOffset += PtrByteSize;
          if (GPR_idx != NumGPRs)
            GPR_idx++;
        }
        // We could elide this store in the case where the object fits
        // entirely in R registers.  Maybe later.
        PtrOff = DAG.getNode(ISD::ADD, dl, PtrVT, StackPtr,
                             DAG.getConstant(ArgOffset, dl, PtrVT));
        SDValue Store =
            DAG.getStore(Chain, dl, Arg, PtrOff, MachinePointerInfo());
        MemOpChains.push_back(Store);
        if (VR_idx != NumVRs) {
          SDValue Load =
              DAG.getLoad(MVT::v4f32, dl, Store, PtrOff, MachinePointerInfo());
          MemOpChains.push_back(Load.getValue(1));
          RegsToPass.push_back(std::make_pair(VR[VR_idx++], Load));
        }
        ArgOffset += 16;
        for (unsigned i=0; i<16; i+=PtrByteSize) {
          if (GPR_idx == NumGPRs)
            break;
          SDValue Ix = DAG.getNode(ISD::ADD, dl, PtrVT, PtrOff,
                                   DAG.getConstant(i, dl, PtrVT));
          SDValue Load =
              DAG.getLoad(PtrVT, dl, Store, Ix, MachinePointerInfo());
          MemOpChains.push_back(Load.getValue(1));
          RegsToPass.push_back(std::make_pair(GPR[GPR_idx++], Load));
        }
        break;
      }

      // Non-varargs Altivec params generally go in registers, but have
      // stack space allocated at the end.
      if (VR_idx != NumVRs) {
        // Doesn't have GPR space allocated.
        RegsToPass.push_back(std::make_pair(VR[VR_idx++], Arg));
      } else if (nAltivecParamsAtEnd==0) {
        // We are emitting Altivec params in order.
        LowerMemOpCallTo(DAG, MF, Chain, Arg, PtrOff, SPDiff, ArgOffset,
                         isPPC64, isTailCall, true, MemOpChains,
                         TailCallArguments, dl);
        ArgOffset += 16;
      }
      break;
    }
  }
  // If all Altivec parameters fit in registers, as they usually do,
  // they get stack space following the non-Altivec parameters.  We
  // don't track this here because nobody below needs it.
  // If there are more Altivec parameters than fit in registers emit
  // the stores here.
  if (!isVarArg && nAltivecParamsAtEnd > NumVRs) {
    unsigned j = 0;
    // Offset is aligned; skip 1st 12 params which go in V registers.
    ArgOffset = ((ArgOffset+15)/16)*16;
    ArgOffset += 12*16;
    for (unsigned i = 0; i != NumOps; ++i) {
      SDValue Arg = OutVals[i];
      EVT ArgType = Outs[i].VT;
      if (ArgType==MVT::v4f32 || ArgType==MVT::v4i32 ||
          ArgType==MVT::v8i16 || ArgType==MVT::v16i8) {
        if (++j > NumVRs) {
          SDValue PtrOff;
          // We are emitting Altivec params in order.
          LowerMemOpCallTo(DAG, MF, Chain, Arg, PtrOff, SPDiff, ArgOffset,
                           isPPC64, isTailCall, true, MemOpChains,
                           TailCallArguments, dl);
          ArgOffset += 16;
        }
      }
    }
  }

  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, MemOpChains);

  // On Darwin, R12 must contain the address of an indirect callee.  This does
  // not mean the MTCTR instruction must use R12; it's easier to model this as
  // an extra parameter, so do that.
  if (!isTailCall &&
      !isFunctionGlobalAddress(Callee) &&
      !isa<ExternalSymbolSDNode>(Callee) &&
      !isBLACompatibleAddress(Callee, DAG))
    RegsToPass.push_back(std::make_pair((unsigned)(isPPC64 ? PPC::X12 :
                                                   PPC::R12), Callee));

  // Build a sequence of copy-to-reg nodes chained together with token chain
  // and flag operands which copy the outgoing args into the appropriate regs.
  SDValue InFlag;
  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i) {
    Chain = DAG.getCopyToReg(Chain, dl, RegsToPass[i].first,
                             RegsToPass[i].second, InFlag);
    InFlag = Chain.getValue(1);
  }

  if (isTailCall)
    PrepareTailCall(DAG, InFlag, Chain, dl, SPDiff, NumBytes, LROp, FPOp,
                    TailCallArguments);

  return FinishCall(CallConv, dl, isTailCall, isVarArg, isPatchPoint,
                    /* unused except on PPC64 ELFv1 */ false, DAG,
                    RegsToPass, InFlag, Chain, CallSeqStart, Callee, SPDiff,
                    NumBytes, Ins, InVals, CS);
}

bool
PPCTargetLowering::CanLowerReturn(CallingConv::ID CallConv,
                                  MachineFunction &MF, bool isVarArg,
                                  const SmallVectorImpl<ISD::OutputArg> &Outs,
                                  LLVMContext &Context) const {
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, isVarArg, MF, RVLocs, Context);
  return CCInfo.CheckReturn(
      Outs, (Subtarget.isSVR4ABI() && CallConv == CallingConv::Cold)
                ? RetCC_PPC_Cold
                : RetCC_PPC);
}

SDValue
PPCTargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                               bool isVarArg,
                               const SmallVectorImpl<ISD::OutputArg> &Outs,
                               const SmallVectorImpl<SDValue> &OutVals,
                               const SDLoc &dl, SelectionDAG &DAG) const {
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeReturn(Outs,
                       (Subtarget.isSVR4ABI() && CallConv == CallingConv::Cold)
                           ? RetCC_PPC_Cold
                           : RetCC_PPC);

  SDValue Flag;
  SmallVector<SDValue, 4> RetOps(1, Chain);

  // Copy the result values into the output registers.
  for (unsigned i = 0; i != RVLocs.size(); ++i) {
    CCValAssign &VA = RVLocs[i];
    assert(VA.isRegLoc() && "Can only return in registers!");

    SDValue Arg = OutVals[i];

    switch (VA.getLocInfo()) {
    default: llvm_unreachable("Unknown loc info!");
    case CCValAssign::Full: break;
    case CCValAssign::AExt:
      Arg = DAG.getNode(ISD::ANY_EXTEND, dl, VA.getLocVT(), Arg);
      break;
    case CCValAssign::ZExt:
      Arg = DAG.getNode(ISD::ZERO_EXTEND, dl, VA.getLocVT(), Arg);
      break;
    case CCValAssign::SExt:
      Arg = DAG.getNode(ISD::SIGN_EXTEND, dl, VA.getLocVT(), Arg);
      break;
    }

    Chain = DAG.getCopyToReg(Chain, dl, VA.getLocReg(), Arg, Flag);
    Flag = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  const PPCRegisterInfo *TRI = Subtarget.getRegisterInfo();
  const MCPhysReg *I =
    TRI->getCalleeSavedRegsViaCopy(&DAG.getMachineFunction());
  if (I) {
    for (; *I; ++I) {

      if (PPC::G8RCRegClass.contains(*I))
        RetOps.push_back(DAG.getRegister(*I, MVT::i64));
      else if (PPC::F8RCRegClass.contains(*I))
        RetOps.push_back(DAG.getRegister(*I, MVT::getFloatingPointVT(64)));
      else if (PPC::CRRCRegClass.contains(*I))
        RetOps.push_back(DAG.getRegister(*I, MVT::i1));
      else if (PPC::VRRCRegClass.contains(*I))
        RetOps.push_back(DAG.getRegister(*I, MVT::Other));
      else
        llvm_unreachable("Unexpected register class in CSRsViaCopy!");
    }
  }

  RetOps[0] = Chain;  // Update chain.

  // Add the flag if we have it.
  if (Flag.getNode())
    RetOps.push_back(Flag);

  return DAG.getNode(PPCISD::RET_FLAG, dl, MVT::Other, RetOps);
}

SDValue
PPCTargetLowering::LowerGET_DYNAMIC_AREA_OFFSET(SDValue Op,
                                                SelectionDAG &DAG) const {
  SDLoc dl(Op);

  // Get the correct type for integers.
  EVT IntVT = Op.getValueType();

  // Get the inputs.
  SDValue Chain = Op.getOperand(0);
  SDValue FPSIdx = getFramePointerFrameIndex(DAG);
  // Build a DYNAREAOFFSET node.
  SDValue Ops[2] = {Chain, FPSIdx};
  SDVTList VTs = DAG.getVTList(IntVT);
  return DAG.getNode(PPCISD::DYNAREAOFFSET, dl, VTs, Ops);
}

SDValue PPCTargetLowering::LowerSTACKRESTORE(SDValue Op,
                                             SelectionDAG &DAG) const {
  // When we pop the dynamic allocation we need to restore the SP link.
  SDLoc dl(Op);

  // Get the correct type for pointers.
  EVT PtrVT = getPointerTy(DAG.getDataLayout());

  // Construct the stack pointer operand.
  bool isPPC64 = Subtarget.isPPC64();
  unsigned SP = isPPC64 ? PPC::X1 : PPC::R1;
  SDValue StackPtr = DAG.getRegister(SP, PtrVT);

  // Get the operands for the STACKRESTORE.
  SDValue Chain = Op.getOperand(0);
  SDValue SaveSP = Op.getOperand(1);

  // Load the old link SP.
  SDValue LoadLinkSP =
      DAG.getLoad(PtrVT, dl, Chain, StackPtr, MachinePointerInfo());

  // Restore the stack pointer.
  Chain = DAG.getCopyToReg(LoadLinkSP.getValue(1), dl, SP, SaveSP);

  // Store the old link SP.
  return DAG.getStore(Chain, dl, LoadLinkSP, StackPtr, MachinePointerInfo());
}

SDValue PPCTargetLowering::getReturnAddrFrameIndex(SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  bool isPPC64 = Subtarget.isPPC64();
  EVT PtrVT = getPointerTy(MF.getDataLayout());

  // Get current frame pointer save index.  The users of this index will be
  // primarily DYNALLOC instructions.
  PPCFunctionInfo *FI = MF.getInfo<PPCFunctionInfo>();
  int RASI = FI->getReturnAddrSaveIndex();

  // If the frame pointer save index hasn't been defined yet.
  if (!RASI) {
    // Find out what the fix offset of the frame pointer save area.
    int LROffset = Subtarget.getFrameLowering()->getReturnSaveOffset();
    // Allocate the frame index for frame pointer save area.
    RASI = MF.getFrameInfo().CreateFixedObject(isPPC64? 8 : 4, LROffset, false);
    // Save the result.
    FI->setReturnAddrSaveIndex(RASI);
  }
  return DAG.getFrameIndex(RASI, PtrVT);
}

SDValue
PPCTargetLowering::getFramePointerFrameIndex(SelectionDAG & DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  bool isPPC64 = Subtarget.isPPC64();
  EVT PtrVT = getPointerTy(MF.getDataLayout());

  // Get current frame pointer save index.  The users of this index will be
  // primarily DYNALLOC instructions.
  PPCFunctionInfo *FI = MF.getInfo<PPCFunctionInfo>();
  int FPSI = FI->getFramePointerSaveIndex();

  // If the frame pointer save index hasn't been defined yet.
  if (!FPSI) {
    // Find out what the fix offset of the frame pointer save area.
    int FPOffset = Subtarget.getFrameLowering()->getFramePointerSaveOffset();
    // Allocate the frame index for frame pointer save area.
    FPSI = MF.getFrameInfo().CreateFixedObject(isPPC64? 8 : 4, FPOffset, true);
    // Save the result.
    FI->setFramePointerSaveIndex(FPSI);
  }
  return DAG.getFrameIndex(FPSI, PtrVT);
}

SDValue PPCTargetLowering::LowerDYNAMIC_STACKALLOC(SDValue Op,
                                                   SelectionDAG &DAG) const {
  // Get the inputs.
  SDValue Chain = Op.getOperand(0);
  SDValue Size  = Op.getOperand(1);
  SDLoc dl(Op);

  // Get the correct type for pointers.
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  // Negate the size.
  SDValue NegSize = DAG.getNode(ISD::SUB, dl, PtrVT,
                                DAG.getConstant(0, dl, PtrVT), Size);
  // Construct a node for the frame pointer save index.
  SDValue FPSIdx = getFramePointerFrameIndex(DAG);
  // Build a DYNALLOC node.
  SDValue Ops[3] = { Chain, NegSize, FPSIdx };
  SDVTList VTs = DAG.getVTList(PtrVT, MVT::Other);
  return DAG.getNode(PPCISD::DYNALLOC, dl, VTs, Ops);
}

SDValue PPCTargetLowering::LowerEH_DWARF_CFA(SDValue Op,
                                                     SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();

  bool isPPC64 = Subtarget.isPPC64();
  EVT PtrVT = getPointerTy(DAG.getDataLayout());

  int FI = MF.getFrameInfo().CreateFixedObject(isPPC64 ? 8 : 4, 0, false);
  return DAG.getFrameIndex(FI, PtrVT);
}

SDValue PPCTargetLowering::lowerEH_SJLJ_SETJMP(SDValue Op,
                                               SelectionDAG &DAG) const {
  SDLoc DL(Op);
  return DAG.getNode(PPCISD::EH_SJLJ_SETJMP, DL,
                     DAG.getVTList(MVT::i32, MVT::Other),
                     Op.getOperand(0), Op.getOperand(1));
}

SDValue PPCTargetLowering::lowerEH_SJLJ_LONGJMP(SDValue Op,
                                                SelectionDAG &DAG) const {
  SDLoc DL(Op);
  return DAG.getNode(PPCISD::EH_SJLJ_LONGJMP, DL, MVT::Other,
                     Op.getOperand(0), Op.getOperand(1));
}

SDValue PPCTargetLowering::LowerLOAD(SDValue Op, SelectionDAG &DAG) const {
  if (Op.getValueType().isVector())
    return LowerVectorLoad(Op, DAG);

  assert(Op.getValueType() == MVT::i1 &&
         "Custom lowering only for i1 loads");

  // First, load 8 bits into 32 bits, then truncate to 1 bit.

  SDLoc dl(Op);
  LoadSDNode *LD = cast<LoadSDNode>(Op);

  SDValue Chain = LD->getChain();
  SDValue BasePtr = LD->getBasePtr();
  MachineMemOperand *MMO = LD->getMemOperand();

  SDValue NewLD =
      DAG.getExtLoad(ISD::EXTLOAD, dl, getPointerTy(DAG.getDataLayout()), Chain,
                     BasePtr, MVT::i8, MMO);
  SDValue Result = DAG.getNode(ISD::TRUNCATE, dl, MVT::i1, NewLD);

  SDValue Ops[] = { Result, SDValue(NewLD.getNode(), 1) };
  return DAG.getMergeValues(Ops, dl);
}

SDValue PPCTargetLowering::LowerSTORE(SDValue Op, SelectionDAG &DAG) const {
  if (Op.getOperand(1).getValueType().isVector())
    return LowerVectorStore(Op, DAG);

  assert(Op.getOperand(1).getValueType() == MVT::i1 &&
         "Custom lowering only for i1 stores");

  // First, zero extend to 32 bits, then use a truncating store to 8 bits.

  SDLoc dl(Op);
  StoreSDNode *ST = cast<StoreSDNode>(Op);

  SDValue Chain = ST->getChain();
  SDValue BasePtr = ST->getBasePtr();
  SDValue Value = ST->getValue();
  MachineMemOperand *MMO = ST->getMemOperand();

  Value = DAG.getNode(ISD::ZERO_EXTEND, dl, getPointerTy(DAG.getDataLayout()),
                      Value);
  return DAG.getTruncStore(Chain, dl, Value, BasePtr, MVT::i8, MMO);
}

// FIXME: Remove this once the ANDI glue bug is fixed:
SDValue PPCTargetLowering::LowerTRUNCATE(SDValue Op, SelectionDAG &DAG) const {
  assert(Op.getValueType() == MVT::i1 &&
         "Custom lowering only for i1 results");

  SDLoc DL(Op);
  return DAG.getNode(PPCISD::ANDIo_1_GT_BIT, DL, MVT::i1,
                     Op.getOperand(0));
}

/// LowerSELECT_CC - Lower floating point select_cc's into fsel instruction when
/// possible.
SDValue PPCTargetLowering::LowerSELECT_CC(SDValue Op, SelectionDAG &DAG) const {
  // Not FP? Not a fsel.
  if (!Op.getOperand(0).getValueType().isFloatingPoint() ||
      !Op.getOperand(2).getValueType().isFloatingPoint())
    return Op;

  // We might be able to do better than this under some circumstances, but in
  // general, fsel-based lowering of select is a finite-math-only optimization.
  // For more information, see section F.3 of the 2.06 ISA specification.
  if (!DAG.getTarget().Options.NoInfsFPMath ||
      !DAG.getTarget().Options.NoNaNsFPMath)
    return Op;
  // TODO: Propagate flags from the select rather than global settings.
  SDNodeFlags Flags;
  Flags.setNoInfs(true);
  Flags.setNoNaNs(true);

  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(4))->get();

  EVT ResVT = Op.getValueType();
  EVT CmpVT = Op.getOperand(0).getValueType();
  SDValue LHS = Op.getOperand(0), RHS = Op.getOperand(1);
  SDValue TV  = Op.getOperand(2), FV  = Op.getOperand(3);
  SDLoc dl(Op);

  // If the RHS of the comparison is a 0.0, we don't need to do the
  // subtraction at all.
  SDValue Sel1;
  if (isFloatingPointZero(RHS))
    switch (CC) {
    default: break;       // SETUO etc aren't handled by fsel.
    case ISD::SETNE:
      std::swap(TV, FV);
      LLVM_FALLTHROUGH;
    case ISD::SETEQ:
      if (LHS.getValueType() == MVT::f32)   // Comparison is always 64-bits
        LHS = DAG.getNode(ISD::FP_EXTEND, dl, MVT::f64, LHS);
      Sel1 = DAG.getNode(PPCISD::FSEL, dl, ResVT, LHS, TV, FV);
      if (Sel1.getValueType() == MVT::f32)   // Comparison is always 64-bits
        Sel1 = DAG.getNode(ISD::FP_EXTEND, dl, MVT::f64, Sel1);
      return DAG.getNode(PPCISD::FSEL, dl, ResVT,
                         DAG.getNode(ISD::FNEG, dl, MVT::f64, LHS), Sel1, FV);
    case ISD::SETULT:
    case ISD::SETLT:
      std::swap(TV, FV);  // fsel is natively setge, swap operands for setlt
      LLVM_FALLTHROUGH;
    case ISD::SETOGE:
    case ISD::SETGE:
      if (LHS.getValueType() == MVT::f32)   // Comparison is always 64-bits
        LHS = DAG.getNode(ISD::FP_EXTEND, dl, MVT::f64, LHS);
      return DAG.getNode(PPCISD::FSEL, dl, ResVT, LHS, TV, FV);
    case ISD::SETUGT:
    case ISD::SETGT:
      std::swap(TV, FV);  // fsel is natively setge, swap operands for setlt
      LLVM_FALLTHROUGH;
    case ISD::SETOLE:
    case ISD::SETLE:
      if (LHS.getValueType() == MVT::f32)   // Comparison is always 64-bits
        LHS = DAG.getNode(ISD::FP_EXTEND, dl, MVT::f64, LHS);
      return DAG.getNode(PPCISD::FSEL, dl, ResVT,
                         DAG.getNode(ISD::FNEG, dl, MVT::f64, LHS), TV, FV);
    }

  SDValue Cmp;
  switch (CC) {
  default: break;       // SETUO etc aren't handled by fsel.
  case ISD::SETNE:
    std::swap(TV, FV);
    LLVM_FALLTHROUGH;
  case ISD::SETEQ:
    Cmp = DAG.getNode(ISD::FSUB, dl, CmpVT, LHS, RHS, Flags);
    if (Cmp.getValueType() == MVT::f32)   // Comparison is always 64-bits
      Cmp = DAG.getNode(ISD::FP_EXTEND, dl, MVT::f64, Cmp);
    Sel1 = DAG.getNode(PPCISD::FSEL, dl, ResVT, Cmp, TV, FV);
    if (Sel1.getValueType() == MVT::f32)   // Comparison is always 64-bits
      Sel1 = DAG.getNode(ISD::FP_EXTEND, dl, MVT::f64, Sel1);
    return DAG.getNode(PPCISD::FSEL, dl, ResVT,
                       DAG.getNode(ISD::FNEG, dl, MVT::f64, Cmp), Sel1, FV);
  case ISD::SETULT:
  case ISD::SETLT:
    Cmp = DAG.getNode(ISD::FSUB, dl, CmpVT, LHS, RHS, Flags);
    if (Cmp.getValueType() == MVT::f32)   // Comparison is always 64-bits
      Cmp = DAG.getNode(ISD::FP_EXTEND, dl, MVT::f64, Cmp);
    return DAG.getNode(PPCISD::FSEL, dl, ResVT, Cmp, FV, TV);
  case ISD::SETOGE:
  case ISD::SETGE:
    Cmp = DAG.getNode(ISD::FSUB, dl, CmpVT, LHS, RHS, Flags);
    if (Cmp.getValueType() == MVT::f32)   // Comparison is always 64-bits
      Cmp = DAG.getNode(ISD::FP_EXTEND, dl, MVT::f64, Cmp);
    return DAG.getNode(PPCISD::FSEL, dl, ResVT, Cmp, TV, FV);
  case ISD::SETUGT:
  case ISD::SETGT:
    Cmp = DAG.getNode(ISD::FSUB, dl, CmpVT, RHS, LHS, Flags);
    if (Cmp.getValueType() == MVT::f32)   // Comparison is always 64-bits
      Cmp = DAG.getNode(ISD::FP_EXTEND, dl, MVT::f64, Cmp);
    return DAG.getNode(PPCISD::FSEL, dl, ResVT, Cmp, FV, TV);
  case ISD::SETOLE:
  case ISD::SETLE:
    Cmp = DAG.getNode(ISD::FSUB, dl, CmpVT, RHS, LHS, Flags);
    if (Cmp.getValueType() == MVT::f32)   // Comparison is always 64-bits
      Cmp = DAG.getNode(ISD::FP_EXTEND, dl, MVT::f64, Cmp);
    return DAG.getNode(PPCISD::FSEL, dl, ResVT, Cmp, TV, FV);
  }
  return Op;
}

void PPCTargetLowering::LowerFP_TO_INTForReuse(SDValue Op, ReuseLoadInfo &RLI,
                                               SelectionDAG &DAG,
                                               const SDLoc &dl) const {
  assert(Op.getOperand(0).getValueType().isFloatingPoint());
  SDValue Src = Op.getOperand(0);
  if (Src.getValueType() == MVT::f32)
    Src = DAG.getNode(ISD::FP_EXTEND, dl, MVT::f64, Src);

  SDValue Tmp;
  switch (Op.getSimpleValueType().SimpleTy) {
  default: llvm_unreachable("Unhandled FP_TO_INT type in custom expander!");
  case MVT::i32:
    Tmp = DAG.getNode(
        Op.getOpcode() == ISD::FP_TO_SINT
            ? PPCISD::FCTIWZ
            : (Subtarget.hasFPCVT() ? PPCISD::FCTIWUZ : PPCISD::FCTIDZ),
        dl, MVT::f64, Src);
    break;
  case MVT::i64:
    assert((Op.getOpcode() == ISD::FP_TO_SINT || Subtarget.hasFPCVT()) &&
           "i64 FP_TO_UINT is supported only with FPCVT");
    Tmp = DAG.getNode(Op.getOpcode()==ISD::FP_TO_SINT ? PPCISD::FCTIDZ :
                                                        PPCISD::FCTIDUZ,
                      dl, MVT::f64, Src);
    break;
  }

  // Convert the FP value to an int value through memory.
  bool i32Stack = Op.getValueType() == MVT::i32 && Subtarget.hasSTFIWX() &&
    (Op.getOpcode() == ISD::FP_TO_SINT || Subtarget.hasFPCVT());
  SDValue FIPtr = DAG.CreateStackTemporary(i32Stack ? MVT::i32 : MVT::f64);
  int FI = cast<FrameIndexSDNode>(FIPtr)->getIndex();
  MachinePointerInfo MPI =
      MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FI);

  // Emit a store to the stack slot.
  SDValue Chain;
  if (i32Stack) {
    MachineFunction &MF = DAG.getMachineFunction();
    MachineMemOperand *MMO =
      MF.getMachineMemOperand(MPI, MachineMemOperand::MOStore, 4, 4);
    SDValue Ops[] = { DAG.getEntryNode(), Tmp, FIPtr };
    Chain = DAG.getMemIntrinsicNode(PPCISD::STFIWX, dl,
              DAG.getVTList(MVT::Other), Ops, MVT::i32, MMO);
  } else
    Chain = DAG.getStore(DAG.getEntryNode(), dl, Tmp, FIPtr, MPI);

  // Result is a load from the stack slot.  If loading 4 bytes, make sure to
  // add in a bias on big endian.
  if (Op.getValueType() == MVT::i32 && !i32Stack) {
    FIPtr = DAG.getNode(ISD::ADD, dl, FIPtr.getValueType(), FIPtr,
                        DAG.getConstant(4, dl, FIPtr.getValueType()));
    MPI = MPI.getWithOffset(Subtarget.isLittleEndian() ? 0 : 4);
  }

  RLI.Chain = Chain;
  RLI.Ptr = FIPtr;
  RLI.MPI = MPI;
}

/// Custom lowers floating point to integer conversions to use
/// the direct move instructions available in ISA 2.07 to avoid the
/// need for load/store combinations.
SDValue PPCTargetLowering::LowerFP_TO_INTDirectMove(SDValue Op,
                                                    SelectionDAG &DAG,
                                                    const SDLoc &dl) const {
  assert(Op.getOperand(0).getValueType().isFloatingPoint());
  SDValue Src = Op.getOperand(0);

  if (Src.getValueType() == MVT::f32)
    Src = DAG.getNode(ISD::FP_EXTEND, dl, MVT::f64, Src);

  SDValue Tmp;
  switch (Op.getSimpleValueType().SimpleTy) {
  default: llvm_unreachable("Unhandled FP_TO_INT type in custom expander!");
  case MVT::i32:
    Tmp = DAG.getNode(
        Op.getOpcode() == ISD::FP_TO_SINT
            ? PPCISD::FCTIWZ
            : (Subtarget.hasFPCVT() ? PPCISD::FCTIWUZ : PPCISD::FCTIDZ),
        dl, MVT::f64, Src);
    Tmp = DAG.getNode(PPCISD::MFVSR, dl, MVT::i32, Tmp);
    break;
  case MVT::i64:
    assert((Op.getOpcode() == ISD::FP_TO_SINT || Subtarget.hasFPCVT()) &&
           "i64 FP_TO_UINT is supported only with FPCVT");
    Tmp = DAG.getNode(Op.getOpcode()==ISD::FP_TO_SINT ? PPCISD::FCTIDZ :
                                                        PPCISD::FCTIDUZ,
                      dl, MVT::f64, Src);
    Tmp = DAG.getNode(PPCISD::MFVSR, dl, MVT::i64, Tmp);
    break;
  }
  return Tmp;
}

SDValue PPCTargetLowering::LowerFP_TO_INT(SDValue Op, SelectionDAG &DAG,
                                          const SDLoc &dl) const {

  // FP to INT conversions are legal for f128.
  if (EnableQuadPrecision && (Op->getOperand(0).getValueType() == MVT::f128))
    return Op;

  // Expand ppcf128 to i32 by hand for the benefit of llvm-gcc bootstrap on
  // PPC (the libcall is not available).
  if (Op.getOperand(0).getValueType() == MVT::ppcf128) {
    if (Op.getValueType() == MVT::i32) {
      if (Op.getOpcode() == ISD::FP_TO_SINT) {
        SDValue Lo = DAG.getNode(ISD::EXTRACT_ELEMENT, dl,
                                 MVT::f64, Op.getOperand(0),
                                 DAG.getIntPtrConstant(0, dl));
        SDValue Hi = DAG.getNode(ISD::EXTRACT_ELEMENT, dl,
                                 MVT::f64, Op.getOperand(0),
                                 DAG.getIntPtrConstant(1, dl));

        // Add the two halves of the long double in round-to-zero mode.
        SDValue Res = DAG.getNode(PPCISD::FADDRTZ, dl, MVT::f64, Lo, Hi);

        // Now use a smaller FP_TO_SINT.
        return DAG.getNode(ISD::FP_TO_SINT, dl, MVT::i32, Res);
      }
      if (Op.getOpcode() == ISD::FP_TO_UINT) {
        const uint64_t TwoE31[] = {0x41e0000000000000LL, 0};
        APFloat APF = APFloat(APFloat::PPCDoubleDouble(), APInt(128, TwoE31));
        SDValue Tmp = DAG.getConstantFP(APF, dl, MVT::ppcf128);
        //  X>=2^31 ? (int)(X-2^31)+0x80000000 : (int)X
        // FIXME: generated code sucks.
        // TODO: Are there fast-math-flags to propagate to this FSUB?
        SDValue True = DAG.getNode(ISD::FSUB, dl, MVT::ppcf128,
                                   Op.getOperand(0), Tmp);
        True = DAG.getNode(ISD::FP_TO_SINT, dl, MVT::i32, True);
        True = DAG.getNode(ISD::ADD, dl, MVT::i32, True,
                           DAG.getConstant(0x80000000, dl, MVT::i32));
        SDValue False = DAG.getNode(ISD::FP_TO_SINT, dl, MVT::i32,
                                    Op.getOperand(0));
        return DAG.getSelectCC(dl, Op.getOperand(0), Tmp, True, False,
                               ISD::SETGE);
      }
    }

    return SDValue();
  }

  if (Subtarget.hasDirectMove() && Subtarget.isPPC64())
    return LowerFP_TO_INTDirectMove(Op, DAG, dl);

  ReuseLoadInfo RLI;
  LowerFP_TO_INTForReuse(Op, RLI, DAG, dl);

  return DAG.getLoad(Op.getValueType(), dl, RLI.Chain, RLI.Ptr, RLI.MPI,
                     RLI.Alignment, RLI.MMOFlags(), RLI.AAInfo, RLI.Ranges);
}

// We're trying to insert a regular store, S, and then a load, L. If the
// incoming value, O, is a load, we might just be able to have our load use the
// address used by O. However, we don't know if anything else will store to
// that address before we can load from it. To prevent this situation, we need
// to insert our load, L, into the chain as a peer of O. To do this, we give L
// the same chain operand as O, we create a token factor from the chain results
// of O and L, and we replace all uses of O's chain result with that token
// factor (see spliceIntoChain below for this last part).
bool PPCTargetLowering::canReuseLoadAddress(SDValue Op, EVT MemVT,
                                            ReuseLoadInfo &RLI,
                                            SelectionDAG &DAG,
                                            ISD::LoadExtType ET) const {
  SDLoc dl(Op);
  if (ET == ISD::NON_EXTLOAD &&
      (Op.getOpcode() == ISD::FP_TO_UINT ||
       Op.getOpcode() == ISD::FP_TO_SINT) &&
      isOperationLegalOrCustom(Op.getOpcode(),
                               Op.getOperand(0).getValueType())) {

    LowerFP_TO_INTForReuse(Op, RLI, DAG, dl);
    return true;
  }

  LoadSDNode *LD = dyn_cast<LoadSDNode>(Op);
  if (!LD || LD->getExtensionType() != ET || LD->isVolatile() ||
      LD->isNonTemporal())
    return false;
  if (LD->getMemoryVT() != MemVT)
    return false;

  RLI.Ptr = LD->getBasePtr();
  if (LD->isIndexed() && !LD->getOffset().isUndef()) {
    assert(LD->getAddressingMode() == ISD::PRE_INC &&
           "Non-pre-inc AM on PPC?");
    RLI.Ptr = DAG.getNode(ISD::ADD, dl, RLI.Ptr.getValueType(), RLI.Ptr,
                          LD->getOffset());
  }

  RLI.Chain = LD->getChain();
  RLI.MPI = LD->getPointerInfo();
  RLI.IsDereferenceable = LD->isDereferenceable();
  RLI.IsInvariant = LD->isInvariant();
  RLI.Alignment = LD->getAlignment();
  RLI.AAInfo = LD->getAAInfo();
  RLI.Ranges = LD->getRanges();

  RLI.ResChain = SDValue(LD, LD->isIndexed() ? 2 : 1);
  return true;
}

// Given the head of the old chain, ResChain, insert a token factor containing
// it and NewResChain, and make users of ResChain now be users of that token
// factor.
// TODO: Remove and use DAG::makeEquivalentMemoryOrdering() instead.
void PPCTargetLowering::spliceIntoChain(SDValue ResChain,
                                        SDValue NewResChain,
                                        SelectionDAG &DAG) const {
  if (!ResChain)
    return;

  SDLoc dl(NewResChain);

  SDValue TF = DAG.getNode(ISD::TokenFactor, dl, MVT::Other,
                           NewResChain, DAG.getUNDEF(MVT::Other));
  assert(TF.getNode() != NewResChain.getNode() &&
         "A new TF really is required here");

  DAG.ReplaceAllUsesOfValueWith(ResChain, TF);
  DAG.UpdateNodeOperands(TF.getNode(), ResChain, NewResChain);
}

/// Analyze profitability of direct move
/// prefer float load to int load plus direct move
/// when there is no integer use of int load
bool PPCTargetLowering::directMoveIsProfitable(const SDValue &Op) const {
  SDNode *Origin = Op.getOperand(0).getNode();
  if (Origin->getOpcode() != ISD::LOAD)
    return true;

  // If there is no LXSIBZX/LXSIHZX, like Power8,
  // prefer direct move if the memory size is 1 or 2 bytes.
  MachineMemOperand *MMO = cast<LoadSDNode>(Origin)->getMemOperand();
  if (!Subtarget.hasP9Vector() && MMO->getSize() <= 2)
    return true;

  for (SDNode::use_iterator UI = Origin->use_begin(),
                            UE = Origin->use_end();
       UI != UE; ++UI) {

    // Only look at the users of the loaded value.
    if (UI.getUse().get().getResNo() != 0)
      continue;

    if (UI->getOpcode() != ISD::SINT_TO_FP &&
        UI->getOpcode() != ISD::UINT_TO_FP)
      return true;
  }

  return false;
}

/// Custom lowers integer to floating point conversions to use
/// the direct move instructions available in ISA 2.07 to avoid the
/// need for load/store combinations.
SDValue PPCTargetLowering::LowerINT_TO_FPDirectMove(SDValue Op,
                                                    SelectionDAG &DAG,
                                                    const SDLoc &dl) const {
  assert((Op.getValueType() == MVT::f32 ||
          Op.getValueType() == MVT::f64) &&
         "Invalid floating point type as target of conversion");
  assert(Subtarget.hasFPCVT() &&
         "Int to FP conversions with direct moves require FPCVT");
  SDValue FP;
  SDValue Src = Op.getOperand(0);
  bool SinglePrec = Op.getValueType() == MVT::f32;
  bool WordInt = Src.getSimpleValueType().SimpleTy == MVT::i32;
  bool Signed = Op.getOpcode() == ISD::SINT_TO_FP;
  unsigned ConvOp = Signed ? (SinglePrec ? PPCISD::FCFIDS : PPCISD::FCFID) :
                             (SinglePrec ? PPCISD::FCFIDUS : PPCISD::FCFIDU);

  if (WordInt) {
    FP = DAG.getNode(Signed ? PPCISD::MTVSRA : PPCISD::MTVSRZ,
                     dl, MVT::f64, Src);
    FP = DAG.getNode(ConvOp, dl, SinglePrec ? MVT::f32 : MVT::f64, FP);
  }
  else {
    FP = DAG.getNode(PPCISD::MTVSRA, dl, MVT::f64, Src);
    FP = DAG.getNode(ConvOp, dl, SinglePrec ? MVT::f32 : MVT::f64, FP);
  }

  return FP;
}

static SDValue widenVec(SelectionDAG &DAG, SDValue Vec, const SDLoc &dl) {

  EVT VecVT = Vec.getValueType();
  assert(VecVT.isVector() && "Expected a vector type.");
  assert(VecVT.getSizeInBits() < 128 && "Vector is already full width.");

  EVT EltVT = VecVT.getVectorElementType();
  unsigned WideNumElts = 128 / EltVT.getSizeInBits();
  EVT WideVT = EVT::getVectorVT(*DAG.getContext(), EltVT, WideNumElts);

  unsigned NumConcat = WideNumElts / VecVT.getVectorNumElements();
  SmallVector<SDValue, 16> Ops(NumConcat);
  Ops[0] = Vec;
  SDValue UndefVec = DAG.getUNDEF(VecVT);
  for (unsigned i = 1; i < NumConcat; ++i)
    Ops[i] = UndefVec;

  return DAG.getNode(ISD::CONCAT_VECTORS, dl, WideVT, Ops);
}

SDValue PPCTargetLowering::LowerINT_TO_FPVector(SDValue Op, SelectionDAG &DAG,
                                                const SDLoc &dl) const {

  unsigned Opc = Op.getOpcode();
  assert((Opc == ISD::UINT_TO_FP || Opc == ISD::SINT_TO_FP) &&
         "Unexpected conversion type");
  assert((Op.getValueType() == MVT::v2f64 || Op.getValueType() == MVT::v4f32) &&
         "Supports conversions to v2f64/v4f32 only.");

  bool SignedConv = Opc == ISD::SINT_TO_FP;
  bool FourEltRes = Op.getValueType() == MVT::v4f32;

  SDValue Wide = widenVec(DAG, Op.getOperand(0), dl);
  EVT WideVT = Wide.getValueType();
  unsigned WideNumElts = WideVT.getVectorNumElements();
  MVT IntermediateVT = FourEltRes ? MVT::v4i32 : MVT::v2i64;

  SmallVector<int, 16> ShuffV;
  for (unsigned i = 0; i < WideNumElts; ++i)
    ShuffV.push_back(i + WideNumElts);

  int Stride = FourEltRes ? WideNumElts / 4 : WideNumElts / 2;
  int SaveElts = FourEltRes ? 4 : 2;
  if (Subtarget.isLittleEndian())
    for (int i = 0; i < SaveElts; i++)
      ShuffV[i * Stride] = i;
  else
    for (int i = 1; i <= SaveElts; i++)
      ShuffV[i * Stride - 1] = i - 1;

  SDValue ShuffleSrc2 =
      SignedConv ? DAG.getUNDEF(WideVT) : DAG.getConstant(0, dl, WideVT);
  SDValue Arrange = DAG.getVectorShuffle(WideVT, dl, Wide, ShuffleSrc2, ShuffV);
  unsigned ExtendOp =
      SignedConv ? (unsigned)PPCISD::SExtVElems : (unsigned)ISD::BITCAST;

  SDValue Extend;
  if (!Subtarget.hasP9Altivec() && SignedConv) {
    Arrange = DAG.getBitcast(IntermediateVT, Arrange);
    Extend = DAG.getNode(ISD::SIGN_EXTEND_INREG, dl, IntermediateVT, Arrange,
                         DAG.getValueType(Op.getOperand(0).getValueType()));
  } else
    Extend = DAG.getNode(ExtendOp, dl, IntermediateVT, Arrange);

  return DAG.getNode(Opc, dl, Op.getValueType(), Extend);
}

SDValue PPCTargetLowering::LowerINT_TO_FP(SDValue Op,
                                          SelectionDAG &DAG) const {
  SDLoc dl(Op);

  EVT InVT = Op.getOperand(0).getValueType();
  EVT OutVT = Op.getValueType();
  if (OutVT.isVector() && OutVT.isFloatingPoint() &&
      isOperationCustom(Op.getOpcode(), InVT))
    return LowerINT_TO_FPVector(Op, DAG, dl);

  // Conversions to f128 are legal.
  if (EnableQuadPrecision && (Op.getValueType() == MVT::f128))
    return Op;

  if (Subtarget.hasQPX() && Op.getOperand(0).getValueType() == MVT::v4i1) {
    if (Op.getValueType() != MVT::v4f32 && Op.getValueType() != MVT::v4f64)
      return SDValue();

    SDValue Value = Op.getOperand(0);
    // The values are now known to be -1 (false) or 1 (true). To convert this
    // into 0 (false) and 1 (true), add 1 and then divide by 2 (multiply by 0.5).
    // This can be done with an fma and the 0.5 constant: (V+1.0)*0.5 = 0.5*V+0.5
    Value = DAG.getNode(PPCISD::QBFLT, dl, MVT::v4f64, Value);

    SDValue FPHalfs = DAG.getConstantFP(0.5, dl, MVT::v4f64);

    Value = DAG.getNode(ISD::FMA, dl, MVT::v4f64, Value, FPHalfs, FPHalfs);

    if (Op.getValueType() != MVT::v4f64)
      Value = DAG.getNode(ISD::FP_ROUND, dl,
                          Op.getValueType(), Value,
                          DAG.getIntPtrConstant(1, dl));
    return Value;
  }

  // Don't handle ppc_fp128 here; let it be lowered to a libcall.
  if (Op.getValueType() != MVT::f32 && Op.getValueType() != MVT::f64)
    return SDValue();

  if (Op.getOperand(0).getValueType() == MVT::i1)
    return DAG.getNode(ISD::SELECT, dl, Op.getValueType(), Op.getOperand(0),
                       DAG.getConstantFP(1.0, dl, Op.getValueType()),
                       DAG.getConstantFP(0.0, dl, Op.getValueType()));

  // If we have direct moves, we can do all the conversion, skip the store/load
  // however, without FPCVT we can't do most conversions.
  if (Subtarget.hasDirectMove() && directMoveIsProfitable(Op) &&
      Subtarget.isPPC64() && Subtarget.hasFPCVT())
    return LowerINT_TO_FPDirectMove(Op, DAG, dl);

  assert((Op.getOpcode() == ISD::SINT_TO_FP || Subtarget.hasFPCVT()) &&
         "UINT_TO_FP is supported only with FPCVT");

  // If we have FCFIDS, then use it when converting to single-precision.
  // Otherwise, convert to double-precision and then round.
  unsigned FCFOp = (Subtarget.hasFPCVT() && Op.getValueType() == MVT::f32)
                       ? (Op.getOpcode() == ISD::UINT_TO_FP ? PPCISD::FCFIDUS
                                                            : PPCISD::FCFIDS)
                       : (Op.getOpcode() == ISD::UINT_TO_FP ? PPCISD::FCFIDU
                                                            : PPCISD::FCFID);
  MVT FCFTy = (Subtarget.hasFPCVT() && Op.getValueType() == MVT::f32)
                  ? MVT::f32
                  : MVT::f64;

  if (Op.getOperand(0).getValueType() == MVT::i64) {
    SDValue SINT = Op.getOperand(0);
    // When converting to single-precision, we actually need to convert
    // to double-precision first and then round to single-precision.
    // To avoid double-rounding effects during that operation, we have
    // to prepare the input operand.  Bits that might be truncated when
    // converting to double-precision are replaced by a bit that won't
    // be lost at this stage, but is below the single-precision rounding
    // position.
    //
    // However, if -enable-unsafe-fp-math is in effect, accept double
    // rounding to avoid the extra overhead.
    if (Op.getValueType() == MVT::f32 &&
        !Subtarget.hasFPCVT() &&
        !DAG.getTarget().Options.UnsafeFPMath) {

      // Twiddle input to make sure the low 11 bits are zero.  (If this
      // is the case, we are guaranteed the value will fit into the 53 bit
      // mantissa of an IEEE double-precision value without rounding.)
      // If any of those low 11 bits were not zero originally, make sure
      // bit 12 (value 2048) is set instead, so that the final rounding
      // to single-precision gets the correct result.
      SDValue Round = DAG.getNode(ISD::AND, dl, MVT::i64,
                                  SINT, DAG.getConstant(2047, dl, MVT::i64));
      Round = DAG.getNode(ISD::ADD, dl, MVT::i64,
                          Round, DAG.getConstant(2047, dl, MVT::i64));
      Round = DAG.getNode(ISD::OR, dl, MVT::i64, Round, SINT);
      Round = DAG.getNode(ISD::AND, dl, MVT::i64,
                          Round, DAG.getConstant(-2048, dl, MVT::i64));

      // However, we cannot use that value unconditionally: if the magnitude
      // of the input value is small, the bit-twiddling we did above might
      // end up visibly changing the output.  Fortunately, in that case, we
      // don't need to twiddle bits since the original input will convert
      // exactly to double-precision floating-point already.  Therefore,
      // construct a conditional to use the original value if the top 11
      // bits are all sign-bit copies, and use the rounded value computed
      // above otherwise.
      SDValue Cond = DAG.getNode(ISD::SRA, dl, MVT::i64,
                                 SINT, DAG.getConstant(53, dl, MVT::i32));
      Cond = DAG.getNode(ISD::ADD, dl, MVT::i64,
                         Cond, DAG.getConstant(1, dl, MVT::i64));
      Cond = DAG.getSetCC(dl, MVT::i32,
                          Cond, DAG.getConstant(1, dl, MVT::i64), ISD::SETUGT);

      SINT = DAG.getNode(ISD::SELECT, dl, MVT::i64, Cond, Round, SINT);
    }

    ReuseLoadInfo RLI;
    SDValue Bits;

    MachineFunction &MF = DAG.getMachineFunction();
    if (canReuseLoadAddress(SINT, MVT::i64, RLI, DAG)) {
      Bits = DAG.getLoad(MVT::f64, dl, RLI.Chain, RLI.Ptr, RLI.MPI,
                         RLI.Alignment, RLI.MMOFlags(), RLI.AAInfo, RLI.Ranges);
      spliceIntoChain(RLI.ResChain, Bits.getValue(1), DAG);
    } else if (Subtarget.hasLFIWAX() &&
               canReuseLoadAddress(SINT, MVT::i32, RLI, DAG, ISD::SEXTLOAD)) {
      MachineMemOperand *MMO =
        MF.getMachineMemOperand(RLI.MPI, MachineMemOperand::MOLoad, 4,
                                RLI.Alignment, RLI.AAInfo, RLI.Ranges);
      SDValue Ops[] = { RLI.Chain, RLI.Ptr };
      Bits = DAG.getMemIntrinsicNode(PPCISD::LFIWAX, dl,
                                     DAG.getVTList(MVT::f64, MVT::Other),
                                     Ops, MVT::i32, MMO);
      spliceIntoChain(RLI.ResChain, Bits.getValue(1), DAG);
    } else if (Subtarget.hasFPCVT() &&
               canReuseLoadAddress(SINT, MVT::i32, RLI, DAG, ISD::ZEXTLOAD)) {
      MachineMemOperand *MMO =
        MF.getMachineMemOperand(RLI.MPI, MachineMemOperand::MOLoad, 4,
                                RLI.Alignment, RLI.AAInfo, RLI.Ranges);
      SDValue Ops[] = { RLI.Chain, RLI.Ptr };
      Bits = DAG.getMemIntrinsicNode(PPCISD::LFIWZX, dl,
                                     DAG.getVTList(MVT::f64, MVT::Other),
                                     Ops, MVT::i32, MMO);
      spliceIntoChain(RLI.ResChain, Bits.getValue(1), DAG);
    } else if (((Subtarget.hasLFIWAX() &&
                 SINT.getOpcode() == ISD::SIGN_EXTEND) ||
                (Subtarget.hasFPCVT() &&
                 SINT.getOpcode() == ISD::ZERO_EXTEND)) &&
               SINT.getOperand(0).getValueType() == MVT::i32) {
      MachineFrameInfo &MFI = MF.getFrameInfo();
      EVT PtrVT = getPointerTy(DAG.getDataLayout());

      int FrameIdx = MFI.CreateStackObject(4, 4, false);
      SDValue FIdx = DAG.getFrameIndex(FrameIdx, PtrVT);

      SDValue Store =
          DAG.getStore(DAG.getEntryNode(), dl, SINT.getOperand(0), FIdx,
                       MachinePointerInfo::getFixedStack(
                           DAG.getMachineFunction(), FrameIdx));

      assert(cast<StoreSDNode>(Store)->getMemoryVT() == MVT::i32 &&
             "Expected an i32 store");

      RLI.Ptr = FIdx;
      RLI.Chain = Store;
      RLI.MPI =
          MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FrameIdx);
      RLI.Alignment = 4;

      MachineMemOperand *MMO =
        MF.getMachineMemOperand(RLI.MPI, MachineMemOperand::MOLoad, 4,
                                RLI.Alignment, RLI.AAInfo, RLI.Ranges);
      SDValue Ops[] = { RLI.Chain, RLI.Ptr };
      Bits = DAG.getMemIntrinsicNode(SINT.getOpcode() == ISD::ZERO_EXTEND ?
                                     PPCISD::LFIWZX : PPCISD::LFIWAX,
                                     dl, DAG.getVTList(MVT::f64, MVT::Other),
                                     Ops, MVT::i32, MMO);
    } else
      Bits = DAG.getNode(ISD::BITCAST, dl, MVT::f64, SINT);

    SDValue FP = DAG.getNode(FCFOp, dl, FCFTy, Bits);

    if (Op.getValueType() == MVT::f32 && !Subtarget.hasFPCVT())
      FP = DAG.getNode(ISD::FP_ROUND, dl,
                       MVT::f32, FP, DAG.getIntPtrConstant(0, dl));
    return FP;
  }

  assert(Op.getOperand(0).getValueType() == MVT::i32 &&
         "Unhandled INT_TO_FP type in custom expander!");
  // Since we only generate this in 64-bit mode, we can take advantage of
  // 64-bit registers.  In particular, sign extend the input value into the
  // 64-bit register with extsw, store the WHOLE 64-bit value into the stack
  // then lfd it and fcfid it.
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  EVT PtrVT = getPointerTy(MF.getDataLayout());

  SDValue Ld;
  if (Subtarget.hasLFIWAX() || Subtarget.hasFPCVT()) {
    ReuseLoadInfo RLI;
    bool ReusingLoad;
    if (!(ReusingLoad = canReuseLoadAddress(Op.getOperand(0), MVT::i32, RLI,
                                            DAG))) {
      int FrameIdx = MFI.CreateStackObject(4, 4, false);
      SDValue FIdx = DAG.getFrameIndex(FrameIdx, PtrVT);

      SDValue Store =
          DAG.getStore(DAG.getEntryNode(), dl, Op.getOperand(0), FIdx,
                       MachinePointerInfo::getFixedStack(
                           DAG.getMachineFunction(), FrameIdx));

      assert(cast<StoreSDNode>(Store)->getMemoryVT() == MVT::i32 &&
             "Expected an i32 store");

      RLI.Ptr = FIdx;
      RLI.Chain = Store;
      RLI.MPI =
          MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FrameIdx);
      RLI.Alignment = 4;
    }

    MachineMemOperand *MMO =
      MF.getMachineMemOperand(RLI.MPI, MachineMemOperand::MOLoad, 4,
                              RLI.Alignment, RLI.AAInfo, RLI.Ranges);
    SDValue Ops[] = { RLI.Chain, RLI.Ptr };
    Ld = DAG.getMemIntrinsicNode(Op.getOpcode() == ISD::UINT_TO_FP ?
                                   PPCISD::LFIWZX : PPCISD::LFIWAX,
                                 dl, DAG.getVTList(MVT::f64, MVT::Other),
                                 Ops, MVT::i32, MMO);
    if (ReusingLoad)
      spliceIntoChain(RLI.ResChain, Ld.getValue(1), DAG);
  } else {
    assert(Subtarget.isPPC64() &&
           "i32->FP without LFIWAX supported only on PPC64");

    int FrameIdx = MFI.CreateStackObject(8, 8, false);
    SDValue FIdx = DAG.getFrameIndex(FrameIdx, PtrVT);

    SDValue Ext64 = DAG.getNode(ISD::SIGN_EXTEND, dl, MVT::i64,
                                Op.getOperand(0));

    // STD the extended value into the stack slot.
    SDValue Store = DAG.getStore(
        DAG.getEntryNode(), dl, Ext64, FIdx,
        MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FrameIdx));

    // Load the value as a double.
    Ld = DAG.getLoad(
        MVT::f64, dl, Store, FIdx,
        MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FrameIdx));
  }

  // FCFID it and return it.
  SDValue FP = DAG.getNode(FCFOp, dl, FCFTy, Ld);
  if (Op.getValueType() == MVT::f32 && !Subtarget.hasFPCVT())
    FP = DAG.getNode(ISD::FP_ROUND, dl, MVT::f32, FP,
                     DAG.getIntPtrConstant(0, dl));
  return FP;
}

SDValue PPCTargetLowering::LowerFLT_ROUNDS_(SDValue Op,
                                            SelectionDAG &DAG) const {
  SDLoc dl(Op);
  /*
   The rounding mode is in bits 30:31 of FPSR, and has the following
   settings:
     00 Round to nearest
     01 Round to 0
     10 Round to +inf
     11 Round to -inf

  FLT_ROUNDS, on the other hand, expects the following:
    -1 Undefined
     0 Round to 0
     1 Round to nearest
     2 Round to +inf
     3 Round to -inf

  To perform the conversion, we do:
    ((FPSCR & 0x3) ^ ((~FPSCR & 0x3) >> 1))
  */

  MachineFunction &MF = DAG.getMachineFunction();
  EVT VT = Op.getValueType();
  EVT PtrVT = getPointerTy(MF.getDataLayout());

  // Save FP Control Word to register
  EVT NodeTys[] = {
    MVT::f64,    // return register
    MVT::Glue    // unused in this context
  };
  SDValue Chain = DAG.getNode(PPCISD::MFFS, dl, NodeTys, None);

  // Save FP register to stack slot
  int SSFI = MF.getFrameInfo().CreateStackObject(8, 8, false);
  SDValue StackSlot = DAG.getFrameIndex(SSFI, PtrVT);
  SDValue Store = DAG.getStore(DAG.getEntryNode(), dl, Chain, StackSlot,
                               MachinePointerInfo());

  // Load FP Control Word from low 32 bits of stack slot.
  SDValue Four = DAG.getConstant(4, dl, PtrVT);
  SDValue Addr = DAG.getNode(ISD::ADD, dl, PtrVT, StackSlot, Four);
  SDValue CWD = DAG.getLoad(MVT::i32, dl, Store, Addr, MachinePointerInfo());

  // Transform as necessary
  SDValue CWD1 =
    DAG.getNode(ISD::AND, dl, MVT::i32,
                CWD, DAG.getConstant(3, dl, MVT::i32));
  SDValue CWD2 =
    DAG.getNode(ISD::SRL, dl, MVT::i32,
                DAG.getNode(ISD::AND, dl, MVT::i32,
                            DAG.getNode(ISD::XOR, dl, MVT::i32,
                                        CWD, DAG.getConstant(3, dl, MVT::i32)),
                            DAG.getConstant(3, dl, MVT::i32)),
                DAG.getConstant(1, dl, MVT::i32));

  SDValue RetVal =
    DAG.getNode(ISD::XOR, dl, MVT::i32, CWD1, CWD2);

  return DAG.getNode((VT.getSizeInBits() < 16 ?
                      ISD::TRUNCATE : ISD::ZERO_EXTEND), dl, VT, RetVal);
}

SDValue PPCTargetLowering::LowerSHL_PARTS(SDValue Op, SelectionDAG &DAG) const {
  EVT VT = Op.getValueType();
  unsigned BitWidth = VT.getSizeInBits();
  SDLoc dl(Op);
  assert(Op.getNumOperands() == 3 &&
         VT == Op.getOperand(1).getValueType() &&
         "Unexpected SHL!");

  // Expand into a bunch of logical ops.  Note that these ops
  // depend on the PPC behavior for oversized shift amounts.
  SDValue Lo = Op.getOperand(0);
  SDValue Hi = Op.getOperand(1);
  SDValue Amt = Op.getOperand(2);
  EVT AmtVT = Amt.getValueType();

  SDValue Tmp1 = DAG.getNode(ISD::SUB, dl, AmtVT,
                             DAG.getConstant(BitWidth, dl, AmtVT), Amt);
  SDValue Tmp2 = DAG.getNode(PPCISD::SHL, dl, VT, Hi, Amt);
  SDValue Tmp3 = DAG.getNode(PPCISD::SRL, dl, VT, Lo, Tmp1);
  SDValue Tmp4 = DAG.getNode(ISD::OR , dl, VT, Tmp2, Tmp3);
  SDValue Tmp5 = DAG.getNode(ISD::ADD, dl, AmtVT, Amt,
                             DAG.getConstant(-BitWidth, dl, AmtVT));
  SDValue Tmp6 = DAG.getNode(PPCISD::SHL, dl, VT, Lo, Tmp5);
  SDValue OutHi = DAG.getNode(ISD::OR, dl, VT, Tmp4, Tmp6);
  SDValue OutLo = DAG.getNode(PPCISD::SHL, dl, VT, Lo, Amt);
  SDValue OutOps[] = { OutLo, OutHi };
  return DAG.getMergeValues(OutOps, dl);
}

SDValue PPCTargetLowering::LowerSRL_PARTS(SDValue Op, SelectionDAG &DAG) const {
  EVT VT = Op.getValueType();
  SDLoc dl(Op);
  unsigned BitWidth = VT.getSizeInBits();
  assert(Op.getNumOperands() == 3 &&
         VT == Op.getOperand(1).getValueType() &&
         "Unexpected SRL!");

  // Expand into a bunch of logical ops.  Note that these ops
  // depend on the PPC behavior for oversized shift amounts.
  SDValue Lo = Op.getOperand(0);
  SDValue Hi = Op.getOperand(1);
  SDValue Amt = Op.getOperand(2);
  EVT AmtVT = Amt.getValueType();

  SDValue Tmp1 = DAG.getNode(ISD::SUB, dl, AmtVT,
                             DAG.getConstant(BitWidth, dl, AmtVT), Amt);
  SDValue Tmp2 = DAG.getNode(PPCISD::SRL, dl, VT, Lo, Amt);
  SDValue Tmp3 = DAG.getNode(PPCISD::SHL, dl, VT, Hi, Tmp1);
  SDValue Tmp4 = DAG.getNode(ISD::OR, dl, VT, Tmp2, Tmp3);
  SDValue Tmp5 = DAG.getNode(ISD::ADD, dl, AmtVT, Amt,
                             DAG.getConstant(-BitWidth, dl, AmtVT));
  SDValue Tmp6 = DAG.getNode(PPCISD::SRL, dl, VT, Hi, Tmp5);
  SDValue OutLo = DAG.getNode(ISD::OR, dl, VT, Tmp4, Tmp6);
  SDValue OutHi = DAG.getNode(PPCISD::SRL, dl, VT, Hi, Amt);
  SDValue OutOps[] = { OutLo, OutHi };
  return DAG.getMergeValues(OutOps, dl);
}

SDValue PPCTargetLowering::LowerSRA_PARTS(SDValue Op, SelectionDAG &DAG) const {
  SDLoc dl(Op);
  EVT VT = Op.getValueType();
  unsigned BitWidth = VT.getSizeInBits();
  assert(Op.getNumOperands() == 3 &&
         VT == Op.getOperand(1).getValueType() &&
         "Unexpected SRA!");

  // Expand into a bunch of logical ops, followed by a select_cc.
  SDValue Lo = Op.getOperand(0);
  SDValue Hi = Op.getOperand(1);
  SDValue Amt = Op.getOperand(2);
  EVT AmtVT = Amt.getValueType();

  SDValue Tmp1 = DAG.getNode(ISD::SUB, dl, AmtVT,
                             DAG.getConstant(BitWidth, dl, AmtVT), Amt);
  SDValue Tmp2 = DAG.getNode(PPCISD::SRL, dl, VT, Lo, Amt);
  SDValue Tmp3 = DAG.getNode(PPCISD::SHL, dl, VT, Hi, Tmp1);
  SDValue Tmp4 = DAG.getNode(ISD::OR, dl, VT, Tmp2, Tmp3);
  SDValue Tmp5 = DAG.getNode(ISD::ADD, dl, AmtVT, Amt,
                             DAG.getConstant(-BitWidth, dl, AmtVT));
  SDValue Tmp6 = DAG.getNode(PPCISD::SRA, dl, VT, Hi, Tmp5);
  SDValue OutHi = DAG.getNode(PPCISD::SRA, dl, VT, Hi, Amt);
  SDValue OutLo = DAG.getSelectCC(dl, Tmp5, DAG.getConstant(0, dl, AmtVT),
                                  Tmp4, Tmp6, ISD::SETLE);
  SDValue OutOps[] = { OutLo, OutHi };
  return DAG.getMergeValues(OutOps, dl);
}

//===----------------------------------------------------------------------===//
// Vector related lowering.
//

/// BuildSplatI - Build a canonical splati of Val with an element size of
/// SplatSize.  Cast the result to VT.
static SDValue BuildSplatI(int Val, unsigned SplatSize, EVT VT,
                           SelectionDAG &DAG, const SDLoc &dl) {
  assert(Val >= -16 && Val <= 15 && "vsplti is out of range!");

  static const MVT VTys[] = { // canonical VT to use for each size.
    MVT::v16i8, MVT::v8i16, MVT::Other, MVT::v4i32
  };

  EVT ReqVT = VT != MVT::Other ? VT : VTys[SplatSize-1];

  // Force vspltis[hw] -1 to vspltisb -1 to canonicalize.
  if (Val == -1)
    SplatSize = 1;

  EVT CanonicalVT = VTys[SplatSize-1];

  // Build a canonical splat for this value.
  return DAG.getBitcast(ReqVT, DAG.getConstant(Val, dl, CanonicalVT));
}

/// BuildIntrinsicOp - Return a unary operator intrinsic node with the
/// specified intrinsic ID.
static SDValue BuildIntrinsicOp(unsigned IID, SDValue Op, SelectionDAG &DAG,
                                const SDLoc &dl, EVT DestVT = MVT::Other) {
  if (DestVT == MVT::Other) DestVT = Op.getValueType();
  return DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, DestVT,
                     DAG.getConstant(IID, dl, MVT::i32), Op);
}

/// BuildIntrinsicOp - Return a binary operator intrinsic node with the
/// specified intrinsic ID.
static SDValue BuildIntrinsicOp(unsigned IID, SDValue LHS, SDValue RHS,
                                SelectionDAG &DAG, const SDLoc &dl,
                                EVT DestVT = MVT::Other) {
  if (DestVT == MVT::Other) DestVT = LHS.getValueType();
  return DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, DestVT,
                     DAG.getConstant(IID, dl, MVT::i32), LHS, RHS);
}

/// BuildIntrinsicOp - Return a ternary operator intrinsic node with the
/// specified intrinsic ID.
static SDValue BuildIntrinsicOp(unsigned IID, SDValue Op0, SDValue Op1,
                                SDValue Op2, SelectionDAG &DAG, const SDLoc &dl,
                                EVT DestVT = MVT::Other) {
  if (DestVT == MVT::Other) DestVT = Op0.getValueType();
  return DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, DestVT,
                     DAG.getConstant(IID, dl, MVT::i32), Op0, Op1, Op2);
}

/// BuildVSLDOI - Return a VECTOR_SHUFFLE that is a vsldoi of the specified
/// amount.  The result has the specified value type.
static SDValue BuildVSLDOI(SDValue LHS, SDValue RHS, unsigned Amt, EVT VT,
                           SelectionDAG &DAG, const SDLoc &dl) {
  // Force LHS/RHS to be the right type.
  LHS = DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, LHS);
  RHS = DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, RHS);

  int Ops[16];
  for (unsigned i = 0; i != 16; ++i)
    Ops[i] = i + Amt;
  SDValue T = DAG.getVectorShuffle(MVT::v16i8, dl, LHS, RHS, Ops);
  return DAG.getNode(ISD::BITCAST, dl, VT, T);
}

/// Do we have an efficient pattern in a .td file for this node?
///
/// \param V - pointer to the BuildVectorSDNode being matched
/// \param HasDirectMove - does this subtarget have VSR <-> GPR direct moves?
///
/// There are some patterns where it is beneficial to keep a BUILD_VECTOR
/// node as a BUILD_VECTOR node rather than expanding it. The patterns where
/// the opposite is true (expansion is beneficial) are:
/// - The node builds a vector out of integers that are not 32 or 64-bits
/// - The node builds a vector out of constants
/// - The node is a "load-and-splat"
/// In all other cases, we will choose to keep the BUILD_VECTOR.
static bool haveEfficientBuildVectorPattern(BuildVectorSDNode *V,
                                            bool HasDirectMove,
                                            bool HasP8Vector) {
  EVT VecVT = V->getValueType(0);
  bool RightType = VecVT == MVT::v2f64 ||
    (HasP8Vector && VecVT == MVT::v4f32) ||
    (HasDirectMove && (VecVT == MVT::v2i64 || VecVT == MVT::v4i32));
  if (!RightType)
    return false;

  bool IsSplat = true;
  bool IsLoad = false;
  SDValue Op0 = V->getOperand(0);

  // This function is called in a block that confirms the node is not a constant
  // splat. So a constant BUILD_VECTOR here means the vector is built out of
  // different constants.
  if (V->isConstant())
    return false;
  for (int i = 0, e = V->getNumOperands(); i < e; ++i) {
    if (V->getOperand(i).isUndef())
      return false;
    // We want to expand nodes that represent load-and-splat even if the
    // loaded value is a floating point truncation or conversion to int.
    if (V->getOperand(i).getOpcode() == ISD::LOAD ||
        (V->getOperand(i).getOpcode() == ISD::FP_ROUND &&
         V->getOperand(i).getOperand(0).getOpcode() == ISD::LOAD) ||
        (V->getOperand(i).getOpcode() == ISD::FP_TO_SINT &&
         V->getOperand(i).getOperand(0).getOpcode() == ISD::LOAD) ||
        (V->getOperand(i).getOpcode() == ISD::FP_TO_UINT &&
         V->getOperand(i).getOperand(0).getOpcode() == ISD::LOAD))
      IsLoad = true;
    // If the operands are different or the input is not a load and has more
    // uses than just this BV node, then it isn't a splat.
    if (V->getOperand(i) != Op0 ||
        (!IsLoad && !V->isOnlyUserOf(V->getOperand(i).getNode())))
      IsSplat = false;
  }
  return !(IsSplat && IsLoad);
}

// Lower BITCAST(f128, (build_pair i64, i64)) to BUILD_FP128.
SDValue PPCTargetLowering::LowerBITCAST(SDValue Op, SelectionDAG &DAG) const {

  SDLoc dl(Op);
  SDValue Op0 = Op->getOperand(0);

  if (!EnableQuadPrecision ||
      (Op.getValueType() != MVT::f128 ) ||
      (Op0.getOpcode() != ISD::BUILD_PAIR) ||
      (Op0.getOperand(0).getValueType() !=  MVT::i64) ||
      (Op0.getOperand(1).getValueType() != MVT::i64))
    return SDValue();

  return DAG.getNode(PPCISD::BUILD_FP128, dl, MVT::f128, Op0.getOperand(0),
                     Op0.getOperand(1));
}

// If this is a case we can't handle, return null and let the default
// expansion code take care of it.  If we CAN select this case, and if it
// selects to a single instruction, return Op.  Otherwise, if we can codegen
// this case more efficiently than a constant pool load, lower it to the
// sequence of ops that should be used.
SDValue PPCTargetLowering::LowerBUILD_VECTOR(SDValue Op,
                                             SelectionDAG &DAG) const {
  SDLoc dl(Op);
  BuildVectorSDNode *BVN = dyn_cast<BuildVectorSDNode>(Op.getNode());
  assert(BVN && "Expected a BuildVectorSDNode in LowerBUILD_VECTOR");

  if (Subtarget.hasQPX() && Op.getValueType() == MVT::v4i1) {
    // We first build an i32 vector, load it into a QPX register,
    // then convert it to a floating-point vector and compare it
    // to a zero vector to get the boolean result.
    MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
    int FrameIdx = MFI.CreateStackObject(16, 16, false);
    MachinePointerInfo PtrInfo =
        MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FrameIdx);
    EVT PtrVT = getPointerTy(DAG.getDataLayout());
    SDValue FIdx = DAG.getFrameIndex(FrameIdx, PtrVT);

    assert(BVN->getNumOperands() == 4 &&
      "BUILD_VECTOR for v4i1 does not have 4 operands");

    bool IsConst = true;
    for (unsigned i = 0; i < 4; ++i) {
      if (BVN->getOperand(i).isUndef()) continue;
      if (!isa<ConstantSDNode>(BVN->getOperand(i))) {
        IsConst = false;
        break;
      }
    }

    if (IsConst) {
      Constant *One =
        ConstantFP::get(Type::getFloatTy(*DAG.getContext()), 1.0);
      Constant *NegOne =
        ConstantFP::get(Type::getFloatTy(*DAG.getContext()), -1.0);

      Constant *CV[4];
      for (unsigned i = 0; i < 4; ++i) {
        if (BVN->getOperand(i).isUndef())
          CV[i] = UndefValue::get(Type::getFloatTy(*DAG.getContext()));
        else if (isNullConstant(BVN->getOperand(i)))
          CV[i] = NegOne;
        else
          CV[i] = One;
      }

      Constant *CP = ConstantVector::get(CV);
      SDValue CPIdx = DAG.getConstantPool(CP, getPointerTy(DAG.getDataLayout()),
                                          16 /* alignment */);

      SDValue Ops[] = {DAG.getEntryNode(), CPIdx};
      SDVTList VTs = DAG.getVTList({MVT::v4i1, /*chain*/ MVT::Other});
      return DAG.getMemIntrinsicNode(
          PPCISD::QVLFSb, dl, VTs, Ops, MVT::v4f32,
          MachinePointerInfo::getConstantPool(DAG.getMachineFunction()));
    }

    SmallVector<SDValue, 4> Stores;
    for (unsigned i = 0; i < 4; ++i) {
      if (BVN->getOperand(i).isUndef()) continue;

      unsigned Offset = 4*i;
      SDValue Idx = DAG.getConstant(Offset, dl, FIdx.getValueType());
      Idx = DAG.getNode(ISD::ADD, dl, FIdx.getValueType(), FIdx, Idx);

      unsigned StoreSize = BVN->getOperand(i).getValueType().getStoreSize();
      if (StoreSize > 4) {
        Stores.push_back(
            DAG.getTruncStore(DAG.getEntryNode(), dl, BVN->getOperand(i), Idx,
                              PtrInfo.getWithOffset(Offset), MVT::i32));
      } else {
        SDValue StoreValue = BVN->getOperand(i);
        if (StoreSize < 4)
          StoreValue = DAG.getNode(ISD::ANY_EXTEND, dl, MVT::i32, StoreValue);

        Stores.push_back(DAG.getStore(DAG.getEntryNode(), dl, StoreValue, Idx,
                                      PtrInfo.getWithOffset(Offset)));
      }
    }

    SDValue StoreChain;
    if (!Stores.empty())
      StoreChain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Stores);
    else
      StoreChain = DAG.getEntryNode();

    // Now load from v4i32 into the QPX register; this will extend it to
    // v4i64 but not yet convert it to a floating point. Nevertheless, this
    // is typed as v4f64 because the QPX register integer states are not
    // explicitly represented.

    SDValue Ops[] = {StoreChain,
                     DAG.getConstant(Intrinsic::ppc_qpx_qvlfiwz, dl, MVT::i32),
                     FIdx};
    SDVTList VTs = DAG.getVTList({MVT::v4f64, /*chain*/ MVT::Other});

    SDValue LoadedVect = DAG.getMemIntrinsicNode(ISD::INTRINSIC_W_CHAIN,
      dl, VTs, Ops, MVT::v4i32, PtrInfo);
    LoadedVect = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, MVT::v4f64,
      DAG.getConstant(Intrinsic::ppc_qpx_qvfcfidu, dl, MVT::i32),
      LoadedVect);

    SDValue FPZeros = DAG.getConstantFP(0.0, dl, MVT::v4f64);

    return DAG.getSetCC(dl, MVT::v4i1, LoadedVect, FPZeros, ISD::SETEQ);
  }

  // All other QPX vectors are handled by generic code.
  if (Subtarget.hasQPX())
    return SDValue();

  // Check if this is a splat of a constant value.
  APInt APSplatBits, APSplatUndef;
  unsigned SplatBitSize;
  bool HasAnyUndefs;
  if (! BVN->isConstantSplat(APSplatBits, APSplatUndef, SplatBitSize,
                             HasAnyUndefs, 0, !Subtarget.isLittleEndian()) ||
      SplatBitSize > 32) {
    // BUILD_VECTOR nodes that are not constant splats of up to 32-bits can be
    // lowered to VSX instructions under certain conditions.
    // Without VSX, there is no pattern more efficient than expanding the node.
    if (Subtarget.hasVSX() &&
        haveEfficientBuildVectorPattern(BVN, Subtarget.hasDirectMove(),
                                        Subtarget.hasP8Vector()))
      return Op;
    return SDValue();
  }

  unsigned SplatBits = APSplatBits.getZExtValue();
  unsigned SplatUndef = APSplatUndef.getZExtValue();
  unsigned SplatSize = SplatBitSize / 8;

  // First, handle single instruction cases.

  // All zeros?
  if (SplatBits == 0) {
    // Canonicalize all zero vectors to be v4i32.
    if (Op.getValueType() != MVT::v4i32 || HasAnyUndefs) {
      SDValue Z = DAG.getConstant(0, dl, MVT::v4i32);
      Op = DAG.getNode(ISD::BITCAST, dl, Op.getValueType(), Z);
    }
    return Op;
  }

  // We have XXSPLTIB for constant splats one byte wide
  if (Subtarget.hasP9Vector() && SplatSize == 1) {
    // This is a splat of 1-byte elements with some elements potentially undef.
    // Rather than trying to match undef in the SDAG patterns, ensure that all
    // elements are the same constant.
    if (HasAnyUndefs || ISD::isBuildVectorAllOnes(BVN)) {
      SmallVector<SDValue, 16> Ops(16, DAG.getConstant(SplatBits,
                                                       dl, MVT::i32));
      SDValue NewBV = DAG.getBuildVector(MVT::v16i8, dl, Ops);
      if (Op.getValueType() != MVT::v16i8)
        return DAG.getBitcast(Op.getValueType(), NewBV);
      return NewBV;
    }

    // BuildVectorSDNode::isConstantSplat() is actually pretty smart. It'll
    // detect that constant splats like v8i16: 0xABAB are really just splats
    // of a 1-byte constant. In this case, we need to convert the node to a
    // splat of v16i8 and a bitcast.
    if (Op.getValueType() != MVT::v16i8)
      return DAG.getBitcast(Op.getValueType(),
                            DAG.getConstant(SplatBits, dl, MVT::v16i8));

    return Op;
  }

  // If the sign extended value is in the range [-16,15], use VSPLTI[bhw].
  int32_t SextVal= (int32_t(SplatBits << (32-SplatBitSize)) >>
                    (32-SplatBitSize));
  if (SextVal >= -16 && SextVal <= 15)
    return BuildSplatI(SextVal, SplatSize, Op.getValueType(), DAG, dl);

  // Two instruction sequences.

  // If this value is in the range [-32,30] and is even, use:
  //     VSPLTI[bhw](val/2) + VSPLTI[bhw](val/2)
  // If this value is in the range [17,31] and is odd, use:
  //     VSPLTI[bhw](val-16) - VSPLTI[bhw](-16)
  // If this value is in the range [-31,-17] and is odd, use:
  //     VSPLTI[bhw](val+16) + VSPLTI[bhw](-16)
  // Note the last two are three-instruction sequences.
  if (SextVal >= -32 && SextVal <= 31) {
    // To avoid having these optimizations undone by constant folding,
    // we convert to a pseudo that will be expanded later into one of
    // the above forms.
    SDValue Elt = DAG.getConstant(SextVal, dl, MVT::i32);
    EVT VT = (SplatSize == 1 ? MVT::v16i8 :
              (SplatSize == 2 ? MVT::v8i16 : MVT::v4i32));
    SDValue EltSize = DAG.getConstant(SplatSize, dl, MVT::i32);
    SDValue RetVal = DAG.getNode(PPCISD::VADD_SPLAT, dl, VT, Elt, EltSize);
    if (VT == Op.getValueType())
      return RetVal;
    else
      return DAG.getNode(ISD::BITCAST, dl, Op.getValueType(), RetVal);
  }

  // If this is 0x8000_0000 x 4, turn into vspltisw + vslw.  If it is
  // 0x7FFF_FFFF x 4, turn it into not(0x8000_0000).  This is important
  // for fneg/fabs.
  if (SplatSize == 4 && SplatBits == (0x7FFFFFFF&~SplatUndef)) {
    // Make -1 and vspltisw -1:
    SDValue OnesV = BuildSplatI(-1, 4, MVT::v4i32, DAG, dl);

    // Make the VSLW intrinsic, computing 0x8000_0000.
    SDValue Res = BuildIntrinsicOp(Intrinsic::ppc_altivec_vslw, OnesV,
                                   OnesV, DAG, dl);

    // xor by OnesV to invert it.
    Res = DAG.getNode(ISD::XOR, dl, MVT::v4i32, Res, OnesV);
    return DAG.getNode(ISD::BITCAST, dl, Op.getValueType(), Res);
  }

  // Check to see if this is a wide variety of vsplti*, binop self cases.
  static const signed char SplatCsts[] = {
    -1, 1, -2, 2, -3, 3, -4, 4, -5, 5, -6, 6, -7, 7,
    -8, 8, -9, 9, -10, 10, -11, 11, -12, 12, -13, 13, 14, -14, 15, -15, -16
  };

  for (unsigned idx = 0; idx < array_lengthof(SplatCsts); ++idx) {
    // Indirect through the SplatCsts array so that we favor 'vsplti -1' for
    // cases which are ambiguous (e.g. formation of 0x8000_0000).  'vsplti -1'
    int i = SplatCsts[idx];

    // Figure out what shift amount will be used by altivec if shifted by i in
    // this splat size.
    unsigned TypeShiftAmt = i & (SplatBitSize-1);

    // vsplti + shl self.
    if (SextVal == (int)((unsigned)i << TypeShiftAmt)) {
      SDValue Res = BuildSplatI(i, SplatSize, MVT::Other, DAG, dl);
      static const unsigned IIDs[] = { // Intrinsic to use for each size.
        Intrinsic::ppc_altivec_vslb, Intrinsic::ppc_altivec_vslh, 0,
        Intrinsic::ppc_altivec_vslw
      };
      Res = BuildIntrinsicOp(IIDs[SplatSize-1], Res, Res, DAG, dl);
      return DAG.getNode(ISD::BITCAST, dl, Op.getValueType(), Res);
    }

    // vsplti + srl self.
    if (SextVal == (int)((unsigned)i >> TypeShiftAmt)) {
      SDValue Res = BuildSplatI(i, SplatSize, MVT::Other, DAG, dl);
      static const unsigned IIDs[] = { // Intrinsic to use for each size.
        Intrinsic::ppc_altivec_vsrb, Intrinsic::ppc_altivec_vsrh, 0,
        Intrinsic::ppc_altivec_vsrw
      };
      Res = BuildIntrinsicOp(IIDs[SplatSize-1], Res, Res, DAG, dl);
      return DAG.getNode(ISD::BITCAST, dl, Op.getValueType(), Res);
    }

    // vsplti + sra self.
    if (SextVal == (int)((unsigned)i >> TypeShiftAmt)) {
      SDValue Res = BuildSplatI(i, SplatSize, MVT::Other, DAG, dl);
      static const unsigned IIDs[] = { // Intrinsic to use for each size.
        Intrinsic::ppc_altivec_vsrab, Intrinsic::ppc_altivec_vsrah, 0,
        Intrinsic::ppc_altivec_vsraw
      };
      Res = BuildIntrinsicOp(IIDs[SplatSize-1], Res, Res, DAG, dl);
      return DAG.getNode(ISD::BITCAST, dl, Op.getValueType(), Res);
    }

    // vsplti + rol self.
    if (SextVal == (int)(((unsigned)i << TypeShiftAmt) |
                         ((unsigned)i >> (SplatBitSize-TypeShiftAmt)))) {
      SDValue Res = BuildSplatI(i, SplatSize, MVT::Other, DAG, dl);
      static const unsigned IIDs[] = { // Intrinsic to use for each size.
        Intrinsic::ppc_altivec_vrlb, Intrinsic::ppc_altivec_vrlh, 0,
        Intrinsic::ppc_altivec_vrlw
      };
      Res = BuildIntrinsicOp(IIDs[SplatSize-1], Res, Res, DAG, dl);
      return DAG.getNode(ISD::BITCAST, dl, Op.getValueType(), Res);
    }

    // t = vsplti c, result = vsldoi t, t, 1
    if (SextVal == (int)(((unsigned)i << 8) | (i < 0 ? 0xFF : 0))) {
      SDValue T = BuildSplatI(i, SplatSize, MVT::v16i8, DAG, dl);
      unsigned Amt = Subtarget.isLittleEndian() ? 15 : 1;
      return BuildVSLDOI(T, T, Amt, Op.getValueType(), DAG, dl);
    }
    // t = vsplti c, result = vsldoi t, t, 2
    if (SextVal == (int)(((unsigned)i << 16) | (i < 0 ? 0xFFFF : 0))) {
      SDValue T = BuildSplatI(i, SplatSize, MVT::v16i8, DAG, dl);
      unsigned Amt = Subtarget.isLittleEndian() ? 14 : 2;
      return BuildVSLDOI(T, T, Amt, Op.getValueType(), DAG, dl);
    }
    // t = vsplti c, result = vsldoi t, t, 3
    if (SextVal == (int)(((unsigned)i << 24) | (i < 0 ? 0xFFFFFF : 0))) {
      SDValue T = BuildSplatI(i, SplatSize, MVT::v16i8, DAG, dl);
      unsigned Amt = Subtarget.isLittleEndian() ? 13 : 3;
      return BuildVSLDOI(T, T, Amt, Op.getValueType(), DAG, dl);
    }
  }

  return SDValue();
}

/// GeneratePerfectShuffle - Given an entry in the perfect-shuffle table, emit
/// the specified operations to build the shuffle.
static SDValue GeneratePerfectShuffle(unsigned PFEntry, SDValue LHS,
                                      SDValue RHS, SelectionDAG &DAG,
                                      const SDLoc &dl) {
  unsigned OpNum = (PFEntry >> 26) & 0x0F;
  unsigned LHSID = (PFEntry >> 13) & ((1 << 13)-1);
  unsigned RHSID = (PFEntry >>  0) & ((1 << 13)-1);

  enum {
    OP_COPY = 0,  // Copy, used for things like <u,u,u,3> to say it is <0,1,2,3>
    OP_VMRGHW,
    OP_VMRGLW,
    OP_VSPLTISW0,
    OP_VSPLTISW1,
    OP_VSPLTISW2,
    OP_VSPLTISW3,
    OP_VSLDOI4,
    OP_VSLDOI8,
    OP_VSLDOI12
  };

  if (OpNum == OP_COPY) {
    if (LHSID == (1*9+2)*9+3) return LHS;
    assert(LHSID == ((4*9+5)*9+6)*9+7 && "Illegal OP_COPY!");
    return RHS;
  }

  SDValue OpLHS, OpRHS;
  OpLHS = GeneratePerfectShuffle(PerfectShuffleTable[LHSID], LHS, RHS, DAG, dl);
  OpRHS = GeneratePerfectShuffle(PerfectShuffleTable[RHSID], LHS, RHS, DAG, dl);

  int ShufIdxs[16];
  switch (OpNum) {
  default: llvm_unreachable("Unknown i32 permute!");
  case OP_VMRGHW:
    ShufIdxs[ 0] =  0; ShufIdxs[ 1] =  1; ShufIdxs[ 2] =  2; ShufIdxs[ 3] =  3;
    ShufIdxs[ 4] = 16; ShufIdxs[ 5] = 17; ShufIdxs[ 6] = 18; ShufIdxs[ 7] = 19;
    ShufIdxs[ 8] =  4; ShufIdxs[ 9] =  5; ShufIdxs[10] =  6; ShufIdxs[11] =  7;
    ShufIdxs[12] = 20; ShufIdxs[13] = 21; ShufIdxs[14] = 22; ShufIdxs[15] = 23;
    break;
  case OP_VMRGLW:
    ShufIdxs[ 0] =  8; ShufIdxs[ 1] =  9; ShufIdxs[ 2] = 10; ShufIdxs[ 3] = 11;
    ShufIdxs[ 4] = 24; ShufIdxs[ 5] = 25; ShufIdxs[ 6] = 26; ShufIdxs[ 7] = 27;
    ShufIdxs[ 8] = 12; ShufIdxs[ 9] = 13; ShufIdxs[10] = 14; ShufIdxs[11] = 15;
    ShufIdxs[12] = 28; ShufIdxs[13] = 29; ShufIdxs[14] = 30; ShufIdxs[15] = 31;
    break;
  case OP_VSPLTISW0:
    for (unsigned i = 0; i != 16; ++i)
      ShufIdxs[i] = (i&3)+0;
    break;
  case OP_VSPLTISW1:
    for (unsigned i = 0; i != 16; ++i)
      ShufIdxs[i] = (i&3)+4;
    break;
  case OP_VSPLTISW2:
    for (unsigned i = 0; i != 16; ++i)
      ShufIdxs[i] = (i&3)+8;
    break;
  case OP_VSPLTISW3:
    for (unsigned i = 0; i != 16; ++i)
      ShufIdxs[i] = (i&3)+12;
    break;
  case OP_VSLDOI4:
    return BuildVSLDOI(OpLHS, OpRHS, 4, OpLHS.getValueType(), DAG, dl);
  case OP_VSLDOI8:
    return BuildVSLDOI(OpLHS, OpRHS, 8, OpLHS.getValueType(), DAG, dl);
  case OP_VSLDOI12:
    return BuildVSLDOI(OpLHS, OpRHS, 12, OpLHS.getValueType(), DAG, dl);
  }
  EVT VT = OpLHS.getValueType();
  OpLHS = DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, OpLHS);
  OpRHS = DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, OpRHS);
  SDValue T = DAG.getVectorShuffle(MVT::v16i8, dl, OpLHS, OpRHS, ShufIdxs);
  return DAG.getNode(ISD::BITCAST, dl, VT, T);
}

/// lowerToVINSERTB - Return the SDValue if this VECTOR_SHUFFLE can be handled
/// by the VINSERTB instruction introduced in ISA 3.0, else just return default
/// SDValue.
SDValue PPCTargetLowering::lowerToVINSERTB(ShuffleVectorSDNode *N,
                                           SelectionDAG &DAG) const {
  const unsigned BytesInVector = 16;
  bool IsLE = Subtarget.isLittleEndian();
  SDLoc dl(N);
  SDValue V1 = N->getOperand(0);
  SDValue V2 = N->getOperand(1);
  unsigned ShiftElts = 0, InsertAtByte = 0;
  bool Swap = false;

  // Shifts required to get the byte we want at element 7.
  unsigned LittleEndianShifts[] = {8, 7,  6,  5,  4,  3,  2,  1,
                                   0, 15, 14, 13, 12, 11, 10, 9};
  unsigned BigEndianShifts[] = {9, 10, 11, 12, 13, 14, 15, 0,
                                1, 2,  3,  4,  5,  6,  7,  8};

  ArrayRef<int> Mask = N->getMask();
  int OriginalOrder[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

  // For each mask element, find out if we're just inserting something
  // from V2 into V1 or vice versa.
  // Possible permutations inserting an element from V2 into V1:
  //   X, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
  //   0, X, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
  //   ...
  //   0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, X
  // Inserting from V1 into V2 will be similar, except mask range will be
  // [16,31].

  bool FoundCandidate = false;
  // If both vector operands for the shuffle are the same vector, the mask
  // will contain only elements from the first one and the second one will be
  // undef.
  unsigned VINSERTBSrcElem = IsLE ? 8 : 7;
  // Go through the mask of half-words to find an element that's being moved
  // from one vector to the other.
  for (unsigned i = 0; i < BytesInVector; ++i) {
    unsigned CurrentElement = Mask[i];
    // If 2nd operand is undefined, we should only look for element 7 in the
    // Mask.
    if (V2.isUndef() && CurrentElement != VINSERTBSrcElem)
      continue;

    bool OtherElementsInOrder = true;
    // Examine the other elements in the Mask to see if they're in original
    // order.
    for (unsigned j = 0; j < BytesInVector; ++j) {
      if (j == i)
        continue;
      // If CurrentElement is from V1 [0,15], then we the rest of the Mask to be
      // from V2 [16,31] and vice versa.  Unless the 2nd operand is undefined,
      // in which we always assume we're always picking from the 1st operand.
      int MaskOffset =
          (!V2.isUndef() && CurrentElement < BytesInVector) ? BytesInVector : 0;
      if (Mask[j] != OriginalOrder[j] + MaskOffset) {
        OtherElementsInOrder = false;
        break;
      }
    }
    // If other elements are in original order, we record the number of shifts
    // we need to get the element we want into element 7. Also record which byte
    // in the vector we should insert into.
    if (OtherElementsInOrder) {
      // If 2nd operand is undefined, we assume no shifts and no swapping.
      if (V2.isUndef()) {
        ShiftElts = 0;
        Swap = false;
      } else {
        // Only need the last 4-bits for shifts because operands will be swapped if CurrentElement is >= 2^4.
        ShiftElts = IsLE ? LittleEndianShifts[CurrentElement & 0xF]
                         : BigEndianShifts[CurrentElement & 0xF];
        Swap = CurrentElement < BytesInVector;
      }
      InsertAtByte = IsLE ? BytesInVector - (i + 1) : i;
      FoundCandidate = true;
      break;
    }
  }

  if (!FoundCandidate)
    return SDValue();

  // Candidate found, construct the proper SDAG sequence with VINSERTB,
  // optionally with VECSHL if shift is required.
  if (Swap)
    std::swap(V1, V2);
  if (V2.isUndef())
    V2 = V1;
  if (ShiftElts) {
    SDValue Shl = DAG.getNode(PPCISD::VECSHL, dl, MVT::v16i8, V2, V2,
                              DAG.getConstant(ShiftElts, dl, MVT::i32));
    return DAG.getNode(PPCISD::VECINSERT, dl, MVT::v16i8, V1, Shl,
                       DAG.getConstant(InsertAtByte, dl, MVT::i32));
  }
  return DAG.getNode(PPCISD::VECINSERT, dl, MVT::v16i8, V1, V2,
                     DAG.getConstant(InsertAtByte, dl, MVT::i32));
}

/// lowerToVINSERTH - Return the SDValue if this VECTOR_SHUFFLE can be handled
/// by the VINSERTH instruction introduced in ISA 3.0, else just return default
/// SDValue.
SDValue PPCTargetLowering::lowerToVINSERTH(ShuffleVectorSDNode *N,
                                           SelectionDAG &DAG) const {
  const unsigned NumHalfWords = 8;
  const unsigned BytesInVector = NumHalfWords * 2;
  // Check that the shuffle is on half-words.
  if (!isNByteElemShuffleMask(N, 2, 1))
    return SDValue();

  bool IsLE = Subtarget.isLittleEndian();
  SDLoc dl(N);
  SDValue V1 = N->getOperand(0);
  SDValue V2 = N->getOperand(1);
  unsigned ShiftElts = 0, InsertAtByte = 0;
  bool Swap = false;

  // Shifts required to get the half-word we want at element 3.
  unsigned LittleEndianShifts[] = {4, 3, 2, 1, 0, 7, 6, 5};
  unsigned BigEndianShifts[] = {5, 6, 7, 0, 1, 2, 3, 4};

  uint32_t Mask = 0;
  uint32_t OriginalOrderLow = 0x1234567;
  uint32_t OriginalOrderHigh = 0x89ABCDEF;
  // Now we look at mask elements 0,2,4,6,8,10,12,14.  Pack the mask into a
  // 32-bit space, only need 4-bit nibbles per element.
  for (unsigned i = 0; i < NumHalfWords; ++i) {
    unsigned MaskShift = (NumHalfWords - 1 - i) * 4;
    Mask |= ((uint32_t)(N->getMaskElt(i * 2) / 2) << MaskShift);
  }

  // For each mask element, find out if we're just inserting something
  // from V2 into V1 or vice versa.  Possible permutations inserting an element
  // from V2 into V1:
  //   X, 1, 2, 3, 4, 5, 6, 7
  //   0, X, 2, 3, 4, 5, 6, 7
  //   0, 1, X, 3, 4, 5, 6, 7
  //   0, 1, 2, X, 4, 5, 6, 7
  //   0, 1, 2, 3, X, 5, 6, 7
  //   0, 1, 2, 3, 4, X, 6, 7
  //   0, 1, 2, 3, 4, 5, X, 7
  //   0, 1, 2, 3, 4, 5, 6, X
  // Inserting from V1 into V2 will be similar, except mask range will be [8,15].

  bool FoundCandidate = false;
  // Go through the mask of half-words to find an element that's being moved
  // from one vector to the other.
  for (unsigned i = 0; i < NumHalfWords; ++i) {
    unsigned MaskShift = (NumHalfWords - 1 - i) * 4;
    uint32_t MaskOneElt = (Mask >> MaskShift) & 0xF;
    uint32_t MaskOtherElts = ~(0xF << MaskShift);
    uint32_t TargetOrder = 0x0;

    // If both vector operands for the shuffle are the same vector, the mask
    // will contain only elements from the first one and the second one will be
    // undef.
    if (V2.isUndef()) {
      ShiftElts = 0;
      unsigned VINSERTHSrcElem = IsLE ? 4 : 3;
      TargetOrder = OriginalOrderLow;
      Swap = false;
      // Skip if not the correct element or mask of other elements don't equal
      // to our expected order.
      if (MaskOneElt == VINSERTHSrcElem &&
          (Mask & MaskOtherElts) == (TargetOrder & MaskOtherElts)) {
        InsertAtByte = IsLE ? BytesInVector - (i + 1) * 2 : i * 2;
        FoundCandidate = true;
        break;
      }
    } else { // If both operands are defined.
      // Target order is [8,15] if the current mask is between [0,7].
      TargetOrder =
          (MaskOneElt < NumHalfWords) ? OriginalOrderHigh : OriginalOrderLow;
      // Skip if mask of other elements don't equal our expected order.
      if ((Mask & MaskOtherElts) == (TargetOrder & MaskOtherElts)) {
        // We only need the last 3 bits for the number of shifts.
        ShiftElts = IsLE ? LittleEndianShifts[MaskOneElt & 0x7]
                         : BigEndianShifts[MaskOneElt & 0x7];
        InsertAtByte = IsLE ? BytesInVector - (i + 1) * 2 : i * 2;
        Swap = MaskOneElt < NumHalfWords;
        FoundCandidate = true;
        break;
      }
    }
  }

  if (!FoundCandidate)
    return SDValue();

  // Candidate found, construct the proper SDAG sequence with VINSERTH,
  // optionally with VECSHL if shift is required.
  if (Swap)
    std::swap(V1, V2);
  if (V2.isUndef())
    V2 = V1;
  SDValue Conv1 = DAG.getNode(ISD::BITCAST, dl, MVT::v8i16, V1);
  if (ShiftElts) {
    // Double ShiftElts because we're left shifting on v16i8 type.
    SDValue Shl = DAG.getNode(PPCISD::VECSHL, dl, MVT::v16i8, V2, V2,
                              DAG.getConstant(2 * ShiftElts, dl, MVT::i32));
    SDValue Conv2 = DAG.getNode(ISD::BITCAST, dl, MVT::v8i16, Shl);
    SDValue Ins = DAG.getNode(PPCISD::VECINSERT, dl, MVT::v8i16, Conv1, Conv2,
                              DAG.getConstant(InsertAtByte, dl, MVT::i32));
    return DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, Ins);
  }
  SDValue Conv2 = DAG.getNode(ISD::BITCAST, dl, MVT::v8i16, V2);
  SDValue Ins = DAG.getNode(PPCISD::VECINSERT, dl, MVT::v8i16, Conv1, Conv2,
                            DAG.getConstant(InsertAtByte, dl, MVT::i32));
  return DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, Ins);
}

/// LowerVECTOR_SHUFFLE - Return the code we lower for VECTOR_SHUFFLE.  If this
/// is a shuffle we can handle in a single instruction, return it.  Otherwise,
/// return the code it can be lowered into.  Worst case, it can always be
/// lowered into a vperm.
SDValue PPCTargetLowering::LowerVECTOR_SHUFFLE(SDValue Op,
                                               SelectionDAG &DAG) const {
  SDLoc dl(Op);
  SDValue V1 = Op.getOperand(0);
  SDValue V2 = Op.getOperand(1);
  ShuffleVectorSDNode *SVOp = cast<ShuffleVectorSDNode>(Op);
  EVT VT = Op.getValueType();
  bool isLittleEndian = Subtarget.isLittleEndian();

  unsigned ShiftElts, InsertAtByte;
  bool Swap = false;
  if (Subtarget.hasP9Vector() &&
      PPC::isXXINSERTWMask(SVOp, ShiftElts, InsertAtByte, Swap,
                           isLittleEndian)) {
    if (Swap)
      std::swap(V1, V2);
    SDValue Conv1 = DAG.getNode(ISD::BITCAST, dl, MVT::v4i32, V1);
    SDValue Conv2 = DAG.getNode(ISD::BITCAST, dl, MVT::v4i32, V2);
    if (ShiftElts) {
      SDValue Shl = DAG.getNode(PPCISD::VECSHL, dl, MVT::v4i32, Conv2, Conv2,
                                DAG.getConstant(ShiftElts, dl, MVT::i32));
      SDValue Ins = DAG.getNode(PPCISD::VECINSERT, dl, MVT::v4i32, Conv1, Shl,
                                DAG.getConstant(InsertAtByte, dl, MVT::i32));
      return DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, Ins);
    }
    SDValue Ins = DAG.getNode(PPCISD::VECINSERT, dl, MVT::v4i32, Conv1, Conv2,
                              DAG.getConstant(InsertAtByte, dl, MVT::i32));
    return DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, Ins);
  }

  if (Subtarget.hasP9Altivec()) {
    SDValue NewISDNode;
    if ((NewISDNode = lowerToVINSERTH(SVOp, DAG)))
      return NewISDNode;

    if ((NewISDNode = lowerToVINSERTB(SVOp, DAG)))
      return NewISDNode;
  }

  if (Subtarget.hasVSX() &&
      PPC::isXXSLDWIShuffleMask(SVOp, ShiftElts, Swap, isLittleEndian)) {
    if (Swap)
      std::swap(V1, V2);
    SDValue Conv1 = DAG.getNode(ISD::BITCAST, dl, MVT::v4i32, V1);
    SDValue Conv2 =
        DAG.getNode(ISD::BITCAST, dl, MVT::v4i32, V2.isUndef() ? V1 : V2);

    SDValue Shl = DAG.getNode(PPCISD::VECSHL, dl, MVT::v4i32, Conv1, Conv2,
                              DAG.getConstant(ShiftElts, dl, MVT::i32));
    return DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, Shl);
  }

  if (Subtarget.hasVSX() &&
    PPC::isXXPERMDIShuffleMask(SVOp, ShiftElts, Swap, isLittleEndian)) {
    if (Swap)
      std::swap(V1, V2);
    SDValue Conv1 = DAG.getNode(ISD::BITCAST, dl, MVT::v2i64, V1);
    SDValue Conv2 =
        DAG.getNode(ISD::BITCAST, dl, MVT::v2i64, V2.isUndef() ? V1 : V2);

    SDValue PermDI = DAG.getNode(PPCISD::XXPERMDI, dl, MVT::v2i64, Conv1, Conv2,
                              DAG.getConstant(ShiftElts, dl, MVT::i32));
    return DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, PermDI);
  }

  if (Subtarget.hasP9Vector()) {
     if (PPC::isXXBRHShuffleMask(SVOp)) {
      SDValue Conv = DAG.getNode(ISD::BITCAST, dl, MVT::v8i16, V1);
      SDValue ReveHWord = DAG.getNode(PPCISD::XXREVERSE, dl, MVT::v8i16, Conv);
      return DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, ReveHWord);
    } else if (PPC::isXXBRWShuffleMask(SVOp)) {
      SDValue Conv = DAG.getNode(ISD::BITCAST, dl, MVT::v4i32, V1);
      SDValue ReveWord = DAG.getNode(PPCISD::XXREVERSE, dl, MVT::v4i32, Conv);
      return DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, ReveWord);
    } else if (PPC::isXXBRDShuffleMask(SVOp)) {
      SDValue Conv = DAG.getNode(ISD::BITCAST, dl, MVT::v2i64, V1);
      SDValue ReveDWord = DAG.getNode(PPCISD::XXREVERSE, dl, MVT::v2i64, Conv);
      return DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, ReveDWord);
    } else if (PPC::isXXBRQShuffleMask(SVOp)) {
      SDValue Conv = DAG.getNode(ISD::BITCAST, dl, MVT::v1i128, V1);
      SDValue ReveQWord = DAG.getNode(PPCISD::XXREVERSE, dl, MVT::v1i128, Conv);
      return DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, ReveQWord);
    }
  }

  if (Subtarget.hasVSX()) {
    if (V2.isUndef() && PPC::isSplatShuffleMask(SVOp, 4)) {
      int SplatIdx = PPC::getVSPLTImmediate(SVOp, 4, DAG);

      SDValue Conv = DAG.getNode(ISD::BITCAST, dl, MVT::v4i32, V1);
      SDValue Splat = DAG.getNode(PPCISD::XXSPLT, dl, MVT::v4i32, Conv,
                                  DAG.getConstant(SplatIdx, dl, MVT::i32));
      return DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, Splat);
    }

    // Left shifts of 8 bytes are actually swaps. Convert accordingly.
    if (V2.isUndef() && PPC::isVSLDOIShuffleMask(SVOp, 1, DAG) == 8) {
      SDValue Conv = DAG.getNode(ISD::BITCAST, dl, MVT::v2f64, V1);
      SDValue Swap = DAG.getNode(PPCISD::SWAP_NO_CHAIN, dl, MVT::v2f64, Conv);
      return DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, Swap);
    }
  }

  if (Subtarget.hasQPX()) {
    if (VT.getVectorNumElements() != 4)
      return SDValue();

    if (V2.isUndef()) V2 = V1;

    int AlignIdx = PPC::isQVALIGNIShuffleMask(SVOp);
    if (AlignIdx != -1) {
      return DAG.getNode(PPCISD::QVALIGNI, dl, VT, V1, V2,
                         DAG.getConstant(AlignIdx, dl, MVT::i32));
    } else if (SVOp->isSplat()) {
      int SplatIdx = SVOp->getSplatIndex();
      if (SplatIdx >= 4) {
        std::swap(V1, V2);
        SplatIdx -= 4;
      }

      return DAG.getNode(PPCISD::QVESPLATI, dl, VT, V1,
                         DAG.getConstant(SplatIdx, dl, MVT::i32));
    }

    // Lower this into a qvgpci/qvfperm pair.

    // Compute the qvgpci literal
    unsigned idx = 0;
    for (unsigned i = 0; i < 4; ++i) {
      int m = SVOp->getMaskElt(i);
      unsigned mm = m >= 0 ? (unsigned) m : i;
      idx |= mm << (3-i)*3;
    }

    SDValue V3 = DAG.getNode(PPCISD::QVGPCI, dl, MVT::v4f64,
                             DAG.getConstant(idx, dl, MVT::i32));
    return DAG.getNode(PPCISD::QVFPERM, dl, VT, V1, V2, V3);
  }

  // Cases that are handled by instructions that take permute immediates
  // (such as vsplt*) should be left as VECTOR_SHUFFLE nodes so they can be
  // selected by the instruction selector.
  if (V2.isUndef()) {
    if (PPC::isSplatShuffleMask(SVOp, 1) ||
        PPC::isSplatShuffleMask(SVOp, 2) ||
        PPC::isSplatShuffleMask(SVOp, 4) ||
        PPC::isVPKUWUMShuffleMask(SVOp, 1, DAG) ||
        PPC::isVPKUHUMShuffleMask(SVOp, 1, DAG) ||
        PPC::isVSLDOIShuffleMask(SVOp, 1, DAG) != -1 ||
        PPC::isVMRGLShuffleMask(SVOp, 1, 1, DAG) ||
        PPC::isVMRGLShuffleMask(SVOp, 2, 1, DAG) ||
        PPC::isVMRGLShuffleMask(SVOp, 4, 1, DAG) ||
        PPC::isVMRGHShuffleMask(SVOp, 1, 1, DAG) ||
        PPC::isVMRGHShuffleMask(SVOp, 2, 1, DAG) ||
        PPC::isVMRGHShuffleMask(SVOp, 4, 1, DAG) ||
        (Subtarget.hasP8Altivec() && (
         PPC::isVPKUDUMShuffleMask(SVOp, 1, DAG) ||
         PPC::isVMRGEOShuffleMask(SVOp, true, 1, DAG) ||
         PPC::isVMRGEOShuffleMask(SVOp, false, 1, DAG)))) {
      return Op;
    }
  }

  // Altivec has a variety of "shuffle immediates" that take two vector inputs
  // and produce a fixed permutation.  If any of these match, do not lower to
  // VPERM.
  unsigned int ShuffleKind = isLittleEndian ? 2 : 0;
  if (PPC::isVPKUWUMShuffleMask(SVOp, ShuffleKind, DAG) ||
      PPC::isVPKUHUMShuffleMask(SVOp, ShuffleKind, DAG) ||
      PPC::isVSLDOIShuffleMask(SVOp, ShuffleKind, DAG) != -1 ||
      PPC::isVMRGLShuffleMask(SVOp, 1, ShuffleKind, DAG) ||
      PPC::isVMRGLShuffleMask(SVOp, 2, ShuffleKind, DAG) ||
      PPC::isVMRGLShuffleMask(SVOp, 4, ShuffleKind, DAG) ||
      PPC::isVMRGHShuffleMask(SVOp, 1, ShuffleKind, DAG) ||
      PPC::isVMRGHShuffleMask(SVOp, 2, ShuffleKind, DAG) ||
      PPC::isVMRGHShuffleMask(SVOp, 4, ShuffleKind, DAG) ||
      (Subtarget.hasP8Altivec() && (
       PPC::isVPKUDUMShuffleMask(SVOp, ShuffleKind, DAG) ||
       PPC::isVMRGEOShuffleMask(SVOp, true, ShuffleKind, DAG) ||
       PPC::isVMRGEOShuffleMask(SVOp, false, ShuffleKind, DAG))))
    return Op;

  // Check to see if this is a shuffle of 4-byte values.  If so, we can use our
  // perfect shuffle table to emit an optimal matching sequence.
  ArrayRef<int> PermMask = SVOp->getMask();

  unsigned PFIndexes[4];
  bool isFourElementShuffle = true;
  for (unsigned i = 0; i != 4 && isFourElementShuffle; ++i) { // Element number
    unsigned EltNo = 8;   // Start out undef.
    for (unsigned j = 0; j != 4; ++j) {  // Intra-element byte.
      if (PermMask[i*4+j] < 0)
        continue;   // Undef, ignore it.

      unsigned ByteSource = PermMask[i*4+j];
      if ((ByteSource & 3) != j) {
        isFourElementShuffle = false;
        break;
      }

      if (EltNo == 8) {
        EltNo = ByteSource/4;
      } else if (EltNo != ByteSource/4) {
        isFourElementShuffle = false;
        break;
      }
    }
    PFIndexes[i] = EltNo;
  }

  // If this shuffle can be expressed as a shuffle of 4-byte elements, use the
  // perfect shuffle vector to determine if it is cost effective to do this as
  // discrete instructions, or whether we should use a vperm.
  // For now, we skip this for little endian until such time as we have a
  // little-endian perfect shuffle table.
  if (isFourElementShuffle && !isLittleEndian) {
    // Compute the index in the perfect shuffle table.
    unsigned PFTableIndex =
      PFIndexes[0]*9*9*9+PFIndexes[1]*9*9+PFIndexes[2]*9+PFIndexes[3];

    unsigned PFEntry = PerfectShuffleTable[PFTableIndex];
    unsigned Cost  = (PFEntry >> 30);

    // Determining when to avoid vperm is tricky.  Many things affect the cost
    // of vperm, particularly how many times the perm mask needs to be computed.
    // For example, if the perm mask can be hoisted out of a loop or is already
    // used (perhaps because there are multiple permutes with the same shuffle
    // mask?) the vperm has a cost of 1.  OTOH, hoisting the permute mask out of
    // the loop requires an extra register.
    //
    // As a compromise, we only emit discrete instructions if the shuffle can be
    // generated in 3 or fewer operations.  When we have loop information
    // available, if this block is within a loop, we should avoid using vperm
    // for 3-operation perms and use a constant pool load instead.
    if (Cost < 3)
      return GeneratePerfectShuffle(PFEntry, V1, V2, DAG, dl);
  }

  // Lower this to a VPERM(V1, V2, V3) expression, where V3 is a constant
  // vector that will get spilled to the constant pool.
  if (V2.isUndef()) V2 = V1;

  // The SHUFFLE_VECTOR mask is almost exactly what we want for vperm, except
  // that it is in input element units, not in bytes.  Convert now.

  // For little endian, the order of the input vectors is reversed, and
  // the permutation mask is complemented with respect to 31.  This is
  // necessary to produce proper semantics with the big-endian-biased vperm
  // instruction.
  EVT EltVT = V1.getValueType().getVectorElementType();
  unsigned BytesPerElement = EltVT.getSizeInBits()/8;

  SmallVector<SDValue, 16> ResultMask;
  for (unsigned i = 0, e = VT.getVectorNumElements(); i != e; ++i) {
    unsigned SrcElt = PermMask[i] < 0 ? 0 : PermMask[i];

    for (unsigned j = 0; j != BytesPerElement; ++j)
      if (isLittleEndian)
        ResultMask.push_back(DAG.getConstant(31 - (SrcElt*BytesPerElement + j),
                                             dl, MVT::i32));
      else
        ResultMask.push_back(DAG.getConstant(SrcElt*BytesPerElement + j, dl,
                                             MVT::i32));
  }

  SDValue VPermMask = DAG.getBuildVector(MVT::v16i8, dl, ResultMask);
  if (isLittleEndian)
    return DAG.getNode(PPCISD::VPERM, dl, V1.getValueType(),
                       V2, V1, VPermMask);
  else
    return DAG.getNode(PPCISD::VPERM, dl, V1.getValueType(),
                       V1, V2, VPermMask);
}

/// getVectorCompareInfo - Given an intrinsic, return false if it is not a
/// vector comparison.  If it is, return true and fill in Opc/isDot with
/// information about the intrinsic.
static bool getVectorCompareInfo(SDValue Intrin, int &CompareOpc,
                                 bool &isDot, const PPCSubtarget &Subtarget) {
  unsigned IntrinsicID =
      cast<ConstantSDNode>(Intrin.getOperand(0))->getZExtValue();
  CompareOpc = -1;
  isDot = false;
  switch (IntrinsicID) {
  default:
    return false;
  // Comparison predicates.
  case Intrinsic::ppc_altivec_vcmpbfp_p:
    CompareOpc = 966;
    isDot = true;
    break;
  case Intrinsic::ppc_altivec_vcmpeqfp_p:
    CompareOpc = 198;
    isDot = true;
    break;
  case Intrinsic::ppc_altivec_vcmpequb_p:
    CompareOpc = 6;
    isDot = true;
    break;
  case Intrinsic::ppc_altivec_vcmpequh_p:
    CompareOpc = 70;
    isDot = true;
    break;
  case Intrinsic::ppc_altivec_vcmpequw_p:
    CompareOpc = 134;
    isDot = true;
    break;
  case Intrinsic::ppc_altivec_vcmpequd_p:
    if (Subtarget.hasP8Altivec()) {
      CompareOpc = 199;
      isDot = true;
    } else
      return false;
    break;
  case Intrinsic::ppc_altivec_vcmpneb_p:
  case Intrinsic::ppc_altivec_vcmpneh_p:
  case Intrinsic::ppc_altivec_vcmpnew_p:
  case Intrinsic::ppc_altivec_vcmpnezb_p:
  case Intrinsic::ppc_altivec_vcmpnezh_p:
  case Intrinsic::ppc_altivec_vcmpnezw_p:
    if (Subtarget.hasP9Altivec()) {
      switch (IntrinsicID) {
      default:
        llvm_unreachable("Unknown comparison intrinsic.");
      case Intrinsic::ppc_altivec_vcmpneb_p:
        CompareOpc = 7;
        break;
      case Intrinsic::ppc_altivec_vcmpneh_p:
        CompareOpc = 71;
        break;
      case Intrinsic::ppc_altivec_vcmpnew_p:
        CompareOpc = 135;
        break;
      case Intrinsic::ppc_altivec_vcmpnezb_p:
        CompareOpc = 263;
        break;
      case Intrinsic::ppc_altivec_vcmpnezh_p:
        CompareOpc = 327;
        break;
      case Intrinsic::ppc_altivec_vcmpnezw_p:
        CompareOpc = 391;
        break;
      }
      isDot = true;
    } else
      return false;
    break;
  case Intrinsic::ppc_altivec_vcmpgefp_p:
    CompareOpc = 454;
    isDot = true;
    break;
  case Intrinsic::ppc_altivec_vcmpgtfp_p:
    CompareOpc = 710;
    isDot = true;
    break;
  case Intrinsic::ppc_altivec_vcmpgtsb_p:
    CompareOpc = 774;
    isDot = true;
    break;
  case Intrinsic::ppc_altivec_vcmpgtsh_p:
    CompareOpc = 838;
    isDot = true;
    break;
  case Intrinsic::ppc_altivec_vcmpgtsw_p:
    CompareOpc = 902;
    isDot = true;
    break;
  case Intrinsic::ppc_altivec_vcmpgtsd_p:
    if (Subtarget.hasP8Altivec()) {
      CompareOpc = 967;
      isDot = true;
    } else
      return false;
    break;
  case Intrinsic::ppc_altivec_vcmpgtub_p:
    CompareOpc = 518;
    isDot = true;
    break;
  case Intrinsic::ppc_altivec_vcmpgtuh_p:
    CompareOpc = 582;
    isDot = true;
    break;
  case Intrinsic::ppc_altivec_vcmpgtuw_p:
    CompareOpc = 646;
    isDot = true;
    break;
  case Intrinsic::ppc_altivec_vcmpgtud_p:
    if (Subtarget.hasP8Altivec()) {
      CompareOpc = 711;
      isDot = true;
    } else
      return false;
    break;

  // VSX predicate comparisons use the same infrastructure
  case Intrinsic::ppc_vsx_xvcmpeqdp_p:
  case Intrinsic::ppc_vsx_xvcmpgedp_p:
  case Intrinsic::ppc_vsx_xvcmpgtdp_p:
  case Intrinsic::ppc_vsx_xvcmpeqsp_p:
  case Intrinsic::ppc_vsx_xvcmpgesp_p:
  case Intrinsic::ppc_vsx_xvcmpgtsp_p:
    if (Subtarget.hasVSX()) {
      switch (IntrinsicID) {
      case Intrinsic::ppc_vsx_xvcmpeqdp_p:
        CompareOpc = 99;
        break;
      case Intrinsic::ppc_vsx_xvcmpgedp_p:
        CompareOpc = 115;
        break;
      case Intrinsic::ppc_vsx_xvcmpgtdp_p:
        CompareOpc = 107;
        break;
      case Intrinsic::ppc_vsx_xvcmpeqsp_p:
        CompareOpc = 67;
        break;
      case Intrinsic::ppc_vsx_xvcmpgesp_p:
        CompareOpc = 83;
        break;
      case Intrinsic::ppc_vsx_xvcmpgtsp_p:
        CompareOpc = 75;
        break;
      }
      isDot = true;
    } else
      return false;
    break;

  // Normal Comparisons.
  case Intrinsic::ppc_altivec_vcmpbfp:
    CompareOpc = 966;
    break;
  case Intrinsic::ppc_altivec_vcmpeqfp:
    CompareOpc = 198;
    break;
  case Intrinsic::ppc_altivec_vcmpequb:
    CompareOpc = 6;
    break;
  case Intrinsic::ppc_altivec_vcmpequh:
    CompareOpc = 70;
    break;
  case Intrinsic::ppc_altivec_vcmpequw:
    CompareOpc = 134;
    break;
  case Intrinsic::ppc_altivec_vcmpequd:
    if (Subtarget.hasP8Altivec())
      CompareOpc = 199;
    else
      return false;
    break;
  case Intrinsic::ppc_altivec_vcmpneb:
  case Intrinsic::ppc_altivec_vcmpneh:
  case Intrinsic::ppc_altivec_vcmpnew:
  case Intrinsic::ppc_altivec_vcmpnezb:
  case Intrinsic::ppc_altivec_vcmpnezh:
  case Intrinsic::ppc_altivec_vcmpnezw:
    if (Subtarget.hasP9Altivec())
      switch (IntrinsicID) {
      default:
        llvm_unreachable("Unknown comparison intrinsic.");
      case Intrinsic::ppc_altivec_vcmpneb:
        CompareOpc = 7;
        break;
      case Intrinsic::ppc_altivec_vcmpneh:
        CompareOpc = 71;
        break;
      case Intrinsic::ppc_altivec_vcmpnew:
        CompareOpc = 135;
        break;
      case Intrinsic::ppc_altivec_vcmpnezb:
        CompareOpc = 263;
        break;
      case Intrinsic::ppc_altivec_vcmpnezh:
        CompareOpc = 327;
        break;
      case Intrinsic::ppc_altivec_vcmpnezw:
        CompareOpc = 391;
        break;
      }
    else
      return false;
    break;
  case Intrinsic::ppc_altivec_vcmpgefp:
    CompareOpc = 454;
    break;
  case Intrinsic::ppc_altivec_vcmpgtfp:
    CompareOpc = 710;
    break;
  case Intrinsic::ppc_altivec_vcmpgtsb:
    CompareOpc = 774;
    break;
  case Intrinsic::ppc_altivec_vcmpgtsh:
    CompareOpc = 838;
    break;
  case Intrinsic::ppc_altivec_vcmpgtsw:
    CompareOpc = 902;
    break;
  case Intrinsic::ppc_altivec_vcmpgtsd:
    if (Subtarget.hasP8Altivec())
      CompareOpc = 967;
    else
      return false;
    break;
  case Intrinsic::ppc_altivec_vcmpgtub:
    CompareOpc = 518;
    break;
  case Intrinsic::ppc_altivec_vcmpgtuh:
    CompareOpc = 582;
    break;
  case Intrinsic::ppc_altivec_vcmpgtuw:
    CompareOpc = 646;
    break;
  case Intrinsic::ppc_altivec_vcmpgtud:
    if (Subtarget.hasP8Altivec())
      CompareOpc = 711;
    else
      return false;
    break;
  }
  return true;
}

/// LowerINTRINSIC_WO_CHAIN - If this is an intrinsic that we want to custom
/// lower, do it, otherwise return null.
SDValue PPCTargetLowering::LowerINTRINSIC_WO_CHAIN(SDValue Op,
                                                   SelectionDAG &DAG) const {
  unsigned IntrinsicID =
    cast<ConstantSDNode>(Op.getOperand(0))->getZExtValue();

  SDLoc dl(Op);

  if (IntrinsicID == Intrinsic::thread_pointer) {
    // Reads the thread pointer register, used for __builtin_thread_pointer.
    if (Subtarget.isPPC64())
      return DAG.getRegister(PPC::X13, MVT::i64);
    return DAG.getRegister(PPC::R2, MVT::i32);
  }

  // If this is a lowered altivec predicate compare, CompareOpc is set to the
  // opcode number of the comparison.
  int CompareOpc;
  bool isDot;
  if (!getVectorCompareInfo(Op, CompareOpc, isDot, Subtarget))
    return SDValue();    // Don't custom lower most intrinsics.

  // If this is a non-dot comparison, make the VCMP node and we are done.
  if (!isDot) {
    SDValue Tmp = DAG.getNode(PPCISD::VCMP, dl, Op.getOperand(2).getValueType(),
                              Op.getOperand(1), Op.getOperand(2),
                              DAG.getConstant(CompareOpc, dl, MVT::i32));
    return DAG.getNode(ISD::BITCAST, dl, Op.getValueType(), Tmp);
  }

  // Create the PPCISD altivec 'dot' comparison node.
  SDValue Ops[] = {
    Op.getOperand(2),  // LHS
    Op.getOperand(3),  // RHS
    DAG.getConstant(CompareOpc, dl, MVT::i32)
  };
  EVT VTs[] = { Op.getOperand(2).getValueType(), MVT::Glue };
  SDValue CompNode = DAG.getNode(PPCISD::VCMPo, dl, VTs, Ops);

  // Now that we have the comparison, emit a copy from the CR to a GPR.
  // This is flagged to the above dot comparison.
  SDValue Flags = DAG.getNode(PPCISD::MFOCRF, dl, MVT::i32,
                                DAG.getRegister(PPC::CR6, MVT::i32),
                                CompNode.getValue(1));

  // Unpack the result based on how the target uses it.
  unsigned BitNo;   // Bit # of CR6.
  bool InvertBit;   // Invert result?
  switch (cast<ConstantSDNode>(Op.getOperand(1))->getZExtValue()) {
  default:  // Can't happen, don't crash on invalid number though.
  case 0:   // Return the value of the EQ bit of CR6.
    BitNo = 0; InvertBit = false;
    break;
  case 1:   // Return the inverted value of the EQ bit of CR6.
    BitNo = 0; InvertBit = true;
    break;
  case 2:   // Return the value of the LT bit of CR6.
    BitNo = 2; InvertBit = false;
    break;
  case 3:   // Return the inverted value of the LT bit of CR6.
    BitNo = 2; InvertBit = true;
    break;
  }

  // Shift the bit into the low position.
  Flags = DAG.getNode(ISD::SRL, dl, MVT::i32, Flags,
                      DAG.getConstant(8 - (3 - BitNo), dl, MVT::i32));
  // Isolate the bit.
  Flags = DAG.getNode(ISD::AND, dl, MVT::i32, Flags,
                      DAG.getConstant(1, dl, MVT::i32));

  // If we are supposed to, toggle the bit.
  if (InvertBit)
    Flags = DAG.getNode(ISD::XOR, dl, MVT::i32, Flags,
                        DAG.getConstant(1, dl, MVT::i32));
  return Flags;
}

SDValue PPCTargetLowering::LowerINTRINSIC_VOID(SDValue Op,
                                               SelectionDAG &DAG) const {
  // SelectionDAGBuilder::visitTargetIntrinsic may insert one extra chain to
  // the beginning of the argument list.
  int ArgStart = isa<ConstantSDNode>(Op.getOperand(0)) ? 0 : 1;
  SDLoc DL(Op);
  switch (cast<ConstantSDNode>(Op.getOperand(ArgStart))->getZExtValue()) {
  case Intrinsic::ppc_cfence: {
    assert(ArgStart == 1 && "llvm.ppc.cfence must carry a chain argument.");
    assert(Subtarget.isPPC64() && "Only 64-bit is supported for now.");
    return SDValue(DAG.getMachineNode(PPC::CFENCE8, DL, MVT::Other,
                                      DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64,
                                                  Op.getOperand(ArgStart + 1)),
                                      Op.getOperand(0)),
                   0);
  }
  default:
    break;
  }
  return SDValue();
}

SDValue PPCTargetLowering::LowerREM(SDValue Op, SelectionDAG &DAG) const {
  // Check for a DIV with the same operands as this REM.
  for (auto UI : Op.getOperand(1)->uses()) {
    if ((Op.getOpcode() == ISD::SREM && UI->getOpcode() == ISD::SDIV) ||
        (Op.getOpcode() == ISD::UREM && UI->getOpcode() == ISD::UDIV))
      if (UI->getOperand(0) == Op.getOperand(0) &&
          UI->getOperand(1) == Op.getOperand(1))
        return SDValue();
  }
  return Op;
}

// Lower scalar BSWAP64 to xxbrd.
SDValue PPCTargetLowering::LowerBSWAP(SDValue Op, SelectionDAG &DAG) const {
  SDLoc dl(Op);
  // MTVSRDD
  Op = DAG.getNode(ISD::BUILD_VECTOR, dl, MVT::v2i64, Op.getOperand(0),
                   Op.getOperand(0));
  // XXBRD
  Op = DAG.getNode(PPCISD::XXREVERSE, dl, MVT::v2i64, Op);
  // MFVSRD
  int VectorIndex = 0;
  if (Subtarget.isLittleEndian())
    VectorIndex = 1;
  Op = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, MVT::i64, Op,
                   DAG.getTargetConstant(VectorIndex, dl, MVT::i32));
  return Op;
}

// ATOMIC_CMP_SWAP for i8/i16 needs to zero-extend its input since it will be
// compared to a value that is atomically loaded (atomic loads zero-extend).
SDValue PPCTargetLowering::LowerATOMIC_CMP_SWAP(SDValue Op,
                                                SelectionDAG &DAG) const {
  assert(Op.getOpcode() == ISD::ATOMIC_CMP_SWAP &&
         "Expecting an atomic compare-and-swap here.");
  SDLoc dl(Op);
  auto *AtomicNode = cast<AtomicSDNode>(Op.getNode());
  EVT MemVT = AtomicNode->getMemoryVT();
  if (MemVT.getSizeInBits() >= 32)
    return Op;

  SDValue CmpOp = Op.getOperand(2);
  // If this is already correctly zero-extended, leave it alone.
  auto HighBits = APInt::getHighBitsSet(32, 32 - MemVT.getSizeInBits());
  if (DAG.MaskedValueIsZero(CmpOp, HighBits))
    return Op;

  // Clear the high bits of the compare operand.
  unsigned MaskVal = (1 << MemVT.getSizeInBits()) - 1;
  SDValue NewCmpOp =
    DAG.getNode(ISD::AND, dl, MVT::i32, CmpOp,
                DAG.getConstant(MaskVal, dl, MVT::i32));

  // Replace the existing compare operand with the properly zero-extended one.
  SmallVector<SDValue, 4> Ops;
  for (int i = 0, e = AtomicNode->getNumOperands(); i < e; i++)
    Ops.push_back(AtomicNode->getOperand(i));
  Ops[2] = NewCmpOp;
  MachineMemOperand *MMO = AtomicNode->getMemOperand();
  SDVTList Tys = DAG.getVTList(MVT::i32, MVT::Other);
  auto NodeTy =
    (MemVT == MVT::i8) ? PPCISD::ATOMIC_CMP_SWAP_8 : PPCISD::ATOMIC_CMP_SWAP_16;
  return DAG.getMemIntrinsicNode(NodeTy, dl, Tys, Ops, MemVT, MMO);
}

SDValue PPCTargetLowering::LowerSCALAR_TO_VECTOR(SDValue Op,
                                                 SelectionDAG &DAG) const {
  SDLoc dl(Op);
  // Create a stack slot that is 16-byte aligned.
  MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
  int FrameIdx = MFI.CreateStackObject(16, 16, false);
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  SDValue FIdx = DAG.getFrameIndex(FrameIdx, PtrVT);

  // Store the input value into Value#0 of the stack slot.
  SDValue Store = DAG.getStore(DAG.getEntryNode(), dl, Op.getOperand(0), FIdx,
                               MachinePointerInfo());
  // Load it out.
  return DAG.getLoad(Op.getValueType(), dl, Store, FIdx, MachinePointerInfo());
}

SDValue PPCTargetLowering::LowerINSERT_VECTOR_ELT(SDValue Op,
                                                  SelectionDAG &DAG) const {
  assert(Op.getOpcode() == ISD::INSERT_VECTOR_ELT &&
         "Should only be called for ISD::INSERT_VECTOR_ELT");

  ConstantSDNode *C = dyn_cast<ConstantSDNode>(Op.getOperand(2));
  // We have legal lowering for constant indices but not for variable ones.
  if (!C)
    return SDValue();

  EVT VT = Op.getValueType();
  SDLoc dl(Op);
  SDValue V1 = Op.getOperand(0);
  SDValue V2 = Op.getOperand(1);
  // We can use MTVSRZ + VECINSERT for v8i16 and v16i8 types.
  if (VT == MVT::v8i16 || VT == MVT::v16i8) {
    SDValue Mtvsrz = DAG.getNode(PPCISD::MTVSRZ, dl, VT, V2);
    unsigned BytesInEachElement = VT.getVectorElementType().getSizeInBits() / 8;
    unsigned InsertAtElement = C->getZExtValue();
    unsigned InsertAtByte = InsertAtElement * BytesInEachElement;
    if (Subtarget.isLittleEndian()) {
      InsertAtByte = (16 - BytesInEachElement) - InsertAtByte;
    }
    return DAG.getNode(PPCISD::VECINSERT, dl, VT, V1, Mtvsrz,
                       DAG.getConstant(InsertAtByte, dl, MVT::i32));
  }
  return Op;
}

SDValue PPCTargetLowering::LowerEXTRACT_VECTOR_ELT(SDValue Op,
                                                   SelectionDAG &DAG) const {
  SDLoc dl(Op);
  SDNode *N = Op.getNode();

  assert(N->getOperand(0).getValueType() == MVT::v4i1 &&
         "Unknown extract_vector_elt type");

  SDValue Value = N->getOperand(0);

  // The first part of this is like the store lowering except that we don't
  // need to track the chain.

  // The values are now known to be -1 (false) or 1 (true). To convert this
  // into 0 (false) and 1 (true), add 1 and then divide by 2 (multiply by 0.5).
  // This can be done with an fma and the 0.5 constant: (V+1.0)*0.5 = 0.5*V+0.5
  Value = DAG.getNode(PPCISD::QBFLT, dl, MVT::v4f64, Value);

  // FIXME: We can make this an f32 vector, but the BUILD_VECTOR code needs to
  // understand how to form the extending load.
  SDValue FPHalfs = DAG.getConstantFP(0.5, dl, MVT::v4f64);

  Value = DAG.getNode(ISD::FMA, dl, MVT::v4f64, Value, FPHalfs, FPHalfs);

  // Now convert to an integer and store.
  Value = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, MVT::v4f64,
    DAG.getConstant(Intrinsic::ppc_qpx_qvfctiwu, dl, MVT::i32),
    Value);

  MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
  int FrameIdx = MFI.CreateStackObject(16, 16, false);
  MachinePointerInfo PtrInfo =
      MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FrameIdx);
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  SDValue FIdx = DAG.getFrameIndex(FrameIdx, PtrVT);

  SDValue StoreChain = DAG.getEntryNode();
  SDValue Ops[] = {StoreChain,
                   DAG.getConstant(Intrinsic::ppc_qpx_qvstfiw, dl, MVT::i32),
                   Value, FIdx};
  SDVTList VTs = DAG.getVTList(/*chain*/ MVT::Other);

  StoreChain = DAG.getMemIntrinsicNode(ISD::INTRINSIC_VOID,
    dl, VTs, Ops, MVT::v4i32, PtrInfo);

  // Extract the value requested.
  unsigned Offset = 4*cast<ConstantSDNode>(N->getOperand(1))->getZExtValue();
  SDValue Idx = DAG.getConstant(Offset, dl, FIdx.getValueType());
  Idx = DAG.getNode(ISD::ADD, dl, FIdx.getValueType(), FIdx, Idx);

  SDValue IntVal =
      DAG.getLoad(MVT::i32, dl, StoreChain, Idx, PtrInfo.getWithOffset(Offset));

  if (!Subtarget.useCRBits())
    return IntVal;

  return DAG.getNode(ISD::TRUNCATE, dl, MVT::i1, IntVal);
}

/// Lowering for QPX v4i1 loads
SDValue PPCTargetLowering::LowerVectorLoad(SDValue Op,
                                           SelectionDAG &DAG) const {
  SDLoc dl(Op);
  LoadSDNode *LN = cast<LoadSDNode>(Op.getNode());
  SDValue LoadChain = LN->getChain();
  SDValue BasePtr = LN->getBasePtr();

  if (Op.getValueType() == MVT::v4f64 ||
      Op.getValueType() == MVT::v4f32) {
    EVT MemVT = LN->getMemoryVT();
    unsigned Alignment = LN->getAlignment();

    // If this load is properly aligned, then it is legal.
    if (Alignment >= MemVT.getStoreSize())
      return Op;

    EVT ScalarVT = Op.getValueType().getScalarType(),
        ScalarMemVT = MemVT.getScalarType();
    unsigned Stride = ScalarMemVT.getStoreSize();

    SDValue Vals[4], LoadChains[4];
    for (unsigned Idx = 0; Idx < 4; ++Idx) {
      SDValue Load;
      if (ScalarVT != ScalarMemVT)
        Load = DAG.getExtLoad(LN->getExtensionType(), dl, ScalarVT, LoadChain,
                              BasePtr,
                              LN->getPointerInfo().getWithOffset(Idx * Stride),
                              ScalarMemVT, MinAlign(Alignment, Idx * Stride),
                              LN->getMemOperand()->getFlags(), LN->getAAInfo());
      else
        Load = DAG.getLoad(ScalarVT, dl, LoadChain, BasePtr,
                           LN->getPointerInfo().getWithOffset(Idx * Stride),
                           MinAlign(Alignment, Idx * Stride),
                           LN->getMemOperand()->getFlags(), LN->getAAInfo());

      if (Idx == 0 && LN->isIndexed()) {
        assert(LN->getAddressingMode() == ISD::PRE_INC &&
               "Unknown addressing mode on vector load");
        Load = DAG.getIndexedLoad(Load, dl, BasePtr, LN->getOffset(),
                                  LN->getAddressingMode());
      }

      Vals[Idx] = Load;
      LoadChains[Idx] = Load.getValue(1);

      BasePtr = DAG.getNode(ISD::ADD, dl, BasePtr.getValueType(), BasePtr,
                            DAG.getConstant(Stride, dl,
                                            BasePtr.getValueType()));
    }

    SDValue TF =  DAG.getNode(ISD::TokenFactor, dl, MVT::Other, LoadChains);
    SDValue Value = DAG.getBuildVector(Op.getValueType(), dl, Vals);

    if (LN->isIndexed()) {
      SDValue RetOps[] = { Value, Vals[0].getValue(1), TF };
      return DAG.getMergeValues(RetOps, dl);
    }

    SDValue RetOps[] = { Value, TF };
    return DAG.getMergeValues(RetOps, dl);
  }

  assert(Op.getValueType() == MVT::v4i1 && "Unknown load to lower");
  assert(LN->isUnindexed() && "Indexed v4i1 loads are not supported");

  // To lower v4i1 from a byte array, we load the byte elements of the
  // vector and then reuse the BUILD_VECTOR logic.

  SDValue VectElmts[4], VectElmtChains[4];
  for (unsigned i = 0; i < 4; ++i) {
    SDValue Idx = DAG.getConstant(i, dl, BasePtr.getValueType());
    Idx = DAG.getNode(ISD::ADD, dl, BasePtr.getValueType(), BasePtr, Idx);

    VectElmts[i] = DAG.getExtLoad(
        ISD::EXTLOAD, dl, MVT::i32, LoadChain, Idx,
        LN->getPointerInfo().getWithOffset(i), MVT::i8,
        /* Alignment = */ 1, LN->getMemOperand()->getFlags(), LN->getAAInfo());
    VectElmtChains[i] = VectElmts[i].getValue(1);
  }

  LoadChain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, VectElmtChains);
  SDValue Value = DAG.getBuildVector(MVT::v4i1, dl, VectElmts);

  SDValue RVals[] = { Value, LoadChain };
  return DAG.getMergeValues(RVals, dl);
}

/// Lowering for QPX v4i1 stores
SDValue PPCTargetLowering::LowerVectorStore(SDValue Op,
                                            SelectionDAG &DAG) const {
  SDLoc dl(Op);
  StoreSDNode *SN = cast<StoreSDNode>(Op.getNode());
  SDValue StoreChain = SN->getChain();
  SDValue BasePtr = SN->getBasePtr();
  SDValue Value = SN->getValue();

  if (Value.getValueType() == MVT::v4f64 ||
      Value.getValueType() == MVT::v4f32) {
    EVT MemVT = SN->getMemoryVT();
    unsigned Alignment = SN->getAlignment();

    // If this store is properly aligned, then it is legal.
    if (Alignment >= MemVT.getStoreSize())
      return Op;

    EVT ScalarVT = Value.getValueType().getScalarType(),
        ScalarMemVT = MemVT.getScalarType();
    unsigned Stride = ScalarMemVT.getStoreSize();

    SDValue Stores[4];
    for (unsigned Idx = 0; Idx < 4; ++Idx) {
      SDValue Ex = DAG.getNode(
          ISD::EXTRACT_VECTOR_ELT, dl, ScalarVT, Value,
          DAG.getConstant(Idx, dl, getVectorIdxTy(DAG.getDataLayout())));
      SDValue Store;
      if (ScalarVT != ScalarMemVT)
        Store =
            DAG.getTruncStore(StoreChain, dl, Ex, BasePtr,
                              SN->getPointerInfo().getWithOffset(Idx * Stride),
                              ScalarMemVT, MinAlign(Alignment, Idx * Stride),
                              SN->getMemOperand()->getFlags(), SN->getAAInfo());
      else
        Store = DAG.getStore(StoreChain, dl, Ex, BasePtr,
                             SN->getPointerInfo().getWithOffset(Idx * Stride),
                             MinAlign(Alignment, Idx * Stride),
                             SN->getMemOperand()->getFlags(), SN->getAAInfo());

      if (Idx == 0 && SN->isIndexed()) {
        assert(SN->getAddressingMode() == ISD::PRE_INC &&
               "Unknown addressing mode on vector store");
        Store = DAG.getIndexedStore(Store, dl, BasePtr, SN->getOffset(),
                                    SN->getAddressingMode());
      }

      BasePtr = DAG.getNode(ISD::ADD, dl, BasePtr.getValueType(), BasePtr,
                            DAG.getConstant(Stride, dl,
                                            BasePtr.getValueType()));
      Stores[Idx] = Store;
    }

    SDValue TF =  DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Stores);

    if (SN->isIndexed()) {
      SDValue RetOps[] = { TF, Stores[0].getValue(1) };
      return DAG.getMergeValues(RetOps, dl);
    }

    return TF;
  }

  assert(SN->isUnindexed() && "Indexed v4i1 stores are not supported");
  assert(Value.getValueType() == MVT::v4i1 && "Unknown store to lower");

  // The values are now known to be -1 (false) or 1 (true). To convert this
  // into 0 (false) and 1 (true), add 1 and then divide by 2 (multiply by 0.5).
  // This can be done with an fma and the 0.5 constant: (V+1.0)*0.5 = 0.5*V+0.5
  Value = DAG.getNode(PPCISD::QBFLT, dl, MVT::v4f64, Value);

  // FIXME: We can make this an f32 vector, but the BUILD_VECTOR code needs to
  // understand how to form the extending load.
  SDValue FPHalfs = DAG.getConstantFP(0.5, dl, MVT::v4f64);

  Value = DAG.getNode(ISD::FMA, dl, MVT::v4f64, Value, FPHalfs, FPHalfs);

  // Now convert to an integer and store.
  Value = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, MVT::v4f64,
    DAG.getConstant(Intrinsic::ppc_qpx_qvfctiwu, dl, MVT::i32),
    Value);

  MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
  int FrameIdx = MFI.CreateStackObject(16, 16, false);
  MachinePointerInfo PtrInfo =
      MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FrameIdx);
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  SDValue FIdx = DAG.getFrameIndex(FrameIdx, PtrVT);

  SDValue Ops[] = {StoreChain,
                   DAG.getConstant(Intrinsic::ppc_qpx_qvstfiw, dl, MVT::i32),
                   Value, FIdx};
  SDVTList VTs = DAG.getVTList(/*chain*/ MVT::Other);

  StoreChain = DAG.getMemIntrinsicNode(ISD::INTRINSIC_VOID,
    dl, VTs, Ops, MVT::v4i32, PtrInfo);

  // Move data into the byte array.
  SDValue Loads[4], LoadChains[4];
  for (unsigned i = 0; i < 4; ++i) {
    unsigned Offset = 4*i;
    SDValue Idx = DAG.getConstant(Offset, dl, FIdx.getValueType());
    Idx = DAG.getNode(ISD::ADD, dl, FIdx.getValueType(), FIdx, Idx);

    Loads[i] = DAG.getLoad(MVT::i32, dl, StoreChain, Idx,
                           PtrInfo.getWithOffset(Offset));
    LoadChains[i] = Loads[i].getValue(1);
  }

  StoreChain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, LoadChains);

  SDValue Stores[4];
  for (unsigned i = 0; i < 4; ++i) {
    SDValue Idx = DAG.getConstant(i, dl, BasePtr.getValueType());
    Idx = DAG.getNode(ISD::ADD, dl, BasePtr.getValueType(), BasePtr, Idx);

    Stores[i] = DAG.getTruncStore(
        StoreChain, dl, Loads[i], Idx, SN->getPointerInfo().getWithOffset(i),
        MVT::i8, /* Alignment = */ 1, SN->getMemOperand()->getFlags(),
        SN->getAAInfo());
  }

  StoreChain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Stores);

  return StoreChain;
}

SDValue PPCTargetLowering::LowerMUL(SDValue Op, SelectionDAG &DAG) const {
  SDLoc dl(Op);
  if (Op.getValueType() == MVT::v4i32) {
    SDValue LHS = Op.getOperand(0), RHS = Op.getOperand(1);

    SDValue Zero  = BuildSplatI(  0, 1, MVT::v4i32, DAG, dl);
    SDValue Neg16 = BuildSplatI(-16, 4, MVT::v4i32, DAG, dl);//+16 as shift amt.

    SDValue RHSSwap =   // = vrlw RHS, 16
      BuildIntrinsicOp(Intrinsic::ppc_altivec_vrlw, RHS, Neg16, DAG, dl);

    // Shrinkify inputs to v8i16.
    LHS = DAG.getNode(ISD::BITCAST, dl, MVT::v8i16, LHS);
    RHS = DAG.getNode(ISD::BITCAST, dl, MVT::v8i16, RHS);
    RHSSwap = DAG.getNode(ISD::BITCAST, dl, MVT::v8i16, RHSSwap);

    // Low parts multiplied together, generating 32-bit results (we ignore the
    // top parts).
    SDValue LoProd = BuildIntrinsicOp(Intrinsic::ppc_altivec_vmulouh,
                                        LHS, RHS, DAG, dl, MVT::v4i32);

    SDValue HiProd = BuildIntrinsicOp(Intrinsic::ppc_altivec_vmsumuhm,
                                      LHS, RHSSwap, Zero, DAG, dl, MVT::v4i32);
    // Shift the high parts up 16 bits.
    HiProd = BuildIntrinsicOp(Intrinsic::ppc_altivec_vslw, HiProd,
                              Neg16, DAG, dl);
    return DAG.getNode(ISD::ADD, dl, MVT::v4i32, LoProd, HiProd);
  } else if (Op.getValueType() == MVT::v8i16) {
    SDValue LHS = Op.getOperand(0), RHS = Op.getOperand(1);

    SDValue Zero = BuildSplatI(0, 1, MVT::v8i16, DAG, dl);

    return BuildIntrinsicOp(Intrinsic::ppc_altivec_vmladduhm,
                            LHS, RHS, Zero, DAG, dl);
  } else if (Op.getValueType() == MVT::v16i8) {
    SDValue LHS = Op.getOperand(0), RHS = Op.getOperand(1);
    bool isLittleEndian = Subtarget.isLittleEndian();

    // Multiply the even 8-bit parts, producing 16-bit sums.
    SDValue EvenParts = BuildIntrinsicOp(Intrinsic::ppc_altivec_vmuleub,
                                           LHS, RHS, DAG, dl, MVT::v8i16);
    EvenParts = DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, EvenParts);

    // Multiply the odd 8-bit parts, producing 16-bit sums.
    SDValue OddParts = BuildIntrinsicOp(Intrinsic::ppc_altivec_vmuloub,
                                          LHS, RHS, DAG, dl, MVT::v8i16);
    OddParts = DAG.getNode(ISD::BITCAST, dl, MVT::v16i8, OddParts);

    // Merge the results together.  Because vmuleub and vmuloub are
    // instructions with a big-endian bias, we must reverse the
    // element numbering and reverse the meaning of "odd" and "even"
    // when generating little endian code.
    int Ops[16];
    for (unsigned i = 0; i != 8; ++i) {
      if (isLittleEndian) {
        Ops[i*2  ] = 2*i;
        Ops[i*2+1] = 2*i+16;
      } else {
        Ops[i*2  ] = 2*i+1;
        Ops[i*2+1] = 2*i+1+16;
      }
    }
    if (isLittleEndian)
      return DAG.getVectorShuffle(MVT::v16i8, dl, OddParts, EvenParts, Ops);
    else
      return DAG.getVectorShuffle(MVT::v16i8, dl, EvenParts, OddParts, Ops);
  } else {
    llvm_unreachable("Unknown mul to lower!");
  }
}

SDValue PPCTargetLowering::LowerABS(SDValue Op, SelectionDAG &DAG) const {

  assert(Op.getOpcode() == ISD::ABS && "Should only be called for ISD::ABS");

  EVT VT = Op.getValueType();
  assert(VT.isVector() &&
         "Only set vector abs as custom, scalar abs shouldn't reach here!");
  assert((VT == MVT::v2i64 || VT == MVT::v4i32 || VT == MVT::v8i16 ||
          VT == MVT::v16i8) &&
         "Unexpected vector element type!");
  assert((VT != MVT::v2i64 || Subtarget.hasP8Altivec()) &&
         "Current subtarget doesn't support smax v2i64!");

  // For vector abs, it can be lowered to:
  // abs x
  // ==>
  // y = -x
  // smax(x, y)

  SDLoc dl(Op);
  SDValue X = Op.getOperand(0);
  SDValue Zero = DAG.getConstant(0, dl, VT);
  SDValue Y = DAG.getNode(ISD::SUB, dl, VT, Zero, X);

  // SMAX patch https://reviews.llvm.org/D47332
  // hasn't landed yet, so use intrinsic first here.
  // TODO: Should use SMAX directly once SMAX patch landed
  Intrinsic::ID BifID = Intrinsic::ppc_altivec_vmaxsw;
  if (VT == MVT::v2i64)
    BifID = Intrinsic::ppc_altivec_vmaxsd;
  else if (VT == MVT::v8i16)
    BifID = Intrinsic::ppc_altivec_vmaxsh;
  else if (VT == MVT::v16i8)
    BifID = Intrinsic::ppc_altivec_vmaxsb;
  
  return BuildIntrinsicOp(BifID, X, Y, DAG, dl, VT);
}

/// LowerOperation - Provide custom lowering hooks for some operations.
///
SDValue PPCTargetLowering::LowerOperation(SDValue Op, SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  default: llvm_unreachable("Wasn't expecting to be able to lower this!");
  case ISD::ConstantPool:       return LowerConstantPool(Op, DAG);
  case ISD::BlockAddress:       return LowerBlockAddress(Op, DAG);
  case ISD::GlobalAddress:      return LowerGlobalAddress(Op, DAG);
  case ISD::GlobalTLSAddress:   return LowerGlobalTLSAddress(Op, DAG);
  case ISD::JumpTable:          return LowerJumpTable(Op, DAG);
  case ISD::SETCC:              return LowerSETCC(Op, DAG);
  case ISD::INIT_TRAMPOLINE:    return LowerINIT_TRAMPOLINE(Op, DAG);
  case ISD::ADJUST_TRAMPOLINE:  return LowerADJUST_TRAMPOLINE(Op, DAG);

  // Variable argument lowering.
  case ISD::VASTART:            return LowerVASTART(Op, DAG);
  case ISD::VAARG:              return LowerVAARG(Op, DAG);
  case ISD::VACOPY:             return LowerVACOPY(Op, DAG);

  case ISD::STACKRESTORE:       return LowerSTACKRESTORE(Op, DAG);
  case ISD::DYNAMIC_STACKALLOC: return LowerDYNAMIC_STACKALLOC(Op, DAG);
  case ISD::GET_DYNAMIC_AREA_OFFSET:
    return LowerGET_DYNAMIC_AREA_OFFSET(Op, DAG);

  // Exception handling lowering.
  case ISD::EH_DWARF_CFA:       return LowerEH_DWARF_CFA(Op, DAG);
  case ISD::EH_SJLJ_SETJMP:     return lowerEH_SJLJ_SETJMP(Op, DAG);
  case ISD::EH_SJLJ_LONGJMP:    return lowerEH_SJLJ_LONGJMP(Op, DAG);

  case ISD::LOAD:               return LowerLOAD(Op, DAG);
  case ISD::STORE:              return LowerSTORE(Op, DAG);
  case ISD::TRUNCATE:           return LowerTRUNCATE(Op, DAG);
  case ISD::SELECT_CC:          return LowerSELECT_CC(Op, DAG);
  case ISD::FP_TO_UINT:
  case ISD::FP_TO_SINT:         return LowerFP_TO_INT(Op, DAG, SDLoc(Op));
  case ISD::UINT_TO_FP:
  case ISD::SINT_TO_FP:         return LowerINT_TO_FP(Op, DAG);
  case ISD::FLT_ROUNDS_:        return LowerFLT_ROUNDS_(Op, DAG);

  // Lower 64-bit shifts.
  case ISD::SHL_PARTS:          return LowerSHL_PARTS(Op, DAG);
  case ISD::SRL_PARTS:          return LowerSRL_PARTS(Op, DAG);
  case ISD::SRA_PARTS:          return LowerSRA_PARTS(Op, DAG);

  // Vector-related lowering.
  case ISD::BUILD_VECTOR:       return LowerBUILD_VECTOR(Op, DAG);
  case ISD::VECTOR_SHUFFLE:     return LowerVECTOR_SHUFFLE(Op, DAG);
  case ISD::INTRINSIC_WO_CHAIN: return LowerINTRINSIC_WO_CHAIN(Op, DAG);
  case ISD::SCALAR_TO_VECTOR:   return LowerSCALAR_TO_VECTOR(Op, DAG);
  case ISD::EXTRACT_VECTOR_ELT: return LowerEXTRACT_VECTOR_ELT(Op, DAG);
  case ISD::INSERT_VECTOR_ELT:  return LowerINSERT_VECTOR_ELT(Op, DAG);
  case ISD::MUL:                return LowerMUL(Op, DAG);
  case ISD::ABS:                return LowerABS(Op, DAG);

  // For counter-based loop handling.
  case ISD::INTRINSIC_W_CHAIN:  return SDValue();

  case ISD::BITCAST:            return LowerBITCAST(Op, DAG);

  // Frame & Return address.
  case ISD::RETURNADDR:         return LowerRETURNADDR(Op, DAG);
  case ISD::FRAMEADDR:          return LowerFRAMEADDR(Op, DAG);

  case ISD::INTRINSIC_VOID:
    return LowerINTRINSIC_VOID(Op, DAG);
  case ISD::SREM:
  case ISD::UREM:
    return LowerREM(Op, DAG);
  case ISD::BSWAP:
    return LowerBSWAP(Op, DAG);
  case ISD::ATOMIC_CMP_SWAP:
    return LowerATOMIC_CMP_SWAP(Op, DAG);
  }
}

void PPCTargetLowering::ReplaceNodeResults(SDNode *N,
                                           SmallVectorImpl<SDValue>&Results,
                                           SelectionDAG &DAG) const {
  SDLoc dl(N);
  switch (N->getOpcode()) {
  default:
    llvm_unreachable("Do not know how to custom type legalize this operation!");
  case ISD::READCYCLECOUNTER: {
    SDVTList VTs = DAG.getVTList(MVT::i32, MVT::i32, MVT::Other);
    SDValue RTB = DAG.getNode(PPCISD::READ_TIME_BASE, dl, VTs, N->getOperand(0));

    Results.push_back(RTB);
    Results.push_back(RTB.getValue(1));
    Results.push_back(RTB.getValue(2));
    break;
  }
  case ISD::INTRINSIC_W_CHAIN: {
    if (cast<ConstantSDNode>(N->getOperand(1))->getZExtValue() !=
        Intrinsic::ppc_is_decremented_ctr_nonzero)
      break;

    assert(N->getValueType(0) == MVT::i1 &&
           "Unexpected result type for CTR decrement intrinsic");
    EVT SVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(),
                                 N->getValueType(0));
    SDVTList VTs = DAG.getVTList(SVT, MVT::Other);
    SDValue NewInt = DAG.getNode(N->getOpcode(), dl, VTs, N->getOperand(0),
                                 N->getOperand(1));

    Results.push_back(DAG.getNode(ISD::TRUNCATE, dl, MVT::i1, NewInt));
    Results.push_back(NewInt.getValue(1));
    break;
  }
  case ISD::VAARG: {
    if (!Subtarget.isSVR4ABI() || Subtarget.isPPC64())
      return;

    EVT VT = N->getValueType(0);

    if (VT == MVT::i64) {
      SDValue NewNode = LowerVAARG(SDValue(N, 1), DAG);

      Results.push_back(NewNode);
      Results.push_back(NewNode.getValue(1));
    }
    return;
  }
  case ISD::FP_TO_SINT:
  case ISD::FP_TO_UINT:
    // LowerFP_TO_INT() can only handle f32 and f64.
    if (N->getOperand(0).getValueType() == MVT::ppcf128)
      return;
    Results.push_back(LowerFP_TO_INT(SDValue(N, 0), DAG, dl));
    return;
  case ISD::BITCAST:
    // Don't handle bitcast here.
    return;
  }
}

//===----------------------------------------------------------------------===//
//  Other Lowering Code
//===----------------------------------------------------------------------===//

static Instruction* callIntrinsic(IRBuilder<> &Builder, Intrinsic::ID Id) {
  Module *M = Builder.GetInsertBlock()->getParent()->getParent();
  Function *Func = Intrinsic::getDeclaration(M, Id);
  return Builder.CreateCall(Func, {});
}

// The mappings for emitLeading/TrailingFence is taken from
// http://www.cl.cam.ac.uk/~pes20/cpp/cpp0xmappings.html
Instruction *PPCTargetLowering::emitLeadingFence(IRBuilder<> &Builder,
                                                 Instruction *Inst,
                                                 AtomicOrdering Ord) const {
  if (Ord == AtomicOrdering::SequentiallyConsistent)
    return callIntrinsic(Builder, Intrinsic::ppc_sync);
  if (isReleaseOrStronger(Ord))
    return callIntrinsic(Builder, Intrinsic::ppc_lwsync);
  return nullptr;
}

Instruction *PPCTargetLowering::emitTrailingFence(IRBuilder<> &Builder,
                                                  Instruction *Inst,
                                                  AtomicOrdering Ord) const {
  if (Inst->hasAtomicLoad() && isAcquireOrStronger(Ord)) {
    // See http://www.cl.cam.ac.uk/~pes20/cpp/cpp0xmappings.html and
    // http://www.rdrop.com/users/paulmck/scalability/paper/N2745r.2011.03.04a.html
    // and http://www.cl.cam.ac.uk/~pes20/cppppc/ for justification.
    if (isa<LoadInst>(Inst) && Subtarget.isPPC64())
      return Builder.CreateCall(
          Intrinsic::getDeclaration(
              Builder.GetInsertBlock()->getParent()->getParent(),
              Intrinsic::ppc_cfence, {Inst->getType()}),
          {Inst});
    // FIXME: Can use isync for rmw operation.
    return callIntrinsic(Builder, Intrinsic::ppc_lwsync);
  }
  return nullptr;
}

MachineBasicBlock *
PPCTargetLowering::EmitAtomicBinary(MachineInstr &MI, MachineBasicBlock *BB,
                                    unsigned AtomicSize,
                                    unsigned BinOpcode,
                                    unsigned CmpOpcode,
                                    unsigned CmpPred) const {
  // This also handles ATOMIC_SWAP, indicated by BinOpcode==0.
  const TargetInstrInfo *TII = Subtarget.getInstrInfo();

  auto LoadMnemonic = PPC::LDARX;
  auto StoreMnemonic = PPC::STDCX;
  switch (AtomicSize) {
  default:
    llvm_unreachable("Unexpected size of atomic entity");
  case 1:
    LoadMnemonic = PPC::LBARX;
    StoreMnemonic = PPC::STBCX;
    assert(Subtarget.hasPartwordAtomics() && "Call this only with size >=4");
    break;
  case 2:
    LoadMnemonic = PPC::LHARX;
    StoreMnemonic = PPC::STHCX;
    assert(Subtarget.hasPartwordAtomics() && "Call this only with size >=4");
    break;
  case 4:
    LoadMnemonic = PPC::LWARX;
    StoreMnemonic = PPC::STWCX;
    break;
  case 8:
    LoadMnemonic = PPC::LDARX;
    StoreMnemonic = PPC::STDCX;
    break;
  }

  const BasicBlock *LLVM_BB = BB->getBasicBlock();
  MachineFunction *F = BB->getParent();
  MachineFunction::iterator It = ++BB->getIterator();

  unsigned dest = MI.getOperand(0).getReg();
  unsigned ptrA = MI.getOperand(1).getReg();
  unsigned ptrB = MI.getOperand(2).getReg();
  unsigned incr = MI.getOperand(3).getReg();
  DebugLoc dl = MI.getDebugLoc();

  MachineBasicBlock *loopMBB = F->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *loop2MBB =
    CmpOpcode ? F->CreateMachineBasicBlock(LLVM_BB) : nullptr;
  MachineBasicBlock *exitMBB = F->CreateMachineBasicBlock(LLVM_BB);
  F->insert(It, loopMBB);
  if (CmpOpcode)
    F->insert(It, loop2MBB);
  F->insert(It, exitMBB);
  exitMBB->splice(exitMBB->begin(), BB,
                  std::next(MachineBasicBlock::iterator(MI)), BB->end());
  exitMBB->transferSuccessorsAndUpdatePHIs(BB);

  MachineRegisterInfo &RegInfo = F->getRegInfo();
  unsigned TmpReg = (!BinOpcode) ? incr :
    RegInfo.createVirtualRegister( AtomicSize == 8 ? &PPC::G8RCRegClass
                                           : &PPC::GPRCRegClass);

  //  thisMBB:
  //   ...
  //   fallthrough --> loopMBB
  BB->addSuccessor(loopMBB);

  //  loopMBB:
  //   l[wd]arx dest, ptr
  //   add r0, dest, incr
  //   st[wd]cx. r0, ptr
  //   bne- loopMBB
  //   fallthrough --> exitMBB

  // For max/min...
  //  loopMBB:
  //   l[wd]arx dest, ptr
  //   cmpl?[wd] incr, dest
  //   bgt exitMBB
  //  loop2MBB:
  //   st[wd]cx. dest, ptr
  //   bne- loopMBB
  //   fallthrough --> exitMBB

  BB = loopMBB;
  BuildMI(BB, dl, TII->get(LoadMnemonic), dest)
    .addReg(ptrA).addReg(ptrB);
  if (BinOpcode)
    BuildMI(BB, dl, TII->get(BinOpcode), TmpReg).addReg(incr).addReg(dest);
  if (CmpOpcode) {
    // Signed comparisons of byte or halfword values must be sign-extended.
    if (CmpOpcode == PPC::CMPW && AtomicSize < 4) {
      unsigned ExtReg =  RegInfo.createVirtualRegister(&PPC::GPRCRegClass);
      BuildMI(BB, dl, TII->get(AtomicSize == 1 ? PPC::EXTSB : PPC::EXTSH),
              ExtReg).addReg(dest);
      BuildMI(BB, dl, TII->get(CmpOpcode), PPC::CR0)
        .addReg(incr).addReg(ExtReg);
    } else
      BuildMI(BB, dl, TII->get(CmpOpcode), PPC::CR0)
        .addReg(incr).addReg(dest);

    BuildMI(BB, dl, TII->get(PPC::BCC))
      .addImm(CmpPred).addReg(PPC::CR0).addMBB(exitMBB);
    BB->addSuccessor(loop2MBB);
    BB->addSuccessor(exitMBB);
    BB = loop2MBB;
  }
  BuildMI(BB, dl, TII->get(StoreMnemonic))
    .addReg(TmpReg).addReg(ptrA).addReg(ptrB);
  BuildMI(BB, dl, TII->get(PPC::BCC))
    .addImm(PPC::PRED_NE).addReg(PPC::CR0).addMBB(loopMBB);
  BB->addSuccessor(loopMBB);
  BB->addSuccessor(exitMBB);

  //  exitMBB:
  //   ...
  BB = exitMBB;
  return BB;
}

MachineBasicBlock *PPCTargetLowering::EmitPartwordAtomicBinary(
    MachineInstr &MI, MachineBasicBlock *BB,
    bool is8bit, // operation
    unsigned BinOpcode, unsigned CmpOpcode, unsigned CmpPred) const {
  // If we support part-word atomic mnemonics, just use them
  if (Subtarget.hasPartwordAtomics())
    return EmitAtomicBinary(MI, BB, is8bit ? 1 : 2, BinOpcode, CmpOpcode,
                            CmpPred);

  // This also handles ATOMIC_SWAP, indicated by BinOpcode==0.
  const TargetInstrInfo *TII = Subtarget.getInstrInfo();
  // In 64 bit mode we have to use 64 bits for addresses, even though the
  // lwarx/stwcx are 32 bits.  With the 32-bit atomics we can use address
  // registers without caring whether they're 32 or 64, but here we're
  // doing actual arithmetic on the addresses.
  bool is64bit = Subtarget.isPPC64();
  bool isLittleEndian = Subtarget.isLittleEndian();
  unsigned ZeroReg = is64bit ? PPC::ZERO8 : PPC::ZERO;

  const BasicBlock *LLVM_BB = BB->getBasicBlock();
  MachineFunction *F = BB->getParent();
  MachineFunction::iterator It = ++BB->getIterator();

  unsigned dest = MI.getOperand(0).getReg();
  unsigned ptrA = MI.getOperand(1).getReg();
  unsigned ptrB = MI.getOperand(2).getReg();
  unsigned incr = MI.getOperand(3).getReg();
  DebugLoc dl = MI.getDebugLoc();

  MachineBasicBlock *loopMBB = F->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *loop2MBB =
      CmpOpcode ? F->CreateMachineBasicBlock(LLVM_BB) : nullptr;
  MachineBasicBlock *exitMBB = F->CreateMachineBasicBlock(LLVM_BB);
  F->insert(It, loopMBB);
  if (CmpOpcode)
    F->insert(It, loop2MBB);
  F->insert(It, exitMBB);
  exitMBB->splice(exitMBB->begin(), BB,
                  std::next(MachineBasicBlock::iterator(MI)), BB->end());
  exitMBB->transferSuccessorsAndUpdatePHIs(BB);

  MachineRegisterInfo &RegInfo = F->getRegInfo();
  const TargetRegisterClass *RC =
      is64bit ? &PPC::G8RCRegClass : &PPC::GPRCRegClass;
  const TargetRegisterClass *GPRC = &PPC::GPRCRegClass;

  unsigned PtrReg = RegInfo.createVirtualRegister(RC);
  unsigned Shift1Reg = RegInfo.createVirtualRegister(GPRC);
  unsigned ShiftReg =
      isLittleEndian ? Shift1Reg : RegInfo.createVirtualRegister(GPRC);
  unsigned Incr2Reg = RegInfo.createVirtualRegister(GPRC);
  unsigned MaskReg = RegInfo.createVirtualRegister(GPRC);
  unsigned Mask2Reg = RegInfo.createVirtualRegister(GPRC);
  unsigned Mask3Reg = RegInfo.createVirtualRegister(GPRC);
  unsigned Tmp2Reg = RegInfo.createVirtualRegister(GPRC);
  unsigned Tmp3Reg = RegInfo.createVirtualRegister(GPRC);
  unsigned Tmp4Reg = RegInfo.createVirtualRegister(GPRC);
  unsigned TmpDestReg = RegInfo.createVirtualRegister(GPRC);
  unsigned Ptr1Reg;
  unsigned TmpReg =
      (!BinOpcode) ? Incr2Reg : RegInfo.createVirtualRegister(GPRC);

  //  thisMBB:
  //   ...
  //   fallthrough --> loopMBB
  BB->addSuccessor(loopMBB);

  // The 4-byte load must be aligned, while a char or short may be
  // anywhere in the word.  Hence all this nasty bookkeeping code.
  //   add ptr1, ptrA, ptrB [copy if ptrA==0]
  //   rlwinm shift1, ptr1, 3, 27, 28 [3, 27, 27]
  //   xori shift, shift1, 24 [16]
  //   rlwinm ptr, ptr1, 0, 0, 29
  //   slw incr2, incr, shift
  //   li mask2, 255 [li mask3, 0; ori mask2, mask3, 65535]
  //   slw mask, mask2, shift
  //  loopMBB:
  //   lwarx tmpDest, ptr
  //   add tmp, tmpDest, incr2
  //   andc tmp2, tmpDest, mask
  //   and tmp3, tmp, mask
  //   or tmp4, tmp3, tmp2
  //   stwcx. tmp4, ptr
  //   bne- loopMBB
  //   fallthrough --> exitMBB
  //   srw dest, tmpDest, shift
  if (ptrA != ZeroReg) {
    Ptr1Reg = RegInfo.createVirtualRegister(RC);
    BuildMI(BB, dl, TII->get(is64bit ? PPC::ADD8 : PPC::ADD4), Ptr1Reg)
        .addReg(ptrA)
        .addReg(ptrB);
  } else {
    Ptr1Reg = ptrB;
  }
  // We need use 32-bit subregister to avoid mismatch register class in 64-bit
  // mode.
  BuildMI(BB, dl, TII->get(PPC::RLWINM), Shift1Reg)
      .addReg(Ptr1Reg, 0, is64bit ? PPC::sub_32 : 0)
      .addImm(3)
      .addImm(27)
      .addImm(is8bit ? 28 : 27);
  if (!isLittleEndian)
    BuildMI(BB, dl, TII->get(PPC::XORI), ShiftReg)
        .addReg(Shift1Reg)
        .addImm(is8bit ? 24 : 16);
  if (is64bit)
    BuildMI(BB, dl, TII->get(PPC::RLDICR), PtrReg)
        .addReg(Ptr1Reg)
        .addImm(0)
        .addImm(61);
  else
    BuildMI(BB, dl, TII->get(PPC::RLWINM), PtrReg)
        .addReg(Ptr1Reg)
        .addImm(0)
        .addImm(0)
        .addImm(29);
  BuildMI(BB, dl, TII->get(PPC::SLW), Incr2Reg).addReg(incr).addReg(ShiftReg);
  if (is8bit)
    BuildMI(BB, dl, TII->get(PPC::LI), Mask2Reg).addImm(255);
  else {
    BuildMI(BB, dl, TII->get(PPC::LI), Mask3Reg).addImm(0);
    BuildMI(BB, dl, TII->get(PPC::ORI), Mask2Reg)
        .addReg(Mask3Reg)
        .addImm(65535);
  }
  BuildMI(BB, dl, TII->get(PPC::SLW), MaskReg)
      .addReg(Mask2Reg)
      .addReg(ShiftReg);

  BB = loopMBB;
  BuildMI(BB, dl, TII->get(PPC::LWARX), TmpDestReg)
      .addReg(ZeroReg)
      .addReg(PtrReg);
  if (BinOpcode)
    BuildMI(BB, dl, TII->get(BinOpcode), TmpReg)
        .addReg(Incr2Reg)
        .addReg(TmpDestReg);
  BuildMI(BB, dl, TII->get(PPC::ANDC), Tmp2Reg)
      .addReg(TmpDestReg)
      .addReg(MaskReg);
  BuildMI(BB, dl, TII->get(PPC::AND), Tmp3Reg).addReg(TmpReg).addReg(MaskReg);
  if (CmpOpcode) {
    // For unsigned comparisons, we can directly compare the shifted values.
    // For signed comparisons we shift and sign extend.
    unsigned SReg = RegInfo.createVirtualRegister(GPRC);
    BuildMI(BB, dl, TII->get(PPC::AND), SReg)
        .addReg(TmpDestReg)
        .addReg(MaskReg);
    unsigned ValueReg = SReg;
    unsigned CmpReg = Incr2Reg;
    if (CmpOpcode == PPC::CMPW) {
      ValueReg = RegInfo.createVirtualRegister(GPRC);
      BuildMI(BB, dl, TII->get(PPC::SRW), ValueReg)
          .addReg(SReg)
          .addReg(ShiftReg);
      unsigned ValueSReg = RegInfo.createVirtualRegister(GPRC);
      BuildMI(BB, dl, TII->get(is8bit ? PPC::EXTSB : PPC::EXTSH), ValueSReg)
          .addReg(ValueReg);
      ValueReg = ValueSReg;
      CmpReg = incr;
    }
    BuildMI(BB, dl, TII->get(CmpOpcode), PPC::CR0)
        .addReg(CmpReg)
        .addReg(ValueReg);
    BuildMI(BB, dl, TII->get(PPC::BCC))
        .addImm(CmpPred)
        .addReg(PPC::CR0)
        .addMBB(exitMBB);
    BB->addSuccessor(loop2MBB);
    BB->addSuccessor(exitMBB);
    BB = loop2MBB;
  }
  BuildMI(BB, dl, TII->get(PPC::OR), Tmp4Reg).addReg(Tmp3Reg).addReg(Tmp2Reg);
  BuildMI(BB, dl, TII->get(PPC::STWCX))
      .addReg(Tmp4Reg)
      .addReg(ZeroReg)
      .addReg(PtrReg);
  BuildMI(BB, dl, TII->get(PPC::BCC))
      .addImm(PPC::PRED_NE)
      .addReg(PPC::CR0)
      .addMBB(loopMBB);
  BB->addSuccessor(loopMBB);
  BB->addSuccessor(exitMBB);

  //  exitMBB:
  //   ...
  BB = exitMBB;
  BuildMI(*BB, BB->begin(), dl, TII->get(PPC::SRW), dest)
      .addReg(TmpDestReg)
      .addReg(ShiftReg);
  return BB;
}

llvm::MachineBasicBlock *
PPCTargetLowering::emitEHSjLjSetJmp(MachineInstr &MI,
                                    MachineBasicBlock *MBB) const {
  DebugLoc DL = MI.getDebugLoc();
  const TargetInstrInfo *TII = Subtarget.getInstrInfo();
  const PPCRegisterInfo *TRI = Subtarget.getRegisterInfo();

  MachineFunction *MF = MBB->getParent();
  MachineRegisterInfo &MRI = MF->getRegInfo();

  const BasicBlock *BB = MBB->getBasicBlock();
  MachineFunction::iterator I = ++MBB->getIterator();

  unsigned DstReg = MI.getOperand(0).getReg();
  const TargetRegisterClass *RC = MRI.getRegClass(DstReg);
  assert(TRI->isTypeLegalForClass(*RC, MVT::i32) && "Invalid destination!");
  unsigned mainDstReg = MRI.createVirtualRegister(RC);
  unsigned restoreDstReg = MRI.createVirtualRegister(RC);

  MVT PVT = getPointerTy(MF->getDataLayout());
  assert((PVT == MVT::i64 || PVT == MVT::i32) &&
         "Invalid Pointer Size!");
  // For v = setjmp(buf), we generate
  //
  // thisMBB:
  //  SjLjSetup mainMBB
  //  bl mainMBB
  //  v_restore = 1
  //  b sinkMBB
  //
  // mainMBB:
  //  buf[LabelOffset] = LR
  //  v_main = 0
  //
  // sinkMBB:
  //  v = phi(main, restore)
  //

  MachineBasicBlock *thisMBB = MBB;
  MachineBasicBlock *mainMBB = MF->CreateMachineBasicBlock(BB);
  MachineBasicBlock *sinkMBB = MF->CreateMachineBasicBlock(BB);
  MF->insert(I, mainMBB);
  MF->insert(I, sinkMBB);

  MachineInstrBuilder MIB;

  // Transfer the remainder of BB and its successor edges to sinkMBB.
  sinkMBB->splice(sinkMBB->begin(), MBB,
                  std::next(MachineBasicBlock::iterator(MI)), MBB->end());
  sinkMBB->transferSuccessorsAndUpdatePHIs(MBB);

  // Note that the structure of the jmp_buf used here is not compatible
  // with that used by libc, and is not designed to be. Specifically, it
  // stores only those 'reserved' registers that LLVM does not otherwise
  // understand how to spill. Also, by convention, by the time this
  // intrinsic is called, Clang has already stored the frame address in the
  // first slot of the buffer and stack address in the third. Following the
  // X86 target code, we'll store the jump address in the second slot. We also
  // need to save the TOC pointer (R2) to handle jumps between shared
  // libraries, and that will be stored in the fourth slot. The thread
  // identifier (R13) is not affected.

  // thisMBB:
  const int64_t LabelOffset = 1 * PVT.getStoreSize();
  const int64_t TOCOffset   = 3 * PVT.getStoreSize();
  const int64_t BPOffset    = 4 * PVT.getStoreSize();

  // Prepare IP either in reg.
  const TargetRegisterClass *PtrRC = getRegClassFor(PVT);
  unsigned LabelReg = MRI.createVirtualRegister(PtrRC);
  unsigned BufReg = MI.getOperand(1).getReg();

  if (Subtarget.isPPC64() && Subtarget.isSVR4ABI()) {
    setUsesTOCBasePtr(*MBB->getParent());
    MIB = BuildMI(*thisMBB, MI, DL, TII->get(PPC::STD))
              .addReg(PPC::X2)
              .addImm(TOCOffset)
              .addReg(BufReg)
              .cloneMemRefs(MI);
  }

  // Naked functions never have a base pointer, and so we use r1. For all
  // other functions, this decision must be delayed until during PEI.
  unsigned BaseReg;
  if (MF->getFunction().hasFnAttribute(Attribute::Naked))
    BaseReg = Subtarget.isPPC64() ? PPC::X1 : PPC::R1;
  else
    BaseReg = Subtarget.isPPC64() ? PPC::BP8 : PPC::BP;

  MIB = BuildMI(*thisMBB, MI, DL,
                TII->get(Subtarget.isPPC64() ? PPC::STD : PPC::STW))
            .addReg(BaseReg)
            .addImm(BPOffset)
            .addReg(BufReg)
            .cloneMemRefs(MI);

  // Setup
  MIB = BuildMI(*thisMBB, MI, DL, TII->get(PPC::BCLalways)).addMBB(mainMBB);
  MIB.addRegMask(TRI->getNoPreservedMask());

  BuildMI(*thisMBB, MI, DL, TII->get(PPC::LI), restoreDstReg).addImm(1);

  MIB = BuildMI(*thisMBB, MI, DL, TII->get(PPC::EH_SjLj_Setup))
          .addMBB(mainMBB);
  MIB = BuildMI(*thisMBB, MI, DL, TII->get(PPC::B)).addMBB(sinkMBB);

  thisMBB->addSuccessor(mainMBB, BranchProbability::getZero());
  thisMBB->addSuccessor(sinkMBB, BranchProbability::getOne());

  // mainMBB:
  //  mainDstReg = 0
  MIB =
      BuildMI(mainMBB, DL,
              TII->get(Subtarget.isPPC64() ? PPC::MFLR8 : PPC::MFLR), LabelReg);

  // Store IP
  if (Subtarget.isPPC64()) {
    MIB = BuildMI(mainMBB, DL, TII->get(PPC::STD))
            .addReg(LabelReg)
            .addImm(LabelOffset)
            .addReg(BufReg);
  } else {
    MIB = BuildMI(mainMBB, DL, TII->get(PPC::STW))
            .addReg(LabelReg)
            .addImm(LabelOffset)
            .addReg(BufReg);
  }
  MIB.cloneMemRefs(MI);

  BuildMI(mainMBB, DL, TII->get(PPC::LI), mainDstReg).addImm(0);
  mainMBB->addSuccessor(sinkMBB);

  // sinkMBB:
  BuildMI(*sinkMBB, sinkMBB->begin(), DL,
          TII->get(PPC::PHI), DstReg)
    .addReg(mainDstReg).addMBB(mainMBB)
    .addReg(restoreDstReg).addMBB(thisMBB);

  MI.eraseFromParent();
  return sinkMBB;
}

MachineBasicBlock *
PPCTargetLowering::emitEHSjLjLongJmp(MachineInstr &MI,
                                     MachineBasicBlock *MBB) const {
  DebugLoc DL = MI.getDebugLoc();
  const TargetInstrInfo *TII = Subtarget.getInstrInfo();

  MachineFunction *MF = MBB->getParent();
  MachineRegisterInfo &MRI = MF->getRegInfo();

  MVT PVT = getPointerTy(MF->getDataLayout());
  assert((PVT == MVT::i64 || PVT == MVT::i32) &&
         "Invalid Pointer Size!");

  const TargetRegisterClass *RC =
    (PVT == MVT::i64) ? &PPC::G8RCRegClass : &PPC::GPRCRegClass;
  unsigned Tmp = MRI.createVirtualRegister(RC);
  // Since FP is only updated here but NOT referenced, it's treated as GPR.
  unsigned FP  = (PVT == MVT::i64) ? PPC::X31 : PPC::R31;
  unsigned SP  = (PVT == MVT::i64) ? PPC::X1 : PPC::R1;
  unsigned BP =
      (PVT == MVT::i64)
          ? PPC::X30
          : (Subtarget.isSVR4ABI() && isPositionIndependent() ? PPC::R29
                                                              : PPC::R30);

  MachineInstrBuilder MIB;

  const int64_t LabelOffset = 1 * PVT.getStoreSize();
  const int64_t SPOffset    = 2 * PVT.getStoreSize();
  const int64_t TOCOffset   = 3 * PVT.getStoreSize();
  const int64_t BPOffset    = 4 * PVT.getStoreSize();

  unsigned BufReg = MI.getOperand(0).getReg();

  // Reload FP (the jumped-to function may not have had a
  // frame pointer, and if so, then its r31 will be restored
  // as necessary).
  if (PVT == MVT::i64) {
    MIB = BuildMI(*MBB, MI, DL, TII->get(PPC::LD), FP)
            .addImm(0)
            .addReg(BufReg);
  } else {
    MIB = BuildMI(*MBB, MI, DL, TII->get(PPC::LWZ), FP)
            .addImm(0)
            .addReg(BufReg);
  }
  MIB.cloneMemRefs(MI);

  // Reload IP
  if (PVT == MVT::i64) {
    MIB = BuildMI(*MBB, MI, DL, TII->get(PPC::LD), Tmp)
            .addImm(LabelOffset)
            .addReg(BufReg);
  } else {
    MIB = BuildMI(*MBB, MI, DL, TII->get(PPC::LWZ), Tmp)
            .addImm(LabelOffset)
            .addReg(BufReg);
  }
  MIB.cloneMemRefs(MI);

  // Reload SP
  if (PVT == MVT::i64) {
    MIB = BuildMI(*MBB, MI, DL, TII->get(PPC::LD), SP)
            .addImm(SPOffset)
            .addReg(BufReg);
  } else {
    MIB = BuildMI(*MBB, MI, DL, TII->get(PPC::LWZ), SP)
            .addImm(SPOffset)
            .addReg(BufReg);
  }
  MIB.cloneMemRefs(MI);

  // Reload BP
  if (PVT == MVT::i64) {
    MIB = BuildMI(*MBB, MI, DL, TII->get(PPC::LD), BP)
            .addImm(BPOffset)
            .addReg(BufReg);
  } else {
    MIB = BuildMI(*MBB, MI, DL, TII->get(PPC::LWZ), BP)
            .addImm(BPOffset)
            .addReg(BufReg);
  }
  MIB.cloneMemRefs(MI);

  // Reload TOC
  if (PVT == MVT::i64 && Subtarget.isSVR4ABI()) {
    setUsesTOCBasePtr(*MBB->getParent());
    MIB = BuildMI(*MBB, MI, DL, TII->get(PPC::LD), PPC::X2)
              .addImm(TOCOffset)
              .addReg(BufReg)
              .cloneMemRefs(MI);
  }

  // Jump
  BuildMI(*MBB, MI, DL,
          TII->get(PVT == MVT::i64 ? PPC::MTCTR8 : PPC::MTCTR)).addReg(Tmp);
  BuildMI(*MBB, MI, DL, TII->get(PVT == MVT::i64 ? PPC::BCTR8 : PPC::BCTR));

  MI.eraseFromParent();
  return MBB;
}

MachineBasicBlock *
PPCTargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                               MachineBasicBlock *BB) const {
  if (MI.getOpcode() == TargetOpcode::STACKMAP ||
      MI.getOpcode() == TargetOpcode::PATCHPOINT) {
    if (Subtarget.isPPC64() && Subtarget.isSVR4ABI() &&
        MI.getOpcode() == TargetOpcode::PATCHPOINT) {
      // Call lowering should have added an r2 operand to indicate a dependence
      // on the TOC base pointer value. It can't however, because there is no
      // way to mark the dependence as implicit there, and so the stackmap code
      // will confuse it with a regular operand. Instead, add the dependence
      // here.
      MI.addOperand(MachineOperand::CreateReg(PPC::X2, false, true));
    }

    return emitPatchPoint(MI, BB);
  }

  if (MI.getOpcode() == PPC::EH_SjLj_SetJmp32 ||
      MI.getOpcode() == PPC::EH_SjLj_SetJmp64) {
    return emitEHSjLjSetJmp(MI, BB);
  } else if (MI.getOpcode() == PPC::EH_SjLj_LongJmp32 ||
             MI.getOpcode() == PPC::EH_SjLj_LongJmp64) {
    return emitEHSjLjLongJmp(MI, BB);
  }

  const TargetInstrInfo *TII = Subtarget.getInstrInfo();

  // To "insert" these instructions we actually have to insert their
  // control-flow patterns.
  const BasicBlock *LLVM_BB = BB->getBasicBlock();
  MachineFunction::iterator It = ++BB->getIterator();

  MachineFunction *F = BB->getParent();

  if (MI.getOpcode() == PPC::SELECT_CC_I4 ||
      MI.getOpcode() == PPC::SELECT_CC_I8 || MI.getOpcode() == PPC::SELECT_I4 ||
      MI.getOpcode() == PPC::SELECT_I8) {
    SmallVector<MachineOperand, 2> Cond;
    if (MI.getOpcode() == PPC::SELECT_CC_I4 ||
        MI.getOpcode() == PPC::SELECT_CC_I8)
      Cond.push_back(MI.getOperand(4));
    else
      Cond.push_back(MachineOperand::CreateImm(PPC::PRED_BIT_SET));
    Cond.push_back(MI.getOperand(1));

    DebugLoc dl = MI.getDebugLoc();
    TII->insertSelect(*BB, MI, dl, MI.getOperand(0).getReg(), Cond,
                      MI.getOperand(2).getReg(), MI.getOperand(3).getReg());
  } else if (MI.getOpcode() == PPC::SELECT_CC_I4 ||
             MI.getOpcode() == PPC::SELECT_CC_I8 ||
             MI.getOpcode() == PPC::SELECT_CC_F4 ||
             MI.getOpcode() == PPC::SELECT_CC_F8 ||
             MI.getOpcode() == PPC::SELECT_CC_F16 ||
             MI.getOpcode() == PPC::SELECT_CC_QFRC ||
             MI.getOpcode() == PPC::SELECT_CC_QSRC ||
             MI.getOpcode() == PPC::SELECT_CC_QBRC ||
             MI.getOpcode() == PPC::SELECT_CC_VRRC ||
             MI.getOpcode() == PPC::SELECT_CC_VSFRC ||
             MI.getOpcode() == PPC::SELECT_CC_VSSRC ||
             MI.getOpcode() == PPC::SELECT_CC_VSRC ||
             MI.getOpcode() == PPC::SELECT_CC_SPE4 ||
             MI.getOpcode() == PPC::SELECT_CC_SPE ||
             MI.getOpcode() == PPC::SELECT_I4 ||
             MI.getOpcode() == PPC::SELECT_I8 ||
             MI.getOpcode() == PPC::SELECT_F4 ||
             MI.getOpcode() == PPC::SELECT_F8 ||
             MI.getOpcode() == PPC::SELECT_F16 ||
             MI.getOpcode() == PPC::SELECT_QFRC ||
             MI.getOpcode() == PPC::SELECT_QSRC ||
             MI.getOpcode() == PPC::SELECT_QBRC ||
             MI.getOpcode() == PPC::SELECT_SPE ||
             MI.getOpcode() == PPC::SELECT_SPE4 ||
             MI.getOpcode() == PPC::SELECT_VRRC ||
             MI.getOpcode() == PPC::SELECT_VSFRC ||
             MI.getOpcode() == PPC::SELECT_VSSRC ||
             MI.getOpcode() == PPC::SELECT_VSRC) {
    // The incoming instruction knows the destination vreg to set, the
    // condition code register to branch on, the true/false values to
    // select between, and a branch opcode to use.

    //  thisMBB:
    //  ...
    //   TrueVal = ...
    //   cmpTY ccX, r1, r2
    //   bCC copy1MBB
    //   fallthrough --> copy0MBB
    MachineBasicBlock *thisMBB = BB;
    MachineBasicBlock *copy0MBB = F->CreateMachineBasicBlock(LLVM_BB);
    MachineBasicBlock *sinkMBB = F->CreateMachineBasicBlock(LLVM_BB);
    DebugLoc dl = MI.getDebugLoc();
    F->insert(It, copy0MBB);
    F->insert(It, sinkMBB);

    // Transfer the remainder of BB and its successor edges to sinkMBB.
    sinkMBB->splice(sinkMBB->begin(), BB,
                    std::next(MachineBasicBlock::iterator(MI)), BB->end());
    sinkMBB->transferSuccessorsAndUpdatePHIs(BB);

    // Next, add the true and fallthrough blocks as its successors.
    BB->addSuccessor(copy0MBB);
    BB->addSuccessor(sinkMBB);

    if (MI.getOpcode() == PPC::SELECT_I4 || MI.getOpcode() == PPC::SELECT_I8 ||
        MI.getOpcode() == PPC::SELECT_F4 || MI.getOpcode() == PPC::SELECT_F8 ||
        MI.getOpcode() == PPC::SELECT_F16 ||
        MI.getOpcode() == PPC::SELECT_SPE4 ||
        MI.getOpcode() == PPC::SELECT_SPE ||
        MI.getOpcode() == PPC::SELECT_QFRC ||
        MI.getOpcode() == PPC::SELECT_QSRC ||
        MI.getOpcode() == PPC::SELECT_QBRC ||
        MI.getOpcode() == PPC::SELECT_VRRC ||
        MI.getOpcode() == PPC::SELECT_VSFRC ||
        MI.getOpcode() == PPC::SELECT_VSSRC ||
        MI.getOpcode() == PPC::SELECT_VSRC) {
      BuildMI(BB, dl, TII->get(PPC::BC))
          .addReg(MI.getOperand(1).getReg())
          .addMBB(sinkMBB);
    } else {
      unsigned SelectPred = MI.getOperand(4).getImm();
      BuildMI(BB, dl, TII->get(PPC::BCC))
          .addImm(SelectPred)
          .addReg(MI.getOperand(1).getReg())
          .addMBB(sinkMBB);
    }

    //  copy0MBB:
    //   %FalseValue = ...
    //   # fallthrough to sinkMBB
    BB = copy0MBB;

    // Update machine-CFG edges
    BB->addSuccessor(sinkMBB);

    //  sinkMBB:
    //   %Result = phi [ %FalseValue, copy0MBB ], [ %TrueValue, thisMBB ]
    //  ...
    BB = sinkMBB;
    BuildMI(*BB, BB->begin(), dl, TII->get(PPC::PHI), MI.getOperand(0).getReg())
        .addReg(MI.getOperand(3).getReg())
        .addMBB(copy0MBB)
        .addReg(MI.getOperand(2).getReg())
        .addMBB(thisMBB);
  } else if (MI.getOpcode() == PPC::ReadTB) {
    // To read the 64-bit time-base register on a 32-bit target, we read the
    // two halves. Should the counter have wrapped while it was being read, we
    // need to try again.
    // ...
    // readLoop:
    // mfspr Rx,TBU # load from TBU
    // mfspr Ry,TB  # load from TB
    // mfspr Rz,TBU # load from TBU
    // cmpw crX,Rx,Rz # check if 'old'='new'
    // bne readLoop   # branch if they're not equal
    // ...

    MachineBasicBlock *readMBB = F->CreateMachineBasicBlock(LLVM_BB);
    MachineBasicBlock *sinkMBB = F->CreateMachineBasicBlock(LLVM_BB);
    DebugLoc dl = MI.getDebugLoc();
    F->insert(It, readMBB);
    F->insert(It, sinkMBB);

    // Transfer the remainder of BB and its successor edges to sinkMBB.
    sinkMBB->splice(sinkMBB->begin(), BB,
                    std::next(MachineBasicBlock::iterator(MI)), BB->end());
    sinkMBB->transferSuccessorsAndUpdatePHIs(BB);

    BB->addSuccessor(readMBB);
    BB = readMBB;

    MachineRegisterInfo &RegInfo = F->getRegInfo();
    unsigned ReadAgainReg = RegInfo.createVirtualRegister(&PPC::GPRCRegClass);
    unsigned LoReg = MI.getOperand(0).getReg();
    unsigned HiReg = MI.getOperand(1).getReg();

    BuildMI(BB, dl, TII->get(PPC::MFSPR), HiReg).addImm(269);
    BuildMI(BB, dl, TII->get(PPC::MFSPR), LoReg).addImm(268);
    BuildMI(BB, dl, TII->get(PPC::MFSPR), ReadAgainReg).addImm(269);

    unsigned CmpReg = RegInfo.createVirtualRegister(&PPC::CRRCRegClass);

    BuildMI(BB, dl, TII->get(PPC::CMPW), CmpReg)
        .addReg(HiReg)
        .addReg(ReadAgainReg);
    BuildMI(BB, dl, TII->get(PPC::BCC))
        .addImm(PPC::PRED_NE)
        .addReg(CmpReg)
        .addMBB(readMBB);

    BB->addSuccessor(readMBB);
    BB->addSuccessor(sinkMBB);
  } else if (MI.getOpcode() == PPC::ATOMIC_LOAD_ADD_I8)
    BB = EmitPartwordAtomicBinary(MI, BB, true, PPC::ADD4);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_ADD_I16)
    BB = EmitPartwordAtomicBinary(MI, BB, false, PPC::ADD4);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_ADD_I32)
    BB = EmitAtomicBinary(MI, BB, 4, PPC::ADD4);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_ADD_I64)
    BB = EmitAtomicBinary(MI, BB, 8, PPC::ADD8);

  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_AND_I8)
    BB = EmitPartwordAtomicBinary(MI, BB, true, PPC::AND);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_AND_I16)
    BB = EmitPartwordAtomicBinary(MI, BB, false, PPC::AND);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_AND_I32)
    BB = EmitAtomicBinary(MI, BB, 4, PPC::AND);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_AND_I64)
    BB = EmitAtomicBinary(MI, BB, 8, PPC::AND8);

  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_OR_I8)
    BB = EmitPartwordAtomicBinary(MI, BB, true, PPC::OR);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_OR_I16)
    BB = EmitPartwordAtomicBinary(MI, BB, false, PPC::OR);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_OR_I32)
    BB = EmitAtomicBinary(MI, BB, 4, PPC::OR);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_OR_I64)
    BB = EmitAtomicBinary(MI, BB, 8, PPC::OR8);

  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_XOR_I8)
    BB = EmitPartwordAtomicBinary(MI, BB, true, PPC::XOR);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_XOR_I16)
    BB = EmitPartwordAtomicBinary(MI, BB, false, PPC::XOR);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_XOR_I32)
    BB = EmitAtomicBinary(MI, BB, 4, PPC::XOR);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_XOR_I64)
    BB = EmitAtomicBinary(MI, BB, 8, PPC::XOR8);

  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_NAND_I8)
    BB = EmitPartwordAtomicBinary(MI, BB, true, PPC::NAND);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_NAND_I16)
    BB = EmitPartwordAtomicBinary(MI, BB, false, PPC::NAND);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_NAND_I32)
    BB = EmitAtomicBinary(MI, BB, 4, PPC::NAND);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_NAND_I64)
    BB = EmitAtomicBinary(MI, BB, 8, PPC::NAND8);

  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_SUB_I8)
    BB = EmitPartwordAtomicBinary(MI, BB, true, PPC::SUBF);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_SUB_I16)
    BB = EmitPartwordAtomicBinary(MI, BB, false, PPC::SUBF);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_SUB_I32)
    BB = EmitAtomicBinary(MI, BB, 4, PPC::SUBF);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_SUB_I64)
    BB = EmitAtomicBinary(MI, BB, 8, PPC::SUBF8);

  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_MIN_I8)
    BB = EmitPartwordAtomicBinary(MI, BB, true, 0, PPC::CMPW, PPC::PRED_GE);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_MIN_I16)
    BB = EmitPartwordAtomicBinary(MI, BB, false, 0, PPC::CMPW, PPC::PRED_GE);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_MIN_I32)
    BB = EmitAtomicBinary(MI, BB, 4, 0, PPC::CMPW, PPC::PRED_GE);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_MIN_I64)
    BB = EmitAtomicBinary(MI, BB, 8, 0, PPC::CMPD, PPC::PRED_GE);

  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_MAX_I8)
    BB = EmitPartwordAtomicBinary(MI, BB, true, 0, PPC::CMPW, PPC::PRED_LE);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_MAX_I16)
    BB = EmitPartwordAtomicBinary(MI, BB, false, 0, PPC::CMPW, PPC::PRED_LE);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_MAX_I32)
    BB = EmitAtomicBinary(MI, BB, 4, 0, PPC::CMPW, PPC::PRED_LE);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_MAX_I64)
    BB = EmitAtomicBinary(MI, BB, 8, 0, PPC::CMPD, PPC::PRED_LE);

  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_UMIN_I8)
    BB = EmitPartwordAtomicBinary(MI, BB, true, 0, PPC::CMPLW, PPC::PRED_GE);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_UMIN_I16)
    BB = EmitPartwordAtomicBinary(MI, BB, false, 0, PPC::CMPLW, PPC::PRED_GE);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_UMIN_I32)
    BB = EmitAtomicBinary(MI, BB, 4, 0, PPC::CMPLW, PPC::PRED_GE);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_UMIN_I64)
    BB = EmitAtomicBinary(MI, BB, 8, 0, PPC::CMPLD, PPC::PRED_GE);

  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_UMAX_I8)
    BB = EmitPartwordAtomicBinary(MI, BB, true, 0, PPC::CMPLW, PPC::PRED_LE);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_UMAX_I16)
    BB = EmitPartwordAtomicBinary(MI, BB, false, 0, PPC::CMPLW, PPC::PRED_LE);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_UMAX_I32)
    BB = EmitAtomicBinary(MI, BB, 4, 0, PPC::CMPLW, PPC::PRED_LE);
  else if (MI.getOpcode() == PPC::ATOMIC_LOAD_UMAX_I64)
    BB = EmitAtomicBinary(MI, BB, 8, 0, PPC::CMPLD, PPC::PRED_LE);

  else if (MI.getOpcode() == PPC::ATOMIC_SWAP_I8)
    BB = EmitPartwordAtomicBinary(MI, BB, true, 0);
  else if (MI.getOpcode() == PPC::ATOMIC_SWAP_I16)
    BB = EmitPartwordAtomicBinary(MI, BB, false, 0);
  else if (MI.getOpcode() == PPC::ATOMIC_SWAP_I32)
    BB = EmitAtomicBinary(MI, BB, 4, 0);
  else if (MI.getOpcode() == PPC::ATOMIC_SWAP_I64)
    BB = EmitAtomicBinary(MI, BB, 8, 0);
  else if (MI.getOpcode() == PPC::ATOMIC_CMP_SWAP_I32 ||
           MI.getOpcode() == PPC::ATOMIC_CMP_SWAP_I64 ||
           (Subtarget.hasPartwordAtomics() &&
            MI.getOpcode() == PPC::ATOMIC_CMP_SWAP_I8) ||
           (Subtarget.hasPartwordAtomics() &&
            MI.getOpcode() == PPC::ATOMIC_CMP_SWAP_I16)) {
    bool is64bit = MI.getOpcode() == PPC::ATOMIC_CMP_SWAP_I64;

    auto LoadMnemonic = PPC::LDARX;
    auto StoreMnemonic = PPC::STDCX;
    switch (MI.getOpcode()) {
    default:
      llvm_unreachable("Compare and swap of unknown size");
    case PPC::ATOMIC_CMP_SWAP_I8:
      LoadMnemonic = PPC::LBARX;
      StoreMnemonic = PPC::STBCX;
      assert(Subtarget.hasPartwordAtomics() && "No support partword atomics.");
      break;
    case PPC::ATOMIC_CMP_SWAP_I16:
      LoadMnemonic = PPC::LHARX;
      StoreMnemonic = PPC::STHCX;
      assert(Subtarget.hasPartwordAtomics() && "No support partword atomics.");
      break;
    case PPC::ATOMIC_CMP_SWAP_I32:
      LoadMnemonic = PPC::LWARX;
      StoreMnemonic = PPC::STWCX;
      break;
    case PPC::ATOMIC_CMP_SWAP_I64:
      LoadMnemonic = PPC::LDARX;
      StoreMnemonic = PPC::STDCX;
      break;
    }
    unsigned dest = MI.getOperand(0).getReg();
    unsigned ptrA = MI.getOperand(1).getReg();
    unsigned ptrB = MI.getOperand(2).getReg();
    unsigned oldval = MI.getOperand(3).getReg();
    unsigned newval = MI.getOperand(4).getReg();
    DebugLoc dl = MI.getDebugLoc();

    MachineBasicBlock *loop1MBB = F->CreateMachineBasicBlock(LLVM_BB);
    MachineBasicBlock *loop2MBB = F->CreateMachineBasicBlock(LLVM_BB);
    MachineBasicBlock *midMBB = F->CreateMachineBasicBlock(LLVM_BB);
    MachineBasicBlock *exitMBB = F->CreateMachineBasicBlock(LLVM_BB);
    F->insert(It, loop1MBB);
    F->insert(It, loop2MBB);
    F->insert(It, midMBB);
    F->insert(It, exitMBB);
    exitMBB->splice(exitMBB->begin(), BB,
                    std::next(MachineBasicBlock::iterator(MI)), BB->end());
    exitMBB->transferSuccessorsAndUpdatePHIs(BB);

    //  thisMBB:
    //   ...
    //   fallthrough --> loopMBB
    BB->addSuccessor(loop1MBB);

    // loop1MBB:
    //   l[bhwd]arx dest, ptr
    //   cmp[wd] dest, oldval
    //   bne- midMBB
    // loop2MBB:
    //   st[bhwd]cx. newval, ptr
    //   bne- loopMBB
    //   b exitBB
    // midMBB:
    //   st[bhwd]cx. dest, ptr
    // exitBB:
    BB = loop1MBB;
    BuildMI(BB, dl, TII->get(LoadMnemonic), dest).addReg(ptrA).addReg(ptrB);
    BuildMI(BB, dl, TII->get(is64bit ? PPC::CMPD : PPC::CMPW), PPC::CR0)
        .addReg(oldval)
        .addReg(dest);
    BuildMI(BB, dl, TII->get(PPC::BCC))
        .addImm(PPC::PRED_NE)
        .addReg(PPC::CR0)
        .addMBB(midMBB);
    BB->addSuccessor(loop2MBB);
    BB->addSuccessor(midMBB);

    BB = loop2MBB;
    BuildMI(BB, dl, TII->get(StoreMnemonic))
        .addReg(newval)
        .addReg(ptrA)
        .addReg(ptrB);
    BuildMI(BB, dl, TII->get(PPC::BCC))
        .addImm(PPC::PRED_NE)
        .addReg(PPC::CR0)
        .addMBB(loop1MBB);
    BuildMI(BB, dl, TII->get(PPC::B)).addMBB(exitMBB);
    BB->addSuccessor(loop1MBB);
    BB->addSuccessor(exitMBB);

    BB = midMBB;
    BuildMI(BB, dl, TII->get(StoreMnemonic))
        .addReg(dest)
        .addReg(ptrA)
        .addReg(ptrB);
    BB->addSuccessor(exitMBB);

    //  exitMBB:
    //   ...
    BB = exitMBB;
  } else if (MI.getOpcode() == PPC::ATOMIC_CMP_SWAP_I8 ||
             MI.getOpcode() == PPC::ATOMIC_CMP_SWAP_I16) {
    // We must use 64-bit registers for addresses when targeting 64-bit,
    // since we're actually doing arithmetic on them.  Other registers
    // can be 32-bit.
    bool is64bit = Subtarget.isPPC64();
    bool isLittleEndian = Subtarget.isLittleEndian();
    bool is8bit = MI.getOpcode() == PPC::ATOMIC_CMP_SWAP_I8;

    unsigned dest = MI.getOperand(0).getReg();
    unsigned ptrA = MI.getOperand(1).getReg();
    unsigned ptrB = MI.getOperand(2).getReg();
    unsigned oldval = MI.getOperand(3).getReg();
    unsigned newval = MI.getOperand(4).getReg();
    DebugLoc dl = MI.getDebugLoc();

    MachineBasicBlock *loop1MBB = F->CreateMachineBasicBlock(LLVM_BB);
    MachineBasicBlock *loop2MBB = F->CreateMachineBasicBlock(LLVM_BB);
    MachineBasicBlock *midMBB = F->CreateMachineBasicBlock(LLVM_BB);
    MachineBasicBlock *exitMBB = F->CreateMachineBasicBlock(LLVM_BB);
    F->insert(It, loop1MBB);
    F->insert(It, loop2MBB);
    F->insert(It, midMBB);
    F->insert(It, exitMBB);
    exitMBB->splice(exitMBB->begin(), BB,
                    std::next(MachineBasicBlock::iterator(MI)), BB->end());
    exitMBB->transferSuccessorsAndUpdatePHIs(BB);

    MachineRegisterInfo &RegInfo = F->getRegInfo();
    const TargetRegisterClass *RC =
        is64bit ? &PPC::G8RCRegClass : &PPC::GPRCRegClass;
    const TargetRegisterClass *GPRC = &PPC::GPRCRegClass;

    unsigned PtrReg = RegInfo.createVirtualRegister(RC);
    unsigned Shift1Reg = RegInfo.createVirtualRegister(GPRC);
    unsigned ShiftReg =
        isLittleEndian ? Shift1Reg : RegInfo.createVirtualRegister(GPRC);
    unsigned NewVal2Reg = RegInfo.createVirtualRegister(GPRC);
    unsigned NewVal3Reg = RegInfo.createVirtualRegister(GPRC);
    unsigned OldVal2Reg = RegInfo.createVirtualRegister(GPRC);
    unsigned OldVal3Reg = RegInfo.createVirtualRegister(GPRC);
    unsigned MaskReg = RegInfo.createVirtualRegister(GPRC);
    unsigned Mask2Reg = RegInfo.createVirtualRegister(GPRC);
    unsigned Mask3Reg = RegInfo.createVirtualRegister(GPRC);
    unsigned Tmp2Reg = RegInfo.createVirtualRegister(GPRC);
    unsigned Tmp4Reg = RegInfo.createVirtualRegister(GPRC);
    unsigned TmpDestReg = RegInfo.createVirtualRegister(GPRC);
    unsigned Ptr1Reg;
    unsigned TmpReg = RegInfo.createVirtualRegister(GPRC);
    unsigned ZeroReg = is64bit ? PPC::ZERO8 : PPC::ZERO;
    //  thisMBB:
    //   ...
    //   fallthrough --> loopMBB
    BB->addSuccessor(loop1MBB);

    // The 4-byte load must be aligned, while a char or short may be
    // anywhere in the word.  Hence all this nasty bookkeeping code.
    //   add ptr1, ptrA, ptrB [copy if ptrA==0]
    //   rlwinm shift1, ptr1, 3, 27, 28 [3, 27, 27]
    //   xori shift, shift1, 24 [16]
    //   rlwinm ptr, ptr1, 0, 0, 29
    //   slw newval2, newval, shift
    //   slw oldval2, oldval,shift
    //   li mask2, 255 [li mask3, 0; ori mask2, mask3, 65535]
    //   slw mask, mask2, shift
    //   and newval3, newval2, mask
    //   and oldval3, oldval2, mask
    // loop1MBB:
    //   lwarx tmpDest, ptr
    //   and tmp, tmpDest, mask
    //   cmpw tmp, oldval3
    //   bne- midMBB
    // loop2MBB:
    //   andc tmp2, tmpDest, mask
    //   or tmp4, tmp2, newval3
    //   stwcx. tmp4, ptr
    //   bne- loop1MBB
    //   b exitBB
    // midMBB:
    //   stwcx. tmpDest, ptr
    // exitBB:
    //   srw dest, tmpDest, shift
    if (ptrA != ZeroReg) {
      Ptr1Reg = RegInfo.createVirtualRegister(RC);
      BuildMI(BB, dl, TII->get(is64bit ? PPC::ADD8 : PPC::ADD4), Ptr1Reg)
          .addReg(ptrA)
          .addReg(ptrB);
    } else {
      Ptr1Reg = ptrB;
    }

    // We need use 32-bit subregister to avoid mismatch register class in 64-bit
    // mode.
    BuildMI(BB, dl, TII->get(PPC::RLWINM), Shift1Reg)
        .addReg(Ptr1Reg, 0, is64bit ? PPC::sub_32 : 0)
        .addImm(3)
        .addImm(27)
        .addImm(is8bit ? 28 : 27);
    if (!isLittleEndian)
      BuildMI(BB, dl, TII->get(PPC::XORI), ShiftReg)
          .addReg(Shift1Reg)
          .addImm(is8bit ? 24 : 16);
    if (is64bit)
      BuildMI(BB, dl, TII->get(PPC::RLDICR), PtrReg)
          .addReg(Ptr1Reg)
          .addImm(0)
          .addImm(61);
    else
      BuildMI(BB, dl, TII->get(PPC::RLWINM), PtrReg)
          .addReg(Ptr1Reg)
          .addImm(0)
          .addImm(0)
          .addImm(29);
    BuildMI(BB, dl, TII->get(PPC::SLW), NewVal2Reg)
        .addReg(newval)
        .addReg(ShiftReg);
    BuildMI(BB, dl, TII->get(PPC::SLW), OldVal2Reg)
        .addReg(oldval)
        .addReg(ShiftReg);
    if (is8bit)
      BuildMI(BB, dl, TII->get(PPC::LI), Mask2Reg).addImm(255);
    else {
      BuildMI(BB, dl, TII->get(PPC::LI), Mask3Reg).addImm(0);
      BuildMI(BB, dl, TII->get(PPC::ORI), Mask2Reg)
          .addReg(Mask3Reg)
          .addImm(65535);
    }
    BuildMI(BB, dl, TII->get(PPC::SLW), MaskReg)
        .addReg(Mask2Reg)
        .addReg(ShiftReg);
    BuildMI(BB, dl, TII->get(PPC::AND), NewVal3Reg)
        .addReg(NewVal2Reg)
        .addReg(MaskReg);
    BuildMI(BB, dl, TII->get(PPC::AND), OldVal3Reg)
        .addReg(OldVal2Reg)
        .addReg(MaskReg);

    BB = loop1MBB;
    BuildMI(BB, dl, TII->get(PPC::LWARX), TmpDestReg)
        .addReg(ZeroReg)
        .addReg(PtrReg);
    BuildMI(BB, dl, TII->get(PPC::AND), TmpReg)
        .addReg(TmpDestReg)
        .addReg(MaskReg);
    BuildMI(BB, dl, TII->get(PPC::CMPW), PPC::CR0)
        .addReg(TmpReg)
        .addReg(OldVal3Reg);
    BuildMI(BB, dl, TII->get(PPC::BCC))
        .addImm(PPC::PRED_NE)
        .addReg(PPC::CR0)
        .addMBB(midMBB);
    BB->addSuccessor(loop2MBB);
    BB->addSuccessor(midMBB);

    BB = loop2MBB;
    BuildMI(BB, dl, TII->get(PPC::ANDC), Tmp2Reg)
        .addReg(TmpDestReg)
        .addReg(MaskReg);
    BuildMI(BB, dl, TII->get(PPC::OR), Tmp4Reg)
        .addReg(Tmp2Reg)
        .addReg(NewVal3Reg);
    BuildMI(BB, dl, TII->get(PPC::STWCX))
        .addReg(Tmp4Reg)
        .addReg(ZeroReg)
        .addReg(PtrReg);
    BuildMI(BB, dl, TII->get(PPC::BCC))
        .addImm(PPC::PRED_NE)
        .addReg(PPC::CR0)
        .addMBB(loop1MBB);
    BuildMI(BB, dl, TII->get(PPC::B)).addMBB(exitMBB);
    BB->addSuccessor(loop1MBB);
    BB->addSuccessor(exitMBB);

    BB = midMBB;
    BuildMI(BB, dl, TII->get(PPC::STWCX))
        .addReg(TmpDestReg)
        .addReg(ZeroReg)
        .addReg(PtrReg);
    BB->addSuccessor(exitMBB);

    //  exitMBB:
    //   ...
    BB = exitMBB;
    BuildMI(*BB, BB->begin(), dl, TII->get(PPC::SRW), dest)
        .addReg(TmpReg)
        .addReg(ShiftReg);
  } else if (MI.getOpcode() == PPC::FADDrtz) {
    // This pseudo performs an FADD with rounding mode temporarily forced
    // to round-to-zero.  We emit this via custom inserter since the FPSCR
    // is not modeled at the SelectionDAG level.
    unsigned Dest = MI.getOperand(0).getReg();
    unsigned Src1 = MI.getOperand(1).getReg();
    unsigned Src2 = MI.getOperand(2).getReg();
    DebugLoc dl = MI.getDebugLoc();

    MachineRegisterInfo &RegInfo = F->getRegInfo();
    unsigned MFFSReg = RegInfo.createVirtualRegister(&PPC::F8RCRegClass);

    // Save FPSCR value.
    BuildMI(*BB, MI, dl, TII->get(PPC::MFFS), MFFSReg);

    // Set rounding mode to round-to-zero.
    BuildMI(*BB, MI, dl, TII->get(PPC::MTFSB1)).addImm(31);
    BuildMI(*BB, MI, dl, TII->get(PPC::MTFSB0)).addImm(30);

    // Perform addition.
    BuildMI(*BB, MI, dl, TII->get(PPC::FADD), Dest).addReg(Src1).addReg(Src2);

    // Restore FPSCR value.
    BuildMI(*BB, MI, dl, TII->get(PPC::MTFSFb)).addImm(1).addReg(MFFSReg);
  } else if (MI.getOpcode() == PPC::ANDIo_1_EQ_BIT ||
             MI.getOpcode() == PPC::ANDIo_1_GT_BIT ||
             MI.getOpcode() == PPC::ANDIo_1_EQ_BIT8 ||
             MI.getOpcode() == PPC::ANDIo_1_GT_BIT8) {
    unsigned Opcode = (MI.getOpcode() == PPC::ANDIo_1_EQ_BIT8 ||
                       MI.getOpcode() == PPC::ANDIo_1_GT_BIT8)
                          ? PPC::ANDIo8
                          : PPC::ANDIo;
    bool isEQ = (MI.getOpcode() == PPC::ANDIo_1_EQ_BIT ||
                 MI.getOpcode() == PPC::ANDIo_1_EQ_BIT8);

    MachineRegisterInfo &RegInfo = F->getRegInfo();
    unsigned Dest = RegInfo.createVirtualRegister(
        Opcode == PPC::ANDIo ? &PPC::GPRCRegClass : &PPC::G8RCRegClass);

    DebugLoc dl = MI.getDebugLoc();
    BuildMI(*BB, MI, dl, TII->get(Opcode), Dest)
        .addReg(MI.getOperand(1).getReg())
        .addImm(1);
    BuildMI(*BB, MI, dl, TII->get(TargetOpcode::COPY),
            MI.getOperand(0).getReg())
        .addReg(isEQ ? PPC::CR0EQ : PPC::CR0GT);
  } else if (MI.getOpcode() == PPC::TCHECK_RET) {
    DebugLoc Dl = MI.getDebugLoc();
    MachineRegisterInfo &RegInfo = F->getRegInfo();
    unsigned CRReg = RegInfo.createVirtualRegister(&PPC::CRRCRegClass);
    BuildMI(*BB, MI, Dl, TII->get(PPC::TCHECK), CRReg);
    return BB;
  } else {
    llvm_unreachable("Unexpected instr type to insert");
  }

  MI.eraseFromParent(); // The pseudo instruction is gone now.
  return BB;
}

//===----------------------------------------------------------------------===//
// Target Optimization Hooks
//===----------------------------------------------------------------------===//

static int getEstimateRefinementSteps(EVT VT, const PPCSubtarget &Subtarget) {
  // For the estimates, convergence is quadratic, so we essentially double the
  // number of digits correct after every iteration. For both FRE and FRSQRTE,
  // the minimum architected relative accuracy is 2^-5. When hasRecipPrec(),
  // this is 2^-14. IEEE float has 23 digits and double has 52 digits.
  int RefinementSteps = Subtarget.hasRecipPrec() ? 1 : 3;
  if (VT.getScalarType() == MVT::f64)
    RefinementSteps++;
  return RefinementSteps;
}

SDValue PPCTargetLowering::getSqrtEstimate(SDValue Operand, SelectionDAG &DAG,
                                           int Enabled, int &RefinementSteps,
                                           bool &UseOneConstNR,
                                           bool Reciprocal) const {
  EVT VT = Operand.getValueType();
  if ((VT == MVT::f32 && Subtarget.hasFRSQRTES()) ||
      (VT == MVT::f64 && Subtarget.hasFRSQRTE()) ||
      (VT == MVT::v4f32 && Subtarget.hasAltivec()) ||
      (VT == MVT::v2f64 && Subtarget.hasVSX()) ||
      (VT == MVT::v4f32 && Subtarget.hasQPX()) ||
      (VT == MVT::v4f64 && Subtarget.hasQPX())) {
    if (RefinementSteps == ReciprocalEstimate::Unspecified)
      RefinementSteps = getEstimateRefinementSteps(VT, Subtarget);

    UseOneConstNR = true;
    return DAG.getNode(PPCISD::FRSQRTE, SDLoc(Operand), VT, Operand);
  }
  return SDValue();
}

SDValue PPCTargetLowering::getRecipEstimate(SDValue Operand, SelectionDAG &DAG,
                                            int Enabled,
                                            int &RefinementSteps) const {
  EVT VT = Operand.getValueType();
  if ((VT == MVT::f32 && Subtarget.hasFRES()) ||
      (VT == MVT::f64 && Subtarget.hasFRE()) ||
      (VT == MVT::v4f32 && Subtarget.hasAltivec()) ||
      (VT == MVT::v2f64 && Subtarget.hasVSX()) ||
      (VT == MVT::v4f32 && Subtarget.hasQPX()) ||
      (VT == MVT::v4f64 && Subtarget.hasQPX())) {
    if (RefinementSteps == ReciprocalEstimate::Unspecified)
      RefinementSteps = getEstimateRefinementSteps(VT, Subtarget);
    return DAG.getNode(PPCISD::FRE, SDLoc(Operand), VT, Operand);
  }
  return SDValue();
}

unsigned PPCTargetLowering::combineRepeatedFPDivisors() const {
  // Note: This functionality is used only when unsafe-fp-math is enabled, and
  // on cores with reciprocal estimates (which are used when unsafe-fp-math is
  // enabled for division), this functionality is redundant with the default
  // combiner logic (once the division -> reciprocal/multiply transformation
  // has taken place). As a result, this matters more for older cores than for
  // newer ones.

  // Combine multiple FDIVs with the same divisor into multiple FMULs by the
  // reciprocal if there are two or more FDIVs (for embedded cores with only
  // one FP pipeline) for three or more FDIVs (for generic OOO cores).
  switch (Subtarget.getDarwinDirective()) {
  default:
    return 3;
  case PPC::DIR_440:
  case PPC::DIR_A2:
  case PPC::DIR_E500:
  case PPC::DIR_E500mc:
  case PPC::DIR_E5500:
    return 2;
  }
}

// isConsecutiveLSLoc needs to work even if all adds have not yet been
// collapsed, and so we need to look through chains of them.
static void getBaseWithConstantOffset(SDValue Loc, SDValue &Base,
                                     int64_t& Offset, SelectionDAG &DAG) {
  if (DAG.isBaseWithConstantOffset(Loc)) {
    Base = Loc.getOperand(0);
    Offset += cast<ConstantSDNode>(Loc.getOperand(1))->getSExtValue();

    // The base might itself be a base plus an offset, and if so, accumulate
    // that as well.
    getBaseWithConstantOffset(Loc.getOperand(0), Base, Offset, DAG);
  }
}

static bool isConsecutiveLSLoc(SDValue Loc, EVT VT, LSBaseSDNode *Base,
                            unsigned Bytes, int Dist,
                            SelectionDAG &DAG) {
  if (VT.getSizeInBits() / 8 != Bytes)
    return false;

  SDValue BaseLoc = Base->getBasePtr();
  if (Loc.getOpcode() == ISD::FrameIndex) {
    if (BaseLoc.getOpcode() != ISD::FrameIndex)
      return false;
    const MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
    int FI  = cast<FrameIndexSDNode>(Loc)->getIndex();
    int BFI = cast<FrameIndexSDNode>(BaseLoc)->getIndex();
    int FS  = MFI.getObjectSize(FI);
    int BFS = MFI.getObjectSize(BFI);
    if (FS != BFS || FS != (int)Bytes) return false;
    return MFI.getObjectOffset(FI) == (MFI.getObjectOffset(BFI) + Dist*Bytes);
  }

  SDValue Base1 = Loc, Base2 = BaseLoc;
  int64_t Offset1 = 0, Offset2 = 0;
  getBaseWithConstantOffset(Loc, Base1, Offset1, DAG);
  getBaseWithConstantOffset(BaseLoc, Base2, Offset2, DAG);
  if (Base1 == Base2 && Offset1 == (Offset2 + Dist * Bytes))
    return true;

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  const GlobalValue *GV1 = nullptr;
  const GlobalValue *GV2 = nullptr;
  Offset1 = 0;
  Offset2 = 0;
  bool isGA1 = TLI.isGAPlusOffset(Loc.getNode(), GV1, Offset1);
  bool isGA2 = TLI.isGAPlusOffset(BaseLoc.getNode(), GV2, Offset2);
  if (isGA1 && isGA2 && GV1 == GV2)
    return Offset1 == (Offset2 + Dist*Bytes);
  return false;
}

// Like SelectionDAG::isConsecutiveLoad, but also works for stores, and does
// not enforce equality of the chain operands.
static bool isConsecutiveLS(SDNode *N, LSBaseSDNode *Base,
                            unsigned Bytes, int Dist,
                            SelectionDAG &DAG) {
  if (LSBaseSDNode *LS = dyn_cast<LSBaseSDNode>(N)) {
    EVT VT = LS->getMemoryVT();
    SDValue Loc = LS->getBasePtr();
    return isConsecutiveLSLoc(Loc, VT, Base, Bytes, Dist, DAG);
  }

  if (N->getOpcode() == ISD::INTRINSIC_W_CHAIN) {
    EVT VT;
    switch (cast<ConstantSDNode>(N->getOperand(1))->getZExtValue()) {
    default: return false;
    case Intrinsic::ppc_qpx_qvlfd:
    case Intrinsic::ppc_qpx_qvlfda:
      VT = MVT::v4f64;
      break;
    case Intrinsic::ppc_qpx_qvlfs:
    case Intrinsic::ppc_qpx_qvlfsa:
      VT = MVT::v4f32;
      break;
    case Intrinsic::ppc_qpx_qvlfcd:
    case Intrinsic::ppc_qpx_qvlfcda:
      VT = MVT::v2f64;
      break;
    case Intrinsic::ppc_qpx_qvlfcs:
    case Intrinsic::ppc_qpx_qvlfcsa:
      VT = MVT::v2f32;
      break;
    case Intrinsic::ppc_qpx_qvlfiwa:
    case Intrinsic::ppc_qpx_qvlfiwz:
    case Intrinsic::ppc_altivec_lvx:
    case Intrinsic::ppc_altivec_lvxl:
    case Intrinsic::ppc_vsx_lxvw4x:
    case Intrinsic::ppc_vsx_lxvw4x_be:
      VT = MVT::v4i32;
      break;
    case Intrinsic::ppc_vsx_lxvd2x:
    case Intrinsic::ppc_vsx_lxvd2x_be:
      VT = MVT::v2f64;
      break;
    case Intrinsic::ppc_altivec_lvebx:
      VT = MVT::i8;
      break;
    case Intrinsic::ppc_altivec_lvehx:
      VT = MVT::i16;
      break;
    case Intrinsic::ppc_altivec_lvewx:
      VT = MVT::i32;
      break;
    }

    return isConsecutiveLSLoc(N->getOperand(2), VT, Base, Bytes, Dist, DAG);
  }

  if (N->getOpcode() == ISD::INTRINSIC_VOID) {
    EVT VT;
    switch (cast<ConstantSDNode>(N->getOperand(1))->getZExtValue()) {
    default: return false;
    case Intrinsic::ppc_qpx_qvstfd:
    case Intrinsic::ppc_qpx_qvstfda:
      VT = MVT::v4f64;
      break;
    case Intrinsic::ppc_qpx_qvstfs:
    case Intrinsic::ppc_qpx_qvstfsa:
      VT = MVT::v4f32;
      break;
    case Intrinsic::ppc_qpx_qvstfcd:
    case Intrinsic::ppc_qpx_qvstfcda:
      VT = MVT::v2f64;
      break;
    case Intrinsic::ppc_qpx_qvstfcs:
    case Intrinsic::ppc_qpx_qvstfcsa:
      VT = MVT::v2f32;
      break;
    case Intrinsic::ppc_qpx_qvstfiw:
    case Intrinsic::ppc_qpx_qvstfiwa:
    case Intrinsic::ppc_altivec_stvx:
    case Intrinsic::ppc_altivec_stvxl:
    case Intrinsic::ppc_vsx_stxvw4x:
      VT = MVT::v4i32;
      break;
    case Intrinsic::ppc_vsx_stxvd2x:
      VT = MVT::v2f64;
      break;
    case Intrinsic::ppc_vsx_stxvw4x_be:
      VT = MVT::v4i32;
      break;
    case Intrinsic::ppc_vsx_stxvd2x_be:
      VT = MVT::v2f64;
      break;
    case Intrinsic::ppc_altivec_stvebx:
      VT = MVT::i8;
      break;
    case Intrinsic::ppc_altivec_stvehx:
      VT = MVT::i16;
      break;
    case Intrinsic::ppc_altivec_stvewx:
      VT = MVT::i32;
      break;
    }

    return isConsecutiveLSLoc(N->getOperand(3), VT, Base, Bytes, Dist, DAG);
  }

  return false;
}

// Return true is there is a nearyby consecutive load to the one provided
// (regardless of alignment). We search up and down the chain, looking though
// token factors and other loads (but nothing else). As a result, a true result
// indicates that it is safe to create a new consecutive load adjacent to the
// load provided.
static bool findConsecutiveLoad(LoadSDNode *LD, SelectionDAG &DAG) {
  SDValue Chain = LD->getChain();
  EVT VT = LD->getMemoryVT();

  SmallSet<SDNode *, 16> LoadRoots;
  SmallVector<SDNode *, 8> Queue(1, Chain.getNode());
  SmallSet<SDNode *, 16> Visited;

  // First, search up the chain, branching to follow all token-factor operands.
  // If we find a consecutive load, then we're done, otherwise, record all
  // nodes just above the top-level loads and token factors.
  while (!Queue.empty()) {
    SDNode *ChainNext = Queue.pop_back_val();
    if (!Visited.insert(ChainNext).second)
      continue;

    if (MemSDNode *ChainLD = dyn_cast<MemSDNode>(ChainNext)) {
      if (isConsecutiveLS(ChainLD, LD, VT.getStoreSize(), 1, DAG))
        return true;

      if (!Visited.count(ChainLD->getChain().getNode()))
        Queue.push_back(ChainLD->getChain().getNode());
    } else if (ChainNext->getOpcode() == ISD::TokenFactor) {
      for (const SDUse &O : ChainNext->ops())
        if (!Visited.count(O.getNode()))
          Queue.push_back(O.getNode());
    } else
      LoadRoots.insert(ChainNext);
  }

  // Second, search down the chain, starting from the top-level nodes recorded
  // in the first phase. These top-level nodes are the nodes just above all
  // loads and token factors. Starting with their uses, recursively look though
  // all loads (just the chain uses) and token factors to find a consecutive
  // load.
  Visited.clear();
  Queue.clear();

  for (SmallSet<SDNode *, 16>::iterator I = LoadRoots.begin(),
       IE = LoadRoots.end(); I != IE; ++I) {
    Queue.push_back(*I);

    while (!Queue.empty()) {
      SDNode *LoadRoot = Queue.pop_back_val();
      if (!Visited.insert(LoadRoot).second)
        continue;

      if (MemSDNode *ChainLD = dyn_cast<MemSDNode>(LoadRoot))
        if (isConsecutiveLS(ChainLD, LD, VT.getStoreSize(), 1, DAG))
          return true;

      for (SDNode::use_iterator UI = LoadRoot->use_begin(),
           UE = LoadRoot->use_end(); UI != UE; ++UI)
        if (((isa<MemSDNode>(*UI) &&
            cast<MemSDNode>(*UI)->getChain().getNode() == LoadRoot) ||
            UI->getOpcode() == ISD::TokenFactor) && !Visited.count(*UI))
          Queue.push_back(*UI);
    }
  }

  return false;
}

/// This function is called when we have proved that a SETCC node can be replaced
/// by subtraction (and other supporting instructions) so that the result of
/// comparison is kept in a GPR instead of CR. This function is purely for
/// codegen purposes and has some flags to guide the codegen process.
static SDValue generateEquivalentSub(SDNode *N, int Size, bool Complement,
                                     bool Swap, SDLoc &DL, SelectionDAG &DAG) {
  assert(N->getOpcode() == ISD::SETCC && "ISD::SETCC Expected.");

  // Zero extend the operands to the largest legal integer. Originally, they
  // must be of a strictly smaller size.
  auto Op0 = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i64, N->getOperand(0),
                         DAG.getConstant(Size, DL, MVT::i32));
  auto Op1 = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i64, N->getOperand(1),
                         DAG.getConstant(Size, DL, MVT::i32));

  // Swap if needed. Depends on the condition code.
  if (Swap)
    std::swap(Op0, Op1);

  // Subtract extended integers.
  auto SubNode = DAG.getNode(ISD::SUB, DL, MVT::i64, Op0, Op1);

  // Move the sign bit to the least significant position and zero out the rest.
  // Now the least significant bit carries the result of original comparison.
  auto Shifted = DAG.getNode(ISD::SRL, DL, MVT::i64, SubNode,
                             DAG.getConstant(Size - 1, DL, MVT::i32));
  auto Final = Shifted;

  // Complement the result if needed. Based on the condition code.
  if (Complement)
    Final = DAG.getNode(ISD::XOR, DL, MVT::i64, Shifted,
                        DAG.getConstant(1, DL, MVT::i64));

  return DAG.getNode(ISD::TRUNCATE, DL, MVT::i1, Final);
}

SDValue PPCTargetLowering::ConvertSETCCToSubtract(SDNode *N,
                                                  DAGCombinerInfo &DCI) const {
  assert(N->getOpcode() == ISD::SETCC && "ISD::SETCC Expected.");

  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);

  // Size of integers being compared has a critical role in the following
  // analysis, so we prefer to do this when all types are legal.
  if (!DCI.isAfterLegalizeDAG())
    return SDValue();

  // If all users of SETCC extend its value to a legal integer type
  // then we replace SETCC with a subtraction
  for (SDNode::use_iterator UI = N->use_begin(),
       UE = N->use_end(); UI != UE; ++UI) {
    if (UI->getOpcode() != ISD::ZERO_EXTEND)
      return SDValue();
  }

  ISD::CondCode CC = cast<CondCodeSDNode>(N->getOperand(2))->get();
  auto OpSize = N->getOperand(0).getValueSizeInBits();

  unsigned Size = DAG.getDataLayout().getLargestLegalIntTypeSizeInBits();

  if (OpSize < Size) {
    switch (CC) {
    default: break;
    case ISD::SETULT:
      return generateEquivalentSub(N, Size, false, false, DL, DAG);
    case ISD::SETULE:
      return generateEquivalentSub(N, Size, true, true, DL, DAG);
    case ISD::SETUGT:
      return generateEquivalentSub(N, Size, false, true, DL, DAG);
    case ISD::SETUGE:
      return generateEquivalentSub(N, Size, true, false, DL, DAG);
    }
  }

  return SDValue();
}

SDValue PPCTargetLowering::DAGCombineTruncBoolExt(SDNode *N,
                                                  DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  SDLoc dl(N);

  assert(Subtarget.useCRBits() && "Expecting to be tracking CR bits");
  // If we're tracking CR bits, we need to be careful that we don't have:
  //   trunc(binary-ops(zext(x), zext(y)))
  // or
  //   trunc(binary-ops(binary-ops(zext(x), zext(y)), ...)
  // such that we're unnecessarily moving things into GPRs when it would be
  // better to keep them in CR bits.

  // Note that trunc here can be an actual i1 trunc, or can be the effective
  // truncation that comes from a setcc or select_cc.
  if (N->getOpcode() == ISD::TRUNCATE &&
      N->getValueType(0) != MVT::i1)
    return SDValue();

  if (N->getOperand(0).getValueType() != MVT::i32 &&
      N->getOperand(0).getValueType() != MVT::i64)
    return SDValue();

  if (N->getOpcode() == ISD::SETCC ||
      N->getOpcode() == ISD::SELECT_CC) {
    // If we're looking at a comparison, then we need to make sure that the
    // high bits (all except for the first) don't matter the result.
    ISD::CondCode CC =
      cast<CondCodeSDNode>(N->getOperand(
        N->getOpcode() == ISD::SETCC ? 2 : 4))->get();
    unsigned OpBits = N->getOperand(0).getValueSizeInBits();

    if (ISD::isSignedIntSetCC(CC)) {
      if (DAG.ComputeNumSignBits(N->getOperand(0)) != OpBits ||
          DAG.ComputeNumSignBits(N->getOperand(1)) != OpBits)
        return SDValue();
    } else if (ISD::isUnsignedIntSetCC(CC)) {
      if (!DAG.MaskedValueIsZero(N->getOperand(0),
                                 APInt::getHighBitsSet(OpBits, OpBits-1)) ||
          !DAG.MaskedValueIsZero(N->getOperand(1),
                                 APInt::getHighBitsSet(OpBits, OpBits-1)))
        return (N->getOpcode() == ISD::SETCC ? ConvertSETCCToSubtract(N, DCI)
                                             : SDValue());
    } else {
      // This is neither a signed nor an unsigned comparison, just make sure
      // that the high bits are equal.
      KnownBits Op1Known = DAG.computeKnownBits(N->getOperand(0));
      KnownBits Op2Known = DAG.computeKnownBits(N->getOperand(1));

      // We don't really care about what is known about the first bit (if
      // anything), so clear it in all masks prior to comparing them.
      Op1Known.Zero.clearBit(0); Op1Known.One.clearBit(0);
      Op2Known.Zero.clearBit(0); Op2Known.One.clearBit(0);

      if (Op1Known.Zero != Op2Known.Zero || Op1Known.One != Op2Known.One)
        return SDValue();
    }
  }

  // We now know that the higher-order bits are irrelevant, we just need to
  // make sure that all of the intermediate operations are bit operations, and
  // all inputs are extensions.
  if (N->getOperand(0).getOpcode() != ISD::AND &&
      N->getOperand(0).getOpcode() != ISD::OR  &&
      N->getOperand(0).getOpcode() != ISD::XOR &&
      N->getOperand(0).getOpcode() != ISD::SELECT &&
      N->getOperand(0).getOpcode() != ISD::SELECT_CC &&
      N->getOperand(0).getOpcode() != ISD::TRUNCATE &&
      N->getOperand(0).getOpcode() != ISD::SIGN_EXTEND &&
      N->getOperand(0).getOpcode() != ISD::ZERO_EXTEND &&
      N->getOperand(0).getOpcode() != ISD::ANY_EXTEND)
    return SDValue();

  if ((N->getOpcode() == ISD::SETCC || N->getOpcode() == ISD::SELECT_CC) &&
      N->getOperand(1).getOpcode() != ISD::AND &&
      N->getOperand(1).getOpcode() != ISD::OR  &&
      N->getOperand(1).getOpcode() != ISD::XOR &&
      N->getOperand(1).getOpcode() != ISD::SELECT &&
      N->getOperand(1).getOpcode() != ISD::SELECT_CC &&
      N->getOperand(1).getOpcode() != ISD::TRUNCATE &&
      N->getOperand(1).getOpcode() != ISD::SIGN_EXTEND &&
      N->getOperand(1).getOpcode() != ISD::ZERO_EXTEND &&
      N->getOperand(1).getOpcode() != ISD::ANY_EXTEND)
    return SDValue();

  SmallVector<SDValue, 4> Inputs;
  SmallVector<SDValue, 8> BinOps, PromOps;
  SmallPtrSet<SDNode *, 16> Visited;

  for (unsigned i = 0; i < 2; ++i) {
    if (((N->getOperand(i).getOpcode() == ISD::SIGN_EXTEND ||
          N->getOperand(i).getOpcode() == ISD::ZERO_EXTEND ||
          N->getOperand(i).getOpcode() == ISD::ANY_EXTEND) &&
          N->getOperand(i).getOperand(0).getValueType() == MVT::i1) ||
        isa<ConstantSDNode>(N->getOperand(i)))
      Inputs.push_back(N->getOperand(i));
    else
      BinOps.push_back(N->getOperand(i));

    if (N->getOpcode() == ISD::TRUNCATE)
      break;
  }

  // Visit all inputs, collect all binary operations (and, or, xor and
  // select) that are all fed by extensions.
  while (!BinOps.empty()) {
    SDValue BinOp = BinOps.back();
    BinOps.pop_back();

    if (!Visited.insert(BinOp.getNode()).second)
      continue;

    PromOps.push_back(BinOp);

    for (unsigned i = 0, ie = BinOp.getNumOperands(); i != ie; ++i) {
      // The condition of the select is not promoted.
      if (BinOp.getOpcode() == ISD::SELECT && i == 0)
        continue;
      if (BinOp.getOpcode() == ISD::SELECT_CC && i != 2 && i != 3)
        continue;

      if (((BinOp.getOperand(i).getOpcode() == ISD::SIGN_EXTEND ||
            BinOp.getOperand(i).getOpcode() == ISD::ZERO_EXTEND ||
            BinOp.getOperand(i).getOpcode() == ISD::ANY_EXTEND) &&
           BinOp.getOperand(i).getOperand(0).getValueType() == MVT::i1) ||
          isa<ConstantSDNode>(BinOp.getOperand(i))) {
        Inputs.push_back(BinOp.getOperand(i));
      } else if (BinOp.getOperand(i).getOpcode() == ISD::AND ||
                 BinOp.getOperand(i).getOpcode() == ISD::OR  ||
                 BinOp.getOperand(i).getOpcode() == ISD::XOR ||
                 BinOp.getOperand(i).getOpcode() == ISD::SELECT ||
                 BinOp.getOperand(i).getOpcode() == ISD::SELECT_CC ||
                 BinOp.getOperand(i).getOpcode() == ISD::TRUNCATE ||
                 BinOp.getOperand(i).getOpcode() == ISD::SIGN_EXTEND ||
                 BinOp.getOperand(i).getOpcode() == ISD::ZERO_EXTEND ||
                 BinOp.getOperand(i).getOpcode() == ISD::ANY_EXTEND) {
        BinOps.push_back(BinOp.getOperand(i));
      } else {
        // We have an input that is not an extension or another binary
        // operation; we'll abort this transformation.
        return SDValue();
      }
    }
  }

  // Make sure that this is a self-contained cluster of operations (which
  // is not quite the same thing as saying that everything has only one
  // use).
  for (unsigned i = 0, ie = Inputs.size(); i != ie; ++i) {
    if (isa<ConstantSDNode>(Inputs[i]))
      continue;

    for (SDNode::use_iterator UI = Inputs[i].getNode()->use_begin(),
                              UE = Inputs[i].getNode()->use_end();
         UI != UE; ++UI) {
      SDNode *User = *UI;
      if (User != N && !Visited.count(User))
        return SDValue();

      // Make sure that we're not going to promote the non-output-value
      // operand(s) or SELECT or SELECT_CC.
      // FIXME: Although we could sometimes handle this, and it does occur in
      // practice that one of the condition inputs to the select is also one of
      // the outputs, we currently can't deal with this.
      if (User->getOpcode() == ISD::SELECT) {
        if (User->getOperand(0) == Inputs[i])
          return SDValue();
      } else if (User->getOpcode() == ISD::SELECT_CC) {
        if (User->getOperand(0) == Inputs[i] ||
            User->getOperand(1) == Inputs[i])
          return SDValue();
      }
    }
  }

  for (unsigned i = 0, ie = PromOps.size(); i != ie; ++i) {
    for (SDNode::use_iterator UI = PromOps[i].getNode()->use_begin(),
                              UE = PromOps[i].getNode()->use_end();
         UI != UE; ++UI) {
      SDNode *User = *UI;
      if (User != N && !Visited.count(User))
        return SDValue();

      // Make sure that we're not going to promote the non-output-value
      // operand(s) or SELECT or SELECT_CC.
      // FIXME: Although we could sometimes handle this, and it does occur in
      // practice that one of the condition inputs to the select is also one of
      // the outputs, we currently can't deal with this.
      if (User->getOpcode() == ISD::SELECT) {
        if (User->getOperand(0) == PromOps[i])
          return SDValue();
      } else if (User->getOpcode() == ISD::SELECT_CC) {
        if (User->getOperand(0) == PromOps[i] ||
            User->getOperand(1) == PromOps[i])
          return SDValue();
      }
    }
  }

  // Replace all inputs with the extension operand.
  for (unsigned i = 0, ie = Inputs.size(); i != ie; ++i) {
    // Constants may have users outside the cluster of to-be-promoted nodes,
    // and so we need to replace those as we do the promotions.
    if (isa<ConstantSDNode>(Inputs[i]))
      continue;
    else
      DAG.ReplaceAllUsesOfValueWith(Inputs[i], Inputs[i].getOperand(0));
  }

  std::list<HandleSDNode> PromOpHandles;
  for (auto &PromOp : PromOps)
    PromOpHandles.emplace_back(PromOp);

  // Replace all operations (these are all the same, but have a different
  // (i1) return type). DAG.getNode will validate that the types of
  // a binary operator match, so go through the list in reverse so that
  // we've likely promoted both operands first. Any intermediate truncations or
  // extensions disappear.
  while (!PromOpHandles.empty()) {
    SDValue PromOp = PromOpHandles.back().getValue();
    PromOpHandles.pop_back();

    if (PromOp.getOpcode() == ISD::TRUNCATE ||
        PromOp.getOpcode() == ISD::SIGN_EXTEND ||
        PromOp.getOpcode() == ISD::ZERO_EXTEND ||
        PromOp.getOpcode() == ISD::ANY_EXTEND) {
      if (!isa<ConstantSDNode>(PromOp.getOperand(0)) &&
          PromOp.getOperand(0).getValueType() != MVT::i1) {
        // The operand is not yet ready (see comment below).
        PromOpHandles.emplace_front(PromOp);
        continue;
      }

      SDValue RepValue = PromOp.getOperand(0);
      if (isa<ConstantSDNode>(RepValue))
        RepValue = DAG.getNode(ISD::TRUNCATE, dl, MVT::i1, RepValue);

      DAG.ReplaceAllUsesOfValueWith(PromOp, RepValue);
      continue;
    }

    unsigned C;
    switch (PromOp.getOpcode()) {
    default:             C = 0; break;
    case ISD::SELECT:    C = 1; break;
    case ISD::SELECT_CC: C = 2; break;
    }

    if ((!isa<ConstantSDNode>(PromOp.getOperand(C)) &&
         PromOp.getOperand(C).getValueType() != MVT::i1) ||
        (!isa<ConstantSDNode>(PromOp.getOperand(C+1)) &&
         PromOp.getOperand(C+1).getValueType() != MVT::i1)) {
      // The to-be-promoted operands of this node have not yet been
      // promoted (this should be rare because we're going through the
      // list backward, but if one of the operands has several users in
      // this cluster of to-be-promoted nodes, it is possible).
      PromOpHandles.emplace_front(PromOp);
      continue;
    }

    SmallVector<SDValue, 3> Ops(PromOp.getNode()->op_begin(),
                                PromOp.getNode()->op_end());

    // If there are any constant inputs, make sure they're replaced now.
    for (unsigned i = 0; i < 2; ++i)
      if (isa<ConstantSDNode>(Ops[C+i]))
        Ops[C+i] = DAG.getNode(ISD::TRUNCATE, dl, MVT::i1, Ops[C+i]);

    DAG.ReplaceAllUsesOfValueWith(PromOp,
      DAG.getNode(PromOp.getOpcode(), dl, MVT::i1, Ops));
  }

  // Now we're left with the initial truncation itself.
  if (N->getOpcode() == ISD::TRUNCATE)
    return N->getOperand(0);

  // Otherwise, this is a comparison. The operands to be compared have just
  // changed type (to i1), but everything else is the same.
  return SDValue(N, 0);
}

SDValue PPCTargetLowering::DAGCombineExtBoolTrunc(SDNode *N,
                                                  DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  SDLoc dl(N);

  // If we're tracking CR bits, we need to be careful that we don't have:
  //   zext(binary-ops(trunc(x), trunc(y)))
  // or
  //   zext(binary-ops(binary-ops(trunc(x), trunc(y)), ...)
  // such that we're unnecessarily moving things into CR bits that can more
  // efficiently stay in GPRs. Note that if we're not certain that the high
  // bits are set as required by the final extension, we still may need to do
  // some masking to get the proper behavior.

  // This same functionality is important on PPC64 when dealing with
  // 32-to-64-bit extensions; these occur often when 32-bit values are used as
  // the return values of functions. Because it is so similar, it is handled
  // here as well.

  if (N->getValueType(0) != MVT::i32 &&
      N->getValueType(0) != MVT::i64)
    return SDValue();

  if (!((N->getOperand(0).getValueType() == MVT::i1 && Subtarget.useCRBits()) ||
        (N->getOperand(0).getValueType() == MVT::i32 && Subtarget.isPPC64())))
    return SDValue();

  if (N->getOperand(0).getOpcode() != ISD::AND &&
      N->getOperand(0).getOpcode() != ISD::OR  &&
      N->getOperand(0).getOpcode() != ISD::XOR &&
      N->getOperand(0).getOpcode() != ISD::SELECT &&
      N->getOperand(0).getOpcode() != ISD::SELECT_CC)
    return SDValue();

  SmallVector<SDValue, 4> Inputs;
  SmallVector<SDValue, 8> BinOps(1, N->getOperand(0)), PromOps;
  SmallPtrSet<SDNode *, 16> Visited;

  // Visit all inputs, collect all binary operations (and, or, xor and
  // select) that are all fed by truncations.
  while (!BinOps.empty()) {
    SDValue BinOp = BinOps.back();
    BinOps.pop_back();

    if (!Visited.insert(BinOp.getNode()).second)
      continue;

    PromOps.push_back(BinOp);

    for (unsigned i = 0, ie = BinOp.getNumOperands(); i != ie; ++i) {
      // The condition of the select is not promoted.
      if (BinOp.getOpcode() == ISD::SELECT && i == 0)
        continue;
      if (BinOp.getOpcode() == ISD::SELECT_CC && i != 2 && i != 3)
        continue;

      if (BinOp.getOperand(i).getOpcode() == ISD::TRUNCATE ||
          isa<ConstantSDNode>(BinOp.getOperand(i))) {
        Inputs.push_back(BinOp.getOperand(i));
      } else if (BinOp.getOperand(i).getOpcode() == ISD::AND ||
                 BinOp.getOperand(i).getOpcode() == ISD::OR  ||
                 BinOp.getOperand(i).getOpcode() == ISD::XOR ||
                 BinOp.getOperand(i).getOpcode() == ISD::SELECT ||
                 BinOp.getOperand(i).getOpcode() == ISD::SELECT_CC) {
        BinOps.push_back(BinOp.getOperand(i));
      } else {
        // We have an input that is not a truncation or another binary
        // operation; we'll abort this transformation.
        return SDValue();
      }
    }
  }

  // The operands of a select that must be truncated when the select is
  // promoted because the operand is actually part of the to-be-promoted set.
  DenseMap<SDNode *, EVT> SelectTruncOp[2];

  // Make sure that this is a self-contained cluster of operations (which
  // is not quite the same thing as saying that everything has only one
  // use).
  for (unsigned i = 0, ie = Inputs.size(); i != ie; ++i) {
    if (isa<ConstantSDNode>(Inputs[i]))
      continue;

    for (SDNode::use_iterator UI = Inputs[i].getNode()->use_begin(),
                              UE = Inputs[i].getNode()->use_end();
         UI != UE; ++UI) {
      SDNode *User = *UI;
      if (User != N && !Visited.count(User))
        return SDValue();

      // If we're going to promote the non-output-value operand(s) or SELECT or
      // SELECT_CC, record them for truncation.
      if (User->getOpcode() == ISD::SELECT) {
        if (User->getOperand(0) == Inputs[i])
          SelectTruncOp[0].insert(std::make_pair(User,
                                    User->getOperand(0).getValueType()));
      } else if (User->getOpcode() == ISD::SELECT_CC) {
        if (User->getOperand(0) == Inputs[i])
          SelectTruncOp[0].insert(std::make_pair(User,
                                    User->getOperand(0).getValueType()));
        if (User->getOperand(1) == Inputs[i])
          SelectTruncOp[1].insert(std::make_pair(User,
                                    User->getOperand(1).getValueType()));
      }
    }
  }

  for (unsigned i = 0, ie = PromOps.size(); i != ie; ++i) {
    for (SDNode::use_iterator UI = PromOps[i].getNode()->use_begin(),
                              UE = PromOps[i].getNode()->use_end();
         UI != UE; ++UI) {
      SDNode *User = *UI;
      if (User != N && !Visited.count(User))
        return SDValue();

      // If we're going to promote the non-output-value operand(s) or SELECT or
      // SELECT_CC, record them for truncation.
      if (User->getOpcode() == ISD::SELECT) {
        if (User->getOperand(0) == PromOps[i])
          SelectTruncOp[0].insert(std::make_pair(User,
                                    User->getOperand(0).getValueType()));
      } else if (User->getOpcode() == ISD::SELECT_CC) {
        if (User->getOperand(0) == PromOps[i])
          SelectTruncOp[0].insert(std::make_pair(User,
                                    User->getOperand(0).getValueType()));
        if (User->getOperand(1) == PromOps[i])
          SelectTruncOp[1].insert(std::make_pair(User,
                                    User->getOperand(1).getValueType()));
      }
    }
  }

  unsigned PromBits = N->getOperand(0).getValueSizeInBits();
  bool ReallyNeedsExt = false;
  if (N->getOpcode() != ISD::ANY_EXTEND) {
    // If all of the inputs are not already sign/zero extended, then
    // we'll still need to do that at the end.
    for (unsigned i = 0, ie = Inputs.size(); i != ie; ++i) {
      if (isa<ConstantSDNode>(Inputs[i]))
        continue;

      unsigned OpBits =
        Inputs[i].getOperand(0).getValueSizeInBits();
      assert(PromBits < OpBits && "Truncation not to a smaller bit count?");

      if ((N->getOpcode() == ISD::ZERO_EXTEND &&
           !DAG.MaskedValueIsZero(Inputs[i].getOperand(0),
                                  APInt::getHighBitsSet(OpBits,
                                                        OpBits-PromBits))) ||
          (N->getOpcode() == ISD::SIGN_EXTEND &&
           DAG.ComputeNumSignBits(Inputs[i].getOperand(0)) <
             (OpBits-(PromBits-1)))) {
        ReallyNeedsExt = true;
        break;
      }
    }
  }

  // Replace all inputs, either with the truncation operand, or a
  // truncation or extension to the final output type.
  for (unsigned i = 0, ie = Inputs.size(); i != ie; ++i) {
    // Constant inputs need to be replaced with the to-be-promoted nodes that
    // use them because they might have users outside of the cluster of
    // promoted nodes.
    if (isa<ConstantSDNode>(Inputs[i]))
      continue;

    SDValue InSrc = Inputs[i].getOperand(0);
    if (Inputs[i].getValueType() == N->getValueType(0))
      DAG.ReplaceAllUsesOfValueWith(Inputs[i], InSrc);
    else if (N->getOpcode() == ISD::SIGN_EXTEND)
      DAG.ReplaceAllUsesOfValueWith(Inputs[i],
        DAG.getSExtOrTrunc(InSrc, dl, N->getValueType(0)));
    else if (N->getOpcode() == ISD::ZERO_EXTEND)
      DAG.ReplaceAllUsesOfValueWith(Inputs[i],
        DAG.getZExtOrTrunc(InSrc, dl, N->getValueType(0)));
    else
      DAG.ReplaceAllUsesOfValueWith(Inputs[i],
        DAG.getAnyExtOrTrunc(InSrc, dl, N->getValueType(0)));
  }

  std::list<HandleSDNode> PromOpHandles;
  for (auto &PromOp : PromOps)
    PromOpHandles.emplace_back(PromOp);

  // Replace all operations (these are all the same, but have a different
  // (promoted) return type). DAG.getNode will validate that the types of
  // a binary operator match, so go through the list in reverse so that
  // we've likely promoted both operands first.
  while (!PromOpHandles.empty()) {
    SDValue PromOp = PromOpHandles.back().getValue();
    PromOpHandles.pop_back();

    unsigned C;
    switch (PromOp.getOpcode()) {
    default:             C = 0; break;
    case ISD::SELECT:    C = 1; break;
    case ISD::SELECT_CC: C = 2; break;
    }

    if ((!isa<ConstantSDNode>(PromOp.getOperand(C)) &&
         PromOp.getOperand(C).getValueType() != N->getValueType(0)) ||
        (!isa<ConstantSDNode>(PromOp.getOperand(C+1)) &&
         PromOp.getOperand(C+1).getValueType() != N->getValueType(0))) {
      // The to-be-promoted operands of this node have not yet been
      // promoted (this should be rare because we're going through the
      // list backward, but if one of the operands has several users in
      // this cluster of to-be-promoted nodes, it is possible).
      PromOpHandles.emplace_front(PromOp);
      continue;
    }

    // For SELECT and SELECT_CC nodes, we do a similar check for any
    // to-be-promoted comparison inputs.
    if (PromOp.getOpcode() == ISD::SELECT ||
        PromOp.getOpcode() == ISD::SELECT_CC) {
      if ((SelectTruncOp[0].count(PromOp.getNode()) &&
           PromOp.getOperand(0).getValueType() != N->getValueType(0)) ||
          (SelectTruncOp[1].count(PromOp.getNode()) &&
           PromOp.getOperand(1).getValueType() != N->getValueType(0))) {
        PromOpHandles.emplace_front(PromOp);
        continue;
      }
    }

    SmallVector<SDValue, 3> Ops(PromOp.getNode()->op_begin(),
                                PromOp.getNode()->op_end());

    // If this node has constant inputs, then they'll need to be promoted here.
    for (unsigned i = 0; i < 2; ++i) {
      if (!isa<ConstantSDNode>(Ops[C+i]))
        continue;
      if (Ops[C+i].getValueType() == N->getValueType(0))
        continue;

      if (N->getOpcode() == ISD::SIGN_EXTEND)
        Ops[C+i] = DAG.getSExtOrTrunc(Ops[C+i], dl, N->getValueType(0));
      else if (N->getOpcode() == ISD::ZERO_EXTEND)
        Ops[C+i] = DAG.getZExtOrTrunc(Ops[C+i], dl, N->getValueType(0));
      else
        Ops[C+i] = DAG.getAnyExtOrTrunc(Ops[C+i], dl, N->getValueType(0));
    }

    // If we've promoted the comparison inputs of a SELECT or SELECT_CC,
    // truncate them again to the original value type.
    if (PromOp.getOpcode() == ISD::SELECT ||
        PromOp.getOpcode() == ISD::SELECT_CC) {
      auto SI0 = SelectTruncOp[0].find(PromOp.getNode());
      if (SI0 != SelectTruncOp[0].end())
        Ops[0] = DAG.getNode(ISD::TRUNCATE, dl, SI0->second, Ops[0]);
      auto SI1 = SelectTruncOp[1].find(PromOp.getNode());
      if (SI1 != SelectTruncOp[1].end())
        Ops[1] = DAG.getNode(ISD::TRUNCATE, dl, SI1->second, Ops[1]);
    }

    DAG.ReplaceAllUsesOfValueWith(PromOp,
      DAG.getNode(PromOp.getOpcode(), dl, N->getValueType(0), Ops));
  }

  // Now we're left with the initial extension itself.
  if (!ReallyNeedsExt)
    return N->getOperand(0);

  // To zero extend, just mask off everything except for the first bit (in the
  // i1 case).
  if (N->getOpcode() == ISD::ZERO_EXTEND)
    return DAG.getNode(ISD::AND, dl, N->getValueType(0), N->getOperand(0),
                       DAG.getConstant(APInt::getLowBitsSet(
                                         N->getValueSizeInBits(0), PromBits),
                                       dl, N->getValueType(0)));

  assert(N->getOpcode() == ISD::SIGN_EXTEND &&
         "Invalid extension type");
  EVT ShiftAmountTy = getShiftAmountTy(N->getValueType(0), DAG.getDataLayout());
  SDValue ShiftCst =
      DAG.getConstant(N->getValueSizeInBits(0) - PromBits, dl, ShiftAmountTy);
  return DAG.getNode(
      ISD::SRA, dl, N->getValueType(0),
      DAG.getNode(ISD::SHL, dl, N->getValueType(0), N->getOperand(0), ShiftCst),
      ShiftCst);
}

SDValue PPCTargetLowering::combineSetCC(SDNode *N,
                                        DAGCombinerInfo &DCI) const {
  assert(N->getOpcode() == ISD::SETCC &&
         "Should be called with a SETCC node");

  ISD::CondCode CC = cast<CondCodeSDNode>(N->getOperand(2))->get();
  if (CC == ISD::SETNE || CC == ISD::SETEQ) {
    SDValue LHS = N->getOperand(0);
    SDValue RHS = N->getOperand(1);

    // If there is a '0 - y' pattern, canonicalize the pattern to the RHS.
    if (LHS.getOpcode() == ISD::SUB && isNullConstant(LHS.getOperand(0)) &&
        LHS.hasOneUse())
      std::swap(LHS, RHS);

    // x == 0-y --> x+y == 0
    // x != 0-y --> x+y != 0
    if (RHS.getOpcode() == ISD::SUB && isNullConstant(RHS.getOperand(0)) &&
        RHS.hasOneUse()) {
      SDLoc DL(N);
      SelectionDAG &DAG = DCI.DAG;
      EVT VT = N->getValueType(0);
      EVT OpVT = LHS.getValueType();
      SDValue Add = DAG.getNode(ISD::ADD, DL, OpVT, LHS, RHS.getOperand(1));
      return DAG.getSetCC(DL, VT, Add, DAG.getConstant(0, DL, OpVT), CC);
    }
  }

  return DAGCombineTruncBoolExt(N, DCI);
}

// Is this an extending load from an f32 to an f64?
static bool isFPExtLoad(SDValue Op) {
  if (LoadSDNode *LD = dyn_cast<LoadSDNode>(Op.getNode()))
    return LD->getExtensionType() == ISD::EXTLOAD &&
      Op.getValueType() == MVT::f64;
  return false;
}

/// Reduces the number of fp-to-int conversion when building a vector.
///
/// If this vector is built out of floating to integer conversions,
/// transform it to a vector built out of floating point values followed by a
/// single floating to integer conversion of the vector.
/// Namely  (build_vector (fptosi $A), (fptosi $B), ...)
/// becomes (fptosi (build_vector ($A, $B, ...)))
SDValue PPCTargetLowering::
combineElementTruncationToVectorTruncation(SDNode *N,
                                           DAGCombinerInfo &DCI) const {
  assert(N->getOpcode() == ISD::BUILD_VECTOR &&
         "Should be called with a BUILD_VECTOR node");

  SelectionDAG &DAG = DCI.DAG;
  SDLoc dl(N);

  SDValue FirstInput = N->getOperand(0);
  assert(FirstInput.getOpcode() == PPCISD::MFVSR &&
         "The input operand must be an fp-to-int conversion.");

  // This combine happens after legalization so the fp_to_[su]i nodes are
  // already converted to PPCSISD nodes.
  unsigned FirstConversion = FirstInput.getOperand(0).getOpcode();
  if (FirstConversion == PPCISD::FCTIDZ ||
      FirstConversion == PPCISD::FCTIDUZ ||
      FirstConversion == PPCISD::FCTIWZ ||
      FirstConversion == PPCISD::FCTIWUZ) {
    bool IsSplat = true;
    bool Is32Bit = FirstConversion == PPCISD::FCTIWZ ||
      FirstConversion == PPCISD::FCTIWUZ;
    EVT SrcVT = FirstInput.getOperand(0).getValueType();
    SmallVector<SDValue, 4> Ops;
    EVT TargetVT = N->getValueType(0);
    for (int i = 0, e = N->getNumOperands(); i < e; ++i) {
      SDValue NextOp = N->getOperand(i);
      if (NextOp.getOpcode() != PPCISD::MFVSR)
        return SDValue();
      unsigned NextConversion = NextOp.getOperand(0).getOpcode();
      if (NextConversion != FirstConversion)
        return SDValue();
      // If we are converting to 32-bit integers, we need to add an FP_ROUND.
      // This is not valid if the input was originally double precision. It is
      // also not profitable to do unless this is an extending load in which
      // case doing this combine will allow us to combine consecutive loads.
      if (Is32Bit && !isFPExtLoad(NextOp.getOperand(0).getOperand(0)))
        return SDValue();
      if (N->getOperand(i) != FirstInput)
        IsSplat = false;
    }

    // If this is a splat, we leave it as-is since there will be only a single
    // fp-to-int conversion followed by a splat of the integer. This is better
    // for 32-bit and smaller ints and neutral for 64-bit ints.
    if (IsSplat)
      return SDValue();

    // Now that we know we have the right type of node, get its operands
    for (int i = 0, e = N->getNumOperands(); i < e; ++i) {
      SDValue In = N->getOperand(i).getOperand(0);
      if (Is32Bit) {
        // For 32-bit values, we need to add an FP_ROUND node (if we made it
        // here, we know that all inputs are extending loads so this is safe).
        if (In.isUndef())
          Ops.push_back(DAG.getUNDEF(SrcVT));
        else {
          SDValue Trunc = DAG.getNode(ISD::FP_ROUND, dl,
                                      MVT::f32, In.getOperand(0),
                                      DAG.getIntPtrConstant(1, dl));
          Ops.push_back(Trunc);
        }
      } else
        Ops.push_back(In.isUndef() ? DAG.getUNDEF(SrcVT) : In.getOperand(0));
    }

    unsigned Opcode;
    if (FirstConversion == PPCISD::FCTIDZ ||
        FirstConversion == PPCISD::FCTIWZ)
      Opcode = ISD::FP_TO_SINT;
    else
      Opcode = ISD::FP_TO_UINT;

    EVT NewVT = TargetVT == MVT::v2i64 ? MVT::v2f64 : MVT::v4f32;
    SDValue BV = DAG.getBuildVector(NewVT, dl, Ops);
    return DAG.getNode(Opcode, dl, TargetVT, BV);
  }
  return SDValue();
}

/// Reduce the number of loads when building a vector.
///
/// Building a vector out of multiple loads can be converted to a load
/// of the vector type if the loads are consecutive. If the loads are
/// consecutive but in descending order, a shuffle is added at the end
/// to reorder the vector.
static SDValue combineBVOfConsecutiveLoads(SDNode *N, SelectionDAG &DAG) {
  assert(N->getOpcode() == ISD::BUILD_VECTOR &&
         "Should be called with a BUILD_VECTOR node");

  SDLoc dl(N);
  bool InputsAreConsecutiveLoads = true;
  bool InputsAreReverseConsecutive = true;
  unsigned ElemSize = N->getValueType(0).getScalarSizeInBits() / 8;
  SDValue FirstInput = N->getOperand(0);
  bool IsRoundOfExtLoad = false;

  if (FirstInput.getOpcode() == ISD::FP_ROUND &&
      FirstInput.getOperand(0).getOpcode() == ISD::LOAD) {
    LoadSDNode *LD = dyn_cast<LoadSDNode>(FirstInput.getOperand(0));
    IsRoundOfExtLoad = LD->getExtensionType() == ISD::EXTLOAD;
  }
  // Not a build vector of (possibly fp_rounded) loads.
  if ((!IsRoundOfExtLoad && FirstInput.getOpcode() != ISD::LOAD) ||
      N->getNumOperands() == 1)
    return SDValue();

  for (int i = 1, e = N->getNumOperands(); i < e; ++i) {
    // If any inputs are fp_round(extload), they all must be.
    if (IsRoundOfExtLoad && N->getOperand(i).getOpcode() != ISD::FP_ROUND)
      return SDValue();

    SDValue NextInput = IsRoundOfExtLoad ? N->getOperand(i).getOperand(0) :
      N->getOperand(i);
    if (NextInput.getOpcode() != ISD::LOAD)
      return SDValue();

    SDValue PreviousInput =
      IsRoundOfExtLoad ? N->getOperand(i-1).getOperand(0) : N->getOperand(i-1);
    LoadSDNode *LD1 = dyn_cast<LoadSDNode>(PreviousInput);
    LoadSDNode *LD2 = dyn_cast<LoadSDNode>(NextInput);

    // If any inputs are fp_round(extload), they all must be.
    if (IsRoundOfExtLoad && LD2->getExtensionType() != ISD::EXTLOAD)
      return SDValue();

    if (!isConsecutiveLS(LD2, LD1, ElemSize, 1, DAG))
      InputsAreConsecutiveLoads = false;
    if (!isConsecutiveLS(LD1, LD2, ElemSize, 1, DAG))
      InputsAreReverseConsecutive = false;

    // Exit early if the loads are neither consecutive nor reverse consecutive.
    if (!InputsAreConsecutiveLoads && !InputsAreReverseConsecutive)
      return SDValue();
  }

  assert(!(InputsAreConsecutiveLoads && InputsAreReverseConsecutive) &&
         "The loads cannot be both consecutive and reverse consecutive.");

  SDValue FirstLoadOp =
    IsRoundOfExtLoad ? FirstInput.getOperand(0) : FirstInput;
  SDValue LastLoadOp =
    IsRoundOfExtLoad ? N->getOperand(N->getNumOperands()-1).getOperand(0) :
                       N->getOperand(N->getNumOperands()-1);

  LoadSDNode *LD1 = dyn_cast<LoadSDNode>(FirstLoadOp);
  LoadSDNode *LDL = dyn_cast<LoadSDNode>(LastLoadOp);
  if (InputsAreConsecutiveLoads) {
    assert(LD1 && "Input needs to be a LoadSDNode.");
    return DAG.getLoad(N->getValueType(0), dl, LD1->getChain(),
                       LD1->getBasePtr(), LD1->getPointerInfo(),
                       LD1->getAlignment());
  }
  if (InputsAreReverseConsecutive) {
    assert(LDL && "Input needs to be a LoadSDNode.");
    SDValue Load = DAG.getLoad(N->getValueType(0), dl, LDL->getChain(),
                               LDL->getBasePtr(), LDL->getPointerInfo(),
                               LDL->getAlignment());
    SmallVector<int, 16> Ops;
    for (int i = N->getNumOperands() - 1; i >= 0; i--)
      Ops.push_back(i);

    return DAG.getVectorShuffle(N->getValueType(0), dl, Load,
                                DAG.getUNDEF(N->getValueType(0)), Ops);
  }
  return SDValue();
}

// This function adds the required vector_shuffle needed to get
// the elements of the vector extract in the correct position
// as specified by the CorrectElems encoding.
static SDValue addShuffleForVecExtend(SDNode *N, SelectionDAG &DAG,
                                      SDValue Input, uint64_t Elems,
                                      uint64_t CorrectElems) {
  SDLoc dl(N);

  unsigned NumElems = Input.getValueType().getVectorNumElements();
  SmallVector<int, 16> ShuffleMask(NumElems, -1);

  // Knowing the element indices being extracted from the original
  // vector and the order in which they're being inserted, just put
  // them at element indices required for the instruction.
  for (unsigned i = 0; i < N->getNumOperands(); i++) {
    if (DAG.getDataLayout().isLittleEndian())
      ShuffleMask[CorrectElems & 0xF] = Elems & 0xF;
    else
      ShuffleMask[(CorrectElems & 0xF0) >> 4] = (Elems & 0xF0) >> 4;
    CorrectElems = CorrectElems >> 8;
    Elems = Elems >> 8;
  }

  SDValue Shuffle =
      DAG.getVectorShuffle(Input.getValueType(), dl, Input,
                           DAG.getUNDEF(Input.getValueType()), ShuffleMask);

  EVT Ty = N->getValueType(0);
  SDValue BV = DAG.getNode(PPCISD::SExtVElems, dl, Ty, Shuffle);
  return BV;
}

// Look for build vector patterns where input operands come from sign
// extended vector_extract elements of specific indices. If the correct indices
// aren't used, add a vector shuffle to fix up the indices and create a new
// PPCISD:SExtVElems node which selects the vector sign extend instructions
// during instruction selection.
static SDValue combineBVOfVecSExt(SDNode *N, SelectionDAG &DAG) {
  // This array encodes the indices that the vector sign extend instructions
  // extract from when extending from one type to another for both BE and LE.
  // The right nibble of each byte corresponds to the LE incides.
  // and the left nibble of each byte corresponds to the BE incides.
  // For example: 0x3074B8FC  byte->word
  // For LE: the allowed indices are: 0x0,0x4,0x8,0xC
  // For BE: the allowed indices are: 0x3,0x7,0xB,0xF
  // For example: 0x000070F8  byte->double word
  // For LE: the allowed indices are: 0x0,0x8
  // For BE: the allowed indices are: 0x7,0xF
  uint64_t TargetElems[] = {
      0x3074B8FC, // b->w
      0x000070F8, // b->d
      0x10325476, // h->w
      0x00003074, // h->d
      0x00001032, // w->d
  };

  uint64_t Elems = 0;
  int Index;
  SDValue Input;

  auto isSExtOfVecExtract = [&](SDValue Op) -> bool {
    if (!Op)
      return false;
    if (Op.getOpcode() != ISD::SIGN_EXTEND &&
        Op.getOpcode() != ISD::SIGN_EXTEND_INREG)
      return false;

    // A SIGN_EXTEND_INREG might be fed by an ANY_EXTEND to produce a value
    // of the right width.
    SDValue Extract = Op.getOperand(0);
    if (Extract.getOpcode() == ISD::ANY_EXTEND)
      Extract = Extract.getOperand(0);
    if (Extract.getOpcode() != ISD::EXTRACT_VECTOR_ELT)
      return false;

    ConstantSDNode *ExtOp = dyn_cast<ConstantSDNode>(Extract.getOperand(1));
    if (!ExtOp)
      return false;

    Index = ExtOp->getZExtValue();
    if (Input && Input != Extract.getOperand(0))
      return false;

    if (!Input)
      Input = Extract.getOperand(0);

    Elems = Elems << 8;
    Index = DAG.getDataLayout().isLittleEndian() ? Index : Index << 4;
    Elems |= Index;

    return true;
  };

  // If the build vector operands aren't sign extended vector extracts,
  // of the same input vector, then return.
  for (unsigned i = 0; i < N->getNumOperands(); i++) {
    if (!isSExtOfVecExtract(N->getOperand(i))) {
      return SDValue();
    }
  }

  // If the vector extract indicies are not correct, add the appropriate
  // vector_shuffle.
  int TgtElemArrayIdx;
  int InputSize = Input.getValueType().getScalarSizeInBits();
  int OutputSize = N->getValueType(0).getScalarSizeInBits();
  if (InputSize + OutputSize == 40)
    TgtElemArrayIdx = 0;
  else if (InputSize + OutputSize == 72)
    TgtElemArrayIdx = 1;
  else if (InputSize + OutputSize == 48)
    TgtElemArrayIdx = 2;
  else if (InputSize + OutputSize == 80)
    TgtElemArrayIdx = 3;
  else if (InputSize + OutputSize == 96)
    TgtElemArrayIdx = 4;
  else
    return SDValue();

  uint64_t CorrectElems = TargetElems[TgtElemArrayIdx];
  CorrectElems = DAG.getDataLayout().isLittleEndian()
                     ? CorrectElems & 0x0F0F0F0F0F0F0F0F
                     : CorrectElems & 0xF0F0F0F0F0F0F0F0;
  if (Elems != CorrectElems) {
    return addShuffleForVecExtend(N, DAG, Input, Elems, CorrectElems);
  }

  // Regular lowering will catch cases where a shuffle is not needed.
  return SDValue();
}

SDValue PPCTargetLowering::DAGCombineBuildVector(SDNode *N,
                                                 DAGCombinerInfo &DCI) const {
  assert(N->getOpcode() == ISD::BUILD_VECTOR &&
         "Should be called with a BUILD_VECTOR node");

  SelectionDAG &DAG = DCI.DAG;
  SDLoc dl(N);

  if (!Subtarget.hasVSX())
    return SDValue();

  // The target independent DAG combiner will leave a build_vector of
  // float-to-int conversions intact. We can generate MUCH better code for
  // a float-to-int conversion of a vector of floats.
  SDValue FirstInput = N->getOperand(0);
  if (FirstInput.getOpcode() == PPCISD::MFVSR) {
    SDValue Reduced = combineElementTruncationToVectorTruncation(N, DCI);
    if (Reduced)
      return Reduced;
  }

  // If we're building a vector out of consecutive loads, just load that
  // vector type.
  SDValue Reduced = combineBVOfConsecutiveLoads(N, DAG);
  if (Reduced)
    return Reduced;

  // If we're building a vector out of extended elements from another vector
  // we have P9 vector integer extend instructions. The code assumes legal
  // input types (i.e. it can't handle things like v4i16) so do not run before
  // legalization.
  if (Subtarget.hasP9Altivec() && !DCI.isBeforeLegalize()) {
    Reduced = combineBVOfVecSExt(N, DAG);
    if (Reduced)
      return Reduced;
  }


  if (N->getValueType(0) != MVT::v2f64)
    return SDValue();

  // Looking for:
  // (build_vector ([su]int_to_fp (extractelt 0)), [su]int_to_fp (extractelt 1))
  if (FirstInput.getOpcode() != ISD::SINT_TO_FP &&
      FirstInput.getOpcode() != ISD::UINT_TO_FP)
    return SDValue();
  if (N->getOperand(1).getOpcode() != ISD::SINT_TO_FP &&
      N->getOperand(1).getOpcode() != ISD::UINT_TO_FP)
    return SDValue();
  if (FirstInput.getOpcode() != N->getOperand(1).getOpcode())
    return SDValue();

  SDValue Ext1 = FirstInput.getOperand(0);
  SDValue Ext2 = N->getOperand(1).getOperand(0);
  if(Ext1.getOpcode() != ISD::EXTRACT_VECTOR_ELT ||
     Ext2.getOpcode() != ISD::EXTRACT_VECTOR_ELT)
    return SDValue();

  ConstantSDNode *Ext1Op = dyn_cast<ConstantSDNode>(Ext1.getOperand(1));
  ConstantSDNode *Ext2Op = dyn_cast<ConstantSDNode>(Ext2.getOperand(1));
  if (!Ext1Op || !Ext2Op)
    return SDValue();
  if (Ext1.getValueType() != MVT::i32 ||
      Ext2.getValueType() != MVT::i32)
  if (Ext1.getOperand(0) != Ext2.getOperand(0))
    return SDValue();

  int FirstElem = Ext1Op->getZExtValue();
  int SecondElem = Ext2Op->getZExtValue();
  int SubvecIdx;
  if (FirstElem == 0 && SecondElem == 1)
    SubvecIdx = Subtarget.isLittleEndian() ? 1 : 0;
  else if (FirstElem == 2 && SecondElem == 3)
    SubvecIdx = Subtarget.isLittleEndian() ? 0 : 1;
  else
    return SDValue();

  SDValue SrcVec = Ext1.getOperand(0);
  auto NodeType = (N->getOperand(1).getOpcode() == ISD::SINT_TO_FP) ?
    PPCISD::SINT_VEC_TO_FP : PPCISD::UINT_VEC_TO_FP;
  return DAG.getNode(NodeType, dl, MVT::v2f64,
                     SrcVec, DAG.getIntPtrConstant(SubvecIdx, dl));
}

SDValue PPCTargetLowering::combineFPToIntToFP(SDNode *N,
                                              DAGCombinerInfo &DCI) const {
  assert((N->getOpcode() == ISD::SINT_TO_FP ||
          N->getOpcode() == ISD::UINT_TO_FP) &&
         "Need an int -> FP conversion node here");

  if (useSoftFloat() || !Subtarget.has64BitSupport())
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDLoc dl(N);
  SDValue Op(N, 0);

  // Don't handle ppc_fp128 here or conversions that are out-of-range capable
  // from the hardware.
  if (Op.getValueType() != MVT::f32 && Op.getValueType() != MVT::f64)
    return SDValue();
  if (Op.getOperand(0).getValueType().getSimpleVT() <= MVT(MVT::i1) ||
      Op.getOperand(0).getValueType().getSimpleVT() > MVT(MVT::i64))
    return SDValue();

  SDValue FirstOperand(Op.getOperand(0));
  bool SubWordLoad = FirstOperand.getOpcode() == ISD::LOAD &&
    (FirstOperand.getValueType() == MVT::i8 ||
     FirstOperand.getValueType() == MVT::i16);
  if (Subtarget.hasP9Vector() && Subtarget.hasP9Altivec() && SubWordLoad) {
    bool Signed = N->getOpcode() == ISD::SINT_TO_FP;
    bool DstDouble = Op.getValueType() == MVT::f64;
    unsigned ConvOp = Signed ?
      (DstDouble ? PPCISD::FCFID  : PPCISD::FCFIDS) :
      (DstDouble ? PPCISD::FCFIDU : PPCISD::FCFIDUS);
    SDValue WidthConst =
      DAG.getIntPtrConstant(FirstOperand.getValueType() == MVT::i8 ? 1 : 2,
                            dl, false);
    LoadSDNode *LDN = cast<LoadSDNode>(FirstOperand.getNode());
    SDValue Ops[] = { LDN->getChain(), LDN->getBasePtr(), WidthConst };
    SDValue Ld = DAG.getMemIntrinsicNode(PPCISD::LXSIZX, dl,
                                         DAG.getVTList(MVT::f64, MVT::Other),
                                         Ops, MVT::i8, LDN->getMemOperand());

    // For signed conversion, we need to sign-extend the value in the VSR
    if (Signed) {
      SDValue ExtOps[] = { Ld, WidthConst };
      SDValue Ext = DAG.getNode(PPCISD::VEXTS, dl, MVT::f64, ExtOps);
      return DAG.getNode(ConvOp, dl, DstDouble ? MVT::f64 : MVT::f32, Ext);
    } else
      return DAG.getNode(ConvOp, dl, DstDouble ? MVT::f64 : MVT::f32, Ld);
  }


  // For i32 intermediate values, unfortunately, the conversion functions
  // leave the upper 32 bits of the value are undefined. Within the set of
  // scalar instructions, we have no method for zero- or sign-extending the
  // value. Thus, we cannot handle i32 intermediate values here.
  if (Op.getOperand(0).getValueType() == MVT::i32)
    return SDValue();

  assert((Op.getOpcode() == ISD::SINT_TO_FP || Subtarget.hasFPCVT()) &&
         "UINT_TO_FP is supported only with FPCVT");

  // If we have FCFIDS, then use it when converting to single-precision.
  // Otherwise, convert to double-precision and then round.
  unsigned FCFOp = (Subtarget.hasFPCVT() && Op.getValueType() == MVT::f32)
                       ? (Op.getOpcode() == ISD::UINT_TO_FP ? PPCISD::FCFIDUS
                                                            : PPCISD::FCFIDS)
                       : (Op.getOpcode() == ISD::UINT_TO_FP ? PPCISD::FCFIDU
                                                            : PPCISD::FCFID);
  MVT FCFTy = (Subtarget.hasFPCVT() && Op.getValueType() == MVT::f32)
                  ? MVT::f32
                  : MVT::f64;

  // If we're converting from a float, to an int, and back to a float again,
  // then we don't need the store/load pair at all.
  if ((Op.getOperand(0).getOpcode() == ISD::FP_TO_UINT &&
       Subtarget.hasFPCVT()) ||
      (Op.getOperand(0).getOpcode() == ISD::FP_TO_SINT)) {
    SDValue Src = Op.getOperand(0).getOperand(0);
    if (Src.getValueType() == MVT::f32) {
      Src = DAG.getNode(ISD::FP_EXTEND, dl, MVT::f64, Src);
      DCI.AddToWorklist(Src.getNode());
    } else if (Src.getValueType() != MVT::f64) {
      // Make sure that we don't pick up a ppc_fp128 source value.
      return SDValue();
    }

    unsigned FCTOp =
      Op.getOperand(0).getOpcode() == ISD::FP_TO_SINT ? PPCISD::FCTIDZ :
                                                        PPCISD::FCTIDUZ;

    SDValue Tmp = DAG.getNode(FCTOp, dl, MVT::f64, Src);
    SDValue FP = DAG.getNode(FCFOp, dl, FCFTy, Tmp);

    if (Op.getValueType() == MVT::f32 && !Subtarget.hasFPCVT()) {
      FP = DAG.getNode(ISD::FP_ROUND, dl,
                       MVT::f32, FP, DAG.getIntPtrConstant(0, dl));
      DCI.AddToWorklist(FP.getNode());
    }

    return FP;
  }

  return SDValue();
}

// expandVSXLoadForLE - Convert VSX loads (which may be intrinsics for
// builtins) into loads with swaps.
SDValue PPCTargetLowering::expandVSXLoadForLE(SDNode *N,
                                              DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  SDLoc dl(N);
  SDValue Chain;
  SDValue Base;
  MachineMemOperand *MMO;

  switch (N->getOpcode()) {
  default:
    llvm_unreachable("Unexpected opcode for little endian VSX load");
  case ISD::LOAD: {
    LoadSDNode *LD = cast<LoadSDNode>(N);
    Chain = LD->getChain();
    Base = LD->getBasePtr();
    MMO = LD->getMemOperand();
    // If the MMO suggests this isn't a load of a full vector, leave
    // things alone.  For a built-in, we have to make the change for
    // correctness, so if there is a size problem that will be a bug.
    if (MMO->getSize() < 16)
      return SDValue();
    break;
  }
  case ISD::INTRINSIC_W_CHAIN: {
    MemIntrinsicSDNode *Intrin = cast<MemIntrinsicSDNode>(N);
    Chain = Intrin->getChain();
    // Similarly to the store case below, Intrin->getBasePtr() doesn't get
    // us what we want. Get operand 2 instead.
    Base = Intrin->getOperand(2);
    MMO = Intrin->getMemOperand();
    break;
  }
  }

  MVT VecTy = N->getValueType(0).getSimpleVT();

  // Do not expand to PPCISD::LXVD2X + PPCISD::XXSWAPD when the load is
  // aligned and the type is a vector with elements up to 4 bytes
  if (Subtarget.needsSwapsForVSXMemOps() && !(MMO->getAlignment()%16)
      && VecTy.getScalarSizeInBits() <= 32 ) {
    return SDValue();
  }

  SDValue LoadOps[] = { Chain, Base };
  SDValue Load = DAG.getMemIntrinsicNode(PPCISD::LXVD2X, dl,
                                         DAG.getVTList(MVT::v2f64, MVT::Other),
                                         LoadOps, MVT::v2f64, MMO);

  DCI.AddToWorklist(Load.getNode());
  Chain = Load.getValue(1);
  SDValue Swap = DAG.getNode(
      PPCISD::XXSWAPD, dl, DAG.getVTList(MVT::v2f64, MVT::Other), Chain, Load);
  DCI.AddToWorklist(Swap.getNode());

  // Add a bitcast if the resulting load type doesn't match v2f64.
  if (VecTy != MVT::v2f64) {
    SDValue N = DAG.getNode(ISD::BITCAST, dl, VecTy, Swap);
    DCI.AddToWorklist(N.getNode());
    // Package {bitcast value, swap's chain} to match Load's shape.
    return DAG.getNode(ISD::MERGE_VALUES, dl, DAG.getVTList(VecTy, MVT::Other),
                       N, Swap.getValue(1));
  }

  return Swap;
}

// expandVSXStoreForLE - Convert VSX stores (which may be intrinsics for
// builtins) into stores with swaps.
SDValue PPCTargetLowering::expandVSXStoreForLE(SDNode *N,
                                               DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  SDLoc dl(N);
  SDValue Chain;
  SDValue Base;
  unsigned SrcOpnd;
  MachineMemOperand *MMO;

  switch (N->getOpcode()) {
  default:
    llvm_unreachable("Unexpected opcode for little endian VSX store");
  case ISD::STORE: {
    StoreSDNode *ST = cast<StoreSDNode>(N);
    Chain = ST->getChain();
    Base = ST->getBasePtr();
    MMO = ST->getMemOperand();
    SrcOpnd = 1;
    // If the MMO suggests this isn't a store of a full vector, leave
    // things alone.  For a built-in, we have to make the change for
    // correctness, so if there is a size problem that will be a bug.
    if (MMO->getSize() < 16)
      return SDValue();
    break;
  }
  case ISD::INTRINSIC_VOID: {
    MemIntrinsicSDNode *Intrin = cast<MemIntrinsicSDNode>(N);
    Chain = Intrin->getChain();
    // Intrin->getBasePtr() oddly does not get what we want.
    Base = Intrin->getOperand(3);
    MMO = Intrin->getMemOperand();
    SrcOpnd = 2;
    break;
  }
  }

  SDValue Src = N->getOperand(SrcOpnd);
  MVT VecTy = Src.getValueType().getSimpleVT();

  // Do not expand to PPCISD::XXSWAPD and PPCISD::STXVD2X when the load is
  // aligned and the type is a vector with elements up to 4 bytes
  if (Subtarget.needsSwapsForVSXMemOps() && !(MMO->getAlignment()%16)
      && VecTy.getScalarSizeInBits() <= 32 ) {
    return SDValue();
  }

  // All stores are done as v2f64 and possible bit cast.
  if (VecTy != MVT::v2f64) {
    Src = DAG.getNode(ISD::BITCAST, dl, MVT::v2f64, Src);
    DCI.AddToWorklist(Src.getNode());
  }

  SDValue Swap = DAG.getNode(PPCISD::XXSWAPD, dl,
                             DAG.getVTList(MVT::v2f64, MVT::Other), Chain, Src);
  DCI.AddToWorklist(Swap.getNode());
  Chain = Swap.getValue(1);
  SDValue StoreOps[] = { Chain, Swap, Base };
  SDValue Store = DAG.getMemIntrinsicNode(PPCISD::STXVD2X, dl,
                                          DAG.getVTList(MVT::Other),
                                          StoreOps, VecTy, MMO);
  DCI.AddToWorklist(Store.getNode());
  return Store;
}

// Handle DAG combine for STORE (FP_TO_INT F).
SDValue PPCTargetLowering::combineStoreFPToInt(SDNode *N,
                                               DAGCombinerInfo &DCI) const {

  SelectionDAG &DAG = DCI.DAG;
  SDLoc dl(N);
  unsigned Opcode = N->getOperand(1).getOpcode();

  assert((Opcode == ISD::FP_TO_SINT || Opcode == ISD::FP_TO_UINT)
         && "Not a FP_TO_INT Instruction!");

  SDValue Val = N->getOperand(1).getOperand(0);
  EVT Op1VT = N->getOperand(1).getValueType();
  EVT ResVT = Val.getValueType();

  // Floating point types smaller than 32 bits are not legal on Power.
  if (ResVT.getScalarSizeInBits() < 32)
    return SDValue();

  // Only perform combine for conversion to i64/i32 or power9 i16/i8.
  bool ValidTypeForStoreFltAsInt =
        (Op1VT == MVT::i32 || Op1VT == MVT::i64 ||
         (Subtarget.hasP9Vector() && (Op1VT == MVT::i16 || Op1VT == MVT::i8)));

  if (ResVT == MVT::ppcf128 || !Subtarget.hasP8Altivec() ||
      cast<StoreSDNode>(N)->isTruncatingStore() || !ValidTypeForStoreFltAsInt)
    return SDValue();

  // Extend f32 values to f64
  if (ResVT.getScalarSizeInBits() == 32) {
    Val = DAG.getNode(ISD::FP_EXTEND, dl, MVT::f64, Val);
    DCI.AddToWorklist(Val.getNode());
  }

  // Set signed or unsigned conversion opcode.
  unsigned ConvOpcode = (Opcode == ISD::FP_TO_SINT) ?
                          PPCISD::FP_TO_SINT_IN_VSR :
                          PPCISD::FP_TO_UINT_IN_VSR;

  Val = DAG.getNode(ConvOpcode,
                    dl, ResVT == MVT::f128 ? MVT::f128 : MVT::f64, Val);
  DCI.AddToWorklist(Val.getNode());

  // Set number of bytes being converted.
  unsigned ByteSize = Op1VT.getScalarSizeInBits() / 8;
  SDValue Ops[] = { N->getOperand(0), Val, N->getOperand(2),
                    DAG.getIntPtrConstant(ByteSize, dl, false),
                    DAG.getValueType(Op1VT) };

  Val = DAG.getMemIntrinsicNode(PPCISD::ST_VSR_SCAL_INT, dl,
          DAG.getVTList(MVT::Other), Ops,
          cast<StoreSDNode>(N)->getMemoryVT(),
          cast<StoreSDNode>(N)->getMemOperand());

  DCI.AddToWorklist(Val.getNode());
  return Val;
}

SDValue PPCTargetLowering::PerformDAGCombine(SDNode *N,
                                             DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  SDLoc dl(N);
  switch (N->getOpcode()) {
  default: break;
  case ISD::ADD:
    return combineADD(N, DCI);
  case ISD::SHL:
    return combineSHL(N, DCI);
  case ISD::SRA:
    return combineSRA(N, DCI);
  case ISD::SRL:
    return combineSRL(N, DCI);
  case PPCISD::SHL:
    if (isNullConstant(N->getOperand(0))) // 0 << V -> 0.
        return N->getOperand(0);
    break;
  case PPCISD::SRL:
    if (isNullConstant(N->getOperand(0))) // 0 >>u V -> 0.
        return N->getOperand(0);
    break;
  case PPCISD::SRA:
    if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(N->getOperand(0))) {
      if (C->isNullValue() ||   //  0 >>s V -> 0.
          C->isAllOnesValue())    // -1 >>s V -> -1.
        return N->getOperand(0);
    }
    break;
  case ISD::SIGN_EXTEND:
  case ISD::ZERO_EXTEND:
  case ISD::ANY_EXTEND:
    return DAGCombineExtBoolTrunc(N, DCI);
  case ISD::TRUNCATE:
    return combineTRUNCATE(N, DCI);
  case ISD::SETCC:
    if (SDValue CSCC = combineSetCC(N, DCI))
      return CSCC;
    LLVM_FALLTHROUGH;
  case ISD::SELECT_CC:
    return DAGCombineTruncBoolExt(N, DCI);
  case ISD::SINT_TO_FP:
  case ISD::UINT_TO_FP:
    return combineFPToIntToFP(N, DCI);
  case ISD::STORE: {

    EVT Op1VT = N->getOperand(1).getValueType();
    unsigned Opcode = N->getOperand(1).getOpcode();

    if (Opcode == ISD::FP_TO_SINT || Opcode == ISD::FP_TO_UINT) {
      SDValue Val= combineStoreFPToInt(N, DCI);
      if (Val)
        return Val;
    }

    // Turn STORE (BSWAP) -> sthbrx/stwbrx.
    if (cast<StoreSDNode>(N)->isUnindexed() && Opcode == ISD::BSWAP &&
        N->getOperand(1).getNode()->hasOneUse() &&
        (Op1VT == MVT::i32 || Op1VT == MVT::i16 ||
         (Subtarget.hasLDBRX() && Subtarget.isPPC64() && Op1VT == MVT::i64))) {

      // STBRX can only handle simple types and it makes no sense to store less
      // two bytes in byte-reversed order.
      EVT mVT = cast<StoreSDNode>(N)->getMemoryVT();
      if (mVT.isExtended() || mVT.getSizeInBits() < 16)
        break;

      SDValue BSwapOp = N->getOperand(1).getOperand(0);
      // Do an any-extend to 32-bits if this is a half-word input.
      if (BSwapOp.getValueType() == MVT::i16)
        BSwapOp = DAG.getNode(ISD::ANY_EXTEND, dl, MVT::i32, BSwapOp);

      // If the type of BSWAP operand is wider than stored memory width
      // it need to be shifted to the right side before STBRX.
      if (Op1VT.bitsGT(mVT)) {
        int Shift = Op1VT.getSizeInBits() - mVT.getSizeInBits();
        BSwapOp = DAG.getNode(ISD::SRL, dl, Op1VT, BSwapOp,
                              DAG.getConstant(Shift, dl, MVT::i32));
        // Need to truncate if this is a bswap of i64 stored as i32/i16.
        if (Op1VT == MVT::i64)
          BSwapOp = DAG.getNode(ISD::TRUNCATE, dl, MVT::i32, BSwapOp);
      }

      SDValue Ops[] = {
        N->getOperand(0), BSwapOp, N->getOperand(2), DAG.getValueType(mVT)
      };
      return
        DAG.getMemIntrinsicNode(PPCISD::STBRX, dl, DAG.getVTList(MVT::Other),
                                Ops, cast<StoreSDNode>(N)->getMemoryVT(),
                                cast<StoreSDNode>(N)->getMemOperand());
    }

    // STORE Constant:i32<0>  ->  STORE<trunc to i32> Constant:i64<0>
    // So it can increase the chance of CSE constant construction.
    if (Subtarget.isPPC64() && !DCI.isBeforeLegalize() &&
        isa<ConstantSDNode>(N->getOperand(1)) && Op1VT == MVT::i32) {
      // Need to sign-extended to 64-bits to handle negative values.
      EVT MemVT = cast<StoreSDNode>(N)->getMemoryVT();
      uint64_t Val64 = SignExtend64(N->getConstantOperandVal(1),
                                    MemVT.getSizeInBits());
      SDValue Const64 = DAG.getConstant(Val64, dl, MVT::i64);

      // DAG.getTruncStore() can't be used here because it doesn't accept
      // the general (base + offset) addressing mode.
      // So we use UpdateNodeOperands and setTruncatingStore instead.
      DAG.UpdateNodeOperands(N, N->getOperand(0), Const64, N->getOperand(2),
                             N->getOperand(3));
      cast<StoreSDNode>(N)->setTruncatingStore(true);
      return SDValue(N, 0);
    }

    // For little endian, VSX stores require generating xxswapd/lxvd2x.
    // Not needed on ISA 3.0 based CPUs since we have a non-permuting store.
    if (Op1VT.isSimple()) {
      MVT StoreVT = Op1VT.getSimpleVT();
      if (Subtarget.needsSwapsForVSXMemOps() &&
          (StoreVT == MVT::v2f64 || StoreVT == MVT::v2i64 ||
           StoreVT == MVT::v4f32 || StoreVT == MVT::v4i32))
        return expandVSXStoreForLE(N, DCI);
    }
    break;
  }
  case ISD::LOAD: {
    LoadSDNode *LD = cast<LoadSDNode>(N);
    EVT VT = LD->getValueType(0);

    // For little endian, VSX loads require generating lxvd2x/xxswapd.
    // Not needed on ISA 3.0 based CPUs since we have a non-permuting load.
    if (VT.isSimple()) {
      MVT LoadVT = VT.getSimpleVT();
      if (Subtarget.needsSwapsForVSXMemOps() &&
          (LoadVT == MVT::v2f64 || LoadVT == MVT::v2i64 ||
           LoadVT == MVT::v4f32 || LoadVT == MVT::v4i32))
        return expandVSXLoadForLE(N, DCI);
    }

    // We sometimes end up with a 64-bit integer load, from which we extract
    // two single-precision floating-point numbers. This happens with
    // std::complex<float>, and other similar structures, because of the way we
    // canonicalize structure copies. However, if we lack direct moves,
    // then the final bitcasts from the extracted integer values to the
    // floating-point numbers turn into store/load pairs. Even with direct moves,
    // just loading the two floating-point numbers is likely better.
    auto ReplaceTwoFloatLoad = [&]() {
      if (VT != MVT::i64)
        return false;

      if (LD->getExtensionType() != ISD::NON_EXTLOAD ||
          LD->isVolatile())
        return false;

      //  We're looking for a sequence like this:
      //  t13: i64,ch = load<LD8[%ref.tmp]> t0, t6, undef:i64
      //      t16: i64 = srl t13, Constant:i32<32>
      //    t17: i32 = truncate t16
      //  t18: f32 = bitcast t17
      //    t19: i32 = truncate t13
      //  t20: f32 = bitcast t19

      if (!LD->hasNUsesOfValue(2, 0))
        return false;

      auto UI = LD->use_begin();
      while (UI.getUse().getResNo() != 0) ++UI;
      SDNode *Trunc = *UI++;
      while (UI.getUse().getResNo() != 0) ++UI;
      SDNode *RightShift = *UI;
      if (Trunc->getOpcode() != ISD::TRUNCATE)
        std::swap(Trunc, RightShift);

      if (Trunc->getOpcode() != ISD::TRUNCATE ||
          Trunc->getValueType(0) != MVT::i32 ||
          !Trunc->hasOneUse())
        return false;
      if (RightShift->getOpcode() != ISD::SRL ||
          !isa<ConstantSDNode>(RightShift->getOperand(1)) ||
          RightShift->getConstantOperandVal(1) != 32 ||
          !RightShift->hasOneUse())
        return false;

      SDNode *Trunc2 = *RightShift->use_begin();
      if (Trunc2->getOpcode() != ISD::TRUNCATE ||
          Trunc2->getValueType(0) != MVT::i32 ||
          !Trunc2->hasOneUse())
        return false;

      SDNode *Bitcast = *Trunc->use_begin();
      SDNode *Bitcast2 = *Trunc2->use_begin();

      if (Bitcast->getOpcode() != ISD::BITCAST ||
          Bitcast->getValueType(0) != MVT::f32)
        return false;
      if (Bitcast2->getOpcode() != ISD::BITCAST ||
          Bitcast2->getValueType(0) != MVT::f32)
        return false;

      if (Subtarget.isLittleEndian())
        std::swap(Bitcast, Bitcast2);

      // Bitcast has the second float (in memory-layout order) and Bitcast2
      // has the first one.

      SDValue BasePtr = LD->getBasePtr();
      if (LD->isIndexed()) {
        assert(LD->getAddressingMode() == ISD::PRE_INC &&
               "Non-pre-inc AM on PPC?");
        BasePtr =
          DAG.getNode(ISD::ADD, dl, BasePtr.getValueType(), BasePtr,
                      LD->getOffset());
      }

      auto MMOFlags =
          LD->getMemOperand()->getFlags() & ~MachineMemOperand::MOVolatile;
      SDValue FloatLoad = DAG.getLoad(MVT::f32, dl, LD->getChain(), BasePtr,
                                      LD->getPointerInfo(), LD->getAlignment(),
                                      MMOFlags, LD->getAAInfo());
      SDValue AddPtr =
        DAG.getNode(ISD::ADD, dl, BasePtr.getValueType(),
                    BasePtr, DAG.getIntPtrConstant(4, dl));
      SDValue FloatLoad2 = DAG.getLoad(
          MVT::f32, dl, SDValue(FloatLoad.getNode(), 1), AddPtr,
          LD->getPointerInfo().getWithOffset(4),
          MinAlign(LD->getAlignment(), 4), MMOFlags, LD->getAAInfo());

      if (LD->isIndexed()) {
        // Note that DAGCombine should re-form any pre-increment load(s) from
        // what is produced here if that makes sense.
        DAG.ReplaceAllUsesOfValueWith(SDValue(LD, 1), BasePtr);
      }

      DCI.CombineTo(Bitcast2, FloatLoad);
      DCI.CombineTo(Bitcast, FloatLoad2);

      DAG.ReplaceAllUsesOfValueWith(SDValue(LD, LD->isIndexed() ? 2 : 1),
                                    SDValue(FloatLoad2.getNode(), 1));
      return true;
    };

    if (ReplaceTwoFloatLoad())
      return SDValue(N, 0);

    EVT MemVT = LD->getMemoryVT();
    Type *Ty = MemVT.getTypeForEVT(*DAG.getContext());
    unsigned ABIAlignment = DAG.getDataLayout().getABITypeAlignment(Ty);
    Type *STy = MemVT.getScalarType().getTypeForEVT(*DAG.getContext());
    unsigned ScalarABIAlignment = DAG.getDataLayout().getABITypeAlignment(STy);
    if (LD->isUnindexed() && VT.isVector() &&
        ((Subtarget.hasAltivec() && ISD::isNON_EXTLoad(N) &&
          // P8 and later hardware should just use LOAD.
          !Subtarget.hasP8Vector() && (VT == MVT::v16i8 || VT == MVT::v8i16 ||
                                       VT == MVT::v4i32 || VT == MVT::v4f32)) ||
         (Subtarget.hasQPX() && (VT == MVT::v4f64 || VT == MVT::v4f32) &&
          LD->getAlignment() >= ScalarABIAlignment)) &&
        LD->getAlignment() < ABIAlignment) {
      // This is a type-legal unaligned Altivec or QPX load.
      SDValue Chain = LD->getChain();
      SDValue Ptr = LD->getBasePtr();
      bool isLittleEndian = Subtarget.isLittleEndian();

      // This implements the loading of unaligned vectors as described in
      // the venerable Apple Velocity Engine overview. Specifically:
      // https://developer.apple.com/hardwaredrivers/ve/alignment.html
      // https://developer.apple.com/hardwaredrivers/ve/code_optimization.html
      //
      // The general idea is to expand a sequence of one or more unaligned
      // loads into an alignment-based permutation-control instruction (lvsl
      // or lvsr), a series of regular vector loads (which always truncate
      // their input address to an aligned address), and a series of
      // permutations.  The results of these permutations are the requested
      // loaded values.  The trick is that the last "extra" load is not taken
      // from the address you might suspect (sizeof(vector) bytes after the
      // last requested load), but rather sizeof(vector) - 1 bytes after the
      // last requested vector. The point of this is to avoid a page fault if
      // the base address happened to be aligned. This works because if the
      // base address is aligned, then adding less than a full vector length
      // will cause the last vector in the sequence to be (re)loaded.
      // Otherwise, the next vector will be fetched as you might suspect was
      // necessary.

      // We might be able to reuse the permutation generation from
      // a different base address offset from this one by an aligned amount.
      // The INTRINSIC_WO_CHAIN DAG combine will attempt to perform this
      // optimization later.
      Intrinsic::ID Intr, IntrLD, IntrPerm;
      MVT PermCntlTy, PermTy, LDTy;
      if (Subtarget.hasAltivec()) {
        Intr = isLittleEndian ?  Intrinsic::ppc_altivec_lvsr :
                                 Intrinsic::ppc_altivec_lvsl;
        IntrLD = Intrinsic::ppc_altivec_lvx;
        IntrPerm = Intrinsic::ppc_altivec_vperm;
        PermCntlTy = MVT::v16i8;
        PermTy = MVT::v4i32;
        LDTy = MVT::v4i32;
      } else {
        Intr =   MemVT == MVT::v4f64 ? Intrinsic::ppc_qpx_qvlpcld :
                                       Intrinsic::ppc_qpx_qvlpcls;
        IntrLD = MemVT == MVT::v4f64 ? Intrinsic::ppc_qpx_qvlfd :
                                       Intrinsic::ppc_qpx_qvlfs;
        IntrPerm = Intrinsic::ppc_qpx_qvfperm;
        PermCntlTy = MVT::v4f64;
        PermTy = MVT::v4f64;
        LDTy = MemVT.getSimpleVT();
      }

      SDValue PermCntl = BuildIntrinsicOp(Intr, Ptr, DAG, dl, PermCntlTy);

      // Create the new MMO for the new base load. It is like the original MMO,
      // but represents an area in memory almost twice the vector size centered
      // on the original address. If the address is unaligned, we might start
      // reading up to (sizeof(vector)-1) bytes below the address of the
      // original unaligned load.
      MachineFunction &MF = DAG.getMachineFunction();
      MachineMemOperand *BaseMMO =
        MF.getMachineMemOperand(LD->getMemOperand(),
                                -(long)MemVT.getStoreSize()+1,
                                2*MemVT.getStoreSize()-1);

      // Create the new base load.
      SDValue LDXIntID =
          DAG.getTargetConstant(IntrLD, dl, getPointerTy(MF.getDataLayout()));
      SDValue BaseLoadOps[] = { Chain, LDXIntID, Ptr };
      SDValue BaseLoad =
        DAG.getMemIntrinsicNode(ISD::INTRINSIC_W_CHAIN, dl,
                                DAG.getVTList(PermTy, MVT::Other),
                                BaseLoadOps, LDTy, BaseMMO);

      // Note that the value of IncOffset (which is provided to the next
      // load's pointer info offset value, and thus used to calculate the
      // alignment), and the value of IncValue (which is actually used to
      // increment the pointer value) are different! This is because we
      // require the next load to appear to be aligned, even though it
      // is actually offset from the base pointer by a lesser amount.
      int IncOffset = VT.getSizeInBits() / 8;
      int IncValue = IncOffset;

      // Walk (both up and down) the chain looking for another load at the real
      // (aligned) offset (the alignment of the other load does not matter in
      // this case). If found, then do not use the offset reduction trick, as
      // that will prevent the loads from being later combined (as they would
      // otherwise be duplicates).
      if (!findConsecutiveLoad(LD, DAG))
        --IncValue;

      SDValue Increment =
          DAG.getConstant(IncValue, dl, getPointerTy(MF.getDataLayout()));
      Ptr = DAG.getNode(ISD::ADD, dl, Ptr.getValueType(), Ptr, Increment);

      MachineMemOperand *ExtraMMO =
        MF.getMachineMemOperand(LD->getMemOperand(),
                                1, 2*MemVT.getStoreSize()-1);
      SDValue ExtraLoadOps[] = { Chain, LDXIntID, Ptr };
      SDValue ExtraLoad =
        DAG.getMemIntrinsicNode(ISD::INTRINSIC_W_CHAIN, dl,
                                DAG.getVTList(PermTy, MVT::Other),
                                ExtraLoadOps, LDTy, ExtraMMO);

      SDValue TF = DAG.getNode(ISD::TokenFactor, dl, MVT::Other,
        BaseLoad.getValue(1), ExtraLoad.getValue(1));

      // Because vperm has a big-endian bias, we must reverse the order
      // of the input vectors and complement the permute control vector
      // when generating little endian code.  We have already handled the
      // latter by using lvsr instead of lvsl, so just reverse BaseLoad
      // and ExtraLoad here.
      SDValue Perm;
      if (isLittleEndian)
        Perm = BuildIntrinsicOp(IntrPerm,
                                ExtraLoad, BaseLoad, PermCntl, DAG, dl);
      else
        Perm = BuildIntrinsicOp(IntrPerm,
                                BaseLoad, ExtraLoad, PermCntl, DAG, dl);

      if (VT != PermTy)
        Perm = Subtarget.hasAltivec() ?
                 DAG.getNode(ISD::BITCAST, dl, VT, Perm) :
                 DAG.getNode(ISD::FP_ROUND, dl, VT, Perm, // QPX
                               DAG.getTargetConstant(1, dl, MVT::i64));
                               // second argument is 1 because this rounding
                               // is always exact.

      // The output of the permutation is our loaded result, the TokenFactor is
      // our new chain.
      DCI.CombineTo(N, Perm, TF);
      return SDValue(N, 0);
    }
    }
    break;
    case ISD::INTRINSIC_WO_CHAIN: {
      bool isLittleEndian = Subtarget.isLittleEndian();
      unsigned IID = cast<ConstantSDNode>(N->getOperand(0))->getZExtValue();
      Intrinsic::ID Intr = (isLittleEndian ? Intrinsic::ppc_altivec_lvsr
                                           : Intrinsic::ppc_altivec_lvsl);
      if ((IID == Intr ||
           IID == Intrinsic::ppc_qpx_qvlpcld  ||
           IID == Intrinsic::ppc_qpx_qvlpcls) &&
        N->getOperand(1)->getOpcode() == ISD::ADD) {
        SDValue Add = N->getOperand(1);

        int Bits = IID == Intrinsic::ppc_qpx_qvlpcld ?
                   5 /* 32 byte alignment */ : 4 /* 16 byte alignment */;

        if (DAG.MaskedValueIsZero(Add->getOperand(1),
                                  APInt::getAllOnesValue(Bits /* alignment */)
                                      .zext(Add.getScalarValueSizeInBits()))) {
          SDNode *BasePtr = Add->getOperand(0).getNode();
          for (SDNode::use_iterator UI = BasePtr->use_begin(),
                                    UE = BasePtr->use_end();
               UI != UE; ++UI) {
            if (UI->getOpcode() == ISD::INTRINSIC_WO_CHAIN &&
                cast<ConstantSDNode>(UI->getOperand(0))->getZExtValue() == IID) {
              // We've found another LVSL/LVSR, and this address is an aligned
              // multiple of that one. The results will be the same, so use the
              // one we've just found instead.

              return SDValue(*UI, 0);
            }
          }
        }

        if (isa<ConstantSDNode>(Add->getOperand(1))) {
          SDNode *BasePtr = Add->getOperand(0).getNode();
          for (SDNode::use_iterator UI = BasePtr->use_begin(),
               UE = BasePtr->use_end(); UI != UE; ++UI) {
            if (UI->getOpcode() == ISD::ADD &&
                isa<ConstantSDNode>(UI->getOperand(1)) &&
                (cast<ConstantSDNode>(Add->getOperand(1))->getZExtValue() -
                 cast<ConstantSDNode>(UI->getOperand(1))->getZExtValue()) %
                (1ULL << Bits) == 0) {
              SDNode *OtherAdd = *UI;
              for (SDNode::use_iterator VI = OtherAdd->use_begin(),
                   VE = OtherAdd->use_end(); VI != VE; ++VI) {
                if (VI->getOpcode() == ISD::INTRINSIC_WO_CHAIN &&
                    cast<ConstantSDNode>(VI->getOperand(0))->getZExtValue() == IID) {
                  return SDValue(*VI, 0);
                }
              }
            }
          }
        }
      }

      // Combine vmaxsw/h/b(a, a's negation) to abs(a)
      // Expose the vabsduw/h/b opportunity for down stream
      if (!DCI.isAfterLegalizeDAG() && Subtarget.hasP9Altivec() &&
          (IID == Intrinsic::ppc_altivec_vmaxsw ||
           IID == Intrinsic::ppc_altivec_vmaxsh ||
           IID == Intrinsic::ppc_altivec_vmaxsb)) {
        SDValue V1 = N->getOperand(1);
        SDValue V2 = N->getOperand(2);
        if ((V1.getSimpleValueType() == MVT::v4i32 ||
             V1.getSimpleValueType() == MVT::v8i16 ||
             V1.getSimpleValueType() == MVT::v16i8) &&
            V1.getSimpleValueType() == V2.getSimpleValueType()) {
          // (0-a, a)
          if (V1.getOpcode() == ISD::SUB &&
              ISD::isBuildVectorAllZeros(V1.getOperand(0).getNode()) &&
              V1.getOperand(1) == V2) {
            return DAG.getNode(ISD::ABS, dl, V2.getValueType(), V2);
          }
          // (a, 0-a)
          if (V2.getOpcode() == ISD::SUB &&
              ISD::isBuildVectorAllZeros(V2.getOperand(0).getNode()) &&
              V2.getOperand(1) == V1) {
            return DAG.getNode(ISD::ABS, dl, V1.getValueType(), V1);
          }
          // (x-y, y-x)
          if (V1.getOpcode() == ISD::SUB && V2.getOpcode() == ISD::SUB &&
              V1.getOperand(0) == V2.getOperand(1) &&
              V1.getOperand(1) == V2.getOperand(0)) {
            return DAG.getNode(ISD::ABS, dl, V1.getValueType(), V1);
          }
        }
      }
    }

    break;
  case ISD::INTRINSIC_W_CHAIN:
    // For little endian, VSX loads require generating lxvd2x/xxswapd.
    // Not needed on ISA 3.0 based CPUs since we have a non-permuting load.
    if (Subtarget.needsSwapsForVSXMemOps()) {
      switch (cast<ConstantSDNode>(N->getOperand(1))->getZExtValue()) {
      default:
        break;
      case Intrinsic::ppc_vsx_lxvw4x:
      case Intrinsic::ppc_vsx_lxvd2x:
        return expandVSXLoadForLE(N, DCI);
      }
    }
    break;
  case ISD::INTRINSIC_VOID:
    // For little endian, VSX stores require generating xxswapd/stxvd2x.
    // Not needed on ISA 3.0 based CPUs since we have a non-permuting store.
    if (Subtarget.needsSwapsForVSXMemOps()) {
      switch (cast<ConstantSDNode>(N->getOperand(1))->getZExtValue()) {
      default:
        break;
      case Intrinsic::ppc_vsx_stxvw4x:
      case Intrinsic::ppc_vsx_stxvd2x:
        return expandVSXStoreForLE(N, DCI);
      }
    }
    break;
  case ISD::BSWAP:
    // Turn BSWAP (LOAD) -> lhbrx/lwbrx.
    if (ISD::isNON_EXTLoad(N->getOperand(0).getNode()) &&
        N->getOperand(0).hasOneUse() &&
        (N->getValueType(0) == MVT::i32 || N->getValueType(0) == MVT::i16 ||
         (Subtarget.hasLDBRX() && Subtarget.isPPC64() &&
          N->getValueType(0) == MVT::i64))) {
      SDValue Load = N->getOperand(0);
      LoadSDNode *LD = cast<LoadSDNode>(Load);
      // Create the byte-swapping load.
      SDValue Ops[] = {
        LD->getChain(),    // Chain
        LD->getBasePtr(),  // Ptr
        DAG.getValueType(N->getValueType(0)) // VT
      };
      SDValue BSLoad =
        DAG.getMemIntrinsicNode(PPCISD::LBRX, dl,
                                DAG.getVTList(N->getValueType(0) == MVT::i64 ?
                                              MVT::i64 : MVT::i32, MVT::Other),
                                Ops, LD->getMemoryVT(), LD->getMemOperand());

      // If this is an i16 load, insert the truncate.
      SDValue ResVal = BSLoad;
      if (N->getValueType(0) == MVT::i16)
        ResVal = DAG.getNode(ISD::TRUNCATE, dl, MVT::i16, BSLoad);

      // First, combine the bswap away.  This makes the value produced by the
      // load dead.
      DCI.CombineTo(N, ResVal);

      // Next, combine the load away, we give it a bogus result value but a real
      // chain result.  The result value is dead because the bswap is dead.
      DCI.CombineTo(Load.getNode(), ResVal, BSLoad.getValue(1));

      // Return N so it doesn't get rechecked!
      return SDValue(N, 0);
    }
    break;
  case PPCISD::VCMP:
    // If a VCMPo node already exists with exactly the same operands as this
    // node, use its result instead of this node (VCMPo computes both a CR6 and
    // a normal output).
    //
    if (!N->getOperand(0).hasOneUse() &&
        !N->getOperand(1).hasOneUse() &&
        !N->getOperand(2).hasOneUse()) {

      // Scan all of the users of the LHS, looking for VCMPo's that match.
      SDNode *VCMPoNode = nullptr;

      SDNode *LHSN = N->getOperand(0).getNode();
      for (SDNode::use_iterator UI = LHSN->use_begin(), E = LHSN->use_end();
           UI != E; ++UI)
        if (UI->getOpcode() == PPCISD::VCMPo &&
            UI->getOperand(1) == N->getOperand(1) &&
            UI->getOperand(2) == N->getOperand(2) &&
            UI->getOperand(0) == N->getOperand(0)) {
          VCMPoNode = *UI;
          break;
        }

      // If there is no VCMPo node, or if the flag value has a single use, don't
      // transform this.
      if (!VCMPoNode || VCMPoNode->hasNUsesOfValue(0, 1))
        break;

      // Look at the (necessarily single) use of the flag value.  If it has a
      // chain, this transformation is more complex.  Note that multiple things
      // could use the value result, which we should ignore.
      SDNode *FlagUser = nullptr;
      for (SDNode::use_iterator UI = VCMPoNode->use_begin();
           FlagUser == nullptr; ++UI) {
        assert(UI != VCMPoNode->use_end() && "Didn't find user!");
        SDNode *User = *UI;
        for (unsigned i = 0, e = User->getNumOperands(); i != e; ++i) {
          if (User->getOperand(i) == SDValue(VCMPoNode, 1)) {
            FlagUser = User;
            break;
          }
        }
      }

      // If the user is a MFOCRF instruction, we know this is safe.
      // Otherwise we give up for right now.
      if (FlagUser->getOpcode() == PPCISD::MFOCRF)
        return SDValue(VCMPoNode, 0);
    }
    break;
  case ISD::BRCOND: {
    SDValue Cond = N->getOperand(1);
    SDValue Target = N->getOperand(2);

    if (Cond.getOpcode() == ISD::INTRINSIC_W_CHAIN &&
        cast<ConstantSDNode>(Cond.getOperand(1))->getZExtValue() ==
          Intrinsic::ppc_is_decremented_ctr_nonzero) {

      // We now need to make the intrinsic dead (it cannot be instruction
      // selected).
      DAG.ReplaceAllUsesOfValueWith(Cond.getValue(1), Cond.getOperand(0));
      assert(Cond.getNode()->hasOneUse() &&
             "Counter decrement has more than one use");

      return DAG.getNode(PPCISD::BDNZ, dl, MVT::Other,
                         N->getOperand(0), Target);
    }
  }
  break;
  case ISD::BR_CC: {
    // If this is a branch on an altivec predicate comparison, lower this so
    // that we don't have to do a MFOCRF: instead, branch directly on CR6.  This
    // lowering is done pre-legalize, because the legalizer lowers the predicate
    // compare down to code that is difficult to reassemble.
    ISD::CondCode CC = cast<CondCodeSDNode>(N->getOperand(1))->get();
    SDValue LHS = N->getOperand(2), RHS = N->getOperand(3);

    // Sometimes the promoted value of the intrinsic is ANDed by some non-zero
    // value. If so, pass-through the AND to get to the intrinsic.
    if (LHS.getOpcode() == ISD::AND &&
        LHS.getOperand(0).getOpcode() == ISD::INTRINSIC_W_CHAIN &&
        cast<ConstantSDNode>(LHS.getOperand(0).getOperand(1))->getZExtValue() ==
          Intrinsic::ppc_is_decremented_ctr_nonzero &&
        isa<ConstantSDNode>(LHS.getOperand(1)) &&
        !isNullConstant(LHS.getOperand(1)))
      LHS = LHS.getOperand(0);

    if (LHS.getOpcode() == ISD::INTRINSIC_W_CHAIN &&
        cast<ConstantSDNode>(LHS.getOperand(1))->getZExtValue() ==
          Intrinsic::ppc_is_decremented_ctr_nonzero &&
        isa<ConstantSDNode>(RHS)) {
      assert((CC == ISD::SETEQ || CC == ISD::SETNE) &&
             "Counter decrement comparison is not EQ or NE");

      unsigned Val = cast<ConstantSDNode>(RHS)->getZExtValue();
      bool isBDNZ = (CC == ISD::SETEQ && Val) ||
                    (CC == ISD::SETNE && !Val);

      // We now need to make the intrinsic dead (it cannot be instruction
      // selected).
      DAG.ReplaceAllUsesOfValueWith(LHS.getValue(1), LHS.getOperand(0));
      assert(LHS.getNode()->hasOneUse() &&
             "Counter decrement has more than one use");

      return DAG.getNode(isBDNZ ? PPCISD::BDNZ : PPCISD::BDZ, dl, MVT::Other,
                         N->getOperand(0), N->getOperand(4));
    }

    int CompareOpc;
    bool isDot;

    if (LHS.getOpcode() == ISD::INTRINSIC_WO_CHAIN &&
        isa<ConstantSDNode>(RHS) && (CC == ISD::SETEQ || CC == ISD::SETNE) &&
        getVectorCompareInfo(LHS, CompareOpc, isDot, Subtarget)) {
      assert(isDot && "Can't compare against a vector result!");

      // If this is a comparison against something other than 0/1, then we know
      // that the condition is never/always true.
      unsigned Val = cast<ConstantSDNode>(RHS)->getZExtValue();
      if (Val != 0 && Val != 1) {
        if (CC == ISD::SETEQ)      // Cond never true, remove branch.
          return N->getOperand(0);
        // Always !=, turn it into an unconditional branch.
        return DAG.getNode(ISD::BR, dl, MVT::Other,
                           N->getOperand(0), N->getOperand(4));
      }

      bool BranchOnWhenPredTrue = (CC == ISD::SETEQ) ^ (Val == 0);

      // Create the PPCISD altivec 'dot' comparison node.
      SDValue Ops[] = {
        LHS.getOperand(2),  // LHS of compare
        LHS.getOperand(3),  // RHS of compare
        DAG.getConstant(CompareOpc, dl, MVT::i32)
      };
      EVT VTs[] = { LHS.getOperand(2).getValueType(), MVT::Glue };
      SDValue CompNode = DAG.getNode(PPCISD::VCMPo, dl, VTs, Ops);

      // Unpack the result based on how the target uses it.
      PPC::Predicate CompOpc;
      switch (cast<ConstantSDNode>(LHS.getOperand(1))->getZExtValue()) {
      default:  // Can't happen, don't crash on invalid number though.
      case 0:   // Branch on the value of the EQ bit of CR6.
        CompOpc = BranchOnWhenPredTrue ? PPC::PRED_EQ : PPC::PRED_NE;
        break;
      case 1:   // Branch on the inverted value of the EQ bit of CR6.
        CompOpc = BranchOnWhenPredTrue ? PPC::PRED_NE : PPC::PRED_EQ;
        break;
      case 2:   // Branch on the value of the LT bit of CR6.
        CompOpc = BranchOnWhenPredTrue ? PPC::PRED_LT : PPC::PRED_GE;
        break;
      case 3:   // Branch on the inverted value of the LT bit of CR6.
        CompOpc = BranchOnWhenPredTrue ? PPC::PRED_GE : PPC::PRED_LT;
        break;
      }

      return DAG.getNode(PPCISD::COND_BRANCH, dl, MVT::Other, N->getOperand(0),
                         DAG.getConstant(CompOpc, dl, MVT::i32),
                         DAG.getRegister(PPC::CR6, MVT::i32),
                         N->getOperand(4), CompNode.getValue(1));
    }
    break;
  }
  case ISD::BUILD_VECTOR:
    return DAGCombineBuildVector(N, DCI);
  case ISD::ABS: 
    return combineABS(N, DCI);
  case ISD::VSELECT: 
    return combineVSelect(N, DCI);
  }

  return SDValue();
}

SDValue
PPCTargetLowering::BuildSDIVPow2(SDNode *N, const APInt &Divisor,
                                 SelectionDAG &DAG,
                                 SmallVectorImpl<SDNode *> &Created) const {
  // fold (sdiv X, pow2)
  EVT VT = N->getValueType(0);
  if (VT == MVT::i64 && !Subtarget.isPPC64())
    return SDValue();
  if ((VT != MVT::i32 && VT != MVT::i64) ||
      !(Divisor.isPowerOf2() || (-Divisor).isPowerOf2()))
    return SDValue();

  SDLoc DL(N);
  SDValue N0 = N->getOperand(0);

  bool IsNegPow2 = (-Divisor).isPowerOf2();
  unsigned Lg2 = (IsNegPow2 ? -Divisor : Divisor).countTrailingZeros();
  SDValue ShiftAmt = DAG.getConstant(Lg2, DL, VT);

  SDValue Op = DAG.getNode(PPCISD::SRA_ADDZE, DL, VT, N0, ShiftAmt);
  Created.push_back(Op.getNode());

  if (IsNegPow2) {
    Op = DAG.getNode(ISD::SUB, DL, VT, DAG.getConstant(0, DL, VT), Op);
    Created.push_back(Op.getNode());
  }

  return Op;
}

//===----------------------------------------------------------------------===//
// Inline Assembly Support
//===----------------------------------------------------------------------===//

void PPCTargetLowering::computeKnownBitsForTargetNode(const SDValue Op,
                                                      KnownBits &Known,
                                                      const APInt &DemandedElts,
                                                      const SelectionDAG &DAG,
                                                      unsigned Depth) const {
  Known.resetAll();
  switch (Op.getOpcode()) {
  default: break;
  case PPCISD::LBRX: {
    // lhbrx is known to have the top bits cleared out.
    if (cast<VTSDNode>(Op.getOperand(2))->getVT() == MVT::i16)
      Known.Zero = 0xFFFF0000;
    break;
  }
  case ISD::INTRINSIC_WO_CHAIN: {
    switch (cast<ConstantSDNode>(Op.getOperand(0))->getZExtValue()) {
    default: break;
    case Intrinsic::ppc_altivec_vcmpbfp_p:
    case Intrinsic::ppc_altivec_vcmpeqfp_p:
    case Intrinsic::ppc_altivec_vcmpequb_p:
    case Intrinsic::ppc_altivec_vcmpequh_p:
    case Intrinsic::ppc_altivec_vcmpequw_p:
    case Intrinsic::ppc_altivec_vcmpequd_p:
    case Intrinsic::ppc_altivec_vcmpgefp_p:
    case Intrinsic::ppc_altivec_vcmpgtfp_p:
    case Intrinsic::ppc_altivec_vcmpgtsb_p:
    case Intrinsic::ppc_altivec_vcmpgtsh_p:
    case Intrinsic::ppc_altivec_vcmpgtsw_p:
    case Intrinsic::ppc_altivec_vcmpgtsd_p:
    case Intrinsic::ppc_altivec_vcmpgtub_p:
    case Intrinsic::ppc_altivec_vcmpgtuh_p:
    case Intrinsic::ppc_altivec_vcmpgtuw_p:
    case Intrinsic::ppc_altivec_vcmpgtud_p:
      Known.Zero = ~1U;  // All bits but the low one are known to be zero.
      break;
    }
  }
  }
}

unsigned PPCTargetLowering::getPrefLoopAlignment(MachineLoop *ML) const {
  switch (Subtarget.getDarwinDirective()) {
  default: break;
  case PPC::DIR_970:
  case PPC::DIR_PWR4:
  case PPC::DIR_PWR5:
  case PPC::DIR_PWR5X:
  case PPC::DIR_PWR6:
  case PPC::DIR_PWR6X:
  case PPC::DIR_PWR7:
  case PPC::DIR_PWR8:
  case PPC::DIR_PWR9: {
    if (!ML)
      break;

    const PPCInstrInfo *TII = Subtarget.getInstrInfo();

    // For small loops (between 5 and 8 instructions), align to a 32-byte
    // boundary so that the entire loop fits in one instruction-cache line.
    uint64_t LoopSize = 0;
    for (auto I = ML->block_begin(), IE = ML->block_end(); I != IE; ++I)
      for (auto J = (*I)->begin(), JE = (*I)->end(); J != JE; ++J) {
        LoopSize += TII->getInstSizeInBytes(*J);
        if (LoopSize > 32)
          break;
      }

    if (LoopSize > 16 && LoopSize <= 32)
      return 5;

    break;
  }
  }

  return TargetLowering::getPrefLoopAlignment(ML);
}

/// getConstraintType - Given a constraint, return the type of
/// constraint it is for this target.
PPCTargetLowering::ConstraintType
PPCTargetLowering::getConstraintType(StringRef Constraint) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    default: break;
    case 'b':
    case 'r':
    case 'f':
    case 'd':
    case 'v':
    case 'y':
      return C_RegisterClass;
    case 'Z':
      // FIXME: While Z does indicate a memory constraint, it specifically
      // indicates an r+r address (used in conjunction with the 'y' modifier
      // in the replacement string). Currently, we're forcing the base
      // register to be r0 in the asm printer (which is interpreted as zero)
      // and forming the complete address in the second register. This is
      // suboptimal.
      return C_Memory;
    }
  } else if (Constraint == "wc") { // individual CR bits.
    return C_RegisterClass;
  } else if (Constraint == "wa" || Constraint == "wd" ||
             Constraint == "wf" || Constraint == "ws" ||
             Constraint == "wi") {
    return C_RegisterClass; // VSX registers.
  }
  return TargetLowering::getConstraintType(Constraint);
}

/// Examine constraint type and operand type and determine a weight value.
/// This object must already have been set up with the operand type
/// and the current alternative constraint selected.
TargetLowering::ConstraintWeight
PPCTargetLowering::getSingleConstraintMatchWeight(
    AsmOperandInfo &info, const char *constraint) const {
  ConstraintWeight weight = CW_Invalid;
  Value *CallOperandVal = info.CallOperandVal;
    // If we don't have a value, we can't do a match,
    // but allow it at the lowest weight.
  if (!CallOperandVal)
    return CW_Default;
  Type *type = CallOperandVal->getType();

  // Look at the constraint type.
  if (StringRef(constraint) == "wc" && type->isIntegerTy(1))
    return CW_Register; // an individual CR bit.
  else if ((StringRef(constraint) == "wa" ||
            StringRef(constraint) == "wd" ||
            StringRef(constraint) == "wf") &&
           type->isVectorTy())
    return CW_Register;
  else if (StringRef(constraint) == "ws" && type->isDoubleTy())
    return CW_Register;
  else if (StringRef(constraint) == "wi" && type->isIntegerTy(64))
    return CW_Register; // just hold 64-bit integers data.

  switch (*constraint) {
  default:
    weight = TargetLowering::getSingleConstraintMatchWeight(info, constraint);
    break;
  case 'b':
    if (type->isIntegerTy())
      weight = CW_Register;
    break;
  case 'f':
    if (type->isFloatTy())
      weight = CW_Register;
    break;
  case 'd':
    if (type->isDoubleTy())
      weight = CW_Register;
    break;
  case 'v':
    if (type->isVectorTy())
      weight = CW_Register;
    break;
  case 'y':
    weight = CW_Register;
    break;
  case 'Z':
    weight = CW_Memory;
    break;
  }
  return weight;
}

std::pair<unsigned, const TargetRegisterClass *>
PPCTargetLowering::getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                                                StringRef Constraint,
                                                MVT VT) const {
  if (Constraint.size() == 1) {
    // GCC RS6000 Constraint Letters
    switch (Constraint[0]) {
    case 'b':   // R1-R31
      if (VT == MVT::i64 && Subtarget.isPPC64())
        return std::make_pair(0U, &PPC::G8RC_NOX0RegClass);
      return std::make_pair(0U, &PPC::GPRC_NOR0RegClass);
    case 'r':   // R0-R31
      if (VT == MVT::i64 && Subtarget.isPPC64())
        return std::make_pair(0U, &PPC::G8RCRegClass);
      return std::make_pair(0U, &PPC::GPRCRegClass);
    // 'd' and 'f' constraints are both defined to be "the floating point
    // registers", where one is for 32-bit and the other for 64-bit. We don't
    // really care overly much here so just give them all the same reg classes.
    case 'd':
    case 'f':
      if (Subtarget.hasSPE()) {
        if (VT == MVT::f32 || VT == MVT::i32)
          return std::make_pair(0U, &PPC::SPE4RCRegClass);
        if (VT == MVT::f64 || VT == MVT::i64)
          return std::make_pair(0U, &PPC::SPERCRegClass);
      } else {
        if (VT == MVT::f32 || VT == MVT::i32)
          return std::make_pair(0U, &PPC::F4RCRegClass);
        if (VT == MVT::f64 || VT == MVT::i64)
          return std::make_pair(0U, &PPC::F8RCRegClass);
        if (VT == MVT::v4f64 && Subtarget.hasQPX())
          return std::make_pair(0U, &PPC::QFRCRegClass);
        if (VT == MVT::v4f32 && Subtarget.hasQPX())
          return std::make_pair(0U, &PPC::QSRCRegClass);
      }
      break;
    case 'v':
      if (VT == MVT::v4f64 && Subtarget.hasQPX())
        return std::make_pair(0U, &PPC::QFRCRegClass);
      if (VT == MVT::v4f32 && Subtarget.hasQPX())
        return std::make_pair(0U, &PPC::QSRCRegClass);
      if (Subtarget.hasAltivec())
        return std::make_pair(0U, &PPC::VRRCRegClass);
      break;
    case 'y':   // crrc
      return std::make_pair(0U, &PPC::CRRCRegClass);
    }
  } else if (Constraint == "wc" && Subtarget.useCRBits()) {
    // An individual CR bit.
    return std::make_pair(0U, &PPC::CRBITRCRegClass);
  } else if ((Constraint == "wa" || Constraint == "wd" ||
             Constraint == "wf" || Constraint == "wi") &&
             Subtarget.hasVSX()) {
    return std::make_pair(0U, &PPC::VSRCRegClass);
  } else if (Constraint == "ws" && Subtarget.hasVSX()) {
    if (VT == MVT::f32 && Subtarget.hasP8Vector())
      return std::make_pair(0U, &PPC::VSSRCRegClass);
    else
      return std::make_pair(0U, &PPC::VSFRCRegClass);
  }

  std::pair<unsigned, const TargetRegisterClass *> R =
      TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);

  // r[0-9]+ are used, on PPC64, to refer to the corresponding 64-bit registers
  // (which we call X[0-9]+). If a 64-bit value has been requested, and a
  // 32-bit GPR has been selected, then 'upgrade' it to the 64-bit parent
  // register.
  // FIXME: If TargetLowering::getRegForInlineAsmConstraint could somehow use
  // the AsmName field from *RegisterInfo.td, then this would not be necessary.
  if (R.first && VT == MVT::i64 && Subtarget.isPPC64() &&
      PPC::GPRCRegClass.contains(R.first))
    return std::make_pair(TRI->getMatchingSuperReg(R.first,
                            PPC::sub_32, &PPC::G8RCRegClass),
                          &PPC::G8RCRegClass);

  // GCC accepts 'cc' as an alias for 'cr0', and we need to do the same.
  if (!R.second && StringRef("{cc}").equals_lower(Constraint)) {
    R.first = PPC::CR0;
    R.second = &PPC::CRRCRegClass;
  }

  return R;
}

/// LowerAsmOperandForConstraint - Lower the specified operand into the Ops
/// vector.  If it is invalid, don't add anything to Ops.
void PPCTargetLowering::LowerAsmOperandForConstraint(SDValue Op,
                                                     std::string &Constraint,
                                                     std::vector<SDValue>&Ops,
                                                     SelectionDAG &DAG) const {
  SDValue Result;

  // Only support length 1 constraints.
  if (Constraint.length() > 1) return;

  char Letter = Constraint[0];
  switch (Letter) {
  default: break;
  case 'I':
  case 'J':
  case 'K':
  case 'L':
  case 'M':
  case 'N':
  case 'O':
  case 'P': {
    ConstantSDNode *CST = dyn_cast<ConstantSDNode>(Op);
    if (!CST) return; // Must be an immediate to match.
    SDLoc dl(Op);
    int64_t Value = CST->getSExtValue();
    EVT TCVT = MVT::i64; // All constants taken to be 64 bits so that negative
                         // numbers are printed as such.
    switch (Letter) {
    default: llvm_unreachable("Unknown constraint letter!");
    case 'I':  // "I" is a signed 16-bit constant.
      if (isInt<16>(Value))
        Result = DAG.getTargetConstant(Value, dl, TCVT);
      break;
    case 'J':  // "J" is a constant with only the high-order 16 bits nonzero.
      if (isShiftedUInt<16, 16>(Value))
        Result = DAG.getTargetConstant(Value, dl, TCVT);
      break;
    case 'L':  // "L" is a signed 16-bit constant shifted left 16 bits.
      if (isShiftedInt<16, 16>(Value))
        Result = DAG.getTargetConstant(Value, dl, TCVT);
      break;
    case 'K':  // "K" is a constant with only the low-order 16 bits nonzero.
      if (isUInt<16>(Value))
        Result = DAG.getTargetConstant(Value, dl, TCVT);
      break;
    case 'M':  // "M" is a constant that is greater than 31.
      if (Value > 31)
        Result = DAG.getTargetConstant(Value, dl, TCVT);
      break;
    case 'N':  // "N" is a positive constant that is an exact power of two.
      if (Value > 0 && isPowerOf2_64(Value))
        Result = DAG.getTargetConstant(Value, dl, TCVT);
      break;
    case 'O':  // "O" is the constant zero.
      if (Value == 0)
        Result = DAG.getTargetConstant(Value, dl, TCVT);
      break;
    case 'P':  // "P" is a constant whose negation is a signed 16-bit constant.
      if (isInt<16>(-Value))
        Result = DAG.getTargetConstant(Value, dl, TCVT);
      break;
    }
    break;
  }
  }

  if (Result.getNode()) {
    Ops.push_back(Result);
    return;
  }

  // Handle standard constraint letters.
  TargetLowering::LowerAsmOperandForConstraint(Op, Constraint, Ops, DAG);
}

// isLegalAddressingMode - Return true if the addressing mode represented
// by AM is legal for this target, for a load/store of the specified type.
bool PPCTargetLowering::isLegalAddressingMode(const DataLayout &DL,
                                              const AddrMode &AM, Type *Ty,
                                              unsigned AS, Instruction *I) const {
  // PPC does not allow r+i addressing modes for vectors!
  if (Ty->isVectorTy() && AM.BaseOffs != 0)
    return false;

  // PPC allows a sign-extended 16-bit immediate field.
  if (AM.BaseOffs <= -(1LL << 16) || AM.BaseOffs >= (1LL << 16)-1)
    return false;

  // No global is ever allowed as a base.
  if (AM.BaseGV)
    return false;

  // PPC only support r+r,
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
  default:
    // No other scales are supported.
    return false;
  }

  return true;
}

SDValue PPCTargetLowering::LowerRETURNADDR(SDValue Op,
                                           SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MFI.setReturnAddressIsTaken(true);

  if (verifyReturnAddressArgumentIsConstant(Op, DAG))
    return SDValue();

  SDLoc dl(Op);
  unsigned Depth = cast<ConstantSDNode>(Op.getOperand(0))->getZExtValue();

  // Make sure the function does not optimize away the store of the RA to
  // the stack.
  PPCFunctionInfo *FuncInfo = MF.getInfo<PPCFunctionInfo>();
  FuncInfo->setLRStoreRequired();
  bool isPPC64 = Subtarget.isPPC64();
  auto PtrVT = getPointerTy(MF.getDataLayout());

  if (Depth > 0) {
    SDValue FrameAddr = LowerFRAMEADDR(Op, DAG);
    SDValue Offset =
        DAG.getConstant(Subtarget.getFrameLowering()->getReturnSaveOffset(), dl,
                        isPPC64 ? MVT::i64 : MVT::i32);
    return DAG.getLoad(PtrVT, dl, DAG.getEntryNode(),
                       DAG.getNode(ISD::ADD, dl, PtrVT, FrameAddr, Offset),
                       MachinePointerInfo());
  }

  // Just load the return address off the stack.
  SDValue RetAddrFI = getReturnAddrFrameIndex(DAG);
  return DAG.getLoad(PtrVT, dl, DAG.getEntryNode(), RetAddrFI,
                     MachinePointerInfo());
}

SDValue PPCTargetLowering::LowerFRAMEADDR(SDValue Op,
                                          SelectionDAG &DAG) const {
  SDLoc dl(Op);
  unsigned Depth = cast<ConstantSDNode>(Op.getOperand(0))->getZExtValue();

  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MFI.setFrameAddressIsTaken(true);

  EVT PtrVT = getPointerTy(MF.getDataLayout());
  bool isPPC64 = PtrVT == MVT::i64;

  // Naked functions never have a frame pointer, and so we use r1. For all
  // other functions, this decision must be delayed until during PEI.
  unsigned FrameReg;
  if (MF.getFunction().hasFnAttribute(Attribute::Naked))
    FrameReg = isPPC64 ? PPC::X1 : PPC::R1;
  else
    FrameReg = isPPC64 ? PPC::FP8 : PPC::FP;

  SDValue FrameAddr = DAG.getCopyFromReg(DAG.getEntryNode(), dl, FrameReg,
                                         PtrVT);
  while (Depth--)
    FrameAddr = DAG.getLoad(Op.getValueType(), dl, DAG.getEntryNode(),
                            FrameAddr, MachinePointerInfo());
  return FrameAddr;
}

// FIXME? Maybe this could be a TableGen attribute on some registers and
// this table could be generated automatically from RegInfo.
unsigned PPCTargetLowering::getRegisterByName(const char* RegName, EVT VT,
                                              SelectionDAG &DAG) const {
  bool isPPC64 = Subtarget.isPPC64();
  bool isDarwinABI = Subtarget.isDarwinABI();

  if ((isPPC64 && VT != MVT::i64 && VT != MVT::i32) ||
      (!isPPC64 && VT != MVT::i32))
    report_fatal_error("Invalid register global variable type");

  bool is64Bit = isPPC64 && VT == MVT::i64;
  unsigned Reg = StringSwitch<unsigned>(RegName)
                   .Case("r1", is64Bit ? PPC::X1 : PPC::R1)
                   .Case("r2", (isDarwinABI || isPPC64) ? 0 : PPC::R2)
                   .Case("r13", (!isPPC64 && isDarwinABI) ? 0 :
                                  (is64Bit ? PPC::X13 : PPC::R13))
                   .Default(0);

  if (Reg)
    return Reg;
  report_fatal_error("Invalid register name global variable");
}

bool PPCTargetLowering::isAccessedAsGotIndirect(SDValue GA) const {
  // 32-bit SVR4 ABI access everything as got-indirect.
  if (Subtarget.isSVR4ABI() && !Subtarget.isPPC64())
    return true;

  CodeModel::Model CModel = getTargetMachine().getCodeModel();
  // If it is small or large code model, module locals are accessed
  // indirectly by loading their address from .toc/.got. The difference
  // is that for large code model we have ADDISTocHa + LDtocL and for
  // small code model we simply have LDtoc.
  if (CModel == CodeModel::Small || CModel == CodeModel::Large)
    return true;

  // JumpTable and BlockAddress are accessed as got-indirect. 
  if (isa<JumpTableSDNode>(GA) || isa<BlockAddressSDNode>(GA))
    return true;

  if (GlobalAddressSDNode *G = dyn_cast<GlobalAddressSDNode>(GA)) {
    const GlobalValue *GV = G->getGlobal();
    unsigned char GVFlags = Subtarget.classifyGlobalReference(GV);
    // The NLP flag indicates that a global access has to use an
    // extra indirection.
    if (GVFlags & PPCII::MO_NLP_FLAG)
      return true;
  }

  return false;
}

bool
PPCTargetLowering::isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const {
  // The PowerPC target isn't yet aware of offsets.
  return false;
}

bool PPCTargetLowering::getTgtMemIntrinsic(IntrinsicInfo &Info,
                                           const CallInst &I,
                                           MachineFunction &MF,
                                           unsigned Intrinsic) const {
  switch (Intrinsic) {
  case Intrinsic::ppc_qpx_qvlfd:
  case Intrinsic::ppc_qpx_qvlfs:
  case Intrinsic::ppc_qpx_qvlfcd:
  case Intrinsic::ppc_qpx_qvlfcs:
  case Intrinsic::ppc_qpx_qvlfiwa:
  case Intrinsic::ppc_qpx_qvlfiwz:
  case Intrinsic::ppc_altivec_lvx:
  case Intrinsic::ppc_altivec_lvxl:
  case Intrinsic::ppc_altivec_lvebx:
  case Intrinsic::ppc_altivec_lvehx:
  case Intrinsic::ppc_altivec_lvewx:
  case Intrinsic::ppc_vsx_lxvd2x:
  case Intrinsic::ppc_vsx_lxvw4x: {
    EVT VT;
    switch (Intrinsic) {
    case Intrinsic::ppc_altivec_lvebx:
      VT = MVT::i8;
      break;
    case Intrinsic::ppc_altivec_lvehx:
      VT = MVT::i16;
      break;
    case Intrinsic::ppc_altivec_lvewx:
      VT = MVT::i32;
      break;
    case Intrinsic::ppc_vsx_lxvd2x:
      VT = MVT::v2f64;
      break;
    case Intrinsic::ppc_qpx_qvlfd:
      VT = MVT::v4f64;
      break;
    case Intrinsic::ppc_qpx_qvlfs:
      VT = MVT::v4f32;
      break;
    case Intrinsic::ppc_qpx_qvlfcd:
      VT = MVT::v2f64;
      break;
    case Intrinsic::ppc_qpx_qvlfcs:
      VT = MVT::v2f32;
      break;
    default:
      VT = MVT::v4i32;
      break;
    }

    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = VT;
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = -VT.getStoreSize()+1;
    Info.size = 2*VT.getStoreSize()-1;
    Info.align = 1;
    Info.flags = MachineMemOperand::MOLoad;
    return true;
  }
  case Intrinsic::ppc_qpx_qvlfda:
  case Intrinsic::ppc_qpx_qvlfsa:
  case Intrinsic::ppc_qpx_qvlfcda:
  case Intrinsic::ppc_qpx_qvlfcsa:
  case Intrinsic::ppc_qpx_qvlfiwaa:
  case Intrinsic::ppc_qpx_qvlfiwza: {
    EVT VT;
    switch (Intrinsic) {
    case Intrinsic::ppc_qpx_qvlfda:
      VT = MVT::v4f64;
      break;
    case Intrinsic::ppc_qpx_qvlfsa:
      VT = MVT::v4f32;
      break;
    case Intrinsic::ppc_qpx_qvlfcda:
      VT = MVT::v2f64;
      break;
    case Intrinsic::ppc_qpx_qvlfcsa:
      VT = MVT::v2f32;
      break;
    default:
      VT = MVT::v4i32;
      break;
    }

    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = VT;
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.size = VT.getStoreSize();
    Info.align = 1;
    Info.flags = MachineMemOperand::MOLoad;
    return true;
  }
  case Intrinsic::ppc_qpx_qvstfd:
  case Intrinsic::ppc_qpx_qvstfs:
  case Intrinsic::ppc_qpx_qvstfcd:
  case Intrinsic::ppc_qpx_qvstfcs:
  case Intrinsic::ppc_qpx_qvstfiw:
  case Intrinsic::ppc_altivec_stvx:
  case Intrinsic::ppc_altivec_stvxl:
  case Intrinsic::ppc_altivec_stvebx:
  case Intrinsic::ppc_altivec_stvehx:
  case Intrinsic::ppc_altivec_stvewx:
  case Intrinsic::ppc_vsx_stxvd2x:
  case Intrinsic::ppc_vsx_stxvw4x: {
    EVT VT;
    switch (Intrinsic) {
    case Intrinsic::ppc_altivec_stvebx:
      VT = MVT::i8;
      break;
    case Intrinsic::ppc_altivec_stvehx:
      VT = MVT::i16;
      break;
    case Intrinsic::ppc_altivec_stvewx:
      VT = MVT::i32;
      break;
    case Intrinsic::ppc_vsx_stxvd2x:
      VT = MVT::v2f64;
      break;
    case Intrinsic::ppc_qpx_qvstfd:
      VT = MVT::v4f64;
      break;
    case Intrinsic::ppc_qpx_qvstfs:
      VT = MVT::v4f32;
      break;
    case Intrinsic::ppc_qpx_qvstfcd:
      VT = MVT::v2f64;
      break;
    case Intrinsic::ppc_qpx_qvstfcs:
      VT = MVT::v2f32;
      break;
    default:
      VT = MVT::v4i32;
      break;
    }

    Info.opc = ISD::INTRINSIC_VOID;
    Info.memVT = VT;
    Info.ptrVal = I.getArgOperand(1);
    Info.offset = -VT.getStoreSize()+1;
    Info.size = 2*VT.getStoreSize()-1;
    Info.align = 1;
    Info.flags = MachineMemOperand::MOStore;
    return true;
  }
  case Intrinsic::ppc_qpx_qvstfda:
  case Intrinsic::ppc_qpx_qvstfsa:
  case Intrinsic::ppc_qpx_qvstfcda:
  case Intrinsic::ppc_qpx_qvstfcsa:
  case Intrinsic::ppc_qpx_qvstfiwa: {
    EVT VT;
    switch (Intrinsic) {
    case Intrinsic::ppc_qpx_qvstfda:
      VT = MVT::v4f64;
      break;
    case Intrinsic::ppc_qpx_qvstfsa:
      VT = MVT::v4f32;
      break;
    case Intrinsic::ppc_qpx_qvstfcda:
      VT = MVT::v2f64;
      break;
    case Intrinsic::ppc_qpx_qvstfcsa:
      VT = MVT::v2f32;
      break;
    default:
      VT = MVT::v4i32;
      break;
    }

    Info.opc = ISD::INTRINSIC_VOID;
    Info.memVT = VT;
    Info.ptrVal = I.getArgOperand(1);
    Info.offset = 0;
    Info.size = VT.getStoreSize();
    Info.align = 1;
    Info.flags = MachineMemOperand::MOStore;
    return true;
  }
  default:
    break;
  }

  return false;
}

/// getOptimalMemOpType - Returns the target specific optimal type for load
/// and store operations as a result of memset, memcpy, and memmove
/// lowering. If DstAlign is zero that means it's safe to destination
/// alignment can satisfy any constraint. Similarly if SrcAlign is zero it
/// means there isn't a need to check it against alignment requirement,
/// probably because the source does not need to be loaded. If 'IsMemset' is
/// true, that means it's expanding a memset. If 'ZeroMemset' is true, that
/// means it's a memset of zero. 'MemcpyStrSrc' indicates whether the memcpy
/// source is constant so it does not need to be loaded.
/// It returns EVT::Other if the type should be determined using generic
/// target-independent logic.
EVT PPCTargetLowering::getOptimalMemOpType(uint64_t Size,
                                           unsigned DstAlign, unsigned SrcAlign,
                                           bool IsMemset, bool ZeroMemset,
                                           bool MemcpyStrSrc,
                                           MachineFunction &MF) const {
  if (getTargetMachine().getOptLevel() != CodeGenOpt::None) {
    const Function &F = MF.getFunction();
    // When expanding a memset, require at least two QPX instructions to cover
    // the cost of loading the value to be stored from the constant pool.
    if (Subtarget.hasQPX() && Size >= 32 && (!IsMemset || Size >= 64) &&
       (!SrcAlign || SrcAlign >= 32) && (!DstAlign || DstAlign >= 32) &&
        !F.hasFnAttribute(Attribute::NoImplicitFloat)) {
      return MVT::v4f64;
    }

    // We should use Altivec/VSX loads and stores when available. For unaligned
    // addresses, unaligned VSX loads are only fast starting with the P8.
    if (Subtarget.hasAltivec() && Size >= 16 &&
        (((!SrcAlign || SrcAlign >= 16) && (!DstAlign || DstAlign >= 16)) ||
         ((IsMemset && Subtarget.hasVSX()) || Subtarget.hasP8Vector())))
      return MVT::v4i32;
  }

  if (Subtarget.isPPC64()) {
    return MVT::i64;
  }

  return MVT::i32;
}

/// Returns true if it is beneficial to convert a load of a constant
/// to just the constant itself.
bool PPCTargetLowering::shouldConvertConstantLoadToIntImm(const APInt &Imm,
                                                          Type *Ty) const {
  assert(Ty->isIntegerTy());

  unsigned BitSize = Ty->getPrimitiveSizeInBits();
  return !(BitSize == 0 || BitSize > 64);
}

bool PPCTargetLowering::isTruncateFree(Type *Ty1, Type *Ty2) const {
  if (!Ty1->isIntegerTy() || !Ty2->isIntegerTy())
    return false;
  unsigned NumBits1 = Ty1->getPrimitiveSizeInBits();
  unsigned NumBits2 = Ty2->getPrimitiveSizeInBits();
  return NumBits1 == 64 && NumBits2 == 32;
}

bool PPCTargetLowering::isTruncateFree(EVT VT1, EVT VT2) const {
  if (!VT1.isInteger() || !VT2.isInteger())
    return false;
  unsigned NumBits1 = VT1.getSizeInBits();
  unsigned NumBits2 = VT2.getSizeInBits();
  return NumBits1 == 64 && NumBits2 == 32;
}

bool PPCTargetLowering::isZExtFree(SDValue Val, EVT VT2) const {
  // Generally speaking, zexts are not free, but they are free when they can be
  // folded with other operations.
  if (LoadSDNode *LD = dyn_cast<LoadSDNode>(Val)) {
    EVT MemVT = LD->getMemoryVT();
    if ((MemVT == MVT::i1 || MemVT == MVT::i8 || MemVT == MVT::i16 ||
         (Subtarget.isPPC64() && MemVT == MVT::i32)) &&
        (LD->getExtensionType() == ISD::NON_EXTLOAD ||
         LD->getExtensionType() == ISD::ZEXTLOAD))
      return true;
  }

  // FIXME: Add other cases...
  //  - 32-bit shifts with a zext to i64
  //  - zext after ctlz, bswap, etc.
  //  - zext after and by a constant mask

  return TargetLowering::isZExtFree(Val, VT2);
}

bool PPCTargetLowering::isFPExtFree(EVT DestVT, EVT SrcVT) const {
  assert(DestVT.isFloatingPoint() && SrcVT.isFloatingPoint() &&
         "invalid fpext types");
  // Extending to float128 is not free.
  if (DestVT == MVT::f128)
    return false;
  return true;
}

bool PPCTargetLowering::isLegalICmpImmediate(int64_t Imm) const {
  return isInt<16>(Imm) || isUInt<16>(Imm);
}

bool PPCTargetLowering::isLegalAddImmediate(int64_t Imm) const {
  return isInt<16>(Imm) || isUInt<16>(Imm);
}

bool PPCTargetLowering::allowsMisalignedMemoryAccesses(EVT VT,
                                                       unsigned,
                                                       unsigned,
                                                       bool *Fast) const {
  if (DisablePPCUnaligned)
    return false;

  // PowerPC supports unaligned memory access for simple non-vector types.
  // Although accessing unaligned addresses is not as efficient as accessing
  // aligned addresses, it is generally more efficient than manual expansion,
  // and generally only traps for software emulation when crossing page
  // boundaries.

  if (!VT.isSimple())
    return false;

  if (VT.getSimpleVT().isVector()) {
    if (Subtarget.hasVSX()) {
      if (VT != MVT::v2f64 && VT != MVT::v2i64 &&
          VT != MVT::v4f32 && VT != MVT::v4i32)
        return false;
    } else {
      return false;
    }
  }

  if (VT == MVT::ppcf128)
    return false;

  if (Fast)
    *Fast = true;

  return true;
}

bool PPCTargetLowering::isFMAFasterThanFMulAndFAdd(EVT VT) const {
  VT = VT.getScalarType();

  if (!VT.isSimple())
    return false;

  switch (VT.getSimpleVT().SimpleTy) {
  case MVT::f32:
  case MVT::f64:
    return true;
  case MVT::f128:
    return (EnableQuadPrecision && Subtarget.hasP9Vector());
  default:
    break;
  }

  return false;
}

const MCPhysReg *
PPCTargetLowering::getScratchRegisters(CallingConv::ID) const {
  // LR is a callee-save register, but we must treat it as clobbered by any call
  // site. Hence we include LR in the scratch registers, which are in turn added
  // as implicit-defs for stackmaps and patchpoints. The same reasoning applies
  // to CTR, which is used by any indirect call.
  static const MCPhysReg ScratchRegs[] = {
    PPC::X12, PPC::LR8, PPC::CTR8, 0
  };

  return ScratchRegs;
}

unsigned PPCTargetLowering::getExceptionPointerRegister(
    const Constant *PersonalityFn) const {
  return Subtarget.isPPC64() ? PPC::X3 : PPC::R3;
}

unsigned PPCTargetLowering::getExceptionSelectorRegister(
    const Constant *PersonalityFn) const {
  return Subtarget.isPPC64() ? PPC::X4 : PPC::R4;
}

bool
PPCTargetLowering::shouldExpandBuildVectorWithShuffles(
                     EVT VT , unsigned DefinedValues) const {
  if (VT == MVT::v2i64)
    return Subtarget.hasDirectMove(); // Don't need stack ops with direct moves

  if (Subtarget.hasVSX() || Subtarget.hasQPX())
    return true;

  return TargetLowering::shouldExpandBuildVectorWithShuffles(VT, DefinedValues);
}

Sched::Preference PPCTargetLowering::getSchedulingPreference(SDNode *N) const {
  if (DisableILPPref || Subtarget.enableMachineScheduler())
    return TargetLowering::getSchedulingPreference(N);

  return Sched::ILP;
}

// Create a fast isel object.
FastISel *
PPCTargetLowering::createFastISel(FunctionLoweringInfo &FuncInfo,
                                  const TargetLibraryInfo *LibInfo) const {
  return PPC::createFastISel(FuncInfo, LibInfo);
}

void PPCTargetLowering::initializeSplitCSR(MachineBasicBlock *Entry) const {
  if (Subtarget.isDarwinABI()) return;
  if (!Subtarget.isPPC64()) return;

  // Update IsSplitCSR in PPCFunctionInfo
  PPCFunctionInfo *PFI = Entry->getParent()->getInfo<PPCFunctionInfo>();
  PFI->setIsSplitCSR(true);
}

void PPCTargetLowering::insertCopiesSplitCSR(
  MachineBasicBlock *Entry,
  const SmallVectorImpl<MachineBasicBlock *> &Exits) const {
  const PPCRegisterInfo *TRI = Subtarget.getRegisterInfo();
  const MCPhysReg *IStart = TRI->getCalleeSavedRegsViaCopy(Entry->getParent());
  if (!IStart)
    return;

  const TargetInstrInfo *TII = Subtarget.getInstrInfo();
  MachineRegisterInfo *MRI = &Entry->getParent()->getRegInfo();
  MachineBasicBlock::iterator MBBI = Entry->begin();
  for (const MCPhysReg *I = IStart; *I; ++I) {
    const TargetRegisterClass *RC = nullptr;
    if (PPC::G8RCRegClass.contains(*I))
      RC = &PPC::G8RCRegClass;
    else if (PPC::F8RCRegClass.contains(*I))
      RC = &PPC::F8RCRegClass;
    else if (PPC::CRRCRegClass.contains(*I))
      RC = &PPC::CRRCRegClass;
    else if (PPC::VRRCRegClass.contains(*I))
      RC = &PPC::VRRCRegClass;
    else
      llvm_unreachable("Unexpected register class in CSRsViaCopy!");

    unsigned NewVR = MRI->createVirtualRegister(RC);
    // Create copy from CSR to a virtual register.
    // FIXME: this currently does not emit CFI pseudo-instructions, it works
    // fine for CXX_FAST_TLS since the C++-style TLS access functions should be
    // nounwind. If we want to generalize this later, we may need to emit
    // CFI pseudo-instructions.
    assert(Entry->getParent()->getFunction().hasFnAttribute(
             Attribute::NoUnwind) &&
           "Function should be nounwind in insertCopiesSplitCSR!");
    Entry->addLiveIn(*I);
    BuildMI(*Entry, MBBI, DebugLoc(), TII->get(TargetOpcode::COPY), NewVR)
      .addReg(*I);

    // Insert the copy-back instructions right before the terminator
    for (auto *Exit : Exits)
      BuildMI(*Exit, Exit->getFirstTerminator(), DebugLoc(),
              TII->get(TargetOpcode::COPY), *I)
        .addReg(NewVR);
  }
}

// Override to enable LOAD_STACK_GUARD lowering on Linux.
bool PPCTargetLowering::useLoadStackGuardNode() const {
  if (!Subtarget.isTargetLinux())
    return TargetLowering::useLoadStackGuardNode();
  return true;
}

// Override to disable global variable loading on Linux.
void PPCTargetLowering::insertSSPDeclarations(Module &M) const {
  if (!Subtarget.isTargetLinux())
    return TargetLowering::insertSSPDeclarations(M);
}

bool PPCTargetLowering::isFPImmLegal(const APFloat &Imm, EVT VT) const {
  if (!VT.isSimple() || !Subtarget.hasVSX())
    return false;

  switch(VT.getSimpleVT().SimpleTy) {
  default:
    // For FP types that are currently not supported by PPC backend, return
    // false. Examples: f16, f80.
    return false;
  case MVT::f32:
  case MVT::f64:
  case MVT::ppcf128:
    return Imm.isPosZero();
  }
}

// For vector shift operation op, fold
// (op x, (and y, ((1 << numbits(x)) - 1))) -> (target op x, y)
static SDValue stripModuloOnShift(const TargetLowering &TLI, SDNode *N,
                                  SelectionDAG &DAG) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  EVT VT = N0.getValueType();
  unsigned OpSizeInBits = VT.getScalarSizeInBits();
  unsigned Opcode = N->getOpcode();
  unsigned TargetOpcode;

  switch (Opcode) {
  default:
    llvm_unreachable("Unexpected shift operation");
  case ISD::SHL:
    TargetOpcode = PPCISD::SHL;
    break;
  case ISD::SRL:
    TargetOpcode = PPCISD::SRL;
    break;
  case ISD::SRA:
    TargetOpcode = PPCISD::SRA;
    break;
  }

  if (VT.isVector() && TLI.isOperationLegal(Opcode, VT) &&
      N1->getOpcode() == ISD::AND)
    if (ConstantSDNode *Mask = isConstOrConstSplat(N1->getOperand(1)))
      if (Mask->getZExtValue() == OpSizeInBits - 1)
        return DAG.getNode(TargetOpcode, SDLoc(N), VT, N0, N1->getOperand(0));

  return SDValue();
}

SDValue PPCTargetLowering::combineSHL(SDNode *N, DAGCombinerInfo &DCI) const {
  if (auto Value = stripModuloOnShift(*this, N, DCI.DAG))
    return Value;

  SDValue N0 = N->getOperand(0);
  ConstantSDNode *CN1 = dyn_cast<ConstantSDNode>(N->getOperand(1));
  if (!Subtarget.isISA3_0() ||
      N0.getOpcode() != ISD::SIGN_EXTEND ||
      N0.getOperand(0).getValueType() != MVT::i32 ||
      CN1 == nullptr || N->getValueType(0) != MVT::i64)
    return SDValue();

  // We can't save an operation here if the value is already extended, and
  // the existing shift is easier to combine.
  SDValue ExtsSrc = N0.getOperand(0);
  if (ExtsSrc.getOpcode() == ISD::TRUNCATE &&
      ExtsSrc.getOperand(0).getOpcode() == ISD::AssertSext)
    return SDValue();

  SDLoc DL(N0);
  SDValue ShiftBy = SDValue(CN1, 0);
  // We want the shift amount to be i32 on the extswli, but the shift could
  // have an i64.
  if (ShiftBy.getValueType() == MVT::i64)
    ShiftBy = DCI.DAG.getConstant(CN1->getZExtValue(), DL, MVT::i32);

  return DCI.DAG.getNode(PPCISD::EXTSWSLI, DL, MVT::i64, N0->getOperand(0),
                         ShiftBy);
}

SDValue PPCTargetLowering::combineSRA(SDNode *N, DAGCombinerInfo &DCI) const {
  if (auto Value = stripModuloOnShift(*this, N, DCI.DAG))
    return Value;

  return SDValue();
}

SDValue PPCTargetLowering::combineSRL(SDNode *N, DAGCombinerInfo &DCI) const {
  if (auto Value = stripModuloOnShift(*this, N, DCI.DAG))
    return Value;

  return SDValue();
}

// Transform (add X, (zext(setne Z, C))) -> (addze X, (addic (addi Z, -C), -1))
// Transform (add X, (zext(sete  Z, C))) -> (addze X, (subfic (addi Z, -C), 0))
// When C is zero, the equation (addi Z, -C) can be simplified to Z
// Requirement: -C in [-32768, 32767], X and Z are MVT::i64 types
static SDValue combineADDToADDZE(SDNode *N, SelectionDAG &DAG,
                                 const PPCSubtarget &Subtarget) {
  if (!Subtarget.isPPC64())
    return SDValue();

  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);

  auto isZextOfCompareWithConstant = [](SDValue Op) {
    if (Op.getOpcode() != ISD::ZERO_EXTEND || !Op.hasOneUse() ||
        Op.getValueType() != MVT::i64)
      return false;

    SDValue Cmp = Op.getOperand(0);
    if (Cmp.getOpcode() != ISD::SETCC || !Cmp.hasOneUse() ||
        Cmp.getOperand(0).getValueType() != MVT::i64)
      return false;

    if (auto *Constant = dyn_cast<ConstantSDNode>(Cmp.getOperand(1))) {
      int64_t NegConstant = 0 - Constant->getSExtValue();
      // Due to the limitations of the addi instruction,
      // -C is required to be [-32768, 32767].
      return isInt<16>(NegConstant);
    }

    return false;
  };

  bool LHSHasPattern = isZextOfCompareWithConstant(LHS);
  bool RHSHasPattern = isZextOfCompareWithConstant(RHS);

  // If there is a pattern, canonicalize a zext operand to the RHS.
  if (LHSHasPattern && !RHSHasPattern)
    std::swap(LHS, RHS);
  else if (!LHSHasPattern && !RHSHasPattern)
    return SDValue();

  SDLoc DL(N);
  SDVTList VTs = DAG.getVTList(MVT::i64, MVT::Glue);
  SDValue Cmp = RHS.getOperand(0);
  SDValue Z = Cmp.getOperand(0);
  auto *Constant = dyn_cast<ConstantSDNode>(Cmp.getOperand(1));

  assert(Constant && "Constant Should not be a null pointer.");
  int64_t NegConstant = 0 - Constant->getSExtValue();

  switch(cast<CondCodeSDNode>(Cmp.getOperand(2))->get()) {
  default: break;
  case ISD::SETNE: {
    //                                 when C == 0
    //                             --> addze X, (addic Z, -1).carry
    //                            /
    // add X, (zext(setne Z, C))--
    //                            \    when -32768 <= -C <= 32767 && C != 0
    //                             --> addze X, (addic (addi Z, -C), -1).carry
    SDValue Add = DAG.getNode(ISD::ADD, DL, MVT::i64, Z,
                              DAG.getConstant(NegConstant, DL, MVT::i64));
    SDValue AddOrZ = NegConstant != 0 ? Add : Z;
    SDValue Addc = DAG.getNode(ISD::ADDC, DL, DAG.getVTList(MVT::i64, MVT::Glue),
                               AddOrZ, DAG.getConstant(-1ULL, DL, MVT::i64));
    return DAG.getNode(ISD::ADDE, DL, VTs, LHS, DAG.getConstant(0, DL, MVT::i64),
                       SDValue(Addc.getNode(), 1));
    }
  case ISD::SETEQ: {
    //                                 when C == 0
    //                             --> addze X, (subfic Z, 0).carry
    //                            /
    // add X, (zext(sete  Z, C))--
    //                            \    when -32768 <= -C <= 32767 && C != 0
    //                             --> addze X, (subfic (addi Z, -C), 0).carry
    SDValue Add = DAG.getNode(ISD::ADD, DL, MVT::i64, Z,
                              DAG.getConstant(NegConstant, DL, MVT::i64));
    SDValue AddOrZ = NegConstant != 0 ? Add : Z;
    SDValue Subc = DAG.getNode(ISD::SUBC, DL, DAG.getVTList(MVT::i64, MVT::Glue),
                               DAG.getConstant(0, DL, MVT::i64), AddOrZ);
    return DAG.getNode(ISD::ADDE, DL, VTs, LHS, DAG.getConstant(0, DL, MVT::i64),
                       SDValue(Subc.getNode(), 1));
    }
  }

  return SDValue();
}

SDValue PPCTargetLowering::combineADD(SDNode *N, DAGCombinerInfo &DCI) const {
  if (auto Value = combineADDToADDZE(N, DCI.DAG, Subtarget))
    return Value;

  return SDValue();
}

// Detect TRUNCATE operations on bitcasts of float128 values.
// What we are looking for here is the situtation where we extract a subset
// of bits from a 128 bit float.
// This can be of two forms:
// 1) BITCAST of f128 feeding TRUNCATE
// 2) BITCAST of f128 feeding SRL (a shift) feeding TRUNCATE
// The reason this is required is because we do not have a legal i128 type
// and so we want to prevent having to store the f128 and then reload part
// of it.
SDValue PPCTargetLowering::combineTRUNCATE(SDNode *N,
                                           DAGCombinerInfo &DCI) const {
  // If we are using CRBits then try that first.
  if (Subtarget.useCRBits()) {
    // Check if CRBits did anything and return that if it did.
    if (SDValue CRTruncValue = DAGCombineTruncBoolExt(N, DCI))
      return CRTruncValue;
  }

  SDLoc dl(N);
  SDValue Op0 = N->getOperand(0);

  // Looking for a truncate of i128 to i64.
  if (Op0.getValueType() != MVT::i128 || N->getValueType(0) != MVT::i64)
    return SDValue();

  int EltToExtract = DCI.DAG.getDataLayout().isBigEndian() ? 1 : 0;

  // SRL feeding TRUNCATE.
  if (Op0.getOpcode() == ISD::SRL) {
    ConstantSDNode *ConstNode = dyn_cast<ConstantSDNode>(Op0.getOperand(1));
    // The right shift has to be by 64 bits.
    if (!ConstNode || ConstNode->getZExtValue() != 64)
      return SDValue();

    // Switch the element number to extract.
    EltToExtract = EltToExtract ? 0 : 1;
    // Update Op0 past the SRL.
    Op0 = Op0.getOperand(0);
  }

  // BITCAST feeding a TRUNCATE possibly via SRL.
  if (Op0.getOpcode() == ISD::BITCAST &&
      Op0.getValueType() == MVT::i128 &&
      Op0.getOperand(0).getValueType() == MVT::f128) {
    SDValue Bitcast = DCI.DAG.getBitcast(MVT::v2i64, Op0.getOperand(0));
    return DCI.DAG.getNode(
        ISD::EXTRACT_VECTOR_ELT, dl, MVT::i64, Bitcast,
        DCI.DAG.getTargetConstant(EltToExtract, dl, MVT::i32));
  }
  return SDValue();
}

bool PPCTargetLowering::mayBeEmittedAsTailCall(const CallInst *CI) const {
  // Only duplicate to increase tail-calls for the 64bit SysV ABIs.
  if (!Subtarget.isSVR4ABI() || !Subtarget.isPPC64())
    return false;

  // If not a tail call then no need to proceed.
  if (!CI->isTailCall())
    return false;

  // If tail calls are disabled for the caller then we are done.
  const Function *Caller = CI->getParent()->getParent();
  auto Attr = Caller->getFnAttribute("disable-tail-calls");
  if (Attr.getValueAsString() == "true")
    return false;

  // If sibling calls have been disabled and tail-calls aren't guaranteed
  // there is no reason to duplicate.
  auto &TM = getTargetMachine();
  if (!TM.Options.GuaranteedTailCallOpt && DisableSCO)
    return false;

  // Can't tail call a function called indirectly, or if it has variadic args.
  const Function *Callee = CI->getCalledFunction();
  if (!Callee || Callee->isVarArg())
    return false;

  // Make sure the callee and caller calling conventions are eligible for tco.
  if (!areCallingConvEligibleForTCO_64SVR4(Caller->getCallingConv(),
                                           CI->getCallingConv()))
      return false;

  // If the function is local then we have a good chance at tail-calling it
  return getTargetMachine().shouldAssumeDSOLocal(*Caller->getParent(), Callee);
}

bool PPCTargetLowering::hasBitPreservingFPLogic(EVT VT) const {
  if (!Subtarget.hasVSX())
    return false;
  if (Subtarget.hasP9Vector() && VT == MVT::f128)
    return true;
  return VT == MVT::f32 || VT == MVT::f64 ||
    VT == MVT::v4f32 || VT == MVT::v2f64;
}

bool PPCTargetLowering::
isMaskAndCmp0FoldingBeneficial(const Instruction &AndI) const {
  const Value *Mask = AndI.getOperand(1);
  // If the mask is suitable for andi. or andis. we should sink the and.
  if (const ConstantInt *CI = dyn_cast<ConstantInt>(Mask)) {
    // Can't handle constants wider than 64-bits.
    if (CI->getBitWidth() > 64)
      return false;
    int64_t ConstVal = CI->getZExtValue();
    return isUInt<16>(ConstVal) ||
      (isUInt<16>(ConstVal >> 16) && !(ConstVal & 0xFFFF));
  }

  // For non-constant masks, we can always use the record-form and.
  return true;
}

// Transform (abs (sub (zext a), (zext b))) to (vabsd a b 0)
// Transform (abs (sub (zext a), (zext_invec b))) to (vabsd a b 0)
// Transform (abs (sub (zext_invec a), (zext_invec b))) to (vabsd a b 0)
// Transform (abs (sub (zext_invec a), (zext b))) to (vabsd a b 0)
// Transform (abs (sub a, b) to (vabsd a b 1)) if a & b of type v4i32
SDValue PPCTargetLowering::combineABS(SDNode *N, DAGCombinerInfo &DCI) const {
  assert((N->getOpcode() == ISD::ABS) && "Need ABS node here");
  assert(Subtarget.hasP9Altivec() &&
         "Only combine this when P9 altivec supported!");
  EVT VT = N->getValueType(0);
  if (VT != MVT::v4i32 && VT != MVT::v8i16 && VT != MVT::v16i8)
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDLoc dl(N);
  if (N->getOperand(0).getOpcode() == ISD::SUB) {
    // Even for signed integers, if it's known to be positive (as signed
    // integer) due to zero-extended inputs.
    unsigned SubOpcd0 = N->getOperand(0)->getOperand(0).getOpcode();
    unsigned SubOpcd1 = N->getOperand(0)->getOperand(1).getOpcode();
    if ((SubOpcd0 == ISD::ZERO_EXTEND ||
         SubOpcd0 == ISD::ZERO_EXTEND_VECTOR_INREG) &&
        (SubOpcd1 == ISD::ZERO_EXTEND ||
         SubOpcd1 == ISD::ZERO_EXTEND_VECTOR_INREG)) {
      return DAG.getNode(PPCISD::VABSD, dl, N->getOperand(0).getValueType(),
                         N->getOperand(0)->getOperand(0),
                         N->getOperand(0)->getOperand(1),
                         DAG.getTargetConstant(0, dl, MVT::i32));
    }

    // For type v4i32, it can be optimized with xvnegsp + vabsduw
    if (N->getOperand(0).getValueType() == MVT::v4i32 &&
        N->getOperand(0).hasOneUse()) {
      return DAG.getNode(PPCISD::VABSD, dl, N->getOperand(0).getValueType(),
                         N->getOperand(0)->getOperand(0),
                         N->getOperand(0)->getOperand(1),
                         DAG.getTargetConstant(1, dl, MVT::i32));
    }
  }

  return SDValue();
}

// For type v4i32/v8ii16/v16i8, transform
// from (vselect (setcc a, b, setugt), (sub a, b), (sub b, a)) to (vabsd a, b)
// from (vselect (setcc a, b, setuge), (sub a, b), (sub b, a)) to (vabsd a, b)
// from (vselect (setcc a, b, setult), (sub b, a), (sub a, b)) to (vabsd a, b)
// from (vselect (setcc a, b, setule), (sub b, a), (sub a, b)) to (vabsd a, b)
SDValue PPCTargetLowering::combineVSelect(SDNode *N,
                                          DAGCombinerInfo &DCI) const {
  assert((N->getOpcode() == ISD::VSELECT) && "Need VSELECT node here");
  assert(Subtarget.hasP9Altivec() &&
         "Only combine this when P9 altivec supported!");

  SelectionDAG &DAG = DCI.DAG;
  SDLoc dl(N);
  SDValue Cond = N->getOperand(0);
  SDValue TrueOpnd = N->getOperand(1);
  SDValue FalseOpnd = N->getOperand(2);
  EVT VT = N->getOperand(1).getValueType();

  if (Cond.getOpcode() != ISD::SETCC || TrueOpnd.getOpcode() != ISD::SUB ||
      FalseOpnd.getOpcode() != ISD::SUB)
    return SDValue();

  // ABSD only available for type v4i32/v8i16/v16i8
  if (VT != MVT::v4i32 && VT != MVT::v8i16 && VT != MVT::v16i8)
    return SDValue();

  // At least to save one more dependent computation
  if (!(Cond.hasOneUse() || TrueOpnd.hasOneUse() || FalseOpnd.hasOneUse()))
    return SDValue();

  ISD::CondCode CC = cast<CondCodeSDNode>(Cond.getOperand(2))->get();

  // Can only handle unsigned comparison here
  switch (CC) {
  default:
    return SDValue();
  case ISD::SETUGT:
  case ISD::SETUGE:
    break;
  case ISD::SETULT:
  case ISD::SETULE:
    std::swap(TrueOpnd, FalseOpnd);
    break;
  }

  SDValue CmpOpnd1 = Cond.getOperand(0);
  SDValue CmpOpnd2 = Cond.getOperand(1);

  // SETCC CmpOpnd1 CmpOpnd2 cond
  // TrueOpnd = CmpOpnd1 - CmpOpnd2
  // FalseOpnd = CmpOpnd2 - CmpOpnd1
  if (TrueOpnd.getOperand(0) == CmpOpnd1 &&
      TrueOpnd.getOperand(1) == CmpOpnd2 &&
      FalseOpnd.getOperand(0) == CmpOpnd2 &&
      FalseOpnd.getOperand(1) == CmpOpnd1) {
    return DAG.getNode(PPCISD::VABSD, dl, N->getOperand(1).getValueType(),
                       CmpOpnd1, CmpOpnd2,
                       DAG.getTargetConstant(0, dl, MVT::i32));
  }

  return SDValue();
}
