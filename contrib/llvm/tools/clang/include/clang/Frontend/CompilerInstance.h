//===-- CompilerInstance.h - Clang Compiler Instance ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_COMPILERINSTANCE_H_
#define LLVM_CLANG_FRONTEND_COMPILERINSTANCE_H_

#include "clang/AST/ASTConsumer.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/PCHContainerOperations.h"
#include "clang/Frontend/Utils.h"
#include "clang/Lex/HeaderSearchOptions.h"
#include "clang/Lex/ModuleLoader.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/BuryPointer.h"
#include <cassert>
#include <list>
#include <memory>
#include <string>
#include <utility>

namespace llvm {
class raw_fd_ostream;
class Timer;
class TimerGroup;
}

namespace clang {
class ASTContext;
class ASTReader;
class CodeCompleteConsumer;
class DiagnosticsEngine;
class DiagnosticConsumer;
class ExternalASTSource;
class FileEntry;
class FileManager;
class FrontendAction;
class MemoryBufferCache;
class Module;
class Preprocessor;
class Sema;
class SourceManager;
class TargetInfo;

/// CompilerInstance - Helper class for managing a single instance of the Clang
/// compiler.
///
/// The CompilerInstance serves two purposes:
///  (1) It manages the various objects which are necessary to run the compiler,
///      for example the preprocessor, the target information, and the AST
///      context.
///  (2) It provides utility routines for constructing and manipulating the
///      common Clang objects.
///
/// The compiler instance generally owns the instance of all the objects that it
/// manages. However, clients can still share objects by manually setting the
/// object and retaking ownership prior to destroying the CompilerInstance.
///
/// The compiler instance is intended to simplify clients, but not to lock them
/// in to the compiler instance for everything. When possible, utility functions
/// come in two forms; a short form that reuses the CompilerInstance objects,
/// and a long form that takes explicit instances of any required objects.
class CompilerInstance : public ModuleLoader {
  /// The options used in this compiler instance.
  std::shared_ptr<CompilerInvocation> Invocation;

  /// The diagnostics engine instance.
  IntrusiveRefCntPtr<DiagnosticsEngine> Diagnostics;

  /// The target being compiled for.
  IntrusiveRefCntPtr<TargetInfo> Target;

  /// Auxiliary Target info.
  IntrusiveRefCntPtr<TargetInfo> AuxTarget;

  /// The virtual file system.
  IntrusiveRefCntPtr<llvm::vfs::FileSystem> VirtualFileSystem;

  /// The file manager.
  IntrusiveRefCntPtr<FileManager> FileMgr;

  /// The source manager.
  IntrusiveRefCntPtr<SourceManager> SourceMgr;

  /// The cache of PCM files.
  IntrusiveRefCntPtr<MemoryBufferCache> PCMCache;

  /// The preprocessor.
  std::shared_ptr<Preprocessor> PP;

  /// The AST context.
  IntrusiveRefCntPtr<ASTContext> Context;

  /// An optional sema source that will be attached to sema.
  IntrusiveRefCntPtr<ExternalSemaSource> ExternalSemaSrc;

  /// The AST consumer.
  std::unique_ptr<ASTConsumer> Consumer;

  /// The code completion consumer.
  std::unique_ptr<CodeCompleteConsumer> CompletionConsumer;

  /// The semantic analysis object.
  std::unique_ptr<Sema> TheSema;

  /// The frontend timer group.
  std::unique_ptr<llvm::TimerGroup> FrontendTimerGroup;

  /// The frontend timer.
  std::unique_ptr<llvm::Timer> FrontendTimer;

  /// The ASTReader, if one exists.
  IntrusiveRefCntPtr<ASTReader> ModuleManager;

  /// The module dependency collector for crashdumps
  std::shared_ptr<ModuleDependencyCollector> ModuleDepCollector;

  /// The module provider.
  std::shared_ptr<PCHContainerOperations> ThePCHContainerOperations;

  /// The dependency file generator.
  std::unique_ptr<DependencyFileGenerator> TheDependencyFileGenerator;

  std::vector<std::shared_ptr<DependencyCollector>> DependencyCollectors;

  /// The set of top-level modules that has already been loaded,
  /// along with the module map
  llvm::DenseMap<const IdentifierInfo *, Module *> KnownModules;

  /// The set of top-level modules that has already been built on the
  /// fly as part of this overall compilation action.
  std::map<std::string, std::string> BuiltModules;

  /// Should we delete the BuiltModules when we're done?
  bool DeleteBuiltModules = true;

  /// The location of the module-import keyword for the last module
  /// import.
  SourceLocation LastModuleImportLoc;

  /// The result of the last module import.
  ///
  ModuleLoadResult LastModuleImportResult;

  /// Whether we should (re)build the global module index once we
  /// have finished with this translation unit.
  bool BuildGlobalModuleIndex = false;

  /// We have a full global module index, with all modules.
  bool HaveFullGlobalModuleIndex = false;

  /// One or more modules failed to build.
  bool ModuleBuildFailed = false;

  /// Holds information about the output file.
  ///
  /// If TempFilename is not empty we must rename it to Filename at the end.
  /// TempFilename may be empty and Filename non-empty if creating the temporary
  /// failed.
  struct OutputFile {
    std::string Filename;
    std::string TempFilename;

    OutputFile(std::string filename, std::string tempFilename)
        : Filename(std::move(filename)), TempFilename(std::move(tempFilename)) {
    }
  };

  /// If the output doesn't support seeking (terminal, pipe). we switch
  /// the stream to a buffer_ostream. These are the buffer and the original
  /// stream.
  std::unique_ptr<llvm::raw_fd_ostream> NonSeekStream;

  /// The list of active output files.
  std::list<OutputFile> OutputFiles;

  /// Force an output buffer.
  std::unique_ptr<llvm::raw_pwrite_stream> OutputStream;

  CompilerInstance(const CompilerInstance &) = delete;
  void operator=(const CompilerInstance &) = delete;
public:
  explicit CompilerInstance(
      std::shared_ptr<PCHContainerOperations> PCHContainerOps =
          std::make_shared<PCHContainerOperations>(),
      MemoryBufferCache *SharedPCMCache = nullptr);
  ~CompilerInstance() override;

  /// @name High-Level Operations
  /// {

  /// ExecuteAction - Execute the provided action against the compiler's
  /// CompilerInvocation object.
  ///
  /// This function makes the following assumptions:
  ///
  ///  - The invocation options should be initialized. This function does not
  ///    handle the '-help' or '-version' options, clients should handle those
  ///    directly.
  ///
  ///  - The diagnostics engine should have already been created by the client.
  ///
  ///  - No other CompilerInstance state should have been initialized (this is
  ///    an unchecked error).
  ///
  ///  - Clients should have initialized any LLVM target features that may be
  ///    required.
  ///
  ///  - Clients should eventually call llvm_shutdown() upon the completion of
  ///    this routine to ensure that any managed objects are properly destroyed.
  ///
  /// Note that this routine may write output to 'stderr'.
  ///
  /// \param Act - The action to execute.
  /// \return - True on success.
  //
  // FIXME: This function should take the stream to write any debugging /
  // verbose output to as an argument.
  //
  // FIXME: Eliminate the llvm_shutdown requirement, that should either be part
  // of the context or else not CompilerInstance specific.
  bool ExecuteAction(FrontendAction &Act);

  /// }
  /// @name Compiler Invocation and Options
  /// {

  bool hasInvocation() const { return Invocation != nullptr; }

  CompilerInvocation &getInvocation() {
    assert(Invocation && "Compiler instance has no invocation!");
    return *Invocation;
  }

  /// setInvocation - Replace the current invocation.
  void setInvocation(std::shared_ptr<CompilerInvocation> Value);

  /// Indicates whether we should (re)build the global module index.
  bool shouldBuildGlobalModuleIndex() const;

  /// Set the flag indicating whether we should (re)build the global
  /// module index.
  void setBuildGlobalModuleIndex(bool Build) {
    BuildGlobalModuleIndex = Build;
  }

  /// }
  /// @name Forwarding Methods
  /// {

  AnalyzerOptionsRef getAnalyzerOpts() {
    return Invocation->getAnalyzerOpts();
  }

  CodeGenOptions &getCodeGenOpts() {
    return Invocation->getCodeGenOpts();
  }
  const CodeGenOptions &getCodeGenOpts() const {
    return Invocation->getCodeGenOpts();
  }

  DependencyOutputOptions &getDependencyOutputOpts() {
    return Invocation->getDependencyOutputOpts();
  }
  const DependencyOutputOptions &getDependencyOutputOpts() const {
    return Invocation->getDependencyOutputOpts();
  }

  DiagnosticOptions &getDiagnosticOpts() {
    return Invocation->getDiagnosticOpts();
  }
  const DiagnosticOptions &getDiagnosticOpts() const {
    return Invocation->getDiagnosticOpts();
  }

  FileSystemOptions &getFileSystemOpts() {
    return Invocation->getFileSystemOpts();
  }
  const FileSystemOptions &getFileSystemOpts() const {
    return Invocation->getFileSystemOpts();
  }

  FrontendOptions &getFrontendOpts() {
    return Invocation->getFrontendOpts();
  }
  const FrontendOptions &getFrontendOpts() const {
    return Invocation->getFrontendOpts();
  }

  HeaderSearchOptions &getHeaderSearchOpts() {
    return Invocation->getHeaderSearchOpts();
  }
  const HeaderSearchOptions &getHeaderSearchOpts() const {
    return Invocation->getHeaderSearchOpts();
  }
  std::shared_ptr<HeaderSearchOptions> getHeaderSearchOptsPtr() const {
    return Invocation->getHeaderSearchOptsPtr();
  }

  LangOptions &getLangOpts() {
    return *Invocation->getLangOpts();
  }
  const LangOptions &getLangOpts() const {
    return *Invocation->getLangOpts();
  }

  PreprocessorOptions &getPreprocessorOpts() {
    return Invocation->getPreprocessorOpts();
  }
  const PreprocessorOptions &getPreprocessorOpts() const {
    return Invocation->getPreprocessorOpts();
  }

  PreprocessorOutputOptions &getPreprocessorOutputOpts() {
    return Invocation->getPreprocessorOutputOpts();
  }
  const PreprocessorOutputOptions &getPreprocessorOutputOpts() const {
    return Invocation->getPreprocessorOutputOpts();
  }

  TargetOptions &getTargetOpts() {
    return Invocation->getTargetOpts();
  }
  const TargetOptions &getTargetOpts() const {
    return Invocation->getTargetOpts();
  }

  /// }
  /// @name Diagnostics Engine
  /// {

  bool hasDiagnostics() const { return Diagnostics != nullptr; }

  /// Get the current diagnostics engine.
  DiagnosticsEngine &getDiagnostics() const {
    assert(Diagnostics && "Compiler instance has no diagnostics!");
    return *Diagnostics;
  }

  /// setDiagnostics - Replace the current diagnostics engine.
  void setDiagnostics(DiagnosticsEngine *Value);

  DiagnosticConsumer &getDiagnosticClient() const {
    assert(Diagnostics && Diagnostics->getClient() &&
           "Compiler instance has no diagnostic client!");
    return *Diagnostics->getClient();
  }

  /// }
  /// @name Target Info
  /// {

  bool hasTarget() const { return Target != nullptr; }

  TargetInfo &getTarget() const {
    assert(Target && "Compiler instance has no target!");
    return *Target;
  }

  /// Replace the current Target.
  void setTarget(TargetInfo *Value);

  /// }
  /// @name AuxTarget Info
  /// {

  TargetInfo *getAuxTarget() const { return AuxTarget.get(); }

  /// Replace the current AuxTarget.
  void setAuxTarget(TargetInfo *Value);

  /// }
  /// @name Virtual File System
  /// {

  bool hasVirtualFileSystem() const { return VirtualFileSystem != nullptr; }

  llvm::vfs::FileSystem &getVirtualFileSystem() const {
    assert(hasVirtualFileSystem() &&
           "Compiler instance has no virtual file system");
    return *VirtualFileSystem;
  }

  /// Replace the current virtual file system.
  ///
  /// \note Most clients should use setFileManager, which will implicitly reset
  /// the virtual file system to the one contained in the file manager.
  void setVirtualFileSystem(IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS) {
    VirtualFileSystem = std::move(FS);
  }

  /// }
  /// @name File Manager
  /// {

  bool hasFileManager() const { return FileMgr != nullptr; }

  /// Return the current file manager to the caller.
  FileManager &getFileManager() const {
    assert(FileMgr && "Compiler instance has no file manager!");
    return *FileMgr;
  }

  void resetAndLeakFileManager() {
    llvm::BuryPointer(FileMgr.get());
    FileMgr.resetWithoutRelease();
  }

  /// Replace the current file manager and virtual file system.
  void setFileManager(FileManager *Value);

  /// }
  /// @name Source Manager
  /// {

  bool hasSourceManager() const { return SourceMgr != nullptr; }

  /// Return the current source manager.
  SourceManager &getSourceManager() const {
    assert(SourceMgr && "Compiler instance has no source manager!");
    return *SourceMgr;
  }

  void resetAndLeakSourceManager() {
    llvm::BuryPointer(SourceMgr.get());
    SourceMgr.resetWithoutRelease();
  }

  /// setSourceManager - Replace the current source manager.
  void setSourceManager(SourceManager *Value);

  /// }
  /// @name Preprocessor
  /// {

  bool hasPreprocessor() const { return PP != nullptr; }

  /// Return the current preprocessor.
  Preprocessor &getPreprocessor() const {
    assert(PP && "Compiler instance has no preprocessor!");
    return *PP;
  }

  std::shared_ptr<Preprocessor> getPreprocessorPtr() { return PP; }

  void resetAndLeakPreprocessor() {
    llvm::BuryPointer(new std::shared_ptr<Preprocessor>(PP));
  }

  /// Replace the current preprocessor.
  void setPreprocessor(std::shared_ptr<Preprocessor> Value);

  /// }
  /// @name ASTContext
  /// {

  bool hasASTContext() const { return Context != nullptr; }

  ASTContext &getASTContext() const {
    assert(Context && "Compiler instance has no AST context!");
    return *Context;
  }

  void resetAndLeakASTContext() {
    llvm::BuryPointer(Context.get());
    Context.resetWithoutRelease();
  }

  /// setASTContext - Replace the current AST context.
  void setASTContext(ASTContext *Value);

  /// Replace the current Sema; the compiler instance takes ownership
  /// of S.
  void setSema(Sema *S);

  /// }
  /// @name ASTConsumer
  /// {

  bool hasASTConsumer() const { return (bool)Consumer; }

  ASTConsumer &getASTConsumer() const {
    assert(Consumer && "Compiler instance has no AST consumer!");
    return *Consumer;
  }

  /// takeASTConsumer - Remove the current AST consumer and give ownership to
  /// the caller.
  std::unique_ptr<ASTConsumer> takeASTConsumer() { return std::move(Consumer); }

  /// setASTConsumer - Replace the current AST consumer; the compiler instance
  /// takes ownership of \p Value.
  void setASTConsumer(std::unique_ptr<ASTConsumer> Value);

  /// }
  /// @name Semantic analysis
  /// {
  bool hasSema() const { return (bool)TheSema; }

  Sema &getSema() const {
    assert(TheSema && "Compiler instance has no Sema object!");
    return *TheSema;
  }

  std::unique_ptr<Sema> takeSema();
  void resetAndLeakSema();

  /// }
  /// @name Module Management
  /// {

  IntrusiveRefCntPtr<ASTReader> getModuleManager() const;
  void setModuleManager(IntrusiveRefCntPtr<ASTReader> Reader);

  std::shared_ptr<ModuleDependencyCollector> getModuleDepCollector() const;
  void setModuleDepCollector(
      std::shared_ptr<ModuleDependencyCollector> Collector);

  std::shared_ptr<PCHContainerOperations> getPCHContainerOperations() const {
    return ThePCHContainerOperations;
  }

  /// Return the appropriate PCHContainerWriter depending on the
  /// current CodeGenOptions.
  const PCHContainerWriter &getPCHContainerWriter() const {
    assert(Invocation && "cannot determine module format without invocation");
    StringRef Format = getHeaderSearchOpts().ModuleFormat;
    auto *Writer = ThePCHContainerOperations->getWriterOrNull(Format);
    if (!Writer) {
      if (Diagnostics)
        Diagnostics->Report(diag::err_module_format_unhandled) << Format;
      llvm::report_fatal_error("unknown module format");
    }
    return *Writer;
  }

  /// Return the appropriate PCHContainerReader depending on the
  /// current CodeGenOptions.
  const PCHContainerReader &getPCHContainerReader() const {
    assert(Invocation && "cannot determine module format without invocation");
    StringRef Format = getHeaderSearchOpts().ModuleFormat;
    auto *Reader = ThePCHContainerOperations->getReaderOrNull(Format);
    if (!Reader) {
      if (Diagnostics)
        Diagnostics->Report(diag::err_module_format_unhandled) << Format;
      llvm::report_fatal_error("unknown module format");
    }
    return *Reader;
  }

  /// }
  /// @name Code Completion
  /// {

  bool hasCodeCompletionConsumer() const { return (bool)CompletionConsumer; }

  CodeCompleteConsumer &getCodeCompletionConsumer() const {
    assert(CompletionConsumer &&
           "Compiler instance has no code completion consumer!");
    return *CompletionConsumer;
  }

  /// setCodeCompletionConsumer - Replace the current code completion consumer;
  /// the compiler instance takes ownership of \p Value.
  void setCodeCompletionConsumer(CodeCompleteConsumer *Value);

  /// }
  /// @name Frontend timer
  /// {

  bool hasFrontendTimer() const { return (bool)FrontendTimer; }

  llvm::Timer &getFrontendTimer() const {
    assert(FrontendTimer && "Compiler instance has no frontend timer!");
    return *FrontendTimer;
  }

  /// }
  /// @name Output Files
  /// {

  /// addOutputFile - Add an output file onto the list of tracked output files.
  ///
  /// \param OutFile - The output file info.
  void addOutputFile(OutputFile &&OutFile);

  /// clearOutputFiles - Clear the output file list. The underlying output
  /// streams must have been closed beforehand.
  ///
  /// \param EraseFiles - If true, attempt to erase the files from disk.
  void clearOutputFiles(bool EraseFiles);

  /// }
  /// @name Construction Utility Methods
  /// {

  /// Create the diagnostics engine using the invocation's diagnostic options
  /// and replace any existing one with it.
  ///
  /// Note that this routine also replaces the diagnostic client,
  /// allocating one if one is not provided.
  ///
  /// \param Client If non-NULL, a diagnostic client that will be
  /// attached to (and, then, owned by) the DiagnosticsEngine inside this AST
  /// unit.
  ///
  /// \param ShouldOwnClient If Client is non-NULL, specifies whether
  /// the diagnostic object should take ownership of the client.
  void createDiagnostics(DiagnosticConsumer *Client = nullptr,
                         bool ShouldOwnClient = true);

  /// Create a DiagnosticsEngine object with a the TextDiagnosticPrinter.
  ///
  /// If no diagnostic client is provided, this creates a
  /// DiagnosticConsumer that is owned by the returned diagnostic
  /// object, if using directly the caller is responsible for
  /// releasing the returned DiagnosticsEngine's client eventually.
  ///
  /// \param Opts - The diagnostic options; note that the created text
  /// diagnostic object contains a reference to these options.
  ///
  /// \param Client If non-NULL, a diagnostic client that will be
  /// attached to (and, then, owned by) the returned DiagnosticsEngine
  /// object.
  ///
  /// \param CodeGenOpts If non-NULL, the code gen options in use, which may be
  /// used by some diagnostics printers (for logging purposes only).
  ///
  /// \return The new object on success, or null on failure.
  static IntrusiveRefCntPtr<DiagnosticsEngine>
  createDiagnostics(DiagnosticOptions *Opts,
                    DiagnosticConsumer *Client = nullptr,
                    bool ShouldOwnClient = true,
                    const CodeGenOptions *CodeGenOpts = nullptr);

  /// Create the file manager and replace any existing one with it.
  ///
  /// \return The new file manager on success, or null on failure.
  FileManager *createFileManager();

  /// Create the source manager and replace any existing one with it.
  void createSourceManager(FileManager &FileMgr);

  /// Create the preprocessor, using the invocation, file, and source managers,
  /// and replace any existing one with it.
  void createPreprocessor(TranslationUnitKind TUKind);

  std::string getSpecificModuleCachePath();

  /// Create the AST context.
  void createASTContext();

  /// Create an external AST source to read a PCH file and attach it to the AST
  /// context.
  void createPCHExternalASTSource(StringRef Path, bool DisablePCHValidation,
                                  bool AllowPCHWithCompilerErrors,
                                  void *DeserializationListener,
                                  bool OwnDeserializationListener);

  /// Create an external AST source to read a PCH file.
  ///
  /// \return - The new object on success, or null on failure.
  static IntrusiveRefCntPtr<ASTReader> createPCHExternalASTSource(
      StringRef Path, StringRef Sysroot, bool DisablePCHValidation,
      bool AllowPCHWithCompilerErrors, Preprocessor &PP, ASTContext &Context,
      const PCHContainerReader &PCHContainerRdr,
      ArrayRef<std::shared_ptr<ModuleFileExtension>> Extensions,
      DependencyFileGenerator *DependencyFile,
      ArrayRef<std::shared_ptr<DependencyCollector>> DependencyCollectors,
      void *DeserializationListener, bool OwnDeserializationListener,
      bool Preamble, bool UseGlobalModuleIndex);

  /// Create a code completion consumer using the invocation; note that this
  /// will cause the source manager to truncate the input source file at the
  /// completion point.
  void createCodeCompletionConsumer();

  /// Create a code completion consumer to print code completion results, at
  /// \p Filename, \p Line, and \p Column, to the given output stream \p OS.
  static CodeCompleteConsumer *createCodeCompletionConsumer(
      Preprocessor &PP, StringRef Filename, unsigned Line, unsigned Column,
      const CodeCompleteOptions &Opts, raw_ostream &OS);

  /// Create the Sema object to be used for parsing.
  void createSema(TranslationUnitKind TUKind,
                  CodeCompleteConsumer *CompletionConsumer);

  /// Create the frontend timer and replace any existing one with it.
  void createFrontendTimer();

  /// Create the default output file (from the invocation's options) and add it
  /// to the list of tracked output files.
  ///
  /// The files created by this function always use temporary files to write to
  /// their result (that is, the data is written to a temporary file which will
  /// atomically replace the target output on success).
  ///
  /// \return - Null on error.
  std::unique_ptr<raw_pwrite_stream>
  createDefaultOutputFile(bool Binary = true, StringRef BaseInput = "",
                          StringRef Extension = "");

  /// Create a new output file and add it to the list of tracked output files,
  /// optionally deriving the output path name.
  ///
  /// \return - Null on error.
  std::unique_ptr<raw_pwrite_stream>
  createOutputFile(StringRef OutputPath, bool Binary, bool RemoveFileOnSignal,
                   StringRef BaseInput, StringRef Extension, bool UseTemporary,
                   bool CreateMissingDirectories = false);

  /// Create a new output file, optionally deriving the output path name.
  ///
  /// If \p OutputPath is empty, then createOutputFile will derive an output
  /// path location as \p BaseInput, with any suffix removed, and \p Extension
  /// appended. If \p OutputPath is not stdout and \p UseTemporary
  /// is true, createOutputFile will create a new temporary file that must be
  /// renamed to \p OutputPath in the end.
  ///
  /// \param OutputPath - If given, the path to the output file.
  /// \param Error [out] - On failure, the error.
  /// \param BaseInput - If \p OutputPath is empty, the input path name to use
  /// for deriving the output path.
  /// \param Extension - The extension to use for derived output names.
  /// \param Binary - The mode to open the file in.
  /// \param RemoveFileOnSignal - Whether the file should be registered with
  /// llvm::sys::RemoveFileOnSignal. Note that this is not safe for
  /// multithreaded use, as the underlying signal mechanism is not reentrant
  /// \param UseTemporary - Create a new temporary file that must be renamed to
  /// OutputPath in the end.
  /// \param CreateMissingDirectories - When \p UseTemporary is true, create
  /// missing directories in the output path.
  /// \param ResultPathName [out] - If given, the result path name will be
  /// stored here on success.
  /// \param TempPathName [out] - If given, the temporary file path name
  /// will be stored here on success.
  std::unique_ptr<raw_pwrite_stream>
  createOutputFile(StringRef OutputPath, std::error_code &Error, bool Binary,
                   bool RemoveFileOnSignal, StringRef BaseInput,
                   StringRef Extension, bool UseTemporary,
                   bool CreateMissingDirectories, std::string *ResultPathName,
                   std::string *TempPathName);

  std::unique_ptr<raw_pwrite_stream> createNullOutputFile();

  /// }
  /// @name Initialization Utility Methods
  /// {

  /// InitializeSourceManager - Initialize the source manager to set InputFile
  /// as the main file.
  ///
  /// \return True on success.
  bool InitializeSourceManager(const FrontendInputFile &Input);

  /// InitializeSourceManager - Initialize the source manager to set InputFile
  /// as the main file.
  ///
  /// \return True on success.
  static bool InitializeSourceManager(const FrontendInputFile &Input,
                                      DiagnosticsEngine &Diags,
                                      FileManager &FileMgr,
                                      SourceManager &SourceMgr,
                                      HeaderSearch *HS,
                                      DependencyOutputOptions &DepOpts,
                                      const FrontendOptions &Opts);

  /// }

  void setOutputStream(std::unique_ptr<llvm::raw_pwrite_stream> OutStream) {
    OutputStream = std::move(OutStream);
  }

  std::unique_ptr<llvm::raw_pwrite_stream> takeOutputStream() {
    return std::move(OutputStream);
  }

  // Create module manager.
  void createModuleManager();

  bool loadModuleFile(StringRef FileName);

  ModuleLoadResult loadModule(SourceLocation ImportLoc, ModuleIdPath Path,
                              Module::NameVisibilityKind Visibility,
                              bool IsInclusionDirective) override;

  void loadModuleFromSource(SourceLocation ImportLoc, StringRef ModuleName,
                            StringRef Source) override;

  void makeModuleVisible(Module *Mod, Module::NameVisibilityKind Visibility,
                         SourceLocation ImportLoc) override;

  bool hadModuleLoaderFatalFailure() const {
    return ModuleLoader::HadFatalFailure;
  }

  GlobalModuleIndex *loadGlobalModuleIndex(SourceLocation TriggerLoc) override;

  bool lookupMissingImports(StringRef Name, SourceLocation TriggerLoc) override;

  void addDependencyCollector(std::shared_ptr<DependencyCollector> Listener) {
    DependencyCollectors.push_back(std::move(Listener));
  }

  void setExternalSemaSource(IntrusiveRefCntPtr<ExternalSemaSource> ESS);

  MemoryBufferCache &getPCMCache() const { return *PCMCache; }
};

} // end namespace clang

#endif
