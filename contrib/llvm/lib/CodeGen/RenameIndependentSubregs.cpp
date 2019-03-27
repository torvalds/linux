//===-- RenameIndependentSubregs.cpp - Live Interval Analysis -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// Rename independent subregisters looks for virtual registers with
/// independently used subregisters and renames them to new virtual registers.
/// Example: In the following:
///   %0:sub0<read-undef> = ...
///   %0:sub1 = ...
///   use %0:sub0
///   %0:sub0 = ...
///   use %0:sub0
///   use %0:sub1
/// sub0 and sub1 are never used together, and we have two independent sub0
/// definitions. This pass will rename to:
///   %0:sub0<read-undef> = ...
///   %1:sub1<read-undef> = ...
///   use %1:sub1
///   %2:sub1<read-undef> = ...
///   use %2:sub1
///   use %0:sub0
//
//===----------------------------------------------------------------------===//

#include "LiveRangeUtils.h"
#include "PHIEliminationUtils.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

using namespace llvm;

#define DEBUG_TYPE "rename-independent-subregs"

namespace {

class RenameIndependentSubregs : public MachineFunctionPass {
public:
  static char ID;
  RenameIndependentSubregs() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "Rename Disconnected Subregister Components";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<LiveIntervals>();
    AU.addPreserved<LiveIntervals>();
    AU.addRequired<SlotIndexes>();
    AU.addPreserved<SlotIndexes>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  struct SubRangeInfo {
    ConnectedVNInfoEqClasses ConEQ;
    LiveInterval::SubRange *SR;
    unsigned Index;

    SubRangeInfo(LiveIntervals &LIS, LiveInterval::SubRange &SR,
                 unsigned Index)
      : ConEQ(LIS), SR(&SR), Index(Index) {}
  };

  /// Split unrelated subregister components and rename them to new vregs.
  bool renameComponents(LiveInterval &LI) const;

  /// Build a vector of SubRange infos and a union find set of
  /// equivalence classes.
  /// Returns true if more than 1 equivalence class was found.
  bool findComponents(IntEqClasses &Classes,
                      SmallVectorImpl<SubRangeInfo> &SubRangeInfos,
                      LiveInterval &LI) const;

  /// Distribute the LiveInterval segments into the new LiveIntervals
  /// belonging to their class.
  void distribute(const IntEqClasses &Classes,
                  const SmallVectorImpl<SubRangeInfo> &SubRangeInfos,
                  const SmallVectorImpl<LiveInterval*> &Intervals) const;

  /// Constructs main liverange and add missing undef+dead flags.
  void computeMainRangesFixFlags(const IntEqClasses &Classes,
      const SmallVectorImpl<SubRangeInfo> &SubRangeInfos,
      const SmallVectorImpl<LiveInterval*> &Intervals) const;

  /// Rewrite Machine Operands to use the new vreg belonging to their class.
  void rewriteOperands(const IntEqClasses &Classes,
                       const SmallVectorImpl<SubRangeInfo> &SubRangeInfos,
                       const SmallVectorImpl<LiveInterval*> &Intervals) const;


  LiveIntervals *LIS;
  MachineRegisterInfo *MRI;
  const TargetInstrInfo *TII;
};

} // end anonymous namespace

char RenameIndependentSubregs::ID;

char &llvm::RenameIndependentSubregsID = RenameIndependentSubregs::ID;

INITIALIZE_PASS_BEGIN(RenameIndependentSubregs, DEBUG_TYPE,
                      "Rename Independent Subregisters", false, false)
INITIALIZE_PASS_DEPENDENCY(SlotIndexes)
INITIALIZE_PASS_DEPENDENCY(LiveIntervals)
INITIALIZE_PASS_END(RenameIndependentSubregs, DEBUG_TYPE,
                    "Rename Independent Subregisters", false, false)

bool RenameIndependentSubregs::renameComponents(LiveInterval &LI) const {
  // Shortcut: We cannot have split components with a single definition.
  if (LI.valnos.size() < 2)
    return false;

  SmallVector<SubRangeInfo, 4> SubRangeInfos;
  IntEqClasses Classes;
  if (!findComponents(Classes, SubRangeInfos, LI))
    return false;

  // Create a new VReg for each class.
  unsigned Reg = LI.reg;
  const TargetRegisterClass *RegClass = MRI->getRegClass(Reg);
  SmallVector<LiveInterval*, 4> Intervals;
  Intervals.push_back(&LI);
  LLVM_DEBUG(dbgs() << printReg(Reg) << ": Found " << Classes.getNumClasses()
                    << " equivalence classes.\n");
  LLVM_DEBUG(dbgs() << printReg(Reg) << ": Splitting into newly created:");
  for (unsigned I = 1, NumClasses = Classes.getNumClasses(); I < NumClasses;
       ++I) {
    unsigned NewVReg = MRI->createVirtualRegister(RegClass);
    LiveInterval &NewLI = LIS->createEmptyInterval(NewVReg);
    Intervals.push_back(&NewLI);
    LLVM_DEBUG(dbgs() << ' ' << printReg(NewVReg));
  }
  LLVM_DEBUG(dbgs() << '\n');

  rewriteOperands(Classes, SubRangeInfos, Intervals);
  distribute(Classes, SubRangeInfos, Intervals);
  computeMainRangesFixFlags(Classes, SubRangeInfos, Intervals);
  return true;
}

bool RenameIndependentSubregs::findComponents(IntEqClasses &Classes,
    SmallVectorImpl<RenameIndependentSubregs::SubRangeInfo> &SubRangeInfos,
    LiveInterval &LI) const {
  // First step: Create connected components for the VNInfos inside the
  // subranges and count the global number of such components.
  unsigned NumComponents = 0;
  for (LiveInterval::SubRange &SR : LI.subranges()) {
    SubRangeInfos.push_back(SubRangeInfo(*LIS, SR, NumComponents));
    ConnectedVNInfoEqClasses &ConEQ = SubRangeInfos.back().ConEQ;

    unsigned NumSubComponents = ConEQ.Classify(SR);
    NumComponents += NumSubComponents;
  }
  // Shortcut: With only 1 subrange, the normal separate component tests are
  // enough and we do not need to perform the union-find on the subregister
  // segments.
  if (SubRangeInfos.size() < 2)
    return false;

  // Next step: Build union-find structure over all subranges and merge classes
  // across subranges when they are affected by the same MachineOperand.
  const TargetRegisterInfo &TRI = *MRI->getTargetRegisterInfo();
  Classes.grow(NumComponents);
  unsigned Reg = LI.reg;
  for (const MachineOperand &MO : MRI->reg_nodbg_operands(Reg)) {
    if (!MO.isDef() && !MO.readsReg())
      continue;
    unsigned SubRegIdx = MO.getSubReg();
    LaneBitmask LaneMask = TRI.getSubRegIndexLaneMask(SubRegIdx);
    unsigned MergedID = ~0u;
    for (RenameIndependentSubregs::SubRangeInfo &SRInfo : SubRangeInfos) {
      const LiveInterval::SubRange &SR = *SRInfo.SR;
      if ((SR.LaneMask & LaneMask).none())
        continue;
      SlotIndex Pos = LIS->getInstructionIndex(*MO.getParent());
      Pos = MO.isDef() ? Pos.getRegSlot(MO.isEarlyClobber())
                       : Pos.getBaseIndex();
      const VNInfo *VNI = SR.getVNInfoAt(Pos);
      if (VNI == nullptr)
        continue;

      // Map to local representant ID.
      unsigned LocalID = SRInfo.ConEQ.getEqClass(VNI);
      // Global ID
      unsigned ID = LocalID + SRInfo.Index;
      // Merge other sets
      MergedID = MergedID == ~0u ? ID : Classes.join(MergedID, ID);
    }
  }

  // Early exit if we ended up with a single equivalence class.
  Classes.compress();
  unsigned NumClasses = Classes.getNumClasses();
  return NumClasses > 1;
}

void RenameIndependentSubregs::rewriteOperands(const IntEqClasses &Classes,
    const SmallVectorImpl<SubRangeInfo> &SubRangeInfos,
    const SmallVectorImpl<LiveInterval*> &Intervals) const {
  const TargetRegisterInfo &TRI = *MRI->getTargetRegisterInfo();
  unsigned Reg = Intervals[0]->reg;
  for (MachineRegisterInfo::reg_nodbg_iterator I = MRI->reg_nodbg_begin(Reg),
       E = MRI->reg_nodbg_end(); I != E; ) {
    MachineOperand &MO = *I++;
    if (!MO.isDef() && !MO.readsReg())
      continue;

    auto *MI = MO.getParent();
    SlotIndex Pos = LIS->getInstructionIndex(*MI);
    Pos = MO.isDef() ? Pos.getRegSlot(MO.isEarlyClobber())
                     : Pos.getBaseIndex();
    unsigned SubRegIdx = MO.getSubReg();
    LaneBitmask LaneMask = TRI.getSubRegIndexLaneMask(SubRegIdx);

    unsigned ID = ~0u;
    for (const SubRangeInfo &SRInfo : SubRangeInfos) {
      const LiveInterval::SubRange &SR = *SRInfo.SR;
      if ((SR.LaneMask & LaneMask).none())
        continue;
      const VNInfo *VNI = SR.getVNInfoAt(Pos);
      if (VNI == nullptr)
        continue;

      // Map to local representant ID.
      unsigned LocalID = SRInfo.ConEQ.getEqClass(VNI);
      // Global ID
      ID = Classes[LocalID + SRInfo.Index];
      break;
    }

    unsigned VReg = Intervals[ID]->reg;
    MO.setReg(VReg);

    if (MO.isTied() && Reg != VReg) {
      /// Undef use operands are not tracked in the equivalence class,
      /// but need to be updated if they are tied; take care to only
      /// update the tied operand.
      unsigned OperandNo = MI->getOperandNo(&MO);
      unsigned TiedIdx = MI->findTiedOperandIdx(OperandNo);
      MI->getOperand(TiedIdx).setReg(VReg);

      // above substitution breaks the iterator, so restart.
      I = MRI->reg_nodbg_begin(Reg);
    }
  }
  // TODO: We could attempt to recompute new register classes while visiting
  // the operands: Some of the split register may be fine with less constraint
  // classes than the original vreg.
}

void RenameIndependentSubregs::distribute(const IntEqClasses &Classes,
    const SmallVectorImpl<SubRangeInfo> &SubRangeInfos,
    const SmallVectorImpl<LiveInterval*> &Intervals) const {
  unsigned NumClasses = Classes.getNumClasses();
  SmallVector<unsigned, 8> VNIMapping;
  SmallVector<LiveInterval::SubRange*, 8> SubRanges;
  BumpPtrAllocator &Allocator = LIS->getVNInfoAllocator();
  for (const SubRangeInfo &SRInfo : SubRangeInfos) {
    LiveInterval::SubRange &SR = *SRInfo.SR;
    unsigned NumValNos = SR.valnos.size();
    VNIMapping.clear();
    VNIMapping.reserve(NumValNos);
    SubRanges.clear();
    SubRanges.resize(NumClasses-1, nullptr);
    for (unsigned I = 0; I < NumValNos; ++I) {
      const VNInfo &VNI = *SR.valnos[I];
      unsigned LocalID = SRInfo.ConEQ.getEqClass(&VNI);
      unsigned ID = Classes[LocalID + SRInfo.Index];
      VNIMapping.push_back(ID);
      if (ID > 0 && SubRanges[ID-1] == nullptr)
        SubRanges[ID-1] = Intervals[ID]->createSubRange(Allocator, SR.LaneMask);
    }
    DistributeRange(SR, SubRanges.data(), VNIMapping);
  }
}

static bool subRangeLiveAt(const LiveInterval &LI, SlotIndex Pos) {
  for (const LiveInterval::SubRange &SR : LI.subranges()) {
    if (SR.liveAt(Pos))
      return true;
  }
  return false;
}

void RenameIndependentSubregs::computeMainRangesFixFlags(
    const IntEqClasses &Classes,
    const SmallVectorImpl<SubRangeInfo> &SubRangeInfos,
    const SmallVectorImpl<LiveInterval*> &Intervals) const {
  BumpPtrAllocator &Allocator = LIS->getVNInfoAllocator();
  const SlotIndexes &Indexes = *LIS->getSlotIndexes();
  for (size_t I = 0, E = Intervals.size(); I < E; ++I) {
    LiveInterval &LI = *Intervals[I];
    unsigned Reg = LI.reg;

    LI.removeEmptySubRanges();

    // There must be a def (or live-in) before every use. Splitting vregs may
    // violate this principle as the splitted vreg may not have a definition on
    // every path. Fix this by creating IMPLICIT_DEF instruction as necessary.
    for (const LiveInterval::SubRange &SR : LI.subranges()) {
      // Search for "PHI" value numbers in the subranges. We must find a live
      // value in each predecessor block, add an IMPLICIT_DEF where it is
      // missing.
      for (unsigned I = 0; I < SR.valnos.size(); ++I) {
        const VNInfo &VNI = *SR.valnos[I];
        if (VNI.isUnused() || !VNI.isPHIDef())
          continue;

        SlotIndex Def = VNI.def;
        MachineBasicBlock &MBB = *Indexes.getMBBFromIndex(Def);
        for (MachineBasicBlock *PredMBB : MBB.predecessors()) {
          SlotIndex PredEnd = Indexes.getMBBEndIdx(PredMBB);
          if (subRangeLiveAt(LI, PredEnd.getPrevSlot()))
            continue;

          MachineBasicBlock::iterator InsertPos =
            llvm::findPHICopyInsertPoint(PredMBB, &MBB, Reg);
          const MCInstrDesc &MCDesc = TII->get(TargetOpcode::IMPLICIT_DEF);
          MachineInstrBuilder ImpDef = BuildMI(*PredMBB, InsertPos,
                                               DebugLoc(), MCDesc, Reg);
          SlotIndex DefIdx = LIS->InsertMachineInstrInMaps(*ImpDef);
          SlotIndex RegDefIdx = DefIdx.getRegSlot();
          for (LiveInterval::SubRange &SR : LI.subranges()) {
            VNInfo *SRVNI = SR.getNextValue(RegDefIdx, Allocator);
            SR.addSegment(LiveRange::Segment(RegDefIdx, PredEnd, SRVNI));
          }
        }
      }
    }

    for (MachineOperand &MO : MRI->reg_nodbg_operands(Reg)) {
      if (!MO.isDef())
        continue;
      unsigned SubRegIdx = MO.getSubReg();
      if (SubRegIdx == 0)
        continue;
      // After assigning the new vreg we may not have any other sublanes living
      // in and out of the instruction anymore. We need to add new dead and
      // undef flags in these cases.
      if (!MO.isUndef()) {
        SlotIndex Pos = LIS->getInstructionIndex(*MO.getParent());
        if (!subRangeLiveAt(LI, Pos))
          MO.setIsUndef();
      }
      if (!MO.isDead()) {
        SlotIndex Pos = LIS->getInstructionIndex(*MO.getParent()).getDeadSlot();
        if (!subRangeLiveAt(LI, Pos))
          MO.setIsDead();
      }
    }

    if (I == 0)
      LI.clear();
    LIS->constructMainRangeFromSubranges(LI);
    // A def of a subregister may be a use of other register lanes. Replacing
    // such a def with a def of a different register will eliminate the use,
    // and may cause the recorded live range to be larger than the actual
    // liveness in the program IR.
    LIS->shrinkToUses(&LI);
  }
}

bool RenameIndependentSubregs::runOnMachineFunction(MachineFunction &MF) {
  // Skip renaming if liveness of subregister is not tracked.
  MRI = &MF.getRegInfo();
  if (!MRI->subRegLivenessEnabled())
    return false;

  LLVM_DEBUG(dbgs() << "Renaming independent subregister live ranges in "
                    << MF.getName() << '\n');

  LIS = &getAnalysis<LiveIntervals>();
  TII = MF.getSubtarget().getInstrInfo();

  // Iterate over all vregs. Note that we query getNumVirtRegs() the newly
  // created vregs end up with higher numbers but do not need to be visited as
  // there can't be any further splitting.
  bool Changed = false;
  for (size_t I = 0, E = MRI->getNumVirtRegs(); I < E; ++I) {
    unsigned Reg = TargetRegisterInfo::index2VirtReg(I);
    if (!LIS->hasInterval(Reg))
      continue;
    LiveInterval &LI = LIS->getInterval(Reg);
    if (!LI.hasSubRanges())
      continue;

    Changed |= renameComponents(LI);
  }

  return Changed;
}
