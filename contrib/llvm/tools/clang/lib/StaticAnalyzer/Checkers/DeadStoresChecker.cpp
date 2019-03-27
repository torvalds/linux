//==- DeadStoresChecker.cpp - Check for stores to dead variables -*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines a DeadStores, a flow-sensitive checker that looks for
//  stores to variables that are no longer live.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/ParentMap.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Analysis/Analyses/LiveVariables.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/SaveAndRestore.h"

using namespace clang;
using namespace ento;

namespace {

/// A simple visitor to record what VarDecls occur in EH-handling code.
class EHCodeVisitor : public RecursiveASTVisitor<EHCodeVisitor> {
public:
  bool inEH;
  llvm::DenseSet<const VarDecl *> &S;

  bool TraverseObjCAtFinallyStmt(ObjCAtFinallyStmt *S) {
    SaveAndRestore<bool> inFinally(inEH, true);
    return ::RecursiveASTVisitor<EHCodeVisitor>::TraverseObjCAtFinallyStmt(S);
  }

  bool TraverseObjCAtCatchStmt(ObjCAtCatchStmt *S) {
    SaveAndRestore<bool> inCatch(inEH, true);
    return ::RecursiveASTVisitor<EHCodeVisitor>::TraverseObjCAtCatchStmt(S);
  }

  bool TraverseCXXCatchStmt(CXXCatchStmt *S) {
    SaveAndRestore<bool> inCatch(inEH, true);
    return TraverseStmt(S->getHandlerBlock());
  }

  bool VisitDeclRefExpr(DeclRefExpr *DR) {
    if (inEH)
      if (const VarDecl *D = dyn_cast<VarDecl>(DR->getDecl()))
        S.insert(D);
    return true;
  }

  EHCodeVisitor(llvm::DenseSet<const VarDecl *> &S) :
  inEH(false), S(S) {}
};

// FIXME: Eventually migrate into its own file, and have it managed by
// AnalysisManager.
class ReachableCode {
  const CFG &cfg;
  llvm::BitVector reachable;
public:
  ReachableCode(const CFG &cfg)
    : cfg(cfg), reachable(cfg.getNumBlockIDs(), false) {}

  void computeReachableBlocks();

  bool isReachable(const CFGBlock *block) const {
    return reachable[block->getBlockID()];
  }
};
}

void ReachableCode::computeReachableBlocks() {
  if (!cfg.getNumBlockIDs())
    return;

  SmallVector<const CFGBlock*, 10> worklist;
  worklist.push_back(&cfg.getEntry());

  while (!worklist.empty()) {
    const CFGBlock *block = worklist.pop_back_val();
    llvm::BitVector::reference isReachable = reachable[block->getBlockID()];
    if (isReachable)
      continue;
    isReachable = true;
    for (CFGBlock::const_succ_iterator i = block->succ_begin(),
                                       e = block->succ_end(); i != e; ++i)
      if (const CFGBlock *succ = *i)
        worklist.push_back(succ);
  }
}

static const Expr *
LookThroughTransitiveAssignmentsAndCommaOperators(const Expr *Ex) {
  while (Ex) {
    const BinaryOperator *BO =
      dyn_cast<BinaryOperator>(Ex->IgnoreParenCasts());
    if (!BO)
      break;
    if (BO->getOpcode() == BO_Assign) {
      Ex = BO->getRHS();
      continue;
    }
    if (BO->getOpcode() == BO_Comma) {
      Ex = BO->getRHS();
      continue;
    }
    break;
  }
  return Ex;
}

namespace {
class DeadStoreObs : public LiveVariables::Observer {
  const CFG &cfg;
  ASTContext &Ctx;
  BugReporter& BR;
  const CheckerBase *Checker;
  AnalysisDeclContext* AC;
  ParentMap& Parents;
  llvm::SmallPtrSet<const VarDecl*, 20> Escaped;
  std::unique_ptr<ReachableCode> reachableCode;
  const CFGBlock *currentBlock;
  std::unique_ptr<llvm::DenseSet<const VarDecl *>> InEH;

  enum DeadStoreKind { Standard, Enclosing, DeadIncrement, DeadInit };

public:
  DeadStoreObs(const CFG &cfg, ASTContext &ctx, BugReporter &br,
               const CheckerBase *checker, AnalysisDeclContext *ac,
               ParentMap &parents,
               llvm::SmallPtrSet<const VarDecl *, 20> &escaped)
      : cfg(cfg), Ctx(ctx), BR(br), Checker(checker), AC(ac), Parents(parents),
        Escaped(escaped), currentBlock(nullptr) {}

  ~DeadStoreObs() override {}

  bool isLive(const LiveVariables::LivenessValues &Live, const VarDecl *D) {
    if (Live.isLive(D))
      return true;
    // Lazily construct the set that records which VarDecls are in
    // EH code.
    if (!InEH.get()) {
      InEH.reset(new llvm::DenseSet<const VarDecl *>());
      EHCodeVisitor V(*InEH.get());
      V.TraverseStmt(AC->getBody());
    }
    // Treat all VarDecls that occur in EH code as being "always live"
    // when considering to suppress dead stores.  Frequently stores
    // are followed by reads in EH code, but we don't have the ability
    // to analyze that yet.
    return InEH->count(D);
  }

  void Report(const VarDecl *V, DeadStoreKind dsk,
              PathDiagnosticLocation L, SourceRange R) {
    if (Escaped.count(V))
      return;

    // Compute reachable blocks within the CFG for trivial cases
    // where a bogus dead store can be reported because itself is unreachable.
    if (!reachableCode.get()) {
      reachableCode.reset(new ReachableCode(cfg));
      reachableCode->computeReachableBlocks();
    }

    if (!reachableCode->isReachable(currentBlock))
      return;

    SmallString<64> buf;
    llvm::raw_svector_ostream os(buf);
    const char *BugType = nullptr;

    switch (dsk) {
      case DeadInit:
        BugType = "Dead initialization";
        os << "Value stored to '" << *V
           << "' during its initialization is never read";
        break;

      case DeadIncrement:
        BugType = "Dead increment";
        LLVM_FALLTHROUGH;
      case Standard:
        if (!BugType) BugType = "Dead assignment";
        os << "Value stored to '" << *V << "' is never read";
        break;

      case Enclosing:
        // Don't report issues in this case, e.g.: "if (x = foo())",
        // where 'x' is unused later.  We have yet to see a case where
        // this is a real bug.
        return;
    }

    BR.EmitBasicReport(AC->getDecl(), Checker, BugType, "Dead store", os.str(),
                       L, R);
  }

  void CheckVarDecl(const VarDecl *VD, const Expr *Ex, const Expr *Val,
                    DeadStoreKind dsk,
                    const LiveVariables::LivenessValues &Live) {

    if (!VD->hasLocalStorage())
      return;
    // Reference types confuse the dead stores checker.  Skip them
    // for now.
    if (VD->getType()->getAs<ReferenceType>())
      return;

    if (!isLive(Live, VD) &&
        !(VD->hasAttr<UnusedAttr>() || VD->hasAttr<BlocksAttr>() ||
          VD->hasAttr<ObjCPreciseLifetimeAttr>())) {

      PathDiagnosticLocation ExLoc =
        PathDiagnosticLocation::createBegin(Ex, BR.getSourceManager(), AC);
      Report(VD, dsk, ExLoc, Val->getSourceRange());
    }
  }

  void CheckDeclRef(const DeclRefExpr *DR, const Expr *Val, DeadStoreKind dsk,
                    const LiveVariables::LivenessValues& Live) {
    if (const VarDecl *VD = dyn_cast<VarDecl>(DR->getDecl()))
      CheckVarDecl(VD, DR, Val, dsk, Live);
  }

  bool isIncrement(VarDecl *VD, const BinaryOperator* B) {
    if (B->isCompoundAssignmentOp())
      return true;

    const Expr *RHS = B->getRHS()->IgnoreParenCasts();
    const BinaryOperator* BRHS = dyn_cast<BinaryOperator>(RHS);

    if (!BRHS)
      return false;

    const DeclRefExpr *DR;

    if ((DR = dyn_cast<DeclRefExpr>(BRHS->getLHS()->IgnoreParenCasts())))
      if (DR->getDecl() == VD)
        return true;

    if ((DR = dyn_cast<DeclRefExpr>(BRHS->getRHS()->IgnoreParenCasts())))
      if (DR->getDecl() == VD)
        return true;

    return false;
  }

  void observeStmt(const Stmt *S, const CFGBlock *block,
                   const LiveVariables::LivenessValues &Live) override {

    currentBlock = block;

    // Skip statements in macros.
    if (S->getBeginLoc().isMacroID())
      return;

    // Only cover dead stores from regular assignments.  ++/-- dead stores
    // have never flagged a real bug.
    if (const BinaryOperator* B = dyn_cast<BinaryOperator>(S)) {
      if (!B->isAssignmentOp()) return; // Skip non-assignments.

      if (DeclRefExpr *DR = dyn_cast<DeclRefExpr>(B->getLHS()))
        if (VarDecl *VD = dyn_cast<VarDecl>(DR->getDecl())) {
          // Special case: check for assigning null to a pointer.
          //  This is a common form of defensive programming.
          const Expr *RHS =
            LookThroughTransitiveAssignmentsAndCommaOperators(B->getRHS());
          RHS = RHS->IgnoreParenCasts();

          QualType T = VD->getType();
          if (T.isVolatileQualified())
            return;
          if (T->isPointerType() || T->isObjCObjectPointerType()) {
            if (RHS->isNullPointerConstant(Ctx, Expr::NPC_ValueDependentIsNull))
              return;
          }

          // Special case: self-assignments.  These are often used to shut up
          //  "unused variable" compiler warnings.
          if (const DeclRefExpr *RhsDR = dyn_cast<DeclRefExpr>(RHS))
            if (VD == dyn_cast<VarDecl>(RhsDR->getDecl()))
              return;

          // Otherwise, issue a warning.
          DeadStoreKind dsk = Parents.isConsumedExpr(B)
                              ? Enclosing
                              : (isIncrement(VD,B) ? DeadIncrement : Standard);

          CheckVarDecl(VD, DR, B->getRHS(), dsk, Live);
        }
    }
    else if (const UnaryOperator* U = dyn_cast<UnaryOperator>(S)) {
      if (!U->isIncrementOp() || U->isPrefix())
        return;

      const Stmt *parent = Parents.getParentIgnoreParenCasts(U);
      if (!parent || !isa<ReturnStmt>(parent))
        return;

      const Expr *Ex = U->getSubExpr()->IgnoreParenCasts();

      if (const DeclRefExpr *DR = dyn_cast<DeclRefExpr>(Ex))
        CheckDeclRef(DR, U, DeadIncrement, Live);
    }
    else if (const DeclStmt *DS = dyn_cast<DeclStmt>(S))
      // Iterate through the decls.  Warn if any initializers are complex
      // expressions that are not live (never used).
      for (const auto *DI : DS->decls()) {
        const auto *V = dyn_cast<VarDecl>(DI);

        if (!V)
          continue;

        if (V->hasLocalStorage()) {
          // Reference types confuse the dead stores checker.  Skip them
          // for now.
          if (V->getType()->getAs<ReferenceType>())
            return;

          if (const Expr *E = V->getInit()) {
            while (const FullExpr *FE = dyn_cast<FullExpr>(E))
              E = FE->getSubExpr();

            // Look through transitive assignments, e.g.:
            // int x = y = 0;
            E = LookThroughTransitiveAssignmentsAndCommaOperators(E);

            // Don't warn on C++ objects (yet) until we can show that their
            // constructors/destructors don't have side effects.
            if (isa<CXXConstructExpr>(E))
              return;

            // A dead initialization is a variable that is dead after it
            // is initialized.  We don't flag warnings for those variables
            // marked 'unused' or 'objc_precise_lifetime'.
            if (!isLive(Live, V) &&
                !V->hasAttr<UnusedAttr>() &&
                !V->hasAttr<ObjCPreciseLifetimeAttr>()) {
              // Special case: check for initializations with constants.
              //
              //  e.g. : int x = 0;
              //
              // If x is EVER assigned a new value later, don't issue
              // a warning.  This is because such initialization can be
              // due to defensive programming.
              if (E->isEvaluatable(Ctx))
                return;

              if (const DeclRefExpr *DRE =
                  dyn_cast<DeclRefExpr>(E->IgnoreParenCasts()))
                if (const VarDecl *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
                  // Special case: check for initialization from constant
                  //  variables.
                  //
                  //  e.g. extern const int MyConstant;
                  //       int x = MyConstant;
                  //
                  if (VD->hasGlobalStorage() &&
                      VD->getType().isConstQualified())
                    return;
                  // Special case: check for initialization from scalar
                  //  parameters.  This is often a form of defensive
                  //  programming.  Non-scalars are still an error since
                  //  because it more likely represents an actual algorithmic
                  //  bug.
                  if (isa<ParmVarDecl>(VD) && VD->getType()->isScalarType())
                    return;
                }

              PathDiagnosticLocation Loc =
                PathDiagnosticLocation::create(V, BR.getSourceManager());
              Report(V, DeadInit, Loc, E->getSourceRange());
            }
          }
        }
      }
  }
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Driver function to invoke the Dead-Stores checker on a CFG.
//===----------------------------------------------------------------------===//

namespace {
class FindEscaped {
public:
  llvm::SmallPtrSet<const VarDecl*, 20> Escaped;

  void operator()(const Stmt *S) {
    // Check for '&'. Any VarDecl whose address has been taken we treat as
    // escaped.
    // FIXME: What about references?
    if (auto *LE = dyn_cast<LambdaExpr>(S)) {
      findLambdaReferenceCaptures(LE);
      return;
    }

    const UnaryOperator *U = dyn_cast<UnaryOperator>(S);
    if (!U)
      return;
    if (U->getOpcode() != UO_AddrOf)
      return;

    const Expr *E = U->getSubExpr()->IgnoreParenCasts();
    if (const DeclRefExpr *DR = dyn_cast<DeclRefExpr>(E))
      if (const VarDecl *VD = dyn_cast<VarDecl>(DR->getDecl()))
        Escaped.insert(VD);
  }

  // Treat local variables captured by reference in C++ lambdas as escaped.
  void findLambdaReferenceCaptures(const LambdaExpr *LE)  {
    const CXXRecordDecl *LambdaClass = LE->getLambdaClass();
    llvm::DenseMap<const VarDecl *, FieldDecl *> CaptureFields;
    FieldDecl *ThisCaptureField;
    LambdaClass->getCaptureFields(CaptureFields, ThisCaptureField);

    for (const LambdaCapture &C : LE->captures()) {
      if (!C.capturesVariable())
        continue;

      VarDecl *VD = C.getCapturedVar();
      const FieldDecl *FD = CaptureFields[VD];
      if (!FD)
        continue;

      // If the capture field is a reference type, it is capture-by-reference.
      if (FD->getType()->isReferenceType())
        Escaped.insert(VD);
    }
  }
};
} // end anonymous namespace


//===----------------------------------------------------------------------===//
// DeadStoresChecker
//===----------------------------------------------------------------------===//

namespace {
class DeadStoresChecker : public Checker<check::ASTCodeBody> {
public:
  void checkASTCodeBody(const Decl *D, AnalysisManager& mgr,
                        BugReporter &BR) const {

    // Don't do anything for template instantiations.
    // Proving that code in a template instantiation is "dead"
    // means proving that it is dead in all instantiations.
    // This same problem exists with -Wunreachable-code.
    if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D))
      if (FD->isTemplateInstantiation())
        return;

    if (LiveVariables *L = mgr.getAnalysis<LiveVariables>(D)) {
      CFG &cfg = *mgr.getCFG(D);
      AnalysisDeclContext *AC = mgr.getAnalysisDeclContext(D);
      ParentMap &pmap = mgr.getParentMap(D);
      FindEscaped FS;
      cfg.VisitBlockStmts(FS);
      DeadStoreObs A(cfg, BR.getContext(), BR, this, AC, pmap, FS.Escaped);
      L->runOnAllBlocks(A);
    }
  }
};
}

void ento::registerDeadStoresChecker(CheckerManager &mgr) {
  mgr.registerChecker<DeadStoresChecker>();
}
