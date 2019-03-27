//===-- llvm/CodeGen/MachineBasicBlock.cpp ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Collect the sequence of machine instructions for a basic block.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/LiveVariables.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/ModuleSlotTracker.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>
using namespace llvm;

#define DEBUG_TYPE "codegen"

MachineBasicBlock::MachineBasicBlock(MachineFunction &MF, const BasicBlock *B)
    : BB(B), Number(-1), xParent(&MF) {
  Insts.Parent = this;
  if (B)
    IrrLoopHeaderWeight = B->getIrrLoopHeaderWeight();
}

MachineBasicBlock::~MachineBasicBlock() {
}

/// Return the MCSymbol for this basic block.
MCSymbol *MachineBasicBlock::getSymbol() const {
  if (!CachedMCSymbol) {
    const MachineFunction *MF = getParent();
    MCContext &Ctx = MF->getContext();
    auto Prefix = Ctx.getAsmInfo()->getPrivateLabelPrefix();
    assert(getNumber() >= 0 && "cannot get label for unreachable MBB");
    CachedMCSymbol = Ctx.getOrCreateSymbol(Twine(Prefix) + "BB" +
                                           Twine(MF->getFunctionNumber()) +
                                           "_" + Twine(getNumber()));
  }

  return CachedMCSymbol;
}


raw_ostream &llvm::operator<<(raw_ostream &OS, const MachineBasicBlock &MBB) {
  MBB.print(OS);
  return OS;
}

Printable llvm::printMBBReference(const MachineBasicBlock &MBB) {
  return Printable([&MBB](raw_ostream &OS) { return MBB.printAsOperand(OS); });
}

/// When an MBB is added to an MF, we need to update the parent pointer of the
/// MBB, the MBB numbering, and any instructions in the MBB to be on the right
/// operand list for registers.
///
/// MBBs start out as #-1. When a MBB is added to a MachineFunction, it
/// gets the next available unique MBB number. If it is removed from a
/// MachineFunction, it goes back to being #-1.
void ilist_callback_traits<MachineBasicBlock>::addNodeToList(
    MachineBasicBlock *N) {
  MachineFunction &MF = *N->getParent();
  N->Number = MF.addToMBBNumbering(N);

  // Make sure the instructions have their operands in the reginfo lists.
  MachineRegisterInfo &RegInfo = MF.getRegInfo();
  for (MachineBasicBlock::instr_iterator
         I = N->instr_begin(), E = N->instr_end(); I != E; ++I)
    I->AddRegOperandsToUseLists(RegInfo);
}

void ilist_callback_traits<MachineBasicBlock>::removeNodeFromList(
    MachineBasicBlock *N) {
  N->getParent()->removeFromMBBNumbering(N->Number);
  N->Number = -1;
}

/// When we add an instruction to a basic block list, we update its parent
/// pointer and add its operands from reg use/def lists if appropriate.
void ilist_traits<MachineInstr>::addNodeToList(MachineInstr *N) {
  assert(!N->getParent() && "machine instruction already in a basic block");
  N->setParent(Parent);

  // Add the instruction's register operands to their corresponding
  // use/def lists.
  MachineFunction *MF = Parent->getParent();
  N->AddRegOperandsToUseLists(MF->getRegInfo());
  MF->handleInsertion(*N);
}

/// When we remove an instruction from a basic block list, we update its parent
/// pointer and remove its operands from reg use/def lists if appropriate.
void ilist_traits<MachineInstr>::removeNodeFromList(MachineInstr *N) {
  assert(N->getParent() && "machine instruction not in a basic block");

  // Remove from the use/def lists.
  if (MachineFunction *MF = N->getMF()) {
    MF->handleRemoval(*N);
    N->RemoveRegOperandsFromUseLists(MF->getRegInfo());
  }

  N->setParent(nullptr);
}

/// When moving a range of instructions from one MBB list to another, we need to
/// update the parent pointers and the use/def lists.
void ilist_traits<MachineInstr>::transferNodesFromList(ilist_traits &FromList,
                                                       instr_iterator First,
                                                       instr_iterator Last) {
  assert(Parent->getParent() == FromList.Parent->getParent() &&
        "MachineInstr parent mismatch!");
  assert(this != &FromList && "Called without a real transfer...");
  assert(Parent != FromList.Parent && "Two lists have the same parent?");

  // If splicing between two blocks within the same function, just update the
  // parent pointers.
  for (; First != Last; ++First)
    First->setParent(Parent);
}

void ilist_traits<MachineInstr>::deleteNode(MachineInstr *MI) {
  assert(!MI->getParent() && "MI is still in a block!");
  Parent->getParent()->DeleteMachineInstr(MI);
}

MachineBasicBlock::iterator MachineBasicBlock::getFirstNonPHI() {
  instr_iterator I = instr_begin(), E = instr_end();
  while (I != E && I->isPHI())
    ++I;
  assert((I == E || !I->isInsideBundle()) &&
         "First non-phi MI cannot be inside a bundle!");
  return I;
}

MachineBasicBlock::iterator
MachineBasicBlock::SkipPHIsAndLabels(MachineBasicBlock::iterator I) {
  const TargetInstrInfo *TII = getParent()->getSubtarget().getInstrInfo();

  iterator E = end();
  while (I != E && (I->isPHI() || I->isPosition() ||
                    TII->isBasicBlockPrologue(*I)))
    ++I;
  // FIXME: This needs to change if we wish to bundle labels
  // inside the bundle.
  assert((I == E || !I->isInsideBundle()) &&
         "First non-phi / non-label instruction is inside a bundle!");
  return I;
}

MachineBasicBlock::iterator
MachineBasicBlock::SkipPHIsLabelsAndDebug(MachineBasicBlock::iterator I) {
  const TargetInstrInfo *TII = getParent()->getSubtarget().getInstrInfo();

  iterator E = end();
  while (I != E && (I->isPHI() || I->isPosition() || I->isDebugInstr() ||
                    TII->isBasicBlockPrologue(*I)))
    ++I;
  // FIXME: This needs to change if we wish to bundle labels / dbg_values
  // inside the bundle.
  assert((I == E || !I->isInsideBundle()) &&
         "First non-phi / non-label / non-debug "
         "instruction is inside a bundle!");
  return I;
}

MachineBasicBlock::iterator MachineBasicBlock::getFirstTerminator() {
  iterator B = begin(), E = end(), I = E;
  while (I != B && ((--I)->isTerminator() || I->isDebugInstr()))
    ; /*noop */
  while (I != E && !I->isTerminator())
    ++I;
  return I;
}

MachineBasicBlock::instr_iterator MachineBasicBlock::getFirstInstrTerminator() {
  instr_iterator B = instr_begin(), E = instr_end(), I = E;
  while (I != B && ((--I)->isTerminator() || I->isDebugInstr()))
    ; /*noop */
  while (I != E && !I->isTerminator())
    ++I;
  return I;
}

MachineBasicBlock::iterator MachineBasicBlock::getFirstNonDebugInstr() {
  // Skip over begin-of-block dbg_value instructions.
  return skipDebugInstructionsForward(begin(), end());
}

MachineBasicBlock::iterator MachineBasicBlock::getLastNonDebugInstr() {
  // Skip over end-of-block dbg_value instructions.
  instr_iterator B = instr_begin(), I = instr_end();
  while (I != B) {
    --I;
    // Return instruction that starts a bundle.
    if (I->isDebugInstr() || I->isInsideBundle())
      continue;
    return I;
  }
  // The block is all debug values.
  return end();
}

bool MachineBasicBlock::hasEHPadSuccessor() const {
  for (const_succ_iterator I = succ_begin(), E = succ_end(); I != E; ++I)
    if ((*I)->isEHPad())
      return true;
  return false;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void MachineBasicBlock::dump() const {
  print(dbgs());
}
#endif

bool MachineBasicBlock::isLegalToHoistInto() const {
  if (isReturnBlock() || hasEHPadSuccessor())
    return false;
  return true;
}

StringRef MachineBasicBlock::getName() const {
  if (const BasicBlock *LBB = getBasicBlock())
    return LBB->getName();
  else
    return StringRef("", 0);
}

/// Return a hopefully unique identifier for this block.
std::string MachineBasicBlock::getFullName() const {
  std::string Name;
  if (getParent())
    Name = (getParent()->getName() + ":").str();
  if (getBasicBlock())
    Name += getBasicBlock()->getName();
  else
    Name += ("BB" + Twine(getNumber())).str();
  return Name;
}

void MachineBasicBlock::print(raw_ostream &OS, const SlotIndexes *Indexes,
                              bool IsStandalone) const {
  const MachineFunction *MF = getParent();
  if (!MF) {
    OS << "Can't print out MachineBasicBlock because parent MachineFunction"
       << " is null\n";
    return;
  }
  const Function &F = MF->getFunction();
  const Module *M = F.getParent();
  ModuleSlotTracker MST(M);
  MST.incorporateFunction(F);
  print(OS, MST, Indexes, IsStandalone);
}

void MachineBasicBlock::print(raw_ostream &OS, ModuleSlotTracker &MST,
                              const SlotIndexes *Indexes,
                              bool IsStandalone) const {
  const MachineFunction *MF = getParent();
  if (!MF) {
    OS << "Can't print out MachineBasicBlock because parent MachineFunction"
       << " is null\n";
    return;
  }

  if (Indexes)
    OS << Indexes->getMBBStartIdx(this) << '\t';

  OS << "bb." << getNumber();
  bool HasAttributes = false;
  if (const auto *BB = getBasicBlock()) {
    if (BB->hasName()) {
      OS << "." << BB->getName();
    } else {
      HasAttributes = true;
      OS << " (";
      int Slot = MST.getLocalSlot(BB);
      if (Slot == -1)
        OS << "<ir-block badref>";
      else
        OS << (Twine("%ir-block.") + Twine(Slot)).str();
    }
  }

  if (hasAddressTaken()) {
    OS << (HasAttributes ? ", " : " (");
    OS << "address-taken";
    HasAttributes = true;
  }
  if (isEHPad()) {
    OS << (HasAttributes ? ", " : " (");
    OS << "landing-pad";
    HasAttributes = true;
  }
  if (getAlignment()) {
    OS << (HasAttributes ? ", " : " (");
    OS << "align " << getAlignment();
    HasAttributes = true;
  }
  if (HasAttributes)
    OS << ")";
  OS << ":\n";

  const TargetRegisterInfo *TRI = MF->getSubtarget().getRegisterInfo();
  const MachineRegisterInfo &MRI = MF->getRegInfo();
  const TargetInstrInfo &TII = *getParent()->getSubtarget().getInstrInfo();
  bool HasLineAttributes = false;

  // Print the preds of this block according to the CFG.
  if (!pred_empty() && IsStandalone) {
    if (Indexes) OS << '\t';
    // Don't indent(2), align with previous line attributes.
    OS << "; predecessors: ";
    for (auto I = pred_begin(), E = pred_end(); I != E; ++I) {
      if (I != pred_begin())
        OS << ", ";
      OS << printMBBReference(**I);
    }
    OS << '\n';
    HasLineAttributes = true;
  }

  if (!succ_empty()) {
    if (Indexes) OS << '\t';
    // Print the successors
    OS.indent(2) << "successors: ";
    for (auto I = succ_begin(), E = succ_end(); I != E; ++I) {
      if (I != succ_begin())
        OS << ", ";
      OS << printMBBReference(**I);
      if (!Probs.empty())
        OS << '('
           << format("0x%08" PRIx32, getSuccProbability(I).getNumerator())
           << ')';
    }
    if (!Probs.empty() && IsStandalone) {
      // Print human readable probabilities as comments.
      OS << "; ";
      for (auto I = succ_begin(), E = succ_end(); I != E; ++I) {
        const BranchProbability &BP = getSuccProbability(I);
        if (I != succ_begin())
          OS << ", ";
        OS << printMBBReference(**I) << '('
           << format("%.2f%%",
                     rint(((double)BP.getNumerator() / BP.getDenominator()) *
                          100.0 * 100.0) /
                         100.0)
           << ')';
      }
    }

    OS << '\n';
    HasLineAttributes = true;
  }

  if (!livein_empty() && MRI.tracksLiveness()) {
    if (Indexes) OS << '\t';
    OS.indent(2) << "liveins: ";

    bool First = true;
    for (const auto &LI : liveins()) {
      if (!First)
        OS << ", ";
      First = false;
      OS << printReg(LI.PhysReg, TRI);
      if (!LI.LaneMask.all())
        OS << ":0x" << PrintLaneMask(LI.LaneMask);
    }
    HasLineAttributes = true;
  }

  if (HasLineAttributes)
    OS << '\n';

  bool IsInBundle = false;
  for (const MachineInstr &MI : instrs()) {
    if (Indexes) {
      if (Indexes->hasIndex(MI))
        OS << Indexes->getInstructionIndex(MI);
      OS << '\t';
    }

    if (IsInBundle && !MI.isInsideBundle()) {
      OS.indent(2) << "}\n";
      IsInBundle = false;
    }

    OS.indent(IsInBundle ? 4 : 2);
    MI.print(OS, MST, IsStandalone, /*SkipOpers=*/false, /*SkipDebugLoc=*/false,
             /*AddNewLine=*/false, &TII);

    if (!IsInBundle && MI.getFlag(MachineInstr::BundledSucc)) {
      OS << " {";
      IsInBundle = true;
    }
    OS << '\n';
  }

  if (IsInBundle)
    OS.indent(2) << "}\n";

  if (IrrLoopHeaderWeight && IsStandalone) {
    if (Indexes) OS << '\t';
    OS.indent(2) << "; Irreducible loop header weight: "
                 << IrrLoopHeaderWeight.getValue() << '\n';
  }
}

void MachineBasicBlock::printAsOperand(raw_ostream &OS,
                                       bool /*PrintType*/) const {
  OS << "%bb." << getNumber();
}

void MachineBasicBlock::removeLiveIn(MCPhysReg Reg, LaneBitmask LaneMask) {
  LiveInVector::iterator I = find_if(
      LiveIns, [Reg](const RegisterMaskPair &LI) { return LI.PhysReg == Reg; });
  if (I == LiveIns.end())
    return;

  I->LaneMask &= ~LaneMask;
  if (I->LaneMask.none())
    LiveIns.erase(I);
}

MachineBasicBlock::livein_iterator
MachineBasicBlock::removeLiveIn(MachineBasicBlock::livein_iterator I) {
  // Get non-const version of iterator.
  LiveInVector::iterator LI = LiveIns.begin() + (I - LiveIns.begin());
  return LiveIns.erase(LI);
}

bool MachineBasicBlock::isLiveIn(MCPhysReg Reg, LaneBitmask LaneMask) const {
  livein_iterator I = find_if(
      LiveIns, [Reg](const RegisterMaskPair &LI) { return LI.PhysReg == Reg; });
  return I != livein_end() && (I->LaneMask & LaneMask).any();
}

void MachineBasicBlock::sortUniqueLiveIns() {
  llvm::sort(LiveIns,
             [](const RegisterMaskPair &LI0, const RegisterMaskPair &LI1) {
               return LI0.PhysReg < LI1.PhysReg;
             });
  // Liveins are sorted by physreg now we can merge their lanemasks.
  LiveInVector::const_iterator I = LiveIns.begin();
  LiveInVector::const_iterator J;
  LiveInVector::iterator Out = LiveIns.begin();
  for (; I != LiveIns.end(); ++Out, I = J) {
    unsigned PhysReg = I->PhysReg;
    LaneBitmask LaneMask = I->LaneMask;
    for (J = std::next(I); J != LiveIns.end() && J->PhysReg == PhysReg; ++J)
      LaneMask |= J->LaneMask;
    Out->PhysReg = PhysReg;
    Out->LaneMask = LaneMask;
  }
  LiveIns.erase(Out, LiveIns.end());
}

unsigned
MachineBasicBlock::addLiveIn(MCPhysReg PhysReg, const TargetRegisterClass *RC) {
  assert(getParent() && "MBB must be inserted in function");
  assert(TargetRegisterInfo::isPhysicalRegister(PhysReg) && "Expected physreg");
  assert(RC && "Register class is required");
  assert((isEHPad() || this == &getParent()->front()) &&
         "Only the entry block and landing pads can have physreg live ins");

  bool LiveIn = isLiveIn(PhysReg);
  iterator I = SkipPHIsAndLabels(begin()), E = end();
  MachineRegisterInfo &MRI = getParent()->getRegInfo();
  const TargetInstrInfo &TII = *getParent()->getSubtarget().getInstrInfo();

  // Look for an existing copy.
  if (LiveIn)
    for (;I != E && I->isCopy(); ++I)
      if (I->getOperand(1).getReg() == PhysReg) {
        unsigned VirtReg = I->getOperand(0).getReg();
        if (!MRI.constrainRegClass(VirtReg, RC))
          llvm_unreachable("Incompatible live-in register class.");
        return VirtReg;
      }

  // No luck, create a virtual register.
  unsigned VirtReg = MRI.createVirtualRegister(RC);
  BuildMI(*this, I, DebugLoc(), TII.get(TargetOpcode::COPY), VirtReg)
    .addReg(PhysReg, RegState::Kill);
  if (!LiveIn)
    addLiveIn(PhysReg);
  return VirtReg;
}

void MachineBasicBlock::moveBefore(MachineBasicBlock *NewAfter) {
  getParent()->splice(NewAfter->getIterator(), getIterator());
}

void MachineBasicBlock::moveAfter(MachineBasicBlock *NewBefore) {
  getParent()->splice(++NewBefore->getIterator(), getIterator());
}

void MachineBasicBlock::updateTerminator() {
  const TargetInstrInfo *TII = getParent()->getSubtarget().getInstrInfo();
  // A block with no successors has no concerns with fall-through edges.
  if (this->succ_empty())
    return;

  MachineBasicBlock *TBB = nullptr, *FBB = nullptr;
  SmallVector<MachineOperand, 4> Cond;
  DebugLoc DL = findBranchDebugLoc();
  bool B = TII->analyzeBranch(*this, TBB, FBB, Cond);
  (void) B;
  assert(!B && "UpdateTerminators requires analyzable predecessors!");
  if (Cond.empty()) {
    if (TBB) {
      // The block has an unconditional branch. If its successor is now its
      // layout successor, delete the branch.
      if (isLayoutSuccessor(TBB))
        TII->removeBranch(*this);
    } else {
      // The block has an unconditional fallthrough. If its successor is not its
      // layout successor, insert a branch. First we have to locate the only
      // non-landing-pad successor, as that is the fallthrough block.
      for (succ_iterator SI = succ_begin(), SE = succ_end(); SI != SE; ++SI) {
        if ((*SI)->isEHPad())
          continue;
        assert(!TBB && "Found more than one non-landing-pad successor!");
        TBB = *SI;
      }

      // If there is no non-landing-pad successor, the block has no fall-through
      // edges to be concerned with.
      if (!TBB)
        return;

      // Finally update the unconditional successor to be reached via a branch
      // if it would not be reached by fallthrough.
      if (!isLayoutSuccessor(TBB))
        TII->insertBranch(*this, TBB, nullptr, Cond, DL);
    }
    return;
  }

  if (FBB) {
    // The block has a non-fallthrough conditional branch. If one of its
    // successors is its layout successor, rewrite it to a fallthrough
    // conditional branch.
    if (isLayoutSuccessor(TBB)) {
      if (TII->reverseBranchCondition(Cond))
        return;
      TII->removeBranch(*this);
      TII->insertBranch(*this, FBB, nullptr, Cond, DL);
    } else if (isLayoutSuccessor(FBB)) {
      TII->removeBranch(*this);
      TII->insertBranch(*this, TBB, nullptr, Cond, DL);
    }
    return;
  }

  // Walk through the successors and find the successor which is not a landing
  // pad and is not the conditional branch destination (in TBB) as the
  // fallthrough successor.
  MachineBasicBlock *FallthroughBB = nullptr;
  for (succ_iterator SI = succ_begin(), SE = succ_end(); SI != SE; ++SI) {
    if ((*SI)->isEHPad() || *SI == TBB)
      continue;
    assert(!FallthroughBB && "Found more than one fallthrough successor.");
    FallthroughBB = *SI;
  }

  if (!FallthroughBB) {
    if (canFallThrough()) {
      // We fallthrough to the same basic block as the conditional jump targets.
      // Remove the conditional jump, leaving unconditional fallthrough.
      // FIXME: This does not seem like a reasonable pattern to support, but it
      // has been seen in the wild coming out of degenerate ARM test cases.
      TII->removeBranch(*this);

      // Finally update the unconditional successor to be reached via a branch if
      // it would not be reached by fallthrough.
      if (!isLayoutSuccessor(TBB))
        TII->insertBranch(*this, TBB, nullptr, Cond, DL);
      return;
    }

    // We enter here iff exactly one successor is TBB which cannot fallthrough
    // and the rest successors if any are EHPads.  In this case, we need to
    // change the conditional branch into unconditional branch.
    TII->removeBranch(*this);
    Cond.clear();
    TII->insertBranch(*this, TBB, nullptr, Cond, DL);
    return;
  }

  // The block has a fallthrough conditional branch.
  if (isLayoutSuccessor(TBB)) {
    if (TII->reverseBranchCondition(Cond)) {
      // We can't reverse the condition, add an unconditional branch.
      Cond.clear();
      TII->insertBranch(*this, FallthroughBB, nullptr, Cond, DL);
      return;
    }
    TII->removeBranch(*this);
    TII->insertBranch(*this, FallthroughBB, nullptr, Cond, DL);
  } else if (!isLayoutSuccessor(FallthroughBB)) {
    TII->removeBranch(*this);
    TII->insertBranch(*this, TBB, FallthroughBB, Cond, DL);
  }
}

void MachineBasicBlock::validateSuccProbs() const {
#ifndef NDEBUG
  int64_t Sum = 0;
  for (auto Prob : Probs)
    Sum += Prob.getNumerator();
  // Due to precision issue, we assume that the sum of probabilities is one if
  // the difference between the sum of their numerators and the denominator is
  // no greater than the number of successors.
  assert((uint64_t)std::abs(Sum - BranchProbability::getDenominator()) <=
             Probs.size() &&
         "The sum of successors's probabilities exceeds one.");
#endif // NDEBUG
}

void MachineBasicBlock::addSuccessor(MachineBasicBlock *Succ,
                                     BranchProbability Prob) {
  // Probability list is either empty (if successor list isn't empty, this means
  // disabled optimization) or has the same size as successor list.
  if (!(Probs.empty() && !Successors.empty()))
    Probs.push_back(Prob);
  Successors.push_back(Succ);
  Succ->addPredecessor(this);
}

void MachineBasicBlock::addSuccessorWithoutProb(MachineBasicBlock *Succ) {
  // We need to make sure probability list is either empty or has the same size
  // of successor list. When this function is called, we can safely delete all
  // probability in the list.
  Probs.clear();
  Successors.push_back(Succ);
  Succ->addPredecessor(this);
}

void MachineBasicBlock::splitSuccessor(MachineBasicBlock *Old,
                                       MachineBasicBlock *New,
                                       bool NormalizeSuccProbs) {
  succ_iterator OldI = llvm::find(successors(), Old);
  assert(OldI != succ_end() && "Old is not a successor of this block!");
  assert(llvm::find(successors(), New) == succ_end() &&
         "New is already a successor of this block!");

  // Add a new successor with equal probability as the original one. Note
  // that we directly copy the probability using the iterator rather than
  // getting a potentially synthetic probability computed when unknown. This
  // preserves the probabilities as-is and then we can renormalize them and
  // query them effectively afterward.
  addSuccessor(New, Probs.empty() ? BranchProbability::getUnknown()
                                  : *getProbabilityIterator(OldI));
  if (NormalizeSuccProbs)
    normalizeSuccProbs();
}

void MachineBasicBlock::removeSuccessor(MachineBasicBlock *Succ,
                                        bool NormalizeSuccProbs) {
  succ_iterator I = find(Successors, Succ);
  removeSuccessor(I, NormalizeSuccProbs);
}

MachineBasicBlock::succ_iterator
MachineBasicBlock::removeSuccessor(succ_iterator I, bool NormalizeSuccProbs) {
  assert(I != Successors.end() && "Not a current successor!");

  // If probability list is empty it means we don't use it (disabled
  // optimization).
  if (!Probs.empty()) {
    probability_iterator WI = getProbabilityIterator(I);
    Probs.erase(WI);
    if (NormalizeSuccProbs)
      normalizeSuccProbs();
  }

  (*I)->removePredecessor(this);
  return Successors.erase(I);
}

void MachineBasicBlock::replaceSuccessor(MachineBasicBlock *Old,
                                         MachineBasicBlock *New) {
  if (Old == New)
    return;

  succ_iterator E = succ_end();
  succ_iterator NewI = E;
  succ_iterator OldI = E;
  for (succ_iterator I = succ_begin(); I != E; ++I) {
    if (*I == Old) {
      OldI = I;
      if (NewI != E)
        break;
    }
    if (*I == New) {
      NewI = I;
      if (OldI != E)
        break;
    }
  }
  assert(OldI != E && "Old is not a successor of this block");

  // If New isn't already a successor, let it take Old's place.
  if (NewI == E) {
    Old->removePredecessor(this);
    New->addPredecessor(this);
    *OldI = New;
    return;
  }

  // New is already a successor.
  // Update its probability instead of adding a duplicate edge.
  if (!Probs.empty()) {
    auto ProbIter = getProbabilityIterator(NewI);
    if (!ProbIter->isUnknown())
      *ProbIter += *getProbabilityIterator(OldI);
  }
  removeSuccessor(OldI);
}

void MachineBasicBlock::copySuccessor(MachineBasicBlock *Orig,
                                      succ_iterator I) {
  if (Orig->Probs.empty())
    addSuccessor(*I, Orig->getSuccProbability(I));
  else
    addSuccessorWithoutProb(*I);
}

void MachineBasicBlock::addPredecessor(MachineBasicBlock *Pred) {
  Predecessors.push_back(Pred);
}

void MachineBasicBlock::removePredecessor(MachineBasicBlock *Pred) {
  pred_iterator I = find(Predecessors, Pred);
  assert(I != Predecessors.end() && "Pred is not a predecessor of this block!");
  Predecessors.erase(I);
}

void MachineBasicBlock::transferSuccessors(MachineBasicBlock *FromMBB) {
  if (this == FromMBB)
    return;

  while (!FromMBB->succ_empty()) {
    MachineBasicBlock *Succ = *FromMBB->succ_begin();

    // If probability list is empty it means we don't use it (disabled optimization).
    if (!FromMBB->Probs.empty()) {
      auto Prob = *FromMBB->Probs.begin();
      addSuccessor(Succ, Prob);
    } else
      addSuccessorWithoutProb(Succ);

    FromMBB->removeSuccessor(Succ);
  }
}

void
MachineBasicBlock::transferSuccessorsAndUpdatePHIs(MachineBasicBlock *FromMBB) {
  if (this == FromMBB)
    return;

  while (!FromMBB->succ_empty()) {
    MachineBasicBlock *Succ = *FromMBB->succ_begin();
    if (!FromMBB->Probs.empty()) {
      auto Prob = *FromMBB->Probs.begin();
      addSuccessor(Succ, Prob);
    } else
      addSuccessorWithoutProb(Succ);
    FromMBB->removeSuccessor(Succ);

    // Fix up any PHI nodes in the successor.
    for (MachineBasicBlock::instr_iterator MI = Succ->instr_begin(),
           ME = Succ->instr_end(); MI != ME && MI->isPHI(); ++MI)
      for (unsigned i = 2, e = MI->getNumOperands()+1; i != e; i += 2) {
        MachineOperand &MO = MI->getOperand(i);
        if (MO.getMBB() == FromMBB)
          MO.setMBB(this);
      }
  }
  normalizeSuccProbs();
}

bool MachineBasicBlock::isPredecessor(const MachineBasicBlock *MBB) const {
  return is_contained(predecessors(), MBB);
}

bool MachineBasicBlock::isSuccessor(const MachineBasicBlock *MBB) const {
  return is_contained(successors(), MBB);
}

bool MachineBasicBlock::isLayoutSuccessor(const MachineBasicBlock *MBB) const {
  MachineFunction::const_iterator I(this);
  return std::next(I) == MachineFunction::const_iterator(MBB);
}

MachineBasicBlock *MachineBasicBlock::getFallThrough() {
  MachineFunction::iterator Fallthrough = getIterator();
  ++Fallthrough;
  // If FallthroughBlock is off the end of the function, it can't fall through.
  if (Fallthrough == getParent()->end())
    return nullptr;

  // If FallthroughBlock isn't a successor, no fallthrough is possible.
  if (!isSuccessor(&*Fallthrough))
    return nullptr;

  // Analyze the branches, if any, at the end of the block.
  MachineBasicBlock *TBB = nullptr, *FBB = nullptr;
  SmallVector<MachineOperand, 4> Cond;
  const TargetInstrInfo *TII = getParent()->getSubtarget().getInstrInfo();
  if (TII->analyzeBranch(*this, TBB, FBB, Cond)) {
    // If we couldn't analyze the branch, examine the last instruction.
    // If the block doesn't end in a known control barrier, assume fallthrough
    // is possible. The isPredicated check is needed because this code can be
    // called during IfConversion, where an instruction which is normally a
    // Barrier is predicated and thus no longer an actual control barrier.
    return (empty() || !back().isBarrier() || TII->isPredicated(back()))
               ? &*Fallthrough
               : nullptr;
  }

  // If there is no branch, control always falls through.
  if (!TBB) return &*Fallthrough;

  // If there is some explicit branch to the fallthrough block, it can obviously
  // reach, even though the branch should get folded to fall through implicitly.
  if (MachineFunction::iterator(TBB) == Fallthrough ||
      MachineFunction::iterator(FBB) == Fallthrough)
    return &*Fallthrough;

  // If it's an unconditional branch to some block not the fall through, it
  // doesn't fall through.
  if (Cond.empty()) return nullptr;

  // Otherwise, if it is conditional and has no explicit false block, it falls
  // through.
  return (FBB == nullptr) ? &*Fallthrough : nullptr;
}

bool MachineBasicBlock::canFallThrough() {
  return getFallThrough() != nullptr;
}

MachineBasicBlock *MachineBasicBlock::SplitCriticalEdge(MachineBasicBlock *Succ,
                                                        Pass &P) {
  if (!canSplitCriticalEdge(Succ))
    return nullptr;

  MachineFunction *MF = getParent();
  DebugLoc DL;  // FIXME: this is nowhere

  MachineBasicBlock *NMBB = MF->CreateMachineBasicBlock();
  MF->insert(std::next(MachineFunction::iterator(this)), NMBB);
  LLVM_DEBUG(dbgs() << "Splitting critical edge: " << printMBBReference(*this)
                    << " -- " << printMBBReference(*NMBB) << " -- "
                    << printMBBReference(*Succ) << '\n');

  LiveIntervals *LIS = P.getAnalysisIfAvailable<LiveIntervals>();
  SlotIndexes *Indexes = P.getAnalysisIfAvailable<SlotIndexes>();
  if (LIS)
    LIS->insertMBBInMaps(NMBB);
  else if (Indexes)
    Indexes->insertMBBInMaps(NMBB);

  // On some targets like Mips, branches may kill virtual registers. Make sure
  // that LiveVariables is properly updated after updateTerminator replaces the
  // terminators.
  LiveVariables *LV = P.getAnalysisIfAvailable<LiveVariables>();

  // Collect a list of virtual registers killed by the terminators.
  SmallVector<unsigned, 4> KilledRegs;
  if (LV)
    for (instr_iterator I = getFirstInstrTerminator(), E = instr_end();
         I != E; ++I) {
      MachineInstr *MI = &*I;
      for (MachineInstr::mop_iterator OI = MI->operands_begin(),
           OE = MI->operands_end(); OI != OE; ++OI) {
        if (!OI->isReg() || OI->getReg() == 0 ||
            !OI->isUse() || !OI->isKill() || OI->isUndef())
          continue;
        unsigned Reg = OI->getReg();
        if (TargetRegisterInfo::isPhysicalRegister(Reg) ||
            LV->getVarInfo(Reg).removeKill(*MI)) {
          KilledRegs.push_back(Reg);
          LLVM_DEBUG(dbgs() << "Removing terminator kill: " << *MI);
          OI->setIsKill(false);
        }
      }
    }

  SmallVector<unsigned, 4> UsedRegs;
  if (LIS) {
    for (instr_iterator I = getFirstInstrTerminator(), E = instr_end();
         I != E; ++I) {
      MachineInstr *MI = &*I;

      for (MachineInstr::mop_iterator OI = MI->operands_begin(),
           OE = MI->operands_end(); OI != OE; ++OI) {
        if (!OI->isReg() || OI->getReg() == 0)
          continue;

        unsigned Reg = OI->getReg();
        if (!is_contained(UsedRegs, Reg))
          UsedRegs.push_back(Reg);
      }
    }
  }

  ReplaceUsesOfBlockWith(Succ, NMBB);

  // If updateTerminator() removes instructions, we need to remove them from
  // SlotIndexes.
  SmallVector<MachineInstr*, 4> Terminators;
  if (Indexes) {
    for (instr_iterator I = getFirstInstrTerminator(), E = instr_end();
         I != E; ++I)
      Terminators.push_back(&*I);
  }

  updateTerminator();

  if (Indexes) {
    SmallVector<MachineInstr*, 4> NewTerminators;
    for (instr_iterator I = getFirstInstrTerminator(), E = instr_end();
         I != E; ++I)
      NewTerminators.push_back(&*I);

    for (SmallVectorImpl<MachineInstr*>::iterator I = Terminators.begin(),
        E = Terminators.end(); I != E; ++I) {
      if (!is_contained(NewTerminators, *I))
        Indexes->removeMachineInstrFromMaps(**I);
    }
  }

  // Insert unconditional "jump Succ" instruction in NMBB if necessary.
  NMBB->addSuccessor(Succ);
  if (!NMBB->isLayoutSuccessor(Succ)) {
    SmallVector<MachineOperand, 4> Cond;
    const TargetInstrInfo *TII = getParent()->getSubtarget().getInstrInfo();
    TII->insertBranch(*NMBB, Succ, nullptr, Cond, DL);

    if (Indexes) {
      for (MachineInstr &MI : NMBB->instrs()) {
        // Some instructions may have been moved to NMBB by updateTerminator(),
        // so we first remove any instruction that already has an index.
        if (Indexes->hasIndex(MI))
          Indexes->removeMachineInstrFromMaps(MI);
        Indexes->insertMachineInstrInMaps(MI);
      }
    }
  }

  // Fix PHI nodes in Succ so they refer to NMBB instead of this
  for (MachineBasicBlock::instr_iterator
         i = Succ->instr_begin(),e = Succ->instr_end();
       i != e && i->isPHI(); ++i)
    for (unsigned ni = 1, ne = i->getNumOperands(); ni != ne; ni += 2)
      if (i->getOperand(ni+1).getMBB() == this)
        i->getOperand(ni+1).setMBB(NMBB);

  // Inherit live-ins from the successor
  for (const auto &LI : Succ->liveins())
    NMBB->addLiveIn(LI);

  // Update LiveVariables.
  const TargetRegisterInfo *TRI = MF->getSubtarget().getRegisterInfo();
  if (LV) {
    // Restore kills of virtual registers that were killed by the terminators.
    while (!KilledRegs.empty()) {
      unsigned Reg = KilledRegs.pop_back_val();
      for (instr_iterator I = instr_end(), E = instr_begin(); I != E;) {
        if (!(--I)->addRegisterKilled(Reg, TRI, /* addIfNotFound= */ false))
          continue;
        if (TargetRegisterInfo::isVirtualRegister(Reg))
          LV->getVarInfo(Reg).Kills.push_back(&*I);
        LLVM_DEBUG(dbgs() << "Restored terminator kill: " << *I);
        break;
      }
    }
    // Update relevant live-through information.
    LV->addNewBlock(NMBB, this, Succ);
  }

  if (LIS) {
    // After splitting the edge and updating SlotIndexes, live intervals may be
    // in one of two situations, depending on whether this block was the last in
    // the function. If the original block was the last in the function, all
    // live intervals will end prior to the beginning of the new split block. If
    // the original block was not at the end of the function, all live intervals
    // will extend to the end of the new split block.

    bool isLastMBB =
      std::next(MachineFunction::iterator(NMBB)) == getParent()->end();

    SlotIndex StartIndex = Indexes->getMBBEndIdx(this);
    SlotIndex PrevIndex = StartIndex.getPrevSlot();
    SlotIndex EndIndex = Indexes->getMBBEndIdx(NMBB);

    // Find the registers used from NMBB in PHIs in Succ.
    SmallSet<unsigned, 8> PHISrcRegs;
    for (MachineBasicBlock::instr_iterator
         I = Succ->instr_begin(), E = Succ->instr_end();
         I != E && I->isPHI(); ++I) {
      for (unsigned ni = 1, ne = I->getNumOperands(); ni != ne; ni += 2) {
        if (I->getOperand(ni+1).getMBB() == NMBB) {
          MachineOperand &MO = I->getOperand(ni);
          unsigned Reg = MO.getReg();
          PHISrcRegs.insert(Reg);
          if (MO.isUndef())
            continue;

          LiveInterval &LI = LIS->getInterval(Reg);
          VNInfo *VNI = LI.getVNInfoAt(PrevIndex);
          assert(VNI &&
                 "PHI sources should be live out of their predecessors.");
          LI.addSegment(LiveInterval::Segment(StartIndex, EndIndex, VNI));
        }
      }
    }

    MachineRegisterInfo *MRI = &getParent()->getRegInfo();
    for (unsigned i = 0, e = MRI->getNumVirtRegs(); i != e; ++i) {
      unsigned Reg = TargetRegisterInfo::index2VirtReg(i);
      if (PHISrcRegs.count(Reg) || !LIS->hasInterval(Reg))
        continue;

      LiveInterval &LI = LIS->getInterval(Reg);
      if (!LI.liveAt(PrevIndex))
        continue;

      bool isLiveOut = LI.liveAt(LIS->getMBBStartIdx(Succ));
      if (isLiveOut && isLastMBB) {
        VNInfo *VNI = LI.getVNInfoAt(PrevIndex);
        assert(VNI && "LiveInterval should have VNInfo where it is live.");
        LI.addSegment(LiveInterval::Segment(StartIndex, EndIndex, VNI));
      } else if (!isLiveOut && !isLastMBB) {
        LI.removeSegment(StartIndex, EndIndex);
      }
    }

    // Update all intervals for registers whose uses may have been modified by
    // updateTerminator().
    LIS->repairIntervalsInRange(this, getFirstTerminator(), end(), UsedRegs);
  }

  if (MachineDominatorTree *MDT =
          P.getAnalysisIfAvailable<MachineDominatorTree>())
    MDT->recordSplitCriticalEdge(this, Succ, NMBB);

  if (MachineLoopInfo *MLI = P.getAnalysisIfAvailable<MachineLoopInfo>())
    if (MachineLoop *TIL = MLI->getLoopFor(this)) {
      // If one or the other blocks were not in a loop, the new block is not
      // either, and thus LI doesn't need to be updated.
      if (MachineLoop *DestLoop = MLI->getLoopFor(Succ)) {
        if (TIL == DestLoop) {
          // Both in the same loop, the NMBB joins loop.
          DestLoop->addBasicBlockToLoop(NMBB, MLI->getBase());
        } else if (TIL->contains(DestLoop)) {
          // Edge from an outer loop to an inner loop.  Add to the outer loop.
          TIL->addBasicBlockToLoop(NMBB, MLI->getBase());
        } else if (DestLoop->contains(TIL)) {
          // Edge from an inner loop to an outer loop.  Add to the outer loop.
          DestLoop->addBasicBlockToLoop(NMBB, MLI->getBase());
        } else {
          // Edge from two loops with no containment relation.  Because these
          // are natural loops, we know that the destination block must be the
          // header of its loop (adding a branch into a loop elsewhere would
          // create an irreducible loop).
          assert(DestLoop->getHeader() == Succ &&
                 "Should not create irreducible loops!");
          if (MachineLoop *P = DestLoop->getParentLoop())
            P->addBasicBlockToLoop(NMBB, MLI->getBase());
        }
      }
    }

  return NMBB;
}

bool MachineBasicBlock::canSplitCriticalEdge(
    const MachineBasicBlock *Succ) const {
  // Splitting the critical edge to a landing pad block is non-trivial. Don't do
  // it in this generic function.
  if (Succ->isEHPad())
    return false;

  const MachineFunction *MF = getParent();

  // Performance might be harmed on HW that implements branching using exec mask
  // where both sides of the branches are always executed.
  if (MF->getTarget().requiresStructuredCFG())
    return false;

  // We may need to update this's terminator, but we can't do that if
  // AnalyzeBranch fails. If this uses a jump table, we won't touch it.
  const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();
  MachineBasicBlock *TBB = nullptr, *FBB = nullptr;
  SmallVector<MachineOperand, 4> Cond;
  // AnalyzeBanch should modify this, since we did not allow modification.
  if (TII->analyzeBranch(*const_cast<MachineBasicBlock *>(this), TBB, FBB, Cond,
                         /*AllowModify*/ false))
    return false;

  // Avoid bugpoint weirdness: A block may end with a conditional branch but
  // jumps to the same MBB is either case. We have duplicate CFG edges in that
  // case that we can't handle. Since this never happens in properly optimized
  // code, just skip those edges.
  if (TBB && TBB == FBB) {
    LLVM_DEBUG(dbgs() << "Won't split critical edge after degenerate "
                      << printMBBReference(*this) << '\n');
    return false;
  }
  return true;
}

/// Prepare MI to be removed from its bundle. This fixes bundle flags on MI's
/// neighboring instructions so the bundle won't be broken by removing MI.
static void unbundleSingleMI(MachineInstr *MI) {
  // Removing the first instruction in a bundle.
  if (MI->isBundledWithSucc() && !MI->isBundledWithPred())
    MI->unbundleFromSucc();
  // Removing the last instruction in a bundle.
  if (MI->isBundledWithPred() && !MI->isBundledWithSucc())
    MI->unbundleFromPred();
  // If MI is not bundled, or if it is internal to a bundle, the neighbor flags
  // are already fine.
}

MachineBasicBlock::instr_iterator
MachineBasicBlock::erase(MachineBasicBlock::instr_iterator I) {
  unbundleSingleMI(&*I);
  return Insts.erase(I);
}

MachineInstr *MachineBasicBlock::remove_instr(MachineInstr *MI) {
  unbundleSingleMI(MI);
  MI->clearFlag(MachineInstr::BundledPred);
  MI->clearFlag(MachineInstr::BundledSucc);
  return Insts.remove(MI);
}

MachineBasicBlock::instr_iterator
MachineBasicBlock::insert(instr_iterator I, MachineInstr *MI) {
  assert(!MI->isBundledWithPred() && !MI->isBundledWithSucc() &&
         "Cannot insert instruction with bundle flags");
  // Set the bundle flags when inserting inside a bundle.
  if (I != instr_end() && I->isBundledWithPred()) {
    MI->setFlag(MachineInstr::BundledPred);
    MI->setFlag(MachineInstr::BundledSucc);
  }
  return Insts.insert(I, MI);
}

/// This method unlinks 'this' from the containing function, and returns it, but
/// does not delete it.
MachineBasicBlock *MachineBasicBlock::removeFromParent() {
  assert(getParent() && "Not embedded in a function!");
  getParent()->remove(this);
  return this;
}

/// This method unlinks 'this' from the containing function, and deletes it.
void MachineBasicBlock::eraseFromParent() {
  assert(getParent() && "Not embedded in a function!");
  getParent()->erase(this);
}

/// Given a machine basic block that branched to 'Old', change the code and CFG
/// so that it branches to 'New' instead.
void MachineBasicBlock::ReplaceUsesOfBlockWith(MachineBasicBlock *Old,
                                               MachineBasicBlock *New) {
  assert(Old != New && "Cannot replace self with self!");

  MachineBasicBlock::instr_iterator I = instr_end();
  while (I != instr_begin()) {
    --I;
    if (!I->isTerminator()) break;

    // Scan the operands of this machine instruction, replacing any uses of Old
    // with New.
    for (unsigned i = 0, e = I->getNumOperands(); i != e; ++i)
      if (I->getOperand(i).isMBB() &&
          I->getOperand(i).getMBB() == Old)
        I->getOperand(i).setMBB(New);
  }

  // Update the successor information.
  replaceSuccessor(Old, New);
}

/// Various pieces of code can cause excess edges in the CFG to be inserted.  If
/// we have proven that MBB can only branch to DestA and DestB, remove any other
/// MBB successors from the CFG.  DestA and DestB can be null.
///
/// Besides DestA and DestB, retain other edges leading to LandingPads
/// (currently there can be only one; we don't check or require that here).
/// Note it is possible that DestA and/or DestB are LandingPads.
bool MachineBasicBlock::CorrectExtraCFGEdges(MachineBasicBlock *DestA,
                                             MachineBasicBlock *DestB,
                                             bool IsCond) {
  // The values of DestA and DestB frequently come from a call to the
  // 'TargetInstrInfo::AnalyzeBranch' method. We take our meaning of the initial
  // values from there.
  //
  // 1. If both DestA and DestB are null, then the block ends with no branches
  //    (it falls through to its successor).
  // 2. If DestA is set, DestB is null, and IsCond is false, then the block ends
  //    with only an unconditional branch.
  // 3. If DestA is set, DestB is null, and IsCond is true, then the block ends
  //    with a conditional branch that falls through to a successor (DestB).
  // 4. If DestA and DestB is set and IsCond is true, then the block ends with a
  //    conditional branch followed by an unconditional branch. DestA is the
  //    'true' destination and DestB is the 'false' destination.

  bool Changed = false;

  MachineBasicBlock *FallThru = getNextNode();

  if (!DestA && !DestB) {
    // Block falls through to successor.
    DestA = FallThru;
    DestB = FallThru;
  } else if (DestA && !DestB) {
    if (IsCond)
      // Block ends in conditional jump that falls through to successor.
      DestB = FallThru;
  } else {
    assert(DestA && DestB && IsCond &&
           "CFG in a bad state. Cannot correct CFG edges");
  }

  // Remove superfluous edges. I.e., those which aren't destinations of this
  // basic block, duplicate edges, or landing pads.
  SmallPtrSet<const MachineBasicBlock*, 8> SeenMBBs;
  MachineBasicBlock::succ_iterator SI = succ_begin();
  while (SI != succ_end()) {
    const MachineBasicBlock *MBB = *SI;
    if (!SeenMBBs.insert(MBB).second ||
        (MBB != DestA && MBB != DestB && !MBB->isEHPad())) {
      // This is a superfluous edge, remove it.
      SI = removeSuccessor(SI);
      Changed = true;
    } else {
      ++SI;
    }
  }

  if (Changed)
    normalizeSuccProbs();
  return Changed;
}

/// Find the next valid DebugLoc starting at MBBI, skipping any DBG_VALUE
/// instructions.  Return UnknownLoc if there is none.
DebugLoc
MachineBasicBlock::findDebugLoc(instr_iterator MBBI) {
  // Skip debug declarations, we don't want a DebugLoc from them.
  MBBI = skipDebugInstructionsForward(MBBI, instr_end());
  if (MBBI != instr_end())
    return MBBI->getDebugLoc();
  return {};
}

/// Find the previous valid DebugLoc preceding MBBI, skipping and DBG_VALUE
/// instructions.  Return UnknownLoc if there is none.
DebugLoc MachineBasicBlock::findPrevDebugLoc(instr_iterator MBBI) {
  if (MBBI == instr_begin()) return {};
  // Skip debug declarations, we don't want a DebugLoc from them.
  MBBI = skipDebugInstructionsBackward(std::prev(MBBI), instr_begin());
  if (!MBBI->isDebugInstr()) return MBBI->getDebugLoc();
  return {};
}

/// Find and return the merged DebugLoc of the branch instructions of the block.
/// Return UnknownLoc if there is none.
DebugLoc
MachineBasicBlock::findBranchDebugLoc() {
  DebugLoc DL;
  auto TI = getFirstTerminator();
  while (TI != end() && !TI->isBranch())
    ++TI;

  if (TI != end()) {
    DL = TI->getDebugLoc();
    for (++TI ; TI != end() ; ++TI)
      if (TI->isBranch())
        DL = DILocation::getMergedLocation(DL, TI->getDebugLoc());
  }
  return DL;
}

/// Return probability of the edge from this block to MBB.
BranchProbability
MachineBasicBlock::getSuccProbability(const_succ_iterator Succ) const {
  if (Probs.empty())
    return BranchProbability(1, succ_size());

  const auto &Prob = *getProbabilityIterator(Succ);
  if (Prob.isUnknown()) {
    // For unknown probabilities, collect the sum of all known ones, and evenly
    // ditribute the complemental of the sum to each unknown probability.
    unsigned KnownProbNum = 0;
    auto Sum = BranchProbability::getZero();
    for (auto &P : Probs) {
      if (!P.isUnknown()) {
        Sum += P;
        KnownProbNum++;
      }
    }
    return Sum.getCompl() / (Probs.size() - KnownProbNum);
  } else
    return Prob;
}

/// Set successor probability of a given iterator.
void MachineBasicBlock::setSuccProbability(succ_iterator I,
                                           BranchProbability Prob) {
  assert(!Prob.isUnknown());
  if (Probs.empty())
    return;
  *getProbabilityIterator(I) = Prob;
}

/// Return probability iterator corresonding to the I successor iterator
MachineBasicBlock::const_probability_iterator
MachineBasicBlock::getProbabilityIterator(
    MachineBasicBlock::const_succ_iterator I) const {
  assert(Probs.size() == Successors.size() && "Async probability list!");
  const size_t index = std::distance(Successors.begin(), I);
  assert(index < Probs.size() && "Not a current successor!");
  return Probs.begin() + index;
}

/// Return probability iterator corresonding to the I successor iterator.
MachineBasicBlock::probability_iterator
MachineBasicBlock::getProbabilityIterator(MachineBasicBlock::succ_iterator I) {
  assert(Probs.size() == Successors.size() && "Async probability list!");
  const size_t index = std::distance(Successors.begin(), I);
  assert(index < Probs.size() && "Not a current successor!");
  return Probs.begin() + index;
}

/// Return whether (physical) register "Reg" has been <def>ined and not <kill>ed
/// as of just before "MI".
///
/// Search is localised to a neighborhood of
/// Neighborhood instructions before (searching for defs or kills) and N
/// instructions after (searching just for defs) MI.
MachineBasicBlock::LivenessQueryResult
MachineBasicBlock::computeRegisterLiveness(const TargetRegisterInfo *TRI,
                                           unsigned Reg, const_iterator Before,
                                           unsigned Neighborhood) const {
  unsigned N = Neighborhood;

  // Try searching forwards from Before, looking for reads or defs.
  const_iterator I(Before);
  for (; I != end() && N > 0; ++I) {
    if (I->isDebugInstr())
      continue;

    --N;

    MachineOperandIteratorBase::PhysRegInfo Info =
        ConstMIOperands(*I).analyzePhysReg(Reg, TRI);

    // Register is live when we read it here.
    if (Info.Read)
      return LQR_Live;
    // Register is dead if we can fully overwrite or clobber it here.
    if (Info.FullyDefined || Info.Clobbered)
      return LQR_Dead;
  }

  // If we reached the end, it is safe to clobber Reg at the end of a block of
  // no successor has it live in.
  if (I == end()) {
    for (MachineBasicBlock *S : successors()) {
      for (const MachineBasicBlock::RegisterMaskPair &LI : S->liveins()) {
        if (TRI->regsOverlap(LI.PhysReg, Reg))
          return LQR_Live;
      }
    }

    return LQR_Dead;
  }


  N = Neighborhood;

  // Start by searching backwards from Before, looking for kills, reads or defs.
  I = const_iterator(Before);
  // If this is the first insn in the block, don't search backwards.
  if (I != begin()) {
    do {
      --I;

      if (I->isDebugInstr())
        continue;

      --N;

      MachineOperandIteratorBase::PhysRegInfo Info =
          ConstMIOperands(*I).analyzePhysReg(Reg, TRI);

      // Defs happen after uses so they take precedence if both are present.

      // Register is dead after a dead def of the full register.
      if (Info.DeadDef)
        return LQR_Dead;
      // Register is (at least partially) live after a def.
      if (Info.Defined) {
        if (!Info.PartialDeadDef)
          return LQR_Live;
        // As soon as we saw a partial definition (dead or not),
        // we cannot tell if the value is partial live without
        // tracking the lanemasks. We are not going to do this,
        // so fall back on the remaining of the analysis.
        break;
      }
      // Register is dead after a full kill or clobber and no def.
      if (Info.Killed || Info.Clobbered)
        return LQR_Dead;
      // Register must be live if we read it.
      if (Info.Read)
        return LQR_Live;

    } while (I != begin() && N > 0);
  }

  // Did we get to the start of the block?
  if (I == begin()) {
    // If so, the register's state is definitely defined by the live-in state.
    for (const MachineBasicBlock::RegisterMaskPair &LI : liveins())
      if (TRI->regsOverlap(LI.PhysReg, Reg))
        return LQR_Live;

    return LQR_Dead;
  }

  // At this point we have no idea of the liveness of the register.
  return LQR_Unknown;
}

const uint32_t *
MachineBasicBlock::getBeginClobberMask(const TargetRegisterInfo *TRI) const {
  // EH funclet entry does not preserve any registers.
  return isEHFuncletEntry() ? TRI->getNoPreservedMask() : nullptr;
}

const uint32_t *
MachineBasicBlock::getEndClobberMask(const TargetRegisterInfo *TRI) const {
  // If we see a return block with successors, this must be a funclet return,
  // which does not preserve any registers. If there are no successors, we don't
  // care what kind of return it is, putting a mask after it is a no-op.
  return isReturnBlock() && !succ_empty() ? TRI->getNoPreservedMask() : nullptr;
}

void MachineBasicBlock::clearLiveIns() {
  LiveIns.clear();
}

MachineBasicBlock::livein_iterator MachineBasicBlock::livein_begin() const {
  assert(getParent()->getProperties().hasProperty(
      MachineFunctionProperties::Property::TracksLiveness) &&
      "Liveness information is accurate");
  return LiveIns.begin();
}
