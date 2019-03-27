//===----- SchedulePostRAList.cpp - list scheduler ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This implements a top-down list scheduler, using standard algorithms.
// The basic approach uses a priority queue of available nodes to schedule.
// One at a time, nodes are taken from the priority queue (thus in priority
// order), checked for legality to schedule, and emitted if legal.
//
// Nodes may not be legal to schedule either due to structural hazards (e.g.
// pipeline or resource constraints) or because an input to the instruction has
// not completed execution.
//
//===----------------------------------------------------------------------===//

#include "AggressiveAntiDepBreaker.h"
#include "AntiDepBreaker.h"
#include "CriticalAntiDepBreaker.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/LatencyPriorityQueue.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/RegisterClassInfo.h"
#include "llvm/CodeGen/ScheduleDAGInstrs.h"
#include "llvm/CodeGen/ScheduleHazardRecognizer.h"
#include "llvm/CodeGen/SchedulerRegistry.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "post-RA-sched"

STATISTIC(NumNoops, "Number of noops inserted");
STATISTIC(NumStalls, "Number of pipeline stalls");
STATISTIC(NumFixedAnti, "Number of fixed anti-dependencies");

// Post-RA scheduling is enabled with
// TargetSubtargetInfo.enablePostRAScheduler(). This flag can be used to
// override the target.
static cl::opt<bool>
EnablePostRAScheduler("post-RA-scheduler",
                       cl::desc("Enable scheduling after register allocation"),
                       cl::init(false), cl::Hidden);
static cl::opt<std::string>
EnableAntiDepBreaking("break-anti-dependencies",
                      cl::desc("Break post-RA scheduling anti-dependencies: "
                               "\"critical\", \"all\", or \"none\""),
                      cl::init("none"), cl::Hidden);

// If DebugDiv > 0 then only schedule MBB with (ID % DebugDiv) == DebugMod
static cl::opt<int>
DebugDiv("postra-sched-debugdiv",
                      cl::desc("Debug control MBBs that are scheduled"),
                      cl::init(0), cl::Hidden);
static cl::opt<int>
DebugMod("postra-sched-debugmod",
                      cl::desc("Debug control MBBs that are scheduled"),
                      cl::init(0), cl::Hidden);

AntiDepBreaker::~AntiDepBreaker() { }

namespace {
  class PostRAScheduler : public MachineFunctionPass {
    const TargetInstrInfo *TII;
    RegisterClassInfo RegClassInfo;

  public:
    static char ID;
    PostRAScheduler() : MachineFunctionPass(ID) {}

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesCFG();
      AU.addRequired<AAResultsWrapperPass>();
      AU.addRequired<TargetPassConfig>();
      AU.addRequired<MachineDominatorTree>();
      AU.addPreserved<MachineDominatorTree>();
      AU.addRequired<MachineLoopInfo>();
      AU.addPreserved<MachineLoopInfo>();
      MachineFunctionPass::getAnalysisUsage(AU);
    }

    MachineFunctionProperties getRequiredProperties() const override {
      return MachineFunctionProperties().set(
          MachineFunctionProperties::Property::NoVRegs);
    }

    bool runOnMachineFunction(MachineFunction &Fn) override;

  private:
    bool enablePostRAScheduler(
        const TargetSubtargetInfo &ST, CodeGenOpt::Level OptLevel,
        TargetSubtargetInfo::AntiDepBreakMode &Mode,
        TargetSubtargetInfo::RegClassVector &CriticalPathRCs) const;
  };
  char PostRAScheduler::ID = 0;

  class SchedulePostRATDList : public ScheduleDAGInstrs {
    /// AvailableQueue - The priority queue to use for the available SUnits.
    ///
    LatencyPriorityQueue AvailableQueue;

    /// PendingQueue - This contains all of the instructions whose operands have
    /// been issued, but their results are not ready yet (due to the latency of
    /// the operation).  Once the operands becomes available, the instruction is
    /// added to the AvailableQueue.
    std::vector<SUnit*> PendingQueue;

    /// HazardRec - The hazard recognizer to use.
    ScheduleHazardRecognizer *HazardRec;

    /// AntiDepBreak - Anti-dependence breaking object, or NULL if none
    AntiDepBreaker *AntiDepBreak;

    /// AA - AliasAnalysis for making memory reference queries.
    AliasAnalysis *AA;

    /// The schedule. Null SUnit*'s represent noop instructions.
    std::vector<SUnit*> Sequence;

    /// Ordered list of DAG postprocessing steps.
    std::vector<std::unique_ptr<ScheduleDAGMutation>> Mutations;

    /// The index in BB of RegionEnd.
    ///
    /// This is the instruction number from the top of the current block, not
    /// the SlotIndex. It is only used by the AntiDepBreaker.
    unsigned EndIndex;

  public:
    SchedulePostRATDList(
        MachineFunction &MF, MachineLoopInfo &MLI, AliasAnalysis *AA,
        const RegisterClassInfo &,
        TargetSubtargetInfo::AntiDepBreakMode AntiDepMode,
        SmallVectorImpl<const TargetRegisterClass *> &CriticalPathRCs);

    ~SchedulePostRATDList() override;

    /// startBlock - Initialize register live-range state for scheduling in
    /// this block.
    ///
    void startBlock(MachineBasicBlock *BB) override;

    // Set the index of RegionEnd within the current BB.
    void setEndIndex(unsigned EndIdx) { EndIndex = EndIdx; }

    /// Initialize the scheduler state for the next scheduling region.
    void enterRegion(MachineBasicBlock *bb,
                     MachineBasicBlock::iterator begin,
                     MachineBasicBlock::iterator end,
                     unsigned regioninstrs) override;

    /// Notify that the scheduler has finished scheduling the current region.
    void exitRegion() override;

    /// Schedule - Schedule the instruction range using list scheduling.
    ///
    void schedule() override;

    void EmitSchedule();

    /// Observe - Update liveness information to account for the current
    /// instruction, which will not be scheduled.
    ///
    void Observe(MachineInstr &MI, unsigned Count);

    /// finishBlock - Clean up register live-range state.
    ///
    void finishBlock() override;

  private:
    /// Apply each ScheduleDAGMutation step in order.
    void postprocessDAG();

    void ReleaseSucc(SUnit *SU, SDep *SuccEdge);
    void ReleaseSuccessors(SUnit *SU);
    void ScheduleNodeTopDown(SUnit *SU, unsigned CurCycle);
    void ListScheduleTopDown();

    void dumpSchedule() const;
    void emitNoop(unsigned CurCycle);
  };
}

char &llvm::PostRASchedulerID = PostRAScheduler::ID;

INITIALIZE_PASS(PostRAScheduler, DEBUG_TYPE,
                "Post RA top-down list latency scheduler", false, false)

SchedulePostRATDList::SchedulePostRATDList(
    MachineFunction &MF, MachineLoopInfo &MLI, AliasAnalysis *AA,
    const RegisterClassInfo &RCI,
    TargetSubtargetInfo::AntiDepBreakMode AntiDepMode,
    SmallVectorImpl<const TargetRegisterClass *> &CriticalPathRCs)
    : ScheduleDAGInstrs(MF, &MLI), AA(AA), EndIndex(0) {

  const InstrItineraryData *InstrItins =
      MF.getSubtarget().getInstrItineraryData();
  HazardRec =
      MF.getSubtarget().getInstrInfo()->CreateTargetPostRAHazardRecognizer(
          InstrItins, this);
  MF.getSubtarget().getPostRAMutations(Mutations);

  assert((AntiDepMode == TargetSubtargetInfo::ANTIDEP_NONE ||
          MRI.tracksLiveness()) &&
         "Live-ins must be accurate for anti-dependency breaking");
  AntiDepBreak =
    ((AntiDepMode == TargetSubtargetInfo::ANTIDEP_ALL) ?
     (AntiDepBreaker *)new AggressiveAntiDepBreaker(MF, RCI, CriticalPathRCs) :
     ((AntiDepMode == TargetSubtargetInfo::ANTIDEP_CRITICAL) ?
      (AntiDepBreaker *)new CriticalAntiDepBreaker(MF, RCI) : nullptr));
}

SchedulePostRATDList::~SchedulePostRATDList() {
  delete HazardRec;
  delete AntiDepBreak;
}

/// Initialize state associated with the next scheduling region.
void SchedulePostRATDList::enterRegion(MachineBasicBlock *bb,
                 MachineBasicBlock::iterator begin,
                 MachineBasicBlock::iterator end,
                 unsigned regioninstrs) {
  ScheduleDAGInstrs::enterRegion(bb, begin, end, regioninstrs);
  Sequence.clear();
}

/// Print the schedule before exiting the region.
void SchedulePostRATDList::exitRegion() {
  LLVM_DEBUG({
    dbgs() << "*** Final schedule ***\n";
    dumpSchedule();
    dbgs() << '\n';
  });
  ScheduleDAGInstrs::exitRegion();
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
/// dumpSchedule - dump the scheduled Sequence.
LLVM_DUMP_METHOD void SchedulePostRATDList::dumpSchedule() const {
  for (unsigned i = 0, e = Sequence.size(); i != e; i++) {
    if (SUnit *SU = Sequence[i])
      dumpNode(*SU);
    else
      dbgs() << "**** NOOP ****\n";
  }
}
#endif

bool PostRAScheduler::enablePostRAScheduler(
    const TargetSubtargetInfo &ST,
    CodeGenOpt::Level OptLevel,
    TargetSubtargetInfo::AntiDepBreakMode &Mode,
    TargetSubtargetInfo::RegClassVector &CriticalPathRCs) const {
  Mode = ST.getAntiDepBreakMode();
  ST.getCriticalPathRCs(CriticalPathRCs);

  // Check for explicit enable/disable of post-ra scheduling.
  if (EnablePostRAScheduler.getPosition() > 0)
    return EnablePostRAScheduler;

  return ST.enablePostRAScheduler() &&
         OptLevel >= ST.getOptLevelToEnablePostRAScheduler();
}

bool PostRAScheduler::runOnMachineFunction(MachineFunction &Fn) {
  if (skipFunction(Fn.getFunction()))
    return false;

  TII = Fn.getSubtarget().getInstrInfo();
  MachineLoopInfo &MLI = getAnalysis<MachineLoopInfo>();
  AliasAnalysis *AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();
  TargetPassConfig *PassConfig = &getAnalysis<TargetPassConfig>();

  RegClassInfo.runOnMachineFunction(Fn);

  TargetSubtargetInfo::AntiDepBreakMode AntiDepMode =
    TargetSubtargetInfo::ANTIDEP_NONE;
  SmallVector<const TargetRegisterClass*, 4> CriticalPathRCs;

  // Check that post-RA scheduling is enabled for this target.
  // This may upgrade the AntiDepMode.
  if (!enablePostRAScheduler(Fn.getSubtarget(), PassConfig->getOptLevel(),
                             AntiDepMode, CriticalPathRCs))
    return false;

  // Check for antidep breaking override...
  if (EnableAntiDepBreaking.getPosition() > 0) {
    AntiDepMode = (EnableAntiDepBreaking == "all")
      ? TargetSubtargetInfo::ANTIDEP_ALL
      : ((EnableAntiDepBreaking == "critical")
         ? TargetSubtargetInfo::ANTIDEP_CRITICAL
         : TargetSubtargetInfo::ANTIDEP_NONE);
  }

  LLVM_DEBUG(dbgs() << "PostRAScheduler\n");

  SchedulePostRATDList Scheduler(Fn, MLI, AA, RegClassInfo, AntiDepMode,
                                 CriticalPathRCs);

  // Loop over all of the basic blocks
  for (auto &MBB : Fn) {
#ifndef NDEBUG
    // If DebugDiv > 0 then only schedule MBB with (ID % DebugDiv) == DebugMod
    if (DebugDiv > 0) {
      static int bbcnt = 0;
      if (bbcnt++ % DebugDiv != DebugMod)
        continue;
      dbgs() << "*** DEBUG scheduling " << Fn.getName() << ":"
             << printMBBReference(MBB) << " ***\n";
    }
#endif

    // Initialize register live-range state for scheduling in this block.
    Scheduler.startBlock(&MBB);

    // Schedule each sequence of instructions not interrupted by a label
    // or anything else that effectively needs to shut down scheduling.
    MachineBasicBlock::iterator Current = MBB.end();
    unsigned Count = MBB.size(), CurrentCount = Count;
    for (MachineBasicBlock::iterator I = Current; I != MBB.begin();) {
      MachineInstr &MI = *std::prev(I);
      --Count;
      // Calls are not scheduling boundaries before register allocation, but
      // post-ra we don't gain anything by scheduling across calls since we
      // don't need to worry about register pressure.
      if (MI.isCall() || TII->isSchedulingBoundary(MI, &MBB, Fn)) {
        Scheduler.enterRegion(&MBB, I, Current, CurrentCount - Count);
        Scheduler.setEndIndex(CurrentCount);
        Scheduler.schedule();
        Scheduler.exitRegion();
        Scheduler.EmitSchedule();
        Current = &MI;
        CurrentCount = Count;
        Scheduler.Observe(MI, CurrentCount);
      }
      I = MI;
      if (MI.isBundle())
        Count -= MI.getBundleSize();
    }
    assert(Count == 0 && "Instruction count mismatch!");
    assert((MBB.begin() == Current || CurrentCount != 0) &&
           "Instruction count mismatch!");
    Scheduler.enterRegion(&MBB, MBB.begin(), Current, CurrentCount);
    Scheduler.setEndIndex(CurrentCount);
    Scheduler.schedule();
    Scheduler.exitRegion();
    Scheduler.EmitSchedule();

    // Clean up register live-range state.
    Scheduler.finishBlock();

    // Update register kills
    Scheduler.fixupKills(MBB);
  }

  return true;
}

/// StartBlock - Initialize register live-range state for scheduling in
/// this block.
///
void SchedulePostRATDList::startBlock(MachineBasicBlock *BB) {
  // Call the superclass.
  ScheduleDAGInstrs::startBlock(BB);

  // Reset the hazard recognizer and anti-dep breaker.
  HazardRec->Reset();
  if (AntiDepBreak)
    AntiDepBreak->StartBlock(BB);
}

/// Schedule - Schedule the instruction range using list scheduling.
///
void SchedulePostRATDList::schedule() {
  // Build the scheduling graph.
  buildSchedGraph(AA);

  if (AntiDepBreak) {
    unsigned Broken =
      AntiDepBreak->BreakAntiDependencies(SUnits, RegionBegin, RegionEnd,
                                          EndIndex, DbgValues);

    if (Broken != 0) {
      // We made changes. Update the dependency graph.
      // Theoretically we could update the graph in place:
      // When a live range is changed to use a different register, remove
      // the def's anti-dependence *and* output-dependence edges due to
      // that register, and add new anti-dependence and output-dependence
      // edges based on the next live range of the register.
      ScheduleDAG::clearDAG();
      buildSchedGraph(AA);

      NumFixedAnti += Broken;
    }
  }

  postprocessDAG();

  LLVM_DEBUG(dbgs() << "********** List Scheduling **********\n");
  LLVM_DEBUG(dump());

  AvailableQueue.initNodes(SUnits);
  ListScheduleTopDown();
  AvailableQueue.releaseState();
}

/// Observe - Update liveness information to account for the current
/// instruction, which will not be scheduled.
///
void SchedulePostRATDList::Observe(MachineInstr &MI, unsigned Count) {
  if (AntiDepBreak)
    AntiDepBreak->Observe(MI, Count, EndIndex);
}

/// FinishBlock - Clean up register live-range state.
///
void SchedulePostRATDList::finishBlock() {
  if (AntiDepBreak)
    AntiDepBreak->FinishBlock();

  // Call the superclass.
  ScheduleDAGInstrs::finishBlock();
}

/// Apply each ScheduleDAGMutation step in order.
void SchedulePostRATDList::postprocessDAG() {
  for (auto &M : Mutations)
    M->apply(this);
}

//===----------------------------------------------------------------------===//
//  Top-Down Scheduling
//===----------------------------------------------------------------------===//

/// ReleaseSucc - Decrement the NumPredsLeft count of a successor. Add it to
/// the PendingQueue if the count reaches zero.
void SchedulePostRATDList::ReleaseSucc(SUnit *SU, SDep *SuccEdge) {
  SUnit *SuccSU = SuccEdge->getSUnit();

  if (SuccEdge->isWeak()) {
    --SuccSU->WeakPredsLeft;
    return;
  }
#ifndef NDEBUG
  if (SuccSU->NumPredsLeft == 0) {
    dbgs() << "*** Scheduling failed! ***\n";
    dumpNode(*SuccSU);
    dbgs() << " has been released too many times!\n";
    llvm_unreachable(nullptr);
  }
#endif
  --SuccSU->NumPredsLeft;

  // Standard scheduler algorithms will recompute the depth of the successor
  // here as such:
  //   SuccSU->setDepthToAtLeast(SU->getDepth() + SuccEdge->getLatency());
  //
  // However, we lazily compute node depth instead. Note that
  // ScheduleNodeTopDown has already updated the depth of this node which causes
  // all descendents to be marked dirty. Setting the successor depth explicitly
  // here would cause depth to be recomputed for all its ancestors. If the
  // successor is not yet ready (because of a transitively redundant edge) then
  // this causes depth computation to be quadratic in the size of the DAG.

  // If all the node's predecessors are scheduled, this node is ready
  // to be scheduled. Ignore the special ExitSU node.
  if (SuccSU->NumPredsLeft == 0 && SuccSU != &ExitSU)
    PendingQueue.push_back(SuccSU);
}

/// ReleaseSuccessors - Call ReleaseSucc on each of SU's successors.
void SchedulePostRATDList::ReleaseSuccessors(SUnit *SU) {
  for (SUnit::succ_iterator I = SU->Succs.begin(), E = SU->Succs.end();
       I != E; ++I) {
    ReleaseSucc(SU, &*I);
  }
}

/// ScheduleNodeTopDown - Add the node to the schedule. Decrement the pending
/// count of its successors. If a successor pending count is zero, add it to
/// the Available queue.
void SchedulePostRATDList::ScheduleNodeTopDown(SUnit *SU, unsigned CurCycle) {
  LLVM_DEBUG(dbgs() << "*** Scheduling [" << CurCycle << "]: ");
  LLVM_DEBUG(dumpNode(*SU));

  Sequence.push_back(SU);
  assert(CurCycle >= SU->getDepth() &&
         "Node scheduled above its depth!");
  SU->setDepthToAtLeast(CurCycle);

  ReleaseSuccessors(SU);
  SU->isScheduled = true;
  AvailableQueue.scheduledNode(SU);
}

/// emitNoop - Add a noop to the current instruction sequence.
void SchedulePostRATDList::emitNoop(unsigned CurCycle) {
  LLVM_DEBUG(dbgs() << "*** Emitting noop in cycle " << CurCycle << '\n');
  HazardRec->EmitNoop();
  Sequence.push_back(nullptr);   // NULL here means noop
  ++NumNoops;
}

/// ListScheduleTopDown - The main loop of list scheduling for top-down
/// schedulers.
void SchedulePostRATDList::ListScheduleTopDown() {
  unsigned CurCycle = 0;

  // We're scheduling top-down but we're visiting the regions in
  // bottom-up order, so we don't know the hazards at the start of a
  // region. So assume no hazards (this should usually be ok as most
  // blocks are a single region).
  HazardRec->Reset();

  // Release any successors of the special Entry node.
  ReleaseSuccessors(&EntrySU);

  // Add all leaves to Available queue.
  for (unsigned i = 0, e = SUnits.size(); i != e; ++i) {
    // It is available if it has no predecessors.
    if (!SUnits[i].NumPredsLeft && !SUnits[i].isAvailable) {
      AvailableQueue.push(&SUnits[i]);
      SUnits[i].isAvailable = true;
    }
  }

  // In any cycle where we can't schedule any instructions, we must
  // stall or emit a noop, depending on the target.
  bool CycleHasInsts = false;

  // While Available queue is not empty, grab the node with the highest
  // priority. If it is not ready put it back.  Schedule the node.
  std::vector<SUnit*> NotReady;
  Sequence.reserve(SUnits.size());
  while (!AvailableQueue.empty() || !PendingQueue.empty()) {
    // Check to see if any of the pending instructions are ready to issue.  If
    // so, add them to the available queue.
    unsigned MinDepth = ~0u;
    for (unsigned i = 0, e = PendingQueue.size(); i != e; ++i) {
      if (PendingQueue[i]->getDepth() <= CurCycle) {
        AvailableQueue.push(PendingQueue[i]);
        PendingQueue[i]->isAvailable = true;
        PendingQueue[i] = PendingQueue.back();
        PendingQueue.pop_back();
        --i; --e;
      } else if (PendingQueue[i]->getDepth() < MinDepth)
        MinDepth = PendingQueue[i]->getDepth();
    }

    LLVM_DEBUG(dbgs() << "\n*** Examining Available\n";
               AvailableQueue.dump(this));

    SUnit *FoundSUnit = nullptr, *NotPreferredSUnit = nullptr;
    bool HasNoopHazards = false;
    while (!AvailableQueue.empty()) {
      SUnit *CurSUnit = AvailableQueue.pop();

      ScheduleHazardRecognizer::HazardType HT =
        HazardRec->getHazardType(CurSUnit, 0/*no stalls*/);
      if (HT == ScheduleHazardRecognizer::NoHazard) {
        if (HazardRec->ShouldPreferAnother(CurSUnit)) {
          if (!NotPreferredSUnit) {
            // If this is the first non-preferred node for this cycle, then
            // record it and continue searching for a preferred node. If this
            // is not the first non-preferred node, then treat it as though
            // there had been a hazard.
            NotPreferredSUnit = CurSUnit;
            continue;
          }
        } else {
          FoundSUnit = CurSUnit;
          break;
        }
      }

      // Remember if this is a noop hazard.
      HasNoopHazards |= HT == ScheduleHazardRecognizer::NoopHazard;

      NotReady.push_back(CurSUnit);
    }

    // If we have a non-preferred node, push it back onto the available list.
    // If we did not find a preferred node, then schedule this first
    // non-preferred node.
    if (NotPreferredSUnit) {
      if (!FoundSUnit) {
        LLVM_DEBUG(
            dbgs() << "*** Will schedule a non-preferred instruction...\n");
        FoundSUnit = NotPreferredSUnit;
      } else {
        AvailableQueue.push(NotPreferredSUnit);
      }

      NotPreferredSUnit = nullptr;
    }

    // Add the nodes that aren't ready back onto the available list.
    if (!NotReady.empty()) {
      AvailableQueue.push_all(NotReady);
      NotReady.clear();
    }

    // If we found a node to schedule...
    if (FoundSUnit) {
      // If we need to emit noops prior to this instruction, then do so.
      unsigned NumPreNoops = HazardRec->PreEmitNoops(FoundSUnit);
      for (unsigned i = 0; i != NumPreNoops; ++i)
        emitNoop(CurCycle);

      // ... schedule the node...
      ScheduleNodeTopDown(FoundSUnit, CurCycle);
      HazardRec->EmitInstruction(FoundSUnit);
      CycleHasInsts = true;
      if (HazardRec->atIssueLimit()) {
        LLVM_DEBUG(dbgs() << "*** Max instructions per cycle " << CurCycle
                          << '\n');
        HazardRec->AdvanceCycle();
        ++CurCycle;
        CycleHasInsts = false;
      }
    } else {
      if (CycleHasInsts) {
        LLVM_DEBUG(dbgs() << "*** Finished cycle " << CurCycle << '\n');
        HazardRec->AdvanceCycle();
      } else if (!HasNoopHazards) {
        // Otherwise, we have a pipeline stall, but no other problem,
        // just advance the current cycle and try again.
        LLVM_DEBUG(dbgs() << "*** Stall in cycle " << CurCycle << '\n');
        HazardRec->AdvanceCycle();
        ++NumStalls;
      } else {
        // Otherwise, we have no instructions to issue and we have instructions
        // that will fault if we don't do this right.  This is the case for
        // processors without pipeline interlocks and other cases.
        emitNoop(CurCycle);
      }

      ++CurCycle;
      CycleHasInsts = false;
    }
  }

#ifndef NDEBUG
  unsigned ScheduledNodes = VerifyScheduledDAG(/*isBottomUp=*/false);
  unsigned Noops = 0;
  for (unsigned i = 0, e = Sequence.size(); i != e; ++i)
    if (!Sequence[i])
      ++Noops;
  assert(Sequence.size() - Noops == ScheduledNodes &&
         "The number of nodes scheduled doesn't match the expected number!");
#endif // NDEBUG
}

// EmitSchedule - Emit the machine code in scheduled order.
void SchedulePostRATDList::EmitSchedule() {
  RegionBegin = RegionEnd;

  // If first instruction was a DBG_VALUE then put it back.
  if (FirstDbgValue)
    BB->splice(RegionEnd, BB, FirstDbgValue);

  // Then re-insert them according to the given schedule.
  for (unsigned i = 0, e = Sequence.size(); i != e; i++) {
    if (SUnit *SU = Sequence[i])
      BB->splice(RegionEnd, BB, SU->getInstr());
    else
      // Null SUnit* is a noop.
      TII->insertNoop(*BB, RegionEnd);

    // Update the Begin iterator, as the first instruction in the block
    // may have been scheduled later.
    if (i == 0)
      RegionBegin = std::prev(RegionEnd);
  }

  // Reinsert any remaining debug_values.
  for (std::vector<std::pair<MachineInstr *, MachineInstr *> >::iterator
         DI = DbgValues.end(), DE = DbgValues.begin(); DI != DE; --DI) {
    std::pair<MachineInstr *, MachineInstr *> P = *std::prev(DI);
    MachineInstr *DbgValue = P.first;
    MachineBasicBlock::iterator OrigPrivMI = P.second;
    BB->splice(++OrigPrivMI, BB, DbgValue);
  }
  DbgValues.clear();
  FirstDbgValue = nullptr;
}
