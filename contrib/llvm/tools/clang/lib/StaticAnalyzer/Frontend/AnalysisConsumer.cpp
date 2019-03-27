//===--- AnalysisConsumer.cpp - ASTConsumer for running Analyses ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// "Meta" ASTConsumer for running different source analyses.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Frontend/AnalysisConsumer.h"
#include "ModelInjector.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Analysis/Analyses/LiveVariables.h"
#include "clang/Analysis/CFG.h"
#include "clang/Analysis/CallGraph.h"
#include "clang/Analysis/CodeInjector.h"
#include "clang/Basic/SourceManager.h"
#include "clang/CrossTU/CrossTranslationUnit.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/StaticAnalyzer/Checkers/LocalCheckers.h"
#include "clang/StaticAnalyzer/Core/AnalyzerOptions.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/BugReporter/PathDiagnostic.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathDiagnosticConsumers.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExprEngine.h"
#include "clang/StaticAnalyzer/Frontend/CheckerRegistration.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <queue>
#include <utility>

using namespace clang;
using namespace ento;

#define DEBUG_TYPE "AnalysisConsumer"

STATISTIC(NumFunctionTopLevel, "The # of functions at top level.");
STATISTIC(NumFunctionsAnalyzed,
                      "The # of functions and blocks analyzed (as top level "
                      "with inlining turned on).");
STATISTIC(NumBlocksInAnalyzedFunctions,
                      "The # of basic blocks in the analyzed functions.");
STATISTIC(NumVisitedBlocksInAnalyzedFunctions,
          "The # of visited basic blocks in the analyzed functions.");
STATISTIC(PercentReachableBlocks, "The % of reachable basic blocks.");
STATISTIC(MaxCFGSize, "The maximum number of basic blocks in a function.");

//===----------------------------------------------------------------------===//
// Special PathDiagnosticConsumers.
//===----------------------------------------------------------------------===//

void ento::createPlistHTMLDiagnosticConsumer(AnalyzerOptions &AnalyzerOpts,
                                             PathDiagnosticConsumers &C,
                                             const std::string &prefix,
                                             const Preprocessor &PP) {
  createHTMLDiagnosticConsumer(AnalyzerOpts, C,
                               llvm::sys::path::parent_path(prefix), PP);
  createPlistMultiFileDiagnosticConsumer(AnalyzerOpts, C, prefix, PP);
}

void ento::createTextPathDiagnosticConsumer(AnalyzerOptions &AnalyzerOpts,
                                            PathDiagnosticConsumers &C,
                                            const std::string &Prefix,
                                            const clang::Preprocessor &PP) {
  llvm_unreachable("'text' consumer should be enabled on ClangDiags");
}

namespace {
class ClangDiagPathDiagConsumer : public PathDiagnosticConsumer {
  DiagnosticsEngine &Diag;
  bool IncludePath;
public:
  ClangDiagPathDiagConsumer(DiagnosticsEngine &Diag)
    : Diag(Diag), IncludePath(false) {}
  ~ClangDiagPathDiagConsumer() override {}
  StringRef getName() const override { return "ClangDiags"; }

  bool supportsLogicalOpControlFlow() const override { return true; }
  bool supportsCrossFileDiagnostics() const override { return true; }

  PathGenerationScheme getGenerationScheme() const override {
    return IncludePath ? Minimal : None;
  }

  void enablePaths() {
    IncludePath = true;
  }

  void FlushDiagnosticsImpl(std::vector<const PathDiagnostic *> &Diags,
                            FilesMade *filesMade) override {
    unsigned WarnID = Diag.getCustomDiagID(DiagnosticsEngine::Warning, "%0");
    unsigned NoteID = Diag.getCustomDiagID(DiagnosticsEngine::Note, "%0");

    for (std::vector<const PathDiagnostic*>::iterator I = Diags.begin(),
         E = Diags.end(); I != E; ++I) {
      const PathDiagnostic *PD = *I;
      SourceLocation WarnLoc = PD->getLocation().asLocation();
      Diag.Report(WarnLoc, WarnID) << PD->getShortDescription()
                                   << PD->path.back()->getRanges();

      // First, add extra notes, even if paths should not be included.
      for (const auto &Piece : PD->path) {
        if (!isa<PathDiagnosticNotePiece>(Piece.get()))
          continue;

        SourceLocation NoteLoc = Piece->getLocation().asLocation();
        Diag.Report(NoteLoc, NoteID) << Piece->getString()
                                     << Piece->getRanges();
      }

      if (!IncludePath)
        continue;

      // Then, add the path notes if necessary.
      PathPieces FlatPath = PD->path.flatten(/*ShouldFlattenMacros=*/true);
      for (const auto &Piece : FlatPath) {
        if (isa<PathDiagnosticNotePiece>(Piece.get()))
          continue;

        SourceLocation NoteLoc = Piece->getLocation().asLocation();
        Diag.Report(NoteLoc, NoteID) << Piece->getString()
                                     << Piece->getRanges();
      }
    }
  }
};
} // end anonymous namespace

//===----------------------------------------------------------------------===//
// AnalysisConsumer declaration.
//===----------------------------------------------------------------------===//

namespace {

class AnalysisConsumer : public AnalysisASTConsumer,
                         public RecursiveASTVisitor<AnalysisConsumer> {
  enum {
    AM_None = 0,
    AM_Syntax = 0x1,
    AM_Path = 0x2
  };
  typedef unsigned AnalysisMode;

  /// Mode of the analyzes while recursively visiting Decls.
  AnalysisMode RecVisitorMode;
  /// Bug Reporter to use while recursively visiting Decls.
  BugReporter *RecVisitorBR;

  std::vector<std::function<void(CheckerRegistry &)>> CheckerRegistrationFns;

public:
  ASTContext *Ctx;
  const Preprocessor &PP;
  const std::string OutDir;
  AnalyzerOptionsRef Opts;
  ArrayRef<std::string> Plugins;
  CodeInjector *Injector;
  cross_tu::CrossTranslationUnitContext CTU;

  /// Stores the declarations from the local translation unit.
  /// Note, we pre-compute the local declarations at parse time as an
  /// optimization to make sure we do not deserialize everything from disk.
  /// The local declaration to all declarations ratio might be very small when
  /// working with a PCH file.
  SetOfDecls LocalTUDecls;

  // Set of PathDiagnosticConsumers.  Owned by AnalysisManager.
  PathDiagnosticConsumers PathConsumers;

  StoreManagerCreator CreateStoreMgr;
  ConstraintManagerCreator CreateConstraintMgr;

  std::unique_ptr<CheckerManager> checkerMgr;
  std::unique_ptr<AnalysisManager> Mgr;

  /// Time the analyzes time of each translation unit.
  std::unique_ptr<llvm::TimerGroup> AnalyzerTimers;
  std::unique_ptr<llvm::Timer> TUTotalTimer;

  /// The information about analyzed functions shared throughout the
  /// translation unit.
  FunctionSummariesTy FunctionSummaries;

  AnalysisConsumer(CompilerInstance &CI, const std::string &outdir,
                   AnalyzerOptionsRef opts, ArrayRef<std::string> plugins,
                   CodeInjector *injector)
      : RecVisitorMode(0), RecVisitorBR(nullptr), Ctx(nullptr),
        PP(CI.getPreprocessor()), OutDir(outdir), Opts(std::move(opts)),
        Plugins(plugins), Injector(injector), CTU(CI) {
    DigestAnalyzerOptions();
    if (Opts->PrintStats || Opts->ShouldSerializeStats) {
      AnalyzerTimers = llvm::make_unique<llvm::TimerGroup>(
          "analyzer", "Analyzer timers");
      TUTotalTimer = llvm::make_unique<llvm::Timer>(
          "time", "Analyzer total time", *AnalyzerTimers);
      llvm::EnableStatistics(/* PrintOnExit= */ false);
    }
  }

  ~AnalysisConsumer() override {
    if (Opts->PrintStats) {
      llvm::PrintStatistics();
    }
  }

  void DigestAnalyzerOptions() {
    if (Opts->AnalysisDiagOpt != PD_NONE) {
      // Create the PathDiagnosticConsumer.
      ClangDiagPathDiagConsumer *clangDiags =
          new ClangDiagPathDiagConsumer(PP.getDiagnostics());
      PathConsumers.push_back(clangDiags);

      if (Opts->AnalysisDiagOpt == PD_TEXT) {
        clangDiags->enablePaths();

      } else if (!OutDir.empty()) {
        switch (Opts->AnalysisDiagOpt) {
        default:
#define ANALYSIS_DIAGNOSTICS(NAME, CMDFLAG, DESC, CREATEFN)                    \
  case PD_##NAME:                                                              \
    CREATEFN(*Opts.get(), PathConsumers, OutDir, PP);                       \
    break;
#include "clang/StaticAnalyzer/Core/Analyses.def"
        }
      }
    }

    // Create the analyzer component creators.
    switch (Opts->AnalysisStoreOpt) {
    default:
      llvm_unreachable("Unknown store manager.");
#define ANALYSIS_STORE(NAME, CMDFLAG, DESC, CREATEFN)           \
      case NAME##Model: CreateStoreMgr = CREATEFN; break;
#include "clang/StaticAnalyzer/Core/Analyses.def"
    }

    switch (Opts->AnalysisConstraintsOpt) {
    default:
      llvm_unreachable("Unknown constraint manager.");
#define ANALYSIS_CONSTRAINTS(NAME, CMDFLAG, DESC, CREATEFN)     \
      case NAME##Model: CreateConstraintMgr = CREATEFN; break;
#include "clang/StaticAnalyzer/Core/Analyses.def"
    }
  }

  void DisplayFunction(const Decl *D, AnalysisMode Mode,
                       ExprEngine::InliningModes IMode) {
    if (!Opts->AnalyzerDisplayProgress)
      return;

    SourceManager &SM = Mgr->getASTContext().getSourceManager();
    PresumedLoc Loc = SM.getPresumedLoc(D->getLocation());
    if (Loc.isValid()) {
      llvm::errs() << "ANALYZE";

      if (Mode == AM_Syntax)
        llvm::errs() << " (Syntax)";
      else if (Mode == AM_Path) {
        llvm::errs() << " (Path, ";
        switch (IMode) {
          case ExprEngine::Inline_Minimal:
            llvm::errs() << " Inline_Minimal";
            break;
          case ExprEngine::Inline_Regular:
            llvm::errs() << " Inline_Regular";
            break;
        }
        llvm::errs() << ")";
      }
      else
        assert(Mode == (AM_Syntax | AM_Path) && "Unexpected mode!");

      llvm::errs() << ": " << Loc.getFilename() << ' '
                           << getFunctionName(D) << '\n';
    }
  }

  void Initialize(ASTContext &Context) override {
    Ctx = &Context;
    checkerMgr = createCheckerManager(
        *Ctx, *Opts, Plugins, CheckerRegistrationFns, PP.getDiagnostics());

    Mgr = llvm::make_unique<AnalysisManager>(
        *Ctx, PP.getDiagnostics(), PathConsumers, CreateStoreMgr,
        CreateConstraintMgr, checkerMgr.get(), *Opts, Injector);
  }

  /// Store the top level decls in the set to be processed later on.
  /// (Doing this pre-processing avoids deserialization of data from PCH.)
  bool HandleTopLevelDecl(DeclGroupRef D) override;
  void HandleTopLevelDeclInObjCContainer(DeclGroupRef D) override;

  void HandleTranslationUnit(ASTContext &C) override;

  /// Determine which inlining mode should be used when this function is
  /// analyzed. This allows to redefine the default inlining policies when
  /// analyzing a given function.
  ExprEngine::InliningModes
    getInliningModeForFunction(const Decl *D, const SetOfConstDecls &Visited);

  /// Build the call graph for all the top level decls of this TU and
  /// use it to define the order in which the functions should be visited.
  void HandleDeclsCallGraph(const unsigned LocalTUDeclsSize);

  /// Run analyzes(syntax or path sensitive) on the given function.
  /// \param Mode - determines if we are requesting syntax only or path
  /// sensitive only analysis.
  /// \param VisitedCallees - The output parameter, which is populated with the
  /// set of functions which should be considered analyzed after analyzing the
  /// given root function.
  void HandleCode(Decl *D, AnalysisMode Mode,
                  ExprEngine::InliningModes IMode = ExprEngine::Inline_Minimal,
                  SetOfConstDecls *VisitedCallees = nullptr);

  void RunPathSensitiveChecks(Decl *D,
                              ExprEngine::InliningModes IMode,
                              SetOfConstDecls *VisitedCallees);

  /// Visitors for the RecursiveASTVisitor.
  bool shouldWalkTypesOfTypeLocs() const { return false; }

  /// Handle callbacks for arbitrary Decls.
  bool VisitDecl(Decl *D) {
    AnalysisMode Mode = getModeForDecl(D, RecVisitorMode);
    if (Mode & AM_Syntax)
      checkerMgr->runCheckersOnASTDecl(D, *Mgr, *RecVisitorBR);
    return true;
  }

  bool VisitFunctionDecl(FunctionDecl *FD) {
    IdentifierInfo *II = FD->getIdentifier();
    if (II && II->getName().startswith("__inline"))
      return true;

    // We skip function template definitions, as their semantics is
    // only determined when they are instantiated.
    if (FD->isThisDeclarationADefinition() &&
        !FD->isDependentContext()) {
      assert(RecVisitorMode == AM_Syntax || Mgr->shouldInlineCall() == false);
      HandleCode(FD, RecVisitorMode);
    }
    return true;
  }

  bool VisitObjCMethodDecl(ObjCMethodDecl *MD) {
    if (MD->isThisDeclarationADefinition()) {
      assert(RecVisitorMode == AM_Syntax || Mgr->shouldInlineCall() == false);
      HandleCode(MD, RecVisitorMode);
    }
    return true;
  }

  bool VisitBlockDecl(BlockDecl *BD) {
    if (BD->hasBody()) {
      assert(RecVisitorMode == AM_Syntax || Mgr->shouldInlineCall() == false);
      // Since we skip function template definitions, we should skip blocks
      // declared in those functions as well.
      if (!BD->isDependentContext()) {
        HandleCode(BD, RecVisitorMode);
      }
    }
    return true;
  }

  void AddDiagnosticConsumer(PathDiagnosticConsumer *Consumer) override {
    PathConsumers.push_back(Consumer);
  }

  void AddCheckerRegistrationFn(std::function<void(CheckerRegistry&)> Fn) override {
    CheckerRegistrationFns.push_back(std::move(Fn));
  }

private:
  void storeTopLevelDecls(DeclGroupRef DG);
  std::string getFunctionName(const Decl *D);

  /// Check if we should skip (not analyze) the given function.
  AnalysisMode getModeForDecl(Decl *D, AnalysisMode Mode);
  void runAnalysisOnTranslationUnit(ASTContext &C);

  /// Print \p S to stderr if \c Opts->AnalyzerDisplayProgress is set.
  void reportAnalyzerProgress(StringRef S);
};
} // end anonymous namespace


//===----------------------------------------------------------------------===//
// AnalysisConsumer implementation.
//===----------------------------------------------------------------------===//
bool AnalysisConsumer::HandleTopLevelDecl(DeclGroupRef DG) {
  storeTopLevelDecls(DG);
  return true;
}

void AnalysisConsumer::HandleTopLevelDeclInObjCContainer(DeclGroupRef DG) {
  storeTopLevelDecls(DG);
}

void AnalysisConsumer::storeTopLevelDecls(DeclGroupRef DG) {
  for (DeclGroupRef::iterator I = DG.begin(), E = DG.end(); I != E; ++I) {

    // Skip ObjCMethodDecl, wait for the objc container to avoid
    // analyzing twice.
    if (isa<ObjCMethodDecl>(*I))
      continue;

    LocalTUDecls.push_back(*I);
  }
}

static bool shouldSkipFunction(const Decl *D,
                               const SetOfConstDecls &Visited,
                               const SetOfConstDecls &VisitedAsTopLevel) {
  if (VisitedAsTopLevel.count(D))
    return true;

  // We want to re-analyse the functions as top level in the following cases:
  // - The 'init' methods should be reanalyzed because
  //   ObjCNonNilReturnValueChecker assumes that '[super init]' never returns
  //   'nil' and unless we analyze the 'init' functions as top level, we will
  //   not catch errors within defensive code.
  // - We want to reanalyze all ObjC methods as top level to report Retain
  //   Count naming convention errors more aggressively.
  if (isa<ObjCMethodDecl>(D))
    return false;
  // We also want to reanalyze all C++ copy and move assignment operators to
  // separately check the two cases where 'this' aliases with the parameter and
  // where it may not. (cplusplus.SelfAssignmentChecker)
  if (const auto *MD = dyn_cast<CXXMethodDecl>(D)) {
    if (MD->isCopyAssignmentOperator() || MD->isMoveAssignmentOperator())
      return false;
  }

  // Otherwise, if we visited the function before, do not reanalyze it.
  return Visited.count(D);
}

ExprEngine::InliningModes
AnalysisConsumer::getInliningModeForFunction(const Decl *D,
                                             const SetOfConstDecls &Visited) {
  // We want to reanalyze all ObjC methods as top level to report Retain
  // Count naming convention errors more aggressively. But we should tune down
  // inlining when reanalyzing an already inlined function.
  if (Visited.count(D) && isa<ObjCMethodDecl>(D)) {
    const ObjCMethodDecl *ObjCM = cast<ObjCMethodDecl>(D);
    if (ObjCM->getMethodFamily() != OMF_init)
      return ExprEngine::Inline_Minimal;
  }

  return ExprEngine::Inline_Regular;
}

void AnalysisConsumer::HandleDeclsCallGraph(const unsigned LocalTUDeclsSize) {
  // Build the Call Graph by adding all the top level declarations to the graph.
  // Note: CallGraph can trigger deserialization of more items from a pch
  // (though HandleInterestingDecl); triggering additions to LocalTUDecls.
  // We rely on random access to add the initially processed Decls to CG.
  CallGraph CG;
  for (unsigned i = 0 ; i < LocalTUDeclsSize ; ++i) {
    CG.addToCallGraph(LocalTUDecls[i]);
  }

  // Walk over all of the call graph nodes in topological order, so that we
  // analyze parents before the children. Skip the functions inlined into
  // the previously processed functions. Use external Visited set to identify
  // inlined functions. The topological order allows the "do not reanalyze
  // previously inlined function" performance heuristic to be triggered more
  // often.
  SetOfConstDecls Visited;
  SetOfConstDecls VisitedAsTopLevel;
  llvm::ReversePostOrderTraversal<clang::CallGraph*> RPOT(&CG);
  for (llvm::ReversePostOrderTraversal<clang::CallGraph*>::rpo_iterator
         I = RPOT.begin(), E = RPOT.end(); I != E; ++I) {
    NumFunctionTopLevel++;

    CallGraphNode *N = *I;
    Decl *D = N->getDecl();

    // Skip the abstract root node.
    if (!D)
      continue;

    // Skip the functions which have been processed already or previously
    // inlined.
    if (shouldSkipFunction(D, Visited, VisitedAsTopLevel))
      continue;

    // Analyze the function.
    SetOfConstDecls VisitedCallees;

    HandleCode(D, AM_Path, getInliningModeForFunction(D, Visited),
               (Mgr->options.InliningMode == All ? nullptr : &VisitedCallees));

    // Add the visited callees to the global visited set.
    for (const Decl *Callee : VisitedCallees)
      // Decls from CallGraph are already canonical. But Decls coming from
      // CallExprs may be not. We should canonicalize them manually.
      Visited.insert(isa<ObjCMethodDecl>(Callee) ? Callee
                                                 : Callee->getCanonicalDecl());
    VisitedAsTopLevel.insert(D);
  }
}

static bool isBisonFile(ASTContext &C) {
  const SourceManager &SM = C.getSourceManager();
  FileID FID = SM.getMainFileID();
  StringRef Buffer = SM.getBuffer(FID)->getBuffer();
  if (Buffer.startswith("/* A Bison parser, made by"))
    return true;
  return false;
}

void AnalysisConsumer::runAnalysisOnTranslationUnit(ASTContext &C) {
  BugReporter BR(*Mgr);
  TranslationUnitDecl *TU = C.getTranslationUnitDecl();
  checkerMgr->runCheckersOnASTDecl(TU, *Mgr, BR);

  // Run the AST-only checks using the order in which functions are defined.
  // If inlining is not turned on, use the simplest function order for path
  // sensitive analyzes as well.
  RecVisitorMode = AM_Syntax;
  if (!Mgr->shouldInlineCall())
    RecVisitorMode |= AM_Path;
  RecVisitorBR = &BR;

  // Process all the top level declarations.
  //
  // Note: TraverseDecl may modify LocalTUDecls, but only by appending more
  // entries.  Thus we don't use an iterator, but rely on LocalTUDecls
  // random access.  By doing so, we automatically compensate for iterators
  // possibly being invalidated, although this is a bit slower.
  const unsigned LocalTUDeclsSize = LocalTUDecls.size();
  for (unsigned i = 0 ; i < LocalTUDeclsSize ; ++i) {
    TraverseDecl(LocalTUDecls[i]);
  }

  if (Mgr->shouldInlineCall())
    HandleDeclsCallGraph(LocalTUDeclsSize);

  // After all decls handled, run checkers on the entire TranslationUnit.
  checkerMgr->runCheckersOnEndOfTranslationUnit(TU, *Mgr, BR);

  RecVisitorBR = nullptr;
}

void AnalysisConsumer::reportAnalyzerProgress(StringRef S) {
  if (Opts->AnalyzerDisplayProgress)
    llvm::errs() << S;
}

void AnalysisConsumer::HandleTranslationUnit(ASTContext &C) {

  // Don't run the actions if an error has occurred with parsing the file.
  DiagnosticsEngine &Diags = PP.getDiagnostics();
  if (Diags.hasErrorOccurred() || Diags.hasFatalErrorOccurred())
    return;

  if (TUTotalTimer) TUTotalTimer->startTimer();

  if (isBisonFile(C)) {
    reportAnalyzerProgress("Skipping bison-generated file\n");
  } else if (Opts->DisableAllChecks) {

    // Don't analyze if the user explicitly asked for no checks to be performed
    // on this file.
    reportAnalyzerProgress("All checks are disabled using a supplied option\n");
  } else {
    // Otherwise, just run the analysis.
    runAnalysisOnTranslationUnit(C);
  }

  if (TUTotalTimer) TUTotalTimer->stopTimer();

  // Count how many basic blocks we have not covered.
  NumBlocksInAnalyzedFunctions = FunctionSummaries.getTotalNumBasicBlocks();
  NumVisitedBlocksInAnalyzedFunctions =
      FunctionSummaries.getTotalNumVisitedBasicBlocks();
  if (NumBlocksInAnalyzedFunctions > 0)
    PercentReachableBlocks =
      (FunctionSummaries.getTotalNumVisitedBasicBlocks() * 100) /
        NumBlocksInAnalyzedFunctions;

  // Explicitly destroy the PathDiagnosticConsumer.  This will flush its output.
  // FIXME: This should be replaced with something that doesn't rely on
  // side-effects in PathDiagnosticConsumer's destructor. This is required when
  // used with option -disable-free.
  Mgr.reset();
}

std::string AnalysisConsumer::getFunctionName(const Decl *D) {
  std::string Str;
  llvm::raw_string_ostream OS(Str);

  if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
    OS << FD->getQualifiedNameAsString();

    // In C++, there are overloads.
    if (Ctx->getLangOpts().CPlusPlus) {
      OS << '(';
      for (const auto &P : FD->parameters()) {
        if (P != *FD->param_begin())
          OS << ", ";
        OS << P->getType().getAsString();
      }
      OS << ')';
    }

  } else if (isa<BlockDecl>(D)) {
    PresumedLoc Loc = Ctx->getSourceManager().getPresumedLoc(D->getLocation());

    if (Loc.isValid()) {
      OS << "block (line: " << Loc.getLine() << ", col: " << Loc.getColumn()
         << ')';
    }

  } else if (const ObjCMethodDecl *OMD = dyn_cast<ObjCMethodDecl>(D)) {

    // FIXME: copy-pasted from CGDebugInfo.cpp.
    OS << (OMD->isInstanceMethod() ? '-' : '+') << '[';
    const DeclContext *DC = OMD->getDeclContext();
    if (const auto *OID = dyn_cast<ObjCImplementationDecl>(DC)) {
      OS << OID->getName();
    } else if (const auto *OID = dyn_cast<ObjCInterfaceDecl>(DC)) {
      OS << OID->getName();
    } else if (const auto *OC = dyn_cast<ObjCCategoryDecl>(DC)) {
      if (OC->IsClassExtension()) {
        OS << OC->getClassInterface()->getName();
      } else {
        OS << OC->getIdentifier()->getNameStart() << '('
           << OC->getIdentifier()->getNameStart() << ')';
      }
    } else if (const auto *OCD = dyn_cast<ObjCCategoryImplDecl>(DC)) {
      OS << OCD->getClassInterface()->getName() << '('
         << OCD->getName() << ')';
    } else if (isa<ObjCProtocolDecl>(DC)) {
      // We can extract the type of the class from the self pointer.
      if (ImplicitParamDecl *SelfDecl = OMD->getSelfDecl()) {
        QualType ClassTy =
            cast<ObjCObjectPointerType>(SelfDecl->getType())->getPointeeType();
        ClassTy.print(OS, PrintingPolicy(LangOptions()));
      }
    }
    OS << ' ' << OMD->getSelector().getAsString() << ']';

  }

  return OS.str();
}

AnalysisConsumer::AnalysisMode
AnalysisConsumer::getModeForDecl(Decl *D, AnalysisMode Mode) {
  if (!Opts->AnalyzeSpecificFunction.empty() &&
      getFunctionName(D) != Opts->AnalyzeSpecificFunction)
    return AM_None;

  // Unless -analyze-all is specified, treat decls differently depending on
  // where they came from:
  // - Main source file: run both path-sensitive and non-path-sensitive checks.
  // - Header files: run non-path-sensitive checks only.
  // - System headers: don't run any checks.
  SourceManager &SM = Ctx->getSourceManager();
  const Stmt *Body = D->getBody();
  SourceLocation SL = Body ? Body->getBeginLoc() : D->getLocation();
  SL = SM.getExpansionLoc(SL);

  if (!Opts->AnalyzeAll && !Mgr->isInCodeFile(SL)) {
    if (SL.isInvalid() || SM.isInSystemHeader(SL))
      return AM_None;
    return Mode & ~AM_Path;
  }

  return Mode;
}

void AnalysisConsumer::HandleCode(Decl *D, AnalysisMode Mode,
                                  ExprEngine::InliningModes IMode,
                                  SetOfConstDecls *VisitedCallees) {
  if (!D->hasBody())
    return;
  Mode = getModeForDecl(D, Mode);
  if (Mode == AM_None)
    return;

  // Clear the AnalysisManager of old AnalysisDeclContexts.
  Mgr->ClearContexts();
  // Ignore autosynthesized code.
  if (Mgr->getAnalysisDeclContext(D)->isBodyAutosynthesized())
    return;

  DisplayFunction(D, Mode, IMode);
  CFG *DeclCFG = Mgr->getCFG(D);
  if (DeclCFG)
    MaxCFGSize.updateMax(DeclCFG->size());

  BugReporter BR(*Mgr);

  if (Mode & AM_Syntax)
    checkerMgr->runCheckersOnASTBody(D, *Mgr, BR);
  if ((Mode & AM_Path) && checkerMgr->hasPathSensitiveCheckers()) {
    RunPathSensitiveChecks(D, IMode, VisitedCallees);
    if (IMode != ExprEngine::Inline_Minimal)
      NumFunctionsAnalyzed++;
  }
}

//===----------------------------------------------------------------------===//
// Path-sensitive checking.
//===----------------------------------------------------------------------===//

void AnalysisConsumer::RunPathSensitiveChecks(Decl *D,
                                              ExprEngine::InliningModes IMode,
                                              SetOfConstDecls *VisitedCallees) {
  // Construct the analysis engine.  First check if the CFG is valid.
  // FIXME: Inter-procedural analysis will need to handle invalid CFGs.
  if (!Mgr->getCFG(D))
    return;

  // See if the LiveVariables analysis scales.
  if (!Mgr->getAnalysisDeclContext(D)->getAnalysis<RelaxedLiveVariables>())
    return;

  ExprEngine Eng(CTU, *Mgr, VisitedCallees, &FunctionSummaries, IMode);

  // Execute the worklist algorithm.
  Eng.ExecuteWorkList(Mgr->getAnalysisDeclContextManager().getStackFrame(D),
                      Mgr->options.MaxNodesPerTopLevelFunction);

  if (!Mgr->options.DumpExplodedGraphTo.empty())
    Eng.DumpGraph(Mgr->options.TrimGraph, Mgr->options.DumpExplodedGraphTo);

  // Visualize the exploded graph.
  if (Mgr->options.visualizeExplodedGraphWithGraphViz)
    Eng.ViewGraph(Mgr->options.TrimGraph);

  // Display warnings.
  Eng.getBugReporter().FlushReports();
}

//===----------------------------------------------------------------------===//
// AnalysisConsumer creation.
//===----------------------------------------------------------------------===//

std::unique_ptr<AnalysisASTConsumer>
ento::CreateAnalysisConsumer(CompilerInstance &CI) {
  // Disable the effects of '-Werror' when using the AnalysisConsumer.
  CI.getPreprocessor().getDiagnostics().setWarningsAsErrors(false);

  AnalyzerOptionsRef analyzerOpts = CI.getAnalyzerOpts();
  bool hasModelPath = analyzerOpts->Config.count("model-path") > 0;

  return llvm::make_unique<AnalysisConsumer>(
      CI, CI.getFrontendOpts().OutputFile, analyzerOpts,
      CI.getFrontendOpts().Plugins,
      hasModelPath ? new ModelInjector(CI) : nullptr);
}
