//===- CompilerInvocation.h - Compiler Invocation Helper Data ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_COMPILERINVOCATION_H
#define LLVM_CLANG_FRONTEND_COMPILERINVOCATION_H

#include "clang/Basic/CodeGenOptions.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/FileSystemOptions.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Frontend/DependencyOutputOptions.h"
#include "clang/Frontend/FrontendOptions.h"
#include "clang/Frontend/LangStandard.h"
#include "clang/Frontend/MigratorOptions.h"
#include "clang/Frontend/PreprocessorOutputOptions.h"
#include "clang/StaticAnalyzer/Core/AnalyzerOptions.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include <memory>
#include <string>

namespace llvm {

class Triple;

namespace opt {

class ArgList;

} // namespace opt

namespace vfs {

class FileSystem;

} // namespace vfs

} // namespace llvm

namespace clang {

class DiagnosticsEngine;
class HeaderSearchOptions;
class PreprocessorOptions;
class TargetOptions;

/// Fill out Opts based on the options given in Args.
///
/// Args must have been created from the OptTable returned by
/// createCC1OptTable().
///
/// When errors are encountered, return false and, if Diags is non-null,
/// report the error(s).
bool ParseDiagnosticArgs(DiagnosticOptions &Opts, llvm::opt::ArgList &Args,
                         DiagnosticsEngine *Diags = nullptr,
                         bool DefaultDiagColor = true,
                         bool DefaultShowOpt = true);

class CompilerInvocationBase {
public:
  /// Options controlling the language variant.
  std::shared_ptr<LangOptions> LangOpts;

  /// Options controlling the target.
  std::shared_ptr<TargetOptions> TargetOpts;

  /// Options controlling the diagnostic engine.
  IntrusiveRefCntPtr<DiagnosticOptions> DiagnosticOpts;

  /// Options controlling the \#include directive.
  std::shared_ptr<HeaderSearchOptions> HeaderSearchOpts;

  /// Options controlling the preprocessor (aside from \#include handling).
  std::shared_ptr<PreprocessorOptions> PreprocessorOpts;

  CompilerInvocationBase();
  CompilerInvocationBase(const CompilerInvocationBase &X);
  CompilerInvocationBase &operator=(const CompilerInvocationBase &) = delete;
  ~CompilerInvocationBase();

  LangOptions *getLangOpts() { return LangOpts.get(); }
  const LangOptions *getLangOpts() const { return LangOpts.get(); }

  TargetOptions &getTargetOpts() { return *TargetOpts.get(); }
  const TargetOptions &getTargetOpts() const { return *TargetOpts.get(); }

  DiagnosticOptions &getDiagnosticOpts() const { return *DiagnosticOpts; }

  HeaderSearchOptions &getHeaderSearchOpts() { return *HeaderSearchOpts; }

  const HeaderSearchOptions &getHeaderSearchOpts() const {
    return *HeaderSearchOpts;
  }

  std::shared_ptr<HeaderSearchOptions> getHeaderSearchOptsPtr() const {
    return HeaderSearchOpts;
  }

  std::shared_ptr<PreprocessorOptions> getPreprocessorOptsPtr() {
    return PreprocessorOpts;
  }

  PreprocessorOptions &getPreprocessorOpts() { return *PreprocessorOpts; }

  const PreprocessorOptions &getPreprocessorOpts() const {
    return *PreprocessorOpts;
  }
};

/// Helper class for holding the data necessary to invoke the compiler.
///
/// This class is designed to represent an abstract "invocation" of the
/// compiler, including data such as the include paths, the code generation
/// options, the warning flags, and so on.
class CompilerInvocation : public CompilerInvocationBase {
  /// Options controlling the static analyzer.
  AnalyzerOptionsRef AnalyzerOpts;

  MigratorOptions MigratorOpts;

  /// Options controlling IRgen and the backend.
  CodeGenOptions CodeGenOpts;

  /// Options controlling dependency output.
  DependencyOutputOptions DependencyOutputOpts;

  /// Options controlling file system operations.
  FileSystemOptions FileSystemOpts;

  /// Options controlling the frontend itself.
  FrontendOptions FrontendOpts;

  /// Options controlling preprocessed output.
  PreprocessorOutputOptions PreprocessorOutputOpts;

public:
  CompilerInvocation() : AnalyzerOpts(new AnalyzerOptions()) {}

  /// @name Utility Methods
  /// @{

  /// Create a compiler invocation from a list of input options.
  /// \returns true on success.
  ///
  /// \param [out] Res - The resulting invocation.
  /// \param ArgBegin - The first element in the argument vector.
  /// \param ArgEnd - The last element in the argument vector.
  /// \param Diags - The diagnostic engine to use for errors.
  static bool CreateFromArgs(CompilerInvocation &Res,
                             const char* const *ArgBegin,
                             const char* const *ArgEnd,
                             DiagnosticsEngine &Diags);

  /// Get the directory where the compiler headers
  /// reside, relative to the compiler binary (found by the passed in
  /// arguments).
  ///
  /// \param Argv0 - The program path (from argv[0]), for finding the builtin
  /// compiler path.
  /// \param MainAddr - The address of main (or some other function in the main
  /// executable), for finding the builtin compiler path.
  static std::string GetResourcesPath(const char *Argv0, void *MainAddr);

  /// Set language defaults for the given input language and
  /// language standard in the given LangOptions object.
  ///
  /// \param Opts - The LangOptions object to set up.
  /// \param IK - The input language.
  /// \param T - The target triple.
  /// \param PPOpts - The PreprocessorOptions affected.
  /// \param LangStd - The input language standard.
  static void setLangDefaults(LangOptions &Opts, InputKind IK,
                   const llvm::Triple &T, PreprocessorOptions &PPOpts,
                   LangStandard::Kind LangStd = LangStandard::lang_unspecified);

  /// Retrieve a module hash string that is suitable for uniquely
  /// identifying the conditions under which the module was built.
  std::string getModuleHash() const;

  /// @}
  /// @name Option Subgroups
  /// @{

  AnalyzerOptionsRef getAnalyzerOpts() const { return AnalyzerOpts; }

  MigratorOptions &getMigratorOpts() { return MigratorOpts; }
  const MigratorOptions &getMigratorOpts() const { return MigratorOpts; }

  CodeGenOptions &getCodeGenOpts() { return CodeGenOpts; }
  const CodeGenOptions &getCodeGenOpts() const { return CodeGenOpts; }

  DependencyOutputOptions &getDependencyOutputOpts() {
    return DependencyOutputOpts;
  }

  const DependencyOutputOptions &getDependencyOutputOpts() const {
    return DependencyOutputOpts;
  }

  FileSystemOptions &getFileSystemOpts() { return FileSystemOpts; }

  const FileSystemOptions &getFileSystemOpts() const {
    return FileSystemOpts;
  }

  FrontendOptions &getFrontendOpts() { return FrontendOpts; }
  const FrontendOptions &getFrontendOpts() const { return FrontendOpts; }

  PreprocessorOutputOptions &getPreprocessorOutputOpts() {
    return PreprocessorOutputOpts;
  }

  const PreprocessorOutputOptions &getPreprocessorOutputOpts() const {
    return PreprocessorOutputOpts;
  }

  /// @}
};

IntrusiveRefCntPtr<llvm::vfs::FileSystem>
createVFSFromCompilerInvocation(const CompilerInvocation &CI,
                                DiagnosticsEngine &Diags);

IntrusiveRefCntPtr<llvm::vfs::FileSystem> createVFSFromCompilerInvocation(
    const CompilerInvocation &CI, DiagnosticsEngine &Diags,
    IntrusiveRefCntPtr<llvm::vfs::FileSystem> BaseFS);

} // namespace clang

#endif // LLVM_CLANG_FRONTEND_COMPILERINVOCATION_H
