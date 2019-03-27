//===- CheckerRegistry.h - Maintains all available checkers -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_CHECKERREGISTRY_H
#define LLVM_CLANG_STATICANALYZER_CORE_CHECKERREGISTRY_H

#include "clang/Basic/LLVM.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include <cstddef>
#include <vector>

// FIXME: move this information to an HTML file in docs/.
// At the very least, a checker plugin is a dynamic library that exports
// clang_analyzerAPIVersionString. This should be defined as follows:
//
//   extern "C"
//   const char clang_analyzerAPIVersionString[] =
//     CLANG_ANALYZER_API_VERSION_STRING;
//
// This is used to check whether the current version of the analyzer is known to
// be incompatible with a plugin. Plugins with incompatible version strings,
// or without a version string at all, will not be loaded.
//
// To add a custom checker to the analyzer, the plugin must also define the
// function clang_registerCheckers. For example:
//
//    extern "C"
//    void clang_registerCheckers (CheckerRegistry &registry) {
//      registry.addChecker<MainCallChecker>("example.MainCallChecker",
//        "Disallows calls to functions called main");
//    }
//
// The first method argument is the full name of the checker, including its
// enclosing package. By convention, the registered name of a checker is the
// name of the associated class (the template argument).
// The second method argument is a short human-readable description of the
// checker.
//
// The clang_registerCheckers function may add any number of checkers to the
// registry. If any checkers require additional initialization, use the three-
// argument form of CheckerRegistry::addChecker.
//
// To load a checker plugin, specify the full path to the dynamic library as
// the argument to the -load option in the cc1 frontend. You can then enable
// your custom checker using the -analyzer-checker:
//
//   clang -cc1 -load </path/to/plugin.dylib> -analyze
//     -analyzer-checker=<example.MainCallChecker>
//
// For a complete working example, see examples/analyzer-plugin.

#ifndef CLANG_ANALYZER_API_VERSION_STRING
// FIXME: The Clang version string is not particularly granular;
// the analyzer infrastructure can change a lot between releases.
// Unfortunately, this string has to be statically embedded in each plugin,
// so we can't just use the functions defined in Version.h.
#include "clang/Basic/Version.h"
#define CLANG_ANALYZER_API_VERSION_STRING CLANG_VERSION_STRING
#endif

namespace clang {

class AnalyzerOptions;
class DiagnosticsEngine;

namespace ento {

/// Manages a set of available checkers for running a static analysis.
/// The checkers are organized into packages by full name, where including
/// a package will recursively include all subpackages and checkers within it.
/// For example, the checker "core.builtin.NoReturnFunctionChecker" will be
/// included if initializeManager() is called with an option of "core",
/// "core.builtin", or the full name "core.builtin.NoReturnFunctionChecker".
class CheckerRegistry {
public:
  CheckerRegistry(ArrayRef<std::string> plugins, DiagnosticsEngine &diags);

  /// Initialization functions perform any necessary setup for a checker.
  /// They should include a call to CheckerManager::registerChecker.
  using InitializationFunction = void (*)(CheckerManager &);

  struct CheckerInfo {
    InitializationFunction Initialize;
    StringRef FullName;
    StringRef Desc;
    StringRef DocumentationUri;

    CheckerInfo(InitializationFunction Fn, StringRef Name, StringRef Desc,
                StringRef DocsUri)
        : Initialize(Fn), FullName(Name), Desc(Desc),
          DocumentationUri(DocsUri) {}
  };

  using CheckerInfoList = std::vector<CheckerInfo>;
  using CheckerInfoSet = llvm::SetVector<const CheckerRegistry::CheckerInfo *>;

private:
  template <typename T>
  static void initializeManager(CheckerManager &mgr) {
    mgr.registerChecker<T>();
  }

public:
  /// Adds a checker to the registry. Use this non-templated overload when your
  /// checker requires custom initialization.
  void addChecker(InitializationFunction Fn, StringRef FullName, StringRef Desc,
                  StringRef DocsUri);

  /// Adds a checker to the registry. Use this templated overload when your
  /// checker does not require any custom initialization.
  template <class T>
  void addChecker(StringRef FullName, StringRef Desc, StringRef DocsUri) {
    // Avoid MSVC's Compiler Error C2276:
    // http://msdn.microsoft.com/en-us/library/850cstw1(v=VS.80).aspx
    addChecker(&CheckerRegistry::initializeManager<T>, FullName, Desc, DocsUri);
  }

  /// Initializes a CheckerManager by calling the initialization functions for
  /// all checkers specified by the given CheckerOptInfo list. The order of this
  /// list is significant; later options can be used to reverse earlier ones.
  /// This can be used to exclude certain checkers in an included package.
  void initializeManager(CheckerManager &mgr,
                         const AnalyzerOptions &Opts) const;

  /// Check if every option corresponds to a specific checker or package.
  void validateCheckerOptions(const AnalyzerOptions &opts) const;

  /// Prints the name and description of all checkers in this registry.
  /// This output is not intended to be machine-parseable.
  void printHelp(raw_ostream &out, size_t maxNameChars = 30) const;
  void printList(raw_ostream &out, const AnalyzerOptions &opts) const;

private:
  CheckerInfoSet getEnabledCheckers(const AnalyzerOptions &Opts) const;

  mutable CheckerInfoList Checkers;
  mutable llvm::StringMap<size_t> Packages;
  DiagnosticsEngine &Diags;
};

} // namespace ento

} // namespace clang

#endif // LLVM_CLANG_STATICANALYZER_CORE_CHECKERREGISTRY_H
