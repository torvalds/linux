//===-- NVPTXISelLowering.cpp - NVPTX DAG Lowering Implementation ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that NVPTX uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#include "NVPTXISelLowering.h"
#include "MCTargetDesc/NVPTXBaseInfo.h"
#include "NVPTX.h"
#include "NVPTXSubtarget.h"
#include "NVPTXTargetMachine.h"
#include "NVPTXTargetObjectFile.h"
#include "NVPTXUtilities.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/TargetCallingConv.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MachineValueType.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#define DEBUG_TYPE "nvptx-lower"

using namespace llvm;

static unsigned int uniqueCallSite = 0;

static cl::opt<bool> sched4reg(
    "nvptx-sched4reg",
    cl::desc("NVPTX Specific: schedule for register pressue"), cl::init(false));

static cl::opt<unsigned>
FMAContractLevelOpt("nvptx-fma-level", cl::ZeroOrMore, cl::Hidden,
                    cl::desc("NVPTX Specific: FMA contraction (0: don't do it"
                             " 1: do it  2: do it aggressively"),
                    cl::init(2));

static cl::opt<int> UsePrecDivF32(
    "nvptx-prec-divf32", cl::ZeroOrMore, cl::Hidden,
    cl::desc("NVPTX Specifies: 0 use div.approx, 1 use div.full, 2 use"
             " IEEE Compliant F32 div.rnd if available."),
    cl::init(2));

static cl::opt<bool> UsePrecSqrtF32(
    "nvptx-prec-sqrtf32", cl::Hidden,
    cl::desc("NVPTX Specific: 0 use sqrt.approx, 1 use sqrt.rn."),
    cl::init(true));

static cl::opt<bool> FtzEnabled(
    "nvptx-f32ftz", cl::ZeroOrMore, cl::Hidden,
    cl::desc("NVPTX Specific: Flush f32 subnormals to sign-preserving zero."),
    cl::init(false));

int NVPTXTargetLowering::getDivF32Level() const {
  if (UsePrecDivF32.getNumOccurrences() > 0) {
    // If nvptx-prec-div32=N is used on the command-line, always honor it
    return UsePrecDivF32;
  } else {
    // Otherwise, use div.approx if fast math is enabled
    if (getTargetMachine().Options.UnsafeFPMath)
      return 0;
    else
      return 2;
  }
}

bool NVPTXTargetLowering::usePrecSqrtF32() const {
  if (UsePrecSqrtF32.getNumOccurrences() > 0) {
    // If nvptx-prec-sqrtf32 is used on the command-line, always honor it
    return UsePrecSqrtF32;
  } else {
    // Otherwise, use sqrt.approx if fast math is enabled
    return !getTargetMachine().Options.UnsafeFPMath;
  }
}

bool NVPTXTargetLowering::useF32FTZ(const MachineFunction &MF) const {
  // TODO: Get rid of this flag; there can be only one way to do this.
  if (FtzEnabled.getNumOccurrences() > 0) {
    // If nvptx-f32ftz is used on the command-line, always honor it
    return FtzEnabled;
  } else {
    const Function &F = MF.getFunction();
    // Otherwise, check for an nvptx-f32ftz attribute on the function
    if (F.hasFnAttribute("nvptx-f32ftz"))
      return F.getFnAttribute("nvptx-f32ftz").getValueAsString() == "true";
    else
      return false;
  }
}

static bool IsPTXVectorType(MVT VT) {
  switch (VT.SimpleTy) {
  default:
    return false;
  case MVT::v2i1:
  case MVT::v4i1:
  case MVT::v2i8:
  case MVT::v4i8:
  case MVT::v2i16:
  case MVT::v4i16:
  case MVT::v2i32:
  case MVT::v4i32:
  case MVT::v2i64:
  case MVT::v2f16:
  case MVT::v4f16:
  case MVT::v8f16: // <4 x f16x2>
  case MVT::v2f32:
  case MVT::v4f32:
  case MVT::v2f64:
    return true;
  }
}

/// ComputePTXValueVTs - For the given Type \p Ty, returns the set of primitive
/// EVTs that compose it.  Unlike ComputeValueVTs, this will break apart vectors
/// into their primitive components.
/// NOTE: This is a band-aid for code that expects ComputeValueVTs to return the
/// same number of types as the Ins/Outs arrays in LowerFormalArguments,
/// LowerCall, and LowerReturn.
static void ComputePTXValueVTs(const TargetLowering &TLI, const DataLayout &DL,
                               Type *Ty, SmallVectorImpl<EVT> &ValueVTs,
                               SmallVectorImpl<uint64_t> *Offsets = nullptr,
                               uint64_t StartingOffset = 0) {
  SmallVector<EVT, 16> TempVTs;
  SmallVector<uint64_t, 16> TempOffsets;

  // Special case for i128 - decompose to (i64, i64)
  if (Ty->isIntegerTy(128)) {
    ValueVTs.push_back(EVT(MVT::i64));
    ValueVTs.push_back(EVT(MVT::i64));

    if (Offsets) {
      Offsets->push_back(StartingOffset + 0);
      Offsets->push_back(StartingOffset + 8);
    }

    return;
  }

  // Given a struct type, recursively traverse the elements with custom ComputePTXValueVTs.
  if (StructType *STy = dyn_cast<StructType>(Ty)) {
    auto const *SL = DL.getStructLayout(STy);
    auto ElementNum = 0;
    for(auto *EI : STy->elements()) {
      ComputePTXValueVTs(TLI, DL, EI, ValueVTs, Offsets,
                         StartingOffset + SL->getElementOffset(ElementNum));
      ++ElementNum;
    }
    return;
  }

  ComputeValueVTs(TLI, DL, Ty, TempVTs, &TempOffsets, StartingOffset);
  for (unsigned i = 0, e = TempVTs.size(); i != e; ++i) {
    EVT VT = TempVTs[i];
    uint64_t Off = TempOffsets[i];
    // Split vectors into individual elements, except for v2f16, which
    // we will pass as a single scalar.
    if (VT.isVector()) {
      unsigned NumElts = VT.getVectorNumElements();
      EVT EltVT = VT.getVectorElementType();
      // Vectors with an even number of f16 elements will be passed to
      // us as an array of v2f16 elements. We must match this so we
      // stay in sync with Ins/Outs.
      if (EltVT == MVT::f16 && NumElts % 2 == 0) {
        EltVT = MVT::v2f16;
        NumElts /= 2;
      }
      for (unsigned j = 0; j != NumElts; ++j) {
        ValueVTs.push_back(EltVT);
        if (Offsets)
          Offsets->push_back(Off + j * EltVT.getStoreSize());
      }
    } else {
      ValueVTs.push_back(VT);
      if (Offsets)
        Offsets->push_back(Off);
    }
  }
}

// Check whether we can merge loads/stores of some of the pieces of a
// flattened function parameter or return value into a single vector
// load/store.
//
// The flattened parameter is represented as a list of EVTs and
// offsets, and the whole structure is aligned to ParamAlignment. This
// function determines whether we can load/store pieces of the
// parameter starting at index Idx using a single vectorized op of
// size AccessSize. If so, it returns the number of param pieces
// covered by the vector op. Otherwise, it returns 1.
static unsigned CanMergeParamLoadStoresStartingAt(
    unsigned Idx, uint32_t AccessSize, const SmallVectorImpl<EVT> &ValueVTs,
    const SmallVectorImpl<uint64_t> &Offsets, unsigned ParamAlignment) {
  assert(isPowerOf2_32(AccessSize) && "must be a power of 2!");

  // Can't vectorize if param alignment is not sufficient.
  if (AccessSize > ParamAlignment)
    return 1;
  // Can't vectorize if offset is not aligned.
  if (Offsets[Idx] & (AccessSize - 1))
    return 1;

  EVT EltVT = ValueVTs[Idx];
  unsigned EltSize = EltVT.getStoreSize();

  // Element is too large to vectorize.
  if (EltSize >= AccessSize)
    return 1;

  unsigned NumElts = AccessSize / EltSize;
  // Can't vectorize if AccessBytes if not a multiple of EltSize.
  if (AccessSize != EltSize * NumElts)
    return 1;

  // We don't have enough elements to vectorize.
  if (Idx + NumElts > ValueVTs.size())
    return 1;

  // PTX ISA can only deal with 2- and 4-element vector ops.
  if (NumElts != 4 && NumElts != 2)
    return 1;

  for (unsigned j = Idx + 1; j < Idx + NumElts; ++j) {
    // Types do not match.
    if (ValueVTs[j] != EltVT)
      return 1;

    // Elements are not contiguous.
    if (Offsets[j] - Offsets[j - 1] != EltSize)
      return 1;
  }
  // OK. We can vectorize ValueVTs[i..i+NumElts)
  return NumElts;
}

// Flags for tracking per-element vectorization state of loads/stores
// of a flattened function parameter or return value.
enum ParamVectorizationFlags {
  PVF_INNER = 0x0, // Middle elements of a vector.
  PVF_FIRST = 0x1, // First element of the vector.
  PVF_LAST = 0x2,  // Last element of the vector.
  // Scalar is effectively a 1-element vector.
  PVF_SCALAR = PVF_FIRST | PVF_LAST
};

// Computes whether and how we can vectorize the loads/stores of a
// flattened function parameter or return value.
//
// The flattened parameter is represented as the list of ValueVTs and
// Offsets, and is aligned to ParamAlignment bytes. We return a vector
// of the same size as ValueVTs indicating how each piece should be
// loaded/stored (i.e. as a scalar, or as part of a vector
// load/store).
static SmallVector<ParamVectorizationFlags, 16>
VectorizePTXValueVTs(const SmallVectorImpl<EVT> &ValueVTs,
                     const SmallVectorImpl<uint64_t> &Offsets,
                     unsigned ParamAlignment) {
  // Set vector size to match ValueVTs and mark all elements as
  // scalars by default.
  SmallVector<ParamVectorizationFlags, 16> VectorInfo;
  VectorInfo.assign(ValueVTs.size(), PVF_SCALAR);

  // Check what we can vectorize using 128/64/32-bit accesses.
  for (int I = 0, E = ValueVTs.size(); I != E; ++I) {
    // Skip elements we've already processed.
    assert(VectorInfo[I] == PVF_SCALAR && "Unexpected vector info state.");
    for (unsigned AccessSize : {16, 8, 4, 2}) {
      unsigned NumElts = CanMergeParamLoadStoresStartingAt(
          I, AccessSize, ValueVTs, Offsets, ParamAlignment);
      // Mark vectorized elements.
      switch (NumElts) {
      default:
        llvm_unreachable("Unexpected return value");
      case 1:
        // Can't vectorize using this size, try next smaller size.
        continue;
      case 2:
        assert(I + 1 < E && "Not enough elements.");
        VectorInfo[I] = PVF_FIRST;
        VectorInfo[I + 1] = PVF_LAST;
        I += 1;
        break;
      case 4:
        assert(I + 3 < E && "Not enough elements.");
        VectorInfo[I] = PVF_FIRST;
        VectorInfo[I + 1] = PVF_INNER;
        VectorInfo[I + 2] = PVF_INNER;
        VectorInfo[I + 3] = PVF_LAST;
        I += 3;
        break;
      }
      // Break out of the inner loop because we've already succeeded
      // using largest possible AccessSize.
      break;
    }
  }
  return VectorInfo;
}

// NVPTXTargetLowering Constructor.
NVPTXTargetLowering::NVPTXTargetLowering(const NVPTXTargetMachine &TM,
                                         const NVPTXSubtarget &STI)
    : TargetLowering(TM), nvTM(&TM), STI(STI) {
  // always lower memset, memcpy, and memmove intrinsics to load/store
  // instructions, rather
  // then generating calls to memset, mempcy or memmove.
  MaxStoresPerMemset = (unsigned) 0xFFFFFFFF;
  MaxStoresPerMemcpy = (unsigned) 0xFFFFFFFF;
  MaxStoresPerMemmove = (unsigned) 0xFFFFFFFF;

  setBooleanContents(ZeroOrNegativeOneBooleanContent);
  setBooleanVectorContents(ZeroOrNegativeOneBooleanContent);

  // Jump is Expensive. Don't create extra control flow for 'and', 'or'
  // condition branches.
  setJumpIsExpensive(true);

  // Wide divides are _very_ slow. Try to reduce the width of the divide if
  // possible.
  addBypassSlowDiv(64, 32);

  // By default, use the Source scheduling
  if (sched4reg)
    setSchedulingPreference(Sched::RegPressure);
  else
    setSchedulingPreference(Sched::Source);

  auto setFP16OperationAction = [&](unsigned Op, MVT VT, LegalizeAction Action,
                                    LegalizeAction NoF16Action) {
    setOperationAction(Op, VT, STI.allowFP16Math() ? Action : NoF16Action);
  };

  addRegisterClass(MVT::i1, &NVPTX::Int1RegsRegClass);
  addRegisterClass(MVT::i16, &NVPTX::Int16RegsRegClass);
  addRegisterClass(MVT::i32, &NVPTX::Int32RegsRegClass);
  addRegisterClass(MVT::i64, &NVPTX::Int64RegsRegClass);
  addRegisterClass(MVT::f32, &NVPTX::Float32RegsRegClass);
  addRegisterClass(MVT::f64, &NVPTX::Float64RegsRegClass);
  addRegisterClass(MVT::f16, &NVPTX::Float16RegsRegClass);
  addRegisterClass(MVT::v2f16, &NVPTX::Float16x2RegsRegClass);

  // Conversion to/from FP16/FP16x2 is always legal.
  setOperationAction(ISD::SINT_TO_FP, MVT::f16, Legal);
  setOperationAction(ISD::FP_TO_SINT, MVT::f16, Legal);
  setOperationAction(ISD::BUILD_VECTOR, MVT::v2f16, Custom);
  setOperationAction(ISD::EXTRACT_VECTOR_ELT, MVT::v2f16, Custom);
  setOperationAction(ISD::INSERT_VECTOR_ELT, MVT::v2f16, Expand);
  setOperationAction(ISD::VECTOR_SHUFFLE, MVT::v2f16, Expand);

  setFP16OperationAction(ISD::SETCC, MVT::f16, Legal, Promote);
  setFP16OperationAction(ISD::SETCC, MVT::v2f16, Legal, Expand);

  // Operations not directly supported by NVPTX.
  for (MVT VT : {MVT::f16, MVT::v2f16, MVT::f32, MVT::f64, MVT::i1, MVT::i8,
                 MVT::i16, MVT::i32, MVT::i64}) {
    setOperationAction(ISD::SELECT_CC, VT, Expand);
    setOperationAction(ISD::BR_CC, VT, Expand);
  }

  // Some SIGN_EXTEND_INREG can be done using cvt instruction.
  // For others we will expand to a SHL/SRA pair.
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i64, Legal);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i32, Legal);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i16, Legal);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i8 , Legal);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);

  setOperationAction(ISD::SHL_PARTS, MVT::i32  , Custom);
  setOperationAction(ISD::SRA_PARTS, MVT::i32  , Custom);
  setOperationAction(ISD::SRL_PARTS, MVT::i32  , Custom);
  setOperationAction(ISD::SHL_PARTS, MVT::i64  , Custom);
  setOperationAction(ISD::SRA_PARTS, MVT::i64  , Custom);
  setOperationAction(ISD::SRL_PARTS, MVT::i64  , Custom);

  setOperationAction(ISD::BITREVERSE, MVT::i32, Legal);
  setOperationAction(ISD::BITREVERSE, MVT::i64, Legal);

  // TODO: we may consider expanding ROTL/ROTR on older GPUs.  Currently on GPUs
  // that don't have h/w rotation we lower them to multi-instruction assembly.
  // See ROT*_sw in NVPTXIntrInfo.td
  setOperationAction(ISD::ROTL, MVT::i64, Legal);
  setOperationAction(ISD::ROTR, MVT::i64, Legal);
  setOperationAction(ISD::ROTL, MVT::i32, Legal);
  setOperationAction(ISD::ROTR, MVT::i32, Legal);

  setOperationAction(ISD::ROTL, MVT::i16, Expand);
  setOperationAction(ISD::ROTR, MVT::i16, Expand);
  setOperationAction(ISD::ROTL, MVT::i8, Expand);
  setOperationAction(ISD::ROTR, MVT::i8, Expand);
  setOperationAction(ISD::BSWAP, MVT::i16, Expand);
  setOperationAction(ISD::BSWAP, MVT::i32, Expand);
  setOperationAction(ISD::BSWAP, MVT::i64, Expand);

  // Indirect branch is not supported.
  // This also disables Jump Table creation.
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);
  setOperationAction(ISD::BRIND, MVT::Other, Expand);

  setOperationAction(ISD::GlobalAddress, MVT::i32, Custom);
  setOperationAction(ISD::GlobalAddress, MVT::i64, Custom);

  // We want to legalize constant related memmove and memcopy
  // intrinsics.
  setOperationAction(ISD::INTRINSIC_W_CHAIN, MVT::Other, Custom);

  // Turn FP extload into load/fpextend
  setLoadExtAction(ISD::EXTLOAD, MVT::f32, MVT::f16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::f64, MVT::f16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::f64, MVT::f32, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v2f32, MVT::v2f16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v2f64, MVT::v2f16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v2f64, MVT::v2f32, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v4f32, MVT::v4f16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v4f64, MVT::v4f16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v4f64, MVT::v4f32, Expand);
  // Turn FP truncstore into trunc + store.
  // FIXME: vector types should also be expanded
  setTruncStoreAction(MVT::f32, MVT::f16, Expand);
  setTruncStoreAction(MVT::f64, MVT::f16, Expand);
  setTruncStoreAction(MVT::f64, MVT::f32, Expand);

  // PTX does not support load / store predicate registers
  setOperationAction(ISD::LOAD, MVT::i1, Custom);
  setOperationAction(ISD::STORE, MVT::i1, Custom);

  for (MVT VT : MVT::integer_valuetypes()) {
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::i1, Promote);
    setTruncStoreAction(VT, MVT::i1, Expand);
  }

  // This is legal in NVPTX
  setOperationAction(ISD::ConstantFP, MVT::f64, Legal);
  setOperationAction(ISD::ConstantFP, MVT::f32, Legal);
  setOperationAction(ISD::ConstantFP, MVT::f16, Legal);

  // TRAP can be lowered to PTX trap
  setOperationAction(ISD::TRAP, MVT::Other, Legal);

  // Register custom handling for vector loads/stores
  for (MVT VT : MVT::vector_valuetypes()) {
    if (IsPTXVectorType(VT)) {
      setOperationAction(ISD::LOAD, VT, Custom);
      setOperationAction(ISD::STORE, VT, Custom);
      setOperationAction(ISD::INTRINSIC_W_CHAIN, VT, Custom);
    }
  }

  // Custom handling for i8 intrinsics
  setOperationAction(ISD::INTRINSIC_W_CHAIN, MVT::i8, Custom);

  for (const auto& Ty : {MVT::i16, MVT::i32, MVT::i64}) {
    setOperationAction(ISD::ABS,  Ty, Legal);
    setOperationAction(ISD::SMIN, Ty, Legal);
    setOperationAction(ISD::SMAX, Ty, Legal);
    setOperationAction(ISD::UMIN, Ty, Legal);
    setOperationAction(ISD::UMAX, Ty, Legal);

    setOperationAction(ISD::CTPOP, Ty, Legal);
    setOperationAction(ISD::CTLZ, Ty, Legal);
  }

  setOperationAction(ISD::CTTZ, MVT::i16, Expand);
  setOperationAction(ISD::CTTZ, MVT::i32, Expand);
  setOperationAction(ISD::CTTZ, MVT::i64, Expand);

  // PTX does not directly support SELP of i1, so promote to i32 first
  setOperationAction(ISD::SELECT, MVT::i1, Custom);

  // PTX cannot multiply two i64s in a single instruction.
  setOperationAction(ISD::SMUL_LOHI, MVT::i64, Expand);
  setOperationAction(ISD::UMUL_LOHI, MVT::i64, Expand);

  // We have some custom DAG combine patterns for these nodes
  setTargetDAGCombine(ISD::ADD);
  setTargetDAGCombine(ISD::AND);
  setTargetDAGCombine(ISD::FADD);
  setTargetDAGCombine(ISD::MUL);
  setTargetDAGCombine(ISD::SHL);
  setTargetDAGCombine(ISD::SREM);
  setTargetDAGCombine(ISD::UREM);

  // setcc for f16x2 needs special handling to prevent legalizer's
  // attempt to scalarize it due to v2i1 not being legal.
  if (STI.allowFP16Math())
    setTargetDAGCombine(ISD::SETCC);

  // Promote fp16 arithmetic if fp16 hardware isn't available or the
  // user passed --nvptx-no-fp16-math. The flag is useful because,
  // although sm_53+ GPUs have some sort of FP16 support in
  // hardware, only sm_53 and sm_60 have full implementation. Others
  // only have token amount of hardware and are likely to run faster
  // by using fp32 units instead.
  for (const auto &Op : {ISD::FADD, ISD::FMUL, ISD::FSUB, ISD::FMA}) {
    setFP16OperationAction(Op, MVT::f16, Legal, Promote);
    setFP16OperationAction(Op, MVT::v2f16, Legal, Expand);
  }

  // There's no neg.f16 instruction. Expand to (0-x).
  setOperationAction(ISD::FNEG, MVT::f16, Expand);
  setOperationAction(ISD::FNEG, MVT::v2f16, Expand);

  // (would be) Library functions.

  // These map to conversion instructions for scalar FP types.
  for (const auto &Op : {ISD::FCEIL, ISD::FFLOOR, ISD::FNEARBYINT, ISD::FRINT,
                         ISD::FROUND, ISD::FTRUNC}) {
    setOperationAction(Op, MVT::f16, Legal);
    setOperationAction(Op, MVT::f32, Legal);
    setOperationAction(Op, MVT::f64, Legal);
    setOperationAction(Op, MVT::v2f16, Expand);
  }

  // 'Expand' implements FCOPYSIGN without calling an external library.
  setOperationAction(ISD::FCOPYSIGN, MVT::f16, Expand);
  setOperationAction(ISD::FCOPYSIGN, MVT::v2f16, Expand);
  setOperationAction(ISD::FCOPYSIGN, MVT::f32, Expand);
  setOperationAction(ISD::FCOPYSIGN, MVT::f64, Expand);

  // These map to corresponding instructions for f32/f64. f16 must be
  // promoted to f32. v2f16 is expanded to f16, which is then promoted
  // to f32.
  for (const auto &Op : {ISD::FDIV, ISD::FREM, ISD::FSQRT, ISD::FSIN, ISD::FCOS,
                         ISD::FABS, ISD::FMINNUM, ISD::FMAXNUM}) {
    setOperationAction(Op, MVT::f16, Promote);
    setOperationAction(Op, MVT::f32, Legal);
    setOperationAction(Op, MVT::f64, Legal);
    setOperationAction(Op, MVT::v2f16, Expand);
  }
  setOperationAction(ISD::FMINNUM, MVT::f16, Promote);
  setOperationAction(ISD::FMAXNUM, MVT::f16, Promote);
  setOperationAction(ISD::FMINIMUM, MVT::f16, Promote);
  setOperationAction(ISD::FMAXIMUM, MVT::f16, Promote);

  // No FEXP2, FLOG2.  The PTX ex2 and log2 functions are always approximate.
  // No FPOW or FREM in PTX.

  // Now deduce the information based on the above mentioned
  // actions
  computeRegisterProperties(STI.getRegisterInfo());
}

const char *NVPTXTargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch ((NVPTXISD::NodeType)Opcode) {
  case NVPTXISD::FIRST_NUMBER:
    break;
  case NVPTXISD::CALL:
    return "NVPTXISD::CALL";
  case NVPTXISD::RET_FLAG:
    return "NVPTXISD::RET_FLAG";
  case NVPTXISD::LOAD_PARAM:
    return "NVPTXISD::LOAD_PARAM";
  case NVPTXISD::Wrapper:
    return "NVPTXISD::Wrapper";
  case NVPTXISD::DeclareParam:
    return "NVPTXISD::DeclareParam";
  case NVPTXISD::DeclareScalarParam:
    return "NVPTXISD::DeclareScalarParam";
  case NVPTXISD::DeclareRet:
    return "NVPTXISD::DeclareRet";
  case NVPTXISD::DeclareScalarRet:
    return "NVPTXISD::DeclareScalarRet";
  case NVPTXISD::DeclareRetParam:
    return "NVPTXISD::DeclareRetParam";
  case NVPTXISD::PrintCall:
    return "NVPTXISD::PrintCall";
  case NVPTXISD::PrintConvergentCall:
    return "NVPTXISD::PrintConvergentCall";
  case NVPTXISD::PrintCallUni:
    return "NVPTXISD::PrintCallUni";
  case NVPTXISD::PrintConvergentCallUni:
    return "NVPTXISD::PrintConvergentCallUni";
  case NVPTXISD::LoadParam:
    return "NVPTXISD::LoadParam";
  case NVPTXISD::LoadParamV2:
    return "NVPTXISD::LoadParamV2";
  case NVPTXISD::LoadParamV4:
    return "NVPTXISD::LoadParamV4";
  case NVPTXISD::StoreParam:
    return "NVPTXISD::StoreParam";
  case NVPTXISD::StoreParamV2:
    return "NVPTXISD::StoreParamV2";
  case NVPTXISD::StoreParamV4:
    return "NVPTXISD::StoreParamV4";
  case NVPTXISD::StoreParamS32:
    return "NVPTXISD::StoreParamS32";
  case NVPTXISD::StoreParamU32:
    return "NVPTXISD::StoreParamU32";
  case NVPTXISD::CallArgBegin:
    return "NVPTXISD::CallArgBegin";
  case NVPTXISD::CallArg:
    return "NVPTXISD::CallArg";
  case NVPTXISD::LastCallArg:
    return "NVPTXISD::LastCallArg";
  case NVPTXISD::CallArgEnd:
    return "NVPTXISD::CallArgEnd";
  case NVPTXISD::CallVoid:
    return "NVPTXISD::CallVoid";
  case NVPTXISD::CallVal:
    return "NVPTXISD::CallVal";
  case NVPTXISD::CallSymbol:
    return "NVPTXISD::CallSymbol";
  case NVPTXISD::Prototype:
    return "NVPTXISD::Prototype";
  case NVPTXISD::MoveParam:
    return "NVPTXISD::MoveParam";
  case NVPTXISD::StoreRetval:
    return "NVPTXISD::StoreRetval";
  case NVPTXISD::StoreRetvalV2:
    return "NVPTXISD::StoreRetvalV2";
  case NVPTXISD::StoreRetvalV4:
    return "NVPTXISD::StoreRetvalV4";
  case NVPTXISD::PseudoUseParam:
    return "NVPTXISD::PseudoUseParam";
  case NVPTXISD::RETURN:
    return "NVPTXISD::RETURN";
  case NVPTXISD::CallSeqBegin:
    return "NVPTXISD::CallSeqBegin";
  case NVPTXISD::CallSeqEnd:
    return "NVPTXISD::CallSeqEnd";
  case NVPTXISD::CallPrototype:
    return "NVPTXISD::CallPrototype";
  case NVPTXISD::ProxyReg:
    return "NVPTXISD::ProxyReg";
  case NVPTXISD::LoadV2:
    return "NVPTXISD::LoadV2";
  case NVPTXISD::LoadV4:
    return "NVPTXISD::LoadV4";
  case NVPTXISD::LDGV2:
    return "NVPTXISD::LDGV2";
  case NVPTXISD::LDGV4:
    return "NVPTXISD::LDGV4";
  case NVPTXISD::LDUV2:
    return "NVPTXISD::LDUV2";
  case NVPTXISD::LDUV4:
    return "NVPTXISD::LDUV4";
  case NVPTXISD::StoreV2:
    return "NVPTXISD::StoreV2";
  case NVPTXISD::StoreV4:
    return "NVPTXISD::StoreV4";
  case NVPTXISD::FUN_SHFL_CLAMP:
    return "NVPTXISD::FUN_SHFL_CLAMP";
  case NVPTXISD::FUN_SHFR_CLAMP:
    return "NVPTXISD::FUN_SHFR_CLAMP";
  case NVPTXISD::IMAD:
    return "NVPTXISD::IMAD";
  case NVPTXISD::SETP_F16X2:
    return "NVPTXISD::SETP_F16X2";
  case NVPTXISD::Dummy:
    return "NVPTXISD::Dummy";
  case NVPTXISD::MUL_WIDE_SIGNED:
    return "NVPTXISD::MUL_WIDE_SIGNED";
  case NVPTXISD::MUL_WIDE_UNSIGNED:
    return "NVPTXISD::MUL_WIDE_UNSIGNED";
  case NVPTXISD::Tex1DFloatS32:        return "NVPTXISD::Tex1DFloatS32";
  case NVPTXISD::Tex1DFloatFloat:      return "NVPTXISD::Tex1DFloatFloat";
  case NVPTXISD::Tex1DFloatFloatLevel:
    return "NVPTXISD::Tex1DFloatFloatLevel";
  case NVPTXISD::Tex1DFloatFloatGrad:
    return "NVPTXISD::Tex1DFloatFloatGrad";
  case NVPTXISD::Tex1DS32S32:          return "NVPTXISD::Tex1DS32S32";
  case NVPTXISD::Tex1DS32Float:        return "NVPTXISD::Tex1DS32Float";
  case NVPTXISD::Tex1DS32FloatLevel:
    return "NVPTXISD::Tex1DS32FloatLevel";
  case NVPTXISD::Tex1DS32FloatGrad:
    return "NVPTXISD::Tex1DS32FloatGrad";
  case NVPTXISD::Tex1DU32S32:          return "NVPTXISD::Tex1DU32S32";
  case NVPTXISD::Tex1DU32Float:        return "NVPTXISD::Tex1DU32Float";
  case NVPTXISD::Tex1DU32FloatLevel:
    return "NVPTXISD::Tex1DU32FloatLevel";
  case NVPTXISD::Tex1DU32FloatGrad:
    return "NVPTXISD::Tex1DU32FloatGrad";
  case NVPTXISD::Tex1DArrayFloatS32:   return "NVPTXISD::Tex1DArrayFloatS32";
  case NVPTXISD::Tex1DArrayFloatFloat: return "NVPTXISD::Tex1DArrayFloatFloat";
  case NVPTXISD::Tex1DArrayFloatFloatLevel:
    return "NVPTXISD::Tex1DArrayFloatFloatLevel";
  case NVPTXISD::Tex1DArrayFloatFloatGrad:
    return "NVPTXISD::Tex1DArrayFloatFloatGrad";
  case NVPTXISD::Tex1DArrayS32S32:     return "NVPTXISD::Tex1DArrayS32S32";
  case NVPTXISD::Tex1DArrayS32Float:   return "NVPTXISD::Tex1DArrayS32Float";
  case NVPTXISD::Tex1DArrayS32FloatLevel:
    return "NVPTXISD::Tex1DArrayS32FloatLevel";
  case NVPTXISD::Tex1DArrayS32FloatGrad:
    return "NVPTXISD::Tex1DArrayS32FloatGrad";
  case NVPTXISD::Tex1DArrayU32S32:     return "NVPTXISD::Tex1DArrayU32S32";
  case NVPTXISD::Tex1DArrayU32Float:   return "NVPTXISD::Tex1DArrayU32Float";
  case NVPTXISD::Tex1DArrayU32FloatLevel:
    return "NVPTXISD::Tex1DArrayU32FloatLevel";
  case NVPTXISD::Tex1DArrayU32FloatGrad:
    return "NVPTXISD::Tex1DArrayU32FloatGrad";
  case NVPTXISD::Tex2DFloatS32:        return "NVPTXISD::Tex2DFloatS32";
  case NVPTXISD::Tex2DFloatFloat:      return "NVPTXISD::Tex2DFloatFloat";
  case NVPTXISD::Tex2DFloatFloatLevel:
    return "NVPTXISD::Tex2DFloatFloatLevel";
  case NVPTXISD::Tex2DFloatFloatGrad:
    return "NVPTXISD::Tex2DFloatFloatGrad";
  case NVPTXISD::Tex2DS32S32:          return "NVPTXISD::Tex2DS32S32";
  case NVPTXISD::Tex2DS32Float:        return "NVPTXISD::Tex2DS32Float";
  case NVPTXISD::Tex2DS32FloatLevel:
    return "NVPTXISD::Tex2DS32FloatLevel";
  case NVPTXISD::Tex2DS32FloatGrad:
    return "NVPTXISD::Tex2DS32FloatGrad";
  case NVPTXISD::Tex2DU32S32:          return "NVPTXISD::Tex2DU32S32";
  case NVPTXISD::Tex2DU32Float:        return "NVPTXISD::Tex2DU32Float";
  case NVPTXISD::Tex2DU32FloatLevel:
    return "NVPTXISD::Tex2DU32FloatLevel";
  case NVPTXISD::Tex2DU32FloatGrad:
    return "NVPTXISD::Tex2DU32FloatGrad";
  case NVPTXISD::Tex2DArrayFloatS32:   return "NVPTXISD::Tex2DArrayFloatS32";
  case NVPTXISD::Tex2DArrayFloatFloat: return "NVPTXISD::Tex2DArrayFloatFloat";
  case NVPTXISD::Tex2DArrayFloatFloatLevel:
    return "NVPTXISD::Tex2DArrayFloatFloatLevel";
  case NVPTXISD::Tex2DArrayFloatFloatGrad:
    return "NVPTXISD::Tex2DArrayFloatFloatGrad";
  case NVPTXISD::Tex2DArrayS32S32:     return "NVPTXISD::Tex2DArrayS32S32";
  case NVPTXISD::Tex2DArrayS32Float:   return "NVPTXISD::Tex2DArrayS32Float";
  case NVPTXISD::Tex2DArrayS32FloatLevel:
    return "NVPTXISD::Tex2DArrayS32FloatLevel";
  case NVPTXISD::Tex2DArrayS32FloatGrad:
    return "NVPTXISD::Tex2DArrayS32FloatGrad";
  case NVPTXISD::Tex2DArrayU32S32:     return "NVPTXISD::Tex2DArrayU32S32";
  case NVPTXISD::Tex2DArrayU32Float:   return "NVPTXISD::Tex2DArrayU32Float";
  case NVPTXISD::Tex2DArrayU32FloatLevel:
    return "NVPTXISD::Tex2DArrayU32FloatLevel";
  case NVPTXISD::Tex2DArrayU32FloatGrad:
    return "NVPTXISD::Tex2DArrayU32FloatGrad";
  case NVPTXISD::Tex3DFloatS32:        return "NVPTXISD::Tex3DFloatS32";
  case NVPTXISD::Tex3DFloatFloat:      return "NVPTXISD::Tex3DFloatFloat";
  case NVPTXISD::Tex3DFloatFloatLevel:
    return "NVPTXISD::Tex3DFloatFloatLevel";
  case NVPTXISD::Tex3DFloatFloatGrad:
    return "NVPTXISD::Tex3DFloatFloatGrad";
  case NVPTXISD::Tex3DS32S32:          return "NVPTXISD::Tex3DS32S32";
  case NVPTXISD::Tex3DS32Float:        return "NVPTXISD::Tex3DS32Float";
  case NVPTXISD::Tex3DS32FloatLevel:
    return "NVPTXISD::Tex3DS32FloatLevel";
  case NVPTXISD::Tex3DS32FloatGrad:
    return "NVPTXISD::Tex3DS32FloatGrad";
  case NVPTXISD::Tex3DU32S32:          return "NVPTXISD::Tex3DU32S32";
  case NVPTXISD::Tex3DU32Float:        return "NVPTXISD::Tex3DU32Float";
  case NVPTXISD::Tex3DU32FloatLevel:
    return "NVPTXISD::Tex3DU32FloatLevel";
  case NVPTXISD::Tex3DU32FloatGrad:
    return "NVPTXISD::Tex3DU32FloatGrad";
  case NVPTXISD::TexCubeFloatFloat:      return "NVPTXISD::TexCubeFloatFloat";
  case NVPTXISD::TexCubeFloatFloatLevel:
    return "NVPTXISD::TexCubeFloatFloatLevel";
  case NVPTXISD::TexCubeS32Float:        return "NVPTXISD::TexCubeS32Float";
  case NVPTXISD::TexCubeS32FloatLevel:
    return "NVPTXISD::TexCubeS32FloatLevel";
  case NVPTXISD::TexCubeU32Float:        return "NVPTXISD::TexCubeU32Float";
  case NVPTXISD::TexCubeU32FloatLevel:
    return "NVPTXISD::TexCubeU32FloatLevel";
  case NVPTXISD::TexCubeArrayFloatFloat:
    return "NVPTXISD::TexCubeArrayFloatFloat";
  case NVPTXISD::TexCubeArrayFloatFloatLevel:
    return "NVPTXISD::TexCubeArrayFloatFloatLevel";
  case NVPTXISD::TexCubeArrayS32Float:
    return "NVPTXISD::TexCubeArrayS32Float";
  case NVPTXISD::TexCubeArrayS32FloatLevel:
    return "NVPTXISD::TexCubeArrayS32FloatLevel";
  case NVPTXISD::TexCubeArrayU32Float:
    return "NVPTXISD::TexCubeArrayU32Float";
  case NVPTXISD::TexCubeArrayU32FloatLevel:
    return "NVPTXISD::TexCubeArrayU32FloatLevel";
  case NVPTXISD::Tld4R2DFloatFloat:
    return "NVPTXISD::Tld4R2DFloatFloat";
  case NVPTXISD::Tld4G2DFloatFloat:
    return "NVPTXISD::Tld4G2DFloatFloat";
  case NVPTXISD::Tld4B2DFloatFloat:
    return "NVPTXISD::Tld4B2DFloatFloat";
  case NVPTXISD::Tld4A2DFloatFloat:
    return "NVPTXISD::Tld4A2DFloatFloat";
  case NVPTXISD::Tld4R2DS64Float:
    return "NVPTXISD::Tld4R2DS64Float";
  case NVPTXISD::Tld4G2DS64Float:
    return "NVPTXISD::Tld4G2DS64Float";
  case NVPTXISD::Tld4B2DS64Float:
    return "NVPTXISD::Tld4B2DS64Float";
  case NVPTXISD::Tld4A2DS64Float:
    return "NVPTXISD::Tld4A2DS64Float";
  case NVPTXISD::Tld4R2DU64Float:
    return "NVPTXISD::Tld4R2DU64Float";
  case NVPTXISD::Tld4G2DU64Float:
    return "NVPTXISD::Tld4G2DU64Float";
  case NVPTXISD::Tld4B2DU64Float:
    return "NVPTXISD::Tld4B2DU64Float";
  case NVPTXISD::Tld4A2DU64Float:
    return "NVPTXISD::Tld4A2DU64Float";

  case NVPTXISD::TexUnified1DFloatS32:
    return "NVPTXISD::TexUnified1DFloatS32";
  case NVPTXISD::TexUnified1DFloatFloat:
    return "NVPTXISD::TexUnified1DFloatFloat";
  case NVPTXISD::TexUnified1DFloatFloatLevel:
    return "NVPTXISD::TexUnified1DFloatFloatLevel";
  case NVPTXISD::TexUnified1DFloatFloatGrad:
    return "NVPTXISD::TexUnified1DFloatFloatGrad";
  case NVPTXISD::TexUnified1DS32S32:
    return "NVPTXISD::TexUnified1DS32S32";
  case NVPTXISD::TexUnified1DS32Float:
    return "NVPTXISD::TexUnified1DS32Float";
  case NVPTXISD::TexUnified1DS32FloatLevel:
    return "NVPTXISD::TexUnified1DS32FloatLevel";
  case NVPTXISD::TexUnified1DS32FloatGrad:
    return "NVPTXISD::TexUnified1DS32FloatGrad";
  case NVPTXISD::TexUnified1DU32S32:
    return "NVPTXISD::TexUnified1DU32S32";
  case NVPTXISD::TexUnified1DU32Float:
    return "NVPTXISD::TexUnified1DU32Float";
  case NVPTXISD::TexUnified1DU32FloatLevel:
    return "NVPTXISD::TexUnified1DU32FloatLevel";
  case NVPTXISD::TexUnified1DU32FloatGrad:
    return "NVPTXISD::TexUnified1DU32FloatGrad";
  case NVPTXISD::TexUnified1DArrayFloatS32:
    return "NVPTXISD::TexUnified1DArrayFloatS32";
  case NVPTXISD::TexUnified1DArrayFloatFloat:
    return "NVPTXISD::TexUnified1DArrayFloatFloat";
  case NVPTXISD::TexUnified1DArrayFloatFloatLevel:
    return "NVPTXISD::TexUnified1DArrayFloatFloatLevel";
  case NVPTXISD::TexUnified1DArrayFloatFloatGrad:
    return "NVPTXISD::TexUnified1DArrayFloatFloatGrad";
  case NVPTXISD::TexUnified1DArrayS32S32:
    return "NVPTXISD::TexUnified1DArrayS32S32";
  case NVPTXISD::TexUnified1DArrayS32Float:
    return "NVPTXISD::TexUnified1DArrayS32Float";
  case NVPTXISD::TexUnified1DArrayS32FloatLevel:
    return "NVPTXISD::TexUnified1DArrayS32FloatLevel";
  case NVPTXISD::TexUnified1DArrayS32FloatGrad:
    return "NVPTXISD::TexUnified1DArrayS32FloatGrad";
  case NVPTXISD::TexUnified1DArrayU32S32:
    return "NVPTXISD::TexUnified1DArrayU32S32";
  case NVPTXISD::TexUnified1DArrayU32Float:
    return "NVPTXISD::TexUnified1DArrayU32Float";
  case NVPTXISD::TexUnified1DArrayU32FloatLevel:
    return "NVPTXISD::TexUnified1DArrayU32FloatLevel";
  case NVPTXISD::TexUnified1DArrayU32FloatGrad:
    return "NVPTXISD::TexUnified1DArrayU32FloatGrad";
  case NVPTXISD::TexUnified2DFloatS32:
    return "NVPTXISD::TexUnified2DFloatS32";
  case NVPTXISD::TexUnified2DFloatFloat:
    return "NVPTXISD::TexUnified2DFloatFloat";
  case NVPTXISD::TexUnified2DFloatFloatLevel:
    return "NVPTXISD::TexUnified2DFloatFloatLevel";
  case NVPTXISD::TexUnified2DFloatFloatGrad:
    return "NVPTXISD::TexUnified2DFloatFloatGrad";
  case NVPTXISD::TexUnified2DS32S32:
    return "NVPTXISD::TexUnified2DS32S32";
  case NVPTXISD::TexUnified2DS32Float:
    return "NVPTXISD::TexUnified2DS32Float";
  case NVPTXISD::TexUnified2DS32FloatLevel:
    return "NVPTXISD::TexUnified2DS32FloatLevel";
  case NVPTXISD::TexUnified2DS32FloatGrad:
    return "NVPTXISD::TexUnified2DS32FloatGrad";
  case NVPTXISD::TexUnified2DU32S32:
    return "NVPTXISD::TexUnified2DU32S32";
  case NVPTXISD::TexUnified2DU32Float:
    return "NVPTXISD::TexUnified2DU32Float";
  case NVPTXISD::TexUnified2DU32FloatLevel:
    return "NVPTXISD::TexUnified2DU32FloatLevel";
  case NVPTXISD::TexUnified2DU32FloatGrad:
    return "NVPTXISD::TexUnified2DU32FloatGrad";
  case NVPTXISD::TexUnified2DArrayFloatS32:
    return "NVPTXISD::TexUnified2DArrayFloatS32";
  case NVPTXISD::TexUnified2DArrayFloatFloat:
    return "NVPTXISD::TexUnified2DArrayFloatFloat";
  case NVPTXISD::TexUnified2DArrayFloatFloatLevel:
    return "NVPTXISD::TexUnified2DArrayFloatFloatLevel";
  case NVPTXISD::TexUnified2DArrayFloatFloatGrad:
    return "NVPTXISD::TexUnified2DArrayFloatFloatGrad";
  case NVPTXISD::TexUnified2DArrayS32S32:
    return "NVPTXISD::TexUnified2DArrayS32S32";
  case NVPTXISD::TexUnified2DArrayS32Float:
    return "NVPTXISD::TexUnified2DArrayS32Float";
  case NVPTXISD::TexUnified2DArrayS32FloatLevel:
    return "NVPTXISD::TexUnified2DArrayS32FloatLevel";
  case NVPTXISD::TexUnified2DArrayS32FloatGrad:
    return "NVPTXISD::TexUnified2DArrayS32FloatGrad";
  case NVPTXISD::TexUnified2DArrayU32S32:
    return "NVPTXISD::TexUnified2DArrayU32S32";
  case NVPTXISD::TexUnified2DArrayU32Float:
    return "NVPTXISD::TexUnified2DArrayU32Float";
  case NVPTXISD::TexUnified2DArrayU32FloatLevel:
    return "NVPTXISD::TexUnified2DArrayU32FloatLevel";
  case NVPTXISD::TexUnified2DArrayU32FloatGrad:
    return "NVPTXISD::TexUnified2DArrayU32FloatGrad";
  case NVPTXISD::TexUnified3DFloatS32:
    return "NVPTXISD::TexUnified3DFloatS32";
  case NVPTXISD::TexUnified3DFloatFloat:
    return "NVPTXISD::TexUnified3DFloatFloat";
  case NVPTXISD::TexUnified3DFloatFloatLevel:
    return "NVPTXISD::TexUnified3DFloatFloatLevel";
  case NVPTXISD::TexUnified3DFloatFloatGrad:
    return "NVPTXISD::TexUnified3DFloatFloatGrad";
  case NVPTXISD::TexUnified3DS32S32:
    return "NVPTXISD::TexUnified3DS32S32";
  case NVPTXISD::TexUnified3DS32Float:
    return "NVPTXISD::TexUnified3DS32Float";
  case NVPTXISD::TexUnified3DS32FloatLevel:
    return "NVPTXISD::TexUnified3DS32FloatLevel";
  case NVPTXISD::TexUnified3DS32FloatGrad:
    return "NVPTXISD::TexUnified3DS32FloatGrad";
  case NVPTXISD::TexUnified3DU32S32:
    return "NVPTXISD::TexUnified3DU32S32";
  case NVPTXISD::TexUnified3DU32Float:
    return "NVPTXISD::TexUnified3DU32Float";
  case NVPTXISD::TexUnified3DU32FloatLevel:
    return "NVPTXISD::TexUnified3DU32FloatLevel";
  case NVPTXISD::TexUnified3DU32FloatGrad:
    return "NVPTXISD::TexUnified3DU32FloatGrad";
  case NVPTXISD::TexUnifiedCubeFloatFloat:
    return "NVPTXISD::TexUnifiedCubeFloatFloat";
  case NVPTXISD::TexUnifiedCubeFloatFloatLevel:
    return "NVPTXISD::TexUnifiedCubeFloatFloatLevel";
  case NVPTXISD::TexUnifiedCubeS32Float:
    return "NVPTXISD::TexUnifiedCubeS32Float";
  case NVPTXISD::TexUnifiedCubeS32FloatLevel:
    return "NVPTXISD::TexUnifiedCubeS32FloatLevel";
  case NVPTXISD::TexUnifiedCubeU32Float:
    return "NVPTXISD::TexUnifiedCubeU32Float";
  case NVPTXISD::TexUnifiedCubeU32FloatLevel:
    return "NVPTXISD::TexUnifiedCubeU32FloatLevel";
  case NVPTXISD::TexUnifiedCubeArrayFloatFloat:
    return "NVPTXISD::TexUnifiedCubeArrayFloatFloat";
  case NVPTXISD::TexUnifiedCubeArrayFloatFloatLevel:
    return "NVPTXISD::TexUnifiedCubeArrayFloatFloatLevel";
  case NVPTXISD::TexUnifiedCubeArrayS32Float:
    return "NVPTXISD::TexUnifiedCubeArrayS32Float";
  case NVPTXISD::TexUnifiedCubeArrayS32FloatLevel:
    return "NVPTXISD::TexUnifiedCubeArrayS32FloatLevel";
  case NVPTXISD::TexUnifiedCubeArrayU32Float:
    return "NVPTXISD::TexUnifiedCubeArrayU32Float";
  case NVPTXISD::TexUnifiedCubeArrayU32FloatLevel:
    return "NVPTXISD::TexUnifiedCubeArrayU32FloatLevel";
  case NVPTXISD::Tld4UnifiedR2DFloatFloat:
    return "NVPTXISD::Tld4UnifiedR2DFloatFloat";
  case NVPTXISD::Tld4UnifiedG2DFloatFloat:
    return "NVPTXISD::Tld4UnifiedG2DFloatFloat";
  case NVPTXISD::Tld4UnifiedB2DFloatFloat:
    return "NVPTXISD::Tld4UnifiedB2DFloatFloat";
  case NVPTXISD::Tld4UnifiedA2DFloatFloat:
    return "NVPTXISD::Tld4UnifiedA2DFloatFloat";
  case NVPTXISD::Tld4UnifiedR2DS64Float:
    return "NVPTXISD::Tld4UnifiedR2DS64Float";
  case NVPTXISD::Tld4UnifiedG2DS64Float:
    return "NVPTXISD::Tld4UnifiedG2DS64Float";
  case NVPTXISD::Tld4UnifiedB2DS64Float:
    return "NVPTXISD::Tld4UnifiedB2DS64Float";
  case NVPTXISD::Tld4UnifiedA2DS64Float:
    return "NVPTXISD::Tld4UnifiedA2DS64Float";
  case NVPTXISD::Tld4UnifiedR2DU64Float:
    return "NVPTXISD::Tld4UnifiedR2DU64Float";
  case NVPTXISD::Tld4UnifiedG2DU64Float:
    return "NVPTXISD::Tld4UnifiedG2DU64Float";
  case NVPTXISD::Tld4UnifiedB2DU64Float:
    return "NVPTXISD::Tld4UnifiedB2DU64Float";
  case NVPTXISD::Tld4UnifiedA2DU64Float:
    return "NVPTXISD::Tld4UnifiedA2DU64Float";

  case NVPTXISD::Suld1DI8Clamp:          return "NVPTXISD::Suld1DI8Clamp";
  case NVPTXISD::Suld1DI16Clamp:         return "NVPTXISD::Suld1DI16Clamp";
  case NVPTXISD::Suld1DI32Clamp:         return "NVPTXISD::Suld1DI32Clamp";
  case NVPTXISD::Suld1DI64Clamp:         return "NVPTXISD::Suld1DI64Clamp";
  case NVPTXISD::Suld1DV2I8Clamp:        return "NVPTXISD::Suld1DV2I8Clamp";
  case NVPTXISD::Suld1DV2I16Clamp:       return "NVPTXISD::Suld1DV2I16Clamp";
  case NVPTXISD::Suld1DV2I32Clamp:       return "NVPTXISD::Suld1DV2I32Clamp";
  case NVPTXISD::Suld1DV2I64Clamp:       return "NVPTXISD::Suld1DV2I64Clamp";
  case NVPTXISD::Suld1DV4I8Clamp:        return "NVPTXISD::Suld1DV4I8Clamp";
  case NVPTXISD::Suld1DV4I16Clamp:       return "NVPTXISD::Suld1DV4I16Clamp";
  case NVPTXISD::Suld1DV4I32Clamp:       return "NVPTXISD::Suld1DV4I32Clamp";

  case NVPTXISD::Suld1DArrayI8Clamp:   return "NVPTXISD::Suld1DArrayI8Clamp";
  case NVPTXISD::Suld1DArrayI16Clamp:  return "NVPTXISD::Suld1DArrayI16Clamp";
  case NVPTXISD::Suld1DArrayI32Clamp:  return "NVPTXISD::Suld1DArrayI32Clamp";
  case NVPTXISD::Suld1DArrayI64Clamp:  return "NVPTXISD::Suld1DArrayI64Clamp";
  case NVPTXISD::Suld1DArrayV2I8Clamp: return "NVPTXISD::Suld1DArrayV2I8Clamp";
  case NVPTXISD::Suld1DArrayV2I16Clamp:return "NVPTXISD::Suld1DArrayV2I16Clamp";
  case NVPTXISD::Suld1DArrayV2I32Clamp:return "NVPTXISD::Suld1DArrayV2I32Clamp";
  case NVPTXISD::Suld1DArrayV2I64Clamp:return "NVPTXISD::Suld1DArrayV2I64Clamp";
  case NVPTXISD::Suld1DArrayV4I8Clamp: return "NVPTXISD::Suld1DArrayV4I8Clamp";
  case NVPTXISD::Suld1DArrayV4I16Clamp:return "NVPTXISD::Suld1DArrayV4I16Clamp";
  case NVPTXISD::Suld1DArrayV4I32Clamp:return "NVPTXISD::Suld1DArrayV4I32Clamp";

  case NVPTXISD::Suld2DI8Clamp:          return "NVPTXISD::Suld2DI8Clamp";
  case NVPTXISD::Suld2DI16Clamp:         return "NVPTXISD::Suld2DI16Clamp";
  case NVPTXISD::Suld2DI32Clamp:         return "NVPTXISD::Suld2DI32Clamp";
  case NVPTXISD::Suld2DI64Clamp:         return "NVPTXISD::Suld2DI64Clamp";
  case NVPTXISD::Suld2DV2I8Clamp:        return "NVPTXISD::Suld2DV2I8Clamp";
  case NVPTXISD::Suld2DV2I16Clamp:       return "NVPTXISD::Suld2DV2I16Clamp";
  case NVPTXISD::Suld2DV2I32Clamp:       return "NVPTXISD::Suld2DV2I32Clamp";
  case NVPTXISD::Suld2DV2I64Clamp:       return "NVPTXISD::Suld2DV2I64Clamp";
  case NVPTXISD::Suld2DV4I8Clamp:        return "NVPTXISD::Suld2DV4I8Clamp";
  case NVPTXISD::Suld2DV4I16Clamp:       return "NVPTXISD::Suld2DV4I16Clamp";
  case NVPTXISD::Suld2DV4I32Clamp:       return "NVPTXISD::Suld2DV4I32Clamp";

  case NVPTXISD::Suld2DArrayI8Clamp:   return "NVPTXISD::Suld2DArrayI8Clamp";
  case NVPTXISD::Suld2DArrayI16Clamp:  return "NVPTXISD::Suld2DArrayI16Clamp";
  case NVPTXISD::Suld2DArrayI32Clamp:  return "NVPTXISD::Suld2DArrayI32Clamp";
  case NVPTXISD::Suld2DArrayI64Clamp:  return "NVPTXISD::Suld2DArrayI64Clamp";
  case NVPTXISD::Suld2DArrayV2I8Clamp: return "NVPTXISD::Suld2DArrayV2I8Clamp";
  case NVPTXISD::Suld2DArrayV2I16Clamp:return "NVPTXISD::Suld2DArrayV2I16Clamp";
  case NVPTXISD::Suld2DArrayV2I32Clamp:return "NVPTXISD::Suld2DArrayV2I32Clamp";
  case NVPTXISD::Suld2DArrayV2I64Clamp:return "NVPTXISD::Suld2DArrayV2I64Clamp";
  case NVPTXISD::Suld2DArrayV4I8Clamp: return "NVPTXISD::Suld2DArrayV4I8Clamp";
  case NVPTXISD::Suld2DArrayV4I16Clamp:return "NVPTXISD::Suld2DArrayV4I16Clamp";
  case NVPTXISD::Suld2DArrayV4I32Clamp:return "NVPTXISD::Suld2DArrayV4I32Clamp";

  case NVPTXISD::Suld3DI8Clamp:          return "NVPTXISD::Suld3DI8Clamp";
  case NVPTXISD::Suld3DI16Clamp:         return "NVPTXISD::Suld3DI16Clamp";
  case NVPTXISD::Suld3DI32Clamp:         return "NVPTXISD::Suld3DI32Clamp";
  case NVPTXISD::Suld3DI64Clamp:         return "NVPTXISD::Suld3DI64Clamp";
  case NVPTXISD::Suld3DV2I8Clamp:        return "NVPTXISD::Suld3DV2I8Clamp";
  case NVPTXISD::Suld3DV2I16Clamp:       return "NVPTXISD::Suld3DV2I16Clamp";
  case NVPTXISD::Suld3DV2I32Clamp:       return "NVPTXISD::Suld3DV2I32Clamp";
  case NVPTXISD::Suld3DV2I64Clamp:       return "NVPTXISD::Suld3DV2I64Clamp";
  case NVPTXISD::Suld3DV4I8Clamp:        return "NVPTXISD::Suld3DV4I8Clamp";
  case NVPTXISD::Suld3DV4I16Clamp:       return "NVPTXISD::Suld3DV4I16Clamp";
  case NVPTXISD::Suld3DV4I32Clamp:       return "NVPTXISD::Suld3DV4I32Clamp";

  case NVPTXISD::Suld1DI8Trap:          return "NVPTXISD::Suld1DI8Trap";
  case NVPTXISD::Suld1DI16Trap:         return "NVPTXISD::Suld1DI16Trap";
  case NVPTXISD::Suld1DI32Trap:         return "NVPTXISD::Suld1DI32Trap";
  case NVPTXISD::Suld1DI64Trap:         return "NVPTXISD::Suld1DI64Trap";
  case NVPTXISD::Suld1DV2I8Trap:        return "NVPTXISD::Suld1DV2I8Trap";
  case NVPTXISD::Suld1DV2I16Trap:       return "NVPTXISD::Suld1DV2I16Trap";
  case NVPTXISD::Suld1DV2I32Trap:       return "NVPTXISD::Suld1DV2I32Trap";
  case NVPTXISD::Suld1DV2I64Trap:       return "NVPTXISD::Suld1DV2I64Trap";
  case NVPTXISD::Suld1DV4I8Trap:        return "NVPTXISD::Suld1DV4I8Trap";
  case NVPTXISD::Suld1DV4I16Trap:       return "NVPTXISD::Suld1DV4I16Trap";
  case NVPTXISD::Suld1DV4I32Trap:       return "NVPTXISD::Suld1DV4I32Trap";

  case NVPTXISD::Suld1DArrayI8Trap:     return "NVPTXISD::Suld1DArrayI8Trap";
  case NVPTXISD::Suld1DArrayI16Trap:    return "NVPTXISD::Suld1DArrayI16Trap";
  case NVPTXISD::Suld1DArrayI32Trap:    return "NVPTXISD::Suld1DArrayI32Trap";
  case NVPTXISD::Suld1DArrayI64Trap:    return "NVPTXISD::Suld1DArrayI64Trap";
  case NVPTXISD::Suld1DArrayV2I8Trap:   return "NVPTXISD::Suld1DArrayV2I8Trap";
  case NVPTXISD::Suld1DArrayV2I16Trap:  return "NVPTXISD::Suld1DArrayV2I16Trap";
  case NVPTXISD::Suld1DArrayV2I32Trap:  return "NVPTXISD::Suld1DArrayV2I32Trap";
  case NVPTXISD::Suld1DArrayV2I64Trap:  return "NVPTXISD::Suld1DArrayV2I64Trap";
  case NVPTXISD::Suld1DArrayV4I8Trap:   return "NVPTXISD::Suld1DArrayV4I8Trap";
  case NVPTXISD::Suld1DArrayV4I16Trap:  return "NVPTXISD::Suld1DArrayV4I16Trap";
  case NVPTXISD::Suld1DArrayV4I32Trap:  return "NVPTXISD::Suld1DArrayV4I32Trap";

  case NVPTXISD::Suld2DI8Trap:          return "NVPTXISD::Suld2DI8Trap";
  case NVPTXISD::Suld2DI16Trap:         return "NVPTXISD::Suld2DI16Trap";
  case NVPTXISD::Suld2DI32Trap:         return "NVPTXISD::Suld2DI32Trap";
  case NVPTXISD::Suld2DI64Trap:         return "NVPTXISD::Suld2DI64Trap";
  case NVPTXISD::Suld2DV2I8Trap:        return "NVPTXISD::Suld2DV2I8Trap";
  case NVPTXISD::Suld2DV2I16Trap:       return "NVPTXISD::Suld2DV2I16Trap";
  case NVPTXISD::Suld2DV2I32Trap:       return "NVPTXISD::Suld2DV2I32Trap";
  case NVPTXISD::Suld2DV2I64Trap:       return "NVPTXISD::Suld2DV2I64Trap";
  case NVPTXISD::Suld2DV4I8Trap:        return "NVPTXISD::Suld2DV4I8Trap";
  case NVPTXISD::Suld2DV4I16Trap:       return "NVPTXISD::Suld2DV4I16Trap";
  case NVPTXISD::Suld2DV4I32Trap:       return "NVPTXISD::Suld2DV4I32Trap";

  case NVPTXISD::Suld2DArrayI8Trap:     return "NVPTXISD::Suld2DArrayI8Trap";
  case NVPTXISD::Suld2DArrayI16Trap:    return "NVPTXISD::Suld2DArrayI16Trap";
  case NVPTXISD::Suld2DArrayI32Trap:    return "NVPTXISD::Suld2DArrayI32Trap";
  case NVPTXISD::Suld2DArrayI64Trap:    return "NVPTXISD::Suld2DArrayI64Trap";
  case NVPTXISD::Suld2DArrayV2I8Trap:   return "NVPTXISD::Suld2DArrayV2I8Trap";
  case NVPTXISD::Suld2DArrayV2I16Trap:  return "NVPTXISD::Suld2DArrayV2I16Trap";
  case NVPTXISD::Suld2DArrayV2I32Trap:  return "NVPTXISD::Suld2DArrayV2I32Trap";
  case NVPTXISD::Suld2DArrayV2I64Trap:  return "NVPTXISD::Suld2DArrayV2I64Trap";
  case NVPTXISD::Suld2DArrayV4I8Trap:   return "NVPTXISD::Suld2DArrayV4I8Trap";
  case NVPTXISD::Suld2DArrayV4I16Trap:  return "NVPTXISD::Suld2DArrayV4I16Trap";
  case NVPTXISD::Suld2DArrayV4I32Trap:  return "NVPTXISD::Suld2DArrayV4I32Trap";

  case NVPTXISD::Suld3DI8Trap:          return "NVPTXISD::Suld3DI8Trap";
  case NVPTXISD::Suld3DI16Trap:         return "NVPTXISD::Suld3DI16Trap";
  case NVPTXISD::Suld3DI32Trap:         return "NVPTXISD::Suld3DI32Trap";
  case NVPTXISD::Suld3DI64Trap:         return "NVPTXISD::Suld3DI64Trap";
  case NVPTXISD::Suld3DV2I8Trap:        return "NVPTXISD::Suld3DV2I8Trap";
  case NVPTXISD::Suld3DV2I16Trap:       return "NVPTXISD::Suld3DV2I16Trap";
  case NVPTXISD::Suld3DV2I32Trap:       return "NVPTXISD::Suld3DV2I32Trap";
  case NVPTXISD::Suld3DV2I64Trap:       return "NVPTXISD::Suld3DV2I64Trap";
  case NVPTXISD::Suld3DV4I8Trap:        return "NVPTXISD::Suld3DV4I8Trap";
  case NVPTXISD::Suld3DV4I16Trap:       return "NVPTXISD::Suld3DV4I16Trap";
  case NVPTXISD::Suld3DV4I32Trap:       return "NVPTXISD::Suld3DV4I32Trap";

  case NVPTXISD::Suld1DI8Zero:          return "NVPTXISD::Suld1DI8Zero";
  case NVPTXISD::Suld1DI16Zero:         return "NVPTXISD::Suld1DI16Zero";
  case NVPTXISD::Suld1DI32Zero:         return "NVPTXISD::Suld1DI32Zero";
  case NVPTXISD::Suld1DI64Zero:         return "NVPTXISD::Suld1DI64Zero";
  case NVPTXISD::Suld1DV2I8Zero:        return "NVPTXISD::Suld1DV2I8Zero";
  case NVPTXISD::Suld1DV2I16Zero:       return "NVPTXISD::Suld1DV2I16Zero";
  case NVPTXISD::Suld1DV2I32Zero:       return "NVPTXISD::Suld1DV2I32Zero";
  case NVPTXISD::Suld1DV2I64Zero:       return "NVPTXISD::Suld1DV2I64Zero";
  case NVPTXISD::Suld1DV4I8Zero:        return "NVPTXISD::Suld1DV4I8Zero";
  case NVPTXISD::Suld1DV4I16Zero:       return "NVPTXISD::Suld1DV4I16Zero";
  case NVPTXISD::Suld1DV4I32Zero:       return "NVPTXISD::Suld1DV4I32Zero";

  case NVPTXISD::Suld1DArrayI8Zero:     return "NVPTXISD::Suld1DArrayI8Zero";
  case NVPTXISD::Suld1DArrayI16Zero:    return "NVPTXISD::Suld1DArrayI16Zero";
  case NVPTXISD::Suld1DArrayI32Zero:    return "NVPTXISD::Suld1DArrayI32Zero";
  case NVPTXISD::Suld1DArrayI64Zero:    return "NVPTXISD::Suld1DArrayI64Zero";
  case NVPTXISD::Suld1DArrayV2I8Zero:   return "NVPTXISD::Suld1DArrayV2I8Zero";
  case NVPTXISD::Suld1DArrayV2I16Zero:  return "NVPTXISD::Suld1DArrayV2I16Zero";
  case NVPTXISD::Suld1DArrayV2I32Zero:  return "NVPTXISD::Suld1DArrayV2I32Zero";
  case NVPTXISD::Suld1DArrayV2I64Zero:  return "NVPTXISD::Suld1DArrayV2I64Zero";
  case NVPTXISD::Suld1DArrayV4I8Zero:   return "NVPTXISD::Suld1DArrayV4I8Zero";
  case NVPTXISD::Suld1DArrayV4I16Zero:  return "NVPTXISD::Suld1DArrayV4I16Zero";
  case NVPTXISD::Suld1DArrayV4I32Zero:  return "NVPTXISD::Suld1DArrayV4I32Zero";

  case NVPTXISD::Suld2DI8Zero:          return "NVPTXISD::Suld2DI8Zero";
  case NVPTXISD::Suld2DI16Zero:         return "NVPTXISD::Suld2DI16Zero";
  case NVPTXISD::Suld2DI32Zero:         return "NVPTXISD::Suld2DI32Zero";
  case NVPTXISD::Suld2DI64Zero:         return "NVPTXISD::Suld2DI64Zero";
  case NVPTXISD::Suld2DV2I8Zero:        return "NVPTXISD::Suld2DV2I8Zero";
  case NVPTXISD::Suld2DV2I16Zero:       return "NVPTXISD::Suld2DV2I16Zero";
  case NVPTXISD::Suld2DV2I32Zero:       return "NVPTXISD::Suld2DV2I32Zero";
  case NVPTXISD::Suld2DV2I64Zero:       return "NVPTXISD::Suld2DV2I64Zero";
  case NVPTXISD::Suld2DV4I8Zero:        return "NVPTXISD::Suld2DV4I8Zero";
  case NVPTXISD::Suld2DV4I16Zero:       return "NVPTXISD::Suld2DV4I16Zero";
  case NVPTXISD::Suld2DV4I32Zero:       return "NVPTXISD::Suld2DV4I32Zero";

  case NVPTXISD::Suld2DArrayI8Zero:     return "NVPTXISD::Suld2DArrayI8Zero";
  case NVPTXISD::Suld2DArrayI16Zero:    return "NVPTXISD::Suld2DArrayI16Zero";
  case NVPTXISD::Suld2DArrayI32Zero:    return "NVPTXISD::Suld2DArrayI32Zero";
  case NVPTXISD::Suld2DArrayI64Zero:    return "NVPTXISD::Suld2DArrayI64Zero";
  case NVPTXISD::Suld2DArrayV2I8Zero:   return "NVPTXISD::Suld2DArrayV2I8Zero";
  case NVPTXISD::Suld2DArrayV2I16Zero:  return "NVPTXISD::Suld2DArrayV2I16Zero";
  case NVPTXISD::Suld2DArrayV2I32Zero:  return "NVPTXISD::Suld2DArrayV2I32Zero";
  case NVPTXISD::Suld2DArrayV2I64Zero:  return "NVPTXISD::Suld2DArrayV2I64Zero";
  case NVPTXISD::Suld2DArrayV4I8Zero:   return "NVPTXISD::Suld2DArrayV4I8Zero";
  case NVPTXISD::Suld2DArrayV4I16Zero:  return "NVPTXISD::Suld2DArrayV4I16Zero";
  case NVPTXISD::Suld2DArrayV4I32Zero:  return "NVPTXISD::Suld2DArrayV4I32Zero";

  case NVPTXISD::Suld3DI8Zero:          return "NVPTXISD::Suld3DI8Zero";
  case NVPTXISD::Suld3DI16Zero:         return "NVPTXISD::Suld3DI16Zero";
  case NVPTXISD::Suld3DI32Zero:         return "NVPTXISD::Suld3DI32Zero";
  case NVPTXISD::Suld3DI64Zero:         return "NVPTXISD::Suld3DI64Zero";
  case NVPTXISD::Suld3DV2I8Zero:        return "NVPTXISD::Suld3DV2I8Zero";
  case NVPTXISD::Suld3DV2I16Zero:       return "NVPTXISD::Suld3DV2I16Zero";
  case NVPTXISD::Suld3DV2I32Zero:       return "NVPTXISD::Suld3DV2I32Zero";
  case NVPTXISD::Suld3DV2I64Zero:       return "NVPTXISD::Suld3DV2I64Zero";
  case NVPTXISD::Suld3DV4I8Zero:        return "NVPTXISD::Suld3DV4I8Zero";
  case NVPTXISD::Suld3DV4I16Zero:       return "NVPTXISD::Suld3DV4I16Zero";
  case NVPTXISD::Suld3DV4I32Zero:       return "NVPTXISD::Suld3DV4I32Zero";
  }
  return nullptr;
}

TargetLoweringBase::LegalizeTypeAction
NVPTXTargetLowering::getPreferredVectorAction(MVT VT) const {
  if (VT.getVectorNumElements() != 1 && VT.getScalarType() == MVT::i1)
    return TypeSplitVector;
  if (VT == MVT::v2f16)
    return TypeLegal;
  return TargetLoweringBase::getPreferredVectorAction(VT);
}

SDValue NVPTXTargetLowering::getSqrtEstimate(SDValue Operand, SelectionDAG &DAG,
                                             int Enabled, int &ExtraSteps,
                                             bool &UseOneConst,
                                             bool Reciprocal) const {
  if (!(Enabled == ReciprocalEstimate::Enabled ||
        (Enabled == ReciprocalEstimate::Unspecified && !usePrecSqrtF32())))
    return SDValue();

  if (ExtraSteps == ReciprocalEstimate::Unspecified)
    ExtraSteps = 0;

  SDLoc DL(Operand);
  EVT VT = Operand.getValueType();
  bool Ftz = useF32FTZ(DAG.getMachineFunction());

  auto MakeIntrinsicCall = [&](Intrinsic::ID IID) {
    return DAG.getNode(ISD::INTRINSIC_WO_CHAIN, DL, VT,
                       DAG.getConstant(IID, DL, MVT::i32), Operand);
  };

  // The sqrt and rsqrt refinement processes assume we always start out with an
  // approximation of the rsqrt.  Therefore, if we're going to do any refinement
  // (i.e. ExtraSteps > 0), we must return an rsqrt.  But if we're *not* doing
  // any refinement, we must return a regular sqrt.
  if (Reciprocal || ExtraSteps > 0) {
    if (VT == MVT::f32)
      return MakeIntrinsicCall(Ftz ? Intrinsic::nvvm_rsqrt_approx_ftz_f
                                   : Intrinsic::nvvm_rsqrt_approx_f);
    else if (VT == MVT::f64)
      return MakeIntrinsicCall(Intrinsic::nvvm_rsqrt_approx_d);
    else
      return SDValue();
  } else {
    if (VT == MVT::f32)
      return MakeIntrinsicCall(Ftz ? Intrinsic::nvvm_sqrt_approx_ftz_f
                                   : Intrinsic::nvvm_sqrt_approx_f);
    else {
      // There's no sqrt.approx.f64 instruction, so we emit
      // reciprocal(rsqrt(x)).  This is faster than
      // select(x == 0, 0, x * rsqrt(x)).  (In fact, it's faster than plain
      // x * rsqrt(x).)
      return DAG.getNode(
          ISD::INTRINSIC_WO_CHAIN, DL, VT,
          DAG.getConstant(Intrinsic::nvvm_rcp_approx_ftz_d, DL, MVT::i32),
          MakeIntrinsicCall(Intrinsic::nvvm_rsqrt_approx_d));
    }
  }
}

SDValue
NVPTXTargetLowering::LowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const {
  SDLoc dl(Op);
  const GlobalAddressSDNode *GAN = cast<GlobalAddressSDNode>(Op);
  auto PtrVT = getPointerTy(DAG.getDataLayout(), GAN->getAddressSpace());
  Op = DAG.getTargetGlobalAddress(GAN->getGlobal(), dl, PtrVT);
  return DAG.getNode(NVPTXISD::Wrapper, dl, PtrVT, Op);
}

std::string NVPTXTargetLowering::getPrototype(
    const DataLayout &DL, Type *retTy, const ArgListTy &Args,
    const SmallVectorImpl<ISD::OutputArg> &Outs, unsigned retAlignment,
    ImmutableCallSite CS) const {
  auto PtrVT = getPointerTy(DL);

  bool isABI = (STI.getSmVersion() >= 20);
  assert(isABI && "Non-ABI compilation is not supported");
  if (!isABI)
    return "";

  std::stringstream O;
  O << "prototype_" << uniqueCallSite << " : .callprototype ";

  if (retTy->getTypeID() == Type::VoidTyID) {
    O << "()";
  } else {
    O << "(";
    if (retTy->isFloatingPointTy() || (retTy->isIntegerTy() && !retTy->isIntegerTy(128))) {
      unsigned size = 0;
      if (auto *ITy = dyn_cast<IntegerType>(retTy)) {
        size = ITy->getBitWidth();
      } else {
        assert(retTy->isFloatingPointTy() &&
               "Floating point type expected here");
        size = retTy->getPrimitiveSizeInBits();
      }
      // PTX ABI requires all scalar return values to be at least 32
      // bits in size.  fp16 normally uses .b16 as its storage type in
      // PTX, so its size must be adjusted here, too.
      if (size < 32)
        size = 32;

      O << ".param .b" << size << " _";
    } else if (isa<PointerType>(retTy)) {
      O << ".param .b" << PtrVT.getSizeInBits() << " _";
    } else if (retTy->isAggregateType() || retTy->isVectorTy() || retTy->isIntegerTy(128)) {
      auto &DL = CS.getCalledFunction()->getParent()->getDataLayout();
      O << ".param .align " << retAlignment << " .b8 _["
        << DL.getTypeAllocSize(retTy) << "]";
    } else {
      llvm_unreachable("Unknown return type");
    }
    O << ") ";
  }
  O << "_ (";

  bool first = true;

  unsigned OIdx = 0;
  for (unsigned i = 0, e = Args.size(); i != e; ++i, ++OIdx) {
    Type *Ty = Args[i].Ty;
    if (!first) {
      O << ", ";
    }
    first = false;

    if (!Outs[OIdx].Flags.isByVal()) {
      if (Ty->isAggregateType() || Ty->isVectorTy() || Ty->isIntegerTy(128)) {
        unsigned align = 0;
        const CallInst *CallI = cast<CallInst>(CS.getInstruction());
        // +1 because index 0 is reserved for return type alignment
        if (!getAlign(*CallI, i + 1, align))
          align = DL.getABITypeAlignment(Ty);
        unsigned sz = DL.getTypeAllocSize(Ty);
        O << ".param .align " << align << " .b8 ";
        O << "_";
        O << "[" << sz << "]";
        // update the index for Outs
        SmallVector<EVT, 16> vtparts;
        ComputeValueVTs(*this, DL, Ty, vtparts);
        if (unsigned len = vtparts.size())
          OIdx += len - 1;
        continue;
      }
      // i8 types in IR will be i16 types in SDAG
      assert((getValueType(DL, Ty) == Outs[OIdx].VT ||
              (getValueType(DL, Ty) == MVT::i8 && Outs[OIdx].VT == MVT::i16)) &&
             "type mismatch between callee prototype and arguments");
      // scalar type
      unsigned sz = 0;
      if (isa<IntegerType>(Ty)) {
        sz = cast<IntegerType>(Ty)->getBitWidth();
        if (sz < 32)
          sz = 32;
      } else if (isa<PointerType>(Ty)) {
        sz = PtrVT.getSizeInBits();
      } else if (Ty->isHalfTy())
        // PTX ABI requires all scalar parameters to be at least 32
        // bits in size.  fp16 normally uses .b16 as its storage type
        // in PTX, so its size must be adjusted here, too.
        sz = 32;
      else
        sz = Ty->getPrimitiveSizeInBits();
      O << ".param .b" << sz << " ";
      O << "_";
      continue;
    }
    auto *PTy = dyn_cast<PointerType>(Ty);
    assert(PTy && "Param with byval attribute should be a pointer type");
    Type *ETy = PTy->getElementType();

    unsigned align = Outs[OIdx].Flags.getByValAlign();
    unsigned sz = DL.getTypeAllocSize(ETy);
    O << ".param .align " << align << " .b8 ";
    O << "_";
    O << "[" << sz << "]";
  }
  O << ");";
  return O.str();
}

unsigned NVPTXTargetLowering::getArgumentAlignment(SDValue Callee,
                                                   ImmutableCallSite CS,
                                                   Type *Ty, unsigned Idx,
                                                   const DataLayout &DL) const {
  if (!CS) {
    // CallSite is zero, fallback to ABI type alignment
    return DL.getABITypeAlignment(Ty);
  }

  unsigned Align = 0;
  const Value *DirectCallee = CS.getCalledFunction();

  if (!DirectCallee) {
    // We don't have a direct function symbol, but that may be because of
    // constant cast instructions in the call.
    const Instruction *CalleeI = CS.getInstruction();
    assert(CalleeI && "Call target is not a function or derived value?");

    // With bitcast'd call targets, the instruction will be the call
    if (isa<CallInst>(CalleeI)) {
      // Check if we have call alignment metadata
      if (getAlign(*cast<CallInst>(CalleeI), Idx, Align))
        return Align;

      const Value *CalleeV = cast<CallInst>(CalleeI)->getCalledValue();
      // Ignore any bitcast instructions
      while (isa<ConstantExpr>(CalleeV)) {
        const ConstantExpr *CE = cast<ConstantExpr>(CalleeV);
        if (!CE->isCast())
          break;
        // Look through the bitcast
        CalleeV = cast<ConstantExpr>(CalleeV)->getOperand(0);
      }

      // We have now looked past all of the bitcasts.  Do we finally have a
      // Function?
      if (isa<Function>(CalleeV))
        DirectCallee = CalleeV;
    }
  }

  // Check for function alignment information if we found that the
  // ultimate target is a Function
  if (DirectCallee)
    if (getAlign(*cast<Function>(DirectCallee), Idx, Align))
      return Align;

  // Call is indirect or alignment information is not available, fall back to
  // the ABI type alignment
  return DL.getABITypeAlignment(Ty);
}

SDValue NVPTXTargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                                       SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  SDLoc dl = CLI.DL;
  SmallVectorImpl<ISD::OutputArg> &Outs = CLI.Outs;
  SmallVectorImpl<SDValue> &OutVals = CLI.OutVals;
  SmallVectorImpl<ISD::InputArg> &Ins = CLI.Ins;
  SDValue Chain = CLI.Chain;
  SDValue Callee = CLI.Callee;
  bool &isTailCall = CLI.IsTailCall;
  ArgListTy &Args = CLI.getArgs();
  Type *RetTy = CLI.RetTy;
  ImmutableCallSite CS = CLI.CS;
  const DataLayout &DL = DAG.getDataLayout();

  bool isABI = (STI.getSmVersion() >= 20);
  assert(isABI && "Non-ABI compilation is not supported");
  if (!isABI)
    return Chain;

  SDValue tempChain = Chain;
  Chain = DAG.getCALLSEQ_START(Chain, uniqueCallSite, 0, dl);
  SDValue InFlag = Chain.getValue(1);

  unsigned paramCount = 0;
  // Args.size() and Outs.size() need not match.
  // Outs.size() will be larger
  //   * if there is an aggregate argument with multiple fields (each field
  //     showing up separately in Outs)
  //   * if there is a vector argument with more than typical vector-length
  //     elements (generally if more than 4) where each vector element is
  //     individually present in Outs.
  // So a different index should be used for indexing into Outs/OutVals.
  // See similar issue in LowerFormalArguments.
  unsigned OIdx = 0;
  // Declare the .params or .reg need to pass values
  // to the function
  for (unsigned i = 0, e = Args.size(); i != e; ++i, ++OIdx) {
    EVT VT = Outs[OIdx].VT;
    Type *Ty = Args[i].Ty;

    if (!Outs[OIdx].Flags.isByVal()) {
      SmallVector<EVT, 16> VTs;
      SmallVector<uint64_t, 16> Offsets;
      ComputePTXValueVTs(*this, DL, Ty, VTs, &Offsets);
      unsigned ArgAlign =
          getArgumentAlignment(Callee, CS, Ty, paramCount + 1, DL);
      unsigned AllocSize = DL.getTypeAllocSize(Ty);
      SDVTList DeclareParamVTs = DAG.getVTList(MVT::Other, MVT::Glue);
      bool NeedAlign; // Does argument declaration specify alignment?
      if (Ty->isAggregateType() || Ty->isVectorTy() || Ty->isIntegerTy(128)) {
        // declare .param .align <align> .b8 .param<n>[<size>];
        SDValue DeclareParamOps[] = {
            Chain, DAG.getConstant(ArgAlign, dl, MVT::i32),
            DAG.getConstant(paramCount, dl, MVT::i32),
            DAG.getConstant(AllocSize, dl, MVT::i32), InFlag};
        Chain = DAG.getNode(NVPTXISD::DeclareParam, dl, DeclareParamVTs,
                            DeclareParamOps);
        NeedAlign = true;
      } else {
        // declare .param .b<size> .param<n>;
        if ((VT.isInteger() || VT.isFloatingPoint()) && AllocSize < 4) {
          // PTX ABI requires integral types to be at least 32 bits in
          // size. FP16 is loaded/stored using i16, so it's handled
          // here as well.
          AllocSize = 4;
        }
        SDValue DeclareScalarParamOps[] = {
            Chain, DAG.getConstant(paramCount, dl, MVT::i32),
            DAG.getConstant(AllocSize * 8, dl, MVT::i32),
            DAG.getConstant(0, dl, MVT::i32), InFlag};
        Chain = DAG.getNode(NVPTXISD::DeclareScalarParam, dl, DeclareParamVTs,
                            DeclareScalarParamOps);
        NeedAlign = false;
      }
      InFlag = Chain.getValue(1);

      // PTX Interoperability Guide 3.3(A): [Integer] Values shorter
      // than 32-bits are sign extended or zero extended, depending on
      // whether they are signed or unsigned types. This case applies
      // only to scalar parameters and not to aggregate values.
      bool ExtendIntegerParam =
          Ty->isIntegerTy() && DL.getTypeAllocSizeInBits(Ty) < 32;

      auto VectorInfo = VectorizePTXValueVTs(VTs, Offsets, ArgAlign);
      SmallVector<SDValue, 6> StoreOperands;
      for (unsigned j = 0, je = VTs.size(); j != je; ++j) {
        // New store.
        if (VectorInfo[j] & PVF_FIRST) {
          assert(StoreOperands.empty() && "Unfinished preceeding store.");
          StoreOperands.push_back(Chain);
          StoreOperands.push_back(DAG.getConstant(paramCount, dl, MVT::i32));
          StoreOperands.push_back(DAG.getConstant(Offsets[j], dl, MVT::i32));
        }

        EVT EltVT = VTs[j];
        SDValue StVal = OutVals[OIdx];
        if (ExtendIntegerParam) {
          assert(VTs.size() == 1 && "Scalar can't have multiple parts.");
          // zext/sext to i32
          StVal = DAG.getNode(Outs[OIdx].Flags.isSExt() ? ISD::SIGN_EXTEND
                                                        : ISD::ZERO_EXTEND,
                              dl, MVT::i32, StVal);
        } else if (EltVT.getSizeInBits() < 16) {
          // Use 16-bit registers for small stores as it's the
          // smallest general purpose register size supported by NVPTX.
          StVal = DAG.getNode(ISD::ANY_EXTEND, dl, MVT::i16, StVal);
        }

        // Record the value to store.
        StoreOperands.push_back(StVal);

        if (VectorInfo[j] & PVF_LAST) {
          unsigned NumElts = StoreOperands.size() - 3;
          NVPTXISD::NodeType Op;
          switch (NumElts) {
          case 1:
            Op = NVPTXISD::StoreParam;
            break;
          case 2:
            Op = NVPTXISD::StoreParamV2;
            break;
          case 4:
            Op = NVPTXISD::StoreParamV4;
            break;
          default:
            llvm_unreachable("Invalid vector info.");
          }

          StoreOperands.push_back(InFlag);

          // Adjust type of the store op if we've extended the scalar
          // return value.
          EVT TheStoreType = ExtendIntegerParam ? MVT::i32 : VTs[j];
          unsigned EltAlign =
              NeedAlign ? GreatestCommonDivisor64(ArgAlign, Offsets[j]) : 0;

          Chain = DAG.getMemIntrinsicNode(
              Op, dl, DAG.getVTList(MVT::Other, MVT::Glue), StoreOperands,
              TheStoreType, MachinePointerInfo(), EltAlign,
              MachineMemOperand::MOStore);
          InFlag = Chain.getValue(1);

          // Cleanup.
          StoreOperands.clear();
        }
        ++OIdx;
      }
      assert(StoreOperands.empty() && "Unfinished parameter store.");
      if (VTs.size() > 0)
        --OIdx;
      ++paramCount;
      continue;
    }

    // ByVal arguments
    SmallVector<EVT, 16> VTs;
    SmallVector<uint64_t, 16> Offsets;
    auto *PTy = dyn_cast<PointerType>(Args[i].Ty);
    assert(PTy && "Type of a byval parameter should be pointer");
    ComputePTXValueVTs(*this, DL, PTy->getElementType(), VTs, &Offsets, 0);

    // declare .param .align <align> .b8 .param<n>[<size>];
    unsigned sz = Outs[OIdx].Flags.getByValSize();
    SDVTList DeclareParamVTs = DAG.getVTList(MVT::Other, MVT::Glue);
    unsigned ArgAlign = Outs[OIdx].Flags.getByValAlign();
    // The ByValAlign in the Outs[OIdx].Flags is alway set at this point,
    // so we don't need to worry about natural alignment or not.
    // See TargetLowering::LowerCallTo().

    // Enforce minumum alignment of 4 to work around ptxas miscompile
    // for sm_50+. See corresponding alignment adjustment in
    // emitFunctionParamList() for details.
    if (ArgAlign < 4)
      ArgAlign = 4;
    SDValue DeclareParamOps[] = {Chain, DAG.getConstant(ArgAlign, dl, MVT::i32),
                                 DAG.getConstant(paramCount, dl, MVT::i32),
                                 DAG.getConstant(sz, dl, MVT::i32), InFlag};
    Chain = DAG.getNode(NVPTXISD::DeclareParam, dl, DeclareParamVTs,
                        DeclareParamOps);
    InFlag = Chain.getValue(1);
    for (unsigned j = 0, je = VTs.size(); j != je; ++j) {
      EVT elemtype = VTs[j];
      int curOffset = Offsets[j];
      unsigned PartAlign = GreatestCommonDivisor64(ArgAlign, curOffset);
      auto PtrVT = getPointerTy(DL);
      SDValue srcAddr = DAG.getNode(ISD::ADD, dl, PtrVT, OutVals[OIdx],
                                    DAG.getConstant(curOffset, dl, PtrVT));
      SDValue theVal = DAG.getLoad(elemtype, dl, tempChain, srcAddr,
                                   MachinePointerInfo(), PartAlign);
      if (elemtype.getSizeInBits() < 16) {
        theVal = DAG.getNode(ISD::ANY_EXTEND, dl, MVT::i16, theVal);
      }
      SDVTList CopyParamVTs = DAG.getVTList(MVT::Other, MVT::Glue);
      SDValue CopyParamOps[] = { Chain,
                                 DAG.getConstant(paramCount, dl, MVT::i32),
                                 DAG.getConstant(curOffset, dl, MVT::i32),
                                 theVal, InFlag };
      Chain = DAG.getMemIntrinsicNode(NVPTXISD::StoreParam, dl, CopyParamVTs,
                                      CopyParamOps, elemtype,
                                      MachinePointerInfo(), /* Align */ 0,
                                      MachineMemOperand::MOStore);

      InFlag = Chain.getValue(1);
    }
    ++paramCount;
  }

  GlobalAddressSDNode *Func = dyn_cast<GlobalAddressSDNode>(Callee.getNode());
  unsigned retAlignment = 0;

  // Handle Result
  if (Ins.size() > 0) {
    SmallVector<EVT, 16> resvtparts;
    ComputeValueVTs(*this, DL, RetTy, resvtparts);

    // Declare
    //  .param .align 16 .b8 retval0[<size-in-bytes>], or
    //  .param .b<size-in-bits> retval0
    unsigned resultsz = DL.getTypeAllocSizeInBits(RetTy);
    // Emit ".param .b<size-in-bits> retval0" instead of byte arrays only for
    // these three types to match the logic in
    // NVPTXAsmPrinter::printReturnValStr and NVPTXTargetLowering::getPrototype.
    // Plus, this behavior is consistent with nvcc's.
    if (RetTy->isFloatingPointTy() || RetTy->isPointerTy() ||
        (RetTy->isIntegerTy() && !RetTy->isIntegerTy(128))) {
      // Scalar needs to be at least 32bit wide
      if (resultsz < 32)
        resultsz = 32;
      SDVTList DeclareRetVTs = DAG.getVTList(MVT::Other, MVT::Glue);
      SDValue DeclareRetOps[] = { Chain, DAG.getConstant(1, dl, MVT::i32),
                                  DAG.getConstant(resultsz, dl, MVT::i32),
                                  DAG.getConstant(0, dl, MVT::i32), InFlag };
      Chain = DAG.getNode(NVPTXISD::DeclareRet, dl, DeclareRetVTs,
                          DeclareRetOps);
      InFlag = Chain.getValue(1);
    } else {
      retAlignment = getArgumentAlignment(Callee, CS, RetTy, 0, DL);
      SDVTList DeclareRetVTs = DAG.getVTList(MVT::Other, MVT::Glue);
      SDValue DeclareRetOps[] = { Chain,
                                  DAG.getConstant(retAlignment, dl, MVT::i32),
                                  DAG.getConstant(resultsz / 8, dl, MVT::i32),
                                  DAG.getConstant(0, dl, MVT::i32), InFlag };
      Chain = DAG.getNode(NVPTXISD::DeclareRetParam, dl, DeclareRetVTs,
                          DeclareRetOps);
      InFlag = Chain.getValue(1);
    }
  }

  // Both indirect calls and libcalls have nullptr Func. In order to distinguish
  // between them we must rely on the call site value which is valid for
  // indirect calls but is always null for libcalls.
  bool isIndirectCall = !Func && CS;

  if (isa<ExternalSymbolSDNode>(Callee)) {
    Function* CalleeFunc = nullptr;

    // Try to find the callee in the current module.
    Callee = DAG.getSymbolFunctionGlobalAddress(Callee, &CalleeFunc);
    assert(CalleeFunc != nullptr && "Libcall callee must be set.");

    // Set the "libcall callee" attribute to indicate that the function
    // must always have a declaration.
    CalleeFunc->addFnAttr("nvptx-libcall-callee", "true");
  }

  if (isIndirectCall) {
    // This is indirect function call case : PTX requires a prototype of the
    // form
    // proto_0 : .callprototype(.param .b32 _) _ (.param .b32 _);
    // to be emitted, and the label has to used as the last arg of call
    // instruction.
    // The prototype is embedded in a string and put as the operand for a
    // CallPrototype SDNode which will print out to the value of the string.
    SDVTList ProtoVTs = DAG.getVTList(MVT::Other, MVT::Glue);
    std::string Proto = getPrototype(DL, RetTy, Args, Outs, retAlignment, CS);
    const char *ProtoStr =
      nvTM->getManagedStrPool()->getManagedString(Proto.c_str())->c_str();
    SDValue ProtoOps[] = {
      Chain, DAG.getTargetExternalSymbol(ProtoStr, MVT::i32), InFlag,
    };
    Chain = DAG.getNode(NVPTXISD::CallPrototype, dl, ProtoVTs, ProtoOps);
    InFlag = Chain.getValue(1);
  }
  // Op to just print "call"
  SDVTList PrintCallVTs = DAG.getVTList(MVT::Other, MVT::Glue);
  SDValue PrintCallOps[] = {
    Chain, DAG.getConstant((Ins.size() == 0) ? 0 : 1, dl, MVT::i32), InFlag
  };
  // We model convergent calls as separate opcodes.
  unsigned Opcode = isIndirectCall ? NVPTXISD::PrintCall : NVPTXISD::PrintCallUni;
  if (CLI.IsConvergent)
    Opcode = Opcode == NVPTXISD::PrintCallUni ? NVPTXISD::PrintConvergentCallUni
                                              : NVPTXISD::PrintConvergentCall;
  Chain = DAG.getNode(Opcode, dl, PrintCallVTs, PrintCallOps);
  InFlag = Chain.getValue(1);

  // Ops to print out the function name
  SDVTList CallVoidVTs = DAG.getVTList(MVT::Other, MVT::Glue);
  SDValue CallVoidOps[] = { Chain, Callee, InFlag };
  Chain = DAG.getNode(NVPTXISD::CallVoid, dl, CallVoidVTs, CallVoidOps);
  InFlag = Chain.getValue(1);

  // Ops to print out the param list
  SDVTList CallArgBeginVTs = DAG.getVTList(MVT::Other, MVT::Glue);
  SDValue CallArgBeginOps[] = { Chain, InFlag };
  Chain = DAG.getNode(NVPTXISD::CallArgBegin, dl, CallArgBeginVTs,
                      CallArgBeginOps);
  InFlag = Chain.getValue(1);

  for (unsigned i = 0, e = paramCount; i != e; ++i) {
    unsigned opcode;
    if (i == (e - 1))
      opcode = NVPTXISD::LastCallArg;
    else
      opcode = NVPTXISD::CallArg;
    SDVTList CallArgVTs = DAG.getVTList(MVT::Other, MVT::Glue);
    SDValue CallArgOps[] = { Chain, DAG.getConstant(1, dl, MVT::i32),
                             DAG.getConstant(i, dl, MVT::i32), InFlag };
    Chain = DAG.getNode(opcode, dl, CallArgVTs, CallArgOps);
    InFlag = Chain.getValue(1);
  }
  SDVTList CallArgEndVTs = DAG.getVTList(MVT::Other, MVT::Glue);
  SDValue CallArgEndOps[] = { Chain,
                              DAG.getConstant(isIndirectCall ? 0 : 1, dl, MVT::i32),
                              InFlag };
  Chain = DAG.getNode(NVPTXISD::CallArgEnd, dl, CallArgEndVTs, CallArgEndOps);
  InFlag = Chain.getValue(1);

  if (isIndirectCall) {
    SDVTList PrototypeVTs = DAG.getVTList(MVT::Other, MVT::Glue);
    SDValue PrototypeOps[] = { Chain,
                               DAG.getConstant(uniqueCallSite, dl, MVT::i32),
                               InFlag };
    Chain = DAG.getNode(NVPTXISD::Prototype, dl, PrototypeVTs, PrototypeOps);
    InFlag = Chain.getValue(1);
  }

  SmallVector<SDValue, 16> ProxyRegOps;
  SmallVector<Optional<MVT>, 16> ProxyRegTruncates;

  // Generate loads from param memory/moves from registers for result
  if (Ins.size() > 0) {
    SmallVector<EVT, 16> VTs;
    SmallVector<uint64_t, 16> Offsets;
    ComputePTXValueVTs(*this, DL, RetTy, VTs, &Offsets, 0);
    assert(VTs.size() == Ins.size() && "Bad value decomposition");

    unsigned RetAlign = getArgumentAlignment(Callee, CS, RetTy, 0, DL);
    auto VectorInfo = VectorizePTXValueVTs(VTs, Offsets, RetAlign);

    SmallVector<EVT, 6> LoadVTs;
    int VecIdx = -1; // Index of the first element of the vector.

    // PTX Interoperability Guide 3.3(A): [Integer] Values shorter than
    // 32-bits are sign extended or zero extended, depending on whether
    // they are signed or unsigned types.
    bool ExtendIntegerRetVal =
        RetTy->isIntegerTy() && DL.getTypeAllocSizeInBits(RetTy) < 32;

    for (unsigned i = 0, e = VTs.size(); i != e; ++i) {
      bool needTruncate = false;
      EVT TheLoadType = VTs[i];
      EVT EltType = Ins[i].VT;
      unsigned EltAlign = GreatestCommonDivisor64(RetAlign, Offsets[i]);
      if (ExtendIntegerRetVal) {
        TheLoadType = MVT::i32;
        EltType = MVT::i32;
        needTruncate = true;
      } else if (TheLoadType.getSizeInBits() < 16) {
        if (VTs[i].isInteger())
          needTruncate = true;
        EltType = MVT::i16;
      }

      // Record index of the very first element of the vector.
      if (VectorInfo[i] & PVF_FIRST) {
        assert(VecIdx == -1 && LoadVTs.empty() && "Orphaned operand list.");
        VecIdx = i;
      }

      LoadVTs.push_back(EltType);

      if (VectorInfo[i] & PVF_LAST) {
        unsigned NumElts = LoadVTs.size();
        LoadVTs.push_back(MVT::Other);
        LoadVTs.push_back(MVT::Glue);
        NVPTXISD::NodeType Op;
        switch (NumElts) {
        case 1:
          Op = NVPTXISD::LoadParam;
          break;
        case 2:
          Op = NVPTXISD::LoadParamV2;
          break;
        case 4:
          Op = NVPTXISD::LoadParamV4;
          break;
        default:
          llvm_unreachable("Invalid vector info.");
        }

        SDValue LoadOperands[] = {
            Chain, DAG.getConstant(1, dl, MVT::i32),
            DAG.getConstant(Offsets[VecIdx], dl, MVT::i32), InFlag};
        SDValue RetVal = DAG.getMemIntrinsicNode(
            Op, dl, DAG.getVTList(LoadVTs), LoadOperands, TheLoadType,
            MachinePointerInfo(), EltAlign,
            MachineMemOperand::MOLoad);

        for (unsigned j = 0; j < NumElts; ++j) {
          ProxyRegOps.push_back(RetVal.getValue(j));

          if (needTruncate)
            ProxyRegTruncates.push_back(Optional<MVT>(Ins[VecIdx + j].VT));
          else
            ProxyRegTruncates.push_back(Optional<MVT>());
        }

        Chain = RetVal.getValue(NumElts);
        InFlag = RetVal.getValue(NumElts + 1);

        // Cleanup
        VecIdx = -1;
        LoadVTs.clear();
      }
    }
  }

  Chain = DAG.getCALLSEQ_END(Chain,
                             DAG.getIntPtrConstant(uniqueCallSite, dl, true),
                             DAG.getIntPtrConstant(uniqueCallSite + 1, dl,
                                                   true),
                             InFlag, dl);
  InFlag = Chain.getValue(1);
  uniqueCallSite++;

  // Append ProxyReg instructions to the chain to make sure that `callseq_end`
  // will not get lost. Otherwise, during libcalls expansion, the nodes can become
  // dangling.
  for (unsigned i = 0; i < ProxyRegOps.size(); ++i) {
    SDValue Ret = DAG.getNode(
      NVPTXISD::ProxyReg, dl,
      DAG.getVTList(ProxyRegOps[i].getSimpleValueType(), MVT::Other, MVT::Glue),
      { Chain, ProxyRegOps[i], InFlag }
    );

    Chain = Ret.getValue(1);
    InFlag = Ret.getValue(2);

    if (ProxyRegTruncates[i].hasValue()) {
      Ret = DAG.getNode(ISD::TRUNCATE, dl, ProxyRegTruncates[i].getValue(), Ret);
    }

    InVals.push_back(Ret);
  }

  // set isTailCall to false for now, until we figure out how to express
  // tail call optimization in PTX
  isTailCall = false;
  return Chain;
}

// By default CONCAT_VECTORS is lowered by ExpandVectorBuildThroughStack()
// (see LegalizeDAG.cpp). This is slow and uses local memory.
// We use extract/insert/build vector just as what LegalizeOp() does in llvm 2.5
SDValue
NVPTXTargetLowering::LowerCONCAT_VECTORS(SDValue Op, SelectionDAG &DAG) const {
  SDNode *Node = Op.getNode();
  SDLoc dl(Node);
  SmallVector<SDValue, 8> Ops;
  unsigned NumOperands = Node->getNumOperands();
  for (unsigned i = 0; i < NumOperands; ++i) {
    SDValue SubOp = Node->getOperand(i);
    EVT VVT = SubOp.getNode()->getValueType(0);
    EVT EltVT = VVT.getVectorElementType();
    unsigned NumSubElem = VVT.getVectorNumElements();
    for (unsigned j = 0; j < NumSubElem; ++j) {
      Ops.push_back(DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, EltVT, SubOp,
                                DAG.getIntPtrConstant(j, dl)));
    }
  }
  return DAG.getBuildVector(Node->getValueType(0), dl, Ops);
}

// We can init constant f16x2 with a single .b32 move.  Normally it
// would get lowered as two constant loads and vector-packing move.
//        mov.b16         %h1, 0x4000;
//        mov.b16         %h2, 0x3C00;
//        mov.b32         %hh2, {%h2, %h1};
// Instead we want just a constant move:
//        mov.b32         %hh2, 0x40003C00
//
// This results in better SASS code with CUDA 7.x. Ptxas in CUDA 8.0
// generates good SASS in both cases.
SDValue NVPTXTargetLowering::LowerBUILD_VECTOR(SDValue Op,
                                               SelectionDAG &DAG) const {
  //return Op;
  if (!(Op->getValueType(0) == MVT::v2f16 &&
        isa<ConstantFPSDNode>(Op->getOperand(0)) &&
        isa<ConstantFPSDNode>(Op->getOperand(1))))
    return Op;

  APInt E0 =
      cast<ConstantFPSDNode>(Op->getOperand(0))->getValueAPF().bitcastToAPInt();
  APInt E1 =
      cast<ConstantFPSDNode>(Op->getOperand(1))->getValueAPF().bitcastToAPInt();
  SDValue Const =
      DAG.getConstant(E1.zext(32).shl(16) | E0.zext(32), SDLoc(Op), MVT::i32);
  return DAG.getNode(ISD::BITCAST, SDLoc(Op), MVT::v2f16, Const);
}

SDValue NVPTXTargetLowering::LowerEXTRACT_VECTOR_ELT(SDValue Op,
                                                     SelectionDAG &DAG) const {
  SDValue Index = Op->getOperand(1);
  // Constant index will be matched by tablegen.
  if (isa<ConstantSDNode>(Index.getNode()))
    return Op;

  // Extract individual elements and select one of them.
  SDValue Vector = Op->getOperand(0);
  EVT VectorVT = Vector.getValueType();
  assert(VectorVT == MVT::v2f16 && "Unexpected vector type.");
  EVT EltVT = VectorVT.getVectorElementType();

  SDLoc dl(Op.getNode());
  SDValue E0 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, EltVT, Vector,
                           DAG.getIntPtrConstant(0, dl));
  SDValue E1 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, EltVT, Vector,
                           DAG.getIntPtrConstant(1, dl));
  return DAG.getSelectCC(dl, Index, DAG.getIntPtrConstant(0, dl), E0, E1,
                         ISD::CondCode::SETEQ);
}

/// LowerShiftRightParts - Lower SRL_PARTS, SRA_PARTS, which
/// 1) returns two i32 values and take a 2 x i32 value to shift plus a shift
///    amount, or
/// 2) returns two i64 values and take a 2 x i64 value to shift plus a shift
///    amount.
SDValue NVPTXTargetLowering::LowerShiftRightParts(SDValue Op,
                                                  SelectionDAG &DAG) const {
  assert(Op.getNumOperands() == 3 && "Not a double-shift!");
  assert(Op.getOpcode() == ISD::SRA_PARTS || Op.getOpcode() == ISD::SRL_PARTS);

  EVT VT = Op.getValueType();
  unsigned VTBits = VT.getSizeInBits();
  SDLoc dl(Op);
  SDValue ShOpLo = Op.getOperand(0);
  SDValue ShOpHi = Op.getOperand(1);
  SDValue ShAmt  = Op.getOperand(2);
  unsigned Opc = (Op.getOpcode() == ISD::SRA_PARTS) ? ISD::SRA : ISD::SRL;

  if (VTBits == 32 && STI.getSmVersion() >= 35) {
    // For 32bit and sm35, we can use the funnel shift 'shf' instruction.
    // {dHi, dLo} = {aHi, aLo} >> Amt
    //   dHi = aHi >> Amt
    //   dLo = shf.r.clamp aLo, aHi, Amt

    SDValue Hi = DAG.getNode(Opc, dl, VT, ShOpHi, ShAmt);
    SDValue Lo = DAG.getNode(NVPTXISD::FUN_SHFR_CLAMP, dl, VT, ShOpLo, ShOpHi,
                             ShAmt);

    SDValue Ops[2] = { Lo, Hi };
    return DAG.getMergeValues(Ops, dl);
  }
  else {
    // {dHi, dLo} = {aHi, aLo} >> Amt
    // - if (Amt>=size) then
    //      dLo = aHi >> (Amt-size)
    //      dHi = aHi >> Amt (this is either all 0 or all 1)
    //   else
    //      dLo = (aLo >>logic Amt) | (aHi << (size-Amt))
    //      dHi = aHi >> Amt

    SDValue RevShAmt = DAG.getNode(ISD::SUB, dl, MVT::i32,
                                   DAG.getConstant(VTBits, dl, MVT::i32),
                                   ShAmt);
    SDValue Tmp1 = DAG.getNode(ISD::SRL, dl, VT, ShOpLo, ShAmt);
    SDValue ExtraShAmt = DAG.getNode(ISD::SUB, dl, MVT::i32, ShAmt,
                                     DAG.getConstant(VTBits, dl, MVT::i32));
    SDValue Tmp2 = DAG.getNode(ISD::SHL, dl, VT, ShOpHi, RevShAmt);
    SDValue FalseVal = DAG.getNode(ISD::OR, dl, VT, Tmp1, Tmp2);
    SDValue TrueVal = DAG.getNode(Opc, dl, VT, ShOpHi, ExtraShAmt);

    SDValue Cmp = DAG.getSetCC(dl, MVT::i1, ShAmt,
                               DAG.getConstant(VTBits, dl, MVT::i32),
                               ISD::SETGE);
    SDValue Hi = DAG.getNode(Opc, dl, VT, ShOpHi, ShAmt);
    SDValue Lo = DAG.getNode(ISD::SELECT, dl, VT, Cmp, TrueVal, FalseVal);

    SDValue Ops[2] = { Lo, Hi };
    return DAG.getMergeValues(Ops, dl);
  }
}

/// LowerShiftLeftParts - Lower SHL_PARTS, which
/// 1) returns two i32 values and take a 2 x i32 value to shift plus a shift
///    amount, or
/// 2) returns two i64 values and take a 2 x i64 value to shift plus a shift
///    amount.
SDValue NVPTXTargetLowering::LowerShiftLeftParts(SDValue Op,
                                                 SelectionDAG &DAG) const {
  assert(Op.getNumOperands() == 3 && "Not a double-shift!");
  assert(Op.getOpcode() == ISD::SHL_PARTS);

  EVT VT = Op.getValueType();
  unsigned VTBits = VT.getSizeInBits();
  SDLoc dl(Op);
  SDValue ShOpLo = Op.getOperand(0);
  SDValue ShOpHi = Op.getOperand(1);
  SDValue ShAmt  = Op.getOperand(2);

  if (VTBits == 32 && STI.getSmVersion() >= 35) {
    // For 32bit and sm35, we can use the funnel shift 'shf' instruction.
    // {dHi, dLo} = {aHi, aLo} << Amt
    //   dHi = shf.l.clamp aLo, aHi, Amt
    //   dLo = aLo << Amt

    SDValue Hi = DAG.getNode(NVPTXISD::FUN_SHFL_CLAMP, dl, VT, ShOpLo, ShOpHi,
                             ShAmt);
    SDValue Lo = DAG.getNode(ISD::SHL, dl, VT, ShOpLo, ShAmt);

    SDValue Ops[2] = { Lo, Hi };
    return DAG.getMergeValues(Ops, dl);
  }
  else {
    // {dHi, dLo} = {aHi, aLo} << Amt
    // - if (Amt>=size) then
    //      dLo = aLo << Amt (all 0)
    //      dLo = aLo << (Amt-size)
    //   else
    //      dLo = aLo << Amt
    //      dHi = (aHi << Amt) | (aLo >> (size-Amt))

    SDValue RevShAmt = DAG.getNode(ISD::SUB, dl, MVT::i32,
                                   DAG.getConstant(VTBits, dl, MVT::i32),
                                   ShAmt);
    SDValue Tmp1 = DAG.getNode(ISD::SHL, dl, VT, ShOpHi, ShAmt);
    SDValue ExtraShAmt = DAG.getNode(ISD::SUB, dl, MVT::i32, ShAmt,
                                     DAG.getConstant(VTBits, dl, MVT::i32));
    SDValue Tmp2 = DAG.getNode(ISD::SRL, dl, VT, ShOpLo, RevShAmt);
    SDValue FalseVal = DAG.getNode(ISD::OR, dl, VT, Tmp1, Tmp2);
    SDValue TrueVal = DAG.getNode(ISD::SHL, dl, VT, ShOpLo, ExtraShAmt);

    SDValue Cmp = DAG.getSetCC(dl, MVT::i1, ShAmt,
                               DAG.getConstant(VTBits, dl, MVT::i32),
                               ISD::SETGE);
    SDValue Lo = DAG.getNode(ISD::SHL, dl, VT, ShOpLo, ShAmt);
    SDValue Hi = DAG.getNode(ISD::SELECT, dl, VT, Cmp, TrueVal, FalseVal);

    SDValue Ops[2] = { Lo, Hi };
    return DAG.getMergeValues(Ops, dl);
  }
}

SDValue
NVPTXTargetLowering::LowerOperation(SDValue Op, SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  case ISD::RETURNADDR:
    return SDValue();
  case ISD::FRAMEADDR:
    return SDValue();
  case ISD::GlobalAddress:
    return LowerGlobalAddress(Op, DAG);
  case ISD::INTRINSIC_W_CHAIN:
    return Op;
  case ISD::BUILD_VECTOR:
    return LowerBUILD_VECTOR(Op, DAG);
  case ISD::EXTRACT_SUBVECTOR:
    return Op;
  case ISD::EXTRACT_VECTOR_ELT:
    return LowerEXTRACT_VECTOR_ELT(Op, DAG);
  case ISD::CONCAT_VECTORS:
    return LowerCONCAT_VECTORS(Op, DAG);
  case ISD::STORE:
    return LowerSTORE(Op, DAG);
  case ISD::LOAD:
    return LowerLOAD(Op, DAG);
  case ISD::SHL_PARTS:
    return LowerShiftLeftParts(Op, DAG);
  case ISD::SRA_PARTS:
  case ISD::SRL_PARTS:
    return LowerShiftRightParts(Op, DAG);
  case ISD::SELECT:
    return LowerSelect(Op, DAG);
  default:
    llvm_unreachable("Custom lowering not defined for operation");
  }
}

SDValue NVPTXTargetLowering::LowerSelect(SDValue Op, SelectionDAG &DAG) const {
  SDValue Op0 = Op->getOperand(0);
  SDValue Op1 = Op->getOperand(1);
  SDValue Op2 = Op->getOperand(2);
  SDLoc DL(Op.getNode());

  assert(Op.getValueType() == MVT::i1 && "Custom lowering enabled only for i1");

  Op1 = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i32, Op1);
  Op2 = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i32, Op2);
  SDValue Select = DAG.getNode(ISD::SELECT, DL, MVT::i32, Op0, Op1, Op2);
  SDValue Trunc = DAG.getNode(ISD::TRUNCATE, DL, MVT::i1, Select);

  return Trunc;
}

SDValue NVPTXTargetLowering::LowerLOAD(SDValue Op, SelectionDAG &DAG) const {
  if (Op.getValueType() == MVT::i1)
    return LowerLOADi1(Op, DAG);

  // v2f16 is legal, so we can't rely on legalizer to handle unaligned
  // loads and have to handle it here.
  if (Op.getValueType() == MVT::v2f16) {
    LoadSDNode *Load = cast<LoadSDNode>(Op);
    EVT MemVT = Load->getMemoryVT();
    if (!allowsMemoryAccess(*DAG.getContext(), DAG.getDataLayout(), MemVT,
                            Load->getAddressSpace(), Load->getAlignment())) {
      SDValue Ops[2];
      std::tie(Ops[0], Ops[1]) = expandUnalignedLoad(Load, DAG);
      return DAG.getMergeValues(Ops, SDLoc(Op));
    }
  }

  return SDValue();
}

// v = ld i1* addr
//   =>
// v1 = ld i8* addr (-> i16)
// v = trunc i16 to i1
SDValue NVPTXTargetLowering::LowerLOADi1(SDValue Op, SelectionDAG &DAG) const {
  SDNode *Node = Op.getNode();
  LoadSDNode *LD = cast<LoadSDNode>(Node);
  SDLoc dl(Node);
  assert(LD->getExtensionType() == ISD::NON_EXTLOAD);
  assert(Node->getValueType(0) == MVT::i1 &&
         "Custom lowering for i1 load only");
  SDValue newLD = DAG.getLoad(MVT::i16, dl, LD->getChain(), LD->getBasePtr(),
                              LD->getPointerInfo(), LD->getAlignment(),
                              LD->getMemOperand()->getFlags());
  SDValue result = DAG.getNode(ISD::TRUNCATE, dl, MVT::i1, newLD);
  // The legalizer (the caller) is expecting two values from the legalized
  // load, so we build a MergeValues node for it. See ExpandUnalignedLoad()
  // in LegalizeDAG.cpp which also uses MergeValues.
  SDValue Ops[] = { result, LD->getChain() };
  return DAG.getMergeValues(Ops, dl);
}

SDValue NVPTXTargetLowering::LowerSTORE(SDValue Op, SelectionDAG &DAG) const {
  StoreSDNode *Store = cast<StoreSDNode>(Op);
  EVT VT = Store->getMemoryVT();

  if (VT == MVT::i1)
    return LowerSTOREi1(Op, DAG);

  // v2f16 is legal, so we can't rely on legalizer to handle unaligned
  // stores and have to handle it here.
  if (VT == MVT::v2f16 &&
      !allowsMemoryAccess(*DAG.getContext(), DAG.getDataLayout(), VT,
                          Store->getAddressSpace(), Store->getAlignment()))
    return expandUnalignedStore(Store, DAG);

  if (VT.isVector())
    return LowerSTOREVector(Op, DAG);

  return SDValue();
}

SDValue
NVPTXTargetLowering::LowerSTOREVector(SDValue Op, SelectionDAG &DAG) const {
  SDNode *N = Op.getNode();
  SDValue Val = N->getOperand(1);
  SDLoc DL(N);
  EVT ValVT = Val.getValueType();

  if (ValVT.isVector()) {
    // We only handle "native" vector sizes for now, e.g. <4 x double> is not
    // legal.  We can (and should) split that into 2 stores of <2 x double> here
    // but I'm leaving that as a TODO for now.
    if (!ValVT.isSimple())
      return SDValue();
    switch (ValVT.getSimpleVT().SimpleTy) {
    default:
      return SDValue();
    case MVT::v2i8:
    case MVT::v2i16:
    case MVT::v2i32:
    case MVT::v2i64:
    case MVT::v2f16:
    case MVT::v2f32:
    case MVT::v2f64:
    case MVT::v4i8:
    case MVT::v4i16:
    case MVT::v4i32:
    case MVT::v4f16:
    case MVT::v4f32:
    case MVT::v8f16: // <4 x f16x2>
      // This is a "native" vector type
      break;
    }

    MemSDNode *MemSD = cast<MemSDNode>(N);
    const DataLayout &TD = DAG.getDataLayout();

    unsigned Align = MemSD->getAlignment();
    unsigned PrefAlign =
        TD.getPrefTypeAlignment(ValVT.getTypeForEVT(*DAG.getContext()));
    if (Align < PrefAlign) {
      // This store is not sufficiently aligned, so bail out and let this vector
      // store be scalarized.  Note that we may still be able to emit smaller
      // vector stores.  For example, if we are storing a <4 x float> with an
      // alignment of 8, this check will fail but the legalizer will try again
      // with 2 x <2 x float>, which will succeed with an alignment of 8.
      return SDValue();
    }

    unsigned Opcode = 0;
    EVT EltVT = ValVT.getVectorElementType();
    unsigned NumElts = ValVT.getVectorNumElements();

    // Since StoreV2 is a target node, we cannot rely on DAG type legalization.
    // Therefore, we must ensure the type is legal.  For i1 and i8, we set the
    // stored type to i16 and propagate the "real" type as the memory type.
    bool NeedExt = false;
    if (EltVT.getSizeInBits() < 16)
      NeedExt = true;

    bool StoreF16x2 = false;
    switch (NumElts) {
    default:
      return SDValue();
    case 2:
      Opcode = NVPTXISD::StoreV2;
      break;
    case 4:
      Opcode = NVPTXISD::StoreV4;
      break;
    case 8:
      // v8f16 is a special case. PTX doesn't have st.v8.f16
      // instruction. Instead, we split the vector into v2f16 chunks and
      // store them with st.v4.b32.
      assert(EltVT == MVT::f16 && "Wrong type for the vector.");
      Opcode = NVPTXISD::StoreV4;
      StoreF16x2 = true;
      break;
    }

    SmallVector<SDValue, 8> Ops;

    // First is the chain
    Ops.push_back(N->getOperand(0));

    if (StoreF16x2) {
      // Combine f16,f16 -> v2f16
      NumElts /= 2;
      for (unsigned i = 0; i < NumElts; ++i) {
        SDValue E0 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::f16, Val,
                                 DAG.getIntPtrConstant(i * 2, DL));
        SDValue E1 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::f16, Val,
                                 DAG.getIntPtrConstant(i * 2 + 1, DL));
        SDValue V2 = DAG.getNode(ISD::BUILD_VECTOR, DL, MVT::v2f16, E0, E1);
        Ops.push_back(V2);
      }
    } else {
      // Then the split values
      for (unsigned i = 0; i < NumElts; ++i) {
        SDValue ExtVal = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, EltVT, Val,
                                     DAG.getIntPtrConstant(i, DL));
        if (NeedExt)
          ExtVal = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i16, ExtVal);
        Ops.push_back(ExtVal);
      }
    }

    // Then any remaining arguments
    Ops.append(N->op_begin() + 2, N->op_end());

    SDValue NewSt =
        DAG.getMemIntrinsicNode(Opcode, DL, DAG.getVTList(MVT::Other), Ops,
                                MemSD->getMemoryVT(), MemSD->getMemOperand());

    // return DCI.CombineTo(N, NewSt, true);
    return NewSt;
  }

  return SDValue();
}

// st i1 v, addr
//    =>
// v1 = zxt v to i16
// st.u8 i16, addr
SDValue NVPTXTargetLowering::LowerSTOREi1(SDValue Op, SelectionDAG &DAG) const {
  SDNode *Node = Op.getNode();
  SDLoc dl(Node);
  StoreSDNode *ST = cast<StoreSDNode>(Node);
  SDValue Tmp1 = ST->getChain();
  SDValue Tmp2 = ST->getBasePtr();
  SDValue Tmp3 = ST->getValue();
  assert(Tmp3.getValueType() == MVT::i1 && "Custom lowering for i1 store only");
  Tmp3 = DAG.getNode(ISD::ZERO_EXTEND, dl, MVT::i16, Tmp3);
  SDValue Result =
      DAG.getTruncStore(Tmp1, dl, Tmp3, Tmp2, ST->getPointerInfo(), MVT::i8,
                        ST->getAlignment(), ST->getMemOperand()->getFlags());
  return Result;
}

SDValue
NVPTXTargetLowering::getParamSymbol(SelectionDAG &DAG, int idx, EVT v) const {
  std::string ParamSym;
  raw_string_ostream ParamStr(ParamSym);

  ParamStr << DAG.getMachineFunction().getName() << "_param_" << idx;
  ParamStr.flush();

  std::string *SavedStr =
    nvTM->getManagedStrPool()->getManagedString(ParamSym.c_str());
  return DAG.getTargetExternalSymbol(SavedStr->c_str(), v);
}

// Check to see if the kernel argument is image*_t or sampler_t

static bool isImageOrSamplerVal(const Value *arg, const Module *context) {
  static const char *const specialTypes[] = { "struct._image2d_t",
                                              "struct._image3d_t",
                                              "struct._sampler_t" };

  Type *Ty = arg->getType();
  auto *PTy = dyn_cast<PointerType>(Ty);

  if (!PTy)
    return false;

  if (!context)
    return false;

  auto *STy = dyn_cast<StructType>(PTy->getElementType());
  if (!STy || STy->isLiteral())
    return false;

  return std::find(std::begin(specialTypes), std::end(specialTypes),
                   STy->getName()) != std::end(specialTypes);
}

SDValue NVPTXTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  MachineFunction &MF = DAG.getMachineFunction();
  const DataLayout &DL = DAG.getDataLayout();
  auto PtrVT = getPointerTy(DAG.getDataLayout());

  const Function *F = &MF.getFunction();
  const AttributeList &PAL = F->getAttributes();
  const TargetLowering *TLI = STI.getTargetLowering();

  SDValue Root = DAG.getRoot();
  std::vector<SDValue> OutChains;

  bool isABI = (STI.getSmVersion() >= 20);
  assert(isABI && "Non-ABI compilation is not supported");
  if (!isABI)
    return Chain;

  std::vector<Type *> argTypes;
  std::vector<const Argument *> theArgs;
  for (const Argument &I : F->args()) {
    theArgs.push_back(&I);
    argTypes.push_back(I.getType());
  }
  // argTypes.size() (or theArgs.size()) and Ins.size() need not match.
  // Ins.size() will be larger
  //   * if there is an aggregate argument with multiple fields (each field
  //     showing up separately in Ins)
  //   * if there is a vector argument with more than typical vector-length
  //     elements (generally if more than 4) where each vector element is
  //     individually present in Ins.
  // So a different index should be used for indexing into Ins.
  // See similar issue in LowerCall.
  unsigned InsIdx = 0;

  int idx = 0;
  for (unsigned i = 0, e = theArgs.size(); i != e; ++i, ++idx, ++InsIdx) {
    Type *Ty = argTypes[i];

    // If the kernel argument is image*_t or sampler_t, convert it to
    // a i32 constant holding the parameter position. This can later
    // matched in the AsmPrinter to output the correct mangled name.
    if (isImageOrSamplerVal(
            theArgs[i],
            (theArgs[i]->getParent() ? theArgs[i]->getParent()->getParent()
                                     : nullptr))) {
      assert(isKernelFunction(*F) &&
             "Only kernels can have image/sampler params");
      InVals.push_back(DAG.getConstant(i + 1, dl, MVT::i32));
      continue;
    }

    if (theArgs[i]->use_empty()) {
      // argument is dead
      if (Ty->isAggregateType() || Ty->isIntegerTy(128)) {
        SmallVector<EVT, 16> vtparts;

        ComputePTXValueVTs(*this, DAG.getDataLayout(), Ty, vtparts);
        assert(vtparts.size() > 0 && "empty aggregate type not expected");
        for (unsigned parti = 0, parte = vtparts.size(); parti != parte;
             ++parti) {
          InVals.push_back(DAG.getNode(ISD::UNDEF, dl, Ins[InsIdx].VT));
          ++InsIdx;
        }
        if (vtparts.size() > 0)
          --InsIdx;
        continue;
      }
      if (Ty->isVectorTy()) {
        EVT ObjectVT = getValueType(DL, Ty);
        unsigned NumRegs = TLI->getNumRegisters(F->getContext(), ObjectVT);
        for (unsigned parti = 0; parti < NumRegs; ++parti) {
          InVals.push_back(DAG.getNode(ISD::UNDEF, dl, Ins[InsIdx].VT));
          ++InsIdx;
        }
        if (NumRegs > 0)
          --InsIdx;
        continue;
      }
      InVals.push_back(DAG.getNode(ISD::UNDEF, dl, Ins[InsIdx].VT));
      continue;
    }

    // In the following cases, assign a node order of "idx+1"
    // to newly created nodes. The SDNodes for params have to
    // appear in the same order as their order of appearance
    // in the original function. "idx+1" holds that order.
    if (!PAL.hasParamAttribute(i, Attribute::ByVal)) {
      bool aggregateIsPacked = false;
      if (StructType *STy = dyn_cast<StructType>(Ty))
        aggregateIsPacked = STy->isPacked();

      SmallVector<EVT, 16> VTs;
      SmallVector<uint64_t, 16> Offsets;
      ComputePTXValueVTs(*this, DL, Ty, VTs, &Offsets, 0);
      assert(VTs.size() > 0 && "Unexpected empty type.");
      auto VectorInfo =
          VectorizePTXValueVTs(VTs, Offsets, DL.getABITypeAlignment(Ty));

      SDValue Arg = getParamSymbol(DAG, idx, PtrVT);
      int VecIdx = -1; // Index of the first element of the current vector.
      for (unsigned parti = 0, parte = VTs.size(); parti != parte; ++parti) {
        if (VectorInfo[parti] & PVF_FIRST) {
          assert(VecIdx == -1 && "Orphaned vector.");
          VecIdx = parti;
        }

        // That's the last element of this store op.
        if (VectorInfo[parti] & PVF_LAST) {
          unsigned NumElts = parti - VecIdx + 1;
          EVT EltVT = VTs[parti];
          // i1 is loaded/stored as i8.
          EVT LoadVT = EltVT;
          if (EltVT == MVT::i1)
            LoadVT = MVT::i8;
          else if (EltVT == MVT::v2f16)
            // getLoad needs a vector type, but it can't handle
            // vectors which contain v2f16 elements. So we must load
            // using i32 here and then bitcast back.
            LoadVT = MVT::i32;

          EVT VecVT = EVT::getVectorVT(F->getContext(), LoadVT, NumElts);
          SDValue VecAddr =
              DAG.getNode(ISD::ADD, dl, PtrVT, Arg,
                          DAG.getConstant(Offsets[VecIdx], dl, PtrVT));
          Value *srcValue = Constant::getNullValue(PointerType::get(
              EltVT.getTypeForEVT(F->getContext()), ADDRESS_SPACE_PARAM));
          SDValue P =
              DAG.getLoad(VecVT, dl, Root, VecAddr,
                          MachinePointerInfo(srcValue), aggregateIsPacked,
                          MachineMemOperand::MODereferenceable |
                              MachineMemOperand::MOInvariant);
          if (P.getNode())
            P.getNode()->setIROrder(idx + 1);
          for (unsigned j = 0; j < NumElts; ++j) {
            SDValue Elt = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, LoadVT, P,
                                      DAG.getIntPtrConstant(j, dl));
            // We've loaded i1 as an i8 and now must truncate it back to i1
            if (EltVT == MVT::i1)
              Elt = DAG.getNode(ISD::TRUNCATE, dl, MVT::i1, Elt);
            // v2f16 was loaded as an i32. Now we must bitcast it back.
            else if (EltVT == MVT::v2f16)
              Elt = DAG.getNode(ISD::BITCAST, dl, MVT::v2f16, Elt);
            // Extend the element if necessary (e.g. an i8 is loaded
            // into an i16 register)
            if (Ins[InsIdx].VT.isInteger() &&
                Ins[InsIdx].VT.getSizeInBits() > LoadVT.getSizeInBits()) {
              unsigned Extend = Ins[InsIdx].Flags.isSExt() ? ISD::SIGN_EXTEND
                                                           : ISD::ZERO_EXTEND;
              Elt = DAG.getNode(Extend, dl, Ins[InsIdx].VT, Elt);
            }
            InVals.push_back(Elt);
          }

          // Reset vector tracking state.
          VecIdx = -1;
        }
        ++InsIdx;
      }
      if (VTs.size() > 0)
        --InsIdx;
      continue;
    }

    // Param has ByVal attribute
    // Return MoveParam(param symbol).
    // Ideally, the param symbol can be returned directly,
    // but when SDNode builder decides to use it in a CopyToReg(),
    // machine instruction fails because TargetExternalSymbol
    // (not lowered) is target dependent, and CopyToReg assumes
    // the source is lowered.
    EVT ObjectVT = getValueType(DL, Ty);
    assert(ObjectVT == Ins[InsIdx].VT &&
           "Ins type did not match function type");
    SDValue Arg = getParamSymbol(DAG, idx, PtrVT);
    SDValue p = DAG.getNode(NVPTXISD::MoveParam, dl, ObjectVT, Arg);
    if (p.getNode())
      p.getNode()->setIROrder(idx + 1);
    InVals.push_back(p);
  }

  // Clang will check explicit VarArg and issue error if any. However, Clang
  // will let code with
  // implicit var arg like f() pass. See bug 617733.
  // We treat this case as if the arg list is empty.
  // if (F.isVarArg()) {
  // assert(0 && "VarArg not supported yet!");
  //}

  if (!OutChains.empty())
    DAG.setRoot(DAG.getNode(ISD::TokenFactor, dl, MVT::Other, OutChains));

  return Chain;
}

SDValue
NVPTXTargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                                 bool isVarArg,
                                 const SmallVectorImpl<ISD::OutputArg> &Outs,
                                 const SmallVectorImpl<SDValue> &OutVals,
                                 const SDLoc &dl, SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  Type *RetTy = MF.getFunction().getReturnType();

  bool isABI = (STI.getSmVersion() >= 20);
  assert(isABI && "Non-ABI compilation is not supported");
  if (!isABI)
    return Chain;

  const DataLayout DL = DAG.getDataLayout();
  SmallVector<EVT, 16> VTs;
  SmallVector<uint64_t, 16> Offsets;
  ComputePTXValueVTs(*this, DL, RetTy, VTs, &Offsets);
  assert(VTs.size() == OutVals.size() && "Bad return value decomposition");

  auto VectorInfo = VectorizePTXValueVTs(
      VTs, Offsets, RetTy->isSized() ? DL.getABITypeAlignment(RetTy) : 1);

  // PTX Interoperability Guide 3.3(A): [Integer] Values shorter than
  // 32-bits are sign extended or zero extended, depending on whether
  // they are signed or unsigned types.
  bool ExtendIntegerRetVal =
      RetTy->isIntegerTy() && DL.getTypeAllocSizeInBits(RetTy) < 32;

  SmallVector<SDValue, 6> StoreOperands;
  for (unsigned i = 0, e = VTs.size(); i != e; ++i) {
    // New load/store. Record chain and offset operands.
    if (VectorInfo[i] & PVF_FIRST) {
      assert(StoreOperands.empty() && "Orphaned operand list.");
      StoreOperands.push_back(Chain);
      StoreOperands.push_back(DAG.getConstant(Offsets[i], dl, MVT::i32));
    }

    SDValue RetVal = OutVals[i];
    if (ExtendIntegerRetVal) {
      RetVal = DAG.getNode(Outs[i].Flags.isSExt() ? ISD::SIGN_EXTEND
                                                  : ISD::ZERO_EXTEND,
                           dl, MVT::i32, RetVal);
    } else if (RetVal.getValueSizeInBits() < 16) {
      // Use 16-bit registers for small load-stores as it's the
      // smallest general purpose register size supported by NVPTX.
      RetVal = DAG.getNode(ISD::ANY_EXTEND, dl, MVT::i16, RetVal);
    }

    // Record the value to return.
    StoreOperands.push_back(RetVal);

    // That's the last element of this store op.
    if (VectorInfo[i] & PVF_LAST) {
      NVPTXISD::NodeType Op;
      unsigned NumElts = StoreOperands.size() - 2;
      switch (NumElts) {
      case 1:
        Op = NVPTXISD::StoreRetval;
        break;
      case 2:
        Op = NVPTXISD::StoreRetvalV2;
        break;
      case 4:
        Op = NVPTXISD::StoreRetvalV4;
        break;
      default:
        llvm_unreachable("Invalid vector info.");
      }

      // Adjust type of load/store op if we've extended the scalar
      // return value.
      EVT TheStoreType = ExtendIntegerRetVal ? MVT::i32 : VTs[i];
      Chain = DAG.getMemIntrinsicNode(Op, dl, DAG.getVTList(MVT::Other),
                                      StoreOperands, TheStoreType,
                                      MachinePointerInfo(), /* Align */ 1,
                                      MachineMemOperand::MOStore);
      // Cleanup vector state.
      StoreOperands.clear();
    }
  }

  return DAG.getNode(NVPTXISD::RET_FLAG, dl, MVT::Other, Chain);
}

void NVPTXTargetLowering::LowerAsmOperandForConstraint(
    SDValue Op, std::string &Constraint, std::vector<SDValue> &Ops,
    SelectionDAG &DAG) const {
  if (Constraint.length() > 1)
    return;
  else
    TargetLowering::LowerAsmOperandForConstraint(Op, Constraint, Ops, DAG);
}

static unsigned getOpcForTextureInstr(unsigned Intrinsic) {
  switch (Intrinsic) {
  default:
    return 0;

  case Intrinsic::nvvm_tex_1d_v4f32_s32:
    return NVPTXISD::Tex1DFloatS32;
  case Intrinsic::nvvm_tex_1d_v4f32_f32:
    return NVPTXISD::Tex1DFloatFloat;
  case Intrinsic::nvvm_tex_1d_level_v4f32_f32:
    return NVPTXISD::Tex1DFloatFloatLevel;
  case Intrinsic::nvvm_tex_1d_grad_v4f32_f32:
    return NVPTXISD::Tex1DFloatFloatGrad;
  case Intrinsic::nvvm_tex_1d_v4s32_s32:
    return NVPTXISD::Tex1DS32S32;
  case Intrinsic::nvvm_tex_1d_v4s32_f32:
    return NVPTXISD::Tex1DS32Float;
  case Intrinsic::nvvm_tex_1d_level_v4s32_f32:
    return NVPTXISD::Tex1DS32FloatLevel;
  case Intrinsic::nvvm_tex_1d_grad_v4s32_f32:
    return NVPTXISD::Tex1DS32FloatGrad;
  case Intrinsic::nvvm_tex_1d_v4u32_s32:
    return NVPTXISD::Tex1DU32S32;
  case Intrinsic::nvvm_tex_1d_v4u32_f32:
    return NVPTXISD::Tex1DU32Float;
  case Intrinsic::nvvm_tex_1d_level_v4u32_f32:
    return NVPTXISD::Tex1DU32FloatLevel;
  case Intrinsic::nvvm_tex_1d_grad_v4u32_f32:
    return NVPTXISD::Tex1DU32FloatGrad;

  case Intrinsic::nvvm_tex_1d_array_v4f32_s32:
    return NVPTXISD::Tex1DArrayFloatS32;
  case Intrinsic::nvvm_tex_1d_array_v4f32_f32:
    return NVPTXISD::Tex1DArrayFloatFloat;
  case Intrinsic::nvvm_tex_1d_array_level_v4f32_f32:
    return NVPTXISD::Tex1DArrayFloatFloatLevel;
  case Intrinsic::nvvm_tex_1d_array_grad_v4f32_f32:
    return NVPTXISD::Tex1DArrayFloatFloatGrad;
  case Intrinsic::nvvm_tex_1d_array_v4s32_s32:
    return NVPTXISD::Tex1DArrayS32S32;
  case Intrinsic::nvvm_tex_1d_array_v4s32_f32:
    return NVPTXISD::Tex1DArrayS32Float;
  case Intrinsic::nvvm_tex_1d_array_level_v4s32_f32:
    return NVPTXISD::Tex1DArrayS32FloatLevel;
  case Intrinsic::nvvm_tex_1d_array_grad_v4s32_f32:
    return NVPTXISD::Tex1DArrayS32FloatGrad;
  case Intrinsic::nvvm_tex_1d_array_v4u32_s32:
    return NVPTXISD::Tex1DArrayU32S32;
  case Intrinsic::nvvm_tex_1d_array_v4u32_f32:
    return NVPTXISD::Tex1DArrayU32Float;
  case Intrinsic::nvvm_tex_1d_array_level_v4u32_f32:
    return NVPTXISD::Tex1DArrayU32FloatLevel;
  case Intrinsic::nvvm_tex_1d_array_grad_v4u32_f32:
    return NVPTXISD::Tex1DArrayU32FloatGrad;

  case Intrinsic::nvvm_tex_2d_v4f32_s32:
    return NVPTXISD::Tex2DFloatS32;
  case Intrinsic::nvvm_tex_2d_v4f32_f32:
    return NVPTXISD::Tex2DFloatFloat;
  case Intrinsic::nvvm_tex_2d_level_v4f32_f32:
    return NVPTXISD::Tex2DFloatFloatLevel;
  case Intrinsic::nvvm_tex_2d_grad_v4f32_f32:
    return NVPTXISD::Tex2DFloatFloatGrad;
  case Intrinsic::nvvm_tex_2d_v4s32_s32:
    return NVPTXISD::Tex2DS32S32;
  case Intrinsic::nvvm_tex_2d_v4s32_f32:
    return NVPTXISD::Tex2DS32Float;
  case Intrinsic::nvvm_tex_2d_level_v4s32_f32:
    return NVPTXISD::Tex2DS32FloatLevel;
  case Intrinsic::nvvm_tex_2d_grad_v4s32_f32:
    return NVPTXISD::Tex2DS32FloatGrad;
  case Intrinsic::nvvm_tex_2d_v4u32_s32:
    return NVPTXISD::Tex2DU32S32;
  case Intrinsic::nvvm_tex_2d_v4u32_f32:
    return NVPTXISD::Tex2DU32Float;
  case Intrinsic::nvvm_tex_2d_level_v4u32_f32:
    return NVPTXISD::Tex2DU32FloatLevel;
  case Intrinsic::nvvm_tex_2d_grad_v4u32_f32:
    return NVPTXISD::Tex2DU32FloatGrad;

  case Intrinsic::nvvm_tex_2d_array_v4f32_s32:
    return NVPTXISD::Tex2DArrayFloatS32;
  case Intrinsic::nvvm_tex_2d_array_v4f32_f32:
    return NVPTXISD::Tex2DArrayFloatFloat;
  case Intrinsic::nvvm_tex_2d_array_level_v4f32_f32:
    return NVPTXISD::Tex2DArrayFloatFloatLevel;
  case Intrinsic::nvvm_tex_2d_array_grad_v4f32_f32:
    return NVPTXISD::Tex2DArrayFloatFloatGrad;
  case Intrinsic::nvvm_tex_2d_array_v4s32_s32:
    return NVPTXISD::Tex2DArrayS32S32;
  case Intrinsic::nvvm_tex_2d_array_v4s32_f32:
    return NVPTXISD::Tex2DArrayS32Float;
  case Intrinsic::nvvm_tex_2d_array_level_v4s32_f32:
    return NVPTXISD::Tex2DArrayS32FloatLevel;
  case Intrinsic::nvvm_tex_2d_array_grad_v4s32_f32:
    return NVPTXISD::Tex2DArrayS32FloatGrad;
  case Intrinsic::nvvm_tex_2d_array_v4u32_s32:
    return NVPTXISD::Tex2DArrayU32S32;
  case Intrinsic::nvvm_tex_2d_array_v4u32_f32:
    return NVPTXISD::Tex2DArrayU32Float;
  case Intrinsic::nvvm_tex_2d_array_level_v4u32_f32:
    return NVPTXISD::Tex2DArrayU32FloatLevel;
  case Intrinsic::nvvm_tex_2d_array_grad_v4u32_f32:
    return NVPTXISD::Tex2DArrayU32FloatGrad;

  case Intrinsic::nvvm_tex_3d_v4f32_s32:
    return NVPTXISD::Tex3DFloatS32;
  case Intrinsic::nvvm_tex_3d_v4f32_f32:
    return NVPTXISD::Tex3DFloatFloat;
  case Intrinsic::nvvm_tex_3d_level_v4f32_f32:
    return NVPTXISD::Tex3DFloatFloatLevel;
  case Intrinsic::nvvm_tex_3d_grad_v4f32_f32:
    return NVPTXISD::Tex3DFloatFloatGrad;
  case Intrinsic::nvvm_tex_3d_v4s32_s32:
    return NVPTXISD::Tex3DS32S32;
  case Intrinsic::nvvm_tex_3d_v4s32_f32:
    return NVPTXISD::Tex3DS32Float;
  case Intrinsic::nvvm_tex_3d_level_v4s32_f32:
    return NVPTXISD::Tex3DS32FloatLevel;
  case Intrinsic::nvvm_tex_3d_grad_v4s32_f32:
    return NVPTXISD::Tex3DS32FloatGrad;
  case Intrinsic::nvvm_tex_3d_v4u32_s32:
    return NVPTXISD::Tex3DU32S32;
  case Intrinsic::nvvm_tex_3d_v4u32_f32:
    return NVPTXISD::Tex3DU32Float;
  case Intrinsic::nvvm_tex_3d_level_v4u32_f32:
    return NVPTXISD::Tex3DU32FloatLevel;
  case Intrinsic::nvvm_tex_3d_grad_v4u32_f32:
    return NVPTXISD::Tex3DU32FloatGrad;

  case Intrinsic::nvvm_tex_cube_v4f32_f32:
    return NVPTXISD::TexCubeFloatFloat;
  case Intrinsic::nvvm_tex_cube_level_v4f32_f32:
    return NVPTXISD::TexCubeFloatFloatLevel;
  case Intrinsic::nvvm_tex_cube_v4s32_f32:
    return NVPTXISD::TexCubeS32Float;
  case Intrinsic::nvvm_tex_cube_level_v4s32_f32:
    return NVPTXISD::TexCubeS32FloatLevel;
  case Intrinsic::nvvm_tex_cube_v4u32_f32:
    return NVPTXISD::TexCubeU32Float;
  case Intrinsic::nvvm_tex_cube_level_v4u32_f32:
    return NVPTXISD::TexCubeU32FloatLevel;

  case Intrinsic::nvvm_tex_cube_array_v4f32_f32:
    return NVPTXISD::TexCubeArrayFloatFloat;
  case Intrinsic::nvvm_tex_cube_array_level_v4f32_f32:
    return NVPTXISD::TexCubeArrayFloatFloatLevel;
  case Intrinsic::nvvm_tex_cube_array_v4s32_f32:
    return NVPTXISD::TexCubeArrayS32Float;
  case Intrinsic::nvvm_tex_cube_array_level_v4s32_f32:
    return NVPTXISD::TexCubeArrayS32FloatLevel;
  case Intrinsic::nvvm_tex_cube_array_v4u32_f32:
    return NVPTXISD::TexCubeArrayU32Float;
  case Intrinsic::nvvm_tex_cube_array_level_v4u32_f32:
    return NVPTXISD::TexCubeArrayU32FloatLevel;

  case Intrinsic::nvvm_tld4_r_2d_v4f32_f32:
    return NVPTXISD::Tld4R2DFloatFloat;
  case Intrinsic::nvvm_tld4_g_2d_v4f32_f32:
    return NVPTXISD::Tld4G2DFloatFloat;
  case Intrinsic::nvvm_tld4_b_2d_v4f32_f32:
    return NVPTXISD::Tld4B2DFloatFloat;
  case Intrinsic::nvvm_tld4_a_2d_v4f32_f32:
    return NVPTXISD::Tld4A2DFloatFloat;
  case Intrinsic::nvvm_tld4_r_2d_v4s32_f32:
    return NVPTXISD::Tld4R2DS64Float;
  case Intrinsic::nvvm_tld4_g_2d_v4s32_f32:
    return NVPTXISD::Tld4G2DS64Float;
  case Intrinsic::nvvm_tld4_b_2d_v4s32_f32:
    return NVPTXISD::Tld4B2DS64Float;
  case Intrinsic::nvvm_tld4_a_2d_v4s32_f32:
    return NVPTXISD::Tld4A2DS64Float;
  case Intrinsic::nvvm_tld4_r_2d_v4u32_f32:
    return NVPTXISD::Tld4R2DU64Float;
  case Intrinsic::nvvm_tld4_g_2d_v4u32_f32:
    return NVPTXISD::Tld4G2DU64Float;
  case Intrinsic::nvvm_tld4_b_2d_v4u32_f32:
    return NVPTXISD::Tld4B2DU64Float;
  case Intrinsic::nvvm_tld4_a_2d_v4u32_f32:
    return NVPTXISD::Tld4A2DU64Float;

  case Intrinsic::nvvm_tex_unified_1d_v4f32_s32:
    return NVPTXISD::TexUnified1DFloatS32;
  case Intrinsic::nvvm_tex_unified_1d_v4f32_f32:
    return NVPTXISD::TexUnified1DFloatFloat;
  case Intrinsic::nvvm_tex_unified_1d_level_v4f32_f32:
    return NVPTXISD::TexUnified1DFloatFloatLevel;
  case Intrinsic::nvvm_tex_unified_1d_grad_v4f32_f32:
    return NVPTXISD::TexUnified1DFloatFloatGrad;
  case Intrinsic::nvvm_tex_unified_1d_v4s32_s32:
    return NVPTXISD::TexUnified1DS32S32;
  case Intrinsic::nvvm_tex_unified_1d_v4s32_f32:
    return NVPTXISD::TexUnified1DS32Float;
  case Intrinsic::nvvm_tex_unified_1d_level_v4s32_f32:
    return NVPTXISD::TexUnified1DS32FloatLevel;
  case Intrinsic::nvvm_tex_unified_1d_grad_v4s32_f32:
    return NVPTXISD::TexUnified1DS32FloatGrad;
  case Intrinsic::nvvm_tex_unified_1d_v4u32_s32:
    return NVPTXISD::TexUnified1DU32S32;
  case Intrinsic::nvvm_tex_unified_1d_v4u32_f32:
    return NVPTXISD::TexUnified1DU32Float;
  case Intrinsic::nvvm_tex_unified_1d_level_v4u32_f32:
    return NVPTXISD::TexUnified1DU32FloatLevel;
  case Intrinsic::nvvm_tex_unified_1d_grad_v4u32_f32:
    return NVPTXISD::TexUnified1DU32FloatGrad;

  case Intrinsic::nvvm_tex_unified_1d_array_v4f32_s32:
    return NVPTXISD::TexUnified1DArrayFloatS32;
  case Intrinsic::nvvm_tex_unified_1d_array_v4f32_f32:
    return NVPTXISD::TexUnified1DArrayFloatFloat;
  case Intrinsic::nvvm_tex_unified_1d_array_level_v4f32_f32:
    return NVPTXISD::TexUnified1DArrayFloatFloatLevel;
  case Intrinsic::nvvm_tex_unified_1d_array_grad_v4f32_f32:
    return NVPTXISD::TexUnified1DArrayFloatFloatGrad;
  case Intrinsic::nvvm_tex_unified_1d_array_v4s32_s32:
    return NVPTXISD::TexUnified1DArrayS32S32;
  case Intrinsic::nvvm_tex_unified_1d_array_v4s32_f32:
    return NVPTXISD::TexUnified1DArrayS32Float;
  case Intrinsic::nvvm_tex_unified_1d_array_level_v4s32_f32:
    return NVPTXISD::TexUnified1DArrayS32FloatLevel;
  case Intrinsic::nvvm_tex_unified_1d_array_grad_v4s32_f32:
    return NVPTXISD::TexUnified1DArrayS32FloatGrad;
  case Intrinsic::nvvm_tex_unified_1d_array_v4u32_s32:
    return NVPTXISD::TexUnified1DArrayU32S32;
  case Intrinsic::nvvm_tex_unified_1d_array_v4u32_f32:
    return NVPTXISD::TexUnified1DArrayU32Float;
  case Intrinsic::nvvm_tex_unified_1d_array_level_v4u32_f32:
    return NVPTXISD::TexUnified1DArrayU32FloatLevel;
  case Intrinsic::nvvm_tex_unified_1d_array_grad_v4u32_f32:
    return NVPTXISD::TexUnified1DArrayU32FloatGrad;

  case Intrinsic::nvvm_tex_unified_2d_v4f32_s32:
    return NVPTXISD::TexUnified2DFloatS32;
  case Intrinsic::nvvm_tex_unified_2d_v4f32_f32:
    return NVPTXISD::TexUnified2DFloatFloat;
  case Intrinsic::nvvm_tex_unified_2d_level_v4f32_f32:
    return NVPTXISD::TexUnified2DFloatFloatLevel;
  case Intrinsic::nvvm_tex_unified_2d_grad_v4f32_f32:
    return NVPTXISD::TexUnified2DFloatFloatGrad;
  case Intrinsic::nvvm_tex_unified_2d_v4s32_s32:
    return NVPTXISD::TexUnified2DS32S32;
  case Intrinsic::nvvm_tex_unified_2d_v4s32_f32:
    return NVPTXISD::TexUnified2DS32Float;
  case Intrinsic::nvvm_tex_unified_2d_level_v4s32_f32:
    return NVPTXISD::TexUnified2DS32FloatLevel;
  case Intrinsic::nvvm_tex_unified_2d_grad_v4s32_f32:
    return NVPTXISD::TexUnified2DS32FloatGrad;
  case Intrinsic::nvvm_tex_unified_2d_v4u32_s32:
    return NVPTXISD::TexUnified2DU32S32;
  case Intrinsic::nvvm_tex_unified_2d_v4u32_f32:
    return NVPTXISD::TexUnified2DU32Float;
  case Intrinsic::nvvm_tex_unified_2d_level_v4u32_f32:
    return NVPTXISD::TexUnified2DU32FloatLevel;
  case Intrinsic::nvvm_tex_unified_2d_grad_v4u32_f32:
    return NVPTXISD::TexUnified2DU32FloatGrad;

  case Intrinsic::nvvm_tex_unified_2d_array_v4f32_s32:
    return NVPTXISD::TexUnified2DArrayFloatS32;
  case Intrinsic::nvvm_tex_unified_2d_array_v4f32_f32:
    return NVPTXISD::TexUnified2DArrayFloatFloat;
  case Intrinsic::nvvm_tex_unified_2d_array_level_v4f32_f32:
    return NVPTXISD::TexUnified2DArrayFloatFloatLevel;
  case Intrinsic::nvvm_tex_unified_2d_array_grad_v4f32_f32:
    return NVPTXISD::TexUnified2DArrayFloatFloatGrad;
  case Intrinsic::nvvm_tex_unified_2d_array_v4s32_s32:
    return NVPTXISD::TexUnified2DArrayS32S32;
  case Intrinsic::nvvm_tex_unified_2d_array_v4s32_f32:
    return NVPTXISD::TexUnified2DArrayS32Float;
  case Intrinsic::nvvm_tex_unified_2d_array_level_v4s32_f32:
    return NVPTXISD::TexUnified2DArrayS32FloatLevel;
  case Intrinsic::nvvm_tex_unified_2d_array_grad_v4s32_f32:
    return NVPTXISD::TexUnified2DArrayS32FloatGrad;
  case Intrinsic::nvvm_tex_unified_2d_array_v4u32_s32:
    return NVPTXISD::TexUnified2DArrayU32S32;
  case Intrinsic::nvvm_tex_unified_2d_array_v4u32_f32:
    return NVPTXISD::TexUnified2DArrayU32Float;
  case Intrinsic::nvvm_tex_unified_2d_array_level_v4u32_f32:
    return NVPTXISD::TexUnified2DArrayU32FloatLevel;
  case Intrinsic::nvvm_tex_unified_2d_array_grad_v4u32_f32:
    return NVPTXISD::TexUnified2DArrayU32FloatGrad;

  case Intrinsic::nvvm_tex_unified_3d_v4f32_s32:
    return NVPTXISD::TexUnified3DFloatS32;
  case Intrinsic::nvvm_tex_unified_3d_v4f32_f32:
    return NVPTXISD::TexUnified3DFloatFloat;
  case Intrinsic::nvvm_tex_unified_3d_level_v4f32_f32:
    return NVPTXISD::TexUnified3DFloatFloatLevel;
  case Intrinsic::nvvm_tex_unified_3d_grad_v4f32_f32:
    return NVPTXISD::TexUnified3DFloatFloatGrad;
  case Intrinsic::nvvm_tex_unified_3d_v4s32_s32:
    return NVPTXISD::TexUnified3DS32S32;
  case Intrinsic::nvvm_tex_unified_3d_v4s32_f32:
    return NVPTXISD::TexUnified3DS32Float;
  case Intrinsic::nvvm_tex_unified_3d_level_v4s32_f32:
    return NVPTXISD::TexUnified3DS32FloatLevel;
  case Intrinsic::nvvm_tex_unified_3d_grad_v4s32_f32:
    return NVPTXISD::TexUnified3DS32FloatGrad;
  case Intrinsic::nvvm_tex_unified_3d_v4u32_s32:
    return NVPTXISD::TexUnified3DU32S32;
  case Intrinsic::nvvm_tex_unified_3d_v4u32_f32:
    return NVPTXISD::TexUnified3DU32Float;
  case Intrinsic::nvvm_tex_unified_3d_level_v4u32_f32:
    return NVPTXISD::TexUnified3DU32FloatLevel;
  case Intrinsic::nvvm_tex_unified_3d_grad_v4u32_f32:
    return NVPTXISD::TexUnified3DU32FloatGrad;

  case Intrinsic::nvvm_tex_unified_cube_v4f32_f32:
    return NVPTXISD::TexUnifiedCubeFloatFloat;
  case Intrinsic::nvvm_tex_unified_cube_level_v4f32_f32:
    return NVPTXISD::TexUnifiedCubeFloatFloatLevel;
  case Intrinsic::nvvm_tex_unified_cube_v4s32_f32:
    return NVPTXISD::TexUnifiedCubeS32Float;
  case Intrinsic::nvvm_tex_unified_cube_level_v4s32_f32:
    return NVPTXISD::TexUnifiedCubeS32FloatLevel;
  case Intrinsic::nvvm_tex_unified_cube_v4u32_f32:
    return NVPTXISD::TexUnifiedCubeU32Float;
  case Intrinsic::nvvm_tex_unified_cube_level_v4u32_f32:
    return NVPTXISD::TexUnifiedCubeU32FloatLevel;

  case Intrinsic::nvvm_tex_unified_cube_array_v4f32_f32:
    return NVPTXISD::TexUnifiedCubeArrayFloatFloat;
  case Intrinsic::nvvm_tex_unified_cube_array_level_v4f32_f32:
    return NVPTXISD::TexUnifiedCubeArrayFloatFloatLevel;
  case Intrinsic::nvvm_tex_unified_cube_array_v4s32_f32:
    return NVPTXISD::TexUnifiedCubeArrayS32Float;
  case Intrinsic::nvvm_tex_unified_cube_array_level_v4s32_f32:
    return NVPTXISD::TexUnifiedCubeArrayS32FloatLevel;
  case Intrinsic::nvvm_tex_unified_cube_array_v4u32_f32:
    return NVPTXISD::TexUnifiedCubeArrayU32Float;
  case Intrinsic::nvvm_tex_unified_cube_array_level_v4u32_f32:
    return NVPTXISD::TexUnifiedCubeArrayU32FloatLevel;

  case Intrinsic::nvvm_tld4_unified_r_2d_v4f32_f32:
    return NVPTXISD::Tld4UnifiedR2DFloatFloat;
  case Intrinsic::nvvm_tld4_unified_g_2d_v4f32_f32:
    return NVPTXISD::Tld4UnifiedG2DFloatFloat;
  case Intrinsic::nvvm_tld4_unified_b_2d_v4f32_f32:
    return NVPTXISD::Tld4UnifiedB2DFloatFloat;
  case Intrinsic::nvvm_tld4_unified_a_2d_v4f32_f32:
    return NVPTXISD::Tld4UnifiedA2DFloatFloat;
  case Intrinsic::nvvm_tld4_unified_r_2d_v4s32_f32:
    return NVPTXISD::Tld4UnifiedR2DS64Float;
  case Intrinsic::nvvm_tld4_unified_g_2d_v4s32_f32:
    return NVPTXISD::Tld4UnifiedG2DS64Float;
  case Intrinsic::nvvm_tld4_unified_b_2d_v4s32_f32:
    return NVPTXISD::Tld4UnifiedB2DS64Float;
  case Intrinsic::nvvm_tld4_unified_a_2d_v4s32_f32:
    return NVPTXISD::Tld4UnifiedA2DS64Float;
  case Intrinsic::nvvm_tld4_unified_r_2d_v4u32_f32:
    return NVPTXISD::Tld4UnifiedR2DU64Float;
  case Intrinsic::nvvm_tld4_unified_g_2d_v4u32_f32:
    return NVPTXISD::Tld4UnifiedG2DU64Float;
  case Intrinsic::nvvm_tld4_unified_b_2d_v4u32_f32:
    return NVPTXISD::Tld4UnifiedB2DU64Float;
  case Intrinsic::nvvm_tld4_unified_a_2d_v4u32_f32:
    return NVPTXISD::Tld4UnifiedA2DU64Float;
  }
}

static unsigned getOpcForSurfaceInstr(unsigned Intrinsic) {
  switch (Intrinsic) {
  default:
    return 0;
  case Intrinsic::nvvm_suld_1d_i8_clamp:
    return NVPTXISD::Suld1DI8Clamp;
  case Intrinsic::nvvm_suld_1d_i16_clamp:
    return NVPTXISD::Suld1DI16Clamp;
  case Intrinsic::nvvm_suld_1d_i32_clamp:
    return NVPTXISD::Suld1DI32Clamp;
  case Intrinsic::nvvm_suld_1d_i64_clamp:
    return NVPTXISD::Suld1DI64Clamp;
  case Intrinsic::nvvm_suld_1d_v2i8_clamp:
    return NVPTXISD::Suld1DV2I8Clamp;
  case Intrinsic::nvvm_suld_1d_v2i16_clamp:
    return NVPTXISD::Suld1DV2I16Clamp;
  case Intrinsic::nvvm_suld_1d_v2i32_clamp:
    return NVPTXISD::Suld1DV2I32Clamp;
  case Intrinsic::nvvm_suld_1d_v2i64_clamp:
    return NVPTXISD::Suld1DV2I64Clamp;
  case Intrinsic::nvvm_suld_1d_v4i8_clamp:
    return NVPTXISD::Suld1DV4I8Clamp;
  case Intrinsic::nvvm_suld_1d_v4i16_clamp:
    return NVPTXISD::Suld1DV4I16Clamp;
  case Intrinsic::nvvm_suld_1d_v4i32_clamp:
    return NVPTXISD::Suld1DV4I32Clamp;
  case Intrinsic::nvvm_suld_1d_array_i8_clamp:
    return NVPTXISD::Suld1DArrayI8Clamp;
  case Intrinsic::nvvm_suld_1d_array_i16_clamp:
    return NVPTXISD::Suld1DArrayI16Clamp;
  case Intrinsic::nvvm_suld_1d_array_i32_clamp:
    return NVPTXISD::Suld1DArrayI32Clamp;
  case Intrinsic::nvvm_suld_1d_array_i64_clamp:
    return NVPTXISD::Suld1DArrayI64Clamp;
  case Intrinsic::nvvm_suld_1d_array_v2i8_clamp:
    return NVPTXISD::Suld1DArrayV2I8Clamp;
  case Intrinsic::nvvm_suld_1d_array_v2i16_clamp:
    return NVPTXISD::Suld1DArrayV2I16Clamp;
  case Intrinsic::nvvm_suld_1d_array_v2i32_clamp:
    return NVPTXISD::Suld1DArrayV2I32Clamp;
  case Intrinsic::nvvm_suld_1d_array_v2i64_clamp:
    return NVPTXISD::Suld1DArrayV2I64Clamp;
  case Intrinsic::nvvm_suld_1d_array_v4i8_clamp:
    return NVPTXISD::Suld1DArrayV4I8Clamp;
  case Intrinsic::nvvm_suld_1d_array_v4i16_clamp:
    return NVPTXISD::Suld1DArrayV4I16Clamp;
  case Intrinsic::nvvm_suld_1d_array_v4i32_clamp:
    return NVPTXISD::Suld1DArrayV4I32Clamp;
  case Intrinsic::nvvm_suld_2d_i8_clamp:
    return NVPTXISD::Suld2DI8Clamp;
  case Intrinsic::nvvm_suld_2d_i16_clamp:
    return NVPTXISD::Suld2DI16Clamp;
  case Intrinsic::nvvm_suld_2d_i32_clamp:
    return NVPTXISD::Suld2DI32Clamp;
  case Intrinsic::nvvm_suld_2d_i64_clamp:
    return NVPTXISD::Suld2DI64Clamp;
  case Intrinsic::nvvm_suld_2d_v2i8_clamp:
    return NVPTXISD::Suld2DV2I8Clamp;
  case Intrinsic::nvvm_suld_2d_v2i16_clamp:
    return NVPTXISD::Suld2DV2I16Clamp;
  case Intrinsic::nvvm_suld_2d_v2i32_clamp:
    return NVPTXISD::Suld2DV2I32Clamp;
  case Intrinsic::nvvm_suld_2d_v2i64_clamp:
    return NVPTXISD::Suld2DV2I64Clamp;
  case Intrinsic::nvvm_suld_2d_v4i8_clamp:
    return NVPTXISD::Suld2DV4I8Clamp;
  case Intrinsic::nvvm_suld_2d_v4i16_clamp:
    return NVPTXISD::Suld2DV4I16Clamp;
  case Intrinsic::nvvm_suld_2d_v4i32_clamp:
    return NVPTXISD::Suld2DV4I32Clamp;
  case Intrinsic::nvvm_suld_2d_array_i8_clamp:
    return NVPTXISD::Suld2DArrayI8Clamp;
  case Intrinsic::nvvm_suld_2d_array_i16_clamp:
    return NVPTXISD::Suld2DArrayI16Clamp;
  case Intrinsic::nvvm_suld_2d_array_i32_clamp:
    return NVPTXISD::Suld2DArrayI32Clamp;
  case Intrinsic::nvvm_suld_2d_array_i64_clamp:
    return NVPTXISD::Suld2DArrayI64Clamp;
  case Intrinsic::nvvm_suld_2d_array_v2i8_clamp:
    return NVPTXISD::Suld2DArrayV2I8Clamp;
  case Intrinsic::nvvm_suld_2d_array_v2i16_clamp:
    return NVPTXISD::Suld2DArrayV2I16Clamp;
  case Intrinsic::nvvm_suld_2d_array_v2i32_clamp:
    return NVPTXISD::Suld2DArrayV2I32Clamp;
  case Intrinsic::nvvm_suld_2d_array_v2i64_clamp:
    return NVPTXISD::Suld2DArrayV2I64Clamp;
  case Intrinsic::nvvm_suld_2d_array_v4i8_clamp:
    return NVPTXISD::Suld2DArrayV4I8Clamp;
  case Intrinsic::nvvm_suld_2d_array_v4i16_clamp:
    return NVPTXISD::Suld2DArrayV4I16Clamp;
  case Intrinsic::nvvm_suld_2d_array_v4i32_clamp:
    return NVPTXISD::Suld2DArrayV4I32Clamp;
  case Intrinsic::nvvm_suld_3d_i8_clamp:
    return NVPTXISD::Suld3DI8Clamp;
  case Intrinsic::nvvm_suld_3d_i16_clamp:
    return NVPTXISD::Suld3DI16Clamp;
  case Intrinsic::nvvm_suld_3d_i32_clamp:
    return NVPTXISD::Suld3DI32Clamp;
  case Intrinsic::nvvm_suld_3d_i64_clamp:
    return NVPTXISD::Suld3DI64Clamp;
  case Intrinsic::nvvm_suld_3d_v2i8_clamp:
    return NVPTXISD::Suld3DV2I8Clamp;
  case Intrinsic::nvvm_suld_3d_v2i16_clamp:
    return NVPTXISD::Suld3DV2I16Clamp;
  case Intrinsic::nvvm_suld_3d_v2i32_clamp:
    return NVPTXISD::Suld3DV2I32Clamp;
  case Intrinsic::nvvm_suld_3d_v2i64_clamp:
    return NVPTXISD::Suld3DV2I64Clamp;
  case Intrinsic::nvvm_suld_3d_v4i8_clamp:
    return NVPTXISD::Suld3DV4I8Clamp;
  case Intrinsic::nvvm_suld_3d_v4i16_clamp:
    return NVPTXISD::Suld3DV4I16Clamp;
  case Intrinsic::nvvm_suld_3d_v4i32_clamp:
    return NVPTXISD::Suld3DV4I32Clamp;
  case Intrinsic::nvvm_suld_1d_i8_trap:
    return NVPTXISD::Suld1DI8Trap;
  case Intrinsic::nvvm_suld_1d_i16_trap:
    return NVPTXISD::Suld1DI16Trap;
  case Intrinsic::nvvm_suld_1d_i32_trap:
    return NVPTXISD::Suld1DI32Trap;
  case Intrinsic::nvvm_suld_1d_i64_trap:
    return NVPTXISD::Suld1DI64Trap;
  case Intrinsic::nvvm_suld_1d_v2i8_trap:
    return NVPTXISD::Suld1DV2I8Trap;
  case Intrinsic::nvvm_suld_1d_v2i16_trap:
    return NVPTXISD::Suld1DV2I16Trap;
  case Intrinsic::nvvm_suld_1d_v2i32_trap:
    return NVPTXISD::Suld1DV2I32Trap;
  case Intrinsic::nvvm_suld_1d_v2i64_trap:
    return NVPTXISD::Suld1DV2I64Trap;
  case Intrinsic::nvvm_suld_1d_v4i8_trap:
    return NVPTXISD::Suld1DV4I8Trap;
  case Intrinsic::nvvm_suld_1d_v4i16_trap:
    return NVPTXISD::Suld1DV4I16Trap;
  case Intrinsic::nvvm_suld_1d_v4i32_trap:
    return NVPTXISD::Suld1DV4I32Trap;
  case Intrinsic::nvvm_suld_1d_array_i8_trap:
    return NVPTXISD::Suld1DArrayI8Trap;
  case Intrinsic::nvvm_suld_1d_array_i16_trap:
    return NVPTXISD::Suld1DArrayI16Trap;
  case Intrinsic::nvvm_suld_1d_array_i32_trap:
    return NVPTXISD::Suld1DArrayI32Trap;
  case Intrinsic::nvvm_suld_1d_array_i64_trap:
    return NVPTXISD::Suld1DArrayI64Trap;
  case Intrinsic::nvvm_suld_1d_array_v2i8_trap:
    return NVPTXISD::Suld1DArrayV2I8Trap;
  case Intrinsic::nvvm_suld_1d_array_v2i16_trap:
    return NVPTXISD::Suld1DArrayV2I16Trap;
  case Intrinsic::nvvm_suld_1d_array_v2i32_trap:
    return NVPTXISD::Suld1DArrayV2I32Trap;
  case Intrinsic::nvvm_suld_1d_array_v2i64_trap:
    return NVPTXISD::Suld1DArrayV2I64Trap;
  case Intrinsic::nvvm_suld_1d_array_v4i8_trap:
    return NVPTXISD::Suld1DArrayV4I8Trap;
  case Intrinsic::nvvm_suld_1d_array_v4i16_trap:
    return NVPTXISD::Suld1DArrayV4I16Trap;
  case Intrinsic::nvvm_suld_1d_array_v4i32_trap:
    return NVPTXISD::Suld1DArrayV4I32Trap;
  case Intrinsic::nvvm_suld_2d_i8_trap:
    return NVPTXISD::Suld2DI8Trap;
  case Intrinsic::nvvm_suld_2d_i16_trap:
    return NVPTXISD::Suld2DI16Trap;
  case Intrinsic::nvvm_suld_2d_i32_trap:
    return NVPTXISD::Suld2DI32Trap;
  case Intrinsic::nvvm_suld_2d_i64_trap:
    return NVPTXISD::Suld2DI64Trap;
  case Intrinsic::nvvm_suld_2d_v2i8_trap:
    return NVPTXISD::Suld2DV2I8Trap;
  case Intrinsic::nvvm_suld_2d_v2i16_trap:
    return NVPTXISD::Suld2DV2I16Trap;
  case Intrinsic::nvvm_suld_2d_v2i32_trap:
    return NVPTXISD::Suld2DV2I32Trap;
  case Intrinsic::nvvm_suld_2d_v2i64_trap:
    return NVPTXISD::Suld2DV2I64Trap;
  case Intrinsic::nvvm_suld_2d_v4i8_trap:
    return NVPTXISD::Suld2DV4I8Trap;
  case Intrinsic::nvvm_suld_2d_v4i16_trap:
    return NVPTXISD::Suld2DV4I16Trap;
  case Intrinsic::nvvm_suld_2d_v4i32_trap:
    return NVPTXISD::Suld2DV4I32Trap;
  case Intrinsic::nvvm_suld_2d_array_i8_trap:
    return NVPTXISD::Suld2DArrayI8Trap;
  case Intrinsic::nvvm_suld_2d_array_i16_trap:
    return NVPTXISD::Suld2DArrayI16Trap;
  case Intrinsic::nvvm_suld_2d_array_i32_trap:
    return NVPTXISD::Suld2DArrayI32Trap;
  case Intrinsic::nvvm_suld_2d_array_i64_trap:
    return NVPTXISD::Suld2DArrayI64Trap;
  case Intrinsic::nvvm_suld_2d_array_v2i8_trap:
    return NVPTXISD::Suld2DArrayV2I8Trap;
  case Intrinsic::nvvm_suld_2d_array_v2i16_trap:
    return NVPTXISD::Suld2DArrayV2I16Trap;
  case Intrinsic::nvvm_suld_2d_array_v2i32_trap:
    return NVPTXISD::Suld2DArrayV2I32Trap;
  case Intrinsic::nvvm_suld_2d_array_v2i64_trap:
    return NVPTXISD::Suld2DArrayV2I64Trap;
  case Intrinsic::nvvm_suld_2d_array_v4i8_trap:
    return NVPTXISD::Suld2DArrayV4I8Trap;
  case Intrinsic::nvvm_suld_2d_array_v4i16_trap:
    return NVPTXISD::Suld2DArrayV4I16Trap;
  case Intrinsic::nvvm_suld_2d_array_v4i32_trap:
    return NVPTXISD::Suld2DArrayV4I32Trap;
  case Intrinsic::nvvm_suld_3d_i8_trap:
    return NVPTXISD::Suld3DI8Trap;
  case Intrinsic::nvvm_suld_3d_i16_trap:
    return NVPTXISD::Suld3DI16Trap;
  case Intrinsic::nvvm_suld_3d_i32_trap:
    return NVPTXISD::Suld3DI32Trap;
  case Intrinsic::nvvm_suld_3d_i64_trap:
    return NVPTXISD::Suld3DI64Trap;
  case Intrinsic::nvvm_suld_3d_v2i8_trap:
    return NVPTXISD::Suld3DV2I8Trap;
  case Intrinsic::nvvm_suld_3d_v2i16_trap:
    return NVPTXISD::Suld3DV2I16Trap;
  case Intrinsic::nvvm_suld_3d_v2i32_trap:
    return NVPTXISD::Suld3DV2I32Trap;
  case Intrinsic::nvvm_suld_3d_v2i64_trap:
    return NVPTXISD::Suld3DV2I64Trap;
  case Intrinsic::nvvm_suld_3d_v4i8_trap:
    return NVPTXISD::Suld3DV4I8Trap;
  case Intrinsic::nvvm_suld_3d_v4i16_trap:
    return NVPTXISD::Suld3DV4I16Trap;
  case Intrinsic::nvvm_suld_3d_v4i32_trap:
    return NVPTXISD::Suld3DV4I32Trap;
  case Intrinsic::nvvm_suld_1d_i8_zero:
    return NVPTXISD::Suld1DI8Zero;
  case Intrinsic::nvvm_suld_1d_i16_zero:
    return NVPTXISD::Suld1DI16Zero;
  case Intrinsic::nvvm_suld_1d_i32_zero:
    return NVPTXISD::Suld1DI32Zero;
  case Intrinsic::nvvm_suld_1d_i64_zero:
    return NVPTXISD::Suld1DI64Zero;
  case Intrinsic::nvvm_suld_1d_v2i8_zero:
    return NVPTXISD::Suld1DV2I8Zero;
  case Intrinsic::nvvm_suld_1d_v2i16_zero:
    return NVPTXISD::Suld1DV2I16Zero;
  case Intrinsic::nvvm_suld_1d_v2i32_zero:
    return NVPTXISD::Suld1DV2I32Zero;
  case Intrinsic::nvvm_suld_1d_v2i64_zero:
    return NVPTXISD::Suld1DV2I64Zero;
  case Intrinsic::nvvm_suld_1d_v4i8_zero:
    return NVPTXISD::Suld1DV4I8Zero;
  case Intrinsic::nvvm_suld_1d_v4i16_zero:
    return NVPTXISD::Suld1DV4I16Zero;
  case Intrinsic::nvvm_suld_1d_v4i32_zero:
    return NVPTXISD::Suld1DV4I32Zero;
  case Intrinsic::nvvm_suld_1d_array_i8_zero:
    return NVPTXISD::Suld1DArrayI8Zero;
  case Intrinsic::nvvm_suld_1d_array_i16_zero:
    return NVPTXISD::Suld1DArrayI16Zero;
  case Intrinsic::nvvm_suld_1d_array_i32_zero:
    return NVPTXISD::Suld1DArrayI32Zero;
  case Intrinsic::nvvm_suld_1d_array_i64_zero:
    return NVPTXISD::Suld1DArrayI64Zero;
  case Intrinsic::nvvm_suld_1d_array_v2i8_zero:
    return NVPTXISD::Suld1DArrayV2I8Zero;
  case Intrinsic::nvvm_suld_1d_array_v2i16_zero:
    return NVPTXISD::Suld1DArrayV2I16Zero;
  case Intrinsic::nvvm_suld_1d_array_v2i32_zero:
    return NVPTXISD::Suld1DArrayV2I32Zero;
  case Intrinsic::nvvm_suld_1d_array_v2i64_zero:
    return NVPTXISD::Suld1DArrayV2I64Zero;
  case Intrinsic::nvvm_suld_1d_array_v4i8_zero:
    return NVPTXISD::Suld1DArrayV4I8Zero;
  case Intrinsic::nvvm_suld_1d_array_v4i16_zero:
    return NVPTXISD::Suld1DArrayV4I16Zero;
  case Intrinsic::nvvm_suld_1d_array_v4i32_zero:
    return NVPTXISD::Suld1DArrayV4I32Zero;
  case Intrinsic::nvvm_suld_2d_i8_zero:
    return NVPTXISD::Suld2DI8Zero;
  case Intrinsic::nvvm_suld_2d_i16_zero:
    return NVPTXISD::Suld2DI16Zero;
  case Intrinsic::nvvm_suld_2d_i32_zero:
    return NVPTXISD::Suld2DI32Zero;
  case Intrinsic::nvvm_suld_2d_i64_zero:
    return NVPTXISD::Suld2DI64Zero;
  case Intrinsic::nvvm_suld_2d_v2i8_zero:
    return NVPTXISD::Suld2DV2I8Zero;
  case Intrinsic::nvvm_suld_2d_v2i16_zero:
    return NVPTXISD::Suld2DV2I16Zero;
  case Intrinsic::nvvm_suld_2d_v2i32_zero:
    return NVPTXISD::Suld2DV2I32Zero;
  case Intrinsic::nvvm_suld_2d_v2i64_zero:
    return NVPTXISD::Suld2DV2I64Zero;
  case Intrinsic::nvvm_suld_2d_v4i8_zero:
    return NVPTXISD::Suld2DV4I8Zero;
  case Intrinsic::nvvm_suld_2d_v4i16_zero:
    return NVPTXISD::Suld2DV4I16Zero;
  case Intrinsic::nvvm_suld_2d_v4i32_zero:
    return NVPTXISD::Suld2DV4I32Zero;
  case Intrinsic::nvvm_suld_2d_array_i8_zero:
    return NVPTXISD::Suld2DArrayI8Zero;
  case Intrinsic::nvvm_suld_2d_array_i16_zero:
    return NVPTXISD::Suld2DArrayI16Zero;
  case Intrinsic::nvvm_suld_2d_array_i32_zero:
    return NVPTXISD::Suld2DArrayI32Zero;
  case Intrinsic::nvvm_suld_2d_array_i64_zero:
    return NVPTXISD::Suld2DArrayI64Zero;
  case Intrinsic::nvvm_suld_2d_array_v2i8_zero:
    return NVPTXISD::Suld2DArrayV2I8Zero;
  case Intrinsic::nvvm_suld_2d_array_v2i16_zero:
    return NVPTXISD::Suld2DArrayV2I16Zero;
  case Intrinsic::nvvm_suld_2d_array_v2i32_zero:
    return NVPTXISD::Suld2DArrayV2I32Zero;
  case Intrinsic::nvvm_suld_2d_array_v2i64_zero:
    return NVPTXISD::Suld2DArrayV2I64Zero;
  case Intrinsic::nvvm_suld_2d_array_v4i8_zero:
    return NVPTXISD::Suld2DArrayV4I8Zero;
  case Intrinsic::nvvm_suld_2d_array_v4i16_zero:
    return NVPTXISD::Suld2DArrayV4I16Zero;
  case Intrinsic::nvvm_suld_2d_array_v4i32_zero:
    return NVPTXISD::Suld2DArrayV4I32Zero;
  case Intrinsic::nvvm_suld_3d_i8_zero:
    return NVPTXISD::Suld3DI8Zero;
  case Intrinsic::nvvm_suld_3d_i16_zero:
    return NVPTXISD::Suld3DI16Zero;
  case Intrinsic::nvvm_suld_3d_i32_zero:
    return NVPTXISD::Suld3DI32Zero;
  case Intrinsic::nvvm_suld_3d_i64_zero:
    return NVPTXISD::Suld3DI64Zero;
  case Intrinsic::nvvm_suld_3d_v2i8_zero:
    return NVPTXISD::Suld3DV2I8Zero;
  case Intrinsic::nvvm_suld_3d_v2i16_zero:
    return NVPTXISD::Suld3DV2I16Zero;
  case Intrinsic::nvvm_suld_3d_v2i32_zero:
    return NVPTXISD::Suld3DV2I32Zero;
  case Intrinsic::nvvm_suld_3d_v2i64_zero:
    return NVPTXISD::Suld3DV2I64Zero;
  case Intrinsic::nvvm_suld_3d_v4i8_zero:
    return NVPTXISD::Suld3DV4I8Zero;
  case Intrinsic::nvvm_suld_3d_v4i16_zero:
    return NVPTXISD::Suld3DV4I16Zero;
  case Intrinsic::nvvm_suld_3d_v4i32_zero:
    return NVPTXISD::Suld3DV4I32Zero;
  }
}

// llvm.ptx.memcpy.const and llvm.ptx.memmove.const need to be modeled as
// TgtMemIntrinsic
// because we need the information that is only available in the "Value" type
// of destination
// pointer. In particular, the address space information.
bool NVPTXTargetLowering::getTgtMemIntrinsic(
    IntrinsicInfo &Info, const CallInst &I,
    MachineFunction &MF, unsigned Intrinsic) const {
  switch (Intrinsic) {
  default:
    return false;
  case Intrinsic::nvvm_match_all_sync_i32p:
  case Intrinsic::nvvm_match_all_sync_i64p:
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    // memVT is bogus. These intrinsics have IntrInaccessibleMemOnly attribute
    // in order to model data exchange with other threads, but perform no real
    // memory accesses.
    Info.memVT = MVT::i1;

    // Our result depends on both our and other thread's arguments.
    Info.flags = MachineMemOperand::MOLoad | MachineMemOperand::MOStore;
    return true;
  case Intrinsic::nvvm_wmma_m16n16k16_load_a_f16_col:
  case Intrinsic::nvvm_wmma_m16n16k16_load_a_f16_row:
  case Intrinsic::nvvm_wmma_m16n16k16_load_a_f16_col_stride:
  case Intrinsic::nvvm_wmma_m16n16k16_load_a_f16_row_stride:
  case Intrinsic::nvvm_wmma_m16n16k16_load_b_f16_col:
  case Intrinsic::nvvm_wmma_m16n16k16_load_b_f16_row:
  case Intrinsic::nvvm_wmma_m16n16k16_load_b_f16_col_stride:
  case Intrinsic::nvvm_wmma_m16n16k16_load_b_f16_row_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_load_a_f16_col:
  case Intrinsic::nvvm_wmma_m32n8k16_load_a_f16_row:
  case Intrinsic::nvvm_wmma_m32n8k16_load_a_f16_col_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_load_a_f16_row_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_load_b_f16_col:
  case Intrinsic::nvvm_wmma_m32n8k16_load_b_f16_row:
  case Intrinsic::nvvm_wmma_m32n8k16_load_b_f16_col_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_load_b_f16_row_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_load_a_f16_col:
  case Intrinsic::nvvm_wmma_m8n32k16_load_a_f16_row:
  case Intrinsic::nvvm_wmma_m8n32k16_load_a_f16_col_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_load_a_f16_row_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_load_b_f16_col:
  case Intrinsic::nvvm_wmma_m8n32k16_load_b_f16_row:
  case Intrinsic::nvvm_wmma_m8n32k16_load_b_f16_col_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_load_b_f16_row_stride: {
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = MVT::v8f16;
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOLoad;
    Info.align = 16;
    return true;
  }

  case Intrinsic::nvvm_wmma_m16n16k16_load_c_f16_col:
  case Intrinsic::nvvm_wmma_m16n16k16_load_c_f16_row:
  case Intrinsic::nvvm_wmma_m16n16k16_load_c_f16_col_stride:
  case Intrinsic::nvvm_wmma_m16n16k16_load_c_f16_row_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_load_c_f16_col:
  case Intrinsic::nvvm_wmma_m32n8k16_load_c_f16_row:
  case Intrinsic::nvvm_wmma_m32n8k16_load_c_f16_col_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_load_c_f16_row_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_load_c_f16_col:
  case Intrinsic::nvvm_wmma_m8n32k16_load_c_f16_row:
  case Intrinsic::nvvm_wmma_m8n32k16_load_c_f16_col_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_load_c_f16_row_stride: {
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = MVT::v4f16;
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOLoad;
    Info.align = 16;
    return true;
  }

  case Intrinsic::nvvm_wmma_m16n16k16_load_c_f32_col:
  case Intrinsic::nvvm_wmma_m16n16k16_load_c_f32_row:
  case Intrinsic::nvvm_wmma_m16n16k16_load_c_f32_col_stride:
  case Intrinsic::nvvm_wmma_m16n16k16_load_c_f32_row_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_load_c_f32_col:
  case Intrinsic::nvvm_wmma_m32n8k16_load_c_f32_row:
  case Intrinsic::nvvm_wmma_m32n8k16_load_c_f32_col_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_load_c_f32_row_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_load_c_f32_col:
  case Intrinsic::nvvm_wmma_m8n32k16_load_c_f32_row:
  case Intrinsic::nvvm_wmma_m8n32k16_load_c_f32_col_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_load_c_f32_row_stride: {
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = MVT::v8f32;
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOLoad;
    Info.align = 16;
    return true;
  }

  case Intrinsic::nvvm_wmma_m16n16k16_store_d_f16_col:
  case Intrinsic::nvvm_wmma_m16n16k16_store_d_f16_row:
  case Intrinsic::nvvm_wmma_m16n16k16_store_d_f16_col_stride:
  case Intrinsic::nvvm_wmma_m16n16k16_store_d_f16_row_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_store_d_f16_col:
  case Intrinsic::nvvm_wmma_m32n8k16_store_d_f16_row:
  case Intrinsic::nvvm_wmma_m32n8k16_store_d_f16_col_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_store_d_f16_row_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_store_d_f16_col:
  case Intrinsic::nvvm_wmma_m8n32k16_store_d_f16_row:
  case Intrinsic::nvvm_wmma_m8n32k16_store_d_f16_col_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_store_d_f16_row_stride: {
    Info.opc = ISD::INTRINSIC_VOID;
    Info.memVT = MVT::v4f16;
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOStore;
    Info.align = 16;
    return true;
  }

  case Intrinsic::nvvm_wmma_m16n16k16_store_d_f32_col:
  case Intrinsic::nvvm_wmma_m16n16k16_store_d_f32_row:
  case Intrinsic::nvvm_wmma_m16n16k16_store_d_f32_col_stride:
  case Intrinsic::nvvm_wmma_m16n16k16_store_d_f32_row_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_store_d_f32_col:
  case Intrinsic::nvvm_wmma_m32n8k16_store_d_f32_row:
  case Intrinsic::nvvm_wmma_m32n8k16_store_d_f32_col_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_store_d_f32_row_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_store_d_f32_col:
  case Intrinsic::nvvm_wmma_m8n32k16_store_d_f32_row:
  case Intrinsic::nvvm_wmma_m8n32k16_store_d_f32_col_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_store_d_f32_row_stride: {
    Info.opc = ISD::INTRINSIC_VOID;
    Info.memVT = MVT::v8f32;
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOStore;
    Info.align = 16;
    return true;
  }

  case Intrinsic::nvvm_atomic_load_add_f32:
  case Intrinsic::nvvm_atomic_load_add_f64:
  case Intrinsic::nvvm_atomic_load_inc_32:
  case Intrinsic::nvvm_atomic_load_dec_32:

  case Intrinsic::nvvm_atomic_add_gen_f_cta:
  case Intrinsic::nvvm_atomic_add_gen_f_sys:
  case Intrinsic::nvvm_atomic_add_gen_i_cta:
  case Intrinsic::nvvm_atomic_add_gen_i_sys:
  case Intrinsic::nvvm_atomic_and_gen_i_cta:
  case Intrinsic::nvvm_atomic_and_gen_i_sys:
  case Intrinsic::nvvm_atomic_cas_gen_i_cta:
  case Intrinsic::nvvm_atomic_cas_gen_i_sys:
  case Intrinsic::nvvm_atomic_dec_gen_i_cta:
  case Intrinsic::nvvm_atomic_dec_gen_i_sys:
  case Intrinsic::nvvm_atomic_inc_gen_i_cta:
  case Intrinsic::nvvm_atomic_inc_gen_i_sys:
  case Intrinsic::nvvm_atomic_max_gen_i_cta:
  case Intrinsic::nvvm_atomic_max_gen_i_sys:
  case Intrinsic::nvvm_atomic_min_gen_i_cta:
  case Intrinsic::nvvm_atomic_min_gen_i_sys:
  case Intrinsic::nvvm_atomic_or_gen_i_cta:
  case Intrinsic::nvvm_atomic_or_gen_i_sys:
  case Intrinsic::nvvm_atomic_exch_gen_i_cta:
  case Intrinsic::nvvm_atomic_exch_gen_i_sys:
  case Intrinsic::nvvm_atomic_xor_gen_i_cta:
  case Intrinsic::nvvm_atomic_xor_gen_i_sys: {
    auto &DL = I.getModule()->getDataLayout();
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = getValueType(DL, I.getType());
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOLoad | MachineMemOperand::MOStore;
    Info.align = 0;
    return true;
  }

  case Intrinsic::nvvm_ldu_global_i:
  case Intrinsic::nvvm_ldu_global_f:
  case Intrinsic::nvvm_ldu_global_p: {
    auto &DL = I.getModule()->getDataLayout();
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    if (Intrinsic == Intrinsic::nvvm_ldu_global_i)
      Info.memVT = getValueType(DL, I.getType());
    else if(Intrinsic == Intrinsic::nvvm_ldu_global_p)
      Info.memVT = getPointerTy(DL);
    else
      Info.memVT = getValueType(DL, I.getType());
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOLoad;
    Info.align = cast<ConstantInt>(I.getArgOperand(1))->getZExtValue();

    return true;
  }
  case Intrinsic::nvvm_ldg_global_i:
  case Intrinsic::nvvm_ldg_global_f:
  case Intrinsic::nvvm_ldg_global_p: {
    auto &DL = I.getModule()->getDataLayout();

    Info.opc = ISD::INTRINSIC_W_CHAIN;
    if (Intrinsic == Intrinsic::nvvm_ldg_global_i)
      Info.memVT = getValueType(DL, I.getType());
    else if(Intrinsic == Intrinsic::nvvm_ldg_global_p)
      Info.memVT = getPointerTy(DL);
    else
      Info.memVT = getValueType(DL, I.getType());
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOLoad;
    Info.align = cast<ConstantInt>(I.getArgOperand(1))->getZExtValue();

    return true;
  }

  case Intrinsic::nvvm_tex_1d_v4f32_s32:
  case Intrinsic::nvvm_tex_1d_v4f32_f32:
  case Intrinsic::nvvm_tex_1d_level_v4f32_f32:
  case Intrinsic::nvvm_tex_1d_grad_v4f32_f32:
  case Intrinsic::nvvm_tex_1d_array_v4f32_s32:
  case Intrinsic::nvvm_tex_1d_array_v4f32_f32:
  case Intrinsic::nvvm_tex_1d_array_level_v4f32_f32:
  case Intrinsic::nvvm_tex_1d_array_grad_v4f32_f32:
  case Intrinsic::nvvm_tex_2d_v4f32_s32:
  case Intrinsic::nvvm_tex_2d_v4f32_f32:
  case Intrinsic::nvvm_tex_2d_level_v4f32_f32:
  case Intrinsic::nvvm_tex_2d_grad_v4f32_f32:
  case Intrinsic::nvvm_tex_2d_array_v4f32_s32:
  case Intrinsic::nvvm_tex_2d_array_v4f32_f32:
  case Intrinsic::nvvm_tex_2d_array_level_v4f32_f32:
  case Intrinsic::nvvm_tex_2d_array_grad_v4f32_f32:
  case Intrinsic::nvvm_tex_3d_v4f32_s32:
  case Intrinsic::nvvm_tex_3d_v4f32_f32:
  case Intrinsic::nvvm_tex_3d_level_v4f32_f32:
  case Intrinsic::nvvm_tex_3d_grad_v4f32_f32:
  case Intrinsic::nvvm_tex_cube_v4f32_f32:
  case Intrinsic::nvvm_tex_cube_level_v4f32_f32:
  case Intrinsic::nvvm_tex_cube_array_v4f32_f32:
  case Intrinsic::nvvm_tex_cube_array_level_v4f32_f32:
  case Intrinsic::nvvm_tld4_r_2d_v4f32_f32:
  case Intrinsic::nvvm_tld4_g_2d_v4f32_f32:
  case Intrinsic::nvvm_tld4_b_2d_v4f32_f32:
  case Intrinsic::nvvm_tld4_a_2d_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_1d_v4f32_s32:
  case Intrinsic::nvvm_tex_unified_1d_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_1d_level_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_1d_grad_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_1d_array_v4f32_s32:
  case Intrinsic::nvvm_tex_unified_1d_array_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_1d_array_level_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_1d_array_grad_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_2d_v4f32_s32:
  case Intrinsic::nvvm_tex_unified_2d_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_2d_level_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_2d_grad_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_2d_array_v4f32_s32:
  case Intrinsic::nvvm_tex_unified_2d_array_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_2d_array_level_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_2d_array_grad_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_3d_v4f32_s32:
  case Intrinsic::nvvm_tex_unified_3d_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_3d_level_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_3d_grad_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_cube_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_cube_level_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_cube_array_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_cube_array_level_v4f32_f32:
  case Intrinsic::nvvm_tld4_unified_r_2d_v4f32_f32:
  case Intrinsic::nvvm_tld4_unified_g_2d_v4f32_f32:
  case Intrinsic::nvvm_tld4_unified_b_2d_v4f32_f32:
  case Intrinsic::nvvm_tld4_unified_a_2d_v4f32_f32:
    Info.opc = getOpcForTextureInstr(Intrinsic);
    Info.memVT = MVT::v4f32;
    Info.ptrVal = nullptr;
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOLoad;
    Info.align = 16;
    return true;

  case Intrinsic::nvvm_tex_1d_v4s32_s32:
  case Intrinsic::nvvm_tex_1d_v4s32_f32:
  case Intrinsic::nvvm_tex_1d_level_v4s32_f32:
  case Intrinsic::nvvm_tex_1d_grad_v4s32_f32:
  case Intrinsic::nvvm_tex_1d_array_v4s32_s32:
  case Intrinsic::nvvm_tex_1d_array_v4s32_f32:
  case Intrinsic::nvvm_tex_1d_array_level_v4s32_f32:
  case Intrinsic::nvvm_tex_1d_array_grad_v4s32_f32:
  case Intrinsic::nvvm_tex_2d_v4s32_s32:
  case Intrinsic::nvvm_tex_2d_v4s32_f32:
  case Intrinsic::nvvm_tex_2d_level_v4s32_f32:
  case Intrinsic::nvvm_tex_2d_grad_v4s32_f32:
  case Intrinsic::nvvm_tex_2d_array_v4s32_s32:
  case Intrinsic::nvvm_tex_2d_array_v4s32_f32:
  case Intrinsic::nvvm_tex_2d_array_level_v4s32_f32:
  case Intrinsic::nvvm_tex_2d_array_grad_v4s32_f32:
  case Intrinsic::nvvm_tex_3d_v4s32_s32:
  case Intrinsic::nvvm_tex_3d_v4s32_f32:
  case Intrinsic::nvvm_tex_3d_level_v4s32_f32:
  case Intrinsic::nvvm_tex_3d_grad_v4s32_f32:
  case Intrinsic::nvvm_tex_cube_v4s32_f32:
  case Intrinsic::nvvm_tex_cube_level_v4s32_f32:
  case Intrinsic::nvvm_tex_cube_array_v4s32_f32:
  case Intrinsic::nvvm_tex_cube_array_level_v4s32_f32:
  case Intrinsic::nvvm_tex_cube_v4u32_f32:
  case Intrinsic::nvvm_tex_cube_level_v4u32_f32:
  case Intrinsic::nvvm_tex_cube_array_v4u32_f32:
  case Intrinsic::nvvm_tex_cube_array_level_v4u32_f32:
  case Intrinsic::nvvm_tex_1d_v4u32_s32:
  case Intrinsic::nvvm_tex_1d_v4u32_f32:
  case Intrinsic::nvvm_tex_1d_level_v4u32_f32:
  case Intrinsic::nvvm_tex_1d_grad_v4u32_f32:
  case Intrinsic::nvvm_tex_1d_array_v4u32_s32:
  case Intrinsic::nvvm_tex_1d_array_v4u32_f32:
  case Intrinsic::nvvm_tex_1d_array_level_v4u32_f32:
  case Intrinsic::nvvm_tex_1d_array_grad_v4u32_f32:
  case Intrinsic::nvvm_tex_2d_v4u32_s32:
  case Intrinsic::nvvm_tex_2d_v4u32_f32:
  case Intrinsic::nvvm_tex_2d_level_v4u32_f32:
  case Intrinsic::nvvm_tex_2d_grad_v4u32_f32:
  case Intrinsic::nvvm_tex_2d_array_v4u32_s32:
  case Intrinsic::nvvm_tex_2d_array_v4u32_f32:
  case Intrinsic::nvvm_tex_2d_array_level_v4u32_f32:
  case Intrinsic::nvvm_tex_2d_array_grad_v4u32_f32:
  case Intrinsic::nvvm_tex_3d_v4u32_s32:
  case Intrinsic::nvvm_tex_3d_v4u32_f32:
  case Intrinsic::nvvm_tex_3d_level_v4u32_f32:
  case Intrinsic::nvvm_tex_3d_grad_v4u32_f32:
  case Intrinsic::nvvm_tld4_r_2d_v4s32_f32:
  case Intrinsic::nvvm_tld4_g_2d_v4s32_f32:
  case Intrinsic::nvvm_tld4_b_2d_v4s32_f32:
  case Intrinsic::nvvm_tld4_a_2d_v4s32_f32:
  case Intrinsic::nvvm_tld4_r_2d_v4u32_f32:
  case Intrinsic::nvvm_tld4_g_2d_v4u32_f32:
  case Intrinsic::nvvm_tld4_b_2d_v4u32_f32:
  case Intrinsic::nvvm_tld4_a_2d_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_1d_v4s32_s32:
  case Intrinsic::nvvm_tex_unified_1d_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_1d_level_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_1d_grad_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_1d_array_v4s32_s32:
  case Intrinsic::nvvm_tex_unified_1d_array_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_1d_array_level_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_1d_array_grad_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_2d_v4s32_s32:
  case Intrinsic::nvvm_tex_unified_2d_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_2d_level_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_2d_grad_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_2d_array_v4s32_s32:
  case Intrinsic::nvvm_tex_unified_2d_array_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_2d_array_level_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_2d_array_grad_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_3d_v4s32_s32:
  case Intrinsic::nvvm_tex_unified_3d_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_3d_level_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_3d_grad_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_1d_v4u32_s32:
  case Intrinsic::nvvm_tex_unified_1d_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_1d_level_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_1d_grad_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_1d_array_v4u32_s32:
  case Intrinsic::nvvm_tex_unified_1d_array_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_1d_array_level_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_1d_array_grad_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_2d_v4u32_s32:
  case Intrinsic::nvvm_tex_unified_2d_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_2d_level_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_2d_grad_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_2d_array_v4u32_s32:
  case Intrinsic::nvvm_tex_unified_2d_array_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_2d_array_level_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_2d_array_grad_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_3d_v4u32_s32:
  case Intrinsic::nvvm_tex_unified_3d_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_3d_level_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_3d_grad_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_cube_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_cube_level_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_cube_array_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_cube_array_level_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_cube_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_cube_level_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_cube_array_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_cube_array_level_v4u32_f32:
  case Intrinsic::nvvm_tld4_unified_r_2d_v4s32_f32:
  case Intrinsic::nvvm_tld4_unified_g_2d_v4s32_f32:
  case Intrinsic::nvvm_tld4_unified_b_2d_v4s32_f32:
  case Intrinsic::nvvm_tld4_unified_a_2d_v4s32_f32:
  case Intrinsic::nvvm_tld4_unified_r_2d_v4u32_f32:
  case Intrinsic::nvvm_tld4_unified_g_2d_v4u32_f32:
  case Intrinsic::nvvm_tld4_unified_b_2d_v4u32_f32:
  case Intrinsic::nvvm_tld4_unified_a_2d_v4u32_f32:
    Info.opc = getOpcForTextureInstr(Intrinsic);
    Info.memVT = MVT::v4i32;
    Info.ptrVal = nullptr;
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOLoad;
    Info.align = 16;
    return true;

  case Intrinsic::nvvm_suld_1d_i8_clamp:
  case Intrinsic::nvvm_suld_1d_v2i8_clamp:
  case Intrinsic::nvvm_suld_1d_v4i8_clamp:
  case Intrinsic::nvvm_suld_1d_array_i8_clamp:
  case Intrinsic::nvvm_suld_1d_array_v2i8_clamp:
  case Intrinsic::nvvm_suld_1d_array_v4i8_clamp:
  case Intrinsic::nvvm_suld_2d_i8_clamp:
  case Intrinsic::nvvm_suld_2d_v2i8_clamp:
  case Intrinsic::nvvm_suld_2d_v4i8_clamp:
  case Intrinsic::nvvm_suld_2d_array_i8_clamp:
  case Intrinsic::nvvm_suld_2d_array_v2i8_clamp:
  case Intrinsic::nvvm_suld_2d_array_v4i8_clamp:
  case Intrinsic::nvvm_suld_3d_i8_clamp:
  case Intrinsic::nvvm_suld_3d_v2i8_clamp:
  case Intrinsic::nvvm_suld_3d_v4i8_clamp:
  case Intrinsic::nvvm_suld_1d_i8_trap:
  case Intrinsic::nvvm_suld_1d_v2i8_trap:
  case Intrinsic::nvvm_suld_1d_v4i8_trap:
  case Intrinsic::nvvm_suld_1d_array_i8_trap:
  case Intrinsic::nvvm_suld_1d_array_v2i8_trap:
  case Intrinsic::nvvm_suld_1d_array_v4i8_trap:
  case Intrinsic::nvvm_suld_2d_i8_trap:
  case Intrinsic::nvvm_suld_2d_v2i8_trap:
  case Intrinsic::nvvm_suld_2d_v4i8_trap:
  case Intrinsic::nvvm_suld_2d_array_i8_trap:
  case Intrinsic::nvvm_suld_2d_array_v2i8_trap:
  case Intrinsic::nvvm_suld_2d_array_v4i8_trap:
  case Intrinsic::nvvm_suld_3d_i8_trap:
  case Intrinsic::nvvm_suld_3d_v2i8_trap:
  case Intrinsic::nvvm_suld_3d_v4i8_trap:
  case Intrinsic::nvvm_suld_1d_i8_zero:
  case Intrinsic::nvvm_suld_1d_v2i8_zero:
  case Intrinsic::nvvm_suld_1d_v4i8_zero:
  case Intrinsic::nvvm_suld_1d_array_i8_zero:
  case Intrinsic::nvvm_suld_1d_array_v2i8_zero:
  case Intrinsic::nvvm_suld_1d_array_v4i8_zero:
  case Intrinsic::nvvm_suld_2d_i8_zero:
  case Intrinsic::nvvm_suld_2d_v2i8_zero:
  case Intrinsic::nvvm_suld_2d_v4i8_zero:
  case Intrinsic::nvvm_suld_2d_array_i8_zero:
  case Intrinsic::nvvm_suld_2d_array_v2i8_zero:
  case Intrinsic::nvvm_suld_2d_array_v4i8_zero:
  case Intrinsic::nvvm_suld_3d_i8_zero:
  case Intrinsic::nvvm_suld_3d_v2i8_zero:
  case Intrinsic::nvvm_suld_3d_v4i8_zero:
    Info.opc = getOpcForSurfaceInstr(Intrinsic);
    Info.memVT = MVT::i8;
    Info.ptrVal = nullptr;
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOLoad;
    Info.align = 16;
    return true;

  case Intrinsic::nvvm_suld_1d_i16_clamp:
  case Intrinsic::nvvm_suld_1d_v2i16_clamp:
  case Intrinsic::nvvm_suld_1d_v4i16_clamp:
  case Intrinsic::nvvm_suld_1d_array_i16_clamp:
  case Intrinsic::nvvm_suld_1d_array_v2i16_clamp:
  case Intrinsic::nvvm_suld_1d_array_v4i16_clamp:
  case Intrinsic::nvvm_suld_2d_i16_clamp:
  case Intrinsic::nvvm_suld_2d_v2i16_clamp:
  case Intrinsic::nvvm_suld_2d_v4i16_clamp:
  case Intrinsic::nvvm_suld_2d_array_i16_clamp:
  case Intrinsic::nvvm_suld_2d_array_v2i16_clamp:
  case Intrinsic::nvvm_suld_2d_array_v4i16_clamp:
  case Intrinsic::nvvm_suld_3d_i16_clamp:
  case Intrinsic::nvvm_suld_3d_v2i16_clamp:
  case Intrinsic::nvvm_suld_3d_v4i16_clamp:
  case Intrinsic::nvvm_suld_1d_i16_trap:
  case Intrinsic::nvvm_suld_1d_v2i16_trap:
  case Intrinsic::nvvm_suld_1d_v4i16_trap:
  case Intrinsic::nvvm_suld_1d_array_i16_trap:
  case Intrinsic::nvvm_suld_1d_array_v2i16_trap:
  case Intrinsic::nvvm_suld_1d_array_v4i16_trap:
  case Intrinsic::nvvm_suld_2d_i16_trap:
  case Intrinsic::nvvm_suld_2d_v2i16_trap:
  case Intrinsic::nvvm_suld_2d_v4i16_trap:
  case Intrinsic::nvvm_suld_2d_array_i16_trap:
  case Intrinsic::nvvm_suld_2d_array_v2i16_trap:
  case Intrinsic::nvvm_suld_2d_array_v4i16_trap:
  case Intrinsic::nvvm_suld_3d_i16_trap:
  case Intrinsic::nvvm_suld_3d_v2i16_trap:
  case Intrinsic::nvvm_suld_3d_v4i16_trap:
  case Intrinsic::nvvm_suld_1d_i16_zero:
  case Intrinsic::nvvm_suld_1d_v2i16_zero:
  case Intrinsic::nvvm_suld_1d_v4i16_zero:
  case Intrinsic::nvvm_suld_1d_array_i16_zero:
  case Intrinsic::nvvm_suld_1d_array_v2i16_zero:
  case Intrinsic::nvvm_suld_1d_array_v4i16_zero:
  case Intrinsic::nvvm_suld_2d_i16_zero:
  case Intrinsic::nvvm_suld_2d_v2i16_zero:
  case Intrinsic::nvvm_suld_2d_v4i16_zero:
  case Intrinsic::nvvm_suld_2d_array_i16_zero:
  case Intrinsic::nvvm_suld_2d_array_v2i16_zero:
  case Intrinsic::nvvm_suld_2d_array_v4i16_zero:
  case Intrinsic::nvvm_suld_3d_i16_zero:
  case Intrinsic::nvvm_suld_3d_v2i16_zero:
  case Intrinsic::nvvm_suld_3d_v4i16_zero:
    Info.opc = getOpcForSurfaceInstr(Intrinsic);
    Info.memVT = MVT::i16;
    Info.ptrVal = nullptr;
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOLoad;
    Info.align = 16;
    return true;

  case Intrinsic::nvvm_suld_1d_i32_clamp:
  case Intrinsic::nvvm_suld_1d_v2i32_clamp:
  case Intrinsic::nvvm_suld_1d_v4i32_clamp:
  case Intrinsic::nvvm_suld_1d_array_i32_clamp:
  case Intrinsic::nvvm_suld_1d_array_v2i32_clamp:
  case Intrinsic::nvvm_suld_1d_array_v4i32_clamp:
  case Intrinsic::nvvm_suld_2d_i32_clamp:
  case Intrinsic::nvvm_suld_2d_v2i32_clamp:
  case Intrinsic::nvvm_suld_2d_v4i32_clamp:
  case Intrinsic::nvvm_suld_2d_array_i32_clamp:
  case Intrinsic::nvvm_suld_2d_array_v2i32_clamp:
  case Intrinsic::nvvm_suld_2d_array_v4i32_clamp:
  case Intrinsic::nvvm_suld_3d_i32_clamp:
  case Intrinsic::nvvm_suld_3d_v2i32_clamp:
  case Intrinsic::nvvm_suld_3d_v4i32_clamp:
  case Intrinsic::nvvm_suld_1d_i32_trap:
  case Intrinsic::nvvm_suld_1d_v2i32_trap:
  case Intrinsic::nvvm_suld_1d_v4i32_trap:
  case Intrinsic::nvvm_suld_1d_array_i32_trap:
  case Intrinsic::nvvm_suld_1d_array_v2i32_trap:
  case Intrinsic::nvvm_suld_1d_array_v4i32_trap:
  case Intrinsic::nvvm_suld_2d_i32_trap:
  case Intrinsic::nvvm_suld_2d_v2i32_trap:
  case Intrinsic::nvvm_suld_2d_v4i32_trap:
  case Intrinsic::nvvm_suld_2d_array_i32_trap:
  case Intrinsic::nvvm_suld_2d_array_v2i32_trap:
  case Intrinsic::nvvm_suld_2d_array_v4i32_trap:
  case Intrinsic::nvvm_suld_3d_i32_trap:
  case Intrinsic::nvvm_suld_3d_v2i32_trap:
  case Intrinsic::nvvm_suld_3d_v4i32_trap:
  case Intrinsic::nvvm_suld_1d_i32_zero:
  case Intrinsic::nvvm_suld_1d_v2i32_zero:
  case Intrinsic::nvvm_suld_1d_v4i32_zero:
  case Intrinsic::nvvm_suld_1d_array_i32_zero:
  case Intrinsic::nvvm_suld_1d_array_v2i32_zero:
  case Intrinsic::nvvm_suld_1d_array_v4i32_zero:
  case Intrinsic::nvvm_suld_2d_i32_zero:
  case Intrinsic::nvvm_suld_2d_v2i32_zero:
  case Intrinsic::nvvm_suld_2d_v4i32_zero:
  case Intrinsic::nvvm_suld_2d_array_i32_zero:
  case Intrinsic::nvvm_suld_2d_array_v2i32_zero:
  case Intrinsic::nvvm_suld_2d_array_v4i32_zero:
  case Intrinsic::nvvm_suld_3d_i32_zero:
  case Intrinsic::nvvm_suld_3d_v2i32_zero:
  case Intrinsic::nvvm_suld_3d_v4i32_zero:
    Info.opc = getOpcForSurfaceInstr(Intrinsic);
    Info.memVT = MVT::i32;
    Info.ptrVal = nullptr;
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOLoad;
    Info.align = 16;
    return true;

  case Intrinsic::nvvm_suld_1d_i64_clamp:
  case Intrinsic::nvvm_suld_1d_v2i64_clamp:
  case Intrinsic::nvvm_suld_1d_array_i64_clamp:
  case Intrinsic::nvvm_suld_1d_array_v2i64_clamp:
  case Intrinsic::nvvm_suld_2d_i64_clamp:
  case Intrinsic::nvvm_suld_2d_v2i64_clamp:
  case Intrinsic::nvvm_suld_2d_array_i64_clamp:
  case Intrinsic::nvvm_suld_2d_array_v2i64_clamp:
  case Intrinsic::nvvm_suld_3d_i64_clamp:
  case Intrinsic::nvvm_suld_3d_v2i64_clamp:
  case Intrinsic::nvvm_suld_1d_i64_trap:
  case Intrinsic::nvvm_suld_1d_v2i64_trap:
  case Intrinsic::nvvm_suld_1d_array_i64_trap:
  case Intrinsic::nvvm_suld_1d_array_v2i64_trap:
  case Intrinsic::nvvm_suld_2d_i64_trap:
  case Intrinsic::nvvm_suld_2d_v2i64_trap:
  case Intrinsic::nvvm_suld_2d_array_i64_trap:
  case Intrinsic::nvvm_suld_2d_array_v2i64_trap:
  case Intrinsic::nvvm_suld_3d_i64_trap:
  case Intrinsic::nvvm_suld_3d_v2i64_trap:
  case Intrinsic::nvvm_suld_1d_i64_zero:
  case Intrinsic::nvvm_suld_1d_v2i64_zero:
  case Intrinsic::nvvm_suld_1d_array_i64_zero:
  case Intrinsic::nvvm_suld_1d_array_v2i64_zero:
  case Intrinsic::nvvm_suld_2d_i64_zero:
  case Intrinsic::nvvm_suld_2d_v2i64_zero:
  case Intrinsic::nvvm_suld_2d_array_i64_zero:
  case Intrinsic::nvvm_suld_2d_array_v2i64_zero:
  case Intrinsic::nvvm_suld_3d_i64_zero:
  case Intrinsic::nvvm_suld_3d_v2i64_zero:
    Info.opc = getOpcForSurfaceInstr(Intrinsic);
    Info.memVT = MVT::i64;
    Info.ptrVal = nullptr;
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOLoad;
    Info.align = 16;
    return true;
  }
  return false;
}

/// isLegalAddressingMode - Return true if the addressing mode represented
/// by AM is legal for this target, for a load/store of the specified type.
/// Used to guide target specific optimizations, like loop strength reduction
/// (LoopStrengthReduce.cpp) and memory optimization for address mode
/// (CodeGenPrepare.cpp)
bool NVPTXTargetLowering::isLegalAddressingMode(const DataLayout &DL,
                                                const AddrMode &AM, Type *Ty,
                                                unsigned AS, Instruction *I) const {
  // AddrMode - This represents an addressing mode of:
  //    BaseGV + BaseOffs + BaseReg + Scale*ScaleReg
  //
  // The legal address modes are
  // - [avar]
  // - [areg]
  // - [areg+immoff]
  // - [immAddr]

  if (AM.BaseGV) {
    return !AM.BaseOffs && !AM.HasBaseReg && !AM.Scale;
  }

  switch (AM.Scale) {
  case 0: // "r", "r+i" or "i" is allowed
    break;
  case 1:
    if (AM.HasBaseReg) // "r+r+i" or "r+r" is not allowed.
      return false;
    // Otherwise we have r+i.
    break;
  default:
    // No scale > 1 is allowed
    return false;
  }
  return true;
}

//===----------------------------------------------------------------------===//
//                         NVPTX Inline Assembly Support
//===----------------------------------------------------------------------===//

/// getConstraintType - Given a constraint letter, return the type of
/// constraint it is for this target.
NVPTXTargetLowering::ConstraintType
NVPTXTargetLowering::getConstraintType(StringRef Constraint) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    default:
      break;
    case 'b':
    case 'r':
    case 'h':
    case 'c':
    case 'l':
    case 'f':
    case 'd':
    case '0':
    case 'N':
      return C_RegisterClass;
    }
  }
  return TargetLowering::getConstraintType(Constraint);
}

std::pair<unsigned, const TargetRegisterClass *>
NVPTXTargetLowering::getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                                                  StringRef Constraint,
                                                  MVT VT) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'b':
      return std::make_pair(0U, &NVPTX::Int1RegsRegClass);
    case 'c':
      return std::make_pair(0U, &NVPTX::Int16RegsRegClass);
    case 'h':
      return std::make_pair(0U, &NVPTX::Int16RegsRegClass);
    case 'r':
      return std::make_pair(0U, &NVPTX::Int32RegsRegClass);
    case 'l':
    case 'N':
      return std::make_pair(0U, &NVPTX::Int64RegsRegClass);
    case 'f':
      return std::make_pair(0U, &NVPTX::Float32RegsRegClass);
    case 'd':
      return std::make_pair(0U, &NVPTX::Float64RegsRegClass);
    }
  }
  return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
}

//===----------------------------------------------------------------------===//
//                         NVPTX DAG Combining
//===----------------------------------------------------------------------===//

bool NVPTXTargetLowering::allowFMA(MachineFunction &MF,
                                   CodeGenOpt::Level OptLevel) const {
  // Always honor command-line argument
  if (FMAContractLevelOpt.getNumOccurrences() > 0)
    return FMAContractLevelOpt > 0;

  // Do not contract if we're not optimizing the code.
  if (OptLevel == 0)
    return false;

  // Honor TargetOptions flags that explicitly say fusion is okay.
  if (MF.getTarget().Options.AllowFPOpFusion == FPOpFusion::Fast)
    return true;

  return allowUnsafeFPMath(MF);
}

bool NVPTXTargetLowering::allowUnsafeFPMath(MachineFunction &MF) const {
  // Honor TargetOptions flags that explicitly say unsafe math is okay.
  if (MF.getTarget().Options.UnsafeFPMath)
    return true;

  // Allow unsafe math if unsafe-fp-math attribute explicitly says so.
  const Function &F = MF.getFunction();
  if (F.hasFnAttribute("unsafe-fp-math")) {
    Attribute Attr = F.getFnAttribute("unsafe-fp-math");
    StringRef Val = Attr.getValueAsString();
    if (Val == "true")
      return true;
  }

  return false;
}

/// PerformADDCombineWithOperands - Try DAG combinations for an ADD with
/// operands N0 and N1.  This is a helper for PerformADDCombine that is
/// called with the default operands, and if that fails, with commuted
/// operands.
static SDValue PerformADDCombineWithOperands(SDNode *N, SDValue N0, SDValue N1,
                                           TargetLowering::DAGCombinerInfo &DCI,
                                             const NVPTXSubtarget &Subtarget,
                                             CodeGenOpt::Level OptLevel) {
  SelectionDAG  &DAG = DCI.DAG;
  // Skip non-integer, non-scalar case
  EVT VT=N0.getValueType();
  if (VT.isVector())
    return SDValue();

  // fold (add (mul a, b), c) -> (mad a, b, c)
  //
  if (N0.getOpcode() == ISD::MUL) {
    assert (VT.isInteger());
    // For integer:
    // Since integer multiply-add costs the same as integer multiply
    // but is more costly than integer add, do the fusion only when
    // the mul is only used in the add.
    if (OptLevel==CodeGenOpt::None || VT != MVT::i32 ||
        !N0.getNode()->hasOneUse())
      return SDValue();

    // Do the folding
    return DAG.getNode(NVPTXISD::IMAD, SDLoc(N), VT,
                       N0.getOperand(0), N0.getOperand(1), N1);
  }
  else if (N0.getOpcode() == ISD::FMUL) {
    if (VT == MVT::f32 || VT == MVT::f64) {
      const auto *TLI = static_cast<const NVPTXTargetLowering *>(
          &DAG.getTargetLoweringInfo());
      if (!TLI->allowFMA(DAG.getMachineFunction(), OptLevel))
        return SDValue();

      // For floating point:
      // Do the fusion only when the mul has less than 5 uses and all
      // are add.
      // The heuristic is that if a use is not an add, then that use
      // cannot be fused into fma, therefore mul is still needed anyway.
      // If there are more than 4 uses, even if they are all add, fusing
      // them will increase register pressue.
      //
      int numUses = 0;
      int nonAddCount = 0;
      for (SDNode::use_iterator UI = N0.getNode()->use_begin(),
           UE = N0.getNode()->use_end();
           UI != UE; ++UI) {
        numUses++;
        SDNode *User = *UI;
        if (User->getOpcode() != ISD::FADD)
          ++nonAddCount;
      }
      if (numUses >= 5)
        return SDValue();
      if (nonAddCount) {
        int orderNo = N->getIROrder();
        int orderNo2 = N0.getNode()->getIROrder();
        // simple heuristics here for considering potential register
        // pressure, the logics here is that the differnce are used
        // to measure the distance between def and use, the longer distance
        // more likely cause register pressure.
        if (orderNo - orderNo2 < 500)
          return SDValue();

        // Now, check if at least one of the FMUL's operands is live beyond the node N,
        // which guarantees that the FMA will not increase register pressure at node N.
        bool opIsLive = false;
        const SDNode *left = N0.getOperand(0).getNode();
        const SDNode *right = N0.getOperand(1).getNode();

        if (isa<ConstantSDNode>(left) || isa<ConstantSDNode>(right))
          opIsLive = true;

        if (!opIsLive)
          for (SDNode::use_iterator UI = left->use_begin(), UE = left->use_end(); UI != UE; ++UI) {
            SDNode *User = *UI;
            int orderNo3 = User->getIROrder();
            if (orderNo3 > orderNo) {
              opIsLive = true;
              break;
            }
          }

        if (!opIsLive)
          for (SDNode::use_iterator UI = right->use_begin(), UE = right->use_end(); UI != UE; ++UI) {
            SDNode *User = *UI;
            int orderNo3 = User->getIROrder();
            if (orderNo3 > orderNo) {
              opIsLive = true;
              break;
            }
          }

        if (!opIsLive)
          return SDValue();
      }

      return DAG.getNode(ISD::FMA, SDLoc(N), VT,
                         N0.getOperand(0), N0.getOperand(1), N1);
    }
  }

  return SDValue();
}

/// PerformADDCombine - Target-specific dag combine xforms for ISD::ADD.
///
static SDValue PerformADDCombine(SDNode *N,
                                 TargetLowering::DAGCombinerInfo &DCI,
                                 const NVPTXSubtarget &Subtarget,
                                 CodeGenOpt::Level OptLevel) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);

  // First try with the default operand order.
  if (SDValue Result =
          PerformADDCombineWithOperands(N, N0, N1, DCI, Subtarget, OptLevel))
    return Result;

  // If that didn't work, try again with the operands commuted.
  return PerformADDCombineWithOperands(N, N1, N0, DCI, Subtarget, OptLevel);
}

static SDValue PerformANDCombine(SDNode *N,
                                 TargetLowering::DAGCombinerInfo &DCI) {
  // The type legalizer turns a vector load of i8 values into a zextload to i16
  // registers, optionally ANY_EXTENDs it (if target type is integer),
  // and ANDs off the high 8 bits. Since we turn this load into a
  // target-specific DAG node, the DAG combiner fails to eliminate these AND
  // nodes. Do that here.
  SDValue Val = N->getOperand(0);
  SDValue Mask = N->getOperand(1);

  if (isa<ConstantSDNode>(Val)) {
    std::swap(Val, Mask);
  }

  SDValue AExt;
  // Generally, we will see zextload -> IMOV16rr -> ANY_EXTEND -> and
  if (Val.getOpcode() == ISD::ANY_EXTEND) {
    AExt = Val;
    Val = Val->getOperand(0);
  }

  if (Val->isMachineOpcode() && Val->getMachineOpcode() == NVPTX::IMOV16rr) {
    Val = Val->getOperand(0);
  }

  if (Val->getOpcode() == NVPTXISD::LoadV2 ||
      Val->getOpcode() == NVPTXISD::LoadV4) {
    ConstantSDNode *MaskCnst = dyn_cast<ConstantSDNode>(Mask);
    if (!MaskCnst) {
      // Not an AND with a constant
      return SDValue();
    }

    uint64_t MaskVal = MaskCnst->getZExtValue();
    if (MaskVal != 0xff) {
      // Not an AND that chops off top 8 bits
      return SDValue();
    }

    MemSDNode *Mem = dyn_cast<MemSDNode>(Val);
    if (!Mem) {
      // Not a MemSDNode?!?
      return SDValue();
    }

    EVT MemVT = Mem->getMemoryVT();
    if (MemVT != MVT::v2i8 && MemVT != MVT::v4i8) {
      // We only handle the i8 case
      return SDValue();
    }

    unsigned ExtType =
      cast<ConstantSDNode>(Val->getOperand(Val->getNumOperands()-1))->
        getZExtValue();
    if (ExtType == ISD::SEXTLOAD) {
      // If for some reason the load is a sextload, the and is needed to zero
      // out the high 8 bits
      return SDValue();
    }

    bool AddTo = false;
    if (AExt.getNode() != nullptr) {
      // Re-insert the ext as a zext.
      Val = DCI.DAG.getNode(ISD::ZERO_EXTEND, SDLoc(N),
                            AExt.getValueType(), Val);
      AddTo = true;
    }

    // If we get here, the AND is unnecessary.  Just replace it with the load
    DCI.CombineTo(N, Val, AddTo);
  }

  return SDValue();
}

static SDValue PerformREMCombine(SDNode *N,
                                 TargetLowering::DAGCombinerInfo &DCI,
                                 CodeGenOpt::Level OptLevel) {
  assert(N->getOpcode() == ISD::SREM || N->getOpcode() == ISD::UREM);

  // Don't do anything at less than -O2.
  if (OptLevel < CodeGenOpt::Default)
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);
  EVT VT = N->getValueType(0);
  bool IsSigned = N->getOpcode() == ISD::SREM;
  unsigned DivOpc = IsSigned ? ISD::SDIV : ISD::UDIV;

  const SDValue &Num = N->getOperand(0);
  const SDValue &Den = N->getOperand(1);

  for (const SDNode *U : Num->uses()) {
    if (U->getOpcode() == DivOpc && U->getOperand(0) == Num &&
        U->getOperand(1) == Den) {
      // Num % Den -> Num - (Num / Den) * Den
      return DAG.getNode(ISD::SUB, DL, VT, Num,
                         DAG.getNode(ISD::MUL, DL, VT,
                                     DAG.getNode(DivOpc, DL, VT, Num, Den),
                                     Den));
    }
  }
  return SDValue();
}

enum OperandSignedness {
  Signed = 0,
  Unsigned,
  Unknown
};

/// IsMulWideOperandDemotable - Checks if the provided DAG node is an operand
/// that can be demoted to \p OptSize bits without loss of information. The
/// signedness of the operand, if determinable, is placed in \p S.
static bool IsMulWideOperandDemotable(SDValue Op,
                                      unsigned OptSize,
                                      OperandSignedness &S) {
  S = Unknown;

  if (Op.getOpcode() == ISD::SIGN_EXTEND ||
      Op.getOpcode() == ISD::SIGN_EXTEND_INREG) {
    EVT OrigVT = Op.getOperand(0).getValueType();
    if (OrigVT.getSizeInBits() <= OptSize) {
      S = Signed;
      return true;
    }
  } else if (Op.getOpcode() == ISD::ZERO_EXTEND) {
    EVT OrigVT = Op.getOperand(0).getValueType();
    if (OrigVT.getSizeInBits() <= OptSize) {
      S = Unsigned;
      return true;
    }
  }

  return false;
}

/// AreMulWideOperandsDemotable - Checks if the given LHS and RHS operands can
/// be demoted to \p OptSize bits without loss of information. If the operands
/// contain a constant, it should appear as the RHS operand. The signedness of
/// the operands is placed in \p IsSigned.
static bool AreMulWideOperandsDemotable(SDValue LHS, SDValue RHS,
                                        unsigned OptSize,
                                        bool &IsSigned) {
  OperandSignedness LHSSign;

  // The LHS operand must be a demotable op
  if (!IsMulWideOperandDemotable(LHS, OptSize, LHSSign))
    return false;

  // We should have been able to determine the signedness from the LHS
  if (LHSSign == Unknown)
    return false;

  IsSigned = (LHSSign == Signed);

  // The RHS can be a demotable op or a constant
  if (ConstantSDNode *CI = dyn_cast<ConstantSDNode>(RHS)) {
    const APInt &Val = CI->getAPIntValue();
    if (LHSSign == Unsigned) {
      return Val.isIntN(OptSize);
    } else {
      return Val.isSignedIntN(OptSize);
    }
  } else {
    OperandSignedness RHSSign;
    if (!IsMulWideOperandDemotable(RHS, OptSize, RHSSign))
      return false;

    return LHSSign == RHSSign;
  }
}

/// TryMULWIDECombine - Attempt to replace a multiply of M bits with a multiply
/// of M/2 bits that produces an M-bit result (i.e. mul.wide). This transform
/// works on both multiply DAG nodes and SHL DAG nodes with a constant shift
/// amount.
static SDValue TryMULWIDECombine(SDNode *N,
                                 TargetLowering::DAGCombinerInfo &DCI) {
  EVT MulType = N->getValueType(0);
  if (MulType != MVT::i32 && MulType != MVT::i64) {
    return SDValue();
  }

  SDLoc DL(N);
  unsigned OptSize = MulType.getSizeInBits() >> 1;
  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);

  // Canonicalize the multiply so the constant (if any) is on the right
  if (N->getOpcode() == ISD::MUL) {
    if (isa<ConstantSDNode>(LHS)) {
      std::swap(LHS, RHS);
    }
  }

  // If we have a SHL, determine the actual multiply amount
  if (N->getOpcode() == ISD::SHL) {
    ConstantSDNode *ShlRHS = dyn_cast<ConstantSDNode>(RHS);
    if (!ShlRHS) {
      return SDValue();
    }

    APInt ShiftAmt = ShlRHS->getAPIntValue();
    unsigned BitWidth = MulType.getSizeInBits();
    if (ShiftAmt.sge(0) && ShiftAmt.slt(BitWidth)) {
      APInt MulVal = APInt(BitWidth, 1) << ShiftAmt;
      RHS = DCI.DAG.getConstant(MulVal, DL, MulType);
    } else {
      return SDValue();
    }
  }

  bool Signed;
  // Verify that our operands are demotable
  if (!AreMulWideOperandsDemotable(LHS, RHS, OptSize, Signed)) {
    return SDValue();
  }

  EVT DemotedVT;
  if (MulType == MVT::i32) {
    DemotedVT = MVT::i16;
  } else {
    DemotedVT = MVT::i32;
  }

  // Truncate the operands to the correct size. Note that these are just for
  // type consistency and will (likely) be eliminated in later phases.
  SDValue TruncLHS =
    DCI.DAG.getNode(ISD::TRUNCATE, DL, DemotedVT, LHS);
  SDValue TruncRHS =
    DCI.DAG.getNode(ISD::TRUNCATE, DL, DemotedVT, RHS);

  unsigned Opc;
  if (Signed) {
    Opc = NVPTXISD::MUL_WIDE_SIGNED;
  } else {
    Opc = NVPTXISD::MUL_WIDE_UNSIGNED;
  }

  return DCI.DAG.getNode(Opc, DL, MulType, TruncLHS, TruncRHS);
}

/// PerformMULCombine - Runs PTX-specific DAG combine patterns on MUL nodes.
static SDValue PerformMULCombine(SDNode *N,
                                 TargetLowering::DAGCombinerInfo &DCI,
                                 CodeGenOpt::Level OptLevel) {
  if (OptLevel > 0) {
    // Try mul.wide combining at OptLevel > 0
    if (SDValue Ret = TryMULWIDECombine(N, DCI))
      return Ret;
  }

  return SDValue();
}

/// PerformSHLCombine - Runs PTX-specific DAG combine patterns on SHL nodes.
static SDValue PerformSHLCombine(SDNode *N,
                                 TargetLowering::DAGCombinerInfo &DCI,
                                 CodeGenOpt::Level OptLevel) {
  if (OptLevel > 0) {
    // Try mul.wide combining at OptLevel > 0
    if (SDValue Ret = TryMULWIDECombine(N, DCI))
      return Ret;
  }

  return SDValue();
}

static SDValue PerformSETCCCombine(SDNode *N,
                                   TargetLowering::DAGCombinerInfo &DCI) {
  EVT CCType = N->getValueType(0);
  SDValue A = N->getOperand(0);
  SDValue B = N->getOperand(1);

  if (CCType != MVT::v2i1 || A.getValueType() != MVT::v2f16)
    return SDValue();

  SDLoc DL(N);
  // setp.f16x2 returns two scalar predicates, which we need to
  // convert back to v2i1. The returned result will be scalarized by
  // the legalizer, but the comparison will remain a single vector
  // instruction.
  SDValue CCNode = DCI.DAG.getNode(NVPTXISD::SETP_F16X2, DL,
                                   DCI.DAG.getVTList(MVT::i1, MVT::i1),
                                   {A, B, N->getOperand(2)});
  return DCI.DAG.getNode(ISD::BUILD_VECTOR, DL, CCType, CCNode.getValue(0),
                         CCNode.getValue(1));
}

SDValue NVPTXTargetLowering::PerformDAGCombine(SDNode *N,
                                               DAGCombinerInfo &DCI) const {
  CodeGenOpt::Level OptLevel = getTargetMachine().getOptLevel();
  switch (N->getOpcode()) {
    default: break;
    case ISD::ADD:
    case ISD::FADD:
      return PerformADDCombine(N, DCI, STI, OptLevel);
    case ISD::MUL:
      return PerformMULCombine(N, DCI, OptLevel);
    case ISD::SHL:
      return PerformSHLCombine(N, DCI, OptLevel);
    case ISD::AND:
      return PerformANDCombine(N, DCI);
    case ISD::UREM:
    case ISD::SREM:
      return PerformREMCombine(N, DCI, OptLevel);
    case ISD::SETCC:
      return PerformSETCCCombine(N, DCI);
  }
  return SDValue();
}

/// ReplaceVectorLoad - Convert vector loads into multi-output scalar loads.
static void ReplaceLoadVector(SDNode *N, SelectionDAG &DAG,
                              SmallVectorImpl<SDValue> &Results) {
  EVT ResVT = N->getValueType(0);
  SDLoc DL(N);

  assert(ResVT.isVector() && "Vector load must have vector type");

  // We only handle "native" vector sizes for now, e.g. <4 x double> is not
  // legal.  We can (and should) split that into 2 loads of <2 x double> here
  // but I'm leaving that as a TODO for now.
  assert(ResVT.isSimple() && "Can only handle simple types");
  switch (ResVT.getSimpleVT().SimpleTy) {
  default:
    return;
  case MVT::v2i8:
  case MVT::v2i16:
  case MVT::v2i32:
  case MVT::v2i64:
  case MVT::v2f16:
  case MVT::v2f32:
  case MVT::v2f64:
  case MVT::v4i8:
  case MVT::v4i16:
  case MVT::v4i32:
  case MVT::v4f16:
  case MVT::v4f32:
  case MVT::v8f16: // <4 x f16x2>
    // This is a "native" vector type
    break;
  }

  LoadSDNode *LD = cast<LoadSDNode>(N);

  unsigned Align = LD->getAlignment();
  auto &TD = DAG.getDataLayout();
  unsigned PrefAlign =
      TD.getPrefTypeAlignment(ResVT.getTypeForEVT(*DAG.getContext()));
  if (Align < PrefAlign) {
    // This load is not sufficiently aligned, so bail out and let this vector
    // load be scalarized.  Note that we may still be able to emit smaller
    // vector loads.  For example, if we are loading a <4 x float> with an
    // alignment of 8, this check will fail but the legalizer will try again
    // with 2 x <2 x float>, which will succeed with an alignment of 8.
    return;
  }

  EVT EltVT = ResVT.getVectorElementType();
  unsigned NumElts = ResVT.getVectorNumElements();

  // Since LoadV2 is a target node, we cannot rely on DAG type legalization.
  // Therefore, we must ensure the type is legal.  For i1 and i8, we set the
  // loaded type to i16 and propagate the "real" type as the memory type.
  bool NeedTrunc = false;
  if (EltVT.getSizeInBits() < 16) {
    EltVT = MVT::i16;
    NeedTrunc = true;
  }

  unsigned Opcode = 0;
  SDVTList LdResVTs;
  bool LoadF16x2 = false;

  switch (NumElts) {
  default:
    return;
  case 2:
    Opcode = NVPTXISD::LoadV2;
    LdResVTs = DAG.getVTList(EltVT, EltVT, MVT::Other);
    break;
  case 4: {
    Opcode = NVPTXISD::LoadV4;
    EVT ListVTs[] = { EltVT, EltVT, EltVT, EltVT, MVT::Other };
    LdResVTs = DAG.getVTList(ListVTs);
    break;
  }
  case 8: {
    // v8f16 is a special case. PTX doesn't have ld.v8.f16
    // instruction. Instead, we split the vector into v2f16 chunks and
    // load them with ld.v4.b32.
    assert(EltVT == MVT::f16 && "Unsupported v8 vector type.");
    LoadF16x2 = true;
    Opcode = NVPTXISD::LoadV4;
    EVT ListVTs[] = {MVT::v2f16, MVT::v2f16, MVT::v2f16, MVT::v2f16,
                     MVT::Other};
    LdResVTs = DAG.getVTList(ListVTs);
    break;
  }
  }

  // Copy regular operands
  SmallVector<SDValue, 8> OtherOps(N->op_begin(), N->op_end());

  // The select routine does not have access to the LoadSDNode instance, so
  // pass along the extension information
  OtherOps.push_back(DAG.getIntPtrConstant(LD->getExtensionType(), DL));

  SDValue NewLD = DAG.getMemIntrinsicNode(Opcode, DL, LdResVTs, OtherOps,
                                          LD->getMemoryVT(),
                                          LD->getMemOperand());

  SmallVector<SDValue, 8> ScalarRes;
  if (LoadF16x2) {
    // Split v2f16 subvectors back into individual elements.
    NumElts /= 2;
    for (unsigned i = 0; i < NumElts; ++i) {
      SDValue SubVector = NewLD.getValue(i);
      SDValue E0 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, EltVT, SubVector,
                               DAG.getIntPtrConstant(0, DL));
      SDValue E1 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, EltVT, SubVector,
                               DAG.getIntPtrConstant(1, DL));
      ScalarRes.push_back(E0);
      ScalarRes.push_back(E1);
    }
  } else {
    for (unsigned i = 0; i < NumElts; ++i) {
      SDValue Res = NewLD.getValue(i);
      if (NeedTrunc)
        Res = DAG.getNode(ISD::TRUNCATE, DL, ResVT.getVectorElementType(), Res);
      ScalarRes.push_back(Res);
    }
  }

  SDValue LoadChain = NewLD.getValue(NumElts);

  SDValue BuildVec = DAG.getBuildVector(ResVT, DL, ScalarRes);

  Results.push_back(BuildVec);
  Results.push_back(LoadChain);
}

static void ReplaceINTRINSIC_W_CHAIN(SDNode *N, SelectionDAG &DAG,
                                     SmallVectorImpl<SDValue> &Results) {
  SDValue Chain = N->getOperand(0);
  SDValue Intrin = N->getOperand(1);
  SDLoc DL(N);

  // Get the intrinsic ID
  unsigned IntrinNo = cast<ConstantSDNode>(Intrin.getNode())->getZExtValue();
  switch (IntrinNo) {
  default:
    return;
  case Intrinsic::nvvm_ldg_global_i:
  case Intrinsic::nvvm_ldg_global_f:
  case Intrinsic::nvvm_ldg_global_p:
  case Intrinsic::nvvm_ldu_global_i:
  case Intrinsic::nvvm_ldu_global_f:
  case Intrinsic::nvvm_ldu_global_p: {
    EVT ResVT = N->getValueType(0);

    if (ResVT.isVector()) {
      // Vector LDG/LDU

      unsigned NumElts = ResVT.getVectorNumElements();
      EVT EltVT = ResVT.getVectorElementType();

      // Since LDU/LDG are target nodes, we cannot rely on DAG type
      // legalization.
      // Therefore, we must ensure the type is legal.  For i1 and i8, we set the
      // loaded type to i16 and propagate the "real" type as the memory type.
      bool NeedTrunc = false;
      if (EltVT.getSizeInBits() < 16) {
        EltVT = MVT::i16;
        NeedTrunc = true;
      }

      unsigned Opcode = 0;
      SDVTList LdResVTs;

      switch (NumElts) {
      default:
        return;
      case 2:
        switch (IntrinNo) {
        default:
          return;
        case Intrinsic::nvvm_ldg_global_i:
        case Intrinsic::nvvm_ldg_global_f:
        case Intrinsic::nvvm_ldg_global_p:
          Opcode = NVPTXISD::LDGV2;
          break;
        case Intrinsic::nvvm_ldu_global_i:
        case Intrinsic::nvvm_ldu_global_f:
        case Intrinsic::nvvm_ldu_global_p:
          Opcode = NVPTXISD::LDUV2;
          break;
        }
        LdResVTs = DAG.getVTList(EltVT, EltVT, MVT::Other);
        break;
      case 4: {
        switch (IntrinNo) {
        default:
          return;
        case Intrinsic::nvvm_ldg_global_i:
        case Intrinsic::nvvm_ldg_global_f:
        case Intrinsic::nvvm_ldg_global_p:
          Opcode = NVPTXISD::LDGV4;
          break;
        case Intrinsic::nvvm_ldu_global_i:
        case Intrinsic::nvvm_ldu_global_f:
        case Intrinsic::nvvm_ldu_global_p:
          Opcode = NVPTXISD::LDUV4;
          break;
        }
        EVT ListVTs[] = { EltVT, EltVT, EltVT, EltVT, MVT::Other };
        LdResVTs = DAG.getVTList(ListVTs);
        break;
      }
      }

      SmallVector<SDValue, 8> OtherOps;

      // Copy regular operands

      OtherOps.push_back(Chain); // Chain
                                 // Skip operand 1 (intrinsic ID)
      // Others
      OtherOps.append(N->op_begin() + 2, N->op_end());

      MemIntrinsicSDNode *MemSD = cast<MemIntrinsicSDNode>(N);

      SDValue NewLD = DAG.getMemIntrinsicNode(Opcode, DL, LdResVTs, OtherOps,
                                              MemSD->getMemoryVT(),
                                              MemSD->getMemOperand());

      SmallVector<SDValue, 4> ScalarRes;

      for (unsigned i = 0; i < NumElts; ++i) {
        SDValue Res = NewLD.getValue(i);
        if (NeedTrunc)
          Res =
              DAG.getNode(ISD::TRUNCATE, DL, ResVT.getVectorElementType(), Res);
        ScalarRes.push_back(Res);
      }

      SDValue LoadChain = NewLD.getValue(NumElts);

      SDValue BuildVec =
          DAG.getBuildVector(ResVT, DL, ScalarRes);

      Results.push_back(BuildVec);
      Results.push_back(LoadChain);
    } else {
      // i8 LDG/LDU
      assert(ResVT.isSimple() && ResVT.getSimpleVT().SimpleTy == MVT::i8 &&
             "Custom handling of non-i8 ldu/ldg?");

      // Just copy all operands as-is
      SmallVector<SDValue, 4> Ops(N->op_begin(), N->op_end());

      // Force output to i16
      SDVTList LdResVTs = DAG.getVTList(MVT::i16, MVT::Other);

      MemIntrinsicSDNode *MemSD = cast<MemIntrinsicSDNode>(N);

      // We make sure the memory type is i8, which will be used during isel
      // to select the proper instruction.
      SDValue NewLD =
          DAG.getMemIntrinsicNode(ISD::INTRINSIC_W_CHAIN, DL, LdResVTs, Ops,
                                  MVT::i8, MemSD->getMemOperand());

      Results.push_back(DAG.getNode(ISD::TRUNCATE, DL, MVT::i8,
                                    NewLD.getValue(0)));
      Results.push_back(NewLD.getValue(1));
    }
  }
  }
}

void NVPTXTargetLowering::ReplaceNodeResults(
    SDNode *N, SmallVectorImpl<SDValue> &Results, SelectionDAG &DAG) const {
  switch (N->getOpcode()) {
  default:
    report_fatal_error("Unhandled custom legalization");
  case ISD::LOAD:
    ReplaceLoadVector(N, DAG, Results);
    return;
  case ISD::INTRINSIC_W_CHAIN:
    ReplaceINTRINSIC_W_CHAIN(N, DAG, Results);
    return;
  }
}

// Pin NVPTXTargetObjectFile's vtables to this file.
NVPTXTargetObjectFile::~NVPTXTargetObjectFile() {}

MCSection *NVPTXTargetObjectFile::SelectSectionForGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  return getDataSection();
}
