//===- lib/Codegen/MachineRegionInfo.cpp ----------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineRegionInfo.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/RegionInfoImpl.h"
#include "llvm/CodeGen/MachinePostDominators.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "machine-region-info"

using namespace llvm;

STATISTIC(numMachineRegions,       "The # of machine regions");
STATISTIC(numMachineSimpleRegions, "The # of simple machine regions");

namespace llvm {

template class RegionBase<RegionTraits<MachineFunction>>;
template class RegionNodeBase<RegionTraits<MachineFunction>>;
template class RegionInfoBase<RegionTraits<MachineFunction>>;

} // end namespace llvm

//===----------------------------------------------------------------------===//
// MachineRegion implementation

MachineRegion::MachineRegion(MachineBasicBlock *Entry, MachineBasicBlock *Exit,
                             MachineRegionInfo* RI,
                             MachineDominatorTree *DT, MachineRegion *Parent) :
  RegionBase<RegionTraits<MachineFunction>>(Entry, Exit, RI, DT, Parent) {}

MachineRegion::~MachineRegion() = default;

//===----------------------------------------------------------------------===//
// MachineRegionInfo implementation

MachineRegionInfo::MachineRegionInfo() = default;

MachineRegionInfo::~MachineRegionInfo() = default;

void MachineRegionInfo::updateStatistics(MachineRegion *R) {
  ++numMachineRegions;

  // TODO: Slow. Should only be enabled if -stats is used.
  if (R->isSimple())
    ++numMachineSimpleRegions;
}

void MachineRegionInfo::recalculate(MachineFunction &F,
                                    MachineDominatorTree *DT_,
                                    MachinePostDominatorTree *PDT_,
                                    MachineDominanceFrontier *DF_) {
  DT = DT_;
  PDT = PDT_;
  DF = DF_;

  MachineBasicBlock *Entry = GraphTraits<MachineFunction*>::getEntryNode(&F);

  TopLevelRegion = new MachineRegion(Entry, nullptr, this, DT, nullptr);
  updateStatistics(TopLevelRegion);
  calculate(F);
}

//===----------------------------------------------------------------------===//
// MachineRegionInfoPass implementation
//

MachineRegionInfoPass::MachineRegionInfoPass() : MachineFunctionPass(ID) {
  initializeMachineRegionInfoPassPass(*PassRegistry::getPassRegistry());
}

MachineRegionInfoPass::~MachineRegionInfoPass() = default;

bool MachineRegionInfoPass::runOnMachineFunction(MachineFunction &F) {
  releaseMemory();

  auto DT = &getAnalysis<MachineDominatorTree>();
  auto PDT = &getAnalysis<MachinePostDominatorTree>();
  auto DF = &getAnalysis<MachineDominanceFrontier>();

  RI.recalculate(F, DT, PDT, DF);

  LLVM_DEBUG(RI.dump());

  return false;
}

void MachineRegionInfoPass::releaseMemory() {
  RI.releaseMemory();
}

void MachineRegionInfoPass::verifyAnalysis() const {
  // Only do verification when user wants to, otherwise this expensive check
  // will be invoked by PMDataManager::verifyPreservedAnalysis when
  // a regionpass (marked PreservedAll) finish.
  if (MachineRegionInfo::VerifyRegionInfo)
    RI.verifyAnalysis();
}

void MachineRegionInfoPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<MachineDominatorTree>();
  AU.addRequired<MachinePostDominatorTree>();
  AU.addRequired<MachineDominanceFrontier>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

void MachineRegionInfoPass::print(raw_ostream &OS, const Module *) const {
  RI.print(OS);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void MachineRegionInfoPass::dump() const {
  RI.dump();
}
#endif

char MachineRegionInfoPass::ID = 0;
char &MachineRegionInfoPassID = MachineRegionInfoPass::ID;

INITIALIZE_PASS_BEGIN(MachineRegionInfoPass, DEBUG_TYPE,
                      "Detect single entry single exit regions", true, true)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)
INITIALIZE_PASS_DEPENDENCY(MachinePostDominatorTree)
INITIALIZE_PASS_DEPENDENCY(MachineDominanceFrontier)
INITIALIZE_PASS_END(MachineRegionInfoPass, DEBUG_TYPE,
                    "Detect single entry single exit regions", true, true)

// Create methods available outside of this file, to use them
// "include/llvm/LinkAllPasses.h". Otherwise the pass would be deleted by
// the link time optimization.

namespace llvm {

FunctionPass *createMachineRegionInfoPass() {
  return new MachineRegionInfoPass();
}

} // end namespace llvm
