//===- Utils.h - Misc utilities for the front-end ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This header contains miscellaneous utilities for various front-end actions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_UTILS_H
#define LLVM_CLANG_FRONTEND_UTILS_H

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Option/OptSpecifier.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <cstdint>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace llvm {

class Triple;

namespace opt {

class ArgList;

} // namespace opt

} // namespace llvm

namespace clang {

class ASTReader;
class CompilerInstance;
class CompilerInvocation;
class DependencyOutputOptions;
class DiagnosticsEngine;
class ExternalSemaSource;
class FrontendOptions;
class HeaderSearch;
class HeaderSearchOptions;
class LangOptions;
class PCHContainerReader;
class Preprocessor;
class PreprocessorOptions;
class PreprocessorOutputOptions;

/// Apply the header search options to get given HeaderSearch object.
void ApplyHeaderSearchOptions(HeaderSearch &HS,
                              const HeaderSearchOptions &HSOpts,
                              const LangOptions &Lang,
                              const llvm::Triple &triple);

/// InitializePreprocessor - Initialize the preprocessor getting it and the
/// environment ready to process a single file.
void InitializePreprocessor(Preprocessor &PP, const PreprocessorOptions &PPOpts,
                            const PCHContainerReader &PCHContainerRdr,
                            const FrontendOptions &FEOpts);

/// DoPrintPreprocessedInput - Implement -E mode.
void DoPrintPreprocessedInput(Preprocessor &PP, raw_ostream *OS,
                              const PreprocessorOutputOptions &Opts);

/// An interface for collecting the dependencies of a compilation. Users should
/// use \c attachToPreprocessor and \c attachToASTReader to get all of the
/// dependencies.
/// FIXME: Migrate DependencyFileGen and DependencyGraphGen to use this
/// interface.
class DependencyCollector {
public:
  virtual ~DependencyCollector();

  virtual void attachToPreprocessor(Preprocessor &PP);
  virtual void attachToASTReader(ASTReader &R);
  ArrayRef<std::string> getDependencies() const { return Dependencies; }

  /// Called when a new file is seen. Return true if \p Filename should be added
  /// to the list of dependencies.
  ///
  /// The default implementation ignores <built-in> and system files.
  virtual bool sawDependency(StringRef Filename, bool FromModule,
                             bool IsSystem, bool IsModuleFile, bool IsMissing);

  /// Called when the end of the main file is reached.
  virtual void finishedMainFile() {}

  /// Return true if system files should be passed to sawDependency().
  virtual bool needSystemDependencies() { return false; }

  // implementation detail
  /// Add a dependency \p Filename if it has not been seen before and
  /// sawDependency() returns true.
  void maybeAddDependency(StringRef Filename, bool FromModule, bool IsSystem,
                          bool IsModuleFile, bool IsMissing);

private:
  llvm::StringSet<> Seen;
  std::vector<std::string> Dependencies;
};

/// Builds a depdenency file when attached to a Preprocessor (for includes) and
/// ASTReader (for module imports), and writes it out at the end of processing
/// a source file.  Users should attach to the ast reader whenever a module is
/// loaded.
class DependencyFileGenerator {
  void *Impl; // Opaque implementation

  DependencyFileGenerator(void *Impl);

public:
  static DependencyFileGenerator *CreateAndAttachToPreprocessor(
    Preprocessor &PP, const DependencyOutputOptions &Opts);

  void AttachToASTReader(ASTReader &R);
};

/// Collects the dependencies for imported modules into a directory.  Users
/// should attach to the AST reader whenever a module is loaded.
class ModuleDependencyCollector : public DependencyCollector {
  std::string DestDir;
  bool HasErrors = false;
  llvm::StringSet<> Seen;
  llvm::vfs::YAMLVFSWriter VFSWriter;
  llvm::StringMap<std::string> SymLinkMap;

  bool getRealPath(StringRef SrcPath, SmallVectorImpl<char> &Result);
  std::error_code copyToRoot(StringRef Src, StringRef Dst = {});

public:
  ModuleDependencyCollector(std::string DestDir)
      : DestDir(std::move(DestDir)) {}
  ~ModuleDependencyCollector() override { writeFileMap(); }

  StringRef getDest() { return DestDir; }
  bool insertSeen(StringRef Filename) { return Seen.insert(Filename).second; }
  void addFile(StringRef Filename, StringRef FileDst = {});

  void addFileMapping(StringRef VPath, StringRef RPath) {
    VFSWriter.addFileMapping(VPath, RPath);
  }

  void attachToPreprocessor(Preprocessor &PP) override;
  void attachToASTReader(ASTReader &R) override;

  void writeFileMap();
  bool hasErrors() { return HasErrors; }
};

/// AttachDependencyGraphGen - Create a dependency graph generator, and attach
/// it to the given preprocessor.
void AttachDependencyGraphGen(Preprocessor &PP, StringRef OutputFile,
                              StringRef SysRoot);

/// AttachHeaderIncludeGen - Create a header include list generator, and attach
/// it to the given preprocessor.
///
/// \param DepOpts - Options controlling the output.
/// \param ShowAllHeaders - If true, show all header information instead of just
/// headers following the predefines buffer. This is useful for making sure
/// includes mentioned on the command line are also reported, but differs from
/// the default behavior used by -H.
/// \param OutputPath - If non-empty, a path to write the header include
/// information to, instead of writing to stderr.
/// \param ShowDepth - Whether to indent to show the nesting of the includes.
/// \param MSStyle - Whether to print in cl.exe /showIncludes style.
void AttachHeaderIncludeGen(Preprocessor &PP,
                            const DependencyOutputOptions &DepOpts,
                            bool ShowAllHeaders = false,
                            StringRef OutputPath = {},
                            bool ShowDepth = true, bool MSStyle = false);

/// The ChainedIncludesSource class converts headers to chained PCHs in
/// memory, mainly for testing.
IntrusiveRefCntPtr<ExternalSemaSource>
createChainedIncludesSource(CompilerInstance &CI,
                            IntrusiveRefCntPtr<ExternalSemaSource> &Reader);

/// createInvocationFromCommandLine - Construct a compiler invocation object for
/// a command line argument vector.
///
/// \return A CompilerInvocation, or 0 if none was built for the given
/// argument vector.
std::unique_ptr<CompilerInvocation> createInvocationFromCommandLine(
    ArrayRef<const char *> Args,
    IntrusiveRefCntPtr<DiagnosticsEngine> Diags =
        IntrusiveRefCntPtr<DiagnosticsEngine>(),
    IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS = nullptr);

/// Return the value of the last argument as an integer, or a default. If Diags
/// is non-null, emits an error if the argument is given, but non-integral.
int getLastArgIntValue(const llvm::opt::ArgList &Args,
                       llvm::opt::OptSpecifier Id, int Default,
                       DiagnosticsEngine *Diags = nullptr);

inline int getLastArgIntValue(const llvm::opt::ArgList &Args,
                              llvm::opt::OptSpecifier Id, int Default,
                              DiagnosticsEngine &Diags) {
  return getLastArgIntValue(Args, Id, Default, &Diags);
}

uint64_t getLastArgUInt64Value(const llvm::opt::ArgList &Args,
                               llvm::opt::OptSpecifier Id, uint64_t Default,
                               DiagnosticsEngine *Diags = nullptr);

inline uint64_t getLastArgUInt64Value(const llvm::opt::ArgList &Args,
                                      llvm::opt::OptSpecifier Id,
                                      uint64_t Default,
                                      DiagnosticsEngine &Diags) {
  return getLastArgUInt64Value(Args, Id, Default, &Diags);
}

// Frontend timing utils

/// If the user specifies the -ftime-report argument on an Clang command line
/// then the value of this boolean will be true, otherwise false.
extern bool FrontendTimesIsEnabled;

} // namespace clang

#endif // LLVM_CLANG_FRONTEND_UTILS_H
