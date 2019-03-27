//===--- TransGCCalls.cpp - Transformations to ARC mode -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Transforms.h"
#include "Internals.h"
#include "clang/AST/ASTContext.h"
#include "clang/Sema/SemaDiagnostic.h"

using namespace clang;
using namespace arcmt;
using namespace trans;

namespace {

class GCCollectableCallsChecker :
                         public RecursiveASTVisitor<GCCollectableCallsChecker> {
  MigrationContext &MigrateCtx;
  IdentifierInfo *NSMakeCollectableII;
  IdentifierInfo *CFMakeCollectableII;

public:
  GCCollectableCallsChecker(MigrationContext &ctx)
    : MigrateCtx(ctx) {
    IdentifierTable &Ids = MigrateCtx.Pass.Ctx.Idents;
    NSMakeCollectableII = &Ids.get("NSMakeCollectable");
    CFMakeCollectableII = &Ids.get("CFMakeCollectable");
  }

  bool shouldWalkTypesOfTypeLocs() const { return false; }

  bool VisitCallExpr(CallExpr *E) {
    TransformActions &TA = MigrateCtx.Pass.TA;

    if (MigrateCtx.isGCOwnedNonObjC(E->getType())) {
      TA.report(E->getBeginLoc(), diag::warn_arcmt_nsalloc_realloc,
                E->getSourceRange());
      return true;
    }

    Expr *CEE = E->getCallee()->IgnoreParenImpCasts();
    if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(CEE)) {
      if (FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(DRE->getDecl())) {
        if (!FD->getDeclContext()->getRedeclContext()->isFileContext())
          return true;

        if (FD->getIdentifier() == NSMakeCollectableII) {
          Transaction Trans(TA);
          TA.clearDiagnostic(diag::err_unavailable,
                             diag::err_unavailable_message,
                             diag::err_ovl_deleted_call, // ObjC++
                             DRE->getSourceRange());
          TA.replace(DRE->getSourceRange(), "CFBridgingRelease");

        } else if (FD->getIdentifier() == CFMakeCollectableII) {
          TA.reportError("CFMakeCollectable will leak the object that it "
                         "receives in ARC", DRE->getLocation(),
                         DRE->getSourceRange());
        }
      }
    }

    return true;
  }
};

} // anonymous namespace

void GCCollectableCallsTraverser::traverseBody(BodyContext &BodyCtx) {
  GCCollectableCallsChecker(BodyCtx.getMigrationContext())
                                            .TraverseStmt(BodyCtx.getTopStmt());
}
