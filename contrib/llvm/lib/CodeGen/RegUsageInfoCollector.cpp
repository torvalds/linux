//===-- RegUsageInfoCollector.cpp - Register Usage Information Collector --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// This pass is required to take advantage of the interprocedural register
/// allocation infrastructure.
///
/// This pass is simple MachineFunction pass which collects register usage
/// details by iterating through each physical registers and checking
/// MRI::isPhysRegUsed() then creates a RegMask based on this details.
/// The pass then stores this RegMask in PhysicalRegisterUsageInfo.cpp
///
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/RegisterUsageInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/CodeGen/TargetFrameLowering.h"

using namespace llvm;

#define DEBUG_TYPE "ip-regalloc"

STATISTIC(NumCSROpt,
          "Number of functions optimized for callee saved registers");

namespace {

class RegUsageInfoCollector : public MachineFunctionPass {
public:
  RegUsageInfoCollector() : MachineFunctionPass(ID) {
    PassRegistry &Registry = *PassRegistry::getPassRegistry();
    initializeRegUsageInfoCollectorPass(Registry);
  }

  StringRef getPassName() const override {
    return "Register Usage Information Collector Pass";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<PhysicalRegisterUsageInfo>();
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  // Call determineCalleeSaves and then also set the bits for subregs and
  // fully saved superregs.
  static void computeCalleeSavedRegs(BitVector &SavedRegs, MachineFunction &MF);

  static char ID;
};

} // end of anonymous namespace

char RegUsageInfoCollector::ID = 0;

INITIALIZE_PASS_BEGIN(RegUsageInfoCollector, "RegUsageInfoCollector",
                      "Register Usage Information Collector", false, false)
INITIALIZE_PASS_DEPENDENCY(PhysicalRegisterUsageInfo)
INITIALIZE_PASS_END(RegUsageInfoCollector, "RegUsageInfoCollector",
                    "Register Usage Information Collector", false, false)

FunctionPass *llvm::createRegUsageInfoCollector() {
  return new RegUsageInfoCollector();
}

bool RegUsageInfoCollector::runOnMachineFunction(MachineFunction &MF) {
  MachineRegisterInfo *MRI = &MF.getRegInfo();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  const LLVMTargetMachine &TM = MF.getTarget();

  LLVM_DEBUG(dbgs() << " -------------------- " << getPassName()
                    << " -------------------- \n");
  LLVM_DEBUG(dbgs() << "Function Name : " << MF.getName() << "\n");

  std::vector<uint32_t> RegMask;

  // Compute the size of the bit vector to represent all the registers.
  // The bit vector is broken into 32-bit chunks, thus takes the ceil of
  // the number of registers divided by 32 for the size.
  unsigned RegMaskSize = MachineOperand::getRegMaskSize(TRI->getNumRegs());
  RegMask.resize(RegMaskSize, ~((uint32_t)0));

  const Function &F = MF.getFunction();

  PhysicalRegisterUsageInfo &PRUI = getAnalysis<PhysicalRegisterUsageInfo>();
  PRUI.setTargetMachine(TM);

  LLVM_DEBUG(dbgs() << "Clobbered Registers: ");

  BitVector SavedRegs;
  computeCalleeSavedRegs(SavedRegs, MF);

  const BitVector &UsedPhysRegsMask = MRI->getUsedPhysRegsMask();
  auto SetRegAsDefined = [&RegMask] (unsigned Reg) {
    RegMask[Reg / 32] &= ~(1u << Reg % 32);
  };
  // Scan all the physical registers. When a register is defined in the current
  // function set it and all the aliasing registers as defined in the regmask.
  for (unsigned PReg = 1, PRegE = TRI->getNumRegs(); PReg < PRegE; ++PReg) {
    // Don't count registers that are saved and restored.
    if (SavedRegs.test(PReg))
      continue;
    // If a register is defined by an instruction mark it as defined together
    // with all it's unsaved aliases.
    if (!MRI->def_empty(PReg)) {
      for (MCRegAliasIterator AI(PReg, TRI, true); AI.isValid(); ++AI)
        if (!SavedRegs.test(*AI))
          SetRegAsDefined(*AI);
      continue;
    }
    // If a register is in the UsedPhysRegsMask set then mark it as defined.
    // All clobbered aliases will also be in the set, so we can skip setting
    // as defined all the aliases here.
    if (UsedPhysRegsMask.test(PReg))
      SetRegAsDefined(PReg);
  }

  if (TargetFrameLowering::isSafeForNoCSROpt(F)) {
    ++NumCSROpt;
    LLVM_DEBUG(dbgs() << MF.getName()
                      << " function optimized for not having CSR.\n");
  }

  for (unsigned PReg = 1, PRegE = TRI->getNumRegs(); PReg < PRegE; ++PReg)
    if (MachineOperand::clobbersPhysReg(&(RegMask[0]), PReg))
      LLVM_DEBUG(dbgs() << printReg(PReg, TRI) << " ");

  LLVM_DEBUG(dbgs() << " \n----------------------------------------\n");

  PRUI.storeUpdateRegUsageInfo(F, RegMask);

  return false;
}

void RegUsageInfoCollector::
computeCalleeSavedRegs(BitVector &SavedRegs, MachineFunction &MF) {
  const TargetFrameLowering &TFI = *MF.getSubtarget().getFrameLowering();
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  // Target will return the set of registers that it saves/restores as needed.
  SavedRegs.clear();
  TFI.determineCalleeSaves(MF, SavedRegs);

  // Insert subregs.
  const MCPhysReg *CSRegs = TRI.getCalleeSavedRegs(&MF);
  for (unsigned i = 0; CSRegs[i]; ++i) {
    unsigned Reg = CSRegs[i];
    if (SavedRegs.test(Reg))
      for (MCSubRegIterator SR(Reg, &TRI, false); SR.isValid(); ++SR)
        SavedRegs.set(*SR);
  }

  // Insert any register fully saved via subregisters.
  for (const TargetRegisterClass *RC : TRI.regclasses()) {
    if (!RC->CoveredBySubRegs)
       continue;

    for (unsigned PReg = 1, PRegE = TRI.getNumRegs(); PReg < PRegE; ++PReg) {
      if (SavedRegs.test(PReg))
        continue;

      // Check if PReg is fully covered by its subregs.
      if (!RC->contains(PReg))
        continue;

      // Add PReg to SavedRegs if all subregs are saved.
      bool AllSubRegsSaved = true;
      for (MCSubRegIterator SR(PReg, &TRI, false); SR.isValid(); ++SR)
        if (!SavedRegs.test(*SR)) {
          AllSubRegsSaved = false;
          break;
        }
      if (AllSubRegsSaved)
        SavedRegs.set(PReg);
    }
  }
}
