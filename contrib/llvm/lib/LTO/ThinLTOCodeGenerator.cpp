//===-ThinLTOCodeGenerator.cpp - LLVM Link Time Optimizer -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Thin Link Time Optimization library. This library is
// intended to be used by linker to optimize code at link time.
//
//===----------------------------------------------------------------------===//

#include "llvm/LTO/legacy/ThinLTOCodeGenerator.h"

#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/ModuleSummaryAnalysis.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Bitcode/BitcodeWriterPass.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/PassTimingInfo.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/LTO/LTO.h"
#include "llvm/LTO/SummaryBasedOptimizations.h"
#include "llvm/MC/SubtargetFeature.h"
#include "llvm/Object/IRObjectFile.h"
#include "llvm/Support/CachePruning.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SHA1.h"
#include "llvm/Support/SmallVectorMemoryBuffer.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/ThreadPool.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/VCSRevision.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/IPO/FunctionImport.h"
#include "llvm/Transforms/IPO/Internalize.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/ObjCARC.h"
#include "llvm/Transforms/Utils/FunctionImportUtils.h"

#include <numeric>

#if !defined(_MSC_VER) && !defined(__MINGW32__)
#include <unistd.h>
#else
#include <io.h>
#endif

using namespace llvm;

#define DEBUG_TYPE "thinlto"

namespace llvm {
// Flags -discard-value-names, defined in LTOCodeGenerator.cpp
extern cl::opt<bool> LTODiscardValueNames;
extern cl::opt<std::string> LTORemarksFilename;
extern cl::opt<bool> LTOPassRemarksWithHotness;
}

namespace {

static cl::opt<int>
    ThreadCount("threads", cl::init(llvm::heavyweight_hardware_concurrency()));

// Simple helper to save temporary files for debug.
static void saveTempBitcode(const Module &TheModule, StringRef TempDir,
                            unsigned count, StringRef Suffix) {
  if (TempDir.empty())
    return;
  // User asked to save temps, let dump the bitcode file after import.
  std::string SaveTempPath = (TempDir + llvm::Twine(count) + Suffix).str();
  std::error_code EC;
  raw_fd_ostream OS(SaveTempPath, EC, sys::fs::F_None);
  if (EC)
    report_fatal_error(Twine("Failed to open ") + SaveTempPath +
                       " to save optimized bitcode\n");
  WriteBitcodeToFile(TheModule, OS, /* ShouldPreserveUseListOrder */ true);
}

static const GlobalValueSummary *
getFirstDefinitionForLinker(const GlobalValueSummaryList &GVSummaryList) {
  // If there is any strong definition anywhere, get it.
  auto StrongDefForLinker = llvm::find_if(
      GVSummaryList, [](const std::unique_ptr<GlobalValueSummary> &Summary) {
        auto Linkage = Summary->linkage();
        return !GlobalValue::isAvailableExternallyLinkage(Linkage) &&
               !GlobalValue::isWeakForLinker(Linkage);
      });
  if (StrongDefForLinker != GVSummaryList.end())
    return StrongDefForLinker->get();
  // Get the first *linker visible* definition for this global in the summary
  // list.
  auto FirstDefForLinker = llvm::find_if(
      GVSummaryList, [](const std::unique_ptr<GlobalValueSummary> &Summary) {
        auto Linkage = Summary->linkage();
        return !GlobalValue::isAvailableExternallyLinkage(Linkage);
      });
  // Extern templates can be emitted as available_externally.
  if (FirstDefForLinker == GVSummaryList.end())
    return nullptr;
  return FirstDefForLinker->get();
}

// Populate map of GUID to the prevailing copy for any multiply defined
// symbols. Currently assume first copy is prevailing, or any strong
// definition. Can be refined with Linker information in the future.
static void computePrevailingCopies(
    const ModuleSummaryIndex &Index,
    DenseMap<GlobalValue::GUID, const GlobalValueSummary *> &PrevailingCopy) {
  auto HasMultipleCopies = [&](const GlobalValueSummaryList &GVSummaryList) {
    return GVSummaryList.size() > 1;
  };

  for (auto &I : Index) {
    if (HasMultipleCopies(I.second.SummaryList))
      PrevailingCopy[I.first] =
          getFirstDefinitionForLinker(I.second.SummaryList);
  }
}

static StringMap<MemoryBufferRef>
generateModuleMap(const std::vector<ThinLTOBuffer> &Modules) {
  StringMap<MemoryBufferRef> ModuleMap;
  for (auto &ModuleBuffer : Modules) {
    assert(ModuleMap.find(ModuleBuffer.getBufferIdentifier()) ==
               ModuleMap.end() &&
           "Expect unique Buffer Identifier");
    ModuleMap[ModuleBuffer.getBufferIdentifier()] = ModuleBuffer.getMemBuffer();
  }
  return ModuleMap;
}

static void promoteModule(Module &TheModule, const ModuleSummaryIndex &Index) {
  if (renameModuleForThinLTO(TheModule, Index))
    report_fatal_error("renameModuleForThinLTO failed");
}

namespace {
class ThinLTODiagnosticInfo : public DiagnosticInfo {
  const Twine &Msg;
public:
  ThinLTODiagnosticInfo(const Twine &DiagMsg,
                        DiagnosticSeverity Severity = DS_Error)
      : DiagnosticInfo(DK_Linker, Severity), Msg(DiagMsg) {}
  void print(DiagnosticPrinter &DP) const override { DP << Msg; }
};
}

/// Verify the module and strip broken debug info.
static void verifyLoadedModule(Module &TheModule) {
  bool BrokenDebugInfo = false;
  if (verifyModule(TheModule, &dbgs(), &BrokenDebugInfo))
    report_fatal_error("Broken module found, compilation aborted!");
  if (BrokenDebugInfo) {
    TheModule.getContext().diagnose(ThinLTODiagnosticInfo(
        "Invalid debug info found, debug info will be stripped", DS_Warning));
    StripDebugInfo(TheModule);
  }
}

static std::unique_ptr<Module>
loadModuleFromBuffer(const MemoryBufferRef &Buffer, LLVMContext &Context,
                     bool Lazy, bool IsImporting) {
  SMDiagnostic Err;
  Expected<std::unique_ptr<Module>> ModuleOrErr =
      Lazy
          ? getLazyBitcodeModule(Buffer, Context,
                                 /* ShouldLazyLoadMetadata */ true, IsImporting)
          : parseBitcodeFile(Buffer, Context);
  if (!ModuleOrErr) {
    handleAllErrors(ModuleOrErr.takeError(), [&](ErrorInfoBase &EIB) {
      SMDiagnostic Err = SMDiagnostic(Buffer.getBufferIdentifier(),
                                      SourceMgr::DK_Error, EIB.message());
      Err.print("ThinLTO", errs());
    });
    report_fatal_error("Can't load module, abort.");
  }
  if (!Lazy)
    verifyLoadedModule(*ModuleOrErr.get());
  return std::move(ModuleOrErr.get());
}

static void
crossImportIntoModule(Module &TheModule, const ModuleSummaryIndex &Index,
                      StringMap<MemoryBufferRef> &ModuleMap,
                      const FunctionImporter::ImportMapTy &ImportList) {
  auto Loader = [&](StringRef Identifier) {
    return loadModuleFromBuffer(ModuleMap[Identifier], TheModule.getContext(),
                                /*Lazy=*/true, /*IsImporting*/ true);
  };

  FunctionImporter Importer(Index, Loader);
  Expected<bool> Result = Importer.importFunctions(TheModule, ImportList);
  if (!Result) {
    handleAllErrors(Result.takeError(), [&](ErrorInfoBase &EIB) {
      SMDiagnostic Err = SMDiagnostic(TheModule.getModuleIdentifier(),
                                      SourceMgr::DK_Error, EIB.message());
      Err.print("ThinLTO", errs());
    });
    report_fatal_error("importFunctions failed");
  }
  // Verify again after cross-importing.
  verifyLoadedModule(TheModule);
}

static void optimizeModule(Module &TheModule, TargetMachine &TM,
                           unsigned OptLevel, bool Freestanding) {
  // Populate the PassManager
  PassManagerBuilder PMB;
  PMB.LibraryInfo = new TargetLibraryInfoImpl(TM.getTargetTriple());
  if (Freestanding)
    PMB.LibraryInfo->disableAllFunctions();
  PMB.Inliner = createFunctionInliningPass();
  // FIXME: should get it from the bitcode?
  PMB.OptLevel = OptLevel;
  PMB.LoopVectorize = true;
  PMB.SLPVectorize = true;
  // Already did this in verifyLoadedModule().
  PMB.VerifyInput = false;
  PMB.VerifyOutput = false;

  legacy::PassManager PM;

  // Add the TTI (required to inform the vectorizer about register size for
  // instance)
  PM.add(createTargetTransformInfoWrapperPass(TM.getTargetIRAnalysis()));

  // Add optimizations
  PMB.populateThinLTOPassManager(PM);

  PM.run(TheModule);
}

// Convert the PreservedSymbols map from "Name" based to "GUID" based.
static DenseSet<GlobalValue::GUID>
computeGUIDPreservedSymbols(const StringSet<> &PreservedSymbols,
                            const Triple &TheTriple) {
  DenseSet<GlobalValue::GUID> GUIDPreservedSymbols(PreservedSymbols.size());
  for (auto &Entry : PreservedSymbols) {
    StringRef Name = Entry.first();
    if (TheTriple.isOSBinFormatMachO() && Name.size() > 0 && Name[0] == '_')
      Name = Name.drop_front();
    GUIDPreservedSymbols.insert(GlobalValue::getGUID(Name));
  }
  return GUIDPreservedSymbols;
}

std::unique_ptr<MemoryBuffer> codegenModule(Module &TheModule,
                                            TargetMachine &TM) {
  SmallVector<char, 128> OutputBuffer;

  // CodeGen
  {
    raw_svector_ostream OS(OutputBuffer);
    legacy::PassManager PM;

    // If the bitcode files contain ARC code and were compiled with optimization,
    // the ObjCARCContractPass must be run, so do it unconditionally here.
    PM.add(createObjCARCContractPass());

    // Setup the codegen now.
    if (TM.addPassesToEmitFile(PM, OS, nullptr, TargetMachine::CGFT_ObjectFile,
                               /* DisableVerify */ true))
      report_fatal_error("Failed to setup codegen");

    // Run codegen now. resulting binary is in OutputBuffer.
    PM.run(TheModule);
  }
  return make_unique<SmallVectorMemoryBuffer>(std::move(OutputBuffer));
}

/// Manage caching for a single Module.
class ModuleCacheEntry {
  SmallString<128> EntryPath;

public:
  // Create a cache entry. This compute a unique hash for the Module considering
  // the current list of export/import, and offer an interface to query to
  // access the content in the cache.
  ModuleCacheEntry(
      StringRef CachePath, const ModuleSummaryIndex &Index, StringRef ModuleID,
      const FunctionImporter::ImportMapTy &ImportList,
      const FunctionImporter::ExportSetTy &ExportList,
      const std::map<GlobalValue::GUID, GlobalValue::LinkageTypes> &ResolvedODR,
      const GVSummaryMapTy &DefinedGVSummaries, unsigned OptLevel,
      bool Freestanding, const TargetMachineBuilder &TMBuilder) {
    if (CachePath.empty())
      return;

    if (!Index.modulePaths().count(ModuleID))
      // The module does not have an entry, it can't have a hash at all
      return;

    if (all_of(Index.getModuleHash(ModuleID),
               [](uint32_t V) { return V == 0; }))
      // No hash entry, no caching!
      return;

    llvm::lto::Config Conf;
    Conf.OptLevel = OptLevel;
    Conf.Options = TMBuilder.Options;
    Conf.CPU = TMBuilder.MCpu;
    Conf.MAttrs.push_back(TMBuilder.MAttr);
    Conf.RelocModel = TMBuilder.RelocModel;
    Conf.CGOptLevel = TMBuilder.CGOptLevel;
    Conf.Freestanding = Freestanding;
    SmallString<40> Key;
    computeLTOCacheKey(Key, Conf, Index, ModuleID, ImportList, ExportList,
                       ResolvedODR, DefinedGVSummaries);

    // This choice of file name allows the cache to be pruned (see pruneCache()
    // in include/llvm/Support/CachePruning.h).
    sys::path::append(EntryPath, CachePath, "llvmcache-" + Key);
  }

  // Access the path to this entry in the cache.
  StringRef getEntryPath() { return EntryPath; }

  // Try loading the buffer for this cache entry.
  ErrorOr<std::unique_ptr<MemoryBuffer>> tryLoadingBuffer() {
    if (EntryPath.empty())
      return std::error_code();
    int FD;
    SmallString<64> ResultPath;
    std::error_code EC = sys::fs::openFileForRead(
        Twine(EntryPath), FD, sys::fs::OF_UpdateAtime, &ResultPath);
    if (EC)
      return EC;
    ErrorOr<std::unique_ptr<MemoryBuffer>> MBOrErr =
        MemoryBuffer::getOpenFile(FD, EntryPath,
                                  /*FileSize*/ -1,
                                  /*RequiresNullTerminator*/ false);
    close(FD);
    return MBOrErr;
  }

  // Cache the Produced object file
  void write(const MemoryBuffer &OutputBuffer) {
    if (EntryPath.empty())
      return;

    // Write to a temporary to avoid race condition
    SmallString<128> TempFilename;
    SmallString<128> CachePath(EntryPath);
    int TempFD;
    llvm::sys::path::remove_filename(CachePath);
    sys::path::append(TempFilename, CachePath, "Thin-%%%%%%.tmp.o");
    std::error_code EC =
      sys::fs::createUniqueFile(TempFilename, TempFD, TempFilename);
    if (EC) {
      errs() << "Error: " << EC.message() << "\n";
      report_fatal_error("ThinLTO: Can't get a temporary file");
    }
    {
      raw_fd_ostream OS(TempFD, /* ShouldClose */ true);
      OS << OutputBuffer.getBuffer();
    }
    // Rename temp file to final destination; rename is atomic
    EC = sys::fs::rename(TempFilename, EntryPath);
    if (EC)
      sys::fs::remove(TempFilename);
  }
};

static std::unique_ptr<MemoryBuffer>
ProcessThinLTOModule(Module &TheModule, ModuleSummaryIndex &Index,
                     StringMap<MemoryBufferRef> &ModuleMap, TargetMachine &TM,
                     const FunctionImporter::ImportMapTy &ImportList,
                     const FunctionImporter::ExportSetTy &ExportList,
                     const DenseSet<GlobalValue::GUID> &GUIDPreservedSymbols,
                     const GVSummaryMapTy &DefinedGlobals,
                     const ThinLTOCodeGenerator::CachingOptions &CacheOptions,
                     bool DisableCodeGen, StringRef SaveTempsDir,
                     bool Freestanding, unsigned OptLevel, unsigned count) {

  // "Benchmark"-like optimization: single-source case
  bool SingleModule = (ModuleMap.size() == 1);

  if (!SingleModule) {
    promoteModule(TheModule, Index);

    // Apply summary-based prevailing-symbol resolution decisions.
    thinLTOResolvePrevailingInModule(TheModule, DefinedGlobals);

    // Save temps: after promotion.
    saveTempBitcode(TheModule, SaveTempsDir, count, ".1.promoted.bc");
  }

  // Be friendly and don't nuke totally the module when the client didn't
  // supply anything to preserve.
  if (!ExportList.empty() || !GUIDPreservedSymbols.empty()) {
    // Apply summary-based internalization decisions.
    thinLTOInternalizeModule(TheModule, DefinedGlobals);
  }

  // Save internalized bitcode
  saveTempBitcode(TheModule, SaveTempsDir, count, ".2.internalized.bc");

  if (!SingleModule) {
    crossImportIntoModule(TheModule, Index, ModuleMap, ImportList);

    // Save temps: after cross-module import.
    saveTempBitcode(TheModule, SaveTempsDir, count, ".3.imported.bc");
  }

  optimizeModule(TheModule, TM, OptLevel, Freestanding);

  saveTempBitcode(TheModule, SaveTempsDir, count, ".4.opt.bc");

  if (DisableCodeGen) {
    // Configured to stop before CodeGen, serialize the bitcode and return.
    SmallVector<char, 128> OutputBuffer;
    {
      raw_svector_ostream OS(OutputBuffer);
      ProfileSummaryInfo PSI(TheModule);
      auto Index = buildModuleSummaryIndex(TheModule, nullptr, &PSI);
      WriteBitcodeToFile(TheModule, OS, true, &Index);
    }
    return make_unique<SmallVectorMemoryBuffer>(std::move(OutputBuffer));
  }

  return codegenModule(TheModule, TM);
}

/// Resolve prevailing symbols. Record resolutions in the \p ResolvedODR map
/// for caching, and in the \p Index for application during the ThinLTO
/// backends. This is needed for correctness for exported symbols (ensure
/// at least one copy kept) and a compile-time optimization (to drop duplicate
/// copies when possible).
static void resolvePrevailingInIndex(
    ModuleSummaryIndex &Index,
    StringMap<std::map<GlobalValue::GUID, GlobalValue::LinkageTypes>>
        &ResolvedODR) {

  DenseMap<GlobalValue::GUID, const GlobalValueSummary *> PrevailingCopy;
  computePrevailingCopies(Index, PrevailingCopy);

  auto isPrevailing = [&](GlobalValue::GUID GUID, const GlobalValueSummary *S) {
    const auto &Prevailing = PrevailingCopy.find(GUID);
    // Not in map means that there was only one copy, which must be prevailing.
    if (Prevailing == PrevailingCopy.end())
      return true;
    return Prevailing->second == S;
  };

  auto recordNewLinkage = [&](StringRef ModuleIdentifier,
                              GlobalValue::GUID GUID,
                              GlobalValue::LinkageTypes NewLinkage) {
    ResolvedODR[ModuleIdentifier][GUID] = NewLinkage;
  };

  thinLTOResolvePrevailingInIndex(Index, isPrevailing, recordNewLinkage);
}

// Initialize the TargetMachine builder for a given Triple
static void initTMBuilder(TargetMachineBuilder &TMBuilder,
                          const Triple &TheTriple) {
  // Set a default CPU for Darwin triples (copied from LTOCodeGenerator).
  // FIXME this looks pretty terrible...
  if (TMBuilder.MCpu.empty() && TheTriple.isOSDarwin()) {
    if (TheTriple.getArch() == llvm::Triple::x86_64)
      TMBuilder.MCpu = "core2";
    else if (TheTriple.getArch() == llvm::Triple::x86)
      TMBuilder.MCpu = "yonah";
    else if (TheTriple.getArch() == llvm::Triple::aarch64)
      TMBuilder.MCpu = "cyclone";
  }
  TMBuilder.TheTriple = std::move(TheTriple);
}

} // end anonymous namespace

void ThinLTOCodeGenerator::addModule(StringRef Identifier, StringRef Data) {
  ThinLTOBuffer Buffer(Data, Identifier);
  LLVMContext Context;
  StringRef TripleStr;
  ErrorOr<std::string> TripleOrErr = expectedToErrorOrAndEmitErrors(
      Context, getBitcodeTargetTriple(Buffer.getMemBuffer()));

  if (TripleOrErr)
    TripleStr = *TripleOrErr;

  Triple TheTriple(TripleStr);

  if (Modules.empty())
    initTMBuilder(TMBuilder, Triple(TheTriple));
  else if (TMBuilder.TheTriple != TheTriple) {
    if (!TMBuilder.TheTriple.isCompatibleWith(TheTriple))
      report_fatal_error("ThinLTO modules with incompatible triples not "
                         "supported");
    initTMBuilder(TMBuilder, Triple(TMBuilder.TheTriple.merge(TheTriple)));
  }

  Modules.push_back(Buffer);
}

void ThinLTOCodeGenerator::preserveSymbol(StringRef Name) {
  PreservedSymbols.insert(Name);
}

void ThinLTOCodeGenerator::crossReferenceSymbol(StringRef Name) {
  // FIXME: At the moment, we don't take advantage of this extra information,
  // we're conservatively considering cross-references as preserved.
  //  CrossReferencedSymbols.insert(Name);
  PreservedSymbols.insert(Name);
}

// TargetMachine factory
std::unique_ptr<TargetMachine> TargetMachineBuilder::create() const {
  std::string ErrMsg;
  const Target *TheTarget =
      TargetRegistry::lookupTarget(TheTriple.str(), ErrMsg);
  if (!TheTarget) {
    report_fatal_error("Can't load target for this Triple: " + ErrMsg);
  }

  // Use MAttr as the default set of features.
  SubtargetFeatures Features(MAttr);
  Features.getDefaultSubtargetFeatures(TheTriple);
  std::string FeatureStr = Features.getString();

  return std::unique_ptr<TargetMachine>(
      TheTarget->createTargetMachine(TheTriple.str(), MCpu, FeatureStr, Options,
                                     RelocModel, None, CGOptLevel));
}

/**
 * Produce the combined summary index from all the bitcode files:
 * "thin-link".
 */
std::unique_ptr<ModuleSummaryIndex> ThinLTOCodeGenerator::linkCombinedIndex() {
  std::unique_ptr<ModuleSummaryIndex> CombinedIndex =
      llvm::make_unique<ModuleSummaryIndex>(/*HaveGVs=*/false);
  uint64_t NextModuleId = 0;
  for (auto &ModuleBuffer : Modules) {
    if (Error Err = readModuleSummaryIndex(ModuleBuffer.getMemBuffer(),
                                           *CombinedIndex, NextModuleId++)) {
      // FIXME diagnose
      logAllUnhandledErrors(
          std::move(Err), errs(),
          "error: can't create module summary index for buffer: ");
      return nullptr;
    }
  }
  return CombinedIndex;
}

static void internalizeAndPromoteInIndex(
    const StringMap<FunctionImporter::ExportSetTy> &ExportLists,
    const DenseSet<GlobalValue::GUID> &GUIDPreservedSymbols,
    ModuleSummaryIndex &Index) {
  auto isExported = [&](StringRef ModuleIdentifier, GlobalValue::GUID GUID) {
    const auto &ExportList = ExportLists.find(ModuleIdentifier);
    return (ExportList != ExportLists.end() &&
            ExportList->second.count(GUID)) ||
           GUIDPreservedSymbols.count(GUID);
  };

  thinLTOInternalizeAndPromoteInIndex(Index, isExported);
}

static void computeDeadSymbolsInIndex(
    ModuleSummaryIndex &Index,
    const DenseSet<GlobalValue::GUID> &GUIDPreservedSymbols) {
  // We have no symbols resolution available. And can't do any better now in the
  // case where the prevailing symbol is in a native object. It can be refined
  // with linker information in the future.
  auto isPrevailing = [&](GlobalValue::GUID G) {
    return PrevailingType::Unknown;
  };
  computeDeadSymbolsWithConstProp(Index, GUIDPreservedSymbols, isPrevailing,
                                  /* ImportEnabled = */ true);
}

/**
 * Perform promotion and renaming of exported internal functions.
 * Index is updated to reflect linkage changes from weak resolution.
 */
void ThinLTOCodeGenerator::promote(Module &TheModule,
                                   ModuleSummaryIndex &Index) {
  auto ModuleCount = Index.modulePaths().size();
  auto ModuleIdentifier = TheModule.getModuleIdentifier();

  // Collect for each module the list of function it defines (GUID -> Summary).
  StringMap<GVSummaryMapTy> ModuleToDefinedGVSummaries;
  Index.collectDefinedGVSummariesPerModule(ModuleToDefinedGVSummaries);

  // Convert the preserved symbols set from string to GUID
  auto GUIDPreservedSymbols = computeGUIDPreservedSymbols(
      PreservedSymbols, Triple(TheModule.getTargetTriple()));

  // Compute "dead" symbols, we don't want to import/export these!
  computeDeadSymbolsInIndex(Index, GUIDPreservedSymbols);

  // Generate import/export list
  StringMap<FunctionImporter::ImportMapTy> ImportLists(ModuleCount);
  StringMap<FunctionImporter::ExportSetTy> ExportLists(ModuleCount);
  ComputeCrossModuleImport(Index, ModuleToDefinedGVSummaries, ImportLists,
                           ExportLists);

  // Resolve prevailing symbols
  StringMap<std::map<GlobalValue::GUID, GlobalValue::LinkageTypes>> ResolvedODR;
  resolvePrevailingInIndex(Index, ResolvedODR);

  thinLTOResolvePrevailingInModule(
      TheModule, ModuleToDefinedGVSummaries[ModuleIdentifier]);

  // Promote the exported values in the index, so that they are promoted
  // in the module.
  internalizeAndPromoteInIndex(ExportLists, GUIDPreservedSymbols, Index);

  promoteModule(TheModule, Index);
}

/**
 * Perform cross-module importing for the module identified by ModuleIdentifier.
 */
void ThinLTOCodeGenerator::crossModuleImport(Module &TheModule,
                                             ModuleSummaryIndex &Index) {
  auto ModuleMap = generateModuleMap(Modules);
  auto ModuleCount = Index.modulePaths().size();

  // Collect for each module the list of function it defines (GUID -> Summary).
  StringMap<GVSummaryMapTy> ModuleToDefinedGVSummaries(ModuleCount);
  Index.collectDefinedGVSummariesPerModule(ModuleToDefinedGVSummaries);

  // Convert the preserved symbols set from string to GUID
  auto GUIDPreservedSymbols = computeGUIDPreservedSymbols(
      PreservedSymbols, Triple(TheModule.getTargetTriple()));

  // Compute "dead" symbols, we don't want to import/export these!
  computeDeadSymbolsInIndex(Index, GUIDPreservedSymbols);

  // Generate import/export list
  StringMap<FunctionImporter::ImportMapTy> ImportLists(ModuleCount);
  StringMap<FunctionImporter::ExportSetTy> ExportLists(ModuleCount);
  ComputeCrossModuleImport(Index, ModuleToDefinedGVSummaries, ImportLists,
                           ExportLists);
  auto &ImportList = ImportLists[TheModule.getModuleIdentifier()];

  crossImportIntoModule(TheModule, Index, ModuleMap, ImportList);
}

/**
 * Compute the list of summaries needed for importing into module.
 */
void ThinLTOCodeGenerator::gatherImportedSummariesForModule(
    Module &TheModule, ModuleSummaryIndex &Index,
    std::map<std::string, GVSummaryMapTy> &ModuleToSummariesForIndex) {
  auto ModuleCount = Index.modulePaths().size();
  auto ModuleIdentifier = TheModule.getModuleIdentifier();

  // Collect for each module the list of function it defines (GUID -> Summary).
  StringMap<GVSummaryMapTy> ModuleToDefinedGVSummaries(ModuleCount);
  Index.collectDefinedGVSummariesPerModule(ModuleToDefinedGVSummaries);

  // Convert the preserved symbols set from string to GUID
  auto GUIDPreservedSymbols = computeGUIDPreservedSymbols(
      PreservedSymbols, Triple(TheModule.getTargetTriple()));

  // Compute "dead" symbols, we don't want to import/export these!
  computeDeadSymbolsInIndex(Index, GUIDPreservedSymbols);

  // Generate import/export list
  StringMap<FunctionImporter::ImportMapTy> ImportLists(ModuleCount);
  StringMap<FunctionImporter::ExportSetTy> ExportLists(ModuleCount);
  ComputeCrossModuleImport(Index, ModuleToDefinedGVSummaries, ImportLists,
                           ExportLists);

  llvm::gatherImportedSummariesForModule(
      ModuleIdentifier, ModuleToDefinedGVSummaries,
      ImportLists[ModuleIdentifier], ModuleToSummariesForIndex);
}

/**
 * Emit the list of files needed for importing into module.
 */
void ThinLTOCodeGenerator::emitImports(Module &TheModule, StringRef OutputName,
                                       ModuleSummaryIndex &Index) {
  auto ModuleCount = Index.modulePaths().size();
  auto ModuleIdentifier = TheModule.getModuleIdentifier();

  // Collect for each module the list of function it defines (GUID -> Summary).
  StringMap<GVSummaryMapTy> ModuleToDefinedGVSummaries(ModuleCount);
  Index.collectDefinedGVSummariesPerModule(ModuleToDefinedGVSummaries);

  // Convert the preserved symbols set from string to GUID
  auto GUIDPreservedSymbols = computeGUIDPreservedSymbols(
      PreservedSymbols, Triple(TheModule.getTargetTriple()));

  // Compute "dead" symbols, we don't want to import/export these!
  computeDeadSymbolsInIndex(Index, GUIDPreservedSymbols);

  // Generate import/export list
  StringMap<FunctionImporter::ImportMapTy> ImportLists(ModuleCount);
  StringMap<FunctionImporter::ExportSetTy> ExportLists(ModuleCount);
  ComputeCrossModuleImport(Index, ModuleToDefinedGVSummaries, ImportLists,
                           ExportLists);

  std::map<std::string, GVSummaryMapTy> ModuleToSummariesForIndex;
  llvm::gatherImportedSummariesForModule(
      ModuleIdentifier, ModuleToDefinedGVSummaries,
      ImportLists[ModuleIdentifier], ModuleToSummariesForIndex);

  std::error_code EC;
  if ((EC = EmitImportsFiles(ModuleIdentifier, OutputName,
                             ModuleToSummariesForIndex)))
    report_fatal_error(Twine("Failed to open ") + OutputName +
                       " to save imports lists\n");
}

/**
 * Perform internalization. Index is updated to reflect linkage changes.
 */
void ThinLTOCodeGenerator::internalize(Module &TheModule,
                                       ModuleSummaryIndex &Index) {
  initTMBuilder(TMBuilder, Triple(TheModule.getTargetTriple()));
  auto ModuleCount = Index.modulePaths().size();
  auto ModuleIdentifier = TheModule.getModuleIdentifier();

  // Convert the preserved symbols set from string to GUID
  auto GUIDPreservedSymbols =
      computeGUIDPreservedSymbols(PreservedSymbols, TMBuilder.TheTriple);

  // Collect for each module the list of function it defines (GUID -> Summary).
  StringMap<GVSummaryMapTy> ModuleToDefinedGVSummaries(ModuleCount);
  Index.collectDefinedGVSummariesPerModule(ModuleToDefinedGVSummaries);

  // Compute "dead" symbols, we don't want to import/export these!
  computeDeadSymbolsInIndex(Index, GUIDPreservedSymbols);

  // Generate import/export list
  StringMap<FunctionImporter::ImportMapTy> ImportLists(ModuleCount);
  StringMap<FunctionImporter::ExportSetTy> ExportLists(ModuleCount);
  ComputeCrossModuleImport(Index, ModuleToDefinedGVSummaries, ImportLists,
                           ExportLists);
  auto &ExportList = ExportLists[ModuleIdentifier];

  // Be friendly and don't nuke totally the module when the client didn't
  // supply anything to preserve.
  if (ExportList.empty() && GUIDPreservedSymbols.empty())
    return;

  // Internalization
  internalizeAndPromoteInIndex(ExportLists, GUIDPreservedSymbols, Index);
  thinLTOInternalizeModule(TheModule,
                           ModuleToDefinedGVSummaries[ModuleIdentifier]);
}

/**
 * Perform post-importing ThinLTO optimizations.
 */
void ThinLTOCodeGenerator::optimize(Module &TheModule) {
  initTMBuilder(TMBuilder, Triple(TheModule.getTargetTriple()));

  // Optimize now
  optimizeModule(TheModule, *TMBuilder.create(), OptLevel, Freestanding);
}

/// Write out the generated object file, either from CacheEntryPath or from
/// OutputBuffer, preferring hard-link when possible.
/// Returns the path to the generated file in SavedObjectsDirectoryPath.
static std::string writeGeneratedObject(int count, StringRef CacheEntryPath,
                                        StringRef SavedObjectsDirectoryPath,
                                        const MemoryBuffer &OutputBuffer) {
  SmallString<128> OutputPath(SavedObjectsDirectoryPath);
  llvm::sys::path::append(OutputPath, Twine(count) + ".thinlto.o");
  OutputPath.c_str(); // Ensure the string is null terminated.
  if (sys::fs::exists(OutputPath))
    sys::fs::remove(OutputPath);

  // We don't return a memory buffer to the linker, just a list of files.
  if (!CacheEntryPath.empty()) {
    // Cache is enabled, hard-link the entry (or copy if hard-link fails).
    auto Err = sys::fs::create_hard_link(CacheEntryPath, OutputPath);
    if (!Err)
      return OutputPath.str();
    // Hard linking failed, try to copy.
    Err = sys::fs::copy_file(CacheEntryPath, OutputPath);
    if (!Err)
      return OutputPath.str();
    // Copy failed (could be because the CacheEntry was removed from the cache
    // in the meantime by another process), fall back and try to write down the
    // buffer to the output.
    errs() << "error: can't link or copy from cached entry '" << CacheEntryPath
           << "' to '" << OutputPath << "'\n";
  }
  // No cache entry, just write out the buffer.
  std::error_code Err;
  raw_fd_ostream OS(OutputPath, Err, sys::fs::F_None);
  if (Err)
    report_fatal_error("Can't open output '" + OutputPath + "'\n");
  OS << OutputBuffer.getBuffer();
  return OutputPath.str();
}

// Main entry point for the ThinLTO processing
void ThinLTOCodeGenerator::run() {
  // Prepare the resulting object vector
  assert(ProducedBinaries.empty() && "The generator should not be reused");
  if (SavedObjectsDirectoryPath.empty())
    ProducedBinaries.resize(Modules.size());
  else {
    sys::fs::create_directories(SavedObjectsDirectoryPath);
    bool IsDir;
    sys::fs::is_directory(SavedObjectsDirectoryPath, IsDir);
    if (!IsDir)
      report_fatal_error("Unexistent dir: '" + SavedObjectsDirectoryPath + "'");
    ProducedBinaryFiles.resize(Modules.size());
  }

  if (CodeGenOnly) {
    // Perform only parallel codegen and return.
    ThreadPool Pool;
    int count = 0;
    for (auto &ModuleBuffer : Modules) {
      Pool.async([&](int count) {
        LLVMContext Context;
        Context.setDiscardValueNames(LTODiscardValueNames);

        // Parse module now
        auto TheModule =
            loadModuleFromBuffer(ModuleBuffer.getMemBuffer(), Context, false,
                                 /*IsImporting*/ false);

        // CodeGen
        auto OutputBuffer = codegenModule(*TheModule, *TMBuilder.create());
        if (SavedObjectsDirectoryPath.empty())
          ProducedBinaries[count] = std::move(OutputBuffer);
        else
          ProducedBinaryFiles[count] = writeGeneratedObject(
              count, "", SavedObjectsDirectoryPath, *OutputBuffer);
      }, count++);
    }

    return;
  }

  // Sequential linking phase
  auto Index = linkCombinedIndex();

  // Save temps: index.
  if (!SaveTempsDir.empty()) {
    auto SaveTempPath = SaveTempsDir + "index.bc";
    std::error_code EC;
    raw_fd_ostream OS(SaveTempPath, EC, sys::fs::F_None);
    if (EC)
      report_fatal_error(Twine("Failed to open ") + SaveTempPath +
                         " to save optimized bitcode\n");
    WriteIndexToFile(*Index, OS);
  }


  // Prepare the module map.
  auto ModuleMap = generateModuleMap(Modules);
  auto ModuleCount = Modules.size();

  // Collect for each module the list of function it defines (GUID -> Summary).
  StringMap<GVSummaryMapTy> ModuleToDefinedGVSummaries(ModuleCount);
  Index->collectDefinedGVSummariesPerModule(ModuleToDefinedGVSummaries);

  // Convert the preserved symbols set from string to GUID, this is needed for
  // computing the caching hash and the internalization.
  auto GUIDPreservedSymbols =
      computeGUIDPreservedSymbols(PreservedSymbols, TMBuilder.TheTriple);

  // Compute "dead" symbols, we don't want to import/export these!
  computeDeadSymbolsInIndex(*Index, GUIDPreservedSymbols);

  // Synthesize entry counts for functions in the combined index.
  computeSyntheticCounts(*Index);

  // Collect the import/export lists for all modules from the call-graph in the
  // combined index.
  StringMap<FunctionImporter::ImportMapTy> ImportLists(ModuleCount);
  StringMap<FunctionImporter::ExportSetTy> ExportLists(ModuleCount);
  ComputeCrossModuleImport(*Index, ModuleToDefinedGVSummaries, ImportLists,
                           ExportLists);

  // We use a std::map here to be able to have a defined ordering when
  // producing a hash for the cache entry.
  // FIXME: we should be able to compute the caching hash for the entry based
  // on the index, and nuke this map.
  StringMap<std::map<GlobalValue::GUID, GlobalValue::LinkageTypes>> ResolvedODR;

  // Resolve prevailing symbols, this has to be computed early because it
  // impacts the caching.
  resolvePrevailingInIndex(*Index, ResolvedODR);

  // Use global summary-based analysis to identify symbols that can be
  // internalized (because they aren't exported or preserved as per callback).
  // Changes are made in the index, consumed in the ThinLTO backends.
  internalizeAndPromoteInIndex(ExportLists, GUIDPreservedSymbols, *Index);

  // Make sure that every module has an entry in the ExportLists, ImportList,
  // GVSummary and ResolvedODR maps to enable threaded access to these maps
  // below.
  for (auto &Module : Modules) {
    auto ModuleIdentifier = Module.getBufferIdentifier();
    ExportLists[ModuleIdentifier];
    ImportLists[ModuleIdentifier];
    ResolvedODR[ModuleIdentifier];
    ModuleToDefinedGVSummaries[ModuleIdentifier];
  }

  // Compute the ordering we will process the inputs: the rough heuristic here
  // is to sort them per size so that the largest module get schedule as soon as
  // possible. This is purely a compile-time optimization.
  std::vector<int> ModulesOrdering;
  ModulesOrdering.resize(Modules.size());
  std::iota(ModulesOrdering.begin(), ModulesOrdering.end(), 0);
  llvm::sort(ModulesOrdering, [&](int LeftIndex, int RightIndex) {
    auto LSize = Modules[LeftIndex].getBuffer().size();
    auto RSize = Modules[RightIndex].getBuffer().size();
    return LSize > RSize;
  });

  // Parallel optimizer + codegen
  {
    ThreadPool Pool(ThreadCount);
    for (auto IndexCount : ModulesOrdering) {
      auto &ModuleBuffer = Modules[IndexCount];
      Pool.async([&](int count) {
        auto ModuleIdentifier = ModuleBuffer.getBufferIdentifier();
        auto &ExportList = ExportLists[ModuleIdentifier];

        auto &DefinedGVSummaries = ModuleToDefinedGVSummaries[ModuleIdentifier];

        // The module may be cached, this helps handling it.
        ModuleCacheEntry CacheEntry(CacheOptions.Path, *Index, ModuleIdentifier,
                                    ImportLists[ModuleIdentifier], ExportList,
                                    ResolvedODR[ModuleIdentifier],
                                    DefinedGVSummaries, OptLevel, Freestanding,
                                    TMBuilder);
        auto CacheEntryPath = CacheEntry.getEntryPath();

        {
          auto ErrOrBuffer = CacheEntry.tryLoadingBuffer();
          LLVM_DEBUG(dbgs() << "Cache " << (ErrOrBuffer ? "hit" : "miss")
                            << " '" << CacheEntryPath << "' for buffer "
                            << count << " " << ModuleIdentifier << "\n");

          if (ErrOrBuffer) {
            // Cache Hit!
            if (SavedObjectsDirectoryPath.empty())
              ProducedBinaries[count] = std::move(ErrOrBuffer.get());
            else
              ProducedBinaryFiles[count] = writeGeneratedObject(
                  count, CacheEntryPath, SavedObjectsDirectoryPath,
                  *ErrOrBuffer.get());
            return;
          }
        }

        LLVMContext Context;
        Context.setDiscardValueNames(LTODiscardValueNames);
        Context.enableDebugTypeODRUniquing();
        auto DiagFileOrErr = lto::setupOptimizationRemarks(
            Context, LTORemarksFilename, LTOPassRemarksWithHotness, count);
        if (!DiagFileOrErr) {
          errs() << "Error: " << toString(DiagFileOrErr.takeError()) << "\n";
          report_fatal_error("ThinLTO: Can't get an output file for the "
                             "remarks");
        }

        // Parse module now
        auto TheModule =
            loadModuleFromBuffer(ModuleBuffer.getMemBuffer(), Context, false,
                                 /*IsImporting*/ false);

        // Save temps: original file.
        saveTempBitcode(*TheModule, SaveTempsDir, count, ".0.original.bc");

        auto &ImportList = ImportLists[ModuleIdentifier];
        // Run the main process now, and generates a binary
        auto OutputBuffer = ProcessThinLTOModule(
            *TheModule, *Index, ModuleMap, *TMBuilder.create(), ImportList,
            ExportList, GUIDPreservedSymbols,
            ModuleToDefinedGVSummaries[ModuleIdentifier], CacheOptions,
            DisableCodeGen, SaveTempsDir, Freestanding, OptLevel, count);

        // Commit to the cache (if enabled)
        CacheEntry.write(*OutputBuffer);

        if (SavedObjectsDirectoryPath.empty()) {
          // We need to generated a memory buffer for the linker.
          if (!CacheEntryPath.empty()) {
            // When cache is enabled, reload from the cache if possible.
            // Releasing the buffer from the heap and reloading it from the
            // cache file with mmap helps us to lower memory pressure.
            // The freed memory can be used for the next input file.
            // The final binary link will read from the VFS cache (hopefully!)
            // or from disk (if the memory pressure was too high).
            auto ReloadedBufferOrErr = CacheEntry.tryLoadingBuffer();
            if (auto EC = ReloadedBufferOrErr.getError()) {
              // On error, keep the preexisting buffer and print a diagnostic.
              errs() << "error: can't reload cached file '" << CacheEntryPath
                     << "': " << EC.message() << "\n";
            } else {
              OutputBuffer = std::move(*ReloadedBufferOrErr);
            }
          }
          ProducedBinaries[count] = std::move(OutputBuffer);
          return;
        }
        ProducedBinaryFiles[count] = writeGeneratedObject(
            count, CacheEntryPath, SavedObjectsDirectoryPath, *OutputBuffer);
      }, IndexCount);
    }
  }

  pruneCache(CacheOptions.Path, CacheOptions.Policy);

  // If statistics were requested, print them out now.
  if (llvm::AreStatisticsEnabled())
    llvm::PrintStatistics();
  reportAndResetTimings();
}
