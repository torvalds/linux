//===-LTO.h - LLVM Link Time Optimizer ------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares functions and classes used to support LTO. It is intended
// to be used both by LTO classes as well as by clients (gold-plugin) that
// don't utilize the LTO code generator interfaces.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LTO_LTO_H
#define LLVM_LTO_LTO_H

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/LTO/Config.h"
#include "llvm/Linker/IRMover.h"
#include "llvm/Object/IRSymtab.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/thread.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Transforms/IPO/FunctionImport.h"

namespace llvm {

class BitcodeModule;
class Error;
class LLVMContext;
class MemoryBufferRef;
class Module;
class Target;
class raw_pwrite_stream;

/// Resolve linkage for prevailing symbols in the \p Index. Linkage changes
/// recorded in the index and the ThinLTO backends must apply the changes to
/// the module via thinLTOResolvePrevailingInModule.
///
/// This is done for correctness (if value exported, ensure we always
/// emit a copy), and compile-time optimization (allow drop of duplicates).
void thinLTOResolvePrevailingInIndex(
    ModuleSummaryIndex &Index,
    function_ref<bool(GlobalValue::GUID, const GlobalValueSummary *)>
        isPrevailing,
    function_ref<void(StringRef, GlobalValue::GUID, GlobalValue::LinkageTypes)>
        recordNewLinkage);

/// Update the linkages in the given \p Index to mark exported values
/// as external and non-exported values as internal. The ThinLTO backends
/// must apply the changes to the Module via thinLTOInternalizeModule.
void thinLTOInternalizeAndPromoteInIndex(
    ModuleSummaryIndex &Index,
    function_ref<bool(StringRef, GlobalValue::GUID)> isExported);

/// Computes a unique hash for the Module considering the current list of
/// export/import and other global analysis results.
/// The hash is produced in \p Key.
void computeLTOCacheKey(
    SmallString<40> &Key, const lto::Config &Conf,
    const ModuleSummaryIndex &Index, StringRef ModuleID,
    const FunctionImporter::ImportMapTy &ImportList,
    const FunctionImporter::ExportSetTy &ExportList,
    const std::map<GlobalValue::GUID, GlobalValue::LinkageTypes> &ResolvedODR,
    const GVSummaryMapTy &DefinedGlobals,
    const std::set<GlobalValue::GUID> &CfiFunctionDefs = {},
    const std::set<GlobalValue::GUID> &CfiFunctionDecls = {});

namespace lto {

/// Given the original \p Path to an output file, replace any path
/// prefix matching \p OldPrefix with \p NewPrefix. Also, create the
/// resulting directory if it does not yet exist.
std::string getThinLTOOutputFile(const std::string &Path,
                                 const std::string &OldPrefix,
                                 const std::string &NewPrefix);

/// Setup optimization remarks.
Expected<std::unique_ptr<ToolOutputFile>>
setupOptimizationRemarks(LLVMContext &Context, StringRef LTORemarksFilename,
                         bool LTOPassRemarksWithHotness, int Count = -1);

class LTO;
struct SymbolResolution;
class ThinBackendProc;

/// An input file. This is a symbol table wrapper that only exposes the
/// information that an LTO client should need in order to do symbol resolution.
class InputFile {
public:
  class Symbol;

private:
  // FIXME: Remove LTO class friendship once we have bitcode symbol tables.
  friend LTO;
  InputFile() = default;

  std::vector<BitcodeModule> Mods;
  SmallVector<char, 0> Strtab;
  std::vector<Symbol> Symbols;

  // [begin, end) for each module
  std::vector<std::pair<size_t, size_t>> ModuleSymIndices;

  StringRef TargetTriple, SourceFileName, COFFLinkerOpts;
  std::vector<StringRef> ComdatTable;

public:
  ~InputFile();

  /// Create an InputFile.
  static Expected<std::unique_ptr<InputFile>> create(MemoryBufferRef Object);

  /// The purpose of this class is to only expose the symbol information that an
  /// LTO client should need in order to do symbol resolution.
  class Symbol : irsymtab::Symbol {
    friend LTO;

  public:
    Symbol(const irsymtab::Symbol &S) : irsymtab::Symbol(S) {}

    using irsymtab::Symbol::isUndefined;
    using irsymtab::Symbol::isCommon;
    using irsymtab::Symbol::isWeak;
    using irsymtab::Symbol::isIndirect;
    using irsymtab::Symbol::getName;
    using irsymtab::Symbol::getVisibility;
    using irsymtab::Symbol::canBeOmittedFromSymbolTable;
    using irsymtab::Symbol::isTLS;
    using irsymtab::Symbol::getComdatIndex;
    using irsymtab::Symbol::getCommonSize;
    using irsymtab::Symbol::getCommonAlignment;
    using irsymtab::Symbol::getCOFFWeakExternalFallback;
    using irsymtab::Symbol::getSectionName;
    using irsymtab::Symbol::isExecutable;
  };

  /// A range over the symbols in this InputFile.
  ArrayRef<Symbol> symbols() const { return Symbols; }

  /// Returns linker options specified in the input file.
  StringRef getCOFFLinkerOpts() const { return COFFLinkerOpts; }

  /// Returns the path to the InputFile.
  StringRef getName() const;

  /// Returns the input file's target triple.
  StringRef getTargetTriple() const { return TargetTriple; }

  /// Returns the source file path specified at compile time.
  StringRef getSourceFileName() const { return SourceFileName; }

  // Returns a table with all the comdats used by this file.
  ArrayRef<StringRef> getComdatTable() const { return ComdatTable; }

private:
  ArrayRef<Symbol> module_symbols(unsigned I) const {
    const auto &Indices = ModuleSymIndices[I];
    return {Symbols.data() + Indices.first, Symbols.data() + Indices.second};
  }
};

/// This class wraps an output stream for a native object. Most clients should
/// just be able to return an instance of this base class from the stream
/// callback, but if a client needs to perform some action after the stream is
/// written to, that can be done by deriving from this class and overriding the
/// destructor.
class NativeObjectStream {
public:
  NativeObjectStream(std::unique_ptr<raw_pwrite_stream> OS) : OS(std::move(OS)) {}
  std::unique_ptr<raw_pwrite_stream> OS;
  virtual ~NativeObjectStream() = default;
};

/// This type defines the callback to add a native object that is generated on
/// the fly.
///
/// Stream callbacks must be thread safe.
typedef std::function<std::unique_ptr<NativeObjectStream>(unsigned Task)>
    AddStreamFn;

/// This is the type of a native object cache. To request an item from the
/// cache, pass a unique string as the Key. For hits, the cached file will be
/// added to the link and this function will return AddStreamFn(). For misses,
/// the cache will return a stream callback which must be called at most once to
/// produce content for the stream. The native object stream produced by the
/// stream callback will add the file to the link after the stream is written
/// to.
///
/// Clients generally look like this:
///
/// if (AddStreamFn AddStream = Cache(Task, Key))
///   ProduceContent(AddStream);
typedef std::function<AddStreamFn(unsigned Task, StringRef Key)>
    NativeObjectCache;

/// A ThinBackend defines what happens after the thin-link phase during ThinLTO.
/// The details of this type definition aren't important; clients can only
/// create a ThinBackend using one of the create*ThinBackend() functions below.
typedef std::function<std::unique_ptr<ThinBackendProc>(
    Config &C, ModuleSummaryIndex &CombinedIndex,
    StringMap<GVSummaryMapTy> &ModuleToDefinedGVSummaries,
    AddStreamFn AddStream, NativeObjectCache Cache)>
    ThinBackend;

/// This ThinBackend runs the individual backend jobs in-process.
ThinBackend createInProcessThinBackend(unsigned ParallelismLevel);

/// This ThinBackend writes individual module indexes to files, instead of
/// running the individual backend jobs. This backend is for distributed builds
/// where separate processes will invoke the real backends.
///
/// To find the path to write the index to, the backend checks if the path has a
/// prefix of OldPrefix; if so, it replaces that prefix with NewPrefix. It then
/// appends ".thinlto.bc" and writes the index to that path. If
/// ShouldEmitImportsFiles is true it also writes a list of imported files to a
/// similar path with ".imports" appended instead.
/// LinkedObjectsFile is an output stream to write the list of object files for
/// the final ThinLTO linking. Can be nullptr.
/// OnWrite is callback which receives module identifier and notifies LTO user
/// that index file for the module (and optionally imports file) was created.
using IndexWriteCallback = std::function<void(const std::string &)>;
ThinBackend createWriteIndexesThinBackend(std::string OldPrefix,
                                          std::string NewPrefix,
                                          bool ShouldEmitImportsFiles,
                                          raw_fd_ostream *LinkedObjectsFile,
                                          IndexWriteCallback OnWrite);

/// This class implements a resolution-based interface to LLVM's LTO
/// functionality. It supports regular LTO, parallel LTO code generation and
/// ThinLTO. You can use it from a linker in the following way:
/// - Set hooks and code generation options (see lto::Config struct defined in
///   Config.h), and use the lto::Config object to create an lto::LTO object.
/// - Create lto::InputFile objects using lto::InputFile::create(), then use
///   the symbols() function to enumerate its symbols and compute a resolution
///   for each symbol (see SymbolResolution below).
/// - After the linker has visited each input file (and each regular object
///   file) and computed a resolution for each symbol, take each lto::InputFile
///   and pass it and an array of symbol resolutions to the add() function.
/// - Call the getMaxTasks() function to get an upper bound on the number of
///   native object files that LTO may add to the link.
/// - Call the run() function. This function will use the supplied AddStream
///   and Cache functions to add up to getMaxTasks() native object files to
///   the link.
class LTO {
  friend InputFile;

public:
  /// Create an LTO object. A default constructed LTO object has a reasonable
  /// production configuration, but you can customize it by passing arguments to
  /// this constructor.
  /// FIXME: We do currently require the DiagHandler field to be set in Conf.
  /// Until that is fixed, a Config argument is required.
  LTO(Config Conf, ThinBackend Backend = nullptr,
      unsigned ParallelCodeGenParallelismLevel = 1);
  ~LTO();

  /// Add an input file to the LTO link, using the provided symbol resolutions.
  /// The symbol resolutions must appear in the enumeration order given by
  /// InputFile::symbols().
  Error add(std::unique_ptr<InputFile> Obj, ArrayRef<SymbolResolution> Res);

  /// Returns an upper bound on the number of tasks that the client may expect.
  /// This may only be called after all IR object files have been added. For a
  /// full description of tasks see LTOBackend.h.
  unsigned getMaxTasks() const;

  /// Runs the LTO pipeline. This function calls the supplied AddStream
  /// function to add native object files to the link.
  ///
  /// The Cache parameter is optional. If supplied, it will be used to cache
  /// native object files and add them to the link.
  ///
  /// The client will receive at most one callback (via either AddStream or
  /// Cache) for each task identifier.
  Error run(AddStreamFn AddStream, NativeObjectCache Cache = nullptr);

private:
  Config Conf;

  struct RegularLTOState {
    RegularLTOState(unsigned ParallelCodeGenParallelismLevel, Config &Conf);
    struct CommonResolution {
      uint64_t Size = 0;
      unsigned Align = 0;
      /// Record if at least one instance of the common was marked as prevailing
      bool Prevailing = false;
    };
    std::map<std::string, CommonResolution> Commons;

    unsigned ParallelCodeGenParallelismLevel;
    LTOLLVMContext Ctx;
    std::unique_ptr<Module> CombinedModule;
    std::unique_ptr<IRMover> Mover;

    // This stores the information about a regular LTO module that we have added
    // to the link. It will either be linked immediately (for modules without
    // summaries) or after summary-based dead stripping (for modules with
    // summaries).
    struct AddedModule {
      std::unique_ptr<Module> M;
      std::vector<GlobalValue *> Keep;
    };
    std::vector<AddedModule> ModsWithSummaries;
  } RegularLTO;

  struct ThinLTOState {
    ThinLTOState(ThinBackend Backend);

    ThinBackend Backend;
    ModuleSummaryIndex CombinedIndex;
    MapVector<StringRef, BitcodeModule> ModuleMap;
    DenseMap<GlobalValue::GUID, StringRef> PrevailingModuleForGUID;
  } ThinLTO;

  // The global resolution for a particular (mangled) symbol name. This is in
  // particular necessary to track whether each symbol can be internalized.
  // Because any input file may introduce a new cross-partition reference, we
  // cannot make any final internalization decisions until all input files have
  // been added and the client has called run(). During run() we apply
  // internalization decisions either directly to the module (for regular LTO)
  // or to the combined index (for ThinLTO).
  struct GlobalResolution {
    /// The unmangled name of the global.
    std::string IRName;

    /// Keep track if the symbol is visible outside of a module with a summary
    /// (i.e. in either a regular object or a regular LTO module without a
    /// summary).
    bool VisibleOutsideSummary = false;

    bool UnnamedAddr = true;

    /// True if module contains the prevailing definition.
    bool Prevailing = false;

    /// Returns true if module contains the prevailing definition and symbol is
    /// an IR symbol. For example when module-level inline asm block is used,
    /// symbol can be prevailing in module but have no IR name.
    bool isPrevailingIRSymbol() const { return Prevailing && !IRName.empty(); }

    /// This field keeps track of the partition number of this global. The
    /// regular LTO object is partition 0, while each ThinLTO object has its own
    /// partition number from 1 onwards.
    ///
    /// Any global that is defined or used by more than one partition, or that
    /// is referenced externally, may not be internalized.
    ///
    /// Partitions generally have a one-to-one correspondence with tasks, except
    /// that we use partition 0 for all parallel LTO code generation partitions.
    /// Any partitioning of the combined LTO object is done internally by the
    /// LTO backend.
    unsigned Partition = Unknown;

    /// Special partition numbers.
    enum : unsigned {
      /// A partition number has not yet been assigned to this global.
      Unknown = -1u,

      /// This global is either used by more than one partition or has an
      /// external reference, and therefore cannot be internalized.
      External = -2u,

      /// The RegularLTO partition
      RegularLTO = 0,
    };
  };

  // Global mapping from mangled symbol names to resolutions.
  StringMap<GlobalResolution> GlobalResolutions;

  void addModuleToGlobalRes(ArrayRef<InputFile::Symbol> Syms,
                            ArrayRef<SymbolResolution> Res, unsigned Partition,
                            bool InSummary);

  // These functions take a range of symbol resolutions [ResI, ResE) and consume
  // the resolutions used by a single input module by incrementing ResI. After
  // these functions return, [ResI, ResE) will refer to the resolution range for
  // the remaining modules in the InputFile.
  Error addModule(InputFile &Input, unsigned ModI,
                  const SymbolResolution *&ResI, const SymbolResolution *ResE);

  Expected<RegularLTOState::AddedModule>
  addRegularLTO(BitcodeModule BM, ArrayRef<InputFile::Symbol> Syms,
                const SymbolResolution *&ResI, const SymbolResolution *ResE);
  Error linkRegularLTO(RegularLTOState::AddedModule Mod,
                       bool LivenessFromIndex);

  Error addThinLTO(BitcodeModule BM, ArrayRef<InputFile::Symbol> Syms,
                   const SymbolResolution *&ResI, const SymbolResolution *ResE);

  Error runRegularLTO(AddStreamFn AddStream);
  Error runThinLTO(AddStreamFn AddStream, NativeObjectCache Cache);

  mutable bool CalledGetMaxTasks = false;

  // Use Optional to distinguish false from not yet initialized.
  Optional<bool> EnableSplitLTOUnit;
};

/// The resolution for a symbol. The linker must provide a SymbolResolution for
/// each global symbol based on its internal resolution of that symbol.
struct SymbolResolution {
  SymbolResolution()
      : Prevailing(0), FinalDefinitionInLinkageUnit(0), VisibleToRegularObj(0),
        LinkerRedefined(0) {}

  /// The linker has chosen this definition of the symbol.
  unsigned Prevailing : 1;

  /// The definition of this symbol is unpreemptable at runtime and is known to
  /// be in this linkage unit.
  unsigned FinalDefinitionInLinkageUnit : 1;

  /// The definition of this symbol is visible outside of the LTO unit.
  unsigned VisibleToRegularObj : 1;

  /// Linker redefined version of the symbol which appeared in -wrap or -defsym
  /// linker option.
  unsigned LinkerRedefined : 1;
};

} // namespace lto
} // namespace llvm

#endif
