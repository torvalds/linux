//===- CoroSplit.cpp - Converts a coroutine into a state machine ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// This pass builds the coroutine frame and outlines resume and destroy parts
// of the coroutine into separate functions.
//
// We present a coroutine to an LLVM as an ordinary function with suspension
// points marked up with intrinsics. We let the optimizer party on the coroutine
// as a single function for as long as possible. Shortly before the coroutine is
// eligible to be inlined into its callers, we split up the coroutine into parts
// corresponding to an initial, resume and destroy invocations of the coroutine,
// add them to the current SCC and restart the IPO pipeline to optimize the
// coroutine subfunctions we extracted before proceeding to the caller of the
// coroutine.
//===----------------------------------------------------------------------===//

#include "CoroInstr.h"
#include "CoroInternal.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>

using namespace llvm;

#define DEBUG_TYPE "coro-split"

// Create an entry block for a resume function with a switch that will jump to
// suspend points.
static BasicBlock *createResumeEntryBlock(Function &F, coro::Shape &Shape) {
  LLVMContext &C = F.getContext();

  // resume.entry:
  //  %index.addr = getelementptr inbounds %f.Frame, %f.Frame* %FramePtr, i32 0,
  //  i32 2
  //  % index = load i32, i32* %index.addr
  //  switch i32 %index, label %unreachable [
  //    i32 0, label %resume.0
  //    i32 1, label %resume.1
  //    ...
  //  ]

  auto *NewEntry = BasicBlock::Create(C, "resume.entry", &F);
  auto *UnreachBB = BasicBlock::Create(C, "unreachable", &F);

  IRBuilder<> Builder(NewEntry);
  auto *FramePtr = Shape.FramePtr;
  auto *FrameTy = Shape.FrameTy;
  auto *GepIndex = Builder.CreateConstInBoundsGEP2_32(
      FrameTy, FramePtr, 0, coro::Shape::IndexField, "index.addr");
  auto *Index = Builder.CreateLoad(GepIndex, "index");
  auto *Switch =
      Builder.CreateSwitch(Index, UnreachBB, Shape.CoroSuspends.size());
  Shape.ResumeSwitch = Switch;

  size_t SuspendIndex = 0;
  for (CoroSuspendInst *S : Shape.CoroSuspends) {
    ConstantInt *IndexVal = Shape.getIndex(SuspendIndex);

    // Replace CoroSave with a store to Index:
    //    %index.addr = getelementptr %f.frame... (index field number)
    //    store i32 0, i32* %index.addr1
    auto *Save = S->getCoroSave();
    Builder.SetInsertPoint(Save);
    if (S->isFinal()) {
      // Final suspend point is represented by storing zero in ResumeFnAddr.
      auto *GepIndex = Builder.CreateConstInBoundsGEP2_32(FrameTy, FramePtr, 0,
                                                          0, "ResumeFn.addr");
      auto *NullPtr = ConstantPointerNull::get(cast<PointerType>(
          cast<PointerType>(GepIndex->getType())->getElementType()));
      Builder.CreateStore(NullPtr, GepIndex);
    } else {
      auto *GepIndex = Builder.CreateConstInBoundsGEP2_32(
          FrameTy, FramePtr, 0, coro::Shape::IndexField, "index.addr");
      Builder.CreateStore(IndexVal, GepIndex);
    }
    Save->replaceAllUsesWith(ConstantTokenNone::get(C));
    Save->eraseFromParent();

    // Split block before and after coro.suspend and add a jump from an entry
    // switch:
    //
    //  whateverBB:
    //    whatever
    //    %0 = call i8 @llvm.coro.suspend(token none, i1 false)
    //    switch i8 %0, label %suspend[i8 0, label %resume
    //                                 i8 1, label %cleanup]
    // becomes:
    //
    //  whateverBB:
    //     whatever
    //     br label %resume.0.landing
    //
    //  resume.0: ; <--- jump from the switch in the resume.entry
    //     %0 = tail call i8 @llvm.coro.suspend(token none, i1 false)
    //     br label %resume.0.landing
    //
    //  resume.0.landing:
    //     %1 = phi i8[-1, %whateverBB], [%0, %resume.0]
    //     switch i8 % 1, label %suspend [i8 0, label %resume
    //                                    i8 1, label %cleanup]

    auto *SuspendBB = S->getParent();
    auto *ResumeBB =
        SuspendBB->splitBasicBlock(S, "resume." + Twine(SuspendIndex));
    auto *LandingBB = ResumeBB->splitBasicBlock(
        S->getNextNode(), ResumeBB->getName() + Twine(".landing"));
    Switch->addCase(IndexVal, ResumeBB);

    cast<BranchInst>(SuspendBB->getTerminator())->setSuccessor(0, LandingBB);
    auto *PN = PHINode::Create(Builder.getInt8Ty(), 2, "", &LandingBB->front());
    S->replaceAllUsesWith(PN);
    PN->addIncoming(Builder.getInt8(-1), SuspendBB);
    PN->addIncoming(S, ResumeBB);

    ++SuspendIndex;
  }

  Builder.SetInsertPoint(UnreachBB);
  Builder.CreateUnreachable();

  return NewEntry;
}

// In Resumers, we replace fallthrough coro.end with ret void and delete the
// rest of the block.
static void replaceFallthroughCoroEnd(IntrinsicInst *End,
                                      ValueToValueMapTy &VMap) {
  auto *NewE = cast<IntrinsicInst>(VMap[End]);
  ReturnInst::Create(NewE->getContext(), nullptr, NewE);

  // Remove the rest of the block, by splitting it into an unreachable block.
  auto *BB = NewE->getParent();
  BB->splitBasicBlock(NewE);
  BB->getTerminator()->eraseFromParent();
}

// In Resumers, we replace unwind coro.end with True to force the immediate
// unwind to caller.
static void replaceUnwindCoroEnds(coro::Shape &Shape, ValueToValueMapTy &VMap) {
  if (Shape.CoroEnds.empty())
    return;

  LLVMContext &Context = Shape.CoroEnds.front()->getContext();
  auto *True = ConstantInt::getTrue(Context);
  for (CoroEndInst *CE : Shape.CoroEnds) {
    if (!CE->isUnwind())
      continue;

    auto *NewCE = cast<IntrinsicInst>(VMap[CE]);

    // If coro.end has an associated bundle, add cleanupret instruction.
    if (auto Bundle = NewCE->getOperandBundle(LLVMContext::OB_funclet)) {
      Value *FromPad = Bundle->Inputs[0];
      auto *CleanupRet = CleanupReturnInst::Create(FromPad, nullptr, NewCE);
      NewCE->getParent()->splitBasicBlock(NewCE);
      CleanupRet->getParent()->getTerminator()->eraseFromParent();
    }

    NewCE->replaceAllUsesWith(True);
    NewCE->eraseFromParent();
  }
}

// Rewrite final suspend point handling. We do not use suspend index to
// represent the final suspend point. Instead we zero-out ResumeFnAddr in the
// coroutine frame, since it is undefined behavior to resume a coroutine
// suspended at the final suspend point. Thus, in the resume function, we can
// simply remove the last case (when coro::Shape is built, the final suspend
// point (if present) is always the last element of CoroSuspends array).
// In the destroy function, we add a code sequence to check if ResumeFnAddress
// is Null, and if so, jump to the appropriate label to handle cleanup from the
// final suspend point.
static void handleFinalSuspend(IRBuilder<> &Builder, Value *FramePtr,
                               coro::Shape &Shape, SwitchInst *Switch,
                               bool IsDestroy) {
  assert(Shape.HasFinalSuspend);
  auto FinalCaseIt = std::prev(Switch->case_end());
  BasicBlock *ResumeBB = FinalCaseIt->getCaseSuccessor();
  Switch->removeCase(FinalCaseIt);
  if (IsDestroy) {
    BasicBlock *OldSwitchBB = Switch->getParent();
    auto *NewSwitchBB = OldSwitchBB->splitBasicBlock(Switch, "Switch");
    Builder.SetInsertPoint(OldSwitchBB->getTerminator());
    auto *GepIndex = Builder.CreateConstInBoundsGEP2_32(Shape.FrameTy, FramePtr,
                                                        0, 0, "ResumeFn.addr");
    auto *Load = Builder.CreateLoad(GepIndex);
    auto *NullPtr =
        ConstantPointerNull::get(cast<PointerType>(Load->getType()));
    auto *Cond = Builder.CreateICmpEQ(Load, NullPtr);
    Builder.CreateCondBr(Cond, ResumeBB, NewSwitchBB);
    OldSwitchBB->getTerminator()->eraseFromParent();
  }
}

// Create a resume clone by cloning the body of the original function, setting
// new entry block and replacing coro.suspend an appropriate value to force
// resume or cleanup pass for every suspend point.
static Function *createClone(Function &F, Twine Suffix, coro::Shape &Shape,
                             BasicBlock *ResumeEntry, int8_t FnIndex) {
  Module *M = F.getParent();
  auto *FrameTy = Shape.FrameTy;
  auto *FnPtrTy = cast<PointerType>(FrameTy->getElementType(0));
  auto *FnTy = cast<FunctionType>(FnPtrTy->getElementType());

  Function *NewF =
      Function::Create(FnTy, GlobalValue::LinkageTypes::ExternalLinkage,
                       F.getName() + Suffix, M);
  NewF->addParamAttr(0, Attribute::NonNull);
  NewF->addParamAttr(0, Attribute::NoAlias);

  ValueToValueMapTy VMap;
  // Replace all args with undefs. The buildCoroutineFrame algorithm already
  // rewritten access to the args that occurs after suspend points with loads
  // and stores to/from the coroutine frame.
  for (Argument &A : F.args())
    VMap[&A] = UndefValue::get(A.getType());

  SmallVector<ReturnInst *, 4> Returns;

  CloneFunctionInto(NewF, &F, VMap, /*ModuleLevelChanges=*/true, Returns);
  NewF->setLinkage(GlobalValue::LinkageTypes::InternalLinkage);

  // Remove old returns.
  for (ReturnInst *Return : Returns)
    changeToUnreachable(Return, /*UseLLVMTrap=*/false);

  // Remove old return attributes.
  NewF->removeAttributes(
      AttributeList::ReturnIndex,
      AttributeFuncs::typeIncompatible(NewF->getReturnType()));

  // Make AllocaSpillBlock the new entry block.
  auto *SwitchBB = cast<BasicBlock>(VMap[ResumeEntry]);
  auto *Entry = cast<BasicBlock>(VMap[Shape.AllocaSpillBlock]);
  Entry->moveBefore(&NewF->getEntryBlock());
  Entry->getTerminator()->eraseFromParent();
  BranchInst::Create(SwitchBB, Entry);
  Entry->setName("entry" + Suffix);

  // Clear all predecessors of the new entry block.
  auto *Switch = cast<SwitchInst>(VMap[Shape.ResumeSwitch]);
  Entry->replaceAllUsesWith(Switch->getDefaultDest());

  IRBuilder<> Builder(&NewF->getEntryBlock().front());

  // Remap frame pointer.
  Argument *NewFramePtr = &*NewF->arg_begin();
  Value *OldFramePtr = cast<Value>(VMap[Shape.FramePtr]);
  NewFramePtr->takeName(OldFramePtr);
  OldFramePtr->replaceAllUsesWith(NewFramePtr);

  // Remap vFrame pointer.
  auto *NewVFrame = Builder.CreateBitCast(
      NewFramePtr, Type::getInt8PtrTy(Builder.getContext()), "vFrame");
  Value *OldVFrame = cast<Value>(VMap[Shape.CoroBegin]);
  OldVFrame->replaceAllUsesWith(NewVFrame);

  // Rewrite final suspend handling as it is not done via switch (allows to
  // remove final case from the switch, since it is undefined behavior to resume
  // the coroutine suspended at the final suspend point.
  if (Shape.HasFinalSuspend) {
    auto *Switch = cast<SwitchInst>(VMap[Shape.ResumeSwitch]);
    bool IsDestroy = FnIndex != 0;
    handleFinalSuspend(Builder, NewFramePtr, Shape, Switch, IsDestroy);
  }

  // Replace coro suspend with the appropriate resume index.
  // Replacing coro.suspend with (0) will result in control flow proceeding to
  // a resume label associated with a suspend point, replacing it with (1) will
  // result in control flow proceeding to a cleanup label associated with this
  // suspend point.
  auto *NewValue = Builder.getInt8(FnIndex ? 1 : 0);
  for (CoroSuspendInst *CS : Shape.CoroSuspends) {
    auto *MappedCS = cast<CoroSuspendInst>(VMap[CS]);
    MappedCS->replaceAllUsesWith(NewValue);
    MappedCS->eraseFromParent();
  }

  // Remove coro.end intrinsics.
  replaceFallthroughCoroEnd(Shape.CoroEnds.front(), VMap);
  replaceUnwindCoroEnds(Shape, VMap);
  // Eliminate coro.free from the clones, replacing it with 'null' in cleanup,
  // to suppress deallocation code.
  coro::replaceCoroFree(cast<CoroIdInst>(VMap[Shape.CoroBegin->getId()]),
                        /*Elide=*/FnIndex == 2);

  NewF->setCallingConv(CallingConv::Fast);

  return NewF;
}

static void removeCoroEnds(coro::Shape &Shape) {
  if (Shape.CoroEnds.empty())
    return;

  LLVMContext &Context = Shape.CoroEnds.front()->getContext();
  auto *False = ConstantInt::getFalse(Context);

  for (CoroEndInst *CE : Shape.CoroEnds) {
    CE->replaceAllUsesWith(False);
    CE->eraseFromParent();
  }
}

static void replaceFrameSize(coro::Shape &Shape) {
  if (Shape.CoroSizes.empty())
    return;

  // In the same function all coro.sizes should have the same result type.
  auto *SizeIntrin = Shape.CoroSizes.back();
  Module *M = SizeIntrin->getModule();
  const DataLayout &DL = M->getDataLayout();
  auto Size = DL.getTypeAllocSize(Shape.FrameTy);
  auto *SizeConstant = ConstantInt::get(SizeIntrin->getType(), Size);

  for (CoroSizeInst *CS : Shape.CoroSizes) {
    CS->replaceAllUsesWith(SizeConstant);
    CS->eraseFromParent();
  }
}

// Create a global constant array containing pointers to functions provided and
// set Info parameter of CoroBegin to point at this constant. Example:
//
//   @f.resumers = internal constant [2 x void(%f.frame*)*]
//                    [void(%f.frame*)* @f.resume, void(%f.frame*)* @f.destroy]
//   define void @f() {
//     ...
//     call i8* @llvm.coro.begin(i8* null, i32 0, i8* null,
//                    i8* bitcast([2 x void(%f.frame*)*] * @f.resumers to i8*))
//
// Assumes that all the functions have the same signature.
static void setCoroInfo(Function &F, CoroBeginInst *CoroBegin,
                        std::initializer_list<Function *> Fns) {
  SmallVector<Constant *, 4> Args(Fns.begin(), Fns.end());
  assert(!Args.empty());
  Function *Part = *Fns.begin();
  Module *M = Part->getParent();
  auto *ArrTy = ArrayType::get(Part->getType(), Args.size());

  auto *ConstVal = ConstantArray::get(ArrTy, Args);
  auto *GV = new GlobalVariable(*M, ConstVal->getType(), /*isConstant=*/true,
                                GlobalVariable::PrivateLinkage, ConstVal,
                                F.getName() + Twine(".resumers"));

  // Update coro.begin instruction to refer to this constant.
  LLVMContext &C = F.getContext();
  auto *BC = ConstantExpr::getPointerCast(GV, Type::getInt8PtrTy(C));
  CoroBegin->getId()->setInfo(BC);
}

// Store addresses of Resume/Destroy/Cleanup functions in the coroutine frame.
static void updateCoroFrame(coro::Shape &Shape, Function *ResumeFn,
                            Function *DestroyFn, Function *CleanupFn) {
  IRBuilder<> Builder(Shape.FramePtr->getNextNode());
  auto *ResumeAddr = Builder.CreateConstInBoundsGEP2_32(
      Shape.FrameTy, Shape.FramePtr, 0, coro::Shape::ResumeField,
      "resume.addr");
  Builder.CreateStore(ResumeFn, ResumeAddr);

  Value *DestroyOrCleanupFn = DestroyFn;

  CoroIdInst *CoroId = Shape.CoroBegin->getId();
  if (CoroAllocInst *CA = CoroId->getCoroAlloc()) {
    // If there is a CoroAlloc and it returns false (meaning we elide the
    // allocation, use CleanupFn instead of DestroyFn).
    DestroyOrCleanupFn = Builder.CreateSelect(CA, DestroyFn, CleanupFn);
  }

  auto *DestroyAddr = Builder.CreateConstInBoundsGEP2_32(
      Shape.FrameTy, Shape.FramePtr, 0, coro::Shape::DestroyField,
      "destroy.addr");
  Builder.CreateStore(DestroyOrCleanupFn, DestroyAddr);
}

static void postSplitCleanup(Function &F) {
  removeUnreachableBlocks(F);
  legacy::FunctionPassManager FPM(F.getParent());

  FPM.add(createVerifierPass());
  FPM.add(createSCCPPass());
  FPM.add(createCFGSimplificationPass());
  FPM.add(createEarlyCSEPass());
  FPM.add(createCFGSimplificationPass());

  FPM.doInitialization();
  FPM.run(F);
  FPM.doFinalization();
}

// Assuming we arrived at the block NewBlock from Prev instruction, store
// PHI's incoming values in the ResolvedValues map.
static void
scanPHIsAndUpdateValueMap(Instruction *Prev, BasicBlock *NewBlock,
                          DenseMap<Value *, Value *> &ResolvedValues) {
  auto *PrevBB = Prev->getParent();
  for (PHINode &PN : NewBlock->phis()) {
    auto V = PN.getIncomingValueForBlock(PrevBB);
    // See if we already resolved it.
    auto VI = ResolvedValues.find(V);
    if (VI != ResolvedValues.end())
      V = VI->second;
    // Remember the value.
    ResolvedValues[&PN] = V;
  }
}

// Replace a sequence of branches leading to a ret, with a clone of a ret
// instruction. Suspend instruction represented by a switch, track the PHI
// values and select the correct case successor when possible.
static bool simplifyTerminatorLeadingToRet(Instruction *InitialInst) {
  DenseMap<Value *, Value *> ResolvedValues;

  Instruction *I = InitialInst;
  while (I->isTerminator()) {
    if (isa<ReturnInst>(I)) {
      if (I != InitialInst)
        ReplaceInstWithInst(InitialInst, I->clone());
      return true;
    }
    if (auto *BR = dyn_cast<BranchInst>(I)) {
      if (BR->isUnconditional()) {
        BasicBlock *BB = BR->getSuccessor(0);
        scanPHIsAndUpdateValueMap(I, BB, ResolvedValues);
        I = BB->getFirstNonPHIOrDbgOrLifetime();
        continue;
      }
    } else if (auto *SI = dyn_cast<SwitchInst>(I)) {
      Value *V = SI->getCondition();
      auto it = ResolvedValues.find(V);
      if (it != ResolvedValues.end())
        V = it->second;
      if (ConstantInt *Cond = dyn_cast<ConstantInt>(V)) {
        BasicBlock *BB = SI->findCaseValue(Cond)->getCaseSuccessor();
        scanPHIsAndUpdateValueMap(I, BB, ResolvedValues);
        I = BB->getFirstNonPHIOrDbgOrLifetime();
        continue;
      }
    }
    return false;
  }
  return false;
}

// Add musttail to any resume instructions that is immediately followed by a
// suspend (i.e. ret). We do this even in -O0 to support guaranteed tail call
// for symmetrical coroutine control transfer (C++ Coroutines TS extension).
// This transformation is done only in the resume part of the coroutine that has
// identical signature and calling convention as the coro.resume call.
static void addMustTailToCoroResumes(Function &F) {
  bool changed = false;

  // Collect potential resume instructions.
  SmallVector<CallInst *, 4> Resumes;
  for (auto &I : instructions(F))
    if (auto *Call = dyn_cast<CallInst>(&I))
      if (auto *CalledValue = Call->getCalledValue())
        // CoroEarly pass replaced coro resumes with indirect calls to an
        // address return by CoroSubFnInst intrinsic. See if it is one of those.
        if (isa<CoroSubFnInst>(CalledValue->stripPointerCasts()))
          Resumes.push_back(Call);

  // Set musttail on those that are followed by a ret instruction.
  for (CallInst *Call : Resumes)
    if (simplifyTerminatorLeadingToRet(Call->getNextNode())) {
      Call->setTailCallKind(CallInst::TCK_MustTail);
      changed = true;
    }

  if (changed)
    removeUnreachableBlocks(F);
}

// Coroutine has no suspend points. Remove heap allocation for the coroutine
// frame if possible.
static void handleNoSuspendCoroutine(CoroBeginInst *CoroBegin, Type *FrameTy) {
  auto *CoroId = CoroBegin->getId();
  auto *AllocInst = CoroId->getCoroAlloc();
  coro::replaceCoroFree(CoroId, /*Elide=*/AllocInst != nullptr);
  if (AllocInst) {
    IRBuilder<> Builder(AllocInst);
    // FIXME: Need to handle overaligned members.
    auto *Frame = Builder.CreateAlloca(FrameTy);
    auto *VFrame = Builder.CreateBitCast(Frame, Builder.getInt8PtrTy());
    AllocInst->replaceAllUsesWith(Builder.getFalse());
    AllocInst->eraseFromParent();
    CoroBegin->replaceAllUsesWith(VFrame);
  } else {
    CoroBegin->replaceAllUsesWith(CoroBegin->getMem());
  }
  CoroBegin->eraseFromParent();
}

// SimplifySuspendPoint needs to check that there is no calls between
// coro_save and coro_suspend, since any of the calls may potentially resume
// the coroutine and if that is the case we cannot eliminate the suspend point.
static bool hasCallsInBlockBetween(Instruction *From, Instruction *To) {
  for (Instruction *I = From; I != To; I = I->getNextNode()) {
    // Assume that no intrinsic can resume the coroutine.
    if (isa<IntrinsicInst>(I))
      continue;

    if (CallSite(I))
      return true;
  }
  return false;
}

static bool hasCallsInBlocksBetween(BasicBlock *SaveBB, BasicBlock *ResDesBB) {
  SmallPtrSet<BasicBlock *, 8> Set;
  SmallVector<BasicBlock *, 8> Worklist;

  Set.insert(SaveBB);
  Worklist.push_back(ResDesBB);

  // Accumulate all blocks between SaveBB and ResDesBB. Because CoroSaveIntr
  // returns a token consumed by suspend instruction, all blocks in between
  // will have to eventually hit SaveBB when going backwards from ResDesBB.
  while (!Worklist.empty()) {
    auto *BB = Worklist.pop_back_val();
    Set.insert(BB);
    for (auto *Pred : predecessors(BB))
      if (Set.count(Pred) == 0)
        Worklist.push_back(Pred);
  }

  // SaveBB and ResDesBB are checked separately in hasCallsBetween.
  Set.erase(SaveBB);
  Set.erase(ResDesBB);

  for (auto *BB : Set)
    if (hasCallsInBlockBetween(BB->getFirstNonPHI(), nullptr))
      return true;

  return false;
}

static bool hasCallsBetween(Instruction *Save, Instruction *ResumeOrDestroy) {
  auto *SaveBB = Save->getParent();
  auto *ResumeOrDestroyBB = ResumeOrDestroy->getParent();

  if (SaveBB == ResumeOrDestroyBB)
    return hasCallsInBlockBetween(Save->getNextNode(), ResumeOrDestroy);

  // Any calls from Save to the end of the block?
  if (hasCallsInBlockBetween(Save->getNextNode(), nullptr))
    return true;

  // Any calls from begging of the block up to ResumeOrDestroy?
  if (hasCallsInBlockBetween(ResumeOrDestroyBB->getFirstNonPHI(),
                             ResumeOrDestroy))
    return true;

  // Any calls in all of the blocks between SaveBB and ResumeOrDestroyBB?
  if (hasCallsInBlocksBetween(SaveBB, ResumeOrDestroyBB))
    return true;

  return false;
}

// If a SuspendIntrin is preceded by Resume or Destroy, we can eliminate the
// suspend point and replace it with nornal control flow.
static bool simplifySuspendPoint(CoroSuspendInst *Suspend,
                                 CoroBeginInst *CoroBegin) {
  Instruction *Prev = Suspend->getPrevNode();
  if (!Prev) {
    auto *Pred = Suspend->getParent()->getSinglePredecessor();
    if (!Pred)
      return false;
    Prev = Pred->getTerminator();
  }

  CallSite CS{Prev};
  if (!CS)
    return false;

  auto *CallInstr = CS.getInstruction();

  auto *Callee = CS.getCalledValue()->stripPointerCasts();

  // See if the callsite is for resumption or destruction of the coroutine.
  auto *SubFn = dyn_cast<CoroSubFnInst>(Callee);
  if (!SubFn)
    return false;

  // Does not refer to the current coroutine, we cannot do anything with it.
  if (SubFn->getFrame() != CoroBegin)
    return false;

  // See if the transformation is safe. Specifically, see if there are any
  // calls in between Save and CallInstr. They can potenitally resume the
  // coroutine rendering this optimization unsafe.
  auto *Save = Suspend->getCoroSave();
  if (hasCallsBetween(Save, CallInstr))
    return false;

  // Replace llvm.coro.suspend with the value that results in resumption over
  // the resume or cleanup path.
  Suspend->replaceAllUsesWith(SubFn->getRawIndex());
  Suspend->eraseFromParent();
  Save->eraseFromParent();

  // No longer need a call to coro.resume or coro.destroy.
  if (auto *Invoke = dyn_cast<InvokeInst>(CallInstr)) {
    BranchInst::Create(Invoke->getNormalDest(), Invoke);
  }

  // Grab the CalledValue from CS before erasing the CallInstr.
  auto *CalledValue = CS.getCalledValue();
  CallInstr->eraseFromParent();

  // If no more users remove it. Usually it is a bitcast of SubFn.
  if (CalledValue != SubFn && CalledValue->user_empty())
    if (auto *I = dyn_cast<Instruction>(CalledValue))
      I->eraseFromParent();

  // Now we are good to remove SubFn.
  if (SubFn->user_empty())
    SubFn->eraseFromParent();

  return true;
}

// Remove suspend points that are simplified.
static void simplifySuspendPoints(coro::Shape &Shape) {
  auto &S = Shape.CoroSuspends;
  size_t I = 0, N = S.size();
  if (N == 0)
    return;
  while (true) {
    if (simplifySuspendPoint(S[I], Shape.CoroBegin)) {
      if (--N == I)
        break;
      std::swap(S[I], S[N]);
      continue;
    }
    if (++I == N)
      break;
  }
  S.resize(N);
}

static SmallPtrSet<BasicBlock *, 4> getCoroBeginPredBlocks(CoroBeginInst *CB) {
  // Collect all blocks that we need to look for instructions to relocate.
  SmallPtrSet<BasicBlock *, 4> RelocBlocks;
  SmallVector<BasicBlock *, 4> Work;
  Work.push_back(CB->getParent());

  do {
    BasicBlock *Current = Work.pop_back_val();
    for (BasicBlock *BB : predecessors(Current))
      if (RelocBlocks.count(BB) == 0) {
        RelocBlocks.insert(BB);
        Work.push_back(BB);
      }
  } while (!Work.empty());
  return RelocBlocks;
}

static SmallPtrSet<Instruction *, 8>
getNotRelocatableInstructions(CoroBeginInst *CoroBegin,
                              SmallPtrSetImpl<BasicBlock *> &RelocBlocks) {
  SmallPtrSet<Instruction *, 8> DoNotRelocate;
  // Collect all instructions that we should not relocate
  SmallVector<Instruction *, 8> Work;

  // Start with CoroBegin and terminators of all preceding blocks.
  Work.push_back(CoroBegin);
  BasicBlock *CoroBeginBB = CoroBegin->getParent();
  for (BasicBlock *BB : RelocBlocks)
    if (BB != CoroBeginBB)
      Work.push_back(BB->getTerminator());

  // For every instruction in the Work list, place its operands in DoNotRelocate
  // set.
  do {
    Instruction *Current = Work.pop_back_val();
    LLVM_DEBUG(dbgs() << "CoroSplit: Will not relocate: " << *Current << "\n");
    DoNotRelocate.insert(Current);
    for (Value *U : Current->operands()) {
      auto *I = dyn_cast<Instruction>(U);
      if (!I)
        continue;

      if (auto *A = dyn_cast<AllocaInst>(I)) {
        // Stores to alloca instructions that occur before the coroutine frame
        // is allocated should not be moved; the stored values may be used by
        // the coroutine frame allocator. The operands to those stores must also
        // remain in place.
        for (const auto &User : A->users())
          if (auto *SI = dyn_cast<llvm::StoreInst>(User))
            if (RelocBlocks.count(SI->getParent()) != 0 &&
                DoNotRelocate.count(SI) == 0) {
              Work.push_back(SI);
              DoNotRelocate.insert(SI);
            }
        continue;
      }

      if (DoNotRelocate.count(I) == 0) {
        Work.push_back(I);
        DoNotRelocate.insert(I);
      }
    }
  } while (!Work.empty());
  return DoNotRelocate;
}

static void relocateInstructionBefore(CoroBeginInst *CoroBegin, Function &F) {
  // Analyze which non-alloca instructions are needed for allocation and
  // relocate the rest to after coro.begin. We need to do it, since some of the
  // targets of those instructions may be placed into coroutine frame memory
  // for which becomes available after coro.begin intrinsic.

  auto BlockSet = getCoroBeginPredBlocks(CoroBegin);
  auto DoNotRelocateSet = getNotRelocatableInstructions(CoroBegin, BlockSet);

  Instruction *InsertPt = CoroBegin->getNextNode();
  BasicBlock &BB = F.getEntryBlock(); // TODO: Look at other blocks as well.
  for (auto B = BB.begin(), E = BB.end(); B != E;) {
    Instruction &I = *B++;
    if (isa<AllocaInst>(&I))
      continue;
    if (&I == CoroBegin)
      break;
    if (DoNotRelocateSet.count(&I))
      continue;
    I.moveBefore(InsertPt);
  }
}

static void splitCoroutine(Function &F, CallGraph &CG, CallGraphSCC &SCC) {
  coro::Shape Shape(F);
  if (!Shape.CoroBegin)
    return;

  simplifySuspendPoints(Shape);
  relocateInstructionBefore(Shape.CoroBegin, F);
  buildCoroutineFrame(F, Shape);
  replaceFrameSize(Shape);

  // If there are no suspend points, no split required, just remove
  // the allocation and deallocation blocks, they are not needed.
  if (Shape.CoroSuspends.empty()) {
    handleNoSuspendCoroutine(Shape.CoroBegin, Shape.FrameTy);
    removeCoroEnds(Shape);
    postSplitCleanup(F);
    coro::updateCallGraph(F, {}, CG, SCC);
    return;
  }

  auto *ResumeEntry = createResumeEntryBlock(F, Shape);
  auto ResumeClone = createClone(F, ".resume", Shape, ResumeEntry, 0);
  auto DestroyClone = createClone(F, ".destroy", Shape, ResumeEntry, 1);
  auto CleanupClone = createClone(F, ".cleanup", Shape, ResumeEntry, 2);

  // We no longer need coro.end in F.
  removeCoroEnds(Shape);

  postSplitCleanup(F);
  postSplitCleanup(*ResumeClone);
  postSplitCleanup(*DestroyClone);
  postSplitCleanup(*CleanupClone);

  addMustTailToCoroResumes(*ResumeClone);

  // Store addresses resume/destroy/cleanup functions in the coroutine frame.
  updateCoroFrame(Shape, ResumeClone, DestroyClone, CleanupClone);

  // Create a constant array referring to resume/destroy/clone functions pointed
  // by the last argument of @llvm.coro.info, so that CoroElide pass can
  // determined correct function to call.
  setCoroInfo(F, Shape.CoroBegin, {ResumeClone, DestroyClone, CleanupClone});

  // Update call graph and add the functions we created to the SCC.
  coro::updateCallGraph(F, {ResumeClone, DestroyClone, CleanupClone}, CG, SCC);
}

// When we see the coroutine the first time, we insert an indirect call to a
// devirt trigger function and mark the coroutine that it is now ready for
// split.
static void prepareForSplit(Function &F, CallGraph &CG) {
  Module &M = *F.getParent();
#ifndef NDEBUG
  Function *DevirtFn = M.getFunction(CORO_DEVIRT_TRIGGER_FN);
  assert(DevirtFn && "coro.devirt.trigger function not found");
#endif

  F.addFnAttr(CORO_PRESPLIT_ATTR, PREPARED_FOR_SPLIT);

  // Insert an indirect call sequence that will be devirtualized by CoroElide
  // pass:
  //    %0 = call i8* @llvm.coro.subfn.addr(i8* null, i8 -1)
  //    %1 = bitcast i8* %0 to void(i8*)*
  //    call void %1(i8* null)
  coro::LowererBase Lowerer(M);
  Instruction *InsertPt = F.getEntryBlock().getTerminator();
  auto *Null = ConstantPointerNull::get(Type::getInt8PtrTy(F.getContext()));
  auto *DevirtFnAddr =
      Lowerer.makeSubFnCall(Null, CoroSubFnInst::RestartTrigger, InsertPt);
  auto *IndirectCall = CallInst::Create(DevirtFnAddr, Null, "", InsertPt);

  // Update CG graph with an indirect call we just added.
  CG[&F]->addCalledFunction(IndirectCall, CG.getCallsExternalNode());
}

// Make sure that there is a devirtualization trigger function that CoroSplit
// pass uses the force restart CGSCC pipeline. If devirt trigger function is not
// found, we will create one and add it to the current SCC.
static void createDevirtTriggerFunc(CallGraph &CG, CallGraphSCC &SCC) {
  Module &M = CG.getModule();
  if (M.getFunction(CORO_DEVIRT_TRIGGER_FN))
    return;

  LLVMContext &C = M.getContext();
  auto *FnTy = FunctionType::get(Type::getVoidTy(C), Type::getInt8PtrTy(C),
                                 /*IsVarArgs=*/false);
  Function *DevirtFn =
      Function::Create(FnTy, GlobalValue::LinkageTypes::PrivateLinkage,
                       CORO_DEVIRT_TRIGGER_FN, &M);
  DevirtFn->addFnAttr(Attribute::AlwaysInline);
  auto *Entry = BasicBlock::Create(C, "entry", DevirtFn);
  ReturnInst::Create(C, Entry);

  auto *Node = CG.getOrInsertFunction(DevirtFn);

  SmallVector<CallGraphNode *, 8> Nodes(SCC.begin(), SCC.end());
  Nodes.push_back(Node);
  SCC.initialize(Nodes);
}

//===----------------------------------------------------------------------===//
//                              Top Level Driver
//===----------------------------------------------------------------------===//

namespace {

struct CoroSplit : public CallGraphSCCPass {
  static char ID; // Pass identification, replacement for typeid

  CoroSplit() : CallGraphSCCPass(ID) {
    initializeCoroSplitPass(*PassRegistry::getPassRegistry());
  }

  bool Run = false;

  // A coroutine is identified by the presence of coro.begin intrinsic, if
  // we don't have any, this pass has nothing to do.
  bool doInitialization(CallGraph &CG) override {
    Run = coro::declaresIntrinsics(CG.getModule(), {"llvm.coro.begin"});
    return CallGraphSCCPass::doInitialization(CG);
  }

  bool runOnSCC(CallGraphSCC &SCC) override {
    if (!Run)
      return false;

    // Find coroutines for processing.
    SmallVector<Function *, 4> Coroutines;
    for (CallGraphNode *CGN : SCC)
      if (auto *F = CGN->getFunction())
        if (F->hasFnAttribute(CORO_PRESPLIT_ATTR))
          Coroutines.push_back(F);

    if (Coroutines.empty())
      return false;

    CallGraph &CG = getAnalysis<CallGraphWrapperPass>().getCallGraph();
    createDevirtTriggerFunc(CG, SCC);

    for (Function *F : Coroutines) {
      Attribute Attr = F->getFnAttribute(CORO_PRESPLIT_ATTR);
      StringRef Value = Attr.getValueAsString();
      LLVM_DEBUG(dbgs() << "CoroSplit: Processing coroutine '" << F->getName()
                        << "' state: " << Value << "\n");
      if (Value == UNPREPARED_FOR_SPLIT) {
        prepareForSplit(*F, CG);
        continue;
      }
      F->removeFnAttr(CORO_PRESPLIT_ATTR);
      splitCoroutine(*F, CG, SCC);
    }
    return true;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    CallGraphSCCPass::getAnalysisUsage(AU);
  }

  StringRef getPassName() const override { return "Coroutine Splitting"; }
};

} // end anonymous namespace

char CoroSplit::ID = 0;

INITIALIZE_PASS(
    CoroSplit, "coro-split",
    "Split coroutine into a set of functions driving its state machine", false,
    false)

Pass *llvm::createCoroSplitPass() { return new CoroSplit(); }
