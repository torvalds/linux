//===- RDFRegisters.cpp ---------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "RDFRegisters.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <set>
#include <utility>

using namespace llvm;
using namespace rdf;

PhysicalRegisterInfo::PhysicalRegisterInfo(const TargetRegisterInfo &tri,
      const MachineFunction &mf)
    : TRI(tri) {
  RegInfos.resize(TRI.getNumRegs());

  BitVector BadRC(TRI.getNumRegs());
  for (const TargetRegisterClass *RC : TRI.regclasses()) {
    for (MCPhysReg R : *RC) {
      RegInfo &RI = RegInfos[R];
      if (RI.RegClass != nullptr && !BadRC[R]) {
        if (RC->LaneMask != RI.RegClass->LaneMask) {
          BadRC.set(R);
          RI.RegClass = nullptr;
        }
      } else
        RI.RegClass = RC;
    }
  }

  UnitInfos.resize(TRI.getNumRegUnits());

  for (uint32_t U = 0, NU = TRI.getNumRegUnits(); U != NU; ++U) {
    if (UnitInfos[U].Reg != 0)
      continue;
    MCRegUnitRootIterator R(U, &TRI);
    assert(R.isValid());
    RegisterId F = *R;
    ++R;
    if (R.isValid()) {
      UnitInfos[U].Mask = LaneBitmask::getAll();
      UnitInfos[U].Reg = F;
    } else {
      for (MCRegUnitMaskIterator I(F, &TRI); I.isValid(); ++I) {
        std::pair<uint32_t,LaneBitmask> P = *I;
        UnitInfo &UI = UnitInfos[P.first];
        UI.Reg = F;
        if (P.second.any()) {
          UI.Mask = P.second;
        } else {
          if (const TargetRegisterClass *RC = RegInfos[F].RegClass)
            UI.Mask = RC->LaneMask;
          else
            UI.Mask = LaneBitmask::getAll();
        }
      }
    }
  }

  for (const uint32_t *RM : TRI.getRegMasks())
    RegMasks.insert(RM);
  for (const MachineBasicBlock &B : mf)
    for (const MachineInstr &In : B)
      for (const MachineOperand &Op : In.operands())
        if (Op.isRegMask())
          RegMasks.insert(Op.getRegMask());

  MaskInfos.resize(RegMasks.size()+1);
  for (uint32_t M = 1, NM = RegMasks.size(); M <= NM; ++M) {
    BitVector PU(TRI.getNumRegUnits());
    const uint32_t *MB = RegMasks.get(M);
    for (unsigned i = 1, e = TRI.getNumRegs(); i != e; ++i) {
      if (!(MB[i/32] & (1u << (i%32))))
        continue;
      for (MCRegUnitIterator U(i, &TRI); U.isValid(); ++U)
        PU.set(*U);
    }
    MaskInfos[M].Units = PU.flip();
  }
}

RegisterRef PhysicalRegisterInfo::normalize(RegisterRef RR) const {
  return RR;
}

std::set<RegisterId> PhysicalRegisterInfo::getAliasSet(RegisterId Reg) const {
  // Do not include RR in the alias set.
  std::set<RegisterId> AS;
  assert(isRegMaskId(Reg) || TargetRegisterInfo::isPhysicalRegister(Reg));
  if (isRegMaskId(Reg)) {
    // XXX SLOW
    const uint32_t *MB = getRegMaskBits(Reg);
    for (unsigned i = 1, e = TRI.getNumRegs(); i != e; ++i) {
      if (MB[i/32] & (1u << (i%32)))
        continue;
      AS.insert(i);
    }
    for (const uint32_t *RM : RegMasks) {
      RegisterId MI = getRegMaskId(RM);
      if (MI != Reg && aliasMM(RegisterRef(Reg), RegisterRef(MI)))
        AS.insert(MI);
    }
    return AS;
  }

  for (MCRegAliasIterator AI(Reg, &TRI, false); AI.isValid(); ++AI)
    AS.insert(*AI);
  for (const uint32_t *RM : RegMasks) {
    RegisterId MI = getRegMaskId(RM);
    if (aliasRM(RegisterRef(Reg), RegisterRef(MI)))
      AS.insert(MI);
  }
  return AS;
}

bool PhysicalRegisterInfo::aliasRR(RegisterRef RA, RegisterRef RB) const {
  assert(TargetRegisterInfo::isPhysicalRegister(RA.Reg));
  assert(TargetRegisterInfo::isPhysicalRegister(RB.Reg));

  MCRegUnitMaskIterator UMA(RA.Reg, &TRI);
  MCRegUnitMaskIterator UMB(RB.Reg, &TRI);
  // Reg units are returned in the numerical order.
  while (UMA.isValid() && UMB.isValid()) {
    // Skip units that are masked off in RA.
    std::pair<RegisterId,LaneBitmask> PA = *UMA;
    if (PA.second.any() && (PA.second & RA.Mask).none()) {
      ++UMA;
      continue;
    }
    // Skip units that are masked off in RB.
    std::pair<RegisterId,LaneBitmask> PB = *UMB;
    if (PB.second.any() && (PB.second & RB.Mask).none()) {
      ++UMB;
      continue;
    }

    if (PA.first == PB.first)
      return true;
    if (PA.first < PB.first)
      ++UMA;
    else if (PB.first < PA.first)
      ++UMB;
  }
  return false;
}

bool PhysicalRegisterInfo::aliasRM(RegisterRef RR, RegisterRef RM) const {
  assert(TargetRegisterInfo::isPhysicalRegister(RR.Reg) && isRegMaskId(RM.Reg));
  const uint32_t *MB = getRegMaskBits(RM.Reg);
  bool Preserved = MB[RR.Reg/32] & (1u << (RR.Reg%32));
  // If the lane mask information is "full", e.g. when the given lane mask
  // is a superset of the lane mask from the register class, check the regmask
  // bit directly.
  if (RR.Mask == LaneBitmask::getAll())
    return !Preserved;
  const TargetRegisterClass *RC = RegInfos[RR.Reg].RegClass;
  if (RC != nullptr && (RR.Mask & RC->LaneMask) == RC->LaneMask)
    return !Preserved;

  // Otherwise, check all subregisters whose lane mask overlaps the given
  // mask. For each such register, if it is preserved by the regmask, then
  // clear the corresponding bits in the given mask. If at the end, all
  // bits have been cleared, the register does not alias the regmask (i.e.
  // is it preserved by it).
  LaneBitmask M = RR.Mask;
  for (MCSubRegIndexIterator SI(RR.Reg, &TRI); SI.isValid(); ++SI) {
    LaneBitmask SM = TRI.getSubRegIndexLaneMask(SI.getSubRegIndex());
    if ((SM & RR.Mask).none())
      continue;
    unsigned SR = SI.getSubReg();
    if (!(MB[SR/32] & (1u << (SR%32))))
      continue;
    // The subregister SR is preserved.
    M &= ~SM;
    if (M.none())
      return false;
  }

  return true;
}

bool PhysicalRegisterInfo::aliasMM(RegisterRef RM, RegisterRef RN) const {
  assert(isRegMaskId(RM.Reg) && isRegMaskId(RN.Reg));
  unsigned NumRegs = TRI.getNumRegs();
  const uint32_t *BM = getRegMaskBits(RM.Reg);
  const uint32_t *BN = getRegMaskBits(RN.Reg);

  for (unsigned w = 0, nw = NumRegs/32; w != nw; ++w) {
    // Intersect the negations of both words. Disregard reg=0,
    // i.e. 0th bit in the 0th word.
    uint32_t C = ~BM[w] & ~BN[w];
    if (w == 0)
      C &= ~1;
    if (C)
      return true;
  }

  // Check the remaining registers in the last word.
  unsigned TailRegs = NumRegs % 32;
  if (TailRegs == 0)
    return false;
  unsigned TW = NumRegs / 32;
  uint32_t TailMask = (1u << TailRegs) - 1;
  if (~BM[TW] & ~BN[TW] & TailMask)
    return true;

  return false;
}

RegisterRef PhysicalRegisterInfo::mapTo(RegisterRef RR, unsigned R) const {
  if (RR.Reg == R)
    return RR;
  if (unsigned Idx = TRI.getSubRegIndex(R, RR.Reg))
    return RegisterRef(R, TRI.composeSubRegIndexLaneMask(Idx, RR.Mask));
  if (unsigned Idx = TRI.getSubRegIndex(RR.Reg, R)) {
    const RegInfo &RI = RegInfos[R];
    LaneBitmask RCM = RI.RegClass ? RI.RegClass->LaneMask
                                  : LaneBitmask::getAll();
    LaneBitmask M = TRI.reverseComposeSubRegIndexLaneMask(Idx, RR.Mask);
    return RegisterRef(R, M & RCM);
  }
  llvm_unreachable("Invalid arguments: unrelated registers?");
}

bool RegisterAggr::hasAliasOf(RegisterRef RR) const {
  if (PhysicalRegisterInfo::isRegMaskId(RR.Reg))
    return Units.anyCommon(PRI.getMaskUnits(RR.Reg));

  for (MCRegUnitMaskIterator U(RR.Reg, &PRI.getTRI()); U.isValid(); ++U) {
    std::pair<uint32_t,LaneBitmask> P = *U;
    if (P.second.none() || (P.second & RR.Mask).any())
      if (Units.test(P.first))
        return true;
  }
  return false;
}

bool RegisterAggr::hasCoverOf(RegisterRef RR) const {
  if (PhysicalRegisterInfo::isRegMaskId(RR.Reg)) {
    BitVector T(PRI.getMaskUnits(RR.Reg));
    return T.reset(Units).none();
  }

  for (MCRegUnitMaskIterator U(RR.Reg, &PRI.getTRI()); U.isValid(); ++U) {
    std::pair<uint32_t,LaneBitmask> P = *U;
    if (P.second.none() || (P.second & RR.Mask).any())
      if (!Units.test(P.first))
        return false;
  }
  return true;
}

RegisterAggr &RegisterAggr::insert(RegisterRef RR) {
  if (PhysicalRegisterInfo::isRegMaskId(RR.Reg)) {
    Units |= PRI.getMaskUnits(RR.Reg);
    return *this;
  }

  for (MCRegUnitMaskIterator U(RR.Reg, &PRI.getTRI()); U.isValid(); ++U) {
    std::pair<uint32_t,LaneBitmask> P = *U;
    if (P.second.none() || (P.second & RR.Mask).any())
      Units.set(P.first);
  }
  return *this;
}

RegisterAggr &RegisterAggr::insert(const RegisterAggr &RG) {
  Units |= RG.Units;
  return *this;
}

RegisterAggr &RegisterAggr::intersect(RegisterRef RR) {
  return intersect(RegisterAggr(PRI).insert(RR));
}

RegisterAggr &RegisterAggr::intersect(const RegisterAggr &RG) {
  Units &= RG.Units;
  return *this;
}

RegisterAggr &RegisterAggr::clear(RegisterRef RR) {
  return clear(RegisterAggr(PRI).insert(RR));
}

RegisterAggr &RegisterAggr::clear(const RegisterAggr &RG) {
  Units.reset(RG.Units);
  return *this;
}

RegisterRef RegisterAggr::intersectWith(RegisterRef RR) const {
  RegisterAggr T(PRI);
  T.insert(RR).intersect(*this);
  if (T.empty())
    return RegisterRef();
  RegisterRef NR = T.makeRegRef();
  assert(NR);
  return NR;
}

RegisterRef RegisterAggr::clearIn(RegisterRef RR) const {
  return RegisterAggr(PRI).insert(RR).clear(*this).makeRegRef();
}

RegisterRef RegisterAggr::makeRegRef() const {
  int U = Units.find_first();
  if (U < 0)
    return RegisterRef();

  auto AliasedRegs = [this] (uint32_t Unit, BitVector &Regs) {
    for (MCRegUnitRootIterator R(Unit, &PRI.getTRI()); R.isValid(); ++R)
      for (MCSuperRegIterator S(*R, &PRI.getTRI(), true); S.isValid(); ++S)
        Regs.set(*S);
  };

  // Find the set of all registers that are aliased to all the units
  // in this aggregate.

  // Get all the registers aliased to the first unit in the bit vector.
  BitVector Regs(PRI.getTRI().getNumRegs());
  AliasedRegs(U, Regs);
  U = Units.find_next(U);

  // For each other unit, intersect it with the set of all registers
  // aliased that unit.
  while (U >= 0) {
    BitVector AR(PRI.getTRI().getNumRegs());
    AliasedRegs(U, AR);
    Regs &= AR;
    U = Units.find_next(U);
  }

  // If there is at least one register remaining, pick the first one,
  // and consolidate the masks of all of its units contained in this
  // aggregate.

  int F = Regs.find_first();
  if (F <= 0)
    return RegisterRef();

  LaneBitmask M;
  for (MCRegUnitMaskIterator I(F, &PRI.getTRI()); I.isValid(); ++I) {
    std::pair<uint32_t,LaneBitmask> P = *I;
    if (Units.test(P.first))
      M |= P.second.none() ? LaneBitmask::getAll() : P.second;
  }
  return RegisterRef(F, M);
}

void RegisterAggr::print(raw_ostream &OS) const {
  OS << '{';
  for (int U = Units.find_first(); U >= 0; U = Units.find_next(U))
    OS << ' ' << printRegUnit(U, &PRI.getTRI());
  OS << " }";
}

RegisterAggr::rr_iterator::rr_iterator(const RegisterAggr &RG,
      bool End)
    : Owner(&RG) {
  for (int U = RG.Units.find_first(); U >= 0; U = RG.Units.find_next(U)) {
    RegisterRef R = RG.PRI.getRefForUnit(U);
    Masks[R.Reg] |= R.Mask;
  }
  Pos = End ? Masks.end() : Masks.begin();
  Index = End ? Masks.size() : 0;
}
