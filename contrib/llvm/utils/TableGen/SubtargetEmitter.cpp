//===- SubtargetEmitter.cpp - Generate subtarget enumerations -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This tablegen backend emits subtarget enumerations.
//
//===----------------------------------------------------------------------===//

#include "CodeGenTarget.h"
#include "CodeGenSchedule.h"
#include "PredicateExpander.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/MC/MCSchedule.h"
#include "llvm/MC/SubtargetFeature.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <map>
#include <string>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "subtarget-emitter"

namespace {

class SubtargetEmitter {
  // Each processor has a SchedClassDesc table with an entry for each SchedClass.
  // The SchedClassDesc table indexes into a global write resource table, write
  // latency table, and read advance table.
  struct SchedClassTables {
    std::vector<std::vector<MCSchedClassDesc>> ProcSchedClasses;
    std::vector<MCWriteProcResEntry> WriteProcResources;
    std::vector<MCWriteLatencyEntry> WriteLatencies;
    std::vector<std::string> WriterNames;
    std::vector<MCReadAdvanceEntry> ReadAdvanceEntries;

    // Reserve an invalid entry at index 0
    SchedClassTables() {
      ProcSchedClasses.resize(1);
      WriteProcResources.resize(1);
      WriteLatencies.resize(1);
      WriterNames.push_back("InvalidWrite");
      ReadAdvanceEntries.resize(1);
    }
  };

  struct LessWriteProcResources {
    bool operator()(const MCWriteProcResEntry &LHS,
                    const MCWriteProcResEntry &RHS) {
      return LHS.ProcResourceIdx < RHS.ProcResourceIdx;
    }
  };

  const CodeGenTarget &TGT;
  RecordKeeper &Records;
  CodeGenSchedModels &SchedModels;
  std::string Target;

  void Enumeration(raw_ostream &OS);
  unsigned FeatureKeyValues(raw_ostream &OS);
  unsigned CPUKeyValues(raw_ostream &OS);
  void FormItineraryStageString(const std::string &Names,
                                Record *ItinData, std::string &ItinString,
                                unsigned &NStages);
  void FormItineraryOperandCycleString(Record *ItinData, std::string &ItinString,
                                       unsigned &NOperandCycles);
  void FormItineraryBypassString(const std::string &Names,
                                 Record *ItinData,
                                 std::string &ItinString, unsigned NOperandCycles);
  void EmitStageAndOperandCycleData(raw_ostream &OS,
                                    std::vector<std::vector<InstrItinerary>>
                                      &ProcItinLists);
  void EmitItineraries(raw_ostream &OS,
                       std::vector<std::vector<InstrItinerary>>
                         &ProcItinLists);
  unsigned EmitRegisterFileTables(const CodeGenProcModel &ProcModel,
                                  raw_ostream &OS);
  void EmitLoadStoreQueueInfo(const CodeGenProcModel &ProcModel,
                              raw_ostream &OS);
  void EmitExtraProcessorInfo(const CodeGenProcModel &ProcModel,
                              raw_ostream &OS);
  void EmitProcessorProp(raw_ostream &OS, const Record *R, StringRef Name,
                         char Separator);
  void EmitProcessorResourceSubUnits(const CodeGenProcModel &ProcModel,
                                     raw_ostream &OS);
  void EmitProcessorResources(const CodeGenProcModel &ProcModel,
                              raw_ostream &OS);
  Record *FindWriteResources(const CodeGenSchedRW &SchedWrite,
                             const CodeGenProcModel &ProcModel);
  Record *FindReadAdvance(const CodeGenSchedRW &SchedRead,
                          const CodeGenProcModel &ProcModel);
  void ExpandProcResources(RecVec &PRVec, std::vector<int64_t> &Cycles,
                           const CodeGenProcModel &ProcModel);
  void GenSchedClassTables(const CodeGenProcModel &ProcModel,
                           SchedClassTables &SchedTables);
  void EmitSchedClassTables(SchedClassTables &SchedTables, raw_ostream &OS);
  void EmitProcessorModels(raw_ostream &OS);
  void EmitProcessorLookup(raw_ostream &OS);
  void EmitSchedModelHelpers(const std::string &ClassName, raw_ostream &OS);
  void emitSchedModelHelpersImpl(raw_ostream &OS,
                                 bool OnlyExpandMCInstPredicates = false);
  void emitGenMCSubtargetInfo(raw_ostream &OS);
  void EmitMCInstrAnalysisPredicateFunctions(raw_ostream &OS);

  void EmitSchedModel(raw_ostream &OS);
  void EmitHwModeCheck(const std::string &ClassName, raw_ostream &OS);
  void ParseFeaturesFunction(raw_ostream &OS, unsigned NumFeatures,
                             unsigned NumProcs);

public:
  SubtargetEmitter(RecordKeeper &R, CodeGenTarget &TGT)
    : TGT(TGT), Records(R), SchedModels(TGT.getSchedModels()),
      Target(TGT.getName()) {}

  void run(raw_ostream &o);
};

} // end anonymous namespace

//
// Enumeration - Emit the specified class as an enumeration.
//
void SubtargetEmitter::Enumeration(raw_ostream &OS) {
  // Get all records of class and sort
  std::vector<Record*> DefList =
    Records.getAllDerivedDefinitions("SubtargetFeature");
  llvm::sort(DefList, LessRecord());

  unsigned N = DefList.size();
  if (N == 0)
    return;
  if (N > MAX_SUBTARGET_FEATURES)
    PrintFatalError("Too many subtarget features! Bump MAX_SUBTARGET_FEATURES.");

  OS << "namespace " << Target << " {\n";

  // Open enumeration.
  OS << "enum {\n";

  // For each record
  for (unsigned i = 0; i < N; ++i) {
    // Next record
    Record *Def = DefList[i];

    // Get and emit name
    OS << "  " << Def->getName() << " = " << i << ",\n";
  }

  // Close enumeration and namespace
  OS << "};\n";
  OS << "} // end namespace " << Target << "\n";
}

//
// FeatureKeyValues - Emit data of all the subtarget features.  Used by the
// command line.
//
unsigned SubtargetEmitter::FeatureKeyValues(raw_ostream &OS) {
  // Gather and sort all the features
  std::vector<Record*> FeatureList =
                           Records.getAllDerivedDefinitions("SubtargetFeature");

  if (FeatureList.empty())
    return 0;

  llvm::sort(FeatureList, LessRecordFieldName());

  // Begin feature table
  OS << "// Sorted (by key) array of values for CPU features.\n"
     << "extern const llvm::SubtargetFeatureKV " << Target
     << "FeatureKV[] = {\n";

  // For each feature
  unsigned NumFeatures = 0;
  for (unsigned i = 0, N = FeatureList.size(); i < N; ++i) {
    // Next feature
    Record *Feature = FeatureList[i];

    StringRef Name = Feature->getName();
    StringRef CommandLineName = Feature->getValueAsString("Name");
    StringRef Desc = Feature->getValueAsString("Desc");

    if (CommandLineName.empty()) continue;

    // Emit as { "feature", "description", { featureEnum }, { i1 , i2 , ... , in } }
    OS << "  { "
       << "\"" << CommandLineName << "\", "
       << "\"" << Desc << "\", "
       << "{ " << Target << "::" << Name << " }, ";

    RecVec ImpliesList = Feature->getValueAsListOfDefs("Implies");

    OS << "{";
    for (unsigned j = 0, M = ImpliesList.size(); j < M;) {
      OS << " " << Target << "::" << ImpliesList[j]->getName();
      if (++j < M) OS << ",";
    }
    OS << " } },\n";
    ++NumFeatures;
  }

  // End feature table
  OS << "};\n";

  return NumFeatures;
}

//
// CPUKeyValues - Emit data of all the subtarget processors.  Used by command
// line.
//
unsigned SubtargetEmitter::CPUKeyValues(raw_ostream &OS) {
  // Gather and sort processor information
  std::vector<Record*> ProcessorList =
                          Records.getAllDerivedDefinitions("Processor");
  llvm::sort(ProcessorList, LessRecordFieldName());

  // Begin processor table
  OS << "// Sorted (by key) array of values for CPU subtype.\n"
     << "extern const llvm::SubtargetFeatureKV " << Target
     << "SubTypeKV[] = {\n";

  // For each processor
  for (Record *Processor : ProcessorList) {
    StringRef Name = Processor->getValueAsString("Name");
    RecVec FeatureList = Processor->getValueAsListOfDefs("Features");

    // Emit as { "cpu", "description", { f1 , f2 , ... fn } },
    OS << "  { "
       << "\"" << Name << "\", "
       << "\"Select the " << Name << " processor\", ";

    OS << "{";
    for (unsigned j = 0, M = FeatureList.size(); j < M;) {
      OS << " " << Target << "::" << FeatureList[j]->getName();
      if (++j < M) OS << ",";
    }
    // The { } is for the "implies" section of this data structure.
    OS << " }, { } },\n";
  }

  // End processor table
  OS << "};\n";

  return ProcessorList.size();
}

//
// FormItineraryStageString - Compose a string containing the stage
// data initialization for the specified itinerary.  N is the number
// of stages.
//
void SubtargetEmitter::FormItineraryStageString(const std::string &Name,
                                                Record *ItinData,
                                                std::string &ItinString,
                                                unsigned &NStages) {
  // Get states list
  RecVec StageList = ItinData->getValueAsListOfDefs("Stages");

  // For each stage
  unsigned N = NStages = StageList.size();
  for (unsigned i = 0; i < N;) {
    // Next stage
    const Record *Stage = StageList[i];

    // Form string as ,{ cycles, u1 | u2 | ... | un, timeinc, kind }
    int Cycles = Stage->getValueAsInt("Cycles");
    ItinString += "  { " + itostr(Cycles) + ", ";

    // Get unit list
    RecVec UnitList = Stage->getValueAsListOfDefs("Units");

    // For each unit
    for (unsigned j = 0, M = UnitList.size(); j < M;) {
      // Add name and bitwise or
      ItinString += Name + "FU::" + UnitList[j]->getName().str();
      if (++j < M) ItinString += " | ";
    }

    int TimeInc = Stage->getValueAsInt("TimeInc");
    ItinString += ", " + itostr(TimeInc);

    int Kind = Stage->getValueAsInt("Kind");
    ItinString += ", (llvm::InstrStage::ReservationKinds)" + itostr(Kind);

    // Close off stage
    ItinString += " }";
    if (++i < N) ItinString += ", ";
  }
}

//
// FormItineraryOperandCycleString - Compose a string containing the
// operand cycle initialization for the specified itinerary.  N is the
// number of operands that has cycles specified.
//
void SubtargetEmitter::FormItineraryOperandCycleString(Record *ItinData,
                         std::string &ItinString, unsigned &NOperandCycles) {
  // Get operand cycle list
  std::vector<int64_t> OperandCycleList =
    ItinData->getValueAsListOfInts("OperandCycles");

  // For each operand cycle
  unsigned N = NOperandCycles = OperandCycleList.size();
  for (unsigned i = 0; i < N;) {
    // Next operand cycle
    const int OCycle = OperandCycleList[i];

    ItinString += "  " + itostr(OCycle);
    if (++i < N) ItinString += ", ";
  }
}

void SubtargetEmitter::FormItineraryBypassString(const std::string &Name,
                                                 Record *ItinData,
                                                 std::string &ItinString,
                                                 unsigned NOperandCycles) {
  RecVec BypassList = ItinData->getValueAsListOfDefs("Bypasses");
  unsigned N = BypassList.size();
  unsigned i = 0;
  for (; i < N;) {
    ItinString += Name + "Bypass::" + BypassList[i]->getName().str();
    if (++i < NOperandCycles) ItinString += ", ";
  }
  for (; i < NOperandCycles;) {
    ItinString += " 0";
    if (++i < NOperandCycles) ItinString += ", ";
  }
}

//
// EmitStageAndOperandCycleData - Generate unique itinerary stages and operand
// cycle tables. Create a list of InstrItinerary objects (ProcItinLists) indexed
// by CodeGenSchedClass::Index.
//
void SubtargetEmitter::
EmitStageAndOperandCycleData(raw_ostream &OS,
                             std::vector<std::vector<InstrItinerary>>
                               &ProcItinLists) {
  // Multiple processor models may share an itinerary record. Emit it once.
  SmallPtrSet<Record*, 8> ItinsDefSet;

  // Emit functional units for all the itineraries.
  for (const CodeGenProcModel &ProcModel : SchedModels.procModels()) {

    if (!ItinsDefSet.insert(ProcModel.ItinsDef).second)
      continue;

    RecVec FUs = ProcModel.ItinsDef->getValueAsListOfDefs("FU");
    if (FUs.empty())
      continue;

    StringRef Name = ProcModel.ItinsDef->getName();
    OS << "\n// Functional units for \"" << Name << "\"\n"
       << "namespace " << Name << "FU {\n";

    for (unsigned j = 0, FUN = FUs.size(); j < FUN; ++j)
      OS << "  const unsigned " << FUs[j]->getName()
         << " = 1 << " << j << ";\n";

    OS << "} // end namespace " << Name << "FU\n";

    RecVec BPs = ProcModel.ItinsDef->getValueAsListOfDefs("BP");
    if (!BPs.empty()) {
      OS << "\n// Pipeline forwarding paths for itineraries \"" << Name
         << "\"\n" << "namespace " << Name << "Bypass {\n";

      OS << "  const unsigned NoBypass = 0;\n";
      for (unsigned j = 0, BPN = BPs.size(); j < BPN; ++j)
        OS << "  const unsigned " << BPs[j]->getName()
           << " = 1 << " << j << ";\n";

      OS << "} // end namespace " << Name << "Bypass\n";
    }
  }

  // Begin stages table
  std::string StageTable = "\nextern const llvm::InstrStage " + Target +
                           "Stages[] = {\n";
  StageTable += "  { 0, 0, 0, llvm::InstrStage::Required }, // No itinerary\n";

  // Begin operand cycle table
  std::string OperandCycleTable = "extern const unsigned " + Target +
    "OperandCycles[] = {\n";
  OperandCycleTable += "  0, // No itinerary\n";

  // Begin pipeline bypass table
  std::string BypassTable = "extern const unsigned " + Target +
    "ForwardingPaths[] = {\n";
  BypassTable += " 0, // No itinerary\n";

  // For each Itinerary across all processors, add a unique entry to the stages,
  // operand cycles, and pipeline bypass tables. Then add the new Itinerary
  // object with computed offsets to the ProcItinLists result.
  unsigned StageCount = 1, OperandCycleCount = 1;
  std::map<std::string, unsigned> ItinStageMap, ItinOperandMap;
  for (const CodeGenProcModel &ProcModel : SchedModels.procModels()) {
    // Add process itinerary to the list.
    ProcItinLists.resize(ProcItinLists.size()+1);

    // If this processor defines no itineraries, then leave the itinerary list
    // empty.
    std::vector<InstrItinerary> &ItinList = ProcItinLists.back();
    if (!ProcModel.hasItineraries())
      continue;

    StringRef Name = ProcModel.ItinsDef->getName();

    ItinList.resize(SchedModels.numInstrSchedClasses());
    assert(ProcModel.ItinDefList.size() == ItinList.size() && "bad Itins");

    for (unsigned SchedClassIdx = 0, SchedClassEnd = ItinList.size();
         SchedClassIdx < SchedClassEnd; ++SchedClassIdx) {

      // Next itinerary data
      Record *ItinData = ProcModel.ItinDefList[SchedClassIdx];

      // Get string and stage count
      std::string ItinStageString;
      unsigned NStages = 0;
      if (ItinData)
        FormItineraryStageString(Name, ItinData, ItinStageString, NStages);

      // Get string and operand cycle count
      std::string ItinOperandCycleString;
      unsigned NOperandCycles = 0;
      std::string ItinBypassString;
      if (ItinData) {
        FormItineraryOperandCycleString(ItinData, ItinOperandCycleString,
                                        NOperandCycles);

        FormItineraryBypassString(Name, ItinData, ItinBypassString,
                                  NOperandCycles);
      }

      // Check to see if stage already exists and create if it doesn't
      uint16_t FindStage = 0;
      if (NStages > 0) {
        FindStage = ItinStageMap[ItinStageString];
        if (FindStage == 0) {
          // Emit as { cycles, u1 | u2 | ... | un, timeinc }, // indices
          StageTable += ItinStageString + ", // " + itostr(StageCount);
          if (NStages > 1)
            StageTable += "-" + itostr(StageCount + NStages - 1);
          StageTable += "\n";
          // Record Itin class number.
          ItinStageMap[ItinStageString] = FindStage = StageCount;
          StageCount += NStages;
        }
      }

      // Check to see if operand cycle already exists and create if it doesn't
      uint16_t FindOperandCycle = 0;
      if (NOperandCycles > 0) {
        std::string ItinOperandString = ItinOperandCycleString+ItinBypassString;
        FindOperandCycle = ItinOperandMap[ItinOperandString];
        if (FindOperandCycle == 0) {
          // Emit as  cycle, // index
          OperandCycleTable += ItinOperandCycleString + ", // ";
          std::string OperandIdxComment = itostr(OperandCycleCount);
          if (NOperandCycles > 1)
            OperandIdxComment += "-"
              + itostr(OperandCycleCount + NOperandCycles - 1);
          OperandCycleTable += OperandIdxComment + "\n";
          // Record Itin class number.
          ItinOperandMap[ItinOperandCycleString] =
            FindOperandCycle = OperandCycleCount;
          // Emit as bypass, // index
          BypassTable += ItinBypassString + ", // " + OperandIdxComment + "\n";
          OperandCycleCount += NOperandCycles;
        }
      }

      // Set up itinerary as location and location + stage count
      int16_t NumUOps = ItinData ? ItinData->getValueAsInt("NumMicroOps") : 0;
      InstrItinerary Intinerary = {
          NumUOps,
          FindStage,
          uint16_t(FindStage + NStages),
          FindOperandCycle,
          uint16_t(FindOperandCycle + NOperandCycles),
      };

      // Inject - empty slots will be 0, 0
      ItinList[SchedClassIdx] = Intinerary;
    }
  }

  // Closing stage
  StageTable += "  { 0, 0, 0, llvm::InstrStage::Required } // End stages\n";
  StageTable += "};\n";

  // Closing operand cycles
  OperandCycleTable += "  0 // End operand cycles\n";
  OperandCycleTable += "};\n";

  BypassTable += " 0 // End bypass tables\n";
  BypassTable += "};\n";

  // Emit tables.
  OS << StageTable;
  OS << OperandCycleTable;
  OS << BypassTable;
}

//
// EmitProcessorData - Generate data for processor itineraries that were
// computed during EmitStageAndOperandCycleData(). ProcItinLists lists all
// Itineraries for each processor. The Itinerary lists are indexed on
// CodeGenSchedClass::Index.
//
void SubtargetEmitter::
EmitItineraries(raw_ostream &OS,
                std::vector<std::vector<InstrItinerary>> &ProcItinLists) {
  // Multiple processor models may share an itinerary record. Emit it once.
  SmallPtrSet<Record*, 8> ItinsDefSet;

  // For each processor's machine model
  std::vector<std::vector<InstrItinerary>>::iterator
      ProcItinListsIter = ProcItinLists.begin();
  for (CodeGenSchedModels::ProcIter PI = SchedModels.procModelBegin(),
         PE = SchedModels.procModelEnd(); PI != PE; ++PI, ++ProcItinListsIter) {

    Record *ItinsDef = PI->ItinsDef;
    if (!ItinsDefSet.insert(ItinsDef).second)
      continue;

    // Get the itinerary list for the processor.
    assert(ProcItinListsIter != ProcItinLists.end() && "bad iterator");
    std::vector<InstrItinerary> &ItinList = *ProcItinListsIter;

    // Empty itineraries aren't referenced anywhere in the tablegen output
    // so don't emit them.
    if (ItinList.empty())
      continue;

    OS << "\n";
    OS << "static const llvm::InstrItinerary ";

    // Begin processor itinerary table
    OS << ItinsDef->getName() << "[] = {\n";

    // For each itinerary class in CodeGenSchedClass::Index order.
    for (unsigned j = 0, M = ItinList.size(); j < M; ++j) {
      InstrItinerary &Intinerary = ItinList[j];

      // Emit Itinerary in the form of
      // { firstStage, lastStage, firstCycle, lastCycle } // index
      OS << "  { " <<
        Intinerary.NumMicroOps << ", " <<
        Intinerary.FirstStage << ", " <<
        Intinerary.LastStage << ", " <<
        Intinerary.FirstOperandCycle << ", " <<
        Intinerary.LastOperandCycle << " }" <<
        ", // " << j << " " << SchedModels.getSchedClass(j).Name << "\n";
    }
    // End processor itinerary table
    OS << "  { 0, uint16_t(~0U), uint16_t(~0U), uint16_t(~0U), uint16_t(~0U) }"
          "// end marker\n";
    OS << "};\n";
  }
}

// Emit either the value defined in the TableGen Record, or the default
// value defined in the C++ header. The Record is null if the processor does not
// define a model.
void SubtargetEmitter::EmitProcessorProp(raw_ostream &OS, const Record *R,
                                         StringRef Name, char Separator) {
  OS << "  ";
  int V = R ? R->getValueAsInt(Name) : -1;
  if (V >= 0)
    OS << V << Separator << " // " << Name;
  else
    OS << "MCSchedModel::Default" << Name << Separator;
  OS << '\n';
}

void SubtargetEmitter::EmitProcessorResourceSubUnits(
    const CodeGenProcModel &ProcModel, raw_ostream &OS) {
  OS << "\nstatic const unsigned " << ProcModel.ModelName
     << "ProcResourceSubUnits[] = {\n"
     << "  0,  // Invalid\n";

  for (unsigned i = 0, e = ProcModel.ProcResourceDefs.size(); i < e; ++i) {
    Record *PRDef = ProcModel.ProcResourceDefs[i];
    if (!PRDef->isSubClassOf("ProcResGroup"))
      continue;
    RecVec ResUnits = PRDef->getValueAsListOfDefs("Resources");
    for (Record *RUDef : ResUnits) {
      Record *const RU =
          SchedModels.findProcResUnits(RUDef, ProcModel, PRDef->getLoc());
      for (unsigned J = 0; J < RU->getValueAsInt("NumUnits"); ++J) {
        OS << "  " << ProcModel.getProcResourceIdx(RU) << ", ";
      }
    }
    OS << "  // " << PRDef->getName() << "\n";
  }
  OS << "};\n";
}

static void EmitRetireControlUnitInfo(const CodeGenProcModel &ProcModel,
                                      raw_ostream &OS) {
  int64_t ReorderBufferSize = 0, MaxRetirePerCycle = 0;
  if (Record *RCU = ProcModel.RetireControlUnit) {
    ReorderBufferSize =
        std::max(ReorderBufferSize, RCU->getValueAsInt("ReorderBufferSize"));
    MaxRetirePerCycle =
        std::max(MaxRetirePerCycle, RCU->getValueAsInt("MaxRetirePerCycle"));
  }

  OS << ReorderBufferSize << ", // ReorderBufferSize\n  ";
  OS << MaxRetirePerCycle << ", // MaxRetirePerCycle\n  ";
}

static void EmitRegisterFileInfo(const CodeGenProcModel &ProcModel,
                                 unsigned NumRegisterFiles,
                                 unsigned NumCostEntries, raw_ostream &OS) {
  if (NumRegisterFiles)
    OS << ProcModel.ModelName << "RegisterFiles,\n  " << (1 + NumRegisterFiles);
  else
    OS << "nullptr,\n  0";

  OS << ", // Number of register files.\n  ";
  if (NumCostEntries)
    OS << ProcModel.ModelName << "RegisterCosts,\n  ";
  else
    OS << "nullptr,\n  ";
  OS << NumCostEntries << ", // Number of register cost entries.\n";
}

unsigned
SubtargetEmitter::EmitRegisterFileTables(const CodeGenProcModel &ProcModel,
                                         raw_ostream &OS) {
  if (llvm::all_of(ProcModel.RegisterFiles, [](const CodeGenRegisterFile &RF) {
        return RF.hasDefaultCosts();
      }))
    return 0;

  // Print the RegisterCost table first.
  OS << "\n// {RegisterClassID, Register Cost, AllowMoveElimination }\n";
  OS << "static const llvm::MCRegisterCostEntry " << ProcModel.ModelName
     << "RegisterCosts"
     << "[] = {\n";

  for (const CodeGenRegisterFile &RF : ProcModel.RegisterFiles) {
    // Skip register files with a default cost table.
    if (RF.hasDefaultCosts())
      continue;
    // Add entries to the cost table.
    for (const CodeGenRegisterCost &RC : RF.Costs) {
      OS << "  { ";
      Record *Rec = RC.RCDef;
      if (Rec->getValue("Namespace"))
        OS << Rec->getValueAsString("Namespace") << "::";
      OS << Rec->getName() << "RegClassID, " << RC.Cost << ", "
         << RC.AllowMoveElimination << "},\n";
    }
  }
  OS << "};\n";

  // Now generate a table with register file info.
  OS << "\n // {Name, #PhysRegs, #CostEntries, IndexToCostTbl, "
     << "MaxMovesEliminatedPerCycle, AllowZeroMoveEliminationOnly }\n";
  OS << "static const llvm::MCRegisterFileDesc " << ProcModel.ModelName
     << "RegisterFiles"
     << "[] = {\n"
     << "  { \"InvalidRegisterFile\", 0, 0, 0, 0, 0 },\n";
  unsigned CostTblIndex = 0;

  for (const CodeGenRegisterFile &RD : ProcModel.RegisterFiles) {
    OS << "  { ";
    OS << '"' << RD.Name << '"' << ", " << RD.NumPhysRegs << ", ";
    unsigned NumCostEntries = RD.Costs.size();
    OS << NumCostEntries << ", " << CostTblIndex << ", "
       << RD.MaxMovesEliminatedPerCycle << ", "
       << RD.AllowZeroMoveEliminationOnly << "},\n";
    CostTblIndex += NumCostEntries;
  }
  OS << "};\n";

  return CostTblIndex;
}

void SubtargetEmitter::EmitLoadStoreQueueInfo(const CodeGenProcModel &ProcModel,
                                              raw_ostream &OS) {
  unsigned QueueID = 0;
  if (ProcModel.LoadQueue) {
    const Record *Queue = ProcModel.LoadQueue->getValueAsDef("QueueDescriptor");
    QueueID =
        1 + std::distance(ProcModel.ProcResourceDefs.begin(),
                          std::find(ProcModel.ProcResourceDefs.begin(),
                                    ProcModel.ProcResourceDefs.end(), Queue));
  }
  OS << "  " << QueueID << ", // Resource Descriptor for the Load Queue\n";

  QueueID = 0;
  if (ProcModel.StoreQueue) {
    const Record *Queue =
        ProcModel.StoreQueue->getValueAsDef("QueueDescriptor");
    QueueID =
        1 + std::distance(ProcModel.ProcResourceDefs.begin(),
                          std::find(ProcModel.ProcResourceDefs.begin(),
                                    ProcModel.ProcResourceDefs.end(), Queue));
  }
  OS << "  " << QueueID << ", // Resource Descriptor for the Store Queue\n";
}

void SubtargetEmitter::EmitExtraProcessorInfo(const CodeGenProcModel &ProcModel,
                                              raw_ostream &OS) {
  // Generate a table of register file descriptors (one entry per each user
  // defined register file), and a table of register costs.
  unsigned NumCostEntries = EmitRegisterFileTables(ProcModel, OS);

  // Now generate a table for the extra processor info.
  OS << "\nstatic const llvm::MCExtraProcessorInfo " << ProcModel.ModelName
     << "ExtraInfo = {\n  ";

  // Add information related to the retire control unit.
  EmitRetireControlUnitInfo(ProcModel, OS);

  // Add information related to the register files (i.e. where to find register
  // file descriptors and register costs).
  EmitRegisterFileInfo(ProcModel, ProcModel.RegisterFiles.size(),
                       NumCostEntries, OS);

  // Add information about load/store queues.
  EmitLoadStoreQueueInfo(ProcModel, OS);

  OS << "};\n";
}

void SubtargetEmitter::EmitProcessorResources(const CodeGenProcModel &ProcModel,
                                              raw_ostream &OS) {
  EmitProcessorResourceSubUnits(ProcModel, OS);

  OS << "\n// {Name, NumUnits, SuperIdx, BufferSize, SubUnitsIdxBegin}\n";
  OS << "static const llvm::MCProcResourceDesc " << ProcModel.ModelName
     << "ProcResources"
     << "[] = {\n"
     << "  {\"InvalidUnit\", 0, 0, 0, 0},\n";

  unsigned SubUnitsOffset = 1;
  for (unsigned i = 0, e = ProcModel.ProcResourceDefs.size(); i < e; ++i) {
    Record *PRDef = ProcModel.ProcResourceDefs[i];

    Record *SuperDef = nullptr;
    unsigned SuperIdx = 0;
    unsigned NumUnits = 0;
    const unsigned SubUnitsBeginOffset = SubUnitsOffset;
    int BufferSize = PRDef->getValueAsInt("BufferSize");
    if (PRDef->isSubClassOf("ProcResGroup")) {
      RecVec ResUnits = PRDef->getValueAsListOfDefs("Resources");
      for (Record *RU : ResUnits) {
        NumUnits += RU->getValueAsInt("NumUnits");
        SubUnitsOffset += RU->getValueAsInt("NumUnits");
      }
    }
    else {
      // Find the SuperIdx
      if (PRDef->getValueInit("Super")->isComplete()) {
        SuperDef =
            SchedModels.findProcResUnits(PRDef->getValueAsDef("Super"),
                                         ProcModel, PRDef->getLoc());
        SuperIdx = ProcModel.getProcResourceIdx(SuperDef);
      }
      NumUnits = PRDef->getValueAsInt("NumUnits");
    }
    // Emit the ProcResourceDesc
    OS << "  {\"" << PRDef->getName() << "\", ";
    if (PRDef->getName().size() < 15)
      OS.indent(15 - PRDef->getName().size());
    OS << NumUnits << ", " << SuperIdx << ", " << BufferSize << ", ";
    if (SubUnitsBeginOffset != SubUnitsOffset) {
      OS << ProcModel.ModelName << "ProcResourceSubUnits + "
         << SubUnitsBeginOffset;
    } else {
      OS << "nullptr";
    }
    OS << "}, // #" << i+1;
    if (SuperDef)
      OS << ", Super=" << SuperDef->getName();
    OS << "\n";
  }
  OS << "};\n";
}

// Find the WriteRes Record that defines processor resources for this
// SchedWrite.
Record *SubtargetEmitter::FindWriteResources(
  const CodeGenSchedRW &SchedWrite, const CodeGenProcModel &ProcModel) {

  // Check if the SchedWrite is already subtarget-specific and directly
  // specifies a set of processor resources.
  if (SchedWrite.TheDef->isSubClassOf("SchedWriteRes"))
    return SchedWrite.TheDef;

  Record *AliasDef = nullptr;
  for (Record *A : SchedWrite.Aliases) {
    const CodeGenSchedRW &AliasRW =
      SchedModels.getSchedRW(A->getValueAsDef("AliasRW"));
    if (AliasRW.TheDef->getValueInit("SchedModel")->isComplete()) {
      Record *ModelDef = AliasRW.TheDef->getValueAsDef("SchedModel");
      if (&SchedModels.getProcModel(ModelDef) != &ProcModel)
        continue;
    }
    if (AliasDef)
      PrintFatalError(AliasRW.TheDef->getLoc(), "Multiple aliases "
                    "defined for processor " + ProcModel.ModelName +
                    " Ensure only one SchedAlias exists per RW.");
    AliasDef = AliasRW.TheDef;
  }
  if (AliasDef && AliasDef->isSubClassOf("SchedWriteRes"))
    return AliasDef;

  // Check this processor's list of write resources.
  Record *ResDef = nullptr;
  for (Record *WR : ProcModel.WriteResDefs) {
    if (!WR->isSubClassOf("WriteRes"))
      continue;
    if (AliasDef == WR->getValueAsDef("WriteType")
        || SchedWrite.TheDef == WR->getValueAsDef("WriteType")) {
      if (ResDef) {
        PrintFatalError(WR->getLoc(), "Resources are defined for both "
                      "SchedWrite and its alias on processor " +
                      ProcModel.ModelName);
      }
      ResDef = WR;
    }
  }
  // TODO: If ProcModel has a base model (previous generation processor),
  // then call FindWriteResources recursively with that model here.
  if (!ResDef) {
    PrintFatalError(ProcModel.ModelDef->getLoc(),
                    Twine("Processor does not define resources for ") +
                    SchedWrite.TheDef->getName());
  }
  return ResDef;
}

/// Find the ReadAdvance record for the given SchedRead on this processor or
/// return NULL.
Record *SubtargetEmitter::FindReadAdvance(const CodeGenSchedRW &SchedRead,
                                          const CodeGenProcModel &ProcModel) {
  // Check for SchedReads that directly specify a ReadAdvance.
  if (SchedRead.TheDef->isSubClassOf("SchedReadAdvance"))
    return SchedRead.TheDef;

  // Check this processor's list of aliases for SchedRead.
  Record *AliasDef = nullptr;
  for (Record *A : SchedRead.Aliases) {
    const CodeGenSchedRW &AliasRW =
      SchedModels.getSchedRW(A->getValueAsDef("AliasRW"));
    if (AliasRW.TheDef->getValueInit("SchedModel")->isComplete()) {
      Record *ModelDef = AliasRW.TheDef->getValueAsDef("SchedModel");
      if (&SchedModels.getProcModel(ModelDef) != &ProcModel)
        continue;
    }
    if (AliasDef)
      PrintFatalError(AliasRW.TheDef->getLoc(), "Multiple aliases "
                    "defined for processor " + ProcModel.ModelName +
                    " Ensure only one SchedAlias exists per RW.");
    AliasDef = AliasRW.TheDef;
  }
  if (AliasDef && AliasDef->isSubClassOf("SchedReadAdvance"))
    return AliasDef;

  // Check this processor's ReadAdvanceList.
  Record *ResDef = nullptr;
  for (Record *RA : ProcModel.ReadAdvanceDefs) {
    if (!RA->isSubClassOf("ReadAdvance"))
      continue;
    if (AliasDef == RA->getValueAsDef("ReadType")
        || SchedRead.TheDef == RA->getValueAsDef("ReadType")) {
      if (ResDef) {
        PrintFatalError(RA->getLoc(), "Resources are defined for both "
                      "SchedRead and its alias on processor " +
                      ProcModel.ModelName);
      }
      ResDef = RA;
    }
  }
  // TODO: If ProcModel has a base model (previous generation processor),
  // then call FindReadAdvance recursively with that model here.
  if (!ResDef && SchedRead.TheDef->getName() != "ReadDefault") {
    PrintFatalError(ProcModel.ModelDef->getLoc(),
                    Twine("Processor does not define resources for ") +
                    SchedRead.TheDef->getName());
  }
  return ResDef;
}

// Expand an explicit list of processor resources into a full list of implied
// resource groups and super resources that cover them.
void SubtargetEmitter::ExpandProcResources(RecVec &PRVec,
                                           std::vector<int64_t> &Cycles,
                                           const CodeGenProcModel &PM) {
  assert(PRVec.size() == Cycles.size() && "failed precondition");
  for (unsigned i = 0, e = PRVec.size(); i != e; ++i) {
    Record *PRDef = PRVec[i];
    RecVec SubResources;
    if (PRDef->isSubClassOf("ProcResGroup"))
      SubResources = PRDef->getValueAsListOfDefs("Resources");
    else {
      SubResources.push_back(PRDef);
      PRDef = SchedModels.findProcResUnits(PRDef, PM, PRDef->getLoc());
      for (Record *SubDef = PRDef;
           SubDef->getValueInit("Super")->isComplete();) {
        if (SubDef->isSubClassOf("ProcResGroup")) {
          // Disallow this for simplicitly.
          PrintFatalError(SubDef->getLoc(), "Processor resource group "
                          " cannot be a super resources.");
        }
        Record *SuperDef =
            SchedModels.findProcResUnits(SubDef->getValueAsDef("Super"), PM,
                                         SubDef->getLoc());
        PRVec.push_back(SuperDef);
        Cycles.push_back(Cycles[i]);
        SubDef = SuperDef;
      }
    }
    for (Record *PR : PM.ProcResourceDefs) {
      if (PR == PRDef || !PR->isSubClassOf("ProcResGroup"))
        continue;
      RecVec SuperResources = PR->getValueAsListOfDefs("Resources");
      RecIter SubI = SubResources.begin(), SubE = SubResources.end();
      for( ; SubI != SubE; ++SubI) {
        if (!is_contained(SuperResources, *SubI)) {
          break;
        }
      }
      if (SubI == SubE) {
        PRVec.push_back(PR);
        Cycles.push_back(Cycles[i]);
      }
    }
  }
}

// Generate the SchedClass table for this processor and update global
// tables. Must be called for each processor in order.
void SubtargetEmitter::GenSchedClassTables(const CodeGenProcModel &ProcModel,
                                           SchedClassTables &SchedTables) {
  SchedTables.ProcSchedClasses.resize(SchedTables.ProcSchedClasses.size() + 1);
  if (!ProcModel.hasInstrSchedModel())
    return;

  std::vector<MCSchedClassDesc> &SCTab = SchedTables.ProcSchedClasses.back();
  LLVM_DEBUG(dbgs() << "\n+++ SCHED CLASSES (GenSchedClassTables) +++\n");
  for (const CodeGenSchedClass &SC : SchedModels.schedClasses()) {
    LLVM_DEBUG(SC.dump(&SchedModels));

    SCTab.resize(SCTab.size() + 1);
    MCSchedClassDesc &SCDesc = SCTab.back();
    // SCDesc.Name is guarded by NDEBUG
    SCDesc.NumMicroOps = 0;
    SCDesc.BeginGroup = false;
    SCDesc.EndGroup = false;
    SCDesc.WriteProcResIdx = 0;
    SCDesc.WriteLatencyIdx = 0;
    SCDesc.ReadAdvanceIdx = 0;

    // A Variant SchedClass has no resources of its own.
    bool HasVariants = false;
    for (const CodeGenSchedTransition &CGT :
           make_range(SC.Transitions.begin(), SC.Transitions.end())) {
      if (CGT.ProcIndices[0] == 0 ||
          is_contained(CGT.ProcIndices, ProcModel.Index)) {
        HasVariants = true;
        break;
      }
    }
    if (HasVariants) {
      SCDesc.NumMicroOps = MCSchedClassDesc::VariantNumMicroOps;
      continue;
    }

    // Determine if the SchedClass is actually reachable on this processor. If
    // not don't try to locate the processor resources, it will fail.
    // If ProcIndices contains 0, this class applies to all processors.
    assert(!SC.ProcIndices.empty() && "expect at least one procidx");
    if (SC.ProcIndices[0] != 0) {
      if (!is_contained(SC.ProcIndices, ProcModel.Index))
        continue;
    }
    IdxVec Writes = SC.Writes;
    IdxVec Reads = SC.Reads;
    if (!SC.InstRWs.empty()) {
      // This class has a default ReadWrite list which can be overridden by
      // InstRW definitions.
      Record *RWDef = nullptr;
      for (Record *RW : SC.InstRWs) {
        Record *RWModelDef = RW->getValueAsDef("SchedModel");
        if (&ProcModel == &SchedModels.getProcModel(RWModelDef)) {
          RWDef = RW;
          break;
        }
      }
      if (RWDef) {
        Writes.clear();
        Reads.clear();
        SchedModels.findRWs(RWDef->getValueAsListOfDefs("OperandReadWrites"),
                            Writes, Reads);
      }
    }
    if (Writes.empty()) {
      // Check this processor's itinerary class resources.
      for (Record *I : ProcModel.ItinRWDefs) {
        RecVec Matched = I->getValueAsListOfDefs("MatchedItinClasses");
        if (is_contained(Matched, SC.ItinClassDef)) {
          SchedModels.findRWs(I->getValueAsListOfDefs("OperandReadWrites"),
                              Writes, Reads);
          break;
        }
      }
      if (Writes.empty()) {
        LLVM_DEBUG(dbgs() << ProcModel.ModelName
                          << " does not have resources for class " << SC.Name
                          << '\n');
      }
    }
    // Sum resources across all operand writes.
    std::vector<MCWriteProcResEntry> WriteProcResources;
    std::vector<MCWriteLatencyEntry> WriteLatencies;
    std::vector<std::string> WriterNames;
    std::vector<MCReadAdvanceEntry> ReadAdvanceEntries;
    for (unsigned W : Writes) {
      IdxVec WriteSeq;
      SchedModels.expandRWSeqForProc(W, WriteSeq, /*IsRead=*/false,
                                     ProcModel);

      // For each operand, create a latency entry.
      MCWriteLatencyEntry WLEntry;
      WLEntry.Cycles = 0;
      unsigned WriteID = WriteSeq.back();
      WriterNames.push_back(SchedModels.getSchedWrite(WriteID).Name);
      // If this Write is not referenced by a ReadAdvance, don't distinguish it
      // from other WriteLatency entries.
      if (!SchedModels.hasReadOfWrite(
            SchedModels.getSchedWrite(WriteID).TheDef)) {
        WriteID = 0;
      }
      WLEntry.WriteResourceID = WriteID;

      for (unsigned WS : WriteSeq) {

        Record *WriteRes =
          FindWriteResources(SchedModels.getSchedWrite(WS), ProcModel);

        // Mark the parent class as invalid for unsupported write types.
        if (WriteRes->getValueAsBit("Unsupported")) {
          SCDesc.NumMicroOps = MCSchedClassDesc::InvalidNumMicroOps;
          break;
        }
        WLEntry.Cycles += WriteRes->getValueAsInt("Latency");
        SCDesc.NumMicroOps += WriteRes->getValueAsInt("NumMicroOps");
        SCDesc.BeginGroup |= WriteRes->getValueAsBit("BeginGroup");
        SCDesc.EndGroup |= WriteRes->getValueAsBit("EndGroup");
        SCDesc.BeginGroup |= WriteRes->getValueAsBit("SingleIssue");
        SCDesc.EndGroup |= WriteRes->getValueAsBit("SingleIssue");

        // Create an entry for each ProcResource listed in WriteRes.
        RecVec PRVec = WriteRes->getValueAsListOfDefs("ProcResources");
        std::vector<int64_t> Cycles =
          WriteRes->getValueAsListOfInts("ResourceCycles");

        if (Cycles.empty()) {
          // If ResourceCycles is not provided, default to one cycle per
          // resource.
          Cycles.resize(PRVec.size(), 1);
        } else if (Cycles.size() != PRVec.size()) {
          // If ResourceCycles is provided, check consistency.
          PrintFatalError(
              WriteRes->getLoc(),
              Twine("Inconsistent resource cycles: !size(ResourceCycles) != "
                    "!size(ProcResources): ")
                  .concat(Twine(PRVec.size()))
                  .concat(" vs ")
                  .concat(Twine(Cycles.size())));
        }

        ExpandProcResources(PRVec, Cycles, ProcModel);

        for (unsigned PRIdx = 0, PREnd = PRVec.size();
             PRIdx != PREnd; ++PRIdx) {
          MCWriteProcResEntry WPREntry;
          WPREntry.ProcResourceIdx = ProcModel.getProcResourceIdx(PRVec[PRIdx]);
          assert(WPREntry.ProcResourceIdx && "Bad ProcResourceIdx");
          WPREntry.Cycles = Cycles[PRIdx];
          // If this resource is already used in this sequence, add the current
          // entry's cycles so that the same resource appears to be used
          // serially, rather than multiple parallel uses. This is important for
          // in-order machine where the resource consumption is a hazard.
          unsigned WPRIdx = 0, WPREnd = WriteProcResources.size();
          for( ; WPRIdx != WPREnd; ++WPRIdx) {
            if (WriteProcResources[WPRIdx].ProcResourceIdx
                == WPREntry.ProcResourceIdx) {
              WriteProcResources[WPRIdx].Cycles += WPREntry.Cycles;
              break;
            }
          }
          if (WPRIdx == WPREnd)
            WriteProcResources.push_back(WPREntry);
        }
      }
      WriteLatencies.push_back(WLEntry);
    }
    // Create an entry for each operand Read in this SchedClass.
    // Entries must be sorted first by UseIdx then by WriteResourceID.
    for (unsigned UseIdx = 0, EndIdx = Reads.size();
         UseIdx != EndIdx; ++UseIdx) {
      Record *ReadAdvance =
        FindReadAdvance(SchedModels.getSchedRead(Reads[UseIdx]), ProcModel);
      if (!ReadAdvance)
        continue;

      // Mark the parent class as invalid for unsupported write types.
      if (ReadAdvance->getValueAsBit("Unsupported")) {
        SCDesc.NumMicroOps = MCSchedClassDesc::InvalidNumMicroOps;
        break;
      }
      RecVec ValidWrites = ReadAdvance->getValueAsListOfDefs("ValidWrites");
      IdxVec WriteIDs;
      if (ValidWrites.empty())
        WriteIDs.push_back(0);
      else {
        for (Record *VW : ValidWrites) {
          WriteIDs.push_back(SchedModels.getSchedRWIdx(VW, /*IsRead=*/false));
        }
      }
      llvm::sort(WriteIDs);
      for(unsigned W : WriteIDs) {
        MCReadAdvanceEntry RAEntry;
        RAEntry.UseIdx = UseIdx;
        RAEntry.WriteResourceID = W;
        RAEntry.Cycles = ReadAdvance->getValueAsInt("Cycles");
        ReadAdvanceEntries.push_back(RAEntry);
      }
    }
    if (SCDesc.NumMicroOps == MCSchedClassDesc::InvalidNumMicroOps) {
      WriteProcResources.clear();
      WriteLatencies.clear();
      ReadAdvanceEntries.clear();
    }
    // Add the information for this SchedClass to the global tables using basic
    // compression.
    //
    // WritePrecRes entries are sorted by ProcResIdx.
    llvm::sort(WriteProcResources, LessWriteProcResources());

    SCDesc.NumWriteProcResEntries = WriteProcResources.size();
    std::vector<MCWriteProcResEntry>::iterator WPRPos =
      std::search(SchedTables.WriteProcResources.begin(),
                  SchedTables.WriteProcResources.end(),
                  WriteProcResources.begin(), WriteProcResources.end());
    if (WPRPos != SchedTables.WriteProcResources.end())
      SCDesc.WriteProcResIdx = WPRPos - SchedTables.WriteProcResources.begin();
    else {
      SCDesc.WriteProcResIdx = SchedTables.WriteProcResources.size();
      SchedTables.WriteProcResources.insert(WPRPos, WriteProcResources.begin(),
                                            WriteProcResources.end());
    }
    // Latency entries must remain in operand order.
    SCDesc.NumWriteLatencyEntries = WriteLatencies.size();
    std::vector<MCWriteLatencyEntry>::iterator WLPos =
      std::search(SchedTables.WriteLatencies.begin(),
                  SchedTables.WriteLatencies.end(),
                  WriteLatencies.begin(), WriteLatencies.end());
    if (WLPos != SchedTables.WriteLatencies.end()) {
      unsigned idx = WLPos - SchedTables.WriteLatencies.begin();
      SCDesc.WriteLatencyIdx = idx;
      for (unsigned i = 0, e = WriteLatencies.size(); i < e; ++i)
        if (SchedTables.WriterNames[idx + i].find(WriterNames[i]) ==
            std::string::npos) {
          SchedTables.WriterNames[idx + i] += std::string("_") + WriterNames[i];
        }
    }
    else {
      SCDesc.WriteLatencyIdx = SchedTables.WriteLatencies.size();
      SchedTables.WriteLatencies.insert(SchedTables.WriteLatencies.end(),
                                        WriteLatencies.begin(),
                                        WriteLatencies.end());
      SchedTables.WriterNames.insert(SchedTables.WriterNames.end(),
                                     WriterNames.begin(), WriterNames.end());
    }
    // ReadAdvanceEntries must remain in operand order.
    SCDesc.NumReadAdvanceEntries = ReadAdvanceEntries.size();
    std::vector<MCReadAdvanceEntry>::iterator RAPos =
      std::search(SchedTables.ReadAdvanceEntries.begin(),
                  SchedTables.ReadAdvanceEntries.end(),
                  ReadAdvanceEntries.begin(), ReadAdvanceEntries.end());
    if (RAPos != SchedTables.ReadAdvanceEntries.end())
      SCDesc.ReadAdvanceIdx = RAPos - SchedTables.ReadAdvanceEntries.begin();
    else {
      SCDesc.ReadAdvanceIdx = SchedTables.ReadAdvanceEntries.size();
      SchedTables.ReadAdvanceEntries.insert(RAPos, ReadAdvanceEntries.begin(),
                                            ReadAdvanceEntries.end());
    }
  }
}

// Emit SchedClass tables for all processors and associated global tables.
void SubtargetEmitter::EmitSchedClassTables(SchedClassTables &SchedTables,
                                            raw_ostream &OS) {
  // Emit global WriteProcResTable.
  OS << "\n// {ProcResourceIdx, Cycles}\n"
     << "extern const llvm::MCWriteProcResEntry "
     << Target << "WriteProcResTable[] = {\n"
     << "  { 0,  0}, // Invalid\n";
  for (unsigned WPRIdx = 1, WPREnd = SchedTables.WriteProcResources.size();
       WPRIdx != WPREnd; ++WPRIdx) {
    MCWriteProcResEntry &WPREntry = SchedTables.WriteProcResources[WPRIdx];
    OS << "  {" << format("%2d", WPREntry.ProcResourceIdx) << ", "
       << format("%2d", WPREntry.Cycles) << "}";
    if (WPRIdx + 1 < WPREnd)
      OS << ',';
    OS << " // #" << WPRIdx << '\n';
  }
  OS << "}; // " << Target << "WriteProcResTable\n";

  // Emit global WriteLatencyTable.
  OS << "\n// {Cycles, WriteResourceID}\n"
     << "extern const llvm::MCWriteLatencyEntry "
     << Target << "WriteLatencyTable[] = {\n"
     << "  { 0,  0}, // Invalid\n";
  for (unsigned WLIdx = 1, WLEnd = SchedTables.WriteLatencies.size();
       WLIdx != WLEnd; ++WLIdx) {
    MCWriteLatencyEntry &WLEntry = SchedTables.WriteLatencies[WLIdx];
    OS << "  {" << format("%2d", WLEntry.Cycles) << ", "
       << format("%2d", WLEntry.WriteResourceID) << "}";
    if (WLIdx + 1 < WLEnd)
      OS << ',';
    OS << " // #" << WLIdx << " " << SchedTables.WriterNames[WLIdx] << '\n';
  }
  OS << "}; // " << Target << "WriteLatencyTable\n";

  // Emit global ReadAdvanceTable.
  OS << "\n// {UseIdx, WriteResourceID, Cycles}\n"
     << "extern const llvm::MCReadAdvanceEntry "
     << Target << "ReadAdvanceTable[] = {\n"
     << "  {0,  0,  0}, // Invalid\n";
  for (unsigned RAIdx = 1, RAEnd = SchedTables.ReadAdvanceEntries.size();
       RAIdx != RAEnd; ++RAIdx) {
    MCReadAdvanceEntry &RAEntry = SchedTables.ReadAdvanceEntries[RAIdx];
    OS << "  {" << RAEntry.UseIdx << ", "
       << format("%2d", RAEntry.WriteResourceID) << ", "
       << format("%2d", RAEntry.Cycles) << "}";
    if (RAIdx + 1 < RAEnd)
      OS << ',';
    OS << " // #" << RAIdx << '\n';
  }
  OS << "}; // " << Target << "ReadAdvanceTable\n";

  // Emit a SchedClass table for each processor.
  for (CodeGenSchedModels::ProcIter PI = SchedModels.procModelBegin(),
         PE = SchedModels.procModelEnd(); PI != PE; ++PI) {
    if (!PI->hasInstrSchedModel())
      continue;

    std::vector<MCSchedClassDesc> &SCTab =
      SchedTables.ProcSchedClasses[1 + (PI - SchedModels.procModelBegin())];

    OS << "\n// {Name, NumMicroOps, BeginGroup, EndGroup,"
       << " WriteProcResIdx,#, WriteLatencyIdx,#, ReadAdvanceIdx,#}\n";
    OS << "static const llvm::MCSchedClassDesc "
       << PI->ModelName << "SchedClasses[] = {\n";

    // The first class is always invalid. We no way to distinguish it except by
    // name and position.
    assert(SchedModels.getSchedClass(0).Name == "NoInstrModel"
           && "invalid class not first");
    OS << "  {DBGFIELD(\"InvalidSchedClass\")  "
       << MCSchedClassDesc::InvalidNumMicroOps
       << ", false, false,  0, 0,  0, 0,  0, 0},\n";

    for (unsigned SCIdx = 1, SCEnd = SCTab.size(); SCIdx != SCEnd; ++SCIdx) {
      MCSchedClassDesc &MCDesc = SCTab[SCIdx];
      const CodeGenSchedClass &SchedClass = SchedModels.getSchedClass(SCIdx);
      OS << "  {DBGFIELD(\"" << SchedClass.Name << "\") ";
      if (SchedClass.Name.size() < 18)
        OS.indent(18 - SchedClass.Name.size());
      OS << MCDesc.NumMicroOps
         << ", " << ( MCDesc.BeginGroup ? "true" : "false" )
         << ", " << ( MCDesc.EndGroup ? "true" : "false" )
         << ", " << format("%2d", MCDesc.WriteProcResIdx)
         << ", " << MCDesc.NumWriteProcResEntries
         << ", " << format("%2d", MCDesc.WriteLatencyIdx)
         << ", " << MCDesc.NumWriteLatencyEntries
         << ", " << format("%2d", MCDesc.ReadAdvanceIdx)
         << ", " << MCDesc.NumReadAdvanceEntries
         << "}, // #" << SCIdx << '\n';
    }
    OS << "}; // " << PI->ModelName << "SchedClasses\n";
  }
}

void SubtargetEmitter::EmitProcessorModels(raw_ostream &OS) {
  // For each processor model.
  for (const CodeGenProcModel &PM : SchedModels.procModels()) {
    // Emit extra processor info if available.
    if (PM.hasExtraProcessorInfo())
      EmitExtraProcessorInfo(PM, OS);
    // Emit processor resource table.
    if (PM.hasInstrSchedModel())
      EmitProcessorResources(PM, OS);
    else if(!PM.ProcResourceDefs.empty())
      PrintFatalError(PM.ModelDef->getLoc(), "SchedMachineModel defines "
                    "ProcResources without defining WriteRes SchedWriteRes");

    // Begin processor itinerary properties
    OS << "\n";
    OS << "static const llvm::MCSchedModel " << PM.ModelName << " = {\n";
    EmitProcessorProp(OS, PM.ModelDef, "IssueWidth", ',');
    EmitProcessorProp(OS, PM.ModelDef, "MicroOpBufferSize", ',');
    EmitProcessorProp(OS, PM.ModelDef, "LoopMicroOpBufferSize", ',');
    EmitProcessorProp(OS, PM.ModelDef, "LoadLatency", ',');
    EmitProcessorProp(OS, PM.ModelDef, "HighLatency", ',');
    EmitProcessorProp(OS, PM.ModelDef, "MispredictPenalty", ',');

    bool PostRAScheduler =
      (PM.ModelDef ? PM.ModelDef->getValueAsBit("PostRAScheduler") : false);

    OS << "  " << (PostRAScheduler ? "true" : "false")  << ", // "
       << "PostRAScheduler\n";

    bool CompleteModel =
      (PM.ModelDef ? PM.ModelDef->getValueAsBit("CompleteModel") : false);

    OS << "  " << (CompleteModel ? "true" : "false") << ", // "
       << "CompleteModel\n";

    OS << "  " << PM.Index << ", // Processor ID\n";
    if (PM.hasInstrSchedModel())
      OS << "  " << PM.ModelName << "ProcResources" << ",\n"
         << "  " << PM.ModelName << "SchedClasses" << ",\n"
         << "  " << PM.ProcResourceDefs.size()+1 << ",\n"
         << "  " << (SchedModels.schedClassEnd()
                     - SchedModels.schedClassBegin()) << ",\n";
    else
      OS << "  nullptr, nullptr, 0, 0,"
         << " // No instruction-level machine model.\n";
    if (PM.hasItineraries())
      OS << "  " << PM.ItinsDef->getName() << ",\n";
    else
      OS << "  nullptr, // No Itinerary\n";
    if (PM.hasExtraProcessorInfo())
      OS << "  &" << PM.ModelName << "ExtraInfo,\n";
    else
      OS << "  nullptr // No extra processor descriptor\n";
    OS << "};\n";
  }
}

//
// EmitProcessorLookup - generate cpu name to sched model lookup tables.
//
void SubtargetEmitter::EmitProcessorLookup(raw_ostream &OS) {
  // Gather and sort processor information
  std::vector<Record*> ProcessorList =
                          Records.getAllDerivedDefinitions("Processor");
  llvm::sort(ProcessorList, LessRecordFieldName());

  // Begin processor->sched model table
  OS << "\n";
  OS << "// Sorted (by key) array of sched model for CPU subtype.\n"
     << "extern const llvm::SubtargetInfoKV " << Target
     << "ProcSchedKV[] = {\n";
  // For each processor
  for (Record *Processor : ProcessorList) {
    StringRef Name = Processor->getValueAsString("Name");
    const std::string &ProcModelName =
      SchedModels.getModelForProc(Processor).ModelName;

    // Emit as { "cpu", procinit },
    OS << "  { \"" << Name << "\", (const void *)&" << ProcModelName << " },\n";
  }
  // End processor->sched model table
  OS << "};\n";
}

//
// EmitSchedModel - Emits all scheduling model tables, folding common patterns.
//
void SubtargetEmitter::EmitSchedModel(raw_ostream &OS) {
  OS << "#ifdef DBGFIELD\n"
     << "#error \"<target>GenSubtargetInfo.inc requires a DBGFIELD macro\"\n"
     << "#endif\n"
     << "#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)\n"
     << "#define DBGFIELD(x) x,\n"
     << "#else\n"
     << "#define DBGFIELD(x)\n"
     << "#endif\n";

  if (SchedModels.hasItineraries()) {
    std::vector<std::vector<InstrItinerary>> ProcItinLists;
    // Emit the stage data
    EmitStageAndOperandCycleData(OS, ProcItinLists);
    EmitItineraries(OS, ProcItinLists);
  }
  OS << "\n// ===============================================================\n"
     << "// Data tables for the new per-operand machine model.\n";

  SchedClassTables SchedTables;
  for (const CodeGenProcModel &ProcModel : SchedModels.procModels()) {
    GenSchedClassTables(ProcModel, SchedTables);
  }
  EmitSchedClassTables(SchedTables, OS);

  // Emit the processor machine model
  EmitProcessorModels(OS);
  // Emit the processor lookup data
  EmitProcessorLookup(OS);

  OS << "\n#undef DBGFIELD";
}

static void emitPredicateProlog(const RecordKeeper &Records, raw_ostream &OS) {
  std::string Buffer;
  raw_string_ostream Stream(Buffer);

  // Collect all the PredicateProlog records and print them to the output
  // stream.
  std::vector<Record *> Prologs =
      Records.getAllDerivedDefinitions("PredicateProlog");
  llvm::sort(Prologs, LessRecord());
  for (Record *P : Prologs)
    Stream << P->getValueAsString("Code") << '\n';

  Stream.flush();
  OS << Buffer;
}

static void emitPredicates(const CodeGenSchedTransition &T,
                           const CodeGenSchedClass &SC, PredicateExpander &PE,
                           raw_ostream &OS) {
  std::string Buffer;
  raw_string_ostream SS(Buffer);

  auto IsTruePredicate = [](const Record *Rec) {
    return Rec->isSubClassOf("MCSchedPredicate") &&
           Rec->getValueAsDef("Pred")->isSubClassOf("MCTrue");
  };

  // If not all predicates are MCTrue, then we need an if-stmt.
  unsigned NumNonTruePreds =
      T.PredTerm.size() - count_if(T.PredTerm, IsTruePredicate);

  SS.indent(PE.getIndentLevel() * 2);

  if (NumNonTruePreds) {
    bool FirstNonTruePredicate = true;
    SS << "if (";

    PE.setIndentLevel(PE.getIndentLevel() + 2);

    for (const Record *Rec : T.PredTerm) {
      // Skip predicates that evaluate to "true".
      if (IsTruePredicate(Rec))
        continue;

      if (FirstNonTruePredicate) {
        FirstNonTruePredicate = false;
      } else {
        SS << "\n";
        SS.indent(PE.getIndentLevel() * 2);
        SS << "&& ";
      }

      if (Rec->isSubClassOf("MCSchedPredicate")) {
        PE.expandPredicate(SS, Rec->getValueAsDef("Pred"));
        continue;
      }

      // Expand this legacy predicate and wrap it around braces if there is more
      // than one predicate to expand.
      SS << ((NumNonTruePreds > 1) ? "(" : "")
         << Rec->getValueAsString("Predicate")
         << ((NumNonTruePreds > 1) ? ")" : "");
    }

    SS << ")\n"; // end of if-stmt
    PE.decreaseIndentLevel();
    SS.indent(PE.getIndentLevel() * 2);
    PE.decreaseIndentLevel();
  }

  SS << "return " << T.ToClassIdx << "; // " << SC.Name << '\n';
  SS.flush();
  OS << Buffer;
}

// Used by method `SubtargetEmitter::emitSchedModelHelpersImpl()` to generate
// epilogue code for the auto-generated helper.
void emitSchedModelHelperEpilogue(raw_ostream &OS, bool ShouldReturnZero) {
  if (ShouldReturnZero) {
    OS << "  // Don't know how to resolve this scheduling class.\n"
       << "  return 0;\n";
    return;
  }

  OS << "  report_fatal_error(\"Expected a variant SchedClass\");\n";
}

bool hasMCSchedPredicates(const CodeGenSchedTransition &T) {
  return all_of(T.PredTerm, [](const Record *Rec) {
    return Rec->isSubClassOf("MCSchedPredicate");
  });
}

void collectVariantClasses(const CodeGenSchedModels &SchedModels,
                           IdxVec &VariantClasses,
                           bool OnlyExpandMCInstPredicates) {
  for (const CodeGenSchedClass &SC : SchedModels.schedClasses()) {
    // Ignore non-variant scheduling classes.
    if (SC.Transitions.empty())
      continue;

    if (OnlyExpandMCInstPredicates) {
      // Ignore this variant scheduling class no transitions use any meaningful
      // MCSchedPredicate definitions.
      if (!any_of(SC.Transitions, [](const CodeGenSchedTransition &T) {
            return hasMCSchedPredicates(T);
          }))
        continue;
    }

    VariantClasses.push_back(SC.Index);
  }
}

void collectProcessorIndices(const CodeGenSchedClass &SC, IdxVec &ProcIndices) {
  // A variant scheduling class may define transitions for multiple
  // processors.  This function identifies wich processors are associated with
  // transition rules specified by variant class `SC`.
  for (const CodeGenSchedTransition &T : SC.Transitions) {
    IdxVec PI;
    std::set_union(T.ProcIndices.begin(), T.ProcIndices.end(),
                   ProcIndices.begin(), ProcIndices.end(),
                   std::back_inserter(PI));
    ProcIndices.swap(PI);
  }
}

void SubtargetEmitter::emitSchedModelHelpersImpl(
    raw_ostream &OS, bool OnlyExpandMCInstPredicates) {
  IdxVec VariantClasses;
  collectVariantClasses(SchedModels, VariantClasses,
                        OnlyExpandMCInstPredicates);

  if (VariantClasses.empty()) {
    emitSchedModelHelperEpilogue(OS, OnlyExpandMCInstPredicates);
    return;
  }

  // Construct a switch statement where the condition is a check on the
  // scheduling class identifier. There is a `case` for every variant class
  // defined by the processor models of this target.
  // Each `case` implements a number of rules to resolve (i.e. to transition from)
  // a variant scheduling class to another scheduling class.  Rules are
  // described by instances of CodeGenSchedTransition. Note that transitions may
  // not be valid for all processors.
  OS << "  switch (SchedClass) {\n";
  for (unsigned VC : VariantClasses) {
    IdxVec ProcIndices;
    const CodeGenSchedClass &SC = SchedModels.getSchedClass(VC);
    collectProcessorIndices(SC, ProcIndices);

    OS << "  case " << VC << ": // " << SC.Name << '\n';

    PredicateExpander PE(Target);
    PE.setByRef(false);
    PE.setExpandForMC(OnlyExpandMCInstPredicates);
    for (unsigned PI : ProcIndices) {
      OS << "    ";

      // Emit a guard on the processor ID.
      if (PI != 0) {
        OS << (OnlyExpandMCInstPredicates
                   ? "if (CPUID == "
                   : "if (SchedModel->getProcessorID() == ");
        OS << PI << ") ";
        OS << "{ // " << (SchedModels.procModelBegin() + PI)->ModelName << '\n';
      }

      // Now emit transitions associated with processor PI.
      for (const CodeGenSchedTransition &T : SC.Transitions) {
        if (PI != 0 && !count(T.ProcIndices, PI))
          continue;

        // Emit only transitions based on MCSchedPredicate, if it's the case.
        // At least the transition specified by NoSchedPred is emitted,
        // which becomes the default transition for those variants otherwise
        // not based on MCSchedPredicate.
        // FIXME: preferably, llvm-mca should instead assume a reasonable
        // default when a variant transition is not based on MCSchedPredicate
        // for a given processor.
        if (OnlyExpandMCInstPredicates && !hasMCSchedPredicates(T))
          continue;

        PE.setIndentLevel(3);
        emitPredicates(T, SchedModels.getSchedClass(T.ToClassIdx), PE, OS);
      }

      OS << "    }\n";

      if (PI == 0)
        break;
    }

    if (SC.isInferred())
      OS << "    return " << SC.Index << ";\n";
    OS << "    break;\n";
  }

  OS << "  };\n";

  emitSchedModelHelperEpilogue(OS, OnlyExpandMCInstPredicates);
}

void SubtargetEmitter::EmitSchedModelHelpers(const std::string &ClassName,
                                             raw_ostream &OS) {
  OS << "unsigned " << ClassName
     << "\n::resolveSchedClass(unsigned SchedClass, const MachineInstr *MI,"
     << " const TargetSchedModel *SchedModel) const {\n";

  // Emit the predicate prolog code.
  emitPredicateProlog(Records, OS);

  // Emit target predicates.
  emitSchedModelHelpersImpl(OS);

  OS << "} // " << ClassName << "::resolveSchedClass\n\n";

  OS << "unsigned " << ClassName
     << "\n::resolveVariantSchedClass(unsigned SchedClass, const MCInst *MI,"
     << " unsigned CPUID) const {\n"
     << "  return " << Target << "_MC"
     << "::resolveVariantSchedClassImpl(SchedClass, MI, CPUID);\n"
     << "} // " << ClassName << "::resolveVariantSchedClass\n\n";

  STIPredicateExpander PE(Target);
  PE.setClassPrefix(ClassName);
  PE.setExpandDefinition(true);
  PE.setByRef(false);
  PE.setIndentLevel(0);

  for (const STIPredicateFunction &Fn : SchedModels.getSTIPredicates())
    PE.expandSTIPredicate(OS, Fn);
}

void SubtargetEmitter::EmitHwModeCheck(const std::string &ClassName,
                                       raw_ostream &OS) {
  const CodeGenHwModes &CGH = TGT.getHwModes();
  assert(CGH.getNumModeIds() > 0);
  if (CGH.getNumModeIds() == 1)
    return;

  OS << "unsigned " << ClassName << "::getHwMode() const {\n";
  for (unsigned M = 1, NumModes = CGH.getNumModeIds(); M != NumModes; ++M) {
    const HwMode &HM = CGH.getMode(M);
    OS << "  if (checkFeatures(\"" << HM.Features
       << "\")) return " << M << ";\n";
  }
  OS << "  return 0;\n}\n";
}

//
// ParseFeaturesFunction - Produces a subtarget specific function for parsing
// the subtarget features string.
//
void SubtargetEmitter::ParseFeaturesFunction(raw_ostream &OS,
                                             unsigned NumFeatures,
                                             unsigned NumProcs) {
  std::vector<Record*> Features =
                       Records.getAllDerivedDefinitions("SubtargetFeature");
  llvm::sort(Features, LessRecord());

  OS << "// ParseSubtargetFeatures - Parses features string setting specified\n"
     << "// subtarget options.\n"
     << "void llvm::";
  OS << Target;
  OS << "Subtarget::ParseSubtargetFeatures(StringRef CPU, StringRef FS) {\n"
     << "  LLVM_DEBUG(dbgs() << \"\\nFeatures:\" << FS);\n"
     << "  LLVM_DEBUG(dbgs() << \"\\nCPU:\" << CPU << \"\\n\\n\");\n";

  if (Features.empty()) {
    OS << "}\n";
    return;
  }

  OS << "  InitMCProcessorInfo(CPU, FS);\n"
     << "  const FeatureBitset& Bits = getFeatureBits();\n";

  for (Record *R : Features) {
    // Next record
    StringRef Instance = R->getName();
    StringRef Value = R->getValueAsString("Value");
    StringRef Attribute = R->getValueAsString("Attribute");

    if (Value=="true" || Value=="false")
      OS << "  if (Bits[" << Target << "::"
         << Instance << "]) "
         << Attribute << " = " << Value << ";\n";
    else
      OS << "  if (Bits[" << Target << "::"
         << Instance << "] && "
         << Attribute << " < " << Value << ") "
         << Attribute << " = " << Value << ";\n";
  }

  OS << "}\n";
}

void SubtargetEmitter::emitGenMCSubtargetInfo(raw_ostream &OS) {
  OS << "namespace " << Target << "_MC {\n"
     << "unsigned resolveVariantSchedClassImpl(unsigned SchedClass,\n"
     << "    const MCInst *MI, unsigned CPUID) {\n";
  emitSchedModelHelpersImpl(OS, /* OnlyExpandMCPredicates */ true);
  OS << "}\n";
  OS << "} // end of namespace " << Target << "_MC\n\n";

  OS << "struct " << Target
     << "GenMCSubtargetInfo : public MCSubtargetInfo {\n";
  OS << "  " << Target << "GenMCSubtargetInfo(const Triple &TT, \n"
     << "    StringRef CPU, StringRef FS, ArrayRef<SubtargetFeatureKV> PF,\n"
     << "    ArrayRef<SubtargetFeatureKV> PD,\n"
     << "    const SubtargetInfoKV *ProcSched,\n"
     << "    const MCWriteProcResEntry *WPR,\n"
     << "    const MCWriteLatencyEntry *WL,\n"
     << "    const MCReadAdvanceEntry *RA, const InstrStage *IS,\n"
     << "    const unsigned *OC, const unsigned *FP) :\n"
     << "      MCSubtargetInfo(TT, CPU, FS, PF, PD, ProcSched,\n"
     << "                      WPR, WL, RA, IS, OC, FP) { }\n\n"
     << "  unsigned resolveVariantSchedClass(unsigned SchedClass,\n"
     << "      const MCInst *MI, unsigned CPUID) const override {\n"
     << "    return " << Target << "_MC"
     << "::resolveVariantSchedClassImpl(SchedClass, MI, CPUID); \n";
  OS << "  }\n";
  OS << "};\n";
}

void SubtargetEmitter::EmitMCInstrAnalysisPredicateFunctions(raw_ostream &OS) {
  OS << "\n#ifdef GET_STIPREDICATE_DECLS_FOR_MC_ANALYSIS\n";
  OS << "#undef GET_STIPREDICATE_DECLS_FOR_MC_ANALYSIS\n\n";

  STIPredicateExpander PE(Target);
  PE.setExpandForMC(true);
  PE.setByRef(true);
  for (const STIPredicateFunction &Fn : SchedModels.getSTIPredicates())
    PE.expandSTIPredicate(OS, Fn);

  OS << "#endif // GET_STIPREDICATE_DECLS_FOR_MC_ANALYSIS\n\n";

  OS << "\n#ifdef GET_STIPREDICATE_DEFS_FOR_MC_ANALYSIS\n";
  OS << "#undef GET_STIPREDICATE_DEFS_FOR_MC_ANALYSIS\n\n";

  std::string ClassPrefix = Target + "MCInstrAnalysis";
  PE.setExpandDefinition(true);
  PE.setClassPrefix(ClassPrefix);
  PE.setIndentLevel(0);
  for (const STIPredicateFunction &Fn : SchedModels.getSTIPredicates())
    PE.expandSTIPredicate(OS, Fn);

  OS << "#endif // GET_STIPREDICATE_DEFS_FOR_MC_ANALYSIS\n\n";
}

//
// SubtargetEmitter::run - Main subtarget enumeration emitter.
//
void SubtargetEmitter::run(raw_ostream &OS) {
  emitSourceFileHeader("Subtarget Enumeration Source Fragment", OS);

  OS << "\n#ifdef GET_SUBTARGETINFO_ENUM\n";
  OS << "#undef GET_SUBTARGETINFO_ENUM\n\n";

  OS << "namespace llvm {\n";
  Enumeration(OS);
  OS << "} // end namespace llvm\n\n";
  OS << "#endif // GET_SUBTARGETINFO_ENUM\n\n";

  OS << "\n#ifdef GET_SUBTARGETINFO_MC_DESC\n";
  OS << "#undef GET_SUBTARGETINFO_MC_DESC\n\n";

  OS << "namespace llvm {\n";
#if 0
  OS << "namespace {\n";
#endif
  unsigned NumFeatures = FeatureKeyValues(OS);
  OS << "\n";
  unsigned NumProcs = CPUKeyValues(OS);
  OS << "\n";
  EmitSchedModel(OS);
  OS << "\n";
#if 0
  OS << "} // end anonymous namespace\n\n";
#endif

  // MCInstrInfo initialization routine.
  emitGenMCSubtargetInfo(OS);

  OS << "\nstatic inline MCSubtargetInfo *create" << Target
     << "MCSubtargetInfoImpl("
     << "const Triple &TT, StringRef CPU, StringRef FS) {\n";
  OS << "  return new " << Target << "GenMCSubtargetInfo(TT, CPU, FS, ";
  if (NumFeatures)
    OS << Target << "FeatureKV, ";
  else
    OS << "None, ";
  if (NumProcs)
    OS << Target << "SubTypeKV, ";
  else
    OS << "None, ";
  OS << '\n'; OS.indent(22);
  OS << Target << "ProcSchedKV, "
     << Target << "WriteProcResTable, "
     << Target << "WriteLatencyTable, "
     << Target << "ReadAdvanceTable, ";
  OS << '\n'; OS.indent(22);
  if (SchedModels.hasItineraries()) {
    OS << Target << "Stages, "
       << Target << "OperandCycles, "
       << Target << "ForwardingPaths";
  } else
    OS << "nullptr, nullptr, nullptr";
  OS << ");\n}\n\n";

  OS << "} // end namespace llvm\n\n";

  OS << "#endif // GET_SUBTARGETINFO_MC_DESC\n\n";

  OS << "\n#ifdef GET_SUBTARGETINFO_TARGET_DESC\n";
  OS << "#undef GET_SUBTARGETINFO_TARGET_DESC\n\n";

  OS << "#include \"llvm/Support/Debug.h\"\n";
  OS << "#include \"llvm/Support/raw_ostream.h\"\n\n";
  ParseFeaturesFunction(OS, NumFeatures, NumProcs);

  OS << "#endif // GET_SUBTARGETINFO_TARGET_DESC\n\n";

  // Create a TargetSubtargetInfo subclass to hide the MC layer initialization.
  OS << "\n#ifdef GET_SUBTARGETINFO_HEADER\n";
  OS << "#undef GET_SUBTARGETINFO_HEADER\n\n";

  std::string ClassName = Target + "GenSubtargetInfo";
  OS << "namespace llvm {\n";
  OS << "class DFAPacketizer;\n";
  OS << "namespace " << Target << "_MC {\n"
     << "unsigned resolveVariantSchedClassImpl(unsigned SchedClass,"
     << " const MCInst *MI, unsigned CPUID);\n"
     << "}\n\n";
  OS << "struct " << ClassName << " : public TargetSubtargetInfo {\n"
     << "  explicit " << ClassName << "(const Triple &TT, StringRef CPU, "
     << "StringRef FS);\n"
     << "public:\n"
     << "  unsigned resolveSchedClass(unsigned SchedClass, "
     << " const MachineInstr *DefMI,"
     << " const TargetSchedModel *SchedModel) const override;\n"
     << "  unsigned resolveVariantSchedClass(unsigned SchedClass,"
     << " const MCInst *MI, unsigned CPUID) const override;\n"
     << "  DFAPacketizer *createDFAPacketizer(const InstrItineraryData *IID)"
     << " const;\n";
  if (TGT.getHwModes().getNumModeIds() > 1)
    OS << "  unsigned getHwMode() const override;\n";

  STIPredicateExpander PE(Target);
  PE.setByRef(false);
  for (const STIPredicateFunction &Fn : SchedModels.getSTIPredicates())
    PE.expandSTIPredicate(OS, Fn);

  OS << "};\n"
     << "} // end namespace llvm\n\n";

  OS << "#endif // GET_SUBTARGETINFO_HEADER\n\n";

  OS << "\n#ifdef GET_SUBTARGETINFO_CTOR\n";
  OS << "#undef GET_SUBTARGETINFO_CTOR\n\n";

  OS << "#include \"llvm/CodeGen/TargetSchedule.h\"\n\n";
  OS << "namespace llvm {\n";
  OS << "extern const llvm::SubtargetFeatureKV " << Target << "FeatureKV[];\n";
  OS << "extern const llvm::SubtargetFeatureKV " << Target << "SubTypeKV[];\n";
  OS << "extern const llvm::SubtargetInfoKV " << Target << "ProcSchedKV[];\n";
  OS << "extern const llvm::MCWriteProcResEntry "
     << Target << "WriteProcResTable[];\n";
  OS << "extern const llvm::MCWriteLatencyEntry "
     << Target << "WriteLatencyTable[];\n";
  OS << "extern const llvm::MCReadAdvanceEntry "
     << Target << "ReadAdvanceTable[];\n";

  if (SchedModels.hasItineraries()) {
    OS << "extern const llvm::InstrStage " << Target << "Stages[];\n";
    OS << "extern const unsigned " << Target << "OperandCycles[];\n";
    OS << "extern const unsigned " << Target << "ForwardingPaths[];\n";
  }

  OS << ClassName << "::" << ClassName << "(const Triple &TT, StringRef CPU, "
     << "StringRef FS)\n"
     << "  : TargetSubtargetInfo(TT, CPU, FS, ";
  if (NumFeatures)
    OS << "makeArrayRef(" << Target << "FeatureKV, " << NumFeatures << "), ";
  else
    OS << "None, ";
  if (NumProcs)
    OS << "makeArrayRef(" << Target << "SubTypeKV, " << NumProcs << "), ";
  else
    OS << "None, ";
  OS << '\n'; OS.indent(24);
  OS << Target << "ProcSchedKV, "
     << Target << "WriteProcResTable, "
     << Target << "WriteLatencyTable, "
     << Target << "ReadAdvanceTable, ";
  OS << '\n'; OS.indent(24);
  if (SchedModels.hasItineraries()) {
    OS << Target << "Stages, "
       << Target << "OperandCycles, "
       << Target << "ForwardingPaths";
  } else
    OS << "nullptr, nullptr, nullptr";
  OS << ") {}\n\n";

  EmitSchedModelHelpers(ClassName, OS);
  EmitHwModeCheck(ClassName, OS);

  OS << "} // end namespace llvm\n\n";

  OS << "#endif // GET_SUBTARGETINFO_CTOR\n\n";

  EmitMCInstrAnalysisPredicateFunctions(OS);
}

namespace llvm {

void EmitSubtarget(RecordKeeper &RK, raw_ostream &OS) {
  CodeGenTarget CGTarget(RK);
  SubtargetEmitter(RK, CGTarget).run(OS);
}

} // end namespace llvm
