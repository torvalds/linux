//===-- Thumb2ITBlockPass.cpp - Insert Thumb-2 IT blocks ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ARM.h"
#include "ARMMachineFunctionInfo.h"
#include "ARMSubtarget.h"
#include "MCTargetDesc/ARMBaseInfo.h"
#include "Thumb2InstrInfo.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCRegisterInfo.h"
#include <cassert>
#include <new>

using namespace llvm;

#define DEBUG_TYPE "thumb2-it"

STATISTIC(NumITs,        "Number of IT blocks inserted");
STATISTIC(NumMovedInsts, "Number of predicated instructions moved");

namespace {

  class Thumb2ITBlockPass : public MachineFunctionPass {
  public:
    static char ID;

    bool restrictIT;
    const Thumb2InstrInfo *TII;
    const TargetRegisterInfo *TRI;
    ARMFunctionInfo *AFI;

    Thumb2ITBlockPass() : MachineFunctionPass(ID) {}

    bool runOnMachineFunction(MachineFunction &Fn) override;

    MachineFunctionProperties getRequiredProperties() const override {
      return MachineFunctionProperties().set(
          MachineFunctionProperties::Property::NoVRegs);
    }

    StringRef getPassName() const override {
      return "Thumb IT blocks insertion pass";
    }

  private:
    bool MoveCopyOutOfITBlock(MachineInstr *MI,
                              ARMCC::CondCodes CC, ARMCC::CondCodes OCC,
                              SmallSet<unsigned, 4> &Defs,
                              SmallSet<unsigned, 4> &Uses);
    bool InsertITInstructions(MachineBasicBlock &MBB);
  };

  char Thumb2ITBlockPass::ID = 0;

} // end anonymous namespace

/// TrackDefUses - Tracking what registers are being defined and used by
/// instructions in the IT block. This also tracks "dependencies", i.e. uses
/// in the IT block that are defined before the IT instruction.
static void TrackDefUses(MachineInstr *MI,
                         SmallSet<unsigned, 4> &Defs,
                         SmallSet<unsigned, 4> &Uses,
                         const TargetRegisterInfo *TRI) {
  SmallVector<unsigned, 4> LocalDefs;
  SmallVector<unsigned, 4> LocalUses;

  for (unsigned i = 0, e = MI->getNumOperands(); i != e; ++i) {
    MachineOperand &MO = MI->getOperand(i);
    if (!MO.isReg())
      continue;
    unsigned Reg = MO.getReg();
    if (!Reg || Reg == ARM::ITSTATE || Reg == ARM::SP)
      continue;
    if (MO.isUse())
      LocalUses.push_back(Reg);
    else
      LocalDefs.push_back(Reg);
  }

  for (unsigned i = 0, e = LocalUses.size(); i != e; ++i) {
    unsigned Reg = LocalUses[i];
    for (MCSubRegIterator Subreg(Reg, TRI, /*IncludeSelf=*/true);
         Subreg.isValid(); ++Subreg)
      Uses.insert(*Subreg);
  }

  for (unsigned i = 0, e = LocalDefs.size(); i != e; ++i) {
    unsigned Reg = LocalDefs[i];
    for (MCSubRegIterator Subreg(Reg, TRI, /*IncludeSelf=*/true);
         Subreg.isValid(); ++Subreg)
      Defs.insert(*Subreg);
    if (Reg == ARM::CPSR)
      continue;
  }
}

/// Clear kill flags for any uses in the given set.  This will likely
/// conservatively remove more kill flags than are necessary, but removing them
/// is safer than incorrect kill flags remaining on instructions.
static void ClearKillFlags(MachineInstr *MI, SmallSet<unsigned, 4> &Uses) {
  for (MachineOperand &MO : MI->operands()) {
    if (!MO.isReg() || MO.isDef() || !MO.isKill())
      continue;
    if (!Uses.count(MO.getReg()))
      continue;
    MO.setIsKill(false);
  }
}

static bool isCopy(MachineInstr *MI) {
  switch (MI->getOpcode()) {
  default:
    return false;
  case ARM::MOVr:
  case ARM::MOVr_TC:
  case ARM::tMOVr:
  case ARM::t2MOVr:
    return true;
  }
}

bool
Thumb2ITBlockPass::MoveCopyOutOfITBlock(MachineInstr *MI,
                                      ARMCC::CondCodes CC, ARMCC::CondCodes OCC,
                                        SmallSet<unsigned, 4> &Defs,
                                        SmallSet<unsigned, 4> &Uses) {
  if (!isCopy(MI))
    return false;
  // llvm models select's as two-address instructions. That means a copy
  // is inserted before a t2MOVccr, etc. If the copy is scheduled in
  // between selects we would end up creating multiple IT blocks.
  assert(MI->getOperand(0).getSubReg() == 0 &&
         MI->getOperand(1).getSubReg() == 0 &&
         "Sub-register indices still around?");

  unsigned DstReg = MI->getOperand(0).getReg();
  unsigned SrcReg = MI->getOperand(1).getReg();

  // First check if it's safe to move it.
  if (Uses.count(DstReg) || Defs.count(SrcReg))
    return false;

  // If the CPSR is defined by this copy, then we don't want to move it. E.g.,
  // if we have:
  //
  //   movs  r1, r1
  //   rsb   r1, 0
  //   movs  r2, r2
  //   rsb   r2, 0
  //
  // we don't want this to be converted to:
  //
  //   movs  r1, r1
  //   movs  r2, r2
  //   itt   mi
  //   rsb   r1, 0
  //   rsb   r2, 0
  //
  const MCInstrDesc &MCID = MI->getDesc();
  if (MI->hasOptionalDef() &&
      MI->getOperand(MCID.getNumOperands() - 1).getReg() == ARM::CPSR)
    return false;

  // Then peek at the next instruction to see if it's predicated on CC or OCC.
  // If not, then there is nothing to be gained by moving the copy.
  MachineBasicBlock::iterator I = MI; ++I;
  MachineBasicBlock::iterator E = MI->getParent()->end();
  while (I != E && I->isDebugInstr())
    ++I;
  if (I != E) {
    unsigned NPredReg = 0;
    ARMCC::CondCodes NCC = getITInstrPredicate(*I, NPredReg);
    if (NCC == CC || NCC == OCC)
      return true;
  }
  return false;
}

bool Thumb2ITBlockPass::InsertITInstructions(MachineBasicBlock &MBB) {
  bool Modified = false;

  SmallSet<unsigned, 4> Defs;
  SmallSet<unsigned, 4> Uses;
  MachineBasicBlock::iterator MBBI = MBB.begin(), E = MBB.end();
  while (MBBI != E) {
    MachineInstr *MI = &*MBBI;
    DebugLoc dl = MI->getDebugLoc();
    unsigned PredReg = 0;
    ARMCC::CondCodes CC = getITInstrPredicate(*MI, PredReg);
    if (CC == ARMCC::AL) {
      ++MBBI;
      continue;
    }

    Defs.clear();
    Uses.clear();
    TrackDefUses(MI, Defs, Uses, TRI);

    // Insert an IT instruction.
    MachineInstrBuilder MIB = BuildMI(MBB, MBBI, dl, TII->get(ARM::t2IT))
      .addImm(CC);

    // Add implicit use of ITSTATE to IT block instructions.
    MI->addOperand(MachineOperand::CreateReg(ARM::ITSTATE, false/*ifDef*/,
                                             true/*isImp*/, false/*isKill*/));

    MachineInstr *LastITMI = MI;
    MachineBasicBlock::iterator InsertPos = MIB.getInstr();
    ++MBBI;

    // Form IT block.
    ARMCC::CondCodes OCC = ARMCC::getOppositeCondition(CC);
    unsigned Mask = 0, Pos = 3;

    // v8 IT blocks are limited to one conditional op unless -arm-no-restrict-it
    // is set: skip the loop
    if (!restrictIT) {
      // Branches, including tricky ones like LDM_RET, need to end an IT
      // block so check the instruction we just put in the block.
      for (; MBBI != E && Pos &&
             (!MI->isBranch() && !MI->isReturn()) ; ++MBBI) {
        if (MBBI->isDebugInstr())
          continue;

        MachineInstr *NMI = &*MBBI;
        MI = NMI;

        unsigned NPredReg = 0;
        ARMCC::CondCodes NCC = getITInstrPredicate(*NMI, NPredReg);
        if (NCC == CC || NCC == OCC) {
          Mask |= (NCC & 1) << Pos;
          // Add implicit use of ITSTATE.
          NMI->addOperand(MachineOperand::CreateReg(ARM::ITSTATE, false/*ifDef*/,
                                                 true/*isImp*/, false/*isKill*/));
          LastITMI = NMI;
        } else {
          if (NCC == ARMCC::AL &&
              MoveCopyOutOfITBlock(NMI, CC, OCC, Defs, Uses)) {
            --MBBI;
            MBB.remove(NMI);
            MBB.insert(InsertPos, NMI);
            ClearKillFlags(MI, Uses);
            ++NumMovedInsts;
            continue;
          }
          break;
        }
        TrackDefUses(NMI, Defs, Uses, TRI);
        --Pos;
      }
    }

    // Finalize IT mask.
    Mask |= (1 << Pos);
    // Tag along (firstcond[0] << 4) with the mask.
    Mask |= (CC & 1) << 4;
    MIB.addImm(Mask);

    // Last instruction in IT block kills ITSTATE.
    LastITMI->findRegisterUseOperand(ARM::ITSTATE)->setIsKill();

    // Finalize the bundle.
    finalizeBundle(MBB, InsertPos.getInstrIterator(),
                   ++LastITMI->getIterator());

    Modified = true;
    ++NumITs;
  }

  return Modified;
}

bool Thumb2ITBlockPass::runOnMachineFunction(MachineFunction &Fn) {
  const ARMSubtarget &STI =
      static_cast<const ARMSubtarget &>(Fn.getSubtarget());
  if (!STI.isThumb2())
    return false;
  AFI = Fn.getInfo<ARMFunctionInfo>();
  TII = static_cast<const Thumb2InstrInfo *>(STI.getInstrInfo());
  TRI = STI.getRegisterInfo();
  restrictIT = STI.restrictIT();

  if (!AFI->isThumbFunction())
    return false;

  bool Modified = false;
  for (MachineFunction::iterator MFI = Fn.begin(), E = Fn.end(); MFI != E; ) {
    MachineBasicBlock &MBB = *MFI;
    ++MFI;
    Modified |= InsertITInstructions(MBB);
  }

  if (Modified)
    AFI->setHasITBlocks(true);

  return Modified;
}

/// createThumb2ITBlockPass - Returns an instance of the Thumb2 IT blocks
/// insertion pass.
FunctionPass *llvm::createThumb2ITBlockPass() {
  return new Thumb2ITBlockPass();
}
