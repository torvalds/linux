//===--- ScopeInfo.cpp - Information about a semantic context -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements FunctionScopeInfo and its subclasses, which contain
// information about a single function, block, lambda, or method body.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/ScopeInfo.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"

using namespace clang;
using namespace sema;

void FunctionScopeInfo::Clear() {
  HasBranchProtectedScope = false;
  HasBranchIntoScope = false;
  HasIndirectGoto = false;
  HasDroppedStmt = false;
  HasOMPDeclareReductionCombiner = false;
  HasFallthroughStmt = false;
  HasPotentialAvailabilityViolations = false;
  ObjCShouldCallSuper = false;
  ObjCIsDesignatedInit = false;
  ObjCWarnForNoDesignatedInitChain = false;
  ObjCIsSecondaryInit = false;
  ObjCWarnForNoInitDelegation = false;
  FirstReturnLoc = SourceLocation();
  FirstCXXTryLoc = SourceLocation();
  FirstSEHTryLoc = SourceLocation();

  // Coroutine state
  FirstCoroutineStmtLoc = SourceLocation();
  CoroutinePromise = nullptr;
  CoroutineParameterMoves.clear();
  NeedsCoroutineSuspends = true;
  CoroutineSuspends.first = nullptr;
  CoroutineSuspends.second = nullptr;

  SwitchStack.clear();
  Returns.clear();
  ErrorTrap.reset();
  PossiblyUnreachableDiags.clear();
  WeakObjectUses.clear();
  ModifiedNonNullParams.clear();
  Blocks.clear();
  ByrefBlockVars.clear();
}

static const NamedDecl *getBestPropertyDecl(const ObjCPropertyRefExpr *PropE) {
  if (PropE->isExplicitProperty())
    return PropE->getExplicitProperty();

  return PropE->getImplicitPropertyGetter();
}

FunctionScopeInfo::WeakObjectProfileTy::BaseInfoTy
FunctionScopeInfo::WeakObjectProfileTy::getBaseInfo(const Expr *E) {
  E = E->IgnoreParenCasts();

  const NamedDecl *D = nullptr;
  bool IsExact = false;

  switch (E->getStmtClass()) {
  case Stmt::DeclRefExprClass:
    D = cast<DeclRefExpr>(E)->getDecl();
    IsExact = isa<VarDecl>(D);
    break;
  case Stmt::MemberExprClass: {
    const MemberExpr *ME = cast<MemberExpr>(E);
    D = ME->getMemberDecl();
    IsExact = isa<CXXThisExpr>(ME->getBase()->IgnoreParenImpCasts());
    break;
  }
  case Stmt::ObjCIvarRefExprClass: {
    const ObjCIvarRefExpr *IE = cast<ObjCIvarRefExpr>(E);
    D = IE->getDecl();
    IsExact = IE->getBase()->isObjCSelfExpr();
    break;
  }
  case Stmt::PseudoObjectExprClass: {
    const PseudoObjectExpr *POE = cast<PseudoObjectExpr>(E);
    const ObjCPropertyRefExpr *BaseProp =
      dyn_cast<ObjCPropertyRefExpr>(POE->getSyntacticForm());
    if (BaseProp) {
      D = getBestPropertyDecl(BaseProp);

      if (BaseProp->isObjectReceiver()) {
        const Expr *DoubleBase = BaseProp->getBase();
        if (const OpaqueValueExpr *OVE = dyn_cast<OpaqueValueExpr>(DoubleBase))
          DoubleBase = OVE->getSourceExpr();

        IsExact = DoubleBase->isObjCSelfExpr();
      }
    }
    break;
  }
  default:
    break;
  }

  return BaseInfoTy(D, IsExact);
}

bool CapturingScopeInfo::isVLATypeCaptured(const VariableArrayType *VAT) const {
  RecordDecl *RD = nullptr;
  if (auto *LSI = dyn_cast<LambdaScopeInfo>(this))
    RD = LSI->Lambda;
  else if (auto CRSI = dyn_cast<CapturedRegionScopeInfo>(this))
    RD = CRSI->TheRecordDecl;

  if (RD)
    for (auto *FD : RD->fields()) {
      if (FD->hasCapturedVLAType() && FD->getCapturedVLAType() == VAT)
        return true;
    }
  return false;
}

FunctionScopeInfo::WeakObjectProfileTy::WeakObjectProfileTy(
                                          const ObjCPropertyRefExpr *PropE)
    : Base(nullptr, true), Property(getBestPropertyDecl(PropE)) {

  if (PropE->isObjectReceiver()) {
    const OpaqueValueExpr *OVE = cast<OpaqueValueExpr>(PropE->getBase());
    const Expr *E = OVE->getSourceExpr();
    Base = getBaseInfo(E);
  } else if (PropE->isClassReceiver()) {
    Base.setPointer(PropE->getClassReceiver());
  } else {
    assert(PropE->isSuperReceiver());
  }
}

FunctionScopeInfo::WeakObjectProfileTy::WeakObjectProfileTy(const Expr *BaseE,
                                                const ObjCPropertyDecl *Prop)
    : Base(nullptr, true), Property(Prop) {
  if (BaseE)
    Base = getBaseInfo(BaseE);
  // else, this is a message accessing a property on super.
}

FunctionScopeInfo::WeakObjectProfileTy::WeakObjectProfileTy(
                                                      const DeclRefExpr *DRE)
  : Base(nullptr, true), Property(DRE->getDecl()) {
  assert(isa<VarDecl>(Property));
}

FunctionScopeInfo::WeakObjectProfileTy::WeakObjectProfileTy(
                                                  const ObjCIvarRefExpr *IvarE)
  : Base(getBaseInfo(IvarE->getBase())), Property(IvarE->getDecl()) {
}

void FunctionScopeInfo::recordUseOfWeak(const ObjCMessageExpr *Msg,
                                        const ObjCPropertyDecl *Prop) {
  assert(Msg && Prop);
  WeakUseVector &Uses =
    WeakObjectUses[WeakObjectProfileTy(Msg->getInstanceReceiver(), Prop)];
  Uses.push_back(WeakUseTy(Msg, Msg->getNumArgs() == 0));
}

void FunctionScopeInfo::markSafeWeakUse(const Expr *E) {
  E = E->IgnoreParenCasts();

  if (const PseudoObjectExpr *POE = dyn_cast<PseudoObjectExpr>(E)) {
    markSafeWeakUse(POE->getSyntacticForm());
    return;
  }

  if (const ConditionalOperator *Cond = dyn_cast<ConditionalOperator>(E)) {
    markSafeWeakUse(Cond->getTrueExpr());
    markSafeWeakUse(Cond->getFalseExpr());
    return;
  }

  if (const BinaryConditionalOperator *Cond =
        dyn_cast<BinaryConditionalOperator>(E)) {
    markSafeWeakUse(Cond->getCommon());
    markSafeWeakUse(Cond->getFalseExpr());
    return;
  }

  // Has this weak object been seen before?
  FunctionScopeInfo::WeakObjectUseMap::iterator Uses = WeakObjectUses.end();
  if (const ObjCPropertyRefExpr *RefExpr = dyn_cast<ObjCPropertyRefExpr>(E)) {
    if (!RefExpr->isObjectReceiver())
      return;
    if (isa<OpaqueValueExpr>(RefExpr->getBase()))
     Uses = WeakObjectUses.find(WeakObjectProfileTy(RefExpr));
    else {
      markSafeWeakUse(RefExpr->getBase());
      return;
    }
  }
  else if (const ObjCIvarRefExpr *IvarE = dyn_cast<ObjCIvarRefExpr>(E))
    Uses = WeakObjectUses.find(WeakObjectProfileTy(IvarE));
  else if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (isa<VarDecl>(DRE->getDecl()))
      Uses = WeakObjectUses.find(WeakObjectProfileTy(DRE));
  } else if (const ObjCMessageExpr *MsgE = dyn_cast<ObjCMessageExpr>(E)) {
    if (const ObjCMethodDecl *MD = MsgE->getMethodDecl()) {
      if (const ObjCPropertyDecl *Prop = MD->findPropertyDecl()) {
        Uses =
          WeakObjectUses.find(WeakObjectProfileTy(MsgE->getInstanceReceiver(),
                                                  Prop));
      }
    }
  }
  else
    return;

  if (Uses == WeakObjectUses.end())
    return;

  // Has there been a read from the object using this Expr?
  FunctionScopeInfo::WeakUseVector::reverse_iterator ThisUse =
      llvm::find(llvm::reverse(Uses->second), WeakUseTy(E, true));
  if (ThisUse == Uses->second.rend())
    return;

  ThisUse->markSafe();
}

void LambdaScopeInfo::getPotentialVariableCapture(unsigned Idx, VarDecl *&VD,
                                                  Expr *&E) const {
  assert(Idx < getNumPotentialVariableCaptures() &&
         "Index of potential capture must be within 0 to less than the "
         "number of captures!");
  E = PotentiallyCapturingExprs[Idx];
  if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E))
    VD = dyn_cast<VarDecl>(DRE->getFoundDecl());
  else if (MemberExpr *ME = dyn_cast<MemberExpr>(E))
    VD = dyn_cast<VarDecl>(ME->getMemberDecl());
  else
    llvm_unreachable("Only DeclRefExprs or MemberExprs should be added for "
    "potential captures");
  assert(VD);
}

FunctionScopeInfo::~FunctionScopeInfo() { }
BlockScopeInfo::~BlockScopeInfo() { }
CapturedRegionScopeInfo::~CapturedRegionScopeInfo() { }
