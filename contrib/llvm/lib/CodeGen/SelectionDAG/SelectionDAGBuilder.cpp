//===- SelectionDAGBuilder.cpp - Selection-DAG building -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This implements routines for translating from LLVM IR into SelectionDAG IR.
//
//===----------------------------------------------------------------------===//

#include "SelectionDAGBuilder.h"
#include "SDNodeDbgValue.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/EHPersonalities.h"
#include "llvm/Analysis/Loads.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/FunctionLoweringInfo.h"
#include "llvm/CodeGen/GCMetadata.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RuntimeLibcalls.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/SelectionDAGTargetInfo.h"
#include "llvm/CodeGen/StackMaps.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/CodeGen/WinEHFuncInfo.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Statepoint.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MachineValueType.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetIntrinsicInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <numeric>
#include <tuple>
#include <utility>
#include <vector>

using namespace llvm;
using namespace PatternMatch;

#define DEBUG_TYPE "isel"

/// LimitFloatPrecision - Generate low-precision inline sequences for
/// some float libcalls (6, 8 or 12 bits).
static unsigned LimitFloatPrecision;

static cl::opt<unsigned, true>
    LimitFPPrecision("limit-float-precision",
                     cl::desc("Generate low-precision inline sequences "
                              "for some float libcalls"),
                     cl::location(LimitFloatPrecision), cl::Hidden,
                     cl::init(0));

static cl::opt<unsigned> SwitchPeelThreshold(
    "switch-peel-threshold", cl::Hidden, cl::init(66),
    cl::desc("Set the case probability threshold for peeling the case from a "
             "switch statement. A value greater than 100 will void this "
             "optimization"));

// Limit the width of DAG chains. This is important in general to prevent
// DAG-based analysis from blowing up. For example, alias analysis and
// load clustering may not complete in reasonable time. It is difficult to
// recognize and avoid this situation within each individual analysis, and
// future analyses are likely to have the same behavior. Limiting DAG width is
// the safe approach and will be especially important with global DAGs.
//
// MaxParallelChains default is arbitrarily high to avoid affecting
// optimization, but could be lowered to improve compile time. Any ld-ld-st-st
// sequence over this should have been converted to llvm.memcpy by the
// frontend. It is easy to induce this behavior with .ll code such as:
// %buffer = alloca [4096 x i8]
// %data = load [4096 x i8]* %argPtr
// store [4096 x i8] %data, [4096 x i8]* %buffer
static const unsigned MaxParallelChains = 64;

// Return the calling convention if the Value passed requires ABI mangling as it
// is a parameter to a function or a return value from a function which is not
// an intrinsic.
static Optional<CallingConv::ID> getABIRegCopyCC(const Value *V) {
  if (auto *R = dyn_cast<ReturnInst>(V))
    return R->getParent()->getParent()->getCallingConv();

  if (auto *CI = dyn_cast<CallInst>(V)) {
    const bool IsInlineAsm = CI->isInlineAsm();
    const bool IsIndirectFunctionCall =
        !IsInlineAsm && !CI->getCalledFunction();

    // It is possible that the call instruction is an inline asm statement or an
    // indirect function call in which case the return value of
    // getCalledFunction() would be nullptr.
    const bool IsInstrinsicCall =
        !IsInlineAsm && !IsIndirectFunctionCall &&
        CI->getCalledFunction()->getIntrinsicID() != Intrinsic::not_intrinsic;

    if (!IsInlineAsm && !IsInstrinsicCall)
      return CI->getCallingConv();
  }

  return None;
}

static SDValue getCopyFromPartsVector(SelectionDAG &DAG, const SDLoc &DL,
                                      const SDValue *Parts, unsigned NumParts,
                                      MVT PartVT, EVT ValueVT, const Value *V,
                                      Optional<CallingConv::ID> CC);

/// getCopyFromParts - Create a value that contains the specified legal parts
/// combined into the value they represent.  If the parts combine to a type
/// larger than ValueVT then AssertOp can be used to specify whether the extra
/// bits are known to be zero (ISD::AssertZext) or sign extended from ValueVT
/// (ISD::AssertSext).
static SDValue getCopyFromParts(SelectionDAG &DAG, const SDLoc &DL,
                                const SDValue *Parts, unsigned NumParts,
                                MVT PartVT, EVT ValueVT, const Value *V,
                                Optional<CallingConv::ID> CC = None,
                                Optional<ISD::NodeType> AssertOp = None) {
  if (ValueVT.isVector())
    return getCopyFromPartsVector(DAG, DL, Parts, NumParts, PartVT, ValueVT, V,
                                  CC);

  assert(NumParts > 0 && "No parts to assemble!");
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  SDValue Val = Parts[0];

  if (NumParts > 1) {
    // Assemble the value from multiple parts.
    if (ValueVT.isInteger()) {
      unsigned PartBits = PartVT.getSizeInBits();
      unsigned ValueBits = ValueVT.getSizeInBits();

      // Assemble the power of 2 part.
      unsigned RoundParts = NumParts & (NumParts - 1) ?
        1 << Log2_32(NumParts) : NumParts;
      unsigned RoundBits = PartBits * RoundParts;
      EVT RoundVT = RoundBits == ValueBits ?
        ValueVT : EVT::getIntegerVT(*DAG.getContext(), RoundBits);
      SDValue Lo, Hi;

      EVT HalfVT = EVT::getIntegerVT(*DAG.getContext(), RoundBits/2);

      if (RoundParts > 2) {
        Lo = getCopyFromParts(DAG, DL, Parts, RoundParts / 2,
                              PartVT, HalfVT, V);
        Hi = getCopyFromParts(DAG, DL, Parts + RoundParts / 2,
                              RoundParts / 2, PartVT, HalfVT, V);
      } else {
        Lo = DAG.getNode(ISD::BITCAST, DL, HalfVT, Parts[0]);
        Hi = DAG.getNode(ISD::BITCAST, DL, HalfVT, Parts[1]);
      }

      if (DAG.getDataLayout().isBigEndian())
        std::swap(Lo, Hi);

      Val = DAG.getNode(ISD::BUILD_PAIR, DL, RoundVT, Lo, Hi);

      if (RoundParts < NumParts) {
        // Assemble the trailing non-power-of-2 part.
        unsigned OddParts = NumParts - RoundParts;
        EVT OddVT = EVT::getIntegerVT(*DAG.getContext(), OddParts * PartBits);
        Hi = getCopyFromParts(DAG, DL, Parts + RoundParts, OddParts, PartVT,
                              OddVT, V, CC);

        // Combine the round and odd parts.
        Lo = Val;
        if (DAG.getDataLayout().isBigEndian())
          std::swap(Lo, Hi);
        EVT TotalVT = EVT::getIntegerVT(*DAG.getContext(), NumParts * PartBits);
        Hi = DAG.getNode(ISD::ANY_EXTEND, DL, TotalVT, Hi);
        Hi =
            DAG.getNode(ISD::SHL, DL, TotalVT, Hi,
                        DAG.getConstant(Lo.getValueSizeInBits(), DL,
                                        TLI.getPointerTy(DAG.getDataLayout())));
        Lo = DAG.getNode(ISD::ZERO_EXTEND, DL, TotalVT, Lo);
        Val = DAG.getNode(ISD::OR, DL, TotalVT, Lo, Hi);
      }
    } else if (PartVT.isFloatingPoint()) {
      // FP split into multiple FP parts (for ppcf128)
      assert(ValueVT == EVT(MVT::ppcf128) && PartVT == MVT::f64 &&
             "Unexpected split");
      SDValue Lo, Hi;
      Lo = DAG.getNode(ISD::BITCAST, DL, EVT(MVT::f64), Parts[0]);
      Hi = DAG.getNode(ISD::BITCAST, DL, EVT(MVT::f64), Parts[1]);
      if (TLI.hasBigEndianPartOrdering(ValueVT, DAG.getDataLayout()))
        std::swap(Lo, Hi);
      Val = DAG.getNode(ISD::BUILD_PAIR, DL, ValueVT, Lo, Hi);
    } else {
      // FP split into integer parts (soft fp)
      assert(ValueVT.isFloatingPoint() && PartVT.isInteger() &&
             !PartVT.isVector() && "Unexpected split");
      EVT IntVT = EVT::getIntegerVT(*DAG.getContext(), ValueVT.getSizeInBits());
      Val = getCopyFromParts(DAG, DL, Parts, NumParts, PartVT, IntVT, V, CC);
    }
  }

  // There is now one part, held in Val.  Correct it to match ValueVT.
  // PartEVT is the type of the register class that holds the value.
  // ValueVT is the type of the inline asm operation.
  EVT PartEVT = Val.getValueType();

  if (PartEVT == ValueVT)
    return Val;

  if (PartEVT.isInteger() && ValueVT.isFloatingPoint() &&
      ValueVT.bitsLT(PartEVT)) {
    // For an FP value in an integer part, we need to truncate to the right
    // width first.
    PartEVT = EVT::getIntegerVT(*DAG.getContext(),  ValueVT.getSizeInBits());
    Val = DAG.getNode(ISD::TRUNCATE, DL, PartEVT, Val);
  }

  // Handle types that have the same size.
  if (PartEVT.getSizeInBits() == ValueVT.getSizeInBits())
    return DAG.getNode(ISD::BITCAST, DL, ValueVT, Val);

  // Handle types with different sizes.
  if (PartEVT.isInteger() && ValueVT.isInteger()) {
    if (ValueVT.bitsLT(PartEVT)) {
      // For a truncate, see if we have any information to
      // indicate whether the truncated bits will always be
      // zero or sign-extension.
      if (AssertOp.hasValue())
        Val = DAG.getNode(*AssertOp, DL, PartEVT, Val,
                          DAG.getValueType(ValueVT));
      return DAG.getNode(ISD::TRUNCATE, DL, ValueVT, Val);
    }
    return DAG.getNode(ISD::ANY_EXTEND, DL, ValueVT, Val);
  }

  if (PartEVT.isFloatingPoint() && ValueVT.isFloatingPoint()) {
    // FP_ROUND's are always exact here.
    if (ValueVT.bitsLT(Val.getValueType()))
      return DAG.getNode(
          ISD::FP_ROUND, DL, ValueVT, Val,
          DAG.getTargetConstant(1, DL, TLI.getPointerTy(DAG.getDataLayout())));

    return DAG.getNode(ISD::FP_EXTEND, DL, ValueVT, Val);
  }

  llvm_unreachable("Unknown mismatch!");
}

static void diagnosePossiblyInvalidConstraint(LLVMContext &Ctx, const Value *V,
                                              const Twine &ErrMsg) {
  const Instruction *I = dyn_cast_or_null<Instruction>(V);
  if (!V)
    return Ctx.emitError(ErrMsg);

  const char *AsmError = ", possible invalid constraint for vector type";
  if (const CallInst *CI = dyn_cast<CallInst>(I))
    if (isa<InlineAsm>(CI->getCalledValue()))
      return Ctx.emitError(I, ErrMsg + AsmError);

  return Ctx.emitError(I, ErrMsg);
}

/// getCopyFromPartsVector - Create a value that contains the specified legal
/// parts combined into the value they represent.  If the parts combine to a
/// type larger than ValueVT then AssertOp can be used to specify whether the
/// extra bits are known to be zero (ISD::AssertZext) or sign extended from
/// ValueVT (ISD::AssertSext).
static SDValue getCopyFromPartsVector(SelectionDAG &DAG, const SDLoc &DL,
                                      const SDValue *Parts, unsigned NumParts,
                                      MVT PartVT, EVT ValueVT, const Value *V,
                                      Optional<CallingConv::ID> CallConv) {
  assert(ValueVT.isVector() && "Not a vector value");
  assert(NumParts > 0 && "No parts to assemble!");
  const bool IsABIRegCopy = CallConv.hasValue();

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  SDValue Val = Parts[0];

  // Handle a multi-element vector.
  if (NumParts > 1) {
    EVT IntermediateVT;
    MVT RegisterVT;
    unsigned NumIntermediates;
    unsigned NumRegs;

    if (IsABIRegCopy) {
      NumRegs = TLI.getVectorTypeBreakdownForCallingConv(
          *DAG.getContext(), CallConv.getValue(), ValueVT, IntermediateVT,
          NumIntermediates, RegisterVT);
    } else {
      NumRegs =
          TLI.getVectorTypeBreakdown(*DAG.getContext(), ValueVT, IntermediateVT,
                                     NumIntermediates, RegisterVT);
    }

    assert(NumRegs == NumParts && "Part count doesn't match vector breakdown!");
    NumParts = NumRegs; // Silence a compiler warning.
    assert(RegisterVT == PartVT && "Part type doesn't match vector breakdown!");
    assert(RegisterVT.getSizeInBits() ==
           Parts[0].getSimpleValueType().getSizeInBits() &&
           "Part type sizes don't match!");

    // Assemble the parts into intermediate operands.
    SmallVector<SDValue, 8> Ops(NumIntermediates);
    if (NumIntermediates == NumParts) {
      // If the register was not expanded, truncate or copy the value,
      // as appropriate.
      for (unsigned i = 0; i != NumParts; ++i)
        Ops[i] = getCopyFromParts(DAG, DL, &Parts[i], 1,
                                  PartVT, IntermediateVT, V);
    } else if (NumParts > 0) {
      // If the intermediate type was expanded, build the intermediate
      // operands from the parts.
      assert(NumParts % NumIntermediates == 0 &&
             "Must expand into a divisible number of parts!");
      unsigned Factor = NumParts / NumIntermediates;
      for (unsigned i = 0; i != NumIntermediates; ++i)
        Ops[i] = getCopyFromParts(DAG, DL, &Parts[i * Factor], Factor,
                                  PartVT, IntermediateVT, V);
    }

    // Build a vector with BUILD_VECTOR or CONCAT_VECTORS from the
    // intermediate operands.
    EVT BuiltVectorTy =
        EVT::getVectorVT(*DAG.getContext(), IntermediateVT.getScalarType(),
                         (IntermediateVT.isVector()
                              ? IntermediateVT.getVectorNumElements() * NumParts
                              : NumIntermediates));
    Val = DAG.getNode(IntermediateVT.isVector() ? ISD::CONCAT_VECTORS
                                                : ISD::BUILD_VECTOR,
                      DL, BuiltVectorTy, Ops);
  }

  // There is now one part, held in Val.  Correct it to match ValueVT.
  EVT PartEVT = Val.getValueType();

  if (PartEVT == ValueVT)
    return Val;

  if (PartEVT.isVector()) {
    // If the element type of the source/dest vectors are the same, but the
    // parts vector has more elements than the value vector, then we have a
    // vector widening case (e.g. <2 x float> -> <4 x float>).  Extract the
    // elements we want.
    if (PartEVT.getVectorElementType() == ValueVT.getVectorElementType()) {
      assert(PartEVT.getVectorNumElements() > ValueVT.getVectorNumElements() &&
             "Cannot narrow, it would be a lossy transformation");
      return DAG.getNode(
          ISD::EXTRACT_SUBVECTOR, DL, ValueVT, Val,
          DAG.getConstant(0, DL, TLI.getVectorIdxTy(DAG.getDataLayout())));
    }

    // Vector/Vector bitcast.
    if (ValueVT.getSizeInBits() == PartEVT.getSizeInBits())
      return DAG.getNode(ISD::BITCAST, DL, ValueVT, Val);

    assert(PartEVT.getVectorNumElements() == ValueVT.getVectorNumElements() &&
      "Cannot handle this kind of promotion");
    // Promoted vector extract
    return DAG.getAnyExtOrTrunc(Val, DL, ValueVT);

  }

  // Trivial bitcast if the types are the same size and the destination
  // vector type is legal.
  if (PartEVT.getSizeInBits() == ValueVT.getSizeInBits() &&
      TLI.isTypeLegal(ValueVT))
    return DAG.getNode(ISD::BITCAST, DL, ValueVT, Val);

  if (ValueVT.getVectorNumElements() != 1) {
     // Certain ABIs require that vectors are passed as integers. For vectors
     // are the same size, this is an obvious bitcast.
     if (ValueVT.getSizeInBits() == PartEVT.getSizeInBits()) {
       return DAG.getNode(ISD::BITCAST, DL, ValueVT, Val);
     } else if (ValueVT.getSizeInBits() < PartEVT.getSizeInBits()) {
       // Bitcast Val back the original type and extract the corresponding
       // vector we want.
       unsigned Elts = PartEVT.getSizeInBits() / ValueVT.getScalarSizeInBits();
       EVT WiderVecType = EVT::getVectorVT(*DAG.getContext(),
                                           ValueVT.getVectorElementType(), Elts);
       Val = DAG.getBitcast(WiderVecType, Val);
       return DAG.getNode(
           ISD::EXTRACT_SUBVECTOR, DL, ValueVT, Val,
           DAG.getConstant(0, DL, TLI.getVectorIdxTy(DAG.getDataLayout())));
     }

     diagnosePossiblyInvalidConstraint(
         *DAG.getContext(), V, "non-trivial scalar-to-vector conversion");
     return DAG.getUNDEF(ValueVT);
  }

  // Handle cases such as i8 -> <1 x i1>
  EVT ValueSVT = ValueVT.getVectorElementType();
  if (ValueVT.getVectorNumElements() == 1 && ValueSVT != PartEVT)
    Val = ValueVT.isFloatingPoint() ? DAG.getFPExtendOrRound(Val, DL, ValueSVT)
                                    : DAG.getAnyExtOrTrunc(Val, DL, ValueSVT);

  return DAG.getBuildVector(ValueVT, DL, Val);
}

static void getCopyToPartsVector(SelectionDAG &DAG, const SDLoc &dl,
                                 SDValue Val, SDValue *Parts, unsigned NumParts,
                                 MVT PartVT, const Value *V,
                                 Optional<CallingConv::ID> CallConv);

/// getCopyToParts - Create a series of nodes that contain the specified value
/// split into legal parts.  If the parts contain more bits than Val, then, for
/// integers, ExtendKind can be used to specify how to generate the extra bits.
static void getCopyToParts(SelectionDAG &DAG, const SDLoc &DL, SDValue Val,
                           SDValue *Parts, unsigned NumParts, MVT PartVT,
                           const Value *V,
                           Optional<CallingConv::ID> CallConv = None,
                           ISD::NodeType ExtendKind = ISD::ANY_EXTEND) {
  EVT ValueVT = Val.getValueType();

  // Handle the vector case separately.
  if (ValueVT.isVector())
    return getCopyToPartsVector(DAG, DL, Val, Parts, NumParts, PartVT, V,
                                CallConv);

  unsigned PartBits = PartVT.getSizeInBits();
  unsigned OrigNumParts = NumParts;
  assert(DAG.getTargetLoweringInfo().isTypeLegal(PartVT) &&
         "Copying to an illegal type!");

  if (NumParts == 0)
    return;

  assert(!ValueVT.isVector() && "Vector case handled elsewhere");
  EVT PartEVT = PartVT;
  if (PartEVT == ValueVT) {
    assert(NumParts == 1 && "No-op copy with multiple parts!");
    Parts[0] = Val;
    return;
  }

  if (NumParts * PartBits > ValueVT.getSizeInBits()) {
    // If the parts cover more bits than the value has, promote the value.
    if (PartVT.isFloatingPoint() && ValueVT.isFloatingPoint()) {
      assert(NumParts == 1 && "Do not know what to promote to!");
      Val = DAG.getNode(ISD::FP_EXTEND, DL, PartVT, Val);
    } else {
      if (ValueVT.isFloatingPoint()) {
        // FP values need to be bitcast, then extended if they are being put
        // into a larger container.
        ValueVT = EVT::getIntegerVT(*DAG.getContext(),  ValueVT.getSizeInBits());
        Val = DAG.getNode(ISD::BITCAST, DL, ValueVT, Val);
      }
      assert((PartVT.isInteger() || PartVT == MVT::x86mmx) &&
             ValueVT.isInteger() &&
             "Unknown mismatch!");
      ValueVT = EVT::getIntegerVT(*DAG.getContext(), NumParts * PartBits);
      Val = DAG.getNode(ExtendKind, DL, ValueVT, Val);
      if (PartVT == MVT::x86mmx)
        Val = DAG.getNode(ISD::BITCAST, DL, PartVT, Val);
    }
  } else if (PartBits == ValueVT.getSizeInBits()) {
    // Different types of the same size.
    assert(NumParts == 1 && PartEVT != ValueVT);
    Val = DAG.getNode(ISD::BITCAST, DL, PartVT, Val);
  } else if (NumParts * PartBits < ValueVT.getSizeInBits()) {
    // If the parts cover less bits than value has, truncate the value.
    assert((PartVT.isInteger() || PartVT == MVT::x86mmx) &&
           ValueVT.isInteger() &&
           "Unknown mismatch!");
    ValueVT = EVT::getIntegerVT(*DAG.getContext(), NumParts * PartBits);
    Val = DAG.getNode(ISD::TRUNCATE, DL, ValueVT, Val);
    if (PartVT == MVT::x86mmx)
      Val = DAG.getNode(ISD::BITCAST, DL, PartVT, Val);
  }

  // The value may have changed - recompute ValueVT.
  ValueVT = Val.getValueType();
  assert(NumParts * PartBits == ValueVT.getSizeInBits() &&
         "Failed to tile the value with PartVT!");

  if (NumParts == 1) {
    if (PartEVT != ValueVT) {
      diagnosePossiblyInvalidConstraint(*DAG.getContext(), V,
                                        "scalar-to-vector conversion failed");
      Val = DAG.getNode(ISD::BITCAST, DL, PartVT, Val);
    }

    Parts[0] = Val;
    return;
  }

  // Expand the value into multiple parts.
  if (NumParts & (NumParts - 1)) {
    // The number of parts is not a power of 2.  Split off and copy the tail.
    assert(PartVT.isInteger() && ValueVT.isInteger() &&
           "Do not know what to expand to!");
    unsigned RoundParts = 1 << Log2_32(NumParts);
    unsigned RoundBits = RoundParts * PartBits;
    unsigned OddParts = NumParts - RoundParts;
    SDValue OddVal = DAG.getNode(ISD::SRL, DL, ValueVT, Val,
                                 DAG.getIntPtrConstant(RoundBits, DL));
    getCopyToParts(DAG, DL, OddVal, Parts + RoundParts, OddParts, PartVT, V,
                   CallConv);

    if (DAG.getDataLayout().isBigEndian())
      // The odd parts were reversed by getCopyToParts - unreverse them.
      std::reverse(Parts + RoundParts, Parts + NumParts);

    NumParts = RoundParts;
    ValueVT = EVT::getIntegerVT(*DAG.getContext(), NumParts * PartBits);
    Val = DAG.getNode(ISD::TRUNCATE, DL, ValueVT, Val);
  }

  // The number of parts is a power of 2.  Repeatedly bisect the value using
  // EXTRACT_ELEMENT.
  Parts[0] = DAG.getNode(ISD::BITCAST, DL,
                         EVT::getIntegerVT(*DAG.getContext(),
                                           ValueVT.getSizeInBits()),
                         Val);

  for (unsigned StepSize = NumParts; StepSize > 1; StepSize /= 2) {
    for (unsigned i = 0; i < NumParts; i += StepSize) {
      unsigned ThisBits = StepSize * PartBits / 2;
      EVT ThisVT = EVT::getIntegerVT(*DAG.getContext(), ThisBits);
      SDValue &Part0 = Parts[i];
      SDValue &Part1 = Parts[i+StepSize/2];

      Part1 = DAG.getNode(ISD::EXTRACT_ELEMENT, DL,
                          ThisVT, Part0, DAG.getIntPtrConstant(1, DL));
      Part0 = DAG.getNode(ISD::EXTRACT_ELEMENT, DL,
                          ThisVT, Part0, DAG.getIntPtrConstant(0, DL));

      if (ThisBits == PartBits && ThisVT != PartVT) {
        Part0 = DAG.getNode(ISD::BITCAST, DL, PartVT, Part0);
        Part1 = DAG.getNode(ISD::BITCAST, DL, PartVT, Part1);
      }
    }
  }

  if (DAG.getDataLayout().isBigEndian())
    std::reverse(Parts, Parts + OrigNumParts);
}

static SDValue widenVectorToPartType(SelectionDAG &DAG,
                                     SDValue Val, const SDLoc &DL, EVT PartVT) {
  if (!PartVT.isVector())
    return SDValue();

  EVT ValueVT = Val.getValueType();
  unsigned PartNumElts = PartVT.getVectorNumElements();
  unsigned ValueNumElts = ValueVT.getVectorNumElements();
  if (PartNumElts > ValueNumElts &&
      PartVT.getVectorElementType() == ValueVT.getVectorElementType()) {
    EVT ElementVT = PartVT.getVectorElementType();
    // Vector widening case, e.g. <2 x float> -> <4 x float>.  Shuffle in
    // undef elements.
    SmallVector<SDValue, 16> Ops;
    DAG.ExtractVectorElements(Val, Ops);
    SDValue EltUndef = DAG.getUNDEF(ElementVT);
    for (unsigned i = ValueNumElts, e = PartNumElts; i != e; ++i)
      Ops.push_back(EltUndef);

    // FIXME: Use CONCAT for 2x -> 4x.
    return DAG.getBuildVector(PartVT, DL, Ops);
  }

  return SDValue();
}

/// getCopyToPartsVector - Create a series of nodes that contain the specified
/// value split into legal parts.
static void getCopyToPartsVector(SelectionDAG &DAG, const SDLoc &DL,
                                 SDValue Val, SDValue *Parts, unsigned NumParts,
                                 MVT PartVT, const Value *V,
                                 Optional<CallingConv::ID> CallConv) {
  EVT ValueVT = Val.getValueType();
  assert(ValueVT.isVector() && "Not a vector");
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  const bool IsABIRegCopy = CallConv.hasValue();

  if (NumParts == 1) {
    EVT PartEVT = PartVT;
    if (PartEVT == ValueVT) {
      // Nothing to do.
    } else if (PartVT.getSizeInBits() == ValueVT.getSizeInBits()) {
      // Bitconvert vector->vector case.
      Val = DAG.getNode(ISD::BITCAST, DL, PartVT, Val);
    } else if (SDValue Widened = widenVectorToPartType(DAG, Val, DL, PartVT)) {
      Val = Widened;
    } else if (PartVT.isVector() &&
               PartEVT.getVectorElementType().bitsGE(
                 ValueVT.getVectorElementType()) &&
               PartEVT.getVectorNumElements() == ValueVT.getVectorNumElements()) {

      // Promoted vector extract
      Val = DAG.getAnyExtOrTrunc(Val, DL, PartVT);
    } else {
      if (ValueVT.getVectorNumElements() == 1) {
        Val = DAG.getNode(
            ISD::EXTRACT_VECTOR_ELT, DL, PartVT, Val,
            DAG.getConstant(0, DL, TLI.getVectorIdxTy(DAG.getDataLayout())));
      } else {
        assert(PartVT.getSizeInBits() > ValueVT.getSizeInBits() &&
               "lossy conversion of vector to scalar type");
        EVT IntermediateType =
            EVT::getIntegerVT(*DAG.getContext(), ValueVT.getSizeInBits());
        Val = DAG.getBitcast(IntermediateType, Val);
        Val = DAG.getAnyExtOrTrunc(Val, DL, PartVT);
      }
    }

    assert(Val.getValueType() == PartVT && "Unexpected vector part value type");
    Parts[0] = Val;
    return;
  }

  // Handle a multi-element vector.
  EVT IntermediateVT;
  MVT RegisterVT;
  unsigned NumIntermediates;
  unsigned NumRegs;
  if (IsABIRegCopy) {
    NumRegs = TLI.getVectorTypeBreakdownForCallingConv(
        *DAG.getContext(), CallConv.getValue(), ValueVT, IntermediateVT,
        NumIntermediates, RegisterVT);
  } else {
    NumRegs =
        TLI.getVectorTypeBreakdown(*DAG.getContext(), ValueVT, IntermediateVT,
                                   NumIntermediates, RegisterVT);
  }

  assert(NumRegs == NumParts && "Part count doesn't match vector breakdown!");
  NumParts = NumRegs; // Silence a compiler warning.
  assert(RegisterVT == PartVT && "Part type doesn't match vector breakdown!");

  unsigned IntermediateNumElts = IntermediateVT.isVector() ?
    IntermediateVT.getVectorNumElements() : 1;

  // Convert the vector to the appropiate type if necessary.
  unsigned DestVectorNoElts = NumIntermediates * IntermediateNumElts;

  EVT BuiltVectorTy = EVT::getVectorVT(
      *DAG.getContext(), IntermediateVT.getScalarType(), DestVectorNoElts);
  MVT IdxVT = TLI.getVectorIdxTy(DAG.getDataLayout());
  if (ValueVT != BuiltVectorTy) {
    if (SDValue Widened = widenVectorToPartType(DAG, Val, DL, BuiltVectorTy))
      Val = Widened;

    Val = DAG.getNode(ISD::BITCAST, DL, BuiltVectorTy, Val);
  }

  // Split the vector into intermediate operands.
  SmallVector<SDValue, 8> Ops(NumIntermediates);
  for (unsigned i = 0; i != NumIntermediates; ++i) {
    if (IntermediateVT.isVector()) {
      Ops[i] = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, IntermediateVT, Val,
                           DAG.getConstant(i * IntermediateNumElts, DL, IdxVT));
    } else {
      Ops[i] = DAG.getNode(
          ISD::EXTRACT_VECTOR_ELT, DL, IntermediateVT, Val,
          DAG.getConstant(i, DL, IdxVT));
    }
  }

  // Split the intermediate operands into legal parts.
  if (NumParts == NumIntermediates) {
    // If the register was not expanded, promote or copy the value,
    // as appropriate.
    for (unsigned i = 0; i != NumParts; ++i)
      getCopyToParts(DAG, DL, Ops[i], &Parts[i], 1, PartVT, V, CallConv);
  } else if (NumParts > 0) {
    // If the intermediate type was expanded, split each the value into
    // legal parts.
    assert(NumIntermediates != 0 && "division by zero");
    assert(NumParts % NumIntermediates == 0 &&
           "Must expand into a divisible number of parts!");
    unsigned Factor = NumParts / NumIntermediates;
    for (unsigned i = 0; i != NumIntermediates; ++i)
      getCopyToParts(DAG, DL, Ops[i], &Parts[i * Factor], Factor, PartVT, V,
                     CallConv);
  }
}

RegsForValue::RegsForValue(const SmallVector<unsigned, 4> &regs, MVT regvt,
                           EVT valuevt, Optional<CallingConv::ID> CC)
    : ValueVTs(1, valuevt), RegVTs(1, regvt), Regs(regs),
      RegCount(1, regs.size()), CallConv(CC) {}

RegsForValue::RegsForValue(LLVMContext &Context, const TargetLowering &TLI,
                           const DataLayout &DL, unsigned Reg, Type *Ty,
                           Optional<CallingConv::ID> CC) {
  ComputeValueVTs(TLI, DL, Ty, ValueVTs);

  CallConv = CC;

  for (EVT ValueVT : ValueVTs) {
    unsigned NumRegs =
        isABIMangled()
            ? TLI.getNumRegistersForCallingConv(Context, CC.getValue(), ValueVT)
            : TLI.getNumRegisters(Context, ValueVT);
    MVT RegisterVT =
        isABIMangled()
            ? TLI.getRegisterTypeForCallingConv(Context, CC.getValue(), ValueVT)
            : TLI.getRegisterType(Context, ValueVT);
    for (unsigned i = 0; i != NumRegs; ++i)
      Regs.push_back(Reg + i);
    RegVTs.push_back(RegisterVT);
    RegCount.push_back(NumRegs);
    Reg += NumRegs;
  }
}

SDValue RegsForValue::getCopyFromRegs(SelectionDAG &DAG,
                                      FunctionLoweringInfo &FuncInfo,
                                      const SDLoc &dl, SDValue &Chain,
                                      SDValue *Flag, const Value *V) const {
  // A Value with type {} or [0 x %t] needs no registers.
  if (ValueVTs.empty())
    return SDValue();

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();

  // Assemble the legal parts into the final values.
  SmallVector<SDValue, 4> Values(ValueVTs.size());
  SmallVector<SDValue, 8> Parts;
  for (unsigned Value = 0, Part = 0, e = ValueVTs.size(); Value != e; ++Value) {
    // Copy the legal parts from the registers.
    EVT ValueVT = ValueVTs[Value];
    unsigned NumRegs = RegCount[Value];
    MVT RegisterVT = isABIMangled() ? TLI.getRegisterTypeForCallingConv(
                                          *DAG.getContext(),
                                          CallConv.getValue(), RegVTs[Value])
                                    : RegVTs[Value];

    Parts.resize(NumRegs);
    for (unsigned i = 0; i != NumRegs; ++i) {
      SDValue P;
      if (!Flag) {
        P = DAG.getCopyFromReg(Chain, dl, Regs[Part+i], RegisterVT);
      } else {
        P = DAG.getCopyFromReg(Chain, dl, Regs[Part+i], RegisterVT, *Flag);
        *Flag = P.getValue(2);
      }

      Chain = P.getValue(1);
      Parts[i] = P;

      // If the source register was virtual and if we know something about it,
      // add an assert node.
      if (!TargetRegisterInfo::isVirtualRegister(Regs[Part+i]) ||
          !RegisterVT.isInteger())
        continue;

      const FunctionLoweringInfo::LiveOutInfo *LOI =
        FuncInfo.GetLiveOutRegInfo(Regs[Part+i]);
      if (!LOI)
        continue;

      unsigned RegSize = RegisterVT.getScalarSizeInBits();
      unsigned NumSignBits = LOI->NumSignBits;
      unsigned NumZeroBits = LOI->Known.countMinLeadingZeros();

      if (NumZeroBits == RegSize) {
        // The current value is a zero.
        // Explicitly express that as it would be easier for
        // optimizations to kick in.
        Parts[i] = DAG.getConstant(0, dl, RegisterVT);
        continue;
      }

      // FIXME: We capture more information than the dag can represent.  For
      // now, just use the tightest assertzext/assertsext possible.
      bool isSExt;
      EVT FromVT(MVT::Other);
      if (NumZeroBits) {
        FromVT = EVT::getIntegerVT(*DAG.getContext(), RegSize - NumZeroBits);
        isSExt = false;
      } else if (NumSignBits > 1) {
        FromVT =
            EVT::getIntegerVT(*DAG.getContext(), RegSize - NumSignBits + 1);
        isSExt = true;
      } else {
        continue;
      }
      // Add an assertion node.
      assert(FromVT != MVT::Other);
      Parts[i] = DAG.getNode(isSExt ? ISD::AssertSext : ISD::AssertZext, dl,
                             RegisterVT, P, DAG.getValueType(FromVT));
    }

    Values[Value] = getCopyFromParts(DAG, dl, Parts.begin(), NumRegs,
                                     RegisterVT, ValueVT, V, CallConv);
    Part += NumRegs;
    Parts.clear();
  }

  return DAG.getNode(ISD::MERGE_VALUES, dl, DAG.getVTList(ValueVTs), Values);
}

void RegsForValue::getCopyToRegs(SDValue Val, SelectionDAG &DAG,
                                 const SDLoc &dl, SDValue &Chain, SDValue *Flag,
                                 const Value *V,
                                 ISD::NodeType PreferredExtendType) const {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  ISD::NodeType ExtendKind = PreferredExtendType;

  // Get the list of the values's legal parts.
  unsigned NumRegs = Regs.size();
  SmallVector<SDValue, 8> Parts(NumRegs);
  for (unsigned Value = 0, Part = 0, e = ValueVTs.size(); Value != e; ++Value) {
    unsigned NumParts = RegCount[Value];

    MVT RegisterVT = isABIMangled() ? TLI.getRegisterTypeForCallingConv(
                                          *DAG.getContext(),
                                          CallConv.getValue(), RegVTs[Value])
                                    : RegVTs[Value];

    if (ExtendKind == ISD::ANY_EXTEND && TLI.isZExtFree(Val, RegisterVT))
      ExtendKind = ISD::ZERO_EXTEND;

    getCopyToParts(DAG, dl, Val.getValue(Val.getResNo() + Value), &Parts[Part],
                   NumParts, RegisterVT, V, CallConv, ExtendKind);
    Part += NumParts;
  }

  // Copy the parts into the registers.
  SmallVector<SDValue, 8> Chains(NumRegs);
  for (unsigned i = 0; i != NumRegs; ++i) {
    SDValue Part;
    if (!Flag) {
      Part = DAG.getCopyToReg(Chain, dl, Regs[i], Parts[i]);
    } else {
      Part = DAG.getCopyToReg(Chain, dl, Regs[i], Parts[i], *Flag);
      *Flag = Part.getValue(1);
    }

    Chains[i] = Part.getValue(0);
  }

  if (NumRegs == 1 || Flag)
    // If NumRegs > 1 && Flag is used then the use of the last CopyToReg is
    // flagged to it. That is the CopyToReg nodes and the user are considered
    // a single scheduling unit. If we create a TokenFactor and return it as
    // chain, then the TokenFactor is both a predecessor (operand) of the
    // user as well as a successor (the TF operands are flagged to the user).
    // c1, f1 = CopyToReg
    // c2, f2 = CopyToReg
    // c3     = TokenFactor c1, c2
    // ...
    //        = op c3, ..., f2
    Chain = Chains[NumRegs-1];
  else
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Chains);
}

void RegsForValue::AddInlineAsmOperands(unsigned Code, bool HasMatching,
                                        unsigned MatchingIdx, const SDLoc &dl,
                                        SelectionDAG &DAG,
                                        std::vector<SDValue> &Ops) const {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();

  unsigned Flag = InlineAsm::getFlagWord(Code, Regs.size());
  if (HasMatching)
    Flag = InlineAsm::getFlagWordForMatchingOp(Flag, MatchingIdx);
  else if (!Regs.empty() &&
           TargetRegisterInfo::isVirtualRegister(Regs.front())) {
    // Put the register class of the virtual registers in the flag word.  That
    // way, later passes can recompute register class constraints for inline
    // assembly as well as normal instructions.
    // Don't do this for tied operands that can use the regclass information
    // from the def.
    const MachineRegisterInfo &MRI = DAG.getMachineFunction().getRegInfo();
    const TargetRegisterClass *RC = MRI.getRegClass(Regs.front());
    Flag = InlineAsm::getFlagWordForRegClass(Flag, RC->getID());
  }

  SDValue Res = DAG.getTargetConstant(Flag, dl, MVT::i32);
  Ops.push_back(Res);

  if (Code == InlineAsm::Kind_Clobber) {
    // Clobbers should always have a 1:1 mapping with registers, and may
    // reference registers that have illegal (e.g. vector) types. Hence, we
    // shouldn't try to apply any sort of splitting logic to them.
    assert(Regs.size() == RegVTs.size() && Regs.size() == ValueVTs.size() &&
           "No 1:1 mapping from clobbers to regs?");
    unsigned SP = TLI.getStackPointerRegisterToSaveRestore();
    (void)SP;
    for (unsigned I = 0, E = ValueVTs.size(); I != E; ++I) {
      Ops.push_back(DAG.getRegister(Regs[I], RegVTs[I]));
      assert(
          (Regs[I] != SP ||
           DAG.getMachineFunction().getFrameInfo().hasOpaqueSPAdjustment()) &&
          "If we clobbered the stack pointer, MFI should know about it.");
    }
    return;
  }

  for (unsigned Value = 0, Reg = 0, e = ValueVTs.size(); Value != e; ++Value) {
    unsigned NumRegs = TLI.getNumRegisters(*DAG.getContext(), ValueVTs[Value]);
    MVT RegisterVT = RegVTs[Value];
    for (unsigned i = 0; i != NumRegs; ++i) {
      assert(Reg < Regs.size() && "Mismatch in # registers expected");
      unsigned TheReg = Regs[Reg++];
      Ops.push_back(DAG.getRegister(TheReg, RegisterVT));
    }
  }
}

SmallVector<std::pair<unsigned, unsigned>, 4>
RegsForValue::getRegsAndSizes() const {
  SmallVector<std::pair<unsigned, unsigned>, 4> OutVec;
  unsigned I = 0;
  for (auto CountAndVT : zip_first(RegCount, RegVTs)) {
    unsigned RegCount = std::get<0>(CountAndVT);
    MVT RegisterVT = std::get<1>(CountAndVT);
    unsigned RegisterSize = RegisterVT.getSizeInBits();
    for (unsigned E = I + RegCount; I != E; ++I)
      OutVec.push_back(std::make_pair(Regs[I], RegisterSize));
  }
  return OutVec;
}

void SelectionDAGBuilder::init(GCFunctionInfo *gfi, AliasAnalysis *aa,
                               const TargetLibraryInfo *li) {
  AA = aa;
  GFI = gfi;
  LibInfo = li;
  DL = &DAG.getDataLayout();
  Context = DAG.getContext();
  LPadToCallSiteMap.clear();
}

void SelectionDAGBuilder::clear() {
  NodeMap.clear();
  UnusedArgNodeMap.clear();
  PendingLoads.clear();
  PendingExports.clear();
  CurInst = nullptr;
  HasTailCall = false;
  SDNodeOrder = LowestSDNodeOrder;
  StatepointLowering.clear();
}

void SelectionDAGBuilder::clearDanglingDebugInfo() {
  DanglingDebugInfoMap.clear();
}

SDValue SelectionDAGBuilder::getRoot() {
  if (PendingLoads.empty())
    return DAG.getRoot();

  if (PendingLoads.size() == 1) {
    SDValue Root = PendingLoads[0];
    DAG.setRoot(Root);
    PendingLoads.clear();
    return Root;
  }

  // Otherwise, we have to make a token factor node.
  // If we have >= 2^16 loads then split across multiple token factors as
  // there's a 64k limit on the number of SDNode operands.
  SDValue Root;
  size_t Limit = (1 << 16) - 1;
  while (PendingLoads.size() > Limit) {
    unsigned SliceIdx = PendingLoads.size() - Limit;
    auto ExtractedTFs = ArrayRef<SDValue>(PendingLoads).slice(SliceIdx, Limit);
    SDValue NewTF =
        DAG.getNode(ISD::TokenFactor, getCurSDLoc(), MVT::Other, ExtractedTFs);
    PendingLoads.erase(PendingLoads.begin() + SliceIdx, PendingLoads.end());
    PendingLoads.emplace_back(NewTF);
  }
  Root = DAG.getNode(ISD::TokenFactor, getCurSDLoc(), MVT::Other, PendingLoads);
  PendingLoads.clear();
  DAG.setRoot(Root);
  return Root;
}

SDValue SelectionDAGBuilder::getControlRoot() {
  SDValue Root = DAG.getRoot();

  if (PendingExports.empty())
    return Root;

  // Turn all of the CopyToReg chains into one factored node.
  if (Root.getOpcode() != ISD::EntryToken) {
    unsigned i = 0, e = PendingExports.size();
    for (; i != e; ++i) {
      assert(PendingExports[i].getNode()->getNumOperands() > 1);
      if (PendingExports[i].getNode()->getOperand(0) == Root)
        break;  // Don't add the root if we already indirectly depend on it.
    }

    if (i == e)
      PendingExports.push_back(Root);
  }

  Root = DAG.getNode(ISD::TokenFactor, getCurSDLoc(), MVT::Other,
                     PendingExports);
  PendingExports.clear();
  DAG.setRoot(Root);
  return Root;
}

void SelectionDAGBuilder::visit(const Instruction &I) {
  // Set up outgoing PHI node register values before emitting the terminator.
  if (I.isTerminator()) {
    HandlePHINodesInSuccessorBlocks(I.getParent());
  }

  // Increase the SDNodeOrder if dealing with a non-debug instruction.
  if (!isa<DbgInfoIntrinsic>(I))
    ++SDNodeOrder;

  CurInst = &I;

  visit(I.getOpcode(), I);

  if (auto *FPMO = dyn_cast<FPMathOperator>(&I)) {
    // Propagate the fast-math-flags of this IR instruction to the DAG node that
    // maps to this instruction.
    // TODO: We could handle all flags (nsw, etc) here.
    // TODO: If an IR instruction maps to >1 node, only the final node will have
    //       flags set.
    if (SDNode *Node = getNodeForIRValue(&I)) {
      SDNodeFlags IncomingFlags;
      IncomingFlags.copyFMF(*FPMO);
      if (!Node->getFlags().isDefined())
        Node->setFlags(IncomingFlags);
      else
        Node->intersectFlagsWith(IncomingFlags);
    }
  }

  if (!I.isTerminator() && !HasTailCall &&
      !isStatepoint(&I)) // statepoints handle their exports internally
    CopyToExportRegsIfNeeded(&I);

  CurInst = nullptr;
}

void SelectionDAGBuilder::visitPHI(const PHINode &) {
  llvm_unreachable("SelectionDAGBuilder shouldn't visit PHI nodes!");
}

void SelectionDAGBuilder::visit(unsigned Opcode, const User &I) {
  // Note: this doesn't use InstVisitor, because it has to work with
  // ConstantExpr's in addition to instructions.
  switch (Opcode) {
  default: llvm_unreachable("Unknown instruction type encountered!");
    // Build the switch statement using the Instruction.def file.
#define HANDLE_INST(NUM, OPCODE, CLASS) \
    case Instruction::OPCODE: visit##OPCODE((const CLASS&)I); break;
#include "llvm/IR/Instruction.def"
  }
}

void SelectionDAGBuilder::dropDanglingDebugInfo(const DILocalVariable *Variable,
                                                const DIExpression *Expr) {
  auto isMatchingDbgValue = [&](DanglingDebugInfo &DDI) {
    const DbgValueInst *DI = DDI.getDI();
    DIVariable *DanglingVariable = DI->getVariable();
    DIExpression *DanglingExpr = DI->getExpression();
    if (DanglingVariable == Variable && Expr->fragmentsOverlap(DanglingExpr)) {
      LLVM_DEBUG(dbgs() << "Dropping dangling debug info for " << *DI << "\n");
      return true;
    }
    return false;
  };

  for (auto &DDIMI : DanglingDebugInfoMap) {
    DanglingDebugInfoVector &DDIV = DDIMI.second;
    DDIV.erase(remove_if(DDIV, isMatchingDbgValue), DDIV.end());
  }
}

// resolveDanglingDebugInfo - if we saw an earlier dbg_value referring to V,
// generate the debug data structures now that we've seen its definition.
void SelectionDAGBuilder::resolveDanglingDebugInfo(const Value *V,
                                                   SDValue Val) {
  auto DanglingDbgInfoIt = DanglingDebugInfoMap.find(V);
  if (DanglingDbgInfoIt == DanglingDebugInfoMap.end())
    return;

  DanglingDebugInfoVector &DDIV = DanglingDbgInfoIt->second;
  for (auto &DDI : DDIV) {
    const DbgValueInst *DI = DDI.getDI();
    assert(DI && "Ill-formed DanglingDebugInfo");
    DebugLoc dl = DDI.getdl();
    unsigned ValSDNodeOrder = Val.getNode()->getIROrder();
    unsigned DbgSDNodeOrder = DDI.getSDNodeOrder();
    DILocalVariable *Variable = DI->getVariable();
    DIExpression *Expr = DI->getExpression();
    assert(Variable->isValidLocationForIntrinsic(dl) &&
           "Expected inlined-at fields to agree");
    SDDbgValue *SDV;
    if (Val.getNode()) {
      if (!EmitFuncArgumentDbgValue(V, Variable, Expr, dl, false, Val)) {
        LLVM_DEBUG(dbgs() << "Resolve dangling debug info [order="
                          << DbgSDNodeOrder << "] for:\n  " << *DI << "\n");
        LLVM_DEBUG(dbgs() << "  By mapping to:\n    "; Val.dump());
        // Increase the SDNodeOrder for the DbgValue here to make sure it is
        // inserted after the definition of Val when emitting the instructions
        // after ISel. An alternative could be to teach
        // ScheduleDAGSDNodes::EmitSchedule to delay the insertion properly.
        LLVM_DEBUG(if (ValSDNodeOrder > DbgSDNodeOrder) dbgs()
                   << "changing SDNodeOrder from " << DbgSDNodeOrder << " to "
                   << ValSDNodeOrder << "\n");
        SDV = getDbgValue(Val, Variable, Expr, dl,
                          std::max(DbgSDNodeOrder, ValSDNodeOrder));
        DAG.AddDbgValue(SDV, Val.getNode(), false);
      } else
        LLVM_DEBUG(dbgs() << "Resolved dangling debug info for " << *DI
                          << "in EmitFuncArgumentDbgValue\n");
    } else
      LLVM_DEBUG(dbgs() << "Dropping debug info for " << *DI << "\n");
  }
  DDIV.clear();
}

/// getCopyFromRegs - If there was virtual register allocated for the value V
/// emit CopyFromReg of the specified type Ty. Return empty SDValue() otherwise.
SDValue SelectionDAGBuilder::getCopyFromRegs(const Value *V, Type *Ty) {
  DenseMap<const Value *, unsigned>::iterator It = FuncInfo.ValueMap.find(V);
  SDValue Result;

  if (It != FuncInfo.ValueMap.end()) {
    unsigned InReg = It->second;

    RegsForValue RFV(*DAG.getContext(), DAG.getTargetLoweringInfo(),
                     DAG.getDataLayout(), InReg, Ty,
                     None); // This is not an ABI copy.
    SDValue Chain = DAG.getEntryNode();
    Result = RFV.getCopyFromRegs(DAG, FuncInfo, getCurSDLoc(), Chain, nullptr,
                                 V);
    resolveDanglingDebugInfo(V, Result);
  }

  return Result;
}

/// getValue - Return an SDValue for the given Value.
SDValue SelectionDAGBuilder::getValue(const Value *V) {
  // If we already have an SDValue for this value, use it. It's important
  // to do this first, so that we don't create a CopyFromReg if we already
  // have a regular SDValue.
  SDValue &N = NodeMap[V];
  if (N.getNode()) return N;

  // If there's a virtual register allocated and initialized for this
  // value, use it.
  if (SDValue copyFromReg = getCopyFromRegs(V, V->getType()))
    return copyFromReg;

  // Otherwise create a new SDValue and remember it.
  SDValue Val = getValueImpl(V);
  NodeMap[V] = Val;
  resolveDanglingDebugInfo(V, Val);
  return Val;
}

// Return true if SDValue exists for the given Value
bool SelectionDAGBuilder::findValue(const Value *V) const {
  return (NodeMap.find(V) != NodeMap.end()) ||
    (FuncInfo.ValueMap.find(V) != FuncInfo.ValueMap.end());
}

/// getNonRegisterValue - Return an SDValue for the given Value, but
/// don't look in FuncInfo.ValueMap for a virtual register.
SDValue SelectionDAGBuilder::getNonRegisterValue(const Value *V) {
  // If we already have an SDValue for this value, use it.
  SDValue &N = NodeMap[V];
  if (N.getNode()) {
    if (isa<ConstantSDNode>(N) || isa<ConstantFPSDNode>(N)) {
      // Remove the debug location from the node as the node is about to be used
      // in a location which may differ from the original debug location.  This
      // is relevant to Constant and ConstantFP nodes because they can appear
      // as constant expressions inside PHI nodes.
      N->setDebugLoc(DebugLoc());
    }
    return N;
  }

  // Otherwise create a new SDValue and remember it.
  SDValue Val = getValueImpl(V);
  NodeMap[V] = Val;
  resolveDanglingDebugInfo(V, Val);
  return Val;
}

/// getValueImpl - Helper function for getValue and getNonRegisterValue.
/// Create an SDValue for the given value.
SDValue SelectionDAGBuilder::getValueImpl(const Value *V) {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();

  if (const Constant *C = dyn_cast<Constant>(V)) {
    EVT VT = TLI.getValueType(DAG.getDataLayout(), V->getType(), true);

    if (const ConstantInt *CI = dyn_cast<ConstantInt>(C))
      return DAG.getConstant(*CI, getCurSDLoc(), VT);

    if (const GlobalValue *GV = dyn_cast<GlobalValue>(C))
      return DAG.getGlobalAddress(GV, getCurSDLoc(), VT);

    if (isa<ConstantPointerNull>(C)) {
      unsigned AS = V->getType()->getPointerAddressSpace();
      return DAG.getConstant(0, getCurSDLoc(),
                             TLI.getPointerTy(DAG.getDataLayout(), AS));
    }

    if (const ConstantFP *CFP = dyn_cast<ConstantFP>(C))
      return DAG.getConstantFP(*CFP, getCurSDLoc(), VT);

    if (isa<UndefValue>(C) && !V->getType()->isAggregateType())
      return DAG.getUNDEF(VT);

    if (const ConstantExpr *CE = dyn_cast<ConstantExpr>(C)) {
      visit(CE->getOpcode(), *CE);
      SDValue N1 = NodeMap[V];
      assert(N1.getNode() && "visit didn't populate the NodeMap!");
      return N1;
    }

    if (isa<ConstantStruct>(C) || isa<ConstantArray>(C)) {
      SmallVector<SDValue, 4> Constants;
      for (User::const_op_iterator OI = C->op_begin(), OE = C->op_end();
           OI != OE; ++OI) {
        SDNode *Val = getValue(*OI).getNode();
        // If the operand is an empty aggregate, there are no values.
        if (!Val) continue;
        // Add each leaf value from the operand to the Constants list
        // to form a flattened list of all the values.
        for (unsigned i = 0, e = Val->getNumValues(); i != e; ++i)
          Constants.push_back(SDValue(Val, i));
      }

      return DAG.getMergeValues(Constants, getCurSDLoc());
    }

    if (const ConstantDataSequential *CDS =
          dyn_cast<ConstantDataSequential>(C)) {
      SmallVector<SDValue, 4> Ops;
      for (unsigned i = 0, e = CDS->getNumElements(); i != e; ++i) {
        SDNode *Val = getValue(CDS->getElementAsConstant(i)).getNode();
        // Add each leaf value from the operand to the Constants list
        // to form a flattened list of all the values.
        for (unsigned i = 0, e = Val->getNumValues(); i != e; ++i)
          Ops.push_back(SDValue(Val, i));
      }

      if (isa<ArrayType>(CDS->getType()))
        return DAG.getMergeValues(Ops, getCurSDLoc());
      return NodeMap[V] = DAG.getBuildVector(VT, getCurSDLoc(), Ops);
    }

    if (C->getType()->isStructTy() || C->getType()->isArrayTy()) {
      assert((isa<ConstantAggregateZero>(C) || isa<UndefValue>(C)) &&
             "Unknown struct or array constant!");

      SmallVector<EVT, 4> ValueVTs;
      ComputeValueVTs(TLI, DAG.getDataLayout(), C->getType(), ValueVTs);
      unsigned NumElts = ValueVTs.size();
      if (NumElts == 0)
        return SDValue(); // empty struct
      SmallVector<SDValue, 4> Constants(NumElts);
      for (unsigned i = 0; i != NumElts; ++i) {
        EVT EltVT = ValueVTs[i];
        if (isa<UndefValue>(C))
          Constants[i] = DAG.getUNDEF(EltVT);
        else if (EltVT.isFloatingPoint())
          Constants[i] = DAG.getConstantFP(0, getCurSDLoc(), EltVT);
        else
          Constants[i] = DAG.getConstant(0, getCurSDLoc(), EltVT);
      }

      return DAG.getMergeValues(Constants, getCurSDLoc());
    }

    if (const BlockAddress *BA = dyn_cast<BlockAddress>(C))
      return DAG.getBlockAddress(BA, VT);

    VectorType *VecTy = cast<VectorType>(V->getType());
    unsigned NumElements = VecTy->getNumElements();

    // Now that we know the number and type of the elements, get that number of
    // elements into the Ops array based on what kind of constant it is.
    SmallVector<SDValue, 16> Ops;
    if (const ConstantVector *CV = dyn_cast<ConstantVector>(C)) {
      for (unsigned i = 0; i != NumElements; ++i)
        Ops.push_back(getValue(CV->getOperand(i)));
    } else {
      assert(isa<ConstantAggregateZero>(C) && "Unknown vector constant!");
      EVT EltVT =
          TLI.getValueType(DAG.getDataLayout(), VecTy->getElementType());

      SDValue Op;
      if (EltVT.isFloatingPoint())
        Op = DAG.getConstantFP(0, getCurSDLoc(), EltVT);
      else
        Op = DAG.getConstant(0, getCurSDLoc(), EltVT);
      Ops.assign(NumElements, Op);
    }

    // Create a BUILD_VECTOR node.
    return NodeMap[V] = DAG.getBuildVector(VT, getCurSDLoc(), Ops);
  }

  // If this is a static alloca, generate it as the frameindex instead of
  // computation.
  if (const AllocaInst *AI = dyn_cast<AllocaInst>(V)) {
    DenseMap<const AllocaInst*, int>::iterator SI =
      FuncInfo.StaticAllocaMap.find(AI);
    if (SI != FuncInfo.StaticAllocaMap.end())
      return DAG.getFrameIndex(SI->second,
                               TLI.getFrameIndexTy(DAG.getDataLayout()));
  }

  // If this is an instruction which fast-isel has deferred, select it now.
  if (const Instruction *Inst = dyn_cast<Instruction>(V)) {
    unsigned InReg = FuncInfo.InitializeRegForValue(Inst);

    RegsForValue RFV(*DAG.getContext(), TLI, DAG.getDataLayout(), InReg,
                     Inst->getType(), getABIRegCopyCC(V));
    SDValue Chain = DAG.getEntryNode();
    return RFV.getCopyFromRegs(DAG, FuncInfo, getCurSDLoc(), Chain, nullptr, V);
  }

  llvm_unreachable("Can't get register for value!");
}

void SelectionDAGBuilder::visitCatchPad(const CatchPadInst &I) {
  auto Pers = classifyEHPersonality(FuncInfo.Fn->getPersonalityFn());
  bool IsMSVCCXX = Pers == EHPersonality::MSVC_CXX;
  bool IsCoreCLR = Pers == EHPersonality::CoreCLR;
  bool IsSEH = isAsynchronousEHPersonality(Pers);
  bool IsWasmCXX = Pers == EHPersonality::Wasm_CXX;
  MachineBasicBlock *CatchPadMBB = FuncInfo.MBB;
  if (!IsSEH)
    CatchPadMBB->setIsEHScopeEntry();
  // In MSVC C++ and CoreCLR, catchblocks are funclets and need prologues.
  if (IsMSVCCXX || IsCoreCLR)
    CatchPadMBB->setIsEHFuncletEntry();
  // Wasm does not need catchpads anymore
  if (!IsWasmCXX)
    DAG.setRoot(DAG.getNode(ISD::CATCHPAD, getCurSDLoc(), MVT::Other,
                            getControlRoot()));
}

void SelectionDAGBuilder::visitCatchRet(const CatchReturnInst &I) {
  // Update machine-CFG edge.
  MachineBasicBlock *TargetMBB = FuncInfo.MBBMap[I.getSuccessor()];
  FuncInfo.MBB->addSuccessor(TargetMBB);

  auto Pers = classifyEHPersonality(FuncInfo.Fn->getPersonalityFn());
  bool IsSEH = isAsynchronousEHPersonality(Pers);
  if (IsSEH) {
    // If this is not a fall-through branch or optimizations are switched off,
    // emit the branch.
    if (TargetMBB != NextBlock(FuncInfo.MBB) ||
        TM.getOptLevel() == CodeGenOpt::None)
      DAG.setRoot(DAG.getNode(ISD::BR, getCurSDLoc(), MVT::Other,
                              getControlRoot(), DAG.getBasicBlock(TargetMBB)));
    return;
  }

  // Figure out the funclet membership for the catchret's successor.
  // This will be used by the FuncletLayout pass to determine how to order the
  // BB's.
  // A 'catchret' returns to the outer scope's color.
  Value *ParentPad = I.getCatchSwitchParentPad();
  const BasicBlock *SuccessorColor;
  if (isa<ConstantTokenNone>(ParentPad))
    SuccessorColor = &FuncInfo.Fn->getEntryBlock();
  else
    SuccessorColor = cast<Instruction>(ParentPad)->getParent();
  assert(SuccessorColor && "No parent funclet for catchret!");
  MachineBasicBlock *SuccessorColorMBB = FuncInfo.MBBMap[SuccessorColor];
  assert(SuccessorColorMBB && "No MBB for SuccessorColor!");

  // Create the terminator node.
  SDValue Ret = DAG.getNode(ISD::CATCHRET, getCurSDLoc(), MVT::Other,
                            getControlRoot(), DAG.getBasicBlock(TargetMBB),
                            DAG.getBasicBlock(SuccessorColorMBB));
  DAG.setRoot(Ret);
}

void SelectionDAGBuilder::visitCleanupPad(const CleanupPadInst &CPI) {
  // Don't emit any special code for the cleanuppad instruction. It just marks
  // the start of an EH scope/funclet.
  FuncInfo.MBB->setIsEHScopeEntry();
  auto Pers = classifyEHPersonality(FuncInfo.Fn->getPersonalityFn());
  if (Pers != EHPersonality::Wasm_CXX) {
    FuncInfo.MBB->setIsEHFuncletEntry();
    FuncInfo.MBB->setIsCleanupFuncletEntry();
  }
}

/// When an invoke or a cleanupret unwinds to the next EH pad, there are
/// many places it could ultimately go. In the IR, we have a single unwind
/// destination, but in the machine CFG, we enumerate all the possible blocks.
/// This function skips over imaginary basic blocks that hold catchswitch
/// instructions, and finds all the "real" machine
/// basic block destinations. As those destinations may not be successors of
/// EHPadBB, here we also calculate the edge probability to those destinations.
/// The passed-in Prob is the edge probability to EHPadBB.
static void findUnwindDestinations(
    FunctionLoweringInfo &FuncInfo, const BasicBlock *EHPadBB,
    BranchProbability Prob,
    SmallVectorImpl<std::pair<MachineBasicBlock *, BranchProbability>>
        &UnwindDests) {
  EHPersonality Personality =
    classifyEHPersonality(FuncInfo.Fn->getPersonalityFn());
  bool IsMSVCCXX = Personality == EHPersonality::MSVC_CXX;
  bool IsCoreCLR = Personality == EHPersonality::CoreCLR;
  bool IsWasmCXX = Personality == EHPersonality::Wasm_CXX;
  bool IsSEH = isAsynchronousEHPersonality(Personality);

  while (EHPadBB) {
    const Instruction *Pad = EHPadBB->getFirstNonPHI();
    BasicBlock *NewEHPadBB = nullptr;
    if (isa<LandingPadInst>(Pad)) {
      // Stop on landingpads. They are not funclets.
      UnwindDests.emplace_back(FuncInfo.MBBMap[EHPadBB], Prob);
      break;
    } else if (isa<CleanupPadInst>(Pad)) {
      // Stop on cleanup pads. Cleanups are always funclet entries for all known
      // personalities.
      UnwindDests.emplace_back(FuncInfo.MBBMap[EHPadBB], Prob);
      UnwindDests.back().first->setIsEHScopeEntry();
      if (!IsWasmCXX)
        UnwindDests.back().first->setIsEHFuncletEntry();
      break;
    } else if (auto *CatchSwitch = dyn_cast<CatchSwitchInst>(Pad)) {
      // Add the catchpad handlers to the possible destinations.
      for (const BasicBlock *CatchPadBB : CatchSwitch->handlers()) {
        UnwindDests.emplace_back(FuncInfo.MBBMap[CatchPadBB], Prob);
        // For MSVC++ and the CLR, catchblocks are funclets and need prologues.
        if (IsMSVCCXX || IsCoreCLR)
          UnwindDests.back().first->setIsEHFuncletEntry();
        if (!IsSEH)
          UnwindDests.back().first->setIsEHScopeEntry();
      }
      NewEHPadBB = CatchSwitch->getUnwindDest();
    } else {
      continue;
    }

    BranchProbabilityInfo *BPI = FuncInfo.BPI;
    if (BPI && NewEHPadBB)
      Prob *= BPI->getEdgeProbability(EHPadBB, NewEHPadBB);
    EHPadBB = NewEHPadBB;
  }
}

void SelectionDAGBuilder::visitCleanupRet(const CleanupReturnInst &I) {
  // Update successor info.
  SmallVector<std::pair<MachineBasicBlock *, BranchProbability>, 1> UnwindDests;
  auto UnwindDest = I.getUnwindDest();
  BranchProbabilityInfo *BPI = FuncInfo.BPI;
  BranchProbability UnwindDestProb =
      (BPI && UnwindDest)
          ? BPI->getEdgeProbability(FuncInfo.MBB->getBasicBlock(), UnwindDest)
          : BranchProbability::getZero();
  findUnwindDestinations(FuncInfo, UnwindDest, UnwindDestProb, UnwindDests);
  for (auto &UnwindDest : UnwindDests) {
    UnwindDest.first->setIsEHPad();
    addSuccessorWithProb(FuncInfo.MBB, UnwindDest.first, UnwindDest.second);
  }
  FuncInfo.MBB->normalizeSuccProbs();

  // Create the terminator node.
  SDValue Ret =
      DAG.getNode(ISD::CLEANUPRET, getCurSDLoc(), MVT::Other, getControlRoot());
  DAG.setRoot(Ret);
}

void SelectionDAGBuilder::visitCatchSwitch(const CatchSwitchInst &CSI) {
  report_fatal_error("visitCatchSwitch not yet implemented!");
}

void SelectionDAGBuilder::visitRet(const ReturnInst &I) {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  auto &DL = DAG.getDataLayout();
  SDValue Chain = getControlRoot();
  SmallVector<ISD::OutputArg, 8> Outs;
  SmallVector<SDValue, 8> OutVals;

  // Calls to @llvm.experimental.deoptimize don't generate a return value, so
  // lower
  //
  //   %val = call <ty> @llvm.experimental.deoptimize()
  //   ret <ty> %val
  //
  // differently.
  if (I.getParent()->getTerminatingDeoptimizeCall()) {
    LowerDeoptimizingReturn();
    return;
  }

  if (!FuncInfo.CanLowerReturn) {
    unsigned DemoteReg = FuncInfo.DemoteRegister;
    const Function *F = I.getParent()->getParent();

    // Emit a store of the return value through the virtual register.
    // Leave Outs empty so that LowerReturn won't try to load return
    // registers the usual way.
    SmallVector<EVT, 1> PtrValueVTs;
    ComputeValueVTs(TLI, DL,
                    F->getReturnType()->getPointerTo(
                        DAG.getDataLayout().getAllocaAddrSpace()),
                    PtrValueVTs);

    SDValue RetPtr = DAG.getCopyFromReg(DAG.getEntryNode(), getCurSDLoc(),
                                        DemoteReg, PtrValueVTs[0]);
    SDValue RetOp = getValue(I.getOperand(0));

    SmallVector<EVT, 4> ValueVTs;
    SmallVector<uint64_t, 4> Offsets;
    ComputeValueVTs(TLI, DL, I.getOperand(0)->getType(), ValueVTs, &Offsets);
    unsigned NumValues = ValueVTs.size();

    SmallVector<SDValue, 4> Chains(NumValues);
    for (unsigned i = 0; i != NumValues; ++i) {
      // An aggregate return value cannot wrap around the address space, so
      // offsets to its parts don't wrap either.
      SDValue Ptr = DAG.getObjectPtrOffset(getCurSDLoc(), RetPtr, Offsets[i]);
      Chains[i] = DAG.getStore(
          Chain, getCurSDLoc(), SDValue(RetOp.getNode(), RetOp.getResNo() + i),
          // FIXME: better loc info would be nice.
          Ptr, MachinePointerInfo::getUnknownStack(DAG.getMachineFunction()));
    }

    Chain = DAG.getNode(ISD::TokenFactor, getCurSDLoc(),
                        MVT::Other, Chains);
  } else if (I.getNumOperands() != 0) {
    SmallVector<EVT, 4> ValueVTs;
    ComputeValueVTs(TLI, DL, I.getOperand(0)->getType(), ValueVTs);
    unsigned NumValues = ValueVTs.size();
    if (NumValues) {
      SDValue RetOp = getValue(I.getOperand(0));

      const Function *F = I.getParent()->getParent();

      ISD::NodeType ExtendKind = ISD::ANY_EXTEND;
      if (F->getAttributes().hasAttribute(AttributeList::ReturnIndex,
                                          Attribute::SExt))
        ExtendKind = ISD::SIGN_EXTEND;
      else if (F->getAttributes().hasAttribute(AttributeList::ReturnIndex,
                                               Attribute::ZExt))
        ExtendKind = ISD::ZERO_EXTEND;

      LLVMContext &Context = F->getContext();
      bool RetInReg = F->getAttributes().hasAttribute(
          AttributeList::ReturnIndex, Attribute::InReg);

      for (unsigned j = 0; j != NumValues; ++j) {
        EVT VT = ValueVTs[j];

        if (ExtendKind != ISD::ANY_EXTEND && VT.isInteger())
          VT = TLI.getTypeForExtReturn(Context, VT, ExtendKind);

        CallingConv::ID CC = F->getCallingConv();

        unsigned NumParts = TLI.getNumRegistersForCallingConv(Context, CC, VT);
        MVT PartVT = TLI.getRegisterTypeForCallingConv(Context, CC, VT);
        SmallVector<SDValue, 4> Parts(NumParts);
        getCopyToParts(DAG, getCurSDLoc(),
                       SDValue(RetOp.getNode(), RetOp.getResNo() + j),
                       &Parts[0], NumParts, PartVT, &I, CC, ExtendKind);

        // 'inreg' on function refers to return value
        ISD::ArgFlagsTy Flags = ISD::ArgFlagsTy();
        if (RetInReg)
          Flags.setInReg();

        // Propagate extension type if any
        if (ExtendKind == ISD::SIGN_EXTEND)
          Flags.setSExt();
        else if (ExtendKind == ISD::ZERO_EXTEND)
          Flags.setZExt();

        for (unsigned i = 0; i < NumParts; ++i) {
          Outs.push_back(ISD::OutputArg(Flags, Parts[i].getValueType(),
                                        VT, /*isfixed=*/true, 0, 0));
          OutVals.push_back(Parts[i]);
        }
      }
    }
  }

  // Push in swifterror virtual register as the last element of Outs. This makes
  // sure swifterror virtual register will be returned in the swifterror
  // physical register.
  const Function *F = I.getParent()->getParent();
  if (TLI.supportSwiftError() &&
      F->getAttributes().hasAttrSomewhere(Attribute::SwiftError)) {
    assert(FuncInfo.SwiftErrorArg && "Need a swift error argument");
    ISD::ArgFlagsTy Flags = ISD::ArgFlagsTy();
    Flags.setSwiftError();
    Outs.push_back(ISD::OutputArg(Flags, EVT(TLI.getPointerTy(DL)) /*vt*/,
                                  EVT(TLI.getPointerTy(DL)) /*argvt*/,
                                  true /*isfixed*/, 1 /*origidx*/,
                                  0 /*partOffs*/));
    // Create SDNode for the swifterror virtual register.
    OutVals.push_back(
        DAG.getRegister(FuncInfo.getOrCreateSwiftErrorVRegUseAt(
                            &I, FuncInfo.MBB, FuncInfo.SwiftErrorArg).first,
                        EVT(TLI.getPointerTy(DL))));
  }

  bool isVarArg = DAG.getMachineFunction().getFunction().isVarArg();
  CallingConv::ID CallConv =
    DAG.getMachineFunction().getFunction().getCallingConv();
  Chain = DAG.getTargetLoweringInfo().LowerReturn(
      Chain, CallConv, isVarArg, Outs, OutVals, getCurSDLoc(), DAG);

  // Verify that the target's LowerReturn behaved as expected.
  assert(Chain.getNode() && Chain.getValueType() == MVT::Other &&
         "LowerReturn didn't return a valid chain!");

  // Update the DAG with the new chain value resulting from return lowering.
  DAG.setRoot(Chain);
}

/// CopyToExportRegsIfNeeded - If the given value has virtual registers
/// created for it, emit nodes to copy the value into the virtual
/// registers.
void SelectionDAGBuilder::CopyToExportRegsIfNeeded(const Value *V) {
  // Skip empty types
  if (V->getType()->isEmptyTy())
    return;

  DenseMap<const Value *, unsigned>::iterator VMI = FuncInfo.ValueMap.find(V);
  if (VMI != FuncInfo.ValueMap.end()) {
    assert(!V->use_empty() && "Unused value assigned virtual registers!");
    CopyValueToVirtualRegister(V, VMI->second);
  }
}

/// ExportFromCurrentBlock - If this condition isn't known to be exported from
/// the current basic block, add it to ValueMap now so that we'll get a
/// CopyTo/FromReg.
void SelectionDAGBuilder::ExportFromCurrentBlock(const Value *V) {
  // No need to export constants.
  if (!isa<Instruction>(V) && !isa<Argument>(V)) return;

  // Already exported?
  if (FuncInfo.isExportedInst(V)) return;

  unsigned Reg = FuncInfo.InitializeRegForValue(V);
  CopyValueToVirtualRegister(V, Reg);
}

bool SelectionDAGBuilder::isExportableFromCurrentBlock(const Value *V,
                                                     const BasicBlock *FromBB) {
  // The operands of the setcc have to be in this block.  We don't know
  // how to export them from some other block.
  if (const Instruction *VI = dyn_cast<Instruction>(V)) {
    // Can export from current BB.
    if (VI->getParent() == FromBB)
      return true;

    // Is already exported, noop.
    return FuncInfo.isExportedInst(V);
  }

  // If this is an argument, we can export it if the BB is the entry block or
  // if it is already exported.
  if (isa<Argument>(V)) {
    if (FromBB == &FromBB->getParent()->getEntryBlock())
      return true;

    // Otherwise, can only export this if it is already exported.
    return FuncInfo.isExportedInst(V);
  }

  // Otherwise, constants can always be exported.
  return true;
}

/// Return branch probability calculated by BranchProbabilityInfo for IR blocks.
BranchProbability
SelectionDAGBuilder::getEdgeProbability(const MachineBasicBlock *Src,
                                        const MachineBasicBlock *Dst) const {
  BranchProbabilityInfo *BPI = FuncInfo.BPI;
  const BasicBlock *SrcBB = Src->getBasicBlock();
  const BasicBlock *DstBB = Dst->getBasicBlock();
  if (!BPI) {
    // If BPI is not available, set the default probability as 1 / N, where N is
    // the number of successors.
    auto SuccSize = std::max<uint32_t>(succ_size(SrcBB), 1);
    return BranchProbability(1, SuccSize);
  }
  return BPI->getEdgeProbability(SrcBB, DstBB);
}

void SelectionDAGBuilder::addSuccessorWithProb(MachineBasicBlock *Src,
                                               MachineBasicBlock *Dst,
                                               BranchProbability Prob) {
  if (!FuncInfo.BPI)
    Src->addSuccessorWithoutProb(Dst);
  else {
    if (Prob.isUnknown())
      Prob = getEdgeProbability(Src, Dst);
    Src->addSuccessor(Dst, Prob);
  }
}

static bool InBlock(const Value *V, const BasicBlock *BB) {
  if (const Instruction *I = dyn_cast<Instruction>(V))
    return I->getParent() == BB;
  return true;
}

/// EmitBranchForMergedCondition - Helper method for FindMergedConditions.
/// This function emits a branch and is used at the leaves of an OR or an
/// AND operator tree.
void
SelectionDAGBuilder::EmitBranchForMergedCondition(const Value *Cond,
                                                  MachineBasicBlock *TBB,
                                                  MachineBasicBlock *FBB,
                                                  MachineBasicBlock *CurBB,
                                                  MachineBasicBlock *SwitchBB,
                                                  BranchProbability TProb,
                                                  BranchProbability FProb,
                                                  bool InvertCond) {
  const BasicBlock *BB = CurBB->getBasicBlock();

  // If the leaf of the tree is a comparison, merge the condition into
  // the caseblock.
  if (const CmpInst *BOp = dyn_cast<CmpInst>(Cond)) {
    // The operands of the cmp have to be in this block.  We don't know
    // how to export them from some other block.  If this is the first block
    // of the sequence, no exporting is needed.
    if (CurBB == SwitchBB ||
        (isExportableFromCurrentBlock(BOp->getOperand(0), BB) &&
         isExportableFromCurrentBlock(BOp->getOperand(1), BB))) {
      ISD::CondCode Condition;
      if (const ICmpInst *IC = dyn_cast<ICmpInst>(Cond)) {
        ICmpInst::Predicate Pred =
            InvertCond ? IC->getInversePredicate() : IC->getPredicate();
        Condition = getICmpCondCode(Pred);
      } else {
        const FCmpInst *FC = cast<FCmpInst>(Cond);
        FCmpInst::Predicate Pred =
            InvertCond ? FC->getInversePredicate() : FC->getPredicate();
        Condition = getFCmpCondCode(Pred);
        if (TM.Options.NoNaNsFPMath)
          Condition = getFCmpCodeWithoutNaN(Condition);
      }

      CaseBlock CB(Condition, BOp->getOperand(0), BOp->getOperand(1), nullptr,
                   TBB, FBB, CurBB, getCurSDLoc(), TProb, FProb);
      SwitchCases.push_back(CB);
      return;
    }
  }

  // Create a CaseBlock record representing this branch.
  ISD::CondCode Opc = InvertCond ? ISD::SETNE : ISD::SETEQ;
  CaseBlock CB(Opc, Cond, ConstantInt::getTrue(*DAG.getContext()),
               nullptr, TBB, FBB, CurBB, getCurSDLoc(), TProb, FProb);
  SwitchCases.push_back(CB);
}

void SelectionDAGBuilder::FindMergedConditions(const Value *Cond,
                                               MachineBasicBlock *TBB,
                                               MachineBasicBlock *FBB,
                                               MachineBasicBlock *CurBB,
                                               MachineBasicBlock *SwitchBB,
                                               Instruction::BinaryOps Opc,
                                               BranchProbability TProb,
                                               BranchProbability FProb,
                                               bool InvertCond) {
  // Skip over not part of the tree and remember to invert op and operands at
  // next level.
  Value *NotCond;
  if (match(Cond, m_OneUse(m_Not(m_Value(NotCond)))) &&
      InBlock(NotCond, CurBB->getBasicBlock())) {
    FindMergedConditions(NotCond, TBB, FBB, CurBB, SwitchBB, Opc, TProb, FProb,
                         !InvertCond);
    return;
  }

  const Instruction *BOp = dyn_cast<Instruction>(Cond);
  // Compute the effective opcode for Cond, taking into account whether it needs
  // to be inverted, e.g.
  //   and (not (or A, B)), C
  // gets lowered as
  //   and (and (not A, not B), C)
  unsigned BOpc = 0;
  if (BOp) {
    BOpc = BOp->getOpcode();
    if (InvertCond) {
      if (BOpc == Instruction::And)
        BOpc = Instruction::Or;
      else if (BOpc == Instruction::Or)
        BOpc = Instruction::And;
    }
  }

  // If this node is not part of the or/and tree, emit it as a branch.
  if (!BOp || !(isa<BinaryOperator>(BOp) || isa<CmpInst>(BOp)) ||
      BOpc != unsigned(Opc) || !BOp->hasOneUse() ||
      BOp->getParent() != CurBB->getBasicBlock() ||
      !InBlock(BOp->getOperand(0), CurBB->getBasicBlock()) ||
      !InBlock(BOp->getOperand(1), CurBB->getBasicBlock())) {
    EmitBranchForMergedCondition(Cond, TBB, FBB, CurBB, SwitchBB,
                                 TProb, FProb, InvertCond);
    return;
  }

  //  Create TmpBB after CurBB.
  MachineFunction::iterator BBI(CurBB);
  MachineFunction &MF = DAG.getMachineFunction();
  MachineBasicBlock *TmpBB = MF.CreateMachineBasicBlock(CurBB->getBasicBlock());
  CurBB->getParent()->insert(++BBI, TmpBB);

  if (Opc == Instruction::Or) {
    // Codegen X | Y as:
    // BB1:
    //   jmp_if_X TBB
    //   jmp TmpBB
    // TmpBB:
    //   jmp_if_Y TBB
    //   jmp FBB
    //

    // We have flexibility in setting Prob for BB1 and Prob for TmpBB.
    // The requirement is that
    //   TrueProb for BB1 + (FalseProb for BB1 * TrueProb for TmpBB)
    //     = TrueProb for original BB.
    // Assuming the original probabilities are A and B, one choice is to set
    // BB1's probabilities to A/2 and A/2+B, and set TmpBB's probabilities to
    // A/(1+B) and 2B/(1+B). This choice assumes that
    //   TrueProb for BB1 == FalseProb for BB1 * TrueProb for TmpBB.
    // Another choice is to assume TrueProb for BB1 equals to TrueProb for
    // TmpBB, but the math is more complicated.

    auto NewTrueProb = TProb / 2;
    auto NewFalseProb = TProb / 2 + FProb;
    // Emit the LHS condition.
    FindMergedConditions(BOp->getOperand(0), TBB, TmpBB, CurBB, SwitchBB, Opc,
                         NewTrueProb, NewFalseProb, InvertCond);

    // Normalize A/2 and B to get A/(1+B) and 2B/(1+B).
    SmallVector<BranchProbability, 2> Probs{TProb / 2, FProb};
    BranchProbability::normalizeProbabilities(Probs.begin(), Probs.end());
    // Emit the RHS condition into TmpBB.
    FindMergedConditions(BOp->getOperand(1), TBB, FBB, TmpBB, SwitchBB, Opc,
                         Probs[0], Probs[1], InvertCond);
  } else {
    assert(Opc == Instruction::And && "Unknown merge op!");
    // Codegen X & Y as:
    // BB1:
    //   jmp_if_X TmpBB
    //   jmp FBB
    // TmpBB:
    //   jmp_if_Y TBB
    //   jmp FBB
    //
    //  This requires creation of TmpBB after CurBB.

    // We have flexibility in setting Prob for BB1 and Prob for TmpBB.
    // The requirement is that
    //   FalseProb for BB1 + (TrueProb for BB1 * FalseProb for TmpBB)
    //     = FalseProb for original BB.
    // Assuming the original probabilities are A and B, one choice is to set
    // BB1's probabilities to A+B/2 and B/2, and set TmpBB's probabilities to
    // 2A/(1+A) and B/(1+A). This choice assumes that FalseProb for BB1 ==
    // TrueProb for BB1 * FalseProb for TmpBB.

    auto NewTrueProb = TProb + FProb / 2;
    auto NewFalseProb = FProb / 2;
    // Emit the LHS condition.
    FindMergedConditions(BOp->getOperand(0), TmpBB, FBB, CurBB, SwitchBB, Opc,
                         NewTrueProb, NewFalseProb, InvertCond);

    // Normalize A and B/2 to get 2A/(1+A) and B/(1+A).
    SmallVector<BranchProbability, 2> Probs{TProb, FProb / 2};
    BranchProbability::normalizeProbabilities(Probs.begin(), Probs.end());
    // Emit the RHS condition into TmpBB.
    FindMergedConditions(BOp->getOperand(1), TBB, FBB, TmpBB, SwitchBB, Opc,
                         Probs[0], Probs[1], InvertCond);
  }
}

/// If the set of cases should be emitted as a series of branches, return true.
/// If we should emit this as a bunch of and/or'd together conditions, return
/// false.
bool
SelectionDAGBuilder::ShouldEmitAsBranches(const std::vector<CaseBlock> &Cases) {
  if (Cases.size() != 2) return true;

  // If this is two comparisons of the same values or'd or and'd together, they
  // will get folded into a single comparison, so don't emit two blocks.
  if ((Cases[0].CmpLHS == Cases[1].CmpLHS &&
       Cases[0].CmpRHS == Cases[1].CmpRHS) ||
      (Cases[0].CmpRHS == Cases[1].CmpLHS &&
       Cases[0].CmpLHS == Cases[1].CmpRHS)) {
    return false;
  }

  // Handle: (X != null) | (Y != null) --> (X|Y) != 0
  // Handle: (X == null) & (Y == null) --> (X|Y) == 0
  if (Cases[0].CmpRHS == Cases[1].CmpRHS &&
      Cases[0].CC == Cases[1].CC &&
      isa<Constant>(Cases[0].CmpRHS) &&
      cast<Constant>(Cases[0].CmpRHS)->isNullValue()) {
    if (Cases[0].CC == ISD::SETEQ && Cases[0].TrueBB == Cases[1].ThisBB)
      return false;
    if (Cases[0].CC == ISD::SETNE && Cases[0].FalseBB == Cases[1].ThisBB)
      return false;
  }

  return true;
}

void SelectionDAGBuilder::visitBr(const BranchInst &I) {
  MachineBasicBlock *BrMBB = FuncInfo.MBB;

  // Update machine-CFG edges.
  MachineBasicBlock *Succ0MBB = FuncInfo.MBBMap[I.getSuccessor(0)];

  if (I.isUnconditional()) {
    // Update machine-CFG edges.
    BrMBB->addSuccessor(Succ0MBB);

    // If this is not a fall-through branch or optimizations are switched off,
    // emit the branch.
    if (Succ0MBB != NextBlock(BrMBB) || TM.getOptLevel() == CodeGenOpt::None)
      DAG.setRoot(DAG.getNode(ISD::BR, getCurSDLoc(),
                              MVT::Other, getControlRoot(),
                              DAG.getBasicBlock(Succ0MBB)));

    return;
  }

  // If this condition is one of the special cases we handle, do special stuff
  // now.
  const Value *CondVal = I.getCondition();
  MachineBasicBlock *Succ1MBB = FuncInfo.MBBMap[I.getSuccessor(1)];

  // If this is a series of conditions that are or'd or and'd together, emit
  // this as a sequence of branches instead of setcc's with and/or operations.
  // As long as jumps are not expensive, this should improve performance.
  // For example, instead of something like:
  //     cmp A, B
  //     C = seteq
  //     cmp D, E
  //     F = setle
  //     or C, F
  //     jnz foo
  // Emit:
  //     cmp A, B
  //     je foo
  //     cmp D, E
  //     jle foo
  if (const BinaryOperator *BOp = dyn_cast<BinaryOperator>(CondVal)) {
    Instruction::BinaryOps Opcode = BOp->getOpcode();
    if (!DAG.getTargetLoweringInfo().isJumpExpensive() && BOp->hasOneUse() &&
        !I.getMetadata(LLVMContext::MD_unpredictable) &&
        (Opcode == Instruction::And || Opcode == Instruction::Or)) {
      FindMergedConditions(BOp, Succ0MBB, Succ1MBB, BrMBB, BrMBB,
                           Opcode,
                           getEdgeProbability(BrMBB, Succ0MBB),
                           getEdgeProbability(BrMBB, Succ1MBB),
                           /*InvertCond=*/false);
      // If the compares in later blocks need to use values not currently
      // exported from this block, export them now.  This block should always
      // be the first entry.
      assert(SwitchCases[0].ThisBB == BrMBB && "Unexpected lowering!");

      // Allow some cases to be rejected.
      if (ShouldEmitAsBranches(SwitchCases)) {
        for (unsigned i = 1, e = SwitchCases.size(); i != e; ++i) {
          ExportFromCurrentBlock(SwitchCases[i].CmpLHS);
          ExportFromCurrentBlock(SwitchCases[i].CmpRHS);
        }

        // Emit the branch for this block.
        visitSwitchCase(SwitchCases[0], BrMBB);
        SwitchCases.erase(SwitchCases.begin());
        return;
      }

      // Okay, we decided not to do this, remove any inserted MBB's and clear
      // SwitchCases.
      for (unsigned i = 1, e = SwitchCases.size(); i != e; ++i)
        FuncInfo.MF->erase(SwitchCases[i].ThisBB);

      SwitchCases.clear();
    }
  }

  // Create a CaseBlock record representing this branch.
  CaseBlock CB(ISD::SETEQ, CondVal, ConstantInt::getTrue(*DAG.getContext()),
               nullptr, Succ0MBB, Succ1MBB, BrMBB, getCurSDLoc());

  // Use visitSwitchCase to actually insert the fast branch sequence for this
  // cond branch.
  visitSwitchCase(CB, BrMBB);
}

/// visitSwitchCase - Emits the necessary code to represent a single node in
/// the binary search tree resulting from lowering a switch instruction.
void SelectionDAGBuilder::visitSwitchCase(CaseBlock &CB,
                                          MachineBasicBlock *SwitchBB) {
  SDValue Cond;
  SDValue CondLHS = getValue(CB.CmpLHS);
  SDLoc dl = CB.DL;

  // Build the setcc now.
  if (!CB.CmpMHS) {
    // Fold "(X == true)" to X and "(X == false)" to !X to
    // handle common cases produced by branch lowering.
    if (CB.CmpRHS == ConstantInt::getTrue(*DAG.getContext()) &&
        CB.CC == ISD::SETEQ)
      Cond = CondLHS;
    else if (CB.CmpRHS == ConstantInt::getFalse(*DAG.getContext()) &&
             CB.CC == ISD::SETEQ) {
      SDValue True = DAG.getConstant(1, dl, CondLHS.getValueType());
      Cond = DAG.getNode(ISD::XOR, dl, CondLHS.getValueType(), CondLHS, True);
    } else
      Cond = DAG.getSetCC(dl, MVT::i1, CondLHS, getValue(CB.CmpRHS), CB.CC);
  } else {
    assert(CB.CC == ISD::SETLE && "Can handle only LE ranges now");

    const APInt& Low = cast<ConstantInt>(CB.CmpLHS)->getValue();
    const APInt& High = cast<ConstantInt>(CB.CmpRHS)->getValue();

    SDValue CmpOp = getValue(CB.CmpMHS);
    EVT VT = CmpOp.getValueType();

    if (cast<ConstantInt>(CB.CmpLHS)->isMinValue(true)) {
      Cond = DAG.getSetCC(dl, MVT::i1, CmpOp, DAG.getConstant(High, dl, VT),
                          ISD::SETLE);
    } else {
      SDValue SUB = DAG.getNode(ISD::SUB, dl,
                                VT, CmpOp, DAG.getConstant(Low, dl, VT));
      Cond = DAG.getSetCC(dl, MVT::i1, SUB,
                          DAG.getConstant(High-Low, dl, VT), ISD::SETULE);
    }
  }

  // Update successor info
  addSuccessorWithProb(SwitchBB, CB.TrueBB, CB.TrueProb);
  // TrueBB and FalseBB are always different unless the incoming IR is
  // degenerate. This only happens when running llc on weird IR.
  if (CB.TrueBB != CB.FalseBB)
    addSuccessorWithProb(SwitchBB, CB.FalseBB, CB.FalseProb);
  SwitchBB->normalizeSuccProbs();

  // If the lhs block is the next block, invert the condition so that we can
  // fall through to the lhs instead of the rhs block.
  if (CB.TrueBB == NextBlock(SwitchBB)) {
    std::swap(CB.TrueBB, CB.FalseBB);
    SDValue True = DAG.getConstant(1, dl, Cond.getValueType());
    Cond = DAG.getNode(ISD::XOR, dl, Cond.getValueType(), Cond, True);
  }

  SDValue BrCond = DAG.getNode(ISD::BRCOND, dl,
                               MVT::Other, getControlRoot(), Cond,
                               DAG.getBasicBlock(CB.TrueBB));

  // Insert the false branch. Do this even if it's a fall through branch,
  // this makes it easier to do DAG optimizations which require inverting
  // the branch condition.
  BrCond = DAG.getNode(ISD::BR, dl, MVT::Other, BrCond,
                       DAG.getBasicBlock(CB.FalseBB));

  DAG.setRoot(BrCond);
}

/// visitJumpTable - Emit JumpTable node in the current MBB
void SelectionDAGBuilder::visitJumpTable(JumpTable &JT) {
  // Emit the code for the jump table
  assert(JT.Reg != -1U && "Should lower JT Header first!");
  EVT PTy = DAG.getTargetLoweringInfo().getPointerTy(DAG.getDataLayout());
  SDValue Index = DAG.getCopyFromReg(getControlRoot(), getCurSDLoc(),
                                     JT.Reg, PTy);
  SDValue Table = DAG.getJumpTable(JT.JTI, PTy);
  SDValue BrJumpTable = DAG.getNode(ISD::BR_JT, getCurSDLoc(),
                                    MVT::Other, Index.getValue(1),
                                    Table, Index);
  DAG.setRoot(BrJumpTable);
}

/// visitJumpTableHeader - This function emits necessary code to produce index
/// in the JumpTable from switch case.
void SelectionDAGBuilder::visitJumpTableHeader(JumpTable &JT,
                                               JumpTableHeader &JTH,
                                               MachineBasicBlock *SwitchBB) {
  SDLoc dl = getCurSDLoc();

  // Subtract the lowest switch case value from the value being switched on and
  // conditional branch to default mbb if the result is greater than the
  // difference between smallest and largest cases.
  SDValue SwitchOp = getValue(JTH.SValue);
  EVT VT = SwitchOp.getValueType();
  SDValue Sub = DAG.getNode(ISD::SUB, dl, VT, SwitchOp,
                            DAG.getConstant(JTH.First, dl, VT));

  // The SDNode we just created, which holds the value being switched on minus
  // the smallest case value, needs to be copied to a virtual register so it
  // can be used as an index into the jump table in a subsequent basic block.
  // This value may be smaller or larger than the target's pointer type, and
  // therefore require extension or truncating.
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  SwitchOp = DAG.getZExtOrTrunc(Sub, dl, TLI.getPointerTy(DAG.getDataLayout()));

  unsigned JumpTableReg =
      FuncInfo.CreateReg(TLI.getPointerTy(DAG.getDataLayout()));
  SDValue CopyTo = DAG.getCopyToReg(getControlRoot(), dl,
                                    JumpTableReg, SwitchOp);
  JT.Reg = JumpTableReg;

  // Emit the range check for the jump table, and branch to the default block
  // for the switch statement if the value being switched on exceeds the largest
  // case in the switch.
  SDValue CMP = DAG.getSetCC(
      dl, TLI.getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(),
                                 Sub.getValueType()),
      Sub, DAG.getConstant(JTH.Last - JTH.First, dl, VT), ISD::SETUGT);

  SDValue BrCond = DAG.getNode(ISD::BRCOND, dl,
                               MVT::Other, CopyTo, CMP,
                               DAG.getBasicBlock(JT.Default));

  // Avoid emitting unnecessary branches to the next block.
  if (JT.MBB != NextBlock(SwitchBB))
    BrCond = DAG.getNode(ISD::BR, dl, MVT::Other, BrCond,
                         DAG.getBasicBlock(JT.MBB));

  DAG.setRoot(BrCond);
}

/// Create a LOAD_STACK_GUARD node, and let it carry the target specific global
/// variable if there exists one.
static SDValue getLoadStackGuard(SelectionDAG &DAG, const SDLoc &DL,
                                 SDValue &Chain) {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  EVT PtrTy = TLI.getPointerTy(DAG.getDataLayout());
  MachineFunction &MF = DAG.getMachineFunction();
  Value *Global = TLI.getSDagStackGuard(*MF.getFunction().getParent());
  MachineSDNode *Node =
      DAG.getMachineNode(TargetOpcode::LOAD_STACK_GUARD, DL, PtrTy, Chain);
  if (Global) {
    MachinePointerInfo MPInfo(Global);
    auto Flags = MachineMemOperand::MOLoad | MachineMemOperand::MOInvariant |
                 MachineMemOperand::MODereferenceable;
    MachineMemOperand *MemRef = MF.getMachineMemOperand(
        MPInfo, Flags, PtrTy.getSizeInBits() / 8, DAG.getEVTAlignment(PtrTy));
    DAG.setNodeMemRefs(Node, {MemRef});
  }
  return SDValue(Node, 0);
}

/// Codegen a new tail for a stack protector check ParentMBB which has had its
/// tail spliced into a stack protector check success bb.
///
/// For a high level explanation of how this fits into the stack protector
/// generation see the comment on the declaration of class
/// StackProtectorDescriptor.
void SelectionDAGBuilder::visitSPDescriptorParent(StackProtectorDescriptor &SPD,
                                                  MachineBasicBlock *ParentBB) {

  // First create the loads to the guard/stack slot for the comparison.
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  EVT PtrTy = TLI.getPointerTy(DAG.getDataLayout());

  MachineFrameInfo &MFI = ParentBB->getParent()->getFrameInfo();
  int FI = MFI.getStackProtectorIndex();

  SDValue Guard;
  SDLoc dl = getCurSDLoc();
  SDValue StackSlotPtr = DAG.getFrameIndex(FI, PtrTy);
  const Module &M = *ParentBB->getParent()->getFunction().getParent();
  unsigned Align = DL->getPrefTypeAlignment(Type::getInt8PtrTy(M.getContext()));

  // Generate code to load the content of the guard slot.
  SDValue GuardVal = DAG.getLoad(
      PtrTy, dl, DAG.getEntryNode(), StackSlotPtr,
      MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FI), Align,
      MachineMemOperand::MOVolatile);

  if (TLI.useStackGuardXorFP())
    GuardVal = TLI.emitStackGuardXorFP(DAG, GuardVal, dl);

  // Retrieve guard check function, nullptr if instrumentation is inlined.
  if (const Value *GuardCheck = TLI.getSSPStackGuardCheck(M)) {
    // The target provides a guard check function to validate the guard value.
    // Generate a call to that function with the content of the guard slot as
    // argument.
    auto *Fn = cast<Function>(GuardCheck);
    FunctionType *FnTy = Fn->getFunctionType();
    assert(FnTy->getNumParams() == 1 && "Invalid function signature");

    TargetLowering::ArgListTy Args;
    TargetLowering::ArgListEntry Entry;
    Entry.Node = GuardVal;
    Entry.Ty = FnTy->getParamType(0);
    if (Fn->hasAttribute(1, Attribute::AttrKind::InReg))
      Entry.IsInReg = true;
    Args.push_back(Entry);

    TargetLowering::CallLoweringInfo CLI(DAG);
    CLI.setDebugLoc(getCurSDLoc())
      .setChain(DAG.getEntryNode())
      .setCallee(Fn->getCallingConv(), FnTy->getReturnType(),
                 getValue(GuardCheck), std::move(Args));

    std::pair<SDValue, SDValue> Result = TLI.LowerCallTo(CLI);
    DAG.setRoot(Result.second);
    return;
  }

  // If useLoadStackGuardNode returns true, generate LOAD_STACK_GUARD.
  // Otherwise, emit a volatile load to retrieve the stack guard value.
  SDValue Chain = DAG.getEntryNode();
  if (TLI.useLoadStackGuardNode()) {
    Guard = getLoadStackGuard(DAG, dl, Chain);
  } else {
    const Value *IRGuard = TLI.getSDagStackGuard(M);
    SDValue GuardPtr = getValue(IRGuard);

    Guard =
        DAG.getLoad(PtrTy, dl, Chain, GuardPtr, MachinePointerInfo(IRGuard, 0),
                    Align, MachineMemOperand::MOVolatile);
  }

  // Perform the comparison via a subtract/getsetcc.
  EVT VT = Guard.getValueType();
  SDValue Sub = DAG.getNode(ISD::SUB, dl, VT, Guard, GuardVal);

  SDValue Cmp = DAG.getSetCC(dl, TLI.getSetCCResultType(DAG.getDataLayout(),
                                                        *DAG.getContext(),
                                                        Sub.getValueType()),
                             Sub, DAG.getConstant(0, dl, VT), ISD::SETNE);

  // If the sub is not 0, then we know the guard/stackslot do not equal, so
  // branch to failure MBB.
  SDValue BrCond = DAG.getNode(ISD::BRCOND, dl,
                               MVT::Other, GuardVal.getOperand(0),
                               Cmp, DAG.getBasicBlock(SPD.getFailureMBB()));
  // Otherwise branch to success MBB.
  SDValue Br = DAG.getNode(ISD::BR, dl,
                           MVT::Other, BrCond,
                           DAG.getBasicBlock(SPD.getSuccessMBB()));

  DAG.setRoot(Br);
}

/// Codegen the failure basic block for a stack protector check.
///
/// A failure stack protector machine basic block consists simply of a call to
/// __stack_chk_fail().
///
/// For a high level explanation of how this fits into the stack protector
/// generation see the comment on the declaration of class
/// StackProtectorDescriptor.
void
SelectionDAGBuilder::visitSPDescriptorFailure(StackProtectorDescriptor &SPD) {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  SDValue Chain =
      TLI.makeLibCall(DAG, RTLIB::STACKPROTECTOR_CHECK_FAIL, MVT::isVoid,
                      None, false, getCurSDLoc(), false, false).second;
  DAG.setRoot(Chain);
}

/// visitBitTestHeader - This function emits necessary code to produce value
/// suitable for "bit tests"
void SelectionDAGBuilder::visitBitTestHeader(BitTestBlock &B,
                                             MachineBasicBlock *SwitchBB) {
  SDLoc dl = getCurSDLoc();

  // Subtract the minimum value
  SDValue SwitchOp = getValue(B.SValue);
  EVT VT = SwitchOp.getValueType();
  SDValue Sub = DAG.getNode(ISD::SUB, dl, VT, SwitchOp,
                            DAG.getConstant(B.First, dl, VT));

  // Check range
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  SDValue RangeCmp = DAG.getSetCC(
      dl, TLI.getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(),
                                 Sub.getValueType()),
      Sub, DAG.getConstant(B.Range, dl, VT), ISD::SETUGT);

  // Determine the type of the test operands.
  bool UsePtrType = false;
  if (!TLI.isTypeLegal(VT))
    UsePtrType = true;
  else {
    for (unsigned i = 0, e = B.Cases.size(); i != e; ++i)
      if (!isUIntN(VT.getSizeInBits(), B.Cases[i].Mask)) {
        // Switch table case range are encoded into series of masks.
        // Just use pointer type, it's guaranteed to fit.
        UsePtrType = true;
        break;
      }
  }
  if (UsePtrType) {
    VT = TLI.getPointerTy(DAG.getDataLayout());
    Sub = DAG.getZExtOrTrunc(Sub, dl, VT);
  }

  B.RegVT = VT.getSimpleVT();
  B.Reg = FuncInfo.CreateReg(B.RegVT);
  SDValue CopyTo = DAG.getCopyToReg(getControlRoot(), dl, B.Reg, Sub);

  MachineBasicBlock* MBB = B.Cases[0].ThisBB;

  addSuccessorWithProb(SwitchBB, B.Default, B.DefaultProb);
  addSuccessorWithProb(SwitchBB, MBB, B.Prob);
  SwitchBB->normalizeSuccProbs();

  SDValue BrRange = DAG.getNode(ISD::BRCOND, dl,
                                MVT::Other, CopyTo, RangeCmp,
                                DAG.getBasicBlock(B.Default));

  // Avoid emitting unnecessary branches to the next block.
  if (MBB != NextBlock(SwitchBB))
    BrRange = DAG.getNode(ISD::BR, dl, MVT::Other, BrRange,
                          DAG.getBasicBlock(MBB));

  DAG.setRoot(BrRange);
}

/// visitBitTestCase - this function produces one "bit test"
void SelectionDAGBuilder::visitBitTestCase(BitTestBlock &BB,
                                           MachineBasicBlock* NextMBB,
                                           BranchProbability BranchProbToNext,
                                           unsigned Reg,
                                           BitTestCase &B,
                                           MachineBasicBlock *SwitchBB) {
  SDLoc dl = getCurSDLoc();
  MVT VT = BB.RegVT;
  SDValue ShiftOp = DAG.getCopyFromReg(getControlRoot(), dl, Reg, VT);
  SDValue Cmp;
  unsigned PopCount = countPopulation(B.Mask);
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  if (PopCount == 1) {
    // Testing for a single bit; just compare the shift count with what it
    // would need to be to shift a 1 bit in that position.
    Cmp = DAG.getSetCC(
        dl, TLI.getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT),
        ShiftOp, DAG.getConstant(countTrailingZeros(B.Mask), dl, VT),
        ISD::SETEQ);
  } else if (PopCount == BB.Range) {
    // There is only one zero bit in the range, test for it directly.
    Cmp = DAG.getSetCC(
        dl, TLI.getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT),
        ShiftOp, DAG.getConstant(countTrailingOnes(B.Mask), dl, VT),
        ISD::SETNE);
  } else {
    // Make desired shift
    SDValue SwitchVal = DAG.getNode(ISD::SHL, dl, VT,
                                    DAG.getConstant(1, dl, VT), ShiftOp);

    // Emit bit tests and jumps
    SDValue AndOp = DAG.getNode(ISD::AND, dl,
                                VT, SwitchVal, DAG.getConstant(B.Mask, dl, VT));
    Cmp = DAG.getSetCC(
        dl, TLI.getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT),
        AndOp, DAG.getConstant(0, dl, VT), ISD::SETNE);
  }

  // The branch probability from SwitchBB to B.TargetBB is B.ExtraProb.
  addSuccessorWithProb(SwitchBB, B.TargetBB, B.ExtraProb);
  // The branch probability from SwitchBB to NextMBB is BranchProbToNext.
  addSuccessorWithProb(SwitchBB, NextMBB, BranchProbToNext);
  // It is not guaranteed that the sum of B.ExtraProb and BranchProbToNext is
  // one as they are relative probabilities (and thus work more like weights),
  // and hence we need to normalize them to let the sum of them become one.
  SwitchBB->normalizeSuccProbs();

  SDValue BrAnd = DAG.getNode(ISD::BRCOND, dl,
                              MVT::Other, getControlRoot(),
                              Cmp, DAG.getBasicBlock(B.TargetBB));

  // Avoid emitting unnecessary branches to the next block.
  if (NextMBB != NextBlock(SwitchBB))
    BrAnd = DAG.getNode(ISD::BR, dl, MVT::Other, BrAnd,
                        DAG.getBasicBlock(NextMBB));

  DAG.setRoot(BrAnd);
}

void SelectionDAGBuilder::visitInvoke(const InvokeInst &I) {
  MachineBasicBlock *InvokeMBB = FuncInfo.MBB;

  // Retrieve successors. Look through artificial IR level blocks like
  // catchswitch for successors.
  MachineBasicBlock *Return = FuncInfo.MBBMap[I.getSuccessor(0)];
  const BasicBlock *EHPadBB = I.getSuccessor(1);

  // Deopt bundles are lowered in LowerCallSiteWithDeoptBundle, and we don't
  // have to do anything here to lower funclet bundles.
  assert(!I.hasOperandBundlesOtherThan(
             {LLVMContext::OB_deopt, LLVMContext::OB_funclet}) &&
         "Cannot lower invokes with arbitrary operand bundles yet!");

  const Value *Callee(I.getCalledValue());
  const Function *Fn = dyn_cast<Function>(Callee);
  if (isa<InlineAsm>(Callee))
    visitInlineAsm(&I);
  else if (Fn && Fn->isIntrinsic()) {
    switch (Fn->getIntrinsicID()) {
    default:
      llvm_unreachable("Cannot invoke this intrinsic");
    case Intrinsic::donothing:
      // Ignore invokes to @llvm.donothing: jump directly to the next BB.
      break;
    case Intrinsic::experimental_patchpoint_void:
    case Intrinsic::experimental_patchpoint_i64:
      visitPatchpoint(&I, EHPadBB);
      break;
    case Intrinsic::experimental_gc_statepoint:
      LowerStatepoint(ImmutableStatepoint(&I), EHPadBB);
      break;
    }
  } else if (I.countOperandBundlesOfType(LLVMContext::OB_deopt)) {
    // Currently we do not lower any intrinsic calls with deopt operand bundles.
    // Eventually we will support lowering the @llvm.experimental.deoptimize
    // intrinsic, and right now there are no plans to support other intrinsics
    // with deopt state.
    LowerCallSiteWithDeoptBundle(&I, getValue(Callee), EHPadBB);
  } else {
    LowerCallTo(&I, getValue(Callee), false, EHPadBB);
  }

  // If the value of the invoke is used outside of its defining block, make it
  // available as a virtual register.
  // We already took care of the exported value for the statepoint instruction
  // during call to the LowerStatepoint.
  if (!isStatepoint(I)) {
    CopyToExportRegsIfNeeded(&I);
  }

  SmallVector<std::pair<MachineBasicBlock *, BranchProbability>, 1> UnwindDests;
  BranchProbabilityInfo *BPI = FuncInfo.BPI;
  BranchProbability EHPadBBProb =
      BPI ? BPI->getEdgeProbability(InvokeMBB->getBasicBlock(), EHPadBB)
          : BranchProbability::getZero();
  findUnwindDestinations(FuncInfo, EHPadBB, EHPadBBProb, UnwindDests);

  // Update successor info.
  addSuccessorWithProb(InvokeMBB, Return);
  for (auto &UnwindDest : UnwindDests) {
    UnwindDest.first->setIsEHPad();
    addSuccessorWithProb(InvokeMBB, UnwindDest.first, UnwindDest.second);
  }
  InvokeMBB->normalizeSuccProbs();

  // Drop into normal successor.
  DAG.setRoot(DAG.getNode(ISD::BR, getCurSDLoc(),
                          MVT::Other, getControlRoot(),
                          DAG.getBasicBlock(Return)));
}

void SelectionDAGBuilder::visitResume(const ResumeInst &RI) {
  llvm_unreachable("SelectionDAGBuilder shouldn't visit resume instructions!");
}

void SelectionDAGBuilder::visitLandingPad(const LandingPadInst &LP) {
  assert(FuncInfo.MBB->isEHPad() &&
         "Call to landingpad not in landing pad!");

  // If there aren't registers to copy the values into (e.g., during SjLj
  // exceptions), then don't bother to create these DAG nodes.
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  const Constant *PersonalityFn = FuncInfo.Fn->getPersonalityFn();
  if (TLI.getExceptionPointerRegister(PersonalityFn) == 0 &&
      TLI.getExceptionSelectorRegister(PersonalityFn) == 0)
    return;

  // If landingpad's return type is token type, we don't create DAG nodes
  // for its exception pointer and selector value. The extraction of exception
  // pointer or selector value from token type landingpads is not currently
  // supported.
  if (LP.getType()->isTokenTy())
    return;

  SmallVector<EVT, 2> ValueVTs;
  SDLoc dl = getCurSDLoc();
  ComputeValueVTs(TLI, DAG.getDataLayout(), LP.getType(), ValueVTs);
  assert(ValueVTs.size() == 2 && "Only two-valued landingpads are supported");

  // Get the two live-in registers as SDValues. The physregs have already been
  // copied into virtual registers.
  SDValue Ops[2];
  if (FuncInfo.ExceptionPointerVirtReg) {
    Ops[0] = DAG.getZExtOrTrunc(
        DAG.getCopyFromReg(DAG.getEntryNode(), dl,
                           FuncInfo.ExceptionPointerVirtReg,
                           TLI.getPointerTy(DAG.getDataLayout())),
        dl, ValueVTs[0]);
  } else {
    Ops[0] = DAG.getConstant(0, dl, TLI.getPointerTy(DAG.getDataLayout()));
  }
  Ops[1] = DAG.getZExtOrTrunc(
      DAG.getCopyFromReg(DAG.getEntryNode(), dl,
                         FuncInfo.ExceptionSelectorVirtReg,
                         TLI.getPointerTy(DAG.getDataLayout())),
      dl, ValueVTs[1]);

  // Merge into one.
  SDValue Res = DAG.getNode(ISD::MERGE_VALUES, dl,
                            DAG.getVTList(ValueVTs), Ops);
  setValue(&LP, Res);
}

void SelectionDAGBuilder::sortAndRangeify(CaseClusterVector &Clusters) {
#ifndef NDEBUG
  for (const CaseCluster &CC : Clusters)
    assert(CC.Low == CC.High && "Input clusters must be single-case");
#endif

  llvm::sort(Clusters, [](const CaseCluster &a, const CaseCluster &b) {
    return a.Low->getValue().slt(b.Low->getValue());
  });

  // Merge adjacent clusters with the same destination.
  const unsigned N = Clusters.size();
  unsigned DstIndex = 0;
  for (unsigned SrcIndex = 0; SrcIndex < N; ++SrcIndex) {
    CaseCluster &CC = Clusters[SrcIndex];
    const ConstantInt *CaseVal = CC.Low;
    MachineBasicBlock *Succ = CC.MBB;

    if (DstIndex != 0 && Clusters[DstIndex - 1].MBB == Succ &&
        (CaseVal->getValue() - Clusters[DstIndex - 1].High->getValue()) == 1) {
      // If this case has the same successor and is a neighbour, merge it into
      // the previous cluster.
      Clusters[DstIndex - 1].High = CaseVal;
      Clusters[DstIndex - 1].Prob += CC.Prob;
    } else {
      std::memmove(&Clusters[DstIndex++], &Clusters[SrcIndex],
                   sizeof(Clusters[SrcIndex]));
    }
  }
  Clusters.resize(DstIndex);
}

void SelectionDAGBuilder::UpdateSplitBlock(MachineBasicBlock *First,
                                           MachineBasicBlock *Last) {
  // Update JTCases.
  for (unsigned i = 0, e = JTCases.size(); i != e; ++i)
    if (JTCases[i].first.HeaderBB == First)
      JTCases[i].first.HeaderBB = Last;

  // Update BitTestCases.
  for (unsigned i = 0, e = BitTestCases.size(); i != e; ++i)
    if (BitTestCases[i].Parent == First)
      BitTestCases[i].Parent = Last;
}

void SelectionDAGBuilder::visitIndirectBr(const IndirectBrInst &I) {
  MachineBasicBlock *IndirectBrMBB = FuncInfo.MBB;

  // Update machine-CFG edges with unique successors.
  SmallSet<BasicBlock*, 32> Done;
  for (unsigned i = 0, e = I.getNumSuccessors(); i != e; ++i) {
    BasicBlock *BB = I.getSuccessor(i);
    bool Inserted = Done.insert(BB).second;
    if (!Inserted)
        continue;

    MachineBasicBlock *Succ = FuncInfo.MBBMap[BB];
    addSuccessorWithProb(IndirectBrMBB, Succ);
  }
  IndirectBrMBB->normalizeSuccProbs();

  DAG.setRoot(DAG.getNode(ISD::BRIND, getCurSDLoc(),
                          MVT::Other, getControlRoot(),
                          getValue(I.getAddress())));
}

void SelectionDAGBuilder::visitUnreachable(const UnreachableInst &I) {
  if (!DAG.getTarget().Options.TrapUnreachable)
    return;

  // We may be able to ignore unreachable behind a noreturn call.
  if (DAG.getTarget().Options.NoTrapAfterNoreturn) {
    const BasicBlock &BB = *I.getParent();
    if (&I != &BB.front()) {
      BasicBlock::const_iterator PredI =
        std::prev(BasicBlock::const_iterator(&I));
      if (const CallInst *Call = dyn_cast<CallInst>(&*PredI)) {
        if (Call->doesNotReturn())
          return;
      }
    }
  }

  DAG.setRoot(DAG.getNode(ISD::TRAP, getCurSDLoc(), MVT::Other, DAG.getRoot()));
}

void SelectionDAGBuilder::visitFSub(const User &I) {
  // -0.0 - X --> fneg
  Type *Ty = I.getType();
  if (isa<Constant>(I.getOperand(0)) &&
      I.getOperand(0) == ConstantFP::getZeroValueForNegation(Ty)) {
    SDValue Op2 = getValue(I.getOperand(1));
    setValue(&I, DAG.getNode(ISD::FNEG, getCurSDLoc(),
                             Op2.getValueType(), Op2));
    return;
  }

  visitBinary(I, ISD::FSUB);
}

/// Checks if the given instruction performs a vector reduction, in which case
/// we have the freedom to alter the elements in the result as long as the
/// reduction of them stays unchanged.
static bool isVectorReductionOp(const User *I) {
  const Instruction *Inst = dyn_cast<Instruction>(I);
  if (!Inst || !Inst->getType()->isVectorTy())
    return false;

  auto OpCode = Inst->getOpcode();
  switch (OpCode) {
  case Instruction::Add:
  case Instruction::Mul:
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
    break;
  case Instruction::FAdd:
  case Instruction::FMul:
    if (const FPMathOperator *FPOp = dyn_cast<const FPMathOperator>(Inst))
      if (FPOp->getFastMathFlags().isFast())
        break;
    LLVM_FALLTHROUGH;
  default:
    return false;
  }

  unsigned ElemNum = Inst->getType()->getVectorNumElements();
  // Ensure the reduction size is a power of 2.
  if (!isPowerOf2_32(ElemNum))
    return false;

  unsigned ElemNumToReduce = ElemNum;

  // Do DFS search on the def-use chain from the given instruction. We only
  // allow four kinds of operations during the search until we reach the
  // instruction that extracts the first element from the vector:
  //
  //   1. The reduction operation of the same opcode as the given instruction.
  //
  //   2. PHI node.
  //
  //   3. ShuffleVector instruction together with a reduction operation that
  //      does a partial reduction.
  //
  //   4. ExtractElement that extracts the first element from the vector, and we
  //      stop searching the def-use chain here.
  //
  // 3 & 4 above perform a reduction on all elements of the vector. We push defs
  // from 1-3 to the stack to continue the DFS. The given instruction is not
  // a reduction operation if we meet any other instructions other than those
  // listed above.

  SmallVector<const User *, 16> UsersToVisit{Inst};
  SmallPtrSet<const User *, 16> Visited;
  bool ReduxExtracted = false;

  while (!UsersToVisit.empty()) {
    auto User = UsersToVisit.back();
    UsersToVisit.pop_back();
    if (!Visited.insert(User).second)
      continue;

    for (const auto &U : User->users()) {
      auto Inst = dyn_cast<Instruction>(U);
      if (!Inst)
        return false;

      if (Inst->getOpcode() == OpCode || isa<PHINode>(U)) {
        if (const FPMathOperator *FPOp = dyn_cast<const FPMathOperator>(Inst))
          if (!isa<PHINode>(FPOp) && !FPOp->getFastMathFlags().isFast())
            return false;
        UsersToVisit.push_back(U);
      } else if (const ShuffleVectorInst *ShufInst =
                     dyn_cast<ShuffleVectorInst>(U)) {
        // Detect the following pattern: A ShuffleVector instruction together
        // with a reduction that do partial reduction on the first and second
        // ElemNumToReduce / 2 elements, and store the result in
        // ElemNumToReduce / 2 elements in another vector.

        unsigned ResultElements = ShufInst->getType()->getVectorNumElements();
        if (ResultElements < ElemNum)
          return false;

        if (ElemNumToReduce == 1)
          return false;
        if (!isa<UndefValue>(U->getOperand(1)))
          return false;
        for (unsigned i = 0; i < ElemNumToReduce / 2; ++i)
          if (ShufInst->getMaskValue(i) != int(i + ElemNumToReduce / 2))
            return false;
        for (unsigned i = ElemNumToReduce / 2; i < ElemNum; ++i)
          if (ShufInst->getMaskValue(i) != -1)
            return false;

        // There is only one user of this ShuffleVector instruction, which
        // must be a reduction operation.
        if (!U->hasOneUse())
          return false;

        auto U2 = dyn_cast<Instruction>(*U->user_begin());
        if (!U2 || U2->getOpcode() != OpCode)
          return false;

        // Check operands of the reduction operation.
        if ((U2->getOperand(0) == U->getOperand(0) && U2->getOperand(1) == U) ||
            (U2->getOperand(1) == U->getOperand(0) && U2->getOperand(0) == U)) {
          UsersToVisit.push_back(U2);
          ElemNumToReduce /= 2;
        } else
          return false;
      } else if (isa<ExtractElementInst>(U)) {
        // At this moment we should have reduced all elements in the vector.
        if (ElemNumToReduce != 1)
          return false;

        const ConstantInt *Val = dyn_cast<ConstantInt>(U->getOperand(1));
        if (!Val || !Val->isZero())
          return false;

        ReduxExtracted = true;
      } else
        return false;
    }
  }
  return ReduxExtracted;
}

void SelectionDAGBuilder::visitUnary(const User &I, unsigned Opcode) {
  SDNodeFlags Flags;

  SDValue Op = getValue(I.getOperand(0));
  SDValue UnNodeValue = DAG.getNode(Opcode, getCurSDLoc(), Op.getValueType(),
                                    Op, Flags);
  setValue(&I, UnNodeValue);
}

void SelectionDAGBuilder::visitBinary(const User &I, unsigned Opcode) {
  SDNodeFlags Flags;
  if (auto *OFBinOp = dyn_cast<OverflowingBinaryOperator>(&I)) {
    Flags.setNoSignedWrap(OFBinOp->hasNoSignedWrap());
    Flags.setNoUnsignedWrap(OFBinOp->hasNoUnsignedWrap());
  }
  if (auto *ExactOp = dyn_cast<PossiblyExactOperator>(&I)) {
    Flags.setExact(ExactOp->isExact());
  }
  if (isVectorReductionOp(&I)) {
    Flags.setVectorReduction(true);
    LLVM_DEBUG(dbgs() << "Detected a reduction operation:" << I << "\n");
  }

  SDValue Op1 = getValue(I.getOperand(0));
  SDValue Op2 = getValue(I.getOperand(1));
  SDValue BinNodeValue = DAG.getNode(Opcode, getCurSDLoc(), Op1.getValueType(),
                                     Op1, Op2, Flags);
  setValue(&I, BinNodeValue);
}

void SelectionDAGBuilder::visitShift(const User &I, unsigned Opcode) {
  SDValue Op1 = getValue(I.getOperand(0));
  SDValue Op2 = getValue(I.getOperand(1));

  EVT ShiftTy = DAG.getTargetLoweringInfo().getShiftAmountTy(
      Op1.getValueType(), DAG.getDataLayout());

  // Coerce the shift amount to the right type if we can.
  if (!I.getType()->isVectorTy() && Op2.getValueType() != ShiftTy) {
    unsigned ShiftSize = ShiftTy.getSizeInBits();
    unsigned Op2Size = Op2.getValueSizeInBits();
    SDLoc DL = getCurSDLoc();

    // If the operand is smaller than the shift count type, promote it.
    if (ShiftSize > Op2Size)
      Op2 = DAG.getNode(ISD::ZERO_EXTEND, DL, ShiftTy, Op2);

    // If the operand is larger than the shift count type but the shift
    // count type has enough bits to represent any shift value, truncate
    // it now. This is a common case and it exposes the truncate to
    // optimization early.
    else if (ShiftSize >= Log2_32_Ceil(Op2.getValueSizeInBits()))
      Op2 = DAG.getNode(ISD::TRUNCATE, DL, ShiftTy, Op2);
    // Otherwise we'll need to temporarily settle for some other convenient
    // type.  Type legalization will make adjustments once the shiftee is split.
    else
      Op2 = DAG.getZExtOrTrunc(Op2, DL, MVT::i32);
  }

  bool nuw = false;
  bool nsw = false;
  bool exact = false;

  if (Opcode == ISD::SRL || Opcode == ISD::SRA || Opcode == ISD::SHL) {

    if (const OverflowingBinaryOperator *OFBinOp =
            dyn_cast<const OverflowingBinaryOperator>(&I)) {
      nuw = OFBinOp->hasNoUnsignedWrap();
      nsw = OFBinOp->hasNoSignedWrap();
    }
    if (const PossiblyExactOperator *ExactOp =
            dyn_cast<const PossiblyExactOperator>(&I))
      exact = ExactOp->isExact();
  }
  SDNodeFlags Flags;
  Flags.setExact(exact);
  Flags.setNoSignedWrap(nsw);
  Flags.setNoUnsignedWrap(nuw);
  SDValue Res = DAG.getNode(Opcode, getCurSDLoc(), Op1.getValueType(), Op1, Op2,
                            Flags);
  setValue(&I, Res);
}

void SelectionDAGBuilder::visitSDiv(const User &I) {
  SDValue Op1 = getValue(I.getOperand(0));
  SDValue Op2 = getValue(I.getOperand(1));

  SDNodeFlags Flags;
  Flags.setExact(isa<PossiblyExactOperator>(&I) &&
                 cast<PossiblyExactOperator>(&I)->isExact());
  setValue(&I, DAG.getNode(ISD::SDIV, getCurSDLoc(), Op1.getValueType(), Op1,
                           Op2, Flags));
}

void SelectionDAGBuilder::visitICmp(const User &I) {
  ICmpInst::Predicate predicate = ICmpInst::BAD_ICMP_PREDICATE;
  if (const ICmpInst *IC = dyn_cast<ICmpInst>(&I))
    predicate = IC->getPredicate();
  else if (const ConstantExpr *IC = dyn_cast<ConstantExpr>(&I))
    predicate = ICmpInst::Predicate(IC->getPredicate());
  SDValue Op1 = getValue(I.getOperand(0));
  SDValue Op2 = getValue(I.getOperand(1));
  ISD::CondCode Opcode = getICmpCondCode(predicate);

  EVT DestVT = DAG.getTargetLoweringInfo().getValueType(DAG.getDataLayout(),
                                                        I.getType());
  setValue(&I, DAG.getSetCC(getCurSDLoc(), DestVT, Op1, Op2, Opcode));
}

void SelectionDAGBuilder::visitFCmp(const User &I) {
  FCmpInst::Predicate predicate = FCmpInst::BAD_FCMP_PREDICATE;
  if (const FCmpInst *FC = dyn_cast<FCmpInst>(&I))
    predicate = FC->getPredicate();
  else if (const ConstantExpr *FC = dyn_cast<ConstantExpr>(&I))
    predicate = FCmpInst::Predicate(FC->getPredicate());
  SDValue Op1 = getValue(I.getOperand(0));
  SDValue Op2 = getValue(I.getOperand(1));

  ISD::CondCode Condition = getFCmpCondCode(predicate);
  auto *FPMO = dyn_cast<FPMathOperator>(&I);
  if ((FPMO && FPMO->hasNoNaNs()) || TM.Options.NoNaNsFPMath)
    Condition = getFCmpCodeWithoutNaN(Condition);

  EVT DestVT = DAG.getTargetLoweringInfo().getValueType(DAG.getDataLayout(),
                                                        I.getType());
  setValue(&I, DAG.getSetCC(getCurSDLoc(), DestVT, Op1, Op2, Condition));
}

// Check if the condition of the select has one use or two users that are both
// selects with the same condition.
static bool hasOnlySelectUsers(const Value *Cond) {
  return llvm::all_of(Cond->users(), [](const Value *V) {
    return isa<SelectInst>(V);
  });
}

void SelectionDAGBuilder::visitSelect(const User &I) {
  SmallVector<EVT, 4> ValueVTs;
  ComputeValueVTs(DAG.getTargetLoweringInfo(), DAG.getDataLayout(), I.getType(),
                  ValueVTs);
  unsigned NumValues = ValueVTs.size();
  if (NumValues == 0) return;

  SmallVector<SDValue, 4> Values(NumValues);
  SDValue Cond     = getValue(I.getOperand(0));
  SDValue LHSVal   = getValue(I.getOperand(1));
  SDValue RHSVal   = getValue(I.getOperand(2));
  auto BaseOps = {Cond};
  ISD::NodeType OpCode = Cond.getValueType().isVector() ?
    ISD::VSELECT : ISD::SELECT;

  // Min/max matching is only viable if all output VTs are the same.
  if (is_splat(ValueVTs)) {
    EVT VT = ValueVTs[0];
    LLVMContext &Ctx = *DAG.getContext();
    auto &TLI = DAG.getTargetLoweringInfo();

    // We care about the legality of the operation after it has been type
    // legalized.
    while (TLI.getTypeAction(Ctx, VT) != TargetLoweringBase::TypeLegal &&
           VT != TLI.getTypeToTransformTo(Ctx, VT))
      VT = TLI.getTypeToTransformTo(Ctx, VT);

    // If the vselect is legal, assume we want to leave this as a vector setcc +
    // vselect. Otherwise, if this is going to be scalarized, we want to see if
    // min/max is legal on the scalar type.
    bool UseScalarMinMax = VT.isVector() &&
      !TLI.isOperationLegalOrCustom(ISD::VSELECT, VT);

    Value *LHS, *RHS;
    auto SPR = matchSelectPattern(const_cast<User*>(&I), LHS, RHS);
    ISD::NodeType Opc = ISD::DELETED_NODE;
    switch (SPR.Flavor) {
    case SPF_UMAX:    Opc = ISD::UMAX; break;
    case SPF_UMIN:    Opc = ISD::UMIN; break;
    case SPF_SMAX:    Opc = ISD::SMAX; break;
    case SPF_SMIN:    Opc = ISD::SMIN; break;
    case SPF_FMINNUM:
      switch (SPR.NaNBehavior) {
      case SPNB_NA: llvm_unreachable("No NaN behavior for FP op?");
      case SPNB_RETURNS_NAN:   Opc = ISD::FMINIMUM; break;
      case SPNB_RETURNS_OTHER: Opc = ISD::FMINNUM; break;
      case SPNB_RETURNS_ANY: {
        if (TLI.isOperationLegalOrCustom(ISD::FMINNUM, VT))
          Opc = ISD::FMINNUM;
        else if (TLI.isOperationLegalOrCustom(ISD::FMINIMUM, VT))
          Opc = ISD::FMINIMUM;
        else if (UseScalarMinMax)
          Opc = TLI.isOperationLegalOrCustom(ISD::FMINNUM, VT.getScalarType()) ?
            ISD::FMINNUM : ISD::FMINIMUM;
        break;
      }
      }
      break;
    case SPF_FMAXNUM:
      switch (SPR.NaNBehavior) {
      case SPNB_NA: llvm_unreachable("No NaN behavior for FP op?");
      case SPNB_RETURNS_NAN:   Opc = ISD::FMAXIMUM; break;
      case SPNB_RETURNS_OTHER: Opc = ISD::FMAXNUM; break;
      case SPNB_RETURNS_ANY:

        if (TLI.isOperationLegalOrCustom(ISD::FMAXNUM, VT))
          Opc = ISD::FMAXNUM;
        else if (TLI.isOperationLegalOrCustom(ISD::FMAXIMUM, VT))
          Opc = ISD::FMAXIMUM;
        else if (UseScalarMinMax)
          Opc = TLI.isOperationLegalOrCustom(ISD::FMAXNUM, VT.getScalarType()) ?
            ISD::FMAXNUM : ISD::FMAXIMUM;
        break;
      }
      break;
    default: break;
    }

    if (Opc != ISD::DELETED_NODE &&
        (TLI.isOperationLegalOrCustom(Opc, VT) ||
         (UseScalarMinMax &&
          TLI.isOperationLegalOrCustom(Opc, VT.getScalarType()))) &&
        // If the underlying comparison instruction is used by any other
        // instruction, the consumed instructions won't be destroyed, so it is
        // not profitable to convert to a min/max.
        hasOnlySelectUsers(cast<SelectInst>(I).getCondition())) {
      OpCode = Opc;
      LHSVal = getValue(LHS);
      RHSVal = getValue(RHS);
      BaseOps = {};
    }
  }

  for (unsigned i = 0; i != NumValues; ++i) {
    SmallVector<SDValue, 3> Ops(BaseOps.begin(), BaseOps.end());
    Ops.push_back(SDValue(LHSVal.getNode(), LHSVal.getResNo() + i));
    Ops.push_back(SDValue(RHSVal.getNode(), RHSVal.getResNo() + i));
    Values[i] = DAG.getNode(OpCode, getCurSDLoc(),
                            LHSVal.getNode()->getValueType(LHSVal.getResNo()+i),
                            Ops);
  }

  setValue(&I, DAG.getNode(ISD::MERGE_VALUES, getCurSDLoc(),
                           DAG.getVTList(ValueVTs), Values));
}

void SelectionDAGBuilder::visitTrunc(const User &I) {
  // TruncInst cannot be a no-op cast because sizeof(src) > sizeof(dest).
  SDValue N = getValue(I.getOperand(0));
  EVT DestVT = DAG.getTargetLoweringInfo().getValueType(DAG.getDataLayout(),
                                                        I.getType());
  setValue(&I, DAG.getNode(ISD::TRUNCATE, getCurSDLoc(), DestVT, N));
}

void SelectionDAGBuilder::visitZExt(const User &I) {
  // ZExt cannot be a no-op cast because sizeof(src) < sizeof(dest).
  // ZExt also can't be a cast to bool for same reason. So, nothing much to do
  SDValue N = getValue(I.getOperand(0));
  EVT DestVT = DAG.getTargetLoweringInfo().getValueType(DAG.getDataLayout(),
                                                        I.getType());
  setValue(&I, DAG.getNode(ISD::ZERO_EXTEND, getCurSDLoc(), DestVT, N));
}

void SelectionDAGBuilder::visitSExt(const User &I) {
  // SExt cannot be a no-op cast because sizeof(src) < sizeof(dest).
  // SExt also can't be a cast to bool for same reason. So, nothing much to do
  SDValue N = getValue(I.getOperand(0));
  EVT DestVT = DAG.getTargetLoweringInfo().getValueType(DAG.getDataLayout(),
                                                        I.getType());
  setValue(&I, DAG.getNode(ISD::SIGN_EXTEND, getCurSDLoc(), DestVT, N));
}

void SelectionDAGBuilder::visitFPTrunc(const User &I) {
  // FPTrunc is never a no-op cast, no need to check
  SDValue N = getValue(I.getOperand(0));
  SDLoc dl = getCurSDLoc();
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  EVT DestVT = TLI.getValueType(DAG.getDataLayout(), I.getType());
  setValue(&I, DAG.getNode(ISD::FP_ROUND, dl, DestVT, N,
                           DAG.getTargetConstant(
                               0, dl, TLI.getPointerTy(DAG.getDataLayout()))));
}

void SelectionDAGBuilder::visitFPExt(const User &I) {
  // FPExt is never a no-op cast, no need to check
  SDValue N = getValue(I.getOperand(0));
  EVT DestVT = DAG.getTargetLoweringInfo().getValueType(DAG.getDataLayout(),
                                                        I.getType());
  setValue(&I, DAG.getNode(ISD::FP_EXTEND, getCurSDLoc(), DestVT, N));
}

void SelectionDAGBuilder::visitFPToUI(const User &I) {
  // FPToUI is never a no-op cast, no need to check
  SDValue N = getValue(I.getOperand(0));
  EVT DestVT = DAG.getTargetLoweringInfo().getValueType(DAG.getDataLayout(),
                                                        I.getType());
  setValue(&I, DAG.getNode(ISD::FP_TO_UINT, getCurSDLoc(), DestVT, N));
}

void SelectionDAGBuilder::visitFPToSI(const User &I) {
  // FPToSI is never a no-op cast, no need to check
  SDValue N = getValue(I.getOperand(0));
  EVT DestVT = DAG.getTargetLoweringInfo().getValueType(DAG.getDataLayout(),
                                                        I.getType());
  setValue(&I, DAG.getNode(ISD::FP_TO_SINT, getCurSDLoc(), DestVT, N));
}

void SelectionDAGBuilder::visitUIToFP(const User &I) {
  // UIToFP is never a no-op cast, no need to check
  SDValue N = getValue(I.getOperand(0));
  EVT DestVT = DAG.getTargetLoweringInfo().getValueType(DAG.getDataLayout(),
                                                        I.getType());
  setValue(&I, DAG.getNode(ISD::UINT_TO_FP, getCurSDLoc(), DestVT, N));
}

void SelectionDAGBuilder::visitSIToFP(const User &I) {
  // SIToFP is never a no-op cast, no need to check
  SDValue N = getValue(I.getOperand(0));
  EVT DestVT = DAG.getTargetLoweringInfo().getValueType(DAG.getDataLayout(),
                                                        I.getType());
  setValue(&I, DAG.getNode(ISD::SINT_TO_FP, getCurSDLoc(), DestVT, N));
}

void SelectionDAGBuilder::visitPtrToInt(const User &I) {
  // What to do depends on the size of the integer and the size of the pointer.
  // We can either truncate, zero extend, or no-op, accordingly.
  SDValue N = getValue(I.getOperand(0));
  EVT DestVT = DAG.getTargetLoweringInfo().getValueType(DAG.getDataLayout(),
                                                        I.getType());
  setValue(&I, DAG.getZExtOrTrunc(N, getCurSDLoc(), DestVT));
}

void SelectionDAGBuilder::visitIntToPtr(const User &I) {
  // What to do depends on the size of the integer and the size of the pointer.
  // We can either truncate, zero extend, or no-op, accordingly.
  SDValue N = getValue(I.getOperand(0));
  EVT DestVT = DAG.getTargetLoweringInfo().getValueType(DAG.getDataLayout(),
                                                        I.getType());
  setValue(&I, DAG.getZExtOrTrunc(N, getCurSDLoc(), DestVT));
}

void SelectionDAGBuilder::visitBitCast(const User &I) {
  SDValue N = getValue(I.getOperand(0));
  SDLoc dl = getCurSDLoc();
  EVT DestVT = DAG.getTargetLoweringInfo().getValueType(DAG.getDataLayout(),
                                                        I.getType());

  // BitCast assures us that source and destination are the same size so this is
  // either a BITCAST or a no-op.
  if (DestVT != N.getValueType())
    setValue(&I, DAG.getNode(ISD::BITCAST, dl,
                             DestVT, N)); // convert types.
  // Check if the original LLVM IR Operand was a ConstantInt, because getValue()
  // might fold any kind of constant expression to an integer constant and that
  // is not what we are looking for. Only recognize a bitcast of a genuine
  // constant integer as an opaque constant.
  else if(ConstantInt *C = dyn_cast<ConstantInt>(I.getOperand(0)))
    setValue(&I, DAG.getConstant(C->getValue(), dl, DestVT, /*isTarget=*/false,
                                 /*isOpaque*/true));
  else
    setValue(&I, N);            // noop cast.
}

void SelectionDAGBuilder::visitAddrSpaceCast(const User &I) {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  const Value *SV = I.getOperand(0);
  SDValue N = getValue(SV);
  EVT DestVT = TLI.getValueType(DAG.getDataLayout(), I.getType());

  unsigned SrcAS = SV->getType()->getPointerAddressSpace();
  unsigned DestAS = I.getType()->getPointerAddressSpace();

  if (!TLI.isNoopAddrSpaceCast(SrcAS, DestAS))
    N = DAG.getAddrSpaceCast(getCurSDLoc(), DestVT, N, SrcAS, DestAS);

  setValue(&I, N);
}

void SelectionDAGBuilder::visitInsertElement(const User &I) {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  SDValue InVec = getValue(I.getOperand(0));
  SDValue InVal = getValue(I.getOperand(1));
  SDValue InIdx = DAG.getSExtOrTrunc(getValue(I.getOperand(2)), getCurSDLoc(),
                                     TLI.getVectorIdxTy(DAG.getDataLayout()));
  setValue(&I, DAG.getNode(ISD::INSERT_VECTOR_ELT, getCurSDLoc(),
                           TLI.getValueType(DAG.getDataLayout(), I.getType()),
                           InVec, InVal, InIdx));
}

void SelectionDAGBuilder::visitExtractElement(const User &I) {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  SDValue InVec = getValue(I.getOperand(0));
  SDValue InIdx = DAG.getSExtOrTrunc(getValue(I.getOperand(1)), getCurSDLoc(),
                                     TLI.getVectorIdxTy(DAG.getDataLayout()));
  setValue(&I, DAG.getNode(ISD::EXTRACT_VECTOR_ELT, getCurSDLoc(),
                           TLI.getValueType(DAG.getDataLayout(), I.getType()),
                           InVec, InIdx));
}

void SelectionDAGBuilder::visitShuffleVector(const User &I) {
  SDValue Src1 = getValue(I.getOperand(0));
  SDValue Src2 = getValue(I.getOperand(1));
  SDLoc DL = getCurSDLoc();

  SmallVector<int, 8> Mask;
  ShuffleVectorInst::getShuffleMask(cast<Constant>(I.getOperand(2)), Mask);
  unsigned MaskNumElts = Mask.size();

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  EVT VT = TLI.getValueType(DAG.getDataLayout(), I.getType());
  EVT SrcVT = Src1.getValueType();
  unsigned SrcNumElts = SrcVT.getVectorNumElements();

  if (SrcNumElts == MaskNumElts) {
    setValue(&I, DAG.getVectorShuffle(VT, DL, Src1, Src2, Mask));
    return;
  }

  // Normalize the shuffle vector since mask and vector length don't match.
  if (SrcNumElts < MaskNumElts) {
    // Mask is longer than the source vectors. We can use concatenate vector to
    // make the mask and vectors lengths match.

    if (MaskNumElts % SrcNumElts == 0) {
      // Mask length is a multiple of the source vector length.
      // Check if the shuffle is some kind of concatenation of the input
      // vectors.
      unsigned NumConcat = MaskNumElts / SrcNumElts;
      bool IsConcat = true;
      SmallVector<int, 8> ConcatSrcs(NumConcat, -1);
      for (unsigned i = 0; i != MaskNumElts; ++i) {
        int Idx = Mask[i];
        if (Idx < 0)
          continue;
        // Ensure the indices in each SrcVT sized piece are sequential and that
        // the same source is used for the whole piece.
        if ((Idx % SrcNumElts != (i % SrcNumElts)) ||
            (ConcatSrcs[i / SrcNumElts] >= 0 &&
             ConcatSrcs[i / SrcNumElts] != (int)(Idx / SrcNumElts))) {
          IsConcat = false;
          break;
        }
        // Remember which source this index came from.
        ConcatSrcs[i / SrcNumElts] = Idx / SrcNumElts;
      }

      // The shuffle is concatenating multiple vectors together. Just emit
      // a CONCAT_VECTORS operation.
      if (IsConcat) {
        SmallVector<SDValue, 8> ConcatOps;
        for (auto Src : ConcatSrcs) {
          if (Src < 0)
            ConcatOps.push_back(DAG.getUNDEF(SrcVT));
          else if (Src == 0)
            ConcatOps.push_back(Src1);
          else
            ConcatOps.push_back(Src2);
        }
        setValue(&I, DAG.getNode(ISD::CONCAT_VECTORS, DL, VT, ConcatOps));
        return;
      }
    }

    unsigned PaddedMaskNumElts = alignTo(MaskNumElts, SrcNumElts);
    unsigned NumConcat = PaddedMaskNumElts / SrcNumElts;
    EVT PaddedVT = EVT::getVectorVT(*DAG.getContext(), VT.getScalarType(),
                                    PaddedMaskNumElts);

    // Pad both vectors with undefs to make them the same length as the mask.
    SDValue UndefVal = DAG.getUNDEF(SrcVT);

    SmallVector<SDValue, 8> MOps1(NumConcat, UndefVal);
    SmallVector<SDValue, 8> MOps2(NumConcat, UndefVal);
    MOps1[0] = Src1;
    MOps2[0] = Src2;

    Src1 = Src1.isUndef()
               ? DAG.getUNDEF(PaddedVT)
               : DAG.getNode(ISD::CONCAT_VECTORS, DL, PaddedVT, MOps1);
    Src2 = Src2.isUndef()
               ? DAG.getUNDEF(PaddedVT)
               : DAG.getNode(ISD::CONCAT_VECTORS, DL, PaddedVT, MOps2);

    // Readjust mask for new input vector length.
    SmallVector<int, 8> MappedOps(PaddedMaskNumElts, -1);
    for (unsigned i = 0; i != MaskNumElts; ++i) {
      int Idx = Mask[i];
      if (Idx >= (int)SrcNumElts)
        Idx -= SrcNumElts - PaddedMaskNumElts;
      MappedOps[i] = Idx;
    }

    SDValue Result = DAG.getVectorShuffle(PaddedVT, DL, Src1, Src2, MappedOps);

    // If the concatenated vector was padded, extract a subvector with the
    // correct number of elements.
    if (MaskNumElts != PaddedMaskNumElts)
      Result = DAG.getNode(
          ISD::EXTRACT_SUBVECTOR, DL, VT, Result,
          DAG.getConstant(0, DL, TLI.getVectorIdxTy(DAG.getDataLayout())));

    setValue(&I, Result);
    return;
  }

  if (SrcNumElts > MaskNumElts) {
    // Analyze the access pattern of the vector to see if we can extract
    // two subvectors and do the shuffle.
    int StartIdx[2] = { -1, -1 };  // StartIdx to extract from
    bool CanExtract = true;
    for (int Idx : Mask) {
      unsigned Input = 0;
      if (Idx < 0)
        continue;

      if (Idx >= (int)SrcNumElts) {
        Input = 1;
        Idx -= SrcNumElts;
      }

      // If all the indices come from the same MaskNumElts sized portion of
      // the sources we can use extract. Also make sure the extract wouldn't
      // extract past the end of the source.
      int NewStartIdx = alignDown(Idx, MaskNumElts);
      if (NewStartIdx + MaskNumElts > SrcNumElts ||
          (StartIdx[Input] >= 0 && StartIdx[Input] != NewStartIdx))
        CanExtract = false;
      // Make sure we always update StartIdx as we use it to track if all
      // elements are undef.
      StartIdx[Input] = NewStartIdx;
    }

    if (StartIdx[0] < 0 && StartIdx[1] < 0) {
      setValue(&I, DAG.getUNDEF(VT)); // Vectors are not used.
      return;
    }
    if (CanExtract) {
      // Extract appropriate subvector and generate a vector shuffle
      for (unsigned Input = 0; Input < 2; ++Input) {
        SDValue &Src = Input == 0 ? Src1 : Src2;
        if (StartIdx[Input] < 0)
          Src = DAG.getUNDEF(VT);
        else {
          Src = DAG.getNode(
              ISD::EXTRACT_SUBVECTOR, DL, VT, Src,
              DAG.getConstant(StartIdx[Input], DL,
                              TLI.getVectorIdxTy(DAG.getDataLayout())));
        }
      }

      // Calculate new mask.
      SmallVector<int, 8> MappedOps(Mask.begin(), Mask.end());
      for (int &Idx : MappedOps) {
        if (Idx >= (int)SrcNumElts)
          Idx -= SrcNumElts + StartIdx[1] - MaskNumElts;
        else if (Idx >= 0)
          Idx -= StartIdx[0];
      }

      setValue(&I, DAG.getVectorShuffle(VT, DL, Src1, Src2, MappedOps));
      return;
    }
  }

  // We can't use either concat vectors or extract subvectors so fall back to
  // replacing the shuffle with extract and build vector.
  // to insert and build vector.
  EVT EltVT = VT.getVectorElementType();
  EVT IdxVT = TLI.getVectorIdxTy(DAG.getDataLayout());
  SmallVector<SDValue,8> Ops;
  for (int Idx : Mask) {
    SDValue Res;

    if (Idx < 0) {
      Res = DAG.getUNDEF(EltVT);
    } else {
      SDValue &Src = Idx < (int)SrcNumElts ? Src1 : Src2;
      if (Idx >= (int)SrcNumElts) Idx -= SrcNumElts;

      Res = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL,
                        EltVT, Src, DAG.getConstant(Idx, DL, IdxVT));
    }

    Ops.push_back(Res);
  }

  setValue(&I, DAG.getBuildVector(VT, DL, Ops));
}

void SelectionDAGBuilder::visitInsertValue(const User &I) {
  ArrayRef<unsigned> Indices;
  if (const InsertValueInst *IV = dyn_cast<InsertValueInst>(&I))
    Indices = IV->getIndices();
  else
    Indices = cast<ConstantExpr>(&I)->getIndices();

  const Value *Op0 = I.getOperand(0);
  const Value *Op1 = I.getOperand(1);
  Type *AggTy = I.getType();
  Type *ValTy = Op1->getType();
  bool IntoUndef = isa<UndefValue>(Op0);
  bool FromUndef = isa<UndefValue>(Op1);

  unsigned LinearIndex = ComputeLinearIndex(AggTy, Indices);

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  SmallVector<EVT, 4> AggValueVTs;
  ComputeValueVTs(TLI, DAG.getDataLayout(), AggTy, AggValueVTs);
  SmallVector<EVT, 4> ValValueVTs;
  ComputeValueVTs(TLI, DAG.getDataLayout(), ValTy, ValValueVTs);

  unsigned NumAggValues = AggValueVTs.size();
  unsigned NumValValues = ValValueVTs.size();
  SmallVector<SDValue, 4> Values(NumAggValues);

  // Ignore an insertvalue that produces an empty object
  if (!NumAggValues) {
    setValue(&I, DAG.getUNDEF(MVT(MVT::Other)));
    return;
  }

  SDValue Agg = getValue(Op0);
  unsigned i = 0;
  // Copy the beginning value(s) from the original aggregate.
  for (; i != LinearIndex; ++i)
    Values[i] = IntoUndef ? DAG.getUNDEF(AggValueVTs[i]) :
                SDValue(Agg.getNode(), Agg.getResNo() + i);
  // Copy values from the inserted value(s).
  if (NumValValues) {
    SDValue Val = getValue(Op1);
    for (; i != LinearIndex + NumValValues; ++i)
      Values[i] = FromUndef ? DAG.getUNDEF(AggValueVTs[i]) :
                  SDValue(Val.getNode(), Val.getResNo() + i - LinearIndex);
  }
  // Copy remaining value(s) from the original aggregate.
  for (; i != NumAggValues; ++i)
    Values[i] = IntoUndef ? DAG.getUNDEF(AggValueVTs[i]) :
                SDValue(Agg.getNode(), Agg.getResNo() + i);

  setValue(&I, DAG.getNode(ISD::MERGE_VALUES, getCurSDLoc(),
                           DAG.getVTList(AggValueVTs), Values));
}

void SelectionDAGBuilder::visitExtractValue(const User &I) {
  ArrayRef<unsigned> Indices;
  if (const ExtractValueInst *EV = dyn_cast<ExtractValueInst>(&I))
    Indices = EV->getIndices();
  else
    Indices = cast<ConstantExpr>(&I)->getIndices();

  const Value *Op0 = I.getOperand(0);
  Type *AggTy = Op0->getType();
  Type *ValTy = I.getType();
  bool OutOfUndef = isa<UndefValue>(Op0);

  unsigned LinearIndex = ComputeLinearIndex(AggTy, Indices);

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  SmallVector<EVT, 4> ValValueVTs;
  ComputeValueVTs(TLI, DAG.getDataLayout(), ValTy, ValValueVTs);

  unsigned NumValValues = ValValueVTs.size();

  // Ignore a extractvalue that produces an empty object
  if (!NumValValues) {
    setValue(&I, DAG.getUNDEF(MVT(MVT::Other)));
    return;
  }

  SmallVector<SDValue, 4> Values(NumValValues);

  SDValue Agg = getValue(Op0);
  // Copy out the selected value(s).
  for (unsigned i = LinearIndex; i != LinearIndex + NumValValues; ++i)
    Values[i - LinearIndex] =
      OutOfUndef ?
        DAG.getUNDEF(Agg.getNode()->getValueType(Agg.getResNo() + i)) :
        SDValue(Agg.getNode(), Agg.getResNo() + i);

  setValue(&I, DAG.getNode(ISD::MERGE_VALUES, getCurSDLoc(),
                           DAG.getVTList(ValValueVTs), Values));
}

void SelectionDAGBuilder::visitGetElementPtr(const User &I) {
  Value *Op0 = I.getOperand(0);
  // Note that the pointer operand may be a vector of pointers. Take the scalar
  // element which holds a pointer.
  unsigned AS = Op0->getType()->getScalarType()->getPointerAddressSpace();
  SDValue N = getValue(Op0);
  SDLoc dl = getCurSDLoc();

  // Normalize Vector GEP - all scalar operands should be converted to the
  // splat vector.
  unsigned VectorWidth = I.getType()->isVectorTy() ?
    cast<VectorType>(I.getType())->getVectorNumElements() : 0;

  if (VectorWidth && !N.getValueType().isVector()) {
    LLVMContext &Context = *DAG.getContext();
    EVT VT = EVT::getVectorVT(Context, N.getValueType(), VectorWidth);
    N = DAG.getSplatBuildVector(VT, dl, N);
  }

  for (gep_type_iterator GTI = gep_type_begin(&I), E = gep_type_end(&I);
       GTI != E; ++GTI) {
    const Value *Idx = GTI.getOperand();
    if (StructType *StTy = GTI.getStructTypeOrNull()) {
      unsigned Field = cast<Constant>(Idx)->getUniqueInteger().getZExtValue();
      if (Field) {
        // N = N + Offset
        uint64_t Offset = DL->getStructLayout(StTy)->getElementOffset(Field);

        // In an inbounds GEP with an offset that is nonnegative even when
        // interpreted as signed, assume there is no unsigned overflow.
        SDNodeFlags Flags;
        if (int64_t(Offset) >= 0 && cast<GEPOperator>(I).isInBounds())
          Flags.setNoUnsignedWrap(true);

        N = DAG.getNode(ISD::ADD, dl, N.getValueType(), N,
                        DAG.getConstant(Offset, dl, N.getValueType()), Flags);
      }
    } else {
      unsigned IdxSize = DAG.getDataLayout().getIndexSizeInBits(AS);
      MVT IdxTy = MVT::getIntegerVT(IdxSize);
      APInt ElementSize(IdxSize, DL->getTypeAllocSize(GTI.getIndexedType()));

      // If this is a scalar constant or a splat vector of constants,
      // handle it quickly.
      const auto *CI = dyn_cast<ConstantInt>(Idx);
      if (!CI && isa<ConstantDataVector>(Idx) &&
          cast<ConstantDataVector>(Idx)->getSplatValue())
        CI = cast<ConstantInt>(cast<ConstantDataVector>(Idx)->getSplatValue());

      if (CI) {
        if (CI->isZero())
          continue;
        APInt Offs = ElementSize * CI->getValue().sextOrTrunc(IdxSize);
        LLVMContext &Context = *DAG.getContext();
        SDValue OffsVal = VectorWidth ?
          DAG.getConstant(Offs, dl, EVT::getVectorVT(Context, IdxTy, VectorWidth)) :
          DAG.getConstant(Offs, dl, IdxTy);

        // In an inbouds GEP with an offset that is nonnegative even when
        // interpreted as signed, assume there is no unsigned overflow.
        SDNodeFlags Flags;
        if (Offs.isNonNegative() && cast<GEPOperator>(I).isInBounds())
          Flags.setNoUnsignedWrap(true);

        N = DAG.getNode(ISD::ADD, dl, N.getValueType(), N, OffsVal, Flags);
        continue;
      }

      // N = N + Idx * ElementSize;
      SDValue IdxN = getValue(Idx);

      if (!IdxN.getValueType().isVector() && VectorWidth) {
        EVT VT = EVT::getVectorVT(*Context, IdxN.getValueType(), VectorWidth);
        IdxN = DAG.getSplatBuildVector(VT, dl, IdxN);
      }

      // If the index is smaller or larger than intptr_t, truncate or extend
      // it.
      IdxN = DAG.getSExtOrTrunc(IdxN, dl, N.getValueType());

      // If this is a multiply by a power of two, turn it into a shl
      // immediately.  This is a very common case.
      if (ElementSize != 1) {
        if (ElementSize.isPowerOf2()) {
          unsigned Amt = ElementSize.logBase2();
          IdxN = DAG.getNode(ISD::SHL, dl,
                             N.getValueType(), IdxN,
                             DAG.getConstant(Amt, dl, IdxN.getValueType()));
        } else {
          SDValue Scale = DAG.getConstant(ElementSize, dl, IdxN.getValueType());
          IdxN = DAG.getNode(ISD::MUL, dl,
                             N.getValueType(), IdxN, Scale);
        }
      }

      N = DAG.getNode(ISD::ADD, dl,
                      N.getValueType(), N, IdxN);
    }
  }

  setValue(&I, N);
}

void SelectionDAGBuilder::visitAlloca(const AllocaInst &I) {
  // If this is a fixed sized alloca in the entry block of the function,
  // allocate it statically on the stack.
  if (FuncInfo.StaticAllocaMap.count(&I))
    return;   // getValue will auto-populate this.

  SDLoc dl = getCurSDLoc();
  Type *Ty = I.getAllocatedType();
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  auto &DL = DAG.getDataLayout();
  uint64_t TySize = DL.getTypeAllocSize(Ty);
  unsigned Align =
      std::max((unsigned)DL.getPrefTypeAlignment(Ty), I.getAlignment());

  SDValue AllocSize = getValue(I.getArraySize());

  EVT IntPtr = TLI.getPointerTy(DAG.getDataLayout(), DL.getAllocaAddrSpace());
  if (AllocSize.getValueType() != IntPtr)
    AllocSize = DAG.getZExtOrTrunc(AllocSize, dl, IntPtr);

  AllocSize = DAG.getNode(ISD::MUL, dl, IntPtr,
                          AllocSize,
                          DAG.getConstant(TySize, dl, IntPtr));

  // Handle alignment.  If the requested alignment is less than or equal to
  // the stack alignment, ignore it.  If the size is greater than or equal to
  // the stack alignment, we note this in the DYNAMIC_STACKALLOC node.
  unsigned StackAlign =
      DAG.getSubtarget().getFrameLowering()->getStackAlignment();
  if (Align <= StackAlign)
    Align = 0;

  // Round the size of the allocation up to the stack alignment size
  // by add SA-1 to the size. This doesn't overflow because we're computing
  // an address inside an alloca.
  SDNodeFlags Flags;
  Flags.setNoUnsignedWrap(true);
  AllocSize = DAG.getNode(ISD::ADD, dl, AllocSize.getValueType(), AllocSize,
                          DAG.getConstant(StackAlign - 1, dl, IntPtr), Flags);

  // Mask out the low bits for alignment purposes.
  AllocSize =
      DAG.getNode(ISD::AND, dl, AllocSize.getValueType(), AllocSize,
                  DAG.getConstant(~(uint64_t)(StackAlign - 1), dl, IntPtr));

  SDValue Ops[] = {getRoot(), AllocSize, DAG.getConstant(Align, dl, IntPtr)};
  SDVTList VTs = DAG.getVTList(AllocSize.getValueType(), MVT::Other);
  SDValue DSA = DAG.getNode(ISD::DYNAMIC_STACKALLOC, dl, VTs, Ops);
  setValue(&I, DSA);
  DAG.setRoot(DSA.getValue(1));

  assert(FuncInfo.MF->getFrameInfo().hasVarSizedObjects());
}

void SelectionDAGBuilder::visitLoad(const LoadInst &I) {
  if (I.isAtomic())
    return visitAtomicLoad(I);

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  const Value *SV = I.getOperand(0);
  if (TLI.supportSwiftError()) {
    // Swifterror values can come from either a function parameter with
    // swifterror attribute or an alloca with swifterror attribute.
    if (const Argument *Arg = dyn_cast<Argument>(SV)) {
      if (Arg->hasSwiftErrorAttr())
        return visitLoadFromSwiftError(I);
    }

    if (const AllocaInst *Alloca = dyn_cast<AllocaInst>(SV)) {
      if (Alloca->isSwiftError())
        return visitLoadFromSwiftError(I);
    }
  }

  SDValue Ptr = getValue(SV);

  Type *Ty = I.getType();

  bool isVolatile = I.isVolatile();
  bool isNonTemporal = I.getMetadata(LLVMContext::MD_nontemporal) != nullptr;
  bool isInvariant = I.getMetadata(LLVMContext::MD_invariant_load) != nullptr;
  bool isDereferenceable = isDereferenceablePointer(SV, DAG.getDataLayout());
  unsigned Alignment = I.getAlignment();

  AAMDNodes AAInfo;
  I.getAAMetadata(AAInfo);
  const MDNode *Ranges = I.getMetadata(LLVMContext::MD_range);

  SmallVector<EVT, 4> ValueVTs;
  SmallVector<uint64_t, 4> Offsets;
  ComputeValueVTs(TLI, DAG.getDataLayout(), Ty, ValueVTs, &Offsets);
  unsigned NumValues = ValueVTs.size();
  if (NumValues == 0)
    return;

  SDValue Root;
  bool ConstantMemory = false;
  if (isVolatile || NumValues > MaxParallelChains)
    // Serialize volatile loads with other side effects.
    Root = getRoot();
  else if (AA &&
           AA->pointsToConstantMemory(MemoryLocation(
               SV,
               LocationSize::precise(DAG.getDataLayout().getTypeStoreSize(Ty)),
               AAInfo))) {
    // Do not serialize (non-volatile) loads of constant memory with anything.
    Root = DAG.getEntryNode();
    ConstantMemory = true;
  } else {
    // Do not serialize non-volatile loads against each other.
    Root = DAG.getRoot();
  }

  SDLoc dl = getCurSDLoc();

  if (isVolatile)
    Root = TLI.prepareVolatileOrAtomicLoad(Root, dl, DAG);

  // An aggregate load cannot wrap around the address space, so offsets to its
  // parts don't wrap either.
  SDNodeFlags Flags;
  Flags.setNoUnsignedWrap(true);

  SmallVector<SDValue, 4> Values(NumValues);
  SmallVector<SDValue, 4> Chains(std::min(MaxParallelChains, NumValues));
  EVT PtrVT = Ptr.getValueType();
  unsigned ChainI = 0;
  for (unsigned i = 0; i != NumValues; ++i, ++ChainI) {
    // Serializing loads here may result in excessive register pressure, and
    // TokenFactor places arbitrary choke points on the scheduler. SD scheduling
    // could recover a bit by hoisting nodes upward in the chain by recognizing
    // they are side-effect free or do not alias. The optimizer should really
    // avoid this case by converting large object/array copies to llvm.memcpy
    // (MaxParallelChains should always remain as failsafe).
    if (ChainI == MaxParallelChains) {
      assert(PendingLoads.empty() && "PendingLoads must be serialized first");
      SDValue Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other,
                                  makeArrayRef(Chains.data(), ChainI));
      Root = Chain;
      ChainI = 0;
    }
    SDValue A = DAG.getNode(ISD::ADD, dl,
                            PtrVT, Ptr,
                            DAG.getConstant(Offsets[i], dl, PtrVT),
                            Flags);
    auto MMOFlags = MachineMemOperand::MONone;
    if (isVolatile)
      MMOFlags |= MachineMemOperand::MOVolatile;
    if (isNonTemporal)
      MMOFlags |= MachineMemOperand::MONonTemporal;
    if (isInvariant)
      MMOFlags |= MachineMemOperand::MOInvariant;
    if (isDereferenceable)
      MMOFlags |= MachineMemOperand::MODereferenceable;
    MMOFlags |= TLI.getMMOFlags(I);

    SDValue L = DAG.getLoad(ValueVTs[i], dl, Root, A,
                            MachinePointerInfo(SV, Offsets[i]), Alignment,
                            MMOFlags, AAInfo, Ranges);

    Values[i] = L;
    Chains[ChainI] = L.getValue(1);
  }

  if (!ConstantMemory) {
    SDValue Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other,
                                makeArrayRef(Chains.data(), ChainI));
    if (isVolatile)
      DAG.setRoot(Chain);
    else
      PendingLoads.push_back(Chain);
  }

  setValue(&I, DAG.getNode(ISD::MERGE_VALUES, dl,
                           DAG.getVTList(ValueVTs), Values));
}

void SelectionDAGBuilder::visitStoreToSwiftError(const StoreInst &I) {
  assert(DAG.getTargetLoweringInfo().supportSwiftError() &&
         "call visitStoreToSwiftError when backend supports swifterror");

  SmallVector<EVT, 4> ValueVTs;
  SmallVector<uint64_t, 4> Offsets;
  const Value *SrcV = I.getOperand(0);
  ComputeValueVTs(DAG.getTargetLoweringInfo(), DAG.getDataLayout(),
                  SrcV->getType(), ValueVTs, &Offsets);
  assert(ValueVTs.size() == 1 && Offsets[0] == 0 &&
         "expect a single EVT for swifterror");

  SDValue Src = getValue(SrcV);
  // Create a virtual register, then update the virtual register.
  unsigned VReg; bool CreatedVReg;
  std::tie(VReg, CreatedVReg) = FuncInfo.getOrCreateSwiftErrorVRegDefAt(&I);
  // Chain, DL, Reg, N or Chain, DL, Reg, N, Glue
  // Chain can be getRoot or getControlRoot.
  SDValue CopyNode = DAG.getCopyToReg(getRoot(), getCurSDLoc(), VReg,
                                      SDValue(Src.getNode(), Src.getResNo()));
  DAG.setRoot(CopyNode);
  if (CreatedVReg)
    FuncInfo.setCurrentSwiftErrorVReg(FuncInfo.MBB, I.getOperand(1), VReg);
}

void SelectionDAGBuilder::visitLoadFromSwiftError(const LoadInst &I) {
  assert(DAG.getTargetLoweringInfo().supportSwiftError() &&
         "call visitLoadFromSwiftError when backend supports swifterror");

  assert(!I.isVolatile() &&
         I.getMetadata(LLVMContext::MD_nontemporal) == nullptr &&
         I.getMetadata(LLVMContext::MD_invariant_load) == nullptr &&
         "Support volatile, non temporal, invariant for load_from_swift_error");

  const Value *SV = I.getOperand(0);
  Type *Ty = I.getType();
  AAMDNodes AAInfo;
  I.getAAMetadata(AAInfo);
  assert(
      (!AA ||
       !AA->pointsToConstantMemory(MemoryLocation(
           SV, LocationSize::precise(DAG.getDataLayout().getTypeStoreSize(Ty)),
           AAInfo))) &&
      "load_from_swift_error should not be constant memory");

  SmallVector<EVT, 4> ValueVTs;
  SmallVector<uint64_t, 4> Offsets;
  ComputeValueVTs(DAG.getTargetLoweringInfo(), DAG.getDataLayout(), Ty,
                  ValueVTs, &Offsets);
  assert(ValueVTs.size() == 1 && Offsets[0] == 0 &&
         "expect a single EVT for swifterror");

  // Chain, DL, Reg, VT, Glue or Chain, DL, Reg, VT
  SDValue L = DAG.getCopyFromReg(
      getRoot(), getCurSDLoc(),
      FuncInfo.getOrCreateSwiftErrorVRegUseAt(&I, FuncInfo.MBB, SV).first,
      ValueVTs[0]);

  setValue(&I, L);
}

void SelectionDAGBuilder::visitStore(const StoreInst &I) {
  if (I.isAtomic())
    return visitAtomicStore(I);

  const Value *SrcV = I.getOperand(0);
  const Value *PtrV = I.getOperand(1);

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  if (TLI.supportSwiftError()) {
    // Swifterror values can come from either a function parameter with
    // swifterror attribute or an alloca with swifterror attribute.
    if (const Argument *Arg = dyn_cast<Argument>(PtrV)) {
      if (Arg->hasSwiftErrorAttr())
        return visitStoreToSwiftError(I);
    }

    if (const AllocaInst *Alloca = dyn_cast<AllocaInst>(PtrV)) {
      if (Alloca->isSwiftError())
        return visitStoreToSwiftError(I);
    }
  }

  SmallVector<EVT, 4> ValueVTs;
  SmallVector<uint64_t, 4> Offsets;
  ComputeValueVTs(DAG.getTargetLoweringInfo(), DAG.getDataLayout(),
                  SrcV->getType(), ValueVTs, &Offsets);
  unsigned NumValues = ValueVTs.size();
  if (NumValues == 0)
    return;

  // Get the lowered operands. Note that we do this after
  // checking if NumResults is zero, because with zero results
  // the operands won't have values in the map.
  SDValue Src = getValue(SrcV);
  SDValue Ptr = getValue(PtrV);

  SDValue Root = getRoot();
  SmallVector<SDValue, 4> Chains(std::min(MaxParallelChains, NumValues));
  SDLoc dl = getCurSDLoc();
  EVT PtrVT = Ptr.getValueType();
  unsigned Alignment = I.getAlignment();
  AAMDNodes AAInfo;
  I.getAAMetadata(AAInfo);

  auto MMOFlags = MachineMemOperand::MONone;
  if (I.isVolatile())
    MMOFlags |= MachineMemOperand::MOVolatile;
  if (I.getMetadata(LLVMContext::MD_nontemporal) != nullptr)
    MMOFlags |= MachineMemOperand::MONonTemporal;
  MMOFlags |= TLI.getMMOFlags(I);

  // An aggregate load cannot wrap around the address space, so offsets to its
  // parts don't wrap either.
  SDNodeFlags Flags;
  Flags.setNoUnsignedWrap(true);

  unsigned ChainI = 0;
  for (unsigned i = 0; i != NumValues; ++i, ++ChainI) {
    // See visitLoad comments.
    if (ChainI == MaxParallelChains) {
      SDValue Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other,
                                  makeArrayRef(Chains.data(), ChainI));
      Root = Chain;
      ChainI = 0;
    }
    SDValue Add = DAG.getNode(ISD::ADD, dl, PtrVT, Ptr,
                              DAG.getConstant(Offsets[i], dl, PtrVT), Flags);
    SDValue St = DAG.getStore(
        Root, dl, SDValue(Src.getNode(), Src.getResNo() + i), Add,
        MachinePointerInfo(PtrV, Offsets[i]), Alignment, MMOFlags, AAInfo);
    Chains[ChainI] = St;
  }

  SDValue StoreNode = DAG.getNode(ISD::TokenFactor, dl, MVT::Other,
                                  makeArrayRef(Chains.data(), ChainI));
  DAG.setRoot(StoreNode);
}

void SelectionDAGBuilder::visitMaskedStore(const CallInst &I,
                                           bool IsCompressing) {
  SDLoc sdl = getCurSDLoc();

  auto getMaskedStoreOps = [&](Value* &Ptr, Value* &Mask, Value* &Src0,
                           unsigned& Alignment) {
    // llvm.masked.store.*(Src0, Ptr, alignment, Mask)
    Src0 = I.getArgOperand(0);
    Ptr = I.getArgOperand(1);
    Alignment = cast<ConstantInt>(I.getArgOperand(2))->getZExtValue();
    Mask = I.getArgOperand(3);
  };
  auto getCompressingStoreOps = [&](Value* &Ptr, Value* &Mask, Value* &Src0,
                           unsigned& Alignment) {
    // llvm.masked.compressstore.*(Src0, Ptr, Mask)
    Src0 = I.getArgOperand(0);
    Ptr = I.getArgOperand(1);
    Mask = I.getArgOperand(2);
    Alignment = 0;
  };

  Value  *PtrOperand, *MaskOperand, *Src0Operand;
  unsigned Alignment;
  if (IsCompressing)
    getCompressingStoreOps(PtrOperand, MaskOperand, Src0Operand, Alignment);
  else
    getMaskedStoreOps(PtrOperand, MaskOperand, Src0Operand, Alignment);

  SDValue Ptr = getValue(PtrOperand);
  SDValue Src0 = getValue(Src0Operand);
  SDValue Mask = getValue(MaskOperand);

  EVT VT = Src0.getValueType();
  if (!Alignment)
    Alignment = DAG.getEVTAlignment(VT);

  AAMDNodes AAInfo;
  I.getAAMetadata(AAInfo);

  MachineMemOperand *MMO =
    DAG.getMachineFunction().
    getMachineMemOperand(MachinePointerInfo(PtrOperand),
                          MachineMemOperand::MOStore,  VT.getStoreSize(),
                          Alignment, AAInfo);
  SDValue StoreNode = DAG.getMaskedStore(getRoot(), sdl, Src0, Ptr, Mask, VT,
                                         MMO, false /* Truncating */,
                                         IsCompressing);
  DAG.setRoot(StoreNode);
  setValue(&I, StoreNode);
}

// Get a uniform base for the Gather/Scatter intrinsic.
// The first argument of the Gather/Scatter intrinsic is a vector of pointers.
// We try to represent it as a base pointer + vector of indices.
// Usually, the vector of pointers comes from a 'getelementptr' instruction.
// The first operand of the GEP may be a single pointer or a vector of pointers
// Example:
//   %gep.ptr = getelementptr i32, <8 x i32*> %vptr, <8 x i32> %ind
//  or
//   %gep.ptr = getelementptr i32, i32* %ptr,        <8 x i32> %ind
// %res = call <8 x i32> @llvm.masked.gather.v8i32(<8 x i32*> %gep.ptr, ..
//
// When the first GEP operand is a single pointer - it is the uniform base we
// are looking for. If first operand of the GEP is a splat vector - we
// extract the splat value and use it as a uniform base.
// In all other cases the function returns 'false'.
static bool getUniformBase(const Value* &Ptr, SDValue& Base, SDValue& Index,
                           SDValue &Scale, SelectionDAGBuilder* SDB) {
  SelectionDAG& DAG = SDB->DAG;
  LLVMContext &Context = *DAG.getContext();

  assert(Ptr->getType()->isVectorTy() && "Uexpected pointer type");
  const GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Ptr);
  if (!GEP)
    return false;

  const Value *GEPPtr = GEP->getPointerOperand();
  if (!GEPPtr->getType()->isVectorTy())
    Ptr = GEPPtr;
  else if (!(Ptr = getSplatValue(GEPPtr)))
    return false;

  unsigned FinalIndex = GEP->getNumOperands() - 1;
  Value *IndexVal = GEP->getOperand(FinalIndex);

  // Ensure all the other indices are 0.
  for (unsigned i = 1; i < FinalIndex; ++i) {
    auto *C = dyn_cast<ConstantInt>(GEP->getOperand(i));
    if (!C || !C->isZero())
      return false;
  }

  // The operands of the GEP may be defined in another basic block.
  // In this case we'll not find nodes for the operands.
  if (!SDB->findValue(Ptr) || !SDB->findValue(IndexVal))
    return false;

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  const DataLayout &DL = DAG.getDataLayout();
  Scale = DAG.getTargetConstant(DL.getTypeAllocSize(GEP->getResultElementType()),
                                SDB->getCurSDLoc(), TLI.getPointerTy(DL));
  Base = SDB->getValue(Ptr);
  Index = SDB->getValue(IndexVal);

  if (!Index.getValueType().isVector()) {
    unsigned GEPWidth = GEP->getType()->getVectorNumElements();
    EVT VT = EVT::getVectorVT(Context, Index.getValueType(), GEPWidth);
    Index = DAG.getSplatBuildVector(VT, SDLoc(Index), Index);
  }
  return true;
}

void SelectionDAGBuilder::visitMaskedScatter(const CallInst &I) {
  SDLoc sdl = getCurSDLoc();

  // llvm.masked.scatter.*(Src0, Ptrs, alignemt, Mask)
  const Value *Ptr = I.getArgOperand(1);
  SDValue Src0 = getValue(I.getArgOperand(0));
  SDValue Mask = getValue(I.getArgOperand(3));
  EVT VT = Src0.getValueType();
  unsigned Alignment = (cast<ConstantInt>(I.getArgOperand(2)))->getZExtValue();
  if (!Alignment)
    Alignment = DAG.getEVTAlignment(VT);
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();

  AAMDNodes AAInfo;
  I.getAAMetadata(AAInfo);

  SDValue Base;
  SDValue Index;
  SDValue Scale;
  const Value *BasePtr = Ptr;
  bool UniformBase = getUniformBase(BasePtr, Base, Index, Scale, this);

  const Value *MemOpBasePtr = UniformBase ? BasePtr : nullptr;
  MachineMemOperand *MMO = DAG.getMachineFunction().
    getMachineMemOperand(MachinePointerInfo(MemOpBasePtr),
                         MachineMemOperand::MOStore,  VT.getStoreSize(),
                         Alignment, AAInfo);
  if (!UniformBase) {
    Base = DAG.getConstant(0, sdl, TLI.getPointerTy(DAG.getDataLayout()));
    Index = getValue(Ptr);
    Scale = DAG.getTargetConstant(1, sdl, TLI.getPointerTy(DAG.getDataLayout()));
  }
  SDValue Ops[] = { getRoot(), Src0, Mask, Base, Index, Scale };
  SDValue Scatter = DAG.getMaskedScatter(DAG.getVTList(MVT::Other), VT, sdl,
                                         Ops, MMO);
  DAG.setRoot(Scatter);
  setValue(&I, Scatter);
}

void SelectionDAGBuilder::visitMaskedLoad(const CallInst &I, bool IsExpanding) {
  SDLoc sdl = getCurSDLoc();

  auto getMaskedLoadOps = [&](Value* &Ptr, Value* &Mask, Value* &Src0,
                           unsigned& Alignment) {
    // @llvm.masked.load.*(Ptr, alignment, Mask, Src0)
    Ptr = I.getArgOperand(0);
    Alignment = cast<ConstantInt>(I.getArgOperand(1))->getZExtValue();
    Mask = I.getArgOperand(2);
    Src0 = I.getArgOperand(3);
  };
  auto getExpandingLoadOps = [&](Value* &Ptr, Value* &Mask, Value* &Src0,
                           unsigned& Alignment) {
    // @llvm.masked.expandload.*(Ptr, Mask, Src0)
    Ptr = I.getArgOperand(0);
    Alignment = 0;
    Mask = I.getArgOperand(1);
    Src0 = I.getArgOperand(2);
  };

  Value  *PtrOperand, *MaskOperand, *Src0Operand;
  unsigned Alignment;
  if (IsExpanding)
    getExpandingLoadOps(PtrOperand, MaskOperand, Src0Operand, Alignment);
  else
    getMaskedLoadOps(PtrOperand, MaskOperand, Src0Operand, Alignment);

  SDValue Ptr = getValue(PtrOperand);
  SDValue Src0 = getValue(Src0Operand);
  SDValue Mask = getValue(MaskOperand);

  EVT VT = Src0.getValueType();
  if (!Alignment)
    Alignment = DAG.getEVTAlignment(VT);

  AAMDNodes AAInfo;
  I.getAAMetadata(AAInfo);
  const MDNode *Ranges = I.getMetadata(LLVMContext::MD_range);

  // Do not serialize masked loads of constant memory with anything.
  bool AddToChain =
      !AA || !AA->pointsToConstantMemory(MemoryLocation(
                 PtrOperand,
                 LocationSize::precise(
                     DAG.getDataLayout().getTypeStoreSize(I.getType())),
                 AAInfo));
  SDValue InChain = AddToChain ? DAG.getRoot() : DAG.getEntryNode();

  MachineMemOperand *MMO =
    DAG.getMachineFunction().
    getMachineMemOperand(MachinePointerInfo(PtrOperand),
                          MachineMemOperand::MOLoad,  VT.getStoreSize(),
                          Alignment, AAInfo, Ranges);

  SDValue Load = DAG.getMaskedLoad(VT, sdl, InChain, Ptr, Mask, Src0, VT, MMO,
                                   ISD::NON_EXTLOAD, IsExpanding);
  if (AddToChain)
    PendingLoads.push_back(Load.getValue(1));
  setValue(&I, Load);
}

void SelectionDAGBuilder::visitMaskedGather(const CallInst &I) {
  SDLoc sdl = getCurSDLoc();

  // @llvm.masked.gather.*(Ptrs, alignment, Mask, Src0)
  const Value *Ptr = I.getArgOperand(0);
  SDValue Src0 = getValue(I.getArgOperand(3));
  SDValue Mask = getValue(I.getArgOperand(2));

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  EVT VT = TLI.getValueType(DAG.getDataLayout(), I.getType());
  unsigned Alignment = (cast<ConstantInt>(I.getArgOperand(1)))->getZExtValue();
  if (!Alignment)
    Alignment = DAG.getEVTAlignment(VT);

  AAMDNodes AAInfo;
  I.getAAMetadata(AAInfo);
  const MDNode *Ranges = I.getMetadata(LLVMContext::MD_range);

  SDValue Root = DAG.getRoot();
  SDValue Base;
  SDValue Index;
  SDValue Scale;
  const Value *BasePtr = Ptr;
  bool UniformBase = getUniformBase(BasePtr, Base, Index, Scale, this);
  bool ConstantMemory = false;
  if (UniformBase && AA &&
      AA->pointsToConstantMemory(
          MemoryLocation(BasePtr,
                         LocationSize::precise(
                             DAG.getDataLayout().getTypeStoreSize(I.getType())),
                         AAInfo))) {
    // Do not serialize (non-volatile) loads of constant memory with anything.
    Root = DAG.getEntryNode();
    ConstantMemory = true;
  }

  MachineMemOperand *MMO =
    DAG.getMachineFunction().
    getMachineMemOperand(MachinePointerInfo(UniformBase ? BasePtr : nullptr),
                         MachineMemOperand::MOLoad,  VT.getStoreSize(),
                         Alignment, AAInfo, Ranges);

  if (!UniformBase) {
    Base = DAG.getConstant(0, sdl, TLI.getPointerTy(DAG.getDataLayout()));
    Index = getValue(Ptr);
    Scale = DAG.getTargetConstant(1, sdl, TLI.getPointerTy(DAG.getDataLayout()));
  }
  SDValue Ops[] = { Root, Src0, Mask, Base, Index, Scale };
  SDValue Gather = DAG.getMaskedGather(DAG.getVTList(VT, MVT::Other), VT, sdl,
                                       Ops, MMO);

  SDValue OutChain = Gather.getValue(1);
  if (!ConstantMemory)
    PendingLoads.push_back(OutChain);
  setValue(&I, Gather);
}

void SelectionDAGBuilder::visitAtomicCmpXchg(const AtomicCmpXchgInst &I) {
  SDLoc dl = getCurSDLoc();
  AtomicOrdering SuccessOrder = I.getSuccessOrdering();
  AtomicOrdering FailureOrder = I.getFailureOrdering();
  SyncScope::ID SSID = I.getSyncScopeID();

  SDValue InChain = getRoot();

  MVT MemVT = getValue(I.getCompareOperand()).getSimpleValueType();
  SDVTList VTs = DAG.getVTList(MemVT, MVT::i1, MVT::Other);
  SDValue L = DAG.getAtomicCmpSwap(
      ISD::ATOMIC_CMP_SWAP_WITH_SUCCESS, dl, MemVT, VTs, InChain,
      getValue(I.getPointerOperand()), getValue(I.getCompareOperand()),
      getValue(I.getNewValOperand()), MachinePointerInfo(I.getPointerOperand()),
      /*Alignment=*/ 0, SuccessOrder, FailureOrder, SSID);

  SDValue OutChain = L.getValue(2);

  setValue(&I, L);
  DAG.setRoot(OutChain);
}

void SelectionDAGBuilder::visitAtomicRMW(const AtomicRMWInst &I) {
  SDLoc dl = getCurSDLoc();
  ISD::NodeType NT;
  switch (I.getOperation()) {
  default: llvm_unreachable("Unknown atomicrmw operation");
  case AtomicRMWInst::Xchg: NT = ISD::ATOMIC_SWAP; break;
  case AtomicRMWInst::Add:  NT = ISD::ATOMIC_LOAD_ADD; break;
  case AtomicRMWInst::Sub:  NT = ISD::ATOMIC_LOAD_SUB; break;
  case AtomicRMWInst::And:  NT = ISD::ATOMIC_LOAD_AND; break;
  case AtomicRMWInst::Nand: NT = ISD::ATOMIC_LOAD_NAND; break;
  case AtomicRMWInst::Or:   NT = ISD::ATOMIC_LOAD_OR; break;
  case AtomicRMWInst::Xor:  NT = ISD::ATOMIC_LOAD_XOR; break;
  case AtomicRMWInst::Max:  NT = ISD::ATOMIC_LOAD_MAX; break;
  case AtomicRMWInst::Min:  NT = ISD::ATOMIC_LOAD_MIN; break;
  case AtomicRMWInst::UMax: NT = ISD::ATOMIC_LOAD_UMAX; break;
  case AtomicRMWInst::UMin: NT = ISD::ATOMIC_LOAD_UMIN; break;
  }
  AtomicOrdering Order = I.getOrdering();
  SyncScope::ID SSID = I.getSyncScopeID();

  SDValue InChain = getRoot();

  SDValue L =
    DAG.getAtomic(NT, dl,
                  getValue(I.getValOperand()).getSimpleValueType(),
                  InChain,
                  getValue(I.getPointerOperand()),
                  getValue(I.getValOperand()),
                  I.getPointerOperand(),
                  /* Alignment=*/ 0, Order, SSID);

  SDValue OutChain = L.getValue(1);

  setValue(&I, L);
  DAG.setRoot(OutChain);
}

void SelectionDAGBuilder::visitFence(const FenceInst &I) {
  SDLoc dl = getCurSDLoc();
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  SDValue Ops[3];
  Ops[0] = getRoot();
  Ops[1] = DAG.getConstant((unsigned)I.getOrdering(), dl,
                           TLI.getFenceOperandTy(DAG.getDataLayout()));
  Ops[2] = DAG.getConstant(I.getSyncScopeID(), dl,
                           TLI.getFenceOperandTy(DAG.getDataLayout()));
  DAG.setRoot(DAG.getNode(ISD::ATOMIC_FENCE, dl, MVT::Other, Ops));
}

void SelectionDAGBuilder::visitAtomicLoad(const LoadInst &I) {
  SDLoc dl = getCurSDLoc();
  AtomicOrdering Order = I.getOrdering();
  SyncScope::ID SSID = I.getSyncScopeID();

  SDValue InChain = getRoot();

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  EVT VT = TLI.getValueType(DAG.getDataLayout(), I.getType());

  if (!TLI.supportsUnalignedAtomics() &&
      I.getAlignment() < VT.getStoreSize())
    report_fatal_error("Cannot generate unaligned atomic load");

  MachineMemOperand *MMO =
      DAG.getMachineFunction().
      getMachineMemOperand(MachinePointerInfo(I.getPointerOperand()),
                           MachineMemOperand::MOVolatile |
                           MachineMemOperand::MOLoad,
                           VT.getStoreSize(),
                           I.getAlignment() ? I.getAlignment() :
                                              DAG.getEVTAlignment(VT),
                           AAMDNodes(), nullptr, SSID, Order);

  InChain = TLI.prepareVolatileOrAtomicLoad(InChain, dl, DAG);
  SDValue L =
      DAG.getAtomic(ISD::ATOMIC_LOAD, dl, VT, VT, InChain,
                    getValue(I.getPointerOperand()), MMO);

  SDValue OutChain = L.getValue(1);

  setValue(&I, L);
  DAG.setRoot(OutChain);
}

void SelectionDAGBuilder::visitAtomicStore(const StoreInst &I) {
  SDLoc dl = getCurSDLoc();

  AtomicOrdering Order = I.getOrdering();
  SyncScope::ID SSID = I.getSyncScopeID();

  SDValue InChain = getRoot();

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  EVT VT =
      TLI.getValueType(DAG.getDataLayout(), I.getValueOperand()->getType());

  if (I.getAlignment() < VT.getStoreSize())
    report_fatal_error("Cannot generate unaligned atomic store");

  SDValue OutChain =
    DAG.getAtomic(ISD::ATOMIC_STORE, dl, VT,
                  InChain,
                  getValue(I.getPointerOperand()),
                  getValue(I.getValueOperand()),
                  I.getPointerOperand(), I.getAlignment(),
                  Order, SSID);

  DAG.setRoot(OutChain);
}

/// visitTargetIntrinsic - Lower a call of a target intrinsic to an INTRINSIC
/// node.
void SelectionDAGBuilder::visitTargetIntrinsic(const CallInst &I,
                                               unsigned Intrinsic) {
  // Ignore the callsite's attributes. A specific call site may be marked with
  // readnone, but the lowering code will expect the chain based on the
  // definition.
  const Function *F = I.getCalledFunction();
  bool HasChain = !F->doesNotAccessMemory();
  bool OnlyLoad = HasChain && F->onlyReadsMemory();

  // Build the operand list.
  SmallVector<SDValue, 8> Ops;
  if (HasChain) {  // If this intrinsic has side-effects, chainify it.
    if (OnlyLoad) {
      // We don't need to serialize loads against other loads.
      Ops.push_back(DAG.getRoot());
    } else {
      Ops.push_back(getRoot());
    }
  }

  // Info is set by getTgtMemInstrinsic
  TargetLowering::IntrinsicInfo Info;
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  bool IsTgtIntrinsic = TLI.getTgtMemIntrinsic(Info, I,
                                               DAG.getMachineFunction(),
                                               Intrinsic);

  // Add the intrinsic ID as an integer operand if it's not a target intrinsic.
  if (!IsTgtIntrinsic || Info.opc == ISD::INTRINSIC_VOID ||
      Info.opc == ISD::INTRINSIC_W_CHAIN)
    Ops.push_back(DAG.getTargetConstant(Intrinsic, getCurSDLoc(),
                                        TLI.getPointerTy(DAG.getDataLayout())));

  // Add all operands of the call to the operand list.
  for (unsigned i = 0, e = I.getNumArgOperands(); i != e; ++i) {
    SDValue Op = getValue(I.getArgOperand(i));
    Ops.push_back(Op);
  }

  SmallVector<EVT, 4> ValueVTs;
  ComputeValueVTs(TLI, DAG.getDataLayout(), I.getType(), ValueVTs);

  if (HasChain)
    ValueVTs.push_back(MVT::Other);

  SDVTList VTs = DAG.getVTList(ValueVTs);

  // Create the node.
  SDValue Result;
  if (IsTgtIntrinsic) {
    // This is target intrinsic that touches memory
    Result = DAG.getMemIntrinsicNode(Info.opc, getCurSDLoc(), VTs,
      Ops, Info.memVT,
      MachinePointerInfo(Info.ptrVal, Info.offset), Info.align,
      Info.flags, Info.size);
  } else if (!HasChain) {
    Result = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, getCurSDLoc(), VTs, Ops);
  } else if (!I.getType()->isVoidTy()) {
    Result = DAG.getNode(ISD::INTRINSIC_W_CHAIN, getCurSDLoc(), VTs, Ops);
  } else {
    Result = DAG.getNode(ISD::INTRINSIC_VOID, getCurSDLoc(), VTs, Ops);
  }

  if (HasChain) {
    SDValue Chain = Result.getValue(Result.getNode()->getNumValues()-1);
    if (OnlyLoad)
      PendingLoads.push_back(Chain);
    else
      DAG.setRoot(Chain);
  }

  if (!I.getType()->isVoidTy()) {
    if (VectorType *PTy = dyn_cast<VectorType>(I.getType())) {
      EVT VT = TLI.getValueType(DAG.getDataLayout(), PTy);
      Result = DAG.getNode(ISD::BITCAST, getCurSDLoc(), VT, Result);
    } else
      Result = lowerRangeToAssertZExt(DAG, I, Result);

    setValue(&I, Result);
  }
}

/// GetSignificand - Get the significand and build it into a floating-point
/// number with exponent of 1:
///
///   Op = (Op & 0x007fffff) | 0x3f800000;
///
/// where Op is the hexadecimal representation of floating point value.
static SDValue GetSignificand(SelectionDAG &DAG, SDValue Op, const SDLoc &dl) {
  SDValue t1 = DAG.getNode(ISD::AND, dl, MVT::i32, Op,
                           DAG.getConstant(0x007fffff, dl, MVT::i32));
  SDValue t2 = DAG.getNode(ISD::OR, dl, MVT::i32, t1,
                           DAG.getConstant(0x3f800000, dl, MVT::i32));
  return DAG.getNode(ISD::BITCAST, dl, MVT::f32, t2);
}

/// GetExponent - Get the exponent:
///
///   (float)(int)(((Op & 0x7f800000) >> 23) - 127);
///
/// where Op is the hexadecimal representation of floating point value.
static SDValue GetExponent(SelectionDAG &DAG, SDValue Op,
                           const TargetLowering &TLI, const SDLoc &dl) {
  SDValue t0 = DAG.getNode(ISD::AND, dl, MVT::i32, Op,
                           DAG.getConstant(0x7f800000, dl, MVT::i32));
  SDValue t1 = DAG.getNode(
      ISD::SRL, dl, MVT::i32, t0,
      DAG.getConstant(23, dl, TLI.getPointerTy(DAG.getDataLayout())));
  SDValue t2 = DAG.getNode(ISD::SUB, dl, MVT::i32, t1,
                           DAG.getConstant(127, dl, MVT::i32));
  return DAG.getNode(ISD::SINT_TO_FP, dl, MVT::f32, t2);
}

/// getF32Constant - Get 32-bit floating point constant.
static SDValue getF32Constant(SelectionDAG &DAG, unsigned Flt,
                              const SDLoc &dl) {
  return DAG.getConstantFP(APFloat(APFloat::IEEEsingle(), APInt(32, Flt)), dl,
                           MVT::f32);
}

static SDValue getLimitedPrecisionExp2(SDValue t0, const SDLoc &dl,
                                       SelectionDAG &DAG) {
  // TODO: What fast-math-flags should be set on the floating-point nodes?

  //   IntegerPartOfX = ((int32_t)(t0);
  SDValue IntegerPartOfX = DAG.getNode(ISD::FP_TO_SINT, dl, MVT::i32, t0);

  //   FractionalPartOfX = t0 - (float)IntegerPartOfX;
  SDValue t1 = DAG.getNode(ISD::SINT_TO_FP, dl, MVT::f32, IntegerPartOfX);
  SDValue X = DAG.getNode(ISD::FSUB, dl, MVT::f32, t0, t1);

  //   IntegerPartOfX <<= 23;
  IntegerPartOfX = DAG.getNode(
      ISD::SHL, dl, MVT::i32, IntegerPartOfX,
      DAG.getConstant(23, dl, DAG.getTargetLoweringInfo().getPointerTy(
                                  DAG.getDataLayout())));

  SDValue TwoToFractionalPartOfX;
  if (LimitFloatPrecision <= 6) {
    // For floating-point precision of 6:
    //
    //   TwoToFractionalPartOfX =
    //     0.997535578f +
    //       (0.735607626f + 0.252464424f * x) * x;
    //
    // error 0.0144103317, which is 6 bits
    SDValue t2 = DAG.getNode(ISD::FMUL, dl, MVT::f32, X,
                             getF32Constant(DAG, 0x3e814304, dl));
    SDValue t3 = DAG.getNode(ISD::FADD, dl, MVT::f32, t2,
                             getF32Constant(DAG, 0x3f3c50c8, dl));
    SDValue t4 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t3, X);
    TwoToFractionalPartOfX = DAG.getNode(ISD::FADD, dl, MVT::f32, t4,
                                         getF32Constant(DAG, 0x3f7f5e7e, dl));
  } else if (LimitFloatPrecision <= 12) {
    // For floating-point precision of 12:
    //
    //   TwoToFractionalPartOfX =
    //     0.999892986f +
    //       (0.696457318f +
    //         (0.224338339f + 0.792043434e-1f * x) * x) * x;
    //
    // error 0.000107046256, which is 13 to 14 bits
    SDValue t2 = DAG.getNode(ISD::FMUL, dl, MVT::f32, X,
                             getF32Constant(DAG, 0x3da235e3, dl));
    SDValue t3 = DAG.getNode(ISD::FADD, dl, MVT::f32, t2,
                             getF32Constant(DAG, 0x3e65b8f3, dl));
    SDValue t4 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t3, X);
    SDValue t5 = DAG.getNode(ISD::FADD, dl, MVT::f32, t4,
                             getF32Constant(DAG, 0x3f324b07, dl));
    SDValue t6 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t5, X);
    TwoToFractionalPartOfX = DAG.getNode(ISD::FADD, dl, MVT::f32, t6,
                                         getF32Constant(DAG, 0x3f7ff8fd, dl));
  } else { // LimitFloatPrecision <= 18
    // For floating-point precision of 18:
    //
    //   TwoToFractionalPartOfX =
    //     0.999999982f +
    //       (0.693148872f +
    //         (0.240227044f +
    //           (0.554906021e-1f +
    //             (0.961591928e-2f +
    //               (0.136028312e-2f + 0.157059148e-3f *x)*x)*x)*x)*x)*x;
    // error 2.47208000*10^(-7), which is better than 18 bits
    SDValue t2 = DAG.getNode(ISD::FMUL, dl, MVT::f32, X,
                             getF32Constant(DAG, 0x3924b03e, dl));
    SDValue t3 = DAG.getNode(ISD::FADD, dl, MVT::f32, t2,
                             getF32Constant(DAG, 0x3ab24b87, dl));
    SDValue t4 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t3, X);
    SDValue t5 = DAG.getNode(ISD::FADD, dl, MVT::f32, t4,
                             getF32Constant(DAG, 0x3c1d8c17, dl));
    SDValue t6 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t5, X);
    SDValue t7 = DAG.getNode(ISD::FADD, dl, MVT::f32, t6,
                             getF32Constant(DAG, 0x3d634a1d, dl));
    SDValue t8 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t7, X);
    SDValue t9 = DAG.getNode(ISD::FADD, dl, MVT::f32, t8,
                             getF32Constant(DAG, 0x3e75fe14, dl));
    SDValue t10 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t9, X);
    SDValue t11 = DAG.getNode(ISD::FADD, dl, MVT::f32, t10,
                              getF32Constant(DAG, 0x3f317234, dl));
    SDValue t12 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t11, X);
    TwoToFractionalPartOfX = DAG.getNode(ISD::FADD, dl, MVT::f32, t12,
                                         getF32Constant(DAG, 0x3f800000, dl));
  }

  // Add the exponent into the result in integer domain.
  SDValue t13 = DAG.getNode(ISD::BITCAST, dl, MVT::i32, TwoToFractionalPartOfX);
  return DAG.getNode(ISD::BITCAST, dl, MVT::f32,
                     DAG.getNode(ISD::ADD, dl, MVT::i32, t13, IntegerPartOfX));
}

/// expandExp - Lower an exp intrinsic. Handles the special sequences for
/// limited-precision mode.
static SDValue expandExp(const SDLoc &dl, SDValue Op, SelectionDAG &DAG,
                         const TargetLowering &TLI) {
  if (Op.getValueType() == MVT::f32 &&
      LimitFloatPrecision > 0 && LimitFloatPrecision <= 18) {

    // Put the exponent in the right bit position for later addition to the
    // final result:
    //
    //   #define LOG2OFe 1.4426950f
    //   t0 = Op * LOG2OFe

    // TODO: What fast-math-flags should be set here?
    SDValue t0 = DAG.getNode(ISD::FMUL, dl, MVT::f32, Op,
                             getF32Constant(DAG, 0x3fb8aa3b, dl));
    return getLimitedPrecisionExp2(t0, dl, DAG);
  }

  // No special expansion.
  return DAG.getNode(ISD::FEXP, dl, Op.getValueType(), Op);
}

/// expandLog - Lower a log intrinsic. Handles the special sequences for
/// limited-precision mode.
static SDValue expandLog(const SDLoc &dl, SDValue Op, SelectionDAG &DAG,
                         const TargetLowering &TLI) {
  // TODO: What fast-math-flags should be set on the floating-point nodes?

  if (Op.getValueType() == MVT::f32 &&
      LimitFloatPrecision > 0 && LimitFloatPrecision <= 18) {
    SDValue Op1 = DAG.getNode(ISD::BITCAST, dl, MVT::i32, Op);

    // Scale the exponent by log(2) [0.69314718f].
    SDValue Exp = GetExponent(DAG, Op1, TLI, dl);
    SDValue LogOfExponent = DAG.getNode(ISD::FMUL, dl, MVT::f32, Exp,
                                        getF32Constant(DAG, 0x3f317218, dl));

    // Get the significand and build it into a floating-point number with
    // exponent of 1.
    SDValue X = GetSignificand(DAG, Op1, dl);

    SDValue LogOfMantissa;
    if (LimitFloatPrecision <= 6) {
      // For floating-point precision of 6:
      //
      //   LogofMantissa =
      //     -1.1609546f +
      //       (1.4034025f - 0.23903021f * x) * x;
      //
      // error 0.0034276066, which is better than 8 bits
      SDValue t0 = DAG.getNode(ISD::FMUL, dl, MVT::f32, X,
                               getF32Constant(DAG, 0xbe74c456, dl));
      SDValue t1 = DAG.getNode(ISD::FADD, dl, MVT::f32, t0,
                               getF32Constant(DAG, 0x3fb3a2b1, dl));
      SDValue t2 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t1, X);
      LogOfMantissa = DAG.getNode(ISD::FSUB, dl, MVT::f32, t2,
                                  getF32Constant(DAG, 0x3f949a29, dl));
    } else if (LimitFloatPrecision <= 12) {
      // For floating-point precision of 12:
      //
      //   LogOfMantissa =
      //     -1.7417939f +
      //       (2.8212026f +
      //         (-1.4699568f +
      //           (0.44717955f - 0.56570851e-1f * x) * x) * x) * x;
      //
      // error 0.000061011436, which is 14 bits
      SDValue t0 = DAG.getNode(ISD::FMUL, dl, MVT::f32, X,
                               getF32Constant(DAG, 0xbd67b6d6, dl));
      SDValue t1 = DAG.getNode(ISD::FADD, dl, MVT::f32, t0,
                               getF32Constant(DAG, 0x3ee4f4b8, dl));
      SDValue t2 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t1, X);
      SDValue t3 = DAG.getNode(ISD::FSUB, dl, MVT::f32, t2,
                               getF32Constant(DAG, 0x3fbc278b, dl));
      SDValue t4 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t3, X);
      SDValue t5 = DAG.getNode(ISD::FADD, dl, MVT::f32, t4,
                               getF32Constant(DAG, 0x40348e95, dl));
      SDValue t6 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t5, X);
      LogOfMantissa = DAG.getNode(ISD::FSUB, dl, MVT::f32, t6,
                                  getF32Constant(DAG, 0x3fdef31a, dl));
    } else { // LimitFloatPrecision <= 18
      // For floating-point precision of 18:
      //
      //   LogOfMantissa =
      //     -2.1072184f +
      //       (4.2372794f +
      //         (-3.7029485f +
      //           (2.2781945f +
      //             (-0.87823314f +
      //               (0.19073739f - 0.17809712e-1f * x) * x) * x) * x) * x)*x;
      //
      // error 0.0000023660568, which is better than 18 bits
      SDValue t0 = DAG.getNode(ISD::FMUL, dl, MVT::f32, X,
                               getF32Constant(DAG, 0xbc91e5ac, dl));
      SDValue t1 = DAG.getNode(ISD::FADD, dl, MVT::f32, t0,
                               getF32Constant(DAG, 0x3e4350aa, dl));
      SDValue t2 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t1, X);
      SDValue t3 = DAG.getNode(ISD::FSUB, dl, MVT::f32, t2,
                               getF32Constant(DAG, 0x3f60d3e3, dl));
      SDValue t4 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t3, X);
      SDValue t5 = DAG.getNode(ISD::FADD, dl, MVT::f32, t4,
                               getF32Constant(DAG, 0x4011cdf0, dl));
      SDValue t6 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t5, X);
      SDValue t7 = DAG.getNode(ISD::FSUB, dl, MVT::f32, t6,
                               getF32Constant(DAG, 0x406cfd1c, dl));
      SDValue t8 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t7, X);
      SDValue t9 = DAG.getNode(ISD::FADD, dl, MVT::f32, t8,
                               getF32Constant(DAG, 0x408797cb, dl));
      SDValue t10 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t9, X);
      LogOfMantissa = DAG.getNode(ISD::FSUB, dl, MVT::f32, t10,
                                  getF32Constant(DAG, 0x4006dcab, dl));
    }

    return DAG.getNode(ISD::FADD, dl, MVT::f32, LogOfExponent, LogOfMantissa);
  }

  // No special expansion.
  return DAG.getNode(ISD::FLOG, dl, Op.getValueType(), Op);
}

/// expandLog2 - Lower a log2 intrinsic. Handles the special sequences for
/// limited-precision mode.
static SDValue expandLog2(const SDLoc &dl, SDValue Op, SelectionDAG &DAG,
                          const TargetLowering &TLI) {
  // TODO: What fast-math-flags should be set on the floating-point nodes?

  if (Op.getValueType() == MVT::f32 &&
      LimitFloatPrecision > 0 && LimitFloatPrecision <= 18) {
    SDValue Op1 = DAG.getNode(ISD::BITCAST, dl, MVT::i32, Op);

    // Get the exponent.
    SDValue LogOfExponent = GetExponent(DAG, Op1, TLI, dl);

    // Get the significand and build it into a floating-point number with
    // exponent of 1.
    SDValue X = GetSignificand(DAG, Op1, dl);

    // Different possible minimax approximations of significand in
    // floating-point for various degrees of accuracy over [1,2].
    SDValue Log2ofMantissa;
    if (LimitFloatPrecision <= 6) {
      // For floating-point precision of 6:
      //
      //   Log2ofMantissa = -1.6749035f + (2.0246817f - .34484768f * x) * x;
      //
      // error 0.0049451742, which is more than 7 bits
      SDValue t0 = DAG.getNode(ISD::FMUL, dl, MVT::f32, X,
                               getF32Constant(DAG, 0xbeb08fe0, dl));
      SDValue t1 = DAG.getNode(ISD::FADD, dl, MVT::f32, t0,
                               getF32Constant(DAG, 0x40019463, dl));
      SDValue t2 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t1, X);
      Log2ofMantissa = DAG.getNode(ISD::FSUB, dl, MVT::f32, t2,
                                   getF32Constant(DAG, 0x3fd6633d, dl));
    } else if (LimitFloatPrecision <= 12) {
      // For floating-point precision of 12:
      //
      //   Log2ofMantissa =
      //     -2.51285454f +
      //       (4.07009056f +
      //         (-2.12067489f +
      //           (.645142248f - 0.816157886e-1f * x) * x) * x) * x;
      //
      // error 0.0000876136000, which is better than 13 bits
      SDValue t0 = DAG.getNode(ISD::FMUL, dl, MVT::f32, X,
                               getF32Constant(DAG, 0xbda7262e, dl));
      SDValue t1 = DAG.getNode(ISD::FADD, dl, MVT::f32, t0,
                               getF32Constant(DAG, 0x3f25280b, dl));
      SDValue t2 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t1, X);
      SDValue t3 = DAG.getNode(ISD::FSUB, dl, MVT::f32, t2,
                               getF32Constant(DAG, 0x4007b923, dl));
      SDValue t4 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t3, X);
      SDValue t5 = DAG.getNode(ISD::FADD, dl, MVT::f32, t4,
                               getF32Constant(DAG, 0x40823e2f, dl));
      SDValue t6 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t5, X);
      Log2ofMantissa = DAG.getNode(ISD::FSUB, dl, MVT::f32, t6,
                                   getF32Constant(DAG, 0x4020d29c, dl));
    } else { // LimitFloatPrecision <= 18
      // For floating-point precision of 18:
      //
      //   Log2ofMantissa =
      //     -3.0400495f +
      //       (6.1129976f +
      //         (-5.3420409f +
      //           (3.2865683f +
      //             (-1.2669343f +
      //               (0.27515199f -
      //                 0.25691327e-1f * x) * x) * x) * x) * x) * x;
      //
      // error 0.0000018516, which is better than 18 bits
      SDValue t0 = DAG.getNode(ISD::FMUL, dl, MVT::f32, X,
                               getF32Constant(DAG, 0xbcd2769e, dl));
      SDValue t1 = DAG.getNode(ISD::FADD, dl, MVT::f32, t0,
                               getF32Constant(DAG, 0x3e8ce0b9, dl));
      SDValue t2 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t1, X);
      SDValue t3 = DAG.getNode(ISD::FSUB, dl, MVT::f32, t2,
                               getF32Constant(DAG, 0x3fa22ae7, dl));
      SDValue t4 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t3, X);
      SDValue t5 = DAG.getNode(ISD::FADD, dl, MVT::f32, t4,
                               getF32Constant(DAG, 0x40525723, dl));
      SDValue t6 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t5, X);
      SDValue t7 = DAG.getNode(ISD::FSUB, dl, MVT::f32, t6,
                               getF32Constant(DAG, 0x40aaf200, dl));
      SDValue t8 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t7, X);
      SDValue t9 = DAG.getNode(ISD::FADD, dl, MVT::f32, t8,
                               getF32Constant(DAG, 0x40c39dad, dl));
      SDValue t10 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t9, X);
      Log2ofMantissa = DAG.getNode(ISD::FSUB, dl, MVT::f32, t10,
                                   getF32Constant(DAG, 0x4042902c, dl));
    }

    return DAG.getNode(ISD::FADD, dl, MVT::f32, LogOfExponent, Log2ofMantissa);
  }

  // No special expansion.
  return DAG.getNode(ISD::FLOG2, dl, Op.getValueType(), Op);
}

/// expandLog10 - Lower a log10 intrinsic. Handles the special sequences for
/// limited-precision mode.
static SDValue expandLog10(const SDLoc &dl, SDValue Op, SelectionDAG &DAG,
                           const TargetLowering &TLI) {
  // TODO: What fast-math-flags should be set on the floating-point nodes?

  if (Op.getValueType() == MVT::f32 &&
      LimitFloatPrecision > 0 && LimitFloatPrecision <= 18) {
    SDValue Op1 = DAG.getNode(ISD::BITCAST, dl, MVT::i32, Op);

    // Scale the exponent by log10(2) [0.30102999f].
    SDValue Exp = GetExponent(DAG, Op1, TLI, dl);
    SDValue LogOfExponent = DAG.getNode(ISD::FMUL, dl, MVT::f32, Exp,
                                        getF32Constant(DAG, 0x3e9a209a, dl));

    // Get the significand and build it into a floating-point number with
    // exponent of 1.
    SDValue X = GetSignificand(DAG, Op1, dl);

    SDValue Log10ofMantissa;
    if (LimitFloatPrecision <= 6) {
      // For floating-point precision of 6:
      //
      //   Log10ofMantissa =
      //     -0.50419619f +
      //       (0.60948995f - 0.10380950f * x) * x;
      //
      // error 0.0014886165, which is 6 bits
      SDValue t0 = DAG.getNode(ISD::FMUL, dl, MVT::f32, X,
                               getF32Constant(DAG, 0xbdd49a13, dl));
      SDValue t1 = DAG.getNode(ISD::FADD, dl, MVT::f32, t0,
                               getF32Constant(DAG, 0x3f1c0789, dl));
      SDValue t2 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t1, X);
      Log10ofMantissa = DAG.getNode(ISD::FSUB, dl, MVT::f32, t2,
                                    getF32Constant(DAG, 0x3f011300, dl));
    } else if (LimitFloatPrecision <= 12) {
      // For floating-point precision of 12:
      //
      //   Log10ofMantissa =
      //     -0.64831180f +
      //       (0.91751397f +
      //         (-0.31664806f + 0.47637168e-1f * x) * x) * x;
      //
      // error 0.00019228036, which is better than 12 bits
      SDValue t0 = DAG.getNode(ISD::FMUL, dl, MVT::f32, X,
                               getF32Constant(DAG, 0x3d431f31, dl));
      SDValue t1 = DAG.getNode(ISD::FSUB, dl, MVT::f32, t0,
                               getF32Constant(DAG, 0x3ea21fb2, dl));
      SDValue t2 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t1, X);
      SDValue t3 = DAG.getNode(ISD::FADD, dl, MVT::f32, t2,
                               getF32Constant(DAG, 0x3f6ae232, dl));
      SDValue t4 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t3, X);
      Log10ofMantissa = DAG.getNode(ISD::FSUB, dl, MVT::f32, t4,
                                    getF32Constant(DAG, 0x3f25f7c3, dl));
    } else { // LimitFloatPrecision <= 18
      // For floating-point precision of 18:
      //
      //   Log10ofMantissa =
      //     -0.84299375f +
      //       (1.5327582f +
      //         (-1.0688956f +
      //           (0.49102474f +
      //             (-0.12539807f + 0.13508273e-1f * x) * x) * x) * x) * x;
      //
      // error 0.0000037995730, which is better than 18 bits
      SDValue t0 = DAG.getNode(ISD::FMUL, dl, MVT::f32, X,
                               getF32Constant(DAG, 0x3c5d51ce, dl));
      SDValue t1 = DAG.getNode(ISD::FSUB, dl, MVT::f32, t0,
                               getF32Constant(DAG, 0x3e00685a, dl));
      SDValue t2 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t1, X);
      SDValue t3 = DAG.getNode(ISD::FADD, dl, MVT::f32, t2,
                               getF32Constant(DAG, 0x3efb6798, dl));
      SDValue t4 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t3, X);
      SDValue t5 = DAG.getNode(ISD::FSUB, dl, MVT::f32, t4,
                               getF32Constant(DAG, 0x3f88d192, dl));
      SDValue t6 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t5, X);
      SDValue t7 = DAG.getNode(ISD::FADD, dl, MVT::f32, t6,
                               getF32Constant(DAG, 0x3fc4316c, dl));
      SDValue t8 = DAG.getNode(ISD::FMUL, dl, MVT::f32, t7, X);
      Log10ofMantissa = DAG.getNode(ISD::FSUB, dl, MVT::f32, t8,
                                    getF32Constant(DAG, 0x3f57ce70, dl));
    }

    return DAG.getNode(ISD::FADD, dl, MVT::f32, LogOfExponent, Log10ofMantissa);
  }

  // No special expansion.
  return DAG.getNode(ISD::FLOG10, dl, Op.getValueType(), Op);
}

/// expandExp2 - Lower an exp2 intrinsic. Handles the special sequences for
/// limited-precision mode.
static SDValue expandExp2(const SDLoc &dl, SDValue Op, SelectionDAG &DAG,
                          const TargetLowering &TLI) {
  if (Op.getValueType() == MVT::f32 &&
      LimitFloatPrecision > 0 && LimitFloatPrecision <= 18)
    return getLimitedPrecisionExp2(Op, dl, DAG);

  // No special expansion.
  return DAG.getNode(ISD::FEXP2, dl, Op.getValueType(), Op);
}

/// visitPow - Lower a pow intrinsic. Handles the special sequences for
/// limited-precision mode with x == 10.0f.
static SDValue expandPow(const SDLoc &dl, SDValue LHS, SDValue RHS,
                         SelectionDAG &DAG, const TargetLowering &TLI) {
  bool IsExp10 = false;
  if (LHS.getValueType() == MVT::f32 && RHS.getValueType() == MVT::f32 &&
      LimitFloatPrecision > 0 && LimitFloatPrecision <= 18) {
    if (ConstantFPSDNode *LHSC = dyn_cast<ConstantFPSDNode>(LHS)) {
      APFloat Ten(10.0f);
      IsExp10 = LHSC->isExactlyValue(Ten);
    }
  }

  // TODO: What fast-math-flags should be set on the FMUL node?
  if (IsExp10) {
    // Put the exponent in the right bit position for later addition to the
    // final result:
    //
    //   #define LOG2OF10 3.3219281f
    //   t0 = Op * LOG2OF10;
    SDValue t0 = DAG.getNode(ISD::FMUL, dl, MVT::f32, RHS,
                             getF32Constant(DAG, 0x40549a78, dl));
    return getLimitedPrecisionExp2(t0, dl, DAG);
  }

  // No special expansion.
  return DAG.getNode(ISD::FPOW, dl, LHS.getValueType(), LHS, RHS);
}

/// ExpandPowI - Expand a llvm.powi intrinsic.
static SDValue ExpandPowI(const SDLoc &DL, SDValue LHS, SDValue RHS,
                          SelectionDAG &DAG) {
  // If RHS is a constant, we can expand this out to a multiplication tree,
  // otherwise we end up lowering to a call to __powidf2 (for example).  When
  // optimizing for size, we only want to do this if the expansion would produce
  // a small number of multiplies, otherwise we do the full expansion.
  if (ConstantSDNode *RHSC = dyn_cast<ConstantSDNode>(RHS)) {
    // Get the exponent as a positive value.
    unsigned Val = RHSC->getSExtValue();
    if ((int)Val < 0) Val = -Val;

    // powi(x, 0) -> 1.0
    if (Val == 0)
      return DAG.getConstantFP(1.0, DL, LHS.getValueType());

    const Function &F = DAG.getMachineFunction().getFunction();
    if (!F.optForSize() ||
        // If optimizing for size, don't insert too many multiplies.
        // This inserts up to 5 multiplies.
        countPopulation(Val) + Log2_32(Val) < 7) {
      // We use the simple binary decomposition method to generate the multiply
      // sequence.  There are more optimal ways to do this (for example,
      // powi(x,15) generates one more multiply than it should), but this has
      // the benefit of being both really simple and much better than a libcall.
      SDValue Res;  // Logically starts equal to 1.0
      SDValue CurSquare = LHS;
      // TODO: Intrinsics should have fast-math-flags that propagate to these
      // nodes.
      while (Val) {
        if (Val & 1) {
          if (Res.getNode())
            Res = DAG.getNode(ISD::FMUL, DL,Res.getValueType(), Res, CurSquare);
          else
            Res = CurSquare;  // 1.0*CurSquare.
        }

        CurSquare = DAG.getNode(ISD::FMUL, DL, CurSquare.getValueType(),
                                CurSquare, CurSquare);
        Val >>= 1;
      }

      // If the original was negative, invert the result, producing 1/(x*x*x).
      if (RHSC->getSExtValue() < 0)
        Res = DAG.getNode(ISD::FDIV, DL, LHS.getValueType(),
                          DAG.getConstantFP(1.0, DL, LHS.getValueType()), Res);
      return Res;
    }
  }

  // Otherwise, expand to a libcall.
  return DAG.getNode(ISD::FPOWI, DL, LHS.getValueType(), LHS, RHS);
}

// getUnderlyingArgReg - Find underlying register used for a truncated or
// bitcasted argument.
static unsigned getUnderlyingArgReg(const SDValue &N) {
  switch (N.getOpcode()) {
  case ISD::CopyFromReg:
    return cast<RegisterSDNode>(N.getOperand(1))->getReg();
  case ISD::BITCAST:
  case ISD::AssertZext:
  case ISD::AssertSext:
  case ISD::TRUNCATE:
    return getUnderlyingArgReg(N.getOperand(0));
  default:
    return 0;
  }
}

/// If the DbgValueInst is a dbg_value of a function argument, create the
/// corresponding DBG_VALUE machine instruction for it now.  At the end of
/// instruction selection, they will be inserted to the entry BB.
bool SelectionDAGBuilder::EmitFuncArgumentDbgValue(
    const Value *V, DILocalVariable *Variable, DIExpression *Expr,
    DILocation *DL, bool IsDbgDeclare, const SDValue &N) {
  const Argument *Arg = dyn_cast<Argument>(V);
  if (!Arg)
    return false;

  MachineFunction &MF = DAG.getMachineFunction();
  const TargetInstrInfo *TII = DAG.getSubtarget().getInstrInfo();

  bool IsIndirect = false;
  Optional<MachineOperand> Op;
  // Some arguments' frame index is recorded during argument lowering.
  int FI = FuncInfo.getArgumentFrameIndex(Arg);
  if (FI != std::numeric_limits<int>::max())
    Op = MachineOperand::CreateFI(FI);

  if (!Op && N.getNode()) {
    unsigned Reg = getUnderlyingArgReg(N);
    if (Reg && TargetRegisterInfo::isVirtualRegister(Reg)) {
      MachineRegisterInfo &RegInfo = MF.getRegInfo();
      unsigned PR = RegInfo.getLiveInPhysReg(Reg);
      if (PR)
        Reg = PR;
    }
    if (Reg) {
      Op = MachineOperand::CreateReg(Reg, false);
      IsIndirect = IsDbgDeclare;
    }
  }

  if (!Op && N.getNode())
    // Check if frame index is available.
    if (LoadSDNode *LNode = dyn_cast<LoadSDNode>(N.getNode()))
      if (FrameIndexSDNode *FINode =
          dyn_cast<FrameIndexSDNode>(LNode->getBasePtr().getNode()))
        Op = MachineOperand::CreateFI(FINode->getIndex());

  if (!Op) {
    // Check if ValueMap has reg number.
    DenseMap<const Value *, unsigned>::iterator VMI = FuncInfo.ValueMap.find(V);
    if (VMI != FuncInfo.ValueMap.end()) {
      const auto &TLI = DAG.getTargetLoweringInfo();
      RegsForValue RFV(V->getContext(), TLI, DAG.getDataLayout(), VMI->second,
                       V->getType(), getABIRegCopyCC(V));
      if (RFV.occupiesMultipleRegs()) {
        unsigned Offset = 0;
        for (auto RegAndSize : RFV.getRegsAndSizes()) {
          Op = MachineOperand::CreateReg(RegAndSize.first, false);
          auto FragmentExpr = DIExpression::createFragmentExpression(
              Expr, Offset, RegAndSize.second);
          if (!FragmentExpr)
            continue;
          FuncInfo.ArgDbgValues.push_back(
              BuildMI(MF, DL, TII->get(TargetOpcode::DBG_VALUE), IsDbgDeclare,
                      Op->getReg(), Variable, *FragmentExpr));
          Offset += RegAndSize.second;
        }
        return true;
      }
      Op = MachineOperand::CreateReg(VMI->second, false);
      IsIndirect = IsDbgDeclare;
    }
  }

  if (!Op)
    return false;

  assert(Variable->isValidLocationForIntrinsic(DL) &&
         "Expected inlined-at fields to agree");
  IsIndirect = (Op->isReg()) ? IsIndirect : true;
  FuncInfo.ArgDbgValues.push_back(
      BuildMI(MF, DL, TII->get(TargetOpcode::DBG_VALUE), IsIndirect,
              *Op, Variable, Expr));

  return true;
}

/// Return the appropriate SDDbgValue based on N.
SDDbgValue *SelectionDAGBuilder::getDbgValue(SDValue N,
                                             DILocalVariable *Variable,
                                             DIExpression *Expr,
                                             const DebugLoc &dl,
                                             unsigned DbgSDNodeOrder) {
  if (auto *FISDN = dyn_cast<FrameIndexSDNode>(N.getNode())) {
    // Construct a FrameIndexDbgValue for FrameIndexSDNodes so we can describe
    // stack slot locations.
    //
    // Consider "int x = 0; int *px = &x;". There are two kinds of interesting
    // debug values here after optimization:
    //
    //   dbg.value(i32* %px, !"int *px", !DIExpression()), and
    //   dbg.value(i32* %px, !"int x", !DIExpression(DW_OP_deref))
    //
    // Both describe the direct values of their associated variables.
    return DAG.getFrameIndexDbgValue(Variable, Expr, FISDN->getIndex(),
                                     /*IsIndirect*/ false, dl, DbgSDNodeOrder);
  }
  return DAG.getDbgValue(Variable, Expr, N.getNode(), N.getResNo(),
                         /*IsIndirect*/ false, dl, DbgSDNodeOrder);
}

// VisualStudio defines setjmp as _setjmp
#if defined(_MSC_VER) && defined(setjmp) && \
                         !defined(setjmp_undefined_for_msvc)
#  pragma push_macro("setjmp")
#  undef setjmp
#  define setjmp_undefined_for_msvc
#endif

/// Lower the call to the specified intrinsic function. If we want to emit this
/// as a call to a named external function, return the name. Otherwise, lower it
/// and return null.
const char *
SelectionDAGBuilder::visitIntrinsicCall(const CallInst &I, unsigned Intrinsic) {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  SDLoc sdl = getCurSDLoc();
  DebugLoc dl = getCurDebugLoc();
  SDValue Res;

  switch (Intrinsic) {
  default:
    // By default, turn this into a target intrinsic node.
    visitTargetIntrinsic(I, Intrinsic);
    return nullptr;
  case Intrinsic::vastart:  visitVAStart(I); return nullptr;
  case Intrinsic::vaend:    visitVAEnd(I); return nullptr;
  case Intrinsic::vacopy:   visitVACopy(I); return nullptr;
  case Intrinsic::returnaddress:
    setValue(&I, DAG.getNode(ISD::RETURNADDR, sdl,
                             TLI.getPointerTy(DAG.getDataLayout()),
                             getValue(I.getArgOperand(0))));
    return nullptr;
  case Intrinsic::addressofreturnaddress:
    setValue(&I, DAG.getNode(ISD::ADDROFRETURNADDR, sdl,
                             TLI.getPointerTy(DAG.getDataLayout())));
    return nullptr;
  case Intrinsic::sponentry:
    setValue(&I, DAG.getNode(ISD::SPONENTRY, sdl,
                             TLI.getPointerTy(DAG.getDataLayout())));
    return nullptr;
  case Intrinsic::frameaddress:
    setValue(&I, DAG.getNode(ISD::FRAMEADDR, sdl,
                             TLI.getPointerTy(DAG.getDataLayout()),
                             getValue(I.getArgOperand(0))));
    return nullptr;
  case Intrinsic::read_register: {
    Value *Reg = I.getArgOperand(0);
    SDValue Chain = getRoot();
    SDValue RegName =
        DAG.getMDNode(cast<MDNode>(cast<MetadataAsValue>(Reg)->getMetadata()));
    EVT VT = TLI.getValueType(DAG.getDataLayout(), I.getType());
    Res = DAG.getNode(ISD::READ_REGISTER, sdl,
      DAG.getVTList(VT, MVT::Other), Chain, RegName);
    setValue(&I, Res);
    DAG.setRoot(Res.getValue(1));
    return nullptr;
  }
  case Intrinsic::write_register: {
    Value *Reg = I.getArgOperand(0);
    Value *RegValue = I.getArgOperand(1);
    SDValue Chain = getRoot();
    SDValue RegName =
        DAG.getMDNode(cast<MDNode>(cast<MetadataAsValue>(Reg)->getMetadata()));
    DAG.setRoot(DAG.getNode(ISD::WRITE_REGISTER, sdl, MVT::Other, Chain,
                            RegName, getValue(RegValue)));
    return nullptr;
  }
  case Intrinsic::setjmp:
    return &"_setjmp"[!TLI.usesUnderscoreSetJmp()];
  case Intrinsic::longjmp:
    return &"_longjmp"[!TLI.usesUnderscoreLongJmp()];
  case Intrinsic::memcpy: {
    const auto &MCI = cast<MemCpyInst>(I);
    SDValue Op1 = getValue(I.getArgOperand(0));
    SDValue Op2 = getValue(I.getArgOperand(1));
    SDValue Op3 = getValue(I.getArgOperand(2));
    // @llvm.memcpy defines 0 and 1 to both mean no alignment.
    unsigned DstAlign = std::max<unsigned>(MCI.getDestAlignment(), 1);
    unsigned SrcAlign = std::max<unsigned>(MCI.getSourceAlignment(), 1);
    unsigned Align = MinAlign(DstAlign, SrcAlign);
    bool isVol = MCI.isVolatile();
    bool isTC = I.isTailCall() && isInTailCallPosition(&I, DAG.getTarget());
    // FIXME: Support passing different dest/src alignments to the memcpy DAG
    // node.
    SDValue MC = DAG.getMemcpy(getRoot(), sdl, Op1, Op2, Op3, Align, isVol,
                               false, isTC,
                               MachinePointerInfo(I.getArgOperand(0)),
                               MachinePointerInfo(I.getArgOperand(1)));
    updateDAGForMaybeTailCall(MC);
    return nullptr;
  }
  case Intrinsic::memset: {
    const auto &MSI = cast<MemSetInst>(I);
    SDValue Op1 = getValue(I.getArgOperand(0));
    SDValue Op2 = getValue(I.getArgOperand(1));
    SDValue Op3 = getValue(I.getArgOperand(2));
    // @llvm.memset defines 0 and 1 to both mean no alignment.
    unsigned Align = std::max<unsigned>(MSI.getDestAlignment(), 1);
    bool isVol = MSI.isVolatile();
    bool isTC = I.isTailCall() && isInTailCallPosition(&I, DAG.getTarget());
    SDValue MS = DAG.getMemset(getRoot(), sdl, Op1, Op2, Op3, Align, isVol,
                               isTC, MachinePointerInfo(I.getArgOperand(0)));
    updateDAGForMaybeTailCall(MS);
    return nullptr;
  }
  case Intrinsic::memmove: {
    const auto &MMI = cast<MemMoveInst>(I);
    SDValue Op1 = getValue(I.getArgOperand(0));
    SDValue Op2 = getValue(I.getArgOperand(1));
    SDValue Op3 = getValue(I.getArgOperand(2));
    // @llvm.memmove defines 0 and 1 to both mean no alignment.
    unsigned DstAlign = std::max<unsigned>(MMI.getDestAlignment(), 1);
    unsigned SrcAlign = std::max<unsigned>(MMI.getSourceAlignment(), 1);
    unsigned Align = MinAlign(DstAlign, SrcAlign);
    bool isVol = MMI.isVolatile();
    bool isTC = I.isTailCall() && isInTailCallPosition(&I, DAG.getTarget());
    // FIXME: Support passing different dest/src alignments to the memmove DAG
    // node.
    SDValue MM = DAG.getMemmove(getRoot(), sdl, Op1, Op2, Op3, Align, isVol,
                                isTC, MachinePointerInfo(I.getArgOperand(0)),
                                MachinePointerInfo(I.getArgOperand(1)));
    updateDAGForMaybeTailCall(MM);
    return nullptr;
  }
  case Intrinsic::memcpy_element_unordered_atomic: {
    const AtomicMemCpyInst &MI = cast<AtomicMemCpyInst>(I);
    SDValue Dst = getValue(MI.getRawDest());
    SDValue Src = getValue(MI.getRawSource());
    SDValue Length = getValue(MI.getLength());

    unsigned DstAlign = MI.getDestAlignment();
    unsigned SrcAlign = MI.getSourceAlignment();
    Type *LengthTy = MI.getLength()->getType();
    unsigned ElemSz = MI.getElementSizeInBytes();
    bool isTC = I.isTailCall() && isInTailCallPosition(&I, DAG.getTarget());
    SDValue MC = DAG.getAtomicMemcpy(getRoot(), sdl, Dst, DstAlign, Src,
                                     SrcAlign, Length, LengthTy, ElemSz, isTC,
                                     MachinePointerInfo(MI.getRawDest()),
                                     MachinePointerInfo(MI.getRawSource()));
    updateDAGForMaybeTailCall(MC);
    return nullptr;
  }
  case Intrinsic::memmove_element_unordered_atomic: {
    auto &MI = cast<AtomicMemMoveInst>(I);
    SDValue Dst = getValue(MI.getRawDest());
    SDValue Src = getValue(MI.getRawSource());
    SDValue Length = getValue(MI.getLength());

    unsigned DstAlign = MI.getDestAlignment();
    unsigned SrcAlign = MI.getSourceAlignment();
    Type *LengthTy = MI.getLength()->getType();
    unsigned ElemSz = MI.getElementSizeInBytes();
    bool isTC = I.isTailCall() && isInTailCallPosition(&I, DAG.getTarget());
    SDValue MC = DAG.getAtomicMemmove(getRoot(), sdl, Dst, DstAlign, Src,
                                      SrcAlign, Length, LengthTy, ElemSz, isTC,
                                      MachinePointerInfo(MI.getRawDest()),
                                      MachinePointerInfo(MI.getRawSource()));
    updateDAGForMaybeTailCall(MC);
    return nullptr;
  }
  case Intrinsic::memset_element_unordered_atomic: {
    auto &MI = cast<AtomicMemSetInst>(I);
    SDValue Dst = getValue(MI.getRawDest());
    SDValue Val = getValue(MI.getValue());
    SDValue Length = getValue(MI.getLength());

    unsigned DstAlign = MI.getDestAlignment();
    Type *LengthTy = MI.getLength()->getType();
    unsigned ElemSz = MI.getElementSizeInBytes();
    bool isTC = I.isTailCall() && isInTailCallPosition(&I, DAG.getTarget());
    SDValue MC = DAG.getAtomicMemset(getRoot(), sdl, Dst, DstAlign, Val, Length,
                                     LengthTy, ElemSz, isTC,
                                     MachinePointerInfo(MI.getRawDest()));
    updateDAGForMaybeTailCall(MC);
    return nullptr;
  }
  case Intrinsic::dbg_addr:
  case Intrinsic::dbg_declare: {
    const auto &DI = cast<DbgVariableIntrinsic>(I);
    DILocalVariable *Variable = DI.getVariable();
    DIExpression *Expression = DI.getExpression();
    dropDanglingDebugInfo(Variable, Expression);
    assert(Variable && "Missing variable");

    // Check if address has undef value.
    const Value *Address = DI.getVariableLocation();
    if (!Address || isa<UndefValue>(Address) ||
        (Address->use_empty() && !isa<Argument>(Address))) {
      LLVM_DEBUG(dbgs() << "Dropping debug info for " << DI << "\n");
      return nullptr;
    }

    bool isParameter = Variable->isParameter() || isa<Argument>(Address);

    // Check if this variable can be described by a frame index, typically
    // either as a static alloca or a byval parameter.
    int FI = std::numeric_limits<int>::max();
    if (const auto *AI =
            dyn_cast<AllocaInst>(Address->stripInBoundsConstantOffsets())) {
      if (AI->isStaticAlloca()) {
        auto I = FuncInfo.StaticAllocaMap.find(AI);
        if (I != FuncInfo.StaticAllocaMap.end())
          FI = I->second;
      }
    } else if (const auto *Arg = dyn_cast<Argument>(
                   Address->stripInBoundsConstantOffsets())) {
      FI = FuncInfo.getArgumentFrameIndex(Arg);
    }

    // llvm.dbg.addr is control dependent and always generates indirect
    // DBG_VALUE instructions. llvm.dbg.declare is handled as a frame index in
    // the MachineFunction variable table.
    if (FI != std::numeric_limits<int>::max()) {
      if (Intrinsic == Intrinsic::dbg_addr) {
        SDDbgValue *SDV = DAG.getFrameIndexDbgValue(
            Variable, Expression, FI, /*IsIndirect*/ true, dl, SDNodeOrder);
        DAG.AddDbgValue(SDV, getRoot().getNode(), isParameter);
      }
      return nullptr;
    }

    SDValue &N = NodeMap[Address];
    if (!N.getNode() && isa<Argument>(Address))
      // Check unused arguments map.
      N = UnusedArgNodeMap[Address];
    SDDbgValue *SDV;
    if (N.getNode()) {
      if (const BitCastInst *BCI = dyn_cast<BitCastInst>(Address))
        Address = BCI->getOperand(0);
      // Parameters are handled specially.
      auto FINode = dyn_cast<FrameIndexSDNode>(N.getNode());
      if (isParameter && FINode) {
        // Byval parameter. We have a frame index at this point.
        SDV =
            DAG.getFrameIndexDbgValue(Variable, Expression, FINode->getIndex(),
                                      /*IsIndirect*/ true, dl, SDNodeOrder);
      } else if (isa<Argument>(Address)) {
        // Address is an argument, so try to emit its dbg value using
        // virtual register info from the FuncInfo.ValueMap.
        EmitFuncArgumentDbgValue(Address, Variable, Expression, dl, true, N);
        return nullptr;
      } else {
        SDV = DAG.getDbgValue(Variable, Expression, N.getNode(), N.getResNo(),
                              true, dl, SDNodeOrder);
      }
      DAG.AddDbgValue(SDV, N.getNode(), isParameter);
    } else {
      // If Address is an argument then try to emit its dbg value using
      // virtual register info from the FuncInfo.ValueMap.
      if (!EmitFuncArgumentDbgValue(Address, Variable, Expression, dl, true,
                                    N)) {
        LLVM_DEBUG(dbgs() << "Dropping debug info for " << DI << "\n");
      }
    }
    return nullptr;
  }
  case Intrinsic::dbg_label: {
    const DbgLabelInst &DI = cast<DbgLabelInst>(I);
    DILabel *Label = DI.getLabel();
    assert(Label && "Missing label");

    SDDbgLabel *SDV;
    SDV = DAG.getDbgLabel(Label, dl, SDNodeOrder);
    DAG.AddDbgLabel(SDV);
    return nullptr;
  }
  case Intrinsic::dbg_value: {
    const DbgValueInst &DI = cast<DbgValueInst>(I);
    assert(DI.getVariable() && "Missing variable");

    DILocalVariable *Variable = DI.getVariable();
    DIExpression *Expression = DI.getExpression();
    dropDanglingDebugInfo(Variable, Expression);
    const Value *V = DI.getValue();
    if (!V)
      return nullptr;

    SDDbgValue *SDV;
    if (isa<ConstantInt>(V) || isa<ConstantFP>(V) || isa<UndefValue>(V) ||
        isa<ConstantPointerNull>(V)) {
      SDV = DAG.getConstantDbgValue(Variable, Expression, V, dl, SDNodeOrder);
      DAG.AddDbgValue(SDV, nullptr, false);
      return nullptr;
    }

    // Do not use getValue() in here; we don't want to generate code at
    // this point if it hasn't been done yet.
    SDValue N = NodeMap[V];
    if (!N.getNode() && isa<Argument>(V)) // Check unused arguments map.
      N = UnusedArgNodeMap[V];
    if (N.getNode()) {
      if (EmitFuncArgumentDbgValue(V, Variable, Expression, dl, false, N))
        return nullptr;
      SDV = getDbgValue(N, Variable, Expression, dl, SDNodeOrder);
      DAG.AddDbgValue(SDV, N.getNode(), false);
      return nullptr;
    }

    // PHI nodes have already been selected, so we should know which VReg that
    // is assigns to already.
    if (isa<PHINode>(V)) {
      auto VMI = FuncInfo.ValueMap.find(V);
      if (VMI != FuncInfo.ValueMap.end()) {
        unsigned Reg = VMI->second;
        // The PHI node may be split up into several MI PHI nodes (in
        // FunctionLoweringInfo::set).
        RegsForValue RFV(V->getContext(), TLI, DAG.getDataLayout(), Reg,
                         V->getType(), None);
        if (RFV.occupiesMultipleRegs()) {
          unsigned Offset = 0;
          unsigned BitsToDescribe = 0;
          if (auto VarSize = Variable->getSizeInBits())
            BitsToDescribe = *VarSize;
          if (auto Fragment = Expression->getFragmentInfo())
            BitsToDescribe = Fragment->SizeInBits;
          for (auto RegAndSize : RFV.getRegsAndSizes()) {
            unsigned RegisterSize = RegAndSize.second;
            // Bail out if all bits are described already.
            if (Offset >= BitsToDescribe)
              break;
            unsigned FragmentSize = (Offset + RegisterSize > BitsToDescribe)
                ? BitsToDescribe - Offset
                : RegisterSize;
            auto FragmentExpr = DIExpression::createFragmentExpression(
                Expression, Offset, FragmentSize);
            if (!FragmentExpr)
                continue;
            SDV = DAG.getVRegDbgValue(Variable, *FragmentExpr, RegAndSize.first,
                                      false, dl, SDNodeOrder);
            DAG.AddDbgValue(SDV, nullptr, false);
            Offset += RegisterSize;
          }
        } else {
          SDV = DAG.getVRegDbgValue(Variable, Expression, Reg, false, dl,
                                    SDNodeOrder);
          DAG.AddDbgValue(SDV, nullptr, false);
        }
        return nullptr;
      }
    }

    // TODO: When we get here we will either drop the dbg.value completely, or
    // we try to move it forward by letting it dangle for awhile. So we should
    // probably add an extra DbgValue to the DAG here, with a reference to
    // "noreg", to indicate that we have lost the debug location for the
    // variable.

    if (!V->use_empty() ) {
      // Do not call getValue(V) yet, as we don't want to generate code.
      // Remember it for later.
      DanglingDebugInfoMap[V].emplace_back(&DI, dl, SDNodeOrder);
      return nullptr;
    }

    LLVM_DEBUG(dbgs() << "Dropping debug location info for:\n  " << DI << "\n");
    LLVM_DEBUG(dbgs() << "  Last seen at:\n    " << *V << "\n");
    return nullptr;
  }

  case Intrinsic::eh_typeid_for: {
    // Find the type id for the given typeinfo.
    GlobalValue *GV = ExtractTypeInfo(I.getArgOperand(0));
    unsigned TypeID = DAG.getMachineFunction().getTypeIDFor(GV);
    Res = DAG.getConstant(TypeID, sdl, MVT::i32);
    setValue(&I, Res);
    return nullptr;
  }

  case Intrinsic::eh_return_i32:
  case Intrinsic::eh_return_i64:
    DAG.getMachineFunction().setCallsEHReturn(true);
    DAG.setRoot(DAG.getNode(ISD::EH_RETURN, sdl,
                            MVT::Other,
                            getControlRoot(),
                            getValue(I.getArgOperand(0)),
                            getValue(I.getArgOperand(1))));
    return nullptr;
  case Intrinsic::eh_unwind_init:
    DAG.getMachineFunction().setCallsUnwindInit(true);
    return nullptr;
  case Intrinsic::eh_dwarf_cfa:
    setValue(&I, DAG.getNode(ISD::EH_DWARF_CFA, sdl,
                             TLI.getPointerTy(DAG.getDataLayout()),
                             getValue(I.getArgOperand(0))));
    return nullptr;
  case Intrinsic::eh_sjlj_callsite: {
    MachineModuleInfo &MMI = DAG.getMachineFunction().getMMI();
    ConstantInt *CI = dyn_cast<ConstantInt>(I.getArgOperand(0));
    assert(CI && "Non-constant call site value in eh.sjlj.callsite!");
    assert(MMI.getCurrentCallSite() == 0 && "Overlapping call sites!");

    MMI.setCurrentCallSite(CI->getZExtValue());
    return nullptr;
  }
  case Intrinsic::eh_sjlj_functioncontext: {
    // Get and store the index of the function context.
    MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
    AllocaInst *FnCtx =
      cast<AllocaInst>(I.getArgOperand(0)->stripPointerCasts());
    int FI = FuncInfo.StaticAllocaMap[FnCtx];
    MFI.setFunctionContextIndex(FI);
    return nullptr;
  }
  case Intrinsic::eh_sjlj_setjmp: {
    SDValue Ops[2];
    Ops[0] = getRoot();
    Ops[1] = getValue(I.getArgOperand(0));
    SDValue Op = DAG.getNode(ISD::EH_SJLJ_SETJMP, sdl,
                             DAG.getVTList(MVT::i32, MVT::Other), Ops);
    setValue(&I, Op.getValue(0));
    DAG.setRoot(Op.getValue(1));
    return nullptr;
  }
  case Intrinsic::eh_sjlj_longjmp:
    DAG.setRoot(DAG.getNode(ISD::EH_SJLJ_LONGJMP, sdl, MVT::Other,
                            getRoot(), getValue(I.getArgOperand(0))));
    return nullptr;
  case Intrinsic::eh_sjlj_setup_dispatch:
    DAG.setRoot(DAG.getNode(ISD::EH_SJLJ_SETUP_DISPATCH, sdl, MVT::Other,
                            getRoot()));
    return nullptr;
  case Intrinsic::masked_gather:
    visitMaskedGather(I);
    return nullptr;
  case Intrinsic::masked_load:
    visitMaskedLoad(I);
    return nullptr;
  case Intrinsic::masked_scatter:
    visitMaskedScatter(I);
    return nullptr;
  case Intrinsic::masked_store:
    visitMaskedStore(I);
    return nullptr;
  case Intrinsic::masked_expandload:
    visitMaskedLoad(I, true /* IsExpanding */);
    return nullptr;
  case Intrinsic::masked_compressstore:
    visitMaskedStore(I, true /* IsCompressing */);
    return nullptr;
  case Intrinsic::x86_mmx_pslli_w:
  case Intrinsic::x86_mmx_pslli_d:
  case Intrinsic::x86_mmx_pslli_q:
  case Intrinsic::x86_mmx_psrli_w:
  case Intrinsic::x86_mmx_psrli_d:
  case Intrinsic::x86_mmx_psrli_q:
  case Intrinsic::x86_mmx_psrai_w:
  case Intrinsic::x86_mmx_psrai_d: {
    SDValue ShAmt = getValue(I.getArgOperand(1));
    if (isa<ConstantSDNode>(ShAmt)) {
      visitTargetIntrinsic(I, Intrinsic);
      return nullptr;
    }
    unsigned NewIntrinsic = 0;
    EVT ShAmtVT = MVT::v2i32;
    switch (Intrinsic) {
    case Intrinsic::x86_mmx_pslli_w:
      NewIntrinsic = Intrinsic::x86_mmx_psll_w;
      break;
    case Intrinsic::x86_mmx_pslli_d:
      NewIntrinsic = Intrinsic::x86_mmx_psll_d;
      break;
    case Intrinsic::x86_mmx_pslli_q:
      NewIntrinsic = Intrinsic::x86_mmx_psll_q;
      break;
    case Intrinsic::x86_mmx_psrli_w:
      NewIntrinsic = Intrinsic::x86_mmx_psrl_w;
      break;
    case Intrinsic::x86_mmx_psrli_d:
      NewIntrinsic = Intrinsic::x86_mmx_psrl_d;
      break;
    case Intrinsic::x86_mmx_psrli_q:
      NewIntrinsic = Intrinsic::x86_mmx_psrl_q;
      break;
    case Intrinsic::x86_mmx_psrai_w:
      NewIntrinsic = Intrinsic::x86_mmx_psra_w;
      break;
    case Intrinsic::x86_mmx_psrai_d:
      NewIntrinsic = Intrinsic::x86_mmx_psra_d;
      break;
    default: llvm_unreachable("Impossible intrinsic");  // Can't reach here.
    }

    // The vector shift intrinsics with scalars uses 32b shift amounts but
    // the sse2/mmx shift instructions reads 64 bits. Set the upper 32 bits
    // to be zero.
    // We must do this early because v2i32 is not a legal type.
    SDValue ShOps[2];
    ShOps[0] = ShAmt;
    ShOps[1] = DAG.getConstant(0, sdl, MVT::i32);
    ShAmt =  DAG.getBuildVector(ShAmtVT, sdl, ShOps);
    EVT DestVT = TLI.getValueType(DAG.getDataLayout(), I.getType());
    ShAmt = DAG.getNode(ISD::BITCAST, sdl, DestVT, ShAmt);
    Res = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, sdl, DestVT,
                       DAG.getConstant(NewIntrinsic, sdl, MVT::i32),
                       getValue(I.getArgOperand(0)), ShAmt);
    setValue(&I, Res);
    return nullptr;
  }
  case Intrinsic::powi:
    setValue(&I, ExpandPowI(sdl, getValue(I.getArgOperand(0)),
                            getValue(I.getArgOperand(1)), DAG));
    return nullptr;
  case Intrinsic::log:
    setValue(&I, expandLog(sdl, getValue(I.getArgOperand(0)), DAG, TLI));
    return nullptr;
  case Intrinsic::log2:
    setValue(&I, expandLog2(sdl, getValue(I.getArgOperand(0)), DAG, TLI));
    return nullptr;
  case Intrinsic::log10:
    setValue(&I, expandLog10(sdl, getValue(I.getArgOperand(0)), DAG, TLI));
    return nullptr;
  case Intrinsic::exp:
    setValue(&I, expandExp(sdl, getValue(I.getArgOperand(0)), DAG, TLI));
    return nullptr;
  case Intrinsic::exp2:
    setValue(&I, expandExp2(sdl, getValue(I.getArgOperand(0)), DAG, TLI));
    return nullptr;
  case Intrinsic::pow:
    setValue(&I, expandPow(sdl, getValue(I.getArgOperand(0)),
                           getValue(I.getArgOperand(1)), DAG, TLI));
    return nullptr;
  case Intrinsic::sqrt:
  case Intrinsic::fabs:
  case Intrinsic::sin:
  case Intrinsic::cos:
  case Intrinsic::floor:
  case Intrinsic::ceil:
  case Intrinsic::trunc:
  case Intrinsic::rint:
  case Intrinsic::nearbyint:
  case Intrinsic::round:
  case Intrinsic::canonicalize: {
    unsigned Opcode;
    switch (Intrinsic) {
    default: llvm_unreachable("Impossible intrinsic");  // Can't reach here.
    case Intrinsic::sqrt:      Opcode = ISD::FSQRT;      break;
    case Intrinsic::fabs:      Opcode = ISD::FABS;       break;
    case Intrinsic::sin:       Opcode = ISD::FSIN;       break;
    case Intrinsic::cos:       Opcode = ISD::FCOS;       break;
    case Intrinsic::floor:     Opcode = ISD::FFLOOR;     break;
    case Intrinsic::ceil:      Opcode = ISD::FCEIL;      break;
    case Intrinsic::trunc:     Opcode = ISD::FTRUNC;     break;
    case Intrinsic::rint:      Opcode = ISD::FRINT;      break;
    case Intrinsic::nearbyint: Opcode = ISD::FNEARBYINT; break;
    case Intrinsic::round:     Opcode = ISD::FROUND;     break;
    case Intrinsic::canonicalize: Opcode = ISD::FCANONICALIZE; break;
    }

    setValue(&I, DAG.getNode(Opcode, sdl,
                             getValue(I.getArgOperand(0)).getValueType(),
                             getValue(I.getArgOperand(0))));
    return nullptr;
  }
  case Intrinsic::minnum: {
    auto VT = getValue(I.getArgOperand(0)).getValueType();
    unsigned Opc =
        I.hasNoNaNs() && TLI.isOperationLegalOrCustom(ISD::FMINIMUM, VT)
            ? ISD::FMINIMUM
            : ISD::FMINNUM;
    setValue(&I, DAG.getNode(Opc, sdl, VT,
                             getValue(I.getArgOperand(0)),
                             getValue(I.getArgOperand(1))));
    return nullptr;
  }
  case Intrinsic::maxnum: {
    auto VT = getValue(I.getArgOperand(0)).getValueType();
    unsigned Opc =
        I.hasNoNaNs() && TLI.isOperationLegalOrCustom(ISD::FMAXIMUM, VT)
            ? ISD::FMAXIMUM
            : ISD::FMAXNUM;
    setValue(&I, DAG.getNode(Opc, sdl, VT,
                             getValue(I.getArgOperand(0)),
                             getValue(I.getArgOperand(1))));
    return nullptr;
  }
  case Intrinsic::minimum:
    setValue(&I, DAG.getNode(ISD::FMINIMUM, sdl,
                             getValue(I.getArgOperand(0)).getValueType(),
                             getValue(I.getArgOperand(0)),
                             getValue(I.getArgOperand(1))));
    return nullptr;
  case Intrinsic::maximum:
    setValue(&I, DAG.getNode(ISD::FMAXIMUM, sdl,
                             getValue(I.getArgOperand(0)).getValueType(),
                             getValue(I.getArgOperand(0)),
                             getValue(I.getArgOperand(1))));
    return nullptr;
  case Intrinsic::copysign:
    setValue(&I, DAG.getNode(ISD::FCOPYSIGN, sdl,
                             getValue(I.getArgOperand(0)).getValueType(),
                             getValue(I.getArgOperand(0)),
                             getValue(I.getArgOperand(1))));
    return nullptr;
  case Intrinsic::fma:
    setValue(&I, DAG.getNode(ISD::FMA, sdl,
                             getValue(I.getArgOperand(0)).getValueType(),
                             getValue(I.getArgOperand(0)),
                             getValue(I.getArgOperand(1)),
                             getValue(I.getArgOperand(2))));
    return nullptr;
  case Intrinsic::experimental_constrained_fadd:
  case Intrinsic::experimental_constrained_fsub:
  case Intrinsic::experimental_constrained_fmul:
  case Intrinsic::experimental_constrained_fdiv:
  case Intrinsic::experimental_constrained_frem:
  case Intrinsic::experimental_constrained_fma:
  case Intrinsic::experimental_constrained_sqrt:
  case Intrinsic::experimental_constrained_pow:
  case Intrinsic::experimental_constrained_powi:
  case Intrinsic::experimental_constrained_sin:
  case Intrinsic::experimental_constrained_cos:
  case Intrinsic::experimental_constrained_exp:
  case Intrinsic::experimental_constrained_exp2:
  case Intrinsic::experimental_constrained_log:
  case Intrinsic::experimental_constrained_log10:
  case Intrinsic::experimental_constrained_log2:
  case Intrinsic::experimental_constrained_rint:
  case Intrinsic::experimental_constrained_nearbyint:
  case Intrinsic::experimental_constrained_maxnum:
  case Intrinsic::experimental_constrained_minnum:
  case Intrinsic::experimental_constrained_ceil:
  case Intrinsic::experimental_constrained_floor:
  case Intrinsic::experimental_constrained_round:
  case Intrinsic::experimental_constrained_trunc:
    visitConstrainedFPIntrinsic(cast<ConstrainedFPIntrinsic>(I));
    return nullptr;
  case Intrinsic::fmuladd: {
    EVT VT = TLI.getValueType(DAG.getDataLayout(), I.getType());
    if (TM.Options.AllowFPOpFusion != FPOpFusion::Strict &&
        TLI.isFMAFasterThanFMulAndFAdd(VT)) {
      setValue(&I, DAG.getNode(ISD::FMA, sdl,
                               getValue(I.getArgOperand(0)).getValueType(),
                               getValue(I.getArgOperand(0)),
                               getValue(I.getArgOperand(1)),
                               getValue(I.getArgOperand(2))));
    } else {
      // TODO: Intrinsic calls should have fast-math-flags.
      SDValue Mul = DAG.getNode(ISD::FMUL, sdl,
                                getValue(I.getArgOperand(0)).getValueType(),
                                getValue(I.getArgOperand(0)),
                                getValue(I.getArgOperand(1)));
      SDValue Add = DAG.getNode(ISD::FADD, sdl,
                                getValue(I.getArgOperand(0)).getValueType(),
                                Mul,
                                getValue(I.getArgOperand(2)));
      setValue(&I, Add);
    }
    return nullptr;
  }
  case Intrinsic::convert_to_fp16:
    setValue(&I, DAG.getNode(ISD::BITCAST, sdl, MVT::i16,
                             DAG.getNode(ISD::FP_ROUND, sdl, MVT::f16,
                                         getValue(I.getArgOperand(0)),
                                         DAG.getTargetConstant(0, sdl,
                                                               MVT::i32))));
    return nullptr;
  case Intrinsic::convert_from_fp16:
    setValue(&I, DAG.getNode(ISD::FP_EXTEND, sdl,
                             TLI.getValueType(DAG.getDataLayout(), I.getType()),
                             DAG.getNode(ISD::BITCAST, sdl, MVT::f16,
                                         getValue(I.getArgOperand(0)))));
    return nullptr;
  case Intrinsic::pcmarker: {
    SDValue Tmp = getValue(I.getArgOperand(0));
    DAG.setRoot(DAG.getNode(ISD::PCMARKER, sdl, MVT::Other, getRoot(), Tmp));
    return nullptr;
  }
  case Intrinsic::readcyclecounter: {
    SDValue Op = getRoot();
    Res = DAG.getNode(ISD::READCYCLECOUNTER, sdl,
                      DAG.getVTList(MVT::i64, MVT::Other), Op);
    setValue(&I, Res);
    DAG.setRoot(Res.getValue(1));
    return nullptr;
  }
  case Intrinsic::bitreverse:
    setValue(&I, DAG.getNode(ISD::BITREVERSE, sdl,
                             getValue(I.getArgOperand(0)).getValueType(),
                             getValue(I.getArgOperand(0))));
    return nullptr;
  case Intrinsic::bswap:
    setValue(&I, DAG.getNode(ISD::BSWAP, sdl,
                             getValue(I.getArgOperand(0)).getValueType(),
                             getValue(I.getArgOperand(0))));
    return nullptr;
  case Intrinsic::cttz: {
    SDValue Arg = getValue(I.getArgOperand(0));
    ConstantInt *CI = cast<ConstantInt>(I.getArgOperand(1));
    EVT Ty = Arg.getValueType();
    setValue(&I, DAG.getNode(CI->isZero() ? ISD::CTTZ : ISD::CTTZ_ZERO_UNDEF,
                             sdl, Ty, Arg));
    return nullptr;
  }
  case Intrinsic::ctlz: {
    SDValue Arg = getValue(I.getArgOperand(0));
    ConstantInt *CI = cast<ConstantInt>(I.getArgOperand(1));
    EVT Ty = Arg.getValueType();
    setValue(&I, DAG.getNode(CI->isZero() ? ISD::CTLZ : ISD::CTLZ_ZERO_UNDEF,
                             sdl, Ty, Arg));
    return nullptr;
  }
  case Intrinsic::ctpop: {
    SDValue Arg = getValue(I.getArgOperand(0));
    EVT Ty = Arg.getValueType();
    setValue(&I, DAG.getNode(ISD::CTPOP, sdl, Ty, Arg));
    return nullptr;
  }
  case Intrinsic::fshl:
  case Intrinsic::fshr: {
    bool IsFSHL = Intrinsic == Intrinsic::fshl;
    SDValue X = getValue(I.getArgOperand(0));
    SDValue Y = getValue(I.getArgOperand(1));
    SDValue Z = getValue(I.getArgOperand(2));
    EVT VT = X.getValueType();
    SDValue BitWidthC = DAG.getConstant(VT.getScalarSizeInBits(), sdl, VT);
    SDValue Zero = DAG.getConstant(0, sdl, VT);
    SDValue ShAmt = DAG.getNode(ISD::UREM, sdl, VT, Z, BitWidthC);

    auto FunnelOpcode = IsFSHL ? ISD::FSHL : ISD::FSHR;
    if (TLI.isOperationLegalOrCustom(FunnelOpcode, VT)) {
      setValue(&I, DAG.getNode(FunnelOpcode, sdl, VT, X, Y, Z));
      return nullptr;
    }

    // When X == Y, this is rotate. If the data type has a power-of-2 size, we
    // avoid the select that is necessary in the general case to filter out
    // the 0-shift possibility that leads to UB.
    if (X == Y && isPowerOf2_32(VT.getScalarSizeInBits())) {
      auto RotateOpcode = IsFSHL ? ISD::ROTL : ISD::ROTR;
      if (TLI.isOperationLegalOrCustom(RotateOpcode, VT)) {
        setValue(&I, DAG.getNode(RotateOpcode, sdl, VT, X, Z));
        return nullptr;
      }

      // Some targets only rotate one way. Try the opposite direction.
      RotateOpcode = IsFSHL ? ISD::ROTR : ISD::ROTL;
      if (TLI.isOperationLegalOrCustom(RotateOpcode, VT)) {
        // Negate the shift amount because it is safe to ignore the high bits.
        SDValue NegShAmt = DAG.getNode(ISD::SUB, sdl, VT, Zero, Z);
        setValue(&I, DAG.getNode(RotateOpcode, sdl, VT, X, NegShAmt));
        return nullptr;
      }

      // fshl (rotl): (X << (Z % BW)) | (X >> ((0 - Z) % BW))
      // fshr (rotr): (X << ((0 - Z) % BW)) | (X >> (Z % BW))
      SDValue NegZ = DAG.getNode(ISD::SUB, sdl, VT, Zero, Z);
      SDValue NShAmt = DAG.getNode(ISD::UREM, sdl, VT, NegZ, BitWidthC);
      SDValue ShX = DAG.getNode(ISD::SHL, sdl, VT, X, IsFSHL ? ShAmt : NShAmt);
      SDValue ShY = DAG.getNode(ISD::SRL, sdl, VT, X, IsFSHL ? NShAmt : ShAmt);
      setValue(&I, DAG.getNode(ISD::OR, sdl, VT, ShX, ShY));
      return nullptr;
    }

    // fshl: (X << (Z % BW)) | (Y >> (BW - (Z % BW)))
    // fshr: (X << (BW - (Z % BW))) | (Y >> (Z % BW))
    SDValue InvShAmt = DAG.getNode(ISD::SUB, sdl, VT, BitWidthC, ShAmt);
    SDValue ShX = DAG.getNode(ISD::SHL, sdl, VT, X, IsFSHL ? ShAmt : InvShAmt);
    SDValue ShY = DAG.getNode(ISD::SRL, sdl, VT, Y, IsFSHL ? InvShAmt : ShAmt);
    SDValue Or = DAG.getNode(ISD::OR, sdl, VT, ShX, ShY);

    // If (Z % BW == 0), then the opposite direction shift is shift-by-bitwidth,
    // and that is undefined. We must compare and select to avoid UB.
    EVT CCVT = MVT::i1;
    if (VT.isVector())
      CCVT = EVT::getVectorVT(*Context, CCVT, VT.getVectorNumElements());

    // For fshl, 0-shift returns the 1st arg (X).
    // For fshr, 0-shift returns the 2nd arg (Y).
    SDValue IsZeroShift = DAG.getSetCC(sdl, CCVT, ShAmt, Zero, ISD::SETEQ);
    setValue(&I, DAG.getSelect(sdl, VT, IsZeroShift, IsFSHL ? X : Y, Or));
    return nullptr;
  }
  case Intrinsic::sadd_sat: {
    SDValue Op1 = getValue(I.getArgOperand(0));
    SDValue Op2 = getValue(I.getArgOperand(1));
    setValue(&I, DAG.getNode(ISD::SADDSAT, sdl, Op1.getValueType(), Op1, Op2));
    return nullptr;
  }
  case Intrinsic::uadd_sat: {
    SDValue Op1 = getValue(I.getArgOperand(0));
    SDValue Op2 = getValue(I.getArgOperand(1));
    setValue(&I, DAG.getNode(ISD::UADDSAT, sdl, Op1.getValueType(), Op1, Op2));
    return nullptr;
  }
  case Intrinsic::ssub_sat: {
    SDValue Op1 = getValue(I.getArgOperand(0));
    SDValue Op2 = getValue(I.getArgOperand(1));
    setValue(&I, DAG.getNode(ISD::SSUBSAT, sdl, Op1.getValueType(), Op1, Op2));
    return nullptr;
  }
  case Intrinsic::usub_sat: {
    SDValue Op1 = getValue(I.getArgOperand(0));
    SDValue Op2 = getValue(I.getArgOperand(1));
    setValue(&I, DAG.getNode(ISD::USUBSAT, sdl, Op1.getValueType(), Op1, Op2));
    return nullptr;
  }
  case Intrinsic::smul_fix: {
    SDValue Op1 = getValue(I.getArgOperand(0));
    SDValue Op2 = getValue(I.getArgOperand(1));
    SDValue Op3 = getValue(I.getArgOperand(2));
    setValue(&I,
             DAG.getNode(ISD::SMULFIX, sdl, Op1.getValueType(), Op1, Op2, Op3));
    return nullptr;
  }
  case Intrinsic::stacksave: {
    SDValue Op = getRoot();
    Res = DAG.getNode(
        ISD::STACKSAVE, sdl,
        DAG.getVTList(TLI.getPointerTy(DAG.getDataLayout()), MVT::Other), Op);
    setValue(&I, Res);
    DAG.setRoot(Res.getValue(1));
    return nullptr;
  }
  case Intrinsic::stackrestore:
    Res = getValue(I.getArgOperand(0));
    DAG.setRoot(DAG.getNode(ISD::STACKRESTORE, sdl, MVT::Other, getRoot(), Res));
    return nullptr;
  case Intrinsic::get_dynamic_area_offset: {
    SDValue Op = getRoot();
    EVT PtrTy = TLI.getPointerTy(DAG.getDataLayout());
    EVT ResTy = TLI.getValueType(DAG.getDataLayout(), I.getType());
    // Result type for @llvm.get.dynamic.area.offset should match PtrTy for
    // target.
    if (PtrTy != ResTy)
      report_fatal_error("Wrong result type for @llvm.get.dynamic.area.offset"
                         " intrinsic!");
    Res = DAG.getNode(ISD::GET_DYNAMIC_AREA_OFFSET, sdl, DAG.getVTList(ResTy),
                      Op);
    DAG.setRoot(Op);
    setValue(&I, Res);
    return nullptr;
  }
  case Intrinsic::stackguard: {
    EVT PtrTy = TLI.getPointerTy(DAG.getDataLayout());
    MachineFunction &MF = DAG.getMachineFunction();
    const Module &M = *MF.getFunction().getParent();
    SDValue Chain = getRoot();
    if (TLI.useLoadStackGuardNode()) {
      Res = getLoadStackGuard(DAG, sdl, Chain);
    } else {
      const Value *Global = TLI.getSDagStackGuard(M);
      unsigned Align = DL->getPrefTypeAlignment(Global->getType());
      Res = DAG.getLoad(PtrTy, sdl, Chain, getValue(Global),
                        MachinePointerInfo(Global, 0), Align,
                        MachineMemOperand::MOVolatile);
    }
    if (TLI.useStackGuardXorFP())
      Res = TLI.emitStackGuardXorFP(DAG, Res, sdl);
    DAG.setRoot(Chain);
    setValue(&I, Res);
    return nullptr;
  }
  case Intrinsic::stackprotector: {
    // Emit code into the DAG to store the stack guard onto the stack.
    MachineFunction &MF = DAG.getMachineFunction();
    MachineFrameInfo &MFI = MF.getFrameInfo();
    EVT PtrTy = TLI.getPointerTy(DAG.getDataLayout());
    SDValue Src, Chain = getRoot();

    if (TLI.useLoadStackGuardNode())
      Src = getLoadStackGuard(DAG, sdl, Chain);
    else
      Src = getValue(I.getArgOperand(0));   // The guard's value.

    AllocaInst *Slot = cast<AllocaInst>(I.getArgOperand(1));

    int FI = FuncInfo.StaticAllocaMap[Slot];
    MFI.setStackProtectorIndex(FI);

    SDValue FIN = DAG.getFrameIndex(FI, PtrTy);

    // Store the stack protector onto the stack.
    Res = DAG.getStore(Chain, sdl, Src, FIN, MachinePointerInfo::getFixedStack(
                                                 DAG.getMachineFunction(), FI),
                       /* Alignment = */ 0, MachineMemOperand::MOVolatile);
    setValue(&I, Res);
    DAG.setRoot(Res);
    return nullptr;
  }
  case Intrinsic::objectsize: {
    // If we don't know by now, we're never going to know.
    ConstantInt *CI = dyn_cast<ConstantInt>(I.getArgOperand(1));

    assert(CI && "Non-constant type in __builtin_object_size?");

    SDValue Arg = getValue(I.getCalledValue());
    EVT Ty = Arg.getValueType();

    if (CI->isZero())
      Res = DAG.getConstant(-1ULL, sdl, Ty);
    else
      Res = DAG.getConstant(0, sdl, Ty);

    setValue(&I, Res);
    return nullptr;
  }

  case Intrinsic::is_constant:
    // If this wasn't constant-folded away by now, then it's not a
    // constant.
    setValue(&I, DAG.getConstant(0, sdl, MVT::i1));
    return nullptr;

  case Intrinsic::annotation:
  case Intrinsic::ptr_annotation:
  case Intrinsic::launder_invariant_group:
  case Intrinsic::strip_invariant_group:
    // Drop the intrinsic, but forward the value
    setValue(&I, getValue(I.getOperand(0)));
    return nullptr;
  case Intrinsic::assume:
  case Intrinsic::var_annotation:
  case Intrinsic::sideeffect:
    // Discard annotate attributes, assumptions, and artificial side-effects.
    return nullptr;

  case Intrinsic::codeview_annotation: {
    // Emit a label associated with this metadata.
    MachineFunction &MF = DAG.getMachineFunction();
    MCSymbol *Label =
        MF.getMMI().getContext().createTempSymbol("annotation", true);
    Metadata *MD = cast<MetadataAsValue>(I.getArgOperand(0))->getMetadata();
    MF.addCodeViewAnnotation(Label, cast<MDNode>(MD));
    Res = DAG.getLabelNode(ISD::ANNOTATION_LABEL, sdl, getRoot(), Label);
    DAG.setRoot(Res);
    return nullptr;
  }

  case Intrinsic::init_trampoline: {
    const Function *F = cast<Function>(I.getArgOperand(1)->stripPointerCasts());

    SDValue Ops[6];
    Ops[0] = getRoot();
    Ops[1] = getValue(I.getArgOperand(0));
    Ops[2] = getValue(I.getArgOperand(1));
    Ops[3] = getValue(I.getArgOperand(2));
    Ops[4] = DAG.getSrcValue(I.getArgOperand(0));
    Ops[5] = DAG.getSrcValue(F);

    Res = DAG.getNode(ISD::INIT_TRAMPOLINE, sdl, MVT::Other, Ops);

    DAG.setRoot(Res);
    return nullptr;
  }
  case Intrinsic::adjust_trampoline:
    setValue(&I, DAG.getNode(ISD::ADJUST_TRAMPOLINE, sdl,
                             TLI.getPointerTy(DAG.getDataLayout()),
                             getValue(I.getArgOperand(0))));
    return nullptr;
  case Intrinsic::gcroot: {
    assert(DAG.getMachineFunction().getFunction().hasGC() &&
           "only valid in functions with gc specified, enforced by Verifier");
    assert(GFI && "implied by previous");
    const Value *Alloca = I.getArgOperand(0)->stripPointerCasts();
    const Constant *TypeMap = cast<Constant>(I.getArgOperand(1));

    FrameIndexSDNode *FI = cast<FrameIndexSDNode>(getValue(Alloca).getNode());
    GFI->addStackRoot(FI->getIndex(), TypeMap);
    return nullptr;
  }
  case Intrinsic::gcread:
  case Intrinsic::gcwrite:
    llvm_unreachable("GC failed to lower gcread/gcwrite intrinsics!");
  case Intrinsic::flt_rounds:
    setValue(&I, DAG.getNode(ISD::FLT_ROUNDS_, sdl, MVT::i32));
    return nullptr;

  case Intrinsic::expect:
    // Just replace __builtin_expect(exp, c) with EXP.
    setValue(&I, getValue(I.getArgOperand(0)));
    return nullptr;

  case Intrinsic::debugtrap:
  case Intrinsic::trap: {
    StringRef TrapFuncName =
        I.getAttributes()
            .getAttribute(AttributeList::FunctionIndex, "trap-func-name")
            .getValueAsString();
    if (TrapFuncName.empty()) {
      ISD::NodeType Op = (Intrinsic == Intrinsic::trap) ?
        ISD::TRAP : ISD::DEBUGTRAP;
      DAG.setRoot(DAG.getNode(Op, sdl,MVT::Other, getRoot()));
      return nullptr;
    }
    TargetLowering::ArgListTy Args;

    TargetLowering::CallLoweringInfo CLI(DAG);
    CLI.setDebugLoc(sdl).setChain(getRoot()).setLibCallee(
        CallingConv::C, I.getType(),
        DAG.getExternalSymbol(TrapFuncName.data(),
                              TLI.getPointerTy(DAG.getDataLayout())),
        std::move(Args));

    std::pair<SDValue, SDValue> Result = TLI.LowerCallTo(CLI);
    DAG.setRoot(Result.second);
    return nullptr;
  }

  case Intrinsic::uadd_with_overflow:
  case Intrinsic::sadd_with_overflow:
  case Intrinsic::usub_with_overflow:
  case Intrinsic::ssub_with_overflow:
  case Intrinsic::umul_with_overflow:
  case Intrinsic::smul_with_overflow: {
    ISD::NodeType Op;
    switch (Intrinsic) {
    default: llvm_unreachable("Impossible intrinsic");  // Can't reach here.
    case Intrinsic::uadd_with_overflow: Op = ISD::UADDO; break;
    case Intrinsic::sadd_with_overflow: Op = ISD::SADDO; break;
    case Intrinsic::usub_with_overflow: Op = ISD::USUBO; break;
    case Intrinsic::ssub_with_overflow: Op = ISD::SSUBO; break;
    case Intrinsic::umul_with_overflow: Op = ISD::UMULO; break;
    case Intrinsic::smul_with_overflow: Op = ISD::SMULO; break;
    }
    SDValue Op1 = getValue(I.getArgOperand(0));
    SDValue Op2 = getValue(I.getArgOperand(1));

    SDVTList VTs = DAG.getVTList(Op1.getValueType(), MVT::i1);
    setValue(&I, DAG.getNode(Op, sdl, VTs, Op1, Op2));
    return nullptr;
  }
  case Intrinsic::prefetch: {
    SDValue Ops[5];
    unsigned rw = cast<ConstantInt>(I.getArgOperand(1))->getZExtValue();
    auto Flags = rw == 0 ? MachineMemOperand::MOLoad :MachineMemOperand::MOStore;
    Ops[0] = DAG.getRoot();
    Ops[1] = getValue(I.getArgOperand(0));
    Ops[2] = getValue(I.getArgOperand(1));
    Ops[3] = getValue(I.getArgOperand(2));
    Ops[4] = getValue(I.getArgOperand(3));
    SDValue Result = DAG.getMemIntrinsicNode(ISD::PREFETCH, sdl,
                                             DAG.getVTList(MVT::Other), Ops,
                                             EVT::getIntegerVT(*Context, 8),
                                             MachinePointerInfo(I.getArgOperand(0)),
                                             0, /* align */
                                             Flags);

    // Chain the prefetch in parallell with any pending loads, to stay out of
    // the way of later optimizations.
    PendingLoads.push_back(Result);
    Result = getRoot();
    DAG.setRoot(Result);
    return nullptr;
  }
  case Intrinsic::lifetime_start:
  case Intrinsic::lifetime_end: {
    bool IsStart = (Intrinsic == Intrinsic::lifetime_start);
    // Stack coloring is not enabled in O0, discard region information.
    if (TM.getOptLevel() == CodeGenOpt::None)
      return nullptr;

    SmallVector<Value *, 4> Allocas;
    GetUnderlyingObjects(I.getArgOperand(1), Allocas, *DL);

    for (SmallVectorImpl<Value*>::iterator Object = Allocas.begin(),
           E = Allocas.end(); Object != E; ++Object) {
      AllocaInst *LifetimeObject = dyn_cast_or_null<AllocaInst>(*Object);

      // Could not find an Alloca.
      if (!LifetimeObject)
        continue;

      // First check that the Alloca is static, otherwise it won't have a
      // valid frame index.
      auto SI = FuncInfo.StaticAllocaMap.find(LifetimeObject);
      if (SI == FuncInfo.StaticAllocaMap.end())
        return nullptr;

      int FI = SI->second;

      SDValue Ops[2];
      Ops[0] = getRoot();
      Ops[1] =
          DAG.getFrameIndex(FI, TLI.getFrameIndexTy(DAG.getDataLayout()), true);
      unsigned Opcode = (IsStart ? ISD::LIFETIME_START : ISD::LIFETIME_END);

      Res = DAG.getNode(Opcode, sdl, MVT::Other, Ops);
      DAG.setRoot(Res);
    }
    return nullptr;
  }
  case Intrinsic::invariant_start:
    // Discard region information.
    setValue(&I, DAG.getUNDEF(TLI.getPointerTy(DAG.getDataLayout())));
    return nullptr;
  case Intrinsic::invariant_end:
    // Discard region information.
    return nullptr;
  case Intrinsic::clear_cache:
    return TLI.getClearCacheBuiltinName();
  case Intrinsic::donothing:
    // ignore
    return nullptr;
  case Intrinsic::experimental_stackmap:
    visitStackmap(I);
    return nullptr;
  case Intrinsic::experimental_patchpoint_void:
  case Intrinsic::experimental_patchpoint_i64:
    visitPatchpoint(&I);
    return nullptr;
  case Intrinsic::experimental_gc_statepoint:
    LowerStatepoint(ImmutableStatepoint(&I));
    return nullptr;
  case Intrinsic::experimental_gc_result:
    visitGCResult(cast<GCResultInst>(I));
    return nullptr;
  case Intrinsic::experimental_gc_relocate:
    visitGCRelocate(cast<GCRelocateInst>(I));
    return nullptr;
  case Intrinsic::instrprof_increment:
    llvm_unreachable("instrprof failed to lower an increment");
  case Intrinsic::instrprof_value_profile:
    llvm_unreachable("instrprof failed to lower a value profiling call");
  case Intrinsic::localescape: {
    MachineFunction &MF = DAG.getMachineFunction();
    const TargetInstrInfo *TII = DAG.getSubtarget().getInstrInfo();

    // Directly emit some LOCAL_ESCAPE machine instrs. Label assignment emission
    // is the same on all targets.
    for (unsigned Idx = 0, E = I.getNumArgOperands(); Idx < E; ++Idx) {
      Value *Arg = I.getArgOperand(Idx)->stripPointerCasts();
      if (isa<ConstantPointerNull>(Arg))
        continue; // Skip null pointers. They represent a hole in index space.
      AllocaInst *Slot = cast<AllocaInst>(Arg);
      assert(FuncInfo.StaticAllocaMap.count(Slot) &&
             "can only escape static allocas");
      int FI = FuncInfo.StaticAllocaMap[Slot];
      MCSymbol *FrameAllocSym =
          MF.getMMI().getContext().getOrCreateFrameAllocSymbol(
              GlobalValue::dropLLVMManglingEscape(MF.getName()), Idx);
      BuildMI(*FuncInfo.MBB, FuncInfo.InsertPt, dl,
              TII->get(TargetOpcode::LOCAL_ESCAPE))
          .addSym(FrameAllocSym)
          .addFrameIndex(FI);
    }

    MF.setHasLocalEscape(true);

    return nullptr;
  }

  case Intrinsic::localrecover: {
    // i8* @llvm.localrecover(i8* %fn, i8* %fp, i32 %idx)
    MachineFunction &MF = DAG.getMachineFunction();
    MVT PtrVT = TLI.getPointerTy(DAG.getDataLayout(), 0);

    // Get the symbol that defines the frame offset.
    auto *Fn = cast<Function>(I.getArgOperand(0)->stripPointerCasts());
    auto *Idx = cast<ConstantInt>(I.getArgOperand(2));
    unsigned IdxVal =
        unsigned(Idx->getLimitedValue(std::numeric_limits<int>::max()));
    MCSymbol *FrameAllocSym =
        MF.getMMI().getContext().getOrCreateFrameAllocSymbol(
            GlobalValue::dropLLVMManglingEscape(Fn->getName()), IdxVal);

    // Create a MCSymbol for the label to avoid any target lowering
    // that would make this PC relative.
    SDValue OffsetSym = DAG.getMCSymbol(FrameAllocSym, PtrVT);
    SDValue OffsetVal =
        DAG.getNode(ISD::LOCAL_RECOVER, sdl, PtrVT, OffsetSym);

    // Add the offset to the FP.
    Value *FP = I.getArgOperand(1);
    SDValue FPVal = getValue(FP);
    SDValue Add = DAG.getNode(ISD::ADD, sdl, PtrVT, FPVal, OffsetVal);
    setValue(&I, Add);

    return nullptr;
  }

  case Intrinsic::eh_exceptionpointer:
  case Intrinsic::eh_exceptioncode: {
    // Get the exception pointer vreg, copy from it, and resize it to fit.
    const auto *CPI = cast<CatchPadInst>(I.getArgOperand(0));
    MVT PtrVT = TLI.getPointerTy(DAG.getDataLayout());
    const TargetRegisterClass *PtrRC = TLI.getRegClassFor(PtrVT);
    unsigned VReg = FuncInfo.getCatchPadExceptionPointerVReg(CPI, PtrRC);
    SDValue N =
        DAG.getCopyFromReg(DAG.getEntryNode(), getCurSDLoc(), VReg, PtrVT);
    if (Intrinsic == Intrinsic::eh_exceptioncode)
      N = DAG.getZExtOrTrunc(N, getCurSDLoc(), MVT::i32);
    setValue(&I, N);
    return nullptr;
  }
  case Intrinsic::xray_customevent: {
    // Here we want to make sure that the intrinsic behaves as if it has a
    // specific calling convention, and only for x86_64.
    // FIXME: Support other platforms later.
    const auto &Triple = DAG.getTarget().getTargetTriple();
    if (Triple.getArch() != Triple::x86_64 || !Triple.isOSLinux())
      return nullptr;

    SDLoc DL = getCurSDLoc();
    SmallVector<SDValue, 8> Ops;

    // We want to say that we always want the arguments in registers.
    SDValue LogEntryVal = getValue(I.getArgOperand(0));
    SDValue StrSizeVal = getValue(I.getArgOperand(1));
    SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
    SDValue Chain = getRoot();
    Ops.push_back(LogEntryVal);
    Ops.push_back(StrSizeVal);
    Ops.push_back(Chain);

    // We need to enforce the calling convention for the callsite, so that
    // argument ordering is enforced correctly, and that register allocation can
    // see that some registers may be assumed clobbered and have to preserve
    // them across calls to the intrinsic.
    MachineSDNode *MN = DAG.getMachineNode(TargetOpcode::PATCHABLE_EVENT_CALL,
                                           DL, NodeTys, Ops);
    SDValue patchableNode = SDValue(MN, 0);
    DAG.setRoot(patchableNode);
    setValue(&I, patchableNode);
    return nullptr;
  }
  case Intrinsic::xray_typedevent: {
    // Here we want to make sure that the intrinsic behaves as if it has a
    // specific calling convention, and only for x86_64.
    // FIXME: Support other platforms later.
    const auto &Triple = DAG.getTarget().getTargetTriple();
    if (Triple.getArch() != Triple::x86_64 || !Triple.isOSLinux())
      return nullptr;

    SDLoc DL = getCurSDLoc();
    SmallVector<SDValue, 8> Ops;

    // We want to say that we always want the arguments in registers.
    // It's unclear to me how manipulating the selection DAG here forces callers
    // to provide arguments in registers instead of on the stack.
    SDValue LogTypeId = getValue(I.getArgOperand(0));
    SDValue LogEntryVal = getValue(I.getArgOperand(1));
    SDValue StrSizeVal = getValue(I.getArgOperand(2));
    SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
    SDValue Chain = getRoot();
    Ops.push_back(LogTypeId);
    Ops.push_back(LogEntryVal);
    Ops.push_back(StrSizeVal);
    Ops.push_back(Chain);

    // We need to enforce the calling convention for the callsite, so that
    // argument ordering is enforced correctly, and that register allocation can
    // see that some registers may be assumed clobbered and have to preserve
    // them across calls to the intrinsic.
    MachineSDNode *MN = DAG.getMachineNode(
        TargetOpcode::PATCHABLE_TYPED_EVENT_CALL, DL, NodeTys, Ops);
    SDValue patchableNode = SDValue(MN, 0);
    DAG.setRoot(patchableNode);
    setValue(&I, patchableNode);
    return nullptr;
  }
  case Intrinsic::experimental_deoptimize:
    LowerDeoptimizeCall(&I);
    return nullptr;

  case Intrinsic::experimental_vector_reduce_fadd:
  case Intrinsic::experimental_vector_reduce_fmul:
  case Intrinsic::experimental_vector_reduce_add:
  case Intrinsic::experimental_vector_reduce_mul:
  case Intrinsic::experimental_vector_reduce_and:
  case Intrinsic::experimental_vector_reduce_or:
  case Intrinsic::experimental_vector_reduce_xor:
  case Intrinsic::experimental_vector_reduce_smax:
  case Intrinsic::experimental_vector_reduce_smin:
  case Intrinsic::experimental_vector_reduce_umax:
  case Intrinsic::experimental_vector_reduce_umin:
  case Intrinsic::experimental_vector_reduce_fmax:
  case Intrinsic::experimental_vector_reduce_fmin:
    visitVectorReduce(I, Intrinsic);
    return nullptr;

  case Intrinsic::icall_branch_funnel: {
    SmallVector<SDValue, 16> Ops;
    Ops.push_back(DAG.getRoot());
    Ops.push_back(getValue(I.getArgOperand(0)));

    int64_t Offset;
    auto *Base = dyn_cast<GlobalObject>(GetPointerBaseWithConstantOffset(
        I.getArgOperand(1), Offset, DAG.getDataLayout()));
    if (!Base)
      report_fatal_error(
          "llvm.icall.branch.funnel operand must be a GlobalValue");
    Ops.push_back(DAG.getTargetGlobalAddress(Base, getCurSDLoc(), MVT::i64, 0));

    struct BranchFunnelTarget {
      int64_t Offset;
      SDValue Target;
    };
    SmallVector<BranchFunnelTarget, 8> Targets;

    for (unsigned Op = 1, N = I.getNumArgOperands(); Op != N; Op += 2) {
      auto *ElemBase = dyn_cast<GlobalObject>(GetPointerBaseWithConstantOffset(
          I.getArgOperand(Op), Offset, DAG.getDataLayout()));
      if (ElemBase != Base)
        report_fatal_error("all llvm.icall.branch.funnel operands must refer "
                           "to the same GlobalValue");

      SDValue Val = getValue(I.getArgOperand(Op + 1));
      auto *GA = dyn_cast<GlobalAddressSDNode>(Val);
      if (!GA)
        report_fatal_error(
            "llvm.icall.branch.funnel operand must be a GlobalValue");
      Targets.push_back({Offset, DAG.getTargetGlobalAddress(
                                     GA->getGlobal(), getCurSDLoc(),
                                     Val.getValueType(), GA->getOffset())});
    }
    llvm::sort(Targets,
               [](const BranchFunnelTarget &T1, const BranchFunnelTarget &T2) {
                 return T1.Offset < T2.Offset;
               });

    for (auto &T : Targets) {
      Ops.push_back(DAG.getTargetConstant(T.Offset, getCurSDLoc(), MVT::i32));
      Ops.push_back(T.Target);
    }

    SDValue N(DAG.getMachineNode(TargetOpcode::ICALL_BRANCH_FUNNEL,
                                 getCurSDLoc(), MVT::Other, Ops),
              0);
    DAG.setRoot(N);
    setValue(&I, N);
    HasTailCall = true;
    return nullptr;
  }

  case Intrinsic::wasm_landingpad_index:
    // Information this intrinsic contained has been transferred to
    // MachineFunction in SelectionDAGISel::PrepareEHLandingPad. We can safely
    // delete it now.
    return nullptr;
  }
}

void SelectionDAGBuilder::visitConstrainedFPIntrinsic(
    const ConstrainedFPIntrinsic &FPI) {
  SDLoc sdl = getCurSDLoc();
  unsigned Opcode;
  switch (FPI.getIntrinsicID()) {
  default: llvm_unreachable("Impossible intrinsic");  // Can't reach here.
  case Intrinsic::experimental_constrained_fadd:
    Opcode = ISD::STRICT_FADD;
    break;
  case Intrinsic::experimental_constrained_fsub:
    Opcode = ISD::STRICT_FSUB;
    break;
  case Intrinsic::experimental_constrained_fmul:
    Opcode = ISD::STRICT_FMUL;
    break;
  case Intrinsic::experimental_constrained_fdiv:
    Opcode = ISD::STRICT_FDIV;
    break;
  case Intrinsic::experimental_constrained_frem:
    Opcode = ISD::STRICT_FREM;
    break;
  case Intrinsic::experimental_constrained_fma:
    Opcode = ISD::STRICT_FMA;
    break;
  case Intrinsic::experimental_constrained_sqrt:
    Opcode = ISD::STRICT_FSQRT;
    break;
  case Intrinsic::experimental_constrained_pow:
    Opcode = ISD::STRICT_FPOW;
    break;
  case Intrinsic::experimental_constrained_powi:
    Opcode = ISD::STRICT_FPOWI;
    break;
  case Intrinsic::experimental_constrained_sin:
    Opcode = ISD::STRICT_FSIN;
    break;
  case Intrinsic::experimental_constrained_cos:
    Opcode = ISD::STRICT_FCOS;
    break;
  case Intrinsic::experimental_constrained_exp:
    Opcode = ISD::STRICT_FEXP;
    break;
  case Intrinsic::experimental_constrained_exp2:
    Opcode = ISD::STRICT_FEXP2;
    break;
  case Intrinsic::experimental_constrained_log:
    Opcode = ISD::STRICT_FLOG;
    break;
  case Intrinsic::experimental_constrained_log10:
    Opcode = ISD::STRICT_FLOG10;
    break;
  case Intrinsic::experimental_constrained_log2:
    Opcode = ISD::STRICT_FLOG2;
    break;
  case Intrinsic::experimental_constrained_rint:
    Opcode = ISD::STRICT_FRINT;
    break;
  case Intrinsic::experimental_constrained_nearbyint:
    Opcode = ISD::STRICT_FNEARBYINT;
    break;
  case Intrinsic::experimental_constrained_maxnum:
    Opcode = ISD::STRICT_FMAXNUM;
    break;
  case Intrinsic::experimental_constrained_minnum:
    Opcode = ISD::STRICT_FMINNUM;
    break;
  case Intrinsic::experimental_constrained_ceil:
    Opcode = ISD::STRICT_FCEIL;
    break;
  case Intrinsic::experimental_constrained_floor:
    Opcode = ISD::STRICT_FFLOOR;
    break;
  case Intrinsic::experimental_constrained_round:
    Opcode = ISD::STRICT_FROUND;
    break;
  case Intrinsic::experimental_constrained_trunc:
    Opcode = ISD::STRICT_FTRUNC;
    break;
  }
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  SDValue Chain = getRoot();
  SmallVector<EVT, 4> ValueVTs;
  ComputeValueVTs(TLI, DAG.getDataLayout(), FPI.getType(), ValueVTs);
  ValueVTs.push_back(MVT::Other); // Out chain

  SDVTList VTs = DAG.getVTList(ValueVTs);
  SDValue Result;
  if (FPI.isUnaryOp())
    Result = DAG.getNode(Opcode, sdl, VTs,
                         { Chain, getValue(FPI.getArgOperand(0)) });
  else if (FPI.isTernaryOp())
    Result = DAG.getNode(Opcode, sdl, VTs,
                         { Chain, getValue(FPI.getArgOperand(0)),
                                  getValue(FPI.getArgOperand(1)),
                                  getValue(FPI.getArgOperand(2)) });
  else
    Result = DAG.getNode(Opcode, sdl, VTs,
                         { Chain, getValue(FPI.getArgOperand(0)),
                           getValue(FPI.getArgOperand(1))  });

  assert(Result.getNode()->getNumValues() == 2);
  SDValue OutChain = Result.getValue(1);
  DAG.setRoot(OutChain);
  SDValue FPResult = Result.getValue(0);
  setValue(&FPI, FPResult);
}

std::pair<SDValue, SDValue>
SelectionDAGBuilder::lowerInvokable(TargetLowering::CallLoweringInfo &CLI,
                                    const BasicBlock *EHPadBB) {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineModuleInfo &MMI = MF.getMMI();
  MCSymbol *BeginLabel = nullptr;

  if (EHPadBB) {
    // Insert a label before the invoke call to mark the try range.  This can be
    // used to detect deletion of the invoke via the MachineModuleInfo.
    BeginLabel = MMI.getContext().createTempSymbol();

    // For SjLj, keep track of which landing pads go with which invokes
    // so as to maintain the ordering of pads in the LSDA.
    unsigned CallSiteIndex = MMI.getCurrentCallSite();
    if (CallSiteIndex) {
      MF.setCallSiteBeginLabel(BeginLabel, CallSiteIndex);
      LPadToCallSiteMap[FuncInfo.MBBMap[EHPadBB]].push_back(CallSiteIndex);

      // Now that the call site is handled, stop tracking it.
      MMI.setCurrentCallSite(0);
    }

    // Both PendingLoads and PendingExports must be flushed here;
    // this call might not return.
    (void)getRoot();
    DAG.setRoot(DAG.getEHLabel(getCurSDLoc(), getControlRoot(), BeginLabel));

    CLI.setChain(getRoot());
  }
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  std::pair<SDValue, SDValue> Result = TLI.LowerCallTo(CLI);

  assert((CLI.IsTailCall || Result.second.getNode()) &&
         "Non-null chain expected with non-tail call!");
  assert((Result.second.getNode() || !Result.first.getNode()) &&
         "Null value expected with tail call!");

  if (!Result.second.getNode()) {
    // As a special case, a null chain means that a tail call has been emitted
    // and the DAG root is already updated.
    HasTailCall = true;

    // Since there's no actual continuation from this block, nothing can be
    // relying on us setting vregs for them.
    PendingExports.clear();
  } else {
    DAG.setRoot(Result.second);
  }

  if (EHPadBB) {
    // Insert a label at the end of the invoke call to mark the try range.  This
    // can be used to detect deletion of the invoke via the MachineModuleInfo.
    MCSymbol *EndLabel = MMI.getContext().createTempSymbol();
    DAG.setRoot(DAG.getEHLabel(getCurSDLoc(), getRoot(), EndLabel));

    // Inform MachineModuleInfo of range.
    auto Pers = classifyEHPersonality(FuncInfo.Fn->getPersonalityFn());
    // There is a platform (e.g. wasm) that uses funclet style IR but does not
    // actually use outlined funclets and their LSDA info style.
    if (MF.hasEHFunclets() && isFuncletEHPersonality(Pers)) {
      assert(CLI.CS);
      WinEHFuncInfo *EHInfo = DAG.getMachineFunction().getWinEHFuncInfo();
      EHInfo->addIPToStateRange(cast<InvokeInst>(CLI.CS.getInstruction()),
                                BeginLabel, EndLabel);
    } else if (!isScopedEHPersonality(Pers)) {
      MF.addInvoke(FuncInfo.MBBMap[EHPadBB], BeginLabel, EndLabel);
    }
  }

  return Result;
}

void SelectionDAGBuilder::LowerCallTo(ImmutableCallSite CS, SDValue Callee,
                                      bool isTailCall,
                                      const BasicBlock *EHPadBB) {
  auto &DL = DAG.getDataLayout();
  FunctionType *FTy = CS.getFunctionType();
  Type *RetTy = CS.getType();

  TargetLowering::ArgListTy Args;
  Args.reserve(CS.arg_size());

  const Value *SwiftErrorVal = nullptr;
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();

  // We can't tail call inside a function with a swifterror argument. Lowering
  // does not support this yet. It would have to move into the swifterror
  // register before the call.
  auto *Caller = CS.getInstruction()->getParent()->getParent();
  if (TLI.supportSwiftError() &&
      Caller->getAttributes().hasAttrSomewhere(Attribute::SwiftError))
    isTailCall = false;

  for (ImmutableCallSite::arg_iterator i = CS.arg_begin(), e = CS.arg_end();
       i != e; ++i) {
    TargetLowering::ArgListEntry Entry;
    const Value *V = *i;

    // Skip empty types
    if (V->getType()->isEmptyTy())
      continue;

    SDValue ArgNode = getValue(V);
    Entry.Node = ArgNode; Entry.Ty = V->getType();

    Entry.setAttributes(&CS, i - CS.arg_begin());

    // Use swifterror virtual register as input to the call.
    if (Entry.IsSwiftError && TLI.supportSwiftError()) {
      SwiftErrorVal = V;
      // We find the virtual register for the actual swifterror argument.
      // Instead of using the Value, we use the virtual register instead.
      Entry.Node = DAG.getRegister(FuncInfo
                                       .getOrCreateSwiftErrorVRegUseAt(
                                           CS.getInstruction(), FuncInfo.MBB, V)
                                       .first,
                                   EVT(TLI.getPointerTy(DL)));
    }

    Args.push_back(Entry);

    // If we have an explicit sret argument that is an Instruction, (i.e., it
    // might point to function-local memory), we can't meaningfully tail-call.
    if (Entry.IsSRet && isa<Instruction>(V))
      isTailCall = false;
  }

  // Check if target-independent constraints permit a tail call here.
  // Target-dependent constraints are checked within TLI->LowerCallTo.
  if (isTailCall && !isInTailCallPosition(CS, DAG.getTarget()))
    isTailCall = false;

  // Disable tail calls if there is an swifterror argument. Targets have not
  // been updated to support tail calls.
  if (TLI.supportSwiftError() && SwiftErrorVal)
    isTailCall = false;

  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(getCurSDLoc())
      .setChain(getRoot())
      .setCallee(RetTy, FTy, Callee, std::move(Args), CS)
      .setTailCall(isTailCall)
      .setConvergent(CS.isConvergent());
  std::pair<SDValue, SDValue> Result = lowerInvokable(CLI, EHPadBB);

  if (Result.first.getNode()) {
    const Instruction *Inst = CS.getInstruction();
    Result.first = lowerRangeToAssertZExt(DAG, *Inst, Result.first);
    setValue(Inst, Result.first);
  }

  // The last element of CLI.InVals has the SDValue for swifterror return.
  // Here we copy it to a virtual register and update SwiftErrorMap for
  // book-keeping.
  if (SwiftErrorVal && TLI.supportSwiftError()) {
    // Get the last element of InVals.
    SDValue Src = CLI.InVals.back();
    unsigned VReg; bool CreatedVReg;
    std::tie(VReg, CreatedVReg) =
        FuncInfo.getOrCreateSwiftErrorVRegDefAt(CS.getInstruction());
    SDValue CopyNode = CLI.DAG.getCopyToReg(Result.second, CLI.DL, VReg, Src);
    // We update the virtual register for the actual swifterror argument.
    if (CreatedVReg)
      FuncInfo.setCurrentSwiftErrorVReg(FuncInfo.MBB, SwiftErrorVal, VReg);
    DAG.setRoot(CopyNode);
  }
}

static SDValue getMemCmpLoad(const Value *PtrVal, MVT LoadVT,
                             SelectionDAGBuilder &Builder) {
  // Check to see if this load can be trivially constant folded, e.g. if the
  // input is from a string literal.
  if (const Constant *LoadInput = dyn_cast<Constant>(PtrVal)) {
    // Cast pointer to the type we really want to load.
    Type *LoadTy =
        Type::getIntNTy(PtrVal->getContext(), LoadVT.getScalarSizeInBits());
    if (LoadVT.isVector())
      LoadTy = VectorType::get(LoadTy, LoadVT.getVectorNumElements());

    LoadInput = ConstantExpr::getBitCast(const_cast<Constant *>(LoadInput),
                                         PointerType::getUnqual(LoadTy));

    if (const Constant *LoadCst = ConstantFoldLoadFromConstPtr(
            const_cast<Constant *>(LoadInput), LoadTy, *Builder.DL))
      return Builder.getValue(LoadCst);
  }

  // Otherwise, we have to emit the load.  If the pointer is to unfoldable but
  // still constant memory, the input chain can be the entry node.
  SDValue Root;
  bool ConstantMemory = false;

  // Do not serialize (non-volatile) loads of constant memory with anything.
  if (Builder.AA && Builder.AA->pointsToConstantMemory(PtrVal)) {
    Root = Builder.DAG.getEntryNode();
    ConstantMemory = true;
  } else {
    // Do not serialize non-volatile loads against each other.
    Root = Builder.DAG.getRoot();
  }

  SDValue Ptr = Builder.getValue(PtrVal);
  SDValue LoadVal = Builder.DAG.getLoad(LoadVT, Builder.getCurSDLoc(), Root,
                                        Ptr, MachinePointerInfo(PtrVal),
                                        /* Alignment = */ 1);

  if (!ConstantMemory)
    Builder.PendingLoads.push_back(LoadVal.getValue(1));
  return LoadVal;
}

/// Record the value for an instruction that produces an integer result,
/// converting the type where necessary.
void SelectionDAGBuilder::processIntegerCallValue(const Instruction &I,
                                                  SDValue Value,
                                                  bool IsSigned) {
  EVT VT = DAG.getTargetLoweringInfo().getValueType(DAG.getDataLayout(),
                                                    I.getType(), true);
  if (IsSigned)
    Value = DAG.getSExtOrTrunc(Value, getCurSDLoc(), VT);
  else
    Value = DAG.getZExtOrTrunc(Value, getCurSDLoc(), VT);
  setValue(&I, Value);
}

/// See if we can lower a memcmp call into an optimized form. If so, return
/// true and lower it. Otherwise return false, and it will be lowered like a
/// normal call.
/// The caller already checked that \p I calls the appropriate LibFunc with a
/// correct prototype.
bool SelectionDAGBuilder::visitMemCmpCall(const CallInst &I) {
  const Value *LHS = I.getArgOperand(0), *RHS = I.getArgOperand(1);
  const Value *Size = I.getArgOperand(2);
  const ConstantInt *CSize = dyn_cast<ConstantInt>(Size);
  if (CSize && CSize->getZExtValue() == 0) {
    EVT CallVT = DAG.getTargetLoweringInfo().getValueType(DAG.getDataLayout(),
                                                          I.getType(), true);
    setValue(&I, DAG.getConstant(0, getCurSDLoc(), CallVT));
    return true;
  }

  const SelectionDAGTargetInfo &TSI = DAG.getSelectionDAGInfo();
  std::pair<SDValue, SDValue> Res = TSI.EmitTargetCodeForMemcmp(
      DAG, getCurSDLoc(), DAG.getRoot(), getValue(LHS), getValue(RHS),
      getValue(Size), MachinePointerInfo(LHS), MachinePointerInfo(RHS));
  if (Res.first.getNode()) {
    processIntegerCallValue(I, Res.first, true);
    PendingLoads.push_back(Res.second);
    return true;
  }

  // memcmp(S1,S2,2) != 0 -> (*(short*)LHS != *(short*)RHS)  != 0
  // memcmp(S1,S2,4) != 0 -> (*(int*)LHS != *(int*)RHS)  != 0
  if (!CSize || !isOnlyUsedInZeroEqualityComparison(&I))
    return false;

  // If the target has a fast compare for the given size, it will return a
  // preferred load type for that size. Require that the load VT is legal and
  // that the target supports unaligned loads of that type. Otherwise, return
  // INVALID.
  auto hasFastLoadsAndCompare = [&](unsigned NumBits) {
    const TargetLowering &TLI = DAG.getTargetLoweringInfo();
    MVT LVT = TLI.hasFastEqualityCompare(NumBits);
    if (LVT != MVT::INVALID_SIMPLE_VALUE_TYPE) {
      // TODO: Handle 5 byte compare as 4-byte + 1 byte.
      // TODO: Handle 8 byte compare on x86-32 as two 32-bit loads.
      // TODO: Check alignment of src and dest ptrs.
      unsigned DstAS = LHS->getType()->getPointerAddressSpace();
      unsigned SrcAS = RHS->getType()->getPointerAddressSpace();
      if (!TLI.isTypeLegal(LVT) ||
          !TLI.allowsMisalignedMemoryAccesses(LVT, SrcAS) ||
          !TLI.allowsMisalignedMemoryAccesses(LVT, DstAS))
        LVT = MVT::INVALID_SIMPLE_VALUE_TYPE;
    }

    return LVT;
  };

  // This turns into unaligned loads. We only do this if the target natively
  // supports the MVT we'll be loading or if it is small enough (<= 4) that
  // we'll only produce a small number of byte loads.
  MVT LoadVT;
  unsigned NumBitsToCompare = CSize->getZExtValue() * 8;
  switch (NumBitsToCompare) {
  default:
    return false;
  case 16:
    LoadVT = MVT::i16;
    break;
  case 32:
    LoadVT = MVT::i32;
    break;
  case 64:
  case 128:
  case 256:
    LoadVT = hasFastLoadsAndCompare(NumBitsToCompare);
    break;
  }

  if (LoadVT == MVT::INVALID_SIMPLE_VALUE_TYPE)
    return false;

  SDValue LoadL = getMemCmpLoad(LHS, LoadVT, *this);
  SDValue LoadR = getMemCmpLoad(RHS, LoadVT, *this);

  // Bitcast to a wide integer type if the loads are vectors.
  if (LoadVT.isVector()) {
    EVT CmpVT = EVT::getIntegerVT(LHS->getContext(), LoadVT.getSizeInBits());
    LoadL = DAG.getBitcast(CmpVT, LoadL);
    LoadR = DAG.getBitcast(CmpVT, LoadR);
  }

  SDValue Cmp = DAG.getSetCC(getCurSDLoc(), MVT::i1, LoadL, LoadR, ISD::SETNE);
  processIntegerCallValue(I, Cmp, false);
  return true;
}

/// See if we can lower a memchr call into an optimized form. If so, return
/// true and lower it. Otherwise return false, and it will be lowered like a
/// normal call.
/// The caller already checked that \p I calls the appropriate LibFunc with a
/// correct prototype.
bool SelectionDAGBuilder::visitMemChrCall(const CallInst &I) {
  const Value *Src = I.getArgOperand(0);
  const Value *Char = I.getArgOperand(1);
  const Value *Length = I.getArgOperand(2);

  const SelectionDAGTargetInfo &TSI = DAG.getSelectionDAGInfo();
  std::pair<SDValue, SDValue> Res =
    TSI.EmitTargetCodeForMemchr(DAG, getCurSDLoc(), DAG.getRoot(),
                                getValue(Src), getValue(Char), getValue(Length),
                                MachinePointerInfo(Src));
  if (Res.first.getNode()) {
    setValue(&I, Res.first);
    PendingLoads.push_back(Res.second);
    return true;
  }

  return false;
}

/// See if we can lower a mempcpy call into an optimized form. If so, return
/// true and lower it. Otherwise return false, and it will be lowered like a
/// normal call.
/// The caller already checked that \p I calls the appropriate LibFunc with a
/// correct prototype.
bool SelectionDAGBuilder::visitMemPCpyCall(const CallInst &I) {
  SDValue Dst = getValue(I.getArgOperand(0));
  SDValue Src = getValue(I.getArgOperand(1));
  SDValue Size = getValue(I.getArgOperand(2));

  unsigned DstAlign = DAG.InferPtrAlignment(Dst);
  unsigned SrcAlign = DAG.InferPtrAlignment(Src);
  unsigned Align = std::min(DstAlign, SrcAlign);
  if (Align == 0) // Alignment of one or both could not be inferred.
    Align = 1; // 0 and 1 both specify no alignment, but 0 is reserved.

  bool isVol = false;
  SDLoc sdl = getCurSDLoc();

  // In the mempcpy context we need to pass in a false value for isTailCall
  // because the return pointer needs to be adjusted by the size of
  // the copied memory.
  SDValue MC = DAG.getMemcpy(getRoot(), sdl, Dst, Src, Size, Align, isVol,
                             false, /*isTailCall=*/false,
                             MachinePointerInfo(I.getArgOperand(0)),
                             MachinePointerInfo(I.getArgOperand(1)));
  assert(MC.getNode() != nullptr &&
         "** memcpy should not be lowered as TailCall in mempcpy context **");
  DAG.setRoot(MC);

  // Check if Size needs to be truncated or extended.
  Size = DAG.getSExtOrTrunc(Size, sdl, Dst.getValueType());

  // Adjust return pointer to point just past the last dst byte.
  SDValue DstPlusSize = DAG.getNode(ISD::ADD, sdl, Dst.getValueType(),
                                    Dst, Size);
  setValue(&I, DstPlusSize);
  return true;
}

/// See if we can lower a strcpy call into an optimized form.  If so, return
/// true and lower it, otherwise return false and it will be lowered like a
/// normal call.
/// The caller already checked that \p I calls the appropriate LibFunc with a
/// correct prototype.
bool SelectionDAGBuilder::visitStrCpyCall(const CallInst &I, bool isStpcpy) {
  const Value *Arg0 = I.getArgOperand(0), *Arg1 = I.getArgOperand(1);

  const SelectionDAGTargetInfo &TSI = DAG.getSelectionDAGInfo();
  std::pair<SDValue, SDValue> Res =
    TSI.EmitTargetCodeForStrcpy(DAG, getCurSDLoc(), getRoot(),
                                getValue(Arg0), getValue(Arg1),
                                MachinePointerInfo(Arg0),
                                MachinePointerInfo(Arg1), isStpcpy);
  if (Res.first.getNode()) {
    setValue(&I, Res.first);
    DAG.setRoot(Res.second);
    return true;
  }

  return false;
}

/// See if we can lower a strcmp call into an optimized form.  If so, return
/// true and lower it, otherwise return false and it will be lowered like a
/// normal call.
/// The caller already checked that \p I calls the appropriate LibFunc with a
/// correct prototype.
bool SelectionDAGBuilder::visitStrCmpCall(const CallInst &I) {
  const Value *Arg0 = I.getArgOperand(0), *Arg1 = I.getArgOperand(1);

  const SelectionDAGTargetInfo &TSI = DAG.getSelectionDAGInfo();
  std::pair<SDValue, SDValue> Res =
    TSI.EmitTargetCodeForStrcmp(DAG, getCurSDLoc(), DAG.getRoot(),
                                getValue(Arg0), getValue(Arg1),
                                MachinePointerInfo(Arg0),
                                MachinePointerInfo(Arg1));
  if (Res.first.getNode()) {
    processIntegerCallValue(I, Res.first, true);
    PendingLoads.push_back(Res.second);
    return true;
  }

  return false;
}

/// See if we can lower a strlen call into an optimized form.  If so, return
/// true and lower it, otherwise return false and it will be lowered like a
/// normal call.
/// The caller already checked that \p I calls the appropriate LibFunc with a
/// correct prototype.
bool SelectionDAGBuilder::visitStrLenCall(const CallInst &I) {
  const Value *Arg0 = I.getArgOperand(0);

  const SelectionDAGTargetInfo &TSI = DAG.getSelectionDAGInfo();
  std::pair<SDValue, SDValue> Res =
    TSI.EmitTargetCodeForStrlen(DAG, getCurSDLoc(), DAG.getRoot(),
                                getValue(Arg0), MachinePointerInfo(Arg0));
  if (Res.first.getNode()) {
    processIntegerCallValue(I, Res.first, false);
    PendingLoads.push_back(Res.second);
    return true;
  }

  return false;
}

/// See if we can lower a strnlen call into an optimized form.  If so, return
/// true and lower it, otherwise return false and it will be lowered like a
/// normal call.
/// The caller already checked that \p I calls the appropriate LibFunc with a
/// correct prototype.
bool SelectionDAGBuilder::visitStrNLenCall(const CallInst &I) {
  const Value *Arg0 = I.getArgOperand(0), *Arg1 = I.getArgOperand(1);

  const SelectionDAGTargetInfo &TSI = DAG.getSelectionDAGInfo();
  std::pair<SDValue, SDValue> Res =
    TSI.EmitTargetCodeForStrnlen(DAG, getCurSDLoc(), DAG.getRoot(),
                                 getValue(Arg0), getValue(Arg1),
                                 MachinePointerInfo(Arg0));
  if (Res.first.getNode()) {
    processIntegerCallValue(I, Res.first, false);
    PendingLoads.push_back(Res.second);
    return true;
  }

  return false;
}

/// See if we can lower a unary floating-point operation into an SDNode with
/// the specified Opcode.  If so, return true and lower it, otherwise return
/// false and it will be lowered like a normal call.
/// The caller already checked that \p I calls the appropriate LibFunc with a
/// correct prototype.
bool SelectionDAGBuilder::visitUnaryFloatCall(const CallInst &I,
                                              unsigned Opcode) {
  // We already checked this call's prototype; verify it doesn't modify errno.
  if (!I.onlyReadsMemory())
    return false;

  SDValue Tmp = getValue(I.getArgOperand(0));
  setValue(&I, DAG.getNode(Opcode, getCurSDLoc(), Tmp.getValueType(), Tmp));
  return true;
}

/// See if we can lower a binary floating-point operation into an SDNode with
/// the specified Opcode. If so, return true and lower it. Otherwise return
/// false, and it will be lowered like a normal call.
/// The caller already checked that \p I calls the appropriate LibFunc with a
/// correct prototype.
bool SelectionDAGBuilder::visitBinaryFloatCall(const CallInst &I,
                                               unsigned Opcode) {
  // We already checked this call's prototype; verify it doesn't modify errno.
  if (!I.onlyReadsMemory())
    return false;

  SDValue Tmp0 = getValue(I.getArgOperand(0));
  SDValue Tmp1 = getValue(I.getArgOperand(1));
  EVT VT = Tmp0.getValueType();
  setValue(&I, DAG.getNode(Opcode, getCurSDLoc(), VT, Tmp0, Tmp1));
  return true;
}

void SelectionDAGBuilder::visitCall(const CallInst &I) {
  // Handle inline assembly differently.
  if (isa<InlineAsm>(I.getCalledValue())) {
    visitInlineAsm(&I);
    return;
  }

  MachineModuleInfo &MMI = DAG.getMachineFunction().getMMI();
  computeUsesVAFloatArgument(I, MMI);

  const char *RenameFn = nullptr;
  if (Function *F = I.getCalledFunction()) {
    if (F->isDeclaration()) {
      // Is this an LLVM intrinsic or a target-specific intrinsic?
      unsigned IID = F->getIntrinsicID();
      if (!IID)
        if (const TargetIntrinsicInfo *II = TM.getIntrinsicInfo())
          IID = II->getIntrinsicID(F);

      if (IID) {
        RenameFn = visitIntrinsicCall(I, IID);
        if (!RenameFn)
          return;
      }
    }

    // Check for well-known libc/libm calls.  If the function is internal, it
    // can't be a library call.  Don't do the check if marked as nobuiltin for
    // some reason or the call site requires strict floating point semantics.
    LibFunc Func;
    if (!I.isNoBuiltin() && !I.isStrictFP() && !F->hasLocalLinkage() &&
        F->hasName() && LibInfo->getLibFunc(*F, Func) &&
        LibInfo->hasOptimizedCodeGen(Func)) {
      switch (Func) {
      default: break;
      case LibFunc_copysign:
      case LibFunc_copysignf:
      case LibFunc_copysignl:
        // We already checked this call's prototype; verify it doesn't modify
        // errno.
        if (I.onlyReadsMemory()) {
          SDValue LHS = getValue(I.getArgOperand(0));
          SDValue RHS = getValue(I.getArgOperand(1));
          setValue(&I, DAG.getNode(ISD::FCOPYSIGN, getCurSDLoc(),
                                   LHS.getValueType(), LHS, RHS));
          return;
        }
        break;
      case LibFunc_fabs:
      case LibFunc_fabsf:
      case LibFunc_fabsl:
        if (visitUnaryFloatCall(I, ISD::FABS))
          return;
        break;
      case LibFunc_fmin:
      case LibFunc_fminf:
      case LibFunc_fminl:
        if (visitBinaryFloatCall(I, ISD::FMINNUM))
          return;
        break;
      case LibFunc_fmax:
      case LibFunc_fmaxf:
      case LibFunc_fmaxl:
        if (visitBinaryFloatCall(I, ISD::FMAXNUM))
          return;
        break;
      case LibFunc_sin:
      case LibFunc_sinf:
      case LibFunc_sinl:
        if (visitUnaryFloatCall(I, ISD::FSIN))
          return;
        break;
      case LibFunc_cos:
      case LibFunc_cosf:
      case LibFunc_cosl:
        if (visitUnaryFloatCall(I, ISD::FCOS))
          return;
        break;
      case LibFunc_sqrt:
      case LibFunc_sqrtf:
      case LibFunc_sqrtl:
      case LibFunc_sqrt_finite:
      case LibFunc_sqrtf_finite:
      case LibFunc_sqrtl_finite:
        if (visitUnaryFloatCall(I, ISD::FSQRT))
          return;
        break;
      case LibFunc_floor:
      case LibFunc_floorf:
      case LibFunc_floorl:
        if (visitUnaryFloatCall(I, ISD::FFLOOR))
          return;
        break;
      case LibFunc_nearbyint:
      case LibFunc_nearbyintf:
      case LibFunc_nearbyintl:
        if (visitUnaryFloatCall(I, ISD::FNEARBYINT))
          return;
        break;
      case LibFunc_ceil:
      case LibFunc_ceilf:
      case LibFunc_ceill:
        if (visitUnaryFloatCall(I, ISD::FCEIL))
          return;
        break;
      case LibFunc_rint:
      case LibFunc_rintf:
      case LibFunc_rintl:
        if (visitUnaryFloatCall(I, ISD::FRINT))
          return;
        break;
      case LibFunc_round:
      case LibFunc_roundf:
      case LibFunc_roundl:
        if (visitUnaryFloatCall(I, ISD::FROUND))
          return;
        break;
      case LibFunc_trunc:
      case LibFunc_truncf:
      case LibFunc_truncl:
        if (visitUnaryFloatCall(I, ISD::FTRUNC))
          return;
        break;
      case LibFunc_log2:
      case LibFunc_log2f:
      case LibFunc_log2l:
        if (visitUnaryFloatCall(I, ISD::FLOG2))
          return;
        break;
      case LibFunc_exp2:
      case LibFunc_exp2f:
      case LibFunc_exp2l:
        if (visitUnaryFloatCall(I, ISD::FEXP2))
          return;
        break;
      case LibFunc_memcmp:
        if (visitMemCmpCall(I))
          return;
        break;
      case LibFunc_mempcpy:
        if (visitMemPCpyCall(I))
          return;
        break;
      case LibFunc_memchr:
        if (visitMemChrCall(I))
          return;
        break;
      case LibFunc_strcpy:
        if (visitStrCpyCall(I, false))
          return;
        break;
      case LibFunc_stpcpy:
        if (visitStrCpyCall(I, true))
          return;
        break;
      case LibFunc_strcmp:
        if (visitStrCmpCall(I))
          return;
        break;
      case LibFunc_strlen:
        if (visitStrLenCall(I))
          return;
        break;
      case LibFunc_strnlen:
        if (visitStrNLenCall(I))
          return;
        break;
      }
    }
  }

  SDValue Callee;
  if (!RenameFn)
    Callee = getValue(I.getCalledValue());
  else
    Callee = DAG.getExternalSymbol(
        RenameFn,
        DAG.getTargetLoweringInfo().getPointerTy(DAG.getDataLayout()));

  // Deopt bundles are lowered in LowerCallSiteWithDeoptBundle, and we don't
  // have to do anything here to lower funclet bundles.
  assert(!I.hasOperandBundlesOtherThan(
             {LLVMContext::OB_deopt, LLVMContext::OB_funclet}) &&
         "Cannot lower calls with arbitrary operand bundles!");

  if (I.countOperandBundlesOfType(LLVMContext::OB_deopt))
    LowerCallSiteWithDeoptBundle(&I, Callee, nullptr);
  else
    // Check if we can potentially perform a tail call. More detailed checking
    // is be done within LowerCallTo, after more information about the call is
    // known.
    LowerCallTo(&I, Callee, I.isTailCall());
}

namespace {

/// AsmOperandInfo - This contains information for each constraint that we are
/// lowering.
class SDISelAsmOperandInfo : public TargetLowering::AsmOperandInfo {
public:
  /// CallOperand - If this is the result output operand or a clobber
  /// this is null, otherwise it is the incoming operand to the CallInst.
  /// This gets modified as the asm is processed.
  SDValue CallOperand;

  /// AssignedRegs - If this is a register or register class operand, this
  /// contains the set of register corresponding to the operand.
  RegsForValue AssignedRegs;

  explicit SDISelAsmOperandInfo(const TargetLowering::AsmOperandInfo &info)
    : TargetLowering::AsmOperandInfo(info), CallOperand(nullptr, 0) {
  }

  /// Whether or not this operand accesses memory
  bool hasMemory(const TargetLowering &TLI) const {
    // Indirect operand accesses access memory.
    if (isIndirect)
      return true;

    for (const auto &Code : Codes)
      if (TLI.getConstraintType(Code) == TargetLowering::C_Memory)
        return true;

    return false;
  }

  /// getCallOperandValEVT - Return the EVT of the Value* that this operand
  /// corresponds to.  If there is no Value* for this operand, it returns
  /// MVT::Other.
  EVT getCallOperandValEVT(LLVMContext &Context, const TargetLowering &TLI,
                           const DataLayout &DL) const {
    if (!CallOperandVal) return MVT::Other;

    if (isa<BasicBlock>(CallOperandVal))
      return TLI.getPointerTy(DL);

    llvm::Type *OpTy = CallOperandVal->getType();

    // FIXME: code duplicated from TargetLowering::ParseConstraints().
    // If this is an indirect operand, the operand is a pointer to the
    // accessed type.
    if (isIndirect) {
      PointerType *PtrTy = dyn_cast<PointerType>(OpTy);
      if (!PtrTy)
        report_fatal_error("Indirect operand for inline asm not a pointer!");
      OpTy = PtrTy->getElementType();
    }

    // Look for vector wrapped in a struct. e.g. { <16 x i8> }.
    if (StructType *STy = dyn_cast<StructType>(OpTy))
      if (STy->getNumElements() == 1)
        OpTy = STy->getElementType(0);

    // If OpTy is not a single value, it may be a struct/union that we
    // can tile with integers.
    if (!OpTy->isSingleValueType() && OpTy->isSized()) {
      unsigned BitSize = DL.getTypeSizeInBits(OpTy);
      switch (BitSize) {
      default: break;
      case 1:
      case 8:
      case 16:
      case 32:
      case 64:
      case 128:
        OpTy = IntegerType::get(Context, BitSize);
        break;
      }
    }

    return TLI.getValueType(DL, OpTy, true);
  }
};

using SDISelAsmOperandInfoVector = SmallVector<SDISelAsmOperandInfo, 16>;

} // end anonymous namespace

/// Make sure that the output operand \p OpInfo and its corresponding input
/// operand \p MatchingOpInfo have compatible constraint types (otherwise error
/// out).
static void patchMatchingInput(const SDISelAsmOperandInfo &OpInfo,
                               SDISelAsmOperandInfo &MatchingOpInfo,
                               SelectionDAG &DAG) {
  if (OpInfo.ConstraintVT == MatchingOpInfo.ConstraintVT)
    return;

  const TargetRegisterInfo *TRI = DAG.getSubtarget().getRegisterInfo();
  const auto &TLI = DAG.getTargetLoweringInfo();

  std::pair<unsigned, const TargetRegisterClass *> MatchRC =
      TLI.getRegForInlineAsmConstraint(TRI, OpInfo.ConstraintCode,
                                       OpInfo.ConstraintVT);
  std::pair<unsigned, const TargetRegisterClass *> InputRC =
      TLI.getRegForInlineAsmConstraint(TRI, MatchingOpInfo.ConstraintCode,
                                       MatchingOpInfo.ConstraintVT);
  if ((OpInfo.ConstraintVT.isInteger() !=
       MatchingOpInfo.ConstraintVT.isInteger()) ||
      (MatchRC.second != InputRC.second)) {
    // FIXME: error out in a more elegant fashion
    report_fatal_error("Unsupported asm: input constraint"
                       " with a matching output constraint of"
                       " incompatible type!");
  }
  MatchingOpInfo.ConstraintVT = OpInfo.ConstraintVT;
}

/// Get a direct memory input to behave well as an indirect operand.
/// This may introduce stores, hence the need for a \p Chain.
/// \return The (possibly updated) chain.
static SDValue getAddressForMemoryInput(SDValue Chain, const SDLoc &Location,
                                        SDISelAsmOperandInfo &OpInfo,
                                        SelectionDAG &DAG) {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();

  // If we don't have an indirect input, put it in the constpool if we can,
  // otherwise spill it to a stack slot.
  // TODO: This isn't quite right. We need to handle these according to
  // the addressing mode that the constraint wants. Also, this may take
  // an additional register for the computation and we don't want that
  // either.

  // If the operand is a float, integer, or vector constant, spill to a
  // constant pool entry to get its address.
  const Value *OpVal = OpInfo.CallOperandVal;
  if (isa<ConstantFP>(OpVal) || isa<ConstantInt>(OpVal) ||
      isa<ConstantVector>(OpVal) || isa<ConstantDataVector>(OpVal)) {
    OpInfo.CallOperand = DAG.getConstantPool(
        cast<Constant>(OpVal), TLI.getPointerTy(DAG.getDataLayout()));
    return Chain;
  }

  // Otherwise, create a stack slot and emit a store to it before the asm.
  Type *Ty = OpVal->getType();
  auto &DL = DAG.getDataLayout();
  uint64_t TySize = DL.getTypeAllocSize(Ty);
  unsigned Align = DL.getPrefTypeAlignment(Ty);
  MachineFunction &MF = DAG.getMachineFunction();
  int SSFI = MF.getFrameInfo().CreateStackObject(TySize, Align, false);
  SDValue StackSlot = DAG.getFrameIndex(SSFI, TLI.getFrameIndexTy(DL));
  Chain = DAG.getStore(Chain, Location, OpInfo.CallOperand, StackSlot,
                       MachinePointerInfo::getFixedStack(MF, SSFI));
  OpInfo.CallOperand = StackSlot;

  return Chain;
}

/// GetRegistersForValue - Assign registers (virtual or physical) for the
/// specified operand.  We prefer to assign virtual registers, to allow the
/// register allocator to handle the assignment process.  However, if the asm
/// uses features that we can't model on machineinstrs, we have SDISel do the
/// allocation.  This produces generally horrible, but correct, code.
///
///   OpInfo describes the operand
///   RefOpInfo describes the matching operand if any, the operand otherwise
static void GetRegistersForValue(SelectionDAG &DAG, const SDLoc &DL,
                                 SDISelAsmOperandInfo &OpInfo,
                                 SDISelAsmOperandInfo &RefOpInfo) {
  LLVMContext &Context = *DAG.getContext();
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();

  MachineFunction &MF = DAG.getMachineFunction();
  SmallVector<unsigned, 4> Regs;
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  // If this is a constraint for a single physreg, or a constraint for a
  // register class, find it.
  unsigned AssignedReg;
  const TargetRegisterClass *RC;
  std::tie(AssignedReg, RC) = TLI.getRegForInlineAsmConstraint(
      &TRI, RefOpInfo.ConstraintCode, RefOpInfo.ConstraintVT);
  // RC is unset only on failure. Return immediately.
  if (!RC)
    return;

  // Get the actual register value type.  This is important, because the user
  // may have asked for (e.g.) the AX register in i32 type.  We need to
  // remember that AX is actually i16 to get the right extension.
  const MVT RegVT = *TRI.legalclasstypes_begin(*RC);

  if (OpInfo.ConstraintVT != MVT::Other) {
    // If this is an FP operand in an integer register (or visa versa), or more
    // generally if the operand value disagrees with the register class we plan
    // to stick it in, fix the operand type.
    //
    // If this is an input value, the bitcast to the new type is done now.
    // Bitcast for output value is done at the end of visitInlineAsm().
    if ((OpInfo.Type == InlineAsm::isOutput ||
         OpInfo.Type == InlineAsm::isInput) &&
        !TRI.isTypeLegalForClass(*RC, OpInfo.ConstraintVT)) {
      // Try to convert to the first EVT that the reg class contains.  If the
      // types are identical size, use a bitcast to convert (e.g. two differing
      // vector types).  Note: output bitcast is done at the end of
      // visitInlineAsm().
      if (RegVT.getSizeInBits() == OpInfo.ConstraintVT.getSizeInBits()) {
        // Exclude indirect inputs while they are unsupported because the code
        // to perform the load is missing and thus OpInfo.CallOperand still
        // refers to the input address rather than the pointed-to value.
        if (OpInfo.Type == InlineAsm::isInput && !OpInfo.isIndirect)
          OpInfo.CallOperand =
              DAG.getNode(ISD::BITCAST, DL, RegVT, OpInfo.CallOperand);
        OpInfo.ConstraintVT = RegVT;
        // If the operand is an FP value and we want it in integer registers,
        // use the corresponding integer type. This turns an f64 value into
        // i64, which can be passed with two i32 values on a 32-bit machine.
      } else if (RegVT.isInteger() && OpInfo.ConstraintVT.isFloatingPoint()) {
        MVT VT = MVT::getIntegerVT(OpInfo.ConstraintVT.getSizeInBits());
        if (OpInfo.Type == InlineAsm::isInput)
          OpInfo.CallOperand =
              DAG.getNode(ISD::BITCAST, DL, VT, OpInfo.CallOperand);
        OpInfo.ConstraintVT = VT;
      }
    }
  }

  // No need to allocate a matching input constraint since the constraint it's
  // matching to has already been allocated.
  if (OpInfo.isMatchingInputConstraint())
    return;

  EVT ValueVT = OpInfo.ConstraintVT;
  if (OpInfo.ConstraintVT == MVT::Other)
    ValueVT = RegVT;

  // Initialize NumRegs.
  unsigned NumRegs = 1;
  if (OpInfo.ConstraintVT != MVT::Other)
    NumRegs = TLI.getNumRegisters(Context, OpInfo.ConstraintVT);

  // If this is a constraint for a specific physical register, like {r17},
  // assign it now.

  // If this associated to a specific register, initialize iterator to correct
  // place. If virtual, make sure we have enough registers

  // Initialize iterator if necessary
  TargetRegisterClass::iterator I = RC->begin();
  MachineRegisterInfo &RegInfo = MF.getRegInfo();

  // Do not check for single registers.
  if (AssignedReg) {
      for (; *I != AssignedReg; ++I)
        assert(I != RC->end() && "AssignedReg should be member of RC");
  }

  for (; NumRegs; --NumRegs, ++I) {
    assert(I != RC->end() && "Ran out of registers to allocate!");
    auto R = (AssignedReg) ? *I : RegInfo.createVirtualRegister(RC);
    Regs.push_back(R);
  }

  OpInfo.AssignedRegs = RegsForValue(Regs, RegVT, ValueVT);
}

static unsigned
findMatchingInlineAsmOperand(unsigned OperandNo,
                             const std::vector<SDValue> &AsmNodeOperands) {
  // Scan until we find the definition we already emitted of this operand.
  unsigned CurOp = InlineAsm::Op_FirstOperand;
  for (; OperandNo; --OperandNo) {
    // Advance to the next operand.
    unsigned OpFlag =
        cast<ConstantSDNode>(AsmNodeOperands[CurOp])->getZExtValue();
    assert((InlineAsm::isRegDefKind(OpFlag) ||
            InlineAsm::isRegDefEarlyClobberKind(OpFlag) ||
            InlineAsm::isMemKind(OpFlag)) &&
           "Skipped past definitions?");
    CurOp += InlineAsm::getNumOperandRegisters(OpFlag) + 1;
  }
  return CurOp;
}

namespace {

class ExtraFlags {
  unsigned Flags = 0;

public:
  explicit ExtraFlags(ImmutableCallSite CS) {
    const InlineAsm *IA = cast<InlineAsm>(CS.getCalledValue());
    if (IA->hasSideEffects())
      Flags |= InlineAsm::Extra_HasSideEffects;
    if (IA->isAlignStack())
      Flags |= InlineAsm::Extra_IsAlignStack;
    if (CS.isConvergent())
      Flags |= InlineAsm::Extra_IsConvergent;
    Flags |= IA->getDialect() * InlineAsm::Extra_AsmDialect;
  }

  void update(const TargetLowering::AsmOperandInfo &OpInfo) {
    // Ideally, we would only check against memory constraints.  However, the
    // meaning of an Other constraint can be target-specific and we can't easily
    // reason about it.  Therefore, be conservative and set MayLoad/MayStore
    // for Other constraints as well.
    if (OpInfo.ConstraintType == TargetLowering::C_Memory ||
        OpInfo.ConstraintType == TargetLowering::C_Other) {
      if (OpInfo.Type == InlineAsm::isInput)
        Flags |= InlineAsm::Extra_MayLoad;
      else if (OpInfo.Type == InlineAsm::isOutput)
        Flags |= InlineAsm::Extra_MayStore;
      else if (OpInfo.Type == InlineAsm::isClobber)
        Flags |= (InlineAsm::Extra_MayLoad | InlineAsm::Extra_MayStore);
    }
  }

  unsigned get() const { return Flags; }
};

} // end anonymous namespace

/// visitInlineAsm - Handle a call to an InlineAsm object.
void SelectionDAGBuilder::visitInlineAsm(ImmutableCallSite CS) {
  const InlineAsm *IA = cast<InlineAsm>(CS.getCalledValue());

  /// ConstraintOperands - Information about all of the constraints.
  SDISelAsmOperandInfoVector ConstraintOperands;

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  TargetLowering::AsmOperandInfoVector TargetConstraints = TLI.ParseConstraints(
      DAG.getDataLayout(), DAG.getSubtarget().getRegisterInfo(), CS);

  bool hasMemory = false;

  // Remember the HasSideEffect, AlignStack, AsmDialect, MayLoad and MayStore
  ExtraFlags ExtraInfo(CS);

  unsigned ArgNo = 0;   // ArgNo - The argument of the CallInst.
  unsigned ResNo = 0;   // ResNo - The result number of the next output.
  for (auto &T : TargetConstraints) {
    ConstraintOperands.push_back(SDISelAsmOperandInfo(T));
    SDISelAsmOperandInfo &OpInfo = ConstraintOperands.back();

    // Compute the value type for each operand.
    if (OpInfo.Type == InlineAsm::isInput ||
        (OpInfo.Type == InlineAsm::isOutput && OpInfo.isIndirect)) {
      OpInfo.CallOperandVal = const_cast<Value *>(CS.getArgument(ArgNo++));

      // Process the call argument. BasicBlocks are labels, currently appearing
      // only in asm's.
      if (const BasicBlock *BB = dyn_cast<BasicBlock>(OpInfo.CallOperandVal)) {
        OpInfo.CallOperand = DAG.getBasicBlock(FuncInfo.MBBMap[BB]);
      } else {
        OpInfo.CallOperand = getValue(OpInfo.CallOperandVal);
      }

      OpInfo.ConstraintVT =
          OpInfo
              .getCallOperandValEVT(*DAG.getContext(), TLI, DAG.getDataLayout())
              .getSimpleVT();
    } else if (OpInfo.Type == InlineAsm::isOutput && !OpInfo.isIndirect) {
      // The return value of the call is this value.  As such, there is no
      // corresponding argument.
      assert(!CS.getType()->isVoidTy() && "Bad inline asm!");
      if (StructType *STy = dyn_cast<StructType>(CS.getType())) {
        OpInfo.ConstraintVT = TLI.getSimpleValueType(
            DAG.getDataLayout(), STy->getElementType(ResNo));
      } else {
        assert(ResNo == 0 && "Asm only has one result!");
        OpInfo.ConstraintVT =
            TLI.getSimpleValueType(DAG.getDataLayout(), CS.getType());
      }
      ++ResNo;
    } else {
      OpInfo.ConstraintVT = MVT::Other;
    }

    if (!hasMemory)
      hasMemory = OpInfo.hasMemory(TLI);

    // Determine if this InlineAsm MayLoad or MayStore based on the constraints.
    // FIXME: Could we compute this on OpInfo rather than T?

    // Compute the constraint code and ConstraintType to use.
    TLI.ComputeConstraintToUse(T, SDValue());

    ExtraInfo.update(T);
  }

  SDValue Chain, Flag;

  // We won't need to flush pending loads if this asm doesn't touch
  // memory and is nonvolatile.
  if (hasMemory || IA->hasSideEffects())
    Chain = getRoot();
  else
    Chain = DAG.getRoot();

  // Second pass over the constraints: compute which constraint option to use
  // and assign registers to constraints that want a specific physreg.
  for (SDISelAsmOperandInfo &OpInfo : ConstraintOperands) {
    // If this is an output operand with a matching input operand, look up the
    // matching input. If their types mismatch, e.g. one is an integer, the
    // other is floating point, or their sizes are different, flag it as an
    // error.
    if (OpInfo.hasMatchingInput()) {
      SDISelAsmOperandInfo &Input = ConstraintOperands[OpInfo.MatchingInput];
      patchMatchingInput(OpInfo, Input, DAG);
    }

    // Compute the constraint code and ConstraintType to use.
    TLI.ComputeConstraintToUse(OpInfo, OpInfo.CallOperand, &DAG);

    if (OpInfo.ConstraintType == TargetLowering::C_Memory &&
        OpInfo.Type == InlineAsm::isClobber)
      continue;

    // If this is a memory input, and if the operand is not indirect, do what we
    // need to provide an address for the memory input.
    if (OpInfo.ConstraintType == TargetLowering::C_Memory &&
        !OpInfo.isIndirect) {
      assert((OpInfo.isMultipleAlternative ||
              (OpInfo.Type == InlineAsm::isInput)) &&
             "Can only indirectify direct input operands!");

      // Memory operands really want the address of the value.
      Chain = getAddressForMemoryInput(Chain, getCurSDLoc(), OpInfo, DAG);

      // There is no longer a Value* corresponding to this operand.
      OpInfo.CallOperandVal = nullptr;

      // It is now an indirect operand.
      OpInfo.isIndirect = true;
    }

    // If this constraint is for a specific register, allocate it before
    // anything else.
    SDISelAsmOperandInfo &RefOpInfo =
        OpInfo.isMatchingInputConstraint()
            ? ConstraintOperands[OpInfo.getMatchedOperand()]
            : OpInfo;
    if (RefOpInfo.ConstraintType == TargetLowering::C_Register)
      GetRegistersForValue(DAG, getCurSDLoc(), OpInfo, RefOpInfo);
  }

  // Third pass - Loop over all of the operands, assigning virtual or physregs
  // to register class operands.
  for (SDISelAsmOperandInfo &OpInfo : ConstraintOperands) {
    SDISelAsmOperandInfo &RefOpInfo =
        OpInfo.isMatchingInputConstraint()
            ? ConstraintOperands[OpInfo.getMatchedOperand()]
            : OpInfo;

    // C_Register operands have already been allocated, Other/Memory don't need
    // to be.
    if (RefOpInfo.ConstraintType == TargetLowering::C_RegisterClass)
      GetRegistersForValue(DAG, getCurSDLoc(), OpInfo, RefOpInfo);
  }

  // AsmNodeOperands - The operands for the ISD::INLINEASM node.
  std::vector<SDValue> AsmNodeOperands;
  AsmNodeOperands.push_back(SDValue());  // reserve space for input chain
  AsmNodeOperands.push_back(DAG.getTargetExternalSymbol(
      IA->getAsmString().c_str(), TLI.getPointerTy(DAG.getDataLayout())));

  // If we have a !srcloc metadata node associated with it, we want to attach
  // this to the ultimately generated inline asm machineinstr.  To do this, we
  // pass in the third operand as this (potentially null) inline asm MDNode.
  const MDNode *SrcLoc = CS.getInstruction()->getMetadata("srcloc");
  AsmNodeOperands.push_back(DAG.getMDNode(SrcLoc));

  // Remember the HasSideEffect, AlignStack, AsmDialect, MayLoad and MayStore
  // bits as operand 3.
  AsmNodeOperands.push_back(DAG.getTargetConstant(
      ExtraInfo.get(), getCurSDLoc(), TLI.getPointerTy(DAG.getDataLayout())));

  // Loop over all of the inputs, copying the operand values into the
  // appropriate registers and processing the output regs.
  RegsForValue RetValRegs;

  // IndirectStoresToEmit - The set of stores to emit after the inline asm node.
  std::vector<std::pair<RegsForValue, Value *>> IndirectStoresToEmit;

  for (SDISelAsmOperandInfo &OpInfo : ConstraintOperands) {
    switch (OpInfo.Type) {
    case InlineAsm::isOutput:
      if (OpInfo.ConstraintType != TargetLowering::C_RegisterClass &&
          OpInfo.ConstraintType != TargetLowering::C_Register) {
        // Memory output, or 'other' output (e.g. 'X' constraint).
        assert(OpInfo.isIndirect && "Memory output must be indirect operand");

        unsigned ConstraintID =
            TLI.getInlineAsmMemConstraint(OpInfo.ConstraintCode);
        assert(ConstraintID != InlineAsm::Constraint_Unknown &&
               "Failed to convert memory constraint code to constraint id.");

        // Add information to the INLINEASM node to know about this output.
        unsigned OpFlags = InlineAsm::getFlagWord(InlineAsm::Kind_Mem, 1);
        OpFlags = InlineAsm::getFlagWordForMem(OpFlags, ConstraintID);
        AsmNodeOperands.push_back(DAG.getTargetConstant(OpFlags, getCurSDLoc(),
                                                        MVT::i32));
        AsmNodeOperands.push_back(OpInfo.CallOperand);
        break;
      }

      // Otherwise, this is a register or register class output.

      // Copy the output from the appropriate register.  Find a register that
      // we can use.
      if (OpInfo.AssignedRegs.Regs.empty()) {
        emitInlineAsmError(
            CS, "couldn't allocate output register for constraint '" +
                    Twine(OpInfo.ConstraintCode) + "'");
        return;
      }

      // If this is an indirect operand, store through the pointer after the
      // asm.
      if (OpInfo.isIndirect) {
        IndirectStoresToEmit.push_back(std::make_pair(OpInfo.AssignedRegs,
                                                      OpInfo.CallOperandVal));
      } else {
        // This is the result value of the call.
        assert(!CS.getType()->isVoidTy() && "Bad inline asm!");
        // Concatenate this output onto the outputs list.
        RetValRegs.append(OpInfo.AssignedRegs);
      }

      // Add information to the INLINEASM node to know that this register is
      // set.
      OpInfo.AssignedRegs
          .AddInlineAsmOperands(OpInfo.isEarlyClobber
                                    ? InlineAsm::Kind_RegDefEarlyClobber
                                    : InlineAsm::Kind_RegDef,
                                false, 0, getCurSDLoc(), DAG, AsmNodeOperands);
      break;

    case InlineAsm::isInput: {
      SDValue InOperandVal = OpInfo.CallOperand;

      if (OpInfo.isMatchingInputConstraint()) {
        // If this is required to match an output register we have already set,
        // just use its register.
        auto CurOp = findMatchingInlineAsmOperand(OpInfo.getMatchedOperand(),
                                                  AsmNodeOperands);
        unsigned OpFlag =
          cast<ConstantSDNode>(AsmNodeOperands[CurOp])->getZExtValue();
        if (InlineAsm::isRegDefKind(OpFlag) ||
            InlineAsm::isRegDefEarlyClobberKind(OpFlag)) {
          // Add (OpFlag&0xffff)>>3 registers to MatchedRegs.
          if (OpInfo.isIndirect) {
            // This happens on gcc/testsuite/gcc.dg/pr8788-1.c
            emitInlineAsmError(CS, "inline asm not supported yet:"
                                   " don't know how to handle tied "
                                   "indirect register inputs");
            return;
          }

          MVT RegVT = AsmNodeOperands[CurOp+1].getSimpleValueType();
          SmallVector<unsigned, 4> Regs;

          if (const TargetRegisterClass *RC = TLI.getRegClassFor(RegVT)) {
            unsigned NumRegs = InlineAsm::getNumOperandRegisters(OpFlag);
            MachineRegisterInfo &RegInfo =
                DAG.getMachineFunction().getRegInfo();
            for (unsigned i = 0; i != NumRegs; ++i)
              Regs.push_back(RegInfo.createVirtualRegister(RC));
          } else {
            emitInlineAsmError(CS, "inline asm error: This value type register "
                                   "class is not natively supported!");
            return;
          }

          RegsForValue MatchedRegs(Regs, RegVT, InOperandVal.getValueType());

          SDLoc dl = getCurSDLoc();
          // Use the produced MatchedRegs object to
          MatchedRegs.getCopyToRegs(InOperandVal, DAG, dl, Chain, &Flag,
                                    CS.getInstruction());
          MatchedRegs.AddInlineAsmOperands(InlineAsm::Kind_RegUse,
                                           true, OpInfo.getMatchedOperand(), dl,
                                           DAG, AsmNodeOperands);
          break;
        }

        assert(InlineAsm::isMemKind(OpFlag) && "Unknown matching constraint!");
        assert(InlineAsm::getNumOperandRegisters(OpFlag) == 1 &&
               "Unexpected number of operands");
        // Add information to the INLINEASM node to know about this input.
        // See InlineAsm.h isUseOperandTiedToDef.
        OpFlag = InlineAsm::convertMemFlagWordToMatchingFlagWord(OpFlag);
        OpFlag = InlineAsm::getFlagWordForMatchingOp(OpFlag,
                                                    OpInfo.getMatchedOperand());
        AsmNodeOperands.push_back(DAG.getTargetConstant(
            OpFlag, getCurSDLoc(), TLI.getPointerTy(DAG.getDataLayout())));
        AsmNodeOperands.push_back(AsmNodeOperands[CurOp+1]);
        break;
      }

      // Treat indirect 'X' constraint as memory.
      if (OpInfo.ConstraintType == TargetLowering::C_Other &&
          OpInfo.isIndirect)
        OpInfo.ConstraintType = TargetLowering::C_Memory;

      if (OpInfo.ConstraintType == TargetLowering::C_Other) {
        std::vector<SDValue> Ops;
        TLI.LowerAsmOperandForConstraint(InOperandVal, OpInfo.ConstraintCode,
                                          Ops, DAG);
        if (Ops.empty()) {
          emitInlineAsmError(CS, "invalid operand for inline asm constraint '" +
                                     Twine(OpInfo.ConstraintCode) + "'");
          return;
        }

        // Add information to the INLINEASM node to know about this input.
        unsigned ResOpType =
          InlineAsm::getFlagWord(InlineAsm::Kind_Imm, Ops.size());
        AsmNodeOperands.push_back(DAG.getTargetConstant(
            ResOpType, getCurSDLoc(), TLI.getPointerTy(DAG.getDataLayout())));
        AsmNodeOperands.insert(AsmNodeOperands.end(), Ops.begin(), Ops.end());
        break;
      }

      if (OpInfo.ConstraintType == TargetLowering::C_Memory) {
        assert(OpInfo.isIndirect && "Operand must be indirect to be a mem!");
        assert(InOperandVal.getValueType() ==
                   TLI.getPointerTy(DAG.getDataLayout()) &&
               "Memory operands expect pointer values");

        unsigned ConstraintID =
            TLI.getInlineAsmMemConstraint(OpInfo.ConstraintCode);
        assert(ConstraintID != InlineAsm::Constraint_Unknown &&
               "Failed to convert memory constraint code to constraint id.");

        // Add information to the INLINEASM node to know about this input.
        unsigned ResOpType = InlineAsm::getFlagWord(InlineAsm::Kind_Mem, 1);
        ResOpType = InlineAsm::getFlagWordForMem(ResOpType, ConstraintID);
        AsmNodeOperands.push_back(DAG.getTargetConstant(ResOpType,
                                                        getCurSDLoc(),
                                                        MVT::i32));
        AsmNodeOperands.push_back(InOperandVal);
        break;
      }

      assert((OpInfo.ConstraintType == TargetLowering::C_RegisterClass ||
              OpInfo.ConstraintType == TargetLowering::C_Register) &&
             "Unknown constraint type!");

      // TODO: Support this.
      if (OpInfo.isIndirect) {
        emitInlineAsmError(
            CS, "Don't know how to handle indirect register inputs yet "
                "for constraint '" +
                    Twine(OpInfo.ConstraintCode) + "'");
        return;
      }

      // Copy the input into the appropriate registers.
      if (OpInfo.AssignedRegs.Regs.empty()) {
        emitInlineAsmError(CS, "couldn't allocate input reg for constraint '" +
                                   Twine(OpInfo.ConstraintCode) + "'");
        return;
      }

      SDLoc dl = getCurSDLoc();

      OpInfo.AssignedRegs.getCopyToRegs(InOperandVal, DAG, dl,
                                        Chain, &Flag, CS.getInstruction());

      OpInfo.AssignedRegs.AddInlineAsmOperands(InlineAsm::Kind_RegUse, false, 0,
                                               dl, DAG, AsmNodeOperands);
      break;
    }
    case InlineAsm::isClobber:
      // Add the clobbered value to the operand list, so that the register
      // allocator is aware that the physreg got clobbered.
      if (!OpInfo.AssignedRegs.Regs.empty())
        OpInfo.AssignedRegs.AddInlineAsmOperands(InlineAsm::Kind_Clobber,
                                                 false, 0, getCurSDLoc(), DAG,
                                                 AsmNodeOperands);
      break;
    }
  }

  // Finish up input operands.  Set the input chain and add the flag last.
  AsmNodeOperands[InlineAsm::Op_InputChain] = Chain;
  if (Flag.getNode()) AsmNodeOperands.push_back(Flag);

  Chain = DAG.getNode(ISD::INLINEASM, getCurSDLoc(),
                      DAG.getVTList(MVT::Other, MVT::Glue), AsmNodeOperands);
  Flag = Chain.getValue(1);

  // If this asm returns a register value, copy the result from that register
  // and set it as the value of the call.
  if (!RetValRegs.Regs.empty()) {
    SDValue Val = RetValRegs.getCopyFromRegs(DAG, FuncInfo, getCurSDLoc(),
                                             Chain, &Flag, CS.getInstruction());

    llvm::Type *CSResultType = CS.getType();
    unsigned numRet;
    ArrayRef<Type *> ResultTypes;
    SmallVector<SDValue, 1> ResultValues(1);
    if (StructType *StructResult = dyn_cast<StructType>(CSResultType)) {
      numRet = StructResult->getNumElements();
      assert(Val->getNumOperands() == numRet &&
             "Mismatch in number of output operands in asm result");
      ResultTypes = StructResult->elements();
      ArrayRef<SDUse> ValueUses = Val->ops();
      ResultValues.resize(numRet);
      std::transform(ValueUses.begin(), ValueUses.end(), ResultValues.begin(),
                     [](const SDUse &u) -> SDValue { return u.get(); });
    } else {
      numRet = 1;
      ResultValues[0] = Val;
      ResultTypes = makeArrayRef(CSResultType);
    }
    SmallVector<EVT, 1> ResultVTs(numRet);
    for (unsigned i = 0; i < numRet; i++) {
      EVT ResultVT = TLI.getValueType(DAG.getDataLayout(), ResultTypes[i]);
      SDValue Val = ResultValues[i];
      assert(ResultTypes[i]->isSized() && "Unexpected unsized type");
      // If the type of the inline asm call site return value is different but
      // has same size as the type of the asm output bitcast it.  One example
      // of this is for vectors with different width / number of elements.
      // This can happen for register classes that can contain multiple
      // different value types.  The preg or vreg allocated may not have the
      // same VT as was expected.
      //
      // This can also happen for a return value that disagrees with the
      // register class it is put in, eg. a double in a general-purpose
      // register on a 32-bit machine.
      if (ResultVT != Val.getValueType() &&
          ResultVT.getSizeInBits() == Val.getValueSizeInBits())
        Val = DAG.getNode(ISD::BITCAST, getCurSDLoc(), ResultVT, Val);
      else if (ResultVT != Val.getValueType() && ResultVT.isInteger() &&
               Val.getValueType().isInteger()) {
        // If a result value was tied to an input value, the computed result
        // may have a wider width than the expected result.  Extract the
        // relevant portion.
        Val = DAG.getNode(ISD::TRUNCATE, getCurSDLoc(), ResultVT, Val);
      }

      assert(ResultVT == Val.getValueType() && "Asm result value mismatch!");
      ResultVTs[i] = ResultVT;
      ResultValues[i] = Val;
    }

    Val = DAG.getNode(ISD::MERGE_VALUES, getCurSDLoc(),
                      DAG.getVTList(ResultVTs), ResultValues);
    setValue(CS.getInstruction(), Val);
    // Don't need to use this as a chain in this case.
    if (!IA->hasSideEffects() && !hasMemory && IndirectStoresToEmit.empty())
      return;
  }

  std::vector<std::pair<SDValue, const Value *>> StoresToEmit;

  // Process indirect outputs, first output all of the flagged copies out of
  // physregs.
  for (unsigned i = 0, e = IndirectStoresToEmit.size(); i != e; ++i) {
    RegsForValue &OutRegs = IndirectStoresToEmit[i].first;
    const Value *Ptr = IndirectStoresToEmit[i].second;
    SDValue OutVal = OutRegs.getCopyFromRegs(DAG, FuncInfo, getCurSDLoc(),
                                             Chain, &Flag, IA);
    StoresToEmit.push_back(std::make_pair(OutVal, Ptr));
  }

  // Emit the non-flagged stores from the physregs.
  SmallVector<SDValue, 8> OutChains;
  for (unsigned i = 0, e = StoresToEmit.size(); i != e; ++i) {
    SDValue Val = DAG.getStore(Chain, getCurSDLoc(), StoresToEmit[i].first,
                               getValue(StoresToEmit[i].second),
                               MachinePointerInfo(StoresToEmit[i].second));
    OutChains.push_back(Val);
  }

  if (!OutChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, getCurSDLoc(), MVT::Other, OutChains);

  DAG.setRoot(Chain);
}

void SelectionDAGBuilder::emitInlineAsmError(ImmutableCallSite CS,
                                             const Twine &Message) {
  LLVMContext &Ctx = *DAG.getContext();
  Ctx.emitError(CS.getInstruction(), Message);

  // Make sure we leave the DAG in a valid state
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  SmallVector<EVT, 1> ValueVTs;
  ComputeValueVTs(TLI, DAG.getDataLayout(), CS->getType(), ValueVTs);

  if (ValueVTs.empty())
    return;

  SmallVector<SDValue, 1> Ops;
  for (unsigned i = 0, e = ValueVTs.size(); i != e; ++i)
    Ops.push_back(DAG.getUNDEF(ValueVTs[i]));

  setValue(CS.getInstruction(), DAG.getMergeValues(Ops, getCurSDLoc()));
}

void SelectionDAGBuilder::visitVAStart(const CallInst &I) {
  DAG.setRoot(DAG.getNode(ISD::VASTART, getCurSDLoc(),
                          MVT::Other, getRoot(),
                          getValue(I.getArgOperand(0)),
                          DAG.getSrcValue(I.getArgOperand(0))));
}

void SelectionDAGBuilder::visitVAArg(const VAArgInst &I) {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  const DataLayout &DL = DAG.getDataLayout();
  SDValue V = DAG.getVAArg(TLI.getValueType(DAG.getDataLayout(), I.getType()),
                           getCurSDLoc(), getRoot(), getValue(I.getOperand(0)),
                           DAG.getSrcValue(I.getOperand(0)),
                           DL.getABITypeAlignment(I.getType()));
  setValue(&I, V);
  DAG.setRoot(V.getValue(1));
}

void SelectionDAGBuilder::visitVAEnd(const CallInst &I) {
  DAG.setRoot(DAG.getNode(ISD::VAEND, getCurSDLoc(),
                          MVT::Other, getRoot(),
                          getValue(I.getArgOperand(0)),
                          DAG.getSrcValue(I.getArgOperand(0))));
}

void SelectionDAGBuilder::visitVACopy(const CallInst &I) {
  DAG.setRoot(DAG.getNode(ISD::VACOPY, getCurSDLoc(),
                          MVT::Other, getRoot(),
                          getValue(I.getArgOperand(0)),
                          getValue(I.getArgOperand(1)),
                          DAG.getSrcValue(I.getArgOperand(0)),
                          DAG.getSrcValue(I.getArgOperand(1))));
}

SDValue SelectionDAGBuilder::lowerRangeToAssertZExt(SelectionDAG &DAG,
                                                    const Instruction &I,
                                                    SDValue Op) {
  const MDNode *Range = I.getMetadata(LLVMContext::MD_range);
  if (!Range)
    return Op;

  ConstantRange CR = getConstantRangeFromMetadata(*Range);
  if (CR.isFullSet() || CR.isEmptySet() || CR.isWrappedSet())
    return Op;

  APInt Lo = CR.getUnsignedMin();
  if (!Lo.isMinValue())
    return Op;

  APInt Hi = CR.getUnsignedMax();
  unsigned Bits = std::max(Hi.getActiveBits(),
                           static_cast<unsigned>(IntegerType::MIN_INT_BITS));

  EVT SmallVT = EVT::getIntegerVT(*DAG.getContext(), Bits);

  SDLoc SL = getCurSDLoc();

  SDValue ZExt = DAG.getNode(ISD::AssertZext, SL, Op.getValueType(), Op,
                             DAG.getValueType(SmallVT));
  unsigned NumVals = Op.getNode()->getNumValues();
  if (NumVals == 1)
    return ZExt;

  SmallVector<SDValue, 4> Ops;

  Ops.push_back(ZExt);
  for (unsigned I = 1; I != NumVals; ++I)
    Ops.push_back(Op.getValue(I));

  return DAG.getMergeValues(Ops, SL);
}

/// Populate a CallLowerinInfo (into \p CLI) based on the properties of
/// the call being lowered.
///
/// This is a helper for lowering intrinsics that follow a target calling
/// convention or require stack pointer adjustment. Only a subset of the
/// intrinsic's operands need to participate in the calling convention.
void SelectionDAGBuilder::populateCallLoweringInfo(
    TargetLowering::CallLoweringInfo &CLI, ImmutableCallSite CS,
    unsigned ArgIdx, unsigned NumArgs, SDValue Callee, Type *ReturnTy,
    bool IsPatchPoint) {
  TargetLowering::ArgListTy Args;
  Args.reserve(NumArgs);

  // Populate the argument list.
  // Attributes for args start at offset 1, after the return attribute.
  for (unsigned ArgI = ArgIdx, ArgE = ArgIdx + NumArgs;
       ArgI != ArgE; ++ArgI) {
    const Value *V = CS->getOperand(ArgI);

    assert(!V->getType()->isEmptyTy() && "Empty type passed to intrinsic.");

    TargetLowering::ArgListEntry Entry;
    Entry.Node = getValue(V);
    Entry.Ty = V->getType();
    Entry.setAttributes(&CS, ArgI);
    Args.push_back(Entry);
  }

  CLI.setDebugLoc(getCurSDLoc())
      .setChain(getRoot())
      .setCallee(CS.getCallingConv(), ReturnTy, Callee, std::move(Args))
      .setDiscardResult(CS->use_empty())
      .setIsPatchPoint(IsPatchPoint);
}

/// Add a stack map intrinsic call's live variable operands to a stackmap
/// or patchpoint target node's operand list.
///
/// Constants are converted to TargetConstants purely as an optimization to
/// avoid constant materialization and register allocation.
///
/// FrameIndex operands are converted to TargetFrameIndex so that ISEL does not
/// generate addess computation nodes, and so ExpandISelPseudo can convert the
/// TargetFrameIndex into a DirectMemRefOp StackMap location. This avoids
/// address materialization and register allocation, but may also be required
/// for correctness. If a StackMap (or PatchPoint) intrinsic directly uses an
/// alloca in the entry block, then the runtime may assume that the alloca's
/// StackMap location can be read immediately after compilation and that the
/// location is valid at any point during execution (this is similar to the
/// assumption made by the llvm.gcroot intrinsic). If the alloca's location were
/// only available in a register, then the runtime would need to trap when
/// execution reaches the StackMap in order to read the alloca's location.
static void addStackMapLiveVars(ImmutableCallSite CS, unsigned StartIdx,
                                const SDLoc &DL, SmallVectorImpl<SDValue> &Ops,
                                SelectionDAGBuilder &Builder) {
  for (unsigned i = StartIdx, e = CS.arg_size(); i != e; ++i) {
    SDValue OpVal = Builder.getValue(CS.getArgument(i));
    if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(OpVal)) {
      Ops.push_back(
        Builder.DAG.getTargetConstant(StackMaps::ConstantOp, DL, MVT::i64));
      Ops.push_back(
        Builder.DAG.getTargetConstant(C->getSExtValue(), DL, MVT::i64));
    } else if (FrameIndexSDNode *FI = dyn_cast<FrameIndexSDNode>(OpVal)) {
      const TargetLowering &TLI = Builder.DAG.getTargetLoweringInfo();
      Ops.push_back(Builder.DAG.getTargetFrameIndex(
          FI->getIndex(), TLI.getFrameIndexTy(Builder.DAG.getDataLayout())));
    } else
      Ops.push_back(OpVal);
  }
}

/// Lower llvm.experimental.stackmap directly to its target opcode.
void SelectionDAGBuilder::visitStackmap(const CallInst &CI) {
  // void @llvm.experimental.stackmap(i32 <id>, i32 <numShadowBytes>,
  //                                  [live variables...])

  assert(CI.getType()->isVoidTy() && "Stackmap cannot return a value.");

  SDValue Chain, InFlag, Callee, NullPtr;
  SmallVector<SDValue, 32> Ops;

  SDLoc DL = getCurSDLoc();
  Callee = getValue(CI.getCalledValue());
  NullPtr = DAG.getIntPtrConstant(0, DL, true);

  // The stackmap intrinsic only records the live variables (the arguemnts
  // passed to it) and emits NOPS (if requested). Unlike the patchpoint
  // intrinsic, this won't be lowered to a function call. This means we don't
  // have to worry about calling conventions and target specific lowering code.
  // Instead we perform the call lowering right here.
  //
  // chain, flag = CALLSEQ_START(chain, 0, 0)
  // chain, flag = STACKMAP(id, nbytes, ..., chain, flag)
  // chain, flag = CALLSEQ_END(chain, 0, 0, flag)
  //
  Chain = DAG.getCALLSEQ_START(getRoot(), 0, 0, DL);
  InFlag = Chain.getValue(1);

  // Add the <id> and <numBytes> constants.
  SDValue IDVal = getValue(CI.getOperand(PatchPointOpers::IDPos));
  Ops.push_back(DAG.getTargetConstant(
                  cast<ConstantSDNode>(IDVal)->getZExtValue(), DL, MVT::i64));
  SDValue NBytesVal = getValue(CI.getOperand(PatchPointOpers::NBytesPos));
  Ops.push_back(DAG.getTargetConstant(
                  cast<ConstantSDNode>(NBytesVal)->getZExtValue(), DL,
                  MVT::i32));

  // Push live variables for the stack map.
  addStackMapLiveVars(&CI, 2, DL, Ops, *this);

  // We are not pushing any register mask info here on the operands list,
  // because the stackmap doesn't clobber anything.

  // Push the chain and the glue flag.
  Ops.push_back(Chain);
  Ops.push_back(InFlag);

  // Create the STACKMAP node.
  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  SDNode *SM = DAG.getMachineNode(TargetOpcode::STACKMAP, DL, NodeTys, Ops);
  Chain = SDValue(SM, 0);
  InFlag = Chain.getValue(1);

  Chain = DAG.getCALLSEQ_END(Chain, NullPtr, NullPtr, InFlag, DL);

  // Stackmaps don't generate values, so nothing goes into the NodeMap.

  // Set the root to the target-lowered call chain.
  DAG.setRoot(Chain);

  // Inform the Frame Information that we have a stackmap in this function.
  FuncInfo.MF->getFrameInfo().setHasStackMap();
}

/// Lower llvm.experimental.patchpoint directly to its target opcode.
void SelectionDAGBuilder::visitPatchpoint(ImmutableCallSite CS,
                                          const BasicBlock *EHPadBB) {
  // void|i64 @llvm.experimental.patchpoint.void|i64(i64 <id>,
  //                                                 i32 <numBytes>,
  //                                                 i8* <target>,
  //                                                 i32 <numArgs>,
  //                                                 [Args...],
  //                                                 [live variables...])

  CallingConv::ID CC = CS.getCallingConv();
  bool IsAnyRegCC = CC == CallingConv::AnyReg;
  bool HasDef = !CS->getType()->isVoidTy();
  SDLoc dl = getCurSDLoc();
  SDValue Callee = getValue(CS->getOperand(PatchPointOpers::TargetPos));

  // Handle immediate and symbolic callees.
  if (auto* ConstCallee = dyn_cast<ConstantSDNode>(Callee))
    Callee = DAG.getIntPtrConstant(ConstCallee->getZExtValue(), dl,
                                   /*isTarget=*/true);
  else if (auto* SymbolicCallee = dyn_cast<GlobalAddressSDNode>(Callee))
    Callee =  DAG.getTargetGlobalAddress(SymbolicCallee->getGlobal(),
                                         SDLoc(SymbolicCallee),
                                         SymbolicCallee->getValueType(0));

  // Get the real number of arguments participating in the call <numArgs>
  SDValue NArgVal = getValue(CS.getArgument(PatchPointOpers::NArgPos));
  unsigned NumArgs = cast<ConstantSDNode>(NArgVal)->getZExtValue();

  // Skip the four meta args: <id>, <numNopBytes>, <target>, <numArgs>
  // Intrinsics include all meta-operands up to but not including CC.
  unsigned NumMetaOpers = PatchPointOpers::CCPos;
  assert(CS.arg_size() >= NumMetaOpers + NumArgs &&
         "Not enough arguments provided to the patchpoint intrinsic");

  // For AnyRegCC the arguments are lowered later on manually.
  unsigned NumCallArgs = IsAnyRegCC ? 0 : NumArgs;
  Type *ReturnTy =
    IsAnyRegCC ? Type::getVoidTy(*DAG.getContext()) : CS->getType();

  TargetLowering::CallLoweringInfo CLI(DAG);
  populateCallLoweringInfo(CLI, CS, NumMetaOpers, NumCallArgs, Callee, ReturnTy,
                           true);
  std::pair<SDValue, SDValue> Result = lowerInvokable(CLI, EHPadBB);

  SDNode *CallEnd = Result.second.getNode();
  if (HasDef && (CallEnd->getOpcode() == ISD::CopyFromReg))
    CallEnd = CallEnd->getOperand(0).getNode();

  /// Get a call instruction from the call sequence chain.
  /// Tail calls are not allowed.
  assert(CallEnd->getOpcode() == ISD::CALLSEQ_END &&
         "Expected a callseq node.");
  SDNode *Call = CallEnd->getOperand(0).getNode();
  bool HasGlue = Call->getGluedNode();

  // Replace the target specific call node with the patchable intrinsic.
  SmallVector<SDValue, 8> Ops;

  // Add the <id> and <numBytes> constants.
  SDValue IDVal = getValue(CS->getOperand(PatchPointOpers::IDPos));
  Ops.push_back(DAG.getTargetConstant(
                  cast<ConstantSDNode>(IDVal)->getZExtValue(), dl, MVT::i64));
  SDValue NBytesVal = getValue(CS->getOperand(PatchPointOpers::NBytesPos));
  Ops.push_back(DAG.getTargetConstant(
                  cast<ConstantSDNode>(NBytesVal)->getZExtValue(), dl,
                  MVT::i32));

  // Add the callee.
  Ops.push_back(Callee);

  // Adjust <numArgs> to account for any arguments that have been passed on the
  // stack instead.
  // Call Node: Chain, Target, {Args}, RegMask, [Glue]
  unsigned NumCallRegArgs = Call->getNumOperands() - (HasGlue ? 4 : 3);
  NumCallRegArgs = IsAnyRegCC ? NumArgs : NumCallRegArgs;
  Ops.push_back(DAG.getTargetConstant(NumCallRegArgs, dl, MVT::i32));

  // Add the calling convention
  Ops.push_back(DAG.getTargetConstant((unsigned)CC, dl, MVT::i32));

  // Add the arguments we omitted previously. The register allocator should
  // place these in any free register.
  if (IsAnyRegCC)
    for (unsigned i = NumMetaOpers, e = NumMetaOpers + NumArgs; i != e; ++i)
      Ops.push_back(getValue(CS.getArgument(i)));

  // Push the arguments from the call instruction up to the register mask.
  SDNode::op_iterator e = HasGlue ? Call->op_end()-2 : Call->op_end()-1;
  Ops.append(Call->op_begin() + 2, e);

  // Push live variables for the stack map.
  addStackMapLiveVars(CS, NumMetaOpers + NumArgs, dl, Ops, *this);

  // Push the register mask info.
  if (HasGlue)
    Ops.push_back(*(Call->op_end()-2));
  else
    Ops.push_back(*(Call->op_end()-1));

  // Push the chain (this is originally the first operand of the call, but
  // becomes now the last or second to last operand).
  Ops.push_back(*(Call->op_begin()));

  // Push the glue flag (last operand).
  if (HasGlue)
    Ops.push_back(*(Call->op_end()-1));

  SDVTList NodeTys;
  if (IsAnyRegCC && HasDef) {
    // Create the return types based on the intrinsic definition
    const TargetLowering &TLI = DAG.getTargetLoweringInfo();
    SmallVector<EVT, 3> ValueVTs;
    ComputeValueVTs(TLI, DAG.getDataLayout(), CS->getType(), ValueVTs);
    assert(ValueVTs.size() == 1 && "Expected only one return value type.");

    // There is always a chain and a glue type at the end
    ValueVTs.push_back(MVT::Other);
    ValueVTs.push_back(MVT::Glue);
    NodeTys = DAG.getVTList(ValueVTs);
  } else
    NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);

  // Replace the target specific call node with a PATCHPOINT node.
  MachineSDNode *MN = DAG.getMachineNode(TargetOpcode::PATCHPOINT,
                                         dl, NodeTys, Ops);

  // Update the NodeMap.
  if (HasDef) {
    if (IsAnyRegCC)
      setValue(CS.getInstruction(), SDValue(MN, 0));
    else
      setValue(CS.getInstruction(), Result.first);
  }

  // Fixup the consumers of the intrinsic. The chain and glue may be used in the
  // call sequence. Furthermore the location of the chain and glue can change
  // when the AnyReg calling convention is used and the intrinsic returns a
  // value.
  if (IsAnyRegCC && HasDef) {
    SDValue From[] = {SDValue(Call, 0), SDValue(Call, 1)};
    SDValue To[] = {SDValue(MN, 1), SDValue(MN, 2)};
    DAG.ReplaceAllUsesOfValuesWith(From, To, 2);
  } else
    DAG.ReplaceAllUsesWith(Call, MN);
  DAG.DeleteNode(Call);

  // Inform the Frame Information that we have a patchpoint in this function.
  FuncInfo.MF->getFrameInfo().setHasPatchPoint();
}

void SelectionDAGBuilder::visitVectorReduce(const CallInst &I,
                                            unsigned Intrinsic) {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  SDValue Op1 = getValue(I.getArgOperand(0));
  SDValue Op2;
  if (I.getNumArgOperands() > 1)
    Op2 = getValue(I.getArgOperand(1));
  SDLoc dl = getCurSDLoc();
  EVT VT = TLI.getValueType(DAG.getDataLayout(), I.getType());
  SDValue Res;
  FastMathFlags FMF;
  if (isa<FPMathOperator>(I))
    FMF = I.getFastMathFlags();

  switch (Intrinsic) {
  case Intrinsic::experimental_vector_reduce_fadd:
    if (FMF.isFast())
      Res = DAG.getNode(ISD::VECREDUCE_FADD, dl, VT, Op2);
    else
      Res = DAG.getNode(ISD::VECREDUCE_STRICT_FADD, dl, VT, Op1, Op2);
    break;
  case Intrinsic::experimental_vector_reduce_fmul:
    if (FMF.isFast())
      Res = DAG.getNode(ISD::VECREDUCE_FMUL, dl, VT, Op2);
    else
      Res = DAG.getNode(ISD::VECREDUCE_STRICT_FMUL, dl, VT, Op1, Op2);
    break;
  case Intrinsic::experimental_vector_reduce_add:
    Res = DAG.getNode(ISD::VECREDUCE_ADD, dl, VT, Op1);
    break;
  case Intrinsic::experimental_vector_reduce_mul:
    Res = DAG.getNode(ISD::VECREDUCE_MUL, dl, VT, Op1);
    break;
  case Intrinsic::experimental_vector_reduce_and:
    Res = DAG.getNode(ISD::VECREDUCE_AND, dl, VT, Op1);
    break;
  case Intrinsic::experimental_vector_reduce_or:
    Res = DAG.getNode(ISD::VECREDUCE_OR, dl, VT, Op1);
    break;
  case Intrinsic::experimental_vector_reduce_xor:
    Res = DAG.getNode(ISD::VECREDUCE_XOR, dl, VT, Op1);
    break;
  case Intrinsic::experimental_vector_reduce_smax:
    Res = DAG.getNode(ISD::VECREDUCE_SMAX, dl, VT, Op1);
    break;
  case Intrinsic::experimental_vector_reduce_smin:
    Res = DAG.getNode(ISD::VECREDUCE_SMIN, dl, VT, Op1);
    break;
  case Intrinsic::experimental_vector_reduce_umax:
    Res = DAG.getNode(ISD::VECREDUCE_UMAX, dl, VT, Op1);
    break;
  case Intrinsic::experimental_vector_reduce_umin:
    Res = DAG.getNode(ISD::VECREDUCE_UMIN, dl, VT, Op1);
    break;
  case Intrinsic::experimental_vector_reduce_fmax:
    Res = DAG.getNode(ISD::VECREDUCE_FMAX, dl, VT, Op1);
    break;
  case Intrinsic::experimental_vector_reduce_fmin:
    Res = DAG.getNode(ISD::VECREDUCE_FMIN, dl, VT, Op1);
    break;
  default:
    llvm_unreachable("Unhandled vector reduce intrinsic");
  }
  setValue(&I, Res);
}

/// Returns an AttributeList representing the attributes applied to the return
/// value of the given call.
static AttributeList getReturnAttrs(TargetLowering::CallLoweringInfo &CLI) {
  SmallVector<Attribute::AttrKind, 2> Attrs;
  if (CLI.RetSExt)
    Attrs.push_back(Attribute::SExt);
  if (CLI.RetZExt)
    Attrs.push_back(Attribute::ZExt);
  if (CLI.IsInReg)
    Attrs.push_back(Attribute::InReg);

  return AttributeList::get(CLI.RetTy->getContext(), AttributeList::ReturnIndex,
                            Attrs);
}

/// TargetLowering::LowerCallTo - This is the default LowerCallTo
/// implementation, which just calls LowerCall.
/// FIXME: When all targets are
/// migrated to using LowerCall, this hook should be integrated into SDISel.
std::pair<SDValue, SDValue>
TargetLowering::LowerCallTo(TargetLowering::CallLoweringInfo &CLI) const {
  // Handle the incoming return values from the call.
  CLI.Ins.clear();
  Type *OrigRetTy = CLI.RetTy;
  SmallVector<EVT, 4> RetTys;
  SmallVector<uint64_t, 4> Offsets;
  auto &DL = CLI.DAG.getDataLayout();
  ComputeValueVTs(*this, DL, CLI.RetTy, RetTys, &Offsets);

  if (CLI.IsPostTypeLegalization) {
    // If we are lowering a libcall after legalization, split the return type.
    SmallVector<EVT, 4> OldRetTys = std::move(RetTys);
    SmallVector<uint64_t, 4> OldOffsets = std::move(Offsets);
    for (size_t i = 0, e = OldRetTys.size(); i != e; ++i) {
      EVT RetVT = OldRetTys[i];
      uint64_t Offset = OldOffsets[i];
      MVT RegisterVT = getRegisterType(CLI.RetTy->getContext(), RetVT);
      unsigned NumRegs = getNumRegisters(CLI.RetTy->getContext(), RetVT);
      unsigned RegisterVTByteSZ = RegisterVT.getSizeInBits() / 8;
      RetTys.append(NumRegs, RegisterVT);
      for (unsigned j = 0; j != NumRegs; ++j)
        Offsets.push_back(Offset + j * RegisterVTByteSZ);
    }
  }

  SmallVector<ISD::OutputArg, 4> Outs;
  GetReturnInfo(CLI.CallConv, CLI.RetTy, getReturnAttrs(CLI), Outs, *this, DL);

  bool CanLowerReturn =
      this->CanLowerReturn(CLI.CallConv, CLI.DAG.getMachineFunction(),
                           CLI.IsVarArg, Outs, CLI.RetTy->getContext());

  SDValue DemoteStackSlot;
  int DemoteStackIdx = -100;
  if (!CanLowerReturn) {
    // FIXME: equivalent assert?
    // assert(!CS.hasInAllocaArgument() &&
    //        "sret demotion is incompatible with inalloca");
    uint64_t TySize = DL.getTypeAllocSize(CLI.RetTy);
    unsigned Align = DL.getPrefTypeAlignment(CLI.RetTy);
    MachineFunction &MF = CLI.DAG.getMachineFunction();
    DemoteStackIdx = MF.getFrameInfo().CreateStackObject(TySize, Align, false);
    Type *StackSlotPtrType = PointerType::get(CLI.RetTy,
                                              DL.getAllocaAddrSpace());

    DemoteStackSlot = CLI.DAG.getFrameIndex(DemoteStackIdx, getFrameIndexTy(DL));
    ArgListEntry Entry;
    Entry.Node = DemoteStackSlot;
    Entry.Ty = StackSlotPtrType;
    Entry.IsSExt = false;
    Entry.IsZExt = false;
    Entry.IsInReg = false;
    Entry.IsSRet = true;
    Entry.IsNest = false;
    Entry.IsByVal = false;
    Entry.IsReturned = false;
    Entry.IsSwiftSelf = false;
    Entry.IsSwiftError = false;
    Entry.Alignment = Align;
    CLI.getArgs().insert(CLI.getArgs().begin(), Entry);
    CLI.NumFixedArgs += 1;
    CLI.RetTy = Type::getVoidTy(CLI.RetTy->getContext());

    // sret demotion isn't compatible with tail-calls, since the sret argument
    // points into the callers stack frame.
    CLI.IsTailCall = false;
  } else {
    for (unsigned I = 0, E = RetTys.size(); I != E; ++I) {
      EVT VT = RetTys[I];
      MVT RegisterVT = getRegisterTypeForCallingConv(CLI.RetTy->getContext(),
                                                     CLI.CallConv, VT);
      unsigned NumRegs = getNumRegistersForCallingConv(CLI.RetTy->getContext(),
                                                       CLI.CallConv, VT);
      for (unsigned i = 0; i != NumRegs; ++i) {
        ISD::InputArg MyFlags;
        MyFlags.VT = RegisterVT;
        MyFlags.ArgVT = VT;
        MyFlags.Used = CLI.IsReturnValueUsed;
        if (CLI.RetSExt)
          MyFlags.Flags.setSExt();
        if (CLI.RetZExt)
          MyFlags.Flags.setZExt();
        if (CLI.IsInReg)
          MyFlags.Flags.setInReg();
        CLI.Ins.push_back(MyFlags);
      }
    }
  }

  // We push in swifterror return as the last element of CLI.Ins.
  ArgListTy &Args = CLI.getArgs();
  if (supportSwiftError()) {
    for (unsigned i = 0, e = Args.size(); i != e; ++i) {
      if (Args[i].IsSwiftError) {
        ISD::InputArg MyFlags;
        MyFlags.VT = getPointerTy(DL);
        MyFlags.ArgVT = EVT(getPointerTy(DL));
        MyFlags.Flags.setSwiftError();
        CLI.Ins.push_back(MyFlags);
      }
    }
  }

  // Handle all of the outgoing arguments.
  CLI.Outs.clear();
  CLI.OutVals.clear();
  for (unsigned i = 0, e = Args.size(); i != e; ++i) {
    SmallVector<EVT, 4> ValueVTs;
    ComputeValueVTs(*this, DL, Args[i].Ty, ValueVTs);
    // FIXME: Split arguments if CLI.IsPostTypeLegalization
    Type *FinalType = Args[i].Ty;
    if (Args[i].IsByVal)
      FinalType = cast<PointerType>(Args[i].Ty)->getElementType();
    bool NeedsRegBlock = functionArgumentNeedsConsecutiveRegisters(
        FinalType, CLI.CallConv, CLI.IsVarArg);
    for (unsigned Value = 0, NumValues = ValueVTs.size(); Value != NumValues;
         ++Value) {
      EVT VT = ValueVTs[Value];
      Type *ArgTy = VT.getTypeForEVT(CLI.RetTy->getContext());
      SDValue Op = SDValue(Args[i].Node.getNode(),
                           Args[i].Node.getResNo() + Value);
      ISD::ArgFlagsTy Flags;

      // Certain targets (such as MIPS), may have a different ABI alignment
      // for a type depending on the context. Give the target a chance to
      // specify the alignment it wants.
      unsigned OriginalAlignment = getABIAlignmentForCallingConv(ArgTy, DL);

      if (Args[i].IsZExt)
        Flags.setZExt();
      if (Args[i].IsSExt)
        Flags.setSExt();
      if (Args[i].IsInReg) {
        // If we are using vectorcall calling convention, a structure that is
        // passed InReg - is surely an HVA
        if (CLI.CallConv == CallingConv::X86_VectorCall &&
            isa<StructType>(FinalType)) {
          // The first value of a structure is marked
          if (0 == Value)
            Flags.setHvaStart();
          Flags.setHva();
        }
        // Set InReg Flag
        Flags.setInReg();
      }
      if (Args[i].IsSRet)
        Flags.setSRet();
      if (Args[i].IsSwiftSelf)
        Flags.setSwiftSelf();
      if (Args[i].IsSwiftError)
        Flags.setSwiftError();
      if (Args[i].IsByVal)
        Flags.setByVal();
      if (Args[i].IsInAlloca) {
        Flags.setInAlloca();
        // Set the byval flag for CCAssignFn callbacks that don't know about
        // inalloca.  This way we can know how many bytes we should've allocated
        // and how many bytes a callee cleanup function will pop.  If we port
        // inalloca to more targets, we'll have to add custom inalloca handling
        // in the various CC lowering callbacks.
        Flags.setByVal();
      }
      if (Args[i].IsByVal || Args[i].IsInAlloca) {
        PointerType *Ty = cast<PointerType>(Args[i].Ty);
        Type *ElementTy = Ty->getElementType();
        Flags.setByValSize(DL.getTypeAllocSize(ElementTy));
        // For ByVal, alignment should come from FE.  BE will guess if this
        // info is not there but there are cases it cannot get right.
        unsigned FrameAlign;
        if (Args[i].Alignment)
          FrameAlign = Args[i].Alignment;
        else
          FrameAlign = getByValTypeAlignment(ElementTy, DL);
        Flags.setByValAlign(FrameAlign);
      }
      if (Args[i].IsNest)
        Flags.setNest();
      if (NeedsRegBlock)
        Flags.setInConsecutiveRegs();
      Flags.setOrigAlign(OriginalAlignment);

      MVT PartVT = getRegisterTypeForCallingConv(CLI.RetTy->getContext(),
                                                 CLI.CallConv, VT);
      unsigned NumParts = getNumRegistersForCallingConv(CLI.RetTy->getContext(),
                                                        CLI.CallConv, VT);
      SmallVector<SDValue, 4> Parts(NumParts);
      ISD::NodeType ExtendKind = ISD::ANY_EXTEND;

      if (Args[i].IsSExt)
        ExtendKind = ISD::SIGN_EXTEND;
      else if (Args[i].IsZExt)
        ExtendKind = ISD::ZERO_EXTEND;

      // Conservatively only handle 'returned' on non-vectors that can be lowered,
      // for now.
      if (Args[i].IsReturned && !Op.getValueType().isVector() &&
          CanLowerReturn) {
        assert(CLI.RetTy == Args[i].Ty && RetTys.size() == NumValues &&
               "unexpected use of 'returned'");
        // Before passing 'returned' to the target lowering code, ensure that
        // either the register MVT and the actual EVT are the same size or that
        // the return value and argument are extended in the same way; in these
        // cases it's safe to pass the argument register value unchanged as the
        // return register value (although it's at the target's option whether
        // to do so)
        // TODO: allow code generation to take advantage of partially preserved
        // registers rather than clobbering the entire register when the
        // parameter extension method is not compatible with the return
        // extension method
        if ((NumParts * PartVT.getSizeInBits() == VT.getSizeInBits()) ||
            (ExtendKind != ISD::ANY_EXTEND && CLI.RetSExt == Args[i].IsSExt &&
             CLI.RetZExt == Args[i].IsZExt))
          Flags.setReturned();
      }

      getCopyToParts(CLI.DAG, CLI.DL, Op, &Parts[0], NumParts, PartVT,
                     CLI.CS.getInstruction(), CLI.CallConv, ExtendKind);

      for (unsigned j = 0; j != NumParts; ++j) {
        // if it isn't first piece, alignment must be 1
        ISD::OutputArg MyFlags(Flags, Parts[j].getValueType(), VT,
                               i < CLI.NumFixedArgs,
                               i, j*Parts[j].getValueType().getStoreSize());
        if (NumParts > 1 && j == 0)
          MyFlags.Flags.setSplit();
        else if (j != 0) {
          MyFlags.Flags.setOrigAlign(1);
          if (j == NumParts - 1)
            MyFlags.Flags.setSplitEnd();
        }

        CLI.Outs.push_back(MyFlags);
        CLI.OutVals.push_back(Parts[j]);
      }

      if (NeedsRegBlock && Value == NumValues - 1)
        CLI.Outs[CLI.Outs.size() - 1].Flags.setInConsecutiveRegsLast();
    }
  }

  SmallVector<SDValue, 4> InVals;
  CLI.Chain = LowerCall(CLI, InVals);

  // Update CLI.InVals to use outside of this function.
  CLI.InVals = InVals;

  // Verify that the target's LowerCall behaved as expected.
  assert(CLI.Chain.getNode() && CLI.Chain.getValueType() == MVT::Other &&
         "LowerCall didn't return a valid chain!");
  assert((!CLI.IsTailCall || InVals.empty()) &&
         "LowerCall emitted a return value for a tail call!");
  assert((CLI.IsTailCall || InVals.size() == CLI.Ins.size()) &&
         "LowerCall didn't emit the correct number of values!");

  // For a tail call, the return value is merely live-out and there aren't
  // any nodes in the DAG representing it. Return a special value to
  // indicate that a tail call has been emitted and no more Instructions
  // should be processed in the current block.
  if (CLI.IsTailCall) {
    CLI.DAG.setRoot(CLI.Chain);
    return std::make_pair(SDValue(), SDValue());
  }

#ifndef NDEBUG
  for (unsigned i = 0, e = CLI.Ins.size(); i != e; ++i) {
    assert(InVals[i].getNode() && "LowerCall emitted a null value!");
    assert(EVT(CLI.Ins[i].VT) == InVals[i].getValueType() &&
           "LowerCall emitted a value with the wrong type!");
  }
#endif

  SmallVector<SDValue, 4> ReturnValues;
  if (!CanLowerReturn) {
    // The instruction result is the result of loading from the
    // hidden sret parameter.
    SmallVector<EVT, 1> PVTs;
    Type *PtrRetTy = OrigRetTy->getPointerTo(DL.getAllocaAddrSpace());

    ComputeValueVTs(*this, DL, PtrRetTy, PVTs);
    assert(PVTs.size() == 1 && "Pointers should fit in one register");
    EVT PtrVT = PVTs[0];

    unsigned NumValues = RetTys.size();
    ReturnValues.resize(NumValues);
    SmallVector<SDValue, 4> Chains(NumValues);

    // An aggregate return value cannot wrap around the address space, so
    // offsets to its parts don't wrap either.
    SDNodeFlags Flags;
    Flags.setNoUnsignedWrap(true);

    for (unsigned i = 0; i < NumValues; ++i) {
      SDValue Add = CLI.DAG.getNode(ISD::ADD, CLI.DL, PtrVT, DemoteStackSlot,
                                    CLI.DAG.getConstant(Offsets[i], CLI.DL,
                                                        PtrVT), Flags);
      SDValue L = CLI.DAG.getLoad(
          RetTys[i], CLI.DL, CLI.Chain, Add,
          MachinePointerInfo::getFixedStack(CLI.DAG.getMachineFunction(),
                                            DemoteStackIdx, Offsets[i]),
          /* Alignment = */ 1);
      ReturnValues[i] = L;
      Chains[i] = L.getValue(1);
    }

    CLI.Chain = CLI.DAG.getNode(ISD::TokenFactor, CLI.DL, MVT::Other, Chains);
  } else {
    // Collect the legal value parts into potentially illegal values
    // that correspond to the original function's return values.
    Optional<ISD::NodeType> AssertOp;
    if (CLI.RetSExt)
      AssertOp = ISD::AssertSext;
    else if (CLI.RetZExt)
      AssertOp = ISD::AssertZext;
    unsigned CurReg = 0;
    for (unsigned I = 0, E = RetTys.size(); I != E; ++I) {
      EVT VT = RetTys[I];
      MVT RegisterVT = getRegisterTypeForCallingConv(CLI.RetTy->getContext(),
                                                     CLI.CallConv, VT);
      unsigned NumRegs = getNumRegistersForCallingConv(CLI.RetTy->getContext(),
                                                       CLI.CallConv, VT);

      ReturnValues.push_back(getCopyFromParts(CLI.DAG, CLI.DL, &InVals[CurReg],
                                              NumRegs, RegisterVT, VT, nullptr,
                                              CLI.CallConv, AssertOp));
      CurReg += NumRegs;
    }

    // For a function returning void, there is no return value. We can't create
    // such a node, so we just return a null return value in that case. In
    // that case, nothing will actually look at the value.
    if (ReturnValues.empty())
      return std::make_pair(SDValue(), CLI.Chain);
  }

  SDValue Res = CLI.DAG.getNode(ISD::MERGE_VALUES, CLI.DL,
                                CLI.DAG.getVTList(RetTys), ReturnValues);
  return std::make_pair(Res, CLI.Chain);
}

void TargetLowering::LowerOperationWrapper(SDNode *N,
                                           SmallVectorImpl<SDValue> &Results,
                                           SelectionDAG &DAG) const {
  if (SDValue Res = LowerOperation(SDValue(N, 0), DAG))
    Results.push_back(Res);
}

SDValue TargetLowering::LowerOperation(SDValue Op, SelectionDAG &DAG) const {
  llvm_unreachable("LowerOperation not implemented for this target!");
}

void
SelectionDAGBuilder::CopyValueToVirtualRegister(const Value *V, unsigned Reg) {
  SDValue Op = getNonRegisterValue(V);
  assert((Op.getOpcode() != ISD::CopyFromReg ||
          cast<RegisterSDNode>(Op.getOperand(1))->getReg() != Reg) &&
         "Copy from a reg to the same reg!");
  assert(!TargetRegisterInfo::isPhysicalRegister(Reg) && "Is a physreg");

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  // If this is an InlineAsm we have to match the registers required, not the
  // notional registers required by the type.

  RegsForValue RFV(V->getContext(), TLI, DAG.getDataLayout(), Reg, V->getType(),
                   None); // This is not an ABI copy.
  SDValue Chain = DAG.getEntryNode();

  ISD::NodeType ExtendType = (FuncInfo.PreferredExtendType.find(V) ==
                              FuncInfo.PreferredExtendType.end())
                                 ? ISD::ANY_EXTEND
                                 : FuncInfo.PreferredExtendType[V];
  RFV.getCopyToRegs(Op, DAG, getCurSDLoc(), Chain, nullptr, V, ExtendType);
  PendingExports.push_back(Chain);
}

#include "llvm/CodeGen/SelectionDAGISel.h"

/// isOnlyUsedInEntryBlock - If the specified argument is only used in the
/// entry block, return true.  This includes arguments used by switches, since
/// the switch may expand into multiple basic blocks.
static bool isOnlyUsedInEntryBlock(const Argument *A, bool FastISel) {
  // With FastISel active, we may be splitting blocks, so force creation
  // of virtual registers for all non-dead arguments.
  if (FastISel)
    return A->use_empty();

  const BasicBlock &Entry = A->getParent()->front();
  for (const User *U : A->users())
    if (cast<Instruction>(U)->getParent() != &Entry || isa<SwitchInst>(U))
      return false;  // Use not in entry block.

  return true;
}

using ArgCopyElisionMapTy =
    DenseMap<const Argument *,
             std::pair<const AllocaInst *, const StoreInst *>>;

/// Scan the entry block of the function in FuncInfo for arguments that look
/// like copies into a local alloca. Record any copied arguments in
/// ArgCopyElisionCandidates.
static void
findArgumentCopyElisionCandidates(const DataLayout &DL,
                                  FunctionLoweringInfo *FuncInfo,
                                  ArgCopyElisionMapTy &ArgCopyElisionCandidates) {
  // Record the state of every static alloca used in the entry block. Argument
  // allocas are all used in the entry block, so we need approximately as many
  // entries as we have arguments.
  enum StaticAllocaInfo { Unknown, Clobbered, Elidable };
  SmallDenseMap<const AllocaInst *, StaticAllocaInfo, 8> StaticAllocas;
  unsigned NumArgs = FuncInfo->Fn->arg_size();
  StaticAllocas.reserve(NumArgs * 2);

  auto GetInfoIfStaticAlloca = [&](const Value *V) -> StaticAllocaInfo * {
    if (!V)
      return nullptr;
    V = V->stripPointerCasts();
    const auto *AI = dyn_cast<AllocaInst>(V);
    if (!AI || !AI->isStaticAlloca() || !FuncInfo->StaticAllocaMap.count(AI))
      return nullptr;
    auto Iter = StaticAllocas.insert({AI, Unknown});
    return &Iter.first->second;
  };

  // Look for stores of arguments to static allocas. Look through bitcasts and
  // GEPs to handle type coercions, as long as the alloca is fully initialized
  // by the store. Any non-store use of an alloca escapes it and any subsequent
  // unanalyzed store might write it.
  // FIXME: Handle structs initialized with multiple stores.
  for (const Instruction &I : FuncInfo->Fn->getEntryBlock()) {
    // Look for stores, and handle non-store uses conservatively.
    const auto *SI = dyn_cast<StoreInst>(&I);
    if (!SI) {
      // We will look through cast uses, so ignore them completely.
      if (I.isCast())
        continue;
      // Ignore debug info intrinsics, they don't escape or store to allocas.
      if (isa<DbgInfoIntrinsic>(I))
        continue;
      // This is an unknown instruction. Assume it escapes or writes to all
      // static alloca operands.
      for (const Use &U : I.operands()) {
        if (StaticAllocaInfo *Info = GetInfoIfStaticAlloca(U))
          *Info = StaticAllocaInfo::Clobbered;
      }
      continue;
    }

    // If the stored value is a static alloca, mark it as escaped.
    if (StaticAllocaInfo *Info = GetInfoIfStaticAlloca(SI->getValueOperand()))
      *Info = StaticAllocaInfo::Clobbered;

    // Check if the destination is a static alloca.
    const Value *Dst = SI->getPointerOperand()->stripPointerCasts();
    StaticAllocaInfo *Info = GetInfoIfStaticAlloca(Dst);
    if (!Info)
      continue;
    const AllocaInst *AI = cast<AllocaInst>(Dst);

    // Skip allocas that have been initialized or clobbered.
    if (*Info != StaticAllocaInfo::Unknown)
      continue;

    // Check if the stored value is an argument, and that this store fully
    // initializes the alloca. Don't elide copies from the same argument twice.
    const Value *Val = SI->getValueOperand()->stripPointerCasts();
    const auto *Arg = dyn_cast<Argument>(Val);
    if (!Arg || Arg->hasInAllocaAttr() || Arg->hasByValAttr() ||
        Arg->getType()->isEmptyTy() ||
        DL.getTypeStoreSize(Arg->getType()) !=
            DL.getTypeAllocSize(AI->getAllocatedType()) ||
        ArgCopyElisionCandidates.count(Arg)) {
      *Info = StaticAllocaInfo::Clobbered;
      continue;
    }

    LLVM_DEBUG(dbgs() << "Found argument copy elision candidate: " << *AI
                      << '\n');

    // Mark this alloca and store for argument copy elision.
    *Info = StaticAllocaInfo::Elidable;
    ArgCopyElisionCandidates.insert({Arg, {AI, SI}});

    // Stop scanning if we've seen all arguments. This will happen early in -O0
    // builds, which is useful, because -O0 builds have large entry blocks and
    // many allocas.
    if (ArgCopyElisionCandidates.size() == NumArgs)
      break;
  }
}

/// Try to elide argument copies from memory into a local alloca. Succeeds if
/// ArgVal is a load from a suitable fixed stack object.
static void tryToElideArgumentCopy(
    FunctionLoweringInfo *FuncInfo, SmallVectorImpl<SDValue> &Chains,
    DenseMap<int, int> &ArgCopyElisionFrameIndexMap,
    SmallPtrSetImpl<const Instruction *> &ElidedArgCopyInstrs,
    ArgCopyElisionMapTy &ArgCopyElisionCandidates, const Argument &Arg,
    SDValue ArgVal, bool &ArgHasUses) {
  // Check if this is a load from a fixed stack object.
  auto *LNode = dyn_cast<LoadSDNode>(ArgVal);
  if (!LNode)
    return;
  auto *FINode = dyn_cast<FrameIndexSDNode>(LNode->getBasePtr().getNode());
  if (!FINode)
    return;

  // Check that the fixed stack object is the right size and alignment.
  // Look at the alignment that the user wrote on the alloca instead of looking
  // at the stack object.
  auto ArgCopyIter = ArgCopyElisionCandidates.find(&Arg);
  assert(ArgCopyIter != ArgCopyElisionCandidates.end());
  const AllocaInst *AI = ArgCopyIter->second.first;
  int FixedIndex = FINode->getIndex();
  int &AllocaIndex = FuncInfo->StaticAllocaMap[AI];
  int OldIndex = AllocaIndex;
  MachineFrameInfo &MFI = FuncInfo->MF->getFrameInfo();
  if (MFI.getObjectSize(FixedIndex) != MFI.getObjectSize(OldIndex)) {
    LLVM_DEBUG(
        dbgs() << "  argument copy elision failed due to bad fixed stack "
                  "object size\n");
    return;
  }
  unsigned RequiredAlignment = AI->getAlignment();
  if (!RequiredAlignment) {
    RequiredAlignment = FuncInfo->MF->getDataLayout().getABITypeAlignment(
        AI->getAllocatedType());
  }
  if (MFI.getObjectAlignment(FixedIndex) < RequiredAlignment) {
    LLVM_DEBUG(dbgs() << "  argument copy elision failed: alignment of alloca "
                         "greater than stack argument alignment ("
                      << RequiredAlignment << " vs "
                      << MFI.getObjectAlignment(FixedIndex) << ")\n");
    return;
  }

  // Perform the elision. Delete the old stack object and replace its only use
  // in the variable info map. Mark the stack object as mutable.
  LLVM_DEBUG({
    dbgs() << "Eliding argument copy from " << Arg << " to " << *AI << '\n'
           << "  Replacing frame index " << OldIndex << " with " << FixedIndex
           << '\n';
  });
  MFI.RemoveStackObject(OldIndex);
  MFI.setIsImmutableObjectIndex(FixedIndex, false);
  AllocaIndex = FixedIndex;
  ArgCopyElisionFrameIndexMap.insert({OldIndex, FixedIndex});
  Chains.push_back(ArgVal.getValue(1));

  // Avoid emitting code for the store implementing the copy.
  const StoreInst *SI = ArgCopyIter->second.second;
  ElidedArgCopyInstrs.insert(SI);

  // Check for uses of the argument again so that we can avoid exporting ArgVal
  // if it is't used by anything other than the store.
  for (const Value *U : Arg.users()) {
    if (U != SI) {
      ArgHasUses = true;
      break;
    }
  }
}

void SelectionDAGISel::LowerArguments(const Function &F) {
  SelectionDAG &DAG = SDB->DAG;
  SDLoc dl = SDB->getCurSDLoc();
  const DataLayout &DL = DAG.getDataLayout();
  SmallVector<ISD::InputArg, 16> Ins;

  if (!FuncInfo->CanLowerReturn) {
    // Put in an sret pointer parameter before all the other parameters.
    SmallVector<EVT, 1> ValueVTs;
    ComputeValueVTs(*TLI, DAG.getDataLayout(),
                    F.getReturnType()->getPointerTo(
                        DAG.getDataLayout().getAllocaAddrSpace()),
                    ValueVTs);

    // NOTE: Assuming that a pointer will never break down to more than one VT
    // or one register.
    ISD::ArgFlagsTy Flags;
    Flags.setSRet();
    MVT RegisterVT = TLI->getRegisterType(*DAG.getContext(), ValueVTs[0]);
    ISD::InputArg RetArg(Flags, RegisterVT, ValueVTs[0], true,
                         ISD::InputArg::NoArgIndex, 0);
    Ins.push_back(RetArg);
  }

  // Look for stores of arguments to static allocas. Mark such arguments with a
  // flag to ask the target to give us the memory location of that argument if
  // available.
  ArgCopyElisionMapTy ArgCopyElisionCandidates;
  findArgumentCopyElisionCandidates(DL, FuncInfo, ArgCopyElisionCandidates);

  // Set up the incoming argument description vector.
  for (const Argument &Arg : F.args()) {
    unsigned ArgNo = Arg.getArgNo();
    SmallVector<EVT, 4> ValueVTs;
    ComputeValueVTs(*TLI, DAG.getDataLayout(), Arg.getType(), ValueVTs);
    bool isArgValueUsed = !Arg.use_empty();
    unsigned PartBase = 0;
    Type *FinalType = Arg.getType();
    if (Arg.hasAttribute(Attribute::ByVal))
      FinalType = cast<PointerType>(FinalType)->getElementType();
    bool NeedsRegBlock = TLI->functionArgumentNeedsConsecutiveRegisters(
        FinalType, F.getCallingConv(), F.isVarArg());
    for (unsigned Value = 0, NumValues = ValueVTs.size();
         Value != NumValues; ++Value) {
      EVT VT = ValueVTs[Value];
      Type *ArgTy = VT.getTypeForEVT(*DAG.getContext());
      ISD::ArgFlagsTy Flags;

      // Certain targets (such as MIPS), may have a different ABI alignment
      // for a type depending on the context. Give the target a chance to
      // specify the alignment it wants.
      unsigned OriginalAlignment =
          TLI->getABIAlignmentForCallingConv(ArgTy, DL);

      if (Arg.hasAttribute(Attribute::ZExt))
        Flags.setZExt();
      if (Arg.hasAttribute(Attribute::SExt))
        Flags.setSExt();
      if (Arg.hasAttribute(Attribute::InReg)) {
        // If we are using vectorcall calling convention, a structure that is
        // passed InReg - is surely an HVA
        if (F.getCallingConv() == CallingConv::X86_VectorCall &&
            isa<StructType>(Arg.getType())) {
          // The first value of a structure is marked
          if (0 == Value)
            Flags.setHvaStart();
          Flags.setHva();
        }
        // Set InReg Flag
        Flags.setInReg();
      }
      if (Arg.hasAttribute(Attribute::StructRet))
        Flags.setSRet();
      if (Arg.hasAttribute(Attribute::SwiftSelf))
        Flags.setSwiftSelf();
      if (Arg.hasAttribute(Attribute::SwiftError))
        Flags.setSwiftError();
      if (Arg.hasAttribute(Attribute::ByVal))
        Flags.setByVal();
      if (Arg.hasAttribute(Attribute::InAlloca)) {
        Flags.setInAlloca();
        // Set the byval flag for CCAssignFn callbacks that don't know about
        // inalloca.  This way we can know how many bytes we should've allocated
        // and how many bytes a callee cleanup function will pop.  If we port
        // inalloca to more targets, we'll have to add custom inalloca handling
        // in the various CC lowering callbacks.
        Flags.setByVal();
      }
      if (F.getCallingConv() == CallingConv::X86_INTR) {
        // IA Interrupt passes frame (1st parameter) by value in the stack.
        if (ArgNo == 0)
          Flags.setByVal();
      }
      if (Flags.isByVal() || Flags.isInAlloca()) {
        PointerType *Ty = cast<PointerType>(Arg.getType());
        Type *ElementTy = Ty->getElementType();
        Flags.setByValSize(DL.getTypeAllocSize(ElementTy));
        // For ByVal, alignment should be passed from FE.  BE will guess if
        // this info is not there but there are cases it cannot get right.
        unsigned FrameAlign;
        if (Arg.getParamAlignment())
          FrameAlign = Arg.getParamAlignment();
        else
          FrameAlign = TLI->getByValTypeAlignment(ElementTy, DL);
        Flags.setByValAlign(FrameAlign);
      }
      if (Arg.hasAttribute(Attribute::Nest))
        Flags.setNest();
      if (NeedsRegBlock)
        Flags.setInConsecutiveRegs();
      Flags.setOrigAlign(OriginalAlignment);
      if (ArgCopyElisionCandidates.count(&Arg))
        Flags.setCopyElisionCandidate();

      MVT RegisterVT = TLI->getRegisterTypeForCallingConv(
          *CurDAG->getContext(), F.getCallingConv(), VT);
      unsigned NumRegs = TLI->getNumRegistersForCallingConv(
          *CurDAG->getContext(), F.getCallingConv(), VT);
      for (unsigned i = 0; i != NumRegs; ++i) {
        ISD::InputArg MyFlags(Flags, RegisterVT, VT, isArgValueUsed,
                              ArgNo, PartBase+i*RegisterVT.getStoreSize());
        if (NumRegs > 1 && i == 0)
          MyFlags.Flags.setSplit();
        // if it isn't first piece, alignment must be 1
        else if (i > 0) {
          MyFlags.Flags.setOrigAlign(1);
          if (i == NumRegs - 1)
            MyFlags.Flags.setSplitEnd();
        }
        Ins.push_back(MyFlags);
      }
      if (NeedsRegBlock && Value == NumValues - 1)
        Ins[Ins.size() - 1].Flags.setInConsecutiveRegsLast();
      PartBase += VT.getStoreSize();
    }
  }

  // Call the target to set up the argument values.
  SmallVector<SDValue, 8> InVals;
  SDValue NewRoot = TLI->LowerFormalArguments(
      DAG.getRoot(), F.getCallingConv(), F.isVarArg(), Ins, dl, DAG, InVals);

  // Verify that the target's LowerFormalArguments behaved as expected.
  assert(NewRoot.getNode() && NewRoot.getValueType() == MVT::Other &&
         "LowerFormalArguments didn't return a valid chain!");
  assert(InVals.size() == Ins.size() &&
         "LowerFormalArguments didn't emit the correct number of values!");
  LLVM_DEBUG({
    for (unsigned i = 0, e = Ins.size(); i != e; ++i) {
      assert(InVals[i].getNode() &&
             "LowerFormalArguments emitted a null value!");
      assert(EVT(Ins[i].VT) == InVals[i].getValueType() &&
             "LowerFormalArguments emitted a value with the wrong type!");
    }
  });

  // Update the DAG with the new chain value resulting from argument lowering.
  DAG.setRoot(NewRoot);

  // Set up the argument values.
  unsigned i = 0;
  if (!FuncInfo->CanLowerReturn) {
    // Create a virtual register for the sret pointer, and put in a copy
    // from the sret argument into it.
    SmallVector<EVT, 1> ValueVTs;
    ComputeValueVTs(*TLI, DAG.getDataLayout(),
                    F.getReturnType()->getPointerTo(
                        DAG.getDataLayout().getAllocaAddrSpace()),
                    ValueVTs);
    MVT VT = ValueVTs[0].getSimpleVT();
    MVT RegVT = TLI->getRegisterType(*CurDAG->getContext(), VT);
    Optional<ISD::NodeType> AssertOp = None;
    SDValue ArgValue = getCopyFromParts(DAG, dl, &InVals[0], 1, RegVT, VT,
                                        nullptr, F.getCallingConv(), AssertOp);

    MachineFunction& MF = SDB->DAG.getMachineFunction();
    MachineRegisterInfo& RegInfo = MF.getRegInfo();
    unsigned SRetReg = RegInfo.createVirtualRegister(TLI->getRegClassFor(RegVT));
    FuncInfo->DemoteRegister = SRetReg;
    NewRoot =
        SDB->DAG.getCopyToReg(NewRoot, SDB->getCurSDLoc(), SRetReg, ArgValue);
    DAG.setRoot(NewRoot);

    // i indexes lowered arguments.  Bump it past the hidden sret argument.
    ++i;
  }

  SmallVector<SDValue, 4> Chains;
  DenseMap<int, int> ArgCopyElisionFrameIndexMap;
  for (const Argument &Arg : F.args()) {
    SmallVector<SDValue, 4> ArgValues;
    SmallVector<EVT, 4> ValueVTs;
    ComputeValueVTs(*TLI, DAG.getDataLayout(), Arg.getType(), ValueVTs);
    unsigned NumValues = ValueVTs.size();
    if (NumValues == 0)
      continue;

    bool ArgHasUses = !Arg.use_empty();

    // Elide the copying store if the target loaded this argument from a
    // suitable fixed stack object.
    if (Ins[i].Flags.isCopyElisionCandidate()) {
      tryToElideArgumentCopy(FuncInfo, Chains, ArgCopyElisionFrameIndexMap,
                             ElidedArgCopyInstrs, ArgCopyElisionCandidates, Arg,
                             InVals[i], ArgHasUses);
    }

    // If this argument is unused then remember its value. It is used to generate
    // debugging information.
    bool isSwiftErrorArg =
        TLI->supportSwiftError() &&
        Arg.hasAttribute(Attribute::SwiftError);
    if (!ArgHasUses && !isSwiftErrorArg) {
      SDB->setUnusedArgValue(&Arg, InVals[i]);

      // Also remember any frame index for use in FastISel.
      if (FrameIndexSDNode *FI =
          dyn_cast<FrameIndexSDNode>(InVals[i].getNode()))
        FuncInfo->setArgumentFrameIndex(&Arg, FI->getIndex());
    }

    for (unsigned Val = 0; Val != NumValues; ++Val) {
      EVT VT = ValueVTs[Val];
      MVT PartVT = TLI->getRegisterTypeForCallingConv(*CurDAG->getContext(),
                                                      F.getCallingConv(), VT);
      unsigned NumParts = TLI->getNumRegistersForCallingConv(
          *CurDAG->getContext(), F.getCallingConv(), VT);

      // Even an apparant 'unused' swifterror argument needs to be returned. So
      // we do generate a copy for it that can be used on return from the
      // function.
      if (ArgHasUses || isSwiftErrorArg) {
        Optional<ISD::NodeType> AssertOp;
        if (Arg.hasAttribute(Attribute::SExt))
          AssertOp = ISD::AssertSext;
        else if (Arg.hasAttribute(Attribute::ZExt))
          AssertOp = ISD::AssertZext;

        ArgValues.push_back(getCopyFromParts(DAG, dl, &InVals[i], NumParts,
                                             PartVT, VT, nullptr,
                                             F.getCallingConv(), AssertOp));
      }

      i += NumParts;
    }

    // We don't need to do anything else for unused arguments.
    if (ArgValues.empty())
      continue;

    // Note down frame index.
    if (FrameIndexSDNode *FI =
        dyn_cast<FrameIndexSDNode>(ArgValues[0].getNode()))
      FuncInfo->setArgumentFrameIndex(&Arg, FI->getIndex());

    SDValue Res = DAG.getMergeValues(makeArrayRef(ArgValues.data(), NumValues),
                                     SDB->getCurSDLoc());

    SDB->setValue(&Arg, Res);
    if (!TM.Options.EnableFastISel && Res.getOpcode() == ISD::BUILD_PAIR) {
      // We want to associate the argument with the frame index, among
      // involved operands, that correspond to the lowest address. The
      // getCopyFromParts function, called earlier, is swapping the order of
      // the operands to BUILD_PAIR depending on endianness. The result of
      // that swapping is that the least significant bits of the argument will
      // be in the first operand of the BUILD_PAIR node, and the most
      // significant bits will be in the second operand.
      unsigned LowAddressOp = DAG.getDataLayout().isBigEndian() ? 1 : 0;
      if (LoadSDNode *LNode =
          dyn_cast<LoadSDNode>(Res.getOperand(LowAddressOp).getNode()))
        if (FrameIndexSDNode *FI =
            dyn_cast<FrameIndexSDNode>(LNode->getBasePtr().getNode()))
          FuncInfo->setArgumentFrameIndex(&Arg, FI->getIndex());
    }

    // Update the SwiftErrorVRegDefMap.
    if (Res.getOpcode() == ISD::CopyFromReg && isSwiftErrorArg) {
      unsigned Reg = cast<RegisterSDNode>(Res.getOperand(1))->getReg();
      if (TargetRegisterInfo::isVirtualRegister(Reg))
        FuncInfo->setCurrentSwiftErrorVReg(FuncInfo->MBB,
                                           FuncInfo->SwiftErrorArg, Reg);
    }

    // If this argument is live outside of the entry block, insert a copy from
    // wherever we got it to the vreg that other BB's will reference it as.
    if (!TM.Options.EnableFastISel && Res.getOpcode() == ISD::CopyFromReg) {
      // If we can, though, try to skip creating an unnecessary vreg.
      // FIXME: This isn't very clean... it would be nice to make this more
      // general.  It's also subtly incompatible with the hacks FastISel
      // uses with vregs.
      unsigned Reg = cast<RegisterSDNode>(Res.getOperand(1))->getReg();
      if (TargetRegisterInfo::isVirtualRegister(Reg)) {
        FuncInfo->ValueMap[&Arg] = Reg;
        continue;
      }
    }
    if (!isOnlyUsedInEntryBlock(&Arg, TM.Options.EnableFastISel)) {
      FuncInfo->InitializeRegForValue(&Arg);
      SDB->CopyToExportRegsIfNeeded(&Arg);
    }
  }

  if (!Chains.empty()) {
    Chains.push_back(NewRoot);
    NewRoot = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Chains);
  }

  DAG.setRoot(NewRoot);

  assert(i == InVals.size() && "Argument register count mismatch!");

  // If any argument copy elisions occurred and we have debug info, update the
  // stale frame indices used in the dbg.declare variable info table.
  MachineFunction::VariableDbgInfoMapTy &DbgDeclareInfo = MF->getVariableDbgInfo();
  if (!DbgDeclareInfo.empty() && !ArgCopyElisionFrameIndexMap.empty()) {
    for (MachineFunction::VariableDbgInfo &VI : DbgDeclareInfo) {
      auto I = ArgCopyElisionFrameIndexMap.find(VI.Slot);
      if (I != ArgCopyElisionFrameIndexMap.end())
        VI.Slot = I->second;
    }
  }

  // Finally, if the target has anything special to do, allow it to do so.
  EmitFunctionEntryCode();
}

/// Handle PHI nodes in successor blocks.  Emit code into the SelectionDAG to
/// ensure constants are generated when needed.  Remember the virtual registers
/// that need to be added to the Machine PHI nodes as input.  We cannot just
/// directly add them, because expansion might result in multiple MBB's for one
/// BB.  As such, the start of the BB might correspond to a different MBB than
/// the end.
void
SelectionDAGBuilder::HandlePHINodesInSuccessorBlocks(const BasicBlock *LLVMBB) {
  const Instruction *TI = LLVMBB->getTerminator();

  SmallPtrSet<MachineBasicBlock *, 4> SuccsHandled;

  // Check PHI nodes in successors that expect a value to be available from this
  // block.
  for (unsigned succ = 0, e = TI->getNumSuccessors(); succ != e; ++succ) {
    const BasicBlock *SuccBB = TI->getSuccessor(succ);
    if (!isa<PHINode>(SuccBB->begin())) continue;
    MachineBasicBlock *SuccMBB = FuncInfo.MBBMap[SuccBB];

    // If this terminator has multiple identical successors (common for
    // switches), only handle each succ once.
    if (!SuccsHandled.insert(SuccMBB).second)
      continue;

    MachineBasicBlock::iterator MBBI = SuccMBB->begin();

    // At this point we know that there is a 1-1 correspondence between LLVM PHI
    // nodes and Machine PHI nodes, but the incoming operands have not been
    // emitted yet.
    for (const PHINode &PN : SuccBB->phis()) {
      // Ignore dead phi's.
      if (PN.use_empty())
        continue;

      // Skip empty types
      if (PN.getType()->isEmptyTy())
        continue;

      unsigned Reg;
      const Value *PHIOp = PN.getIncomingValueForBlock(LLVMBB);

      if (const Constant *C = dyn_cast<Constant>(PHIOp)) {
        unsigned &RegOut = ConstantsOut[C];
        if (RegOut == 0) {
          RegOut = FuncInfo.CreateRegs(C->getType());
          CopyValueToVirtualRegister(C, RegOut);
        }
        Reg = RegOut;
      } else {
        DenseMap<const Value *, unsigned>::iterator I =
          FuncInfo.ValueMap.find(PHIOp);
        if (I != FuncInfo.ValueMap.end())
          Reg = I->second;
        else {
          assert(isa<AllocaInst>(PHIOp) &&
                 FuncInfo.StaticAllocaMap.count(cast<AllocaInst>(PHIOp)) &&
                 "Didn't codegen value into a register!??");
          Reg = FuncInfo.CreateRegs(PHIOp->getType());
          CopyValueToVirtualRegister(PHIOp, Reg);
        }
      }

      // Remember that this register needs to added to the machine PHI node as
      // the input for this MBB.
      SmallVector<EVT, 4> ValueVTs;
      const TargetLowering &TLI = DAG.getTargetLoweringInfo();
      ComputeValueVTs(TLI, DAG.getDataLayout(), PN.getType(), ValueVTs);
      for (unsigned vti = 0, vte = ValueVTs.size(); vti != vte; ++vti) {
        EVT VT = ValueVTs[vti];
        unsigned NumRegisters = TLI.getNumRegisters(*DAG.getContext(), VT);
        for (unsigned i = 0, e = NumRegisters; i != e; ++i)
          FuncInfo.PHINodesToUpdate.push_back(
              std::make_pair(&*MBBI++, Reg + i));
        Reg += NumRegisters;
      }
    }
  }

  ConstantsOut.clear();
}

/// Add a successor MBB to ParentMBB< creating a new MachineBB for BB if SuccMBB
/// is 0.
MachineBasicBlock *
SelectionDAGBuilder::StackProtectorDescriptor::
AddSuccessorMBB(const BasicBlock *BB,
                MachineBasicBlock *ParentMBB,
                bool IsLikely,
                MachineBasicBlock *SuccMBB) {
  // If SuccBB has not been created yet, create it.
  if (!SuccMBB) {
    MachineFunction *MF = ParentMBB->getParent();
    MachineFunction::iterator BBI(ParentMBB);
    SuccMBB = MF->CreateMachineBasicBlock(BB);
    MF->insert(++BBI, SuccMBB);
  }
  // Add it as a successor of ParentMBB.
  ParentMBB->addSuccessor(
      SuccMBB, BranchProbabilityInfo::getBranchProbStackProtector(IsLikely));
  return SuccMBB;
}

MachineBasicBlock *SelectionDAGBuilder::NextBlock(MachineBasicBlock *MBB) {
  MachineFunction::iterator I(MBB);
  if (++I == FuncInfo.MF->end())
    return nullptr;
  return &*I;
}

/// During lowering new call nodes can be created (such as memset, etc.).
/// Those will become new roots of the current DAG, but complications arise
/// when they are tail calls. In such cases, the call lowering will update
/// the root, but the builder still needs to know that a tail call has been
/// lowered in order to avoid generating an additional return.
void SelectionDAGBuilder::updateDAGForMaybeTailCall(SDValue MaybeTC) {
  // If the node is null, we do have a tail call.
  if (MaybeTC.getNode() != nullptr)
    DAG.setRoot(MaybeTC);
  else
    HasTailCall = true;
}

uint64_t
SelectionDAGBuilder::getJumpTableRange(const CaseClusterVector &Clusters,
                                       unsigned First, unsigned Last) const {
  assert(Last >= First);
  const APInt &LowCase = Clusters[First].Low->getValue();
  const APInt &HighCase = Clusters[Last].High->getValue();
  assert(LowCase.getBitWidth() == HighCase.getBitWidth());

  // FIXME: A range of consecutive cases has 100% density, but only requires one
  // comparison to lower. We should discriminate against such consecutive ranges
  // in jump tables.

  return (HighCase - LowCase).getLimitedValue((UINT64_MAX - 1) / 100) + 1;
}

uint64_t SelectionDAGBuilder::getJumpTableNumCases(
    const SmallVectorImpl<unsigned> &TotalCases, unsigned First,
    unsigned Last) const {
  assert(Last >= First);
  assert(TotalCases[Last] >= TotalCases[First]);
  uint64_t NumCases =
      TotalCases[Last] - (First == 0 ? 0 : TotalCases[First - 1]);
  return NumCases;
}

bool SelectionDAGBuilder::buildJumpTable(const CaseClusterVector &Clusters,
                                         unsigned First, unsigned Last,
                                         const SwitchInst *SI,
                                         MachineBasicBlock *DefaultMBB,
                                         CaseCluster &JTCluster) {
  assert(First <= Last);

  auto Prob = BranchProbability::getZero();
  unsigned NumCmps = 0;
  std::vector<MachineBasicBlock*> Table;
  DenseMap<MachineBasicBlock*, BranchProbability> JTProbs;

  // Initialize probabilities in JTProbs.
  for (unsigned I = First; I <= Last; ++I)
    JTProbs[Clusters[I].MBB] = BranchProbability::getZero();

  for (unsigned I = First; I <= Last; ++I) {
    assert(Clusters[I].Kind == CC_Range);
    Prob += Clusters[I].Prob;
    const APInt &Low = Clusters[I].Low->getValue();
    const APInt &High = Clusters[I].High->getValue();
    NumCmps += (Low == High) ? 1 : 2;
    if (I != First) {
      // Fill the gap between this and the previous cluster.
      const APInt &PreviousHigh = Clusters[I - 1].High->getValue();
      assert(PreviousHigh.slt(Low));
      uint64_t Gap = (Low - PreviousHigh).getLimitedValue() - 1;
      for (uint64_t J = 0; J < Gap; J++)
        Table.push_back(DefaultMBB);
    }
    uint64_t ClusterSize = (High - Low).getLimitedValue() + 1;
    for (uint64_t J = 0; J < ClusterSize; ++J)
      Table.push_back(Clusters[I].MBB);
    JTProbs[Clusters[I].MBB] += Clusters[I].Prob;
  }

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  unsigned NumDests = JTProbs.size();
  if (TLI.isSuitableForBitTests(
          NumDests, NumCmps, Clusters[First].Low->getValue(),
          Clusters[Last].High->getValue(), DAG.getDataLayout())) {
    // Clusters[First..Last] should be lowered as bit tests instead.
    return false;
  }

  // Create the MBB that will load from and jump through the table.
  // Note: We create it here, but it's not inserted into the function yet.
  MachineFunction *CurMF = FuncInfo.MF;
  MachineBasicBlock *JumpTableMBB =
      CurMF->CreateMachineBasicBlock(SI->getParent());

  // Add successors. Note: use table order for determinism.
  SmallPtrSet<MachineBasicBlock *, 8> Done;
  for (MachineBasicBlock *Succ : Table) {
    if (Done.count(Succ))
      continue;
    addSuccessorWithProb(JumpTableMBB, Succ, JTProbs[Succ]);
    Done.insert(Succ);
  }
  JumpTableMBB->normalizeSuccProbs();

  unsigned JTI = CurMF->getOrCreateJumpTableInfo(TLI.getJumpTableEncoding())
                     ->createJumpTableIndex(Table);

  // Set up the jump table info.
  JumpTable JT(-1U, JTI, JumpTableMBB, nullptr);
  JumpTableHeader JTH(Clusters[First].Low->getValue(),
                      Clusters[Last].High->getValue(), SI->getCondition(),
                      nullptr, false);
  JTCases.emplace_back(std::move(JTH), std::move(JT));

  JTCluster = CaseCluster::jumpTable(Clusters[First].Low, Clusters[Last].High,
                                     JTCases.size() - 1, Prob);
  return true;
}

void SelectionDAGBuilder::findJumpTables(CaseClusterVector &Clusters,
                                         const SwitchInst *SI,
                                         MachineBasicBlock *DefaultMBB) {
#ifndef NDEBUG
  // Clusters must be non-empty, sorted, and only contain Range clusters.
  assert(!Clusters.empty());
  for (CaseCluster &C : Clusters)
    assert(C.Kind == CC_Range);
  for (unsigned i = 1, e = Clusters.size(); i < e; ++i)
    assert(Clusters[i - 1].High->getValue().slt(Clusters[i].Low->getValue()));
#endif

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  if (!TLI.areJTsAllowed(SI->getParent()->getParent()))
    return;

  const int64_t N = Clusters.size();
  const unsigned MinJumpTableEntries = TLI.getMinimumJumpTableEntries();
  const unsigned SmallNumberOfEntries = MinJumpTableEntries / 2;

  if (N < 2 || N < MinJumpTableEntries)
    return;

  // TotalCases[i]: Total nbr of cases in Clusters[0..i].
  SmallVector<unsigned, 8> TotalCases(N);
  for (unsigned i = 0; i < N; ++i) {
    const APInt &Hi = Clusters[i].High->getValue();
    const APInt &Lo = Clusters[i].Low->getValue();
    TotalCases[i] = (Hi - Lo).getLimitedValue() + 1;
    if (i != 0)
      TotalCases[i] += TotalCases[i - 1];
  }

  // Cheap case: the whole range may be suitable for jump table.
  uint64_t Range = getJumpTableRange(Clusters,0, N - 1);
  uint64_t NumCases = getJumpTableNumCases(TotalCases, 0, N - 1);
  assert(NumCases < UINT64_MAX / 100);
  assert(Range >= NumCases);
  if (TLI.isSuitableForJumpTable(SI, NumCases, Range)) {
    CaseCluster JTCluster;
    if (buildJumpTable(Clusters, 0, N - 1, SI, DefaultMBB, JTCluster)) {
      Clusters[0] = JTCluster;
      Clusters.resize(1);
      return;
    }
  }

  // The algorithm below is not suitable for -O0.
  if (TM.getOptLevel() == CodeGenOpt::None)
    return;

  // Split Clusters into minimum number of dense partitions. The algorithm uses
  // the same idea as Kannan & Proebsting "Correction to 'Producing Good Code
  // for the Case Statement'" (1994), but builds the MinPartitions array in
  // reverse order to make it easier to reconstruct the partitions in ascending
  // order. In the choice between two optimal partitionings, it picks the one
  // which yields more jump tables.

  // MinPartitions[i] is the minimum nbr of partitions of Clusters[i..N-1].
  SmallVector<unsigned, 8> MinPartitions(N);
  // LastElement[i] is the last element of the partition starting at i.
  SmallVector<unsigned, 8> LastElement(N);
  // PartitionsScore[i] is used to break ties when choosing between two
  // partitionings resulting in the same number of partitions.
  SmallVector<unsigned, 8> PartitionsScore(N);
  // For PartitionsScore, a small number of comparisons is considered as good as
  // a jump table and a single comparison is considered better than a jump
  // table.
  enum PartitionScores : unsigned {
    NoTable = 0,
    Table = 1,
    FewCases = 1,
    SingleCase = 2
  };

  // Base case: There is only one way to partition Clusters[N-1].
  MinPartitions[N - 1] = 1;
  LastElement[N - 1] = N - 1;
  PartitionsScore[N - 1] = PartitionScores::SingleCase;

  // Note: loop indexes are signed to avoid underflow.
  for (int64_t i = N - 2; i >= 0; i--) {
    // Find optimal partitioning of Clusters[i..N-1].
    // Baseline: Put Clusters[i] into a partition on its own.
    MinPartitions[i] = MinPartitions[i + 1] + 1;
    LastElement[i] = i;
    PartitionsScore[i] = PartitionsScore[i + 1] + PartitionScores::SingleCase;

    // Search for a solution that results in fewer partitions.
    for (int64_t j = N - 1; j > i; j--) {
      // Try building a partition from Clusters[i..j].
      uint64_t Range = getJumpTableRange(Clusters, i, j);
      uint64_t NumCases = getJumpTableNumCases(TotalCases, i, j);
      assert(NumCases < UINT64_MAX / 100);
      assert(Range >= NumCases);
      if (TLI.isSuitableForJumpTable(SI, NumCases, Range)) {
        unsigned NumPartitions = 1 + (j == N - 1 ? 0 : MinPartitions[j + 1]);
        unsigned Score = j == N - 1 ? 0 : PartitionsScore[j + 1];
        int64_t NumEntries = j - i + 1;

        if (NumEntries == 1)
          Score += PartitionScores::SingleCase;
        else if (NumEntries <= SmallNumberOfEntries)
          Score += PartitionScores::FewCases;
        else if (NumEntries >= MinJumpTableEntries)
          Score += PartitionScores::Table;

        // If this leads to fewer partitions, or to the same number of
        // partitions with better score, it is a better partitioning.
        if (NumPartitions < MinPartitions[i] ||
            (NumPartitions == MinPartitions[i] && Score > PartitionsScore[i])) {
          MinPartitions[i] = NumPartitions;
          LastElement[i] = j;
          PartitionsScore[i] = Score;
        }
      }
    }
  }

  // Iterate over the partitions, replacing some with jump tables in-place.
  unsigned DstIndex = 0;
  for (unsigned First = 0, Last; First < N; First = Last + 1) {
    Last = LastElement[First];
    assert(Last >= First);
    assert(DstIndex <= First);
    unsigned NumClusters = Last - First + 1;

    CaseCluster JTCluster;
    if (NumClusters >= MinJumpTableEntries &&
        buildJumpTable(Clusters, First, Last, SI, DefaultMBB, JTCluster)) {
      Clusters[DstIndex++] = JTCluster;
    } else {
      for (unsigned I = First; I <= Last; ++I)
        std::memmove(&Clusters[DstIndex++], &Clusters[I], sizeof(Clusters[I]));
    }
  }
  Clusters.resize(DstIndex);
}

bool SelectionDAGBuilder::buildBitTests(CaseClusterVector &Clusters,
                                        unsigned First, unsigned Last,
                                        const SwitchInst *SI,
                                        CaseCluster &BTCluster) {
  assert(First <= Last);
  if (First == Last)
    return false;

  BitVector Dests(FuncInfo.MF->getNumBlockIDs());
  unsigned NumCmps = 0;
  for (int64_t I = First; I <= Last; ++I) {
    assert(Clusters[I].Kind == CC_Range);
    Dests.set(Clusters[I].MBB->getNumber());
    NumCmps += (Clusters[I].Low == Clusters[I].High) ? 1 : 2;
  }
  unsigned NumDests = Dests.count();

  APInt Low = Clusters[First].Low->getValue();
  APInt High = Clusters[Last].High->getValue();
  assert(Low.slt(High));

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  const DataLayout &DL = DAG.getDataLayout();
  if (!TLI.isSuitableForBitTests(NumDests, NumCmps, Low, High, DL))
    return false;

  APInt LowBound;
  APInt CmpRange;

  const int BitWidth = TLI.getPointerTy(DL).getSizeInBits();
  assert(TLI.rangeFitsInWord(Low, High, DL) &&
         "Case range must fit in bit mask!");

  // Check if the clusters cover a contiguous range such that no value in the
  // range will jump to the default statement.
  bool ContiguousRange = true;
  for (int64_t I = First + 1; I <= Last; ++I) {
    if (Clusters[I].Low->getValue() != Clusters[I - 1].High->getValue() + 1) {
      ContiguousRange = false;
      break;
    }
  }

  if (Low.isStrictlyPositive() && High.slt(BitWidth)) {
    // Optimize the case where all the case values fit in a word without having
    // to subtract minValue. In this case, we can optimize away the subtraction.
    LowBound = APInt::getNullValue(Low.getBitWidth());
    CmpRange = High;
    ContiguousRange = false;
  } else {
    LowBound = Low;
    CmpRange = High - Low;
  }

  CaseBitsVector CBV;
  auto TotalProb = BranchProbability::getZero();
  for (unsigned i = First; i <= Last; ++i) {
    // Find the CaseBits for this destination.
    unsigned j;
    for (j = 0; j < CBV.size(); ++j)
      if (CBV[j].BB == Clusters[i].MBB)
        break;
    if (j == CBV.size())
      CBV.push_back(
          CaseBits(0, Clusters[i].MBB, 0, BranchProbability::getZero()));
    CaseBits *CB = &CBV[j];

    // Update Mask, Bits and ExtraProb.
    uint64_t Lo = (Clusters[i].Low->getValue() - LowBound).getZExtValue();
    uint64_t Hi = (Clusters[i].High->getValue() - LowBound).getZExtValue();
    assert(Hi >= Lo && Hi < 64 && "Invalid bit case!");
    CB->Mask |= (-1ULL >> (63 - (Hi - Lo))) << Lo;
    CB->Bits += Hi - Lo + 1;
    CB->ExtraProb += Clusters[i].Prob;
    TotalProb += Clusters[i].Prob;
  }

  BitTestInfo BTI;
  llvm::sort(CBV, [](const CaseBits &a, const CaseBits &b) {
    // Sort by probability first, number of bits second, bit mask third.
    if (a.ExtraProb != b.ExtraProb)
      return a.ExtraProb > b.ExtraProb;
    if (a.Bits != b.Bits)
      return a.Bits > b.Bits;
    return a.Mask < b.Mask;
  });

  for (auto &CB : CBV) {
    MachineBasicBlock *BitTestBB =
        FuncInfo.MF->CreateMachineBasicBlock(SI->getParent());
    BTI.push_back(BitTestCase(CB.Mask, BitTestBB, CB.BB, CB.ExtraProb));
  }
  BitTestCases.emplace_back(std::move(LowBound), std::move(CmpRange),
                            SI->getCondition(), -1U, MVT::Other, false,
                            ContiguousRange, nullptr, nullptr, std::move(BTI),
                            TotalProb);

  BTCluster = CaseCluster::bitTests(Clusters[First].Low, Clusters[Last].High,
                                    BitTestCases.size() - 1, TotalProb);
  return true;
}

void SelectionDAGBuilder::findBitTestClusters(CaseClusterVector &Clusters,
                                              const SwitchInst *SI) {
// Partition Clusters into as few subsets as possible, where each subset has a
// range that fits in a machine word and has <= 3 unique destinations.

#ifndef NDEBUG
  // Clusters must be sorted and contain Range or JumpTable clusters.
  assert(!Clusters.empty());
  assert(Clusters[0].Kind == CC_Range || Clusters[0].Kind == CC_JumpTable);
  for (const CaseCluster &C : Clusters)
    assert(C.Kind == CC_Range || C.Kind == CC_JumpTable);
  for (unsigned i = 1; i < Clusters.size(); ++i)
    assert(Clusters[i-1].High->getValue().slt(Clusters[i].Low->getValue()));
#endif

  // The algorithm below is not suitable for -O0.
  if (TM.getOptLevel() == CodeGenOpt::None)
    return;

  // If target does not have legal shift left, do not emit bit tests at all.
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  const DataLayout &DL = DAG.getDataLayout();

  EVT PTy = TLI.getPointerTy(DL);
  if (!TLI.isOperationLegal(ISD::SHL, PTy))
    return;

  int BitWidth = PTy.getSizeInBits();
  const int64_t N = Clusters.size();

  // MinPartitions[i] is the minimum nbr of partitions of Clusters[i..N-1].
  SmallVector<unsigned, 8> MinPartitions(N);
  // LastElement[i] is the last element of the partition starting at i.
  SmallVector<unsigned, 8> LastElement(N);

  // FIXME: This might not be the best algorithm for finding bit test clusters.

  // Base case: There is only one way to partition Clusters[N-1].
  MinPartitions[N - 1] = 1;
  LastElement[N - 1] = N - 1;

  // Note: loop indexes are signed to avoid underflow.
  for (int64_t i = N - 2; i >= 0; --i) {
    // Find optimal partitioning of Clusters[i..N-1].
    // Baseline: Put Clusters[i] into a partition on its own.
    MinPartitions[i] = MinPartitions[i + 1] + 1;
    LastElement[i] = i;

    // Search for a solution that results in fewer partitions.
    // Note: the search is limited by BitWidth, reducing time complexity.
    for (int64_t j = std::min(N - 1, i + BitWidth - 1); j > i; --j) {
      // Try building a partition from Clusters[i..j].

      // Check the range.
      if (!TLI.rangeFitsInWord(Clusters[i].Low->getValue(),
                               Clusters[j].High->getValue(), DL))
        continue;

      // Check nbr of destinations and cluster types.
      // FIXME: This works, but doesn't seem very efficient.
      bool RangesOnly = true;
      BitVector Dests(FuncInfo.MF->getNumBlockIDs());
      for (int64_t k = i; k <= j; k++) {
        if (Clusters[k].Kind != CC_Range) {
          RangesOnly = false;
          break;
        }
        Dests.set(Clusters[k].MBB->getNumber());
      }
      if (!RangesOnly || Dests.count() > 3)
        break;

      // Check if it's a better partition.
      unsigned NumPartitions = 1 + (j == N - 1 ? 0 : MinPartitions[j + 1]);
      if (NumPartitions < MinPartitions[i]) {
        // Found a better partition.
        MinPartitions[i] = NumPartitions;
        LastElement[i] = j;
      }
    }
  }

  // Iterate over the partitions, replacing with bit-test clusters in-place.
  unsigned DstIndex = 0;
  for (unsigned First = 0, Last; First < N; First = Last + 1) {
    Last = LastElement[First];
    assert(First <= Last);
    assert(DstIndex <= First);

    CaseCluster BitTestCluster;
    if (buildBitTests(Clusters, First, Last, SI, BitTestCluster)) {
      Clusters[DstIndex++] = BitTestCluster;
    } else {
      size_t NumClusters = Last - First + 1;
      std::memmove(&Clusters[DstIndex], &Clusters[First],
                   sizeof(Clusters[0]) * NumClusters);
      DstIndex += NumClusters;
    }
  }
  Clusters.resize(DstIndex);
}

void SelectionDAGBuilder::lowerWorkItem(SwitchWorkListItem W, Value *Cond,
                                        MachineBasicBlock *SwitchMBB,
                                        MachineBasicBlock *DefaultMBB) {
  MachineFunction *CurMF = FuncInfo.MF;
  MachineBasicBlock *NextMBB = nullptr;
  MachineFunction::iterator BBI(W.MBB);
  if (++BBI != FuncInfo.MF->end())
    NextMBB = &*BBI;

  unsigned Size = W.LastCluster - W.FirstCluster + 1;

  BranchProbabilityInfo *BPI = FuncInfo.BPI;

  if (Size == 2 && W.MBB == SwitchMBB) {
    // If any two of the cases has the same destination, and if one value
    // is the same as the other, but has one bit unset that the other has set,
    // use bit manipulation to do two compares at once.  For example:
    // "if (X == 6 || X == 4)" -> "if ((X|2) == 6)"
    // TODO: This could be extended to merge any 2 cases in switches with 3
    // cases.
    // TODO: Handle cases where W.CaseBB != SwitchBB.
    CaseCluster &Small = *W.FirstCluster;
    CaseCluster &Big = *W.LastCluster;

    if (Small.Low == Small.High && Big.Low == Big.High &&
        Small.MBB == Big.MBB) {
      const APInt &SmallValue = Small.Low->getValue();
      const APInt &BigValue = Big.Low->getValue();

      // Check that there is only one bit different.
      APInt CommonBit = BigValue ^ SmallValue;
      if (CommonBit.isPowerOf2()) {
        SDValue CondLHS = getValue(Cond);
        EVT VT = CondLHS.getValueType();
        SDLoc DL = getCurSDLoc();

        SDValue Or = DAG.getNode(ISD::OR, DL, VT, CondLHS,
                                 DAG.getConstant(CommonBit, DL, VT));
        SDValue Cond = DAG.getSetCC(
            DL, MVT::i1, Or, DAG.getConstant(BigValue | SmallValue, DL, VT),
            ISD::SETEQ);

        // Update successor info.
        // Both Small and Big will jump to Small.BB, so we sum up the
        // probabilities.
        addSuccessorWithProb(SwitchMBB, Small.MBB, Small.Prob + Big.Prob);
        if (BPI)
          addSuccessorWithProb(
              SwitchMBB, DefaultMBB,
              // The default destination is the first successor in IR.
              BPI->getEdgeProbability(SwitchMBB->getBasicBlock(), (unsigned)0));
        else
          addSuccessorWithProb(SwitchMBB, DefaultMBB);

        // Insert the true branch.
        SDValue BrCond =
            DAG.getNode(ISD::BRCOND, DL, MVT::Other, getControlRoot(), Cond,
                        DAG.getBasicBlock(Small.MBB));
        // Insert the false branch.
        BrCond = DAG.getNode(ISD::BR, DL, MVT::Other, BrCond,
                             DAG.getBasicBlock(DefaultMBB));

        DAG.setRoot(BrCond);
        return;
      }
    }
  }

  if (TM.getOptLevel() != CodeGenOpt::None) {
    // Here, we order cases by probability so the most likely case will be
    // checked first. However, two clusters can have the same probability in
    // which case their relative ordering is non-deterministic. So we use Low
    // as a tie-breaker as clusters are guaranteed to never overlap.
    llvm::sort(W.FirstCluster, W.LastCluster + 1,
               [](const CaseCluster &a, const CaseCluster &b) {
      return a.Prob != b.Prob ?
             a.Prob > b.Prob :
             a.Low->getValue().slt(b.Low->getValue());
    });

    // Rearrange the case blocks so that the last one falls through if possible
    // without changing the order of probabilities.
    for (CaseClusterIt I = W.LastCluster; I > W.FirstCluster; ) {
      --I;
      if (I->Prob > W.LastCluster->Prob)
        break;
      if (I->Kind == CC_Range && I->MBB == NextMBB) {
        std::swap(*I, *W.LastCluster);
        break;
      }
    }
  }

  // Compute total probability.
  BranchProbability DefaultProb = W.DefaultProb;
  BranchProbability UnhandledProbs = DefaultProb;
  for (CaseClusterIt I = W.FirstCluster; I <= W.LastCluster; ++I)
    UnhandledProbs += I->Prob;

  MachineBasicBlock *CurMBB = W.MBB;
  for (CaseClusterIt I = W.FirstCluster, E = W.LastCluster; I <= E; ++I) {
    MachineBasicBlock *Fallthrough;
    if (I == W.LastCluster) {
      // For the last cluster, fall through to the default destination.
      Fallthrough = DefaultMBB;
    } else {
      Fallthrough = CurMF->CreateMachineBasicBlock(CurMBB->getBasicBlock());
      CurMF->insert(BBI, Fallthrough);
      // Put Cond in a virtual register to make it available from the new blocks.
      ExportFromCurrentBlock(Cond);
    }
    UnhandledProbs -= I->Prob;

    switch (I->Kind) {
      case CC_JumpTable: {
        // FIXME: Optimize away range check based on pivot comparisons.
        JumpTableHeader *JTH = &JTCases[I->JTCasesIndex].first;
        JumpTable *JT = &JTCases[I->JTCasesIndex].second;

        // The jump block hasn't been inserted yet; insert it here.
        MachineBasicBlock *JumpMBB = JT->MBB;
        CurMF->insert(BBI, JumpMBB);

        auto JumpProb = I->Prob;
        auto FallthroughProb = UnhandledProbs;

        // If the default statement is a target of the jump table, we evenly
        // distribute the default probability to successors of CurMBB. Also
        // update the probability on the edge from JumpMBB to Fallthrough.
        for (MachineBasicBlock::succ_iterator SI = JumpMBB->succ_begin(),
                                              SE = JumpMBB->succ_end();
             SI != SE; ++SI) {
          if (*SI == DefaultMBB) {
            JumpProb += DefaultProb / 2;
            FallthroughProb -= DefaultProb / 2;
            JumpMBB->setSuccProbability(SI, DefaultProb / 2);
            JumpMBB->normalizeSuccProbs();
            break;
          }
        }

        addSuccessorWithProb(CurMBB, Fallthrough, FallthroughProb);
        addSuccessorWithProb(CurMBB, JumpMBB, JumpProb);
        CurMBB->normalizeSuccProbs();

        // The jump table header will be inserted in our current block, do the
        // range check, and fall through to our fallthrough block.
        JTH->HeaderBB = CurMBB;
        JT->Default = Fallthrough; // FIXME: Move Default to JumpTableHeader.

        // If we're in the right place, emit the jump table header right now.
        if (CurMBB == SwitchMBB) {
          visitJumpTableHeader(*JT, *JTH, SwitchMBB);
          JTH->Emitted = true;
        }
        break;
      }
      case CC_BitTests: {
        // FIXME: Optimize away range check based on pivot comparisons.
        BitTestBlock *BTB = &BitTestCases[I->BTCasesIndex];

        // The bit test blocks haven't been inserted yet; insert them here.
        for (BitTestCase &BTC : BTB->Cases)
          CurMF->insert(BBI, BTC.ThisBB);

        // Fill in fields of the BitTestBlock.
        BTB->Parent = CurMBB;
        BTB->Default = Fallthrough;

        BTB->DefaultProb = UnhandledProbs;
        // If the cases in bit test don't form a contiguous range, we evenly
        // distribute the probability on the edge to Fallthrough to two
        // successors of CurMBB.
        if (!BTB->ContiguousRange) {
          BTB->Prob += DefaultProb / 2;
          BTB->DefaultProb -= DefaultProb / 2;
        }

        // If we're in the right place, emit the bit test header right now.
        if (CurMBB == SwitchMBB) {
          visitBitTestHeader(*BTB, SwitchMBB);
          BTB->Emitted = true;
        }
        break;
      }
      case CC_Range: {
        const Value *RHS, *LHS, *MHS;
        ISD::CondCode CC;
        if (I->Low == I->High) {
          // Check Cond == I->Low.
          CC = ISD::SETEQ;
          LHS = Cond;
          RHS=I->Low;
          MHS = nullptr;
        } else {
          // Check I->Low <= Cond <= I->High.
          CC = ISD::SETLE;
          LHS = I->Low;
          MHS = Cond;
          RHS = I->High;
        }

        // The false probability is the sum of all unhandled cases.
        CaseBlock CB(CC, LHS, RHS, MHS, I->MBB, Fallthrough, CurMBB,
                     getCurSDLoc(), I->Prob, UnhandledProbs);

        if (CurMBB == SwitchMBB)
          visitSwitchCase(CB, SwitchMBB);
        else
          SwitchCases.push_back(CB);

        break;
      }
    }
    CurMBB = Fallthrough;
  }
}

unsigned SelectionDAGBuilder::caseClusterRank(const CaseCluster &CC,
                                              CaseClusterIt First,
                                              CaseClusterIt Last) {
  return std::count_if(First, Last + 1, [&](const CaseCluster &X) {
    if (X.Prob != CC.Prob)
      return X.Prob > CC.Prob;

    // Ties are broken by comparing the case value.
    return X.Low->getValue().slt(CC.Low->getValue());
  });
}

void SelectionDAGBuilder::splitWorkItem(SwitchWorkList &WorkList,
                                        const SwitchWorkListItem &W,
                                        Value *Cond,
                                        MachineBasicBlock *SwitchMBB) {
  assert(W.FirstCluster->Low->getValue().slt(W.LastCluster->Low->getValue()) &&
         "Clusters not sorted?");

  assert(W.LastCluster - W.FirstCluster + 1 >= 2 && "Too small to split!");

  // Balance the tree based on branch probabilities to create a near-optimal (in
  // terms of search time given key frequency) binary search tree. See e.g. Kurt
  // Mehlhorn "Nearly Optimal Binary Search Trees" (1975).
  CaseClusterIt LastLeft = W.FirstCluster;
  CaseClusterIt FirstRight = W.LastCluster;
  auto LeftProb = LastLeft->Prob + W.DefaultProb / 2;
  auto RightProb = FirstRight->Prob + W.DefaultProb / 2;

  // Move LastLeft and FirstRight towards each other from opposite directions to
  // find a partitioning of the clusters which balances the probability on both
  // sides. If LeftProb and RightProb are equal, alternate which side is
  // taken to ensure 0-probability nodes are distributed evenly.
  unsigned I = 0;
  while (LastLeft + 1 < FirstRight) {
    if (LeftProb < RightProb || (LeftProb == RightProb && (I & 1)))
      LeftProb += (++LastLeft)->Prob;
    else
      RightProb += (--FirstRight)->Prob;
    I++;
  }

  while (true) {
    // Our binary search tree differs from a typical BST in that ours can have up
    // to three values in each leaf. The pivot selection above doesn't take that
    // into account, which means the tree might require more nodes and be less
    // efficient. We compensate for this here.

    unsigned NumLeft = LastLeft - W.FirstCluster + 1;
    unsigned NumRight = W.LastCluster - FirstRight + 1;

    if (std::min(NumLeft, NumRight) < 3 && std::max(NumLeft, NumRight) > 3) {
      // If one side has less than 3 clusters, and the other has more than 3,
      // consider taking a cluster from the other side.

      if (NumLeft < NumRight) {
        // Consider moving the first cluster on the right to the left side.
        CaseCluster &CC = *FirstRight;
        unsigned RightSideRank = caseClusterRank(CC, FirstRight, W.LastCluster);
        unsigned LeftSideRank = caseClusterRank(CC, W.FirstCluster, LastLeft);
        if (LeftSideRank <= RightSideRank) {
          // Moving the cluster to the left does not demote it.
          ++LastLeft;
          ++FirstRight;
          continue;
        }
      } else {
        assert(NumRight < NumLeft);
        // Consider moving the last element on the left to the right side.
        CaseCluster &CC = *LastLeft;
        unsigned LeftSideRank = caseClusterRank(CC, W.FirstCluster, LastLeft);
        unsigned RightSideRank = caseClusterRank(CC, FirstRight, W.LastCluster);
        if (RightSideRank <= LeftSideRank) {
          // Moving the cluster to the right does not demot it.
          --LastLeft;
          --FirstRight;
          continue;
        }
      }
    }
    break;
  }

  assert(LastLeft + 1 == FirstRight);
  assert(LastLeft >= W.FirstCluster);
  assert(FirstRight <= W.LastCluster);

  // Use the first element on the right as pivot since we will make less-than
  // comparisons against it.
  CaseClusterIt PivotCluster = FirstRight;
  assert(PivotCluster > W.FirstCluster);
  assert(PivotCluster <= W.LastCluster);

  CaseClusterIt FirstLeft = W.FirstCluster;
  CaseClusterIt LastRight = W.LastCluster;

  const ConstantInt *Pivot = PivotCluster->Low;

  // New blocks will be inserted immediately after the current one.
  MachineFunction::iterator BBI(W.MBB);
  ++BBI;

  // We will branch to the LHS if Value < Pivot. If LHS is a single cluster,
  // we can branch to its destination directly if it's squeezed exactly in
  // between the known lower bound and Pivot - 1.
  MachineBasicBlock *LeftMBB;
  if (FirstLeft == LastLeft && FirstLeft->Kind == CC_Range &&
      FirstLeft->Low == W.GE &&
      (FirstLeft->High->getValue() + 1LL) == Pivot->getValue()) {
    LeftMBB = FirstLeft->MBB;
  } else {
    LeftMBB = FuncInfo.MF->CreateMachineBasicBlock(W.MBB->getBasicBlock());
    FuncInfo.MF->insert(BBI, LeftMBB);
    WorkList.push_back(
        {LeftMBB, FirstLeft, LastLeft, W.GE, Pivot, W.DefaultProb / 2});
    // Put Cond in a virtual register to make it available from the new blocks.
    ExportFromCurrentBlock(Cond);
  }

  // Similarly, we will branch to the RHS if Value >= Pivot. If RHS is a
  // single cluster, RHS.Low == Pivot, and we can branch to its destination
  // directly if RHS.High equals the current upper bound.
  MachineBasicBlock *RightMBB;
  if (FirstRight == LastRight && FirstRight->Kind == CC_Range &&
      W.LT && (FirstRight->High->getValue() + 1ULL) == W.LT->getValue()) {
    RightMBB = FirstRight->MBB;
  } else {
    RightMBB = FuncInfo.MF->CreateMachineBasicBlock(W.MBB->getBasicBlock());
    FuncInfo.MF->insert(BBI, RightMBB);
    WorkList.push_back(
        {RightMBB, FirstRight, LastRight, Pivot, W.LT, W.DefaultProb / 2});
    // Put Cond in a virtual register to make it available from the new blocks.
    ExportFromCurrentBlock(Cond);
  }

  // Create the CaseBlock record that will be used to lower the branch.
  CaseBlock CB(ISD::SETLT, Cond, Pivot, nullptr, LeftMBB, RightMBB, W.MBB,
               getCurSDLoc(), LeftProb, RightProb);

  if (W.MBB == SwitchMBB)
    visitSwitchCase(CB, SwitchMBB);
  else
    SwitchCases.push_back(CB);
}

// Scale CaseProb after peeling a case with the probablity of PeeledCaseProb
// from the swith statement.
static BranchProbability scaleCaseProbality(BranchProbability CaseProb,
                                            BranchProbability PeeledCaseProb) {
  if (PeeledCaseProb == BranchProbability::getOne())
    return BranchProbability::getZero();
  BranchProbability SwitchProb = PeeledCaseProb.getCompl();

  uint32_t Numerator = CaseProb.getNumerator();
  uint32_t Denominator = SwitchProb.scale(CaseProb.getDenominator());
  return BranchProbability(Numerator, std::max(Numerator, Denominator));
}

// Try to peel the top probability case if it exceeds the threshold.
// Return current MachineBasicBlock for the switch statement if the peeling
// does not occur.
// If the peeling is performed, return the newly created MachineBasicBlock
// for the peeled switch statement. Also update Clusters to remove the peeled
// case. PeeledCaseProb is the BranchProbability for the peeled case.
MachineBasicBlock *SelectionDAGBuilder::peelDominantCaseCluster(
    const SwitchInst &SI, CaseClusterVector &Clusters,
    BranchProbability &PeeledCaseProb) {
  MachineBasicBlock *SwitchMBB = FuncInfo.MBB;
  // Don't perform if there is only one cluster or optimizing for size.
  if (SwitchPeelThreshold > 100 || !FuncInfo.BPI || Clusters.size() < 2 ||
      TM.getOptLevel() == CodeGenOpt::None ||
      SwitchMBB->getParent()->getFunction().optForMinSize())
    return SwitchMBB;

  BranchProbability TopCaseProb = BranchProbability(SwitchPeelThreshold, 100);
  unsigned PeeledCaseIndex = 0;
  bool SwitchPeeled = false;
  for (unsigned Index = 0; Index < Clusters.size(); ++Index) {
    CaseCluster &CC = Clusters[Index];
    if (CC.Prob < TopCaseProb)
      continue;
    TopCaseProb = CC.Prob;
    PeeledCaseIndex = Index;
    SwitchPeeled = true;
  }
  if (!SwitchPeeled)
    return SwitchMBB;

  LLVM_DEBUG(dbgs() << "Peeled one top case in switch stmt, prob: "
                    << TopCaseProb << "\n");

  // Record the MBB for the peeled switch statement.
  MachineFunction::iterator BBI(SwitchMBB);
  ++BBI;
  MachineBasicBlock *PeeledSwitchMBB =
      FuncInfo.MF->CreateMachineBasicBlock(SwitchMBB->getBasicBlock());
  FuncInfo.MF->insert(BBI, PeeledSwitchMBB);

  ExportFromCurrentBlock(SI.getCondition());
  auto PeeledCaseIt = Clusters.begin() + PeeledCaseIndex;
  SwitchWorkListItem W = {SwitchMBB, PeeledCaseIt, PeeledCaseIt,
                          nullptr,   nullptr,      TopCaseProb.getCompl()};
  lowerWorkItem(W, SI.getCondition(), SwitchMBB, PeeledSwitchMBB);

  Clusters.erase(PeeledCaseIt);
  for (CaseCluster &CC : Clusters) {
    LLVM_DEBUG(
        dbgs() << "Scale the probablity for one cluster, before scaling: "
               << CC.Prob << "\n");
    CC.Prob = scaleCaseProbality(CC.Prob, TopCaseProb);
    LLVM_DEBUG(dbgs() << "After scaling: " << CC.Prob << "\n");
  }
  PeeledCaseProb = TopCaseProb;
  return PeeledSwitchMBB;
}

void SelectionDAGBuilder::visitSwitch(const SwitchInst &SI) {
  // Extract cases from the switch.
  BranchProbabilityInfo *BPI = FuncInfo.BPI;
  CaseClusterVector Clusters;
  Clusters.reserve(SI.getNumCases());
  for (auto I : SI.cases()) {
    MachineBasicBlock *Succ = FuncInfo.MBBMap[I.getCaseSuccessor()];
    const ConstantInt *CaseVal = I.getCaseValue();
    BranchProbability Prob =
        BPI ? BPI->getEdgeProbability(SI.getParent(), I.getSuccessorIndex())
            : BranchProbability(1, SI.getNumCases() + 1);
    Clusters.push_back(CaseCluster::range(CaseVal, CaseVal, Succ, Prob));
  }

  MachineBasicBlock *DefaultMBB = FuncInfo.MBBMap[SI.getDefaultDest()];

  // Cluster adjacent cases with the same destination. We do this at all
  // optimization levels because it's cheap to do and will make codegen faster
  // if there are many clusters.
  sortAndRangeify(Clusters);

  if (TM.getOptLevel() != CodeGenOpt::None) {
    // Replace an unreachable default with the most popular destination.
    // FIXME: Exploit unreachable default more aggressively.
    bool UnreachableDefault =
        isa<UnreachableInst>(SI.getDefaultDest()->getFirstNonPHIOrDbg());
    if (UnreachableDefault && !Clusters.empty()) {
      DenseMap<const BasicBlock *, unsigned> Popularity;
      unsigned MaxPop = 0;
      const BasicBlock *MaxBB = nullptr;
      for (auto I : SI.cases()) {
        const BasicBlock *BB = I.getCaseSuccessor();
        if (++Popularity[BB] > MaxPop) {
          MaxPop = Popularity[BB];
          MaxBB = BB;
        }
      }
      // Set new default.
      assert(MaxPop > 0 && MaxBB);
      DefaultMBB = FuncInfo.MBBMap[MaxBB];

      // Remove cases that were pointing to the destination that is now the
      // default.
      CaseClusterVector New;
      New.reserve(Clusters.size());
      for (CaseCluster &CC : Clusters) {
        if (CC.MBB != DefaultMBB)
          New.push_back(CC);
      }
      Clusters = std::move(New);
    }
  }

  // The branch probablity of the peeled case.
  BranchProbability PeeledCaseProb = BranchProbability::getZero();
  MachineBasicBlock *PeeledSwitchMBB =
      peelDominantCaseCluster(SI, Clusters, PeeledCaseProb);

  // If there is only the default destination, jump there directly.
  MachineBasicBlock *SwitchMBB = FuncInfo.MBB;
  if (Clusters.empty()) {
    assert(PeeledSwitchMBB == SwitchMBB);
    SwitchMBB->addSuccessor(DefaultMBB);
    if (DefaultMBB != NextBlock(SwitchMBB)) {
      DAG.setRoot(DAG.getNode(ISD::BR, getCurSDLoc(), MVT::Other,
                              getControlRoot(), DAG.getBasicBlock(DefaultMBB)));
    }
    return;
  }

  findJumpTables(Clusters, &SI, DefaultMBB);
  findBitTestClusters(Clusters, &SI);

  LLVM_DEBUG({
    dbgs() << "Case clusters: ";
    for (const CaseCluster &C : Clusters) {
      if (C.Kind == CC_JumpTable)
        dbgs() << "JT:";
      if (C.Kind == CC_BitTests)
        dbgs() << "BT:";

      C.Low->getValue().print(dbgs(), true);
      if (C.Low != C.High) {
        dbgs() << '-';
        C.High->getValue().print(dbgs(), true);
      }
      dbgs() << ' ';
    }
    dbgs() << '\n';
  });

  assert(!Clusters.empty());
  SwitchWorkList WorkList;
  CaseClusterIt First = Clusters.begin();
  CaseClusterIt Last = Clusters.end() - 1;
  auto DefaultProb = getEdgeProbability(PeeledSwitchMBB, DefaultMBB);
  // Scale the branchprobability for DefaultMBB if the peel occurs and
  // DefaultMBB is not replaced.
  if (PeeledCaseProb != BranchProbability::getZero() &&
      DefaultMBB == FuncInfo.MBBMap[SI.getDefaultDest()])
    DefaultProb = scaleCaseProbality(DefaultProb, PeeledCaseProb);
  WorkList.push_back(
      {PeeledSwitchMBB, First, Last, nullptr, nullptr, DefaultProb});

  while (!WorkList.empty()) {
    SwitchWorkListItem W = WorkList.back();
    WorkList.pop_back();
    unsigned NumClusters = W.LastCluster - W.FirstCluster + 1;

    if (NumClusters > 3 && TM.getOptLevel() != CodeGenOpt::None &&
        !DefaultMBB->getParent()->getFunction().optForMinSize()) {
      // For optimized builds, lower large range as a balanced binary tree.
      splitWorkItem(WorkList, W, SI.getCondition(), SwitchMBB);
      continue;
    }

    lowerWorkItem(W, SI.getCondition(), SwitchMBB, DefaultMBB);
  }
}
