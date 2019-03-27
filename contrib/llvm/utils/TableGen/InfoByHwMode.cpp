//===--- InfoByHwMode.cpp -------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Classes that implement data parameterized by HW modes for instruction
// selection. Currently it is ValueTypeByHwMode (parameterized ValueType),
// and RegSizeInfoByHwMode (parameterized register/spill size and alignment
// data).
//===----------------------------------------------------------------------===//

#include "CodeGenTarget.h"
#include "InfoByHwMode.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include <set>
#include <string>

using namespace llvm;

std::string llvm::getModeName(unsigned Mode) {
  if (Mode == DefaultMode)
    return "*";
  return (Twine('m') + Twine(Mode)).str();
}

ValueTypeByHwMode::ValueTypeByHwMode(Record *R, const CodeGenHwModes &CGH) {
  const HwModeSelect &MS = CGH.getHwModeSelect(R);
  for (const HwModeSelect::PairType &P : MS.Items) {
    auto I = Map.insert({P.first, MVT(llvm::getValueType(P.second))});
    assert(I.second && "Duplicate entry?");
    (void)I;
  }
}

bool ValueTypeByHwMode::operator== (const ValueTypeByHwMode &T) const {
  assert(isValid() && T.isValid() && "Invalid type in assignment");
  bool Simple = isSimple();
  if (Simple != T.isSimple())
    return false;
  if (Simple)
    return getSimple() == T.getSimple();

  return Map == T.Map;
}

bool ValueTypeByHwMode::operator< (const ValueTypeByHwMode &T) const {
  assert(isValid() && T.isValid() && "Invalid type in comparison");
  // Default order for maps.
  return Map < T.Map;
}

MVT &ValueTypeByHwMode::getOrCreateTypeForMode(unsigned Mode, MVT Type) {
  auto F = Map.find(Mode);
  if (F != Map.end())
    return F->second;
  // If Mode is not in the map, look up the default mode. If it exists,
  // make a copy of it for Mode and return it.
  auto D = Map.find(DefaultMode);
  if (D != Map.end())
    return Map.insert(std::make_pair(Mode, D->second)).first->second;
  // If default mode is not present either, use provided Type.
  return Map.insert(std::make_pair(Mode, Type)).first->second;
}

StringRef ValueTypeByHwMode::getMVTName(MVT T) {
  StringRef N = llvm::getEnumName(T.SimpleTy);
  N.consume_front("MVT::");
  return N;
}

void ValueTypeByHwMode::writeToStream(raw_ostream &OS) const {
  if (isSimple()) {
    OS << getMVTName(getSimple());
    return;
  }

  std::vector<const PairType*> Pairs;
  for (const auto &P : Map)
    Pairs.push_back(&P);
  llvm::sort(Pairs, deref<std::less<PairType>>());

  OS << '{';
  for (unsigned i = 0, e = Pairs.size(); i != e; ++i) {
    const PairType *P = Pairs[i];
    OS << '(' << getModeName(P->first)
       << ':' << getMVTName(P->second).str() << ')';
    if (i != e-1)
      OS << ',';
  }
  OS << '}';
}

LLVM_DUMP_METHOD
void ValueTypeByHwMode::dump() const {
  dbgs() << *this << '\n';
}

ValueTypeByHwMode llvm::getValueTypeByHwMode(Record *Rec,
                                             const CodeGenHwModes &CGH) {
#ifndef NDEBUG
  if (!Rec->isSubClassOf("ValueType"))
    Rec->dump();
#endif
  assert(Rec->isSubClassOf("ValueType") &&
         "Record must be derived from ValueType");
  if (Rec->isSubClassOf("HwModeSelect"))
    return ValueTypeByHwMode(Rec, CGH);
  return ValueTypeByHwMode(llvm::getValueType(Rec));
}

RegSizeInfo::RegSizeInfo(Record *R, const CodeGenHwModes &CGH) {
  RegSize = R->getValueAsInt("RegSize");
  SpillSize = R->getValueAsInt("SpillSize");
  SpillAlignment = R->getValueAsInt("SpillAlignment");
}

bool RegSizeInfo::operator< (const RegSizeInfo &I) const {
  return std::tie(RegSize, SpillSize, SpillAlignment) <
         std::tie(I.RegSize, I.SpillSize, I.SpillAlignment);
}

bool RegSizeInfo::isSubClassOf(const RegSizeInfo &I) const {
  return RegSize <= I.RegSize &&
         SpillAlignment && I.SpillAlignment % SpillAlignment == 0 &&
         SpillSize <= I.SpillSize;
}

void RegSizeInfo::writeToStream(raw_ostream &OS) const {
  OS << "[R=" << RegSize << ",S=" << SpillSize
     << ",A=" << SpillAlignment << ']';
}

RegSizeInfoByHwMode::RegSizeInfoByHwMode(Record *R,
      const CodeGenHwModes &CGH) {
  const HwModeSelect &MS = CGH.getHwModeSelect(R);
  for (const HwModeSelect::PairType &P : MS.Items) {
    auto I = Map.insert({P.first, RegSizeInfo(P.second, CGH)});
    assert(I.second && "Duplicate entry?");
    (void)I;
  }
}

bool RegSizeInfoByHwMode::operator< (const RegSizeInfoByHwMode &I) const {
  unsigned M0 = Map.begin()->first;
  return get(M0) < I.get(M0);
}

bool RegSizeInfoByHwMode::operator== (const RegSizeInfoByHwMode &I) const {
  unsigned M0 = Map.begin()->first;
  return get(M0) == I.get(M0);
}

bool RegSizeInfoByHwMode::isSubClassOf(const RegSizeInfoByHwMode &I) const {
  unsigned M0 = Map.begin()->first;
  return get(M0).isSubClassOf(I.get(M0));
}

bool RegSizeInfoByHwMode::hasStricterSpillThan(const RegSizeInfoByHwMode &I)
      const {
  unsigned M0 = Map.begin()->first;
  const RegSizeInfo &A0 = get(M0);
  const RegSizeInfo &B0 = I.get(M0);
  return std::tie(A0.SpillSize, A0.SpillAlignment) >
         std::tie(B0.SpillSize, B0.SpillAlignment);
}

void RegSizeInfoByHwMode::writeToStream(raw_ostream &OS) const {
  typedef typename decltype(Map)::value_type PairType;
  std::vector<const PairType*> Pairs;
  for (const auto &P : Map)
    Pairs.push_back(&P);
  llvm::sort(Pairs, deref<std::less<PairType>>());

  OS << '{';
  for (unsigned i = 0, e = Pairs.size(); i != e; ++i) {
    const PairType *P = Pairs[i];
    OS << '(' << getModeName(P->first) << ':' << P->second << ')';
    if (i != e-1)
      OS << ',';
  }
  OS << '}';
}

namespace llvm {
  raw_ostream &operator<<(raw_ostream &OS, const ValueTypeByHwMode &T) {
    T.writeToStream(OS);
    return OS;
  }

  raw_ostream &operator<<(raw_ostream &OS, const RegSizeInfo &T) {
    T.writeToStream(OS);
    return OS;
  }

  raw_ostream &operator<<(raw_ostream &OS, const RegSizeInfoByHwMode &T) {
    T.writeToStream(OS);
    return OS;
  }
}
