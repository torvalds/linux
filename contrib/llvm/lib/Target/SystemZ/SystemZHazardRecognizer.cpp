//=-- SystemZHazardRecognizer.h - SystemZ Hazard Recognizer -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a hazard recognizer for the SystemZ scheduler.
//
// This class is used by the SystemZ scheduling strategy to maintain
// the state during scheduling, and provide cost functions for
// scheduling candidates. This includes:
//
// * Decoder grouping. A decoder group can maximally hold 3 uops, and
// instructions that always begin a new group should be scheduled when
// the current decoder group is empty.
// * Processor resources usage. It is beneficial to balance the use of
// resources.
//
// A goal is to consider all instructions, also those outside of any
// scheduling region. Such instructions are "advanced" past and include
// single instructions before a scheduling region, branches etc.
//
// A block that has only one predecessor continues scheduling with the state
// of it (which may be updated by emitting branches).
//
// ===---------------------------------------------------------------------===//

#include "SystemZHazardRecognizer.h"
#include "llvm/ADT/Statistic.h"

using namespace llvm;

#define DEBUG_TYPE "machine-scheduler"

// This is the limit of processor resource usage at which the
// scheduler should try to look for other instructions (not using the
// critical resource).
static cl::opt<int> ProcResCostLim("procres-cost-lim", cl::Hidden,
                                   cl::desc("The OOO window for processor "
                                            "resources during scheduling."),
                                   cl::init(8));

unsigned SystemZHazardRecognizer::
getNumDecoderSlots(SUnit *SU) const {
  const MCSchedClassDesc *SC = getSchedClass(SU);
  if (!SC->isValid())
    return 0; // IMPLICIT_DEF / KILL -- will not make impact in output.

  assert((SC->NumMicroOps != 2 || (SC->BeginGroup && !SC->EndGroup)) &&
         "Only cracked instruction can have 2 uops.");
  assert((SC->NumMicroOps < 3 || (SC->BeginGroup && SC->EndGroup)) &&
         "Expanded instructions always group alone.");
  assert((SC->NumMicroOps < 3 || (SC->NumMicroOps % 3 == 0)) &&
         "Expanded instructions fill the group(s).");

  return SC->NumMicroOps;
}

unsigned SystemZHazardRecognizer::getCurrCycleIdx(SUnit *SU) const {
  unsigned Idx = CurrGroupSize;
  if (GrpCount % 2)
    Idx += 3;

  if (SU != nullptr && !fitsIntoCurrentGroup(SU)) {
    if (Idx == 1 || Idx == 2)
      Idx = 3;
    else if (Idx == 4 || Idx == 5)
      Idx = 0;
  }

  return Idx;
}

ScheduleHazardRecognizer::HazardType SystemZHazardRecognizer::
getHazardType(SUnit *m, int Stalls) {
  return (fitsIntoCurrentGroup(m) ? NoHazard : Hazard);
}

void SystemZHazardRecognizer::Reset() {
  CurrGroupSize = 0;
  CurrGroupHas4RegOps = false;
  clearProcResCounters();
  GrpCount = 0;
  LastFPdOpCycleIdx = UINT_MAX;
  LastEmittedMI = nullptr;
  LLVM_DEBUG(CurGroupDbg = "";);
}

bool
SystemZHazardRecognizer::fitsIntoCurrentGroup(SUnit *SU) const {
  const MCSchedClassDesc *SC = getSchedClass(SU);
  if (!SC->isValid())
    return true;

  // A cracked instruction only fits into schedule if the current
  // group is empty.
  if (SC->BeginGroup)
    return (CurrGroupSize == 0);

  // An instruction with 4 register operands will not fit in last slot.
  assert ((CurrGroupSize < 2 || !CurrGroupHas4RegOps) &&
          "Current decoder group is already full!");
  if (CurrGroupSize == 2 && has4RegOps(SU->getInstr()))
    return false;

  // Since a full group is handled immediately in EmitInstruction(),
  // SU should fit into current group. NumSlots should be 1 or 0,
  // since it is not a cracked or expanded instruction.
  assert ((getNumDecoderSlots(SU) <= 1) && (CurrGroupSize < 3) &&
          "Expected normal instruction to fit in non-full group!");

  return true;
}

bool SystemZHazardRecognizer::has4RegOps(const MachineInstr *MI) const {
  const MachineFunction &MF = *MI->getParent()->getParent();
  const TargetRegisterInfo *TRI = &TII->getRegisterInfo();
  const MCInstrDesc &MID = MI->getDesc();
  unsigned Count = 0;
  for (unsigned OpIdx = 0; OpIdx < MID.getNumOperands(); OpIdx++) {
    const TargetRegisterClass *RC = TII->getRegClass(MID, OpIdx, TRI, MF);
    if (RC == nullptr)
      continue;
    if (OpIdx >= MID.getNumDefs() &&
        MID.getOperandConstraint(OpIdx, MCOI::TIED_TO) != -1)
      continue;
    Count++;
  }
  return Count >= 4;
}

void SystemZHazardRecognizer::nextGroup() {
  if (CurrGroupSize == 0)
    return;

  LLVM_DEBUG(dumpCurrGroup("Completed decode group"));
  LLVM_DEBUG(CurGroupDbg = "";);

  int NumGroups = ((CurrGroupSize > 3) ? (CurrGroupSize / 3) : 1);
  assert((CurrGroupSize <= 3 || CurrGroupSize % 3 == 0) &&
         "Current decoder group bad.");

  // Reset counter for next group.
  CurrGroupSize = 0;
  CurrGroupHas4RegOps = false;

  GrpCount += ((unsigned) NumGroups);

  // Decrease counters for execution units.
  for (unsigned i = 0; i < SchedModel->getNumProcResourceKinds(); ++i)
    ProcResourceCounters[i] = ((ProcResourceCounters[i] > NumGroups)
                                   ? (ProcResourceCounters[i] - NumGroups)
                                   : 0);

  // Clear CriticalResourceIdx if it is now below the threshold.
  if (CriticalResourceIdx != UINT_MAX &&
      (ProcResourceCounters[CriticalResourceIdx] <=
       ProcResCostLim))
    CriticalResourceIdx = UINT_MAX;

  LLVM_DEBUG(dumpState(););
}

#ifndef NDEBUG // Debug output
void SystemZHazardRecognizer::dumpSU(SUnit *SU, raw_ostream &OS) const {
  OS << "SU(" << SU->NodeNum << "):";
  OS << TII->getName(SU->getInstr()->getOpcode());

  const MCSchedClassDesc *SC = getSchedClass(SU);
  if (!SC->isValid())
    return;

  for (TargetSchedModel::ProcResIter
         PI = SchedModel->getWriteProcResBegin(SC),
         PE = SchedModel->getWriteProcResEnd(SC); PI != PE; ++PI) {
    const MCProcResourceDesc &PRD =
      *SchedModel->getProcResource(PI->ProcResourceIdx);
    std::string FU(PRD.Name);
    // trim e.g. Z13_FXaUnit -> FXa
    FU = FU.substr(FU.find("_") + 1);
    size_t Pos = FU.find("Unit");
    if (Pos != std::string::npos)
      FU.resize(Pos);
    if (FU == "LS") // LSUnit -> LSU
      FU = "LSU";
    OS << "/" << FU;

    if (PI->Cycles > 1)
      OS << "(" << PI->Cycles << "cyc)";
  }

  if (SC->NumMicroOps > 1)
    OS << "/" << SC->NumMicroOps << "uops";
  if (SC->BeginGroup && SC->EndGroup)
    OS << "/GroupsAlone";
  else if (SC->BeginGroup)
    OS << "/BeginsGroup";
  else if (SC->EndGroup)
    OS << "/EndsGroup";
  if (SU->isUnbuffered)
    OS << "/Unbuffered";
  if (has4RegOps(SU->getInstr()))
    OS << "/4RegOps";
}

void SystemZHazardRecognizer::dumpCurrGroup(std::string Msg) const {
  dbgs() << "++ " << Msg;
  dbgs() << ": ";

  if (CurGroupDbg.empty())
    dbgs() << " <empty>\n";
  else {
    dbgs() << "{ " << CurGroupDbg << " }";
    dbgs() << " (" << CurrGroupSize << " decoder slot"
           << (CurrGroupSize > 1 ? "s":"")
           << (CurrGroupHas4RegOps ? ", 4RegOps" : "")
           << ")\n";
  }
}

void SystemZHazardRecognizer::dumpProcResourceCounters() const {
  bool any = false;

  for (unsigned i = 0; i < SchedModel->getNumProcResourceKinds(); ++i)
    if (ProcResourceCounters[i] > 0) {
      any = true;
      break;
    }

  if (!any)
    return;

  dbgs() << "++ | Resource counters: ";
  for (unsigned i = 0; i < SchedModel->getNumProcResourceKinds(); ++i)
    if (ProcResourceCounters[i] > 0)
      dbgs() << SchedModel->getProcResource(i)->Name
             << ":" << ProcResourceCounters[i] << " ";
  dbgs() << "\n";

  if (CriticalResourceIdx != UINT_MAX)
    dbgs() << "++ | Critical resource: "
           << SchedModel->getProcResource(CriticalResourceIdx)->Name
           << "\n";
}

void SystemZHazardRecognizer::dumpState() const {
  dumpCurrGroup("| Current decoder group");
  dbgs() << "++ | Current cycle index: "
         << getCurrCycleIdx() << "\n";
  dumpProcResourceCounters();
  if (LastFPdOpCycleIdx != UINT_MAX)
    dbgs() << "++ | Last FPd cycle index: " << LastFPdOpCycleIdx << "\n";
}

#endif //NDEBUG

void SystemZHazardRecognizer::clearProcResCounters() {
  ProcResourceCounters.assign(SchedModel->getNumProcResourceKinds(), 0);
  CriticalResourceIdx = UINT_MAX;
}

static inline bool isBranchRetTrap(MachineInstr *MI) {
  return (MI->isBranch() || MI->isReturn() ||
          MI->getOpcode() == SystemZ::CondTrap);
}

// Update state with SU as the next scheduled unit.
void SystemZHazardRecognizer::
EmitInstruction(SUnit *SU) {
  const MCSchedClassDesc *SC = getSchedClass(SU);
  LLVM_DEBUG(dbgs() << "++ HazardRecognizer emitting "; dumpSU(SU, dbgs());
             dbgs() << "\n";);
  LLVM_DEBUG(dumpCurrGroup("Decode group before emission"););

  // If scheduling an SU that must begin a new decoder group, move on
  // to next group.
  if (!fitsIntoCurrentGroup(SU))
    nextGroup();

  LLVM_DEBUG(raw_string_ostream cgd(CurGroupDbg);
             if (CurGroupDbg.length()) cgd << ", "; dumpSU(SU, cgd););

  LastEmittedMI = SU->getInstr();

  // After returning from a call, we don't know much about the state.
  if (SU->isCall) {
    LLVM_DEBUG(dbgs() << "++ Clearing state after call.\n";);
    Reset();
    LastEmittedMI = SU->getInstr();
    return;
  }

  // Increase counter for execution unit(s).
  for (TargetSchedModel::ProcResIter
         PI = SchedModel->getWriteProcResBegin(SC),
         PE = SchedModel->getWriteProcResEnd(SC); PI != PE; ++PI) {
    // Don't handle FPd together with the other resources.
    if (SchedModel->getProcResource(PI->ProcResourceIdx)->BufferSize == 1)
      continue;
    int &CurrCounter =
      ProcResourceCounters[PI->ProcResourceIdx];
    CurrCounter += PI->Cycles;
    // Check if this is now the new critical resource.
    if ((CurrCounter > ProcResCostLim) &&
        (CriticalResourceIdx == UINT_MAX ||
         (PI->ProcResourceIdx != CriticalResourceIdx &&
          CurrCounter >
          ProcResourceCounters[CriticalResourceIdx]))) {
      LLVM_DEBUG(
          dbgs() << "++ New critical resource: "
                 << SchedModel->getProcResource(PI->ProcResourceIdx)->Name
                 << "\n";);
      CriticalResourceIdx = PI->ProcResourceIdx;
    }
  }

  // Make note of an instruction that uses a blocking resource (FPd).
  if (SU->isUnbuffered) {
    LastFPdOpCycleIdx = getCurrCycleIdx(SU);
    LLVM_DEBUG(dbgs() << "++ Last FPd cycle index: " << LastFPdOpCycleIdx
                      << "\n";);
  }

  // Insert SU into current group by increasing number of slots used
  // in current group.
  CurrGroupSize += getNumDecoderSlots(SU);
  CurrGroupHas4RegOps |= has4RegOps(SU->getInstr());
  unsigned GroupLim = (CurrGroupHas4RegOps ? 2 : 3);
  assert((CurrGroupSize <= GroupLim || CurrGroupSize == getNumDecoderSlots(SU))
         && "SU does not fit into decoder group!");

  // Check if current group is now full/ended. If so, move on to next
  // group to be ready to evaluate more candidates.
  if (CurrGroupSize >= GroupLim || SC->EndGroup)
    nextGroup();
}

int SystemZHazardRecognizer::groupingCost(SUnit *SU) const {
  const MCSchedClassDesc *SC = getSchedClass(SU);
  if (!SC->isValid())
    return 0;

  // If SU begins new group, it can either break a current group early
  // or fit naturally if current group is empty (negative cost).
  if (SC->BeginGroup) {
    if (CurrGroupSize)
      return 3 - CurrGroupSize;
    return -1;
  }

  // Similarly, a group-ending SU may either fit well (last in group), or
  // end the group prematurely.
  if (SC->EndGroup) {
    unsigned resultingGroupSize =
      (CurrGroupSize + getNumDecoderSlots(SU));
    if (resultingGroupSize < 3)
      return (3 - resultingGroupSize);
    return -1;
  }

  // An instruction with 4 register operands will not fit in last slot.
  if (CurrGroupSize == 2 && has4RegOps(SU->getInstr()))
    return 1;

  // Most instructions can be placed in any decoder slot.
  return 0;
}

bool SystemZHazardRecognizer::isFPdOpPreferred_distance(SUnit *SU) const {
  assert (SU->isUnbuffered);
  // If this is the first FPd op, it should be scheduled high.
  if (LastFPdOpCycleIdx == UINT_MAX)
    return true;
  // If this is not the first PFd op, it should go into the other side
  // of the processor to use the other FPd unit there. This should
  // generally happen if two FPd ops are placed with 2 other
  // instructions between them (modulo 6).
  unsigned SUCycleIdx = getCurrCycleIdx(SU);
  if (LastFPdOpCycleIdx > SUCycleIdx)
    return ((LastFPdOpCycleIdx - SUCycleIdx) == 3);
  return ((SUCycleIdx - LastFPdOpCycleIdx) == 3);
}

int SystemZHazardRecognizer::
resourcesCost(SUnit *SU) {
  int Cost = 0;

  const MCSchedClassDesc *SC = getSchedClass(SU);
  if (!SC->isValid())
    return 0;

  // For a FPd op, either return min or max value as indicated by the
  // distance to any prior FPd op.
  if (SU->isUnbuffered)
    Cost = (isFPdOpPreferred_distance(SU) ? INT_MIN : INT_MAX);
  // For other instructions, give a cost to the use of the critical resource.
  else if (CriticalResourceIdx != UINT_MAX) {
    for (TargetSchedModel::ProcResIter
           PI = SchedModel->getWriteProcResBegin(SC),
           PE = SchedModel->getWriteProcResEnd(SC); PI != PE; ++PI)
      if (PI->ProcResourceIdx == CriticalResourceIdx)
        Cost = PI->Cycles;
  }

  return Cost;
}

void SystemZHazardRecognizer::emitInstruction(MachineInstr *MI,
                                              bool TakenBranch) {
  // Make a temporary SUnit.
  SUnit SU(MI, 0);

  // Set interesting flags.
  SU.isCall = MI->isCall();

  const MCSchedClassDesc *SC = SchedModel->resolveSchedClass(MI);
  for (const MCWriteProcResEntry &PRE :
         make_range(SchedModel->getWriteProcResBegin(SC),
                    SchedModel->getWriteProcResEnd(SC))) {
    switch (SchedModel->getProcResource(PRE.ProcResourceIdx)->BufferSize) {
    case 0:
      SU.hasReservedResource = true;
      break;
    case 1:
      SU.isUnbuffered = true;
      break;
    default:
      break;
    }
  }

  unsigned GroupSizeBeforeEmit = CurrGroupSize;
  EmitInstruction(&SU);

  if (!TakenBranch && isBranchRetTrap(MI)) {
    // NT Branch on second slot ends group.
    if (GroupSizeBeforeEmit == 1)
      nextGroup();
  }

  if (TakenBranch && CurrGroupSize > 0)
    nextGroup();

  assert ((!MI->isTerminator() || isBranchRetTrap(MI)) &&
          "Scheduler: unhandled terminator!");
}

void SystemZHazardRecognizer::
copyState(SystemZHazardRecognizer *Incoming) {
  // Current decoder group
  CurrGroupSize = Incoming->CurrGroupSize;
  LLVM_DEBUG(CurGroupDbg = Incoming->CurGroupDbg;);

  // Processor resources
  ProcResourceCounters = Incoming->ProcResourceCounters;
  CriticalResourceIdx = Incoming->CriticalResourceIdx;

  // FPd
  LastFPdOpCycleIdx = Incoming->LastFPdOpCycleIdx;
  GrpCount = Incoming->GrpCount;
}
