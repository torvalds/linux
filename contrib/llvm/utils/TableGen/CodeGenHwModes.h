//===--- CodeGenHwModes.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Classes to parse and store HW mode information for instruction selection.
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_TABLEGEN_CODEGENHWMODES_H
#define LLVM_UTILS_TABLEGEN_CODEGENHWMODES_H

#include "llvm/ADT/StringMap.h"
#include <map>
#include <string>
#include <vector>

// HwModeId -> list of predicates (definition)

namespace llvm {
  class Record;
  class RecordKeeper;

  struct CodeGenHwModes;

  struct HwMode {
    HwMode(Record *R);
    StringRef Name;
    std::string Features;
    void dump() const;
  };

  struct HwModeSelect {
    HwModeSelect(Record *R, CodeGenHwModes &CGH);
    typedef std::pair<unsigned, Record*> PairType;
    std::vector<PairType> Items;
    void dump() const;
  };

  struct CodeGenHwModes {
    enum : unsigned { DefaultMode = 0 };
    static StringRef DefaultModeName;

    CodeGenHwModes(RecordKeeper &R);
    unsigned getHwModeId(StringRef Name) const;
    const HwMode &getMode(unsigned Id) const {
      assert(Id != 0 && "Mode id of 0 is reserved for the default mode");
      return Modes[Id-1];
    }
    const HwModeSelect &getHwModeSelect(Record *R) const;
    unsigned getNumModeIds() const { return Modes.size()+1; }
    void dump() const;

  private:
    RecordKeeper &Records;
    StringMap<unsigned> ModeIds;  // HwMode (string) -> HwModeId
    std::vector<HwMode> Modes;
    std::map<Record*,HwModeSelect> ModeSelects;
  };
}

#endif // LLVM_UTILS_TABLEGEN_CODEGENHWMODES_H
