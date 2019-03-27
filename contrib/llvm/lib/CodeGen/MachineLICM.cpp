//===- MachineLICM.cpp - Machine Loop Invariant Code Motion Pass ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass performs loop invariant code motion on machine instructions. We
// attempt to remove as much code from the body of a loop as possible.
//
// This pass is not intended to be a replacement or a complete alternative
// for the LLVM-IR-level LICM pass. It is only designed to hoist simple
// constructs that are not exposed before lowering and instruction selection.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSchedule.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <limits>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "machinelicm"

static cl::opt<bool>
AvoidSpeculation("avoid-speculation",
                 cl::desc("MachineLICM should avoid speculation"),
                 cl::init(true), cl::Hidden);

static cl::opt<bool>
HoistCheapInsts("hoist-cheap-insts",
                cl::desc("MachineLICM should hoist even cheap instructions"),
                cl::init(false), cl::Hidden);

static cl::opt<bool>
SinkInstsToAvoidSpills("sink-insts-to-avoid-spills",
                       cl::desc("MachineLICM should sink instructions into "
                                "loops to avoid register spills"),
                       cl::init(false), cl::Hidden);
static cl::opt<bool>
HoistConstStores("hoist-const-stores",
                 cl::desc("Hoist invariant stores"),
                 cl::init(true), cl::Hidden);

STATISTIC(NumHoisted,
          "Number of machine instructions hoisted out of loops");
STATISTIC(NumLowRP,
          "Number of instructions hoisted in low reg pressure situation");
STATISTIC(NumHighLatency,
          "Number of high latency instructions hoisted");
STATISTIC(NumCSEed,
          "Number of hoisted machine instructions CSEed");
STATISTIC(NumPostRAHoisted,
          "Number of machine instructions hoisted out of loops post regalloc");
STATISTIC(NumStoreConst,
          "Number of stores of const phys reg hoisted out of loops");

namespace {

  class MachineLICMBase : public MachineFunctionPass {
    const TargetInstrInfo *TII;
    const TargetLoweringBase *TLI;
    const TargetRegisterInfo *TRI;
    const MachineFrameInfo *MFI;
    MachineRegisterInfo *MRI;
    TargetSchedModel SchedModel;
    bool PreRegAlloc;

    // Various analyses that we use...
    AliasAnalysis        *AA;      // Alias analysis info.
    MachineLoopInfo      *MLI;     // Current MachineLoopInfo
    MachineDominatorTree *DT;      // Machine dominator tree for the cur loop

    // State that is updated as we process loops
    bool         Changed;          // True if a loop is changed.
    bool         FirstInLoop;      // True if it's the first LICM in the loop.
    MachineLoop *CurLoop;          // The current loop we are working on.
    MachineBasicBlock *CurPreheader; // The preheader for CurLoop.

    // Exit blocks for CurLoop.
    SmallVector<MachineBasicBlock *, 8> ExitBlocks;

    bool isExitBlock(const MachineBasicBlock *MBB) const {
      return is_contained(ExitBlocks, MBB);
    }

    // Track 'estimated' register pressure.
    SmallSet<unsigned, 32> RegSeen;
    SmallVector<unsigned, 8> RegPressure;

    // Register pressure "limit" per register pressure set. If the pressure
    // is higher than the limit, then it's considered high.
    SmallVector<unsigned, 8> RegLimit;

    // Register pressure on path leading from loop preheader to current BB.
    SmallVector<SmallVector<unsigned, 8>, 16> BackTrace;

    // For each opcode, keep a list of potential CSE instructions.
    DenseMap<unsigned, std::vector<const MachineInstr *>> CSEMap;

    enum {
      SpeculateFalse   = 0,
      SpeculateTrue    = 1,
      SpeculateUnknown = 2
    };

    // If a MBB does not dominate loop exiting blocks then it may not safe
    // to hoist loads from this block.
    // Tri-state: 0 - false, 1 - true, 2 - unknown
    unsigned SpeculationState;

  public:
    MachineLICMBase(char &PassID, bool PreRegAlloc)
        : MachineFunctionPass(PassID), PreRegAlloc(PreRegAlloc) {}

    bool runOnMachineFunction(MachineFunction &MF) override;

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<MachineLoopInfo>();
      AU.addRequired<MachineDominatorTree>();
      AU.addRequired<AAResultsWrapperPass>();
      AU.addPreserved<MachineLoopInfo>();
      AU.addPreserved<MachineDominatorTree>();
      MachineFunctionPass::getAnalysisUsage(AU);
    }

    void releaseMemory() override {
      RegSeen.clear();
      RegPressure.clear();
      RegLimit.clear();
      BackTrace.clear();
      CSEMap.clear();
    }

  private:
    /// Keep track of information about hoisting candidates.
    struct CandidateInfo {
      MachineInstr *MI;
      unsigned      Def;
      int           FI;

      CandidateInfo(MachineInstr *mi, unsigned def, int fi)
        : MI(mi), Def(def), FI(fi) {}
    };

    void HoistRegionPostRA();

    void HoistPostRA(MachineInstr *MI, unsigned Def);

    void ProcessMI(MachineInstr *MI, BitVector &PhysRegDefs,
                   BitVector &PhysRegClobbers, SmallSet<int, 32> &StoredFIs,
                   SmallVectorImpl<CandidateInfo> &Candidates);

    void AddToLiveIns(unsigned Reg);

    bool IsLICMCandidate(MachineInstr &I);

    bool IsLoopInvariantInst(MachineInstr &I);

    bool HasLoopPHIUse(const MachineInstr *MI) const;

    bool HasHighOperandLatency(MachineInstr &MI, unsigned DefIdx,
                               unsigned Reg) const;

    bool IsCheapInstruction(MachineInstr &MI) const;

    bool CanCauseHighRegPressure(const DenseMap<unsigned, int> &Cost,
                                 bool Cheap);

    void UpdateBackTraceRegPressure(const MachineInstr *MI);

    bool IsProfitableToHoist(MachineInstr &MI);

    bool IsGuaranteedToExecute(MachineBasicBlock *BB);

    void EnterScope(MachineBasicBlock *MBB);

    void ExitScope(MachineBasicBlock *MBB);

    void ExitScopeIfDone(
        MachineDomTreeNode *Node,
        DenseMap<MachineDomTreeNode *, unsigned> &OpenChildren,
        DenseMap<MachineDomTreeNode *, MachineDomTreeNode *> &ParentMap);

    void HoistOutOfLoop(MachineDomTreeNode *HeaderN);

    void HoistRegion(MachineDomTreeNode *N, bool IsHeader);

    void SinkIntoLoop();

    void InitRegPressure(MachineBasicBlock *BB);

    DenseMap<unsigned, int> calcRegisterCost(const MachineInstr *MI,
                                             bool ConsiderSeen,
                                             bool ConsiderUnseenAsDef);

    void UpdateRegPressure(const MachineInstr *MI,
                           bool ConsiderUnseenAsDef = false);

    MachineInstr *ExtractHoistableLoad(MachineInstr *MI);

    const MachineInstr *
    LookForDuplicate(const MachineInstr *MI,
                     std::vector<const MachineInstr *> &PrevMIs);

    bool EliminateCSE(
        MachineInstr *MI,
        DenseMap<unsigned, std::vector<const MachineInstr *>>::iterator &CI);

    bool MayCSE(MachineInstr *MI);

    bool Hoist(MachineInstr *MI, MachineBasicBlock *Preheader);

    void InitCSEMap(MachineBasicBlock *BB);

    MachineBasicBlock *getCurPreheader();
  };

  class MachineLICM : public MachineLICMBase {
  public:
    static char ID;
    MachineLICM() : MachineLICMBase(ID, false) {
      initializeMachineLICMPass(*PassRegistry::getPassRegistry());
    }
  };

  class EarlyMachineLICM : public MachineLICMBase {
  public:
    static char ID;
    EarlyMachineLICM() : MachineLICMBase(ID, true) {
      initializeEarlyMachineLICMPass(*PassRegistry::getPassRegistry());
    }
  };

} // end anonymous namespace

char MachineLICM::ID;
char EarlyMachineLICM::ID;

char &llvm::MachineLICMID = MachineLICM::ID;
char &llvm::EarlyMachineLICMID = EarlyMachineLICM::ID;

INITIALIZE_PASS_BEGIN(MachineLICM, DEBUG_TYPE,
                      "Machine Loop Invariant Code Motion", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfo)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_END(MachineLICM, DEBUG_TYPE,
                    "Machine Loop Invariant Code Motion", false, false)

INITIALIZE_PASS_BEGIN(EarlyMachineLICM, "early-machinelicm",
                      "Early Machine Loop Invariant Code Motion", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfo)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_END(EarlyMachineLICM, "early-machinelicm",
                    "Early Machine Loop Invariant Code Motion", false, false)

/// Test if the given loop is the outer-most loop that has a unique predecessor.
static bool LoopIsOuterMostWithPredecessor(MachineLoop *CurLoop) {
  // Check whether this loop even has a unique predecessor.
  if (!CurLoop->getLoopPredecessor())
    return false;
  // Ok, now check to see if any of its outer loops do.
  for (MachineLoop *L = CurLoop->getParentLoop(); L; L = L->getParentLoop())
    if (L->getLoopPredecessor())
      return false;
  // None of them did, so this is the outermost with a unique predecessor.
  return true;
}

bool MachineLICMBase::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  Changed = FirstInLoop = false;
  const TargetSubtargetInfo &ST = MF.getSubtarget();
  TII = ST.getInstrInfo();
  TLI = ST.getTargetLowering();
  TRI = ST.getRegisterInfo();
  MFI = &MF.getFrameInfo();
  MRI = &MF.getRegInfo();
  SchedModel.init(&ST);

  PreRegAlloc = MRI->isSSA();

  if (PreRegAlloc)
    LLVM_DEBUG(dbgs() << "******** Pre-regalloc Machine LICM: ");
  else
    LLVM_DEBUG(dbgs() << "******** Post-regalloc Machine LICM: ");
  LLVM_DEBUG(dbgs() << MF.getName() << " ********\n");

  if (PreRegAlloc) {
    // Estimate register pressure during pre-regalloc pass.
    unsigned NumRPS = TRI->getNumRegPressureSets();
    RegPressure.resize(NumRPS);
    std::fill(RegPressure.begin(), RegPressure.end(), 0);
    RegLimit.resize(NumRPS);
    for (unsigned i = 0, e = NumRPS; i != e; ++i)
      RegLimit[i] = TRI->getRegPressureSetLimit(MF, i);
  }

  // Get our Loop information...
  MLI = &getAnalysis<MachineLoopInfo>();
  DT  = &getAnalysis<MachineDominatorTree>();
  AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();

  SmallVector<MachineLoop *, 8> Worklist(MLI->begin(), MLI->end());
  while (!Worklist.empty()) {
    CurLoop = Worklist.pop_back_val();
    CurPreheader = nullptr;
    ExitBlocks.clear();

    // If this is done before regalloc, only visit outer-most preheader-sporting
    // loops.
    if (PreRegAlloc && !LoopIsOuterMostWithPredecessor(CurLoop)) {
      Worklist.append(CurLoop->begin(), CurLoop->end());
      continue;
    }

    CurLoop->getExitBlocks(ExitBlocks);

    if (!PreRegAlloc)
      HoistRegionPostRA();
    else {
      // CSEMap is initialized for loop header when the first instruction is
      // being hoisted.
      MachineDomTreeNode *N = DT->getNode(CurLoop->getHeader());
      FirstInLoop = true;
      HoistOutOfLoop(N);
      CSEMap.clear();

      if (SinkInstsToAvoidSpills)
        SinkIntoLoop();
    }
  }

  return Changed;
}

/// Return true if instruction stores to the specified frame.
static bool InstructionStoresToFI(const MachineInstr *MI, int FI) {
  // Check mayStore before memory operands so that e.g. DBG_VALUEs will return
  // true since they have no memory operands.
  if (!MI->mayStore())
     return false;
  // If we lost memory operands, conservatively assume that the instruction
  // writes to all slots.
  if (MI->memoperands_empty())
    return true;
  for (const MachineMemOperand *MemOp : MI->memoperands()) {
    if (!MemOp->isStore() || !MemOp->getPseudoValue())
      continue;
    if (const FixedStackPseudoSourceValue *Value =
        dyn_cast<FixedStackPseudoSourceValue>(MemOp->getPseudoValue())) {
      if (Value->getFrameIndex() == FI)
        return true;
    }
  }
  return false;
}

/// Examine the instruction for potentai LICM candidate. Also
/// gather register def and frame object update information.
void MachineLICMBase::ProcessMI(MachineInstr *MI,
                                BitVector &PhysRegDefs,
                                BitVector &PhysRegClobbers,
                                SmallSet<int, 32> &StoredFIs,
                                SmallVectorImpl<CandidateInfo> &Candidates) {
  bool RuledOut = false;
  bool HasNonInvariantUse = false;
  unsigned Def = 0;
  for (const MachineOperand &MO : MI->operands()) {
    if (MO.isFI()) {
      // Remember if the instruction stores to the frame index.
      int FI = MO.getIndex();
      if (!StoredFIs.count(FI) &&
          MFI->isSpillSlotObjectIndex(FI) &&
          InstructionStoresToFI(MI, FI))
        StoredFIs.insert(FI);
      HasNonInvariantUse = true;
      continue;
    }

    // We can't hoist an instruction defining a physreg that is clobbered in
    // the loop.
    if (MO.isRegMask()) {
      PhysRegClobbers.setBitsNotInMask(MO.getRegMask());
      continue;
    }

    if (!MO.isReg())
      continue;
    unsigned Reg = MO.getReg();
    if (!Reg)
      continue;
    assert(TargetRegisterInfo::isPhysicalRegister(Reg) &&
           "Not expecting virtual register!");

    if (!MO.isDef()) {
      if (Reg && (PhysRegDefs.test(Reg) || PhysRegClobbers.test(Reg)))
        // If it's using a non-loop-invariant register, then it's obviously not
        // safe to hoist.
        HasNonInvariantUse = true;
      continue;
    }

    if (MO.isImplicit()) {
      for (MCRegAliasIterator AI(Reg, TRI, true); AI.isValid(); ++AI)
        PhysRegClobbers.set(*AI);
      if (!MO.isDead())
        // Non-dead implicit def? This cannot be hoisted.
        RuledOut = true;
      // No need to check if a dead implicit def is also defined by
      // another instruction.
      continue;
    }

    // FIXME: For now, avoid instructions with multiple defs, unless
    // it's a dead implicit def.
    if (Def)
      RuledOut = true;
    else
      Def = Reg;

    // If we have already seen another instruction that defines the same
    // register, then this is not safe.  Two defs is indicated by setting a
    // PhysRegClobbers bit.
    for (MCRegAliasIterator AS(Reg, TRI, true); AS.isValid(); ++AS) {
      if (PhysRegDefs.test(*AS))
        PhysRegClobbers.set(*AS);
    }
    // Need a second loop because MCRegAliasIterator can visit the same
    // register twice.
    for (MCRegAliasIterator AS(Reg, TRI, true); AS.isValid(); ++AS)
      PhysRegDefs.set(*AS);

    if (PhysRegClobbers.test(Reg))
      // MI defined register is seen defined by another instruction in
      // the loop, it cannot be a LICM candidate.
      RuledOut = true;
  }

  // Only consider reloads for now and remats which do not have register
  // operands. FIXME: Consider unfold load folding instructions.
  if (Def && !RuledOut) {
    int FI = std::numeric_limits<int>::min();
    if ((!HasNonInvariantUse && IsLICMCandidate(*MI)) ||
        (TII->isLoadFromStackSlot(*MI, FI) && MFI->isSpillSlotObjectIndex(FI)))
      Candidates.push_back(CandidateInfo(MI, Def, FI));
  }
}

/// Walk the specified region of the CFG and hoist loop invariants out to the
/// preheader.
void MachineLICMBase::HoistRegionPostRA() {
  MachineBasicBlock *Preheader = getCurPreheader();
  if (!Preheader)
    return;

  unsigned NumRegs = TRI->getNumRegs();
  BitVector PhysRegDefs(NumRegs); // Regs defined once in the loop.
  BitVector PhysRegClobbers(NumRegs); // Regs defined more than once.

  SmallVector<CandidateInfo, 32> Candidates;
  SmallSet<int, 32> StoredFIs;

  // Walk the entire region, count number of defs for each register, and
  // collect potential LICM candidates.
  for (MachineBasicBlock *BB : CurLoop->getBlocks()) {
    // If the header of the loop containing this basic block is a landing pad,
    // then don't try to hoist instructions out of this loop.
    const MachineLoop *ML = MLI->getLoopFor(BB);
    if (ML && ML->getHeader()->isEHPad()) continue;

    // Conservatively treat live-in's as an external def.
    // FIXME: That means a reload that're reused in successor block(s) will not
    // be LICM'ed.
    for (const auto &LI : BB->liveins()) {
      for (MCRegAliasIterator AI(LI.PhysReg, TRI, true); AI.isValid(); ++AI)
        PhysRegDefs.set(*AI);
    }

    SpeculationState = SpeculateUnknown;
    for (MachineInstr &MI : *BB)
      ProcessMI(&MI, PhysRegDefs, PhysRegClobbers, StoredFIs, Candidates);
  }

  // Gather the registers read / clobbered by the terminator.
  BitVector TermRegs(NumRegs);
  MachineBasicBlock::iterator TI = Preheader->getFirstTerminator();
  if (TI != Preheader->end()) {
    for (const MachineOperand &MO : TI->operands()) {
      if (!MO.isReg())
        continue;
      unsigned Reg = MO.getReg();
      if (!Reg)
        continue;
      for (MCRegAliasIterator AI(Reg, TRI, true); AI.isValid(); ++AI)
        TermRegs.set(*AI);
    }
  }

  // Now evaluate whether the potential candidates qualify.
  // 1. Check if the candidate defined register is defined by another
  //    instruction in the loop.
  // 2. If the candidate is a load from stack slot (always true for now),
  //    check if the slot is stored anywhere in the loop.
  // 3. Make sure candidate def should not clobber
  //    registers read by the terminator. Similarly its def should not be
  //    clobbered by the terminator.
  for (CandidateInfo &Candidate : Candidates) {
    if (Candidate.FI != std::numeric_limits<int>::min() &&
        StoredFIs.count(Candidate.FI))
      continue;

    unsigned Def = Candidate.Def;
    if (!PhysRegClobbers.test(Def) && !TermRegs.test(Def)) {
      bool Safe = true;
      MachineInstr *MI = Candidate.MI;
      for (const MachineOperand &MO : MI->operands()) {
        if (!MO.isReg() || MO.isDef() || !MO.getReg())
          continue;
        unsigned Reg = MO.getReg();
        if (PhysRegDefs.test(Reg) ||
            PhysRegClobbers.test(Reg)) {
          // If it's using a non-loop-invariant register, then it's obviously
          // not safe to hoist.
          Safe = false;
          break;
        }
      }
      if (Safe)
        HoistPostRA(MI, Candidate.Def);
    }
  }
}

/// Add register 'Reg' to the livein sets of BBs in the current loop, and make
/// sure it is not killed by any instructions in the loop.
void MachineLICMBase::AddToLiveIns(unsigned Reg) {
  for (MachineBasicBlock *BB : CurLoop->getBlocks()) {
    if (!BB->isLiveIn(Reg))
      BB->addLiveIn(Reg);
    for (MachineInstr &MI : *BB) {
      for (MachineOperand &MO : MI.operands()) {
        if (!MO.isReg() || !MO.getReg() || MO.isDef()) continue;
        if (MO.getReg() == Reg || TRI->isSuperRegister(Reg, MO.getReg()))
          MO.setIsKill(false);
      }
    }
  }
}

/// When an instruction is found to only use loop invariant operands that is
/// safe to hoist, this instruction is called to do the dirty work.
void MachineLICMBase::HoistPostRA(MachineInstr *MI, unsigned Def) {
  MachineBasicBlock *Preheader = getCurPreheader();

  // Now move the instructions to the predecessor, inserting it before any
  // terminator instructions.
  LLVM_DEBUG(dbgs() << "Hoisting to " << printMBBReference(*Preheader)
                    << " from " << printMBBReference(*MI->getParent()) << ": "
                    << *MI);

  // Splice the instruction to the preheader.
  MachineBasicBlock *MBB = MI->getParent();
  Preheader->splice(Preheader->getFirstTerminator(), MBB, MI);

  // Add register to livein list to all the BBs in the current loop since a
  // loop invariant must be kept live throughout the whole loop. This is
  // important to ensure later passes do not scavenge the def register.
  AddToLiveIns(Def);

  ++NumPostRAHoisted;
  Changed = true;
}

/// Check if this mbb is guaranteed to execute. If not then a load from this mbb
/// may not be safe to hoist.
bool MachineLICMBase::IsGuaranteedToExecute(MachineBasicBlock *BB) {
  if (SpeculationState != SpeculateUnknown)
    return SpeculationState == SpeculateFalse;

  if (BB != CurLoop->getHeader()) {
    // Check loop exiting blocks.
    SmallVector<MachineBasicBlock*, 8> CurrentLoopExitingBlocks;
    CurLoop->getExitingBlocks(CurrentLoopExitingBlocks);
    for (MachineBasicBlock *CurrentLoopExitingBlock : CurrentLoopExitingBlocks)
      if (!DT->dominates(BB, CurrentLoopExitingBlock)) {
        SpeculationState = SpeculateTrue;
        return false;
      }
  }

  SpeculationState = SpeculateFalse;
  return true;
}

void MachineLICMBase::EnterScope(MachineBasicBlock *MBB) {
  LLVM_DEBUG(dbgs() << "Entering " << printMBBReference(*MBB) << '\n');

  // Remember livein register pressure.
  BackTrace.push_back(RegPressure);
}

void MachineLICMBase::ExitScope(MachineBasicBlock *MBB) {
  LLVM_DEBUG(dbgs() << "Exiting " << printMBBReference(*MBB) << '\n');
  BackTrace.pop_back();
}

/// Destroy scope for the MBB that corresponds to the given dominator tree node
/// if its a leaf or all of its children are done. Walk up the dominator tree to
/// destroy ancestors which are now done.
void MachineLICMBase::ExitScopeIfDone(MachineDomTreeNode *Node,
    DenseMap<MachineDomTreeNode*, unsigned> &OpenChildren,
    DenseMap<MachineDomTreeNode*, MachineDomTreeNode*> &ParentMap) {
  if (OpenChildren[Node])
    return;

  // Pop scope.
  ExitScope(Node->getBlock());

  // Now traverse upwards to pop ancestors whose offsprings are all done.
  while (MachineDomTreeNode *Parent = ParentMap[Node]) {
    unsigned Left = --OpenChildren[Parent];
    if (Left != 0)
      break;
    ExitScope(Parent->getBlock());
    Node = Parent;
  }
}

/// Walk the specified loop in the CFG (defined by all blocks dominated by the
/// specified header block, and that are in the current loop) in depth first
/// order w.r.t the DominatorTree. This allows us to visit definitions before
/// uses, allowing us to hoist a loop body in one pass without iteration.
void MachineLICMBase::HoistOutOfLoop(MachineDomTreeNode *HeaderN) {
  MachineBasicBlock *Preheader = getCurPreheader();
  if (!Preheader)
    return;

  SmallVector<MachineDomTreeNode*, 32> Scopes;
  SmallVector<MachineDomTreeNode*, 8> WorkList;
  DenseMap<MachineDomTreeNode*, MachineDomTreeNode*> ParentMap;
  DenseMap<MachineDomTreeNode*, unsigned> OpenChildren;

  // Perform a DFS walk to determine the order of visit.
  WorkList.push_back(HeaderN);
  while (!WorkList.empty()) {
    MachineDomTreeNode *Node = WorkList.pop_back_val();
    assert(Node && "Null dominator tree node?");
    MachineBasicBlock *BB = Node->getBlock();

    // If the header of the loop containing this basic block is a landing pad,
    // then don't try to hoist instructions out of this loop.
    const MachineLoop *ML = MLI->getLoopFor(BB);
    if (ML && ML->getHeader()->isEHPad())
      continue;

    // If this subregion is not in the top level loop at all, exit.
    if (!CurLoop->contains(BB))
      continue;

    Scopes.push_back(Node);
    const std::vector<MachineDomTreeNode*> &Children = Node->getChildren();
    unsigned NumChildren = Children.size();

    // Don't hoist things out of a large switch statement.  This often causes
    // code to be hoisted that wasn't going to be executed, and increases
    // register pressure in a situation where it's likely to matter.
    if (BB->succ_size() >= 25)
      NumChildren = 0;

    OpenChildren[Node] = NumChildren;
    // Add children in reverse order as then the next popped worklist node is
    // the first child of this node.  This means we ultimately traverse the
    // DOM tree in exactly the same order as if we'd recursed.
    for (int i = (int)NumChildren-1; i >= 0; --i) {
      MachineDomTreeNode *Child = Children[i];
      ParentMap[Child] = Node;
      WorkList.push_back(Child);
    }
  }

  if (Scopes.size() == 0)
    return;

  // Compute registers which are livein into the loop headers.
  RegSeen.clear();
  BackTrace.clear();
  InitRegPressure(Preheader);

  // Now perform LICM.
  for (MachineDomTreeNode *Node : Scopes) {
    MachineBasicBlock *MBB = Node->getBlock();

    EnterScope(MBB);

    // Process the block
    SpeculationState = SpeculateUnknown;
    for (MachineBasicBlock::iterator
         MII = MBB->begin(), E = MBB->end(); MII != E; ) {
      MachineBasicBlock::iterator NextMII = MII; ++NextMII;
      MachineInstr *MI = &*MII;
      if (!Hoist(MI, Preheader))
        UpdateRegPressure(MI);
      // If we have hoisted an instruction that may store, it can only be a
      // constant store.
      MII = NextMII;
    }

    // If it's a leaf node, it's done. Traverse upwards to pop ancestors.
    ExitScopeIfDone(Node, OpenChildren, ParentMap);
  }
}

/// Sink instructions into loops if profitable. This especially tries to prevent
/// register spills caused by register pressure if there is little to no
/// overhead moving instructions into loops.
void MachineLICMBase::SinkIntoLoop() {
  MachineBasicBlock *Preheader = getCurPreheader();
  if (!Preheader)
    return;

  SmallVector<MachineInstr *, 8> Candidates;
  for (MachineBasicBlock::instr_iterator I = Preheader->instr_begin();
       I != Preheader->instr_end(); ++I) {
    // We need to ensure that we can safely move this instruction into the loop.
    // As such, it must not have side-effects, e.g. such as a call has.
    if (IsLoopInvariantInst(*I) && !HasLoopPHIUse(&*I))
      Candidates.push_back(&*I);
  }

  for (MachineInstr *I : Candidates) {
    const MachineOperand &MO = I->getOperand(0);
    if (!MO.isDef() || !MO.isReg() || !MO.getReg())
      continue;
    if (!MRI->hasOneDef(MO.getReg()))
      continue;
    bool CanSink = true;
    MachineBasicBlock *B = nullptr;
    for (MachineInstr &MI : MRI->use_instructions(MO.getReg())) {
      // FIXME: Come up with a proper cost model that estimates whether sinking
      // the instruction (and thus possibly executing it on every loop
      // iteration) is more expensive than a register.
      // For now assumes that copies are cheap and thus almost always worth it.
      if (!MI.isCopy()) {
        CanSink = false;
        break;
      }
      if (!B) {
        B = MI.getParent();
        continue;
      }
      B = DT->findNearestCommonDominator(B, MI.getParent());
      if (!B) {
        CanSink = false;
        break;
      }
    }
    if (!CanSink || !B || B == Preheader)
      continue;
    B->splice(B->getFirstNonPHI(), Preheader, I);
  }
}

static bool isOperandKill(const MachineOperand &MO, MachineRegisterInfo *MRI) {
  return MO.isKill() || MRI->hasOneNonDBGUse(MO.getReg());
}

/// Find all virtual register references that are liveout of the preheader to
/// initialize the starting "register pressure". Note this does not count live
/// through (livein but not used) registers.
void MachineLICMBase::InitRegPressure(MachineBasicBlock *BB) {
  std::fill(RegPressure.begin(), RegPressure.end(), 0);

  // If the preheader has only a single predecessor and it ends with a
  // fallthrough or an unconditional branch, then scan its predecessor for live
  // defs as well. This happens whenever the preheader is created by splitting
  // the critical edge from the loop predecessor to the loop header.
  if (BB->pred_size() == 1) {
    MachineBasicBlock *TBB = nullptr, *FBB = nullptr;
    SmallVector<MachineOperand, 4> Cond;
    if (!TII->analyzeBranch(*BB, TBB, FBB, Cond, false) && Cond.empty())
      InitRegPressure(*BB->pred_begin());
  }

  for (const MachineInstr &MI : *BB)
    UpdateRegPressure(&MI, /*ConsiderUnseenAsDef=*/true);
}

/// Update estimate of register pressure after the specified instruction.
void MachineLICMBase::UpdateRegPressure(const MachineInstr *MI,
                                        bool ConsiderUnseenAsDef) {
  auto Cost = calcRegisterCost(MI, /*ConsiderSeen=*/true, ConsiderUnseenAsDef);
  for (const auto &RPIdAndCost : Cost) {
    unsigned Class = RPIdAndCost.first;
    if (static_cast<int>(RegPressure[Class]) < -RPIdAndCost.second)
      RegPressure[Class] = 0;
    else
      RegPressure[Class] += RPIdAndCost.second;
  }
}

/// Calculate the additional register pressure that the registers used in MI
/// cause.
///
/// If 'ConsiderSeen' is true, updates 'RegSeen' and uses the information to
/// figure out which usages are live-ins.
/// FIXME: Figure out a way to consider 'RegSeen' from all code paths.
DenseMap<unsigned, int>
MachineLICMBase::calcRegisterCost(const MachineInstr *MI, bool ConsiderSeen,
                                  bool ConsiderUnseenAsDef) {
  DenseMap<unsigned, int> Cost;
  if (MI->isImplicitDef())
    return Cost;
  for (unsigned i = 0, e = MI->getDesc().getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = MI->getOperand(i);
    if (!MO.isReg() || MO.isImplicit())
      continue;
    unsigned Reg = MO.getReg();
    if (!TargetRegisterInfo::isVirtualRegister(Reg))
      continue;

    // FIXME: It seems bad to use RegSeen only for some of these calculations.
    bool isNew = ConsiderSeen ? RegSeen.insert(Reg).second : false;
    const TargetRegisterClass *RC = MRI->getRegClass(Reg);

    RegClassWeight W = TRI->getRegClassWeight(RC);
    int RCCost = 0;
    if (MO.isDef())
      RCCost = W.RegWeight;
    else {
      bool isKill = isOperandKill(MO, MRI);
      if (isNew && !isKill && ConsiderUnseenAsDef)
        // Haven't seen this, it must be a livein.
        RCCost = W.RegWeight;
      else if (!isNew && isKill)
        RCCost = -W.RegWeight;
    }
    if (RCCost == 0)
      continue;
    const int *PS = TRI->getRegClassPressureSets(RC);
    for (; *PS != -1; ++PS) {
      if (Cost.find(*PS) == Cost.end())
        Cost[*PS] = RCCost;
      else
        Cost[*PS] += RCCost;
    }
  }
  return Cost;
}

/// Return true if this machine instruction loads from global offset table or
/// constant pool.
static bool mayLoadFromGOTOrConstantPool(MachineInstr &MI) {
  assert(MI.mayLoad() && "Expected MI that loads!");

  // If we lost memory operands, conservatively assume that the instruction
  // reads from everything..
  if (MI.memoperands_empty())
    return true;

  for (MachineMemOperand *MemOp : MI.memoperands())
    if (const PseudoSourceValue *PSV = MemOp->getPseudoValue())
      if (PSV->isGOT() || PSV->isConstantPool())
        return true;

  return false;
}

// This function iterates through all the operands of the input store MI and
// checks that each register operand statisfies isCallerPreservedPhysReg.
// This means, the value being stored and the address where it is being stored
// is constant throughout the body of the function (not including prologue and
// epilogue). When called with an MI that isn't a store, it returns false.
// A future improvement can be to check if the store registers are constant
// throughout the loop rather than throughout the funtion.
static bool isInvariantStore(const MachineInstr &MI,
                             const TargetRegisterInfo *TRI,
                             const MachineRegisterInfo *MRI) {

  bool FoundCallerPresReg = false;
  if (!MI.mayStore() || MI.hasUnmodeledSideEffects() ||
      (MI.getNumOperands() == 0))
    return false;

  // Check that all register operands are caller-preserved physical registers.
  for (const MachineOperand &MO : MI.operands()) {
    if (MO.isReg()) {
      unsigned Reg = MO.getReg();
      // If operand is a virtual register, check if it comes from a copy of a
      // physical register.
      if (TargetRegisterInfo::isVirtualRegister(Reg))
        Reg = TRI->lookThruCopyLike(MO.getReg(), MRI);
      if (TargetRegisterInfo::isVirtualRegister(Reg))
        return false;
      if (!TRI->isCallerPreservedPhysReg(Reg, *MI.getMF()))
        return false;
      else
        FoundCallerPresReg = true;
    } else if (!MO.isImm()) {
        return false;
    }
  }
  return FoundCallerPresReg;
}

// Return true if the input MI is a copy instruction that feeds an invariant
// store instruction. This means that the src of the copy has to satisfy
// isCallerPreservedPhysReg and atleast one of it's users should satisfy
// isInvariantStore.
static bool isCopyFeedingInvariantStore(const MachineInstr &MI,
                                        const MachineRegisterInfo *MRI,
                                        const TargetRegisterInfo *TRI) {

  // FIXME: If targets would like to look through instructions that aren't
  // pure copies, this can be updated to a query.
  if (!MI.isCopy())
    return false;

  const MachineFunction *MF = MI.getMF();
  // Check that we are copying a constant physical register.
  unsigned CopySrcReg = MI.getOperand(1).getReg();
  if (TargetRegisterInfo::isVirtualRegister(CopySrcReg))
    return false;

  if (!TRI->isCallerPreservedPhysReg(CopySrcReg, *MF))
    return false;

  unsigned CopyDstReg = MI.getOperand(0).getReg();
  // Check if any of the uses of the copy are invariant stores.
  assert (TargetRegisterInfo::isVirtualRegister(CopyDstReg) &&
          "copy dst is not a virtual reg");

  for (MachineInstr &UseMI : MRI->use_instructions(CopyDstReg)) {
    if (UseMI.mayStore() && isInvariantStore(UseMI, TRI, MRI))
      return true;
  }
  return false;
}

/// Returns true if the instruction may be a suitable candidate for LICM.
/// e.g. If the instruction is a call, then it's obviously not safe to hoist it.
bool MachineLICMBase::IsLICMCandidate(MachineInstr &I) {
  // Check if it's safe to move the instruction.
  bool DontMoveAcrossStore = true;
  if ((!I.isSafeToMove(AA, DontMoveAcrossStore)) &&
      !(HoistConstStores && isInvariantStore(I, TRI, MRI))) {
    return false;
  }

  // If it is load then check if it is guaranteed to execute by making sure that
  // it dominates all exiting blocks. If it doesn't, then there is a path out of
  // the loop which does not execute this load, so we can't hoist it. Loads
  // from constant memory are not safe to speculate all the time, for example
  // indexed load from a jump table.
  // Stores and side effects are already checked by isSafeToMove.
  if (I.mayLoad() && !mayLoadFromGOTOrConstantPool(I) &&
      !IsGuaranteedToExecute(I.getParent()))
    return false;

  return true;
}

/// Returns true if the instruction is loop invariant.
/// I.e., all virtual register operands are defined outside of the loop,
/// physical registers aren't accessed explicitly, and there are no side
/// effects that aren't captured by the operands or other flags.
bool MachineLICMBase::IsLoopInvariantInst(MachineInstr &I) {
  if (!IsLICMCandidate(I))
    return false;

  // The instruction is loop invariant if all of its operands are.
  for (const MachineOperand &MO : I.operands()) {
    if (!MO.isReg())
      continue;

    unsigned Reg = MO.getReg();
    if (Reg == 0) continue;

    // Don't hoist an instruction that uses or defines a physical register.
    if (TargetRegisterInfo::isPhysicalRegister(Reg)) {
      if (MO.isUse()) {
        // If the physreg has no defs anywhere, it's just an ambient register
        // and we can freely move its uses. Alternatively, if it's allocatable,
        // it could get allocated to something with a def during allocation.
        // However, if the physreg is known to always be caller saved/restored
        // then this use is safe to hoist.
        if (!MRI->isConstantPhysReg(Reg) &&
            !(TRI->isCallerPreservedPhysReg(Reg, *I.getMF())))
          return false;
        // Otherwise it's safe to move.
        continue;
      } else if (!MO.isDead()) {
        // A def that isn't dead. We can't move it.
        return false;
      } else if (CurLoop->getHeader()->isLiveIn(Reg)) {
        // If the reg is live into the loop, we can't hoist an instruction
        // which would clobber it.
        return false;
      }
    }

    if (!MO.isUse())
      continue;

    assert(MRI->getVRegDef(Reg) &&
           "Machine instr not mapped for this vreg?!");

    // If the loop contains the definition of an operand, then the instruction
    // isn't loop invariant.
    if (CurLoop->contains(MRI->getVRegDef(Reg)))
      return false;
  }

  // If we got this far, the instruction is loop invariant!
  return true;
}

/// Return true if the specified instruction is used by a phi node and hoisting
/// it could cause a copy to be inserted.
bool MachineLICMBase::HasLoopPHIUse(const MachineInstr *MI) const {
  SmallVector<const MachineInstr*, 8> Work(1, MI);
  do {
    MI = Work.pop_back_val();
    for (const MachineOperand &MO : MI->operands()) {
      if (!MO.isReg() || !MO.isDef())
        continue;
      unsigned Reg = MO.getReg();
      if (!TargetRegisterInfo::isVirtualRegister(Reg))
        continue;
      for (MachineInstr &UseMI : MRI->use_instructions(Reg)) {
        // A PHI may cause a copy to be inserted.
        if (UseMI.isPHI()) {
          // A PHI inside the loop causes a copy because the live range of Reg is
          // extended across the PHI.
          if (CurLoop->contains(&UseMI))
            return true;
          // A PHI in an exit block can cause a copy to be inserted if the PHI
          // has multiple predecessors in the loop with different values.
          // For now, approximate by rejecting all exit blocks.
          if (isExitBlock(UseMI.getParent()))
            return true;
          continue;
        }
        // Look past copies as well.
        if (UseMI.isCopy() && CurLoop->contains(&UseMI))
          Work.push_back(&UseMI);
      }
    }
  } while (!Work.empty());
  return false;
}

/// Compute operand latency between a def of 'Reg' and an use in the current
/// loop, return true if the target considered it high.
bool MachineLICMBase::HasHighOperandLatency(MachineInstr &MI,
                                            unsigned DefIdx,
                                            unsigned Reg) const {
  if (MRI->use_nodbg_empty(Reg))
    return false;

  for (MachineInstr &UseMI : MRI->use_nodbg_instructions(Reg)) {
    if (UseMI.isCopyLike())
      continue;
    if (!CurLoop->contains(UseMI.getParent()))
      continue;
    for (unsigned i = 0, e = UseMI.getNumOperands(); i != e; ++i) {
      const MachineOperand &MO = UseMI.getOperand(i);
      if (!MO.isReg() || !MO.isUse())
        continue;
      unsigned MOReg = MO.getReg();
      if (MOReg != Reg)
        continue;

      if (TII->hasHighOperandLatency(SchedModel, MRI, MI, DefIdx, UseMI, i))
        return true;
    }

    // Only look at the first in loop use.
    break;
  }

  return false;
}

/// Return true if the instruction is marked "cheap" or the operand latency
/// between its def and a use is one or less.
bool MachineLICMBase::IsCheapInstruction(MachineInstr &MI) const {
  if (TII->isAsCheapAsAMove(MI) || MI.isCopyLike())
    return true;

  bool isCheap = false;
  unsigned NumDefs = MI.getDesc().getNumDefs();
  for (unsigned i = 0, e = MI.getNumOperands(); NumDefs && i != e; ++i) {
    MachineOperand &DefMO = MI.getOperand(i);
    if (!DefMO.isReg() || !DefMO.isDef())
      continue;
    --NumDefs;
    unsigned Reg = DefMO.getReg();
    if (TargetRegisterInfo::isPhysicalRegister(Reg))
      continue;

    if (!TII->hasLowDefLatency(SchedModel, MI, i))
      return false;
    isCheap = true;
  }

  return isCheap;
}

/// Visit BBs from header to current BB, check if hoisting an instruction of the
/// given cost matrix can cause high register pressure.
bool
MachineLICMBase::CanCauseHighRegPressure(const DenseMap<unsigned, int>& Cost,
                                         bool CheapInstr) {
  for (const auto &RPIdAndCost : Cost) {
    if (RPIdAndCost.second <= 0)
      continue;

    unsigned Class = RPIdAndCost.first;
    int Limit = RegLimit[Class];

    // Don't hoist cheap instructions if they would increase register pressure,
    // even if we're under the limit.
    if (CheapInstr && !HoistCheapInsts)
      return true;

    for (const auto &RP : BackTrace)
      if (static_cast<int>(RP[Class]) + RPIdAndCost.second >= Limit)
        return true;
  }

  return false;
}

/// Traverse the back trace from header to the current block and update their
/// register pressures to reflect the effect of hoisting MI from the current
/// block to the preheader.
void MachineLICMBase::UpdateBackTraceRegPressure(const MachineInstr *MI) {
  // First compute the 'cost' of the instruction, i.e. its contribution
  // to register pressure.
  auto Cost = calcRegisterCost(MI, /*ConsiderSeen=*/false,
                               /*ConsiderUnseenAsDef=*/false);

  // Update register pressure of blocks from loop header to current block.
  for (auto &RP : BackTrace)
    for (const auto &RPIdAndCost : Cost)
      RP[RPIdAndCost.first] += RPIdAndCost.second;
}

/// Return true if it is potentially profitable to hoist the given loop
/// invariant.
bool MachineLICMBase::IsProfitableToHoist(MachineInstr &MI) {
  if (MI.isImplicitDef())
    return true;

  // Besides removing computation from the loop, hoisting an instruction has
  // these effects:
  //
  // - The value defined by the instruction becomes live across the entire
  //   loop. This increases register pressure in the loop.
  //
  // - If the value is used by a PHI in the loop, a copy will be required for
  //   lowering the PHI after extending the live range.
  //
  // - When hoisting the last use of a value in the loop, that value no longer
  //   needs to be live in the loop. This lowers register pressure in the loop.

  if (HoistConstStores &&  isCopyFeedingInvariantStore(MI, MRI, TRI))
    return true;

  bool CheapInstr = IsCheapInstruction(MI);
  bool CreatesCopy = HasLoopPHIUse(&MI);

  // Don't hoist a cheap instruction if it would create a copy in the loop.
  if (CheapInstr && CreatesCopy) {
    LLVM_DEBUG(dbgs() << "Won't hoist cheap instr with loop PHI use: " << MI);
    return false;
  }

  // Rematerializable instructions should always be hoisted since the register
  // allocator can just pull them down again when needed.
  if (TII->isTriviallyReMaterializable(MI, AA))
    return true;

  // FIXME: If there are long latency loop-invariant instructions inside the
  // loop at this point, why didn't the optimizer's LICM hoist them?
  for (unsigned i = 0, e = MI.getDesc().getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = MI.getOperand(i);
    if (!MO.isReg() || MO.isImplicit())
      continue;
    unsigned Reg = MO.getReg();
    if (!TargetRegisterInfo::isVirtualRegister(Reg))
      continue;
    if (MO.isDef() && HasHighOperandLatency(MI, i, Reg)) {
      LLVM_DEBUG(dbgs() << "Hoist High Latency: " << MI);
      ++NumHighLatency;
      return true;
    }
  }

  // Estimate register pressure to determine whether to LICM the instruction.
  // In low register pressure situation, we can be more aggressive about
  // hoisting. Also, favors hoisting long latency instructions even in
  // moderately high pressure situation.
  // Cheap instructions will only be hoisted if they don't increase register
  // pressure at all.
  auto Cost = calcRegisterCost(&MI, /*ConsiderSeen=*/false,
                               /*ConsiderUnseenAsDef=*/false);

  // Visit BBs from header to current BB, if hoisting this doesn't cause
  // high register pressure, then it's safe to proceed.
  if (!CanCauseHighRegPressure(Cost, CheapInstr)) {
    LLVM_DEBUG(dbgs() << "Hoist non-reg-pressure: " << MI);
    ++NumLowRP;
    return true;
  }

  // Don't risk increasing register pressure if it would create copies.
  if (CreatesCopy) {
    LLVM_DEBUG(dbgs() << "Won't hoist instr with loop PHI use: " << MI);
    return false;
  }

  // Do not "speculate" in high register pressure situation. If an
  // instruction is not guaranteed to be executed in the loop, it's best to be
  // conservative.
  if (AvoidSpeculation &&
      (!IsGuaranteedToExecute(MI.getParent()) && !MayCSE(&MI))) {
    LLVM_DEBUG(dbgs() << "Won't speculate: " << MI);
    return false;
  }

  // High register pressure situation, only hoist if the instruction is going
  // to be remat'ed.
  if (!TII->isTriviallyReMaterializable(MI, AA) &&
      !MI.isDereferenceableInvariantLoad(AA)) {
    LLVM_DEBUG(dbgs() << "Can't remat / high reg-pressure: " << MI);
    return false;
  }

  return true;
}

/// Unfold a load from the given machineinstr if the load itself could be
/// hoisted. Return the unfolded and hoistable load, or null if the load
/// couldn't be unfolded or if it wouldn't be hoistable.
MachineInstr *MachineLICMBase::ExtractHoistableLoad(MachineInstr *MI) {
  // Don't unfold simple loads.
  if (MI->canFoldAsLoad())
    return nullptr;

  // If not, we may be able to unfold a load and hoist that.
  // First test whether the instruction is loading from an amenable
  // memory location.
  if (!MI->isDereferenceableInvariantLoad(AA))
    return nullptr;

  // Next determine the register class for a temporary register.
  unsigned LoadRegIndex;
  unsigned NewOpc =
    TII->getOpcodeAfterMemoryUnfold(MI->getOpcode(),
                                    /*UnfoldLoad=*/true,
                                    /*UnfoldStore=*/false,
                                    &LoadRegIndex);
  if (NewOpc == 0) return nullptr;
  const MCInstrDesc &MID = TII->get(NewOpc);
  MachineFunction &MF = *MI->getMF();
  const TargetRegisterClass *RC = TII->getRegClass(MID, LoadRegIndex, TRI, MF);
  // Ok, we're unfolding. Create a temporary register and do the unfold.
  unsigned Reg = MRI->createVirtualRegister(RC);

  SmallVector<MachineInstr *, 2> NewMIs;
  bool Success = TII->unfoldMemoryOperand(MF, *MI, Reg,
                                          /*UnfoldLoad=*/true,
                                          /*UnfoldStore=*/false, NewMIs);
  (void)Success;
  assert(Success &&
         "unfoldMemoryOperand failed when getOpcodeAfterMemoryUnfold "
         "succeeded!");
  assert(NewMIs.size() == 2 &&
         "Unfolded a load into multiple instructions!");
  MachineBasicBlock *MBB = MI->getParent();
  MachineBasicBlock::iterator Pos = MI;
  MBB->insert(Pos, NewMIs[0]);
  MBB->insert(Pos, NewMIs[1]);
  // If unfolding produced a load that wasn't loop-invariant or profitable to
  // hoist, discard the new instructions and bail.
  if (!IsLoopInvariantInst(*NewMIs[0]) || !IsProfitableToHoist(*NewMIs[0])) {
    NewMIs[0]->eraseFromParent();
    NewMIs[1]->eraseFromParent();
    return nullptr;
  }

  // Update register pressure for the unfolded instruction.
  UpdateRegPressure(NewMIs[1]);

  // Otherwise we successfully unfolded a load that we can hoist.
  MI->eraseFromParent();
  return NewMIs[0];
}

/// Initialize the CSE map with instructions that are in the current loop
/// preheader that may become duplicates of instructions that are hoisted
/// out of the loop.
void MachineLICMBase::InitCSEMap(MachineBasicBlock *BB) {
  for (MachineInstr &MI : *BB)
    CSEMap[MI.getOpcode()].push_back(&MI);
}

/// Find an instruction amount PrevMIs that is a duplicate of MI.
/// Return this instruction if it's found.
const MachineInstr*
MachineLICMBase::LookForDuplicate(const MachineInstr *MI,
                                  std::vector<const MachineInstr*> &PrevMIs) {
  for (const MachineInstr *PrevMI : PrevMIs)
    if (TII->produceSameValue(*MI, *PrevMI, (PreRegAlloc ? MRI : nullptr)))
      return PrevMI;

  return nullptr;
}

/// Given a LICM'ed instruction, look for an instruction on the preheader that
/// computes the same value. If it's found, do a RAU on with the definition of
/// the existing instruction rather than hoisting the instruction to the
/// preheader.
bool MachineLICMBase::EliminateCSE(MachineInstr *MI,
    DenseMap<unsigned, std::vector<const MachineInstr *>>::iterator &CI) {
  // Do not CSE implicit_def so ProcessImplicitDefs can properly propagate
  // the undef property onto uses.
  if (CI == CSEMap.end() || MI->isImplicitDef())
    return false;

  if (const MachineInstr *Dup = LookForDuplicate(MI, CI->second)) {
    LLVM_DEBUG(dbgs() << "CSEing " << *MI << " with " << *Dup);

    // Replace virtual registers defined by MI by their counterparts defined
    // by Dup.
    SmallVector<unsigned, 2> Defs;
    for (unsigned i = 0, e = MI->getNumOperands(); i != e; ++i) {
      const MachineOperand &MO = MI->getOperand(i);

      // Physical registers may not differ here.
      assert((!MO.isReg() || MO.getReg() == 0 ||
              !TargetRegisterInfo::isPhysicalRegister(MO.getReg()) ||
              MO.getReg() == Dup->getOperand(i).getReg()) &&
             "Instructions with different phys regs are not identical!");

      if (MO.isReg() && MO.isDef() &&
          !TargetRegisterInfo::isPhysicalRegister(MO.getReg()))
        Defs.push_back(i);
    }

    SmallVector<const TargetRegisterClass*, 2> OrigRCs;
    for (unsigned i = 0, e = Defs.size(); i != e; ++i) {
      unsigned Idx = Defs[i];
      unsigned Reg = MI->getOperand(Idx).getReg();
      unsigned DupReg = Dup->getOperand(Idx).getReg();
      OrigRCs.push_back(MRI->getRegClass(DupReg));

      if (!MRI->constrainRegClass(DupReg, MRI->getRegClass(Reg))) {
        // Restore old RCs if more than one defs.
        for (unsigned j = 0; j != i; ++j)
          MRI->setRegClass(Dup->getOperand(Defs[j]).getReg(), OrigRCs[j]);
        return false;
      }
    }

    for (unsigned Idx : Defs) {
      unsigned Reg = MI->getOperand(Idx).getReg();
      unsigned DupReg = Dup->getOperand(Idx).getReg();
      MRI->replaceRegWith(Reg, DupReg);
      MRI->clearKillFlags(DupReg);
    }

    MI->eraseFromParent();
    ++NumCSEed;
    return true;
  }
  return false;
}

/// Return true if the given instruction will be CSE'd if it's hoisted out of
/// the loop.
bool MachineLICMBase::MayCSE(MachineInstr *MI) {
  unsigned Opcode = MI->getOpcode();
  DenseMap<unsigned, std::vector<const MachineInstr *>>::iterator
    CI = CSEMap.find(Opcode);
  // Do not CSE implicit_def so ProcessImplicitDefs can properly propagate
  // the undef property onto uses.
  if (CI == CSEMap.end() || MI->isImplicitDef())
    return false;

  return LookForDuplicate(MI, CI->second) != nullptr;
}

/// When an instruction is found to use only loop invariant operands
/// that are safe to hoist, this instruction is called to do the dirty work.
/// It returns true if the instruction is hoisted.
bool MachineLICMBase::Hoist(MachineInstr *MI, MachineBasicBlock *Preheader) {
  // First check whether we should hoist this instruction.
  if (!IsLoopInvariantInst(*MI) || !IsProfitableToHoist(*MI)) {
    // If not, try unfolding a hoistable load.
    MI = ExtractHoistableLoad(MI);
    if (!MI) return false;
  }

  // If we have hoisted an instruction that may store, it can only be a constant
  // store.
  if (MI->mayStore())
    NumStoreConst++;

  // Now move the instructions to the predecessor, inserting it before any
  // terminator instructions.
  LLVM_DEBUG({
    dbgs() << "Hoisting " << *MI;
    if (MI->getParent()->getBasicBlock())
      dbgs() << " from " << printMBBReference(*MI->getParent());
    if (Preheader->getBasicBlock())
      dbgs() << " to " << printMBBReference(*Preheader);
    dbgs() << "\n";
  });

  // If this is the first instruction being hoisted to the preheader,
  // initialize the CSE map with potential common expressions.
  if (FirstInLoop) {
    InitCSEMap(Preheader);
    FirstInLoop = false;
  }

  // Look for opportunity to CSE the hoisted instruction.
  unsigned Opcode = MI->getOpcode();
  DenseMap<unsigned, std::vector<const MachineInstr *>>::iterator
    CI = CSEMap.find(Opcode);
  if (!EliminateCSE(MI, CI)) {
    // Otherwise, splice the instruction to the preheader.
    Preheader->splice(Preheader->getFirstTerminator(),MI->getParent(),MI);

    // Since we are moving the instruction out of its basic block, we do not
    // retain its debug location. Doing so would degrade the debugging
    // experience and adversely affect the accuracy of profiling information.
    MI->setDebugLoc(DebugLoc());

    // Update register pressure for BBs from header to this block.
    UpdateBackTraceRegPressure(MI);

    // Clear the kill flags of any register this instruction defines,
    // since they may need to be live throughout the entire loop
    // rather than just live for part of it.
    for (MachineOperand &MO : MI->operands())
      if (MO.isReg() && MO.isDef() && !MO.isDead())
        MRI->clearKillFlags(MO.getReg());

    // Add to the CSE map.
    if (CI != CSEMap.end())
      CI->second.push_back(MI);
    else
      CSEMap[Opcode].push_back(MI);
  }

  ++NumHoisted;
  Changed = true;

  return true;
}

/// Get the preheader for the current loop, splitting a critical edge if needed.
MachineBasicBlock *MachineLICMBase::getCurPreheader() {
  // Determine the block to which to hoist instructions. If we can't find a
  // suitable loop predecessor, we can't do any hoisting.

  // If we've tried to get a preheader and failed, don't try again.
  if (CurPreheader == reinterpret_cast<MachineBasicBlock *>(-1))
    return nullptr;

  if (!CurPreheader) {
    CurPreheader = CurLoop->getLoopPreheader();
    if (!CurPreheader) {
      MachineBasicBlock *Pred = CurLoop->getLoopPredecessor();
      if (!Pred) {
        CurPreheader = reinterpret_cast<MachineBasicBlock *>(-1);
        return nullptr;
      }

      CurPreheader = Pred->SplitCriticalEdge(CurLoop->getHeader(), *this);
      if (!CurPreheader) {
        CurPreheader = reinterpret_cast<MachineBasicBlock *>(-1);
        return nullptr;
      }
    }
  }
  return CurPreheader;
}
