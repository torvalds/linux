//===- ASTUnit.h - ASTUnit utility ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// ASTUnit utility class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_ASTUNIT_H
#define LLVM_CLANG_FRONTEND_ASTUNIT_H

#include "clang-c/Index.h"
#include "clang/AST/ASTContext.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/FileSystemOptions.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetOptions.h"
#include "clang/Lex/HeaderSearchOptions.h"
#include "clang/Lex/ModuleLoader.h"
#include "clang/Lex/PreprocessingRecord.h"
#include "clang/Sema/CodeCompleteConsumer.h"
#include "clang/Serialization/ASTBitCodes.h"
#include "clang/Frontend/PrecompiledPreamble.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

class MemoryBuffer;

namespace vfs {

class FileSystem;

} // namespace vfs
} // namespace llvm

namespace clang {

class ASTContext;
class ASTDeserializationListener;
class ASTMutationListener;
class ASTReader;
class CompilerInstance;
class CompilerInvocation;
class Decl;
class FileEntry;
class FileManager;
class FrontendAction;
class HeaderSearch;
class InputKind;
class MemoryBufferCache;
class PCHContainerOperations;
class PCHContainerReader;
class Preprocessor;
class PreprocessorOptions;
class Sema;
class TargetInfo;

/// \brief Enumerates the available scopes for skipping function bodies.
enum class SkipFunctionBodiesScope { None, Preamble, PreambleAndMainFile };

/// Utility class for loading a ASTContext from an AST file.
class ASTUnit {
public:
  struct StandaloneFixIt {
    std::pair<unsigned, unsigned> RemoveRange;
    std::pair<unsigned, unsigned> InsertFromRange;
    std::string CodeToInsert;
    bool BeforePreviousInsertions;
  };

  struct StandaloneDiagnostic {
    unsigned ID;
    DiagnosticsEngine::Level Level;
    std::string Message;
    std::string Filename;
    unsigned LocOffset;
    std::vector<std::pair<unsigned, unsigned>> Ranges;
    std::vector<StandaloneFixIt> FixIts;
  };

private:
  std::shared_ptr<LangOptions>            LangOpts;
  IntrusiveRefCntPtr<DiagnosticsEngine>   Diagnostics;
  IntrusiveRefCntPtr<FileManager>         FileMgr;
  IntrusiveRefCntPtr<SourceManager>       SourceMgr;
  IntrusiveRefCntPtr<MemoryBufferCache>   PCMCache;
  std::unique_ptr<HeaderSearch>           HeaderInfo;
  IntrusiveRefCntPtr<TargetInfo>          Target;
  std::shared_ptr<Preprocessor>           PP;
  IntrusiveRefCntPtr<ASTContext>          Ctx;
  std::shared_ptr<TargetOptions>          TargetOpts;
  std::shared_ptr<HeaderSearchOptions>    HSOpts;
  std::shared_ptr<PreprocessorOptions>    PPOpts;
  IntrusiveRefCntPtr<ASTReader> Reader;
  bool HadModuleLoaderFatalFailure = false;

  struct ASTWriterData;
  std::unique_ptr<ASTWriterData> WriterData;

  FileSystemOptions FileSystemOpts;

  /// The AST consumer that received information about the translation
  /// unit as it was parsed or loaded.
  std::unique_ptr<ASTConsumer> Consumer;

  /// The semantic analysis object used to type-check the translation
  /// unit.
  std::unique_ptr<Sema> TheSema;

  /// Optional owned invocation, just used to make the invocation used in
  /// LoadFromCommandLine available.
  std::shared_ptr<CompilerInvocation> Invocation;

  /// Fake module loader: the AST unit doesn't need to load any modules.
  TrivialModuleLoader ModuleLoader;

  // OnlyLocalDecls - when true, walking this AST should only visit declarations
  // that come from the AST itself, not from included precompiled headers.
  // FIXME: This is temporary; eventually, CIndex will always do this.
  bool OnlyLocalDecls = false;

  /// Whether to capture any diagnostics produced.
  bool CaptureDiagnostics = false;

  /// Track whether the main file was loaded from an AST or not.
  bool MainFileIsAST;

  /// What kind of translation unit this AST represents.
  TranslationUnitKind TUKind = TU_Complete;

  /// Whether we should time each operation.
  bool WantTiming;

  /// Whether the ASTUnit should delete the remapped buffers.
  bool OwnsRemappedFileBuffers = true;

  /// Track the top-level decls which appeared in an ASTUnit which was loaded
  /// from a source file.
  //
  // FIXME: This is just an optimization hack to avoid deserializing large parts
  // of a PCH file when using the Index library on an ASTUnit loaded from
  // source. In the long term we should make the Index library use efficient and
  // more scalable search mechanisms.
  std::vector<Decl*> TopLevelDecls;

  /// Sorted (by file offset) vector of pairs of file offset/Decl.
  using LocDeclsTy = SmallVector<std::pair<unsigned, Decl *>, 64>;
  using FileDeclsTy = llvm::DenseMap<FileID, LocDeclsTy *>;

  /// Map from FileID to the file-level declarations that it contains.
  /// The files and decls are only local (and non-preamble) ones.
  FileDeclsTy FileDecls;

  /// The name of the original source file used to generate this ASTUnit.
  std::string OriginalSourceFile;

  /// The set of diagnostics produced when creating the preamble.
  SmallVector<StandaloneDiagnostic, 4> PreambleDiagnostics;

  /// The set of diagnostics produced when creating this
  /// translation unit.
  SmallVector<StoredDiagnostic, 4> StoredDiagnostics;

  /// The set of diagnostics produced when failing to parse, e.g. due
  /// to failure to load the PCH.
  SmallVector<StoredDiagnostic, 4> FailedParseDiagnostics;

  /// The number of stored diagnostics that come from the driver
  /// itself.
  ///
  /// Diagnostics that come from the driver are retained from one parse to
  /// the next.
  unsigned NumStoredDiagnosticsFromDriver = 0;

  /// Counter that determines when we want to try building a
  /// precompiled preamble.
  ///
  /// If zero, we will never build a precompiled preamble. Otherwise,
  /// it's treated as a counter that decrements each time we reparse
  /// without the benefit of a precompiled preamble. When it hits 1,
  /// we'll attempt to rebuild the precompiled header. This way, if
  /// building the precompiled preamble fails, we won't try again for
  /// some number of calls.
  unsigned PreambleRebuildCounter = 0;

  /// Cache pairs "filename - source location"
  ///
  /// Cache contains only source locations from preamble so it is
  /// guaranteed that they stay valid when the SourceManager is recreated.
  /// This cache is used when loading preamble to increase performance
  /// of that loading. It must be cleared when preamble is recreated.
  llvm::StringMap<SourceLocation> PreambleSrcLocCache;

  /// The contents of the preamble.
  llvm::Optional<PrecompiledPreamble> Preamble;

  /// When non-NULL, this is the buffer used to store the contents of
  /// the main file when it has been padded for use with the precompiled
  /// preamble.
  std::unique_ptr<llvm::MemoryBuffer> SavedMainFileBuffer;

  /// The number of warnings that occurred while parsing the preamble.
  ///
  /// This value will be used to restore the state of the \c DiagnosticsEngine
  /// object when re-using the precompiled preamble. Note that only the
  /// number of warnings matters, since we will not save the preamble
  /// when any errors are present.
  unsigned NumWarningsInPreamble = 0;

  /// A list of the serialization ID numbers for each of the top-level
  /// declarations parsed within the precompiled preamble.
  std::vector<serialization::DeclID> TopLevelDeclsInPreamble;

  /// Whether we should be caching code-completion results.
  bool ShouldCacheCodeCompletionResults : 1;

  /// Whether to include brief documentation within the set of code
  /// completions cached.
  bool IncludeBriefCommentsInCodeCompletion : 1;

  /// True if non-system source files should be treated as volatile
  /// (likely to change while trying to use them).
  bool UserFilesAreVolatile : 1;

  static void ConfigureDiags(IntrusiveRefCntPtr<DiagnosticsEngine> Diags,
                             ASTUnit &AST, bool CaptureDiagnostics);

  void TranslateStoredDiagnostics(FileManager &FileMgr,
                                  SourceManager &SrcMan,
                      const SmallVectorImpl<StandaloneDiagnostic> &Diags,
                            SmallVectorImpl<StoredDiagnostic> &Out);

  void clearFileLevelDecls();

public:
  /// A cached code-completion result, which may be introduced in one of
  /// many different contexts.
  struct CachedCodeCompletionResult {
    /// The code-completion string corresponding to this completion
    /// result.
    CodeCompletionString *Completion;

    /// A bitmask that indicates which code-completion contexts should
    /// contain this completion result.
    ///
    /// The bits in the bitmask correspond to the values of
    /// CodeCompleteContext::Kind. To map from a completion context kind to a
    /// bit, shift 1 by that number of bits. Many completions can occur in
    /// several different contexts.
    uint64_t ShowInContexts;

    /// The priority given to this code-completion result.
    unsigned Priority;

    /// The libclang cursor kind corresponding to this code-completion
    /// result.
    CXCursorKind Kind;

    /// The availability of this code-completion result.
    CXAvailabilityKind Availability;

    /// The simplified type class for a non-macro completion result.
    SimplifiedTypeClass TypeClass;

    /// The type of a non-macro completion result, stored as a unique
    /// integer used by the string map of cached completion types.
    ///
    /// This value will be zero if the type is not known, or a unique value
    /// determined by the formatted type string. Se \c CachedCompletionTypes
    /// for more information.
    unsigned Type;
  };

  /// Retrieve the mapping from formatted type names to unique type
  /// identifiers.
  llvm::StringMap<unsigned> &getCachedCompletionTypes() {
    return CachedCompletionTypes;
  }

  /// Retrieve the allocator used to cache global code completions.
  std::shared_ptr<GlobalCodeCompletionAllocator>
  getCachedCompletionAllocator() {
    return CachedCompletionAllocator;
  }

  CodeCompletionTUInfo &getCodeCompletionTUInfo() {
    if (!CCTUInfo)
      CCTUInfo = llvm::make_unique<CodeCompletionTUInfo>(
          std::make_shared<GlobalCodeCompletionAllocator>());
    return *CCTUInfo;
  }

private:
  /// Allocator used to store cached code completions.
  std::shared_ptr<GlobalCodeCompletionAllocator> CachedCompletionAllocator;

  std::unique_ptr<CodeCompletionTUInfo> CCTUInfo;

  /// The set of cached code-completion results.
  std::vector<CachedCodeCompletionResult> CachedCompletionResults;

  /// A mapping from the formatted type name to a unique number for that
  /// type, which is used for type equality comparisons.
  llvm::StringMap<unsigned> CachedCompletionTypes;

  /// A string hash of the top-level declaration and macro definition
  /// names processed the last time that we reparsed the file.
  ///
  /// This hash value is used to determine when we need to refresh the
  /// global code-completion cache.
  unsigned CompletionCacheTopLevelHashValue = 0;

  /// A string hash of the top-level declaration and macro definition
  /// names processed the last time that we reparsed the precompiled preamble.
  ///
  /// This hash value is used to determine when we need to refresh the
  /// global code-completion cache after a rebuild of the precompiled preamble.
  unsigned PreambleTopLevelHashValue = 0;

  /// The current hash value for the top-level declaration and macro
  /// definition names
  unsigned CurrentTopLevelHashValue = 0;

  /// Bit used by CIndex to mark when a translation unit may be in an
  /// inconsistent state, and is not safe to free.
  unsigned UnsafeToFree : 1;

  /// \brief Enumerator specifying the scope for skipping function bodies.
  SkipFunctionBodiesScope SkipFunctionBodies = SkipFunctionBodiesScope::None;

  /// Cache any "global" code-completion results, so that we can avoid
  /// recomputing them with each completion.
  void CacheCodeCompletionResults();

  /// Clear out and deallocate
  void ClearCachedCompletionResults();

  explicit ASTUnit(bool MainFileIsAST);

  bool Parse(std::shared_ptr<PCHContainerOperations> PCHContainerOps,
             std::unique_ptr<llvm::MemoryBuffer> OverrideMainBuffer,
             IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS);

  std::unique_ptr<llvm::MemoryBuffer> getMainBufferWithPrecompiledPreamble(
      std::shared_ptr<PCHContainerOperations> PCHContainerOps,
      CompilerInvocation &PreambleInvocationIn,
      IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS, bool AllowRebuild = true,
      unsigned MaxLines = 0);
  void RealizeTopLevelDeclsFromPreamble();

  /// Transfers ownership of the objects (like SourceManager) from
  /// \param CI to this ASTUnit.
  void transferASTDataFromCompilerInstance(CompilerInstance &CI);

  /// Allows us to assert that ASTUnit is not being used concurrently,
  /// which is not supported.
  ///
  /// Clients should create instances of the ConcurrencyCheck class whenever
  /// using the ASTUnit in a way that isn't intended to be concurrent, which is
  /// just about any usage.
  /// Becomes a noop in release mode; only useful for debug mode checking.
  class ConcurrencyState {
    void *Mutex; // a llvm::sys::MutexImpl in debug;

  public:
    ConcurrencyState();
    ~ConcurrencyState();

    void start();
    void finish();
  };
  ConcurrencyState ConcurrencyCheckValue;

public:
  friend class ConcurrencyCheck;

  class ConcurrencyCheck {
    ASTUnit &Self;

  public:
    explicit ConcurrencyCheck(ASTUnit &Self) : Self(Self) {
      Self.ConcurrencyCheckValue.start();
    }

    ~ConcurrencyCheck() {
      Self.ConcurrencyCheckValue.finish();
    }
  };

  ASTUnit(const ASTUnit &) = delete;
  ASTUnit &operator=(const ASTUnit &) = delete;
  ~ASTUnit();

  bool isMainFileAST() const { return MainFileIsAST; }

  bool isUnsafeToFree() const { return UnsafeToFree; }
  void setUnsafeToFree(bool Value) { UnsafeToFree = Value; }

  const DiagnosticsEngine &getDiagnostics() const { return *Diagnostics; }
  DiagnosticsEngine &getDiagnostics() { return *Diagnostics; }

  const SourceManager &getSourceManager() const { return *SourceMgr; }
  SourceManager &getSourceManager() { return *SourceMgr; }

  const Preprocessor &getPreprocessor() const { return *PP; }
  Preprocessor &getPreprocessor() { return *PP; }
  std::shared_ptr<Preprocessor> getPreprocessorPtr() const { return PP; }

  const ASTContext &getASTContext() const { return *Ctx; }
  ASTContext &getASTContext() { return *Ctx; }

  void setASTContext(ASTContext *ctx) { Ctx = ctx; }
  void setPreprocessor(std::shared_ptr<Preprocessor> pp);

  /// Enable source-range based diagnostic messages.
  ///
  /// If diagnostic messages with source-range information are to be expected
  /// and AST comes not from file (e.g. after LoadFromCompilerInvocation) this
  /// function has to be called.
  /// The function is to be called only once and the AST should be associated
  /// with the same source file afterwards.
  void enableSourceFileDiagnostics();

  bool hasSema() const { return (bool)TheSema; }

  Sema &getSema() const {
    assert(TheSema && "ASTUnit does not have a Sema object!");
    return *TheSema;
  }

  const LangOptions &getLangOpts() const {
    assert(LangOpts && "ASTUnit does not have language options");
    return *LangOpts;
  }

  const HeaderSearchOptions &getHeaderSearchOpts() const {
    assert(HSOpts && "ASTUnit does not have header search options");
    return *HSOpts;
  }

  const PreprocessorOptions &getPreprocessorOpts() const {
    assert(PPOpts && "ASTUnit does not have preprocessor options");
    return *PPOpts;
  }

  const FileManager &getFileManager() const { return *FileMgr; }
  FileManager &getFileManager() { return *FileMgr; }

  const FileSystemOptions &getFileSystemOpts() const { return FileSystemOpts; }

  IntrusiveRefCntPtr<ASTReader> getASTReader() const;

  StringRef getOriginalSourceFileName() const {
    return OriginalSourceFile;
  }

  ASTMutationListener *getASTMutationListener();
  ASTDeserializationListener *getDeserializationListener();

  bool getOnlyLocalDecls() const { return OnlyLocalDecls; }

  bool getOwnsRemappedFileBuffers() const { return OwnsRemappedFileBuffers; }
  void setOwnsRemappedFileBuffers(bool val) { OwnsRemappedFileBuffers = val; }

  StringRef getMainFileName() const;

  /// If this ASTUnit came from an AST file, returns the filename for it.
  StringRef getASTFileName() const;

  using top_level_iterator = std::vector<Decl *>::iterator;

  top_level_iterator top_level_begin() {
    assert(!isMainFileAST() && "Invalid call for AST based ASTUnit!");
    if (!TopLevelDeclsInPreamble.empty())
      RealizeTopLevelDeclsFromPreamble();
    return TopLevelDecls.begin();
  }

  top_level_iterator top_level_end() {
    assert(!isMainFileAST() && "Invalid call for AST based ASTUnit!");
    if (!TopLevelDeclsInPreamble.empty())
      RealizeTopLevelDeclsFromPreamble();
    return TopLevelDecls.end();
  }

  std::size_t top_level_size() const {
    assert(!isMainFileAST() && "Invalid call for AST based ASTUnit!");
    return TopLevelDeclsInPreamble.size() + TopLevelDecls.size();
  }

  bool top_level_empty() const {
    assert(!isMainFileAST() && "Invalid call for AST based ASTUnit!");
    return TopLevelDeclsInPreamble.empty() && TopLevelDecls.empty();
  }

  /// Add a new top-level declaration.
  void addTopLevelDecl(Decl *D) {
    TopLevelDecls.push_back(D);
  }

  /// Add a new local file-level declaration.
  void addFileLevelDecl(Decl *D);

  /// Get the decls that are contained in a file in the Offset/Length
  /// range. \p Length can be 0 to indicate a point at \p Offset instead of
  /// a range.
  void findFileRegionDecls(FileID File, unsigned Offset, unsigned Length,
                           SmallVectorImpl<Decl *> &Decls);

  /// Retrieve a reference to the current top-level name hash value.
  ///
  /// Note: This is used internally by the top-level tracking action
  unsigned &getCurrentTopLevelHashValue() { return CurrentTopLevelHashValue; }

  /// Get the source location for the given file:line:col triplet.
  ///
  /// The difference with SourceManager::getLocation is that this method checks
  /// whether the requested location points inside the precompiled preamble
  /// in which case the returned source location will be a "loaded" one.
  SourceLocation getLocation(const FileEntry *File,
                             unsigned Line, unsigned Col) const;

  /// Get the source location for the given file:offset pair.
  SourceLocation getLocation(const FileEntry *File, unsigned Offset) const;

  /// If \p Loc is a loaded location from the preamble, returns
  /// the corresponding local location of the main file, otherwise it returns
  /// \p Loc.
  SourceLocation mapLocationFromPreamble(SourceLocation Loc) const;

  /// If \p Loc is a local location of the main file but inside the
  /// preamble chunk, returns the corresponding loaded location from the
  /// preamble, otherwise it returns \p Loc.
  SourceLocation mapLocationToPreamble(SourceLocation Loc) const;

  bool isInPreambleFileID(SourceLocation Loc) const;
  bool isInMainFileID(SourceLocation Loc) const;
  SourceLocation getStartOfMainFileID() const;
  SourceLocation getEndOfPreambleFileID() const;

  /// \see mapLocationFromPreamble.
  SourceRange mapRangeFromPreamble(SourceRange R) const {
    return SourceRange(mapLocationFromPreamble(R.getBegin()),
                       mapLocationFromPreamble(R.getEnd()));
  }

  /// \see mapLocationToPreamble.
  SourceRange mapRangeToPreamble(SourceRange R) const {
    return SourceRange(mapLocationToPreamble(R.getBegin()),
                       mapLocationToPreamble(R.getEnd()));
  }

  // Retrieve the diagnostics associated with this AST
  using stored_diag_iterator = StoredDiagnostic *;
  using stored_diag_const_iterator = const StoredDiagnostic *;

  stored_diag_const_iterator stored_diag_begin() const {
    return StoredDiagnostics.begin();
  }

  stored_diag_iterator stored_diag_begin() {
    return StoredDiagnostics.begin();
  }

  stored_diag_const_iterator stored_diag_end() const {
    return StoredDiagnostics.end();
  }

  stored_diag_iterator stored_diag_end() {
    return StoredDiagnostics.end();
  }

  unsigned stored_diag_size() const { return StoredDiagnostics.size(); }

  stored_diag_iterator stored_diag_afterDriver_begin() {
    if (NumStoredDiagnosticsFromDriver > StoredDiagnostics.size())
      NumStoredDiagnosticsFromDriver = 0;
    return StoredDiagnostics.begin() + NumStoredDiagnosticsFromDriver;
  }

  using cached_completion_iterator =
      std::vector<CachedCodeCompletionResult>::iterator;

  cached_completion_iterator cached_completion_begin() {
    return CachedCompletionResults.begin();
  }

  cached_completion_iterator cached_completion_end() {
    return CachedCompletionResults.end();
  }

  unsigned cached_completion_size() const {
    return CachedCompletionResults.size();
  }

  /// Returns an iterator range for the local preprocessing entities
  /// of the local Preprocessor, if this is a parsed source file, or the loaded
  /// preprocessing entities of the primary module if this is an AST file.
  llvm::iterator_range<PreprocessingRecord::iterator>
  getLocalPreprocessingEntities() const;

  /// Type for a function iterating over a number of declarations.
  /// \returns true to continue iteration and false to abort.
  using DeclVisitorFn = bool (*)(void *context, const Decl *D);

  /// Iterate over local declarations (locally parsed if this is a parsed
  /// source file or the loaded declarations of the primary module if this is an
  /// AST file).
  /// \returns true if the iteration was complete or false if it was aborted.
  bool visitLocalTopLevelDecls(void *context, DeclVisitorFn Fn);

  /// Get the PCH file if one was included.
  const FileEntry *getPCHFile();

  /// Returns true if the ASTUnit was constructed from a serialized
  /// module file.
  bool isModuleFile() const;

  std::unique_ptr<llvm::MemoryBuffer>
  getBufferForFile(StringRef Filename, std::string *ErrorStr = nullptr);

  /// Determine what kind of translation unit this AST represents.
  TranslationUnitKind getTranslationUnitKind() const { return TUKind; }

  /// Determine the input kind this AST unit represents.
  InputKind getInputKind() const;

  /// A mapping from a file name to the memory buffer that stores the
  /// remapped contents of that file.
  using RemappedFile = std::pair<std::string, llvm::MemoryBuffer *>;

  /// Create a ASTUnit. Gets ownership of the passed CompilerInvocation.
  static std::unique_ptr<ASTUnit>
  create(std::shared_ptr<CompilerInvocation> CI,
         IntrusiveRefCntPtr<DiagnosticsEngine> Diags, bool CaptureDiagnostics,
         bool UserFilesAreVolatile);

  enum WhatToLoad {
    /// Load options and the preprocessor state.
    LoadPreprocessorOnly,

    /// Load the AST, but do not restore Sema state.
    LoadASTOnly,

    /// Load everything, including Sema.
    LoadEverything
  };

  /// Create a ASTUnit from an AST file.
  ///
  /// \param Filename - The AST file to load.
  ///
  /// \param PCHContainerRdr - The PCHContainerOperations to use for loading and
  /// creating modules.
  /// \param Diags - The diagnostics engine to use for reporting errors; its
  /// lifetime is expected to extend past that of the returned ASTUnit.
  ///
  /// \returns - The initialized ASTUnit or null if the AST failed to load.
  static std::unique_ptr<ASTUnit> LoadFromASTFile(
      const std::string &Filename, const PCHContainerReader &PCHContainerRdr,
      WhatToLoad ToLoad, IntrusiveRefCntPtr<DiagnosticsEngine> Diags,
      const FileSystemOptions &FileSystemOpts, bool UseDebugInfo = false,
      bool OnlyLocalDecls = false, ArrayRef<RemappedFile> RemappedFiles = None,
      bool CaptureDiagnostics = false, bool AllowPCHWithCompilerErrors = false,
      bool UserFilesAreVolatile = false);

private:
  /// Helper function for \c LoadFromCompilerInvocation() and
  /// \c LoadFromCommandLine(), which loads an AST from a compiler invocation.
  ///
  /// \param PrecompilePreambleAfterNParses After how many parses the preamble
  /// of this translation unit should be precompiled, to improve the performance
  /// of reparsing. Set to zero to disable preambles.
  ///
  /// \param VFS - A llvm::vfs::FileSystem to be used for all file accesses.
  /// Note that preamble is saved to a temporary directory on a RealFileSystem,
  /// so in order for it to be loaded correctly, VFS should have access to
  /// it(i.e., be an overlay over RealFileSystem).
  ///
  /// \returns \c true if a catastrophic failure occurred (which means that the
  /// \c ASTUnit itself is invalid), or \c false otherwise.
  bool LoadFromCompilerInvocation(
      std::shared_ptr<PCHContainerOperations> PCHContainerOps,
      unsigned PrecompilePreambleAfterNParses,
      IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS);

public:
  /// Create an ASTUnit from a source file, via a CompilerInvocation
  /// object, by invoking the optionally provided ASTFrontendAction.
  ///
  /// \param CI - The compiler invocation to use; it must have exactly one input
  /// source file. The ASTUnit takes ownership of the CompilerInvocation object.
  ///
  /// \param PCHContainerOps - The PCHContainerOperations to use for loading and
  /// creating modules.
  ///
  /// \param Diags - The diagnostics engine to use for reporting errors; its
  /// lifetime is expected to extend past that of the returned ASTUnit.
  ///
  /// \param Action - The ASTFrontendAction to invoke. Its ownership is not
  /// transferred.
  ///
  /// \param Unit - optionally an already created ASTUnit. Its ownership is not
  /// transferred.
  ///
  /// \param Persistent - if true the returned ASTUnit will be complete.
  /// false means the caller is only interested in getting info through the
  /// provided \see Action.
  ///
  /// \param ErrAST - If non-null and parsing failed without any AST to return
  /// (e.g. because the PCH could not be loaded), this accepts the ASTUnit
  /// mainly to allow the caller to see the diagnostics.
  /// This will only receive an ASTUnit if a new one was created. If an already
  /// created ASTUnit was passed in \p Unit then the caller can check that.
  ///
  static ASTUnit *LoadFromCompilerInvocationAction(
      std::shared_ptr<CompilerInvocation> CI,
      std::shared_ptr<PCHContainerOperations> PCHContainerOps,
      IntrusiveRefCntPtr<DiagnosticsEngine> Diags,
      FrontendAction *Action = nullptr, ASTUnit *Unit = nullptr,
      bool Persistent = true, StringRef ResourceFilesPath = StringRef(),
      bool OnlyLocalDecls = false, bool CaptureDiagnostics = false,
      unsigned PrecompilePreambleAfterNParses = 0,
      bool CacheCodeCompletionResults = false,
      bool IncludeBriefCommentsInCodeCompletion = false,
      bool UserFilesAreVolatile = false,
      std::unique_ptr<ASTUnit> *ErrAST = nullptr);

  /// LoadFromCompilerInvocation - Create an ASTUnit from a source file, via a
  /// CompilerInvocation object.
  ///
  /// \param CI - The compiler invocation to use; it must have exactly one input
  /// source file. The ASTUnit takes ownership of the CompilerInvocation object.
  ///
  /// \param PCHContainerOps - The PCHContainerOperations to use for loading and
  /// creating modules.
  ///
  /// \param Diags - The diagnostics engine to use for reporting errors; its
  /// lifetime is expected to extend past that of the returned ASTUnit.
  //
  // FIXME: Move OnlyLocalDecls, UseBumpAllocator to setters on the ASTUnit, we
  // shouldn't need to specify them at construction time.
  static std::unique_ptr<ASTUnit> LoadFromCompilerInvocation(
      std::shared_ptr<CompilerInvocation> CI,
      std::shared_ptr<PCHContainerOperations> PCHContainerOps,
      IntrusiveRefCntPtr<DiagnosticsEngine> Diags, FileManager *FileMgr,
      bool OnlyLocalDecls = false, bool CaptureDiagnostics = false,
      unsigned PrecompilePreambleAfterNParses = 0,
      TranslationUnitKind TUKind = TU_Complete,
      bool CacheCodeCompletionResults = false,
      bool IncludeBriefCommentsInCodeCompletion = false,
      bool UserFilesAreVolatile = false);

  /// LoadFromCommandLine - Create an ASTUnit from a vector of command line
  /// arguments, which must specify exactly one source file.
  ///
  /// \param ArgBegin - The beginning of the argument vector.
  ///
  /// \param ArgEnd - The end of the argument vector.
  ///
  /// \param PCHContainerOps - The PCHContainerOperations to use for loading and
  /// creating modules.
  ///
  /// \param Diags - The diagnostics engine to use for reporting errors; its
  /// lifetime is expected to extend past that of the returned ASTUnit.
  ///
  /// \param ResourceFilesPath - The path to the compiler resource files.
  ///
  /// \param ModuleFormat - If provided, uses the specific module format.
  ///
  /// \param ErrAST - If non-null and parsing failed without any AST to return
  /// (e.g. because the PCH could not be loaded), this accepts the ASTUnit
  /// mainly to allow the caller to see the diagnostics.
  ///
  /// \param VFS - A llvm::vfs::FileSystem to be used for all file accesses.
  /// Note that preamble is saved to a temporary directory on a RealFileSystem,
  /// so in order for it to be loaded correctly, VFS should have access to
  /// it(i.e., be an overlay over RealFileSystem). RealFileSystem will be used
  /// if \p VFS is nullptr.
  ///
  // FIXME: Move OnlyLocalDecls, UseBumpAllocator to setters on the ASTUnit, we
  // shouldn't need to specify them at construction time.
  static ASTUnit *LoadFromCommandLine(
      const char **ArgBegin, const char **ArgEnd,
      std::shared_ptr<PCHContainerOperations> PCHContainerOps,
      IntrusiveRefCntPtr<DiagnosticsEngine> Diags, StringRef ResourceFilesPath,
      bool OnlyLocalDecls = false, bool CaptureDiagnostics = false,
      ArrayRef<RemappedFile> RemappedFiles = None,
      bool RemappedFilesKeepOriginalName = true,
      unsigned PrecompilePreambleAfterNParses = 0,
      TranslationUnitKind TUKind = TU_Complete,
      bool CacheCodeCompletionResults = false,
      bool IncludeBriefCommentsInCodeCompletion = false,
      bool AllowPCHWithCompilerErrors = false,
      SkipFunctionBodiesScope SkipFunctionBodies =
          SkipFunctionBodiesScope::None,
      bool SingleFileParse = false, bool UserFilesAreVolatile = false,
      bool ForSerialization = false,
      llvm::Optional<StringRef> ModuleFormat = llvm::None,
      std::unique_ptr<ASTUnit> *ErrAST = nullptr,
      IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS = nullptr);

  /// Reparse the source files using the same command-line options that
  /// were originally used to produce this translation unit.
  ///
  /// \param VFS - A llvm::vfs::FileSystem to be used for all file accesses.
  /// Note that preamble is saved to a temporary directory on a RealFileSystem,
  /// so in order for it to be loaded correctly, VFS should give an access to
  /// this(i.e. be an overlay over RealFileSystem).
  /// FileMgr->getVirtualFileSystem() will be used if \p VFS is nullptr.
  ///
  /// \returns True if a failure occurred that causes the ASTUnit not to
  /// contain any translation-unit information, false otherwise.
  bool Reparse(std::shared_ptr<PCHContainerOperations> PCHContainerOps,
               ArrayRef<RemappedFile> RemappedFiles = None,
               IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS = nullptr);

  /// Free data that will be re-generated on the next parse.
  ///
  /// Preamble-related data is not affected.
  void ResetForParse();

  /// Perform code completion at the given file, line, and
  /// column within this translation unit.
  ///
  /// \param File The file in which code completion will occur.
  ///
  /// \param Line The line at which code completion will occur.
  ///
  /// \param Column The column at which code completion will occur.
  ///
  /// \param IncludeMacros Whether to include macros in the code-completion
  /// results.
  ///
  /// \param IncludeCodePatterns Whether to include code patterns (such as a
  /// for loop) in the code-completion results.
  ///
  /// \param IncludeBriefComments Whether to include brief documentation within
  /// the set of code completions returned.
  ///
  /// FIXME: The Diag, LangOpts, SourceMgr, FileMgr, StoredDiagnostics, and
  /// OwnedBuffers parameters are all disgusting hacks. They will go away.
  void CodeComplete(StringRef File, unsigned Line, unsigned Column,
                    ArrayRef<RemappedFile> RemappedFiles, bool IncludeMacros,
                    bool IncludeCodePatterns, bool IncludeBriefComments,
                    CodeCompleteConsumer &Consumer,
                    std::shared_ptr<PCHContainerOperations> PCHContainerOps,
                    DiagnosticsEngine &Diag, LangOptions &LangOpts,
                    SourceManager &SourceMgr, FileManager &FileMgr,
                    SmallVectorImpl<StoredDiagnostic> &StoredDiagnostics,
                    SmallVectorImpl<const llvm::MemoryBuffer *> &OwnedBuffers);

  /// Save this translation unit to a file with the given name.
  ///
  /// \returns true if there was a file error or false if the save was
  /// successful.
  bool Save(StringRef File);

  /// Serialize this translation unit with the given output stream.
  ///
  /// \returns True if an error occurred, false otherwise.
  bool serialize(raw_ostream &OS);
};

} // namespace clang

#endif // LLVM_CLANG_FRONTEND_ASTUNIT_H
