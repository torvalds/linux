//===- ExegesisEmitter.cpp - Generate exegesis target data ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This tablegen backend emits llvm-exegesis information.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "exegesis-emitter"

namespace {

class ExegesisEmitter {
public:
  ExegesisEmitter(RecordKeeper &RK);

  void run(raw_ostream &OS) const;

private:
  unsigned getPfmCounterId(llvm::StringRef Name) const {
    const auto It = PfmCounterNameTable.find(Name);
    if (It == PfmCounterNameTable.end())
      PrintFatalError("no pfm counter id for " + Name);
    return It->second;
  }

  // Collects all the ProcPfmCounters definitions available in this target.
  void emitPfmCounters(raw_ostream &OS) const;

  void emitPfmCountersInfo(const Record &Def,
                           unsigned &IssueCountersTableOffset,
                           raw_ostream &OS) const;

  void emitPfmCountersLookupTable(raw_ostream &OS) const;

  RecordKeeper &Records;
  std::string Target;

  // Table of counter name -> counter index.
  const std::map<llvm::StringRef, unsigned> PfmCounterNameTable;
};

static std::map<llvm::StringRef, unsigned>
collectPfmCounters(const RecordKeeper &Records) {
  std::map<llvm::StringRef, unsigned> PfmCounterNameTable;
  const auto AddPfmCounterName = [&PfmCounterNameTable](
                                     const Record *PfmCounterDef) {
    const llvm::StringRef Counter = PfmCounterDef->getValueAsString("Counter");
    if (!Counter.empty())
      PfmCounterNameTable.emplace(Counter, 0);
  };
  for (Record *Def : Records.getAllDerivedDefinitions("ProcPfmCounters")) {
    // Check that ResourceNames are unique.
    llvm::SmallSet<llvm::StringRef, 16> Seen;
    for (const Record *IssueCounter :
         Def->getValueAsListOfDefs("IssueCounters")) {
      const llvm::StringRef ResourceName =
          IssueCounter->getValueAsString("ResourceName");
      if (ResourceName.empty())
        PrintFatalError(IssueCounter->getLoc(), "invalid empty ResourceName");
      if (!Seen.insert(ResourceName).second)
        PrintFatalError(IssueCounter->getLoc(),
                        "duplicate ResourceName " + ResourceName);
      AddPfmCounterName(IssueCounter);
    }
    AddPfmCounterName(Def->getValueAsDef("CycleCounter"));
    AddPfmCounterName(Def->getValueAsDef("UopsCounter"));
  }
  unsigned Index = 0;
  for (auto &NameAndIndex : PfmCounterNameTable)
    NameAndIndex.second = Index++;
  return PfmCounterNameTable;
}

ExegesisEmitter::ExegesisEmitter(RecordKeeper &RK)
    : Records(RK), PfmCounterNameTable(collectPfmCounters(RK)) {
  std::vector<Record *> Targets = Records.getAllDerivedDefinitions("Target");
  if (Targets.size() == 0)
    PrintFatalError("ERROR: No 'Target' subclasses defined!");
  if (Targets.size() != 1)
    PrintFatalError("ERROR: Multiple subclasses of Target defined!");
  Target = Targets[0]->getName();
}

void ExegesisEmitter::emitPfmCountersInfo(const Record &Def,
                                          unsigned &IssueCountersTableOffset,
                                          raw_ostream &OS) const {
  const auto CycleCounter =
      Def.getValueAsDef("CycleCounter")->getValueAsString("Counter");
  const auto UopsCounter =
      Def.getValueAsDef("UopsCounter")->getValueAsString("Counter");
  const size_t NumIssueCounters =
      Def.getValueAsListOfDefs("IssueCounters").size();

  OS << "\nstatic const PfmCountersInfo " << Target << Def.getName()
     << " = {\n";

  // Cycle Counter.
  if (CycleCounter.empty())
    OS << "  nullptr,  // No cycle counter.\n";
  else
    OS << "  " << Target << "PfmCounterNames[" << getPfmCounterId(CycleCounter)
       << "],  // Cycle counter\n";

  // Uops Counter.
  if (UopsCounter.empty())
    OS << "  nullptr,  // No uops counter.\n";
  else
    OS << "  " << Target << "PfmCounterNames[" << getPfmCounterId(UopsCounter)
       << "],  // Uops counter\n";

  // Issue Counters
  if (NumIssueCounters == 0)
    OS << "  nullptr,  // No issue counters.\n  0\n";
  else
    OS << "  " << Target << "PfmIssueCounters + " << IssueCountersTableOffset
       << ", " << NumIssueCounters << " // Issue counters.\n";

  OS << "};\n";
  IssueCountersTableOffset += NumIssueCounters;
}

void ExegesisEmitter::emitPfmCounters(raw_ostream &OS) const {
  // Emit the counter name table.
  OS << "\nstatic const char* " << Target << "PfmCounterNames[] = {\n";
  for (const auto &NameAndIndex : PfmCounterNameTable)
    OS << "  \"" << NameAndIndex.first << "\", // " << NameAndIndex.second
       << "\n";
  OS << "};\n\n";

  // Emit the IssueCounters table.
  const auto PfmCounterDefs =
      Records.getAllDerivedDefinitions("ProcPfmCounters");
  // Only emit if non-empty.
  const bool HasAtLeastOnePfmIssueCounter =
      llvm::any_of(PfmCounterDefs, [](const Record *Def) {
        return !Def->getValueAsListOfDefs("IssueCounters").empty();
      });
  if (HasAtLeastOnePfmIssueCounter) {
    OS << "static const PfmCountersInfo::IssueCounter " << Target
       << "PfmIssueCounters[] = {\n";
    for (const Record *Def : PfmCounterDefs) {
      for (const Record *ICDef : Def->getValueAsListOfDefs("IssueCounters"))
        OS << "  { " << Target << "PfmCounterNames["
           << getPfmCounterId(ICDef->getValueAsString("Counter")) << "], \""
           << ICDef->getValueAsString("ResourceName") << "\"},\n";
    }
    OS << "};\n";
  }

  // Now generate the PfmCountersInfo.
  unsigned IssueCountersTableOffset = 0;
  for (const Record *Def : PfmCounterDefs)
    emitPfmCountersInfo(*Def, IssueCountersTableOffset, OS);

  OS << "\n";
} // namespace

void ExegesisEmitter::emitPfmCountersLookupTable(raw_ostream &OS) const {
  std::vector<Record *> Bindings =
      Records.getAllDerivedDefinitions("PfmCountersBinding");
  assert(!Bindings.empty() && "there must be at least one binding");
  llvm::sort(Bindings, [](const Record *L, const Record *R) {
    return L->getValueAsString("CpuName") < R->getValueAsString("CpuName");
  });

  OS << "// Sorted (by CpuName) array of pfm counters.\n"
     << "static const CpuAndPfmCounters " << Target << "CpuPfmCounters[] = {\n";
  for (Record *Binding : Bindings) {
    // Emit as { "cpu", procinit },
    OS << "  { \""                                                        //
       << Binding->getValueAsString("CpuName") << "\","                   //
       << " &" << Target << Binding->getValueAsDef("Counters")->getName() //
       << " },\n";
  }
  OS << "};\n\n";
}

void ExegesisEmitter::run(raw_ostream &OS) const {
  emitSourceFileHeader("Exegesis Tables", OS);
  emitPfmCounters(OS);
  emitPfmCountersLookupTable(OS);
}

} // end anonymous namespace

namespace llvm {

void EmitExegesis(RecordKeeper &RK, raw_ostream &OS) {
  ExegesisEmitter(RK).run(OS);
}

} // end namespace llvm
