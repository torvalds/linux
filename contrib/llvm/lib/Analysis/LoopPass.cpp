//===- LoopPass.cpp - Loop Pass and Loop Pass Manager ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements LoopPass and LPPassManager. All loop optimization
// and transformation passes are derived from LoopPass. LPPassManager is
// responsible for managing LoopPasses.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/OptBisect.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/PassTimingInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "loop-pass-manager"

namespace {

/// PrintLoopPass - Print a Function corresponding to a Loop.
///
class PrintLoopPassWrapper : public LoopPass {
  raw_ostream &OS;
  std::string Banner;

public:
  static char ID;
  PrintLoopPassWrapper() : LoopPass(ID), OS(dbgs()) {}
  PrintLoopPassWrapper(raw_ostream &OS, const std::string &Banner)
      : LoopPass(ID), OS(OS), Banner(Banner) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }

  bool runOnLoop(Loop *L, LPPassManager &) override {
    auto BBI = llvm::find_if(L->blocks(), [](BasicBlock *BB) { return BB; });
    if (BBI != L->blocks().end() &&
        isFunctionInPrintList((*BBI)->getParent()->getName())) {
      printLoop(*L, OS, Banner);
    }
    return false;
  }

  StringRef getPassName() const override { return "Print Loop IR"; }
};

char PrintLoopPassWrapper::ID = 0;
}

//===----------------------------------------------------------------------===//
// LPPassManager
//

char LPPassManager::ID = 0;

LPPassManager::LPPassManager()
  : FunctionPass(ID), PMDataManager() {
  LI = nullptr;
  CurrentLoop = nullptr;
}

// Insert loop into loop nest (LoopInfo) and loop queue (LQ).
void LPPassManager::addLoop(Loop &L) {
  if (!L.getParentLoop()) {
    // This is the top level loop.
    LQ.push_front(&L);
    return;
  }

  // Insert L into the loop queue after the parent loop.
  for (auto I = LQ.begin(), E = LQ.end(); I != E; ++I) {
    if (*I == L.getParentLoop()) {
      // deque does not support insert after.
      ++I;
      LQ.insert(I, 1, &L);
      return;
    }
  }
}

/// cloneBasicBlockSimpleAnalysis - Invoke cloneBasicBlockAnalysis hook for
/// all loop passes.
void LPPassManager::cloneBasicBlockSimpleAnalysis(BasicBlock *From,
                                                  BasicBlock *To, Loop *L) {
  for (unsigned Index = 0; Index < getNumContainedPasses(); ++Index) {
    LoopPass *LP = getContainedPass(Index);
    LP->cloneBasicBlockAnalysis(From, To, L);
  }
}

/// deleteSimpleAnalysisValue - Invoke deleteAnalysisValue hook for all passes.
void LPPassManager::deleteSimpleAnalysisValue(Value *V, Loop *L) {
  if (BasicBlock *BB = dyn_cast<BasicBlock>(V)) {
    for (Instruction &I : *BB) {
      deleteSimpleAnalysisValue(&I, L);
    }
  }
  for (unsigned Index = 0; Index < getNumContainedPasses(); ++Index) {
    LoopPass *LP = getContainedPass(Index);
    LP->deleteAnalysisValue(V, L);
  }
}

/// Invoke deleteAnalysisLoop hook for all passes.
void LPPassManager::deleteSimpleAnalysisLoop(Loop *L) {
  for (unsigned Index = 0; Index < getNumContainedPasses(); ++Index) {
    LoopPass *LP = getContainedPass(Index);
    LP->deleteAnalysisLoop(L);
  }
}


// Recurse through all subloops and all loops  into LQ.
static void addLoopIntoQueue(Loop *L, std::deque<Loop *> &LQ) {
  LQ.push_back(L);
  for (Loop *I : reverse(*L))
    addLoopIntoQueue(I, LQ);
}

/// Pass Manager itself does not invalidate any analysis info.
void LPPassManager::getAnalysisUsage(AnalysisUsage &Info) const {
  // LPPassManager needs LoopInfo. In the long term LoopInfo class will
  // become part of LPPassManager.
  Info.addRequired<LoopInfoWrapperPass>();
  Info.addRequired<DominatorTreeWrapperPass>();
  Info.setPreservesAll();
}

void LPPassManager::markLoopAsDeleted(Loop &L) {
  assert((&L == CurrentLoop || CurrentLoop->contains(&L)) &&
         "Must not delete loop outside the current loop tree!");
  // If this loop appears elsewhere within the queue, we also need to remove it
  // there. However, we have to be careful to not remove the back of the queue
  // as that is assumed to match the current loop.
  assert(LQ.back() == CurrentLoop && "Loop queue back isn't the current loop!");
  LQ.erase(std::remove(LQ.begin(), LQ.end(), &L), LQ.end());

  if (&L == CurrentLoop) {
    CurrentLoopDeleted = true;
    // Add this loop back onto the back of the queue to preserve our invariants.
    LQ.push_back(&L);
  }
}

/// run - Execute all of the passes scheduled for execution.  Keep track of
/// whether any of the passes modifies the function, and if so, return true.
bool LPPassManager::runOnFunction(Function &F) {
  auto &LIWP = getAnalysis<LoopInfoWrapperPass>();
  LI = &LIWP.getLoopInfo();
  Module &M = *F.getParent();
#if 0
  DominatorTree *DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
#endif
  bool Changed = false;

  // Collect inherited analysis from Module level pass manager.
  populateInheritedAnalysis(TPM->activeStack);

  // Populate the loop queue in reverse program order. There is no clear need to
  // process sibling loops in either forward or reverse order. There may be some
  // advantage in deleting uses in a later loop before optimizing the
  // definitions in an earlier loop. If we find a clear reason to process in
  // forward order, then a forward variant of LoopPassManager should be created.
  //
  // Note that LoopInfo::iterator visits loops in reverse program
  // order. Here, reverse_iterator gives us a forward order, and the LoopQueue
  // reverses the order a third time by popping from the back.
  for (Loop *L : reverse(*LI))
    addLoopIntoQueue(L, LQ);

  if (LQ.empty()) // No loops, skip calling finalizers
    return false;

  // Initialization
  for (Loop *L : LQ) {
    for (unsigned Index = 0; Index < getNumContainedPasses(); ++Index) {
      LoopPass *P = getContainedPass(Index);
      Changed |= P->doInitialization(L, *this);
    }
  }

  // Walk Loops
  unsigned InstrCount, FunctionSize = 0;
  StringMap<std::pair<unsigned, unsigned>> FunctionToInstrCount;
  bool EmitICRemark = M.shouldEmitInstrCountChangedRemark();
  // Collect the initial size of the module and the function we're looking at.
  if (EmitICRemark) {
    InstrCount = initSizeRemarkInfo(M, FunctionToInstrCount);
    FunctionSize = F.getInstructionCount();
  }
  while (!LQ.empty()) {
    CurrentLoopDeleted = false;
    CurrentLoop = LQ.back();

    // Run all passes on the current Loop.
    for (unsigned Index = 0; Index < getNumContainedPasses(); ++Index) {
      LoopPass *P = getContainedPass(Index);

      dumpPassInfo(P, EXECUTION_MSG, ON_LOOP_MSG,
                   CurrentLoop->getHeader()->getName());
      dumpRequiredSet(P);

      initializeAnalysisImpl(P);

      bool LocalChanged = false;
      {
        PassManagerPrettyStackEntry X(P, *CurrentLoop->getHeader());
        TimeRegion PassTimer(getPassTimer(P));
        LocalChanged = P->runOnLoop(CurrentLoop, *this);
        Changed |= LocalChanged;
        if (EmitICRemark) {
          unsigned NewSize = F.getInstructionCount();
          // Update the size of the function, emit a remark, and update the
          // size of the module.
          if (NewSize != FunctionSize) {
            int64_t Delta = static_cast<int64_t>(NewSize) -
                            static_cast<int64_t>(FunctionSize);
            emitInstrCountChangedRemark(P, M, Delta, InstrCount,
                                        FunctionToInstrCount, &F);
            InstrCount = static_cast<int64_t>(InstrCount) + Delta;
            FunctionSize = NewSize;
          }
        }
      }

      if (LocalChanged)
        dumpPassInfo(P, MODIFICATION_MSG, ON_LOOP_MSG,
                     CurrentLoopDeleted ? "<deleted loop>"
                                        : CurrentLoop->getName());
      dumpPreservedSet(P);

      if (CurrentLoopDeleted) {
        // Notify passes that the loop is being deleted.
        deleteSimpleAnalysisLoop(CurrentLoop);
      } else {
        // Manually check that this loop is still healthy. This is done
        // instead of relying on LoopInfo::verifyLoop since LoopInfo
        // is a function pass and it's really expensive to verify every
        // loop in the function every time. That level of checking can be
        // enabled with the -verify-loop-info option.
        {
          TimeRegion PassTimer(getPassTimer(&LIWP));
          CurrentLoop->verifyLoop();
        }
        // Here we apply same reasoning as in the above case. Only difference
        // is that LPPassManager might run passes which do not require LCSSA
        // form (LoopPassPrinter for example). We should skip verification for
        // such passes.
        // FIXME: Loop-sink currently break LCSSA. Fix it and reenable the
        // verification!
#if 0
        if (mustPreserveAnalysisID(LCSSAVerificationPass::ID))
          assert(CurrentLoop->isRecursivelyLCSSAForm(*DT, *LI));
#endif

        // Then call the regular verifyAnalysis functions.
        verifyPreservedAnalysis(P);

        F.getContext().yield();
      }

      removeNotPreservedAnalysis(P);
      recordAvailableAnalysis(P);
      removeDeadPasses(P,
                       CurrentLoopDeleted ? "<deleted>"
                                          : CurrentLoop->getHeader()->getName(),
                       ON_LOOP_MSG);

      if (CurrentLoopDeleted)
        // Do not run other passes on this loop.
        break;
    }

    // If the loop was deleted, release all the loop passes. This frees up
    // some memory, and avoids trouble with the pass manager trying to call
    // verifyAnalysis on them.
    if (CurrentLoopDeleted) {
      for (unsigned Index = 0; Index < getNumContainedPasses(); ++Index) {
        Pass *P = getContainedPass(Index);
        freePass(P, "<deleted>", ON_LOOP_MSG);
      }
    }

    // Pop the loop from queue after running all passes.
    LQ.pop_back();
  }

  // Finalization
  for (unsigned Index = 0; Index < getNumContainedPasses(); ++Index) {
    LoopPass *P = getContainedPass(Index);
    Changed |= P->doFinalization();
  }

  return Changed;
}

/// Print passes managed by this manager
void LPPassManager::dumpPassStructure(unsigned Offset) {
  errs().indent(Offset*2) << "Loop Pass Manager\n";
  for (unsigned Index = 0; Index < getNumContainedPasses(); ++Index) {
    Pass *P = getContainedPass(Index);
    P->dumpPassStructure(Offset + 1);
    dumpLastUses(P, Offset+1);
  }
}


//===----------------------------------------------------------------------===//
// LoopPass

Pass *LoopPass::createPrinterPass(raw_ostream &O,
                                  const std::string &Banner) const {
  return new PrintLoopPassWrapper(O, Banner);
}

// Check if this pass is suitable for the current LPPassManager, if
// available. This pass P is not suitable for a LPPassManager if P
// is not preserving higher level analysis info used by other
// LPPassManager passes. In such case, pop LPPassManager from the
// stack. This will force assignPassManager() to create new
// LPPassManger as expected.
void LoopPass::preparePassManager(PMStack &PMS) {

  // Find LPPassManager
  while (!PMS.empty() &&
         PMS.top()->getPassManagerType() > PMT_LoopPassManager)
    PMS.pop();

  // If this pass is destroying high level information that is used
  // by other passes that are managed by LPM then do not insert
  // this pass in current LPM. Use new LPPassManager.
  if (PMS.top()->getPassManagerType() == PMT_LoopPassManager &&
      !PMS.top()->preserveHigherLevelAnalysis(this))
    PMS.pop();
}

/// Assign pass manager to manage this pass.
void LoopPass::assignPassManager(PMStack &PMS,
                                 PassManagerType PreferredType) {
  // Find LPPassManager
  while (!PMS.empty() &&
         PMS.top()->getPassManagerType() > PMT_LoopPassManager)
    PMS.pop();

  LPPassManager *LPPM;
  if (PMS.top()->getPassManagerType() == PMT_LoopPassManager)
    LPPM = (LPPassManager*)PMS.top();
  else {
    // Create new Loop Pass Manager if it does not exist.
    assert (!PMS.empty() && "Unable to create Loop Pass Manager");
    PMDataManager *PMD = PMS.top();

    // [1] Create new Loop Pass Manager
    LPPM = new LPPassManager();
    LPPM->populateInheritedAnalysis(PMS);

    // [2] Set up new manager's top level manager
    PMTopLevelManager *TPM = PMD->getTopLevelManager();
    TPM->addIndirectPassManager(LPPM);

    // [3] Assign manager to manage this new manager. This may create
    // and push new managers into PMS
    Pass *P = LPPM->getAsPass();
    TPM->schedulePass(P);

    // [4] Push new manager into PMS
    PMS.push(LPPM);
  }

  LPPM->add(this);
}

bool LoopPass::skipLoop(const Loop *L) const {
  const Function *F = L->getHeader()->getParent();
  if (!F)
    return false;
  // Check the opt bisect limit.
  LLVMContext &Context = F->getContext();
  if (!Context.getOptPassGate().shouldRunPass(this, *L))
    return true;
  // Check for the OptimizeNone attribute.
  if (F->hasFnAttribute(Attribute::OptimizeNone)) {
    // FIXME: Report this to dbgs() only once per function.
    LLVM_DEBUG(dbgs() << "Skipping pass '" << getPassName() << "' in function "
                      << F->getName() << "\n");
    // FIXME: Delete loop from pass manager's queue?
    return true;
  }
  return false;
}

char LCSSAVerificationPass::ID = 0;
INITIALIZE_PASS(LCSSAVerificationPass, "lcssa-verification", "LCSSA Verifier",
                false, false)
