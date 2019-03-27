//===- InlineSpiller.cpp - Insert spills and restores inline --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// The inline spiller modifies the machine function directly instead of
// inserting spills and restores in VirtRegMap.
//
//===----------------------------------------------------------------------===//

#include "LiveRangeCalc.h"
#include "Spiller.h"
#include "SplitKit.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/LiveRangeEdit.h"
#include "llvm/CodeGen/LiveStacks.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/VirtRegMap.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/BlockFrequency.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <iterator>
#include <tuple>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "regalloc"

STATISTIC(NumSpilledRanges,   "Number of spilled live ranges");
STATISTIC(NumSnippets,        "Number of spilled snippets");
STATISTIC(NumSpills,          "Number of spills inserted");
STATISTIC(NumSpillsRemoved,   "Number of spills removed");
STATISTIC(NumReloads,         "Number of reloads inserted");
STATISTIC(NumReloadsRemoved,  "Number of reloads removed");
STATISTIC(NumFolded,          "Number of folded stack accesses");
STATISTIC(NumFoldedLoads,     "Number of folded loads");
STATISTIC(NumRemats,          "Number of rematerialized defs for spilling");

static cl::opt<bool> DisableHoisting("disable-spill-hoist", cl::Hidden,
                                     cl::desc("Disable inline spill hoisting"));

namespace {

class HoistSpillHelper : private LiveRangeEdit::Delegate {
  MachineFunction &MF;
  LiveIntervals &LIS;
  LiveStacks &LSS;
  AliasAnalysis *AA;
  MachineDominatorTree &MDT;
  MachineLoopInfo &Loops;
  VirtRegMap &VRM;
  MachineRegisterInfo &MRI;
  const TargetInstrInfo &TII;
  const TargetRegisterInfo &TRI;
  const MachineBlockFrequencyInfo &MBFI;

  InsertPointAnalysis IPA;

  // Map from StackSlot to the LiveInterval of the original register.
  // Note the LiveInterval of the original register may have been deleted
  // after it is spilled. We keep a copy here to track the range where
  // spills can be moved.
  DenseMap<int, std::unique_ptr<LiveInterval>> StackSlotToOrigLI;

  // Map from pair of (StackSlot and Original VNI) to a set of spills which
  // have the same stackslot and have equal values defined by Original VNI.
  // These spills are mergeable and are hoist candiates.
  using MergeableSpillsMap =
      MapVector<std::pair<int, VNInfo *>, SmallPtrSet<MachineInstr *, 16>>;
  MergeableSpillsMap MergeableSpills;

  /// This is the map from original register to a set containing all its
  /// siblings. To hoist a spill to another BB, we need to find out a live
  /// sibling there and use it as the source of the new spill.
  DenseMap<unsigned, SmallSetVector<unsigned, 16>> Virt2SiblingsMap;

  bool isSpillCandBB(LiveInterval &OrigLI, VNInfo &OrigVNI,
                     MachineBasicBlock &BB, unsigned &LiveReg);

  void rmRedundantSpills(
      SmallPtrSet<MachineInstr *, 16> &Spills,
      SmallVectorImpl<MachineInstr *> &SpillsToRm,
      DenseMap<MachineDomTreeNode *, MachineInstr *> &SpillBBToSpill);

  void getVisitOrders(
      MachineBasicBlock *Root, SmallPtrSet<MachineInstr *, 16> &Spills,
      SmallVectorImpl<MachineDomTreeNode *> &Orders,
      SmallVectorImpl<MachineInstr *> &SpillsToRm,
      DenseMap<MachineDomTreeNode *, unsigned> &SpillsToKeep,
      DenseMap<MachineDomTreeNode *, MachineInstr *> &SpillBBToSpill);

  void runHoistSpills(LiveInterval &OrigLI, VNInfo &OrigVNI,
                      SmallPtrSet<MachineInstr *, 16> &Spills,
                      SmallVectorImpl<MachineInstr *> &SpillsToRm,
                      DenseMap<MachineBasicBlock *, unsigned> &SpillsToIns);

public:
  HoistSpillHelper(MachineFunctionPass &pass, MachineFunction &mf,
                   VirtRegMap &vrm)
      : MF(mf), LIS(pass.getAnalysis<LiveIntervals>()),
        LSS(pass.getAnalysis<LiveStacks>()),
        AA(&pass.getAnalysis<AAResultsWrapperPass>().getAAResults()),
        MDT(pass.getAnalysis<MachineDominatorTree>()),
        Loops(pass.getAnalysis<MachineLoopInfo>()), VRM(vrm),
        MRI(mf.getRegInfo()), TII(*mf.getSubtarget().getInstrInfo()),
        TRI(*mf.getSubtarget().getRegisterInfo()),
        MBFI(pass.getAnalysis<MachineBlockFrequencyInfo>()),
        IPA(LIS, mf.getNumBlockIDs()) {}

  void addToMergeableSpills(MachineInstr &Spill, int StackSlot,
                            unsigned Original);
  bool rmFromMergeableSpills(MachineInstr &Spill, int StackSlot);
  void hoistAllSpills();
  void LRE_DidCloneVirtReg(unsigned, unsigned) override;
};

class InlineSpiller : public Spiller {
  MachineFunction &MF;
  LiveIntervals &LIS;
  LiveStacks &LSS;
  AliasAnalysis *AA;
  MachineDominatorTree &MDT;
  MachineLoopInfo &Loops;
  VirtRegMap &VRM;
  MachineRegisterInfo &MRI;
  const TargetInstrInfo &TII;
  const TargetRegisterInfo &TRI;
  const MachineBlockFrequencyInfo &MBFI;

  // Variables that are valid during spill(), but used by multiple methods.
  LiveRangeEdit *Edit;
  LiveInterval *StackInt;
  int StackSlot;
  unsigned Original;

  // All registers to spill to StackSlot, including the main register.
  SmallVector<unsigned, 8> RegsToSpill;

  // All COPY instructions to/from snippets.
  // They are ignored since both operands refer to the same stack slot.
  SmallPtrSet<MachineInstr*, 8> SnippetCopies;

  // Values that failed to remat at some point.
  SmallPtrSet<VNInfo*, 8> UsedValues;

  // Dead defs generated during spilling.
  SmallVector<MachineInstr*, 8> DeadDefs;

  // Object records spills information and does the hoisting.
  HoistSpillHelper HSpiller;

  ~InlineSpiller() override = default;

public:
  InlineSpiller(MachineFunctionPass &pass, MachineFunction &mf, VirtRegMap &vrm)
      : MF(mf), LIS(pass.getAnalysis<LiveIntervals>()),
        LSS(pass.getAnalysis<LiveStacks>()),
        AA(&pass.getAnalysis<AAResultsWrapperPass>().getAAResults()),
        MDT(pass.getAnalysis<MachineDominatorTree>()),
        Loops(pass.getAnalysis<MachineLoopInfo>()), VRM(vrm),
        MRI(mf.getRegInfo()), TII(*mf.getSubtarget().getInstrInfo()),
        TRI(*mf.getSubtarget().getRegisterInfo()),
        MBFI(pass.getAnalysis<MachineBlockFrequencyInfo>()),
        HSpiller(pass, mf, vrm) {}

  void spill(LiveRangeEdit &) override;
  void postOptimization() override;

private:
  bool isSnippet(const LiveInterval &SnipLI);
  void collectRegsToSpill();

  bool isRegToSpill(unsigned Reg) { return is_contained(RegsToSpill, Reg); }

  bool isSibling(unsigned Reg);
  bool hoistSpillInsideBB(LiveInterval &SpillLI, MachineInstr &CopyMI);
  void eliminateRedundantSpills(LiveInterval &LI, VNInfo *VNI);

  void markValueUsed(LiveInterval*, VNInfo*);
  bool reMaterializeFor(LiveInterval &, MachineInstr &MI);
  void reMaterializeAll();

  bool coalesceStackAccess(MachineInstr *MI, unsigned Reg);
  bool foldMemoryOperand(ArrayRef<std::pair<MachineInstr *, unsigned>>,
                         MachineInstr *LoadMI = nullptr);
  void insertReload(unsigned VReg, SlotIndex, MachineBasicBlock::iterator MI);
  void insertSpill(unsigned VReg, bool isKill, MachineBasicBlock::iterator MI);

  void spillAroundUses(unsigned Reg);
  void spillAll();
};

} // end anonymous namespace

Spiller::~Spiller() = default;

void Spiller::anchor() {}

Spiller *llvm::createInlineSpiller(MachineFunctionPass &pass,
                                   MachineFunction &mf,
                                   VirtRegMap &vrm) {
  return new InlineSpiller(pass, mf, vrm);
}

//===----------------------------------------------------------------------===//
//                                Snippets
//===----------------------------------------------------------------------===//

// When spilling a virtual register, we also spill any snippets it is connected
// to. The snippets are small live ranges that only have a single real use,
// leftovers from live range splitting. Spilling them enables memory operand
// folding or tightens the live range around the single use.
//
// This minimizes register pressure and maximizes the store-to-load distance for
// spill slots which can be important in tight loops.

/// isFullCopyOf - If MI is a COPY to or from Reg, return the other register,
/// otherwise return 0.
static unsigned isFullCopyOf(const MachineInstr &MI, unsigned Reg) {
  if (!MI.isFullCopy())
    return 0;
  if (MI.getOperand(0).getReg() == Reg)
    return MI.getOperand(1).getReg();
  if (MI.getOperand(1).getReg() == Reg)
    return MI.getOperand(0).getReg();
  return 0;
}

/// isSnippet - Identify if a live interval is a snippet that should be spilled.
/// It is assumed that SnipLI is a virtual register with the same original as
/// Edit->getReg().
bool InlineSpiller::isSnippet(const LiveInterval &SnipLI) {
  unsigned Reg = Edit->getReg();

  // A snippet is a tiny live range with only a single instruction using it
  // besides copies to/from Reg or spills/fills. We accept:
  //
  //   %snip = COPY %Reg / FILL fi#
  //   %snip = USE %snip
  //   %Reg = COPY %snip / SPILL %snip, fi#
  //
  if (SnipLI.getNumValNums() > 2 || !LIS.intervalIsInOneMBB(SnipLI))
    return false;

  MachineInstr *UseMI = nullptr;

  // Check that all uses satisfy our criteria.
  for (MachineRegisterInfo::reg_instr_nodbg_iterator
       RI = MRI.reg_instr_nodbg_begin(SnipLI.reg),
       E = MRI.reg_instr_nodbg_end(); RI != E; ) {
    MachineInstr &MI = *RI++;

    // Allow copies to/from Reg.
    if (isFullCopyOf(MI, Reg))
      continue;

    // Allow stack slot loads.
    int FI;
    if (SnipLI.reg == TII.isLoadFromStackSlot(MI, FI) && FI == StackSlot)
      continue;

    // Allow stack slot stores.
    if (SnipLI.reg == TII.isStoreToStackSlot(MI, FI) && FI == StackSlot)
      continue;

    // Allow a single additional instruction.
    if (UseMI && &MI != UseMI)
      return false;
    UseMI = &MI;
  }
  return true;
}

/// collectRegsToSpill - Collect live range snippets that only have a single
/// real use.
void InlineSpiller::collectRegsToSpill() {
  unsigned Reg = Edit->getReg();

  // Main register always spills.
  RegsToSpill.assign(1, Reg);
  SnippetCopies.clear();

  // Snippets all have the same original, so there can't be any for an original
  // register.
  if (Original == Reg)
    return;

  for (MachineRegisterInfo::reg_instr_iterator
       RI = MRI.reg_instr_begin(Reg), E = MRI.reg_instr_end(); RI != E; ) {
    MachineInstr &MI = *RI++;
    unsigned SnipReg = isFullCopyOf(MI, Reg);
    if (!isSibling(SnipReg))
      continue;
    LiveInterval &SnipLI = LIS.getInterval(SnipReg);
    if (!isSnippet(SnipLI))
      continue;
    SnippetCopies.insert(&MI);
    if (isRegToSpill(SnipReg))
      continue;
    RegsToSpill.push_back(SnipReg);
    LLVM_DEBUG(dbgs() << "\talso spill snippet " << SnipLI << '\n');
    ++NumSnippets;
  }
}

bool InlineSpiller::isSibling(unsigned Reg) {
  return TargetRegisterInfo::isVirtualRegister(Reg) &&
           VRM.getOriginal(Reg) == Original;
}

/// It is beneficial to spill to earlier place in the same BB in case
/// as follows:
/// There is an alternative def earlier in the same MBB.
/// Hoist the spill as far as possible in SpillMBB. This can ease
/// register pressure:
///
///   x = def
///   y = use x
///   s = copy x
///
/// Hoisting the spill of s to immediately after the def removes the
/// interference between x and y:
///
///   x = def
///   spill x
///   y = use killed x
///
/// This hoist only helps when the copy kills its source.
///
bool InlineSpiller::hoistSpillInsideBB(LiveInterval &SpillLI,
                                       MachineInstr &CopyMI) {
  SlotIndex Idx = LIS.getInstructionIndex(CopyMI);
#ifndef NDEBUG
  VNInfo *VNI = SpillLI.getVNInfoAt(Idx.getRegSlot());
  assert(VNI && VNI->def == Idx.getRegSlot() && "Not defined by copy");
#endif

  unsigned SrcReg = CopyMI.getOperand(1).getReg();
  LiveInterval &SrcLI = LIS.getInterval(SrcReg);
  VNInfo *SrcVNI = SrcLI.getVNInfoAt(Idx);
  LiveQueryResult SrcQ = SrcLI.Query(Idx);
  MachineBasicBlock *DefMBB = LIS.getMBBFromIndex(SrcVNI->def);
  if (DefMBB != CopyMI.getParent() || !SrcQ.isKill())
    return false;

  // Conservatively extend the stack slot range to the range of the original
  // value. We may be able to do better with stack slot coloring by being more
  // careful here.
  assert(StackInt && "No stack slot assigned yet.");
  LiveInterval &OrigLI = LIS.getInterval(Original);
  VNInfo *OrigVNI = OrigLI.getVNInfoAt(Idx);
  StackInt->MergeValueInAsValue(OrigLI, OrigVNI, StackInt->getValNumInfo(0));
  LLVM_DEBUG(dbgs() << "\tmerged orig valno " << OrigVNI->id << ": "
                    << *StackInt << '\n');

  // We are going to spill SrcVNI immediately after its def, so clear out
  // any later spills of the same value.
  eliminateRedundantSpills(SrcLI, SrcVNI);

  MachineBasicBlock *MBB = LIS.getMBBFromIndex(SrcVNI->def);
  MachineBasicBlock::iterator MII;
  if (SrcVNI->isPHIDef())
    MII = MBB->SkipPHIsLabelsAndDebug(MBB->begin());
  else {
    MachineInstr *DefMI = LIS.getInstructionFromIndex(SrcVNI->def);
    assert(DefMI && "Defining instruction disappeared");
    MII = DefMI;
    ++MII;
  }
  // Insert spill without kill flag immediately after def.
  TII.storeRegToStackSlot(*MBB, MII, SrcReg, false, StackSlot,
                          MRI.getRegClass(SrcReg), &TRI);
  --MII; // Point to store instruction.
  LIS.InsertMachineInstrInMaps(*MII);
  LLVM_DEBUG(dbgs() << "\thoisted: " << SrcVNI->def << '\t' << *MII);

  HSpiller.addToMergeableSpills(*MII, StackSlot, Original);
  ++NumSpills;
  return true;
}

/// eliminateRedundantSpills - SLI:VNI is known to be on the stack. Remove any
/// redundant spills of this value in SLI.reg and sibling copies.
void InlineSpiller::eliminateRedundantSpills(LiveInterval &SLI, VNInfo *VNI) {
  assert(VNI && "Missing value");
  SmallVector<std::pair<LiveInterval*, VNInfo*>, 8> WorkList;
  WorkList.push_back(std::make_pair(&SLI, VNI));
  assert(StackInt && "No stack slot assigned yet.");

  do {
    LiveInterval *LI;
    std::tie(LI, VNI) = WorkList.pop_back_val();
    unsigned Reg = LI->reg;
    LLVM_DEBUG(dbgs() << "Checking redundant spills for " << VNI->id << '@'
                      << VNI->def << " in " << *LI << '\n');

    // Regs to spill are taken care of.
    if (isRegToSpill(Reg))
      continue;

    // Add all of VNI's live range to StackInt.
    StackInt->MergeValueInAsValue(*LI, VNI, StackInt->getValNumInfo(0));
    LLVM_DEBUG(dbgs() << "Merged to stack int: " << *StackInt << '\n');

    // Find all spills and copies of VNI.
    for (MachineRegisterInfo::use_instr_nodbg_iterator
         UI = MRI.use_instr_nodbg_begin(Reg), E = MRI.use_instr_nodbg_end();
         UI != E; ) {
      MachineInstr &MI = *UI++;
      if (!MI.isCopy() && !MI.mayStore())
        continue;
      SlotIndex Idx = LIS.getInstructionIndex(MI);
      if (LI->getVNInfoAt(Idx) != VNI)
        continue;

      // Follow sibling copies down the dominator tree.
      if (unsigned DstReg = isFullCopyOf(MI, Reg)) {
        if (isSibling(DstReg)) {
           LiveInterval &DstLI = LIS.getInterval(DstReg);
           VNInfo *DstVNI = DstLI.getVNInfoAt(Idx.getRegSlot());
           assert(DstVNI && "Missing defined value");
           assert(DstVNI->def == Idx.getRegSlot() && "Wrong copy def slot");
           WorkList.push_back(std::make_pair(&DstLI, DstVNI));
        }
        continue;
      }

      // Erase spills.
      int FI;
      if (Reg == TII.isStoreToStackSlot(MI, FI) && FI == StackSlot) {
        LLVM_DEBUG(dbgs() << "Redundant spill " << Idx << '\t' << MI);
        // eliminateDeadDefs won't normally remove stores, so switch opcode.
        MI.setDesc(TII.get(TargetOpcode::KILL));
        DeadDefs.push_back(&MI);
        ++NumSpillsRemoved;
        if (HSpiller.rmFromMergeableSpills(MI, StackSlot))
          --NumSpills;
      }
    }
  } while (!WorkList.empty());
}

//===----------------------------------------------------------------------===//
//                            Rematerialization
//===----------------------------------------------------------------------===//

/// markValueUsed - Remember that VNI failed to rematerialize, so its defining
/// instruction cannot be eliminated. See through snippet copies
void InlineSpiller::markValueUsed(LiveInterval *LI, VNInfo *VNI) {
  SmallVector<std::pair<LiveInterval*, VNInfo*>, 8> WorkList;
  WorkList.push_back(std::make_pair(LI, VNI));
  do {
    std::tie(LI, VNI) = WorkList.pop_back_val();
    if (!UsedValues.insert(VNI).second)
      continue;

    if (VNI->isPHIDef()) {
      MachineBasicBlock *MBB = LIS.getMBBFromIndex(VNI->def);
      for (MachineBasicBlock *P : MBB->predecessors()) {
        VNInfo *PVNI = LI->getVNInfoBefore(LIS.getMBBEndIdx(P));
        if (PVNI)
          WorkList.push_back(std::make_pair(LI, PVNI));
      }
      continue;
    }

    // Follow snippet copies.
    MachineInstr *MI = LIS.getInstructionFromIndex(VNI->def);
    if (!SnippetCopies.count(MI))
      continue;
    LiveInterval &SnipLI = LIS.getInterval(MI->getOperand(1).getReg());
    assert(isRegToSpill(SnipLI.reg) && "Unexpected register in copy");
    VNInfo *SnipVNI = SnipLI.getVNInfoAt(VNI->def.getRegSlot(true));
    assert(SnipVNI && "Snippet undefined before copy");
    WorkList.push_back(std::make_pair(&SnipLI, SnipVNI));
  } while (!WorkList.empty());
}

/// reMaterializeFor - Attempt to rematerialize before MI instead of reloading.
bool InlineSpiller::reMaterializeFor(LiveInterval &VirtReg, MachineInstr &MI) {
  // Analyze instruction
  SmallVector<std::pair<MachineInstr *, unsigned>, 8> Ops;
  MIBundleOperands::VirtRegInfo RI =
      MIBundleOperands(MI).analyzeVirtReg(VirtReg.reg, &Ops);

  if (!RI.Reads)
    return false;

  SlotIndex UseIdx = LIS.getInstructionIndex(MI).getRegSlot(true);
  VNInfo *ParentVNI = VirtReg.getVNInfoAt(UseIdx.getBaseIndex());

  if (!ParentVNI) {
    LLVM_DEBUG(dbgs() << "\tadding <undef> flags: ");
    for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
      MachineOperand &MO = MI.getOperand(i);
      if (MO.isReg() && MO.isUse() && MO.getReg() == VirtReg.reg)
        MO.setIsUndef();
    }
    LLVM_DEBUG(dbgs() << UseIdx << '\t' << MI);
    return true;
  }

  if (SnippetCopies.count(&MI))
    return false;

  LiveInterval &OrigLI = LIS.getInterval(Original);
  VNInfo *OrigVNI = OrigLI.getVNInfoAt(UseIdx);
  LiveRangeEdit::Remat RM(ParentVNI);
  RM.OrigMI = LIS.getInstructionFromIndex(OrigVNI->def);

  if (!Edit->canRematerializeAt(RM, OrigVNI, UseIdx, false)) {
    markValueUsed(&VirtReg, ParentVNI);
    LLVM_DEBUG(dbgs() << "\tcannot remat for " << UseIdx << '\t' << MI);
    return false;
  }

  // If the instruction also writes VirtReg.reg, it had better not require the
  // same register for uses and defs.
  if (RI.Tied) {
    markValueUsed(&VirtReg, ParentVNI);
    LLVM_DEBUG(dbgs() << "\tcannot remat tied reg: " << UseIdx << '\t' << MI);
    return false;
  }

  // Before rematerializing into a register for a single instruction, try to
  // fold a load into the instruction. That avoids allocating a new register.
  if (RM.OrigMI->canFoldAsLoad() &&
      foldMemoryOperand(Ops, RM.OrigMI)) {
    Edit->markRematerialized(RM.ParentVNI);
    ++NumFoldedLoads;
    return true;
  }

  // Allocate a new register for the remat.
  unsigned NewVReg = Edit->createFrom(Original);

  // Finally we can rematerialize OrigMI before MI.
  SlotIndex DefIdx =
      Edit->rematerializeAt(*MI.getParent(), MI, NewVReg, RM, TRI);

  // We take the DebugLoc from MI, since OrigMI may be attributed to a
  // different source location.
  auto *NewMI = LIS.getInstructionFromIndex(DefIdx);
  NewMI->setDebugLoc(MI.getDebugLoc());

  (void)DefIdx;
  LLVM_DEBUG(dbgs() << "\tremat:  " << DefIdx << '\t'
                    << *LIS.getInstructionFromIndex(DefIdx));

  // Replace operands
  for (const auto &OpPair : Ops) {
    MachineOperand &MO = OpPair.first->getOperand(OpPair.second);
    if (MO.isReg() && MO.isUse() && MO.getReg() == VirtReg.reg) {
      MO.setReg(NewVReg);
      MO.setIsKill();
    }
  }
  LLVM_DEBUG(dbgs() << "\t        " << UseIdx << '\t' << MI << '\n');

  ++NumRemats;
  return true;
}

/// reMaterializeAll - Try to rematerialize as many uses as possible,
/// and trim the live ranges after.
void InlineSpiller::reMaterializeAll() {
  if (!Edit->anyRematerializable(AA))
    return;

  UsedValues.clear();

  // Try to remat before all uses of snippets.
  bool anyRemat = false;
  for (unsigned Reg : RegsToSpill) {
    LiveInterval &LI = LIS.getInterval(Reg);
    for (MachineRegisterInfo::reg_bundle_iterator
           RegI = MRI.reg_bundle_begin(Reg), E = MRI.reg_bundle_end();
         RegI != E; ) {
      MachineInstr &MI = *RegI++;

      // Debug values are not allowed to affect codegen.
      if (MI.isDebugValue())
        continue;

      assert(!MI.isDebugInstr() && "Did not expect to find a use in debug "
             "instruction that isn't a DBG_VALUE");

      anyRemat |= reMaterializeFor(LI, MI);
    }
  }
  if (!anyRemat)
    return;

  // Remove any values that were completely rematted.
  for (unsigned Reg : RegsToSpill) {
    LiveInterval &LI = LIS.getInterval(Reg);
    for (LiveInterval::vni_iterator I = LI.vni_begin(), E = LI.vni_end();
         I != E; ++I) {
      VNInfo *VNI = *I;
      if (VNI->isUnused() || VNI->isPHIDef() || UsedValues.count(VNI))
        continue;
      MachineInstr *MI = LIS.getInstructionFromIndex(VNI->def);
      MI->addRegisterDead(Reg, &TRI);
      if (!MI->allDefsAreDead())
        continue;
      LLVM_DEBUG(dbgs() << "All defs dead: " << *MI);
      DeadDefs.push_back(MI);
    }
  }

  // Eliminate dead code after remat. Note that some snippet copies may be
  // deleted here.
  if (DeadDefs.empty())
    return;
  LLVM_DEBUG(dbgs() << "Remat created " << DeadDefs.size() << " dead defs.\n");
  Edit->eliminateDeadDefs(DeadDefs, RegsToSpill, AA);

  // LiveRangeEdit::eliminateDeadDef is used to remove dead define instructions
  // after rematerialization.  To remove a VNI for a vreg from its LiveInterval,
  // LiveIntervals::removeVRegDefAt is used. However, after non-PHI VNIs are all
  // removed, PHI VNI are still left in the LiveInterval.
  // So to get rid of unused reg, we need to check whether it has non-dbg
  // reference instead of whether it has non-empty interval.
  unsigned ResultPos = 0;
  for (unsigned Reg : RegsToSpill) {
    if (MRI.reg_nodbg_empty(Reg)) {
      Edit->eraseVirtReg(Reg);
      continue;
    }

    assert(LIS.hasInterval(Reg) &&
           (!LIS.getInterval(Reg).empty() || !MRI.reg_nodbg_empty(Reg)) &&
           "Empty and not used live-range?!");

    RegsToSpill[ResultPos++] = Reg;
  }
  RegsToSpill.erase(RegsToSpill.begin() + ResultPos, RegsToSpill.end());
  LLVM_DEBUG(dbgs() << RegsToSpill.size()
                    << " registers to spill after remat.\n");
}

//===----------------------------------------------------------------------===//
//                                 Spilling
//===----------------------------------------------------------------------===//

/// If MI is a load or store of StackSlot, it can be removed.
bool InlineSpiller::coalesceStackAccess(MachineInstr *MI, unsigned Reg) {
  int FI = 0;
  unsigned InstrReg = TII.isLoadFromStackSlot(*MI, FI);
  bool IsLoad = InstrReg;
  if (!IsLoad)
    InstrReg = TII.isStoreToStackSlot(*MI, FI);

  // We have a stack access. Is it the right register and slot?
  if (InstrReg != Reg || FI != StackSlot)
    return false;

  if (!IsLoad)
    HSpiller.rmFromMergeableSpills(*MI, StackSlot);

  LLVM_DEBUG(dbgs() << "Coalescing stack access: " << *MI);
  LIS.RemoveMachineInstrFromMaps(*MI);
  MI->eraseFromParent();

  if (IsLoad) {
    ++NumReloadsRemoved;
    --NumReloads;
  } else {
    ++NumSpillsRemoved;
    --NumSpills;
  }

  return true;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD
// Dump the range of instructions from B to E with their slot indexes.
static void dumpMachineInstrRangeWithSlotIndex(MachineBasicBlock::iterator B,
                                               MachineBasicBlock::iterator E,
                                               LiveIntervals const &LIS,
                                               const char *const header,
                                               unsigned VReg =0) {
  char NextLine = '\n';
  char SlotIndent = '\t';

  if (std::next(B) == E) {
    NextLine = ' ';
    SlotIndent = ' ';
  }

  dbgs() << '\t' << header << ": " << NextLine;

  for (MachineBasicBlock::iterator I = B; I != E; ++I) {
    SlotIndex Idx = LIS.getInstructionIndex(*I).getRegSlot();

    // If a register was passed in and this instruction has it as a
    // destination that is marked as an early clobber, print the
    // early-clobber slot index.
    if (VReg) {
      MachineOperand *MO = I->findRegisterDefOperand(VReg);
      if (MO && MO->isEarlyClobber())
        Idx = Idx.getRegSlot(true);
    }

    dbgs() << SlotIndent << Idx << '\t' << *I;
  }
}
#endif

/// foldMemoryOperand - Try folding stack slot references in Ops into their
/// instructions.
///
/// @param Ops    Operand indices from analyzeVirtReg().
/// @param LoadMI Load instruction to use instead of stack slot when non-null.
/// @return       True on success.
bool InlineSpiller::
foldMemoryOperand(ArrayRef<std::pair<MachineInstr *, unsigned>> Ops,
                  MachineInstr *LoadMI) {
  if (Ops.empty())
    return false;
  // Don't attempt folding in bundles.
  MachineInstr *MI = Ops.front().first;
  if (Ops.back().first != MI || MI->isBundled())
    return false;

  bool WasCopy = MI->isCopy();
  unsigned ImpReg = 0;

  // Spill subregs if the target allows it.
  // We always want to spill subregs for stackmap/patchpoint pseudos.
  bool SpillSubRegs = TII.isSubregFoldable() ||
                      MI->getOpcode() == TargetOpcode::STATEPOINT ||
                      MI->getOpcode() == TargetOpcode::PATCHPOINT ||
                      MI->getOpcode() == TargetOpcode::STACKMAP;

  // TargetInstrInfo::foldMemoryOperand only expects explicit, non-tied
  // operands.
  SmallVector<unsigned, 8> FoldOps;
  for (const auto &OpPair : Ops) {
    unsigned Idx = OpPair.second;
    assert(MI == OpPair.first && "Instruction conflict during operand folding");
    MachineOperand &MO = MI->getOperand(Idx);
    if (MO.isImplicit()) {
      ImpReg = MO.getReg();
      continue;
    }

    if (!SpillSubRegs && MO.getSubReg())
      return false;
    // We cannot fold a load instruction into a def.
    if (LoadMI && MO.isDef())
      return false;
    // Tied use operands should not be passed to foldMemoryOperand.
    if (!MI->isRegTiedToDefOperand(Idx))
      FoldOps.push_back(Idx);
  }

  // If we only have implicit uses, we won't be able to fold that.
  // Moreover, TargetInstrInfo::foldMemoryOperand will assert if we try!
  if (FoldOps.empty())
    return false;

  MachineInstrSpan MIS(MI);

  MachineInstr *FoldMI =
      LoadMI ? TII.foldMemoryOperand(*MI, FoldOps, *LoadMI, &LIS)
             : TII.foldMemoryOperand(*MI, FoldOps, StackSlot, &LIS);
  if (!FoldMI)
    return false;

  // Remove LIS for any dead defs in the original MI not in FoldMI.
  for (MIBundleOperands MO(*MI); MO.isValid(); ++MO) {
    if (!MO->isReg())
      continue;
    unsigned Reg = MO->getReg();
    if (!Reg || TargetRegisterInfo::isVirtualRegister(Reg) ||
        MRI.isReserved(Reg)) {
      continue;
    }
    // Skip non-Defs, including undef uses and internal reads.
    if (MO->isUse())
      continue;
    MIBundleOperands::PhysRegInfo RI =
        MIBundleOperands(*FoldMI).analyzePhysReg(Reg, &TRI);
    if (RI.FullyDefined)
      continue;
    // FoldMI does not define this physreg. Remove the LI segment.
    assert(MO->isDead() && "Cannot fold physreg def");
    SlotIndex Idx = LIS.getInstructionIndex(*MI).getRegSlot();
    LIS.removePhysRegDefAt(Reg, Idx);
  }

  int FI;
  if (TII.isStoreToStackSlot(*MI, FI) &&
      HSpiller.rmFromMergeableSpills(*MI, FI))
    --NumSpills;
  LIS.ReplaceMachineInstrInMaps(*MI, *FoldMI);
  MI->eraseFromParent();

  // Insert any new instructions other than FoldMI into the LIS maps.
  assert(!MIS.empty() && "Unexpected empty span of instructions!");
  for (MachineInstr &MI : MIS)
    if (&MI != FoldMI)
      LIS.InsertMachineInstrInMaps(MI);

  // TII.foldMemoryOperand may have left some implicit operands on the
  // instruction.  Strip them.
  if (ImpReg)
    for (unsigned i = FoldMI->getNumOperands(); i; --i) {
      MachineOperand &MO = FoldMI->getOperand(i - 1);
      if (!MO.isReg() || !MO.isImplicit())
        break;
      if (MO.getReg() == ImpReg)
        FoldMI->RemoveOperand(i - 1);
    }

  LLVM_DEBUG(dumpMachineInstrRangeWithSlotIndex(MIS.begin(), MIS.end(), LIS,
                                                "folded"));

  if (!WasCopy)
    ++NumFolded;
  else if (Ops.front().second == 0) {
    ++NumSpills;
    HSpiller.addToMergeableSpills(*FoldMI, StackSlot, Original);
  } else
    ++NumReloads;
  return true;
}

void InlineSpiller::insertReload(unsigned NewVReg,
                                 SlotIndex Idx,
                                 MachineBasicBlock::iterator MI) {
  MachineBasicBlock &MBB = *MI->getParent();

  MachineInstrSpan MIS(MI);
  TII.loadRegFromStackSlot(MBB, MI, NewVReg, StackSlot,
                           MRI.getRegClass(NewVReg), &TRI);

  LIS.InsertMachineInstrRangeInMaps(MIS.begin(), MI);

  LLVM_DEBUG(dumpMachineInstrRangeWithSlotIndex(MIS.begin(), MI, LIS, "reload",
                                                NewVReg));
  ++NumReloads;
}

/// Check if \p Def fully defines a VReg with an undefined value.
/// If that's the case, that means the value of VReg is actually
/// not relevant.
static bool isFullUndefDef(const MachineInstr &Def) {
  if (!Def.isImplicitDef())
    return false;
  assert(Def.getNumOperands() == 1 &&
         "Implicit def with more than one definition");
  // We can say that the VReg defined by Def is undef, only if it is
  // fully defined by Def. Otherwise, some of the lanes may not be
  // undef and the value of the VReg matters.
  return !Def.getOperand(0).getSubReg();
}

/// insertSpill - Insert a spill of NewVReg after MI.
void InlineSpiller::insertSpill(unsigned NewVReg, bool isKill,
                                 MachineBasicBlock::iterator MI) {
  MachineBasicBlock &MBB = *MI->getParent();

  MachineInstrSpan MIS(MI);
  bool IsRealSpill = true;
  if (isFullUndefDef(*MI)) {
    // Don't spill undef value.
    // Anything works for undef, in particular keeping the memory
    // uninitialized is a viable option and it saves code size and
    // run time.
    BuildMI(MBB, std::next(MI), MI->getDebugLoc(), TII.get(TargetOpcode::KILL))
        .addReg(NewVReg, getKillRegState(isKill));
    IsRealSpill = false;
  } else
    TII.storeRegToStackSlot(MBB, std::next(MI), NewVReg, isKill, StackSlot,
                            MRI.getRegClass(NewVReg), &TRI);

  LIS.InsertMachineInstrRangeInMaps(std::next(MI), MIS.end());

  LLVM_DEBUG(dumpMachineInstrRangeWithSlotIndex(std::next(MI), MIS.end(), LIS,
                                                "spill"));
  ++NumSpills;
  if (IsRealSpill)
    HSpiller.addToMergeableSpills(*std::next(MI), StackSlot, Original);
}

/// spillAroundUses - insert spill code around each use of Reg.
void InlineSpiller::spillAroundUses(unsigned Reg) {
  LLVM_DEBUG(dbgs() << "spillAroundUses " << printReg(Reg) << '\n');
  LiveInterval &OldLI = LIS.getInterval(Reg);

  // Iterate over instructions using Reg.
  for (MachineRegisterInfo::reg_bundle_iterator
       RegI = MRI.reg_bundle_begin(Reg), E = MRI.reg_bundle_end();
       RegI != E; ) {
    MachineInstr *MI = &*(RegI++);

    // Debug values are not allowed to affect codegen.
    if (MI->isDebugValue()) {
      // Modify DBG_VALUE now that the value is in a spill slot.
      MachineBasicBlock *MBB = MI->getParent();
      LLVM_DEBUG(dbgs() << "Modifying debug info due to spill:\t" << *MI);
      buildDbgValueForSpill(*MBB, MI, *MI, StackSlot);
      MBB->erase(MI);
      continue;
    }

    assert(!MI->isDebugInstr() && "Did not expect to find a use in debug "
           "instruction that isn't a DBG_VALUE");

    // Ignore copies to/from snippets. We'll delete them.
    if (SnippetCopies.count(MI))
      continue;

    // Stack slot accesses may coalesce away.
    if (coalesceStackAccess(MI, Reg))
      continue;

    // Analyze instruction.
    SmallVector<std::pair<MachineInstr*, unsigned>, 8> Ops;
    MIBundleOperands::VirtRegInfo RI =
        MIBundleOperands(*MI).analyzeVirtReg(Reg, &Ops);

    // Find the slot index where this instruction reads and writes OldLI.
    // This is usually the def slot, except for tied early clobbers.
    SlotIndex Idx = LIS.getInstructionIndex(*MI).getRegSlot();
    if (VNInfo *VNI = OldLI.getVNInfoAt(Idx.getRegSlot(true)))
      if (SlotIndex::isSameInstr(Idx, VNI->def))
        Idx = VNI->def;

    // Check for a sibling copy.
    unsigned SibReg = isFullCopyOf(*MI, Reg);
    if (SibReg && isSibling(SibReg)) {
      // This may actually be a copy between snippets.
      if (isRegToSpill(SibReg)) {
        LLVM_DEBUG(dbgs() << "Found new snippet copy: " << *MI);
        SnippetCopies.insert(MI);
        continue;
      }
      if (RI.Writes) {
        if (hoistSpillInsideBB(OldLI, *MI)) {
          // This COPY is now dead, the value is already in the stack slot.
          MI->getOperand(0).setIsDead();
          DeadDefs.push_back(MI);
          continue;
        }
      } else {
        // This is a reload for a sib-reg copy. Drop spills downstream.
        LiveInterval &SibLI = LIS.getInterval(SibReg);
        eliminateRedundantSpills(SibLI, SibLI.getVNInfoAt(Idx));
        // The COPY will fold to a reload below.
      }
    }

    // Attempt to fold memory ops.
    if (foldMemoryOperand(Ops))
      continue;

    // Create a new virtual register for spill/fill.
    // FIXME: Infer regclass from instruction alone.
    unsigned NewVReg = Edit->createFrom(Reg);

    if (RI.Reads)
      insertReload(NewVReg, Idx, MI);

    // Rewrite instruction operands.
    bool hasLiveDef = false;
    for (const auto &OpPair : Ops) {
      MachineOperand &MO = OpPair.first->getOperand(OpPair.second);
      MO.setReg(NewVReg);
      if (MO.isUse()) {
        if (!OpPair.first->isRegTiedToDefOperand(OpPair.second))
          MO.setIsKill();
      } else {
        if (!MO.isDead())
          hasLiveDef = true;
      }
    }
    LLVM_DEBUG(dbgs() << "\trewrite: " << Idx << '\t' << *MI << '\n');

    // FIXME: Use a second vreg if instruction has no tied ops.
    if (RI.Writes)
      if (hasLiveDef)
        insertSpill(NewVReg, true, MI);
  }
}

/// spillAll - Spill all registers remaining after rematerialization.
void InlineSpiller::spillAll() {
  // Update LiveStacks now that we are committed to spilling.
  if (StackSlot == VirtRegMap::NO_STACK_SLOT) {
    StackSlot = VRM.assignVirt2StackSlot(Original);
    StackInt = &LSS.getOrCreateInterval(StackSlot, MRI.getRegClass(Original));
    StackInt->getNextValue(SlotIndex(), LSS.getVNInfoAllocator());
  } else
    StackInt = &LSS.getInterval(StackSlot);

  if (Original != Edit->getReg())
    VRM.assignVirt2StackSlot(Edit->getReg(), StackSlot);

  assert(StackInt->getNumValNums() == 1 && "Bad stack interval values");
  for (unsigned Reg : RegsToSpill)
    StackInt->MergeSegmentsInAsValue(LIS.getInterval(Reg),
                                     StackInt->getValNumInfo(0));
  LLVM_DEBUG(dbgs() << "Merged spilled regs: " << *StackInt << '\n');

  // Spill around uses of all RegsToSpill.
  for (unsigned Reg : RegsToSpill)
    spillAroundUses(Reg);

  // Hoisted spills may cause dead code.
  if (!DeadDefs.empty()) {
    LLVM_DEBUG(dbgs() << "Eliminating " << DeadDefs.size() << " dead defs\n");
    Edit->eliminateDeadDefs(DeadDefs, RegsToSpill, AA);
  }

  // Finally delete the SnippetCopies.
  for (unsigned Reg : RegsToSpill) {
    for (MachineRegisterInfo::reg_instr_iterator
         RI = MRI.reg_instr_begin(Reg), E = MRI.reg_instr_end();
         RI != E; ) {
      MachineInstr &MI = *(RI++);
      assert(SnippetCopies.count(&MI) && "Remaining use wasn't a snippet copy");
      // FIXME: Do this with a LiveRangeEdit callback.
      LIS.RemoveMachineInstrFromMaps(MI);
      MI.eraseFromParent();
    }
  }

  // Delete all spilled registers.
  for (unsigned Reg : RegsToSpill)
    Edit->eraseVirtReg(Reg);
}

void InlineSpiller::spill(LiveRangeEdit &edit) {
  ++NumSpilledRanges;
  Edit = &edit;
  assert(!TargetRegisterInfo::isStackSlot(edit.getReg())
         && "Trying to spill a stack slot.");
  // Share a stack slot among all descendants of Original.
  Original = VRM.getOriginal(edit.getReg());
  StackSlot = VRM.getStackSlot(Original);
  StackInt = nullptr;

  LLVM_DEBUG(dbgs() << "Inline spilling "
                    << TRI.getRegClassName(MRI.getRegClass(edit.getReg()))
                    << ':' << edit.getParent() << "\nFrom original "
                    << printReg(Original) << '\n');
  assert(edit.getParent().isSpillable() &&
         "Attempting to spill already spilled value.");
  assert(DeadDefs.empty() && "Previous spill didn't remove dead defs");

  collectRegsToSpill();
  reMaterializeAll();

  // Remat may handle everything.
  if (!RegsToSpill.empty())
    spillAll();

  Edit->calculateRegClassAndHint(MF, Loops, MBFI);
}

/// Optimizations after all the reg selections and spills are done.
void InlineSpiller::postOptimization() { HSpiller.hoistAllSpills(); }

/// When a spill is inserted, add the spill to MergeableSpills map.
void HoistSpillHelper::addToMergeableSpills(MachineInstr &Spill, int StackSlot,
                                            unsigned Original) {
  BumpPtrAllocator &Allocator = LIS.getVNInfoAllocator();
  LiveInterval &OrigLI = LIS.getInterval(Original);
  // save a copy of LiveInterval in StackSlotToOrigLI because the original
  // LiveInterval may be cleared after all its references are spilled.
  if (StackSlotToOrigLI.find(StackSlot) == StackSlotToOrigLI.end()) {
    auto LI = llvm::make_unique<LiveInterval>(OrigLI.reg, OrigLI.weight);
    LI->assign(OrigLI, Allocator);
    StackSlotToOrigLI[StackSlot] = std::move(LI);
  }
  SlotIndex Idx = LIS.getInstructionIndex(Spill);
  VNInfo *OrigVNI = StackSlotToOrigLI[StackSlot]->getVNInfoAt(Idx.getRegSlot());
  std::pair<int, VNInfo *> MIdx = std::make_pair(StackSlot, OrigVNI);
  MergeableSpills[MIdx].insert(&Spill);
}

/// When a spill is removed, remove the spill from MergeableSpills map.
/// Return true if the spill is removed successfully.
bool HoistSpillHelper::rmFromMergeableSpills(MachineInstr &Spill,
                                             int StackSlot) {
  auto It = StackSlotToOrigLI.find(StackSlot);
  if (It == StackSlotToOrigLI.end())
    return false;
  SlotIndex Idx = LIS.getInstructionIndex(Spill);
  VNInfo *OrigVNI = It->second->getVNInfoAt(Idx.getRegSlot());
  std::pair<int, VNInfo *> MIdx = std::make_pair(StackSlot, OrigVNI);
  return MergeableSpills[MIdx].erase(&Spill);
}

/// Check BB to see if it is a possible target BB to place a hoisted spill,
/// i.e., there should be a living sibling of OrigReg at the insert point.
bool HoistSpillHelper::isSpillCandBB(LiveInterval &OrigLI, VNInfo &OrigVNI,
                                     MachineBasicBlock &BB, unsigned &LiveReg) {
  SlotIndex Idx;
  unsigned OrigReg = OrigLI.reg;
  MachineBasicBlock::iterator MI = IPA.getLastInsertPointIter(OrigLI, BB);
  if (MI != BB.end())
    Idx = LIS.getInstructionIndex(*MI);
  else
    Idx = LIS.getMBBEndIdx(&BB).getPrevSlot();
  SmallSetVector<unsigned, 16> &Siblings = Virt2SiblingsMap[OrigReg];
  assert(OrigLI.getVNInfoAt(Idx) == &OrigVNI && "Unexpected VNI");

  for (auto const SibReg : Siblings) {
    LiveInterval &LI = LIS.getInterval(SibReg);
    VNInfo *VNI = LI.getVNInfoAt(Idx);
    if (VNI) {
      LiveReg = SibReg;
      return true;
    }
  }
  return false;
}

/// Remove redundant spills in the same BB. Save those redundant spills in
/// SpillsToRm, and save the spill to keep and its BB in SpillBBToSpill map.
void HoistSpillHelper::rmRedundantSpills(
    SmallPtrSet<MachineInstr *, 16> &Spills,
    SmallVectorImpl<MachineInstr *> &SpillsToRm,
    DenseMap<MachineDomTreeNode *, MachineInstr *> &SpillBBToSpill) {
  // For each spill saw, check SpillBBToSpill[] and see if its BB already has
  // another spill inside. If a BB contains more than one spill, only keep the
  // earlier spill with smaller SlotIndex.
  for (const auto CurrentSpill : Spills) {
    MachineBasicBlock *Block = CurrentSpill->getParent();
    MachineDomTreeNode *Node = MDT.getBase().getNode(Block);
    MachineInstr *PrevSpill = SpillBBToSpill[Node];
    if (PrevSpill) {
      SlotIndex PIdx = LIS.getInstructionIndex(*PrevSpill);
      SlotIndex CIdx = LIS.getInstructionIndex(*CurrentSpill);
      MachineInstr *SpillToRm = (CIdx > PIdx) ? CurrentSpill : PrevSpill;
      MachineInstr *SpillToKeep = (CIdx > PIdx) ? PrevSpill : CurrentSpill;
      SpillsToRm.push_back(SpillToRm);
      SpillBBToSpill[MDT.getBase().getNode(Block)] = SpillToKeep;
    } else {
      SpillBBToSpill[MDT.getBase().getNode(Block)] = CurrentSpill;
    }
  }
  for (const auto SpillToRm : SpillsToRm)
    Spills.erase(SpillToRm);
}

/// Starting from \p Root find a top-down traversal order of the dominator
/// tree to visit all basic blocks containing the elements of \p Spills.
/// Redundant spills will be found and put into \p SpillsToRm at the same
/// time. \p SpillBBToSpill will be populated as part of the process and
/// maps a basic block to the first store occurring in the basic block.
/// \post SpillsToRm.union(Spills\@post) == Spills\@pre
void HoistSpillHelper::getVisitOrders(
    MachineBasicBlock *Root, SmallPtrSet<MachineInstr *, 16> &Spills,
    SmallVectorImpl<MachineDomTreeNode *> &Orders,
    SmallVectorImpl<MachineInstr *> &SpillsToRm,
    DenseMap<MachineDomTreeNode *, unsigned> &SpillsToKeep,
    DenseMap<MachineDomTreeNode *, MachineInstr *> &SpillBBToSpill) {
  // The set contains all the possible BB nodes to which we may hoist
  // original spills.
  SmallPtrSet<MachineDomTreeNode *, 8> WorkSet;
  // Save the BB nodes on the path from the first BB node containing
  // non-redundant spill to the Root node.
  SmallPtrSet<MachineDomTreeNode *, 8> NodesOnPath;
  // All the spills to be hoisted must originate from a single def instruction
  // to the OrigReg. It means the def instruction should dominate all the spills
  // to be hoisted. We choose the BB where the def instruction is located as
  // the Root.
  MachineDomTreeNode *RootIDomNode = MDT[Root]->getIDom();
  // For every node on the dominator tree with spill, walk up on the dominator
  // tree towards the Root node until it is reached. If there is other node
  // containing spill in the middle of the path, the previous spill saw will
  // be redundant and the node containing it will be removed. All the nodes on
  // the path starting from the first node with non-redundant spill to the Root
  // node will be added to the WorkSet, which will contain all the possible
  // locations where spills may be hoisted to after the loop below is done.
  for (const auto Spill : Spills) {
    MachineBasicBlock *Block = Spill->getParent();
    MachineDomTreeNode *Node = MDT[Block];
    MachineInstr *SpillToRm = nullptr;
    while (Node != RootIDomNode) {
      // If Node dominates Block, and it already contains a spill, the spill in
      // Block will be redundant.
      if (Node != MDT[Block] && SpillBBToSpill[Node]) {
        SpillToRm = SpillBBToSpill[MDT[Block]];
        break;
        /// If we see the Node already in WorkSet, the path from the Node to
        /// the Root node must already be traversed by another spill.
        /// Then no need to repeat.
      } else if (WorkSet.count(Node)) {
        break;
      } else {
        NodesOnPath.insert(Node);
      }
      Node = Node->getIDom();
    }
    if (SpillToRm) {
      SpillsToRm.push_back(SpillToRm);
    } else {
      // Add a BB containing the original spills to SpillsToKeep -- i.e.,
      // set the initial status before hoisting start. The value of BBs
      // containing original spills is set to 0, in order to descriminate
      // with BBs containing hoisted spills which will be inserted to
      // SpillsToKeep later during hoisting.
      SpillsToKeep[MDT[Block]] = 0;
      WorkSet.insert(NodesOnPath.begin(), NodesOnPath.end());
    }
    NodesOnPath.clear();
  }

  // Sort the nodes in WorkSet in top-down order and save the nodes
  // in Orders. Orders will be used for hoisting in runHoistSpills.
  unsigned idx = 0;
  Orders.push_back(MDT.getBase().getNode(Root));
  do {
    MachineDomTreeNode *Node = Orders[idx++];
    const std::vector<MachineDomTreeNode *> &Children = Node->getChildren();
    unsigned NumChildren = Children.size();
    for (unsigned i = 0; i != NumChildren; ++i) {
      MachineDomTreeNode *Child = Children[i];
      if (WorkSet.count(Child))
        Orders.push_back(Child);
    }
  } while (idx != Orders.size());
  assert(Orders.size() == WorkSet.size() &&
         "Orders have different size with WorkSet");

#ifndef NDEBUG
  LLVM_DEBUG(dbgs() << "Orders size is " << Orders.size() << "\n");
  SmallVector<MachineDomTreeNode *, 32>::reverse_iterator RIt = Orders.rbegin();
  for (; RIt != Orders.rend(); RIt++)
    LLVM_DEBUG(dbgs() << "BB" << (*RIt)->getBlock()->getNumber() << ",");
  LLVM_DEBUG(dbgs() << "\n");
#endif
}

/// Try to hoist spills according to BB hotness. The spills to removed will
/// be saved in \p SpillsToRm. The spills to be inserted will be saved in
/// \p SpillsToIns.
void HoistSpillHelper::runHoistSpills(
    LiveInterval &OrigLI, VNInfo &OrigVNI,
    SmallPtrSet<MachineInstr *, 16> &Spills,
    SmallVectorImpl<MachineInstr *> &SpillsToRm,
    DenseMap<MachineBasicBlock *, unsigned> &SpillsToIns) {
  // Visit order of dominator tree nodes.
  SmallVector<MachineDomTreeNode *, 32> Orders;
  // SpillsToKeep contains all the nodes where spills are to be inserted
  // during hoisting. If the spill to be inserted is an original spill
  // (not a hoisted one), the value of the map entry is 0. If the spill
  // is a hoisted spill, the value of the map entry is the VReg to be used
  // as the source of the spill.
  DenseMap<MachineDomTreeNode *, unsigned> SpillsToKeep;
  // Map from BB to the first spill inside of it.
  DenseMap<MachineDomTreeNode *, MachineInstr *> SpillBBToSpill;

  rmRedundantSpills(Spills, SpillsToRm, SpillBBToSpill);

  MachineBasicBlock *Root = LIS.getMBBFromIndex(OrigVNI.def);
  getVisitOrders(Root, Spills, Orders, SpillsToRm, SpillsToKeep,
                 SpillBBToSpill);

  // SpillsInSubTreeMap keeps the map from a dom tree node to a pair of
  // nodes set and the cost of all the spills inside those nodes.
  // The nodes set are the locations where spills are to be inserted
  // in the subtree of current node.
  using NodesCostPair =
      std::pair<SmallPtrSet<MachineDomTreeNode *, 16>, BlockFrequency>;
  DenseMap<MachineDomTreeNode *, NodesCostPair> SpillsInSubTreeMap;

  // Iterate Orders set in reverse order, which will be a bottom-up order
  // in the dominator tree. Once we visit a dom tree node, we know its
  // children have already been visited and the spill locations in the
  // subtrees of all the children have been determined.
  SmallVector<MachineDomTreeNode *, 32>::reverse_iterator RIt = Orders.rbegin();
  for (; RIt != Orders.rend(); RIt++) {
    MachineBasicBlock *Block = (*RIt)->getBlock();

    // If Block contains an original spill, simply continue.
    if (SpillsToKeep.find(*RIt) != SpillsToKeep.end() && !SpillsToKeep[*RIt]) {
      SpillsInSubTreeMap[*RIt].first.insert(*RIt);
      // SpillsInSubTreeMap[*RIt].second contains the cost of spill.
      SpillsInSubTreeMap[*RIt].second = MBFI.getBlockFreq(Block);
      continue;
    }

    // Collect spills in subtree of current node (*RIt) to
    // SpillsInSubTreeMap[*RIt].first.
    const std::vector<MachineDomTreeNode *> &Children = (*RIt)->getChildren();
    unsigned NumChildren = Children.size();
    for (unsigned i = 0; i != NumChildren; ++i) {
      MachineDomTreeNode *Child = Children[i];
      if (SpillsInSubTreeMap.find(Child) == SpillsInSubTreeMap.end())
        continue;
      // The stmt "SpillsInSubTree = SpillsInSubTreeMap[*RIt].first" below
      // should be placed before getting the begin and end iterators of
      // SpillsInSubTreeMap[Child].first, or else the iterators may be
      // invalidated when SpillsInSubTreeMap[*RIt] is seen the first time
      // and the map grows and then the original buckets in the map are moved.
      SmallPtrSet<MachineDomTreeNode *, 16> &SpillsInSubTree =
          SpillsInSubTreeMap[*RIt].first;
      BlockFrequency &SubTreeCost = SpillsInSubTreeMap[*RIt].second;
      SubTreeCost += SpillsInSubTreeMap[Child].second;
      auto BI = SpillsInSubTreeMap[Child].first.begin();
      auto EI = SpillsInSubTreeMap[Child].first.end();
      SpillsInSubTree.insert(BI, EI);
      SpillsInSubTreeMap.erase(Child);
    }

    SmallPtrSet<MachineDomTreeNode *, 16> &SpillsInSubTree =
          SpillsInSubTreeMap[*RIt].first;
    BlockFrequency &SubTreeCost = SpillsInSubTreeMap[*RIt].second;
    // No spills in subtree, simply continue.
    if (SpillsInSubTree.empty())
      continue;

    // Check whether Block is a possible candidate to insert spill.
    unsigned LiveReg = 0;
    if (!isSpillCandBB(OrigLI, OrigVNI, *Block, LiveReg))
      continue;

    // If there are multiple spills that could be merged, bias a little
    // to hoist the spill.
    BranchProbability MarginProb = (SpillsInSubTree.size() > 1)
                                       ? BranchProbability(9, 10)
                                       : BranchProbability(1, 1);
    if (SubTreeCost > MBFI.getBlockFreq(Block) * MarginProb) {
      // Hoist: Move spills to current Block.
      for (const auto SpillBB : SpillsInSubTree) {
        // When SpillBB is a BB contains original spill, insert the spill
        // to SpillsToRm.
        if (SpillsToKeep.find(SpillBB) != SpillsToKeep.end() &&
            !SpillsToKeep[SpillBB]) {
          MachineInstr *SpillToRm = SpillBBToSpill[SpillBB];
          SpillsToRm.push_back(SpillToRm);
        }
        // SpillBB will not contain spill anymore, remove it from SpillsToKeep.
        SpillsToKeep.erase(SpillBB);
      }
      // Current Block is the BB containing the new hoisted spill. Add it to
      // SpillsToKeep. LiveReg is the source of the new spill.
      SpillsToKeep[*RIt] = LiveReg;
      LLVM_DEBUG({
        dbgs() << "spills in BB: ";
        for (const auto Rspill : SpillsInSubTree)
          dbgs() << Rspill->getBlock()->getNumber() << " ";
        dbgs() << "were promoted to BB" << (*RIt)->getBlock()->getNumber()
               << "\n";
      });
      SpillsInSubTree.clear();
      SpillsInSubTree.insert(*RIt);
      SubTreeCost = MBFI.getBlockFreq(Block);
    }
  }
  // For spills in SpillsToKeep with LiveReg set (i.e., not original spill),
  // save them to SpillsToIns.
  for (const auto Ent : SpillsToKeep) {
    if (Ent.second)
      SpillsToIns[Ent.first->getBlock()] = Ent.second;
  }
}

/// For spills with equal values, remove redundant spills and hoist those left
/// to less hot spots.
///
/// Spills with equal values will be collected into the same set in
/// MergeableSpills when spill is inserted. These equal spills are originated
/// from the same defining instruction and are dominated by the instruction.
/// Before hoisting all the equal spills, redundant spills inside in the same
/// BB are first marked to be deleted. Then starting from the spills left, walk
/// up on the dominator tree towards the Root node where the define instruction
/// is located, mark the dominated spills to be deleted along the way and
/// collect the BB nodes on the path from non-dominated spills to the define
/// instruction into a WorkSet. The nodes in WorkSet are the candidate places
/// where we are considering to hoist the spills. We iterate the WorkSet in
/// bottom-up order, and for each node, we will decide whether to hoist spills
/// inside its subtree to that node. In this way, we can get benefit locally
/// even if hoisting all the equal spills to one cold place is impossible.
void HoistSpillHelper::hoistAllSpills() {
  SmallVector<unsigned, 4> NewVRegs;
  LiveRangeEdit Edit(nullptr, NewVRegs, MF, LIS, &VRM, this);

  for (unsigned i = 0, e = MRI.getNumVirtRegs(); i != e; ++i) {
    unsigned Reg = TargetRegisterInfo::index2VirtReg(i);
    unsigned Original = VRM.getPreSplitReg(Reg);
    if (!MRI.def_empty(Reg))
      Virt2SiblingsMap[Original].insert(Reg);
  }

  // Each entry in MergeableSpills contains a spill set with equal values.
  for (auto &Ent : MergeableSpills) {
    int Slot = Ent.first.first;
    LiveInterval &OrigLI = *StackSlotToOrigLI[Slot];
    VNInfo *OrigVNI = Ent.first.second;
    SmallPtrSet<MachineInstr *, 16> &EqValSpills = Ent.second;
    if (Ent.second.empty())
      continue;

    LLVM_DEBUG({
      dbgs() << "\nFor Slot" << Slot << " and VN" << OrigVNI->id << ":\n"
             << "Equal spills in BB: ";
      for (const auto spill : EqValSpills)
        dbgs() << spill->getParent()->getNumber() << " ";
      dbgs() << "\n";
    });

    // SpillsToRm is the spill set to be removed from EqValSpills.
    SmallVector<MachineInstr *, 16> SpillsToRm;
    // SpillsToIns is the spill set to be newly inserted after hoisting.
    DenseMap<MachineBasicBlock *, unsigned> SpillsToIns;

    runHoistSpills(OrigLI, *OrigVNI, EqValSpills, SpillsToRm, SpillsToIns);

    LLVM_DEBUG({
      dbgs() << "Finally inserted spills in BB: ";
      for (const auto Ispill : SpillsToIns)
        dbgs() << Ispill.first->getNumber() << " ";
      dbgs() << "\nFinally removed spills in BB: ";
      for (const auto Rspill : SpillsToRm)
        dbgs() << Rspill->getParent()->getNumber() << " ";
      dbgs() << "\n";
    });

    // Stack live range update.
    LiveInterval &StackIntvl = LSS.getInterval(Slot);
    if (!SpillsToIns.empty() || !SpillsToRm.empty())
      StackIntvl.MergeValueInAsValue(OrigLI, OrigVNI,
                                     StackIntvl.getValNumInfo(0));

    // Insert hoisted spills.
    for (auto const Insert : SpillsToIns) {
      MachineBasicBlock *BB = Insert.first;
      unsigned LiveReg = Insert.second;
      MachineBasicBlock::iterator MI = IPA.getLastInsertPointIter(OrigLI, *BB);
      TII.storeRegToStackSlot(*BB, MI, LiveReg, false, Slot,
                              MRI.getRegClass(LiveReg), &TRI);
      LIS.InsertMachineInstrRangeInMaps(std::prev(MI), MI);
      ++NumSpills;
    }

    // Remove redundant spills or change them to dead instructions.
    NumSpills -= SpillsToRm.size();
    for (auto const RMEnt : SpillsToRm) {
      RMEnt->setDesc(TII.get(TargetOpcode::KILL));
      for (unsigned i = RMEnt->getNumOperands(); i; --i) {
        MachineOperand &MO = RMEnt->getOperand(i - 1);
        if (MO.isReg() && MO.isImplicit() && MO.isDef() && !MO.isDead())
          RMEnt->RemoveOperand(i - 1);
      }
    }
    Edit.eliminateDeadDefs(SpillsToRm, None, AA);
  }
}

/// For VirtReg clone, the \p New register should have the same physreg or
/// stackslot as the \p old register.
void HoistSpillHelper::LRE_DidCloneVirtReg(unsigned New, unsigned Old) {
  if (VRM.hasPhys(Old))
    VRM.assignVirt2Phys(New, VRM.getPhys(Old));
  else if (VRM.getStackSlot(Old) != VirtRegMap::NO_STACK_SLOT)
    VRM.assignVirt2StackSlot(New, VRM.getStackSlot(Old));
  else
    llvm_unreachable("VReg should be assigned either physreg or stackslot");
}
