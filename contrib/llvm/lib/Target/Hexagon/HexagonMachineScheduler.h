//===- HexagonMachineScheduler.h - Custom Hexagon MI scheduler --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Custom Hexagon MI scheduler.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_HEXAGONMACHINESCHEDULER_H
#define LLVM_LIB_TARGET_HEXAGON_HEXAGONMACHINESCHEDULER_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/DFAPacketizer.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/RegisterPressure.h"
#include "llvm/CodeGen/ScheduleHazardRecognizer.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSchedule.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include <algorithm>
#include <cassert>
#include <limits>
#include <memory>
#include <vector>

namespace llvm {

class SUnit;

class VLIWResourceModel {
  /// ResourcesModel - Represents VLIW state.
  /// Not limited to VLIW targets per se, but assumes
  /// definition of DFA by a target.
  DFAPacketizer *ResourcesModel;

  const TargetSchedModel *SchedModel;

  /// Local packet/bundle model. Purely
  /// internal to the MI schedulre at the time.
  std::vector<SUnit *> Packet;

  /// Total packets created.
  unsigned TotalPackets = 0;

public:
  VLIWResourceModel(const TargetSubtargetInfo &STI, const TargetSchedModel *SM)
      : SchedModel(SM) {
    ResourcesModel = STI.getInstrInfo()->CreateTargetScheduleState(STI);

    // This hard requirement could be relaxed,
    // but for now do not let it proceed.
    assert(ResourcesModel && "Unimplemented CreateTargetScheduleState.");

    Packet.resize(SchedModel->getIssueWidth());
    Packet.clear();
    ResourcesModel->clearResources();
  }

  ~VLIWResourceModel() {
    delete ResourcesModel;
  }

  void resetPacketState() {
    Packet.clear();
  }

  void resetDFA() {
    ResourcesModel->clearResources();
  }

  void reset() {
    Packet.clear();
    ResourcesModel->clearResources();
  }

  bool isResourceAvailable(SUnit *SU, bool IsTop);
  bool reserveResources(SUnit *SU, bool IsTop);
  unsigned getTotalPackets() const { return TotalPackets; }
  bool isInPacket(SUnit *SU) const { return is_contained(Packet, SU); }
};

/// Extend the standard ScheduleDAGMI to provide more context and override the
/// top-level schedule() driver.
class VLIWMachineScheduler : public ScheduleDAGMILive {
public:
  VLIWMachineScheduler(MachineSchedContext *C,
                       std::unique_ptr<MachineSchedStrategy> S)
      : ScheduleDAGMILive(C, std::move(S)) {}

  /// Schedule - This is called back from ScheduleDAGInstrs::Run() when it's
  /// time to do some work.
  void schedule() override;

  RegisterClassInfo *getRegClassInfo() { return RegClassInfo; }
  int getBBSize() { return BB->size(); }
};

//===----------------------------------------------------------------------===//
// ConvergingVLIWScheduler - Implementation of the standard
// MachineSchedStrategy.
//===----------------------------------------------------------------------===//

/// ConvergingVLIWScheduler shrinks the unscheduled zone using heuristics
/// to balance the schedule.
class ConvergingVLIWScheduler : public MachineSchedStrategy {
  /// Store the state used by ConvergingVLIWScheduler heuristics, required
  ///  for the lifetime of one invocation of pickNode().
  struct SchedCandidate {
    // The best SUnit candidate.
    SUnit *SU = nullptr;

    // Register pressure values for the best candidate.
    RegPressureDelta RPDelta;

    // Best scheduling cost.
    int SCost = 0;

    SchedCandidate() = default;
  };
  /// Represent the type of SchedCandidate found within a single queue.
  enum CandResult {
    NoCand, NodeOrder, SingleExcess, SingleCritical, SingleMax, MultiPressure,
    BestCost, Weak};

  /// Each Scheduling boundary is associated with ready queues. It tracks the
  /// current cycle in whichever direction at has moved, and maintains the state
  /// of "hazards" and other interlocks at the current cycle.
  struct VLIWSchedBoundary {
    VLIWMachineScheduler *DAG = nullptr;
    const TargetSchedModel *SchedModel = nullptr;

    ReadyQueue Available;
    ReadyQueue Pending;
    bool CheckPending = false;

    ScheduleHazardRecognizer *HazardRec = nullptr;
    VLIWResourceModel *ResourceModel = nullptr;

    unsigned CurrCycle = 0;
    unsigned IssueCount = 0;
    unsigned CriticalPathLength = 0;

    /// MinReadyCycle - Cycle of the soonest available instruction.
    unsigned MinReadyCycle = std::numeric_limits<unsigned>::max();

    // Remember the greatest min operand latency.
    unsigned MaxMinLatency = 0;

    /// Pending queues extend the ready queues with the same ID and the
    /// PendingFlag set.
    VLIWSchedBoundary(unsigned ID, const Twine &Name)
        : Available(ID, Name+".A"),
          Pending(ID << ConvergingVLIWScheduler::LogMaxQID, Name+".P") {}

    ~VLIWSchedBoundary() {
      delete ResourceModel;
      delete HazardRec;
    }

    void init(VLIWMachineScheduler *dag, const TargetSchedModel *smodel) {
      DAG = dag;
      SchedModel = smodel;
      CurrCycle = 0;
      IssueCount = 0;
      // Initialize the critical path length limit, which used by the scheduling
      // cost model to determine the value for scheduling an instruction. We use
      // a slightly different heuristic for small and large functions. For small
      // functions, it's important to use the height/depth of the instruction.
      // For large functions, prioritizing by height or depth increases spills.
      CriticalPathLength = DAG->getBBSize() / SchedModel->getIssueWidth();
      if (DAG->getBBSize() < 50)
        // We divide by two as a cheap and simple heuristic to reduce the
        // critcal path length, which increases the priority of using the graph
        // height/depth in the scheduler's cost computation.
        CriticalPathLength >>= 1;
      else {
        // For large basic blocks, we prefer a larger critical path length to
        // decrease the priority of using the graph height/depth.
        unsigned MaxPath = 0;
        for (auto &SU : DAG->SUnits)
          MaxPath = std::max(MaxPath, isTop() ? SU.getHeight() : SU.getDepth());
        CriticalPathLength = std::max(CriticalPathLength, MaxPath) + 1;
      }
    }

    bool isTop() const {
      return Available.getID() == ConvergingVLIWScheduler::TopQID;
    }

    bool checkHazard(SUnit *SU);

    void releaseNode(SUnit *SU, unsigned ReadyCycle);

    void bumpCycle();

    void bumpNode(SUnit *SU);

    void releasePending();

    void removeReady(SUnit *SU);

    SUnit *pickOnlyChoice();

    bool isLatencyBound(SUnit *SU) {
      if (CurrCycle >= CriticalPathLength)
        return true;
      unsigned PathLength = isTop() ? SU->getHeight() : SU->getDepth();
      return CriticalPathLength - CurrCycle <= PathLength;
    }
  };

  VLIWMachineScheduler *DAG = nullptr;
  const TargetSchedModel *SchedModel = nullptr;

  // State of the top and bottom scheduled instruction boundaries.
  VLIWSchedBoundary Top;
  VLIWSchedBoundary Bot;

  /// List of pressure sets that have a high pressure level in the region.
  std::vector<bool> HighPressureSets;

public:
  /// SUnit::NodeQueueId: 0 (none), 1 (top), 2 (bot), 3 (both)
  enum {
    TopQID = 1,
    BotQID = 2,
    LogMaxQID = 2
  };

  ConvergingVLIWScheduler() : Top(TopQID, "TopQ"), Bot(BotQID, "BotQ") {}

  void initialize(ScheduleDAGMI *dag) override;

  SUnit *pickNode(bool &IsTopNode) override;

  void schedNode(SUnit *SU, bool IsTopNode) override;

  void releaseTopNode(SUnit *SU) override;

  void releaseBottomNode(SUnit *SU) override;

  unsigned reportPackets() {
    return Top.ResourceModel->getTotalPackets() +
           Bot.ResourceModel->getTotalPackets();
  }

protected:
  SUnit *pickNodeBidrectional(bool &IsTopNode);

  int pressureChange(const SUnit *SU, bool isBotUp);

  int SchedulingCost(ReadyQueue &Q,
                     SUnit *SU, SchedCandidate &Candidate,
                     RegPressureDelta &Delta, bool verbose);

  CandResult pickNodeFromQueue(VLIWSchedBoundary &Zone,
                               const RegPressureTracker &RPTracker,
                               SchedCandidate &Candidate);
#ifndef NDEBUG
  void traceCandidate(const char *Label, const ReadyQueue &Q, SUnit *SU,
                      int Cost, PressureChange P = PressureChange());

  void readyQueueVerboseDump(const RegPressureTracker &RPTracker,
                             SchedCandidate &Candidate, ReadyQueue &Q);
#endif
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_HEXAGON_HEXAGONMACHINESCHEDULER_H
