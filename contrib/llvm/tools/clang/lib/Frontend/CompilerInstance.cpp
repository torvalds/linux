//===--- CompilerInstance.cpp ---------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/CompilerInstance.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/MemoryBufferCache.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Stack.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/Version.h"
#include "clang/Config/config.h"
#include "clang/Frontend/ChainedDiagnosticConsumer.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Frontend/LogDiagnosticPrinter.h"
#include "clang/Frontend/SerializedDiagnosticPrinter.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Frontend/Utils.h"
#include "clang/Frontend/VerifyDiagnosticConsumer.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Sema/CodeCompleteConsumer.h"
#include "clang/Sema/Sema.h"
#include "clang/Serialization/ASTReader.h"
#include "clang/Serialization/GlobalModuleIndex.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/BuryPointer.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/LockFileManager.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include <sys/stat.h>
#include <system_error>
#include <time.h>
#include <utility>

using namespace clang;

CompilerInstance::CompilerInstance(
    std::shared_ptr<PCHContainerOperations> PCHContainerOps,
    MemoryBufferCache *SharedPCMCache)
    : ModuleLoader(/* BuildingModule = */ SharedPCMCache),
      Invocation(new CompilerInvocation()),
      PCMCache(SharedPCMCache ? SharedPCMCache : new MemoryBufferCache),
      ThePCHContainerOperations(std::move(PCHContainerOps)) {
  // Don't allow this to invalidate buffers in use by others.
  if (SharedPCMCache)
    getPCMCache().finalizeCurrentBuffers();
}

CompilerInstance::~CompilerInstance() {
  assert(OutputFiles.empty() && "Still output files in flight?");
}

void CompilerInstance::setInvocation(
    std::shared_ptr<CompilerInvocation> Value) {
  Invocation = std::move(Value);
}

bool CompilerInstance::shouldBuildGlobalModuleIndex() const {
  return (BuildGlobalModuleIndex ||
          (ModuleManager && ModuleManager->isGlobalIndexUnavailable() &&
           getFrontendOpts().GenerateGlobalModuleIndex)) &&
         !ModuleBuildFailed;
}

void CompilerInstance::setDiagnostics(DiagnosticsEngine *Value) {
  Diagnostics = Value;
}

void CompilerInstance::setTarget(TargetInfo *Value) { Target = Value; }
void CompilerInstance::setAuxTarget(TargetInfo *Value) { AuxTarget = Value; }

void CompilerInstance::setFileManager(FileManager *Value) {
  FileMgr = Value;
  if (Value)
    VirtualFileSystem = Value->getVirtualFileSystem();
  else
    VirtualFileSystem.reset();
}

void CompilerInstance::setSourceManager(SourceManager *Value) {
  SourceMgr = Value;
}

void CompilerInstance::setPreprocessor(std::shared_ptr<Preprocessor> Value) {
  PP = std::move(Value);
}

void CompilerInstance::setASTContext(ASTContext *Value) {
  Context = Value;

  if (Context && Consumer)
    getASTConsumer().Initialize(getASTContext());
}

void CompilerInstance::setSema(Sema *S) {
  TheSema.reset(S);
}

void CompilerInstance::setASTConsumer(std::unique_ptr<ASTConsumer> Value) {
  Consumer = std::move(Value);

  if (Context && Consumer)
    getASTConsumer().Initialize(getASTContext());
}

void CompilerInstance::setCodeCompletionConsumer(CodeCompleteConsumer *Value) {
  CompletionConsumer.reset(Value);
}

std::unique_ptr<Sema> CompilerInstance::takeSema() {
  return std::move(TheSema);
}

IntrusiveRefCntPtr<ASTReader> CompilerInstance::getModuleManager() const {
  return ModuleManager;
}
void CompilerInstance::setModuleManager(IntrusiveRefCntPtr<ASTReader> Reader) {
  assert(PCMCache.get() == &Reader->getModuleManager().getPCMCache() &&
         "Expected ASTReader to use the same PCM cache");
  ModuleManager = std::move(Reader);
}

std::shared_ptr<ModuleDependencyCollector>
CompilerInstance::getModuleDepCollector() const {
  return ModuleDepCollector;
}

void CompilerInstance::setModuleDepCollector(
    std::shared_ptr<ModuleDependencyCollector> Collector) {
  ModuleDepCollector = std::move(Collector);
}

static void collectHeaderMaps(const HeaderSearch &HS,
                              std::shared_ptr<ModuleDependencyCollector> MDC) {
  SmallVector<std::string, 4> HeaderMapFileNames;
  HS.getHeaderMapFileNames(HeaderMapFileNames);
  for (auto &Name : HeaderMapFileNames)
    MDC->addFile(Name);
}

static void collectIncludePCH(CompilerInstance &CI,
                              std::shared_ptr<ModuleDependencyCollector> MDC) {
  const PreprocessorOptions &PPOpts = CI.getPreprocessorOpts();
  if (PPOpts.ImplicitPCHInclude.empty())
    return;

  StringRef PCHInclude = PPOpts.ImplicitPCHInclude;
  FileManager &FileMgr = CI.getFileManager();
  const DirectoryEntry *PCHDir = FileMgr.getDirectory(PCHInclude);
  if (!PCHDir) {
    MDC->addFile(PCHInclude);
    return;
  }

  std::error_code EC;
  SmallString<128> DirNative;
  llvm::sys::path::native(PCHDir->getName(), DirNative);
  llvm::vfs::FileSystem &FS = *FileMgr.getVirtualFileSystem();
  SimpleASTReaderListener Validator(CI.getPreprocessor());
  for (llvm::vfs::directory_iterator Dir = FS.dir_begin(DirNative, EC), DirEnd;
       Dir != DirEnd && !EC; Dir.increment(EC)) {
    // Check whether this is an AST file. ASTReader::isAcceptableASTFile is not
    // used here since we're not interested in validating the PCH at this time,
    // but only to check whether this is a file containing an AST.
    if (!ASTReader::readASTFileControlBlock(
            Dir->path(), FileMgr, CI.getPCHContainerReader(),
            /*FindModuleFileExtensions=*/false, Validator,
            /*ValidateDiagnosticOptions=*/false))
      MDC->addFile(Dir->path());
  }
}

static void collectVFSEntries(CompilerInstance &CI,
                              std::shared_ptr<ModuleDependencyCollector> MDC) {
  if (CI.getHeaderSearchOpts().VFSOverlayFiles.empty())
    return;

  // Collect all VFS found.
  SmallVector<llvm::vfs::YAMLVFSEntry, 16> VFSEntries;
  for (const std::string &VFSFile : CI.getHeaderSearchOpts().VFSOverlayFiles) {
    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> Buffer =
        llvm::MemoryBuffer::getFile(VFSFile);
    if (!Buffer)
      return;
    llvm::vfs::collectVFSFromYAML(std::move(Buffer.get()),
                                  /*DiagHandler*/ nullptr, VFSFile, VFSEntries);
  }

  for (auto &E : VFSEntries)
    MDC->addFile(E.VPath, E.RPath);
}

// Diagnostics
static void SetUpDiagnosticLog(DiagnosticOptions *DiagOpts,
                               const CodeGenOptions *CodeGenOpts,
                               DiagnosticsEngine &Diags) {
  std::error_code EC;
  std::unique_ptr<raw_ostream> StreamOwner;
  raw_ostream *OS = &llvm::errs();
  if (DiagOpts->DiagnosticLogFile != "-") {
    // Create the output stream.
    auto FileOS = llvm::make_unique<llvm::raw_fd_ostream>(
        DiagOpts->DiagnosticLogFile, EC,
        llvm::sys::fs::F_Append | llvm::sys::fs::F_Text);
    if (EC) {
      Diags.Report(diag::warn_fe_cc_log_diagnostics_failure)
          << DiagOpts->DiagnosticLogFile << EC.message();
    } else {
      FileOS->SetUnbuffered();
      OS = FileOS.get();
      StreamOwner = std::move(FileOS);
    }
  }

  // Chain in the diagnostic client which will log the diagnostics.
  auto Logger = llvm::make_unique<LogDiagnosticPrinter>(*OS, DiagOpts,
                                                        std::move(StreamOwner));
  if (CodeGenOpts)
    Logger->setDwarfDebugFlags(CodeGenOpts->DwarfDebugFlags);
  assert(Diags.ownsClient());
  Diags.setClient(
      new ChainedDiagnosticConsumer(Diags.takeClient(), std::move(Logger)));
}

static void SetupSerializedDiagnostics(DiagnosticOptions *DiagOpts,
                                       DiagnosticsEngine &Diags,
                                       StringRef OutputFile) {
  auto SerializedConsumer =
      clang::serialized_diags::create(OutputFile, DiagOpts);

  if (Diags.ownsClient()) {
    Diags.setClient(new ChainedDiagnosticConsumer(
        Diags.takeClient(), std::move(SerializedConsumer)));
  } else {
    Diags.setClient(new ChainedDiagnosticConsumer(
        Diags.getClient(), std::move(SerializedConsumer)));
  }
}

void CompilerInstance::createDiagnostics(DiagnosticConsumer *Client,
                                         bool ShouldOwnClient) {
  Diagnostics = createDiagnostics(&getDiagnosticOpts(), Client,
                                  ShouldOwnClient, &getCodeGenOpts());
}

IntrusiveRefCntPtr<DiagnosticsEngine>
CompilerInstance::createDiagnostics(DiagnosticOptions *Opts,
                                    DiagnosticConsumer *Client,
                                    bool ShouldOwnClient,
                                    const CodeGenOptions *CodeGenOpts) {
  IntrusiveRefCntPtr<DiagnosticIDs> DiagID(new DiagnosticIDs());
  IntrusiveRefCntPtr<DiagnosticsEngine>
      Diags(new DiagnosticsEngine(DiagID, Opts));

  // Create the diagnostic client for reporting errors or for
  // implementing -verify.
  if (Client) {
    Diags->setClient(Client, ShouldOwnClient);
  } else
    Diags->setClient(new TextDiagnosticPrinter(llvm::errs(), Opts));

  // Chain in -verify checker, if requested.
  if (Opts->VerifyDiagnostics)
    Diags->setClient(new VerifyDiagnosticConsumer(*Diags));

  // Chain in -diagnostic-log-file dumper, if requested.
  if (!Opts->DiagnosticLogFile.empty())
    SetUpDiagnosticLog(Opts, CodeGenOpts, *Diags);

  if (!Opts->DiagnosticSerializationFile.empty())
    SetupSerializedDiagnostics(Opts, *Diags,
                               Opts->DiagnosticSerializationFile);

  // Configure our handling of diagnostics.
  ProcessWarningOptions(*Diags, *Opts);

  return Diags;
}

// File Manager

FileManager *CompilerInstance::createFileManager() {
  if (!hasVirtualFileSystem()) {
    IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS =
        createVFSFromCompilerInvocation(getInvocation(), getDiagnostics());
    setVirtualFileSystem(VFS);
  }
  FileMgr = new FileManager(getFileSystemOpts(), VirtualFileSystem);
  return FileMgr.get();
}

// Source Manager

void CompilerInstance::createSourceManager(FileManager &FileMgr) {
  SourceMgr = new SourceManager(getDiagnostics(), FileMgr);
}

// Initialize the remapping of files to alternative contents, e.g.,
// those specified through other files.
static void InitializeFileRemapping(DiagnosticsEngine &Diags,
                                    SourceManager &SourceMgr,
                                    FileManager &FileMgr,
                                    const PreprocessorOptions &InitOpts) {
  // Remap files in the source manager (with buffers).
  for (const auto &RB : InitOpts.RemappedFileBuffers) {
    // Create the file entry for the file that we're mapping from.
    const FileEntry *FromFile =
        FileMgr.getVirtualFile(RB.first, RB.second->getBufferSize(), 0);
    if (!FromFile) {
      Diags.Report(diag::err_fe_remap_missing_from_file) << RB.first;
      if (!InitOpts.RetainRemappedFileBuffers)
        delete RB.second;
      continue;
    }

    // Override the contents of the "from" file with the contents of
    // the "to" file.
    SourceMgr.overrideFileContents(FromFile, RB.second,
                                   InitOpts.RetainRemappedFileBuffers);
  }

  // Remap files in the source manager (with other files).
  for (const auto &RF : InitOpts.RemappedFiles) {
    // Find the file that we're mapping to.
    const FileEntry *ToFile = FileMgr.getFile(RF.second);
    if (!ToFile) {
      Diags.Report(diag::err_fe_remap_missing_to_file) << RF.first << RF.second;
      continue;
    }

    // Create the file entry for the file that we're mapping from.
    const FileEntry *FromFile =
        FileMgr.getVirtualFile(RF.first, ToFile->getSize(), 0);
    if (!FromFile) {
      Diags.Report(diag::err_fe_remap_missing_from_file) << RF.first;
      continue;
    }

    // Override the contents of the "from" file with the contents of
    // the "to" file.
    SourceMgr.overrideFileContents(FromFile, ToFile);
  }

  SourceMgr.setOverridenFilesKeepOriginalName(
      InitOpts.RemappedFilesKeepOriginalName);
}

// Preprocessor

void CompilerInstance::createPreprocessor(TranslationUnitKind TUKind) {
  const PreprocessorOptions &PPOpts = getPreprocessorOpts();

  // The module manager holds a reference to the old preprocessor (if any).
  ModuleManager.reset();

  // Create the Preprocessor.
  HeaderSearch *HeaderInfo =
      new HeaderSearch(getHeaderSearchOptsPtr(), getSourceManager(),
                       getDiagnostics(), getLangOpts(), &getTarget());
  PP = std::make_shared<Preprocessor>(
      Invocation->getPreprocessorOptsPtr(), getDiagnostics(), getLangOpts(),
      getSourceManager(), getPCMCache(), *HeaderInfo, *this,
      /*IdentifierInfoLookup=*/nullptr,
      /*OwnsHeaderSearch=*/true, TUKind);
  getTarget().adjust(getLangOpts());
  PP->Initialize(getTarget(), getAuxTarget());

  if (PPOpts.DetailedRecord)
    PP->createPreprocessingRecord();

  // Apply remappings to the source manager.
  InitializeFileRemapping(PP->getDiagnostics(), PP->getSourceManager(),
                          PP->getFileManager(), PPOpts);

  // Predefine macros and configure the preprocessor.
  InitializePreprocessor(*PP, PPOpts, getPCHContainerReader(),
                         getFrontendOpts());

  // Initialize the header search object.  In CUDA compilations, we use the aux
  // triple (the host triple) to initialize our header search, since we need to
  // find the host headers in order to compile the CUDA code.
  const llvm::Triple *HeaderSearchTriple = &PP->getTargetInfo().getTriple();
  if (PP->getTargetInfo().getTriple().getOS() == llvm::Triple::CUDA &&
      PP->getAuxTargetInfo())
    HeaderSearchTriple = &PP->getAuxTargetInfo()->getTriple();

  ApplyHeaderSearchOptions(PP->getHeaderSearchInfo(), getHeaderSearchOpts(),
                           PP->getLangOpts(), *HeaderSearchTriple);

  PP->setPreprocessedOutput(getPreprocessorOutputOpts().ShowCPP);

  if (PP->getLangOpts().Modules && PP->getLangOpts().ImplicitModules)
    PP->getHeaderSearchInfo().setModuleCachePath(getSpecificModuleCachePath());

  // Handle generating dependencies, if requested.
  const DependencyOutputOptions &DepOpts = getDependencyOutputOpts();
  if (!DepOpts.OutputFile.empty())
    TheDependencyFileGenerator.reset(
        DependencyFileGenerator::CreateAndAttachToPreprocessor(*PP, DepOpts));
  if (!DepOpts.DOTOutputFile.empty())
    AttachDependencyGraphGen(*PP, DepOpts.DOTOutputFile,
                             getHeaderSearchOpts().Sysroot);

  // If we don't have a collector, but we are collecting module dependencies,
  // then we're the top level compiler instance and need to create one.
  if (!ModuleDepCollector && !DepOpts.ModuleDependencyOutputDir.empty()) {
    ModuleDepCollector = std::make_shared<ModuleDependencyCollector>(
        DepOpts.ModuleDependencyOutputDir);
  }

  // If there is a module dep collector, register with other dep collectors
  // and also (a) collect header maps and (b) TODO: input vfs overlay files.
  if (ModuleDepCollector) {
    addDependencyCollector(ModuleDepCollector);
    collectHeaderMaps(PP->getHeaderSearchInfo(), ModuleDepCollector);
    collectIncludePCH(*this, ModuleDepCollector);
    collectVFSEntries(*this, ModuleDepCollector);
  }

  for (auto &Listener : DependencyCollectors)
    Listener->attachToPreprocessor(*PP);

  // Handle generating header include information, if requested.
  if (DepOpts.ShowHeaderIncludes)
    AttachHeaderIncludeGen(*PP, DepOpts);
  if (!DepOpts.HeaderIncludeOutputFile.empty()) {
    StringRef OutputPath = DepOpts.HeaderIncludeOutputFile;
    if (OutputPath == "-")
      OutputPath = "";
    AttachHeaderIncludeGen(*PP, DepOpts,
                           /*ShowAllHeaders=*/true, OutputPath,
                           /*ShowDepth=*/false);
  }

  if (DepOpts.ShowIncludesDest != ShowIncludesDestination::None) {
    AttachHeaderIncludeGen(*PP, DepOpts,
                           /*ShowAllHeaders=*/true, /*OutputPath=*/"",
                           /*ShowDepth=*/true, /*MSStyle=*/true);
  }
}

std::string CompilerInstance::getSpecificModuleCachePath() {
  // Set up the module path, including the hash for the
  // module-creation options.
  SmallString<256> SpecificModuleCache(getHeaderSearchOpts().ModuleCachePath);
  if (!SpecificModuleCache.empty() && !getHeaderSearchOpts().DisableModuleHash)
    llvm::sys::path::append(SpecificModuleCache,
                            getInvocation().getModuleHash());
  return SpecificModuleCache.str();
}

// ASTContext

void CompilerInstance::createASTContext() {
  Preprocessor &PP = getPreprocessor();
  auto *Context = new ASTContext(getLangOpts(), PP.getSourceManager(),
                                 PP.getIdentifierTable(), PP.getSelectorTable(),
                                 PP.getBuiltinInfo());
  Context->InitBuiltinTypes(getTarget(), getAuxTarget());
  setASTContext(Context);
}

// ExternalASTSource

void CompilerInstance::createPCHExternalASTSource(
    StringRef Path, bool DisablePCHValidation, bool AllowPCHWithCompilerErrors,
    void *DeserializationListener, bool OwnDeserializationListener) {
  bool Preamble = getPreprocessorOpts().PrecompiledPreambleBytes.first != 0;
  ModuleManager = createPCHExternalASTSource(
      Path, getHeaderSearchOpts().Sysroot, DisablePCHValidation,
      AllowPCHWithCompilerErrors, getPreprocessor(), getASTContext(),
      getPCHContainerReader(),
      getFrontendOpts().ModuleFileExtensions,
      TheDependencyFileGenerator.get(),
      DependencyCollectors,
      DeserializationListener,
      OwnDeserializationListener, Preamble,
      getFrontendOpts().UseGlobalModuleIndex);
}

IntrusiveRefCntPtr<ASTReader> CompilerInstance::createPCHExternalASTSource(
    StringRef Path, StringRef Sysroot, bool DisablePCHValidation,
    bool AllowPCHWithCompilerErrors, Preprocessor &PP, ASTContext &Context,
    const PCHContainerReader &PCHContainerRdr,
    ArrayRef<std::shared_ptr<ModuleFileExtension>> Extensions,
    DependencyFileGenerator *DependencyFile,
    ArrayRef<std::shared_ptr<DependencyCollector>> DependencyCollectors,
    void *DeserializationListener, bool OwnDeserializationListener,
    bool Preamble, bool UseGlobalModuleIndex) {
  HeaderSearchOptions &HSOpts = PP.getHeaderSearchInfo().getHeaderSearchOpts();

  IntrusiveRefCntPtr<ASTReader> Reader(new ASTReader(
      PP, &Context, PCHContainerRdr, Extensions,
      Sysroot.empty() ? "" : Sysroot.data(), DisablePCHValidation,
      AllowPCHWithCompilerErrors, /*AllowConfigurationMismatch*/ false,
      HSOpts.ModulesValidateSystemHeaders, UseGlobalModuleIndex));

  // We need the external source to be set up before we read the AST, because
  // eagerly-deserialized declarations may use it.
  Context.setExternalSource(Reader.get());

  Reader->setDeserializationListener(
      static_cast<ASTDeserializationListener *>(DeserializationListener),
      /*TakeOwnership=*/OwnDeserializationListener);

  if (DependencyFile)
    DependencyFile->AttachToASTReader(*Reader);
  for (auto &Listener : DependencyCollectors)
    Listener->attachToASTReader(*Reader);

  switch (Reader->ReadAST(Path,
                          Preamble ? serialization::MK_Preamble
                                   : serialization::MK_PCH,
                          SourceLocation(),
                          ASTReader::ARR_None)) {
  case ASTReader::Success:
    // Set the predefines buffer as suggested by the PCH reader. Typically, the
    // predefines buffer will be empty.
    PP.setPredefines(Reader->getSuggestedPredefines());
    return Reader;

  case ASTReader::Failure:
    // Unrecoverable failure: don't even try to process the input file.
    break;

  case ASTReader::Missing:
  case ASTReader::OutOfDate:
  case ASTReader::VersionMismatch:
  case ASTReader::ConfigurationMismatch:
  case ASTReader::HadErrors:
    // No suitable PCH file could be found. Return an error.
    break;
  }

  Context.setExternalSource(nullptr);
  return nullptr;
}

// Code Completion

static bool EnableCodeCompletion(Preprocessor &PP,
                                 StringRef Filename,
                                 unsigned Line,
                                 unsigned Column) {
  // Tell the source manager to chop off the given file at a specific
  // line and column.
  const FileEntry *Entry = PP.getFileManager().getFile(Filename);
  if (!Entry) {
    PP.getDiagnostics().Report(diag::err_fe_invalid_code_complete_file)
      << Filename;
    return true;
  }

  // Truncate the named file at the given line/column.
  PP.SetCodeCompletionPoint(Entry, Line, Column);
  return false;
}

void CompilerInstance::createCodeCompletionConsumer() {
  const ParsedSourceLocation &Loc = getFrontendOpts().CodeCompletionAt;
  if (!CompletionConsumer) {
    setCodeCompletionConsumer(
      createCodeCompletionConsumer(getPreprocessor(),
                                   Loc.FileName, Loc.Line, Loc.Column,
                                   getFrontendOpts().CodeCompleteOpts,
                                   llvm::outs()));
    if (!CompletionConsumer)
      return;
  } else if (EnableCodeCompletion(getPreprocessor(), Loc.FileName,
                                  Loc.Line, Loc.Column)) {
    setCodeCompletionConsumer(nullptr);
    return;
  }

  if (CompletionConsumer->isOutputBinary() &&
      llvm::sys::ChangeStdoutToBinary()) {
    getPreprocessor().getDiagnostics().Report(diag::err_fe_stdout_binary);
    setCodeCompletionConsumer(nullptr);
  }
}

void CompilerInstance::createFrontendTimer() {
  FrontendTimerGroup.reset(
      new llvm::TimerGroup("frontend", "Clang front-end time report"));
  FrontendTimer.reset(
      new llvm::Timer("frontend", "Clang front-end timer",
                      *FrontendTimerGroup));
}

CodeCompleteConsumer *
CompilerInstance::createCodeCompletionConsumer(Preprocessor &PP,
                                               StringRef Filename,
                                               unsigned Line,
                                               unsigned Column,
                                               const CodeCompleteOptions &Opts,
                                               raw_ostream &OS) {
  if (EnableCodeCompletion(PP, Filename, Line, Column))
    return nullptr;

  // Set up the creation routine for code-completion.
  return new PrintingCodeCompleteConsumer(Opts, OS);
}

void CompilerInstance::createSema(TranslationUnitKind TUKind,
                                  CodeCompleteConsumer *CompletionConsumer) {
  TheSema.reset(new Sema(getPreprocessor(), getASTContext(), getASTConsumer(),
                         TUKind, CompletionConsumer));
  // Attach the external sema source if there is any.
  if (ExternalSemaSrc) {
    TheSema->addExternalSource(ExternalSemaSrc.get());
    ExternalSemaSrc->InitializeSema(*TheSema);
  }
}

// Output Files

void CompilerInstance::addOutputFile(OutputFile &&OutFile) {
  OutputFiles.push_back(std::move(OutFile));
}

void CompilerInstance::clearOutputFiles(bool EraseFiles) {
  for (OutputFile &OF : OutputFiles) {
    if (!OF.TempFilename.empty()) {
      if (EraseFiles) {
        llvm::sys::fs::remove(OF.TempFilename);
      } else {
        SmallString<128> NewOutFile(OF.Filename);

        // If '-working-directory' was passed, the output filename should be
        // relative to that.
        FileMgr->FixupRelativePath(NewOutFile);
        if (std::error_code ec =
                llvm::sys::fs::rename(OF.TempFilename, NewOutFile)) {
          getDiagnostics().Report(diag::err_unable_to_rename_temp)
            << OF.TempFilename << OF.Filename << ec.message();

          llvm::sys::fs::remove(OF.TempFilename);
        }
      }
    } else if (!OF.Filename.empty() && EraseFiles)
      llvm::sys::fs::remove(OF.Filename);
  }
  OutputFiles.clear();
  if (DeleteBuiltModules) {
    for (auto &Module : BuiltModules)
      llvm::sys::fs::remove(Module.second);
    BuiltModules.clear();
  }
  NonSeekStream.reset();
}

std::unique_ptr<raw_pwrite_stream>
CompilerInstance::createDefaultOutputFile(bool Binary, StringRef InFile,
                                          StringRef Extension) {
  return createOutputFile(getFrontendOpts().OutputFile, Binary,
                          /*RemoveFileOnSignal=*/true, InFile, Extension,
                          /*UseTemporary=*/true);
}

std::unique_ptr<raw_pwrite_stream> CompilerInstance::createNullOutputFile() {
  return llvm::make_unique<llvm::raw_null_ostream>();
}

std::unique_ptr<raw_pwrite_stream>
CompilerInstance::createOutputFile(StringRef OutputPath, bool Binary,
                                   bool RemoveFileOnSignal, StringRef InFile,
                                   StringRef Extension, bool UseTemporary,
                                   bool CreateMissingDirectories) {
  std::string OutputPathName, TempPathName;
  std::error_code EC;
  std::unique_ptr<raw_pwrite_stream> OS = createOutputFile(
      OutputPath, EC, Binary, RemoveFileOnSignal, InFile, Extension,
      UseTemporary, CreateMissingDirectories, &OutputPathName, &TempPathName);
  if (!OS) {
    getDiagnostics().Report(diag::err_fe_unable_to_open_output) << OutputPath
                                                                << EC.message();
    return nullptr;
  }

  // Add the output file -- but don't try to remove "-", since this means we are
  // using stdin.
  addOutputFile(
      OutputFile((OutputPathName != "-") ? OutputPathName : "", TempPathName));

  return OS;
}

std::unique_ptr<llvm::raw_pwrite_stream> CompilerInstance::createOutputFile(
    StringRef OutputPath, std::error_code &Error, bool Binary,
    bool RemoveFileOnSignal, StringRef InFile, StringRef Extension,
    bool UseTemporary, bool CreateMissingDirectories,
    std::string *ResultPathName, std::string *TempPathName) {
  assert((!CreateMissingDirectories || UseTemporary) &&
         "CreateMissingDirectories is only allowed when using temporary files");

  std::string OutFile, TempFile;
  if (!OutputPath.empty()) {
    OutFile = OutputPath;
  } else if (InFile == "-") {
    OutFile = "-";
  } else if (!Extension.empty()) {
    SmallString<128> Path(InFile);
    llvm::sys::path::replace_extension(Path, Extension);
    OutFile = Path.str();
  } else {
    OutFile = "-";
  }

  std::unique_ptr<llvm::raw_fd_ostream> OS;
  std::string OSFile;

  if (UseTemporary) {
    if (OutFile == "-")
      UseTemporary = false;
    else {
      llvm::sys::fs::file_status Status;
      llvm::sys::fs::status(OutputPath, Status);
      if (llvm::sys::fs::exists(Status)) {
        // Fail early if we can't write to the final destination.
        if (!llvm::sys::fs::can_write(OutputPath)) {
          Error = make_error_code(llvm::errc::operation_not_permitted);
          return nullptr;
        }

        // Don't use a temporary if the output is a special file. This handles
        // things like '-o /dev/null'
        if (!llvm::sys::fs::is_regular_file(Status))
          UseTemporary = false;
      }
    }
  }

  if (UseTemporary) {
    // Create a temporary file.
    // Insert -%%%%%%%% before the extension (if any), and because some tools
    // (noticeable, clang's own GlobalModuleIndex.cpp) glob for build
    // artifacts, also append .tmp.
    StringRef OutputExtension = llvm::sys::path::extension(OutFile);
    SmallString<128> TempPath =
        StringRef(OutFile).drop_back(OutputExtension.size());
    TempPath += "-%%%%%%%%";
    TempPath += OutputExtension;
    TempPath += ".tmp";
    int fd;
    std::error_code EC =
        llvm::sys::fs::createUniqueFile(TempPath, fd, TempPath);

    if (CreateMissingDirectories &&
        EC == llvm::errc::no_such_file_or_directory) {
      StringRef Parent = llvm::sys::path::parent_path(OutputPath);
      EC = llvm::sys::fs::create_directories(Parent);
      if (!EC) {
        EC = llvm::sys::fs::createUniqueFile(TempPath, fd, TempPath);
      }
    }

    if (!EC) {
      OS.reset(new llvm::raw_fd_ostream(fd, /*shouldClose=*/true));
      OSFile = TempFile = TempPath.str();
    }
    // If we failed to create the temporary, fallback to writing to the file
    // directly. This handles the corner case where we cannot write to the
    // directory, but can write to the file.
  }

  if (!OS) {
    OSFile = OutFile;
    OS.reset(new llvm::raw_fd_ostream(
        OSFile, Error,
        (Binary ? llvm::sys::fs::F_None : llvm::sys::fs::F_Text)));
    if (Error)
      return nullptr;
  }

  // Make sure the out stream file gets removed if we crash.
  if (RemoveFileOnSignal)
    llvm::sys::RemoveFileOnSignal(OSFile);

  if (ResultPathName)
    *ResultPathName = OutFile;
  if (TempPathName)
    *TempPathName = TempFile;

  if (!Binary || OS->supportsSeeking())
    return std::move(OS);

  auto B = llvm::make_unique<llvm::buffer_ostream>(*OS);
  assert(!NonSeekStream);
  NonSeekStream = std::move(OS);
  return std::move(B);
}

// Initialization Utilities

bool CompilerInstance::InitializeSourceManager(const FrontendInputFile &Input){
  return InitializeSourceManager(
      Input, getDiagnostics(), getFileManager(), getSourceManager(),
      hasPreprocessor() ? &getPreprocessor().getHeaderSearchInfo() : nullptr,
      getDependencyOutputOpts(), getFrontendOpts());
}

// static
bool CompilerInstance::InitializeSourceManager(
    const FrontendInputFile &Input, DiagnosticsEngine &Diags,
    FileManager &FileMgr, SourceManager &SourceMgr, HeaderSearch *HS,
    DependencyOutputOptions &DepOpts, const FrontendOptions &Opts) {
  SrcMgr::CharacteristicKind Kind =
      Input.getKind().getFormat() == InputKind::ModuleMap
          ? Input.isSystem() ? SrcMgr::C_System_ModuleMap
                             : SrcMgr::C_User_ModuleMap
          : Input.isSystem() ? SrcMgr::C_System : SrcMgr::C_User;

  if (Input.isBuffer()) {
    SourceMgr.setMainFileID(SourceMgr.createFileID(SourceManager::Unowned,
                                                   Input.getBuffer(), Kind));
    assert(SourceMgr.getMainFileID().isValid() &&
           "Couldn't establish MainFileID!");
    return true;
  }

  StringRef InputFile = Input.getFile();

  // Figure out where to get and map in the main file.
  if (InputFile != "-") {
    const FileEntry *File = FileMgr.getFile(InputFile, /*OpenFile=*/true);
    if (!File) {
      Diags.Report(diag::err_fe_error_reading) << InputFile;
      return false;
    }

    // The natural SourceManager infrastructure can't currently handle named
    // pipes, but we would at least like to accept them for the main
    // file. Detect them here, read them with the volatile flag so FileMgr will
    // pick up the correct size, and simply override their contents as we do for
    // STDIN.
    if (File->isNamedPipe()) {
      auto MB = FileMgr.getBufferForFile(File, /*isVolatile=*/true);
      if (MB) {
        // Create a new virtual file that will have the correct size.
        File = FileMgr.getVirtualFile(InputFile, (*MB)->getBufferSize(), 0);
        SourceMgr.overrideFileContents(File, std::move(*MB));
      } else {
        Diags.Report(diag::err_cannot_open_file) << InputFile
                                                 << MB.getError().message();
        return false;
      }
    }

    SourceMgr.setMainFileID(
        SourceMgr.createFileID(File, SourceLocation(), Kind));
  } else {
    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> SBOrErr =
        llvm::MemoryBuffer::getSTDIN();
    if (std::error_code EC = SBOrErr.getError()) {
      Diags.Report(diag::err_fe_error_reading_stdin) << EC.message();
      return false;
    }
    std::unique_ptr<llvm::MemoryBuffer> SB = std::move(SBOrErr.get());

    const FileEntry *File = FileMgr.getVirtualFile(SB->getBufferIdentifier(),
                                                   SB->getBufferSize(), 0);
    SourceMgr.setMainFileID(
        SourceMgr.createFileID(File, SourceLocation(), Kind));
    SourceMgr.overrideFileContents(File, std::move(SB));
  }

  assert(SourceMgr.getMainFileID().isValid() &&
         "Couldn't establish MainFileID!");
  return true;
}

// High-Level Operations

bool CompilerInstance::ExecuteAction(FrontendAction &Act) {
  assert(hasDiagnostics() && "Diagnostics engine is not initialized!");
  assert(!getFrontendOpts().ShowHelp && "Client must handle '-help'!");
  assert(!getFrontendOpts().ShowVersion && "Client must handle '-version'!");

  // FIXME: Take this as an argument, once all the APIs we used have moved to
  // taking it as an input instead of hard-coding llvm::errs.
  raw_ostream &OS = llvm::errs();

  if (!Act.PrepareToExecute(*this))
    return false;

  // Create the target instance.
  setTarget(TargetInfo::CreateTargetInfo(getDiagnostics(),
                                         getInvocation().TargetOpts));
  if (!hasTarget())
    return false;

  // Create TargetInfo for the other side of CUDA and OpenMP compilation.
  if ((getLangOpts().CUDA || getLangOpts().OpenMPIsDevice) &&
      !getFrontendOpts().AuxTriple.empty()) {
    auto TO = std::make_shared<TargetOptions>();
    TO->Triple = llvm::Triple::normalize(getFrontendOpts().AuxTriple);
    TO->HostTriple = getTarget().getTriple().str();
    setAuxTarget(TargetInfo::CreateTargetInfo(getDiagnostics(), TO));
  }

  // Inform the target of the language options.
  //
  // FIXME: We shouldn't need to do this, the target should be immutable once
  // created. This complexity should be lifted elsewhere.
  getTarget().adjust(getLangOpts());

  // Adjust target options based on codegen options.
  getTarget().adjustTargetOptions(getCodeGenOpts(), getTargetOpts());

  // rewriter project will change target built-in bool type from its default.
  if (getFrontendOpts().ProgramAction == frontend::RewriteObjC)
    getTarget().noSignedCharForObjCBool();

  // Validate/process some options.
  if (getHeaderSearchOpts().Verbose)
    OS << "clang -cc1 version " CLANG_VERSION_STRING
       << " based upon " << BACKEND_PACKAGE_STRING
       << " default target " << llvm::sys::getDefaultTargetTriple() << "\n";

  if (getFrontendOpts().ShowTimers)
    createFrontendTimer();

  if (getFrontendOpts().ShowStats || !getFrontendOpts().StatsFile.empty())
    llvm::EnableStatistics(false);

  for (const FrontendInputFile &FIF : getFrontendOpts().Inputs) {
    // Reset the ID tables if we are reusing the SourceManager and parsing
    // regular files.
    if (hasSourceManager() && !Act.isModelParsingAction())
      getSourceManager().clearIDTables();

    if (Act.BeginSourceFile(*this, FIF)) {
      Act.Execute();
      Act.EndSourceFile();
    }
  }

  // Notify the diagnostic client that all files were processed.
  getDiagnostics().getClient()->finish();

  if (getDiagnosticOpts().ShowCarets) {
    // We can have multiple diagnostics sharing one diagnostic client.
    // Get the total number of warnings/errors from the client.
    unsigned NumWarnings = getDiagnostics().getClient()->getNumWarnings();
    unsigned NumErrors = getDiagnostics().getClient()->getNumErrors();

    if (NumWarnings)
      OS << NumWarnings << " warning" << (NumWarnings == 1 ? "" : "s");
    if (NumWarnings && NumErrors)
      OS << " and ";
    if (NumErrors)
      OS << NumErrors << " error" << (NumErrors == 1 ? "" : "s");
    if (NumWarnings || NumErrors) {
      OS << " generated";
      if (getLangOpts().CUDA) {
        if (!getLangOpts().CUDAIsDevice) {
          OS << " when compiling for host";
        } else {
          OS << " when compiling for " << getTargetOpts().CPU;
        }
      }
      OS << ".\n";
    }
  }

  if (getFrontendOpts().ShowStats) {
    if (hasFileManager()) {
      getFileManager().PrintStats();
      OS << '\n';
    }
    llvm::PrintStatistics(OS);
  }
  StringRef StatsFile = getFrontendOpts().StatsFile;
  if (!StatsFile.empty()) {
    std::error_code EC;
    auto StatS = llvm::make_unique<llvm::raw_fd_ostream>(StatsFile, EC,
                                                         llvm::sys::fs::F_Text);
    if (EC) {
      getDiagnostics().Report(diag::warn_fe_unable_to_open_stats_file)
          << StatsFile << EC.message();
    } else {
      llvm::PrintStatisticsJSON(*StatS);
    }
  }

  return !getDiagnostics().getClient()->getNumErrors();
}

/// Determine the appropriate source input kind based on language
/// options.
static InputKind::Language getLanguageFromOptions(const LangOptions &LangOpts) {
  if (LangOpts.OpenCL)
    return InputKind::OpenCL;
  if (LangOpts.CUDA)
    return InputKind::CUDA;
  if (LangOpts.ObjC)
    return LangOpts.CPlusPlus ? InputKind::ObjCXX : InputKind::ObjC;
  return LangOpts.CPlusPlus ? InputKind::CXX : InputKind::C;
}

/// Compile a module file for the given module, using the options
/// provided by the importing compiler instance. Returns true if the module
/// was built without errors.
static bool
compileModuleImpl(CompilerInstance &ImportingInstance, SourceLocation ImportLoc,
                  StringRef ModuleName, FrontendInputFile Input,
                  StringRef OriginalModuleMapFile, StringRef ModuleFileName,
                  llvm::function_ref<void(CompilerInstance &)> PreBuildStep =
                      [](CompilerInstance &) {},
                  llvm::function_ref<void(CompilerInstance &)> PostBuildStep =
                      [](CompilerInstance &) {}) {
  // Construct a compiler invocation for creating this module.
  auto Invocation =
      std::make_shared<CompilerInvocation>(ImportingInstance.getInvocation());

  PreprocessorOptions &PPOpts = Invocation->getPreprocessorOpts();

  // For any options that aren't intended to affect how a module is built,
  // reset them to their default values.
  Invocation->getLangOpts()->resetNonModularOptions();
  PPOpts.resetNonModularOptions();

  // Remove any macro definitions that are explicitly ignored by the module.
  // They aren't supposed to affect how the module is built anyway.
  HeaderSearchOptions &HSOpts = Invocation->getHeaderSearchOpts();
  PPOpts.Macros.erase(
      std::remove_if(PPOpts.Macros.begin(), PPOpts.Macros.end(),
                     [&HSOpts](const std::pair<std::string, bool> &def) {
        StringRef MacroDef = def.first;
        return HSOpts.ModulesIgnoreMacros.count(
                   llvm::CachedHashString(MacroDef.split('=').first)) > 0;
      }),
      PPOpts.Macros.end());

  // If the original compiler invocation had -fmodule-name, pass it through.
  Invocation->getLangOpts()->ModuleName =
      ImportingInstance.getInvocation().getLangOpts()->ModuleName;

  // Note the name of the module we're building.
  Invocation->getLangOpts()->CurrentModule = ModuleName;

  // Make sure that the failed-module structure has been allocated in
  // the importing instance, and propagate the pointer to the newly-created
  // instance.
  PreprocessorOptions &ImportingPPOpts
    = ImportingInstance.getInvocation().getPreprocessorOpts();
  if (!ImportingPPOpts.FailedModules)
    ImportingPPOpts.FailedModules =
        std::make_shared<PreprocessorOptions::FailedModulesSet>();
  PPOpts.FailedModules = ImportingPPOpts.FailedModules;

  // If there is a module map file, build the module using the module map.
  // Set up the inputs/outputs so that we build the module from its umbrella
  // header.
  FrontendOptions &FrontendOpts = Invocation->getFrontendOpts();
  FrontendOpts.OutputFile = ModuleFileName.str();
  FrontendOpts.DisableFree = false;
  FrontendOpts.GenerateGlobalModuleIndex = false;
  FrontendOpts.BuildingImplicitModule = true;
  FrontendOpts.OriginalModuleMap = OriginalModuleMapFile;
  // Force implicitly-built modules to hash the content of the module file.
  HSOpts.ModulesHashContent = true;
  FrontendOpts.Inputs = {Input};

  // Don't free the remapped file buffers; they are owned by our caller.
  PPOpts.RetainRemappedFileBuffers = true;

  Invocation->getDiagnosticOpts().VerifyDiagnostics = 0;
  assert(ImportingInstance.getInvocation().getModuleHash() ==
         Invocation->getModuleHash() && "Module hash mismatch!");

  // Construct a compiler instance that will be used to actually create the
  // module.  Since we're sharing a PCMCache,
  // CompilerInstance::CompilerInstance is responsible for finalizing the
  // buffers to prevent use-after-frees.
  CompilerInstance Instance(ImportingInstance.getPCHContainerOperations(),
                            &ImportingInstance.getPreprocessor().getPCMCache());
  auto &Inv = *Invocation;
  Instance.setInvocation(std::move(Invocation));

  Instance.createDiagnostics(new ForwardingDiagnosticConsumer(
                                   ImportingInstance.getDiagnosticClient()),
                             /*ShouldOwnClient=*/true);

  Instance.setVirtualFileSystem(&ImportingInstance.getVirtualFileSystem());

  // Note that this module is part of the module build stack, so that we
  // can detect cycles in the module graph.
  Instance.setFileManager(&ImportingInstance.getFileManager());
  Instance.createSourceManager(Instance.getFileManager());
  SourceManager &SourceMgr = Instance.getSourceManager();
  SourceMgr.setModuleBuildStack(
    ImportingInstance.getSourceManager().getModuleBuildStack());
  SourceMgr.pushModuleBuildStack(ModuleName,
    FullSourceLoc(ImportLoc, ImportingInstance.getSourceManager()));

  // If we're collecting module dependencies, we need to share a collector
  // between all of the module CompilerInstances. Other than that, we don't
  // want to produce any dependency output from the module build.
  Instance.setModuleDepCollector(ImportingInstance.getModuleDepCollector());
  Inv.getDependencyOutputOpts() = DependencyOutputOptions();

  ImportingInstance.getDiagnostics().Report(ImportLoc,
                                            diag::remark_module_build)
    << ModuleName << ModuleFileName;

  PreBuildStep(Instance);

  // Execute the action to actually build the module in-place. Use a separate
  // thread so that we get a stack large enough.
  llvm::CrashRecoveryContext CRC;
  CRC.RunSafelyOnThread(
      [&]() {
        GenerateModuleFromModuleMapAction Action;
        Instance.ExecuteAction(Action);
      },
      DesiredStackSize);

  PostBuildStep(Instance);

  ImportingInstance.getDiagnostics().Report(ImportLoc,
                                            diag::remark_module_build_done)
    << ModuleName;

  // Delete the temporary module map file.
  // FIXME: Even though we're executing under crash protection, it would still
  // be nice to do this with RemoveFileOnSignal when we can. However, that
  // doesn't make sense for all clients, so clean this up manually.
  Instance.clearOutputFiles(/*EraseFiles=*/true);

  return !Instance.getDiagnostics().hasErrorOccurred();
}

static const FileEntry *getPublicModuleMap(const FileEntry *File,
                                           FileManager &FileMgr) {
  StringRef Filename = llvm::sys::path::filename(File->getName());
  SmallString<128> PublicFilename(File->getDir()->getName());
  if (Filename == "module_private.map")
    llvm::sys::path::append(PublicFilename, "module.map");
  else if (Filename == "module.private.modulemap")
    llvm::sys::path::append(PublicFilename, "module.modulemap");
  else
    return nullptr;
  return FileMgr.getFile(PublicFilename);
}

/// Compile a module file for the given module, using the options
/// provided by the importing compiler instance. Returns true if the module
/// was built without errors.
static bool compileModuleImpl(CompilerInstance &ImportingInstance,
                              SourceLocation ImportLoc,
                              Module *Module,
                              StringRef ModuleFileName) {
  InputKind IK(getLanguageFromOptions(ImportingInstance.getLangOpts()),
               InputKind::ModuleMap);

  // Get or create the module map that we'll use to build this module.
  ModuleMap &ModMap
    = ImportingInstance.getPreprocessor().getHeaderSearchInfo().getModuleMap();
  bool Result;
  if (const FileEntry *ModuleMapFile =
          ModMap.getContainingModuleMapFile(Module)) {
    // Canonicalize compilation to start with the public module map. This is
    // vital for submodules declarations in the private module maps to be
    // correctly parsed when depending on a top level module in the public one.
    if (const FileEntry *PublicMMFile = getPublicModuleMap(
            ModuleMapFile, ImportingInstance.getFileManager()))
      ModuleMapFile = PublicMMFile;

    // Use the module map where this module resides.
    Result = compileModuleImpl(
        ImportingInstance, ImportLoc, Module->getTopLevelModuleName(),
        FrontendInputFile(ModuleMapFile->getName(), IK, +Module->IsSystem),
        ModMap.getModuleMapFileForUniquing(Module)->getName(),
        ModuleFileName);
  } else {
    // FIXME: We only need to fake up an input file here as a way of
    // transporting the module's directory to the module map parser. We should
    // be able to do that more directly, and parse from a memory buffer without
    // inventing this file.
    SmallString<128> FakeModuleMapFile(Module->Directory->getName());
    llvm::sys::path::append(FakeModuleMapFile, "__inferred_module.map");

    std::string InferredModuleMapContent;
    llvm::raw_string_ostream OS(InferredModuleMapContent);
    Module->print(OS);
    OS.flush();

    Result = compileModuleImpl(
        ImportingInstance, ImportLoc, Module->getTopLevelModuleName(),
        FrontendInputFile(FakeModuleMapFile, IK, +Module->IsSystem),
        ModMap.getModuleMapFileForUniquing(Module)->getName(),
        ModuleFileName,
        [&](CompilerInstance &Instance) {
      std::unique_ptr<llvm::MemoryBuffer> ModuleMapBuffer =
          llvm::MemoryBuffer::getMemBuffer(InferredModuleMapContent);
      ModuleMapFile = Instance.getFileManager().getVirtualFile(
          FakeModuleMapFile, InferredModuleMapContent.size(), 0);
      Instance.getSourceManager().overrideFileContents(
          ModuleMapFile, std::move(ModuleMapBuffer));
    });
  }

  // We've rebuilt a module. If we're allowed to generate or update the global
  // module index, record that fact in the importing compiler instance.
  if (ImportingInstance.getFrontendOpts().GenerateGlobalModuleIndex) {
    ImportingInstance.setBuildGlobalModuleIndex(true);
  }

  return Result;
}

static bool compileAndLoadModule(CompilerInstance &ImportingInstance,
                                 SourceLocation ImportLoc,
                                 SourceLocation ModuleNameLoc, Module *Module,
                                 StringRef ModuleFileName) {
  DiagnosticsEngine &Diags = ImportingInstance.getDiagnostics();

  auto diagnoseBuildFailure = [&] {
    Diags.Report(ModuleNameLoc, diag::err_module_not_built)
        << Module->Name << SourceRange(ImportLoc, ModuleNameLoc);
  };

  // FIXME: have LockFileManager return an error_code so that we can
  // avoid the mkdir when the directory already exists.
  StringRef Dir = llvm::sys::path::parent_path(ModuleFileName);
  llvm::sys::fs::create_directories(Dir);

  while (1) {
    unsigned ModuleLoadCapabilities = ASTReader::ARR_Missing;
    llvm::LockFileManager Locked(ModuleFileName);
    switch (Locked) {
    case llvm::LockFileManager::LFS_Error:
      // PCMCache takes care of correctness and locks are only necessary for
      // performance. Fallback to building the module in case of any lock
      // related errors.
      Diags.Report(ModuleNameLoc, diag::remark_module_lock_failure)
          << Module->Name << Locked.getErrorMessage();
      // Clear out any potential leftover.
      Locked.unsafeRemoveLockFile();
      LLVM_FALLTHROUGH;
    case llvm::LockFileManager::LFS_Owned:
      // We're responsible for building the module ourselves.
      if (!compileModuleImpl(ImportingInstance, ModuleNameLoc, Module,
                             ModuleFileName)) {
        diagnoseBuildFailure();
        return false;
      }
      break;

    case llvm::LockFileManager::LFS_Shared:
      // Someone else is responsible for building the module. Wait for them to
      // finish.
      switch (Locked.waitForUnlock()) {
      case llvm::LockFileManager::Res_Success:
        ModuleLoadCapabilities |= ASTReader::ARR_OutOfDate;
        break;
      case llvm::LockFileManager::Res_OwnerDied:
        continue; // try again to get the lock.
      case llvm::LockFileManager::Res_Timeout:
        // Since PCMCache takes care of correctness, we try waiting for another
        // process to complete the build so clang does not do it done twice. If
        // case of timeout, build it ourselves.
        Diags.Report(ModuleNameLoc, diag::remark_module_lock_timeout)
            << Module->Name;
        // Clear the lock file so that future invocations can make progress.
        Locked.unsafeRemoveLockFile();
        continue;
      }
      break;
    }

    // Try to read the module file, now that we've compiled it.
    ASTReader::ASTReadResult ReadResult =
        ImportingInstance.getModuleManager()->ReadAST(
            ModuleFileName, serialization::MK_ImplicitModule, ImportLoc,
            ModuleLoadCapabilities);

    if (ReadResult == ASTReader::OutOfDate &&
        Locked == llvm::LockFileManager::LFS_Shared) {
      // The module may be out of date in the presence of file system races,
      // or if one of its imports depends on header search paths that are not
      // consistent with this ImportingInstance.  Try again...
      continue;
    } else if (ReadResult == ASTReader::Missing) {
      diagnoseBuildFailure();
    } else if (ReadResult != ASTReader::Success &&
               !Diags.hasErrorOccurred()) {
      // The ASTReader didn't diagnose the error, so conservatively report it.
      diagnoseBuildFailure();
    }
    return ReadResult == ASTReader::Success;
  }
}

/// Diagnose differences between the current definition of the given
/// configuration macro and the definition provided on the command line.
static void checkConfigMacro(Preprocessor &PP, StringRef ConfigMacro,
                             Module *Mod, SourceLocation ImportLoc) {
  IdentifierInfo *Id = PP.getIdentifierInfo(ConfigMacro);
  SourceManager &SourceMgr = PP.getSourceManager();

  // If this identifier has never had a macro definition, then it could
  // not have changed.
  if (!Id->hadMacroDefinition())
    return;
  auto *LatestLocalMD = PP.getLocalMacroDirectiveHistory(Id);

  // Find the macro definition from the command line.
  MacroInfo *CmdLineDefinition = nullptr;
  for (auto *MD = LatestLocalMD; MD; MD = MD->getPrevious()) {
    // We only care about the predefines buffer.
    FileID FID = SourceMgr.getFileID(MD->getLocation());
    if (FID.isInvalid() || FID != PP.getPredefinesFileID())
      continue;
    if (auto *DMD = dyn_cast<DefMacroDirective>(MD))
      CmdLineDefinition = DMD->getMacroInfo();
    break;
  }

  auto *CurrentDefinition = PP.getMacroInfo(Id);
  if (CurrentDefinition == CmdLineDefinition) {
    // Macro matches. Nothing to do.
  } else if (!CurrentDefinition) {
    // This macro was defined on the command line, then #undef'd later.
    // Complain.
    PP.Diag(ImportLoc, diag::warn_module_config_macro_undef)
      << true << ConfigMacro << Mod->getFullModuleName();
    auto LatestDef = LatestLocalMD->getDefinition();
    assert(LatestDef.isUndefined() &&
           "predefined macro went away with no #undef?");
    PP.Diag(LatestDef.getUndefLocation(), diag::note_module_def_undef_here)
      << true;
    return;
  } else if (!CmdLineDefinition) {
    // There was no definition for this macro in the predefines buffer,
    // but there was a local definition. Complain.
    PP.Diag(ImportLoc, diag::warn_module_config_macro_undef)
      << false << ConfigMacro << Mod->getFullModuleName();
    PP.Diag(CurrentDefinition->getDefinitionLoc(),
            diag::note_module_def_undef_here)
      << false;
  } else if (!CurrentDefinition->isIdenticalTo(*CmdLineDefinition, PP,
                                               /*Syntactically=*/true)) {
    // The macro definitions differ.
    PP.Diag(ImportLoc, diag::warn_module_config_macro_undef)
      << false << ConfigMacro << Mod->getFullModuleName();
    PP.Diag(CurrentDefinition->getDefinitionLoc(),
            diag::note_module_def_undef_here)
      << false;
  }
}

/// Write a new timestamp file with the given path.
static void writeTimestampFile(StringRef TimestampFile) {
  std::error_code EC;
  llvm::raw_fd_ostream Out(TimestampFile.str(), EC, llvm::sys::fs::F_None);
}

/// Prune the module cache of modules that haven't been accessed in
/// a long time.
static void pruneModuleCache(const HeaderSearchOptions &HSOpts) {
  struct stat StatBuf;
  llvm::SmallString<128> TimestampFile;
  TimestampFile = HSOpts.ModuleCachePath;
  assert(!TimestampFile.empty());
  llvm::sys::path::append(TimestampFile, "modules.timestamp");

  // Try to stat() the timestamp file.
  if (::stat(TimestampFile.c_str(), &StatBuf)) {
    // If the timestamp file wasn't there, create one now.
    if (errno == ENOENT) {
      writeTimestampFile(TimestampFile);
    }
    return;
  }

  // Check whether the time stamp is older than our pruning interval.
  // If not, do nothing.
  time_t TimeStampModTime = StatBuf.st_mtime;
  time_t CurrentTime = time(nullptr);
  if (CurrentTime - TimeStampModTime <= time_t(HSOpts.ModuleCachePruneInterval))
    return;

  // Write a new timestamp file so that nobody else attempts to prune.
  // There is a benign race condition here, if two Clang instances happen to
  // notice at the same time that the timestamp is out-of-date.
  writeTimestampFile(TimestampFile);

  // Walk the entire module cache, looking for unused module files and module
  // indices.
  std::error_code EC;
  SmallString<128> ModuleCachePathNative;
  llvm::sys::path::native(HSOpts.ModuleCachePath, ModuleCachePathNative);
  for (llvm::sys::fs::directory_iterator Dir(ModuleCachePathNative, EC), DirEnd;
       Dir != DirEnd && !EC; Dir.increment(EC)) {
    // If we don't have a directory, there's nothing to look into.
    if (!llvm::sys::fs::is_directory(Dir->path()))
      continue;

    // Walk all of the files within this directory.
    for (llvm::sys::fs::directory_iterator File(Dir->path(), EC), FileEnd;
         File != FileEnd && !EC; File.increment(EC)) {
      // We only care about module and global module index files.
      StringRef Extension = llvm::sys::path::extension(File->path());
      if (Extension != ".pcm" && Extension != ".timestamp" &&
          llvm::sys::path::filename(File->path()) != "modules.idx")
        continue;

      // Look at this file. If we can't stat it, there's nothing interesting
      // there.
      if (::stat(File->path().c_str(), &StatBuf))
        continue;

      // If the file has been used recently enough, leave it there.
      time_t FileAccessTime = StatBuf.st_atime;
      if (CurrentTime - FileAccessTime <=
              time_t(HSOpts.ModuleCachePruneAfter)) {
        continue;
      }

      // Remove the file.
      llvm::sys::fs::remove(File->path());

      // Remove the timestamp file.
      std::string TimpestampFilename = File->path() + ".timestamp";
      llvm::sys::fs::remove(TimpestampFilename);
    }

    // If we removed all of the files in the directory, remove the directory
    // itself.
    if (llvm::sys::fs::directory_iterator(Dir->path(), EC) ==
            llvm::sys::fs::directory_iterator() && !EC)
      llvm::sys::fs::remove(Dir->path());
  }
}

void CompilerInstance::createModuleManager() {
  if (!ModuleManager) {
    if (!hasASTContext())
      createASTContext();

    // If we're implicitly building modules but not currently recursively
    // building a module, check whether we need to prune the module cache.
    if (getSourceManager().getModuleBuildStack().empty() &&
        !getPreprocessor().getHeaderSearchInfo().getModuleCachePath().empty() &&
        getHeaderSearchOpts().ModuleCachePruneInterval > 0 &&
        getHeaderSearchOpts().ModuleCachePruneAfter > 0) {
      pruneModuleCache(getHeaderSearchOpts());
    }

    HeaderSearchOptions &HSOpts = getHeaderSearchOpts();
    std::string Sysroot = HSOpts.Sysroot;
    const PreprocessorOptions &PPOpts = getPreprocessorOpts();
    std::unique_ptr<llvm::Timer> ReadTimer;
    if (FrontendTimerGroup)
      ReadTimer = llvm::make_unique<llvm::Timer>("reading_modules",
                                                 "Reading modules",
                                                 *FrontendTimerGroup);
    ModuleManager = new ASTReader(
        getPreprocessor(), &getASTContext(), getPCHContainerReader(),
        getFrontendOpts().ModuleFileExtensions,
        Sysroot.empty() ? "" : Sysroot.c_str(), PPOpts.DisablePCHValidation,
        /*AllowASTWithCompilerErrors=*/false,
        /*AllowConfigurationMismatch=*/false,
        HSOpts.ModulesValidateSystemHeaders,
        getFrontendOpts().UseGlobalModuleIndex,
        std::move(ReadTimer));
    if (hasASTConsumer()) {
      ModuleManager->setDeserializationListener(
        getASTConsumer().GetASTDeserializationListener());
      getASTContext().setASTMutationListener(
        getASTConsumer().GetASTMutationListener());
    }
    getASTContext().setExternalSource(ModuleManager);
    if (hasSema())
      ModuleManager->InitializeSema(getSema());
    if (hasASTConsumer())
      ModuleManager->StartTranslationUnit(&getASTConsumer());

    if (TheDependencyFileGenerator)
      TheDependencyFileGenerator->AttachToASTReader(*ModuleManager);
    for (auto &Listener : DependencyCollectors)
      Listener->attachToASTReader(*ModuleManager);
  }
}

bool CompilerInstance::loadModuleFile(StringRef FileName) {
  llvm::Timer Timer;
  if (FrontendTimerGroup)
    Timer.init("preloading." + FileName.str(), "Preloading " + FileName.str(),
               *FrontendTimerGroup);
  llvm::TimeRegion TimeLoading(FrontendTimerGroup ? &Timer : nullptr);

  // Helper to recursively read the module names for all modules we're adding.
  // We mark these as known and redirect any attempt to load that module to
  // the files we were handed.
  struct ReadModuleNames : ASTReaderListener {
    CompilerInstance &CI;
    llvm::SmallVector<IdentifierInfo*, 8> LoadedModules;

    ReadModuleNames(CompilerInstance &CI) : CI(CI) {}

    void ReadModuleName(StringRef ModuleName) override {
      LoadedModules.push_back(
          CI.getPreprocessor().getIdentifierInfo(ModuleName));
    }

    void registerAll() {
      for (auto *II : LoadedModules) {
        CI.KnownModules[II] = CI.getPreprocessor()
                                  .getHeaderSearchInfo()
                                  .getModuleMap()
                                  .findModule(II->getName());
      }
      LoadedModules.clear();
    }

    void markAllUnavailable() {
      for (auto *II : LoadedModules) {
        if (Module *M = CI.getPreprocessor()
                            .getHeaderSearchInfo()
                            .getModuleMap()
                            .findModule(II->getName())) {
          M->HasIncompatibleModuleFile = true;

          // Mark module as available if the only reason it was unavailable
          // was missing headers.
          SmallVector<Module *, 2> Stack;
          Stack.push_back(M);
          while (!Stack.empty()) {
            Module *Current = Stack.pop_back_val();
            if (Current->IsMissingRequirement) continue;
            Current->IsAvailable = true;
            Stack.insert(Stack.end(),
                         Current->submodule_begin(), Current->submodule_end());
          }
        }
      }
      LoadedModules.clear();
    }
  };

  // If we don't already have an ASTReader, create one now.
  if (!ModuleManager)
    createModuleManager();

  // If -Wmodule-file-config-mismatch is mapped as an error or worse, allow the
  // ASTReader to diagnose it, since it can produce better errors that we can.
  bool ConfigMismatchIsRecoverable =
      getDiagnostics().getDiagnosticLevel(diag::warn_module_config_mismatch,
                                          SourceLocation())
        <= DiagnosticsEngine::Warning;

  auto Listener = llvm::make_unique<ReadModuleNames>(*this);
  auto &ListenerRef = *Listener;
  ASTReader::ListenerScope ReadModuleNamesListener(*ModuleManager,
                                                   std::move(Listener));

  // Try to load the module file.
  switch (ModuleManager->ReadAST(
      FileName, serialization::MK_ExplicitModule, SourceLocation(),
      ConfigMismatchIsRecoverable ? ASTReader::ARR_ConfigurationMismatch : 0)) {
  case ASTReader::Success:
    // We successfully loaded the module file; remember the set of provided
    // modules so that we don't try to load implicit modules for them.
    ListenerRef.registerAll();
    return true;

  case ASTReader::ConfigurationMismatch:
    // Ignore unusable module files.
    getDiagnostics().Report(SourceLocation(), diag::warn_module_config_mismatch)
        << FileName;
    // All modules provided by any files we tried and failed to load are now
    // unavailable; includes of those modules should now be handled textually.
    ListenerRef.markAllUnavailable();
    return true;

  default:
    return false;
  }
}

ModuleLoadResult
CompilerInstance::loadModule(SourceLocation ImportLoc,
                             ModuleIdPath Path,
                             Module::NameVisibilityKind Visibility,
                             bool IsInclusionDirective) {
  // Determine what file we're searching from.
  StringRef ModuleName = Path[0].first->getName();
  SourceLocation ModuleNameLoc = Path[0].second;

  // If we've already handled this import, just return the cached result.
  // This one-element cache is important to eliminate redundant diagnostics
  // when both the preprocessor and parser see the same import declaration.
  if (ImportLoc.isValid() && LastModuleImportLoc == ImportLoc) {
    // Make the named module visible.
    if (LastModuleImportResult && ModuleName != getLangOpts().CurrentModule)
      ModuleManager->makeModuleVisible(LastModuleImportResult, Visibility,
                                       ImportLoc);
    return LastModuleImportResult;
  }

  clang::Module *Module = nullptr;

  // If we don't already have information on this module, load the module now.
  llvm::DenseMap<const IdentifierInfo *, clang::Module *>::iterator Known
    = KnownModules.find(Path[0].first);
  if (Known != KnownModules.end()) {
    // Retrieve the cached top-level module.
    Module = Known->second;
  } else if (ModuleName == getLangOpts().CurrentModule) {
    // This is the module we're building.
    Module = PP->getHeaderSearchInfo().lookupModule(
        ModuleName, /*AllowSearch*/ true,
        /*AllowExtraModuleMapSearch*/ !IsInclusionDirective);
    /// FIXME: perhaps we should (a) look for a module using the module name
    //  to file map (PrebuiltModuleFiles) and (b) diagnose if still not found?
    //if (Module == nullptr) {
    //  getDiagnostics().Report(ModuleNameLoc, diag::err_module_not_found)
    //    << ModuleName;
    //  ModuleBuildFailed = true;
    //  return ModuleLoadResult();
    //}
    Known = KnownModules.insert(std::make_pair(Path[0].first, Module)).first;
  } else {
    // Search for a module with the given name.
    Module = PP->getHeaderSearchInfo().lookupModule(ModuleName, true,
                                                    !IsInclusionDirective);
    HeaderSearchOptions &HSOpts =
        PP->getHeaderSearchInfo().getHeaderSearchOpts();

    std::string ModuleFileName;
    enum ModuleSource {
      ModuleNotFound, ModuleCache, PrebuiltModulePath, ModuleBuildPragma
    } Source = ModuleNotFound;

    // Check to see if the module has been built as part of this compilation
    // via a module build pragma.
    auto BuiltModuleIt = BuiltModules.find(ModuleName);
    if (BuiltModuleIt != BuiltModules.end()) {
      ModuleFileName = BuiltModuleIt->second;
      Source = ModuleBuildPragma;
    }

    // Try to load the module from the prebuilt module path.
    if (Source == ModuleNotFound && (!HSOpts.PrebuiltModuleFiles.empty() ||
                                     !HSOpts.PrebuiltModulePaths.empty())) {
      ModuleFileName =
        PP->getHeaderSearchInfo().getPrebuiltModuleFileName(ModuleName);
      if (!ModuleFileName.empty())
        Source = PrebuiltModulePath;
    }

    // Try to load the module from the module cache.
    if (Source == ModuleNotFound && Module) {
      ModuleFileName = PP->getHeaderSearchInfo().getCachedModuleFileName(Module);
      Source = ModuleCache;
    }

    if (Source == ModuleNotFound) {
      // We can't find a module, error out here.
      getDiagnostics().Report(ModuleNameLoc, diag::err_module_not_found)
          << ModuleName << SourceRange(ImportLoc, ModuleNameLoc);
      ModuleBuildFailed = true;
      return ModuleLoadResult();
    }

    if (ModuleFileName.empty()) {
      if (Module && Module->HasIncompatibleModuleFile) {
        // We tried and failed to load a module file for this module. Fall
        // back to textual inclusion for its headers.
        return ModuleLoadResult::ConfigMismatch;
      }

      getDiagnostics().Report(ModuleNameLoc, diag::err_module_build_disabled)
          << ModuleName;
      ModuleBuildFailed = true;
      return ModuleLoadResult();
    }

    // If we don't already have an ASTReader, create one now.
    if (!ModuleManager)
      createModuleManager();

    llvm::Timer Timer;
    if (FrontendTimerGroup)
      Timer.init("loading." + ModuleFileName, "Loading " + ModuleFileName,
                 *FrontendTimerGroup);
    llvm::TimeRegion TimeLoading(FrontendTimerGroup ? &Timer : nullptr);

    // Try to load the module file. If we are not trying to load from the
    // module cache, we don't know how to rebuild modules.
    unsigned ARRFlags = Source == ModuleCache ?
                        ASTReader::ARR_OutOfDate | ASTReader::ARR_Missing :
                        Source == PrebuiltModulePath ?
                            0 :
                            ASTReader::ARR_ConfigurationMismatch;
    switch (ModuleManager->ReadAST(ModuleFileName,
                                   Source == PrebuiltModulePath
                                       ? serialization::MK_PrebuiltModule
                                       : Source == ModuleBuildPragma
                                             ? serialization::MK_ExplicitModule
                                             : serialization::MK_ImplicitModule,
                                   ImportLoc, ARRFlags)) {
    case ASTReader::Success: {
      if (Source != ModuleCache && !Module) {
        Module = PP->getHeaderSearchInfo().lookupModule(ModuleName, true,
                                                        !IsInclusionDirective);
        if (!Module || !Module->getASTFile() ||
            FileMgr->getFile(ModuleFileName) != Module->getASTFile()) {
          // Error out if Module does not refer to the file in the prebuilt
          // module path.
          getDiagnostics().Report(ModuleNameLoc, diag::err_module_prebuilt)
              << ModuleName;
          ModuleBuildFailed = true;
          KnownModules[Path[0].first] = nullptr;
          return ModuleLoadResult();
        }
      }
      break;
    }

    case ASTReader::OutOfDate:
    case ASTReader::Missing: {
      if (Source != ModuleCache) {
        // We don't know the desired configuration for this module and don't
        // necessarily even have a module map. Since ReadAST already produces
        // diagnostics for these two cases, we simply error out here.
        ModuleBuildFailed = true;
        KnownModules[Path[0].first] = nullptr;
        return ModuleLoadResult();
      }

      // The module file is missing or out-of-date. Build it.
      assert(Module && "missing module file");
      // Check whether there is a cycle in the module graph.
      ModuleBuildStack ModPath = getSourceManager().getModuleBuildStack();
      ModuleBuildStack::iterator Pos = ModPath.begin(), PosEnd = ModPath.end();
      for (; Pos != PosEnd; ++Pos) {
        if (Pos->first == ModuleName)
          break;
      }

      if (Pos != PosEnd) {
        SmallString<256> CyclePath;
        for (; Pos != PosEnd; ++Pos) {
          CyclePath += Pos->first;
          CyclePath += " -> ";
        }
        CyclePath += ModuleName;

        getDiagnostics().Report(ModuleNameLoc, diag::err_module_cycle)
          << ModuleName << CyclePath;
        return ModuleLoadResult();
      }

      // Check whether we have already attempted to build this module (but
      // failed).
      if (getPreprocessorOpts().FailedModules &&
          getPreprocessorOpts().FailedModules->hasAlreadyFailed(ModuleName)) {
        getDiagnostics().Report(ModuleNameLoc, diag::err_module_not_built)
          << ModuleName
          << SourceRange(ImportLoc, ModuleNameLoc);
        ModuleBuildFailed = true;
        return ModuleLoadResult();
      }

      // Try to compile and then load the module.
      if (!compileAndLoadModule(*this, ImportLoc, ModuleNameLoc, Module,
                                ModuleFileName)) {
        assert(getDiagnostics().hasErrorOccurred() &&
               "undiagnosed error in compileAndLoadModule");
        if (getPreprocessorOpts().FailedModules)
          getPreprocessorOpts().FailedModules->addFailed(ModuleName);
        KnownModules[Path[0].first] = nullptr;
        ModuleBuildFailed = true;
        return ModuleLoadResult();
      }

      // Okay, we've rebuilt and now loaded the module.
      break;
    }

    case ASTReader::ConfigurationMismatch:
      if (Source == PrebuiltModulePath)
        // FIXME: We shouldn't be setting HadFatalFailure below if we only
        // produce a warning here!
        getDiagnostics().Report(SourceLocation(),
                                diag::warn_module_config_mismatch)
            << ModuleFileName;
      // Fall through to error out.
      LLVM_FALLTHROUGH;
    case ASTReader::VersionMismatch:
    case ASTReader::HadErrors:
      ModuleLoader::HadFatalFailure = true;
      // FIXME: The ASTReader will already have complained, but can we shoehorn
      // that diagnostic information into a more useful form?
      KnownModules[Path[0].first] = nullptr;
      return ModuleLoadResult();

    case ASTReader::Failure:
      ModuleLoader::HadFatalFailure = true;
      // Already complained, but note now that we failed.
      KnownModules[Path[0].first] = nullptr;
      ModuleBuildFailed = true;
      return ModuleLoadResult();
    }

    // Cache the result of this top-level module lookup for later.
    Known = KnownModules.insert(std::make_pair(Path[0].first, Module)).first;
  }

  // If we never found the module, fail.
  if (!Module)
    return ModuleLoadResult();

  // Verify that the rest of the module path actually corresponds to
  // a submodule.
  bool MapPrivateSubModToTopLevel = false;
  if (Path.size() > 1) {
    for (unsigned I = 1, N = Path.size(); I != N; ++I) {
      StringRef Name = Path[I].first->getName();
      clang::Module *Sub = Module->findSubmodule(Name);

      // If the user is requesting Foo.Private and it doesn't exist, try to
      // match Foo_Private and emit a warning asking for the user to write
      // @import Foo_Private instead. FIXME: remove this when existing clients
      // migrate off of Foo.Private syntax.
      if (!Sub && PP->getLangOpts().ImplicitModules && Name == "Private" &&
          Module == Module->getTopLevelModule()) {
        SmallString<128> PrivateModule(Module->Name);
        PrivateModule.append("_Private");

        SmallVector<std::pair<IdentifierInfo *, SourceLocation>, 2> PrivPath;
        auto &II = PP->getIdentifierTable().get(
            PrivateModule, PP->getIdentifierInfo(Module->Name)->getTokenID());
        PrivPath.push_back(std::make_pair(&II, Path[0].second));

        if (PP->getHeaderSearchInfo().lookupModule(PrivateModule, true,
                                                   !IsInclusionDirective))
          Sub =
              loadModule(ImportLoc, PrivPath, Visibility, IsInclusionDirective);
        if (Sub) {
          MapPrivateSubModToTopLevel = true;
          if (!getDiagnostics().isIgnored(
                  diag::warn_no_priv_submodule_use_toplevel, ImportLoc)) {
            getDiagnostics().Report(Path[I].second,
                                    diag::warn_no_priv_submodule_use_toplevel)
                << Path[I].first << Module->getFullModuleName() << PrivateModule
                << SourceRange(Path[0].second, Path[I].second)
                << FixItHint::CreateReplacement(SourceRange(Path[0].second),
                                                PrivateModule);
            getDiagnostics().Report(Sub->DefinitionLoc,
                                    diag::note_private_top_level_defined);
          }
        }
      }

      if (!Sub) {
        // Attempt to perform typo correction to find a module name that works.
        SmallVector<StringRef, 2> Best;
        unsigned BestEditDistance = (std::numeric_limits<unsigned>::max)();

        for (clang::Module::submodule_iterator J = Module->submodule_begin(),
                                            JEnd = Module->submodule_end();
             J != JEnd; ++J) {
          unsigned ED = Name.edit_distance((*J)->Name,
                                           /*AllowReplacements=*/true,
                                           BestEditDistance);
          if (ED <= BestEditDistance) {
            if (ED < BestEditDistance) {
              Best.clear();
              BestEditDistance = ED;
            }

            Best.push_back((*J)->Name);
          }
        }

        // If there was a clear winner, user it.
        if (Best.size() == 1) {
          getDiagnostics().Report(Path[I].second,
                                  diag::err_no_submodule_suggest)
            << Path[I].first << Module->getFullModuleName() << Best[0]
            << SourceRange(Path[0].second, Path[I-1].second)
            << FixItHint::CreateReplacement(SourceRange(Path[I].second),
                                            Best[0]);

          Sub = Module->findSubmodule(Best[0]);
        }
      }

      if (!Sub) {
        // No submodule by this name. Complain, and don't look for further
        // submodules.
        getDiagnostics().Report(Path[I].second, diag::err_no_submodule)
          << Path[I].first << Module->getFullModuleName()
          << SourceRange(Path[0].second, Path[I-1].second);
        break;
      }

      Module = Sub;
    }
  }

  // Make the named module visible, if it's not already part of the module
  // we are parsing.
  if (ModuleName != getLangOpts().CurrentModule) {
    if (!Module->IsFromModuleFile && !MapPrivateSubModToTopLevel) {
      // We have an umbrella header or directory that doesn't actually include
      // all of the headers within the directory it covers. Complain about
      // this missing submodule and recover by forgetting that we ever saw
      // this submodule.
      // FIXME: Should we detect this at module load time? It seems fairly
      // expensive (and rare).
      getDiagnostics().Report(ImportLoc, diag::warn_missing_submodule)
        << Module->getFullModuleName()
        << SourceRange(Path.front().second, Path.back().second);

      return ModuleLoadResult::MissingExpected;
    }

    // Check whether this module is available.
    if (Preprocessor::checkModuleIsAvailable(getLangOpts(), getTarget(),
                                             getDiagnostics(), Module)) {
      getDiagnostics().Report(ImportLoc, diag::note_module_import_here)
        << SourceRange(Path.front().second, Path.back().second);
      LastModuleImportLoc = ImportLoc;
      LastModuleImportResult = ModuleLoadResult();
      return ModuleLoadResult();
    }

    ModuleManager->makeModuleVisible(Module, Visibility, ImportLoc);
  }

  // Check for any configuration macros that have changed.
  clang::Module *TopModule = Module->getTopLevelModule();
  for (unsigned I = 0, N = TopModule->ConfigMacros.size(); I != N; ++I) {
    checkConfigMacro(getPreprocessor(), TopModule->ConfigMacros[I],
                     Module, ImportLoc);
  }

  // Resolve any remaining module using export_as for this one.
  getPreprocessor()
      .getHeaderSearchInfo()
      .getModuleMap()
      .resolveLinkAsDependencies(TopModule);

  LastModuleImportLoc = ImportLoc;
  LastModuleImportResult = ModuleLoadResult(Module);
  return LastModuleImportResult;
}

void CompilerInstance::loadModuleFromSource(SourceLocation ImportLoc,
                                            StringRef ModuleName,
                                            StringRef Source) {
  // Avoid creating filenames with special characters.
  SmallString<128> CleanModuleName(ModuleName);
  for (auto &C : CleanModuleName)
    if (!isAlphanumeric(C))
      C = '_';

  // FIXME: Using a randomized filename here means that our intermediate .pcm
  // output is nondeterministic (as .pcm files refer to each other by name).
  // Can this affect the output in any way?
  SmallString<128> ModuleFileName;
  if (std::error_code EC = llvm::sys::fs::createTemporaryFile(
          CleanModuleName, "pcm", ModuleFileName)) {
    getDiagnostics().Report(ImportLoc, diag::err_fe_unable_to_open_output)
        << ModuleFileName << EC.message();
    return;
  }
  std::string ModuleMapFileName = (CleanModuleName + ".map").str();

  FrontendInputFile Input(
      ModuleMapFileName,
      InputKind(getLanguageFromOptions(*Invocation->getLangOpts()),
                InputKind::ModuleMap, /*Preprocessed*/true));

  std::string NullTerminatedSource(Source.str());

  auto PreBuildStep = [&](CompilerInstance &Other) {
    // Create a virtual file containing our desired source.
    // FIXME: We shouldn't need to do this.
    const FileEntry *ModuleMapFile = Other.getFileManager().getVirtualFile(
        ModuleMapFileName, NullTerminatedSource.size(), 0);
    Other.getSourceManager().overrideFileContents(
        ModuleMapFile,
        llvm::MemoryBuffer::getMemBuffer(NullTerminatedSource.c_str()));

    Other.BuiltModules = std::move(BuiltModules);
    Other.DeleteBuiltModules = false;
  };

  auto PostBuildStep = [this](CompilerInstance &Other) {
    BuiltModules = std::move(Other.BuiltModules);
  };

  // Build the module, inheriting any modules that we've built locally.
  if (compileModuleImpl(*this, ImportLoc, ModuleName, Input, StringRef(),
                        ModuleFileName, PreBuildStep, PostBuildStep)) {
    BuiltModules[ModuleName] = ModuleFileName.str();
    llvm::sys::RemoveFileOnSignal(ModuleFileName);
  }
}

void CompilerInstance::makeModuleVisible(Module *Mod,
                                         Module::NameVisibilityKind Visibility,
                                         SourceLocation ImportLoc) {
  if (!ModuleManager)
    createModuleManager();
  if (!ModuleManager)
    return;

  ModuleManager->makeModuleVisible(Mod, Visibility, ImportLoc);
}

GlobalModuleIndex *CompilerInstance::loadGlobalModuleIndex(
    SourceLocation TriggerLoc) {
  if (getPreprocessor().getHeaderSearchInfo().getModuleCachePath().empty())
    return nullptr;
  if (!ModuleManager)
    createModuleManager();
  // Can't do anything if we don't have the module manager.
  if (!ModuleManager)
    return nullptr;
  // Get an existing global index.  This loads it if not already
  // loaded.
  ModuleManager->loadGlobalIndex();
  GlobalModuleIndex *GlobalIndex = ModuleManager->getGlobalIndex();
  // If the global index doesn't exist, create it.
  if (!GlobalIndex && shouldBuildGlobalModuleIndex() && hasFileManager() &&
      hasPreprocessor()) {
    llvm::sys::fs::create_directories(
      getPreprocessor().getHeaderSearchInfo().getModuleCachePath());
    GlobalModuleIndex::writeIndex(
        getFileManager(), getPCHContainerReader(),
        getPreprocessor().getHeaderSearchInfo().getModuleCachePath());
    ModuleManager->resetForReload();
    ModuleManager->loadGlobalIndex();
    GlobalIndex = ModuleManager->getGlobalIndex();
  }
  // For finding modules needing to be imported for fixit messages,
  // we need to make the global index cover all modules, so we do that here.
  if (!HaveFullGlobalModuleIndex && GlobalIndex && !buildingModule()) {
    ModuleMap &MMap = getPreprocessor().getHeaderSearchInfo().getModuleMap();
    bool RecreateIndex = false;
    for (ModuleMap::module_iterator I = MMap.module_begin(),
        E = MMap.module_end(); I != E; ++I) {
      Module *TheModule = I->second;
      const FileEntry *Entry = TheModule->getASTFile();
      if (!Entry) {
        SmallVector<std::pair<IdentifierInfo *, SourceLocation>, 2> Path;
        Path.push_back(std::make_pair(
            getPreprocessor().getIdentifierInfo(TheModule->Name), TriggerLoc));
        std::reverse(Path.begin(), Path.end());
        // Load a module as hidden.  This also adds it to the global index.
        loadModule(TheModule->DefinitionLoc, Path, Module::Hidden, false);
        RecreateIndex = true;
      }
    }
    if (RecreateIndex) {
      GlobalModuleIndex::writeIndex(
          getFileManager(), getPCHContainerReader(),
          getPreprocessor().getHeaderSearchInfo().getModuleCachePath());
      ModuleManager->resetForReload();
      ModuleManager->loadGlobalIndex();
      GlobalIndex = ModuleManager->getGlobalIndex();
    }
    HaveFullGlobalModuleIndex = true;
  }
  return GlobalIndex;
}

// Check global module index for missing imports.
bool
CompilerInstance::lookupMissingImports(StringRef Name,
                                       SourceLocation TriggerLoc) {
  // Look for the symbol in non-imported modules, but only if an error
  // actually occurred.
  if (!buildingModule()) {
    // Load global module index, or retrieve a previously loaded one.
    GlobalModuleIndex *GlobalIndex = loadGlobalModuleIndex(
      TriggerLoc);

    // Only if we have a global index.
    if (GlobalIndex) {
      GlobalModuleIndex::HitSet FoundModules;

      // Find the modules that reference the identifier.
      // Note that this only finds top-level modules.
      // We'll let diagnoseTypo find the actual declaration module.
      if (GlobalIndex->lookupIdentifier(Name, FoundModules))
        return true;
    }
  }

  return false;
}
void CompilerInstance::resetAndLeakSema() { llvm::BuryPointer(takeSema()); }

void CompilerInstance::setExternalSemaSource(
    IntrusiveRefCntPtr<ExternalSemaSource> ESS) {
  ExternalSemaSrc = std::move(ESS);
}
