//===- llvm-lto: a simple command-line program to link modules with LTO ---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This program takes in a list of bitcode files, links them, performs link-time
// optimization, and outputs an object file.
//
//===----------------------------------------------------------------------===//

#include "llvm-c/lto.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/CodeGen/CommandFlags.inc"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/LTO/legacy/LTOCodeGenerator.h"
#include "llvm/LTO/legacy/LTOModule.h"
#include "llvm/LTO/legacy/ThinLTOCodeGenerator.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetOptions.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

using namespace llvm;

static cl::opt<char>
    OptLevel("O", cl::desc("Optimization level. [-O0, -O1, -O2, or -O3] "
                           "(default = '-O2')"),
             cl::Prefix, cl::ZeroOrMore, cl::init('2'));

static cl::opt<bool>
    IndexStats("thinlto-index-stats",
               cl::desc("Print statistic for the index in every input files"),
               cl::init(false));

static cl::opt<bool> DisableVerify(
    "disable-verify", cl::init(false),
    cl::desc("Do not run the verifier during the optimization pipeline"));

static cl::opt<bool> DisableInline("disable-inlining", cl::init(false),
                                   cl::desc("Do not run the inliner pass"));

static cl::opt<bool>
    DisableGVNLoadPRE("disable-gvn-loadpre", cl::init(false),
                      cl::desc("Do not run the GVN load PRE pass"));

static cl::opt<bool> DisableLTOVectorization(
    "disable-lto-vectorization", cl::init(false),
    cl::desc("Do not run loop or slp vectorization during LTO"));

static cl::opt<bool> EnableFreestanding(
    "lto-freestanding", cl::init(false),
    cl::desc("Enable Freestanding (disable builtins / TLI) during LTO"));

static cl::opt<bool> UseDiagnosticHandler(
    "use-diagnostic-handler", cl::init(false),
    cl::desc("Use a diagnostic handler to test the handler interface"));

static cl::opt<bool>
    ThinLTO("thinlto", cl::init(false),
            cl::desc("Only write combined global index for ThinLTO backends"));

enum ThinLTOModes {
  THINLINK,
  THINDISTRIBUTE,
  THINEMITIMPORTS,
  THINPROMOTE,
  THINIMPORT,
  THININTERNALIZE,
  THINOPT,
  THINCODEGEN,
  THINALL
};

cl::opt<ThinLTOModes> ThinLTOMode(
    "thinlto-action", cl::desc("Perform a single ThinLTO stage:"),
    cl::values(
        clEnumValN(
            THINLINK, "thinlink",
            "ThinLink: produces the index by linking only the summaries."),
        clEnumValN(THINDISTRIBUTE, "distributedindexes",
                   "Produces individual indexes for distributed backends."),
        clEnumValN(THINEMITIMPORTS, "emitimports",
                   "Emit imports files for distributed backends."),
        clEnumValN(THINPROMOTE, "promote",
                   "Perform pre-import promotion (requires -thinlto-index)."),
        clEnumValN(THINIMPORT, "import", "Perform both promotion and "
                                         "cross-module importing (requires "
                                         "-thinlto-index)."),
        clEnumValN(THININTERNALIZE, "internalize",
                   "Perform internalization driven by -exported-symbol "
                   "(requires -thinlto-index)."),
        clEnumValN(THINOPT, "optimize", "Perform ThinLTO optimizations."),
        clEnumValN(THINCODEGEN, "codegen", "CodeGen (expected to match llc)"),
        clEnumValN(THINALL, "run", "Perform ThinLTO end-to-end")));

static cl::opt<std::string>
    ThinLTOIndex("thinlto-index",
                 cl::desc("Provide the index produced by a ThinLink, required "
                          "to perform the promotion and/or importing."));

static cl::opt<std::string> ThinLTOPrefixReplace(
    "thinlto-prefix-replace",
    cl::desc("Control where files for distributed backends are "
             "created. Expects 'oldprefix;newprefix' and if path "
             "prefix of output file is oldprefix it will be "
             "replaced with newprefix."));

static cl::opt<std::string> ThinLTOModuleId(
    "thinlto-module-id",
    cl::desc("For the module ID for the file to process, useful to "
             "match what is in the index."));

static cl::opt<std::string>
    ThinLTOCacheDir("thinlto-cache-dir", cl::desc("Enable ThinLTO caching."));

static cl::opt<int>
    ThinLTOCachePruningInterval("thinlto-cache-pruning-interval",
    cl::init(1200), cl::desc("Set ThinLTO cache pruning interval."));

static cl::opt<unsigned long long>
    ThinLTOCacheMaxSizeBytes("thinlto-cache-max-size-bytes",
    cl::desc("Set ThinLTO cache pruning directory maximum size in bytes."));

static cl::opt<int>
    ThinLTOCacheMaxSizeFiles("thinlto-cache-max-size-files", cl::init(1000000),
    cl::desc("Set ThinLTO cache pruning directory maximum number of files."));

static cl::opt<unsigned>
    ThinLTOCacheEntryExpiration("thinlto-cache-entry-expiration", cl::init(604800) /* 1w */,
    cl::desc("Set ThinLTO cache entry expiration time."));

static cl::opt<std::string> ThinLTOSaveTempsPrefix(
    "thinlto-save-temps",
    cl::desc("Save ThinLTO temp files using filenames created by adding "
             "suffixes to the given file path prefix."));

static cl::opt<std::string> ThinLTOGeneratedObjectsDir(
    "thinlto-save-objects",
    cl::desc("Save ThinLTO generated object files using filenames created in "
             "the given directory."));

static cl::opt<bool>
    SaveModuleFile("save-merged-module", cl::init(false),
                   cl::desc("Write merged LTO module to file before CodeGen"));

static cl::list<std::string> InputFilenames(cl::Positional, cl::OneOrMore,
                                            cl::desc("<input bitcode files>"));

static cl::opt<std::string> OutputFilename("o", cl::init(""),
                                           cl::desc("Override output filename"),
                                           cl::value_desc("filename"));

static cl::list<std::string> ExportedSymbols(
    "exported-symbol",
    cl::desc("List of symbols to export from the resulting object file"),
    cl::ZeroOrMore);

static cl::list<std::string>
    DSOSymbols("dso-symbol",
               cl::desc("Symbol to put in the symtab in the resulting dso"),
               cl::ZeroOrMore);

static cl::opt<bool> ListSymbolsOnly(
    "list-symbols-only", cl::init(false),
    cl::desc("Instead of running LTO, list the symbols in each IR file"));

static cl::opt<bool> SetMergedModule(
    "set-merged-module", cl::init(false),
    cl::desc("Use the first input module as the merged module"));

static cl::opt<unsigned> Parallelism("j", cl::Prefix, cl::init(1),
                                     cl::desc("Number of backend threads"));

static cl::opt<bool> RestoreGlobalsLinkage(
    "restore-linkage", cl::init(false),
    cl::desc("Restore original linkage of globals prior to CodeGen"));

static cl::opt<bool> CheckHasObjC(
    "check-for-objc", cl::init(false),
    cl::desc("Only check if the module has objective-C defined in it"));

namespace {

struct ModuleInfo {
  std::vector<bool> CanBeHidden;
};

} // end anonymous namespace

static void handleDiagnostics(lto_codegen_diagnostic_severity_t Severity,
                              const char *Msg, void *) {
  errs() << "llvm-lto: ";
  switch (Severity) {
  case LTO_DS_NOTE:
    errs() << "note: ";
    break;
  case LTO_DS_REMARK:
    errs() << "remark: ";
    break;
  case LTO_DS_ERROR:
    errs() << "error: ";
    break;
  case LTO_DS_WARNING:
    errs() << "warning: ";
    break;
  }
  errs() << Msg << "\n";
}

static std::string CurrentActivity;

namespace {
  struct LLVMLTODiagnosticHandler : public DiagnosticHandler {
    bool handleDiagnostics(const DiagnosticInfo &DI) override {
      raw_ostream &OS = errs();
      OS << "llvm-lto: ";
      switch (DI.getSeverity()) {
      case DS_Error:
        OS << "error";
        break;
      case DS_Warning:
        OS << "warning";
        break;
      case DS_Remark:
        OS << "remark";
        break;
      case DS_Note:
        OS << "note";
        break;
      }
      if (!CurrentActivity.empty())
        OS << ' ' << CurrentActivity;
      OS << ": ";
  
      DiagnosticPrinterRawOStream DP(OS);
      DI.print(DP);
      OS << '\n';
  
      if (DI.getSeverity() == DS_Error)
        exit(1);
      return true;
    }
  };
  }

static void error(const Twine &Msg) {
  errs() << "llvm-lto: " << Msg << '\n';
  exit(1);
}

static void error(std::error_code EC, const Twine &Prefix) {
  if (EC)
    error(Prefix + ": " + EC.message());
}

template <typename T>
static void error(const ErrorOr<T> &V, const Twine &Prefix) {
  error(V.getError(), Prefix);
}

static void maybeVerifyModule(const Module &Mod) {
  if (!DisableVerify && verifyModule(Mod, &errs()))
    error("Broken Module");
}

static std::unique_ptr<LTOModule>
getLocalLTOModule(StringRef Path, std::unique_ptr<MemoryBuffer> &Buffer,
                  const TargetOptions &Options) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> BufferOrErr =
      MemoryBuffer::getFile(Path);
  error(BufferOrErr, "error loading file '" + Path + "'");
  Buffer = std::move(BufferOrErr.get());
  CurrentActivity = ("loading file '" + Path + "'").str();
  std::unique_ptr<LLVMContext> Context = llvm::make_unique<LLVMContext>();
  Context->setDiagnosticHandler(llvm::make_unique<LLVMLTODiagnosticHandler>(),
                                true);
  ErrorOr<std::unique_ptr<LTOModule>> Ret = LTOModule::createInLocalContext(
      std::move(Context), Buffer->getBufferStart(), Buffer->getBufferSize(),
      Options, Path);
  CurrentActivity = "";
  maybeVerifyModule((*Ret)->getModule());
  return std::move(*Ret);
}

/// Print some statistics on the index for each input files.
void printIndexStats() {
  for (auto &Filename : InputFilenames) {
    ExitOnError ExitOnErr("llvm-lto: error loading file '" + Filename + "': ");
    std::unique_ptr<ModuleSummaryIndex> Index =
        ExitOnErr(getModuleSummaryIndexForFile(Filename));
    // Skip files without a module summary.
    if (!Index)
      report_fatal_error(Filename + " does not contain an index");

    unsigned Calls = 0, Refs = 0, Functions = 0, Alias = 0, Globals = 0;
    for (auto &Summaries : *Index) {
      for (auto &Summary : Summaries.second.SummaryList) {
        Refs += Summary->refs().size();
        if (auto *FuncSummary = dyn_cast<FunctionSummary>(Summary.get())) {
          Functions++;
          Calls += FuncSummary->calls().size();
        } else if (isa<AliasSummary>(Summary.get()))
          Alias++;
        else
          Globals++;
      }
    }
    outs() << "Index " << Filename << " contains "
           << (Alias + Globals + Functions) << " nodes (" << Functions
           << " functions, " << Alias << " alias, " << Globals
           << " globals) and " << (Calls + Refs) << " edges (" << Refs
           << " refs and " << Calls << " calls)\n";
  }
}

/// List symbols in each IR file.
///
/// The main point here is to provide lit-testable coverage for the LTOModule
/// functionality that's exposed by the C API to list symbols.  Moreover, this
/// provides testing coverage for modules that have been created in their own
/// contexts.
static void listSymbols(const TargetOptions &Options) {
  for (auto &Filename : InputFilenames) {
    std::unique_ptr<MemoryBuffer> Buffer;
    std::unique_ptr<LTOModule> Module =
        getLocalLTOModule(Filename, Buffer, Options);

    // List the symbols.
    outs() << Filename << ":\n";
    for (int I = 0, E = Module->getSymbolCount(); I != E; ++I)
      outs() << Module->getSymbolName(I) << "\n";
  }
}

/// Create a combined index file from the input IR files and write it.
///
/// This is meant to enable testing of ThinLTO combined index generation,
/// currently available via the gold plugin via -thinlto.
static void createCombinedModuleSummaryIndex() {
  ModuleSummaryIndex CombinedIndex(/*HaveGVs=*/false);
  uint64_t NextModuleId = 0;
  for (auto &Filename : InputFilenames) {
    ExitOnError ExitOnErr("llvm-lto: error loading file '" + Filename + "': ");
    std::unique_ptr<MemoryBuffer> MB =
        ExitOnErr(errorOrToExpected(MemoryBuffer::getFileOrSTDIN(Filename)));
    ExitOnErr(readModuleSummaryIndex(*MB, CombinedIndex, NextModuleId++));
  }
  std::error_code EC;
  assert(!OutputFilename.empty());
  raw_fd_ostream OS(OutputFilename + ".thinlto.bc", EC,
                    sys::fs::OpenFlags::F_None);
  error(EC, "error opening the file '" + OutputFilename + ".thinlto.bc'");
  WriteIndexToFile(CombinedIndex, OS);
  OS.close();
}

/// Parse the thinlto_prefix_replace option into the \p OldPrefix and
/// \p NewPrefix strings, if it was specified.
static void getThinLTOOldAndNewPrefix(std::string &OldPrefix,
                                      std::string &NewPrefix) {
  assert(ThinLTOPrefixReplace.empty() ||
         ThinLTOPrefixReplace.find(";") != StringRef::npos);
  StringRef PrefixReplace = ThinLTOPrefixReplace;
  std::pair<StringRef, StringRef> Split = PrefixReplace.split(";");
  OldPrefix = Split.first.str();
  NewPrefix = Split.second.str();
}

/// Given the original \p Path to an output file, replace any path
/// prefix matching \p OldPrefix with \p NewPrefix. Also, create the
/// resulting directory if it does not yet exist.
static std::string getThinLTOOutputFile(const std::string &Path,
                                        const std::string &OldPrefix,
                                        const std::string &NewPrefix) {
  if (OldPrefix.empty() && NewPrefix.empty())
    return Path;
  SmallString<128> NewPath(Path);
  llvm::sys::path::replace_path_prefix(NewPath, OldPrefix, NewPrefix);
  StringRef ParentPath = llvm::sys::path::parent_path(NewPath.str());
  if (!ParentPath.empty()) {
    // Make sure the new directory exists, creating it if necessary.
    if (std::error_code EC = llvm::sys::fs::create_directories(ParentPath))
      error(EC, "error creating the directory '" + ParentPath + "'");
  }
  return NewPath.str();
}

namespace thinlto {

std::vector<std::unique_ptr<MemoryBuffer>>
loadAllFilesForIndex(const ModuleSummaryIndex &Index) {
  std::vector<std::unique_ptr<MemoryBuffer>> InputBuffers;

  for (auto &ModPath : Index.modulePaths()) {
    const auto &Filename = ModPath.first();
    std::string CurrentActivity = ("loading file '" + Filename + "'").str();
    auto InputOrErr = MemoryBuffer::getFile(Filename);
    error(InputOrErr, "error " + CurrentActivity);
    InputBuffers.push_back(std::move(*InputOrErr));
  }
  return InputBuffers;
}

std::unique_ptr<ModuleSummaryIndex> loadCombinedIndex() {
  if (ThinLTOIndex.empty())
    report_fatal_error("Missing -thinlto-index for ThinLTO promotion stage");
  ExitOnError ExitOnErr("llvm-lto: error loading file '" + ThinLTOIndex +
                        "': ");
  return ExitOnErr(getModuleSummaryIndexForFile(ThinLTOIndex));
}

static std::unique_ptr<Module> loadModule(StringRef Filename,
                                          LLVMContext &Ctx) {
  SMDiagnostic Err;
  std::unique_ptr<Module> M(parseIRFile(Filename, Err, Ctx));
  if (!M) {
    Err.print("llvm-lto", errs());
    report_fatal_error("Can't load module for file " + Filename);
  }
  maybeVerifyModule(*M);

  if (ThinLTOModuleId.getNumOccurrences()) {
    if (InputFilenames.size() != 1)
      report_fatal_error("Can't override the module id for multiple files");
    M->setModuleIdentifier(ThinLTOModuleId);
  }
  return M;
}

static void writeModuleToFile(Module &TheModule, StringRef Filename) {
  std::error_code EC;
  raw_fd_ostream OS(Filename, EC, sys::fs::OpenFlags::F_None);
  error(EC, "error opening the file '" + Filename + "'");
  maybeVerifyModule(TheModule);
  WriteBitcodeToFile(TheModule, OS, /* ShouldPreserveUseListOrder */ true);
}

class ThinLTOProcessing {
public:
  ThinLTOCodeGenerator ThinGenerator;

  ThinLTOProcessing(const TargetOptions &Options) {
    ThinGenerator.setCodePICModel(getRelocModel());
    ThinGenerator.setTargetOptions(Options);
    ThinGenerator.setCacheDir(ThinLTOCacheDir);
    ThinGenerator.setCachePruningInterval(ThinLTOCachePruningInterval);
    ThinGenerator.setCacheEntryExpiration(ThinLTOCacheEntryExpiration);
    ThinGenerator.setCacheMaxSizeFiles(ThinLTOCacheMaxSizeFiles);
    ThinGenerator.setCacheMaxSizeBytes(ThinLTOCacheMaxSizeBytes);
    ThinGenerator.setFreestanding(EnableFreestanding);

    // Add all the exported symbols to the table of symbols to preserve.
    for (unsigned i = 0; i < ExportedSymbols.size(); ++i)
      ThinGenerator.preserveSymbol(ExportedSymbols[i]);
  }

  void run() {
    switch (ThinLTOMode) {
    case THINLINK:
      return thinLink();
    case THINDISTRIBUTE:
      return distributedIndexes();
    case THINEMITIMPORTS:
      return emitImports();
    case THINPROMOTE:
      return promote();
    case THINIMPORT:
      return import();
    case THININTERNALIZE:
      return internalize();
    case THINOPT:
      return optimize();
    case THINCODEGEN:
      return codegen();
    case THINALL:
      return runAll();
    }
  }

private:
  /// Load the input files, create the combined index, and write it out.
  void thinLink() {
    // Perform "ThinLink": just produce the index
    if (OutputFilename.empty())
      report_fatal_error(
          "OutputFilename is necessary to store the combined index.\n");

    LLVMContext Ctx;
    std::vector<std::unique_ptr<MemoryBuffer>> InputBuffers;
    for (unsigned i = 0; i < InputFilenames.size(); ++i) {
      auto &Filename = InputFilenames[i];
      std::string CurrentActivity = "loading file '" + Filename + "'";
      auto InputOrErr = MemoryBuffer::getFile(Filename);
      error(InputOrErr, "error " + CurrentActivity);
      InputBuffers.push_back(std::move(*InputOrErr));
      ThinGenerator.addModule(Filename, InputBuffers.back()->getBuffer());
    }

    auto CombinedIndex = ThinGenerator.linkCombinedIndex();
    if (!CombinedIndex)
      report_fatal_error("ThinLink didn't create an index");
    std::error_code EC;
    raw_fd_ostream OS(OutputFilename, EC, sys::fs::OpenFlags::F_None);
    error(EC, "error opening the file '" + OutputFilename + "'");
    WriteIndexToFile(*CombinedIndex, OS);
  }

  /// Load the combined index from disk, then compute and generate
  /// individual index files suitable for ThinLTO distributed backend builds
  /// on the files mentioned on the command line (these must match the index
  /// content).
  void distributedIndexes() {
    if (InputFilenames.size() != 1 && !OutputFilename.empty())
      report_fatal_error("Can't handle a single output filename and multiple "
                         "input files, do not provide an output filename and "
                         "the output files will be suffixed from the input "
                         "ones.");

    std::string OldPrefix, NewPrefix;
    getThinLTOOldAndNewPrefix(OldPrefix, NewPrefix);

    auto Index = loadCombinedIndex();
    for (auto &Filename : InputFilenames) {
      LLVMContext Ctx;
      auto TheModule = loadModule(Filename, Ctx);

      // Build a map of module to the GUIDs and summary objects that should
      // be written to its index.
      std::map<std::string, GVSummaryMapTy> ModuleToSummariesForIndex;
      ThinGenerator.gatherImportedSummariesForModule(*TheModule, *Index,
                                                     ModuleToSummariesForIndex);

      std::string OutputName = OutputFilename;
      if (OutputName.empty()) {
        OutputName = Filename + ".thinlto.bc";
      }
      OutputName = getThinLTOOutputFile(OutputName, OldPrefix, NewPrefix);
      std::error_code EC;
      raw_fd_ostream OS(OutputName, EC, sys::fs::OpenFlags::F_None);
      error(EC, "error opening the file '" + OutputName + "'");
      WriteIndexToFile(*Index, OS, &ModuleToSummariesForIndex);
    }
  }

  /// Load the combined index from disk, compute the imports, and emit
  /// the import file lists for each module to disk.
  void emitImports() {
    if (InputFilenames.size() != 1 && !OutputFilename.empty())
      report_fatal_error("Can't handle a single output filename and multiple "
                         "input files, do not provide an output filename and "
                         "the output files will be suffixed from the input "
                         "ones.");

    std::string OldPrefix, NewPrefix;
    getThinLTOOldAndNewPrefix(OldPrefix, NewPrefix);

    auto Index = loadCombinedIndex();
    for (auto &Filename : InputFilenames) {
      LLVMContext Ctx;
      auto TheModule = loadModule(Filename, Ctx);
      std::string OutputName = OutputFilename;
      if (OutputName.empty()) {
        OutputName = Filename + ".imports";
      }
      OutputName = getThinLTOOutputFile(OutputName, OldPrefix, NewPrefix);
      ThinGenerator.emitImports(*TheModule, OutputName, *Index);
    }
  }

  /// Load the combined index from disk, then load every file referenced by
  /// the index and add them to the generator, finally perform the promotion
  /// on the files mentioned on the command line (these must match the index
  /// content).
  void promote() {
    if (InputFilenames.size() != 1 && !OutputFilename.empty())
      report_fatal_error("Can't handle a single output filename and multiple "
                         "input files, do not provide an output filename and "
                         "the output files will be suffixed from the input "
                         "ones.");

    auto Index = loadCombinedIndex();
    for (auto &Filename : InputFilenames) {
      LLVMContext Ctx;
      auto TheModule = loadModule(Filename, Ctx);

      ThinGenerator.promote(*TheModule, *Index);

      std::string OutputName = OutputFilename;
      if (OutputName.empty()) {
        OutputName = Filename + ".thinlto.promoted.bc";
      }
      writeModuleToFile(*TheModule, OutputName);
    }
  }

  /// Load the combined index from disk, then load every file referenced by
  /// the index and add them to the generator, then performs the promotion and
  /// cross module importing on the files mentioned on the command line
  /// (these must match the index content).
  void import() {
    if (InputFilenames.size() != 1 && !OutputFilename.empty())
      report_fatal_error("Can't handle a single output filename and multiple "
                         "input files, do not provide an output filename and "
                         "the output files will be suffixed from the input "
                         "ones.");

    auto Index = loadCombinedIndex();
    auto InputBuffers = loadAllFilesForIndex(*Index);
    for (auto &MemBuffer : InputBuffers)
      ThinGenerator.addModule(MemBuffer->getBufferIdentifier(),
                              MemBuffer->getBuffer());

    for (auto &Filename : InputFilenames) {
      LLVMContext Ctx;
      auto TheModule = loadModule(Filename, Ctx);

      ThinGenerator.crossModuleImport(*TheModule, *Index);

      std::string OutputName = OutputFilename;
      if (OutputName.empty()) {
        OutputName = Filename + ".thinlto.imported.bc";
      }
      writeModuleToFile(*TheModule, OutputName);
    }
  }

  void internalize() {
    if (InputFilenames.size() != 1 && !OutputFilename.empty())
      report_fatal_error("Can't handle a single output filename and multiple "
                         "input files, do not provide an output filename and "
                         "the output files will be suffixed from the input "
                         "ones.");

    if (ExportedSymbols.empty())
      errs() << "Warning: -internalize will not perform without "
                "-exported-symbol\n";

    auto Index = loadCombinedIndex();
    auto InputBuffers = loadAllFilesForIndex(*Index);
    for (auto &MemBuffer : InputBuffers)
      ThinGenerator.addModule(MemBuffer->getBufferIdentifier(),
                              MemBuffer->getBuffer());

    for (auto &Filename : InputFilenames) {
      LLVMContext Ctx;
      auto TheModule = loadModule(Filename, Ctx);

      ThinGenerator.internalize(*TheModule, *Index);

      std::string OutputName = OutputFilename;
      if (OutputName.empty()) {
        OutputName = Filename + ".thinlto.internalized.bc";
      }
      writeModuleToFile(*TheModule, OutputName);
    }
  }

  void optimize() {
    if (InputFilenames.size() != 1 && !OutputFilename.empty())
      report_fatal_error("Can't handle a single output filename and multiple "
                         "input files, do not provide an output filename and "
                         "the output files will be suffixed from the input "
                         "ones.");
    if (!ThinLTOIndex.empty())
      errs() << "Warning: -thinlto-index ignored for optimize stage";

    for (auto &Filename : InputFilenames) {
      LLVMContext Ctx;
      auto TheModule = loadModule(Filename, Ctx);

      ThinGenerator.optimize(*TheModule);

      std::string OutputName = OutputFilename;
      if (OutputName.empty()) {
        OutputName = Filename + ".thinlto.imported.bc";
      }
      writeModuleToFile(*TheModule, OutputName);
    }
  }

  void codegen() {
    if (InputFilenames.size() != 1 && !OutputFilename.empty())
      report_fatal_error("Can't handle a single output filename and multiple "
                         "input files, do not provide an output filename and "
                         "the output files will be suffixed from the input "
                         "ones.");
    if (!ThinLTOIndex.empty())
      errs() << "Warning: -thinlto-index ignored for codegen stage";

    std::vector<std::unique_ptr<MemoryBuffer>> InputBuffers;
    for (auto &Filename : InputFilenames) {
      LLVMContext Ctx;
      auto InputOrErr = MemoryBuffer::getFile(Filename);
      error(InputOrErr, "error " + CurrentActivity);
      InputBuffers.push_back(std::move(*InputOrErr));
      ThinGenerator.addModule(Filename, InputBuffers.back()->getBuffer());
    }
    ThinGenerator.setCodeGenOnly(true);
    ThinGenerator.run();
    for (auto BinName :
         zip(ThinGenerator.getProducedBinaries(), InputFilenames)) {
      std::string OutputName = OutputFilename;
      if (OutputName.empty())
        OutputName = std::get<1>(BinName) + ".thinlto.o";
      else if (OutputName == "-") {
        outs() << std::get<0>(BinName)->getBuffer();
        return;
      }

      std::error_code EC;
      raw_fd_ostream OS(OutputName, EC, sys::fs::OpenFlags::F_None);
      error(EC, "error opening the file '" + OutputName + "'");
      OS << std::get<0>(BinName)->getBuffer();
    }
  }

  /// Full ThinLTO process
  void runAll() {
    if (!OutputFilename.empty())
      report_fatal_error("Do not provide an output filename for ThinLTO "
                         " processing, the output files will be suffixed from "
                         "the input ones.");

    if (!ThinLTOIndex.empty())
      errs() << "Warning: -thinlto-index ignored for full ThinLTO process";

    LLVMContext Ctx;
    std::vector<std::unique_ptr<MemoryBuffer>> InputBuffers;
    for (unsigned i = 0; i < InputFilenames.size(); ++i) {
      auto &Filename = InputFilenames[i];
      std::string CurrentActivity = "loading file '" + Filename + "'";
      auto InputOrErr = MemoryBuffer::getFile(Filename);
      error(InputOrErr, "error " + CurrentActivity);
      InputBuffers.push_back(std::move(*InputOrErr));
      ThinGenerator.addModule(Filename, InputBuffers.back()->getBuffer());
    }

    if (!ThinLTOSaveTempsPrefix.empty())
      ThinGenerator.setSaveTempsDir(ThinLTOSaveTempsPrefix);

    if (!ThinLTOGeneratedObjectsDir.empty()) {
      ThinGenerator.setGeneratedObjectsDirectory(ThinLTOGeneratedObjectsDir);
      ThinGenerator.run();
      return;
    }

    ThinGenerator.run();

    auto &Binaries = ThinGenerator.getProducedBinaries();
    if (Binaries.size() != InputFilenames.size())
      report_fatal_error("Number of output objects does not match the number "
                         "of inputs");

    for (unsigned BufID = 0; BufID < Binaries.size(); ++BufID) {
      auto OutputName = InputFilenames[BufID] + ".thinlto.o";
      std::error_code EC;
      raw_fd_ostream OS(OutputName, EC, sys::fs::OpenFlags::F_None);
      error(EC, "error opening the file '" + OutputName + "'");
      OS << Binaries[BufID]->getBuffer();
    }
  }

  /// Load the combined index from disk, then load every file referenced by
};

} // end namespace thinlto

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);
  cl::ParseCommandLineOptions(argc, argv, "llvm LTO linker\n");

  if (OptLevel < '0' || OptLevel > '3')
    error("optimization level must be between 0 and 3");

  // Initialize the configured targets.
  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmPrinters();
  InitializeAllAsmParsers();

  // set up the TargetOptions for the machine
  TargetOptions Options = InitTargetOptionsFromCodeGenFlags();

  if (ListSymbolsOnly) {
    listSymbols(Options);
    return 0;
  }

  if (IndexStats) {
    printIndexStats();
    return 0;
  }

  if (CheckHasObjC) {
    for (auto &Filename : InputFilenames) {
      ExitOnError ExitOnErr(std::string(*argv) + ": error loading file '" +
                            Filename + "': ");
      std::unique_ptr<MemoryBuffer> BufferOrErr =
          ExitOnErr(errorOrToExpected(MemoryBuffer::getFile(Filename)));
      auto Buffer = std::move(BufferOrErr.get());
      if (ExitOnErr(isBitcodeContainingObjCCategory(*Buffer)))
        outs() << "Bitcode " << Filename << " contains ObjC\n";
      else
        outs() << "Bitcode " << Filename << " does not contain ObjC\n";
    }
    return 0;
  }

  if (ThinLTOMode.getNumOccurrences()) {
    if (ThinLTOMode.getNumOccurrences() > 1)
      report_fatal_error("You can't specify more than one -thinlto-action");
    thinlto::ThinLTOProcessing ThinLTOProcessor(Options);
    ThinLTOProcessor.run();
    return 0;
  }

  if (ThinLTO) {
    createCombinedModuleSummaryIndex();
    return 0;
  }

  unsigned BaseArg = 0;

  LLVMContext Context;
  Context.setDiagnosticHandler(llvm::make_unique<LLVMLTODiagnosticHandler>(),
                               true);

  LTOCodeGenerator CodeGen(Context);

  if (UseDiagnosticHandler)
    CodeGen.setDiagnosticHandler(handleDiagnostics, nullptr);

  CodeGen.setCodePICModel(getRelocModel());
  CodeGen.setFreestanding(EnableFreestanding);

  CodeGen.setDebugInfo(LTO_DEBUG_MODEL_DWARF);
  CodeGen.setTargetOptions(Options);
  CodeGen.setShouldRestoreGlobalsLinkage(RestoreGlobalsLinkage);

  StringSet<MallocAllocator> DSOSymbolsSet;
  for (unsigned i = 0; i < DSOSymbols.size(); ++i)
    DSOSymbolsSet.insert(DSOSymbols[i]);

  std::vector<std::string> KeptDSOSyms;

  for (unsigned i = BaseArg; i < InputFilenames.size(); ++i) {
    CurrentActivity = "loading file '" + InputFilenames[i] + "'";
    ErrorOr<std::unique_ptr<LTOModule>> ModuleOrErr =
        LTOModule::createFromFile(Context, InputFilenames[i], Options);
    std::unique_ptr<LTOModule> &Module = *ModuleOrErr;
    CurrentActivity = "";

    unsigned NumSyms = Module->getSymbolCount();
    for (unsigned I = 0; I < NumSyms; ++I) {
      StringRef Name = Module->getSymbolName(I);
      if (!DSOSymbolsSet.count(Name))
        continue;
      lto_symbol_attributes Attrs = Module->getSymbolAttributes(I);
      unsigned Scope = Attrs & LTO_SYMBOL_SCOPE_MASK;
      if (Scope != LTO_SYMBOL_SCOPE_DEFAULT_CAN_BE_HIDDEN)
        KeptDSOSyms.push_back(Name);
    }

    // We use the first input module as the destination module when
    // SetMergedModule is true.
    if (SetMergedModule && i == BaseArg) {
      // Transfer ownership to the code generator.
      CodeGen.setModule(std::move(Module));
    } else if (!CodeGen.addModule(Module.get())) {
      // Print a message here so that we know addModule() did not abort.
      error("error adding file '" + InputFilenames[i] + "'");
    }
  }

  // Add all the exported symbols to the table of symbols to preserve.
  for (unsigned i = 0; i < ExportedSymbols.size(); ++i)
    CodeGen.addMustPreserveSymbol(ExportedSymbols[i]);

  // Add all the dso symbols to the table of symbols to expose.
  for (unsigned i = 0; i < KeptDSOSyms.size(); ++i)
    CodeGen.addMustPreserveSymbol(KeptDSOSyms[i]);

  // Set cpu and attrs strings for the default target/subtarget.
  CodeGen.setCpu(MCPU.c_str());

  CodeGen.setOptLevel(OptLevel - '0');

  std::string attrs;
  for (unsigned i = 0; i < MAttrs.size(); ++i) {
    if (i > 0)
      attrs.append(",");
    attrs.append(MAttrs[i]);
  }

  if (!attrs.empty())
    CodeGen.setAttr(attrs);

  if (FileType.getNumOccurrences())
    CodeGen.setFileType(FileType);

  if (!OutputFilename.empty()) {
    if (!CodeGen.optimize(DisableVerify, DisableInline, DisableGVNLoadPRE,
                          DisableLTOVectorization)) {
      // Diagnostic messages should have been printed by the handler.
      error("error optimizing the code");
    }

    if (SaveModuleFile) {
      std::string ModuleFilename = OutputFilename;
      ModuleFilename += ".merged.bc";
      std::string ErrMsg;

      if (!CodeGen.writeMergedModules(ModuleFilename))
        error("writing merged module failed.");
    }

    std::list<ToolOutputFile> OSs;
    std::vector<raw_pwrite_stream *> OSPtrs;
    for (unsigned I = 0; I != Parallelism; ++I) {
      std::string PartFilename = OutputFilename;
      if (Parallelism != 1)
        PartFilename += "." + utostr(I);
      std::error_code EC;
      OSs.emplace_back(PartFilename, EC, sys::fs::F_None);
      if (EC)
        error("error opening the file '" + PartFilename + "': " + EC.message());
      OSPtrs.push_back(&OSs.back().os());
    }

    if (!CodeGen.compileOptimized(OSPtrs))
      // Diagnostic messages should have been printed by the handler.
      error("error compiling the code");

    for (ToolOutputFile &OS : OSs)
      OS.keep();
  } else {
    if (Parallelism != 1)
      error("-j must be specified together with -o");

    if (SaveModuleFile)
      error(": -save-merged-module must be specified with -o");

    const char *OutputName = nullptr;
    if (!CodeGen.compile_to_file(&OutputName, DisableVerify, DisableInline,
                                 DisableGVNLoadPRE, DisableLTOVectorization))
      error("error compiling the code");
      // Diagnostic messages should have been printed by the handler.

    outs() << "Wrote native object file '" << OutputName << "'\n";
  }

  return 0;
}
