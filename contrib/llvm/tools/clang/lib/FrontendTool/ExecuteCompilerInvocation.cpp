//===--- ExecuteCompilerInvocation.cpp ------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file holds ExecuteCompilerInvocation(). It is split into its own file to
// minimize the impact of pulling in essentially everything else in Clang.
//
//===----------------------------------------------------------------------===//

#include "clang/ARCMigrate/ARCMTActions.h"
#include "clang/CodeGen/CodeGenAction.h"
#include "clang/Config/config.h"
#include "clang/Driver/Options.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Frontend/Utils.h"
#include "clang/FrontendTool/Utils.h"
#include "clang/Rewrite/Frontend/FrontendActions.h"
#include "clang/StaticAnalyzer/Frontend/FrontendActions.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/BuryPointer.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/ErrorHandling.h"
using namespace clang;
using namespace llvm::opt;

namespace clang {

static std::unique_ptr<FrontendAction>
CreateFrontendBaseAction(CompilerInstance &CI) {
  using namespace clang::frontend;
  StringRef Action("unknown");
  (void)Action;

  switch (CI.getFrontendOpts().ProgramAction) {
  case ASTDeclList:            return llvm::make_unique<ASTDeclListAction>();
  case ASTDump:                return llvm::make_unique<ASTDumpAction>();
  case ASTPrint:               return llvm::make_unique<ASTPrintAction>();
  case ASTView:                return llvm::make_unique<ASTViewAction>();
  case DumpCompilerOptions:
    return llvm::make_unique<DumpCompilerOptionsAction>();
  case DumpRawTokens:          return llvm::make_unique<DumpRawTokensAction>();
  case DumpTokens:             return llvm::make_unique<DumpTokensAction>();
  case EmitAssembly:           return llvm::make_unique<EmitAssemblyAction>();
  case EmitBC:                 return llvm::make_unique<EmitBCAction>();
  case EmitHTML:               return llvm::make_unique<HTMLPrintAction>();
  case EmitLLVM:               return llvm::make_unique<EmitLLVMAction>();
  case EmitLLVMOnly:           return llvm::make_unique<EmitLLVMOnlyAction>();
  case EmitCodeGenOnly:        return llvm::make_unique<EmitCodeGenOnlyAction>();
  case EmitObj:                return llvm::make_unique<EmitObjAction>();
  case FixIt:                  return llvm::make_unique<FixItAction>();
  case GenerateModule:
    return llvm::make_unique<GenerateModuleFromModuleMapAction>();
  case GenerateModuleInterface:
    return llvm::make_unique<GenerateModuleInterfaceAction>();
  case GenerateHeaderModule:
    return llvm::make_unique<GenerateHeaderModuleAction>();
  case GeneratePCH:            return llvm::make_unique<GeneratePCHAction>();
  case InitOnly:               return llvm::make_unique<InitOnlyAction>();
  case ParseSyntaxOnly:        return llvm::make_unique<SyntaxOnlyAction>();
  case ModuleFileInfo:         return llvm::make_unique<DumpModuleInfoAction>();
  case VerifyPCH:              return llvm::make_unique<VerifyPCHAction>();
  case TemplightDump:          return llvm::make_unique<TemplightDumpAction>();

  case PluginAction: {
    for (FrontendPluginRegistry::iterator it =
           FrontendPluginRegistry::begin(), ie = FrontendPluginRegistry::end();
         it != ie; ++it) {
      if (it->getName() == CI.getFrontendOpts().ActionName) {
        std::unique_ptr<PluginASTAction> P(it->instantiate());
        if ((P->getActionType() != PluginASTAction::ReplaceAction &&
             P->getActionType() != PluginASTAction::Cmdline) ||
            !P->ParseArgs(CI, CI.getFrontendOpts().PluginArgs[it->getName()]))
          return nullptr;
        return std::move(P);
      }
    }

    CI.getDiagnostics().Report(diag::err_fe_invalid_plugin_name)
      << CI.getFrontendOpts().ActionName;
    return nullptr;
  }

  case PrintPreamble:          return llvm::make_unique<PrintPreambleAction>();
  case PrintPreprocessedInput: {
    if (CI.getPreprocessorOutputOpts().RewriteIncludes ||
        CI.getPreprocessorOutputOpts().RewriteImports)
      return llvm::make_unique<RewriteIncludesAction>();
    return llvm::make_unique<PrintPreprocessedAction>();
  }

  case RewriteMacros:          return llvm::make_unique<RewriteMacrosAction>();
  case RewriteTest:            return llvm::make_unique<RewriteTestAction>();
#if CLANG_ENABLE_OBJC_REWRITER
  case RewriteObjC:            return llvm::make_unique<RewriteObjCAction>();
#else
  case RewriteObjC:            Action = "RewriteObjC"; break;
#endif
#if CLANG_ENABLE_ARCMT
  case MigrateSource:
    return llvm::make_unique<arcmt::MigrateSourceAction>();
#else
  case MigrateSource:          Action = "MigrateSource"; break;
#endif
#if CLANG_ENABLE_STATIC_ANALYZER
  case RunAnalysis:            return llvm::make_unique<ento::AnalysisAction>();
#else
  case RunAnalysis:            Action = "RunAnalysis"; break;
#endif
  case RunPreprocessorOnly:    return llvm::make_unique<PreprocessOnlyAction>();
  }

#if !CLANG_ENABLE_ARCMT || !CLANG_ENABLE_STATIC_ANALYZER \
  || !CLANG_ENABLE_OBJC_REWRITER
  CI.getDiagnostics().Report(diag::err_fe_action_not_available) << Action;
  return 0;
#else
  llvm_unreachable("Invalid program action!");
#endif
}

std::unique_ptr<FrontendAction>
CreateFrontendAction(CompilerInstance &CI) {
  // Create the underlying action.
  std::unique_ptr<FrontendAction> Act = CreateFrontendBaseAction(CI);
  if (!Act)
    return nullptr;

  const FrontendOptions &FEOpts = CI.getFrontendOpts();

  if (FEOpts.FixAndRecompile) {
    Act = llvm::make_unique<FixItRecompile>(std::move(Act));
  }

#if CLANG_ENABLE_ARCMT
  if (CI.getFrontendOpts().ProgramAction != frontend::MigrateSource &&
      CI.getFrontendOpts().ProgramAction != frontend::GeneratePCH) {
    // Potentially wrap the base FE action in an ARC Migrate Tool action.
    switch (FEOpts.ARCMTAction) {
    case FrontendOptions::ARCMT_None:
      break;
    case FrontendOptions::ARCMT_Check:
      Act = llvm::make_unique<arcmt::CheckAction>(std::move(Act));
      break;
    case FrontendOptions::ARCMT_Modify:
      Act = llvm::make_unique<arcmt::ModifyAction>(std::move(Act));
      break;
    case FrontendOptions::ARCMT_Migrate:
      Act = llvm::make_unique<arcmt::MigrateAction>(std::move(Act),
                                     FEOpts.MTMigrateDir,
                                     FEOpts.ARCMTMigrateReportOut,
                                     FEOpts.ARCMTMigrateEmitARCErrors);
      break;
    }

    if (FEOpts.ObjCMTAction != FrontendOptions::ObjCMT_None) {
      Act = llvm::make_unique<arcmt::ObjCMigrateAction>(std::move(Act),
                                                        FEOpts.MTMigrateDir,
                                                        FEOpts.ObjCMTAction);
    }
  }
#endif

  // If there are any AST files to merge, create a frontend action
  // adaptor to perform the merge.
  if (!FEOpts.ASTMergeFiles.empty())
    Act = llvm::make_unique<ASTMergeAction>(std::move(Act),
                                            FEOpts.ASTMergeFiles);

  return Act;
}

bool ExecuteCompilerInvocation(CompilerInstance *Clang) {
  // Honor -help.
  if (Clang->getFrontendOpts().ShowHelp) {
    std::unique_ptr<OptTable> Opts = driver::createDriverOptTable();
    Opts->PrintHelp(llvm::outs(), "clang -cc1 [options] file...",
                    "LLVM 'Clang' Compiler: http://clang.llvm.org",
                    /*Include=*/driver::options::CC1Option,
                    /*Exclude=*/0, /*ShowAllAliases=*/false);
    return true;
  }

  // Honor -version.
  //
  // FIXME: Use a better -version message?
  if (Clang->getFrontendOpts().ShowVersion) {
    llvm::cl::PrintVersionMessage();
    return true;
  }

  // Load any requested plugins.
  for (unsigned i = 0,
         e = Clang->getFrontendOpts().Plugins.size(); i != e; ++i) {
    const std::string &Path = Clang->getFrontendOpts().Plugins[i];
    std::string Error;
    if (llvm::sys::DynamicLibrary::LoadLibraryPermanently(Path.c_str(), &Error))
      Clang->getDiagnostics().Report(diag::err_fe_unable_to_load_plugin)
        << Path << Error;
  }

  // Check if any of the loaded plugins replaces the main AST action
  for (FrontendPluginRegistry::iterator it = FrontendPluginRegistry::begin(),
                                        ie = FrontendPluginRegistry::end();
       it != ie; ++it) {
    std::unique_ptr<PluginASTAction> P(it->instantiate());
    if (P->getActionType() == PluginASTAction::ReplaceAction) {
      Clang->getFrontendOpts().ProgramAction = clang::frontend::PluginAction;
      Clang->getFrontendOpts().ActionName = it->getName();
      break;
    }
  }

  // Honor -mllvm.
  //
  // FIXME: Remove this, one day.
  // This should happen AFTER plugins have been loaded!
  if (!Clang->getFrontendOpts().LLVMArgs.empty()) {
    unsigned NumArgs = Clang->getFrontendOpts().LLVMArgs.size();
    auto Args = llvm::make_unique<const char*[]>(NumArgs + 2);
    Args[0] = "clang (LLVM option parsing)";
    for (unsigned i = 0; i != NumArgs; ++i)
      Args[i + 1] = Clang->getFrontendOpts().LLVMArgs[i].c_str();
    Args[NumArgs + 1] = nullptr;
    llvm::cl::ParseCommandLineOptions(NumArgs + 1, Args.get());
  }

#if CLANG_ENABLE_STATIC_ANALYZER
  // Honor -analyzer-checker-help.
  // This should happen AFTER plugins have been loaded!
  if (Clang->getAnalyzerOpts()->ShowCheckerHelp) {
    ento::printCheckerHelp(llvm::outs(), Clang->getFrontendOpts().Plugins,
                           Clang->getDiagnostics());
    return true;
  }

  // Honor -analyzer-list-enabled-checkers.
  if (Clang->getAnalyzerOpts()->ShowEnabledCheckerList) {
    ento::printEnabledCheckerList(llvm::outs(),
                                  Clang->getFrontendOpts().Plugins,
                                  *Clang->getAnalyzerOpts(),
                                  Clang->getDiagnostics());
  }

  // Honor -analyzer-config-help.
  if (Clang->getAnalyzerOpts()->ShowConfigOptionsList) {
    ento::printAnalyzerConfigList(llvm::outs());
    return true;
  }
#endif

  // If there were errors in processing arguments, don't do anything else.
  if (Clang->getDiagnostics().hasErrorOccurred())
    return false;
  // Create and execute the frontend action.
  std::unique_ptr<FrontendAction> Act(CreateFrontendAction(*Clang));
  if (!Act)
    return false;
  bool Success = Clang->ExecuteAction(*Act);
  if (Clang->getFrontendOpts().DisableFree)
    llvm::BuryPointer(std::move(Act));
  return Success;
}

} // namespace clang
