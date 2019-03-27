//===- Tooling.h - Framework for standalone Clang tools ---------*- C++ -*-===//
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
//  A ClangTool is initialized with a CompilationDatabase and a set of files
//  to run over. The tool will then run a user-specified FrontendAction over
//  all TUs in which the given files are compiled.
//
//  It is also possible to run a FrontendAction over a snippet of code by
//  calling runToolOnCode, which is useful for unit testing.
//
//  Applications that need more fine grained control over how to run
//  multiple FrontendActions over code can use ToolInvocation.
//
//  Example tools:
//  - running clang -fsyntax-only over source code from an editor to get
//    fast syntax checks
//  - running match/replace tools over C++ code
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_TOOLING_H
#define LLVM_CLANG_TOOLING_TOOLING_H

#include "clang/AST/ASTConsumer.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/LLVM.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Frontend/PCHContainerOperations.h"
#include "clang/Tooling/ArgumentsAdjusters.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace clang {

class CompilerInstance;
class CompilerInvocation;
class DiagnosticConsumer;
class DiagnosticsEngine;
class SourceManager;

namespace driver {

class Compilation;

} // namespace driver

namespace tooling {

class CompilationDatabase;

/// Interface to process a clang::CompilerInvocation.
///
/// If your tool is based on FrontendAction, you should be deriving from
/// FrontendActionFactory instead.
class ToolAction {
public:
  virtual ~ToolAction();

  /// Perform an action for an invocation.
  virtual bool
  runInvocation(std::shared_ptr<CompilerInvocation> Invocation,
                FileManager *Files,
                std::shared_ptr<PCHContainerOperations> PCHContainerOps,
                DiagnosticConsumer *DiagConsumer) = 0;
};

/// Interface to generate clang::FrontendActions.
///
/// Having a factory interface allows, for example, a new FrontendAction to be
/// created for each translation unit processed by ClangTool.  This class is
/// also a ToolAction which uses the FrontendActions created by create() to
/// process each translation unit.
class FrontendActionFactory : public ToolAction {
public:
  ~FrontendActionFactory() override;

  /// Invokes the compiler with a FrontendAction created by create().
  bool runInvocation(std::shared_ptr<CompilerInvocation> Invocation,
                     FileManager *Files,
                     std::shared_ptr<PCHContainerOperations> PCHContainerOps,
                     DiagnosticConsumer *DiagConsumer) override;

  /// Returns a new clang::FrontendAction.
  ///
  /// The caller takes ownership of the returned action.
  virtual FrontendAction *create() = 0;
};

/// Returns a new FrontendActionFactory for a given type.
///
/// T must derive from clang::FrontendAction.
///
/// Example:
/// FrontendActionFactory *Factory =
///   newFrontendActionFactory<clang::SyntaxOnlyAction>();
template <typename T>
std::unique_ptr<FrontendActionFactory> newFrontendActionFactory();

/// Callbacks called before and after each source file processed by a
/// FrontendAction created by the FrontedActionFactory returned by \c
/// newFrontendActionFactory.
class SourceFileCallbacks {
public:
  virtual ~SourceFileCallbacks() = default;

  /// Called before a source file is processed by a FrontEndAction.
  /// \see clang::FrontendAction::BeginSourceFileAction
  virtual bool handleBeginSource(CompilerInstance &CI) {
    return true;
  }

  /// Called after a source file is processed by a FrontendAction.
  /// \see clang::FrontendAction::EndSourceFileAction
  virtual void handleEndSource() {}
};

/// Returns a new FrontendActionFactory for any type that provides an
/// implementation of newASTConsumer().
///
/// FactoryT must implement: ASTConsumer *newASTConsumer().
///
/// Example:
/// struct ProvidesASTConsumers {
///   clang::ASTConsumer *newASTConsumer();
/// } Factory;
/// std::unique_ptr<FrontendActionFactory> FactoryAdapter(
///   newFrontendActionFactory(&Factory));
template <typename FactoryT>
inline std::unique_ptr<FrontendActionFactory> newFrontendActionFactory(
    FactoryT *ConsumerFactory, SourceFileCallbacks *Callbacks = nullptr);

/// Runs (and deletes) the tool on 'Code' with the -fsyntax-only flag.
///
/// \param ToolAction The action to run over the code.
/// \param Code C++ code.
/// \param FileName The file name which 'Code' will be mapped as.
/// \param PCHContainerOps  The PCHContainerOperations for loading and creating
///                         clang modules.
///
/// \return - True if 'ToolAction' was successfully executed.
bool runToolOnCode(FrontendAction *ToolAction, const Twine &Code,
                   const Twine &FileName = "input.cc",
                   std::shared_ptr<PCHContainerOperations> PCHContainerOps =
                       std::make_shared<PCHContainerOperations>());

/// The first part of the pair is the filename, the second part the
/// file-content.
using FileContentMappings = std::vector<std::pair<std::string, std::string>>;

/// Runs (and deletes) the tool on 'Code' with the -fsyntax-only flag and
///        with additional other flags.
///
/// \param ToolAction The action to run over the code.
/// \param Code C++ code.
/// \param Args Additional flags to pass on.
/// \param FileName The file name which 'Code' will be mapped as.
/// \param ToolName The name of the binary running the tool. Standard library
///                 header paths will be resolved relative to this.
/// \param PCHContainerOps   The PCHContainerOperations for loading and creating
///                          clang modules.
///
/// \return - True if 'ToolAction' was successfully executed.
bool runToolOnCodeWithArgs(
    FrontendAction *ToolAction, const Twine &Code,
    const std::vector<std::string> &Args, const Twine &FileName = "input.cc",
    const Twine &ToolName = "clang-tool",
    std::shared_ptr<PCHContainerOperations> PCHContainerOps =
        std::make_shared<PCHContainerOperations>(),
    const FileContentMappings &VirtualMappedFiles = FileContentMappings());

// Similar to the overload except this takes a VFS.
bool runToolOnCodeWithArgs(
    FrontendAction *ToolAction, const Twine &Code,
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS,
    const std::vector<std::string> &Args, const Twine &FileName = "input.cc",
    const Twine &ToolName = "clang-tool",
    std::shared_ptr<PCHContainerOperations> PCHContainerOps =
        std::make_shared<PCHContainerOperations>());

/// Builds an AST for 'Code'.
///
/// \param Code C++ code.
/// \param FileName The file name which 'Code' will be mapped as.
/// \param PCHContainerOps The PCHContainerOperations for loading and creating
/// clang modules.
///
/// \return The resulting AST or null if an error occurred.
std::unique_ptr<ASTUnit>
buildASTFromCode(StringRef Code, StringRef FileName = "input.cc",
                 std::shared_ptr<PCHContainerOperations> PCHContainerOps =
                     std::make_shared<PCHContainerOperations>());

/// Builds an AST for 'Code' with additional flags.
///
/// \param Code C++ code.
/// \param Args Additional flags to pass on.
/// \param FileName The file name which 'Code' will be mapped as.
/// \param ToolName The name of the binary running the tool. Standard library
///                 header paths will be resolved relative to this.
/// \param PCHContainerOps The PCHContainerOperations for loading and creating
/// clang modules.
///
/// \param Adjuster A function to filter the command line arguments as specified.
///
/// \return The resulting AST or null if an error occurred.
std::unique_ptr<ASTUnit> buildASTFromCodeWithArgs(
    StringRef Code, const std::vector<std::string> &Args,
    StringRef FileName = "input.cc", StringRef ToolName = "clang-tool",
    std::shared_ptr<PCHContainerOperations> PCHContainerOps =
        std::make_shared<PCHContainerOperations>(),
    ArgumentsAdjuster Adjuster = getClangStripDependencyFileAdjuster());

/// Utility to run a FrontendAction in a single clang invocation.
class ToolInvocation {
public:
  /// Create a tool invocation.
  ///
  /// \param CommandLine The command line arguments to clang. Note that clang
  /// uses its binary name (CommandLine[0]) to locate its builtin headers.
  /// Callers have to ensure that they are installed in a compatible location
  /// (see clang driver implementation) or mapped in via mapVirtualFile.
  /// \param FAction The action to be executed. Class takes ownership.
  /// \param Files The FileManager used for the execution. Class does not take
  /// ownership.
  /// \param PCHContainerOps The PCHContainerOperations for loading and creating
  /// clang modules.
  ToolInvocation(std::vector<std::string> CommandLine, FrontendAction *FAction,
                 FileManager *Files,
                 std::shared_ptr<PCHContainerOperations> PCHContainerOps =
                     std::make_shared<PCHContainerOperations>());

  /// Create a tool invocation.
  ///
  /// \param CommandLine The command line arguments to clang.
  /// \param Action The action to be executed.
  /// \param Files The FileManager used for the execution.
  /// \param PCHContainerOps The PCHContainerOperations for loading and creating
  /// clang modules.
  ToolInvocation(std::vector<std::string> CommandLine, ToolAction *Action,
                 FileManager *Files,
                 std::shared_ptr<PCHContainerOperations> PCHContainerOps);

  ~ToolInvocation();

  /// Set a \c DiagnosticConsumer to use during parsing.
  void setDiagnosticConsumer(DiagnosticConsumer *DiagConsumer) {
    this->DiagConsumer = DiagConsumer;
  }

  /// Map a virtual file to be used while running the tool.
  ///
  /// \param FilePath The path at which the content will be mapped.
  /// \param Content A null terminated buffer of the file's content.
  // FIXME: remove this when all users have migrated!
  void mapVirtualFile(StringRef FilePath, StringRef Content);

  /// Run the clang invocation.
  ///
  /// \returns True if there were no errors during execution.
  bool run();

 private:
  void addFileMappingsTo(SourceManager &SourceManager);

  bool runInvocation(const char *BinaryName,
                     driver::Compilation *Compilation,
                     std::shared_ptr<CompilerInvocation> Invocation,
                     std::shared_ptr<PCHContainerOperations> PCHContainerOps);

  std::vector<std::string> CommandLine;
  ToolAction *Action;
  bool OwnsAction;
  FileManager *Files;
  std::shared_ptr<PCHContainerOperations> PCHContainerOps;
  // Maps <file name> -> <file content>.
  llvm::StringMap<StringRef> MappedFileContents;
  DiagnosticConsumer *DiagConsumer = nullptr;
};

/// Utility to run a FrontendAction over a set of files.
///
/// This class is written to be usable for command line utilities.
/// By default the class uses ClangSyntaxOnlyAdjuster to modify
/// command line arguments before the arguments are used to run
/// a frontend action. One could install an additional command line
/// arguments adjuster by calling the appendArgumentsAdjuster() method.
class ClangTool {
public:
  /// Constructs a clang tool to run over a list of files.
  ///
  /// \param Compilations The CompilationDatabase which contains the compile
  ///        command lines for the given source paths.
  /// \param SourcePaths The source files to run over. If a source files is
  ///        not found in Compilations, it is skipped.
  /// \param PCHContainerOps The PCHContainerOperations for loading and creating
  /// clang modules.
  /// \param BaseFS VFS used for all underlying file accesses when running the
  /// tool.
  ClangTool(const CompilationDatabase &Compilations,
            ArrayRef<std::string> SourcePaths,
            std::shared_ptr<PCHContainerOperations> PCHContainerOps =
                std::make_shared<PCHContainerOperations>(),
            IntrusiveRefCntPtr<llvm::vfs::FileSystem> BaseFS =
                llvm::vfs::getRealFileSystem());

  ~ClangTool();

  /// Set a \c DiagnosticConsumer to use during parsing.
  void setDiagnosticConsumer(DiagnosticConsumer *DiagConsumer) {
    this->DiagConsumer = DiagConsumer;
  }

  /// Map a virtual file to be used while running the tool.
  ///
  /// \param FilePath The path at which the content will be mapped.
  /// \param Content A null terminated buffer of the file's content.
  void mapVirtualFile(StringRef FilePath, StringRef Content);

  /// Append a command line arguments adjuster to the adjuster chain.
  ///
  /// \param Adjuster An argument adjuster, which will be run on the output of
  ///        previous argument adjusters.
  void appendArgumentsAdjuster(ArgumentsAdjuster Adjuster);

  /// Clear the command line arguments adjuster chain.
  void clearArgumentsAdjusters();

  /// Runs an action over all files specified in the command line.
  ///
  /// \param Action Tool action.
  ///
  /// \returns 0 on success; 1 if any error occurred; 2 if there is no error but
  /// some files are skipped due to missing compile commands.
  int run(ToolAction *Action);

  /// Create an AST for each file specified in the command line and
  /// append them to ASTs.
  int buildASTs(std::vector<std::unique_ptr<ASTUnit>> &ASTs);

  /// Sets whether working directory should be restored after calling run(). By
  /// default, working directory is restored. However, it could be useful to
  /// turn this off when running on multiple threads to avoid the raciness.
  void setRestoreWorkingDir(bool RestoreCWD);

  /// Returns the file manager used in the tool.
  ///
  /// The file manager is shared between all translation units.
  FileManager &getFiles() { return *Files; }

  llvm::ArrayRef<std::string> getSourcePaths() const { return SourcePaths; }

private:
  const CompilationDatabase &Compilations;
  std::vector<std::string> SourcePaths;
  std::shared_ptr<PCHContainerOperations> PCHContainerOps;

  llvm::IntrusiveRefCntPtr<llvm::vfs::OverlayFileSystem> OverlayFileSystem;
  llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem> InMemoryFileSystem;
  llvm::IntrusiveRefCntPtr<FileManager> Files;

  // Contains a list of pairs (<file name>, <file content>).
  std::vector<std::pair<StringRef, StringRef>> MappedFileContents;

  llvm::StringSet<> SeenWorkingDirectories;

  ArgumentsAdjuster ArgsAdjuster;

  DiagnosticConsumer *DiagConsumer = nullptr;

  bool RestoreCWD = true;
};

template <typename T>
std::unique_ptr<FrontendActionFactory> newFrontendActionFactory() {
  class SimpleFrontendActionFactory : public FrontendActionFactory {
  public:
    FrontendAction *create() override { return new T; }
  };

  return std::unique_ptr<FrontendActionFactory>(
      new SimpleFrontendActionFactory);
}

template <typename FactoryT>
inline std::unique_ptr<FrontendActionFactory> newFrontendActionFactory(
    FactoryT *ConsumerFactory, SourceFileCallbacks *Callbacks) {
  class FrontendActionFactoryAdapter : public FrontendActionFactory {
  public:
    explicit FrontendActionFactoryAdapter(FactoryT *ConsumerFactory,
                                          SourceFileCallbacks *Callbacks)
        : ConsumerFactory(ConsumerFactory), Callbacks(Callbacks) {}

    FrontendAction *create() override {
      return new ConsumerFactoryAdaptor(ConsumerFactory, Callbacks);
    }

  private:
    class ConsumerFactoryAdaptor : public ASTFrontendAction {
    public:
      ConsumerFactoryAdaptor(FactoryT *ConsumerFactory,
                             SourceFileCallbacks *Callbacks)
          : ConsumerFactory(ConsumerFactory), Callbacks(Callbacks) {}

      std::unique_ptr<ASTConsumer>
      CreateASTConsumer(CompilerInstance &, StringRef) override {
        return ConsumerFactory->newASTConsumer();
      }

    protected:
      bool BeginSourceFileAction(CompilerInstance &CI) override {
        if (!ASTFrontendAction::BeginSourceFileAction(CI))
          return false;
        if (Callbacks)
          return Callbacks->handleBeginSource(CI);
        return true;
      }

      void EndSourceFileAction() override {
        if (Callbacks)
          Callbacks->handleEndSource();
        ASTFrontendAction::EndSourceFileAction();
      }

    private:
      FactoryT *ConsumerFactory;
      SourceFileCallbacks *Callbacks;
    };
    FactoryT *ConsumerFactory;
    SourceFileCallbacks *Callbacks;
  };

  return std::unique_ptr<FrontendActionFactory>(
      new FrontendActionFactoryAdapter(ConsumerFactory, Callbacks));
}

/// Returns the absolute path of \c File, by prepending it with
/// the current directory if \c File is not absolute.
///
/// Otherwise returns \c File.
/// If 'File' starts with "./", the returned path will not contain the "./".
/// Otherwise, the returned path will contain the literal path-concatenation of
/// the current directory and \c File.
///
/// The difference to llvm::sys::fs::make_absolute is the canonicalization this
/// does by removing "./" and computing native paths.
///
/// \param File Either an absolute or relative path.
std::string getAbsolutePath(StringRef File);

/// An overload of getAbsolutePath that works over the provided \p FS.
llvm::Expected<std::string> getAbsolutePath(llvm::vfs::FileSystem &FS,
                                            StringRef File);

/// Changes CommandLine to contain implicit flags that would have been
/// defined had the compiler driver been invoked through the path InvokedAs.
///
/// For example, when called with \c InvokedAs set to `i686-linux-android-g++`,
/// the arguments '-target', 'i686-linux-android`, `--driver-mode=g++` will
/// be inserted after the first argument in \c CommandLine.
///
/// This function will not add new `-target` or `--driver-mode` flags if they
/// are already present in `CommandLine` (even if they have different settings
/// than would have been inserted).
///
/// \pre `llvm::InitializeAllTargets()` has been called.
///
/// \param CommandLine the command line used to invoke the compiler driver or
/// Clang tool, including the path to the executable as \c CommandLine[0].
/// \param InvokedAs the path to the driver used to infer implicit flags.
///
/// \note This will not set \c CommandLine[0] to \c InvokedAs. The tooling
/// infrastructure expects that CommandLine[0] is a tool path relative to which
/// the builtin headers can be found.
void addTargetAndModeForProgramName(std::vector<std::string> &CommandLine,
                                    StringRef InvokedAs);

/// Creates a \c CompilerInvocation.
CompilerInvocation *newInvocation(DiagnosticsEngine *Diagnostics,
                                  const llvm::opt::ArgStringList &CC1Args);

} // namespace tooling

} // namespace clang

#endif // LLVM_CLANG_TOOLING_TOOLING_H
