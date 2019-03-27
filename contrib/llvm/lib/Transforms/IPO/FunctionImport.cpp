//===- FunctionImport.cpp - ThinLTO Summary-based Function Import ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements Function import based on summaries.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/FunctionImport.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/AutoUpgrade.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Linker/IRMover.h"
#include "llvm/Object/ModuleSymbolTable.h"
#include "llvm/Object/SymbolicFile.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/Internalize.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/FunctionImportUtils.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <cassert>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "function-import"

STATISTIC(NumImportedFunctionsThinLink,
          "Number of functions thin link decided to import");
STATISTIC(NumImportedHotFunctionsThinLink,
          "Number of hot functions thin link decided to import");
STATISTIC(NumImportedCriticalFunctionsThinLink,
          "Number of critical functions thin link decided to import");
STATISTIC(NumImportedGlobalVarsThinLink,
          "Number of global variables thin link decided to import");
STATISTIC(NumImportedFunctions, "Number of functions imported in backend");
STATISTIC(NumImportedGlobalVars,
          "Number of global variables imported in backend");
STATISTIC(NumImportedModules, "Number of modules imported from");
STATISTIC(NumDeadSymbols, "Number of dead stripped symbols in index");
STATISTIC(NumLiveSymbols, "Number of live symbols in index");

/// Limit on instruction count of imported functions.
static cl::opt<unsigned> ImportInstrLimit(
    "import-instr-limit", cl::init(100), cl::Hidden, cl::value_desc("N"),
    cl::desc("Only import functions with less than N instructions"));

static cl::opt<int> ImportCutoff(
    "import-cutoff", cl::init(-1), cl::Hidden, cl::value_desc("N"),
    cl::desc("Only import first N functions if N>=0 (default -1)"));

static cl::opt<float>
    ImportInstrFactor("import-instr-evolution-factor", cl::init(0.7),
                      cl::Hidden, cl::value_desc("x"),
                      cl::desc("As we import functions, multiply the "
                               "`import-instr-limit` threshold by this factor "
                               "before processing newly imported functions"));

static cl::opt<float> ImportHotInstrFactor(
    "import-hot-evolution-factor", cl::init(1.0), cl::Hidden,
    cl::value_desc("x"),
    cl::desc("As we import functions called from hot callsite, multiply the "
             "`import-instr-limit` threshold by this factor "
             "before processing newly imported functions"));

static cl::opt<float> ImportHotMultiplier(
    "import-hot-multiplier", cl::init(10.0), cl::Hidden, cl::value_desc("x"),
    cl::desc("Multiply the `import-instr-limit` threshold for hot callsites"));

static cl::opt<float> ImportCriticalMultiplier(
    "import-critical-multiplier", cl::init(100.0), cl::Hidden,
    cl::value_desc("x"),
    cl::desc(
        "Multiply the `import-instr-limit` threshold for critical callsites"));

// FIXME: This multiplier was not really tuned up.
static cl::opt<float> ImportColdMultiplier(
    "import-cold-multiplier", cl::init(0), cl::Hidden, cl::value_desc("N"),
    cl::desc("Multiply the `import-instr-limit` threshold for cold callsites"));

static cl::opt<bool> PrintImports("print-imports", cl::init(false), cl::Hidden,
                                  cl::desc("Print imported functions"));

static cl::opt<bool> PrintImportFailures(
    "print-import-failures", cl::init(false), cl::Hidden,
    cl::desc("Print information for functions rejected for importing"));

static cl::opt<bool> ComputeDead("compute-dead", cl::init(true), cl::Hidden,
                                 cl::desc("Compute dead symbols"));

static cl::opt<bool> EnableImportMetadata(
    "enable-import-metadata", cl::init(
#if !defined(NDEBUG)
                                  true /*Enabled with asserts.*/
#else
                                  false
#endif
                                  ),
    cl::Hidden, cl::desc("Enable import metadata like 'thinlto_src_module'"));

/// Summary file to use for function importing when using -function-import from
/// the command line.
static cl::opt<std::string>
    SummaryFile("summary-file",
                cl::desc("The summary file to use for function importing."));

/// Used when testing importing from distributed indexes via opt
// -function-import.
static cl::opt<bool>
    ImportAllIndex("import-all-index",
                   cl::desc("Import all external functions in index."));

// Load lazily a module from \p FileName in \p Context.
static std::unique_ptr<Module> loadFile(const std::string &FileName,
                                        LLVMContext &Context) {
  SMDiagnostic Err;
  LLVM_DEBUG(dbgs() << "Loading '" << FileName << "'\n");
  // Metadata isn't loaded until functions are imported, to minimize
  // the memory overhead.
  std::unique_ptr<Module> Result =
      getLazyIRFileModule(FileName, Err, Context,
                          /* ShouldLazyLoadMetadata = */ true);
  if (!Result) {
    Err.print("function-import", errs());
    report_fatal_error("Abort");
  }

  return Result;
}

/// Given a list of possible callee implementation for a call site, select one
/// that fits the \p Threshold.
///
/// FIXME: select "best" instead of first that fits. But what is "best"?
/// - The smallest: more likely to be inlined.
/// - The one with the least outgoing edges (already well optimized).
/// - One from a module already being imported from in order to reduce the
///   number of source modules parsed/linked.
/// - One that has PGO data attached.
/// - [insert you fancy metric here]
static const GlobalValueSummary *
selectCallee(const ModuleSummaryIndex &Index,
             ArrayRef<std::unique_ptr<GlobalValueSummary>> CalleeSummaryList,
             unsigned Threshold, StringRef CallerModulePath,
             FunctionImporter::ImportFailureReason &Reason,
             GlobalValue::GUID GUID) {
  Reason = FunctionImporter::ImportFailureReason::None;
  auto It = llvm::find_if(
      CalleeSummaryList,
      [&](const std::unique_ptr<GlobalValueSummary> &SummaryPtr) {
        auto *GVSummary = SummaryPtr.get();
        if (!Index.isGlobalValueLive(GVSummary)) {
          Reason = FunctionImporter::ImportFailureReason::NotLive;
          return false;
        }

        // For SamplePGO, in computeImportForFunction the OriginalId
        // may have been used to locate the callee summary list (See
        // comment there).
        // The mapping from OriginalId to GUID may return a GUID
        // that corresponds to a static variable. Filter it out here.
        // This can happen when
        // 1) There is a call to a library function which is not defined
        // in the index.
        // 2) There is a static variable with the  OriginalGUID identical
        // to the GUID of the library function in 1);
        // When this happens, the logic for SamplePGO kicks in and
        // the static variable in 2) will be found, which needs to be
        // filtered out.
        if (GVSummary->getSummaryKind() == GlobalValueSummary::GlobalVarKind) {
          Reason = FunctionImporter::ImportFailureReason::GlobalVar;
          return false;
        }
        if (GlobalValue::isInterposableLinkage(GVSummary->linkage())) {
          Reason = FunctionImporter::ImportFailureReason::InterposableLinkage;
          // There is no point in importing these, we can't inline them
          return false;
        }

        auto *Summary = cast<FunctionSummary>(GVSummary->getBaseObject());

        // If this is a local function, make sure we import the copy
        // in the caller's module. The only time a local function can
        // share an entry in the index is if there is a local with the same name
        // in another module that had the same source file name (in a different
        // directory), where each was compiled in their own directory so there
        // was not distinguishing path.
        // However, do the import from another module if there is only one
        // entry in the list - in that case this must be a reference due
        // to indirect call profile data, since a function pointer can point to
        // a local in another module.
        if (GlobalValue::isLocalLinkage(Summary->linkage()) &&
            CalleeSummaryList.size() > 1 &&
            Summary->modulePath() != CallerModulePath) {
          Reason =
              FunctionImporter::ImportFailureReason::LocalLinkageNotInModule;
          return false;
        }

        if (Summary->instCount() > Threshold) {
          Reason = FunctionImporter::ImportFailureReason::TooLarge;
          return false;
        }

        // Skip if it isn't legal to import (e.g. may reference unpromotable
        // locals).
        if (Summary->notEligibleToImport()) {
          Reason = FunctionImporter::ImportFailureReason::NotEligible;
          return false;
        }

        // Don't bother importing if we can't inline it anyway.
        if (Summary->fflags().NoInline) {
          Reason = FunctionImporter::ImportFailureReason::NoInline;
          return false;
        }

        return true;
      });
  if (It == CalleeSummaryList.end())
    return nullptr;

  return cast<GlobalValueSummary>(It->get());
}

namespace {

using EdgeInfo = std::tuple<const FunctionSummary *, unsigned /* Threshold */,
                            GlobalValue::GUID>;

} // anonymous namespace

static ValueInfo
updateValueInfoForIndirectCalls(const ModuleSummaryIndex &Index, ValueInfo VI) {
  if (!VI.getSummaryList().empty())
    return VI;
  // For SamplePGO, the indirect call targets for local functions will
  // have its original name annotated in profile. We try to find the
  // corresponding PGOFuncName as the GUID.
  // FIXME: Consider updating the edges in the graph after building
  // it, rather than needing to perform this mapping on each walk.
  auto GUID = Index.getGUIDFromOriginalID(VI.getGUID());
  if (GUID == 0)
    return ValueInfo();
  return Index.getValueInfo(GUID);
}

static void computeImportForReferencedGlobals(
    const FunctionSummary &Summary, const GVSummaryMapTy &DefinedGVSummaries,
    FunctionImporter::ImportMapTy &ImportList,
    StringMap<FunctionImporter::ExportSetTy> *ExportLists) {
  for (auto &VI : Summary.refs()) {
    if (DefinedGVSummaries.count(VI.getGUID())) {
      LLVM_DEBUG(
          dbgs() << "Ref ignored! Target already in destination module.\n");
      continue;
    }

    LLVM_DEBUG(dbgs() << " ref -> " << VI << "\n");

    // If this is a local variable, make sure we import the copy
    // in the caller's module. The only time a local variable can
    // share an entry in the index is if there is a local with the same name
    // in another module that had the same source file name (in a different
    // directory), where each was compiled in their own directory so there
    // was not distinguishing path.
    auto LocalNotInModule = [&](const GlobalValueSummary *RefSummary) -> bool {
      return GlobalValue::isLocalLinkage(RefSummary->linkage()) &&
             RefSummary->modulePath() != Summary.modulePath();
    };

    for (auto &RefSummary : VI.getSummaryList())
      if (isa<GlobalVarSummary>(RefSummary.get()) &&
          canImportGlobalVar(RefSummary.get()) &&
          !LocalNotInModule(RefSummary.get())) {
        auto ILI = ImportList[RefSummary->modulePath()].insert(VI.getGUID());
        // Only update stat if we haven't already imported this variable.
        if (ILI.second)
          NumImportedGlobalVarsThinLink++;
        if (ExportLists)
          (*ExportLists)[RefSummary->modulePath()].insert(VI.getGUID());
        break;
      }
  }
}

static const char *
getFailureName(FunctionImporter::ImportFailureReason Reason) {
  switch (Reason) {
  case FunctionImporter::ImportFailureReason::None:
    return "None";
  case FunctionImporter::ImportFailureReason::GlobalVar:
    return "GlobalVar";
  case FunctionImporter::ImportFailureReason::NotLive:
    return "NotLive";
  case FunctionImporter::ImportFailureReason::TooLarge:
    return "TooLarge";
  case FunctionImporter::ImportFailureReason::InterposableLinkage:
    return "InterposableLinkage";
  case FunctionImporter::ImportFailureReason::LocalLinkageNotInModule:
    return "LocalLinkageNotInModule";
  case FunctionImporter::ImportFailureReason::NotEligible:
    return "NotEligible";
  case FunctionImporter::ImportFailureReason::NoInline:
    return "NoInline";
  }
  llvm_unreachable("invalid reason");
}

/// Compute the list of functions to import for a given caller. Mark these
/// imported functions and the symbols they reference in their source module as
/// exported from their source module.
static void computeImportForFunction(
    const FunctionSummary &Summary, const ModuleSummaryIndex &Index,
    const unsigned Threshold, const GVSummaryMapTy &DefinedGVSummaries,
    SmallVectorImpl<EdgeInfo> &Worklist,
    FunctionImporter::ImportMapTy &ImportList,
    StringMap<FunctionImporter::ExportSetTy> *ExportLists,
    FunctionImporter::ImportThresholdsTy &ImportThresholds) {
  computeImportForReferencedGlobals(Summary, DefinedGVSummaries, ImportList,
                                    ExportLists);
  static int ImportCount = 0;
  for (auto &Edge : Summary.calls()) {
    ValueInfo VI = Edge.first;
    LLVM_DEBUG(dbgs() << " edge -> " << VI << " Threshold:" << Threshold
                      << "\n");

    if (ImportCutoff >= 0 && ImportCount >= ImportCutoff) {
      LLVM_DEBUG(dbgs() << "ignored! import-cutoff value of " << ImportCutoff
                        << " reached.\n");
      continue;
    }

    VI = updateValueInfoForIndirectCalls(Index, VI);
    if (!VI)
      continue;

    if (DefinedGVSummaries.count(VI.getGUID())) {
      LLVM_DEBUG(dbgs() << "ignored! Target already in destination module.\n");
      continue;
    }

    auto GetBonusMultiplier = [](CalleeInfo::HotnessType Hotness) -> float {
      if (Hotness == CalleeInfo::HotnessType::Hot)
        return ImportHotMultiplier;
      if (Hotness == CalleeInfo::HotnessType::Cold)
        return ImportColdMultiplier;
      if (Hotness == CalleeInfo::HotnessType::Critical)
        return ImportCriticalMultiplier;
      return 1.0;
    };

    const auto NewThreshold =
        Threshold * GetBonusMultiplier(Edge.second.getHotness());

    auto IT = ImportThresholds.insert(std::make_pair(
        VI.getGUID(), std::make_tuple(NewThreshold, nullptr, nullptr)));
    bool PreviouslyVisited = !IT.second;
    auto &ProcessedThreshold = std::get<0>(IT.first->second);
    auto &CalleeSummary = std::get<1>(IT.first->second);
    auto &FailureInfo = std::get<2>(IT.first->second);

    bool IsHotCallsite =
        Edge.second.getHotness() == CalleeInfo::HotnessType::Hot;
    bool IsCriticalCallsite =
        Edge.second.getHotness() == CalleeInfo::HotnessType::Critical;

    const FunctionSummary *ResolvedCalleeSummary = nullptr;
    if (CalleeSummary) {
      assert(PreviouslyVisited);
      // Since the traversal of the call graph is DFS, we can revisit a function
      // a second time with a higher threshold. In this case, it is added back
      // to the worklist with the new threshold (so that its own callee chains
      // can be considered with the higher threshold).
      if (NewThreshold <= ProcessedThreshold) {
        LLVM_DEBUG(
            dbgs() << "ignored! Target was already imported with Threshold "
                   << ProcessedThreshold << "\n");
        continue;
      }
      // Update with new larger threshold.
      ProcessedThreshold = NewThreshold;
      ResolvedCalleeSummary = cast<FunctionSummary>(CalleeSummary);
    } else {
      // If we already rejected importing a callee at the same or higher
      // threshold, don't waste time calling selectCallee.
      if (PreviouslyVisited && NewThreshold <= ProcessedThreshold) {
        LLVM_DEBUG(
            dbgs() << "ignored! Target was already rejected with Threshold "
            << ProcessedThreshold << "\n");
        if (PrintImportFailures) {
          assert(FailureInfo &&
                 "Expected FailureInfo for previously rejected candidate");
          FailureInfo->Attempts++;
        }
        continue;
      }

      FunctionImporter::ImportFailureReason Reason;
      CalleeSummary = selectCallee(Index, VI.getSummaryList(), NewThreshold,
                                   Summary.modulePath(), Reason, VI.getGUID());
      if (!CalleeSummary) {
        // Update with new larger threshold if this was a retry (otherwise
        // we would have already inserted with NewThreshold above). Also
        // update failure info if requested.
        if (PreviouslyVisited) {
          ProcessedThreshold = NewThreshold;
          if (PrintImportFailures) {
            assert(FailureInfo &&
                   "Expected FailureInfo for previously rejected candidate");
            FailureInfo->Reason = Reason;
            FailureInfo->Attempts++;
            FailureInfo->MaxHotness =
                std::max(FailureInfo->MaxHotness, Edge.second.getHotness());
          }
        } else if (PrintImportFailures) {
          assert(!FailureInfo &&
                 "Expected no FailureInfo for newly rejected candidate");
          FailureInfo = llvm::make_unique<FunctionImporter::ImportFailureInfo>(
              VI, Edge.second.getHotness(), Reason, 1);
        }
        LLVM_DEBUG(
            dbgs() << "ignored! No qualifying callee with summary found.\n");
        continue;
      }

      // "Resolve" the summary
      CalleeSummary = CalleeSummary->getBaseObject();
      ResolvedCalleeSummary = cast<FunctionSummary>(CalleeSummary);

      assert(ResolvedCalleeSummary->instCount() <= NewThreshold &&
             "selectCallee() didn't honor the threshold");

      auto ExportModulePath = ResolvedCalleeSummary->modulePath();
      auto ILI = ImportList[ExportModulePath].insert(VI.getGUID());
      // We previously decided to import this GUID definition if it was already
      // inserted in the set of imports from the exporting module.
      bool PreviouslyImported = !ILI.second;
      if (!PreviouslyImported) {
        NumImportedFunctionsThinLink++;
        if (IsHotCallsite)
          NumImportedHotFunctionsThinLink++;
        if (IsCriticalCallsite)
          NumImportedCriticalFunctionsThinLink++;
      }

      // Make exports in the source module.
      if (ExportLists) {
        auto &ExportList = (*ExportLists)[ExportModulePath];
        ExportList.insert(VI.getGUID());
        if (!PreviouslyImported) {
          // This is the first time this function was exported from its source
          // module, so mark all functions and globals it references as exported
          // to the outside if they are defined in the same source module.
          // For efficiency, we unconditionally add all the referenced GUIDs
          // to the ExportList for this module, and will prune out any not
          // defined in the module later in a single pass.
          for (auto &Edge : ResolvedCalleeSummary->calls()) {
            auto CalleeGUID = Edge.first.getGUID();
            ExportList.insert(CalleeGUID);
          }
          for (auto &Ref : ResolvedCalleeSummary->refs()) {
            auto GUID = Ref.getGUID();
            ExportList.insert(GUID);
          }
        }
      }
    }

    auto GetAdjustedThreshold = [](unsigned Threshold, bool IsHotCallsite) {
      // Adjust the threshold for next level of imported functions.
      // The threshold is different for hot callsites because we can then
      // inline chains of hot calls.
      if (IsHotCallsite)
        return Threshold * ImportHotInstrFactor;
      return Threshold * ImportInstrFactor;
    };

    const auto AdjThreshold = GetAdjustedThreshold(Threshold, IsHotCallsite);

    ImportCount++;

    // Insert the newly imported function to the worklist.
    Worklist.emplace_back(ResolvedCalleeSummary, AdjThreshold, VI.getGUID());
  }
}

/// Given the list of globals defined in a module, compute the list of imports
/// as well as the list of "exports", i.e. the list of symbols referenced from
/// another module (that may require promotion).
static void ComputeImportForModule(
    const GVSummaryMapTy &DefinedGVSummaries, const ModuleSummaryIndex &Index,
    StringRef ModName, FunctionImporter::ImportMapTy &ImportList,
    StringMap<FunctionImporter::ExportSetTy> *ExportLists = nullptr) {
  // Worklist contains the list of function imported in this module, for which
  // we will analyse the callees and may import further down the callgraph.
  SmallVector<EdgeInfo, 128> Worklist;
  FunctionImporter::ImportThresholdsTy ImportThresholds;

  // Populate the worklist with the import for the functions in the current
  // module
  for (auto &GVSummary : DefinedGVSummaries) {
#ifndef NDEBUG
    // FIXME: Change the GVSummaryMapTy to hold ValueInfo instead of GUID
    // so this map look up (and possibly others) can be avoided.
    auto VI = Index.getValueInfo(GVSummary.first);
#endif
    if (!Index.isGlobalValueLive(GVSummary.second)) {
      LLVM_DEBUG(dbgs() << "Ignores Dead GUID: " << VI << "\n");
      continue;
    }
    auto *FuncSummary =
        dyn_cast<FunctionSummary>(GVSummary.second->getBaseObject());
    if (!FuncSummary)
      // Skip import for global variables
      continue;
    LLVM_DEBUG(dbgs() << "Initialize import for " << VI << "\n");
    computeImportForFunction(*FuncSummary, Index, ImportInstrLimit,
                             DefinedGVSummaries, Worklist, ImportList,
                             ExportLists, ImportThresholds);
  }

  // Process the newly imported functions and add callees to the worklist.
  while (!Worklist.empty()) {
    auto FuncInfo = Worklist.pop_back_val();
    auto *Summary = std::get<0>(FuncInfo);
    auto Threshold = std::get<1>(FuncInfo);

    computeImportForFunction(*Summary, Index, Threshold, DefinedGVSummaries,
                             Worklist, ImportList, ExportLists,
                             ImportThresholds);
  }

  // Print stats about functions considered but rejected for importing
  // when requested.
  if (PrintImportFailures) {
    dbgs() << "Missed imports into module " << ModName << "\n";
    for (auto &I : ImportThresholds) {
      auto &ProcessedThreshold = std::get<0>(I.second);
      auto &CalleeSummary = std::get<1>(I.second);
      auto &FailureInfo = std::get<2>(I.second);
      if (CalleeSummary)
        continue; // We are going to import.
      assert(FailureInfo);
      FunctionSummary *FS = nullptr;
      if (!FailureInfo->VI.getSummaryList().empty())
        FS = dyn_cast<FunctionSummary>(
            FailureInfo->VI.getSummaryList()[0]->getBaseObject());
      dbgs() << FailureInfo->VI
             << ": Reason = " << getFailureName(FailureInfo->Reason)
             << ", Threshold = " << ProcessedThreshold
             << ", Size = " << (FS ? (int)FS->instCount() : -1)
             << ", MaxHotness = " << getHotnessName(FailureInfo->MaxHotness)
             << ", Attempts = " << FailureInfo->Attempts << "\n";
    }
  }
}

#ifndef NDEBUG
static bool isGlobalVarSummary(const ModuleSummaryIndex &Index,
                               GlobalValue::GUID G) {
  if (const auto &VI = Index.getValueInfo(G)) {
    auto SL = VI.getSummaryList();
    if (!SL.empty())
      return SL[0]->getSummaryKind() == GlobalValueSummary::GlobalVarKind;
  }
  return false;
}

static GlobalValue::GUID getGUID(GlobalValue::GUID G) { return G; }

template <class T>
static unsigned numGlobalVarSummaries(const ModuleSummaryIndex &Index,
                                      T &Cont) {
  unsigned NumGVS = 0;
  for (auto &V : Cont)
    if (isGlobalVarSummary(Index, getGUID(V)))
      ++NumGVS;
  return NumGVS;
}
#endif

/// Compute all the import and export for every module using the Index.
void llvm::ComputeCrossModuleImport(
    const ModuleSummaryIndex &Index,
    const StringMap<GVSummaryMapTy> &ModuleToDefinedGVSummaries,
    StringMap<FunctionImporter::ImportMapTy> &ImportLists,
    StringMap<FunctionImporter::ExportSetTy> &ExportLists) {
  // For each module that has function defined, compute the import/export lists.
  for (auto &DefinedGVSummaries : ModuleToDefinedGVSummaries) {
    auto &ImportList = ImportLists[DefinedGVSummaries.first()];
    LLVM_DEBUG(dbgs() << "Computing import for Module '"
                      << DefinedGVSummaries.first() << "'\n");
    ComputeImportForModule(DefinedGVSummaries.second, Index,
                           DefinedGVSummaries.first(), ImportList,
                           &ExportLists);
  }

  // When computing imports we added all GUIDs referenced by anything
  // imported from the module to its ExportList. Now we prune each ExportList
  // of any not defined in that module. This is more efficient than checking
  // while computing imports because some of the summary lists may be long
  // due to linkonce (comdat) copies.
  for (auto &ELI : ExportLists) {
    const auto &DefinedGVSummaries =
        ModuleToDefinedGVSummaries.lookup(ELI.first());
    for (auto EI = ELI.second.begin(); EI != ELI.second.end();) {
      if (!DefinedGVSummaries.count(*EI))
        EI = ELI.second.erase(EI);
      else
        ++EI;
    }
  }

#ifndef NDEBUG
  LLVM_DEBUG(dbgs() << "Import/Export lists for " << ImportLists.size()
                    << " modules:\n");
  for (auto &ModuleImports : ImportLists) {
    auto ModName = ModuleImports.first();
    auto &Exports = ExportLists[ModName];
    unsigned NumGVS = numGlobalVarSummaries(Index, Exports);
    LLVM_DEBUG(dbgs() << "* Module " << ModName << " exports "
                      << Exports.size() - NumGVS << " functions and " << NumGVS
                      << " vars. Imports from " << ModuleImports.second.size()
                      << " modules.\n");
    for (auto &Src : ModuleImports.second) {
      auto SrcModName = Src.first();
      unsigned NumGVSPerMod = numGlobalVarSummaries(Index, Src.second);
      LLVM_DEBUG(dbgs() << " - " << Src.second.size() - NumGVSPerMod
                        << " functions imported from " << SrcModName << "\n");
      LLVM_DEBUG(dbgs() << " - " << NumGVSPerMod
                        << " global vars imported from " << SrcModName << "\n");
    }
  }
#endif
}

#ifndef NDEBUG
static void dumpImportListForModule(const ModuleSummaryIndex &Index,
                                    StringRef ModulePath,
                                    FunctionImporter::ImportMapTy &ImportList) {
  LLVM_DEBUG(dbgs() << "* Module " << ModulePath << " imports from "
                    << ImportList.size() << " modules.\n");
  for (auto &Src : ImportList) {
    auto SrcModName = Src.first();
    unsigned NumGVSPerMod = numGlobalVarSummaries(Index, Src.second);
    LLVM_DEBUG(dbgs() << " - " << Src.second.size() - NumGVSPerMod
                      << " functions imported from " << SrcModName << "\n");
    LLVM_DEBUG(dbgs() << " - " << NumGVSPerMod << " vars imported from "
                      << SrcModName << "\n");
  }
}
#endif

/// Compute all the imports for the given module in the Index.
void llvm::ComputeCrossModuleImportForModule(
    StringRef ModulePath, const ModuleSummaryIndex &Index,
    FunctionImporter::ImportMapTy &ImportList) {
  // Collect the list of functions this module defines.
  // GUID -> Summary
  GVSummaryMapTy FunctionSummaryMap;
  Index.collectDefinedFunctionsForModule(ModulePath, FunctionSummaryMap);

  // Compute the import list for this module.
  LLVM_DEBUG(dbgs() << "Computing import for Module '" << ModulePath << "'\n");
  ComputeImportForModule(FunctionSummaryMap, Index, ModulePath, ImportList);

#ifndef NDEBUG
  dumpImportListForModule(Index, ModulePath, ImportList);
#endif
}

// Mark all external summaries in Index for import into the given module.
// Used for distributed builds using a distributed index.
void llvm::ComputeCrossModuleImportForModuleFromIndex(
    StringRef ModulePath, const ModuleSummaryIndex &Index,
    FunctionImporter::ImportMapTy &ImportList) {
  for (auto &GlobalList : Index) {
    // Ignore entries for undefined references.
    if (GlobalList.second.SummaryList.empty())
      continue;

    auto GUID = GlobalList.first;
    assert(GlobalList.second.SummaryList.size() == 1 &&
           "Expected individual combined index to have one summary per GUID");
    auto &Summary = GlobalList.second.SummaryList[0];
    // Skip the summaries for the importing module. These are included to
    // e.g. record required linkage changes.
    if (Summary->modulePath() == ModulePath)
      continue;
    // Add an entry to provoke importing by thinBackend.
    ImportList[Summary->modulePath()].insert(GUID);
  }
#ifndef NDEBUG
  dumpImportListForModule(Index, ModulePath, ImportList);
#endif
}

void llvm::computeDeadSymbols(
    ModuleSummaryIndex &Index,
    const DenseSet<GlobalValue::GUID> &GUIDPreservedSymbols,
    function_ref<PrevailingType(GlobalValue::GUID)> isPrevailing) {
  assert(!Index.withGlobalValueDeadStripping());
  if (!ComputeDead)
    return;
  if (GUIDPreservedSymbols.empty())
    // Don't do anything when nothing is live, this is friendly with tests.
    return;
  unsigned LiveSymbols = 0;
  SmallVector<ValueInfo, 128> Worklist;
  Worklist.reserve(GUIDPreservedSymbols.size() * 2);
  for (auto GUID : GUIDPreservedSymbols) {
    ValueInfo VI = Index.getValueInfo(GUID);
    if (!VI)
      continue;
    for (auto &S : VI.getSummaryList())
      S->setLive(true);
  }

  // Add values flagged in the index as live roots to the worklist.
  for (const auto &Entry : Index) {
    auto VI = Index.getValueInfo(Entry);
    for (auto &S : Entry.second.SummaryList)
      if (S->isLive()) {
        LLVM_DEBUG(dbgs() << "Live root: " << VI << "\n");
        Worklist.push_back(VI);
        ++LiveSymbols;
        break;
      }
  }

  // Make value live and add it to the worklist if it was not live before.
  auto visit = [&](ValueInfo VI) {
    // FIXME: If we knew which edges were created for indirect call profiles,
    // we could skip them here. Any that are live should be reached via
    // other edges, e.g. reference edges. Otherwise, using a profile collected
    // on a slightly different binary might provoke preserving, importing
    // and ultimately promoting calls to functions not linked into this
    // binary, which increases the binary size unnecessarily. Note that
    // if this code changes, the importer needs to change so that edges
    // to functions marked dead are skipped.
    VI = updateValueInfoForIndirectCalls(Index, VI);
    if (!VI)
      return;

    // We need to make sure all variants of the symbol are scanned, alias can
    // make one (but not all) alive.
    if (llvm::all_of(VI.getSummaryList(),
                     [](const std::unique_ptr<llvm::GlobalValueSummary> &S) {
                       return S->isLive();
                     }))
      return;

    // We only keep live symbols that are known to be non-prevailing if any are
    // available_externally, linkonceodr, weakodr. Those symbols are discarded
    // later in the EliminateAvailableExternally pass and setting them to
    // not-live could break downstreams users of liveness information (PR36483)
    // or limit optimization opportunities.
    if (isPrevailing(VI.getGUID()) == PrevailingType::No) {
      bool KeepAliveLinkage = false;
      bool Interposable = false;
      for (auto &S : VI.getSummaryList()) {
        if (S->linkage() == GlobalValue::AvailableExternallyLinkage ||
            S->linkage() == GlobalValue::WeakODRLinkage ||
            S->linkage() == GlobalValue::LinkOnceODRLinkage)
          KeepAliveLinkage = true;
        else if (GlobalValue::isInterposableLinkage(S->linkage()))
          Interposable = true;
      }

      if (!KeepAliveLinkage)
        return;

      if (Interposable)
        report_fatal_error(
          "Interposable and available_externally/linkonce_odr/weak_odr symbol");
    }

    for (auto &S : VI.getSummaryList())
      S->setLive(true);
    ++LiveSymbols;
    Worklist.push_back(VI);
  };

  while (!Worklist.empty()) {
    auto VI = Worklist.pop_back_val();
    for (auto &Summary : VI.getSummaryList()) {
      GlobalValueSummary *Base = Summary->getBaseObject();
      // Set base value live in case it is an alias.
      Base->setLive(true);
      for (auto Ref : Base->refs())
        visit(Ref);
      if (auto *FS = dyn_cast<FunctionSummary>(Base))
        for (auto Call : FS->calls())
          visit(Call.first);
    }
  }
  Index.setWithGlobalValueDeadStripping();

  unsigned DeadSymbols = Index.size() - LiveSymbols;
  LLVM_DEBUG(dbgs() << LiveSymbols << " symbols Live, and " << DeadSymbols
                    << " symbols Dead \n");
  NumDeadSymbols += DeadSymbols;
  NumLiveSymbols += LiveSymbols;
}

// Compute dead symbols and propagate constants in combined index.
void llvm::computeDeadSymbolsWithConstProp(
    ModuleSummaryIndex &Index,
    const DenseSet<GlobalValue::GUID> &GUIDPreservedSymbols,
    function_ref<PrevailingType(GlobalValue::GUID)> isPrevailing,
    bool ImportEnabled) {
  computeDeadSymbols(Index, GUIDPreservedSymbols, isPrevailing);
  if (ImportEnabled) {
    Index.propagateConstants(GUIDPreservedSymbols);
  } else {
    // If import is disabled we should drop read-only attribute
    // from all summaries to prevent internalization.
    for (auto &P : Index)
      for (auto &S : P.second.SummaryList)
        if (auto *GVS = dyn_cast<GlobalVarSummary>(S.get()))
          GVS->setReadOnly(false);
  }
}

/// Compute the set of summaries needed for a ThinLTO backend compilation of
/// \p ModulePath.
void llvm::gatherImportedSummariesForModule(
    StringRef ModulePath,
    const StringMap<GVSummaryMapTy> &ModuleToDefinedGVSummaries,
    const FunctionImporter::ImportMapTy &ImportList,
    std::map<std::string, GVSummaryMapTy> &ModuleToSummariesForIndex) {
  // Include all summaries from the importing module.
  ModuleToSummariesForIndex[ModulePath] =
      ModuleToDefinedGVSummaries.lookup(ModulePath);
  // Include summaries for imports.
  for (auto &ILI : ImportList) {
    auto &SummariesForIndex = ModuleToSummariesForIndex[ILI.first()];
    const auto &DefinedGVSummaries =
        ModuleToDefinedGVSummaries.lookup(ILI.first());
    for (auto &GI : ILI.second) {
      const auto &DS = DefinedGVSummaries.find(GI);
      assert(DS != DefinedGVSummaries.end() &&
             "Expected a defined summary for imported global value");
      SummariesForIndex[GI] = DS->second;
    }
  }
}

/// Emit the files \p ModulePath will import from into \p OutputFilename.
std::error_code llvm::EmitImportsFiles(
    StringRef ModulePath, StringRef OutputFilename,
    const std::map<std::string, GVSummaryMapTy> &ModuleToSummariesForIndex) {
  std::error_code EC;
  raw_fd_ostream ImportsOS(OutputFilename, EC, sys::fs::OpenFlags::F_None);
  if (EC)
    return EC;
  for (auto &ILI : ModuleToSummariesForIndex)
    // The ModuleToSummariesForIndex map includes an entry for the current
    // Module (needed for writing out the index files). We don't want to
    // include it in the imports file, however, so filter it out.
    if (ILI.first != ModulePath)
      ImportsOS << ILI.first << "\n";
  return std::error_code();
}

bool llvm::convertToDeclaration(GlobalValue &GV) {
  LLVM_DEBUG(dbgs() << "Converting to a declaration: `" << GV.getName()
                    << "\n");
  if (Function *F = dyn_cast<Function>(&GV)) {
    F->deleteBody();
    F->clearMetadata();
    F->setComdat(nullptr);
  } else if (GlobalVariable *V = dyn_cast<GlobalVariable>(&GV)) {
    V->setInitializer(nullptr);
    V->setLinkage(GlobalValue::ExternalLinkage);
    V->clearMetadata();
    V->setComdat(nullptr);
  } else {
    GlobalValue *NewGV;
    if (GV.getValueType()->isFunctionTy())
      NewGV =
          Function::Create(cast<FunctionType>(GV.getValueType()),
                           GlobalValue::ExternalLinkage, GV.getAddressSpace(),
                           "", GV.getParent());
    else
      NewGV =
          new GlobalVariable(*GV.getParent(), GV.getValueType(),
                             /*isConstant*/ false, GlobalValue::ExternalLinkage,
                             /*init*/ nullptr, "",
                             /*insertbefore*/ nullptr, GV.getThreadLocalMode(),
                             GV.getType()->getAddressSpace());
    NewGV->takeName(&GV);
    GV.replaceAllUsesWith(NewGV);
    return false;
  }
  return true;
}

/// Fixup prevailing symbol linkages in \p TheModule based on summary analysis.
void llvm::thinLTOResolvePrevailingInModule(
    Module &TheModule, const GVSummaryMapTy &DefinedGlobals) {
  auto updateLinkage = [&](GlobalValue &GV) {
    // See if the global summary analysis computed a new resolved linkage.
    const auto &GS = DefinedGlobals.find(GV.getGUID());
    if (GS == DefinedGlobals.end())
      return;
    auto NewLinkage = GS->second->linkage();
    if (NewLinkage == GV.getLinkage())
      return;

    // Switch the linkage to weakany if asked for, e.g. we do this for
    // linker redefined symbols (via --wrap or --defsym).
    // We record that the visibility should be changed here in `addThinLTO`
    // as we need access to the resolution vectors for each input file in
    // order to find which symbols have been redefined.
    // We may consider reorganizing this code and moving the linkage recording
    // somewhere else, e.g. in thinLTOResolvePrevailingInIndex.
    if (NewLinkage == GlobalValue::WeakAnyLinkage) {
      GV.setLinkage(NewLinkage);
      return;
    }

    if (GlobalValue::isLocalLinkage(GV.getLinkage()) ||
        // In case it was dead and already converted to declaration.
        GV.isDeclaration())
      return;
    // Check for a non-prevailing def that has interposable linkage
    // (e.g. non-odr weak or linkonce). In that case we can't simply
    // convert to available_externally, since it would lose the
    // interposable property and possibly get inlined. Simply drop
    // the definition in that case.
    if (GlobalValue::isAvailableExternallyLinkage(NewLinkage) &&
        GlobalValue::isInterposableLinkage(GV.getLinkage())) {
      if (!convertToDeclaration(GV))
        // FIXME: Change this to collect replaced GVs and later erase
        // them from the parent module once thinLTOResolvePrevailingGUID is
        // changed to enable this for aliases.
        llvm_unreachable("Expected GV to be converted");
    } else {
      // If the original symbols has global unnamed addr and linkonce_odr linkage,
      // it should be an auto hide symbol. Add hidden visibility to the symbol to
      // preserve the property.
      if (GV.hasLinkOnceODRLinkage() && GV.hasGlobalUnnamedAddr() &&
          NewLinkage == GlobalValue::WeakODRLinkage)
        GV.setVisibility(GlobalValue::HiddenVisibility);

      LLVM_DEBUG(dbgs() << "ODR fixing up linkage for `" << GV.getName()
                        << "` from " << GV.getLinkage() << " to " << NewLinkage
                        << "\n");
      GV.setLinkage(NewLinkage);
    }
    // Remove declarations from comdats, including available_externally
    // as this is a declaration for the linker, and will be dropped eventually.
    // It is illegal for comdats to contain declarations.
    auto *GO = dyn_cast_or_null<GlobalObject>(&GV);
    if (GO && GO->isDeclarationForLinker() && GO->hasComdat())
      GO->setComdat(nullptr);
  };

  // Process functions and global now
  for (auto &GV : TheModule)
    updateLinkage(GV);
  for (auto &GV : TheModule.globals())
    updateLinkage(GV);
  for (auto &GV : TheModule.aliases())
    updateLinkage(GV);
}

/// Run internalization on \p TheModule based on symmary analysis.
void llvm::thinLTOInternalizeModule(Module &TheModule,
                                    const GVSummaryMapTy &DefinedGlobals) {
  // Declare a callback for the internalize pass that will ask for every
  // candidate GlobalValue if it can be internalized or not.
  auto MustPreserveGV = [&](const GlobalValue &GV) -> bool {
    // Lookup the linkage recorded in the summaries during global analysis.
    auto GS = DefinedGlobals.find(GV.getGUID());
    if (GS == DefinedGlobals.end()) {
      // Must have been promoted (possibly conservatively). Find original
      // name so that we can access the correct summary and see if it can
      // be internalized again.
      // FIXME: Eventually we should control promotion instead of promoting
      // and internalizing again.
      StringRef OrigName =
          ModuleSummaryIndex::getOriginalNameBeforePromote(GV.getName());
      std::string OrigId = GlobalValue::getGlobalIdentifier(
          OrigName, GlobalValue::InternalLinkage,
          TheModule.getSourceFileName());
      GS = DefinedGlobals.find(GlobalValue::getGUID(OrigId));
      if (GS == DefinedGlobals.end()) {
        // Also check the original non-promoted non-globalized name. In some
        // cases a preempted weak value is linked in as a local copy because
        // it is referenced by an alias (IRLinker::linkGlobalValueProto).
        // In that case, since it was originally not a local value, it was
        // recorded in the index using the original name.
        // FIXME: This may not be needed once PR27866 is fixed.
        GS = DefinedGlobals.find(GlobalValue::getGUID(OrigName));
        assert(GS != DefinedGlobals.end());
      }
    }
    return !GlobalValue::isLocalLinkage(GS->second->linkage());
  };

  // FIXME: See if we can just internalize directly here via linkage changes
  // based on the index, rather than invoking internalizeModule.
  internalizeModule(TheModule, MustPreserveGV);
}

/// Make alias a clone of its aliasee.
static Function *replaceAliasWithAliasee(Module *SrcModule, GlobalAlias *GA) {
  Function *Fn = cast<Function>(GA->getBaseObject());

  ValueToValueMapTy VMap;
  Function *NewFn = CloneFunction(Fn, VMap);
  // Clone should use the original alias's linkage and name, and we ensure
  // all uses of alias instead use the new clone (casted if necessary).
  NewFn->setLinkage(GA->getLinkage());
  GA->replaceAllUsesWith(ConstantExpr::getBitCast(NewFn, GA->getType()));
  NewFn->takeName(GA);
  return NewFn;
}

// Internalize values that we marked with specific attribute
// in processGlobalForThinLTO.
static void internalizeImmutableGVs(Module &M) {
  for (auto &GV : M.globals())
    // Skip GVs which have been converted to declarations
    // by dropDeadSymbols.
    if (!GV.isDeclaration() && GV.hasAttribute("thinlto-internalize")) {
      GV.setLinkage(GlobalValue::InternalLinkage);
      GV.setVisibility(GlobalValue::DefaultVisibility);
    }
}

// Automatically import functions in Module \p DestModule based on the summaries
// index.
Expected<bool> FunctionImporter::importFunctions(
    Module &DestModule, const FunctionImporter::ImportMapTy &ImportList) {
  LLVM_DEBUG(dbgs() << "Starting import for Module "
                    << DestModule.getModuleIdentifier() << "\n");
  unsigned ImportedCount = 0, ImportedGVCount = 0;

  IRMover Mover(DestModule);
  // Do the actual import of functions now, one Module at a time
  std::set<StringRef> ModuleNameOrderedList;
  for (auto &FunctionsToImportPerModule : ImportList) {
    ModuleNameOrderedList.insert(FunctionsToImportPerModule.first());
  }
  for (auto &Name : ModuleNameOrderedList) {
    // Get the module for the import
    const auto &FunctionsToImportPerModule = ImportList.find(Name);
    assert(FunctionsToImportPerModule != ImportList.end());
    Expected<std::unique_ptr<Module>> SrcModuleOrErr = ModuleLoader(Name);
    if (!SrcModuleOrErr)
      return SrcModuleOrErr.takeError();
    std::unique_ptr<Module> SrcModule = std::move(*SrcModuleOrErr);
    assert(&DestModule.getContext() == &SrcModule->getContext() &&
           "Context mismatch");

    // If modules were created with lazy metadata loading, materialize it
    // now, before linking it (otherwise this will be a noop).
    if (Error Err = SrcModule->materializeMetadata())
      return std::move(Err);

    auto &ImportGUIDs = FunctionsToImportPerModule->second;
    // Find the globals to import
    SetVector<GlobalValue *> GlobalsToImport;
    for (Function &F : *SrcModule) {
      if (!F.hasName())
        continue;
      auto GUID = F.getGUID();
      auto Import = ImportGUIDs.count(GUID);
      LLVM_DEBUG(dbgs() << (Import ? "Is" : "Not") << " importing function "
                        << GUID << " " << F.getName() << " from "
                        << SrcModule->getSourceFileName() << "\n");
      if (Import) {
        if (Error Err = F.materialize())
          return std::move(Err);
        if (EnableImportMetadata) {
          // Add 'thinlto_src_module' metadata for statistics and debugging.
          F.setMetadata(
              "thinlto_src_module",
              MDNode::get(DestModule.getContext(),
                          {MDString::get(DestModule.getContext(),
                                         SrcModule->getSourceFileName())}));
        }
        GlobalsToImport.insert(&F);
      }
    }
    for (GlobalVariable &GV : SrcModule->globals()) {
      if (!GV.hasName())
        continue;
      auto GUID = GV.getGUID();
      auto Import = ImportGUIDs.count(GUID);
      LLVM_DEBUG(dbgs() << (Import ? "Is" : "Not") << " importing global "
                        << GUID << " " << GV.getName() << " from "
                        << SrcModule->getSourceFileName() << "\n");
      if (Import) {
        if (Error Err = GV.materialize())
          return std::move(Err);
        ImportedGVCount += GlobalsToImport.insert(&GV);
      }
    }
    for (GlobalAlias &GA : SrcModule->aliases()) {
      if (!GA.hasName())
        continue;
      auto GUID = GA.getGUID();
      auto Import = ImportGUIDs.count(GUID);
      LLVM_DEBUG(dbgs() << (Import ? "Is" : "Not") << " importing alias "
                        << GUID << " " << GA.getName() << " from "
                        << SrcModule->getSourceFileName() << "\n");
      if (Import) {
        if (Error Err = GA.materialize())
          return std::move(Err);
        // Import alias as a copy of its aliasee.
        GlobalObject *Base = GA.getBaseObject();
        if (Error Err = Base->materialize())
          return std::move(Err);
        auto *Fn = replaceAliasWithAliasee(SrcModule.get(), &GA);
        LLVM_DEBUG(dbgs() << "Is importing aliasee fn " << Base->getGUID()
                          << " " << Base->getName() << " from "
                          << SrcModule->getSourceFileName() << "\n");
        if (EnableImportMetadata) {
          // Add 'thinlto_src_module' metadata for statistics and debugging.
          Fn->setMetadata(
              "thinlto_src_module",
              MDNode::get(DestModule.getContext(),
                          {MDString::get(DestModule.getContext(),
                                         SrcModule->getSourceFileName())}));
        }
        GlobalsToImport.insert(Fn);
      }
    }

    // Upgrade debug info after we're done materializing all the globals and we
    // have loaded all the required metadata!
    UpgradeDebugInfo(*SrcModule);

    // Link in the specified functions.
    if (renameModuleForThinLTO(*SrcModule, Index, &GlobalsToImport))
      return true;

    if (PrintImports) {
      for (const auto *GV : GlobalsToImport)
        dbgs() << DestModule.getSourceFileName() << ": Import " << GV->getName()
               << " from " << SrcModule->getSourceFileName() << "\n";
    }

    if (Mover.move(std::move(SrcModule), GlobalsToImport.getArrayRef(),
                   [](GlobalValue &, IRMover::ValueAdder) {},
                   /*IsPerformingImport=*/true))
      report_fatal_error("Function Import: link error");

    ImportedCount += GlobalsToImport.size();
    NumImportedModules++;
  }

  internalizeImmutableGVs(DestModule);

  NumImportedFunctions += (ImportedCount - ImportedGVCount);
  NumImportedGlobalVars += ImportedGVCount;

  LLVM_DEBUG(dbgs() << "Imported " << ImportedCount - ImportedGVCount
                    << " functions for Module "
                    << DestModule.getModuleIdentifier() << "\n");
  LLVM_DEBUG(dbgs() << "Imported " << ImportedGVCount
                    << " global variables for Module "
                    << DestModule.getModuleIdentifier() << "\n");
  return ImportedCount;
}

static bool doImportingForModule(Module &M) {
  if (SummaryFile.empty())
    report_fatal_error("error: -function-import requires -summary-file\n");
  Expected<std::unique_ptr<ModuleSummaryIndex>> IndexPtrOrErr =
      getModuleSummaryIndexForFile(SummaryFile);
  if (!IndexPtrOrErr) {
    logAllUnhandledErrors(IndexPtrOrErr.takeError(), errs(),
                          "Error loading file '" + SummaryFile + "': ");
    return false;
  }
  std::unique_ptr<ModuleSummaryIndex> Index = std::move(*IndexPtrOrErr);

  // First step is collecting the import list.
  FunctionImporter::ImportMapTy ImportList;
  // If requested, simply import all functions in the index. This is used
  // when testing distributed backend handling via the opt tool, when
  // we have distributed indexes containing exactly the summaries to import.
  if (ImportAllIndex)
    ComputeCrossModuleImportForModuleFromIndex(M.getModuleIdentifier(), *Index,
                                               ImportList);
  else
    ComputeCrossModuleImportForModule(M.getModuleIdentifier(), *Index,
                                      ImportList);

  // Conservatively mark all internal values as promoted. This interface is
  // only used when doing importing via the function importing pass. The pass
  // is only enabled when testing importing via the 'opt' tool, which does
  // not do the ThinLink that would normally determine what values to promote.
  for (auto &I : *Index) {
    for (auto &S : I.second.SummaryList) {
      if (GlobalValue::isLocalLinkage(S->linkage()))
        S->setLinkage(GlobalValue::ExternalLinkage);
    }
  }

  // Next we need to promote to global scope and rename any local values that
  // are potentially exported to other modules.
  if (renameModuleForThinLTO(M, *Index, nullptr)) {
    errs() << "Error renaming module\n";
    return false;
  }

  // Perform the import now.
  auto ModuleLoader = [&M](StringRef Identifier) {
    return loadFile(Identifier, M.getContext());
  };
  FunctionImporter Importer(*Index, ModuleLoader);
  Expected<bool> Result = Importer.importFunctions(M, ImportList);

  // FIXME: Probably need to propagate Errors through the pass manager.
  if (!Result) {
    logAllUnhandledErrors(Result.takeError(), errs(),
                          "Error importing module: ");
    return false;
  }

  return *Result;
}

namespace {

/// Pass that performs cross-module function import provided a summary file.
class FunctionImportLegacyPass : public ModulePass {
public:
  /// Pass identification, replacement for typeid
  static char ID;

  explicit FunctionImportLegacyPass() : ModulePass(ID) {}

  /// Specify pass name for debug output
  StringRef getPassName() const override { return "Function Importing"; }

  bool runOnModule(Module &M) override {
    if (skipModule(M))
      return false;

    return doImportingForModule(M);
  }
};

} // end anonymous namespace

PreservedAnalyses FunctionImportPass::run(Module &M,
                                          ModuleAnalysisManager &AM) {
  if (!doImportingForModule(M))
    return PreservedAnalyses::all();

  return PreservedAnalyses::none();
}

char FunctionImportLegacyPass::ID = 0;
INITIALIZE_PASS(FunctionImportLegacyPass, "function-import",
                "Summary Based Function Import", false, false)

namespace llvm {

Pass *createFunctionImportPass() {
  return new FunctionImportLegacyPass();
}

} // end namespace llvm
