//===--- SemaDeclAttr.cpp - Declaration Attribute Handling ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements decl-related attribute processing.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTMutationListener.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/DelayedDiagnostic.h"
#include "clang/Sema/Initialization.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/SemaInternal.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/MathExtras.h"

using namespace clang;
using namespace sema;

namespace AttributeLangSupport {
  enum LANG {
    C,
    Cpp,
    ObjC
  };
} // end namespace AttributeLangSupport

//===----------------------------------------------------------------------===//
//  Helper functions
//===----------------------------------------------------------------------===//

/// isFunctionOrMethod - Return true if the given decl has function
/// type (function or function-typed variable) or an Objective-C
/// method.
static bool isFunctionOrMethod(const Decl *D) {
  return (D->getFunctionType() != nullptr) || isa<ObjCMethodDecl>(D);
}

/// Return true if the given decl has function type (function or
/// function-typed variable) or an Objective-C method or a block.
static bool isFunctionOrMethodOrBlock(const Decl *D) {
  return isFunctionOrMethod(D) || isa<BlockDecl>(D);
}

/// Return true if the given decl has a declarator that should have
/// been processed by Sema::GetTypeForDeclarator.
static bool hasDeclarator(const Decl *D) {
  // In some sense, TypedefDecl really *ought* to be a DeclaratorDecl.
  return isa<DeclaratorDecl>(D) || isa<BlockDecl>(D) || isa<TypedefNameDecl>(D) ||
         isa<ObjCPropertyDecl>(D);
}

/// hasFunctionProto - Return true if the given decl has a argument
/// information. This decl should have already passed
/// isFunctionOrMethod or isFunctionOrMethodOrBlock.
static bool hasFunctionProto(const Decl *D) {
  if (const FunctionType *FnTy = D->getFunctionType())
    return isa<FunctionProtoType>(FnTy);
  return isa<ObjCMethodDecl>(D) || isa<BlockDecl>(D);
}

/// getFunctionOrMethodNumParams - Return number of function or method
/// parameters. It is an error to call this on a K&R function (use
/// hasFunctionProto first).
static unsigned getFunctionOrMethodNumParams(const Decl *D) {
  if (const FunctionType *FnTy = D->getFunctionType())
    return cast<FunctionProtoType>(FnTy)->getNumParams();
  if (const auto *BD = dyn_cast<BlockDecl>(D))
    return BD->getNumParams();
  return cast<ObjCMethodDecl>(D)->param_size();
}

static const ParmVarDecl *getFunctionOrMethodParam(const Decl *D,
                                                   unsigned Idx) {
  if (const auto *FD = dyn_cast<FunctionDecl>(D))
    return FD->getParamDecl(Idx);
  if (const auto *MD = dyn_cast<ObjCMethodDecl>(D))
    return MD->getParamDecl(Idx);
  if (const auto *BD = dyn_cast<BlockDecl>(D))
    return BD->getParamDecl(Idx);
  return nullptr;
}

static QualType getFunctionOrMethodParamType(const Decl *D, unsigned Idx) {
  if (const FunctionType *FnTy = D->getFunctionType())
    return cast<FunctionProtoType>(FnTy)->getParamType(Idx);
  if (const auto *BD = dyn_cast<BlockDecl>(D))
    return BD->getParamDecl(Idx)->getType();

  return cast<ObjCMethodDecl>(D)->parameters()[Idx]->getType();
}

static SourceRange getFunctionOrMethodParamRange(const Decl *D, unsigned Idx) {
  if (auto *PVD = getFunctionOrMethodParam(D, Idx))
    return PVD->getSourceRange();
  return SourceRange();
}

static QualType getFunctionOrMethodResultType(const Decl *D) {
  if (const FunctionType *FnTy = D->getFunctionType())
    return FnTy->getReturnType();
  return cast<ObjCMethodDecl>(D)->getReturnType();
}

static SourceRange getFunctionOrMethodResultSourceRange(const Decl *D) {
  if (const auto *FD = dyn_cast<FunctionDecl>(D))
    return FD->getReturnTypeSourceRange();
  if (const auto *MD = dyn_cast<ObjCMethodDecl>(D))
    return MD->getReturnTypeSourceRange();
  return SourceRange();
}

static bool isFunctionOrMethodVariadic(const Decl *D) {
  if (const FunctionType *FnTy = D->getFunctionType())
    return cast<FunctionProtoType>(FnTy)->isVariadic();
  if (const auto *BD = dyn_cast<BlockDecl>(D))
    return BD->isVariadic();
  return cast<ObjCMethodDecl>(D)->isVariadic();
}

static bool isInstanceMethod(const Decl *D) {
  if (const auto *MethodDecl = dyn_cast<CXXMethodDecl>(D))
    return MethodDecl->isInstance();
  return false;
}

static inline bool isNSStringType(QualType T, ASTContext &Ctx) {
  const auto *PT = T->getAs<ObjCObjectPointerType>();
  if (!PT)
    return false;

  ObjCInterfaceDecl *Cls = PT->getObjectType()->getInterface();
  if (!Cls)
    return false;

  IdentifierInfo* ClsName = Cls->getIdentifier();

  // FIXME: Should we walk the chain of classes?
  return ClsName == &Ctx.Idents.get("NSString") ||
         ClsName == &Ctx.Idents.get("NSMutableString");
}

static inline bool isCFStringType(QualType T, ASTContext &Ctx) {
  const auto *PT = T->getAs<PointerType>();
  if (!PT)
    return false;

  const auto *RT = PT->getPointeeType()->getAs<RecordType>();
  if (!RT)
    return false;

  const RecordDecl *RD = RT->getDecl();
  if (RD->getTagKind() != TTK_Struct)
    return false;

  return RD->getIdentifier() == &Ctx.Idents.get("__CFString");
}

static unsigned getNumAttributeArgs(const ParsedAttr &AL) {
  // FIXME: Include the type in the argument list.
  return AL.getNumArgs() + AL.hasParsedType();
}

template <typename Compare>
static bool checkAttributeNumArgsImpl(Sema &S, const ParsedAttr &AL,
                                      unsigned Num, unsigned Diag,
                                      Compare Comp) {
  if (Comp(getNumAttributeArgs(AL), Num)) {
    S.Diag(AL.getLoc(), Diag) << AL << Num;
    return false;
  }

  return true;
}

/// Check if the attribute has exactly as many args as Num. May
/// output an error.
static bool checkAttributeNumArgs(Sema &S, const ParsedAttr &AL, unsigned Num) {
  return checkAttributeNumArgsImpl(S, AL, Num,
                                   diag::err_attribute_wrong_number_arguments,
                                   std::not_equal_to<unsigned>());
}

/// Check if the attribute has at least as many args as Num. May
/// output an error.
static bool checkAttributeAtLeastNumArgs(Sema &S, const ParsedAttr &AL,
                                         unsigned Num) {
  return checkAttributeNumArgsImpl(S, AL, Num,
                                   diag::err_attribute_too_few_arguments,
                                   std::less<unsigned>());
}

/// Check if the attribute has at most as many args as Num. May
/// output an error.
static bool checkAttributeAtMostNumArgs(Sema &S, const ParsedAttr &AL,
                                        unsigned Num) {
  return checkAttributeNumArgsImpl(S, AL, Num,
                                   diag::err_attribute_too_many_arguments,
                                   std::greater<unsigned>());
}

/// A helper function to provide Attribute Location for the Attr types
/// AND the ParsedAttr.
template <typename AttrInfo>
static typename std::enable_if<std::is_base_of<Attr, AttrInfo>::value,
                               SourceLocation>::type
getAttrLoc(const AttrInfo &AL) {
  return AL.getLocation();
}
static SourceLocation getAttrLoc(const ParsedAttr &AL) { return AL.getLoc(); }

/// If Expr is a valid integer constant, get the value of the integer
/// expression and return success or failure. May output an error.
///
/// Negative argument is implicitly converted to unsigned, unless
/// \p StrictlyUnsigned is true.
template <typename AttrInfo>
static bool checkUInt32Argument(Sema &S, const AttrInfo &AI, const Expr *Expr,
                                uint32_t &Val, unsigned Idx = UINT_MAX,
                                bool StrictlyUnsigned = false) {
  llvm::APSInt I(32);
  if (Expr->isTypeDependent() || Expr->isValueDependent() ||
      !Expr->isIntegerConstantExpr(I, S.Context)) {
    if (Idx != UINT_MAX)
      S.Diag(getAttrLoc(AI), diag::err_attribute_argument_n_type)
          << AI << Idx << AANT_ArgumentIntegerConstant
          << Expr->getSourceRange();
    else
      S.Diag(getAttrLoc(AI), diag::err_attribute_argument_type)
          << AI << AANT_ArgumentIntegerConstant << Expr->getSourceRange();
    return false;
  }

  if (!I.isIntN(32)) {
    S.Diag(Expr->getExprLoc(), diag::err_ice_too_large)
        << I.toString(10, false) << 32 << /* Unsigned */ 1;
    return false;
  }

  if (StrictlyUnsigned && I.isSigned() && I.isNegative()) {
    S.Diag(getAttrLoc(AI), diag::err_attribute_requires_positive_integer)
        << AI << /*non-negative*/ 1;
    return false;
  }

  Val = (uint32_t)I.getZExtValue();
  return true;
}

/// Wrapper around checkUInt32Argument, with an extra check to be sure
/// that the result will fit into a regular (signed) int. All args have the same
/// purpose as they do in checkUInt32Argument.
template <typename AttrInfo>
static bool checkPositiveIntArgument(Sema &S, const AttrInfo &AI, const Expr *Expr,
                                     int &Val, unsigned Idx = UINT_MAX) {
  uint32_t UVal;
  if (!checkUInt32Argument(S, AI, Expr, UVal, Idx))
    return false;

  if (UVal > (uint32_t)std::numeric_limits<int>::max()) {
    llvm::APSInt I(32); // for toString
    I = UVal;
    S.Diag(Expr->getExprLoc(), diag::err_ice_too_large)
        << I.toString(10, false) << 32 << /* Unsigned */ 0;
    return false;
  }

  Val = UVal;
  return true;
}

/// Diagnose mutually exclusive attributes when present on a given
/// declaration. Returns true if diagnosed.
template <typename AttrTy>
static bool checkAttrMutualExclusion(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (const auto *A = D->getAttr<AttrTy>()) {
    S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible) << AL << A;
    S.Diag(A->getLocation(), diag::note_conflicting_attribute);
    return true;
  }
  return false;
}

template <typename AttrTy>
static bool checkAttrMutualExclusion(Sema &S, Decl *D, const Attr &AL) {
  if (const auto *A = D->getAttr<AttrTy>()) {
    S.Diag(AL.getLocation(), diag::err_attributes_are_not_compatible) << &AL
                                                                      << A;
    S.Diag(A->getLocation(), diag::note_conflicting_attribute);
    return true;
  }
  return false;
}

/// Check if IdxExpr is a valid parameter index for a function or
/// instance method D.  May output an error.
///
/// \returns true if IdxExpr is a valid index.
template <typename AttrInfo>
static bool checkFunctionOrMethodParameterIndex(
    Sema &S, const Decl *D, const AttrInfo &AI, unsigned AttrArgNum,
    const Expr *IdxExpr, ParamIdx &Idx, bool CanIndexImplicitThis = false) {
  assert(isFunctionOrMethodOrBlock(D));

  // In C++ the implicit 'this' function parameter also counts.
  // Parameters are counted from one.
  bool HP = hasFunctionProto(D);
  bool HasImplicitThisParam = isInstanceMethod(D);
  bool IV = HP && isFunctionOrMethodVariadic(D);
  unsigned NumParams =
      (HP ? getFunctionOrMethodNumParams(D) : 0) + HasImplicitThisParam;

  llvm::APSInt IdxInt;
  if (IdxExpr->isTypeDependent() || IdxExpr->isValueDependent() ||
      !IdxExpr->isIntegerConstantExpr(IdxInt, S.Context)) {
    S.Diag(getAttrLoc(AI), diag::err_attribute_argument_n_type)
        << &AI << AttrArgNum << AANT_ArgumentIntegerConstant
        << IdxExpr->getSourceRange();
    return false;
  }

  unsigned IdxSource = IdxInt.getLimitedValue(UINT_MAX);
  if (IdxSource < 1 || (!IV && IdxSource > NumParams)) {
    S.Diag(getAttrLoc(AI), diag::err_attribute_argument_out_of_bounds)
        << &AI << AttrArgNum << IdxExpr->getSourceRange();
    return false;
  }
  if (HasImplicitThisParam && !CanIndexImplicitThis) {
    if (IdxSource == 1) {
      S.Diag(getAttrLoc(AI), diag::err_attribute_invalid_implicit_this_argument)
          << &AI << IdxExpr->getSourceRange();
      return false;
    }
  }

  Idx = ParamIdx(IdxSource, D);
  return true;
}

/// Check if the argument \p ArgNum of \p Attr is a ASCII string literal.
/// If not emit an error and return false. If the argument is an identifier it
/// will emit an error with a fixit hint and treat it as if it was a string
/// literal.
bool Sema::checkStringLiteralArgumentAttr(const ParsedAttr &AL, unsigned ArgNum,
                                          StringRef &Str,
                                          SourceLocation *ArgLocation) {
  // Look for identifiers. If we have one emit a hint to fix it to a literal.
  if (AL.isArgIdent(ArgNum)) {
    IdentifierLoc *Loc = AL.getArgAsIdent(ArgNum);
    Diag(Loc->Loc, diag::err_attribute_argument_type)
        << AL << AANT_ArgumentString
        << FixItHint::CreateInsertion(Loc->Loc, "\"")
        << FixItHint::CreateInsertion(getLocForEndOfToken(Loc->Loc), "\"");
    Str = Loc->Ident->getName();
    if (ArgLocation)
      *ArgLocation = Loc->Loc;
    return true;
  }

  // Now check for an actual string literal.
  Expr *ArgExpr = AL.getArgAsExpr(ArgNum);
  const auto *Literal = dyn_cast<StringLiteral>(ArgExpr->IgnoreParenCasts());
  if (ArgLocation)
    *ArgLocation = ArgExpr->getBeginLoc();

  if (!Literal || !Literal->isAscii()) {
    Diag(ArgExpr->getBeginLoc(), diag::err_attribute_argument_type)
        << AL << AANT_ArgumentString;
    return false;
  }

  Str = Literal->getString();
  return true;
}

/// Applies the given attribute to the Decl without performing any
/// additional semantic checking.
template <typename AttrType>
static void handleSimpleAttribute(Sema &S, Decl *D, SourceRange SR,
                                  unsigned SpellingIndex) {
  D->addAttr(::new (S.Context) AttrType(SR, S.Context, SpellingIndex));
}

template <typename AttrType>
static void handleSimpleAttribute(Sema &S, Decl *D, const ParsedAttr &AL) {
  handleSimpleAttribute<AttrType>(S, D, AL.getRange(),
                                  AL.getAttributeSpellingListIndex());
}


template <typename... DiagnosticArgs>
static const Sema::SemaDiagnosticBuilder&
appendDiagnostics(const Sema::SemaDiagnosticBuilder &Bldr) {
  return Bldr;
}

template <typename T, typename... DiagnosticArgs>
static const Sema::SemaDiagnosticBuilder&
appendDiagnostics(const Sema::SemaDiagnosticBuilder &Bldr, T &&ExtraArg,
                  DiagnosticArgs &&... ExtraArgs) {
  return appendDiagnostics(Bldr << std::forward<T>(ExtraArg),
                           std::forward<DiagnosticArgs>(ExtraArgs)...);
}

/// Add an attribute {@code AttrType} to declaration {@code D}, provided that
/// {@code PassesCheck} is true.
/// Otherwise, emit diagnostic {@code DiagID}, passing in all parameters
/// specified in {@code ExtraArgs}.
template <typename AttrType, typename... DiagnosticArgs>
static void
handleSimpleAttributeOrDiagnose(Sema &S, Decl *D, SourceRange SR,
                               unsigned SpellingIndex,
                               bool PassesCheck,
                               unsigned DiagID, DiagnosticArgs&&... ExtraArgs) {
  if (!PassesCheck) {
    Sema::SemaDiagnosticBuilder DB = S.Diag(D->getBeginLoc(), DiagID);
    appendDiagnostics(DB, std::forward<DiagnosticArgs>(ExtraArgs)...);
    return;
  }
  handleSimpleAttribute<AttrType>(S, D, SR, SpellingIndex);
}

template <typename AttrType, typename... DiagnosticArgs>
static void
handleSimpleAttributeOrDiagnose(Sema &S, Decl *D, const ParsedAttr &AL,
                               bool PassesCheck,
                               unsigned DiagID,
                               DiagnosticArgs&&... ExtraArgs) {
  return handleSimpleAttributeOrDiagnose<AttrType>(
      S, D, AL.getRange(), AL.getAttributeSpellingListIndex(), PassesCheck,
      DiagID, std::forward<DiagnosticArgs>(ExtraArgs)...);
}

template <typename AttrType>
static void handleSimpleAttributeWithExclusions(Sema &S, Decl *D,
                                                const ParsedAttr &AL) {
  handleSimpleAttribute<AttrType>(S, D, AL);
}

/// Applies the given attribute to the Decl so long as the Decl doesn't
/// already have one of the given incompatible attributes.
template <typename AttrType, typename IncompatibleAttrType,
          typename... IncompatibleAttrTypes>
static void handleSimpleAttributeWithExclusions(Sema &S, Decl *D,
                                                const ParsedAttr &AL) {
  if (checkAttrMutualExclusion<IncompatibleAttrType>(S, D, AL))
    return;
  handleSimpleAttributeWithExclusions<AttrType, IncompatibleAttrTypes...>(S, D,
                                                                          AL);
}

/// Check if the passed-in expression is of type int or bool.
static bool isIntOrBool(Expr *Exp) {
  QualType QT = Exp->getType();
  return QT->isBooleanType() || QT->isIntegerType();
}


// Check to see if the type is a smart pointer of some kind.  We assume
// it's a smart pointer if it defines both operator-> and operator*.
static bool threadSafetyCheckIsSmartPointer(Sema &S, const RecordType* RT) {
  auto IsOverloadedOperatorPresent = [&S](const RecordDecl *Record,
                                          OverloadedOperatorKind Op) {
    DeclContextLookupResult Result =
        Record->lookup(S.Context.DeclarationNames.getCXXOperatorName(Op));
    return !Result.empty();
  };

  const RecordDecl *Record = RT->getDecl();
  bool foundStarOperator = IsOverloadedOperatorPresent(Record, OO_Star);
  bool foundArrowOperator = IsOverloadedOperatorPresent(Record, OO_Arrow);
  if (foundStarOperator && foundArrowOperator)
    return true;

  const CXXRecordDecl *CXXRecord = dyn_cast<CXXRecordDecl>(Record);
  if (!CXXRecord)
    return false;

  for (auto BaseSpecifier : CXXRecord->bases()) {
    if (!foundStarOperator)
      foundStarOperator = IsOverloadedOperatorPresent(
          BaseSpecifier.getType()->getAsRecordDecl(), OO_Star);
    if (!foundArrowOperator)
      foundArrowOperator = IsOverloadedOperatorPresent(
          BaseSpecifier.getType()->getAsRecordDecl(), OO_Arrow);
  }

  if (foundStarOperator && foundArrowOperator)
    return true;

  return false;
}

/// Check if passed in Decl is a pointer type.
/// Note that this function may produce an error message.
/// \return true if the Decl is a pointer type; false otherwise
static bool threadSafetyCheckIsPointer(Sema &S, const Decl *D,
                                       const ParsedAttr &AL) {
  const auto *VD = cast<ValueDecl>(D);
  QualType QT = VD->getType();
  if (QT->isAnyPointerType())
    return true;

  if (const auto *RT = QT->getAs<RecordType>()) {
    // If it's an incomplete type, it could be a smart pointer; skip it.
    // (We don't want to force template instantiation if we can avoid it,
    // since that would alter the order in which templates are instantiated.)
    if (RT->isIncompleteType())
      return true;

    if (threadSafetyCheckIsSmartPointer(S, RT))
      return true;
  }

  S.Diag(AL.getLoc(), diag::warn_thread_attribute_decl_not_pointer) << AL << QT;
  return false;
}

/// Checks that the passed in QualType either is of RecordType or points
/// to RecordType. Returns the relevant RecordType, null if it does not exit.
static const RecordType *getRecordType(QualType QT) {
  if (const auto *RT = QT->getAs<RecordType>())
    return RT;

  // Now check if we point to record type.
  if (const auto *PT = QT->getAs<PointerType>())
    return PT->getPointeeType()->getAs<RecordType>();

  return nullptr;
}

template <typename AttrType>
static bool checkRecordDeclForAttr(const RecordDecl *RD) {
  // Check if the record itself has the attribute.
  if (RD->hasAttr<AttrType>())
    return true;

  // Else check if any base classes have the attribute.
  if (const auto *CRD = dyn_cast<CXXRecordDecl>(RD)) {
    CXXBasePaths BPaths(false, false);
    if (CRD->lookupInBases(
            [](const CXXBaseSpecifier *BS, CXXBasePath &) {
              const auto &Ty = *BS->getType();
              // If it's type-dependent, we assume it could have the attribute.
              if (Ty.isDependentType())
                return true;
              return Ty.getAs<RecordType>()->getDecl()->hasAttr<AttrType>();
            },
            BPaths, true))
      return true;
  }
  return false;
}

static bool checkRecordTypeForCapability(Sema &S, QualType Ty) {
  const RecordType *RT = getRecordType(Ty);

  if (!RT)
    return false;

  // Don't check for the capability if the class hasn't been defined yet.
  if (RT->isIncompleteType())
    return true;

  // Allow smart pointers to be used as capability objects.
  // FIXME -- Check the type that the smart pointer points to.
  if (threadSafetyCheckIsSmartPointer(S, RT))
    return true;

  return checkRecordDeclForAttr<CapabilityAttr>(RT->getDecl());
}

static bool checkTypedefTypeForCapability(QualType Ty) {
  const auto *TD = Ty->getAs<TypedefType>();
  if (!TD)
    return false;

  TypedefNameDecl *TN = TD->getDecl();
  if (!TN)
    return false;

  return TN->hasAttr<CapabilityAttr>();
}

static bool typeHasCapability(Sema &S, QualType Ty) {
  if (checkTypedefTypeForCapability(Ty))
    return true;

  if (checkRecordTypeForCapability(S, Ty))
    return true;

  return false;
}

static bool isCapabilityExpr(Sema &S, const Expr *Ex) {
  // Capability expressions are simple expressions involving the boolean logic
  // operators &&, || or !, a simple DeclRefExpr, CastExpr or a ParenExpr. Once
  // a DeclRefExpr is found, its type should be checked to determine whether it
  // is a capability or not.

  if (const auto *E = dyn_cast<CastExpr>(Ex))
    return isCapabilityExpr(S, E->getSubExpr());
  else if (const auto *E = dyn_cast<ParenExpr>(Ex))
    return isCapabilityExpr(S, E->getSubExpr());
  else if (const auto *E = dyn_cast<UnaryOperator>(Ex)) {
    if (E->getOpcode() == UO_LNot || E->getOpcode() == UO_AddrOf ||
        E->getOpcode() == UO_Deref)
      return isCapabilityExpr(S, E->getSubExpr());
    return false;
  } else if (const auto *E = dyn_cast<BinaryOperator>(Ex)) {
    if (E->getOpcode() == BO_LAnd || E->getOpcode() == BO_LOr)
      return isCapabilityExpr(S, E->getLHS()) &&
             isCapabilityExpr(S, E->getRHS());
    return false;
  }

  return typeHasCapability(S, Ex->getType());
}

/// Checks that all attribute arguments, starting from Sidx, resolve to
/// a capability object.
/// \param Sidx The attribute argument index to start checking with.
/// \param ParamIdxOk Whether an argument can be indexing into a function
/// parameter list.
static void checkAttrArgsAreCapabilityObjs(Sema &S, Decl *D,
                                           const ParsedAttr &AL,
                                           SmallVectorImpl<Expr *> &Args,
                                           unsigned Sidx = 0,
                                           bool ParamIdxOk = false) {
  if (Sidx == AL.getNumArgs()) {
    // If we don't have any capability arguments, the attribute implicitly
    // refers to 'this'. So we need to make sure that 'this' exists, i.e. we're
    // a non-static method, and that the class is a (scoped) capability.
    const auto *MD = dyn_cast<const CXXMethodDecl>(D);
    if (MD && !MD->isStatic()) {
      const CXXRecordDecl *RD = MD->getParent();
      // FIXME -- need to check this again on template instantiation
      if (!checkRecordDeclForAttr<CapabilityAttr>(RD) &&
          !checkRecordDeclForAttr<ScopedLockableAttr>(RD))
        S.Diag(AL.getLoc(),
               diag::warn_thread_attribute_not_on_capability_member)
            << AL << MD->getParent();
    } else {
      S.Diag(AL.getLoc(), diag::warn_thread_attribute_not_on_non_static_member)
          << AL;
    }
  }

  for (unsigned Idx = Sidx; Idx < AL.getNumArgs(); ++Idx) {
    Expr *ArgExp = AL.getArgAsExpr(Idx);

    if (ArgExp->isTypeDependent()) {
      // FIXME -- need to check this again on template instantiation
      Args.push_back(ArgExp);
      continue;
    }

    if (const auto *StrLit = dyn_cast<StringLiteral>(ArgExp)) {
      if (StrLit->getLength() == 0 ||
          (StrLit->isAscii() && StrLit->getString() == StringRef("*"))) {
        // Pass empty strings to the analyzer without warnings.
        // Treat "*" as the universal lock.
        Args.push_back(ArgExp);
        continue;
      }

      // We allow constant strings to be used as a placeholder for expressions
      // that are not valid C++ syntax, but warn that they are ignored.
      S.Diag(AL.getLoc(), diag::warn_thread_attribute_ignored) << AL;
      Args.push_back(ArgExp);
      continue;
    }

    QualType ArgTy = ArgExp->getType();

    // A pointer to member expression of the form  &MyClass::mu is treated
    // specially -- we need to look at the type of the member.
    if (const auto *UOp = dyn_cast<UnaryOperator>(ArgExp))
      if (UOp->getOpcode() == UO_AddrOf)
        if (const auto *DRE = dyn_cast<DeclRefExpr>(UOp->getSubExpr()))
          if (DRE->getDecl()->isCXXInstanceMember())
            ArgTy = DRE->getDecl()->getType();

    // First see if we can just cast to record type, or pointer to record type.
    const RecordType *RT = getRecordType(ArgTy);

    // Now check if we index into a record type function param.
    if(!RT && ParamIdxOk) {
      const auto *FD = dyn_cast<FunctionDecl>(D);
      const auto *IL = dyn_cast<IntegerLiteral>(ArgExp);
      if(FD && IL) {
        unsigned int NumParams = FD->getNumParams();
        llvm::APInt ArgValue = IL->getValue();
        uint64_t ParamIdxFromOne = ArgValue.getZExtValue();
        uint64_t ParamIdxFromZero = ParamIdxFromOne - 1;
        if (!ArgValue.isStrictlyPositive() || ParamIdxFromOne > NumParams) {
          S.Diag(AL.getLoc(), diag::err_attribute_argument_out_of_range)
              << AL << Idx + 1 << NumParams;
          continue;
        }
        ArgTy = FD->getParamDecl(ParamIdxFromZero)->getType();
      }
    }

    // If the type does not have a capability, see if the components of the
    // expression have capabilities. This allows for writing C code where the
    // capability may be on the type, and the expression is a capability
    // boolean logic expression. Eg) requires_capability(A || B && !C)
    if (!typeHasCapability(S, ArgTy) && !isCapabilityExpr(S, ArgExp))
      S.Diag(AL.getLoc(), diag::warn_thread_attribute_argument_not_lockable)
          << AL << ArgTy;

    Args.push_back(ArgExp);
  }
}

//===----------------------------------------------------------------------===//
// Attribute Implementations
//===----------------------------------------------------------------------===//

static void handlePtGuardedVarAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (!threadSafetyCheckIsPointer(S, D, AL))
    return;

  D->addAttr(::new (S.Context)
             PtGuardedVarAttr(AL.getRange(), S.Context,
                              AL.getAttributeSpellingListIndex()));
}

static bool checkGuardedByAttrCommon(Sema &S, Decl *D, const ParsedAttr &AL,
                                     Expr *&Arg) {
  SmallVector<Expr *, 1> Args;
  // check that all arguments are lockable objects
  checkAttrArgsAreCapabilityObjs(S, D, AL, Args);
  unsigned Size = Args.size();
  if (Size != 1)
    return false;

  Arg = Args[0];

  return true;
}

static void handleGuardedByAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  Expr *Arg = nullptr;
  if (!checkGuardedByAttrCommon(S, D, AL, Arg))
    return;

  D->addAttr(::new (S.Context) GuardedByAttr(
      AL.getRange(), S.Context, Arg, AL.getAttributeSpellingListIndex()));
}

static void handlePtGuardedByAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  Expr *Arg = nullptr;
  if (!checkGuardedByAttrCommon(S, D, AL, Arg))
    return;

  if (!threadSafetyCheckIsPointer(S, D, AL))
    return;

  D->addAttr(::new (S.Context) PtGuardedByAttr(
      AL.getRange(), S.Context, Arg, AL.getAttributeSpellingListIndex()));
}

static bool checkAcquireOrderAttrCommon(Sema &S, Decl *D, const ParsedAttr &AL,
                                        SmallVectorImpl<Expr *> &Args) {
  if (!checkAttributeAtLeastNumArgs(S, AL, 1))
    return false;

  // Check that this attribute only applies to lockable types.
  QualType QT = cast<ValueDecl>(D)->getType();
  if (!QT->isDependentType() && !typeHasCapability(S, QT)) {
    S.Diag(AL.getLoc(), diag::warn_thread_attribute_decl_not_lockable) << AL;
    return false;
  }

  // Check that all arguments are lockable objects.
  checkAttrArgsAreCapabilityObjs(S, D, AL, Args);
  if (Args.empty())
    return false;

  return true;
}

static void handleAcquiredAfterAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  SmallVector<Expr *, 1> Args;
  if (!checkAcquireOrderAttrCommon(S, D, AL, Args))
    return;

  Expr **StartArg = &Args[0];
  D->addAttr(::new (S.Context) AcquiredAfterAttr(
      AL.getRange(), S.Context, StartArg, Args.size(),
      AL.getAttributeSpellingListIndex()));
}

static void handleAcquiredBeforeAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  SmallVector<Expr *, 1> Args;
  if (!checkAcquireOrderAttrCommon(S, D, AL, Args))
    return;

  Expr **StartArg = &Args[0];
  D->addAttr(::new (S.Context) AcquiredBeforeAttr(
      AL.getRange(), S.Context, StartArg, Args.size(),
      AL.getAttributeSpellingListIndex()));
}

static bool checkLockFunAttrCommon(Sema &S, Decl *D, const ParsedAttr &AL,
                                   SmallVectorImpl<Expr *> &Args) {
  // zero or more arguments ok
  // check that all arguments are lockable objects
  checkAttrArgsAreCapabilityObjs(S, D, AL, Args, 0, /*ParamIdxOk=*/true);

  return true;
}

static void handleAssertSharedLockAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  SmallVector<Expr *, 1> Args;
  if (!checkLockFunAttrCommon(S, D, AL, Args))
    return;

  unsigned Size = Args.size();
  Expr **StartArg = Size == 0 ? nullptr : &Args[0];
  D->addAttr(::new (S.Context)
                 AssertSharedLockAttr(AL.getRange(), S.Context, StartArg, Size,
                                      AL.getAttributeSpellingListIndex()));
}

static void handleAssertExclusiveLockAttr(Sema &S, Decl *D,
                                          const ParsedAttr &AL) {
  SmallVector<Expr *, 1> Args;
  if (!checkLockFunAttrCommon(S, D, AL, Args))
    return;

  unsigned Size = Args.size();
  Expr **StartArg = Size == 0 ? nullptr : &Args[0];
  D->addAttr(::new (S.Context) AssertExclusiveLockAttr(
      AL.getRange(), S.Context, StartArg, Size,
      AL.getAttributeSpellingListIndex()));
}

/// Checks to be sure that the given parameter number is in bounds, and
/// is an integral type. Will emit appropriate diagnostics if this returns
/// false.
///
/// AttrArgNo is used to actually retrieve the argument, so it's base-0.
template <typename AttrInfo>
static bool checkParamIsIntegerType(Sema &S, const FunctionDecl *FD,
                                    const AttrInfo &AI, unsigned AttrArgNo) {
  assert(AI.isArgExpr(AttrArgNo) && "Expected expression argument");
  Expr *AttrArg = AI.getArgAsExpr(AttrArgNo);
  ParamIdx Idx;
  if (!checkFunctionOrMethodParameterIndex(S, FD, AI, AttrArgNo + 1, AttrArg,
                                           Idx))
    return false;

  const ParmVarDecl *Param = FD->getParamDecl(Idx.getASTIndex());
  if (!Param->getType()->isIntegerType() && !Param->getType()->isCharType()) {
    SourceLocation SrcLoc = AttrArg->getBeginLoc();
    S.Diag(SrcLoc, diag::err_attribute_integers_only)
        << AI << Param->getSourceRange();
    return false;
  }
  return true;
}

static void handleAllocSizeAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (!checkAttributeAtLeastNumArgs(S, AL, 1) ||
      !checkAttributeAtMostNumArgs(S, AL, 2))
    return;

  const auto *FD = cast<FunctionDecl>(D);
  if (!FD->getReturnType()->isPointerType()) {
    S.Diag(AL.getLoc(), diag::warn_attribute_return_pointers_only) << AL;
    return;
  }

  const Expr *SizeExpr = AL.getArgAsExpr(0);
  int SizeArgNoVal;
  // Parameter indices are 1-indexed, hence Index=1
  if (!checkPositiveIntArgument(S, AL, SizeExpr, SizeArgNoVal, /*Index=*/1))
    return;
  if (!checkParamIsIntegerType(S, FD, AL, /*AttrArgNo=*/0))
    return;
  ParamIdx SizeArgNo(SizeArgNoVal, D);

  ParamIdx NumberArgNo;
  if (AL.getNumArgs() == 2) {
    const Expr *NumberExpr = AL.getArgAsExpr(1);
    int Val;
    // Parameter indices are 1-based, hence Index=2
    if (!checkPositiveIntArgument(S, AL, NumberExpr, Val, /*Index=*/2))
      return;
    if (!checkParamIsIntegerType(S, FD, AL, /*AttrArgNo=*/1))
      return;
    NumberArgNo = ParamIdx(Val, D);
  }

  D->addAttr(::new (S.Context)
                 AllocSizeAttr(AL.getRange(), S.Context, SizeArgNo, NumberArgNo,
                               AL.getAttributeSpellingListIndex()));
}

static bool checkTryLockFunAttrCommon(Sema &S, Decl *D, const ParsedAttr &AL,
                                      SmallVectorImpl<Expr *> &Args) {
  if (!checkAttributeAtLeastNumArgs(S, AL, 1))
    return false;

  if (!isIntOrBool(AL.getArgAsExpr(0))) {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_n_type)
        << AL << 1 << AANT_ArgumentIntOrBool;
    return false;
  }

  // check that all arguments are lockable objects
  checkAttrArgsAreCapabilityObjs(S, D, AL, Args, 1);

  return true;
}

static void handleSharedTrylockFunctionAttr(Sema &S, Decl *D,
                                            const ParsedAttr &AL) {
  SmallVector<Expr*, 2> Args;
  if (!checkTryLockFunAttrCommon(S, D, AL, Args))
    return;

  D->addAttr(::new (S.Context) SharedTrylockFunctionAttr(
      AL.getRange(), S.Context, AL.getArgAsExpr(0), Args.data(), Args.size(),
      AL.getAttributeSpellingListIndex()));
}

static void handleExclusiveTrylockFunctionAttr(Sema &S, Decl *D,
                                               const ParsedAttr &AL) {
  SmallVector<Expr*, 2> Args;
  if (!checkTryLockFunAttrCommon(S, D, AL, Args))
    return;

  D->addAttr(::new (S.Context) ExclusiveTrylockFunctionAttr(
      AL.getRange(), S.Context, AL.getArgAsExpr(0), Args.data(),
      Args.size(), AL.getAttributeSpellingListIndex()));
}

static void handleLockReturnedAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  // check that the argument is lockable object
  SmallVector<Expr*, 1> Args;
  checkAttrArgsAreCapabilityObjs(S, D, AL, Args);
  unsigned Size = Args.size();
  if (Size == 0)
    return;

  D->addAttr(::new (S.Context)
             LockReturnedAttr(AL.getRange(), S.Context, Args[0],
                              AL.getAttributeSpellingListIndex()));
}

static void handleLocksExcludedAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (!checkAttributeAtLeastNumArgs(S, AL, 1))
    return;

  // check that all arguments are lockable objects
  SmallVector<Expr*, 1> Args;
  checkAttrArgsAreCapabilityObjs(S, D, AL, Args);
  unsigned Size = Args.size();
  if (Size == 0)
    return;
  Expr **StartArg = &Args[0];

  D->addAttr(::new (S.Context)
             LocksExcludedAttr(AL.getRange(), S.Context, StartArg, Size,
                               AL.getAttributeSpellingListIndex()));
}

static bool checkFunctionConditionAttr(Sema &S, Decl *D, const ParsedAttr &AL,
                                       Expr *&Cond, StringRef &Msg) {
  Cond = AL.getArgAsExpr(0);
  if (!Cond->isTypeDependent()) {
    ExprResult Converted = S.PerformContextuallyConvertToBool(Cond);
    if (Converted.isInvalid())
      return false;
    Cond = Converted.get();
  }

  if (!S.checkStringLiteralArgumentAttr(AL, 1, Msg))
    return false;

  if (Msg.empty())
    Msg = "<no message provided>";

  SmallVector<PartialDiagnosticAt, 8> Diags;
  if (isa<FunctionDecl>(D) && !Cond->isValueDependent() &&
      !Expr::isPotentialConstantExprUnevaluated(Cond, cast<FunctionDecl>(D),
                                                Diags)) {
    S.Diag(AL.getLoc(), diag::err_attr_cond_never_constant_expr) << AL;
    for (const PartialDiagnosticAt &PDiag : Diags)
      S.Diag(PDiag.first, PDiag.second);
    return false;
  }
  return true;
}

static void handleEnableIfAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  S.Diag(AL.getLoc(), diag::ext_clang_enable_if);

  Expr *Cond;
  StringRef Msg;
  if (checkFunctionConditionAttr(S, D, AL, Cond, Msg))
    D->addAttr(::new (S.Context)
                   EnableIfAttr(AL.getRange(), S.Context, Cond, Msg,
                                AL.getAttributeSpellingListIndex()));
}

namespace {
/// Determines if a given Expr references any of the given function's
/// ParmVarDecls, or the function's implicit `this` parameter (if applicable).
class ArgumentDependenceChecker
    : public RecursiveASTVisitor<ArgumentDependenceChecker> {
#ifndef NDEBUG
  const CXXRecordDecl *ClassType;
#endif
  llvm::SmallPtrSet<const ParmVarDecl *, 16> Parms;
  bool Result;

public:
  ArgumentDependenceChecker(const FunctionDecl *FD) {
#ifndef NDEBUG
    if (const auto *MD = dyn_cast<CXXMethodDecl>(FD))
      ClassType = MD->getParent();
    else
      ClassType = nullptr;
#endif
    Parms.insert(FD->param_begin(), FD->param_end());
  }

  bool referencesArgs(Expr *E) {
    Result = false;
    TraverseStmt(E);
    return Result;
  }

  bool VisitCXXThisExpr(CXXThisExpr *E) {
    assert(E->getType()->getPointeeCXXRecordDecl() == ClassType &&
           "`this` doesn't refer to the enclosing class?");
    Result = true;
    return false;
  }

  bool VisitDeclRefExpr(DeclRefExpr *DRE) {
    if (const auto *PVD = dyn_cast<ParmVarDecl>(DRE->getDecl()))
      if (Parms.count(PVD)) {
        Result = true;
        return false;
      }
    return true;
  }
};
}

static void handleDiagnoseIfAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  S.Diag(AL.getLoc(), diag::ext_clang_diagnose_if);

  Expr *Cond;
  StringRef Msg;
  if (!checkFunctionConditionAttr(S, D, AL, Cond, Msg))
    return;

  StringRef DiagTypeStr;
  if (!S.checkStringLiteralArgumentAttr(AL, 2, DiagTypeStr))
    return;

  DiagnoseIfAttr::DiagnosticType DiagType;
  if (!DiagnoseIfAttr::ConvertStrToDiagnosticType(DiagTypeStr, DiagType)) {
    S.Diag(AL.getArgAsExpr(2)->getBeginLoc(),
           diag::err_diagnose_if_invalid_diagnostic_type);
    return;
  }

  bool ArgDependent = false;
  if (const auto *FD = dyn_cast<FunctionDecl>(D))
    ArgDependent = ArgumentDependenceChecker(FD).referencesArgs(Cond);
  D->addAttr(::new (S.Context) DiagnoseIfAttr(
      AL.getRange(), S.Context, Cond, Msg, DiagType, ArgDependent,
      cast<NamedDecl>(D), AL.getAttributeSpellingListIndex()));
}

static void handlePassObjectSizeAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (D->hasAttr<PassObjectSizeAttr>()) {
    S.Diag(D->getBeginLoc(), diag::err_attribute_only_once_per_parameter) << AL;
    return;
  }

  Expr *E = AL.getArgAsExpr(0);
  uint32_t Type;
  if (!checkUInt32Argument(S, AL, E, Type, /*Idx=*/1))
    return;

  // pass_object_size's argument is passed in as the second argument of
  // __builtin_object_size. So, it has the same constraints as that second
  // argument; namely, it must be in the range [0, 3].
  if (Type > 3) {
    S.Diag(E->getBeginLoc(), diag::err_attribute_argument_outof_range)
        << AL << 0 << 3 << E->getSourceRange();
    return;
  }

  // pass_object_size is only supported on constant pointer parameters; as a
  // kindness to users, we allow the parameter to be non-const for declarations.
  // At this point, we have no clue if `D` belongs to a function declaration or
  // definition, so we defer the constness check until later.
  if (!cast<ParmVarDecl>(D)->getType()->isPointerType()) {
    S.Diag(D->getBeginLoc(), diag::err_attribute_pointers_only) << AL << 1;
    return;
  }

  D->addAttr(::new (S.Context) PassObjectSizeAttr(
      AL.getRange(), S.Context, (int)Type, AL.getAttributeSpellingListIndex()));
}

static void handleConsumableAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  ConsumableAttr::ConsumedState DefaultState;

  if (AL.isArgIdent(0)) {
    IdentifierLoc *IL = AL.getArgAsIdent(0);
    if (!ConsumableAttr::ConvertStrToConsumedState(IL->Ident->getName(),
                                                   DefaultState)) {
      S.Diag(IL->Loc, diag::warn_attribute_type_not_supported) << AL
                                                               << IL->Ident;
      return;
    }
  } else {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_type)
        << AL << AANT_ArgumentIdentifier;
    return;
  }

  D->addAttr(::new (S.Context)
             ConsumableAttr(AL.getRange(), S.Context, DefaultState,
                            AL.getAttributeSpellingListIndex()));
}

static bool checkForConsumableClass(Sema &S, const CXXMethodDecl *MD,
                                    const ParsedAttr &AL) {
  QualType ThisType = MD->getThisType()->getPointeeType();

  if (const CXXRecordDecl *RD = ThisType->getAsCXXRecordDecl()) {
    if (!RD->hasAttr<ConsumableAttr>()) {
      S.Diag(AL.getLoc(), diag::warn_attr_on_unconsumable_class) <<
        RD->getNameAsString();

      return false;
    }
  }

  return true;
}

static void handleCallableWhenAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (!checkAttributeAtLeastNumArgs(S, AL, 1))
    return;

  if (!checkForConsumableClass(S, cast<CXXMethodDecl>(D), AL))
    return;

  SmallVector<CallableWhenAttr::ConsumedState, 3> States;
  for (unsigned ArgIndex = 0; ArgIndex < AL.getNumArgs(); ++ArgIndex) {
    CallableWhenAttr::ConsumedState CallableState;

    StringRef StateString;
    SourceLocation Loc;
    if (AL.isArgIdent(ArgIndex)) {
      IdentifierLoc *Ident = AL.getArgAsIdent(ArgIndex);
      StateString = Ident->Ident->getName();
      Loc = Ident->Loc;
    } else {
      if (!S.checkStringLiteralArgumentAttr(AL, ArgIndex, StateString, &Loc))
        return;
    }

    if (!CallableWhenAttr::ConvertStrToConsumedState(StateString,
                                                     CallableState)) {
      S.Diag(Loc, diag::warn_attribute_type_not_supported) << AL << StateString;
      return;
    }

    States.push_back(CallableState);
  }

  D->addAttr(::new (S.Context)
             CallableWhenAttr(AL.getRange(), S.Context, States.data(),
               States.size(), AL.getAttributeSpellingListIndex()));
}

static void handleParamTypestateAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  ParamTypestateAttr::ConsumedState ParamState;

  if (AL.isArgIdent(0)) {
    IdentifierLoc *Ident = AL.getArgAsIdent(0);
    StringRef StateString = Ident->Ident->getName();

    if (!ParamTypestateAttr::ConvertStrToConsumedState(StateString,
                                                       ParamState)) {
      S.Diag(Ident->Loc, diag::warn_attribute_type_not_supported)
          << AL << StateString;
      return;
    }
  } else {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_type)
        << AL << AANT_ArgumentIdentifier;
    return;
  }

  // FIXME: This check is currently being done in the analysis.  It can be
  //        enabled here only after the parser propagates attributes at
  //        template specialization definition, not declaration.
  //QualType ReturnType = cast<ParmVarDecl>(D)->getType();
  //const CXXRecordDecl *RD = ReturnType->getAsCXXRecordDecl();
  //
  //if (!RD || !RD->hasAttr<ConsumableAttr>()) {
  //    S.Diag(AL.getLoc(), diag::warn_return_state_for_unconsumable_type) <<
  //      ReturnType.getAsString();
  //    return;
  //}

  D->addAttr(::new (S.Context)
             ParamTypestateAttr(AL.getRange(), S.Context, ParamState,
                                AL.getAttributeSpellingListIndex()));
}

static void handleReturnTypestateAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  ReturnTypestateAttr::ConsumedState ReturnState;

  if (AL.isArgIdent(0)) {
    IdentifierLoc *IL = AL.getArgAsIdent(0);
    if (!ReturnTypestateAttr::ConvertStrToConsumedState(IL->Ident->getName(),
                                                        ReturnState)) {
      S.Diag(IL->Loc, diag::warn_attribute_type_not_supported) << AL
                                                               << IL->Ident;
      return;
    }
  } else {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_type)
        << AL << AANT_ArgumentIdentifier;
    return;
  }

  // FIXME: This check is currently being done in the analysis.  It can be
  //        enabled here only after the parser propagates attributes at
  //        template specialization definition, not declaration.
  //QualType ReturnType;
  //
  //if (const ParmVarDecl *Param = dyn_cast<ParmVarDecl>(D)) {
  //  ReturnType = Param->getType();
  //
  //} else if (const CXXConstructorDecl *Constructor =
  //             dyn_cast<CXXConstructorDecl>(D)) {
  //  ReturnType = Constructor->getThisType()->getPointeeType();
  //
  //} else {
  //
  //  ReturnType = cast<FunctionDecl>(D)->getCallResultType();
  //}
  //
  //const CXXRecordDecl *RD = ReturnType->getAsCXXRecordDecl();
  //
  //if (!RD || !RD->hasAttr<ConsumableAttr>()) {
  //    S.Diag(Attr.getLoc(), diag::warn_return_state_for_unconsumable_type) <<
  //      ReturnType.getAsString();
  //    return;
  //}

  D->addAttr(::new (S.Context)
                 ReturnTypestateAttr(AL.getRange(), S.Context, ReturnState,
                                     AL.getAttributeSpellingListIndex()));
}

static void handleSetTypestateAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (!checkForConsumableClass(S, cast<CXXMethodDecl>(D), AL))
    return;

  SetTypestateAttr::ConsumedState NewState;
  if (AL.isArgIdent(0)) {
    IdentifierLoc *Ident = AL.getArgAsIdent(0);
    StringRef Param = Ident->Ident->getName();
    if (!SetTypestateAttr::ConvertStrToConsumedState(Param, NewState)) {
      S.Diag(Ident->Loc, diag::warn_attribute_type_not_supported) << AL
                                                                  << Param;
      return;
    }
  } else {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_type)
        << AL << AANT_ArgumentIdentifier;
    return;
  }

  D->addAttr(::new (S.Context)
             SetTypestateAttr(AL.getRange(), S.Context, NewState,
                              AL.getAttributeSpellingListIndex()));
}

static void handleTestTypestateAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (!checkForConsumableClass(S, cast<CXXMethodDecl>(D), AL))
    return;

  TestTypestateAttr::ConsumedState TestState;
  if (AL.isArgIdent(0)) {
    IdentifierLoc *Ident = AL.getArgAsIdent(0);
    StringRef Param = Ident->Ident->getName();
    if (!TestTypestateAttr::ConvertStrToConsumedState(Param, TestState)) {
      S.Diag(Ident->Loc, diag::warn_attribute_type_not_supported) << AL
                                                                  << Param;
      return;
    }
  } else {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_type)
        << AL << AANT_ArgumentIdentifier;
    return;
  }

  D->addAttr(::new (S.Context)
             TestTypestateAttr(AL.getRange(), S.Context, TestState,
                                AL.getAttributeSpellingListIndex()));
}

static void handleExtVectorTypeAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  // Remember this typedef decl, we will need it later for diagnostics.
  S.ExtVectorDecls.push_back(cast<TypedefNameDecl>(D));
}

static void handlePackedAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (auto *TD = dyn_cast<TagDecl>(D))
    TD->addAttr(::new (S.Context) PackedAttr(AL.getRange(), S.Context,
                                        AL.getAttributeSpellingListIndex()));
  else if (auto *FD = dyn_cast<FieldDecl>(D)) {
    bool BitfieldByteAligned = (!FD->getType()->isDependentType() &&
                                !FD->getType()->isIncompleteType() &&
                                FD->isBitField() &&
                                S.Context.getTypeAlign(FD->getType()) <= 8);

    if (S.getASTContext().getTargetInfo().getTriple().isPS4()) {
      if (BitfieldByteAligned)
        // The PS4 target needs to maintain ABI backwards compatibility.
        S.Diag(AL.getLoc(), diag::warn_attribute_ignored_for_field_of_type)
            << AL << FD->getType();
      else
        FD->addAttr(::new (S.Context) PackedAttr(
                    AL.getRange(), S.Context, AL.getAttributeSpellingListIndex()));
    } else {
      // Report warning about changed offset in the newer compiler versions.
      if (BitfieldByteAligned)
        S.Diag(AL.getLoc(), diag::warn_attribute_packed_for_bitfield);

      FD->addAttr(::new (S.Context) PackedAttr(
                  AL.getRange(), S.Context, AL.getAttributeSpellingListIndex()));
    }

  } else
    S.Diag(AL.getLoc(), diag::warn_attribute_ignored) << AL;
}

static bool checkIBOutletCommon(Sema &S, Decl *D, const ParsedAttr &AL) {
  // The IBOutlet/IBOutletCollection attributes only apply to instance
  // variables or properties of Objective-C classes.  The outlet must also
  // have an object reference type.
  if (const auto *VD = dyn_cast<ObjCIvarDecl>(D)) {
    if (!VD->getType()->getAs<ObjCObjectPointerType>()) {
      S.Diag(AL.getLoc(), diag::warn_iboutlet_object_type)
          << AL << VD->getType() << 0;
      return false;
    }
  }
  else if (const auto *PD = dyn_cast<ObjCPropertyDecl>(D)) {
    if (!PD->getType()->getAs<ObjCObjectPointerType>()) {
      S.Diag(AL.getLoc(), diag::warn_iboutlet_object_type)
          << AL << PD->getType() << 1;
      return false;
    }
  }
  else {
    S.Diag(AL.getLoc(), diag::warn_attribute_iboutlet) << AL;
    return false;
  }

  return true;
}

static void handleIBOutlet(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (!checkIBOutletCommon(S, D, AL))
    return;

  D->addAttr(::new (S.Context)
             IBOutletAttr(AL.getRange(), S.Context,
                          AL.getAttributeSpellingListIndex()));
}

static void handleIBOutletCollection(Sema &S, Decl *D, const ParsedAttr &AL) {

  // The iboutletcollection attribute can have zero or one arguments.
  if (AL.getNumArgs() > 1) {
    S.Diag(AL.getLoc(), diag::err_attribute_wrong_number_arguments) << AL << 1;
    return;
  }

  if (!checkIBOutletCommon(S, D, AL))
    return;

  ParsedType PT;

  if (AL.hasParsedType())
    PT = AL.getTypeArg();
  else {
    PT = S.getTypeName(S.Context.Idents.get("NSObject"), AL.getLoc(),
                       S.getScopeForContext(D->getDeclContext()->getParent()));
    if (!PT) {
      S.Diag(AL.getLoc(), diag::err_iboutletcollection_type) << "NSObject";
      return;
    }
  }

  TypeSourceInfo *QTLoc = nullptr;
  QualType QT = S.GetTypeFromParser(PT, &QTLoc);
  if (!QTLoc)
    QTLoc = S.Context.getTrivialTypeSourceInfo(QT, AL.getLoc());

  // Diagnose use of non-object type in iboutletcollection attribute.
  // FIXME. Gnu attribute extension ignores use of builtin types in
  // attributes. So, __attribute__((iboutletcollection(char))) will be
  // treated as __attribute__((iboutletcollection())).
  if (!QT->isObjCIdType() && !QT->isObjCObjectType()) {
    S.Diag(AL.getLoc(),
           QT->isBuiltinType() ? diag::err_iboutletcollection_builtintype
                               : diag::err_iboutletcollection_type) << QT;
    return;
  }

  D->addAttr(::new (S.Context)
             IBOutletCollectionAttr(AL.getRange(), S.Context, QTLoc,
                                    AL.getAttributeSpellingListIndex()));
}

bool Sema::isValidPointerAttrType(QualType T, bool RefOkay) {
  if (RefOkay) {
    if (T->isReferenceType())
      return true;
  } else {
    T = T.getNonReferenceType();
  }

  // The nonnull attribute, and other similar attributes, can be applied to a
  // transparent union that contains a pointer type.
  if (const RecordType *UT = T->getAsUnionType()) {
    if (UT && UT->getDecl()->hasAttr<TransparentUnionAttr>()) {
      RecordDecl *UD = UT->getDecl();
      for (const auto *I : UD->fields()) {
        QualType QT = I->getType();
        if (QT->isAnyPointerType() || QT->isBlockPointerType())
          return true;
      }
    }
  }

  return T->isAnyPointerType() || T->isBlockPointerType();
}

static bool attrNonNullArgCheck(Sema &S, QualType T, const ParsedAttr &AL,
                                SourceRange AttrParmRange,
                                SourceRange TypeRange,
                                bool isReturnValue = false) {
  if (!S.isValidPointerAttrType(T)) {
    if (isReturnValue)
      S.Diag(AL.getLoc(), diag::warn_attribute_return_pointers_only)
          << AL << AttrParmRange << TypeRange;
    else
      S.Diag(AL.getLoc(), diag::warn_attribute_pointers_only)
          << AL << AttrParmRange << TypeRange << 0;
    return false;
  }
  return true;
}

static void handleNonNullAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  SmallVector<ParamIdx, 8> NonNullArgs;
  for (unsigned I = 0; I < AL.getNumArgs(); ++I) {
    Expr *Ex = AL.getArgAsExpr(I);
    ParamIdx Idx;
    if (!checkFunctionOrMethodParameterIndex(S, D, AL, I + 1, Ex, Idx))
      return;

    // Is the function argument a pointer type?
    if (Idx.getASTIndex() < getFunctionOrMethodNumParams(D) &&
        !attrNonNullArgCheck(
            S, getFunctionOrMethodParamType(D, Idx.getASTIndex()), AL,
            Ex->getSourceRange(),
            getFunctionOrMethodParamRange(D, Idx.getASTIndex())))
      continue;

    NonNullArgs.push_back(Idx);
  }

  // If no arguments were specified to __attribute__((nonnull)) then all pointer
  // arguments have a nonnull attribute; warn if there aren't any. Skip this
  // check if the attribute came from a macro expansion or a template
  // instantiation.
  if (NonNullArgs.empty() && AL.getLoc().isFileID() &&
      !S.inTemplateInstantiation()) {
    bool AnyPointers = isFunctionOrMethodVariadic(D);
    for (unsigned I = 0, E = getFunctionOrMethodNumParams(D);
         I != E && !AnyPointers; ++I) {
      QualType T = getFunctionOrMethodParamType(D, I);
      if (T->isDependentType() || S.isValidPointerAttrType(T))
        AnyPointers = true;
    }

    if (!AnyPointers)
      S.Diag(AL.getLoc(), diag::warn_attribute_nonnull_no_pointers);
  }

  ParamIdx *Start = NonNullArgs.data();
  unsigned Size = NonNullArgs.size();
  llvm::array_pod_sort(Start, Start + Size);
  D->addAttr(::new (S.Context)
                 NonNullAttr(AL.getRange(), S.Context, Start, Size,
                             AL.getAttributeSpellingListIndex()));
}

static void handleNonNullAttrParameter(Sema &S, ParmVarDecl *D,
                                       const ParsedAttr &AL) {
  if (AL.getNumArgs() > 0) {
    if (D->getFunctionType()) {
      handleNonNullAttr(S, D, AL);
    } else {
      S.Diag(AL.getLoc(), diag::warn_attribute_nonnull_parm_no_args)
        << D->getSourceRange();
    }
    return;
  }

  // Is the argument a pointer type?
  if (!attrNonNullArgCheck(S, D->getType(), AL, SourceRange(),
                           D->getSourceRange()))
    return;

  D->addAttr(::new (S.Context)
                 NonNullAttr(AL.getRange(), S.Context, nullptr, 0,
                             AL.getAttributeSpellingListIndex()));
}

static void handleReturnsNonNullAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  QualType ResultType = getFunctionOrMethodResultType(D);
  SourceRange SR = getFunctionOrMethodResultSourceRange(D);
  if (!attrNonNullArgCheck(S, ResultType, AL, SourceRange(), SR,
                           /* isReturnValue */ true))
    return;

  D->addAttr(::new (S.Context)
            ReturnsNonNullAttr(AL.getRange(), S.Context,
                               AL.getAttributeSpellingListIndex()));
}

static void handleNoEscapeAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (D->isInvalidDecl())
    return;

  // noescape only applies to pointer types.
  QualType T = cast<ParmVarDecl>(D)->getType();
  if (!S.isValidPointerAttrType(T, /* RefOkay */ true)) {
    S.Diag(AL.getLoc(), diag::warn_attribute_pointers_only)
        << AL << AL.getRange() << 0;
    return;
  }

  D->addAttr(::new (S.Context) NoEscapeAttr(
      AL.getRange(), S.Context, AL.getAttributeSpellingListIndex()));
}

static void handleAssumeAlignedAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  Expr *E = AL.getArgAsExpr(0),
       *OE = AL.getNumArgs() > 1 ? AL.getArgAsExpr(1) : nullptr;
  S.AddAssumeAlignedAttr(AL.getRange(), D, E, OE,
                         AL.getAttributeSpellingListIndex());
}

static void handleAllocAlignAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  S.AddAllocAlignAttr(AL.getRange(), D, AL.getArgAsExpr(0),
                      AL.getAttributeSpellingListIndex());
}

void Sema::AddAssumeAlignedAttr(SourceRange AttrRange, Decl *D, Expr *E,
                                Expr *OE, unsigned SpellingListIndex) {
  QualType ResultType = getFunctionOrMethodResultType(D);
  SourceRange SR = getFunctionOrMethodResultSourceRange(D);

  AssumeAlignedAttr TmpAttr(AttrRange, Context, E, OE, SpellingListIndex);
  SourceLocation AttrLoc = AttrRange.getBegin();

  if (!isValidPointerAttrType(ResultType, /* RefOkay */ true)) {
    Diag(AttrLoc, diag::warn_attribute_return_pointers_refs_only)
      << &TmpAttr << AttrRange << SR;
    return;
  }

  if (!E->isValueDependent()) {
    llvm::APSInt I(64);
    if (!E->isIntegerConstantExpr(I, Context)) {
      if (OE)
        Diag(AttrLoc, diag::err_attribute_argument_n_type)
          << &TmpAttr << 1 << AANT_ArgumentIntegerConstant
          << E->getSourceRange();
      else
        Diag(AttrLoc, diag::err_attribute_argument_type)
          << &TmpAttr << AANT_ArgumentIntegerConstant
          << E->getSourceRange();
      return;
    }

    if (!I.isPowerOf2()) {
      Diag(AttrLoc, diag::err_alignment_not_power_of_two)
        << E->getSourceRange();
      return;
    }
  }

  if (OE) {
    if (!OE->isValueDependent()) {
      llvm::APSInt I(64);
      if (!OE->isIntegerConstantExpr(I, Context)) {
        Diag(AttrLoc, diag::err_attribute_argument_n_type)
          << &TmpAttr << 2 << AANT_ArgumentIntegerConstant
          << OE->getSourceRange();
        return;
      }
    }
  }

  D->addAttr(::new (Context)
            AssumeAlignedAttr(AttrRange, Context, E, OE, SpellingListIndex));
}

void Sema::AddAllocAlignAttr(SourceRange AttrRange, Decl *D, Expr *ParamExpr,
                             unsigned SpellingListIndex) {
  QualType ResultType = getFunctionOrMethodResultType(D);

  AllocAlignAttr TmpAttr(AttrRange, Context, ParamIdx(), SpellingListIndex);
  SourceLocation AttrLoc = AttrRange.getBegin();

  if (!ResultType->isDependentType() &&
      !isValidPointerAttrType(ResultType, /* RefOkay */ true)) {
    Diag(AttrLoc, diag::warn_attribute_return_pointers_refs_only)
        << &TmpAttr << AttrRange << getFunctionOrMethodResultSourceRange(D);
    return;
  }

  ParamIdx Idx;
  const auto *FuncDecl = cast<FunctionDecl>(D);
  if (!checkFunctionOrMethodParameterIndex(*this, FuncDecl, TmpAttr,
                                           /*AttrArgNo=*/1, ParamExpr, Idx))
    return;

  QualType Ty = getFunctionOrMethodParamType(D, Idx.getASTIndex());
  if (!Ty->isDependentType() && !Ty->isIntegralType(Context)) {
    Diag(ParamExpr->getBeginLoc(), diag::err_attribute_integers_only)
        << &TmpAttr
        << FuncDecl->getParamDecl(Idx.getASTIndex())->getSourceRange();
    return;
  }

  D->addAttr(::new (Context)
                 AllocAlignAttr(AttrRange, Context, Idx, SpellingListIndex));
}

/// Normalize the attribute, __foo__ becomes foo.
/// Returns true if normalization was applied.
static bool normalizeName(StringRef &AttrName) {
  if (AttrName.size() > 4 && AttrName.startswith("__") &&
      AttrName.endswith("__")) {
    AttrName = AttrName.drop_front(2).drop_back(2);
    return true;
  }
  return false;
}

static void handleOwnershipAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  // This attribute must be applied to a function declaration. The first
  // argument to the attribute must be an identifier, the name of the resource,
  // for example: malloc. The following arguments must be argument indexes, the
  // arguments must be of integer type for Returns, otherwise of pointer type.
  // The difference between Holds and Takes is that a pointer may still be used
  // after being held. free() should be __attribute((ownership_takes)), whereas
  // a list append function may well be __attribute((ownership_holds)).

  if (!AL.isArgIdent(0)) {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_n_type)
        << AL << 1 << AANT_ArgumentIdentifier;
    return;
  }

  // Figure out our Kind.
  OwnershipAttr::OwnershipKind K =
      OwnershipAttr(AL.getLoc(), S.Context, nullptr, nullptr, 0,
                    AL.getAttributeSpellingListIndex()).getOwnKind();

  // Check arguments.
  switch (K) {
  case OwnershipAttr::Takes:
  case OwnershipAttr::Holds:
    if (AL.getNumArgs() < 2) {
      S.Diag(AL.getLoc(), diag::err_attribute_too_few_arguments) << AL << 2;
      return;
    }
    break;
  case OwnershipAttr::Returns:
    if (AL.getNumArgs() > 2) {
      S.Diag(AL.getLoc(), diag::err_attribute_too_many_arguments) << AL << 1;
      return;
    }
    break;
  }

  IdentifierInfo *Module = AL.getArgAsIdent(0)->Ident;

  StringRef ModuleName = Module->getName();
  if (normalizeName(ModuleName)) {
    Module = &S.PP.getIdentifierTable().get(ModuleName);
  }

  SmallVector<ParamIdx, 8> OwnershipArgs;
  for (unsigned i = 1; i < AL.getNumArgs(); ++i) {
    Expr *Ex = AL.getArgAsExpr(i);
    ParamIdx Idx;
    if (!checkFunctionOrMethodParameterIndex(S, D, AL, i, Ex, Idx))
      return;

    // Is the function argument a pointer type?
    QualType T = getFunctionOrMethodParamType(D, Idx.getASTIndex());
    int Err = -1;  // No error
    switch (K) {
      case OwnershipAttr::Takes:
      case OwnershipAttr::Holds:
        if (!T->isAnyPointerType() && !T->isBlockPointerType())
          Err = 0;
        break;
      case OwnershipAttr::Returns:
        if (!T->isIntegerType())
          Err = 1;
        break;
    }
    if (-1 != Err) {
      S.Diag(AL.getLoc(), diag::err_ownership_type) << AL << Err
                                                    << Ex->getSourceRange();
      return;
    }

    // Check we don't have a conflict with another ownership attribute.
    for (const auto *I : D->specific_attrs<OwnershipAttr>()) {
      // Cannot have two ownership attributes of different kinds for the same
      // index.
      if (I->getOwnKind() != K && I->args_end() !=
          std::find(I->args_begin(), I->args_end(), Idx)) {
        S.Diag(AL.getLoc(), diag::err_attributes_are_not_compatible) << AL << I;
        return;
      } else if (K == OwnershipAttr::Returns &&
                 I->getOwnKind() == OwnershipAttr::Returns) {
        // A returns attribute conflicts with any other returns attribute using
        // a different index.
        if (std::find(I->args_begin(), I->args_end(), Idx) == I->args_end()) {
          S.Diag(I->getLocation(), diag::err_ownership_returns_index_mismatch)
              << I->args_begin()->getSourceIndex();
          if (I->args_size())
            S.Diag(AL.getLoc(), diag::note_ownership_returns_index_mismatch)
                << Idx.getSourceIndex() << Ex->getSourceRange();
          return;
        }
      }
    }
    OwnershipArgs.push_back(Idx);
  }

  ParamIdx *Start = OwnershipArgs.data();
  unsigned Size = OwnershipArgs.size();
  llvm::array_pod_sort(Start, Start + Size);
  D->addAttr(::new (S.Context)
                 OwnershipAttr(AL.getLoc(), S.Context, Module, Start, Size,
                               AL.getAttributeSpellingListIndex()));
}

static void handleWeakRefAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  // Check the attribute arguments.
  if (AL.getNumArgs() > 1) {
    S.Diag(AL.getLoc(), diag::err_attribute_wrong_number_arguments) << AL << 1;
    return;
  }

  // gcc rejects
  // class c {
  //   static int a __attribute__((weakref ("v2")));
  //   static int b() __attribute__((weakref ("f3")));
  // };
  // and ignores the attributes of
  // void f(void) {
  //   static int a __attribute__((weakref ("v2")));
  // }
  // we reject them
  const DeclContext *Ctx = D->getDeclContext()->getRedeclContext();
  if (!Ctx->isFileContext()) {
    S.Diag(AL.getLoc(), diag::err_attribute_weakref_not_global_context)
        << cast<NamedDecl>(D);
    return;
  }

  // The GCC manual says
  //
  // At present, a declaration to which `weakref' is attached can only
  // be `static'.
  //
  // It also says
  //
  // Without a TARGET,
  // given as an argument to `weakref' or to `alias', `weakref' is
  // equivalent to `weak'.
  //
  // gcc 4.4.1 will accept
  // int a7 __attribute__((weakref));
  // as
  // int a7 __attribute__((weak));
  // This looks like a bug in gcc. We reject that for now. We should revisit
  // it if this behaviour is actually used.

  // GCC rejects
  // static ((alias ("y"), weakref)).
  // Should we? How to check that weakref is before or after alias?

  // FIXME: it would be good for us to keep the WeakRefAttr as-written instead
  // of transforming it into an AliasAttr.  The WeakRefAttr never uses the
  // StringRef parameter it was given anyway.
  StringRef Str;
  if (AL.getNumArgs() && S.checkStringLiteralArgumentAttr(AL, 0, Str))
    // GCC will accept anything as the argument of weakref. Should we
    // check for an existing decl?
    D->addAttr(::new (S.Context) AliasAttr(AL.getRange(), S.Context, Str,
                                        AL.getAttributeSpellingListIndex()));

  D->addAttr(::new (S.Context)
             WeakRefAttr(AL.getRange(), S.Context,
                         AL.getAttributeSpellingListIndex()));
}

static void handleIFuncAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  StringRef Str;
  if (!S.checkStringLiteralArgumentAttr(AL, 0, Str))
    return;

  // Aliases should be on declarations, not definitions.
  const auto *FD = cast<FunctionDecl>(D);
  if (FD->isThisDeclarationADefinition()) {
    S.Diag(AL.getLoc(), diag::err_alias_is_definition) << FD << 1;
    return;
  }

  D->addAttr(::new (S.Context) IFuncAttr(AL.getRange(), S.Context, Str,
                                         AL.getAttributeSpellingListIndex()));
}

static void handleAliasAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  StringRef Str;
  if (!S.checkStringLiteralArgumentAttr(AL, 0, Str))
    return;

  if (S.Context.getTargetInfo().getTriple().isOSDarwin()) {
    S.Diag(AL.getLoc(), diag::err_alias_not_supported_on_darwin);
    return;
  }
  if (S.Context.getTargetInfo().getTriple().isNVPTX()) {
    S.Diag(AL.getLoc(), diag::err_alias_not_supported_on_nvptx);
  }

  // Aliases should be on declarations, not definitions.
  if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
    if (FD->isThisDeclarationADefinition()) {
      S.Diag(AL.getLoc(), diag::err_alias_is_definition) << FD << 0;
      return;
    }
  } else {
    const auto *VD = cast<VarDecl>(D);
    if (VD->isThisDeclarationADefinition() && VD->isExternallyVisible()) {
      S.Diag(AL.getLoc(), diag::err_alias_is_definition) << VD << 0;
      return;
    }
  }

  // Mark target used to prevent unneeded-internal-declaration warnings.
  if (!S.LangOpts.CPlusPlus) {
    // FIXME: demangle Str for C++, as the attribute refers to the mangled
    // linkage name, not the pre-mangled identifier.
    const DeclarationNameInfo target(&S.Context.Idents.get(Str), AL.getLoc());
    LookupResult LR(S, target, Sema::LookupOrdinaryName);
    if (S.LookupQualifiedName(LR, S.getCurLexicalContext()))
      for (NamedDecl *ND : LR)
        ND->markUsed(S.Context);
  }

  D->addAttr(::new (S.Context) AliasAttr(AL.getRange(), S.Context, Str,
                                         AL.getAttributeSpellingListIndex()));
}

static void handleTLSModelAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  StringRef Model;
  SourceLocation LiteralLoc;
  // Check that it is a string.
  if (!S.checkStringLiteralArgumentAttr(AL, 0, Model, &LiteralLoc))
    return;

  // Check that the value.
  if (Model != "global-dynamic" && Model != "local-dynamic"
      && Model != "initial-exec" && Model != "local-exec") {
    S.Diag(LiteralLoc, diag::err_attr_tlsmodel_arg);
    return;
  }

  D->addAttr(::new (S.Context)
             TLSModelAttr(AL.getRange(), S.Context, Model,
                          AL.getAttributeSpellingListIndex()));
}

static void handleRestrictAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  QualType ResultType = getFunctionOrMethodResultType(D);
  if (ResultType->isAnyPointerType() || ResultType->isBlockPointerType()) {
    D->addAttr(::new (S.Context) RestrictAttr(
        AL.getRange(), S.Context, AL.getAttributeSpellingListIndex()));
    return;
  }

  S.Diag(AL.getLoc(), diag::warn_attribute_return_pointers_only)
      << AL << getFunctionOrMethodResultSourceRange(D);
}

static void handleCPUSpecificAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  FunctionDecl *FD = cast<FunctionDecl>(D);

  if (const auto *MD = dyn_cast<CXXMethodDecl>(D)) {
    if (MD->getParent()->isLambda()) {
      S.Diag(AL.getLoc(), diag::err_attribute_dll_lambda) << AL;
      return;
    }
  }

  if (!checkAttributeAtLeastNumArgs(S, AL, 1))
    return;

  SmallVector<IdentifierInfo *, 8> CPUs;
  for (unsigned ArgNo = 0; ArgNo < getNumAttributeArgs(AL); ++ArgNo) {
    if (!AL.isArgIdent(ArgNo)) {
      S.Diag(AL.getLoc(), diag::err_attribute_argument_type)
          << AL << AANT_ArgumentIdentifier;
      return;
    }

    IdentifierLoc *CPUArg = AL.getArgAsIdent(ArgNo);
    StringRef CPUName = CPUArg->Ident->getName().trim();

    if (!S.Context.getTargetInfo().validateCPUSpecificCPUDispatch(CPUName)) {
      S.Diag(CPUArg->Loc, diag::err_invalid_cpu_specific_dispatch_value)
          << CPUName << (AL.getKind() == ParsedAttr::AT_CPUDispatch);
      return;
    }

    const TargetInfo &Target = S.Context.getTargetInfo();
    if (llvm::any_of(CPUs, [CPUName, &Target](const IdentifierInfo *Cur) {
          return Target.CPUSpecificManglingCharacter(CPUName) ==
                 Target.CPUSpecificManglingCharacter(Cur->getName());
        })) {
      S.Diag(AL.getLoc(), diag::warn_multiversion_duplicate_entries);
      return;
    }
    CPUs.push_back(CPUArg->Ident);
  }

  FD->setIsMultiVersion(true);
  if (AL.getKind() == ParsedAttr::AT_CPUSpecific)
    D->addAttr(::new (S.Context) CPUSpecificAttr(
        AL.getRange(), S.Context, CPUs.data(), CPUs.size(),
        AL.getAttributeSpellingListIndex()));
  else
    D->addAttr(::new (S.Context) CPUDispatchAttr(
        AL.getRange(), S.Context, CPUs.data(), CPUs.size(),
        AL.getAttributeSpellingListIndex()));
}

static void handleCommonAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (S.LangOpts.CPlusPlus) {
    S.Diag(AL.getLoc(), diag::err_attribute_not_supported_in_lang)
        << AL << AttributeLangSupport::Cpp;
    return;
  }

  if (CommonAttr *CA = S.mergeCommonAttr(D, AL))
    D->addAttr(CA);
}

static void handleNakedAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (checkAttrMutualExclusion<DisableTailCallsAttr>(S, D, AL))
    return;

  if (AL.isDeclspecAttribute()) {
    const auto &Triple = S.getASTContext().getTargetInfo().getTriple();
    const auto &Arch = Triple.getArch();
    if (Arch != llvm::Triple::x86 &&
        (Arch != llvm::Triple::arm && Arch != llvm::Triple::thumb)) {
      S.Diag(AL.getLoc(), diag::err_attribute_not_supported_on_arch)
          << AL << Triple.getArchName();
      return;
    }
  }

  D->addAttr(::new (S.Context) NakedAttr(AL.getRange(), S.Context,
                                         AL.getAttributeSpellingListIndex()));
}

static void handleNoReturnAttr(Sema &S, Decl *D, const ParsedAttr &Attrs) {
  if (hasDeclarator(D)) return;

  if (!isa<ObjCMethodDecl>(D)) {
    S.Diag(Attrs.getLoc(), diag::warn_attribute_wrong_decl_type)
        << Attrs << ExpectedFunctionOrMethod;
    return;
  }

  D->addAttr(::new (S.Context) NoReturnAttr(
      Attrs.getRange(), S.Context, Attrs.getAttributeSpellingListIndex()));
}

static void handleNoCfCheckAttr(Sema &S, Decl *D, const ParsedAttr &Attrs) {
  if (!S.getLangOpts().CFProtectionBranch)
    S.Diag(Attrs.getLoc(), diag::warn_nocf_check_attribute_ignored);
  else
    handleSimpleAttribute<AnyX86NoCfCheckAttr>(S, D, Attrs);
}

bool Sema::CheckAttrNoArgs(const ParsedAttr &Attrs) {
  if (!checkAttributeNumArgs(*this, Attrs, 0)) {
    Attrs.setInvalid();
    return true;
  }

  return false;
}

bool Sema::CheckAttrTarget(const ParsedAttr &AL) {
  // Check whether the attribute is valid on the current target.
  if (!AL.existsInTarget(Context.getTargetInfo())) {
    Diag(AL.getLoc(), diag::warn_unknown_attribute_ignored) << AL;
    AL.setInvalid();
    return true;
  }

  return false;
}

static void handleAnalyzerNoReturnAttr(Sema &S, Decl *D, const ParsedAttr &AL) {

  // The checking path for 'noreturn' and 'analyzer_noreturn' are different
  // because 'analyzer_noreturn' does not impact the type.
  if (!isFunctionOrMethodOrBlock(D)) {
    ValueDecl *VD = dyn_cast<ValueDecl>(D);
    if (!VD || (!VD->getType()->isBlockPointerType() &&
                !VD->getType()->isFunctionPointerType())) {
      S.Diag(AL.getLoc(), AL.isCXX11Attribute()
                              ? diag::err_attribute_wrong_decl_type
                              : diag::warn_attribute_wrong_decl_type)
          << AL << ExpectedFunctionMethodOrBlock;
      return;
    }
  }

  D->addAttr(::new (S.Context)
             AnalyzerNoReturnAttr(AL.getRange(), S.Context,
                                  AL.getAttributeSpellingListIndex()));
}

// PS3 PPU-specific.
static void handleVecReturnAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  /*
    Returning a Vector Class in Registers

    According to the PPU ABI specifications, a class with a single member of
    vector type is returned in memory when used as the return value of a
    function.
    This results in inefficient code when implementing vector classes. To return
    the value in a single vector register, add the vecreturn attribute to the
    class definition. This attribute is also applicable to struct types.

    Example:

    struct Vector
    {
      __vector float xyzw;
    } __attribute__((vecreturn));

    Vector Add(Vector lhs, Vector rhs)
    {
      Vector result;
      result.xyzw = vec_add(lhs.xyzw, rhs.xyzw);
      return result; // This will be returned in a register
    }
  */
  if (VecReturnAttr *A = D->getAttr<VecReturnAttr>()) {
    S.Diag(AL.getLoc(), diag::err_repeat_attribute) << A;
    return;
  }

  const auto *R = cast<RecordDecl>(D);
  int count = 0;

  if (!isa<CXXRecordDecl>(R)) {
    S.Diag(AL.getLoc(), diag::err_attribute_vecreturn_only_vector_member);
    return;
  }

  if (!cast<CXXRecordDecl>(R)->isPOD()) {
    S.Diag(AL.getLoc(), diag::err_attribute_vecreturn_only_pod_record);
    return;
  }

  for (const auto *I : R->fields()) {
    if ((count == 1) || !I->getType()->isVectorType()) {
      S.Diag(AL.getLoc(), diag::err_attribute_vecreturn_only_vector_member);
      return;
    }
    count++;
  }

  D->addAttr(::new (S.Context) VecReturnAttr(
      AL.getRange(), S.Context, AL.getAttributeSpellingListIndex()));
}

static void handleDependencyAttr(Sema &S, Scope *Scope, Decl *D,
                                 const ParsedAttr &AL) {
  if (isa<ParmVarDecl>(D)) {
    // [[carries_dependency]] can only be applied to a parameter if it is a
    // parameter of a function declaration or lambda.
    if (!(Scope->getFlags() & clang::Scope::FunctionDeclarationScope)) {
      S.Diag(AL.getLoc(),
             diag::err_carries_dependency_param_not_function_decl);
      return;
    }
  }

  D->addAttr(::new (S.Context) CarriesDependencyAttr(
                                   AL.getRange(), S.Context,
                                   AL.getAttributeSpellingListIndex()));
}

static void handleUnusedAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  bool IsCXX17Attr = AL.isCXX11Attribute() && !AL.getScopeName();

  // If this is spelled as the standard C++17 attribute, but not in C++17, warn
  // about using it as an extension.
  if (!S.getLangOpts().CPlusPlus17 && IsCXX17Attr)
    S.Diag(AL.getLoc(), diag::ext_cxx17_attr) << AL;

  D->addAttr(::new (S.Context) UnusedAttr(
      AL.getRange(), S.Context, AL.getAttributeSpellingListIndex()));
}

static void handleConstructorAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  uint32_t priority = ConstructorAttr::DefaultPriority;
  if (AL.getNumArgs() &&
      !checkUInt32Argument(S, AL, AL.getArgAsExpr(0), priority))
    return;

  D->addAttr(::new (S.Context)
             ConstructorAttr(AL.getRange(), S.Context, priority,
                             AL.getAttributeSpellingListIndex()));
}

static void handleDestructorAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  uint32_t priority = DestructorAttr::DefaultPriority;
  if (AL.getNumArgs() &&
      !checkUInt32Argument(S, AL, AL.getArgAsExpr(0), priority))
    return;

  D->addAttr(::new (S.Context)
             DestructorAttr(AL.getRange(), S.Context, priority,
                            AL.getAttributeSpellingListIndex()));
}

template <typename AttrTy>
static void handleAttrWithMessage(Sema &S, Decl *D, const ParsedAttr &AL) {
  // Handle the case where the attribute has a text message.
  StringRef Str;
  if (AL.getNumArgs() == 1 && !S.checkStringLiteralArgumentAttr(AL, 0, Str))
    return;

  D->addAttr(::new (S.Context) AttrTy(AL.getRange(), S.Context, Str,
                                      AL.getAttributeSpellingListIndex()));
}

static void handleObjCSuppresProtocolAttr(Sema &S, Decl *D,
                                          const ParsedAttr &AL) {
  if (!cast<ObjCProtocolDecl>(D)->isThisDeclarationADefinition()) {
    S.Diag(AL.getLoc(), diag::err_objc_attr_protocol_requires_definition)
        << AL << AL.getRange();
    return;
  }

  D->addAttr(::new (S.Context)
          ObjCExplicitProtocolImplAttr(AL.getRange(), S.Context,
                                       AL.getAttributeSpellingListIndex()));
}

static bool checkAvailabilityAttr(Sema &S, SourceRange Range,
                                  IdentifierInfo *Platform,
                                  VersionTuple Introduced,
                                  VersionTuple Deprecated,
                                  VersionTuple Obsoleted) {
  StringRef PlatformName
    = AvailabilityAttr::getPrettyPlatformName(Platform->getName());
  if (PlatformName.empty())
    PlatformName = Platform->getName();

  // Ensure that Introduced <= Deprecated <= Obsoleted (although not all
  // of these steps are needed).
  if (!Introduced.empty() && !Deprecated.empty() &&
      !(Introduced <= Deprecated)) {
    S.Diag(Range.getBegin(), diag::warn_availability_version_ordering)
      << 1 << PlatformName << Deprecated.getAsString()
      << 0 << Introduced.getAsString();
    return true;
  }

  if (!Introduced.empty() && !Obsoleted.empty() &&
      !(Introduced <= Obsoleted)) {
    S.Diag(Range.getBegin(), diag::warn_availability_version_ordering)
      << 2 << PlatformName << Obsoleted.getAsString()
      << 0 << Introduced.getAsString();
    return true;
  }

  if (!Deprecated.empty() && !Obsoleted.empty() &&
      !(Deprecated <= Obsoleted)) {
    S.Diag(Range.getBegin(), diag::warn_availability_version_ordering)
      << 2 << PlatformName << Obsoleted.getAsString()
      << 1 << Deprecated.getAsString();
    return true;
  }

  return false;
}

/// Check whether the two versions match.
///
/// If either version tuple is empty, then they are assumed to match. If
/// \p BeforeIsOkay is true, then \p X can be less than or equal to \p Y.
static bool versionsMatch(const VersionTuple &X, const VersionTuple &Y,
                          bool BeforeIsOkay) {
  if (X.empty() || Y.empty())
    return true;

  if (X == Y)
    return true;

  if (BeforeIsOkay && X < Y)
    return true;

  return false;
}

AvailabilityAttr *Sema::mergeAvailabilityAttr(NamedDecl *D, SourceRange Range,
                                              IdentifierInfo *Platform,
                                              bool Implicit,
                                              VersionTuple Introduced,
                                              VersionTuple Deprecated,
                                              VersionTuple Obsoleted,
                                              bool IsUnavailable,
                                              StringRef Message,
                                              bool IsStrict,
                                              StringRef Replacement,
                                              AvailabilityMergeKind AMK,
                                              unsigned AttrSpellingListIndex) {
  VersionTuple MergedIntroduced = Introduced;
  VersionTuple MergedDeprecated = Deprecated;
  VersionTuple MergedObsoleted = Obsoleted;
  bool FoundAny = false;
  bool OverrideOrImpl = false;
  switch (AMK) {
  case AMK_None:
  case AMK_Redeclaration:
    OverrideOrImpl = false;
    break;

  case AMK_Override:
  case AMK_ProtocolImplementation:
    OverrideOrImpl = true;
    break;
  }

  if (D->hasAttrs()) {
    AttrVec &Attrs = D->getAttrs();
    for (unsigned i = 0, e = Attrs.size(); i != e;) {
      const auto *OldAA = dyn_cast<AvailabilityAttr>(Attrs[i]);
      if (!OldAA) {
        ++i;
        continue;
      }

      IdentifierInfo *OldPlatform = OldAA->getPlatform();
      if (OldPlatform != Platform) {
        ++i;
        continue;
      }

      // If there is an existing availability attribute for this platform that
      // is explicit and the new one is implicit use the explicit one and
      // discard the new implicit attribute.
      if (!OldAA->isImplicit() && Implicit) {
        return nullptr;
      }

      // If there is an existing attribute for this platform that is implicit
      // and the new attribute is explicit then erase the old one and
      // continue processing the attributes.
      if (!Implicit && OldAA->isImplicit()) {
        Attrs.erase(Attrs.begin() + i);
        --e;
        continue;
      }

      FoundAny = true;
      VersionTuple OldIntroduced = OldAA->getIntroduced();
      VersionTuple OldDeprecated = OldAA->getDeprecated();
      VersionTuple OldObsoleted = OldAA->getObsoleted();
      bool OldIsUnavailable = OldAA->getUnavailable();

      if (!versionsMatch(OldIntroduced, Introduced, OverrideOrImpl) ||
          !versionsMatch(Deprecated, OldDeprecated, OverrideOrImpl) ||
          !versionsMatch(Obsoleted, OldObsoleted, OverrideOrImpl) ||
          !(OldIsUnavailable == IsUnavailable ||
            (OverrideOrImpl && !OldIsUnavailable && IsUnavailable))) {
        if (OverrideOrImpl) {
          int Which = -1;
          VersionTuple FirstVersion;
          VersionTuple SecondVersion;
          if (!versionsMatch(OldIntroduced, Introduced, OverrideOrImpl)) {
            Which = 0;
            FirstVersion = OldIntroduced;
            SecondVersion = Introduced;
          } else if (!versionsMatch(Deprecated, OldDeprecated, OverrideOrImpl)) {
            Which = 1;
            FirstVersion = Deprecated;
            SecondVersion = OldDeprecated;
          } else if (!versionsMatch(Obsoleted, OldObsoleted, OverrideOrImpl)) {
            Which = 2;
            FirstVersion = Obsoleted;
            SecondVersion = OldObsoleted;
          }

          if (Which == -1) {
            Diag(OldAA->getLocation(),
                 diag::warn_mismatched_availability_override_unavail)
              << AvailabilityAttr::getPrettyPlatformName(Platform->getName())
              << (AMK == AMK_Override);
          } else {
            Diag(OldAA->getLocation(),
                 diag::warn_mismatched_availability_override)
              << Which
              << AvailabilityAttr::getPrettyPlatformName(Platform->getName())
              << FirstVersion.getAsString() << SecondVersion.getAsString()
              << (AMK == AMK_Override);
          }
          if (AMK == AMK_Override)
            Diag(Range.getBegin(), diag::note_overridden_method);
          else
            Diag(Range.getBegin(), diag::note_protocol_method);
        } else {
          Diag(OldAA->getLocation(), diag::warn_mismatched_availability);
          Diag(Range.getBegin(), diag::note_previous_attribute);
        }

        Attrs.erase(Attrs.begin() + i);
        --e;
        continue;
      }

      VersionTuple MergedIntroduced2 = MergedIntroduced;
      VersionTuple MergedDeprecated2 = MergedDeprecated;
      VersionTuple MergedObsoleted2 = MergedObsoleted;

      if (MergedIntroduced2.empty())
        MergedIntroduced2 = OldIntroduced;
      if (MergedDeprecated2.empty())
        MergedDeprecated2 = OldDeprecated;
      if (MergedObsoleted2.empty())
        MergedObsoleted2 = OldObsoleted;

      if (checkAvailabilityAttr(*this, OldAA->getRange(), Platform,
                                MergedIntroduced2, MergedDeprecated2,
                                MergedObsoleted2)) {
        Attrs.erase(Attrs.begin() + i);
        --e;
        continue;
      }

      MergedIntroduced = MergedIntroduced2;
      MergedDeprecated = MergedDeprecated2;
      MergedObsoleted = MergedObsoleted2;
      ++i;
    }
  }

  if (FoundAny &&
      MergedIntroduced == Introduced &&
      MergedDeprecated == Deprecated &&
      MergedObsoleted == Obsoleted)
    return nullptr;

  // Only create a new attribute if !OverrideOrImpl, but we want to do
  // the checking.
  if (!checkAvailabilityAttr(*this, Range, Platform, MergedIntroduced,
                             MergedDeprecated, MergedObsoleted) &&
      !OverrideOrImpl) {
    auto *Avail =  ::new (Context) AvailabilityAttr(Range, Context, Platform,
                                            Introduced, Deprecated,
                                            Obsoleted, IsUnavailable, Message,
                                            IsStrict, Replacement,
                                            AttrSpellingListIndex);
    Avail->setImplicit(Implicit);
    return Avail;
  }
  return nullptr;
}

static void handleAvailabilityAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (!checkAttributeNumArgs(S, AL, 1))
    return;
  IdentifierLoc *Platform = AL.getArgAsIdent(0);
  unsigned Index = AL.getAttributeSpellingListIndex();

  IdentifierInfo *II = Platform->Ident;
  if (AvailabilityAttr::getPrettyPlatformName(II->getName()).empty())
    S.Diag(Platform->Loc, diag::warn_availability_unknown_platform)
      << Platform->Ident;

  auto *ND = dyn_cast<NamedDecl>(D);
  if (!ND) // We warned about this already, so just return.
    return;

  AvailabilityChange Introduced = AL.getAvailabilityIntroduced();
  AvailabilityChange Deprecated = AL.getAvailabilityDeprecated();
  AvailabilityChange Obsoleted = AL.getAvailabilityObsoleted();
  bool IsUnavailable = AL.getUnavailableLoc().isValid();
  bool IsStrict = AL.getStrictLoc().isValid();
  StringRef Str;
  if (const auto *SE = dyn_cast_or_null<StringLiteral>(AL.getMessageExpr()))
    Str = SE->getString();
  StringRef Replacement;
  if (const auto *SE = dyn_cast_or_null<StringLiteral>(AL.getReplacementExpr()))
    Replacement = SE->getString();

  if (II->isStr("swift")) {
    if (Introduced.isValid() || Obsoleted.isValid() ||
        (!IsUnavailable && !Deprecated.isValid())) {
      S.Diag(AL.getLoc(),
             diag::warn_availability_swift_unavailable_deprecated_only);
      return;
    }
  }

  AvailabilityAttr *NewAttr = S.mergeAvailabilityAttr(ND, AL.getRange(), II,
                                                      false/*Implicit*/,
                                                      Introduced.Version,
                                                      Deprecated.Version,
                                                      Obsoleted.Version,
                                                      IsUnavailable, Str,
                                                      IsStrict, Replacement,
                                                      Sema::AMK_None,
                                                      Index);
  if (NewAttr)
    D->addAttr(NewAttr);

  // Transcribe "ios" to "watchos" (and add a new attribute) if the versioning
  // matches before the start of the watchOS platform.
  if (S.Context.getTargetInfo().getTriple().isWatchOS()) {
    IdentifierInfo *NewII = nullptr;
    if (II->getName() == "ios")
      NewII = &S.Context.Idents.get("watchos");
    else if (II->getName() == "ios_app_extension")
      NewII = &S.Context.Idents.get("watchos_app_extension");

    if (NewII) {
        auto adjustWatchOSVersion = [](VersionTuple Version) -> VersionTuple {
          if (Version.empty())
            return Version;
          auto Major = Version.getMajor();
          auto NewMajor = Major >= 9 ? Major - 7 : 0;
          if (NewMajor >= 2) {
            if (Version.getMinor().hasValue()) {
              if (Version.getSubminor().hasValue())
                return VersionTuple(NewMajor, Version.getMinor().getValue(),
                                    Version.getSubminor().getValue());
              else
                return VersionTuple(NewMajor, Version.getMinor().getValue());
            }
          }

          return VersionTuple(2, 0);
        };

        auto NewIntroduced = adjustWatchOSVersion(Introduced.Version);
        auto NewDeprecated = adjustWatchOSVersion(Deprecated.Version);
        auto NewObsoleted = adjustWatchOSVersion(Obsoleted.Version);

        AvailabilityAttr *NewAttr = S.mergeAvailabilityAttr(ND,
                                                            AL.getRange(),
                                                            NewII,
                                                            true/*Implicit*/,
                                                            NewIntroduced,
                                                            NewDeprecated,
                                                            NewObsoleted,
                                                            IsUnavailable, Str,
                                                            IsStrict,
                                                            Replacement,
                                                            Sema::AMK_None,
                                                            Index);
        if (NewAttr)
          D->addAttr(NewAttr);
      }
  } else if (S.Context.getTargetInfo().getTriple().isTvOS()) {
    // Transcribe "ios" to "tvos" (and add a new attribute) if the versioning
    // matches before the start of the tvOS platform.
    IdentifierInfo *NewII = nullptr;
    if (II->getName() == "ios")
      NewII = &S.Context.Idents.get("tvos");
    else if (II->getName() == "ios_app_extension")
      NewII = &S.Context.Idents.get("tvos_app_extension");

    if (NewII) {
        AvailabilityAttr *NewAttr = S.mergeAvailabilityAttr(ND,
                                                            AL.getRange(),
                                                            NewII,
                                                            true/*Implicit*/,
                                                            Introduced.Version,
                                                            Deprecated.Version,
                                                            Obsoleted.Version,
                                                            IsUnavailable, Str,
                                                            IsStrict,
                                                            Replacement,
                                                            Sema::AMK_None,
                                                            Index);
        if (NewAttr)
          D->addAttr(NewAttr);
      }
  }
}

static void handleExternalSourceSymbolAttr(Sema &S, Decl *D,
                                           const ParsedAttr &AL) {
  if (!checkAttributeAtLeastNumArgs(S, AL, 1))
    return;
  assert(checkAttributeAtMostNumArgs(S, AL, 3) &&
         "Invalid number of arguments in an external_source_symbol attribute");

  StringRef Language;
  if (const auto *SE = dyn_cast_or_null<StringLiteral>(AL.getArgAsExpr(0)))
    Language = SE->getString();
  StringRef DefinedIn;
  if (const auto *SE = dyn_cast_or_null<StringLiteral>(AL.getArgAsExpr(1)))
    DefinedIn = SE->getString();
  bool IsGeneratedDeclaration = AL.getArgAsIdent(2) != nullptr;

  D->addAttr(::new (S.Context) ExternalSourceSymbolAttr(
      AL.getRange(), S.Context, Language, DefinedIn, IsGeneratedDeclaration,
      AL.getAttributeSpellingListIndex()));
}

template <class T>
static T *mergeVisibilityAttr(Sema &S, Decl *D, SourceRange range,
                              typename T::VisibilityType value,
                              unsigned attrSpellingListIndex) {
  T *existingAttr = D->getAttr<T>();
  if (existingAttr) {
    typename T::VisibilityType existingValue = existingAttr->getVisibility();
    if (existingValue == value)
      return nullptr;
    S.Diag(existingAttr->getLocation(), diag::err_mismatched_visibility);
    S.Diag(range.getBegin(), diag::note_previous_attribute);
    D->dropAttr<T>();
  }
  return ::new (S.Context) T(range, S.Context, value, attrSpellingListIndex);
}

VisibilityAttr *Sema::mergeVisibilityAttr(Decl *D, SourceRange Range,
                                          VisibilityAttr::VisibilityType Vis,
                                          unsigned AttrSpellingListIndex) {
  return ::mergeVisibilityAttr<VisibilityAttr>(*this, D, Range, Vis,
                                               AttrSpellingListIndex);
}

TypeVisibilityAttr *Sema::mergeTypeVisibilityAttr(Decl *D, SourceRange Range,
                                      TypeVisibilityAttr::VisibilityType Vis,
                                      unsigned AttrSpellingListIndex) {
  return ::mergeVisibilityAttr<TypeVisibilityAttr>(*this, D, Range, Vis,
                                                   AttrSpellingListIndex);
}

static void handleVisibilityAttr(Sema &S, Decl *D, const ParsedAttr &AL,
                                 bool isTypeVisibility) {
  // Visibility attributes don't mean anything on a typedef.
  if (isa<TypedefNameDecl>(D)) {
    S.Diag(AL.getRange().getBegin(), diag::warn_attribute_ignored) << AL;
    return;
  }

  // 'type_visibility' can only go on a type or namespace.
  if (isTypeVisibility &&
      !(isa<TagDecl>(D) ||
        isa<ObjCInterfaceDecl>(D) ||
        isa<NamespaceDecl>(D))) {
    S.Diag(AL.getRange().getBegin(), diag::err_attribute_wrong_decl_type)
        << AL << ExpectedTypeOrNamespace;
    return;
  }

  // Check that the argument is a string literal.
  StringRef TypeStr;
  SourceLocation LiteralLoc;
  if (!S.checkStringLiteralArgumentAttr(AL, 0, TypeStr, &LiteralLoc))
    return;

  VisibilityAttr::VisibilityType type;
  if (!VisibilityAttr::ConvertStrToVisibilityType(TypeStr, type)) {
    S.Diag(LiteralLoc, diag::warn_attribute_type_not_supported) << AL
                                                                << TypeStr;
    return;
  }

  // Complain about attempts to use protected visibility on targets
  // (like Darwin) that don't support it.
  if (type == VisibilityAttr::Protected &&
      !S.Context.getTargetInfo().hasProtectedVisibility()) {
    S.Diag(AL.getLoc(), diag::warn_attribute_protected_visibility);
    type = VisibilityAttr::Default;
  }

  unsigned Index = AL.getAttributeSpellingListIndex();
  Attr *newAttr;
  if (isTypeVisibility) {
    newAttr = S.mergeTypeVisibilityAttr(D, AL.getRange(),
                                    (TypeVisibilityAttr::VisibilityType) type,
                                        Index);
  } else {
    newAttr = S.mergeVisibilityAttr(D, AL.getRange(), type, Index);
  }
  if (newAttr)
    D->addAttr(newAttr);
}

static void handleObjCMethodFamilyAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  const auto *M = cast<ObjCMethodDecl>(D);
  if (!AL.isArgIdent(0)) {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_n_type)
        << AL << 1 << AANT_ArgumentIdentifier;
    return;
  }

  IdentifierLoc *IL = AL.getArgAsIdent(0);
  ObjCMethodFamilyAttr::FamilyKind F;
  if (!ObjCMethodFamilyAttr::ConvertStrToFamilyKind(IL->Ident->getName(), F)) {
    S.Diag(IL->Loc, diag::warn_attribute_type_not_supported) << AL << IL->Ident;
    return;
  }

  if (F == ObjCMethodFamilyAttr::OMF_init &&
      !M->getReturnType()->isObjCObjectPointerType()) {
    S.Diag(M->getLocation(), diag::err_init_method_bad_return_type)
        << M->getReturnType();
    // Ignore the attribute.
    return;
  }

  D->addAttr(new (S.Context) ObjCMethodFamilyAttr(
      AL.getRange(), S.Context, F, AL.getAttributeSpellingListIndex()));
}

static void handleObjCNSObject(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (const auto *TD = dyn_cast<TypedefNameDecl>(D)) {
    QualType T = TD->getUnderlyingType();
    if (!T->isCARCBridgableType()) {
      S.Diag(TD->getLocation(), diag::err_nsobject_attribute);
      return;
    }
  }
  else if (const auto *PD = dyn_cast<ObjCPropertyDecl>(D)) {
    QualType T = PD->getType();
    if (!T->isCARCBridgableType()) {
      S.Diag(PD->getLocation(), diag::err_nsobject_attribute);
      return;
    }
  }
  else {
    // It is okay to include this attribute on properties, e.g.:
    //
    //  @property (retain, nonatomic) struct Bork *Q __attribute__((NSObject));
    //
    // In this case it follows tradition and suppresses an error in the above
    // case.
    S.Diag(D->getLocation(), diag::warn_nsobject_attribute);
  }
  D->addAttr(::new (S.Context)
             ObjCNSObjectAttr(AL.getRange(), S.Context,
                              AL.getAttributeSpellingListIndex()));
}

static void handleObjCIndependentClass(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (const auto *TD = dyn_cast<TypedefNameDecl>(D)) {
    QualType T = TD->getUnderlyingType();
    if (!T->isObjCObjectPointerType()) {
      S.Diag(TD->getLocation(), diag::warn_ptr_independentclass_attribute);
      return;
    }
  } else {
    S.Diag(D->getLocation(), diag::warn_independentclass_attribute);
    return;
  }
  D->addAttr(::new (S.Context)
             ObjCIndependentClassAttr(AL.getRange(), S.Context,
                              AL.getAttributeSpellingListIndex()));
}

static void handleBlocksAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (!AL.isArgIdent(0)) {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_n_type)
        << AL << 1 << AANT_ArgumentIdentifier;
    return;
  }

  IdentifierInfo *II = AL.getArgAsIdent(0)->Ident;
  BlocksAttr::BlockType type;
  if (!BlocksAttr::ConvertStrToBlockType(II->getName(), type)) {
    S.Diag(AL.getLoc(), diag::warn_attribute_type_not_supported) << AL << II;
    return;
  }

  D->addAttr(::new (S.Context)
             BlocksAttr(AL.getRange(), S.Context, type,
                        AL.getAttributeSpellingListIndex()));
}

static void handleSentinelAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  unsigned sentinel = (unsigned)SentinelAttr::DefaultSentinel;
  if (AL.getNumArgs() > 0) {
    Expr *E = AL.getArgAsExpr(0);
    llvm::APSInt Idx(32);
    if (E->isTypeDependent() || E->isValueDependent() ||
        !E->isIntegerConstantExpr(Idx, S.Context)) {
      S.Diag(AL.getLoc(), diag::err_attribute_argument_n_type)
          << AL << 1 << AANT_ArgumentIntegerConstant << E->getSourceRange();
      return;
    }

    if (Idx.isSigned() && Idx.isNegative()) {
      S.Diag(AL.getLoc(), diag::err_attribute_sentinel_less_than_zero)
        << E->getSourceRange();
      return;
    }

    sentinel = Idx.getZExtValue();
  }

  unsigned nullPos = (unsigned)SentinelAttr::DefaultNullPos;
  if (AL.getNumArgs() > 1) {
    Expr *E = AL.getArgAsExpr(1);
    llvm::APSInt Idx(32);
    if (E->isTypeDependent() || E->isValueDependent() ||
        !E->isIntegerConstantExpr(Idx, S.Context)) {
      S.Diag(AL.getLoc(), diag::err_attribute_argument_n_type)
          << AL << 2 << AANT_ArgumentIntegerConstant << E->getSourceRange();
      return;
    }
    nullPos = Idx.getZExtValue();

    if ((Idx.isSigned() && Idx.isNegative()) || nullPos > 1) {
      // FIXME: This error message could be improved, it would be nice
      // to say what the bounds actually are.
      S.Diag(AL.getLoc(), diag::err_attribute_sentinel_not_zero_or_one)
        << E->getSourceRange();
      return;
    }
  }

  if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
    const FunctionType *FT = FD->getType()->castAs<FunctionType>();
    if (isa<FunctionNoProtoType>(FT)) {
      S.Diag(AL.getLoc(), diag::warn_attribute_sentinel_named_arguments);
      return;
    }

    if (!cast<FunctionProtoType>(FT)->isVariadic()) {
      S.Diag(AL.getLoc(), diag::warn_attribute_sentinel_not_variadic) << 0;
      return;
    }
  } else if (const auto *MD = dyn_cast<ObjCMethodDecl>(D)) {
    if (!MD->isVariadic()) {
      S.Diag(AL.getLoc(), diag::warn_attribute_sentinel_not_variadic) << 0;
      return;
    }
  } else if (const auto *BD = dyn_cast<BlockDecl>(D)) {
    if (!BD->isVariadic()) {
      S.Diag(AL.getLoc(), diag::warn_attribute_sentinel_not_variadic) << 1;
      return;
    }
  } else if (const auto *V = dyn_cast<VarDecl>(D)) {
    QualType Ty = V->getType();
    if (Ty->isBlockPointerType() || Ty->isFunctionPointerType()) {
      const FunctionType *FT = Ty->isFunctionPointerType()
       ? D->getFunctionType()
       : Ty->getAs<BlockPointerType>()->getPointeeType()->getAs<FunctionType>();
      if (!cast<FunctionProtoType>(FT)->isVariadic()) {
        int m = Ty->isFunctionPointerType() ? 0 : 1;
        S.Diag(AL.getLoc(), diag::warn_attribute_sentinel_not_variadic) << m;
        return;
      }
    } else {
      S.Diag(AL.getLoc(), diag::warn_attribute_wrong_decl_type)
          << AL << ExpectedFunctionMethodOrBlock;
      return;
    }
  } else {
    S.Diag(AL.getLoc(), diag::warn_attribute_wrong_decl_type)
        << AL << ExpectedFunctionMethodOrBlock;
    return;
  }
  D->addAttr(::new (S.Context)
             SentinelAttr(AL.getRange(), S.Context, sentinel, nullPos,
                          AL.getAttributeSpellingListIndex()));
}

static void handleWarnUnusedResult(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (D->getFunctionType() &&
      D->getFunctionType()->getReturnType()->isVoidType()) {
    S.Diag(AL.getLoc(), diag::warn_attribute_void_function_method) << AL << 0;
    return;
  }
  if (const auto *MD = dyn_cast<ObjCMethodDecl>(D))
    if (MD->getReturnType()->isVoidType()) {
      S.Diag(AL.getLoc(), diag::warn_attribute_void_function_method) << AL << 1;
      return;
    }

  // If this is spelled as the standard C++17 attribute, but not in C++17, warn
  // about using it as an extension.
  if (!S.getLangOpts().CPlusPlus17 && AL.isCXX11Attribute() &&
      !AL.getScopeName())
    S.Diag(AL.getLoc(), diag::ext_cxx17_attr) << AL;

  D->addAttr(::new (S.Context)
             WarnUnusedResultAttr(AL.getRange(), S.Context,
                                  AL.getAttributeSpellingListIndex()));
}

static void handleWeakImportAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  // weak_import only applies to variable & function declarations.
  bool isDef = false;
  if (!D->canBeWeakImported(isDef)) {
    if (isDef)
      S.Diag(AL.getLoc(), diag::warn_attribute_invalid_on_definition)
        << "weak_import";
    else if (isa<ObjCPropertyDecl>(D) || isa<ObjCMethodDecl>(D) ||
             (S.Context.getTargetInfo().getTriple().isOSDarwin() &&
              (isa<ObjCInterfaceDecl>(D) || isa<EnumDecl>(D)))) {
      // Nothing to warn about here.
    } else
      S.Diag(AL.getLoc(), diag::warn_attribute_wrong_decl_type)
          << AL << ExpectedVariableOrFunction;

    return;
  }

  D->addAttr(::new (S.Context)
             WeakImportAttr(AL.getRange(), S.Context,
                            AL.getAttributeSpellingListIndex()));
}

// Handles reqd_work_group_size and work_group_size_hint.
template <typename WorkGroupAttr>
static void handleWorkGroupSize(Sema &S, Decl *D, const ParsedAttr &AL) {
  uint32_t WGSize[3];
  for (unsigned i = 0; i < 3; ++i) {
    const Expr *E = AL.getArgAsExpr(i);
    if (!checkUInt32Argument(S, AL, E, WGSize[i], i,
                             /*StrictlyUnsigned=*/true))
      return;
    if (WGSize[i] == 0) {
      S.Diag(AL.getLoc(), diag::err_attribute_argument_is_zero)
          << AL << E->getSourceRange();
      return;
    }
  }

  WorkGroupAttr *Existing = D->getAttr<WorkGroupAttr>();
  if (Existing && !(Existing->getXDim() == WGSize[0] &&
                    Existing->getYDim() == WGSize[1] &&
                    Existing->getZDim() == WGSize[2]))
    S.Diag(AL.getLoc(), diag::warn_duplicate_attribute) << AL;

  D->addAttr(::new (S.Context) WorkGroupAttr(AL.getRange(), S.Context,
                                             WGSize[0], WGSize[1], WGSize[2],
                                       AL.getAttributeSpellingListIndex()));
}

// Handles intel_reqd_sub_group_size.
static void handleSubGroupSize(Sema &S, Decl *D, const ParsedAttr &AL) {
  uint32_t SGSize;
  const Expr *E = AL.getArgAsExpr(0);
  if (!checkUInt32Argument(S, AL, E, SGSize))
    return;
  if (SGSize == 0) {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_is_zero)
        << AL << E->getSourceRange();
    return;
  }

  OpenCLIntelReqdSubGroupSizeAttr *Existing =
      D->getAttr<OpenCLIntelReqdSubGroupSizeAttr>();
  if (Existing && Existing->getSubGroupSize() != SGSize)
    S.Diag(AL.getLoc(), diag::warn_duplicate_attribute) << AL;

  D->addAttr(::new (S.Context) OpenCLIntelReqdSubGroupSizeAttr(
      AL.getRange(), S.Context, SGSize,
      AL.getAttributeSpellingListIndex()));
}

static void handleVecTypeHint(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (!AL.hasParsedType()) {
    S.Diag(AL.getLoc(), diag::err_attribute_wrong_number_arguments) << AL << 1;
    return;
  }

  TypeSourceInfo *ParmTSI = nullptr;
  QualType ParmType = S.GetTypeFromParser(AL.getTypeArg(), &ParmTSI);
  assert(ParmTSI && "no type source info for attribute argument");

  if (!ParmType->isExtVectorType() && !ParmType->isFloatingType() &&
      (ParmType->isBooleanType() ||
       !ParmType->isIntegralType(S.getASTContext()))) {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_vec_type_hint)
        << ParmType;
    return;
  }

  if (VecTypeHintAttr *A = D->getAttr<VecTypeHintAttr>()) {
    if (!S.Context.hasSameType(A->getTypeHint(), ParmType)) {
      S.Diag(AL.getLoc(), diag::warn_duplicate_attribute) << AL;
      return;
    }
  }

  D->addAttr(::new (S.Context) VecTypeHintAttr(AL.getLoc(), S.Context,
                                               ParmTSI,
                                        AL.getAttributeSpellingListIndex()));
}

SectionAttr *Sema::mergeSectionAttr(Decl *D, SourceRange Range,
                                    StringRef Name,
                                    unsigned AttrSpellingListIndex) {
  // Explicit or partial specializations do not inherit
  // the section attribute from the primary template.
  if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
    if (AttrSpellingListIndex == SectionAttr::Declspec_allocate &&
        FD->isFunctionTemplateSpecialization())
      return nullptr;
  }
  if (SectionAttr *ExistingAttr = D->getAttr<SectionAttr>()) {
    if (ExistingAttr->getName() == Name)
      return nullptr;
    Diag(ExistingAttr->getLocation(), diag::warn_mismatched_section)
         << 1 /*section*/;
    Diag(Range.getBegin(), diag::note_previous_attribute);
    return nullptr;
  }
  return ::new (Context) SectionAttr(Range, Context, Name,
                                     AttrSpellingListIndex);
}

bool Sema::checkSectionName(SourceLocation LiteralLoc, StringRef SecName) {
  std::string Error = Context.getTargetInfo().isValidSectionSpecifier(SecName);
  if (!Error.empty()) {
    Diag(LiteralLoc, diag::err_attribute_section_invalid_for_target) << Error
         << 1 /*'section'*/;
    return false;
  }
  return true;
}

static void handleSectionAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  // Make sure that there is a string literal as the sections's single
  // argument.
  StringRef Str;
  SourceLocation LiteralLoc;
  if (!S.checkStringLiteralArgumentAttr(AL, 0, Str, &LiteralLoc))
    return;

  if (!S.checkSectionName(LiteralLoc, Str))
    return;

  // If the target wants to validate the section specifier, make it happen.
  std::string Error = S.Context.getTargetInfo().isValidSectionSpecifier(Str);
  if (!Error.empty()) {
    S.Diag(LiteralLoc, diag::err_attribute_section_invalid_for_target)
    << Error;
    return;
  }

  unsigned Index = AL.getAttributeSpellingListIndex();
  SectionAttr *NewAttr = S.mergeSectionAttr(D, AL.getRange(), Str, Index);
  if (NewAttr)
    D->addAttr(NewAttr);
}

static bool checkCodeSegName(Sema&S, SourceLocation LiteralLoc, StringRef CodeSegName) {
  std::string Error = S.Context.getTargetInfo().isValidSectionSpecifier(CodeSegName);
  if (!Error.empty()) {
    S.Diag(LiteralLoc, diag::err_attribute_section_invalid_for_target) << Error
           << 0 /*'code-seg'*/;
    return false;
  }
  return true;
}

CodeSegAttr *Sema::mergeCodeSegAttr(Decl *D, SourceRange Range,
                                    StringRef Name,
                                    unsigned AttrSpellingListIndex) {
  // Explicit or partial specializations do not inherit
  // the code_seg attribute from the primary template.
  if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
    if (FD->isFunctionTemplateSpecialization())
      return nullptr;
  }
  if (const auto *ExistingAttr = D->getAttr<CodeSegAttr>()) {
    if (ExistingAttr->getName() == Name)
      return nullptr;
    Diag(ExistingAttr->getLocation(), diag::warn_mismatched_section)
         << 0 /*codeseg*/;
    Diag(Range.getBegin(), diag::note_previous_attribute);
    return nullptr;
  }
  return ::new (Context) CodeSegAttr(Range, Context, Name,
                                     AttrSpellingListIndex);
}

static void handleCodeSegAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  StringRef Str;
  SourceLocation LiteralLoc;
  if (!S.checkStringLiteralArgumentAttr(AL, 0, Str, &LiteralLoc))
    return;
  if (!checkCodeSegName(S, LiteralLoc, Str))
    return;
  if (const auto *ExistingAttr = D->getAttr<CodeSegAttr>()) {
    if (!ExistingAttr->isImplicit()) {
      S.Diag(AL.getLoc(),
             ExistingAttr->getName() == Str
             ? diag::warn_duplicate_codeseg_attribute
             : diag::err_conflicting_codeseg_attribute);
      return;
    }
    D->dropAttr<CodeSegAttr>();
  }
  if (CodeSegAttr *CSA = S.mergeCodeSegAttr(D, AL.getRange(), Str,
                                            AL.getAttributeSpellingListIndex()))
    D->addAttr(CSA);
}

// Check for things we'd like to warn about. Multiversioning issues are
// handled later in the process, once we know how many exist.
bool Sema::checkTargetAttr(SourceLocation LiteralLoc, StringRef AttrStr) {
  enum FirstParam { Unsupported, Duplicate };
  enum SecondParam { None, Architecture };
  for (auto Str : {"tune=", "fpmath="})
    if (AttrStr.find(Str) != StringRef::npos)
      return Diag(LiteralLoc, diag::warn_unsupported_target_attribute)
             << Unsupported << None << Str;

  TargetAttr::ParsedTargetAttr ParsedAttrs = TargetAttr::parse(AttrStr);

  if (!ParsedAttrs.Architecture.empty() &&
      !Context.getTargetInfo().isValidCPUName(ParsedAttrs.Architecture))
    return Diag(LiteralLoc, diag::warn_unsupported_target_attribute)
           << Unsupported << Architecture << ParsedAttrs.Architecture;

  if (ParsedAttrs.DuplicateArchitecture)
    return Diag(LiteralLoc, diag::warn_unsupported_target_attribute)
           << Duplicate << None << "arch=";

  for (const auto &Feature : ParsedAttrs.Features) {
    auto CurFeature = StringRef(Feature).drop_front(); // remove + or -.
    if (!Context.getTargetInfo().isValidFeatureName(CurFeature))
      return Diag(LiteralLoc, diag::warn_unsupported_target_attribute)
             << Unsupported << None << CurFeature;
  }

  return false;
}

static void handleTargetAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  StringRef Str;
  SourceLocation LiteralLoc;
  if (!S.checkStringLiteralArgumentAttr(AL, 0, Str, &LiteralLoc) ||
      S.checkTargetAttr(LiteralLoc, Str))
    return;

  unsigned Index = AL.getAttributeSpellingListIndex();
  TargetAttr *NewAttr =
      ::new (S.Context) TargetAttr(AL.getRange(), S.Context, Str, Index);
  D->addAttr(NewAttr);
}

static void handleMinVectorWidthAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  Expr *E = AL.getArgAsExpr(0);
  uint32_t VecWidth;
  if (!checkUInt32Argument(S, AL, E, VecWidth)) {
    AL.setInvalid();
    return;
  }

  MinVectorWidthAttr *Existing = D->getAttr<MinVectorWidthAttr>();
  if (Existing && Existing->getVectorWidth() != VecWidth) {
    S.Diag(AL.getLoc(), diag::warn_duplicate_attribute) << AL;
    return;
  }

  D->addAttr(::new (S.Context)
             MinVectorWidthAttr(AL.getRange(), S.Context, VecWidth,
                                AL.getAttributeSpellingListIndex()));
}

static void handleCleanupAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  Expr *E = AL.getArgAsExpr(0);
  SourceLocation Loc = E->getExprLoc();
  FunctionDecl *FD = nullptr;
  DeclarationNameInfo NI;

  // gcc only allows for simple identifiers. Since we support more than gcc, we
  // will warn the user.
  if (auto *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (DRE->hasQualifier())
      S.Diag(Loc, diag::warn_cleanup_ext);
    FD = dyn_cast<FunctionDecl>(DRE->getDecl());
    NI = DRE->getNameInfo();
    if (!FD) {
      S.Diag(Loc, diag::err_attribute_cleanup_arg_not_function) << 1
        << NI.getName();
      return;
    }
  } else if (auto *ULE = dyn_cast<UnresolvedLookupExpr>(E)) {
    if (ULE->hasExplicitTemplateArgs())
      S.Diag(Loc, diag::warn_cleanup_ext);
    FD = S.ResolveSingleFunctionTemplateSpecialization(ULE, true);
    NI = ULE->getNameInfo();
    if (!FD) {
      S.Diag(Loc, diag::err_attribute_cleanup_arg_not_function) << 2
        << NI.getName();
      if (ULE->getType() == S.Context.OverloadTy)
        S.NoteAllOverloadCandidates(ULE);
      return;
    }
  } else {
    S.Diag(Loc, diag::err_attribute_cleanup_arg_not_function) << 0;
    return;
  }

  if (FD->getNumParams() != 1) {
    S.Diag(Loc, diag::err_attribute_cleanup_func_must_take_one_arg)
      << NI.getName();
    return;
  }

  // We're currently more strict than GCC about what function types we accept.
  // If this ever proves to be a problem it should be easy to fix.
  QualType Ty = S.Context.getPointerType(cast<VarDecl>(D)->getType());
  QualType ParamTy = FD->getParamDecl(0)->getType();
  if (S.CheckAssignmentConstraints(FD->getParamDecl(0)->getLocation(),
                                   ParamTy, Ty) != Sema::Compatible) {
    S.Diag(Loc, diag::err_attribute_cleanup_func_arg_incompatible_type)
      << NI.getName() << ParamTy << Ty;
    return;
  }

  D->addAttr(::new (S.Context)
             CleanupAttr(AL.getRange(), S.Context, FD,
                         AL.getAttributeSpellingListIndex()));
}

static void handleEnumExtensibilityAttr(Sema &S, Decl *D,
                                        const ParsedAttr &AL) {
  if (!AL.isArgIdent(0)) {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_n_type)
        << AL << 0 << AANT_ArgumentIdentifier;
    return;
  }

  EnumExtensibilityAttr::Kind ExtensibilityKind;
  IdentifierInfo *II = AL.getArgAsIdent(0)->Ident;
  if (!EnumExtensibilityAttr::ConvertStrToKind(II->getName(),
                                               ExtensibilityKind)) {
    S.Diag(AL.getLoc(), diag::warn_attribute_type_not_supported) << AL << II;
    return;
  }

  D->addAttr(::new (S.Context) EnumExtensibilityAttr(
      AL.getRange(), S.Context, ExtensibilityKind,
      AL.getAttributeSpellingListIndex()));
}

/// Handle __attribute__((format_arg((idx)))) attribute based on
/// http://gcc.gnu.org/onlinedocs/gcc/Function-Attributes.html
static void handleFormatArgAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  Expr *IdxExpr = AL.getArgAsExpr(0);
  ParamIdx Idx;
  if (!checkFunctionOrMethodParameterIndex(S, D, AL, 1, IdxExpr, Idx))
    return;

  // Make sure the format string is really a string.
  QualType Ty = getFunctionOrMethodParamType(D, Idx.getASTIndex());

  bool NotNSStringTy = !isNSStringType(Ty, S.Context);
  if (NotNSStringTy &&
      !isCFStringType(Ty, S.Context) &&
      (!Ty->isPointerType() ||
       !Ty->getAs<PointerType>()->getPointeeType()->isCharType())) {
    S.Diag(AL.getLoc(), diag::err_format_attribute_not)
        << "a string type" << IdxExpr->getSourceRange()
        << getFunctionOrMethodParamRange(D, 0);
    return;
  }
  Ty = getFunctionOrMethodResultType(D);
  if (!isNSStringType(Ty, S.Context) &&
      !isCFStringType(Ty, S.Context) &&
      (!Ty->isPointerType() ||
       !Ty->getAs<PointerType>()->getPointeeType()->isCharType())) {
    S.Diag(AL.getLoc(), diag::err_format_attribute_result_not)
        << (NotNSStringTy ? "string type" : "NSString")
        << IdxExpr->getSourceRange() << getFunctionOrMethodParamRange(D, 0);
    return;
  }

  D->addAttr(::new (S.Context) FormatArgAttr(
      AL.getRange(), S.Context, Idx, AL.getAttributeSpellingListIndex()));
}

enum FormatAttrKind {
  CFStringFormat,
  NSStringFormat,
  StrftimeFormat,
  SupportedFormat,
  IgnoredFormat,
  InvalidFormat
};

/// getFormatAttrKind - Map from format attribute names to supported format
/// types.
static FormatAttrKind getFormatAttrKind(StringRef Format) {
  return llvm::StringSwitch<FormatAttrKind>(Format)
      // Check for formats that get handled specially.
      .Case("NSString", NSStringFormat)
      .Case("CFString", CFStringFormat)
      .Case("strftime", StrftimeFormat)

      // Otherwise, check for supported formats.
      .Cases("scanf", "printf", "printf0", "strfmon", SupportedFormat)
      .Cases("cmn_err", "vcmn_err", "zcmn_err", SupportedFormat)
      .Case("kprintf", SupportedFormat)         // OpenBSD.
      .Case("freebsd_kprintf", SupportedFormat) // FreeBSD.
      .Case("os_trace", SupportedFormat)
      .Case("os_log", SupportedFormat)

      .Cases("gcc_diag", "gcc_cdiag", "gcc_cxxdiag", "gcc_tdiag", IgnoredFormat)
      .Default(InvalidFormat);
}

/// Handle __attribute__((init_priority(priority))) attributes based on
/// http://gcc.gnu.org/onlinedocs/gcc/C_002b_002b-Attributes.html
static void handleInitPriorityAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (!S.getLangOpts().CPlusPlus) {
    S.Diag(AL.getLoc(), diag::warn_attribute_ignored) << AL;
    return;
  }

  if (S.getCurFunctionOrMethodDecl()) {
    S.Diag(AL.getLoc(), diag::err_init_priority_object_attr);
    AL.setInvalid();
    return;
  }
  QualType T = cast<VarDecl>(D)->getType();
  if (S.Context.getAsArrayType(T))
    T = S.Context.getBaseElementType(T);
  if (!T->getAs<RecordType>()) {
    S.Diag(AL.getLoc(), diag::err_init_priority_object_attr);
    AL.setInvalid();
    return;
  }

  Expr *E = AL.getArgAsExpr(0);
  uint32_t prioritynum;
  if (!checkUInt32Argument(S, AL, E, prioritynum)) {
    AL.setInvalid();
    return;
  }

  if (prioritynum < 101 || prioritynum > 65535) {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_outof_range)
        << E->getSourceRange() << AL << 101 << 65535;
    AL.setInvalid();
    return;
  }
  D->addAttr(::new (S.Context)
             InitPriorityAttr(AL.getRange(), S.Context, prioritynum,
                              AL.getAttributeSpellingListIndex()));
}

FormatAttr *Sema::mergeFormatAttr(Decl *D, SourceRange Range,
                                  IdentifierInfo *Format, int FormatIdx,
                                  int FirstArg,
                                  unsigned AttrSpellingListIndex) {
  // Check whether we already have an equivalent format attribute.
  for (auto *F : D->specific_attrs<FormatAttr>()) {
    if (F->getType() == Format &&
        F->getFormatIdx() == FormatIdx &&
        F->getFirstArg() == FirstArg) {
      // If we don't have a valid location for this attribute, adopt the
      // location.
      if (F->getLocation().isInvalid())
        F->setRange(Range);
      return nullptr;
    }
  }

  return ::new (Context) FormatAttr(Range, Context, Format, FormatIdx,
                                    FirstArg, AttrSpellingListIndex);
}

/// Handle __attribute__((format(type,idx,firstarg))) attributes based on
/// http://gcc.gnu.org/onlinedocs/gcc/Function-Attributes.html
static void handleFormatAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (!AL.isArgIdent(0)) {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_n_type)
        << AL << 1 << AANT_ArgumentIdentifier;
    return;
  }

  // In C++ the implicit 'this' function parameter also counts, and they are
  // counted from one.
  bool HasImplicitThisParam = isInstanceMethod(D);
  unsigned NumArgs = getFunctionOrMethodNumParams(D) + HasImplicitThisParam;

  IdentifierInfo *II = AL.getArgAsIdent(0)->Ident;
  StringRef Format = II->getName();

  if (normalizeName(Format)) {
    // If we've modified the string name, we need a new identifier for it.
    II = &S.Context.Idents.get(Format);
  }

  // Check for supported formats.
  FormatAttrKind Kind = getFormatAttrKind(Format);

  if (Kind == IgnoredFormat)
    return;

  if (Kind == InvalidFormat) {
    S.Diag(AL.getLoc(), diag::warn_attribute_type_not_supported)
        << AL << II->getName();
    return;
  }

  // checks for the 2nd argument
  Expr *IdxExpr = AL.getArgAsExpr(1);
  uint32_t Idx;
  if (!checkUInt32Argument(S, AL, IdxExpr, Idx, 2))
    return;

  if (Idx < 1 || Idx > NumArgs) {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_out_of_bounds)
        << AL << 2 << IdxExpr->getSourceRange();
    return;
  }

  // FIXME: Do we need to bounds check?
  unsigned ArgIdx = Idx - 1;

  if (HasImplicitThisParam) {
    if (ArgIdx == 0) {
      S.Diag(AL.getLoc(),
             diag::err_format_attribute_implicit_this_format_string)
        << IdxExpr->getSourceRange();
      return;
    }
    ArgIdx--;
  }

  // make sure the format string is really a string
  QualType Ty = getFunctionOrMethodParamType(D, ArgIdx);

  if (Kind == CFStringFormat) {
    if (!isCFStringType(Ty, S.Context)) {
      S.Diag(AL.getLoc(), diag::err_format_attribute_not)
        << "a CFString" << IdxExpr->getSourceRange()
        << getFunctionOrMethodParamRange(D, ArgIdx);
      return;
    }
  } else if (Kind == NSStringFormat) {
    // FIXME: do we need to check if the type is NSString*?  What are the
    // semantics?
    if (!isNSStringType(Ty, S.Context)) {
      S.Diag(AL.getLoc(), diag::err_format_attribute_not)
        << "an NSString" << IdxExpr->getSourceRange()
        << getFunctionOrMethodParamRange(D, ArgIdx);
      return;
    }
  } else if (!Ty->isPointerType() ||
             !Ty->getAs<PointerType>()->getPointeeType()->isCharType()) {
    S.Diag(AL.getLoc(), diag::err_format_attribute_not)
      << "a string type" << IdxExpr->getSourceRange()
      << getFunctionOrMethodParamRange(D, ArgIdx);
    return;
  }

  // check the 3rd argument
  Expr *FirstArgExpr = AL.getArgAsExpr(2);
  uint32_t FirstArg;
  if (!checkUInt32Argument(S, AL, FirstArgExpr, FirstArg, 3))
    return;

  // check if the function is variadic if the 3rd argument non-zero
  if (FirstArg != 0) {
    if (isFunctionOrMethodVariadic(D)) {
      ++NumArgs; // +1 for ...
    } else {
      S.Diag(D->getLocation(), diag::err_format_attribute_requires_variadic);
      return;
    }
  }

  // strftime requires FirstArg to be 0 because it doesn't read from any
  // variable the input is just the current time + the format string.
  if (Kind == StrftimeFormat) {
    if (FirstArg != 0) {
      S.Diag(AL.getLoc(), diag::err_format_strftime_third_parameter)
        << FirstArgExpr->getSourceRange();
      return;
    }
  // if 0 it disables parameter checking (to use with e.g. va_list)
  } else if (FirstArg != 0 && FirstArg != NumArgs) {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_out_of_bounds)
        << AL << 3 << FirstArgExpr->getSourceRange();
    return;
  }

  FormatAttr *NewAttr = S.mergeFormatAttr(D, AL.getRange(), II,
                                          Idx, FirstArg,
                                          AL.getAttributeSpellingListIndex());
  if (NewAttr)
    D->addAttr(NewAttr);
}

static void handleTransparentUnionAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  // Try to find the underlying union declaration.
  RecordDecl *RD = nullptr;
  const auto *TD = dyn_cast<TypedefNameDecl>(D);
  if (TD && TD->getUnderlyingType()->isUnionType())
    RD = TD->getUnderlyingType()->getAsUnionType()->getDecl();
  else
    RD = dyn_cast<RecordDecl>(D);

  if (!RD || !RD->isUnion()) {
    S.Diag(AL.getLoc(), diag::warn_attribute_wrong_decl_type) << AL
                                                              << ExpectedUnion;
    return;
  }

  if (!RD->isCompleteDefinition()) {
    if (!RD->isBeingDefined())
      S.Diag(AL.getLoc(),
             diag::warn_transparent_union_attribute_not_definition);
    return;
  }

  RecordDecl::field_iterator Field = RD->field_begin(),
                          FieldEnd = RD->field_end();
  if (Field == FieldEnd) {
    S.Diag(AL.getLoc(), diag::warn_transparent_union_attribute_zero_fields);
    return;
  }

  FieldDecl *FirstField = *Field;
  QualType FirstType = FirstField->getType();
  if (FirstType->hasFloatingRepresentation() || FirstType->isVectorType()) {
    S.Diag(FirstField->getLocation(),
           diag::warn_transparent_union_attribute_floating)
      << FirstType->isVectorType() << FirstType;
    return;
  }

  if (FirstType->isIncompleteType())
    return;
  uint64_t FirstSize = S.Context.getTypeSize(FirstType);
  uint64_t FirstAlign = S.Context.getTypeAlign(FirstType);
  for (; Field != FieldEnd; ++Field) {
    QualType FieldType = Field->getType();
    if (FieldType->isIncompleteType())
      return;
    // FIXME: this isn't fully correct; we also need to test whether the
    // members of the union would all have the same calling convention as the
    // first member of the union. Checking just the size and alignment isn't
    // sufficient (consider structs passed on the stack instead of in registers
    // as an example).
    if (S.Context.getTypeSize(FieldType) != FirstSize ||
        S.Context.getTypeAlign(FieldType) > FirstAlign) {
      // Warn if we drop the attribute.
      bool isSize = S.Context.getTypeSize(FieldType) != FirstSize;
      unsigned FieldBits = isSize? S.Context.getTypeSize(FieldType)
                                 : S.Context.getTypeAlign(FieldType);
      S.Diag(Field->getLocation(),
          diag::warn_transparent_union_attribute_field_size_align)
        << isSize << Field->getDeclName() << FieldBits;
      unsigned FirstBits = isSize? FirstSize : FirstAlign;
      S.Diag(FirstField->getLocation(),
             diag::note_transparent_union_first_field_size_align)
        << isSize << FirstBits;
      return;
    }
  }

  RD->addAttr(::new (S.Context)
              TransparentUnionAttr(AL.getRange(), S.Context,
                                   AL.getAttributeSpellingListIndex()));
}

static void handleAnnotateAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  // Make sure that there is a string literal as the annotation's single
  // argument.
  StringRef Str;
  if (!S.checkStringLiteralArgumentAttr(AL, 0, Str))
    return;

  // Don't duplicate annotations that are already set.
  for (const auto *I : D->specific_attrs<AnnotateAttr>()) {
    if (I->getAnnotation() == Str)
      return;
  }

  D->addAttr(::new (S.Context)
             AnnotateAttr(AL.getRange(), S.Context, Str,
                          AL.getAttributeSpellingListIndex()));
}

static void handleAlignValueAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  S.AddAlignValueAttr(AL.getRange(), D, AL.getArgAsExpr(0),
                      AL.getAttributeSpellingListIndex());
}

void Sema::AddAlignValueAttr(SourceRange AttrRange, Decl *D, Expr *E,
                             unsigned SpellingListIndex) {
  AlignValueAttr TmpAttr(AttrRange, Context, E, SpellingListIndex);
  SourceLocation AttrLoc = AttrRange.getBegin();

  QualType T;
  if (const auto *TD = dyn_cast<TypedefNameDecl>(D))
    T = TD->getUnderlyingType();
  else if (const auto *VD = dyn_cast<ValueDecl>(D))
    T = VD->getType();
  else
    llvm_unreachable("Unknown decl type for align_value");

  if (!T->isDependentType() && !T->isAnyPointerType() &&
      !T->isReferenceType() && !T->isMemberPointerType()) {
    Diag(AttrLoc, diag::warn_attribute_pointer_or_reference_only)
      << &TmpAttr /*TmpAttr.getName()*/ << T << D->getSourceRange();
    return;
  }

  if (!E->isValueDependent()) {
    llvm::APSInt Alignment;
    ExprResult ICE
      = VerifyIntegerConstantExpression(E, &Alignment,
          diag::err_align_value_attribute_argument_not_int,
            /*AllowFold*/ false);
    if (ICE.isInvalid())
      return;

    if (!Alignment.isPowerOf2()) {
      Diag(AttrLoc, diag::err_alignment_not_power_of_two)
        << E->getSourceRange();
      return;
    }

    D->addAttr(::new (Context)
               AlignValueAttr(AttrRange, Context, ICE.get(),
               SpellingListIndex));
    return;
  }

  // Save dependent expressions in the AST to be instantiated.
  D->addAttr(::new (Context) AlignValueAttr(TmpAttr));
}

static void handleAlignedAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  // check the attribute arguments.
  if (AL.getNumArgs() > 1) {
    S.Diag(AL.getLoc(), diag::err_attribute_wrong_number_arguments) << AL << 1;
    return;
  }

  if (AL.getNumArgs() == 0) {
    D->addAttr(::new (S.Context) AlignedAttr(AL.getRange(), S.Context,
               true, nullptr, AL.getAttributeSpellingListIndex()));
    return;
  }

  Expr *E = AL.getArgAsExpr(0);
  if (AL.isPackExpansion() && !E->containsUnexpandedParameterPack()) {
    S.Diag(AL.getEllipsisLoc(),
           diag::err_pack_expansion_without_parameter_packs);
    return;
  }

  if (!AL.isPackExpansion() && S.DiagnoseUnexpandedParameterPack(E))
    return;

  S.AddAlignedAttr(AL.getRange(), D, E, AL.getAttributeSpellingListIndex(),
                   AL.isPackExpansion());
}

void Sema::AddAlignedAttr(SourceRange AttrRange, Decl *D, Expr *E,
                          unsigned SpellingListIndex, bool IsPackExpansion) {
  AlignedAttr TmpAttr(AttrRange, Context, true, E, SpellingListIndex);
  SourceLocation AttrLoc = AttrRange.getBegin();

  // C++11 alignas(...) and C11 _Alignas(...) have additional requirements.
  if (TmpAttr.isAlignas()) {
    // C++11 [dcl.align]p1:
    //   An alignment-specifier may be applied to a variable or to a class
    //   data member, but it shall not be applied to a bit-field, a function
    //   parameter, the formal parameter of a catch clause, or a variable
    //   declared with the register storage class specifier. An
    //   alignment-specifier may also be applied to the declaration of a class
    //   or enumeration type.
    // C11 6.7.5/2:
    //   An alignment attribute shall not be specified in a declaration of
    //   a typedef, or a bit-field, or a function, or a parameter, or an
    //   object declared with the register storage-class specifier.
    int DiagKind = -1;
    if (isa<ParmVarDecl>(D)) {
      DiagKind = 0;
    } else if (const auto *VD = dyn_cast<VarDecl>(D)) {
      if (VD->getStorageClass() == SC_Register)
        DiagKind = 1;
      if (VD->isExceptionVariable())
        DiagKind = 2;
    } else if (const auto *FD = dyn_cast<FieldDecl>(D)) {
      if (FD->isBitField())
        DiagKind = 3;
    } else if (!isa<TagDecl>(D)) {
      Diag(AttrLoc, diag::err_attribute_wrong_decl_type) << &TmpAttr
        << (TmpAttr.isC11() ? ExpectedVariableOrField
                            : ExpectedVariableFieldOrTag);
      return;
    }
    if (DiagKind != -1) {
      Diag(AttrLoc, diag::err_alignas_attribute_wrong_decl_type)
        << &TmpAttr << DiagKind;
      return;
    }
  }

  if (E->isValueDependent()) {
    // We can't support a dependent alignment on a non-dependent type,
    // because we have no way to model that a type is "alignment-dependent"
    // but not dependent in any other way.
    if (const auto *TND = dyn_cast<TypedefNameDecl>(D)) {
      if (!TND->getUnderlyingType()->isDependentType()) {
        Diag(AttrLoc, diag::err_alignment_dependent_typedef_name)
            << E->getSourceRange();
        return;
      }
    }

    // Save dependent expressions in the AST to be instantiated.
    AlignedAttr *AA = ::new (Context) AlignedAttr(TmpAttr);
    AA->setPackExpansion(IsPackExpansion);
    D->addAttr(AA);
    return;
  }

  // FIXME: Cache the number on the AL object?
  llvm::APSInt Alignment;
  ExprResult ICE
    = VerifyIntegerConstantExpression(E, &Alignment,
        diag::err_aligned_attribute_argument_not_int,
        /*AllowFold*/ false);
  if (ICE.isInvalid())
    return;

  uint64_t AlignVal = Alignment.getZExtValue();

  // C++11 [dcl.align]p2:
  //   -- if the constant expression evaluates to zero, the alignment
  //      specifier shall have no effect
  // C11 6.7.5p6:
  //   An alignment specification of zero has no effect.
  if (!(TmpAttr.isAlignas() && !Alignment)) {
    if (!llvm::isPowerOf2_64(AlignVal)) {
      Diag(AttrLoc, diag::err_alignment_not_power_of_two)
        << E->getSourceRange();
      return;
    }
  }

  // Alignment calculations can wrap around if it's greater than 2**28.
  unsigned MaxValidAlignment =
      Context.getTargetInfo().getTriple().isOSBinFormatCOFF() ? 8192
                                                              : 268435456;
  if (AlignVal > MaxValidAlignment) {
    Diag(AttrLoc, diag::err_attribute_aligned_too_great) << MaxValidAlignment
                                                         << E->getSourceRange();
    return;
  }

  if (Context.getTargetInfo().isTLSSupported()) {
    unsigned MaxTLSAlign =
        Context.toCharUnitsFromBits(Context.getTargetInfo().getMaxTLSAlign())
            .getQuantity();
    const auto *VD = dyn_cast<VarDecl>(D);
    if (MaxTLSAlign && AlignVal > MaxTLSAlign && VD &&
        VD->getTLSKind() != VarDecl::TLS_None) {
      Diag(VD->getLocation(), diag::err_tls_var_aligned_over_maximum)
          << (unsigned)AlignVal << VD << MaxTLSAlign;
      return;
    }
  }

  AlignedAttr *AA = ::new (Context) AlignedAttr(AttrRange, Context, true,
                                                ICE.get(), SpellingListIndex);
  AA->setPackExpansion(IsPackExpansion);
  D->addAttr(AA);
}

void Sema::AddAlignedAttr(SourceRange AttrRange, Decl *D, TypeSourceInfo *TS,
                          unsigned SpellingListIndex, bool IsPackExpansion) {
  // FIXME: Cache the number on the AL object if non-dependent?
  // FIXME: Perform checking of type validity
  AlignedAttr *AA = ::new (Context) AlignedAttr(AttrRange, Context, false, TS,
                                                SpellingListIndex);
  AA->setPackExpansion(IsPackExpansion);
  D->addAttr(AA);
}

void Sema::CheckAlignasUnderalignment(Decl *D) {
  assert(D->hasAttrs() && "no attributes on decl");

  QualType UnderlyingTy, DiagTy;
  if (const auto *VD = dyn_cast<ValueDecl>(D)) {
    UnderlyingTy = DiagTy = VD->getType();
  } else {
    UnderlyingTy = DiagTy = Context.getTagDeclType(cast<TagDecl>(D));
    if (const auto *ED = dyn_cast<EnumDecl>(D))
      UnderlyingTy = ED->getIntegerType();
  }
  if (DiagTy->isDependentType() || DiagTy->isIncompleteType())
    return;

  // C++11 [dcl.align]p5, C11 6.7.5/4:
  //   The combined effect of all alignment attributes in a declaration shall
  //   not specify an alignment that is less strict than the alignment that
  //   would otherwise be required for the entity being declared.
  AlignedAttr *AlignasAttr = nullptr;
  unsigned Align = 0;
  for (auto *I : D->specific_attrs<AlignedAttr>()) {
    if (I->isAlignmentDependent())
      return;
    if (I->isAlignas())
      AlignasAttr = I;
    Align = std::max(Align, I->getAlignment(Context));
  }

  if (AlignasAttr && Align) {
    CharUnits RequestedAlign = Context.toCharUnitsFromBits(Align);
    CharUnits NaturalAlign = Context.getTypeAlignInChars(UnderlyingTy);
    if (NaturalAlign > RequestedAlign)
      Diag(AlignasAttr->getLocation(), diag::err_alignas_underaligned)
        << DiagTy << (unsigned)NaturalAlign.getQuantity();
  }
}

bool Sema::checkMSInheritanceAttrOnDefinition(
    CXXRecordDecl *RD, SourceRange Range, bool BestCase,
    MSInheritanceAttr::Spelling SemanticSpelling) {
  assert(RD->hasDefinition() && "RD has no definition!");

  // We may not have seen base specifiers or any virtual methods yet.  We will
  // have to wait until the record is defined to catch any mismatches.
  if (!RD->getDefinition()->isCompleteDefinition())
    return false;

  // The unspecified model never matches what a definition could need.
  if (SemanticSpelling == MSInheritanceAttr::Keyword_unspecified_inheritance)
    return false;

  if (BestCase) {
    if (RD->calculateInheritanceModel() == SemanticSpelling)
      return false;
  } else {
    if (RD->calculateInheritanceModel() <= SemanticSpelling)
      return false;
  }

  Diag(Range.getBegin(), diag::err_mismatched_ms_inheritance)
      << 0 /*definition*/;
  Diag(RD->getDefinition()->getLocation(), diag::note_defined_here)
      << RD->getNameAsString();
  return true;
}

/// parseModeAttrArg - Parses attribute mode string and returns parsed type
/// attribute.
static void parseModeAttrArg(Sema &S, StringRef Str, unsigned &DestWidth,
                             bool &IntegerMode, bool &ComplexMode) {
  IntegerMode = true;
  ComplexMode = false;
  switch (Str.size()) {
  case 2:
    switch (Str[0]) {
    case 'Q':
      DestWidth = 8;
      break;
    case 'H':
      DestWidth = 16;
      break;
    case 'S':
      DestWidth = 32;
      break;
    case 'D':
      DestWidth = 64;
      break;
    case 'X':
      DestWidth = 96;
      break;
    case 'T':
      DestWidth = 128;
      break;
    }
    if (Str[1] == 'F') {
      IntegerMode = false;
    } else if (Str[1] == 'C') {
      IntegerMode = false;
      ComplexMode = true;
    } else if (Str[1] != 'I') {
      DestWidth = 0;
    }
    break;
  case 4:
    // FIXME: glibc uses 'word' to define register_t; this is narrower than a
    // pointer on PIC16 and other embedded platforms.
    if (Str == "word")
      DestWidth = S.Context.getTargetInfo().getRegisterWidth();
    else if (Str == "byte")
      DestWidth = S.Context.getTargetInfo().getCharWidth();
    break;
  case 7:
    if (Str == "pointer")
      DestWidth = S.Context.getTargetInfo().getPointerWidth(0);
    break;
  case 11:
    if (Str == "unwind_word")
      DestWidth = S.Context.getTargetInfo().getUnwindWordWidth();
    break;
  }
}

/// handleModeAttr - This attribute modifies the width of a decl with primitive
/// type.
///
/// Despite what would be logical, the mode attribute is a decl attribute, not a
/// type attribute: 'int ** __attribute((mode(HI))) *G;' tries to make 'G' be
/// HImode, not an intermediate pointer.
static void handleModeAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  // This attribute isn't documented, but glibc uses it.  It changes
  // the width of an int or unsigned int to the specified size.
  if (!AL.isArgIdent(0)) {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_type)
        << AL << AANT_ArgumentIdentifier;
    return;
  }

  IdentifierInfo *Name = AL.getArgAsIdent(0)->Ident;

  S.AddModeAttr(AL.getRange(), D, Name, AL.getAttributeSpellingListIndex());
}

void Sema::AddModeAttr(SourceRange AttrRange, Decl *D, IdentifierInfo *Name,
                       unsigned SpellingListIndex, bool InInstantiation) {
  StringRef Str = Name->getName();
  normalizeName(Str);
  SourceLocation AttrLoc = AttrRange.getBegin();

  unsigned DestWidth = 0;
  bool IntegerMode = true;
  bool ComplexMode = false;
  llvm::APInt VectorSize(64, 0);
  if (Str.size() >= 4 && Str[0] == 'V') {
    // Minimal length of vector mode is 4: 'V' + NUMBER(>=1) + TYPE(>=2).
    size_t StrSize = Str.size();
    size_t VectorStringLength = 0;
    while ((VectorStringLength + 1) < StrSize &&
           isdigit(Str[VectorStringLength + 1]))
      ++VectorStringLength;
    if (VectorStringLength &&
        !Str.substr(1, VectorStringLength).getAsInteger(10, VectorSize) &&
        VectorSize.isPowerOf2()) {
      parseModeAttrArg(*this, Str.substr(VectorStringLength + 1), DestWidth,
                       IntegerMode, ComplexMode);
      // Avoid duplicate warning from template instantiation.
      if (!InInstantiation)
        Diag(AttrLoc, diag::warn_vector_mode_deprecated);
    } else {
      VectorSize = 0;
    }
  }

  if (!VectorSize)
    parseModeAttrArg(*this, Str, DestWidth, IntegerMode, ComplexMode);

  // FIXME: Sync this with InitializePredefinedMacros; we need to match int8_t
  // and friends, at least with glibc.
  // FIXME: Make sure floating-point mappings are accurate
  // FIXME: Support XF and TF types
  if (!DestWidth) {
    Diag(AttrLoc, diag::err_machine_mode) << 0 /*Unknown*/ << Name;
    return;
  }

  QualType OldTy;
  if (const auto *TD = dyn_cast<TypedefNameDecl>(D))
    OldTy = TD->getUnderlyingType();
  else if (const auto *ED = dyn_cast<EnumDecl>(D)) {
    // Something like 'typedef enum { X } __attribute__((mode(XX))) T;'.
    // Try to get type from enum declaration, default to int.
    OldTy = ED->getIntegerType();
    if (OldTy.isNull())
      OldTy = Context.IntTy;
  } else
    OldTy = cast<ValueDecl>(D)->getType();

  if (OldTy->isDependentType()) {
    D->addAttr(::new (Context)
               ModeAttr(AttrRange, Context, Name, SpellingListIndex));
    return;
  }

  // Base type can also be a vector type (see PR17453).
  // Distinguish between base type and base element type.
  QualType OldElemTy = OldTy;
  if (const auto *VT = OldTy->getAs<VectorType>())
    OldElemTy = VT->getElementType();

  // GCC allows 'mode' attribute on enumeration types (even incomplete), except
  // for vector modes. So, 'enum X __attribute__((mode(QI)));' forms a complete
  // type, 'enum { A } __attribute__((mode(V4SI)))' is rejected.
  if ((isa<EnumDecl>(D) || OldElemTy->getAs<EnumType>()) &&
      VectorSize.getBoolValue()) {
    Diag(AttrLoc, diag::err_enum_mode_vector_type) << Name << AttrRange;
    return;
  }
  bool IntegralOrAnyEnumType =
      OldElemTy->isIntegralOrEnumerationType() || OldElemTy->getAs<EnumType>();

  if (!OldElemTy->getAs<BuiltinType>() && !OldElemTy->isComplexType() &&
      !IntegralOrAnyEnumType)
    Diag(AttrLoc, diag::err_mode_not_primitive);
  else if (IntegerMode) {
    if (!IntegralOrAnyEnumType)
      Diag(AttrLoc, diag::err_mode_wrong_type);
  } else if (ComplexMode) {
    if (!OldElemTy->isComplexType())
      Diag(AttrLoc, diag::err_mode_wrong_type);
  } else {
    if (!OldElemTy->isFloatingType())
      Diag(AttrLoc, diag::err_mode_wrong_type);
  }

  QualType NewElemTy;

  if (IntegerMode)
    NewElemTy = Context.getIntTypeForBitwidth(DestWidth,
                                              OldElemTy->isSignedIntegerType());
  else
    NewElemTy = Context.getRealTypeForBitwidth(DestWidth);

  if (NewElemTy.isNull()) {
    Diag(AttrLoc, diag::err_machine_mode) << 1 /*Unsupported*/ << Name;
    return;
  }

  if (ComplexMode) {
    NewElemTy = Context.getComplexType(NewElemTy);
  }

  QualType NewTy = NewElemTy;
  if (VectorSize.getBoolValue()) {
    NewTy = Context.getVectorType(NewTy, VectorSize.getZExtValue(),
                                  VectorType::GenericVector);
  } else if (const auto *OldVT = OldTy->getAs<VectorType>()) {
    // Complex machine mode does not support base vector types.
    if (ComplexMode) {
      Diag(AttrLoc, diag::err_complex_mode_vector_type);
      return;
    }
    unsigned NumElements = Context.getTypeSize(OldElemTy) *
                           OldVT->getNumElements() /
                           Context.getTypeSize(NewElemTy);
    NewTy =
        Context.getVectorType(NewElemTy, NumElements, OldVT->getVectorKind());
  }

  if (NewTy.isNull()) {
    Diag(AttrLoc, diag::err_mode_wrong_type);
    return;
  }

  // Install the new type.
  if (auto *TD = dyn_cast<TypedefNameDecl>(D))
    TD->setModedTypeSourceInfo(TD->getTypeSourceInfo(), NewTy);
  else if (auto *ED = dyn_cast<EnumDecl>(D))
    ED->setIntegerType(NewTy);
  else
    cast<ValueDecl>(D)->setType(NewTy);

  D->addAttr(::new (Context)
             ModeAttr(AttrRange, Context, Name, SpellingListIndex));
}

static void handleNoDebugAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  D->addAttr(::new (S.Context)
             NoDebugAttr(AL.getRange(), S.Context,
                         AL.getAttributeSpellingListIndex()));
}

AlwaysInlineAttr *Sema::mergeAlwaysInlineAttr(Decl *D, SourceRange Range,
                                              IdentifierInfo *Ident,
                                              unsigned AttrSpellingListIndex) {
  if (OptimizeNoneAttr *Optnone = D->getAttr<OptimizeNoneAttr>()) {
    Diag(Range.getBegin(), diag::warn_attribute_ignored) << Ident;
    Diag(Optnone->getLocation(), diag::note_conflicting_attribute);
    return nullptr;
  }

  if (D->hasAttr<AlwaysInlineAttr>())
    return nullptr;

  return ::new (Context) AlwaysInlineAttr(Range, Context,
                                          AttrSpellingListIndex);
}

CommonAttr *Sema::mergeCommonAttr(Decl *D, const ParsedAttr &AL) {
  if (checkAttrMutualExclusion<InternalLinkageAttr>(*this, D, AL))
    return nullptr;

  return ::new (Context)
      CommonAttr(AL.getRange(), Context, AL.getAttributeSpellingListIndex());
}

CommonAttr *Sema::mergeCommonAttr(Decl *D, const CommonAttr &AL) {
  if (checkAttrMutualExclusion<InternalLinkageAttr>(*this, D, AL))
    return nullptr;

  return ::new (Context)
      CommonAttr(AL.getRange(), Context, AL.getSpellingListIndex());
}

InternalLinkageAttr *Sema::mergeInternalLinkageAttr(Decl *D,
                                                    const ParsedAttr &AL) {
  if (const auto *VD = dyn_cast<VarDecl>(D)) {
    // Attribute applies to Var but not any subclass of it (like ParmVar,
    // ImplicitParm or VarTemplateSpecialization).
    if (VD->getKind() != Decl::Var) {
      Diag(AL.getLoc(), diag::warn_attribute_wrong_decl_type)
          << AL << (getLangOpts().CPlusPlus ? ExpectedFunctionVariableOrClass
                                            : ExpectedVariableOrFunction);
      return nullptr;
    }
    // Attribute does not apply to non-static local variables.
    if (VD->hasLocalStorage()) {
      Diag(VD->getLocation(), diag::warn_internal_linkage_local_storage);
      return nullptr;
    }
  }

  if (checkAttrMutualExclusion<CommonAttr>(*this, D, AL))
    return nullptr;

  return ::new (Context) InternalLinkageAttr(
      AL.getRange(), Context, AL.getAttributeSpellingListIndex());
}
InternalLinkageAttr *
Sema::mergeInternalLinkageAttr(Decl *D, const InternalLinkageAttr &AL) {
  if (const auto *VD = dyn_cast<VarDecl>(D)) {
    // Attribute applies to Var but not any subclass of it (like ParmVar,
    // ImplicitParm or VarTemplateSpecialization).
    if (VD->getKind() != Decl::Var) {
      Diag(AL.getLocation(), diag::warn_attribute_wrong_decl_type)
          << &AL << (getLangOpts().CPlusPlus ? ExpectedFunctionVariableOrClass
                                             : ExpectedVariableOrFunction);
      return nullptr;
    }
    // Attribute does not apply to non-static local variables.
    if (VD->hasLocalStorage()) {
      Diag(VD->getLocation(), diag::warn_internal_linkage_local_storage);
      return nullptr;
    }
  }

  if (checkAttrMutualExclusion<CommonAttr>(*this, D, AL))
    return nullptr;

  return ::new (Context)
      InternalLinkageAttr(AL.getRange(), Context, AL.getSpellingListIndex());
}

MinSizeAttr *Sema::mergeMinSizeAttr(Decl *D, SourceRange Range,
                                    unsigned AttrSpellingListIndex) {
  if (OptimizeNoneAttr *Optnone = D->getAttr<OptimizeNoneAttr>()) {
    Diag(Range.getBegin(), diag::warn_attribute_ignored) << "'minsize'";
    Diag(Optnone->getLocation(), diag::note_conflicting_attribute);
    return nullptr;
  }

  if (D->hasAttr<MinSizeAttr>())
    return nullptr;

  return ::new (Context) MinSizeAttr(Range, Context, AttrSpellingListIndex);
}

OptimizeNoneAttr *Sema::mergeOptimizeNoneAttr(Decl *D, SourceRange Range,
                                              unsigned AttrSpellingListIndex) {
  if (AlwaysInlineAttr *Inline = D->getAttr<AlwaysInlineAttr>()) {
    Diag(Inline->getLocation(), diag::warn_attribute_ignored) << Inline;
    Diag(Range.getBegin(), diag::note_conflicting_attribute);
    D->dropAttr<AlwaysInlineAttr>();
  }
  if (MinSizeAttr *MinSize = D->getAttr<MinSizeAttr>()) {
    Diag(MinSize->getLocation(), diag::warn_attribute_ignored) << MinSize;
    Diag(Range.getBegin(), diag::note_conflicting_attribute);
    D->dropAttr<MinSizeAttr>();
  }

  if (D->hasAttr<OptimizeNoneAttr>())
    return nullptr;

  return ::new (Context) OptimizeNoneAttr(Range, Context,
                                          AttrSpellingListIndex);
}

static void handleAlwaysInlineAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (checkAttrMutualExclusion<NotTailCalledAttr>(S, D, AL))
    return;

  if (AlwaysInlineAttr *Inline = S.mergeAlwaysInlineAttr(
          D, AL.getRange(), AL.getName(),
          AL.getAttributeSpellingListIndex()))
    D->addAttr(Inline);
}

static void handleMinSizeAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (MinSizeAttr *MinSize = S.mergeMinSizeAttr(
          D, AL.getRange(), AL.getAttributeSpellingListIndex()))
    D->addAttr(MinSize);
}

static void handleOptimizeNoneAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (OptimizeNoneAttr *Optnone = S.mergeOptimizeNoneAttr(
          D, AL.getRange(), AL.getAttributeSpellingListIndex()))
    D->addAttr(Optnone);
}

static void handleConstantAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (checkAttrMutualExclusion<CUDASharedAttr>(S, D, AL))
    return;
  const auto *VD = cast<VarDecl>(D);
  if (!VD->hasGlobalStorage()) {
    S.Diag(AL.getLoc(), diag::err_cuda_nonglobal_constant);
    return;
  }
  D->addAttr(::new (S.Context) CUDAConstantAttr(
      AL.getRange(), S.Context, AL.getAttributeSpellingListIndex()));
}

static void handleSharedAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (checkAttrMutualExclusion<CUDAConstantAttr>(S, D, AL))
    return;
  const auto *VD = cast<VarDecl>(D);
  // extern __shared__ is only allowed on arrays with no length (e.g.
  // "int x[]").
  if (!S.getLangOpts().GPURelocatableDeviceCode && VD->hasExternalStorage() &&
      !isa<IncompleteArrayType>(VD->getType())) {
    S.Diag(AL.getLoc(), diag::err_cuda_extern_shared) << VD;
    return;
  }
  if (S.getLangOpts().CUDA && VD->hasLocalStorage() &&
      S.CUDADiagIfHostCode(AL.getLoc(), diag::err_cuda_host_shared)
          << S.CurrentCUDATarget())
    return;
  D->addAttr(::new (S.Context) CUDASharedAttr(
      AL.getRange(), S.Context, AL.getAttributeSpellingListIndex()));
}

static void handleGlobalAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (checkAttrMutualExclusion<CUDADeviceAttr>(S, D, AL) ||
      checkAttrMutualExclusion<CUDAHostAttr>(S, D, AL)) {
    return;
  }
  const auto *FD = cast<FunctionDecl>(D);
  if (!FD->getReturnType()->isVoidType()) {
    SourceRange RTRange = FD->getReturnTypeSourceRange();
    S.Diag(FD->getTypeSpecStartLoc(), diag::err_kern_type_not_void_return)
        << FD->getType()
        << (RTRange.isValid() ? FixItHint::CreateReplacement(RTRange, "void")
                              : FixItHint());
    return;
  }
  if (const auto *Method = dyn_cast<CXXMethodDecl>(FD)) {
    if (Method->isInstance()) {
      S.Diag(Method->getBeginLoc(), diag::err_kern_is_nonstatic_method)
          << Method;
      return;
    }
    S.Diag(Method->getBeginLoc(), diag::warn_kern_is_method) << Method;
  }
  // Only warn for "inline" when compiling for host, to cut down on noise.
  if (FD->isInlineSpecified() && !S.getLangOpts().CUDAIsDevice)
    S.Diag(FD->getBeginLoc(), diag::warn_kern_is_inline) << FD;

  D->addAttr(::new (S.Context)
              CUDAGlobalAttr(AL.getRange(), S.Context,
                             AL.getAttributeSpellingListIndex()));
}

static void handleGNUInlineAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  const auto *Fn = cast<FunctionDecl>(D);
  if (!Fn->isInlineSpecified()) {
    S.Diag(AL.getLoc(), diag::warn_gnu_inline_attribute_requires_inline);
    return;
  }

  D->addAttr(::new (S.Context)
             GNUInlineAttr(AL.getRange(), S.Context,
                           AL.getAttributeSpellingListIndex()));
}

static void handleCallConvAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (hasDeclarator(D)) return;

  // Diagnostic is emitted elsewhere: here we store the (valid) AL
  // in the Decl node for syntactic reasoning, e.g., pretty-printing.
  CallingConv CC;
  if (S.CheckCallingConvAttr(AL, CC, /*FD*/nullptr))
    return;

  if (!isa<ObjCMethodDecl>(D)) {
    S.Diag(AL.getLoc(), diag::warn_attribute_wrong_decl_type)
        << AL << ExpectedFunctionOrMethod;
    return;
  }

  switch (AL.getKind()) {
  case ParsedAttr::AT_FastCall:
    D->addAttr(::new (S.Context)
               FastCallAttr(AL.getRange(), S.Context,
                            AL.getAttributeSpellingListIndex()));
    return;
  case ParsedAttr::AT_StdCall:
    D->addAttr(::new (S.Context)
               StdCallAttr(AL.getRange(), S.Context,
                           AL.getAttributeSpellingListIndex()));
    return;
  case ParsedAttr::AT_ThisCall:
    D->addAttr(::new (S.Context)
               ThisCallAttr(AL.getRange(), S.Context,
                            AL.getAttributeSpellingListIndex()));
    return;
  case ParsedAttr::AT_CDecl:
    D->addAttr(::new (S.Context)
               CDeclAttr(AL.getRange(), S.Context,
                         AL.getAttributeSpellingListIndex()));
    return;
  case ParsedAttr::AT_Pascal:
    D->addAttr(::new (S.Context)
               PascalAttr(AL.getRange(), S.Context,
                          AL.getAttributeSpellingListIndex()));
    return;
  case ParsedAttr::AT_SwiftCall:
    D->addAttr(::new (S.Context)
               SwiftCallAttr(AL.getRange(), S.Context,
                             AL.getAttributeSpellingListIndex()));
    return;
  case ParsedAttr::AT_VectorCall:
    D->addAttr(::new (S.Context)
               VectorCallAttr(AL.getRange(), S.Context,
                              AL.getAttributeSpellingListIndex()));
    return;
  case ParsedAttr::AT_MSABI:
    D->addAttr(::new (S.Context)
               MSABIAttr(AL.getRange(), S.Context,
                         AL.getAttributeSpellingListIndex()));
    return;
  case ParsedAttr::AT_SysVABI:
    D->addAttr(::new (S.Context)
               SysVABIAttr(AL.getRange(), S.Context,
                           AL.getAttributeSpellingListIndex()));
    return;
  case ParsedAttr::AT_RegCall:
    D->addAttr(::new (S.Context) RegCallAttr(
        AL.getRange(), S.Context, AL.getAttributeSpellingListIndex()));
    return;
  case ParsedAttr::AT_Pcs: {
    PcsAttr::PCSType PCS;
    switch (CC) {
    case CC_AAPCS:
      PCS = PcsAttr::AAPCS;
      break;
    case CC_AAPCS_VFP:
      PCS = PcsAttr::AAPCS_VFP;
      break;
    default:
      llvm_unreachable("unexpected calling convention in pcs attribute");
    }

    D->addAttr(::new (S.Context)
               PcsAttr(AL.getRange(), S.Context, PCS,
                       AL.getAttributeSpellingListIndex()));
    return;
  }
  case ParsedAttr::AT_AArch64VectorPcs:
    D->addAttr(::new(S.Context)
               AArch64VectorPcsAttr(AL.getRange(), S.Context,
                                    AL.getAttributeSpellingListIndex()));
    return;
  case ParsedAttr::AT_IntelOclBicc:
    D->addAttr(::new (S.Context)
               IntelOclBiccAttr(AL.getRange(), S.Context,
                                AL.getAttributeSpellingListIndex()));
    return;
  case ParsedAttr::AT_PreserveMost:
    D->addAttr(::new (S.Context) PreserveMostAttr(
        AL.getRange(), S.Context, AL.getAttributeSpellingListIndex()));
    return;
  case ParsedAttr::AT_PreserveAll:
    D->addAttr(::new (S.Context) PreserveAllAttr(
        AL.getRange(), S.Context, AL.getAttributeSpellingListIndex()));
    return;
  default:
    llvm_unreachable("unexpected attribute kind");
  }
}

static void handleSuppressAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (!checkAttributeAtLeastNumArgs(S, AL, 1))
    return;

  std::vector<StringRef> DiagnosticIdentifiers;
  for (unsigned I = 0, E = AL.getNumArgs(); I != E; ++I) {
    StringRef RuleName;

    if (!S.checkStringLiteralArgumentAttr(AL, I, RuleName, nullptr))
      return;

    // FIXME: Warn if the rule name is unknown. This is tricky because only
    // clang-tidy knows about available rules.
    DiagnosticIdentifiers.push_back(RuleName);
  }
  D->addAttr(::new (S.Context) SuppressAttr(
      AL.getRange(), S.Context, DiagnosticIdentifiers.data(),
      DiagnosticIdentifiers.size(), AL.getAttributeSpellingListIndex()));
}

bool Sema::CheckCallingConvAttr(const ParsedAttr &Attrs, CallingConv &CC,
                                const FunctionDecl *FD) {
  if (Attrs.isInvalid())
    return true;

  if (Attrs.hasProcessingCache()) {
    CC = (CallingConv) Attrs.getProcessingCache();
    return false;
  }

  unsigned ReqArgs = Attrs.getKind() == ParsedAttr::AT_Pcs ? 1 : 0;
  if (!checkAttributeNumArgs(*this, Attrs, ReqArgs)) {
    Attrs.setInvalid();
    return true;
  }

  // TODO: diagnose uses of these conventions on the wrong target.
  switch (Attrs.getKind()) {
  case ParsedAttr::AT_CDecl:
    CC = CC_C;
    break;
  case ParsedAttr::AT_FastCall:
    CC = CC_X86FastCall;
    break;
  case ParsedAttr::AT_StdCall:
    CC = CC_X86StdCall;
    break;
  case ParsedAttr::AT_ThisCall:
    CC = CC_X86ThisCall;
    break;
  case ParsedAttr::AT_Pascal:
    CC = CC_X86Pascal;
    break;
  case ParsedAttr::AT_SwiftCall:
    CC = CC_Swift;
    break;
  case ParsedAttr::AT_VectorCall:
    CC = CC_X86VectorCall;
    break;
  case ParsedAttr::AT_AArch64VectorPcs:
    CC = CC_AArch64VectorCall;
    break;
  case ParsedAttr::AT_RegCall:
    CC = CC_X86RegCall;
    break;
  case ParsedAttr::AT_MSABI:
    CC = Context.getTargetInfo().getTriple().isOSWindows() ? CC_C :
                                                             CC_Win64;
    break;
  case ParsedAttr::AT_SysVABI:
    CC = Context.getTargetInfo().getTriple().isOSWindows() ? CC_X86_64SysV :
                                                             CC_C;
    break;
  case ParsedAttr::AT_Pcs: {
    StringRef StrRef;
    if (!checkStringLiteralArgumentAttr(Attrs, 0, StrRef)) {
      Attrs.setInvalid();
      return true;
    }
    if (StrRef == "aapcs") {
      CC = CC_AAPCS;
      break;
    } else if (StrRef == "aapcs-vfp") {
      CC = CC_AAPCS_VFP;
      break;
    }

    Attrs.setInvalid();
    Diag(Attrs.getLoc(), diag::err_invalid_pcs);
    return true;
  }
  case ParsedAttr::AT_IntelOclBicc:
    CC = CC_IntelOclBicc;
    break;
  case ParsedAttr::AT_PreserveMost:
    CC = CC_PreserveMost;
    break;
  case ParsedAttr::AT_PreserveAll:
    CC = CC_PreserveAll;
    break;
  default: llvm_unreachable("unexpected attribute kind");
  }

  const TargetInfo &TI = Context.getTargetInfo();
  TargetInfo::CallingConvCheckResult A = TI.checkCallingConvention(CC);
  if (A != TargetInfo::CCCR_OK) {
    if (A == TargetInfo::CCCR_Warning)
      Diag(Attrs.getLoc(), diag::warn_cconv_ignored) << Attrs;

    // This convention is not valid for the target. Use the default function or
    // method calling convention.
    bool IsCXXMethod = false, IsVariadic = false;
    if (FD) {
      IsCXXMethod = FD->isCXXInstanceMember();
      IsVariadic = FD->isVariadic();
    }
    CC = Context.getDefaultCallingConvention(IsVariadic, IsCXXMethod);
  }

  Attrs.setProcessingCache((unsigned) CC);
  return false;
}

/// Pointer-like types in the default address space.
static bool isValidSwiftContextType(QualType Ty) {
  if (!Ty->hasPointerRepresentation())
    return Ty->isDependentType();
  return Ty->getPointeeType().getAddressSpace() == LangAS::Default;
}

/// Pointers and references in the default address space.
static bool isValidSwiftIndirectResultType(QualType Ty) {
  if (const auto *PtrType = Ty->getAs<PointerType>()) {
    Ty = PtrType->getPointeeType();
  } else if (const auto *RefType = Ty->getAs<ReferenceType>()) {
    Ty = RefType->getPointeeType();
  } else {
    return Ty->isDependentType();
  }
  return Ty.getAddressSpace() == LangAS::Default;
}

/// Pointers and references to pointers in the default address space.
static bool isValidSwiftErrorResultType(QualType Ty) {
  if (const auto *PtrType = Ty->getAs<PointerType>()) {
    Ty = PtrType->getPointeeType();
  } else if (const auto *RefType = Ty->getAs<ReferenceType>()) {
    Ty = RefType->getPointeeType();
  } else {
    return Ty->isDependentType();
  }
  if (!Ty.getQualifiers().empty())
    return false;
  return isValidSwiftContextType(Ty);
}

static void handleParameterABIAttr(Sema &S, Decl *D, const ParsedAttr &Attrs,
                                   ParameterABI Abi) {
  S.AddParameterABIAttr(Attrs.getRange(), D, Abi,
                        Attrs.getAttributeSpellingListIndex());
}

void Sema::AddParameterABIAttr(SourceRange range, Decl *D, ParameterABI abi,
                               unsigned spellingIndex) {

  QualType type = cast<ParmVarDecl>(D)->getType();

  if (auto existingAttr = D->getAttr<ParameterABIAttr>()) {
    if (existingAttr->getABI() != abi) {
      Diag(range.getBegin(), diag::err_attributes_are_not_compatible)
        << getParameterABISpelling(abi) << existingAttr;
      Diag(existingAttr->getLocation(), diag::note_conflicting_attribute);
      return;
    }
  }

  switch (abi) {
  case ParameterABI::Ordinary:
    llvm_unreachable("explicit attribute for ordinary parameter ABI?");

  case ParameterABI::SwiftContext:
    if (!isValidSwiftContextType(type)) {
      Diag(range.getBegin(), diag::err_swift_abi_parameter_wrong_type)
        << getParameterABISpelling(abi)
        << /*pointer to pointer */ 0 << type;
    }
    D->addAttr(::new (Context)
               SwiftContextAttr(range, Context, spellingIndex));
    return;

  case ParameterABI::SwiftErrorResult:
    if (!isValidSwiftErrorResultType(type)) {
      Diag(range.getBegin(), diag::err_swift_abi_parameter_wrong_type)
        << getParameterABISpelling(abi)
        << /*pointer to pointer */ 1 << type;
    }
    D->addAttr(::new (Context)
               SwiftErrorResultAttr(range, Context, spellingIndex));
    return;

  case ParameterABI::SwiftIndirectResult:
    if (!isValidSwiftIndirectResultType(type)) {
      Diag(range.getBegin(), diag::err_swift_abi_parameter_wrong_type)
        << getParameterABISpelling(abi)
        << /*pointer*/ 0 << type;
    }
    D->addAttr(::new (Context)
               SwiftIndirectResultAttr(range, Context, spellingIndex));
    return;
  }
  llvm_unreachable("bad parameter ABI attribute");
}

/// Checks a regparm attribute, returning true if it is ill-formed and
/// otherwise setting numParams to the appropriate value.
bool Sema::CheckRegparmAttr(const ParsedAttr &AL, unsigned &numParams) {
  if (AL.isInvalid())
    return true;

  if (!checkAttributeNumArgs(*this, AL, 1)) {
    AL.setInvalid();
    return true;
  }

  uint32_t NP;
  Expr *NumParamsExpr = AL.getArgAsExpr(0);
  if (!checkUInt32Argument(*this, AL, NumParamsExpr, NP)) {
    AL.setInvalid();
    return true;
  }

  if (Context.getTargetInfo().getRegParmMax() == 0) {
    Diag(AL.getLoc(), diag::err_attribute_regparm_wrong_platform)
      << NumParamsExpr->getSourceRange();
    AL.setInvalid();
    return true;
  }

  numParams = NP;
  if (numParams > Context.getTargetInfo().getRegParmMax()) {
    Diag(AL.getLoc(), diag::err_attribute_regparm_invalid_number)
      << Context.getTargetInfo().getRegParmMax() << NumParamsExpr->getSourceRange();
    AL.setInvalid();
    return true;
  }

  return false;
}

// Checks whether an argument of launch_bounds attribute is
// acceptable, performs implicit conversion to Rvalue, and returns
// non-nullptr Expr result on success. Otherwise, it returns nullptr
// and may output an error.
static Expr *makeLaunchBoundsArgExpr(Sema &S, Expr *E,
                                     const CUDALaunchBoundsAttr &AL,
                                     const unsigned Idx) {
  if (S.DiagnoseUnexpandedParameterPack(E))
    return nullptr;

  // Accept template arguments for now as they depend on something else.
  // We'll get to check them when they eventually get instantiated.
  if (E->isValueDependent())
    return E;

  llvm::APSInt I(64);
  if (!E->isIntegerConstantExpr(I, S.Context)) {
    S.Diag(E->getExprLoc(), diag::err_attribute_argument_n_type)
        << &AL << Idx << AANT_ArgumentIntegerConstant << E->getSourceRange();
    return nullptr;
  }
  // Make sure we can fit it in 32 bits.
  if (!I.isIntN(32)) {
    S.Diag(E->getExprLoc(), diag::err_ice_too_large) << I.toString(10, false)
                                                     << 32 << /* Unsigned */ 1;
    return nullptr;
  }
  if (I < 0)
    S.Diag(E->getExprLoc(), diag::warn_attribute_argument_n_negative)
        << &AL << Idx << E->getSourceRange();

  // We may need to perform implicit conversion of the argument.
  InitializedEntity Entity = InitializedEntity::InitializeParameter(
      S.Context, S.Context.getConstType(S.Context.IntTy), /*consume*/ false);
  ExprResult ValArg = S.PerformCopyInitialization(Entity, SourceLocation(), E);
  assert(!ValArg.isInvalid() &&
         "Unexpected PerformCopyInitialization() failure.");

  return ValArg.getAs<Expr>();
}

void Sema::AddLaunchBoundsAttr(SourceRange AttrRange, Decl *D, Expr *MaxThreads,
                               Expr *MinBlocks, unsigned SpellingListIndex) {
  CUDALaunchBoundsAttr TmpAttr(AttrRange, Context, MaxThreads, MinBlocks,
                               SpellingListIndex);
  MaxThreads = makeLaunchBoundsArgExpr(*this, MaxThreads, TmpAttr, 0);
  if (MaxThreads == nullptr)
    return;

  if (MinBlocks) {
    MinBlocks = makeLaunchBoundsArgExpr(*this, MinBlocks, TmpAttr, 1);
    if (MinBlocks == nullptr)
      return;
  }

  D->addAttr(::new (Context) CUDALaunchBoundsAttr(
      AttrRange, Context, MaxThreads, MinBlocks, SpellingListIndex));
}

static void handleLaunchBoundsAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (!checkAttributeAtLeastNumArgs(S, AL, 1) ||
      !checkAttributeAtMostNumArgs(S, AL, 2))
    return;

  S.AddLaunchBoundsAttr(AL.getRange(), D, AL.getArgAsExpr(0),
                        AL.getNumArgs() > 1 ? AL.getArgAsExpr(1) : nullptr,
                        AL.getAttributeSpellingListIndex());
}

static void handleArgumentWithTypeTagAttr(Sema &S, Decl *D,
                                          const ParsedAttr &AL) {
  if (!AL.isArgIdent(0)) {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_n_type)
        << AL << /* arg num = */ 1 << AANT_ArgumentIdentifier;
    return;
  }

  ParamIdx ArgumentIdx;
  if (!checkFunctionOrMethodParameterIndex(S, D, AL, 2, AL.getArgAsExpr(1),
                                           ArgumentIdx))
    return;

  ParamIdx TypeTagIdx;
  if (!checkFunctionOrMethodParameterIndex(S, D, AL, 3, AL.getArgAsExpr(2),
                                           TypeTagIdx))
    return;

  bool IsPointer = AL.getName()->getName() == "pointer_with_type_tag";
  if (IsPointer) {
    // Ensure that buffer has a pointer type.
    unsigned ArgumentIdxAST = ArgumentIdx.getASTIndex();
    if (ArgumentIdxAST >= getFunctionOrMethodNumParams(D) ||
        !getFunctionOrMethodParamType(D, ArgumentIdxAST)->isPointerType())
      S.Diag(AL.getLoc(), diag::err_attribute_pointers_only) << AL << 0;
  }

  D->addAttr(::new (S.Context) ArgumentWithTypeTagAttr(
      AL.getRange(), S.Context, AL.getArgAsIdent(0)->Ident, ArgumentIdx,
      TypeTagIdx, IsPointer, AL.getAttributeSpellingListIndex()));
}

static void handleTypeTagForDatatypeAttr(Sema &S, Decl *D,
                                         const ParsedAttr &AL) {
  if (!AL.isArgIdent(0)) {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_n_type)
        << AL << 1 << AANT_ArgumentIdentifier;
    return;
  }

  if (!checkAttributeNumArgs(S, AL, 1))
    return;

  if (!isa<VarDecl>(D)) {
    S.Diag(AL.getLoc(), diag::err_attribute_wrong_decl_type)
        << AL << ExpectedVariable;
    return;
  }

  IdentifierInfo *PointerKind = AL.getArgAsIdent(0)->Ident;
  TypeSourceInfo *MatchingCTypeLoc = nullptr;
  S.GetTypeFromParser(AL.getMatchingCType(), &MatchingCTypeLoc);
  assert(MatchingCTypeLoc && "no type source info for attribute argument");

  D->addAttr(::new (S.Context)
             TypeTagForDatatypeAttr(AL.getRange(), S.Context, PointerKind,
                                    MatchingCTypeLoc,
                                    AL.getLayoutCompatible(),
                                    AL.getMustBeNull(),
                                    AL.getAttributeSpellingListIndex()));
}

static void handleXRayLogArgsAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  ParamIdx ArgCount;

  if (!checkFunctionOrMethodParameterIndex(S, D, AL, 1, AL.getArgAsExpr(0),
                                           ArgCount,
                                           true /* CanIndexImplicitThis */))
    return;

  // ArgCount isn't a parameter index [0;n), it's a count [1;n]
  D->addAttr(::new (S.Context) XRayLogArgsAttr(
      AL.getRange(), S.Context, ArgCount.getSourceIndex(),
      AL.getAttributeSpellingListIndex()));
}

//===----------------------------------------------------------------------===//
// Checker-specific attribute handlers.
//===----------------------------------------------------------------------===//
static bool isValidSubjectOfNSReturnsRetainedAttribute(QualType QT) {
  return QT->isDependentType() || QT->isObjCRetainableType();
}

static bool isValidSubjectOfNSAttribute(QualType QT) {
  return QT->isDependentType() || QT->isObjCObjectPointerType() ||
         QT->isObjCNSObjectType();
}

static bool isValidSubjectOfCFAttribute(QualType QT) {
  return QT->isDependentType() || QT->isPointerType() ||
         isValidSubjectOfNSAttribute(QT);
}

static bool isValidSubjectOfOSAttribute(QualType QT) {
  if (QT->isDependentType())
    return true;
  QualType PT = QT->getPointeeType();
  return !PT.isNull() && PT->getAsCXXRecordDecl() != nullptr;
}

void Sema::AddXConsumedAttr(Decl *D, SourceRange SR, unsigned SpellingIndex,
                            RetainOwnershipKind K,
                            bool IsTemplateInstantiation) {
  ValueDecl *VD = cast<ValueDecl>(D);
  switch (K) {
  case RetainOwnershipKind::OS:
    handleSimpleAttributeOrDiagnose<OSConsumedAttr>(
        *this, VD, SR, SpellingIndex, isValidSubjectOfOSAttribute(VD->getType()),
        diag::warn_ns_attribute_wrong_parameter_type,
        /*ExtraArgs=*/SR, "os_consumed", /*pointers*/ 1);
    return;
  case RetainOwnershipKind::NS:
    handleSimpleAttributeOrDiagnose<NSConsumedAttr>(
        *this, VD, SR, SpellingIndex, isValidSubjectOfNSAttribute(VD->getType()),

        // These attributes are normally just advisory, but in ARC, ns_consumed
        // is significant.  Allow non-dependent code to contain inappropriate
        // attributes even in ARC, but require template instantiations to be
        // set up correctly.
        ((IsTemplateInstantiation && getLangOpts().ObjCAutoRefCount)
             ? diag::err_ns_attribute_wrong_parameter_type
             : diag::warn_ns_attribute_wrong_parameter_type),
        /*ExtraArgs=*/SR, "ns_consumed", /*objc pointers*/ 0);
    return;
  case RetainOwnershipKind::CF:
    handleSimpleAttributeOrDiagnose<CFConsumedAttr>(
        *this, VD, SR, SpellingIndex,
        isValidSubjectOfCFAttribute(VD->getType()),
        diag::warn_ns_attribute_wrong_parameter_type,
        /*ExtraArgs=*/SR, "cf_consumed", /*pointers*/1);
    return;
  }
}

static Sema::RetainOwnershipKind
parsedAttrToRetainOwnershipKind(const ParsedAttr &AL) {
  switch (AL.getKind()) {
  case ParsedAttr::AT_CFConsumed:
  case ParsedAttr::AT_CFReturnsRetained:
  case ParsedAttr::AT_CFReturnsNotRetained:
    return Sema::RetainOwnershipKind::CF;
  case ParsedAttr::AT_OSConsumesThis:
  case ParsedAttr::AT_OSConsumed:
  case ParsedAttr::AT_OSReturnsRetained:
  case ParsedAttr::AT_OSReturnsNotRetained:
  case ParsedAttr::AT_OSReturnsRetainedOnZero:
  case ParsedAttr::AT_OSReturnsRetainedOnNonZero:
    return Sema::RetainOwnershipKind::OS;
  case ParsedAttr::AT_NSConsumesSelf:
  case ParsedAttr::AT_NSConsumed:
  case ParsedAttr::AT_NSReturnsRetained:
  case ParsedAttr::AT_NSReturnsNotRetained:
  case ParsedAttr::AT_NSReturnsAutoreleased:
    return Sema::RetainOwnershipKind::NS;
  default:
    llvm_unreachable("Wrong argument supplied");
  }
}

bool Sema::checkNSReturnsRetainedReturnType(SourceLocation Loc, QualType QT) {
  if (isValidSubjectOfNSReturnsRetainedAttribute(QT))
    return false;

  Diag(Loc, diag::warn_ns_attribute_wrong_return_type)
      << "'ns_returns_retained'" << 0 << 0;
  return true;
}

/// \return whether the parameter is a pointer to OSObject pointer.
static bool isValidOSObjectOutParameter(const Decl *D) {
  const auto *PVD = dyn_cast<ParmVarDecl>(D);
  if (!PVD)
    return false;
  QualType QT = PVD->getType();
  QualType PT = QT->getPointeeType();
  return !PT.isNull() && isValidSubjectOfOSAttribute(PT);
}

static void handleXReturnsXRetainedAttr(Sema &S, Decl *D,
                                        const ParsedAttr &AL) {
  QualType ReturnType;
  Sema::RetainOwnershipKind K = parsedAttrToRetainOwnershipKind(AL);

  if (const auto *MD = dyn_cast<ObjCMethodDecl>(D)) {
    ReturnType = MD->getReturnType();
  } else if (S.getLangOpts().ObjCAutoRefCount && hasDeclarator(D) &&
             (AL.getKind() == ParsedAttr::AT_NSReturnsRetained)) {
    return; // ignore: was handled as a type attribute
  } else if (const auto *PD = dyn_cast<ObjCPropertyDecl>(D)) {
    ReturnType = PD->getType();
  } else if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
    ReturnType = FD->getReturnType();
  } else if (const auto *Param = dyn_cast<ParmVarDecl>(D)) {
    // Attributes on parameters are used for out-parameters,
    // passed as pointers-to-pointers.
    unsigned DiagID = K == Sema::RetainOwnershipKind::CF
            ? /*pointer-to-CF-pointer*/2
            : /*pointer-to-OSObject-pointer*/3;
    ReturnType = Param->getType()->getPointeeType();
    if (ReturnType.isNull()) {
      S.Diag(D->getBeginLoc(), diag::warn_ns_attribute_wrong_parameter_type)
          << AL << DiagID << AL.getRange();
      return;
    }
  } else if (AL.isUsedAsTypeAttr()) {
    return;
  } else {
    AttributeDeclKind ExpectedDeclKind;
    switch (AL.getKind()) {
    default: llvm_unreachable("invalid ownership attribute");
    case ParsedAttr::AT_NSReturnsRetained:
    case ParsedAttr::AT_NSReturnsAutoreleased:
    case ParsedAttr::AT_NSReturnsNotRetained:
      ExpectedDeclKind = ExpectedFunctionOrMethod;
      break;

    case ParsedAttr::AT_OSReturnsRetained:
    case ParsedAttr::AT_OSReturnsNotRetained:
    case ParsedAttr::AT_CFReturnsRetained:
    case ParsedAttr::AT_CFReturnsNotRetained:
      ExpectedDeclKind = ExpectedFunctionMethodOrParameter;
      break;
    }
    S.Diag(D->getBeginLoc(), diag::warn_attribute_wrong_decl_type)
        << AL.getRange() << AL << ExpectedDeclKind;
    return;
  }

  bool TypeOK;
  bool Cf;
  unsigned ParmDiagID = 2; // Pointer-to-CF-pointer
  switch (AL.getKind()) {
  default: llvm_unreachable("invalid ownership attribute");
  case ParsedAttr::AT_NSReturnsRetained:
    TypeOK = isValidSubjectOfNSReturnsRetainedAttribute(ReturnType);
    Cf = false;
    break;

  case ParsedAttr::AT_NSReturnsAutoreleased:
  case ParsedAttr::AT_NSReturnsNotRetained:
    TypeOK = isValidSubjectOfNSAttribute(ReturnType);
    Cf = false;
    break;

  case ParsedAttr::AT_CFReturnsRetained:
  case ParsedAttr::AT_CFReturnsNotRetained:
    TypeOK = isValidSubjectOfCFAttribute(ReturnType);
    Cf = true;
    break;

  case ParsedAttr::AT_OSReturnsRetained:
  case ParsedAttr::AT_OSReturnsNotRetained:
    TypeOK = isValidSubjectOfOSAttribute(ReturnType);
    Cf = true;
    ParmDiagID = 3; // Pointer-to-OSObject-pointer
    break;
  }

  if (!TypeOK) {
    if (AL.isUsedAsTypeAttr())
      return;

    if (isa<ParmVarDecl>(D)) {
      S.Diag(D->getBeginLoc(), diag::warn_ns_attribute_wrong_parameter_type)
          << AL << ParmDiagID << AL.getRange();
    } else {
      // Needs to be kept in sync with warn_ns_attribute_wrong_return_type.
      enum : unsigned {
        Function,
        Method,
        Property
      } SubjectKind = Function;
      if (isa<ObjCMethodDecl>(D))
        SubjectKind = Method;
      else if (isa<ObjCPropertyDecl>(D))
        SubjectKind = Property;
      S.Diag(D->getBeginLoc(), diag::warn_ns_attribute_wrong_return_type)
          << AL << SubjectKind << Cf << AL.getRange();
    }
    return;
  }

  switch (AL.getKind()) {
    default:
      llvm_unreachable("invalid ownership attribute");
    case ParsedAttr::AT_NSReturnsAutoreleased:
      handleSimpleAttribute<NSReturnsAutoreleasedAttr>(S, D, AL);
      return;
    case ParsedAttr::AT_CFReturnsNotRetained:
      handleSimpleAttribute<CFReturnsNotRetainedAttr>(S, D, AL);
      return;
    case ParsedAttr::AT_NSReturnsNotRetained:
      handleSimpleAttribute<NSReturnsNotRetainedAttr>(S, D, AL);
      return;
    case ParsedAttr::AT_CFReturnsRetained:
      handleSimpleAttribute<CFReturnsRetainedAttr>(S, D, AL);
      return;
    case ParsedAttr::AT_NSReturnsRetained:
      handleSimpleAttribute<NSReturnsRetainedAttr>(S, D, AL);
      return;
    case ParsedAttr::AT_OSReturnsRetained:
      handleSimpleAttribute<OSReturnsRetainedAttr>(S, D, AL);
      return;
    case ParsedAttr::AT_OSReturnsNotRetained:
      handleSimpleAttribute<OSReturnsNotRetainedAttr>(S, D, AL);
      return;
  };
}

static void handleObjCReturnsInnerPointerAttr(Sema &S, Decl *D,
                                              const ParsedAttr &Attrs) {
  const int EP_ObjCMethod = 1;
  const int EP_ObjCProperty = 2;

  SourceLocation loc = Attrs.getLoc();
  QualType resultType;
  if (isa<ObjCMethodDecl>(D))
    resultType = cast<ObjCMethodDecl>(D)->getReturnType();
  else
    resultType = cast<ObjCPropertyDecl>(D)->getType();

  if (!resultType->isReferenceType() &&
      (!resultType->isPointerType() || resultType->isObjCRetainableType())) {
    S.Diag(D->getBeginLoc(), diag::warn_ns_attribute_wrong_return_type)
        << SourceRange(loc) << Attrs
        << (isa<ObjCMethodDecl>(D) ? EP_ObjCMethod : EP_ObjCProperty)
        << /*non-retainable pointer*/ 2;

    // Drop the attribute.
    return;
  }

  D->addAttr(::new (S.Context) ObjCReturnsInnerPointerAttr(
      Attrs.getRange(), S.Context, Attrs.getAttributeSpellingListIndex()));
}

static void handleObjCRequiresSuperAttr(Sema &S, Decl *D,
                                        const ParsedAttr &Attrs) {
  const auto *Method = cast<ObjCMethodDecl>(D);

  const DeclContext *DC = Method->getDeclContext();
  if (const auto *PDecl = dyn_cast_or_null<ObjCProtocolDecl>(DC)) {
    S.Diag(D->getBeginLoc(), diag::warn_objc_requires_super_protocol) << Attrs
                                                                      << 0;
    S.Diag(PDecl->getLocation(), diag::note_protocol_decl);
    return;
  }
  if (Method->getMethodFamily() == OMF_dealloc) {
    S.Diag(D->getBeginLoc(), diag::warn_objc_requires_super_protocol) << Attrs
                                                                      << 1;
    return;
  }

  D->addAttr(::new (S.Context) ObjCRequiresSuperAttr(
      Attrs.getRange(), S.Context, Attrs.getAttributeSpellingListIndex()));
}

static void handleObjCBridgeAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  IdentifierLoc *Parm = AL.isArgIdent(0) ? AL.getArgAsIdent(0) : nullptr;

  if (!Parm) {
    S.Diag(D->getBeginLoc(), diag::err_objc_attr_not_id) << AL << 0;
    return;
  }

  // Typedefs only allow objc_bridge(id) and have some additional checking.
  if (const auto *TD = dyn_cast<TypedefNameDecl>(D)) {
    if (!Parm->Ident->isStr("id")) {
      S.Diag(AL.getLoc(), diag::err_objc_attr_typedef_not_id) << AL;
      return;
    }

    // Only allow 'cv void *'.
    QualType T = TD->getUnderlyingType();
    if (!T->isVoidPointerType()) {
      S.Diag(AL.getLoc(), diag::err_objc_attr_typedef_not_void_pointer);
      return;
    }
  }

  D->addAttr(::new (S.Context)
             ObjCBridgeAttr(AL.getRange(), S.Context, Parm->Ident,
                           AL.getAttributeSpellingListIndex()));
}

static void handleObjCBridgeMutableAttr(Sema &S, Decl *D,
                                        const ParsedAttr &AL) {
  IdentifierLoc *Parm = AL.isArgIdent(0) ? AL.getArgAsIdent(0) : nullptr;

  if (!Parm) {
    S.Diag(D->getBeginLoc(), diag::err_objc_attr_not_id) << AL << 0;
    return;
  }

  D->addAttr(::new (S.Context)
             ObjCBridgeMutableAttr(AL.getRange(), S.Context, Parm->Ident,
                            AL.getAttributeSpellingListIndex()));
}

static void handleObjCBridgeRelatedAttr(Sema &S, Decl *D,
                                        const ParsedAttr &AL) {
  IdentifierInfo *RelatedClass =
      AL.isArgIdent(0) ? AL.getArgAsIdent(0)->Ident : nullptr;
  if (!RelatedClass) {
    S.Diag(D->getBeginLoc(), diag::err_objc_attr_not_id) << AL << 0;
    return;
  }
  IdentifierInfo *ClassMethod =
    AL.getArgAsIdent(1) ? AL.getArgAsIdent(1)->Ident : nullptr;
  IdentifierInfo *InstanceMethod =
    AL.getArgAsIdent(2) ? AL.getArgAsIdent(2)->Ident : nullptr;
  D->addAttr(::new (S.Context)
             ObjCBridgeRelatedAttr(AL.getRange(), S.Context, RelatedClass,
                                   ClassMethod, InstanceMethod,
                                   AL.getAttributeSpellingListIndex()));
}

static void handleObjCDesignatedInitializer(Sema &S, Decl *D,
                                            const ParsedAttr &AL) {
  DeclContext *Ctx = D->getDeclContext();

  // This attribute can only be applied to methods in interfaces or class
  // extensions.
  if (!isa<ObjCInterfaceDecl>(Ctx) &&
      !(isa<ObjCCategoryDecl>(Ctx) &&
        cast<ObjCCategoryDecl>(Ctx)->IsClassExtension())) {
    S.Diag(D->getLocation(), diag::err_designated_init_attr_non_init);
    return;
  }

  ObjCInterfaceDecl *IFace;
  if (auto *CatDecl = dyn_cast<ObjCCategoryDecl>(Ctx))
    IFace = CatDecl->getClassInterface();
  else
    IFace = cast<ObjCInterfaceDecl>(Ctx);

  if (!IFace)
    return;

  IFace->setHasDesignatedInitializers();
  D->addAttr(::new (S.Context)
                  ObjCDesignatedInitializerAttr(AL.getRange(), S.Context,
                                         AL.getAttributeSpellingListIndex()));
}

static void handleObjCRuntimeName(Sema &S, Decl *D, const ParsedAttr &AL) {
  StringRef MetaDataName;
  if (!S.checkStringLiteralArgumentAttr(AL, 0, MetaDataName))
    return;
  D->addAttr(::new (S.Context)
             ObjCRuntimeNameAttr(AL.getRange(), S.Context,
                                 MetaDataName,
                                 AL.getAttributeSpellingListIndex()));
}

// When a user wants to use objc_boxable with a union or struct
// but they don't have access to the declaration (legacy/third-party code)
// then they can 'enable' this feature with a typedef:
// typedef struct __attribute((objc_boxable)) legacy_struct legacy_struct;
static void handleObjCBoxable(Sema &S, Decl *D, const ParsedAttr &AL) {
  bool notify = false;

  auto *RD = dyn_cast<RecordDecl>(D);
  if (RD && RD->getDefinition()) {
    RD = RD->getDefinition();
    notify = true;
  }

  if (RD) {
    ObjCBoxableAttr *BoxableAttr = ::new (S.Context)
                          ObjCBoxableAttr(AL.getRange(), S.Context,
                                          AL.getAttributeSpellingListIndex());
    RD->addAttr(BoxableAttr);
    if (notify) {
      // we need to notify ASTReader/ASTWriter about
      // modification of existing declaration
      if (ASTMutationListener *L = S.getASTMutationListener())
        L->AddedAttributeToRecord(BoxableAttr, RD);
    }
  }
}

static void handleObjCOwnershipAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (hasDeclarator(D)) return;

  S.Diag(D->getBeginLoc(), diag::err_attribute_wrong_decl_type)
      << AL.getRange() << AL << ExpectedVariable;
}

static void handleObjCPreciseLifetimeAttr(Sema &S, Decl *D,
                                          const ParsedAttr &AL) {
  const auto *VD = cast<ValueDecl>(D);
  QualType QT = VD->getType();

  if (!QT->isDependentType() &&
      !QT->isObjCLifetimeType()) {
    S.Diag(AL.getLoc(), diag::err_objc_precise_lifetime_bad_type)
      << QT;
    return;
  }

  Qualifiers::ObjCLifetime Lifetime = QT.getObjCLifetime();

  // If we have no lifetime yet, check the lifetime we're presumably
  // going to infer.
  if (Lifetime == Qualifiers::OCL_None && !QT->isDependentType())
    Lifetime = QT->getObjCARCImplicitLifetime();

  switch (Lifetime) {
  case Qualifiers::OCL_None:
    assert(QT->isDependentType() &&
           "didn't infer lifetime for non-dependent type?");
    break;

  case Qualifiers::OCL_Weak:   // meaningful
  case Qualifiers::OCL_Strong: // meaningful
    break;

  case Qualifiers::OCL_ExplicitNone:
  case Qualifiers::OCL_Autoreleasing:
    S.Diag(AL.getLoc(), diag::warn_objc_precise_lifetime_meaningless)
        << (Lifetime == Qualifiers::OCL_Autoreleasing);
    break;
  }

  D->addAttr(::new (S.Context)
             ObjCPreciseLifetimeAttr(AL.getRange(), S.Context,
                                     AL.getAttributeSpellingListIndex()));
}

//===----------------------------------------------------------------------===//
// Microsoft specific attribute handlers.
//===----------------------------------------------------------------------===//

UuidAttr *Sema::mergeUuidAttr(Decl *D, SourceRange Range,
                              unsigned AttrSpellingListIndex, StringRef Uuid) {
  if (const auto *UA = D->getAttr<UuidAttr>()) {
    if (UA->getGuid().equals_lower(Uuid))
      return nullptr;
    Diag(UA->getLocation(), diag::err_mismatched_uuid);
    Diag(Range.getBegin(), diag::note_previous_uuid);
    D->dropAttr<UuidAttr>();
  }

  return ::new (Context) UuidAttr(Range, Context, Uuid, AttrSpellingListIndex);
}

static void handleUuidAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (!S.LangOpts.CPlusPlus) {
    S.Diag(AL.getLoc(), diag::err_attribute_not_supported_in_lang)
        << AL << AttributeLangSupport::C;
    return;
  }

  StringRef StrRef;
  SourceLocation LiteralLoc;
  if (!S.checkStringLiteralArgumentAttr(AL, 0, StrRef, &LiteralLoc))
    return;

  // GUID format is "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX" or
  // "{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}", normalize to the former.
  if (StrRef.size() == 38 && StrRef.front() == '{' && StrRef.back() == '}')
    StrRef = StrRef.drop_front().drop_back();

  // Validate GUID length.
  if (StrRef.size() != 36) {
    S.Diag(LiteralLoc, diag::err_attribute_uuid_malformed_guid);
    return;
  }

  for (unsigned i = 0; i < 36; ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (StrRef[i] != '-') {
        S.Diag(LiteralLoc, diag::err_attribute_uuid_malformed_guid);
        return;
      }
    } else if (!isHexDigit(StrRef[i])) {
      S.Diag(LiteralLoc, diag::err_attribute_uuid_malformed_guid);
      return;
    }
  }

  // FIXME: It'd be nice to also emit a fixit removing uuid(...) (and, if it's
  // the only thing in the [] list, the [] too), and add an insertion of
  // __declspec(uuid(...)).  But sadly, neither the SourceLocs of the commas
  // separating attributes nor of the [ and the ] are in the AST.
  // Cf "SourceLocations of attribute list delimiters - [[ ... , ... ]] etc"
  // on cfe-dev.
  if (AL.isMicrosoftAttribute()) // Check for [uuid(...)] spelling.
    S.Diag(AL.getLoc(), diag::warn_atl_uuid_deprecated);

  UuidAttr *UA = S.mergeUuidAttr(D, AL.getRange(),
                                 AL.getAttributeSpellingListIndex(), StrRef);
  if (UA)
    D->addAttr(UA);
}

static void handleMSInheritanceAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (!S.LangOpts.CPlusPlus) {
    S.Diag(AL.getLoc(), diag::err_attribute_not_supported_in_lang)
        << AL << AttributeLangSupport::C;
    return;
  }
  MSInheritanceAttr *IA = S.mergeMSInheritanceAttr(
      D, AL.getRange(), /*BestCase=*/true,
      AL.getAttributeSpellingListIndex(),
      (MSInheritanceAttr::Spelling)AL.getSemanticSpelling());
  if (IA) {
    D->addAttr(IA);
    S.Consumer.AssignInheritanceModel(cast<CXXRecordDecl>(D));
  }
}

static void handleDeclspecThreadAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  const auto *VD = cast<VarDecl>(D);
  if (!S.Context.getTargetInfo().isTLSSupported()) {
    S.Diag(AL.getLoc(), diag::err_thread_unsupported);
    return;
  }
  if (VD->getTSCSpec() != TSCS_unspecified) {
    S.Diag(AL.getLoc(), diag::err_declspec_thread_on_thread_variable);
    return;
  }
  if (VD->hasLocalStorage()) {
    S.Diag(AL.getLoc(), diag::err_thread_non_global) << "__declspec(thread)";
    return;
  }
  D->addAttr(::new (S.Context) ThreadAttr(AL.getRange(), S.Context,
                                          AL.getAttributeSpellingListIndex()));
}

static void handleAbiTagAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  SmallVector<StringRef, 4> Tags;
  for (unsigned I = 0, E = AL.getNumArgs(); I != E; ++I) {
    StringRef Tag;
    if (!S.checkStringLiteralArgumentAttr(AL, I, Tag))
      return;
    Tags.push_back(Tag);
  }

  if (const auto *NS = dyn_cast<NamespaceDecl>(D)) {
    if (!NS->isInline()) {
      S.Diag(AL.getLoc(), diag::warn_attr_abi_tag_namespace) << 0;
      return;
    }
    if (NS->isAnonymousNamespace()) {
      S.Diag(AL.getLoc(), diag::warn_attr_abi_tag_namespace) << 1;
      return;
    }
    if (AL.getNumArgs() == 0)
      Tags.push_back(NS->getName());
  } else if (!checkAttributeAtLeastNumArgs(S, AL, 1))
    return;

  // Store tags sorted and without duplicates.
  llvm::sort(Tags);
  Tags.erase(std::unique(Tags.begin(), Tags.end()), Tags.end());

  D->addAttr(::new (S.Context)
             AbiTagAttr(AL.getRange(), S.Context, Tags.data(), Tags.size(),
                        AL.getAttributeSpellingListIndex()));
}

static void handleARMInterruptAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  // Check the attribute arguments.
  if (AL.getNumArgs() > 1) {
    S.Diag(AL.getLoc(), diag::err_attribute_too_many_arguments) << AL << 1;
    return;
  }

  StringRef Str;
  SourceLocation ArgLoc;

  if (AL.getNumArgs() == 0)
    Str = "";
  else if (!S.checkStringLiteralArgumentAttr(AL, 0, Str, &ArgLoc))
    return;

  ARMInterruptAttr::InterruptType Kind;
  if (!ARMInterruptAttr::ConvertStrToInterruptType(Str, Kind)) {
    S.Diag(AL.getLoc(), diag::warn_attribute_type_not_supported) << AL << Str
                                                                 << ArgLoc;
    return;
  }

  unsigned Index = AL.getAttributeSpellingListIndex();
  D->addAttr(::new (S.Context)
             ARMInterruptAttr(AL.getLoc(), S.Context, Kind, Index));
}

static void handleMSP430InterruptAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  // MSP430 'interrupt' attribute is applied to
  // a function with no parameters and void return type.
  if (!isFunctionOrMethod(D)) {
    S.Diag(D->getLocation(), diag::warn_attribute_wrong_decl_type)
        << "'interrupt'" << ExpectedFunctionOrMethod;
    return;
  }

  if (hasFunctionProto(D) && getFunctionOrMethodNumParams(D) != 0) {
    S.Diag(D->getLocation(), diag::warn_msp430_interrupt_attribute)
        << 0;
    return;
  }

  if (!getFunctionOrMethodResultType(D)->isVoidType()) {
    S.Diag(D->getLocation(), diag::warn_msp430_interrupt_attribute)
        << 1;
    return;
  }

  // The attribute takes one integer argument.
  if (!checkAttributeNumArgs(S, AL, 1))
    return;

  if (!AL.isArgExpr(0)) {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_type)
        << AL << AANT_ArgumentIntegerConstant;
    return;
  }

  Expr *NumParamsExpr = static_cast<Expr *>(AL.getArgAsExpr(0));
  llvm::APSInt NumParams(32);
  if (!NumParamsExpr->isIntegerConstantExpr(NumParams, S.Context)) {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_type)
        << AL << AANT_ArgumentIntegerConstant
        << NumParamsExpr->getSourceRange();
    return;
  }
  // The argument should be in range 0..63.
  unsigned Num = NumParams.getLimitedValue(255);
  if (Num > 63) {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_out_of_bounds)
        << AL << (int)NumParams.getSExtValue()
        << NumParamsExpr->getSourceRange();
    return;
  }

  D->addAttr(::new (S.Context)
              MSP430InterruptAttr(AL.getLoc(), S.Context, Num,
                                  AL.getAttributeSpellingListIndex()));
  D->addAttr(UsedAttr::CreateImplicit(S.Context));
}

static void handleMipsInterruptAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  // Only one optional argument permitted.
  if (AL.getNumArgs() > 1) {
    S.Diag(AL.getLoc(), diag::err_attribute_too_many_arguments) << AL << 1;
    return;
  }

  StringRef Str;
  SourceLocation ArgLoc;

  if (AL.getNumArgs() == 0)
    Str = "";
  else if (!S.checkStringLiteralArgumentAttr(AL, 0, Str, &ArgLoc))
    return;

  // Semantic checks for a function with the 'interrupt' attribute for MIPS:
  // a) Must be a function.
  // b) Must have no parameters.
  // c) Must have the 'void' return type.
  // d) Cannot have the 'mips16' attribute, as that instruction set
  //    lacks the 'eret' instruction.
  // e) The attribute itself must either have no argument or one of the
  //    valid interrupt types, see [MipsInterruptDocs].

  if (!isFunctionOrMethod(D)) {
    S.Diag(D->getLocation(), diag::warn_attribute_wrong_decl_type)
        << "'interrupt'" << ExpectedFunctionOrMethod;
    return;
  }

  if (hasFunctionProto(D) && getFunctionOrMethodNumParams(D) != 0) {
    S.Diag(D->getLocation(), diag::warn_mips_interrupt_attribute)
        << 0;
    return;
  }

  if (!getFunctionOrMethodResultType(D)->isVoidType()) {
    S.Diag(D->getLocation(), diag::warn_mips_interrupt_attribute)
        << 1;
    return;
  }

  if (checkAttrMutualExclusion<Mips16Attr>(S, D, AL))
    return;

  MipsInterruptAttr::InterruptType Kind;
  if (!MipsInterruptAttr::ConvertStrToInterruptType(Str, Kind)) {
    S.Diag(AL.getLoc(), diag::warn_attribute_type_not_supported)
        << AL << "'" + std::string(Str) + "'";
    return;
  }

  D->addAttr(::new (S.Context) MipsInterruptAttr(
      AL.getLoc(), S.Context, Kind, AL.getAttributeSpellingListIndex()));
}

static void handleAnyX86InterruptAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  // Semantic checks for a function with the 'interrupt' attribute.
  // a) Must be a function.
  // b) Must have the 'void' return type.
  // c) Must take 1 or 2 arguments.
  // d) The 1st argument must be a pointer.
  // e) The 2nd argument (if any) must be an unsigned integer.
  if (!isFunctionOrMethod(D) || !hasFunctionProto(D) || isInstanceMethod(D) ||
      CXXMethodDecl::isStaticOverloadedOperator(
          cast<NamedDecl>(D)->getDeclName().getCXXOverloadedOperator())) {
    S.Diag(AL.getLoc(), diag::warn_attribute_wrong_decl_type)
        << AL << ExpectedFunctionWithProtoType;
    return;
  }
  // Interrupt handler must have void return type.
  if (!getFunctionOrMethodResultType(D)->isVoidType()) {
    S.Diag(getFunctionOrMethodResultSourceRange(D).getBegin(),
           diag::err_anyx86_interrupt_attribute)
        << (S.Context.getTargetInfo().getTriple().getArch() == llvm::Triple::x86
                ? 0
                : 1)
        << 0;
    return;
  }
  // Interrupt handler must have 1 or 2 parameters.
  unsigned NumParams = getFunctionOrMethodNumParams(D);
  if (NumParams < 1 || NumParams > 2) {
    S.Diag(D->getBeginLoc(), diag::err_anyx86_interrupt_attribute)
        << (S.Context.getTargetInfo().getTriple().getArch() == llvm::Triple::x86
                ? 0
                : 1)
        << 1;
    return;
  }
  // The first argument must be a pointer.
  if (!getFunctionOrMethodParamType(D, 0)->isPointerType()) {
    S.Diag(getFunctionOrMethodParamRange(D, 0).getBegin(),
           diag::err_anyx86_interrupt_attribute)
        << (S.Context.getTargetInfo().getTriple().getArch() == llvm::Triple::x86
                ? 0
                : 1)
        << 2;
    return;
  }
  // The second argument, if present, must be an unsigned integer.
  unsigned TypeSize =
      S.Context.getTargetInfo().getTriple().getArch() == llvm::Triple::x86_64
          ? 64
          : 32;
  if (NumParams == 2 &&
      (!getFunctionOrMethodParamType(D, 1)->isUnsignedIntegerType() ||
       S.Context.getTypeSize(getFunctionOrMethodParamType(D, 1)) != TypeSize)) {
    S.Diag(getFunctionOrMethodParamRange(D, 1).getBegin(),
           diag::err_anyx86_interrupt_attribute)
        << (S.Context.getTargetInfo().getTriple().getArch() == llvm::Triple::x86
                ? 0
                : 1)
        << 3 << S.Context.getIntTypeForBitwidth(TypeSize, /*Signed=*/false);
    return;
  }
  D->addAttr(::new (S.Context) AnyX86InterruptAttr(
      AL.getLoc(), S.Context, AL.getAttributeSpellingListIndex()));
  D->addAttr(UsedAttr::CreateImplicit(S.Context));
}

static void handleAVRInterruptAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (!isFunctionOrMethod(D)) {
    S.Diag(D->getLocation(), diag::warn_attribute_wrong_decl_type)
        << "'interrupt'" << ExpectedFunction;
    return;
  }

  if (!checkAttributeNumArgs(S, AL, 0))
    return;

  handleSimpleAttribute<AVRInterruptAttr>(S, D, AL);
}

static void handleAVRSignalAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (!isFunctionOrMethod(D)) {
    S.Diag(D->getLocation(), diag::warn_attribute_wrong_decl_type)
        << "'signal'" << ExpectedFunction;
    return;
  }

  if (!checkAttributeNumArgs(S, AL, 0))
    return;

  handleSimpleAttribute<AVRSignalAttr>(S, D, AL);
}

static void handleWebAssemblyImportModuleAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (!isFunctionOrMethod(D)) {
    S.Diag(D->getLocation(), diag::warn_attribute_wrong_decl_type)
        << "'import_module'" << ExpectedFunction;
    return;
  }

  auto *FD = cast<FunctionDecl>(D);
  if (FD->isThisDeclarationADefinition()) {
    S.Diag(D->getLocation(), diag::err_alias_is_definition) << FD << 0;
    return;
  }

  StringRef Str;
  SourceLocation ArgLoc;
  if (!S.checkStringLiteralArgumentAttr(AL, 0, Str, &ArgLoc))
    return;

  FD->addAttr(::new (S.Context) WebAssemblyImportModuleAttr(
      AL.getRange(), S.Context, Str,
      AL.getAttributeSpellingListIndex()));
}

static void handleWebAssemblyImportNameAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (!isFunctionOrMethod(D)) {
    S.Diag(D->getLocation(), diag::warn_attribute_wrong_decl_type)
        << "'import_name'" << ExpectedFunction;
    return;
  }

  auto *FD = cast<FunctionDecl>(D);
  if (FD->isThisDeclarationADefinition()) {
    S.Diag(D->getLocation(), diag::err_alias_is_definition) << FD << 0;
    return;
  }

  StringRef Str;
  SourceLocation ArgLoc;
  if (!S.checkStringLiteralArgumentAttr(AL, 0, Str, &ArgLoc))
    return;

  FD->addAttr(::new (S.Context) WebAssemblyImportNameAttr(
      AL.getRange(), S.Context, Str,
      AL.getAttributeSpellingListIndex()));
}

static void handleRISCVInterruptAttr(Sema &S, Decl *D,
                                     const ParsedAttr &AL) {
  // Warn about repeated attributes.
  if (const auto *A = D->getAttr<RISCVInterruptAttr>()) {
    S.Diag(AL.getRange().getBegin(),
      diag::warn_riscv_repeated_interrupt_attribute);
    S.Diag(A->getLocation(), diag::note_riscv_repeated_interrupt_attribute);
    return;
  }

  // Check the attribute argument. Argument is optional.
  if (!checkAttributeAtMostNumArgs(S, AL, 1))
    return;

  StringRef Str;
  SourceLocation ArgLoc;

  // 'machine'is the default interrupt mode.
  if (AL.getNumArgs() == 0)
    Str = "machine";
  else if (!S.checkStringLiteralArgumentAttr(AL, 0, Str, &ArgLoc))
    return;

  // Semantic checks for a function with the 'interrupt' attribute:
  // - Must be a function.
  // - Must have no parameters.
  // - Must have the 'void' return type.
  // - The attribute itself must either have no argument or one of the
  //   valid interrupt types, see [RISCVInterruptDocs].

  if (D->getFunctionType() == nullptr) {
    S.Diag(D->getLocation(), diag::warn_attribute_wrong_decl_type)
      << "'interrupt'" << ExpectedFunction;
    return;
  }

  if (hasFunctionProto(D) && getFunctionOrMethodNumParams(D) != 0) {
    S.Diag(D->getLocation(), diag::warn_riscv_interrupt_attribute) << 0;
    return;
  }

  if (!getFunctionOrMethodResultType(D)->isVoidType()) {
    S.Diag(D->getLocation(), diag::warn_riscv_interrupt_attribute) << 1;
    return;
  }

  RISCVInterruptAttr::InterruptType Kind;
  if (!RISCVInterruptAttr::ConvertStrToInterruptType(Str, Kind)) {
    S.Diag(AL.getLoc(), diag::warn_attribute_type_not_supported) << AL << Str
                                                                 << ArgLoc;
    return;
  }

  D->addAttr(::new (S.Context) RISCVInterruptAttr(
    AL.getLoc(), S.Context, Kind, AL.getAttributeSpellingListIndex()));
}

static void handleInterruptAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  // Dispatch the interrupt attribute based on the current target.
  switch (S.Context.getTargetInfo().getTriple().getArch()) {
  case llvm::Triple::msp430:
    handleMSP430InterruptAttr(S, D, AL);
    break;
  case llvm::Triple::mipsel:
  case llvm::Triple::mips:
    handleMipsInterruptAttr(S, D, AL);
    break;
  case llvm::Triple::x86:
  case llvm::Triple::x86_64:
    handleAnyX86InterruptAttr(S, D, AL);
    break;
  case llvm::Triple::avr:
    handleAVRInterruptAttr(S, D, AL);
    break;
  case llvm::Triple::riscv32:
  case llvm::Triple::riscv64:
    handleRISCVInterruptAttr(S, D, AL);
    break;
  default:
    handleARMInterruptAttr(S, D, AL);
    break;
  }
}

static void handleAMDGPUFlatWorkGroupSizeAttr(Sema &S, Decl *D,
                                              const ParsedAttr &AL) {
  uint32_t Min = 0;
  Expr *MinExpr = AL.getArgAsExpr(0);
  if (!checkUInt32Argument(S, AL, MinExpr, Min))
    return;

  uint32_t Max = 0;
  Expr *MaxExpr = AL.getArgAsExpr(1);
  if (!checkUInt32Argument(S, AL, MaxExpr, Max))
    return;

  if (Min == 0 && Max != 0) {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_invalid) << AL << 0;
    return;
  }
  if (Min > Max) {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_invalid) << AL << 1;
    return;
  }

  D->addAttr(::new (S.Context)
             AMDGPUFlatWorkGroupSizeAttr(AL.getLoc(), S.Context, Min, Max,
                                         AL.getAttributeSpellingListIndex()));
}

static void handleAMDGPUWavesPerEUAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  uint32_t Min = 0;
  Expr *MinExpr = AL.getArgAsExpr(0);
  if (!checkUInt32Argument(S, AL, MinExpr, Min))
    return;

  uint32_t Max = 0;
  if (AL.getNumArgs() == 2) {
    Expr *MaxExpr = AL.getArgAsExpr(1);
    if (!checkUInt32Argument(S, AL, MaxExpr, Max))
      return;
  }

  if (Min == 0 && Max != 0) {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_invalid) << AL << 0;
    return;
  }
  if (Max != 0 && Min > Max) {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_invalid) << AL << 1;
    return;
  }

  D->addAttr(::new (S.Context)
             AMDGPUWavesPerEUAttr(AL.getLoc(), S.Context, Min, Max,
                                  AL.getAttributeSpellingListIndex()));
}

static void handleAMDGPUNumSGPRAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  uint32_t NumSGPR = 0;
  Expr *NumSGPRExpr = AL.getArgAsExpr(0);
  if (!checkUInt32Argument(S, AL, NumSGPRExpr, NumSGPR))
    return;

  D->addAttr(::new (S.Context)
             AMDGPUNumSGPRAttr(AL.getLoc(), S.Context, NumSGPR,
                               AL.getAttributeSpellingListIndex()));
}

static void handleAMDGPUNumVGPRAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  uint32_t NumVGPR = 0;
  Expr *NumVGPRExpr = AL.getArgAsExpr(0);
  if (!checkUInt32Argument(S, AL, NumVGPRExpr, NumVGPR))
    return;

  D->addAttr(::new (S.Context)
             AMDGPUNumVGPRAttr(AL.getLoc(), S.Context, NumVGPR,
                               AL.getAttributeSpellingListIndex()));
}

static void handleX86ForceAlignArgPointerAttr(Sema &S, Decl *D,
                                              const ParsedAttr &AL) {
  // If we try to apply it to a function pointer, don't warn, but don't
  // do anything, either. It doesn't matter anyway, because there's nothing
  // special about calling a force_align_arg_pointer function.
  const auto *VD = dyn_cast<ValueDecl>(D);
  if (VD && VD->getType()->isFunctionPointerType())
    return;
  // Also don't warn on function pointer typedefs.
  const auto *TD = dyn_cast<TypedefNameDecl>(D);
  if (TD && (TD->getUnderlyingType()->isFunctionPointerType() ||
    TD->getUnderlyingType()->isFunctionType()))
    return;
  // Attribute can only be applied to function types.
  if (!isa<FunctionDecl>(D)) {
    S.Diag(AL.getLoc(), diag::warn_attribute_wrong_decl_type)
        << AL << ExpectedFunction;
    return;
  }

  D->addAttr(::new (S.Context)
              X86ForceAlignArgPointerAttr(AL.getRange(), S.Context,
                                        AL.getAttributeSpellingListIndex()));
}

static void handleLayoutVersion(Sema &S, Decl *D, const ParsedAttr &AL) {
  uint32_t Version;
  Expr *VersionExpr = static_cast<Expr *>(AL.getArgAsExpr(0));
  if (!checkUInt32Argument(S, AL, AL.getArgAsExpr(0), Version))
    return;

  // TODO: Investigate what happens with the next major version of MSVC.
  if (Version != LangOptions::MSVC2015 / 100) {
    S.Diag(AL.getLoc(), diag::err_attribute_argument_out_of_bounds)
        << AL << Version << VersionExpr->getSourceRange();
    return;
  }

  // The attribute expects a "major" version number like 19, but new versions of
  // MSVC have moved to updating the "minor", or less significant numbers, so we
  // have to multiply by 100 now.
  Version *= 100;

  D->addAttr(::new (S.Context)
                 LayoutVersionAttr(AL.getRange(), S.Context, Version,
                                   AL.getAttributeSpellingListIndex()));
}

DLLImportAttr *Sema::mergeDLLImportAttr(Decl *D, SourceRange Range,
                                        unsigned AttrSpellingListIndex) {
  if (D->hasAttr<DLLExportAttr>()) {
    Diag(Range.getBegin(), diag::warn_attribute_ignored) << "'dllimport'";
    return nullptr;
  }

  if (D->hasAttr<DLLImportAttr>())
    return nullptr;

  return ::new (Context) DLLImportAttr(Range, Context, AttrSpellingListIndex);
}

DLLExportAttr *Sema::mergeDLLExportAttr(Decl *D, SourceRange Range,
                                        unsigned AttrSpellingListIndex) {
  if (DLLImportAttr *Import = D->getAttr<DLLImportAttr>()) {
    Diag(Import->getLocation(), diag::warn_attribute_ignored) << Import;
    D->dropAttr<DLLImportAttr>();
  }

  if (D->hasAttr<DLLExportAttr>())
    return nullptr;

  return ::new (Context) DLLExportAttr(Range, Context, AttrSpellingListIndex);
}

static void handleDLLAttr(Sema &S, Decl *D, const ParsedAttr &A) {
  if (isa<ClassTemplatePartialSpecializationDecl>(D) &&
      S.Context.getTargetInfo().getCXXABI().isMicrosoft()) {
    S.Diag(A.getRange().getBegin(), diag::warn_attribute_ignored) << A;
    return;
  }

  if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
    if (FD->isInlined() && A.getKind() == ParsedAttr::AT_DLLImport &&
        !S.Context.getTargetInfo().getCXXABI().isMicrosoft()) {
      // MinGW doesn't allow dllimport on inline functions.
      S.Diag(A.getRange().getBegin(), diag::warn_attribute_ignored_on_inline)
          << A;
      return;
    }
  }

  if (const auto *MD = dyn_cast<CXXMethodDecl>(D)) {
    if (S.Context.getTargetInfo().getCXXABI().isMicrosoft() &&
        MD->getParent()->isLambda()) {
      S.Diag(A.getRange().getBegin(), diag::err_attribute_dll_lambda) << A;
      return;
    }
  }

  unsigned Index = A.getAttributeSpellingListIndex();
  Attr *NewAttr = A.getKind() == ParsedAttr::AT_DLLExport
                      ? (Attr *)S.mergeDLLExportAttr(D, A.getRange(), Index)
                      : (Attr *)S.mergeDLLImportAttr(D, A.getRange(), Index);
  if (NewAttr)
    D->addAttr(NewAttr);
}

MSInheritanceAttr *
Sema::mergeMSInheritanceAttr(Decl *D, SourceRange Range, bool BestCase,
                             unsigned AttrSpellingListIndex,
                             MSInheritanceAttr::Spelling SemanticSpelling) {
  if (MSInheritanceAttr *IA = D->getAttr<MSInheritanceAttr>()) {
    if (IA->getSemanticSpelling() == SemanticSpelling)
      return nullptr;
    Diag(IA->getLocation(), diag::err_mismatched_ms_inheritance)
        << 1 /*previous declaration*/;
    Diag(Range.getBegin(), diag::note_previous_ms_inheritance);
    D->dropAttr<MSInheritanceAttr>();
  }

  auto *RD = cast<CXXRecordDecl>(D);
  if (RD->hasDefinition()) {
    if (checkMSInheritanceAttrOnDefinition(RD, Range, BestCase,
                                           SemanticSpelling)) {
      return nullptr;
    }
  } else {
    if (isa<ClassTemplatePartialSpecializationDecl>(RD)) {
      Diag(Range.getBegin(), diag::warn_ignored_ms_inheritance)
          << 1 /*partial specialization*/;
      return nullptr;
    }
    if (RD->getDescribedClassTemplate()) {
      Diag(Range.getBegin(), diag::warn_ignored_ms_inheritance)
          << 0 /*primary template*/;
      return nullptr;
    }
  }

  return ::new (Context)
      MSInheritanceAttr(Range, Context, BestCase, AttrSpellingListIndex);
}

static void handleCapabilityAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  // The capability attributes take a single string parameter for the name of
  // the capability they represent. The lockable attribute does not take any
  // parameters. However, semantically, both attributes represent the same
  // concept, and so they use the same semantic attribute. Eventually, the
  // lockable attribute will be removed.
  //
  // For backward compatibility, any capability which has no specified string
  // literal will be considered a "mutex."
  StringRef N("mutex");
  SourceLocation LiteralLoc;
  if (AL.getKind() == ParsedAttr::AT_Capability &&
      !S.checkStringLiteralArgumentAttr(AL, 0, N, &LiteralLoc))
    return;

  // Currently, there are only two names allowed for a capability: role and
  // mutex (case insensitive). Diagnose other capability names.
  if (!N.equals_lower("mutex") && !N.equals_lower("role"))
    S.Diag(LiteralLoc, diag::warn_invalid_capability_name) << N;

  D->addAttr(::new (S.Context) CapabilityAttr(AL.getRange(), S.Context, N,
                                        AL.getAttributeSpellingListIndex()));
}

static void handleAssertCapabilityAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  SmallVector<Expr*, 1> Args;
  if (!checkLockFunAttrCommon(S, D, AL, Args))
    return;

  D->addAttr(::new (S.Context) AssertCapabilityAttr(AL.getRange(), S.Context,
                                                    Args.data(), Args.size(),
                                        AL.getAttributeSpellingListIndex()));
}

static void handleAcquireCapabilityAttr(Sema &S, Decl *D,
                                        const ParsedAttr &AL) {
  SmallVector<Expr*, 1> Args;
  if (!checkLockFunAttrCommon(S, D, AL, Args))
    return;

  D->addAttr(::new (S.Context) AcquireCapabilityAttr(AL.getRange(),
                                                     S.Context,
                                                     Args.data(), Args.size(),
                                        AL.getAttributeSpellingListIndex()));
}

static void handleTryAcquireCapabilityAttr(Sema &S, Decl *D,
                                           const ParsedAttr &AL) {
  SmallVector<Expr*, 2> Args;
  if (!checkTryLockFunAttrCommon(S, D, AL, Args))
    return;

  D->addAttr(::new (S.Context) TryAcquireCapabilityAttr(AL.getRange(),
                                                        S.Context,
                                                        AL.getArgAsExpr(0),
                                                        Args.data(),
                                                        Args.size(),
                                        AL.getAttributeSpellingListIndex()));
}

static void handleReleaseCapabilityAttr(Sema &S, Decl *D,
                                        const ParsedAttr &AL) {
  // Check that all arguments are lockable objects.
  SmallVector<Expr *, 1> Args;
  checkAttrArgsAreCapabilityObjs(S, D, AL, Args, 0, true);

  D->addAttr(::new (S.Context) ReleaseCapabilityAttr(
      AL.getRange(), S.Context, Args.data(), Args.size(),
      AL.getAttributeSpellingListIndex()));
}

static void handleRequiresCapabilityAttr(Sema &S, Decl *D,
                                         const ParsedAttr &AL) {
  if (!checkAttributeAtLeastNumArgs(S, AL, 1))
    return;

  // check that all arguments are lockable objects
  SmallVector<Expr*, 1> Args;
  checkAttrArgsAreCapabilityObjs(S, D, AL, Args);
  if (Args.empty())
    return;

  RequiresCapabilityAttr *RCA = ::new (S.Context)
    RequiresCapabilityAttr(AL.getRange(), S.Context, Args.data(),
                           Args.size(), AL.getAttributeSpellingListIndex());

  D->addAttr(RCA);
}

static void handleDeprecatedAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (const auto *NSD = dyn_cast<NamespaceDecl>(D)) {
    if (NSD->isAnonymousNamespace()) {
      S.Diag(AL.getLoc(), diag::warn_deprecated_anonymous_namespace);
      // Do not want to attach the attribute to the namespace because that will
      // cause confusing diagnostic reports for uses of declarations within the
      // namespace.
      return;
    }
  }

  // Handle the cases where the attribute has a text message.
  StringRef Str, Replacement;
  if (AL.isArgExpr(0) && AL.getArgAsExpr(0) &&
      !S.checkStringLiteralArgumentAttr(AL, 0, Str))
    return;

  // Only support a single optional message for Declspec and CXX11.
  if (AL.isDeclspecAttribute() || AL.isCXX11Attribute())
    checkAttributeAtMostNumArgs(S, AL, 1);
  else if (AL.isArgExpr(1) && AL.getArgAsExpr(1) &&
           !S.checkStringLiteralArgumentAttr(AL, 1, Replacement))
    return;

  if (!S.getLangOpts().CPlusPlus14 && AL.isCXX11Attribute() && !AL.isGNUScope())
    S.Diag(AL.getLoc(), diag::ext_cxx14_attr) << AL;

  D->addAttr(::new (S.Context)
                 DeprecatedAttr(AL.getRange(), S.Context, Str, Replacement,
                                AL.getAttributeSpellingListIndex()));
}

static bool isGlobalVar(const Decl *D) {
  if (const auto *S = dyn_cast<VarDecl>(D))
    return S->hasGlobalStorage();
  return false;
}

static void handleNoSanitizeAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (!checkAttributeAtLeastNumArgs(S, AL, 1))
    return;

  std::vector<StringRef> Sanitizers;

  for (unsigned I = 0, E = AL.getNumArgs(); I != E; ++I) {
    StringRef SanitizerName;
    SourceLocation LiteralLoc;

    if (!S.checkStringLiteralArgumentAttr(AL, I, SanitizerName, &LiteralLoc))
      return;

    if (parseSanitizerValue(SanitizerName, /*AllowGroups=*/true) == 0)
      S.Diag(LiteralLoc, diag::warn_unknown_sanitizer_ignored) << SanitizerName;
    else if (isGlobalVar(D) && SanitizerName != "address")
      S.Diag(D->getLocation(), diag::err_attribute_wrong_decl_type)
          << AL << ExpectedFunctionOrMethod;
    Sanitizers.push_back(SanitizerName);
  }

  D->addAttr(::new (S.Context) NoSanitizeAttr(
      AL.getRange(), S.Context, Sanitizers.data(), Sanitizers.size(),
      AL.getAttributeSpellingListIndex()));
}

static void handleNoSanitizeSpecificAttr(Sema &S, Decl *D,
                                         const ParsedAttr &AL) {
  StringRef AttrName = AL.getName()->getName();
  normalizeName(AttrName);
  StringRef SanitizerName = llvm::StringSwitch<StringRef>(AttrName)
                                .Case("no_address_safety_analysis", "address")
                                .Case("no_sanitize_address", "address")
                                .Case("no_sanitize_thread", "thread")
                                .Case("no_sanitize_memory", "memory");
  if (isGlobalVar(D) && SanitizerName != "address")
    S.Diag(D->getLocation(), diag::err_attribute_wrong_decl_type)
        << AL << ExpectedFunction;
  D->addAttr(::new (S.Context)
                 NoSanitizeAttr(AL.getRange(), S.Context, &SanitizerName, 1,
                                AL.getAttributeSpellingListIndex()));
}

static void handleInternalLinkageAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (InternalLinkageAttr *Internal = S.mergeInternalLinkageAttr(D, AL))
    D->addAttr(Internal);
}

static void handleOpenCLNoSVMAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (S.LangOpts.OpenCLVersion != 200)
    S.Diag(AL.getLoc(), diag::err_attribute_requires_opencl_version)
        << AL << "2.0" << 0;
  else
    S.Diag(AL.getLoc(), diag::warn_opencl_attr_deprecated_ignored) << AL
                                                                   << "2.0";
}

/// Handles semantic checking for features that are common to all attributes,
/// such as checking whether a parameter was properly specified, or the correct
/// number of arguments were passed, etc.
static bool handleCommonAttributeFeatures(Sema &S, Decl *D,
                                          const ParsedAttr &AL) {
  // Several attributes carry different semantics than the parsing requires, so
  // those are opted out of the common argument checks.
  //
  // We also bail on unknown and ignored attributes because those are handled
  // as part of the target-specific handling logic.
  if (AL.getKind() == ParsedAttr::UnknownAttribute)
    return false;
  // Check whether the attribute requires specific language extensions to be
  // enabled.
  if (!AL.diagnoseLangOpts(S))
    return true;
  // Check whether the attribute appertains to the given subject.
  if (!AL.diagnoseAppertainsTo(S, D))
    return true;
  if (AL.hasCustomParsing())
    return false;

  if (AL.getMinArgs() == AL.getMaxArgs()) {
    // If there are no optional arguments, then checking for the argument count
    // is trivial.
    if (!checkAttributeNumArgs(S, AL, AL.getMinArgs()))
      return true;
  } else {
    // There are optional arguments, so checking is slightly more involved.
    if (AL.getMinArgs() &&
        !checkAttributeAtLeastNumArgs(S, AL, AL.getMinArgs()))
      return true;
    else if (!AL.hasVariadicArg() && AL.getMaxArgs() &&
             !checkAttributeAtMostNumArgs(S, AL, AL.getMaxArgs()))
      return true;
  }

  if (S.CheckAttrTarget(AL))
    return true;

  return false;
}

static void handleOpenCLAccessAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  if (D->isInvalidDecl())
    return;

  // Check if there is only one access qualifier.
  if (D->hasAttr<OpenCLAccessAttr>()) {
    if (D->getAttr<OpenCLAccessAttr>()->getSemanticSpelling() ==
        AL.getSemanticSpelling()) {
      S.Diag(AL.getLoc(), diag::warn_duplicate_declspec)
          << AL.getName()->getName() << AL.getRange();
    } else {
      S.Diag(AL.getLoc(), diag::err_opencl_multiple_access_qualifiers)
          << D->getSourceRange();
      D->setInvalidDecl(true);
      return;
    }
  }

  // OpenCL v2.0 s6.6 - read_write can be used for image types to specify that an
  // image object can be read and written.
  // OpenCL v2.0 s6.13.6 - A kernel cannot read from and write to the same pipe
  // object. Using the read_write (or __read_write) qualifier with the pipe
  // qualifier is a compilation error.
  if (const auto *PDecl = dyn_cast<ParmVarDecl>(D)) {
    const Type *DeclTy = PDecl->getType().getCanonicalType().getTypePtr();
    if (AL.getName()->getName().find("read_write") != StringRef::npos) {
      if (S.getLangOpts().OpenCLVersion < 200 || DeclTy->isPipeType()) {
        S.Diag(AL.getLoc(), diag::err_opencl_invalid_read_write)
            << AL << PDecl->getType() << DeclTy->isImageType();
        D->setInvalidDecl(true);
        return;
      }
    }
  }

  D->addAttr(::new (S.Context) OpenCLAccessAttr(
      AL.getRange(), S.Context, AL.getAttributeSpellingListIndex()));
}

static void handleDestroyAttr(Sema &S, Decl *D, const ParsedAttr &A) {
  if (!cast<VarDecl>(D)->hasGlobalStorage()) {
    S.Diag(D->getLocation(), diag::err_destroy_attr_on_non_static_var)
        << (A.getKind() == ParsedAttr::AT_AlwaysDestroy);
    return;
  }

  if (A.getKind() == ParsedAttr::AT_AlwaysDestroy)
    handleSimpleAttributeWithExclusions<AlwaysDestroyAttr, NoDestroyAttr>(S, D, A);
  else
    handleSimpleAttributeWithExclusions<NoDestroyAttr, AlwaysDestroyAttr>(S, D, A);
}

static void handleUninitializedAttr(Sema &S, Decl *D, const ParsedAttr &AL) {
  assert(cast<VarDecl>(D)->getStorageDuration() == SD_Automatic &&
         "uninitialized is only valid on automatic duration variables");
  unsigned Index = AL.getAttributeSpellingListIndex();
  D->addAttr(::new (S.Context)
                 UninitializedAttr(AL.getLoc(), S.Context, Index));
}

static bool tryMakeVariablePseudoStrong(Sema &S, VarDecl *VD,
                                        bool DiagnoseFailure) {
  QualType Ty = VD->getType();
  if (!Ty->isObjCRetainableType()) {
    if (DiagnoseFailure) {
      S.Diag(VD->getBeginLoc(), diag::warn_ignored_objc_externally_retained)
          << 0;
    }
    return false;
  }

  Qualifiers::ObjCLifetime LifetimeQual = Ty.getQualifiers().getObjCLifetime();

  // Sema::inferObjCARCLifetime must run after processing decl attributes
  // (because __block lowers to an attribute), so if the lifetime hasn't been
  // explicitly specified, infer it locally now.
  if (LifetimeQual == Qualifiers::OCL_None)
    LifetimeQual = Ty->getObjCARCImplicitLifetime();

  // The attributes only really makes sense for __strong variables; ignore any
  // attempts to annotate a parameter with any other lifetime qualifier.
  if (LifetimeQual != Qualifiers::OCL_Strong) {
    if (DiagnoseFailure) {
      S.Diag(VD->getBeginLoc(), diag::warn_ignored_objc_externally_retained)
          << 1;
    }
    return false;
  }

  // Tampering with the type of a VarDecl here is a bit of a hack, but we need
  // to ensure that the variable is 'const' so that we can error on
  // modification, which can otherwise over-release.
  VD->setType(Ty.withConst());
  VD->setARCPseudoStrong(true);
  return true;
}

static void handleObjCExternallyRetainedAttr(Sema &S, Decl *D,
                                             const ParsedAttr &AL) {
  if (auto *VD = dyn_cast<VarDecl>(D)) {
    assert(!isa<ParmVarDecl>(VD) && "should be diagnosed automatically");
    if (!VD->hasLocalStorage()) {
      S.Diag(D->getBeginLoc(), diag::warn_ignored_objc_externally_retained)
          << 0;
      return;
    }

    if (!tryMakeVariablePseudoStrong(S, VD, /*DiagnoseFailure=*/true))
      return;

    handleSimpleAttribute<ObjCExternallyRetainedAttr>(S, D, AL);
    return;
  }

  // If D is a function-like declaration (method, block, or function), then we
  // make every parameter psuedo-strong.
  for (unsigned I = 0, E = getFunctionOrMethodNumParams(D); I != E; ++I) {
    auto *PVD = const_cast<ParmVarDecl *>(getFunctionOrMethodParam(D, I));
    QualType Ty = PVD->getType();

    // If a user wrote a parameter with __strong explicitly, then assume they
    // want "real" strong semantics for that parameter. This works because if
    // the parameter was written with __strong, then the strong qualifier will
    // be non-local.
    if (Ty.getLocalUnqualifiedType().getQualifiers().getObjCLifetime() ==
        Qualifiers::OCL_Strong)
      continue;

    tryMakeVariablePseudoStrong(S, PVD, /*DiagnoseFailure=*/false);
  }
  handleSimpleAttribute<ObjCExternallyRetainedAttr>(S, D, AL);
}

//===----------------------------------------------------------------------===//
// Top Level Sema Entry Points
//===----------------------------------------------------------------------===//

/// ProcessDeclAttribute - Apply the specific attribute to the specified decl if
/// the attribute applies to decls.  If the attribute is a type attribute, just
/// silently ignore it if a GNU attribute.
static void ProcessDeclAttribute(Sema &S, Scope *scope, Decl *D,
                                 const ParsedAttr &AL,
                                 bool IncludeCXX11Attributes) {
  if (AL.isInvalid() || AL.getKind() == ParsedAttr::IgnoredAttribute)
    return;

  // Ignore C++11 attributes on declarator chunks: they appertain to the type
  // instead.
  if (AL.isCXX11Attribute() && !IncludeCXX11Attributes)
    return;

  // Unknown attributes are automatically warned on. Target-specific attributes
  // which do not apply to the current target architecture are treated as
  // though they were unknown attributes.
  if (AL.getKind() == ParsedAttr::UnknownAttribute ||
      !AL.existsInTarget(S.Context.getTargetInfo())) {
    S.Diag(AL.getLoc(),
           AL.isDeclspecAttribute()
               ? (unsigned)diag::warn_unhandled_ms_attribute_ignored
               : (unsigned)diag::warn_unknown_attribute_ignored)
        << AL;
    return;
  }

  if (handleCommonAttributeFeatures(S, D, AL))
    return;

  switch (AL.getKind()) {
  default:
    if (!AL.isStmtAttr()) {
      // Type attributes are handled elsewhere; silently move on.
      assert(AL.isTypeAttr() && "Non-type attribute not handled");
      break;
    }
    S.Diag(AL.getLoc(), diag::err_stmt_attribute_invalid_on_decl)
        << AL << D->getLocation();
    break;
  case ParsedAttr::AT_Interrupt:
    handleInterruptAttr(S, D, AL);
    break;
  case ParsedAttr::AT_X86ForceAlignArgPointer:
    handleX86ForceAlignArgPointerAttr(S, D, AL);
    break;
  case ParsedAttr::AT_DLLExport:
  case ParsedAttr::AT_DLLImport:
    handleDLLAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Mips16:
    handleSimpleAttributeWithExclusions<Mips16Attr, MicroMipsAttr,
                                        MipsInterruptAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_NoMips16:
    handleSimpleAttribute<NoMips16Attr>(S, D, AL);
    break;
  case ParsedAttr::AT_MicroMips:
    handleSimpleAttributeWithExclusions<MicroMipsAttr, Mips16Attr>(S, D, AL);
    break;
  case ParsedAttr::AT_NoMicroMips:
    handleSimpleAttribute<NoMicroMipsAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_MipsLongCall:
    handleSimpleAttributeWithExclusions<MipsLongCallAttr, MipsShortCallAttr>(
        S, D, AL);
    break;
  case ParsedAttr::AT_MipsShortCall:
    handleSimpleAttributeWithExclusions<MipsShortCallAttr, MipsLongCallAttr>(
        S, D, AL);
    break;
  case ParsedAttr::AT_AMDGPUFlatWorkGroupSize:
    handleAMDGPUFlatWorkGroupSizeAttr(S, D, AL);
    break;
  case ParsedAttr::AT_AMDGPUWavesPerEU:
    handleAMDGPUWavesPerEUAttr(S, D, AL);
    break;
  case ParsedAttr::AT_AMDGPUNumSGPR:
    handleAMDGPUNumSGPRAttr(S, D, AL);
    break;
  case ParsedAttr::AT_AMDGPUNumVGPR:
    handleAMDGPUNumVGPRAttr(S, D, AL);
    break;
  case ParsedAttr::AT_AVRSignal:
    handleAVRSignalAttr(S, D, AL);
    break;
  case ParsedAttr::AT_WebAssemblyImportModule:
    handleWebAssemblyImportModuleAttr(S, D, AL);
    break;
  case ParsedAttr::AT_WebAssemblyImportName:
    handleWebAssemblyImportNameAttr(S, D, AL);
    break;
  case ParsedAttr::AT_IBAction:
    handleSimpleAttribute<IBActionAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_IBOutlet:
    handleIBOutlet(S, D, AL);
    break;
  case ParsedAttr::AT_IBOutletCollection:
    handleIBOutletCollection(S, D, AL);
    break;
  case ParsedAttr::AT_IFunc:
    handleIFuncAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Alias:
    handleAliasAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Aligned:
    handleAlignedAttr(S, D, AL);
    break;
  case ParsedAttr::AT_AlignValue:
    handleAlignValueAttr(S, D, AL);
    break;
  case ParsedAttr::AT_AllocSize:
    handleAllocSizeAttr(S, D, AL);
    break;
  case ParsedAttr::AT_AlwaysInline:
    handleAlwaysInlineAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Artificial:
    handleSimpleAttribute<ArtificialAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_AnalyzerNoReturn:
    handleAnalyzerNoReturnAttr(S, D, AL);
    break;
  case ParsedAttr::AT_TLSModel:
    handleTLSModelAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Annotate:
    handleAnnotateAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Availability:
    handleAvailabilityAttr(S, D, AL);
    break;
  case ParsedAttr::AT_CarriesDependency:
    handleDependencyAttr(S, scope, D, AL);
    break;
  case ParsedAttr::AT_CPUDispatch:
  case ParsedAttr::AT_CPUSpecific:
    handleCPUSpecificAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Common:
    handleCommonAttr(S, D, AL);
    break;
  case ParsedAttr::AT_CUDAConstant:
    handleConstantAttr(S, D, AL);
    break;
  case ParsedAttr::AT_PassObjectSize:
    handlePassObjectSizeAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Constructor:
    handleConstructorAttr(S, D, AL);
    break;
  case ParsedAttr::AT_CXX11NoReturn:
    handleSimpleAttribute<CXX11NoReturnAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_Deprecated:
    handleDeprecatedAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Destructor:
    handleDestructorAttr(S, D, AL);
    break;
  case ParsedAttr::AT_EnableIf:
    handleEnableIfAttr(S, D, AL);
    break;
  case ParsedAttr::AT_DiagnoseIf:
    handleDiagnoseIfAttr(S, D, AL);
    break;
  case ParsedAttr::AT_ExtVectorType:
    handleExtVectorTypeAttr(S, D, AL);
    break;
  case ParsedAttr::AT_ExternalSourceSymbol:
    handleExternalSourceSymbolAttr(S, D, AL);
    break;
  case ParsedAttr::AT_MinSize:
    handleMinSizeAttr(S, D, AL);
    break;
  case ParsedAttr::AT_OptimizeNone:
    handleOptimizeNoneAttr(S, D, AL);
    break;
  case ParsedAttr::AT_FlagEnum:
    handleSimpleAttribute<FlagEnumAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_EnumExtensibility:
    handleEnumExtensibilityAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Flatten:
    handleSimpleAttribute<FlattenAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_Format:
    handleFormatAttr(S, D, AL);
    break;
  case ParsedAttr::AT_FormatArg:
    handleFormatArgAttr(S, D, AL);
    break;
  case ParsedAttr::AT_CUDAGlobal:
    handleGlobalAttr(S, D, AL);
    break;
  case ParsedAttr::AT_CUDADevice:
    handleSimpleAttributeWithExclusions<CUDADeviceAttr, CUDAGlobalAttr>(S, D,
                                                                        AL);
    break;
  case ParsedAttr::AT_CUDAHost:
    handleSimpleAttributeWithExclusions<CUDAHostAttr, CUDAGlobalAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_GNUInline:
    handleGNUInlineAttr(S, D, AL);
    break;
  case ParsedAttr::AT_CUDALaunchBounds:
    handleLaunchBoundsAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Restrict:
    handleRestrictAttr(S, D, AL);
    break;
  case ParsedAttr::AT_LifetimeBound:
    handleSimpleAttribute<LifetimeBoundAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_MayAlias:
    handleSimpleAttribute<MayAliasAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_Mode:
    handleModeAttr(S, D, AL);
    break;
  case ParsedAttr::AT_NoAlias:
    handleSimpleAttribute<NoAliasAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_NoCommon:
    handleSimpleAttribute<NoCommonAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_NoSplitStack:
    handleSimpleAttribute<NoSplitStackAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_NonNull:
    if (auto *PVD = dyn_cast<ParmVarDecl>(D))
      handleNonNullAttrParameter(S, PVD, AL);
    else
      handleNonNullAttr(S, D, AL);
    break;
  case ParsedAttr::AT_ReturnsNonNull:
    handleReturnsNonNullAttr(S, D, AL);
    break;
  case ParsedAttr::AT_NoEscape:
    handleNoEscapeAttr(S, D, AL);
    break;
  case ParsedAttr::AT_AssumeAligned:
    handleAssumeAlignedAttr(S, D, AL);
    break;
  case ParsedAttr::AT_AllocAlign:
    handleAllocAlignAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Overloadable:
    handleSimpleAttribute<OverloadableAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_Ownership:
    handleOwnershipAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Cold:
    handleSimpleAttributeWithExclusions<ColdAttr, HotAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_Hot:
    handleSimpleAttributeWithExclusions<HotAttr, ColdAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_Naked:
    handleNakedAttr(S, D, AL);
    break;
  case ParsedAttr::AT_NoReturn:
    handleNoReturnAttr(S, D, AL);
    break;
  case ParsedAttr::AT_AnyX86NoCfCheck:
    handleNoCfCheckAttr(S, D, AL);
    break;
  case ParsedAttr::AT_NoThrow:
    handleSimpleAttribute<NoThrowAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_CUDAShared:
    handleSharedAttr(S, D, AL);
    break;
  case ParsedAttr::AT_VecReturn:
    handleVecReturnAttr(S, D, AL);
    break;
  case ParsedAttr::AT_ObjCOwnership:
    handleObjCOwnershipAttr(S, D, AL);
    break;
  case ParsedAttr::AT_ObjCPreciseLifetime:
    handleObjCPreciseLifetimeAttr(S, D, AL);
    break;
  case ParsedAttr::AT_ObjCReturnsInnerPointer:
    handleObjCReturnsInnerPointerAttr(S, D, AL);
    break;
  case ParsedAttr::AT_ObjCRequiresSuper:
    handleObjCRequiresSuperAttr(S, D, AL);
    break;
  case ParsedAttr::AT_ObjCBridge:
    handleObjCBridgeAttr(S, D, AL);
    break;
  case ParsedAttr::AT_ObjCBridgeMutable:
    handleObjCBridgeMutableAttr(S, D, AL);
    break;
  case ParsedAttr::AT_ObjCBridgeRelated:
    handleObjCBridgeRelatedAttr(S, D, AL);
    break;
  case ParsedAttr::AT_ObjCDesignatedInitializer:
    handleObjCDesignatedInitializer(S, D, AL);
    break;
  case ParsedAttr::AT_ObjCRuntimeName:
    handleObjCRuntimeName(S, D, AL);
    break;
  case ParsedAttr::AT_ObjCRuntimeVisible:
    handleSimpleAttribute<ObjCRuntimeVisibleAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_ObjCBoxable:
    handleObjCBoxable(S, D, AL);
    break;
  case ParsedAttr::AT_CFAuditedTransfer:
    handleSimpleAttributeWithExclusions<CFAuditedTransferAttr,
                                        CFUnknownTransferAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_CFUnknownTransfer:
    handleSimpleAttributeWithExclusions<CFUnknownTransferAttr,
                                        CFAuditedTransferAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_CFConsumed:
  case ParsedAttr::AT_NSConsumed:
  case ParsedAttr::AT_OSConsumed:
    S.AddXConsumedAttr(D, AL.getRange(), AL.getAttributeSpellingListIndex(),
                     parsedAttrToRetainOwnershipKind(AL),
                     /*IsTemplateInstantiation=*/false);
    break;
  case ParsedAttr::AT_NSConsumesSelf:
    handleSimpleAttribute<NSConsumesSelfAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_OSConsumesThis:
    handleSimpleAttribute<OSConsumesThisAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_OSReturnsRetainedOnZero:
    handleSimpleAttributeOrDiagnose<OSReturnsRetainedOnZeroAttr>(
        S, D, AL, isValidOSObjectOutParameter(D),
        diag::warn_ns_attribute_wrong_parameter_type,
        /*Extra Args=*/AL, /*pointer-to-OSObject-pointer*/ 3, AL.getRange());
    break;
  case ParsedAttr::AT_OSReturnsRetainedOnNonZero:
    handleSimpleAttributeOrDiagnose<OSReturnsRetainedOnNonZeroAttr>(
        S, D, AL, isValidOSObjectOutParameter(D),
        diag::warn_ns_attribute_wrong_parameter_type,
        /*Extra Args=*/AL, /*pointer-to-OSObject-poointer*/ 3, AL.getRange());
    break;
  case ParsedAttr::AT_NSReturnsAutoreleased:
  case ParsedAttr::AT_NSReturnsNotRetained:
  case ParsedAttr::AT_NSReturnsRetained:
  case ParsedAttr::AT_CFReturnsNotRetained:
  case ParsedAttr::AT_CFReturnsRetained:
  case ParsedAttr::AT_OSReturnsNotRetained:
  case ParsedAttr::AT_OSReturnsRetained:
    handleXReturnsXRetainedAttr(S, D, AL);
    break;
  case ParsedAttr::AT_WorkGroupSizeHint:
    handleWorkGroupSize<WorkGroupSizeHintAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_ReqdWorkGroupSize:
    handleWorkGroupSize<ReqdWorkGroupSizeAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_OpenCLIntelReqdSubGroupSize:
    handleSubGroupSize(S, D, AL);
    break;
  case ParsedAttr::AT_VecTypeHint:
    handleVecTypeHint(S, D, AL);
    break;
  case ParsedAttr::AT_RequireConstantInit:
    handleSimpleAttribute<RequireConstantInitAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_InitPriority:
    handleInitPriorityAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Packed:
    handlePackedAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Section:
    handleSectionAttr(S, D, AL);
    break;
  case ParsedAttr::AT_SpeculativeLoadHardening:
    handleSimpleAttribute<SpeculativeLoadHardeningAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_CodeSeg:
    handleCodeSegAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Target:
    handleTargetAttr(S, D, AL);
    break;
  case ParsedAttr::AT_MinVectorWidth:
    handleMinVectorWidthAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Unavailable:
    handleAttrWithMessage<UnavailableAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_ArcWeakrefUnavailable:
    handleSimpleAttribute<ArcWeakrefUnavailableAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_ObjCRootClass:
    handleSimpleAttribute<ObjCRootClassAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_ObjCSubclassingRestricted:
    handleSimpleAttribute<ObjCSubclassingRestrictedAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_ObjCExplicitProtocolImpl:
    handleObjCSuppresProtocolAttr(S, D, AL);
    break;
  case ParsedAttr::AT_ObjCRequiresPropertyDefs:
    handleSimpleAttribute<ObjCRequiresPropertyDefsAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_Unused:
    handleUnusedAttr(S, D, AL);
    break;
  case ParsedAttr::AT_ReturnsTwice:
    handleSimpleAttribute<ReturnsTwiceAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_NotTailCalled:
    handleSimpleAttributeWithExclusions<NotTailCalledAttr, AlwaysInlineAttr>(
        S, D, AL);
    break;
  case ParsedAttr::AT_DisableTailCalls:
    handleSimpleAttributeWithExclusions<DisableTailCallsAttr, NakedAttr>(S, D,
                                                                         AL);
    break;
  case ParsedAttr::AT_Used:
    handleSimpleAttribute<UsedAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_Visibility:
    handleVisibilityAttr(S, D, AL, false);
    break;
  case ParsedAttr::AT_TypeVisibility:
    handleVisibilityAttr(S, D, AL, true);
    break;
  case ParsedAttr::AT_WarnUnused:
    handleSimpleAttribute<WarnUnusedAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_WarnUnusedResult:
    handleWarnUnusedResult(S, D, AL);
    break;
  case ParsedAttr::AT_Weak:
    handleSimpleAttribute<WeakAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_WeakRef:
    handleWeakRefAttr(S, D, AL);
    break;
  case ParsedAttr::AT_WeakImport:
    handleWeakImportAttr(S, D, AL);
    break;
  case ParsedAttr::AT_TransparentUnion:
    handleTransparentUnionAttr(S, D, AL);
    break;
  case ParsedAttr::AT_ObjCException:
    handleSimpleAttribute<ObjCExceptionAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_ObjCMethodFamily:
    handleObjCMethodFamilyAttr(S, D, AL);
    break;
  case ParsedAttr::AT_ObjCNSObject:
    handleObjCNSObject(S, D, AL);
    break;
  case ParsedAttr::AT_ObjCIndependentClass:
    handleObjCIndependentClass(S, D, AL);
    break;
  case ParsedAttr::AT_Blocks:
    handleBlocksAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Sentinel:
    handleSentinelAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Const:
    handleSimpleAttribute<ConstAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_Pure:
    handleSimpleAttribute<PureAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_Cleanup:
    handleCleanupAttr(S, D, AL);
    break;
  case ParsedAttr::AT_NoDebug:
    handleNoDebugAttr(S, D, AL);
    break;
  case ParsedAttr::AT_NoDuplicate:
    handleSimpleAttribute<NoDuplicateAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_Convergent:
    handleSimpleAttribute<ConvergentAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_NoInline:
    handleSimpleAttribute<NoInlineAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_NoInstrumentFunction: // Interacts with -pg.
    handleSimpleAttribute<NoInstrumentFunctionAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_NoStackProtector:
    // Interacts with -fstack-protector options.
    handleSimpleAttribute<NoStackProtectorAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_StdCall:
  case ParsedAttr::AT_CDecl:
  case ParsedAttr::AT_FastCall:
  case ParsedAttr::AT_ThisCall:
  case ParsedAttr::AT_Pascal:
  case ParsedAttr::AT_RegCall:
  case ParsedAttr::AT_SwiftCall:
  case ParsedAttr::AT_VectorCall:
  case ParsedAttr::AT_MSABI:
  case ParsedAttr::AT_SysVABI:
  case ParsedAttr::AT_Pcs:
  case ParsedAttr::AT_IntelOclBicc:
  case ParsedAttr::AT_PreserveMost:
  case ParsedAttr::AT_PreserveAll:
  case ParsedAttr::AT_AArch64VectorPcs:
    handleCallConvAttr(S, D, AL);
    break;
  case ParsedAttr::AT_Suppress:
    handleSuppressAttr(S, D, AL);
    break;
  case ParsedAttr::AT_OpenCLKernel:
    handleSimpleAttribute<OpenCLKernelAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_OpenCLAccess:
    handleOpenCLAccessAttr(S, D, AL);
    break;
  case ParsedAttr::AT_OpenCLNoSVM:
    handleOpenCLNoSVMAttr(S, D, AL);
    break;
  case ParsedAttr::AT_SwiftContext:
    handleParameterABIAttr(S, D, AL, ParameterABI::SwiftContext);
    break;
  case ParsedAttr::AT_SwiftErrorResult:
    handleParameterABIAttr(S, D, AL, ParameterABI::SwiftErrorResult);
    break;
  case ParsedAttr::AT_SwiftIndirectResult:
    handleParameterABIAttr(S, D, AL, ParameterABI::SwiftIndirectResult);
    break;
  case ParsedAttr::AT_InternalLinkage:
    handleInternalLinkageAttr(S, D, AL);
    break;
  case ParsedAttr::AT_ExcludeFromExplicitInstantiation:
    handleSimpleAttribute<ExcludeFromExplicitInstantiationAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_LTOVisibilityPublic:
    handleSimpleAttribute<LTOVisibilityPublicAttr>(S, D, AL);
    break;

  // Microsoft attributes:
  case ParsedAttr::AT_EmptyBases:
    handleSimpleAttribute<EmptyBasesAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_LayoutVersion:
    handleLayoutVersion(S, D, AL);
    break;
  case ParsedAttr::AT_TrivialABI:
    handleSimpleAttribute<TrivialABIAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_MSNoVTable:
    handleSimpleAttribute<MSNoVTableAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_MSStruct:
    handleSimpleAttribute<MSStructAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_Uuid:
    handleUuidAttr(S, D, AL);
    break;
  case ParsedAttr::AT_MSInheritance:
    handleMSInheritanceAttr(S, D, AL);
    break;
  case ParsedAttr::AT_SelectAny:
    handleSimpleAttribute<SelectAnyAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_Thread:
    handleDeclspecThreadAttr(S, D, AL);
    break;

  case ParsedAttr::AT_AbiTag:
    handleAbiTagAttr(S, D, AL);
    break;

  // Thread safety attributes:
  case ParsedAttr::AT_AssertExclusiveLock:
    handleAssertExclusiveLockAttr(S, D, AL);
    break;
  case ParsedAttr::AT_AssertSharedLock:
    handleAssertSharedLockAttr(S, D, AL);
    break;
  case ParsedAttr::AT_GuardedVar:
    handleSimpleAttribute<GuardedVarAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_PtGuardedVar:
    handlePtGuardedVarAttr(S, D, AL);
    break;
  case ParsedAttr::AT_ScopedLockable:
    handleSimpleAttribute<ScopedLockableAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_NoSanitize:
    handleNoSanitizeAttr(S, D, AL);
    break;
  case ParsedAttr::AT_NoSanitizeSpecific:
    handleNoSanitizeSpecificAttr(S, D, AL);
    break;
  case ParsedAttr::AT_NoThreadSafetyAnalysis:
    handleSimpleAttribute<NoThreadSafetyAnalysisAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_GuardedBy:
    handleGuardedByAttr(S, D, AL);
    break;
  case ParsedAttr::AT_PtGuardedBy:
    handlePtGuardedByAttr(S, D, AL);
    break;
  case ParsedAttr::AT_ExclusiveTrylockFunction:
    handleExclusiveTrylockFunctionAttr(S, D, AL);
    break;
  case ParsedAttr::AT_LockReturned:
    handleLockReturnedAttr(S, D, AL);
    break;
  case ParsedAttr::AT_LocksExcluded:
    handleLocksExcludedAttr(S, D, AL);
    break;
  case ParsedAttr::AT_SharedTrylockFunction:
    handleSharedTrylockFunctionAttr(S, D, AL);
    break;
  case ParsedAttr::AT_AcquiredBefore:
    handleAcquiredBeforeAttr(S, D, AL);
    break;
  case ParsedAttr::AT_AcquiredAfter:
    handleAcquiredAfterAttr(S, D, AL);
    break;

  // Capability analysis attributes.
  case ParsedAttr::AT_Capability:
  case ParsedAttr::AT_Lockable:
    handleCapabilityAttr(S, D, AL);
    break;
  case ParsedAttr::AT_RequiresCapability:
    handleRequiresCapabilityAttr(S, D, AL);
    break;

  case ParsedAttr::AT_AssertCapability:
    handleAssertCapabilityAttr(S, D, AL);
    break;
  case ParsedAttr::AT_AcquireCapability:
    handleAcquireCapabilityAttr(S, D, AL);
    break;
  case ParsedAttr::AT_ReleaseCapability:
    handleReleaseCapabilityAttr(S, D, AL);
    break;
  case ParsedAttr::AT_TryAcquireCapability:
    handleTryAcquireCapabilityAttr(S, D, AL);
    break;

  // Consumed analysis attributes.
  case ParsedAttr::AT_Consumable:
    handleConsumableAttr(S, D, AL);
    break;
  case ParsedAttr::AT_ConsumableAutoCast:
    handleSimpleAttribute<ConsumableAutoCastAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_ConsumableSetOnRead:
    handleSimpleAttribute<ConsumableSetOnReadAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_CallableWhen:
    handleCallableWhenAttr(S, D, AL);
    break;
  case ParsedAttr::AT_ParamTypestate:
    handleParamTypestateAttr(S, D, AL);
    break;
  case ParsedAttr::AT_ReturnTypestate:
    handleReturnTypestateAttr(S, D, AL);
    break;
  case ParsedAttr::AT_SetTypestate:
    handleSetTypestateAttr(S, D, AL);
    break;
  case ParsedAttr::AT_TestTypestate:
    handleTestTypestateAttr(S, D, AL);
    break;

  // Type safety attributes.
  case ParsedAttr::AT_ArgumentWithTypeTag:
    handleArgumentWithTypeTagAttr(S, D, AL);
    break;
  case ParsedAttr::AT_TypeTagForDatatype:
    handleTypeTagForDatatypeAttr(S, D, AL);
    break;
  case ParsedAttr::AT_AnyX86NoCallerSavedRegisters:
    handleSimpleAttribute<AnyX86NoCallerSavedRegistersAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_RenderScriptKernel:
    handleSimpleAttribute<RenderScriptKernelAttr>(S, D, AL);
    break;
  // XRay attributes.
  case ParsedAttr::AT_XRayInstrument:
    handleSimpleAttribute<XRayInstrumentAttr>(S, D, AL);
    break;
  case ParsedAttr::AT_XRayLogArgs:
    handleXRayLogArgsAttr(S, D, AL);
    break;

  // Move semantics attribute.
  case ParsedAttr::AT_Reinitializes:
    handleSimpleAttribute<ReinitializesAttr>(S, D, AL);
    break;

  case ParsedAttr::AT_AlwaysDestroy:
  case ParsedAttr::AT_NoDestroy:
    handleDestroyAttr(S, D, AL);
    break;

  case ParsedAttr::AT_Uninitialized:
    handleUninitializedAttr(S, D, AL);
    break;

  case ParsedAttr::AT_ObjCExternallyRetained:
    handleObjCExternallyRetainedAttr(S, D, AL);
    break;
  }
}

/// ProcessDeclAttributeList - Apply all the decl attributes in the specified
/// attribute list to the specified decl, ignoring any type attributes.
void Sema::ProcessDeclAttributeList(Scope *S, Decl *D,
                                    const ParsedAttributesView &AttrList,
                                    bool IncludeCXX11Attributes) {
  if (AttrList.empty())
    return;

  for (const ParsedAttr &AL : AttrList)
    ProcessDeclAttribute(*this, S, D, AL, IncludeCXX11Attributes);

  // FIXME: We should be able to handle these cases in TableGen.
  // GCC accepts
  // static int a9 __attribute__((weakref));
  // but that looks really pointless. We reject it.
  if (D->hasAttr<WeakRefAttr>() && !D->hasAttr<AliasAttr>()) {
    Diag(AttrList.begin()->getLoc(), diag::err_attribute_weakref_without_alias)
        << cast<NamedDecl>(D);
    D->dropAttr<WeakRefAttr>();
    return;
  }

  // FIXME: We should be able to handle this in TableGen as well. It would be
  // good to have a way to specify "these attributes must appear as a group",
  // for these. Additionally, it would be good to have a way to specify "these
  // attribute must never appear as a group" for attributes like cold and hot.
  if (!D->hasAttr<OpenCLKernelAttr>()) {
    // These attributes cannot be applied to a non-kernel function.
    if (const auto *A = D->getAttr<ReqdWorkGroupSizeAttr>()) {
      // FIXME: This emits a different error message than
      // diag::err_attribute_wrong_decl_type + ExpectedKernelFunction.
      Diag(D->getLocation(), diag::err_opencl_kernel_attr) << A;
      D->setInvalidDecl();
    } else if (const auto *A = D->getAttr<WorkGroupSizeHintAttr>()) {
      Diag(D->getLocation(), diag::err_opencl_kernel_attr) << A;
      D->setInvalidDecl();
    } else if (const auto *A = D->getAttr<VecTypeHintAttr>()) {
      Diag(D->getLocation(), diag::err_opencl_kernel_attr) << A;
      D->setInvalidDecl();
    } else if (const auto *A = D->getAttr<OpenCLIntelReqdSubGroupSizeAttr>()) {
      Diag(D->getLocation(), diag::err_opencl_kernel_attr) << A;
      D->setInvalidDecl();
    } else if (!D->hasAttr<CUDAGlobalAttr>()) {
      if (const auto *A = D->getAttr<AMDGPUFlatWorkGroupSizeAttr>()) {
        Diag(D->getLocation(), diag::err_attribute_wrong_decl_type)
            << A << ExpectedKernelFunction;
        D->setInvalidDecl();
      } else if (const auto *A = D->getAttr<AMDGPUWavesPerEUAttr>()) {
        Diag(D->getLocation(), diag::err_attribute_wrong_decl_type)
            << A << ExpectedKernelFunction;
        D->setInvalidDecl();
      } else if (const auto *A = D->getAttr<AMDGPUNumSGPRAttr>()) {
        Diag(D->getLocation(), diag::err_attribute_wrong_decl_type)
            << A << ExpectedKernelFunction;
        D->setInvalidDecl();
      } else if (const auto *A = D->getAttr<AMDGPUNumVGPRAttr>()) {
        Diag(D->getLocation(), diag::err_attribute_wrong_decl_type)
            << A << ExpectedKernelFunction;
        D->setInvalidDecl();
      }
    }
  }

  // Do this check after processing D's attributes because the attribute
  // objc_method_family can change whether the given method is in the init
  // family, and it can be applied after objc_designated_initializer. This is a
  // bit of a hack, but we need it to be compatible with versions of clang that
  // processed the attribute list in the wrong order.
  if (D->hasAttr<ObjCDesignatedInitializerAttr>() &&
      cast<ObjCMethodDecl>(D)->getMethodFamily() != OMF_init) {
    Diag(D->getLocation(), diag::err_designated_init_attr_non_init);
    D->dropAttr<ObjCDesignatedInitializerAttr>();
  }
}

// Helper for delayed processing TransparentUnion attribute.
void Sema::ProcessDeclAttributeDelayed(Decl *D,
                                       const ParsedAttributesView &AttrList) {
  for (const ParsedAttr &AL : AttrList)
    if (AL.getKind() == ParsedAttr::AT_TransparentUnion) {
      handleTransparentUnionAttr(*this, D, AL);
      break;
    }
}

// Annotation attributes are the only attributes allowed after an access
// specifier.
bool Sema::ProcessAccessDeclAttributeList(
    AccessSpecDecl *ASDecl, const ParsedAttributesView &AttrList) {
  for (const ParsedAttr &AL : AttrList) {
    if (AL.getKind() == ParsedAttr::AT_Annotate) {
      ProcessDeclAttribute(*this, nullptr, ASDecl, AL, AL.isCXX11Attribute());
    } else {
      Diag(AL.getLoc(), diag::err_only_annotate_after_access_spec);
      return true;
    }
  }
  return false;
}

/// checkUnusedDeclAttributes - Check a list of attributes to see if it
/// contains any decl attributes that we should warn about.
static void checkUnusedDeclAttributes(Sema &S, const ParsedAttributesView &A) {
  for (const ParsedAttr &AL : A) {
    // Only warn if the attribute is an unignored, non-type attribute.
    if (AL.isUsedAsTypeAttr() || AL.isInvalid())
      continue;
    if (AL.getKind() == ParsedAttr::IgnoredAttribute)
      continue;

    if (AL.getKind() == ParsedAttr::UnknownAttribute) {
      S.Diag(AL.getLoc(), diag::warn_unknown_attribute_ignored)
          << AL << AL.getRange();
    } else {
      S.Diag(AL.getLoc(), diag::warn_attribute_not_on_decl) << AL
                                                            << AL.getRange();
    }
  }
}

/// checkUnusedDeclAttributes - Given a declarator which is not being
/// used to build a declaration, complain about any decl attributes
/// which might be lying around on it.
void Sema::checkUnusedDeclAttributes(Declarator &D) {
  ::checkUnusedDeclAttributes(*this, D.getDeclSpec().getAttributes());
  ::checkUnusedDeclAttributes(*this, D.getAttributes());
  for (unsigned i = 0, e = D.getNumTypeObjects(); i != e; ++i)
    ::checkUnusedDeclAttributes(*this, D.getTypeObject(i).getAttrs());
}

/// DeclClonePragmaWeak - clone existing decl (maybe definition),
/// \#pragma weak needs a non-definition decl and source may not have one.
NamedDecl * Sema::DeclClonePragmaWeak(NamedDecl *ND, IdentifierInfo *II,
                                      SourceLocation Loc) {
  assert(isa<FunctionDecl>(ND) || isa<VarDecl>(ND));
  NamedDecl *NewD = nullptr;
  if (auto *FD = dyn_cast<FunctionDecl>(ND)) {
    FunctionDecl *NewFD;
    // FIXME: Missing call to CheckFunctionDeclaration().
    // FIXME: Mangling?
    // FIXME: Is the qualifier info correct?
    // FIXME: Is the DeclContext correct?
    NewFD = FunctionDecl::Create(FD->getASTContext(), FD->getDeclContext(),
                                 Loc, Loc, DeclarationName(II),
                                 FD->getType(), FD->getTypeSourceInfo(),
                                 SC_None, false/*isInlineSpecified*/,
                                 FD->hasPrototype(),
                                 false/*isConstexprSpecified*/);
    NewD = NewFD;

    if (FD->getQualifier())
      NewFD->setQualifierInfo(FD->getQualifierLoc());

    // Fake up parameter variables; they are declared as if this were
    // a typedef.
    QualType FDTy = FD->getType();
    if (const auto *FT = FDTy->getAs<FunctionProtoType>()) {
      SmallVector<ParmVarDecl*, 16> Params;
      for (const auto &AI : FT->param_types()) {
        ParmVarDecl *Param = BuildParmVarDeclForTypedef(NewFD, Loc, AI);
        Param->setScopeInfo(0, Params.size());
        Params.push_back(Param);
      }
      NewFD->setParams(Params);
    }
  } else if (auto *VD = dyn_cast<VarDecl>(ND)) {
    NewD = VarDecl::Create(VD->getASTContext(), VD->getDeclContext(),
                           VD->getInnerLocStart(), VD->getLocation(), II,
                           VD->getType(), VD->getTypeSourceInfo(),
                           VD->getStorageClass());
    if (VD->getQualifier())
      cast<VarDecl>(NewD)->setQualifierInfo(VD->getQualifierLoc());
  }
  return NewD;
}

/// DeclApplyPragmaWeak - A declaration (maybe definition) needs \#pragma weak
/// applied to it, possibly with an alias.
void Sema::DeclApplyPragmaWeak(Scope *S, NamedDecl *ND, WeakInfo &W) {
  if (W.getUsed()) return; // only do this once
  W.setUsed(true);
  if (W.getAlias()) { // clone decl, impersonate __attribute(weak,alias(...))
    IdentifierInfo *NDId = ND->getIdentifier();
    NamedDecl *NewD = DeclClonePragmaWeak(ND, W.getAlias(), W.getLocation());
    NewD->addAttr(AliasAttr::CreateImplicit(Context, NDId->getName(),
                                            W.getLocation()));
    NewD->addAttr(WeakAttr::CreateImplicit(Context, W.getLocation()));
    WeakTopLevelDecl.push_back(NewD);
    // FIXME: "hideous" code from Sema::LazilyCreateBuiltin
    // to insert Decl at TU scope, sorry.
    DeclContext *SavedContext = CurContext;
    CurContext = Context.getTranslationUnitDecl();
    NewD->setDeclContext(CurContext);
    NewD->setLexicalDeclContext(CurContext);
    PushOnScopeChains(NewD, S);
    CurContext = SavedContext;
  } else { // just add weak to existing
    ND->addAttr(WeakAttr::CreateImplicit(Context, W.getLocation()));
  }
}

void Sema::ProcessPragmaWeak(Scope *S, Decl *D) {
  // It's valid to "forward-declare" #pragma weak, in which case we
  // have to do this.
  LoadExternalWeakUndeclaredIdentifiers();
  if (!WeakUndeclaredIdentifiers.empty()) {
    NamedDecl *ND = nullptr;
    if (auto *VD = dyn_cast<VarDecl>(D))
      if (VD->isExternC())
        ND = VD;
    if (auto *FD = dyn_cast<FunctionDecl>(D))
      if (FD->isExternC())
        ND = FD;
    if (ND) {
      if (IdentifierInfo *Id = ND->getIdentifier()) {
        auto I = WeakUndeclaredIdentifiers.find(Id);
        if (I != WeakUndeclaredIdentifiers.end()) {
          WeakInfo W = I->second;
          DeclApplyPragmaWeak(S, ND, W);
          WeakUndeclaredIdentifiers[Id] = W;
        }
      }
    }
  }
}

/// ProcessDeclAttributes - Given a declarator (PD) with attributes indicated in
/// it, apply them to D.  This is a bit tricky because PD can have attributes
/// specified in many different places, and we need to find and apply them all.
void Sema::ProcessDeclAttributes(Scope *S, Decl *D, const Declarator &PD) {
  // Apply decl attributes from the DeclSpec if present.
  if (!PD.getDeclSpec().getAttributes().empty())
    ProcessDeclAttributeList(S, D, PD.getDeclSpec().getAttributes());

  // Walk the declarator structure, applying decl attributes that were in a type
  // position to the decl itself.  This handles cases like:
  //   int *__attr__(x)** D;
  // when X is a decl attribute.
  for (unsigned i = 0, e = PD.getNumTypeObjects(); i != e; ++i)
    ProcessDeclAttributeList(S, D, PD.getTypeObject(i).getAttrs(),
                             /*IncludeCXX11Attributes=*/false);

  // Finally, apply any attributes on the decl itself.
  ProcessDeclAttributeList(S, D, PD.getAttributes());

  // Apply additional attributes specified by '#pragma clang attribute'.
  AddPragmaAttributes(S, D);
}

/// Is the given declaration allowed to use a forbidden type?
/// If so, it'll still be annotated with an attribute that makes it
/// illegal to actually use.
static bool isForbiddenTypeAllowed(Sema &S, Decl *D,
                                   const DelayedDiagnostic &diag,
                                   UnavailableAttr::ImplicitReason &reason) {
  // Private ivars are always okay.  Unfortunately, people don't
  // always properly make their ivars private, even in system headers.
  // Plus we need to make fields okay, too.
  if (!isa<FieldDecl>(D) && !isa<ObjCPropertyDecl>(D) &&
      !isa<FunctionDecl>(D))
    return false;

  // Silently accept unsupported uses of __weak in both user and system
  // declarations when it's been disabled, for ease of integration with
  // -fno-objc-arc files.  We do have to take some care against attempts
  // to define such things;  for now, we've only done that for ivars
  // and properties.
  if ((isa<ObjCIvarDecl>(D) || isa<ObjCPropertyDecl>(D))) {
    if (diag.getForbiddenTypeDiagnostic() == diag::err_arc_weak_disabled ||
        diag.getForbiddenTypeDiagnostic() == diag::err_arc_weak_no_runtime) {
      reason = UnavailableAttr::IR_ForbiddenWeak;
      return true;
    }
  }

  // Allow all sorts of things in system headers.
  if (S.Context.getSourceManager().isInSystemHeader(D->getLocation())) {
    // Currently, all the failures dealt with this way are due to ARC
    // restrictions.
    reason = UnavailableAttr::IR_ARCForbiddenType;
    return true;
  }

  return false;
}

/// Handle a delayed forbidden-type diagnostic.
static void handleDelayedForbiddenType(Sema &S, DelayedDiagnostic &DD,
                                       Decl *D) {
  auto Reason = UnavailableAttr::IR_None;
  if (D && isForbiddenTypeAllowed(S, D, DD, Reason)) {
    assert(Reason && "didn't set reason?");
    D->addAttr(UnavailableAttr::CreateImplicit(S.Context, "", Reason, DD.Loc));
    return;
  }
  if (S.getLangOpts().ObjCAutoRefCount)
    if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
      // FIXME: we may want to suppress diagnostics for all
      // kind of forbidden type messages on unavailable functions.
      if (FD->hasAttr<UnavailableAttr>() &&
          DD.getForbiddenTypeDiagnostic() ==
              diag::err_arc_array_param_no_ownership) {
        DD.Triggered = true;
        return;
      }
    }

  S.Diag(DD.Loc, DD.getForbiddenTypeDiagnostic())
      << DD.getForbiddenTypeOperand() << DD.getForbiddenTypeArgument();
  DD.Triggered = true;
}

static const AvailabilityAttr *getAttrForPlatform(ASTContext &Context,
                                                  const Decl *D) {
  // Check each AvailabilityAttr to find the one for this platform.
  for (const auto *A : D->attrs()) {
    if (const auto *Avail = dyn_cast<AvailabilityAttr>(A)) {
      // FIXME: this is copied from CheckAvailability. We should try to
      // de-duplicate.

      // Check if this is an App Extension "platform", and if so chop off
      // the suffix for matching with the actual platform.
      StringRef ActualPlatform = Avail->getPlatform()->getName();
      StringRef RealizedPlatform = ActualPlatform;
      if (Context.getLangOpts().AppExt) {
        size_t suffix = RealizedPlatform.rfind("_app_extension");
        if (suffix != StringRef::npos)
          RealizedPlatform = RealizedPlatform.slice(0, suffix);
      }

      StringRef TargetPlatform = Context.getTargetInfo().getPlatformName();

      // Match the platform name.
      if (RealizedPlatform == TargetPlatform)
        return Avail;
    }
  }
  return nullptr;
}

/// The diagnostic we should emit for \c D, and the declaration that
/// originated it, or \c AR_Available.
///
/// \param D The declaration to check.
/// \param Message If non-null, this will be populated with the message from
/// the availability attribute that is selected.
/// \param ClassReceiver If we're checking the the method of a class message
/// send, the class. Otherwise nullptr.
static std::pair<AvailabilityResult, const NamedDecl *>
ShouldDiagnoseAvailabilityOfDecl(Sema &S, const NamedDecl *D,
                                 std::string *Message,
                                 ObjCInterfaceDecl *ClassReceiver) {
  AvailabilityResult Result = D->getAvailability(Message);

  // For typedefs, if the typedef declaration appears available look
  // to the underlying type to see if it is more restrictive.
  while (const auto *TD = dyn_cast<TypedefNameDecl>(D)) {
    if (Result == AR_Available) {
      if (const auto *TT = TD->getUnderlyingType()->getAs<TagType>()) {
        D = TT->getDecl();
        Result = D->getAvailability(Message);
        continue;
      }
    }
    break;
  }

  // Forward class declarations get their attributes from their definition.
  if (const auto *IDecl = dyn_cast<ObjCInterfaceDecl>(D)) {
    if (IDecl->getDefinition()) {
      D = IDecl->getDefinition();
      Result = D->getAvailability(Message);
    }
  }

  if (const auto *ECD = dyn_cast<EnumConstantDecl>(D))
    if (Result == AR_Available) {
      const DeclContext *DC = ECD->getDeclContext();
      if (const auto *TheEnumDecl = dyn_cast<EnumDecl>(DC)) {
        Result = TheEnumDecl->getAvailability(Message);
        D = TheEnumDecl;
      }
    }

  // For +new, infer availability from -init.
  if (const auto *MD = dyn_cast<ObjCMethodDecl>(D)) {
    if (S.NSAPIObj && ClassReceiver) {
      ObjCMethodDecl *Init = ClassReceiver->lookupInstanceMethod(
          S.NSAPIObj->getInitSelector());
      if (Init && Result == AR_Available && MD->isClassMethod() &&
          MD->getSelector() == S.NSAPIObj->getNewSelector() &&
          MD->definedInNSObject(S.getASTContext())) {
        Result = Init->getAvailability(Message);
        D = Init;
      }
    }
  }

  return {Result, D};
}


/// whether we should emit a diagnostic for \c K and \c DeclVersion in
/// the context of \c Ctx. For example, we should emit an unavailable diagnostic
/// in a deprecated context, but not the other way around.
static bool
ShouldDiagnoseAvailabilityInContext(Sema &S, AvailabilityResult K,
                                    VersionTuple DeclVersion, Decl *Ctx,
                                    const NamedDecl *OffendingDecl) {
  assert(K != AR_Available && "Expected an unavailable declaration here!");

  // Checks if we should emit the availability diagnostic in the context of C.
  auto CheckContext = [&](const Decl *C) {
    if (K == AR_NotYetIntroduced) {
      if (const AvailabilityAttr *AA = getAttrForPlatform(S.Context, C))
        if (AA->getIntroduced() >= DeclVersion)
          return true;
    } else if (K == AR_Deprecated) {
      if (C->isDeprecated())
        return true;
    } else if (K == AR_Unavailable) {
      // It is perfectly fine to refer to an 'unavailable' Objective-C method
      // when it is referenced from within the @implementation itself. In this
      // context, we interpret unavailable as a form of access control.
      if (const auto *MD = dyn_cast<ObjCMethodDecl>(OffendingDecl)) {
        if (const auto *Impl = dyn_cast<ObjCImplDecl>(C)) {
          if (MD->getClassInterface() == Impl->getClassInterface())
            return true;
        }
      }
    }

    if (C->isUnavailable())
      return true;
    return false;
  };

  do {
    if (CheckContext(Ctx))
      return false;

    // An implementation implicitly has the availability of the interface.
    // Unless it is "+load" method.
    if (const auto *MethodD = dyn_cast<ObjCMethodDecl>(Ctx))
      if (MethodD->isClassMethod() &&
          MethodD->getSelector().getAsString() == "load")
        return true;

    if (const auto *CatOrImpl = dyn_cast<ObjCImplDecl>(Ctx)) {
      if (const ObjCInterfaceDecl *Interface = CatOrImpl->getClassInterface())
        if (CheckContext(Interface))
          return false;
    }
    // A category implicitly has the availability of the interface.
    else if (const auto *CatD = dyn_cast<ObjCCategoryDecl>(Ctx))
      if (const ObjCInterfaceDecl *Interface = CatD->getClassInterface())
        if (CheckContext(Interface))
          return false;
  } while ((Ctx = cast_or_null<Decl>(Ctx->getDeclContext())));

  return true;
}

static bool
shouldDiagnoseAvailabilityByDefault(const ASTContext &Context,
                                    const VersionTuple &DeploymentVersion,
                                    const VersionTuple &DeclVersion) {
  const auto &Triple = Context.getTargetInfo().getTriple();
  VersionTuple ForceAvailabilityFromVersion;
  switch (Triple.getOS()) {
  case llvm::Triple::IOS:
  case llvm::Triple::TvOS:
    ForceAvailabilityFromVersion = VersionTuple(/*Major=*/11);
    break;
  case llvm::Triple::WatchOS:
    ForceAvailabilityFromVersion = VersionTuple(/*Major=*/4);
    break;
  case llvm::Triple::Darwin:
  case llvm::Triple::MacOSX:
    ForceAvailabilityFromVersion = VersionTuple(/*Major=*/10, /*Minor=*/13);
    break;
  default:
    // New targets should always warn about availability.
    return Triple.getVendor() == llvm::Triple::Apple;
  }
  return DeploymentVersion >= ForceAvailabilityFromVersion ||
         DeclVersion >= ForceAvailabilityFromVersion;
}

static NamedDecl *findEnclosingDeclToAnnotate(Decl *OrigCtx) {
  for (Decl *Ctx = OrigCtx; Ctx;
       Ctx = cast_or_null<Decl>(Ctx->getDeclContext())) {
    if (isa<TagDecl>(Ctx) || isa<FunctionDecl>(Ctx) || isa<ObjCMethodDecl>(Ctx))
      return cast<NamedDecl>(Ctx);
    if (auto *CD = dyn_cast<ObjCContainerDecl>(Ctx)) {
      if (auto *Imp = dyn_cast<ObjCImplDecl>(Ctx))
        return Imp->getClassInterface();
      return CD;
    }
  }

  return dyn_cast<NamedDecl>(OrigCtx);
}

namespace {

struct AttributeInsertion {
  StringRef Prefix;
  SourceLocation Loc;
  StringRef Suffix;

  static AttributeInsertion createInsertionAfter(const NamedDecl *D) {
    return {" ", D->getEndLoc(), ""};
  }
  static AttributeInsertion createInsertionAfter(SourceLocation Loc) {
    return {" ", Loc, ""};
  }
  static AttributeInsertion createInsertionBefore(const NamedDecl *D) {
    return {"", D->getBeginLoc(), "\n"};
  }
};

} // end anonymous namespace

/// Tries to parse a string as ObjC method name.
///
/// \param Name The string to parse. Expected to originate from availability
/// attribute argument.
/// \param SlotNames The vector that will be populated with slot names. In case
/// of unsuccessful parsing can contain invalid data.
/// \returns A number of method parameters if parsing was successful, None
/// otherwise.
static Optional<unsigned>
tryParseObjCMethodName(StringRef Name, SmallVectorImpl<StringRef> &SlotNames,
                       const LangOptions &LangOpts) {
  // Accept replacements starting with - or + as valid ObjC method names.
  if (!Name.empty() && (Name.front() == '-' || Name.front() == '+'))
    Name = Name.drop_front(1);
  if (Name.empty())
    return None;
  Name.split(SlotNames, ':');
  unsigned NumParams;
  if (Name.back() == ':') {
    // Remove an empty string at the end that doesn't represent any slot.
    SlotNames.pop_back();
    NumParams = SlotNames.size();
  } else {
    if (SlotNames.size() != 1)
      // Not a valid method name, just a colon-separated string.
      return None;
    NumParams = 0;
  }
  // Verify all slot names are valid.
  bool AllowDollar = LangOpts.DollarIdents;
  for (StringRef S : SlotNames) {
    if (S.empty())
      continue;
    if (!isValidIdentifier(S, AllowDollar))
      return None;
  }
  return NumParams;
}

/// Returns a source location in which it's appropriate to insert a new
/// attribute for the given declaration \D.
static Optional<AttributeInsertion>
createAttributeInsertion(const NamedDecl *D, const SourceManager &SM,
                         const LangOptions &LangOpts) {
  if (isa<ObjCPropertyDecl>(D))
    return AttributeInsertion::createInsertionAfter(D);
  if (const auto *MD = dyn_cast<ObjCMethodDecl>(D)) {
    if (MD->hasBody())
      return None;
    return AttributeInsertion::createInsertionAfter(D);
  }
  if (const auto *TD = dyn_cast<TagDecl>(D)) {
    SourceLocation Loc =
        Lexer::getLocForEndOfToken(TD->getInnerLocStart(), 0, SM, LangOpts);
    if (Loc.isInvalid())
      return None;
    // Insert after the 'struct'/whatever keyword.
    return AttributeInsertion::createInsertionAfter(Loc);
  }
  return AttributeInsertion::createInsertionBefore(D);
}

/// Actually emit an availability diagnostic for a reference to an unavailable
/// decl.
///
/// \param Ctx The context that the reference occurred in
/// \param ReferringDecl The exact declaration that was referenced.
/// \param OffendingDecl A related decl to \c ReferringDecl that has an
/// availability attribute corresponding to \c K attached to it. Note that this
/// may not be the same as ReferringDecl, i.e. if an EnumDecl is annotated and
/// we refer to a member EnumConstantDecl, ReferringDecl is the EnumConstantDecl
/// and OffendingDecl is the EnumDecl.
static void DoEmitAvailabilityWarning(Sema &S, AvailabilityResult K,
                                      Decl *Ctx, const NamedDecl *ReferringDecl,
                                      const NamedDecl *OffendingDecl,
                                      StringRef Message,
                                      ArrayRef<SourceLocation> Locs,
                                      const ObjCInterfaceDecl *UnknownObjCClass,
                                      const ObjCPropertyDecl *ObjCProperty,
                                      bool ObjCPropertyAccess) {
  // Diagnostics for deprecated or unavailable.
  unsigned diag, diag_message, diag_fwdclass_message;
  unsigned diag_available_here = diag::note_availability_specified_here;
  SourceLocation NoteLocation = OffendingDecl->getLocation();

  // Matches 'diag::note_property_attribute' options.
  unsigned property_note_select;

  // Matches diag::note_availability_specified_here.
  unsigned available_here_select_kind;

  VersionTuple DeclVersion;
  if (const AvailabilityAttr *AA = getAttrForPlatform(S.Context, OffendingDecl))
    DeclVersion = AA->getIntroduced();

  if (!ShouldDiagnoseAvailabilityInContext(S, K, DeclVersion, Ctx,
                                           OffendingDecl))
    return;

  SourceLocation Loc = Locs.front();

  // The declaration can have multiple availability attributes, we are looking
  // at one of them.
  const AvailabilityAttr *A = getAttrForPlatform(S.Context, OffendingDecl);
  if (A && A->isInherited()) {
    for (const Decl *Redecl = OffendingDecl->getMostRecentDecl(); Redecl;
         Redecl = Redecl->getPreviousDecl()) {
      const AvailabilityAttr *AForRedecl =
          getAttrForPlatform(S.Context, Redecl);
      if (AForRedecl && !AForRedecl->isInherited()) {
        // If D is a declaration with inherited attributes, the note should
        // point to the declaration with actual attributes.
        NoteLocation = Redecl->getLocation();
        break;
      }
    }
  }

  switch (K) {
  case AR_NotYetIntroduced: {
    // We would like to emit the diagnostic even if -Wunguarded-availability is
    // not specified for deployment targets >= to iOS 11 or equivalent or
    // for declarations that were introduced in iOS 11 (macOS 10.13, ...) or
    // later.
    const AvailabilityAttr *AA =
        getAttrForPlatform(S.getASTContext(), OffendingDecl);
    VersionTuple Introduced = AA->getIntroduced();

    bool UseNewWarning = shouldDiagnoseAvailabilityByDefault(
        S.Context, S.Context.getTargetInfo().getPlatformMinVersion(),
        Introduced);
    unsigned Warning = UseNewWarning ? diag::warn_unguarded_availability_new
                                     : diag::warn_unguarded_availability;

    std::string PlatformName = AvailabilityAttr::getPrettyPlatformName(
        S.getASTContext().getTargetInfo().getPlatformName());

    S.Diag(Loc, Warning) << OffendingDecl << PlatformName
                         << Introduced.getAsString();

    S.Diag(OffendingDecl->getLocation(),
           diag::note_partial_availability_specified_here)
        << OffendingDecl << PlatformName << Introduced.getAsString()
        << S.Context.getTargetInfo().getPlatformMinVersion().getAsString();

    if (const auto *Enclosing = findEnclosingDeclToAnnotate(Ctx)) {
      if (const auto *TD = dyn_cast<TagDecl>(Enclosing))
        if (TD->getDeclName().isEmpty()) {
          S.Diag(TD->getLocation(),
                 diag::note_decl_unguarded_availability_silence)
              << /*Anonymous*/ 1 << TD->getKindName();
          return;
        }
      auto FixitNoteDiag =
          S.Diag(Enclosing->getLocation(),
                 diag::note_decl_unguarded_availability_silence)
          << /*Named*/ 0 << Enclosing;
      // Don't offer a fixit for declarations with availability attributes.
      if (Enclosing->hasAttr<AvailabilityAttr>())
        return;
      if (!S.getPreprocessor().isMacroDefined("API_AVAILABLE"))
        return;
      Optional<AttributeInsertion> Insertion = createAttributeInsertion(
          Enclosing, S.getSourceManager(), S.getLangOpts());
      if (!Insertion)
        return;
      std::string PlatformName =
          AvailabilityAttr::getPlatformNameSourceSpelling(
              S.getASTContext().getTargetInfo().getPlatformName())
              .lower();
      std::string Introduced =
          OffendingDecl->getVersionIntroduced().getAsString();
      FixitNoteDiag << FixItHint::CreateInsertion(
          Insertion->Loc,
          (llvm::Twine(Insertion->Prefix) + "API_AVAILABLE(" + PlatformName +
           "(" + Introduced + "))" + Insertion->Suffix)
              .str());
    }
    return;
  }
  case AR_Deprecated:
    diag = !ObjCPropertyAccess ? diag::warn_deprecated
                               : diag::warn_property_method_deprecated;
    diag_message = diag::warn_deprecated_message;
    diag_fwdclass_message = diag::warn_deprecated_fwdclass_message;
    property_note_select = /* deprecated */ 0;
    available_here_select_kind = /* deprecated */ 2;
    if (const auto *AL = OffendingDecl->getAttr<DeprecatedAttr>())
      NoteLocation = AL->getLocation();
    break;

  case AR_Unavailable:
    diag = !ObjCPropertyAccess ? diag::err_unavailable
                               : diag::err_property_method_unavailable;
    diag_message = diag::err_unavailable_message;
    diag_fwdclass_message = diag::warn_unavailable_fwdclass_message;
    property_note_select = /* unavailable */ 1;
    available_here_select_kind = /* unavailable */ 0;

    if (auto AL = OffendingDecl->getAttr<UnavailableAttr>()) {
      if (AL->isImplicit() && AL->getImplicitReason()) {
        // Most of these failures are due to extra restrictions in ARC;
        // reflect that in the primary diagnostic when applicable.
        auto flagARCError = [&] {
          if (S.getLangOpts().ObjCAutoRefCount &&
              S.getSourceManager().isInSystemHeader(
                  OffendingDecl->getLocation()))
            diag = diag::err_unavailable_in_arc;
        };

        switch (AL->getImplicitReason()) {
        case UnavailableAttr::IR_None: break;

        case UnavailableAttr::IR_ARCForbiddenType:
          flagARCError();
          diag_available_here = diag::note_arc_forbidden_type;
          break;

        case UnavailableAttr::IR_ForbiddenWeak:
          if (S.getLangOpts().ObjCWeakRuntime)
            diag_available_here = diag::note_arc_weak_disabled;
          else
            diag_available_here = diag::note_arc_weak_no_runtime;
          break;

        case UnavailableAttr::IR_ARCForbiddenConversion:
          flagARCError();
          diag_available_here = diag::note_performs_forbidden_arc_conversion;
          break;

        case UnavailableAttr::IR_ARCInitReturnsUnrelated:
          flagARCError();
          diag_available_here = diag::note_arc_init_returns_unrelated;
          break;

        case UnavailableAttr::IR_ARCFieldWithOwnership:
          flagARCError();
          diag_available_here = diag::note_arc_field_with_ownership;
          break;
        }
      }
    }
    break;

  case AR_Available:
    llvm_unreachable("Warning for availability of available declaration?");
  }

  SmallVector<FixItHint, 12> FixIts;
  if (K == AR_Deprecated) {
    StringRef Replacement;
    if (auto AL = OffendingDecl->getAttr<DeprecatedAttr>())
      Replacement = AL->getReplacement();
    if (auto AL = getAttrForPlatform(S.Context, OffendingDecl))
      Replacement = AL->getReplacement();

    CharSourceRange UseRange;
    if (!Replacement.empty())
      UseRange =
          CharSourceRange::getCharRange(Loc, S.getLocForEndOfToken(Loc));
    if (UseRange.isValid()) {
      if (const auto *MethodDecl = dyn_cast<ObjCMethodDecl>(ReferringDecl)) {
        Selector Sel = MethodDecl->getSelector();
        SmallVector<StringRef, 12> SelectorSlotNames;
        Optional<unsigned> NumParams = tryParseObjCMethodName(
            Replacement, SelectorSlotNames, S.getLangOpts());
        if (NumParams && NumParams.getValue() == Sel.getNumArgs()) {
          assert(SelectorSlotNames.size() == Locs.size());
          for (unsigned I = 0; I < Locs.size(); ++I) {
            if (!Sel.getNameForSlot(I).empty()) {
              CharSourceRange NameRange = CharSourceRange::getCharRange(
                  Locs[I], S.getLocForEndOfToken(Locs[I]));
              FixIts.push_back(FixItHint::CreateReplacement(
                  NameRange, SelectorSlotNames[I]));
            } else
              FixIts.push_back(
                  FixItHint::CreateInsertion(Locs[I], SelectorSlotNames[I]));
          }
        } else
          FixIts.push_back(FixItHint::CreateReplacement(UseRange, Replacement));
      } else
        FixIts.push_back(FixItHint::CreateReplacement(UseRange, Replacement));
    }
  }

  if (!Message.empty()) {
    S.Diag(Loc, diag_message) << ReferringDecl << Message << FixIts;
    if (ObjCProperty)
      S.Diag(ObjCProperty->getLocation(), diag::note_property_attribute)
          << ObjCProperty->getDeclName() << property_note_select;
  } else if (!UnknownObjCClass) {
    S.Diag(Loc, diag) << ReferringDecl << FixIts;
    if (ObjCProperty)
      S.Diag(ObjCProperty->getLocation(), diag::note_property_attribute)
          << ObjCProperty->getDeclName() << property_note_select;
  } else {
    S.Diag(Loc, diag_fwdclass_message) << ReferringDecl << FixIts;
    S.Diag(UnknownObjCClass->getLocation(), diag::note_forward_class);
  }

  S.Diag(NoteLocation, diag_available_here)
    << OffendingDecl << available_here_select_kind;
}

static void handleDelayedAvailabilityCheck(Sema &S, DelayedDiagnostic &DD,
                                           Decl *Ctx) {
  assert(DD.Kind == DelayedDiagnostic::Availability &&
         "Expected an availability diagnostic here");

  DD.Triggered = true;
  DoEmitAvailabilityWarning(
      S, DD.getAvailabilityResult(), Ctx, DD.getAvailabilityReferringDecl(),
      DD.getAvailabilityOffendingDecl(), DD.getAvailabilityMessage(),
      DD.getAvailabilitySelectorLocs(), DD.getUnknownObjCClass(),
      DD.getObjCProperty(), false);
}

void Sema::PopParsingDeclaration(ParsingDeclState state, Decl *decl) {
  assert(DelayedDiagnostics.getCurrentPool());
  DelayedDiagnosticPool &poppedPool = *DelayedDiagnostics.getCurrentPool();
  DelayedDiagnostics.popWithoutEmitting(state);

  // When delaying diagnostics to run in the context of a parsed
  // declaration, we only want to actually emit anything if parsing
  // succeeds.
  if (!decl) return;

  // We emit all the active diagnostics in this pool or any of its
  // parents.  In general, we'll get one pool for the decl spec
  // and a child pool for each declarator; in a decl group like:
  //   deprecated_typedef foo, *bar, baz();
  // only the declarator pops will be passed decls.  This is correct;
  // we really do need to consider delayed diagnostics from the decl spec
  // for each of the different declarations.
  const DelayedDiagnosticPool *pool = &poppedPool;
  do {
    bool AnyAccessFailures = false;
    for (DelayedDiagnosticPool::pool_iterator
           i = pool->pool_begin(), e = pool->pool_end(); i != e; ++i) {
      // This const_cast is a bit lame.  Really, Triggered should be mutable.
      DelayedDiagnostic &diag = const_cast<DelayedDiagnostic&>(*i);
      if (diag.Triggered)
        continue;

      switch (diag.Kind) {
      case DelayedDiagnostic::Availability:
        // Don't bother giving deprecation/unavailable diagnostics if
        // the decl is invalid.
        if (!decl->isInvalidDecl())
          handleDelayedAvailabilityCheck(*this, diag, decl);
        break;

      case DelayedDiagnostic::Access:
        // Only produce one access control diagnostic for a structured binding
        // declaration: we don't need to tell the user that all the fields are
        // inaccessible one at a time.
        if (AnyAccessFailures && isa<DecompositionDecl>(decl))
          continue;
        HandleDelayedAccessCheck(diag, decl);
        if (diag.Triggered)
          AnyAccessFailures = true;
        break;

      case DelayedDiagnostic::ForbiddenType:
        handleDelayedForbiddenType(*this, diag, decl);
        break;
      }
    }
  } while ((pool = pool->getParent()));
}

/// Given a set of delayed diagnostics, re-emit them as if they had
/// been delayed in the current context instead of in the given pool.
/// Essentially, this just moves them to the current pool.
void Sema::redelayDiagnostics(DelayedDiagnosticPool &pool) {
  DelayedDiagnosticPool *curPool = DelayedDiagnostics.getCurrentPool();
  assert(curPool && "re-emitting in undelayed context not supported");
  curPool->steal(pool);
}

static void EmitAvailabilityWarning(Sema &S, AvailabilityResult AR,
                                    const NamedDecl *ReferringDecl,
                                    const NamedDecl *OffendingDecl,
                                    StringRef Message,
                                    ArrayRef<SourceLocation> Locs,
                                    const ObjCInterfaceDecl *UnknownObjCClass,
                                    const ObjCPropertyDecl *ObjCProperty,
                                    bool ObjCPropertyAccess) {
  // Delay if we're currently parsing a declaration.
  if (S.DelayedDiagnostics.shouldDelayDiagnostics()) {
    S.DelayedDiagnostics.add(
        DelayedDiagnostic::makeAvailability(
            AR, Locs, ReferringDecl, OffendingDecl, UnknownObjCClass,
            ObjCProperty, Message, ObjCPropertyAccess));
    return;
  }

  Decl *Ctx = cast<Decl>(S.getCurLexicalContext());
  DoEmitAvailabilityWarning(S, AR, Ctx, ReferringDecl, OffendingDecl,
                            Message, Locs, UnknownObjCClass, ObjCProperty,
                            ObjCPropertyAccess);
}

namespace {

/// Returns true if the given statement can be a body-like child of \p Parent.
bool isBodyLikeChildStmt(const Stmt *S, const Stmt *Parent) {
  switch (Parent->getStmtClass()) {
  case Stmt::IfStmtClass:
    return cast<IfStmt>(Parent)->getThen() == S ||
           cast<IfStmt>(Parent)->getElse() == S;
  case Stmt::WhileStmtClass:
    return cast<WhileStmt>(Parent)->getBody() == S;
  case Stmt::DoStmtClass:
    return cast<DoStmt>(Parent)->getBody() == S;
  case Stmt::ForStmtClass:
    return cast<ForStmt>(Parent)->getBody() == S;
  case Stmt::CXXForRangeStmtClass:
    return cast<CXXForRangeStmt>(Parent)->getBody() == S;
  case Stmt::ObjCForCollectionStmtClass:
    return cast<ObjCForCollectionStmt>(Parent)->getBody() == S;
  case Stmt::CaseStmtClass:
  case Stmt::DefaultStmtClass:
    return cast<SwitchCase>(Parent)->getSubStmt() == S;
  default:
    return false;
  }
}

class StmtUSEFinder : public RecursiveASTVisitor<StmtUSEFinder> {
  const Stmt *Target;

public:
  bool VisitStmt(Stmt *S) { return S != Target; }

  /// Returns true if the given statement is present in the given declaration.
  static bool isContained(const Stmt *Target, const Decl *D) {
    StmtUSEFinder Visitor;
    Visitor.Target = Target;
    return !Visitor.TraverseDecl(const_cast<Decl *>(D));
  }
};

/// Traverses the AST and finds the last statement that used a given
/// declaration.
class LastDeclUSEFinder : public RecursiveASTVisitor<LastDeclUSEFinder> {
  const Decl *D;

public:
  bool VisitDeclRefExpr(DeclRefExpr *DRE) {
    if (DRE->getDecl() == D)
      return false;
    return true;
  }

  static const Stmt *findLastStmtThatUsesDecl(const Decl *D,
                                              const CompoundStmt *Scope) {
    LastDeclUSEFinder Visitor;
    Visitor.D = D;
    for (auto I = Scope->body_rbegin(), E = Scope->body_rend(); I != E; ++I) {
      const Stmt *S = *I;
      if (!Visitor.TraverseStmt(const_cast<Stmt *>(S)))
        return S;
    }
    return nullptr;
  }
};

/// This class implements -Wunguarded-availability.
///
/// This is done with a traversal of the AST of a function that makes reference
/// to a partially available declaration. Whenever we encounter an \c if of the
/// form: \c if(@available(...)), we use the version from the condition to visit
/// the then statement.
class DiagnoseUnguardedAvailability
    : public RecursiveASTVisitor<DiagnoseUnguardedAvailability> {
  typedef RecursiveASTVisitor<DiagnoseUnguardedAvailability> Base;

  Sema &SemaRef;
  Decl *Ctx;

  /// Stack of potentially nested 'if (@available(...))'s.
  SmallVector<VersionTuple, 8> AvailabilityStack;
  SmallVector<const Stmt *, 16> StmtStack;

  void DiagnoseDeclAvailability(NamedDecl *D, SourceRange Range,
                                ObjCInterfaceDecl *ClassReceiver = nullptr);

public:
  DiagnoseUnguardedAvailability(Sema &SemaRef, Decl *Ctx)
      : SemaRef(SemaRef), Ctx(Ctx) {
    AvailabilityStack.push_back(
        SemaRef.Context.getTargetInfo().getPlatformMinVersion());
  }

  bool TraverseDecl(Decl *D) {
    // Avoid visiting nested functions to prevent duplicate warnings.
    if (!D || isa<FunctionDecl>(D))
      return true;
    return Base::TraverseDecl(D);
  }

  bool TraverseStmt(Stmt *S) {
    if (!S)
      return true;
    StmtStack.push_back(S);
    bool Result = Base::TraverseStmt(S);
    StmtStack.pop_back();
    return Result;
  }

  void IssueDiagnostics(Stmt *S) { TraverseStmt(S); }

  bool TraverseIfStmt(IfStmt *If);

  bool TraverseLambdaExpr(LambdaExpr *E) { return true; }

  // for 'case X:' statements, don't bother looking at the 'X'; it can't lead
  // to any useful diagnostics.
  bool TraverseCaseStmt(CaseStmt *CS) { return TraverseStmt(CS->getSubStmt()); }

  bool VisitObjCPropertyRefExpr(ObjCPropertyRefExpr *PRE) {
    if (PRE->isClassReceiver())
      DiagnoseDeclAvailability(PRE->getClassReceiver(), PRE->getReceiverLocation());
    return true;
  }

  bool VisitObjCMessageExpr(ObjCMessageExpr *Msg) {
    if (ObjCMethodDecl *D = Msg->getMethodDecl()) {
      ObjCInterfaceDecl *ID = nullptr;
      QualType ReceiverTy = Msg->getClassReceiver();
      if (!ReceiverTy.isNull() && ReceiverTy->getAsObjCInterfaceType())
        ID = ReceiverTy->getAsObjCInterfaceType()->getInterface();

      DiagnoseDeclAvailability(
          D, SourceRange(Msg->getSelectorStartLoc(), Msg->getEndLoc()), ID);
    }
    return true;
  }

  bool VisitDeclRefExpr(DeclRefExpr *DRE) {
    DiagnoseDeclAvailability(DRE->getDecl(),
                             SourceRange(DRE->getBeginLoc(), DRE->getEndLoc()));
    return true;
  }

  bool VisitMemberExpr(MemberExpr *ME) {
    DiagnoseDeclAvailability(ME->getMemberDecl(),
                             SourceRange(ME->getBeginLoc(), ME->getEndLoc()));
    return true;
  }

  bool VisitObjCAvailabilityCheckExpr(ObjCAvailabilityCheckExpr *E) {
    SemaRef.Diag(E->getBeginLoc(), diag::warn_at_available_unchecked_use)
        << (!SemaRef.getLangOpts().ObjC);
    return true;
  }

  bool VisitTypeLoc(TypeLoc Ty);
};

void DiagnoseUnguardedAvailability::DiagnoseDeclAvailability(
    NamedDecl *D, SourceRange Range, ObjCInterfaceDecl *ReceiverClass) {
  AvailabilityResult Result;
  const NamedDecl *OffendingDecl;
  std::tie(Result, OffendingDecl) =
      ShouldDiagnoseAvailabilityOfDecl(SemaRef, D, nullptr, ReceiverClass);
  if (Result != AR_Available) {
    // All other diagnostic kinds have already been handled in
    // DiagnoseAvailabilityOfDecl.
    if (Result != AR_NotYetIntroduced)
      return;

    const AvailabilityAttr *AA =
      getAttrForPlatform(SemaRef.getASTContext(), OffendingDecl);
    VersionTuple Introduced = AA->getIntroduced();

    if (AvailabilityStack.back() >= Introduced)
      return;

    // If the context of this function is less available than D, we should not
    // emit a diagnostic.
    if (!ShouldDiagnoseAvailabilityInContext(SemaRef, Result, Introduced, Ctx,
                                             OffendingDecl))
      return;

    // We would like to emit the diagnostic even if -Wunguarded-availability is
    // not specified for deployment targets >= to iOS 11 or equivalent or
    // for declarations that were introduced in iOS 11 (macOS 10.13, ...) or
    // later.
    unsigned DiagKind =
        shouldDiagnoseAvailabilityByDefault(
            SemaRef.Context,
            SemaRef.Context.getTargetInfo().getPlatformMinVersion(), Introduced)
            ? diag::warn_unguarded_availability_new
            : diag::warn_unguarded_availability;

    std::string PlatformName = AvailabilityAttr::getPrettyPlatformName(
        SemaRef.getASTContext().getTargetInfo().getPlatformName());

    SemaRef.Diag(Range.getBegin(), DiagKind)
        << Range << D << PlatformName << Introduced.getAsString();

    SemaRef.Diag(OffendingDecl->getLocation(),
                 diag::note_partial_availability_specified_here)
        << OffendingDecl << PlatformName << Introduced.getAsString()
        << SemaRef.Context.getTargetInfo()
               .getPlatformMinVersion()
               .getAsString();

    auto FixitDiag =
        SemaRef.Diag(Range.getBegin(), diag::note_unguarded_available_silence)
        << Range << D
        << (SemaRef.getLangOpts().ObjC ? /*@available*/ 0
                                       : /*__builtin_available*/ 1);

    // Find the statement which should be enclosed in the if @available check.
    if (StmtStack.empty())
      return;
    const Stmt *StmtOfUse = StmtStack.back();
    const CompoundStmt *Scope = nullptr;
    for (const Stmt *S : llvm::reverse(StmtStack)) {
      if (const auto *CS = dyn_cast<CompoundStmt>(S)) {
        Scope = CS;
        break;
      }
      if (isBodyLikeChildStmt(StmtOfUse, S)) {
        // The declaration won't be seen outside of the statement, so we don't
        // have to wrap the uses of any declared variables in if (@available).
        // Therefore we can avoid setting Scope here.
        break;
      }
      StmtOfUse = S;
    }
    const Stmt *LastStmtOfUse = nullptr;
    if (isa<DeclStmt>(StmtOfUse) && Scope) {
      for (const Decl *D : cast<DeclStmt>(StmtOfUse)->decls()) {
        if (StmtUSEFinder::isContained(StmtStack.back(), D)) {
          LastStmtOfUse = LastDeclUSEFinder::findLastStmtThatUsesDecl(D, Scope);
          break;
        }
      }
    }

    const SourceManager &SM = SemaRef.getSourceManager();
    SourceLocation IfInsertionLoc =
        SM.getExpansionLoc(StmtOfUse->getBeginLoc());
    SourceLocation StmtEndLoc =
        SM.getExpansionRange(
              (LastStmtOfUse ? LastStmtOfUse : StmtOfUse)->getEndLoc())
            .getEnd();
    if (SM.getFileID(IfInsertionLoc) != SM.getFileID(StmtEndLoc))
      return;

    StringRef Indentation = Lexer::getIndentationForLine(IfInsertionLoc, SM);
    const char *ExtraIndentation = "    ";
    std::string FixItString;
    llvm::raw_string_ostream FixItOS(FixItString);
    FixItOS << "if (" << (SemaRef.getLangOpts().ObjC ? "@available"
                                                     : "__builtin_available")
            << "("
            << AvailabilityAttr::getPlatformNameSourceSpelling(
                   SemaRef.getASTContext().getTargetInfo().getPlatformName())
            << " " << Introduced.getAsString() << ", *)) {\n"
            << Indentation << ExtraIndentation;
    FixitDiag << FixItHint::CreateInsertion(IfInsertionLoc, FixItOS.str());
    SourceLocation ElseInsertionLoc = Lexer::findLocationAfterToken(
        StmtEndLoc, tok::semi, SM, SemaRef.getLangOpts(),
        /*SkipTrailingWhitespaceAndNewLine=*/false);
    if (ElseInsertionLoc.isInvalid())
      ElseInsertionLoc =
          Lexer::getLocForEndOfToken(StmtEndLoc, 0, SM, SemaRef.getLangOpts());
    FixItOS.str().clear();
    FixItOS << "\n"
            << Indentation << "} else {\n"
            << Indentation << ExtraIndentation
            << "// Fallback on earlier versions\n"
            << Indentation << "}";
    FixitDiag << FixItHint::CreateInsertion(ElseInsertionLoc, FixItOS.str());
  }
}

bool DiagnoseUnguardedAvailability::VisitTypeLoc(TypeLoc Ty) {
  const Type *TyPtr = Ty.getTypePtr();
  SourceRange Range{Ty.getBeginLoc(), Ty.getEndLoc()};

  if (Range.isInvalid())
    return true;

  if (const auto *TT = dyn_cast<TagType>(TyPtr)) {
    TagDecl *TD = TT->getDecl();
    DiagnoseDeclAvailability(TD, Range);

  } else if (const auto *TD = dyn_cast<TypedefType>(TyPtr)) {
    TypedefNameDecl *D = TD->getDecl();
    DiagnoseDeclAvailability(D, Range);

  } else if (const auto *ObjCO = dyn_cast<ObjCObjectType>(TyPtr)) {
    if (NamedDecl *D = ObjCO->getInterface())
      DiagnoseDeclAvailability(D, Range);
  }

  return true;
}

bool DiagnoseUnguardedAvailability::TraverseIfStmt(IfStmt *If) {
  VersionTuple CondVersion;
  if (auto *E = dyn_cast<ObjCAvailabilityCheckExpr>(If->getCond())) {
    CondVersion = E->getVersion();

    // If we're using the '*' case here or if this check is redundant, then we
    // use the enclosing version to check both branches.
    if (CondVersion.empty() || CondVersion <= AvailabilityStack.back())
      return TraverseStmt(If->getThen()) && TraverseStmt(If->getElse());
  } else {
    // This isn't an availability checking 'if', we can just continue.
    return Base::TraverseIfStmt(If);
  }

  AvailabilityStack.push_back(CondVersion);
  bool ShouldContinue = TraverseStmt(If->getThen());
  AvailabilityStack.pop_back();

  return ShouldContinue && TraverseStmt(If->getElse());
}

} // end anonymous namespace

void Sema::DiagnoseUnguardedAvailabilityViolations(Decl *D) {
  Stmt *Body = nullptr;

  if (auto *FD = D->getAsFunction()) {
    // FIXME: We only examine the pattern decl for availability violations now,
    // but we should also examine instantiated templates.
    if (FD->isTemplateInstantiation())
      return;

    Body = FD->getBody();
  } else if (auto *MD = dyn_cast<ObjCMethodDecl>(D))
    Body = MD->getBody();
  else if (auto *BD = dyn_cast<BlockDecl>(D))
    Body = BD->getBody();

  assert(Body && "Need a body here!");

  DiagnoseUnguardedAvailability(*this, D).IssueDiagnostics(Body);
}

void Sema::DiagnoseAvailabilityOfDecl(NamedDecl *D,
                                      ArrayRef<SourceLocation> Locs,
                                      const ObjCInterfaceDecl *UnknownObjCClass,
                                      bool ObjCPropertyAccess,
                                      bool AvoidPartialAvailabilityChecks,
                                      ObjCInterfaceDecl *ClassReceiver) {
  std::string Message;
  AvailabilityResult Result;
  const NamedDecl* OffendingDecl;
  // See if this declaration is unavailable, deprecated, or partial.
  std::tie(Result, OffendingDecl) =
      ShouldDiagnoseAvailabilityOfDecl(*this, D, &Message, ClassReceiver);
  if (Result == AR_Available)
    return;

  if (Result == AR_NotYetIntroduced) {
    if (AvoidPartialAvailabilityChecks)
      return;

    // We need to know the @available context in the current function to
    // diagnose this use, let DiagnoseUnguardedAvailabilityViolations do that
    // when we're done parsing the current function.
    if (getCurFunctionOrMethodDecl()) {
      getEnclosingFunction()->HasPotentialAvailabilityViolations = true;
      return;
    } else if (getCurBlock() || getCurLambda()) {
      getCurFunction()->HasPotentialAvailabilityViolations = true;
      return;
    }
  }

  const ObjCPropertyDecl *ObjCPDecl = nullptr;
  if (const auto *MD = dyn_cast<ObjCMethodDecl>(D)) {
    if (const ObjCPropertyDecl *PD = MD->findPropertyDecl()) {
      AvailabilityResult PDeclResult = PD->getAvailability(nullptr);
      if (PDeclResult == Result)
        ObjCPDecl = PD;
    }
  }

  EmitAvailabilityWarning(*this, Result, D, OffendingDecl, Message, Locs,
                          UnknownObjCClass, ObjCPDecl, ObjCPropertyAccess);
}
