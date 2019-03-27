//===- SValBuilder.cpp - Basic class for all SValBuilder implementations --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines SValBuilder, the base class for all (complete) SValBuilder
//  implementations.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathSensitive/SValBuilder.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/Basic/LLVM.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/APSIntType.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/BasicValueFactory.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/MemRegion.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState_Fwd.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/Store.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SubEngine.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymExpr.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymbolManager.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <tuple>

using namespace clang;
using namespace ento;

//===----------------------------------------------------------------------===//
// Basic SVal creation.
//===----------------------------------------------------------------------===//

void SValBuilder::anchor() {}

DefinedOrUnknownSVal SValBuilder::makeZeroVal(QualType type) {
  if (Loc::isLocType(type))
    return makeNull();

  if (type->isIntegralOrEnumerationType())
    return makeIntVal(0, type);

  if (type->isArrayType() || type->isRecordType() || type->isVectorType() ||
      type->isAnyComplexType())
    return makeCompoundVal(type, BasicVals.getEmptySValList());

  // FIXME: Handle floats.
  return UnknownVal();
}

NonLoc SValBuilder::makeNonLoc(const SymExpr *lhs, BinaryOperator::Opcode op,
                                const llvm::APSInt& rhs, QualType type) {
  // The Environment ensures we always get a persistent APSInt in
  // BasicValueFactory, so we don't need to get the APSInt from
  // BasicValueFactory again.
  assert(lhs);
  assert(!Loc::isLocType(type));
  return nonloc::SymbolVal(SymMgr.getSymIntExpr(lhs, op, rhs, type));
}

NonLoc SValBuilder::makeNonLoc(const llvm::APSInt& lhs,
                               BinaryOperator::Opcode op, const SymExpr *rhs,
                               QualType type) {
  assert(rhs);
  assert(!Loc::isLocType(type));
  return nonloc::SymbolVal(SymMgr.getIntSymExpr(lhs, op, rhs, type));
}

NonLoc SValBuilder::makeNonLoc(const SymExpr *lhs, BinaryOperator::Opcode op,
                               const SymExpr *rhs, QualType type) {
  assert(lhs && rhs);
  assert(!Loc::isLocType(type));
  return nonloc::SymbolVal(SymMgr.getSymSymExpr(lhs, op, rhs, type));
}

NonLoc SValBuilder::makeNonLoc(const SymExpr *operand,
                               QualType fromTy, QualType toTy) {
  assert(operand);
  assert(!Loc::isLocType(toTy));
  return nonloc::SymbolVal(SymMgr.getCastSymbol(operand, fromTy, toTy));
}

SVal SValBuilder::convertToArrayIndex(SVal val) {
  if (val.isUnknownOrUndef())
    return val;

  // Common case: we have an appropriately sized integer.
  if (Optional<nonloc::ConcreteInt> CI = val.getAs<nonloc::ConcreteInt>()) {
    const llvm::APSInt& I = CI->getValue();
    if (I.getBitWidth() == ArrayIndexWidth && I.isSigned())
      return val;
  }

  return evalCastFromNonLoc(val.castAs<NonLoc>(), ArrayIndexTy);
}

nonloc::ConcreteInt SValBuilder::makeBoolVal(const CXXBoolLiteralExpr *boolean){
  return makeTruthVal(boolean->getValue());
}

DefinedOrUnknownSVal
SValBuilder::getRegionValueSymbolVal(const TypedValueRegion *region) {
  QualType T = region->getValueType();

  if (T->isNullPtrType())
    return makeZeroVal(T);

  if (!SymbolManager::canSymbolicate(T))
    return UnknownVal();

  SymbolRef sym = SymMgr.getRegionValueSymbol(region);

  if (Loc::isLocType(T))
    return loc::MemRegionVal(MemMgr.getSymbolicRegion(sym));

  return nonloc::SymbolVal(sym);
}

DefinedOrUnknownSVal SValBuilder::conjureSymbolVal(const void *SymbolTag,
                                                   const Expr *Ex,
                                                   const LocationContext *LCtx,
                                                   unsigned Count) {
  QualType T = Ex->getType();

  if (T->isNullPtrType())
    return makeZeroVal(T);

  // Compute the type of the result. If the expression is not an R-value, the
  // result should be a location.
  QualType ExType = Ex->getType();
  if (Ex->isGLValue())
    T = LCtx->getAnalysisDeclContext()->getASTContext().getPointerType(ExType);

  return conjureSymbolVal(SymbolTag, Ex, LCtx, T, Count);
}

DefinedOrUnknownSVal SValBuilder::conjureSymbolVal(const void *symbolTag,
                                                   const Expr *expr,
                                                   const LocationContext *LCtx,
                                                   QualType type,
                                                   unsigned count) {
  if (type->isNullPtrType())
    return makeZeroVal(type);

  if (!SymbolManager::canSymbolicate(type))
    return UnknownVal();

  SymbolRef sym = SymMgr.conjureSymbol(expr, LCtx, type, count, symbolTag);

  if (Loc::isLocType(type))
    return loc::MemRegionVal(MemMgr.getSymbolicRegion(sym));

  return nonloc::SymbolVal(sym);
}

DefinedOrUnknownSVal SValBuilder::conjureSymbolVal(const Stmt *stmt,
                                                   const LocationContext *LCtx,
                                                   QualType type,
                                                   unsigned visitCount) {
  if (type->isNullPtrType())
    return makeZeroVal(type);

  if (!SymbolManager::canSymbolicate(type))
    return UnknownVal();

  SymbolRef sym = SymMgr.conjureSymbol(stmt, LCtx, type, visitCount);

  if (Loc::isLocType(type))
    return loc::MemRegionVal(MemMgr.getSymbolicRegion(sym));

  return nonloc::SymbolVal(sym);
}

DefinedOrUnknownSVal
SValBuilder::getConjuredHeapSymbolVal(const Expr *E,
                                      const LocationContext *LCtx,
                                      unsigned VisitCount) {
  QualType T = E->getType();
  assert(Loc::isLocType(T));
  assert(SymbolManager::canSymbolicate(T));
  if (T->isNullPtrType())
    return makeZeroVal(T);

  SymbolRef sym = SymMgr.conjureSymbol(E, LCtx, T, VisitCount);
  return loc::MemRegionVal(MemMgr.getSymbolicHeapRegion(sym));
}

DefinedSVal SValBuilder::getMetadataSymbolVal(const void *symbolTag,
                                              const MemRegion *region,
                                              const Expr *expr, QualType type,
                                              const LocationContext *LCtx,
                                              unsigned count) {
  assert(SymbolManager::canSymbolicate(type) && "Invalid metadata symbol type");

  SymbolRef sym =
      SymMgr.getMetadataSymbol(region, expr, type, LCtx, count, symbolTag);

  if (Loc::isLocType(type))
    return loc::MemRegionVal(MemMgr.getSymbolicRegion(sym));

  return nonloc::SymbolVal(sym);
}

DefinedOrUnknownSVal
SValBuilder::getDerivedRegionValueSymbolVal(SymbolRef parentSymbol,
                                             const TypedValueRegion *region) {
  QualType T = region->getValueType();

  if (T->isNullPtrType())
    return makeZeroVal(T);

  if (!SymbolManager::canSymbolicate(T))
    return UnknownVal();

  SymbolRef sym = SymMgr.getDerivedSymbol(parentSymbol, region);

  if (Loc::isLocType(T))
    return loc::MemRegionVal(MemMgr.getSymbolicRegion(sym));

  return nonloc::SymbolVal(sym);
}

DefinedSVal SValBuilder::getMemberPointer(const DeclaratorDecl *DD) {
  assert(!DD || isa<CXXMethodDecl>(DD) || isa<FieldDecl>(DD));

  if (const auto *MD = dyn_cast_or_null<CXXMethodDecl>(DD)) {
    // Sema treats pointers to static member functions as have function pointer
    // type, so return a function pointer for the method.
    // We don't need to play a similar trick for static member fields
    // because these are represented as plain VarDecls and not FieldDecls
    // in the AST.
    if (MD->isStatic())
      return getFunctionPointer(MD);
  }

  return nonloc::PointerToMember(DD);
}

DefinedSVal SValBuilder::getFunctionPointer(const FunctionDecl *func) {
  return loc::MemRegionVal(MemMgr.getFunctionCodeRegion(func));
}

DefinedSVal SValBuilder::getBlockPointer(const BlockDecl *block,
                                         CanQualType locTy,
                                         const LocationContext *locContext,
                                         unsigned blockCount) {
  const BlockCodeRegion *BC =
    MemMgr.getBlockCodeRegion(block, locTy, locContext->getAnalysisDeclContext());
  const BlockDataRegion *BD = MemMgr.getBlockDataRegion(BC, locContext,
                                                        blockCount);
  return loc::MemRegionVal(BD);
}

/// Return a memory region for the 'this' object reference.
loc::MemRegionVal SValBuilder::getCXXThis(const CXXMethodDecl *D,
                                          const StackFrameContext *SFC) {
  return loc::MemRegionVal(
      getRegionManager().getCXXThisRegion(D->getThisType(), SFC));
}

/// Return a memory region for the 'this' object reference.
loc::MemRegionVal SValBuilder::getCXXThis(const CXXRecordDecl *D,
                                          const StackFrameContext *SFC) {
  const Type *T = D->getTypeForDecl();
  QualType PT = getContext().getPointerType(QualType(T, 0));
  return loc::MemRegionVal(getRegionManager().getCXXThisRegion(PT, SFC));
}

Optional<SVal> SValBuilder::getConstantVal(const Expr *E) {
  E = E->IgnoreParens();

  switch (E->getStmtClass()) {
  // Handle expressions that we treat differently from the AST's constant
  // evaluator.
  case Stmt::AddrLabelExprClass:
    return makeLoc(cast<AddrLabelExpr>(E));

  case Stmt::CXXScalarValueInitExprClass:
  case Stmt::ImplicitValueInitExprClass:
    return makeZeroVal(E->getType());

  case Stmt::ObjCStringLiteralClass: {
    const auto *SL = cast<ObjCStringLiteral>(E);
    return makeLoc(getRegionManager().getObjCStringRegion(SL));
  }

  case Stmt::StringLiteralClass: {
    const auto *SL = cast<StringLiteral>(E);
    return makeLoc(getRegionManager().getStringRegion(SL));
  }

  // Fast-path some expressions to avoid the overhead of going through the AST's
  // constant evaluator
  case Stmt::CharacterLiteralClass: {
    const auto *C = cast<CharacterLiteral>(E);
    return makeIntVal(C->getValue(), C->getType());
  }

  case Stmt::CXXBoolLiteralExprClass:
    return makeBoolVal(cast<CXXBoolLiteralExpr>(E));

  case Stmt::TypeTraitExprClass: {
    const auto *TE = cast<TypeTraitExpr>(E);
    return makeTruthVal(TE->getValue(), TE->getType());
  }

  case Stmt::IntegerLiteralClass:
    return makeIntVal(cast<IntegerLiteral>(E));

  case Stmt::ObjCBoolLiteralExprClass:
    return makeBoolVal(cast<ObjCBoolLiteralExpr>(E));

  case Stmt::CXXNullPtrLiteralExprClass:
    return makeNull();

  case Stmt::CStyleCastExprClass:
  case Stmt::CXXFunctionalCastExprClass:
  case Stmt::CXXConstCastExprClass:
  case Stmt::CXXReinterpretCastExprClass:
  case Stmt::CXXStaticCastExprClass:
  case Stmt::ImplicitCastExprClass: {
    const auto *CE = cast<CastExpr>(E);
    switch (CE->getCastKind()) {
    default:
      break;
    case CK_ArrayToPointerDecay:
    case CK_IntegralToPointer:
    case CK_NoOp:
    case CK_BitCast: {
      const Expr *SE = CE->getSubExpr();
      Optional<SVal> Val = getConstantVal(SE);
      if (!Val)
        return None;
      return evalCast(*Val, CE->getType(), SE->getType());
    }
    }
    // FALLTHROUGH
    LLVM_FALLTHROUGH;
  }

  // If we don't have a special case, fall back to the AST's constant evaluator.
  default: {
    // Don't try to come up with a value for materialized temporaries.
    if (E->isGLValue())
      return None;

    ASTContext &Ctx = getContext();
    Expr::EvalResult Result;
    if (E->EvaluateAsInt(Result, Ctx))
      return makeIntVal(Result.Val.getInt());

    if (Loc::isLocType(E->getType()))
      if (E->isNullPointerConstant(Ctx, Expr::NPC_ValueDependentIsNotNull))
        return makeNull();

    return None;
  }
  }
}

SVal SValBuilder::makeSymExprValNN(BinaryOperator::Opcode Op,
                                   NonLoc LHS, NonLoc RHS,
                                   QualType ResultTy) {
  const SymExpr *symLHS = LHS.getAsSymExpr();
  const SymExpr *symRHS = RHS.getAsSymExpr();

  // TODO: When the Max Complexity is reached, we should conjure a symbol
  // instead of generating an Unknown value and propagate the taint info to it.
  const unsigned MaxComp = StateMgr.getOwningEngine()
                               .getAnalysisManager()
                               .options.MaxSymbolComplexity;

  if (symLHS && symRHS &&
      (symLHS->computeComplexity() + symRHS->computeComplexity()) <  MaxComp)
    return makeNonLoc(symLHS, Op, symRHS, ResultTy);

  if (symLHS && symLHS->computeComplexity() < MaxComp)
    if (Optional<nonloc::ConcreteInt> rInt = RHS.getAs<nonloc::ConcreteInt>())
      return makeNonLoc(symLHS, Op, rInt->getValue(), ResultTy);

  if (symRHS && symRHS->computeComplexity() < MaxComp)
    if (Optional<nonloc::ConcreteInt> lInt = LHS.getAs<nonloc::ConcreteInt>())
      return makeNonLoc(lInt->getValue(), Op, symRHS, ResultTy);

  return UnknownVal();
}

SVal SValBuilder::evalBinOp(ProgramStateRef state, BinaryOperator::Opcode op,
                            SVal lhs, SVal rhs, QualType type) {
  if (lhs.isUndef() || rhs.isUndef())
    return UndefinedVal();

  if (lhs.isUnknown() || rhs.isUnknown())
    return UnknownVal();

  if (lhs.getAs<nonloc::LazyCompoundVal>() ||
      rhs.getAs<nonloc::LazyCompoundVal>()) {
    return UnknownVal();
  }

  if (Optional<Loc> LV = lhs.getAs<Loc>()) {
    if (Optional<Loc> RV = rhs.getAs<Loc>())
      return evalBinOpLL(state, op, *LV, *RV, type);

    return evalBinOpLN(state, op, *LV, rhs.castAs<NonLoc>(), type);
  }

  if (Optional<Loc> RV = rhs.getAs<Loc>()) {
    // Support pointer arithmetic where the addend is on the left
    // and the pointer on the right.
    assert(op == BO_Add);

    // Commute the operands.
    return evalBinOpLN(state, op, *RV, lhs.castAs<NonLoc>(), type);
  }

  return evalBinOpNN(state, op, lhs.castAs<NonLoc>(), rhs.castAs<NonLoc>(),
                     type);
}

ConditionTruthVal SValBuilder::areEqual(ProgramStateRef state, SVal lhs,
                                        SVal rhs) {
  return state->isNonNull(evalEQ(state, lhs, rhs));
}

SVal SValBuilder::evalEQ(ProgramStateRef state, SVal lhs, SVal rhs) {
  return evalBinOp(state, BO_EQ, lhs, rhs, getConditionType());
}

DefinedOrUnknownSVal SValBuilder::evalEQ(ProgramStateRef state,
                                         DefinedOrUnknownSVal lhs,
                                         DefinedOrUnknownSVal rhs) {
  return evalEQ(state, static_cast<SVal>(lhs), static_cast<SVal>(rhs))
      .castAs<DefinedOrUnknownSVal>();
}

/// Recursively check if the pointer types are equal modulo const, volatile,
/// and restrict qualifiers. Also, assume that all types are similar to 'void'.
/// Assumes the input types are canonical.
static bool shouldBeModeledWithNoOp(ASTContext &Context, QualType ToTy,
                                                         QualType FromTy) {
  while (Context.UnwrapSimilarTypes(ToTy, FromTy)) {
    Qualifiers Quals1, Quals2;
    ToTy = Context.getUnqualifiedArrayType(ToTy, Quals1);
    FromTy = Context.getUnqualifiedArrayType(FromTy, Quals2);

    // Make sure that non-cvr-qualifiers the other qualifiers (e.g., address
    // spaces) are identical.
    Quals1.removeCVRQualifiers();
    Quals2.removeCVRQualifiers();
    if (Quals1 != Quals2)
      return false;
  }

  // If we are casting to void, the 'From' value can be used to represent the
  // 'To' value.
  //
  // FIXME: Doing this after unwrapping the types doesn't make any sense. A
  // cast from 'int**' to 'void**' is not special in the way that a cast from
  // 'int*' to 'void*' is.
  if (ToTy->isVoidType())
    return true;

  if (ToTy != FromTy)
    return false;

  return true;
}

// Handles casts of type CK_IntegralCast.
// At the moment, this function will redirect to evalCast, except when the range
// of the original value is known to be greater than the max of the target type.
SVal SValBuilder::evalIntegralCast(ProgramStateRef state, SVal val,
                                   QualType castTy, QualType originalTy) {
  // No truncations if target type is big enough.
  if (getContext().getTypeSize(castTy) >= getContext().getTypeSize(originalTy))
    return evalCast(val, castTy, originalTy);

  const SymExpr *se = val.getAsSymbolicExpression();
  if (!se) // Let evalCast handle non symbolic expressions.
    return evalCast(val, castTy, originalTy);

  // Find the maximum value of the target type.
  APSIntType ToType(getContext().getTypeSize(castTy),
                    castTy->isUnsignedIntegerType());
  llvm::APSInt ToTypeMax = ToType.getMaxValue();
  NonLoc ToTypeMaxVal =
      makeIntVal(ToTypeMax.isUnsigned() ? ToTypeMax.getZExtValue()
                                        : ToTypeMax.getSExtValue(),
                 castTy)
          .castAs<NonLoc>();
  // Check the range of the symbol being casted against the maximum value of the
  // target type.
  NonLoc FromVal = val.castAs<NonLoc>();
  QualType CmpTy = getConditionType();
  NonLoc CompVal =
      evalBinOpNN(state, BO_LE, FromVal, ToTypeMaxVal, CmpTy).castAs<NonLoc>();
  ProgramStateRef IsNotTruncated, IsTruncated;
  std::tie(IsNotTruncated, IsTruncated) = state->assume(CompVal);
  if (!IsNotTruncated && IsTruncated) {
    // Symbol is truncated so we evaluate it as a cast.
    NonLoc CastVal = makeNonLoc(se, originalTy, castTy);
    return CastVal;
  }
  return evalCast(val, castTy, originalTy);
}

// FIXME: should rewrite according to the cast kind.
SVal SValBuilder::evalCast(SVal val, QualType castTy, QualType originalTy) {
  castTy = Context.getCanonicalType(castTy);
  originalTy = Context.getCanonicalType(originalTy);
  if (val.isUnknownOrUndef() || castTy == originalTy)
    return val;

  if (castTy->isBooleanType()) {
    if (val.isUnknownOrUndef())
      return val;
    if (val.isConstant())
      return makeTruthVal(!val.isZeroConstant(), castTy);
    if (!Loc::isLocType(originalTy) &&
        !originalTy->isIntegralOrEnumerationType() &&
        !originalTy->isMemberPointerType())
      return UnknownVal();
    if (SymbolRef Sym = val.getAsSymbol(true)) {
      BasicValueFactory &BVF = getBasicValueFactory();
      // FIXME: If we had a state here, we could see if the symbol is known to
      // be zero, but we don't.
      return makeNonLoc(Sym, BO_NE, BVF.getValue(0, Sym->getType()), castTy);
    }
    // Loc values are not always true, they could be weakly linked functions.
    if (Optional<Loc> L = val.getAs<Loc>())
      return evalCastFromLoc(*L, castTy);

    Loc L = val.castAs<nonloc::LocAsInteger>().getLoc();
    return evalCastFromLoc(L, castTy);
  }

  // For const casts, casts to void, just propagate the value.
  if (!castTy->isVariableArrayType() && !originalTy->isVariableArrayType())
    if (shouldBeModeledWithNoOp(Context, Context.getPointerType(castTy),
                                         Context.getPointerType(originalTy)))
      return val;

  // Check for casts from pointers to integers.
  if (castTy->isIntegralOrEnumerationType() && Loc::isLocType(originalTy))
    return evalCastFromLoc(val.castAs<Loc>(), castTy);

  // Check for casts from integers to pointers.
  if (Loc::isLocType(castTy) && originalTy->isIntegralOrEnumerationType()) {
    if (Optional<nonloc::LocAsInteger> LV = val.getAs<nonloc::LocAsInteger>()) {
      if (const MemRegion *R = LV->getLoc().getAsRegion()) {
        StoreManager &storeMgr = StateMgr.getStoreManager();
        R = storeMgr.castRegion(R, castTy);
        return R ? SVal(loc::MemRegionVal(R)) : UnknownVal();
      }
      return LV->getLoc();
    }
    return dispatchCast(val, castTy);
  }

  // Just pass through function and block pointers.
  if (originalTy->isBlockPointerType() || originalTy->isFunctionPointerType()) {
    assert(Loc::isLocType(castTy));
    return val;
  }

  // Check for casts from array type to another type.
  if (const auto *arrayT =
          dyn_cast<ArrayType>(originalTy.getCanonicalType())) {
    // We will always decay to a pointer.
    QualType elemTy = arrayT->getElementType();
    val = StateMgr.ArrayToPointer(val.castAs<Loc>(), elemTy);

    // Are we casting from an array to a pointer?  If so just pass on
    // the decayed value.
    if (castTy->isPointerType() || castTy->isReferenceType())
      return val;

    // Are we casting from an array to an integer?  If so, cast the decayed
    // pointer value to an integer.
    assert(castTy->isIntegralOrEnumerationType());

    // FIXME: Keep these here for now in case we decide soon that we
    // need the original decayed type.
    //    QualType elemTy = cast<ArrayType>(originalTy)->getElementType();
    //    QualType pointerTy = C.getPointerType(elemTy);
    return evalCastFromLoc(val.castAs<Loc>(), castTy);
  }

  // Check for casts from a region to a specific type.
  if (const MemRegion *R = val.getAsRegion()) {
    // Handle other casts of locations to integers.
    if (castTy->isIntegralOrEnumerationType())
      return evalCastFromLoc(loc::MemRegionVal(R), castTy);

    // FIXME: We should handle the case where we strip off view layers to get
    //  to a desugared type.
    if (!Loc::isLocType(castTy)) {
      // FIXME: There can be gross cases where one casts the result of a function
      // (that returns a pointer) to some other value that happens to fit
      // within that pointer value.  We currently have no good way to
      // model such operations.  When this happens, the underlying operation
      // is that the caller is reasoning about bits.  Conceptually we are
      // layering a "view" of a location on top of those bits.  Perhaps
      // we need to be more lazy about mutual possible views, even on an
      // SVal?  This may be necessary for bit-level reasoning as well.
      return UnknownVal();
    }

    // We get a symbolic function pointer for a dereference of a function
    // pointer, but it is of function type. Example:

    //  struct FPRec {
    //    void (*my_func)(int * x);
    //  };
    //
    //  int bar(int x);
    //
    //  int f1_a(struct FPRec* foo) {
    //    int x;
    //    (*foo->my_func)(&x);
    //    return bar(x)+1; // no-warning
    //  }

    assert(Loc::isLocType(originalTy) || originalTy->isFunctionType() ||
           originalTy->isBlockPointerType() || castTy->isReferenceType());

    StoreManager &storeMgr = StateMgr.getStoreManager();

    // Delegate to store manager to get the result of casting a region to a
    // different type.  If the MemRegion* returned is NULL, this expression
    // Evaluates to UnknownVal.
    R = storeMgr.castRegion(R, castTy);
    return R ? SVal(loc::MemRegionVal(R)) : UnknownVal();
  }

  return dispatchCast(val, castTy);
}
