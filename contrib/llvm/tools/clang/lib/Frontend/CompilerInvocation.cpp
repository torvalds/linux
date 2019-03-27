//===- CompilerInvocation.cpp ---------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/CompilerInvocation.h"
#include "TestModuleFileExtension.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/CodeGenOptions.h"
#include "clang/Basic/CommentOptions.h"
#include "clang/Basic/DebugInfoOptions.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/FileSystemOptions.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/ObjCRuntime.h"
#include "clang/Basic/Sanitizers.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/TargetOptions.h"
#include "clang/Basic/Version.h"
#include "clang/Basic/Visibility.h"
#include "clang/Basic/XRayInstr.h"
#include "clang/Config/config.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "clang/Frontend/CommandLineSourceLoc.h"
#include "clang/Frontend/DependencyOutputOptions.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Frontend/FrontendOptions.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Frontend/LangStandard.h"
#include "clang/Frontend/MigratorOptions.h"
#include "clang/Frontend/PreprocessorOutputOptions.h"
#include "clang/Frontend/Utils.h"
#include "clang/Lex/HeaderSearchOptions.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Sema/CodeCompleteOptions.h"
#include "clang/Serialization/ModuleFileExtension.h"
#include "clang/StaticAnalyzer/Core/AnalyzerOptions.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/CachedHashString.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Triple.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/Linker/Linker.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/OptSpecifier.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Option/Option.h"
#include "llvm/ProfileData/InstrProfReader.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetOptions.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace clang;
using namespace driver;
using namespace options;
using namespace llvm::opt;

//===----------------------------------------------------------------------===//
// Initialization.
//===----------------------------------------------------------------------===//

CompilerInvocationBase::CompilerInvocationBase()
    : LangOpts(new LangOptions()), TargetOpts(new TargetOptions()),
      DiagnosticOpts(new DiagnosticOptions()),
      HeaderSearchOpts(new HeaderSearchOptions()),
      PreprocessorOpts(new PreprocessorOptions()) {}

CompilerInvocationBase::CompilerInvocationBase(const CompilerInvocationBase &X)
    : LangOpts(new LangOptions(*X.getLangOpts())),
      TargetOpts(new TargetOptions(X.getTargetOpts())),
      DiagnosticOpts(new DiagnosticOptions(X.getDiagnosticOpts())),
      HeaderSearchOpts(new HeaderSearchOptions(X.getHeaderSearchOpts())),
      PreprocessorOpts(new PreprocessorOptions(X.getPreprocessorOpts())) {}

CompilerInvocationBase::~CompilerInvocationBase() = default;

//===----------------------------------------------------------------------===//
// Deserialization (from args)
//===----------------------------------------------------------------------===//

static unsigned getOptimizationLevel(ArgList &Args, InputKind IK,
                                     DiagnosticsEngine &Diags) {
  unsigned DefaultOpt = llvm::CodeGenOpt::None;
  if (IK.getLanguage() == InputKind::OpenCL && !Args.hasArg(OPT_cl_opt_disable))
    DefaultOpt = llvm::CodeGenOpt::Default;

  if (Arg *A = Args.getLastArg(options::OPT_O_Group)) {
    if (A->getOption().matches(options::OPT_O0))
      return llvm::CodeGenOpt::None;

    if (A->getOption().matches(options::OPT_Ofast))
      return llvm::CodeGenOpt::Aggressive;

    assert(A->getOption().matches(options::OPT_O));

    StringRef S(A->getValue());
    if (S == "s" || S == "z" || S.empty())
      return llvm::CodeGenOpt::Default;

    if (S == "g")
      return llvm::CodeGenOpt::Less;

    return getLastArgIntValue(Args, OPT_O, DefaultOpt, Diags);
  }

  return DefaultOpt;
}

static unsigned getOptimizationLevelSize(ArgList &Args) {
  if (Arg *A = Args.getLastArg(options::OPT_O_Group)) {
    if (A->getOption().matches(options::OPT_O)) {
      switch (A->getValue()[0]) {
      default:
        return 0;
      case 's':
        return 1;
      case 'z':
        return 2;
      }
    }
  }
  return 0;
}

static void addDiagnosticArgs(ArgList &Args, OptSpecifier Group,
                              OptSpecifier GroupWithValue,
                              std::vector<std::string> &Diagnostics) {
  for (auto *A : Args.filtered(Group)) {
    if (A->getOption().getKind() == Option::FlagClass) {
      // The argument is a pure flag (such as OPT_Wall or OPT_Wdeprecated). Add
      // its name (minus the "W" or "R" at the beginning) to the warning list.
      Diagnostics.push_back(A->getOption().getName().drop_front(1));
    } else if (A->getOption().matches(GroupWithValue)) {
      // This is -Wfoo= or -Rfoo=, where foo is the name of the diagnostic group.
      Diagnostics.push_back(A->getOption().getName().drop_front(1).rtrim("=-"));
    } else {
      // Otherwise, add its value (for OPT_W_Joined and similar).
      for (const auto *Arg : A->getValues())
        Diagnostics.emplace_back(Arg);
    }
  }
}

// Parse the Static Analyzer configuration. If \p Diags is set to nullptr,
// it won't verify the input.
static void parseAnalyzerConfigs(AnalyzerOptions &AnOpts,
                                 DiagnosticsEngine *Diags);

static void getAllNoBuiltinFuncValues(ArgList &Args,
                                      std::vector<std::string> &Funcs) {
  SmallVector<const char *, 8> Values;
  for (const auto &Arg : Args) {
    const Option &O = Arg->getOption();
    if (O.matches(options::OPT_fno_builtin_)) {
      const char *FuncName = Arg->getValue();
      if (Builtin::Context::isBuiltinFunc(FuncName))
        Values.push_back(FuncName);
    }
  }
  Funcs.insert(Funcs.end(), Values.begin(), Values.end());
}

static bool ParseAnalyzerArgs(AnalyzerOptions &Opts, ArgList &Args,
                              DiagnosticsEngine &Diags) {
  bool Success = true;
  if (Arg *A = Args.getLastArg(OPT_analyzer_store)) {
    StringRef Name = A->getValue();
    AnalysisStores Value = llvm::StringSwitch<AnalysisStores>(Name)
#define ANALYSIS_STORE(NAME, CMDFLAG, DESC, CREATFN) \
      .Case(CMDFLAG, NAME##Model)
#include "clang/StaticAnalyzer/Core/Analyses.def"
      .Default(NumStores);
    if (Value == NumStores) {
      Diags.Report(diag::err_drv_invalid_value)
        << A->getAsString(Args) << Name;
      Success = false;
    } else {
      Opts.AnalysisStoreOpt = Value;
    }
  }

  if (Arg *A = Args.getLastArg(OPT_analyzer_constraints)) {
    StringRef Name = A->getValue();
    AnalysisConstraints Value = llvm::StringSwitch<AnalysisConstraints>(Name)
#define ANALYSIS_CONSTRAINTS(NAME, CMDFLAG, DESC, CREATFN) \
      .Case(CMDFLAG, NAME##Model)
#include "clang/StaticAnalyzer/Core/Analyses.def"
      .Default(NumConstraints);
    if (Value == NumConstraints) {
      Diags.Report(diag::err_drv_invalid_value)
        << A->getAsString(Args) << Name;
      Success = false;
    } else {
      Opts.AnalysisConstraintsOpt = Value;
    }
  }

  if (Arg *A = Args.getLastArg(OPT_analyzer_output)) {
    StringRef Name = A->getValue();
    AnalysisDiagClients Value = llvm::StringSwitch<AnalysisDiagClients>(Name)
#define ANALYSIS_DIAGNOSTICS(NAME, CMDFLAG, DESC, CREATFN) \
      .Case(CMDFLAG, PD_##NAME)
#include "clang/StaticAnalyzer/Core/Analyses.def"
      .Default(NUM_ANALYSIS_DIAG_CLIENTS);
    if (Value == NUM_ANALYSIS_DIAG_CLIENTS) {
      Diags.Report(diag::err_drv_invalid_value)
        << A->getAsString(Args) << Name;
      Success = false;
    } else {
      Opts.AnalysisDiagOpt = Value;
    }
  }

  if (Arg *A = Args.getLastArg(OPT_analyzer_purge)) {
    StringRef Name = A->getValue();
    AnalysisPurgeMode Value = llvm::StringSwitch<AnalysisPurgeMode>(Name)
#define ANALYSIS_PURGE(NAME, CMDFLAG, DESC) \
      .Case(CMDFLAG, NAME)
#include "clang/StaticAnalyzer/Core/Analyses.def"
      .Default(NumPurgeModes);
    if (Value == NumPurgeModes) {
      Diags.Report(diag::err_drv_invalid_value)
        << A->getAsString(Args) << Name;
      Success = false;
    } else {
      Opts.AnalysisPurgeOpt = Value;
    }
  }

  if (Arg *A = Args.getLastArg(OPT_analyzer_inlining_mode)) {
    StringRef Name = A->getValue();
    AnalysisInliningMode Value = llvm::StringSwitch<AnalysisInliningMode>(Name)
#define ANALYSIS_INLINING_MODE(NAME, CMDFLAG, DESC) \
      .Case(CMDFLAG, NAME)
#include "clang/StaticAnalyzer/Core/Analyses.def"
      .Default(NumInliningModes);
    if (Value == NumInliningModes) {
      Diags.Report(diag::err_drv_invalid_value)
        << A->getAsString(Args) << Name;
      Success = false;
    } else {
      Opts.InliningMode = Value;
    }
  }

  Opts.ShowCheckerHelp = Args.hasArg(OPT_analyzer_checker_help);
  Opts.ShowConfigOptionsList = Args.hasArg(OPT_analyzer_config_help);
  Opts.ShowEnabledCheckerList = Args.hasArg(OPT_analyzer_list_enabled_checkers);
  Opts.ShouldEmitErrorsOnInvalidConfigValue =
      /* negated */!llvm::StringSwitch<bool>(
                   Args.getLastArgValue(OPT_analyzer_config_compatibility_mode))
        .Case("true", true)
        .Case("false", false)
        .Default(false);
  Opts.DisableAllChecks = Args.hasArg(OPT_analyzer_disable_all_checks);

  Opts.visualizeExplodedGraphWithGraphViz =
    Args.hasArg(OPT_analyzer_viz_egraph_graphviz);
  Opts.DumpExplodedGraphTo = Args.getLastArgValue(OPT_analyzer_dump_egraph);
  Opts.NoRetryExhausted = Args.hasArg(OPT_analyzer_disable_retry_exhausted);
  Opts.AnalyzeAll = Args.hasArg(OPT_analyzer_opt_analyze_headers);
  Opts.AnalyzerDisplayProgress = Args.hasArg(OPT_analyzer_display_progress);
  Opts.AnalyzeNestedBlocks =
    Args.hasArg(OPT_analyzer_opt_analyze_nested_blocks);
  Opts.AnalyzeSpecificFunction = Args.getLastArgValue(OPT_analyze_function);
  Opts.UnoptimizedCFG = Args.hasArg(OPT_analysis_UnoptimizedCFG);
  Opts.TrimGraph = Args.hasArg(OPT_trim_egraph);
  Opts.maxBlockVisitOnPath =
      getLastArgIntValue(Args, OPT_analyzer_max_loop, 4, Diags);
  Opts.PrintStats = Args.hasArg(OPT_analyzer_stats);
  Opts.InlineMaxStackDepth =
      getLastArgIntValue(Args, OPT_analyzer_inline_max_stack_depth,
                         Opts.InlineMaxStackDepth, Diags);

  Opts.CheckersControlList.clear();
  for (const Arg *A :
       Args.filtered(OPT_analyzer_checker, OPT_analyzer_disable_checker)) {
    A->claim();
    bool enable = (A->getOption().getID() == OPT_analyzer_checker);
    // We can have a list of comma separated checker names, e.g:
    // '-analyzer-checker=cocoa,unix'
    StringRef checkerList = A->getValue();
    SmallVector<StringRef, 4> checkers;
    checkerList.split(checkers, ",");
    for (auto checker : checkers)
      Opts.CheckersControlList.emplace_back(checker, enable);
  }

  // Go through the analyzer configuration options.
  for (const auto *A : Args.filtered(OPT_analyzer_config)) {

    // We can have a list of comma separated config names, e.g:
    // '-analyzer-config key1=val1,key2=val2'
    StringRef configList = A->getValue();
    SmallVector<StringRef, 4> configVals;
    configList.split(configVals, ",");
    for (const auto &configVal : configVals) {
      StringRef key, val;
      std::tie(key, val) = configVal.split("=");
      if (val.empty()) {
        Diags.Report(SourceLocation(),
                     diag::err_analyzer_config_no_value) << configVal;
        Success = false;
        break;
      }
      if (val.find('=') != StringRef::npos) {
        Diags.Report(SourceLocation(),
                     diag::err_analyzer_config_multiple_values)
          << configVal;
        Success = false;
        break;
      }

      // TODO: Check checker options too, possibly in CheckerRegistry.
      // Leave unknown non-checker configs unclaimed.
      if (!key.contains(":") && Opts.isUnknownAnalyzerConfig(key)) {
        if (Opts.ShouldEmitErrorsOnInvalidConfigValue)
          Diags.Report(diag::err_analyzer_config_unknown) << key;
        continue;
      }

      A->claim();
      Opts.Config[key] = val;
    }
  }

  if (Opts.ShouldEmitErrorsOnInvalidConfigValue)
    parseAnalyzerConfigs(Opts, &Diags);
  else
    parseAnalyzerConfigs(Opts, nullptr);

  llvm::raw_string_ostream os(Opts.FullCompilerInvocation);
  for (unsigned i = 0; i < Args.getNumInputArgStrings(); ++i) {
    if (i != 0)
      os << " ";
    os << Args.getArgString(i);
  }
  os.flush();

  return Success;
}

static StringRef getStringOption(AnalyzerOptions::ConfigTable &Config,
                                 StringRef OptionName, StringRef DefaultVal) {
  return Config.insert({OptionName, DefaultVal}).first->second;
}

static void initOption(AnalyzerOptions::ConfigTable &Config,
                       DiagnosticsEngine *Diags,
                       StringRef &OptionField, StringRef Name,
                       StringRef DefaultVal) {
  // String options may be known to invalid (e.g. if the expected string is a
  // file name, but the file does not exist), those will have to be checked in
  // parseConfigs.
  OptionField = getStringOption(Config, Name, DefaultVal);
}

static void initOption(AnalyzerOptions::ConfigTable &Config,
                       DiagnosticsEngine *Diags,
                       bool &OptionField, StringRef Name, bool DefaultVal) {
  auto PossiblyInvalidVal = llvm::StringSwitch<Optional<bool>>(
                 getStringOption(Config, Name, (DefaultVal ? "true" : "false")))
      .Case("true", true)
      .Case("false", false)
      .Default(None);

  if (!PossiblyInvalidVal) {
    if (Diags)
      Diags->Report(diag::err_analyzer_config_invalid_input)
        << Name << "a boolean";
    else
      OptionField = DefaultVal;
  } else
    OptionField = PossiblyInvalidVal.getValue();
}

static void initOption(AnalyzerOptions::ConfigTable &Config,
                       DiagnosticsEngine *Diags,
                       unsigned &OptionField, StringRef Name,
                       unsigned DefaultVal) {

  OptionField = DefaultVal;
  bool HasFailed = getStringOption(Config, Name, std::to_string(DefaultVal))
                     .getAsInteger(10, OptionField);
  if (Diags && HasFailed)
    Diags->Report(diag::err_analyzer_config_invalid_input)
      << Name << "an unsigned";
}

static void parseAnalyzerConfigs(AnalyzerOptions &AnOpts,
                                 DiagnosticsEngine *Diags) {
  // TODO: There's no need to store the entire configtable, it'd be plenty
  // enough tostore checker options.

#define ANALYZER_OPTION(TYPE, NAME, CMDFLAG, DESC, DEFAULT_VAL)                \
  initOption(AnOpts.Config, Diags, AnOpts.NAME, CMDFLAG, DEFAULT_VAL);

#define ANALYZER_OPTION_DEPENDS_ON_USER_MODE(TYPE, NAME, CMDFLAG, DESC,        \
                                           SHALLOW_VAL, DEEP_VAL)              \
  switch (AnOpts.getUserMode()) {                                              \
  case UMK_Shallow:                                                            \
    initOption(AnOpts.Config, Diags, AnOpts.NAME, CMDFLAG, SHALLOW_VAL);       \
    break;                                                                     \
  case UMK_Deep:                                                               \
    initOption(AnOpts.Config, Diags, AnOpts.NAME, CMDFLAG, DEEP_VAL);          \
    break;                                                                     \
  }                                                                            \

#include "clang/StaticAnalyzer/Core/AnalyzerOptions.def"
#undef ANALYZER_OPTION
#undef ANALYZER_OPTION_DEPENDS_ON_USER_MODE

  // At this point, AnalyzerOptions is configured. Let's validate some options.

  if (!Diags)
    return;

  if (!AnOpts.CTUDir.empty() && !llvm::sys::fs::is_directory(AnOpts.CTUDir))
    Diags->Report(diag::err_analyzer_config_invalid_input) << "ctu-dir"
                                                           << "a filename";

  if (!AnOpts.ModelPath.empty() &&
      !llvm::sys::fs::is_directory(AnOpts.ModelPath))
    Diags->Report(diag::err_analyzer_config_invalid_input) << "model-path"
                                                           << "a filename";
}

static bool ParseMigratorArgs(MigratorOptions &Opts, ArgList &Args) {
  Opts.NoNSAllocReallocError = Args.hasArg(OPT_migrator_no_nsalloc_error);
  Opts.NoFinalizeRemoval = Args.hasArg(OPT_migrator_no_finalize_removal);
  return true;
}

static void ParseCommentArgs(CommentOptions &Opts, ArgList &Args) {
  Opts.BlockCommandNames = Args.getAllArgValues(OPT_fcomment_block_commands);
  Opts.ParseAllComments = Args.hasArg(OPT_fparse_all_comments);
}

static StringRef getCodeModel(ArgList &Args, DiagnosticsEngine &Diags) {
  if (Arg *A = Args.getLastArg(OPT_mcode_model)) {
    StringRef Value = A->getValue();
    if (Value == "small" || Value == "kernel" || Value == "medium" ||
        Value == "large" || Value == "tiny")
      return Value;
    Diags.Report(diag::err_drv_invalid_value) << A->getAsString(Args) << Value;
  }
  return "default";
}

static llvm::Reloc::Model getRelocModel(ArgList &Args,
                                        DiagnosticsEngine &Diags) {
  if (Arg *A = Args.getLastArg(OPT_mrelocation_model)) {
    StringRef Value = A->getValue();
    auto RM = llvm::StringSwitch<llvm::Optional<llvm::Reloc::Model>>(Value)
                  .Case("static", llvm::Reloc::Static)
                  .Case("pic", llvm::Reloc::PIC_)
                  .Case("ropi", llvm::Reloc::ROPI)
                  .Case("rwpi", llvm::Reloc::RWPI)
                  .Case("ropi-rwpi", llvm::Reloc::ROPI_RWPI)
                  .Case("dynamic-no-pic", llvm::Reloc::DynamicNoPIC)
                  .Default(None);
    if (RM.hasValue())
      return *RM;
    Diags.Report(diag::err_drv_invalid_value) << A->getAsString(Args) << Value;
  }
  return llvm::Reloc::PIC_;
}

/// Create a new Regex instance out of the string value in \p RpassArg.
/// It returns a pointer to the newly generated Regex instance.
static std::shared_ptr<llvm::Regex>
GenerateOptimizationRemarkRegex(DiagnosticsEngine &Diags, ArgList &Args,
                                Arg *RpassArg) {
  StringRef Val = RpassArg->getValue();
  std::string RegexError;
  std::shared_ptr<llvm::Regex> Pattern = std::make_shared<llvm::Regex>(Val);
  if (!Pattern->isValid(RegexError)) {
    Diags.Report(diag::err_drv_optimization_remark_pattern)
        << RegexError << RpassArg->getAsString(Args);
    Pattern.reset();
  }
  return Pattern;
}

static bool parseDiagnosticLevelMask(StringRef FlagName,
                                     const std::vector<std::string> &Levels,
                                     DiagnosticsEngine *Diags,
                                     DiagnosticLevelMask &M) {
  bool Success = true;
  for (const auto &Level : Levels) {
    DiagnosticLevelMask const PM =
      llvm::StringSwitch<DiagnosticLevelMask>(Level)
        .Case("note",    DiagnosticLevelMask::Note)
        .Case("remark",  DiagnosticLevelMask::Remark)
        .Case("warning", DiagnosticLevelMask::Warning)
        .Case("error",   DiagnosticLevelMask::Error)
        .Default(DiagnosticLevelMask::None);
    if (PM == DiagnosticLevelMask::None) {
      Success = false;
      if (Diags)
        Diags->Report(diag::err_drv_invalid_value) << FlagName << Level;
    }
    M = M | PM;
  }
  return Success;
}

static void parseSanitizerKinds(StringRef FlagName,
                                const std::vector<std::string> &Sanitizers,
                                DiagnosticsEngine &Diags, SanitizerSet &S) {
  for (const auto &Sanitizer : Sanitizers) {
    SanitizerMask K = parseSanitizerValue(Sanitizer, /*AllowGroups=*/false);
    if (K == 0)
      Diags.Report(diag::err_drv_invalid_value) << FlagName << Sanitizer;
    else
      S.set(K, true);
  }
}

static void parseXRayInstrumentationBundle(StringRef FlagName, StringRef Bundle,
                                           ArgList &Args, DiagnosticsEngine &D,
                                           XRayInstrSet &S) {
  llvm::SmallVector<StringRef, 2> BundleParts;
  llvm::SplitString(Bundle, BundleParts, ",");
  for (const auto B : BundleParts) {
    auto Mask = parseXRayInstrValue(B);
    if (Mask == XRayInstrKind::None)
      if (B != "none")
        D.Report(diag::err_drv_invalid_value) << FlagName << Bundle;
      else
        S.Mask = Mask;
    else if (Mask == XRayInstrKind::All)
      S.Mask = Mask;
    else
      S.set(Mask, true);
  }
}

// Set the profile kind for fprofile-instrument.
static void setPGOInstrumentor(CodeGenOptions &Opts, ArgList &Args,
                               DiagnosticsEngine &Diags) {
  Arg *A = Args.getLastArg(OPT_fprofile_instrument_EQ);
  if (A == nullptr)
    return;
  StringRef S = A->getValue();
  unsigned I = llvm::StringSwitch<unsigned>(S)
                   .Case("none", CodeGenOptions::ProfileNone)
                   .Case("clang", CodeGenOptions::ProfileClangInstr)
                   .Case("llvm", CodeGenOptions::ProfileIRInstr)
                   .Default(~0U);
  if (I == ~0U) {
    Diags.Report(diag::err_drv_invalid_pgo_instrumentor) << A->getAsString(Args)
                                                         << S;
    return;
  }
  auto Instrumentor = static_cast<CodeGenOptions::ProfileInstrKind>(I);
  Opts.setProfileInstr(Instrumentor);
}

// Set the profile kind using fprofile-instrument-use-path.
static void setPGOUseInstrumentor(CodeGenOptions &Opts,
                                  const Twine &ProfileName) {
  auto ReaderOrErr = llvm::IndexedInstrProfReader::create(ProfileName);
  // In error, return silently and let Clang PGOUse report the error message.
  if (auto E = ReaderOrErr.takeError()) {
    llvm::consumeError(std::move(E));
    Opts.setProfileUse(CodeGenOptions::ProfileClangInstr);
    return;
  }
  std::unique_ptr<llvm::IndexedInstrProfReader> PGOReader =
    std::move(ReaderOrErr.get());
  if (PGOReader->isIRLevelProfile())
    Opts.setProfileUse(CodeGenOptions::ProfileIRInstr);
  else
    Opts.setProfileUse(CodeGenOptions::ProfileClangInstr);
}

static bool ParseCodeGenArgs(CodeGenOptions &Opts, ArgList &Args, InputKind IK,
                             DiagnosticsEngine &Diags,
                             const TargetOptions &TargetOpts,
                             const FrontendOptions &FrontendOpts) {
  bool Success = true;
  llvm::Triple Triple = llvm::Triple(TargetOpts.Triple);

  unsigned OptimizationLevel = getOptimizationLevel(Args, IK, Diags);
  // TODO: This could be done in Driver
  unsigned MaxOptLevel = 3;
  if (OptimizationLevel > MaxOptLevel) {
    // If the optimization level is not supported, fall back on the default
    // optimization
    Diags.Report(diag::warn_drv_optimization_value)
        << Args.getLastArg(OPT_O)->getAsString(Args) << "-O" << MaxOptLevel;
    OptimizationLevel = MaxOptLevel;
  }
  Opts.OptimizationLevel = OptimizationLevel;

  // At O0 we want to fully disable inlining outside of cases marked with
  // 'alwaysinline' that are required for correctness.
  Opts.setInlining((Opts.OptimizationLevel == 0)
                       ? CodeGenOptions::OnlyAlwaysInlining
                       : CodeGenOptions::NormalInlining);
  // Explicit inlining flags can disable some or all inlining even at
  // optimization levels above zero.
  if (Arg *InlineArg = Args.getLastArg(
          options::OPT_finline_functions, options::OPT_finline_hint_functions,
          options::OPT_fno_inline_functions, options::OPT_fno_inline)) {
    if (Opts.OptimizationLevel > 0) {
      const Option &InlineOpt = InlineArg->getOption();
      if (InlineOpt.matches(options::OPT_finline_functions))
        Opts.setInlining(CodeGenOptions::NormalInlining);
      else if (InlineOpt.matches(options::OPT_finline_hint_functions))
        Opts.setInlining(CodeGenOptions::OnlyHintInlining);
      else
        Opts.setInlining(CodeGenOptions::OnlyAlwaysInlining);
    }
  }

  Opts.ExperimentalNewPassManager = Args.hasFlag(
      OPT_fexperimental_new_pass_manager, OPT_fno_experimental_new_pass_manager,
      /* Default */ ENABLE_EXPERIMENTAL_NEW_PASS_MANAGER);

  Opts.DebugPassManager =
      Args.hasFlag(OPT_fdebug_pass_manager, OPT_fno_debug_pass_manager,
                   /* Default */ false);

  if (Arg *A = Args.getLastArg(OPT_fveclib)) {
    StringRef Name = A->getValue();
    if (Name == "Accelerate")
      Opts.setVecLib(CodeGenOptions::Accelerate);
    else if (Name == "SVML")
      Opts.setVecLib(CodeGenOptions::SVML);
    else if (Name == "none")
      Opts.setVecLib(CodeGenOptions::NoLibrary);
    else
      Diags.Report(diag::err_drv_invalid_value) << A->getAsString(Args) << Name;
  }

  if (Arg *A = Args.getLastArg(OPT_debug_info_kind_EQ)) {
    unsigned Val =
        llvm::StringSwitch<unsigned>(A->getValue())
            .Case("line-tables-only", codegenoptions::DebugLineTablesOnly)
            .Case("line-directives-only", codegenoptions::DebugDirectivesOnly)
            .Case("limited", codegenoptions::LimitedDebugInfo)
            .Case("standalone", codegenoptions::FullDebugInfo)
            .Default(~0U);
    if (Val == ~0U)
      Diags.Report(diag::err_drv_invalid_value) << A->getAsString(Args)
                                                << A->getValue();
    else
      Opts.setDebugInfo(static_cast<codegenoptions::DebugInfoKind>(Val));
  }
  if (Arg *A = Args.getLastArg(OPT_debugger_tuning_EQ)) {
    unsigned Val = llvm::StringSwitch<unsigned>(A->getValue())
                       .Case("gdb", unsigned(llvm::DebuggerKind::GDB))
                       .Case("lldb", unsigned(llvm::DebuggerKind::LLDB))
                       .Case("sce", unsigned(llvm::DebuggerKind::SCE))
                       .Default(~0U);
    if (Val == ~0U)
      Diags.Report(diag::err_drv_invalid_value) << A->getAsString(Args)
                                                << A->getValue();
    else
      Opts.setDebuggerTuning(static_cast<llvm::DebuggerKind>(Val));
  }
  Opts.DwarfVersion = getLastArgIntValue(Args, OPT_dwarf_version_EQ, 0, Diags);
  Opts.DebugColumnInfo = Args.hasArg(OPT_dwarf_column_info);
  Opts.EmitCodeView = Args.hasArg(OPT_gcodeview);
  Opts.CodeViewGHash = Args.hasArg(OPT_gcodeview_ghash);
  Opts.MacroDebugInfo = Args.hasArg(OPT_debug_info_macro);
  Opts.WholeProgramVTables = Args.hasArg(OPT_fwhole_program_vtables);
  Opts.LTOVisibilityPublicStd = Args.hasArg(OPT_flto_visibility_public_std);
  Opts.SplitDwarfFile = Args.getLastArgValue(OPT_split_dwarf_file);
  Opts.SplitDwarfInlining = !Args.hasArg(OPT_fno_split_dwarf_inlining);

  if (Arg *A =
          Args.getLastArg(OPT_enable_split_dwarf, OPT_enable_split_dwarf_EQ)) {
    if (A->getOption().matches(options::OPT_enable_split_dwarf)) {
      Opts.setSplitDwarfMode(CodeGenOptions::SplitFileFission);
    } else {
      StringRef Name = A->getValue();
      if (Name == "single")
        Opts.setSplitDwarfMode(CodeGenOptions::SingleFileFission);
      else if (Name == "split")
        Opts.setSplitDwarfMode(CodeGenOptions::SplitFileFission);
      else
        Diags.Report(diag::err_drv_invalid_value)
            << A->getAsString(Args) << Name;
    }
  }

  Opts.DebugTypeExtRefs = Args.hasArg(OPT_dwarf_ext_refs);
  Opts.DebugExplicitImport = Args.hasArg(OPT_dwarf_explicit_import);
  Opts.DebugFwdTemplateParams = Args.hasArg(OPT_debug_forward_template_params);
  Opts.EmbedSource = Args.hasArg(OPT_gembed_source);

  for (const auto &Arg : Args.getAllArgValues(OPT_fdebug_prefix_map_EQ))
    Opts.DebugPrefixMap.insert(StringRef(Arg).split('='));

  if (const Arg *A =
          Args.getLastArg(OPT_emit_llvm_uselists, OPT_no_emit_llvm_uselists))
    Opts.EmitLLVMUseLists = A->getOption().getID() == OPT_emit_llvm_uselists;

  Opts.DisableLLVMPasses = Args.hasArg(OPT_disable_llvm_passes);
  Opts.DisableLifetimeMarkers = Args.hasArg(OPT_disable_lifetimemarkers);
  Opts.DisableO0ImplyOptNone = Args.hasArg(OPT_disable_O0_optnone);
  Opts.DisableRedZone = Args.hasArg(OPT_disable_red_zone);
  Opts.IndirectTlsSegRefs = Args.hasArg(OPT_mno_tls_direct_seg_refs);
  Opts.ForbidGuardVariables = Args.hasArg(OPT_fforbid_guard_variables);
  Opts.UseRegisterSizedBitfieldAccess = Args.hasArg(
    OPT_fuse_register_sized_bitfield_access);
  Opts.RelaxedAliasing = Args.hasArg(OPT_relaxed_aliasing);
  Opts.StructPathTBAA = !Args.hasArg(OPT_no_struct_path_tbaa);
  Opts.NewStructPathTBAA = !Args.hasArg(OPT_no_struct_path_tbaa) &&
                           Args.hasArg(OPT_new_struct_path_tbaa);
  Opts.FineGrainedBitfieldAccesses =
      Args.hasFlag(OPT_ffine_grained_bitfield_accesses,
                   OPT_fno_fine_grained_bitfield_accesses, false);
  Opts.DwarfDebugFlags = Args.getLastArgValue(OPT_dwarf_debug_flags);
  Opts.RecordCommandLine = Args.getLastArgValue(OPT_record_command_line);
  Opts.MergeAllConstants = Args.hasArg(OPT_fmerge_all_constants);
  Opts.NoCommon = Args.hasArg(OPT_fno_common);
  Opts.NoImplicitFloat = Args.hasArg(OPT_no_implicit_float);
  Opts.OptimizeSize = getOptimizationLevelSize(Args);
  Opts.SimplifyLibCalls = !(Args.hasArg(OPT_fno_builtin) ||
                            Args.hasArg(OPT_ffreestanding));
  if (Opts.SimplifyLibCalls)
    getAllNoBuiltinFuncValues(Args, Opts.NoBuiltinFuncs);
  Opts.UnrollLoops =
      Args.hasFlag(OPT_funroll_loops, OPT_fno_unroll_loops,
                   (Opts.OptimizationLevel > 1));
  Opts.RerollLoops = Args.hasArg(OPT_freroll_loops);

  Opts.DisableIntegratedAS = Args.hasArg(OPT_fno_integrated_as);
  Opts.Autolink = !Args.hasArg(OPT_fno_autolink);
  Opts.SampleProfileFile = Args.getLastArgValue(OPT_fprofile_sample_use_EQ);
  Opts.DebugInfoForProfiling = Args.hasFlag(
      OPT_fdebug_info_for_profiling, OPT_fno_debug_info_for_profiling, false);
  Opts.DebugNameTable = static_cast<unsigned>(
      Args.hasArg(OPT_ggnu_pubnames)
          ? llvm::DICompileUnit::DebugNameTableKind::GNU
          : Args.hasArg(OPT_gpubnames)
                ? llvm::DICompileUnit::DebugNameTableKind::Default
                : llvm::DICompileUnit::DebugNameTableKind::None);
  Opts.DebugRangesBaseAddress = Args.hasArg(OPT_fdebug_ranges_base_address);

  setPGOInstrumentor(Opts, Args, Diags);
  Opts.InstrProfileOutput =
      Args.getLastArgValue(OPT_fprofile_instrument_path_EQ);
  Opts.ProfileInstrumentUsePath =
      Args.getLastArgValue(OPT_fprofile_instrument_use_path_EQ);
  if (!Opts.ProfileInstrumentUsePath.empty())
    setPGOUseInstrumentor(Opts, Opts.ProfileInstrumentUsePath);
  Opts.ProfileRemappingFile =
      Args.getLastArgValue(OPT_fprofile_remapping_file_EQ);
  if (!Opts.ProfileRemappingFile.empty() && !Opts.ExperimentalNewPassManager) {
    Diags.Report(diag::err_drv_argument_only_allowed_with)
      << Args.getLastArg(OPT_fprofile_remapping_file_EQ)->getAsString(Args)
      << "-fexperimental-new-pass-manager";
  }

  Opts.CoverageMapping =
      Args.hasFlag(OPT_fcoverage_mapping, OPT_fno_coverage_mapping, false);
  Opts.DumpCoverageMapping = Args.hasArg(OPT_dump_coverage_mapping);
  Opts.AsmVerbose = Args.hasArg(OPT_masm_verbose);
  Opts.PreserveAsmComments = !Args.hasArg(OPT_fno_preserve_as_comments);
  Opts.AssumeSaneOperatorNew = !Args.hasArg(OPT_fno_assume_sane_operator_new);
  Opts.ObjCAutoRefCountExceptions = Args.hasArg(OPT_fobjc_arc_exceptions);
  Opts.CXAAtExit = !Args.hasArg(OPT_fno_use_cxa_atexit);
  Opts.RegisterGlobalDtorsWithAtExit =
      Args.hasArg(OPT_fregister_global_dtors_with_atexit);
  Opts.CXXCtorDtorAliases = Args.hasArg(OPT_mconstructor_aliases);
  Opts.CodeModel = TargetOpts.CodeModel;
  Opts.DebugPass = Args.getLastArgValue(OPT_mdebug_pass);
  Opts.DisableFPElim =
      (Args.hasArg(OPT_mdisable_fp_elim) || Args.hasArg(OPT_pg));
  Opts.DisableFree = Args.hasArg(OPT_disable_free);
  Opts.DiscardValueNames = Args.hasArg(OPT_discard_value_names);
  Opts.DisableTailCalls = Args.hasArg(OPT_mdisable_tail_calls);
  Opts.NoEscapingBlockTailCalls =
      Args.hasArg(OPT_fno_escaping_block_tail_calls);
  Opts.FloatABI = Args.getLastArgValue(OPT_mfloat_abi);
  Opts.LessPreciseFPMAD = Args.hasArg(OPT_cl_mad_enable) ||
                          Args.hasArg(OPT_cl_unsafe_math_optimizations) ||
                          Args.hasArg(OPT_cl_fast_relaxed_math);
  Opts.LimitFloatPrecision = Args.getLastArgValue(OPT_mlimit_float_precision);
  Opts.NoInfsFPMath = (Args.hasArg(OPT_menable_no_infinities) ||
                       Args.hasArg(OPT_cl_finite_math_only) ||
                       Args.hasArg(OPT_cl_fast_relaxed_math));
  Opts.NoNaNsFPMath = (Args.hasArg(OPT_menable_no_nans) ||
                       Args.hasArg(OPT_cl_unsafe_math_optimizations) ||
                       Args.hasArg(OPT_cl_finite_math_only) ||
                       Args.hasArg(OPT_cl_fast_relaxed_math));
  Opts.NoSignedZeros = (Args.hasArg(OPT_fno_signed_zeros) ||
                        Args.hasArg(OPT_cl_no_signed_zeros) ||
                        Args.hasArg(OPT_cl_unsafe_math_optimizations) ||
                        Args.hasArg(OPT_cl_fast_relaxed_math));
  Opts.Reassociate = Args.hasArg(OPT_mreassociate);
  Opts.FlushDenorm = Args.hasArg(OPT_cl_denorms_are_zero) ||
                     (Args.hasArg(OPT_fcuda_is_device) &&
                      Args.hasArg(OPT_fcuda_flush_denormals_to_zero));
  Opts.CorrectlyRoundedDivSqrt =
      Args.hasArg(OPT_cl_fp32_correctly_rounded_divide_sqrt);
  Opts.UniformWGSize =
      Args.hasArg(OPT_cl_uniform_work_group_size);
  Opts.Reciprocals = Args.getAllArgValues(OPT_mrecip_EQ);
  Opts.ReciprocalMath = Args.hasArg(OPT_freciprocal_math);
  Opts.NoTrappingMath = Args.hasArg(OPT_fno_trapping_math);
  Opts.StrictFloatCastOverflow =
      !Args.hasArg(OPT_fno_strict_float_cast_overflow);

  Opts.NoZeroInitializedInBSS = Args.hasArg(OPT_mno_zero_initialized_in_bss);
  Opts.NumRegisterParameters = getLastArgIntValue(Args, OPT_mregparm, 0, Diags);
  Opts.NoExecStack = Args.hasArg(OPT_mno_exec_stack);
  Opts.FatalWarnings = Args.hasArg(OPT_massembler_fatal_warnings);
  Opts.EnableSegmentedStacks = Args.hasArg(OPT_split_stacks);
  Opts.RelaxAll = Args.hasArg(OPT_mrelax_all);
  Opts.IncrementalLinkerCompatible =
      Args.hasArg(OPT_mincremental_linker_compatible);
  Opts.PIECopyRelocations =
      Args.hasArg(OPT_mpie_copy_relocations);
  Opts.NoPLT = Args.hasArg(OPT_fno_plt);
  Opts.OmitLeafFramePointer = Args.hasArg(OPT_momit_leaf_frame_pointer);
  Opts.SaveTempLabels = Args.hasArg(OPT_msave_temp_labels);
  Opts.NoDwarfDirectoryAsm = Args.hasArg(OPT_fno_dwarf_directory_asm);
  Opts.SoftFloat = Args.hasArg(OPT_msoft_float);
  Opts.StrictEnums = Args.hasArg(OPT_fstrict_enums);
  Opts.StrictReturn = !Args.hasArg(OPT_fno_strict_return);
  Opts.StrictVTablePointers = Args.hasArg(OPT_fstrict_vtable_pointers);
  Opts.ForceEmitVTables = Args.hasArg(OPT_fforce_emit_vtables);
  Opts.UnsafeFPMath = Args.hasArg(OPT_menable_unsafe_fp_math) ||
                      Args.hasArg(OPT_cl_unsafe_math_optimizations) ||
                      Args.hasArg(OPT_cl_fast_relaxed_math);
  Opts.UnwindTables = Args.hasArg(OPT_munwind_tables);
  Opts.RelocationModel = getRelocModel(Args, Diags);
  Opts.ThreadModel = Args.getLastArgValue(OPT_mthread_model, "posix");
  if (Opts.ThreadModel != "posix" && Opts.ThreadModel != "single")
    Diags.Report(diag::err_drv_invalid_value)
        << Args.getLastArg(OPT_mthread_model)->getAsString(Args)
        << Opts.ThreadModel;
  Opts.TrapFuncName = Args.getLastArgValue(OPT_ftrap_function_EQ);
  Opts.UseInitArray = Args.hasArg(OPT_fuse_init_array);

  Opts.FunctionSections = Args.hasFlag(OPT_ffunction_sections,
                                       OPT_fno_function_sections, false);
  Opts.DataSections = Args.hasFlag(OPT_fdata_sections,
                                   OPT_fno_data_sections, false);
  Opts.StackSizeSection =
      Args.hasFlag(OPT_fstack_size_section, OPT_fno_stack_size_section, false);
  Opts.UniqueSectionNames = Args.hasFlag(OPT_funique_section_names,
                                         OPT_fno_unique_section_names, true);

  Opts.MergeFunctions = Args.hasArg(OPT_fmerge_functions);

  Opts.NoUseJumpTables = Args.hasArg(OPT_fno_jump_tables);

  Opts.NullPointerIsValid = Args.hasArg(OPT_fno_delete_null_pointer_checks);

  Opts.ProfileSampleAccurate = Args.hasArg(OPT_fprofile_sample_accurate);

  Opts.PrepareForLTO = Args.hasArg(OPT_flto, OPT_flto_EQ);
  Opts.PrepareForThinLTO = false;
  if (Arg *A = Args.getLastArg(OPT_flto_EQ)) {
    StringRef S = A->getValue();
    if (S == "thin")
      Opts.PrepareForThinLTO = true;
    else if (S != "full")
      Diags.Report(diag::err_drv_invalid_value) << A->getAsString(Args) << S;
  }
  Opts.LTOUnit = Args.hasFlag(OPT_flto_unit, OPT_fno_lto_unit, false);
  Opts.EnableSplitLTOUnit = Args.hasArg(OPT_fsplit_lto_unit);
  if (Arg *A = Args.getLastArg(OPT_fthinlto_index_EQ)) {
    if (IK.getLanguage() != InputKind::LLVM_IR)
      Diags.Report(diag::err_drv_argument_only_allowed_with)
          << A->getAsString(Args) << "-x ir";
    Opts.ThinLTOIndexFile = Args.getLastArgValue(OPT_fthinlto_index_EQ);
  }
  if (Arg *A = Args.getLastArg(OPT_save_temps_EQ))
    Opts.SaveTempsFilePrefix =
        llvm::StringSwitch<std::string>(A->getValue())
            .Case("obj", FrontendOpts.OutputFile)
            .Default(llvm::sys::path::filename(FrontendOpts.OutputFile).str());

  Opts.ThinLinkBitcodeFile = Args.getLastArgValue(OPT_fthin_link_bitcode_EQ);

  Opts.MSVolatile = Args.hasArg(OPT_fms_volatile);

  Opts.VectorizeLoop = Args.hasArg(OPT_vectorize_loops);
  Opts.VectorizeSLP = Args.hasArg(OPT_vectorize_slp);

  Opts.PreferVectorWidth = Args.getLastArgValue(OPT_mprefer_vector_width_EQ);

  Opts.MainFileName = Args.getLastArgValue(OPT_main_file_name);
  Opts.VerifyModule = !Args.hasArg(OPT_disable_llvm_verifier);

  Opts.ControlFlowGuard = Args.hasArg(OPT_cfguard);

  Opts.DisableGCov = Args.hasArg(OPT_test_coverage);
  Opts.EmitGcovArcs = Args.hasArg(OPT_femit_coverage_data);
  Opts.EmitGcovNotes = Args.hasArg(OPT_femit_coverage_notes);
  if (Opts.EmitGcovArcs || Opts.EmitGcovNotes) {
    Opts.CoverageDataFile = Args.getLastArgValue(OPT_coverage_data_file);
    Opts.CoverageNotesFile = Args.getLastArgValue(OPT_coverage_notes_file);
    Opts.CoverageExtraChecksum = Args.hasArg(OPT_coverage_cfg_checksum);
    Opts.CoverageNoFunctionNamesInData =
        Args.hasArg(OPT_coverage_no_function_names_in_data);
    Opts.ProfileFilterFiles =
        Args.getLastArgValue(OPT_fprofile_filter_files_EQ);
    Opts.ProfileExcludeFiles =
        Args.getLastArgValue(OPT_fprofile_exclude_files_EQ);
    Opts.CoverageExitBlockBeforeBody =
        Args.hasArg(OPT_coverage_exit_block_before_body);
    if (Args.hasArg(OPT_coverage_version_EQ)) {
      StringRef CoverageVersion = Args.getLastArgValue(OPT_coverage_version_EQ);
      if (CoverageVersion.size() != 4) {
        Diags.Report(diag::err_drv_invalid_value)
            << Args.getLastArg(OPT_coverage_version_EQ)->getAsString(Args)
            << CoverageVersion;
      } else {
        memcpy(Opts.CoverageVersion, CoverageVersion.data(), 4);
      }
    }
  }
  // Handle -fembed-bitcode option.
  if (Arg *A = Args.getLastArg(OPT_fembed_bitcode_EQ)) {
    StringRef Name = A->getValue();
    unsigned Model = llvm::StringSwitch<unsigned>(Name)
        .Case("off", CodeGenOptions::Embed_Off)
        .Case("all", CodeGenOptions::Embed_All)
        .Case("bitcode", CodeGenOptions::Embed_Bitcode)
        .Case("marker", CodeGenOptions::Embed_Marker)
        .Default(~0U);
    if (Model == ~0U) {
      Diags.Report(diag::err_drv_invalid_value) << A->getAsString(Args) << Name;
      Success = false;
    } else
      Opts.setEmbedBitcode(
          static_cast<CodeGenOptions::EmbedBitcodeKind>(Model));
  }
  // FIXME: For backend options that are not yet recorded as function
  // attributes in the IR, keep track of them so we can embed them in a
  // separate data section and use them when building the bitcode.
  if (Opts.getEmbedBitcode() == CodeGenOptions::Embed_All) {
    for (const auto &A : Args) {
      // Do not encode output and input.
      if (A->getOption().getID() == options::OPT_o ||
          A->getOption().getID() == options::OPT_INPUT ||
          A->getOption().getID() == options::OPT_x ||
          A->getOption().getID() == options::OPT_fembed_bitcode ||
          (A->getOption().getGroup().isValid() &&
           A->getOption().getGroup().getID() == options::OPT_W_Group))
        continue;
      ArgStringList ASL;
      A->render(Args, ASL);
      for (const auto &arg : ASL) {
        StringRef ArgStr(arg);
        Opts.CmdArgs.insert(Opts.CmdArgs.end(), ArgStr.begin(), ArgStr.end());
        // using \00 to separate each commandline options.
        Opts.CmdArgs.push_back('\0');
      }
    }
  }

  Opts.PreserveVec3Type = Args.hasArg(OPT_fpreserve_vec3_type);
  Opts.InstrumentFunctions = Args.hasArg(OPT_finstrument_functions);
  Opts.InstrumentFunctionsAfterInlining =
      Args.hasArg(OPT_finstrument_functions_after_inlining);
  Opts.InstrumentFunctionEntryBare =
      Args.hasArg(OPT_finstrument_function_entry_bare);

  Opts.XRayInstrumentFunctions =
      Args.hasArg(OPT_fxray_instrument);
  Opts.XRayAlwaysEmitCustomEvents =
      Args.hasArg(OPT_fxray_always_emit_customevents);
  Opts.XRayAlwaysEmitTypedEvents =
      Args.hasArg(OPT_fxray_always_emit_typedevents);
  Opts.XRayInstructionThreshold =
      getLastArgIntValue(Args, OPT_fxray_instruction_threshold_EQ, 200, Diags);

  auto XRayInstrBundles =
      Args.getAllArgValues(OPT_fxray_instrumentation_bundle);
  if (XRayInstrBundles.empty())
    Opts.XRayInstrumentationBundle.Mask = XRayInstrKind::All;
  else
    for (const auto &A : XRayInstrBundles)
      parseXRayInstrumentationBundle("-fxray-instrumentation-bundle=", A, Args,
                                     Diags, Opts.XRayInstrumentationBundle);

  Opts.InstrumentForProfiling = Args.hasArg(OPT_pg);
  Opts.CallFEntry = Args.hasArg(OPT_mfentry);
  Opts.EmitOpenCLArgMetadata = Args.hasArg(OPT_cl_kernel_arg_info);

  if (const Arg *A = Args.getLastArg(OPT_fcf_protection_EQ)) {
    StringRef Name = A->getValue();
    if (Name == "full") {
      Opts.CFProtectionReturn = 1;
      Opts.CFProtectionBranch = 1;
    } else if (Name == "return")
      Opts.CFProtectionReturn = 1;
    else if (Name == "branch")
      Opts.CFProtectionBranch = 1;
    else if (Name != "none") {
      Diags.Report(diag::err_drv_invalid_value) << A->getAsString(Args) << Name;
      Success = false;
    }
  }

  if (const Arg *A = Args.getLastArg(OPT_compress_debug_sections,
                                     OPT_compress_debug_sections_EQ)) {
    if (A->getOption().getID() == OPT_compress_debug_sections) {
      // TODO: be more clever about the compression type auto-detection
      Opts.setCompressDebugSections(llvm::DebugCompressionType::GNU);
    } else {
      auto DCT = llvm::StringSwitch<llvm::DebugCompressionType>(A->getValue())
                     .Case("none", llvm::DebugCompressionType::None)
                     .Case("zlib", llvm::DebugCompressionType::Z)
                     .Case("zlib-gnu", llvm::DebugCompressionType::GNU)
                     .Default(llvm::DebugCompressionType::None);
      Opts.setCompressDebugSections(DCT);
    }
  }

  Opts.RelaxELFRelocations = Args.hasArg(OPT_mrelax_relocations);
  Opts.DebugCompilationDir = Args.getLastArgValue(OPT_fdebug_compilation_dir);
  for (auto *A :
       Args.filtered(OPT_mlink_bitcode_file, OPT_mlink_builtin_bitcode)) {
    CodeGenOptions::BitcodeFileToLink F;
    F.Filename = A->getValue();
    if (A->getOption().matches(OPT_mlink_builtin_bitcode)) {
      F.LinkFlags = llvm::Linker::Flags::LinkOnlyNeeded;
      // When linking CUDA bitcode, propagate function attributes so that
      // e.g. libdevice gets fast-math attrs if we're building with fast-math.
      F.PropagateAttrs = true;
      F.Internalize = true;
    }
    Opts.LinkBitcodeFiles.push_back(F);
  }
  Opts.SanitizeCoverageType =
      getLastArgIntValue(Args, OPT_fsanitize_coverage_type, 0, Diags);
  Opts.SanitizeCoverageIndirectCalls =
      Args.hasArg(OPT_fsanitize_coverage_indirect_calls);
  Opts.SanitizeCoverageTraceBB = Args.hasArg(OPT_fsanitize_coverage_trace_bb);
  Opts.SanitizeCoverageTraceCmp = Args.hasArg(OPT_fsanitize_coverage_trace_cmp);
  Opts.SanitizeCoverageTraceDiv = Args.hasArg(OPT_fsanitize_coverage_trace_div);
  Opts.SanitizeCoverageTraceGep = Args.hasArg(OPT_fsanitize_coverage_trace_gep);
  Opts.SanitizeCoverage8bitCounters =
      Args.hasArg(OPT_fsanitize_coverage_8bit_counters);
  Opts.SanitizeCoverageTracePC = Args.hasArg(OPT_fsanitize_coverage_trace_pc);
  Opts.SanitizeCoverageTracePCGuard =
      Args.hasArg(OPT_fsanitize_coverage_trace_pc_guard);
  Opts.SanitizeCoverageNoPrune = Args.hasArg(OPT_fsanitize_coverage_no_prune);
  Opts.SanitizeCoverageInline8bitCounters =
      Args.hasArg(OPT_fsanitize_coverage_inline_8bit_counters);
  Opts.SanitizeCoveragePCTable = Args.hasArg(OPT_fsanitize_coverage_pc_table);
  Opts.SanitizeCoverageStackDepth =
      Args.hasArg(OPT_fsanitize_coverage_stack_depth);
  Opts.SanitizeMemoryTrackOrigins =
      getLastArgIntValue(Args, OPT_fsanitize_memory_track_origins_EQ, 0, Diags);
  Opts.SanitizeMemoryUseAfterDtor =
      Args.hasFlag(OPT_fsanitize_memory_use_after_dtor,
                   OPT_fno_sanitize_memory_use_after_dtor,
                   false);
  Opts.SanitizeMinimalRuntime = Args.hasArg(OPT_fsanitize_minimal_runtime);
  Opts.SanitizeCfiCrossDso = Args.hasArg(OPT_fsanitize_cfi_cross_dso);
  Opts.SanitizeCfiICallGeneralizePointers =
      Args.hasArg(OPT_fsanitize_cfi_icall_generalize_pointers);
  Opts.SanitizeStats = Args.hasArg(OPT_fsanitize_stats);
  if (Arg *A = Args.getLastArg(
          OPT_fsanitize_address_poison_custom_array_cookie,
          OPT_fno_sanitize_address_poison_custom_array_cookie)) {
    Opts.SanitizeAddressPoisonCustomArrayCookie =
        A->getOption().getID() ==
        OPT_fsanitize_address_poison_custom_array_cookie;
  }
  if (Arg *A = Args.getLastArg(OPT_fsanitize_address_use_after_scope,
                               OPT_fno_sanitize_address_use_after_scope)) {
    Opts.SanitizeAddressUseAfterScope =
        A->getOption().getID() == OPT_fsanitize_address_use_after_scope;
  }
  Opts.SanitizeAddressGlobalsDeadStripping =
      Args.hasArg(OPT_fsanitize_address_globals_dead_stripping);
  if (Arg *A = Args.getLastArg(OPT_fsanitize_address_use_odr_indicator,
                               OPT_fno_sanitize_address_use_odr_indicator)) {
    Opts.SanitizeAddressUseOdrIndicator =
        A->getOption().getID() == OPT_fsanitize_address_use_odr_indicator;
  }
  Opts.SSPBufferSize =
      getLastArgIntValue(Args, OPT_stack_protector_buffer_size, 8, Diags);
  Opts.StackRealignment = Args.hasArg(OPT_mstackrealign);
  if (Arg *A = Args.getLastArg(OPT_mstack_alignment)) {
    StringRef Val = A->getValue();
    unsigned StackAlignment = Opts.StackAlignment;
    Val.getAsInteger(10, StackAlignment);
    Opts.StackAlignment = StackAlignment;
  }

  if (Arg *A = Args.getLastArg(OPT_mstack_probe_size)) {
    StringRef Val = A->getValue();
    unsigned StackProbeSize = Opts.StackProbeSize;
    Val.getAsInteger(0, StackProbeSize);
    Opts.StackProbeSize = StackProbeSize;
  }

  Opts.NoStackArgProbe = Args.hasArg(OPT_mno_stack_arg_probe);

  if (Arg *A = Args.getLastArg(OPT_fobjc_dispatch_method_EQ)) {
    StringRef Name = A->getValue();
    unsigned Method = llvm::StringSwitch<unsigned>(Name)
      .Case("legacy", CodeGenOptions::Legacy)
      .Case("non-legacy", CodeGenOptions::NonLegacy)
      .Case("mixed", CodeGenOptions::Mixed)
      .Default(~0U);
    if (Method == ~0U) {
      Diags.Report(diag::err_drv_invalid_value) << A->getAsString(Args) << Name;
      Success = false;
    } else {
      Opts.setObjCDispatchMethod(
        static_cast<CodeGenOptions::ObjCDispatchMethodKind>(Method));
    }
  }


  if (Args.hasArg(OPT_fno_objc_convert_messages_to_runtime_calls))
    Opts.ObjCConvertMessagesToRuntimeCalls = 0;

  if (Args.getLastArg(OPT_femulated_tls) ||
      Args.getLastArg(OPT_fno_emulated_tls)) {
    Opts.ExplicitEmulatedTLS = true;
    Opts.EmulatedTLS =
        Args.hasFlag(OPT_femulated_tls, OPT_fno_emulated_tls, false);
  }

  if (Arg *A = Args.getLastArg(OPT_ftlsmodel_EQ)) {
    StringRef Name = A->getValue();
    unsigned Model = llvm::StringSwitch<unsigned>(Name)
        .Case("global-dynamic", CodeGenOptions::GeneralDynamicTLSModel)
        .Case("local-dynamic", CodeGenOptions::LocalDynamicTLSModel)
        .Case("initial-exec", CodeGenOptions::InitialExecTLSModel)
        .Case("local-exec", CodeGenOptions::LocalExecTLSModel)
        .Default(~0U);
    if (Model == ~0U) {
      Diags.Report(diag::err_drv_invalid_value) << A->getAsString(Args) << Name;
      Success = false;
    } else {
      Opts.setDefaultTLSModel(static_cast<CodeGenOptions::TLSModel>(Model));
    }
  }

  if (Arg *A = Args.getLastArg(OPT_fdenormal_fp_math_EQ)) {
    StringRef Val = A->getValue();
    if (Val == "ieee")
      Opts.FPDenormalMode = "ieee";
    else if (Val == "preserve-sign")
      Opts.FPDenormalMode = "preserve-sign";
    else if (Val == "positive-zero")
      Opts.FPDenormalMode = "positive-zero";
    else
      Diags.Report(diag::err_drv_invalid_value) << A->getAsString(Args) << Val;
  }

  if (Arg *A = Args.getLastArg(OPT_fpcc_struct_return, OPT_freg_struct_return)) {
    if (A->getOption().matches(OPT_fpcc_struct_return)) {
      Opts.setStructReturnConvention(CodeGenOptions::SRCK_OnStack);
    } else {
      assert(A->getOption().matches(OPT_freg_struct_return));
      Opts.setStructReturnConvention(CodeGenOptions::SRCK_InRegs);
    }
  }

  Opts.DependentLibraries = Args.getAllArgValues(OPT_dependent_lib);
  Opts.LinkerOptions = Args.getAllArgValues(OPT_linker_option);
  bool NeedLocTracking = false;

  Opts.OptRecordFile = Args.getLastArgValue(OPT_opt_record_file);
  if (!Opts.OptRecordFile.empty())
    NeedLocTracking = true;

  if (Arg *A = Args.getLastArg(OPT_Rpass_EQ)) {
    Opts.OptimizationRemarkPattern =
        GenerateOptimizationRemarkRegex(Diags, Args, A);
    NeedLocTracking = true;
  }

  if (Arg *A = Args.getLastArg(OPT_Rpass_missed_EQ)) {
    Opts.OptimizationRemarkMissedPattern =
        GenerateOptimizationRemarkRegex(Diags, Args, A);
    NeedLocTracking = true;
  }

  if (Arg *A = Args.getLastArg(OPT_Rpass_analysis_EQ)) {
    Opts.OptimizationRemarkAnalysisPattern =
        GenerateOptimizationRemarkRegex(Diags, Args, A);
    NeedLocTracking = true;
  }

  Opts.DiagnosticsWithHotness =
      Args.hasArg(options::OPT_fdiagnostics_show_hotness);
  bool UsingSampleProfile = !Opts.SampleProfileFile.empty();
  bool UsingProfile = UsingSampleProfile ||
      (Opts.getProfileUse() != CodeGenOptions::ProfileNone);

  if (Opts.DiagnosticsWithHotness && !UsingProfile &&
      // An IR file will contain PGO as metadata
      IK.getLanguage() != InputKind::LLVM_IR)
    Diags.Report(diag::warn_drv_diagnostics_hotness_requires_pgo)
        << "-fdiagnostics-show-hotness";

  Opts.DiagnosticsHotnessThreshold = getLastArgUInt64Value(
      Args, options::OPT_fdiagnostics_hotness_threshold_EQ, 0);
  if (Opts.DiagnosticsHotnessThreshold > 0 && !UsingProfile)
    Diags.Report(diag::warn_drv_diagnostics_hotness_requires_pgo)
        << "-fdiagnostics-hotness-threshold=";

  // If the user requested to use a sample profile for PGO, then the
  // backend will need to track source location information so the profile
  // can be incorporated into the IR.
  if (UsingSampleProfile)
    NeedLocTracking = true;

  // If the user requested a flag that requires source locations available in
  // the backend, make sure that the backend tracks source location information.
  if (NeedLocTracking && Opts.getDebugInfo() == codegenoptions::NoDebugInfo)
    Opts.setDebugInfo(codegenoptions::LocTrackingOnly);

  Opts.RewriteMapFiles = Args.getAllArgValues(OPT_frewrite_map_file);

  // Parse -fsanitize-recover= arguments.
  // FIXME: Report unrecoverable sanitizers incorrectly specified here.
  parseSanitizerKinds("-fsanitize-recover=",
                      Args.getAllArgValues(OPT_fsanitize_recover_EQ), Diags,
                      Opts.SanitizeRecover);
  parseSanitizerKinds("-fsanitize-trap=",
                      Args.getAllArgValues(OPT_fsanitize_trap_EQ), Diags,
                      Opts.SanitizeTrap);

  Opts.CudaGpuBinaryFileName =
      Args.getLastArgValue(OPT_fcuda_include_gpubinary);

  Opts.Backchain = Args.hasArg(OPT_mbackchain);

  Opts.EmitCheckPathComponentsToStrip = getLastArgIntValue(
      Args, OPT_fsanitize_undefined_strip_path_components_EQ, 0, Diags);

  Opts.EmitVersionIdentMetadata = Args.hasFlag(OPT_Qy, OPT_Qn, true);

  Opts.Addrsig = Args.hasArg(OPT_faddrsig);

  if (Arg *A = Args.getLastArg(OPT_msign_return_address_EQ)) {
    StringRef SignScope = A->getValue();

    if (SignScope.equals_lower("none"))
      Opts.setSignReturnAddress(CodeGenOptions::SignReturnAddressScope::None);
    else if (SignScope.equals_lower("all"))
      Opts.setSignReturnAddress(CodeGenOptions::SignReturnAddressScope::All);
    else if (SignScope.equals_lower("non-leaf"))
      Opts.setSignReturnAddress(
          CodeGenOptions::SignReturnAddressScope::NonLeaf);
    else
      Diags.Report(diag::err_drv_invalid_value)
          << A->getAsString(Args) << SignScope;

    if (Arg *A = Args.getLastArg(OPT_msign_return_address_key_EQ)) {
      StringRef SignKey = A->getValue();
      if (!SignScope.empty() && !SignKey.empty()) {
        if (SignKey.equals_lower("a_key"))
          Opts.setSignReturnAddressKey(
              CodeGenOptions::SignReturnAddressKeyValue::AKey);
        else if (SignKey.equals_lower("b_key"))
          Opts.setSignReturnAddressKey(
              CodeGenOptions::SignReturnAddressKeyValue::BKey);
        else
          Diags.Report(diag::err_drv_invalid_value)
              << A->getAsString(Args) << SignKey;
      }
    }
  }

  Opts.BranchTargetEnforcement = Args.hasArg(OPT_mbranch_target_enforce);

  Opts.KeepStaticConsts = Args.hasArg(OPT_fkeep_static_consts);

  Opts.SpeculativeLoadHardening = Args.hasArg(OPT_mspeculative_load_hardening);

  Opts.DefaultFunctionAttrs = Args.getAllArgValues(OPT_default_function_attr);

  return Success;
}

static void ParseDependencyOutputArgs(DependencyOutputOptions &Opts,
                                      ArgList &Args) {
  Opts.OutputFile = Args.getLastArgValue(OPT_dependency_file);
  Opts.Targets = Args.getAllArgValues(OPT_MT);
  Opts.IncludeSystemHeaders = Args.hasArg(OPT_sys_header_deps);
  Opts.IncludeModuleFiles = Args.hasArg(OPT_module_file_deps);
  Opts.UsePhonyTargets = Args.hasArg(OPT_MP);
  Opts.ShowHeaderIncludes = Args.hasArg(OPT_H);
  Opts.HeaderIncludeOutputFile = Args.getLastArgValue(OPT_header_include_file);
  Opts.AddMissingHeaderDeps = Args.hasArg(OPT_MG);
  if (Args.hasArg(OPT_show_includes)) {
    // Writing both /showIncludes and preprocessor output to stdout
    // would produce interleaved output, so use stderr for /showIncludes.
    // This behaves the same as cl.exe, when /E, /EP or /P are passed.
    if (Args.hasArg(options::OPT_E) || Args.hasArg(options::OPT_P))
      Opts.ShowIncludesDest = ShowIncludesDestination::Stderr;
    else
      Opts.ShowIncludesDest = ShowIncludesDestination::Stdout;
  } else {
    Opts.ShowIncludesDest = ShowIncludesDestination::None;
  }
  Opts.DOTOutputFile = Args.getLastArgValue(OPT_dependency_dot);
  Opts.ModuleDependencyOutputDir =
      Args.getLastArgValue(OPT_module_dependency_dir);
  if (Args.hasArg(OPT_MV))
    Opts.OutputFormat = DependencyOutputFormat::NMake;
  // Add sanitizer blacklists as extra dependencies.
  // They won't be discovered by the regular preprocessor, so
  // we let make / ninja to know about this implicit dependency.
  Opts.ExtraDeps = Args.getAllArgValues(OPT_fdepfile_entry);
  // Only the -fmodule-file=<file> form.
  for (const auto *A : Args.filtered(OPT_fmodule_file)) {
    StringRef Val = A->getValue();
    if (Val.find('=') == StringRef::npos)
      Opts.ExtraDeps.push_back(Val);
  }
}

static bool parseShowColorsArgs(const ArgList &Args, bool DefaultColor) {
  // Color diagnostics default to auto ("on" if terminal supports) in the driver
  // but default to off in cc1, needing an explicit OPT_fdiagnostics_color.
  // Support both clang's -f[no-]color-diagnostics and gcc's
  // -f[no-]diagnostics-colors[=never|always|auto].
  enum {
    Colors_On,
    Colors_Off,
    Colors_Auto
  } ShowColors = DefaultColor ? Colors_Auto : Colors_Off;
  for (auto *A : Args) {
    const Option &O = A->getOption();
    if (O.matches(options::OPT_fcolor_diagnostics) ||
        O.matches(options::OPT_fdiagnostics_color)) {
      ShowColors = Colors_On;
    } else if (O.matches(options::OPT_fno_color_diagnostics) ||
               O.matches(options::OPT_fno_diagnostics_color)) {
      ShowColors = Colors_Off;
    } else if (O.matches(options::OPT_fdiagnostics_color_EQ)) {
      StringRef Value(A->getValue());
      if (Value == "always")
        ShowColors = Colors_On;
      else if (Value == "never")
        ShowColors = Colors_Off;
      else if (Value == "auto")
        ShowColors = Colors_Auto;
    }
  }
  return ShowColors == Colors_On ||
         (ShowColors == Colors_Auto &&
          llvm::sys::Process::StandardErrHasColors());
}

static bool checkVerifyPrefixes(const std::vector<std::string> &VerifyPrefixes,
                                DiagnosticsEngine *Diags) {
  bool Success = true;
  for (const auto &Prefix : VerifyPrefixes) {
    // Every prefix must start with a letter and contain only alphanumeric
    // characters, hyphens, and underscores.
    auto BadChar = std::find_if(Prefix.begin(), Prefix.end(),
                                [](char C){return !isAlphanumeric(C)
                                                  && C != '-' && C != '_';});
    if (BadChar != Prefix.end() || !isLetter(Prefix[0])) {
      Success = false;
      if (Diags) {
        Diags->Report(diag::err_drv_invalid_value) << "-verify=" << Prefix;
        Diags->Report(diag::note_drv_verify_prefix_spelling);
      }
    }
  }
  return Success;
}

bool clang::ParseDiagnosticArgs(DiagnosticOptions &Opts, ArgList &Args,
                                DiagnosticsEngine *Diags,
                                bool DefaultDiagColor, bool DefaultShowOpt) {
  bool Success = true;

  Opts.DiagnosticLogFile = Args.getLastArgValue(OPT_diagnostic_log_file);
  if (Arg *A =
          Args.getLastArg(OPT_diagnostic_serialized_file, OPT__serialize_diags))
    Opts.DiagnosticSerializationFile = A->getValue();
  Opts.IgnoreWarnings = Args.hasArg(OPT_w);
  Opts.NoRewriteMacros = Args.hasArg(OPT_Wno_rewrite_macros);
  Opts.Pedantic = Args.hasArg(OPT_pedantic);
  Opts.PedanticErrors = Args.hasArg(OPT_pedantic_errors);
  Opts.ShowCarets = !Args.hasArg(OPT_fno_caret_diagnostics);
  Opts.ShowColors = parseShowColorsArgs(Args, DefaultDiagColor);
  Opts.ShowColumn = Args.hasFlag(OPT_fshow_column,
                                 OPT_fno_show_column,
                                 /*Default=*/true);
  Opts.ShowFixits = !Args.hasArg(OPT_fno_diagnostics_fixit_info);
  Opts.ShowLocation = !Args.hasArg(OPT_fno_show_source_location);
  Opts.AbsolutePath = Args.hasArg(OPT_fdiagnostics_absolute_paths);
  Opts.ShowOptionNames =
      Args.hasFlag(OPT_fdiagnostics_show_option,
                   OPT_fno_diagnostics_show_option, DefaultShowOpt);

  llvm::sys::Process::UseANSIEscapeCodes(Args.hasArg(OPT_fansi_escape_codes));

  // Default behavior is to not to show note include stacks.
  Opts.ShowNoteIncludeStack = false;
  if (Arg *A = Args.getLastArg(OPT_fdiagnostics_show_note_include_stack,
                               OPT_fno_diagnostics_show_note_include_stack))
    if (A->getOption().matches(OPT_fdiagnostics_show_note_include_stack))
      Opts.ShowNoteIncludeStack = true;

  StringRef ShowOverloads =
    Args.getLastArgValue(OPT_fshow_overloads_EQ, "all");
  if (ShowOverloads == "best")
    Opts.setShowOverloads(Ovl_Best);
  else if (ShowOverloads == "all")
    Opts.setShowOverloads(Ovl_All);
  else {
    Success = false;
    if (Diags)
      Diags->Report(diag::err_drv_invalid_value)
      << Args.getLastArg(OPT_fshow_overloads_EQ)->getAsString(Args)
      << ShowOverloads;
  }

  StringRef ShowCategory =
    Args.getLastArgValue(OPT_fdiagnostics_show_category, "none");
  if (ShowCategory == "none")
    Opts.ShowCategories = 0;
  else if (ShowCategory == "id")
    Opts.ShowCategories = 1;
  else if (ShowCategory == "name")
    Opts.ShowCategories = 2;
  else {
    Success = false;
    if (Diags)
      Diags->Report(diag::err_drv_invalid_value)
      << Args.getLastArg(OPT_fdiagnostics_show_category)->getAsString(Args)
      << ShowCategory;
  }

  StringRef Format =
    Args.getLastArgValue(OPT_fdiagnostics_format, "clang");
  if (Format == "clang")
    Opts.setFormat(DiagnosticOptions::Clang);
  else if (Format == "msvc")
    Opts.setFormat(DiagnosticOptions::MSVC);
  else if (Format == "msvc-fallback") {
    Opts.setFormat(DiagnosticOptions::MSVC);
    Opts.CLFallbackMode = true;
  } else if (Format == "vi")
    Opts.setFormat(DiagnosticOptions::Vi);
  else {
    Success = false;
    if (Diags)
      Diags->Report(diag::err_drv_invalid_value)
      << Args.getLastArg(OPT_fdiagnostics_format)->getAsString(Args)
      << Format;
  }

  Opts.ShowSourceRanges = Args.hasArg(OPT_fdiagnostics_print_source_range_info);
  Opts.ShowParseableFixits = Args.hasArg(OPT_fdiagnostics_parseable_fixits);
  Opts.ShowPresumedLoc = !Args.hasArg(OPT_fno_diagnostics_use_presumed_location);
  Opts.VerifyDiagnostics = Args.hasArg(OPT_verify) || Args.hasArg(OPT_verify_EQ);
  Opts.VerifyPrefixes = Args.getAllArgValues(OPT_verify_EQ);
  if (Args.hasArg(OPT_verify))
    Opts.VerifyPrefixes.push_back("expected");
  // Keep VerifyPrefixes in its original order for the sake of diagnostics, and
  // then sort it to prepare for fast lookup using std::binary_search.
  if (!checkVerifyPrefixes(Opts.VerifyPrefixes, Diags)) {
    Opts.VerifyDiagnostics = false;
    Success = false;
  }
  else
    llvm::sort(Opts.VerifyPrefixes);
  DiagnosticLevelMask DiagMask = DiagnosticLevelMask::None;
  Success &= parseDiagnosticLevelMask("-verify-ignore-unexpected=",
    Args.getAllArgValues(OPT_verify_ignore_unexpected_EQ),
    Diags, DiagMask);
  if (Args.hasArg(OPT_verify_ignore_unexpected))
    DiagMask = DiagnosticLevelMask::All;
  Opts.setVerifyIgnoreUnexpected(DiagMask);
  Opts.ElideType = !Args.hasArg(OPT_fno_elide_type);
  Opts.ShowTemplateTree = Args.hasArg(OPT_fdiagnostics_show_template_tree);
  Opts.ErrorLimit = getLastArgIntValue(Args, OPT_ferror_limit, 0, Diags);
  Opts.MacroBacktraceLimit =
      getLastArgIntValue(Args, OPT_fmacro_backtrace_limit,
                         DiagnosticOptions::DefaultMacroBacktraceLimit, Diags);
  Opts.TemplateBacktraceLimit = getLastArgIntValue(
      Args, OPT_ftemplate_backtrace_limit,
      DiagnosticOptions::DefaultTemplateBacktraceLimit, Diags);
  Opts.ConstexprBacktraceLimit = getLastArgIntValue(
      Args, OPT_fconstexpr_backtrace_limit,
      DiagnosticOptions::DefaultConstexprBacktraceLimit, Diags);
  Opts.SpellCheckingLimit = getLastArgIntValue(
      Args, OPT_fspell_checking_limit,
      DiagnosticOptions::DefaultSpellCheckingLimit, Diags);
  Opts.SnippetLineLimit = getLastArgIntValue(
      Args, OPT_fcaret_diagnostics_max_lines,
      DiagnosticOptions::DefaultSnippetLineLimit, Diags);
  Opts.TabStop = getLastArgIntValue(Args, OPT_ftabstop,
                                    DiagnosticOptions::DefaultTabStop, Diags);
  if (Opts.TabStop == 0 || Opts.TabStop > DiagnosticOptions::MaxTabStop) {
    Opts.TabStop = DiagnosticOptions::DefaultTabStop;
    if (Diags)
      Diags->Report(diag::warn_ignoring_ftabstop_value)
      << Opts.TabStop << DiagnosticOptions::DefaultTabStop;
  }
  Opts.MessageLength = getLastArgIntValue(Args, OPT_fmessage_length, 0, Diags);
  addDiagnosticArgs(Args, OPT_W_Group, OPT_W_value_Group, Opts.Warnings);
  addDiagnosticArgs(Args, OPT_R_Group, OPT_R_value_Group, Opts.Remarks);

  return Success;
}

static void ParseFileSystemArgs(FileSystemOptions &Opts, ArgList &Args) {
  Opts.WorkingDir = Args.getLastArgValue(OPT_working_directory);
}

/// Parse the argument to the -ftest-module-file-extension
/// command-line argument.
///
/// \returns true on error, false on success.
static bool parseTestModuleFileExtensionArg(StringRef Arg,
                                            std::string &BlockName,
                                            unsigned &MajorVersion,
                                            unsigned &MinorVersion,
                                            bool &Hashed,
                                            std::string &UserInfo) {
  SmallVector<StringRef, 5> Args;
  Arg.split(Args, ':', 5);
  if (Args.size() < 5)
    return true;

  BlockName = Args[0];
  if (Args[1].getAsInteger(10, MajorVersion)) return true;
  if (Args[2].getAsInteger(10, MinorVersion)) return true;
  if (Args[3].getAsInteger(2, Hashed)) return true;
  if (Args.size() > 4)
    UserInfo = Args[4];
  return false;
}

static InputKind ParseFrontendArgs(FrontendOptions &Opts, ArgList &Args,
                                   DiagnosticsEngine &Diags,
                                   bool &IsHeaderFile) {
  Opts.ProgramAction = frontend::ParseSyntaxOnly;
  if (const Arg *A = Args.getLastArg(OPT_Action_Group)) {
    switch (A->getOption().getID()) {
    default:
      llvm_unreachable("Invalid option in group!");
    case OPT_ast_list:
      Opts.ProgramAction = frontend::ASTDeclList; break;
    case OPT_ast_dump:
    case OPT_ast_dump_all:
    case OPT_ast_dump_lookups:
      Opts.ProgramAction = frontend::ASTDump; break;
    case OPT_ast_print:
      Opts.ProgramAction = frontend::ASTPrint; break;
    case OPT_ast_view:
      Opts.ProgramAction = frontend::ASTView; break;
    case OPT_compiler_options_dump:
      Opts.ProgramAction = frontend::DumpCompilerOptions; break;
    case OPT_dump_raw_tokens:
      Opts.ProgramAction = frontend::DumpRawTokens; break;
    case OPT_dump_tokens:
      Opts.ProgramAction = frontend::DumpTokens; break;
    case OPT_S:
      Opts.ProgramAction = frontend::EmitAssembly; break;
    case OPT_emit_llvm_bc:
      Opts.ProgramAction = frontend::EmitBC; break;
    case OPT_emit_html:
      Opts.ProgramAction = frontend::EmitHTML; break;
    case OPT_emit_llvm:
      Opts.ProgramAction = frontend::EmitLLVM; break;
    case OPT_emit_llvm_only:
      Opts.ProgramAction = frontend::EmitLLVMOnly; break;
    case OPT_emit_codegen_only:
      Opts.ProgramAction = frontend::EmitCodeGenOnly; break;
    case OPT_emit_obj:
      Opts.ProgramAction = frontend::EmitObj; break;
    case OPT_fixit_EQ:
      Opts.FixItSuffix = A->getValue();
      LLVM_FALLTHROUGH;
    case OPT_fixit:
      Opts.ProgramAction = frontend::FixIt; break;
    case OPT_emit_module:
      Opts.ProgramAction = frontend::GenerateModule; break;
    case OPT_emit_module_interface:
      Opts.ProgramAction = frontend::GenerateModuleInterface; break;
    case OPT_emit_header_module:
      Opts.ProgramAction = frontend::GenerateHeaderModule; break;
    case OPT_emit_pch:
      Opts.ProgramAction = frontend::GeneratePCH; break;
    case OPT_init_only:
      Opts.ProgramAction = frontend::InitOnly; break;
    case OPT_fsyntax_only:
      Opts.ProgramAction = frontend::ParseSyntaxOnly; break;
    case OPT_module_file_info:
      Opts.ProgramAction = frontend::ModuleFileInfo; break;
    case OPT_verify_pch:
      Opts.ProgramAction = frontend::VerifyPCH; break;
    case OPT_print_preamble:
      Opts.ProgramAction = frontend::PrintPreamble; break;
    case OPT_E:
      Opts.ProgramAction = frontend::PrintPreprocessedInput; break;
    case OPT_templight_dump:
      Opts.ProgramAction = frontend::TemplightDump; break;
    case OPT_rewrite_macros:
      Opts.ProgramAction = frontend::RewriteMacros; break;
    case OPT_rewrite_objc:
      Opts.ProgramAction = frontend::RewriteObjC; break;
    case OPT_rewrite_test:
      Opts.ProgramAction = frontend::RewriteTest; break;
    case OPT_analyze:
      Opts.ProgramAction = frontend::RunAnalysis; break;
    case OPT_migrate:
      Opts.ProgramAction = frontend::MigrateSource; break;
    case OPT_Eonly:
      Opts.ProgramAction = frontend::RunPreprocessorOnly; break;
    }
  }

  if (const Arg* A = Args.getLastArg(OPT_plugin)) {
    Opts.Plugins.emplace_back(A->getValue(0));
    Opts.ProgramAction = frontend::PluginAction;
    Opts.ActionName = A->getValue();
  }
  Opts.AddPluginActions = Args.getAllArgValues(OPT_add_plugin);
  for (const auto *AA : Args.filtered(OPT_plugin_arg))
    Opts.PluginArgs[AA->getValue(0)].emplace_back(AA->getValue(1));

  for (const std::string &Arg :
         Args.getAllArgValues(OPT_ftest_module_file_extension_EQ)) {
    std::string BlockName;
    unsigned MajorVersion;
    unsigned MinorVersion;
    bool Hashed;
    std::string UserInfo;
    if (parseTestModuleFileExtensionArg(Arg, BlockName, MajorVersion,
                                        MinorVersion, Hashed, UserInfo)) {
      Diags.Report(diag::err_test_module_file_extension_format) << Arg;

      continue;
    }

    // Add the testing module file extension.
    Opts.ModuleFileExtensions.push_back(
        std::make_shared<TestModuleFileExtension>(
            BlockName, MajorVersion, MinorVersion, Hashed, UserInfo));
  }

  if (const Arg *A = Args.getLastArg(OPT_code_completion_at)) {
    Opts.CodeCompletionAt =
      ParsedSourceLocation::FromString(A->getValue());
    if (Opts.CodeCompletionAt.FileName.empty())
      Diags.Report(diag::err_drv_invalid_value)
        << A->getAsString(Args) << A->getValue();
  }
  Opts.DisableFree = Args.hasArg(OPT_disable_free);

  Opts.OutputFile = Args.getLastArgValue(OPT_o);
  Opts.Plugins = Args.getAllArgValues(OPT_load);
  Opts.RelocatablePCH = Args.hasArg(OPT_relocatable_pch);
  Opts.ShowHelp = Args.hasArg(OPT_help);
  Opts.ShowStats = Args.hasArg(OPT_print_stats);
  Opts.ShowTimers = Args.hasArg(OPT_ftime_report);
  Opts.ShowVersion = Args.hasArg(OPT_version);
  Opts.ASTMergeFiles = Args.getAllArgValues(OPT_ast_merge);
  Opts.LLVMArgs = Args.getAllArgValues(OPT_mllvm);
  Opts.FixWhatYouCan = Args.hasArg(OPT_fix_what_you_can);
  Opts.FixOnlyWarnings = Args.hasArg(OPT_fix_only_warnings);
  Opts.FixAndRecompile = Args.hasArg(OPT_fixit_recompile);
  Opts.FixToTemporaries = Args.hasArg(OPT_fixit_to_temp);
  Opts.ASTDumpDecls = Args.hasArg(OPT_ast_dump);
  Opts.ASTDumpAll = Args.hasArg(OPT_ast_dump_all);
  Opts.ASTDumpFilter = Args.getLastArgValue(OPT_ast_dump_filter);
  Opts.ASTDumpLookups = Args.hasArg(OPT_ast_dump_lookups);
  Opts.UseGlobalModuleIndex = !Args.hasArg(OPT_fno_modules_global_index);
  Opts.GenerateGlobalModuleIndex = Opts.UseGlobalModuleIndex;
  Opts.ModuleMapFiles = Args.getAllArgValues(OPT_fmodule_map_file);
  // Only the -fmodule-file=<file> form.
  for (const auto *A : Args.filtered(OPT_fmodule_file)) {
    StringRef Val = A->getValue();
    if (Val.find('=') == StringRef::npos)
      Opts.ModuleFiles.push_back(Val);
  }
  Opts.ModulesEmbedFiles = Args.getAllArgValues(OPT_fmodules_embed_file_EQ);
  Opts.ModulesEmbedAllFiles = Args.hasArg(OPT_fmodules_embed_all_files);
  Opts.IncludeTimestamps = !Args.hasArg(OPT_fno_pch_timestamp);

  Opts.CodeCompleteOpts.IncludeMacros
    = Args.hasArg(OPT_code_completion_macros);
  Opts.CodeCompleteOpts.IncludeCodePatterns
    = Args.hasArg(OPT_code_completion_patterns);
  Opts.CodeCompleteOpts.IncludeGlobals
    = !Args.hasArg(OPT_no_code_completion_globals);
  Opts.CodeCompleteOpts.IncludeNamespaceLevelDecls
    = !Args.hasArg(OPT_no_code_completion_ns_level_decls);
  Opts.CodeCompleteOpts.IncludeBriefComments
    = Args.hasArg(OPT_code_completion_brief_comments);
  Opts.CodeCompleteOpts.IncludeFixIts
    = Args.hasArg(OPT_code_completion_with_fixits);

  Opts.OverrideRecordLayoutsFile
    = Args.getLastArgValue(OPT_foverride_record_layout_EQ);
  Opts.AuxTriple = Args.getLastArgValue(OPT_aux_triple);
  Opts.StatsFile = Args.getLastArgValue(OPT_stats_file);

  if (const Arg *A = Args.getLastArg(OPT_arcmt_check,
                                     OPT_arcmt_modify,
                                     OPT_arcmt_migrate)) {
    switch (A->getOption().getID()) {
    default:
      llvm_unreachable("missed a case");
    case OPT_arcmt_check:
      Opts.ARCMTAction = FrontendOptions::ARCMT_Check;
      break;
    case OPT_arcmt_modify:
      Opts.ARCMTAction = FrontendOptions::ARCMT_Modify;
      break;
    case OPT_arcmt_migrate:
      Opts.ARCMTAction = FrontendOptions::ARCMT_Migrate;
      break;
    }
  }
  Opts.MTMigrateDir = Args.getLastArgValue(OPT_mt_migrate_directory);
  Opts.ARCMTMigrateReportOut
    = Args.getLastArgValue(OPT_arcmt_migrate_report_output);
  Opts.ARCMTMigrateEmitARCErrors
    = Args.hasArg(OPT_arcmt_migrate_emit_arc_errors);

  if (Args.hasArg(OPT_objcmt_migrate_literals))
    Opts.ObjCMTAction |= FrontendOptions::ObjCMT_Literals;
  if (Args.hasArg(OPT_objcmt_migrate_subscripting))
    Opts.ObjCMTAction |= FrontendOptions::ObjCMT_Subscripting;
  if (Args.hasArg(OPT_objcmt_migrate_property_dot_syntax))
    Opts.ObjCMTAction |= FrontendOptions::ObjCMT_PropertyDotSyntax;
  if (Args.hasArg(OPT_objcmt_migrate_property))
    Opts.ObjCMTAction |= FrontendOptions::ObjCMT_Property;
  if (Args.hasArg(OPT_objcmt_migrate_readonly_property))
    Opts.ObjCMTAction |= FrontendOptions::ObjCMT_ReadonlyProperty;
  if (Args.hasArg(OPT_objcmt_migrate_readwrite_property))
    Opts.ObjCMTAction |= FrontendOptions::ObjCMT_ReadwriteProperty;
  if (Args.hasArg(OPT_objcmt_migrate_annotation))
    Opts.ObjCMTAction |= FrontendOptions::ObjCMT_Annotation;
  if (Args.hasArg(OPT_objcmt_returns_innerpointer_property))
    Opts.ObjCMTAction |= FrontendOptions::ObjCMT_ReturnsInnerPointerProperty;
  if (Args.hasArg(OPT_objcmt_migrate_instancetype))
    Opts.ObjCMTAction |= FrontendOptions::ObjCMT_Instancetype;
  if (Args.hasArg(OPT_objcmt_migrate_nsmacros))
    Opts.ObjCMTAction |= FrontendOptions::ObjCMT_NsMacros;
  if (Args.hasArg(OPT_objcmt_migrate_protocol_conformance))
    Opts.ObjCMTAction |= FrontendOptions::ObjCMT_ProtocolConformance;
  if (Args.hasArg(OPT_objcmt_atomic_property))
    Opts.ObjCMTAction |= FrontendOptions::ObjCMT_AtomicProperty;
  if (Args.hasArg(OPT_objcmt_ns_nonatomic_iosonly))
    Opts.ObjCMTAction |= FrontendOptions::ObjCMT_NsAtomicIOSOnlyProperty;
  if (Args.hasArg(OPT_objcmt_migrate_designated_init))
    Opts.ObjCMTAction |= FrontendOptions::ObjCMT_DesignatedInitializer;
  if (Args.hasArg(OPT_objcmt_migrate_all))
    Opts.ObjCMTAction |= FrontendOptions::ObjCMT_MigrateDecls;

  Opts.ObjCMTWhiteListPath = Args.getLastArgValue(OPT_objcmt_whitelist_dir_path);

  if (Opts.ARCMTAction != FrontendOptions::ARCMT_None &&
      Opts.ObjCMTAction != FrontendOptions::ObjCMT_None) {
    Diags.Report(diag::err_drv_argument_not_allowed_with)
      << "ARC migration" << "ObjC migration";
  }

  InputKind DashX(InputKind::Unknown);
  if (const Arg *A = Args.getLastArg(OPT_x)) {
    StringRef XValue = A->getValue();

    // Parse suffixes: '<lang>(-header|[-module-map][-cpp-output])'.
    // FIXME: Supporting '<lang>-header-cpp-output' would be useful.
    bool Preprocessed = XValue.consume_back("-cpp-output");
    bool ModuleMap = XValue.consume_back("-module-map");
    IsHeaderFile =
        !Preprocessed && !ModuleMap && XValue.consume_back("-header");

    // Principal languages.
    DashX = llvm::StringSwitch<InputKind>(XValue)
                .Case("c", InputKind::C)
                .Case("cl", InputKind::OpenCL)
                .Case("cuda", InputKind::CUDA)
                .Case("hip", InputKind::HIP)
                .Case("c++", InputKind::CXX)
                .Case("objective-c", InputKind::ObjC)
                .Case("objective-c++", InputKind::ObjCXX)
                .Case("renderscript", InputKind::RenderScript)
                .Default(InputKind::Unknown);

    // "objc[++]-cpp-output" is an acceptable synonym for
    // "objective-c[++]-cpp-output".
    if (DashX.isUnknown() && Preprocessed && !IsHeaderFile && !ModuleMap)
      DashX = llvm::StringSwitch<InputKind>(XValue)
                  .Case("objc", InputKind::ObjC)
                  .Case("objc++", InputKind::ObjCXX)
                  .Default(InputKind::Unknown);

    // Some special cases cannot be combined with suffixes.
    if (DashX.isUnknown() && !Preprocessed && !ModuleMap && !IsHeaderFile)
      DashX = llvm::StringSwitch<InputKind>(XValue)
                  .Case("cpp-output", InputKind(InputKind::C).getPreprocessed())
                  .Case("assembler-with-cpp", InputKind::Asm)
                  .Cases("ast", "pcm",
                         InputKind(InputKind::Unknown, InputKind::Precompiled))
                  .Case("ir", InputKind::LLVM_IR)
                  .Default(InputKind::Unknown);

    if (DashX.isUnknown())
      Diags.Report(diag::err_drv_invalid_value)
        << A->getAsString(Args) << A->getValue();

    if (Preprocessed)
      DashX = DashX.getPreprocessed();
    if (ModuleMap)
      DashX = DashX.withFormat(InputKind::ModuleMap);
  }

  // '-' is the default input if none is given.
  std::vector<std::string> Inputs = Args.getAllArgValues(OPT_INPUT);
  Opts.Inputs.clear();
  if (Inputs.empty())
    Inputs.push_back("-");
  for (unsigned i = 0, e = Inputs.size(); i != e; ++i) {
    InputKind IK = DashX;
    if (IK.isUnknown()) {
      IK = FrontendOptions::getInputKindForExtension(
        StringRef(Inputs[i]).rsplit('.').second);
      // FIXME: Warn on this?
      if (IK.isUnknown())
        IK = InputKind::C;
      // FIXME: Remove this hack.
      if (i == 0)
        DashX = IK;
    }

    // The -emit-module action implicitly takes a module map.
    if (Opts.ProgramAction == frontend::GenerateModule &&
        IK.getFormat() == InputKind::Source)
      IK = IK.withFormat(InputKind::ModuleMap);

    Opts.Inputs.emplace_back(std::move(Inputs[i]), IK);
  }

  return DashX;
}

std::string CompilerInvocation::GetResourcesPath(const char *Argv0,
                                                 void *MainAddr) {
  std::string ClangExecutable =
      llvm::sys::fs::getMainExecutable(Argv0, MainAddr);
  StringRef Dir = llvm::sys::path::parent_path(ClangExecutable);

  // Compute the path to the resource directory.
  StringRef ClangResourceDir(CLANG_RESOURCE_DIR);
  SmallString<128> P(Dir);
  if (ClangResourceDir != "")
    llvm::sys::path::append(P, ClangResourceDir);
  else
    llvm::sys::path::append(P, "..", Twine("lib") + CLANG_LIBDIR_SUFFIX,
                            "clang", CLANG_VERSION_STRING);

  return P.str();
}

static void ParseHeaderSearchArgs(HeaderSearchOptions &Opts, ArgList &Args,
                                  const std::string &WorkingDir) {
  Opts.Sysroot = Args.getLastArgValue(OPT_isysroot, "/");
  Opts.Verbose = Args.hasArg(OPT_v);
  Opts.UseBuiltinIncludes = !Args.hasArg(OPT_nobuiltininc);
  Opts.UseStandardSystemIncludes = !Args.hasArg(OPT_nostdsysteminc);
  Opts.UseStandardCXXIncludes = !Args.hasArg(OPT_nostdincxx);
  if (const Arg *A = Args.getLastArg(OPT_stdlib_EQ))
    Opts.UseLibcxx = (strcmp(A->getValue(), "libc++") == 0);
  Opts.ResourceDir = Args.getLastArgValue(OPT_resource_dir);

  // Canonicalize -fmodules-cache-path before storing it.
  SmallString<128> P(Args.getLastArgValue(OPT_fmodules_cache_path));
  if (!(P.empty() || llvm::sys::path::is_absolute(P))) {
    if (WorkingDir.empty())
      llvm::sys::fs::make_absolute(P);
    else
      llvm::sys::fs::make_absolute(WorkingDir, P);
  }
  llvm::sys::path::remove_dots(P);
  Opts.ModuleCachePath = P.str();

  Opts.ModuleUserBuildPath = Args.getLastArgValue(OPT_fmodules_user_build_path);
  // Only the -fmodule-file=<name>=<file> form.
  for (const auto *A : Args.filtered(OPT_fmodule_file)) {
    StringRef Val = A->getValue();
    if (Val.find('=') != StringRef::npos)
      Opts.PrebuiltModuleFiles.insert(Val.split('='));
  }
  for (const auto *A : Args.filtered(OPT_fprebuilt_module_path))
    Opts.AddPrebuiltModulePath(A->getValue());
  Opts.DisableModuleHash = Args.hasArg(OPT_fdisable_module_hash);
  Opts.ModulesHashContent = Args.hasArg(OPT_fmodules_hash_content);
  Opts.ModulesValidateDiagnosticOptions =
      !Args.hasArg(OPT_fmodules_disable_diagnostic_validation);
  Opts.ImplicitModuleMaps = Args.hasArg(OPT_fimplicit_module_maps);
  Opts.ModuleMapFileHomeIsCwd = Args.hasArg(OPT_fmodule_map_file_home_is_cwd);
  Opts.ModuleCachePruneInterval =
      getLastArgIntValue(Args, OPT_fmodules_prune_interval, 7 * 24 * 60 * 60);
  Opts.ModuleCachePruneAfter =
      getLastArgIntValue(Args, OPT_fmodules_prune_after, 31 * 24 * 60 * 60);
  Opts.ModulesValidateOncePerBuildSession =
      Args.hasArg(OPT_fmodules_validate_once_per_build_session);
  Opts.BuildSessionTimestamp =
      getLastArgUInt64Value(Args, OPT_fbuild_session_timestamp, 0);
  Opts.ModulesValidateSystemHeaders =
      Args.hasArg(OPT_fmodules_validate_system_headers);
  if (const Arg *A = Args.getLastArg(OPT_fmodule_format_EQ))
    Opts.ModuleFormat = A->getValue();

  for (const auto *A : Args.filtered(OPT_fmodules_ignore_macro)) {
    StringRef MacroDef = A->getValue();
    Opts.ModulesIgnoreMacros.insert(
        llvm::CachedHashString(MacroDef.split('=').first));
  }

  // Add -I..., -F..., and -index-header-map options in order.
  bool IsIndexHeaderMap = false;
  bool IsSysrootSpecified =
      Args.hasArg(OPT__sysroot_EQ) || Args.hasArg(OPT_isysroot);
  for (const auto *A : Args.filtered(OPT_I, OPT_F, OPT_index_header_map)) {
    if (A->getOption().matches(OPT_index_header_map)) {
      // -index-header-map applies to the next -I or -F.
      IsIndexHeaderMap = true;
      continue;
    }

    frontend::IncludeDirGroup Group =
        IsIndexHeaderMap ? frontend::IndexHeaderMap : frontend::Angled;

    bool IsFramework = A->getOption().matches(OPT_F);
    std::string Path = A->getValue();

    if (IsSysrootSpecified && !IsFramework && A->getValue()[0] == '=') {
      SmallString<32> Buffer;
      llvm::sys::path::append(Buffer, Opts.Sysroot,
                              llvm::StringRef(A->getValue()).substr(1));
      Path = Buffer.str();
    }

    Opts.AddPath(Path, Group, IsFramework,
                 /*IgnoreSysroot*/ true);
    IsIndexHeaderMap = false;
  }

  // Add -iprefix/-iwithprefix/-iwithprefixbefore options.
  StringRef Prefix = ""; // FIXME: This isn't the correct default prefix.
  for (const auto *A :
       Args.filtered(OPT_iprefix, OPT_iwithprefix, OPT_iwithprefixbefore)) {
    if (A->getOption().matches(OPT_iprefix))
      Prefix = A->getValue();
    else if (A->getOption().matches(OPT_iwithprefix))
      Opts.AddPath(Prefix.str() + A->getValue(), frontend::After, false, true);
    else
      Opts.AddPath(Prefix.str() + A->getValue(), frontend::Angled, false, true);
  }

  for (const auto *A : Args.filtered(OPT_idirafter))
    Opts.AddPath(A->getValue(), frontend::After, false, true);
  for (const auto *A : Args.filtered(OPT_iquote))
    Opts.AddPath(A->getValue(), frontend::Quoted, false, true);
  for (const auto *A : Args.filtered(OPT_isystem, OPT_iwithsysroot))
    Opts.AddPath(A->getValue(), frontend::System, false,
                 !A->getOption().matches(OPT_iwithsysroot));
  for (const auto *A : Args.filtered(OPT_iframework))
    Opts.AddPath(A->getValue(), frontend::System, true, true);
  for (const auto *A : Args.filtered(OPT_iframeworkwithsysroot))
    Opts.AddPath(A->getValue(), frontend::System, /*IsFramework=*/true,
                 /*IgnoreSysRoot=*/false);

  // Add the paths for the various language specific isystem flags.
  for (const auto *A : Args.filtered(OPT_c_isystem))
    Opts.AddPath(A->getValue(), frontend::CSystem, false, true);
  for (const auto *A : Args.filtered(OPT_cxx_isystem))
    Opts.AddPath(A->getValue(), frontend::CXXSystem, false, true);
  for (const auto *A : Args.filtered(OPT_objc_isystem))
    Opts.AddPath(A->getValue(), frontend::ObjCSystem, false,true);
  for (const auto *A : Args.filtered(OPT_objcxx_isystem))
    Opts.AddPath(A->getValue(), frontend::ObjCXXSystem, false, true);

  // Add the internal paths from a driver that detects standard include paths.
  for (const auto *A :
       Args.filtered(OPT_internal_isystem, OPT_internal_externc_isystem)) {
    frontend::IncludeDirGroup Group = frontend::System;
    if (A->getOption().matches(OPT_internal_externc_isystem))
      Group = frontend::ExternCSystem;
    Opts.AddPath(A->getValue(), Group, false, true);
  }

  // Add the path prefixes which are implicitly treated as being system headers.
  for (const auto *A :
       Args.filtered(OPT_system_header_prefix, OPT_no_system_header_prefix))
    Opts.AddSystemHeaderPrefix(
        A->getValue(), A->getOption().matches(OPT_system_header_prefix));

  for (const auto *A : Args.filtered(OPT_ivfsoverlay))
    Opts.AddVFSOverlayFile(A->getValue());
}

void CompilerInvocation::setLangDefaults(LangOptions &Opts, InputKind IK,
                                         const llvm::Triple &T,
                                         PreprocessorOptions &PPOpts,
                                         LangStandard::Kind LangStd) {
  // Set some properties which depend solely on the input kind; it would be nice
  // to move these to the language standard, and have the driver resolve the
  // input kind + language standard.
  //
  // FIXME: Perhaps a better model would be for a single source file to have
  // multiple language standards (C / C++ std, ObjC std, OpenCL std, OpenMP std)
  // simultaneously active?
  if (IK.getLanguage() == InputKind::Asm) {
    Opts.AsmPreprocessor = 1;
  } else if (IK.isObjectiveC()) {
    Opts.ObjC = 1;
  }

  if (LangStd == LangStandard::lang_unspecified) {
    // Based on the base language, pick one.
    switch (IK.getLanguage()) {
    case InputKind::Unknown:
    case InputKind::LLVM_IR:
      llvm_unreachable("Invalid input kind!");
    case InputKind::OpenCL:
      LangStd = LangStandard::lang_opencl10;
      break;
    case InputKind::CUDA:
      LangStd = LangStandard::lang_cuda;
      break;
    case InputKind::Asm:
    case InputKind::C:
#if defined(CLANG_DEFAULT_STD_C)
      LangStd = CLANG_DEFAULT_STD_C;
#else
      // The PS4 uses C99 as the default C standard.
      if (T.isPS4())
        LangStd = LangStandard::lang_gnu99;
      else
        LangStd = LangStandard::lang_gnu11;
#endif
      break;
    case InputKind::ObjC:
#if defined(CLANG_DEFAULT_STD_C)
      LangStd = CLANG_DEFAULT_STD_C;
#else
      LangStd = LangStandard::lang_gnu11;
#endif
      break;
    case InputKind::CXX:
    case InputKind::ObjCXX:
#if defined(CLANG_DEFAULT_STD_CXX)
      LangStd = CLANG_DEFAULT_STD_CXX;
#else
      LangStd = LangStandard::lang_gnucxx14;
#endif
      break;
    case InputKind::RenderScript:
      LangStd = LangStandard::lang_c99;
      break;
    case InputKind::HIP:
      LangStd = LangStandard::lang_hip;
      break;
    }
  }

  const LangStandard &Std = LangStandard::getLangStandardForKind(LangStd);
  Opts.LineComment = Std.hasLineComments();
  Opts.C99 = Std.isC99();
  Opts.C11 = Std.isC11();
  Opts.C17 = Std.isC17();
  Opts.CPlusPlus = Std.isCPlusPlus();
  Opts.CPlusPlus11 = Std.isCPlusPlus11();
  Opts.CPlusPlus14 = Std.isCPlusPlus14();
  Opts.CPlusPlus17 = Std.isCPlusPlus17();
  Opts.CPlusPlus2a = Std.isCPlusPlus2a();
  Opts.Digraphs = Std.hasDigraphs();
  Opts.GNUMode = Std.isGNUMode();
  Opts.GNUInline = !Opts.C99 && !Opts.CPlusPlus;
  Opts.HexFloats = Std.hasHexFloats();
  Opts.ImplicitInt = Std.hasImplicitInt();

  // Set OpenCL Version.
  Opts.OpenCL = Std.isOpenCL();
  if (LangStd == LangStandard::lang_opencl10)
    Opts.OpenCLVersion = 100;
  else if (LangStd == LangStandard::lang_opencl11)
    Opts.OpenCLVersion = 110;
  else if (LangStd == LangStandard::lang_opencl12)
    Opts.OpenCLVersion = 120;
  else if (LangStd == LangStandard::lang_opencl20)
    Opts.OpenCLVersion = 200;
  else if (LangStd == LangStandard::lang_openclcpp)
    Opts.OpenCLCPlusPlusVersion = 100;

  // OpenCL has some additional defaults.
  if (Opts.OpenCL) {
    Opts.AltiVec = 0;
    Opts.ZVector = 0;
    Opts.LaxVectorConversions = 0;
    Opts.setDefaultFPContractMode(LangOptions::FPC_On);
    Opts.NativeHalfType = 1;
    Opts.NativeHalfArgsAndReturns = 1;
    Opts.OpenCLCPlusPlus = Opts.CPlusPlus;
    // Include default header file for OpenCL.
    if (Opts.IncludeDefaultHeader) {
      PPOpts.Includes.push_back("opencl-c.h");
    }
  }

  Opts.HIP = IK.getLanguage() == InputKind::HIP;
  Opts.CUDA = IK.getLanguage() == InputKind::CUDA || Opts.HIP;
  if (Opts.CUDA)
    // Set default FP_CONTRACT to FAST.
    Opts.setDefaultFPContractMode(LangOptions::FPC_Fast);

  Opts.RenderScript = IK.getLanguage() == InputKind::RenderScript;
  if (Opts.RenderScript) {
    Opts.NativeHalfType = 1;
    Opts.NativeHalfArgsAndReturns = 1;
  }

  // OpenCL and C++ both have bool, true, false keywords.
  Opts.Bool = Opts.OpenCL || Opts.CPlusPlus;

  // OpenCL has half keyword
  Opts.Half = Opts.OpenCL;

  // C++ has wchar_t keyword.
  Opts.WChar = Opts.CPlusPlus;

  Opts.GNUKeywords = Opts.GNUMode;
  Opts.CXXOperatorNames = Opts.CPlusPlus;

  Opts.AlignedAllocation = Opts.CPlusPlus17;

  Opts.DollarIdents = !Opts.AsmPreprocessor;
}

/// Attempt to parse a visibility value out of the given argument.
static Visibility parseVisibility(Arg *arg, ArgList &args,
                                  DiagnosticsEngine &diags) {
  StringRef value = arg->getValue();
  if (value == "default") {
    return DefaultVisibility;
  } else if (value == "hidden" || value == "internal") {
    return HiddenVisibility;
  } else if (value == "protected") {
    // FIXME: diagnose if target does not support protected visibility
    return ProtectedVisibility;
  }

  diags.Report(diag::err_drv_invalid_value)
    << arg->getAsString(args) << value;
  return DefaultVisibility;
}

/// Check if input file kind and language standard are compatible.
static bool IsInputCompatibleWithStandard(InputKind IK,
                                          const LangStandard &S) {
  switch (IK.getLanguage()) {
  case InputKind::Unknown:
  case InputKind::LLVM_IR:
    llvm_unreachable("should not parse language flags for this input");

  case InputKind::C:
  case InputKind::ObjC:
  case InputKind::RenderScript:
    return S.getLanguage() == InputKind::C;

  case InputKind::OpenCL:
    return S.getLanguage() == InputKind::OpenCL;

  case InputKind::CXX:
  case InputKind::ObjCXX:
    return S.getLanguage() == InputKind::CXX;

  case InputKind::CUDA:
    // FIXME: What -std= values should be permitted for CUDA compilations?
    return S.getLanguage() == InputKind::CUDA ||
           S.getLanguage() == InputKind::CXX;

  case InputKind::HIP:
    return S.getLanguage() == InputKind::CXX ||
           S.getLanguage() == InputKind::HIP;

  case InputKind::Asm:
    // Accept (and ignore) all -std= values.
    // FIXME: The -std= value is not ignored; it affects the tokenization
    // and preprocessing rules if we're preprocessing this asm input.
    return true;
  }

  llvm_unreachable("unexpected input language");
}

/// Get language name for given input kind.
static const StringRef GetInputKindName(InputKind IK) {
  switch (IK.getLanguage()) {
  case InputKind::C:
    return "C";
  case InputKind::ObjC:
    return "Objective-C";
  case InputKind::CXX:
    return "C++";
  case InputKind::ObjCXX:
    return "Objective-C++";
  case InputKind::OpenCL:
    return "OpenCL";
  case InputKind::CUDA:
    return "CUDA";
  case InputKind::RenderScript:
    return "RenderScript";
  case InputKind::HIP:
    return "HIP";

  case InputKind::Asm:
    return "Asm";
  case InputKind::LLVM_IR:
    return "LLVM IR";

  case InputKind::Unknown:
    break;
  }
  llvm_unreachable("unknown input language");
}

static void ParseLangArgs(LangOptions &Opts, ArgList &Args, InputKind IK,
                          const TargetOptions &TargetOpts,
                          PreprocessorOptions &PPOpts,
                          DiagnosticsEngine &Diags) {
  // FIXME: Cleanup per-file based stuff.
  LangStandard::Kind LangStd = LangStandard::lang_unspecified;
  if (const Arg *A = Args.getLastArg(OPT_std_EQ)) {
    LangStd = llvm::StringSwitch<LangStandard::Kind>(A->getValue())
#define LANGSTANDARD(id, name, lang, desc, features) \
      .Case(name, LangStandard::lang_##id)
#define LANGSTANDARD_ALIAS(id, alias) \
      .Case(alias, LangStandard::lang_##id)
#include "clang/Frontend/LangStandards.def"
      .Default(LangStandard::lang_unspecified);
    if (LangStd == LangStandard::lang_unspecified) {
      Diags.Report(diag::err_drv_invalid_value)
        << A->getAsString(Args) << A->getValue();
      // Report supported standards with short description.
      for (unsigned KindValue = 0;
           KindValue != LangStandard::lang_unspecified;
           ++KindValue) {
        const LangStandard &Std = LangStandard::getLangStandardForKind(
          static_cast<LangStandard::Kind>(KindValue));
        if (IsInputCompatibleWithStandard(IK, Std)) {
          auto Diag = Diags.Report(diag::note_drv_use_standard);
          Diag << Std.getName() << Std.getDescription();
          unsigned NumAliases = 0;
#define LANGSTANDARD(id, name, lang, desc, features)
#define LANGSTANDARD_ALIAS(id, alias) \
          if (KindValue == LangStandard::lang_##id) ++NumAliases;
#define LANGSTANDARD_ALIAS_DEPR(id, alias)
#include "clang/Frontend/LangStandards.def"
          Diag << NumAliases;
#define LANGSTANDARD(id, name, lang, desc, features)
#define LANGSTANDARD_ALIAS(id, alias) \
          if (KindValue == LangStandard::lang_##id) Diag << alias;
#define LANGSTANDARD_ALIAS_DEPR(id, alias)
#include "clang/Frontend/LangStandards.def"
        }
      }
    } else {
      // Valid standard, check to make sure language and standard are
      // compatible.
      const LangStandard &Std = LangStandard::getLangStandardForKind(LangStd);
      if (!IsInputCompatibleWithStandard(IK, Std)) {
        Diags.Report(diag::err_drv_argument_not_allowed_with)
          << A->getAsString(Args) << GetInputKindName(IK);
      }
    }
  }

  if (Args.hasArg(OPT_fno_dllexport_inlines))
    Opts.DllExportInlines = false;

  if (const Arg *A = Args.getLastArg(OPT_fcf_protection_EQ)) {
    StringRef Name = A->getValue();
    if (Name == "full" || Name == "branch") {
      Opts.CFProtectionBranch = 1;
    }
  }
  // -cl-std only applies for OpenCL language standards.
  // Override the -std option in this case.
  if (const Arg *A = Args.getLastArg(OPT_cl_std_EQ)) {
    LangStandard::Kind OpenCLLangStd
      = llvm::StringSwitch<LangStandard::Kind>(A->getValue())
        .Cases("cl", "CL", LangStandard::lang_opencl10)
        .Cases("cl1.1", "CL1.1", LangStandard::lang_opencl11)
        .Cases("cl1.2", "CL1.2", LangStandard::lang_opencl12)
        .Cases("cl2.0", "CL2.0", LangStandard::lang_opencl20)
        .Case("c++", LangStandard::lang_openclcpp)
        .Default(LangStandard::lang_unspecified);

    if (OpenCLLangStd == LangStandard::lang_unspecified) {
      Diags.Report(diag::err_drv_invalid_value)
        << A->getAsString(Args) << A->getValue();
    }
    else
      LangStd = OpenCLLangStd;
  }

  Opts.IncludeDefaultHeader = Args.hasArg(OPT_finclude_default_header);

  llvm::Triple T(TargetOpts.Triple);
  CompilerInvocation::setLangDefaults(Opts, IK, T, PPOpts, LangStd);

  // -cl-strict-aliasing needs to emit diagnostic in the case where CL > 1.0.
  // This option should be deprecated for CL > 1.0 because
  // this option was added for compatibility with OpenCL 1.0.
  if (Args.getLastArg(OPT_cl_strict_aliasing)
       && Opts.OpenCLVersion > 100) {
    Diags.Report(diag::warn_option_invalid_ocl_version)
        << Opts.getOpenCLVersionTuple().getAsString()
        << Args.getLastArg(OPT_cl_strict_aliasing)->getAsString(Args);
  }

  // We abuse '-f[no-]gnu-keywords' to force overriding all GNU-extension
  // keywords. This behavior is provided by GCC's poorly named '-fasm' flag,
  // while a subset (the non-C++ GNU keywords) is provided by GCC's
  // '-fgnu-keywords'. Clang conflates the two for simplicity under the single
  // name, as it doesn't seem a useful distinction.
  Opts.GNUKeywords = Args.hasFlag(OPT_fgnu_keywords, OPT_fno_gnu_keywords,
                                  Opts.GNUKeywords);

  Opts.Digraphs = Args.hasFlag(OPT_fdigraphs, OPT_fno_digraphs, Opts.Digraphs);

  if (Args.hasArg(OPT_fno_operator_names))
    Opts.CXXOperatorNames = 0;

  if (Args.hasArg(OPT_fcuda_is_device))
    Opts.CUDAIsDevice = 1;

  if (Args.hasArg(OPT_fcuda_allow_variadic_functions))
    Opts.CUDAAllowVariadicFunctions = 1;

  if (Args.hasArg(OPT_fno_cuda_host_device_constexpr))
    Opts.CUDAHostDeviceConstexpr = 0;

  if (Opts.CUDAIsDevice && Args.hasArg(OPT_fcuda_approx_transcendentals))
    Opts.CUDADeviceApproxTranscendentals = 1;

  Opts.GPURelocatableDeviceCode = Args.hasArg(OPT_fgpu_rdc);

  if (Opts.ObjC) {
    if (Arg *arg = Args.getLastArg(OPT_fobjc_runtime_EQ)) {
      StringRef value = arg->getValue();
      if (Opts.ObjCRuntime.tryParse(value))
        Diags.Report(diag::err_drv_unknown_objc_runtime) << value;
    }

    if (Args.hasArg(OPT_fobjc_gc_only))
      Opts.setGC(LangOptions::GCOnly);
    else if (Args.hasArg(OPT_fobjc_gc))
      Opts.setGC(LangOptions::HybridGC);
    else if (Args.hasArg(OPT_fobjc_arc)) {
      Opts.ObjCAutoRefCount = 1;
      if (!Opts.ObjCRuntime.allowsARC())
        Diags.Report(diag::err_arc_unsupported_on_runtime);
    }

    // ObjCWeakRuntime tracks whether the runtime supports __weak, not
    // whether the feature is actually enabled.  This is predominantly
    // determined by -fobjc-runtime, but we allow it to be overridden
    // from the command line for testing purposes.
    if (Args.hasArg(OPT_fobjc_runtime_has_weak))
      Opts.ObjCWeakRuntime = 1;
    else
      Opts.ObjCWeakRuntime = Opts.ObjCRuntime.allowsWeak();

    // ObjCWeak determines whether __weak is actually enabled.
    // Note that we allow -fno-objc-weak to disable this even in ARC mode.
    if (auto weakArg = Args.getLastArg(OPT_fobjc_weak, OPT_fno_objc_weak)) {
      if (!weakArg->getOption().matches(OPT_fobjc_weak)) {
        assert(!Opts.ObjCWeak);
      } else if (Opts.getGC() != LangOptions::NonGC) {
        Diags.Report(diag::err_objc_weak_with_gc);
      } else if (!Opts.ObjCWeakRuntime) {
        Diags.Report(diag::err_objc_weak_unsupported);
      } else {
        Opts.ObjCWeak = 1;
      }
    } else if (Opts.ObjCAutoRefCount) {
      Opts.ObjCWeak = Opts.ObjCWeakRuntime;
    }

    if (Args.hasArg(OPT_fno_objc_infer_related_result_type))
      Opts.ObjCInferRelatedResultType = 0;

    if (Args.hasArg(OPT_fobjc_subscripting_legacy_runtime))
      Opts.ObjCSubscriptingLegacyRuntime =
        (Opts.ObjCRuntime.getKind() == ObjCRuntime::FragileMacOSX);
  }

  if (Args.hasArg(OPT_fgnu89_inline)) {
    if (Opts.CPlusPlus)
      Diags.Report(diag::err_drv_argument_not_allowed_with)
        << "-fgnu89-inline" << GetInputKindName(IK);
    else
      Opts.GNUInline = 1;
  }

  if (Args.hasArg(OPT_fapple_kext)) {
    if (!Opts.CPlusPlus)
      Diags.Report(diag::warn_c_kext);
    else
      Opts.AppleKext = 1;
  }

  if (Args.hasArg(OPT_print_ivar_layout))
    Opts.ObjCGCBitmapPrint = 1;

  if (Args.hasArg(OPT_fno_constant_cfstrings))
    Opts.NoConstantCFStrings = 1;
  if (const auto *A = Args.getLastArg(OPT_fcf_runtime_abi_EQ))
    Opts.CFRuntime =
        llvm::StringSwitch<LangOptions::CoreFoundationABI>(A->getValue())
            .Cases("unspecified", "standalone", "objc",
                   LangOptions::CoreFoundationABI::ObjectiveC)
            .Cases("swift", "swift-5.0",
                   LangOptions::CoreFoundationABI::Swift5_0)
            .Case("swift-4.2", LangOptions::CoreFoundationABI::Swift4_2)
            .Case("swift-4.1", LangOptions::CoreFoundationABI::Swift4_1)
            .Default(LangOptions::CoreFoundationABI::ObjectiveC);

  if (Args.hasArg(OPT_fzvector))
    Opts.ZVector = 1;

  if (Args.hasArg(OPT_pthread))
    Opts.POSIXThreads = 1;

  // The value-visibility mode defaults to "default".
  if (Arg *visOpt = Args.getLastArg(OPT_fvisibility)) {
    Opts.setValueVisibilityMode(parseVisibility(visOpt, Args, Diags));
  } else {
    Opts.setValueVisibilityMode(DefaultVisibility);
  }

  // The type-visibility mode defaults to the value-visibility mode.
  if (Arg *typeVisOpt = Args.getLastArg(OPT_ftype_visibility)) {
    Opts.setTypeVisibilityMode(parseVisibility(typeVisOpt, Args, Diags));
  } else {
    Opts.setTypeVisibilityMode(Opts.getValueVisibilityMode());
  }

  if (Args.hasArg(OPT_fvisibility_inlines_hidden))
    Opts.InlineVisibilityHidden = 1;

  if (Args.hasArg(OPT_fvisibility_global_new_delete_hidden))
    Opts.GlobalAllocationFunctionVisibilityHidden = 1;

  if (Args.hasArg(OPT_ftrapv)) {
    Opts.setSignedOverflowBehavior(LangOptions::SOB_Trapping);
    // Set the handler, if one is specified.
    Opts.OverflowHandler =
        Args.getLastArgValue(OPT_ftrapv_handler);
  }
  else if (Args.hasArg(OPT_fwrapv))
    Opts.setSignedOverflowBehavior(LangOptions::SOB_Defined);

  Opts.MSVCCompat = Args.hasArg(OPT_fms_compatibility);
  Opts.MicrosoftExt = Opts.MSVCCompat || Args.hasArg(OPT_fms_extensions);
  Opts.AsmBlocks = Args.hasArg(OPT_fasm_blocks) || Opts.MicrosoftExt;
  Opts.MSCompatibilityVersion = 0;
  if (const Arg *A = Args.getLastArg(OPT_fms_compatibility_version)) {
    VersionTuple VT;
    if (VT.tryParse(A->getValue()))
      Diags.Report(diag::err_drv_invalid_value) << A->getAsString(Args)
                                                << A->getValue();
    Opts.MSCompatibilityVersion = VT.getMajor() * 10000000 +
                                  VT.getMinor().getValueOr(0) * 100000 +
                                  VT.getSubminor().getValueOr(0);
  }

  // Mimicking gcc's behavior, trigraphs are only enabled if -trigraphs
  // is specified, or -std is set to a conforming mode.
  // Trigraphs are disabled by default in c++1z onwards.
  Opts.Trigraphs = !Opts.GNUMode && !Opts.MSVCCompat && !Opts.CPlusPlus17;
  Opts.Trigraphs =
      Args.hasFlag(OPT_ftrigraphs, OPT_fno_trigraphs, Opts.Trigraphs);

  Opts.DollarIdents = Args.hasFlag(OPT_fdollars_in_identifiers,
                                   OPT_fno_dollars_in_identifiers,
                                   Opts.DollarIdents);
  Opts.PascalStrings = Args.hasArg(OPT_fpascal_strings);
  Opts.VtorDispMode = getLastArgIntValue(Args, OPT_vtordisp_mode_EQ, 1, Diags);
  Opts.Borland = Args.hasArg(OPT_fborland_extensions);
  Opts.WritableStrings = Args.hasArg(OPT_fwritable_strings);
  Opts.ConstStrings = Args.hasFlag(OPT_fconst_strings, OPT_fno_const_strings,
                                   Opts.ConstStrings);
  if (Args.hasArg(OPT_fno_lax_vector_conversions))
    Opts.LaxVectorConversions = 0;
  if (Args.hasArg(OPT_fno_threadsafe_statics))
    Opts.ThreadsafeStatics = 0;
  Opts.Exceptions = Args.hasArg(OPT_fexceptions);
  Opts.ObjCExceptions = Args.hasArg(OPT_fobjc_exceptions);
  Opts.CXXExceptions = Args.hasArg(OPT_fcxx_exceptions);

  // -ffixed-point
  Opts.FixedPoint =
      Args.hasFlag(OPT_ffixed_point, OPT_fno_fixed_point, /*Default=*/false) &&
      !Opts.CPlusPlus;
  Opts.PaddingOnUnsignedFixedPoint =
      Args.hasFlag(OPT_fpadding_on_unsigned_fixed_point,
                   OPT_fno_padding_on_unsigned_fixed_point,
                   /*Default=*/false) &&
      Opts.FixedPoint;

  // Handle exception personalities
  Arg *A = Args.getLastArg(options::OPT_fsjlj_exceptions,
                           options::OPT_fseh_exceptions,
                           options::OPT_fdwarf_exceptions);
  if (A) {
    const Option &Opt = A->getOption();
    llvm::Triple T(TargetOpts.Triple);
    if (T.isWindowsMSVCEnvironment())
      Diags.Report(diag::err_fe_invalid_exception_model)
          << Opt.getName() << T.str();

    Opts.SjLjExceptions = Opt.matches(options::OPT_fsjlj_exceptions);
    Opts.SEHExceptions = Opt.matches(options::OPT_fseh_exceptions);
    Opts.DWARFExceptions = Opt.matches(options::OPT_fdwarf_exceptions);
  }

  Opts.ExternCNoUnwind = Args.hasArg(OPT_fexternc_nounwind);
  Opts.TraditionalCPP = Args.hasArg(OPT_traditional_cpp);

  Opts.RTTI = Opts.CPlusPlus && !Args.hasArg(OPT_fno_rtti);
  Opts.RTTIData = Opts.RTTI && !Args.hasArg(OPT_fno_rtti_data);
  Opts.Blocks = Args.hasArg(OPT_fblocks) || (Opts.OpenCL
    && Opts.OpenCLVersion == 200);
  Opts.BlocksRuntimeOptional = Args.hasArg(OPT_fblocks_runtime_optional);
  Opts.CoroutinesTS = Args.hasArg(OPT_fcoroutines_ts);

  // Enable [[]] attributes in C++11 by default.
  Opts.DoubleSquareBracketAttributes =
      Args.hasFlag(OPT_fdouble_square_bracket_attributes,
                   OPT_fno_double_square_bracket_attributes, Opts.CPlusPlus11);

  Opts.ModulesTS = Args.hasArg(OPT_fmodules_ts);
  Opts.Modules = Args.hasArg(OPT_fmodules) || Opts.ModulesTS;
  Opts.ModulesStrictDeclUse = Args.hasArg(OPT_fmodules_strict_decluse);
  Opts.ModulesDeclUse =
      Args.hasArg(OPT_fmodules_decluse) || Opts.ModulesStrictDeclUse;
  Opts.ModulesLocalVisibility =
      Args.hasArg(OPT_fmodules_local_submodule_visibility) || Opts.ModulesTS;
  Opts.ModulesCodegen = Args.hasArg(OPT_fmodules_codegen);
  Opts.ModulesDebugInfo = Args.hasArg(OPT_fmodules_debuginfo);
  Opts.ModulesSearchAll = Opts.Modules &&
    !Args.hasArg(OPT_fno_modules_search_all) &&
    Args.hasArg(OPT_fmodules_search_all);
  Opts.ModulesErrorRecovery = !Args.hasArg(OPT_fno_modules_error_recovery);
  Opts.ImplicitModules = !Args.hasArg(OPT_fno_implicit_modules);
  Opts.CharIsSigned = Opts.OpenCL || !Args.hasArg(OPT_fno_signed_char);
  Opts.WChar = Opts.CPlusPlus && !Args.hasArg(OPT_fno_wchar);
  Opts.Char8 = Args.hasFlag(OPT_fchar8__t, OPT_fno_char8__t, Opts.CPlusPlus2a);
  if (const Arg *A = Args.getLastArg(OPT_fwchar_type_EQ)) {
    Opts.WCharSize = llvm::StringSwitch<unsigned>(A->getValue())
                         .Case("char", 1)
                         .Case("short", 2)
                         .Case("int", 4)
                         .Default(0);
    if (Opts.WCharSize == 0)
      Diags.Report(diag::err_fe_invalid_wchar_type) << A->getValue();
  }
  Opts.WCharIsSigned = Args.hasFlag(OPT_fsigned_wchar, OPT_fno_signed_wchar, true);
  Opts.ShortEnums = Args.hasArg(OPT_fshort_enums);
  Opts.Freestanding = Args.hasArg(OPT_ffreestanding);
  Opts.NoBuiltin = Args.hasArg(OPT_fno_builtin) || Opts.Freestanding;
  if (!Opts.NoBuiltin)
    getAllNoBuiltinFuncValues(Args, Opts.NoBuiltinFuncs);
  Opts.NoMathBuiltin = Args.hasArg(OPT_fno_math_builtin);
  Opts.RelaxedTemplateTemplateArgs =
      Args.hasArg(OPT_frelaxed_template_template_args);
  Opts.SizedDeallocation = Args.hasArg(OPT_fsized_deallocation);
  Opts.AlignedAllocation =
      Args.hasFlag(OPT_faligned_allocation, OPT_fno_aligned_allocation,
                   Opts.AlignedAllocation);
  Opts.AlignedAllocationUnavailable =
      Opts.AlignedAllocation && Args.hasArg(OPT_aligned_alloc_unavailable);
  Opts.NewAlignOverride =
      getLastArgIntValue(Args, OPT_fnew_alignment_EQ, 0, Diags);
  if (Opts.NewAlignOverride && !llvm::isPowerOf2_32(Opts.NewAlignOverride)) {
    Arg *A = Args.getLastArg(OPT_fnew_alignment_EQ);
    Diags.Report(diag::err_fe_invalid_alignment) << A->getAsString(Args)
                                                 << A->getValue();
    Opts.NewAlignOverride = 0;
  }
  Opts.ConceptsTS = Args.hasArg(OPT_fconcepts_ts);
  Opts.HeinousExtensions = Args.hasArg(OPT_fheinous_gnu_extensions);
  Opts.AccessControl = !Args.hasArg(OPT_fno_access_control);
  Opts.ElideConstructors = !Args.hasArg(OPT_fno_elide_constructors);
  Opts.MathErrno = !Opts.OpenCL && Args.hasArg(OPT_fmath_errno);
  Opts.InstantiationDepth =
      getLastArgIntValue(Args, OPT_ftemplate_depth, 1024, Diags);
  Opts.ArrowDepth =
      getLastArgIntValue(Args, OPT_foperator_arrow_depth, 256, Diags);
  Opts.ConstexprCallDepth =
      getLastArgIntValue(Args, OPT_fconstexpr_depth, 512, Diags);
  Opts.ConstexprStepLimit =
      getLastArgIntValue(Args, OPT_fconstexpr_steps, 1048576, Diags);
  Opts.BracketDepth = getLastArgIntValue(Args, OPT_fbracket_depth, 256, Diags);
  Opts.DelayedTemplateParsing = Args.hasArg(OPT_fdelayed_template_parsing);
  Opts.NumLargeByValueCopy =
      getLastArgIntValue(Args, OPT_Wlarge_by_value_copy_EQ, 0, Diags);
  Opts.MSBitfields = Args.hasArg(OPT_mms_bitfields);
  Opts.ObjCConstantStringClass =
    Args.getLastArgValue(OPT_fconstant_string_class);
  Opts.ObjCDefaultSynthProperties =
    !Args.hasArg(OPT_disable_objc_default_synthesize_properties);
  Opts.EncodeExtendedBlockSig =
    Args.hasArg(OPT_fencode_extended_block_signature);
  Opts.EmitAllDecls = Args.hasArg(OPT_femit_all_decls);
  Opts.PackStruct = getLastArgIntValue(Args, OPT_fpack_struct_EQ, 0, Diags);
  Opts.MaxTypeAlign = getLastArgIntValue(Args, OPT_fmax_type_align_EQ, 0, Diags);
  Opts.AlignDouble = Args.hasArg(OPT_malign_double);
  Opts.PICLevel = getLastArgIntValue(Args, OPT_pic_level, 0, Diags);
  Opts.PIE = Args.hasArg(OPT_pic_is_pie);
  Opts.Static = Args.hasArg(OPT_static_define);
  Opts.DumpRecordLayoutsSimple = Args.hasArg(OPT_fdump_record_layouts_simple);
  Opts.DumpRecordLayouts = Opts.DumpRecordLayoutsSimple
                        || Args.hasArg(OPT_fdump_record_layouts);
  Opts.DumpVTableLayouts = Args.hasArg(OPT_fdump_vtable_layouts);
  Opts.SpellChecking = !Args.hasArg(OPT_fno_spell_checking);
  Opts.NoBitFieldTypeAlign = Args.hasArg(OPT_fno_bitfield_type_align);
  Opts.SinglePrecisionConstants = Args.hasArg(OPT_cl_single_precision_constant);
  Opts.FastRelaxedMath = Args.hasArg(OPT_cl_fast_relaxed_math);
  Opts.HexagonQdsp6Compat = Args.hasArg(OPT_mqdsp6_compat);
  Opts.FakeAddressSpaceMap = Args.hasArg(OPT_ffake_address_space_map);
  Opts.ParseUnknownAnytype = Args.hasArg(OPT_funknown_anytype);
  Opts.DebuggerSupport = Args.hasArg(OPT_fdebugger_support);
  Opts.DebuggerCastResultToId = Args.hasArg(OPT_fdebugger_cast_result_to_id);
  Opts.DebuggerObjCLiteral = Args.hasArg(OPT_fdebugger_objc_literal);
  Opts.ApplePragmaPack = Args.hasArg(OPT_fapple_pragma_pack);
  Opts.ModuleName = Args.getLastArgValue(OPT_fmodule_name_EQ);
  Opts.CurrentModule = Opts.ModuleName;
  Opts.AppExt = Args.hasArg(OPT_fapplication_extension);
  Opts.ModuleFeatures = Args.getAllArgValues(OPT_fmodule_feature);
  llvm::sort(Opts.ModuleFeatures);
  Opts.NativeHalfType |= Args.hasArg(OPT_fnative_half_type);
  Opts.NativeHalfArgsAndReturns |= Args.hasArg(OPT_fnative_half_arguments_and_returns);
  // Enable HalfArgsAndReturns if present in Args or if NativeHalfArgsAndReturns
  // is enabled.
  Opts.HalfArgsAndReturns = Args.hasArg(OPT_fallow_half_arguments_and_returns)
                            | Opts.NativeHalfArgsAndReturns;
  Opts.GNUAsm = !Args.hasArg(OPT_fno_gnu_inline_asm);

  // __declspec is enabled by default for the PS4 by the driver, and also
  // enabled for Microsoft Extensions or Borland Extensions, here.
  //
  // FIXME: __declspec is also currently enabled for CUDA, but isn't really a
  // CUDA extension. However, it is required for supporting
  // __clang_cuda_builtin_vars.h, which uses __declspec(property). Once that has
  // been rewritten in terms of something more generic, remove the Opts.CUDA
  // term here.
  Opts.DeclSpecKeyword =
      Args.hasFlag(OPT_fdeclspec, OPT_fno_declspec,
                   (Opts.MicrosoftExt || Opts.Borland || Opts.CUDA));

  if (Arg *A = Args.getLastArg(OPT_faddress_space_map_mangling_EQ)) {
    switch (llvm::StringSwitch<unsigned>(A->getValue())
      .Case("target", LangOptions::ASMM_Target)
      .Case("no", LangOptions::ASMM_Off)
      .Case("yes", LangOptions::ASMM_On)
      .Default(255)) {
    default:
      Diags.Report(diag::err_drv_invalid_value)
        << "-faddress-space-map-mangling=" << A->getValue();
      break;
    case LangOptions::ASMM_Target:
      Opts.setAddressSpaceMapMangling(LangOptions::ASMM_Target);
      break;
    case LangOptions::ASMM_On:
      Opts.setAddressSpaceMapMangling(LangOptions::ASMM_On);
      break;
    case LangOptions::ASMM_Off:
      Opts.setAddressSpaceMapMangling(LangOptions::ASMM_Off);
      break;
    }
  }

  if (Arg *A = Args.getLastArg(OPT_fms_memptr_rep_EQ)) {
    LangOptions::PragmaMSPointersToMembersKind InheritanceModel =
        llvm::StringSwitch<LangOptions::PragmaMSPointersToMembersKind>(
            A->getValue())
            .Case("single",
                  LangOptions::PPTMK_FullGeneralitySingleInheritance)
            .Case("multiple",
                  LangOptions::PPTMK_FullGeneralityMultipleInheritance)
            .Case("virtual",
                  LangOptions::PPTMK_FullGeneralityVirtualInheritance)
            .Default(LangOptions::PPTMK_BestCase);
    if (InheritanceModel == LangOptions::PPTMK_BestCase)
      Diags.Report(diag::err_drv_invalid_value)
          << "-fms-memptr-rep=" << A->getValue();

    Opts.setMSPointerToMemberRepresentationMethod(InheritanceModel);
  }

  // Check for MS default calling conventions being specified.
  if (Arg *A = Args.getLastArg(OPT_fdefault_calling_conv_EQ)) {
    LangOptions::DefaultCallingConvention DefaultCC =
        llvm::StringSwitch<LangOptions::DefaultCallingConvention>(A->getValue())
            .Case("cdecl", LangOptions::DCC_CDecl)
            .Case("fastcall", LangOptions::DCC_FastCall)
            .Case("stdcall", LangOptions::DCC_StdCall)
            .Case("vectorcall", LangOptions::DCC_VectorCall)
            .Case("regcall", LangOptions::DCC_RegCall)
            .Default(LangOptions::DCC_None);
    if (DefaultCC == LangOptions::DCC_None)
      Diags.Report(diag::err_drv_invalid_value)
          << "-fdefault-calling-conv=" << A->getValue();

    llvm::Triple T(TargetOpts.Triple);
    llvm::Triple::ArchType Arch = T.getArch();
    bool emitError = (DefaultCC == LangOptions::DCC_FastCall ||
                      DefaultCC == LangOptions::DCC_StdCall) &&
                     Arch != llvm::Triple::x86;
    emitError |= (DefaultCC == LangOptions::DCC_VectorCall ||
                  DefaultCC == LangOptions::DCC_RegCall) &&
                 !(Arch == llvm::Triple::x86 || Arch == llvm::Triple::x86_64);
    if (emitError)
      Diags.Report(diag::err_drv_argument_not_allowed_with)
          << A->getSpelling() << T.getTriple();
    else
      Opts.setDefaultCallingConv(DefaultCC);
  }

  // -mrtd option
  if (Arg *A = Args.getLastArg(OPT_mrtd)) {
    if (Opts.getDefaultCallingConv() != LangOptions::DCC_None)
      Diags.Report(diag::err_drv_argument_not_allowed_with)
          << A->getSpelling() << "-fdefault-calling-conv";
    else {
      llvm::Triple T(TargetOpts.Triple);
      if (T.getArch() != llvm::Triple::x86)
        Diags.Report(diag::err_drv_argument_not_allowed_with)
            << A->getSpelling() << T.getTriple();
      else
        Opts.setDefaultCallingConv(LangOptions::DCC_StdCall);
    }
  }

  // Check if -fopenmp is specified.
  Opts.OpenMP = Args.hasArg(options::OPT_fopenmp) ? 1 : 0;
  // Check if -fopenmp-simd is specified.
  bool IsSimdSpecified =
      Args.hasFlag(options::OPT_fopenmp_simd, options::OPT_fno_openmp_simd,
                   /*Default=*/false);
  Opts.OpenMPSimd = !Opts.OpenMP && IsSimdSpecified;
  Opts.OpenMPUseTLS =
      Opts.OpenMP && !Args.hasArg(options::OPT_fnoopenmp_use_tls);
  Opts.OpenMPIsDevice =
      Opts.OpenMP && Args.hasArg(options::OPT_fopenmp_is_device);
  bool IsTargetSpecified =
      Opts.OpenMPIsDevice || Args.hasArg(options::OPT_fopenmp_targets_EQ);

  if (Opts.OpenMP || Opts.OpenMPSimd) {
    if (int Version = getLastArgIntValue(
            Args, OPT_fopenmp_version_EQ,
            (IsSimdSpecified || IsTargetSpecified) ? 45 : Opts.OpenMP, Diags))
      Opts.OpenMP = Version;
    else if (IsSimdSpecified || IsTargetSpecified)
      Opts.OpenMP = 45;
    // Provide diagnostic when a given target is not expected to be an OpenMP
    // device or host.
    if (!Opts.OpenMPIsDevice) {
      switch (T.getArch()) {
      default:
        break;
      // Add unsupported host targets here:
      case llvm::Triple::nvptx:
      case llvm::Triple::nvptx64:
        Diags.Report(diag::err_drv_omp_host_target_not_supported)
            << TargetOpts.Triple;
        break;
      }
    }
  }

  // Set the flag to prevent the implementation from emitting device exception
  // handling code for those requiring so.
  Opts.OpenMPHostCXXExceptions = Opts.Exceptions && Opts.CXXExceptions;
  if ((Opts.OpenMPIsDevice && T.isNVPTX()) || Opts.OpenCLCPlusPlus) {
    Opts.Exceptions = 0;
    Opts.CXXExceptions = 0;
  }
  if (Opts.OpenMPIsDevice && T.isNVPTX()) {
    Opts.OpenMPCUDANumSMs =
        getLastArgIntValue(Args, options::OPT_fopenmp_cuda_number_of_sm_EQ,
                           Opts.OpenMPCUDANumSMs, Diags);
    Opts.OpenMPCUDABlocksPerSM =
        getLastArgIntValue(Args, options::OPT_fopenmp_cuda_blocks_per_sm_EQ,
                           Opts.OpenMPCUDABlocksPerSM, Diags);
  }

  // Prevent auto-widening the representation of loop counters during an
  // OpenMP collapse clause.
  Opts.OpenMPOptimisticCollapse =
      Args.hasArg(options::OPT_fopenmp_optimistic_collapse) ? 1 : 0;

  // Get the OpenMP target triples if any.
  if (Arg *A = Args.getLastArg(options::OPT_fopenmp_targets_EQ)) {

    for (unsigned i = 0; i < A->getNumValues(); ++i) {
      llvm::Triple TT(A->getValue(i));

      if (TT.getArch() == llvm::Triple::UnknownArch ||
          !(TT.getArch() == llvm::Triple::ppc ||
            TT.getArch() == llvm::Triple::ppc64 ||
            TT.getArch() == llvm::Triple::ppc64le ||
            TT.getArch() == llvm::Triple::nvptx ||
            TT.getArch() == llvm::Triple::nvptx64 ||
            TT.getArch() == llvm::Triple::x86 ||
            TT.getArch() == llvm::Triple::x86_64))
        Diags.Report(diag::err_drv_invalid_omp_target) << A->getValue(i);
      else
        Opts.OMPTargetTriples.push_back(TT);
    }
  }

  // Get OpenMP host file path if any and report if a non existent file is
  // found
  if (Arg *A = Args.getLastArg(options::OPT_fopenmp_host_ir_file_path)) {
    Opts.OMPHostIRFile = A->getValue();
    if (!llvm::sys::fs::exists(Opts.OMPHostIRFile))
      Diags.Report(diag::err_drv_omp_host_ir_file_not_found)
          << Opts.OMPHostIRFile;
  }

  // Set CUDA mode for OpenMP target NVPTX if specified in options
  Opts.OpenMPCUDAMode = Opts.OpenMPIsDevice && T.isNVPTX() &&
                        Args.hasArg(options::OPT_fopenmp_cuda_mode);

  // Set CUDA mode for OpenMP target NVPTX if specified in options
  Opts.OpenMPCUDAForceFullRuntime =
      Opts.OpenMPIsDevice && T.isNVPTX() &&
      Args.hasArg(options::OPT_fopenmp_cuda_force_full_runtime);

  // Record whether the __DEPRECATED define was requested.
  Opts.Deprecated = Args.hasFlag(OPT_fdeprecated_macro,
                                 OPT_fno_deprecated_macro,
                                 Opts.Deprecated);

  // FIXME: Eliminate this dependency.
  unsigned Opt = getOptimizationLevel(Args, IK, Diags),
       OptSize = getOptimizationLevelSize(Args);
  Opts.Optimize = Opt != 0;
  Opts.OptimizeSize = OptSize != 0;

  // This is the __NO_INLINE__ define, which just depends on things like the
  // optimization level and -fno-inline, not actually whether the backend has
  // inlining enabled.
  Opts.NoInlineDefine = !Opts.Optimize;
  if (Arg *InlineArg = Args.getLastArg(
          options::OPT_finline_functions, options::OPT_finline_hint_functions,
          options::OPT_fno_inline_functions, options::OPT_fno_inline))
    if (InlineArg->getOption().matches(options::OPT_fno_inline))
      Opts.NoInlineDefine = true;

  Opts.FastMath = Args.hasArg(OPT_ffast_math) ||
      Args.hasArg(OPT_cl_fast_relaxed_math);
  Opts.FiniteMathOnly = Args.hasArg(OPT_ffinite_math_only) ||
      Args.hasArg(OPT_cl_finite_math_only) ||
      Args.hasArg(OPT_cl_fast_relaxed_math);
  Opts.UnsafeFPMath = Args.hasArg(OPT_menable_unsafe_fp_math) ||
                      Args.hasArg(OPT_cl_unsafe_math_optimizations) ||
                      Args.hasArg(OPT_cl_fast_relaxed_math);

  if (Arg *A = Args.getLastArg(OPT_ffp_contract)) {
    StringRef Val = A->getValue();
    if (Val == "fast")
      Opts.setDefaultFPContractMode(LangOptions::FPC_Fast);
    else if (Val == "on")
      Opts.setDefaultFPContractMode(LangOptions::FPC_On);
    else if (Val == "off")
      Opts.setDefaultFPContractMode(LangOptions::FPC_Off);
    else
      Diags.Report(diag::err_drv_invalid_value) << A->getAsString(Args) << Val;
  }

  Opts.RetainCommentsFromSystemHeaders =
      Args.hasArg(OPT_fretain_comments_from_system_headers);

  unsigned SSP = getLastArgIntValue(Args, OPT_stack_protector, 0, Diags);
  switch (SSP) {
  default:
    Diags.Report(diag::err_drv_invalid_value)
      << Args.getLastArg(OPT_stack_protector)->getAsString(Args) << SSP;
    break;
  case 0: Opts.setStackProtector(LangOptions::SSPOff); break;
  case 1: Opts.setStackProtector(LangOptions::SSPOn);  break;
  case 2: Opts.setStackProtector(LangOptions::SSPStrong); break;
  case 3: Opts.setStackProtector(LangOptions::SSPReq); break;
  }

  if (Arg *A = Args.getLastArg(OPT_ftrivial_auto_var_init)) {
    StringRef Val = A->getValue();
    if (Val == "uninitialized")
      Opts.setTrivialAutoVarInit(
          LangOptions::TrivialAutoVarInitKind::Uninitialized);
    else if (Val == "zero")
      Opts.setTrivialAutoVarInit(LangOptions::TrivialAutoVarInitKind::Zero);
    else if (Val == "pattern")
      Opts.setTrivialAutoVarInit(LangOptions::TrivialAutoVarInitKind::Pattern);
    else
      Diags.Report(diag::err_drv_invalid_value) << A->getAsString(Args) << Val;
  }

  // Parse -fsanitize= arguments.
  parseSanitizerKinds("-fsanitize=", Args.getAllArgValues(OPT_fsanitize_EQ),
                      Diags, Opts.Sanitize);
  // -fsanitize-address-field-padding=N has to be a LangOpt, parse it here.
  Opts.SanitizeAddressFieldPadding =
      getLastArgIntValue(Args, OPT_fsanitize_address_field_padding, 0, Diags);
  Opts.SanitizerBlacklistFiles = Args.getAllArgValues(OPT_fsanitize_blacklist);

  // -fxray-instrument
  Opts.XRayInstrument =
      Args.hasFlag(OPT_fxray_instrument, OPT_fnoxray_instrument, false);

  // -fxray-always-emit-customevents
  Opts.XRayAlwaysEmitCustomEvents =
      Args.hasFlag(OPT_fxray_always_emit_customevents,
                   OPT_fnoxray_always_emit_customevents, false);

  // -fxray-always-emit-typedevents
  Opts.XRayAlwaysEmitTypedEvents =
      Args.hasFlag(OPT_fxray_always_emit_typedevents,
                   OPT_fnoxray_always_emit_customevents, false);

  // -fxray-{always,never}-instrument= filenames.
  Opts.XRayAlwaysInstrumentFiles =
      Args.getAllArgValues(OPT_fxray_always_instrument);
  Opts.XRayNeverInstrumentFiles =
      Args.getAllArgValues(OPT_fxray_never_instrument);
  Opts.XRayAttrListFiles = Args.getAllArgValues(OPT_fxray_attr_list);

  // -fforce-emit-vtables
  Opts.ForceEmitVTables = Args.hasArg(OPT_fforce_emit_vtables);

  // -fallow-editor-placeholders
  Opts.AllowEditorPlaceholders = Args.hasArg(OPT_fallow_editor_placeholders);

  Opts.RegisterStaticDestructors = !Args.hasArg(OPT_fno_cxx_static_destructors);

  if (Arg *A = Args.getLastArg(OPT_fclang_abi_compat_EQ)) {
    Opts.setClangABICompat(LangOptions::ClangABI::Latest);

    StringRef Ver = A->getValue();
    std::pair<StringRef, StringRef> VerParts = Ver.split('.');
    unsigned Major, Minor = 0;

    // Check the version number is valid: either 3.x (0 <= x <= 9) or
    // y or y.0 (4 <= y <= current version).
    if (!VerParts.first.startswith("0") &&
        !VerParts.first.getAsInteger(10, Major) &&
        3 <= Major && Major <= CLANG_VERSION_MAJOR &&
        (Major == 3 ? VerParts.second.size() == 1 &&
                      !VerParts.second.getAsInteger(10, Minor)
                    : VerParts.first.size() == Ver.size() ||
                      VerParts.second == "0")) {
      // Got a valid version number.
      if (Major == 3 && Minor <= 8)
        Opts.setClangABICompat(LangOptions::ClangABI::Ver3_8);
      else if (Major <= 4)
        Opts.setClangABICompat(LangOptions::ClangABI::Ver4);
      else if (Major <= 6)
        Opts.setClangABICompat(LangOptions::ClangABI::Ver6);
      else if (Major <= 7)
        Opts.setClangABICompat(LangOptions::ClangABI::Ver7);
    } else if (Ver != "latest") {
      Diags.Report(diag::err_drv_invalid_value)
          << A->getAsString(Args) << A->getValue();
    }
  }

  Opts.CompleteMemberPointers = Args.hasArg(OPT_fcomplete_member_pointers);
  Opts.BuildingPCHWithObjectFile = Args.hasArg(OPT_building_pch_with_obj);
}

static bool isStrictlyPreprocessorAction(frontend::ActionKind Action) {
  switch (Action) {
  case frontend::ASTDeclList:
  case frontend::ASTDump:
  case frontend::ASTPrint:
  case frontend::ASTView:
  case frontend::EmitAssembly:
  case frontend::EmitBC:
  case frontend::EmitHTML:
  case frontend::EmitLLVM:
  case frontend::EmitLLVMOnly:
  case frontend::EmitCodeGenOnly:
  case frontend::EmitObj:
  case frontend::FixIt:
  case frontend::GenerateModule:
  case frontend::GenerateModuleInterface:
  case frontend::GenerateHeaderModule:
  case frontend::GeneratePCH:
  case frontend::ParseSyntaxOnly:
  case frontend::ModuleFileInfo:
  case frontend::VerifyPCH:
  case frontend::PluginAction:
  case frontend::RewriteObjC:
  case frontend::RewriteTest:
  case frontend::RunAnalysis:
  case frontend::TemplightDump:
  case frontend::MigrateSource:
    return false;

  case frontend::DumpCompilerOptions:
  case frontend::DumpRawTokens:
  case frontend::DumpTokens:
  case frontend::InitOnly:
  case frontend::PrintPreamble:
  case frontend::PrintPreprocessedInput:
  case frontend::RewriteMacros:
  case frontend::RunPreprocessorOnly:
    return true;
  }
  llvm_unreachable("invalid frontend action");
}

static void ParsePreprocessorArgs(PreprocessorOptions &Opts, ArgList &Args,
                                  DiagnosticsEngine &Diags,
                                  frontend::ActionKind Action) {
  Opts.ImplicitPCHInclude = Args.getLastArgValue(OPT_include_pch);
  Opts.PCHWithHdrStop = Args.hasArg(OPT_pch_through_hdrstop_create) ||
                        Args.hasArg(OPT_pch_through_hdrstop_use);
  Opts.PCHWithHdrStopCreate = Args.hasArg(OPT_pch_through_hdrstop_create);
  Opts.PCHThroughHeader = Args.getLastArgValue(OPT_pch_through_header_EQ);
  Opts.UsePredefines = !Args.hasArg(OPT_undef);
  Opts.DetailedRecord = Args.hasArg(OPT_detailed_preprocessing_record);
  Opts.DisablePCHValidation = Args.hasArg(OPT_fno_validate_pch);
  Opts.AllowPCHWithCompilerErrors = Args.hasArg(OPT_fallow_pch_with_errors);

  Opts.DumpDeserializedPCHDecls = Args.hasArg(OPT_dump_deserialized_pch_decls);
  for (const auto *A : Args.filtered(OPT_error_on_deserialized_pch_decl))
    Opts.DeserializedPCHDeclsToErrorOn.insert(A->getValue());

  if (const Arg *A = Args.getLastArg(OPT_preamble_bytes_EQ)) {
    StringRef Value(A->getValue());
    size_t Comma = Value.find(',');
    unsigned Bytes = 0;
    unsigned EndOfLine = 0;

    if (Comma == StringRef::npos ||
        Value.substr(0, Comma).getAsInteger(10, Bytes) ||
        Value.substr(Comma + 1).getAsInteger(10, EndOfLine))
      Diags.Report(diag::err_drv_preamble_format);
    else {
      Opts.PrecompiledPreambleBytes.first = Bytes;
      Opts.PrecompiledPreambleBytes.second = (EndOfLine != 0);
    }
  }

  // Add the __CET__ macro if a CFProtection option is set.
  if (const Arg *A = Args.getLastArg(OPT_fcf_protection_EQ)) {
    StringRef Name = A->getValue();
    if (Name == "branch")
      Opts.addMacroDef("__CET__=1");
    else if (Name == "return")
      Opts.addMacroDef("__CET__=2");
    else if (Name == "full")
      Opts.addMacroDef("__CET__=3");
  }

  // Add macros from the command line.
  for (const auto *A : Args.filtered(OPT_D, OPT_U)) {
    if (A->getOption().matches(OPT_D))
      Opts.addMacroDef(A->getValue());
    else
      Opts.addMacroUndef(A->getValue());
  }

  Opts.MacroIncludes = Args.getAllArgValues(OPT_imacros);

  // Add the ordered list of -includes.
  for (const auto *A : Args.filtered(OPT_include))
    Opts.Includes.emplace_back(A->getValue());

  for (const auto *A : Args.filtered(OPT_chain_include))
    Opts.ChainedIncludes.emplace_back(A->getValue());

  for (const auto *A : Args.filtered(OPT_remap_file)) {
    std::pair<StringRef, StringRef> Split = StringRef(A->getValue()).split(';');

    if (Split.second.empty()) {
      Diags.Report(diag::err_drv_invalid_remap_file) << A->getAsString(Args);
      continue;
    }

    Opts.addRemappedFile(Split.first, Split.second);
  }

  if (Arg *A = Args.getLastArg(OPT_fobjc_arc_cxxlib_EQ)) {
    StringRef Name = A->getValue();
    unsigned Library = llvm::StringSwitch<unsigned>(Name)
      .Case("libc++", ARCXX_libcxx)
      .Case("libstdc++", ARCXX_libstdcxx)
      .Case("none", ARCXX_nolib)
      .Default(~0U);
    if (Library == ~0U)
      Diags.Report(diag::err_drv_invalid_value) << A->getAsString(Args) << Name;
    else
      Opts.ObjCXXARCStandardLibrary = (ObjCXXARCStandardLibraryKind)Library;
  }

  // Always avoid lexing editor placeholders when we're just running the
  // preprocessor as we never want to emit the
  // "editor placeholder in source file" error in PP only mode.
  if (isStrictlyPreprocessorAction(Action))
    Opts.LexEditorPlaceholders = false;
}

static void ParsePreprocessorOutputArgs(PreprocessorOutputOptions &Opts,
                                        ArgList &Args,
                                        frontend::ActionKind Action) {
  if (isStrictlyPreprocessorAction(Action))
    Opts.ShowCPP = !Args.hasArg(OPT_dM);
  else
    Opts.ShowCPP = 0;

  Opts.ShowComments = Args.hasArg(OPT_C);
  Opts.ShowLineMarkers = !Args.hasArg(OPT_P);
  Opts.ShowMacroComments = Args.hasArg(OPT_CC);
  Opts.ShowMacros = Args.hasArg(OPT_dM) || Args.hasArg(OPT_dD);
  Opts.ShowIncludeDirectives = Args.hasArg(OPT_dI);
  Opts.RewriteIncludes = Args.hasArg(OPT_frewrite_includes);
  Opts.RewriteImports = Args.hasArg(OPT_frewrite_imports);
  Opts.UseLineDirectives = Args.hasArg(OPT_fuse_line_directives);
}

static void ParseTargetArgs(TargetOptions &Opts, ArgList &Args,
                            DiagnosticsEngine &Diags) {
  Opts.CodeModel = getCodeModel(Args, Diags);
  Opts.ABI = Args.getLastArgValue(OPT_target_abi);
  if (Arg *A = Args.getLastArg(OPT_meabi)) {
    StringRef Value = A->getValue();
    llvm::EABI EABIVersion = llvm::StringSwitch<llvm::EABI>(Value)
                                 .Case("default", llvm::EABI::Default)
                                 .Case("4", llvm::EABI::EABI4)
                                 .Case("5", llvm::EABI::EABI5)
                                 .Case("gnu", llvm::EABI::GNU)
                                 .Default(llvm::EABI::Unknown);
    if (EABIVersion == llvm::EABI::Unknown)
      Diags.Report(diag::err_drv_invalid_value) << A->getAsString(Args)
                                                << Value;
    else
      Opts.EABIVersion = EABIVersion;
  }
  Opts.CPU = Args.getLastArgValue(OPT_target_cpu);
  Opts.FPMath = Args.getLastArgValue(OPT_mfpmath);
  Opts.FeaturesAsWritten = Args.getAllArgValues(OPT_target_feature);
  Opts.LinkerVersion = Args.getLastArgValue(OPT_target_linker_version);
  Opts.Triple = Args.getLastArgValue(OPT_triple);
  // Use the default target triple if unspecified.
  if (Opts.Triple.empty())
    Opts.Triple = llvm::sys::getDefaultTargetTriple();
  Opts.Triple = llvm::Triple::normalize(Opts.Triple);
  Opts.OpenCLExtensionsAsWritten = Args.getAllArgValues(OPT_cl_ext_EQ);
  Opts.ForceEnableInt128 = Args.hasArg(OPT_fforce_enable_int128);
  Opts.NVPTXUseShortPointers = Args.hasFlag(
      options::OPT_fcuda_short_ptr, options::OPT_fno_cuda_short_ptr, false);
  if (Arg *A = Args.getLastArg(options::OPT_target_sdk_version_EQ)) {
    llvm::VersionTuple Version;
    if (Version.tryParse(A->getValue()))
      Diags.Report(diag::err_drv_invalid_value)
          << A->getAsString(Args) << A->getValue();
    else
      Opts.SDKVersion = Version;
  }
}

bool CompilerInvocation::CreateFromArgs(CompilerInvocation &Res,
                                        const char *const *ArgBegin,
                                        const char *const *ArgEnd,
                                        DiagnosticsEngine &Diags) {
  bool Success = true;

  // Parse the arguments.
  std::unique_ptr<OptTable> Opts = createDriverOptTable();
  const unsigned IncludedFlagsBitmask = options::CC1Option;
  unsigned MissingArgIndex, MissingArgCount;
  InputArgList Args =
      Opts->ParseArgs(llvm::makeArrayRef(ArgBegin, ArgEnd), MissingArgIndex,
                      MissingArgCount, IncludedFlagsBitmask);
  LangOptions &LangOpts = *Res.getLangOpts();

  // Check for missing argument error.
  if (MissingArgCount) {
    Diags.Report(diag::err_drv_missing_argument)
        << Args.getArgString(MissingArgIndex) << MissingArgCount;
    Success = false;
  }

  // Issue errors on unknown arguments.
  for (const auto *A : Args.filtered(OPT_UNKNOWN)) {
    auto ArgString = A->getAsString(Args);
    std::string Nearest;
    if (Opts->findNearest(ArgString, Nearest, IncludedFlagsBitmask) > 1)
      Diags.Report(diag::err_drv_unknown_argument) << ArgString;
    else
      Diags.Report(diag::err_drv_unknown_argument_with_suggestion)
          << ArgString << Nearest;
    Success = false;
  }

  Success &= ParseAnalyzerArgs(*Res.getAnalyzerOpts(), Args, Diags);
  Success &= ParseMigratorArgs(Res.getMigratorOpts(), Args);
  ParseDependencyOutputArgs(Res.getDependencyOutputOpts(), Args);
  Success &=
      ParseDiagnosticArgs(Res.getDiagnosticOpts(), Args, &Diags,
                          false /*DefaultDiagColor*/, false /*DefaultShowOpt*/);
  ParseCommentArgs(LangOpts.CommentOpts, Args);
  ParseFileSystemArgs(Res.getFileSystemOpts(), Args);
  // FIXME: We shouldn't have to pass the DashX option around here
  InputKind DashX = ParseFrontendArgs(Res.getFrontendOpts(), Args, Diags,
                                      LangOpts.IsHeaderFile);
  ParseTargetArgs(Res.getTargetOpts(), Args, Diags);
  Success &= ParseCodeGenArgs(Res.getCodeGenOpts(), Args, DashX, Diags,
                              Res.getTargetOpts(), Res.getFrontendOpts());
  ParseHeaderSearchArgs(Res.getHeaderSearchOpts(), Args,
                        Res.getFileSystemOpts().WorkingDir);
  llvm::Triple T(Res.getTargetOpts().Triple);
  if (DashX.getFormat() == InputKind::Precompiled ||
      DashX.getLanguage() == InputKind::LLVM_IR) {
    // ObjCAAutoRefCount and Sanitize LangOpts are used to setup the
    // PassManager in BackendUtil.cpp. They need to be initializd no matter
    // what the input type is.
    if (Args.hasArg(OPT_fobjc_arc))
      LangOpts.ObjCAutoRefCount = 1;
    // PIClevel and PIELevel are needed during code generation and this should be
    // set regardless of the input type.
    LangOpts.PICLevel = getLastArgIntValue(Args, OPT_pic_level, 0, Diags);
    LangOpts.PIE = Args.hasArg(OPT_pic_is_pie);
    parseSanitizerKinds("-fsanitize=", Args.getAllArgValues(OPT_fsanitize_EQ),
                        Diags, LangOpts.Sanitize);
  } else {
    // Other LangOpts are only initialized when the input is not AST or LLVM IR.
    // FIXME: Should we really be calling this for an InputKind::Asm input?
    ParseLangArgs(LangOpts, Args, DashX, Res.getTargetOpts(),
                  Res.getPreprocessorOpts(), Diags);
    if (Res.getFrontendOpts().ProgramAction == frontend::RewriteObjC)
      LangOpts.ObjCExceptions = 1;
    if (T.isOSDarwin() && DashX.isPreprocessed()) {
      // Supress the darwin-specific 'stdlibcxx-not-found' diagnostic for
      // preprocessed input as we don't expect it to be used with -std=libc++
      // anyway.
      Res.getDiagnosticOpts().Warnings.push_back("no-stdlibcxx-not-found");
    }
  }

  LangOpts.FunctionAlignment =
      getLastArgIntValue(Args, OPT_function_alignment, 0, Diags);

  if (LangOpts.CUDA) {
    // During CUDA device-side compilation, the aux triple is the
    // triple used for host compilation.
    if (LangOpts.CUDAIsDevice)
      Res.getTargetOpts().HostTriple = Res.getFrontendOpts().AuxTriple;
  }

  // Set the triple of the host for OpenMP device compile.
  if (LangOpts.OpenMPIsDevice)
    Res.getTargetOpts().HostTriple = Res.getFrontendOpts().AuxTriple;

  // FIXME: Override value name discarding when asan or msan is used because the
  // backend passes depend on the name of the alloca in order to print out
  // names.
  Res.getCodeGenOpts().DiscardValueNames &=
      !LangOpts.Sanitize.has(SanitizerKind::Address) &&
      !LangOpts.Sanitize.has(SanitizerKind::KernelAddress) &&
      !LangOpts.Sanitize.has(SanitizerKind::Memory) &&
      !LangOpts.Sanitize.has(SanitizerKind::KernelMemory);

  ParsePreprocessorArgs(Res.getPreprocessorOpts(), Args, Diags,
                        Res.getFrontendOpts().ProgramAction);
  ParsePreprocessorOutputArgs(Res.getPreprocessorOutputOpts(), Args,
                              Res.getFrontendOpts().ProgramAction);

  // Turn on -Wspir-compat for SPIR target.
  auto Arch = T.getArch();
  if (Arch == llvm::Triple::spir || Arch == llvm::Triple::spir64) {
    Res.getDiagnosticOpts().Warnings.push_back("spir-compat");
  }

  // If sanitizer is enabled, disable OPT_ffine_grained_bitfield_accesses.
  if (Res.getCodeGenOpts().FineGrainedBitfieldAccesses &&
      !Res.getLangOpts()->Sanitize.empty()) {
    Res.getCodeGenOpts().FineGrainedBitfieldAccesses = false;
    Diags.Report(diag::warn_drv_fine_grained_bitfield_accesses_ignored);
  }
  return Success;
}

std::string CompilerInvocation::getModuleHash() const {
  // Note: For QoI reasons, the things we use as a hash here should all be
  // dumped via the -module-info flag.
  using llvm::hash_code;
  using llvm::hash_value;
  using llvm::hash_combine;

  // Start the signature with the compiler version.
  // FIXME: We'd rather use something more cryptographically sound than
  // CityHash, but this will do for now.
  hash_code code = hash_value(getClangFullRepositoryVersion());

  // Extend the signature with the language options
#define LANGOPT(Name, Bits, Default, Description) \
   code = hash_combine(code, LangOpts->Name);
#define ENUM_LANGOPT(Name, Type, Bits, Default, Description) \
  code = hash_combine(code, static_cast<unsigned>(LangOpts->get##Name()));
#define BENIGN_LANGOPT(Name, Bits, Default, Description)
#define BENIGN_ENUM_LANGOPT(Name, Type, Bits, Default, Description)
#include "clang/Basic/LangOptions.def"

  for (StringRef Feature : LangOpts->ModuleFeatures)
    code = hash_combine(code, Feature);

  // Extend the signature with the target options.
  code = hash_combine(code, TargetOpts->Triple, TargetOpts->CPU,
                      TargetOpts->ABI);
  for (const auto &FeatureAsWritten : TargetOpts->FeaturesAsWritten)
    code = hash_combine(code, FeatureAsWritten);

  // Extend the signature with preprocessor options.
  const PreprocessorOptions &ppOpts = getPreprocessorOpts();
  const HeaderSearchOptions &hsOpts = getHeaderSearchOpts();
  code = hash_combine(code, ppOpts.UsePredefines, ppOpts.DetailedRecord);

  for (const auto &I : getPreprocessorOpts().Macros) {
    // If we're supposed to ignore this macro for the purposes of modules,
    // don't put it into the hash.
    if (!hsOpts.ModulesIgnoreMacros.empty()) {
      // Check whether we're ignoring this macro.
      StringRef MacroDef = I.first;
      if (hsOpts.ModulesIgnoreMacros.count(
              llvm::CachedHashString(MacroDef.split('=').first)))
        continue;
    }

    code = hash_combine(code, I.first, I.second);
  }

  // Extend the signature with the sysroot and other header search options.
  code = hash_combine(code, hsOpts.Sysroot,
                      hsOpts.ModuleFormat,
                      hsOpts.UseDebugInfo,
                      hsOpts.UseBuiltinIncludes,
                      hsOpts.UseStandardSystemIncludes,
                      hsOpts.UseStandardCXXIncludes,
                      hsOpts.UseLibcxx,
                      hsOpts.ModulesValidateDiagnosticOptions);
  code = hash_combine(code, hsOpts.ResourceDir);

  // Extend the signature with the user build path.
  code = hash_combine(code, hsOpts.ModuleUserBuildPath);

  // Extend the signature with the module file extensions.
  const FrontendOptions &frontendOpts = getFrontendOpts();
  for (const auto &ext : frontendOpts.ModuleFileExtensions) {
    code = ext->hashExtension(code);
  }

  // When compiling with -gmodules, also hash -fdebug-prefix-map as it
  // affects the debug info in the PCM.
  if (getCodeGenOpts().DebugTypeExtRefs)
    for (const auto &KeyValue : getCodeGenOpts().DebugPrefixMap)
      code = hash_combine(code, KeyValue.first, KeyValue.second);

  // Extend the signature with the enabled sanitizers, if at least one is
  // enabled. Sanitizers which cannot affect AST generation aren't hashed.
  SanitizerSet SanHash = LangOpts->Sanitize;
  SanHash.clear(getPPTransparentSanitizers());
  if (!SanHash.empty())
    code = hash_combine(code, SanHash.Mask);

  return llvm::APInt(64, code).toString(36, /*Signed=*/false);
}

template<typename IntTy>
static IntTy getLastArgIntValueImpl(const ArgList &Args, OptSpecifier Id,
                                    IntTy Default,
                                    DiagnosticsEngine *Diags) {
  IntTy Res = Default;
  if (Arg *A = Args.getLastArg(Id)) {
    if (StringRef(A->getValue()).getAsInteger(10, Res)) {
      if (Diags)
        Diags->Report(diag::err_drv_invalid_int_value) << A->getAsString(Args)
                                                       << A->getValue();
    }
  }
  return Res;
}

namespace clang {

// Declared in clang/Frontend/Utils.h.
int getLastArgIntValue(const ArgList &Args, OptSpecifier Id, int Default,
                       DiagnosticsEngine *Diags) {
  return getLastArgIntValueImpl<int>(Args, Id, Default, Diags);
}

uint64_t getLastArgUInt64Value(const ArgList &Args, OptSpecifier Id,
                               uint64_t Default,
                               DiagnosticsEngine *Diags) {
  return getLastArgIntValueImpl<uint64_t>(Args, Id, Default, Diags);
}

IntrusiveRefCntPtr<llvm::vfs::FileSystem>
createVFSFromCompilerInvocation(const CompilerInvocation &CI,
                                DiagnosticsEngine &Diags) {
  return createVFSFromCompilerInvocation(CI, Diags,
                                         llvm::vfs::getRealFileSystem());
}

IntrusiveRefCntPtr<llvm::vfs::FileSystem> createVFSFromCompilerInvocation(
    const CompilerInvocation &CI, DiagnosticsEngine &Diags,
    IntrusiveRefCntPtr<llvm::vfs::FileSystem> BaseFS) {
  if (CI.getHeaderSearchOpts().VFSOverlayFiles.empty())
    return BaseFS;

  IntrusiveRefCntPtr<llvm::vfs::FileSystem> Result = BaseFS;
  // earlier vfs files are on the bottom
  for (const auto &File : CI.getHeaderSearchOpts().VFSOverlayFiles) {
    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> Buffer =
        Result->getBufferForFile(File);
    if (!Buffer) {
      Diags.Report(diag::err_missing_vfs_overlay_file) << File;
      continue;
    }

    IntrusiveRefCntPtr<llvm::vfs::FileSystem> FS = llvm::vfs::getVFSFromYAML(
        std::move(Buffer.get()), /*DiagHandler*/ nullptr, File,
        /*DiagContext*/ nullptr, Result);
    if (!FS) {
      Diags.Report(diag::err_invalid_vfs_overlay) << File;
      continue;
    }

    Result = FS;
  }
  return Result;
}

} // namespace clang
