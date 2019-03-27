//===- LegalizeDAG.cpp - Implement SelectionDAG::Legalize -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the SelectionDAG::Legalize method.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/RuntimeLibcalls.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MachineValueType.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <tuple>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "legalizedag"

namespace {

/// Keeps track of state when getting the sign of a floating-point value as an
/// integer.
struct FloatSignAsInt {
  EVT FloatVT;
  SDValue Chain;
  SDValue FloatPtr;
  SDValue IntPtr;
  MachinePointerInfo IntPointerInfo;
  MachinePointerInfo FloatPointerInfo;
  SDValue IntValue;
  APInt SignMask;
  uint8_t SignBit;
};

//===----------------------------------------------------------------------===//
/// This takes an arbitrary SelectionDAG as input and
/// hacks on it until the target machine can handle it.  This involves
/// eliminating value sizes the machine cannot handle (promoting small sizes to
/// large sizes or splitting up large values into small values) as well as
/// eliminating operations the machine cannot handle.
///
/// This code also does a small amount of optimization and recognition of idioms
/// as part of its processing.  For example, if a target does not support a
/// 'setcc' instruction efficiently, but does support 'brcc' instruction, this
/// will attempt merge setcc and brc instructions into brcc's.
class SelectionDAGLegalize {
  const TargetMachine &TM;
  const TargetLowering &TLI;
  SelectionDAG &DAG;

  /// The set of nodes which have already been legalized. We hold a
  /// reference to it in order to update as necessary on node deletion.
  SmallPtrSetImpl<SDNode *> &LegalizedNodes;

  /// A set of all the nodes updated during legalization.
  SmallSetVector<SDNode *, 16> *UpdatedNodes;

  EVT getSetCCResultType(EVT VT) const {
    return TLI.getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);
  }

  // Libcall insertion helpers.

public:
  SelectionDAGLegalize(SelectionDAG &DAG,
                       SmallPtrSetImpl<SDNode *> &LegalizedNodes,
                       SmallSetVector<SDNode *, 16> *UpdatedNodes = nullptr)
      : TM(DAG.getTarget()), TLI(DAG.getTargetLoweringInfo()), DAG(DAG),
        LegalizedNodes(LegalizedNodes), UpdatedNodes(UpdatedNodes) {}

  /// Legalizes the given operation.
  void LegalizeOp(SDNode *Node);

private:
  SDValue OptimizeFloatStore(StoreSDNode *ST);

  void LegalizeLoadOps(SDNode *Node);
  void LegalizeStoreOps(SDNode *Node);

  /// Some targets cannot handle a variable
  /// insertion index for the INSERT_VECTOR_ELT instruction.  In this case, it
  /// is necessary to spill the vector being inserted into to memory, perform
  /// the insert there, and then read the result back.
  SDValue PerformInsertVectorEltInMemory(SDValue Vec, SDValue Val, SDValue Idx,
                                         const SDLoc &dl);
  SDValue ExpandINSERT_VECTOR_ELT(SDValue Vec, SDValue Val, SDValue Idx,
                                  const SDLoc &dl);

  /// Return a vector shuffle operation which
  /// performs the same shuffe in terms of order or result bytes, but on a type
  /// whose vector element type is narrower than the original shuffle type.
  /// e.g. <v4i32> <0, 1, 0, 1> -> v8i16 <0, 1, 2, 3, 0, 1, 2, 3>
  SDValue ShuffleWithNarrowerEltType(EVT NVT, EVT VT, const SDLoc &dl,
                                     SDValue N1, SDValue N2,
                                     ArrayRef<int> Mask) const;

  bool LegalizeSetCCCondCode(EVT VT, SDValue &LHS, SDValue &RHS, SDValue &CC,
                             bool &NeedInvert, const SDLoc &dl);

  SDValue ExpandLibCall(RTLIB::Libcall LC, SDNode *Node, bool isSigned);
  SDValue ExpandLibCall(RTLIB::Libcall LC, EVT RetVT, const SDValue *Ops,
                        unsigned NumOps, bool isSigned, const SDLoc &dl);

  std::pair<SDValue, SDValue> ExpandChainLibCall(RTLIB::Libcall LC,
                                                 SDNode *Node, bool isSigned);
  SDValue ExpandFPLibCall(SDNode *Node, RTLIB::Libcall Call_F32,
                          RTLIB::Libcall Call_F64, RTLIB::Libcall Call_F80,
                          RTLIB::Libcall Call_F128,
                          RTLIB::Libcall Call_PPCF128);
  SDValue ExpandIntLibCall(SDNode *Node, bool isSigned,
                           RTLIB::Libcall Call_I8,
                           RTLIB::Libcall Call_I16,
                           RTLIB::Libcall Call_I32,
                           RTLIB::Libcall Call_I64,
                           RTLIB::Libcall Call_I128);
  void ExpandDivRemLibCall(SDNode *Node, SmallVectorImpl<SDValue> &Results);
  void ExpandSinCosLibCall(SDNode *Node, SmallVectorImpl<SDValue> &Results);

  SDValue EmitStackConvert(SDValue SrcOp, EVT SlotVT, EVT DestVT,
                           const SDLoc &dl);
  SDValue ExpandBUILD_VECTOR(SDNode *Node);
  SDValue ExpandSCALAR_TO_VECTOR(SDNode *Node);
  void ExpandDYNAMIC_STACKALLOC(SDNode *Node,
                                SmallVectorImpl<SDValue> &Results);
  void getSignAsIntValue(FloatSignAsInt &State, const SDLoc &DL,
                         SDValue Value) const;
  SDValue modifySignAsInt(const FloatSignAsInt &State, const SDLoc &DL,
                          SDValue NewIntValue) const;
  SDValue ExpandFCOPYSIGN(SDNode *Node) const;
  SDValue ExpandFABS(SDNode *Node) const;
  SDValue ExpandLegalINT_TO_FP(bool isSigned, SDValue Op0, EVT DestVT,
                               const SDLoc &dl);
  SDValue PromoteLegalINT_TO_FP(SDValue LegalOp, EVT DestVT, bool isSigned,
                                const SDLoc &dl);
  SDValue PromoteLegalFP_TO_INT(SDValue LegalOp, EVT DestVT, bool isSigned,
                                const SDLoc &dl);

  SDValue ExpandBITREVERSE(SDValue Op, const SDLoc &dl);
  SDValue ExpandBSWAP(SDValue Op, const SDLoc &dl);

  SDValue ExpandExtractFromVectorThroughStack(SDValue Op);
  SDValue ExpandInsertToVectorThroughStack(SDValue Op);
  SDValue ExpandVectorBuildThroughStack(SDNode* Node);

  SDValue ExpandConstantFP(ConstantFPSDNode *CFP, bool UseCP);
  SDValue ExpandConstant(ConstantSDNode *CP);

  // if ExpandNode returns false, LegalizeOp falls back to ConvertNodeToLibcall
  bool ExpandNode(SDNode *Node);
  void ConvertNodeToLibcall(SDNode *Node);
  void PromoteNode(SDNode *Node);

public:
  // Node replacement helpers

  void ReplacedNode(SDNode *N) {
    LegalizedNodes.erase(N);
    if (UpdatedNodes)
      UpdatedNodes->insert(N);
  }

  void ReplaceNode(SDNode *Old, SDNode *New) {
    LLVM_DEBUG(dbgs() << " ... replacing: "; Old->dump(&DAG);
               dbgs() << "     with:      "; New->dump(&DAG));

    assert(Old->getNumValues() == New->getNumValues() &&
           "Replacing one node with another that produces a different number "
           "of values!");
    DAG.ReplaceAllUsesWith(Old, New);
    if (UpdatedNodes)
      UpdatedNodes->insert(New);
    ReplacedNode(Old);
  }

  void ReplaceNode(SDValue Old, SDValue New) {
    LLVM_DEBUG(dbgs() << " ... replacing: "; Old->dump(&DAG);
               dbgs() << "     with:      "; New->dump(&DAG));

    DAG.ReplaceAllUsesWith(Old, New);
    if (UpdatedNodes)
      UpdatedNodes->insert(New.getNode());
    ReplacedNode(Old.getNode());
  }

  void ReplaceNode(SDNode *Old, const SDValue *New) {
    LLVM_DEBUG(dbgs() << " ... replacing: "; Old->dump(&DAG));

    DAG.ReplaceAllUsesWith(Old, New);
    for (unsigned i = 0, e = Old->getNumValues(); i != e; ++i) {
      LLVM_DEBUG(dbgs() << (i == 0 ? "     with:      " : "      and:      ");
                 New[i]->dump(&DAG));
      if (UpdatedNodes)
        UpdatedNodes->insert(New[i].getNode());
    }
    ReplacedNode(Old);
  }
};

} // end anonymous namespace

/// Return a vector shuffle operation which
/// performs the same shuffle in terms of order or result bytes, but on a type
/// whose vector element type is narrower than the original shuffle type.
/// e.g. <v4i32> <0, 1, 0, 1> -> v8i16 <0, 1, 2, 3, 0, 1, 2, 3>
SDValue SelectionDAGLegalize::ShuffleWithNarrowerEltType(
    EVT NVT, EVT VT, const SDLoc &dl, SDValue N1, SDValue N2,
    ArrayRef<int> Mask) const {
  unsigned NumMaskElts = VT.getVectorNumElements();
  unsigned NumDestElts = NVT.getVectorNumElements();
  unsigned NumEltsGrowth = NumDestElts / NumMaskElts;

  assert(NumEltsGrowth && "Cannot promote to vector type with fewer elts!");

  if (NumEltsGrowth == 1)
    return DAG.getVectorShuffle(NVT, dl, N1, N2, Mask);

  SmallVector<int, 8> NewMask;
  for (unsigned i = 0; i != NumMaskElts; ++i) {
    int Idx = Mask[i];
    for (unsigned j = 0; j != NumEltsGrowth; ++j) {
      if (Idx < 0)
        NewMask.push_back(-1);
      else
        NewMask.push_back(Idx * NumEltsGrowth + j);
    }
  }
  assert(NewMask.size() == NumDestElts && "Non-integer NumEltsGrowth?");
  assert(TLI.isShuffleMaskLegal(NewMask, NVT) && "Shuffle not legal?");
  return DAG.getVectorShuffle(NVT, dl, N1, N2, NewMask);
}

/// Expands the ConstantFP node to an integer constant or
/// a load from the constant pool.
SDValue
SelectionDAGLegalize::ExpandConstantFP(ConstantFPSDNode *CFP, bool UseCP) {
  bool Extend = false;
  SDLoc dl(CFP);

  // If a FP immediate is precise when represented as a float and if the
  // target can do an extending load from float to double, we put it into
  // the constant pool as a float, even if it's is statically typed as a
  // double.  This shrinks FP constants and canonicalizes them for targets where
  // an FP extending load is the same cost as a normal load (such as on the x87
  // fp stack or PPC FP unit).
  EVT VT = CFP->getValueType(0);
  ConstantFP *LLVMC = const_cast<ConstantFP*>(CFP->getConstantFPValue());
  if (!UseCP) {
    assert((VT == MVT::f64 || VT == MVT::f32) && "Invalid type expansion");
    return DAG.getConstant(LLVMC->getValueAPF().bitcastToAPInt(), dl,
                           (VT == MVT::f64) ? MVT::i64 : MVT::i32);
  }

  APFloat APF = CFP->getValueAPF();
  EVT OrigVT = VT;
  EVT SVT = VT;

  // We don't want to shrink SNaNs. Converting the SNaN back to its real type
  // can cause it to be changed into a QNaN on some platforms (e.g. on SystemZ).
  if (!APF.isSignaling()) {
    while (SVT != MVT::f32 && SVT != MVT::f16) {
      SVT = (MVT::SimpleValueType)(SVT.getSimpleVT().SimpleTy - 1);
      if (ConstantFPSDNode::isValueValidForType(SVT, APF) &&
          // Only do this if the target has a native EXTLOAD instruction from
          // smaller type.
          TLI.isLoadExtLegal(ISD::EXTLOAD, OrigVT, SVT) &&
          TLI.ShouldShrinkFPConstant(OrigVT)) {
        Type *SType = SVT.getTypeForEVT(*DAG.getContext());
        LLVMC = cast<ConstantFP>(ConstantExpr::getFPTrunc(LLVMC, SType));
        VT = SVT;
        Extend = true;
      }
    }
  }

  SDValue CPIdx =
      DAG.getConstantPool(LLVMC, TLI.getPointerTy(DAG.getDataLayout()));
  unsigned Alignment = cast<ConstantPoolSDNode>(CPIdx)->getAlignment();
  if (Extend) {
    SDValue Result = DAG.getExtLoad(
        ISD::EXTLOAD, dl, OrigVT, DAG.getEntryNode(), CPIdx,
        MachinePointerInfo::getConstantPool(DAG.getMachineFunction()), VT,
        Alignment);
    return Result;
  }
  SDValue Result = DAG.getLoad(
      OrigVT, dl, DAG.getEntryNode(), CPIdx,
      MachinePointerInfo::getConstantPool(DAG.getMachineFunction()), Alignment);
  return Result;
}

/// Expands the Constant node to a load from the constant pool.
SDValue SelectionDAGLegalize::ExpandConstant(ConstantSDNode *CP) {
  SDLoc dl(CP);
  EVT VT = CP->getValueType(0);
  SDValue CPIdx = DAG.getConstantPool(CP->getConstantIntValue(),
                                      TLI.getPointerTy(DAG.getDataLayout()));
  unsigned Alignment = cast<ConstantPoolSDNode>(CPIdx)->getAlignment();
  SDValue Result = DAG.getLoad(
      VT, dl, DAG.getEntryNode(), CPIdx,
      MachinePointerInfo::getConstantPool(DAG.getMachineFunction()), Alignment);
  return Result;
}

/// Some target cannot handle a variable insertion index for the
/// INSERT_VECTOR_ELT instruction.  In this case, it
/// is necessary to spill the vector being inserted into to memory, perform
/// the insert there, and then read the result back.
SDValue SelectionDAGLegalize::PerformInsertVectorEltInMemory(SDValue Vec,
                                                             SDValue Val,
                                                             SDValue Idx,
                                                             const SDLoc &dl) {
  SDValue Tmp1 = Vec;
  SDValue Tmp2 = Val;
  SDValue Tmp3 = Idx;

  // If the target doesn't support this, we have to spill the input vector
  // to a temporary stack slot, update the element, then reload it.  This is
  // badness.  We could also load the value into a vector register (either
  // with a "move to register" or "extload into register" instruction, then
  // permute it into place, if the idx is a constant and if the idx is
  // supported by the target.
  EVT VT    = Tmp1.getValueType();
  EVT EltVT = VT.getVectorElementType();
  SDValue StackPtr = DAG.CreateStackTemporary(VT);

  int SPFI = cast<FrameIndexSDNode>(StackPtr.getNode())->getIndex();

  // Store the vector.
  SDValue Ch = DAG.getStore(
      DAG.getEntryNode(), dl, Tmp1, StackPtr,
      MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), SPFI));

  SDValue StackPtr2 = TLI.getVectorElementPointer(DAG, StackPtr, VT, Tmp3);

  // Store the scalar value.
  Ch = DAG.getTruncStore(Ch, dl, Tmp2, StackPtr2, MachinePointerInfo(), EltVT);
  // Load the updated vector.
  return DAG.getLoad(VT, dl, Ch, StackPtr, MachinePointerInfo::getFixedStack(
                                               DAG.getMachineFunction(), SPFI));
}

SDValue SelectionDAGLegalize::ExpandINSERT_VECTOR_ELT(SDValue Vec, SDValue Val,
                                                      SDValue Idx,
                                                      const SDLoc &dl) {
  if (ConstantSDNode *InsertPos = dyn_cast<ConstantSDNode>(Idx)) {
    // SCALAR_TO_VECTOR requires that the type of the value being inserted
    // match the element type of the vector being created, except for
    // integers in which case the inserted value can be over width.
    EVT EltVT = Vec.getValueType().getVectorElementType();
    if (Val.getValueType() == EltVT ||
        (EltVT.isInteger() && Val.getValueType().bitsGE(EltVT))) {
      SDValue ScVec = DAG.getNode(ISD::SCALAR_TO_VECTOR, dl,
                                  Vec.getValueType(), Val);

      unsigned NumElts = Vec.getValueType().getVectorNumElements();
      // We generate a shuffle of InVec and ScVec, so the shuffle mask
      // should be 0,1,2,3,4,5... with the appropriate element replaced with
      // elt 0 of the RHS.
      SmallVector<int, 8> ShufOps;
      for (unsigned i = 0; i != NumElts; ++i)
        ShufOps.push_back(i != InsertPos->getZExtValue() ? i : NumElts);

      return DAG.getVectorShuffle(Vec.getValueType(), dl, Vec, ScVec, ShufOps);
    }
  }
  return PerformInsertVectorEltInMemory(Vec, Val, Idx, dl);
}

SDValue SelectionDAGLegalize::OptimizeFloatStore(StoreSDNode* ST) {
  LLVM_DEBUG(dbgs() << "Optimizing float store operations\n");
  // Turn 'store float 1.0, Ptr' -> 'store int 0x12345678, Ptr'
  // FIXME: We shouldn't do this for TargetConstantFP's.
  // FIXME: move this to the DAG Combiner!  Note that we can't regress due
  // to phase ordering between legalized code and the dag combiner.  This
  // probably means that we need to integrate dag combiner and legalizer
  // together.
  // We generally can't do this one for long doubles.
  SDValue Chain = ST->getChain();
  SDValue Ptr = ST->getBasePtr();
  unsigned Alignment = ST->getAlignment();
  MachineMemOperand::Flags MMOFlags = ST->getMemOperand()->getFlags();
  AAMDNodes AAInfo = ST->getAAInfo();
  SDLoc dl(ST);
  if (ConstantFPSDNode *CFP = dyn_cast<ConstantFPSDNode>(ST->getValue())) {
    if (CFP->getValueType(0) == MVT::f32 &&
        TLI.isTypeLegal(MVT::i32)) {
      SDValue Con = DAG.getConstant(CFP->getValueAPF().
                                      bitcastToAPInt().zextOrTrunc(32),
                                    SDLoc(CFP), MVT::i32);
      return DAG.getStore(Chain, dl, Con, Ptr, ST->getPointerInfo(), Alignment,
                          MMOFlags, AAInfo);
    }

    if (CFP->getValueType(0) == MVT::f64) {
      // If this target supports 64-bit registers, do a single 64-bit store.
      if (TLI.isTypeLegal(MVT::i64)) {
        SDValue Con = DAG.getConstant(CFP->getValueAPF().bitcastToAPInt().
                                      zextOrTrunc(64), SDLoc(CFP), MVT::i64);
        return DAG.getStore(Chain, dl, Con, Ptr, ST->getPointerInfo(),
                            Alignment, MMOFlags, AAInfo);
      }

      if (TLI.isTypeLegal(MVT::i32) && !ST->isVolatile()) {
        // Otherwise, if the target supports 32-bit registers, use 2 32-bit
        // stores.  If the target supports neither 32- nor 64-bits, this
        // xform is certainly not worth it.
        const APInt &IntVal = CFP->getValueAPF().bitcastToAPInt();
        SDValue Lo = DAG.getConstant(IntVal.trunc(32), dl, MVT::i32);
        SDValue Hi = DAG.getConstant(IntVal.lshr(32).trunc(32), dl, MVT::i32);
        if (DAG.getDataLayout().isBigEndian())
          std::swap(Lo, Hi);

        Lo = DAG.getStore(Chain, dl, Lo, Ptr, ST->getPointerInfo(), Alignment,
                          MMOFlags, AAInfo);
        Ptr = DAG.getNode(ISD::ADD, dl, Ptr.getValueType(), Ptr,
                          DAG.getConstant(4, dl, Ptr.getValueType()));
        Hi = DAG.getStore(Chain, dl, Hi, Ptr,
                          ST->getPointerInfo().getWithOffset(4),
                          MinAlign(Alignment, 4U), MMOFlags, AAInfo);

        return DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Lo, Hi);
      }
    }
  }
  return SDValue(nullptr, 0);
}

void SelectionDAGLegalize::LegalizeStoreOps(SDNode *Node) {
  StoreSDNode *ST = cast<StoreSDNode>(Node);
  SDValue Chain = ST->getChain();
  SDValue Ptr = ST->getBasePtr();
  SDLoc dl(Node);

  unsigned Alignment = ST->getAlignment();
  MachineMemOperand::Flags MMOFlags = ST->getMemOperand()->getFlags();
  AAMDNodes AAInfo = ST->getAAInfo();

  if (!ST->isTruncatingStore()) {
    LLVM_DEBUG(dbgs() << "Legalizing store operation\n");
    if (SDNode *OptStore = OptimizeFloatStore(ST).getNode()) {
      ReplaceNode(ST, OptStore);
      return;
    }

    SDValue Value = ST->getValue();
    MVT VT = Value.getSimpleValueType();
    switch (TLI.getOperationAction(ISD::STORE, VT)) {
    default: llvm_unreachable("This action is not supported yet!");
    case TargetLowering::Legal: {
      // If this is an unaligned store and the target doesn't support it,
      // expand it.
      EVT MemVT = ST->getMemoryVT();
      unsigned AS = ST->getAddressSpace();
      unsigned Align = ST->getAlignment();
      const DataLayout &DL = DAG.getDataLayout();
      if (!TLI.allowsMemoryAccess(*DAG.getContext(), DL, MemVT, AS, Align)) {
        LLVM_DEBUG(dbgs() << "Expanding unsupported unaligned store\n");
        SDValue Result = TLI.expandUnalignedStore(ST, DAG);
        ReplaceNode(SDValue(ST, 0), Result);
      } else
        LLVM_DEBUG(dbgs() << "Legal store\n");
      break;
    }
    case TargetLowering::Custom: {
      LLVM_DEBUG(dbgs() << "Trying custom lowering\n");
      SDValue Res = TLI.LowerOperation(SDValue(Node, 0), DAG);
      if (Res && Res != SDValue(Node, 0))
        ReplaceNode(SDValue(Node, 0), Res);
      return;
    }
    case TargetLowering::Promote: {
      MVT NVT = TLI.getTypeToPromoteTo(ISD::STORE, VT);
      assert(NVT.getSizeInBits() == VT.getSizeInBits() &&
             "Can only promote stores to same size type");
      Value = DAG.getNode(ISD::BITCAST, dl, NVT, Value);
      SDValue Result =
          DAG.getStore(Chain, dl, Value, Ptr, ST->getPointerInfo(),
                       Alignment, MMOFlags, AAInfo);
      ReplaceNode(SDValue(Node, 0), Result);
      break;
    }
    }
    return;
  }

  LLVM_DEBUG(dbgs() << "Legalizing truncating store operations\n");
  SDValue Value = ST->getValue();
  EVT StVT = ST->getMemoryVT();
  unsigned StWidth = StVT.getSizeInBits();
  auto &DL = DAG.getDataLayout();

  if (StWidth != StVT.getStoreSizeInBits()) {
    // Promote to a byte-sized store with upper bits zero if not
    // storing an integral number of bytes.  For example, promote
    // TRUNCSTORE:i1 X -> TRUNCSTORE:i8 (and X, 1)
    EVT NVT = EVT::getIntegerVT(*DAG.getContext(),
                                StVT.getStoreSizeInBits());
    Value = DAG.getZeroExtendInReg(Value, dl, StVT);
    SDValue Result =
        DAG.getTruncStore(Chain, dl, Value, Ptr, ST->getPointerInfo(), NVT,
                          Alignment, MMOFlags, AAInfo);
    ReplaceNode(SDValue(Node, 0), Result);
  } else if (StWidth & (StWidth - 1)) {
    // If not storing a power-of-2 number of bits, expand as two stores.
    assert(!StVT.isVector() && "Unsupported truncstore!");
    unsigned RoundWidth = 1 << Log2_32(StWidth);
    assert(RoundWidth < StWidth);
    unsigned ExtraWidth = StWidth - RoundWidth;
    assert(ExtraWidth < RoundWidth);
    assert(!(RoundWidth % 8) && !(ExtraWidth % 8) &&
           "Store size not an integral number of bytes!");
    EVT RoundVT = EVT::getIntegerVT(*DAG.getContext(), RoundWidth);
    EVT ExtraVT = EVT::getIntegerVT(*DAG.getContext(), ExtraWidth);
    SDValue Lo, Hi;
    unsigned IncrementSize;

    if (DL.isLittleEndian()) {
      // TRUNCSTORE:i24 X -> TRUNCSTORE:i16 X, TRUNCSTORE@+2:i8 (srl X, 16)
      // Store the bottom RoundWidth bits.
      Lo = DAG.getTruncStore(Chain, dl, Value, Ptr, ST->getPointerInfo(),
                             RoundVT, Alignment, MMOFlags, AAInfo);

      // Store the remaining ExtraWidth bits.
      IncrementSize = RoundWidth / 8;
      Ptr = DAG.getNode(ISD::ADD, dl, Ptr.getValueType(), Ptr,
                        DAG.getConstant(IncrementSize, dl,
                                        Ptr.getValueType()));
      Hi = DAG.getNode(
          ISD::SRL, dl, Value.getValueType(), Value,
          DAG.getConstant(RoundWidth, dl,
                          TLI.getShiftAmountTy(Value.getValueType(), DL)));
      Hi = DAG.getTruncStore(
          Chain, dl, Hi, Ptr,
          ST->getPointerInfo().getWithOffset(IncrementSize), ExtraVT,
          MinAlign(Alignment, IncrementSize), MMOFlags, AAInfo);
    } else {
      // Big endian - avoid unaligned stores.
      // TRUNCSTORE:i24 X -> TRUNCSTORE:i16 (srl X, 8), TRUNCSTORE@+2:i8 X
      // Store the top RoundWidth bits.
      Hi = DAG.getNode(
          ISD::SRL, dl, Value.getValueType(), Value,
          DAG.getConstant(ExtraWidth, dl,
                          TLI.getShiftAmountTy(Value.getValueType(), DL)));
      Hi = DAG.getTruncStore(Chain, dl, Hi, Ptr, ST->getPointerInfo(),
                             RoundVT, Alignment, MMOFlags, AAInfo);

      // Store the remaining ExtraWidth bits.
      IncrementSize = RoundWidth / 8;
      Ptr = DAG.getNode(ISD::ADD, dl, Ptr.getValueType(), Ptr,
                        DAG.getConstant(IncrementSize, dl,
                                        Ptr.getValueType()));
      Lo = DAG.getTruncStore(
          Chain, dl, Value, Ptr,
          ST->getPointerInfo().getWithOffset(IncrementSize), ExtraVT,
          MinAlign(Alignment, IncrementSize), MMOFlags, AAInfo);
    }

    // The order of the stores doesn't matter.
    SDValue Result = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Lo, Hi);
    ReplaceNode(SDValue(Node, 0), Result);
  } else {
    switch (TLI.getTruncStoreAction(ST->getValue().getValueType(), StVT)) {
    default: llvm_unreachable("This action is not supported yet!");
    case TargetLowering::Legal: {
      EVT MemVT = ST->getMemoryVT();
      unsigned AS = ST->getAddressSpace();
      unsigned Align = ST->getAlignment();
      // If this is an unaligned store and the target doesn't support it,
      // expand it.
      if (!TLI.allowsMemoryAccess(*DAG.getContext(), DL, MemVT, AS, Align)) {
        SDValue Result = TLI.expandUnalignedStore(ST, DAG);
        ReplaceNode(SDValue(ST, 0), Result);
      }
      break;
    }
    case TargetLowering::Custom: {
      SDValue Res = TLI.LowerOperation(SDValue(Node, 0), DAG);
      if (Res && Res != SDValue(Node, 0))
        ReplaceNode(SDValue(Node, 0), Res);
      return;
    }
    case TargetLowering::Expand:
      assert(!StVT.isVector() &&
             "Vector Stores are handled in LegalizeVectorOps");

      SDValue Result;

      // TRUNCSTORE:i16 i32 -> STORE i16
      if (TLI.isTypeLegal(StVT)) {
        Value = DAG.getNode(ISD::TRUNCATE, dl, StVT, Value);
        Result = DAG.getStore(Chain, dl, Value, Ptr, ST->getPointerInfo(),
                              Alignment, MMOFlags, AAInfo);
      } else {
        // The in-memory type isn't legal. Truncate to the type it would promote
        // to, and then do a truncstore.
        Value = DAG.getNode(ISD::TRUNCATE, dl,
                            TLI.getTypeToTransformTo(*DAG.getContext(), StVT),
                            Value);
        Result = DAG.getTruncStore(Chain, dl, Value, Ptr, ST->getPointerInfo(),
                                   StVT, Alignment, MMOFlags, AAInfo);
      }

      ReplaceNode(SDValue(Node, 0), Result);
      break;
    }
  }
}

void SelectionDAGLegalize::LegalizeLoadOps(SDNode *Node) {
  LoadSDNode *LD = cast<LoadSDNode>(Node);
  SDValue Chain = LD->getChain();  // The chain.
  SDValue Ptr = LD->getBasePtr();  // The base pointer.
  SDValue Value;                   // The value returned by the load op.
  SDLoc dl(Node);

  ISD::LoadExtType ExtType = LD->getExtensionType();
  if (ExtType == ISD::NON_EXTLOAD) {
    LLVM_DEBUG(dbgs() << "Legalizing non-extending load operation\n");
    MVT VT = Node->getSimpleValueType(0);
    SDValue RVal = SDValue(Node, 0);
    SDValue RChain = SDValue(Node, 1);

    switch (TLI.getOperationAction(Node->getOpcode(), VT)) {
    default: llvm_unreachable("This action is not supported yet!");
    case TargetLowering::Legal: {
      EVT MemVT = LD->getMemoryVT();
      unsigned AS = LD->getAddressSpace();
      unsigned Align = LD->getAlignment();
      const DataLayout &DL = DAG.getDataLayout();
      // If this is an unaligned load and the target doesn't support it,
      // expand it.
      if (!TLI.allowsMemoryAccess(*DAG.getContext(), DL, MemVT, AS, Align)) {
        std::tie(RVal, RChain) =  TLI.expandUnalignedLoad(LD, DAG);
      }
      break;
    }
    case TargetLowering::Custom:
      if (SDValue Res = TLI.LowerOperation(RVal, DAG)) {
        RVal = Res;
        RChain = Res.getValue(1);
      }
      break;

    case TargetLowering::Promote: {
      MVT NVT = TLI.getTypeToPromoteTo(Node->getOpcode(), VT);
      assert(NVT.getSizeInBits() == VT.getSizeInBits() &&
             "Can only promote loads to same size type");

      SDValue Res = DAG.getLoad(NVT, dl, Chain, Ptr, LD->getMemOperand());
      RVal = DAG.getNode(ISD::BITCAST, dl, VT, Res);
      RChain = Res.getValue(1);
      break;
    }
    }
    if (RChain.getNode() != Node) {
      assert(RVal.getNode() != Node && "Load must be completely replaced");
      DAG.ReplaceAllUsesOfValueWith(SDValue(Node, 0), RVal);
      DAG.ReplaceAllUsesOfValueWith(SDValue(Node, 1), RChain);
      if (UpdatedNodes) {
        UpdatedNodes->insert(RVal.getNode());
        UpdatedNodes->insert(RChain.getNode());
      }
      ReplacedNode(Node);
    }
    return;
  }

  LLVM_DEBUG(dbgs() << "Legalizing extending load operation\n");
  EVT SrcVT = LD->getMemoryVT();
  unsigned SrcWidth = SrcVT.getSizeInBits();
  unsigned Alignment = LD->getAlignment();
  MachineMemOperand::Flags MMOFlags = LD->getMemOperand()->getFlags();
  AAMDNodes AAInfo = LD->getAAInfo();

  if (SrcWidth != SrcVT.getStoreSizeInBits() &&
      // Some targets pretend to have an i1 loading operation, and actually
      // load an i8.  This trick is correct for ZEXTLOAD because the top 7
      // bits are guaranteed to be zero; it helps the optimizers understand
      // that these bits are zero.  It is also useful for EXTLOAD, since it
      // tells the optimizers that those bits are undefined.  It would be
      // nice to have an effective generic way of getting these benefits...
      // Until such a way is found, don't insist on promoting i1 here.
      (SrcVT != MVT::i1 ||
       TLI.getLoadExtAction(ExtType, Node->getValueType(0), MVT::i1) ==
         TargetLowering::Promote)) {
    // Promote to a byte-sized load if not loading an integral number of
    // bytes.  For example, promote EXTLOAD:i20 -> EXTLOAD:i24.
    unsigned NewWidth = SrcVT.getStoreSizeInBits();
    EVT NVT = EVT::getIntegerVT(*DAG.getContext(), NewWidth);
    SDValue Ch;

    // The extra bits are guaranteed to be zero, since we stored them that
    // way.  A zext load from NVT thus automatically gives zext from SrcVT.

    ISD::LoadExtType NewExtType =
      ExtType == ISD::ZEXTLOAD ? ISD::ZEXTLOAD : ISD::EXTLOAD;

    SDValue Result =
        DAG.getExtLoad(NewExtType, dl, Node->getValueType(0), Chain, Ptr,
                       LD->getPointerInfo(), NVT, Alignment, MMOFlags, AAInfo);

    Ch = Result.getValue(1); // The chain.

    if (ExtType == ISD::SEXTLOAD)
      // Having the top bits zero doesn't help when sign extending.
      Result = DAG.getNode(ISD::SIGN_EXTEND_INREG, dl,
                           Result.getValueType(),
                           Result, DAG.getValueType(SrcVT));
    else if (ExtType == ISD::ZEXTLOAD || NVT == Result.getValueType())
      // All the top bits are guaranteed to be zero - inform the optimizers.
      Result = DAG.getNode(ISD::AssertZext, dl,
                           Result.getValueType(), Result,
                           DAG.getValueType(SrcVT));

    Value = Result;
    Chain = Ch;
  } else if (SrcWidth & (SrcWidth - 1)) {
    // If not loading a power-of-2 number of bits, expand as two loads.
    assert(!SrcVT.isVector() && "Unsupported extload!");
    unsigned RoundWidth = 1 << Log2_32(SrcWidth);
    assert(RoundWidth < SrcWidth);
    unsigned ExtraWidth = SrcWidth - RoundWidth;
    assert(ExtraWidth < RoundWidth);
    assert(!(RoundWidth % 8) && !(ExtraWidth % 8) &&
           "Load size not an integral number of bytes!");
    EVT RoundVT = EVT::getIntegerVT(*DAG.getContext(), RoundWidth);
    EVT ExtraVT = EVT::getIntegerVT(*DAG.getContext(), ExtraWidth);
    SDValue Lo, Hi, Ch;
    unsigned IncrementSize;
    auto &DL = DAG.getDataLayout();

    if (DL.isLittleEndian()) {
      // EXTLOAD:i24 -> ZEXTLOAD:i16 | (shl EXTLOAD@+2:i8, 16)
      // Load the bottom RoundWidth bits.
      Lo = DAG.getExtLoad(ISD::ZEXTLOAD, dl, Node->getValueType(0), Chain, Ptr,
                          LD->getPointerInfo(), RoundVT, Alignment, MMOFlags,
                          AAInfo);

      // Load the remaining ExtraWidth bits.
      IncrementSize = RoundWidth / 8;
      Ptr = DAG.getNode(ISD::ADD, dl, Ptr.getValueType(), Ptr,
                         DAG.getConstant(IncrementSize, dl,
                                         Ptr.getValueType()));
      Hi = DAG.getExtLoad(ExtType, dl, Node->getValueType(0), Chain, Ptr,
                          LD->getPointerInfo().getWithOffset(IncrementSize),
                          ExtraVT, MinAlign(Alignment, IncrementSize), MMOFlags,
                          AAInfo);

      // Build a factor node to remember that this load is independent of
      // the other one.
      Ch = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Lo.getValue(1),
                       Hi.getValue(1));

      // Move the top bits to the right place.
      Hi = DAG.getNode(
          ISD::SHL, dl, Hi.getValueType(), Hi,
          DAG.getConstant(RoundWidth, dl,
                          TLI.getShiftAmountTy(Hi.getValueType(), DL)));

      // Join the hi and lo parts.
      Value = DAG.getNode(ISD::OR, dl, Node->getValueType(0), Lo, Hi);
    } else {
      // Big endian - avoid unaligned loads.
      // EXTLOAD:i24 -> (shl EXTLOAD:i16, 8) | ZEXTLOAD@+2:i8
      // Load the top RoundWidth bits.
      Hi = DAG.getExtLoad(ExtType, dl, Node->getValueType(0), Chain, Ptr,
                          LD->getPointerInfo(), RoundVT, Alignment, MMOFlags,
                          AAInfo);

      // Load the remaining ExtraWidth bits.
      IncrementSize = RoundWidth / 8;
      Ptr = DAG.getNode(ISD::ADD, dl, Ptr.getValueType(), Ptr,
                         DAG.getConstant(IncrementSize, dl,
                                         Ptr.getValueType()));
      Lo = DAG.getExtLoad(ISD::ZEXTLOAD, dl, Node->getValueType(0), Chain, Ptr,
                          LD->getPointerInfo().getWithOffset(IncrementSize),
                          ExtraVT, MinAlign(Alignment, IncrementSize), MMOFlags,
                          AAInfo);

      // Build a factor node to remember that this load is independent of
      // the other one.
      Ch = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Lo.getValue(1),
                       Hi.getValue(1));

      // Move the top bits to the right place.
      Hi = DAG.getNode(
          ISD::SHL, dl, Hi.getValueType(), Hi,
          DAG.getConstant(ExtraWidth, dl,
                          TLI.getShiftAmountTy(Hi.getValueType(), DL)));

      // Join the hi and lo parts.
      Value = DAG.getNode(ISD::OR, dl, Node->getValueType(0), Lo, Hi);
    }

    Chain = Ch;
  } else {
    bool isCustom = false;
    switch (TLI.getLoadExtAction(ExtType, Node->getValueType(0),
                                 SrcVT.getSimpleVT())) {
    default: llvm_unreachable("This action is not supported yet!");
    case TargetLowering::Custom:
      isCustom = true;
      LLVM_FALLTHROUGH;
    case TargetLowering::Legal:
      Value = SDValue(Node, 0);
      Chain = SDValue(Node, 1);

      if (isCustom) {
        if (SDValue Res = TLI.LowerOperation(SDValue(Node, 0), DAG)) {
          Value = Res;
          Chain = Res.getValue(1);
        }
      } else {
        // If this is an unaligned load and the target doesn't support it,
        // expand it.
        EVT MemVT = LD->getMemoryVT();
        unsigned AS = LD->getAddressSpace();
        unsigned Align = LD->getAlignment();
        const DataLayout &DL = DAG.getDataLayout();
        if (!TLI.allowsMemoryAccess(*DAG.getContext(), DL, MemVT, AS, Align)) {
          std::tie(Value, Chain) = TLI.expandUnalignedLoad(LD, DAG);
        }
      }
      break;

    case TargetLowering::Expand: {
      EVT DestVT = Node->getValueType(0);
      if (!TLI.isLoadExtLegal(ISD::EXTLOAD, DestVT, SrcVT)) {
        // If the source type is not legal, see if there is a legal extload to
        // an intermediate type that we can then extend further.
        EVT LoadVT = TLI.getRegisterType(SrcVT.getSimpleVT());
        if (TLI.isTypeLegal(SrcVT) || // Same as SrcVT == LoadVT?
            TLI.isLoadExtLegal(ExtType, LoadVT, SrcVT)) {
          // If we are loading a legal type, this is a non-extload followed by a
          // full extend.
          ISD::LoadExtType MidExtType =
              (LoadVT == SrcVT) ? ISD::NON_EXTLOAD : ExtType;

          SDValue Load = DAG.getExtLoad(MidExtType, dl, LoadVT, Chain, Ptr,
                                        SrcVT, LD->getMemOperand());
          unsigned ExtendOp =
              ISD::getExtForLoadExtType(SrcVT.isFloatingPoint(), ExtType);
          Value = DAG.getNode(ExtendOp, dl, Node->getValueType(0), Load);
          Chain = Load.getValue(1);
          break;
        }

        // Handle the special case of fp16 extloads. EXTLOAD doesn't have the
        // normal undefined upper bits behavior to allow using an in-reg extend
        // with the illegal FP type, so load as an integer and do the
        // from-integer conversion.
        if (SrcVT.getScalarType() == MVT::f16) {
          EVT ISrcVT = SrcVT.changeTypeToInteger();
          EVT IDestVT = DestVT.changeTypeToInteger();
          EVT LoadVT = TLI.getRegisterType(IDestVT.getSimpleVT());

          SDValue Result = DAG.getExtLoad(ISD::ZEXTLOAD, dl, LoadVT,
                                          Chain, Ptr, ISrcVT,
                                          LD->getMemOperand());
          Value = DAG.getNode(ISD::FP16_TO_FP, dl, DestVT, Result);
          Chain = Result.getValue(1);
          break;
        }
      }

      assert(!SrcVT.isVector() &&
             "Vector Loads are handled in LegalizeVectorOps");

      // FIXME: This does not work for vectors on most targets.  Sign-
      // and zero-extend operations are currently folded into extending
      // loads, whether they are legal or not, and then we end up here
      // without any support for legalizing them.
      assert(ExtType != ISD::EXTLOAD &&
             "EXTLOAD should always be supported!");
      // Turn the unsupported load into an EXTLOAD followed by an
      // explicit zero/sign extend inreg.
      SDValue Result = DAG.getExtLoad(ISD::EXTLOAD, dl,
                                      Node->getValueType(0),
                                      Chain, Ptr, SrcVT,
                                      LD->getMemOperand());
      SDValue ValRes;
      if (ExtType == ISD::SEXTLOAD)
        ValRes = DAG.getNode(ISD::SIGN_EXTEND_INREG, dl,
                             Result.getValueType(),
                             Result, DAG.getValueType(SrcVT));
      else
        ValRes = DAG.getZeroExtendInReg(Result, dl, SrcVT.getScalarType());
      Value = ValRes;
      Chain = Result.getValue(1);
      break;
    }
    }
  }

  // Since loads produce two values, make sure to remember that we legalized
  // both of them.
  if (Chain.getNode() != Node) {
    assert(Value.getNode() != Node && "Load must be completely replaced");
    DAG.ReplaceAllUsesOfValueWith(SDValue(Node, 0), Value);
    DAG.ReplaceAllUsesOfValueWith(SDValue(Node, 1), Chain);
    if (UpdatedNodes) {
      UpdatedNodes->insert(Value.getNode());
      UpdatedNodes->insert(Chain.getNode());
    }
    ReplacedNode(Node);
  }
}

/// Return a legal replacement for the given operation, with all legal operands.
void SelectionDAGLegalize::LegalizeOp(SDNode *Node) {
  LLVM_DEBUG(dbgs() << "\nLegalizing: "; Node->dump(&DAG));

  // Allow illegal target nodes and illegal registers.
  if (Node->getOpcode() == ISD::TargetConstant ||
      Node->getOpcode() == ISD::Register)
    return;

#ifndef NDEBUG
  for (unsigned i = 0, e = Node->getNumValues(); i != e; ++i)
    assert((TLI.getTypeAction(*DAG.getContext(), Node->getValueType(i)) ==
              TargetLowering::TypeLegal ||
            TLI.isTypeLegal(Node->getValueType(i))) &&
           "Unexpected illegal type!");

  for (const SDValue &Op : Node->op_values())
    assert((TLI.getTypeAction(*DAG.getContext(), Op.getValueType()) ==
              TargetLowering::TypeLegal ||
            TLI.isTypeLegal(Op.getValueType()) ||
            Op.getOpcode() == ISD::TargetConstant ||
            Op.getOpcode() == ISD::Register) &&
            "Unexpected illegal type!");
#endif

  // Figure out the correct action; the way to query this varies by opcode
  TargetLowering::LegalizeAction Action = TargetLowering::Legal;
  bool SimpleFinishLegalizing = true;
  switch (Node->getOpcode()) {
  case ISD::INTRINSIC_W_CHAIN:
  case ISD::INTRINSIC_WO_CHAIN:
  case ISD::INTRINSIC_VOID:
  case ISD::STACKSAVE:
    Action = TLI.getOperationAction(Node->getOpcode(), MVT::Other);
    break;
  case ISD::GET_DYNAMIC_AREA_OFFSET:
    Action = TLI.getOperationAction(Node->getOpcode(),
                                    Node->getValueType(0));
    break;
  case ISD::VAARG:
    Action = TLI.getOperationAction(Node->getOpcode(),
                                    Node->getValueType(0));
    if (Action != TargetLowering::Promote)
      Action = TLI.getOperationAction(Node->getOpcode(), MVT::Other);
    break;
  case ISD::FP_TO_FP16:
  case ISD::SINT_TO_FP:
  case ISD::UINT_TO_FP:
  case ISD::EXTRACT_VECTOR_ELT:
    Action = TLI.getOperationAction(Node->getOpcode(),
                                    Node->getOperand(0).getValueType());
    break;
  case ISD::FP_ROUND_INREG:
  case ISD::SIGN_EXTEND_INREG: {
    EVT InnerType = cast<VTSDNode>(Node->getOperand(1))->getVT();
    Action = TLI.getOperationAction(Node->getOpcode(), InnerType);
    break;
  }
  case ISD::ATOMIC_STORE:
    Action = TLI.getOperationAction(Node->getOpcode(),
                                    Node->getOperand(2).getValueType());
    break;
  case ISD::SELECT_CC:
  case ISD::SETCC:
  case ISD::BR_CC: {
    unsigned CCOperand = Node->getOpcode() == ISD::SELECT_CC ? 4 :
                         Node->getOpcode() == ISD::SETCC ? 2 : 1;
    unsigned CompareOperand = Node->getOpcode() == ISD::BR_CC ? 2 : 0;
    MVT OpVT = Node->getOperand(CompareOperand).getSimpleValueType();
    ISD::CondCode CCCode =
        cast<CondCodeSDNode>(Node->getOperand(CCOperand))->get();
    Action = TLI.getCondCodeAction(CCCode, OpVT);
    if (Action == TargetLowering::Legal) {
      if (Node->getOpcode() == ISD::SELECT_CC)
        Action = TLI.getOperationAction(Node->getOpcode(),
                                        Node->getValueType(0));
      else
        Action = TLI.getOperationAction(Node->getOpcode(), OpVT);
    }
    break;
  }
  case ISD::LOAD:
  case ISD::STORE:
    // FIXME: Model these properly.  LOAD and STORE are complicated, and
    // STORE expects the unlegalized operand in some cases.
    SimpleFinishLegalizing = false;
    break;
  case ISD::CALLSEQ_START:
  case ISD::CALLSEQ_END:
    // FIXME: This shouldn't be necessary.  These nodes have special properties
    // dealing with the recursive nature of legalization.  Removing this
    // special case should be done as part of making LegalizeDAG non-recursive.
    SimpleFinishLegalizing = false;
    break;
  case ISD::EXTRACT_ELEMENT:
  case ISD::FLT_ROUNDS_:
  case ISD::MERGE_VALUES:
  case ISD::EH_RETURN:
  case ISD::FRAME_TO_ARGS_OFFSET:
  case ISD::EH_DWARF_CFA:
  case ISD::EH_SJLJ_SETJMP:
  case ISD::EH_SJLJ_LONGJMP:
  case ISD::EH_SJLJ_SETUP_DISPATCH:
    // These operations lie about being legal: when they claim to be legal,
    // they should actually be expanded.
    Action = TLI.getOperationAction(Node->getOpcode(), Node->getValueType(0));
    if (Action == TargetLowering::Legal)
      Action = TargetLowering::Expand;
    break;
  case ISD::INIT_TRAMPOLINE:
  case ISD::ADJUST_TRAMPOLINE:
  case ISD::FRAMEADDR:
  case ISD::RETURNADDR:
  case ISD::ADDROFRETURNADDR:
  case ISD::SPONENTRY:
    // These operations lie about being legal: when they claim to be legal,
    // they should actually be custom-lowered.
    Action = TLI.getOperationAction(Node->getOpcode(), Node->getValueType(0));
    if (Action == TargetLowering::Legal)
      Action = TargetLowering::Custom;
    break;
  case ISD::READCYCLECOUNTER:
    // READCYCLECOUNTER returns an i64, even if type legalization might have
    // expanded that to several smaller types.
    Action = TLI.getOperationAction(Node->getOpcode(), MVT::i64);
    break;
  case ISD::READ_REGISTER:
  case ISD::WRITE_REGISTER:
    // Named register is legal in the DAG, but blocked by register name
    // selection if not implemented by target (to chose the correct register)
    // They'll be converted to Copy(To/From)Reg.
    Action = TargetLowering::Legal;
    break;
  case ISD::DEBUGTRAP:
    Action = TLI.getOperationAction(Node->getOpcode(), Node->getValueType(0));
    if (Action == TargetLowering::Expand) {
      // replace ISD::DEBUGTRAP with ISD::TRAP
      SDValue NewVal;
      NewVal = DAG.getNode(ISD::TRAP, SDLoc(Node), Node->getVTList(),
                           Node->getOperand(0));
      ReplaceNode(Node, NewVal.getNode());
      LegalizeOp(NewVal.getNode());
      return;
    }
    break;
  case ISD::STRICT_FADD:
  case ISD::STRICT_FSUB:
  case ISD::STRICT_FMUL:
  case ISD::STRICT_FDIV:
  case ISD::STRICT_FREM:
  case ISD::STRICT_FSQRT:
  case ISD::STRICT_FMA:
  case ISD::STRICT_FPOW:
  case ISD::STRICT_FPOWI:
  case ISD::STRICT_FSIN:
  case ISD::STRICT_FCOS:
  case ISD::STRICT_FEXP:
  case ISD::STRICT_FEXP2:
  case ISD::STRICT_FLOG:
  case ISD::STRICT_FLOG10:
  case ISD::STRICT_FLOG2:
  case ISD::STRICT_FRINT:
  case ISD::STRICT_FNEARBYINT:
  case ISD::STRICT_FMAXNUM:
  case ISD::STRICT_FMINNUM:
  case ISD::STRICT_FCEIL:
  case ISD::STRICT_FFLOOR:
  case ISD::STRICT_FROUND:
  case ISD::STRICT_FTRUNC:
    // These pseudo-ops get legalized as if they were their non-strict
    // equivalent.  For instance, if ISD::FSQRT is legal then ISD::STRICT_FSQRT
    // is also legal, but if ISD::FSQRT requires expansion then so does
    // ISD::STRICT_FSQRT.
    Action = TLI.getStrictFPOperationAction(Node->getOpcode(),
                                            Node->getValueType(0));
    break;
  case ISD::SADDSAT:
  case ISD::UADDSAT:
  case ISD::SSUBSAT:
  case ISD::USUBSAT: {
    Action = TLI.getOperationAction(Node->getOpcode(), Node->getValueType(0));
    break;
  }
  case ISD::SMULFIX: {
    unsigned Scale = Node->getConstantOperandVal(2);
    Action = TLI.getFixedPointOperationAction(Node->getOpcode(),
                                              Node->getValueType(0), Scale);
    break;
  }
  case ISD::MSCATTER:
    Action = TLI.getOperationAction(Node->getOpcode(),
                    cast<MaskedScatterSDNode>(Node)->getValue().getValueType());
    break;
  case ISD::MSTORE:
    Action = TLI.getOperationAction(Node->getOpcode(),
                    cast<MaskedStoreSDNode>(Node)->getValue().getValueType());
    break;
  default:
    if (Node->getOpcode() >= ISD::BUILTIN_OP_END) {
      Action = TargetLowering::Legal;
    } else {
      Action = TLI.getOperationAction(Node->getOpcode(), Node->getValueType(0));
    }
    break;
  }

  if (SimpleFinishLegalizing) {
    SDNode *NewNode = Node;
    switch (Node->getOpcode()) {
    default: break;
    case ISD::SHL:
    case ISD::SRL:
    case ISD::SRA:
    case ISD::ROTL:
    case ISD::ROTR: {
      // Legalizing shifts/rotates requires adjusting the shift amount
      // to the appropriate width.
      SDValue Op0 = Node->getOperand(0);
      SDValue Op1 = Node->getOperand(1);
      if (!Op1.getValueType().isVector()) {
        SDValue SAO = DAG.getShiftAmountOperand(Op0.getValueType(), Op1);
        // The getShiftAmountOperand() may create a new operand node or
        // return the existing one. If new operand is created we need
        // to update the parent node.
        // Do not try to legalize SAO here! It will be automatically legalized
        // in the next round.
        if (SAO != Op1)
          NewNode = DAG.UpdateNodeOperands(Node, Op0, SAO);
      }
    }
    break;
    case ISD::FSHL:
    case ISD::FSHR:
    case ISD::SRL_PARTS:
    case ISD::SRA_PARTS:
    case ISD::SHL_PARTS: {
      // Legalizing shifts/rotates requires adjusting the shift amount
      // to the appropriate width.
      SDValue Op0 = Node->getOperand(0);
      SDValue Op1 = Node->getOperand(1);
      SDValue Op2 = Node->getOperand(2);
      if (!Op2.getValueType().isVector()) {
        SDValue SAO = DAG.getShiftAmountOperand(Op0.getValueType(), Op2);
        // The getShiftAmountOperand() may create a new operand node or
        // return the existing one. If new operand is created we need
        // to update the parent node.
        if (SAO != Op2)
          NewNode = DAG.UpdateNodeOperands(Node, Op0, Op1, SAO);
      }
      break;
    }
    }

    if (NewNode != Node) {
      ReplaceNode(Node, NewNode);
      Node = NewNode;
    }
    switch (Action) {
    case TargetLowering::Legal:
      LLVM_DEBUG(dbgs() << "Legal node: nothing to do\n");
      return;
    case TargetLowering::Custom:
      LLVM_DEBUG(dbgs() << "Trying custom legalization\n");
      // FIXME: The handling for custom lowering with multiple results is
      // a complete mess.
      if (SDValue Res = TLI.LowerOperation(SDValue(Node, 0), DAG)) {
        if (!(Res.getNode() != Node || Res.getResNo() != 0))
          return;

        if (Node->getNumValues() == 1) {
          LLVM_DEBUG(dbgs() << "Successfully custom legalized node\n");
          // We can just directly replace this node with the lowered value.
          ReplaceNode(SDValue(Node, 0), Res);
          return;
        }

        SmallVector<SDValue, 8> ResultVals;
        for (unsigned i = 0, e = Node->getNumValues(); i != e; ++i)
          ResultVals.push_back(Res.getValue(i));
        LLVM_DEBUG(dbgs() << "Successfully custom legalized node\n");
        ReplaceNode(Node, ResultVals.data());
        return;
      }
      LLVM_DEBUG(dbgs() << "Could not custom legalize node\n");
      LLVM_FALLTHROUGH;
    case TargetLowering::Expand:
      if (ExpandNode(Node))
        return;
      LLVM_FALLTHROUGH;
    case TargetLowering::LibCall:
      ConvertNodeToLibcall(Node);
      return;
    case TargetLowering::Promote:
      PromoteNode(Node);
      return;
    }
  }

  switch (Node->getOpcode()) {
  default:
#ifndef NDEBUG
    dbgs() << "NODE: ";
    Node->dump( &DAG);
    dbgs() << "\n";
#endif
    llvm_unreachable("Do not know how to legalize this operator!");

  case ISD::CALLSEQ_START:
  case ISD::CALLSEQ_END:
    break;
  case ISD::LOAD:
    return LegalizeLoadOps(Node);
  case ISD::STORE:
    return LegalizeStoreOps(Node);
  }
}

SDValue SelectionDAGLegalize::ExpandExtractFromVectorThroughStack(SDValue Op) {
  SDValue Vec = Op.getOperand(0);
  SDValue Idx = Op.getOperand(1);
  SDLoc dl(Op);

  // Before we generate a new store to a temporary stack slot, see if there is
  // already one that we can use. There often is because when we scalarize
  // vector operations (using SelectionDAG::UnrollVectorOp for example) a whole
  // series of EXTRACT_VECTOR_ELT nodes are generated, one for each element in
  // the vector. If all are expanded here, we don't want one store per vector
  // element.

  // Caches for hasPredecessorHelper
  SmallPtrSet<const SDNode *, 32> Visited;
  SmallVector<const SDNode *, 16> Worklist;
  Visited.insert(Op.getNode());
  Worklist.push_back(Idx.getNode());
  SDValue StackPtr, Ch;
  for (SDNode::use_iterator UI = Vec.getNode()->use_begin(),
       UE = Vec.getNode()->use_end(); UI != UE; ++UI) {
    SDNode *User = *UI;
    if (StoreSDNode *ST = dyn_cast<StoreSDNode>(User)) {
      if (ST->isIndexed() || ST->isTruncatingStore() ||
          ST->getValue() != Vec)
        continue;

      // Make sure that nothing else could have stored into the destination of
      // this store.
      if (!ST->getChain().reachesChainWithoutSideEffects(DAG.getEntryNode()))
        continue;

      // If the index is dependent on the store we will introduce a cycle when
      // creating the load (the load uses the index, and by replacing the chain
      // we will make the index dependent on the load). Also, the store might be
      // dependent on the extractelement and introduce a cycle when creating
      // the load.
      if (SDNode::hasPredecessorHelper(ST, Visited, Worklist) ||
          ST->hasPredecessor(Op.getNode()))
        continue;

      StackPtr = ST->getBasePtr();
      Ch = SDValue(ST, 0);
      break;
    }
  }

  EVT VecVT = Vec.getValueType();

  if (!Ch.getNode()) {
    // Store the value to a temporary stack slot, then LOAD the returned part.
    StackPtr = DAG.CreateStackTemporary(VecVT);
    Ch = DAG.getStore(DAG.getEntryNode(), dl, Vec, StackPtr,
                      MachinePointerInfo());
  }

  StackPtr = TLI.getVectorElementPointer(DAG, StackPtr, VecVT, Idx);

  SDValue NewLoad;

  if (Op.getValueType().isVector())
    NewLoad =
        DAG.getLoad(Op.getValueType(), dl, Ch, StackPtr, MachinePointerInfo());
  else
    NewLoad = DAG.getExtLoad(ISD::EXTLOAD, dl, Op.getValueType(), Ch, StackPtr,
                             MachinePointerInfo(),
                             VecVT.getVectorElementType());

  // Replace the chain going out of the store, by the one out of the load.
  DAG.ReplaceAllUsesOfValueWith(Ch, SDValue(NewLoad.getNode(), 1));

  // We introduced a cycle though, so update the loads operands, making sure
  // to use the original store's chain as an incoming chain.
  SmallVector<SDValue, 6> NewLoadOperands(NewLoad->op_begin(),
                                          NewLoad->op_end());
  NewLoadOperands[0] = Ch;
  NewLoad =
      SDValue(DAG.UpdateNodeOperands(NewLoad.getNode(), NewLoadOperands), 0);
  return NewLoad;
}

SDValue SelectionDAGLegalize::ExpandInsertToVectorThroughStack(SDValue Op) {
  assert(Op.getValueType().isVector() && "Non-vector insert subvector!");

  SDValue Vec  = Op.getOperand(0);
  SDValue Part = Op.getOperand(1);
  SDValue Idx  = Op.getOperand(2);
  SDLoc dl(Op);

  // Store the value to a temporary stack slot, then LOAD the returned part.
  EVT VecVT = Vec.getValueType();
  SDValue StackPtr = DAG.CreateStackTemporary(VecVT);
  int FI = cast<FrameIndexSDNode>(StackPtr.getNode())->getIndex();
  MachinePointerInfo PtrInfo =
      MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FI);

  // First store the whole vector.
  SDValue Ch = DAG.getStore(DAG.getEntryNode(), dl, Vec, StackPtr, PtrInfo);

  // Then store the inserted part.
  SDValue SubStackPtr = TLI.getVectorElementPointer(DAG, StackPtr, VecVT, Idx);

  // Store the subvector.
  Ch = DAG.getStore(Ch, dl, Part, SubStackPtr, MachinePointerInfo());

  // Finally, load the updated vector.
  return DAG.getLoad(Op.getValueType(), dl, Ch, StackPtr, PtrInfo);
}

SDValue SelectionDAGLegalize::ExpandVectorBuildThroughStack(SDNode* Node) {
  // We can't handle this case efficiently.  Allocate a sufficiently
  // aligned object on the stack, store each element into it, then load
  // the result as a vector.
  // Create the stack frame object.
  EVT VT = Node->getValueType(0);
  EVT EltVT = VT.getVectorElementType();
  SDLoc dl(Node);
  SDValue FIPtr = DAG.CreateStackTemporary(VT);
  int FI = cast<FrameIndexSDNode>(FIPtr.getNode())->getIndex();
  MachinePointerInfo PtrInfo =
      MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FI);

  // Emit a store of each element to the stack slot.
  SmallVector<SDValue, 8> Stores;
  unsigned TypeByteSize = EltVT.getSizeInBits() / 8;
  // Store (in the right endianness) the elements to memory.
  for (unsigned i = 0, e = Node->getNumOperands(); i != e; ++i) {
    // Ignore undef elements.
    if (Node->getOperand(i).isUndef()) continue;

    unsigned Offset = TypeByteSize*i;

    SDValue Idx = DAG.getConstant(Offset, dl, FIPtr.getValueType());
    Idx = DAG.getNode(ISD::ADD, dl, FIPtr.getValueType(), FIPtr, Idx);

    // If the destination vector element type is narrower than the source
    // element type, only store the bits necessary.
    if (EltVT.bitsLT(Node->getOperand(i).getValueType().getScalarType())) {
      Stores.push_back(DAG.getTruncStore(DAG.getEntryNode(), dl,
                                         Node->getOperand(i), Idx,
                                         PtrInfo.getWithOffset(Offset), EltVT));
    } else
      Stores.push_back(DAG.getStore(DAG.getEntryNode(), dl, Node->getOperand(i),
                                    Idx, PtrInfo.getWithOffset(Offset)));
  }

  SDValue StoreChain;
  if (!Stores.empty())    // Not all undef elements?
    StoreChain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Stores);
  else
    StoreChain = DAG.getEntryNode();

  // Result is a load from the stack slot.
  return DAG.getLoad(VT, dl, StoreChain, FIPtr, PtrInfo);
}

/// Bitcast a floating-point value to an integer value. Only bitcast the part
/// containing the sign bit if the target has no integer value capable of
/// holding all bits of the floating-point value.
void SelectionDAGLegalize::getSignAsIntValue(FloatSignAsInt &State,
                                             const SDLoc &DL,
                                             SDValue Value) const {
  EVT FloatVT = Value.getValueType();
  unsigned NumBits = FloatVT.getSizeInBits();
  State.FloatVT = FloatVT;
  EVT IVT = EVT::getIntegerVT(*DAG.getContext(), NumBits);
  // Convert to an integer of the same size.
  if (TLI.isTypeLegal(IVT)) {
    State.IntValue = DAG.getNode(ISD::BITCAST, DL, IVT, Value);
    State.SignMask = APInt::getSignMask(NumBits);
    State.SignBit = NumBits - 1;
    return;
  }

  auto &DataLayout = DAG.getDataLayout();
  // Store the float to memory, then load the sign part out as an integer.
  MVT LoadTy = TLI.getRegisterType(*DAG.getContext(), MVT::i8);
  // First create a temporary that is aligned for both the load and store.
  SDValue StackPtr = DAG.CreateStackTemporary(FloatVT, LoadTy);
  int FI = cast<FrameIndexSDNode>(StackPtr.getNode())->getIndex();
  // Then store the float to it.
  State.FloatPtr = StackPtr;
  MachineFunction &MF = DAG.getMachineFunction();
  State.FloatPointerInfo = MachinePointerInfo::getFixedStack(MF, FI);
  State.Chain = DAG.getStore(DAG.getEntryNode(), DL, Value, State.FloatPtr,
                             State.FloatPointerInfo);

  SDValue IntPtr;
  if (DataLayout.isBigEndian()) {
    assert(FloatVT.isByteSized() && "Unsupported floating point type!");
    // Load out a legal integer with the same sign bit as the float.
    IntPtr = StackPtr;
    State.IntPointerInfo = State.FloatPointerInfo;
  } else {
    // Advance the pointer so that the loaded byte will contain the sign bit.
    unsigned ByteOffset = (FloatVT.getSizeInBits() / 8) - 1;
    IntPtr = DAG.getNode(ISD::ADD, DL, StackPtr.getValueType(), StackPtr,
                      DAG.getConstant(ByteOffset, DL, StackPtr.getValueType()));
    State.IntPointerInfo = MachinePointerInfo::getFixedStack(MF, FI,
                                                             ByteOffset);
  }

  State.IntPtr = IntPtr;
  State.IntValue = DAG.getExtLoad(ISD::EXTLOAD, DL, LoadTy, State.Chain, IntPtr,
                                  State.IntPointerInfo, MVT::i8);
  State.SignMask = APInt::getOneBitSet(LoadTy.getSizeInBits(), 7);
  State.SignBit = 7;
}

/// Replace the integer value produced by getSignAsIntValue() with a new value
/// and cast the result back to a floating-point type.
SDValue SelectionDAGLegalize::modifySignAsInt(const FloatSignAsInt &State,
                                              const SDLoc &DL,
                                              SDValue NewIntValue) const {
  if (!State.Chain)
    return DAG.getNode(ISD::BITCAST, DL, State.FloatVT, NewIntValue);

  // Override the part containing the sign bit in the value stored on the stack.
  SDValue Chain = DAG.getTruncStore(State.Chain, DL, NewIntValue, State.IntPtr,
                                    State.IntPointerInfo, MVT::i8);
  return DAG.getLoad(State.FloatVT, DL, Chain, State.FloatPtr,
                     State.FloatPointerInfo);
}

SDValue SelectionDAGLegalize::ExpandFCOPYSIGN(SDNode *Node) const {
  SDLoc DL(Node);
  SDValue Mag = Node->getOperand(0);
  SDValue Sign = Node->getOperand(1);

  // Get sign bit into an integer value.
  FloatSignAsInt SignAsInt;
  getSignAsIntValue(SignAsInt, DL, Sign);

  EVT IntVT = SignAsInt.IntValue.getValueType();
  SDValue SignMask = DAG.getConstant(SignAsInt.SignMask, DL, IntVT);
  SDValue SignBit = DAG.getNode(ISD::AND, DL, IntVT, SignAsInt.IntValue,
                                SignMask);

  // If FABS is legal transform FCOPYSIGN(x, y) => sign(x) ? -FABS(x) : FABS(X)
  EVT FloatVT = Mag.getValueType();
  if (TLI.isOperationLegalOrCustom(ISD::FABS, FloatVT) &&
      TLI.isOperationLegalOrCustom(ISD::FNEG, FloatVT)) {
    SDValue AbsValue = DAG.getNode(ISD::FABS, DL, FloatVT, Mag);
    SDValue NegValue = DAG.getNode(ISD::FNEG, DL, FloatVT, AbsValue);
    SDValue Cond = DAG.getSetCC(DL, getSetCCResultType(IntVT), SignBit,
                                DAG.getConstant(0, DL, IntVT), ISD::SETNE);
    return DAG.getSelect(DL, FloatVT, Cond, NegValue, AbsValue);
  }

  // Transform Mag value to integer, and clear the sign bit.
  FloatSignAsInt MagAsInt;
  getSignAsIntValue(MagAsInt, DL, Mag);
  EVT MagVT = MagAsInt.IntValue.getValueType();
  SDValue ClearSignMask = DAG.getConstant(~MagAsInt.SignMask, DL, MagVT);
  SDValue ClearedSign = DAG.getNode(ISD::AND, DL, MagVT, MagAsInt.IntValue,
                                    ClearSignMask);

  // Get the signbit at the right position for MagAsInt.
  int ShiftAmount = SignAsInt.SignBit - MagAsInt.SignBit;
  EVT ShiftVT = IntVT;
  if (SignBit.getValueSizeInBits() < ClearedSign.getValueSizeInBits()) {
    SignBit = DAG.getNode(ISD::ZERO_EXTEND, DL, MagVT, SignBit);
    ShiftVT = MagVT;
  }
  if (ShiftAmount > 0) {
    SDValue ShiftCnst = DAG.getConstant(ShiftAmount, DL, ShiftVT);
    SignBit = DAG.getNode(ISD::SRL, DL, ShiftVT, SignBit, ShiftCnst);
  } else if (ShiftAmount < 0) {
    SDValue ShiftCnst = DAG.getConstant(-ShiftAmount, DL, ShiftVT);
    SignBit = DAG.getNode(ISD::SHL, DL, ShiftVT, SignBit, ShiftCnst);
  }
  if (SignBit.getValueSizeInBits() > ClearedSign.getValueSizeInBits()) {
    SignBit = DAG.getNode(ISD::TRUNCATE, DL, MagVT, SignBit);
  }

  // Store the part with the modified sign and convert back to float.
  SDValue CopiedSign = DAG.getNode(ISD::OR, DL, MagVT, ClearedSign, SignBit);
  return modifySignAsInt(MagAsInt, DL, CopiedSign);
}

SDValue SelectionDAGLegalize::ExpandFABS(SDNode *Node) const {
  SDLoc DL(Node);
  SDValue Value = Node->getOperand(0);

  // Transform FABS(x) => FCOPYSIGN(x, 0.0) if FCOPYSIGN is legal.
  EVT FloatVT = Value.getValueType();
  if (TLI.isOperationLegalOrCustom(ISD::FCOPYSIGN, FloatVT)) {
    SDValue Zero = DAG.getConstantFP(0.0, DL, FloatVT);
    return DAG.getNode(ISD::FCOPYSIGN, DL, FloatVT, Value, Zero);
  }

  // Transform value to integer, clear the sign bit and transform back.
  FloatSignAsInt ValueAsInt;
  getSignAsIntValue(ValueAsInt, DL, Value);
  EVT IntVT = ValueAsInt.IntValue.getValueType();
  SDValue ClearSignMask = DAG.getConstant(~ValueAsInt.SignMask, DL, IntVT);
  SDValue ClearedSign = DAG.getNode(ISD::AND, DL, IntVT, ValueAsInt.IntValue,
                                    ClearSignMask);
  return modifySignAsInt(ValueAsInt, DL, ClearedSign);
}

void SelectionDAGLegalize::ExpandDYNAMIC_STACKALLOC(SDNode* Node,
                                           SmallVectorImpl<SDValue> &Results) {
  unsigned SPReg = TLI.getStackPointerRegisterToSaveRestore();
  assert(SPReg && "Target cannot require DYNAMIC_STACKALLOC expansion and"
          " not tell us which reg is the stack pointer!");
  SDLoc dl(Node);
  EVT VT = Node->getValueType(0);
  SDValue Tmp1 = SDValue(Node, 0);
  SDValue Tmp2 = SDValue(Node, 1);
  SDValue Tmp3 = Node->getOperand(2);
  SDValue Chain = Tmp1.getOperand(0);

  // Chain the dynamic stack allocation so that it doesn't modify the stack
  // pointer when other instructions are using the stack.
  Chain = DAG.getCALLSEQ_START(Chain, 0, 0, dl);

  SDValue Size  = Tmp2.getOperand(1);
  SDValue SP = DAG.getCopyFromReg(Chain, dl, SPReg, VT);
  Chain = SP.getValue(1);
  unsigned Align = cast<ConstantSDNode>(Tmp3)->getZExtValue();
  unsigned StackAlign =
      DAG.getSubtarget().getFrameLowering()->getStackAlignment();
  Tmp1 = DAG.getNode(ISD::SUB, dl, VT, SP, Size);       // Value
  if (Align > StackAlign)
    Tmp1 = DAG.getNode(ISD::AND, dl, VT, Tmp1,
                       DAG.getConstant(-(uint64_t)Align, dl, VT));
  Chain = DAG.getCopyToReg(Chain, dl, SPReg, Tmp1);     // Output chain

  Tmp2 = DAG.getCALLSEQ_END(Chain, DAG.getIntPtrConstant(0, dl, true),
                            DAG.getIntPtrConstant(0, dl, true), SDValue(), dl);

  Results.push_back(Tmp1);
  Results.push_back(Tmp2);
}

/// Legalize a SETCC with given LHS and RHS and condition code CC on the current
/// target.
///
/// If the SETCC has been legalized using AND / OR, then the legalized node
/// will be stored in LHS. RHS and CC will be set to SDValue(). NeedInvert
/// will be set to false.
///
/// If the SETCC has been legalized by using getSetCCSwappedOperands(),
/// then the values of LHS and RHS will be swapped, CC will be set to the
/// new condition, and NeedInvert will be set to false.
///
/// If the SETCC has been legalized using the inverse condcode, then LHS and
/// RHS will be unchanged, CC will set to the inverted condcode, and NeedInvert
/// will be set to true. The caller must invert the result of the SETCC with
/// SelectionDAG::getLogicalNOT() or take equivalent action to swap the effect
/// of a true/false result.
///
/// \returns true if the SetCC has been legalized, false if it hasn't.
bool SelectionDAGLegalize::LegalizeSetCCCondCode(EVT VT, SDValue &LHS,
                                                 SDValue &RHS, SDValue &CC,
                                                 bool &NeedInvert,
                                                 const SDLoc &dl) {
  MVT OpVT = LHS.getSimpleValueType();
  ISD::CondCode CCCode = cast<CondCodeSDNode>(CC)->get();
  NeedInvert = false;
  bool NeedSwap = false;
  switch (TLI.getCondCodeAction(CCCode, OpVT)) {
  default: llvm_unreachable("Unknown condition code action!");
  case TargetLowering::Legal:
    // Nothing to do.
    break;
  case TargetLowering::Expand: {
    ISD::CondCode InvCC = ISD::getSetCCSwappedOperands(CCCode);
    if (TLI.isCondCodeLegalOrCustom(InvCC, OpVT)) {
      std::swap(LHS, RHS);
      CC = DAG.getCondCode(InvCC);
      return true;
    }
    // Swapping operands didn't work. Try inverting the condition.
    InvCC = getSetCCInverse(CCCode, OpVT.isInteger());
    if (!TLI.isCondCodeLegalOrCustom(InvCC, OpVT)) {
      // If inverting the condition is not enough, try swapping operands
      // on top of it.
      InvCC = ISD::getSetCCSwappedOperands(InvCC);
      NeedSwap = true;
    }
    if (TLI.isCondCodeLegalOrCustom(InvCC, OpVT)) {
      CC = DAG.getCondCode(InvCC);
      NeedInvert = true;
      if (NeedSwap)
        std::swap(LHS, RHS);
      return true;
    }

    ISD::CondCode CC1 = ISD::SETCC_INVALID, CC2 = ISD::SETCC_INVALID;
    unsigned Opc = 0;
    switch (CCCode) {
    default: llvm_unreachable("Don't know how to expand this condition!");
    case ISD::SETO:
        assert(TLI.isCondCodeLegal(ISD::SETOEQ, OpVT)
            && "If SETO is expanded, SETOEQ must be legal!");
        CC1 = ISD::SETOEQ; CC2 = ISD::SETOEQ; Opc = ISD::AND; break;
    case ISD::SETUO:
        assert(TLI.isCondCodeLegal(ISD::SETUNE, OpVT)
            && "If SETUO is expanded, SETUNE must be legal!");
        CC1 = ISD::SETUNE; CC2 = ISD::SETUNE; Opc = ISD::OR;  break;
    case ISD::SETOEQ:
    case ISD::SETOGT:
    case ISD::SETOGE:
    case ISD::SETOLT:
    case ISD::SETOLE:
    case ISD::SETONE:
    case ISD::SETUEQ:
    case ISD::SETUNE:
    case ISD::SETUGT:
    case ISD::SETUGE:
    case ISD::SETULT:
    case ISD::SETULE:
        // If we are floating point, assign and break, otherwise fall through.
        if (!OpVT.isInteger()) {
          // We can use the 4th bit to tell if we are the unordered
          // or ordered version of the opcode.
          CC2 = ((unsigned)CCCode & 0x8U) ? ISD::SETUO : ISD::SETO;
          Opc = ((unsigned)CCCode & 0x8U) ? ISD::OR : ISD::AND;
          CC1 = (ISD::CondCode)(((int)CCCode & 0x7) | 0x10);
          break;
        }
        // Fallthrough if we are unsigned integer.
        LLVM_FALLTHROUGH;
    case ISD::SETLE:
    case ISD::SETGT:
    case ISD::SETGE:
    case ISD::SETLT:
    case ISD::SETNE:
    case ISD::SETEQ:
      // If all combinations of inverting the condition and swapping operands
      // didn't work then we have no means to expand the condition.
      llvm_unreachable("Don't know how to expand this condition!");
    }

    SDValue SetCC1, SetCC2;
    if (CCCode != ISD::SETO && CCCode != ISD::SETUO) {
      // If we aren't the ordered or unorder operation,
      // then the pattern is (LHS CC1 RHS) Opc (LHS CC2 RHS).
      SetCC1 = DAG.getSetCC(dl, VT, LHS, RHS, CC1);
      SetCC2 = DAG.getSetCC(dl, VT, LHS, RHS, CC2);
    } else {
      // Otherwise, the pattern is (LHS CC1 LHS) Opc (RHS CC2 RHS)
      SetCC1 = DAG.getSetCC(dl, VT, LHS, LHS, CC1);
      SetCC2 = DAG.getSetCC(dl, VT, RHS, RHS, CC2);
    }
    LHS = DAG.getNode(Opc, dl, VT, SetCC1, SetCC2);
    RHS = SDValue();
    CC  = SDValue();
    return true;
  }
  }
  return false;
}

/// Emit a store/load combination to the stack.  This stores
/// SrcOp to a stack slot of type SlotVT, truncating it if needed.  It then does
/// a load from the stack slot to DestVT, extending it if needed.
/// The resultant code need not be legal.
SDValue SelectionDAGLegalize::EmitStackConvert(SDValue SrcOp, EVT SlotVT,
                                               EVT DestVT, const SDLoc &dl) {
  // Create the stack frame object.
  unsigned SrcAlign = DAG.getDataLayout().getPrefTypeAlignment(
      SrcOp.getValueType().getTypeForEVT(*DAG.getContext()));
  SDValue FIPtr = DAG.CreateStackTemporary(SlotVT, SrcAlign);

  FrameIndexSDNode *StackPtrFI = cast<FrameIndexSDNode>(FIPtr);
  int SPFI = StackPtrFI->getIndex();
  MachinePointerInfo PtrInfo =
      MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), SPFI);

  unsigned SrcSize = SrcOp.getValueSizeInBits();
  unsigned SlotSize = SlotVT.getSizeInBits();
  unsigned DestSize = DestVT.getSizeInBits();
  Type *DestType = DestVT.getTypeForEVT(*DAG.getContext());
  unsigned DestAlign = DAG.getDataLayout().getPrefTypeAlignment(DestType);

  // Emit a store to the stack slot.  Use a truncstore if the input value is
  // later than DestVT.
  SDValue Store;

  if (SrcSize > SlotSize)
    Store = DAG.getTruncStore(DAG.getEntryNode(), dl, SrcOp, FIPtr, PtrInfo,
                              SlotVT, SrcAlign);
  else {
    assert(SrcSize == SlotSize && "Invalid store");
    Store =
        DAG.getStore(DAG.getEntryNode(), dl, SrcOp, FIPtr, PtrInfo, SrcAlign);
  }

  // Result is a load from the stack slot.
  if (SlotSize == DestSize)
    return DAG.getLoad(DestVT, dl, Store, FIPtr, PtrInfo, DestAlign);

  assert(SlotSize < DestSize && "Unknown extension!");
  return DAG.getExtLoad(ISD::EXTLOAD, dl, DestVT, Store, FIPtr, PtrInfo, SlotVT,
                        DestAlign);
}

SDValue SelectionDAGLegalize::ExpandSCALAR_TO_VECTOR(SDNode *Node) {
  SDLoc dl(Node);
  // Create a vector sized/aligned stack slot, store the value to element #0,
  // then load the whole vector back out.
  SDValue StackPtr = DAG.CreateStackTemporary(Node->getValueType(0));

  FrameIndexSDNode *StackPtrFI = cast<FrameIndexSDNode>(StackPtr);
  int SPFI = StackPtrFI->getIndex();

  SDValue Ch = DAG.getTruncStore(
      DAG.getEntryNode(), dl, Node->getOperand(0), StackPtr,
      MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), SPFI),
      Node->getValueType(0).getVectorElementType());
  return DAG.getLoad(
      Node->getValueType(0), dl, Ch, StackPtr,
      MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), SPFI));
}

static bool
ExpandBVWithShuffles(SDNode *Node, SelectionDAG &DAG,
                     const TargetLowering &TLI, SDValue &Res) {
  unsigned NumElems = Node->getNumOperands();
  SDLoc dl(Node);
  EVT VT = Node->getValueType(0);

  // Try to group the scalars into pairs, shuffle the pairs together, then
  // shuffle the pairs of pairs together, etc. until the vector has
  // been built. This will work only if all of the necessary shuffle masks
  // are legal.

  // We do this in two phases; first to check the legality of the shuffles,
  // and next, assuming that all shuffles are legal, to create the new nodes.
  for (int Phase = 0; Phase < 2; ++Phase) {
    SmallVector<std::pair<SDValue, SmallVector<int, 16>>, 16> IntermedVals,
                                                              NewIntermedVals;
    for (unsigned i = 0; i < NumElems; ++i) {
      SDValue V = Node->getOperand(i);
      if (V.isUndef())
        continue;

      SDValue Vec;
      if (Phase)
        Vec = DAG.getNode(ISD::SCALAR_TO_VECTOR, dl, VT, V);
      IntermedVals.push_back(std::make_pair(Vec, SmallVector<int, 16>(1, i)));
    }

    while (IntermedVals.size() > 2) {
      NewIntermedVals.clear();
      for (unsigned i = 0, e = (IntermedVals.size() & ~1u); i < e; i += 2) {
        // This vector and the next vector are shuffled together (simply to
        // append the one to the other).
        SmallVector<int, 16> ShuffleVec(NumElems, -1);

        SmallVector<int, 16> FinalIndices;
        FinalIndices.reserve(IntermedVals[i].second.size() +
                             IntermedVals[i+1].second.size());

        int k = 0;
        for (unsigned j = 0, f = IntermedVals[i].second.size(); j != f;
             ++j, ++k) {
          ShuffleVec[k] = j;
          FinalIndices.push_back(IntermedVals[i].second[j]);
        }
        for (unsigned j = 0, f = IntermedVals[i+1].second.size(); j != f;
             ++j, ++k) {
          ShuffleVec[k] = NumElems + j;
          FinalIndices.push_back(IntermedVals[i+1].second[j]);
        }

        SDValue Shuffle;
        if (Phase)
          Shuffle = DAG.getVectorShuffle(VT, dl, IntermedVals[i].first,
                                         IntermedVals[i+1].first,
                                         ShuffleVec);
        else if (!TLI.isShuffleMaskLegal(ShuffleVec, VT))
          return false;
        NewIntermedVals.push_back(
            std::make_pair(Shuffle, std::move(FinalIndices)));
      }

      // If we had an odd number of defined values, then append the last
      // element to the array of new vectors.
      if ((IntermedVals.size() & 1) != 0)
        NewIntermedVals.push_back(IntermedVals.back());

      IntermedVals.swap(NewIntermedVals);
    }

    assert(IntermedVals.size() <= 2 && IntermedVals.size() > 0 &&
           "Invalid number of intermediate vectors");
    SDValue Vec1 = IntermedVals[0].first;
    SDValue Vec2;
    if (IntermedVals.size() > 1)
      Vec2 = IntermedVals[1].first;
    else if (Phase)
      Vec2 = DAG.getUNDEF(VT);

    SmallVector<int, 16> ShuffleVec(NumElems, -1);
    for (unsigned i = 0, e = IntermedVals[0].second.size(); i != e; ++i)
      ShuffleVec[IntermedVals[0].second[i]] = i;
    for (unsigned i = 0, e = IntermedVals[1].second.size(); i != e; ++i)
      ShuffleVec[IntermedVals[1].second[i]] = NumElems + i;

    if (Phase)
      Res = DAG.getVectorShuffle(VT, dl, Vec1, Vec2, ShuffleVec);
    else if (!TLI.isShuffleMaskLegal(ShuffleVec, VT))
      return false;
  }

  return true;
}

/// Expand a BUILD_VECTOR node on targets that don't
/// support the operation, but do support the resultant vector type.
SDValue SelectionDAGLegalize::ExpandBUILD_VECTOR(SDNode *Node) {
  unsigned NumElems = Node->getNumOperands();
  SDValue Value1, Value2;
  SDLoc dl(Node);
  EVT VT = Node->getValueType(0);
  EVT OpVT = Node->getOperand(0).getValueType();
  EVT EltVT = VT.getVectorElementType();

  // If the only non-undef value is the low element, turn this into a
  // SCALAR_TO_VECTOR node.  If this is { X, X, X, X }, determine X.
  bool isOnlyLowElement = true;
  bool MoreThanTwoValues = false;
  bool isConstant = true;
  for (unsigned i = 0; i < NumElems; ++i) {
    SDValue V = Node->getOperand(i);
    if (V.isUndef())
      continue;
    if (i > 0)
      isOnlyLowElement = false;
    if (!isa<ConstantFPSDNode>(V) && !isa<ConstantSDNode>(V))
      isConstant = false;

    if (!Value1.getNode()) {
      Value1 = V;
    } else if (!Value2.getNode()) {
      if (V != Value1)
        Value2 = V;
    } else if (V != Value1 && V != Value2) {
      MoreThanTwoValues = true;
    }
  }

  if (!Value1.getNode())
    return DAG.getUNDEF(VT);

  if (isOnlyLowElement)
    return DAG.getNode(ISD::SCALAR_TO_VECTOR, dl, VT, Node->getOperand(0));

  // If all elements are constants, create a load from the constant pool.
  if (isConstant) {
    SmallVector<Constant*, 16> CV;
    for (unsigned i = 0, e = NumElems; i != e; ++i) {
      if (ConstantFPSDNode *V =
          dyn_cast<ConstantFPSDNode>(Node->getOperand(i))) {
        CV.push_back(const_cast<ConstantFP *>(V->getConstantFPValue()));
      } else if (ConstantSDNode *V =
                 dyn_cast<ConstantSDNode>(Node->getOperand(i))) {
        if (OpVT==EltVT)
          CV.push_back(const_cast<ConstantInt *>(V->getConstantIntValue()));
        else {
          // If OpVT and EltVT don't match, EltVT is not legal and the
          // element values have been promoted/truncated earlier.  Undo this;
          // we don't want a v16i8 to become a v16i32 for example.
          const ConstantInt *CI = V->getConstantIntValue();
          CV.push_back(ConstantInt::get(EltVT.getTypeForEVT(*DAG.getContext()),
                                        CI->getZExtValue()));
        }
      } else {
        assert(Node->getOperand(i).isUndef());
        Type *OpNTy = EltVT.getTypeForEVT(*DAG.getContext());
        CV.push_back(UndefValue::get(OpNTy));
      }
    }
    Constant *CP = ConstantVector::get(CV);
    SDValue CPIdx =
        DAG.getConstantPool(CP, TLI.getPointerTy(DAG.getDataLayout()));
    unsigned Alignment = cast<ConstantPoolSDNode>(CPIdx)->getAlignment();
    return DAG.getLoad(
        VT, dl, DAG.getEntryNode(), CPIdx,
        MachinePointerInfo::getConstantPool(DAG.getMachineFunction()),
        Alignment);
  }

  SmallSet<SDValue, 16> DefinedValues;
  for (unsigned i = 0; i < NumElems; ++i) {
    if (Node->getOperand(i).isUndef())
      continue;
    DefinedValues.insert(Node->getOperand(i));
  }

  if (TLI.shouldExpandBuildVectorWithShuffles(VT, DefinedValues.size())) {
    if (!MoreThanTwoValues) {
      SmallVector<int, 8> ShuffleVec(NumElems, -1);
      for (unsigned i = 0; i < NumElems; ++i) {
        SDValue V = Node->getOperand(i);
        if (V.isUndef())
          continue;
        ShuffleVec[i] = V == Value1 ? 0 : NumElems;
      }
      if (TLI.isShuffleMaskLegal(ShuffleVec, Node->getValueType(0))) {
        // Get the splatted value into the low element of a vector register.
        SDValue Vec1 = DAG.getNode(ISD::SCALAR_TO_VECTOR, dl, VT, Value1);
        SDValue Vec2;
        if (Value2.getNode())
          Vec2 = DAG.getNode(ISD::SCALAR_TO_VECTOR, dl, VT, Value2);
        else
          Vec2 = DAG.getUNDEF(VT);

        // Return shuffle(LowValVec, undef, <0,0,0,0>)
        return DAG.getVectorShuffle(VT, dl, Vec1, Vec2, ShuffleVec);
      }
    } else {
      SDValue Res;
      if (ExpandBVWithShuffles(Node, DAG, TLI, Res))
        return Res;
    }
  }

  // Otherwise, we can't handle this case efficiently.
  return ExpandVectorBuildThroughStack(Node);
}

// Expand a node into a call to a libcall.  If the result value
// does not fit into a register, return the lo part and set the hi part to the
// by-reg argument.  If it does fit into a single register, return the result
// and leave the Hi part unset.
SDValue SelectionDAGLegalize::ExpandLibCall(RTLIB::Libcall LC, SDNode *Node,
                                            bool isSigned) {
  TargetLowering::ArgListTy Args;
  TargetLowering::ArgListEntry Entry;
  for (const SDValue &Op : Node->op_values()) {
    EVT ArgVT = Op.getValueType();
    Type *ArgTy = ArgVT.getTypeForEVT(*DAG.getContext());
    Entry.Node = Op;
    Entry.Ty = ArgTy;
    Entry.IsSExt = TLI.shouldSignExtendTypeInLibCall(ArgVT, isSigned);
    Entry.IsZExt = !TLI.shouldSignExtendTypeInLibCall(ArgVT, isSigned);
    Args.push_back(Entry);
  }
  SDValue Callee = DAG.getExternalSymbol(TLI.getLibcallName(LC),
                                         TLI.getPointerTy(DAG.getDataLayout()));

  EVT RetVT = Node->getValueType(0);
  Type *RetTy = RetVT.getTypeForEVT(*DAG.getContext());

  // By default, the input chain to this libcall is the entry node of the
  // function. If the libcall is going to be emitted as a tail call then
  // TLI.isUsedByReturnOnly will change it to the right chain if the return
  // node which is being folded has a non-entry input chain.
  SDValue InChain = DAG.getEntryNode();

  // isTailCall may be true since the callee does not reference caller stack
  // frame. Check if it's in the right position and that the return types match.
  SDValue TCChain = InChain;
  const Function &F = DAG.getMachineFunction().getFunction();
  bool isTailCall =
      TLI.isInTailCallPosition(DAG, Node, TCChain) &&
      (RetTy == F.getReturnType() || F.getReturnType()->isVoidTy());
  if (isTailCall)
    InChain = TCChain;

  TargetLowering::CallLoweringInfo CLI(DAG);
  bool signExtend = TLI.shouldSignExtendTypeInLibCall(RetVT, isSigned);
  CLI.setDebugLoc(SDLoc(Node))
      .setChain(InChain)
      .setLibCallee(TLI.getLibcallCallingConv(LC), RetTy, Callee,
                    std::move(Args))
      .setTailCall(isTailCall)
      .setSExtResult(signExtend)
      .setZExtResult(!signExtend)
      .setIsPostTypeLegalization(true);

  std::pair<SDValue, SDValue> CallInfo = TLI.LowerCallTo(CLI);

  if (!CallInfo.second.getNode()) {
    LLVM_DEBUG(dbgs() << "Created tailcall: "; DAG.getRoot().dump());
    // It's a tailcall, return the chain (which is the DAG root).
    return DAG.getRoot();
  }

  LLVM_DEBUG(dbgs() << "Created libcall: "; CallInfo.first.dump());
  return CallInfo.first;
}

/// Generate a libcall taking the given operands as arguments
/// and returning a result of type RetVT.
SDValue SelectionDAGLegalize::ExpandLibCall(RTLIB::Libcall LC, EVT RetVT,
                                            const SDValue *Ops, unsigned NumOps,
                                            bool isSigned, const SDLoc &dl) {
  TargetLowering::ArgListTy Args;
  Args.reserve(NumOps);

  TargetLowering::ArgListEntry Entry;
  for (unsigned i = 0; i != NumOps; ++i) {
    Entry.Node = Ops[i];
    Entry.Ty = Entry.Node.getValueType().getTypeForEVT(*DAG.getContext());
    Entry.IsSExt = isSigned;
    Entry.IsZExt = !isSigned;
    Args.push_back(Entry);
  }
  SDValue Callee = DAG.getExternalSymbol(TLI.getLibcallName(LC),
                                         TLI.getPointerTy(DAG.getDataLayout()));

  Type *RetTy = RetVT.getTypeForEVT(*DAG.getContext());

  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(dl)
      .setChain(DAG.getEntryNode())
      .setLibCallee(TLI.getLibcallCallingConv(LC), RetTy, Callee,
                    std::move(Args))
      .setSExtResult(isSigned)
      .setZExtResult(!isSigned)
      .setIsPostTypeLegalization(true);

  std::pair<SDValue,SDValue> CallInfo = TLI.LowerCallTo(CLI);

  return CallInfo.first;
}

// Expand a node into a call to a libcall. Similar to
// ExpandLibCall except that the first operand is the in-chain.
std::pair<SDValue, SDValue>
SelectionDAGLegalize::ExpandChainLibCall(RTLIB::Libcall LC,
                                         SDNode *Node,
                                         bool isSigned) {
  SDValue InChain = Node->getOperand(0);

  TargetLowering::ArgListTy Args;
  TargetLowering::ArgListEntry Entry;
  for (unsigned i = 1, e = Node->getNumOperands(); i != e; ++i) {
    EVT ArgVT = Node->getOperand(i).getValueType();
    Type *ArgTy = ArgVT.getTypeForEVT(*DAG.getContext());
    Entry.Node = Node->getOperand(i);
    Entry.Ty = ArgTy;
    Entry.IsSExt = isSigned;
    Entry.IsZExt = !isSigned;
    Args.push_back(Entry);
  }
  SDValue Callee = DAG.getExternalSymbol(TLI.getLibcallName(LC),
                                         TLI.getPointerTy(DAG.getDataLayout()));

  Type *RetTy = Node->getValueType(0).getTypeForEVT(*DAG.getContext());

  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(SDLoc(Node))
      .setChain(InChain)
      .setLibCallee(TLI.getLibcallCallingConv(LC), RetTy, Callee,
                    std::move(Args))
      .setSExtResult(isSigned)
      .setZExtResult(!isSigned);

  std::pair<SDValue, SDValue> CallInfo = TLI.LowerCallTo(CLI);

  return CallInfo;
}

SDValue SelectionDAGLegalize::ExpandFPLibCall(SDNode* Node,
                                              RTLIB::Libcall Call_F32,
                                              RTLIB::Libcall Call_F64,
                                              RTLIB::Libcall Call_F80,
                                              RTLIB::Libcall Call_F128,
                                              RTLIB::Libcall Call_PPCF128) {
  if (Node->isStrictFPOpcode())
    Node = DAG.mutateStrictFPToFP(Node);

  RTLIB::Libcall LC;
  switch (Node->getSimpleValueType(0).SimpleTy) {
  default: llvm_unreachable("Unexpected request for libcall!");
  case MVT::f32: LC = Call_F32; break;
  case MVT::f64: LC = Call_F64; break;
  case MVT::f80: LC = Call_F80; break;
  case MVT::f128: LC = Call_F128; break;
  case MVT::ppcf128: LC = Call_PPCF128; break;
  }
  return ExpandLibCall(LC, Node, false);
}

SDValue SelectionDAGLegalize::ExpandIntLibCall(SDNode* Node, bool isSigned,
                                               RTLIB::Libcall Call_I8,
                                               RTLIB::Libcall Call_I16,
                                               RTLIB::Libcall Call_I32,
                                               RTLIB::Libcall Call_I64,
                                               RTLIB::Libcall Call_I128) {
  RTLIB::Libcall LC;
  switch (Node->getSimpleValueType(0).SimpleTy) {
  default: llvm_unreachable("Unexpected request for libcall!");
  case MVT::i8:   LC = Call_I8; break;
  case MVT::i16:  LC = Call_I16; break;
  case MVT::i32:  LC = Call_I32; break;
  case MVT::i64:  LC = Call_I64; break;
  case MVT::i128: LC = Call_I128; break;
  }
  return ExpandLibCall(LC, Node, isSigned);
}

/// Issue libcalls to __{u}divmod to compute div / rem pairs.
void
SelectionDAGLegalize::ExpandDivRemLibCall(SDNode *Node,
                                          SmallVectorImpl<SDValue> &Results) {
  unsigned Opcode = Node->getOpcode();
  bool isSigned = Opcode == ISD::SDIVREM;

  RTLIB::Libcall LC;
  switch (Node->getSimpleValueType(0).SimpleTy) {
  default: llvm_unreachable("Unexpected request for libcall!");
  case MVT::i8:   LC= isSigned ? RTLIB::SDIVREM_I8  : RTLIB::UDIVREM_I8;  break;
  case MVT::i16:  LC= isSigned ? RTLIB::SDIVREM_I16 : RTLIB::UDIVREM_I16; break;
  case MVT::i32:  LC= isSigned ? RTLIB::SDIVREM_I32 : RTLIB::UDIVREM_I32; break;
  case MVT::i64:  LC= isSigned ? RTLIB::SDIVREM_I64 : RTLIB::UDIVREM_I64; break;
  case MVT::i128: LC= isSigned ? RTLIB::SDIVREM_I128:RTLIB::UDIVREM_I128; break;
  }

  // The input chain to this libcall is the entry node of the function.
  // Legalizing the call will automatically add the previous call to the
  // dependence.
  SDValue InChain = DAG.getEntryNode();

  EVT RetVT = Node->getValueType(0);
  Type *RetTy = RetVT.getTypeForEVT(*DAG.getContext());

  TargetLowering::ArgListTy Args;
  TargetLowering::ArgListEntry Entry;
  for (const SDValue &Op : Node->op_values()) {
    EVT ArgVT = Op.getValueType();
    Type *ArgTy = ArgVT.getTypeForEVT(*DAG.getContext());
    Entry.Node = Op;
    Entry.Ty = ArgTy;
    Entry.IsSExt = isSigned;
    Entry.IsZExt = !isSigned;
    Args.push_back(Entry);
  }

  // Also pass the return address of the remainder.
  SDValue FIPtr = DAG.CreateStackTemporary(RetVT);
  Entry.Node = FIPtr;
  Entry.Ty = RetTy->getPointerTo();
  Entry.IsSExt = isSigned;
  Entry.IsZExt = !isSigned;
  Args.push_back(Entry);

  SDValue Callee = DAG.getExternalSymbol(TLI.getLibcallName(LC),
                                         TLI.getPointerTy(DAG.getDataLayout()));

  SDLoc dl(Node);
  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(dl)
      .setChain(InChain)
      .setLibCallee(TLI.getLibcallCallingConv(LC), RetTy, Callee,
                    std::move(Args))
      .setSExtResult(isSigned)
      .setZExtResult(!isSigned);

  std::pair<SDValue, SDValue> CallInfo = TLI.LowerCallTo(CLI);

  // Remainder is loaded back from the stack frame.
  SDValue Rem =
      DAG.getLoad(RetVT, dl, CallInfo.second, FIPtr, MachinePointerInfo());
  Results.push_back(CallInfo.first);
  Results.push_back(Rem);
}

/// Return true if sincos libcall is available.
static bool isSinCosLibcallAvailable(SDNode *Node, const TargetLowering &TLI) {
  RTLIB::Libcall LC;
  switch (Node->getSimpleValueType(0).SimpleTy) {
  default: llvm_unreachable("Unexpected request for libcall!");
  case MVT::f32:     LC = RTLIB::SINCOS_F32; break;
  case MVT::f64:     LC = RTLIB::SINCOS_F64; break;
  case MVT::f80:     LC = RTLIB::SINCOS_F80; break;
  case MVT::f128:    LC = RTLIB::SINCOS_F128; break;
  case MVT::ppcf128: LC = RTLIB::SINCOS_PPCF128; break;
  }
  return TLI.getLibcallName(LC) != nullptr;
}

/// Only issue sincos libcall if both sin and cos are needed.
static bool useSinCos(SDNode *Node) {
  unsigned OtherOpcode = Node->getOpcode() == ISD::FSIN
    ? ISD::FCOS : ISD::FSIN;

  SDValue Op0 = Node->getOperand(0);
  for (SDNode::use_iterator UI = Op0.getNode()->use_begin(),
       UE = Op0.getNode()->use_end(); UI != UE; ++UI) {
    SDNode *User = *UI;
    if (User == Node)
      continue;
    // The other user might have been turned into sincos already.
    if (User->getOpcode() == OtherOpcode || User->getOpcode() == ISD::FSINCOS)
      return true;
  }
  return false;
}

/// Issue libcalls to sincos to compute sin / cos pairs.
void
SelectionDAGLegalize::ExpandSinCosLibCall(SDNode *Node,
                                          SmallVectorImpl<SDValue> &Results) {
  RTLIB::Libcall LC;
  switch (Node->getSimpleValueType(0).SimpleTy) {
  default: llvm_unreachable("Unexpected request for libcall!");
  case MVT::f32:     LC = RTLIB::SINCOS_F32; break;
  case MVT::f64:     LC = RTLIB::SINCOS_F64; break;
  case MVT::f80:     LC = RTLIB::SINCOS_F80; break;
  case MVT::f128:    LC = RTLIB::SINCOS_F128; break;
  case MVT::ppcf128: LC = RTLIB::SINCOS_PPCF128; break;
  }

  // The input chain to this libcall is the entry node of the function.
  // Legalizing the call will automatically add the previous call to the
  // dependence.
  SDValue InChain = DAG.getEntryNode();

  EVT RetVT = Node->getValueType(0);
  Type *RetTy = RetVT.getTypeForEVT(*DAG.getContext());

  TargetLowering::ArgListTy Args;
  TargetLowering::ArgListEntry Entry;

  // Pass the argument.
  Entry.Node = Node->getOperand(0);
  Entry.Ty = RetTy;
  Entry.IsSExt = false;
  Entry.IsZExt = false;
  Args.push_back(Entry);

  // Pass the return address of sin.
  SDValue SinPtr = DAG.CreateStackTemporary(RetVT);
  Entry.Node = SinPtr;
  Entry.Ty = RetTy->getPointerTo();
  Entry.IsSExt = false;
  Entry.IsZExt = false;
  Args.push_back(Entry);

  // Also pass the return address of the cos.
  SDValue CosPtr = DAG.CreateStackTemporary(RetVT);
  Entry.Node = CosPtr;
  Entry.Ty = RetTy->getPointerTo();
  Entry.IsSExt = false;
  Entry.IsZExt = false;
  Args.push_back(Entry);

  SDValue Callee = DAG.getExternalSymbol(TLI.getLibcallName(LC),
                                         TLI.getPointerTy(DAG.getDataLayout()));

  SDLoc dl(Node);
  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(dl).setChain(InChain).setLibCallee(
      TLI.getLibcallCallingConv(LC), Type::getVoidTy(*DAG.getContext()), Callee,
      std::move(Args));

  std::pair<SDValue, SDValue> CallInfo = TLI.LowerCallTo(CLI);

  Results.push_back(
      DAG.getLoad(RetVT, dl, CallInfo.second, SinPtr, MachinePointerInfo()));
  Results.push_back(
      DAG.getLoad(RetVT, dl, CallInfo.second, CosPtr, MachinePointerInfo()));
}

/// This function is responsible for legalizing a
/// INT_TO_FP operation of the specified operand when the target requests that
/// we expand it.  At this point, we know that the result and operand types are
/// legal for the target.
SDValue SelectionDAGLegalize::ExpandLegalINT_TO_FP(bool isSigned, SDValue Op0,
                                                   EVT DestVT,
                                                   const SDLoc &dl) {
  EVT SrcVT = Op0.getValueType();

  // TODO: Should any fast-math-flags be set for the created nodes?
  LLVM_DEBUG(dbgs() << "Legalizing INT_TO_FP\n");
  if (SrcVT == MVT::i32 && TLI.isTypeLegal(MVT::f64)) {
    LLVM_DEBUG(dbgs() << "32-bit [signed|unsigned] integer to float/double "
                         "expansion\n");

    // Get the stack frame index of a 8 byte buffer.
    SDValue StackSlot = DAG.CreateStackTemporary(MVT::f64);

    // word offset constant for Hi/Lo address computation
    SDValue WordOff = DAG.getConstant(sizeof(int), dl,
                                      StackSlot.getValueType());
    // set up Hi and Lo (into buffer) address based on endian
    SDValue Hi = StackSlot;
    SDValue Lo = DAG.getNode(ISD::ADD, dl, StackSlot.getValueType(),
                             StackSlot, WordOff);
    if (DAG.getDataLayout().isLittleEndian())
      std::swap(Hi, Lo);

    // if signed map to unsigned space
    SDValue Op0Mapped;
    if (isSigned) {
      // constant used to invert sign bit (signed to unsigned mapping)
      SDValue SignBit = DAG.getConstant(0x80000000u, dl, MVT::i32);
      Op0Mapped = DAG.getNode(ISD::XOR, dl, MVT::i32, Op0, SignBit);
    } else {
      Op0Mapped = Op0;
    }
    // store the lo of the constructed double - based on integer input
    SDValue Store1 = DAG.getStore(DAG.getEntryNode(), dl, Op0Mapped, Lo,
                                  MachinePointerInfo());
    // initial hi portion of constructed double
    SDValue InitialHi = DAG.getConstant(0x43300000u, dl, MVT::i32);
    // store the hi of the constructed double - biased exponent
    SDValue Store2 =
        DAG.getStore(Store1, dl, InitialHi, Hi, MachinePointerInfo());
    // load the constructed double
    SDValue Load =
        DAG.getLoad(MVT::f64, dl, Store2, StackSlot, MachinePointerInfo());
    // FP constant to bias correct the final result
    SDValue Bias = DAG.getConstantFP(isSigned ?
                                     BitsToDouble(0x4330000080000000ULL) :
                                     BitsToDouble(0x4330000000000000ULL),
                                     dl, MVT::f64);
    // subtract the bias
    SDValue Sub = DAG.getNode(ISD::FSUB, dl, MVT::f64, Load, Bias);
    // final result
    SDValue Result = DAG.getFPExtendOrRound(Sub, dl, DestVT);
    return Result;
  }
  assert(!isSigned && "Legalize cannot Expand SINT_TO_FP for i64 yet");
  // Code below here assumes !isSigned without checking again.

  SDValue Tmp1 = DAG.getNode(ISD::SINT_TO_FP, dl, DestVT, Op0);

  SDValue SignSet = DAG.getSetCC(dl, getSetCCResultType(SrcVT), Op0,
                                 DAG.getConstant(0, dl, SrcVT), ISD::SETLT);
  SDValue Zero = DAG.getIntPtrConstant(0, dl),
          Four = DAG.getIntPtrConstant(4, dl);
  SDValue CstOffset = DAG.getSelect(dl, Zero.getValueType(),
                                    SignSet, Four, Zero);

  // If the sign bit of the integer is set, the large number will be treated
  // as a negative number.  To counteract this, the dynamic code adds an
  // offset depending on the data type.
  uint64_t FF;
  switch (SrcVT.getSimpleVT().SimpleTy) {
  default: llvm_unreachable("Unsupported integer type!");
  case MVT::i8 : FF = 0x43800000ULL; break;  // 2^8  (as a float)
  case MVT::i16: FF = 0x47800000ULL; break;  // 2^16 (as a float)
  case MVT::i32: FF = 0x4F800000ULL; break;  // 2^32 (as a float)
  case MVT::i64: FF = 0x5F800000ULL; break;  // 2^64 (as a float)
  }
  if (DAG.getDataLayout().isLittleEndian())
    FF <<= 32;
  Constant *FudgeFactor = ConstantInt::get(
                                       Type::getInt64Ty(*DAG.getContext()), FF);

  SDValue CPIdx =
      DAG.getConstantPool(FudgeFactor, TLI.getPointerTy(DAG.getDataLayout()));
  unsigned Alignment = cast<ConstantPoolSDNode>(CPIdx)->getAlignment();
  CPIdx = DAG.getNode(ISD::ADD, dl, CPIdx.getValueType(), CPIdx, CstOffset);
  Alignment = std::min(Alignment, 4u);
  SDValue FudgeInReg;
  if (DestVT == MVT::f32)
    FudgeInReg = DAG.getLoad(
        MVT::f32, dl, DAG.getEntryNode(), CPIdx,
        MachinePointerInfo::getConstantPool(DAG.getMachineFunction()),
        Alignment);
  else {
    SDValue Load = DAG.getExtLoad(
        ISD::EXTLOAD, dl, DestVT, DAG.getEntryNode(), CPIdx,
        MachinePointerInfo::getConstantPool(DAG.getMachineFunction()), MVT::f32,
        Alignment);
    HandleSDNode Handle(Load);
    LegalizeOp(Load.getNode());
    FudgeInReg = Handle.getValue();
  }

  return DAG.getNode(ISD::FADD, dl, DestVT, Tmp1, FudgeInReg);
}

/// This function is responsible for legalizing a
/// *INT_TO_FP operation of the specified operand when the target requests that
/// we promote it.  At this point, we know that the result and operand types are
/// legal for the target, and that there is a legal UINT_TO_FP or SINT_TO_FP
/// operation that takes a larger input.
SDValue SelectionDAGLegalize::PromoteLegalINT_TO_FP(SDValue LegalOp, EVT DestVT,
                                                    bool isSigned,
                                                    const SDLoc &dl) {
  // First step, figure out the appropriate *INT_TO_FP operation to use.
  EVT NewInTy = LegalOp.getValueType();

  unsigned OpToUse = 0;

  // Scan for the appropriate larger type to use.
  while (true) {
    NewInTy = (MVT::SimpleValueType)(NewInTy.getSimpleVT().SimpleTy+1);
    assert(NewInTy.isInteger() && "Ran out of possibilities!");

    // If the target supports SINT_TO_FP of this type, use it.
    if (TLI.isOperationLegalOrCustom(ISD::SINT_TO_FP, NewInTy)) {
      OpToUse = ISD::SINT_TO_FP;
      break;
    }
    if (isSigned) continue;

    // If the target supports UINT_TO_FP of this type, use it.
    if (TLI.isOperationLegalOrCustom(ISD::UINT_TO_FP, NewInTy)) {
      OpToUse = ISD::UINT_TO_FP;
      break;
    }

    // Otherwise, try a larger type.
  }

  // Okay, we found the operation and type to use.  Zero extend our input to the
  // desired type then run the operation on it.
  return DAG.getNode(OpToUse, dl, DestVT,
                     DAG.getNode(isSigned ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND,
                                 dl, NewInTy, LegalOp));
}

/// This function is responsible for legalizing a
/// FP_TO_*INT operation of the specified operand when the target requests that
/// we promote it.  At this point, we know that the result and operand types are
/// legal for the target, and that there is a legal FP_TO_UINT or FP_TO_SINT
/// operation that returns a larger result.
SDValue SelectionDAGLegalize::PromoteLegalFP_TO_INT(SDValue LegalOp, EVT DestVT,
                                                    bool isSigned,
                                                    const SDLoc &dl) {
  // First step, figure out the appropriate FP_TO*INT operation to use.
  EVT NewOutTy = DestVT;

  unsigned OpToUse = 0;

  // Scan for the appropriate larger type to use.
  while (true) {
    NewOutTy = (MVT::SimpleValueType)(NewOutTy.getSimpleVT().SimpleTy+1);
    assert(NewOutTy.isInteger() && "Ran out of possibilities!");

    // A larger signed type can hold all unsigned values of the requested type,
    // so using FP_TO_SINT is valid
    if (TLI.isOperationLegalOrCustom(ISD::FP_TO_SINT, NewOutTy)) {
      OpToUse = ISD::FP_TO_SINT;
      break;
    }

    // However, if the value may be < 0.0, we *must* use some FP_TO_SINT.
    if (!isSigned && TLI.isOperationLegalOrCustom(ISD::FP_TO_UINT, NewOutTy)) {
      OpToUse = ISD::FP_TO_UINT;
      break;
    }

    // Otherwise, try a larger type.
  }

  // Okay, we found the operation and type to use.
  SDValue Operation = DAG.getNode(OpToUse, dl, NewOutTy, LegalOp);

  // Truncate the result of the extended FP_TO_*INT operation to the desired
  // size.
  return DAG.getNode(ISD::TRUNCATE, dl, DestVT, Operation);
}

/// Legalize a BITREVERSE scalar/vector operation as a series of mask + shifts.
SDValue SelectionDAGLegalize::ExpandBITREVERSE(SDValue Op, const SDLoc &dl) {
  EVT VT = Op.getValueType();
  EVT SHVT = TLI.getShiftAmountTy(VT, DAG.getDataLayout());
  unsigned Sz = VT.getScalarSizeInBits();

  SDValue Tmp, Tmp2, Tmp3;

  // If we can, perform BSWAP first and then the mask+swap the i4, then i2
  // and finally the i1 pairs.
  // TODO: We can easily support i4/i2 legal types if any target ever does.
  if (Sz >= 8 && isPowerOf2_32(Sz)) {
    // Create the masks - repeating the pattern every byte.
    APInt MaskHi4(Sz, 0), MaskHi2(Sz, 0), MaskHi1(Sz, 0);
    APInt MaskLo4(Sz, 0), MaskLo2(Sz, 0), MaskLo1(Sz, 0);
    for (unsigned J = 0; J != Sz; J += 8) {
      MaskHi4 = MaskHi4 | (0xF0ull << J);
      MaskLo4 = MaskLo4 | (0x0Full << J);
      MaskHi2 = MaskHi2 | (0xCCull << J);
      MaskLo2 = MaskLo2 | (0x33ull << J);
      MaskHi1 = MaskHi1 | (0xAAull << J);
      MaskLo1 = MaskLo1 | (0x55ull << J);
    }

    // BSWAP if the type is wider than a single byte.
    Tmp = (Sz > 8 ? DAG.getNode(ISD::BSWAP, dl, VT, Op) : Op);

    // swap i4: ((V & 0xF0) >> 4) | ((V & 0x0F) << 4)
    Tmp2 = DAG.getNode(ISD::AND, dl, VT, Tmp, DAG.getConstant(MaskHi4, dl, VT));
    Tmp3 = DAG.getNode(ISD::AND, dl, VT, Tmp, DAG.getConstant(MaskLo4, dl, VT));
    Tmp2 = DAG.getNode(ISD::SRL, dl, VT, Tmp2, DAG.getConstant(4, dl, SHVT));
    Tmp3 = DAG.getNode(ISD::SHL, dl, VT, Tmp3, DAG.getConstant(4, dl, SHVT));
    Tmp = DAG.getNode(ISD::OR, dl, VT, Tmp2, Tmp3);

    // swap i2: ((V & 0xCC) >> 2) | ((V & 0x33) << 2)
    Tmp2 = DAG.getNode(ISD::AND, dl, VT, Tmp, DAG.getConstant(MaskHi2, dl, VT));
    Tmp3 = DAG.getNode(ISD::AND, dl, VT, Tmp, DAG.getConstant(MaskLo2, dl, VT));
    Tmp2 = DAG.getNode(ISD::SRL, dl, VT, Tmp2, DAG.getConstant(2, dl, SHVT));
    Tmp3 = DAG.getNode(ISD::SHL, dl, VT, Tmp3, DAG.getConstant(2, dl, SHVT));
    Tmp = DAG.getNode(ISD::OR, dl, VT, Tmp2, Tmp3);

    // swap i1: ((V & 0xAA) >> 1) | ((V & 0x55) << 1)
    Tmp2 = DAG.getNode(ISD::AND, dl, VT, Tmp, DAG.getConstant(MaskHi1, dl, VT));
    Tmp3 = DAG.getNode(ISD::AND, dl, VT, Tmp, DAG.getConstant(MaskLo1, dl, VT));
    Tmp2 = DAG.getNode(ISD::SRL, dl, VT, Tmp2, DAG.getConstant(1, dl, SHVT));
    Tmp3 = DAG.getNode(ISD::SHL, dl, VT, Tmp3, DAG.getConstant(1, dl, SHVT));
    Tmp = DAG.getNode(ISD::OR, dl, VT, Tmp2, Tmp3);
    return Tmp;
  }

  Tmp = DAG.getConstant(0, dl, VT);
  for (unsigned I = 0, J = Sz-1; I < Sz; ++I, --J) {
    if (I < J)
      Tmp2 =
          DAG.getNode(ISD::SHL, dl, VT, Op, DAG.getConstant(J - I, dl, SHVT));
    else
      Tmp2 =
          DAG.getNode(ISD::SRL, dl, VT, Op, DAG.getConstant(I - J, dl, SHVT));

    APInt Shift(Sz, 1);
    Shift <<= J;
    Tmp2 = DAG.getNode(ISD::AND, dl, VT, Tmp2, DAG.getConstant(Shift, dl, VT));
    Tmp = DAG.getNode(ISD::OR, dl, VT, Tmp, Tmp2);
  }

  return Tmp;
}

/// Open code the operations for BSWAP of the specified operation.
SDValue SelectionDAGLegalize::ExpandBSWAP(SDValue Op, const SDLoc &dl) {
  EVT VT = Op.getValueType();
  EVT SHVT = TLI.getShiftAmountTy(VT, DAG.getDataLayout());
  SDValue Tmp1, Tmp2, Tmp3, Tmp4, Tmp5, Tmp6, Tmp7, Tmp8;
  switch (VT.getSimpleVT().getScalarType().SimpleTy) {
  default: llvm_unreachable("Unhandled Expand type in BSWAP!");
  case MVT::i16:
    Tmp2 = DAG.getNode(ISD::SHL, dl, VT, Op, DAG.getConstant(8, dl, SHVT));
    Tmp1 = DAG.getNode(ISD::SRL, dl, VT, Op, DAG.getConstant(8, dl, SHVT));
    return DAG.getNode(ISD::OR, dl, VT, Tmp1, Tmp2);
  case MVT::i32:
    Tmp4 = DAG.getNode(ISD::SHL, dl, VT, Op, DAG.getConstant(24, dl, SHVT));
    Tmp3 = DAG.getNode(ISD::SHL, dl, VT, Op, DAG.getConstant(8, dl, SHVT));
    Tmp2 = DAG.getNode(ISD::SRL, dl, VT, Op, DAG.getConstant(8, dl, SHVT));
    Tmp1 = DAG.getNode(ISD::SRL, dl, VT, Op, DAG.getConstant(24, dl, SHVT));
    Tmp3 = DAG.getNode(ISD::AND, dl, VT, Tmp3,
                       DAG.getConstant(0xFF0000, dl, VT));
    Tmp2 = DAG.getNode(ISD::AND, dl, VT, Tmp2, DAG.getConstant(0xFF00, dl, VT));
    Tmp4 = DAG.getNode(ISD::OR, dl, VT, Tmp4, Tmp3);
    Tmp2 = DAG.getNode(ISD::OR, dl, VT, Tmp2, Tmp1);
    return DAG.getNode(ISD::OR, dl, VT, Tmp4, Tmp2);
  case MVT::i64:
    Tmp8 = DAG.getNode(ISD::SHL, dl, VT, Op, DAG.getConstant(56, dl, SHVT));
    Tmp7 = DAG.getNode(ISD::SHL, dl, VT, Op, DAG.getConstant(40, dl, SHVT));
    Tmp6 = DAG.getNode(ISD::SHL, dl, VT, Op, DAG.getConstant(24, dl, SHVT));
    Tmp5 = DAG.getNode(ISD::SHL, dl, VT, Op, DAG.getConstant(8, dl, SHVT));
    Tmp4 = DAG.getNode(ISD::SRL, dl, VT, Op, DAG.getConstant(8, dl, SHVT));
    Tmp3 = DAG.getNode(ISD::SRL, dl, VT, Op, DAG.getConstant(24, dl, SHVT));
    Tmp2 = DAG.getNode(ISD::SRL, dl, VT, Op, DAG.getConstant(40, dl, SHVT));
    Tmp1 = DAG.getNode(ISD::SRL, dl, VT, Op, DAG.getConstant(56, dl, SHVT));
    Tmp7 = DAG.getNode(ISD::AND, dl, VT, Tmp7,
                       DAG.getConstant(255ULL<<48, dl, VT));
    Tmp6 = DAG.getNode(ISD::AND, dl, VT, Tmp6,
                       DAG.getConstant(255ULL<<40, dl, VT));
    Tmp5 = DAG.getNode(ISD::AND, dl, VT, Tmp5,
                       DAG.getConstant(255ULL<<32, dl, VT));
    Tmp4 = DAG.getNode(ISD::AND, dl, VT, Tmp4,
                       DAG.getConstant(255ULL<<24, dl, VT));
    Tmp3 = DAG.getNode(ISD::AND, dl, VT, Tmp3,
                       DAG.getConstant(255ULL<<16, dl, VT));
    Tmp2 = DAG.getNode(ISD::AND, dl, VT, Tmp2,
                       DAG.getConstant(255ULL<<8 , dl, VT));
    Tmp8 = DAG.getNode(ISD::OR, dl, VT, Tmp8, Tmp7);
    Tmp6 = DAG.getNode(ISD::OR, dl, VT, Tmp6, Tmp5);
    Tmp4 = DAG.getNode(ISD::OR, dl, VT, Tmp4, Tmp3);
    Tmp2 = DAG.getNode(ISD::OR, dl, VT, Tmp2, Tmp1);
    Tmp8 = DAG.getNode(ISD::OR, dl, VT, Tmp8, Tmp6);
    Tmp4 = DAG.getNode(ISD::OR, dl, VT, Tmp4, Tmp2);
    return DAG.getNode(ISD::OR, dl, VT, Tmp8, Tmp4);
  }
}

bool SelectionDAGLegalize::ExpandNode(SDNode *Node) {
  LLVM_DEBUG(dbgs() << "Trying to expand node\n");
  SmallVector<SDValue, 8> Results;
  SDLoc dl(Node);
  SDValue Tmp1, Tmp2, Tmp3, Tmp4;
  bool NeedInvert;
  switch (Node->getOpcode()) {
  case ISD::ABS:
    if (TLI.expandABS(Node, Tmp1, DAG))
      Results.push_back(Tmp1);
    break;
  case ISD::CTPOP:
    if (TLI.expandCTPOP(Node, Tmp1, DAG))
      Results.push_back(Tmp1);
    break;
  case ISD::CTLZ:
  case ISD::CTLZ_ZERO_UNDEF:
    if (TLI.expandCTLZ(Node, Tmp1, DAG))
      Results.push_back(Tmp1);
    break;
  case ISD::CTTZ:
  case ISD::CTTZ_ZERO_UNDEF:
    if (TLI.expandCTTZ(Node, Tmp1, DAG))
      Results.push_back(Tmp1);
    break;
  case ISD::BITREVERSE:
    Results.push_back(ExpandBITREVERSE(Node->getOperand(0), dl));
    break;
  case ISD::BSWAP:
    Results.push_back(ExpandBSWAP(Node->getOperand(0), dl));
    break;
  case ISD::FRAMEADDR:
  case ISD::RETURNADDR:
  case ISD::FRAME_TO_ARGS_OFFSET:
    Results.push_back(DAG.getConstant(0, dl, Node->getValueType(0)));
    break;
  case ISD::EH_DWARF_CFA: {
    SDValue CfaArg = DAG.getSExtOrTrunc(Node->getOperand(0), dl,
                                        TLI.getPointerTy(DAG.getDataLayout()));
    SDValue Offset = DAG.getNode(ISD::ADD, dl,
                                 CfaArg.getValueType(),
                                 DAG.getNode(ISD::FRAME_TO_ARGS_OFFSET, dl,
                                             CfaArg.getValueType()),
                                 CfaArg);
    SDValue FA = DAG.getNode(
        ISD::FRAMEADDR, dl, TLI.getPointerTy(DAG.getDataLayout()),
        DAG.getConstant(0, dl, TLI.getPointerTy(DAG.getDataLayout())));
    Results.push_back(DAG.getNode(ISD::ADD, dl, FA.getValueType(),
                                  FA, Offset));
    break;
  }
  case ISD::FLT_ROUNDS_:
    Results.push_back(DAG.getConstant(1, dl, Node->getValueType(0)));
    break;
  case ISD::EH_RETURN:
  case ISD::EH_LABEL:
  case ISD::PREFETCH:
  case ISD::VAEND:
  case ISD::EH_SJLJ_LONGJMP:
    // If the target didn't expand these, there's nothing to do, so just
    // preserve the chain and be done.
    Results.push_back(Node->getOperand(0));
    break;
  case ISD::READCYCLECOUNTER:
    // If the target didn't expand this, just return 'zero' and preserve the
    // chain.
    Results.append(Node->getNumValues() - 1,
                   DAG.getConstant(0, dl, Node->getValueType(0)));
    Results.push_back(Node->getOperand(0));
    break;
  case ISD::EH_SJLJ_SETJMP:
    // If the target didn't expand this, just return 'zero' and preserve the
    // chain.
    Results.push_back(DAG.getConstant(0, dl, MVT::i32));
    Results.push_back(Node->getOperand(0));
    break;
  case ISD::ATOMIC_LOAD: {
    // There is no libcall for atomic load; fake it with ATOMIC_CMP_SWAP.
    SDValue Zero = DAG.getConstant(0, dl, Node->getValueType(0));
    SDVTList VTs = DAG.getVTList(Node->getValueType(0), MVT::Other);
    SDValue Swap = DAG.getAtomicCmpSwap(
        ISD::ATOMIC_CMP_SWAP, dl, cast<AtomicSDNode>(Node)->getMemoryVT(), VTs,
        Node->getOperand(0), Node->getOperand(1), Zero, Zero,
        cast<AtomicSDNode>(Node)->getMemOperand());
    Results.push_back(Swap.getValue(0));
    Results.push_back(Swap.getValue(1));
    break;
  }
  case ISD::ATOMIC_STORE: {
    // There is no libcall for atomic store; fake it with ATOMIC_SWAP.
    SDValue Swap = DAG.getAtomic(ISD::ATOMIC_SWAP, dl,
                                 cast<AtomicSDNode>(Node)->getMemoryVT(),
                                 Node->getOperand(0),
                                 Node->getOperand(1), Node->getOperand(2),
                                 cast<AtomicSDNode>(Node)->getMemOperand());
    Results.push_back(Swap.getValue(1));
    break;
  }
  case ISD::ATOMIC_CMP_SWAP_WITH_SUCCESS: {
    // Expanding an ATOMIC_CMP_SWAP_WITH_SUCCESS produces an ATOMIC_CMP_SWAP and
    // splits out the success value as a comparison. Expanding the resulting
    // ATOMIC_CMP_SWAP will produce a libcall.
    SDVTList VTs = DAG.getVTList(Node->getValueType(0), MVT::Other);
    SDValue Res = DAG.getAtomicCmpSwap(
        ISD::ATOMIC_CMP_SWAP, dl, cast<AtomicSDNode>(Node)->getMemoryVT(), VTs,
        Node->getOperand(0), Node->getOperand(1), Node->getOperand(2),
        Node->getOperand(3), cast<MemSDNode>(Node)->getMemOperand());

    SDValue ExtRes = Res;
    SDValue LHS = Res;
    SDValue RHS = Node->getOperand(1);

    EVT AtomicType = cast<AtomicSDNode>(Node)->getMemoryVT();
    EVT OuterType = Node->getValueType(0);
    switch (TLI.getExtendForAtomicOps()) {
    case ISD::SIGN_EXTEND:
      LHS = DAG.getNode(ISD::AssertSext, dl, OuterType, Res,
                        DAG.getValueType(AtomicType));
      RHS = DAG.getNode(ISD::SIGN_EXTEND_INREG, dl, OuterType,
                        Node->getOperand(2), DAG.getValueType(AtomicType));
      ExtRes = LHS;
      break;
    case ISD::ZERO_EXTEND:
      LHS = DAG.getNode(ISD::AssertZext, dl, OuterType, Res,
                        DAG.getValueType(AtomicType));
      RHS = DAG.getZeroExtendInReg(Node->getOperand(2), dl, AtomicType);
      ExtRes = LHS;
      break;
    case ISD::ANY_EXTEND:
      LHS = DAG.getZeroExtendInReg(Res, dl, AtomicType);
      RHS = DAG.getZeroExtendInReg(Node->getOperand(2), dl, AtomicType);
      break;
    default:
      llvm_unreachable("Invalid atomic op extension");
    }

    SDValue Success =
        DAG.getSetCC(dl, Node->getValueType(1), LHS, RHS, ISD::SETEQ);

    Results.push_back(ExtRes.getValue(0));
    Results.push_back(Success);
    Results.push_back(Res.getValue(1));
    break;
  }
  case ISD::DYNAMIC_STACKALLOC:
    ExpandDYNAMIC_STACKALLOC(Node, Results);
    break;
  case ISD::MERGE_VALUES:
    for (unsigned i = 0; i < Node->getNumValues(); i++)
      Results.push_back(Node->getOperand(i));
    break;
  case ISD::UNDEF: {
    EVT VT = Node->getValueType(0);
    if (VT.isInteger())
      Results.push_back(DAG.getConstant(0, dl, VT));
    else {
      assert(VT.isFloatingPoint() && "Unknown value type!");
      Results.push_back(DAG.getConstantFP(0, dl, VT));
    }
    break;
  }
  case ISD::FP_ROUND:
  case ISD::BITCAST:
    Tmp1 = EmitStackConvert(Node->getOperand(0), Node->getValueType(0),
                            Node->getValueType(0), dl);
    Results.push_back(Tmp1);
    break;
  case ISD::FP_EXTEND:
    Tmp1 = EmitStackConvert(Node->getOperand(0),
                            Node->getOperand(0).getValueType(),
                            Node->getValueType(0), dl);
    Results.push_back(Tmp1);
    break;
  case ISD::SIGN_EXTEND_INREG: {
    EVT ExtraVT = cast<VTSDNode>(Node->getOperand(1))->getVT();
    EVT VT = Node->getValueType(0);

    // An in-register sign-extend of a boolean is a negation:
    // 'true' (1) sign-extended is -1.
    // 'false' (0) sign-extended is 0.
    // However, we must mask the high bits of the source operand because the
    // SIGN_EXTEND_INREG does not guarantee that the high bits are already zero.

    // TODO: Do this for vectors too?
    if (ExtraVT.getSizeInBits() == 1) {
      SDValue One = DAG.getConstant(1, dl, VT);
      SDValue And = DAG.getNode(ISD::AND, dl, VT, Node->getOperand(0), One);
      SDValue Zero = DAG.getConstant(0, dl, VT);
      SDValue Neg = DAG.getNode(ISD::SUB, dl, VT, Zero, And);
      Results.push_back(Neg);
      break;
    }

    // NOTE: we could fall back on load/store here too for targets without
    // SRA.  However, it is doubtful that any exist.
    EVT ShiftAmountTy = TLI.getShiftAmountTy(VT, DAG.getDataLayout());
    unsigned BitsDiff = VT.getScalarSizeInBits() -
                        ExtraVT.getScalarSizeInBits();
    SDValue ShiftCst = DAG.getConstant(BitsDiff, dl, ShiftAmountTy);
    Tmp1 = DAG.getNode(ISD::SHL, dl, Node->getValueType(0),
                       Node->getOperand(0), ShiftCst);
    Tmp1 = DAG.getNode(ISD::SRA, dl, Node->getValueType(0), Tmp1, ShiftCst);
    Results.push_back(Tmp1);
    break;
  }
  case ISD::FP_ROUND_INREG: {
    // The only way we can lower this is to turn it into a TRUNCSTORE,
    // EXTLOAD pair, targeting a temporary location (a stack slot).

    // NOTE: there is a choice here between constantly creating new stack
    // slots and always reusing the same one.  We currently always create
    // new ones, as reuse may inhibit scheduling.
    EVT ExtraVT = cast<VTSDNode>(Node->getOperand(1))->getVT();
    Tmp1 = EmitStackConvert(Node->getOperand(0), ExtraVT,
                            Node->getValueType(0), dl);
    Results.push_back(Tmp1);
    break;
  }
  case ISD::UINT_TO_FP:
    if (TLI.expandUINT_TO_FP(Node, Tmp1, DAG)) {
      Results.push_back(Tmp1);
      break;
    }
    LLVM_FALLTHROUGH;
  case ISD::SINT_TO_FP:
    Tmp1 = ExpandLegalINT_TO_FP(Node->getOpcode() == ISD::SINT_TO_FP,
                                Node->getOperand(0), Node->getValueType(0), dl);
    Results.push_back(Tmp1);
    break;
  case ISD::FP_TO_SINT:
    if (TLI.expandFP_TO_SINT(Node, Tmp1, DAG))
      Results.push_back(Tmp1);
    break;
  case ISD::FP_TO_UINT:
    if (TLI.expandFP_TO_UINT(Node, Tmp1, DAG))
      Results.push_back(Tmp1);
    break;
  case ISD::VAARG:
    Results.push_back(DAG.expandVAArg(Node));
    Results.push_back(Results[0].getValue(1));
    break;
  case ISD::VACOPY:
    Results.push_back(DAG.expandVACopy(Node));
    break;
  case ISD::EXTRACT_VECTOR_ELT:
    if (Node->getOperand(0).getValueType().getVectorNumElements() == 1)
      // This must be an access of the only element.  Return it.
      Tmp1 = DAG.getNode(ISD::BITCAST, dl, Node->getValueType(0),
                         Node->getOperand(0));
    else
      Tmp1 = ExpandExtractFromVectorThroughStack(SDValue(Node, 0));
    Results.push_back(Tmp1);
    break;
  case ISD::EXTRACT_SUBVECTOR:
    Results.push_back(ExpandExtractFromVectorThroughStack(SDValue(Node, 0)));
    break;
  case ISD::INSERT_SUBVECTOR:
    Results.push_back(ExpandInsertToVectorThroughStack(SDValue(Node, 0)));
    break;
  case ISD::CONCAT_VECTORS:
    Results.push_back(ExpandVectorBuildThroughStack(Node));
    break;
  case ISD::SCALAR_TO_VECTOR:
    Results.push_back(ExpandSCALAR_TO_VECTOR(Node));
    break;
  case ISD::INSERT_VECTOR_ELT:
    Results.push_back(ExpandINSERT_VECTOR_ELT(Node->getOperand(0),
                                              Node->getOperand(1),
                                              Node->getOperand(2), dl));
    break;
  case ISD::VECTOR_SHUFFLE: {
    SmallVector<int, 32> NewMask;
    ArrayRef<int> Mask = cast<ShuffleVectorSDNode>(Node)->getMask();

    EVT VT = Node->getValueType(0);
    EVT EltVT = VT.getVectorElementType();
    SDValue Op0 = Node->getOperand(0);
    SDValue Op1 = Node->getOperand(1);
    if (!TLI.isTypeLegal(EltVT)) {
      EVT NewEltVT = TLI.getTypeToTransformTo(*DAG.getContext(), EltVT);

      // BUILD_VECTOR operands are allowed to be wider than the element type.
      // But if NewEltVT is smaller that EltVT the BUILD_VECTOR does not accept
      // it.
      if (NewEltVT.bitsLT(EltVT)) {
        // Convert shuffle node.
        // If original node was v4i64 and the new EltVT is i32,
        // cast operands to v8i32 and re-build the mask.

        // Calculate new VT, the size of the new VT should be equal to original.
        EVT NewVT =
            EVT::getVectorVT(*DAG.getContext(), NewEltVT,
                             VT.getSizeInBits() / NewEltVT.getSizeInBits());
        assert(NewVT.bitsEq(VT));

        // cast operands to new VT
        Op0 = DAG.getNode(ISD::BITCAST, dl, NewVT, Op0);
        Op1 = DAG.getNode(ISD::BITCAST, dl, NewVT, Op1);

        // Convert the shuffle mask
        unsigned int factor =
                         NewVT.getVectorNumElements()/VT.getVectorNumElements();

        // EltVT gets smaller
        assert(factor > 0);

        for (unsigned i = 0; i < VT.getVectorNumElements(); ++i) {
          if (Mask[i] < 0) {
            for (unsigned fi = 0; fi < factor; ++fi)
              NewMask.push_back(Mask[i]);
          }
          else {
            for (unsigned fi = 0; fi < factor; ++fi)
              NewMask.push_back(Mask[i]*factor+fi);
          }
        }
        Mask = NewMask;
        VT = NewVT;
      }
      EltVT = NewEltVT;
    }
    unsigned NumElems = VT.getVectorNumElements();
    SmallVector<SDValue, 16> Ops;
    for (unsigned i = 0; i != NumElems; ++i) {
      if (Mask[i] < 0) {
        Ops.push_back(DAG.getUNDEF(EltVT));
        continue;
      }
      unsigned Idx = Mask[i];
      if (Idx < NumElems)
        Ops.push_back(DAG.getNode(
            ISD::EXTRACT_VECTOR_ELT, dl, EltVT, Op0,
            DAG.getConstant(Idx, dl, TLI.getVectorIdxTy(DAG.getDataLayout()))));
      else
        Ops.push_back(DAG.getNode(
            ISD::EXTRACT_VECTOR_ELT, dl, EltVT, Op1,
            DAG.getConstant(Idx - NumElems, dl,
                            TLI.getVectorIdxTy(DAG.getDataLayout()))));
    }

    Tmp1 = DAG.getBuildVector(VT, dl, Ops);
    // We may have changed the BUILD_VECTOR type. Cast it back to the Node type.
    Tmp1 = DAG.getNode(ISD::BITCAST, dl, Node->getValueType(0), Tmp1);
    Results.push_back(Tmp1);
    break;
  }
  case ISD::EXTRACT_ELEMENT: {
    EVT OpTy = Node->getOperand(0).getValueType();
    if (cast<ConstantSDNode>(Node->getOperand(1))->getZExtValue()) {
      // 1 -> Hi
      Tmp1 = DAG.getNode(ISD::SRL, dl, OpTy, Node->getOperand(0),
                         DAG.getConstant(OpTy.getSizeInBits() / 2, dl,
                                         TLI.getShiftAmountTy(
                                             Node->getOperand(0).getValueType(),
                                             DAG.getDataLayout())));
      Tmp1 = DAG.getNode(ISD::TRUNCATE, dl, Node->getValueType(0), Tmp1);
    } else {
      // 0 -> Lo
      Tmp1 = DAG.getNode(ISD::TRUNCATE, dl, Node->getValueType(0),
                         Node->getOperand(0));
    }
    Results.push_back(Tmp1);
    break;
  }
  case ISD::STACKSAVE:
    // Expand to CopyFromReg if the target set
    // StackPointerRegisterToSaveRestore.
    if (unsigned SP = TLI.getStackPointerRegisterToSaveRestore()) {
      Results.push_back(DAG.getCopyFromReg(Node->getOperand(0), dl, SP,
                                           Node->getValueType(0)));
      Results.push_back(Results[0].getValue(1));
    } else {
      Results.push_back(DAG.getUNDEF(Node->getValueType(0)));
      Results.push_back(Node->getOperand(0));
    }
    break;
  case ISD::STACKRESTORE:
    // Expand to CopyToReg if the target set
    // StackPointerRegisterToSaveRestore.
    if (unsigned SP = TLI.getStackPointerRegisterToSaveRestore()) {
      Results.push_back(DAG.getCopyToReg(Node->getOperand(0), dl, SP,
                                         Node->getOperand(1)));
    } else {
      Results.push_back(Node->getOperand(0));
    }
    break;
  case ISD::GET_DYNAMIC_AREA_OFFSET:
    Results.push_back(DAG.getConstant(0, dl, Node->getValueType(0)));
    Results.push_back(Results[0].getValue(0));
    break;
  case ISD::FCOPYSIGN:
    Results.push_back(ExpandFCOPYSIGN(Node));
    break;
  case ISD::FNEG:
    // Expand Y = FNEG(X) ->  Y = SUB -0.0, X
    Tmp1 = DAG.getConstantFP(-0.0, dl, Node->getValueType(0));
    // TODO: If FNEG has fast-math-flags, propagate them to the FSUB.
    Tmp1 = DAG.getNode(ISD::FSUB, dl, Node->getValueType(0), Tmp1,
                       Node->getOperand(0));
    Results.push_back(Tmp1);
    break;
  case ISD::FABS:
    Results.push_back(ExpandFABS(Node));
    break;
  case ISD::SMIN:
  case ISD::SMAX:
  case ISD::UMIN:
  case ISD::UMAX: {
    // Expand Y = MAX(A, B) -> Y = (A > B) ? A : B
    ISD::CondCode Pred;
    switch (Node->getOpcode()) {
    default: llvm_unreachable("How did we get here?");
    case ISD::SMAX: Pred = ISD::SETGT; break;
    case ISD::SMIN: Pred = ISD::SETLT; break;
    case ISD::UMAX: Pred = ISD::SETUGT; break;
    case ISD::UMIN: Pred = ISD::SETULT; break;
    }
    Tmp1 = Node->getOperand(0);
    Tmp2 = Node->getOperand(1);
    Tmp1 = DAG.getSelectCC(dl, Tmp1, Tmp2, Tmp1, Tmp2, Pred);
    Results.push_back(Tmp1);
    break;
  }
  case ISD::FMINNUM:
  case ISD::FMAXNUM: {
    if (SDValue Expanded = TLI.expandFMINNUM_FMAXNUM(Node, DAG))
      Results.push_back(Expanded);
    break;
  }
  case ISD::FSIN:
  case ISD::FCOS: {
    EVT VT = Node->getValueType(0);
    // Turn fsin / fcos into ISD::FSINCOS node if there are a pair of fsin /
    // fcos which share the same operand and both are used.
    if ((TLI.isOperationLegalOrCustom(ISD::FSINCOS, VT) ||
         isSinCosLibcallAvailable(Node, TLI))
        && useSinCos(Node)) {
      SDVTList VTs = DAG.getVTList(VT, VT);
      Tmp1 = DAG.getNode(ISD::FSINCOS, dl, VTs, Node->getOperand(0));
      if (Node->getOpcode() == ISD::FCOS)
        Tmp1 = Tmp1.getValue(1);
      Results.push_back(Tmp1);
    }
    break;
  }
  case ISD::FMAD:
    llvm_unreachable("Illegal fmad should never be formed");

  case ISD::FP16_TO_FP:
    if (Node->getValueType(0) != MVT::f32) {
      // We can extend to types bigger than f32 in two steps without changing
      // the result. Since "f16 -> f32" is much more commonly available, give
      // CodeGen the option of emitting that before resorting to a libcall.
      SDValue Res =
          DAG.getNode(ISD::FP16_TO_FP, dl, MVT::f32, Node->getOperand(0));
      Results.push_back(
          DAG.getNode(ISD::FP_EXTEND, dl, Node->getValueType(0), Res));
    }
    break;
  case ISD::FP_TO_FP16:
    LLVM_DEBUG(dbgs() << "Legalizing FP_TO_FP16\n");
    if (!TLI.useSoftFloat() && TM.Options.UnsafeFPMath) {
      SDValue Op = Node->getOperand(0);
      MVT SVT = Op.getSimpleValueType();
      if ((SVT == MVT::f64 || SVT == MVT::f80) &&
          TLI.isOperationLegalOrCustom(ISD::FP_TO_FP16, MVT::f32)) {
        // Under fastmath, we can expand this node into a fround followed by
        // a float-half conversion.
        SDValue FloatVal = DAG.getNode(ISD::FP_ROUND, dl, MVT::f32, Op,
                                       DAG.getIntPtrConstant(0, dl));
        Results.push_back(
            DAG.getNode(ISD::FP_TO_FP16, dl, Node->getValueType(0), FloatVal));
      }
    }
    break;
  case ISD::ConstantFP: {
    ConstantFPSDNode *CFP = cast<ConstantFPSDNode>(Node);
    // Check to see if this FP immediate is already legal.
    // If this is a legal constant, turn it into a TargetConstantFP node.
    if (!TLI.isFPImmLegal(CFP->getValueAPF(), Node->getValueType(0)))
      Results.push_back(ExpandConstantFP(CFP, true));
    break;
  }
  case ISD::Constant: {
    ConstantSDNode *CP = cast<ConstantSDNode>(Node);
    Results.push_back(ExpandConstant(CP));
    break;
  }
  case ISD::FSUB: {
    EVT VT = Node->getValueType(0);
    if (TLI.isOperationLegalOrCustom(ISD::FADD, VT) &&
        TLI.isOperationLegalOrCustom(ISD::FNEG, VT)) {
      const SDNodeFlags Flags = Node->getFlags();
      Tmp1 = DAG.getNode(ISD::FNEG, dl, VT, Node->getOperand(1));
      Tmp1 = DAG.getNode(ISD::FADD, dl, VT, Node->getOperand(0), Tmp1, Flags);
      Results.push_back(Tmp1);
    }
    break;
  }
  case ISD::SUB: {
    EVT VT = Node->getValueType(0);
    assert(TLI.isOperationLegalOrCustom(ISD::ADD, VT) &&
           TLI.isOperationLegalOrCustom(ISD::XOR, VT) &&
           "Don't know how to expand this subtraction!");
    Tmp1 = DAG.getNode(ISD::XOR, dl, VT, Node->getOperand(1),
               DAG.getConstant(APInt::getAllOnesValue(VT.getSizeInBits()), dl,
                               VT));
    Tmp1 = DAG.getNode(ISD::ADD, dl, VT, Tmp1, DAG.getConstant(1, dl, VT));
    Results.push_back(DAG.getNode(ISD::ADD, dl, VT, Node->getOperand(0), Tmp1));
    break;
  }
  case ISD::UREM:
  case ISD::SREM: {
    EVT VT = Node->getValueType(0);
    bool isSigned = Node->getOpcode() == ISD::SREM;
    unsigned DivOpc = isSigned ? ISD::SDIV : ISD::UDIV;
    unsigned DivRemOpc = isSigned ? ISD::SDIVREM : ISD::UDIVREM;
    Tmp2 = Node->getOperand(0);
    Tmp3 = Node->getOperand(1);
    if (TLI.isOperationLegalOrCustom(DivRemOpc, VT)) {
      SDVTList VTs = DAG.getVTList(VT, VT);
      Tmp1 = DAG.getNode(DivRemOpc, dl, VTs, Tmp2, Tmp3).getValue(1);
      Results.push_back(Tmp1);
    } else if (TLI.isOperationLegalOrCustom(DivOpc, VT)) {
      // X % Y -> X-X/Y*Y
      Tmp1 = DAG.getNode(DivOpc, dl, VT, Tmp2, Tmp3);
      Tmp1 = DAG.getNode(ISD::MUL, dl, VT, Tmp1, Tmp3);
      Tmp1 = DAG.getNode(ISD::SUB, dl, VT, Tmp2, Tmp1);
      Results.push_back(Tmp1);
    }
    break;
  }
  case ISD::UDIV:
  case ISD::SDIV: {
    bool isSigned = Node->getOpcode() == ISD::SDIV;
    unsigned DivRemOpc = isSigned ? ISD::SDIVREM : ISD::UDIVREM;
    EVT VT = Node->getValueType(0);
    if (TLI.isOperationLegalOrCustom(DivRemOpc, VT)) {
      SDVTList VTs = DAG.getVTList(VT, VT);
      Tmp1 = DAG.getNode(DivRemOpc, dl, VTs, Node->getOperand(0),
                         Node->getOperand(1));
      Results.push_back(Tmp1);
    }
    break;
  }
  case ISD::MULHU:
  case ISD::MULHS: {
    unsigned ExpandOpcode =
        Node->getOpcode() == ISD::MULHU ? ISD::UMUL_LOHI : ISD::SMUL_LOHI;
    EVT VT = Node->getValueType(0);
    SDVTList VTs = DAG.getVTList(VT, VT);

    Tmp1 = DAG.getNode(ExpandOpcode, dl, VTs, Node->getOperand(0),
                       Node->getOperand(1));
    Results.push_back(Tmp1.getValue(1));
    break;
  }
  case ISD::UMUL_LOHI:
  case ISD::SMUL_LOHI: {
    SDValue LHS = Node->getOperand(0);
    SDValue RHS = Node->getOperand(1);
    MVT VT = LHS.getSimpleValueType();
    unsigned MULHOpcode =
        Node->getOpcode() == ISD::UMUL_LOHI ? ISD::MULHU : ISD::MULHS;

    if (TLI.isOperationLegalOrCustom(MULHOpcode, VT)) {
      Results.push_back(DAG.getNode(ISD::MUL, dl, VT, LHS, RHS));
      Results.push_back(DAG.getNode(MULHOpcode, dl, VT, LHS, RHS));
      break;
    }

    SmallVector<SDValue, 4> Halves;
    EVT HalfType = EVT(VT).getHalfSizedIntegerVT(*DAG.getContext());
    assert(TLI.isTypeLegal(HalfType));
    if (TLI.expandMUL_LOHI(Node->getOpcode(), VT, Node, LHS, RHS, Halves,
                           HalfType, DAG,
                           TargetLowering::MulExpansionKind::Always)) {
      for (unsigned i = 0; i < 2; ++i) {
        SDValue Lo = DAG.getNode(ISD::ZERO_EXTEND, dl, VT, Halves[2 * i]);
        SDValue Hi = DAG.getNode(ISD::ANY_EXTEND, dl, VT, Halves[2 * i + 1]);
        SDValue Shift = DAG.getConstant(
            HalfType.getScalarSizeInBits(), dl,
            TLI.getShiftAmountTy(HalfType, DAG.getDataLayout()));
        Hi = DAG.getNode(ISD::SHL, dl, VT, Hi, Shift);
        Results.push_back(DAG.getNode(ISD::OR, dl, VT, Lo, Hi));
      }
      break;
    }
    break;
  }
  case ISD::MUL: {
    EVT VT = Node->getValueType(0);
    SDVTList VTs = DAG.getVTList(VT, VT);
    // See if multiply or divide can be lowered using two-result operations.
    // We just need the low half of the multiply; try both the signed
    // and unsigned forms. If the target supports both SMUL_LOHI and
    // UMUL_LOHI, form a preference by checking which forms of plain
    // MULH it supports.
    bool HasSMUL_LOHI = TLI.isOperationLegalOrCustom(ISD::SMUL_LOHI, VT);
    bool HasUMUL_LOHI = TLI.isOperationLegalOrCustom(ISD::UMUL_LOHI, VT);
    bool HasMULHS = TLI.isOperationLegalOrCustom(ISD::MULHS, VT);
    bool HasMULHU = TLI.isOperationLegalOrCustom(ISD::MULHU, VT);
    unsigned OpToUse = 0;
    if (HasSMUL_LOHI && !HasMULHS) {
      OpToUse = ISD::SMUL_LOHI;
    } else if (HasUMUL_LOHI && !HasMULHU) {
      OpToUse = ISD::UMUL_LOHI;
    } else if (HasSMUL_LOHI) {
      OpToUse = ISD::SMUL_LOHI;
    } else if (HasUMUL_LOHI) {
      OpToUse = ISD::UMUL_LOHI;
    }
    if (OpToUse) {
      Results.push_back(DAG.getNode(OpToUse, dl, VTs, Node->getOperand(0),
                                    Node->getOperand(1)));
      break;
    }

    SDValue Lo, Hi;
    EVT HalfType = VT.getHalfSizedIntegerVT(*DAG.getContext());
    if (TLI.isOperationLegalOrCustom(ISD::ZERO_EXTEND, VT) &&
        TLI.isOperationLegalOrCustom(ISD::ANY_EXTEND, VT) &&
        TLI.isOperationLegalOrCustom(ISD::SHL, VT) &&
        TLI.isOperationLegalOrCustom(ISD::OR, VT) &&
        TLI.expandMUL(Node, Lo, Hi, HalfType, DAG,
                      TargetLowering::MulExpansionKind::OnlyLegalOrCustom)) {
      Lo = DAG.getNode(ISD::ZERO_EXTEND, dl, VT, Lo);
      Hi = DAG.getNode(ISD::ANY_EXTEND, dl, VT, Hi);
      SDValue Shift =
          DAG.getConstant(HalfType.getSizeInBits(), dl,
                          TLI.getShiftAmountTy(HalfType, DAG.getDataLayout()));
      Hi = DAG.getNode(ISD::SHL, dl, VT, Hi, Shift);
      Results.push_back(DAG.getNode(ISD::OR, dl, VT, Lo, Hi));
    }
    break;
  }
  case ISD::FSHL:
  case ISD::FSHR:
    if (TLI.expandFunnelShift(Node, Tmp1, DAG))
      Results.push_back(Tmp1);
    break;
  case ISD::ROTL:
  case ISD::ROTR:
    if (TLI.expandROT(Node, Tmp1, DAG))
      Results.push_back(Tmp1);
    break;
  case ISD::SADDSAT:
  case ISD::UADDSAT:
  case ISD::SSUBSAT:
  case ISD::USUBSAT:
    Results.push_back(TLI.expandAddSubSat(Node, DAG));
    break;
  case ISD::SMULFIX:
    Results.push_back(TLI.getExpandedFixedPointMultiplication(Node, DAG));
    break;
  case ISD::SADDO:
  case ISD::SSUBO: {
    SDValue LHS = Node->getOperand(0);
    SDValue RHS = Node->getOperand(1);
    SDValue Sum = DAG.getNode(Node->getOpcode() == ISD::SADDO ?
                              ISD::ADD : ISD::SUB, dl, LHS.getValueType(),
                              LHS, RHS);
    Results.push_back(Sum);
    EVT ResultType = Node->getValueType(1);
    EVT OType = getSetCCResultType(Node->getValueType(0));

    SDValue Zero = DAG.getConstant(0, dl, LHS.getValueType());

    //   LHSSign -> LHS >= 0
    //   RHSSign -> RHS >= 0
    //   SumSign -> Sum >= 0
    //
    //   Add:
    //   Overflow -> (LHSSign == RHSSign) && (LHSSign != SumSign)
    //   Sub:
    //   Overflow -> (LHSSign != RHSSign) && (LHSSign != SumSign)
    SDValue LHSSign = DAG.getSetCC(dl, OType, LHS, Zero, ISD::SETGE);
    SDValue RHSSign = DAG.getSetCC(dl, OType, RHS, Zero, ISD::SETGE);
    SDValue SignsMatch = DAG.getSetCC(dl, OType, LHSSign, RHSSign,
                                      Node->getOpcode() == ISD::SADDO ?
                                      ISD::SETEQ : ISD::SETNE);

    SDValue SumSign = DAG.getSetCC(dl, OType, Sum, Zero, ISD::SETGE);
    SDValue SumSignNE = DAG.getSetCC(dl, OType, LHSSign, SumSign, ISD::SETNE);

    SDValue Cmp = DAG.getNode(ISD::AND, dl, OType, SignsMatch, SumSignNE);
    Results.push_back(DAG.getBoolExtOrTrunc(Cmp, dl, ResultType, ResultType));
    break;
  }
  case ISD::UADDO:
  case ISD::USUBO: {
    SDValue LHS = Node->getOperand(0);
    SDValue RHS = Node->getOperand(1);
    bool IsAdd = Node->getOpcode() == ISD::UADDO;
    // If ADD/SUBCARRY is legal, use that instead.
    unsigned OpcCarry = IsAdd ? ISD::ADDCARRY : ISD::SUBCARRY;
    if (TLI.isOperationLegalOrCustom(OpcCarry, Node->getValueType(0))) {
      SDValue CarryIn = DAG.getConstant(0, dl, Node->getValueType(1));
      SDValue NodeCarry = DAG.getNode(OpcCarry, dl, Node->getVTList(),
                                      { LHS, RHS, CarryIn });
      Results.push_back(SDValue(NodeCarry.getNode(), 0));
      Results.push_back(SDValue(NodeCarry.getNode(), 1));
      break;
    }

    SDValue Sum = DAG.getNode(IsAdd ? ISD::ADD : ISD::SUB, dl,
                              LHS.getValueType(), LHS, RHS);
    Results.push_back(Sum);

    EVT ResultType = Node->getValueType(1);
    EVT SetCCType = getSetCCResultType(Node->getValueType(0));
    ISD::CondCode CC = IsAdd ? ISD::SETULT : ISD::SETUGT;
    SDValue SetCC = DAG.getSetCC(dl, SetCCType, Sum, LHS, CC);

    Results.push_back(DAG.getBoolExtOrTrunc(SetCC, dl, ResultType, ResultType));
    break;
  }
  case ISD::UMULO:
  case ISD::SMULO: {
    EVT VT = Node->getValueType(0);
    EVT WideVT = EVT::getIntegerVT(*DAG.getContext(), VT.getSizeInBits() * 2);
    SDValue LHS = Node->getOperand(0);
    SDValue RHS = Node->getOperand(1);
    SDValue BottomHalf;
    SDValue TopHalf;
    static const unsigned Ops[2][3] =
        { { ISD::MULHU, ISD::UMUL_LOHI, ISD::ZERO_EXTEND },
          { ISD::MULHS, ISD::SMUL_LOHI, ISD::SIGN_EXTEND }};
    bool isSigned = Node->getOpcode() == ISD::SMULO;
    if (TLI.isOperationLegalOrCustom(Ops[isSigned][0], VT)) {
      BottomHalf = DAG.getNode(ISD::MUL, dl, VT, LHS, RHS);
      TopHalf = DAG.getNode(Ops[isSigned][0], dl, VT, LHS, RHS);
    } else if (TLI.isOperationLegalOrCustom(Ops[isSigned][1], VT)) {
      BottomHalf = DAG.getNode(Ops[isSigned][1], dl, DAG.getVTList(VT, VT), LHS,
                               RHS);
      TopHalf = BottomHalf.getValue(1);
    } else if (TLI.isTypeLegal(WideVT)) {
      LHS = DAG.getNode(Ops[isSigned][2], dl, WideVT, LHS);
      RHS = DAG.getNode(Ops[isSigned][2], dl, WideVT, RHS);
      Tmp1 = DAG.getNode(ISD::MUL, dl, WideVT, LHS, RHS);
      BottomHalf = DAG.getNode(ISD::EXTRACT_ELEMENT, dl, VT, Tmp1,
                               DAG.getIntPtrConstant(0, dl));
      TopHalf = DAG.getNode(ISD::EXTRACT_ELEMENT, dl, VT, Tmp1,
                            DAG.getIntPtrConstant(1, dl));
    } else {
      // We can fall back to a libcall with an illegal type for the MUL if we
      // have a libcall big enough.
      // Also, we can fall back to a division in some cases, but that's a big
      // performance hit in the general case.
      RTLIB::Libcall LC = RTLIB::UNKNOWN_LIBCALL;
      if (WideVT == MVT::i16)
        LC = RTLIB::MUL_I16;
      else if (WideVT == MVT::i32)
        LC = RTLIB::MUL_I32;
      else if (WideVT == MVT::i64)
        LC = RTLIB::MUL_I64;
      else if (WideVT == MVT::i128)
        LC = RTLIB::MUL_I128;
      assert(LC != RTLIB::UNKNOWN_LIBCALL && "Cannot expand this operation!");

      SDValue HiLHS;
      SDValue HiRHS;
      if (isSigned) {
        // The high part is obtained by SRA'ing all but one of the bits of low
        // part.
        unsigned LoSize = VT.getSizeInBits();
        HiLHS =
            DAG.getNode(ISD::SRA, dl, VT, LHS,
                        DAG.getConstant(LoSize - 1, dl,
                                        TLI.getPointerTy(DAG.getDataLayout())));
        HiRHS =
            DAG.getNode(ISD::SRA, dl, VT, RHS,
                        DAG.getConstant(LoSize - 1, dl,
                                        TLI.getPointerTy(DAG.getDataLayout())));
      } else {
          HiLHS = DAG.getConstant(0, dl, VT);
          HiRHS = DAG.getConstant(0, dl, VT);
      }

      // Here we're passing the 2 arguments explicitly as 4 arguments that are
      // pre-lowered to the correct types. This all depends upon WideVT not
      // being a legal type for the architecture and thus has to be split to
      // two arguments.
      SDValue Ret;
      if(DAG.getDataLayout().isLittleEndian()) {
        // Halves of WideVT are packed into registers in different order
        // depending on platform endianness. This is usually handled by
        // the C calling convention, but we can't defer to it in
        // the legalizer.
        SDValue Args[] = { LHS, HiLHS, RHS, HiRHS };
        Ret = ExpandLibCall(LC, WideVT, Args, 4, isSigned, dl);
      } else {
        SDValue Args[] = { HiLHS, LHS, HiRHS, RHS };
        Ret = ExpandLibCall(LC, WideVT, Args, 4, isSigned, dl);
      }
      assert(Ret.getOpcode() == ISD::MERGE_VALUES &&
             "Ret value is a collection of constituent nodes holding result.");
      BottomHalf = Ret.getOperand(0);
      TopHalf = Ret.getOperand(1);
    }

    if (isSigned) {
      Tmp1 = DAG.getConstant(
          VT.getSizeInBits() - 1, dl,
          TLI.getShiftAmountTy(BottomHalf.getValueType(), DAG.getDataLayout()));
      Tmp1 = DAG.getNode(ISD::SRA, dl, VT, BottomHalf, Tmp1);
      TopHalf = DAG.getSetCC(dl, getSetCCResultType(VT), TopHalf, Tmp1,
                             ISD::SETNE);
    } else {
      TopHalf = DAG.getSetCC(dl, getSetCCResultType(VT), TopHalf,
                             DAG.getConstant(0, dl, VT), ISD::SETNE);
    }

    // Truncate the result if SetCC returns a larger type than needed.
    EVT RType = Node->getValueType(1);
    if (RType.getSizeInBits() < TopHalf.getValueSizeInBits())
      TopHalf = DAG.getNode(ISD::TRUNCATE, dl, RType, TopHalf);

    assert(RType.getSizeInBits() == TopHalf.getValueSizeInBits() &&
           "Unexpected result type for S/UMULO legalization");

    Results.push_back(BottomHalf);
    Results.push_back(TopHalf);
    break;
  }
  case ISD::BUILD_PAIR: {
    EVT PairTy = Node->getValueType(0);
    Tmp1 = DAG.getNode(ISD::ZERO_EXTEND, dl, PairTy, Node->getOperand(0));
    Tmp2 = DAG.getNode(ISD::ANY_EXTEND, dl, PairTy, Node->getOperand(1));
    Tmp2 = DAG.getNode(
        ISD::SHL, dl, PairTy, Tmp2,
        DAG.getConstant(PairTy.getSizeInBits() / 2, dl,
                        TLI.getShiftAmountTy(PairTy, DAG.getDataLayout())));
    Results.push_back(DAG.getNode(ISD::OR, dl, PairTy, Tmp1, Tmp2));
    break;
  }
  case ISD::SELECT:
    Tmp1 = Node->getOperand(0);
    Tmp2 = Node->getOperand(1);
    Tmp3 = Node->getOperand(2);
    if (Tmp1.getOpcode() == ISD::SETCC) {
      Tmp1 = DAG.getSelectCC(dl, Tmp1.getOperand(0), Tmp1.getOperand(1),
                             Tmp2, Tmp3,
                             cast<CondCodeSDNode>(Tmp1.getOperand(2))->get());
    } else {
      Tmp1 = DAG.getSelectCC(dl, Tmp1,
                             DAG.getConstant(0, dl, Tmp1.getValueType()),
                             Tmp2, Tmp3, ISD::SETNE);
    }
    Results.push_back(Tmp1);
    break;
  case ISD::BR_JT: {
    SDValue Chain = Node->getOperand(0);
    SDValue Table = Node->getOperand(1);
    SDValue Index = Node->getOperand(2);

    const DataLayout &TD = DAG.getDataLayout();
    EVT PTy = TLI.getPointerTy(TD);

    unsigned EntrySize =
      DAG.getMachineFunction().getJumpTableInfo()->getEntrySize(TD);

    // For power-of-two jumptable entry sizes convert multiplication to a shift.
    // This transformation needs to be done here since otherwise the MIPS
    // backend will end up emitting a three instruction multiply sequence
    // instead of a single shift and MSP430 will call a runtime function.
    if (llvm::isPowerOf2_32(EntrySize))
      Index = DAG.getNode(
          ISD::SHL, dl, Index.getValueType(), Index,
          DAG.getConstant(llvm::Log2_32(EntrySize), dl, Index.getValueType()));
    else
      Index = DAG.getNode(ISD::MUL, dl, Index.getValueType(), Index,
                          DAG.getConstant(EntrySize, dl, Index.getValueType()));
    SDValue Addr = DAG.getNode(ISD::ADD, dl, Index.getValueType(),
                               Index, Table);

    EVT MemVT = EVT::getIntegerVT(*DAG.getContext(), EntrySize * 8);
    SDValue LD = DAG.getExtLoad(
        ISD::SEXTLOAD, dl, PTy, Chain, Addr,
        MachinePointerInfo::getJumpTable(DAG.getMachineFunction()), MemVT);
    Addr = LD;
    if (TLI.isJumpTableRelative()) {
      // For PIC, the sequence is:
      // BRIND(load(Jumptable + index) + RelocBase)
      // RelocBase can be JumpTable, GOT or some sort of global base.
      Addr = DAG.getNode(ISD::ADD, dl, PTy, Addr,
                          TLI.getPICJumpTableRelocBase(Table, DAG));
    }

    Tmp1 = TLI.expandIndirectJTBranch(dl, LD.getValue(1), Addr, DAG);
    Results.push_back(Tmp1);
    break;
  }
  case ISD::BRCOND:
    // Expand brcond's setcc into its constituent parts and create a BR_CC
    // Node.
    Tmp1 = Node->getOperand(0);
    Tmp2 = Node->getOperand(1);
    if (Tmp2.getOpcode() == ISD::SETCC) {
      Tmp1 = DAG.getNode(ISD::BR_CC, dl, MVT::Other,
                         Tmp1, Tmp2.getOperand(2),
                         Tmp2.getOperand(0), Tmp2.getOperand(1),
                         Node->getOperand(2));
    } else {
      // We test only the i1 bit.  Skip the AND if UNDEF or another AND.
      if (Tmp2.isUndef() ||
          (Tmp2.getOpcode() == ISD::AND &&
           isa<ConstantSDNode>(Tmp2.getOperand(1)) &&
           cast<ConstantSDNode>(Tmp2.getOperand(1))->getZExtValue() == 1))
        Tmp3 = Tmp2;
      else
        Tmp3 = DAG.getNode(ISD::AND, dl, Tmp2.getValueType(), Tmp2,
                           DAG.getConstant(1, dl, Tmp2.getValueType()));
      Tmp1 = DAG.getNode(ISD::BR_CC, dl, MVT::Other, Tmp1,
                         DAG.getCondCode(ISD::SETNE), Tmp3,
                         DAG.getConstant(0, dl, Tmp3.getValueType()),
                         Node->getOperand(2));
    }
    Results.push_back(Tmp1);
    break;
  case ISD::SETCC: {
    Tmp1 = Node->getOperand(0);
    Tmp2 = Node->getOperand(1);
    Tmp3 = Node->getOperand(2);
    bool Legalized = LegalizeSetCCCondCode(Node->getValueType(0), Tmp1, Tmp2,
                                           Tmp3, NeedInvert, dl);

    if (Legalized) {
      // If we expanded the SETCC by swapping LHS and RHS, or by inverting the
      // condition code, create a new SETCC node.
      if (Tmp3.getNode())
        Tmp1 = DAG.getNode(ISD::SETCC, dl, Node->getValueType(0),
                           Tmp1, Tmp2, Tmp3);

      // If we expanded the SETCC by inverting the condition code, then wrap
      // the existing SETCC in a NOT to restore the intended condition.
      if (NeedInvert)
        Tmp1 = DAG.getLogicalNOT(dl, Tmp1, Tmp1->getValueType(0));

      Results.push_back(Tmp1);
      break;
    }

    // Otherwise, SETCC for the given comparison type must be completely
    // illegal; expand it into a SELECT_CC.
    EVT VT = Node->getValueType(0);
    int TrueValue;
    switch (TLI.getBooleanContents(Tmp1.getValueType())) {
    case TargetLowering::ZeroOrOneBooleanContent:
    case TargetLowering::UndefinedBooleanContent:
      TrueValue = 1;
      break;
    case TargetLowering::ZeroOrNegativeOneBooleanContent:
      TrueValue = -1;
      break;
    }
    Tmp1 = DAG.getNode(ISD::SELECT_CC, dl, VT, Tmp1, Tmp2,
                       DAG.getConstant(TrueValue, dl, VT),
                       DAG.getConstant(0, dl, VT),
                       Tmp3);
    Results.push_back(Tmp1);
    break;
  }
  case ISD::SELECT_CC: {
    Tmp1 = Node->getOperand(0);   // LHS
    Tmp2 = Node->getOperand(1);   // RHS
    Tmp3 = Node->getOperand(2);   // True
    Tmp4 = Node->getOperand(3);   // False
    EVT VT = Node->getValueType(0);
    SDValue CC = Node->getOperand(4);
    ISD::CondCode CCOp = cast<CondCodeSDNode>(CC)->get();

    if (TLI.isCondCodeLegalOrCustom(CCOp, Tmp1.getSimpleValueType())) {
      // If the condition code is legal, then we need to expand this
      // node using SETCC and SELECT.
      EVT CmpVT = Tmp1.getValueType();
      assert(!TLI.isOperationExpand(ISD::SELECT, VT) &&
             "Cannot expand ISD::SELECT_CC when ISD::SELECT also needs to be "
             "expanded.");
      EVT CCVT =
          TLI.getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), CmpVT);
      SDValue Cond = DAG.getNode(ISD::SETCC, dl, CCVT, Tmp1, Tmp2, CC);
      Results.push_back(DAG.getSelect(dl, VT, Cond, Tmp3, Tmp4));
      break;
    }

    // SELECT_CC is legal, so the condition code must not be.
    bool Legalized = false;
    // Try to legalize by inverting the condition.  This is for targets that
    // might support an ordered version of a condition, but not the unordered
    // version (or vice versa).
    ISD::CondCode InvCC = ISD::getSetCCInverse(CCOp,
                                               Tmp1.getValueType().isInteger());
    if (TLI.isCondCodeLegalOrCustom(InvCC, Tmp1.getSimpleValueType())) {
      // Use the new condition code and swap true and false
      Legalized = true;
      Tmp1 = DAG.getSelectCC(dl, Tmp1, Tmp2, Tmp4, Tmp3, InvCC);
    } else {
      // If The inverse is not legal, then try to swap the arguments using
      // the inverse condition code.
      ISD::CondCode SwapInvCC = ISD::getSetCCSwappedOperands(InvCC);
      if (TLI.isCondCodeLegalOrCustom(SwapInvCC, Tmp1.getSimpleValueType())) {
        // The swapped inverse condition is legal, so swap true and false,
        // lhs and rhs.
        Legalized = true;
        Tmp1 = DAG.getSelectCC(dl, Tmp2, Tmp1, Tmp4, Tmp3, SwapInvCC);
      }
    }

    if (!Legalized) {
      Legalized = LegalizeSetCCCondCode(
          getSetCCResultType(Tmp1.getValueType()), Tmp1, Tmp2, CC, NeedInvert,
          dl);

      assert(Legalized && "Can't legalize SELECT_CC with legal condition!");

      // If we expanded the SETCC by inverting the condition code, then swap
      // the True/False operands to match.
      if (NeedInvert)
        std::swap(Tmp3, Tmp4);

      // If we expanded the SETCC by swapping LHS and RHS, or by inverting the
      // condition code, create a new SELECT_CC node.
      if (CC.getNode()) {
        Tmp1 = DAG.getNode(ISD::SELECT_CC, dl, Node->getValueType(0),
                           Tmp1, Tmp2, Tmp3, Tmp4, CC);
      } else {
        Tmp2 = DAG.getConstant(0, dl, Tmp1.getValueType());
        CC = DAG.getCondCode(ISD::SETNE);
        Tmp1 = DAG.getNode(ISD::SELECT_CC, dl, Node->getValueType(0), Tmp1,
                           Tmp2, Tmp3, Tmp4, CC);
      }
    }
    Results.push_back(Tmp1);
    break;
  }
  case ISD::BR_CC: {
    Tmp1 = Node->getOperand(0);              // Chain
    Tmp2 = Node->getOperand(2);              // LHS
    Tmp3 = Node->getOperand(3);              // RHS
    Tmp4 = Node->getOperand(1);              // CC

    bool Legalized = LegalizeSetCCCondCode(getSetCCResultType(
        Tmp2.getValueType()), Tmp2, Tmp3, Tmp4, NeedInvert, dl);
    (void)Legalized;
    assert(Legalized && "Can't legalize BR_CC with legal condition!");

    assert(!NeedInvert && "Don't know how to invert BR_CC!");

    // If we expanded the SETCC by swapping LHS and RHS, create a new BR_CC
    // node.
    if (Tmp4.getNode()) {
      Tmp1 = DAG.getNode(ISD::BR_CC, dl, Node->getValueType(0), Tmp1,
                         Tmp4, Tmp2, Tmp3, Node->getOperand(4));
    } else {
      Tmp3 = DAG.getConstant(0, dl, Tmp2.getValueType());
      Tmp4 = DAG.getCondCode(ISD::SETNE);
      Tmp1 = DAG.getNode(ISD::BR_CC, dl, Node->getValueType(0), Tmp1, Tmp4,
                         Tmp2, Tmp3, Node->getOperand(4));
    }
    Results.push_back(Tmp1);
    break;
  }
  case ISD::BUILD_VECTOR:
    Results.push_back(ExpandBUILD_VECTOR(Node));
    break;
  case ISD::SRA:
  case ISD::SRL:
  case ISD::SHL: {
    // Scalarize vector SRA/SRL/SHL.
    EVT VT = Node->getValueType(0);
    assert(VT.isVector() && "Unable to legalize non-vector shift");
    assert(TLI.isTypeLegal(VT.getScalarType())&& "Element type must be legal");
    unsigned NumElem = VT.getVectorNumElements();

    SmallVector<SDValue, 8> Scalars;
    for (unsigned Idx = 0; Idx < NumElem; Idx++) {
      SDValue Ex = DAG.getNode(
          ISD::EXTRACT_VECTOR_ELT, dl, VT.getScalarType(), Node->getOperand(0),
          DAG.getConstant(Idx, dl, TLI.getVectorIdxTy(DAG.getDataLayout())));
      SDValue Sh = DAG.getNode(
          ISD::EXTRACT_VECTOR_ELT, dl, VT.getScalarType(), Node->getOperand(1),
          DAG.getConstant(Idx, dl, TLI.getVectorIdxTy(DAG.getDataLayout())));
      Scalars.push_back(DAG.getNode(Node->getOpcode(), dl,
                                    VT.getScalarType(), Ex, Sh));
    }

    SDValue Result = DAG.getBuildVector(Node->getValueType(0), dl, Scalars);
    ReplaceNode(SDValue(Node, 0), Result);
    break;
  }
  case ISD::GLOBAL_OFFSET_TABLE:
  case ISD::GlobalAddress:
  case ISD::GlobalTLSAddress:
  case ISD::ExternalSymbol:
  case ISD::ConstantPool:
  case ISD::JumpTable:
  case ISD::INTRINSIC_W_CHAIN:
  case ISD::INTRINSIC_WO_CHAIN:
  case ISD::INTRINSIC_VOID:
    // FIXME: Custom lowering for these operations shouldn't return null!
    break;
  }

  // Replace the original node with the legalized result.
  if (Results.empty()) {
    LLVM_DEBUG(dbgs() << "Cannot expand node\n");
    return false;
  }

  LLVM_DEBUG(dbgs() << "Successfully expanded node\n");
  ReplaceNode(Node, Results.data());
  return true;
}

void SelectionDAGLegalize::ConvertNodeToLibcall(SDNode *Node) {
  LLVM_DEBUG(dbgs() << "Trying to convert node to libcall\n");
  SmallVector<SDValue, 8> Results;
  SDLoc dl(Node);
  // FIXME: Check flags on the node to see if we can use a finite call.
  bool CanUseFiniteLibCall = TM.Options.NoInfsFPMath && TM.Options.NoNaNsFPMath;
  unsigned Opc = Node->getOpcode();
  switch (Opc) {
  case ISD::ATOMIC_FENCE: {
    // If the target didn't lower this, lower it to '__sync_synchronize()' call
    // FIXME: handle "fence singlethread" more efficiently.
    TargetLowering::ArgListTy Args;

    TargetLowering::CallLoweringInfo CLI(DAG);
    CLI.setDebugLoc(dl)
        .setChain(Node->getOperand(0))
        .setLibCallee(
            CallingConv::C, Type::getVoidTy(*DAG.getContext()),
            DAG.getExternalSymbol("__sync_synchronize",
                                  TLI.getPointerTy(DAG.getDataLayout())),
            std::move(Args));

    std::pair<SDValue, SDValue> CallResult = TLI.LowerCallTo(CLI);

    Results.push_back(CallResult.second);
    break;
  }
  // By default, atomic intrinsics are marked Legal and lowered. Targets
  // which don't support them directly, however, may want libcalls, in which
  // case they mark them Expand, and we get here.
  case ISD::ATOMIC_SWAP:
  case ISD::ATOMIC_LOAD_ADD:
  case ISD::ATOMIC_LOAD_SUB:
  case ISD::ATOMIC_LOAD_AND:
  case ISD::ATOMIC_LOAD_CLR:
  case ISD::ATOMIC_LOAD_OR:
  case ISD::ATOMIC_LOAD_XOR:
  case ISD::ATOMIC_LOAD_NAND:
  case ISD::ATOMIC_LOAD_MIN:
  case ISD::ATOMIC_LOAD_MAX:
  case ISD::ATOMIC_LOAD_UMIN:
  case ISD::ATOMIC_LOAD_UMAX:
  case ISD::ATOMIC_CMP_SWAP: {
    MVT VT = cast<AtomicSDNode>(Node)->getMemoryVT().getSimpleVT();
    RTLIB::Libcall LC = RTLIB::getSYNC(Opc, VT);
    assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unexpected atomic op or value type!");

    std::pair<SDValue, SDValue> Tmp = ExpandChainLibCall(LC, Node, false);
    Results.push_back(Tmp.first);
    Results.push_back(Tmp.second);
    break;
  }
  case ISD::TRAP: {
    // If this operation is not supported, lower it to 'abort()' call
    TargetLowering::ArgListTy Args;
    TargetLowering::CallLoweringInfo CLI(DAG);
    CLI.setDebugLoc(dl)
        .setChain(Node->getOperand(0))
        .setLibCallee(CallingConv::C, Type::getVoidTy(*DAG.getContext()),
                      DAG.getExternalSymbol(
                          "abort", TLI.getPointerTy(DAG.getDataLayout())),
                      std::move(Args));
    std::pair<SDValue, SDValue> CallResult = TLI.LowerCallTo(CLI);

    Results.push_back(CallResult.second);
    break;
  }
  case ISD::FMINNUM:
  case ISD::STRICT_FMINNUM:
    Results.push_back(ExpandFPLibCall(Node, RTLIB::FMIN_F32, RTLIB::FMIN_F64,
                                      RTLIB::FMIN_F80, RTLIB::FMIN_F128,
                                      RTLIB::FMIN_PPCF128));
    break;
  case ISD::FMAXNUM:
  case ISD::STRICT_FMAXNUM:
    Results.push_back(ExpandFPLibCall(Node, RTLIB::FMAX_F32, RTLIB::FMAX_F64,
                                      RTLIB::FMAX_F80, RTLIB::FMAX_F128,
                                      RTLIB::FMAX_PPCF128));
    break;
  case ISD::FSQRT:
  case ISD::STRICT_FSQRT:
    Results.push_back(ExpandFPLibCall(Node, RTLIB::SQRT_F32, RTLIB::SQRT_F64,
                                      RTLIB::SQRT_F80, RTLIB::SQRT_F128,
                                      RTLIB::SQRT_PPCF128));
    break;
  case ISD::FCBRT:
    Results.push_back(ExpandFPLibCall(Node, RTLIB::CBRT_F32, RTLIB::CBRT_F64,
                                      RTLIB::CBRT_F80, RTLIB::CBRT_F128,
                                      RTLIB::CBRT_PPCF128));
    break;
  case ISD::FSIN:
  case ISD::STRICT_FSIN:
    Results.push_back(ExpandFPLibCall(Node, RTLIB::SIN_F32, RTLIB::SIN_F64,
                                      RTLIB::SIN_F80, RTLIB::SIN_F128,
                                      RTLIB::SIN_PPCF128));
    break;
  case ISD::FCOS:
  case ISD::STRICT_FCOS:
    Results.push_back(ExpandFPLibCall(Node, RTLIB::COS_F32, RTLIB::COS_F64,
                                      RTLIB::COS_F80, RTLIB::COS_F128,
                                      RTLIB::COS_PPCF128));
    break;
  case ISD::FSINCOS:
    // Expand into sincos libcall.
    ExpandSinCosLibCall(Node, Results);
    break;
  case ISD::FLOG:
  case ISD::STRICT_FLOG:
    if (CanUseFiniteLibCall && DAG.getLibInfo().has(LibFunc_log_finite))
      Results.push_back(ExpandFPLibCall(Node, RTLIB::LOG_FINITE_F32,
                                        RTLIB::LOG_FINITE_F64,
                                        RTLIB::LOG_FINITE_F80,
                                        RTLIB::LOG_FINITE_F128,
                                        RTLIB::LOG_FINITE_PPCF128));
    else
      Results.push_back(ExpandFPLibCall(Node, RTLIB::LOG_F32, RTLIB::LOG_F64,
                                        RTLIB::LOG_F80, RTLIB::LOG_F128,
                                        RTLIB::LOG_PPCF128));
    break;
  case ISD::FLOG2:
  case ISD::STRICT_FLOG2:
    if (CanUseFiniteLibCall && DAG.getLibInfo().has(LibFunc_log2_finite))
      Results.push_back(ExpandFPLibCall(Node, RTLIB::LOG2_FINITE_F32,
                                        RTLIB::LOG2_FINITE_F64,
                                        RTLIB::LOG2_FINITE_F80,
                                        RTLIB::LOG2_FINITE_F128,
                                        RTLIB::LOG2_FINITE_PPCF128));
    else
      Results.push_back(ExpandFPLibCall(Node, RTLIB::LOG2_F32, RTLIB::LOG2_F64,
                                        RTLIB::LOG2_F80, RTLIB::LOG2_F128,
                                        RTLIB::LOG2_PPCF128));
    break;
  case ISD::FLOG10:
  case ISD::STRICT_FLOG10:
    if (CanUseFiniteLibCall && DAG.getLibInfo().has(LibFunc_log10_finite))
      Results.push_back(ExpandFPLibCall(Node, RTLIB::LOG10_FINITE_F32,
                                        RTLIB::LOG10_FINITE_F64,
                                        RTLIB::LOG10_FINITE_F80,
                                        RTLIB::LOG10_FINITE_F128,
                                        RTLIB::LOG10_FINITE_PPCF128));
    else
      Results.push_back(ExpandFPLibCall(Node, RTLIB::LOG10_F32, RTLIB::LOG10_F64,
                                        RTLIB::LOG10_F80, RTLIB::LOG10_F128,
                                        RTLIB::LOG10_PPCF128));
    break;
  case ISD::FEXP:
  case ISD::STRICT_FEXP:
    if (CanUseFiniteLibCall && DAG.getLibInfo().has(LibFunc_exp_finite))
      Results.push_back(ExpandFPLibCall(Node, RTLIB::EXP_FINITE_F32,
                                        RTLIB::EXP_FINITE_F64,
                                        RTLIB::EXP_FINITE_F80,
                                        RTLIB::EXP_FINITE_F128,
                                        RTLIB::EXP_FINITE_PPCF128));
    else
      Results.push_back(ExpandFPLibCall(Node, RTLIB::EXP_F32, RTLIB::EXP_F64,
                                        RTLIB::EXP_F80, RTLIB::EXP_F128,
                                        RTLIB::EXP_PPCF128));
    break;
  case ISD::FEXP2:
  case ISD::STRICT_FEXP2:
    if (CanUseFiniteLibCall && DAG.getLibInfo().has(LibFunc_exp2_finite))
      Results.push_back(ExpandFPLibCall(Node, RTLIB::EXP2_FINITE_F32,
                                        RTLIB::EXP2_FINITE_F64,
                                        RTLIB::EXP2_FINITE_F80,
                                        RTLIB::EXP2_FINITE_F128,
                                        RTLIB::EXP2_FINITE_PPCF128));
    else
      Results.push_back(ExpandFPLibCall(Node, RTLIB::EXP2_F32, RTLIB::EXP2_F64,
                                        RTLIB::EXP2_F80, RTLIB::EXP2_F128,
                                        RTLIB::EXP2_PPCF128));
    break;
  case ISD::FTRUNC:
  case ISD::STRICT_FTRUNC:
    Results.push_back(ExpandFPLibCall(Node, RTLIB::TRUNC_F32, RTLIB::TRUNC_F64,
                                      RTLIB::TRUNC_F80, RTLIB::TRUNC_F128,
                                      RTLIB::TRUNC_PPCF128));
    break;
  case ISD::FFLOOR:
  case ISD::STRICT_FFLOOR:
    Results.push_back(ExpandFPLibCall(Node, RTLIB::FLOOR_F32, RTLIB::FLOOR_F64,
                                      RTLIB::FLOOR_F80, RTLIB::FLOOR_F128,
                                      RTLIB::FLOOR_PPCF128));
    break;
  case ISD::FCEIL:
  case ISD::STRICT_FCEIL:
    Results.push_back(ExpandFPLibCall(Node, RTLIB::CEIL_F32, RTLIB::CEIL_F64,
                                      RTLIB::CEIL_F80, RTLIB::CEIL_F128,
                                      RTLIB::CEIL_PPCF128));
    break;
  case ISD::FRINT:
  case ISD::STRICT_FRINT:
    Results.push_back(ExpandFPLibCall(Node, RTLIB::RINT_F32, RTLIB::RINT_F64,
                                      RTLIB::RINT_F80, RTLIB::RINT_F128,
                                      RTLIB::RINT_PPCF128));
    break;
  case ISD::FNEARBYINT:
  case ISD::STRICT_FNEARBYINT:
    Results.push_back(ExpandFPLibCall(Node, RTLIB::NEARBYINT_F32,
                                      RTLIB::NEARBYINT_F64,
                                      RTLIB::NEARBYINT_F80,
                                      RTLIB::NEARBYINT_F128,
                                      RTLIB::NEARBYINT_PPCF128));
    break;
  case ISD::FROUND:
  case ISD::STRICT_FROUND:
    Results.push_back(ExpandFPLibCall(Node, RTLIB::ROUND_F32,
                                      RTLIB::ROUND_F64,
                                      RTLIB::ROUND_F80,
                                      RTLIB::ROUND_F128,
                                      RTLIB::ROUND_PPCF128));
    break;
  case ISD::FPOWI:
  case ISD::STRICT_FPOWI:
    Results.push_back(ExpandFPLibCall(Node, RTLIB::POWI_F32, RTLIB::POWI_F64,
                                      RTLIB::POWI_F80, RTLIB::POWI_F128,
                                      RTLIB::POWI_PPCF128));
    break;
  case ISD::FPOW:
  case ISD::STRICT_FPOW:
    if (CanUseFiniteLibCall && DAG.getLibInfo().has(LibFunc_pow_finite))
      Results.push_back(ExpandFPLibCall(Node, RTLIB::POW_FINITE_F32,
                                        RTLIB::POW_FINITE_F64,
                                        RTLIB::POW_FINITE_F80,
                                        RTLIB::POW_FINITE_F128,
                                        RTLIB::POW_FINITE_PPCF128));
    else
      Results.push_back(ExpandFPLibCall(Node, RTLIB::POW_F32, RTLIB::POW_F64,
                                        RTLIB::POW_F80, RTLIB::POW_F128,
                                        RTLIB::POW_PPCF128));
    break;
  case ISD::FDIV:
    Results.push_back(ExpandFPLibCall(Node, RTLIB::DIV_F32, RTLIB::DIV_F64,
                                      RTLIB::DIV_F80, RTLIB::DIV_F128,
                                      RTLIB::DIV_PPCF128));
    break;
  case ISD::FREM:
  case ISD::STRICT_FREM:
    Results.push_back(ExpandFPLibCall(Node, RTLIB::REM_F32, RTLIB::REM_F64,
                                      RTLIB::REM_F80, RTLIB::REM_F128,
                                      RTLIB::REM_PPCF128));
    break;
  case ISD::FMA:
  case ISD::STRICT_FMA:
    Results.push_back(ExpandFPLibCall(Node, RTLIB::FMA_F32, RTLIB::FMA_F64,
                                      RTLIB::FMA_F80, RTLIB::FMA_F128,
                                      RTLIB::FMA_PPCF128));
    break;
  case ISD::FADD:
    Results.push_back(ExpandFPLibCall(Node, RTLIB::ADD_F32, RTLIB::ADD_F64,
                                      RTLIB::ADD_F80, RTLIB::ADD_F128,
                                      RTLIB::ADD_PPCF128));
    break;
  case ISD::FMUL:
    Results.push_back(ExpandFPLibCall(Node, RTLIB::MUL_F32, RTLIB::MUL_F64,
                                      RTLIB::MUL_F80, RTLIB::MUL_F128,
                                      RTLIB::MUL_PPCF128));
    break;
  case ISD::FP16_TO_FP:
    if (Node->getValueType(0) == MVT::f32) {
      Results.push_back(ExpandLibCall(RTLIB::FPEXT_F16_F32, Node, false));
    }
    break;
  case ISD::FP_TO_FP16: {
    RTLIB::Libcall LC =
        RTLIB::getFPROUND(Node->getOperand(0).getValueType(), MVT::f16);
    assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unable to expand fp_to_fp16");
    Results.push_back(ExpandLibCall(LC, Node, false));
    break;
  }
  case ISD::FSUB:
    Results.push_back(ExpandFPLibCall(Node, RTLIB::SUB_F32, RTLIB::SUB_F64,
                                      RTLIB::SUB_F80, RTLIB::SUB_F128,
                                      RTLIB::SUB_PPCF128));
    break;
  case ISD::SREM:
    Results.push_back(ExpandIntLibCall(Node, true,
                                       RTLIB::SREM_I8,
                                       RTLIB::SREM_I16, RTLIB::SREM_I32,
                                       RTLIB::SREM_I64, RTLIB::SREM_I128));
    break;
  case ISD::UREM:
    Results.push_back(ExpandIntLibCall(Node, false,
                                       RTLIB::UREM_I8,
                                       RTLIB::UREM_I16, RTLIB::UREM_I32,
                                       RTLIB::UREM_I64, RTLIB::UREM_I128));
    break;
  case ISD::SDIV:
    Results.push_back(ExpandIntLibCall(Node, true,
                                       RTLIB::SDIV_I8,
                                       RTLIB::SDIV_I16, RTLIB::SDIV_I32,
                                       RTLIB::SDIV_I64, RTLIB::SDIV_I128));
    break;
  case ISD::UDIV:
    Results.push_back(ExpandIntLibCall(Node, false,
                                       RTLIB::UDIV_I8,
                                       RTLIB::UDIV_I16, RTLIB::UDIV_I32,
                                       RTLIB::UDIV_I64, RTLIB::UDIV_I128));
    break;
  case ISD::SDIVREM:
  case ISD::UDIVREM:
    // Expand into divrem libcall
    ExpandDivRemLibCall(Node, Results);
    break;
  case ISD::MUL:
    Results.push_back(ExpandIntLibCall(Node, false,
                                       RTLIB::MUL_I8,
                                       RTLIB::MUL_I16, RTLIB::MUL_I32,
                                       RTLIB::MUL_I64, RTLIB::MUL_I128));
    break;
  case ISD::CTLZ_ZERO_UNDEF:
    switch (Node->getSimpleValueType(0).SimpleTy) {
    default:
      llvm_unreachable("LibCall explicitly requested, but not available");
    case MVT::i32:
      Results.push_back(ExpandLibCall(RTLIB::CTLZ_I32, Node, false));
      break;
    case MVT::i64:
      Results.push_back(ExpandLibCall(RTLIB::CTLZ_I64, Node, false));
      break;
    case MVT::i128:
      Results.push_back(ExpandLibCall(RTLIB::CTLZ_I128, Node, false));
      break;
    }
    break;
  }

  // Replace the original node with the legalized result.
  if (!Results.empty()) {
    LLVM_DEBUG(dbgs() << "Successfully converted node to libcall\n");
    ReplaceNode(Node, Results.data());
  } else
    LLVM_DEBUG(dbgs() << "Could not convert node to libcall\n");
}

// Determine the vector type to use in place of an original scalar element when
// promoting equally sized vectors.
static MVT getPromotedVectorElementType(const TargetLowering &TLI,
                                        MVT EltVT, MVT NewEltVT) {
  unsigned OldEltsPerNewElt = EltVT.getSizeInBits() / NewEltVT.getSizeInBits();
  MVT MidVT = MVT::getVectorVT(NewEltVT, OldEltsPerNewElt);
  assert(TLI.isTypeLegal(MidVT) && "unexpected");
  return MidVT;
}

void SelectionDAGLegalize::PromoteNode(SDNode *Node) {
  LLVM_DEBUG(dbgs() << "Trying to promote node\n");
  SmallVector<SDValue, 8> Results;
  MVT OVT = Node->getSimpleValueType(0);
  if (Node->getOpcode() == ISD::UINT_TO_FP ||
      Node->getOpcode() == ISD::SINT_TO_FP ||
      Node->getOpcode() == ISD::SETCC ||
      Node->getOpcode() == ISD::EXTRACT_VECTOR_ELT ||
      Node->getOpcode() == ISD::INSERT_VECTOR_ELT) {
    OVT = Node->getOperand(0).getSimpleValueType();
  }
  if (Node->getOpcode() == ISD::BR_CC)
    OVT = Node->getOperand(2).getSimpleValueType();
  MVT NVT = TLI.getTypeToPromoteTo(Node->getOpcode(), OVT);
  SDLoc dl(Node);
  SDValue Tmp1, Tmp2, Tmp3;
  switch (Node->getOpcode()) {
  case ISD::CTTZ:
  case ISD::CTTZ_ZERO_UNDEF:
  case ISD::CTLZ:
  case ISD::CTLZ_ZERO_UNDEF:
  case ISD::CTPOP:
    // Zero extend the argument.
    Tmp1 = DAG.getNode(ISD::ZERO_EXTEND, dl, NVT, Node->getOperand(0));
    if (Node->getOpcode() == ISD::CTTZ) {
      // The count is the same in the promoted type except if the original
      // value was zero.  This can be handled by setting the bit just off
      // the top of the original type.
      auto TopBit = APInt::getOneBitSet(NVT.getSizeInBits(),
                                        OVT.getSizeInBits());
      Tmp1 = DAG.getNode(ISD::OR, dl, NVT, Tmp1,
                         DAG.getConstant(TopBit, dl, NVT));
    }
    // Perform the larger operation. For CTPOP and CTTZ_ZERO_UNDEF, this is
    // already the correct result.
    Tmp1 = DAG.getNode(Node->getOpcode(), dl, NVT, Tmp1);
    if (Node->getOpcode() == ISD::CTLZ ||
        Node->getOpcode() == ISD::CTLZ_ZERO_UNDEF) {
      // Tmp1 = Tmp1 - (sizeinbits(NVT) - sizeinbits(Old VT))
      Tmp1 = DAG.getNode(ISD::SUB, dl, NVT, Tmp1,
                          DAG.getConstant(NVT.getSizeInBits() -
                                          OVT.getSizeInBits(), dl, NVT));
    }
    Results.push_back(DAG.getNode(ISD::TRUNCATE, dl, OVT, Tmp1));
    break;
  case ISD::BITREVERSE:
  case ISD::BSWAP: {
    unsigned DiffBits = NVT.getSizeInBits() - OVT.getSizeInBits();
    Tmp1 = DAG.getNode(ISD::ZERO_EXTEND, dl, NVT, Node->getOperand(0));
    Tmp1 = DAG.getNode(Node->getOpcode(), dl, NVT, Tmp1);
    Tmp1 = DAG.getNode(
        ISD::SRL, dl, NVT, Tmp1,
        DAG.getConstant(DiffBits, dl,
                        TLI.getShiftAmountTy(NVT, DAG.getDataLayout())));

    Results.push_back(DAG.getNode(ISD::TRUNCATE, dl, OVT, Tmp1));
    break;
  }
  case ISD::FP_TO_UINT:
  case ISD::FP_TO_SINT:
    Tmp1 = PromoteLegalFP_TO_INT(Node->getOperand(0), Node->getValueType(0),
                                 Node->getOpcode() == ISD::FP_TO_SINT, dl);
    Results.push_back(Tmp1);
    break;
  case ISD::UINT_TO_FP:
  case ISD::SINT_TO_FP:
    Tmp1 = PromoteLegalINT_TO_FP(Node->getOperand(0), Node->getValueType(0),
                                 Node->getOpcode() == ISD::SINT_TO_FP, dl);
    Results.push_back(Tmp1);
    break;
  case ISD::VAARG: {
    SDValue Chain = Node->getOperand(0); // Get the chain.
    SDValue Ptr = Node->getOperand(1); // Get the pointer.

    unsigned TruncOp;
    if (OVT.isVector()) {
      TruncOp = ISD::BITCAST;
    } else {
      assert(OVT.isInteger()
        && "VAARG promotion is supported only for vectors or integer types");
      TruncOp = ISD::TRUNCATE;
    }

    // Perform the larger operation, then convert back
    Tmp1 = DAG.getVAArg(NVT, dl, Chain, Ptr, Node->getOperand(2),
             Node->getConstantOperandVal(3));
    Chain = Tmp1.getValue(1);

    Tmp2 = DAG.getNode(TruncOp, dl, OVT, Tmp1);

    // Modified the chain result - switch anything that used the old chain to
    // use the new one.
    DAG.ReplaceAllUsesOfValueWith(SDValue(Node, 0), Tmp2);
    DAG.ReplaceAllUsesOfValueWith(SDValue(Node, 1), Chain);
    if (UpdatedNodes) {
      UpdatedNodes->insert(Tmp2.getNode());
      UpdatedNodes->insert(Chain.getNode());
    }
    ReplacedNode(Node);
    break;
  }
  case ISD::MUL:
  case ISD::SDIV:
  case ISD::SREM:
  case ISD::UDIV:
  case ISD::UREM:
  case ISD::AND:
  case ISD::OR:
  case ISD::XOR: {
    unsigned ExtOp, TruncOp;
    if (OVT.isVector()) {
      ExtOp   = ISD::BITCAST;
      TruncOp = ISD::BITCAST;
    } else {
      assert(OVT.isInteger() && "Cannot promote logic operation");

      switch (Node->getOpcode()) {
      default:
        ExtOp = ISD::ANY_EXTEND;
        break;
      case ISD::SDIV:
      case ISD::SREM:
        ExtOp = ISD::SIGN_EXTEND;
        break;
      case ISD::UDIV:
      case ISD::UREM:
        ExtOp = ISD::ZERO_EXTEND;
        break;
      }
      TruncOp = ISD::TRUNCATE;
    }
    // Promote each of the values to the new type.
    Tmp1 = DAG.getNode(ExtOp, dl, NVT, Node->getOperand(0));
    Tmp2 = DAG.getNode(ExtOp, dl, NVT, Node->getOperand(1));
    // Perform the larger operation, then convert back
    Tmp1 = DAG.getNode(Node->getOpcode(), dl, NVT, Tmp1, Tmp2);
    Results.push_back(DAG.getNode(TruncOp, dl, OVT, Tmp1));
    break;
  }
  case ISD::UMUL_LOHI:
  case ISD::SMUL_LOHI: {
    // Promote to a multiply in a wider integer type.
    unsigned ExtOp = Node->getOpcode() == ISD::UMUL_LOHI ? ISD::ZERO_EXTEND
                                                         : ISD::SIGN_EXTEND;
    Tmp1 = DAG.getNode(ExtOp, dl, NVT, Node->getOperand(0));
    Tmp2 = DAG.getNode(ExtOp, dl, NVT, Node->getOperand(1));
    Tmp1 = DAG.getNode(ISD::MUL, dl, NVT, Tmp1, Tmp2);

    auto &DL = DAG.getDataLayout();
    unsigned OriginalSize = OVT.getScalarSizeInBits();
    Tmp2 = DAG.getNode(
        ISD::SRL, dl, NVT, Tmp1,
        DAG.getConstant(OriginalSize, dl, TLI.getScalarShiftAmountTy(DL, NVT)));
    Results.push_back(DAG.getNode(ISD::TRUNCATE, dl, OVT, Tmp1));
    Results.push_back(DAG.getNode(ISD::TRUNCATE, dl, OVT, Tmp2));
    break;
  }
  case ISD::SELECT: {
    unsigned ExtOp, TruncOp;
    if (Node->getValueType(0).isVector() ||
        Node->getValueType(0).getSizeInBits() == NVT.getSizeInBits()) {
      ExtOp   = ISD::BITCAST;
      TruncOp = ISD::BITCAST;
    } else if (Node->getValueType(0).isInteger()) {
      ExtOp   = ISD::ANY_EXTEND;
      TruncOp = ISD::TRUNCATE;
    } else {
      ExtOp   = ISD::FP_EXTEND;
      TruncOp = ISD::FP_ROUND;
    }
    Tmp1 = Node->getOperand(0);
    // Promote each of the values to the new type.
    Tmp2 = DAG.getNode(ExtOp, dl, NVT, Node->getOperand(1));
    Tmp3 = DAG.getNode(ExtOp, dl, NVT, Node->getOperand(2));
    // Perform the larger operation, then round down.
    Tmp1 = DAG.getSelect(dl, NVT, Tmp1, Tmp2, Tmp3);
    if (TruncOp != ISD::FP_ROUND)
      Tmp1 = DAG.getNode(TruncOp, dl, Node->getValueType(0), Tmp1);
    else
      Tmp1 = DAG.getNode(TruncOp, dl, Node->getValueType(0), Tmp1,
                         DAG.getIntPtrConstant(0, dl));
    Results.push_back(Tmp1);
    break;
  }
  case ISD::VECTOR_SHUFFLE: {
    ArrayRef<int> Mask = cast<ShuffleVectorSDNode>(Node)->getMask();

    // Cast the two input vectors.
    Tmp1 = DAG.getNode(ISD::BITCAST, dl, NVT, Node->getOperand(0));
    Tmp2 = DAG.getNode(ISD::BITCAST, dl, NVT, Node->getOperand(1));

    // Convert the shuffle mask to the right # elements.
    Tmp1 = ShuffleWithNarrowerEltType(NVT, OVT, dl, Tmp1, Tmp2, Mask);
    Tmp1 = DAG.getNode(ISD::BITCAST, dl, OVT, Tmp1);
    Results.push_back(Tmp1);
    break;
  }
  case ISD::SETCC: {
    unsigned ExtOp = ISD::FP_EXTEND;
    if (NVT.isInteger()) {
      ISD::CondCode CCCode =
        cast<CondCodeSDNode>(Node->getOperand(2))->get();
      ExtOp = isSignedIntSetCC(CCCode) ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND;
    }
    Tmp1 = DAG.getNode(ExtOp, dl, NVT, Node->getOperand(0));
    Tmp2 = DAG.getNode(ExtOp, dl, NVT, Node->getOperand(1));
    Results.push_back(DAG.getNode(ISD::SETCC, dl, Node->getValueType(0),
                                  Tmp1, Tmp2, Node->getOperand(2)));
    break;
  }
  case ISD::BR_CC: {
    unsigned ExtOp = ISD::FP_EXTEND;
    if (NVT.isInteger()) {
      ISD::CondCode CCCode =
        cast<CondCodeSDNode>(Node->getOperand(1))->get();
      ExtOp = isSignedIntSetCC(CCCode) ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND;
    }
    Tmp1 = DAG.getNode(ExtOp, dl, NVT, Node->getOperand(2));
    Tmp2 = DAG.getNode(ExtOp, dl, NVT, Node->getOperand(3));
    Results.push_back(DAG.getNode(ISD::BR_CC, dl, Node->getValueType(0),
                                  Node->getOperand(0), Node->getOperand(1),
                                  Tmp1, Tmp2, Node->getOperand(4)));
    break;
  }
  case ISD::FADD:
  case ISD::FSUB:
  case ISD::FMUL:
  case ISD::FDIV:
  case ISD::FREM:
  case ISD::FMINNUM:
  case ISD::FMAXNUM:
  case ISD::FPOW:
    Tmp1 = DAG.getNode(ISD::FP_EXTEND, dl, NVT, Node->getOperand(0));
    Tmp2 = DAG.getNode(ISD::FP_EXTEND, dl, NVT, Node->getOperand(1));
    Tmp3 = DAG.getNode(Node->getOpcode(), dl, NVT, Tmp1, Tmp2,
                       Node->getFlags());
    Results.push_back(DAG.getNode(ISD::FP_ROUND, dl, OVT,
                                  Tmp3, DAG.getIntPtrConstant(0, dl)));
    break;
  case ISD::FMA:
    Tmp1 = DAG.getNode(ISD::FP_EXTEND, dl, NVT, Node->getOperand(0));
    Tmp2 = DAG.getNode(ISD::FP_EXTEND, dl, NVT, Node->getOperand(1));
    Tmp3 = DAG.getNode(ISD::FP_EXTEND, dl, NVT, Node->getOperand(2));
    Results.push_back(
        DAG.getNode(ISD::FP_ROUND, dl, OVT,
                    DAG.getNode(Node->getOpcode(), dl, NVT, Tmp1, Tmp2, Tmp3),
                    DAG.getIntPtrConstant(0, dl)));
    break;
  case ISD::FCOPYSIGN:
  case ISD::FPOWI: {
    Tmp1 = DAG.getNode(ISD::FP_EXTEND, dl, NVT, Node->getOperand(0));
    Tmp2 = Node->getOperand(1);
    Tmp3 = DAG.getNode(Node->getOpcode(), dl, NVT, Tmp1, Tmp2);

    // fcopysign doesn't change anything but the sign bit, so
    //   (fp_round (fcopysign (fpext a), b))
    // is as precise as
    //   (fp_round (fpext a))
    // which is a no-op. Mark it as a TRUNCating FP_ROUND.
    const bool isTrunc = (Node->getOpcode() == ISD::FCOPYSIGN);
    Results.push_back(DAG.getNode(ISD::FP_ROUND, dl, OVT,
                                  Tmp3, DAG.getIntPtrConstant(isTrunc, dl)));
    break;
  }
  case ISD::FFLOOR:
  case ISD::FCEIL:
  case ISD::FRINT:
  case ISD::FNEARBYINT:
  case ISD::FROUND:
  case ISD::FTRUNC:
  case ISD::FNEG:
  case ISD::FSQRT:
  case ISD::FSIN:
  case ISD::FCOS:
  case ISD::FLOG:
  case ISD::FLOG2:
  case ISD::FLOG10:
  case ISD::FABS:
  case ISD::FEXP:
  case ISD::FEXP2:
    Tmp1 = DAG.getNode(ISD::FP_EXTEND, dl, NVT, Node->getOperand(0));
    Tmp2 = DAG.getNode(Node->getOpcode(), dl, NVT, Tmp1);
    Results.push_back(DAG.getNode(ISD::FP_ROUND, dl, OVT,
                                  Tmp2, DAG.getIntPtrConstant(0, dl)));
    break;
  case ISD::BUILD_VECTOR: {
    MVT EltVT = OVT.getVectorElementType();
    MVT NewEltVT = NVT.getVectorElementType();

    // Handle bitcasts to a different vector type with the same total bit size
    //
    // e.g. v2i64 = build_vector i64:x, i64:y => v4i32
    //  =>
    //  v4i32 = concat_vectors (v2i32 (bitcast i64:x)), (v2i32 (bitcast i64:y))

    assert(NVT.isVector() && OVT.getSizeInBits() == NVT.getSizeInBits() &&
           "Invalid promote type for build_vector");
    assert(NewEltVT.bitsLT(EltVT) && "not handled");

    MVT MidVT = getPromotedVectorElementType(TLI, EltVT, NewEltVT);

    SmallVector<SDValue, 8> NewOps;
    for (unsigned I = 0, E = Node->getNumOperands(); I != E; ++I) {
      SDValue Op = Node->getOperand(I);
      NewOps.push_back(DAG.getNode(ISD::BITCAST, SDLoc(Op), MidVT, Op));
    }

    SDLoc SL(Node);
    SDValue Concat = DAG.getNode(ISD::CONCAT_VECTORS, SL, NVT, NewOps);
    SDValue CvtVec = DAG.getNode(ISD::BITCAST, SL, OVT, Concat);
    Results.push_back(CvtVec);
    break;
  }
  case ISD::EXTRACT_VECTOR_ELT: {
    MVT EltVT = OVT.getVectorElementType();
    MVT NewEltVT = NVT.getVectorElementType();

    // Handle bitcasts to a different vector type with the same total bit size.
    //
    // e.g. v2i64 = extract_vector_elt x:v2i64, y:i32
    //  =>
    //  v4i32:castx = bitcast x:v2i64
    //
    // i64 = bitcast
    //   (v2i32 build_vector (i32 (extract_vector_elt castx, (2 * y))),
    //                       (i32 (extract_vector_elt castx, (2 * y + 1)))
    //

    assert(NVT.isVector() && OVT.getSizeInBits() == NVT.getSizeInBits() &&
           "Invalid promote type for extract_vector_elt");
    assert(NewEltVT.bitsLT(EltVT) && "not handled");

    MVT MidVT = getPromotedVectorElementType(TLI, EltVT, NewEltVT);
    unsigned NewEltsPerOldElt = MidVT.getVectorNumElements();

    SDValue Idx = Node->getOperand(1);
    EVT IdxVT = Idx.getValueType();
    SDLoc SL(Node);
    SDValue Factor = DAG.getConstant(NewEltsPerOldElt, SL, IdxVT);
    SDValue NewBaseIdx = DAG.getNode(ISD::MUL, SL, IdxVT, Idx, Factor);

    SDValue CastVec = DAG.getNode(ISD::BITCAST, SL, NVT, Node->getOperand(0));

    SmallVector<SDValue, 8> NewOps;
    for (unsigned I = 0; I < NewEltsPerOldElt; ++I) {
      SDValue IdxOffset = DAG.getConstant(I, SL, IdxVT);
      SDValue TmpIdx = DAG.getNode(ISD::ADD, SL, IdxVT, NewBaseIdx, IdxOffset);

      SDValue Elt = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, NewEltVT,
                                CastVec, TmpIdx);
      NewOps.push_back(Elt);
    }

    SDValue NewVec = DAG.getBuildVector(MidVT, SL, NewOps);
    Results.push_back(DAG.getNode(ISD::BITCAST, SL, EltVT, NewVec));
    break;
  }
  case ISD::INSERT_VECTOR_ELT: {
    MVT EltVT = OVT.getVectorElementType();
    MVT NewEltVT = NVT.getVectorElementType();

    // Handle bitcasts to a different vector type with the same total bit size
    //
    // e.g. v2i64 = insert_vector_elt x:v2i64, y:i64, z:i32
    //  =>
    //  v4i32:castx = bitcast x:v2i64
    //  v2i32:casty = bitcast y:i64
    //
    // v2i64 = bitcast
    //   (v4i32 insert_vector_elt
    //       (v4i32 insert_vector_elt v4i32:castx,
    //                                (extract_vector_elt casty, 0), 2 * z),
    //        (extract_vector_elt casty, 1), (2 * z + 1))

    assert(NVT.isVector() && OVT.getSizeInBits() == NVT.getSizeInBits() &&
           "Invalid promote type for insert_vector_elt");
    assert(NewEltVT.bitsLT(EltVT) && "not handled");

    MVT MidVT = getPromotedVectorElementType(TLI, EltVT, NewEltVT);
    unsigned NewEltsPerOldElt = MidVT.getVectorNumElements();

    SDValue Val = Node->getOperand(1);
    SDValue Idx = Node->getOperand(2);
    EVT IdxVT = Idx.getValueType();
    SDLoc SL(Node);

    SDValue Factor = DAG.getConstant(NewEltsPerOldElt, SDLoc(), IdxVT);
    SDValue NewBaseIdx = DAG.getNode(ISD::MUL, SL, IdxVT, Idx, Factor);

    SDValue CastVec = DAG.getNode(ISD::BITCAST, SL, NVT, Node->getOperand(0));
    SDValue CastVal = DAG.getNode(ISD::BITCAST, SL, MidVT, Val);

    SDValue NewVec = CastVec;
    for (unsigned I = 0; I < NewEltsPerOldElt; ++I) {
      SDValue IdxOffset = DAG.getConstant(I, SL, IdxVT);
      SDValue InEltIdx = DAG.getNode(ISD::ADD, SL, IdxVT, NewBaseIdx, IdxOffset);

      SDValue Elt = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, NewEltVT,
                                CastVal, IdxOffset);

      NewVec = DAG.getNode(ISD::INSERT_VECTOR_ELT, SL, NVT,
                           NewVec, Elt, InEltIdx);
    }

    Results.push_back(DAG.getNode(ISD::BITCAST, SL, OVT, NewVec));
    break;
  }
  case ISD::SCALAR_TO_VECTOR: {
    MVT EltVT = OVT.getVectorElementType();
    MVT NewEltVT = NVT.getVectorElementType();

    // Handle bitcasts to different vector type with the same total bit size.
    //
    // e.g. v2i64 = scalar_to_vector x:i64
    //   =>
    //  concat_vectors (v2i32 bitcast x:i64), (v2i32 undef)
    //

    MVT MidVT = getPromotedVectorElementType(TLI, EltVT, NewEltVT);
    SDValue Val = Node->getOperand(0);
    SDLoc SL(Node);

    SDValue CastVal = DAG.getNode(ISD::BITCAST, SL, MidVT, Val);
    SDValue Undef = DAG.getUNDEF(MidVT);

    SmallVector<SDValue, 8> NewElts;
    NewElts.push_back(CastVal);
    for (unsigned I = 1, NElts = OVT.getVectorNumElements(); I != NElts; ++I)
      NewElts.push_back(Undef);

    SDValue Concat = DAG.getNode(ISD::CONCAT_VECTORS, SL, NVT, NewElts);
    SDValue CvtVec = DAG.getNode(ISD::BITCAST, SL, OVT, Concat);
    Results.push_back(CvtVec);
    break;
  }
  }

  // Replace the original node with the legalized result.
  if (!Results.empty()) {
    LLVM_DEBUG(dbgs() << "Successfully promoted node\n");
    ReplaceNode(Node, Results.data());
  } else
    LLVM_DEBUG(dbgs() << "Could not promote node\n");
}

/// This is the entry point for the file.
void SelectionDAG::Legalize() {
  AssignTopologicalOrder();

  SmallPtrSet<SDNode *, 16> LegalizedNodes;
  // Use a delete listener to remove nodes which were deleted during
  // legalization from LegalizeNodes. This is needed to handle the situation
  // where a new node is allocated by the object pool to the same address of a
  // previously deleted node.
  DAGNodeDeletedListener DeleteListener(
      *this,
      [&LegalizedNodes](SDNode *N, SDNode *E) { LegalizedNodes.erase(N); });

  SelectionDAGLegalize Legalizer(*this, LegalizedNodes);

  // Visit all the nodes. We start in topological order, so that we see
  // nodes with their original operands intact. Legalization can produce
  // new nodes which may themselves need to be legalized. Iterate until all
  // nodes have been legalized.
  while (true) {
    bool AnyLegalized = false;
    for (auto NI = allnodes_end(); NI != allnodes_begin();) {
      --NI;

      SDNode *N = &*NI;
      if (N->use_empty() && N != getRoot().getNode()) {
        ++NI;
        DeleteNode(N);
        continue;
      }

      if (LegalizedNodes.insert(N).second) {
        AnyLegalized = true;
        Legalizer.LegalizeOp(N);

        if (N->use_empty() && N != getRoot().getNode()) {
          ++NI;
          DeleteNode(N);
        }
      }
    }
    if (!AnyLegalized)
      break;

  }

  // Remove dead nodes now.
  RemoveDeadNodes();
}

bool SelectionDAG::LegalizeOp(SDNode *N,
                              SmallSetVector<SDNode *, 16> &UpdatedNodes) {
  SmallPtrSet<SDNode *, 16> LegalizedNodes;
  SelectionDAGLegalize Legalizer(*this, LegalizedNodes, &UpdatedNodes);

  // Directly insert the node in question, and legalize it. This will recurse
  // as needed through operands.
  LegalizedNodes.insert(N);
  Legalizer.LegalizeOp(N);

  return LegalizedNodes.count(N);
}
