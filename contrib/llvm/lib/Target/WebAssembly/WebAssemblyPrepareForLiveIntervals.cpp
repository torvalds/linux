//===- WebAssemblyPrepareForLiveIntervals.cpp - Prepare for LiveIntervals -===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Fix up code to meet LiveInterval's requirements.
///
/// Some CodeGen passes don't preserve LiveInterval's requirements, because
/// they run after register allocation and it isn't important. However,
/// WebAssembly runs LiveIntervals in a late pass. This pass transforms code
/// to meet LiveIntervals' requirements; primarily, it ensures that all
/// virtual register uses have definitions (IMPLICIT_DEF definitions if
/// nothing else).
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/WebAssemblyMCTargetDesc.h"
#include "WebAssembly.h"
#include "WebAssemblyMachineFunctionInfo.h"
#include "WebAssemblySubtarget.h"
#include "WebAssemblyUtilities.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-prepare-for-live-intervals"

namespace {
class WebAssemblyPrepareForLiveIntervals final : public MachineFunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid
  WebAssemblyPrepareForLiveIntervals() : MachineFunctionPass(ID) {}

private:
  StringRef getPassName() const override {
    return "WebAssembly Prepare For LiveIntervals";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // end anonymous namespace

char WebAssemblyPrepareForLiveIntervals::ID = 0;
INITIALIZE_PASS(WebAssemblyPrepareForLiveIntervals, DEBUG_TYPE,
                "Fix up code for LiveIntervals", false, false)

FunctionPass *llvm::createWebAssemblyPrepareForLiveIntervals() {
  return new WebAssemblyPrepareForLiveIntervals();
}

// Test whether the given register has an ARGUMENT def.
static bool HasArgumentDef(unsigned Reg, const MachineRegisterInfo &MRI) {
  for (const auto &Def : MRI.def_instructions(Reg))
    if (WebAssembly::isArgument(Def))
      return true;
  return false;
}

bool WebAssemblyPrepareForLiveIntervals::runOnMachineFunction(
    MachineFunction &MF) {
  LLVM_DEBUG({
    dbgs() << "********** Prepare For LiveIntervals **********\n"
           << "********** Function: " << MF.getName() << '\n';
  });

  bool Changed = false;
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const auto &TII = *MF.getSubtarget<WebAssemblySubtarget>().getInstrInfo();
  MachineBasicBlock &Entry = *MF.begin();

  assert(!mustPreserveAnalysisID(LiveIntervalsID) &&
         "LiveIntervals shouldn't be active yet!");

  // We don't preserve SSA form.
  MRI.leaveSSA();

  // BranchFolding and perhaps other passes don't preserve IMPLICIT_DEF
  // instructions. LiveIntervals requires that all paths to virtual register
  // uses provide a definition. Insert IMPLICIT_DEFs in the entry block to
  // conservatively satisfy this.
  //
  // TODO: This is fairly heavy-handed; find a better approach.
  //
  for (unsigned i = 0, e = MRI.getNumVirtRegs(); i < e; ++i) {
    unsigned Reg = TargetRegisterInfo::index2VirtReg(i);

    // Skip unused registers.
    if (MRI.use_nodbg_empty(Reg))
      continue;

    // Skip registers that have an ARGUMENT definition.
    if (HasArgumentDef(Reg, MRI))
      continue;

    BuildMI(Entry, Entry.begin(), DebugLoc(),
            TII.get(WebAssembly::IMPLICIT_DEF), Reg);
    Changed = true;
  }

  // Move ARGUMENT_* instructions to the top of the entry block, so that their
  // liveness reflects the fact that these really are live-in values.
  for (auto MII = Entry.begin(), MIE = Entry.end(); MII != MIE;) {
    MachineInstr &MI = *MII++;
    if (WebAssembly::isArgument(MI)) {
      MI.removeFromParent();
      Entry.insert(Entry.begin(), &MI);
    }
  }

  // Ok, we're now ready to run the LiveIntervals analysis again.
  MF.getProperties().set(MachineFunctionProperties::Property::TracksLiveness);

  return Changed;
}
