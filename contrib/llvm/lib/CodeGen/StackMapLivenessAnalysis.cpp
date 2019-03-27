//===-- StackMapLivenessAnalysis.cpp - StackMap live Out Analysis ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the StackMap Liveness analysis pass. The pass calculates
// the liveness for each basic block in a function and attaches the register
// live-out information to a stackmap or patchpoint intrinsic if present.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "stackmaps"

static cl::opt<bool> EnablePatchPointLiveness(
    "enable-patchpoint-liveness", cl::Hidden, cl::init(true),
    cl::desc("Enable PatchPoint Liveness Analysis Pass"));

STATISTIC(NumStackMapFuncVisited, "Number of functions visited");
STATISTIC(NumStackMapFuncSkipped, "Number of functions skipped");
STATISTIC(NumBBsVisited,          "Number of basic blocks visited");
STATISTIC(NumBBsHaveNoStackmap,   "Number of basic blocks with no stackmap");
STATISTIC(NumStackMaps,           "Number of StackMaps visited");

namespace {
/// This pass calculates the liveness information for each basic block in
/// a function and attaches the register live-out information to a patchpoint
/// intrinsic if present.
///
/// This pass can be disabled via the -enable-patchpoint-liveness=false flag.
/// The pass skips functions that don't have any patchpoint intrinsics. The
/// information provided by this pass is optional and not required by the
/// aformentioned intrinsic to function.
class StackMapLiveness : public MachineFunctionPass {
  const TargetRegisterInfo *TRI;
  LivePhysRegs LiveRegs;

public:
  static char ID;

  /// Default construct and initialize the pass.
  StackMapLiveness();

  /// Tell the pass manager which passes we depend on and what
  /// information we preserve.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoVRegs);
  }

  /// Calculate the liveness information for the given machine function.
  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  /// Performs the actual liveness calculation for the function.
  bool calculateLiveness(MachineFunction &MF);

  /// Add the current register live set to the instruction.
  void addLiveOutSetToMI(MachineFunction &MF, MachineInstr &MI);

  /// Create a register mask and initialize it with the registers from
  /// the register live set.
  uint32_t *createRegisterMask(MachineFunction &MF) const;
};
} // namespace

char StackMapLiveness::ID = 0;
char &llvm::StackMapLivenessID = StackMapLiveness::ID;
INITIALIZE_PASS(StackMapLiveness, "stackmap-liveness",
                "StackMap Liveness Analysis", false, false)

/// Default construct and initialize the pass.
StackMapLiveness::StackMapLiveness() : MachineFunctionPass(ID) {
  initializeStackMapLivenessPass(*PassRegistry::getPassRegistry());
}

/// Tell the pass manager which passes we depend on and what information we
/// preserve.
void StackMapLiveness::getAnalysisUsage(AnalysisUsage &AU) const {
  // We preserve all information.
  AU.setPreservesAll();
  AU.setPreservesCFG();
  MachineFunctionPass::getAnalysisUsage(AU);
}

/// Calculate the liveness information for the given machine function.
bool StackMapLiveness::runOnMachineFunction(MachineFunction &MF) {
  if (!EnablePatchPointLiveness)
    return false;

  LLVM_DEBUG(dbgs() << "********** COMPUTING STACKMAP LIVENESS: "
                    << MF.getName() << " **********\n");
  TRI = MF.getSubtarget().getRegisterInfo();
  ++NumStackMapFuncVisited;

  // Skip this function if there are no patchpoints to process.
  if (!MF.getFrameInfo().hasPatchPoint()) {
    ++NumStackMapFuncSkipped;
    return false;
  }
  return calculateLiveness(MF);
}

/// Performs the actual liveness calculation for the function.
bool StackMapLiveness::calculateLiveness(MachineFunction &MF) {
  bool HasChanged = false;
  // For all basic blocks in the function.
  for (auto &MBB : MF) {
    LLVM_DEBUG(dbgs() << "****** BB " << MBB.getName() << " ******\n");
    LiveRegs.init(*TRI);
    // FIXME: This should probably be addLiveOuts().
    LiveRegs.addLiveOutsNoPristines(MBB);
    bool HasStackMap = false;
    // Reverse iterate over all instructions and add the current live register
    // set to an instruction if we encounter a patchpoint instruction.
    for (auto I = MBB.rbegin(), E = MBB.rend(); I != E; ++I) {
      if (I->getOpcode() == TargetOpcode::PATCHPOINT) {
        addLiveOutSetToMI(MF, *I);
        HasChanged = true;
        HasStackMap = true;
        ++NumStackMaps;
      }
      LLVM_DEBUG(dbgs() << "   " << LiveRegs << "   " << *I);
      LiveRegs.stepBackward(*I);
    }
    ++NumBBsVisited;
    if (!HasStackMap)
      ++NumBBsHaveNoStackmap;
  }
  return HasChanged;
}

/// Add the current register live set to the instruction.
void StackMapLiveness::addLiveOutSetToMI(MachineFunction &MF,
                                         MachineInstr &MI) {
  uint32_t *Mask = createRegisterMask(MF);
  MachineOperand MO = MachineOperand::CreateRegLiveOut(Mask);
  MI.addOperand(MF, MO);
}

/// Create a register mask and initialize it with the registers from the
/// register live set.
uint32_t *StackMapLiveness::createRegisterMask(MachineFunction &MF) const {
  // The mask is owned and cleaned up by the Machine Function.
  uint32_t *Mask = MF.allocateRegMask();
  for (auto Reg : LiveRegs)
    Mask[Reg / 32] |= 1U << (Reg % 32);

  // Give the target a chance to adjust the mask.
  TRI->adjustStackMapLiveOutMask(Mask);

  return Mask;
}
