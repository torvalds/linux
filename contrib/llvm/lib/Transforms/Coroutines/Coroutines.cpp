//===- Coroutines.cpp -----------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the common infrastructure for Coroutine Passes.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Coroutines.h"
#include "llvm-c/Transforms/Coroutines.h"
#include "CoroInstr.h"
#include "CoroInternal.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include <cassert>
#include <cstddef>
#include <utility>

using namespace llvm;

void llvm::initializeCoroutines(PassRegistry &Registry) {
  initializeCoroEarlyPass(Registry);
  initializeCoroSplitPass(Registry);
  initializeCoroElidePass(Registry);
  initializeCoroCleanupPass(Registry);
}

static void addCoroutineOpt0Passes(const PassManagerBuilder &Builder,
                                   legacy::PassManagerBase &PM) {
  PM.add(createCoroSplitPass());
  PM.add(createCoroElidePass());

  PM.add(createBarrierNoopPass());
  PM.add(createCoroCleanupPass());
}

static void addCoroutineEarlyPasses(const PassManagerBuilder &Builder,
                                    legacy::PassManagerBase &PM) {
  PM.add(createCoroEarlyPass());
}

static void addCoroutineScalarOptimizerPasses(const PassManagerBuilder &Builder,
                                              legacy::PassManagerBase &PM) {
  PM.add(createCoroElidePass());
}

static void addCoroutineSCCPasses(const PassManagerBuilder &Builder,
                                  legacy::PassManagerBase &PM) {
  PM.add(createCoroSplitPass());
}

static void addCoroutineOptimizerLastPasses(const PassManagerBuilder &Builder,
                                            legacy::PassManagerBase &PM) {
  PM.add(createCoroCleanupPass());
}

void llvm::addCoroutinePassesToExtensionPoints(PassManagerBuilder &Builder) {
  Builder.addExtension(PassManagerBuilder::EP_EarlyAsPossible,
                       addCoroutineEarlyPasses);
  Builder.addExtension(PassManagerBuilder::EP_EnabledOnOptLevel0,
                       addCoroutineOpt0Passes);
  Builder.addExtension(PassManagerBuilder::EP_CGSCCOptimizerLate,
                       addCoroutineSCCPasses);
  Builder.addExtension(PassManagerBuilder::EP_ScalarOptimizerLate,
                       addCoroutineScalarOptimizerPasses);
  Builder.addExtension(PassManagerBuilder::EP_OptimizerLast,
                       addCoroutineOptimizerLastPasses);
}

// Construct the lowerer base class and initialize its members.
coro::LowererBase::LowererBase(Module &M)
    : TheModule(M), Context(M.getContext()),
      Int8Ptr(Type::getInt8PtrTy(Context)),
      ResumeFnType(FunctionType::get(Type::getVoidTy(Context), Int8Ptr,
                                     /*isVarArg=*/false)),
      NullPtr(ConstantPointerNull::get(Int8Ptr)) {}

// Creates a sequence of instructions to obtain a resume function address using
// llvm.coro.subfn.addr. It generates the following sequence:
//
//    call i8* @llvm.coro.subfn.addr(i8* %Arg, i8 %index)
//    bitcast i8* %2 to void(i8*)*

Value *coro::LowererBase::makeSubFnCall(Value *Arg, int Index,
                                        Instruction *InsertPt) {
  auto *IndexVal = ConstantInt::get(Type::getInt8Ty(Context), Index);
  auto *Fn = Intrinsic::getDeclaration(&TheModule, Intrinsic::coro_subfn_addr);

  assert(Index >= CoroSubFnInst::IndexFirst &&
         Index < CoroSubFnInst::IndexLast &&
         "makeSubFnCall: Index value out of range");
  auto *Call = CallInst::Create(Fn, {Arg, IndexVal}, "", InsertPt);

  auto *Bitcast =
      new BitCastInst(Call, ResumeFnType->getPointerTo(), "", InsertPt);
  return Bitcast;
}

#ifndef NDEBUG
static bool isCoroutineIntrinsicName(StringRef Name) {
  // NOTE: Must be sorted!
  static const char *const CoroIntrinsics[] = {
      "llvm.coro.alloc",   "llvm.coro.begin",   "llvm.coro.destroy",
      "llvm.coro.done",    "llvm.coro.end",     "llvm.coro.frame",
      "llvm.coro.free",    "llvm.coro.id",      "llvm.coro.noop",
      "llvm.coro.param",   "llvm.coro.promise", "llvm.coro.resume",
      "llvm.coro.save",    "llvm.coro.size",    "llvm.coro.subfn.addr",
      "llvm.coro.suspend",
  };
  return Intrinsic::lookupLLVMIntrinsicByName(CoroIntrinsics, Name) != -1;
}
#endif

// Verifies if a module has named values listed. Also, in debug mode verifies
// that names are intrinsic names.
bool coro::declaresIntrinsics(Module &M,
                              std::initializer_list<StringRef> List) {
  for (StringRef Name : List) {
    assert(isCoroutineIntrinsicName(Name) && "not a coroutine intrinsic");
    if (M.getNamedValue(Name))
      return true;
  }

  return false;
}

// Replace all coro.frees associated with the provided CoroId either with 'null'
// if Elide is true and with its frame parameter otherwise.
void coro::replaceCoroFree(CoroIdInst *CoroId, bool Elide) {
  SmallVector<CoroFreeInst *, 4> CoroFrees;
  for (User *U : CoroId->users())
    if (auto CF = dyn_cast<CoroFreeInst>(U))
      CoroFrees.push_back(CF);

  if (CoroFrees.empty())
    return;

  Value *Replacement =
      Elide ? ConstantPointerNull::get(Type::getInt8PtrTy(CoroId->getContext()))
            : CoroFrees.front()->getFrame();

  for (CoroFreeInst *CF : CoroFrees) {
    CF->replaceAllUsesWith(Replacement);
    CF->eraseFromParent();
  }
}

// FIXME: This code is stolen from CallGraph::addToCallGraph(Function *F), which
// happens to be private. It is better for this functionality exposed by the
// CallGraph.
static void buildCGN(CallGraph &CG, CallGraphNode *Node) {
  Function *F = Node->getFunction();

  // Look for calls by this function.
  for (Instruction &I : instructions(F))
    if (CallSite CS = CallSite(cast<Value>(&I))) {
      const Function *Callee = CS.getCalledFunction();
      if (!Callee || !Intrinsic::isLeaf(Callee->getIntrinsicID()))
        // Indirect calls of intrinsics are not allowed so no need to check.
        // We can be more precise here by using TargetArg returned by
        // Intrinsic::isLeaf.
        Node->addCalledFunction(CS, CG.getCallsExternalNode());
      else if (!Callee->isIntrinsic())
        Node->addCalledFunction(CS, CG.getOrInsertFunction(Callee));
    }
}

// Rebuild CGN after we extracted parts of the code from ParentFunc into
// NewFuncs. Builds CGNs for the NewFuncs and adds them to the current SCC.
void coro::updateCallGraph(Function &ParentFunc, ArrayRef<Function *> NewFuncs,
                           CallGraph &CG, CallGraphSCC &SCC) {
  // Rebuild CGN from scratch for the ParentFunc
  auto *ParentNode = CG[&ParentFunc];
  ParentNode->removeAllCalledFunctions();
  buildCGN(CG, ParentNode);

  SmallVector<CallGraphNode *, 8> Nodes(SCC.begin(), SCC.end());

  for (Function *F : NewFuncs) {
    CallGraphNode *Callee = CG.getOrInsertFunction(F);
    Nodes.push_back(Callee);
    buildCGN(CG, Callee);
  }

  SCC.initialize(Nodes);
}

static void clear(coro::Shape &Shape) {
  Shape.CoroBegin = nullptr;
  Shape.CoroEnds.clear();
  Shape.CoroSizes.clear();
  Shape.CoroSuspends.clear();

  Shape.FrameTy = nullptr;
  Shape.FramePtr = nullptr;
  Shape.AllocaSpillBlock = nullptr;
  Shape.ResumeSwitch = nullptr;
  Shape.PromiseAlloca = nullptr;
  Shape.HasFinalSuspend = false;
}

static CoroSaveInst *createCoroSave(CoroBeginInst *CoroBegin,
                                    CoroSuspendInst *SuspendInst) {
  Module *M = SuspendInst->getModule();
  auto *Fn = Intrinsic::getDeclaration(M, Intrinsic::coro_save);
  auto *SaveInst =
      cast<CoroSaveInst>(CallInst::Create(Fn, CoroBegin, "", SuspendInst));
  assert(!SuspendInst->getCoroSave());
  SuspendInst->setArgOperand(0, SaveInst);
  return SaveInst;
}

// Collect "interesting" coroutine intrinsics.
void coro::Shape::buildFrom(Function &F) {
  size_t FinalSuspendIndex = 0;
  clear(*this);
  SmallVector<CoroFrameInst *, 8> CoroFrames;
  SmallVector<CoroSaveInst *, 2> UnusedCoroSaves;

  for (Instruction &I : instructions(F)) {
    if (auto II = dyn_cast<IntrinsicInst>(&I)) {
      switch (II->getIntrinsicID()) {
      default:
        continue;
      case Intrinsic::coro_size:
        CoroSizes.push_back(cast<CoroSizeInst>(II));
        break;
      case Intrinsic::coro_frame:
        CoroFrames.push_back(cast<CoroFrameInst>(II));
        break;
      case Intrinsic::coro_save:
        // After optimizations, coro_suspends using this coro_save might have
        // been removed, remember orphaned coro_saves to remove them later.
        if (II->use_empty())
          UnusedCoroSaves.push_back(cast<CoroSaveInst>(II));
        break;
      case Intrinsic::coro_suspend:
        CoroSuspends.push_back(cast<CoroSuspendInst>(II));
        if (CoroSuspends.back()->isFinal()) {
          if (HasFinalSuspend)
            report_fatal_error(
              "Only one suspend point can be marked as final");
          HasFinalSuspend = true;
          FinalSuspendIndex = CoroSuspends.size() - 1;
        }
        break;
      case Intrinsic::coro_begin: {
        auto CB = cast<CoroBeginInst>(II);
        if (CB->getId()->getInfo().isPreSplit()) {
          if (CoroBegin)
            report_fatal_error(
                "coroutine should have exactly one defining @llvm.coro.begin");
          CB->addAttribute(AttributeList::ReturnIndex, Attribute::NonNull);
          CB->addAttribute(AttributeList::ReturnIndex, Attribute::NoAlias);
          CB->removeAttribute(AttributeList::FunctionIndex,
                              Attribute::NoDuplicate);
          CoroBegin = CB;
        }
        break;
      }
      case Intrinsic::coro_end:
        CoroEnds.push_back(cast<CoroEndInst>(II));
        if (CoroEnds.back()->isFallthrough()) {
          // Make sure that the fallthrough coro.end is the first element in the
          // CoroEnds vector.
          if (CoroEnds.size() > 1) {
            if (CoroEnds.front()->isFallthrough())
              report_fatal_error(
                  "Only one coro.end can be marked as fallthrough");
            std::swap(CoroEnds.front(), CoroEnds.back());
          }
        }
        break;
      }
    }
  }

  // If for some reason, we were not able to find coro.begin, bailout.
  if (!CoroBegin) {
    // Replace coro.frame which are supposed to be lowered to the result of
    // coro.begin with undef.
    auto *Undef = UndefValue::get(Type::getInt8PtrTy(F.getContext()));
    for (CoroFrameInst *CF : CoroFrames) {
      CF->replaceAllUsesWith(Undef);
      CF->eraseFromParent();
    }

    // Replace all coro.suspend with undef and remove related coro.saves if
    // present.
    for (CoroSuspendInst *CS : CoroSuspends) {
      CS->replaceAllUsesWith(UndefValue::get(CS->getType()));
      CS->eraseFromParent();
      if (auto *CoroSave = CS->getCoroSave())
        CoroSave->eraseFromParent();
    }

    // Replace all coro.ends with unreachable instruction.
    for (CoroEndInst *CE : CoroEnds)
      changeToUnreachable(CE, /*UseLLVMTrap=*/false);

    return;
  }

  // The coro.free intrinsic is always lowered to the result of coro.begin.
  for (CoroFrameInst *CF : CoroFrames) {
    CF->replaceAllUsesWith(CoroBegin);
    CF->eraseFromParent();
  }

  // Canonicalize coro.suspend by inserting a coro.save if needed.
  for (CoroSuspendInst *CS : CoroSuspends)
    if (!CS->getCoroSave())
      createCoroSave(CoroBegin, CS);

  // Move final suspend to be the last element in the CoroSuspends vector.
  if (HasFinalSuspend &&
      FinalSuspendIndex != CoroSuspends.size() - 1)
    std::swap(CoroSuspends[FinalSuspendIndex], CoroSuspends.back());

  // Remove orphaned coro.saves.
  for (CoroSaveInst *CoroSave : UnusedCoroSaves)
    CoroSave->eraseFromParent();
}

void LLVMAddCoroEarlyPass(LLVMPassManagerRef PM) {
  unwrap(PM)->add(createCoroEarlyPass());
}

void LLVMAddCoroSplitPass(LLVMPassManagerRef PM) {
  unwrap(PM)->add(createCoroSplitPass());
}

void LLVMAddCoroElidePass(LLVMPassManagerRef PM) {
  unwrap(PM)->add(createCoroElidePass());
}

void LLVMAddCoroCleanupPass(LLVMPassManagerRef PM) {
  unwrap(PM)->add(createCoroCleanupPass());
}
