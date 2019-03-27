//===- MachineScheduler.cpp - Machine Instruction Scheduler ---------------===//
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

#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PriorityQueue.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachinePassRegistry.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/RegisterClassInfo.h"
#include "llvm/CodeGen/RegisterPressure.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/CodeGen/ScheduleDAGInstrs.h"
#include "llvm/CodeGen/ScheduleDAGMutation.h"
#include "llvm/CodeGen/ScheduleDFS.h"
#include "llvm/CodeGen/ScheduleHazardRecognizer.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSchedule.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/MachineValueType.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "machine-scheduler"

namespace llvm {

cl::opt<bool> ForceTopDown("misched-topdown", cl::Hidden,
                           cl::desc("Force top-down list scheduling"));
cl::opt<bool> ForceBottomUp("misched-bottomup", cl::Hidden,
                            cl::desc("Force bottom-up list scheduling"));
cl::opt<bool>
DumpCriticalPathLength("misched-dcpl", cl::Hidden,
                       cl::desc("Print critical path length to stdout"));

} // end namespace llvm

#ifndef NDEBUG
static cl::opt<bool> ViewMISchedDAGs("view-misched-dags", cl::Hidden,
  cl::desc("Pop up a window to show MISched dags after they are processed"));

/// In some situations a few uninteresting nodes depend on nearly all other
/// nodes in the graph, provide a cutoff to hide them.
static cl::opt<unsigned> ViewMISchedCutoff("view-misched-cutoff", cl::Hidden,
  cl::desc("Hide nodes with more predecessor/successor than cutoff"));

static cl::opt<unsigned> MISchedCutoff("misched-cutoff", cl::Hidden,
  cl::desc("Stop scheduling after N instructions"), cl::init(~0U));

static cl::opt<std::string> SchedOnlyFunc("misched-only-func", cl::Hidden,
  cl::desc("Only schedule this function"));
static cl::opt<unsigned> SchedOnlyBlock("misched-only-block", cl::Hidden,
                                        cl::desc("Only schedule this MBB#"));
static cl::opt<bool> PrintDAGs("misched-print-dags", cl::Hidden,
                              cl::desc("Print schedule DAGs"));
#else
static const bool ViewMISchedDAGs = false;
static const bool PrintDAGs = false;
#endif // NDEBUG

/// Avoid quadratic complexity in unusually large basic blocks by limiting the
/// size of the ready lists.
static cl::opt<unsigned> ReadyListLimit("misched-limit", cl::Hidden,
  cl::desc("Limit ready list to N instructions"), cl::init(256));

static cl::opt<bool> EnableRegPressure("misched-regpressure", cl::Hidden,
  cl::desc("Enable register pressure scheduling."), cl::init(true));

static cl::opt<bool> EnableCyclicPath("misched-cyclicpath", cl::Hidden,
  cl::desc("Enable cyclic critical path analysis."), cl::init(true));

static cl::opt<bool> EnableMemOpCluster("misched-cluster", cl::Hidden,
                                        cl::desc("Enable memop clustering."),
                                        cl::init(true));

static cl::opt<bool> VerifyScheduling("verify-misched", cl::Hidden,
  cl::desc("Verify machine instrs before and after machine scheduling"));

// DAG subtrees must have at least this many nodes.
static const unsigned MinSubtreeSize = 8;

// Pin the vtables to this file.
void MachineSchedStrategy::anchor() {}

void ScheduleDAGMutation::anchor() {}

//===----------------------------------------------------------------------===//
// Machine Instruction Scheduling Pass and Registry
//===----------------------------------------------------------------------===//

MachineSchedContext::MachineSchedContext() {
  RegClassInfo = new RegisterClassInfo();
}

MachineSchedContext::~MachineSchedContext() {
  delete RegClassInfo;
}

namespace {

/// Base class for a machine scheduler class that can run at any point.
class MachineSchedulerBase : public MachineSchedContext,
                             public MachineFunctionPass {
public:
  MachineSchedulerBase(char &ID): MachineFunctionPass(ID) {}

  void print(raw_ostream &O, const Module* = nullptr) const override;

protected:
  void scheduleRegions(ScheduleDAGInstrs &Scheduler, bool FixKillFlags);
};

/// MachineScheduler runs after coalescing and before register allocation.
class MachineScheduler : public MachineSchedulerBase {
public:
  MachineScheduler();

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  bool runOnMachineFunction(MachineFunction&) override;

  static char ID; // Class identification, replacement for typeinfo

protected:
  ScheduleDAGInstrs *createMachineScheduler();
};

/// PostMachineScheduler runs after shortly before code emission.
class PostMachineScheduler : public MachineSchedulerBase {
public:
  PostMachineScheduler();

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  bool runOnMachineFunction(MachineFunction&) override;

  static char ID; // Class identification, replacement for typeinfo

protected:
  ScheduleDAGInstrs *createPostMachineScheduler();
};

} // end anonymous namespace

char MachineScheduler::ID = 0;

char &llvm::MachineSchedulerID = MachineScheduler::ID;

INITIALIZE_PASS_BEGIN(MachineScheduler, DEBUG_TYPE,
                      "Machine Instruction Scheduler", false, false)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfo)
INITIALIZE_PASS_DEPENDENCY(SlotIndexes)
INITIALIZE_PASS_DEPENDENCY(LiveIntervals)
INITIALIZE_PASS_END(MachineScheduler, DEBUG_TYPE,
                    "Machine Instruction Scheduler", false, false)

MachineScheduler::MachineScheduler() : MachineSchedulerBase(ID) {
  initializeMachineSchedulerPass(*PassRegistry::getPassRegistry());
}

void MachineScheduler::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
  AU.addRequiredID(MachineDominatorsID);
  AU.addRequired<MachineLoopInfo>();
  AU.addRequired<AAResultsWrapperPass>();
  AU.addRequired<TargetPassConfig>();
  AU.addRequired<SlotIndexes>();
  AU.addPreserved<SlotIndexes>();
  AU.addRequired<LiveIntervals>();
  AU.addPreserved<LiveIntervals>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

char PostMachineScheduler::ID = 0;

char &llvm::PostMachineSchedulerID = PostMachineScheduler::ID;

INITIALIZE_PASS(PostMachineScheduler, "postmisched",
                "PostRA Machine Instruction Scheduler", false, false)

PostMachineScheduler::PostMachineScheduler() : MachineSchedulerBase(ID) {
  initializePostMachineSchedulerPass(*PassRegistry::getPassRegistry());
}

void PostMachineScheduler::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
  AU.addRequiredID(MachineDominatorsID);
  AU.addRequired<MachineLoopInfo>();
  AU.addRequired<TargetPassConfig>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

MachinePassRegistry<MachineSchedRegistry::ScheduleDAGCtor>
    MachineSchedRegistry::Registry;

/// A dummy default scheduler factory indicates whether the scheduler
/// is overridden on the command line.
static ScheduleDAGInstrs *useDefaultMachineSched(MachineSchedContext *C) {
  return nullptr;
}

/// MachineSchedOpt allows command line selection of the scheduler.
static cl::opt<MachineSchedRegistry::ScheduleDAGCtor, false,
               RegisterPassParser<MachineSchedRegistry>>
MachineSchedOpt("misched",
                cl::init(&useDefaultMachineSched), cl::Hidden,
                cl::desc("Machine instruction scheduler to use"));

static MachineSchedRegistry
DefaultSchedRegistry("default", "Use the target's default scheduler choice.",
                     useDefaultMachineSched);

static cl::opt<bool> EnableMachineSched(
    "enable-misched",
    cl::desc("Enable the machine instruction scheduling pass."), cl::init(true),
    cl::Hidden);

static cl::opt<bool> EnablePostRAMachineSched(
    "enable-post-misched",
    cl::desc("Enable the post-ra machine instruction scheduling pass."),
    cl::init(true), cl::Hidden);

/// Decrement this iterator until reaching the top or a non-debug instr.
static MachineBasicBlock::const_iterator
priorNonDebug(MachineBasicBlock::const_iterator I,
              MachineBasicBlock::const_iterator Beg) {
  assert(I != Beg && "reached the top of the region, cannot decrement");
  while (--I != Beg) {
    if (!I->isDebugInstr())
      break;
  }
  return I;
}

/// Non-const version.
static MachineBasicBlock::iterator
priorNonDebug(MachineBasicBlock::iterator I,
              MachineBasicBlock::const_iterator Beg) {
  return priorNonDebug(MachineBasicBlock::const_iterator(I), Beg)
      .getNonConstIterator();
}

/// If this iterator is a debug value, increment until reaching the End or a
/// non-debug instruction.
static MachineBasicBlock::const_iterator
nextIfDebug(MachineBasicBlock::const_iterator I,
            MachineBasicBlock::const_iterator End) {
  for(; I != End; ++I) {
    if (!I->isDebugInstr())
      break;
  }
  return I;
}

/// Non-const version.
static MachineBasicBlock::iterator
nextIfDebug(MachineBasicBlock::iterator I,
            MachineBasicBlock::const_iterator End) {
  return nextIfDebug(MachineBasicBlock::const_iterator(I), End)
      .getNonConstIterator();
}

/// Instantiate a ScheduleDAGInstrs that will be owned by the caller.
ScheduleDAGInstrs *MachineScheduler::createMachineScheduler() {
  // Select the scheduler, or set the default.
  MachineSchedRegistry::ScheduleDAGCtor Ctor = MachineSchedOpt;
  if (Ctor != useDefaultMachineSched)
    return Ctor(this);

  // Get the default scheduler set by the target for this function.
  ScheduleDAGInstrs *Scheduler = PassConfig->createMachineScheduler(this);
  if (Scheduler)
    return Scheduler;

  // Default to GenericScheduler.
  return createGenericSchedLive(this);
}

/// Instantiate a ScheduleDAGInstrs for PostRA scheduling that will be owned by
/// the caller. We don't have a command line option to override the postRA
/// scheduler. The Target must configure it.
ScheduleDAGInstrs *PostMachineScheduler::createPostMachineScheduler() {
  // Get the postRA scheduler set by the target for this function.
  ScheduleDAGInstrs *Scheduler = PassConfig->createPostMachineScheduler(this);
  if (Scheduler)
    return Scheduler;

  // Default to GenericScheduler.
  return createGenericSchedPostRA(this);
}

/// Top-level MachineScheduler pass driver.
///
/// Visit blocks in function order. Divide each block into scheduling regions
/// and visit them bottom-up. Visiting regions bottom-up is not required, but is
/// consistent with the DAG builder, which traverses the interior of the
/// scheduling regions bottom-up.
///
/// This design avoids exposing scheduling boundaries to the DAG builder,
/// simplifying the DAG builder's support for "special" target instructions.
/// At the same time the design allows target schedulers to operate across
/// scheduling boundaries, for example to bundle the boundary instructions
/// without reordering them. This creates complexity, because the target
/// scheduler must update the RegionBegin and RegionEnd positions cached by
/// ScheduleDAGInstrs whenever adding or removing instructions. A much simpler
/// design would be to split blocks at scheduling boundaries, but LLVM has a
/// general bias against block splitting purely for implementation simplicity.
bool MachineScheduler::runOnMachineFunction(MachineFunction &mf) {
  if (skipFunction(mf.getFunction()))
    return false;

  if (EnableMachineSched.getNumOccurrences()) {
    if (!EnableMachineSched)
      return false;
  } else if (!mf.getSubtarget().enableMachineScheduler())
    return false;

  LLVM_DEBUG(dbgs() << "Before MISched:\n"; mf.print(dbgs()));

  // Initialize the context of the pass.
  MF = &mf;
  MLI = &getAnalysis<MachineLoopInfo>();
  MDT = &getAnalysis<MachineDominatorTree>();
  PassConfig = &getAnalysis<TargetPassConfig>();
  AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();

  LIS = &getAnalysis<LiveIntervals>();

  if (VerifyScheduling) {
    LLVM_DEBUG(LIS->dump());
    MF->verify(this, "Before machine scheduling.");
  }
  RegClassInfo->runOnMachineFunction(*MF);

  // Instantiate the selected scheduler for this target, function, and
  // optimization level.
  std::unique_ptr<ScheduleDAGInstrs> Scheduler(createMachineScheduler());
  scheduleRegions(*Scheduler, false);

  LLVM_DEBUG(LIS->dump());
  if (VerifyScheduling)
    MF->verify(this, "After machine scheduling.");
  return true;
}

bool PostMachineScheduler::runOnMachineFunction(MachineFunction &mf) {
  if (skipFunction(mf.getFunction()))
    return false;

  if (EnablePostRAMachineSched.getNumOccurrences()) {
    if (!EnablePostRAMachineSched)
      return false;
  } else if (!mf.getSubtarget().enablePostRAScheduler()) {
    LLVM_DEBUG(dbgs() << "Subtarget disables post-MI-sched.\n");
    return false;
  }
  LLVM_DEBUG(dbgs() << "Before post-MI-sched:\n"; mf.print(dbgs()));

  // Initialize the context of the pass.
  MF = &mf;
  MLI = &getAnalysis<MachineLoopInfo>();
  PassConfig = &getAnalysis<TargetPassConfig>();

  if (VerifyScheduling)
    MF->verify(this, "Before post machine scheduling.");

  // Instantiate the selected scheduler for this target, function, and
  // optimization level.
  std::unique_ptr<ScheduleDAGInstrs> Scheduler(createPostMachineScheduler());
  scheduleRegions(*Scheduler, true);

  if (VerifyScheduling)
    MF->verify(this, "After post machine scheduling.");
  return true;
}

/// Return true of the given instruction should not be included in a scheduling
/// region.
///
/// MachineScheduler does not currently support scheduling across calls. To
/// handle calls, the DAG builder needs to be modified to create register
/// anti/output dependencies on the registers clobbered by the call's regmask
/// operand. In PreRA scheduling, the stack pointer adjustment already prevents
/// scheduling across calls. In PostRA scheduling, we need the isCall to enforce
/// the boundary, but there would be no benefit to postRA scheduling across
/// calls this late anyway.
static bool isSchedBoundary(MachineBasicBlock::iterator MI,
                            MachineBasicBlock *MBB,
                            MachineFunction *MF,
                            const TargetInstrInfo *TII) {
  return MI->isCall() || TII->isSchedulingBoundary(*MI, MBB, *MF);
}

/// A region of an MBB for scheduling.
namespace {
struct SchedRegion {
  /// RegionBegin is the first instruction in the scheduling region, and
  /// RegionEnd is either MBB->end() or the scheduling boundary after the
  /// last instruction in the scheduling region. These iterators cannot refer
  /// to instructions outside of the identified scheduling region because
  /// those may be reordered before scheduling this region.
  MachineBasicBlock::iterator RegionBegin;
  MachineBasicBlock::iterator RegionEnd;
  unsigned NumRegionInstrs;

  SchedRegion(MachineBasicBlock::iterator B, MachineBasicBlock::iterator E,
              unsigned N) :
    RegionBegin(B), RegionEnd(E), NumRegionInstrs(N) {}
};
} // end anonymous namespace

using MBBRegionsVector = SmallVector<SchedRegion, 16>;

static void
getSchedRegions(MachineBasicBlock *MBB,
                MBBRegionsVector &Regions,
                bool RegionsTopDown) {
  MachineFunction *MF = MBB->getParent();
  const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();

  MachineBasicBlock::iterator I = nullptr;
  for(MachineBasicBlock::iterator RegionEnd = MBB->end();
      RegionEnd != MBB->begin(); RegionEnd = I) {

    // Avoid decrementing RegionEnd for blocks with no terminator.
    if (RegionEnd != MBB->end() ||
        isSchedBoundary(&*std::prev(RegionEnd), &*MBB, MF, TII)) {
      --RegionEnd;
    }

    // The next region starts above the previous region. Look backward in the
    // instruction stream until we find the nearest boundary.
    unsigned NumRegionInstrs = 0;
    I = RegionEnd;
    for (;I != MBB->begin(); --I) {
      MachineInstr &MI = *std::prev(I);
      if (isSchedBoundary(&MI, &*MBB, MF, TII))
        break;
      if (!MI.isDebugInstr())
        // MBB::size() uses instr_iterator to count. Here we need a bundle to
        // count as a single instruction.
        ++NumRegionInstrs;
    }

    Regions.push_back(SchedRegion(I, RegionEnd, NumRegionInstrs));
  }

  if (RegionsTopDown)
    std::reverse(Regions.begin(), Regions.end());
}

/// Main driver for both MachineScheduler and PostMachineScheduler.
void MachineSchedulerBase::scheduleRegions(ScheduleDAGInstrs &Scheduler,
                                           bool FixKillFlags) {
  // Visit all machine basic blocks.
  //
  // TODO: Visit blocks in global postorder or postorder within the bottom-up
  // loop tree. Then we can optionally compute global RegPressure.
  for (MachineFunction::iterator MBB = MF->begin(), MBBEnd = MF->end();
       MBB != MBBEnd; ++MBB) {

    Scheduler.startBlock(&*MBB);

#ifndef NDEBUG
    if (SchedOnlyFunc.getNumOccurrences() && SchedOnlyFunc != MF->getName())
      continue;
    if (SchedOnlyBlock.getNumOccurrences()
        && (int)SchedOnlyBlock != MBB->getNumber())
      continue;
#endif

    // Break the block into scheduling regions [I, RegionEnd). RegionEnd
    // points to the scheduling boundary at the bottom of the region. The DAG
    // does not include RegionEnd, but the region does (i.e. the next
    // RegionEnd is above the previous RegionBegin). If the current block has
    // no terminator then RegionEnd == MBB->end() for the bottom region.
    //
    // All the regions of MBB are first found and stored in MBBRegions, which
    // will be processed (MBB) top-down if initialized with true.
    //
    // The Scheduler may insert instructions during either schedule() or
    // exitRegion(), even for empty regions. So the local iterators 'I' and
    // 'RegionEnd' are invalid across these calls. Instructions must not be
    // added to other regions than the current one without updating MBBRegions.

    MBBRegionsVector MBBRegions;
    getSchedRegions(&*MBB, MBBRegions, Scheduler.doMBBSchedRegionsTopDown());
    for (MBBRegionsVector::iterator R = MBBRegions.begin();
         R != MBBRegions.end(); ++R) {
      MachineBasicBlock::iterator I = R->RegionBegin;
      MachineBasicBlock::iterator RegionEnd = R->RegionEnd;
      unsigned NumRegionInstrs = R->NumRegionInstrs;

      // Notify the scheduler of the region, even if we may skip scheduling
      // it. Perhaps it still needs to be bundled.
      Scheduler.enterRegion(&*MBB, I, RegionEnd, NumRegionInstrs);

      // Skip empty scheduling regions (0 or 1 schedulable instructions).
      if (I == RegionEnd || I == std::prev(RegionEnd)) {
        // Close the current region. Bundle the terminator if needed.
        // This invalidates 'RegionEnd' and 'I'.
        Scheduler.exitRegion();
        continue;
      }
      LLVM_DEBUG(dbgs() << "********** MI Scheduling **********\n");
      LLVM_DEBUG(dbgs() << MF->getName() << ":" << printMBBReference(*MBB)
                        << " " << MBB->getName() << "\n  From: " << *I
                        << "    To: ";
                 if (RegionEnd != MBB->end()) dbgs() << *RegionEnd;
                 else dbgs() << "End";
                 dbgs() << " RegionInstrs: " << NumRegionInstrs << '\n');
      if (DumpCriticalPathLength) {
        errs() << MF->getName();
        errs() << ":%bb. " << MBB->getNumber();
        errs() << " " << MBB->getName() << " \n";
      }

      // Schedule a region: possibly reorder instructions.
      // This invalidates the original region iterators.
      Scheduler.schedule();

      // Close the current region.
      Scheduler.exitRegion();
    }
    Scheduler.finishBlock();
    // FIXME: Ideally, no further passes should rely on kill flags. However,
    // thumb2 size reduction is currently an exception, so the PostMIScheduler
    // needs to do this.
    if (FixKillFlags)
      Scheduler.fixupKills(*MBB);
  }
  Scheduler.finalizeSchedule();
}

void MachineSchedulerBase::print(raw_ostream &O, const Module* m) const {
  // unimplemented
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void ReadyQueue::dump() const {
  dbgs() << "Queue " << Name << ": ";
  for (const SUnit *SU : Queue)
    dbgs() << SU->NodeNum << " ";
  dbgs() << "\n";
}
#endif

//===----------------------------------------------------------------------===//
// ScheduleDAGMI - Basic machine instruction scheduling. This is
// independent of PreRA/PostRA scheduling and involves no extra book-keeping for
// virtual registers.
// ===----------------------------------------------------------------------===/

// Provide a vtable anchor.
ScheduleDAGMI::~ScheduleDAGMI() = default;

bool ScheduleDAGMI::canAddEdge(SUnit *SuccSU, SUnit *PredSU) {
  return SuccSU == &ExitSU || !Topo.IsReachable(PredSU, SuccSU);
}

bool ScheduleDAGMI::addEdge(SUnit *SuccSU, const SDep &PredDep) {
  if (SuccSU != &ExitSU) {
    // Do not use WillCreateCycle, it assumes SD scheduling.
    // If Pred is reachable from Succ, then the edge creates a cycle.
    if (Topo.IsReachable(PredDep.getSUnit(), SuccSU))
      return false;
    Topo.AddPred(SuccSU, PredDep.getSUnit());
  }
  SuccSU->addPred(PredDep, /*Required=*/!PredDep.isArtificial());
  // Return true regardless of whether a new edge needed to be inserted.
  return true;
}

/// ReleaseSucc - Decrement the NumPredsLeft count of a successor. When
/// NumPredsLeft reaches zero, release the successor node.
///
/// FIXME: Adjust SuccSU height based on MinLatency.
void ScheduleDAGMI::releaseSucc(SUnit *SU, SDep *SuccEdge) {
  SUnit *SuccSU = SuccEdge->getSUnit();

  if (SuccEdge->isWeak()) {
    --SuccSU->WeakPredsLeft;
    if (SuccEdge->isCluster())
      NextClusterSucc = SuccSU;
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
  // SU->TopReadyCycle was set to CurrCycle when it was scheduled. However,
  // CurrCycle may have advanced since then.
  if (SuccSU->TopReadyCycle < SU->TopReadyCycle + SuccEdge->getLatency())
    SuccSU->TopReadyCycle = SU->TopReadyCycle + SuccEdge->getLatency();

  --SuccSU->NumPredsLeft;
  if (SuccSU->NumPredsLeft == 0 && SuccSU != &ExitSU)
    SchedImpl->releaseTopNode(SuccSU);
}

/// releaseSuccessors - Call releaseSucc on each of SU's successors.
void ScheduleDAGMI::releaseSuccessors(SUnit *SU) {
  for (SDep &Succ : SU->Succs)
    releaseSucc(SU, &Succ);
}

/// ReleasePred - Decrement the NumSuccsLeft count of a predecessor. When
/// NumSuccsLeft reaches zero, release the predecessor node.
///
/// FIXME: Adjust PredSU height based on MinLatency.
void ScheduleDAGMI::releasePred(SUnit *SU, SDep *PredEdge) {
  SUnit *PredSU = PredEdge->getSUnit();

  if (PredEdge->isWeak()) {
    --PredSU->WeakSuccsLeft;
    if (PredEdge->isCluster())
      NextClusterPred = PredSU;
    return;
  }
#ifndef NDEBUG
  if (PredSU->NumSuccsLeft == 0) {
    dbgs() << "*** Scheduling failed! ***\n";
    dumpNode(*PredSU);
    dbgs() << " has been released too many times!\n";
    llvm_unreachable(nullptr);
  }
#endif
  // SU->BotReadyCycle was set to CurrCycle when it was scheduled. However,
  // CurrCycle may have advanced since then.
  if (PredSU->BotReadyCycle < SU->BotReadyCycle + PredEdge->getLatency())
    PredSU->BotReadyCycle = SU->BotReadyCycle + PredEdge->getLatency();

  --PredSU->NumSuccsLeft;
  if (PredSU->NumSuccsLeft == 0 && PredSU != &EntrySU)
    SchedImpl->releaseBottomNode(PredSU);
}

/// releasePredecessors - Call releasePred on each of SU's predecessors.
void ScheduleDAGMI::releasePredecessors(SUnit *SU) {
  for (SDep &Pred : SU->Preds)
    releasePred(SU, &Pred);
}

void ScheduleDAGMI::startBlock(MachineBasicBlock *bb) {
  ScheduleDAGInstrs::startBlock(bb);
  SchedImpl->enterMBB(bb);
}

void ScheduleDAGMI::finishBlock() {
  SchedImpl->leaveMBB();
  ScheduleDAGInstrs::finishBlock();
}

/// enterRegion - Called back from MachineScheduler::runOnMachineFunction after
/// crossing a scheduling boundary. [begin, end) includes all instructions in
/// the region, including the boundary itself and single-instruction regions
/// that don't get scheduled.
void ScheduleDAGMI::enterRegion(MachineBasicBlock *bb,
                                     MachineBasicBlock::iterator begin,
                                     MachineBasicBlock::iterator end,
                                     unsigned regioninstrs)
{
  ScheduleDAGInstrs::enterRegion(bb, begin, end, regioninstrs);

  SchedImpl->initPolicy(begin, end, regioninstrs);
}

/// This is normally called from the main scheduler loop but may also be invoked
/// by the scheduling strategy to perform additional code motion.
void ScheduleDAGMI::moveInstruction(
  MachineInstr *MI, MachineBasicBlock::iterator InsertPos) {
  // Advance RegionBegin if the first instruction moves down.
  if (&*RegionBegin == MI)
    ++RegionBegin;

  // Update the instruction stream.
  BB->splice(InsertPos, BB, MI);

  // Update LiveIntervals
  if (LIS)
    LIS->handleMove(*MI, /*UpdateFlags=*/true);

  // Recede RegionBegin if an instruction moves above the first.
  if (RegionBegin == InsertPos)
    RegionBegin = MI;
}

bool ScheduleDAGMI::checkSchedLimit() {
#ifndef NDEBUG
  if (NumInstrsScheduled == MISchedCutoff && MISchedCutoff != ~0U) {
    CurrentTop = CurrentBottom;
    return false;
  }
  ++NumInstrsScheduled;
#endif
  return true;
}

/// Per-region scheduling driver, called back from
/// MachineScheduler::runOnMachineFunction. This is a simplified driver that
/// does not consider liveness or register pressure. It is useful for PostRA
/// scheduling and potentially other custom schedulers.
void ScheduleDAGMI::schedule() {
  LLVM_DEBUG(dbgs() << "ScheduleDAGMI::schedule starting\n");
  LLVM_DEBUG(SchedImpl->dumpPolicy());

  // Build the DAG.
  buildSchedGraph(AA);

  Topo.InitDAGTopologicalSorting();

  postprocessDAG();

  SmallVector<SUnit*, 8> TopRoots, BotRoots;
  findRootsAndBiasEdges(TopRoots, BotRoots);

  LLVM_DEBUG(dump());
  if (PrintDAGs) dump();
  if (ViewMISchedDAGs) viewGraph();

  // Initialize the strategy before modifying the DAG.
  // This may initialize a DFSResult to be used for queue priority.
  SchedImpl->initialize(this);

  // Initialize ready queues now that the DAG and priority data are finalized.
  initQueues(TopRoots, BotRoots);

  bool IsTopNode = false;
  while (true) {
    LLVM_DEBUG(dbgs() << "** ScheduleDAGMI::schedule picking next node\n");
    SUnit *SU = SchedImpl->pickNode(IsTopNode);
    if (!SU) break;

    assert(!SU->isScheduled && "Node already scheduled");
    if (!checkSchedLimit())
      break;

    MachineInstr *MI = SU->getInstr();
    if (IsTopNode) {
      assert(SU->isTopReady() && "node still has unscheduled dependencies");
      if (&*CurrentTop == MI)
        CurrentTop = nextIfDebug(++CurrentTop, CurrentBottom);
      else
        moveInstruction(MI, CurrentTop);
    } else {
      assert(SU->isBottomReady() && "node still has unscheduled dependencies");
      MachineBasicBlock::iterator priorII =
        priorNonDebug(CurrentBottom, CurrentTop);
      if (&*priorII == MI)
        CurrentBottom = priorII;
      else {
        if (&*CurrentTop == MI)
          CurrentTop = nextIfDebug(++CurrentTop, priorII);
        moveInstruction(MI, CurrentBottom);
        CurrentBottom = MI;
      }
    }
    // Notify the scheduling strategy before updating the DAG.
    // This sets the scheduled node's ReadyCycle to CurrCycle. When updateQueues
    // runs, it can then use the accurate ReadyCycle time to determine whether
    // newly released nodes can move to the readyQ.
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

/// Apply each ScheduleDAGMutation step in order.
void ScheduleDAGMI::postprocessDAG() {
  for (auto &m : Mutations)
    m->apply(this);
}

void ScheduleDAGMI::
findRootsAndBiasEdges(SmallVectorImpl<SUnit*> &TopRoots,
                      SmallVectorImpl<SUnit*> &BotRoots) {
  for (SUnit &SU : SUnits) {
    assert(!SU.isBoundaryNode() && "Boundary node should not be in SUnits");

    // Order predecessors so DFSResult follows the critical path.
    SU.biasCriticalPath();

    // A SUnit is ready to top schedule if it has no predecessors.
    if (!SU.NumPredsLeft)
      TopRoots.push_back(&SU);
    // A SUnit is ready to bottom schedule if it has no successors.
    if (!SU.NumSuccsLeft)
      BotRoots.push_back(&SU);
  }
  ExitSU.biasCriticalPath();
}

/// Identify DAG roots and setup scheduler queues.
void ScheduleDAGMI::initQueues(ArrayRef<SUnit*> TopRoots,
                               ArrayRef<SUnit*> BotRoots) {
  NextClusterSucc = nullptr;
  NextClusterPred = nullptr;

  // Release all DAG roots for scheduling, not including EntrySU/ExitSU.
  //
  // Nodes with unreleased weak edges can still be roots.
  // Release top roots in forward order.
  for (SUnit *SU : TopRoots)
    SchedImpl->releaseTopNode(SU);

  // Release bottom roots in reverse order so the higher priority nodes appear
  // first. This is more natural and slightly more efficient.
  for (SmallVectorImpl<SUnit*>::const_reverse_iterator
         I = BotRoots.rbegin(), E = BotRoots.rend(); I != E; ++I) {
    SchedImpl->releaseBottomNode(*I);
  }

  releaseSuccessors(&EntrySU);
  releasePredecessors(&ExitSU);

  SchedImpl->registerRoots();

  // Advance past initial DebugValues.
  CurrentTop = nextIfDebug(RegionBegin, RegionEnd);
  CurrentBottom = RegionEnd;
}

/// Update scheduler queues after scheduling an instruction.
void ScheduleDAGMI::updateQueues(SUnit *SU, bool IsTopNode) {
  // Release dependent instructions for scheduling.
  if (IsTopNode)
    releaseSuccessors(SU);
  else
    releasePredecessors(SU);

  SU->isScheduled = true;
}

/// Reinsert any remaining debug_values, just like the PostRA scheduler.
void ScheduleDAGMI::placeDebugValues() {
  // If first instruction was a DBG_VALUE then put it back.
  if (FirstDbgValue) {
    BB->splice(RegionBegin, BB, FirstDbgValue);
    RegionBegin = FirstDbgValue;
  }

  for (std::vector<std::pair<MachineInstr *, MachineInstr *>>::iterator
         DI = DbgValues.end(), DE = DbgValues.begin(); DI != DE; --DI) {
    std::pair<MachineInstr *, MachineInstr *> P = *std::prev(DI);
    MachineInstr *DbgValue = P.first;
    MachineBasicBlock::iterator OrigPrevMI = P.second;
    if (&*RegionBegin == DbgValue)
      ++RegionBegin;
    BB->splice(++OrigPrevMI, BB, DbgValue);
    if (OrigPrevMI == std::prev(RegionEnd))
      RegionEnd = DbgValue;
  }
  DbgValues.clear();
  FirstDbgValue = nullptr;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void ScheduleDAGMI::dumpSchedule() const {
  for (MachineBasicBlock::iterator MI = begin(), ME = end(); MI != ME; ++MI) {
    if (SUnit *SU = getSUnit(&(*MI)))
      dumpNode(*SU);
    else
      dbgs() << "Missing SUnit\n";
  }
}
#endif

//===----------------------------------------------------------------------===//
// ScheduleDAGMILive - Base class for MachineInstr scheduling with LiveIntervals
// preservation.
//===----------------------------------------------------------------------===//

ScheduleDAGMILive::~ScheduleDAGMILive() {
  delete DFSResult;
}

void ScheduleDAGMILive::collectVRegUses(SUnit &SU) {
  const MachineInstr &MI = *SU.getInstr();
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg())
      continue;
    if (!MO.readsReg())
      continue;
    if (TrackLaneMasks && !MO.isUse())
      continue;

    unsigned Reg = MO.getReg();
    if (!TargetRegisterInfo::isVirtualRegister(Reg))
      continue;

    // Ignore re-defs.
    if (TrackLaneMasks) {
      bool FoundDef = false;
      for (const MachineOperand &MO2 : MI.operands()) {
        if (MO2.isReg() && MO2.isDef() && MO2.getReg() == Reg && !MO2.isDead()) {
          FoundDef = true;
          break;
        }
      }
      if (FoundDef)
        continue;
    }

    // Record this local VReg use.
    VReg2SUnitMultiMap::iterator UI = VRegUses.find(Reg);
    for (; UI != VRegUses.end(); ++UI) {
      if (UI->SU == &SU)
        break;
    }
    if (UI == VRegUses.end())
      VRegUses.insert(VReg2SUnit(Reg, LaneBitmask::getNone(), &SU));
  }
}

/// enterRegion - Called back from MachineScheduler::runOnMachineFunction after
/// crossing a scheduling boundary. [begin, end) includes all instructions in
/// the region, including the boundary itself and single-instruction regions
/// that don't get scheduled.
void ScheduleDAGMILive::enterRegion(MachineBasicBlock *bb,
                                MachineBasicBlock::iterator begin,
                                MachineBasicBlock::iterator end,
                                unsigned regioninstrs)
{
  // ScheduleDAGMI initializes SchedImpl's per-region policy.
  ScheduleDAGMI::enterRegion(bb, begin, end, regioninstrs);

  // For convenience remember the end of the liveness region.
  LiveRegionEnd = (RegionEnd == bb->end()) ? RegionEnd : std::next(RegionEnd);

  SUPressureDiffs.clear();

  ShouldTrackPressure = SchedImpl->shouldTrackPressure();
  ShouldTrackLaneMasks = SchedImpl->shouldTrackLaneMasks();

  assert((!ShouldTrackLaneMasks || ShouldTrackPressure) &&
         "ShouldTrackLaneMasks requires ShouldTrackPressure");
}

// Setup the register pressure trackers for the top scheduled top and bottom
// scheduled regions.
void ScheduleDAGMILive::initRegPressure() {
  VRegUses.clear();
  VRegUses.setUniverse(MRI.getNumVirtRegs());
  for (SUnit &SU : SUnits)
    collectVRegUses(SU);

  TopRPTracker.init(&MF, RegClassInfo, LIS, BB, RegionBegin,
                    ShouldTrackLaneMasks, false);
  BotRPTracker.init(&MF, RegClassInfo, LIS, BB, LiveRegionEnd,
                    ShouldTrackLaneMasks, false);

  // Close the RPTracker to finalize live ins.
  RPTracker.closeRegion();

  LLVM_DEBUG(RPTracker.dump());

  // Initialize the live ins and live outs.
  TopRPTracker.addLiveRegs(RPTracker.getPressure().LiveInRegs);
  BotRPTracker.addLiveRegs(RPTracker.getPressure().LiveOutRegs);

  // Close one end of the tracker so we can call
  // getMaxUpward/DownwardPressureDelta before advancing across any
  // instructions. This converts currently live regs into live ins/outs.
  TopRPTracker.closeTop();
  BotRPTracker.closeBottom();

  BotRPTracker.initLiveThru(RPTracker);
  if (!BotRPTracker.getLiveThru().empty()) {
    TopRPTracker.initLiveThru(BotRPTracker.getLiveThru());
    LLVM_DEBUG(dbgs() << "Live Thru: ";
               dumpRegSetPressure(BotRPTracker.getLiveThru(), TRI));
  };

  // For each live out vreg reduce the pressure change associated with other
  // uses of the same vreg below the live-out reaching def.
  updatePressureDiffs(RPTracker.getPressure().LiveOutRegs);

  // Account for liveness generated by the region boundary.
  if (LiveRegionEnd != RegionEnd) {
    SmallVector<RegisterMaskPair, 8> LiveUses;
    BotRPTracker.recede(&LiveUses);
    updatePressureDiffs(LiveUses);
  }

  LLVM_DEBUG(dbgs() << "Top Pressure:\n";
             dumpRegSetPressure(TopRPTracker.getRegSetPressureAtPos(), TRI);
             dbgs() << "Bottom Pressure:\n";
             dumpRegSetPressure(BotRPTracker.getRegSetPressureAtPos(), TRI););

  assert((BotRPTracker.getPos() == RegionEnd ||
          (RegionEnd->isDebugInstr() &&
           BotRPTracker.getPos() == priorNonDebug(RegionEnd, RegionBegin))) &&
         "Can't find the region bottom");

  // Cache the list of excess pressure sets in this region. This will also track
  // the max pressure in the scheduled code for these sets.
  RegionCriticalPSets.clear();
  const std::vector<unsigned> &RegionPressure =
    RPTracker.getPressure().MaxSetPressure;
  for (unsigned i = 0, e = RegionPressure.size(); i < e; ++i) {
    unsigned Limit = RegClassInfo->getRegPressureSetLimit(i);
    if (RegionPressure[i] > Limit) {
      LLVM_DEBUG(dbgs() << TRI->getRegPressureSetName(i) << " Limit " << Limit
                        << " Actual " << RegionPressure[i] << "\n");
      RegionCriticalPSets.push_back(PressureChange(i));
    }
  }
  LLVM_DEBUG(dbgs() << "Excess PSets: ";
             for (const PressureChange &RCPS
                  : RegionCriticalPSets) dbgs()
             << TRI->getRegPressureSetName(RCPS.getPSet()) << " ";
             dbgs() << "\n");
}

void ScheduleDAGMILive::
updateScheduledPressure(const SUnit *SU,
                        const std::vector<unsigned> &NewMaxPressure) {
  const PressureDiff &PDiff = getPressureDiff(SU);
  unsigned CritIdx = 0, CritEnd = RegionCriticalPSets.size();
  for (const PressureChange &PC : PDiff) {
    if (!PC.isValid())
      break;
    unsigned ID = PC.getPSet();
    while (CritIdx != CritEnd && RegionCriticalPSets[CritIdx].getPSet() < ID)
      ++CritIdx;
    if (CritIdx != CritEnd && RegionCriticalPSets[CritIdx].getPSet() == ID) {
      if ((int)NewMaxPressure[ID] > RegionCriticalPSets[CritIdx].getUnitInc()
          && NewMaxPressure[ID] <= (unsigned)std::numeric_limits<int16_t>::max())
        RegionCriticalPSets[CritIdx].setUnitInc(NewMaxPressure[ID]);
    }
    unsigned Limit = RegClassInfo->getRegPressureSetLimit(ID);
    if (NewMaxPressure[ID] >= Limit - 2) {
      LLVM_DEBUG(dbgs() << "  " << TRI->getRegPressureSetName(ID) << ": "
                        << NewMaxPressure[ID]
                        << ((NewMaxPressure[ID] > Limit) ? " > " : " <= ")
                        << Limit << "(+ " << BotRPTracker.getLiveThru()[ID]
                        << " livethru)\n");
    }
  }
}

/// Update the PressureDiff array for liveness after scheduling this
/// instruction.
void ScheduleDAGMILive::updatePressureDiffs(
    ArrayRef<RegisterMaskPair> LiveUses) {
  for (const RegisterMaskPair &P : LiveUses) {
    unsigned Reg = P.RegUnit;
    /// FIXME: Currently assuming single-use physregs.
    if (!TRI->isVirtualRegister(Reg))
      continue;

    if (ShouldTrackLaneMasks) {
      // If the register has just become live then other uses won't change
      // this fact anymore => decrement pressure.
      // If the register has just become dead then other uses make it come
      // back to life => increment pressure.
      bool Decrement = P.LaneMask.any();

      for (const VReg2SUnit &V2SU
           : make_range(VRegUses.find(Reg), VRegUses.end())) {
        SUnit &SU = *V2SU.SU;
        if (SU.isScheduled || &SU == &ExitSU)
          continue;

        PressureDiff &PDiff = getPressureDiff(&SU);
        PDiff.addPressureChange(Reg, Decrement, &MRI);
        LLVM_DEBUG(dbgs() << "  UpdateRegP: SU(" << SU.NodeNum << ") "
                          << printReg(Reg, TRI) << ':'
                          << PrintLaneMask(P.LaneMask) << ' ' << *SU.getInstr();
                   dbgs() << "              to "; PDiff.dump(*TRI););
      }
    } else {
      assert(P.LaneMask.any());
      LLVM_DEBUG(dbgs() << "  LiveReg: " << printVRegOrUnit(Reg, TRI) << "\n");
      // This may be called before CurrentBottom has been initialized. However,
      // BotRPTracker must have a valid position. We want the value live into the
      // instruction or live out of the block, so ask for the previous
      // instruction's live-out.
      const LiveInterval &LI = LIS->getInterval(Reg);
      VNInfo *VNI;
      MachineBasicBlock::const_iterator I =
        nextIfDebug(BotRPTracker.getPos(), BB->end());
      if (I == BB->end())
        VNI = LI.getVNInfoBefore(LIS->getMBBEndIdx(BB));
      else {
        LiveQueryResult LRQ = LI.Query(LIS->getInstructionIndex(*I));
        VNI = LRQ.valueIn();
      }
      // RegisterPressureTracker guarantees that readsReg is true for LiveUses.
      assert(VNI && "No live value at use.");
      for (const VReg2SUnit &V2SU
           : make_range(VRegUses.find(Reg), VRegUses.end())) {
        SUnit *SU = V2SU.SU;
        // If this use comes before the reaching def, it cannot be a last use,
        // so decrease its pressure change.
        if (!SU->isScheduled && SU != &ExitSU) {
          LiveQueryResult LRQ =
              LI.Query(LIS->getInstructionIndex(*SU->getInstr()));
          if (LRQ.valueIn() == VNI) {
            PressureDiff &PDiff = getPressureDiff(SU);
            PDiff.addPressureChange(Reg, true, &MRI);
            LLVM_DEBUG(dbgs() << "  UpdateRegP: SU(" << SU->NodeNum << ") "
                              << *SU->getInstr();
                       dbgs() << "              to "; PDiff.dump(*TRI););
          }
        }
      }
    }
  }
}

void ScheduleDAGMILive::dump() const {
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  if (EntrySU.getInstr() != nullptr)
    dumpNodeAll(EntrySU);
  for (const SUnit &SU : SUnits) {
    dumpNodeAll(SU);
    if (ShouldTrackPressure) {
      dbgs() << "  Pressure Diff      : ";
      getPressureDiff(&SU).dump(*TRI);
    }
    dbgs() << "  Single Issue       : ";
    if (SchedModel.mustBeginGroup(SU.getInstr()) &&
        SchedModel.mustEndGroup(SU.getInstr()))
      dbgs() << "true;";
    else
      dbgs() << "false;";
    dbgs() << '\n';
  }
  if (ExitSU.getInstr() != nullptr)
    dumpNodeAll(ExitSU);
#endif
}

/// schedule - Called back from MachineScheduler::runOnMachineFunction
/// after setting up the current scheduling region. [RegionBegin, RegionEnd)
/// only includes instructions that have DAG nodes, not scheduling boundaries.
///
/// This is a skeletal driver, with all the functionality pushed into helpers,
/// so that it can be easily extended by experimental schedulers. Generally,
/// implementing MachineSchedStrategy should be sufficient to implement a new
/// scheduling algorithm. However, if a scheduler further subclasses
/// ScheduleDAGMILive then it will want to override this virtual method in order
/// to update any specialized state.
void ScheduleDAGMILive::schedule() {
  LLVM_DEBUG(dbgs() << "ScheduleDAGMILive::schedule starting\n");
  LLVM_DEBUG(SchedImpl->dumpPolicy());
  buildDAGWithRegPressure();

  Topo.InitDAGTopologicalSorting();

  postprocessDAG();

  SmallVector<SUnit*, 8> TopRoots, BotRoots;
  findRootsAndBiasEdges(TopRoots, BotRoots);

  // Initialize the strategy before modifying the DAG.
  // This may initialize a DFSResult to be used for queue priority.
  SchedImpl->initialize(this);

  LLVM_DEBUG(dump());
  if (PrintDAGs) dump();
  if (ViewMISchedDAGs) viewGraph();

  // Initialize ready queues now that the DAG and priority data are finalized.
  initQueues(TopRoots, BotRoots);

  bool IsTopNode = false;
  while (true) {
    LLVM_DEBUG(dbgs() << "** ScheduleDAGMILive::schedule picking next node\n");
    SUnit *SU = SchedImpl->pickNode(IsTopNode);
    if (!SU) break;

    assert(!SU->isScheduled && "Node already scheduled");
    if (!checkSchedLimit())
      break;

    scheduleMI(SU, IsTopNode);

    if (DFSResult) {
      unsigned SubtreeID = DFSResult->getSubtreeID(SU);
      if (!ScheduledTrees.test(SubtreeID)) {
        ScheduledTrees.set(SubtreeID);
        DFSResult->scheduleTree(SubtreeID);
        SchedImpl->scheduleTree(SubtreeID);
      }
    }

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

/// Build the DAG and setup three register pressure trackers.
void ScheduleDAGMILive::buildDAGWithRegPressure() {
  if (!ShouldTrackPressure) {
    RPTracker.reset();
    RegionCriticalPSets.clear();
    buildSchedGraph(AA);
    return;
  }

  // Initialize the register pressure tracker used by buildSchedGraph.
  RPTracker.init(&MF, RegClassInfo, LIS, BB, LiveRegionEnd,
                 ShouldTrackLaneMasks, /*TrackUntiedDefs=*/true);

  // Account for liveness generate by the region boundary.
  if (LiveRegionEnd != RegionEnd)
    RPTracker.recede();

  // Build the DAG, and compute current register pressure.
  buildSchedGraph(AA, &RPTracker, &SUPressureDiffs, LIS, ShouldTrackLaneMasks);

  // Initialize top/bottom trackers after computing region pressure.
  initRegPressure();
}

void ScheduleDAGMILive::computeDFSResult() {
  if (!DFSResult)
    DFSResult = new SchedDFSResult(/*BottomU*/true, MinSubtreeSize);
  DFSResult->clear();
  ScheduledTrees.clear();
  DFSResult->resize(SUnits.size());
  DFSResult->compute(SUnits);
  ScheduledTrees.resize(DFSResult->getNumSubtrees());
}

/// Compute the max cyclic critical path through the DAG. The scheduling DAG
/// only provides the critical path for single block loops. To handle loops that
/// span blocks, we could use the vreg path latencies provided by
/// MachineTraceMetrics instead. However, MachineTraceMetrics is not currently
/// available for use in the scheduler.
///
/// The cyclic path estimation identifies a def-use pair that crosses the back
/// edge and considers the depth and height of the nodes. For example, consider
/// the following instruction sequence where each instruction has unit latency
/// and defines an epomymous virtual register:
///
/// a->b(a,c)->c(b)->d(c)->exit
///
/// The cyclic critical path is a two cycles: b->c->b
/// The acyclic critical path is four cycles: a->b->c->d->exit
/// LiveOutHeight = height(c) = len(c->d->exit) = 2
/// LiveOutDepth = depth(c) + 1 = len(a->b->c) + 1 = 3
/// LiveInHeight = height(b) + 1 = len(b->c->d->exit) + 1 = 4
/// LiveInDepth = depth(b) = len(a->b) = 1
///
/// LiveOutDepth - LiveInDepth = 3 - 1 = 2
/// LiveInHeight - LiveOutHeight = 4 - 2 = 2
/// CyclicCriticalPath = min(2, 2) = 2
///
/// This could be relevant to PostRA scheduling, but is currently implemented
/// assuming LiveIntervals.
unsigned ScheduleDAGMILive::computeCyclicCriticalPath() {
  // This only applies to single block loop.
  if (!BB->isSuccessor(BB))
    return 0;

  unsigned MaxCyclicLatency = 0;
  // Visit each live out vreg def to find def/use pairs that cross iterations.
  for (const RegisterMaskPair &P : RPTracker.getPressure().LiveOutRegs) {
    unsigned Reg = P.RegUnit;
    if (!TRI->isVirtualRegister(Reg))
        continue;
    const LiveInterval &LI = LIS->getInterval(Reg);
    const VNInfo *DefVNI = LI.getVNInfoBefore(LIS->getMBBEndIdx(BB));
    if (!DefVNI)
      continue;

    MachineInstr *DefMI = LIS->getInstructionFromIndex(DefVNI->def);
    const SUnit *DefSU = getSUnit(DefMI);
    if (!DefSU)
      continue;

    unsigned LiveOutHeight = DefSU->getHeight();
    unsigned LiveOutDepth = DefSU->getDepth() + DefSU->Latency;
    // Visit all local users of the vreg def.
    for (const VReg2SUnit &V2SU
         : make_range(VRegUses.find(Reg), VRegUses.end())) {
      SUnit *SU = V2SU.SU;
      if (SU == &ExitSU)
        continue;

      // Only consider uses of the phi.
      LiveQueryResult LRQ = LI.Query(LIS->getInstructionIndex(*SU->getInstr()));
      if (!LRQ.valueIn()->isPHIDef())
        continue;

      // Assume that a path spanning two iterations is a cycle, which could
      // overestimate in strange cases. This allows cyclic latency to be
      // estimated as the minimum slack of the vreg's depth or height.
      unsigned CyclicLatency = 0;
      if (LiveOutDepth > SU->getDepth())
        CyclicLatency = LiveOutDepth - SU->getDepth();

      unsigned LiveInHeight = SU->getHeight() + DefSU->Latency;
      if (LiveInHeight > LiveOutHeight) {
        if (LiveInHeight - LiveOutHeight < CyclicLatency)
          CyclicLatency = LiveInHeight - LiveOutHeight;
      } else
        CyclicLatency = 0;

      LLVM_DEBUG(dbgs() << "Cyclic Path: SU(" << DefSU->NodeNum << ") -> SU("
                        << SU->NodeNum << ") = " << CyclicLatency << "c\n");
      if (CyclicLatency > MaxCyclicLatency)
        MaxCyclicLatency = CyclicLatency;
    }
  }
  LLVM_DEBUG(dbgs() << "Cyclic Critical Path: " << MaxCyclicLatency << "c\n");
  return MaxCyclicLatency;
}

/// Release ExitSU predecessors and setup scheduler queues. Re-position
/// the Top RP tracker in case the region beginning has changed.
void ScheduleDAGMILive::initQueues(ArrayRef<SUnit*> TopRoots,
                                   ArrayRef<SUnit*> BotRoots) {
  ScheduleDAGMI::initQueues(TopRoots, BotRoots);
  if (ShouldTrackPressure) {
    assert(TopRPTracker.getPos() == RegionBegin && "bad initial Top tracker");
    TopRPTracker.setPos(CurrentTop);
  }
}

/// Move an instruction and update register pressure.
void ScheduleDAGMILive::scheduleMI(SUnit *SU, bool IsTopNode) {
  // Move the instruction to its new location in the instruction stream.
  MachineInstr *MI = SU->getInstr();

  if (IsTopNode) {
    assert(SU->isTopReady() && "node still has unscheduled dependencies");
    if (&*CurrentTop == MI)
      CurrentTop = nextIfDebug(++CurrentTop, CurrentBottom);
    else {
      moveInstruction(MI, CurrentTop);
      TopRPTracker.setPos(MI);
    }

    if (ShouldTrackPressure) {
      // Update top scheduled pressure.
      RegisterOperands RegOpers;
      RegOpers.collect(*MI, *TRI, MRI, ShouldTrackLaneMasks, false);
      if (ShouldTrackLaneMasks) {
        // Adjust liveness and add missing dead+read-undef flags.
        SlotIndex SlotIdx = LIS->getInstructionIndex(*MI).getRegSlot();
        RegOpers.adjustLaneLiveness(*LIS, MRI, SlotIdx, MI);
      } else {
        // Adjust for missing dead-def flags.
        RegOpers.detectDeadDefs(*MI, *LIS);
      }

      TopRPTracker.advance(RegOpers);
      assert(TopRPTracker.getPos() == CurrentTop && "out of sync");
      LLVM_DEBUG(dbgs() << "Top Pressure:\n"; dumpRegSetPressure(
                     TopRPTracker.getRegSetPressureAtPos(), TRI););

      updateScheduledPressure(SU, TopRPTracker.getPressure().MaxSetPressure);
    }
  } else {
    assert(SU->isBottomReady() && "node still has unscheduled dependencies");
    MachineBasicBlock::iterator priorII =
      priorNonDebug(CurrentBottom, CurrentTop);
    if (&*priorII == MI)
      CurrentBottom = priorII;
    else {
      if (&*CurrentTop == MI) {
        CurrentTop = nextIfDebug(++CurrentTop, priorII);
        TopRPTracker.setPos(CurrentTop);
      }
      moveInstruction(MI, CurrentBottom);
      CurrentBottom = MI;
      BotRPTracker.setPos(CurrentBottom);
    }
    if (ShouldTrackPressure) {
      RegisterOperands RegOpers;
      RegOpers.collect(*MI, *TRI, MRI, ShouldTrackLaneMasks, false);
      if (ShouldTrackLaneMasks) {
        // Adjust liveness and add missing dead+read-undef flags.
        SlotIndex SlotIdx = LIS->getInstructionIndex(*MI).getRegSlot();
        RegOpers.adjustLaneLiveness(*LIS, MRI, SlotIdx, MI);
      } else {
        // Adjust for missing dead-def flags.
        RegOpers.detectDeadDefs(*MI, *LIS);
      }

      if (BotRPTracker.getPos() != CurrentBottom)
        BotRPTracker.recedeSkipDebugValues();
      SmallVector<RegisterMaskPair, 8> LiveUses;
      BotRPTracker.recede(RegOpers, &LiveUses);
      assert(BotRPTracker.getPos() == CurrentBottom && "out of sync");
      LLVM_DEBUG(dbgs() << "Bottom Pressure:\n"; dumpRegSetPressure(
                     BotRPTracker.getRegSetPressureAtPos(), TRI););

      updateScheduledPressure(SU, BotRPTracker.getPressure().MaxSetPressure);
      updatePressureDiffs(LiveUses);
    }
  }
}

//===----------------------------------------------------------------------===//
// BaseMemOpClusterMutation - DAG post-processing to cluster loads or stores.
//===----------------------------------------------------------------------===//

namespace {

/// Post-process the DAG to create cluster edges between neighboring
/// loads or between neighboring stores.
class BaseMemOpClusterMutation : public ScheduleDAGMutation {
  struct MemOpInfo {
    SUnit *SU;
    MachineOperand *BaseOp;
    int64_t Offset;

    MemOpInfo(SUnit *su, MachineOperand *Op, int64_t ofs)
        : SU(su), BaseOp(Op), Offset(ofs) {}

    bool operator<(const MemOpInfo &RHS) const {
      if (BaseOp->getType() != RHS.BaseOp->getType())
        return BaseOp->getType() < RHS.BaseOp->getType();

      if (BaseOp->isReg())
        return std::make_tuple(BaseOp->getReg(), Offset, SU->NodeNum) <
               std::make_tuple(RHS.BaseOp->getReg(), RHS.Offset,
                               RHS.SU->NodeNum);
      if (BaseOp->isFI()) {
        const MachineFunction &MF =
            *BaseOp->getParent()->getParent()->getParent();
        const TargetFrameLowering &TFI = *MF.getSubtarget().getFrameLowering();
        bool StackGrowsDown = TFI.getStackGrowthDirection() ==
                              TargetFrameLowering::StackGrowsDown;
        // Can't use tuple comparison here since we might need to use a
        // different order when the stack grows down.
        if (BaseOp->getIndex() != RHS.BaseOp->getIndex())
          return StackGrowsDown ? BaseOp->getIndex() > RHS.BaseOp->getIndex()
                                : BaseOp->getIndex() < RHS.BaseOp->getIndex();

        if (Offset != RHS.Offset)
          return StackGrowsDown ? Offset > RHS.Offset : Offset < RHS.Offset;

        return SU->NodeNum < RHS.SU->NodeNum;
      }

      llvm_unreachable("MemOpClusterMutation only supports register or frame "
                       "index bases.");
    }
  };

  const TargetInstrInfo *TII;
  const TargetRegisterInfo *TRI;
  bool IsLoad;

public:
  BaseMemOpClusterMutation(const TargetInstrInfo *tii,
                           const TargetRegisterInfo *tri, bool IsLoad)
      : TII(tii), TRI(tri), IsLoad(IsLoad) {}

  void apply(ScheduleDAGInstrs *DAGInstrs) override;

protected:
  void clusterNeighboringMemOps(ArrayRef<SUnit *> MemOps, ScheduleDAGMI *DAG);
};

class StoreClusterMutation : public BaseMemOpClusterMutation {
public:
  StoreClusterMutation(const TargetInstrInfo *tii,
                       const TargetRegisterInfo *tri)
      : BaseMemOpClusterMutation(tii, tri, false) {}
};

class LoadClusterMutation : public BaseMemOpClusterMutation {
public:
  LoadClusterMutation(const TargetInstrInfo *tii, const TargetRegisterInfo *tri)
      : BaseMemOpClusterMutation(tii, tri, true) {}
};

} // end anonymous namespace

namespace llvm {

std::unique_ptr<ScheduleDAGMutation>
createLoadClusterDAGMutation(const TargetInstrInfo *TII,
                             const TargetRegisterInfo *TRI) {
  return EnableMemOpCluster ? llvm::make_unique<LoadClusterMutation>(TII, TRI)
                            : nullptr;
}

std::unique_ptr<ScheduleDAGMutation>
createStoreClusterDAGMutation(const TargetInstrInfo *TII,
                              const TargetRegisterInfo *TRI) {
  return EnableMemOpCluster ? llvm::make_unique<StoreClusterMutation>(TII, TRI)
                            : nullptr;
}

} // end namespace llvm

void BaseMemOpClusterMutation::clusterNeighboringMemOps(
    ArrayRef<SUnit *> MemOps, ScheduleDAGMI *DAG) {
  SmallVector<MemOpInfo, 32> MemOpRecords;
  for (SUnit *SU : MemOps) {
    MachineOperand *BaseOp;
    int64_t Offset;
    if (TII->getMemOperandWithOffset(*SU->getInstr(), BaseOp, Offset, TRI))
      MemOpRecords.push_back(MemOpInfo(SU, BaseOp, Offset));
  }
  if (MemOpRecords.size() < 2)
    return;

  llvm::sort(MemOpRecords);
  unsigned ClusterLength = 1;
  for (unsigned Idx = 0, End = MemOpRecords.size(); Idx < (End - 1); ++Idx) {
    SUnit *SUa = MemOpRecords[Idx].SU;
    SUnit *SUb = MemOpRecords[Idx+1].SU;
    if (TII->shouldClusterMemOps(*MemOpRecords[Idx].BaseOp,
                                 *MemOpRecords[Idx + 1].BaseOp,
                                 ClusterLength) &&
        DAG->addEdge(SUb, SDep(SUa, SDep::Cluster))) {
      LLVM_DEBUG(dbgs() << "Cluster ld/st SU(" << SUa->NodeNum << ") - SU("
                        << SUb->NodeNum << ")\n");
      // Copy successor edges from SUa to SUb. Interleaving computation
      // dependent on SUa can prevent load combining due to register reuse.
      // Predecessor edges do not need to be copied from SUb to SUa since nearby
      // loads should have effectively the same inputs.
      for (const SDep &Succ : SUa->Succs) {
        if (Succ.getSUnit() == SUb)
          continue;
        LLVM_DEBUG(dbgs() << "  Copy Succ SU(" << Succ.getSUnit()->NodeNum
                          << ")\n");
        DAG->addEdge(Succ.getSUnit(), SDep(SUb, SDep::Artificial));
      }
      ++ClusterLength;
    } else
      ClusterLength = 1;
  }
}

/// Callback from DAG postProcessing to create cluster edges for loads.
void BaseMemOpClusterMutation::apply(ScheduleDAGInstrs *DAGInstrs) {
  ScheduleDAGMI *DAG = static_cast<ScheduleDAGMI*>(DAGInstrs);

  // Map DAG NodeNum to store chain ID.
  DenseMap<unsigned, unsigned> StoreChainIDs;
  // Map each store chain to a set of dependent MemOps.
  SmallVector<SmallVector<SUnit*,4>, 32> StoreChainDependents;
  for (SUnit &SU : DAG->SUnits) {
    if ((IsLoad && !SU.getInstr()->mayLoad()) ||
        (!IsLoad && !SU.getInstr()->mayStore()))
      continue;

    unsigned ChainPredID = DAG->SUnits.size();
    for (const SDep &Pred : SU.Preds) {
      if (Pred.isCtrl()) {
        ChainPredID = Pred.getSUnit()->NodeNum;
        break;
      }
    }
    // Check if this chain-like pred has been seen
    // before. ChainPredID==MaxNodeID at the top of the schedule.
    unsigned NumChains = StoreChainDependents.size();
    std::pair<DenseMap<unsigned, unsigned>::iterator, bool> Result =
      StoreChainIDs.insert(std::make_pair(ChainPredID, NumChains));
    if (Result.second)
      StoreChainDependents.resize(NumChains + 1);
    StoreChainDependents[Result.first->second].push_back(&SU);
  }

  // Iterate over the store chains.
  for (auto &SCD : StoreChainDependents)
    clusterNeighboringMemOps(SCD, DAG);
}

//===----------------------------------------------------------------------===//
// CopyConstrain - DAG post-processing to encourage copy elimination.
//===----------------------------------------------------------------------===//

namespace {

/// Post-process the DAG to create weak edges from all uses of a copy to
/// the one use that defines the copy's source vreg, most likely an induction
/// variable increment.
class CopyConstrain : public ScheduleDAGMutation {
  // Transient state.
  SlotIndex RegionBeginIdx;

  // RegionEndIdx is the slot index of the last non-debug instruction in the
  // scheduling region. So we may have RegionBeginIdx == RegionEndIdx.
  SlotIndex RegionEndIdx;

public:
  CopyConstrain(const TargetInstrInfo *, const TargetRegisterInfo *) {}

  void apply(ScheduleDAGInstrs *DAGInstrs) override;

protected:
  void constrainLocalCopy(SUnit *CopySU, ScheduleDAGMILive *DAG);
};

} // end anonymous namespace

namespace llvm {

std::unique_ptr<ScheduleDAGMutation>
createCopyConstrainDAGMutation(const TargetInstrInfo *TII,
                               const TargetRegisterInfo *TRI) {
  return llvm::make_unique<CopyConstrain>(TII, TRI);
}

} // end namespace llvm

/// constrainLocalCopy handles two possibilities:
/// 1) Local src:
/// I0:     = dst
/// I1: src = ...
/// I2:     = dst
/// I3: dst = src (copy)
/// (create pred->succ edges I0->I1, I2->I1)
///
/// 2) Local copy:
/// I0: dst = src (copy)
/// I1:     = dst
/// I2: src = ...
/// I3:     = dst
/// (create pred->succ edges I1->I2, I3->I2)
///
/// Although the MachineScheduler is currently constrained to single blocks,
/// this algorithm should handle extended blocks. An EBB is a set of
/// contiguously numbered blocks such that the previous block in the EBB is
/// always the single predecessor.
void CopyConstrain::constrainLocalCopy(SUnit *CopySU, ScheduleDAGMILive *DAG) {
  LiveIntervals *LIS = DAG->getLIS();
  MachineInstr *Copy = CopySU->getInstr();

  // Check for pure vreg copies.
  const MachineOperand &SrcOp = Copy->getOperand(1);
  unsigned SrcReg = SrcOp.getReg();
  if (!TargetRegisterInfo::isVirtualRegister(SrcReg) || !SrcOp.readsReg())
    return;

  const MachineOperand &DstOp = Copy->getOperand(0);
  unsigned DstReg = DstOp.getReg();
  if (!TargetRegisterInfo::isVirtualRegister(DstReg) || DstOp.isDead())
    return;

  // Check if either the dest or source is local. If it's live across a back
  // edge, it's not local. Note that if both vregs are live across the back
  // edge, we cannot successfully contrain the copy without cyclic scheduling.
  // If both the copy's source and dest are local live intervals, then we
  // should treat the dest as the global for the purpose of adding
  // constraints. This adds edges from source's other uses to the copy.
  unsigned LocalReg = SrcReg;
  unsigned GlobalReg = DstReg;
  LiveInterval *LocalLI = &LIS->getInterval(LocalReg);
  if (!LocalLI->isLocal(RegionBeginIdx, RegionEndIdx)) {
    LocalReg = DstReg;
    GlobalReg = SrcReg;
    LocalLI = &LIS->getInterval(LocalReg);
    if (!LocalLI->isLocal(RegionBeginIdx, RegionEndIdx))
      return;
  }
  LiveInterval *GlobalLI = &LIS->getInterval(GlobalReg);

  // Find the global segment after the start of the local LI.
  LiveInterval::iterator GlobalSegment = GlobalLI->find(LocalLI->beginIndex());
  // If GlobalLI does not overlap LocalLI->start, then a copy directly feeds a
  // local live range. We could create edges from other global uses to the local
  // start, but the coalescer should have already eliminated these cases, so
  // don't bother dealing with it.
  if (GlobalSegment == GlobalLI->end())
    return;

  // If GlobalSegment is killed at the LocalLI->start, the call to find()
  // returned the next global segment. But if GlobalSegment overlaps with
  // LocalLI->start, then advance to the next segment. If a hole in GlobalLI
  // exists in LocalLI's vicinity, GlobalSegment will be the end of the hole.
  if (GlobalSegment->contains(LocalLI->beginIndex()))
    ++GlobalSegment;

  if (GlobalSegment == GlobalLI->end())
    return;

  // Check if GlobalLI contains a hole in the vicinity of LocalLI.
  if (GlobalSegment != GlobalLI->begin()) {
    // Two address defs have no hole.
    if (SlotIndex::isSameInstr(std::prev(GlobalSegment)->end,
                               GlobalSegment->start)) {
      return;
    }
    // If the prior global segment may be defined by the same two-address
    // instruction that also defines LocalLI, then can't make a hole here.
    if (SlotIndex::isSameInstr(std::prev(GlobalSegment)->start,
                               LocalLI->beginIndex())) {
      return;
    }
    // If GlobalLI has a prior segment, it must be live into the EBB. Otherwise
    // it would be a disconnected component in the live range.
    assert(std::prev(GlobalSegment)->start < LocalLI->beginIndex() &&
           "Disconnected LRG within the scheduling region.");
  }
  MachineInstr *GlobalDef = LIS->getInstructionFromIndex(GlobalSegment->start);
  if (!GlobalDef)
    return;

  SUnit *GlobalSU = DAG->getSUnit(GlobalDef);
  if (!GlobalSU)
    return;

  // GlobalDef is the bottom of the GlobalLI hole. Open the hole by
  // constraining the uses of the last local def to precede GlobalDef.
  SmallVector<SUnit*,8> LocalUses;
  const VNInfo *LastLocalVN = LocalLI->getVNInfoBefore(LocalLI->endIndex());
  MachineInstr *LastLocalDef = LIS->getInstructionFromIndex(LastLocalVN->def);
  SUnit *LastLocalSU = DAG->getSUnit(LastLocalDef);
  for (const SDep &Succ : LastLocalSU->Succs) {
    if (Succ.getKind() != SDep::Data || Succ.getReg() != LocalReg)
      continue;
    if (Succ.getSUnit() == GlobalSU)
      continue;
    if (!DAG->canAddEdge(GlobalSU, Succ.getSUnit()))
      return;
    LocalUses.push_back(Succ.getSUnit());
  }
  // Open the top of the GlobalLI hole by constraining any earlier global uses
  // to precede the start of LocalLI.
  SmallVector<SUnit*,8> GlobalUses;
  MachineInstr *FirstLocalDef =
    LIS->getInstructionFromIndex(LocalLI->beginIndex());
  SUnit *FirstLocalSU = DAG->getSUnit(FirstLocalDef);
  for (const SDep &Pred : GlobalSU->Preds) {
    if (Pred.getKind() != SDep::Anti || Pred.getReg() != GlobalReg)
      continue;
    if (Pred.getSUnit() == FirstLocalSU)
      continue;
    if (!DAG->canAddEdge(FirstLocalSU, Pred.getSUnit()))
      return;
    GlobalUses.push_back(Pred.getSUnit());
  }
  LLVM_DEBUG(dbgs() << "Constraining copy SU(" << CopySU->NodeNum << ")\n");
  // Add the weak edges.
  for (SmallVectorImpl<SUnit*>::const_iterator
         I = LocalUses.begin(), E = LocalUses.end(); I != E; ++I) {
    LLVM_DEBUG(dbgs() << "  Local use SU(" << (*I)->NodeNum << ") -> SU("
                      << GlobalSU->NodeNum << ")\n");
    DAG->addEdge(GlobalSU, SDep(*I, SDep::Weak));
  }
  for (SmallVectorImpl<SUnit*>::const_iterator
         I = GlobalUses.begin(), E = GlobalUses.end(); I != E; ++I) {
    LLVM_DEBUG(dbgs() << "  Global use SU(" << (*I)->NodeNum << ") -> SU("
                      << FirstLocalSU->NodeNum << ")\n");
    DAG->addEdge(FirstLocalSU, SDep(*I, SDep::Weak));
  }
}

/// Callback from DAG postProcessing to create weak edges to encourage
/// copy elimination.
void CopyConstrain::apply(ScheduleDAGInstrs *DAGInstrs) {
  ScheduleDAGMI *DAG = static_cast<ScheduleDAGMI*>(DAGInstrs);
  assert(DAG->hasVRegLiveness() && "Expect VRegs with LiveIntervals");

  MachineBasicBlock::iterator FirstPos = nextIfDebug(DAG->begin(), DAG->end());
  if (FirstPos == DAG->end())
    return;
  RegionBeginIdx = DAG->getLIS()->getInstructionIndex(*FirstPos);
  RegionEndIdx = DAG->getLIS()->getInstructionIndex(
      *priorNonDebug(DAG->end(), DAG->begin()));

  for (SUnit &SU : DAG->SUnits) {
    if (!SU.getInstr()->isCopy())
      continue;

    constrainLocalCopy(&SU, static_cast<ScheduleDAGMILive*>(DAG));
  }
}

//===----------------------------------------------------------------------===//
// MachineSchedStrategy helpers used by GenericScheduler, GenericPostScheduler
// and possibly other custom schedulers.
//===----------------------------------------------------------------------===//

static const unsigned InvalidCycle = ~0U;

SchedBoundary::~SchedBoundary() { delete HazardRec; }

/// Given a Count of resource usage and a Latency value, return true if a
/// SchedBoundary becomes resource limited.
static bool checkResourceLimit(unsigned LFactor, unsigned Count,
                               unsigned Latency) {
  return (int)(Count - (Latency * LFactor)) > (int)LFactor;
}

void SchedBoundary::reset() {
  // A new HazardRec is created for each DAG and owned by SchedBoundary.
  // Destroying and reconstructing it is very expensive though. So keep
  // invalid, placeholder HazardRecs.
  if (HazardRec && HazardRec->isEnabled()) {
    delete HazardRec;
    HazardRec = nullptr;
  }
  Available.clear();
  Pending.clear();
  CheckPending = false;
  CurrCycle = 0;
  CurrMOps = 0;
  MinReadyCycle = std::numeric_limits<unsigned>::max();
  ExpectedLatency = 0;
  DependentLatency = 0;
  RetiredMOps = 0;
  MaxExecutedResCount = 0;
  ZoneCritResIdx = 0;
  IsResourceLimited = false;
  ReservedCycles.clear();
#ifndef NDEBUG
  // Track the maximum number of stall cycles that could arise either from the
  // latency of a DAG edge or the number of cycles that a processor resource is
  // reserved (SchedBoundary::ReservedCycles).
  MaxObservedStall = 0;
#endif
  // Reserve a zero-count for invalid CritResIdx.
  ExecutedResCounts.resize(1);
  assert(!ExecutedResCounts[0] && "nonzero count for bad resource");
}

void SchedRemainder::
init(ScheduleDAGMI *DAG, const TargetSchedModel *SchedModel) {
  reset();
  if (!SchedModel->hasInstrSchedModel())
    return;
  RemainingCounts.resize(SchedModel->getNumProcResourceKinds());
  for (SUnit &SU : DAG->SUnits) {
    const MCSchedClassDesc *SC = DAG->getSchedClass(&SU);
    RemIssueCount += SchedModel->getNumMicroOps(SU.getInstr(), SC)
      * SchedModel->getMicroOpFactor();
    for (TargetSchedModel::ProcResIter
           PI = SchedModel->getWriteProcResBegin(SC),
           PE = SchedModel->getWriteProcResEnd(SC); PI != PE; ++PI) {
      unsigned PIdx = PI->ProcResourceIdx;
      unsigned Factor = SchedModel->getResourceFactor(PIdx);
      RemainingCounts[PIdx] += (Factor * PI->Cycles);
    }
  }
}

void SchedBoundary::
init(ScheduleDAGMI *dag, const TargetSchedModel *smodel, SchedRemainder *rem) {
  reset();
  DAG = dag;
  SchedModel = smodel;
  Rem = rem;
  if (SchedModel->hasInstrSchedModel()) {
    ExecutedResCounts.resize(SchedModel->getNumProcResourceKinds());
    ReservedCycles.resize(SchedModel->getNumProcResourceKinds(), InvalidCycle);
  }
}

/// Compute the stall cycles based on this SUnit's ready time. Heuristics treat
/// these "soft stalls" differently than the hard stall cycles based on CPU
/// resources and computed by checkHazard(). A fully in-order model
/// (MicroOpBufferSize==0) will not make use of this since instructions are not
/// available for scheduling until they are ready. However, a weaker in-order
/// model may use this for heuristics. For example, if a processor has in-order
/// behavior when reading certain resources, this may come into play.
unsigned SchedBoundary::getLatencyStallCycles(SUnit *SU) {
  if (!SU->isUnbuffered)
    return 0;

  unsigned ReadyCycle = (isTop() ? SU->TopReadyCycle : SU->BotReadyCycle);
  if (ReadyCycle > CurrCycle)
    return ReadyCycle - CurrCycle;
  return 0;
}

/// Compute the next cycle at which the given processor resource can be
/// scheduled.
unsigned SchedBoundary::
getNextResourceCycle(unsigned PIdx, unsigned Cycles) {
  unsigned NextUnreserved = ReservedCycles[PIdx];
  // If this resource has never been used, always return cycle zero.
  if (NextUnreserved == InvalidCycle)
    return 0;
  // For bottom-up scheduling add the cycles needed for the current operation.
  if (!isTop())
    NextUnreserved += Cycles;
  return NextUnreserved;
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
bool SchedBoundary::checkHazard(SUnit *SU) {
  if (HazardRec->isEnabled()
      && HazardRec->getHazardType(SU) != ScheduleHazardRecognizer::NoHazard) {
    return true;
  }

  unsigned uops = SchedModel->getNumMicroOps(SU->getInstr());
  if ((CurrMOps > 0) && (CurrMOps + uops > SchedModel->getIssueWidth())) {
    LLVM_DEBUG(dbgs() << "  SU(" << SU->NodeNum << ") uops="
                      << SchedModel->getNumMicroOps(SU->getInstr()) << '\n');
    return true;
  }

  if (CurrMOps > 0 &&
      ((isTop() && SchedModel->mustBeginGroup(SU->getInstr())) ||
       (!isTop() && SchedModel->mustEndGroup(SU->getInstr())))) {
    LLVM_DEBUG(dbgs() << "  hazard: SU(" << SU->NodeNum << ") must "
                      << (isTop() ? "begin" : "end") << " group\n");
    return true;
  }

  if (SchedModel->hasInstrSchedModel() && SU->hasReservedResource) {
    const MCSchedClassDesc *SC = DAG->getSchedClass(SU);
    for (const MCWriteProcResEntry &PE :
          make_range(SchedModel->getWriteProcResBegin(SC),
                     SchedModel->getWriteProcResEnd(SC))) {
      unsigned ResIdx = PE.ProcResourceIdx;
      unsigned Cycles = PE.Cycles;
      unsigned NRCycle = getNextResourceCycle(ResIdx, Cycles);
      if (NRCycle > CurrCycle) {
#ifndef NDEBUG
        MaxObservedStall = std::max(Cycles, MaxObservedStall);
#endif
        LLVM_DEBUG(dbgs() << "  SU(" << SU->NodeNum << ") "
                          << SchedModel->getResourceName(ResIdx) << "="
                          << NRCycle << "c\n");
        return true;
      }
    }
  }
  return false;
}

// Find the unscheduled node in ReadySUs with the highest latency.
unsigned SchedBoundary::
findMaxLatency(ArrayRef<SUnit*> ReadySUs) {
  SUnit *LateSU = nullptr;
  unsigned RemLatency = 0;
  for (SUnit *SU : ReadySUs) {
    unsigned L = getUnscheduledLatency(SU);
    if (L > RemLatency) {
      RemLatency = L;
      LateSU = SU;
    }
  }
  if (LateSU) {
    LLVM_DEBUG(dbgs() << Available.getName() << " RemLatency SU("
                      << LateSU->NodeNum << ") " << RemLatency << "c\n");
  }
  return RemLatency;
}

// Count resources in this zone and the remaining unscheduled
// instruction. Return the max count, scaled. Set OtherCritIdx to the critical
// resource index, or zero if the zone is issue limited.
unsigned SchedBoundary::
getOtherResourceCount(unsigned &OtherCritIdx) {
  OtherCritIdx = 0;
  if (!SchedModel->hasInstrSchedModel())
    return 0;

  unsigned OtherCritCount = Rem->RemIssueCount
    + (RetiredMOps * SchedModel->getMicroOpFactor());
  LLVM_DEBUG(dbgs() << "  " << Available.getName() << " + Remain MOps: "
                    << OtherCritCount / SchedModel->getMicroOpFactor() << '\n');
  for (unsigned PIdx = 1, PEnd = SchedModel->getNumProcResourceKinds();
       PIdx != PEnd; ++PIdx) {
    unsigned OtherCount = getResourceCount(PIdx) + Rem->RemainingCounts[PIdx];
    if (OtherCount > OtherCritCount) {
      OtherCritCount = OtherCount;
      OtherCritIdx = PIdx;
    }
  }
  if (OtherCritIdx) {
    LLVM_DEBUG(
        dbgs() << "  " << Available.getName() << " + Remain CritRes: "
               << OtherCritCount / SchedModel->getResourceFactor(OtherCritIdx)
               << " " << SchedModel->getResourceName(OtherCritIdx) << "\n");
  }
  return OtherCritCount;
}

void SchedBoundary::releaseNode(SUnit *SU, unsigned ReadyCycle) {
  assert(SU->getInstr() && "Scheduled SUnit must have instr");

#ifndef NDEBUG
  // ReadyCycle was been bumped up to the CurrCycle when this node was
  // scheduled, but CurrCycle may have been eagerly advanced immediately after
  // scheduling, so may now be greater than ReadyCycle.
  if (ReadyCycle > CurrCycle)
    MaxObservedStall = std::max(ReadyCycle - CurrCycle, MaxObservedStall);
#endif

  if (ReadyCycle < MinReadyCycle)
    MinReadyCycle = ReadyCycle;

  // Check for interlocks first. For the purpose of other heuristics, an
  // instruction that cannot issue appears as if it's not in the ReadyQueue.
  bool IsBuffered = SchedModel->getMicroOpBufferSize() != 0;
  if ((!IsBuffered && ReadyCycle > CurrCycle) || checkHazard(SU) ||
      Available.size() >= ReadyListLimit)
    Pending.push(SU);
  else
    Available.push(SU);
}

/// Move the boundary of scheduled code by one cycle.
void SchedBoundary::bumpCycle(unsigned NextCycle) {
  if (SchedModel->getMicroOpBufferSize() == 0) {
    assert(MinReadyCycle < std::numeric_limits<unsigned>::max() &&
           "MinReadyCycle uninitialized");
    if (MinReadyCycle > NextCycle)
      NextCycle = MinReadyCycle;
  }
  // Update the current micro-ops, which will issue in the next cycle.
  unsigned DecMOps = SchedModel->getIssueWidth() * (NextCycle - CurrCycle);
  CurrMOps = (CurrMOps <= DecMOps) ? 0 : CurrMOps - DecMOps;

  // Decrement DependentLatency based on the next cycle.
  if ((NextCycle - CurrCycle) > DependentLatency)
    DependentLatency = 0;
  else
    DependentLatency -= (NextCycle - CurrCycle);

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
  IsResourceLimited =
      checkResourceLimit(SchedModel->getLatencyFactor(), getCriticalCount(),
                         getScheduledLatency());

  LLVM_DEBUG(dbgs() << "Cycle: " << CurrCycle << ' ' << Available.getName()
                    << '\n');
}

void SchedBoundary::incExecutedResources(unsigned PIdx, unsigned Count) {
  ExecutedResCounts[PIdx] += Count;
  if (ExecutedResCounts[PIdx] > MaxExecutedResCount)
    MaxExecutedResCount = ExecutedResCounts[PIdx];
}

/// Add the given processor resource to this scheduled zone.
///
/// \param Cycles indicates the number of consecutive (non-pipelined) cycles
/// during which this resource is consumed.
///
/// \return the next cycle at which the instruction may execute without
/// oversubscribing resources.
unsigned SchedBoundary::
countResource(unsigned PIdx, unsigned Cycles, unsigned NextCycle) {
  unsigned Factor = SchedModel->getResourceFactor(PIdx);
  unsigned Count = Factor * Cycles;
  LLVM_DEBUG(dbgs() << "  " << SchedModel->getResourceName(PIdx) << " +"
                    << Cycles << "x" << Factor << "u\n");

  // Update Executed resources counts.
  incExecutedResources(PIdx, Count);
  assert(Rem->RemainingCounts[PIdx] >= Count && "resource double counted");
  Rem->RemainingCounts[PIdx] -= Count;

  // Check if this resource exceeds the current critical resource. If so, it
  // becomes the critical resource.
  if (ZoneCritResIdx != PIdx && (getResourceCount(PIdx) > getCriticalCount())) {
    ZoneCritResIdx = PIdx;
    LLVM_DEBUG(dbgs() << "  *** Critical resource "
                      << SchedModel->getResourceName(PIdx) << ": "
                      << getResourceCount(PIdx) / SchedModel->getLatencyFactor()
                      << "c\n");
  }
  // For reserved resources, record the highest cycle using the resource.
  unsigned NextAvailable = getNextResourceCycle(PIdx, Cycles);
  if (NextAvailable > CurrCycle) {
    LLVM_DEBUG(dbgs() << "  Resource conflict: "
                      << SchedModel->getProcResource(PIdx)->Name
                      << " reserved until @" << NextAvailable << "\n");
  }
  return NextAvailable;
}

/// Move the boundary of scheduled code by one SUnit.
void SchedBoundary::bumpNode(SUnit *SU) {
  // Update the reservation table.
  if (HazardRec->isEnabled()) {
    if (!isTop() && SU->isCall) {
      // Calls are scheduled with their preceding instructions. For bottom-up
      // scheduling, clear the pipeline state before emitting.
      HazardRec->Reset();
    }
    HazardRec->EmitInstruction(SU);
  }
  // checkHazard should prevent scheduling multiple instructions per cycle that
  // exceed the issue width.
  const MCSchedClassDesc *SC = DAG->getSchedClass(SU);
  unsigned IncMOps = SchedModel->getNumMicroOps(SU->getInstr());
  assert(
      (CurrMOps == 0 || (CurrMOps + IncMOps) <= SchedModel->getIssueWidth()) &&
      "Cannot schedule this instruction's MicroOps in the current cycle.");

  unsigned ReadyCycle = (isTop() ? SU->TopReadyCycle : SU->BotReadyCycle);
  LLVM_DEBUG(dbgs() << "  Ready @" << ReadyCycle << "c\n");

  unsigned NextCycle = CurrCycle;
  switch (SchedModel->getMicroOpBufferSize()) {
  case 0:
    assert(ReadyCycle <= CurrCycle && "Broken PendingQueue");
    break;
  case 1:
    if (ReadyCycle > NextCycle) {
      NextCycle = ReadyCycle;
      LLVM_DEBUG(dbgs() << "  *** Stall until: " << ReadyCycle << "\n");
    }
    break;
  default:
    // We don't currently model the OOO reorder buffer, so consider all
    // scheduled MOps to be "retired". We do loosely model in-order resource
    // latency. If this instruction uses an in-order resource, account for any
    // likely stall cycles.
    if (SU->isUnbuffered && ReadyCycle > NextCycle)
      NextCycle = ReadyCycle;
    break;
  }
  RetiredMOps += IncMOps;

  // Update resource counts and critical resource.
  if (SchedModel->hasInstrSchedModel()) {
    unsigned DecRemIssue = IncMOps * SchedModel->getMicroOpFactor();
    assert(Rem->RemIssueCount >= DecRemIssue && "MOps double counted");
    Rem->RemIssueCount -= DecRemIssue;
    if (ZoneCritResIdx) {
      // Scale scheduled micro-ops for comparing with the critical resource.
      unsigned ScaledMOps =
        RetiredMOps * SchedModel->getMicroOpFactor();

      // If scaled micro-ops are now more than the previous critical resource by
      // a full cycle, then micro-ops issue becomes critical.
      if ((int)(ScaledMOps - getResourceCount(ZoneCritResIdx))
          >= (int)SchedModel->getLatencyFactor()) {
        ZoneCritResIdx = 0;
        LLVM_DEBUG(dbgs() << "  *** Critical resource NumMicroOps: "
                          << ScaledMOps / SchedModel->getLatencyFactor()
                          << "c\n");
      }
    }
    for (TargetSchedModel::ProcResIter
           PI = SchedModel->getWriteProcResBegin(SC),
           PE = SchedModel->getWriteProcResEnd(SC); PI != PE; ++PI) {
      unsigned RCycle =
        countResource(PI->ProcResourceIdx, PI->Cycles, NextCycle);
      if (RCycle > NextCycle)
        NextCycle = RCycle;
    }
    if (SU->hasReservedResource) {
      // For reserved resources, record the highest cycle using the resource.
      // For top-down scheduling, this is the cycle in which we schedule this
      // instruction plus the number of cycles the operations reserves the
      // resource. For bottom-up is it simply the instruction's cycle.
      for (TargetSchedModel::ProcResIter
             PI = SchedModel->getWriteProcResBegin(SC),
             PE = SchedModel->getWriteProcResEnd(SC); PI != PE; ++PI) {
        unsigned PIdx = PI->ProcResourceIdx;
        if (SchedModel->getProcResource(PIdx)->BufferSize == 0) {
          if (isTop()) {
            ReservedCycles[PIdx] =
              std::max(getNextResourceCycle(PIdx, 0), NextCycle + PI->Cycles);
          }
          else
            ReservedCycles[PIdx] = NextCycle;
        }
      }
    }
  }
  // Update ExpectedLatency and DependentLatency.
  unsigned &TopLatency = isTop() ? ExpectedLatency : DependentLatency;
  unsigned &BotLatency = isTop() ? DependentLatency : ExpectedLatency;
  if (SU->getDepth() > TopLatency) {
    TopLatency = SU->getDepth();
    LLVM_DEBUG(dbgs() << "  " << Available.getName() << " TopLatency SU("
                      << SU->NodeNum << ") " << TopLatency << "c\n");
  }
  if (SU->getHeight() > BotLatency) {
    BotLatency = SU->getHeight();
    LLVM_DEBUG(dbgs() << "  " << Available.getName() << " BotLatency SU("
                      << SU->NodeNum << ") " << BotLatency << "c\n");
  }
  // If we stall for any reason, bump the cycle.
  if (NextCycle > CurrCycle)
    bumpCycle(NextCycle);
  else
    // After updating ZoneCritResIdx and ExpectedLatency, check if we're
    // resource limited. If a stall occurred, bumpCycle does this.
    IsResourceLimited =
        checkResourceLimit(SchedModel->getLatencyFactor(), getCriticalCount(),
                           getScheduledLatency());

  // Update CurrMOps after calling bumpCycle to handle stalls, since bumpCycle
  // resets CurrMOps. Loop to handle instructions with more MOps than issue in
  // one cycle.  Since we commonly reach the max MOps here, opportunistically
  // bump the cycle to avoid uselessly checking everything in the readyQ.
  CurrMOps += IncMOps;

  // Bump the cycle count for issue group constraints.
  // This must be done after NextCycle has been adjust for all other stalls.
  // Calling bumpCycle(X) will reduce CurrMOps by one issue group and set
  // currCycle to X.
  if ((isTop() &&  SchedModel->mustEndGroup(SU->getInstr())) ||
      (!isTop() && SchedModel->mustBeginGroup(SU->getInstr()))) {
    LLVM_DEBUG(dbgs() << "  Bump cycle to " << (isTop() ? "end" : "begin")
                      << " group\n");
    bumpCycle(++NextCycle);
  }

  while (CurrMOps >= SchedModel->getIssueWidth()) {
    LLVM_DEBUG(dbgs() << "  *** Max MOps " << CurrMOps << " at cycle "
                      << CurrCycle << '\n');
    bumpCycle(++NextCycle);
  }
  LLVM_DEBUG(dumpScheduledState());
}

/// Release pending ready nodes in to the available queue. This makes them
/// visible to heuristics.
void SchedBoundary::releasePending() {
  // If the available queue is empty, it is safe to reset MinReadyCycle.
  if (Available.empty())
    MinReadyCycle = std::numeric_limits<unsigned>::max();

  // Check to see if any of the pending instructions are ready to issue.  If
  // so, add them to the available queue.
  bool IsBuffered = SchedModel->getMicroOpBufferSize() != 0;
  for (unsigned i = 0, e = Pending.size(); i != e; ++i) {
    SUnit *SU = *(Pending.begin()+i);
    unsigned ReadyCycle = isTop() ? SU->TopReadyCycle : SU->BotReadyCycle;

    if (ReadyCycle < MinReadyCycle)
      MinReadyCycle = ReadyCycle;

    if (!IsBuffered && ReadyCycle > CurrCycle)
      continue;

    if (checkHazard(SU))
      continue;

    if (Available.size() >= ReadyListLimit)
      break;

    Available.push(SU);
    Pending.remove(Pending.begin()+i);
    --i; --e;
  }
  CheckPending = false;
}

/// Remove SU from the ready set for this boundary.
void SchedBoundary::removeReady(SUnit *SU) {
  if (Available.isInQueue(SU))
    Available.remove(Available.find(SU));
  else {
    assert(Pending.isInQueue(SU) && "bad ready count");
    Pending.remove(Pending.find(SU));
  }
}

/// If this queue only has one ready candidate, return it. As a side effect,
/// defer any nodes that now hit a hazard, and advance the cycle until at least
/// one node is ready. If multiple instructions are ready, return NULL.
SUnit *SchedBoundary::pickOnlyChoice() {
  if (CheckPending)
    releasePending();

  if (CurrMOps > 0) {
    // Defer any ready instrs that now have a hazard.
    for (ReadyQueue::iterator I = Available.begin(); I != Available.end();) {
      if (checkHazard(*I)) {
        Pending.push(*I);
        I = Available.remove(I);
        continue;
      }
      ++I;
    }
  }
  for (unsigned i = 0; Available.empty(); ++i) {
//  FIXME: Re-enable assert once PR20057 is resolved.
//    assert(i <= (HazardRec->getMaxLookAhead() + MaxObservedStall) &&
//           "permanent hazard");
    (void)i;
    bumpCycle(CurrCycle + 1);
    releasePending();
  }

  LLVM_DEBUG(Pending.dump());
  LLVM_DEBUG(Available.dump());

  if (Available.size() == 1)
    return *Available.begin();
  return nullptr;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
// This is useful information to dump after bumpNode.
// Note that the Queue contents are more useful before pickNodeFromQueue.
LLVM_DUMP_METHOD void SchedBoundary::dumpScheduledState() const {
  unsigned ResFactor;
  unsigned ResCount;
  if (ZoneCritResIdx) {
    ResFactor = SchedModel->getResourceFactor(ZoneCritResIdx);
    ResCount = getResourceCount(ZoneCritResIdx);
  } else {
    ResFactor = SchedModel->getMicroOpFactor();
    ResCount = RetiredMOps * ResFactor;
  }
  unsigned LFactor = SchedModel->getLatencyFactor();
  dbgs() << Available.getName() << " @" << CurrCycle << "c\n"
         << "  Retired: " << RetiredMOps;
  dbgs() << "\n  Executed: " << getExecutedCount() / LFactor << "c";
  dbgs() << "\n  Critical: " << ResCount / LFactor << "c, "
         << ResCount / ResFactor << " "
         << SchedModel->getResourceName(ZoneCritResIdx)
         << "\n  ExpectedLatency: " << ExpectedLatency << "c\n"
         << (IsResourceLimited ? "  - Resource" : "  - Latency")
         << " limited.\n";
}
#endif

//===----------------------------------------------------------------------===//
// GenericScheduler - Generic implementation of MachineSchedStrategy.
//===----------------------------------------------------------------------===//

void GenericSchedulerBase::SchedCandidate::
initResourceDelta(const ScheduleDAGMI *DAG,
                  const TargetSchedModel *SchedModel) {
  if (!Policy.ReduceResIdx && !Policy.DemandResIdx)
    return;

  const MCSchedClassDesc *SC = DAG->getSchedClass(SU);
  for (TargetSchedModel::ProcResIter
         PI = SchedModel->getWriteProcResBegin(SC),
         PE = SchedModel->getWriteProcResEnd(SC); PI != PE; ++PI) {
    if (PI->ProcResourceIdx == Policy.ReduceResIdx)
      ResDelta.CritResources += PI->Cycles;
    if (PI->ProcResourceIdx == Policy.DemandResIdx)
      ResDelta.DemandedResources += PI->Cycles;
  }
}

/// Compute remaining latency. We need this both to determine whether the
/// overall schedule has become latency-limited and whether the instructions
/// outside this zone are resource or latency limited.
///
/// The "dependent" latency is updated incrementally during scheduling as the
/// max height/depth of scheduled nodes minus the cycles since it was
/// scheduled:
///   DLat = max (N.depth - (CurrCycle - N.ReadyCycle) for N in Zone
///
/// The "independent" latency is the max ready queue depth:
///   ILat = max N.depth for N in Available|Pending
///
/// RemainingLatency is the greater of independent and dependent latency.
///
/// These computations are expensive, especially in DAGs with many edges, so
/// only do them if necessary.
static unsigned computeRemLatency(SchedBoundary &CurrZone) {
  unsigned RemLatency = CurrZone.getDependentLatency();
  RemLatency = std::max(RemLatency,
                        CurrZone.findMaxLatency(CurrZone.Available.elements()));
  RemLatency = std::max(RemLatency,
                        CurrZone.findMaxLatency(CurrZone.Pending.elements()));
  return RemLatency;
}

/// Returns true if the current cycle plus remaning latency is greater than
/// the critical path in the scheduling region.
bool GenericSchedulerBase::shouldReduceLatency(const CandPolicy &Policy,
                                               SchedBoundary &CurrZone,
                                               bool ComputeRemLatency,
                                               unsigned &RemLatency) const {
  // The current cycle is already greater than the critical path, so we are
  // already latency limited and don't need to compute the remaining latency.
  if (CurrZone.getCurrCycle() > Rem.CriticalPath)
    return true;

  // If we haven't scheduled anything yet, then we aren't latency limited.
  if (CurrZone.getCurrCycle() == 0)
    return false;

  if (ComputeRemLatency)
    RemLatency = computeRemLatency(CurrZone);

  return RemLatency + CurrZone.getCurrCycle() > Rem.CriticalPath;
}

/// Set the CandPolicy given a scheduling zone given the current resources and
/// latencies inside and outside the zone.
void GenericSchedulerBase::setPolicy(CandPolicy &Policy, bool IsPostRA,
                                     SchedBoundary &CurrZone,
                                     SchedBoundary *OtherZone) {
  // Apply preemptive heuristics based on the total latency and resources
  // inside and outside this zone. Potential stalls should be considered before
  // following this policy.

  // Compute the critical resource outside the zone.
  unsigned OtherCritIdx = 0;
  unsigned OtherCount =
    OtherZone ? OtherZone->getOtherResourceCount(OtherCritIdx) : 0;

  bool OtherResLimited = false;
  unsigned RemLatency = 0;
  bool RemLatencyComputed = false;
  if (SchedModel->hasInstrSchedModel() && OtherCount != 0) {
    RemLatency = computeRemLatency(CurrZone);
    RemLatencyComputed = true;
    OtherResLimited = checkResourceLimit(SchedModel->getLatencyFactor(),
                                         OtherCount, RemLatency);
  }

  // Schedule aggressively for latency in PostRA mode. We don't check for
  // acyclic latency during PostRA, and highly out-of-order processors will
  // skip PostRA scheduling.
  if (!OtherResLimited &&
      (IsPostRA || shouldReduceLatency(Policy, CurrZone, !RemLatencyComputed,
                                       RemLatency))) {
    Policy.ReduceLatency |= true;
    LLVM_DEBUG(dbgs() << "  " << CurrZone.Available.getName()
                      << " RemainingLatency " << RemLatency << " + "
                      << CurrZone.getCurrCycle() << "c > CritPath "
                      << Rem.CriticalPath << "\n");
  }
  // If the same resource is limiting inside and outside the zone, do nothing.
  if (CurrZone.getZoneCritResIdx() == OtherCritIdx)
    return;

  LLVM_DEBUG(if (CurrZone.isResourceLimited()) {
    dbgs() << "  " << CurrZone.Available.getName() << " ResourceLimited: "
           << SchedModel->getResourceName(CurrZone.getZoneCritResIdx()) << "\n";
  } if (OtherResLimited) dbgs()
                 << "  RemainingLimit: "
                 << SchedModel->getResourceName(OtherCritIdx) << "\n";
             if (!CurrZone.isResourceLimited() && !OtherResLimited) dbgs()
             << "  Latency limited both directions.\n");

  if (CurrZone.isResourceLimited() && !Policy.ReduceResIdx)
    Policy.ReduceResIdx = CurrZone.getZoneCritResIdx();

  if (OtherResLimited)
    Policy.DemandResIdx = OtherCritIdx;
}

#ifndef NDEBUG
const char *GenericSchedulerBase::getReasonStr(
  GenericSchedulerBase::CandReason Reason) {
  switch (Reason) {
  case NoCand:         return "NOCAND    ";
  case Only1:          return "ONLY1     ";
  case PhysReg:        return "PHYS-REG  ";
  case RegExcess:      return "REG-EXCESS";
  case RegCritical:    return "REG-CRIT  ";
  case Stall:          return "STALL     ";
  case Cluster:        return "CLUSTER   ";
  case Weak:           return "WEAK      ";
  case RegMax:         return "REG-MAX   ";
  case ResourceReduce: return "RES-REDUCE";
  case ResourceDemand: return "RES-DEMAND";
  case TopDepthReduce: return "TOP-DEPTH ";
  case TopPathReduce:  return "TOP-PATH  ";
  case BotHeightReduce:return "BOT-HEIGHT";
  case BotPathReduce:  return "BOT-PATH  ";
  case NextDefUse:     return "DEF-USE   ";
  case NodeOrder:      return "ORDER     ";
  };
  llvm_unreachable("Unknown reason!");
}

void GenericSchedulerBase::traceCandidate(const SchedCandidate &Cand) {
  PressureChange P;
  unsigned ResIdx = 0;
  unsigned Latency = 0;
  switch (Cand.Reason) {
  default:
    break;
  case RegExcess:
    P = Cand.RPDelta.Excess;
    break;
  case RegCritical:
    P = Cand.RPDelta.CriticalMax;
    break;
  case RegMax:
    P = Cand.RPDelta.CurrentMax;
    break;
  case ResourceReduce:
    ResIdx = Cand.Policy.ReduceResIdx;
    break;
  case ResourceDemand:
    ResIdx = Cand.Policy.DemandResIdx;
    break;
  case TopDepthReduce:
    Latency = Cand.SU->getDepth();
    break;
  case TopPathReduce:
    Latency = Cand.SU->getHeight();
    break;
  case BotHeightReduce:
    Latency = Cand.SU->getHeight();
    break;
  case BotPathReduce:
    Latency = Cand.SU->getDepth();
    break;
  }
  dbgs() << "  Cand SU(" << Cand.SU->NodeNum << ") " << getReasonStr(Cand.Reason);
  if (P.isValid())
    dbgs() << " " << TRI->getRegPressureSetName(P.getPSet())
           << ":" << P.getUnitInc() << " ";
  else
    dbgs() << "      ";
  if (ResIdx)
    dbgs() << " " << SchedModel->getProcResource(ResIdx)->Name << " ";
  else
    dbgs() << "         ";
  if (Latency)
    dbgs() << " " << Latency << " cycles ";
  else
    dbgs() << "          ";
  dbgs() << '\n';
}
#endif

namespace llvm {
/// Return true if this heuristic determines order.
bool tryLess(int TryVal, int CandVal,
             GenericSchedulerBase::SchedCandidate &TryCand,
             GenericSchedulerBase::SchedCandidate &Cand,
             GenericSchedulerBase::CandReason Reason) {
  if (TryVal < CandVal) {
    TryCand.Reason = Reason;
    return true;
  }
  if (TryVal > CandVal) {
    if (Cand.Reason > Reason)
      Cand.Reason = Reason;
    return true;
  }
  return false;
}

bool tryGreater(int TryVal, int CandVal,
                GenericSchedulerBase::SchedCandidate &TryCand,
                GenericSchedulerBase::SchedCandidate &Cand,
                GenericSchedulerBase::CandReason Reason) {
  if (TryVal > CandVal) {
    TryCand.Reason = Reason;
    return true;
  }
  if (TryVal < CandVal) {
    if (Cand.Reason > Reason)
      Cand.Reason = Reason;
    return true;
  }
  return false;
}

bool tryLatency(GenericSchedulerBase::SchedCandidate &TryCand,
                GenericSchedulerBase::SchedCandidate &Cand,
                SchedBoundary &Zone) {
  if (Zone.isTop()) {
    if (Cand.SU->getDepth() > Zone.getScheduledLatency()) {
      if (tryLess(TryCand.SU->getDepth(), Cand.SU->getDepth(),
                  TryCand, Cand, GenericSchedulerBase::TopDepthReduce))
        return true;
    }
    if (tryGreater(TryCand.SU->getHeight(), Cand.SU->getHeight(),
                   TryCand, Cand, GenericSchedulerBase::TopPathReduce))
      return true;
  } else {
    if (Cand.SU->getHeight() > Zone.getScheduledLatency()) {
      if (tryLess(TryCand.SU->getHeight(), Cand.SU->getHeight(),
                  TryCand, Cand, GenericSchedulerBase::BotHeightReduce))
        return true;
    }
    if (tryGreater(TryCand.SU->getDepth(), Cand.SU->getDepth(),
                   TryCand, Cand, GenericSchedulerBase::BotPathReduce))
      return true;
  }
  return false;
}
} // end namespace llvm

static void tracePick(GenericSchedulerBase::CandReason Reason, bool IsTop) {
  LLVM_DEBUG(dbgs() << "Pick " << (IsTop ? "Top " : "Bot ")
                    << GenericSchedulerBase::getReasonStr(Reason) << '\n');
}

static void tracePick(const GenericSchedulerBase::SchedCandidate &Cand) {
  tracePick(Cand.Reason, Cand.AtTop);
}

void GenericScheduler::initialize(ScheduleDAGMI *dag) {
  assert(dag->hasVRegLiveness() &&
         "(PreRA)GenericScheduler needs vreg liveness");
  DAG = static_cast<ScheduleDAGMILive*>(dag);
  SchedModel = DAG->getSchedModel();
  TRI = DAG->TRI;

  Rem.init(DAG, SchedModel);
  Top.init(DAG, SchedModel, &Rem);
  Bot.init(DAG, SchedModel, &Rem);

  // Initialize resource counts.

  // Initialize the HazardRecognizers. If itineraries don't exist, are empty, or
  // are disabled, then these HazardRecs will be disabled.
  const InstrItineraryData *Itin = SchedModel->getInstrItineraries();
  if (!Top.HazardRec) {
    Top.HazardRec =
        DAG->MF.getSubtarget().getInstrInfo()->CreateTargetMIHazardRecognizer(
            Itin, DAG);
  }
  if (!Bot.HazardRec) {
    Bot.HazardRec =
        DAG->MF.getSubtarget().getInstrInfo()->CreateTargetMIHazardRecognizer(
            Itin, DAG);
  }
  TopCand.SU = nullptr;
  BotCand.SU = nullptr;
}

/// Initialize the per-region scheduling policy.
void GenericScheduler::initPolicy(MachineBasicBlock::iterator Begin,
                                  MachineBasicBlock::iterator End,
                                  unsigned NumRegionInstrs) {
  const MachineFunction &MF = *Begin->getMF();
  const TargetLowering *TLI = MF.getSubtarget().getTargetLowering();

  // Avoid setting up the register pressure tracker for small regions to save
  // compile time. As a rough heuristic, only track pressure when the number of
  // schedulable instructions exceeds half the integer register file.
  RegionPolicy.ShouldTrackPressure = true;
  for (unsigned VT = MVT::i32; VT > (unsigned)MVT::i1; --VT) {
    MVT::SimpleValueType LegalIntVT = (MVT::SimpleValueType)VT;
    if (TLI->isTypeLegal(LegalIntVT)) {
      unsigned NIntRegs = Context->RegClassInfo->getNumAllocatableRegs(
        TLI->getRegClassFor(LegalIntVT));
      RegionPolicy.ShouldTrackPressure = NumRegionInstrs > (NIntRegs / 2);
    }
  }

  // For generic targets, we default to bottom-up, because it's simpler and more
  // compile-time optimizations have been implemented in that direction.
  RegionPolicy.OnlyBottomUp = true;

  // Allow the subtarget to override default policy.
  MF.getSubtarget().overrideSchedPolicy(RegionPolicy, NumRegionInstrs);

  // After subtarget overrides, apply command line options.
  if (!EnableRegPressure)
    RegionPolicy.ShouldTrackPressure = false;

  // Check -misched-topdown/bottomup can force or unforce scheduling direction.
  // e.g. -misched-bottomup=false allows scheduling in both directions.
  assert((!ForceTopDown || !ForceBottomUp) &&
         "-misched-topdown incompatible with -misched-bottomup");
  if (ForceBottomUp.getNumOccurrences() > 0) {
    RegionPolicy.OnlyBottomUp = ForceBottomUp;
    if (RegionPolicy.OnlyBottomUp)
      RegionPolicy.OnlyTopDown = false;
  }
  if (ForceTopDown.getNumOccurrences() > 0) {
    RegionPolicy.OnlyTopDown = ForceTopDown;
    if (RegionPolicy.OnlyTopDown)
      RegionPolicy.OnlyBottomUp = false;
  }
}

void GenericScheduler::dumpPolicy() const {
  // Cannot completely remove virtual function even in release mode.
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  dbgs() << "GenericScheduler RegionPolicy: "
         << " ShouldTrackPressure=" << RegionPolicy.ShouldTrackPressure
         << " OnlyTopDown=" << RegionPolicy.OnlyTopDown
         << " OnlyBottomUp=" << RegionPolicy.OnlyBottomUp
         << "\n";
#endif
}

/// Set IsAcyclicLatencyLimited if the acyclic path is longer than the cyclic
/// critical path by more cycles than it takes to drain the instruction buffer.
/// We estimate an upper bounds on in-flight instructions as:
///
/// CyclesPerIteration = max( CyclicPath, Loop-Resource-Height )
/// InFlightIterations = AcyclicPath / CyclesPerIteration
/// InFlightResources = InFlightIterations * LoopResources
///
/// TODO: Check execution resources in addition to IssueCount.
void GenericScheduler::checkAcyclicLatency() {
  if (Rem.CyclicCritPath == 0 || Rem.CyclicCritPath >= Rem.CriticalPath)
    return;

  // Scaled number of cycles per loop iteration.
  unsigned IterCount =
    std::max(Rem.CyclicCritPath * SchedModel->getLatencyFactor(),
             Rem.RemIssueCount);
  // Scaled acyclic critical path.
  unsigned AcyclicCount = Rem.CriticalPath * SchedModel->getLatencyFactor();
  // InFlightCount = (AcyclicPath / IterCycles) * InstrPerLoop
  unsigned InFlightCount =
    (AcyclicCount * Rem.RemIssueCount + IterCount-1) / IterCount;
  unsigned BufferLimit =
    SchedModel->getMicroOpBufferSize() * SchedModel->getMicroOpFactor();

  Rem.IsAcyclicLatencyLimited = InFlightCount > BufferLimit;

  LLVM_DEBUG(
      dbgs() << "IssueCycles="
             << Rem.RemIssueCount / SchedModel->getLatencyFactor() << "c "
             << "IterCycles=" << IterCount / SchedModel->getLatencyFactor()
             << "c NumIters=" << (AcyclicCount + IterCount - 1) / IterCount
             << " InFlight=" << InFlightCount / SchedModel->getMicroOpFactor()
             << "m BufferLim=" << SchedModel->getMicroOpBufferSize() << "m\n";
      if (Rem.IsAcyclicLatencyLimited) dbgs() << "  ACYCLIC LATENCY LIMIT\n");
}

void GenericScheduler::registerRoots() {
  Rem.CriticalPath = DAG->ExitSU.getDepth();

  // Some roots may not feed into ExitSU. Check all of them in case.
  for (const SUnit *SU : Bot.Available) {
    if (SU->getDepth() > Rem.CriticalPath)
      Rem.CriticalPath = SU->getDepth();
  }
  LLVM_DEBUG(dbgs() << "Critical Path(GS-RR ): " << Rem.CriticalPath << '\n');
  if (DumpCriticalPathLength) {
    errs() << "Critical Path(GS-RR ): " << Rem.CriticalPath << " \n";
  }

  if (EnableCyclicPath && SchedModel->getMicroOpBufferSize() > 0) {
    Rem.CyclicCritPath = DAG->computeCyclicCriticalPath();
    checkAcyclicLatency();
  }
}

namespace llvm {
bool tryPressure(const PressureChange &TryP,
                 const PressureChange &CandP,
                 GenericSchedulerBase::SchedCandidate &TryCand,
                 GenericSchedulerBase::SchedCandidate &Cand,
                 GenericSchedulerBase::CandReason Reason,
                 const TargetRegisterInfo *TRI,
                 const MachineFunction &MF) {
  // If one candidate decreases and the other increases, go with it.
  // Invalid candidates have UnitInc==0.
  if (tryGreater(TryP.getUnitInc() < 0, CandP.getUnitInc() < 0, TryCand, Cand,
                 Reason)) {
    return true;
  }
  // Do not compare the magnitude of pressure changes between top and bottom
  // boundary.
  if (Cand.AtTop != TryCand.AtTop)
    return false;

  // If both candidates affect the same set in the same boundary, go with the
  // smallest increase.
  unsigned TryPSet = TryP.getPSetOrMax();
  unsigned CandPSet = CandP.getPSetOrMax();
  if (TryPSet == CandPSet) {
    return tryLess(TryP.getUnitInc(), CandP.getUnitInc(), TryCand, Cand,
                   Reason);
  }

  int TryRank = TryP.isValid() ? TRI->getRegPressureSetScore(MF, TryPSet) :
                                 std::numeric_limits<int>::max();

  int CandRank = CandP.isValid() ? TRI->getRegPressureSetScore(MF, CandPSet) :
                                   std::numeric_limits<int>::max();

  // If the candidates are decreasing pressure, reverse priority.
  if (TryP.getUnitInc() < 0)
    std::swap(TryRank, CandRank);
  return tryGreater(TryRank, CandRank, TryCand, Cand, Reason);
}

unsigned getWeakLeft(const SUnit *SU, bool isTop) {
  return (isTop) ? SU->WeakPredsLeft : SU->WeakSuccsLeft;
}

/// Minimize physical register live ranges. Regalloc wants them adjacent to
/// their physreg def/use.
///
/// FIXME: This is an unnecessary check on the critical path. Most are root/leaf
/// copies which can be prescheduled. The rest (e.g. x86 MUL) could be bundled
/// with the operation that produces or consumes the physreg. We'll do this when
/// regalloc has support for parallel copies.
int biasPhysReg(const SUnit *SU, bool isTop) {
  const MachineInstr *MI = SU->getInstr();

  if (MI->isCopy()) {
    unsigned ScheduledOper = isTop ? 1 : 0;
    unsigned UnscheduledOper = isTop ? 0 : 1;
    // If we have already scheduled the physreg produce/consumer, immediately
    // schedule the copy.
    if (TargetRegisterInfo::isPhysicalRegister(
            MI->getOperand(ScheduledOper).getReg()))
      return 1;
    // If the physreg is at the boundary, defer it. Otherwise schedule it
    // immediately to free the dependent. We can hoist the copy later.
    bool AtBoundary = isTop ? !SU->NumSuccsLeft : !SU->NumPredsLeft;
    if (TargetRegisterInfo::isPhysicalRegister(
            MI->getOperand(UnscheduledOper).getReg()))
      return AtBoundary ? -1 : 1;
  }

  if (MI->isMoveImmediate()) {
    // If we have a move immediate and all successors have been assigned, bias
    // towards scheduling this later. Make sure all register defs are to
    // physical registers.
    bool DoBias = true;
    for (const MachineOperand &Op : MI->defs()) {
      if (Op.isReg() && !TargetRegisterInfo::isPhysicalRegister(Op.getReg())) {
        DoBias = false;
        break;
      }
    }

    if (DoBias)
      return isTop ? -1 : 1;
  }

  return 0;
}
} // end namespace llvm

void GenericScheduler::initCandidate(SchedCandidate &Cand, SUnit *SU,
                                     bool AtTop,
                                     const RegPressureTracker &RPTracker,
                                     RegPressureTracker &TempTracker) {
  Cand.SU = SU;
  Cand.AtTop = AtTop;
  if (DAG->isTrackingPressure()) {
    if (AtTop) {
      TempTracker.getMaxDownwardPressureDelta(
        Cand.SU->getInstr(),
        Cand.RPDelta,
        DAG->getRegionCriticalPSets(),
        DAG->getRegPressure().MaxSetPressure);
    } else {
      if (VerifyScheduling) {
        TempTracker.getMaxUpwardPressureDelta(
          Cand.SU->getInstr(),
          &DAG->getPressureDiff(Cand.SU),
          Cand.RPDelta,
          DAG->getRegionCriticalPSets(),
          DAG->getRegPressure().MaxSetPressure);
      } else {
        RPTracker.getUpwardPressureDelta(
          Cand.SU->getInstr(),
          DAG->getPressureDiff(Cand.SU),
          Cand.RPDelta,
          DAG->getRegionCriticalPSets(),
          DAG->getRegPressure().MaxSetPressure);
      }
    }
  }
  LLVM_DEBUG(if (Cand.RPDelta.Excess.isValid()) dbgs()
             << "  Try  SU(" << Cand.SU->NodeNum << ") "
             << TRI->getRegPressureSetName(Cand.RPDelta.Excess.getPSet()) << ":"
             << Cand.RPDelta.Excess.getUnitInc() << "\n");
}

/// Apply a set of heuristics to a new candidate. Heuristics are currently
/// hierarchical. This may be more efficient than a graduated cost model because
/// we don't need to evaluate all aspects of the model for each node in the
/// queue. But it's really done to make the heuristics easier to debug and
/// statistically analyze.
///
/// \param Cand provides the policy and current best candidate.
/// \param TryCand refers to the next SUnit candidate, otherwise uninitialized.
/// \param Zone describes the scheduled zone that we are extending, or nullptr
//              if Cand is from a different zone than TryCand.
void GenericScheduler::tryCandidate(SchedCandidate &Cand,
                                    SchedCandidate &TryCand,
                                    SchedBoundary *Zone) const {
  // Initialize the candidate if needed.
  if (!Cand.isValid()) {
    TryCand.Reason = NodeOrder;
    return;
  }

  // Bias PhysReg Defs and copies to their uses and defined respectively.
  if (tryGreater(biasPhysReg(TryCand.SU, TryCand.AtTop),
                 biasPhysReg(Cand.SU, Cand.AtTop), TryCand, Cand, PhysReg))
    return;

  // Avoid exceeding the target's limit.
  if (DAG->isTrackingPressure() && tryPressure(TryCand.RPDelta.Excess,
                                               Cand.RPDelta.Excess,
                                               TryCand, Cand, RegExcess, TRI,
                                               DAG->MF))
    return;

  // Avoid increasing the max critical pressure in the scheduled region.
  if (DAG->isTrackingPressure() && tryPressure(TryCand.RPDelta.CriticalMax,
                                               Cand.RPDelta.CriticalMax,
                                               TryCand, Cand, RegCritical, TRI,
                                               DAG->MF))
    return;

  // We only compare a subset of features when comparing nodes between
  // Top and Bottom boundary. Some properties are simply incomparable, in many
  // other instances we should only override the other boundary if something
  // is a clear good pick on one boundary. Skip heuristics that are more
  // "tie-breaking" in nature.
  bool SameBoundary = Zone != nullptr;
  if (SameBoundary) {
    // For loops that are acyclic path limited, aggressively schedule for
    // latency. Within an single cycle, whenever CurrMOps > 0, allow normal
    // heuristics to take precedence.
    if (Rem.IsAcyclicLatencyLimited && !Zone->getCurrMOps() &&
        tryLatency(TryCand, Cand, *Zone))
      return;

    // Prioritize instructions that read unbuffered resources by stall cycles.
    if (tryLess(Zone->getLatencyStallCycles(TryCand.SU),
                Zone->getLatencyStallCycles(Cand.SU), TryCand, Cand, Stall))
      return;
  }

  // Keep clustered nodes together to encourage downstream peephole
  // optimizations which may reduce resource requirements.
  //
  // This is a best effort to set things up for a post-RA pass. Optimizations
  // like generating loads of multiple registers should ideally be done within
  // the scheduler pass by combining the loads during DAG postprocessing.
  const SUnit *CandNextClusterSU =
    Cand.AtTop ? DAG->getNextClusterSucc() : DAG->getNextClusterPred();
  const SUnit *TryCandNextClusterSU =
    TryCand.AtTop ? DAG->getNextClusterSucc() : DAG->getNextClusterPred();
  if (tryGreater(TryCand.SU == TryCandNextClusterSU,
                 Cand.SU == CandNextClusterSU,
                 TryCand, Cand, Cluster))
    return;

  if (SameBoundary) {
    // Weak edges are for clustering and other constraints.
    if (tryLess(getWeakLeft(TryCand.SU, TryCand.AtTop),
                getWeakLeft(Cand.SU, Cand.AtTop),
                TryCand, Cand, Weak))
      return;
  }

  // Avoid increasing the max pressure of the entire region.
  if (DAG->isTrackingPressure() && tryPressure(TryCand.RPDelta.CurrentMax,
                                               Cand.RPDelta.CurrentMax,
                                               TryCand, Cand, RegMax, TRI,
                                               DAG->MF))
    return;

  if (SameBoundary) {
    // Avoid critical resource consumption and balance the schedule.
    TryCand.initResourceDelta(DAG, SchedModel);
    if (tryLess(TryCand.ResDelta.CritResources, Cand.ResDelta.CritResources,
                TryCand, Cand, ResourceReduce))
      return;
    if (tryGreater(TryCand.ResDelta.DemandedResources,
                   Cand.ResDelta.DemandedResources,
                   TryCand, Cand, ResourceDemand))
      return;

    // Avoid serializing long latency dependence chains.
    // For acyclic path limited loops, latency was already checked above.
    if (!RegionPolicy.DisableLatencyHeuristic && TryCand.Policy.ReduceLatency &&
        !Rem.IsAcyclicLatencyLimited && tryLatency(TryCand, Cand, *Zone))
      return;

    // Fall through to original instruction order.
    if ((Zone->isTop() && TryCand.SU->NodeNum < Cand.SU->NodeNum)
        || (!Zone->isTop() && TryCand.SU->NodeNum > Cand.SU->NodeNum)) {
      TryCand.Reason = NodeOrder;
    }
  }
}

/// Pick the best candidate from the queue.
///
/// TODO: getMaxPressureDelta results can be mostly cached for each SUnit during
/// DAG building. To adjust for the current scheduling location we need to
/// maintain the number of vreg uses remaining to be top-scheduled.
void GenericScheduler::pickNodeFromQueue(SchedBoundary &Zone,
                                         const CandPolicy &ZonePolicy,
                                         const RegPressureTracker &RPTracker,
                                         SchedCandidate &Cand) {
  // getMaxPressureDelta temporarily modifies the tracker.
  RegPressureTracker &TempTracker = const_cast<RegPressureTracker&>(RPTracker);

  ReadyQueue &Q = Zone.Available;
  for (SUnit *SU : Q) {

    SchedCandidate TryCand(ZonePolicy);
    initCandidate(TryCand, SU, Zone.isTop(), RPTracker, TempTracker);
    // Pass SchedBoundary only when comparing nodes from the same boundary.
    SchedBoundary *ZoneArg = Cand.AtTop == TryCand.AtTop ? &Zone : nullptr;
    tryCandidate(Cand, TryCand, ZoneArg);
    if (TryCand.Reason != NoCand) {
      // Initialize resource delta if needed in case future heuristics query it.
      if (TryCand.ResDelta == SchedResourceDelta())
        TryCand.initResourceDelta(DAG, SchedModel);
      Cand.setBest(TryCand);
      LLVM_DEBUG(traceCandidate(Cand));
    }
  }
}

/// Pick the best candidate node from either the top or bottom queue.
SUnit *GenericScheduler::pickNodeBidirectional(bool &IsTopNode) {
  // Schedule as far as possible in the direction of no choice. This is most
  // efficient, but also provides the best heuristics for CriticalPSets.
  if (SUnit *SU = Bot.pickOnlyChoice()) {
    IsTopNode = false;
    tracePick(Only1, false);
    return SU;
  }
  if (SUnit *SU = Top.pickOnlyChoice()) {
    IsTopNode = true;
    tracePick(Only1, true);
    return SU;
  }
  // Set the bottom-up policy based on the state of the current bottom zone and
  // the instructions outside the zone, including the top zone.
  CandPolicy BotPolicy;
  setPolicy(BotPolicy, /*IsPostRA=*/false, Bot, &Top);
  // Set the top-down policy based on the state of the current top zone and
  // the instructions outside the zone, including the bottom zone.
  CandPolicy TopPolicy;
  setPolicy(TopPolicy, /*IsPostRA=*/false, Top, &Bot);

  // See if BotCand is still valid (because we previously scheduled from Top).
  LLVM_DEBUG(dbgs() << "Picking from Bot:\n");
  if (!BotCand.isValid() || BotCand.SU->isScheduled ||
      BotCand.Policy != BotPolicy) {
    BotCand.reset(CandPolicy());
    pickNodeFromQueue(Bot, BotPolicy, DAG->getBotRPTracker(), BotCand);
    assert(BotCand.Reason != NoCand && "failed to find the first candidate");
  } else {
    LLVM_DEBUG(traceCandidate(BotCand));
#ifndef NDEBUG
    if (VerifyScheduling) {
      SchedCandidate TCand;
      TCand.reset(CandPolicy());
      pickNodeFromQueue(Bot, BotPolicy, DAG->getBotRPTracker(), TCand);
      assert(TCand.SU == BotCand.SU &&
             "Last pick result should correspond to re-picking right now");
    }
#endif
  }

  // Check if the top Q has a better candidate.
  LLVM_DEBUG(dbgs() << "Picking from Top:\n");
  if (!TopCand.isValid() || TopCand.SU->isScheduled ||
      TopCand.Policy != TopPolicy) {
    TopCand.reset(CandPolicy());
    pickNodeFromQueue(Top, TopPolicy, DAG->getTopRPTracker(), TopCand);
    assert(TopCand.Reason != NoCand && "failed to find the first candidate");
  } else {
    LLVM_DEBUG(traceCandidate(TopCand));
#ifndef NDEBUG
    if (VerifyScheduling) {
      SchedCandidate TCand;
      TCand.reset(CandPolicy());
      pickNodeFromQueue(Top, TopPolicy, DAG->getTopRPTracker(), TCand);
      assert(TCand.SU == TopCand.SU &&
           "Last pick result should correspond to re-picking right now");
    }
#endif
  }

  // Pick best from BotCand and TopCand.
  assert(BotCand.isValid());
  assert(TopCand.isValid());
  SchedCandidate Cand = BotCand;
  TopCand.Reason = NoCand;
  tryCandidate(Cand, TopCand, nullptr);
  if (TopCand.Reason != NoCand) {
    Cand.setBest(TopCand);
    LLVM_DEBUG(traceCandidate(Cand));
  }

  IsTopNode = Cand.AtTop;
  tracePick(Cand);
  return Cand.SU;
}

/// Pick the best node to balance the schedule. Implements MachineSchedStrategy.
SUnit *GenericScheduler::pickNode(bool &IsTopNode) {
  if (DAG->top() == DAG->bottom()) {
    assert(Top.Available.empty() && Top.Pending.empty() &&
           Bot.Available.empty() && Bot.Pending.empty() && "ReadyQ garbage");
    return nullptr;
  }
  SUnit *SU;
  do {
    if (RegionPolicy.OnlyTopDown) {
      SU = Top.pickOnlyChoice();
      if (!SU) {
        CandPolicy NoPolicy;
        TopCand.reset(NoPolicy);
        pickNodeFromQueue(Top, NoPolicy, DAG->getTopRPTracker(), TopCand);
        assert(TopCand.Reason != NoCand && "failed to find a candidate");
        tracePick(TopCand);
        SU = TopCand.SU;
      }
      IsTopNode = true;
    } else if (RegionPolicy.OnlyBottomUp) {
      SU = Bot.pickOnlyChoice();
      if (!SU) {
        CandPolicy NoPolicy;
        BotCand.reset(NoPolicy);
        pickNodeFromQueue(Bot, NoPolicy, DAG->getBotRPTracker(), BotCand);
        assert(BotCand.Reason != NoCand && "failed to find a candidate");
        tracePick(BotCand);
        SU = BotCand.SU;
      }
      IsTopNode = false;
    } else {
      SU = pickNodeBidirectional(IsTopNode);
    }
  } while (SU->isScheduled);

  if (SU->isTopReady())
    Top.removeReady(SU);
  if (SU->isBottomReady())
    Bot.removeReady(SU);

  LLVM_DEBUG(dbgs() << "Scheduling SU(" << SU->NodeNum << ") "
                    << *SU->getInstr());
  return SU;
}

void GenericScheduler::reschedulePhysReg(SUnit *SU, bool isTop) {
  MachineBasicBlock::iterator InsertPos = SU->getInstr();
  if (!isTop)
    ++InsertPos;
  SmallVectorImpl<SDep> &Deps = isTop ? SU->Preds : SU->Succs;

  // Find already scheduled copies with a single physreg dependence and move
  // them just above the scheduled instruction.
  for (SDep &Dep : Deps) {
    if (Dep.getKind() != SDep::Data || !TRI->isPhysicalRegister(Dep.getReg()))
      continue;
    SUnit *DepSU = Dep.getSUnit();
    if (isTop ? DepSU->Succs.size() > 1 : DepSU->Preds.size() > 1)
      continue;
    MachineInstr *Copy = DepSU->getInstr();
    if (!Copy->isCopy() && !Copy->isMoveImmediate())
      continue;
    LLVM_DEBUG(dbgs() << "  Rescheduling physreg copy ";
               DAG->dumpNode(*Dep.getSUnit()));
    DAG->moveInstruction(Copy, InsertPos);
  }
}

/// Update the scheduler's state after scheduling a node. This is the same node
/// that was just returned by pickNode(). However, ScheduleDAGMILive needs to
/// update it's state based on the current cycle before MachineSchedStrategy
/// does.
///
/// FIXME: Eventually, we may bundle physreg copies rather than rescheduling
/// them here. See comments in biasPhysReg.
void GenericScheduler::schedNode(SUnit *SU, bool IsTopNode) {
  if (IsTopNode) {
    SU->TopReadyCycle = std::max(SU->TopReadyCycle, Top.getCurrCycle());
    Top.bumpNode(SU);
    if (SU->hasPhysRegUses)
      reschedulePhysReg(SU, true);
  } else {
    SU->BotReadyCycle = std::max(SU->BotReadyCycle, Bot.getCurrCycle());
    Bot.bumpNode(SU);
    if (SU->hasPhysRegDefs)
      reschedulePhysReg(SU, false);
  }
}

/// Create the standard converging machine scheduler. This will be used as the
/// default scheduler if the target does not set a default.
ScheduleDAGMILive *llvm::createGenericSchedLive(MachineSchedContext *C) {
  ScheduleDAGMILive *DAG =
      new ScheduleDAGMILive(C, llvm::make_unique<GenericScheduler>(C));
  // Register DAG post-processors.
  //
  // FIXME: extend the mutation API to allow earlier mutations to instantiate
  // data and pass it to later mutations. Have a single mutation that gathers
  // the interesting nodes in one pass.
  DAG->addMutation(createCopyConstrainDAGMutation(DAG->TII, DAG->TRI));
  return DAG;
}

static ScheduleDAGInstrs *createConveringSched(MachineSchedContext *C) {
  return createGenericSchedLive(C);
}

static MachineSchedRegistry
GenericSchedRegistry("converge", "Standard converging scheduler.",
                     createConveringSched);

//===----------------------------------------------------------------------===//
// PostGenericScheduler - Generic PostRA implementation of MachineSchedStrategy.
//===----------------------------------------------------------------------===//

void PostGenericScheduler::initialize(ScheduleDAGMI *Dag) {
  DAG = Dag;
  SchedModel = DAG->getSchedModel();
  TRI = DAG->TRI;

  Rem.init(DAG, SchedModel);
  Top.init(DAG, SchedModel, &Rem);
  BotRoots.clear();

  // Initialize the HazardRecognizers. If itineraries don't exist, are empty,
  // or are disabled, then these HazardRecs will be disabled.
  const InstrItineraryData *Itin = SchedModel->getInstrItineraries();
  if (!Top.HazardRec) {
    Top.HazardRec =
        DAG->MF.getSubtarget().getInstrInfo()->CreateTargetMIHazardRecognizer(
            Itin, DAG);
  }
}

void PostGenericScheduler::registerRoots() {
  Rem.CriticalPath = DAG->ExitSU.getDepth();

  // Some roots may not feed into ExitSU. Check all of them in case.
  for (const SUnit *SU : BotRoots) {
    if (SU->getDepth() > Rem.CriticalPath)
      Rem.CriticalPath = SU->getDepth();
  }
  LLVM_DEBUG(dbgs() << "Critical Path: (PGS-RR) " << Rem.CriticalPath << '\n');
  if (DumpCriticalPathLength) {
    errs() << "Critical Path(PGS-RR ): " << Rem.CriticalPath << " \n";
  }
}

/// Apply a set of heuristics to a new candidate for PostRA scheduling.
///
/// \param Cand provides the policy and current best candidate.
/// \param TryCand refers to the next SUnit candidate, otherwise uninitialized.
void PostGenericScheduler::tryCandidate(SchedCandidate &Cand,
                                        SchedCandidate &TryCand) {
  // Initialize the candidate if needed.
  if (!Cand.isValid()) {
    TryCand.Reason = NodeOrder;
    return;
  }

  // Prioritize instructions that read unbuffered resources by stall cycles.
  if (tryLess(Top.getLatencyStallCycles(TryCand.SU),
              Top.getLatencyStallCycles(Cand.SU), TryCand, Cand, Stall))
    return;

  // Keep clustered nodes together.
  if (tryGreater(TryCand.SU == DAG->getNextClusterSucc(),
                 Cand.SU == DAG->getNextClusterSucc(),
                 TryCand, Cand, Cluster))
    return;

  // Avoid critical resource consumption and balance the schedule.
  if (tryLess(TryCand.ResDelta.CritResources, Cand.ResDelta.CritResources,
              TryCand, Cand, ResourceReduce))
    return;
  if (tryGreater(TryCand.ResDelta.DemandedResources,
                 Cand.ResDelta.DemandedResources,
                 TryCand, Cand, ResourceDemand))
    return;

  // Avoid serializing long latency dependence chains.
  if (Cand.Policy.ReduceLatency && tryLatency(TryCand, Cand, Top)) {
    return;
  }

  // Fall through to original instruction order.
  if (TryCand.SU->NodeNum < Cand.SU->NodeNum)
    TryCand.Reason = NodeOrder;
}

void PostGenericScheduler::pickNodeFromQueue(SchedCandidate &Cand) {
  ReadyQueue &Q = Top.Available;
  for (SUnit *SU : Q) {
    SchedCandidate TryCand(Cand.Policy);
    TryCand.SU = SU;
    TryCand.AtTop = true;
    TryCand.initResourceDelta(DAG, SchedModel);
    tryCandidate(Cand, TryCand);
    if (TryCand.Reason != NoCand) {
      Cand.setBest(TryCand);
      LLVM_DEBUG(traceCandidate(Cand));
    }
  }
}

/// Pick the next node to schedule.
SUnit *PostGenericScheduler::pickNode(bool &IsTopNode) {
  if (DAG->top() == DAG->bottom()) {
    assert(Top.Available.empty() && Top.Pending.empty() && "ReadyQ garbage");
    return nullptr;
  }
  SUnit *SU;
  do {
    SU = Top.pickOnlyChoice();
    if (SU) {
      tracePick(Only1, true);
    } else {
      CandPolicy NoPolicy;
      SchedCandidate TopCand(NoPolicy);
      // Set the top-down policy based on the state of the current top zone and
      // the instructions outside the zone, including the bottom zone.
      setPolicy(TopCand.Policy, /*IsPostRA=*/true, Top, nullptr);
      pickNodeFromQueue(TopCand);
      assert(TopCand.Reason != NoCand && "failed to find a candidate");
      tracePick(TopCand);
      SU = TopCand.SU;
    }
  } while (SU->isScheduled);

  IsTopNode = true;
  Top.removeReady(SU);

  LLVM_DEBUG(dbgs() << "Scheduling SU(" << SU->NodeNum << ") "
                    << *SU->getInstr());
  return SU;
}

/// Called after ScheduleDAGMI has scheduled an instruction and updated
/// scheduled/remaining flags in the DAG nodes.
void PostGenericScheduler::schedNode(SUnit *SU, bool IsTopNode) {
  SU->TopReadyCycle = std::max(SU->TopReadyCycle, Top.getCurrCycle());
  Top.bumpNode(SU);
}

ScheduleDAGMI *llvm::createGenericSchedPostRA(MachineSchedContext *C) {
  return new ScheduleDAGMI(C, llvm::make_unique<PostGenericScheduler>(C),
                           /*RemoveKillFlags=*/true);
}

//===----------------------------------------------------------------------===//
// ILP Scheduler. Currently for experimental analysis of heuristics.
//===----------------------------------------------------------------------===//

namespace {

/// Order nodes by the ILP metric.
struct ILPOrder {
  const SchedDFSResult *DFSResult = nullptr;
  const BitVector *ScheduledTrees = nullptr;
  bool MaximizeILP;

  ILPOrder(bool MaxILP) : MaximizeILP(MaxILP) {}

  /// Apply a less-than relation on node priority.
  ///
  /// (Return true if A comes after B in the Q.)
  bool operator()(const SUnit *A, const SUnit *B) const {
    unsigned SchedTreeA = DFSResult->getSubtreeID(A);
    unsigned SchedTreeB = DFSResult->getSubtreeID(B);
    if (SchedTreeA != SchedTreeB) {
      // Unscheduled trees have lower priority.
      if (ScheduledTrees->test(SchedTreeA) != ScheduledTrees->test(SchedTreeB))
        return ScheduledTrees->test(SchedTreeB);

      // Trees with shallower connections have have lower priority.
      if (DFSResult->getSubtreeLevel(SchedTreeA)
          != DFSResult->getSubtreeLevel(SchedTreeB)) {
        return DFSResult->getSubtreeLevel(SchedTreeA)
          < DFSResult->getSubtreeLevel(SchedTreeB);
      }
    }
    if (MaximizeILP)
      return DFSResult->getILP(A) < DFSResult->getILP(B);
    else
      return DFSResult->getILP(A) > DFSResult->getILP(B);
  }
};

/// Schedule based on the ILP metric.
class ILPScheduler : public MachineSchedStrategy {
  ScheduleDAGMILive *DAG = nullptr;
  ILPOrder Cmp;

  std::vector<SUnit*> ReadyQ;

public:
  ILPScheduler(bool MaximizeILP) : Cmp(MaximizeILP) {}

  void initialize(ScheduleDAGMI *dag) override {
    assert(dag->hasVRegLiveness() && "ILPScheduler needs vreg liveness");
    DAG = static_cast<ScheduleDAGMILive*>(dag);
    DAG->computeDFSResult();
    Cmp.DFSResult = DAG->getDFSResult();
    Cmp.ScheduledTrees = &DAG->getScheduledTrees();
    ReadyQ.clear();
  }

  void registerRoots() override {
    // Restore the heap in ReadyQ with the updated DFS results.
    std::make_heap(ReadyQ.begin(), ReadyQ.end(), Cmp);
  }

  /// Implement MachineSchedStrategy interface.
  /// -----------------------------------------

  /// Callback to select the highest priority node from the ready Q.
  SUnit *pickNode(bool &IsTopNode) override {
    if (ReadyQ.empty()) return nullptr;
    std::pop_heap(ReadyQ.begin(), ReadyQ.end(), Cmp);
    SUnit *SU = ReadyQ.back();
    ReadyQ.pop_back();
    IsTopNode = false;
    LLVM_DEBUG(dbgs() << "Pick node "
                      << "SU(" << SU->NodeNum << ") "
                      << " ILP: " << DAG->getDFSResult()->getILP(SU)
                      << " Tree: " << DAG->getDFSResult()->getSubtreeID(SU)
                      << " @"
                      << DAG->getDFSResult()->getSubtreeLevel(
                             DAG->getDFSResult()->getSubtreeID(SU))
                      << '\n'
                      << "Scheduling " << *SU->getInstr());
    return SU;
  }

  /// Scheduler callback to notify that a new subtree is scheduled.
  void scheduleTree(unsigned SubtreeID) override {
    std::make_heap(ReadyQ.begin(), ReadyQ.end(), Cmp);
  }

  /// Callback after a node is scheduled. Mark a newly scheduled tree, notify
  /// DFSResults, and resort the priority Q.
  void schedNode(SUnit *SU, bool IsTopNode) override {
    assert(!IsTopNode && "SchedDFSResult needs bottom-up");
  }

  void releaseTopNode(SUnit *) override { /*only called for top roots*/ }

  void releaseBottomNode(SUnit *SU) override {
    ReadyQ.push_back(SU);
    std::push_heap(ReadyQ.begin(), ReadyQ.end(), Cmp);
  }
};

} // end anonymous namespace

static ScheduleDAGInstrs *createILPMaxScheduler(MachineSchedContext *C) {
  return new ScheduleDAGMILive(C, llvm::make_unique<ILPScheduler>(true));
}
static ScheduleDAGInstrs *createILPMinScheduler(MachineSchedContext *C) {
  return new ScheduleDAGMILive(C, llvm::make_unique<ILPScheduler>(false));
}

static MachineSchedRegistry ILPMaxRegistry(
  "ilpmax", "Schedule bottom-up for max ILP", createILPMaxScheduler);
static MachineSchedRegistry ILPMinRegistry(
  "ilpmin", "Schedule bottom-up for min ILP", createILPMinScheduler);

//===----------------------------------------------------------------------===//
// Machine Instruction Shuffler for Correctness Testing
//===----------------------------------------------------------------------===//

#ifndef NDEBUG
namespace {

/// Apply a less-than relation on the node order, which corresponds to the
/// instruction order prior to scheduling. IsReverse implements greater-than.
template<bool IsReverse>
struct SUnitOrder {
  bool operator()(SUnit *A, SUnit *B) const {
    if (IsReverse)
      return A->NodeNum > B->NodeNum;
    else
      return A->NodeNum < B->NodeNum;
  }
};

/// Reorder instructions as much as possible.
class InstructionShuffler : public MachineSchedStrategy {
  bool IsAlternating;
  bool IsTopDown;

  // Using a less-than relation (SUnitOrder<false>) for the TopQ priority
  // gives nodes with a higher number higher priority causing the latest
  // instructions to be scheduled first.
  PriorityQueue<SUnit*, std::vector<SUnit*>, SUnitOrder<false>>
    TopQ;

  // When scheduling bottom-up, use greater-than as the queue priority.
  PriorityQueue<SUnit*, std::vector<SUnit*>, SUnitOrder<true>>
    BottomQ;

public:
  InstructionShuffler(bool alternate, bool topdown)
    : IsAlternating(alternate), IsTopDown(topdown) {}

  void initialize(ScheduleDAGMI*) override {
    TopQ.clear();
    BottomQ.clear();
  }

  /// Implement MachineSchedStrategy interface.
  /// -----------------------------------------

  SUnit *pickNode(bool &IsTopNode) override {
    SUnit *SU;
    if (IsTopDown) {
      do {
        if (TopQ.empty()) return nullptr;
        SU = TopQ.top();
        TopQ.pop();
      } while (SU->isScheduled);
      IsTopNode = true;
    } else {
      do {
        if (BottomQ.empty()) return nullptr;
        SU = BottomQ.top();
        BottomQ.pop();
      } while (SU->isScheduled);
      IsTopNode = false;
    }
    if (IsAlternating)
      IsTopDown = !IsTopDown;
    return SU;
  }

  void schedNode(SUnit *SU, bool IsTopNode) override {}

  void releaseTopNode(SUnit *SU) override {
    TopQ.push(SU);
  }
  void releaseBottomNode(SUnit *SU) override {
    BottomQ.push(SU);
  }
};

} // end anonymous namespace

static ScheduleDAGInstrs *createInstructionShuffler(MachineSchedContext *C) {
  bool Alternate = !ForceTopDown && !ForceBottomUp;
  bool TopDown = !ForceBottomUp;
  assert((TopDown || !ForceTopDown) &&
         "-misched-topdown incompatible with -misched-bottomup");
  return new ScheduleDAGMILive(
      C, llvm::make_unique<InstructionShuffler>(Alternate, TopDown));
}

static MachineSchedRegistry ShufflerRegistry(
  "shuffle", "Shuffle machine instructions alternating directions",
  createInstructionShuffler);
#endif // !NDEBUG

//===----------------------------------------------------------------------===//
// GraphWriter support for ScheduleDAGMILive.
//===----------------------------------------------------------------------===//

#ifndef NDEBUG
namespace llvm {

template<> struct GraphTraits<
  ScheduleDAGMI*> : public GraphTraits<ScheduleDAG*> {};

template<>
struct DOTGraphTraits<ScheduleDAGMI*> : public DefaultDOTGraphTraits {
  DOTGraphTraits(bool isSimple = false) : DefaultDOTGraphTraits(isSimple) {}

  static std::string getGraphName(const ScheduleDAG *G) {
    return G->MF.getName();
  }

  static bool renderGraphFromBottomUp() {
    return true;
  }

  static bool isNodeHidden(const SUnit *Node) {
    if (ViewMISchedCutoff == 0)
      return false;
    return (Node->Preds.size() > ViewMISchedCutoff
         || Node->Succs.size() > ViewMISchedCutoff);
  }

  /// If you want to override the dot attributes printed for a particular
  /// edge, override this method.
  static std::string getEdgeAttributes(const SUnit *Node,
                                       SUnitIterator EI,
                                       const ScheduleDAG *Graph) {
    if (EI.isArtificialDep())
      return "color=cyan,style=dashed";
    if (EI.isCtrlDep())
      return "color=blue,style=dashed";
    return "";
  }

  static std::string getNodeLabel(const SUnit *SU, const ScheduleDAG *G) {
    std::string Str;
    raw_string_ostream SS(Str);
    const ScheduleDAGMI *DAG = static_cast<const ScheduleDAGMI*>(G);
    const SchedDFSResult *DFS = DAG->hasVRegLiveness() ?
      static_cast<const ScheduleDAGMILive*>(G)->getDFSResult() : nullptr;
    SS << "SU:" << SU->NodeNum;
    if (DFS)
      SS << " I:" << DFS->getNumInstrs(SU);
    return SS.str();
  }

  static std::string getNodeDescription(const SUnit *SU, const ScheduleDAG *G) {
    return G->getGraphNodeLabel(SU);
  }

  static std::string getNodeAttributes(const SUnit *N, const ScheduleDAG *G) {
    std::string Str("shape=Mrecord");
    const ScheduleDAGMI *DAG = static_cast<const ScheduleDAGMI*>(G);
    const SchedDFSResult *DFS = DAG->hasVRegLiveness() ?
      static_cast<const ScheduleDAGMILive*>(G)->getDFSResult() : nullptr;
    if (DFS) {
      Str += ",style=filled,fillcolor=\"#";
      Str += DOT::getColorString(DFS->getSubtreeID(N));
      Str += '"';
    }
    return Str;
  }
};

} // end namespace llvm
#endif // NDEBUG

/// viewGraph - Pop up a ghostview window with the reachable parts of the DAG
/// rendered using 'dot'.
void ScheduleDAGMI::viewGraph(const Twine &Name, const Twine &Title) {
#ifndef NDEBUG
  ViewGraph(this, Name, false, Title);
#else
  errs() << "ScheduleDAGMI::viewGraph is only available in debug builds on "
         << "systems with Graphviz or gv!\n";
#endif  // NDEBUG
}

/// Out-of-line implementation with no arguments is handy for gdb.
void ScheduleDAGMI::viewGraph() {
  viewGraph(getDAGName(), "Scheduling-Units Graph for " + getDAGName());
}
