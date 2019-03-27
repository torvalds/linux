//===--- CodeGenHwModes.cpp -----------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Classes to parse and store HW mode information for instruction selection
//===----------------------------------------------------------------------===//

#include "CodeGenHwModes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"

using namespace llvm;

StringRef CodeGenHwModes::DefaultModeName = "DefaultMode";

HwMode::HwMode(Record *R) {
  Name = R->getName();
  Features = R->getValueAsString("Features");
}

LLVM_DUMP_METHOD
void HwMode::dump() const {
  dbgs() << Name << ": " << Features << '\n';
}

HwModeSelect::HwModeSelect(Record *R, CodeGenHwModes &CGH) {
  std::vector<Record*> Modes = R->getValueAsListOfDefs("Modes");
  std::vector<Record*> Objects = R->getValueAsListOfDefs("Objects");
  if (Modes.size() != Objects.size()) {
    PrintError(R->getLoc(), "in record " + R->getName() +
        " derived from HwModeSelect: the lists Modes and Objects should "
        "have the same size");
    report_fatal_error("error in target description.");
  }
  for (unsigned i = 0, e = Modes.size(); i != e; ++i) {
    unsigned ModeId = CGH.getHwModeId(Modes[i]->getName());
    Items.push_back(std::make_pair(ModeId, Objects[i]));
  }
}

LLVM_DUMP_METHOD
void HwModeSelect::dump() const {
  dbgs() << '{';
  for (const PairType &P : Items)
    dbgs() << " (" << P.first << ',' << P.second->getName() << ')';
  dbgs() << " }\n";
}

CodeGenHwModes::CodeGenHwModes(RecordKeeper &RK) : Records(RK) {
  std::vector<Record*> MRs = Records.getAllDerivedDefinitions("HwMode");
  // The default mode needs a definition in the .td sources for TableGen
  // to accept references to it. We need to ignore the definition here.
  for (auto I = MRs.begin(), E = MRs.end(); I != E; ++I) {
    if ((*I)->getName() != DefaultModeName)
      continue;
    MRs.erase(I);
    break;
  }

  for (Record *R : MRs) {
    Modes.emplace_back(R);
    unsigned NewId = Modes.size();
    ModeIds.insert(std::make_pair(Modes[NewId-1].Name, NewId));
  }

  std::vector<Record*> MSs = Records.getAllDerivedDefinitions("HwModeSelect");
  for (Record *R : MSs) {
    auto P = ModeSelects.emplace(std::make_pair(R, HwModeSelect(R, *this)));
    assert(P.second);
    (void)P;
  }
}

unsigned CodeGenHwModes::getHwModeId(StringRef Name) const {
  if (Name == DefaultModeName)
    return DefaultMode;
  auto F = ModeIds.find(Name);
  assert(F != ModeIds.end() && "Unknown mode name");
  return F->second;
}

const HwModeSelect &CodeGenHwModes::getHwModeSelect(Record *R) const {
  auto F = ModeSelects.find(R);
  assert(F != ModeSelects.end() && "Record is not a \"mode select\"");
  return F->second;
}

LLVM_DUMP_METHOD
void CodeGenHwModes::dump() const {
  dbgs() << "Modes: {\n";
  for (const HwMode &M : Modes) {
    dbgs() << "  ";
    M.dump();
  }
  dbgs() << "}\n";

  dbgs() << "ModeIds: {\n";
  for (const auto &P : ModeIds)
    dbgs() << "  " << P.first() << " -> " << P.second << '\n';
  dbgs() << "}\n";

  dbgs() << "ModeSelects: {\n";
  for (const auto &P : ModeSelects) {
    dbgs() << "  " << P.first->getName() << " -> ";
    P.second.dump();
  }
  dbgs() << "}\n";
}
