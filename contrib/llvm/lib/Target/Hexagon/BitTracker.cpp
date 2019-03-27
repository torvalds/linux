//===- BitTracker.cpp -----------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// SSA-based bit propagation.
//
// The purpose of this code is, for a given virtual register, to provide
// information about the value of each bit in the register. The values
// of bits are represented by the class BitValue, and take one of four
// cases: 0, 1, "ref" and "bottom". The 0 and 1 are rather clear, the
// "ref" value means that the bit is a copy of another bit (which itself
// cannot be a copy of yet another bit---such chains are not allowed).
// A "ref" value is associated with a BitRef structure, which indicates
// which virtual register, and which bit in that register is the origin
// of the value. For example, given an instruction
//   %2 = ASL %1, 1
// assuming that nothing is known about bits of %1, bit 1 of %2
// will be a "ref" to (%1, 0). If there is a subsequent instruction
//   %3 = ASL %2, 2
// then bit 3 of %3 will be a "ref" to (%1, 0) as well.
// The "bottom" case means that the bit's value cannot be determined,
// and that this virtual register actually defines it. The "bottom" case
// is discussed in detail in BitTracker.h. In fact, "bottom" is a "ref
// to self", so for the %1 above, the bit 0 of it will be a "ref" to
// (%1, 0), bit 1 will be a "ref" to (%1, 1), etc.
//
// The tracker implements the Wegman-Zadeck algorithm, originally developed
// for SSA-based constant propagation. Each register is represented as
// a sequence of bits, with the convention that bit 0 is the least signi-
// ficant bit. Each bit is propagated individually. The class RegisterCell
// implements the register's representation, and is also the subject of
// the lattice operations in the tracker.
//
// The intended usage of the bit tracker is to create a target-specific
// machine instruction evaluator, pass the evaluator to the BitTracker
// object, and run the tracker. The tracker will then collect the bit
// value information for a given machine function. After that, it can be
// queried for the cells for each virtual register.
// Sample code:
//   const TargetSpecificEvaluator TSE(TRI, MRI);
//   BitTracker BT(TSE, MF);
//   BT.run();
//   ...
//   unsigned Reg = interestingRegister();
//   RegisterCell RC = BT.get(Reg);
//   if (RC[3].is(1))
//      Reg0bit3 = 1;
//
// The code below is intended to be fully target-independent.

#include "BitTracker.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <iterator>

using namespace llvm;

using BT = BitTracker;

namespace {

  // Local trickery to pretty print a register (without the whole "%number"
  // business).
  struct printv {
    printv(unsigned r) : R(r) {}

    unsigned R;
  };

  raw_ostream &operator<< (raw_ostream &OS, const printv &PV) {
    if (PV.R)
      OS << 'v' << TargetRegisterInfo::virtReg2Index(PV.R);
    else
      OS << 's';
    return OS;
  }

} // end anonymous namespace

namespace llvm {

  raw_ostream &operator<<(raw_ostream &OS, const BT::BitValue &BV) {
    switch (BV.Type) {
      case BT::BitValue::Top:
        OS << 'T';
        break;
      case BT::BitValue::Zero:
        OS << '0';
        break;
      case BT::BitValue::One:
        OS << '1';
        break;
      case BT::BitValue::Ref:
        OS << printv(BV.RefI.Reg) << '[' << BV.RefI.Pos << ']';
        break;
    }
    return OS;
  }

  raw_ostream &operator<<(raw_ostream &OS, const BT::RegisterCell &RC) {
    unsigned n = RC.Bits.size();
    OS << "{ w:" << n;
    // Instead of printing each bit value individually, try to group them
    // into logical segments, such as sequences of 0 or 1 bits or references
    // to consecutive bits (e.g. "bits 3-5 are same as bits 7-9 of reg xyz").
    // "Start" will be the index of the beginning of the most recent segment.
    unsigned Start = 0;
    bool SeqRef = false;    // A sequence of refs to consecutive bits.
    bool ConstRef = false;  // A sequence of refs to the same bit.

    for (unsigned i = 1, n = RC.Bits.size(); i < n; ++i) {
      const BT::BitValue &V = RC[i];
      const BT::BitValue &SV = RC[Start];
      bool IsRef = (V.Type == BT::BitValue::Ref);
      // If the current value is the same as Start, skip to the next one.
      if (!IsRef && V == SV)
        continue;
      if (IsRef && SV.Type == BT::BitValue::Ref && V.RefI.Reg == SV.RefI.Reg) {
        if (Start+1 == i) {
          SeqRef = (V.RefI.Pos == SV.RefI.Pos+1);
          ConstRef = (V.RefI.Pos == SV.RefI.Pos);
        }
        if (SeqRef && V.RefI.Pos == SV.RefI.Pos+(i-Start))
          continue;
        if (ConstRef && V.RefI.Pos == SV.RefI.Pos)
          continue;
      }

      // The current value is different. Print the previous one and reset
      // the Start.
      OS << " [" << Start;
      unsigned Count = i - Start;
      if (Count == 1) {
        OS << "]:" << SV;
      } else {
        OS << '-' << i-1 << "]:";
        if (SV.Type == BT::BitValue::Ref && SeqRef)
          OS << printv(SV.RefI.Reg) << '[' << SV.RefI.Pos << '-'
             << SV.RefI.Pos+(Count-1) << ']';
        else
          OS << SV;
      }
      Start = i;
      SeqRef = ConstRef = false;
    }

    OS << " [" << Start;
    unsigned Count = n - Start;
    if (n-Start == 1) {
      OS << "]:" << RC[Start];
    } else {
      OS << '-' << n-1 << "]:";
      const BT::BitValue &SV = RC[Start];
      if (SV.Type == BT::BitValue::Ref && SeqRef)
        OS << printv(SV.RefI.Reg) << '[' << SV.RefI.Pos << '-'
           << SV.RefI.Pos+(Count-1) << ']';
      else
        OS << SV;
    }
    OS << " }";

    return OS;
  }

} // end namespace llvm

void BitTracker::print_cells(raw_ostream &OS) const {
  for (const std::pair<unsigned, RegisterCell> P : Map)
    dbgs() << printReg(P.first, &ME.TRI) << " -> " << P.second << "\n";
}

BitTracker::BitTracker(const MachineEvaluator &E, MachineFunction &F)
    : ME(E), MF(F), MRI(F.getRegInfo()), Map(*new CellMapType), Trace(false) {
}

BitTracker::~BitTracker() {
  delete &Map;
}

// If we were allowed to update a cell for a part of a register, the meet
// operation would need to be parametrized by the register number and the
// exact part of the register, so that the computer BitRefs correspond to
// the actual bits of the "self" register.
// While this cannot happen in the current implementation, I'm not sure
// if this should be ruled out in the future.
bool BT::RegisterCell::meet(const RegisterCell &RC, unsigned SelfR) {
  // An example when "meet" can be invoked with SelfR == 0 is a phi node
  // with a physical register as an operand.
  assert(SelfR == 0 || TargetRegisterInfo::isVirtualRegister(SelfR));
  bool Changed = false;
  for (uint16_t i = 0, n = Bits.size(); i < n; ++i) {
    const BitValue &RCV = RC[i];
    Changed |= Bits[i].meet(RCV, BitRef(SelfR, i));
  }
  return Changed;
}

// Insert the entire cell RC into the current cell at position given by M.
BT::RegisterCell &BT::RegisterCell::insert(const BT::RegisterCell &RC,
      const BitMask &M) {
  uint16_t B = M.first(), E = M.last(), W = width();
  // Sanity: M must be a valid mask for *this.
  assert(B < W && E < W);
  // Sanity: the masked part of *this must have the same number of bits
  // as the source.
  assert(B > E || E-B+1 == RC.width());      // B <= E  =>  E-B+1 = |RC|.
  assert(B <= E || E+(W-B)+1 == RC.width()); // E < B   =>  E+(W-B)+1 = |RC|.
  if (B <= E) {
    for (uint16_t i = 0; i <= E-B; ++i)
      Bits[i+B] = RC[i];
  } else {
    for (uint16_t i = 0; i < W-B; ++i)
      Bits[i+B] = RC[i];
    for (uint16_t i = 0; i <= E; ++i)
      Bits[i] = RC[i+(W-B)];
  }
  return *this;
}

BT::RegisterCell BT::RegisterCell::extract(const BitMask &M) const {
  uint16_t B = M.first(), E = M.last(), W = width();
  assert(B < W && E < W);
  if (B <= E) {
    RegisterCell RC(E-B+1);
    for (uint16_t i = B; i <= E; ++i)
      RC.Bits[i-B] = Bits[i];
    return RC;
  }

  RegisterCell RC(E+(W-B)+1);
  for (uint16_t i = 0; i < W-B; ++i)
    RC.Bits[i] = Bits[i+B];
  for (uint16_t i = 0; i <= E; ++i)
    RC.Bits[i+(W-B)] = Bits[i];
  return RC;
}

BT::RegisterCell &BT::RegisterCell::rol(uint16_t Sh) {
  // Rotate left (i.e. towards increasing bit indices).
  // Swap the two parts:  [0..W-Sh-1] [W-Sh..W-1]
  uint16_t W = width();
  Sh = Sh % W;
  if (Sh == 0)
    return *this;

  RegisterCell Tmp(W-Sh);
  // Tmp = [0..W-Sh-1].
  for (uint16_t i = 0; i < W-Sh; ++i)
    Tmp[i] = Bits[i];
  // Shift [W-Sh..W-1] to [0..Sh-1].
  for (uint16_t i = 0; i < Sh; ++i)
    Bits[i] = Bits[W-Sh+i];
  // Copy Tmp to [Sh..W-1].
  for (uint16_t i = 0; i < W-Sh; ++i)
    Bits[i+Sh] = Tmp.Bits[i];
  return *this;
}

BT::RegisterCell &BT::RegisterCell::fill(uint16_t B, uint16_t E,
      const BitValue &V) {
  assert(B <= E);
  while (B < E)
    Bits[B++] = V;
  return *this;
}

BT::RegisterCell &BT::RegisterCell::cat(const RegisterCell &RC) {
  // Append the cell given as the argument to the "this" cell.
  // Bit 0 of RC becomes bit W of the result, where W is this->width().
  uint16_t W = width(), WRC = RC.width();
  Bits.resize(W+WRC);
  for (uint16_t i = 0; i < WRC; ++i)
    Bits[i+W] = RC.Bits[i];
  return *this;
}

uint16_t BT::RegisterCell::ct(bool B) const {
  uint16_t W = width();
  uint16_t C = 0;
  BitValue V = B;
  while (C < W && Bits[C] == V)
    C++;
  return C;
}

uint16_t BT::RegisterCell::cl(bool B) const {
  uint16_t W = width();
  uint16_t C = 0;
  BitValue V = B;
  while (C < W && Bits[W-(C+1)] == V)
    C++;
  return C;
}

bool BT::RegisterCell::operator== (const RegisterCell &RC) const {
  uint16_t W = Bits.size();
  if (RC.Bits.size() != W)
    return false;
  for (uint16_t i = 0; i < W; ++i)
    if (Bits[i] != RC[i])
      return false;
  return true;
}

BT::RegisterCell &BT::RegisterCell::regify(unsigned R) {
  for (unsigned i = 0, n = width(); i < n; ++i) {
    const BitValue &V = Bits[i];
    if (V.Type == BitValue::Ref && V.RefI.Reg == 0)
      Bits[i].RefI = BitRef(R, i);
  }
  return *this;
}

uint16_t BT::MachineEvaluator::getRegBitWidth(const RegisterRef &RR) const {
  // The general problem is with finding a register class that corresponds
  // to a given reference reg:sub. There can be several such classes, and
  // since we only care about the register size, it does not matter which
  // such class we would find.
  // The easiest way to accomplish what we want is to
  // 1. find a physical register PhysR from the same class as RR.Reg,
  // 2. find a physical register PhysS that corresponds to PhysR:RR.Sub,
  // 3. find a register class that contains PhysS.
  if (TargetRegisterInfo::isVirtualRegister(RR.Reg)) {
    const auto &VC = composeWithSubRegIndex(*MRI.getRegClass(RR.Reg), RR.Sub);
    return TRI.getRegSizeInBits(VC);
  }
  assert(TargetRegisterInfo::isPhysicalRegister(RR.Reg));
  unsigned PhysR = (RR.Sub == 0) ? RR.Reg : TRI.getSubReg(RR.Reg, RR.Sub);
  return getPhysRegBitWidth(PhysR);
}

BT::RegisterCell BT::MachineEvaluator::getCell(const RegisterRef &RR,
      const CellMapType &M) const {
  uint16_t BW = getRegBitWidth(RR);

  // Physical registers are assumed to be present in the map with an unknown
  // value. Don't actually insert anything in the map, just return the cell.
  if (TargetRegisterInfo::isPhysicalRegister(RR.Reg))
    return RegisterCell::self(0, BW);

  assert(TargetRegisterInfo::isVirtualRegister(RR.Reg));
  // For virtual registers that belong to a class that is not tracked,
  // generate an "unknown" value as well.
  const TargetRegisterClass *C = MRI.getRegClass(RR.Reg);
  if (!track(C))
    return RegisterCell::self(0, BW);

  CellMapType::const_iterator F = M.find(RR.Reg);
  if (F != M.end()) {
    if (!RR.Sub)
      return F->second;
    BitMask M = mask(RR.Reg, RR.Sub);
    return F->second.extract(M);
  }
  // If not found, create a "top" entry, but do not insert it in the map.
  return RegisterCell::top(BW);
}

void BT::MachineEvaluator::putCell(const RegisterRef &RR, RegisterCell RC,
      CellMapType &M) const {
  // While updating the cell map can be done in a meaningful way for
  // a part of a register, it makes little sense to implement it as the
  // SSA representation would never contain such "partial definitions".
  if (!TargetRegisterInfo::isVirtualRegister(RR.Reg))
    return;
  assert(RR.Sub == 0 && "Unexpected sub-register in definition");
  // Eliminate all ref-to-reg-0 bit values: replace them with "self".
  M[RR.Reg] = RC.regify(RR.Reg);
}

// Check if the cell represents a compile-time integer value.
bool BT::MachineEvaluator::isInt(const RegisterCell &A) const {
  uint16_t W = A.width();
  for (uint16_t i = 0; i < W; ++i)
    if (!A[i].is(0) && !A[i].is(1))
      return false;
  return true;
}

// Convert a cell to the integer value. The result must fit in uint64_t.
uint64_t BT::MachineEvaluator::toInt(const RegisterCell &A) const {
  assert(isInt(A));
  uint64_t Val = 0;
  uint16_t W = A.width();
  for (uint16_t i = 0; i < W; ++i) {
    Val <<= 1;
    Val |= A[i].is(1);
  }
  return Val;
}

// Evaluator helper functions. These implement some common operation on
// register cells that can be used to implement target-specific instructions
// in a target-specific evaluator.

BT::RegisterCell BT::MachineEvaluator::eIMM(int64_t V, uint16_t W) const {
  RegisterCell Res(W);
  // For bits beyond the 63rd, this will generate the sign bit of V.
  for (uint16_t i = 0; i < W; ++i) {
    Res[i] = BitValue(V & 1);
    V >>= 1;
  }
  return Res;
}

BT::RegisterCell BT::MachineEvaluator::eIMM(const ConstantInt *CI) const {
  const APInt &A = CI->getValue();
  uint16_t BW = A.getBitWidth();
  assert((unsigned)BW == A.getBitWidth() && "BitWidth overflow");
  RegisterCell Res(BW);
  for (uint16_t i = 0; i < BW; ++i)
    Res[i] = A[i];
  return Res;
}

BT::RegisterCell BT::MachineEvaluator::eADD(const RegisterCell &A1,
      const RegisterCell &A2) const {
  uint16_t W = A1.width();
  assert(W == A2.width());
  RegisterCell Res(W);
  bool Carry = false;
  uint16_t I;
  for (I = 0; I < W; ++I) {
    const BitValue &V1 = A1[I];
    const BitValue &V2 = A2[I];
    if (!V1.num() || !V2.num())
      break;
    unsigned S = bool(V1) + bool(V2) + Carry;
    Res[I] = BitValue(S & 1);
    Carry = (S > 1);
  }
  for (; I < W; ++I) {
    const BitValue &V1 = A1[I];
    const BitValue &V2 = A2[I];
    // If the next bit is same as Carry, the result will be 0 plus the
    // other bit. The Carry bit will remain unchanged.
    if (V1.is(Carry))
      Res[I] = BitValue::ref(V2);
    else if (V2.is(Carry))
      Res[I] = BitValue::ref(V1);
    else
      break;
  }
  for (; I < W; ++I)
    Res[I] = BitValue::self();
  return Res;
}

BT::RegisterCell BT::MachineEvaluator::eSUB(const RegisterCell &A1,
      const RegisterCell &A2) const {
  uint16_t W = A1.width();
  assert(W == A2.width());
  RegisterCell Res(W);
  bool Borrow = false;
  uint16_t I;
  for (I = 0; I < W; ++I) {
    const BitValue &V1 = A1[I];
    const BitValue &V2 = A2[I];
    if (!V1.num() || !V2.num())
      break;
    unsigned S = bool(V1) - bool(V2) - Borrow;
    Res[I] = BitValue(S & 1);
    Borrow = (S > 1);
  }
  for (; I < W; ++I) {
    const BitValue &V1 = A1[I];
    const BitValue &V2 = A2[I];
    if (V1.is(Borrow)) {
      Res[I] = BitValue::ref(V2);
      break;
    }
    if (V2.is(Borrow))
      Res[I] = BitValue::ref(V1);
    else
      break;
  }
  for (; I < W; ++I)
    Res[I] = BitValue::self();
  return Res;
}

BT::RegisterCell BT::MachineEvaluator::eMLS(const RegisterCell &A1,
      const RegisterCell &A2) const {
  uint16_t W = A1.width() + A2.width();
  uint16_t Z = A1.ct(false) + A2.ct(false);
  RegisterCell Res(W);
  Res.fill(0, Z, BitValue::Zero);
  Res.fill(Z, W, BitValue::self());
  return Res;
}

BT::RegisterCell BT::MachineEvaluator::eMLU(const RegisterCell &A1,
      const RegisterCell &A2) const {
  uint16_t W = A1.width() + A2.width();
  uint16_t Z = A1.ct(false) + A2.ct(false);
  RegisterCell Res(W);
  Res.fill(0, Z, BitValue::Zero);
  Res.fill(Z, W, BitValue::self());
  return Res;
}

BT::RegisterCell BT::MachineEvaluator::eASL(const RegisterCell &A1,
      uint16_t Sh) const {
  assert(Sh <= A1.width());
  RegisterCell Res = RegisterCell::ref(A1);
  Res.rol(Sh);
  Res.fill(0, Sh, BitValue::Zero);
  return Res;
}

BT::RegisterCell BT::MachineEvaluator::eLSR(const RegisterCell &A1,
      uint16_t Sh) const {
  uint16_t W = A1.width();
  assert(Sh <= W);
  RegisterCell Res = RegisterCell::ref(A1);
  Res.rol(W-Sh);
  Res.fill(W-Sh, W, BitValue::Zero);
  return Res;
}

BT::RegisterCell BT::MachineEvaluator::eASR(const RegisterCell &A1,
      uint16_t Sh) const {
  uint16_t W = A1.width();
  assert(Sh <= W);
  RegisterCell Res = RegisterCell::ref(A1);
  BitValue Sign = Res[W-1];
  Res.rol(W-Sh);
  Res.fill(W-Sh, W, Sign);
  return Res;
}

BT::RegisterCell BT::MachineEvaluator::eAND(const RegisterCell &A1,
      const RegisterCell &A2) const {
  uint16_t W = A1.width();
  assert(W == A2.width());
  RegisterCell Res(W);
  for (uint16_t i = 0; i < W; ++i) {
    const BitValue &V1 = A1[i];
    const BitValue &V2 = A2[i];
    if (V1.is(1))
      Res[i] = BitValue::ref(V2);
    else if (V2.is(1))
      Res[i] = BitValue::ref(V1);
    else if (V1.is(0) || V2.is(0))
      Res[i] = BitValue::Zero;
    else if (V1 == V2)
      Res[i] = V1;
    else
      Res[i] = BitValue::self();
  }
  return Res;
}

BT::RegisterCell BT::MachineEvaluator::eORL(const RegisterCell &A1,
      const RegisterCell &A2) const {
  uint16_t W = A1.width();
  assert(W == A2.width());
  RegisterCell Res(W);
  for (uint16_t i = 0; i < W; ++i) {
    const BitValue &V1 = A1[i];
    const BitValue &V2 = A2[i];
    if (V1.is(1) || V2.is(1))
      Res[i] = BitValue::One;
    else if (V1.is(0))
      Res[i] = BitValue::ref(V2);
    else if (V2.is(0))
      Res[i] = BitValue::ref(V1);
    else if (V1 == V2)
      Res[i] = V1;
    else
      Res[i] = BitValue::self();
  }
  return Res;
}

BT::RegisterCell BT::MachineEvaluator::eXOR(const RegisterCell &A1,
      const RegisterCell &A2) const {
  uint16_t W = A1.width();
  assert(W == A2.width());
  RegisterCell Res(W);
  for (uint16_t i = 0; i < W; ++i) {
    const BitValue &V1 = A1[i];
    const BitValue &V2 = A2[i];
    if (V1.is(0))
      Res[i] = BitValue::ref(V2);
    else if (V2.is(0))
      Res[i] = BitValue::ref(V1);
    else if (V1 == V2)
      Res[i] = BitValue::Zero;
    else
      Res[i] = BitValue::self();
  }
  return Res;
}

BT::RegisterCell BT::MachineEvaluator::eNOT(const RegisterCell &A1) const {
  uint16_t W = A1.width();
  RegisterCell Res(W);
  for (uint16_t i = 0; i < W; ++i) {
    const BitValue &V = A1[i];
    if (V.is(0))
      Res[i] = BitValue::One;
    else if (V.is(1))
      Res[i] = BitValue::Zero;
    else
      Res[i] = BitValue::self();
  }
  return Res;
}

BT::RegisterCell BT::MachineEvaluator::eSET(const RegisterCell &A1,
      uint16_t BitN) const {
  assert(BitN < A1.width());
  RegisterCell Res = RegisterCell::ref(A1);
  Res[BitN] = BitValue::One;
  return Res;
}

BT::RegisterCell BT::MachineEvaluator::eCLR(const RegisterCell &A1,
      uint16_t BitN) const {
  assert(BitN < A1.width());
  RegisterCell Res = RegisterCell::ref(A1);
  Res[BitN] = BitValue::Zero;
  return Res;
}

BT::RegisterCell BT::MachineEvaluator::eCLB(const RegisterCell &A1, bool B,
      uint16_t W) const {
  uint16_t C = A1.cl(B), AW = A1.width();
  // If the last leading non-B bit is not a constant, then we don't know
  // the real count.
  if ((C < AW && A1[AW-1-C].num()) || C == AW)
    return eIMM(C, W);
  return RegisterCell::self(0, W);
}

BT::RegisterCell BT::MachineEvaluator::eCTB(const RegisterCell &A1, bool B,
      uint16_t W) const {
  uint16_t C = A1.ct(B), AW = A1.width();
  // If the last trailing non-B bit is not a constant, then we don't know
  // the real count.
  if ((C < AW && A1[C].num()) || C == AW)
    return eIMM(C, W);
  return RegisterCell::self(0, W);
}

BT::RegisterCell BT::MachineEvaluator::eSXT(const RegisterCell &A1,
      uint16_t FromN) const {
  uint16_t W = A1.width();
  assert(FromN <= W);
  RegisterCell Res = RegisterCell::ref(A1);
  BitValue Sign = Res[FromN-1];
  // Sign-extend "inreg".
  Res.fill(FromN, W, Sign);
  return Res;
}

BT::RegisterCell BT::MachineEvaluator::eZXT(const RegisterCell &A1,
      uint16_t FromN) const {
  uint16_t W = A1.width();
  assert(FromN <= W);
  RegisterCell Res = RegisterCell::ref(A1);
  Res.fill(FromN, W, BitValue::Zero);
  return Res;
}

BT::RegisterCell BT::MachineEvaluator::eXTR(const RegisterCell &A1,
      uint16_t B, uint16_t E) const {
  uint16_t W = A1.width();
  assert(B < W && E <= W);
  if (B == E)
    return RegisterCell(0);
  uint16_t Last = (E > 0) ? E-1 : W-1;
  RegisterCell Res = RegisterCell::ref(A1).extract(BT::BitMask(B, Last));
  // Return shorter cell.
  return Res;
}

BT::RegisterCell BT::MachineEvaluator::eINS(const RegisterCell &A1,
      const RegisterCell &A2, uint16_t AtN) const {
  uint16_t W1 = A1.width(), W2 = A2.width();
  (void)W1;
  assert(AtN < W1 && AtN+W2 <= W1);
  // Copy bits from A1, insert A2 at position AtN.
  RegisterCell Res = RegisterCell::ref(A1);
  if (W2 > 0)
    Res.insert(RegisterCell::ref(A2), BT::BitMask(AtN, AtN+W2-1));
  return Res;
}

BT::BitMask BT::MachineEvaluator::mask(unsigned Reg, unsigned Sub) const {
  assert(Sub == 0 && "Generic BitTracker::mask called for Sub != 0");
  uint16_t W = getRegBitWidth(Reg);
  assert(W > 0 && "Cannot generate mask for empty register");
  return BitMask(0, W-1);
}

uint16_t BT::MachineEvaluator::getPhysRegBitWidth(unsigned Reg) const {
  assert(TargetRegisterInfo::isPhysicalRegister(Reg));
  const TargetRegisterClass &PC = *TRI.getMinimalPhysRegClass(Reg);
  return TRI.getRegSizeInBits(PC);
}

bool BT::MachineEvaluator::evaluate(const MachineInstr &MI,
                                    const CellMapType &Inputs,
                                    CellMapType &Outputs) const {
  unsigned Opc = MI.getOpcode();
  switch (Opc) {
    case TargetOpcode::REG_SEQUENCE: {
      RegisterRef RD = MI.getOperand(0);
      assert(RD.Sub == 0);
      RegisterRef RS = MI.getOperand(1);
      unsigned SS = MI.getOperand(2).getImm();
      RegisterRef RT = MI.getOperand(3);
      unsigned ST = MI.getOperand(4).getImm();
      assert(SS != ST);

      uint16_t W = getRegBitWidth(RD);
      RegisterCell Res(W);
      Res.insert(RegisterCell::ref(getCell(RS, Inputs)), mask(RD.Reg, SS));
      Res.insert(RegisterCell::ref(getCell(RT, Inputs)), mask(RD.Reg, ST));
      putCell(RD, Res, Outputs);
      break;
    }

    case TargetOpcode::COPY: {
      // COPY can transfer a smaller register into a wider one.
      // If that is the case, fill the remaining high bits with 0.
      RegisterRef RD = MI.getOperand(0);
      RegisterRef RS = MI.getOperand(1);
      assert(RD.Sub == 0);
      uint16_t WD = getRegBitWidth(RD);
      uint16_t WS = getRegBitWidth(RS);
      assert(WD >= WS);
      RegisterCell Src = getCell(RS, Inputs);
      RegisterCell Res(WD);
      Res.insert(Src, BitMask(0, WS-1));
      Res.fill(WS, WD, BitValue::Zero);
      putCell(RD, Res, Outputs);
      break;
    }

    default:
      return false;
  }

  return true;
}

bool BT::UseQueueType::Cmp::operator()(const MachineInstr *InstA,
                                       const MachineInstr *InstB) const {
  // This is a comparison function for a priority queue: give higher priority
  // to earlier instructions.
  // This operator is used as "less", so returning "true" gives InstB higher
  // priority (because then InstA < InstB).
  if (InstA == InstB)
    return false;
  const MachineBasicBlock *BA = InstA->getParent();
  const MachineBasicBlock *BB = InstB->getParent();
  if (BA != BB) {
    // If the blocks are different, ideally the dominating block would
    // have a higher priority, but it may be too expensive to check.
    return BA->getNumber() > BB->getNumber();
  }

  auto getDist = [this] (const MachineInstr *MI) {
    auto F = Dist.find(MI);
    if (F != Dist.end())
      return F->second;
    MachineBasicBlock::const_iterator I = MI->getParent()->begin();
    MachineBasicBlock::const_iterator E = MI->getIterator();
    unsigned D = std::distance(I, E);
    Dist.insert(std::make_pair(MI, D));
    return D;
  };

  return getDist(InstA) > getDist(InstB);
}

// Main W-Z implementation.

void BT::visitPHI(const MachineInstr &PI) {
  int ThisN = PI.getParent()->getNumber();
  if (Trace)
    dbgs() << "Visit FI(" << printMBBReference(*PI.getParent()) << "): " << PI;

  const MachineOperand &MD = PI.getOperand(0);
  assert(MD.getSubReg() == 0 && "Unexpected sub-register in definition");
  RegisterRef DefRR(MD);
  uint16_t DefBW = ME.getRegBitWidth(DefRR);

  RegisterCell DefC = ME.getCell(DefRR, Map);
  if (DefC == RegisterCell::self(DefRR.Reg, DefBW))    // XXX slow
    return;

  bool Changed = false;

  for (unsigned i = 1, n = PI.getNumOperands(); i < n; i += 2) {
    const MachineBasicBlock *PB = PI.getOperand(i + 1).getMBB();
    int PredN = PB->getNumber();
    if (Trace)
      dbgs() << "  edge " << printMBBReference(*PB) << "->"
             << printMBBReference(*PI.getParent());
    if (!EdgeExec.count(CFGEdge(PredN, ThisN))) {
      if (Trace)
        dbgs() << " not executable\n";
      continue;
    }

    RegisterRef RU = PI.getOperand(i);
    RegisterCell ResC = ME.getCell(RU, Map);
    if (Trace)
      dbgs() << " input reg: " << printReg(RU.Reg, &ME.TRI, RU.Sub)
             << " cell: " << ResC << "\n";
    Changed |= DefC.meet(ResC, DefRR.Reg);
  }

  if (Changed) {
    if (Trace)
      dbgs() << "Output: " << printReg(DefRR.Reg, &ME.TRI, DefRR.Sub)
             << " cell: " << DefC << "\n";
    ME.putCell(DefRR, DefC, Map);
    visitUsesOf(DefRR.Reg);
  }
}

void BT::visitNonBranch(const MachineInstr &MI) {
  if (Trace)
    dbgs() << "Visit MI(" << printMBBReference(*MI.getParent()) << "): " << MI;
  if (MI.isDebugInstr())
    return;
  assert(!MI.isBranch() && "Unexpected branch instruction");

  CellMapType ResMap;
  bool Eval = ME.evaluate(MI, Map, ResMap);

  if (Trace && Eval) {
    for (unsigned i = 0, n = MI.getNumOperands(); i < n; ++i) {
      const MachineOperand &MO = MI.getOperand(i);
      if (!MO.isReg() || !MO.isUse())
        continue;
      RegisterRef RU(MO);
      dbgs() << "  input reg: " << printReg(RU.Reg, &ME.TRI, RU.Sub)
             << " cell: " << ME.getCell(RU, Map) << "\n";
    }
    dbgs() << "Outputs:\n";
    for (const std::pair<unsigned, RegisterCell> &P : ResMap) {
      RegisterRef RD(P.first);
      dbgs() << "  " << printReg(P.first, &ME.TRI) << " cell: "
             << ME.getCell(RD, ResMap) << "\n";
    }
  }

  // Iterate over all definitions of the instruction, and update the
  // cells accordingly.
  for (const MachineOperand &MO : MI.operands()) {
    // Visit register defs only.
    if (!MO.isReg() || !MO.isDef())
      continue;
    RegisterRef RD(MO);
    assert(RD.Sub == 0 && "Unexpected sub-register in definition");
    if (!TargetRegisterInfo::isVirtualRegister(RD.Reg))
      continue;

    bool Changed = false;
    if (!Eval || ResMap.count(RD.Reg) == 0) {
      // Set to "ref" (aka "bottom").
      uint16_t DefBW = ME.getRegBitWidth(RD);
      RegisterCell RefC = RegisterCell::self(RD.Reg, DefBW);
      if (RefC != ME.getCell(RD, Map)) {
        ME.putCell(RD, RefC, Map);
        Changed = true;
      }
    } else {
      RegisterCell DefC = ME.getCell(RD, Map);
      RegisterCell ResC = ME.getCell(RD, ResMap);
      // This is a non-phi instruction, so the values of the inputs come
      // from the same registers each time this instruction is evaluated.
      // During the propagation, the values of the inputs can become lowered
      // in the sense of the lattice operation, which may cause different
      // results to be calculated in subsequent evaluations. This should
      // not cause the bottoming of the result in the map, since the new
      // result is already reflecting the lowered inputs.
      for (uint16_t i = 0, w = DefC.width(); i < w; ++i) {
        BitValue &V = DefC[i];
        // Bits that are already "bottom" should not be updated.
        if (V.Type == BitValue::Ref && V.RefI.Reg == RD.Reg)
          continue;
        // Same for those that are identical in DefC and ResC.
        if (V == ResC[i])
          continue;
        V = ResC[i];
        Changed = true;
      }
      if (Changed)
        ME.putCell(RD, DefC, Map);
    }
    if (Changed)
      visitUsesOf(RD.Reg);
  }
}

void BT::visitBranchesFrom(const MachineInstr &BI) {
  const MachineBasicBlock &B = *BI.getParent();
  MachineBasicBlock::const_iterator It = BI, End = B.end();
  BranchTargetList Targets, BTs;
  bool FallsThrough = true, DefaultToAll = false;
  int ThisN = B.getNumber();

  do {
    BTs.clear();
    const MachineInstr &MI = *It;
    if (Trace)
      dbgs() << "Visit BR(" << printMBBReference(B) << "): " << MI;
    assert(MI.isBranch() && "Expecting branch instruction");
    InstrExec.insert(&MI);
    bool Eval = ME.evaluate(MI, Map, BTs, FallsThrough);
    if (!Eval) {
      // If the evaluation failed, we will add all targets. Keep going in
      // the loop to mark all executable branches as such.
      DefaultToAll = true;
      FallsThrough = true;
      if (Trace)
        dbgs() << "  failed to evaluate: will add all CFG successors\n";
    } else if (!DefaultToAll) {
      // If evaluated successfully add the targets to the cumulative list.
      if (Trace) {
        dbgs() << "  adding targets:";
        for (unsigned i = 0, n = BTs.size(); i < n; ++i)
          dbgs() << " " << printMBBReference(*BTs[i]);
        if (FallsThrough)
          dbgs() << "\n  falls through\n";
        else
          dbgs() << "\n  does not fall through\n";
      }
      Targets.insert(BTs.begin(), BTs.end());
    }
    ++It;
  } while (FallsThrough && It != End);

  if (!DefaultToAll) {
    // Need to add all CFG successors that lead to EH landing pads.
    // There won't be explicit branches to these blocks, but they must
    // be processed.
    for (const MachineBasicBlock *SB : B.successors()) {
      if (SB->isEHPad())
        Targets.insert(SB);
    }
    if (FallsThrough) {
      MachineFunction::const_iterator BIt = B.getIterator();
      MachineFunction::const_iterator Next = std::next(BIt);
      if (Next != MF.end())
        Targets.insert(&*Next);
    }
  } else {
    for (const MachineBasicBlock *SB : B.successors())
      Targets.insert(SB);
  }

  for (const MachineBasicBlock *TB : Targets)
    FlowQ.push(CFGEdge(ThisN, TB->getNumber()));
}

void BT::visitUsesOf(unsigned Reg) {
  if (Trace)
    dbgs() << "queuing uses of modified reg " << printReg(Reg, &ME.TRI)
           << " cell: " << ME.getCell(Reg, Map) << '\n';

  for (MachineInstr &UseI : MRI.use_nodbg_instructions(Reg))
    UseQ.push(&UseI);
}

BT::RegisterCell BT::get(RegisterRef RR) const {
  return ME.getCell(RR, Map);
}

void BT::put(RegisterRef RR, const RegisterCell &RC) {
  ME.putCell(RR, RC, Map);
}

// Replace all references to bits from OldRR with the corresponding bits
// in NewRR.
void BT::subst(RegisterRef OldRR, RegisterRef NewRR) {
  assert(Map.count(OldRR.Reg) > 0 && "OldRR not present in map");
  BitMask OM = ME.mask(OldRR.Reg, OldRR.Sub);
  BitMask NM = ME.mask(NewRR.Reg, NewRR.Sub);
  uint16_t OMB = OM.first(), OME = OM.last();
  uint16_t NMB = NM.first(), NME = NM.last();
  (void)NME;
  assert((OME-OMB == NME-NMB) &&
         "Substituting registers of different lengths");
  for (std::pair<const unsigned, RegisterCell> &P : Map) {
    RegisterCell &RC = P.second;
    for (uint16_t i = 0, w = RC.width(); i < w; ++i) {
      BitValue &V = RC[i];
      if (V.Type != BitValue::Ref || V.RefI.Reg != OldRR.Reg)
        continue;
      if (V.RefI.Pos < OMB || V.RefI.Pos > OME)
        continue;
      V.RefI.Reg = NewRR.Reg;
      V.RefI.Pos += NMB-OMB;
    }
  }
}

// Check if the block has been "executed" during propagation. (If not, the
// block is dead, but it may still appear to be reachable.)
bool BT::reached(const MachineBasicBlock *B) const {
  int BN = B->getNumber();
  assert(BN >= 0);
  return ReachedBB.count(BN);
}

// Visit an individual instruction. This could be a newly added instruction,
// or one that has been modified by an optimization.
void BT::visit(const MachineInstr &MI) {
  assert(!MI.isBranch() && "Only non-branches are allowed");
  InstrExec.insert(&MI);
  visitNonBranch(MI);
  // Make sure to flush all the pending use updates.
  runUseQueue();
  // The call to visitNonBranch could propagate the changes until a branch
  // is actually visited. This could result in adding CFG edges to the flow
  // queue. Since the queue won't be processed, clear it.
  while (!FlowQ.empty())
    FlowQ.pop();
}

void BT::reset() {
  EdgeExec.clear();
  InstrExec.clear();
  Map.clear();
  ReachedBB.clear();
  ReachedBB.reserve(MF.size());
}

void BT::runEdgeQueue(BitVector &BlockScanned) {
  while (!FlowQ.empty()) {
    CFGEdge Edge = FlowQ.front();
    FlowQ.pop();

    if (EdgeExec.count(Edge))
      return;
    EdgeExec.insert(Edge);
    ReachedBB.insert(Edge.second);

    const MachineBasicBlock &B = *MF.getBlockNumbered(Edge.second);
    MachineBasicBlock::const_iterator It = B.begin(), End = B.end();
    // Visit PHI nodes first.
    while (It != End && It->isPHI()) {
      const MachineInstr &PI = *It++;
      InstrExec.insert(&PI);
      visitPHI(PI);
    }

    // If this block has already been visited through a flow graph edge,
    // then the instructions have already been processed. Any updates to
    // the cells would now only happen through visitUsesOf...
    if (BlockScanned[Edge.second])
      return;
    BlockScanned[Edge.second] = true;

    // Visit non-branch instructions.
    while (It != End && !It->isBranch()) {
      const MachineInstr &MI = *It++;
      InstrExec.insert(&MI);
      visitNonBranch(MI);
    }
    // If block end has been reached, add the fall-through edge to the queue.
    if (It == End) {
      MachineFunction::const_iterator BIt = B.getIterator();
      MachineFunction::const_iterator Next = std::next(BIt);
      if (Next != MF.end() && B.isSuccessor(&*Next)) {
        int ThisN = B.getNumber();
        int NextN = Next->getNumber();
        FlowQ.push(CFGEdge(ThisN, NextN));
      }
    } else {
      // Handle the remaining sequence of branches. This function will update
      // the work queue.
      visitBranchesFrom(*It);
    }
  } // while (!FlowQ->empty())
}

void BT::runUseQueue() {
  while (!UseQ.empty()) {
    MachineInstr &UseI = *UseQ.front();
    UseQ.pop();

    if (!InstrExec.count(&UseI))
      continue;
    if (UseI.isPHI())
      visitPHI(UseI);
    else if (!UseI.isBranch())
      visitNonBranch(UseI);
    else
      visitBranchesFrom(UseI);
  }
}

void BT::run() {
  reset();
  assert(FlowQ.empty());

  using MachineFlowGraphTraits = GraphTraits<const MachineFunction*>;
  const MachineBasicBlock *Entry = MachineFlowGraphTraits::getEntryNode(&MF);

  unsigned MaxBN = 0;
  for (const MachineBasicBlock &B : MF) {
    assert(B.getNumber() >= 0 && "Disconnected block");
    unsigned BN = B.getNumber();
    if (BN > MaxBN)
      MaxBN = BN;
  }

  // Keep track of visited blocks.
  BitVector BlockScanned(MaxBN+1);

  int EntryN = Entry->getNumber();
  // Generate a fake edge to get something to start with.
  FlowQ.push(CFGEdge(-1, EntryN));

  while (!FlowQ.empty() || !UseQ.empty()) {
    runEdgeQueue(BlockScanned);
    runUseQueue();
  }
  UseQ.reset();

  if (Trace)
    print_cells(dbgs() << "Cells after propagation:\n");
}
