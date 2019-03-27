//===- lld/ReaderWriter/MachOLinkingContext.h -----------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_READER_WRITER_MACHO_LINKING_CONTEXT_H
#define LLD_READER_WRITER_MACHO_LINKING_CONTEXT_H

#include "lld/Core/LinkingContext.h"
#include "lld/Core/Reader.h"
#include "lld/Core/Writer.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Support/ErrorHandling.h"
#include <set>

using llvm::MachO::HeaderFileType;

namespace lld {

namespace mach_o {
class ArchHandler;
class MachODylibFile;
class MachOFile;
class SectCreateFile;
}

class MachOLinkingContext : public LinkingContext {
public:
  MachOLinkingContext();
  ~MachOLinkingContext() override;

  enum Arch {
    arch_unknown,
    arch_ppc,
    arch_x86,
    arch_x86_64,
    arch_armv6,
    arch_armv7,
    arch_armv7s,
    arch_arm64,
  };

  enum class OS {
    unknown,
    macOSX,
    iOS,
    iOS_simulator
  };

  enum class ExportMode {
    globals,    // Default, all global symbols exported.
    whiteList,  // -exported_symbol[s_list], only listed symbols exported.
    blackList   // -unexported_symbol[s_list], no listed symbol exported.
  };

  enum class DebugInfoMode {
    addDebugMap,    // Default
    noDebugMap      // -S option
  };

  enum class UndefinedMode {
    error,
    warning,
    suppress,
    dynamicLookup
  };

  enum ObjCConstraint {
    objc_unknown = 0,
    objc_supports_gc = 2,
    objc_gc_only = 4,
    // Image optimized by dyld = 8
    // GC compaction = 16
    objc_retainReleaseForSimulator = 32,
    objc_retainRelease
  };

  /// Initializes the context to sane default values given the specified output
  /// file type, arch, os, and minimum os version.  This should be called before
  /// other setXXX() methods.
  void configure(HeaderFileType type, Arch arch, OS os, uint32_t minOSVersion,
                 bool exportDynamicSymbols);

  void addPasses(PassManager &pm) override;
  bool validateImpl() override;
  std::string demangle(StringRef symbolName) const override;

  void createImplicitFiles(std::vector<std::unique_ptr<File>> &) override;

  /// Creates a new file which is owned by the context.  Returns a pointer to
  /// the new file.
  template <class T, class... Args>
  typename std::enable_if<!std::is_array<T>::value, T *>::type
  make_file(Args &&... args) const {
    auto file = std::unique_ptr<T>(new T(std::forward<Args>(args)...));
    auto *filePtr = file.get();
    auto *ctx = const_cast<MachOLinkingContext *>(this);
    ctx->getNodes().push_back(llvm::make_unique<FileNode>(std::move(file)));
    return filePtr;
  }

  uint32_t getCPUType() const;
  uint32_t getCPUSubType() const;

  bool addEntryPointLoadCommand() const;
  bool addUnixThreadLoadCommand() const;
  bool outputTypeHasEntry() const;
  bool is64Bit() const;

  virtual uint64_t pageZeroSize() const { return _pageZeroSize; }
  virtual uint64_t pageSize() const { return _pageSize; }

  mach_o::ArchHandler &archHandler() const;

  HeaderFileType outputMachOType() const { return _outputMachOType; }

  Arch arch() const { return _arch; }
  StringRef archName() const { return nameFromArch(_arch); }
  OS os() const { return _os; }

  ExportMode exportMode() const { return _exportMode; }
  void setExportMode(ExportMode mode) { _exportMode = mode; }
  void addExportSymbol(StringRef sym);
  bool exportRestrictMode() const { return _exportMode != ExportMode::globals; }
  bool exportSymbolNamed(StringRef sym) const;

  DebugInfoMode debugInfoMode() const { return _debugInfoMode; }
  void setDebugInfoMode(DebugInfoMode mode) {
    _debugInfoMode = mode;
  }

  void appendOrderedSymbol(StringRef symbol, StringRef filename);

  bool keepPrivateExterns() const { return _keepPrivateExterns; }
  void setKeepPrivateExterns(bool v) { _keepPrivateExterns = v; }
  bool demangleSymbols() const { return _demangle; }
  void setDemangleSymbols(bool d) { _demangle = d; }
  bool mergeObjCCategories() const { return _mergeObjCCategories; }
  void setMergeObjCCategories(bool v) { _mergeObjCCategories = v; }
  /// Create file at specified path which will contain a binary encoding
  /// of all input and output file paths.
  std::error_code createDependencyFile(StringRef path);
  void addInputFileDependency(StringRef path) const;
  void addInputFileNotFound(StringRef path) const;
  void addOutputFileDependency(StringRef path) const;

  bool minOS(StringRef mac, StringRef iOS) const;
  void setDoNothing(bool value) { _doNothing = value; }
  bool doNothing() const { return _doNothing; }
  bool printAtoms() const { return _printAtoms; }
  bool testingFileUsage() const { return _testingFileUsage; }
  const StringRefVector &searchDirs() const { return _searchDirs; }
  const StringRefVector &frameworkDirs() const { return _frameworkDirs; }
  void setSysLibRoots(const StringRefVector &paths);
  const StringRefVector &sysLibRoots() const { return _syslibRoots; }
  bool PIE() const { return _pie; }
  void setPIE(bool pie) { _pie = pie; }
  bool generateVersionLoadCommand() const {
    return _generateVersionLoadCommand;
  }
  void setGenerateVersionLoadCommand(bool v) {
    _generateVersionLoadCommand = v;
  }

  bool generateFunctionStartsLoadCommand() const {
    return _generateFunctionStartsLoadCommand;
  }
  void setGenerateFunctionStartsLoadCommand(bool v) {
    _generateFunctionStartsLoadCommand = v;
  }

  bool generateDataInCodeLoadCommand() const {
    return _generateDataInCodeLoadCommand;
  }
  void setGenerateDataInCodeLoadCommand(bool v) {
    _generateDataInCodeLoadCommand = v;
  }

  uint64_t stackSize() const { return _stackSize; }
  void setStackSize(uint64_t stackSize) { _stackSize = stackSize; }

  uint64_t baseAddress() const { return _baseAddress; }
  void setBaseAddress(uint64_t baseAddress) { _baseAddress = baseAddress; }

  ObjCConstraint objcConstraint() const { return _objcConstraint; }

  uint32_t osMinVersion() const { return _osMinVersion; }

  uint32_t sdkVersion() const { return _sdkVersion; }
  void setSdkVersion(uint64_t v) { _sdkVersion = v; }

  uint64_t sourceVersion() const { return _sourceVersion; }
  void setSourceVersion(uint64_t v) { _sourceVersion = v; }

  uint32_t swiftVersion() const { return _swiftVersion; }

  /// Checks whether a given path on the filesystem exists.
  ///
  /// When running in -test_file_usage mode, this method consults an
  /// internally maintained list of files that exist (provided by -path_exists)
  /// instead of the actual filesystem.
  bool pathExists(StringRef path) const;

  /// Like pathExists() but only used on files - not directories.
  bool fileExists(StringRef path) const;

  /// Adds any library search paths derived from the given base, possibly
  /// modified by -syslibroots.
  ///
  /// The set of paths added consists of approximately all syslibroot-prepended
  /// versions of libPath that exist, or the original libPath if there are none
  /// for whatever reason. With various edge-cases for compatibility.
  void addModifiedSearchDir(StringRef libPath, bool isSystemPath = false);

  /// Determine whether -lFoo can be resolve within the given path, and
  /// return the filename if so.
  ///
  /// The -lFoo option is documented to search for libFoo.dylib and libFoo.a in
  /// that order, unless Foo ends in ".o", in which case only the exact file
  /// matches (e.g. -lfoo.o would only find foo.o).
  llvm::Optional<StringRef> searchDirForLibrary(StringRef path,
                                                StringRef libName) const;

  /// Iterates through all search path entries looking for libName (as
  /// specified by -lFoo).
  llvm::Optional<StringRef> searchLibrary(StringRef libName) const;

  /// Add a framework search path.  Internally, this method may be prepended
  /// the path with syslibroot.
  void addFrameworkSearchDir(StringRef fwPath, bool isSystemPath = false);

  /// Iterates through all framework directories looking for
  /// Foo.framework/Foo (when fwName = "Foo").
  llvm::Optional<StringRef> findPathForFramework(StringRef fwName) const;

  /// The dylib's binary compatibility version, in the raw uint32 format.
  ///
  /// When building a dynamic library, this is the compatibility version that
  /// gets embedded into the result. Other Mach-O binaries that link against
  /// this library will store the compatibility version in its load command. At
  /// runtime, the loader will verify that the binary is compatible with the
  /// installed dynamic library.
  uint32_t compatibilityVersion() const { return _compatibilityVersion; }

  /// The dylib's current version, in the the raw uint32 format.
  ///
  /// When building a dynamic library, this is the current version that gets
  /// embedded into the result. Other Mach-O binaries that link against
  /// this library will store the compatibility version in its load command.
  uint32_t currentVersion() const { return _currentVersion; }

  /// The dylib's install name.
  ///
  /// Binaries that link against the dylib will embed this path into the dylib
  /// load command. When loading the binaries at runtime, this is the location
  /// on disk that the loader will look for the dylib.
  StringRef installName() const { return _installName; }

  /// Whether or not the dylib has side effects during initialization.
  ///
  /// Dylibs marked as being dead strippable provide the guarantee that loading
  /// the dylib has no side effects, allowing the linker to strip out the dylib
  /// when linking a binary that does not use any of its symbols.
  bool deadStrippableDylib() const { return _deadStrippableDylib; }

  /// Whether or not to use flat namespace.
  ///
  /// MachO usually uses a two-level namespace, where each external symbol
  /// referenced by the target is associated with the dylib that will provide
  /// the symbol's definition at runtime. Using flat namespace overrides this
  /// behavior: the linker searches all dylibs on the command line and all
  /// dylibs those original dylibs depend on, but does not record which dylib
  /// an external symbol came from. At runtime dyld again searches all images
  /// and uses the first definition it finds. In addition, any undefines in
  /// loaded flat_namespace dylibs must be resolvable at build time.
  bool useFlatNamespace() const { return _flatNamespace; }

  /// How to handle undefined symbols.
  ///
  /// Options are:
  ///  * error: Report an error and terminate linking.
  ///  * warning: Report a warning, but continue linking.
  ///  * suppress: Ignore and continue linking.
  ///  * dynamic_lookup: For use with -twolevel namespace: Records source dylibs
  ///    for symbols that are defined in a linked dylib at static link time.
  ///    Undefined symbols are handled by searching all loaded images at
  ///    runtime.
  UndefinedMode undefinedMode() const { return _undefinedMode; }

  /// The path to the executable that will load the bundle at runtime.
  ///
  /// When building a Mach-O bundle, this executable will be examined if there
  /// are undefined symbols after the main link phase. It is expected that this
  /// binary will be loading the bundle at runtime and will provide the symbols
  /// at that point.
  StringRef bundleLoader() const { return _bundleLoader; }

  void setCompatibilityVersion(uint32_t vers) { _compatibilityVersion = vers; }
  void setCurrentVersion(uint32_t vers) { _currentVersion = vers; }
  void setInstallName(StringRef name) { _installName = name; }
  void setDeadStrippableDylib(bool deadStrippable) {
    _deadStrippableDylib = deadStrippable;
  }
  void setUseFlatNamespace(bool flatNamespace) {
    _flatNamespace = flatNamespace;
  }

  void setUndefinedMode(UndefinedMode undefinedMode) {
    _undefinedMode = undefinedMode;
  }

  void setBundleLoader(StringRef loader) { _bundleLoader = loader; }
  void setPrintAtoms(bool value=true) { _printAtoms = value; }
  void setTestingFileUsage(bool value = true) {
    _testingFileUsage = value;
  }
  void addExistingPathForDebug(StringRef path) {
    _existingPaths.insert(path);
  }

  void addRpath(StringRef rpath);
  const StringRefVector &rpaths() const { return _rpaths; }

  /// Add section alignment constraint on final layout.
  void addSectionAlignment(StringRef seg, StringRef sect, uint16_t align);

  /// Add a section based on a command-line sectcreate option.
  void addSectCreateSection(StringRef seg, StringRef sect,
                            std::unique_ptr<MemoryBuffer> content);

  /// Returns true if specified section had alignment constraints.
  bool sectionAligned(StringRef seg, StringRef sect, uint16_t &align) const;

  StringRef dyldPath() const { return "/usr/lib/dyld"; }

  /// Stub creation Pass should be run.
  bool needsStubsPass() const;

  // GOT creation Pass should be run.
  bool needsGOTPass() const;

  /// Pass to add TLV sections.
  bool needsTLVPass() const;

  /// Pass to transform __compact_unwind into __unwind_info should be run.
  bool needsCompactUnwindPass() const;

  /// Pass to add shims switching between thumb and arm mode.
  bool needsShimPass() const;

  /// Pass to add objc image info and optimized objc data.
  bool needsObjCPass() const;

  /// Magic symbol name stubs will need to help lazy bind.
  StringRef binderSymbolName() const;

  /// Used to keep track of direct and indirect dylibs.
  void registerDylib(mach_o::MachODylibFile *dylib, bool upward) const;

  // Reads a file from disk to memory. Returns only a needed chunk
  // if a fat binary.
  ErrorOr<std::unique_ptr<MemoryBuffer>> getMemoryBuffer(StringRef path);

  /// Used to find indirect dylibs. Instantiates a MachODylibFile if one
  /// has not already been made for the requested dylib.  Uses -L and -F
  /// search paths to allow indirect dylibs to be overridden.
  mach_o::MachODylibFile* findIndirectDylib(StringRef path);

  uint32_t dylibCurrentVersion(StringRef installName) const;

  uint32_t dylibCompatVersion(StringRef installName) const;

  ArrayRef<mach_o::MachODylibFile*> allDylibs() const {
    return _allDylibs;
  }

  /// Creates a copy (owned by this MachOLinkingContext) of a string.
  StringRef copy(StringRef str) { return str.copy(_allocator); }

  /// If the memoryBuffer is a fat file with a slice for the current arch,
  /// this method will return the offset and size of that slice.
  bool sliceFromFatFile(MemoryBufferRef mb, uint32_t &offset, uint32_t &size);

  /// Returns if a command line option specified dylib is an upward link.
  bool isUpwardDylib(StringRef installName) const;

  static bool isThinObjectFile(StringRef path, Arch &arch);
  static Arch archFromCpuType(uint32_t cputype, uint32_t cpusubtype);
  static Arch archFromName(StringRef archName);
  static StringRef nameFromArch(Arch arch);
  static uint32_t cpuTypeFromArch(Arch arch);
  static uint32_t cpuSubtypeFromArch(Arch arch);
  static bool is64Bit(Arch arch);
  static bool isHostEndian(Arch arch);
  static bool isBigEndian(Arch arch);

  /// Construct 32-bit value from string "X.Y.Z" where
  /// bits are xxxx.yy.zz.  Largest number is 65535.255.255
  static bool parsePackedVersion(StringRef str, uint32_t &result);

  /// Construct 64-bit value from string "A.B.C.D.E" where
  /// bits are aaaa.bb.cc.dd.ee.  Largest number is 16777215.1023.1023.1023.1023
  static bool parsePackedVersion(StringRef str, uint64_t &result);

  void finalizeInputFiles() override;

  llvm::Error handleLoadedFile(File &file) override;

  bool customAtomOrderer(const DefinedAtom *left, const DefinedAtom *right,
                         bool &leftBeforeRight) const;

  /// Return the 'flat namespace' file. This is the file that supplies
  /// atoms for otherwise undefined symbols when the -flat_namespace or
  /// -undefined dynamic_lookup options are used.
  File* flatNamespaceFile() const { return _flatNamespaceFile; }

private:
  Writer &writer() const override;
  mach_o::MachODylibFile* loadIndirectDylib(StringRef path);
  void checkExportWhiteList(const DefinedAtom *atom) const;
  void checkExportBlackList(const DefinedAtom *atom) const;
  struct ArchInfo {
    StringRef                 archName;
    MachOLinkingContext::Arch arch;
    bool                      littleEndian;
    uint32_t                  cputype;
    uint32_t                  cpusubtype;
  };

  struct SectionAlign {
    StringRef segmentName;
    StringRef sectionName;
    uint16_t  align;
  };

  struct OrderFileNode {
    StringRef fileFilter;
    unsigned  order;
  };

  static bool findOrderOrdinal(const std::vector<OrderFileNode> &nodes,
                             const DefinedAtom *atom, unsigned &ordinal);

  static ArchInfo _s_archInfos[];

  std::set<StringRef> _existingPaths; // For testing only.
  StringRefVector _searchDirs;
  StringRefVector _syslibRoots;
  StringRefVector _frameworkDirs;
  HeaderFileType _outputMachOType = llvm::MachO::MH_EXECUTE;
  bool _outputMachOTypeStatic = false; // Disambiguate static vs dynamic prog
  bool _doNothing = false;             // for -help and -v which just print info
  bool _pie = false;
  Arch _arch = arch_unknown;
  OS _os = OS::macOSX;
  uint32_t _osMinVersion = 0;
  uint32_t _sdkVersion = 0;
  uint64_t _sourceVersion = 0;
  uint64_t _pageZeroSize = 0;
  uint64_t _pageSize = 4096;
  uint64_t _baseAddress = 0;
  uint64_t _stackSize = 0;
  uint32_t _compatibilityVersion = 0;
  uint32_t _currentVersion = 0;
  ObjCConstraint _objcConstraint = objc_unknown;
  uint32_t _swiftVersion = 0;
  StringRef _installName;
  StringRefVector _rpaths;
  bool _flatNamespace = false;
  UndefinedMode _undefinedMode = UndefinedMode::error;
  bool _deadStrippableDylib = false;
  bool _printAtoms = false;
  bool _testingFileUsage = false;
  bool _keepPrivateExterns = false;
  bool _demangle = false;
  bool _mergeObjCCategories = true;
  bool _generateVersionLoadCommand = false;
  bool _generateFunctionStartsLoadCommand = false;
  bool _generateDataInCodeLoadCommand = false;
  StringRef _bundleLoader;
  mutable std::unique_ptr<mach_o::ArchHandler> _archHandler;
  mutable std::unique_ptr<Writer> _writer;
  std::vector<SectionAlign> _sectAligns;
  mutable llvm::StringMap<mach_o::MachODylibFile*> _pathToDylibMap;
  mutable std::vector<mach_o::MachODylibFile*> _allDylibs;
  mutable std::set<mach_o::MachODylibFile*> _upwardDylibs;
  mutable std::vector<std::unique_ptr<File>> _indirectDylibs;
  mutable std::mutex _dylibsMutex;
  ExportMode _exportMode = ExportMode::globals;
  llvm::StringSet<> _exportedSymbols;
  DebugInfoMode _debugInfoMode = DebugInfoMode::addDebugMap;
  std::unique_ptr<llvm::raw_fd_ostream> _dependencyInfo;
  llvm::StringMap<std::vector<OrderFileNode>> _orderFiles;
  unsigned _orderFileEntries = 0;
  File *_flatNamespaceFile = nullptr;
  mach_o::SectCreateFile *_sectCreateFile = nullptr;
};

} // end namespace lld

#endif // LLD_READER_WRITER_MACHO_LINKING_CONTEXT_H
