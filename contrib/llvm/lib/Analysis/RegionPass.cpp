//===- RegionPass.cpp - Region Pass and Region Pass Manager ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements RegionPass and RGPassManager. All region optimization
// and transformation passes are derived from RegionPass. RGPassManager is
// responsible for managing RegionPasses.
// Most of this code has been COPIED from LoopPass.cpp
//
//===----------------------------------------------------------------------===//
#include "llvm/Analysis/RegionPass.h"
#include "llvm/IR/OptBisect.h"
#include "llvm/IR/PassTimingInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "regionpassmgr"

//===----------------------------------------------------------------------===//
// RGPassManager
//

char RGPassManager::ID = 0;

RGPassManager::RGPassManager()
  : FunctionPass(ID), PMDataManager() {
  skipThisRegion = false;
  redoThisRegion = false;
  RI = nullptr;
  CurrentRegion = nullptr;
}

// Recurse through all subregions and all regions  into RQ.
static void addRegionIntoQueue(Region &R, std::deque<Region *> &RQ) {
  RQ.push_back(&R);
  for (const auto &E : R)
    addRegionIntoQueue(*E, RQ);
}

/// Pass Manager itself does not invalidate any analysis info.
void RGPassManager::getAnalysisUsage(AnalysisUsage &Info) const {
  Info.addRequired<RegionInfoPass>();
  Info.setPreservesAll();
}

/// run - Execute all of the passes scheduled for execution.  Keep track of
/// whether any of the passes modifies the function, and if so, return true.
bool RGPassManager::runOnFunction(Function &F) {
  RI = &getAnalysis<RegionInfoPass>().getRegionInfo();
  bool Changed = false;

  // Collect inherited analysis from Module level pass manager.
  populateInheritedAnalysis(TPM->activeStack);

  addRegionIntoQueue(*RI->getTopLevelRegion(), RQ);

  if (RQ.empty()) // No regions, skip calling finalizers
    return false;

  // Initialization
  for (Region *R : RQ) {
    for (unsigned Index = 0; Index < getNumContainedPasses(); ++Index) {
      RegionPass *RP = (RegionPass *)getContainedPass(Index);
      Changed |= RP->doInitialization(R, *this);
    }
  }

  // Walk Regions
  while (!RQ.empty()) {

    CurrentRegion  = RQ.back();
    skipThisRegion = false;
    redoThisRegion = false;

    // Run all passes on the current Region.
    for (unsigned Index = 0; Index < getNumContainedPasses(); ++Index) {
      RegionPass *P = (RegionPass*)getContainedPass(Index);

      if (isPassDebuggingExecutionsOrMore()) {
        dumpPassInfo(P, EXECUTION_MSG, ON_REGION_MSG,
                     CurrentRegion->getNameStr());
        dumpRequiredSet(P);
      }

      initializeAnalysisImpl(P);

      {
        PassManagerPrettyStackEntry X(P, *CurrentRegion->getEntry());

        TimeRegion PassTimer(getPassTimer(P));
        Changed |= P->runOnRegion(CurrentRegion, *this);
      }

      if (isPassDebuggingExecutionsOrMore()) {
        if (Changed)
          dumpPassInfo(P, MODIFICATION_MSG, ON_REGION_MSG,
                       skipThisRegion ? "<deleted>" :
                                      CurrentRegion->getNameStr());
        dumpPreservedSet(P);
      }

      if (!skipThisRegion) {
        // Manually check that this region is still healthy. This is done
        // instead of relying on RegionInfo::verifyRegion since RegionInfo
        // is a function pass and it's really expensive to verify every
        // Region in the function every time. That level of checking can be
        // enabled with the -verify-region-info option.
        {
          TimeRegion PassTimer(getPassTimer(P));
          CurrentRegion->verifyRegion();
        }

        // Then call the regular verifyAnalysis functions.
        verifyPreservedAnalysis(P);
      }

      removeNotPreservedAnalysis(P);
      recordAvailableAnalysis(P);
      removeDeadPasses(P,
                       (!isPassDebuggingExecutionsOrMore() || skipThisRegion) ?
                       "<deleted>" :  CurrentRegion->getNameStr(),
                       ON_REGION_MSG);

      if (skipThisRegion)
        // Do not run other passes on this region.
        break;
    }

    // If the region was deleted, release all the region passes. This frees up
    // some memory, and avoids trouble with the pass manager trying to call
    // verifyAnalysis on them.
    if (skipThisRegion)
      for (unsigned Index = 0; Index < getNumContainedPasses(); ++Index) {
        Pass *P = getContainedPass(Index);
        freePass(P, "<deleted>", ON_REGION_MSG);
      }

    // Pop the region from queue after running all passes.
    RQ.pop_back();

    if (redoThisRegion)
      RQ.push_back(CurrentRegion);

    // Free all region nodes created in region passes.
    RI->clearNodeCache();
  }

  // Finalization
  for (unsigned Index = 0; Index < getNumContainedPasses(); ++Index) {
    RegionPass *P = (RegionPass*)getContainedPass(Index);
    Changed |= P->doFinalization();
  }

  // Print the region tree after all pass.
  LLVM_DEBUG(dbgs() << "\nRegion tree of function " << F.getName()
                    << " after all region Pass:\n";
             RI->dump(); dbgs() << "\n";);

  return Changed;
}

/// Print passes managed by this manager
void RGPassManager::dumpPassStructure(unsigned Offset) {
  errs().indent(Offset*2) << "Region Pass Manager\n";
  for (unsigned Index = 0; Index < getNumContainedPasses(); ++Index) {
    Pass *P = getContainedPass(Index);
    P->dumpPassStructure(Offset + 1);
    dumpLastUses(P, Offset+1);
  }
}

namespace {
//===----------------------------------------------------------------------===//
// PrintRegionPass
class PrintRegionPass : public RegionPass {
private:
  std::string Banner;
  raw_ostream &Out;       // raw_ostream to print on.

public:
  static char ID;
  PrintRegionPass(const std::string &B, raw_ostream &o)
      : RegionPass(ID), Banner(B), Out(o) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }

  bool runOnRegion(Region *R, RGPassManager &RGM) override {
    Out << Banner;
    for (const auto *BB : R->blocks()) {
      if (BB)
        BB->print(Out);
      else
        Out << "Printing <null> Block";
    }

    return false;
  }

  StringRef getPassName() const override { return "Print Region IR"; }
};

char PrintRegionPass::ID = 0;
}  //end anonymous namespace

//===----------------------------------------------------------------------===//
// RegionPass

// Check if this pass is suitable for the current RGPassManager, if
// available. This pass P is not suitable for a RGPassManager if P
// is not preserving higher level analysis info used by other
// RGPassManager passes. In such case, pop RGPassManager from the
// stack. This will force assignPassManager() to create new
// LPPassManger as expected.
void RegionPass::preparePassManager(PMStack &PMS) {

  // Find RGPassManager
  while (!PMS.empty() &&
         PMS.top()->getPassManagerType() > PMT_RegionPassManager)
    PMS.pop();


  // If this pass is destroying high level information that is used
  // by other passes that are managed by LPM then do not insert
  // this pass in current LPM. Use new RGPassManager.
  if (PMS.top()->getPassManagerType() == PMT_RegionPassManager &&
    !PMS.top()->preserveHigherLevelAnalysis(this))
    PMS.pop();
}

/// Assign pass manager to manage this pass.
void RegionPass::assignPassManager(PMStack &PMS,
                                 PassManagerType PreferredType) {
  // Find RGPassManager
  while (!PMS.empty() &&
         PMS.top()->getPassManagerType() > PMT_RegionPassManager)
    PMS.pop();

  RGPassManager *RGPM;

  // Create new Region Pass Manager if it does not exist.
  if (PMS.top()->getPassManagerType() == PMT_RegionPassManager)
    RGPM = (RGPassManager*)PMS.top();
  else {

    assert (!PMS.empty() && "Unable to create Region Pass Manager");
    PMDataManager *PMD = PMS.top();

    // [1] Create new Region Pass Manager
    RGPM = new RGPassManager();
    RGPM->populateInheritedAnalysis(PMS);

    // [2] Set up new manager's top level manager
    PMTopLevelManager *TPM = PMD->getTopLevelManager();
    TPM->addIndirectPassManager(RGPM);

    // [3] Assign manager to manage this new manager. This may create
    // and push new managers into PMS
    TPM->schedulePass(RGPM);

    // [4] Push new manager into PMS
    PMS.push(RGPM);
  }

  RGPM->add(this);
}

/// Get the printer pass
Pass *RegionPass::createPrinterPass(raw_ostream &O,
                                  const std::string &Banner) const {
  return new PrintRegionPass(Banner, O);
}

bool RegionPass::skipRegion(Region &R) const {
  Function &F = *R.getEntry()->getParent();
  if (!F.getContext().getOptPassGate().shouldRunPass(this, R))
    return true;

  if (F.hasFnAttribute(Attribute::OptimizeNone)) {
    // Report this only once per function.
    if (R.getEntry() == &F.getEntryBlock())
      LLVM_DEBUG(dbgs() << "Skipping pass '" << getPassName()
                        << "' on function " << F.getName() << "\n");
    return true;
  }
  return false;
}
