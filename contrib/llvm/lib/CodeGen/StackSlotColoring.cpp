//===- StackSlotColoring.cpp - Stack slot coloring pass. ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the stack slot coloring pass.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/LiveStacks.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "stack-slot-coloring"

static cl::opt<bool>
DisableSharing("no-stack-slot-sharing",
             cl::init(false), cl::Hidden,
             cl::desc("Suppress slot sharing during stack coloring"));

static cl::opt<int> DCELimit("ssc-dce-limit", cl::init(-1), cl::Hidden);

STATISTIC(NumEliminated, "Number of stack slots eliminated due to coloring");
STATISTIC(NumDead,       "Number of trivially dead stack accesses eliminated");

namespace {

  class StackSlotColoring : public MachineFunctionPass {
    LiveStacks* LS;
    MachineFrameInfo *MFI;
    const TargetInstrInfo  *TII;
    const MachineBlockFrequencyInfo *MBFI;

    // SSIntervals - Spill slot intervals.
    std::vector<LiveInterval*> SSIntervals;

    // SSRefs - Keep a list of MachineMemOperands for each spill slot.
    // MachineMemOperands can be shared between instructions, so we need
    // to be careful that renames like [FI0, FI1] -> [FI1, FI2] do not
    // become FI0 -> FI1 -> FI2.
    SmallVector<SmallVector<MachineMemOperand *, 8>, 16> SSRefs;

    // OrigAlignments - Alignments of stack objects before coloring.
    SmallVector<unsigned, 16> OrigAlignments;

    // OrigSizes - Sizess of stack objects before coloring.
    SmallVector<unsigned, 16> OrigSizes;

    // AllColors - If index is set, it's a spill slot, i.e. color.
    // FIXME: This assumes PEI locate spill slot with smaller indices
    // closest to stack pointer / frame pointer. Therefore, smaller
    // index == better color. This is per stack ID.
    SmallVector<BitVector, 2> AllColors;

    // NextColor - Next "color" that's not yet used. This is per stack ID.
    SmallVector<int, 2> NextColors = { -1 };

    // UsedColors - "Colors" that have been assigned. This is per stack ID
    SmallVector<BitVector, 2> UsedColors;

    // Assignments - Color to intervals mapping.
    SmallVector<SmallVector<LiveInterval*,4>, 16> Assignments;

  public:
    static char ID; // Pass identification

    StackSlotColoring() : MachineFunctionPass(ID) {
      initializeStackSlotColoringPass(*PassRegistry::getPassRegistry());
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesCFG();
      AU.addRequired<SlotIndexes>();
      AU.addPreserved<SlotIndexes>();
      AU.addRequired<LiveStacks>();
      AU.addRequired<MachineBlockFrequencyInfo>();
      AU.addPreserved<MachineBlockFrequencyInfo>();
      AU.addPreservedID(MachineDominatorsID);
      MachineFunctionPass::getAnalysisUsage(AU);
    }

    bool runOnMachineFunction(MachineFunction &MF) override;

  private:
    void InitializeSlots();
    void ScanForSpillSlotRefs(MachineFunction &MF);
    bool OverlapWithAssignments(LiveInterval *li, int Color) const;
    int ColorSlot(LiveInterval *li);
    bool ColorSlots(MachineFunction &MF);
    void RewriteInstruction(MachineInstr &MI, SmallVectorImpl<int> &SlotMapping,
                            MachineFunction &MF);
    bool RemoveDeadStores(MachineBasicBlock* MBB);
  };

} // end anonymous namespace

char StackSlotColoring::ID = 0;

char &llvm::StackSlotColoringID = StackSlotColoring::ID;

INITIALIZE_PASS_BEGIN(StackSlotColoring, DEBUG_TYPE,
                "Stack Slot Coloring", false, false)
INITIALIZE_PASS_DEPENDENCY(SlotIndexes)
INITIALIZE_PASS_DEPENDENCY(LiveStacks)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfo)
INITIALIZE_PASS_END(StackSlotColoring, DEBUG_TYPE,
                "Stack Slot Coloring", false, false)

namespace {

// IntervalSorter - Comparison predicate that sort live intervals by
// their weight.
struct IntervalSorter {
  bool operator()(LiveInterval* LHS, LiveInterval* RHS) const {
    return LHS->weight > RHS->weight;
  }
};

} // end anonymous namespace

/// ScanForSpillSlotRefs - Scan all the machine instructions for spill slot
/// references and update spill slot weights.
void StackSlotColoring::ScanForSpillSlotRefs(MachineFunction &MF) {
  SSRefs.resize(MFI->getObjectIndexEnd());

  // FIXME: Need the equivalent of MachineRegisterInfo for frameindex operands.
  for (MachineFunction::iterator MBBI = MF.begin(), E = MF.end();
       MBBI != E; ++MBBI) {
    MachineBasicBlock *MBB = &*MBBI;
    for (MachineBasicBlock::iterator MII = MBB->begin(), EE = MBB->end();
         MII != EE; ++MII) {
      MachineInstr &MI = *MII;
      for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
        MachineOperand &MO = MI.getOperand(i);
        if (!MO.isFI())
          continue;
        int FI = MO.getIndex();
        if (FI < 0)
          continue;
        if (!LS->hasInterval(FI))
          continue;
        LiveInterval &li = LS->getInterval(FI);
        if (!MI.isDebugValue())
          li.weight += LiveIntervals::getSpillWeight(false, true, MBFI, MI);
      }
      for (MachineInstr::mmo_iterator MMOI = MI.memoperands_begin(),
                                      EE = MI.memoperands_end();
           MMOI != EE; ++MMOI) {
        MachineMemOperand *MMO = *MMOI;
        if (const FixedStackPseudoSourceValue *FSV =
            dyn_cast_or_null<FixedStackPseudoSourceValue>(
                MMO->getPseudoValue())) {
          int FI = FSV->getFrameIndex();
          if (FI >= 0)
            SSRefs[FI].push_back(MMO);
        }
      }
    }
  }
}

/// InitializeSlots - Process all spill stack slot liveintervals and add them
/// to a sorted (by weight) list.
void StackSlotColoring::InitializeSlots() {
  int LastFI = MFI->getObjectIndexEnd();

  // There is always at least one stack ID.
  AllColors.resize(1);
  UsedColors.resize(1);

  OrigAlignments.resize(LastFI);
  OrigSizes.resize(LastFI);
  AllColors[0].resize(LastFI);
  UsedColors[0].resize(LastFI);
  Assignments.resize(LastFI);

  using Pair = std::iterator_traits<LiveStacks::iterator>::value_type;

  SmallVector<Pair *, 16> Intervals;

  Intervals.reserve(LS->getNumIntervals());
  for (auto &I : *LS)
    Intervals.push_back(&I);
  llvm::sort(Intervals,
             [](Pair *LHS, Pair *RHS) { return LHS->first < RHS->first; });

  // Gather all spill slots into a list.
  LLVM_DEBUG(dbgs() << "Spill slot intervals:\n");
  for (auto *I : Intervals) {
    LiveInterval &li = I->second;
    LLVM_DEBUG(li.dump());
    int FI = TargetRegisterInfo::stackSlot2Index(li.reg);
    if (MFI->isDeadObjectIndex(FI))
      continue;

    SSIntervals.push_back(&li);
    OrigAlignments[FI] = MFI->getObjectAlignment(FI);
    OrigSizes[FI]      = MFI->getObjectSize(FI);

    auto StackID = MFI->getStackID(FI);
    if (StackID != 0) {
      AllColors.resize(StackID + 1);
      UsedColors.resize(StackID + 1);
      AllColors[StackID].resize(LastFI);
      UsedColors[StackID].resize(LastFI);
    }

    AllColors[StackID].set(FI);
  }
  LLVM_DEBUG(dbgs() << '\n');

  // Sort them by weight.
  std::stable_sort(SSIntervals.begin(), SSIntervals.end(), IntervalSorter());

  NextColors.resize(AllColors.size());

  // Get first "color".
  for (unsigned I = 0, E = AllColors.size(); I != E; ++I)
    NextColors[I] = AllColors[I].find_first();
}

/// OverlapWithAssignments - Return true if LiveInterval overlaps with any
/// LiveIntervals that have already been assigned to the specified color.
bool
StackSlotColoring::OverlapWithAssignments(LiveInterval *li, int Color) const {
  const SmallVectorImpl<LiveInterval *> &OtherLIs = Assignments[Color];
  for (unsigned i = 0, e = OtherLIs.size(); i != e; ++i) {
    LiveInterval *OtherLI = OtherLIs[i];
    if (OtherLI->overlaps(*li))
      return true;
  }
  return false;
}

/// ColorSlot - Assign a "color" (stack slot) to the specified stack slot.
int StackSlotColoring::ColorSlot(LiveInterval *li) {
  int Color = -1;
  bool Share = false;
  int FI = TargetRegisterInfo::stackSlot2Index(li->reg);
  uint8_t StackID = MFI->getStackID(FI);

  if (!DisableSharing) {

    // Check if it's possible to reuse any of the used colors.
    Color = UsedColors[StackID].find_first();
    while (Color != -1) {
      if (!OverlapWithAssignments(li, Color)) {
        Share = true;
        ++NumEliminated;
        break;
      }
      Color = UsedColors[StackID].find_next(Color);
    }
  }

  if (Color != -1 && MFI->getStackID(Color) != MFI->getStackID(FI)) {
    LLVM_DEBUG(dbgs() << "cannot share FIs with different stack IDs\n");
    Share = false;
  }

  // Assign it to the first available color (assumed to be the best) if it's
  // not possible to share a used color with other objects.
  if (!Share) {
    assert(NextColors[StackID] != -1 && "No more spill slots?");
    Color = NextColors[StackID];
    UsedColors[StackID].set(Color);
    NextColors[StackID] = AllColors[StackID].find_next(NextColors[StackID]);
  }

  assert(MFI->getStackID(Color) == MFI->getStackID(FI));

  // Record the assignment.
  Assignments[Color].push_back(li);
  LLVM_DEBUG(dbgs() << "Assigning fi#" << FI << " to fi#" << Color << "\n");

  // Change size and alignment of the allocated slot. If there are multiple
  // objects sharing the same slot, then make sure the size and alignment
  // are large enough for all.
  unsigned Align = OrigAlignments[FI];
  if (!Share || Align > MFI->getObjectAlignment(Color))
    MFI->setObjectAlignment(Color, Align);
  int64_t Size = OrigSizes[FI];
  if (!Share || Size > MFI->getObjectSize(Color))
    MFI->setObjectSize(Color, Size);
  return Color;
}

/// Colorslots - Color all spill stack slots and rewrite all frameindex machine
/// operands in the function.
bool StackSlotColoring::ColorSlots(MachineFunction &MF) {
  unsigned NumObjs = MFI->getObjectIndexEnd();
  SmallVector<int, 16> SlotMapping(NumObjs, -1);
  SmallVector<float, 16> SlotWeights(NumObjs, 0.0);
  SmallVector<SmallVector<int, 4>, 16> RevMap(NumObjs);
  BitVector UsedColors(NumObjs);

  LLVM_DEBUG(dbgs() << "Color spill slot intervals:\n");
  bool Changed = false;
  for (unsigned i = 0, e = SSIntervals.size(); i != e; ++i) {
    LiveInterval *li = SSIntervals[i];
    int SS = TargetRegisterInfo::stackSlot2Index(li->reg);
    int NewSS = ColorSlot(li);
    assert(NewSS >= 0 && "Stack coloring failed?");
    SlotMapping[SS] = NewSS;
    RevMap[NewSS].push_back(SS);
    SlotWeights[NewSS] += li->weight;
    UsedColors.set(NewSS);
    Changed |= (SS != NewSS);
  }

  LLVM_DEBUG(dbgs() << "\nSpill slots after coloring:\n");
  for (unsigned i = 0, e = SSIntervals.size(); i != e; ++i) {
    LiveInterval *li = SSIntervals[i];
    int SS = TargetRegisterInfo::stackSlot2Index(li->reg);
    li->weight = SlotWeights[SS];
  }
  // Sort them by new weight.
  std::stable_sort(SSIntervals.begin(), SSIntervals.end(), IntervalSorter());

#ifndef NDEBUG
  for (unsigned i = 0, e = SSIntervals.size(); i != e; ++i)
    LLVM_DEBUG(SSIntervals[i]->dump());
  LLVM_DEBUG(dbgs() << '\n');
#endif

  if (!Changed)
    return false;

  // Rewrite all MachineMemOperands.
  for (unsigned SS = 0, SE = SSRefs.size(); SS != SE; ++SS) {
    int NewFI = SlotMapping[SS];
    if (NewFI == -1 || (NewFI == (int)SS))
      continue;

    const PseudoSourceValue *NewSV = MF.getPSVManager().getFixedStack(NewFI);
    SmallVectorImpl<MachineMemOperand *> &RefMMOs = SSRefs[SS];
    for (unsigned i = 0, e = RefMMOs.size(); i != e; ++i)
      RefMMOs[i]->setValue(NewSV);
  }

  // Rewrite all MO_FrameIndex operands.  Look for dead stores.
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB)
      RewriteInstruction(MI, SlotMapping, MF);
    RemoveDeadStores(&MBB);
  }

  // Delete unused stack slots.
  for (int StackID = 0, E = AllColors.size(); StackID != E; ++StackID) {
    int NextColor = NextColors[StackID];
    while (NextColor != -1) {
      LLVM_DEBUG(dbgs() << "Removing unused stack object fi#" << NextColor << "\n");
      MFI->RemoveStackObject(NextColor);
      NextColor = AllColors[StackID].find_next(NextColor);
    }
  }

  return true;
}

/// RewriteInstruction - Rewrite specified instruction by replacing references
/// to old frame index with new one.
void StackSlotColoring::RewriteInstruction(MachineInstr &MI,
                                           SmallVectorImpl<int> &SlotMapping,
                                           MachineFunction &MF) {
  // Update the operands.
  for (unsigned i = 0, ee = MI.getNumOperands(); i != ee; ++i) {
    MachineOperand &MO = MI.getOperand(i);
    if (!MO.isFI())
      continue;
    int OldFI = MO.getIndex();
    if (OldFI < 0)
      continue;
    int NewFI = SlotMapping[OldFI];
    if (NewFI == -1 || NewFI == OldFI)
      continue;

    assert(MFI->getStackID(OldFI) == MFI->getStackID(NewFI));
    MO.setIndex(NewFI);
  }

  // The MachineMemOperands have already been updated.
}

/// RemoveDeadStores - Scan through a basic block and look for loads followed
/// by stores.  If they're both using the same stack slot, then the store is
/// definitely dead.  This could obviously be much more aggressive (consider
/// pairs with instructions between them), but such extensions might have a
/// considerable compile time impact.
bool StackSlotColoring::RemoveDeadStores(MachineBasicBlock* MBB) {
  // FIXME: This could be much more aggressive, but we need to investigate
  // the compile time impact of doing so.
  bool changed = false;

  SmallVector<MachineInstr*, 4> toErase;

  for (MachineBasicBlock::iterator I = MBB->begin(), E = MBB->end();
       I != E; ++I) {
    if (DCELimit != -1 && (int)NumDead >= DCELimit)
      break;
    int FirstSS, SecondSS;
    if (TII->isStackSlotCopy(*I, FirstSS, SecondSS) && FirstSS == SecondSS &&
        FirstSS != -1) {
      ++NumDead;
      changed = true;
      toErase.push_back(&*I);
      continue;
    }

    MachineBasicBlock::iterator NextMI = std::next(I);
    MachineBasicBlock::iterator ProbableLoadMI = I;

    unsigned LoadReg = 0;
    unsigned StoreReg = 0;
    unsigned LoadSize = 0;
    unsigned StoreSize = 0;
    if (!(LoadReg = TII->isLoadFromStackSlot(*I, FirstSS, LoadSize)))
      continue;
    // Skip the ...pseudo debugging... instructions between a load and store.
    while ((NextMI != E) && NextMI->isDebugInstr()) {
      ++NextMI;
      ++I;
    }
    if (NextMI == E) continue;
    if (!(StoreReg = TII->isStoreToStackSlot(*NextMI, SecondSS, StoreSize)))
      continue;
    if (FirstSS != SecondSS || LoadReg != StoreReg || FirstSS == -1 ||
        LoadSize != StoreSize)
      continue;

    ++NumDead;
    changed = true;

    if (NextMI->findRegisterUseOperandIdx(LoadReg, true, nullptr) != -1) {
      ++NumDead;
      toErase.push_back(&*ProbableLoadMI);
    }

    toErase.push_back(&*NextMI);
    ++I;
  }

  for (SmallVectorImpl<MachineInstr *>::iterator I = toErase.begin(),
       E = toErase.end(); I != E; ++I)
    (*I)->eraseFromParent();

  return changed;
}

bool StackSlotColoring::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG({
    dbgs() << "********** Stack Slot Coloring **********\n"
           << "********** Function: " << MF.getName() << '\n';
  });

  if (skipFunction(MF.getFunction()))
    return false;

  MFI = &MF.getFrameInfo();
  TII = MF.getSubtarget().getInstrInfo();
  LS = &getAnalysis<LiveStacks>();
  MBFI = &getAnalysis<MachineBlockFrequencyInfo>();

  bool Changed = false;

  unsigned NumSlots = LS->getNumIntervals();
  if (NumSlots == 0)
    // Nothing to do!
    return false;

  // If there are calls to setjmp or sigsetjmp, don't perform stack slot
  // coloring. The stack could be modified before the longjmp is executed,
  // resulting in the wrong value being used afterwards. (See
  // <rdar://problem/8007500>.)
  if (MF.exposesReturnsTwice())
    return false;

  // Gather spill slot references
  ScanForSpillSlotRefs(MF);
  InitializeSlots();
  Changed = ColorSlots(MF);

  for (int &Next : NextColors)
    Next = -1;

  SSIntervals.clear();
  for (unsigned i = 0, e = SSRefs.size(); i != e; ++i)
    SSRefs[i].clear();
  SSRefs.clear();
  OrigAlignments.clear();
  OrigSizes.clear();
  AllColors.clear();
  UsedColors.clear();
  for (unsigned i = 0, e = Assignments.size(); i != e; ++i)
    Assignments[i].clear();
  Assignments.clear();

  return Changed;
}
