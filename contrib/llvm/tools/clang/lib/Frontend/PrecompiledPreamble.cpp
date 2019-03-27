//===--- PrecompiledPreamble.cpp - Build precompiled preambles --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Helper class to build precompiled preamble.
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/PrecompiledPreamble.h"
#include "clang/AST/DeclObjC.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/FrontendOptions.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Serialization/ASTWriter.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Mutex.h"
#include "llvm/Support/MutexGuard.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <limits>
#include <utility>

using namespace clang;

namespace {

StringRef getInMemoryPreamblePath() {
#if defined(LLVM_ON_UNIX)
  return "/__clang_tmp/___clang_inmemory_preamble___";
#elif defined(_WIN32)
  return "C:\\__clang_tmp\\___clang_inmemory_preamble___";
#else
#warning "Unknown platform. Defaulting to UNIX-style paths for in-memory PCHs"
  return "/__clang_tmp/___clang_inmemory_preamble___";
#endif
}

IntrusiveRefCntPtr<llvm::vfs::FileSystem>
createVFSOverlayForPreamblePCH(StringRef PCHFilename,
                               std::unique_ptr<llvm::MemoryBuffer> PCHBuffer,
                               IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS) {
  // We want only the PCH file from the real filesystem to be available,
  // so we create an in-memory VFS with just that and overlay it on top.
  IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> PCHFS(
      new llvm::vfs::InMemoryFileSystem());
  PCHFS->addFile(PCHFilename, 0, std::move(PCHBuffer));
  IntrusiveRefCntPtr<llvm::vfs::OverlayFileSystem> Overlay(
      new llvm::vfs::OverlayFileSystem(VFS));
  Overlay->pushOverlay(PCHFS);
  return Overlay;
}

class PreambleDependencyCollector : public DependencyCollector {
public:
  // We want to collect all dependencies for correctness. Avoiding the real
  // system dependencies (e.g. stl from /usr/lib) would probably be a good idea,
  // but there is no way to distinguish between those and the ones that can be
  // spuriously added by '-isystem' (e.g. to suppress warnings from those
  // headers).
  bool needSystemDependencies() override { return true; }
};

/// Keeps a track of files to be deleted in destructor.
class TemporaryFiles {
public:
  // A static instance to be used by all clients.
  static TemporaryFiles &getInstance();

private:
  // Disallow constructing the class directly.
  TemporaryFiles() = default;
  // Disallow copy.
  TemporaryFiles(const TemporaryFiles &) = delete;

public:
  ~TemporaryFiles();

  /// Adds \p File to a set of tracked files.
  void addFile(StringRef File);

  /// Remove \p File from disk and from the set of tracked files.
  void removeFile(StringRef File);

private:
  llvm::sys::SmartMutex<false> Mutex;
  llvm::StringSet<> Files;
};

TemporaryFiles &TemporaryFiles::getInstance() {
  static TemporaryFiles Instance;
  return Instance;
}

TemporaryFiles::~TemporaryFiles() {
  llvm::MutexGuard Guard(Mutex);
  for (const auto &File : Files)
    llvm::sys::fs::remove(File.getKey());
}

void TemporaryFiles::addFile(StringRef File) {
  llvm::MutexGuard Guard(Mutex);
  auto IsInserted = Files.insert(File).second;
  (void)IsInserted;
  assert(IsInserted && "File has already been added");
}

void TemporaryFiles::removeFile(StringRef File) {
  llvm::MutexGuard Guard(Mutex);
  auto WasPresent = Files.erase(File);
  (void)WasPresent;
  assert(WasPresent && "File was not tracked");
  llvm::sys::fs::remove(File);
}

class PrecompilePreambleAction : public ASTFrontendAction {
public:
  PrecompilePreambleAction(std::string *InMemStorage,
                           PreambleCallbacks &Callbacks)
      : InMemStorage(InMemStorage), Callbacks(Callbacks) {}

  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 StringRef InFile) override;

  bool hasEmittedPreamblePCH() const { return HasEmittedPreamblePCH; }

  void setEmittedPreamblePCH(ASTWriter &Writer) {
    this->HasEmittedPreamblePCH = true;
    Callbacks.AfterPCHEmitted(Writer);
  }

  bool shouldEraseOutputFiles() override { return !hasEmittedPreamblePCH(); }
  bool hasCodeCompletionSupport() const override { return false; }
  bool hasASTFileSupport() const override { return false; }
  TranslationUnitKind getTranslationUnitKind() override { return TU_Prefix; }

private:
  friend class PrecompilePreambleConsumer;

  bool HasEmittedPreamblePCH = false;
  std::string *InMemStorage;
  PreambleCallbacks &Callbacks;
};

class PrecompilePreambleConsumer : public PCHGenerator {
public:
  PrecompilePreambleConsumer(PrecompilePreambleAction &Action,
                             const Preprocessor &PP, StringRef isysroot,
                             std::unique_ptr<raw_ostream> Out)
      : PCHGenerator(PP, "", isysroot, std::make_shared<PCHBuffer>(),
                     ArrayRef<std::shared_ptr<ModuleFileExtension>>(),
                     /*AllowASTWithErrors=*/true),
        Action(Action), Out(std::move(Out)) {}

  bool HandleTopLevelDecl(DeclGroupRef DG) override {
    Action.Callbacks.HandleTopLevelDecl(DG);
    return true;
  }

  void HandleTranslationUnit(ASTContext &Ctx) override {
    PCHGenerator::HandleTranslationUnit(Ctx);
    if (!hasEmittedPCH())
      return;

    // Write the generated bitstream to "Out".
    *Out << getPCH();
    // Make sure it hits disk now.
    Out->flush();
    // Free the buffer.
    llvm::SmallVector<char, 0> Empty;
    getPCH() = std::move(Empty);

    Action.setEmittedPreamblePCH(getWriter());
  }

private:
  PrecompilePreambleAction &Action;
  std::unique_ptr<raw_ostream> Out;
};

std::unique_ptr<ASTConsumer>
PrecompilePreambleAction::CreateASTConsumer(CompilerInstance &CI,
                                            StringRef InFile) {
  std::string Sysroot;
  if (!GeneratePCHAction::ComputeASTConsumerArguments(CI, Sysroot))
    return nullptr;

  std::unique_ptr<llvm::raw_ostream> OS;
  if (InMemStorage) {
    OS = llvm::make_unique<llvm::raw_string_ostream>(*InMemStorage);
  } else {
    std::string OutputFile;
    OS = GeneratePCHAction::CreateOutputFile(CI, InFile, OutputFile);
  }
  if (!OS)
    return nullptr;

  if (!CI.getFrontendOpts().RelocatablePCH)
    Sysroot.clear();

  return llvm::make_unique<PrecompilePreambleConsumer>(
      *this, CI.getPreprocessor(), Sysroot, std::move(OS));
}

template <class T> bool moveOnNoError(llvm::ErrorOr<T> Val, T &Output) {
  if (!Val)
    return false;
  Output = std::move(*Val);
  return true;
}

} // namespace

PreambleBounds clang::ComputePreambleBounds(const LangOptions &LangOpts,
                                            llvm::MemoryBuffer *Buffer,
                                            unsigned MaxLines) {
  return Lexer::ComputePreamble(Buffer->getBuffer(), LangOpts, MaxLines);
}

llvm::ErrorOr<PrecompiledPreamble> PrecompiledPreamble::Build(
    const CompilerInvocation &Invocation,
    const llvm::MemoryBuffer *MainFileBuffer, PreambleBounds Bounds,
    DiagnosticsEngine &Diagnostics,
    IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS,
    std::shared_ptr<PCHContainerOperations> PCHContainerOps, bool StoreInMemory,
    PreambleCallbacks &Callbacks) {
  assert(VFS && "VFS is null");

  auto PreambleInvocation = std::make_shared<CompilerInvocation>(Invocation);
  FrontendOptions &FrontendOpts = PreambleInvocation->getFrontendOpts();
  PreprocessorOptions &PreprocessorOpts =
      PreambleInvocation->getPreprocessorOpts();

  llvm::Optional<TempPCHFile> TempFile;
  if (!StoreInMemory) {
    // Create a temporary file for the precompiled preamble. In rare
    // circumstances, this can fail.
    llvm::ErrorOr<PrecompiledPreamble::TempPCHFile> PreamblePCHFile =
        PrecompiledPreamble::TempPCHFile::CreateNewPreamblePCHFile();
    if (!PreamblePCHFile)
      return BuildPreambleError::CouldntCreateTempFile;
    TempFile = std::move(*PreamblePCHFile);
  }

  PCHStorage Storage = StoreInMemory ? PCHStorage(InMemoryPreamble())
                                     : PCHStorage(std::move(*TempFile));

  // Save the preamble text for later; we'll need to compare against it for
  // subsequent reparses.
  std::vector<char> PreambleBytes(MainFileBuffer->getBufferStart(),
                                  MainFileBuffer->getBufferStart() +
                                      Bounds.Size);
  bool PreambleEndsAtStartOfLine = Bounds.PreambleEndsAtStartOfLine;

  // Tell the compiler invocation to generate a temporary precompiled header.
  FrontendOpts.ProgramAction = frontend::GeneratePCH;
  FrontendOpts.OutputFile = StoreInMemory ? getInMemoryPreamblePath()
                                          : Storage.asFile().getFilePath();
  PreprocessorOpts.PrecompiledPreambleBytes.first = 0;
  PreprocessorOpts.PrecompiledPreambleBytes.second = false;
  // Inform preprocessor to record conditional stack when building the preamble.
  PreprocessorOpts.GeneratePreamble = true;

  // Create the compiler instance to use for building the precompiled preamble.
  std::unique_ptr<CompilerInstance> Clang(
      new CompilerInstance(std::move(PCHContainerOps)));

  // Recover resources if we crash before exiting this method.
  llvm::CrashRecoveryContextCleanupRegistrar<CompilerInstance> CICleanup(
      Clang.get());

  Clang->setInvocation(std::move(PreambleInvocation));
  Clang->setDiagnostics(&Diagnostics);

  // Create the target instance.
  Clang->setTarget(TargetInfo::CreateTargetInfo(
      Clang->getDiagnostics(), Clang->getInvocation().TargetOpts));
  if (!Clang->hasTarget())
    return BuildPreambleError::CouldntCreateTargetInfo;

  // Inform the target of the language options.
  //
  // FIXME: We shouldn't need to do this, the target should be immutable once
  // created. This complexity should be lifted elsewhere.
  Clang->getTarget().adjust(Clang->getLangOpts());

  assert(Clang->getFrontendOpts().Inputs.size() == 1 &&
         "Invocation must have exactly one source file!");
  assert(Clang->getFrontendOpts().Inputs[0].getKind().getFormat() ==
             InputKind::Source &&
         "FIXME: AST inputs not yet supported here!");
  assert(Clang->getFrontendOpts().Inputs[0].getKind().getLanguage() !=
             InputKind::LLVM_IR &&
         "IR inputs not support here!");

  // Clear out old caches and data.
  Diagnostics.Reset();
  ProcessWarningOptions(Diagnostics, Clang->getDiagnosticOpts());

  VFS =
      createVFSFromCompilerInvocation(Clang->getInvocation(), Diagnostics, VFS);

  // Create a file manager object to provide access to and cache the filesystem.
  Clang->setFileManager(new FileManager(Clang->getFileSystemOpts(), VFS));

  // Create the source manager.
  Clang->setSourceManager(
      new SourceManager(Diagnostics, Clang->getFileManager()));

  auto PreambleDepCollector = std::make_shared<PreambleDependencyCollector>();
  Clang->addDependencyCollector(PreambleDepCollector);

  // Remap the main source file to the preamble buffer.
  StringRef MainFilePath = FrontendOpts.Inputs[0].getFile();
  auto PreambleInputBuffer = llvm::MemoryBuffer::getMemBufferCopy(
      MainFileBuffer->getBuffer().slice(0, Bounds.Size), MainFilePath);
  if (PreprocessorOpts.RetainRemappedFileBuffers) {
    // MainFileBuffer will be deleted by unique_ptr after leaving the method.
    PreprocessorOpts.addRemappedFile(MainFilePath, PreambleInputBuffer.get());
  } else {
    // In that case, remapped buffer will be deleted by CompilerInstance on
    // BeginSourceFile, so we call release() to avoid double deletion.
    PreprocessorOpts.addRemappedFile(MainFilePath,
                                     PreambleInputBuffer.release());
  }

  std::unique_ptr<PrecompilePreambleAction> Act;
  Act.reset(new PrecompilePreambleAction(
      StoreInMemory ? &Storage.asMemory().Data : nullptr, Callbacks));
  Callbacks.BeforeExecute(*Clang);
  if (!Act->BeginSourceFile(*Clang.get(), Clang->getFrontendOpts().Inputs[0]))
    return BuildPreambleError::BeginSourceFileFailed;

  std::unique_ptr<PPCallbacks> DelegatedPPCallbacks =
      Callbacks.createPPCallbacks();
  if (DelegatedPPCallbacks)
    Clang->getPreprocessor().addPPCallbacks(std::move(DelegatedPPCallbacks));

  Act->Execute();

  // Run the callbacks.
  Callbacks.AfterExecute(*Clang);

  Act->EndSourceFile();

  if (!Act->hasEmittedPreamblePCH())
    return BuildPreambleError::CouldntEmitPCH;

  // Keep track of all of the files that the source manager knows about,
  // so we can verify whether they have changed or not.
  llvm::StringMap<PrecompiledPreamble::PreambleFileHash> FilesInPreamble;

  SourceManager &SourceMgr = Clang->getSourceManager();
  for (auto &Filename : PreambleDepCollector->getDependencies()) {
    const FileEntry *File = Clang->getFileManager().getFile(Filename);
    if (!File || File == SourceMgr.getFileEntryForID(SourceMgr.getMainFileID()))
      continue;
    if (time_t ModTime = File->getModificationTime()) {
      FilesInPreamble[File->getName()] =
          PrecompiledPreamble::PreambleFileHash::createForFile(File->getSize(),
                                                               ModTime);
    } else {
      llvm::MemoryBuffer *Buffer = SourceMgr.getMemoryBufferForFile(File);
      FilesInPreamble[File->getName()] =
          PrecompiledPreamble::PreambleFileHash::createForMemoryBuffer(Buffer);
    }
  }

  return PrecompiledPreamble(std::move(Storage), std::move(PreambleBytes),
                             PreambleEndsAtStartOfLine,
                             std::move(FilesInPreamble));
}

PreambleBounds PrecompiledPreamble::getBounds() const {
  return PreambleBounds(PreambleBytes.size(), PreambleEndsAtStartOfLine);
}

std::size_t PrecompiledPreamble::getSize() const {
  switch (Storage.getKind()) {
  case PCHStorage::Kind::Empty:
    assert(false && "Calling getSize() on invalid PrecompiledPreamble. "
                    "Was it std::moved?");
    return 0;
  case PCHStorage::Kind::InMemory:
    return Storage.asMemory().Data.size();
  case PCHStorage::Kind::TempFile: {
    uint64_t Result;
    if (llvm::sys::fs::file_size(Storage.asFile().getFilePath(), Result))
      return 0;

    assert(Result <= std::numeric_limits<std::size_t>::max() &&
           "file size did not fit into size_t");
    return Result;
  }
  }
  llvm_unreachable("Unhandled storage kind");
}

bool PrecompiledPreamble::CanReuse(const CompilerInvocation &Invocation,
                                   const llvm::MemoryBuffer *MainFileBuffer,
                                   PreambleBounds Bounds,
                                   llvm::vfs::FileSystem *VFS) const {

  assert(
      Bounds.Size <= MainFileBuffer->getBufferSize() &&
      "Buffer is too large. Bounds were calculated from a different buffer?");

  auto PreambleInvocation = std::make_shared<CompilerInvocation>(Invocation);
  PreprocessorOptions &PreprocessorOpts =
      PreambleInvocation->getPreprocessorOpts();

  // We've previously computed a preamble. Check whether we have the same
  // preamble now that we did before, and that there's enough space in
  // the main-file buffer within the precompiled preamble to fit the
  // new main file.
  if (PreambleBytes.size() != Bounds.Size ||
      PreambleEndsAtStartOfLine != Bounds.PreambleEndsAtStartOfLine ||
      !std::equal(PreambleBytes.begin(), PreambleBytes.end(),
                  MainFileBuffer->getBuffer().begin()))
    return false;
  // The preamble has not changed. We may be able to re-use the precompiled
  // preamble.

  // Check that none of the files used by the preamble have changed.
  // First, make a record of those files that have been overridden via
  // remapping or unsaved_files.
  std::map<llvm::sys::fs::UniqueID, PreambleFileHash> OverriddenFiles;
  for (const auto &R : PreprocessorOpts.RemappedFiles) {
    llvm::vfs::Status Status;
    if (!moveOnNoError(VFS->status(R.second), Status)) {
      // If we can't stat the file we're remapping to, assume that something
      // horrible happened.
      return false;
    }

    OverriddenFiles[Status.getUniqueID()] = PreambleFileHash::createForFile(
        Status.getSize(), llvm::sys::toTimeT(Status.getLastModificationTime()));
  }

  for (const auto &RB : PreprocessorOpts.RemappedFileBuffers) {
    llvm::vfs::Status Status;
    if (!moveOnNoError(VFS->status(RB.first), Status))
      return false;

    OverriddenFiles[Status.getUniqueID()] =
        PreambleFileHash::createForMemoryBuffer(RB.second);
  }

  // Check whether anything has changed.
  for (const auto &F : FilesInPreamble) {
    llvm::vfs::Status Status;
    if (!moveOnNoError(VFS->status(F.first()), Status)) {
      // If we can't stat the file, assume that something horrible happened.
      return false;
    }

    std::map<llvm::sys::fs::UniqueID, PreambleFileHash>::iterator Overridden =
        OverriddenFiles.find(Status.getUniqueID());
    if (Overridden != OverriddenFiles.end()) {
      // This file was remapped; check whether the newly-mapped file
      // matches up with the previous mapping.
      if (Overridden->second != F.second)
        return false;
      continue;
    }

    // The file was not remapped; check whether it has changed on disk.
    if (Status.getSize() != uint64_t(F.second.Size) ||
        llvm::sys::toTimeT(Status.getLastModificationTime()) !=
            F.second.ModTime)
      return false;
  }
  return true;
}

void PrecompiledPreamble::AddImplicitPreamble(
    CompilerInvocation &CI, IntrusiveRefCntPtr<llvm::vfs::FileSystem> &VFS,
    llvm::MemoryBuffer *MainFileBuffer) const {
  PreambleBounds Bounds(PreambleBytes.size(), PreambleEndsAtStartOfLine);
  configurePreamble(Bounds, CI, VFS, MainFileBuffer);
}

void PrecompiledPreamble::OverridePreamble(
    CompilerInvocation &CI, IntrusiveRefCntPtr<llvm::vfs::FileSystem> &VFS,
    llvm::MemoryBuffer *MainFileBuffer) const {
  auto Bounds = ComputePreambleBounds(*CI.getLangOpts(), MainFileBuffer, 0);
  configurePreamble(Bounds, CI, VFS, MainFileBuffer);
}

PrecompiledPreamble::PrecompiledPreamble(
    PCHStorage Storage, std::vector<char> PreambleBytes,
    bool PreambleEndsAtStartOfLine,
    llvm::StringMap<PreambleFileHash> FilesInPreamble)
    : Storage(std::move(Storage)), FilesInPreamble(std::move(FilesInPreamble)),
      PreambleBytes(std::move(PreambleBytes)),
      PreambleEndsAtStartOfLine(PreambleEndsAtStartOfLine) {
  assert(this->Storage.getKind() != PCHStorage::Kind::Empty);
}

llvm::ErrorOr<PrecompiledPreamble::TempPCHFile>
PrecompiledPreamble::TempPCHFile::CreateNewPreamblePCHFile() {
  // FIXME: This is a hack so that we can override the preamble file during
  // crash-recovery testing, which is the only case where the preamble files
  // are not necessarily cleaned up.
  const char *TmpFile = ::getenv("CINDEXTEST_PREAMBLE_FILE");
  if (TmpFile)
    return TempPCHFile::createFromCustomPath(TmpFile);
  return TempPCHFile::createInSystemTempDir("preamble", "pch");
}

llvm::ErrorOr<PrecompiledPreamble::TempPCHFile>
PrecompiledPreamble::TempPCHFile::createInSystemTempDir(const Twine &Prefix,
                                                        StringRef Suffix) {
  llvm::SmallString<64> File;
  // Using a version of createTemporaryFile with a file descriptor guarantees
  // that we would never get a race condition in a multi-threaded setting
  // (i.e., multiple threads getting the same temporary path).
  int FD;
  auto EC = llvm::sys::fs::createTemporaryFile(Prefix, Suffix, FD, File);
  if (EC)
    return EC;
  // We only needed to make sure the file exists, close the file right away.
  llvm::sys::Process::SafelyCloseFileDescriptor(FD);
  return TempPCHFile(std::move(File).str());
}

llvm::ErrorOr<PrecompiledPreamble::TempPCHFile>
PrecompiledPreamble::TempPCHFile::createFromCustomPath(const Twine &Path) {
  return TempPCHFile(Path.str());
}

PrecompiledPreamble::TempPCHFile::TempPCHFile(std::string FilePath)
    : FilePath(std::move(FilePath)) {
  TemporaryFiles::getInstance().addFile(*this->FilePath);
}

PrecompiledPreamble::TempPCHFile::TempPCHFile(TempPCHFile &&Other) {
  FilePath = std::move(Other.FilePath);
  Other.FilePath = None;
}

PrecompiledPreamble::TempPCHFile &PrecompiledPreamble::TempPCHFile::
operator=(TempPCHFile &&Other) {
  RemoveFileIfPresent();

  FilePath = std::move(Other.FilePath);
  Other.FilePath = None;
  return *this;
}

PrecompiledPreamble::TempPCHFile::~TempPCHFile() { RemoveFileIfPresent(); }

void PrecompiledPreamble::TempPCHFile::RemoveFileIfPresent() {
  if (FilePath) {
    TemporaryFiles::getInstance().removeFile(*FilePath);
    FilePath = None;
  }
}

llvm::StringRef PrecompiledPreamble::TempPCHFile::getFilePath() const {
  assert(FilePath && "TempPCHFile doesn't have a FilePath. Had it been moved?");
  return *FilePath;
}

PrecompiledPreamble::PCHStorage::PCHStorage(TempPCHFile File)
    : StorageKind(Kind::TempFile) {
  new (&asFile()) TempPCHFile(std::move(File));
}

PrecompiledPreamble::PCHStorage::PCHStorage(InMemoryPreamble Memory)
    : StorageKind(Kind::InMemory) {
  new (&asMemory()) InMemoryPreamble(std::move(Memory));
}

PrecompiledPreamble::PCHStorage::PCHStorage(PCHStorage &&Other) : PCHStorage() {
  *this = std::move(Other);
}

PrecompiledPreamble::PCHStorage &PrecompiledPreamble::PCHStorage::
operator=(PCHStorage &&Other) {
  destroy();

  StorageKind = Other.StorageKind;
  switch (StorageKind) {
  case Kind::Empty:
    // do nothing;
    break;
  case Kind::TempFile:
    new (&asFile()) TempPCHFile(std::move(Other.asFile()));
    break;
  case Kind::InMemory:
    new (&asMemory()) InMemoryPreamble(std::move(Other.asMemory()));
    break;
  }

  Other.setEmpty();
  return *this;
}

PrecompiledPreamble::PCHStorage::~PCHStorage() { destroy(); }

PrecompiledPreamble::PCHStorage::Kind
PrecompiledPreamble::PCHStorage::getKind() const {
  return StorageKind;
}

PrecompiledPreamble::TempPCHFile &PrecompiledPreamble::PCHStorage::asFile() {
  assert(getKind() == Kind::TempFile);
  return *reinterpret_cast<TempPCHFile *>(Storage.buffer);
}

const PrecompiledPreamble::TempPCHFile &
PrecompiledPreamble::PCHStorage::asFile() const {
  return const_cast<PCHStorage *>(this)->asFile();
}

PrecompiledPreamble::InMemoryPreamble &
PrecompiledPreamble::PCHStorage::asMemory() {
  assert(getKind() == Kind::InMemory);
  return *reinterpret_cast<InMemoryPreamble *>(Storage.buffer);
}

const PrecompiledPreamble::InMemoryPreamble &
PrecompiledPreamble::PCHStorage::asMemory() const {
  return const_cast<PCHStorage *>(this)->asMemory();
}

void PrecompiledPreamble::PCHStorage::destroy() {
  switch (StorageKind) {
  case Kind::Empty:
    return;
  case Kind::TempFile:
    asFile().~TempPCHFile();
    return;
  case Kind::InMemory:
    asMemory().~InMemoryPreamble();
    return;
  }
}

void PrecompiledPreamble::PCHStorage::setEmpty() {
  destroy();
  StorageKind = Kind::Empty;
}

PrecompiledPreamble::PreambleFileHash
PrecompiledPreamble::PreambleFileHash::createForFile(off_t Size,
                                                     time_t ModTime) {
  PreambleFileHash Result;
  Result.Size = Size;
  Result.ModTime = ModTime;
  Result.MD5 = {};
  return Result;
}

PrecompiledPreamble::PreambleFileHash
PrecompiledPreamble::PreambleFileHash::createForMemoryBuffer(
    const llvm::MemoryBuffer *Buffer) {
  PreambleFileHash Result;
  Result.Size = Buffer->getBufferSize();
  Result.ModTime = 0;

  llvm::MD5 MD5Ctx;
  MD5Ctx.update(Buffer->getBuffer().data());
  MD5Ctx.final(Result.MD5);

  return Result;
}

void PrecompiledPreamble::configurePreamble(
    PreambleBounds Bounds, CompilerInvocation &CI,
    IntrusiveRefCntPtr<llvm::vfs::FileSystem> &VFS,
    llvm::MemoryBuffer *MainFileBuffer) const {
  assert(VFS);

  auto &PreprocessorOpts = CI.getPreprocessorOpts();

  // Remap main file to point to MainFileBuffer.
  auto MainFilePath = CI.getFrontendOpts().Inputs[0].getFile();
  PreprocessorOpts.addRemappedFile(MainFilePath, MainFileBuffer);

  // Configure ImpicitPCHInclude.
  PreprocessorOpts.PrecompiledPreambleBytes.first = Bounds.Size;
  PreprocessorOpts.PrecompiledPreambleBytes.second =
      Bounds.PreambleEndsAtStartOfLine;
  PreprocessorOpts.DisablePCHValidation = true;

  setupPreambleStorage(Storage, PreprocessorOpts, VFS);
}

void PrecompiledPreamble::setupPreambleStorage(
    const PCHStorage &Storage, PreprocessorOptions &PreprocessorOpts,
    IntrusiveRefCntPtr<llvm::vfs::FileSystem> &VFS) {
  if (Storage.getKind() == PCHStorage::Kind::TempFile) {
    const TempPCHFile &PCHFile = Storage.asFile();
    PreprocessorOpts.ImplicitPCHInclude = PCHFile.getFilePath();

    // Make sure we can access the PCH file even if we're using a VFS
    IntrusiveRefCntPtr<llvm::vfs::FileSystem> RealFS =
        llvm::vfs::getRealFileSystem();
    auto PCHPath = PCHFile.getFilePath();
    if (VFS == RealFS || VFS->exists(PCHPath))
      return;
    auto Buf = RealFS->getBufferForFile(PCHPath);
    if (!Buf) {
      // We can't read the file even from RealFS, this is clearly an error,
      // but we'll just leave the current VFS as is and let clang's code
      // figure out what to do with missing PCH.
      return;
    }

    // We have a slight inconsistency here -- we're using the VFS to
    // read files, but the PCH was generated in the real file system.
    VFS = createVFSOverlayForPreamblePCH(PCHPath, std::move(*Buf), VFS);
  } else {
    assert(Storage.getKind() == PCHStorage::Kind::InMemory);
    // For in-memory preamble, we have to provide a VFS overlay that makes it
    // accessible.
    StringRef PCHPath = getInMemoryPreamblePath();
    PreprocessorOpts.ImplicitPCHInclude = PCHPath;

    auto Buf = llvm::MemoryBuffer::getMemBuffer(Storage.asMemory().Data);
    VFS = createVFSOverlayForPreamblePCH(PCHPath, std::move(Buf), VFS);
  }
}

void PreambleCallbacks::BeforeExecute(CompilerInstance &CI) {}
void PreambleCallbacks::AfterExecute(CompilerInstance &CI) {}
void PreambleCallbacks::AfterPCHEmitted(ASTWriter &Writer) {}
void PreambleCallbacks::HandleTopLevelDecl(DeclGroupRef DG) {}
std::unique_ptr<PPCallbacks> PreambleCallbacks::createPPCallbacks() {
  return nullptr;
}

static llvm::ManagedStatic<BuildPreambleErrorCategory> BuildPreambleErrCategory;

std::error_code clang::make_error_code(BuildPreambleError Error) {
  return std::error_code(static_cast<int>(Error), *BuildPreambleErrCategory);
}

const char *BuildPreambleErrorCategory::name() const noexcept {
  return "build-preamble.error";
}

std::string BuildPreambleErrorCategory::message(int condition) const {
  switch (static_cast<BuildPreambleError>(condition)) {
  case BuildPreambleError::CouldntCreateTempFile:
    return "Could not create temporary file for PCH";
  case BuildPreambleError::CouldntCreateTargetInfo:
    return "CreateTargetInfo() return null";
  case BuildPreambleError::BeginSourceFileFailed:
    return "BeginSourceFile() return an error";
  case BuildPreambleError::CouldntEmitPCH:
    return "Could not emit PCH";
  }
  llvm_unreachable("unexpected BuildPreambleError");
}
