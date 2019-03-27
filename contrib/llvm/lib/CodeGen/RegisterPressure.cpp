//===- RegisterPressure.cpp - Dynamic Register Pressure -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the RegisterPressure class which can be used to track
// MachineInstr level register pressure.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/RegisterPressure.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterClassInfo.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>
#include <utility>
#include <vector>

using namespace llvm;

/// Increase pressure for each pressure set provided by TargetRegisterInfo.
static void increaseSetPressure(std::vector<unsigned> &CurrSetPressure,
                                const MachineRegisterInfo &MRI, unsigned Reg,
                                LaneBitmask PrevMask, LaneBitmask NewMask) {
  assert((PrevMask & ~NewMask).none() && "Must not remove bits");
  if (PrevMask.any() || NewMask.none())
    return;

  PSetIterator PSetI = MRI.getPressureSets(Reg);
  unsigned Weight = PSetI.getWeight();
  for (; PSetI.isValid(); ++PSetI)
    CurrSetPressure[*PSetI] += Weight;
}

/// Decrease pressure for each pressure set provided by TargetRegisterInfo.
static void decreaseSetPressure(std::vector<unsigned> &CurrSetPressure,
                                const MachineRegisterInfo &MRI, unsigned Reg,
                                LaneBitmask PrevMask, LaneBitmask NewMask) {
  //assert((NewMask & !PrevMask) == 0 && "Must not add bits");
  if (NewMask.any() || PrevMask.none())
    return;

  PSetIterator PSetI = MRI.getPressureSets(Reg);
  unsigned Weight = PSetI.getWeight();
  for (; PSetI.isValid(); ++PSetI) {
    assert(CurrSetPressure[*PSetI] >= Weight && "register pressure underflow");
    CurrSetPressure[*PSetI] -= Weight;
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD
void llvm::dumpRegSetPressure(ArrayRef<unsigned> SetPressure,
                              const TargetRegisterInfo *TRI) {
  bool Empty = true;
  for (unsigned i = 0, e = SetPressure.size(); i < e; ++i) {
    if (SetPressure[i] != 0) {
      dbgs() << TRI->getRegPressureSetName(i) << "=" << SetPressure[i] << '\n';
      Empty = false;
    }
  }
  if (Empty)
    dbgs() << "\n";
}

LLVM_DUMP_METHOD
void RegisterPressure::dump(const TargetRegisterInfo *TRI) const {
  dbgs() << "Max Pressure: ";
  dumpRegSetPressure(MaxSetPressure, TRI);
  dbgs() << "Live In: ";
  for (const RegisterMaskPair &P : LiveInRegs) {
    dbgs() << printVRegOrUnit(P.RegUnit, TRI);
    if (!P.LaneMask.all())
      dbgs() << ':' << PrintLaneMask(P.LaneMask);
    dbgs() << ' ';
  }
  dbgs() << '\n';
  dbgs() << "Live Out: ";
  for (const RegisterMaskPair &P : LiveOutRegs) {
    dbgs() << printVRegOrUnit(P.RegUnit, TRI);
    if (!P.LaneMask.all())
      dbgs() << ':' << PrintLaneMask(P.LaneMask);
    dbgs() << ' ';
  }
  dbgs() << '\n';
}

LLVM_DUMP_METHOD
void RegPressureTracker::dump() const {
  if (!isTopClosed() || !isBottomClosed()) {
    dbgs() << "Curr Pressure: ";
    dumpRegSetPressure(CurrSetPressure, TRI);
  }
  P.dump(TRI);
}

LLVM_DUMP_METHOD
void PressureDiff::dump(const TargetRegisterInfo &TRI) const {
  const char *sep = "";
  for (const PressureChange &Change : *this) {
    if (!Change.isValid())
      break;
    dbgs() << sep << TRI.getRegPressureSetName(Change.getPSet())
           << " " << Change.getUnitInc();
    sep = "    ";
  }
  dbgs() << '\n';
}
#endif

void RegPressureTracker::increaseRegPressure(unsigned RegUnit,
                                             LaneBitmask PreviousMask,
                                             LaneBitmask NewMask) {
  if (PreviousMask.any() || NewMask.none())
    return;

  PSetIterator PSetI = MRI->getPressureSets(RegUnit);
  unsigned Weight = PSetI.getWeight();
  for (; PSetI.isValid(); ++PSetI) {
    CurrSetPressure[*PSetI] += Weight;
    P.MaxSetPressure[*PSetI] =
        std::max(P.MaxSetPressure[*PSetI], CurrSetPressure[*PSetI]);
  }
}

void RegPressureTracker::decreaseRegPressure(unsigned RegUnit,
                                             LaneBitmask PreviousMask,
                                             LaneBitmask NewMask) {
  decreaseSetPressure(CurrSetPressure, *MRI, RegUnit, PreviousMask, NewMask);
}

/// Clear the result so it can be used for another round of pressure tracking.
void IntervalPressure::reset() {
  TopIdx = BottomIdx = SlotIndex();
  MaxSetPressure.clear();
  LiveInRegs.clear();
  LiveOutRegs.clear();
}

/// Clear the result so it can be used for another round of pressure tracking.
void RegionPressure::reset() {
  TopPos = BottomPos = MachineBasicBlock::const_iterator();
  MaxSetPressure.clear();
  LiveInRegs.clear();
  LiveOutRegs.clear();
}

/// If the current top is not less than or equal to the next index, open it.
/// We happen to need the SlotIndex for the next top for pressure update.
void IntervalPressure::openTop(SlotIndex NextTop) {
  if (TopIdx <= NextTop)
    return;
  TopIdx = SlotIndex();
  LiveInRegs.clear();
}

/// If the current top is the previous instruction (before receding), open it.
void RegionPressure::openTop(MachineBasicBlock::const_iterator PrevTop) {
  if (TopPos != PrevTop)
    return;
  TopPos = MachineBasicBlock::const_iterator();
  LiveInRegs.clear();
}

/// If the current bottom is not greater than the previous index, open it.
void IntervalPressure::openBottom(SlotIndex PrevBottom) {
  if (BottomIdx > PrevBottom)
    return;
  BottomIdx = SlotIndex();
  LiveInRegs.clear();
}

/// If the current bottom is the previous instr (before advancing), open it.
void RegionPressure::openBottom(MachineBasicBlock::const_iterator PrevBottom) {
  if (BottomPos != PrevBottom)
    return;
  BottomPos = MachineBasicBlock::const_iterator();
  LiveInRegs.clear();
}

void LiveRegSet::init(const MachineRegisterInfo &MRI) {
  const TargetRegisterInfo &TRI = *MRI.getTargetRegisterInfo();
  unsigned NumRegUnits = TRI.getNumRegs();
  unsigned NumVirtRegs = MRI.getNumVirtRegs();
  Regs.setUniverse(NumRegUnits + NumVirtRegs);
  this->NumRegUnits = NumRegUnits;
}

void LiveRegSet::clear() {
  Regs.clear();
}

static const LiveRange *getLiveRange(const LiveIntervals &LIS, unsigned Reg) {
  if (TargetRegisterInfo::isVirtualRegister(Reg))
    return &LIS.getInterval(Reg);
  return LIS.getCachedRegUnit(Reg);
}

void RegPressureTracker::reset() {
  MBB = nullptr;
  LIS = nullptr;

  CurrSetPressure.clear();
  LiveThruPressure.clear();
  P.MaxSetPressure.clear();

  if (RequireIntervals)
    static_cast<IntervalPressure&>(P).reset();
  else
    static_cast<RegionPressure&>(P).reset();

  LiveRegs.clear();
  UntiedDefs.clear();
}

/// Setup the RegPressureTracker.
///
/// TODO: Add support for pressure without LiveIntervals.
void RegPressureTracker::init(const MachineFunction *mf,
                              const RegisterClassInfo *rci,
                              const LiveIntervals *lis,
                              const MachineBasicBlock *mbb,
                              MachineBasicBlock::const_iterator pos,
                              bool TrackLaneMasks, bool TrackUntiedDefs) {
  reset();

  MF = mf;
  TRI = MF->getSubtarget().getRegisterInfo();
  RCI = rci;
  MRI = &MF->getRegInfo();
  MBB = mbb;
  this->TrackUntiedDefs = TrackUntiedDefs;
  this->TrackLaneMasks = TrackLaneMasks;

  if (RequireIntervals) {
    assert(lis && "IntervalPressure requires LiveIntervals");
    LIS = lis;
  }

  CurrPos = pos;
  CurrSetPressure.assign(TRI->getNumRegPressureSets(), 0);

  P.MaxSetPressure = CurrSetPressure;

  LiveRegs.init(*MRI);
  if (TrackUntiedDefs)
    UntiedDefs.setUniverse(MRI->getNumVirtRegs());
}

/// Does this pressure result have a valid top position and live ins.
bool RegPressureTracker::isTopClosed() const {
  if (RequireIntervals)
    return static_cast<IntervalPressure&>(P).TopIdx.isValid();
  return (static_cast<RegionPressure&>(P).TopPos ==
          MachineBasicBlock::const_iterator());
}

/// Does this pressure result have a valid bottom position and live outs.
bool RegPressureTracker::isBottomClosed() const {
  if (RequireIntervals)
    return static_cast<IntervalPressure&>(P).BottomIdx.isValid();
  return (static_cast<RegionPressure&>(P).BottomPos ==
          MachineBasicBlock::const_iterator());
}

SlotIndex RegPressureTracker::getCurrSlot() const {
  MachineBasicBlock::const_iterator IdxPos =
    skipDebugInstructionsForward(CurrPos, MBB->end());
  if (IdxPos == MBB->end())
    return LIS->getMBBEndIdx(MBB);
  return LIS->getInstructionIndex(*IdxPos).getRegSlot();
}

/// Set the boundary for the top of the region and summarize live ins.
void RegPressureTracker::closeTop() {
  if (RequireIntervals)
    static_cast<IntervalPressure&>(P).TopIdx = getCurrSlot();
  else
    static_cast<RegionPressure&>(P).TopPos = CurrPos;

  assert(P.LiveInRegs.empty() && "inconsistent max pressure result");
  P.LiveInRegs.reserve(LiveRegs.size());
  LiveRegs.appendTo(P.LiveInRegs);
}

/// Set the boundary for the bottom of the region and summarize live outs.
void RegPressureTracker::closeBottom() {
  if (RequireIntervals)
    static_cast<IntervalPressure&>(P).BottomIdx = getCurrSlot();
  else
    static_cast<RegionPressure&>(P).BottomPos = CurrPos;

  assert(P.LiveOutRegs.empty() && "inconsistent max pressure result");
  P.LiveOutRegs.reserve(LiveRegs.size());
  LiveRegs.appendTo(P.LiveOutRegs);
}

/// Finalize the region boundaries and record live ins and live outs.
void RegPressureTracker::closeRegion() {
  if (!isTopClosed() && !isBottomClosed()) {
    assert(LiveRegs.size() == 0 && "no region boundary");
    return;
  }
  if (!isBottomClosed())
    closeBottom();
  else if (!isTopClosed())
    closeTop();
  // If both top and bottom are closed, do nothing.
}

/// The register tracker is unaware of global liveness so ignores normal
/// live-thru ranges. However, two-address or coalesced chains can also lead
/// to live ranges with no holes. Count these to inform heuristics that we
/// can never drop below this pressure.
void RegPressureTracker::initLiveThru(const RegPressureTracker &RPTracker) {
  LiveThruPressure.assign(TRI->getNumRegPressureSets(), 0);
  assert(isBottomClosed() && "need bottom-up tracking to intialize.");
  for (const RegisterMaskPair &Pair : P.LiveOutRegs) {
    unsigned RegUnit = Pair.RegUnit;
    if (TargetRegisterInfo::isVirtualRegister(RegUnit)
        && !RPTracker.hasUntiedDef(RegUnit))
      increaseSetPressure(LiveThruPressure, *MRI, RegUnit,
                          LaneBitmask::getNone(), Pair.LaneMask);
  }
}

static LaneBitmask getRegLanes(ArrayRef<RegisterMaskPair> RegUnits,
                               unsigned RegUnit) {
  auto I = llvm::find_if(RegUnits, [RegUnit](const RegisterMaskPair Other) {
    return Other.RegUnit == RegUnit;
  });
  if (I == RegUnits.end())
    return LaneBitmask::getNone();
  return I->LaneMask;
}

static void addRegLanes(SmallVectorImpl<RegisterMaskPair> &RegUnits,
                        RegisterMaskPair Pair) {
  unsigned RegUnit = Pair.RegUnit;
  assert(Pair.LaneMask.any());
  auto I = llvm::find_if(RegUnits, [RegUnit](const RegisterMaskPair Other) {
    return Other.RegUnit == RegUnit;
  });
  if (I == RegUnits.end()) {
    RegUnits.push_back(Pair);
  } else {
    I->LaneMask |= Pair.LaneMask;
  }
}

static void setRegZero(SmallVectorImpl<RegisterMaskPair> &RegUnits,
                       unsigned RegUnit) {
  auto I = llvm::find_if(RegUnits, [RegUnit](const RegisterMaskPair Other) {
    return Other.RegUnit == RegUnit;
  });
  if (I == RegUnits.end()) {
    RegUnits.push_back(RegisterMaskPair(RegUnit, LaneBitmask::getNone()));
  } else {
    I->LaneMask = LaneBitmask::getNone();
  }
}

static void removeRegLanes(SmallVectorImpl<RegisterMaskPair> &RegUnits,
                           RegisterMaskPair Pair) {
  unsigned RegUnit = Pair.RegUnit;
  assert(Pair.LaneMask.any());
  auto I = llvm::find_if(RegUnits, [RegUnit](const RegisterMaskPair Other) {
    return Other.RegUnit == RegUnit;
  });
  if (I != RegUnits.end()) {
    I->LaneMask &= ~Pair.LaneMask;
    if (I->LaneMask.none())
      RegUnits.erase(I);
  }
}

static LaneBitmask getLanesWithProperty(const LiveIntervals &LIS,
    const MachineRegisterInfo &MRI, bool TrackLaneMasks, unsigned RegUnit,
    SlotIndex Pos, LaneBitmask SafeDefault,
    bool(*Property)(const LiveRange &LR, SlotIndex Pos)) {
  if (TargetRegisterInfo::isVirtualRegister(RegUnit)) {
    const LiveInterval &LI = LIS.getInterval(RegUnit);
    LaneBitmask Result;
    if (TrackLaneMasks && LI.hasSubRanges()) {
        for (const LiveInterval::SubRange &SR : LI.subranges()) {
          if (Property(SR, Pos))
            Result |= SR.LaneMask;
        }
    } else if (Property(LI, Pos)) {
      Result = TrackLaneMasks ? MRI.getMaxLaneMaskForVReg(RegUnit)
                              : LaneBitmask::getAll();
    }

    return Result;
  } else {
    const LiveRange *LR = LIS.getCachedRegUnit(RegUnit);
    // Be prepared for missing liveranges: We usually do not compute liveranges
    // for physical registers on targets with many registers (GPUs).
    if (LR == nullptr)
      return SafeDefault;
    return Property(*LR, Pos) ? LaneBitmask::getAll() : LaneBitmask::getNone();
  }
}

static LaneBitmask getLiveLanesAt(const LiveIntervals &LIS,
                                  const MachineRegisterInfo &MRI,
                                  bool TrackLaneMasks, unsigned RegUnit,
                                  SlotIndex Pos) {
  return getLanesWithProperty(LIS, MRI, TrackLaneMasks, RegUnit, Pos,
                              LaneBitmask::getAll(),
                              [](const LiveRange &LR, SlotIndex Pos) {
                                return LR.liveAt(Pos);
                              });
}


namespace {

/// Collect this instruction's unique uses and defs into SmallVectors for
/// processing defs and uses in order.
///
/// FIXME: always ignore tied opers
class RegisterOperandsCollector {
  friend class llvm::RegisterOperands;

  RegisterOperands &RegOpers;
  const TargetRegisterInfo &TRI;
  const MachineRegisterInfo &MRI;
  bool IgnoreDead;

  RegisterOperandsCollector(RegisterOperands &RegOpers,
                            const TargetRegisterInfo &TRI,
                            const MachineRegisterInfo &MRI, bool IgnoreDead)
    : RegOpers(RegOpers), TRI(TRI), MRI(MRI), IgnoreDead(IgnoreDead) {}

  void collectInstr(const MachineInstr &MI) const {
    for (ConstMIBundleOperands OperI(MI); OperI.isValid(); ++OperI)
      collectOperand(*OperI);

    // Remove redundant physreg dead defs.
    for (const RegisterMaskPair &P : RegOpers.Defs)
      removeRegLanes(RegOpers.DeadDefs, P);
  }

  void collectInstrLanes(const MachineInstr &MI) const {
    for (ConstMIBundleOperands OperI(MI); OperI.isValid(); ++OperI)
      collectOperandLanes(*OperI);

    // Remove redundant physreg dead defs.
    for (const RegisterMaskPair &P : RegOpers.Defs)
      removeRegLanes(RegOpers.DeadDefs, P);
  }

  /// Push this operand's register onto the correct vectors.
  void collectOperand(const MachineOperand &MO) const {
    if (!MO.isReg() || !MO.getReg())
      return;
    unsigned Reg = MO.getReg();
    if (MO.isUse()) {
      if (!MO.isUndef() && !MO.isInternalRead())
        pushReg(Reg, RegOpers.Uses);
    } else {
      assert(MO.isDef());
      // Subregister definitions may imply a register read.
      if (MO.readsReg())
        pushReg(Reg, RegOpers.Uses);

      if (MO.isDead()) {
        if (!IgnoreDead)
          pushReg(Reg, RegOpers.DeadDefs);
      } else
        pushReg(Reg, RegOpers.Defs);
    }
  }

  void pushReg(unsigned Reg,
               SmallVectorImpl<RegisterMaskPair> &RegUnits) const {
    if (TargetRegisterInfo::isVirtualRegister(Reg)) {
      addRegLanes(RegUnits, RegisterMaskPair(Reg, LaneBitmask::getAll()));
    } else if (MRI.isAllocatable(Reg)) {
      for (MCRegUnitIterator Units(Reg, &TRI); Units.isValid(); ++Units)
        addRegLanes(RegUnits, RegisterMaskPair(*Units, LaneBitmask::getAll()));
    }
  }

  void collectOperandLanes(const MachineOperand &MO) const {
    if (!MO.isReg() || !MO.getReg())
      return;
    unsigned Reg = MO.getReg();
    unsigned SubRegIdx = MO.getSubReg();
    if (MO.isUse()) {
      if (!MO.isUndef() && !MO.isInternalRead())
        pushRegLanes(Reg, SubRegIdx, RegOpers.Uses);
    } else {
      assert(MO.isDef());
      // Treat read-undef subreg defs as definitions of the whole register.
      if (MO.isUndef())
        SubRegIdx = 0;

      if (MO.isDead()) {
        if (!IgnoreDead)
          pushRegLanes(Reg, SubRegIdx, RegOpers.DeadDefs);
      } else
        pushRegLanes(Reg, SubRegIdx, RegOpers.Defs);
    }
  }

  void pushRegLanes(unsigned Reg, unsigned SubRegIdx,
                    SmallVectorImpl<RegisterMaskPair> &RegUnits) const {
    if (TargetRegisterInfo::isVirtualRegister(Reg)) {
      LaneBitmask LaneMask = SubRegIdx != 0
                             ? TRI.getSubRegIndexLaneMask(SubRegIdx)
                             : MRI.getMaxLaneMaskForVReg(Reg);
      addRegLanes(RegUnits, RegisterMaskPair(Reg, LaneMask));
    } else if (MRI.isAllocatable(Reg)) {
      for (MCRegUnitIterator Units(Reg, &TRI); Units.isValid(); ++Units)
        addRegLanes(RegUnits, RegisterMaskPair(*Units, LaneBitmask::getAll()));
    }
  }
};

} // end anonymous namespace

void RegisterOperands::collect(const MachineInstr &MI,
                               const TargetRegisterInfo &TRI,
                               const MachineRegisterInfo &MRI,
                               bool TrackLaneMasks, bool IgnoreDead) {
  RegisterOperandsCollector Collector(*this, TRI, MRI, IgnoreDead);
  if (TrackLaneMasks)
    Collector.collectInstrLanes(MI);
  else
    Collector.collectInstr(MI);
}

void RegisterOperands::detectDeadDefs(const MachineInstr &MI,
                                      const LiveIntervals &LIS) {
  SlotIndex SlotIdx = LIS.getInstructionIndex(MI);
  for (auto RI = Defs.begin(); RI != Defs.end(); /*empty*/) {
    unsigned Reg = RI->RegUnit;
    const LiveRange *LR = getLiveRange(LIS, Reg);
    if (LR != nullptr) {
      LiveQueryResult LRQ = LR->Query(SlotIdx);
      if (LRQ.isDeadDef()) {
        // LiveIntervals knows this is a dead even though it's MachineOperand is
        // not flagged as such.
        DeadDefs.push_back(*RI);
        RI = Defs.erase(RI);
        continue;
      }
    }
    ++RI;
  }
}

void RegisterOperands::adjustLaneLiveness(const LiveIntervals &LIS,
                                          const MachineRegisterInfo &MRI,
                                          SlotIndex Pos,
                                          MachineInstr *AddFlagsMI) {
  for (auto I = Defs.begin(); I != Defs.end(); ) {
    LaneBitmask LiveAfter = getLiveLanesAt(LIS, MRI, true, I->RegUnit,
                                           Pos.getDeadSlot());
    // If the def is all that is live after the instruction, then in case
    // of a subregister def we need a read-undef flag.
    unsigned RegUnit = I->RegUnit;
    if (TargetRegisterInfo::isVirtualRegister(RegUnit) &&
        AddFlagsMI != nullptr && (LiveAfter & ~I->LaneMask).none())
      AddFlagsMI->setRegisterDefReadUndef(RegUnit);

    LaneBitmask ActualDef = I->LaneMask & LiveAfter;
    if (ActualDef.none()) {
      I = Defs.erase(I);
    } else {
      I->LaneMask = ActualDef;
      ++I;
    }
  }
  for (auto I = Uses.begin(); I != Uses.end(); ) {
    LaneBitmask LiveBefore = getLiveLanesAt(LIS, MRI, true, I->RegUnit,
                                            Pos.getBaseIndex());
    LaneBitmask LaneMask = I->LaneMask & LiveBefore;
    if (LaneMask.none()) {
      I = Uses.erase(I);
    } else {
      I->LaneMask = LaneMask;
      ++I;
    }
  }
  if (AddFlagsMI != nullptr) {
    for (const RegisterMaskPair &P : DeadDefs) {
      unsigned RegUnit = P.RegUnit;
      if (!TargetRegisterInfo::isVirtualRegister(RegUnit))
        continue;
      LaneBitmask LiveAfter = getLiveLanesAt(LIS, MRI, true, RegUnit,
                                             Pos.getDeadSlot());
      if (LiveAfter.none())
        AddFlagsMI->setRegisterDefReadUndef(RegUnit);
    }
  }
}

/// Initialize an array of N PressureDiffs.
void PressureDiffs::init(unsigned N) {
  Size = N;
  if (N <= Max) {
    memset(PDiffArray, 0, N * sizeof(PressureDiff));
    return;
  }
  Max = Size;
  free(PDiffArray);
  PDiffArray = static_cast<PressureDiff*>(safe_calloc(N, sizeof(PressureDiff)));
}

void PressureDiffs::addInstruction(unsigned Idx,
                                   const RegisterOperands &RegOpers,
                                   const MachineRegisterInfo &MRI) {
  PressureDiff &PDiff = (*this)[Idx];
  assert(!PDiff.begin()->isValid() && "stale PDiff");
  for (const RegisterMaskPair &P : RegOpers.Defs)
    PDiff.addPressureChange(P.RegUnit, true, &MRI);

  for (const RegisterMaskPair &P : RegOpers.Uses)
    PDiff.addPressureChange(P.RegUnit, false, &MRI);
}

/// Add a change in pressure to the pressure diff of a given instruction.
void PressureDiff::addPressureChange(unsigned RegUnit, bool IsDec,
                                     const MachineRegisterInfo *MRI) {
  PSetIterator PSetI = MRI->getPressureSets(RegUnit);
  int Weight = IsDec ? -PSetI.getWeight() : PSetI.getWeight();
  for (; PSetI.isValid(); ++PSetI) {
    // Find an existing entry in the pressure diff for this PSet.
    PressureDiff::iterator I = nonconst_begin(), E = nonconst_end();
    for (; I != E && I->isValid(); ++I) {
      if (I->getPSet() >= *PSetI)
        break;
    }
    // If all pressure sets are more constrained, skip the remaining PSets.
    if (I == E)
      break;
    // Insert this PressureChange.
    if (!I->isValid() || I->getPSet() != *PSetI) {
      PressureChange PTmp = PressureChange(*PSetI);
      for (PressureDiff::iterator J = I; J != E && PTmp.isValid(); ++J)
        std::swap(*J, PTmp);
    }
    // Update the units for this pressure set.
    unsigned NewUnitInc = I->getUnitInc() + Weight;
    if (NewUnitInc != 0) {
      I->setUnitInc(NewUnitInc);
    } else {
      // Remove entry
      PressureDiff::iterator J;
      for (J = std::next(I); J != E && J->isValid(); ++J, ++I)
        *I = *J;
      *I = PressureChange();
    }
  }
}

/// Force liveness of registers.
void RegPressureTracker::addLiveRegs(ArrayRef<RegisterMaskPair> Regs) {
  for (const RegisterMaskPair &P : Regs) {
    LaneBitmask PrevMask = LiveRegs.insert(P);
    LaneBitmask NewMask = PrevMask | P.LaneMask;
    increaseRegPressure(P.RegUnit, PrevMask, NewMask);
  }
}

void RegPressureTracker::discoverLiveInOrOut(RegisterMaskPair Pair,
    SmallVectorImpl<RegisterMaskPair> &LiveInOrOut) {
  assert(Pair.LaneMask.any());

  unsigned RegUnit = Pair.RegUnit;
  auto I = llvm::find_if(LiveInOrOut, [RegUnit](const RegisterMaskPair &Other) {
    return Other.RegUnit == RegUnit;
  });
  LaneBitmask PrevMask;
  LaneBitmask NewMask;
  if (I == LiveInOrOut.end()) {
    PrevMask = LaneBitmask::getNone();
    NewMask = Pair.LaneMask;
    LiveInOrOut.push_back(Pair);
  } else {
    PrevMask = I->LaneMask;
    NewMask = PrevMask | Pair.LaneMask;
    I->LaneMask = NewMask;
  }
  increaseSetPressure(P.MaxSetPressure, *MRI, RegUnit, PrevMask, NewMask);
}

void RegPressureTracker::discoverLiveIn(RegisterMaskPair Pair) {
  discoverLiveInOrOut(Pair, P.LiveInRegs);
}

void RegPressureTracker::discoverLiveOut(RegisterMaskPair Pair) {
  discoverLiveInOrOut(Pair, P.LiveOutRegs);
}

void RegPressureTracker::bumpDeadDefs(ArrayRef<RegisterMaskPair> DeadDefs) {
  for (const RegisterMaskPair &P : DeadDefs) {
    unsigned Reg = P.RegUnit;
    LaneBitmask LiveMask = LiveRegs.contains(Reg);
    LaneBitmask BumpedMask = LiveMask | P.LaneMask;
    increaseRegPressure(Reg, LiveMask, BumpedMask);
  }
  for (const RegisterMaskPair &P : DeadDefs) {
    unsigned Reg = P.RegUnit;
    LaneBitmask LiveMask = LiveRegs.contains(Reg);
    LaneBitmask BumpedMask = LiveMask | P.LaneMask;
    decreaseRegPressure(Reg, BumpedMask, LiveMask);
  }
}

/// Recede across the previous instruction. If LiveUses is provided, record any
/// RegUnits that are made live by the current instruction's uses. This includes
/// registers that are both defined and used by the instruction.  If a pressure
/// difference pointer is provided record the changes is pressure caused by this
/// instruction independent of liveness.
void RegPressureTracker::recede(const RegisterOperands &RegOpers,
                                SmallVectorImpl<RegisterMaskPair> *LiveUses) {
  assert(!CurrPos->isDebugInstr());

  // Boost pressure for all dead defs together.
  bumpDeadDefs(RegOpers.DeadDefs);

  // Kill liveness at live defs.
  // TODO: consider earlyclobbers?
  for (const RegisterMaskPair &Def : RegOpers.Defs) {
    unsigned Reg = Def.RegUnit;

    LaneBitmask PreviousMask = LiveRegs.erase(Def);
    LaneBitmask NewMask = PreviousMask & ~Def.LaneMask;

    LaneBitmask LiveOut = Def.LaneMask & ~PreviousMask;
    if (LiveOut.any()) {
      discoverLiveOut(RegisterMaskPair(Reg, LiveOut));
      // Retroactively model effects on pressure of the live out lanes.
      increaseSetPressure(CurrSetPressure, *MRI, Reg, LaneBitmask::getNone(),
                          LiveOut);
      PreviousMask = LiveOut;
    }

    if (NewMask.none()) {
      // Add a 0 entry to LiveUses as a marker that the complete vreg has become
      // dead.
      if (TrackLaneMasks && LiveUses != nullptr)
        setRegZero(*LiveUses, Reg);
    }

    decreaseRegPressure(Reg, PreviousMask, NewMask);
  }

  SlotIndex SlotIdx;
  if (RequireIntervals)
    SlotIdx = LIS->getInstructionIndex(*CurrPos).getRegSlot();

  // Generate liveness for uses.
  for (const RegisterMaskPair &Use : RegOpers.Uses) {
    unsigned Reg = Use.RegUnit;
    assert(Use.LaneMask.any());
    LaneBitmask PreviousMask = LiveRegs.insert(Use);
    LaneBitmask NewMask = PreviousMask | Use.LaneMask;
    if (NewMask == PreviousMask)
      continue;

    // Did the register just become live?
    if (PreviousMask.none()) {
      if (LiveUses != nullptr) {
        if (!TrackLaneMasks) {
          addRegLanes(*LiveUses, RegisterMaskPair(Reg, NewMask));
        } else {
          auto I =
              llvm::find_if(*LiveUses, [Reg](const RegisterMaskPair Other) {
                return Other.RegUnit == Reg;
              });
          bool IsRedef = I != LiveUses->end();
          if (IsRedef) {
            // ignore re-defs here...
            assert(I->LaneMask.none());
            removeRegLanes(*LiveUses, RegisterMaskPair(Reg, NewMask));
          } else {
            addRegLanes(*LiveUses, RegisterMaskPair(Reg, NewMask));
          }
        }
      }

      // Discover live outs if this may be the first occurance of this register.
      if (RequireIntervals) {
        LaneBitmask LiveOut = getLiveThroughAt(Reg, SlotIdx);
        if (LiveOut.any())
          discoverLiveOut(RegisterMaskPair(Reg, LiveOut));
      }
    }

    increaseRegPressure(Reg, PreviousMask, NewMask);
  }
  if (TrackUntiedDefs) {
    for (const RegisterMaskPair &Def : RegOpers.Defs) {
      unsigned RegUnit = Def.RegUnit;
      if (TargetRegisterInfo::isVirtualRegister(RegUnit) &&
          (LiveRegs.contains(RegUnit) & Def.LaneMask).none())
        UntiedDefs.insert(RegUnit);
    }
  }
}

void RegPressureTracker::recedeSkipDebugValues() {
  assert(CurrPos != MBB->begin());
  if (!isBottomClosed())
    closeBottom();

  // Open the top of the region using block iterators.
  if (!RequireIntervals && isTopClosed())
    static_cast<RegionPressure&>(P).openTop(CurrPos);

  // Find the previous instruction.
  CurrPos = skipDebugInstructionsBackward(std::prev(CurrPos), MBB->begin());

  SlotIndex SlotIdx;
  if (RequireIntervals)
    SlotIdx = LIS->getInstructionIndex(*CurrPos).getRegSlot();

  // Open the top of the region using slot indexes.
  if (RequireIntervals && isTopClosed())
    static_cast<IntervalPressure&>(P).openTop(SlotIdx);
}

void RegPressureTracker::recede(SmallVectorImpl<RegisterMaskPair> *LiveUses) {
  recedeSkipDebugValues();

  const MachineInstr &MI = *CurrPos;
  RegisterOperands RegOpers;
  RegOpers.collect(MI, *TRI, *MRI, TrackLaneMasks, false);
  if (TrackLaneMasks) {
    SlotIndex SlotIdx = LIS->getInstructionIndex(*CurrPos).getRegSlot();
    RegOpers.adjustLaneLiveness(*LIS, *MRI, SlotIdx);
  } else if (RequireIntervals) {
    RegOpers.detectDeadDefs(MI, *LIS);
  }

  recede(RegOpers, LiveUses);
}

/// Advance across the current instruction.
void RegPressureTracker::advance(const RegisterOperands &RegOpers) {
  assert(!TrackUntiedDefs && "unsupported mode");
  assert(CurrPos != MBB->end());
  if (!isTopClosed())
    closeTop();

  SlotIndex SlotIdx;
  if (RequireIntervals)
    SlotIdx = getCurrSlot();

  // Open the bottom of the region using slot indexes.
  if (isBottomClosed()) {
    if (RequireIntervals)
      static_cast<IntervalPressure&>(P).openBottom(SlotIdx);
    else
      static_cast<RegionPressure&>(P).openBottom(CurrPos);
  }

  for (const RegisterMaskPair &Use : RegOpers.Uses) {
    unsigned Reg = Use.RegUnit;
    LaneBitmask LiveMask = LiveRegs.contains(Reg);
    LaneBitmask LiveIn = Use.LaneMask & ~LiveMask;
    if (LiveIn.any()) {
      discoverLiveIn(RegisterMaskPair(Reg, LiveIn));
      increaseRegPressure(Reg, LiveMask, LiveMask | LiveIn);
      LiveRegs.insert(RegisterMaskPair(Reg, LiveIn));
    }
    // Kill liveness at last uses.
    if (RequireIntervals) {
      LaneBitmask LastUseMask = getLastUsedLanes(Reg, SlotIdx);
      if (LastUseMask.any()) {
        LiveRegs.erase(RegisterMaskPair(Reg, LastUseMask));
        decreaseRegPressure(Reg, LiveMask, LiveMask & ~LastUseMask);
      }
    }
  }

  // Generate liveness for defs.
  for (const RegisterMaskPair &Def : RegOpers.Defs) {
    LaneBitmask PreviousMask = LiveRegs.insert(Def);
    LaneBitmask NewMask = PreviousMask | Def.LaneMask;
    increaseRegPressure(Def.RegUnit, PreviousMask, NewMask);
  }

  // Boost pressure for all dead defs together.
  bumpDeadDefs(RegOpers.DeadDefs);

  // Find the next instruction.
  CurrPos = skipDebugInstructionsForward(std::next(CurrPos), MBB->end());
}

void RegPressureTracker::advance() {
  const MachineInstr &MI = *CurrPos;
  RegisterOperands RegOpers;
  RegOpers.collect(MI, *TRI, *MRI, TrackLaneMasks, false);
  if (TrackLaneMasks) {
    SlotIndex SlotIdx = getCurrSlot();
    RegOpers.adjustLaneLiveness(*LIS, *MRI, SlotIdx);
  }
  advance(RegOpers);
}

/// Find the max change in excess pressure across all sets.
static void computeExcessPressureDelta(ArrayRef<unsigned> OldPressureVec,
                                       ArrayRef<unsigned> NewPressureVec,
                                       RegPressureDelta &Delta,
                                       const RegisterClassInfo *RCI,
                                       ArrayRef<unsigned> LiveThruPressureVec) {
  Delta.Excess = PressureChange();
  for (unsigned i = 0, e = OldPressureVec.size(); i < e; ++i) {
    unsigned POld = OldPressureVec[i];
    unsigned PNew = NewPressureVec[i];
    int PDiff = (int)PNew - (int)POld;
    if (!PDiff) // No change in this set in the common case.
      continue;
    // Only consider change beyond the limit.
    unsigned Limit = RCI->getRegPressureSetLimit(i);
    if (!LiveThruPressureVec.empty())
      Limit += LiveThruPressureVec[i];

    if (Limit > POld) {
      if (Limit > PNew)
        PDiff = 0;            // Under the limit
      else
        PDiff = PNew - Limit; // Just exceeded limit.
    } else if (Limit > PNew)
      PDiff = Limit - POld;   // Just obeyed limit.

    if (PDiff) {
      Delta.Excess = PressureChange(i);
      Delta.Excess.setUnitInc(PDiff);
      break;
    }
  }
}

/// Find the max change in max pressure that either surpasses a critical PSet
/// limit or exceeds the current MaxPressureLimit.
///
/// FIXME: comparing each element of the old and new MaxPressure vectors here is
/// silly. It's done now to demonstrate the concept but will go away with a
/// RegPressureTracker API change to work with pressure differences.
static void computeMaxPressureDelta(ArrayRef<unsigned> OldMaxPressureVec,
                                    ArrayRef<unsigned> NewMaxPressureVec,
                                    ArrayRef<PressureChange> CriticalPSets,
                                    ArrayRef<unsigned> MaxPressureLimit,
                                    RegPressureDelta &Delta) {
  Delta.CriticalMax = PressureChange();
  Delta.CurrentMax = PressureChange();

  unsigned CritIdx = 0, CritEnd = CriticalPSets.size();
  for (unsigned i = 0, e = OldMaxPressureVec.size(); i < e; ++i) {
    unsigned POld = OldMaxPressureVec[i];
    unsigned PNew = NewMaxPressureVec[i];
    if (PNew == POld) // No change in this set in the common case.
      continue;

    if (!Delta.CriticalMax.isValid()) {
      while (CritIdx != CritEnd && CriticalPSets[CritIdx].getPSet() < i)
        ++CritIdx;

      if (CritIdx != CritEnd && CriticalPSets[CritIdx].getPSet() == i) {
        int PDiff = (int)PNew - (int)CriticalPSets[CritIdx].getUnitInc();
        if (PDiff > 0) {
          Delta.CriticalMax = PressureChange(i);
          Delta.CriticalMax.setUnitInc(PDiff);
        }
      }
    }
    // Find the first increase above MaxPressureLimit.
    // (Ignores negative MDiff).
    if (!Delta.CurrentMax.isValid() && PNew > MaxPressureLimit[i]) {
      Delta.CurrentMax = PressureChange(i);
      Delta.CurrentMax.setUnitInc(PNew - POld);
      if (CritIdx == CritEnd || Delta.CriticalMax.isValid())
        break;
    }
  }
}

/// Record the upward impact of a single instruction on current register
/// pressure. Unlike the advance/recede pressure tracking interface, this does
/// not discover live in/outs.
///
/// This is intended for speculative queries. It leaves pressure inconsistent
/// with the current position, so must be restored by the caller.
void RegPressureTracker::bumpUpwardPressure(const MachineInstr *MI) {
  assert(!MI->isDebugInstr() && "Expect a nondebug instruction.");

  SlotIndex SlotIdx;
  if (RequireIntervals)
    SlotIdx = LIS->getInstructionIndex(*MI).getRegSlot();

  // Account for register pressure similar to RegPressureTracker::recede().
  RegisterOperands RegOpers;
  RegOpers.collect(*MI, *TRI, *MRI, TrackLaneMasks, /*IgnoreDead=*/true);
  assert(RegOpers.DeadDefs.size() == 0);
  if (TrackLaneMasks)
    RegOpers.adjustLaneLiveness(*LIS, *MRI, SlotIdx);
  else if (RequireIntervals)
    RegOpers.detectDeadDefs(*MI, *LIS);

  // Boost max pressure for all dead defs together.
  // Since CurrSetPressure and MaxSetPressure
  bumpDeadDefs(RegOpers.DeadDefs);

  // Kill liveness at live defs.
  for (const RegisterMaskPair &P : RegOpers.Defs) {
    unsigned Reg = P.RegUnit;
    LaneBitmask LiveLanes = LiveRegs.contains(Reg);
    LaneBitmask UseLanes = getRegLanes(RegOpers.Uses, Reg);
    LaneBitmask DefLanes = P.LaneMask;
    LaneBitmask LiveAfter = (LiveLanes & ~DefLanes) | UseLanes;
    decreaseRegPressure(Reg, LiveLanes, LiveAfter);
  }
  // Generate liveness for uses.
  for (const RegisterMaskPair &P : RegOpers.Uses) {
    unsigned Reg = P.RegUnit;
    LaneBitmask LiveLanes = LiveRegs.contains(Reg);
    LaneBitmask LiveAfter = LiveLanes | P.LaneMask;
    increaseRegPressure(Reg, LiveLanes, LiveAfter);
  }
}

/// Consider the pressure increase caused by traversing this instruction
/// bottom-up. Find the pressure set with the most change beyond its pressure
/// limit based on the tracker's current pressure, and return the change in
/// number of register units of that pressure set introduced by this
/// instruction.
///
/// This assumes that the current LiveOut set is sufficient.
///
/// This is expensive for an on-the-fly query because it calls
/// bumpUpwardPressure to recompute the pressure sets based on current
/// liveness. This mainly exists to verify correctness, e.g. with
/// -verify-misched. getUpwardPressureDelta is the fast version of this query
/// that uses the per-SUnit cache of the PressureDiff.
void RegPressureTracker::
getMaxUpwardPressureDelta(const MachineInstr *MI, PressureDiff *PDiff,
                          RegPressureDelta &Delta,
                          ArrayRef<PressureChange> CriticalPSets,
                          ArrayRef<unsigned> MaxPressureLimit) {
  // Snapshot Pressure.
  // FIXME: The snapshot heap space should persist. But I'm planning to
  // summarize the pressure effect so we don't need to snapshot at all.
  std::vector<unsigned> SavedPressure = CurrSetPressure;
  std::vector<unsigned> SavedMaxPressure = P.MaxSetPressure;

  bumpUpwardPressure(MI);

  computeExcessPressureDelta(SavedPressure, CurrSetPressure, Delta, RCI,
                             LiveThruPressure);
  computeMaxPressureDelta(SavedMaxPressure, P.MaxSetPressure, CriticalPSets,
                          MaxPressureLimit, Delta);
  assert(Delta.CriticalMax.getUnitInc() >= 0 &&
         Delta.CurrentMax.getUnitInc() >= 0 && "cannot decrease max pressure");

  // Restore the tracker's state.
  P.MaxSetPressure.swap(SavedMaxPressure);
  CurrSetPressure.swap(SavedPressure);

#ifndef NDEBUG
  if (!PDiff)
    return;

  // Check if the alternate algorithm yields the same result.
  RegPressureDelta Delta2;
  getUpwardPressureDelta(MI, *PDiff, Delta2, CriticalPSets, MaxPressureLimit);
  if (Delta != Delta2) {
    dbgs() << "PDiff: ";
    PDiff->dump(*TRI);
    dbgs() << "DELTA: " << *MI;
    if (Delta.Excess.isValid())
      dbgs() << "Excess1 " << TRI->getRegPressureSetName(Delta.Excess.getPSet())
             << " " << Delta.Excess.getUnitInc() << "\n";
    if (Delta.CriticalMax.isValid())
      dbgs() << "Critic1 " << TRI->getRegPressureSetName(Delta.CriticalMax.getPSet())
             << " " << Delta.CriticalMax.getUnitInc() << "\n";
    if (Delta.CurrentMax.isValid())
      dbgs() << "CurrMx1 " << TRI->getRegPressureSetName(Delta.CurrentMax.getPSet())
             << " " << Delta.CurrentMax.getUnitInc() << "\n";
    if (Delta2.Excess.isValid())
      dbgs() << "Excess2 " << TRI->getRegPressureSetName(Delta2.Excess.getPSet())
             << " " << Delta2.Excess.getUnitInc() << "\n";
    if (Delta2.CriticalMax.isValid())
      dbgs() << "Critic2 " << TRI->getRegPressureSetName(Delta2.CriticalMax.getPSet())
             << " " << Delta2.CriticalMax.getUnitInc() << "\n";
    if (Delta2.CurrentMax.isValid())
      dbgs() << "CurrMx2 " << TRI->getRegPressureSetName(Delta2.CurrentMax.getPSet())
             << " " << Delta2.CurrentMax.getUnitInc() << "\n";
    llvm_unreachable("RegP Delta Mismatch");
  }
#endif
}

/// This is the fast version of querying register pressure that does not
/// directly depend on current liveness.
///
/// @param Delta captures information needed for heuristics.
///
/// @param CriticalPSets Are the pressure sets that are known to exceed some
/// limit within the region, not necessarily at the current position.
///
/// @param MaxPressureLimit Is the max pressure within the region, not
/// necessarily at the current position.
void RegPressureTracker::
getUpwardPressureDelta(const MachineInstr *MI, /*const*/ PressureDiff &PDiff,
                       RegPressureDelta &Delta,
                       ArrayRef<PressureChange> CriticalPSets,
                       ArrayRef<unsigned> MaxPressureLimit) const {
  unsigned CritIdx = 0, CritEnd = CriticalPSets.size();
  for (PressureDiff::const_iterator
         PDiffI = PDiff.begin(), PDiffE = PDiff.end();
       PDiffI != PDiffE && PDiffI->isValid(); ++PDiffI) {

    unsigned PSetID = PDiffI->getPSet();
    unsigned Limit = RCI->getRegPressureSetLimit(PSetID);
    if (!LiveThruPressure.empty())
      Limit += LiveThruPressure[PSetID];

    unsigned POld = CurrSetPressure[PSetID];
    unsigned MOld = P.MaxSetPressure[PSetID];
    unsigned MNew = MOld;
    // Ignore DeadDefs here because they aren't captured by PressureChange.
    unsigned PNew = POld + PDiffI->getUnitInc();
    assert((PDiffI->getUnitInc() >= 0) == (PNew >= POld)
           && "PSet overflow/underflow");
    if (PNew > MOld)
      MNew = PNew;
    // Check if current pressure has exceeded the limit.
    if (!Delta.Excess.isValid()) {
      unsigned ExcessInc = 0;
      if (PNew > Limit)
        ExcessInc = POld > Limit ? PNew - POld : PNew - Limit;
      else if (POld > Limit)
        ExcessInc = Limit - POld;
      if (ExcessInc) {
        Delta.Excess = PressureChange(PSetID);
        Delta.Excess.setUnitInc(ExcessInc);
      }
    }
    // Check if max pressure has exceeded a critical pressure set max.
    if (MNew == MOld)
      continue;
    if (!Delta.CriticalMax.isValid()) {
      while (CritIdx != CritEnd && CriticalPSets[CritIdx].getPSet() < PSetID)
        ++CritIdx;

      if (CritIdx != CritEnd && CriticalPSets[CritIdx].getPSet() == PSetID) {
        int CritInc = (int)MNew - (int)CriticalPSets[CritIdx].getUnitInc();
        if (CritInc > 0 && CritInc <= std::numeric_limits<int16_t>::max()) {
          Delta.CriticalMax = PressureChange(PSetID);
          Delta.CriticalMax.setUnitInc(CritInc);
        }
      }
    }
    // Check if max pressure has exceeded the current max.
    if (!Delta.CurrentMax.isValid() && MNew > MaxPressureLimit[PSetID]) {
      Delta.CurrentMax = PressureChange(PSetID);
      Delta.CurrentMax.setUnitInc(MNew - MOld);
    }
  }
}

/// Helper to find a vreg use between two indices [PriorUseIdx, NextUseIdx).
/// The query starts with a lane bitmask which gets lanes/bits removed for every
/// use we find.
static LaneBitmask findUseBetween(unsigned Reg, LaneBitmask LastUseMask,
                                  SlotIndex PriorUseIdx, SlotIndex NextUseIdx,
                                  const MachineRegisterInfo &MRI,
                                  const LiveIntervals *LIS) {
  const TargetRegisterInfo &TRI = *MRI.getTargetRegisterInfo();
  for (const MachineOperand &MO : MRI.use_nodbg_operands(Reg)) {
    if (MO.isUndef())
      continue;
    const MachineInstr *MI = MO.getParent();
    SlotIndex InstSlot = LIS->getInstructionIndex(*MI).getRegSlot();
    if (InstSlot >= PriorUseIdx && InstSlot < NextUseIdx) {
      unsigned SubRegIdx = MO.getSubReg();
      LaneBitmask UseMask = TRI.getSubRegIndexLaneMask(SubRegIdx);
      LastUseMask &= ~UseMask;
      if (LastUseMask.none())
        return LaneBitmask::getNone();
    }
  }
  return LastUseMask;
}

LaneBitmask RegPressureTracker::getLiveLanesAt(unsigned RegUnit,
                                               SlotIndex Pos) const {
  assert(RequireIntervals);
  return getLanesWithProperty(*LIS, *MRI, TrackLaneMasks, RegUnit, Pos,
                              LaneBitmask::getAll(),
      [](const LiveRange &LR, SlotIndex Pos) {
        return LR.liveAt(Pos);
      });
}

LaneBitmask RegPressureTracker::getLastUsedLanes(unsigned RegUnit,
                                                 SlotIndex Pos) const {
  assert(RequireIntervals);
  return getLanesWithProperty(*LIS, *MRI, TrackLaneMasks, RegUnit,
                              Pos.getBaseIndex(), LaneBitmask::getNone(),
      [](const LiveRange &LR, SlotIndex Pos) {
        const LiveRange::Segment *S = LR.getSegmentContaining(Pos);
        return S != nullptr && S->end == Pos.getRegSlot();
      });
}

LaneBitmask RegPressureTracker::getLiveThroughAt(unsigned RegUnit,
                                                 SlotIndex Pos) const {
  assert(RequireIntervals);
  return getLanesWithProperty(*LIS, *MRI, TrackLaneMasks, RegUnit, Pos,
                              LaneBitmask::getNone(),
      [](const LiveRange &LR, SlotIndex Pos) {
        const LiveRange::Segment *S = LR.getSegmentContaining(Pos);
        return S != nullptr && S->start < Pos.getRegSlot(true) &&
               S->end != Pos.getDeadSlot();
      });
}

/// Record the downward impact of a single instruction on current register
/// pressure. Unlike the advance/recede pressure tracking interface, this does
/// not discover live in/outs.
///
/// This is intended for speculative queries. It leaves pressure inconsistent
/// with the current position, so must be restored by the caller.
void RegPressureTracker::bumpDownwardPressure(const MachineInstr *MI) {
  assert(!MI->isDebugInstr() && "Expect a nondebug instruction.");

  SlotIndex SlotIdx;
  if (RequireIntervals)
    SlotIdx = LIS->getInstructionIndex(*MI).getRegSlot();

  // Account for register pressure similar to RegPressureTracker::recede().
  RegisterOperands RegOpers;
  RegOpers.collect(*MI, *TRI, *MRI, TrackLaneMasks, false);
  if (TrackLaneMasks)
    RegOpers.adjustLaneLiveness(*LIS, *MRI, SlotIdx);

  if (RequireIntervals) {
    for (const RegisterMaskPair &Use : RegOpers.Uses) {
      unsigned Reg = Use.RegUnit;
      LaneBitmask LastUseMask = getLastUsedLanes(Reg, SlotIdx);
      if (LastUseMask.none())
        continue;
      // The LastUseMask is queried from the liveness information of instruction
      // which may be further down the schedule. Some lanes may actually not be
      // last uses for the current position.
      // FIXME: allow the caller to pass in the list of vreg uses that remain
      // to be bottom-scheduled to avoid searching uses at each query.
      SlotIndex CurrIdx = getCurrSlot();
      LastUseMask
        = findUseBetween(Reg, LastUseMask, CurrIdx, SlotIdx, *MRI, LIS);
      if (LastUseMask.none())
        continue;

      LaneBitmask LiveMask = LiveRegs.contains(Reg);
      LaneBitmask NewMask = LiveMask & ~LastUseMask;
      decreaseRegPressure(Reg, LiveMask, NewMask);
    }
  }

  // Generate liveness for defs.
  for (const RegisterMaskPair &Def : RegOpers.Defs) {
    unsigned Reg = Def.RegUnit;
    LaneBitmask LiveMask = LiveRegs.contains(Reg);
    LaneBitmask NewMask = LiveMask | Def.LaneMask;
    increaseRegPressure(Reg, LiveMask, NewMask);
  }

  // Boost pressure for all dead defs together.
  bumpDeadDefs(RegOpers.DeadDefs);
}

/// Consider the pressure increase caused by traversing this instruction
/// top-down. Find the register class with the most change in its pressure limit
/// based on the tracker's current pressure, and return the number of excess
/// register units of that pressure set introduced by this instruction.
///
/// This assumes that the current LiveIn set is sufficient.
///
/// This is expensive for an on-the-fly query because it calls
/// bumpDownwardPressure to recompute the pressure sets based on current
/// liveness. We don't yet have a fast version of downward pressure tracking
/// analogous to getUpwardPressureDelta.
void RegPressureTracker::
getMaxDownwardPressureDelta(const MachineInstr *MI, RegPressureDelta &Delta,
                            ArrayRef<PressureChange> CriticalPSets,
                            ArrayRef<unsigned> MaxPressureLimit) {
  // Snapshot Pressure.
  std::vector<unsigned> SavedPressure = CurrSetPressure;
  std::vector<unsigned> SavedMaxPressure = P.MaxSetPressure;

  bumpDownwardPressure(MI);

  computeExcessPressureDelta(SavedPressure, CurrSetPressure, Delta, RCI,
                             LiveThruPressure);
  computeMaxPressureDelta(SavedMaxPressure, P.MaxSetPressure, CriticalPSets,
                          MaxPressureLimit, Delta);
  assert(Delta.CriticalMax.getUnitInc() >= 0 &&
         Delta.CurrentMax.getUnitInc() >= 0 && "cannot decrease max pressure");

  // Restore the tracker's state.
  P.MaxSetPressure.swap(SavedMaxPressure);
  CurrSetPressure.swap(SavedPressure);
}

/// Get the pressure of each PSet after traversing this instruction bottom-up.
void RegPressureTracker::
getUpwardPressure(const MachineInstr *MI,
                  std::vector<unsigned> &PressureResult,
                  std::vector<unsigned> &MaxPressureResult) {
  // Snapshot pressure.
  PressureResult = CurrSetPressure;
  MaxPressureResult = P.MaxSetPressure;

  bumpUpwardPressure(MI);

  // Current pressure becomes the result. Restore current pressure.
  P.MaxSetPressure.swap(MaxPressureResult);
  CurrSetPressure.swap(PressureResult);
}

/// Get the pressure of each PSet after traversing this instruction top-down.
void RegPressureTracker::
getDownwardPressure(const MachineInstr *MI,
                    std::vector<unsigned> &PressureResult,
                    std::vector<unsigned> &MaxPressureResult) {
  // Snapshot pressure.
  PressureResult = CurrSetPressure;
  MaxPressureResult = P.MaxSetPressure;

  bumpDownwardPressure(MI);

  // Current pressure becomes the result. Restore current pressure.
  P.MaxSetPressure.swap(MaxPressureResult);
  CurrSetPressure.swap(PressureResult);
}
