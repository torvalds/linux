//===- Localizer.cpp ---------------------- Localize some instrs -*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements the Localizer class.
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/GlobalISel/Localizer.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "localizer"

using namespace llvm;

char Localizer::ID = 0;
INITIALIZE_PASS(Localizer, DEBUG_TYPE,
                "Move/duplicate certain instructions close to their use", false,
                false)

Localizer::Localizer() : MachineFunctionPass(ID) {
  initializeLocalizerPass(*PassRegistry::getPassRegistry());
}

void Localizer::init(MachineFunction &MF) { MRI = &MF.getRegInfo(); }

bool Localizer::shouldLocalize(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  default:
    return false;
  // Constants-like instructions should be close to their users.
  // We don't want long live-ranges for them.
  case TargetOpcode::G_CONSTANT:
  case TargetOpcode::G_FCONSTANT:
  case TargetOpcode::G_FRAME_INDEX:
    return true;
  }
}

void Localizer::getAnalysisUsage(AnalysisUsage &AU) const {
  getSelectionDAGFallbackAnalysisUsage(AU);
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool Localizer::isLocalUse(MachineOperand &MOUse, const MachineInstr &Def,
                           MachineBasicBlock *&InsertMBB) {
  MachineInstr &MIUse = *MOUse.getParent();
  InsertMBB = MIUse.getParent();
  if (MIUse.isPHI())
    InsertMBB = MIUse.getOperand(MIUse.getOperandNo(&MOUse) + 1).getMBB();
  return InsertMBB == Def.getParent();
}

bool Localizer::runOnMachineFunction(MachineFunction &MF) {
  // If the ISel pipeline failed, do not bother running that pass.
  if (MF.getProperties().hasProperty(
          MachineFunctionProperties::Property::FailedISel))
    return false;

  LLVM_DEBUG(dbgs() << "Localize instructions for: " << MF.getName() << '\n');

  init(MF);

  bool Changed = false;
  // Keep track of the instructions we localized.
  // We won't need to process them if we see them later in the CFG.
  SmallPtrSet<MachineInstr *, 16> LocalizedInstrs;
  DenseMap<std::pair<MachineBasicBlock *, unsigned>, unsigned> MBBWithLocalDef;
  // TODO: Do bottom up traversal.
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (LocalizedInstrs.count(&MI) || !shouldLocalize(MI))
        continue;
      LLVM_DEBUG(dbgs() << "Should localize: " << MI);
      assert(MI.getDesc().getNumDefs() == 1 &&
             "More than one definition not supported yet");
      unsigned Reg = MI.getOperand(0).getReg();
      // Check if all the users of MI are local.
      // We are going to invalidation the list of use operands, so we
      // can't use range iterator.
      for (auto MOIt = MRI->use_begin(Reg), MOItEnd = MRI->use_end();
           MOIt != MOItEnd;) {
        MachineOperand &MOUse = *MOIt++;
        // Check if the use is already local.
        MachineBasicBlock *InsertMBB;
        LLVM_DEBUG(MachineInstr &MIUse = *MOUse.getParent();
                   dbgs() << "Checking use: " << MIUse
                          << " #Opd: " << MIUse.getOperandNo(&MOUse) << '\n');
        if (isLocalUse(MOUse, MI, InsertMBB))
          continue;
        LLVM_DEBUG(dbgs() << "Fixing non-local use\n");
        Changed = true;
        auto MBBAndReg = std::make_pair(InsertMBB, Reg);
        auto NewVRegIt = MBBWithLocalDef.find(MBBAndReg);
        if (NewVRegIt == MBBWithLocalDef.end()) {
          // Create the localized instruction.
          MachineInstr *LocalizedMI = MF.CloneMachineInstr(&MI);
          LocalizedInstrs.insert(LocalizedMI);
          // Don't try to be smart for the insertion point.
          // There is no guarantee that the first seen use is the first
          // use in the block.
          InsertMBB->insert(InsertMBB->SkipPHIsAndLabels(InsertMBB->begin()),
                            LocalizedMI);

          // Set a new register for the definition.
          unsigned NewReg =
              MRI->createGenericVirtualRegister(MRI->getType(Reg));
          MRI->setRegClassOrRegBank(NewReg, MRI->getRegClassOrRegBank(Reg));
          LocalizedMI->getOperand(0).setReg(NewReg);
          NewVRegIt =
              MBBWithLocalDef.insert(std::make_pair(MBBAndReg, NewReg)).first;
          LLVM_DEBUG(dbgs() << "Inserted: " << *LocalizedMI);
        }
        LLVM_DEBUG(dbgs() << "Update use with: " << printReg(NewVRegIt->second)
                          << '\n');
        // Update the user reg.
        MOUse.setReg(NewVRegIt->second);
      }
    }
  }
  return Changed;
}
