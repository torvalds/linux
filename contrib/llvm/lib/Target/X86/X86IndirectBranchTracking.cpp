//===---- X86IndirectBranchTracking.cpp - Enables CET IBT mechanism -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a pass that enables Indirect Branch Tracking (IBT) as part
// of Control-Flow Enforcement Technology (CET).
// The pass adds ENDBR (End Branch) machine instructions at the beginning of
// each basic block or function that is referenced by an indrect jump/call
// instruction.
// The ENDBR instructions have a NOP encoding and as such are ignored in
// targets that do not support CET IBT mechanism.
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86InstrInfo.h"
#include "X86Subtarget.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"

using namespace llvm;

#define DEBUG_TYPE "x86-indirect-branch-tracking"

static cl::opt<bool> IndirectBranchTracking(
    "x86-indirect-branch-tracking", cl::init(false), cl::Hidden,
    cl::desc("Enable X86 indirect branch tracking pass."));

STATISTIC(NumEndBranchAdded, "Number of ENDBR instructions added");

namespace {
class X86IndirectBranchTrackingPass : public MachineFunctionPass {
public:
  X86IndirectBranchTrackingPass() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "X86 Indirect Branch Tracking";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  static char ID;

  /// Machine instruction info used throughout the class.
  const X86InstrInfo *TII;

  /// Endbr opcode for the current machine function.
  unsigned int EndbrOpcode;

  /// Adds a new ENDBR instruction to the begining of the MBB.
  /// The function will not add it if already exists.
  /// It will add ENDBR32 or ENDBR64 opcode, depending on the target.
  /// \returns true if the ENDBR was added and false otherwise.
  bool addENDBR(MachineBasicBlock &MBB) const;
};

} // end anonymous namespace

char X86IndirectBranchTrackingPass::ID = 0;

FunctionPass *llvm::createX86IndirectBranchTrackingPass() {
  return new X86IndirectBranchTrackingPass();
}

bool X86IndirectBranchTrackingPass::addENDBR(MachineBasicBlock &MBB) const {
  assert(TII && "Target instruction info was not initialized");
  assert((X86::ENDBR64 == EndbrOpcode || X86::ENDBR32 == EndbrOpcode) &&
         "Unexpected Endbr opcode");

  auto MI = MBB.begin();
  // If the MBB is empty or the first instruction is not ENDBR,
  // add the ENDBR instruction to the beginning of the MBB.
  if (MI == MBB.end() || EndbrOpcode != MI->getOpcode()) {
    BuildMI(MBB, MI, MBB.findDebugLoc(MI), TII->get(EndbrOpcode));
    NumEndBranchAdded++;
    return true;
  }

  return false;
}

bool X86IndirectBranchTrackingPass::runOnMachineFunction(MachineFunction &MF) {
  const X86Subtarget &SubTarget = MF.getSubtarget<X86Subtarget>();

  // Check that the cf-protection-branch is enabled.
  Metadata *isCFProtectionSupported =
      MF.getMMI().getModule()->getModuleFlag("cf-protection-branch");
  if (!isCFProtectionSupported && !IndirectBranchTracking)
    return false;

  // True if the current MF was changed and false otherwise.
  bool Changed = false;

  TII = SubTarget.getInstrInfo();
  EndbrOpcode = SubTarget.is64Bit() ? X86::ENDBR64 : X86::ENDBR32;

  // Non-internal function or function whose address was taken, can be
  // accessed through indirect calls. Mark the first BB with ENDBR instruction
  // unless nocf_check attribute is used.
  if ((MF.getFunction().hasAddressTaken() ||
       !MF.getFunction().hasLocalLinkage()) &&
      !MF.getFunction().doesNoCfCheck()) {
    auto MBB = MF.begin();
    Changed |= addENDBR(*MBB);
  }

  for (auto &MBB : MF)
    // Find all basic blocks that their address was taken (for example
    // in the case of indirect jump) and add ENDBR instruction.
    if (MBB.hasAddressTaken())
      Changed |= addENDBR(MBB);

  return Changed;
}
