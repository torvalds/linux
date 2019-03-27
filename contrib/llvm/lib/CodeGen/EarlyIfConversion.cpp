//===-- EarlyIfConversion.cpp - If-conversion on SSA form machine code ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Early if-conversion is for out-of-order CPUs that don't have a lot of
// predicable instructions. The goal is to eliminate conditional branches that
// may mispredict.
//
// Instructions from both sides of the branch are executed specutatively, and a
// cmov instruction selects the result.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SparseSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineBranchProbabilityInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineTraceMetrics.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "early-ifcvt"

// Absolute maximum number of instructions allowed per speculated block.
// This bypasses all other heuristics, so it should be set fairly high.
static cl::opt<unsigned>
BlockInstrLimit("early-ifcvt-limit", cl::init(30), cl::Hidden,
  cl::desc("Maximum number of instructions per speculated block."));

// Stress testing mode - disable heuristics.
static cl::opt<bool> Stress("stress-early-ifcvt", cl::Hidden,
  cl::desc("Turn all knobs to 11"));

STATISTIC(NumDiamondsSeen,  "Number of diamonds");
STATISTIC(NumDiamondsConv,  "Number of diamonds converted");
STATISTIC(NumTrianglesSeen, "Number of triangles");
STATISTIC(NumTrianglesConv, "Number of triangles converted");

//===----------------------------------------------------------------------===//
//                                 SSAIfConv
//===----------------------------------------------------------------------===//
//
// The SSAIfConv class performs if-conversion on SSA form machine code after
// determining if it is possible. The class contains no heuristics; external
// code should be used to determine when if-conversion is a good idea.
//
// SSAIfConv can convert both triangles and diamonds:
//
//   Triangle: Head              Diamond: Head
//              | \                       /  \_
//              |  \                     /    |
//              |  [TF]BB              FBB    TBB
//              |  /                     \    /
//              | /                       \  /
//             Tail                       Tail
//
// Instructions in the conditional blocks TBB and/or FBB are spliced into the
// Head block, and phis in the Tail block are converted to select instructions.
//
namespace {
class SSAIfConv {
  const TargetInstrInfo *TII;
  const TargetRegisterInfo *TRI;
  MachineRegisterInfo *MRI;

public:
  /// The block containing the conditional branch.
  MachineBasicBlock *Head;

  /// The block containing phis after the if-then-else.
  MachineBasicBlock *Tail;

  /// The 'true' conditional block as determined by AnalyzeBranch.
  MachineBasicBlock *TBB;

  /// The 'false' conditional block as determined by AnalyzeBranch.
  MachineBasicBlock *FBB;

  /// isTriangle - When there is no 'else' block, either TBB or FBB will be
  /// equal to Tail.
  bool isTriangle() const { return TBB == Tail || FBB == Tail; }

  /// Returns the Tail predecessor for the True side.
  MachineBasicBlock *getTPred() const { return TBB == Tail ? Head : TBB; }

  /// Returns the Tail predecessor for the  False side.
  MachineBasicBlock *getFPred() const { return FBB == Tail ? Head : FBB; }

  /// Information about each phi in the Tail block.
  struct PHIInfo {
    MachineInstr *PHI;
    unsigned TReg, FReg;
    // Latencies from Cond+Branch, TReg, and FReg to DstReg.
    int CondCycles, TCycles, FCycles;

    PHIInfo(MachineInstr *phi)
      : PHI(phi), TReg(0), FReg(0), CondCycles(0), TCycles(0), FCycles(0) {}
  };

  SmallVector<PHIInfo, 8> PHIs;

private:
  /// The branch condition determined by AnalyzeBranch.
  SmallVector<MachineOperand, 4> Cond;

  /// Instructions in Head that define values used by the conditional blocks.
  /// The hoisted instructions must be inserted after these instructions.
  SmallPtrSet<MachineInstr*, 8> InsertAfter;

  /// Register units clobbered by the conditional blocks.
  BitVector ClobberedRegUnits;

  // Scratch pad for findInsertionPoint.
  SparseSet<unsigned> LiveRegUnits;

  /// Insertion point in Head for speculatively executed instructions form TBB
  /// and FBB.
  MachineBasicBlock::iterator InsertionPoint;

  /// Return true if all non-terminator instructions in MBB can be safely
  /// speculated.
  bool canSpeculateInstrs(MachineBasicBlock *MBB);

  /// Find a valid insertion point in Head.
  bool findInsertionPoint();

  /// Replace PHI instructions in Tail with selects.
  void replacePHIInstrs();

  /// Insert selects and rewrite PHI operands to use them.
  void rewritePHIOperands();

public:
  /// runOnMachineFunction - Initialize per-function data structures.
  void runOnMachineFunction(MachineFunction &MF) {
    TII = MF.getSubtarget().getInstrInfo();
    TRI = MF.getSubtarget().getRegisterInfo();
    MRI = &MF.getRegInfo();
    LiveRegUnits.clear();
    LiveRegUnits.setUniverse(TRI->getNumRegUnits());
    ClobberedRegUnits.clear();
    ClobberedRegUnits.resize(TRI->getNumRegUnits());
  }

  /// canConvertIf - If the sub-CFG headed by MBB can be if-converted,
  /// initialize the internal state, and return true.
  bool canConvertIf(MachineBasicBlock *MBB);

  /// convertIf - If-convert the last block passed to canConvertIf(), assuming
  /// it is possible. Add any erased blocks to RemovedBlocks.
  void convertIf(SmallVectorImpl<MachineBasicBlock*> &RemovedBlocks);
};
} // end anonymous namespace


/// canSpeculateInstrs - Returns true if all the instructions in MBB can safely
/// be speculated. The terminators are not considered.
///
/// If instructions use any values that are defined in the head basic block,
/// the defining instructions are added to InsertAfter.
///
/// Any clobbered regunits are added to ClobberedRegUnits.
///
bool SSAIfConv::canSpeculateInstrs(MachineBasicBlock *MBB) {
  // Reject any live-in physregs. It's probably CPSR/EFLAGS, and very hard to
  // get right.
  if (!MBB->livein_empty()) {
    LLVM_DEBUG(dbgs() << printMBBReference(*MBB) << " has live-ins.\n");
    return false;
  }

  unsigned InstrCount = 0;

  // Check all instructions, except the terminators. It is assumed that
  // terminators never have side effects or define any used register values.
  for (MachineBasicBlock::iterator I = MBB->begin(),
       E = MBB->getFirstTerminator(); I != E; ++I) {
    if (I->isDebugInstr())
      continue;

    if (++InstrCount > BlockInstrLimit && !Stress) {
      LLVM_DEBUG(dbgs() << printMBBReference(*MBB) << " has more than "
                        << BlockInstrLimit << " instructions.\n");
      return false;
    }

    // There shouldn't normally be any phis in a single-predecessor block.
    if (I->isPHI()) {
      LLVM_DEBUG(dbgs() << "Can't hoist: " << *I);
      return false;
    }

    // Don't speculate loads. Note that it may be possible and desirable to
    // speculate GOT or constant pool loads that are guaranteed not to trap,
    // but we don't support that for now.
    if (I->mayLoad()) {
      LLVM_DEBUG(dbgs() << "Won't speculate load: " << *I);
      return false;
    }

    // We never speculate stores, so an AA pointer isn't necessary.
    bool DontMoveAcrossStore = true;
    if (!I->isSafeToMove(nullptr, DontMoveAcrossStore)) {
      LLVM_DEBUG(dbgs() << "Can't speculate: " << *I);
      return false;
    }

    // Check for any dependencies on Head instructions.
    for (const MachineOperand &MO : I->operands()) {
      if (MO.isRegMask()) {
        LLVM_DEBUG(dbgs() << "Won't speculate regmask: " << *I);
        return false;
      }
      if (!MO.isReg())
        continue;
      unsigned Reg = MO.getReg();

      // Remember clobbered regunits.
      if (MO.isDef() && TargetRegisterInfo::isPhysicalRegister(Reg))
        for (MCRegUnitIterator Units(Reg, TRI); Units.isValid(); ++Units)
          ClobberedRegUnits.set(*Units);

      if (!MO.readsReg() || !TargetRegisterInfo::isVirtualRegister(Reg))
        continue;
      MachineInstr *DefMI = MRI->getVRegDef(Reg);
      if (!DefMI || DefMI->getParent() != Head)
        continue;
      if (InsertAfter.insert(DefMI).second)
        LLVM_DEBUG(dbgs() << printMBBReference(*MBB) << " depends on "
                          << *DefMI);
      if (DefMI->isTerminator()) {
        LLVM_DEBUG(dbgs() << "Can't insert instructions below terminator.\n");
        return false;
      }
    }
  }
  return true;
}


/// Find an insertion point in Head for the speculated instructions. The
/// insertion point must be:
///
/// 1. Before any terminators.
/// 2. After any instructions in InsertAfter.
/// 3. Not have any clobbered regunits live.
///
/// This function sets InsertionPoint and returns true when successful, it
/// returns false if no valid insertion point could be found.
///
bool SSAIfConv::findInsertionPoint() {
  // Keep track of live regunits before the current position.
  // Only track RegUnits that are also in ClobberedRegUnits.
  LiveRegUnits.clear();
  SmallVector<unsigned, 8> Reads;
  MachineBasicBlock::iterator FirstTerm = Head->getFirstTerminator();
  MachineBasicBlock::iterator I = Head->end();
  MachineBasicBlock::iterator B = Head->begin();
  while (I != B) {
    --I;
    // Some of the conditional code depends in I.
    if (InsertAfter.count(&*I)) {
      LLVM_DEBUG(dbgs() << "Can't insert code after " << *I);
      return false;
    }

    // Update live regunits.
    for (const MachineOperand &MO : I->operands()) {
      // We're ignoring regmask operands. That is conservatively correct.
      if (!MO.isReg())
        continue;
      unsigned Reg = MO.getReg();
      if (!TargetRegisterInfo::isPhysicalRegister(Reg))
        continue;
      // I clobbers Reg, so it isn't live before I.
      if (MO.isDef())
        for (MCRegUnitIterator Units(Reg, TRI); Units.isValid(); ++Units)
          LiveRegUnits.erase(*Units);
      // Unless I reads Reg.
      if (MO.readsReg())
        Reads.push_back(Reg);
    }
    // Anything read by I is live before I.
    while (!Reads.empty())
      for (MCRegUnitIterator Units(Reads.pop_back_val(), TRI); Units.isValid();
           ++Units)
        if (ClobberedRegUnits.test(*Units))
          LiveRegUnits.insert(*Units);

    // We can't insert before a terminator.
    if (I != FirstTerm && I->isTerminator())
      continue;

    // Some of the clobbered registers are live before I, not a valid insertion
    // point.
    if (!LiveRegUnits.empty()) {
      LLVM_DEBUG({
        dbgs() << "Would clobber";
        for (SparseSet<unsigned>::const_iterator
             i = LiveRegUnits.begin(), e = LiveRegUnits.end(); i != e; ++i)
          dbgs() << ' ' << printRegUnit(*i, TRI);
        dbgs() << " live before " << *I;
      });
      continue;
    }

    // This is a valid insertion point.
    InsertionPoint = I;
    LLVM_DEBUG(dbgs() << "Can insert before " << *I);
    return true;
  }
  LLVM_DEBUG(dbgs() << "No legal insertion point found.\n");
  return false;
}



/// canConvertIf - analyze the sub-cfg rooted in MBB, and return true if it is
/// a potential candidate for if-conversion. Fill out the internal state.
///
bool SSAIfConv::canConvertIf(MachineBasicBlock *MBB) {
  Head = MBB;
  TBB = FBB = Tail = nullptr;

  if (Head->succ_size() != 2)
    return false;
  MachineBasicBlock *Succ0 = Head->succ_begin()[0];
  MachineBasicBlock *Succ1 = Head->succ_begin()[1];

  // Canonicalize so Succ0 has MBB as its single predecessor.
  if (Succ0->pred_size() != 1)
    std::swap(Succ0, Succ1);

  if (Succ0->pred_size() != 1 || Succ0->succ_size() != 1)
    return false;

  Tail = Succ0->succ_begin()[0];

  // This is not a triangle.
  if (Tail != Succ1) {
    // Check for a diamond. We won't deal with any critical edges.
    if (Succ1->pred_size() != 1 || Succ1->succ_size() != 1 ||
        Succ1->succ_begin()[0] != Tail)
      return false;
    LLVM_DEBUG(dbgs() << "\nDiamond: " << printMBBReference(*Head) << " -> "
                      << printMBBReference(*Succ0) << "/"
                      << printMBBReference(*Succ1) << " -> "
                      << printMBBReference(*Tail) << '\n');

    // Live-in physregs are tricky to get right when speculating code.
    if (!Tail->livein_empty()) {
      LLVM_DEBUG(dbgs() << "Tail has live-ins.\n");
      return false;
    }
  } else {
    LLVM_DEBUG(dbgs() << "\nTriangle: " << printMBBReference(*Head) << " -> "
                      << printMBBReference(*Succ0) << " -> "
                      << printMBBReference(*Tail) << '\n');
  }

  // This is a triangle or a diamond.
  // If Tail doesn't have any phis, there must be side effects.
  if (Tail->empty() || !Tail->front().isPHI()) {
    LLVM_DEBUG(dbgs() << "No phis in tail.\n");
    return false;
  }

  // The branch we're looking to eliminate must be analyzable.
  Cond.clear();
  if (TII->analyzeBranch(*Head, TBB, FBB, Cond)) {
    LLVM_DEBUG(dbgs() << "Branch not analyzable.\n");
    return false;
  }

  // This is weird, probably some sort of degenerate CFG.
  if (!TBB) {
    LLVM_DEBUG(dbgs() << "AnalyzeBranch didn't find conditional branch.\n");
    return false;
  }

  // Make sure the analyzed branch is conditional; one of the successors
  // could be a landing pad. (Empty landing pads can be generated on Windows.)
  if (Cond.empty()) {
    LLVM_DEBUG(dbgs() << "AnalyzeBranch found an unconditional branch.\n");
    return false;
  }

  // AnalyzeBranch doesn't set FBB on a fall-through branch.
  // Make sure it is always set.
  FBB = TBB == Succ0 ? Succ1 : Succ0;

  // Any phis in the tail block must be convertible to selects.
  PHIs.clear();
  MachineBasicBlock *TPred = getTPred();
  MachineBasicBlock *FPred = getFPred();
  for (MachineBasicBlock::iterator I = Tail->begin(), E = Tail->end();
       I != E && I->isPHI(); ++I) {
    PHIs.push_back(&*I);
    PHIInfo &PI = PHIs.back();
    // Find PHI operands corresponding to TPred and FPred.
    for (unsigned i = 1; i != PI.PHI->getNumOperands(); i += 2) {
      if (PI.PHI->getOperand(i+1).getMBB() == TPred)
        PI.TReg = PI.PHI->getOperand(i).getReg();
      if (PI.PHI->getOperand(i+1).getMBB() == FPred)
        PI.FReg = PI.PHI->getOperand(i).getReg();
    }
    assert(TargetRegisterInfo::isVirtualRegister(PI.TReg) && "Bad PHI");
    assert(TargetRegisterInfo::isVirtualRegister(PI.FReg) && "Bad PHI");

    // Get target information.
    if (!TII->canInsertSelect(*Head, Cond, PI.TReg, PI.FReg,
                              PI.CondCycles, PI.TCycles, PI.FCycles)) {
      LLVM_DEBUG(dbgs() << "Can't convert: " << *PI.PHI);
      return false;
    }
  }

  // Check that the conditional instructions can be speculated.
  InsertAfter.clear();
  ClobberedRegUnits.reset();
  if (TBB != Tail && !canSpeculateInstrs(TBB))
    return false;
  if (FBB != Tail && !canSpeculateInstrs(FBB))
    return false;

  // Try to find a valid insertion point for the speculated instructions in the
  // head basic block.
  if (!findInsertionPoint())
    return false;

  if (isTriangle())
    ++NumTrianglesSeen;
  else
    ++NumDiamondsSeen;
  return true;
}

/// replacePHIInstrs - Completely replace PHI instructions with selects.
/// This is possible when the only Tail predecessors are the if-converted
/// blocks.
void SSAIfConv::replacePHIInstrs() {
  assert(Tail->pred_size() == 2 && "Cannot replace PHIs");
  MachineBasicBlock::iterator FirstTerm = Head->getFirstTerminator();
  assert(FirstTerm != Head->end() && "No terminators");
  DebugLoc HeadDL = FirstTerm->getDebugLoc();

  // Convert all PHIs to select instructions inserted before FirstTerm.
  for (unsigned i = 0, e = PHIs.size(); i != e; ++i) {
    PHIInfo &PI = PHIs[i];
    LLVM_DEBUG(dbgs() << "If-converting " << *PI.PHI);
    unsigned DstReg = PI.PHI->getOperand(0).getReg();
    TII->insertSelect(*Head, FirstTerm, HeadDL, DstReg, Cond, PI.TReg, PI.FReg);
    LLVM_DEBUG(dbgs() << "          --> " << *std::prev(FirstTerm));
    PI.PHI->eraseFromParent();
    PI.PHI = nullptr;
  }
}

/// rewritePHIOperands - When there are additional Tail predecessors, insert
/// select instructions in Head and rewrite PHI operands to use the selects.
/// Keep the PHI instructions in Tail to handle the other predecessors.
void SSAIfConv::rewritePHIOperands() {
  MachineBasicBlock::iterator FirstTerm = Head->getFirstTerminator();
  assert(FirstTerm != Head->end() && "No terminators");
  DebugLoc HeadDL = FirstTerm->getDebugLoc();

  // Convert all PHIs to select instructions inserted before FirstTerm.
  for (unsigned i = 0, e = PHIs.size(); i != e; ++i) {
    PHIInfo &PI = PHIs[i];
    unsigned DstReg = 0;

    LLVM_DEBUG(dbgs() << "If-converting " << *PI.PHI);
    if (PI.TReg == PI.FReg) {
      // We do not need the select instruction if both incoming values are
      // equal.
      DstReg = PI.TReg;
    } else {
      unsigned PHIDst = PI.PHI->getOperand(0).getReg();
      DstReg = MRI->createVirtualRegister(MRI->getRegClass(PHIDst));
      TII->insertSelect(*Head, FirstTerm, HeadDL,
                         DstReg, Cond, PI.TReg, PI.FReg);
      LLVM_DEBUG(dbgs() << "          --> " << *std::prev(FirstTerm));
    }

    // Rewrite PHI operands TPred -> (DstReg, Head), remove FPred.
    for (unsigned i = PI.PHI->getNumOperands(); i != 1; i -= 2) {
      MachineBasicBlock *MBB = PI.PHI->getOperand(i-1).getMBB();
      if (MBB == getTPred()) {
        PI.PHI->getOperand(i-1).setMBB(Head);
        PI.PHI->getOperand(i-2).setReg(DstReg);
      } else if (MBB == getFPred()) {
        PI.PHI->RemoveOperand(i-1);
        PI.PHI->RemoveOperand(i-2);
      }
    }
    LLVM_DEBUG(dbgs() << "          --> " << *PI.PHI);
  }
}

/// convertIf - Execute the if conversion after canConvertIf has determined the
/// feasibility.
///
/// Any basic blocks erased will be added to RemovedBlocks.
///
void SSAIfConv::convertIf(SmallVectorImpl<MachineBasicBlock*> &RemovedBlocks) {
  assert(Head && Tail && TBB && FBB && "Call canConvertIf first.");

  // Update statistics.
  if (isTriangle())
    ++NumTrianglesConv;
  else
    ++NumDiamondsConv;

  // Move all instructions into Head, except for the terminators.
  if (TBB != Tail)
    Head->splice(InsertionPoint, TBB, TBB->begin(), TBB->getFirstTerminator());
  if (FBB != Tail)
    Head->splice(InsertionPoint, FBB, FBB->begin(), FBB->getFirstTerminator());

  // Are there extra Tail predecessors?
  bool ExtraPreds = Tail->pred_size() != 2;
  if (ExtraPreds)
    rewritePHIOperands();
  else
    replacePHIInstrs();

  // Fix up the CFG, temporarily leave Head without any successors.
  Head->removeSuccessor(TBB);
  Head->removeSuccessor(FBB, true);
  if (TBB != Tail)
    TBB->removeSuccessor(Tail, true);
  if (FBB != Tail)
    FBB->removeSuccessor(Tail, true);

  // Fix up Head's terminators.
  // It should become a single branch or a fallthrough.
  DebugLoc HeadDL = Head->getFirstTerminator()->getDebugLoc();
  TII->removeBranch(*Head);

  // Erase the now empty conditional blocks. It is likely that Head can fall
  // through to Tail, and we can join the two blocks.
  if (TBB != Tail) {
    RemovedBlocks.push_back(TBB);
    TBB->eraseFromParent();
  }
  if (FBB != Tail) {
    RemovedBlocks.push_back(FBB);
    FBB->eraseFromParent();
  }

  assert(Head->succ_empty() && "Additional head successors?");
  if (!ExtraPreds && Head->isLayoutSuccessor(Tail)) {
    // Splice Tail onto the end of Head.
    LLVM_DEBUG(dbgs() << "Joining tail " << printMBBReference(*Tail)
                      << " into head " << printMBBReference(*Head) << '\n');
    Head->splice(Head->end(), Tail,
                     Tail->begin(), Tail->end());
    Head->transferSuccessorsAndUpdatePHIs(Tail);
    RemovedBlocks.push_back(Tail);
    Tail->eraseFromParent();
  } else {
    // We need a branch to Tail, let code placement work it out later.
    LLVM_DEBUG(dbgs() << "Converting to unconditional branch.\n");
    SmallVector<MachineOperand, 0> EmptyCond;
    TII->insertBranch(*Head, Tail, nullptr, EmptyCond, HeadDL);
    Head->addSuccessor(Tail);
  }
  LLVM_DEBUG(dbgs() << *Head);
}


//===----------------------------------------------------------------------===//
//                           EarlyIfConverter Pass
//===----------------------------------------------------------------------===//

namespace {
class EarlyIfConverter : public MachineFunctionPass {
  const TargetInstrInfo *TII;
  const TargetRegisterInfo *TRI;
  MCSchedModel SchedModel;
  MachineRegisterInfo *MRI;
  MachineDominatorTree *DomTree;
  MachineLoopInfo *Loops;
  MachineTraceMetrics *Traces;
  MachineTraceMetrics::Ensemble *MinInstr;
  SSAIfConv IfConv;

public:
  static char ID;
  EarlyIfConverter() : MachineFunctionPass(ID) {}
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override { return "Early If-Conversion"; }

private:
  bool tryConvertIf(MachineBasicBlock*);
  void updateDomTree(ArrayRef<MachineBasicBlock*> Removed);
  void updateLoops(ArrayRef<MachineBasicBlock*> Removed);
  void invalidateTraces();
  bool shouldConvertIf();
};
} // end anonymous namespace

char EarlyIfConverter::ID = 0;
char &llvm::EarlyIfConverterID = EarlyIfConverter::ID;

INITIALIZE_PASS_BEGIN(EarlyIfConverter, DEBUG_TYPE,
                      "Early If Converter", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineBranchProbabilityInfo)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree)
INITIALIZE_PASS_DEPENDENCY(MachineTraceMetrics)
INITIALIZE_PASS_END(EarlyIfConverter, DEBUG_TYPE,
                    "Early If Converter", false, false)

void EarlyIfConverter::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<MachineBranchProbabilityInfo>();
  AU.addRequired<MachineDominatorTree>();
  AU.addPreserved<MachineDominatorTree>();
  AU.addRequired<MachineLoopInfo>();
  AU.addPreserved<MachineLoopInfo>();
  AU.addRequired<MachineTraceMetrics>();
  AU.addPreserved<MachineTraceMetrics>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

/// Update the dominator tree after if-conversion erased some blocks.
void EarlyIfConverter::updateDomTree(ArrayRef<MachineBasicBlock*> Removed) {
  // convertIf can remove TBB, FBB, and Tail can be merged into Head.
  // TBB and FBB should not dominate any blocks.
  // Tail children should be transferred to Head.
  MachineDomTreeNode *HeadNode = DomTree->getNode(IfConv.Head);
  for (unsigned i = 0, e = Removed.size(); i != e; ++i) {
    MachineDomTreeNode *Node = DomTree->getNode(Removed[i]);
    assert(Node != HeadNode && "Cannot erase the head node");
    while (Node->getNumChildren()) {
      assert(Node->getBlock() == IfConv.Tail && "Unexpected children");
      DomTree->changeImmediateDominator(Node->getChildren().back(), HeadNode);
    }
    DomTree->eraseNode(Removed[i]);
  }
}

/// Update LoopInfo after if-conversion.
void EarlyIfConverter::updateLoops(ArrayRef<MachineBasicBlock*> Removed) {
  if (!Loops)
    return;
  // If-conversion doesn't change loop structure, and it doesn't mess with back
  // edges, so updating LoopInfo is simply removing the dead blocks.
  for (unsigned i = 0, e = Removed.size(); i != e; ++i)
    Loops->removeBlock(Removed[i]);
}

/// Invalidate MachineTraceMetrics before if-conversion.
void EarlyIfConverter::invalidateTraces() {
  Traces->verifyAnalysis();
  Traces->invalidate(IfConv.Head);
  Traces->invalidate(IfConv.Tail);
  Traces->invalidate(IfConv.TBB);
  Traces->invalidate(IfConv.FBB);
  Traces->verifyAnalysis();
}

// Adjust cycles with downward saturation.
static unsigned adjCycles(unsigned Cyc, int Delta) {
  if (Delta < 0 && Cyc + Delta > Cyc)
    return 0;
  return Cyc + Delta;
}

/// Apply cost model and heuristics to the if-conversion in IfConv.
/// Return true if the conversion is a good idea.
///
bool EarlyIfConverter::shouldConvertIf() {
  // Stress testing mode disables all cost considerations.
  if (Stress)
    return true;

  if (!MinInstr)
    MinInstr = Traces->getEnsemble(MachineTraceMetrics::TS_MinInstrCount);

  MachineTraceMetrics::Trace TBBTrace = MinInstr->getTrace(IfConv.getTPred());
  MachineTraceMetrics::Trace FBBTrace = MinInstr->getTrace(IfConv.getFPred());
  LLVM_DEBUG(dbgs() << "TBB: " << TBBTrace << "FBB: " << FBBTrace);
  unsigned MinCrit = std::min(TBBTrace.getCriticalPath(),
                              FBBTrace.getCriticalPath());

  // Set a somewhat arbitrary limit on the critical path extension we accept.
  unsigned CritLimit = SchedModel.MispredictPenalty/2;

  // If-conversion only makes sense when there is unexploited ILP. Compute the
  // maximum-ILP resource length of the trace after if-conversion. Compare it
  // to the shortest critical path.
  SmallVector<const MachineBasicBlock*, 1> ExtraBlocks;
  if (IfConv.TBB != IfConv.Tail)
    ExtraBlocks.push_back(IfConv.TBB);
  unsigned ResLength = FBBTrace.getResourceLength(ExtraBlocks);
  LLVM_DEBUG(dbgs() << "Resource length " << ResLength
                    << ", minimal critical path " << MinCrit << '\n');
  if (ResLength > MinCrit + CritLimit) {
    LLVM_DEBUG(dbgs() << "Not enough available ILP.\n");
    return false;
  }

  // Assume that the depth of the first head terminator will also be the depth
  // of the select instruction inserted, as determined by the flag dependency.
  // TBB / FBB data dependencies may delay the select even more.
  MachineTraceMetrics::Trace HeadTrace = MinInstr->getTrace(IfConv.Head);
  unsigned BranchDepth =
      HeadTrace.getInstrCycles(*IfConv.Head->getFirstTerminator()).Depth;
  LLVM_DEBUG(dbgs() << "Branch depth: " << BranchDepth << '\n');

  // Look at all the tail phis, and compute the critical path extension caused
  // by inserting select instructions.
  MachineTraceMetrics::Trace TailTrace = MinInstr->getTrace(IfConv.Tail);
  for (unsigned i = 0, e = IfConv.PHIs.size(); i != e; ++i) {
    SSAIfConv::PHIInfo &PI = IfConv.PHIs[i];
    unsigned Slack = TailTrace.getInstrSlack(*PI.PHI);
    unsigned MaxDepth = Slack + TailTrace.getInstrCycles(*PI.PHI).Depth;
    LLVM_DEBUG(dbgs() << "Slack " << Slack << ":\t" << *PI.PHI);

    // The condition is pulled into the critical path.
    unsigned CondDepth = adjCycles(BranchDepth, PI.CondCycles);
    if (CondDepth > MaxDepth) {
      unsigned Extra = CondDepth - MaxDepth;
      LLVM_DEBUG(dbgs() << "Condition adds " << Extra << " cycles.\n");
      if (Extra > CritLimit) {
        LLVM_DEBUG(dbgs() << "Exceeds limit of " << CritLimit << '\n');
        return false;
      }
    }

    // The TBB value is pulled into the critical path.
    unsigned TDepth = adjCycles(TBBTrace.getPHIDepth(*PI.PHI), PI.TCycles);
    if (TDepth > MaxDepth) {
      unsigned Extra = TDepth - MaxDepth;
      LLVM_DEBUG(dbgs() << "TBB data adds " << Extra << " cycles.\n");
      if (Extra > CritLimit) {
        LLVM_DEBUG(dbgs() << "Exceeds limit of " << CritLimit << '\n');
        return false;
      }
    }

    // The FBB value is pulled into the critical path.
    unsigned FDepth = adjCycles(FBBTrace.getPHIDepth(*PI.PHI), PI.FCycles);
    if (FDepth > MaxDepth) {
      unsigned Extra = FDepth - MaxDepth;
      LLVM_DEBUG(dbgs() << "FBB data adds " << Extra << " cycles.\n");
      if (Extra > CritLimit) {
        LLVM_DEBUG(dbgs() << "Exceeds limit of " << CritLimit << '\n');
        return false;
      }
    }
  }
  return true;
}

/// Attempt repeated if-conversion on MBB, return true if successful.
///
bool EarlyIfConverter::tryConvertIf(MachineBasicBlock *MBB) {
  bool Changed = false;
  while (IfConv.canConvertIf(MBB) && shouldConvertIf()) {
    // If-convert MBB and update analyses.
    invalidateTraces();
    SmallVector<MachineBasicBlock*, 4> RemovedBlocks;
    IfConv.convertIf(RemovedBlocks);
    Changed = true;
    updateDomTree(RemovedBlocks);
    updateLoops(RemovedBlocks);
  }
  return Changed;
}

bool EarlyIfConverter::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "********** EARLY IF-CONVERSION **********\n"
                    << "********** Function: " << MF.getName() << '\n');
  if (skipFunction(MF.getFunction()))
    return false;

  // Only run if conversion if the target wants it.
  const TargetSubtargetInfo &STI = MF.getSubtarget();
  if (!STI.enableEarlyIfConversion())
    return false;

  TII = STI.getInstrInfo();
  TRI = STI.getRegisterInfo();
  SchedModel = STI.getSchedModel();
  MRI = &MF.getRegInfo();
  DomTree = &getAnalysis<MachineDominatorTree>();
  Loops = getAnalysisIfAvailable<MachineLoopInfo>();
  Traces = &getAnalysis<MachineTraceMetrics>();
  MinInstr = nullptr;

  bool Changed = false;
  IfConv.runOnMachineFunction(MF);

  // Visit blocks in dominator tree post-order. The post-order enables nested
  // if-conversion in a single pass. The tryConvertIf() function may erase
  // blocks, but only blocks dominated by the head block. This makes it safe to
  // update the dominator tree while the post-order iterator is still active.
  for (auto DomNode : post_order(DomTree))
    if (tryConvertIf(DomNode->getBlock()))
      Changed = true;

  return Changed;
}
