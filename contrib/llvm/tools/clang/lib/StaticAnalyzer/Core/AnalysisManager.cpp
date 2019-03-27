//===-- AnalysisManager.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"

using namespace clang;
using namespace ento;

void AnalysisManager::anchor() { }

AnalysisManager::AnalysisManager(ASTContext &ASTCtx, DiagnosticsEngine &diags,
                                 const PathDiagnosticConsumers &PDC,
                                 StoreManagerCreator storemgr,
                                 ConstraintManagerCreator constraintmgr,
                                 CheckerManager *checkerMgr,
                                 AnalyzerOptions &Options,
                                 CodeInjector *injector)
    : AnaCtxMgr(
          ASTCtx, Options.UnoptimizedCFG,
          Options.ShouldIncludeImplicitDtorsInCFG,
          /*AddInitializers=*/true,
          Options.ShouldIncludeTemporaryDtorsInCFG,
          Options.ShouldIncludeLifetimeInCFG,
          // Adding LoopExit elements to the CFG is a requirement for loop
          // unrolling.
          Options.ShouldIncludeLoopExitInCFG ||
            Options.ShouldUnrollLoops,
          Options.ShouldIncludeScopesInCFG,
          Options.ShouldSynthesizeBodies,
          Options.ShouldConditionalizeStaticInitializers,
          /*addCXXNewAllocator=*/true,
          Options.ShouldIncludeRichConstructorsInCFG,
          Options.ShouldElideConstructors, injector),
      Ctx(ASTCtx), Diags(diags), LangOpts(ASTCtx.getLangOpts()),
      PathConsumers(PDC), CreateStoreMgr(storemgr),
      CreateConstraintMgr(constraintmgr), CheckerMgr(checkerMgr),
      options(Options) {
  AnaCtxMgr.getCFGBuildOptions().setAllAlwaysAdd();
}

AnalysisManager::~AnalysisManager() {
  FlushDiagnostics();
  for (PathDiagnosticConsumers::iterator I = PathConsumers.begin(),
       E = PathConsumers.end(); I != E; ++I) {
    delete *I;
  }
}

void AnalysisManager::FlushDiagnostics() {
  PathDiagnosticConsumer::FilesMade filesMade;
  for (PathDiagnosticConsumers::iterator I = PathConsumers.begin(),
       E = PathConsumers.end();
       I != E; ++I) {
    (*I)->FlushDiagnostics(&filesMade);
  }
}
