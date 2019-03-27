//===- llvm/CodeGen/GlobalISel/IRTranslator.cpp - IRTranslator ---*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements the IRTranslator class.
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/GlobalISel/IRTranslator.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/GlobalISel/CallLowering.h"
#include "llvm/CodeGen/GlobalISel/GISelChangeObserver.h"
#include "llvm/CodeGen/LowLevelType.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/StackProtector.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/MC/MCContext.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/LowLevelTypeImpl.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetIntrinsicInfo.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#define DEBUG_TYPE "irtranslator"

using namespace llvm;

static cl::opt<bool>
    EnableCSEInIRTranslator("enable-cse-in-irtranslator",
                            cl::desc("Should enable CSE in irtranslator"),
                            cl::Optional, cl::init(false));
char IRTranslator::ID = 0;

INITIALIZE_PASS_BEGIN(IRTranslator, DEBUG_TYPE, "IRTranslator LLVM IR -> MI",
                false, false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_DEPENDENCY(GISelCSEAnalysisWrapperPass)
INITIALIZE_PASS_END(IRTranslator, DEBUG_TYPE, "IRTranslator LLVM IR -> MI",
                false, false)

static void reportTranslationError(MachineFunction &MF,
                                   const TargetPassConfig &TPC,
                                   OptimizationRemarkEmitter &ORE,
                                   OptimizationRemarkMissed &R) {
  MF.getProperties().set(MachineFunctionProperties::Property::FailedISel);

  // Print the function name explicitly if we don't have a debug location (which
  // makes the diagnostic less useful) or if we're going to emit a raw error.
  if (!R.getLocation().isValid() || TPC.isGlobalISelAbortEnabled())
    R << (" (in function: " + MF.getName() + ")").str();

  if (TPC.isGlobalISelAbortEnabled())
    report_fatal_error(R.getMsg());
  else
    ORE.emit(R);
}

IRTranslator::IRTranslator() : MachineFunctionPass(ID) {
  initializeIRTranslatorPass(*PassRegistry::getPassRegistry());
}

#ifndef NDEBUG
namespace {
/// Verify that every instruction created has the same DILocation as the
/// instruction being translated.
class DILocationVerifier : public GISelChangeObserver {
  const Instruction *CurrInst = nullptr;

public:
  DILocationVerifier() = default;
  ~DILocationVerifier() = default;

  const Instruction *getCurrentInst() const { return CurrInst; }
  void setCurrentInst(const Instruction *Inst) { CurrInst = Inst; }

  void erasingInstr(MachineInstr &MI) override {}
  void changingInstr(MachineInstr &MI) override {}
  void changedInstr(MachineInstr &MI) override {}

  void createdInstr(MachineInstr &MI) override {
    assert(getCurrentInst() && "Inserted instruction without a current MI");

    // Only print the check message if we're actually checking it.
#ifndef NDEBUG
    LLVM_DEBUG(dbgs() << "Checking DILocation from " << *CurrInst
                      << " was copied to " << MI);
#endif
    assert(CurrInst->getDebugLoc() == MI.getDebugLoc() &&
           "Line info was not transferred to all instructions");
  }
};
} // namespace
#endif // ifndef NDEBUG


void IRTranslator::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<StackProtector>();
  AU.addRequired<TargetPassConfig>();
  AU.addRequired<GISelCSEAnalysisWrapperPass>();
  getSelectionDAGFallbackAnalysisUsage(AU);
  MachineFunctionPass::getAnalysisUsage(AU);
}

static void computeValueLLTs(const DataLayout &DL, Type &Ty,
                             SmallVectorImpl<LLT> &ValueTys,
                             SmallVectorImpl<uint64_t> *Offsets = nullptr,
                             uint64_t StartingOffset = 0) {
  // Given a struct type, recursively traverse the elements.
  if (StructType *STy = dyn_cast<StructType>(&Ty)) {
    const StructLayout *SL = DL.getStructLayout(STy);
    for (unsigned I = 0, E = STy->getNumElements(); I != E; ++I)
      computeValueLLTs(DL, *STy->getElementType(I), ValueTys, Offsets,
                       StartingOffset + SL->getElementOffset(I));
    return;
  }
  // Given an array type, recursively traverse the elements.
  if (ArrayType *ATy = dyn_cast<ArrayType>(&Ty)) {
    Type *EltTy = ATy->getElementType();
    uint64_t EltSize = DL.getTypeAllocSize(EltTy);
    for (unsigned i = 0, e = ATy->getNumElements(); i != e; ++i)
      computeValueLLTs(DL, *EltTy, ValueTys, Offsets,
                       StartingOffset + i * EltSize);
    return;
  }
  // Interpret void as zero return values.
  if (Ty.isVoidTy())
    return;
  // Base case: we can get an LLT for this LLVM IR type.
  ValueTys.push_back(getLLTForType(Ty, DL));
  if (Offsets != nullptr)
    Offsets->push_back(StartingOffset * 8);
}

IRTranslator::ValueToVRegInfo::VRegListT &
IRTranslator::allocateVRegs(const Value &Val) {
  assert(!VMap.contains(Val) && "Value already allocated in VMap");
  auto *Regs = VMap.getVRegs(Val);
  auto *Offsets = VMap.getOffsets(Val);
  SmallVector<LLT, 4> SplitTys;
  computeValueLLTs(*DL, *Val.getType(), SplitTys,
                   Offsets->empty() ? Offsets : nullptr);
  for (unsigned i = 0; i < SplitTys.size(); ++i)
    Regs->push_back(0);
  return *Regs;
}

ArrayRef<unsigned> IRTranslator::getOrCreateVRegs(const Value &Val) {
  auto VRegsIt = VMap.findVRegs(Val);
  if (VRegsIt != VMap.vregs_end())
    return *VRegsIt->second;

  if (Val.getType()->isVoidTy())
    return *VMap.getVRegs(Val);

  // Create entry for this type.
  auto *VRegs = VMap.getVRegs(Val);
  auto *Offsets = VMap.getOffsets(Val);

  assert(Val.getType()->isSized() &&
         "Don't know how to create an empty vreg");

  SmallVector<LLT, 4> SplitTys;
  computeValueLLTs(*DL, *Val.getType(), SplitTys,
                   Offsets->empty() ? Offsets : nullptr);

  if (!isa<Constant>(Val)) {
    for (auto Ty : SplitTys)
      VRegs->push_back(MRI->createGenericVirtualRegister(Ty));
    return *VRegs;
  }

  if (Val.getType()->isAggregateType()) {
    // UndefValue, ConstantAggregateZero
    auto &C = cast<Constant>(Val);
    unsigned Idx = 0;
    while (auto Elt = C.getAggregateElement(Idx++)) {
      auto EltRegs = getOrCreateVRegs(*Elt);
      llvm::copy(EltRegs, std::back_inserter(*VRegs));
    }
  } else {
    assert(SplitTys.size() == 1 && "unexpectedly split LLT");
    VRegs->push_back(MRI->createGenericVirtualRegister(SplitTys[0]));
    bool Success = translate(cast<Constant>(Val), VRegs->front());
    if (!Success) {
      OptimizationRemarkMissed R("gisel-irtranslator", "GISelFailure",
                                 MF->getFunction().getSubprogram(),
                                 &MF->getFunction().getEntryBlock());
      R << "unable to translate constant: " << ore::NV("Type", Val.getType());
      reportTranslationError(*MF, *TPC, *ORE, R);
      return *VRegs;
    }
  }

  return *VRegs;
}

int IRTranslator::getOrCreateFrameIndex(const AllocaInst &AI) {
  if (FrameIndices.find(&AI) != FrameIndices.end())
    return FrameIndices[&AI];

  unsigned ElementSize = DL->getTypeStoreSize(AI.getAllocatedType());
  unsigned Size =
      ElementSize * cast<ConstantInt>(AI.getArraySize())->getZExtValue();

  // Always allocate at least one byte.
  Size = std::max(Size, 1u);

  unsigned Alignment = AI.getAlignment();
  if (!Alignment)
    Alignment = DL->getABITypeAlignment(AI.getAllocatedType());

  int &FI = FrameIndices[&AI];
  FI = MF->getFrameInfo().CreateStackObject(Size, Alignment, false, &AI);
  return FI;
}

unsigned IRTranslator::getMemOpAlignment(const Instruction &I) {
  unsigned Alignment = 0;
  Type *ValTy = nullptr;
  if (const StoreInst *SI = dyn_cast<StoreInst>(&I)) {
    Alignment = SI->getAlignment();
    ValTy = SI->getValueOperand()->getType();
  } else if (const LoadInst *LI = dyn_cast<LoadInst>(&I)) {
    Alignment = LI->getAlignment();
    ValTy = LI->getType();
  } else if (const AtomicCmpXchgInst *AI = dyn_cast<AtomicCmpXchgInst>(&I)) {
    // TODO(PR27168): This instruction has no alignment attribute, but unlike
    // the default alignment for load/store, the default here is to assume
    // it has NATURAL alignment, not DataLayout-specified alignment.
    const DataLayout &DL = AI->getModule()->getDataLayout();
    Alignment = DL.getTypeStoreSize(AI->getCompareOperand()->getType());
    ValTy = AI->getCompareOperand()->getType();
  } else if (const AtomicRMWInst *AI = dyn_cast<AtomicRMWInst>(&I)) {
    // TODO(PR27168): This instruction has no alignment attribute, but unlike
    // the default alignment for load/store, the default here is to assume
    // it has NATURAL alignment, not DataLayout-specified alignment.
    const DataLayout &DL = AI->getModule()->getDataLayout();
    Alignment = DL.getTypeStoreSize(AI->getValOperand()->getType());
    ValTy = AI->getType();
  } else {
    OptimizationRemarkMissed R("gisel-irtranslator", "", &I);
    R << "unable to translate memop: " << ore::NV("Opcode", &I);
    reportTranslationError(*MF, *TPC, *ORE, R);
    return 1;
  }

  return Alignment ? Alignment : DL->getABITypeAlignment(ValTy);
}

MachineBasicBlock &IRTranslator::getMBB(const BasicBlock &BB) {
  MachineBasicBlock *&MBB = BBToMBB[&BB];
  assert(MBB && "BasicBlock was not encountered before");
  return *MBB;
}

void IRTranslator::addMachineCFGPred(CFGEdge Edge, MachineBasicBlock *NewPred) {
  assert(NewPred && "new predecessor must be a real MachineBasicBlock");
  MachinePreds[Edge].push_back(NewPred);
}

bool IRTranslator::translateBinaryOp(unsigned Opcode, const User &U,
                                     MachineIRBuilder &MIRBuilder) {
  // FIXME: handle signed/unsigned wrapping flags.

  // Get or create a virtual register for each value.
  // Unless the value is a Constant => loadimm cst?
  // or inline constant each time?
  // Creation of a virtual register needs to have a size.
  unsigned Op0 = getOrCreateVReg(*U.getOperand(0));
  unsigned Op1 = getOrCreateVReg(*U.getOperand(1));
  unsigned Res = getOrCreateVReg(U);
  auto FBinOp = MIRBuilder.buildInstr(Opcode).addDef(Res).addUse(Op0).addUse(Op1);
  if (isa<Instruction>(U)) {
    MachineInstr *FBinOpMI = FBinOp.getInstr();
    const Instruction &I = cast<Instruction>(U);
    FBinOpMI->copyIRFlags(I);
  }
  return true;
}

bool IRTranslator::translateFSub(const User &U, MachineIRBuilder &MIRBuilder) {
  // -0.0 - X --> G_FNEG
  if (isa<Constant>(U.getOperand(0)) &&
      U.getOperand(0) == ConstantFP::getZeroValueForNegation(U.getType())) {
    MIRBuilder.buildInstr(TargetOpcode::G_FNEG)
        .addDef(getOrCreateVReg(U))
        .addUse(getOrCreateVReg(*U.getOperand(1)));
    return true;
  }
  return translateBinaryOp(TargetOpcode::G_FSUB, U, MIRBuilder);
}

bool IRTranslator::translateFNeg(const User &U, MachineIRBuilder &MIRBuilder) {
  MIRBuilder.buildInstr(TargetOpcode::G_FNEG)
      .addDef(getOrCreateVReg(U))
      .addUse(getOrCreateVReg(*U.getOperand(1)));
  return true;
}

bool IRTranslator::translateCompare(const User &U,
                                    MachineIRBuilder &MIRBuilder) {
  const CmpInst *CI = dyn_cast<CmpInst>(&U);
  unsigned Op0 = getOrCreateVReg(*U.getOperand(0));
  unsigned Op1 = getOrCreateVReg(*U.getOperand(1));
  unsigned Res = getOrCreateVReg(U);
  CmpInst::Predicate Pred =
      CI ? CI->getPredicate() : static_cast<CmpInst::Predicate>(
                                    cast<ConstantExpr>(U).getPredicate());
  if (CmpInst::isIntPredicate(Pred))
    MIRBuilder.buildICmp(Pred, Res, Op0, Op1);
  else if (Pred == CmpInst::FCMP_FALSE)
    MIRBuilder.buildCopy(
        Res, getOrCreateVReg(*Constant::getNullValue(CI->getType())));
  else if (Pred == CmpInst::FCMP_TRUE)
    MIRBuilder.buildCopy(
        Res, getOrCreateVReg(*Constant::getAllOnesValue(CI->getType())));
  else {
    auto FCmp = MIRBuilder.buildFCmp(Pred, Res, Op0, Op1);
    FCmp->copyIRFlags(*CI);
  }

  return true;
}

bool IRTranslator::translateRet(const User &U, MachineIRBuilder &MIRBuilder) {
  const ReturnInst &RI = cast<ReturnInst>(U);
  const Value *Ret = RI.getReturnValue();
  if (Ret && DL->getTypeStoreSize(Ret->getType()) == 0)
    Ret = nullptr;

  ArrayRef<unsigned> VRegs;
  if (Ret)
    VRegs = getOrCreateVRegs(*Ret);

  // The target may mess up with the insertion point, but
  // this is not important as a return is the last instruction
  // of the block anyway.

  return CLI->lowerReturn(MIRBuilder, Ret, VRegs);
}

bool IRTranslator::translateBr(const User &U, MachineIRBuilder &MIRBuilder) {
  const BranchInst &BrInst = cast<BranchInst>(U);
  unsigned Succ = 0;
  if (!BrInst.isUnconditional()) {
    // We want a G_BRCOND to the true BB followed by an unconditional branch.
    unsigned Tst = getOrCreateVReg(*BrInst.getCondition());
    const BasicBlock &TrueTgt = *cast<BasicBlock>(BrInst.getSuccessor(Succ++));
    MachineBasicBlock &TrueBB = getMBB(TrueTgt);
    MIRBuilder.buildBrCond(Tst, TrueBB);
  }

  const BasicBlock &BrTgt = *cast<BasicBlock>(BrInst.getSuccessor(Succ));
  MachineBasicBlock &TgtBB = getMBB(BrTgt);
  MachineBasicBlock &CurBB = MIRBuilder.getMBB();

  // If the unconditional target is the layout successor, fallthrough.
  if (!CurBB.isLayoutSuccessor(&TgtBB))
    MIRBuilder.buildBr(TgtBB);

  // Link successors.
  for (const BasicBlock *Succ : successors(&BrInst))
    CurBB.addSuccessor(&getMBB(*Succ));
  return true;
}

bool IRTranslator::translateSwitch(const User &U,
                                   MachineIRBuilder &MIRBuilder) {
  // For now, just translate as a chain of conditional branches.
  // FIXME: could we share most of the logic/code in
  // SelectionDAGBuilder::visitSwitch between SelectionDAG and GlobalISel?
  // At first sight, it seems most of the logic in there is independent of
  // SelectionDAG-specifics and a lot of work went in to optimize switch
  // lowering in there.

  const SwitchInst &SwInst = cast<SwitchInst>(U);
  const unsigned SwCondValue = getOrCreateVReg(*SwInst.getCondition());
  const BasicBlock *OrigBB = SwInst.getParent();

  LLT LLTi1 = getLLTForType(*Type::getInt1Ty(U.getContext()), *DL);
  for (auto &CaseIt : SwInst.cases()) {
    const unsigned CaseValueReg = getOrCreateVReg(*CaseIt.getCaseValue());
    const unsigned Tst = MRI->createGenericVirtualRegister(LLTi1);
    MIRBuilder.buildICmp(CmpInst::ICMP_EQ, Tst, CaseValueReg, SwCondValue);
    MachineBasicBlock &CurMBB = MIRBuilder.getMBB();
    const BasicBlock *TrueBB = CaseIt.getCaseSuccessor();
    MachineBasicBlock &TrueMBB = getMBB(*TrueBB);

    MIRBuilder.buildBrCond(Tst, TrueMBB);
    CurMBB.addSuccessor(&TrueMBB);
    addMachineCFGPred({OrigBB, TrueBB}, &CurMBB);

    MachineBasicBlock *FalseMBB =
        MF->CreateMachineBasicBlock(SwInst.getParent());
    // Insert the comparison blocks one after the other.
    MF->insert(std::next(CurMBB.getIterator()), FalseMBB);
    MIRBuilder.buildBr(*FalseMBB);
    CurMBB.addSuccessor(FalseMBB);

    MIRBuilder.setMBB(*FalseMBB);
  }
  // handle default case
  const BasicBlock *DefaultBB = SwInst.getDefaultDest();
  MachineBasicBlock &DefaultMBB = getMBB(*DefaultBB);
  MIRBuilder.buildBr(DefaultMBB);
  MachineBasicBlock &CurMBB = MIRBuilder.getMBB();
  CurMBB.addSuccessor(&DefaultMBB);
  addMachineCFGPred({OrigBB, DefaultBB}, &CurMBB);

  return true;
}

bool IRTranslator::translateIndirectBr(const User &U,
                                       MachineIRBuilder &MIRBuilder) {
  const IndirectBrInst &BrInst = cast<IndirectBrInst>(U);

  const unsigned Tgt = getOrCreateVReg(*BrInst.getAddress());
  MIRBuilder.buildBrIndirect(Tgt);

  // Link successors.
  MachineBasicBlock &CurBB = MIRBuilder.getMBB();
  for (const BasicBlock *Succ : successors(&BrInst))
    CurBB.addSuccessor(&getMBB(*Succ));

  return true;
}

bool IRTranslator::translateLoad(const User &U, MachineIRBuilder &MIRBuilder) {
  const LoadInst &LI = cast<LoadInst>(U);

  auto Flags = LI.isVolatile() ? MachineMemOperand::MOVolatile
                               : MachineMemOperand::MONone;
  Flags |= MachineMemOperand::MOLoad;

  if (DL->getTypeStoreSize(LI.getType()) == 0)
    return true;

  ArrayRef<unsigned> Regs = getOrCreateVRegs(LI);
  ArrayRef<uint64_t> Offsets = *VMap.getOffsets(LI);
  unsigned Base = getOrCreateVReg(*LI.getPointerOperand());

  for (unsigned i = 0; i < Regs.size(); ++i) {
    unsigned Addr = 0;
    MIRBuilder.materializeGEP(Addr, Base, LLT::scalar(64), Offsets[i] / 8);

    MachinePointerInfo Ptr(LI.getPointerOperand(), Offsets[i] / 8);
    unsigned BaseAlign = getMemOpAlignment(LI);
    auto MMO = MF->getMachineMemOperand(
        Ptr, Flags, (MRI->getType(Regs[i]).getSizeInBits() + 7) / 8,
        MinAlign(BaseAlign, Offsets[i] / 8), AAMDNodes(), nullptr,
        LI.getSyncScopeID(), LI.getOrdering());
    MIRBuilder.buildLoad(Regs[i], Addr, *MMO);
  }

  return true;
}

bool IRTranslator::translateStore(const User &U, MachineIRBuilder &MIRBuilder) {
  const StoreInst &SI = cast<StoreInst>(U);
  auto Flags = SI.isVolatile() ? MachineMemOperand::MOVolatile
                               : MachineMemOperand::MONone;
  Flags |= MachineMemOperand::MOStore;

  if (DL->getTypeStoreSize(SI.getValueOperand()->getType()) == 0)
    return true;

  ArrayRef<unsigned> Vals = getOrCreateVRegs(*SI.getValueOperand());
  ArrayRef<uint64_t> Offsets = *VMap.getOffsets(*SI.getValueOperand());
  unsigned Base = getOrCreateVReg(*SI.getPointerOperand());

  for (unsigned i = 0; i < Vals.size(); ++i) {
    unsigned Addr = 0;
    MIRBuilder.materializeGEP(Addr, Base, LLT::scalar(64), Offsets[i] / 8);

    MachinePointerInfo Ptr(SI.getPointerOperand(), Offsets[i] / 8);
    unsigned BaseAlign = getMemOpAlignment(SI);
    auto MMO = MF->getMachineMemOperand(
        Ptr, Flags, (MRI->getType(Vals[i]).getSizeInBits() + 7) / 8,
        MinAlign(BaseAlign, Offsets[i] / 8), AAMDNodes(), nullptr,
        SI.getSyncScopeID(), SI.getOrdering());
    MIRBuilder.buildStore(Vals[i], Addr, *MMO);
  }
  return true;
}

static uint64_t getOffsetFromIndices(const User &U, const DataLayout &DL) {
  const Value *Src = U.getOperand(0);
  Type *Int32Ty = Type::getInt32Ty(U.getContext());

  // getIndexedOffsetInType is designed for GEPs, so the first index is the
  // usual array element rather than looking into the actual aggregate.
  SmallVector<Value *, 1> Indices;
  Indices.push_back(ConstantInt::get(Int32Ty, 0));

  if (const ExtractValueInst *EVI = dyn_cast<ExtractValueInst>(&U)) {
    for (auto Idx : EVI->indices())
      Indices.push_back(ConstantInt::get(Int32Ty, Idx));
  } else if (const InsertValueInst *IVI = dyn_cast<InsertValueInst>(&U)) {
    for (auto Idx : IVI->indices())
      Indices.push_back(ConstantInt::get(Int32Ty, Idx));
  } else {
    for (unsigned i = 1; i < U.getNumOperands(); ++i)
      Indices.push_back(U.getOperand(i));
  }

  return 8 * static_cast<uint64_t>(
                 DL.getIndexedOffsetInType(Src->getType(), Indices));
}

bool IRTranslator::translateExtractValue(const User &U,
                                         MachineIRBuilder &MIRBuilder) {
  const Value *Src = U.getOperand(0);
  uint64_t Offset = getOffsetFromIndices(U, *DL);
  ArrayRef<unsigned> SrcRegs = getOrCreateVRegs(*Src);
  ArrayRef<uint64_t> Offsets = *VMap.getOffsets(*Src);
  unsigned Idx = std::lower_bound(Offsets.begin(), Offsets.end(), Offset) -
                 Offsets.begin();
  auto &DstRegs = allocateVRegs(U);

  for (unsigned i = 0; i < DstRegs.size(); ++i)
    DstRegs[i] = SrcRegs[Idx++];

  return true;
}

bool IRTranslator::translateInsertValue(const User &U,
                                        MachineIRBuilder &MIRBuilder) {
  const Value *Src = U.getOperand(0);
  uint64_t Offset = getOffsetFromIndices(U, *DL);
  auto &DstRegs = allocateVRegs(U);
  ArrayRef<uint64_t> DstOffsets = *VMap.getOffsets(U);
  ArrayRef<unsigned> SrcRegs = getOrCreateVRegs(*Src);
  ArrayRef<unsigned> InsertedRegs = getOrCreateVRegs(*U.getOperand(1));
  auto InsertedIt = InsertedRegs.begin();

  for (unsigned i = 0; i < DstRegs.size(); ++i) {
    if (DstOffsets[i] >= Offset && InsertedIt != InsertedRegs.end())
      DstRegs[i] = *InsertedIt++;
    else
      DstRegs[i] = SrcRegs[i];
  }

  return true;
}

bool IRTranslator::translateSelect(const User &U,
                                   MachineIRBuilder &MIRBuilder) {
  unsigned Tst = getOrCreateVReg(*U.getOperand(0));
  ArrayRef<unsigned> ResRegs = getOrCreateVRegs(U);
  ArrayRef<unsigned> Op0Regs = getOrCreateVRegs(*U.getOperand(1));
  ArrayRef<unsigned> Op1Regs = getOrCreateVRegs(*U.getOperand(2));

  const SelectInst &SI = cast<SelectInst>(U);
  const CmpInst *Cmp = dyn_cast<CmpInst>(SI.getCondition());
  for (unsigned i = 0; i < ResRegs.size(); ++i) {
    auto Select =
        MIRBuilder.buildSelect(ResRegs[i], Tst, Op0Regs[i], Op1Regs[i]);
    if (Cmp && isa<FPMathOperator>(Cmp)) {
      Select->copyIRFlags(*Cmp);
    }
  }

  return true;
}

bool IRTranslator::translateBitCast(const User &U,
                                    MachineIRBuilder &MIRBuilder) {
  // If we're bitcasting to the source type, we can reuse the source vreg.
  if (getLLTForType(*U.getOperand(0)->getType(), *DL) ==
      getLLTForType(*U.getType(), *DL)) {
    unsigned SrcReg = getOrCreateVReg(*U.getOperand(0));
    auto &Regs = *VMap.getVRegs(U);
    // If we already assigned a vreg for this bitcast, we can't change that.
    // Emit a copy to satisfy the users we already emitted.
    if (!Regs.empty())
      MIRBuilder.buildCopy(Regs[0], SrcReg);
    else {
      Regs.push_back(SrcReg);
      VMap.getOffsets(U)->push_back(0);
    }
    return true;
  }
  return translateCast(TargetOpcode::G_BITCAST, U, MIRBuilder);
}

bool IRTranslator::translateCast(unsigned Opcode, const User &U,
                                 MachineIRBuilder &MIRBuilder) {
  unsigned Op = getOrCreateVReg(*U.getOperand(0));
  unsigned Res = getOrCreateVReg(U);
  MIRBuilder.buildInstr(Opcode).addDef(Res).addUse(Op);
  return true;
}

bool IRTranslator::translateGetElementPtr(const User &U,
                                          MachineIRBuilder &MIRBuilder) {
  // FIXME: support vector GEPs.
  if (U.getType()->isVectorTy())
    return false;

  Value &Op0 = *U.getOperand(0);
  unsigned BaseReg = getOrCreateVReg(Op0);
  Type *PtrIRTy = Op0.getType();
  LLT PtrTy = getLLTForType(*PtrIRTy, *DL);
  Type *OffsetIRTy = DL->getIntPtrType(PtrIRTy);
  LLT OffsetTy = getLLTForType(*OffsetIRTy, *DL);

  int64_t Offset = 0;
  for (gep_type_iterator GTI = gep_type_begin(&U), E = gep_type_end(&U);
       GTI != E; ++GTI) {
    const Value *Idx = GTI.getOperand();
    if (StructType *StTy = GTI.getStructTypeOrNull()) {
      unsigned Field = cast<Constant>(Idx)->getUniqueInteger().getZExtValue();
      Offset += DL->getStructLayout(StTy)->getElementOffset(Field);
      continue;
    } else {
      uint64_t ElementSize = DL->getTypeAllocSize(GTI.getIndexedType());

      // If this is a scalar constant or a splat vector of constants,
      // handle it quickly.
      if (const auto *CI = dyn_cast<ConstantInt>(Idx)) {
        Offset += ElementSize * CI->getSExtValue();
        continue;
      }

      if (Offset != 0) {
        unsigned NewBaseReg = MRI->createGenericVirtualRegister(PtrTy);
        unsigned OffsetReg =
            getOrCreateVReg(*ConstantInt::get(OffsetIRTy, Offset));
        MIRBuilder.buildGEP(NewBaseReg, BaseReg, OffsetReg);

        BaseReg = NewBaseReg;
        Offset = 0;
      }

      unsigned IdxReg = getOrCreateVReg(*Idx);
      if (MRI->getType(IdxReg) != OffsetTy) {
        unsigned NewIdxReg = MRI->createGenericVirtualRegister(OffsetTy);
        MIRBuilder.buildSExtOrTrunc(NewIdxReg, IdxReg);
        IdxReg = NewIdxReg;
      }

      // N = N + Idx * ElementSize;
      // Avoid doing it for ElementSize of 1.
      unsigned GepOffsetReg;
      if (ElementSize != 1) {
        unsigned ElementSizeReg =
            getOrCreateVReg(*ConstantInt::get(OffsetIRTy, ElementSize));

        GepOffsetReg = MRI->createGenericVirtualRegister(OffsetTy);
        MIRBuilder.buildMul(GepOffsetReg, ElementSizeReg, IdxReg);
      } else
        GepOffsetReg = IdxReg;

      unsigned NewBaseReg = MRI->createGenericVirtualRegister(PtrTy);
      MIRBuilder.buildGEP(NewBaseReg, BaseReg, GepOffsetReg);
      BaseReg = NewBaseReg;
    }
  }

  if (Offset != 0) {
    unsigned OffsetReg = getOrCreateVReg(*ConstantInt::get(OffsetIRTy, Offset));
    MIRBuilder.buildGEP(getOrCreateVReg(U), BaseReg, OffsetReg);
    return true;
  }

  MIRBuilder.buildCopy(getOrCreateVReg(U), BaseReg);
  return true;
}

bool IRTranslator::translateMemfunc(const CallInst &CI,
                                    MachineIRBuilder &MIRBuilder,
                                    unsigned ID) {
  LLT SizeTy = getLLTForType(*CI.getArgOperand(2)->getType(), *DL);
  Type *DstTy = CI.getArgOperand(0)->getType();
  if (cast<PointerType>(DstTy)->getAddressSpace() != 0 ||
      SizeTy.getSizeInBits() != DL->getPointerSizeInBits(0))
    return false;

  SmallVector<CallLowering::ArgInfo, 8> Args;
  for (int i = 0; i < 3; ++i) {
    const auto &Arg = CI.getArgOperand(i);
    Args.emplace_back(getOrCreateVReg(*Arg), Arg->getType());
  }

  const char *Callee;
  switch (ID) {
  case Intrinsic::memmove:
  case Intrinsic::memcpy: {
    Type *SrcTy = CI.getArgOperand(1)->getType();
    if(cast<PointerType>(SrcTy)->getAddressSpace() != 0)
      return false;
    Callee = ID == Intrinsic::memcpy ? "memcpy" : "memmove";
    break;
  }
  case Intrinsic::memset:
    Callee = "memset";
    break;
  default:
    return false;
  }

  return CLI->lowerCall(MIRBuilder, CI.getCallingConv(),
                        MachineOperand::CreateES(Callee),
                        CallLowering::ArgInfo(0, CI.getType()), Args);
}

void IRTranslator::getStackGuard(unsigned DstReg,
                                 MachineIRBuilder &MIRBuilder) {
  const TargetRegisterInfo *TRI = MF->getSubtarget().getRegisterInfo();
  MRI->setRegClass(DstReg, TRI->getPointerRegClass(*MF));
  auto MIB = MIRBuilder.buildInstr(TargetOpcode::LOAD_STACK_GUARD);
  MIB.addDef(DstReg);

  auto &TLI = *MF->getSubtarget().getTargetLowering();
  Value *Global = TLI.getSDagStackGuard(*MF->getFunction().getParent());
  if (!Global)
    return;

  MachinePointerInfo MPInfo(Global);
  auto Flags = MachineMemOperand::MOLoad | MachineMemOperand::MOInvariant |
               MachineMemOperand::MODereferenceable;
  MachineMemOperand *MemRef =
      MF->getMachineMemOperand(MPInfo, Flags, DL->getPointerSizeInBits() / 8,
                               DL->getPointerABIAlignment(0));
  MIB.setMemRefs({MemRef});
}

bool IRTranslator::translateOverflowIntrinsic(const CallInst &CI, unsigned Op,
                                              MachineIRBuilder &MIRBuilder) {
  ArrayRef<unsigned> ResRegs = getOrCreateVRegs(CI);
  MIRBuilder.buildInstr(Op)
      .addDef(ResRegs[0])
      .addDef(ResRegs[1])
      .addUse(getOrCreateVReg(*CI.getOperand(0)))
      .addUse(getOrCreateVReg(*CI.getOperand(1)));

  return true;
}

bool IRTranslator::translateKnownIntrinsic(const CallInst &CI, Intrinsic::ID ID,
                                           MachineIRBuilder &MIRBuilder) {
  switch (ID) {
  default:
    break;
  case Intrinsic::lifetime_start:
  case Intrinsic::lifetime_end:
    // Stack coloring is not enabled in O0 (which we care about now) so we can
    // drop these. Make sure someone notices when we start compiling at higher
    // opts though.
    if (MF->getTarget().getOptLevel() != CodeGenOpt::None)
      return false;
    return true;
  case Intrinsic::dbg_declare: {
    const DbgDeclareInst &DI = cast<DbgDeclareInst>(CI);
    assert(DI.getVariable() && "Missing variable");

    const Value *Address = DI.getAddress();
    if (!Address || isa<UndefValue>(Address)) {
      LLVM_DEBUG(dbgs() << "Dropping debug info for " << DI << "\n");
      return true;
    }

    assert(DI.getVariable()->isValidLocationForIntrinsic(
               MIRBuilder.getDebugLoc()) &&
           "Expected inlined-at fields to agree");
    auto AI = dyn_cast<AllocaInst>(Address);
    if (AI && AI->isStaticAlloca()) {
      // Static allocas are tracked at the MF level, no need for DBG_VALUE
      // instructions (in fact, they get ignored if they *do* exist).
      MF->setVariableDbgInfo(DI.getVariable(), DI.getExpression(),
                             getOrCreateFrameIndex(*AI), DI.getDebugLoc());
    } else {
      // A dbg.declare describes the address of a source variable, so lower it
      // into an indirect DBG_VALUE.
      MIRBuilder.buildIndirectDbgValue(getOrCreateVReg(*Address),
                                       DI.getVariable(), DI.getExpression());
    }
    return true;
  }
  case Intrinsic::dbg_label: {
    const DbgLabelInst &DI = cast<DbgLabelInst>(CI);
    assert(DI.getLabel() && "Missing label");

    assert(DI.getLabel()->isValidLocationForIntrinsic(
               MIRBuilder.getDebugLoc()) &&
           "Expected inlined-at fields to agree");

    MIRBuilder.buildDbgLabel(DI.getLabel());
    return true;
  }
  case Intrinsic::vaend:
    // No target I know of cares about va_end. Certainly no in-tree target
    // does. Simplest intrinsic ever!
    return true;
  case Intrinsic::vastart: {
    auto &TLI = *MF->getSubtarget().getTargetLowering();
    Value *Ptr = CI.getArgOperand(0);
    unsigned ListSize = TLI.getVaListSizeInBits(*DL) / 8;

    MIRBuilder.buildInstr(TargetOpcode::G_VASTART)
        .addUse(getOrCreateVReg(*Ptr))
        .addMemOperand(MF->getMachineMemOperand(
            MachinePointerInfo(Ptr), MachineMemOperand::MOStore, ListSize, 0));
    return true;
  }
  case Intrinsic::dbg_value: {
    // This form of DBG_VALUE is target-independent.
    const DbgValueInst &DI = cast<DbgValueInst>(CI);
    const Value *V = DI.getValue();
    assert(DI.getVariable()->isValidLocationForIntrinsic(
               MIRBuilder.getDebugLoc()) &&
           "Expected inlined-at fields to agree");
    if (!V) {
      // Currently the optimizer can produce this; insert an undef to
      // help debugging.  Probably the optimizer should not do this.
      MIRBuilder.buildIndirectDbgValue(0, DI.getVariable(), DI.getExpression());
    } else if (const auto *CI = dyn_cast<Constant>(V)) {
      MIRBuilder.buildConstDbgValue(*CI, DI.getVariable(), DI.getExpression());
    } else {
      unsigned Reg = getOrCreateVReg(*V);
      // FIXME: This does not handle register-indirect values at offset 0. The
      // direct/indirect thing shouldn't really be handled by something as
      // implicit as reg+noreg vs reg+imm in the first palce, but it seems
      // pretty baked in right now.
      MIRBuilder.buildDirectDbgValue(Reg, DI.getVariable(), DI.getExpression());
    }
    return true;
  }
  case Intrinsic::uadd_with_overflow:
    return translateOverflowIntrinsic(CI, TargetOpcode::G_UADDO, MIRBuilder);
  case Intrinsic::sadd_with_overflow:
    return translateOverflowIntrinsic(CI, TargetOpcode::G_SADDO, MIRBuilder);
  case Intrinsic::usub_with_overflow:
    return translateOverflowIntrinsic(CI, TargetOpcode::G_USUBO, MIRBuilder);
  case Intrinsic::ssub_with_overflow:
    return translateOverflowIntrinsic(CI, TargetOpcode::G_SSUBO, MIRBuilder);
  case Intrinsic::umul_with_overflow:
    return translateOverflowIntrinsic(CI, TargetOpcode::G_UMULO, MIRBuilder);
  case Intrinsic::smul_with_overflow:
    return translateOverflowIntrinsic(CI, TargetOpcode::G_SMULO, MIRBuilder);
  case Intrinsic::pow: {
    auto Pow = MIRBuilder.buildInstr(TargetOpcode::G_FPOW)
        .addDef(getOrCreateVReg(CI))
        .addUse(getOrCreateVReg(*CI.getArgOperand(0)))
        .addUse(getOrCreateVReg(*CI.getArgOperand(1)));
    Pow->copyIRFlags(CI);
    return true;
  }
  case Intrinsic::exp: {
    auto Exp = MIRBuilder.buildInstr(TargetOpcode::G_FEXP)
        .addDef(getOrCreateVReg(CI))
        .addUse(getOrCreateVReg(*CI.getArgOperand(0)));
    Exp->copyIRFlags(CI);
    return true;
  }
  case Intrinsic::exp2: {
    auto Exp2 = MIRBuilder.buildInstr(TargetOpcode::G_FEXP2)
        .addDef(getOrCreateVReg(CI))
        .addUse(getOrCreateVReg(*CI.getArgOperand(0)));
    Exp2->copyIRFlags(CI);
    return true;
  }
  case Intrinsic::log: {
    auto Log = MIRBuilder.buildInstr(TargetOpcode::G_FLOG)
        .addDef(getOrCreateVReg(CI))
        .addUse(getOrCreateVReg(*CI.getArgOperand(0)));
    Log->copyIRFlags(CI);
    return true;
  }
  case Intrinsic::log2: {
    auto Log2 = MIRBuilder.buildInstr(TargetOpcode::G_FLOG2)
        .addDef(getOrCreateVReg(CI))
        .addUse(getOrCreateVReg(*CI.getArgOperand(0)));
    Log2->copyIRFlags(CI);
    return true;
  }
  case Intrinsic::log10: {
    auto Log10 = MIRBuilder.buildInstr(TargetOpcode::G_FLOG10)
        .addDef(getOrCreateVReg(CI))
        .addUse(getOrCreateVReg(*CI.getArgOperand(0)));
    Log10->copyIRFlags(CI);
    return true;
  }
  case Intrinsic::fabs: {
    auto Fabs = MIRBuilder.buildInstr(TargetOpcode::G_FABS)
        .addDef(getOrCreateVReg(CI))
        .addUse(getOrCreateVReg(*CI.getArgOperand(0)));
    Fabs->copyIRFlags(CI);
    return true;
  }
  case Intrinsic::trunc:
    MIRBuilder.buildInstr(TargetOpcode::G_INTRINSIC_TRUNC)
        .addDef(getOrCreateVReg(CI))
        .addUse(getOrCreateVReg(*CI.getArgOperand(0)));
    return true;
  case Intrinsic::round:
    MIRBuilder.buildInstr(TargetOpcode::G_INTRINSIC_ROUND)
        .addDef(getOrCreateVReg(CI))
        .addUse(getOrCreateVReg(*CI.getArgOperand(0)));
    return true;
  case Intrinsic::fma: {
    auto FMA = MIRBuilder.buildInstr(TargetOpcode::G_FMA)
        .addDef(getOrCreateVReg(CI))
        .addUse(getOrCreateVReg(*CI.getArgOperand(0)))
        .addUse(getOrCreateVReg(*CI.getArgOperand(1)))
        .addUse(getOrCreateVReg(*CI.getArgOperand(2)));
    FMA->copyIRFlags(CI);
    return true;
  }
  case Intrinsic::fmuladd: {
    const TargetMachine &TM = MF->getTarget();
    const TargetLowering &TLI = *MF->getSubtarget().getTargetLowering();
    unsigned Dst = getOrCreateVReg(CI);
    unsigned Op0 = getOrCreateVReg(*CI.getArgOperand(0));
    unsigned Op1 = getOrCreateVReg(*CI.getArgOperand(1));
    unsigned Op2 = getOrCreateVReg(*CI.getArgOperand(2));
    if (TM.Options.AllowFPOpFusion != FPOpFusion::Strict &&
        TLI.isFMAFasterThanFMulAndFAdd(TLI.getValueType(*DL, CI.getType()))) {
      // TODO: Revisit this to see if we should move this part of the
      // lowering to the combiner.
      auto FMA =  MIRBuilder.buildInstr(TargetOpcode::G_FMA, {Dst}, {Op0, Op1, Op2});
      FMA->copyIRFlags(CI);
    } else {
      LLT Ty = getLLTForType(*CI.getType(), *DL);
      auto FMul = MIRBuilder.buildInstr(TargetOpcode::G_FMUL, {Ty}, {Op0, Op1});
      FMul->copyIRFlags(CI);
      auto FAdd =  MIRBuilder.buildInstr(TargetOpcode::G_FADD, {Dst}, {FMul, Op2});
      FAdd->copyIRFlags(CI);
    }
    return true;
  }
  case Intrinsic::memcpy:
  case Intrinsic::memmove:
  case Intrinsic::memset:
    return translateMemfunc(CI, MIRBuilder, ID);
  case Intrinsic::eh_typeid_for: {
    GlobalValue *GV = ExtractTypeInfo(CI.getArgOperand(0));
    unsigned Reg = getOrCreateVReg(CI);
    unsigned TypeID = MF->getTypeIDFor(GV);
    MIRBuilder.buildConstant(Reg, TypeID);
    return true;
  }
  case Intrinsic::objectsize: {
    // If we don't know by now, we're never going to know.
    const ConstantInt *Min = cast<ConstantInt>(CI.getArgOperand(1));

    MIRBuilder.buildConstant(getOrCreateVReg(CI), Min->isZero() ? -1ULL : 0);
    return true;
  }
  case Intrinsic::is_constant:
    // If this wasn't constant-folded away by now, then it's not a
    // constant.
    MIRBuilder.buildConstant(getOrCreateVReg(CI), 0);
    return true;
  case Intrinsic::stackguard:
    getStackGuard(getOrCreateVReg(CI), MIRBuilder);
    return true;
  case Intrinsic::stackprotector: {
    LLT PtrTy = getLLTForType(*CI.getArgOperand(0)->getType(), *DL);
    unsigned GuardVal = MRI->createGenericVirtualRegister(PtrTy);
    getStackGuard(GuardVal, MIRBuilder);

    AllocaInst *Slot = cast<AllocaInst>(CI.getArgOperand(1));
    int FI = getOrCreateFrameIndex(*Slot);
    MF->getFrameInfo().setStackProtectorIndex(FI);

    MIRBuilder.buildStore(
        GuardVal, getOrCreateVReg(*Slot),
        *MF->getMachineMemOperand(MachinePointerInfo::getFixedStack(*MF, FI),
                                  MachineMemOperand::MOStore |
                                      MachineMemOperand::MOVolatile,
                                  PtrTy.getSizeInBits() / 8, 8));
    return true;
  }
  case Intrinsic::cttz:
  case Intrinsic::ctlz: {
    ConstantInt *Cst = cast<ConstantInt>(CI.getArgOperand(1));
    bool isTrailing = ID == Intrinsic::cttz;
    unsigned Opcode = isTrailing
                          ? Cst->isZero() ? TargetOpcode::G_CTTZ
                                          : TargetOpcode::G_CTTZ_ZERO_UNDEF
                          : Cst->isZero() ? TargetOpcode::G_CTLZ
                                          : TargetOpcode::G_CTLZ_ZERO_UNDEF;
    MIRBuilder.buildInstr(Opcode)
        .addDef(getOrCreateVReg(CI))
        .addUse(getOrCreateVReg(*CI.getArgOperand(0)));
    return true;
  }
  case Intrinsic::ctpop: {
    MIRBuilder.buildInstr(TargetOpcode::G_CTPOP)
        .addDef(getOrCreateVReg(CI))
        .addUse(getOrCreateVReg(*CI.getArgOperand(0)));
    return true;
  }
  case Intrinsic::invariant_start: {
    LLT PtrTy = getLLTForType(*CI.getArgOperand(0)->getType(), *DL);
    unsigned Undef = MRI->createGenericVirtualRegister(PtrTy);
    MIRBuilder.buildUndef(Undef);
    return true;
  }
  case Intrinsic::invariant_end:
    return true;
  case Intrinsic::ceil:
    MIRBuilder.buildInstr(TargetOpcode::G_FCEIL)
        .addDef(getOrCreateVReg(CI))
        .addUse(getOrCreateVReg(*CI.getArgOperand(0)));
    return true;
  }
  return false;
}

bool IRTranslator::translateInlineAsm(const CallInst &CI,
                                      MachineIRBuilder &MIRBuilder) {
  const InlineAsm &IA = cast<InlineAsm>(*CI.getCalledValue());
  if (!IA.getConstraintString().empty())
    return false;

  unsigned ExtraInfo = 0;
  if (IA.hasSideEffects())
    ExtraInfo |= InlineAsm::Extra_HasSideEffects;
  if (IA.getDialect() == InlineAsm::AD_Intel)
    ExtraInfo |= InlineAsm::Extra_AsmDialect;

  MIRBuilder.buildInstr(TargetOpcode::INLINEASM)
    .addExternalSymbol(IA.getAsmString().c_str())
    .addImm(ExtraInfo);

  return true;
}

unsigned IRTranslator::packRegs(const Value &V,
                                  MachineIRBuilder &MIRBuilder) {
  ArrayRef<unsigned> Regs = getOrCreateVRegs(V);
  ArrayRef<uint64_t> Offsets = *VMap.getOffsets(V);
  LLT BigTy = getLLTForType(*V.getType(), *DL);

  if (Regs.size() == 1)
    return Regs[0];

  unsigned Dst = MRI->createGenericVirtualRegister(BigTy);
  MIRBuilder.buildUndef(Dst);
  for (unsigned i = 0; i < Regs.size(); ++i) {
    unsigned NewDst = MRI->createGenericVirtualRegister(BigTy);
    MIRBuilder.buildInsert(NewDst, Dst, Regs[i], Offsets[i]);
    Dst = NewDst;
  }
  return Dst;
}

void IRTranslator::unpackRegs(const Value &V, unsigned Src,
                                MachineIRBuilder &MIRBuilder) {
  ArrayRef<unsigned> Regs = getOrCreateVRegs(V);
  ArrayRef<uint64_t> Offsets = *VMap.getOffsets(V);

  for (unsigned i = 0; i < Regs.size(); ++i)
    MIRBuilder.buildExtract(Regs[i], Src, Offsets[i]);
}

bool IRTranslator::translateCall(const User &U, MachineIRBuilder &MIRBuilder) {
  const CallInst &CI = cast<CallInst>(U);
  auto TII = MF->getTarget().getIntrinsicInfo();
  const Function *F = CI.getCalledFunction();

  // FIXME: support Windows dllimport function calls.
  if (F && F->hasDLLImportStorageClass())
    return false;

  if (CI.isInlineAsm())
    return translateInlineAsm(CI, MIRBuilder);

  Intrinsic::ID ID = Intrinsic::not_intrinsic;
  if (F && F->isIntrinsic()) {
    ID = F->getIntrinsicID();
    if (TII && ID == Intrinsic::not_intrinsic)
      ID = static_cast<Intrinsic::ID>(TII->getIntrinsicID(F));
  }

  bool IsSplitType = valueIsSplit(CI);
  if (!F || !F->isIntrinsic() || ID == Intrinsic::not_intrinsic) {
    unsigned Res = IsSplitType ? MRI->createGenericVirtualRegister(
                                     getLLTForType(*CI.getType(), *DL))
                               : getOrCreateVReg(CI);

    SmallVector<unsigned, 8> Args;
    for (auto &Arg: CI.arg_operands())
      Args.push_back(packRegs(*Arg, MIRBuilder));

    MF->getFrameInfo().setHasCalls(true);
    bool Success = CLI->lowerCall(MIRBuilder, &CI, Res, Args, [&]() {
      return getOrCreateVReg(*CI.getCalledValue());
    });

    if (IsSplitType)
      unpackRegs(CI, Res, MIRBuilder);
    return Success;
  }

  assert(ID != Intrinsic::not_intrinsic && "unknown intrinsic");

  if (translateKnownIntrinsic(CI, ID, MIRBuilder))
    return true;

  unsigned Res = 0;
  if (!CI.getType()->isVoidTy()) {
    if (IsSplitType)
      Res =
          MRI->createGenericVirtualRegister(getLLTForType(*CI.getType(), *DL));
    else
      Res = getOrCreateVReg(CI);
  }
  MachineInstrBuilder MIB =
      MIRBuilder.buildIntrinsic(ID, Res, !CI.doesNotAccessMemory());

  for (auto &Arg : CI.arg_operands()) {
    // Some intrinsics take metadata parameters. Reject them.
    if (isa<MetadataAsValue>(Arg))
      return false;
    MIB.addUse(packRegs(*Arg, MIRBuilder));
  }

  if (IsSplitType)
    unpackRegs(CI, Res, MIRBuilder);

  // Add a MachineMemOperand if it is a target mem intrinsic.
  const TargetLowering &TLI = *MF->getSubtarget().getTargetLowering();
  TargetLowering::IntrinsicInfo Info;
  // TODO: Add a GlobalISel version of getTgtMemIntrinsic.
  if (TLI.getTgtMemIntrinsic(Info, CI, *MF, ID)) {
    uint64_t Size = Info.memVT.getStoreSize();
    MIB.addMemOperand(MF->getMachineMemOperand(MachinePointerInfo(Info.ptrVal),
                                               Info.flags, Size, Info.align));
  }

  return true;
}

bool IRTranslator::translateInvoke(const User &U,
                                   MachineIRBuilder &MIRBuilder) {
  const InvokeInst &I = cast<InvokeInst>(U);
  MCContext &Context = MF->getContext();

  const BasicBlock *ReturnBB = I.getSuccessor(0);
  const BasicBlock *EHPadBB = I.getSuccessor(1);

  const Value *Callee = I.getCalledValue();
  const Function *Fn = dyn_cast<Function>(Callee);
  if (isa<InlineAsm>(Callee))
    return false;

  // FIXME: support invoking patchpoint and statepoint intrinsics.
  if (Fn && Fn->isIntrinsic())
    return false;

  // FIXME: support whatever these are.
  if (I.countOperandBundlesOfType(LLVMContext::OB_deopt))
    return false;

  // FIXME: support Windows exception handling.
  if (!isa<LandingPadInst>(EHPadBB->front()))
    return false;

  // Emit the actual call, bracketed by EH_LABELs so that the MF knows about
  // the region covered by the try.
  MCSymbol *BeginSymbol = Context.createTempSymbol();
  MIRBuilder.buildInstr(TargetOpcode::EH_LABEL).addSym(BeginSymbol);

  unsigned Res =
        MRI->createGenericVirtualRegister(getLLTForType(*I.getType(), *DL));
  SmallVector<unsigned, 8> Args;
  for (auto &Arg: I.arg_operands())
    Args.push_back(packRegs(*Arg, MIRBuilder));

  if (!CLI->lowerCall(MIRBuilder, &I, Res, Args,
                      [&]() { return getOrCreateVReg(*I.getCalledValue()); }))
    return false;

  unpackRegs(I, Res, MIRBuilder);

  MCSymbol *EndSymbol = Context.createTempSymbol();
  MIRBuilder.buildInstr(TargetOpcode::EH_LABEL).addSym(EndSymbol);

  // FIXME: track probabilities.
  MachineBasicBlock &EHPadMBB = getMBB(*EHPadBB),
                    &ReturnMBB = getMBB(*ReturnBB);
  MF->addInvoke(&EHPadMBB, BeginSymbol, EndSymbol);
  MIRBuilder.getMBB().addSuccessor(&ReturnMBB);
  MIRBuilder.getMBB().addSuccessor(&EHPadMBB);
  MIRBuilder.buildBr(ReturnMBB);

  return true;
}

bool IRTranslator::translateLandingPad(const User &U,
                                       MachineIRBuilder &MIRBuilder) {
  const LandingPadInst &LP = cast<LandingPadInst>(U);

  MachineBasicBlock &MBB = MIRBuilder.getMBB();

  MBB.setIsEHPad();

  // If there aren't registers to copy the values into (e.g., during SjLj
  // exceptions), then don't bother.
  auto &TLI = *MF->getSubtarget().getTargetLowering();
  const Constant *PersonalityFn = MF->getFunction().getPersonalityFn();
  if (TLI.getExceptionPointerRegister(PersonalityFn) == 0 &&
      TLI.getExceptionSelectorRegister(PersonalityFn) == 0)
    return true;

  // If landingpad's return type is token type, we don't create DAG nodes
  // for its exception pointer and selector value. The extraction of exception
  // pointer or selector value from token type landingpads is not currently
  // supported.
  if (LP.getType()->isTokenTy())
    return true;

  // Add a label to mark the beginning of the landing pad.  Deletion of the
  // landing pad can thus be detected via the MachineModuleInfo.
  MIRBuilder.buildInstr(TargetOpcode::EH_LABEL)
    .addSym(MF->addLandingPad(&MBB));

  LLT Ty = getLLTForType(*LP.getType(), *DL);
  unsigned Undef = MRI->createGenericVirtualRegister(Ty);
  MIRBuilder.buildUndef(Undef);

  SmallVector<LLT, 2> Tys;
  for (Type *Ty : cast<StructType>(LP.getType())->elements())
    Tys.push_back(getLLTForType(*Ty, *DL));
  assert(Tys.size() == 2 && "Only two-valued landingpads are supported");

  // Mark exception register as live in.
  unsigned ExceptionReg = TLI.getExceptionPointerRegister(PersonalityFn);
  if (!ExceptionReg)
    return false;

  MBB.addLiveIn(ExceptionReg);
  ArrayRef<unsigned> ResRegs = getOrCreateVRegs(LP);
  MIRBuilder.buildCopy(ResRegs[0], ExceptionReg);

  unsigned SelectorReg = TLI.getExceptionSelectorRegister(PersonalityFn);
  if (!SelectorReg)
    return false;

  MBB.addLiveIn(SelectorReg);
  unsigned PtrVReg = MRI->createGenericVirtualRegister(Tys[0]);
  MIRBuilder.buildCopy(PtrVReg, SelectorReg);
  MIRBuilder.buildCast(ResRegs[1], PtrVReg);

  return true;
}

bool IRTranslator::translateAlloca(const User &U,
                                   MachineIRBuilder &MIRBuilder) {
  auto &AI = cast<AllocaInst>(U);

  if (AI.isSwiftError())
    return false;

  if (AI.isStaticAlloca()) {
    unsigned Res = getOrCreateVReg(AI);
    int FI = getOrCreateFrameIndex(AI);
    MIRBuilder.buildFrameIndex(Res, FI);
    return true;
  }

  // FIXME: support stack probing for Windows.
  if (MF->getTarget().getTargetTriple().isOSWindows())
    return false;

  // Now we're in the harder dynamic case.
  Type *Ty = AI.getAllocatedType();
  unsigned Align =
      std::max((unsigned)DL->getPrefTypeAlignment(Ty), AI.getAlignment());

  unsigned NumElts = getOrCreateVReg(*AI.getArraySize());

  Type *IntPtrIRTy = DL->getIntPtrType(AI.getType());
  LLT IntPtrTy = getLLTForType(*IntPtrIRTy, *DL);
  if (MRI->getType(NumElts) != IntPtrTy) {
    unsigned ExtElts = MRI->createGenericVirtualRegister(IntPtrTy);
    MIRBuilder.buildZExtOrTrunc(ExtElts, NumElts);
    NumElts = ExtElts;
  }

  unsigned AllocSize = MRI->createGenericVirtualRegister(IntPtrTy);
  unsigned TySize =
      getOrCreateVReg(*ConstantInt::get(IntPtrIRTy, -DL->getTypeAllocSize(Ty)));
  MIRBuilder.buildMul(AllocSize, NumElts, TySize);

  LLT PtrTy = getLLTForType(*AI.getType(), *DL);
  auto &TLI = *MF->getSubtarget().getTargetLowering();
  unsigned SPReg = TLI.getStackPointerRegisterToSaveRestore();

  unsigned SPTmp = MRI->createGenericVirtualRegister(PtrTy);
  MIRBuilder.buildCopy(SPTmp, SPReg);

  unsigned AllocTmp = MRI->createGenericVirtualRegister(PtrTy);
  MIRBuilder.buildGEP(AllocTmp, SPTmp, AllocSize);

  // Handle alignment. We have to realign if the allocation granule was smaller
  // than stack alignment, or the specific alloca requires more than stack
  // alignment.
  unsigned StackAlign =
      MF->getSubtarget().getFrameLowering()->getStackAlignment();
  Align = std::max(Align, StackAlign);
  if (Align > StackAlign || DL->getTypeAllocSize(Ty) % StackAlign != 0) {
    // Round the size of the allocation up to the stack alignment size
    // by add SA-1 to the size. This doesn't overflow because we're computing
    // an address inside an alloca.
    unsigned AlignedAlloc = MRI->createGenericVirtualRegister(PtrTy);
    MIRBuilder.buildPtrMask(AlignedAlloc, AllocTmp, Log2_32(Align));
    AllocTmp = AlignedAlloc;
  }

  MIRBuilder.buildCopy(SPReg, AllocTmp);
  MIRBuilder.buildCopy(getOrCreateVReg(AI), AllocTmp);

  MF->getFrameInfo().CreateVariableSizedObject(Align ? Align : 1, &AI);
  assert(MF->getFrameInfo().hasVarSizedObjects());
  return true;
}

bool IRTranslator::translateVAArg(const User &U, MachineIRBuilder &MIRBuilder) {
  // FIXME: We may need more info about the type. Because of how LLT works,
  // we're completely discarding the i64/double distinction here (amongst
  // others). Fortunately the ABIs I know of where that matters don't use va_arg
  // anyway but that's not guaranteed.
  MIRBuilder.buildInstr(TargetOpcode::G_VAARG)
    .addDef(getOrCreateVReg(U))
    .addUse(getOrCreateVReg(*U.getOperand(0)))
    .addImm(DL->getABITypeAlignment(U.getType()));
  return true;
}

bool IRTranslator::translateInsertElement(const User &U,
                                          MachineIRBuilder &MIRBuilder) {
  // If it is a <1 x Ty> vector, use the scalar as it is
  // not a legal vector type in LLT.
  if (U.getType()->getVectorNumElements() == 1) {
    unsigned Elt = getOrCreateVReg(*U.getOperand(1));
    auto &Regs = *VMap.getVRegs(U);
    if (Regs.empty()) {
      Regs.push_back(Elt);
      VMap.getOffsets(U)->push_back(0);
    } else {
      MIRBuilder.buildCopy(Regs[0], Elt);
    }
    return true;
  }

  unsigned Res = getOrCreateVReg(U);
  unsigned Val = getOrCreateVReg(*U.getOperand(0));
  unsigned Elt = getOrCreateVReg(*U.getOperand(1));
  unsigned Idx = getOrCreateVReg(*U.getOperand(2));
  MIRBuilder.buildInsertVectorElement(Res, Val, Elt, Idx);
  return true;
}

bool IRTranslator::translateExtractElement(const User &U,
                                           MachineIRBuilder &MIRBuilder) {
  // If it is a <1 x Ty> vector, use the scalar as it is
  // not a legal vector type in LLT.
  if (U.getOperand(0)->getType()->getVectorNumElements() == 1) {
    unsigned Elt = getOrCreateVReg(*U.getOperand(0));
    auto &Regs = *VMap.getVRegs(U);
    if (Regs.empty()) {
      Regs.push_back(Elt);
      VMap.getOffsets(U)->push_back(0);
    } else {
      MIRBuilder.buildCopy(Regs[0], Elt);
    }
    return true;
  }
  unsigned Res = getOrCreateVReg(U);
  unsigned Val = getOrCreateVReg(*U.getOperand(0));
  const auto &TLI = *MF->getSubtarget().getTargetLowering();
  unsigned PreferredVecIdxWidth = TLI.getVectorIdxTy(*DL).getSizeInBits();
  unsigned Idx = 0;
  if (auto *CI = dyn_cast<ConstantInt>(U.getOperand(1))) {
    if (CI->getBitWidth() != PreferredVecIdxWidth) {
      APInt NewIdx = CI->getValue().sextOrTrunc(PreferredVecIdxWidth);
      auto *NewIdxCI = ConstantInt::get(CI->getContext(), NewIdx);
      Idx = getOrCreateVReg(*NewIdxCI);
    }
  }
  if (!Idx)
    Idx = getOrCreateVReg(*U.getOperand(1));
  if (MRI->getType(Idx).getSizeInBits() != PreferredVecIdxWidth) {
    const LLT &VecIdxTy = LLT::scalar(PreferredVecIdxWidth);
    Idx = MIRBuilder.buildSExtOrTrunc(VecIdxTy, Idx)->getOperand(0).getReg();
  }
  MIRBuilder.buildExtractVectorElement(Res, Val, Idx);
  return true;
}

bool IRTranslator::translateShuffleVector(const User &U,
                                          MachineIRBuilder &MIRBuilder) {
  MIRBuilder.buildInstr(TargetOpcode::G_SHUFFLE_VECTOR)
      .addDef(getOrCreateVReg(U))
      .addUse(getOrCreateVReg(*U.getOperand(0)))
      .addUse(getOrCreateVReg(*U.getOperand(1)))
      .addUse(getOrCreateVReg(*U.getOperand(2)));
  return true;
}

bool IRTranslator::translatePHI(const User &U, MachineIRBuilder &MIRBuilder) {
  const PHINode &PI = cast<PHINode>(U);

  SmallVector<MachineInstr *, 4> Insts;
  for (auto Reg : getOrCreateVRegs(PI)) {
    auto MIB = MIRBuilder.buildInstr(TargetOpcode::G_PHI, {Reg}, {});
    Insts.push_back(MIB.getInstr());
  }

  PendingPHIs.emplace_back(&PI, std::move(Insts));
  return true;
}

bool IRTranslator::translateAtomicCmpXchg(const User &U,
                                          MachineIRBuilder &MIRBuilder) {
  const AtomicCmpXchgInst &I = cast<AtomicCmpXchgInst>(U);

  if (I.isWeak())
    return false;

  auto Flags = I.isVolatile() ? MachineMemOperand::MOVolatile
                              : MachineMemOperand::MONone;
  Flags |= MachineMemOperand::MOLoad | MachineMemOperand::MOStore;

  Type *ResType = I.getType();
  Type *ValType = ResType->Type::getStructElementType(0);

  auto Res = getOrCreateVRegs(I);
  unsigned OldValRes = Res[0];
  unsigned SuccessRes = Res[1];
  unsigned Addr = getOrCreateVReg(*I.getPointerOperand());
  unsigned Cmp = getOrCreateVReg(*I.getCompareOperand());
  unsigned NewVal = getOrCreateVReg(*I.getNewValOperand());

  MIRBuilder.buildAtomicCmpXchgWithSuccess(
      OldValRes, SuccessRes, Addr, Cmp, NewVal,
      *MF->getMachineMemOperand(MachinePointerInfo(I.getPointerOperand()),
                                Flags, DL->getTypeStoreSize(ValType),
                                getMemOpAlignment(I), AAMDNodes(), nullptr,
                                I.getSyncScopeID(), I.getSuccessOrdering(),
                                I.getFailureOrdering()));
  return true;
}

bool IRTranslator::translateAtomicRMW(const User &U,
                                      MachineIRBuilder &MIRBuilder) {
  const AtomicRMWInst &I = cast<AtomicRMWInst>(U);

  auto Flags = I.isVolatile() ? MachineMemOperand::MOVolatile
                              : MachineMemOperand::MONone;
  Flags |= MachineMemOperand::MOLoad | MachineMemOperand::MOStore;

  Type *ResType = I.getType();

  unsigned Res = getOrCreateVReg(I);
  unsigned Addr = getOrCreateVReg(*I.getPointerOperand());
  unsigned Val = getOrCreateVReg(*I.getValOperand());

  unsigned Opcode = 0;
  switch (I.getOperation()) {
  default:
    llvm_unreachable("Unknown atomicrmw op");
    return false;
  case AtomicRMWInst::Xchg:
    Opcode = TargetOpcode::G_ATOMICRMW_XCHG;
    break;
  case AtomicRMWInst::Add:
    Opcode = TargetOpcode::G_ATOMICRMW_ADD;
    break;
  case AtomicRMWInst::Sub:
    Opcode = TargetOpcode::G_ATOMICRMW_SUB;
    break;
  case AtomicRMWInst::And:
    Opcode = TargetOpcode::G_ATOMICRMW_AND;
    break;
  case AtomicRMWInst::Nand:
    Opcode = TargetOpcode::G_ATOMICRMW_NAND;
    break;
  case AtomicRMWInst::Or:
    Opcode = TargetOpcode::G_ATOMICRMW_OR;
    break;
  case AtomicRMWInst::Xor:
    Opcode = TargetOpcode::G_ATOMICRMW_XOR;
    break;
  case AtomicRMWInst::Max:
    Opcode = TargetOpcode::G_ATOMICRMW_MAX;
    break;
  case AtomicRMWInst::Min:
    Opcode = TargetOpcode::G_ATOMICRMW_MIN;
    break;
  case AtomicRMWInst::UMax:
    Opcode = TargetOpcode::G_ATOMICRMW_UMAX;
    break;
  case AtomicRMWInst::UMin:
    Opcode = TargetOpcode::G_ATOMICRMW_UMIN;
    break;
  }

  MIRBuilder.buildAtomicRMW(
      Opcode, Res, Addr, Val,
      *MF->getMachineMemOperand(MachinePointerInfo(I.getPointerOperand()),
                                Flags, DL->getTypeStoreSize(ResType),
                                getMemOpAlignment(I), AAMDNodes(), nullptr,
                                I.getSyncScopeID(), I.getOrdering()));
  return true;
}

void IRTranslator::finishPendingPhis() {
#ifndef NDEBUG
  DILocationVerifier Verifier;
  GISelObserverWrapper WrapperObserver(&Verifier);
  RAIIDelegateInstaller DelInstall(*MF, &WrapperObserver);
#endif // ifndef NDEBUG
  for (auto &Phi : PendingPHIs) {
    const PHINode *PI = Phi.first;
    ArrayRef<MachineInstr *> ComponentPHIs = Phi.second;
    EntryBuilder->setDebugLoc(PI->getDebugLoc());
#ifndef NDEBUG
    Verifier.setCurrentInst(PI);
#endif // ifndef NDEBUG

    // All MachineBasicBlocks exist, add them to the PHI. We assume IRTranslator
    // won't create extra control flow here, otherwise we need to find the
    // dominating predecessor here (or perhaps force the weirder IRTranslators
    // to provide a simple boundary).
    SmallSet<const BasicBlock *, 4> HandledPreds;

    for (unsigned i = 0; i < PI->getNumIncomingValues(); ++i) {
      auto IRPred = PI->getIncomingBlock(i);
      if (HandledPreds.count(IRPred))
        continue;

      HandledPreds.insert(IRPred);
      ArrayRef<unsigned> ValRegs = getOrCreateVRegs(*PI->getIncomingValue(i));
      for (auto Pred : getMachinePredBBs({IRPred, PI->getParent()})) {
        assert(Pred->isSuccessor(ComponentPHIs[0]->getParent()) &&
               "incorrect CFG at MachineBasicBlock level");
        for (unsigned j = 0; j < ValRegs.size(); ++j) {
          MachineInstrBuilder MIB(*MF, ComponentPHIs[j]);
          MIB.addUse(ValRegs[j]);
          MIB.addMBB(Pred);
        }
      }
    }
  }
}

bool IRTranslator::valueIsSplit(const Value &V,
                                SmallVectorImpl<uint64_t> *Offsets) {
  SmallVector<LLT, 4> SplitTys;
  if (Offsets && !Offsets->empty())
    Offsets->clear();
  computeValueLLTs(*DL, *V.getType(), SplitTys, Offsets);
  return SplitTys.size() > 1;
}

bool IRTranslator::translate(const Instruction &Inst) {
  CurBuilder->setDebugLoc(Inst.getDebugLoc());
  EntryBuilder->setDebugLoc(Inst.getDebugLoc());
  switch(Inst.getOpcode()) {
#define HANDLE_INST(NUM, OPCODE, CLASS)                                        \
  case Instruction::OPCODE:                                                    \
    return translate##OPCODE(Inst, *CurBuilder.get());
#include "llvm/IR/Instruction.def"
  default:
    return false;
  }
}

bool IRTranslator::translate(const Constant &C, unsigned Reg) {
  if (auto CI = dyn_cast<ConstantInt>(&C))
    EntryBuilder->buildConstant(Reg, *CI);
  else if (auto CF = dyn_cast<ConstantFP>(&C))
    EntryBuilder->buildFConstant(Reg, *CF);
  else if (isa<UndefValue>(C))
    EntryBuilder->buildUndef(Reg);
  else if (isa<ConstantPointerNull>(C)) {
    // As we are trying to build a constant val of 0 into a pointer,
    // insert a cast to make them correct with respect to types.
    unsigned NullSize = DL->getTypeSizeInBits(C.getType());
    auto *ZeroTy = Type::getIntNTy(C.getContext(), NullSize);
    auto *ZeroVal = ConstantInt::get(ZeroTy, 0);
    unsigned ZeroReg = getOrCreateVReg(*ZeroVal);
    EntryBuilder->buildCast(Reg, ZeroReg);
  } else if (auto GV = dyn_cast<GlobalValue>(&C))
    EntryBuilder->buildGlobalValue(Reg, GV);
  else if (auto CAZ = dyn_cast<ConstantAggregateZero>(&C)) {
    if (!CAZ->getType()->isVectorTy())
      return false;
    // Return the scalar if it is a <1 x Ty> vector.
    if (CAZ->getNumElements() == 1)
      return translate(*CAZ->getElementValue(0u), Reg);
    SmallVector<unsigned, 4> Ops;
    for (unsigned i = 0; i < CAZ->getNumElements(); ++i) {
      Constant &Elt = *CAZ->getElementValue(i);
      Ops.push_back(getOrCreateVReg(Elt));
    }
    EntryBuilder->buildBuildVector(Reg, Ops);
  } else if (auto CV = dyn_cast<ConstantDataVector>(&C)) {
    // Return the scalar if it is a <1 x Ty> vector.
    if (CV->getNumElements() == 1)
      return translate(*CV->getElementAsConstant(0), Reg);
    SmallVector<unsigned, 4> Ops;
    for (unsigned i = 0; i < CV->getNumElements(); ++i) {
      Constant &Elt = *CV->getElementAsConstant(i);
      Ops.push_back(getOrCreateVReg(Elt));
    }
    EntryBuilder->buildBuildVector(Reg, Ops);
  } else if (auto CE = dyn_cast<ConstantExpr>(&C)) {
    switch(CE->getOpcode()) {
#define HANDLE_INST(NUM, OPCODE, CLASS)                                        \
  case Instruction::OPCODE:                                                    \
    return translate##OPCODE(*CE, *EntryBuilder.get());
#include "llvm/IR/Instruction.def"
    default:
      return false;
    }
  } else if (auto CV = dyn_cast<ConstantVector>(&C)) {
    if (CV->getNumOperands() == 1)
      return translate(*CV->getOperand(0), Reg);
    SmallVector<unsigned, 4> Ops;
    for (unsigned i = 0; i < CV->getNumOperands(); ++i) {
      Ops.push_back(getOrCreateVReg(*CV->getOperand(i)));
    }
    EntryBuilder->buildBuildVector(Reg, Ops);
  } else if (auto *BA = dyn_cast<BlockAddress>(&C)) {
    EntryBuilder->buildBlockAddress(Reg, BA);
  } else
    return false;

  return true;
}

void IRTranslator::finalizeFunction() {
  // Release the memory used by the different maps we
  // needed during the translation.
  PendingPHIs.clear();
  VMap.reset();
  FrameIndices.clear();
  MachinePreds.clear();
  // MachineIRBuilder::DebugLoc can outlive the DILocation it holds. Clear it
  // to avoid accessing free’d memory (in runOnMachineFunction) and to avoid
  // destroying it twice (in ~IRTranslator() and ~LLVMContext())
  EntryBuilder.reset();
  CurBuilder.reset();
}

bool IRTranslator::runOnMachineFunction(MachineFunction &CurMF) {
  MF = &CurMF;
  const Function &F = MF->getFunction();
  if (F.empty())
    return false;
  GISelCSEAnalysisWrapper &Wrapper =
      getAnalysis<GISelCSEAnalysisWrapperPass>().getCSEWrapper();
  // Set the CSEConfig and run the analysis.
  GISelCSEInfo *CSEInfo = nullptr;
  TPC = &getAnalysis<TargetPassConfig>();
  bool IsO0 = TPC->getOptLevel() == CodeGenOpt::Level::None;
  // Disable CSE for O0.
  bool EnableCSE = !IsO0 && EnableCSEInIRTranslator;
  if (EnableCSE) {
    EntryBuilder = make_unique<CSEMIRBuilder>(CurMF);
    std::unique_ptr<CSEConfig> Config = make_unique<CSEConfig>();
    CSEInfo = &Wrapper.get(std::move(Config));
    EntryBuilder->setCSEInfo(CSEInfo);
    CurBuilder = make_unique<CSEMIRBuilder>(CurMF);
    CurBuilder->setCSEInfo(CSEInfo);
  } else {
    EntryBuilder = make_unique<MachineIRBuilder>();
    CurBuilder = make_unique<MachineIRBuilder>();
  }
  CLI = MF->getSubtarget().getCallLowering();
  CurBuilder->setMF(*MF);
  EntryBuilder->setMF(*MF);
  MRI = &MF->getRegInfo();
  DL = &F.getParent()->getDataLayout();
  ORE = llvm::make_unique<OptimizationRemarkEmitter>(&F);

  assert(PendingPHIs.empty() && "stale PHIs");

  if (!DL->isLittleEndian()) {
    // Currently we don't properly handle big endian code.
    OptimizationRemarkMissed R("gisel-irtranslator", "GISelFailure",
                               F.getSubprogram(), &F.getEntryBlock());
    R << "unable to translate in big endian mode";
    reportTranslationError(*MF, *TPC, *ORE, R);
  }

  // Release the per-function state when we return, whether we succeeded or not.
  auto FinalizeOnReturn = make_scope_exit([this]() { finalizeFunction(); });

  // Setup a separate basic-block for the arguments and constants
  MachineBasicBlock *EntryBB = MF->CreateMachineBasicBlock();
  MF->push_back(EntryBB);
  EntryBuilder->setMBB(*EntryBB);

  // Create all blocks, in IR order, to preserve the layout.
  for (const BasicBlock &BB: F) {
    auto *&MBB = BBToMBB[&BB];

    MBB = MF->CreateMachineBasicBlock(&BB);
    MF->push_back(MBB);

    if (BB.hasAddressTaken())
      MBB->setHasAddressTaken();
  }

  // Make our arguments/constants entry block fallthrough to the IR entry block.
  EntryBB->addSuccessor(&getMBB(F.front()));

  // Lower the actual args into this basic block.
  SmallVector<unsigned, 8> VRegArgs;
  for (const Argument &Arg: F.args()) {
    if (DL->getTypeStoreSize(Arg.getType()) == 0)
      continue; // Don't handle zero sized types.
    VRegArgs.push_back(
        MRI->createGenericVirtualRegister(getLLTForType(*Arg.getType(), *DL)));
  }

  // We don't currently support translating swifterror or swiftself functions.
  for (auto &Arg : F.args()) {
    if (Arg.hasSwiftErrorAttr() || Arg.hasSwiftSelfAttr()) {
      OptimizationRemarkMissed R("gisel-irtranslator", "GISelFailure",
                                 F.getSubprogram(), &F.getEntryBlock());
      R << "unable to lower arguments due to swifterror/swiftself: "
        << ore::NV("Prototype", F.getType());
      reportTranslationError(*MF, *TPC, *ORE, R);
      return false;
    }
  }

  if (!CLI->lowerFormalArguments(*EntryBuilder.get(), F, VRegArgs)) {
    OptimizationRemarkMissed R("gisel-irtranslator", "GISelFailure",
                               F.getSubprogram(), &F.getEntryBlock());
    R << "unable to lower arguments: " << ore::NV("Prototype", F.getType());
    reportTranslationError(*MF, *TPC, *ORE, R);
    return false;
  }

  auto ArgIt = F.arg_begin();
  for (auto &VArg : VRegArgs) {
    // If the argument is an unsplit scalar then don't use unpackRegs to avoid
    // creating redundant copies.
    if (!valueIsSplit(*ArgIt, VMap.getOffsets(*ArgIt))) {
      auto &VRegs = *VMap.getVRegs(cast<Value>(*ArgIt));
      assert(VRegs.empty() && "VRegs already populated?");
      VRegs.push_back(VArg);
    } else {
      unpackRegs(*ArgIt, VArg, *EntryBuilder.get());
    }
    ArgIt++;
  }

  // Need to visit defs before uses when translating instructions.
  GISelObserverWrapper WrapperObserver;
  if (EnableCSE && CSEInfo)
    WrapperObserver.addObserver(CSEInfo);
  {
    ReversePostOrderTraversal<const Function *> RPOT(&F);
#ifndef NDEBUG
    DILocationVerifier Verifier;
    WrapperObserver.addObserver(&Verifier);
#endif // ifndef NDEBUG
    RAIIDelegateInstaller DelInstall(*MF, &WrapperObserver);
    for (const BasicBlock *BB : RPOT) {
      MachineBasicBlock &MBB = getMBB(*BB);
      // Set the insertion point of all the following translations to
      // the end of this basic block.
      CurBuilder->setMBB(MBB);

      for (const Instruction &Inst : *BB) {
#ifndef NDEBUG
        Verifier.setCurrentInst(&Inst);
#endif // ifndef NDEBUG
        if (translate(Inst))
          continue;

        OptimizationRemarkMissed R("gisel-irtranslator", "GISelFailure",
                                   Inst.getDebugLoc(), BB);
        R << "unable to translate instruction: " << ore::NV("Opcode", &Inst);

        if (ORE->allowExtraAnalysis("gisel-irtranslator")) {
          std::string InstStrStorage;
          raw_string_ostream InstStr(InstStrStorage);
          InstStr << Inst;

          R << ": '" << InstStr.str() << "'";
        }

        reportTranslationError(*MF, *TPC, *ORE, R);
        return false;
      }
    }
#ifndef NDEBUG
    WrapperObserver.removeObserver(&Verifier);
#endif
  }

  finishPendingPhis();

  // Merge the argument lowering and constants block with its single
  // successor, the LLVM-IR entry block.  We want the basic block to
  // be maximal.
  assert(EntryBB->succ_size() == 1 &&
         "Custom BB used for lowering should have only one successor");
  // Get the successor of the current entry block.
  MachineBasicBlock &NewEntryBB = **EntryBB->succ_begin();
  assert(NewEntryBB.pred_size() == 1 &&
         "LLVM-IR entry block has a predecessor!?");
  // Move all the instruction from the current entry block to the
  // new entry block.
  NewEntryBB.splice(NewEntryBB.begin(), EntryBB, EntryBB->begin(),
                    EntryBB->end());

  // Update the live-in information for the new entry block.
  for (const MachineBasicBlock::RegisterMaskPair &LiveIn : EntryBB->liveins())
    NewEntryBB.addLiveIn(LiveIn);
  NewEntryBB.sortUniqueLiveIns();

  // Get rid of the now empty basic block.
  EntryBB->removeSuccessor(&NewEntryBB);
  MF->remove(EntryBB);
  MF->DeleteMachineBasicBlock(EntryBB);

  assert(&MF->front() == &NewEntryBB &&
         "New entry wasn't next in the list of basic block!");

  // Initialize stack protector information.
  StackProtector &SP = getAnalysis<StackProtector>();
  SP.copyToMachineFrameInfo(MF->getFrameInfo());

  return false;
}
