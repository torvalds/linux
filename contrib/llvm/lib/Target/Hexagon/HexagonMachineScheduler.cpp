//===- HexagonMachineScheduler.cpp - MI Scheduler for Hexagon -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// MachineScheduler schedules machine instructions after phi elimination. It
// preserves LiveIntervals so it can be invoked before register allocation.
//
//===----------------------------------------------------------------------===//

#include "HexagonMachineScheduler.h"
#include "HexagonInstrInfo.h"
#include "HexagonSubtarget.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/DFAPacketizer.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/RegisterClassInfo.h"
#include "llvm/CodeGen/RegisterPressure.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/CodeGen/ScheduleHazardRecognizer.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSchedule.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>

using namespace llvm;

#define DEBUG_TYPE "machine-scheduler"

static cl::opt<bool> IgnoreBBRegPressure("ignore-bb-reg-pressure",
    cl::Hidden, cl::ZeroOrMore, cl::init(false));

static cl::opt<bool> UseNewerCandidate("use-newer-candidate",
    cl::Hidden, cl::ZeroOrMore, cl::init(true));

static cl::opt<unsigned> SchedDebugVerboseLevel("misched-verbose-level",
    cl::Hidden, cl::ZeroOrMore, cl::init(1));

// Check if the scheduler should penalize instructions that are available to
// early due to a zero-latency dependence.
static cl::opt<bool> CheckEarlyAvail("check-early-avail", cl::Hidden,
    cl::ZeroOrMore, cl::init(true));

// This value is used to determine if a register class is a high pressure set.
// We compute the maximum number of registers needed and divided by the total
// available. Then, we compare the result to this value.
static cl::opt<float> RPThreshold("hexagon-reg-pressure", cl::Hidden,
    cl::init(0.75f), cl::desc("High register pressure threhold."));

/// Return true if there is a dependence between SUd and SUu.
static bool hasDependence(const SUnit *SUd, const SUnit *SUu,
                          const HexagonInstrInfo &QII) {
  if (SUd->Succs.size() == 0)
    return false;

  // Enable .cur formation.
  if (QII.mayBeCurLoad(*SUd->getInstr()))
    return false;

  if (QII.canExecuteInBundle(*SUd->getInstr(), *SUu->getInstr()))
    return false;

  for (const auto &S : SUd->Succs) {
    // Since we do not add pseudos to packets, might as well
    // ignore order dependencies.
    if (S.isCtrl())
      continue;

    if (S.getSUnit() == SUu && S.getLatency() > 0)
      return true;
  }
  return false;
}

/// Check if scheduling of this SU is possible
/// in the current packet.
/// It is _not_ precise (statefull), it is more like
/// another heuristic. Many corner cases are figured
/// empirically.
bool VLIWResourceModel::isResourceAvailable(SUnit *SU, bool IsTop) {
  if (!SU || !SU->getInstr())
    return false;

  // First see if the pipeline could receive this instruction
  // in the current cycle.
  switch (SU->getInstr()->getOpcode()) {
  default:
    if (!ResourcesModel->canReserveResources(*SU->getInstr()))
      return false;
    break;
  case TargetOpcode::EXTRACT_SUBREG:
  case TargetOpcode::INSERT_SUBREG:
  case TargetOpcode::SUBREG_TO_REG:
  case TargetOpcode::REG_SEQUENCE:
  case TargetOpcode::IMPLICIT_DEF:
  case TargetOpcode::COPY:
  case TargetOpcode::INLINEASM:
    break;
  }

  MachineBasicBlock *MBB = SU->getInstr()->getParent();
  auto &QST = MBB->getParent()->getSubtarget<HexagonSubtarget>();
  const auto &QII = *QST.getInstrInfo();

  // Now see if there are no other dependencies to instructions already
  // in the packet.
  if (IsTop) {
    for (unsigned i = 0, e = Packet.size(); i != e; ++i)
      if (hasDependence(Packet[i], SU, QII))
        return false;
  } else {
    for (unsigned i = 0, e = Packet.size(); i != e; ++i)
      if (hasDependence(SU, Packet[i], QII))
        return false;
  }
  return true;
}

/// Keep track of available resources.
bool VLIWResourceModel::reserveResources(SUnit *SU, bool IsTop) {
  bool startNewCycle = false;
  // Artificially reset state.
  if (!SU) {
    ResourcesModel->clearResources();
    Packet.clear();
    TotalPackets++;
    return false;
  }
  // If this SU does not fit in the packet or the packet is now full
  // start a new one.
  if (!isResourceAvailable(SU, IsTop) ||
      Packet.size() >= SchedModel->getIssueWidth()) {
    ResourcesModel->clearResources();
    Packet.clear();
    TotalPackets++;
    startNewCycle = true;
  }

  switch (SU->getInstr()->getOpcode()) {
  default:
    ResourcesModel->reserveResources(*SU->getInstr());
    break;
  case TargetOpcode::EXTRACT_SUBREG:
  case TargetOpcode::INSERT_SUBREG:
  case TargetOpcode::SUBREG_TO_REG:
  case TargetOpcode::REG_SEQUENCE:
  case TargetOpcode::IMPLICIT_DEF:
  case TargetOpcode::KILL:
  case TargetOpcode::CFI_INSTRUCTION:
  case TargetOpcode::EH_LABEL:
  case TargetOpcode::COPY:
  case TargetOpcode::INLINEASM:
    break;
  }
  Packet.push_back(SU);

#ifndef NDEBUG
  LLVM_DEBUG(dbgs() << "Packet[" << TotalPackets << "]:\n");
  for (unsigned i = 0, e = Packet.size(); i != e; ++i) {
    LLVM_DEBUG(dbgs() << "\t[" << i << "] SU(");
    LLVM_DEBUG(dbgs() << Packet[i]->NodeNum << ")\t");
    LLVM_DEBUG(Packet[i]->getInstr()->dump());
  }
#endif

  return startNewCycle;
}

/// schedule - Called back from MachineScheduler::runOnMachineFunction
/// after setting up the current scheduling region. [RegionBegin, RegionEnd)
/// only includes instructions that have DAG nodes, not scheduling boundaries.
void VLIWMachineScheduler::schedule() {
  LLVM_DEBUG(dbgs() << "********** MI Converging Scheduling VLIW "
                    << printMBBReference(*BB) << " " << BB->getName()
                    << " in_func " << BB->getParent()->getName()
                    << " at loop depth " << MLI->getLoopDepth(BB) << " \n");

  buildDAGWithRegPressure();

  Topo.InitDAGTopologicalSorting();

  // Postprocess the DAG to add platform-specific artificial dependencies.
  postprocessDAG();

  SmallVector<SUnit*, 8> TopRoots, BotRoots;
  findRootsAndBiasEdges(TopRoots, BotRoots);

  // Initialize the strategy before modifying the DAG.
  SchedImpl->initialize(this);

  LLVM_DEBUG(unsigned maxH = 0;
             for (unsigned su = 0, e = SUnits.size(); su != e;
                  ++su) if (SUnits[su].getHeight() > maxH) maxH =
                 SUnits[su].getHeight();
             dbgs() << "Max Height " << maxH << "\n";);
  LLVM_DEBUG(unsigned maxD = 0;
             for (unsigned su = 0, e = SUnits.size(); su != e;
                  ++su) if (SUnits[su].getDepth() > maxD) maxD =
                 SUnits[su].getDepth();
             dbgs() << "Max Depth " << maxD << "\n";);
  LLVM_DEBUG(dump());

  initQueues(TopRoots, BotRoots);

  bool IsTopNode = false;
  while (true) {
    LLVM_DEBUG(
        dbgs() << "** VLIWMachineScheduler::schedule picking next node\n");
    SUnit *SU = SchedImpl->pickNode(IsTopNode);
    if (!SU) break;

    if (!checkSchedLimit())
      break;

    scheduleMI(SU, IsTopNode);

    // Notify the scheduling strategy after updating the DAG.
    SchedImpl->schedNode(SU, IsTopNode);

    updateQueues(SU, IsTopNode);
  }
  assert(CurrentTop == CurrentBottom && "Nonempty unscheduled zone.");

  placeDebugValues();

  LLVM_DEBUG({
    dbgs() << "*** Final schedule for "
           << printMBBReference(*begin()->getParent()) << " ***\n";
    dumpSchedule();
    dbgs() << '\n';
  });
}

void ConvergingVLIWScheduler::initialize(ScheduleDAGMI *dag) {
  DAG = static_cast<VLIWMachineScheduler*>(dag);
  SchedModel = DAG->getSchedModel();

  Top.init(DAG, SchedModel);
  Bot.init(DAG, SchedModel);

  // Initialize the HazardRecognizers. If itineraries don't exist, are empty, or
  // are disabled, then these HazardRecs will be disabled.
  const InstrItineraryData *Itin = DAG->getSchedModel()->getInstrItineraries();
  const TargetSubtargetInfo &STI = DAG->MF.getSubtarget();
  const TargetInstrInfo *TII = STI.getInstrInfo();
  delete Top.HazardRec;
  delete Bot.HazardRec;
  Top.HazardRec = TII->CreateTargetMIHazardRecognizer(Itin, DAG);
  Bot.HazardRec = TII->CreateTargetMIHazardRecognizer(Itin, DAG);

  delete Top.ResourceModel;
  delete Bot.ResourceModel;
  Top.ResourceModel = new VLIWResourceModel(STI, DAG->getSchedModel());
  Bot.ResourceModel = new VLIWResourceModel(STI, DAG->getSchedModel());

  const std::vector<unsigned> &MaxPressure =
    DAG->getRegPressure().MaxSetPressure;
  HighPressureSets.assign(MaxPressure.size(), 0);
  for (unsigned i = 0, e = MaxPressure.size(); i < e; ++i) {
    unsigned Limit = DAG->getRegClassInfo()->getRegPressureSetLimit(i);
    HighPressureSets[i] =
      ((float) MaxPressure[i] > ((float) Limit * RPThreshold));
  }

  assert((!ForceTopDown || !ForceBottomUp) &&
         "-misched-topdown incompatible with -misched-bottomup");
}

void ConvergingVLIWScheduler::releaseTopNode(SUnit *SU) {
  if (SU->isScheduled)
    return;

  for (const SDep &PI : SU->Preds) {
    unsigned PredReadyCycle = PI.getSUnit()->TopReadyCycle;
    unsigned MinLatency = PI.getLatency();
#ifndef NDEBUG
    Top.MaxMinLatency = std::max(MinLatency, Top.MaxMinLatency);
#endif
    if (SU->TopReadyCycle < PredReadyCycle + MinLatency)
      SU->TopReadyCycle = PredReadyCycle + MinLatency;
  }
  Top.releaseNode(SU, SU->TopReadyCycle);
}

void ConvergingVLIWScheduler::releaseBottomNode(SUnit *SU) {
  if (SU->isScheduled)
    return;

  assert(SU->getInstr() && "Scheduled SUnit must have instr");

  for (SUnit::succ_iterator I = SU->Succs.begin(), E = SU->Succs.end();
       I != E; ++I) {
    unsigned SuccReadyCycle = I->getSUnit()->BotReadyCycle;
    unsigned MinLatency = I->getLatency();
#ifndef NDEBUG
    Bot.MaxMinLatency = std::max(MinLatency, Bot.MaxMinLatency);
#endif
    if (SU->BotReadyCycle < SuccReadyCycle + MinLatency)
      SU->BotReadyCycle = SuccReadyCycle + MinLatency;
  }
  Bot.releaseNode(SU, SU->BotReadyCycle);
}

/// Does this SU have a hazard within the current instruction group.
///
/// The scheduler supports two modes of hazard recognition. The first is the
/// ScheduleHazardRecognizer API. It is a fully general hazard recognizer that
/// supports highly complicated in-order reservation tables
/// (ScoreboardHazardRecognizer) and arbitrary target-specific logic.
///
/// The second is a streamlined mechanism that checks for hazards based on
/// simple counters that the scheduler itself maintains. It explicitly checks
/// for instruction dispatch limitations, including the number of micro-ops that
/// can dispatch per cycle.
///
/// TODO: Also check whether the SU must start a new group.
bool ConvergingVLIWScheduler::VLIWSchedBoundary::checkHazard(SUnit *SU) {
  if (HazardRec->isEnabled())
    return HazardRec->getHazardType(SU) != ScheduleHazardRecognizer::NoHazard;

  unsigned uops = SchedModel->getNumMicroOps(SU->getInstr());
  if (IssueCount + uops > SchedModel->getIssueWidth())
    return true;

  return false;
}

void ConvergingVLIWScheduler::VLIWSchedBoundary::releaseNode(SUnit *SU,
                                                     unsigned ReadyCycle) {
  if (ReadyCycle < MinReadyCycle)
    MinReadyCycle = ReadyCycle;

  // Check for interlocks first. For the purpose of other heuristics, an
  // instruction that cannot issue appears as if it's not in the ReadyQueue.
  if (ReadyCycle > CurrCycle || checkHazard(SU))

    Pending.push(SU);
  else
    Available.push(SU);
}

/// Move the boundary of scheduled code by one cycle.
void ConvergingVLIWScheduler::VLIWSchedBoundary::bumpCycle() {
  unsigned Width = SchedModel->getIssueWidth();
  IssueCount = (IssueCount <= Width) ? 0 : IssueCount - Width;

  assert(MinReadyCycle < std::numeric_limits<unsigned>::max() &&
         "MinReadyCycle uninitialized");
  unsigned NextCycle = std::max(CurrCycle + 1, MinReadyCycle);

  if (!HazardRec->isEnabled()) {
    // Bypass HazardRec virtual calls.
    CurrCycle = NextCycle;
  } else {
    // Bypass getHazardType calls in case of long latency.
    for (; CurrCycle != NextCycle; ++CurrCycle) {
      if (isTop())
        HazardRec->AdvanceCycle();
      else
        HazardRec->RecedeCycle();
    }
  }
  CheckPending = true;

  LLVM_DEBUG(dbgs() << "*** Next cycle " << Available.getName() << " cycle "
                    << CurrCycle << '\n');
}

/// Move the boundary of scheduled code by one SUnit.
void ConvergingVLIWScheduler::VLIWSchedBoundary::bumpNode(SUnit *SU) {
  bool startNewCycle = false;

  // Update the reservation table.
  if (HazardRec->isEnabled()) {
    if (!isTop() && SU->isCall) {
      // Calls are scheduled with their preceding instructions. For bottom-up
      // scheduling, clear the pipeline state before emitting.
      HazardRec->Reset();
    }
    HazardRec->EmitInstruction(SU);
  }

  // Update DFA model.
  startNewCycle = ResourceModel->reserveResources(SU, isTop());

  // Check the instruction group dispatch limit.
  // TODO: Check if this SU must end a dispatch group.
  IssueCount += SchedModel->getNumMicroOps(SU->getInstr());
  if (startNewCycle) {
    LLVM_DEBUG(dbgs() << "*** Max instrs at cycle " << CurrCycle << '\n');
    bumpCycle();
  }
  else
    LLVM_DEBUG(dbgs() << "*** IssueCount " << IssueCount << " at cycle "
                      << CurrCycle << '\n');
}

/// Release pending ready nodes in to the available queue. This makes them
/// visible to heuristics.
void ConvergingVLIWScheduler::VLIWSchedBoundary::releasePending() {
  // If the available queue is empty, it is safe to reset MinReadyCycle.
  if (Available.empty())
    MinReadyCycle = std::numeric_limits<unsigned>::max();

  // Check to see if any of the pending instructions are ready to issue.  If
  // so, add them to the available queue.
  for (unsigned i = 0, e = Pending.size(); i != e; ++i) {
    SUnit *SU = *(Pending.begin()+i);
    unsigned ReadyCycle = isTop() ? SU->TopReadyCycle : SU->BotReadyCycle;

    if (ReadyCycle < MinReadyCycle)
      MinReadyCycle = ReadyCycle;

    if (ReadyCycle > CurrCycle)
      continue;

    if (checkHazard(SU))
      continue;

    Available.push(SU);
    Pending.remove(Pending.begin()+i);
    --i; --e;
  }
  CheckPending = false;
}

/// Remove SU from the ready set for this boundary.
void ConvergingVLIWScheduler::VLIWSchedBoundary::removeReady(SUnit *SU) {
  if (Available.isInQueue(SU))
    Available.remove(Available.find(SU));
  else {
    assert(Pending.isInQueue(SU) && "bad ready count");
    Pending.remove(Pending.find(SU));
  }
}

/// If this queue only has one ready candidate, return it. As a side effect,
/// advance the cycle until at least one node is ready. If multiple instructions
/// are ready, return NULL.
SUnit *ConvergingVLIWScheduler::VLIWSchedBoundary::pickOnlyChoice() {
  if (CheckPending)
    releasePending();

  auto AdvanceCycle = [this]() {
    if (Available.empty())
      return true;
    if (Available.size() == 1 && Pending.size() > 0)
      return !ResourceModel->isResourceAvailable(*Available.begin(), isTop()) ||
        getWeakLeft(*Available.begin(), isTop()) != 0;
    return false;
  };
  for (unsigned i = 0; AdvanceCycle(); ++i) {
    assert(i <= (HazardRec->getMaxLookAhead() + MaxMinLatency) &&
           "permanent hazard"); (void)i;
    ResourceModel->reserveResources(nullptr, isTop());
    bumpCycle();
    releasePending();
  }
  if (Available.size() == 1)
    return *Available.begin();
  return nullptr;
}

#ifndef NDEBUG
void ConvergingVLIWScheduler::traceCandidate(const char *Label,
      const ReadyQueue &Q, SUnit *SU, int Cost, PressureChange P) {
  dbgs() << Label << " " << Q.getName() << " ";
  if (P.isValid())
    dbgs() << DAG->TRI->getRegPressureSetName(P.getPSet()) << ":"
           << P.getUnitInc() << " ";
  else
    dbgs() << "     ";
  dbgs() << "cost(" << Cost << ")\t";
  DAG->dumpNode(*SU);
}

// Very detailed queue dump, to be used with higher verbosity levels.
void ConvergingVLIWScheduler::readyQueueVerboseDump(
      const RegPressureTracker &RPTracker, SchedCandidate &Candidate,
      ReadyQueue &Q) {
  RegPressureTracker &TempTracker = const_cast<RegPressureTracker &>(RPTracker);

  dbgs() << ">>> " << Q.getName() << "\n";
  for (ReadyQueue::iterator I = Q.begin(), E = Q.end(); I != E; ++I) {
    RegPressureDelta RPDelta;
    TempTracker.getMaxPressureDelta((*I)->getInstr(), RPDelta,
                                    DAG->getRegionCriticalPSets(),
                                    DAG->getRegPressure().MaxSetPressure);
    std::stringstream dbgstr;
    dbgstr << "SU(" << std::setw(3) << (*I)->NodeNum << ")";
    dbgs() << dbgstr.str();
    SchedulingCost(Q, *I, Candidate, RPDelta, true);
    dbgs() << "\t";
    (*I)->getInstr()->dump();
  }
  dbgs() << "\n";
}
#endif

/// isSingleUnscheduledPred - If SU2 is the only unscheduled predecessor
/// of SU, return true (we may have duplicates)
static inline bool isSingleUnscheduledPred(SUnit *SU, SUnit *SU2) {
  if (SU->NumPredsLeft == 0)
    return false;

  for (auto &Pred : SU->Preds) {
    // We found an available, but not scheduled, predecessor.
    if (!Pred.getSUnit()->isScheduled && (Pred.getSUnit() != SU2))
      return false;
  }

  return true;
}

/// isSingleUnscheduledSucc - If SU2 is the only unscheduled successor
/// of SU, return true (we may have duplicates)
static inline bool isSingleUnscheduledSucc(SUnit *SU, SUnit *SU2) {
  if (SU->NumSuccsLeft == 0)
    return false;

  for (auto &Succ : SU->Succs) {
    // We found an available, but not scheduled, successor.
    if (!Succ.getSUnit()->isScheduled && (Succ.getSUnit() != SU2))
      return false;
  }
  return true;
}

/// Check if the instruction changes the register pressure of a register in the
/// high pressure set. The function returns a negative value if the pressure
/// decreases and a positive value is the pressure increases. If the instruction
/// doesn't use a high pressure register or doesn't change the register
/// pressure, then return 0.
int ConvergingVLIWScheduler::pressureChange(const SUnit *SU, bool isBotUp) {
  PressureDiff &PD = DAG->getPressureDiff(SU);
  for (auto &P : PD) {
    if (!P.isValid())
      continue;
    // The pressure differences are computed bottom-up, so the comparision for
    // an increase is positive in the bottom direction, but negative in the
    //  top-down direction.
    if (HighPressureSets[P.getPSet()])
      return (isBotUp ? P.getUnitInc() : -P.getUnitInc());
  }
  return 0;
}

// Constants used to denote relative importance of
// heuristic components for cost computation.
static const unsigned PriorityOne = 200;
static const unsigned PriorityTwo = 50;
static const unsigned PriorityThree = 75;
static const unsigned ScaleTwo = 10;

/// Single point to compute overall scheduling cost.
/// TODO: More heuristics will be used soon.
int ConvergingVLIWScheduler::SchedulingCost(ReadyQueue &Q, SUnit *SU,
                                            SchedCandidate &Candidate,
                                            RegPressureDelta &Delta,
                                            bool verbose) {
  // Initial trivial priority.
  int ResCount = 1;

  // Do not waste time on a node that is already scheduled.
  if (!SU || SU->isScheduled)
    return ResCount;

  LLVM_DEBUG(if (verbose) dbgs()
             << ((Q.getID() == TopQID) ? "(top|" : "(bot|"));
  // Forced priority is high.
  if (SU->isScheduleHigh) {
    ResCount += PriorityOne;
    LLVM_DEBUG(dbgs() << "H|");
  }

  unsigned IsAvailableAmt = 0;
  // Critical path first.
  if (Q.getID() == TopQID) {
    if (Top.isLatencyBound(SU)) {
      LLVM_DEBUG(if (verbose) dbgs() << "LB|");
      ResCount += (SU->getHeight() * ScaleTwo);
    }

    LLVM_DEBUG(if (verbose) {
      std::stringstream dbgstr;
      dbgstr << "h" << std::setw(3) << SU->getHeight() << "|";
      dbgs() << dbgstr.str();
    });

    // If resources are available for it, multiply the
    // chance of scheduling.
    if (Top.ResourceModel->isResourceAvailable(SU, true)) {
      IsAvailableAmt = (PriorityTwo + PriorityThree);
      ResCount += IsAvailableAmt;
      LLVM_DEBUG(if (verbose) dbgs() << "A|");
    } else
      LLVM_DEBUG(if (verbose) dbgs() << " |");
  } else {
    if (Bot.isLatencyBound(SU)) {
      LLVM_DEBUG(if (verbose) dbgs() << "LB|");
      ResCount += (SU->getDepth() * ScaleTwo);
    }

    LLVM_DEBUG(if (verbose) {
      std::stringstream dbgstr;
      dbgstr << "d" << std::setw(3) << SU->getDepth() << "|";
      dbgs() << dbgstr.str();
    });

    // If resources are available for it, multiply the
    // chance of scheduling.
    if (Bot.ResourceModel->isResourceAvailable(SU, false)) {
      IsAvailableAmt = (PriorityTwo + PriorityThree);
      ResCount += IsAvailableAmt;
      LLVM_DEBUG(if (verbose) dbgs() << "A|");
    } else
      LLVM_DEBUG(if (verbose) dbgs() << " |");
  }

  unsigned NumNodesBlocking = 0;
  if (Q.getID() == TopQID) {
    // How many SUs does it block from scheduling?
    // Look at all of the successors of this node.
    // Count the number of nodes that
    // this node is the sole unscheduled node for.
    if (Top.isLatencyBound(SU))
      for (const SDep &SI : SU->Succs)
        if (isSingleUnscheduledPred(SI.getSUnit(), SU))
          ++NumNodesBlocking;
  } else {
    // How many unscheduled predecessors block this node?
    if (Bot.isLatencyBound(SU))
      for (const SDep &PI : SU->Preds)
        if (isSingleUnscheduledSucc(PI.getSUnit(), SU))
          ++NumNodesBlocking;
  }
  ResCount += (NumNodesBlocking * ScaleTwo);

  LLVM_DEBUG(if (verbose) {
    std::stringstream dbgstr;
    dbgstr << "blk " << std::setw(2) << NumNodesBlocking << ")|";
    dbgs() << dbgstr.str();
  });

  // Factor in reg pressure as a heuristic.
  if (!IgnoreBBRegPressure) {
    // Decrease priority by the amount that register pressure exceeds the limit.
    ResCount -= (Delta.Excess.getUnitInc()*PriorityOne);
    // Decrease priority if register pressure exceeds the limit.
    ResCount -= (Delta.CriticalMax.getUnitInc()*PriorityOne);
    // Decrease priority slightly if register pressure would increase over the
    // current maximum.
    ResCount -= (Delta.CurrentMax.getUnitInc()*PriorityTwo);
    // If there are register pressure issues, then we remove the value added for
    // the instruction being available. The rationale is that we really don't
    // want to schedule an instruction that causes a spill.
    if (IsAvailableAmt && pressureChange(SU, Q.getID() != TopQID) > 0 &&
        (Delta.Excess.getUnitInc() || Delta.CriticalMax.getUnitInc() ||
         Delta.CurrentMax.getUnitInc()))
      ResCount -= IsAvailableAmt;
    LLVM_DEBUG(if (verbose) {
      dbgs() << "RP " << Delta.Excess.getUnitInc() << "/"
             << Delta.CriticalMax.getUnitInc() << "/"
             << Delta.CurrentMax.getUnitInc() << ")|";
    });
  }

  // Give a little extra priority to a .cur instruction if there is a resource
  // available for it.
  auto &QST = DAG->MF.getSubtarget<HexagonSubtarget>();
  auto &QII = *QST.getInstrInfo();
  if (SU->isInstr() && QII.mayBeCurLoad(*SU->getInstr())) {
    if (Q.getID() == TopQID &&
        Top.ResourceModel->isResourceAvailable(SU, true)) {
      ResCount += PriorityTwo;
      LLVM_DEBUG(if (verbose) dbgs() << "C|");
    } else if (Q.getID() == BotQID &&
               Bot.ResourceModel->isResourceAvailable(SU, false)) {
      ResCount += PriorityTwo;
      LLVM_DEBUG(if (verbose) dbgs() << "C|");
    }
  }

  // Give preference to a zero latency instruction if the dependent
  // instruction is in the current packet.
  if (Q.getID() == TopQID && getWeakLeft(SU, true) == 0) {
    for (const SDep &PI : SU->Preds) {
      if (!PI.getSUnit()->getInstr()->isPseudo() && PI.isAssignedRegDep() &&
          PI.getLatency() == 0 &&
          Top.ResourceModel->isInPacket(PI.getSUnit())) {
        ResCount += PriorityThree;
        LLVM_DEBUG(if (verbose) dbgs() << "Z|");
      }
    }
  } else if (Q.getID() == BotQID && getWeakLeft(SU, false) == 0) {
    for (const SDep &SI : SU->Succs) {
      if (!SI.getSUnit()->getInstr()->isPseudo() && SI.isAssignedRegDep() &&
          SI.getLatency() == 0 &&
          Bot.ResourceModel->isInPacket(SI.getSUnit())) {
        ResCount += PriorityThree;
        LLVM_DEBUG(if (verbose) dbgs() << "Z|");
      }
    }
  }

  // If the instruction has a non-zero latency dependence with an instruction in
  // the current packet, then it should not be scheduled yet. The case occurs
  // when the dependent instruction is scheduled in a new packet, so the
  // scheduler updates the current cycle and pending instructions become
  // available.
  if (CheckEarlyAvail) {
    if (Q.getID() == TopQID) {
      for (const auto &PI : SU->Preds) {
        if (PI.getLatency() > 0 &&
            Top.ResourceModel->isInPacket(PI.getSUnit())) {
          ResCount -= PriorityOne;
          LLVM_DEBUG(if (verbose) dbgs() << "D|");
        }
      }
    } else {
      for (const auto &SI : SU->Succs) {
        if (SI.getLatency() > 0 &&
            Bot.ResourceModel->isInPacket(SI.getSUnit())) {
          ResCount -= PriorityOne;
          LLVM_DEBUG(if (verbose) dbgs() << "D|");
        }
      }
    }
  }

  LLVM_DEBUG(if (verbose) {
    std::stringstream dbgstr;
    dbgstr << "Total " << std::setw(4) << ResCount << ")";
    dbgs() << dbgstr.str();
  });

  return ResCount;
}

/// Pick the best candidate from the top queue.
///
/// TODO: getMaxPressureDelta results can be mostly cached for each SUnit during
/// DAG building. To adjust for the current scheduling location we need to
/// maintain the number of vreg uses remaining to be top-scheduled.
ConvergingVLIWScheduler::CandResult ConvergingVLIWScheduler::
pickNodeFromQueue(VLIWSchedBoundary &Zone, const RegPressureTracker &RPTracker,
                  SchedCandidate &Candidate) {
  ReadyQueue &Q = Zone.Available;
  LLVM_DEBUG(if (SchedDebugVerboseLevel > 1)
                 readyQueueVerboseDump(RPTracker, Candidate, Q);
             else Q.dump(););

  // getMaxPressureDelta temporarily modifies the tracker.
  RegPressureTracker &TempTracker = const_cast<RegPressureTracker&>(RPTracker);

  // BestSU remains NULL if no top candidates beat the best existing candidate.
  CandResult FoundCandidate = NoCand;
  for (ReadyQueue::iterator I = Q.begin(), E = Q.end(); I != E; ++I) {
    RegPressureDelta RPDelta;
    TempTracker.getMaxPressureDelta((*I)->getInstr(), RPDelta,
                                    DAG->getRegionCriticalPSets(),
                                    DAG->getRegPressure().MaxSetPressure);

    int CurrentCost = SchedulingCost(Q, *I, Candidate, RPDelta, false);

    // Initialize the candidate if needed.
    if (!Candidate.SU) {
      LLVM_DEBUG(traceCandidate("DCAND", Q, *I, CurrentCost));
      Candidate.SU = *I;
      Candidate.RPDelta = RPDelta;
      Candidate.SCost = CurrentCost;
      FoundCandidate = NodeOrder;
      continue;
    }

    // Choose node order for negative cost candidates. There is no good
    // candidate in this case.
    if (CurrentCost < 0 && Candidate.SCost < 0) {
      if ((Q.getID() == TopQID && (*I)->NodeNum < Candidate.SU->NodeNum)
          || (Q.getID() == BotQID && (*I)->NodeNum > Candidate.SU->NodeNum)) {
        LLVM_DEBUG(traceCandidate("NCAND", Q, *I, CurrentCost));
        Candidate.SU = *I;
        Candidate.RPDelta = RPDelta;
        Candidate.SCost = CurrentCost;
        FoundCandidate = NodeOrder;
      }
      continue;
    }

    // Best cost.
    if (CurrentCost > Candidate.SCost) {
      LLVM_DEBUG(traceCandidate("CCAND", Q, *I, CurrentCost));
      Candidate.SU = *I;
      Candidate.RPDelta = RPDelta;
      Candidate.SCost = CurrentCost;
      FoundCandidate = BestCost;
      continue;
    }

    // Choose an instruction that does not depend on an artificial edge.
    unsigned CurrWeak = getWeakLeft(*I, (Q.getID() == TopQID));
    unsigned CandWeak = getWeakLeft(Candidate.SU, (Q.getID() == TopQID));
    if (CurrWeak != CandWeak) {
      if (CurrWeak < CandWeak) {
        LLVM_DEBUG(traceCandidate("WCAND", Q, *I, CurrentCost));
        Candidate.SU = *I;
        Candidate.RPDelta = RPDelta;
        Candidate.SCost = CurrentCost;
        FoundCandidate = Weak;
      }
      continue;
    }

    if (CurrentCost == Candidate.SCost && Zone.isLatencyBound(*I)) {
      unsigned CurrSize, CandSize;
      if (Q.getID() == TopQID) {
        CurrSize = (*I)->Succs.size();
        CandSize = Candidate.SU->Succs.size();
      } else {
        CurrSize = (*I)->Preds.size();
        CandSize = Candidate.SU->Preds.size();
      }
      if (CurrSize > CandSize) {
        LLVM_DEBUG(traceCandidate("SPCAND", Q, *I, CurrentCost));
        Candidate.SU = *I;
        Candidate.RPDelta = RPDelta;
        Candidate.SCost = CurrentCost;
        FoundCandidate = BestCost;
      }
      // Keep the old candidate if it's a better candidate. That is, don't use
      // the subsequent tie breaker.
      if (CurrSize != CandSize)
        continue;
    }

    // Tie breaker.
    // To avoid scheduling indeterminism, we need a tie breaker
    // for the case when cost is identical for two nodes.
    if (UseNewerCandidate && CurrentCost == Candidate.SCost) {
      if ((Q.getID() == TopQID && (*I)->NodeNum < Candidate.SU->NodeNum)
          || (Q.getID() == BotQID && (*I)->NodeNum > Candidate.SU->NodeNum)) {
        LLVM_DEBUG(traceCandidate("TCAND", Q, *I, CurrentCost));
        Candidate.SU = *I;
        Candidate.RPDelta = RPDelta;
        Candidate.SCost = CurrentCost;
        FoundCandidate = NodeOrder;
        continue;
      }
    }

    // Fall through to original instruction order.
    // Only consider node order if Candidate was chosen from this Q.
    if (FoundCandidate == NoCand)
      continue;
  }
  return FoundCandidate;
}

/// Pick the best candidate node from either the top or bottom queue.
SUnit *ConvergingVLIWScheduler::pickNodeBidrectional(bool &IsTopNode) {
  // Schedule as far as possible in the direction of no choice. This is most
  // efficient, but also provides the best heuristics for CriticalPSets.
  if (SUnit *SU = Bot.pickOnlyChoice()) {
    LLVM_DEBUG(dbgs() << "Picked only Bottom\n");
    IsTopNode = false;
    return SU;
  }
  if (SUnit *SU = Top.pickOnlyChoice()) {
    LLVM_DEBUG(dbgs() << "Picked only Top\n");
    IsTopNode = true;
    return SU;
  }
  SchedCandidate BotCand;
  // Prefer bottom scheduling when heuristics are silent.
  CandResult BotResult = pickNodeFromQueue(Bot,
                                           DAG->getBotRPTracker(), BotCand);
  assert(BotResult != NoCand && "failed to find the first candidate");

  // If either Q has a single candidate that provides the least increase in
  // Excess pressure, we can immediately schedule from that Q.
  //
  // RegionCriticalPSets summarizes the pressure within the scheduled region and
  // affects picking from either Q. If scheduling in one direction must
  // increase pressure for one of the excess PSets, then schedule in that
  // direction first to provide more freedom in the other direction.
  if (BotResult == SingleExcess || BotResult == SingleCritical) {
    LLVM_DEBUG(dbgs() << "Prefered Bottom Node\n");
    IsTopNode = false;
    return BotCand.SU;
  }
  // Check if the top Q has a better candidate.
  SchedCandidate TopCand;
  CandResult TopResult = pickNodeFromQueue(Top,
                                           DAG->getTopRPTracker(), TopCand);
  assert(TopResult != NoCand && "failed to find the first candidate");

  if (TopResult == SingleExcess || TopResult == SingleCritical) {
    LLVM_DEBUG(dbgs() << "Prefered Top Node\n");
    IsTopNode = true;
    return TopCand.SU;
  }
  // If either Q has a single candidate that minimizes pressure above the
  // original region's pressure pick it.
  if (BotResult == SingleMax) {
    LLVM_DEBUG(dbgs() << "Prefered Bottom Node SingleMax\n");
    IsTopNode = false;
    return BotCand.SU;
  }
  if (TopResult == SingleMax) {
    LLVM_DEBUG(dbgs() << "Prefered Top Node SingleMax\n");
    IsTopNode = true;
    return TopCand.SU;
  }
  if (TopCand.SCost > BotCand.SCost) {
    LLVM_DEBUG(dbgs() << "Prefered Top Node Cost\n");
    IsTopNode = true;
    return TopCand.SU;
  }
  // Otherwise prefer the bottom candidate in node order.
  LLVM_DEBUG(dbgs() << "Prefered Bottom in Node order\n");
  IsTopNode = false;
  return BotCand.SU;
}

/// Pick the best node to balance the schedule. Implements MachineSchedStrategy.
SUnit *ConvergingVLIWScheduler::pickNode(bool &IsTopNode) {
  if (DAG->top() == DAG->bottom()) {
    assert(Top.Available.empty() && Top.Pending.empty() &&
           Bot.Available.empty() && Bot.Pending.empty() && "ReadyQ garbage");
    return nullptr;
  }
  SUnit *SU;
  if (ForceTopDown) {
    SU = Top.pickOnlyChoice();
    if (!SU) {
      SchedCandidate TopCand;
      CandResult TopResult =
        pickNodeFromQueue(Top, DAG->getTopRPTracker(), TopCand);
      assert(TopResult != NoCand && "failed to find the first candidate");
      (void)TopResult;
      SU = TopCand.SU;
    }
    IsTopNode = true;
  } else if (ForceBottomUp) {
    SU = Bot.pickOnlyChoice();
    if (!SU) {
      SchedCandidate BotCand;
      CandResult BotResult =
        pickNodeFromQueue(Bot, DAG->getBotRPTracker(), BotCand);
      assert(BotResult != NoCand && "failed to find the first candidate");
      (void)BotResult;
      SU = BotCand.SU;
    }
    IsTopNode = false;
  } else {
    SU = pickNodeBidrectional(IsTopNode);
  }
  if (SU->isTopReady())
    Top.removeReady(SU);
  if (SU->isBottomReady())
    Bot.removeReady(SU);

  LLVM_DEBUG(dbgs() << "*** " << (IsTopNode ? "Top" : "Bottom")
                    << " Scheduling instruction in cycle "
                    << (IsTopNode ? Top.CurrCycle : Bot.CurrCycle) << " ("
                    << reportPackets() << ")\n";
             DAG->dumpNode(*SU));
  return SU;
}

/// Update the scheduler's state after scheduling a node. This is the same node
/// that was just returned by pickNode(). However, VLIWMachineScheduler needs
/// to update it's state based on the current cycle before MachineSchedStrategy
/// does.
void ConvergingVLIWScheduler::schedNode(SUnit *SU, bool IsTopNode) {
  if (IsTopNode) {
    Top.bumpNode(SU);
    SU->TopReadyCycle = Top.CurrCycle;
  } else {
    Bot.bumpNode(SU);
    SU->BotReadyCycle = Bot.CurrCycle;
  }
}
