//===- LiveRegMatrix.cpp - Track register interference --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the LiveRegMatrix analysis pass.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/LiveRegMatrix.h"
#include "RegisterCoalescer.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/LiveIntervalUnion.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/VirtRegMap.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

using namespace llvm;

#define DEBUG_TYPE "regalloc"

STATISTIC(NumAssigned   , "Number of registers assigned");
STATISTIC(NumUnassigned , "Number of registers unassigned");

char LiveRegMatrix::ID = 0;
INITIALIZE_PASS_BEGIN(LiveRegMatrix, "liveregmatrix",
                      "Live Register Matrix", false, false)
INITIALIZE_PASS_DEPENDENCY(LiveIntervals)
INITIALIZE_PASS_DEPENDENCY(VirtRegMap)
INITIALIZE_PASS_END(LiveRegMatrix, "liveregmatrix",
                    "Live Register Matrix", false, false)

LiveRegMatrix::LiveRegMatrix() : MachineFunctionPass(ID) {}

void LiveRegMatrix::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequiredTransitive<LiveIntervals>();
  AU.addRequiredTransitive<VirtRegMap>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool LiveRegMatrix::runOnMachineFunction(MachineFunction &MF) {
  TRI = MF.getSubtarget().getRegisterInfo();
  LIS = &getAnalysis<LiveIntervals>();
  VRM = &getAnalysis<VirtRegMap>();

  unsigned NumRegUnits = TRI->getNumRegUnits();
  if (NumRegUnits != Matrix.size())
    Queries.reset(new LiveIntervalUnion::Query[NumRegUnits]);
  Matrix.init(LIUAlloc, NumRegUnits);

  // Make sure no stale queries get reused.
  invalidateVirtRegs();
  return false;
}

void LiveRegMatrix::releaseMemory() {
  for (unsigned i = 0, e = Matrix.size(); i != e; ++i) {
    Matrix[i].clear();
    // No need to clear Queries here, since LiveIntervalUnion::Query doesn't
    // have anything important to clear and LiveRegMatrix's runOnFunction()
    // does a std::unique_ptr::reset anyways.
  }
}

template <typename Callable>
static bool foreachUnit(const TargetRegisterInfo *TRI,
                        LiveInterval &VRegInterval, unsigned PhysReg,
                        Callable Func) {
  if (VRegInterval.hasSubRanges()) {
    for (MCRegUnitMaskIterator Units(PhysReg, TRI); Units.isValid(); ++Units) {
      unsigned Unit = (*Units).first;
      LaneBitmask Mask = (*Units).second;
      for (LiveInterval::SubRange &S : VRegInterval.subranges()) {
        if ((S.LaneMask & Mask).any()) {
          if (Func(Unit, S))
            return true;
          break;
        }
      }
    }
  } else {
    for (MCRegUnitIterator Units(PhysReg, TRI); Units.isValid(); ++Units) {
      if (Func(*Units, VRegInterval))
        return true;
    }
  }
  return false;
}

void LiveRegMatrix::assign(LiveInterval &VirtReg, unsigned PhysReg) {
  LLVM_DEBUG(dbgs() << "assigning " << printReg(VirtReg.reg, TRI) << " to "
                    << printReg(PhysReg, TRI) << ':');
  assert(!VRM->hasPhys(VirtReg.reg) && "Duplicate VirtReg assignment");
  VRM->assignVirt2Phys(VirtReg.reg, PhysReg);

  foreachUnit(
      TRI, VirtReg, PhysReg, [&](unsigned Unit, const LiveRange &Range) {
        LLVM_DEBUG(dbgs() << ' ' << printRegUnit(Unit, TRI) << ' ' << Range);
        Matrix[Unit].unify(VirtReg, Range);
        return false;
      });

  ++NumAssigned;
  LLVM_DEBUG(dbgs() << '\n');
}

void LiveRegMatrix::unassign(LiveInterval &VirtReg) {
  unsigned PhysReg = VRM->getPhys(VirtReg.reg);
  LLVM_DEBUG(dbgs() << "unassigning " << printReg(VirtReg.reg, TRI) << " from "
                    << printReg(PhysReg, TRI) << ':');
  VRM->clearVirt(VirtReg.reg);

  foreachUnit(TRI, VirtReg, PhysReg,
              [&](unsigned Unit, const LiveRange &Range) {
                LLVM_DEBUG(dbgs() << ' ' << printRegUnit(Unit, TRI));
                Matrix[Unit].extract(VirtReg, Range);
                return false;
              });

  ++NumUnassigned;
  LLVM_DEBUG(dbgs() << '\n');
}

bool LiveRegMatrix::isPhysRegUsed(unsigned PhysReg) const {
  for (MCRegUnitIterator Unit(PhysReg, TRI); Unit.isValid(); ++Unit) {
    if (!Matrix[*Unit].empty())
      return true;
  }
  return false;
}

bool LiveRegMatrix::checkRegMaskInterference(LiveInterval &VirtReg,
                                             unsigned PhysReg) {
  // Check if the cached information is valid.
  // The same BitVector can be reused for all PhysRegs.
  // We could cache multiple VirtRegs if it becomes necessary.
  if (RegMaskVirtReg != VirtReg.reg || RegMaskTag != UserTag) {
    RegMaskVirtReg = VirtReg.reg;
    RegMaskTag = UserTag;
    RegMaskUsable.clear();
    LIS->checkRegMaskInterference(VirtReg, RegMaskUsable);
  }

  // The BitVector is indexed by PhysReg, not register unit.
  // Regmask interference is more fine grained than regunits.
  // For example, a Win64 call can clobber %ymm8 yet preserve %xmm8.
  return !RegMaskUsable.empty() && (!PhysReg || !RegMaskUsable.test(PhysReg));
}

bool LiveRegMatrix::checkRegUnitInterference(LiveInterval &VirtReg,
                                             unsigned PhysReg) {
  if (VirtReg.empty())
    return false;
  CoalescerPair CP(VirtReg.reg, PhysReg, *TRI);

  bool Result = foreachUnit(TRI, VirtReg, PhysReg, [&](unsigned Unit,
                                                       const LiveRange &Range) {
    const LiveRange &UnitRange = LIS->getRegUnit(Unit);
    return Range.overlaps(UnitRange, CP, *LIS->getSlotIndexes());
  });
  return Result;
}

LiveIntervalUnion::Query &LiveRegMatrix::query(const LiveRange &LR,
                                               unsigned RegUnit) {
  LiveIntervalUnion::Query &Q = Queries[RegUnit];
  Q.init(UserTag, LR, Matrix[RegUnit]);
  return Q;
}

LiveRegMatrix::InterferenceKind
LiveRegMatrix::checkInterference(LiveInterval &VirtReg, unsigned PhysReg) {
  if (VirtReg.empty())
    return IK_Free;

  // Regmask interference is the fastest check.
  if (checkRegMaskInterference(VirtReg, PhysReg))
    return IK_RegMask;

  // Check for fixed interference.
  if (checkRegUnitInterference(VirtReg, PhysReg))
    return IK_RegUnit;

  // Check the matrix for virtual register interference.
  bool Interference = foreachUnit(TRI, VirtReg, PhysReg,
                                  [&](unsigned Unit, const LiveRange &LR) {
    return query(LR, Unit).checkInterference();
  });
  if (Interference)
    return IK_VirtReg;

  return IK_Free;
}

bool LiveRegMatrix::checkInterference(SlotIndex Start, SlotIndex End,
                                      unsigned PhysReg) {
  // Construct artificial live range containing only one segment [Start, End).
  VNInfo valno(0, Start);
  LiveRange::Segment Seg(Start, End, &valno);
  LiveRange LR;
  LR.addSegment(Seg);

  // Check for interference with that segment
  for (MCRegUnitIterator Units(PhysReg, TRI); Units.isValid(); ++Units) {
    if (query(LR, *Units).checkInterference())
      return true;
  }
  return false;
}
