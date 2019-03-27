//===- AnalyzerOptions.h - Analysis Engine Options --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This header defines various options for the static analyzer that are set
// by the frontend and are consulted throughout the analyzer.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_ANALYZEROPTIONS_H
#define LLVM_CLANG_STATICANALYZER_CORE_ANALYZEROPTIONS_H

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include <string>
#include <utility>
#include <vector>

namespace clang {

namespace ento {

class CheckerBase;

} // namespace ento

/// Analysis - Set of available source code analyses.
enum Analyses {
#define ANALYSIS(NAME, CMDFLAG, DESC, SCOPE) NAME,
#include "clang/StaticAnalyzer/Core/Analyses.def"
NumAnalyses
};

/// AnalysisStores - Set of available analysis store models.
enum AnalysisStores {
#define ANALYSIS_STORE(NAME, CMDFLAG, DESC, CREATFN) NAME##Model,
#include "clang/StaticAnalyzer/Core/Analyses.def"
NumStores
};

/// AnalysisConstraints - Set of available constraint models.
enum AnalysisConstraints {
#define ANALYSIS_CONSTRAINTS(NAME, CMDFLAG, DESC, CREATFN) NAME##Model,
#include "clang/StaticAnalyzer/Core/Analyses.def"
NumConstraints
};

/// AnalysisDiagClients - Set of available diagnostic clients for rendering
///  analysis results.
enum AnalysisDiagClients {
#define ANALYSIS_DIAGNOSTICS(NAME, CMDFLAG, DESC, CREATFN) PD_##NAME,
#include "clang/StaticAnalyzer/Core/Analyses.def"
PD_NONE,
NUM_ANALYSIS_DIAG_CLIENTS
};

/// AnalysisPurgeModes - Set of available strategies for dead symbol removal.
enum AnalysisPurgeMode {
#define ANALYSIS_PURGE(NAME, CMDFLAG, DESC) NAME,
#include "clang/StaticAnalyzer/Core/Analyses.def"
NumPurgeModes
};

/// AnalysisInlineFunctionSelection - Set of inlining function selection heuristics.
enum AnalysisInliningMode {
#define ANALYSIS_INLINING_MODE(NAME, CMDFLAG, DESC) NAME,
#include "clang/StaticAnalyzer/Core/Analyses.def"
NumInliningModes
};

/// Describes the different kinds of C++ member functions which can be
/// considered for inlining by the analyzer.
///
/// These options are cumulative; enabling one kind of member function will
/// enable all kinds with lower enum values.
enum CXXInlineableMemberKind {
  // Uninitialized = 0,

  /// A dummy mode in which no C++ inlining is enabled.
  CIMK_None,

  /// Refers to regular member function and operator calls.
  CIMK_MemberFunctions,

  /// Refers to constructors (implicit or explicit).
  ///
  /// Note that a constructor will not be inlined if the corresponding
  /// destructor is non-trivial.
  CIMK_Constructors,

  /// Refers to destructors (implicit or explicit).
  CIMK_Destructors
};

/// Describes the different modes of inter-procedural analysis.
enum IPAKind {
  /// Perform only intra-procedural analysis.
  IPAK_None = 1,

  /// Inline C functions and blocks when their definitions are available.
  IPAK_BasicInlining = 2,

  /// Inline callees(C, C++, ObjC) when their definitions are available.
  IPAK_Inlining = 3,

  /// Enable inlining of dynamically dispatched methods.
  IPAK_DynamicDispatch = 4,

  /// Enable inlining of dynamically dispatched methods, bifurcate paths when
  /// exact type info is unavailable.
  IPAK_DynamicDispatchBifurcate = 5
};

enum class ExplorationStrategyKind {
  DFS,
  BFS,
  UnexploredFirst,
  UnexploredFirstQueue,
  UnexploredFirstLocationQueue,
  BFSBlockDFSContents,
};

/// Describes the kinds for high-level analyzer mode.
enum UserModeKind {
  /// Perform shallow but fast analyzes.
  UMK_Shallow = 1,

  /// Perform deep analyzes.
  UMK_Deep = 2
};

/// Stores options for the analyzer from the command line.
///
/// Some options are frontend flags (e.g.: -analyzer-output), but some are
/// analyzer configuration options, which are preceded by -analyzer-config
/// (e.g.: -analyzer-config notes-as-events=true).
///
/// If you'd like to add a new frontend flag, add it to
/// include/clang/Driver/CC1Options.td, add a new field to store the value of
/// that flag in this class, and initialize it in
/// lib/Frontend/CompilerInvocation.cpp.
///
/// If you'd like to add a new non-checker configuration, register it in
/// include/clang/StaticAnalyzer/Core/AnalyzerOptions.def, and refer to the
/// top of the file for documentation.
///
/// If you'd like to add a new checker option, call getChecker*Option()
/// whenever.
///
/// Some of the options are controlled by raw frontend flags for no good reason,
/// and should be eventually converted into -analyzer-config flags. New analyzer
/// options should not be implemented as frontend flags. Frontend flags still
/// make sense for things that do not affect the actual analysis.
class AnalyzerOptions : public RefCountedBase<AnalyzerOptions> {
public:
  using ConfigTable = llvm::StringMap<std::string>;

  static std::vector<StringRef>
  getRegisteredCheckers(bool IncludeExperimental = false);

  /// Pair of checker name and enable/disable.
  std::vector<std::pair<std::string, bool>> CheckersControlList;

  /// A key-value table of use-specified configuration values.
  // TODO: This shouldn't be public.
  ConfigTable Config;
  AnalysisStores AnalysisStoreOpt = RegionStoreModel;
  AnalysisConstraints AnalysisConstraintsOpt = RangeConstraintsModel;
  AnalysisDiagClients AnalysisDiagOpt = PD_HTML;
  AnalysisPurgeMode AnalysisPurgeOpt = PurgeStmt;

  std::string AnalyzeSpecificFunction;

  /// File path to which the exploded graph should be dumped.
  std::string DumpExplodedGraphTo;

  /// Store full compiler invocation for reproducible instructions in the
  /// generated report.
  std::string FullCompilerInvocation;

  /// The maximum number of times the analyzer visits a block.
  unsigned maxBlockVisitOnPath;

  /// Disable all analyzer checks.
  ///
  /// This flag allows one to disable analyzer checks on the code processed by
  /// the given analysis consumer. Note, the code will get parsed and the
  /// command-line options will get checked.
  unsigned DisableAllChecks : 1;

  unsigned ShowCheckerHelp : 1;
  unsigned ShowEnabledCheckerList : 1;
  unsigned ShowConfigOptionsList : 1;
  unsigned ShouldEmitErrorsOnInvalidConfigValue : 1;
  unsigned AnalyzeAll : 1;
  unsigned AnalyzerDisplayProgress : 1;
  unsigned AnalyzeNestedBlocks : 1;

  unsigned eagerlyAssumeBinOpBifurcation : 1;

  unsigned TrimGraph : 1;
  unsigned visualizeExplodedGraphWithGraphViz : 1;
  unsigned UnoptimizedCFG : 1;
  unsigned PrintStats : 1;

  /// Do not re-analyze paths leading to exhausted nodes with a different
  /// strategy. We get better code coverage when retry is enabled.
  unsigned NoRetryExhausted : 1;

  /// The inlining stack depth limit.
  // Cap the stack depth at 4 calls (5 stack frames, base + 4 calls).
  unsigned InlineMaxStackDepth = 5;

  /// The mode of function selection used during inlining.
  AnalysisInliningMode InliningMode = NoRedundancy;

  // Create a field for each -analyzer-config option.
#define ANALYZER_OPTION_DEPENDS_ON_USER_MODE(TYPE, NAME, CMDFLAG, DESC,        \
                                             SHALLOW_VAL, DEEP_VAL)            \
  ANALYZER_OPTION(TYPE, NAME, CMDFLAG, DESC, SHALLOW_VAL)

#define ANALYZER_OPTION(TYPE, NAME, CMDFLAG, DESC, DEFAULT_VAL)                \
  TYPE NAME;

#include "clang/StaticAnalyzer/Core/AnalyzerOptions.def"
#undef ANALYZER_OPTION
#undef ANALYZER_OPTION_DEPENDS_ON_USER_MODE

  // Create an array of all -analyzer-config command line options. Sort it in
  // the constructor.
  std::vector<StringRef> AnalyzerConfigCmdFlags = {
#define ANALYZER_OPTION_DEPENDS_ON_USER_MODE(TYPE, NAME, CMDFLAG, DESC,        \
                                             SHALLOW_VAL, DEEP_VAL)            \
  ANALYZER_OPTION(TYPE, NAME, CMDFLAG, DESC, SHALLOW_VAL)

#define ANALYZER_OPTION(TYPE, NAME, CMDFLAG, DESC, DEFAULT_VAL)                \
    CMDFLAG,

#include "clang/StaticAnalyzer/Core/AnalyzerOptions.def"
#undef ANALYZER_OPTION
#undef ANALYZER_OPTION_DEPENDS_ON_USER_MODE
  };

  bool isUnknownAnalyzerConfig(StringRef Name) const {

    assert(std::is_sorted(AnalyzerConfigCmdFlags.begin(),
                          AnalyzerConfigCmdFlags.end()));

    return !std::binary_search(AnalyzerConfigCmdFlags.begin(),
                               AnalyzerConfigCmdFlags.end(), Name);
  }

  AnalyzerOptions()
      : DisableAllChecks(false), ShowCheckerHelp(false),
        ShowEnabledCheckerList(false), ShowConfigOptionsList(false),
        AnalyzeAll(false), AnalyzerDisplayProgress(false),
        AnalyzeNestedBlocks(false), eagerlyAssumeBinOpBifurcation(false),
        TrimGraph(false), visualizeExplodedGraphWithGraphViz(false),
        UnoptimizedCFG(false), PrintStats(false), NoRetryExhausted(false) {
    llvm::sort(AnalyzerConfigCmdFlags);
  }

  /// Interprets an option's string value as a boolean. The "true" string is
  /// interpreted as true and the "false" string is interpreted as false.
  ///
  /// If an option value is not provided, returns the given \p DefaultVal.
  /// @param [in] Name Name for option to retrieve.
  /// @param [in] DefaultVal Default value returned if no such option was
  /// specified.
  /// @param [in] C The checker object the option belongs to. Checker options
  /// are retrieved in the following format:
  /// `-analyzer-config <package and checker name>:OptionName=Value.
  /// @param [in] SearchInParents If set to true and the searched option was not
  /// specified for the given checker the options for the parent packages will
  /// be searched as well. The inner packages take precedence over the outer
  /// ones.
  bool getCheckerBooleanOption(StringRef Name, bool DefaultVal,
                        const ento::CheckerBase *C,
                        bool SearchInParents = false) const;


  /// Interprets an option's string value as an integer value.
  ///
  /// If an option value is not provided, returns the given \p DefaultVal.
  /// @param [in] Name Name for option to retrieve.
  /// @param [in] DefaultVal Default value returned if no such option was
  /// specified.
  /// @param [in] C The checker object the option belongs to. Checker options
  /// are retrieved in the following format:
  /// `-analyzer-config <package and checker name>:OptionName=Value.
  /// @param [in] SearchInParents If set to true and the searched option was not
  /// specified for the given checker the options for the parent packages will
  /// be searched as well. The inner packages take precedence over the outer
  /// ones.
  int getCheckerIntegerOption(StringRef Name, int DefaultVal,
                         const ento::CheckerBase *C,
                         bool SearchInParents = false) const;

  /// Query an option's string value.
  ///
  /// If an option value is not provided, returns the given \p DefaultVal.
  /// @param [in] Name Name for option to retrieve.
  /// @param [in] DefaultVal Default value returned if no such option was
  /// specified.
  /// @param [in] C The checker object the option belongs to. Checker options
  /// are retrieved in the following format:
  /// `-analyzer-config <package and checker name>:OptionName=Value.
  /// @param [in] SearchInParents If set to true and the searched option was not
  /// specified for the given checker the options for the parent packages will
  /// be searched as well. The inner packages take precedence over the outer
  /// ones.
  StringRef getCheckerStringOption(StringRef Name, StringRef DefaultVal,
                              const ento::CheckerBase *C,
                              bool SearchInParents = false) const;

  /// Retrieves and sets the UserMode. This is a high-level option,
  /// which is used to set other low-level options. It is not accessible
  /// outside of AnalyzerOptions.
  UserModeKind getUserMode() const;

  ExplorationStrategyKind getExplorationStrategy() const;

  /// Returns the inter-procedural analysis mode.
  IPAKind getIPAMode() const;

  /// Returns the option controlling which C++ member functions will be
  /// considered for inlining.
  ///
  /// This is controlled by the 'c++-inlining' config option.
  ///
  /// \sa CXXMemberInliningMode
  bool mayInlineCXXMemberFunction(CXXInlineableMemberKind K) const;
};

using AnalyzerOptionsRef = IntrusiveRefCntPtr<AnalyzerOptions>;

//===----------------------------------------------------------------------===//
// We'll use AnalyzerOptions in the frontend, but we can't link the frontend
// with clangStaticAnalyzerCore, because clangStaticAnalyzerCore depends on
// clangFrontend.
//
// For this reason, implement some methods in this header file.
//===----------------------------------------------------------------------===//

inline UserModeKind AnalyzerOptions::getUserMode() const {
  auto K = llvm::StringSwitch<llvm::Optional<UserModeKind>>(UserMode)
    .Case("shallow", UMK_Shallow)
    .Case("deep", UMK_Deep)
    .Default(None);
  assert(K.hasValue() && "User mode is invalid.");
  return K.getValue();
}

} // namespace clang

#endif // LLVM_CLANG_STATICANALYZER_CORE_ANALYZEROPTIONS_H
