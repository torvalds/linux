//===- CriticalAntiDepBreaker.cpp - Anti-dep breaker ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the CriticalAntiDepBreaker class, which
// implements register anti-dependence breaking along a blocks
// critical path during post-RA scheduler.
//
//===----------------------------------------------------------------------===//

#include "CriticalAntiDepBreaker.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterClassInfo.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <map>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "post-RA-sched"

CriticalAntiDepBreaker::CriticalAntiDepBreaker(MachineFunction &MFi,
                                               const RegisterClassInfo &RCI)
    : AntiDepBreaker(), MF(MFi), MRI(MF.getRegInfo()),
      TII(MF.getSubtarget().getInstrInfo()),
      TRI(MF.getSubtarget().getRegisterInfo()), RegClassInfo(RCI),
      Classes(TRI->getNumRegs(), nullptr), KillIndices(TRI->getNumRegs(), 0),
      DefIndices(TRI->getNumRegs(), 0), KeepRegs(TRI->getNumRegs(), false) {}

CriticalAntiDepBreaker::~CriticalAntiDepBreaker() = default;

void CriticalAntiDepBreaker::StartBlock(MachineBasicBlock *BB) {
  const unsigned BBSize = BB->size();
  for (unsigned i = 0, e = TRI->getNumRegs(); i != e; ++i) {
    // Clear out the register class data.
    Classes[i] = nullptr;

    // Initialize the indices to indicate that no registers are live.
    KillIndices[i] = ~0u;
    DefIndices[i] = BBSize;
  }

  // Clear "do not change" set.
  KeepRegs.reset();

  bool IsReturnBlock = BB->isReturnBlock();

  // Examine the live-in regs of all successors.
  for (MachineBasicBlock::succ_iterator SI = BB->succ_begin(),
         SE = BB->succ_end(); SI != SE; ++SI)
    for (const auto &LI : (*SI)->liveins()) {
      for (MCRegAliasIterator AI(LI.PhysReg, TRI, true); AI.isValid(); ++AI) {
        unsigned Reg = *AI;
        Classes[Reg] = reinterpret_cast<TargetRegisterClass *>(-1);
        KillIndices[Reg] = BBSize;
        DefIndices[Reg] = ~0u;
      }
    }

  // Mark live-out callee-saved registers. In a return block this is
  // all callee-saved registers. In non-return this is any
  // callee-saved register that is not saved in the prolog.
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  BitVector Pristine = MFI.getPristineRegs(MF);
  for (const MCPhysReg *I = MF.getRegInfo().getCalleeSavedRegs(); *I;
       ++I) {
    unsigned Reg = *I;
    if (!IsReturnBlock && !Pristine.test(Reg))
      continue;
    for (MCRegAliasIterator AI(*I, TRI, true); AI.isValid(); ++AI) {
      unsigned Reg = *AI;
      Classes[Reg] = reinterpret_cast<TargetRegisterClass *>(-1);
      KillIndices[Reg] = BBSize;
      DefIndices[Reg] = ~0u;
    }
  }
}

void CriticalAntiDepBreaker::FinishBlock() {
  RegRefs.clear();
  KeepRegs.reset();
}

void CriticalAntiDepBreaker::Observe(MachineInstr &MI, unsigned Count,
                                     unsigned InsertPosIndex) {
  // Kill instructions can define registers but are really nops, and there might
  // be a real definition earlier that needs to be paired with uses dominated by
  // this kill.

  // FIXME: It may be possible to remove the isKill() restriction once PR18663
  // has been properly fixed. There can be value in processing kills as seen in
  // the AggressiveAntiDepBreaker class.
  if (MI.isDebugInstr() || MI.isKill())
    return;
  assert(Count < InsertPosIndex && "Instruction index out of expected range!");

  for (unsigned Reg = 0; Reg != TRI->getNumRegs(); ++Reg) {
    if (KillIndices[Reg] != ~0u) {
      // If Reg is currently live, then mark that it can't be renamed as
      // we don't know the extent of its live-range anymore (now that it
      // has been scheduled).
      Classes[Reg] = reinterpret_cast<TargetRegisterClass *>(-1);
      KillIndices[Reg] = Count;
    } else if (DefIndices[Reg] < InsertPosIndex && DefIndices[Reg] >= Count) {
      // Any register which was defined within the previous scheduling region
      // may have been rescheduled and its lifetime may overlap with registers
      // in ways not reflected in our current liveness state. For each such
      // register, adjust the liveness state to be conservatively correct.
      Classes[Reg] = reinterpret_cast<TargetRegisterClass *>(-1);

      // Move the def index to the end of the previous region, to reflect
      // that the def could theoretically have been scheduled at the end.
      DefIndices[Reg] = InsertPosIndex;
    }
  }

  PrescanInstruction(MI);
  ScanInstruction(MI, Count);
}

/// CriticalPathStep - Return the next SUnit after SU on the bottom-up
/// critical path.
static const SDep *CriticalPathStep(const SUnit *SU) {
  const SDep *Next = nullptr;
  unsigned NextDepth = 0;
  // Find the predecessor edge with the greatest depth.
  for (SUnit::const_pred_iterator P = SU->Preds.begin(), PE = SU->Preds.end();
       P != PE; ++P) {
    const SUnit *PredSU = P->getSUnit();
    unsigned PredLatency = P->getLatency();
    unsigned PredTotalLatency = PredSU->getDepth() + PredLatency;
    // In the case of a latency tie, prefer an anti-dependency edge over
    // other types of edges.
    if (NextDepth < PredTotalLatency ||
        (NextDepth == PredTotalLatency && P->getKind() == SDep::Anti)) {
      NextDepth = PredTotalLatency;
      Next = &*P;
    }
  }
  return Next;
}

void CriticalAntiDepBreaker::PrescanInstruction(MachineInstr &MI) {
  // It's not safe to change register allocation for source operands of
  // instructions that have special allocation requirements. Also assume all
  // registers used in a call must not be changed (ABI).
  // FIXME: The issue with predicated instruction is more complex. We are being
  // conservative here because the kill markers cannot be trusted after
  // if-conversion:
  // %r6 = LDR %sp, %reg0, 92, 14, %reg0; mem:LD4[FixedStack14]
  // ...
  // STR %r0, killed %r6, %reg0, 0, 0, %cpsr; mem:ST4[%395]
  // %r6 = LDR %sp, %reg0, 100, 0, %cpsr; mem:LD4[FixedStack12]
  // STR %r0, killed %r6, %reg0, 0, 14, %reg0; mem:ST4[%396](align=8)
  //
  // The first R6 kill is not really a kill since it's killed by a predicated
  // instruction which may not be executed. The second R6 def may or may not
  // re-define R6 so it's not safe to change it since the last R6 use cannot be
  // changed.
  bool Special =
      MI.isCall() || MI.hasExtraSrcRegAllocReq() || TII->isPredicated(MI);

  // Scan the register operands for this instruction and update
  // Classes and RegRefs.
  for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
    MachineOperand &MO = MI.getOperand(i);
    if (!MO.isReg()) continue;
    unsigned Reg = MO.getReg();
    if (Reg == 0) continue;
    const TargetRegisterClass *NewRC = nullptr;

    if (i < MI.getDesc().getNumOperands())
      NewRC = TII->getRegClass(MI.getDesc(), i, TRI, MF);

    // For now, only allow the register to be changed if its register
    // class is consistent across all uses.
    if (!Classes[Reg] && NewRC)
      Classes[Reg] = NewRC;
    else if (!NewRC || Classes[Reg] != NewRC)
      Classes[Reg] = reinterpret_cast<TargetRegisterClass *>(-1);

    // Now check for aliases.
    for (MCRegAliasIterator AI(Reg, TRI, false); AI.isValid(); ++AI) {
      // If an alias of the reg is used during the live range, give up.
      // Note that this allows us to skip checking if AntiDepReg
      // overlaps with any of the aliases, among other things.
      unsigned AliasReg = *AI;
      if (Classes[AliasReg]) {
        Classes[AliasReg] = reinterpret_cast<TargetRegisterClass *>(-1);
        Classes[Reg] = reinterpret_cast<TargetRegisterClass *>(-1);
      }
    }

    // If we're still willing to consider this register, note the reference.
    if (Classes[Reg] != reinterpret_cast<TargetRegisterClass *>(-1))
      RegRefs.insert(std::make_pair(Reg, &MO));

    // If this reg is tied and live (Classes[Reg] is set to -1), we can't change
    // it or any of its sub or super regs. We need to use KeepRegs to mark the
    // reg because not all uses of the same reg within an instruction are
    // necessarily tagged as tied.
    // Example: an x86 "xor %eax, %eax" will have one source operand tied to the
    // def register but not the second (see PR20020 for details).
    // FIXME: can this check be relaxed to account for undef uses
    // of a register? In the above 'xor' example, the uses of %eax are undef, so
    // earlier instructions could still replace %eax even though the 'xor'
    // itself can't be changed.
    if (MI.isRegTiedToUseOperand(i) &&
        Classes[Reg] == reinterpret_cast<TargetRegisterClass *>(-1)) {
      for (MCSubRegIterator SubRegs(Reg, TRI, /*IncludeSelf=*/true);
           SubRegs.isValid(); ++SubRegs) {
        KeepRegs.set(*SubRegs);
      }
      for (MCSuperRegIterator SuperRegs(Reg, TRI);
           SuperRegs.isValid(); ++SuperRegs) {
        KeepRegs.set(*SuperRegs);
      }
    }

    if (MO.isUse() && Special) {
      if (!KeepRegs.test(Reg)) {
        for (MCSubRegIterator SubRegs(Reg, TRI, /*IncludeSelf=*/true);
             SubRegs.isValid(); ++SubRegs)
          KeepRegs.set(*SubRegs);
      }
    }
  }
}

void CriticalAntiDepBreaker::ScanInstruction(MachineInstr &MI, unsigned Count) {
  // Update liveness.
  // Proceeding upwards, registers that are defed but not used in this
  // instruction are now dead.
  assert(!MI.isKill() && "Attempting to scan a kill instruction");

  if (!TII->isPredicated(MI)) {
    // Predicated defs are modeled as read + write, i.e. similar to two
    // address updates.
    for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
      MachineOperand &MO = MI.getOperand(i);

      if (MO.isRegMask())
        for (unsigned i = 0, e = TRI->getNumRegs(); i != e; ++i)
          if (MO.clobbersPhysReg(i)) {
            DefIndices[i] = Count;
            KillIndices[i] = ~0u;
            KeepRegs.reset(i);
            Classes[i] = nullptr;
            RegRefs.erase(i);
          }

      if (!MO.isReg()) continue;
      unsigned Reg = MO.getReg();
      if (Reg == 0) continue;
      if (!MO.isDef()) continue;

      // Ignore two-addr defs.
      if (MI.isRegTiedToUseOperand(i))
        continue;

      // If we've already marked this reg as unchangeable, don't remove
      // it or any of its subregs from KeepRegs.
      bool Keep = KeepRegs.test(Reg);

      // For the reg itself and all subregs: update the def to current;
      // reset the kill state, any restrictions, and references.
      for (MCSubRegIterator SRI(Reg, TRI, true); SRI.isValid(); ++SRI) {
        unsigned SubregReg = *SRI;
        DefIndices[SubregReg] = Count;
        KillIndices[SubregReg] = ~0u;
        Classes[SubregReg] = nullptr;
        RegRefs.erase(SubregReg);
        if (!Keep)
          KeepRegs.reset(SubregReg);
      }
      // Conservatively mark super-registers as unusable.
      for (MCSuperRegIterator SR(Reg, TRI); SR.isValid(); ++SR)
        Classes[*SR] = reinterpret_cast<TargetRegisterClass *>(-1);
    }
  }
  for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
    MachineOperand &MO = MI.getOperand(i);
    if (!MO.isReg()) continue;
    unsigned Reg = MO.getReg();
    if (Reg == 0) continue;
    if (!MO.isUse()) continue;

    const TargetRegisterClass *NewRC = nullptr;
    if (i < MI.getDesc().getNumOperands())
      NewRC = TII->getRegClass(MI.getDesc(), i, TRI, MF);

    // For now, only allow the register to be changed if its register
    // class is consistent across all uses.
    if (!Classes[Reg] && NewRC)
      Classes[Reg] = NewRC;
    else if (!NewRC || Classes[Reg] != NewRC)
      Classes[Reg] = reinterpret_cast<TargetRegisterClass *>(-1);

    RegRefs.insert(std::make_pair(Reg, &MO));

    // It wasn't previously live but now it is, this is a kill.
    // Repeat for all aliases.
    for (MCRegAliasIterator AI(Reg, TRI, true); AI.isValid(); ++AI) {
      unsigned AliasReg = *AI;
      if (KillIndices[AliasReg] == ~0u) {
        KillIndices[AliasReg] = Count;
        DefIndices[AliasReg] = ~0u;
      }
    }
  }
}

// Check all machine operands that reference the antidependent register and must
// be replaced by NewReg. Return true if any of their parent instructions may
// clobber the new register.
//
// Note: AntiDepReg may be referenced by a two-address instruction such that
// it's use operand is tied to a def operand. We guard against the case in which
// the two-address instruction also defines NewReg, as may happen with
// pre/postincrement loads. In this case, both the use and def operands are in
// RegRefs because the def is inserted by PrescanInstruction and not erased
// during ScanInstruction. So checking for an instruction with definitions of
// both NewReg and AntiDepReg covers it.
bool
CriticalAntiDepBreaker::isNewRegClobberedByRefs(RegRefIter RegRefBegin,
                                                RegRefIter RegRefEnd,
                                                unsigned NewReg) {
  for (RegRefIter I = RegRefBegin; I != RegRefEnd; ++I ) {
    MachineOperand *RefOper = I->second;

    // Don't allow the instruction defining AntiDepReg to earlyclobber its
    // operands, in case they may be assigned to NewReg. In this case antidep
    // breaking must fail, but it's too rare to bother optimizing.
    if (RefOper->isDef() && RefOper->isEarlyClobber())
      return true;

    // Handle cases in which this instruction defines NewReg.
    MachineInstr *MI = RefOper->getParent();
    for (unsigned i = 0, e = MI->getNumOperands(); i != e; ++i) {
      const MachineOperand &CheckOper = MI->getOperand(i);

      if (CheckOper.isRegMask() && CheckOper.clobbersPhysReg(NewReg))
        return true;

      if (!CheckOper.isReg() || !CheckOper.isDef() ||
          CheckOper.getReg() != NewReg)
        continue;

      // Don't allow the instruction to define NewReg and AntiDepReg.
      // When AntiDepReg is renamed it will be an illegal op.
      if (RefOper->isDef())
        return true;

      // Don't allow an instruction using AntiDepReg to be earlyclobbered by
      // NewReg.
      if (CheckOper.isEarlyClobber())
        return true;

      // Don't allow inline asm to define NewReg at all. Who knows what it's
      // doing with it.
      if (MI->isInlineAsm())
        return true;
    }
  }
  return false;
}

unsigned CriticalAntiDepBreaker::
findSuitableFreeRegister(RegRefIter RegRefBegin,
                         RegRefIter RegRefEnd,
                         unsigned AntiDepReg,
                         unsigned LastNewReg,
                         const TargetRegisterClass *RC,
                         SmallVectorImpl<unsigned> &Forbid) {
  ArrayRef<MCPhysReg> Order = RegClassInfo.getOrder(RC);
  for (unsigned i = 0; i != Order.size(); ++i) {
    unsigned NewReg = Order[i];
    // Don't replace a register with itself.
    if (NewReg == AntiDepReg) continue;
    // Don't replace a register with one that was recently used to repair
    // an anti-dependence with this AntiDepReg, because that would
    // re-introduce that anti-dependence.
    if (NewReg == LastNewReg) continue;
    // If any instructions that define AntiDepReg also define the NewReg, it's
    // not suitable.  For example, Instruction with multiple definitions can
    // result in this condition.
    if (isNewRegClobberedByRefs(RegRefBegin, RegRefEnd, NewReg)) continue;
    // If NewReg is dead and NewReg's most recent def is not before
    // AntiDepReg's kill, it's safe to replace AntiDepReg with NewReg.
    assert(((KillIndices[AntiDepReg] == ~0u) != (DefIndices[AntiDepReg] == ~0u))
           && "Kill and Def maps aren't consistent for AntiDepReg!");
    assert(((KillIndices[NewReg] == ~0u) != (DefIndices[NewReg] == ~0u))
           && "Kill and Def maps aren't consistent for NewReg!");
    if (KillIndices[NewReg] != ~0u ||
        Classes[NewReg] == reinterpret_cast<TargetRegisterClass *>(-1) ||
        KillIndices[AntiDepReg] > DefIndices[NewReg])
      continue;
    // If NewReg overlaps any of the forbidden registers, we can't use it.
    bool Forbidden = false;
    for (SmallVectorImpl<unsigned>::iterator it = Forbid.begin(),
           ite = Forbid.end(); it != ite; ++it)
      if (TRI->regsOverlap(NewReg, *it)) {
        Forbidden = true;
        break;
      }
    if (Forbidden) continue;
    return NewReg;
  }

  // No registers are free and available!
  return 0;
}

unsigned CriticalAntiDepBreaker::
BreakAntiDependencies(const std::vector<SUnit> &SUnits,
                      MachineBasicBlock::iterator Begin,
                      MachineBasicBlock::iterator End,
                      unsigned InsertPosIndex,
                      DbgValueVector &DbgValues) {
  // The code below assumes that there is at least one instruction,
  // so just duck out immediately if the block is empty.
  if (SUnits.empty()) return 0;

  // Keep a map of the MachineInstr*'s back to the SUnit representing them.
  // This is used for updating debug information.
  //
  // FIXME: Replace this with the existing map in ScheduleDAGInstrs::MISUnitMap
  DenseMap<MachineInstr *, const SUnit *> MISUnitMap;

  // Find the node at the bottom of the critical path.
  const SUnit *Max = nullptr;
  for (unsigned i = 0, e = SUnits.size(); i != e; ++i) {
    const SUnit *SU = &SUnits[i];
    MISUnitMap[SU->getInstr()] = SU;
    if (!Max || SU->getDepth() + SU->Latency > Max->getDepth() + Max->Latency)
      Max = SU;
  }

#ifndef NDEBUG
  {
    LLVM_DEBUG(dbgs() << "Critical path has total latency "
                      << (Max->getDepth() + Max->Latency) << "\n");
    LLVM_DEBUG(dbgs() << "Available regs:");
    for (unsigned Reg = 0; Reg < TRI->getNumRegs(); ++Reg) {
      if (KillIndices[Reg] == ~0u)
        LLVM_DEBUG(dbgs() << " " << printReg(Reg, TRI));
    }
    LLVM_DEBUG(dbgs() << '\n');
  }
#endif

  // Track progress along the critical path through the SUnit graph as we walk
  // the instructions.
  const SUnit *CriticalPathSU = Max;
  MachineInstr *CriticalPathMI = CriticalPathSU->getInstr();

  // Consider this pattern:
  //   A = ...
  //   ... = A
  //   A = ...
  //   ... = A
  //   A = ...
  //   ... = A
  //   A = ...
  //   ... = A
  // There are three anti-dependencies here, and without special care,
  // we'd break all of them using the same register:
  //   A = ...
  //   ... = A
  //   B = ...
  //   ... = B
  //   B = ...
  //   ... = B
  //   B = ...
  //   ... = B
  // because at each anti-dependence, B is the first register that
  // isn't A which is free.  This re-introduces anti-dependencies
  // at all but one of the original anti-dependencies that we were
  // trying to break.  To avoid this, keep track of the most recent
  // register that each register was replaced with, avoid
  // using it to repair an anti-dependence on the same register.
  // This lets us produce this:
  //   A = ...
  //   ... = A
  //   B = ...
  //   ... = B
  //   C = ...
  //   ... = C
  //   B = ...
  //   ... = B
  // This still has an anti-dependence on B, but at least it isn't on the
  // original critical path.
  //
  // TODO: If we tracked more than one register here, we could potentially
  // fix that remaining critical edge too. This is a little more involved,
  // because unlike the most recent register, less recent registers should
  // still be considered, though only if no other registers are available.
  std::vector<unsigned> LastNewReg(TRI->getNumRegs(), 0);

  // Attempt to break anti-dependence edges on the critical path. Walk the
  // instructions from the bottom up, tracking information about liveness
  // as we go to help determine which registers are available.
  unsigned Broken = 0;
  unsigned Count = InsertPosIndex - 1;
  for (MachineBasicBlock::iterator I = End, E = Begin; I != E; --Count) {
    MachineInstr &MI = *--I;
    // Kill instructions can define registers but are really nops, and there
    // might be a real definition earlier that needs to be paired with uses
    // dominated by this kill.

    // FIXME: It may be possible to remove the isKill() restriction once PR18663
    // has been properly fixed. There can be value in processing kills as seen
    // in the AggressiveAntiDepBreaker class.
    if (MI.isDebugInstr() || MI.isKill())
      continue;

    // Check if this instruction has a dependence on the critical path that
    // is an anti-dependence that we may be able to break. If it is, set
    // AntiDepReg to the non-zero register associated with the anti-dependence.
    //
    // We limit our attention to the critical path as a heuristic to avoid
    // breaking anti-dependence edges that aren't going to significantly
    // impact the overall schedule. There are a limited number of registers
    // and we want to save them for the important edges.
    //
    // TODO: Instructions with multiple defs could have multiple
    // anti-dependencies. The current code here only knows how to break one
    // edge per instruction. Note that we'd have to be able to break all of
    // the anti-dependencies in an instruction in order to be effective.
    unsigned AntiDepReg = 0;
    if (&MI == CriticalPathMI) {
      if (const SDep *Edge = CriticalPathStep(CriticalPathSU)) {
        const SUnit *NextSU = Edge->getSUnit();

        // Only consider anti-dependence edges.
        if (Edge->getKind() == SDep::Anti) {
          AntiDepReg = Edge->getReg();
          assert(AntiDepReg != 0 && "Anti-dependence on reg0?");
          if (!MRI.isAllocatable(AntiDepReg))
            // Don't break anti-dependencies on non-allocatable registers.
            AntiDepReg = 0;
          else if (KeepRegs.test(AntiDepReg))
            // Don't break anti-dependencies if a use down below requires
            // this exact register.
            AntiDepReg = 0;
          else {
            // If the SUnit has other dependencies on the SUnit that it
            // anti-depends on, don't bother breaking the anti-dependency
            // since those edges would prevent such units from being
            // scheduled past each other regardless.
            //
            // Also, if there are dependencies on other SUnits with the
            // same register as the anti-dependency, don't attempt to
            // break it.
            for (SUnit::const_pred_iterator P = CriticalPathSU->Preds.begin(),
                 PE = CriticalPathSU->Preds.end(); P != PE; ++P)
              if (P->getSUnit() == NextSU ?
                    (P->getKind() != SDep::Anti || P->getReg() != AntiDepReg) :
                    (P->getKind() == SDep::Data && P->getReg() == AntiDepReg)) {
                AntiDepReg = 0;
                break;
              }
          }
        }
        CriticalPathSU = NextSU;
        CriticalPathMI = CriticalPathSU->getInstr();
      } else {
        // We've reached the end of the critical path.
        CriticalPathSU = nullptr;
        CriticalPathMI = nullptr;
      }
    }

    PrescanInstruction(MI);

    SmallVector<unsigned, 2> ForbidRegs;

    // If MI's defs have a special allocation requirement, don't allow
    // any def registers to be changed. Also assume all registers
    // defined in a call must not be changed (ABI).
    if (MI.isCall() || MI.hasExtraDefRegAllocReq() || TII->isPredicated(MI))
      // If this instruction's defs have special allocation requirement, don't
      // break this anti-dependency.
      AntiDepReg = 0;
    else if (AntiDepReg) {
      // If this instruction has a use of AntiDepReg, breaking it
      // is invalid.  If the instruction defines other registers,
      // save a list of them so that we don't pick a new register
      // that overlaps any of them.
      for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
        MachineOperand &MO = MI.getOperand(i);
        if (!MO.isReg()) continue;
        unsigned Reg = MO.getReg();
        if (Reg == 0) continue;
        if (MO.isUse() && TRI->regsOverlap(AntiDepReg, Reg)) {
          AntiDepReg = 0;
          break;
        }
        if (MO.isDef() && Reg != AntiDepReg)
          ForbidRegs.push_back(Reg);
      }
    }

    // Determine AntiDepReg's register class, if it is live and is
    // consistently used within a single class.
    const TargetRegisterClass *RC = AntiDepReg != 0 ? Classes[AntiDepReg]
                                                    : nullptr;
    assert((AntiDepReg == 0 || RC != nullptr) &&
           "Register should be live if it's causing an anti-dependence!");
    if (RC == reinterpret_cast<TargetRegisterClass *>(-1))
      AntiDepReg = 0;

    // Look for a suitable register to use to break the anti-dependence.
    //
    // TODO: Instead of picking the first free register, consider which might
    // be the best.
    if (AntiDepReg != 0) {
      std::pair<std::multimap<unsigned, MachineOperand *>::iterator,
                std::multimap<unsigned, MachineOperand *>::iterator>
        Range = RegRefs.equal_range(AntiDepReg);
      if (unsigned NewReg = findSuitableFreeRegister(Range.first, Range.second,
                                                     AntiDepReg,
                                                     LastNewReg[AntiDepReg],
                                                     RC, ForbidRegs)) {
        LLVM_DEBUG(dbgs() << "Breaking anti-dependence edge on "
                          << printReg(AntiDepReg, TRI) << " with "
                          << RegRefs.count(AntiDepReg) << " references"
                          << " using " << printReg(NewReg, TRI) << "!\n");

        // Update the references to the old register to refer to the new
        // register.
        for (std::multimap<unsigned, MachineOperand *>::iterator
             Q = Range.first, QE = Range.second; Q != QE; ++Q) {
          Q->second->setReg(NewReg);
          // If the SU for the instruction being updated has debug information
          // related to the anti-dependency register, make sure to update that
          // as well.
          const SUnit *SU = MISUnitMap[Q->second->getParent()];
          if (!SU) continue;
          UpdateDbgValues(DbgValues, Q->second->getParent(),
                          AntiDepReg, NewReg);
        }

        // We just went back in time and modified history; the
        // liveness information for the anti-dependence reg is now
        // inconsistent. Set the state as if it were dead.
        Classes[NewReg] = Classes[AntiDepReg];
        DefIndices[NewReg] = DefIndices[AntiDepReg];
        KillIndices[NewReg] = KillIndices[AntiDepReg];
        assert(((KillIndices[NewReg] == ~0u) !=
                (DefIndices[NewReg] == ~0u)) &&
             "Kill and Def maps aren't consistent for NewReg!");

        Classes[AntiDepReg] = nullptr;
        DefIndices[AntiDepReg] = KillIndices[AntiDepReg];
        KillIndices[AntiDepReg] = ~0u;
        assert(((KillIndices[AntiDepReg] == ~0u) !=
                (DefIndices[AntiDepReg] == ~0u)) &&
             "Kill and Def maps aren't consistent for AntiDepReg!");

        RegRefs.erase(AntiDepReg);
        LastNewReg[AntiDepReg] = NewReg;
        ++Broken;
      }
    }

    ScanInstruction(MI, Count);
  }

  return Broken;
}
