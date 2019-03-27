//===--- InfoByHwMode.h -----------------------------------------*- C++ -*-===//
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

#ifndef LLVM_UTILS_TABLEGEN_INFOBYHWMODE_H
#define LLVM_UTILS_TABLEGEN_INFOBYHWMODE_H

#include "CodeGenHwModes.h"
#include "llvm/Support/MachineValueType.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace llvm {

struct CodeGenHwModes;
class Record;
class raw_ostream;

template <typename InfoT> struct InfoByHwMode;

std::string getModeName(unsigned Mode);

enum : unsigned {
  DefaultMode = CodeGenHwModes::DefaultMode,
};

template <typename InfoT>
std::vector<unsigned> union_modes(const InfoByHwMode<InfoT> &A,
                                  const InfoByHwMode<InfoT> &B) {
  std::vector<unsigned> V;
  std::set<unsigned> U;
  for (const auto &P : A)
    U.insert(P.first);
  for (const auto &P : B)
    U.insert(P.first);
  // Make sure that the default mode is last on the list.
  bool HasDefault = false;
  for (unsigned M : U)
    if (M != DefaultMode)
      V.push_back(M);
    else
      HasDefault = true;
  if (HasDefault)
    V.push_back(DefaultMode);
  return V;
}

template <typename InfoT>
struct InfoByHwMode {
  typedef std::map<unsigned,InfoT> MapType;
  typedef typename MapType::value_type PairType;
  typedef typename MapType::iterator iterator;
  typedef typename MapType::const_iterator const_iterator;

  InfoByHwMode() = default;
  InfoByHwMode(const MapType &M) : Map(M) {}

  LLVM_ATTRIBUTE_ALWAYS_INLINE
  iterator begin() { return Map.begin(); }
  LLVM_ATTRIBUTE_ALWAYS_INLINE
  iterator end()   { return Map.end(); }
  LLVM_ATTRIBUTE_ALWAYS_INLINE
  const_iterator begin() const { return Map.begin(); }
  LLVM_ATTRIBUTE_ALWAYS_INLINE
  const_iterator end() const   { return Map.end(); }
  LLVM_ATTRIBUTE_ALWAYS_INLINE
  bool empty() const { return Map.empty(); }

  LLVM_ATTRIBUTE_ALWAYS_INLINE
  bool hasMode(unsigned M) const { return Map.find(M) != Map.end(); }
  LLVM_ATTRIBUTE_ALWAYS_INLINE
  bool hasDefault() const { return hasMode(DefaultMode); }

  InfoT &get(unsigned Mode) {
    if (!hasMode(Mode)) {
      assert(hasMode(DefaultMode));
      Map.insert({Mode, Map.at(DefaultMode)});
    }
    return Map.at(Mode);
  }
  const InfoT &get(unsigned Mode) const {
    auto F = Map.find(Mode);
    if (Mode != DefaultMode && F == Map.end())
      F = Map.find(DefaultMode);
    assert(F != Map.end());
    return F->second;
  }

  LLVM_ATTRIBUTE_ALWAYS_INLINE
  bool isSimple() const {
    return Map.size() == 1 && Map.begin()->first == DefaultMode;
  }
  LLVM_ATTRIBUTE_ALWAYS_INLINE
  InfoT getSimple() const {
    assert(isSimple());
    return Map.begin()->second;
  }
  void makeSimple(unsigned Mode) {
    assert(hasMode(Mode) || hasDefault());
    InfoT I = get(Mode);
    Map.clear();
    Map.insert(std::make_pair(DefaultMode, I));
  }

  MapType Map;
};

struct ValueTypeByHwMode : public InfoByHwMode<MVT> {
  ValueTypeByHwMode(Record *R, const CodeGenHwModes &CGH);
  ValueTypeByHwMode(MVT T) { Map.insert({DefaultMode,T}); }
  ValueTypeByHwMode() = default;

  bool operator== (const ValueTypeByHwMode &T) const;
  bool operator< (const ValueTypeByHwMode &T) const;

  bool isValid() const {
    return !Map.empty();
  }
  MVT getType(unsigned Mode) const { return get(Mode); }
  MVT &getOrCreateTypeForMode(unsigned Mode, MVT Type);

  static StringRef getMVTName(MVT T);
  void writeToStream(raw_ostream &OS) const;
  void dump() const;
};

ValueTypeByHwMode getValueTypeByHwMode(Record *Rec,
                                       const CodeGenHwModes &CGH);

struct RegSizeInfo {
  unsigned RegSize;
  unsigned SpillSize;
  unsigned SpillAlignment;

  RegSizeInfo(Record *R, const CodeGenHwModes &CGH);
  RegSizeInfo() = default;
  bool operator< (const RegSizeInfo &I) const;
  bool operator== (const RegSizeInfo &I) const {
    return std::tie(RegSize, SpillSize, SpillAlignment) ==
           std::tie(I.RegSize, I.SpillSize, I.SpillAlignment);
  }
  bool operator!= (const RegSizeInfo &I) const {
    return !(*this == I);
  }

  bool isSubClassOf(const RegSizeInfo &I) const;
  void writeToStream(raw_ostream &OS) const;
};

struct RegSizeInfoByHwMode : public InfoByHwMode<RegSizeInfo> {
  RegSizeInfoByHwMode(Record *R, const CodeGenHwModes &CGH);
  RegSizeInfoByHwMode() = default;
  bool operator< (const RegSizeInfoByHwMode &VI) const;
  bool operator== (const RegSizeInfoByHwMode &VI) const;
  bool operator!= (const RegSizeInfoByHwMode &VI) const {
    return !(*this == VI);
  }

  bool isSubClassOf(const RegSizeInfoByHwMode &I) const;
  bool hasStricterSpillThan(const RegSizeInfoByHwMode &I) const;

  void writeToStream(raw_ostream &OS) const;
};

raw_ostream &operator<<(raw_ostream &OS, const ValueTypeByHwMode &T);
raw_ostream &operator<<(raw_ostream &OS, const RegSizeInfo &T);
raw_ostream &operator<<(raw_ostream &OS, const RegSizeInfoByHwMode &T);

} // namespace llvm

#endif // LLVM_UTILS_TABLEGEN_INFOBYHWMODE_H
