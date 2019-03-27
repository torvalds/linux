//== AnalysisManager.h - Path sensitive analysis data manager ------*- C++ -*-//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the AnalysisManager class that manages the data and policy
// for path sensitive analysis.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_ANALYSISMANAGER_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_ANALYSISMANAGER_H

#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/StaticAnalyzer/Core/AnalyzerOptions.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/PathDiagnostic.h"
#include "clang/StaticAnalyzer/Core/PathDiagnosticConsumers.h"

namespace clang {

class CodeInjector;

namespace ento {
  class CheckerManager;

class AnalysisManager : public BugReporterData {
  virtual void anchor();
  AnalysisDeclContextManager AnaCtxMgr;

  ASTContext &Ctx;
  DiagnosticsEngine &Diags;
  const LangOptions &LangOpts;
  PathDiagnosticConsumers PathConsumers;

  // Configurable components creators.
  StoreManagerCreator CreateStoreMgr;
  ConstraintManagerCreator CreateConstraintMgr;

  CheckerManager *CheckerMgr;

public:
  AnalyzerOptions &options;

  AnalysisManager(ASTContext &ctx, DiagnosticsEngine &diags,
                  const PathDiagnosticConsumers &Consumers,
                  StoreManagerCreator storemgr,
                  ConstraintManagerCreator constraintmgr,
                  CheckerManager *checkerMgr, AnalyzerOptions &Options,
                  CodeInjector *injector = nullptr);

  ~AnalysisManager() override;

  void ClearContexts() {
    AnaCtxMgr.clear();
  }

  AnalysisDeclContextManager& getAnalysisDeclContextManager() {
    return AnaCtxMgr;
  }

  StoreManagerCreator getStoreManagerCreator() {
    return CreateStoreMgr;
  }

  AnalyzerOptions& getAnalyzerOptions() override {
    return options;
  }

  ConstraintManagerCreator getConstraintManagerCreator() {
    return CreateConstraintMgr;
  }

  CheckerManager *getCheckerManager() const { return CheckerMgr; }

  ASTContext &getASTContext() override {
    return Ctx;
  }

  SourceManager &getSourceManager() override {
    return getASTContext().getSourceManager();
  }

  DiagnosticsEngine &getDiagnostic() override {
    return Diags;
  }

  const LangOptions &getLangOpts() const {
    return LangOpts;
  }

  ArrayRef<PathDiagnosticConsumer*> getPathDiagnosticConsumers() override {
    return PathConsumers;
  }

  void FlushDiagnostics();

  bool shouldVisualize() const {
    return options.visualizeExplodedGraphWithGraphViz;
  }

  bool shouldInlineCall() const {
    return options.getIPAMode() != IPAK_None;
  }

  CFG *getCFG(Decl const *D) {
    return AnaCtxMgr.getContext(D)->getCFG();
  }

  template <typename T>
  T *getAnalysis(Decl const *D) {
    return AnaCtxMgr.getContext(D)->getAnalysis<T>();
  }

  ParentMap &getParentMap(Decl const *D) {
    return AnaCtxMgr.getContext(D)->getParentMap();
  }

  AnalysisDeclContext *getAnalysisDeclContext(const Decl *D) {
    return AnaCtxMgr.getContext(D);
  }

  static bool isInCodeFile(SourceLocation SL, const SourceManager &SM) {
    if (SM.isInMainFile(SL))
      return true;

    // Support the "unified sources" compilation method (eg. WebKit) that
    // involves producing non-header files that include other non-header files.
    // We should be included directly from a UnifiedSource* file
    // and we shouldn't be a header - which is a very safe defensive check.
    SourceLocation IL = SM.getIncludeLoc(SM.getFileID(SL));
    if (!IL.isValid() || !SM.isInMainFile(IL))
      return false;
    // Should rather be "file name starts with", but the current .getFilename
    // includes the full path.
    if (SM.getFilename(IL).contains("UnifiedSource")) {
      // It might be great to reuse FrontendOptions::getInputKindForExtension()
      // but for now it doesn't discriminate between code and header files.
      return llvm::StringSwitch<bool>(SM.getFilename(SL).rsplit('.').second)
          .Cases("c", "m", "mm", "C", "cc", "cp", true)
          .Cases("cpp", "CPP", "c++", "cxx", "cppm", true)
          .Default(false);
    }

    return false;
  }

  bool isInCodeFile(SourceLocation SL) {
    const SourceManager &SM = getASTContext().getSourceManager();
    return isInCodeFile(SL, SM);
  }
};

} // enAnaCtxMgrspace

} // end clang namespace

#endif
