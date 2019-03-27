//===- RegisterScavenging.cpp - Machine register scavenging ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file implements the machine register scavenger. It can provide
/// information, such as unused registers, at any point in a machine basic
/// block. It also provides a mechanism to make registers available by evicting
/// them to spill slots.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <limits>
#include <string>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "reg-scavenging"

STATISTIC(NumScavengedRegs, "Number of frame index regs scavenged");

void RegScavenger::setRegUsed(unsigned Reg, LaneBitmask LaneMask) {
  LiveUnits.addRegMasked(Reg, LaneMask);
}

void RegScavenger::init(MachineBasicBlock &MBB) {
  MachineFunction &MF = *MBB.getParent();
  TII = MF.getSubtarget().getInstrInfo();
  TRI = MF.getSubtarget().getRegisterInfo();
  MRI = &MF.getRegInfo();
  LiveUnits.init(*TRI);

  assert((NumRegUnits == 0 || NumRegUnits == TRI->getNumRegUnits()) &&
         "Target changed?");

  // Self-initialize.
  if (!this->MBB) {
    NumRegUnits = TRI->getNumRegUnits();
    KillRegUnits.resize(NumRegUnits);
    DefRegUnits.resize(NumRegUnits);
    TmpRegUnits.resize(NumRegUnits);
  }
  this->MBB = &MBB;

  for (ScavengedInfo &SI : Scavenged) {
    SI.Reg = 0;
    SI.Restore = nullptr;
  }

  Tracking = false;
}

void RegScavenger::enterBasicBlock(MachineBasicBlock &MBB) {
  init(MBB);
  LiveUnits.addLiveIns(MBB);
}

void RegScavenger::enterBasicBlockEnd(MachineBasicBlock &MBB) {
  init(MBB);
  LiveUnits.addLiveOuts(MBB);

  // Move internal iterator at the last instruction of the block.
  if (MBB.begin() != MBB.end()) {
    MBBI = std::prev(MBB.end());
    Tracking = true;
  }
}

void RegScavenger::addRegUnits(BitVector &BV, unsigned Reg) {
  for (MCRegUnitIterator RUI(Reg, TRI); RUI.isValid(); ++RUI)
    BV.set(*RUI);
}

void RegScavenger::removeRegUnits(BitVector &BV, unsigned Reg) {
  for (MCRegUnitIterator RUI(Reg, TRI); RUI.isValid(); ++RUI)
    BV.reset(*RUI);
}

void RegScavenger::determineKillsAndDefs() {
  assert(Tracking && "Must be tracking to determine kills and defs");

  MachineInstr &MI = *MBBI;
  assert(!MI.isDebugInstr() && "Debug values have no kills or defs");

  // Find out which registers are early clobbered, killed, defined, and marked
  // def-dead in this instruction.
  KillRegUnits.reset();
  DefRegUnits.reset();
  for (const MachineOperand &MO : MI.operands()) {
    if (MO.isRegMask()) {
      TmpRegUnits.clear();
      for (unsigned RU = 0, RUEnd = TRI->getNumRegUnits(); RU != RUEnd; ++RU) {
        for (MCRegUnitRootIterator RURI(RU, TRI); RURI.isValid(); ++RURI) {
          if (MO.clobbersPhysReg(*RURI)) {
            TmpRegUnits.set(RU);
            break;
          }
        }
      }

      // Apply the mask.
      KillRegUnits |= TmpRegUnits;
    }
    if (!MO.isReg())
      continue;
    unsigned Reg = MO.getReg();
    if (!TargetRegisterInfo::isPhysicalRegister(Reg) || isReserved(Reg))
      continue;

    if (MO.isUse()) {
      // Ignore undef uses.
      if (MO.isUndef())
        continue;
      if (MO.isKill())
        addRegUnits(KillRegUnits, Reg);
    } else {
      assert(MO.isDef());
      if (MO.isDead())
        addRegUnits(KillRegUnits, Reg);
      else
        addRegUnits(DefRegUnits, Reg);
    }
  }
}

void RegScavenger::unprocess() {
  assert(Tracking && "Cannot unprocess because we're not tracking");

  MachineInstr &MI = *MBBI;
  if (!MI.isDebugInstr()) {
    determineKillsAndDefs();

    // Commit the changes.
    setUnused(DefRegUnits);
    setUsed(KillRegUnits);
  }

  if (MBBI == MBB->begin()) {
    MBBI = MachineBasicBlock::iterator(nullptr);
    Tracking = false;
  } else
    --MBBI;
}

void RegScavenger::forward() {
  // Move ptr forward.
  if (!Tracking) {
    MBBI = MBB->begin();
    Tracking = true;
  } else {
    assert(MBBI != MBB->end() && "Already past the end of the basic block!");
    MBBI = std::next(MBBI);
  }
  assert(MBBI != MBB->end() && "Already at the end of the basic block!");

  MachineInstr &MI = *MBBI;

  for (SmallVectorImpl<ScavengedInfo>::iterator I = Scavenged.begin(),
         IE = Scavenged.end(); I != IE; ++I) {
    if (I->Restore != &MI)
      continue;

    I->Reg = 0;
    I->Restore = nullptr;
  }

  if (MI.isDebugInstr())
    return;

  determineKillsAndDefs();

  // Verify uses and defs.
#ifndef NDEBUG
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg())
      continue;
    unsigned Reg = MO.getReg();
    if (!TargetRegisterInfo::isPhysicalRegister(Reg) || isReserved(Reg))
      continue;
    if (MO.isUse()) {
      if (MO.isUndef())
        continue;
      if (!isRegUsed(Reg)) {
        // Check if it's partial live: e.g.
        // D0 = insert_subreg undef D0, S0
        // ... D0
        // The problem is the insert_subreg could be eliminated. The use of
        // D0 is using a partially undef value. This is not *incorrect* since
        // S1 is can be freely clobbered.
        // Ideally we would like a way to model this, but leaving the
        // insert_subreg around causes both correctness and performance issues.
        bool SubUsed = false;
        for (MCSubRegIterator SubRegs(Reg, TRI); SubRegs.isValid(); ++SubRegs)
          if (isRegUsed(*SubRegs)) {
            SubUsed = true;
            break;
          }
        bool SuperUsed = false;
        for (MCSuperRegIterator SR(Reg, TRI); SR.isValid(); ++SR) {
          if (isRegUsed(*SR)) {
            SuperUsed = true;
            break;
          }
        }
        if (!SubUsed && !SuperUsed) {
          MBB->getParent()->verify(nullptr, "In Register Scavenger");
          llvm_unreachable("Using an undefined register!");
        }
        (void)SubUsed;
        (void)SuperUsed;
      }
    } else {
      assert(MO.isDef());
#if 0
      // FIXME: Enable this once we've figured out how to correctly transfer
      // implicit kills during codegen passes like the coalescer.
      assert((KillRegs.test(Reg) || isUnused(Reg) ||
              isLiveInButUnusedBefore(Reg, MI, MBB, TRI, MRI)) &&
             "Re-defining a live register!");
#endif
    }
  }
#endif // NDEBUG

  // Commit the changes.
  setUnused(KillRegUnits);
  setUsed(DefRegUnits);
}

void RegScavenger::backward() {
  assert(Tracking && "Must be tracking to determine kills and defs");

  const MachineInstr &MI = *MBBI;
  LiveUnits.stepBackward(MI);

  // Expire scavenge spill frameindex uses.
  for (ScavengedInfo &I : Scavenged) {
    if (I.Restore == &MI) {
      I.Reg = 0;
      I.Restore = nullptr;
    }
  }

  if (MBBI == MBB->begin()) {
    MBBI = MachineBasicBlock::iterator(nullptr);
    Tracking = false;
  } else
    --MBBI;
}

bool RegScavenger::isRegUsed(unsigned Reg, bool includeReserved) const {
  if (isReserved(Reg))
    return includeReserved;
  return !LiveUnits.available(Reg);
}

unsigned RegScavenger::FindUnusedReg(const TargetRegisterClass *RC) const {
  for (unsigned Reg : *RC) {
    if (!isRegUsed(Reg)) {
      LLVM_DEBUG(dbgs() << "Scavenger found unused reg: " << printReg(Reg, TRI)
                        << "\n");
      return Reg;
    }
  }
  return 0;
}

BitVector RegScavenger::getRegsAvailable(const TargetRegisterClass *RC) {
  BitVector Mask(TRI->getNumRegs());
  for (unsigned Reg : *RC)
    if (!isRegUsed(Reg))
      Mask.set(Reg);
  return Mask;
}

unsigned RegScavenger::findSurvivorReg(MachineBasicBlock::iterator StartMI,
                                       BitVector &Candidates,
                                       unsigned InstrLimit,
                                       MachineBasicBlock::iterator &UseMI) {
  int Survivor = Candidates.find_first();
  assert(Survivor > 0 && "No candidates for scavenging");

  MachineBasicBlock::iterator ME = MBB->getFirstTerminator();
  assert(StartMI != ME && "MI already at terminator");
  MachineBasicBlock::iterator RestorePointMI = StartMI;
  MachineBasicBlock::iterator MI = StartMI;

  bool inVirtLiveRange = false;
  for (++MI; InstrLimit > 0 && MI != ME; ++MI, --InstrLimit) {
    if (MI->isDebugInstr()) {
      ++InstrLimit; // Don't count debug instructions
      continue;
    }
    bool isVirtKillInsn = false;
    bool isVirtDefInsn = false;
    // Remove any candidates touched by instruction.
    for (const MachineOperand &MO : MI->operands()) {
      if (MO.isRegMask())
        Candidates.clearBitsNotInMask(MO.getRegMask());
      if (!MO.isReg() || MO.isUndef() || !MO.getReg())
        continue;
      if (TargetRegisterInfo::isVirtualRegister(MO.getReg())) {
        if (MO.isDef())
          isVirtDefInsn = true;
        else if (MO.isKill())
          isVirtKillInsn = true;
        continue;
      }
      for (MCRegAliasIterator AI(MO.getReg(), TRI, true); AI.isValid(); ++AI)
        Candidates.reset(*AI);
    }
    // If we're not in a virtual reg's live range, this is a valid
    // restore point.
    if (!inVirtLiveRange) RestorePointMI = MI;

    // Update whether we're in the live range of a virtual register
    if (isVirtKillInsn) inVirtLiveRange = false;
    if (isVirtDefInsn) inVirtLiveRange = true;

    // Was our survivor untouched by this instruction?
    if (Candidates.test(Survivor))
      continue;

    // All candidates gone?
    if (Candidates.none())
      break;

    Survivor = Candidates.find_first();
  }
  // If we ran off the end, that's where we want to restore.
  if (MI == ME) RestorePointMI = ME;
  assert(RestorePointMI != StartMI &&
         "No available scavenger restore location!");

  // We ran out of candidates, so stop the search.
  UseMI = RestorePointMI;
  return Survivor;
}

/// Given the bitvector \p Available of free register units at position
/// \p From. Search backwards to find a register that is part of \p
/// Candidates and not used/clobbered until the point \p To. If there is
/// multiple candidates continue searching and pick the one that is not used/
/// clobbered for the longest time.
/// Returns the register and the earliest position we know it to be free or
/// the position MBB.end() if no register is available.
static std::pair<MCPhysReg, MachineBasicBlock::iterator>
findSurvivorBackwards(const MachineRegisterInfo &MRI,
    MachineBasicBlock::iterator From, MachineBasicBlock::iterator To,
    const LiveRegUnits &LiveOut, ArrayRef<MCPhysReg> AllocationOrder,
    bool RestoreAfter) {
  bool FoundTo = false;
  MCPhysReg Survivor = 0;
  MachineBasicBlock::iterator Pos;
  MachineBasicBlock &MBB = *From->getParent();
  unsigned InstrLimit = 25;
  unsigned InstrCountDown = InstrLimit;
  const TargetRegisterInfo &TRI = *MRI.getTargetRegisterInfo();
  LiveRegUnits Used(TRI);

  for (MachineBasicBlock::iterator I = From;; --I) {
    const MachineInstr &MI = *I;

    Used.accumulate(MI);

    if (I == To) {
      // See if one of the registers in RC wasn't used so far.
      for (MCPhysReg Reg : AllocationOrder) {
        if (!MRI.isReserved(Reg) && Used.available(Reg) &&
            LiveOut.available(Reg))
          return std::make_pair(Reg, MBB.end());
      }
      // Otherwise we will continue up to InstrLimit instructions to find
      // the register which is not defined/used for the longest time.
      FoundTo = true;
      Pos = To;
      // Note: It was fine so far to start our search at From, however now that
      // we have to spill, and can only place the restore after From then
      // add the regs used/defed by std::next(From) to the set.
      if (RestoreAfter)
        Used.accumulate(*std::next(From));
    }
    if (FoundTo) {
      if (Survivor == 0 || !Used.available(Survivor)) {
        MCPhysReg AvilableReg = 0;
        for (MCPhysReg Reg : AllocationOrder) {
          if (!MRI.isReserved(Reg) && Used.available(Reg)) {
            AvilableReg = Reg;
            break;
          }
        }
        if (AvilableReg == 0)
          break;
        Survivor = AvilableReg;
      }
      if (--InstrCountDown == 0)
        break;

      // Keep searching when we find a vreg since the spilled register will
      // be usefull for this other vreg as well later.
      bool FoundVReg = false;
      for (const MachineOperand &MO : MI.operands()) {
        if (MO.isReg() && TargetRegisterInfo::isVirtualRegister(MO.getReg())) {
          FoundVReg = true;
          break;
        }
      }
      if (FoundVReg) {
        InstrCountDown = InstrLimit;
        Pos = I;
      }
      if (I == MBB.begin())
        break;
    }
  }

  return std::make_pair(Survivor, Pos);
}

static unsigned getFrameIndexOperandNum(MachineInstr &MI) {
  unsigned i = 0;
  while (!MI.getOperand(i).isFI()) {
    ++i;
    assert(i < MI.getNumOperands() && "Instr doesn't have FrameIndex operand!");
  }
  return i;
}

RegScavenger::ScavengedInfo &
RegScavenger::spill(unsigned Reg, const TargetRegisterClass &RC, int SPAdj,
                    MachineBasicBlock::iterator Before,
                    MachineBasicBlock::iterator &UseMI) {
  // Find an available scavenging slot with size and alignment matching
  // the requirements of the class RC.
  const MachineFunction &MF = *Before->getMF();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  unsigned NeedSize = TRI->getSpillSize(RC);
  unsigned NeedAlign = TRI->getSpillAlignment(RC);

  unsigned SI = Scavenged.size(), Diff = std::numeric_limits<unsigned>::max();
  int FIB = MFI.getObjectIndexBegin(), FIE = MFI.getObjectIndexEnd();
  for (unsigned I = 0; I < Scavenged.size(); ++I) {
    if (Scavenged[I].Reg != 0)
      continue;
    // Verify that this slot is valid for this register.
    int FI = Scavenged[I].FrameIndex;
    if (FI < FIB || FI >= FIE)
      continue;
    unsigned S = MFI.getObjectSize(FI);
    unsigned A = MFI.getObjectAlignment(FI);
    if (NeedSize > S || NeedAlign > A)
      continue;
    // Avoid wasting slots with large size and/or large alignment. Pick one
    // that is the best fit for this register class (in street metric).
    // Picking a larger slot than necessary could happen if a slot for a
    // larger register is reserved before a slot for a smaller one. When
    // trying to spill a smaller register, the large slot would be found
    // first, thus making it impossible to spill the larger register later.
    unsigned D = (S-NeedSize) + (A-NeedAlign);
    if (D < Diff) {
      SI = I;
      Diff = D;
    }
  }

  if (SI == Scavenged.size()) {
    // We need to scavenge a register but have no spill slot, the target
    // must know how to do it (if not, we'll assert below).
    Scavenged.push_back(ScavengedInfo(FIE));
  }

  // Avoid infinite regress
  Scavenged[SI].Reg = Reg;

  // If the target knows how to save/restore the register, let it do so;
  // otherwise, use the emergency stack spill slot.
  if (!TRI->saveScavengerRegister(*MBB, Before, UseMI, &RC, Reg)) {
    // Spill the scavenged register before \p Before.
    int FI = Scavenged[SI].FrameIndex;
    if (FI < FIB || FI >= FIE) {
      std::string Msg = std::string("Error while trying to spill ") +
          TRI->getName(Reg) + " from class " + TRI->getRegClassName(&RC) +
          ": Cannot scavenge register without an emergency spill slot!";
      report_fatal_error(Msg.c_str());
    }
    TII->storeRegToStackSlot(*MBB, Before, Reg, true, Scavenged[SI].FrameIndex,
                             &RC, TRI);
    MachineBasicBlock::iterator II = std::prev(Before);

    unsigned FIOperandNum = getFrameIndexOperandNum(*II);
    TRI->eliminateFrameIndex(II, SPAdj, FIOperandNum, this);

    // Restore the scavenged register before its use (or first terminator).
    TII->loadRegFromStackSlot(*MBB, UseMI, Reg, Scavenged[SI].FrameIndex,
                              &RC, TRI);
    II = std::prev(UseMI);

    FIOperandNum = getFrameIndexOperandNum(*II);
    TRI->eliminateFrameIndex(II, SPAdj, FIOperandNum, this);
  }
  return Scavenged[SI];
}

unsigned RegScavenger::scavengeRegister(const TargetRegisterClass *RC,
                                        MachineBasicBlock::iterator I,
                                        int SPAdj) {
  MachineInstr &MI = *I;
  const MachineFunction &MF = *MI.getMF();
  // Consider all allocatable registers in the register class initially
  BitVector Candidates = TRI->getAllocatableSet(MF, RC);

  // Exclude all the registers being used by the instruction.
  for (const MachineOperand &MO : MI.operands()) {
    if (MO.isReg() && MO.getReg() != 0 && !(MO.isUse() && MO.isUndef()) &&
        !TargetRegisterInfo::isVirtualRegister(MO.getReg()))
      for (MCRegAliasIterator AI(MO.getReg(), TRI, true); AI.isValid(); ++AI)
        Candidates.reset(*AI);
  }

  // Try to find a register that's unused if there is one, as then we won't
  // have to spill.
  BitVector Available = getRegsAvailable(RC);
  Available &= Candidates;
  if (Available.any())
    Candidates = Available;

  // Find the register whose use is furthest away.
  MachineBasicBlock::iterator UseMI;
  unsigned SReg = findSurvivorReg(I, Candidates, 25, UseMI);

  // If we found an unused register there is no reason to spill it.
  if (!isRegUsed(SReg)) {
    LLVM_DEBUG(dbgs() << "Scavenged register: " << printReg(SReg, TRI) << "\n");
    return SReg;
  }

  ScavengedInfo &Scavenged = spill(SReg, *RC, SPAdj, I, UseMI);
  Scavenged.Restore = &*std::prev(UseMI);

  LLVM_DEBUG(dbgs() << "Scavenged register (with spill): "
                    << printReg(SReg, TRI) << "\n");

  return SReg;
}

unsigned RegScavenger::scavengeRegisterBackwards(const TargetRegisterClass &RC,
                                                 MachineBasicBlock::iterator To,
                                                 bool RestoreAfter, int SPAdj) {
  const MachineBasicBlock &MBB = *To->getParent();
  const MachineFunction &MF = *MBB.getParent();

  // Find the register whose use is furthest away.
  MachineBasicBlock::iterator UseMI;
  ArrayRef<MCPhysReg> AllocationOrder = RC.getRawAllocationOrder(MF);
  std::pair<MCPhysReg, MachineBasicBlock::iterator> P =
      findSurvivorBackwards(*MRI, MBBI, To, LiveUnits, AllocationOrder,
                            RestoreAfter);
  MCPhysReg Reg = P.first;
  MachineBasicBlock::iterator SpillBefore = P.second;
  assert(Reg != 0 && "No register left to scavenge!");
  // Found an available register?
  if (SpillBefore != MBB.end()) {
    MachineBasicBlock::iterator ReloadAfter =
      RestoreAfter ? std::next(MBBI) : MBBI;
    MachineBasicBlock::iterator ReloadBefore = std::next(ReloadAfter);
    if (ReloadBefore != MBB.end())
      LLVM_DEBUG(dbgs() << "Reload before: " << *ReloadBefore << '\n');
    ScavengedInfo &Scavenged = spill(Reg, RC, SPAdj, SpillBefore, ReloadBefore);
    Scavenged.Restore = &*std::prev(SpillBefore);
    LiveUnits.removeReg(Reg);
    LLVM_DEBUG(dbgs() << "Scavenged register with spill: " << printReg(Reg, TRI)
                      << " until " << *SpillBefore);
  } else {
    LLVM_DEBUG(dbgs() << "Scavenged free register: " << printReg(Reg, TRI)
                      << '\n');
  }
  return Reg;
}

/// Allocate a register for the virtual register \p VReg. The last use of
/// \p VReg is around the current position of the register scavenger \p RS.
/// \p ReserveAfter controls whether the scavenged register needs to be reserved
/// after the current instruction, otherwise it will only be reserved before the
/// current instruction.
static unsigned scavengeVReg(MachineRegisterInfo &MRI, RegScavenger &RS,
                             unsigned VReg, bool ReserveAfter) {
  const TargetRegisterInfo &TRI = *MRI.getTargetRegisterInfo();
#ifndef NDEBUG
  // Verify that all definitions and uses are in the same basic block.
  const MachineBasicBlock *CommonMBB = nullptr;
  // Real definition for the reg, re-definitions are not considered.
  const MachineInstr *RealDef = nullptr;
  for (MachineOperand &MO : MRI.reg_nodbg_operands(VReg)) {
    MachineBasicBlock *MBB = MO.getParent()->getParent();
    if (CommonMBB == nullptr)
      CommonMBB = MBB;
    assert(MBB == CommonMBB && "All defs+uses must be in the same basic block");
    if (MO.isDef()) {
      const MachineInstr &MI = *MO.getParent();
      if (!MI.readsRegister(VReg, &TRI)) {
        assert((!RealDef || RealDef == &MI) &&
               "Can have at most one definition which is not a redefinition");
        RealDef = &MI;
      }
    }
  }
  assert(RealDef != nullptr && "Must have at least 1 Def");
#endif

  // We should only have one definition of the register. However to accommodate
  // the requirements of two address code we also allow definitions in
  // subsequent instructions provided they also read the register. That way
  // we get a single contiguous lifetime.
  //
  // Definitions in MRI.def_begin() are unordered, search for the first.
  MachineRegisterInfo::def_iterator FirstDef =
    std::find_if(MRI.def_begin(VReg), MRI.def_end(),
                 [VReg, &TRI](const MachineOperand &MO) {
      return !MO.getParent()->readsRegister(VReg, &TRI);
    });
  assert(FirstDef != MRI.def_end() &&
         "Must have one definition that does not redefine vreg");
  MachineInstr &DefMI = *FirstDef->getParent();

  // The register scavenger will report a free register inserting an emergency
  // spill/reload if necessary.
  int SPAdj = 0;
  const TargetRegisterClass &RC = *MRI.getRegClass(VReg);
  unsigned SReg = RS.scavengeRegisterBackwards(RC, DefMI.getIterator(),
                                               ReserveAfter, SPAdj);
  MRI.replaceRegWith(VReg, SReg);
  ++NumScavengedRegs;
  return SReg;
}

/// Allocate (scavenge) vregs inside a single basic block.
/// Returns true if the target spill callback created new vregs and a 2nd pass
/// is necessary.
static bool scavengeFrameVirtualRegsInBlock(MachineRegisterInfo &MRI,
                                            RegScavenger &RS,
                                            MachineBasicBlock &MBB) {
  const TargetRegisterInfo &TRI = *MRI.getTargetRegisterInfo();
  RS.enterBasicBlockEnd(MBB);

  unsigned InitialNumVirtRegs = MRI.getNumVirtRegs();
  bool NextInstructionReadsVReg = false;
  for (MachineBasicBlock::iterator I = MBB.end(); I != MBB.begin(); ) {
    --I;
    // Move RegScavenger to the position between *I and *std::next(I).
    RS.backward(I);

    // Look for unassigned vregs in the uses of *std::next(I).
    if (NextInstructionReadsVReg) {
      MachineBasicBlock::iterator N = std::next(I);
      const MachineInstr &NMI = *N;
      for (const MachineOperand &MO : NMI.operands()) {
        if (!MO.isReg())
          continue;
        unsigned Reg = MO.getReg();
        // We only care about virtual registers and ignore virtual registers
        // created by the target callbacks in the process (those will be handled
        // in a scavenging round).
        if (!TargetRegisterInfo::isVirtualRegister(Reg) ||
            TargetRegisterInfo::virtReg2Index(Reg) >= InitialNumVirtRegs)
          continue;
        if (!MO.readsReg())
          continue;

        unsigned SReg = scavengeVReg(MRI, RS, Reg, true);
        N->addRegisterKilled(SReg, &TRI, false);
        RS.setRegUsed(SReg);
      }
    }

    // Look for unassigned vregs in the defs of *I.
    NextInstructionReadsVReg = false;
    const MachineInstr &MI = *I;
    for (const MachineOperand &MO : MI.operands()) {
      if (!MO.isReg())
        continue;
      unsigned Reg = MO.getReg();
      // Only vregs, no newly created vregs (see above).
      if (!TargetRegisterInfo::isVirtualRegister(Reg) ||
          TargetRegisterInfo::virtReg2Index(Reg) >= InitialNumVirtRegs)
        continue;
      // We have to look at all operands anyway so we can precalculate here
      // whether there is a reading operand. This allows use to skip the use
      // step in the next iteration if there was none.
      assert(!MO.isInternalRead() && "Cannot assign inside bundles");
      assert((!MO.isUndef() || MO.isDef()) && "Cannot handle undef uses");
      if (MO.readsReg()) {
        NextInstructionReadsVReg = true;
      }
      if (MO.isDef()) {
        unsigned SReg = scavengeVReg(MRI, RS, Reg, false);
        I->addRegisterDead(SReg, &TRI, false);
      }
    }
  }
#ifndef NDEBUG
  for (const MachineOperand &MO : MBB.front().operands()) {
    if (!MO.isReg() || !TargetRegisterInfo::isVirtualRegister(MO.getReg()))
      continue;
    assert(!MO.isInternalRead() && "Cannot assign inside bundles");
    assert((!MO.isUndef() || MO.isDef()) && "Cannot handle undef uses");
    assert(!MO.readsReg() && "Vreg use in first instruction not allowed");
  }
#endif

  return MRI.getNumVirtRegs() != InitialNumVirtRegs;
}

void llvm::scavengeFrameVirtualRegs(MachineFunction &MF, RegScavenger &RS) {
  // FIXME: Iterating over the instruction stream is unnecessary. We can simply
  // iterate over the vreg use list, which at this point only contains machine
  // operands for which eliminateFrameIndex need a new scratch reg.
  MachineRegisterInfo &MRI = MF.getRegInfo();
  // Shortcut.
  if (MRI.getNumVirtRegs() == 0) {
    MF.getProperties().set(MachineFunctionProperties::Property::NoVRegs);
    return;
  }

  // Run through the instructions and find any virtual registers.
  for (MachineBasicBlock &MBB : MF) {
    if (MBB.empty())
      continue;

    bool Again = scavengeFrameVirtualRegsInBlock(MRI, RS, MBB);
    if (Again) {
      LLVM_DEBUG(dbgs() << "Warning: Required two scavenging passes for block "
                        << MBB.getName() << '\n');
      Again = scavengeFrameVirtualRegsInBlock(MRI, RS, MBB);
      // The target required a 2nd run (because it created new vregs while
      // spilling). Refuse to do another pass to keep compiletime in check.
      if (Again)
        report_fatal_error("Incomplete scavenging after 2nd pass");
    }
  }

  MRI.clearVirtRegs();
  MF.getProperties().set(MachineFunctionProperties::Property::NoVRegs);
}

namespace {

/// This class runs register scavenging independ of the PrologEpilogInserter.
/// This is used in for testing.
class ScavengerTest : public MachineFunctionPass {
public:
  static char ID;

  ScavengerTest() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override {
    const TargetSubtargetInfo &STI = MF.getSubtarget();
    const TargetFrameLowering &TFL = *STI.getFrameLowering();

    RegScavenger RS;
    // Let's hope that calling those outside of PrologEpilogueInserter works
    // well enough to initialize the scavenger with some emergency spillslots
    // for the target.
    BitVector SavedRegs;
    TFL.determineCalleeSaves(MF, SavedRegs, &RS);
    TFL.processFunctionBeforeFrameFinalized(MF, &RS);

    // Let's scavenge the current function
    scavengeFrameVirtualRegs(MF, RS);
    return true;
  }
};

} // end anonymous namespace

char ScavengerTest::ID;

INITIALIZE_PASS(ScavengerTest, "scavenger-test",
                "Scavenge virtual registers inside basic blocks", false, false)
