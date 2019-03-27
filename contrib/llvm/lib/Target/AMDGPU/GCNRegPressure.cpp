//===- GCNRegPressure.cpp -------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "GCNRegPressure.h"
#include "AMDGPUSubtarget.h"
#include "SIRegisterInfo.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterPressure.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>

using namespace llvm;

#define DEBUG_TYPE "machine-scheduler"

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD
void llvm::printLivesAt(SlotIndex SI,
                        const LiveIntervals &LIS,
                        const MachineRegisterInfo &MRI) {
  dbgs() << "Live regs at " << SI << ": "
         << *LIS.getInstructionFromIndex(SI);
  unsigned Num = 0;
  for (unsigned I = 0, E = MRI.getNumVirtRegs(); I != E; ++I) {
    const unsigned Reg = TargetRegisterInfo::index2VirtReg(I);
    if (!LIS.hasInterval(Reg))
      continue;
    const auto &LI = LIS.getInterval(Reg);
    if (LI.hasSubRanges()) {
      bool firstTime = true;
      for (const auto &S : LI.subranges()) {
        if (!S.liveAt(SI)) continue;
        if (firstTime) {
          dbgs() << "  " << printReg(Reg, MRI.getTargetRegisterInfo())
                 << '\n';
          firstTime = false;
        }
        dbgs() << "  " << S << '\n';
        ++Num;
      }
    } else if (LI.liveAt(SI)) {
      dbgs() << "  " << LI << '\n';
      ++Num;
    }
  }
  if (!Num) dbgs() << "  <none>\n";
}

static bool isEqual(const GCNRPTracker::LiveRegSet &S1,
                    const GCNRPTracker::LiveRegSet &S2) {
  if (S1.size() != S2.size())
    return false;

  for (const auto &P : S1) {
    auto I = S2.find(P.first);
    if (I == S2.end() || I->second != P.second)
      return false;
  }
  return true;
}
#endif

///////////////////////////////////////////////////////////////////////////////
// GCNRegPressure

unsigned GCNRegPressure::getRegKind(unsigned Reg,
                                    const MachineRegisterInfo &MRI) {
  assert(TargetRegisterInfo::isVirtualRegister(Reg));
  const auto RC = MRI.getRegClass(Reg);
  auto STI = static_cast<const SIRegisterInfo*>(MRI.getTargetRegisterInfo());
  return STI->isSGPRClass(RC) ?
    (STI->getRegSizeInBits(*RC) == 32 ? SGPR32 : SGPR_TUPLE) :
    (STI->getRegSizeInBits(*RC) == 32 ? VGPR32 : VGPR_TUPLE);
}

void GCNRegPressure::inc(unsigned Reg,
                         LaneBitmask PrevMask,
                         LaneBitmask NewMask,
                         const MachineRegisterInfo &MRI) {
  if (NewMask == PrevMask)
    return;

  int Sign = 1;
  if (NewMask < PrevMask) {
    std::swap(NewMask, PrevMask);
    Sign = -1;
  }
#ifndef NDEBUG
  const auto MaxMask = MRI.getMaxLaneMaskForVReg(Reg);
#endif
  switch (auto Kind = getRegKind(Reg, MRI)) {
  case SGPR32:
  case VGPR32:
    assert(PrevMask.none() && NewMask == MaxMask);
    Value[Kind] += Sign;
    break;

  case SGPR_TUPLE:
  case VGPR_TUPLE:
    assert(NewMask < MaxMask || NewMask == MaxMask);
    assert(PrevMask < NewMask);

    Value[Kind == SGPR_TUPLE ? SGPR32 : VGPR32] +=
      Sign * (~PrevMask & NewMask).getNumLanes();

    if (PrevMask.none()) {
      assert(NewMask.any());
      Value[Kind] += Sign * MRI.getPressureSets(Reg).getWeight();
    }
    break;

  default: llvm_unreachable("Unknown register kind");
  }
}

bool GCNRegPressure::less(const GCNSubtarget &ST,
                          const GCNRegPressure& O,
                          unsigned MaxOccupancy) const {
  const auto SGPROcc = std::min(MaxOccupancy,
                                ST.getOccupancyWithNumSGPRs(getSGPRNum()));
  const auto VGPROcc = std::min(MaxOccupancy,
                                ST.getOccupancyWithNumVGPRs(getVGPRNum()));
  const auto OtherSGPROcc = std::min(MaxOccupancy,
                                ST.getOccupancyWithNumSGPRs(O.getSGPRNum()));
  const auto OtherVGPROcc = std::min(MaxOccupancy,
                                ST.getOccupancyWithNumVGPRs(O.getVGPRNum()));

  const auto Occ = std::min(SGPROcc, VGPROcc);
  const auto OtherOcc = std::min(OtherSGPROcc, OtherVGPROcc);
  if (Occ != OtherOcc)
    return Occ > OtherOcc;

  bool SGPRImportant = SGPROcc < VGPROcc;
  const bool OtherSGPRImportant = OtherSGPROcc < OtherVGPROcc;

  // if both pressures disagree on what is more important compare vgprs
  if (SGPRImportant != OtherSGPRImportant) {
    SGPRImportant = false;
  }

  // compare large regs pressure
  bool SGPRFirst = SGPRImportant;
  for (int I = 2; I > 0; --I, SGPRFirst = !SGPRFirst) {
    if (SGPRFirst) {
      auto SW = getSGPRTuplesWeight();
      auto OtherSW = O.getSGPRTuplesWeight();
      if (SW != OtherSW)
        return SW < OtherSW;
    } else {
      auto VW = getVGPRTuplesWeight();
      auto OtherVW = O.getVGPRTuplesWeight();
      if (VW != OtherVW)
        return VW < OtherVW;
    }
  }
  return SGPRImportant ? (getSGPRNum() < O.getSGPRNum()):
                         (getVGPRNum() < O.getVGPRNum());
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD
void GCNRegPressure::print(raw_ostream &OS, const GCNSubtarget *ST) const {
  OS << "VGPRs: " << getVGPRNum();
  if (ST) OS << "(O" << ST->getOccupancyWithNumVGPRs(getVGPRNum()) << ')';
  OS << ", SGPRs: " << getSGPRNum();
  if (ST) OS << "(O" << ST->getOccupancyWithNumSGPRs(getSGPRNum()) << ')';
  OS << ", LVGPR WT: " << getVGPRTuplesWeight()
     << ", LSGPR WT: " << getSGPRTuplesWeight();
  if (ST) OS << " -> Occ: " << getOccupancy(*ST);
  OS << '\n';
}
#endif

static LaneBitmask getDefRegMask(const MachineOperand &MO,
                                 const MachineRegisterInfo &MRI) {
  assert(MO.isDef() && MO.isReg() &&
    TargetRegisterInfo::isVirtualRegister(MO.getReg()));

  // We don't rely on read-undef flag because in case of tentative schedule
  // tracking it isn't set correctly yet. This works correctly however since
  // use mask has been tracked before using LIS.
  return MO.getSubReg() == 0 ?
    MRI.getMaxLaneMaskForVReg(MO.getReg()) :
    MRI.getTargetRegisterInfo()->getSubRegIndexLaneMask(MO.getSubReg());
}

static LaneBitmask getUsedRegMask(const MachineOperand &MO,
                                  const MachineRegisterInfo &MRI,
                                  const LiveIntervals &LIS) {
  assert(MO.isUse() && MO.isReg() &&
         TargetRegisterInfo::isVirtualRegister(MO.getReg()));

  if (auto SubReg = MO.getSubReg())
    return MRI.getTargetRegisterInfo()->getSubRegIndexLaneMask(SubReg);

  auto MaxMask = MRI.getMaxLaneMaskForVReg(MO.getReg());
  if (MaxMask == LaneBitmask::getLane(0)) // cannot have subregs
    return MaxMask;

  // For a tentative schedule LIS isn't updated yet but livemask should remain
  // the same on any schedule. Subreg defs can be reordered but they all must
  // dominate uses anyway.
  auto SI = LIS.getInstructionIndex(*MO.getParent()).getBaseIndex();
  return getLiveLaneMask(MO.getReg(), SI, LIS, MRI);
}

static SmallVector<RegisterMaskPair, 8>
collectVirtualRegUses(const MachineInstr &MI, const LiveIntervals &LIS,
                      const MachineRegisterInfo &MRI) {
  SmallVector<RegisterMaskPair, 8> Res;
  for (const auto &MO : MI.operands()) {
    if (!MO.isReg() || !TargetRegisterInfo::isVirtualRegister(MO.getReg()))
      continue;
    if (!MO.isUse() || !MO.readsReg())
      continue;

    auto const UsedMask = getUsedRegMask(MO, MRI, LIS);

    auto Reg = MO.getReg();
    auto I = std::find_if(Res.begin(), Res.end(), [Reg](const RegisterMaskPair &RM) {
      return RM.RegUnit == Reg;
    });
    if (I != Res.end())
      I->LaneMask |= UsedMask;
    else
      Res.push_back(RegisterMaskPair(Reg, UsedMask));
  }
  return Res;
}

///////////////////////////////////////////////////////////////////////////////
// GCNRPTracker

LaneBitmask llvm::getLiveLaneMask(unsigned Reg,
                                  SlotIndex SI,
                                  const LiveIntervals &LIS,
                                  const MachineRegisterInfo &MRI) {
  LaneBitmask LiveMask;
  const auto &LI = LIS.getInterval(Reg);
  if (LI.hasSubRanges()) {
    for (const auto &S : LI.subranges())
      if (S.liveAt(SI)) {
        LiveMask |= S.LaneMask;
        assert(LiveMask < MRI.getMaxLaneMaskForVReg(Reg) ||
               LiveMask == MRI.getMaxLaneMaskForVReg(Reg));
      }
  } else if (LI.liveAt(SI)) {
    LiveMask = MRI.getMaxLaneMaskForVReg(Reg);
  }
  return LiveMask;
}

GCNRPTracker::LiveRegSet llvm::getLiveRegs(SlotIndex SI,
                                           const LiveIntervals &LIS,
                                           const MachineRegisterInfo &MRI) {
  GCNRPTracker::LiveRegSet LiveRegs;
  for (unsigned I = 0, E = MRI.getNumVirtRegs(); I != E; ++I) {
    auto Reg = TargetRegisterInfo::index2VirtReg(I);
    if (!LIS.hasInterval(Reg))
      continue;
    auto LiveMask = getLiveLaneMask(Reg, SI, LIS, MRI);
    if (LiveMask.any())
      LiveRegs[Reg] = LiveMask;
  }
  return LiveRegs;
}

void GCNRPTracker::reset(const MachineInstr &MI,
                         const LiveRegSet *LiveRegsCopy,
                         bool After) {
  const MachineFunction &MF = *MI.getMF();
  MRI = &MF.getRegInfo();
  if (LiveRegsCopy) {
    if (&LiveRegs != LiveRegsCopy)
      LiveRegs = *LiveRegsCopy;
  } else {
    LiveRegs = After ? getLiveRegsAfter(MI, LIS)
                     : getLiveRegsBefore(MI, LIS);
  }

  MaxPressure = CurPressure = getRegPressure(*MRI, LiveRegs);
}

void GCNUpwardRPTracker::reset(const MachineInstr &MI,
                               const LiveRegSet *LiveRegsCopy) {
  GCNRPTracker::reset(MI, LiveRegsCopy, true);
}

void GCNUpwardRPTracker::recede(const MachineInstr &MI) {
  assert(MRI && "call reset first");

  LastTrackedMI = &MI;

  if (MI.isDebugInstr())
    return;

  auto const RegUses = collectVirtualRegUses(MI, LIS, *MRI);

  // calc pressure at the MI (defs + uses)
  auto AtMIPressure = CurPressure;
  for (const auto &U : RegUses) {
    auto LiveMask = LiveRegs[U.RegUnit];
    AtMIPressure.inc(U.RegUnit, LiveMask, LiveMask | U.LaneMask, *MRI);
  }
  // update max pressure
  MaxPressure = max(AtMIPressure, MaxPressure);

  for (const auto &MO : MI.defs()) {
    if (!MO.isReg() || !TargetRegisterInfo::isVirtualRegister(MO.getReg()) ||
         MO.isDead())
      continue;

    auto Reg = MO.getReg();
    auto I = LiveRegs.find(Reg);
    if (I == LiveRegs.end())
      continue;
    auto &LiveMask = I->second;
    auto PrevMask = LiveMask;
    LiveMask &= ~getDefRegMask(MO, *MRI);
    CurPressure.inc(Reg, PrevMask, LiveMask, *MRI);
    if (LiveMask.none())
      LiveRegs.erase(I);
  }
  for (const auto &U : RegUses) {
    auto &LiveMask = LiveRegs[U.RegUnit];
    auto PrevMask = LiveMask;
    LiveMask |= U.LaneMask;
    CurPressure.inc(U.RegUnit, PrevMask, LiveMask, *MRI);
  }
  assert(CurPressure == getRegPressure(*MRI, LiveRegs));
}

bool GCNDownwardRPTracker::reset(const MachineInstr &MI,
                                 const LiveRegSet *LiveRegsCopy) {
  MRI = &MI.getParent()->getParent()->getRegInfo();
  LastTrackedMI = nullptr;
  MBBEnd = MI.getParent()->end();
  NextMI = &MI;
  NextMI = skipDebugInstructionsForward(NextMI, MBBEnd);
  if (NextMI == MBBEnd)
    return false;
  GCNRPTracker::reset(*NextMI, LiveRegsCopy, false);
  return true;
}

bool GCNDownwardRPTracker::advanceBeforeNext() {
  assert(MRI && "call reset first");

  NextMI = skipDebugInstructionsForward(NextMI, MBBEnd);
  if (NextMI == MBBEnd)
    return false;

  SlotIndex SI = LIS.getInstructionIndex(*NextMI).getBaseIndex();
  assert(SI.isValid());

  // Remove dead registers or mask bits.
  for (auto &It : LiveRegs) {
    const LiveInterval &LI = LIS.getInterval(It.first);
    if (LI.hasSubRanges()) {
      for (const auto &S : LI.subranges()) {
        if (!S.liveAt(SI)) {
          auto PrevMask = It.second;
          It.second &= ~S.LaneMask;
          CurPressure.inc(It.first, PrevMask, It.second, *MRI);
        }
      }
    } else if (!LI.liveAt(SI)) {
      auto PrevMask = It.second;
      It.second = LaneBitmask::getNone();
      CurPressure.inc(It.first, PrevMask, It.second, *MRI);
    }
    if (It.second.none())
      LiveRegs.erase(It.first);
  }

  MaxPressure = max(MaxPressure, CurPressure);

  return true;
}

void GCNDownwardRPTracker::advanceToNext() {
  LastTrackedMI = &*NextMI++;

  // Add new registers or mask bits.
  for (const auto &MO : LastTrackedMI->defs()) {
    if (!MO.isReg())
      continue;
    unsigned Reg = MO.getReg();
    if (!TargetRegisterInfo::isVirtualRegister(Reg))
      continue;
    auto &LiveMask = LiveRegs[Reg];
    auto PrevMask = LiveMask;
    LiveMask |= getDefRegMask(MO, *MRI);
    CurPressure.inc(Reg, PrevMask, LiveMask, *MRI);
  }

  MaxPressure = max(MaxPressure, CurPressure);
}

bool GCNDownwardRPTracker::advance() {
  // If we have just called reset live set is actual.
  if ((NextMI == MBBEnd) || (LastTrackedMI && !advanceBeforeNext()))
    return false;
  advanceToNext();
  return true;
}

bool GCNDownwardRPTracker::advance(MachineBasicBlock::const_iterator End) {
  while (NextMI != End)
    if (!advance()) return false;
  return true;
}

bool GCNDownwardRPTracker::advance(MachineBasicBlock::const_iterator Begin,
                                   MachineBasicBlock::const_iterator End,
                                   const LiveRegSet *LiveRegsCopy) {
  reset(*Begin, LiveRegsCopy);
  return advance(End);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD
static void reportMismatch(const GCNRPTracker::LiveRegSet &LISLR,
                           const GCNRPTracker::LiveRegSet &TrackedLR,
                           const TargetRegisterInfo *TRI) {
  for (auto const &P : TrackedLR) {
    auto I = LISLR.find(P.first);
    if (I == LISLR.end()) {
      dbgs() << "  " << printReg(P.first, TRI)
             << ":L" << PrintLaneMask(P.second)
             << " isn't found in LIS reported set\n";
    }
    else if (I->second != P.second) {
      dbgs() << "  " << printReg(P.first, TRI)
        << " masks doesn't match: LIS reported "
        << PrintLaneMask(I->second)
        << ", tracked "
        << PrintLaneMask(P.second)
        << '\n';
    }
  }
  for (auto const &P : LISLR) {
    auto I = TrackedLR.find(P.first);
    if (I == TrackedLR.end()) {
      dbgs() << "  " << printReg(P.first, TRI)
             << ":L" << PrintLaneMask(P.second)
             << " isn't found in tracked set\n";
    }
  }
}

bool GCNUpwardRPTracker::isValid() const {
  const auto &SI = LIS.getInstructionIndex(*LastTrackedMI).getBaseIndex();
  const auto LISLR = llvm::getLiveRegs(SI, LIS, *MRI);
  const auto &TrackedLR = LiveRegs;

  if (!isEqual(LISLR, TrackedLR)) {
    dbgs() << "\nGCNUpwardRPTracker error: Tracked and"
              " LIS reported livesets mismatch:\n";
    printLivesAt(SI, LIS, *MRI);
    reportMismatch(LISLR, TrackedLR, MRI->getTargetRegisterInfo());
    return false;
  }

  auto LISPressure = getRegPressure(*MRI, LISLR);
  if (LISPressure != CurPressure) {
    dbgs() << "GCNUpwardRPTracker error: Pressure sets different\nTracked: ";
    CurPressure.print(dbgs());
    dbgs() << "LIS rpt: ";
    LISPressure.print(dbgs());
    return false;
  }
  return true;
}

void GCNRPTracker::printLiveRegs(raw_ostream &OS, const LiveRegSet& LiveRegs,
                                 const MachineRegisterInfo &MRI) {
  const TargetRegisterInfo *TRI = MRI.getTargetRegisterInfo();
  for (unsigned I = 0, E = MRI.getNumVirtRegs(); I != E; ++I) {
    unsigned Reg = TargetRegisterInfo::index2VirtReg(I);
    auto It = LiveRegs.find(Reg);
    if (It != LiveRegs.end() && It->second.any())
      OS << ' ' << printVRegOrUnit(Reg, TRI) << ':'
         << PrintLaneMask(It->second);
  }
  OS << '\n';
}
#endif
