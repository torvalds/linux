//===-- LiveVariables.cpp - Live Variable Analysis for Machine Code -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the LiveVariable analysis pass.  For each machine
// instruction in the function, this pass calculates the set of registers that
// are immediately dead after the instruction (i.e., the instruction calculates
// the value, but it is never used) and the set of registers that are used by
// the instruction, but are never used after the instruction (i.e., they are
// killed).
//
// This class computes live variables using a sparse implementation based on
// the machine code SSA form.  This class computes live variable information for
// each virtual and _register allocatable_ physical register in a function.  It
// uses the dominance properties of SSA form to efficiently compute live
// variables for virtual registers, and assumes that physical registers are only
// live within a single basic block (allowing it to do a single local analysis
// to resolve physical register lifetimes in each basic block).  If a physical
// register is not register allocatable, it is not tracked.  This is useful for
// things like the stack pointer and condition codes.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/LiveVariables.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
using namespace llvm;

char LiveVariables::ID = 0;
char &llvm::LiveVariablesID = LiveVariables::ID;
INITIALIZE_PASS_BEGIN(LiveVariables, "livevars",
                "Live Variable Analysis", false, false)
INITIALIZE_PASS_DEPENDENCY(UnreachableMachineBlockElim)
INITIALIZE_PASS_END(LiveVariables, "livevars",
                "Live Variable Analysis", false, false)


void LiveVariables::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequiredID(UnreachableMachineBlockElimID);
  AU.setPreservesAll();
  MachineFunctionPass::getAnalysisUsage(AU);
}

MachineInstr *
LiveVariables::VarInfo::findKill(const MachineBasicBlock *MBB) const {
  for (unsigned i = 0, e = Kills.size(); i != e; ++i)
    if (Kills[i]->getParent() == MBB)
      return Kills[i];
  return nullptr;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void LiveVariables::VarInfo::dump() const {
  dbgs() << "  Alive in blocks: ";
  for (SparseBitVector<>::iterator I = AliveBlocks.begin(),
           E = AliveBlocks.end(); I != E; ++I)
    dbgs() << *I << ", ";
  dbgs() << "\n  Killed by:";
  if (Kills.empty())
    dbgs() << " No instructions.\n";
  else {
    for (unsigned i = 0, e = Kills.size(); i != e; ++i)
      dbgs() << "\n    #" << i << ": " << *Kills[i];
    dbgs() << "\n";
  }
}
#endif

/// getVarInfo - Get (possibly creating) a VarInfo object for the given vreg.
LiveVariables::VarInfo &LiveVariables::getVarInfo(unsigned RegIdx) {
  assert(TargetRegisterInfo::isVirtualRegister(RegIdx) &&
         "getVarInfo: not a virtual register!");
  VirtRegInfo.grow(RegIdx);
  return VirtRegInfo[RegIdx];
}

void LiveVariables::MarkVirtRegAliveInBlock(VarInfo& VRInfo,
                                            MachineBasicBlock *DefBlock,
                                            MachineBasicBlock *MBB,
                                    std::vector<MachineBasicBlock*> &WorkList) {
  unsigned BBNum = MBB->getNumber();

  // Check to see if this basic block is one of the killing blocks.  If so,
  // remove it.
  for (unsigned i = 0, e = VRInfo.Kills.size(); i != e; ++i)
    if (VRInfo.Kills[i]->getParent() == MBB) {
      VRInfo.Kills.erase(VRInfo.Kills.begin()+i);  // Erase entry
      break;
    }

  if (MBB == DefBlock) return;  // Terminate recursion

  if (VRInfo.AliveBlocks.test(BBNum))
    return;  // We already know the block is live

  // Mark the variable known alive in this bb
  VRInfo.AliveBlocks.set(BBNum);

  assert(MBB != &MF->front() && "Can't find reaching def for virtreg");
  WorkList.insert(WorkList.end(), MBB->pred_rbegin(), MBB->pred_rend());
}

void LiveVariables::MarkVirtRegAliveInBlock(VarInfo &VRInfo,
                                            MachineBasicBlock *DefBlock,
                                            MachineBasicBlock *MBB) {
  std::vector<MachineBasicBlock*> WorkList;
  MarkVirtRegAliveInBlock(VRInfo, DefBlock, MBB, WorkList);

  while (!WorkList.empty()) {
    MachineBasicBlock *Pred = WorkList.back();
    WorkList.pop_back();
    MarkVirtRegAliveInBlock(VRInfo, DefBlock, Pred, WorkList);
  }
}

void LiveVariables::HandleVirtRegUse(unsigned reg, MachineBasicBlock *MBB,
                                     MachineInstr &MI) {
  assert(MRI->getVRegDef(reg) && "Register use before def!");

  unsigned BBNum = MBB->getNumber();

  VarInfo& VRInfo = getVarInfo(reg);

  // Check to see if this basic block is already a kill block.
  if (!VRInfo.Kills.empty() && VRInfo.Kills.back()->getParent() == MBB) {
    // Yes, this register is killed in this basic block already. Increase the
    // live range by updating the kill instruction.
    VRInfo.Kills.back() = &MI;
    return;
  }

#ifndef NDEBUG
  for (unsigned i = 0, e = VRInfo.Kills.size(); i != e; ++i)
    assert(VRInfo.Kills[i]->getParent() != MBB && "entry should be at end!");
#endif

  // This situation can occur:
  //
  //     ,------.
  //     |      |
  //     |      v
  //     |   t2 = phi ... t1 ...
  //     |      |
  //     |      v
  //     |   t1 = ...
  //     |  ... = ... t1 ...
  //     |      |
  //     `------'
  //
  // where there is a use in a PHI node that's a predecessor to the defining
  // block. We don't want to mark all predecessors as having the value "alive"
  // in this case.
  if (MBB == MRI->getVRegDef(reg)->getParent()) return;

  // Add a new kill entry for this basic block. If this virtual register is
  // already marked as alive in this basic block, that means it is alive in at
  // least one of the successor blocks, it's not a kill.
  if (!VRInfo.AliveBlocks.test(BBNum))
    VRInfo.Kills.push_back(&MI);

  // Update all dominating blocks to mark them as "known live".
  for (MachineBasicBlock::const_pred_iterator PI = MBB->pred_begin(),
         E = MBB->pred_end(); PI != E; ++PI)
    MarkVirtRegAliveInBlock(VRInfo, MRI->getVRegDef(reg)->getParent(), *PI);
}

void LiveVariables::HandleVirtRegDef(unsigned Reg, MachineInstr &MI) {
  VarInfo &VRInfo = getVarInfo(Reg);

  if (VRInfo.AliveBlocks.empty())
    // If vr is not alive in any block, then defaults to dead.
    VRInfo.Kills.push_back(&MI);
}

/// FindLastPartialDef - Return the last partial def of the specified register.
/// Also returns the sub-registers that're defined by the instruction.
MachineInstr *LiveVariables::FindLastPartialDef(unsigned Reg,
                                            SmallSet<unsigned,4> &PartDefRegs) {
  unsigned LastDefReg = 0;
  unsigned LastDefDist = 0;
  MachineInstr *LastDef = nullptr;
  for (MCSubRegIterator SubRegs(Reg, TRI); SubRegs.isValid(); ++SubRegs) {
    unsigned SubReg = *SubRegs;
    MachineInstr *Def = PhysRegDef[SubReg];
    if (!Def)
      continue;
    unsigned Dist = DistanceMap[Def];
    if (Dist > LastDefDist) {
      LastDefReg  = SubReg;
      LastDef     = Def;
      LastDefDist = Dist;
    }
  }

  if (!LastDef)
    return nullptr;

  PartDefRegs.insert(LastDefReg);
  for (unsigned i = 0, e = LastDef->getNumOperands(); i != e; ++i) {
    MachineOperand &MO = LastDef->getOperand(i);
    if (!MO.isReg() || !MO.isDef() || MO.getReg() == 0)
      continue;
    unsigned DefReg = MO.getReg();
    if (TRI->isSubRegister(Reg, DefReg)) {
      for (MCSubRegIterator SubRegs(DefReg, TRI, /*IncludeSelf=*/true);
           SubRegs.isValid(); ++SubRegs)
        PartDefRegs.insert(*SubRegs);
    }
  }
  return LastDef;
}

/// HandlePhysRegUse - Turn previous partial def's into read/mod/writes. Add
/// implicit defs to a machine instruction if there was an earlier def of its
/// super-register.
void LiveVariables::HandlePhysRegUse(unsigned Reg, MachineInstr &MI) {
  MachineInstr *LastDef = PhysRegDef[Reg];
  // If there was a previous use or a "full" def all is well.
  if (!LastDef && !PhysRegUse[Reg]) {
    // Otherwise, the last sub-register def implicitly defines this register.
    // e.g.
    // AH =
    // AL = ... implicit-def EAX, implicit killed AH
    //    = AH
    // ...
    //    = EAX
    // All of the sub-registers must have been defined before the use of Reg!
    SmallSet<unsigned, 4> PartDefRegs;
    MachineInstr *LastPartialDef = FindLastPartialDef(Reg, PartDefRegs);
    // If LastPartialDef is NULL, it must be using a livein register.
    if (LastPartialDef) {
      LastPartialDef->addOperand(MachineOperand::CreateReg(Reg, true/*IsDef*/,
                                                           true/*IsImp*/));
      PhysRegDef[Reg] = LastPartialDef;
      SmallSet<unsigned, 8> Processed;
      for (MCSubRegIterator SubRegs(Reg, TRI); SubRegs.isValid(); ++SubRegs) {
        unsigned SubReg = *SubRegs;
        if (Processed.count(SubReg))
          continue;
        if (PartDefRegs.count(SubReg))
          continue;
        // This part of Reg was defined before the last partial def. It's killed
        // here.
        LastPartialDef->addOperand(MachineOperand::CreateReg(SubReg,
                                                             false/*IsDef*/,
                                                             true/*IsImp*/));
        PhysRegDef[SubReg] = LastPartialDef;
        for (MCSubRegIterator SS(SubReg, TRI); SS.isValid(); ++SS)
          Processed.insert(*SS);
      }
    }
  } else if (LastDef && !PhysRegUse[Reg] &&
             !LastDef->findRegisterDefOperand(Reg))
    // Last def defines the super register, add an implicit def of reg.
    LastDef->addOperand(MachineOperand::CreateReg(Reg, true/*IsDef*/,
                                                  true/*IsImp*/));

  // Remember this use.
  for (MCSubRegIterator SubRegs(Reg, TRI, /*IncludeSelf=*/true);
       SubRegs.isValid(); ++SubRegs)
    PhysRegUse[*SubRegs] = &MI;
}

/// FindLastRefOrPartRef - Return the last reference or partial reference of
/// the specified register.
MachineInstr *LiveVariables::FindLastRefOrPartRef(unsigned Reg) {
  MachineInstr *LastDef = PhysRegDef[Reg];
  MachineInstr *LastUse = PhysRegUse[Reg];
  if (!LastDef && !LastUse)
    return nullptr;

  MachineInstr *LastRefOrPartRef = LastUse ? LastUse : LastDef;
  unsigned LastRefOrPartRefDist = DistanceMap[LastRefOrPartRef];
  unsigned LastPartDefDist = 0;
  for (MCSubRegIterator SubRegs(Reg, TRI); SubRegs.isValid(); ++SubRegs) {
    unsigned SubReg = *SubRegs;
    MachineInstr *Def = PhysRegDef[SubReg];
    if (Def && Def != LastDef) {
      // There was a def of this sub-register in between. This is a partial
      // def, keep track of the last one.
      unsigned Dist = DistanceMap[Def];
      if (Dist > LastPartDefDist)
        LastPartDefDist = Dist;
    } else if (MachineInstr *Use = PhysRegUse[SubReg]) {
      unsigned Dist = DistanceMap[Use];
      if (Dist > LastRefOrPartRefDist) {
        LastRefOrPartRefDist = Dist;
        LastRefOrPartRef = Use;
      }
    }
  }

  return LastRefOrPartRef;
}

bool LiveVariables::HandlePhysRegKill(unsigned Reg, MachineInstr *MI) {
  MachineInstr *LastDef = PhysRegDef[Reg];
  MachineInstr *LastUse = PhysRegUse[Reg];
  if (!LastDef && !LastUse)
    return false;

  MachineInstr *LastRefOrPartRef = LastUse ? LastUse : LastDef;
  unsigned LastRefOrPartRefDist = DistanceMap[LastRefOrPartRef];
  // The whole register is used.
  // AL =
  // AH =
  //
  //    = AX
  //    = AL, implicit killed AX
  // AX =
  //
  // Or whole register is defined, but not used at all.
  // dead AX =
  // ...
  // AX =
  //
  // Or whole register is defined, but only partly used.
  // dead AX = implicit-def AL
  //    = killed AL
  // AX =
  MachineInstr *LastPartDef = nullptr;
  unsigned LastPartDefDist = 0;
  SmallSet<unsigned, 8> PartUses;
  for (MCSubRegIterator SubRegs(Reg, TRI); SubRegs.isValid(); ++SubRegs) {
    unsigned SubReg = *SubRegs;
    MachineInstr *Def = PhysRegDef[SubReg];
    if (Def && Def != LastDef) {
      // There was a def of this sub-register in between. This is a partial
      // def, keep track of the last one.
      unsigned Dist = DistanceMap[Def];
      if (Dist > LastPartDefDist) {
        LastPartDefDist = Dist;
        LastPartDef = Def;
      }
      continue;
    }
    if (MachineInstr *Use = PhysRegUse[SubReg]) {
      for (MCSubRegIterator SS(SubReg, TRI, /*IncludeSelf=*/true); SS.isValid();
           ++SS)
        PartUses.insert(*SS);
      unsigned Dist = DistanceMap[Use];
      if (Dist > LastRefOrPartRefDist) {
        LastRefOrPartRefDist = Dist;
        LastRefOrPartRef = Use;
      }
    }
  }

  if (!PhysRegUse[Reg]) {
    // Partial uses. Mark register def dead and add implicit def of
    // sub-registers which are used.
    // dead EAX  = op  implicit-def AL
    // That is, EAX def is dead but AL def extends pass it.
    PhysRegDef[Reg]->addRegisterDead(Reg, TRI, true);
    for (MCSubRegIterator SubRegs(Reg, TRI); SubRegs.isValid(); ++SubRegs) {
      unsigned SubReg = *SubRegs;
      if (!PartUses.count(SubReg))
        continue;
      bool NeedDef = true;
      if (PhysRegDef[Reg] == PhysRegDef[SubReg]) {
        MachineOperand *MO = PhysRegDef[Reg]->findRegisterDefOperand(SubReg);
        if (MO) {
          NeedDef = false;
          assert(!MO->isDead());
        }
      }
      if (NeedDef)
        PhysRegDef[Reg]->addOperand(MachineOperand::CreateReg(SubReg,
                                                 true/*IsDef*/, true/*IsImp*/));
      MachineInstr *LastSubRef = FindLastRefOrPartRef(SubReg);
      if (LastSubRef)
        LastSubRef->addRegisterKilled(SubReg, TRI, true);
      else {
        LastRefOrPartRef->addRegisterKilled(SubReg, TRI, true);
        for (MCSubRegIterator SS(SubReg, TRI, /*IncludeSelf=*/true);
             SS.isValid(); ++SS)
          PhysRegUse[*SS] = LastRefOrPartRef;
      }
      for (MCSubRegIterator SS(SubReg, TRI); SS.isValid(); ++SS)
        PartUses.erase(*SS);
    }
  } else if (LastRefOrPartRef == PhysRegDef[Reg] && LastRefOrPartRef != MI) {
    if (LastPartDef)
      // The last partial def kills the register.
      LastPartDef->addOperand(MachineOperand::CreateReg(Reg, false/*IsDef*/,
                                                true/*IsImp*/, true/*IsKill*/));
    else {
      MachineOperand *MO =
        LastRefOrPartRef->findRegisterDefOperand(Reg, false, TRI);
      bool NeedEC = MO->isEarlyClobber() && MO->getReg() != Reg;
      // If the last reference is the last def, then it's not used at all.
      // That is, unless we are currently processing the last reference itself.
      LastRefOrPartRef->addRegisterDead(Reg, TRI, true);
      if (NeedEC) {
        // If we are adding a subreg def and the superreg def is marked early
        // clobber, add an early clobber marker to the subreg def.
        MO = LastRefOrPartRef->findRegisterDefOperand(Reg);
        if (MO)
          MO->setIsEarlyClobber();
      }
    }
  } else
    LastRefOrPartRef->addRegisterKilled(Reg, TRI, true);
  return true;
}

void LiveVariables::HandleRegMask(const MachineOperand &MO) {
  // Call HandlePhysRegKill() for all live registers clobbered by Mask.
  // Clobbered registers are always dead, sp there is no need to use
  // HandlePhysRegDef().
  for (unsigned Reg = 1, NumRegs = TRI->getNumRegs(); Reg != NumRegs; ++Reg) {
    // Skip dead regs.
    if (!PhysRegDef[Reg] && !PhysRegUse[Reg])
      continue;
    // Skip mask-preserved regs.
    if (!MO.clobbersPhysReg(Reg))
      continue;
    // Kill the largest clobbered super-register.
    // This avoids needless implicit operands.
    unsigned Super = Reg;
    for (MCSuperRegIterator SR(Reg, TRI); SR.isValid(); ++SR)
      if ((PhysRegDef[*SR] || PhysRegUse[*SR]) && MO.clobbersPhysReg(*SR))
        Super = *SR;
    HandlePhysRegKill(Super, nullptr);
  }
}

void LiveVariables::HandlePhysRegDef(unsigned Reg, MachineInstr *MI,
                                     SmallVectorImpl<unsigned> &Defs) {
  // What parts of the register are previously defined?
  SmallSet<unsigned, 32> Live;
  if (PhysRegDef[Reg] || PhysRegUse[Reg]) {
    for (MCSubRegIterator SubRegs(Reg, TRI, /*IncludeSelf=*/true);
         SubRegs.isValid(); ++SubRegs)
      Live.insert(*SubRegs);
  } else {
    for (MCSubRegIterator SubRegs(Reg, TRI); SubRegs.isValid(); ++SubRegs) {
      unsigned SubReg = *SubRegs;
      // If a register isn't itself defined, but all parts that make up of it
      // are defined, then consider it also defined.
      // e.g.
      // AL =
      // AH =
      //    = AX
      if (Live.count(SubReg))
        continue;
      if (PhysRegDef[SubReg] || PhysRegUse[SubReg]) {
        for (MCSubRegIterator SS(SubReg, TRI, /*IncludeSelf=*/true);
             SS.isValid(); ++SS)
          Live.insert(*SS);
      }
    }
  }

  // Start from the largest piece, find the last time any part of the register
  // is referenced.
  HandlePhysRegKill(Reg, MI);
  // Only some of the sub-registers are used.
  for (MCSubRegIterator SubRegs(Reg, TRI); SubRegs.isValid(); ++SubRegs) {
    unsigned SubReg = *SubRegs;
    if (!Live.count(SubReg))
      // Skip if this sub-register isn't defined.
      continue;
    HandlePhysRegKill(SubReg, MI);
  }

  if (MI)
    Defs.push_back(Reg);  // Remember this def.
}

void LiveVariables::UpdatePhysRegDefs(MachineInstr &MI,
                                      SmallVectorImpl<unsigned> &Defs) {
  while (!Defs.empty()) {
    unsigned Reg = Defs.back();
    Defs.pop_back();
    for (MCSubRegIterator SubRegs(Reg, TRI, /*IncludeSelf=*/true);
         SubRegs.isValid(); ++SubRegs) {
      unsigned SubReg = *SubRegs;
      PhysRegDef[SubReg] = &MI;
      PhysRegUse[SubReg]  = nullptr;
    }
  }
}

void LiveVariables::runOnInstr(MachineInstr &MI,
                               SmallVectorImpl<unsigned> &Defs) {
  assert(!MI.isDebugInstr());
  // Process all of the operands of the instruction...
  unsigned NumOperandsToProcess = MI.getNumOperands();

  // Unless it is a PHI node.  In this case, ONLY process the DEF, not any
  // of the uses.  They will be handled in other basic blocks.
  if (MI.isPHI())
    NumOperandsToProcess = 1;

  // Clear kill and dead markers. LV will recompute them.
  SmallVector<unsigned, 4> UseRegs;
  SmallVector<unsigned, 4> DefRegs;
  SmallVector<unsigned, 1> RegMasks;
  for (unsigned i = 0; i != NumOperandsToProcess; ++i) {
    MachineOperand &MO = MI.getOperand(i);
    if (MO.isRegMask()) {
      RegMasks.push_back(i);
      continue;
    }
    if (!MO.isReg() || MO.getReg() == 0)
      continue;
    unsigned MOReg = MO.getReg();
    if (MO.isUse()) {
      if (!(TargetRegisterInfo::isPhysicalRegister(MOReg) &&
            MRI->isReserved(MOReg)))
        MO.setIsKill(false);
      if (MO.readsReg())
        UseRegs.push_back(MOReg);
    } else {
      assert(MO.isDef());
      // FIXME: We should not remove any dead flags. However the MIPS RDDSP
      // instruction needs it at the moment: http://llvm.org/PR27116.
      if (TargetRegisterInfo::isPhysicalRegister(MOReg) &&
          !MRI->isReserved(MOReg))
        MO.setIsDead(false);
      DefRegs.push_back(MOReg);
    }
  }

  MachineBasicBlock *MBB = MI.getParent();
  // Process all uses.
  for (unsigned i = 0, e = UseRegs.size(); i != e; ++i) {
    unsigned MOReg = UseRegs[i];
    if (TargetRegisterInfo::isVirtualRegister(MOReg))
      HandleVirtRegUse(MOReg, MBB, MI);
    else if (!MRI->isReserved(MOReg))
      HandlePhysRegUse(MOReg, MI);
  }

  // Process all masked registers. (Call clobbers).
  for (unsigned i = 0, e = RegMasks.size(); i != e; ++i)
    HandleRegMask(MI.getOperand(RegMasks[i]));

  // Process all defs.
  for (unsigned i = 0, e = DefRegs.size(); i != e; ++i) {
    unsigned MOReg = DefRegs[i];
    if (TargetRegisterInfo::isVirtualRegister(MOReg))
      HandleVirtRegDef(MOReg, MI);
    else if (!MRI->isReserved(MOReg))
      HandlePhysRegDef(MOReg, &MI, Defs);
  }
  UpdatePhysRegDefs(MI, Defs);
}

void LiveVariables::runOnBlock(MachineBasicBlock *MBB, const unsigned NumRegs) {
  // Mark live-in registers as live-in.
  SmallVector<unsigned, 4> Defs;
  for (const auto &LI : MBB->liveins()) {
    assert(TargetRegisterInfo::isPhysicalRegister(LI.PhysReg) &&
           "Cannot have a live-in virtual register!");
    HandlePhysRegDef(LI.PhysReg, nullptr, Defs);
  }

  // Loop over all of the instructions, processing them.
  DistanceMap.clear();
  unsigned Dist = 0;
  for (MachineInstr &MI : *MBB) {
    if (MI.isDebugInstr())
      continue;
    DistanceMap.insert(std::make_pair(&MI, Dist++));

    runOnInstr(MI, Defs);
  }

  // Handle any virtual assignments from PHI nodes which might be at the
  // bottom of this basic block.  We check all of our successor blocks to see
  // if they have PHI nodes, and if so, we simulate an assignment at the end
  // of the current block.
  if (!PHIVarInfo[MBB->getNumber()].empty()) {
    SmallVectorImpl<unsigned> &VarInfoVec = PHIVarInfo[MBB->getNumber()];

    for (SmallVectorImpl<unsigned>::iterator I = VarInfoVec.begin(),
           E = VarInfoVec.end(); I != E; ++I)
      // Mark it alive only in the block we are representing.
      MarkVirtRegAliveInBlock(getVarInfo(*I),MRI->getVRegDef(*I)->getParent(),
                              MBB);
  }

  // MachineCSE may CSE instructions which write to non-allocatable physical
  // registers across MBBs. Remember if any reserved register is liveout.
  SmallSet<unsigned, 4> LiveOuts;
  for (MachineBasicBlock::const_succ_iterator SI = MBB->succ_begin(),
         SE = MBB->succ_end(); SI != SE; ++SI) {
    MachineBasicBlock *SuccMBB = *SI;
    if (SuccMBB->isEHPad())
      continue;
    for (const auto &LI : SuccMBB->liveins()) {
      if (!TRI->isInAllocatableClass(LI.PhysReg))
        // Ignore other live-ins, e.g. those that are live into landing pads.
        LiveOuts.insert(LI.PhysReg);
    }
  }

  // Loop over PhysRegDef / PhysRegUse, killing any registers that are
  // available at the end of the basic block.
  for (unsigned i = 0; i != NumRegs; ++i)
    if ((PhysRegDef[i] || PhysRegUse[i]) && !LiveOuts.count(i))
      HandlePhysRegDef(i, nullptr, Defs);
}

bool LiveVariables::runOnMachineFunction(MachineFunction &mf) {
  MF = &mf;
  MRI = &mf.getRegInfo();
  TRI = MF->getSubtarget().getRegisterInfo();

  const unsigned NumRegs = TRI->getNumRegs();
  PhysRegDef.assign(NumRegs, nullptr);
  PhysRegUse.assign(NumRegs, nullptr);
  PHIVarInfo.resize(MF->getNumBlockIDs());
  PHIJoins.clear();

  // FIXME: LiveIntervals will be updated to remove its dependence on
  // LiveVariables to improve compilation time and eliminate bizarre pass
  // dependencies. Until then, we can't change much in -O0.
  if (!MRI->isSSA())
    report_fatal_error("regalloc=... not currently supported with -O0");

  analyzePHINodes(mf);

  // Calculate live variable information in depth first order on the CFG of the
  // function.  This guarantees that we will see the definition of a virtual
  // register before its uses due to dominance properties of SSA (except for PHI
  // nodes, which are treated as a special case).
  MachineBasicBlock *Entry = &MF->front();
  df_iterator_default_set<MachineBasicBlock*,16> Visited;

  for (MachineBasicBlock *MBB : depth_first_ext(Entry, Visited)) {
    runOnBlock(MBB, NumRegs);

    PhysRegDef.assign(NumRegs, nullptr);
    PhysRegUse.assign(NumRegs, nullptr);
  }

  // Convert and transfer the dead / killed information we have gathered into
  // VirtRegInfo onto MI's.
  for (unsigned i = 0, e1 = VirtRegInfo.size(); i != e1; ++i) {
    const unsigned Reg = TargetRegisterInfo::index2VirtReg(i);
    for (unsigned j = 0, e2 = VirtRegInfo[Reg].Kills.size(); j != e2; ++j)
      if (VirtRegInfo[Reg].Kills[j] == MRI->getVRegDef(Reg))
        VirtRegInfo[Reg].Kills[j]->addRegisterDead(Reg, TRI);
      else
        VirtRegInfo[Reg].Kills[j]->addRegisterKilled(Reg, TRI);
  }

  // Check to make sure there are no unreachable blocks in the MC CFG for the
  // function.  If so, it is due to a bug in the instruction selector or some
  // other part of the code generator if this happens.
#ifndef NDEBUG
  for(MachineFunction::iterator i = MF->begin(), e = MF->end(); i != e; ++i)
    assert(Visited.count(&*i) != 0 && "unreachable basic block found");
#endif

  PhysRegDef.clear();
  PhysRegUse.clear();
  PHIVarInfo.clear();

  return false;
}

/// replaceKillInstruction - Update register kill info by replacing a kill
/// instruction with a new one.
void LiveVariables::replaceKillInstruction(unsigned Reg, MachineInstr &OldMI,
                                           MachineInstr &NewMI) {
  VarInfo &VI = getVarInfo(Reg);
  std::replace(VI.Kills.begin(), VI.Kills.end(), &OldMI, &NewMI);
}

/// removeVirtualRegistersKilled - Remove all killed info for the specified
/// instruction.
void LiveVariables::removeVirtualRegistersKilled(MachineInstr &MI) {
  for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
    MachineOperand &MO = MI.getOperand(i);
    if (MO.isReg() && MO.isKill()) {
      MO.setIsKill(false);
      unsigned Reg = MO.getReg();
      if (TargetRegisterInfo::isVirtualRegister(Reg)) {
        bool removed = getVarInfo(Reg).removeKill(MI);
        assert(removed && "kill not in register's VarInfo?");
        (void)removed;
      }
    }
  }
}

/// analyzePHINodes - Gather information about the PHI nodes in here. In
/// particular, we want to map the variable information of a virtual register
/// which is used in a PHI node. We map that to the BB the vreg is coming from.
///
void LiveVariables::analyzePHINodes(const MachineFunction& Fn) {
  for (const auto &MBB : Fn)
    for (const auto &BBI : MBB) {
      if (!BBI.isPHI())
        break;
      for (unsigned i = 1, e = BBI.getNumOperands(); i != e; i += 2)
        if (BBI.getOperand(i).readsReg())
          PHIVarInfo[BBI.getOperand(i + 1).getMBB()->getNumber()]
            .push_back(BBI.getOperand(i).getReg());
    }
}

bool LiveVariables::VarInfo::isLiveIn(const MachineBasicBlock &MBB,
                                      unsigned Reg,
                                      MachineRegisterInfo &MRI) {
  unsigned Num = MBB.getNumber();

  // Reg is live-through.
  if (AliveBlocks.test(Num))
    return true;

  // Registers defined in MBB cannot be live in.
  const MachineInstr *Def = MRI.getVRegDef(Reg);
  if (Def && Def->getParent() == &MBB)
    return false;

 // Reg was not defined in MBB, was it killed here?
  return findKill(&MBB);
}

bool LiveVariables::isLiveOut(unsigned Reg, const MachineBasicBlock &MBB) {
  LiveVariables::VarInfo &VI = getVarInfo(Reg);

  SmallPtrSet<const MachineBasicBlock *, 8> Kills;
  for (unsigned i = 0, e = VI.Kills.size(); i != e; ++i)
    Kills.insert(VI.Kills[i]->getParent());

  // Loop over all of the successors of the basic block, checking to see if
  // the value is either live in the block, or if it is killed in the block.
  for (const MachineBasicBlock *SuccMBB : MBB.successors()) {
    // Is it alive in this successor?
    unsigned SuccIdx = SuccMBB->getNumber();
    if (VI.AliveBlocks.test(SuccIdx))
      return true;
    // Or is it live because there is a use in a successor that kills it?
    if (Kills.count(SuccMBB))
      return true;
  }

  return false;
}

/// addNewBlock - Add a new basic block BB as an empty succcessor to DomBB. All
/// variables that are live out of DomBB will be marked as passing live through
/// BB.
void LiveVariables::addNewBlock(MachineBasicBlock *BB,
                                MachineBasicBlock *DomBB,
                                MachineBasicBlock *SuccBB) {
  const unsigned NumNew = BB->getNumber();

  DenseSet<unsigned> Defs, Kills;

  MachineBasicBlock::iterator BBI = SuccBB->begin(), BBE = SuccBB->end();
  for (; BBI != BBE && BBI->isPHI(); ++BBI) {
    // Record the def of the PHI node.
    Defs.insert(BBI->getOperand(0).getReg());

    // All registers used by PHI nodes in SuccBB must be live through BB.
    for (unsigned i = 1, e = BBI->getNumOperands(); i != e; i += 2)
      if (BBI->getOperand(i+1).getMBB() == BB)
        getVarInfo(BBI->getOperand(i).getReg()).AliveBlocks.set(NumNew);
  }

  // Record all vreg defs and kills of all instructions in SuccBB.
  for (; BBI != BBE; ++BBI) {
    for (MachineInstr::mop_iterator I = BBI->operands_begin(),
         E = BBI->operands_end(); I != E; ++I) {
      if (I->isReg() && TargetRegisterInfo::isVirtualRegister(I->getReg())) {
        if (I->isDef())
          Defs.insert(I->getReg());
        else if (I->isKill())
          Kills.insert(I->getReg());
      }
    }
  }

  // Update info for all live variables
  for (unsigned i = 0, e = MRI->getNumVirtRegs(); i != e; ++i) {
    unsigned Reg = TargetRegisterInfo::index2VirtReg(i);

    // If the Defs is defined in the successor it can't be live in BB.
    if (Defs.count(Reg))
      continue;

    // If the register is either killed in or live through SuccBB it's also live
    // through BB.
    VarInfo &VI = getVarInfo(Reg);
    if (Kills.count(Reg) || VI.AliveBlocks.test(SuccBB->getNumber()))
      VI.AliveBlocks.set(NumNew);
  }
}
