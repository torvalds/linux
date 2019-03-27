//===-- WebAssemblyRegColoring.cpp - Register coloring --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a virtual register coloring pass.
///
/// WebAssembly doesn't have a fixed number of registers, but it is still
/// desirable to minimize the total number of registers used in each function.
///
/// This code is modeled after lib/CodeGen/StackSlotColoring.cpp.
///
//===----------------------------------------------------------------------===//

#include "WebAssembly.h"
#include "WebAssemblyMachineFunctionInfo.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-reg-coloring"

namespace {
class WebAssemblyRegColoring final : public MachineFunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid
  WebAssemblyRegColoring() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "WebAssembly Register Coloring";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<LiveIntervals>();
    AU.addRequired<MachineBlockFrequencyInfo>();
    AU.addPreserved<MachineBlockFrequencyInfo>();
    AU.addPreservedID(MachineDominatorsID);
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

private:
};
} // end anonymous namespace

char WebAssemblyRegColoring::ID = 0;
INITIALIZE_PASS(WebAssemblyRegColoring, DEBUG_TYPE,
                "Minimize number of registers used", false, false)

FunctionPass *llvm::createWebAssemblyRegColoring() {
  return new WebAssemblyRegColoring();
}

// Compute the total spill weight for VReg.
static float computeWeight(const MachineRegisterInfo *MRI,
                           const MachineBlockFrequencyInfo *MBFI,
                           unsigned VReg) {
  float weight = 0.0f;
  for (MachineOperand &MO : MRI->reg_nodbg_operands(VReg))
    weight += LiveIntervals::getSpillWeight(MO.isDef(), MO.isUse(), MBFI,
                                            *MO.getParent());
  return weight;
}

bool WebAssemblyRegColoring::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG({
    dbgs() << "********** Register Coloring **********\n"
           << "********** Function: " << MF.getName() << '\n';
  });

  // If there are calls to setjmp or sigsetjmp, don't perform coloring. Virtual
  // registers could be modified before the longjmp is executed, resulting in
  // the wrong value being used afterwards. (See <rdar://problem/8007500>.)
  // TODO: Does WebAssembly need to care about setjmp for register coloring?
  if (MF.exposesReturnsTwice())
    return false;

  MachineRegisterInfo *MRI = &MF.getRegInfo();
  LiveIntervals *Liveness = &getAnalysis<LiveIntervals>();
  const MachineBlockFrequencyInfo *MBFI =
      &getAnalysis<MachineBlockFrequencyInfo>();
  WebAssemblyFunctionInfo &MFI = *MF.getInfo<WebAssemblyFunctionInfo>();

  // Gather all register intervals into a list and sort them.
  unsigned NumVRegs = MRI->getNumVirtRegs();
  SmallVector<LiveInterval *, 0> SortedIntervals;
  SortedIntervals.reserve(NumVRegs);

  LLVM_DEBUG(dbgs() << "Interesting register intervals:\n");
  for (unsigned i = 0; i < NumVRegs; ++i) {
    unsigned VReg = TargetRegisterInfo::index2VirtReg(i);
    if (MFI.isVRegStackified(VReg))
      continue;
    // Skip unused registers, which can use $drop.
    if (MRI->use_empty(VReg))
      continue;

    LiveInterval *LI = &Liveness->getInterval(VReg);
    assert(LI->weight == 0.0f);
    LI->weight = computeWeight(MRI, MBFI, VReg);
    LLVM_DEBUG(LI->dump());
    SortedIntervals.push_back(LI);
  }
  LLVM_DEBUG(dbgs() << '\n');

  // Sort them to put arguments first (since we don't want to rename live-in
  // registers), by weight next, and then by position.
  // TODO: Investigate more intelligent sorting heuristics. For starters, we
  // should try to coalesce adjacent live intervals before non-adjacent ones.
  llvm::sort(SortedIntervals, [MRI](LiveInterval *LHS, LiveInterval *RHS) {
    if (MRI->isLiveIn(LHS->reg) != MRI->isLiveIn(RHS->reg))
      return MRI->isLiveIn(LHS->reg);
    if (LHS->weight != RHS->weight)
      return LHS->weight > RHS->weight;
    if (LHS->empty() || RHS->empty())
      return !LHS->empty() && RHS->empty();
    return *LHS < *RHS;
  });

  LLVM_DEBUG(dbgs() << "Coloring register intervals:\n");
  SmallVector<unsigned, 16> SlotMapping(SortedIntervals.size(), -1u);
  SmallVector<SmallVector<LiveInterval *, 4>, 16> Assignments(
      SortedIntervals.size());
  BitVector UsedColors(SortedIntervals.size());
  bool Changed = false;
  for (size_t i = 0, e = SortedIntervals.size(); i < e; ++i) {
    LiveInterval *LI = SortedIntervals[i];
    unsigned Old = LI->reg;
    size_t Color = i;
    const TargetRegisterClass *RC = MRI->getRegClass(Old);

    // Check if it's possible to reuse any of the used colors.
    if (!MRI->isLiveIn(Old))
      for (unsigned C : UsedColors.set_bits()) {
        if (MRI->getRegClass(SortedIntervals[C]->reg) != RC)
          continue;
        for (LiveInterval *OtherLI : Assignments[C])
          if (!OtherLI->empty() && OtherLI->overlaps(*LI))
            goto continue_outer;
        Color = C;
        break;
      continue_outer:;
      }

    unsigned New = SortedIntervals[Color]->reg;
    SlotMapping[i] = New;
    Changed |= Old != New;
    UsedColors.set(Color);
    Assignments[Color].push_back(LI);
    LLVM_DEBUG(
        dbgs() << "Assigning vreg" << TargetRegisterInfo::virtReg2Index(LI->reg)
               << " to vreg" << TargetRegisterInfo::virtReg2Index(New) << "\n");
  }
  if (!Changed)
    return false;

  // Rewrite register operands.
  for (size_t i = 0, e = SortedIntervals.size(); i < e; ++i) {
    unsigned Old = SortedIntervals[i]->reg;
    unsigned New = SlotMapping[i];
    if (Old != New)
      MRI->replaceRegWith(Old, New);
  }
  return true;
}
