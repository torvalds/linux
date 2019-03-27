//===- MachineScheduler.h - MachineInstr Scheduling Pass --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides an interface for customizing the standard MachineScheduler
// pass. Note that the entire pass may be replaced as follows:
//
// <Target>TargetMachine::createPassConfig(PassManagerBase &PM) {
//   PM.substitutePass(&MachineSchedulerID, &CustomSchedulerPassID);
//   ...}
//
// The MachineScheduler pass is only responsible for choosing the regions to be
// scheduled. Targets can override the DAG builder and scheduler without
// replacing the pass as follows:
//
// ScheduleDAGInstrs *<Target>PassConfig::
// createMachineScheduler(MachineSchedContext *C) {
//   return new CustomMachineScheduler(C);
// }
//
// The default scheduler, ScheduleDAGMILive, builds the DAG and drives list
// scheduling while updating the instruction stream, register pressure, and live
// intervals. Most targets don't need to override the DAG builder and list
// scheduler, but subtargets that require custom scheduling heuristics may
// plugin an alternate MachineSchedStrategy. The strategy is responsible for
// selecting the highest priority node from the list:
//
// ScheduleDAGInstrs *<Target>PassConfig::
// createMachineScheduler(MachineSchedContext *C) {
//   return new ScheduleDAGMILive(C, CustomStrategy(C));
// }
//
// The DAG builder can also be customized in a sense by adding DAG mutations
// that will run after DAG building and before list scheduling. DAG mutations
// can adjust dependencies based on target-specific knowledge or add weak edges
// to aid heuristics:
//
// ScheduleDAGInstrs *<Target>PassConfig::
// createMachineScheduler(MachineSchedContext *C) {
//   ScheduleDAGMI *DAG = createGenericSchedLive(C);
//   DAG->addMutation(new CustomDAGMutation(...));
//   return DAG;
// }
//
// A target that supports alternative schedulers can use the
// MachineSchedRegistry to allow command line selection. This can be done by
// implementing the following boilerplate:
//
// static ScheduleDAGInstrs *createCustomMachineSched(MachineSchedContext *C) {
//  return new CustomMachineScheduler(C);
// }
// static MachineSchedRegistry
// SchedCustomRegistry("custom", "Run my target's custom scheduler",
//                     createCustomMachineSched);
//
//
// Finally, subtargets that don't need to implement custom heuristics but would
// like to configure the GenericScheduler's policy for a given scheduler region,
// including scheduling direction and register pressure tracking policy, can do
// this:
//
// void <SubTarget>Subtarget::
// overrideSchedPolicy(MachineSchedPolicy &Policy,
//                     unsigned NumRegionInstrs) const {
//   Policy.<Flag> = true;
// }
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINESCHEDULER_H
#define LLVM_CODEGEN_MACHINESCHEDULER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachinePassRegistry.h"
#include "llvm/CodeGen/RegisterPressure.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/CodeGen/ScheduleDAGInstrs.h"
#include "llvm/CodeGen/ScheduleDAGMutation.h"
#include "llvm/CodeGen/TargetSchedule.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <cassert>
#include <memory>
#include <string>
#include <vector>

namespace llvm {

extern cl::opt<bool> ForceTopDown;
extern cl::opt<bool> ForceBottomUp;

class LiveIntervals;
class MachineDominatorTree;
class MachineFunction;
class MachineInstr;
class MachineLoopInfo;
class RegisterClassInfo;
class SchedDFSResult;
class ScheduleHazardRecognizer;
class TargetInstrInfo;
class TargetPassConfig;
class TargetRegisterInfo;

/// MachineSchedContext provides enough context from the MachineScheduler pass
/// for the target to instantiate a scheduler.
struct MachineSchedContext {
  MachineFunction *MF = nullptr;
  const MachineLoopInfo *MLI = nullptr;
  const MachineDominatorTree *MDT = nullptr;
  const TargetPassConfig *PassConfig = nullptr;
  AliasAnalysis *AA = nullptr;
  LiveIntervals *LIS = nullptr;

  RegisterClassInfo *RegClassInfo;

  MachineSchedContext();
  virtual ~MachineSchedContext();
};

/// MachineSchedRegistry provides a selection of available machine instruction
/// schedulers.
class MachineSchedRegistry
    : public MachinePassRegistryNode<
          ScheduleDAGInstrs *(*)(MachineSchedContext *)> {
public:
  using ScheduleDAGCtor = ScheduleDAGInstrs *(*)(MachineSchedContext *);

  // RegisterPassParser requires a (misnamed) FunctionPassCtor type.
  using FunctionPassCtor = ScheduleDAGCtor;

  static MachinePassRegistry<ScheduleDAGCtor> Registry;

  MachineSchedRegistry(const char *N, const char *D, ScheduleDAGCtor C)
      : MachinePassRegistryNode(N, D, C) {
    Registry.Add(this);
  }

  ~MachineSchedRegistry() { Registry.Remove(this); }

  // Accessors.
  //
  MachineSchedRegistry *getNext() const {
    return (MachineSchedRegistry *)MachinePassRegistryNode::getNext();
  }

  static MachineSchedRegistry *getList() {
    return (MachineSchedRegistry *)Registry.getList();
  }

  static void setListener(MachinePassRegistryListener<FunctionPassCtor> *L) {
    Registry.setListener(L);
  }
};

class ScheduleDAGMI;

/// Define a generic scheduling policy for targets that don't provide their own
/// MachineSchedStrategy. This can be overriden for each scheduling region
/// before building the DAG.
struct MachineSchedPolicy {
  // Allow the scheduler to disable register pressure tracking.
  bool ShouldTrackPressure = false;
  /// Track LaneMasks to allow reordering of independent subregister writes
  /// of the same vreg. \sa MachineSchedStrategy::shouldTrackLaneMasks()
  bool ShouldTrackLaneMasks = false;

  // Allow the scheduler to force top-down or bottom-up scheduling. If neither
  // is true, the scheduler runs in both directions and converges.
  bool OnlyTopDown = false;
  bool OnlyBottomUp = false;

  // Disable heuristic that tries to fetch nodes from long dependency chains
  // first.
  bool DisableLatencyHeuristic = false;

  MachineSchedPolicy() = default;
};

/// MachineSchedStrategy - Interface to the scheduling algorithm used by
/// ScheduleDAGMI.
///
/// Initialization sequence:
///   initPolicy -> shouldTrackPressure -> initialize(DAG) -> registerRoots
class MachineSchedStrategy {
  virtual void anchor();

public:
  virtual ~MachineSchedStrategy() = default;

  /// Optionally override the per-region scheduling policy.
  virtual void initPolicy(MachineBasicBlock::iterator Begin,
                          MachineBasicBlock::iterator End,
                          unsigned NumRegionInstrs) {}

  virtual void dumpPolicy() const {}

  /// Check if pressure tracking is needed before building the DAG and
  /// initializing this strategy. Called after initPolicy.
  virtual bool shouldTrackPressure() const { return true; }

  /// Returns true if lanemasks should be tracked. LaneMask tracking is
  /// necessary to reorder independent subregister defs for the same vreg.
  /// This has to be enabled in combination with shouldTrackPressure().
  virtual bool shouldTrackLaneMasks() const { return false; }

  // If this method returns true, handling of the scheduling regions
  // themselves (in case of a scheduling boundary in MBB) will be done
  // beginning with the topmost region of MBB.
  virtual bool doMBBSchedRegionsTopDown() const { return false; }

  /// Initialize the strategy after building the DAG for a new region.
  virtual void initialize(ScheduleDAGMI *DAG) = 0;

  /// Tell the strategy that MBB is about to be processed.
  virtual void enterMBB(MachineBasicBlock *MBB) {};

  /// Tell the strategy that current MBB is done.
  virtual void leaveMBB() {};

  /// Notify this strategy that all roots have been released (including those
  /// that depend on EntrySU or ExitSU).
  virtual void registerRoots() {}

  /// Pick the next node to schedule, or return NULL. Set IsTopNode to true to
  /// schedule the node at the top of the unscheduled region. Otherwise it will
  /// be scheduled at the bottom.
  virtual SUnit *pickNode(bool &IsTopNode) = 0;

  /// Scheduler callback to notify that a new subtree is scheduled.
  virtual void scheduleTree(unsigned SubtreeID) {}

  /// Notify MachineSchedStrategy that ScheduleDAGMI has scheduled an
  /// instruction and updated scheduled/remaining flags in the DAG nodes.
  virtual void schedNode(SUnit *SU, bool IsTopNode) = 0;

  /// When all predecessor dependencies have been resolved, free this node for
  /// top-down scheduling.
  virtual void releaseTopNode(SUnit *SU) = 0;

  /// When all successor dependencies have been resolved, free this node for
  /// bottom-up scheduling.
  virtual void releaseBottomNode(SUnit *SU) = 0;
};

/// ScheduleDAGMI is an implementation of ScheduleDAGInstrs that simply
/// schedules machine instructions according to the given MachineSchedStrategy
/// without much extra book-keeping. This is the common functionality between
/// PreRA and PostRA MachineScheduler.
class ScheduleDAGMI : public ScheduleDAGInstrs {
protected:
  AliasAnalysis *AA;
  LiveIntervals *LIS;
  std::unique_ptr<MachineSchedStrategy> SchedImpl;

  /// Topo - A topological ordering for SUnits which permits fast IsReachable
  /// and similar queries.
  ScheduleDAGTopologicalSort Topo;

  /// Ordered list of DAG postprocessing steps.
  std::vector<std::unique_ptr<ScheduleDAGMutation>> Mutations;

  /// The top of the unscheduled zone.
  MachineBasicBlock::iterator CurrentTop;

  /// The bottom of the unscheduled zone.
  MachineBasicBlock::iterator CurrentBottom;

  /// Record the next node in a scheduled cluster.
  const SUnit *NextClusterPred = nullptr;
  const SUnit *NextClusterSucc = nullptr;

#ifndef NDEBUG
  /// The number of instructions scheduled so far. Used to cut off the
  /// scheduler at the point determined by misched-cutoff.
  unsigned NumInstrsScheduled = 0;
#endif

public:
  ScheduleDAGMI(MachineSchedContext *C, std::unique_ptr<MachineSchedStrategy> S,
                bool RemoveKillFlags)
      : ScheduleDAGInstrs(*C->MF, C->MLI, RemoveKillFlags), AA(C->AA),
        LIS(C->LIS), SchedImpl(std::move(S)), Topo(SUnits, &ExitSU) {}

  // Provide a vtable anchor
  ~ScheduleDAGMI() override;

  /// If this method returns true, handling of the scheduling regions
  /// themselves (in case of a scheduling boundary in MBB) will be done
  /// beginning with the topmost region of MBB.
  bool doMBBSchedRegionsTopDown() const override {
    return SchedImpl->doMBBSchedRegionsTopDown();
  }

  // Returns LiveIntervals instance for use in DAG mutators and such.
  LiveIntervals *getLIS() const { return LIS; }

  /// Return true if this DAG supports VReg liveness and RegPressure.
  virtual bool hasVRegLiveness() const { return false; }

  /// Add a postprocessing step to the DAG builder.
  /// Mutations are applied in the order that they are added after normal DAG
  /// building and before MachineSchedStrategy initialization.
  ///
  /// ScheduleDAGMI takes ownership of the Mutation object.
  void addMutation(std::unique_ptr<ScheduleDAGMutation> Mutation) {
    if (Mutation)
      Mutations.push_back(std::move(Mutation));
  }

  /// True if an edge can be added from PredSU to SuccSU without creating
  /// a cycle.
  bool canAddEdge(SUnit *SuccSU, SUnit *PredSU);

  /// Add a DAG edge to the given SU with the given predecessor
  /// dependence data.
  ///
  /// \returns true if the edge may be added without creating a cycle OR if an
  /// equivalent edge already existed (false indicates failure).
  bool addEdge(SUnit *SuccSU, const SDep &PredDep);

  MachineBasicBlock::iterator top() const { return CurrentTop; }
  MachineBasicBlock::iterator bottom() const { return CurrentBottom; }

  /// Implement the ScheduleDAGInstrs interface for handling the next scheduling
  /// region. This covers all instructions in a block, while schedule() may only
  /// cover a subset.
  void enterRegion(MachineBasicBlock *bb,
                   MachineBasicBlock::iterator begin,
                   MachineBasicBlock::iterator end,
                   unsigned regioninstrs) override;

  /// Implement ScheduleDAGInstrs interface for scheduling a sequence of
  /// reorderable instructions.
  void schedule() override;

  void startBlock(MachineBasicBlock *bb) override;
  void finishBlock() override;

  /// Change the position of an instruction within the basic block and update
  /// live ranges and region boundary iterators.
  void moveInstruction(MachineInstr *MI, MachineBasicBlock::iterator InsertPos);

  const SUnit *getNextClusterPred() const { return NextClusterPred; }

  const SUnit *getNextClusterSucc() const { return NextClusterSucc; }

  void viewGraph(const Twine &Name, const Twine &Title) override;
  void viewGraph() override;

protected:
  // Top-Level entry points for the schedule() driver...

  /// Apply each ScheduleDAGMutation step in order. This allows different
  /// instances of ScheduleDAGMI to perform custom DAG postprocessing.
  void postprocessDAG();

  /// Release ExitSU predecessors and setup scheduler queues.
  void initQueues(ArrayRef<SUnit*> TopRoots, ArrayRef<SUnit*> BotRoots);

  /// Update scheduler DAG and queues after scheduling an instruction.
  void updateQueues(SUnit *SU, bool IsTopNode);

  /// Reinsert debug_values recorded in ScheduleDAGInstrs::DbgValues.
  void placeDebugValues();

  /// dump the scheduled Sequence.
  void dumpSchedule() const;

  // Lesser helpers...
  bool checkSchedLimit();

  void findRootsAndBiasEdges(SmallVectorImpl<SUnit*> &TopRoots,
                             SmallVectorImpl<SUnit*> &BotRoots);

  void releaseSucc(SUnit *SU, SDep *SuccEdge);
  void releaseSuccessors(SUnit *SU);
  void releasePred(SUnit *SU, SDep *PredEdge);
  void releasePredecessors(SUnit *SU);
};

/// ScheduleDAGMILive is an implementation of ScheduleDAGInstrs that schedules
/// machine instructions while updating LiveIntervals and tracking regpressure.
class ScheduleDAGMILive : public ScheduleDAGMI {
protected:
  RegisterClassInfo *RegClassInfo;

  /// Information about DAG subtrees. If DFSResult is NULL, then SchedulerTrees
  /// will be empty.
  SchedDFSResult *DFSResult = nullptr;
  BitVector ScheduledTrees;

  MachineBasicBlock::iterator LiveRegionEnd;

  /// Maps vregs to the SUnits of their uses in the current scheduling region.
  VReg2SUnitMultiMap VRegUses;

  // Map each SU to its summary of pressure changes. This array is updated for
  // liveness during bottom-up scheduling. Top-down scheduling may proceed but
  // has no affect on the pressure diffs.
  PressureDiffs SUPressureDiffs;

  /// Register pressure in this region computed by initRegPressure.
  bool ShouldTrackPressure = false;
  bool ShouldTrackLaneMasks = false;
  IntervalPressure RegPressure;
  RegPressureTracker RPTracker;

  /// List of pressure sets that exceed the target's pressure limit before
  /// scheduling, listed in increasing set ID order. Each pressure set is paired
  /// with its max pressure in the currently scheduled regions.
  std::vector<PressureChange> RegionCriticalPSets;

  /// The top of the unscheduled zone.
  IntervalPressure TopPressure;
  RegPressureTracker TopRPTracker;

  /// The bottom of the unscheduled zone.
  IntervalPressure BotPressure;
  RegPressureTracker BotRPTracker;

  /// True if disconnected subregister components are already renamed.
  /// The renaming is only done on demand if lane masks are tracked.
  bool DisconnectedComponentsRenamed = false;

public:
  ScheduleDAGMILive(MachineSchedContext *C,
                    std::unique_ptr<MachineSchedStrategy> S)
      : ScheduleDAGMI(C, std::move(S), /*RemoveKillFlags=*/false),
        RegClassInfo(C->RegClassInfo), RPTracker(RegPressure),
        TopRPTracker(TopPressure), BotRPTracker(BotPressure) {}

  ~ScheduleDAGMILive() override;

  /// Return true if this DAG supports VReg liveness and RegPressure.
  bool hasVRegLiveness() const override { return true; }

  /// Return true if register pressure tracking is enabled.
  bool isTrackingPressure() const { return ShouldTrackPressure; }

  /// Get current register pressure for the top scheduled instructions.
  const IntervalPressure &getTopPressure() const { return TopPressure; }
  const RegPressureTracker &getTopRPTracker() const { return TopRPTracker; }

  /// Get current register pressure for the bottom scheduled instructions.
  const IntervalPressure &getBotPressure() const { return BotPressure; }
  const RegPressureTracker &getBotRPTracker() const { return BotRPTracker; }

  /// Get register pressure for the entire scheduling region before scheduling.
  const IntervalPressure &getRegPressure() const { return RegPressure; }

  const std::vector<PressureChange> &getRegionCriticalPSets() const {
    return RegionCriticalPSets;
  }

  PressureDiff &getPressureDiff(const SUnit *SU) {
    return SUPressureDiffs[SU->NodeNum];
  }
  const PressureDiff &getPressureDiff(const SUnit *SU) const {
    return SUPressureDiffs[SU->NodeNum];
  }

  /// Compute a DFSResult after DAG building is complete, and before any
  /// queue comparisons.
  void computeDFSResult();

  /// Return a non-null DFS result if the scheduling strategy initialized it.
  const SchedDFSResult *getDFSResult() const { return DFSResult; }

  BitVector &getScheduledTrees() { return ScheduledTrees; }

  /// Implement the ScheduleDAGInstrs interface for handling the next scheduling
  /// region. This covers all instructions in a block, while schedule() may only
  /// cover a subset.
  void enterRegion(MachineBasicBlock *bb,
                   MachineBasicBlock::iterator begin,
                   MachineBasicBlock::iterator end,
                   unsigned regioninstrs) override;

  /// Implement ScheduleDAGInstrs interface for scheduling a sequence of
  /// reorderable instructions.
  void schedule() override;

  /// Compute the cyclic critical path through the DAG.
  unsigned computeCyclicCriticalPath();

  void dump() const override;

protected:
  // Top-Level entry points for the schedule() driver...

  /// Call ScheduleDAGInstrs::buildSchedGraph with register pressure tracking
  /// enabled. This sets up three trackers. RPTracker will cover the entire DAG
  /// region, TopTracker and BottomTracker will be initialized to the top and
  /// bottom of the DAG region without covereing any unscheduled instruction.
  void buildDAGWithRegPressure();

  /// Release ExitSU predecessors and setup scheduler queues. Re-position
  /// the Top RP tracker in case the region beginning has changed.
  void initQueues(ArrayRef<SUnit*> TopRoots, ArrayRef<SUnit*> BotRoots);

  /// Move an instruction and update register pressure.
  void scheduleMI(SUnit *SU, bool IsTopNode);

  // Lesser helpers...

  void initRegPressure();

  void updatePressureDiffs(ArrayRef<RegisterMaskPair> LiveUses);

  void updateScheduledPressure(const SUnit *SU,
                               const std::vector<unsigned> &NewMaxPressure);

  void collectVRegUses(SUnit &SU);
};

//===----------------------------------------------------------------------===//
///
/// Helpers for implementing custom MachineSchedStrategy classes. These take
/// care of the book-keeping associated with list scheduling heuristics.
///
//===----------------------------------------------------------------------===//

/// ReadyQueue encapsulates vector of "ready" SUnits with basic convenience
/// methods for pushing and removing nodes. ReadyQueue's are uniquely identified
/// by an ID. SUnit::NodeQueueId is a mask of the ReadyQueues the SUnit is in.
///
/// This is a convenience class that may be used by implementations of
/// MachineSchedStrategy.
class ReadyQueue {
  unsigned ID;
  std::string Name;
  std::vector<SUnit*> Queue;

public:
  ReadyQueue(unsigned id, const Twine &name): ID(id), Name(name.str()) {}

  unsigned getID() const { return ID; }

  StringRef getName() const { return Name; }

  // SU is in this queue if it's NodeQueueID is a superset of this ID.
  bool isInQueue(SUnit *SU) const { return (SU->NodeQueueId & ID); }

  bool empty() const { return Queue.empty(); }

  void clear() { Queue.clear(); }

  unsigned size() const { return Queue.size(); }

  using iterator = std::vector<SUnit*>::iterator;

  iterator begin() { return Queue.begin(); }

  iterator end() { return Queue.end(); }

  ArrayRef<SUnit*> elements() { return Queue; }

  iterator find(SUnit *SU) { return llvm::find(Queue, SU); }

  void push(SUnit *SU) {
    Queue.push_back(SU);
    SU->NodeQueueId |= ID;
  }

  iterator remove(iterator I) {
    (*I)->NodeQueueId &= ~ID;
    *I = Queue.back();
    unsigned idx = I - Queue.begin();
    Queue.pop_back();
    return Queue.begin() + idx;
  }

  void dump() const;
};

/// Summarize the unscheduled region.
struct SchedRemainder {
  // Critical path through the DAG in expected latency.
  unsigned CriticalPath;
  unsigned CyclicCritPath;

  // Scaled count of micro-ops left to schedule.
  unsigned RemIssueCount;

  bool IsAcyclicLatencyLimited;

  // Unscheduled resources
  SmallVector<unsigned, 16> RemainingCounts;

  SchedRemainder() { reset(); }

  void reset() {
    CriticalPath = 0;
    CyclicCritPath = 0;
    RemIssueCount = 0;
    IsAcyclicLatencyLimited = false;
    RemainingCounts.clear();
  }

  void init(ScheduleDAGMI *DAG, const TargetSchedModel *SchedModel);
};

/// Each Scheduling boundary is associated with ready queues. It tracks the
/// current cycle in the direction of movement, and maintains the state
/// of "hazards" and other interlocks at the current cycle.
class SchedBoundary {
public:
  /// SUnit::NodeQueueId: 0 (none), 1 (top), 2 (bot), 3 (both)
  enum {
    TopQID = 1,
    BotQID = 2,
    LogMaxQID = 2
  };

  ScheduleDAGMI *DAG = nullptr;
  const TargetSchedModel *SchedModel = nullptr;
  SchedRemainder *Rem = nullptr;

  ReadyQueue Available;
  ReadyQueue Pending;

  ScheduleHazardRecognizer *HazardRec = nullptr;

private:
  /// True if the pending Q should be checked/updated before scheduling another
  /// instruction.
  bool CheckPending;

  /// Number of cycles it takes to issue the instructions scheduled in this
  /// zone. It is defined as: scheduled-micro-ops / issue-width + stalls.
  /// See getStalls().
  unsigned CurrCycle;

  /// Micro-ops issued in the current cycle
  unsigned CurrMOps;

  /// MinReadyCycle - Cycle of the soonest available instruction.
  unsigned MinReadyCycle;

  // The expected latency of the critical path in this scheduled zone.
  unsigned ExpectedLatency;

  // The latency of dependence chains leading into this zone.
  // For each node scheduled bottom-up: DLat = max DLat, N.Depth.
  // For each cycle scheduled: DLat -= 1.
  unsigned DependentLatency;

  /// Count the scheduled (issued) micro-ops that can be retired by
  /// time=CurrCycle assuming the first scheduled instr is retired at time=0.
  unsigned RetiredMOps;

  // Count scheduled resources that have been executed. Resources are
  // considered executed if they become ready in the time that it takes to
  // saturate any resource including the one in question. Counts are scaled
  // for direct comparison with other resources. Counts can be compared with
  // MOps * getMicroOpFactor and Latency * getLatencyFactor.
  SmallVector<unsigned, 16> ExecutedResCounts;

  /// Cache the max count for a single resource.
  unsigned MaxExecutedResCount;

  // Cache the critical resources ID in this scheduled zone.
  unsigned ZoneCritResIdx;

  // Is the scheduled region resource limited vs. latency limited.
  bool IsResourceLimited;

  // Record the highest cycle at which each resource has been reserved by a
  // scheduled instruction.
  SmallVector<unsigned, 16> ReservedCycles;

#ifndef NDEBUG
  // Remember the greatest possible stall as an upper bound on the number of
  // times we should retry the pending queue because of a hazard.
  unsigned MaxObservedStall;
#endif

public:
  /// Pending queues extend the ready queues with the same ID and the
  /// PendingFlag set.
  SchedBoundary(unsigned ID, const Twine &Name):
    Available(ID, Name+".A"), Pending(ID << LogMaxQID, Name+".P") {
    reset();
  }

  ~SchedBoundary();

  void reset();

  void init(ScheduleDAGMI *dag, const TargetSchedModel *smodel,
            SchedRemainder *rem);

  bool isTop() const {
    return Available.getID() == TopQID;
  }

  /// Number of cycles to issue the instructions scheduled in this zone.
  unsigned getCurrCycle() const { return CurrCycle; }

  /// Micro-ops issued in the current cycle
  unsigned getCurrMOps() const { return CurrMOps; }

  // The latency of dependence chains leading into this zone.
  unsigned getDependentLatency() const { return DependentLatency; }

  /// Get the number of latency cycles "covered" by the scheduled
  /// instructions. This is the larger of the critical path within the zone
  /// and the number of cycles required to issue the instructions.
  unsigned getScheduledLatency() const {
    return std::max(ExpectedLatency, CurrCycle);
  }

  unsigned getUnscheduledLatency(SUnit *SU) const {
    return isTop() ? SU->getHeight() : SU->getDepth();
  }

  unsigned getResourceCount(unsigned ResIdx) const {
    return ExecutedResCounts[ResIdx];
  }

  /// Get the scaled count of scheduled micro-ops and resources, including
  /// executed resources.
  unsigned getCriticalCount() const {
    if (!ZoneCritResIdx)
      return RetiredMOps * SchedModel->getMicroOpFactor();
    return getResourceCount(ZoneCritResIdx);
  }

  /// Get a scaled count for the minimum execution time of the scheduled
  /// micro-ops that are ready to execute by getExecutedCount. Notice the
  /// feedback loop.
  unsigned getExecutedCount() const {
    return std::max(CurrCycle * SchedModel->getLatencyFactor(),
                    MaxExecutedResCount);
  }

  unsigned getZoneCritResIdx() const { return ZoneCritResIdx; }

  // Is the scheduled region resource limited vs. latency limited.
  bool isResourceLimited() const { return IsResourceLimited; }

  /// Get the difference between the given SUnit's ready time and the current
  /// cycle.
  unsigned getLatencyStallCycles(SUnit *SU);

  unsigned getNextResourceCycle(unsigned PIdx, unsigned Cycles);

  bool checkHazard(SUnit *SU);

  unsigned findMaxLatency(ArrayRef<SUnit*> ReadySUs);

  unsigned getOtherResourceCount(unsigned &OtherCritIdx);

  void releaseNode(SUnit *SU, unsigned ReadyCycle);

  void bumpCycle(unsigned NextCycle);

  void incExecutedResources(unsigned PIdx, unsigned Count);

  unsigned countResource(unsigned PIdx, unsigned Cycles, unsigned ReadyCycle);

  void bumpNode(SUnit *SU);

  void releasePending();

  void removeReady(SUnit *SU);

  /// Call this before applying any other heuristics to the Available queue.
  /// Updates the Available/Pending Q's if necessary and returns the single
  /// available instruction, or NULL if there are multiple candidates.
  SUnit *pickOnlyChoice();

  void dumpScheduledState() const;
};

/// Base class for GenericScheduler. This class maintains information about
/// scheduling candidates based on TargetSchedModel making it easy to implement
/// heuristics for either preRA or postRA scheduling.
class GenericSchedulerBase : public MachineSchedStrategy {
public:
  /// Represent the type of SchedCandidate found within a single queue.
  /// pickNodeBidirectional depends on these listed by decreasing priority.
  enum CandReason : uint8_t {
    NoCand, Only1, PhysReg, RegExcess, RegCritical, Stall, Cluster, Weak,
    RegMax, ResourceReduce, ResourceDemand, BotHeightReduce, BotPathReduce,
    TopDepthReduce, TopPathReduce, NextDefUse, NodeOrder};

#ifndef NDEBUG
  static const char *getReasonStr(GenericSchedulerBase::CandReason Reason);
#endif

  /// Policy for scheduling the next instruction in the candidate's zone.
  struct CandPolicy {
    bool ReduceLatency = false;
    unsigned ReduceResIdx = 0;
    unsigned DemandResIdx = 0;

    CandPolicy() = default;

    bool operator==(const CandPolicy &RHS) const {
      return ReduceLatency == RHS.ReduceLatency &&
             ReduceResIdx == RHS.ReduceResIdx &&
             DemandResIdx == RHS.DemandResIdx;
    }
    bool operator!=(const CandPolicy &RHS) const {
      return !(*this == RHS);
    }
  };

  /// Status of an instruction's critical resource consumption.
  struct SchedResourceDelta {
    // Count critical resources in the scheduled region required by SU.
    unsigned CritResources = 0;

    // Count critical resources from another region consumed by SU.
    unsigned DemandedResources = 0;

    SchedResourceDelta() = default;

    bool operator==(const SchedResourceDelta &RHS) const {
      return CritResources == RHS.CritResources
        && DemandedResources == RHS.DemandedResources;
    }
    bool operator!=(const SchedResourceDelta &RHS) const {
      return !operator==(RHS);
    }
  };

  /// Store the state used by GenericScheduler heuristics, required for the
  /// lifetime of one invocation of pickNode().
  struct SchedCandidate {
    CandPolicy Policy;

    // The best SUnit candidate.
    SUnit *SU;

    // The reason for this candidate.
    CandReason Reason;

    // Whether this candidate should be scheduled at top/bottom.
    bool AtTop;

    // Register pressure values for the best candidate.
    RegPressureDelta RPDelta;

    // Critical resource consumption of the best candidate.
    SchedResourceDelta ResDelta;

    SchedCandidate() { reset(CandPolicy()); }
    SchedCandidate(const CandPolicy &Policy) { reset(Policy); }

    void reset(const CandPolicy &NewPolicy) {
      Policy = NewPolicy;
      SU = nullptr;
      Reason = NoCand;
      AtTop = false;
      RPDelta = RegPressureDelta();
      ResDelta = SchedResourceDelta();
    }

    bool isValid() const { return SU; }

    // Copy the status of another candidate without changing policy.
    void setBest(SchedCandidate &Best) {
      assert(Best.Reason != NoCand && "uninitialized Sched candidate");
      SU = Best.SU;
      Reason = Best.Reason;
      AtTop = Best.AtTop;
      RPDelta = Best.RPDelta;
      ResDelta = Best.ResDelta;
    }

    void initResourceDelta(const ScheduleDAGMI *DAG,
                           const TargetSchedModel *SchedModel);
  };

protected:
  const MachineSchedContext *Context;
  const TargetSchedModel *SchedModel = nullptr;
  const TargetRegisterInfo *TRI = nullptr;

  SchedRemainder Rem;

  GenericSchedulerBase(const MachineSchedContext *C) : Context(C) {}

  void setPolicy(CandPolicy &Policy, bool IsPostRA, SchedBoundary &CurrZone,
                 SchedBoundary *OtherZone);

#ifndef NDEBUG
  void traceCandidate(const SchedCandidate &Cand);
#endif

private:
  bool shouldReduceLatency(const CandPolicy &Policy, SchedBoundary &CurrZone,
                           bool ComputeRemLatency, unsigned &RemLatency) const;
};

// Utility functions used by heuristics in tryCandidate().
bool tryLess(int TryVal, int CandVal,
             GenericSchedulerBase::SchedCandidate &TryCand,
             GenericSchedulerBase::SchedCandidate &Cand,
             GenericSchedulerBase::CandReason Reason);
bool tryGreater(int TryVal, int CandVal,
                GenericSchedulerBase::SchedCandidate &TryCand,
                GenericSchedulerBase::SchedCandidate &Cand,
                GenericSchedulerBase::CandReason Reason);
bool tryLatency(GenericSchedulerBase::SchedCandidate &TryCand,
                GenericSchedulerBase::SchedCandidate &Cand,
                SchedBoundary &Zone);
bool tryPressure(const PressureChange &TryP,
                 const PressureChange &CandP,
                 GenericSchedulerBase::SchedCandidate &TryCand,
                 GenericSchedulerBase::SchedCandidate &Cand,
                 GenericSchedulerBase::CandReason Reason,
                 const TargetRegisterInfo *TRI,
                 const MachineFunction &MF);
unsigned getWeakLeft(const SUnit *SU, bool isTop);
int biasPhysReg(const SUnit *SU, bool isTop);

/// GenericScheduler shrinks the unscheduled zone using heuristics to balance
/// the schedule.
class GenericScheduler : public GenericSchedulerBase {
public:
  GenericScheduler(const MachineSchedContext *C):
    GenericSchedulerBase(C), Top(SchedBoundary::TopQID, "TopQ"),
    Bot(SchedBoundary::BotQID, "BotQ") {}

  void initPolicy(MachineBasicBlock::iterator Begin,
                  MachineBasicBlock::iterator End,
                  unsigned NumRegionInstrs) override;

  void dumpPolicy() const override;

  bool shouldTrackPressure() const override {
    return RegionPolicy.ShouldTrackPressure;
  }

  bool shouldTrackLaneMasks() const override {
    return RegionPolicy.ShouldTrackLaneMasks;
  }

  void initialize(ScheduleDAGMI *dag) override;

  SUnit *pickNode(bool &IsTopNode) override;

  void schedNode(SUnit *SU, bool IsTopNode) override;

  void releaseTopNode(SUnit *SU) override {
    if (SU->isScheduled)
      return;

    Top.releaseNode(SU, SU->TopReadyCycle);
    TopCand.SU = nullptr;
  }

  void releaseBottomNode(SUnit *SU) override {
    if (SU->isScheduled)
      return;

    Bot.releaseNode(SU, SU->BotReadyCycle);
    BotCand.SU = nullptr;
  }

  void registerRoots() override;

protected:
  ScheduleDAGMILive *DAG = nullptr;

  MachineSchedPolicy RegionPolicy;

  // State of the top and bottom scheduled instruction boundaries.
  SchedBoundary Top;
  SchedBoundary Bot;

  /// Candidate last picked from Top boundary.
  SchedCandidate TopCand;
  /// Candidate last picked from Bot boundary.
  SchedCandidate BotCand;

  void checkAcyclicLatency();

  void initCandidate(SchedCandidate &Cand, SUnit *SU, bool AtTop,
                     const RegPressureTracker &RPTracker,
                     RegPressureTracker &TempTracker);

  virtual void tryCandidate(SchedCandidate &Cand, SchedCandidate &TryCand,
                            SchedBoundary *Zone) const;

  SUnit *pickNodeBidirectional(bool &IsTopNode);

  void pickNodeFromQueue(SchedBoundary &Zone,
                         const CandPolicy &ZonePolicy,
                         const RegPressureTracker &RPTracker,
                         SchedCandidate &Candidate);

  void reschedulePhysReg(SUnit *SU, bool isTop);
};

/// PostGenericScheduler - Interface to the scheduling algorithm used by
/// ScheduleDAGMI.
///
/// Callbacks from ScheduleDAGMI:
///   initPolicy -> initialize(DAG) -> registerRoots -> pickNode ...
class PostGenericScheduler : public GenericSchedulerBase {
  ScheduleDAGMI *DAG;
  SchedBoundary Top;
  SmallVector<SUnit*, 8> BotRoots;

public:
  PostGenericScheduler(const MachineSchedContext *C):
    GenericSchedulerBase(C), Top(SchedBoundary::TopQID, "TopQ") {}

  ~PostGenericScheduler() override = default;

  void initPolicy(MachineBasicBlock::iterator Begin,
                  MachineBasicBlock::iterator End,
                  unsigned NumRegionInstrs) override {
    /* no configurable policy */
  }

  /// PostRA scheduling does not track pressure.
  bool shouldTrackPressure() const override { return false; }

  void initialize(ScheduleDAGMI *Dag) override;

  void registerRoots() override;

  SUnit *pickNode(bool &IsTopNode) override;

  void scheduleTree(unsigned SubtreeID) override {
    llvm_unreachable("PostRA scheduler does not support subtree analysis.");
  }

  void schedNode(SUnit *SU, bool IsTopNode) override;

  void releaseTopNode(SUnit *SU) override {
    if (SU->isScheduled)
      return;
    Top.releaseNode(SU, SU->TopReadyCycle);
  }

  // Only called for roots.
  void releaseBottomNode(SUnit *SU) override {
    BotRoots.push_back(SU);
  }

protected:
  void tryCandidate(SchedCandidate &Cand, SchedCandidate &TryCand);

  void pickNodeFromQueue(SchedCandidate &Cand);
};

/// Create the standard converging machine scheduler. This will be used as the
/// default scheduler if the target does not set a default.
/// Adds default DAG mutations.
ScheduleDAGMILive *createGenericSchedLive(MachineSchedContext *C);

/// Create a generic scheduler with no vreg liveness or DAG mutation passes.
ScheduleDAGMI *createGenericSchedPostRA(MachineSchedContext *C);

std::unique_ptr<ScheduleDAGMutation>
createLoadClusterDAGMutation(const TargetInstrInfo *TII,
                             const TargetRegisterInfo *TRI);

std::unique_ptr<ScheduleDAGMutation>
createStoreClusterDAGMutation(const TargetInstrInfo *TII,
                              const TargetRegisterInfo *TRI);

std::unique_ptr<ScheduleDAGMutation>
createCopyConstrainDAGMutation(const TargetInstrInfo *TII,
                               const TargetRegisterInfo *TRI);

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINESCHEDULER_H
