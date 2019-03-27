//===- CalcSpillWeights.cpp -----------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/CalcSpillWeights.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/VirtRegMap.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <tuple>

using namespace llvm;

#define DEBUG_TYPE "calcspillweights"

void llvm::calculateSpillWeightsAndHints(LiveIntervals &LIS,
                           MachineFunction &MF,
                           VirtRegMap *VRM,
                           const MachineLoopInfo &MLI,
                           const MachineBlockFrequencyInfo &MBFI,
                           VirtRegAuxInfo::NormalizingFn norm) {
  LLVM_DEBUG(dbgs() << "********** Compute Spill Weights **********\n"
                    << "********** Function: " << MF.getName() << '\n');

  MachineRegisterInfo &MRI = MF.getRegInfo();
  VirtRegAuxInfo VRAI(MF, LIS, VRM, MLI, MBFI, norm);
  for (unsigned i = 0, e = MRI.getNumVirtRegs(); i != e; ++i) {
    unsigned Reg = TargetRegisterInfo::index2VirtReg(i);
    if (MRI.reg_nodbg_empty(Reg))
      continue;
    VRAI.calculateSpillWeightAndHint(LIS.getInterval(Reg));
  }
}

// Return the preferred allocation register for reg, given a COPY instruction.
static unsigned copyHint(const MachineInstr *mi, unsigned reg,
                         const TargetRegisterInfo &tri,
                         const MachineRegisterInfo &mri) {
  unsigned sub, hreg, hsub;
  if (mi->getOperand(0).getReg() == reg) {
    sub = mi->getOperand(0).getSubReg();
    hreg = mi->getOperand(1).getReg();
    hsub = mi->getOperand(1).getSubReg();
  } else {
    sub = mi->getOperand(1).getSubReg();
    hreg = mi->getOperand(0).getReg();
    hsub = mi->getOperand(0).getSubReg();
  }

  if (!hreg)
    return 0;

  if (TargetRegisterInfo::isVirtualRegister(hreg))
    return sub == hsub ? hreg : 0;

  const TargetRegisterClass *rc = mri.getRegClass(reg);
  unsigned CopiedPReg = (hsub ? tri.getSubReg(hreg, hsub) : hreg);
  if (rc->contains(CopiedPReg))
    return CopiedPReg;

  // Check if reg:sub matches so that a super register could be hinted.
  if (sub)
    return tri.getMatchingSuperReg(CopiedPReg, sub, rc);

  return 0;
}

// Check if all values in LI are rematerializable
static bool isRematerializable(const LiveInterval &LI,
                               const LiveIntervals &LIS,
                               VirtRegMap *VRM,
                               const TargetInstrInfo &TII) {
  unsigned Reg = LI.reg;
  unsigned Original = VRM ? VRM->getOriginal(Reg) : 0;
  for (LiveInterval::const_vni_iterator I = LI.vni_begin(), E = LI.vni_end();
       I != E; ++I) {
    const VNInfo *VNI = *I;
    if (VNI->isUnused())
      continue;
    if (VNI->isPHIDef())
      return false;

    MachineInstr *MI = LIS.getInstructionFromIndex(VNI->def);
    assert(MI && "Dead valno in interval");

    // Trace copies introduced by live range splitting.  The inline
    // spiller can rematerialize through these copies, so the spill
    // weight must reflect this.
    if (VRM) {
      while (MI->isFullCopy()) {
        // The copy destination must match the interval register.
        if (MI->getOperand(0).getReg() != Reg)
          return false;

        // Get the source register.
        Reg = MI->getOperand(1).getReg();

        // If the original (pre-splitting) registers match this
        // copy came from a split.
        if (!TargetRegisterInfo::isVirtualRegister(Reg) ||
            VRM->getOriginal(Reg) != Original)
          return false;

        // Follow the copy live-in value.
        const LiveInterval &SrcLI = LIS.getInterval(Reg);
        LiveQueryResult SrcQ = SrcLI.Query(VNI->def);
        VNI = SrcQ.valueIn();
        assert(VNI && "Copy from non-existing value");
        if (VNI->isPHIDef())
          return false;
        MI = LIS.getInstructionFromIndex(VNI->def);
        assert(MI && "Dead valno in interval");
      }
    }

    if (!TII.isTriviallyReMaterializable(*MI, LIS.getAliasAnalysis()))
      return false;
  }
  return true;
}

void VirtRegAuxInfo::calculateSpillWeightAndHint(LiveInterval &li) {
  float weight = weightCalcHelper(li);
  // Check if unspillable.
  if (weight < 0)
    return;
  li.weight = weight;
}

float VirtRegAuxInfo::futureWeight(LiveInterval &li, SlotIndex start,
                                   SlotIndex end) {
  return weightCalcHelper(li, &start, &end);
}

float VirtRegAuxInfo::weightCalcHelper(LiveInterval &li, SlotIndex *start,
                                       SlotIndex *end) {
  MachineRegisterInfo &mri = MF.getRegInfo();
  const TargetRegisterInfo &tri = *MF.getSubtarget().getRegisterInfo();
  MachineBasicBlock *mbb = nullptr;
  MachineLoop *loop = nullptr;
  bool isExiting = false;
  float totalWeight = 0;
  unsigned numInstr = 0; // Number of instructions using li
  SmallPtrSet<MachineInstr*, 8> visited;

  std::pair<unsigned, unsigned> TargetHint = mri.getRegAllocationHint(li.reg);

  // Don't recompute spill weight for an unspillable register.
  bool Spillable = li.isSpillable();

  bool localSplitArtifact = start && end;

  // Do not update future local split artifacts.
  bool updateLI = !localSplitArtifact;

  if (localSplitArtifact) {
    MachineBasicBlock *localMBB = LIS.getMBBFromIndex(*end);
    assert(localMBB == LIS.getMBBFromIndex(*start) &&
           "start and end are expected to be in the same basic block");

    // Local split artifact will have 2 additional copy instructions and they
    // will be in the same BB.
    // localLI = COPY other
    // ...
    // other   = COPY localLI
    totalWeight += LiveIntervals::getSpillWeight(true, false, &MBFI, localMBB);
    totalWeight += LiveIntervals::getSpillWeight(false, true, &MBFI, localMBB);

    numInstr += 2;
  }

  // CopyHint is a sortable hint derived from a COPY instruction.
  struct CopyHint {
    unsigned Reg;
    float Weight;
    bool IsPhys;
    CopyHint(unsigned R, float W, bool P) :
      Reg(R), Weight(W), IsPhys(P) {}
    bool operator<(const CopyHint &rhs) const {
      // Always prefer any physreg hint.
      if (IsPhys != rhs.IsPhys)
        return (IsPhys && !rhs.IsPhys);
      if (Weight != rhs.Weight)
        return (Weight > rhs.Weight);
      return Reg < rhs.Reg; // Tie-breaker.
    }
  };
  std::set<CopyHint> CopyHints;

  for (MachineRegisterInfo::reg_instr_iterator
       I = mri.reg_instr_begin(li.reg), E = mri.reg_instr_end();
       I != E; ) {
    MachineInstr *mi = &*(I++);

    // For local split artifacts, we are interested only in instructions between
    // the expected start and end of the range.
    SlotIndex si = LIS.getInstructionIndex(*mi);
    if (localSplitArtifact && ((si < *start) || (si > *end)))
      continue;

    numInstr++;
    if (mi->isIdentityCopy() || mi->isImplicitDef() || mi->isDebugInstr())
      continue;
    if (!visited.insert(mi).second)
      continue;

    float weight = 1.0f;
    if (Spillable) {
      // Get loop info for mi.
      if (mi->getParent() != mbb) {
        mbb = mi->getParent();
        loop = Loops.getLoopFor(mbb);
        isExiting = loop ? loop->isLoopExiting(mbb) : false;
      }

      // Calculate instr weight.
      bool reads, writes;
      std::tie(reads, writes) = mi->readsWritesVirtualRegister(li.reg);
      weight = LiveIntervals::getSpillWeight(writes, reads, &MBFI, *mi);

      // Give extra weight to what looks like a loop induction variable update.
      if (writes && isExiting && LIS.isLiveOutOfMBB(li, mbb))
        weight *= 3;

      totalWeight += weight;
    }

    // Get allocation hints from copies.
    if (!mi->isCopy())
      continue;
    unsigned hint = copyHint(mi, li.reg, tri, mri);
    if (!hint)
      continue;
    // Force hweight onto the stack so that x86 doesn't add hidden precision,
    // making the comparison incorrectly pass (i.e., 1 > 1 == true??).
    //
    // FIXME: we probably shouldn't use floats at all.
    volatile float hweight = Hint[hint] += weight;
    if (TargetRegisterInfo::isVirtualRegister(hint) || mri.isAllocatable(hint))
      CopyHints.insert(CopyHint(hint, hweight, tri.isPhysicalRegister(hint)));
  }

  Hint.clear();

  // Pass all the sorted copy hints to mri.
  if (updateLI && CopyHints.size()) {
    // Remove a generic hint if previously added by target.
    if (TargetHint.first == 0 && TargetHint.second)
      mri.clearSimpleHint(li.reg);

    std::set<unsigned> HintedRegs;
    for (auto &Hint : CopyHints) {
      if (!HintedRegs.insert(Hint.Reg).second ||
          (TargetHint.first != 0 && Hint.Reg == TargetHint.second))
        // Don't add the same reg twice or the target-type hint again.
        continue;
      mri.addRegAllocationHint(li.reg, Hint.Reg);
    }

    // Weakly boost the spill weight of hinted registers.
    totalWeight *= 1.01F;
  }

  // If the live interval was already unspillable, leave it that way.
  if (!Spillable)
    return -1.0;

  // Mark li as unspillable if all live ranges are tiny and the interval
  // is not live at any reg mask.  If the interval is live at a reg mask
  // spilling may be required.
  if (updateLI && li.isZeroLength(LIS.getSlotIndexes()) &&
      !li.isLiveAtIndexes(LIS.getRegMaskSlots())) {
    li.markNotSpillable();
    return -1.0;
  }

  // If all of the definitions of the interval are re-materializable,
  // it is a preferred candidate for spilling.
  // FIXME: this gets much more complicated once we support non-trivial
  // re-materialization.
  if (isRematerializable(li, LIS, VRM, *MF.getSubtarget().getInstrInfo()))
    totalWeight *= 0.5F;

  if (localSplitArtifact)
    return normalize(totalWeight, start->distance(*end), numInstr);
  return normalize(totalWeight, li.getSize(), numInstr);
}
