//===- ThreadSafetyCommon.cpp ---------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implementation of the interfaces declared in ThreadSafetyCommon.h
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/Analyses/ThreadSafetyCommon.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclGroup.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/Analysis/Analyses/ThreadSafetyTIL.h"
#include "clang/Analysis/CFG.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/Specifiers.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include <algorithm>
#include <cassert>
#include <string>
#include <utility>

using namespace clang;
using namespace threadSafety;

// From ThreadSafetyUtil.h
std::string threadSafety::getSourceLiteralString(const Expr *CE) {
  switch (CE->getStmtClass()) {
    case Stmt::IntegerLiteralClass:
      return cast<IntegerLiteral>(CE)->getValue().toString(10, true);
    case Stmt::StringLiteralClass: {
      std::string ret("\"");
      ret += cast<StringLiteral>(CE)->getString();
      ret += "\"";
      return ret;
    }
    case Stmt::CharacterLiteralClass:
    case Stmt::CXXNullPtrLiteralExprClass:
    case Stmt::GNUNullExprClass:
    case Stmt::CXXBoolLiteralExprClass:
    case Stmt::FloatingLiteralClass:
    case Stmt::ImaginaryLiteralClass:
    case Stmt::ObjCStringLiteralClass:
    default:
      return "#lit";
  }
}

// Return true if E is a variable that points to an incomplete Phi node.
static bool isIncompletePhi(const til::SExpr *E) {
  if (const auto *Ph = dyn_cast<til::Phi>(E))
    return Ph->status() == til::Phi::PH_Incomplete;
  return false;
}

using CallingContext = SExprBuilder::CallingContext;

til::SExpr *SExprBuilder::lookupStmt(const Stmt *S) {
  auto It = SMap.find(S);
  if (It != SMap.end())
    return It->second;
  return nullptr;
}

til::SCFG *SExprBuilder::buildCFG(CFGWalker &Walker) {
  Walker.walk(*this);
  return Scfg;
}

static bool isCalleeArrow(const Expr *E) {
  const auto *ME = dyn_cast<MemberExpr>(E->IgnoreParenCasts());
  return ME ? ME->isArrow() : false;
}

/// Translate a clang expression in an attribute to a til::SExpr.
/// Constructs the context from D, DeclExp, and SelfDecl.
///
/// \param AttrExp The expression to translate.
/// \param D       The declaration to which the attribute is attached.
/// \param DeclExp An expression involving the Decl to which the attribute
///                is attached.  E.g. the call to a function.
CapabilityExpr SExprBuilder::translateAttrExpr(const Expr *AttrExp,
                                               const NamedDecl *D,
                                               const Expr *DeclExp,
                                               VarDecl *SelfDecl) {
  // If we are processing a raw attribute expression, with no substitutions.
  if (!DeclExp)
    return translateAttrExpr(AttrExp, nullptr);

  CallingContext Ctx(nullptr, D);

  // Examine DeclExp to find SelfArg and FunArgs, which are used to substitute
  // for formal parameters when we call buildMutexID later.
  if (const auto *ME = dyn_cast<MemberExpr>(DeclExp)) {
    Ctx.SelfArg   = ME->getBase();
    Ctx.SelfArrow = ME->isArrow();
  } else if (const auto *CE = dyn_cast<CXXMemberCallExpr>(DeclExp)) {
    Ctx.SelfArg   = CE->getImplicitObjectArgument();
    Ctx.SelfArrow = isCalleeArrow(CE->getCallee());
    Ctx.NumArgs   = CE->getNumArgs();
    Ctx.FunArgs   = CE->getArgs();
  } else if (const auto *CE = dyn_cast<CallExpr>(DeclExp)) {
    Ctx.NumArgs = CE->getNumArgs();
    Ctx.FunArgs = CE->getArgs();
  } else if (const auto *CE = dyn_cast<CXXConstructExpr>(DeclExp)) {
    Ctx.SelfArg = nullptr;  // Will be set below
    Ctx.NumArgs = CE->getNumArgs();
    Ctx.FunArgs = CE->getArgs();
  } else if (D && isa<CXXDestructorDecl>(D)) {
    // There's no such thing as a "destructor call" in the AST.
    Ctx.SelfArg = DeclExp;
  }

  // Hack to handle constructors, where self cannot be recovered from
  // the expression.
  if (SelfDecl && !Ctx.SelfArg) {
    DeclRefExpr SelfDRE(SelfDecl->getASTContext(), SelfDecl, false,
                        SelfDecl->getType(), VK_LValue,
                        SelfDecl->getLocation());
    Ctx.SelfArg = &SelfDRE;

    // If the attribute has no arguments, then assume the argument is "this".
    if (!AttrExp)
      return translateAttrExpr(Ctx.SelfArg, nullptr);
    else  // For most attributes.
      return translateAttrExpr(AttrExp, &Ctx);
  }

  // If the attribute has no arguments, then assume the argument is "this".
  if (!AttrExp)
    return translateAttrExpr(Ctx.SelfArg, nullptr);
  else  // For most attributes.
    return translateAttrExpr(AttrExp, &Ctx);
}

/// Translate a clang expression in an attribute to a til::SExpr.
// This assumes a CallingContext has already been created.
CapabilityExpr SExprBuilder::translateAttrExpr(const Expr *AttrExp,
                                               CallingContext *Ctx) {
  if (!AttrExp)
    return CapabilityExpr(nullptr, false);

  if (const auto* SLit = dyn_cast<StringLiteral>(AttrExp)) {
    if (SLit->getString() == StringRef("*"))
      // The "*" expr is a universal lock, which essentially turns off
      // checks until it is removed from the lockset.
      return CapabilityExpr(new (Arena) til::Wildcard(), false);
    else
      // Ignore other string literals for now.
      return CapabilityExpr(nullptr, false);
  }

  bool Neg = false;
  if (const auto *OE = dyn_cast<CXXOperatorCallExpr>(AttrExp)) {
    if (OE->getOperator() == OO_Exclaim) {
      Neg = true;
      AttrExp = OE->getArg(0);
    }
  }
  else if (const auto *UO = dyn_cast<UnaryOperator>(AttrExp)) {
    if (UO->getOpcode() == UO_LNot) {
      Neg = true;
      AttrExp = UO->getSubExpr();
    }
  }

  til::SExpr *E = translate(AttrExp, Ctx);

  // Trap mutex expressions like nullptr, or 0.
  // Any literal value is nonsense.
  if (!E || isa<til::Literal>(E))
    return CapabilityExpr(nullptr, false);

  // Hack to deal with smart pointers -- strip off top-level pointer casts.
  if (const auto *CE = dyn_cast_or_null<til::Cast>(E)) {
    if (CE->castOpcode() == til::CAST_objToPtr)
      return CapabilityExpr(CE->expr(), Neg);
  }
  return CapabilityExpr(E, Neg);
}

// Translate a clang statement or expression to a TIL expression.
// Also performs substitution of variables; Ctx provides the context.
// Dispatches on the type of S.
til::SExpr *SExprBuilder::translate(const Stmt *S, CallingContext *Ctx) {
  if (!S)
    return nullptr;

  // Check if S has already been translated and cached.
  // This handles the lookup of SSA names for DeclRefExprs here.
  if (til::SExpr *E = lookupStmt(S))
    return E;

  switch (S->getStmtClass()) {
  case Stmt::DeclRefExprClass:
    return translateDeclRefExpr(cast<DeclRefExpr>(S), Ctx);
  case Stmt::CXXThisExprClass:
    return translateCXXThisExpr(cast<CXXThisExpr>(S), Ctx);
  case Stmt::MemberExprClass:
    return translateMemberExpr(cast<MemberExpr>(S), Ctx);
  case Stmt::ObjCIvarRefExprClass:
    return translateObjCIVarRefExpr(cast<ObjCIvarRefExpr>(S), Ctx);
  case Stmt::CallExprClass:
    return translateCallExpr(cast<CallExpr>(S), Ctx);
  case Stmt::CXXMemberCallExprClass:
    return translateCXXMemberCallExpr(cast<CXXMemberCallExpr>(S), Ctx);
  case Stmt::CXXOperatorCallExprClass:
    return translateCXXOperatorCallExpr(cast<CXXOperatorCallExpr>(S), Ctx);
  case Stmt::UnaryOperatorClass:
    return translateUnaryOperator(cast<UnaryOperator>(S), Ctx);
  case Stmt::BinaryOperatorClass:
  case Stmt::CompoundAssignOperatorClass:
    return translateBinaryOperator(cast<BinaryOperator>(S), Ctx);

  case Stmt::ArraySubscriptExprClass:
    return translateArraySubscriptExpr(cast<ArraySubscriptExpr>(S), Ctx);
  case Stmt::ConditionalOperatorClass:
    return translateAbstractConditionalOperator(
             cast<ConditionalOperator>(S), Ctx);
  case Stmt::BinaryConditionalOperatorClass:
    return translateAbstractConditionalOperator(
             cast<BinaryConditionalOperator>(S), Ctx);

  // We treat these as no-ops
  case Stmt::ConstantExprClass:
    return translate(cast<ConstantExpr>(S)->getSubExpr(), Ctx);
  case Stmt::ParenExprClass:
    return translate(cast<ParenExpr>(S)->getSubExpr(), Ctx);
  case Stmt::ExprWithCleanupsClass:
    return translate(cast<ExprWithCleanups>(S)->getSubExpr(), Ctx);
  case Stmt::CXXBindTemporaryExprClass:
    return translate(cast<CXXBindTemporaryExpr>(S)->getSubExpr(), Ctx);
  case Stmt::MaterializeTemporaryExprClass:
    return translate(cast<MaterializeTemporaryExpr>(S)->GetTemporaryExpr(),
                     Ctx);

  // Collect all literals
  case Stmt::CharacterLiteralClass:
  case Stmt::CXXNullPtrLiteralExprClass:
  case Stmt::GNUNullExprClass:
  case Stmt::CXXBoolLiteralExprClass:
  case Stmt::FloatingLiteralClass:
  case Stmt::ImaginaryLiteralClass:
  case Stmt::IntegerLiteralClass:
  case Stmt::StringLiteralClass:
  case Stmt::ObjCStringLiteralClass:
    return new (Arena) til::Literal(cast<Expr>(S));

  case Stmt::DeclStmtClass:
    return translateDeclStmt(cast<DeclStmt>(S), Ctx);
  default:
    break;
  }
  if (const auto *CE = dyn_cast<CastExpr>(S))
    return translateCastExpr(CE, Ctx);

  return new (Arena) til::Undefined(S);
}

til::SExpr *SExprBuilder::translateDeclRefExpr(const DeclRefExpr *DRE,
                                               CallingContext *Ctx) {
  const auto *VD = cast<ValueDecl>(DRE->getDecl()->getCanonicalDecl());

  // Function parameters require substitution and/or renaming.
  if (const auto *PV = dyn_cast_or_null<ParmVarDecl>(VD)) {
    const auto *FD =
        cast<FunctionDecl>(PV->getDeclContext())->getCanonicalDecl();
    unsigned I = PV->getFunctionScopeIndex();

    if (Ctx && Ctx->FunArgs && FD == Ctx->AttrDecl->getCanonicalDecl()) {
      // Substitute call arguments for references to function parameters
      assert(I < Ctx->NumArgs);
      return translate(Ctx->FunArgs[I], Ctx->Prev);
    }
    // Map the param back to the param of the original function declaration
    // for consistent comparisons.
    VD = FD->getParamDecl(I);
  }

  // For non-local variables, treat it as a reference to a named object.
  return new (Arena) til::LiteralPtr(VD);
}

til::SExpr *SExprBuilder::translateCXXThisExpr(const CXXThisExpr *TE,
                                               CallingContext *Ctx) {
  // Substitute for 'this'
  if (Ctx && Ctx->SelfArg)
    return translate(Ctx->SelfArg, Ctx->Prev);
  assert(SelfVar && "We have no variable for 'this'!");
  return SelfVar;
}

static const ValueDecl *getValueDeclFromSExpr(const til::SExpr *E) {
  if (const auto *V = dyn_cast<til::Variable>(E))
    return V->clangDecl();
  if (const auto *Ph = dyn_cast<til::Phi>(E))
    return Ph->clangDecl();
  if (const auto *P = dyn_cast<til::Project>(E))
    return P->clangDecl();
  if (const auto *L = dyn_cast<til::LiteralPtr>(E))
    return L->clangDecl();
  return nullptr;
}

static bool hasAnyPointerType(const til::SExpr *E) {
  auto *VD = getValueDeclFromSExpr(E);
  if (VD && VD->getType()->isAnyPointerType())
    return true;
  if (const auto *C = dyn_cast<til::Cast>(E))
    return C->castOpcode() == til::CAST_objToPtr;

  return false;
}

// Grab the very first declaration of virtual method D
static const CXXMethodDecl *getFirstVirtualDecl(const CXXMethodDecl *D) {
  while (true) {
    D = D->getCanonicalDecl();
    auto OverriddenMethods = D->overridden_methods();
    if (OverriddenMethods.begin() == OverriddenMethods.end())
      return D;  // Method does not override anything
    // FIXME: this does not work with multiple inheritance.
    D = *OverriddenMethods.begin();
  }
  return nullptr;
}

til::SExpr *SExprBuilder::translateMemberExpr(const MemberExpr *ME,
                                              CallingContext *Ctx) {
  til::SExpr *BE = translate(ME->getBase(), Ctx);
  til::SExpr *E  = new (Arena) til::SApply(BE);

  const auto *D = cast<ValueDecl>(ME->getMemberDecl()->getCanonicalDecl());
  if (const auto *VD = dyn_cast<CXXMethodDecl>(D))
    D = getFirstVirtualDecl(VD);

  til::Project *P = new (Arena) til::Project(E, D);
  if (hasAnyPointerType(BE))
    P->setArrow(true);
  return P;
}

til::SExpr *SExprBuilder::translateObjCIVarRefExpr(const ObjCIvarRefExpr *IVRE,
                                                   CallingContext *Ctx) {
  til::SExpr *BE = translate(IVRE->getBase(), Ctx);
  til::SExpr *E = new (Arena) til::SApply(BE);

  const auto *D = cast<ObjCIvarDecl>(IVRE->getDecl()->getCanonicalDecl());

  til::Project *P = new (Arena) til::Project(E, D);
  if (hasAnyPointerType(BE))
    P->setArrow(true);
  return P;
}

til::SExpr *SExprBuilder::translateCallExpr(const CallExpr *CE,
                                            CallingContext *Ctx,
                                            const Expr *SelfE) {
  if (CapabilityExprMode) {
    // Handle LOCK_RETURNED
    if (const FunctionDecl *FD = CE->getDirectCallee()) {
      FD = FD->getMostRecentDecl();
      if (LockReturnedAttr *At = FD->getAttr<LockReturnedAttr>()) {
        CallingContext LRCallCtx(Ctx);
        LRCallCtx.AttrDecl = CE->getDirectCallee();
        LRCallCtx.SelfArg = SelfE;
        LRCallCtx.NumArgs = CE->getNumArgs();
        LRCallCtx.FunArgs = CE->getArgs();
        return const_cast<til::SExpr *>(
            translateAttrExpr(At->getArg(), &LRCallCtx).sexpr());
      }
    }
  }

  til::SExpr *E = translate(CE->getCallee(), Ctx);
  for (const auto *Arg : CE->arguments()) {
    til::SExpr *A = translate(Arg, Ctx);
    E = new (Arena) til::Apply(E, A);
  }
  return new (Arena) til::Call(E, CE);
}

til::SExpr *SExprBuilder::translateCXXMemberCallExpr(
    const CXXMemberCallExpr *ME, CallingContext *Ctx) {
  if (CapabilityExprMode) {
    // Ignore calls to get() on smart pointers.
    if (ME->getMethodDecl()->getNameAsString() == "get" &&
        ME->getNumArgs() == 0) {
      auto *E = translate(ME->getImplicitObjectArgument(), Ctx);
      return new (Arena) til::Cast(til::CAST_objToPtr, E);
      // return E;
    }
  }
  return translateCallExpr(cast<CallExpr>(ME), Ctx,
                           ME->getImplicitObjectArgument());
}

til::SExpr *SExprBuilder::translateCXXOperatorCallExpr(
    const CXXOperatorCallExpr *OCE, CallingContext *Ctx) {
  if (CapabilityExprMode) {
    // Ignore operator * and operator -> on smart pointers.
    OverloadedOperatorKind k = OCE->getOperator();
    if (k == OO_Star || k == OO_Arrow) {
      auto *E = translate(OCE->getArg(0), Ctx);
      return new (Arena) til::Cast(til::CAST_objToPtr, E);
      // return E;
    }
  }
  return translateCallExpr(cast<CallExpr>(OCE), Ctx);
}

til::SExpr *SExprBuilder::translateUnaryOperator(const UnaryOperator *UO,
                                                 CallingContext *Ctx) {
  switch (UO->getOpcode()) {
  case UO_PostInc:
  case UO_PostDec:
  case UO_PreInc:
  case UO_PreDec:
    return new (Arena) til::Undefined(UO);

  case UO_AddrOf:
    if (CapabilityExprMode) {
      // interpret &Graph::mu_ as an existential.
      if (const auto *DRE = dyn_cast<DeclRefExpr>(UO->getSubExpr())) {
        if (DRE->getDecl()->isCXXInstanceMember()) {
          // This is a pointer-to-member expression, e.g. &MyClass::mu_.
          // We interpret this syntax specially, as a wildcard.
          auto *W = new (Arena) til::Wildcard();
          return new (Arena) til::Project(W, DRE->getDecl());
        }
      }
    }
    // otherwise, & is a no-op
    return translate(UO->getSubExpr(), Ctx);

  // We treat these as no-ops
  case UO_Deref:
  case UO_Plus:
    return translate(UO->getSubExpr(), Ctx);

  case UO_Minus:
    return new (Arena)
      til::UnaryOp(til::UOP_Minus, translate(UO->getSubExpr(), Ctx));
  case UO_Not:
    return new (Arena)
      til::UnaryOp(til::UOP_BitNot, translate(UO->getSubExpr(), Ctx));
  case UO_LNot:
    return new (Arena)
      til::UnaryOp(til::UOP_LogicNot, translate(UO->getSubExpr(), Ctx));

  // Currently unsupported
  case UO_Real:
  case UO_Imag:
  case UO_Extension:
  case UO_Coawait:
    return new (Arena) til::Undefined(UO);
  }
  return new (Arena) til::Undefined(UO);
}

til::SExpr *SExprBuilder::translateBinOp(til::TIL_BinaryOpcode Op,
                                         const BinaryOperator *BO,
                                         CallingContext *Ctx, bool Reverse) {
   til::SExpr *E0 = translate(BO->getLHS(), Ctx);
   til::SExpr *E1 = translate(BO->getRHS(), Ctx);
   if (Reverse)
     return new (Arena) til::BinaryOp(Op, E1, E0);
   else
     return new (Arena) til::BinaryOp(Op, E0, E1);
}

til::SExpr *SExprBuilder::translateBinAssign(til::TIL_BinaryOpcode Op,
                                             const BinaryOperator *BO,
                                             CallingContext *Ctx,
                                             bool Assign) {
  const Expr *LHS = BO->getLHS();
  const Expr *RHS = BO->getRHS();
  til::SExpr *E0 = translate(LHS, Ctx);
  til::SExpr *E1 = translate(RHS, Ctx);

  const ValueDecl *VD = nullptr;
  til::SExpr *CV = nullptr;
  if (const auto *DRE = dyn_cast<DeclRefExpr>(LHS)) {
    VD = DRE->getDecl();
    CV = lookupVarDecl(VD);
  }

  if (!Assign) {
    til::SExpr *Arg = CV ? CV : new (Arena) til::Load(E0);
    E1 = new (Arena) til::BinaryOp(Op, Arg, E1);
    E1 = addStatement(E1, nullptr, VD);
  }
  if (VD && CV)
    return updateVarDecl(VD, E1);
  return new (Arena) til::Store(E0, E1);
}

til::SExpr *SExprBuilder::translateBinaryOperator(const BinaryOperator *BO,
                                                  CallingContext *Ctx) {
  switch (BO->getOpcode()) {
  case BO_PtrMemD:
  case BO_PtrMemI:
    return new (Arena) til::Undefined(BO);

  case BO_Mul:  return translateBinOp(til::BOP_Mul, BO, Ctx);
  case BO_Div:  return translateBinOp(til::BOP_Div, BO, Ctx);
  case BO_Rem:  return translateBinOp(til::BOP_Rem, BO, Ctx);
  case BO_Add:  return translateBinOp(til::BOP_Add, BO, Ctx);
  case BO_Sub:  return translateBinOp(til::BOP_Sub, BO, Ctx);
  case BO_Shl:  return translateBinOp(til::BOP_Shl, BO, Ctx);
  case BO_Shr:  return translateBinOp(til::BOP_Shr, BO, Ctx);
  case BO_LT:   return translateBinOp(til::BOP_Lt,  BO, Ctx);
  case BO_GT:   return translateBinOp(til::BOP_Lt,  BO, Ctx, true);
  case BO_LE:   return translateBinOp(til::BOP_Leq, BO, Ctx);
  case BO_GE:   return translateBinOp(til::BOP_Leq, BO, Ctx, true);
  case BO_EQ:   return translateBinOp(til::BOP_Eq,  BO, Ctx);
  case BO_NE:   return translateBinOp(til::BOP_Neq, BO, Ctx);
  case BO_Cmp:  return translateBinOp(til::BOP_Cmp, BO, Ctx);
  case BO_And:  return translateBinOp(til::BOP_BitAnd,   BO, Ctx);
  case BO_Xor:  return translateBinOp(til::BOP_BitXor,   BO, Ctx);
  case BO_Or:   return translateBinOp(til::BOP_BitOr,    BO, Ctx);
  case BO_LAnd: return translateBinOp(til::BOP_LogicAnd, BO, Ctx);
  case BO_LOr:  return translateBinOp(til::BOP_LogicOr,  BO, Ctx);

  case BO_Assign:    return translateBinAssign(til::BOP_Eq,  BO, Ctx, true);
  case BO_MulAssign: return translateBinAssign(til::BOP_Mul, BO, Ctx);
  case BO_DivAssign: return translateBinAssign(til::BOP_Div, BO, Ctx);
  case BO_RemAssign: return translateBinAssign(til::BOP_Rem, BO, Ctx);
  case BO_AddAssign: return translateBinAssign(til::BOP_Add, BO, Ctx);
  case BO_SubAssign: return translateBinAssign(til::BOP_Sub, BO, Ctx);
  case BO_ShlAssign: return translateBinAssign(til::BOP_Shl, BO, Ctx);
  case BO_ShrAssign: return translateBinAssign(til::BOP_Shr, BO, Ctx);
  case BO_AndAssign: return translateBinAssign(til::BOP_BitAnd, BO, Ctx);
  case BO_XorAssign: return translateBinAssign(til::BOP_BitXor, BO, Ctx);
  case BO_OrAssign:  return translateBinAssign(til::BOP_BitOr,  BO, Ctx);

  case BO_Comma:
    // The clang CFG should have already processed both sides.
    return translate(BO->getRHS(), Ctx);
  }
  return new (Arena) til::Undefined(BO);
}

til::SExpr *SExprBuilder::translateCastExpr(const CastExpr *CE,
                                            CallingContext *Ctx) {
  CastKind K = CE->getCastKind();
  switch (K) {
  case CK_LValueToRValue: {
    if (const auto *DRE = dyn_cast<DeclRefExpr>(CE->getSubExpr())) {
      til::SExpr *E0 = lookupVarDecl(DRE->getDecl());
      if (E0)
        return E0;
    }
    til::SExpr *E0 = translate(CE->getSubExpr(), Ctx);
    return E0;
    // FIXME!! -- get Load working properly
    // return new (Arena) til::Load(E0);
  }
  case CK_NoOp:
  case CK_DerivedToBase:
  case CK_UncheckedDerivedToBase:
  case CK_ArrayToPointerDecay:
  case CK_FunctionToPointerDecay: {
    til::SExpr *E0 = translate(CE->getSubExpr(), Ctx);
    return E0;
  }
  default: {
    // FIXME: handle different kinds of casts.
    til::SExpr *E0 = translate(CE->getSubExpr(), Ctx);
    if (CapabilityExprMode)
      return E0;
    return new (Arena) til::Cast(til::CAST_none, E0);
  }
  }
}

til::SExpr *
SExprBuilder::translateArraySubscriptExpr(const ArraySubscriptExpr *E,
                                          CallingContext *Ctx) {
  til::SExpr *E0 = translate(E->getBase(), Ctx);
  til::SExpr *E1 = translate(E->getIdx(), Ctx);
  return new (Arena) til::ArrayIndex(E0, E1);
}

til::SExpr *
SExprBuilder::translateAbstractConditionalOperator(
    const AbstractConditionalOperator *CO, CallingContext *Ctx) {
  auto *C = translate(CO->getCond(), Ctx);
  auto *T = translate(CO->getTrueExpr(), Ctx);
  auto *E = translate(CO->getFalseExpr(), Ctx);
  return new (Arena) til::IfThenElse(C, T, E);
}

til::SExpr *
SExprBuilder::translateDeclStmt(const DeclStmt *S, CallingContext *Ctx) {
  DeclGroupRef DGrp = S->getDeclGroup();
  for (auto I : DGrp) {
    if (auto *VD = dyn_cast_or_null<VarDecl>(I)) {
      Expr *E = VD->getInit();
      til::SExpr* SE = translate(E, Ctx);

      // Add local variables with trivial type to the variable map
      QualType T = VD->getType();
      if (T.isTrivialType(VD->getASTContext()))
        return addVarDecl(VD, SE);
      else {
        // TODO: add alloca
      }
    }
  }
  return nullptr;
}

// If (E) is non-trivial, then add it to the current basic block, and
// update the statement map so that S refers to E.  Returns a new variable
// that refers to E.
// If E is trivial returns E.
til::SExpr *SExprBuilder::addStatement(til::SExpr* E, const Stmt *S,
                                       const ValueDecl *VD) {
  if (!E || !CurrentBB || E->block() || til::ThreadSafetyTIL::isTrivial(E))
    return E;
  if (VD)
    E = new (Arena) til::Variable(E, VD);
  CurrentInstructions.push_back(E);
  if (S)
    insertStmt(S, E);
  return E;
}

// Returns the current value of VD, if known, and nullptr otherwise.
til::SExpr *SExprBuilder::lookupVarDecl(const ValueDecl *VD) {
  auto It = LVarIdxMap.find(VD);
  if (It != LVarIdxMap.end()) {
    assert(CurrentLVarMap[It->second].first == VD);
    return CurrentLVarMap[It->second].second;
  }
  return nullptr;
}

// if E is a til::Variable, update its clangDecl.
static void maybeUpdateVD(til::SExpr *E, const ValueDecl *VD) {
  if (!E)
    return;
  if (auto *V = dyn_cast<til::Variable>(E)) {
    if (!V->clangDecl())
      V->setClangDecl(VD);
  }
}

// Adds a new variable declaration.
til::SExpr *SExprBuilder::addVarDecl(const ValueDecl *VD, til::SExpr *E) {
  maybeUpdateVD(E, VD);
  LVarIdxMap.insert(std::make_pair(VD, CurrentLVarMap.size()));
  CurrentLVarMap.makeWritable();
  CurrentLVarMap.push_back(std::make_pair(VD, E));
  return E;
}

// Updates a current variable declaration.  (E.g. by assignment)
til::SExpr *SExprBuilder::updateVarDecl(const ValueDecl *VD, til::SExpr *E) {
  maybeUpdateVD(E, VD);
  auto It = LVarIdxMap.find(VD);
  if (It == LVarIdxMap.end()) {
    til::SExpr *Ptr = new (Arena) til::LiteralPtr(VD);
    til::SExpr *St  = new (Arena) til::Store(Ptr, E);
    return St;
  }
  CurrentLVarMap.makeWritable();
  CurrentLVarMap.elem(It->second).second = E;
  return E;
}

// Make a Phi node in the current block for the i^th variable in CurrentVarMap.
// If E != null, sets Phi[CurrentBlockInfo->ArgIndex] = E.
// If E == null, this is a backedge and will be set later.
void SExprBuilder::makePhiNodeVar(unsigned i, unsigned NPreds, til::SExpr *E) {
  unsigned ArgIndex = CurrentBlockInfo->ProcessedPredecessors;
  assert(ArgIndex > 0 && ArgIndex < NPreds);

  til::SExpr *CurrE = CurrentLVarMap[i].second;
  if (CurrE->block() == CurrentBB) {
    // We already have a Phi node in the current block,
    // so just add the new variable to the Phi node.
    auto *Ph = dyn_cast<til::Phi>(CurrE);
    assert(Ph && "Expecting Phi node.");
    if (E)
      Ph->values()[ArgIndex] = E;
    return;
  }

  // Make a new phi node: phi(..., E)
  // All phi args up to the current index are set to the current value.
  til::Phi *Ph = new (Arena) til::Phi(Arena, NPreds);
  Ph->values().setValues(NPreds, nullptr);
  for (unsigned PIdx = 0; PIdx < ArgIndex; ++PIdx)
    Ph->values()[PIdx] = CurrE;
  if (E)
    Ph->values()[ArgIndex] = E;
  Ph->setClangDecl(CurrentLVarMap[i].first);
  // If E is from a back-edge, or either E or CurrE are incomplete, then
  // mark this node as incomplete; we may need to remove it later.
  if (!E || isIncompletePhi(E) || isIncompletePhi(CurrE))
    Ph->setStatus(til::Phi::PH_Incomplete);

  // Add Phi node to current block, and update CurrentLVarMap[i]
  CurrentArguments.push_back(Ph);
  if (Ph->status() == til::Phi::PH_Incomplete)
    IncompleteArgs.push_back(Ph);

  CurrentLVarMap.makeWritable();
  CurrentLVarMap.elem(i).second = Ph;
}

// Merge values from Map into the current variable map.
// This will construct Phi nodes in the current basic block as necessary.
void SExprBuilder::mergeEntryMap(LVarDefinitionMap Map) {
  assert(CurrentBlockInfo && "Not processing a block!");

  if (!CurrentLVarMap.valid()) {
    // Steal Map, using copy-on-write.
    CurrentLVarMap = std::move(Map);
    return;
  }
  if (CurrentLVarMap.sameAs(Map))
    return;  // Easy merge: maps from different predecessors are unchanged.

  unsigned NPreds = CurrentBB->numPredecessors();
  unsigned ESz = CurrentLVarMap.size();
  unsigned MSz = Map.size();
  unsigned Sz  = std::min(ESz, MSz);

  for (unsigned i = 0; i < Sz; ++i) {
    if (CurrentLVarMap[i].first != Map[i].first) {
      // We've reached the end of variables in common.
      CurrentLVarMap.makeWritable();
      CurrentLVarMap.downsize(i);
      break;
    }
    if (CurrentLVarMap[i].second != Map[i].second)
      makePhiNodeVar(i, NPreds, Map[i].second);
  }
  if (ESz > MSz) {
    CurrentLVarMap.makeWritable();
    CurrentLVarMap.downsize(Map.size());
  }
}

// Merge a back edge into the current variable map.
// This will create phi nodes for all variables in the variable map.
void SExprBuilder::mergeEntryMapBackEdge() {
  // We don't have definitions for variables on the backedge, because we
  // haven't gotten that far in the CFG.  Thus, when encountering a back edge,
  // we conservatively create Phi nodes for all variables.  Unnecessary Phi
  // nodes will be marked as incomplete, and stripped out at the end.
  //
  // An Phi node is unnecessary if it only refers to itself and one other
  // variable, e.g. x = Phi(y, y, x)  can be reduced to x = y.

  assert(CurrentBlockInfo && "Not processing a block!");

  if (CurrentBlockInfo->HasBackEdges)
    return;
  CurrentBlockInfo->HasBackEdges = true;

  CurrentLVarMap.makeWritable();
  unsigned Sz = CurrentLVarMap.size();
  unsigned NPreds = CurrentBB->numPredecessors();

  for (unsigned i = 0; i < Sz; ++i)
    makePhiNodeVar(i, NPreds, nullptr);
}

// Update the phi nodes that were initially created for a back edge
// once the variable definitions have been computed.
// I.e., merge the current variable map into the phi nodes for Blk.
void SExprBuilder::mergePhiNodesBackEdge(const CFGBlock *Blk) {
  til::BasicBlock *BB = lookupBlock(Blk);
  unsigned ArgIndex = BBInfo[Blk->getBlockID()].ProcessedPredecessors;
  assert(ArgIndex > 0 && ArgIndex < BB->numPredecessors());

  for (til::SExpr *PE : BB->arguments()) {
    auto *Ph = dyn_cast_or_null<til::Phi>(PE);
    assert(Ph && "Expecting Phi Node.");
    assert(Ph->values()[ArgIndex] == nullptr && "Wrong index for back edge.");

    til::SExpr *E = lookupVarDecl(Ph->clangDecl());
    assert(E && "Couldn't find local variable for Phi node.");
    Ph->values()[ArgIndex] = E;
  }
}

void SExprBuilder::enterCFG(CFG *Cfg, const NamedDecl *D,
                            const CFGBlock *First) {
  // Perform initial setup operations.
  unsigned NBlocks = Cfg->getNumBlockIDs();
  Scfg = new (Arena) til::SCFG(Arena, NBlocks);

  // allocate all basic blocks immediately, to handle forward references.
  BBInfo.resize(NBlocks);
  BlockMap.resize(NBlocks, nullptr);
  // create map from clang blockID to til::BasicBlocks
  for (auto *B : *Cfg) {
    auto *BB = new (Arena) til::BasicBlock(Arena);
    BB->reserveInstructions(B->size());
    BlockMap[B->getBlockID()] = BB;
  }

  CurrentBB = lookupBlock(&Cfg->getEntry());
  auto Parms = isa<ObjCMethodDecl>(D) ? cast<ObjCMethodDecl>(D)->parameters()
                                      : cast<FunctionDecl>(D)->parameters();
  for (auto *Pm : Parms) {
    QualType T = Pm->getType();
    if (!T.isTrivialType(Pm->getASTContext()))
      continue;

    // Add parameters to local variable map.
    // FIXME: right now we emulate params with loads; that should be fixed.
    til::SExpr *Lp = new (Arena) til::LiteralPtr(Pm);
    til::SExpr *Ld = new (Arena) til::Load(Lp);
    til::SExpr *V  = addStatement(Ld, nullptr, Pm);
    addVarDecl(Pm, V);
  }
}

void SExprBuilder::enterCFGBlock(const CFGBlock *B) {
  // Initialize TIL basic block and add it to the CFG.
  CurrentBB = lookupBlock(B);
  CurrentBB->reservePredecessors(B->pred_size());
  Scfg->add(CurrentBB);

  CurrentBlockInfo = &BBInfo[B->getBlockID()];

  // CurrentLVarMap is moved to ExitMap on block exit.
  // FIXME: the entry block will hold function parameters.
  // assert(!CurrentLVarMap.valid() && "CurrentLVarMap already initialized.");
}

void SExprBuilder::handlePredecessor(const CFGBlock *Pred) {
  // Compute CurrentLVarMap on entry from ExitMaps of predecessors

  CurrentBB->addPredecessor(BlockMap[Pred->getBlockID()]);
  BlockInfo *PredInfo = &BBInfo[Pred->getBlockID()];
  assert(PredInfo->UnprocessedSuccessors > 0);

  if (--PredInfo->UnprocessedSuccessors == 0)
    mergeEntryMap(std::move(PredInfo->ExitMap));
  else
    mergeEntryMap(PredInfo->ExitMap.clone());

  ++CurrentBlockInfo->ProcessedPredecessors;
}

void SExprBuilder::handlePredecessorBackEdge(const CFGBlock *Pred) {
  mergeEntryMapBackEdge();
}

void SExprBuilder::enterCFGBlockBody(const CFGBlock *B) {
  // The merge*() methods have created arguments.
  // Push those arguments onto the basic block.
  CurrentBB->arguments().reserve(
    static_cast<unsigned>(CurrentArguments.size()), Arena);
  for (auto *A : CurrentArguments)
    CurrentBB->addArgument(A);
}

void SExprBuilder::handleStatement(const Stmt *S) {
  til::SExpr *E = translate(S, nullptr);
  addStatement(E, S);
}

void SExprBuilder::handleDestructorCall(const VarDecl *VD,
                                        const CXXDestructorDecl *DD) {
  til::SExpr *Sf = new (Arena) til::LiteralPtr(VD);
  til::SExpr *Dr = new (Arena) til::LiteralPtr(DD);
  til::SExpr *Ap = new (Arena) til::Apply(Dr, Sf);
  til::SExpr *E = new (Arena) til::Call(Ap);
  addStatement(E, nullptr);
}

void SExprBuilder::exitCFGBlockBody(const CFGBlock *B) {
  CurrentBB->instructions().reserve(
    static_cast<unsigned>(CurrentInstructions.size()), Arena);
  for (auto *V : CurrentInstructions)
    CurrentBB->addInstruction(V);

  // Create an appropriate terminator
  unsigned N = B->succ_size();
  auto It = B->succ_begin();
  if (N == 1) {
    til::BasicBlock *BB = *It ? lookupBlock(*It) : nullptr;
    // TODO: set index
    unsigned Idx = BB ? BB->findPredecessorIndex(CurrentBB) : 0;
    auto *Tm = new (Arena) til::Goto(BB, Idx);
    CurrentBB->setTerminator(Tm);
  }
  else if (N == 2) {
    til::SExpr *C = translate(B->getTerminatorCondition(true), nullptr);
    til::BasicBlock *BB1 = *It ? lookupBlock(*It) : nullptr;
    ++It;
    til::BasicBlock *BB2 = *It ? lookupBlock(*It) : nullptr;
    // FIXME: make sure these aren't critical edges.
    auto *Tm = new (Arena) til::Branch(C, BB1, BB2);
    CurrentBB->setTerminator(Tm);
  }
}

void SExprBuilder::handleSuccessor(const CFGBlock *Succ) {
  ++CurrentBlockInfo->UnprocessedSuccessors;
}

void SExprBuilder::handleSuccessorBackEdge(const CFGBlock *Succ) {
  mergePhiNodesBackEdge(Succ);
  ++BBInfo[Succ->getBlockID()].ProcessedPredecessors;
}

void SExprBuilder::exitCFGBlock(const CFGBlock *B) {
  CurrentArguments.clear();
  CurrentInstructions.clear();
  CurrentBlockInfo->ExitMap = std::move(CurrentLVarMap);
  CurrentBB = nullptr;
  CurrentBlockInfo = nullptr;
}

void SExprBuilder::exitCFG(const CFGBlock *Last) {
  for (auto *Ph : IncompleteArgs) {
    if (Ph->status() == til::Phi::PH_Incomplete)
      simplifyIncompleteArg(Ph);
  }

  CurrentArguments.clear();
  CurrentInstructions.clear();
  IncompleteArgs.clear();
}

/*
namespace {

class TILPrinter :
    public til::PrettyPrinter<TILPrinter, llvm::raw_ostream> {};

} // namespace

namespace clang {
namespace threadSafety {

void printSCFG(CFGWalker &Walker) {
  llvm::BumpPtrAllocator Bpa;
  til::MemRegionRef Arena(&Bpa);
  SExprBuilder SxBuilder(Arena);
  til::SCFG *Scfg = SxBuilder.buildCFG(Walker);
  TILPrinter::print(Scfg, llvm::errs());
}

} // namespace threadSafety
} // namespace clang
*/
