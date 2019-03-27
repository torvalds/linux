//===--- WebAssemblyOptimizeLiveIntervals.cpp - LiveInterval processing ---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Optimize LiveIntervals for use in a post-RA context.
//
/// LiveIntervals normally runs before register allocation when the code is
/// only recently lowered out of SSA form, so it's uncommon for registers to
/// have multiple defs, and when they do, the defs are usually closely related.
/// Later, after coalescing, tail duplication, and other optimizations, it's
/// more common to see registers with multiple unrelated defs. This pass
/// updates LiveIntervals to distribute the value numbers across separate
/// LiveIntervals.
///
//===----------------------------------------------------------------------===//

#include "WebAssembly.h"
#include "WebAssemblySubtarget.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-optimize-live-intervals"

namespace {
class WebAssemblyOptimizeLiveIntervals final : public MachineFunctionPass {
  StringRef getPassName() const override {
    return "WebAssembly Optimize Live Intervals";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<LiveIntervals>();
    AU.addPreserved<MachineBlockFrequencyInfo>();
    AU.addPreserved<SlotIndexes>();
    AU.addPreserved<LiveIntervals>();
    AU.addPreservedID(LiveVariablesID);
    AU.addPreservedID(MachineDominatorsID);
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

public:
  static char ID; // Pass identification, replacement for typeid
  WebAssemblyOptimizeLiveIntervals() : MachineFunctionPass(ID) {}
};
} // end anonymous namespace

char WebAssemblyOptimizeLiveIntervals::ID = 0;
INITIALIZE_PASS(WebAssemblyOptimizeLiveIntervals, DEBUG_TYPE,
                "Optimize LiveIntervals for WebAssembly", false, false)

FunctionPass *llvm::createWebAssemblyOptimizeLiveIntervals() {
  return new WebAssemblyOptimizeLiveIntervals();
}

bool WebAssemblyOptimizeLiveIntervals::runOnMachineFunction(
    MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "********** Optimize LiveIntervals **********\n"
                       "********** Function: "
                    << MF.getName() << '\n');

  MachineRegisterInfo &MRI = MF.getRegInfo();
  LiveIntervals &LIS = getAnalysis<LiveIntervals>();

  // We don't preserve SSA form.
  MRI.leaveSSA();

  assert(MRI.tracksLiveness() && "OptimizeLiveIntervals expects liveness");

  // Split multiple-VN LiveIntervals into multiple LiveIntervals.
  SmallVector<LiveInterval *, 4> SplitLIs;
  for (unsigned i = 0, e = MRI.getNumVirtRegs(); i < e; ++i) {
    unsigned Reg = TargetRegisterInfo::index2VirtReg(i);
    if (MRI.reg_nodbg_empty(Reg))
      continue;

    LIS.splitSeparateComponents(LIS.getInterval(Reg), SplitLIs);
    SplitLIs.clear();
  }

  // In PrepareForLiveIntervals, we conservatively inserted IMPLICIT_DEF
  // instructions to satisfy LiveIntervals' requirement that all uses be
  // dominated by defs. Now that LiveIntervals has computed which of these
  // defs are actually needed and which are dead, remove the dead ones.
  for (auto MII = MF.begin()->begin(), MIE = MF.begin()->end(); MII != MIE;) {
    MachineInstr *MI = &*MII++;
    if (MI->isImplicitDef() && MI->getOperand(0).isDead()) {
      LiveInterval &LI = LIS.getInterval(MI->getOperand(0).getReg());
      LIS.removeVRegDefAt(LI, LIS.getInstructionIndex(*MI).getRegSlot());
      LIS.RemoveMachineInstrFromMaps(*MI);
      MI->eraseFromParent();
    }
  }

  return false;
}
