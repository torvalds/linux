//===--- TransUnbridgedCasts.cpp - Transformations to ARC mode ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// rewriteUnbridgedCasts:
//
// A cast of non-objc pointer to an objc one is checked. If the non-objc pointer
// is from a file-level variable, __bridge cast is used to convert it.
// For the result of a function call that we know is +1/+0,
// __bridge/CFBridgingRelease is used.
//
//  NSString *str = (NSString *)kUTTypePlainText;
//  str = b ? kUTTypeRTF : kUTTypePlainText;
//  NSString *_uuidString = (NSString *)CFUUIDCreateString(kCFAllocatorDefault,
//                                                         _uuid);
// ---->
//  NSString *str = (__bridge NSString *)kUTTypePlainText;
//  str = (__bridge NSString *)(b ? kUTTypeRTF : kUTTypePlainText);
// NSString *_uuidString = (NSString *)
//            CFBridgingRelease(CFUUIDCreateString(kCFAllocatorDefault, _uuid));
//
// For a C pointer to ObjC, for casting 'self', __bridge is used.
//
//  CFStringRef str = (CFStringRef)self;
// ---->
//  CFStringRef str = (__bridge CFStringRef)self;
//
// Uses of Block_copy/Block_release macros are rewritten:
//
//  c = Block_copy(b);
//  Block_release(c);
// ---->
//  c = [b copy];
//  <removed>
//
//===----------------------------------------------------------------------===//

#include "Transforms.h"
#include "Internals.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/ParentMap.h"
#include "clang/Analysis/DomainSpecific/CocoaConventions.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "clang/Sema/SemaDiagnostic.h"
#include "llvm/ADT/SmallString.h"

using namespace clang;
using namespace arcmt;
using namespace trans;

namespace {

class UnbridgedCastRewriter : public RecursiveASTVisitor<UnbridgedCastRewriter>{
  MigrationPass &Pass;
  IdentifierInfo *SelfII;
  std::unique_ptr<ParentMap> StmtMap;
  Decl *ParentD;
  Stmt *Body;
  mutable std::unique_ptr<ExprSet> Removables;

public:
  UnbridgedCastRewriter(MigrationPass &pass)
    : Pass(pass), ParentD(nullptr), Body(nullptr) {
    SelfII = &Pass.Ctx.Idents.get("self");
  }

  void transformBody(Stmt *body, Decl *ParentD) {
    this->ParentD = ParentD;
    Body = body;
    StmtMap.reset(new ParentMap(body));
    TraverseStmt(body);
  }

  bool TraverseBlockDecl(BlockDecl *D) {
    // ParentMap does not enter into a BlockDecl to record its stmts, so use a
    // new UnbridgedCastRewriter to handle the block.
    UnbridgedCastRewriter(Pass).transformBody(D->getBody(), D);
    return true;
  }

  bool VisitCastExpr(CastExpr *E) {
    if (E->getCastKind() != CK_CPointerToObjCPointerCast &&
        E->getCastKind() != CK_BitCast &&
        E->getCastKind() != CK_AnyPointerToBlockPointerCast)
      return true;

    QualType castType = E->getType();
    Expr *castExpr = E->getSubExpr();
    QualType castExprType = castExpr->getType();

    if (castType->isObjCRetainableType() == castExprType->isObjCRetainableType())
      return true;

    bool exprRetainable = castExprType->isObjCIndirectLifetimeType();
    bool castRetainable = castType->isObjCIndirectLifetimeType();
    if (exprRetainable == castRetainable) return true;

    if (castExpr->isNullPointerConstant(Pass.Ctx,
                                        Expr::NPC_ValueDependentIsNull))
      return true;

    SourceLocation loc = castExpr->getExprLoc();
    if (loc.isValid() && Pass.Ctx.getSourceManager().isInSystemHeader(loc))
      return true;

    if (castType->isObjCRetainableType())
      transformNonObjCToObjCCast(E);
    else
      transformObjCToNonObjCCast(E);

    return true;
  }

private:
  void transformNonObjCToObjCCast(CastExpr *E) {
    if (!E) return;

    // Global vars are assumed that are cast as unretained.
    if (isGlobalVar(E))
      if (E->getSubExpr()->getType()->isPointerType()) {
        castToObjCObject(E, /*retained=*/false);
        return;
      }

    // If the cast is directly over the result of a Core Foundation function
    // try to figure out whether it should be cast as retained or unretained.
    Expr *inner = E->IgnoreParenCasts();
    if (CallExpr *callE = dyn_cast<CallExpr>(inner)) {
      if (FunctionDecl *FD = callE->getDirectCallee()) {
        if (FD->hasAttr<CFReturnsRetainedAttr>()) {
          castToObjCObject(E, /*retained=*/true);
          return;
        }
        if (FD->hasAttr<CFReturnsNotRetainedAttr>()) {
          castToObjCObject(E, /*retained=*/false);
          return;
        }
        if (FD->isGlobal() &&
            FD->getIdentifier() &&
            ento::cocoa::isRefType(E->getSubExpr()->getType(), "CF",
                                   FD->getIdentifier()->getName())) {
          StringRef fname = FD->getIdentifier()->getName();
          if (fname.endswith("Retain") ||
              fname.find("Create") != StringRef::npos ||
              fname.find("Copy") != StringRef::npos) {
            // Do not migrate to couple of bridge transfer casts which
            // cancel each other out. Leave it unchanged so error gets user
            // attention instead.
            if (FD->getName() == "CFRetain" &&
                FD->getNumParams() == 1 &&
                FD->getParent()->isTranslationUnit() &&
                FD->isExternallyVisible()) {
              Expr *Arg = callE->getArg(0);
              if (const ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(Arg)) {
                const Expr *sub = ICE->getSubExpr();
                QualType T = sub->getType();
                if (T->isObjCObjectPointerType())
                  return;
              }
            }
            castToObjCObject(E, /*retained=*/true);
            return;
          }

          if (fname.find("Get") != StringRef::npos) {
            castToObjCObject(E, /*retained=*/false);
            return;
          }
        }
      }
    }

    // If returning an ivar or a member of an ivar from a +0 method, use
    // a __bridge cast.
    Expr *base = inner->IgnoreParenImpCasts();
    while (isa<MemberExpr>(base))
      base = cast<MemberExpr>(base)->getBase()->IgnoreParenImpCasts();
    if (isa<ObjCIvarRefExpr>(base) &&
        isa<ReturnStmt>(StmtMap->getParentIgnoreParenCasts(E))) {
      if (ObjCMethodDecl *method = dyn_cast_or_null<ObjCMethodDecl>(ParentD)) {
        if (!method->hasAttr<NSReturnsRetainedAttr>()) {
          castToObjCObject(E, /*retained=*/false);
          return;
        }
      }
    }
  }

  void castToObjCObject(CastExpr *E, bool retained) {
    rewriteToBridgedCast(E, retained ? OBC_BridgeTransfer : OBC_Bridge);
  }

  void rewriteToBridgedCast(CastExpr *E, ObjCBridgeCastKind Kind) {
    Transaction Trans(Pass.TA);
    rewriteToBridgedCast(E, Kind, Trans);
  }

  void rewriteToBridgedCast(CastExpr *E, ObjCBridgeCastKind Kind,
                            Transaction &Trans) {
    TransformActions &TA = Pass.TA;

    // We will remove the compiler diagnostic.
    if (!TA.hasDiagnostic(diag::err_arc_mismatched_cast,
                          diag::err_arc_cast_requires_bridge,
                          E->getBeginLoc())) {
      Trans.abort();
      return;
    }

    StringRef bridge;
    switch(Kind) {
    case OBC_Bridge:
      bridge = "__bridge "; break;
    case OBC_BridgeTransfer:
      bridge = "__bridge_transfer "; break;
    case OBC_BridgeRetained:
      bridge = "__bridge_retained "; break;
    }

    TA.clearDiagnostic(diag::err_arc_mismatched_cast,
                       diag::err_arc_cast_requires_bridge, E->getBeginLoc());
    if (Kind == OBC_Bridge || !Pass.CFBridgingFunctionsDefined()) {
      if (CStyleCastExpr *CCE = dyn_cast<CStyleCastExpr>(E)) {
        TA.insertAfterToken(CCE->getLParenLoc(), bridge);
      } else {
        SourceLocation insertLoc = E->getSubExpr()->getBeginLoc();
        SmallString<128> newCast;
        newCast += '(';
        newCast += bridge;
        newCast += E->getType().getAsString(Pass.Ctx.getPrintingPolicy());
        newCast += ')';

        if (isa<ParenExpr>(E->getSubExpr())) {
          TA.insert(insertLoc, newCast.str());
        } else {
          newCast += '(';
          TA.insert(insertLoc, newCast.str());
          TA.insertAfterToken(E->getEndLoc(), ")");
        }
      }
    } else {
      assert(Kind == OBC_BridgeTransfer || Kind == OBC_BridgeRetained);
      SmallString<32> BridgeCall;

      Expr *WrapE = E->getSubExpr();
      SourceLocation InsertLoc = WrapE->getBeginLoc();

      SourceManager &SM = Pass.Ctx.getSourceManager();
      char PrevChar = *SM.getCharacterData(InsertLoc.getLocWithOffset(-1));
      if (Lexer::isIdentifierBodyChar(PrevChar, Pass.Ctx.getLangOpts()))
        BridgeCall += ' ';

      if (Kind == OBC_BridgeTransfer)
        BridgeCall += "CFBridgingRelease";
      else
        BridgeCall += "CFBridgingRetain";

      if (isa<ParenExpr>(WrapE)) {
        TA.insert(InsertLoc, BridgeCall);
      } else {
        BridgeCall += '(';
        TA.insert(InsertLoc, BridgeCall);
        TA.insertAfterToken(WrapE->getEndLoc(), ")");
      }
    }
  }

  void rewriteCastForCFRetain(CastExpr *castE, CallExpr *callE) {
    Transaction Trans(Pass.TA);
    Pass.TA.replace(callE->getSourceRange(), callE->getArg(0)->getSourceRange());
    rewriteToBridgedCast(castE, OBC_BridgeRetained, Trans);
  }

  void getBlockMacroRanges(CastExpr *E, SourceRange &Outer, SourceRange &Inner) {
    SourceManager &SM = Pass.Ctx.getSourceManager();
    SourceLocation Loc = E->getExprLoc();
    assert(Loc.isMacroID());
    CharSourceRange MacroRange = SM.getImmediateExpansionRange(Loc);
    SourceRange SubRange = E->getSubExpr()->IgnoreParenImpCasts()->getSourceRange();
    SourceLocation InnerBegin = SM.getImmediateMacroCallerLoc(SubRange.getBegin());
    SourceLocation InnerEnd = SM.getImmediateMacroCallerLoc(SubRange.getEnd());

    Outer = MacroRange.getAsRange();
    Inner = SourceRange(InnerBegin, InnerEnd);
  }

  void rewriteBlockCopyMacro(CastExpr *E) {
    SourceRange OuterRange, InnerRange;
    getBlockMacroRanges(E, OuterRange, InnerRange);

    Transaction Trans(Pass.TA);
    Pass.TA.replace(OuterRange, InnerRange);
    Pass.TA.insert(InnerRange.getBegin(), "[");
    Pass.TA.insertAfterToken(InnerRange.getEnd(), " copy]");
    Pass.TA.clearDiagnostic(diag::err_arc_mismatched_cast,
                            diag::err_arc_cast_requires_bridge,
                            OuterRange);
  }

  void removeBlockReleaseMacro(CastExpr *E) {
    SourceRange OuterRange, InnerRange;
    getBlockMacroRanges(E, OuterRange, InnerRange);

    Transaction Trans(Pass.TA);
    Pass.TA.clearDiagnostic(diag::err_arc_mismatched_cast,
                            diag::err_arc_cast_requires_bridge,
                            OuterRange);
    if (!hasSideEffects(E, Pass.Ctx)) {
      if (tryRemoving(cast<Expr>(StmtMap->getParentIgnoreParenCasts(E))))
        return;
    }
    Pass.TA.replace(OuterRange, InnerRange);
  }

  bool tryRemoving(Expr *E) const {
    if (!Removables) {
      Removables.reset(new ExprSet);
      collectRemovables(Body, *Removables);
    }

    if (Removables->count(E)) {
      Pass.TA.removeStmt(E);
      return true;
    }

    return false;
  }

  void transformObjCToNonObjCCast(CastExpr *E) {
    SourceLocation CastLoc = E->getExprLoc();
    if (CastLoc.isMacroID()) {
      StringRef MacroName = Lexer::getImmediateMacroName(CastLoc,
                                                    Pass.Ctx.getSourceManager(),
                                                    Pass.Ctx.getLangOpts());
      if (MacroName == "Block_copy") {
        rewriteBlockCopyMacro(E);
        return;
      }
      if (MacroName == "Block_release") {
        removeBlockReleaseMacro(E);
        return;
      }
    }

    if (isSelf(E->getSubExpr()))
      return rewriteToBridgedCast(E, OBC_Bridge);

    CallExpr *callE;
    if (isPassedToCFRetain(E, callE))
      return rewriteCastForCFRetain(E, callE);

    ObjCMethodFamily family = getFamilyOfMessage(E->getSubExpr());
    if (family == OMF_retain)
      return rewriteToBridgedCast(E, OBC_BridgeRetained);

    if (family == OMF_autorelease || family == OMF_release) {
      std::string err = "it is not safe to cast to '";
      err += E->getType().getAsString(Pass.Ctx.getPrintingPolicy());
      err += "' the result of '";
      err += family == OMF_autorelease ? "autorelease" : "release";
      err += "' message; a __bridge cast may result in a pointer to a "
          "destroyed object and a __bridge_retained may leak the object";
      Pass.TA.reportError(err, E->getBeginLoc(),
                          E->getSubExpr()->getSourceRange());
      Stmt *parent = E;
      do {
        parent = StmtMap->getParentIgnoreParenImpCasts(parent);
      } while (parent && isa<FullExpr>(parent));

      if (ReturnStmt *retS = dyn_cast_or_null<ReturnStmt>(parent)) {
        std::string note = "remove the cast and change return type of function "
            "to '";
        note += E->getSubExpr()->getType().getAsString(Pass.Ctx.getPrintingPolicy());
        note += "' to have the object automatically autoreleased";
        Pass.TA.reportNote(note, retS->getBeginLoc());
      }
    }

    Expr *subExpr = E->getSubExpr();

    // Look through pseudo-object expressions.
    if (PseudoObjectExpr *pseudo = dyn_cast<PseudoObjectExpr>(subExpr)) {
      subExpr = pseudo->getResultExpr();
      assert(subExpr && "no result for pseudo-object of non-void type?");
    }

    if (ImplicitCastExpr *implCE = dyn_cast<ImplicitCastExpr>(subExpr)) {
      if (implCE->getCastKind() == CK_ARCConsumeObject)
        return rewriteToBridgedCast(E, OBC_BridgeRetained);
      if (implCE->getCastKind() == CK_ARCReclaimReturnedObject)
        return rewriteToBridgedCast(E, OBC_Bridge);
    }

    bool isConsumed = false;
    if (isPassedToCParamWithKnownOwnership(E, isConsumed))
      return rewriteToBridgedCast(E, isConsumed ? OBC_BridgeRetained
                                                : OBC_Bridge);
  }

  static ObjCMethodFamily getFamilyOfMessage(Expr *E) {
    E = E->IgnoreParenCasts();
    if (ObjCMessageExpr *ME = dyn_cast<ObjCMessageExpr>(E))
      return ME->getMethodFamily();

    return OMF_None;
  }

  bool isPassedToCFRetain(Expr *E, CallExpr *&callE) const {
    if ((callE = dyn_cast_or_null<CallExpr>(
                                     StmtMap->getParentIgnoreParenImpCasts(E))))
      if (FunctionDecl *
            FD = dyn_cast_or_null<FunctionDecl>(callE->getCalleeDecl()))
        if (FD->getName() == "CFRetain" && FD->getNumParams() == 1 &&
            FD->getParent()->isTranslationUnit() &&
            FD->isExternallyVisible())
          return true;

    return false;
  }

  bool isPassedToCParamWithKnownOwnership(Expr *E, bool &isConsumed) const {
    if (CallExpr *callE = dyn_cast_or_null<CallExpr>(
                                     StmtMap->getParentIgnoreParenImpCasts(E)))
      if (FunctionDecl *
            FD = dyn_cast_or_null<FunctionDecl>(callE->getCalleeDecl())) {
        unsigned i = 0;
        for (unsigned e = callE->getNumArgs(); i != e; ++i) {
          Expr *arg = callE->getArg(i);
          if (arg == E || arg->IgnoreParenImpCasts() == E)
            break;
        }
        if (i < callE->getNumArgs() && i < FD->getNumParams()) {
          ParmVarDecl *PD = FD->getParamDecl(i);
          if (PD->hasAttr<CFConsumedAttr>()) {
            isConsumed = true;
            return true;
          }
        }
      }

    return false;
  }

  bool isSelf(Expr *E) const {
    E = E->IgnoreParenLValueCasts();
    if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E))
      if (ImplicitParamDecl *IPD = dyn_cast<ImplicitParamDecl>(DRE->getDecl()))
        if (IPD->getIdentifier() == SelfII)
          return true;

    return false;
  }
};

} // end anonymous namespace

void trans::rewriteUnbridgedCasts(MigrationPass &pass) {
  BodyTransform<UnbridgedCastRewriter> trans(pass);
  trans.TraverseDecl(pass.Ctx.getTranslationUnitDecl());
}
