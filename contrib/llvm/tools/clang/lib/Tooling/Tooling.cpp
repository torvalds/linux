//===- Tooling.cpp - Running clang standalone tools -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements functions to run clang tools standalone instead
//  of running them as a plugin.
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/Tooling.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/FileSystemOptions.h"
#include "clang/Basic/LLVM.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/Job.h"
#include "clang/Driver/Options.h"
#include "clang/Driver/Tool.h"
#include "clang/Driver/ToolChain.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Frontend/FrontendOptions.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Lex/HeaderSearchOptions.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Tooling/ArgumentsAdjusters.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstring>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#define DEBUG_TYPE "clang-tooling"

using namespace clang;
using namespace tooling;

ToolAction::~ToolAction() = default;

FrontendActionFactory::~FrontendActionFactory() = default;

// FIXME: This file contains structural duplication with other parts of the
// code that sets up a compiler to run tools on it, and we should refactor
// it to be based on the same framework.

/// Builds a clang driver initialized for running clang tools.
static driver::Driver *
newDriver(DiagnosticsEngine *Diagnostics, const char *BinaryName,
          IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS) {
  driver::Driver *CompilerDriver =
      new driver::Driver(BinaryName, llvm::sys::getDefaultTargetTriple(),
                         *Diagnostics, std::move(VFS));
  CompilerDriver->setTitle("clang_based_tool");
  return CompilerDriver;
}

/// Retrieves the clang CC1 specific flags out of the compilation's jobs.
///
/// Returns nullptr on error.
static const llvm::opt::ArgStringList *getCC1Arguments(
    DiagnosticsEngine *Diagnostics, driver::Compilation *Compilation) {
  // We expect to get back exactly one Command job, if we didn't something
  // failed. Extract that job from the Compilation.
  const driver::JobList &Jobs = Compilation->getJobs();
  if (Jobs.size() != 1 || !isa<driver::Command>(*Jobs.begin())) {
    SmallString<256> error_msg;
    llvm::raw_svector_ostream error_stream(error_msg);
    Jobs.Print(error_stream, "; ", true);
    Diagnostics->Report(diag::err_fe_expected_compiler_job)
        << error_stream.str();
    return nullptr;
  }

  // The one job we find should be to invoke clang again.
  const auto &Cmd = cast<driver::Command>(*Jobs.begin());
  if (StringRef(Cmd.getCreator().getName()) != "clang") {
    Diagnostics->Report(diag::err_fe_expected_clang_command);
    return nullptr;
  }

  return &Cmd.getArguments();
}

namespace clang {
namespace tooling {

/// Returns a clang build invocation initialized from the CC1 flags.
CompilerInvocation *newInvocation(
    DiagnosticsEngine *Diagnostics, const llvm::opt::ArgStringList &CC1Args) {
  assert(!CC1Args.empty() && "Must at least contain the program name!");
  CompilerInvocation *Invocation = new CompilerInvocation;
  CompilerInvocation::CreateFromArgs(
      *Invocation, CC1Args.data() + 1, CC1Args.data() + CC1Args.size(),
      *Diagnostics);
  Invocation->getFrontendOpts().DisableFree = false;
  Invocation->getCodeGenOpts().DisableFree = false;
  return Invocation;
}

bool runToolOnCode(FrontendAction *ToolAction, const Twine &Code,
                   const Twine &FileName,
                   std::shared_ptr<PCHContainerOperations> PCHContainerOps) {
  return runToolOnCodeWithArgs(ToolAction, Code, std::vector<std::string>(),
                               FileName, "clang-tool",
                               std::move(PCHContainerOps));
}

} // namespace tooling
} // namespace clang

static std::vector<std::string>
getSyntaxOnlyToolArgs(const Twine &ToolName,
                      const std::vector<std::string> &ExtraArgs,
                      StringRef FileName) {
  std::vector<std::string> Args;
  Args.push_back(ToolName.str());
  Args.push_back("-fsyntax-only");
  Args.insert(Args.end(), ExtraArgs.begin(), ExtraArgs.end());
  Args.push_back(FileName.str());
  return Args;
}

namespace clang {
namespace tooling {

bool runToolOnCodeWithArgs(
    FrontendAction *ToolAction, const Twine &Code,
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS,
    const std::vector<std::string> &Args, const Twine &FileName,
    const Twine &ToolName,
    std::shared_ptr<PCHContainerOperations> PCHContainerOps) {
  SmallString<16> FileNameStorage;
  StringRef FileNameRef = FileName.toNullTerminatedStringRef(FileNameStorage);

  llvm::IntrusiveRefCntPtr<FileManager> Files(
      new FileManager(FileSystemOptions(), VFS));
  ArgumentsAdjuster Adjuster = getClangStripDependencyFileAdjuster();
  ToolInvocation Invocation(
      getSyntaxOnlyToolArgs(ToolName, Adjuster(Args, FileNameRef), FileNameRef),
      ToolAction, Files.get(),
      std::move(PCHContainerOps));
  return Invocation.run();
}

bool runToolOnCodeWithArgs(
    FrontendAction *ToolAction, const Twine &Code,
    const std::vector<std::string> &Args, const Twine &FileName,
    const Twine &ToolName,
    std::shared_ptr<PCHContainerOperations> PCHContainerOps,
    const FileContentMappings &VirtualMappedFiles) {
  llvm::IntrusiveRefCntPtr<llvm::vfs::OverlayFileSystem> OverlayFileSystem(
      new llvm::vfs::OverlayFileSystem(llvm::vfs::getRealFileSystem()));
  llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> InMemoryFileSystem(
      new llvm::vfs::InMemoryFileSystem);
  OverlayFileSystem->pushOverlay(InMemoryFileSystem);

  SmallString<1024> CodeStorage;
  InMemoryFileSystem->addFile(FileName, 0,
                              llvm::MemoryBuffer::getMemBuffer(
                                  Code.toNullTerminatedStringRef(CodeStorage)));

  for (auto &FilenameWithContent : VirtualMappedFiles) {
    InMemoryFileSystem->addFile(
        FilenameWithContent.first, 0,
        llvm::MemoryBuffer::getMemBuffer(FilenameWithContent.second));
  }

  return runToolOnCodeWithArgs(ToolAction, Code, OverlayFileSystem, Args,
                               FileName, ToolName);
}

llvm::Expected<std::string> getAbsolutePath(llvm::vfs::FileSystem &FS,
                                            StringRef File) {
  StringRef RelativePath(File);
  // FIXME: Should '.\\' be accepted on Win32?
  if (RelativePath.startswith("./")) {
    RelativePath = RelativePath.substr(strlen("./"));
  }

  SmallString<1024> AbsolutePath = RelativePath;
  if (auto EC = FS.makeAbsolute(AbsolutePath))
    return llvm::errorCodeToError(EC);
  llvm::sys::path::native(AbsolutePath);
  return AbsolutePath.str();
}

std::string getAbsolutePath(StringRef File) {
  return llvm::cantFail(getAbsolutePath(*llvm::vfs::getRealFileSystem(), File));
}

void addTargetAndModeForProgramName(std::vector<std::string> &CommandLine,
                                    StringRef InvokedAs) {
  if (!CommandLine.empty() && !InvokedAs.empty()) {
    bool AlreadyHasTarget = false;
    bool AlreadyHasMode = false;
    // Skip CommandLine[0].
    for (auto Token = ++CommandLine.begin(); Token != CommandLine.end();
         ++Token) {
      StringRef TokenRef(*Token);
      AlreadyHasTarget |=
          (TokenRef == "-target" || TokenRef.startswith("-target="));
      AlreadyHasMode |= (TokenRef == "--driver-mode" ||
                         TokenRef.startswith("--driver-mode="));
    }
    auto TargetMode =
        driver::ToolChain::getTargetAndModeFromProgramName(InvokedAs);
    if (!AlreadyHasMode && TargetMode.DriverMode) {
      CommandLine.insert(++CommandLine.begin(), TargetMode.DriverMode);
    }
    if (!AlreadyHasTarget && TargetMode.TargetIsValid) {
      CommandLine.insert(++CommandLine.begin(), {"-target",
                                                 TargetMode.TargetPrefix});
    }
  }
}

} // namespace tooling
} // namespace clang

namespace {

class SingleFrontendActionFactory : public FrontendActionFactory {
  FrontendAction *Action;

public:
  SingleFrontendActionFactory(FrontendAction *Action) : Action(Action) {}

  FrontendAction *create() override { return Action; }
};

} // namespace

ToolInvocation::ToolInvocation(
    std::vector<std::string> CommandLine, ToolAction *Action,
    FileManager *Files, std::shared_ptr<PCHContainerOperations> PCHContainerOps)
    : CommandLine(std::move(CommandLine)), Action(Action), OwnsAction(false),
      Files(Files), PCHContainerOps(std::move(PCHContainerOps)) {}

ToolInvocation::ToolInvocation(
    std::vector<std::string> CommandLine, FrontendAction *FAction,
    FileManager *Files, std::shared_ptr<PCHContainerOperations> PCHContainerOps)
    : CommandLine(std::move(CommandLine)),
      Action(new SingleFrontendActionFactory(FAction)), OwnsAction(true),
      Files(Files), PCHContainerOps(std::move(PCHContainerOps)) {}

ToolInvocation::~ToolInvocation() {
  if (OwnsAction)
    delete Action;
}

void ToolInvocation::mapVirtualFile(StringRef FilePath, StringRef Content) {
  SmallString<1024> PathStorage;
  llvm::sys::path::native(FilePath, PathStorage);
  MappedFileContents[PathStorage] = Content;
}

bool ToolInvocation::run() {
  std::vector<const char*> Argv;
  for (const std::string &Str : CommandLine)
    Argv.push_back(Str.c_str());
  const char *const BinaryName = Argv[0];
  IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts = new DiagnosticOptions();
  unsigned MissingArgIndex, MissingArgCount;
  std::unique_ptr<llvm::opt::OptTable> Opts = driver::createDriverOptTable();
  llvm::opt::InputArgList ParsedArgs = Opts->ParseArgs(
      ArrayRef<const char *>(Argv).slice(1), MissingArgIndex, MissingArgCount);
  ParseDiagnosticArgs(*DiagOpts, ParsedArgs);
  TextDiagnosticPrinter DiagnosticPrinter(
      llvm::errs(), &*DiagOpts);
  DiagnosticsEngine Diagnostics(
      IntrusiveRefCntPtr<DiagnosticIDs>(new DiagnosticIDs()), &*DiagOpts,
      DiagConsumer ? DiagConsumer : &DiagnosticPrinter, false);

  const std::unique_ptr<driver::Driver> Driver(
      newDriver(&Diagnostics, BinaryName, Files->getVirtualFileSystem()));
  // The "input file not found" diagnostics from the driver are useful.
  // The driver is only aware of the VFS working directory, but some clients
  // change this at the FileManager level instead.
  // In this case the checks have false positives, so skip them.
  if (!Files->getFileSystemOpts().WorkingDir.empty())
    Driver->setCheckInputsExist(false);
  const std::unique_ptr<driver::Compilation> Compilation(
      Driver->BuildCompilation(llvm::makeArrayRef(Argv)));
  if (!Compilation)
    return false;
  const llvm::opt::ArgStringList *const CC1Args = getCC1Arguments(
      &Diagnostics, Compilation.get());
  if (!CC1Args)
    return false;
  std::unique_ptr<CompilerInvocation> Invocation(
      newInvocation(&Diagnostics, *CC1Args));
  // FIXME: remove this when all users have migrated!
  for (const auto &It : MappedFileContents) {
    // Inject the code as the given file name into the preprocessor options.
    std::unique_ptr<llvm::MemoryBuffer> Input =
        llvm::MemoryBuffer::getMemBuffer(It.getValue());
    Invocation->getPreprocessorOpts().addRemappedFile(It.getKey(),
                                                      Input.release());
  }
  return runInvocation(BinaryName, Compilation.get(), std::move(Invocation),
                       std::move(PCHContainerOps));
}

bool ToolInvocation::runInvocation(
    const char *BinaryName, driver::Compilation *Compilation,
    std::shared_ptr<CompilerInvocation> Invocation,
    std::shared_ptr<PCHContainerOperations> PCHContainerOps) {
  // Show the invocation, with -v.
  if (Invocation->getHeaderSearchOpts().Verbose) {
    llvm::errs() << "clang Invocation:\n";
    Compilation->getJobs().Print(llvm::errs(), "\n", true);
    llvm::errs() << "\n";
  }

  return Action->runInvocation(std::move(Invocation), Files,
                               std::move(PCHContainerOps), DiagConsumer);
}

bool FrontendActionFactory::runInvocation(
    std::shared_ptr<CompilerInvocation> Invocation, FileManager *Files,
    std::shared_ptr<PCHContainerOperations> PCHContainerOps,
    DiagnosticConsumer *DiagConsumer) {
  // Create a compiler instance to handle the actual work.
  CompilerInstance Compiler(std::move(PCHContainerOps));
  Compiler.setInvocation(std::move(Invocation));
  Compiler.setFileManager(Files);

  // The FrontendAction can have lifetime requirements for Compiler or its
  // members, and we need to ensure it's deleted earlier than Compiler. So we
  // pass it to an std::unique_ptr declared after the Compiler variable.
  std::unique_ptr<FrontendAction> ScopedToolAction(create());

  // Create the compiler's actual diagnostics engine.
  Compiler.createDiagnostics(DiagConsumer, /*ShouldOwnClient=*/false);
  if (!Compiler.hasDiagnostics())
    return false;

  Compiler.createSourceManager(*Files);

  const bool Success = Compiler.ExecuteAction(*ScopedToolAction);

  Files->clearStatCache();
  return Success;
}

ClangTool::ClangTool(const CompilationDatabase &Compilations,
                     ArrayRef<std::string> SourcePaths,
                     std::shared_ptr<PCHContainerOperations> PCHContainerOps,
                     IntrusiveRefCntPtr<llvm::vfs::FileSystem> BaseFS)
    : Compilations(Compilations), SourcePaths(SourcePaths),
      PCHContainerOps(std::move(PCHContainerOps)),
      OverlayFileSystem(new llvm::vfs::OverlayFileSystem(std::move(BaseFS))),
      InMemoryFileSystem(new llvm::vfs::InMemoryFileSystem),
      Files(new FileManager(FileSystemOptions(), OverlayFileSystem)) {
  OverlayFileSystem->pushOverlay(InMemoryFileSystem);
  appendArgumentsAdjuster(getClangStripOutputAdjuster());
  appendArgumentsAdjuster(getClangSyntaxOnlyAdjuster());
  appendArgumentsAdjuster(getClangStripDependencyFileAdjuster());
}

ClangTool::~ClangTool() = default;

void ClangTool::mapVirtualFile(StringRef FilePath, StringRef Content) {
  MappedFileContents.push_back(std::make_pair(FilePath, Content));
}

void ClangTool::appendArgumentsAdjuster(ArgumentsAdjuster Adjuster) {
  ArgsAdjuster = combineAdjusters(std::move(ArgsAdjuster), std::move(Adjuster));
}

void ClangTool::clearArgumentsAdjusters() {
  ArgsAdjuster = nullptr;
}

static void injectResourceDir(CommandLineArguments &Args, const char *Argv0,
                              void *MainAddr) {
  // Allow users to override the resource dir.
  for (StringRef Arg : Args)
    if (Arg.startswith("-resource-dir"))
      return;

  // If there's no override in place add our resource dir.
  Args.push_back("-resource-dir=" +
                 CompilerInvocation::GetResourcesPath(Argv0, MainAddr));
}

int ClangTool::run(ToolAction *Action) {
  // Exists solely for the purpose of lookup of the resource path.
  // This just needs to be some symbol in the binary.
  static int StaticSymbol;

  // First insert all absolute paths into the in-memory VFS. These are global
  // for all compile commands.
  if (SeenWorkingDirectories.insert("/").second)
    for (const auto &MappedFile : MappedFileContents)
      if (llvm::sys::path::is_absolute(MappedFile.first))
        InMemoryFileSystem->addFile(
            MappedFile.first, 0,
            llvm::MemoryBuffer::getMemBuffer(MappedFile.second));

  bool ProcessingFailed = false;
  bool FileSkipped = false;
  // Compute all absolute paths before we run any actions, as those will change
  // the working directory.
  std::vector<std::string> AbsolutePaths;
  AbsolutePaths.reserve(SourcePaths.size());
  for (const auto &SourcePath : SourcePaths) {
    auto AbsPath = getAbsolutePath(*OverlayFileSystem, SourcePath);
    if (!AbsPath) {
      llvm::errs() << "Skipping " << SourcePath
                   << ". Error while getting an absolute path: "
                   << llvm::toString(AbsPath.takeError()) << "\n";
      continue;
    }
    AbsolutePaths.push_back(std::move(*AbsPath));
  }

  // Remember the working directory in case we need to restore it.
  std::string InitialWorkingDir;
  if (RestoreCWD) {
    if (auto CWD = OverlayFileSystem->getCurrentWorkingDirectory()) {
      InitialWorkingDir = std::move(*CWD);
    } else {
      llvm::errs() << "Could not get working directory: "
                   << CWD.getError().message() << "\n";
    }
  }

  for (llvm::StringRef File : AbsolutePaths) {
    // Currently implementations of CompilationDatabase::getCompileCommands can
    // change the state of the file system (e.g.  prepare generated headers), so
    // this method needs to run right before we invoke the tool, as the next
    // file may require a different (incompatible) state of the file system.
    //
    // FIXME: Make the compilation database interface more explicit about the
    // requirements to the order of invocation of its members.
    std::vector<CompileCommand> CompileCommandsForFile =
        Compilations.getCompileCommands(File);
    if (CompileCommandsForFile.empty()) {
      llvm::errs() << "Skipping " << File << ". Compile command not found.\n";
      FileSkipped = true;
      continue;
    }
    for (CompileCommand &CompileCommand : CompileCommandsForFile) {
      // FIXME: chdir is thread hostile; on the other hand, creating the same
      // behavior as chdir is complex: chdir resolves the path once, thus
      // guaranteeing that all subsequent relative path operations work
      // on the same path the original chdir resulted in. This makes a
      // difference for example on network filesystems, where symlinks might be
      // switched during runtime of the tool. Fixing this depends on having a
      // file system abstraction that allows openat() style interactions.
      if (OverlayFileSystem->setCurrentWorkingDirectory(
              CompileCommand.Directory))
        llvm::report_fatal_error("Cannot chdir into \"" +
                                 Twine(CompileCommand.Directory) + "\n!");

      // Now fill the in-memory VFS with the relative file mappings so it will
      // have the correct relative paths. We never remove mappings but that
      // should be fine.
      if (SeenWorkingDirectories.insert(CompileCommand.Directory).second)
        for (const auto &MappedFile : MappedFileContents)
          if (!llvm::sys::path::is_absolute(MappedFile.first))
            InMemoryFileSystem->addFile(
                MappedFile.first, 0,
                llvm::MemoryBuffer::getMemBuffer(MappedFile.second));

      std::vector<std::string> CommandLine = CompileCommand.CommandLine;
      if (ArgsAdjuster)
        CommandLine = ArgsAdjuster(CommandLine, CompileCommand.Filename);
      assert(!CommandLine.empty());

      // Add the resource dir based on the binary of this tool. argv[0] in the
      // compilation database may refer to a different compiler and we want to
      // pick up the very same standard library that compiler is using. The
      // builtin headers in the resource dir need to match the exact clang
      // version the tool is using.
      // FIXME: On linux, GetMainExecutable is independent of the value of the
      // first argument, thus allowing ClangTool and runToolOnCode to just
      // pass in made-up names here. Make sure this works on other platforms.
      injectResourceDir(CommandLine, "clang_tool", &StaticSymbol);

      // FIXME: We need a callback mechanism for the tool writer to output a
      // customized message for each file.
      LLVM_DEBUG({ llvm::dbgs() << "Processing: " << File << ".\n"; });
      ToolInvocation Invocation(std::move(CommandLine), Action, Files.get(),
                                PCHContainerOps);
      Invocation.setDiagnosticConsumer(DiagConsumer);

      if (!Invocation.run()) {
        // FIXME: Diagnostics should be used instead.
        llvm::errs() << "Error while processing " << File << ".\n";
        ProcessingFailed = true;
      }
    }
  }

  if (!InitialWorkingDir.empty()) {
    if (auto EC =
            OverlayFileSystem->setCurrentWorkingDirectory(InitialWorkingDir))
      llvm::errs() << "Error when trying to restore working dir: "
                   << EC.message() << "\n";
  }
  return ProcessingFailed ? 1 : (FileSkipped ? 2 : 0);
}

namespace {

class ASTBuilderAction : public ToolAction {
  std::vector<std::unique_ptr<ASTUnit>> &ASTs;

public:
  ASTBuilderAction(std::vector<std::unique_ptr<ASTUnit>> &ASTs) : ASTs(ASTs) {}

  bool runInvocation(std::shared_ptr<CompilerInvocation> Invocation,
                     FileManager *Files,
                     std::shared_ptr<PCHContainerOperations> PCHContainerOps,
                     DiagnosticConsumer *DiagConsumer) override {
    std::unique_ptr<ASTUnit> AST = ASTUnit::LoadFromCompilerInvocation(
        Invocation, std::move(PCHContainerOps),
        CompilerInstance::createDiagnostics(&Invocation->getDiagnosticOpts(),
                                            DiagConsumer,
                                            /*ShouldOwnClient=*/false),
        Files);
    if (!AST)
      return false;

    ASTs.push_back(std::move(AST));
    return true;
  }
};

} // namespace

int ClangTool::buildASTs(std::vector<std::unique_ptr<ASTUnit>> &ASTs) {
  ASTBuilderAction Action(ASTs);
  return run(&Action);
}

void ClangTool::setRestoreWorkingDir(bool RestoreCWD) {
  this->RestoreCWD = RestoreCWD;
}

namespace clang {
namespace tooling {

std::unique_ptr<ASTUnit>
buildASTFromCode(StringRef Code, StringRef FileName,
                 std::shared_ptr<PCHContainerOperations> PCHContainerOps) {
  return buildASTFromCodeWithArgs(Code, std::vector<std::string>(), FileName,
                                  "clang-tool", std::move(PCHContainerOps));
}

std::unique_ptr<ASTUnit> buildASTFromCodeWithArgs(
    StringRef Code, const std::vector<std::string> &Args, StringRef FileName,
    StringRef ToolName, std::shared_ptr<PCHContainerOperations> PCHContainerOps,
    ArgumentsAdjuster Adjuster) {
  std::vector<std::unique_ptr<ASTUnit>> ASTs;
  ASTBuilderAction Action(ASTs);
  llvm::IntrusiveRefCntPtr<llvm::vfs::OverlayFileSystem> OverlayFileSystem(
      new llvm::vfs::OverlayFileSystem(llvm::vfs::getRealFileSystem()));
  llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> InMemoryFileSystem(
      new llvm::vfs::InMemoryFileSystem);
  OverlayFileSystem->pushOverlay(InMemoryFileSystem);
  llvm::IntrusiveRefCntPtr<FileManager> Files(
      new FileManager(FileSystemOptions(), OverlayFileSystem));

  ToolInvocation Invocation(
      getSyntaxOnlyToolArgs(ToolName, Adjuster(Args, FileName), FileName),
      &Action, Files.get(), std::move(PCHContainerOps));

  InMemoryFileSystem->addFile(FileName, 0,
                              llvm::MemoryBuffer::getMemBufferCopy(Code));
  if (!Invocation.run())
    return nullptr;

  assert(ASTs.size() == 1);
  return std::move(ASTs[0]);
}

} // namespace tooling
} // namespace clang
