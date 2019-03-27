//===- SemaTemplateDeduction.cpp - Template Argument Deduction ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements C++ template argument deduction.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/TemplateDeduction.h"
#include "TreeTransform.h"
#include "TypeLocBuilder.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclAccessPair.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/TemplateName.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/AST/UnresolvedSet.h"
#include "clang/Basic/AddressSpaces.h"
#include "clang/Basic/ExceptionSpecificationType.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/PartialDiagnostic.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Sema/Ownership.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/Template.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <cassert>
#include <tuple>
#include <utility>

namespace clang {

  /// Various flags that control template argument deduction.
  ///
  /// These flags can be bitwise-OR'd together.
  enum TemplateDeductionFlags {
    /// No template argument deduction flags, which indicates the
    /// strictest results for template argument deduction (as used for, e.g.,
    /// matching class template partial specializations).
    TDF_None = 0,

    /// Within template argument deduction from a function call, we are
    /// matching with a parameter type for which the original parameter was
    /// a reference.
    TDF_ParamWithReferenceType = 0x1,

    /// Within template argument deduction from a function call, we
    /// are matching in a case where we ignore cv-qualifiers.
    TDF_IgnoreQualifiers = 0x02,

    /// Within template argument deduction from a function call,
    /// we are matching in a case where we can perform template argument
    /// deduction from a template-id of a derived class of the argument type.
    TDF_DerivedClass = 0x04,

    /// Allow non-dependent types to differ, e.g., when performing
    /// template argument deduction from a function call where conversions
    /// may apply.
    TDF_SkipNonDependent = 0x08,

    /// Whether we are performing template argument deduction for
    /// parameters and arguments in a top-level template argument
    TDF_TopLevelParameterTypeList = 0x10,

    /// Within template argument deduction from overload resolution per
    /// C++ [over.over] allow matching function types that are compatible in
    /// terms of noreturn and default calling convention adjustments, or
    /// similarly matching a declared template specialization against a
    /// possible template, per C++ [temp.deduct.decl]. In either case, permit
    /// deduction where the parameter is a function type that can be converted
    /// to the argument type.
    TDF_AllowCompatibleFunctionType = 0x20,

    /// Within template argument deduction for a conversion function, we are
    /// matching with an argument type for which the original argument was
    /// a reference.
    TDF_ArgWithReferenceType = 0x40,
  };
}

using namespace clang;
using namespace sema;

/// Compare two APSInts, extending and switching the sign as
/// necessary to compare their values regardless of underlying type.
static bool hasSameExtendedValue(llvm::APSInt X, llvm::APSInt Y) {
  if (Y.getBitWidth() > X.getBitWidth())
    X = X.extend(Y.getBitWidth());
  else if (Y.getBitWidth() < X.getBitWidth())
    Y = Y.extend(X.getBitWidth());

  // If there is a signedness mismatch, correct it.
  if (X.isSigned() != Y.isSigned()) {
    // If the signed value is negative, then the values cannot be the same.
    if ((Y.isSigned() && Y.isNegative()) || (X.isSigned() && X.isNegative()))
      return false;

    Y.setIsSigned(true);
    X.setIsSigned(true);
  }

  return X == Y;
}

static Sema::TemplateDeductionResult
DeduceTemplateArguments(Sema &S,
                        TemplateParameterList *TemplateParams,
                        const TemplateArgument &Param,
                        TemplateArgument Arg,
                        TemplateDeductionInfo &Info,
                        SmallVectorImpl<DeducedTemplateArgument> &Deduced);

static Sema::TemplateDeductionResult
DeduceTemplateArgumentsByTypeMatch(Sema &S,
                                   TemplateParameterList *TemplateParams,
                                   QualType Param,
                                   QualType Arg,
                                   TemplateDeductionInfo &Info,
                                   SmallVectorImpl<DeducedTemplateArgument> &
                                                      Deduced,
                                   unsigned TDF,
                                   bool PartialOrdering = false,
                                   bool DeducedFromArrayBound = false);

static Sema::TemplateDeductionResult
DeduceTemplateArguments(Sema &S, TemplateParameterList *TemplateParams,
                        ArrayRef<TemplateArgument> Params,
                        ArrayRef<TemplateArgument> Args,
                        TemplateDeductionInfo &Info,
                        SmallVectorImpl<DeducedTemplateArgument> &Deduced,
                        bool NumberOfArgumentsMustMatch);

static void MarkUsedTemplateParameters(ASTContext &Ctx,
                                       const TemplateArgument &TemplateArg,
                                       bool OnlyDeduced, unsigned Depth,
                                       llvm::SmallBitVector &Used);

static void MarkUsedTemplateParameters(ASTContext &Ctx, QualType T,
                                       bool OnlyDeduced, unsigned Level,
                                       llvm::SmallBitVector &Deduced);

/// If the given expression is of a form that permits the deduction
/// of a non-type template parameter, return the declaration of that
/// non-type template parameter.
static NonTypeTemplateParmDecl *
getDeducedParameterFromExpr(TemplateDeductionInfo &Info, Expr *E) {
  // If we are within an alias template, the expression may have undergone
  // any number of parameter substitutions already.
  while (true) {
    if (ImplicitCastExpr *IC = dyn_cast<ImplicitCastExpr>(E))
      E = IC->getSubExpr();
    else if (ConstantExpr *CE = dyn_cast<ConstantExpr>(E))
      E = CE->getSubExpr();
    else if (SubstNonTypeTemplateParmExpr *Subst =
               dyn_cast<SubstNonTypeTemplateParmExpr>(E))
      E = Subst->getReplacement();
    else
      break;
  }

  if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E))
    if (auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(DRE->getDecl()))
      if (NTTP->getDepth() == Info.getDeducedDepth())
        return NTTP;

  return nullptr;
}

/// Determine whether two declaration pointers refer to the same
/// declaration.
static bool isSameDeclaration(Decl *X, Decl *Y) {
  if (NamedDecl *NX = dyn_cast<NamedDecl>(X))
    X = NX->getUnderlyingDecl();
  if (NamedDecl *NY = dyn_cast<NamedDecl>(Y))
    Y = NY->getUnderlyingDecl();

  return X->getCanonicalDecl() == Y->getCanonicalDecl();
}

/// Verify that the given, deduced template arguments are compatible.
///
/// \returns The deduced template argument, or a NULL template argument if
/// the deduced template arguments were incompatible.
static DeducedTemplateArgument
checkDeducedTemplateArguments(ASTContext &Context,
                              const DeducedTemplateArgument &X,
                              const DeducedTemplateArgument &Y) {
  // We have no deduction for one or both of the arguments; they're compatible.
  if (X.isNull())
    return Y;
  if (Y.isNull())
    return X;

  // If we have two non-type template argument values deduced for the same
  // parameter, they must both match the type of the parameter, and thus must
  // match each other's type. As we're only keeping one of them, we must check
  // for that now. The exception is that if either was deduced from an array
  // bound, the type is permitted to differ.
  if (!X.wasDeducedFromArrayBound() && !Y.wasDeducedFromArrayBound()) {
    QualType XType = X.getNonTypeTemplateArgumentType();
    if (!XType.isNull()) {
      QualType YType = Y.getNonTypeTemplateArgumentType();
      if (YType.isNull() || !Context.hasSameType(XType, YType))
        return DeducedTemplateArgument();
    }
  }

  switch (X.getKind()) {
  case TemplateArgument::Null:
    llvm_unreachable("Non-deduced template arguments handled above");

  case TemplateArgument::Type:
    // If two template type arguments have the same type, they're compatible.
    if (Y.getKind() == TemplateArgument::Type &&
        Context.hasSameType(X.getAsType(), Y.getAsType()))
      return X;

    // If one of the two arguments was deduced from an array bound, the other
    // supersedes it.
    if (X.wasDeducedFromArrayBound() != Y.wasDeducedFromArrayBound())
      return X.wasDeducedFromArrayBound() ? Y : X;

    // The arguments are not compatible.
    return DeducedTemplateArgument();

  case TemplateArgument::Integral:
    // If we deduced a constant in one case and either a dependent expression or
    // declaration in another case, keep the integral constant.
    // If both are integral constants with the same value, keep that value.
    if (Y.getKind() == TemplateArgument::Expression ||
        Y.getKind() == TemplateArgument::Declaration ||
        (Y.getKind() == TemplateArgument::Integral &&
         hasSameExtendedValue(X.getAsIntegral(), Y.getAsIntegral())))
      return X.wasDeducedFromArrayBound() ? Y : X;

    // All other combinations are incompatible.
    return DeducedTemplateArgument();

  case TemplateArgument::Template:
    if (Y.getKind() == TemplateArgument::Template &&
        Context.hasSameTemplateName(X.getAsTemplate(), Y.getAsTemplate()))
      return X;

    // All other combinations are incompatible.
    return DeducedTemplateArgument();

  case TemplateArgument::TemplateExpansion:
    if (Y.getKind() == TemplateArgument::TemplateExpansion &&
        Context.hasSameTemplateName(X.getAsTemplateOrTemplatePattern(),
                                    Y.getAsTemplateOrTemplatePattern()))
      return X;

    // All other combinations are incompatible.
    return DeducedTemplateArgument();

  case TemplateArgument::Expression: {
    if (Y.getKind() != TemplateArgument::Expression)
      return checkDeducedTemplateArguments(Context, Y, X);

    // Compare the expressions for equality
    llvm::FoldingSetNodeID ID1, ID2;
    X.getAsExpr()->Profile(ID1, Context, true);
    Y.getAsExpr()->Profile(ID2, Context, true);
    if (ID1 == ID2)
      return X.wasDeducedFromArrayBound() ? Y : X;

    // Differing dependent expressions are incompatible.
    return DeducedTemplateArgument();
  }

  case TemplateArgument::Declaration:
    assert(!X.wasDeducedFromArrayBound());

    // If we deduced a declaration and a dependent expression, keep the
    // declaration.
    if (Y.getKind() == TemplateArgument::Expression)
      return X;

    // If we deduced a declaration and an integral constant, keep the
    // integral constant and whichever type did not come from an array
    // bound.
    if (Y.getKind() == TemplateArgument::Integral) {
      if (Y.wasDeducedFromArrayBound())
        return TemplateArgument(Context, Y.getAsIntegral(),
                                X.getParamTypeForDecl());
      return Y;
    }

    // If we deduced two declarations, make sure that they refer to the
    // same declaration.
    if (Y.getKind() == TemplateArgument::Declaration &&
        isSameDeclaration(X.getAsDecl(), Y.getAsDecl()))
      return X;

    // All other combinations are incompatible.
    return DeducedTemplateArgument();

  case TemplateArgument::NullPtr:
    // If we deduced a null pointer and a dependent expression, keep the
    // null pointer.
    if (Y.getKind() == TemplateArgument::Expression)
      return X;

    // If we deduced a null pointer and an integral constant, keep the
    // integral constant.
    if (Y.getKind() == TemplateArgument::Integral)
      return Y;

    // If we deduced two null pointers, they are the same.
    if (Y.getKind() == TemplateArgument::NullPtr)
      return X;

    // All other combinations are incompatible.
    return DeducedTemplateArgument();

  case TemplateArgument::Pack: {
    if (Y.getKind() != TemplateArgument::Pack ||
        X.pack_size() != Y.pack_size())
      return DeducedTemplateArgument();

    llvm::SmallVector<TemplateArgument, 8> NewPack;
    for (TemplateArgument::pack_iterator XA = X.pack_begin(),
                                      XAEnd = X.pack_end(),
                                         YA = Y.pack_begin();
         XA != XAEnd; ++XA, ++YA) {
      TemplateArgument Merged = checkDeducedTemplateArguments(
          Context, DeducedTemplateArgument(*XA, X.wasDeducedFromArrayBound()),
          DeducedTemplateArgument(*YA, Y.wasDeducedFromArrayBound()));
      if (Merged.isNull())
        return DeducedTemplateArgument();
      NewPack.push_back(Merged);
    }

    return DeducedTemplateArgument(
        TemplateArgument::CreatePackCopy(Context, NewPack),
        X.wasDeducedFromArrayBound() && Y.wasDeducedFromArrayBound());
  }
  }

  llvm_unreachable("Invalid TemplateArgument Kind!");
}

/// Deduce the value of the given non-type template parameter
/// as the given deduced template argument. All non-type template parameter
/// deduction is funneled through here.
static Sema::TemplateDeductionResult DeduceNonTypeTemplateArgument(
    Sema &S, TemplateParameterList *TemplateParams,
    NonTypeTemplateParmDecl *NTTP, const DeducedTemplateArgument &NewDeduced,
    QualType ValueType, TemplateDeductionInfo &Info,
    SmallVectorImpl<DeducedTemplateArgument> &Deduced) {
  assert(NTTP->getDepth() == Info.getDeducedDepth() &&
         "deducing non-type template argument with wrong depth");

  DeducedTemplateArgument Result = checkDeducedTemplateArguments(
      S.Context, Deduced[NTTP->getIndex()], NewDeduced);
  if (Result.isNull()) {
    Info.Param = NTTP;
    Info.FirstArg = Deduced[NTTP->getIndex()];
    Info.SecondArg = NewDeduced;
    return Sema::TDK_Inconsistent;
  }

  Deduced[NTTP->getIndex()] = Result;
  if (!S.getLangOpts().CPlusPlus17)
    return Sema::TDK_Success;

  if (NTTP->isExpandedParameterPack())
    // FIXME: We may still need to deduce parts of the type here! But we
    // don't have any way to find which slice of the type to use, and the
    // type stored on the NTTP itself is nonsense. Perhaps the type of an
    // expanded NTTP should be a pack expansion type?
    return Sema::TDK_Success;

  // Get the type of the parameter for deduction. If it's a (dependent) array
  // or function type, we will not have decayed it yet, so do that now.
  QualType ParamType = S.Context.getAdjustedParameterType(NTTP->getType());
  if (auto *Expansion = dyn_cast<PackExpansionType>(ParamType))
    ParamType = Expansion->getPattern();

  // FIXME: It's not clear how deduction of a parameter of reference
  // type from an argument (of non-reference type) should be performed.
  // For now, we just remove reference types from both sides and let
  // the final check for matching types sort out the mess.
  return DeduceTemplateArgumentsByTypeMatch(
      S, TemplateParams, ParamType.getNonReferenceType(),
      ValueType.getNonReferenceType(), Info, Deduced, TDF_SkipNonDependent,
      /*PartialOrdering=*/false,
      /*ArrayBound=*/NewDeduced.wasDeducedFromArrayBound());
}

/// Deduce the value of the given non-type template parameter
/// from the given integral constant.
static Sema::TemplateDeductionResult DeduceNonTypeTemplateArgument(
    Sema &S, TemplateParameterList *TemplateParams,
    NonTypeTemplateParmDecl *NTTP, const llvm::APSInt &Value,
    QualType ValueType, bool DeducedFromArrayBound, TemplateDeductionInfo &Info,
    SmallVectorImpl<DeducedTemplateArgument> &Deduced) {
  return DeduceNonTypeTemplateArgument(
      S, TemplateParams, NTTP,
      DeducedTemplateArgument(S.Context, Value, ValueType,
                              DeducedFromArrayBound),
      ValueType, Info, Deduced);
}

/// Deduce the value of the given non-type template parameter
/// from the given null pointer template argument type.
static Sema::TemplateDeductionResult DeduceNullPtrTemplateArgument(
    Sema &S, TemplateParameterList *TemplateParams,
    NonTypeTemplateParmDecl *NTTP, QualType NullPtrType,
    TemplateDeductionInfo &Info,
    SmallVectorImpl<DeducedTemplateArgument> &Deduced) {
  Expr *Value =
      S.ImpCastExprToType(new (S.Context) CXXNullPtrLiteralExpr(
                              S.Context.NullPtrTy, NTTP->getLocation()),
                          NullPtrType, CK_NullToPointer)
          .get();
  return DeduceNonTypeTemplateArgument(S, TemplateParams, NTTP,
                                       DeducedTemplateArgument(Value),
                                       Value->getType(), Info, Deduced);
}

/// Deduce the value of the given non-type template parameter
/// from the given type- or value-dependent expression.
///
/// \returns true if deduction succeeded, false otherwise.
static Sema::TemplateDeductionResult DeduceNonTypeTemplateArgument(
    Sema &S, TemplateParameterList *TemplateParams,
    NonTypeTemplateParmDecl *NTTP, Expr *Value, TemplateDeductionInfo &Info,
    SmallVectorImpl<DeducedTemplateArgument> &Deduced) {
  return DeduceNonTypeTemplateArgument(S, TemplateParams, NTTP,
                                       DeducedTemplateArgument(Value),
                                       Value->getType(), Info, Deduced);
}

/// Deduce the value of the given non-type template parameter
/// from the given declaration.
///
/// \returns true if deduction succeeded, false otherwise.
static Sema::TemplateDeductionResult DeduceNonTypeTemplateArgument(
    Sema &S, TemplateParameterList *TemplateParams,
    NonTypeTemplateParmDecl *NTTP, ValueDecl *D, QualType T,
    TemplateDeductionInfo &Info,
    SmallVectorImpl<DeducedTemplateArgument> &Deduced) {
  D = D ? cast<ValueDecl>(D->getCanonicalDecl()) : nullptr;
  TemplateArgument New(D, T);
  return DeduceNonTypeTemplateArgument(
      S, TemplateParams, NTTP, DeducedTemplateArgument(New), T, Info, Deduced);
}

static Sema::TemplateDeductionResult
DeduceTemplateArguments(Sema &S,
                        TemplateParameterList *TemplateParams,
                        TemplateName Param,
                        TemplateName Arg,
                        TemplateDeductionInfo &Info,
                        SmallVectorImpl<DeducedTemplateArgument> &Deduced) {
  TemplateDecl *ParamDecl = Param.getAsTemplateDecl();
  if (!ParamDecl) {
    // The parameter type is dependent and is not a template template parameter,
    // so there is nothing that we can deduce.
    return Sema::TDK_Success;
  }

  if (TemplateTemplateParmDecl *TempParam
        = dyn_cast<TemplateTemplateParmDecl>(ParamDecl)) {
    // If we're not deducing at this depth, there's nothing to deduce.
    if (TempParam->getDepth() != Info.getDeducedDepth())
      return Sema::TDK_Success;

    DeducedTemplateArgument NewDeduced(S.Context.getCanonicalTemplateName(Arg));
    DeducedTemplateArgument Result = checkDeducedTemplateArguments(S.Context,
                                                 Deduced[TempParam->getIndex()],
                                                                   NewDeduced);
    if (Result.isNull()) {
      Info.Param = TempParam;
      Info.FirstArg = Deduced[TempParam->getIndex()];
      Info.SecondArg = NewDeduced;
      return Sema::TDK_Inconsistent;
    }

    Deduced[TempParam->getIndex()] = Result;
    return Sema::TDK_Success;
  }

  // Verify that the two template names are equivalent.
  if (S.Context.hasSameTemplateName(Param, Arg))
    return Sema::TDK_Success;

  // Mismatch of non-dependent template parameter to argument.
  Info.FirstArg = TemplateArgument(Param);
  Info.SecondArg = TemplateArgument(Arg);
  return Sema::TDK_NonDeducedMismatch;
}

/// Deduce the template arguments by comparing the template parameter
/// type (which is a template-id) with the template argument type.
///
/// \param S the Sema
///
/// \param TemplateParams the template parameters that we are deducing
///
/// \param Param the parameter type
///
/// \param Arg the argument type
///
/// \param Info information about the template argument deduction itself
///
/// \param Deduced the deduced template arguments
///
/// \returns the result of template argument deduction so far. Note that a
/// "success" result means that template argument deduction has not yet failed,
/// but it may still fail, later, for other reasons.
static Sema::TemplateDeductionResult
DeduceTemplateArguments(Sema &S,
                        TemplateParameterList *TemplateParams,
                        const TemplateSpecializationType *Param,
                        QualType Arg,
                        TemplateDeductionInfo &Info,
                        SmallVectorImpl<DeducedTemplateArgument> &Deduced) {
  assert(Arg.isCanonical() && "Argument type must be canonical");

  // Treat an injected-class-name as its underlying template-id.
  if (auto *Injected = dyn_cast<InjectedClassNameType>(Arg))
    Arg = Injected->getInjectedSpecializationType();

  // Check whether the template argument is a dependent template-id.
  if (const TemplateSpecializationType *SpecArg
        = dyn_cast<TemplateSpecializationType>(Arg)) {
    // Perform template argument deduction for the template name.
    if (Sema::TemplateDeductionResult Result
          = DeduceTemplateArguments(S, TemplateParams,
                                    Param->getTemplateName(),
                                    SpecArg->getTemplateName(),
                                    Info, Deduced))
      return Result;


    // Perform template argument deduction on each template
    // argument. Ignore any missing/extra arguments, since they could be
    // filled in by default arguments.
    return DeduceTemplateArguments(S, TemplateParams,
                                   Param->template_arguments(),
                                   SpecArg->template_arguments(), Info, Deduced,
                                   /*NumberOfArgumentsMustMatch=*/false);
  }

  // If the argument type is a class template specialization, we
  // perform template argument deduction using its template
  // arguments.
  const RecordType *RecordArg = dyn_cast<RecordType>(Arg);
  if (!RecordArg) {
    Info.FirstArg = TemplateArgument(QualType(Param, 0));
    Info.SecondArg = TemplateArgument(Arg);
    return Sema::TDK_NonDeducedMismatch;
  }

  ClassTemplateSpecializationDecl *SpecArg
    = dyn_cast<ClassTemplateSpecializationDecl>(RecordArg->getDecl());
  if (!SpecArg) {
    Info.FirstArg = TemplateArgument(QualType(Param, 0));
    Info.SecondArg = TemplateArgument(Arg);
    return Sema::TDK_NonDeducedMismatch;
  }

  // Perform template argument deduction for the template name.
  if (Sema::TemplateDeductionResult Result
        = DeduceTemplateArguments(S,
                                  TemplateParams,
                                  Param->getTemplateName(),
                               TemplateName(SpecArg->getSpecializedTemplate()),
                                  Info, Deduced))
    return Result;

  // Perform template argument deduction for the template arguments.
  return DeduceTemplateArguments(S, TemplateParams, Param->template_arguments(),
                                 SpecArg->getTemplateArgs().asArray(), Info,
                                 Deduced, /*NumberOfArgumentsMustMatch=*/true);
}

/// Determines whether the given type is an opaque type that
/// might be more qualified when instantiated.
static bool IsPossiblyOpaquelyQualifiedType(QualType T) {
  switch (T->getTypeClass()) {
  case Type::TypeOfExpr:
  case Type::TypeOf:
  case Type::DependentName:
  case Type::Decltype:
  case Type::UnresolvedUsing:
  case Type::TemplateTypeParm:
    return true;

  case Type::ConstantArray:
  case Type::IncompleteArray:
  case Type::VariableArray:
  case Type::DependentSizedArray:
    return IsPossiblyOpaquelyQualifiedType(
                                      cast<ArrayType>(T)->getElementType());

  default:
    return false;
  }
}

/// Helper function to build a TemplateParameter when we don't
/// know its type statically.
static TemplateParameter makeTemplateParameter(Decl *D) {
  if (TemplateTypeParmDecl *TTP = dyn_cast<TemplateTypeParmDecl>(D))
    return TemplateParameter(TTP);
  if (NonTypeTemplateParmDecl *NTTP = dyn_cast<NonTypeTemplateParmDecl>(D))
    return TemplateParameter(NTTP);

  return TemplateParameter(cast<TemplateTemplateParmDecl>(D));
}

/// If \p Param is an expanded parameter pack, get the number of expansions.
static Optional<unsigned> getExpandedPackSize(NamedDecl *Param) {
  if (auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(Param))
    if (NTTP->isExpandedParameterPack())
      return NTTP->getNumExpansionTypes();

  if (auto *TTP = dyn_cast<TemplateTemplateParmDecl>(Param))
    if (TTP->isExpandedParameterPack())
      return TTP->getNumExpansionTemplateParameters();

  return None;
}

/// A pack that we're currently deducing.
struct clang::DeducedPack {
  // The index of the pack.
  unsigned Index;

  // The old value of the pack before we started deducing it.
  DeducedTemplateArgument Saved;

  // A deferred value of this pack from an inner deduction, that couldn't be
  // deduced because this deduction hadn't happened yet.
  DeducedTemplateArgument DeferredDeduction;

  // The new value of the pack.
  SmallVector<DeducedTemplateArgument, 4> New;

  // The outer deduction for this pack, if any.
  DeducedPack *Outer = nullptr;

  DeducedPack(unsigned Index) : Index(Index) {}
};

namespace {

/// A scope in which we're performing pack deduction.
class PackDeductionScope {
public:
  /// Prepare to deduce the packs named within Pattern.
  PackDeductionScope(Sema &S, TemplateParameterList *TemplateParams,
                     SmallVectorImpl<DeducedTemplateArgument> &Deduced,
                     TemplateDeductionInfo &Info, TemplateArgument Pattern)
      : S(S), TemplateParams(TemplateParams), Deduced(Deduced), Info(Info) {
    unsigned NumNamedPacks = addPacks(Pattern);
    finishConstruction(NumNamedPacks);
  }

  /// Prepare to directly deduce arguments of the parameter with index \p Index.
  PackDeductionScope(Sema &S, TemplateParameterList *TemplateParams,
                     SmallVectorImpl<DeducedTemplateArgument> &Deduced,
                     TemplateDeductionInfo &Info, unsigned Index)
      : S(S), TemplateParams(TemplateParams), Deduced(Deduced), Info(Info) {
    addPack(Index);
    finishConstruction(1);
  }

private:
  void addPack(unsigned Index) {
    // Save the deduced template argument for the parameter pack expanded
    // by this pack expansion, then clear out the deduction.
    DeducedPack Pack(Index);
    Pack.Saved = Deduced[Index];
    Deduced[Index] = TemplateArgument();

    // FIXME: What if we encounter multiple packs with different numbers of
    // pre-expanded expansions? (This should already have been diagnosed
    // during substitution.)
    if (Optional<unsigned> ExpandedPackExpansions =
            getExpandedPackSize(TemplateParams->getParam(Index)))
      FixedNumExpansions = ExpandedPackExpansions;

    Packs.push_back(Pack);
  }

  unsigned addPacks(TemplateArgument Pattern) {
    // Compute the set of template parameter indices that correspond to
    // parameter packs expanded by the pack expansion.
    llvm::SmallBitVector SawIndices(TemplateParams->size());

    auto AddPack = [&](unsigned Index) {
      if (SawIndices[Index])
        return;
      SawIndices[Index] = true;
      addPack(Index);
    };

    // First look for unexpanded packs in the pattern.
    SmallVector<UnexpandedParameterPack, 2> Unexpanded;
    S.collectUnexpandedParameterPacks(Pattern, Unexpanded);
    for (unsigned I = 0, N = Unexpanded.size(); I != N; ++I) {
      unsigned Depth, Index;
      std::tie(Depth, Index) = getDepthAndIndex(Unexpanded[I]);
      if (Depth == Info.getDeducedDepth())
        AddPack(Index);
    }
    assert(!Packs.empty() && "Pack expansion without unexpanded packs?");

    unsigned NumNamedPacks = Packs.size();

    // We can also have deduced template parameters that do not actually
    // appear in the pattern, but can be deduced by it (the type of a non-type
    // template parameter pack, in particular). These won't have prevented us
    // from partially expanding the pack.
    llvm::SmallBitVector Used(TemplateParams->size());
    MarkUsedTemplateParameters(S.Context, Pattern, /*OnlyDeduced*/true,
                               Info.getDeducedDepth(), Used);
    for (int Index = Used.find_first(); Index != -1;
         Index = Used.find_next(Index))
      if (TemplateParams->getParam(Index)->isParameterPack())
        AddPack(Index);

    return NumNamedPacks;
  }

  void finishConstruction(unsigned NumNamedPacks) {
    // Dig out the partially-substituted pack, if there is one.
    const TemplateArgument *PartialPackArgs = nullptr;
    unsigned NumPartialPackArgs = 0;
    std::pair<unsigned, unsigned> PartialPackDepthIndex(-1u, -1u);
    if (auto *Scope = S.CurrentInstantiationScope)
      if (auto *Partial = Scope->getPartiallySubstitutedPack(
              &PartialPackArgs, &NumPartialPackArgs))
        PartialPackDepthIndex = getDepthAndIndex(Partial);

    // This pack expansion will have been partially or fully expanded if
    // it only names explicitly-specified parameter packs (including the
    // partially-substituted one, if any).
    bool IsExpanded = true;
    for (unsigned I = 0; I != NumNamedPacks; ++I) {
      if (Packs[I].Index >= Info.getNumExplicitArgs()) {
        IsExpanded = false;
        IsPartiallyExpanded = false;
        break;
      }
      if (PartialPackDepthIndex ==
            std::make_pair(Info.getDeducedDepth(), Packs[I].Index)) {
        IsPartiallyExpanded = true;
      }
    }

    // Skip over the pack elements that were expanded into separate arguments.
    // If we partially expanded, this is the number of partial arguments.
    if (IsPartiallyExpanded)
      PackElements += NumPartialPackArgs;
    else if (IsExpanded)
      PackElements += *FixedNumExpansions;

    for (auto &Pack : Packs) {
      if (Info.PendingDeducedPacks.size() > Pack.Index)
        Pack.Outer = Info.PendingDeducedPacks[Pack.Index];
      else
        Info.PendingDeducedPacks.resize(Pack.Index + 1);
      Info.PendingDeducedPacks[Pack.Index] = &Pack;

      if (PartialPackDepthIndex ==
            std::make_pair(Info.getDeducedDepth(), Pack.Index)) {
        Pack.New.append(PartialPackArgs, PartialPackArgs + NumPartialPackArgs);
        // We pre-populate the deduced value of the partially-substituted
        // pack with the specified value. This is not entirely correct: the
        // value is supposed to have been substituted, not deduced, but the
        // cases where this is observable require an exact type match anyway.
        //
        // FIXME: If we could represent a "depth i, index j, pack elem k"
        // parameter, we could substitute the partially-substituted pack
        // everywhere and avoid this.
        if (!IsPartiallyExpanded)
          Deduced[Pack.Index] = Pack.New[PackElements];
      }
    }
  }

public:
  ~PackDeductionScope() {
    for (auto &Pack : Packs)
      Info.PendingDeducedPacks[Pack.Index] = Pack.Outer;
  }

  /// Determine whether this pack has already been partially expanded into a
  /// sequence of (prior) function parameters / template arguments.
  bool isPartiallyExpanded() { return IsPartiallyExpanded; }

  /// Determine whether this pack expansion scope has a known, fixed arity.
  /// This happens if it involves a pack from an outer template that has
  /// (notionally) already been expanded.
  bool hasFixedArity() { return FixedNumExpansions.hasValue(); }

  /// Determine whether the next element of the argument is still part of this
  /// pack. This is the case unless the pack is already expanded to a fixed
  /// length.
  bool hasNextElement() {
    return !FixedNumExpansions || *FixedNumExpansions > PackElements;
  }

  /// Move to deducing the next element in each pack that is being deduced.
  void nextPackElement() {
    // Capture the deduced template arguments for each parameter pack expanded
    // by this pack expansion, add them to the list of arguments we've deduced
    // for that pack, then clear out the deduced argument.
    for (auto &Pack : Packs) {
      DeducedTemplateArgument &DeducedArg = Deduced[Pack.Index];
      if (!Pack.New.empty() || !DeducedArg.isNull()) {
        while (Pack.New.size() < PackElements)
          Pack.New.push_back(DeducedTemplateArgument());
        if (Pack.New.size() == PackElements)
          Pack.New.push_back(DeducedArg);
        else
          Pack.New[PackElements] = DeducedArg;
        DeducedArg = Pack.New.size() > PackElements + 1
                         ? Pack.New[PackElements + 1]
                         : DeducedTemplateArgument();
      }
    }
    ++PackElements;
  }

  /// Finish template argument deduction for a set of argument packs,
  /// producing the argument packs and checking for consistency with prior
  /// deductions.
  Sema::TemplateDeductionResult
  finish(bool TreatNoDeductionsAsNonDeduced = true) {
    // Build argument packs for each of the parameter packs expanded by this
    // pack expansion.
    for (auto &Pack : Packs) {
      // Put back the old value for this pack.
      Deduced[Pack.Index] = Pack.Saved;

      // If we are deducing the size of this pack even if we didn't deduce any
      // values for it, then make sure we build a pack of the right size.
      // FIXME: Should we always deduce the size, even if the pack appears in
      // a non-deduced context?
      if (!TreatNoDeductionsAsNonDeduced)
        Pack.New.resize(PackElements);

      // Build or find a new value for this pack.
      DeducedTemplateArgument NewPack;
      if (PackElements && Pack.New.empty()) {
        if (Pack.DeferredDeduction.isNull()) {
          // We were not able to deduce anything for this parameter pack
          // (because it only appeared in non-deduced contexts), so just
          // restore the saved argument pack.
          continue;
        }

        NewPack = Pack.DeferredDeduction;
        Pack.DeferredDeduction = TemplateArgument();
      } else if (Pack.New.empty()) {
        // If we deduced an empty argument pack, create it now.
        NewPack = DeducedTemplateArgument(TemplateArgument::getEmptyPack());
      } else {
        TemplateArgument *ArgumentPack =
            new (S.Context) TemplateArgument[Pack.New.size()];
        std::copy(Pack.New.begin(), Pack.New.end(), ArgumentPack);
        NewPack = DeducedTemplateArgument(
            TemplateArgument(llvm::makeArrayRef(ArgumentPack, Pack.New.size())),
            // FIXME: This is wrong, it's possible that some pack elements are
            // deduced from an array bound and others are not:
            //   template<typename ...T, T ...V> void g(const T (&...p)[V]);
            //   g({1, 2, 3}, {{}, {}});
            // ... should deduce T = {int, size_t (from array bound)}.
            Pack.New[0].wasDeducedFromArrayBound());
      }

      // Pick where we're going to put the merged pack.
      DeducedTemplateArgument *Loc;
      if (Pack.Outer) {
        if (Pack.Outer->DeferredDeduction.isNull()) {
          // Defer checking this pack until we have a complete pack to compare
          // it against.
          Pack.Outer->DeferredDeduction = NewPack;
          continue;
        }
        Loc = &Pack.Outer->DeferredDeduction;
      } else {
        Loc = &Deduced[Pack.Index];
      }

      // Check the new pack matches any previous value.
      DeducedTemplateArgument OldPack = *Loc;
      DeducedTemplateArgument Result =
          checkDeducedTemplateArguments(S.Context, OldPack, NewPack);

      // If we deferred a deduction of this pack, check that one now too.
      if (!Result.isNull() && !Pack.DeferredDeduction.isNull()) {
        OldPack = Result;
        NewPack = Pack.DeferredDeduction;
        Result = checkDeducedTemplateArguments(S.Context, OldPack, NewPack);
      }

      NamedDecl *Param = TemplateParams->getParam(Pack.Index);
      if (Result.isNull()) {
        Info.Param = makeTemplateParameter(Param);
        Info.FirstArg = OldPack;
        Info.SecondArg = NewPack;
        return Sema::TDK_Inconsistent;
      }

      // If we have a pre-expanded pack and we didn't deduce enough elements
      // for it, fail deduction.
      if (Optional<unsigned> Expansions = getExpandedPackSize(Param)) {
        if (*Expansions != PackElements) {
          Info.Param = makeTemplateParameter(Param);
          Info.FirstArg = Result;
          return Sema::TDK_IncompletePack;
        }
      }

      *Loc = Result;
    }

    return Sema::TDK_Success;
  }

private:
  Sema &S;
  TemplateParameterList *TemplateParams;
  SmallVectorImpl<DeducedTemplateArgument> &Deduced;
  TemplateDeductionInfo &Info;
  unsigned PackElements = 0;
  bool IsPartiallyExpanded = false;
  /// The number of expansions, if we have a fully-expanded pack in this scope.
  Optional<unsigned> FixedNumExpansions;

  SmallVector<DeducedPack, 2> Packs;
};

} // namespace

/// Deduce the template arguments by comparing the list of parameter
/// types to the list of argument types, as in the parameter-type-lists of
/// function types (C++ [temp.deduct.type]p10).
///
/// \param S The semantic analysis object within which we are deducing
///
/// \param TemplateParams The template parameters that we are deducing
///
/// \param Params The list of parameter types
///
/// \param NumParams The number of types in \c Params
///
/// \param Args The list of argument types
///
/// \param NumArgs The number of types in \c Args
///
/// \param Info information about the template argument deduction itself
///
/// \param Deduced the deduced template arguments
///
/// \param TDF bitwise OR of the TemplateDeductionFlags bits that describe
/// how template argument deduction is performed.
///
/// \param PartialOrdering If true, we are performing template argument
/// deduction for during partial ordering for a call
/// (C++0x [temp.deduct.partial]).
///
/// \returns the result of template argument deduction so far. Note that a
/// "success" result means that template argument deduction has not yet failed,
/// but it may still fail, later, for other reasons.
static Sema::TemplateDeductionResult
DeduceTemplateArguments(Sema &S,
                        TemplateParameterList *TemplateParams,
                        const QualType *Params, unsigned NumParams,
                        const QualType *Args, unsigned NumArgs,
                        TemplateDeductionInfo &Info,
                        SmallVectorImpl<DeducedTemplateArgument> &Deduced,
                        unsigned TDF,
                        bool PartialOrdering = false) {
  // C++0x [temp.deduct.type]p10:
  //   Similarly, if P has a form that contains (T), then each parameter type
  //   Pi of the respective parameter-type- list of P is compared with the
  //   corresponding parameter type Ai of the corresponding parameter-type-list
  //   of A. [...]
  unsigned ArgIdx = 0, ParamIdx = 0;
  for (; ParamIdx != NumParams; ++ParamIdx) {
    // Check argument types.
    const PackExpansionType *Expansion
                                = dyn_cast<PackExpansionType>(Params[ParamIdx]);
    if (!Expansion) {
      // Simple case: compare the parameter and argument types at this point.

      // Make sure we have an argument.
      if (ArgIdx >= NumArgs)
        return Sema::TDK_MiscellaneousDeductionFailure;

      if (isa<PackExpansionType>(Args[ArgIdx])) {
        // C++0x [temp.deduct.type]p22:
        //   If the original function parameter associated with A is a function
        //   parameter pack and the function parameter associated with P is not
        //   a function parameter pack, then template argument deduction fails.
        return Sema::TDK_MiscellaneousDeductionFailure;
      }

      if (Sema::TemplateDeductionResult Result
            = DeduceTemplateArgumentsByTypeMatch(S, TemplateParams,
                                                 Params[ParamIdx], Args[ArgIdx],
                                                 Info, Deduced, TDF,
                                                 PartialOrdering))
        return Result;

      ++ArgIdx;
      continue;
    }

    // C++0x [temp.deduct.type]p10:
    //   If the parameter-declaration corresponding to Pi is a function
    //   parameter pack, then the type of its declarator- id is compared with
    //   each remaining parameter type in the parameter-type-list of A. Each
    //   comparison deduces template arguments for subsequent positions in the
    //   template parameter packs expanded by the function parameter pack.

    QualType Pattern = Expansion->getPattern();
    PackDeductionScope PackScope(S, TemplateParams, Deduced, Info, Pattern);

    // A pack scope with fixed arity is not really a pack any more, so is not
    // a non-deduced context.
    if (ParamIdx + 1 == NumParams || PackScope.hasFixedArity()) {
      for (; ArgIdx < NumArgs && PackScope.hasNextElement(); ++ArgIdx) {
        // Deduce template arguments from the pattern.
        if (Sema::TemplateDeductionResult Result
              = DeduceTemplateArgumentsByTypeMatch(S, TemplateParams, Pattern,
                                                   Args[ArgIdx], Info, Deduced,
                                                   TDF, PartialOrdering))
          return Result;

        PackScope.nextPackElement();
      }
    } else {
      // C++0x [temp.deduct.type]p5:
      //   The non-deduced contexts are:
      //     - A function parameter pack that does not occur at the end of the
      //       parameter-declaration-clause.
      //
      // FIXME: There is no wording to say what we should do in this case. We
      // choose to resolve this by applying the same rule that is applied for a
      // function call: that is, deduce all contained packs to their
      // explicitly-specified values (or to <> if there is no such value).
      //
      // This is seemingly-arbitrarily different from the case of a template-id
      // with a non-trailing pack-expansion in its arguments, which renders the
      // entire template-argument-list a non-deduced context.

      // If the parameter type contains an explicitly-specified pack that we
      // could not expand, skip the number of parameters notionally created
      // by the expansion.
      Optional<unsigned> NumExpansions = Expansion->getNumExpansions();
      if (NumExpansions && !PackScope.isPartiallyExpanded()) {
        for (unsigned I = 0; I != *NumExpansions && ArgIdx < NumArgs;
             ++I, ++ArgIdx)
          PackScope.nextPackElement();
      }
    }

    // Build argument packs for each of the parameter packs expanded by this
    // pack expansion.
    if (auto Result = PackScope.finish())
      return Result;
  }

  // Make sure we don't have any extra arguments.
  if (ArgIdx < NumArgs)
    return Sema::TDK_MiscellaneousDeductionFailure;

  return Sema::TDK_Success;
}

/// Determine whether the parameter has qualifiers that the argument
/// lacks. Put another way, determine whether there is no way to add
/// a deduced set of qualifiers to the ParamType that would result in
/// its qualifiers matching those of the ArgType.
static bool hasInconsistentOrSupersetQualifiersOf(QualType ParamType,
                                                  QualType ArgType) {
  Qualifiers ParamQs = ParamType.getQualifiers();
  Qualifiers ArgQs = ArgType.getQualifiers();

  if (ParamQs == ArgQs)
    return false;

  // Mismatched (but not missing) Objective-C GC attributes.
  if (ParamQs.getObjCGCAttr() != ArgQs.getObjCGCAttr() &&
      ParamQs.hasObjCGCAttr())
    return true;

  // Mismatched (but not missing) address spaces.
  if (ParamQs.getAddressSpace() != ArgQs.getAddressSpace() &&
      ParamQs.hasAddressSpace())
    return true;

  // Mismatched (but not missing) Objective-C lifetime qualifiers.
  if (ParamQs.getObjCLifetime() != ArgQs.getObjCLifetime() &&
      ParamQs.hasObjCLifetime())
    return true;

  // CVR qualifiers inconsistent or a superset.
  return (ParamQs.getCVRQualifiers() & ~ArgQs.getCVRQualifiers()) != 0;
}

/// Compare types for equality with respect to possibly compatible
/// function types (noreturn adjustment, implicit calling conventions). If any
/// of parameter and argument is not a function, just perform type comparison.
///
/// \param Param the template parameter type.
///
/// \param Arg the argument type.
bool Sema::isSameOrCompatibleFunctionType(CanQualType Param,
                                          CanQualType Arg) {
  const FunctionType *ParamFunction = Param->getAs<FunctionType>(),
                     *ArgFunction   = Arg->getAs<FunctionType>();

  // Just compare if not functions.
  if (!ParamFunction || !ArgFunction)
    return Param == Arg;

  // Noreturn and noexcept adjustment.
  QualType AdjustedParam;
  if (IsFunctionConversion(Param, Arg, AdjustedParam))
    return Arg == Context.getCanonicalType(AdjustedParam);

  // FIXME: Compatible calling conventions.

  return Param == Arg;
}

/// Get the index of the first template parameter that was originally from the
/// innermost template-parameter-list. This is 0 except when we concatenate
/// the template parameter lists of a class template and a constructor template
/// when forming an implicit deduction guide.
static unsigned getFirstInnerIndex(FunctionTemplateDecl *FTD) {
  auto *Guide = dyn_cast<CXXDeductionGuideDecl>(FTD->getTemplatedDecl());
  if (!Guide || !Guide->isImplicit())
    return 0;
  return Guide->getDeducedTemplate()->getTemplateParameters()->size();
}

/// Determine whether a type denotes a forwarding reference.
static bool isForwardingReference(QualType Param, unsigned FirstInnerIndex) {
  // C++1z [temp.deduct.call]p3:
  //   A forwarding reference is an rvalue reference to a cv-unqualified
  //   template parameter that does not represent a template parameter of a
  //   class template.
  if (auto *ParamRef = Param->getAs<RValueReferenceType>()) {
    if (ParamRef->getPointeeType().getQualifiers())
      return false;
    auto *TypeParm = ParamRef->getPointeeType()->getAs<TemplateTypeParmType>();
    return TypeParm && TypeParm->getIndex() >= FirstInnerIndex;
  }
  return false;
}

/// Deduce the template arguments by comparing the parameter type and
/// the argument type (C++ [temp.deduct.type]).
///
/// \param S the semantic analysis object within which we are deducing
///
/// \param TemplateParams the template parameters that we are deducing
///
/// \param ParamIn the parameter type
///
/// \param ArgIn the argument type
///
/// \param Info information about the template argument deduction itself
///
/// \param Deduced the deduced template arguments
///
/// \param TDF bitwise OR of the TemplateDeductionFlags bits that describe
/// how template argument deduction is performed.
///
/// \param PartialOrdering Whether we're performing template argument deduction
/// in the context of partial ordering (C++0x [temp.deduct.partial]).
///
/// \returns the result of template argument deduction so far. Note that a
/// "success" result means that template argument deduction has not yet failed,
/// but it may still fail, later, for other reasons.
static Sema::TemplateDeductionResult
DeduceTemplateArgumentsByTypeMatch(Sema &S,
                                   TemplateParameterList *TemplateParams,
                                   QualType ParamIn, QualType ArgIn,
                                   TemplateDeductionInfo &Info,
                            SmallVectorImpl<DeducedTemplateArgument> &Deduced,
                                   unsigned TDF,
                                   bool PartialOrdering,
                                   bool DeducedFromArrayBound) {
  // We only want to look at the canonical types, since typedefs and
  // sugar are not part of template argument deduction.
  QualType Param = S.Context.getCanonicalType(ParamIn);
  QualType Arg = S.Context.getCanonicalType(ArgIn);

  // If the argument type is a pack expansion, look at its pattern.
  // This isn't explicitly called out
  if (const PackExpansionType *ArgExpansion
                                            = dyn_cast<PackExpansionType>(Arg))
    Arg = ArgExpansion->getPattern();

  if (PartialOrdering) {
    // C++11 [temp.deduct.partial]p5:
    //   Before the partial ordering is done, certain transformations are
    //   performed on the types used for partial ordering:
    //     - If P is a reference type, P is replaced by the type referred to.
    const ReferenceType *ParamRef = Param->getAs<ReferenceType>();
    if (ParamRef)
      Param = ParamRef->getPointeeType();

    //     - If A is a reference type, A is replaced by the type referred to.
    const ReferenceType *ArgRef = Arg->getAs<ReferenceType>();
    if (ArgRef)
      Arg = ArgRef->getPointeeType();

    if (ParamRef && ArgRef && S.Context.hasSameUnqualifiedType(Param, Arg)) {
      // C++11 [temp.deduct.partial]p9:
      //   If, for a given type, deduction succeeds in both directions (i.e.,
      //   the types are identical after the transformations above) and both
      //   P and A were reference types [...]:
      //     - if [one type] was an lvalue reference and [the other type] was
      //       not, [the other type] is not considered to be at least as
      //       specialized as [the first type]
      //     - if [one type] is more cv-qualified than [the other type],
      //       [the other type] is not considered to be at least as specialized
      //       as [the first type]
      // Objective-C ARC adds:
      //     - [one type] has non-trivial lifetime, [the other type] has
      //       __unsafe_unretained lifetime, and the types are otherwise
      //       identical
      //
      // A is "considered to be at least as specialized" as P iff deduction
      // succeeds, so we model this as a deduction failure. Note that
      // [the first type] is P and [the other type] is A here; the standard
      // gets this backwards.
      Qualifiers ParamQuals = Param.getQualifiers();
      Qualifiers ArgQuals = Arg.getQualifiers();
      if ((ParamRef->isLValueReferenceType() &&
           !ArgRef->isLValueReferenceType()) ||
          ParamQuals.isStrictSupersetOf(ArgQuals) ||
          (ParamQuals.hasNonTrivialObjCLifetime() &&
           ArgQuals.getObjCLifetime() == Qualifiers::OCL_ExplicitNone &&
           ParamQuals.withoutObjCLifetime() ==
               ArgQuals.withoutObjCLifetime())) {
        Info.FirstArg = TemplateArgument(ParamIn);
        Info.SecondArg = TemplateArgument(ArgIn);
        return Sema::TDK_NonDeducedMismatch;
      }
    }

    // C++11 [temp.deduct.partial]p7:
    //   Remove any top-level cv-qualifiers:
    //     - If P is a cv-qualified type, P is replaced by the cv-unqualified
    //       version of P.
    Param = Param.getUnqualifiedType();
    //     - If A is a cv-qualified type, A is replaced by the cv-unqualified
    //       version of A.
    Arg = Arg.getUnqualifiedType();
  } else {
    // C++0x [temp.deduct.call]p4 bullet 1:
    //   - If the original P is a reference type, the deduced A (i.e., the type
    //     referred to by the reference) can be more cv-qualified than the
    //     transformed A.
    if (TDF & TDF_ParamWithReferenceType) {
      Qualifiers Quals;
      QualType UnqualParam = S.Context.getUnqualifiedArrayType(Param, Quals);
      Quals.setCVRQualifiers(Quals.getCVRQualifiers() &
                             Arg.getCVRQualifiers());
      Param = S.Context.getQualifiedType(UnqualParam, Quals);
    }

    if ((TDF & TDF_TopLevelParameterTypeList) && !Param->isFunctionType()) {
      // C++0x [temp.deduct.type]p10:
      //   If P and A are function types that originated from deduction when
      //   taking the address of a function template (14.8.2.2) or when deducing
      //   template arguments from a function declaration (14.8.2.6) and Pi and
      //   Ai are parameters of the top-level parameter-type-list of P and A,
      //   respectively, Pi is adjusted if it is a forwarding reference and Ai
      //   is an lvalue reference, in
      //   which case the type of Pi is changed to be the template parameter
      //   type (i.e., T&& is changed to simply T). [ Note: As a result, when
      //   Pi is T&& and Ai is X&, the adjusted Pi will be T, causing T to be
      //   deduced as X&. - end note ]
      TDF &= ~TDF_TopLevelParameterTypeList;
      if (isForwardingReference(Param, 0) && Arg->isLValueReferenceType())
        Param = Param->getPointeeType();
    }
  }

  // C++ [temp.deduct.type]p9:
  //   A template type argument T, a template template argument TT or a
  //   template non-type argument i can be deduced if P and A have one of
  //   the following forms:
  //
  //     T
  //     cv-list T
  if (const TemplateTypeParmType *TemplateTypeParm
        = Param->getAs<TemplateTypeParmType>()) {
    // Just skip any attempts to deduce from a placeholder type or a parameter
    // at a different depth.
    if (Arg->isPlaceholderType() ||
        Info.getDeducedDepth() != TemplateTypeParm->getDepth())
      return Sema::TDK_Success;

    unsigned Index = TemplateTypeParm->getIndex();
    bool RecanonicalizeArg = false;

    // If the argument type is an array type, move the qualifiers up to the
    // top level, so they can be matched with the qualifiers on the parameter.
    if (isa<ArrayType>(Arg)) {
      Qualifiers Quals;
      Arg = S.Context.getUnqualifiedArrayType(Arg, Quals);
      if (Quals) {
        Arg = S.Context.getQualifiedType(Arg, Quals);
        RecanonicalizeArg = true;
      }
    }

    // The argument type can not be less qualified than the parameter
    // type.
    if (!(TDF & TDF_IgnoreQualifiers) &&
        hasInconsistentOrSupersetQualifiersOf(Param, Arg)) {
      Info.Param = cast<TemplateTypeParmDecl>(TemplateParams->getParam(Index));
      Info.FirstArg = TemplateArgument(Param);
      Info.SecondArg = TemplateArgument(Arg);
      return Sema::TDK_Underqualified;
    }

    // Do not match a function type with a cv-qualified type.
    // http://www.open-std.org/jtc1/sc22/wg21/docs/cwg_active.html#1584
    if (Arg->isFunctionType() && Param.hasQualifiers()) {
      return Sema::TDK_NonDeducedMismatch;
    }

    assert(TemplateTypeParm->getDepth() == Info.getDeducedDepth() &&
           "saw template type parameter with wrong depth");
    assert(Arg != S.Context.OverloadTy && "Unresolved overloaded function");
    QualType DeducedType = Arg;

    // Remove any qualifiers on the parameter from the deduced type.
    // We checked the qualifiers for consistency above.
    Qualifiers DeducedQs = DeducedType.getQualifiers();
    Qualifiers ParamQs = Param.getQualifiers();
    DeducedQs.removeCVRQualifiers(ParamQs.getCVRQualifiers());
    if (ParamQs.hasObjCGCAttr())
      DeducedQs.removeObjCGCAttr();
    if (ParamQs.hasAddressSpace())
      DeducedQs.removeAddressSpace();
    if (ParamQs.hasObjCLifetime())
      DeducedQs.removeObjCLifetime();

    // Objective-C ARC:
    //   If template deduction would produce a lifetime qualifier on a type
    //   that is not a lifetime type, template argument deduction fails.
    if (ParamQs.hasObjCLifetime() && !DeducedType->isObjCLifetimeType() &&
        !DeducedType->isDependentType()) {
      Info.Param = cast<TemplateTypeParmDecl>(TemplateParams->getParam(Index));
      Info.FirstArg = TemplateArgument(Param);
      Info.SecondArg = TemplateArgument(Arg);
      return Sema::TDK_Underqualified;
    }

    // Objective-C ARC:
    //   If template deduction would produce an argument type with lifetime type
    //   but no lifetime qualifier, the __strong lifetime qualifier is inferred.
    if (S.getLangOpts().ObjCAutoRefCount &&
        DeducedType->isObjCLifetimeType() &&
        !DeducedQs.hasObjCLifetime())
      DeducedQs.setObjCLifetime(Qualifiers::OCL_Strong);

    DeducedType = S.Context.getQualifiedType(DeducedType.getUnqualifiedType(),
                                             DeducedQs);

    if (RecanonicalizeArg)
      DeducedType = S.Context.getCanonicalType(DeducedType);

    DeducedTemplateArgument NewDeduced(DeducedType, DeducedFromArrayBound);
    DeducedTemplateArgument Result = checkDeducedTemplateArguments(S.Context,
                                                                 Deduced[Index],
                                                                   NewDeduced);
    if (Result.isNull()) {
      Info.Param = cast<TemplateTypeParmDecl>(TemplateParams->getParam(Index));
      Info.FirstArg = Deduced[Index];
      Info.SecondArg = NewDeduced;
      return Sema::TDK_Inconsistent;
    }

    Deduced[Index] = Result;
    return Sema::TDK_Success;
  }

  // Set up the template argument deduction information for a failure.
  Info.FirstArg = TemplateArgument(ParamIn);
  Info.SecondArg = TemplateArgument(ArgIn);

  // If the parameter is an already-substituted template parameter
  // pack, do nothing: we don't know which of its arguments to look
  // at, so we have to wait until all of the parameter packs in this
  // expansion have arguments.
  if (isa<SubstTemplateTypeParmPackType>(Param))
    return Sema::TDK_Success;

  // Check the cv-qualifiers on the parameter and argument types.
  CanQualType CanParam = S.Context.getCanonicalType(Param);
  CanQualType CanArg = S.Context.getCanonicalType(Arg);
  if (!(TDF & TDF_IgnoreQualifiers)) {
    if (TDF & TDF_ParamWithReferenceType) {
      if (hasInconsistentOrSupersetQualifiersOf(Param, Arg))
        return Sema::TDK_NonDeducedMismatch;
    } else if (TDF & TDF_ArgWithReferenceType) {
      // C++ [temp.deduct.conv]p4:
      //   If the original A is a reference type, A can be more cv-qualified
      //   than the deduced A
      if (!Arg.getQualifiers().compatiblyIncludes(Param.getQualifiers()))
        return Sema::TDK_NonDeducedMismatch;

      // Strip out all extra qualifiers from the argument to figure out the
      // type we're converting to, prior to the qualification conversion.
      Qualifiers Quals;
      Arg = S.Context.getUnqualifiedArrayType(Arg, Quals);
      Arg = S.Context.getQualifiedType(Arg, Param.getQualifiers());
    } else if (!IsPossiblyOpaquelyQualifiedType(Param)) {
      if (Param.getCVRQualifiers() != Arg.getCVRQualifiers())
        return Sema::TDK_NonDeducedMismatch;
    }

    // If the parameter type is not dependent, there is nothing to deduce.
    if (!Param->isDependentType()) {
      if (!(TDF & TDF_SkipNonDependent)) {
        bool NonDeduced =
            (TDF & TDF_AllowCompatibleFunctionType)
                ? !S.isSameOrCompatibleFunctionType(CanParam, CanArg)
                : Param != Arg;
        if (NonDeduced) {
          return Sema::TDK_NonDeducedMismatch;
        }
      }
      return Sema::TDK_Success;
    }
  } else if (!Param->isDependentType()) {
    CanQualType ParamUnqualType = CanParam.getUnqualifiedType(),
                ArgUnqualType = CanArg.getUnqualifiedType();
    bool Success =
        (TDF & TDF_AllowCompatibleFunctionType)
            ? S.isSameOrCompatibleFunctionType(ParamUnqualType, ArgUnqualType)
            : ParamUnqualType == ArgUnqualType;
    if (Success)
      return Sema::TDK_Success;
  }

  switch (Param->getTypeClass()) {
    // Non-canonical types cannot appear here.
#define NON_CANONICAL_TYPE(Class, Base) \
  case Type::Class: llvm_unreachable("deducing non-canonical type: " #Class);
#define TYPE(Class, Base)
#include "clang/AST/TypeNodes.def"

    case Type::TemplateTypeParm:
    case Type::SubstTemplateTypeParmPack:
      llvm_unreachable("Type nodes handled above");

    // These types cannot be dependent, so simply check whether the types are
    // the same.
    case Type::Builtin:
    case Type::VariableArray:
    case Type::Vector:
    case Type::FunctionNoProto:
    case Type::Record:
    case Type::Enum:
    case Type::ObjCObject:
    case Type::ObjCInterface:
    case Type::ObjCObjectPointer:
      if (TDF & TDF_SkipNonDependent)
        return Sema::TDK_Success;

      if (TDF & TDF_IgnoreQualifiers) {
        Param = Param.getUnqualifiedType();
        Arg = Arg.getUnqualifiedType();
      }

      return Param == Arg? Sema::TDK_Success : Sema::TDK_NonDeducedMismatch;

    //     _Complex T   [placeholder extension]
    case Type::Complex:
      if (const ComplexType *ComplexArg = Arg->getAs<ComplexType>())
        return DeduceTemplateArgumentsByTypeMatch(S, TemplateParams,
                                    cast<ComplexType>(Param)->getElementType(),
                                    ComplexArg->getElementType(),
                                    Info, Deduced, TDF);

      return Sema::TDK_NonDeducedMismatch;

    //     _Atomic T   [extension]
    case Type::Atomic:
      if (const AtomicType *AtomicArg = Arg->getAs<AtomicType>())
        return DeduceTemplateArgumentsByTypeMatch(S, TemplateParams,
                                       cast<AtomicType>(Param)->getValueType(),
                                       AtomicArg->getValueType(),
                                       Info, Deduced, TDF);

      return Sema::TDK_NonDeducedMismatch;

    //     T *
    case Type::Pointer: {
      QualType PointeeType;
      if (const PointerType *PointerArg = Arg->getAs<PointerType>()) {
        PointeeType = PointerArg->getPointeeType();
      } else if (const ObjCObjectPointerType *PointerArg
                   = Arg->getAs<ObjCObjectPointerType>()) {
        PointeeType = PointerArg->getPointeeType();
      } else {
        return Sema::TDK_NonDeducedMismatch;
      }

      unsigned SubTDF = TDF & (TDF_IgnoreQualifiers | TDF_DerivedClass);
      return DeduceTemplateArgumentsByTypeMatch(S, TemplateParams,
                                     cast<PointerType>(Param)->getPointeeType(),
                                     PointeeType,
                                     Info, Deduced, SubTDF);
    }

    //     T &
    case Type::LValueReference: {
      const LValueReferenceType *ReferenceArg =
          Arg->getAs<LValueReferenceType>();
      if (!ReferenceArg)
        return Sema::TDK_NonDeducedMismatch;

      return DeduceTemplateArgumentsByTypeMatch(S, TemplateParams,
                           cast<LValueReferenceType>(Param)->getPointeeType(),
                           ReferenceArg->getPointeeType(), Info, Deduced, 0);
    }

    //     T && [C++0x]
    case Type::RValueReference: {
      const RValueReferenceType *ReferenceArg =
          Arg->getAs<RValueReferenceType>();
      if (!ReferenceArg)
        return Sema::TDK_NonDeducedMismatch;

      return DeduceTemplateArgumentsByTypeMatch(S, TemplateParams,
                             cast<RValueReferenceType>(Param)->getPointeeType(),
                             ReferenceArg->getPointeeType(),
                             Info, Deduced, 0);
    }

    //     T [] (implied, but not stated explicitly)
    case Type::IncompleteArray: {
      const IncompleteArrayType *IncompleteArrayArg =
        S.Context.getAsIncompleteArrayType(Arg);
      if (!IncompleteArrayArg)
        return Sema::TDK_NonDeducedMismatch;

      unsigned SubTDF = TDF & TDF_IgnoreQualifiers;
      return DeduceTemplateArgumentsByTypeMatch(S, TemplateParams,
                    S.Context.getAsIncompleteArrayType(Param)->getElementType(),
                    IncompleteArrayArg->getElementType(),
                    Info, Deduced, SubTDF);
    }

    //     T [integer-constant]
    case Type::ConstantArray: {
      const ConstantArrayType *ConstantArrayArg =
        S.Context.getAsConstantArrayType(Arg);
      if (!ConstantArrayArg)
        return Sema::TDK_NonDeducedMismatch;

      const ConstantArrayType *ConstantArrayParm =
        S.Context.getAsConstantArrayType(Param);
      if (ConstantArrayArg->getSize() != ConstantArrayParm->getSize())
        return Sema::TDK_NonDeducedMismatch;

      unsigned SubTDF = TDF & TDF_IgnoreQualifiers;
      return DeduceTemplateArgumentsByTypeMatch(S, TemplateParams,
                                           ConstantArrayParm->getElementType(),
                                           ConstantArrayArg->getElementType(),
                                           Info, Deduced, SubTDF);
    }

    //     type [i]
    case Type::DependentSizedArray: {
      const ArrayType *ArrayArg = S.Context.getAsArrayType(Arg);
      if (!ArrayArg)
        return Sema::TDK_NonDeducedMismatch;

      unsigned SubTDF = TDF & TDF_IgnoreQualifiers;

      // Check the element type of the arrays
      const DependentSizedArrayType *DependentArrayParm
        = S.Context.getAsDependentSizedArrayType(Param);
      if (Sema::TemplateDeductionResult Result
            = DeduceTemplateArgumentsByTypeMatch(S, TemplateParams,
                                          DependentArrayParm->getElementType(),
                                          ArrayArg->getElementType(),
                                          Info, Deduced, SubTDF))
        return Result;

      // Determine the array bound is something we can deduce.
      NonTypeTemplateParmDecl *NTTP
        = getDeducedParameterFromExpr(Info, DependentArrayParm->getSizeExpr());
      if (!NTTP)
        return Sema::TDK_Success;

      // We can perform template argument deduction for the given non-type
      // template parameter.
      assert(NTTP->getDepth() == Info.getDeducedDepth() &&
             "saw non-type template parameter with wrong depth");
      if (const ConstantArrayType *ConstantArrayArg
            = dyn_cast<ConstantArrayType>(ArrayArg)) {
        llvm::APSInt Size(ConstantArrayArg->getSize());
        return DeduceNonTypeTemplateArgument(S, TemplateParams, NTTP, Size,
                                             S.Context.getSizeType(),
                                             /*ArrayBound=*/true,
                                             Info, Deduced);
      }
      if (const DependentSizedArrayType *DependentArrayArg
            = dyn_cast<DependentSizedArrayType>(ArrayArg))
        if (DependentArrayArg->getSizeExpr())
          return DeduceNonTypeTemplateArgument(S, TemplateParams, NTTP,
                                               DependentArrayArg->getSizeExpr(),
                                               Info, Deduced);

      // Incomplete type does not match a dependently-sized array type
      return Sema::TDK_NonDeducedMismatch;
    }

    //     type(*)(T)
    //     T(*)()
    //     T(*)(T)
    case Type::FunctionProto: {
      unsigned SubTDF = TDF & TDF_TopLevelParameterTypeList;
      const FunctionProtoType *FunctionProtoArg =
        dyn_cast<FunctionProtoType>(Arg);
      if (!FunctionProtoArg)
        return Sema::TDK_NonDeducedMismatch;

      const FunctionProtoType *FunctionProtoParam =
        cast<FunctionProtoType>(Param);

      if (FunctionProtoParam->getTypeQuals()
            != FunctionProtoArg->getTypeQuals() ||
          FunctionProtoParam->getRefQualifier()
            != FunctionProtoArg->getRefQualifier() ||
          FunctionProtoParam->isVariadic() != FunctionProtoArg->isVariadic())
        return Sema::TDK_NonDeducedMismatch;

      // Check return types.
      if (auto Result = DeduceTemplateArgumentsByTypeMatch(
              S, TemplateParams, FunctionProtoParam->getReturnType(),
              FunctionProtoArg->getReturnType(), Info, Deduced, 0))
        return Result;

      // Check parameter types.
      if (auto Result = DeduceTemplateArguments(
              S, TemplateParams, FunctionProtoParam->param_type_begin(),
              FunctionProtoParam->getNumParams(),
              FunctionProtoArg->param_type_begin(),
              FunctionProtoArg->getNumParams(), Info, Deduced, SubTDF))
        return Result;

      if (TDF & TDF_AllowCompatibleFunctionType)
        return Sema::TDK_Success;

      // FIXME: Per core-2016/10/1019 (no corresponding core issue yet), permit
      // deducing through the noexcept-specifier if it's part of the canonical
      // type. libstdc++ relies on this.
      Expr *NoexceptExpr = FunctionProtoParam->getNoexceptExpr();
      if (NonTypeTemplateParmDecl *NTTP =
          NoexceptExpr ? getDeducedParameterFromExpr(Info, NoexceptExpr)
                       : nullptr) {
        assert(NTTP->getDepth() == Info.getDeducedDepth() &&
               "saw non-type template parameter with wrong depth");

        llvm::APSInt Noexcept(1);
        switch (FunctionProtoArg->canThrow()) {
        case CT_Cannot:
          Noexcept = 1;
          LLVM_FALLTHROUGH;

        case CT_Can:
          // We give E in noexcept(E) the "deduced from array bound" treatment.
          // FIXME: Should we?
          return DeduceNonTypeTemplateArgument(
              S, TemplateParams, NTTP, Noexcept, S.Context.BoolTy,
              /*ArrayBound*/true, Info, Deduced);

        case CT_Dependent:
          if (Expr *ArgNoexceptExpr = FunctionProtoArg->getNoexceptExpr())
            return DeduceNonTypeTemplateArgument(
                S, TemplateParams, NTTP, ArgNoexceptExpr, Info, Deduced);
          // Can't deduce anything from throw(T...).
          break;
        }
      }
      // FIXME: Detect non-deduced exception specification mismatches?
      //
      // Careful about [temp.deduct.call] and [temp.deduct.conv], which allow
      // top-level differences in noexcept-specifications.

      return Sema::TDK_Success;
    }

    case Type::InjectedClassName:
      // Treat a template's injected-class-name as if the template
      // specialization type had been used.
      Param = cast<InjectedClassNameType>(Param)
        ->getInjectedSpecializationType();
      assert(isa<TemplateSpecializationType>(Param) &&
             "injected class name is not a template specialization type");
      LLVM_FALLTHROUGH;

    //     template-name<T> (where template-name refers to a class template)
    //     template-name<i>
    //     TT<T>
    //     TT<i>
    //     TT<>
    case Type::TemplateSpecialization: {
      const TemplateSpecializationType *SpecParam =
          cast<TemplateSpecializationType>(Param);

      // When Arg cannot be a derived class, we can just try to deduce template
      // arguments from the template-id.
      const RecordType *RecordT = Arg->getAs<RecordType>();
      if (!(TDF & TDF_DerivedClass) || !RecordT)
        return DeduceTemplateArguments(S, TemplateParams, SpecParam, Arg, Info,
                                       Deduced);

      SmallVector<DeducedTemplateArgument, 8> DeducedOrig(Deduced.begin(),
                                                          Deduced.end());

      Sema::TemplateDeductionResult Result = DeduceTemplateArguments(
          S, TemplateParams, SpecParam, Arg, Info, Deduced);

      if (Result == Sema::TDK_Success)
        return Result;

      // We cannot inspect base classes as part of deduction when the type
      // is incomplete, so either instantiate any templates necessary to
      // complete the type, or skip over it if it cannot be completed.
      if (!S.isCompleteType(Info.getLocation(), Arg))
        return Result;

      // C++14 [temp.deduct.call] p4b3:
      //   If P is a class and P has the form simple-template-id, then the
      //   transformed A can be a derived class of the deduced A. Likewise if
      //   P is a pointer to a class of the form simple-template-id, the
      //   transformed A can be a pointer to a derived class pointed to by the
      //   deduced A.
      //
      //   These alternatives are considered only if type deduction would
      //   otherwise fail. If they yield more than one possible deduced A, the
      //   type deduction fails.

      // Reset the incorrectly deduced argument from above.
      Deduced = DeducedOrig;

      // Use data recursion to crawl through the list of base classes.
      // Visited contains the set of nodes we have already visited, while
      // ToVisit is our stack of records that we still need to visit.
      llvm::SmallPtrSet<const RecordType *, 8> Visited;
      SmallVector<const RecordType *, 8> ToVisit;
      ToVisit.push_back(RecordT);
      bool Successful = false;
      SmallVector<DeducedTemplateArgument, 8> SuccessfulDeduced;
      while (!ToVisit.empty()) {
        // Retrieve the next class in the inheritance hierarchy.
        const RecordType *NextT = ToVisit.pop_back_val();

        // If we have already seen this type, skip it.
        if (!Visited.insert(NextT).second)
          continue;

        // If this is a base class, try to perform template argument
        // deduction from it.
        if (NextT != RecordT) {
          TemplateDeductionInfo BaseInfo(Info.getLocation());
          Sema::TemplateDeductionResult BaseResult =
              DeduceTemplateArguments(S, TemplateParams, SpecParam,
                                      QualType(NextT, 0), BaseInfo, Deduced);

          // If template argument deduction for this base was successful,
          // note that we had some success. Otherwise, ignore any deductions
          // from this base class.
          if (BaseResult == Sema::TDK_Success) {
            // If we've already seen some success, then deduction fails due to
            // an ambiguity (temp.deduct.call p5).
            if (Successful)
              return Sema::TDK_MiscellaneousDeductionFailure;

            Successful = true;
            std::swap(SuccessfulDeduced, Deduced);

            Info.Param = BaseInfo.Param;
            Info.FirstArg = BaseInfo.FirstArg;
            Info.SecondArg = BaseInfo.SecondArg;
          }

          Deduced = DeducedOrig;
        }

        // Visit base classes
        CXXRecordDecl *Next = cast<CXXRecordDecl>(NextT->getDecl());
        for (const auto &Base : Next->bases()) {
          assert(Base.getType()->isRecordType() &&
                 "Base class that isn't a record?");
          ToVisit.push_back(Base.getType()->getAs<RecordType>());
        }
      }

      if (Successful) {
        std::swap(SuccessfulDeduced, Deduced);
        return Sema::TDK_Success;
      }

      return Result;
    }

    //     T type::*
    //     T T::*
    //     T (type::*)()
    //     type (T::*)()
    //     type (type::*)(T)
    //     type (T::*)(T)
    //     T (type::*)(T)
    //     T (T::*)()
    //     T (T::*)(T)
    case Type::MemberPointer: {
      const MemberPointerType *MemPtrParam = cast<MemberPointerType>(Param);
      const MemberPointerType *MemPtrArg = dyn_cast<MemberPointerType>(Arg);
      if (!MemPtrArg)
        return Sema::TDK_NonDeducedMismatch;

      QualType ParamPointeeType = MemPtrParam->getPointeeType();
      if (ParamPointeeType->isFunctionType())
        S.adjustMemberFunctionCC(ParamPointeeType, /*IsStatic=*/true,
                                 /*IsCtorOrDtor=*/false, Info.getLocation());
      QualType ArgPointeeType = MemPtrArg->getPointeeType();
      if (ArgPointeeType->isFunctionType())
        S.adjustMemberFunctionCC(ArgPointeeType, /*IsStatic=*/true,
                                 /*IsCtorOrDtor=*/false, Info.getLocation());

      if (Sema::TemplateDeductionResult Result
            = DeduceTemplateArgumentsByTypeMatch(S, TemplateParams,
                                                 ParamPointeeType,
                                                 ArgPointeeType,
                                                 Info, Deduced,
                                                 TDF & TDF_IgnoreQualifiers))
        return Result;

      return DeduceTemplateArgumentsByTypeMatch(S, TemplateParams,
                                           QualType(MemPtrParam->getClass(), 0),
                                           QualType(MemPtrArg->getClass(), 0),
                                           Info, Deduced,
                                           TDF & TDF_IgnoreQualifiers);
    }

    //     (clang extension)
    //
    //     type(^)(T)
    //     T(^)()
    //     T(^)(T)
    case Type::BlockPointer: {
      const BlockPointerType *BlockPtrParam = cast<BlockPointerType>(Param);
      const BlockPointerType *BlockPtrArg = dyn_cast<BlockPointerType>(Arg);

      if (!BlockPtrArg)
        return Sema::TDK_NonDeducedMismatch;

      return DeduceTemplateArgumentsByTypeMatch(S, TemplateParams,
                                                BlockPtrParam->getPointeeType(),
                                                BlockPtrArg->getPointeeType(),
                                                Info, Deduced, 0);
    }

    //     (clang extension)
    //
    //     T __attribute__(((ext_vector_type(<integral constant>))))
    case Type::ExtVector: {
      const ExtVectorType *VectorParam = cast<ExtVectorType>(Param);
      if (const ExtVectorType *VectorArg = dyn_cast<ExtVectorType>(Arg)) {
        // Make sure that the vectors have the same number of elements.
        if (VectorParam->getNumElements() != VectorArg->getNumElements())
          return Sema::TDK_NonDeducedMismatch;

        // Perform deduction on the element types.
        return DeduceTemplateArgumentsByTypeMatch(S, TemplateParams,
                                                  VectorParam->getElementType(),
                                                  VectorArg->getElementType(),
                                                  Info, Deduced, TDF);
      }

      if (const DependentSizedExtVectorType *VectorArg
                                = dyn_cast<DependentSizedExtVectorType>(Arg)) {
        // We can't check the number of elements, since the argument has a
        // dependent number of elements. This can only occur during partial
        // ordering.

        // Perform deduction on the element types.
        return DeduceTemplateArgumentsByTypeMatch(S, TemplateParams,
                                                  VectorParam->getElementType(),
                                                  VectorArg->getElementType(),
                                                  Info, Deduced, TDF);
      }

      return Sema::TDK_NonDeducedMismatch;
    }

    case Type::DependentVector: {
      const auto *VectorParam = cast<DependentVectorType>(Param);

      if (const auto *VectorArg = dyn_cast<VectorType>(Arg)) {
        // Perform deduction on the element types.
        if (Sema::TemplateDeductionResult Result =
                DeduceTemplateArgumentsByTypeMatch(
                    S, TemplateParams, VectorParam->getElementType(),
                    VectorArg->getElementType(), Info, Deduced, TDF))
          return Result;

        // Perform deduction on the vector size, if we can.
        NonTypeTemplateParmDecl *NTTP =
            getDeducedParameterFromExpr(Info, VectorParam->getSizeExpr());
        if (!NTTP)
          return Sema::TDK_Success;

        llvm::APSInt ArgSize(S.Context.getTypeSize(S.Context.IntTy), false);
        ArgSize = VectorArg->getNumElements();
        // Note that we use the "array bound" rules here; just like in that
        // case, we don't have any particular type for the vector size, but
        // we can provide one if necessary.
        return DeduceNonTypeTemplateArgument(S, TemplateParams, NTTP, ArgSize,
                                             S.Context.UnsignedIntTy, true,
                                             Info, Deduced);
      }

      if (const auto *VectorArg = dyn_cast<DependentVectorType>(Arg)) {
        // Perform deduction on the element types.
        if (Sema::TemplateDeductionResult Result =
                DeduceTemplateArgumentsByTypeMatch(
                    S, TemplateParams, VectorParam->getElementType(),
                    VectorArg->getElementType(), Info, Deduced, TDF))
          return Result;

        // Perform deduction on the vector size, if we can.
        NonTypeTemplateParmDecl *NTTP = getDeducedParameterFromExpr(
            Info, VectorParam->getSizeExpr());
        if (!NTTP)
          return Sema::TDK_Success;

        return DeduceNonTypeTemplateArgument(
            S, TemplateParams, NTTP, VectorArg->getSizeExpr(), Info, Deduced);
      }

      return Sema::TDK_NonDeducedMismatch;
    }

    //     (clang extension)
    //
    //     T __attribute__(((ext_vector_type(N))))
    case Type::DependentSizedExtVector: {
      const DependentSizedExtVectorType *VectorParam
        = cast<DependentSizedExtVectorType>(Param);

      if (const ExtVectorType *VectorArg = dyn_cast<ExtVectorType>(Arg)) {
        // Perform deduction on the element types.
        if (Sema::TemplateDeductionResult Result
              = DeduceTemplateArgumentsByTypeMatch(S, TemplateParams,
                                                  VectorParam->getElementType(),
                                                   VectorArg->getElementType(),
                                                   Info, Deduced, TDF))
          return Result;

        // Perform deduction on the vector size, if we can.
        NonTypeTemplateParmDecl *NTTP
          = getDeducedParameterFromExpr(Info, VectorParam->getSizeExpr());
        if (!NTTP)
          return Sema::TDK_Success;

        llvm::APSInt ArgSize(S.Context.getTypeSize(S.Context.IntTy), false);
        ArgSize = VectorArg->getNumElements();
        // Note that we use the "array bound" rules here; just like in that
        // case, we don't have any particular type for the vector size, but
        // we can provide one if necessary.
        return DeduceNonTypeTemplateArgument(S, TemplateParams, NTTP, ArgSize,
                                             S.Context.IntTy, true, Info,
                                             Deduced);
      }

      if (const DependentSizedExtVectorType *VectorArg
                                = dyn_cast<DependentSizedExtVectorType>(Arg)) {
        // Perform deduction on the element types.
        if (Sema::TemplateDeductionResult Result
            = DeduceTemplateArgumentsByTypeMatch(S, TemplateParams,
                                                 VectorParam->getElementType(),
                                                 VectorArg->getElementType(),
                                                 Info, Deduced, TDF))
          return Result;

        // Perform deduction on the vector size, if we can.
        NonTypeTemplateParmDecl *NTTP
          = getDeducedParameterFromExpr(Info, VectorParam->getSizeExpr());
        if (!NTTP)
          return Sema::TDK_Success;

        return DeduceNonTypeTemplateArgument(S, TemplateParams, NTTP,
                                             VectorArg->getSizeExpr(),
                                             Info, Deduced);
      }

      return Sema::TDK_NonDeducedMismatch;
    }

    //     (clang extension)
    //
    //     T __attribute__(((address_space(N))))
    case Type::DependentAddressSpace: {
      const DependentAddressSpaceType *AddressSpaceParam =
          cast<DependentAddressSpaceType>(Param);

      if (const DependentAddressSpaceType *AddressSpaceArg =
              dyn_cast<DependentAddressSpaceType>(Arg)) {
        // Perform deduction on the pointer type.
        if (Sema::TemplateDeductionResult Result =
                DeduceTemplateArgumentsByTypeMatch(
                    S, TemplateParams, AddressSpaceParam->getPointeeType(),
                    AddressSpaceArg->getPointeeType(), Info, Deduced, TDF))
          return Result;

        // Perform deduction on the address space, if we can.
        NonTypeTemplateParmDecl *NTTP = getDeducedParameterFromExpr(
            Info, AddressSpaceParam->getAddrSpaceExpr());
        if (!NTTP)
          return Sema::TDK_Success;

        return DeduceNonTypeTemplateArgument(
            S, TemplateParams, NTTP, AddressSpaceArg->getAddrSpaceExpr(), Info,
            Deduced);
      }

      if (isTargetAddressSpace(Arg.getAddressSpace())) {
        llvm::APSInt ArgAddressSpace(S.Context.getTypeSize(S.Context.IntTy),
                                     false);
        ArgAddressSpace = toTargetAddressSpace(Arg.getAddressSpace());

        // Perform deduction on the pointer types.
        if (Sema::TemplateDeductionResult Result =
                DeduceTemplateArgumentsByTypeMatch(
                    S, TemplateParams, AddressSpaceParam->getPointeeType(),
                    S.Context.removeAddrSpaceQualType(Arg), Info, Deduced, TDF))
          return Result;

        // Perform deduction on the address space, if we can.
        NonTypeTemplateParmDecl *NTTP = getDeducedParameterFromExpr(
            Info, AddressSpaceParam->getAddrSpaceExpr());
        if (!NTTP)
          return Sema::TDK_Success;

        return DeduceNonTypeTemplateArgument(S, TemplateParams, NTTP,
                                             ArgAddressSpace, S.Context.IntTy,
                                             true, Info, Deduced);
      }

      return Sema::TDK_NonDeducedMismatch;
    }

    case Type::TypeOfExpr:
    case Type::TypeOf:
    case Type::DependentName:
    case Type::UnresolvedUsing:
    case Type::Decltype:
    case Type::UnaryTransform:
    case Type::Auto:
    case Type::DeducedTemplateSpecialization:
    case Type::DependentTemplateSpecialization:
    case Type::PackExpansion:
    case Type::Pipe:
      // No template argument deduction for these types
      return Sema::TDK_Success;
  }

  llvm_unreachable("Invalid Type Class!");
}

static Sema::TemplateDeductionResult
DeduceTemplateArguments(Sema &S,
                        TemplateParameterList *TemplateParams,
                        const TemplateArgument &Param,
                        TemplateArgument Arg,
                        TemplateDeductionInfo &Info,
                        SmallVectorImpl<DeducedTemplateArgument> &Deduced) {
  // If the template argument is a pack expansion, perform template argument
  // deduction against the pattern of that expansion. This only occurs during
  // partial ordering.
  if (Arg.isPackExpansion())
    Arg = Arg.getPackExpansionPattern();

  switch (Param.getKind()) {
  case TemplateArgument::Null:
    llvm_unreachable("Null template argument in parameter list");

  case TemplateArgument::Type:
    if (Arg.getKind() == TemplateArgument::Type)
      return DeduceTemplateArgumentsByTypeMatch(S, TemplateParams,
                                                Param.getAsType(),
                                                Arg.getAsType(),
                                                Info, Deduced, 0);
    Info.FirstArg = Param;
    Info.SecondArg = Arg;
    return Sema::TDK_NonDeducedMismatch;

  case TemplateArgument::Template:
    if (Arg.getKind() == TemplateArgument::Template)
      return DeduceTemplateArguments(S, TemplateParams,
                                     Param.getAsTemplate(),
                                     Arg.getAsTemplate(), Info, Deduced);
    Info.FirstArg = Param;
    Info.SecondArg = Arg;
    return Sema::TDK_NonDeducedMismatch;

  case TemplateArgument::TemplateExpansion:
    llvm_unreachable("caller should handle pack expansions");

  case TemplateArgument::Declaration:
    if (Arg.getKind() == TemplateArgument::Declaration &&
        isSameDeclaration(Param.getAsDecl(), Arg.getAsDecl()))
      return Sema::TDK_Success;

    Info.FirstArg = Param;
    Info.SecondArg = Arg;
    return Sema::TDK_NonDeducedMismatch;

  case TemplateArgument::NullPtr:
    if (Arg.getKind() == TemplateArgument::NullPtr &&
        S.Context.hasSameType(Param.getNullPtrType(), Arg.getNullPtrType()))
      return Sema::TDK_Success;

    Info.FirstArg = Param;
    Info.SecondArg = Arg;
    return Sema::TDK_NonDeducedMismatch;

  case TemplateArgument::Integral:
    if (Arg.getKind() == TemplateArgument::Integral) {
      if (hasSameExtendedValue(Param.getAsIntegral(), Arg.getAsIntegral()))
        return Sema::TDK_Success;

      Info.FirstArg = Param;
      Info.SecondArg = Arg;
      return Sema::TDK_NonDeducedMismatch;
    }

    if (Arg.getKind() == TemplateArgument::Expression) {
      Info.FirstArg = Param;
      Info.SecondArg = Arg;
      return Sema::TDK_NonDeducedMismatch;
    }

    Info.FirstArg = Param;
    Info.SecondArg = Arg;
    return Sema::TDK_NonDeducedMismatch;

  case TemplateArgument::Expression:
    if (NonTypeTemplateParmDecl *NTTP
          = getDeducedParameterFromExpr(Info, Param.getAsExpr())) {
      if (Arg.getKind() == TemplateArgument::Integral)
        return DeduceNonTypeTemplateArgument(S, TemplateParams, NTTP,
                                             Arg.getAsIntegral(),
                                             Arg.getIntegralType(),
                                             /*ArrayBound=*/false,
                                             Info, Deduced);
      if (Arg.getKind() == TemplateArgument::NullPtr)
        return DeduceNullPtrTemplateArgument(S, TemplateParams, NTTP,
                                             Arg.getNullPtrType(),
                                             Info, Deduced);
      if (Arg.getKind() == TemplateArgument::Expression)
        return DeduceNonTypeTemplateArgument(S, TemplateParams, NTTP,
                                             Arg.getAsExpr(), Info, Deduced);
      if (Arg.getKind() == TemplateArgument::Declaration)
        return DeduceNonTypeTemplateArgument(S, TemplateParams, NTTP,
                                             Arg.getAsDecl(),
                                             Arg.getParamTypeForDecl(),
                                             Info, Deduced);

      Info.FirstArg = Param;
      Info.SecondArg = Arg;
      return Sema::TDK_NonDeducedMismatch;
    }

    // Can't deduce anything, but that's okay.
    return Sema::TDK_Success;

  case TemplateArgument::Pack:
    llvm_unreachable("Argument packs should be expanded by the caller!");
  }

  llvm_unreachable("Invalid TemplateArgument Kind!");
}

/// Determine whether there is a template argument to be used for
/// deduction.
///
/// This routine "expands" argument packs in-place, overriding its input
/// parameters so that \c Args[ArgIdx] will be the available template argument.
///
/// \returns true if there is another template argument (which will be at
/// \c Args[ArgIdx]), false otherwise.
static bool hasTemplateArgumentForDeduction(ArrayRef<TemplateArgument> &Args,
                                            unsigned &ArgIdx) {
  if (ArgIdx == Args.size())
    return false;

  const TemplateArgument &Arg = Args[ArgIdx];
  if (Arg.getKind() != TemplateArgument::Pack)
    return true;

  assert(ArgIdx == Args.size() - 1 && "Pack not at the end of argument list?");
  Args = Arg.pack_elements();
  ArgIdx = 0;
  return ArgIdx < Args.size();
}

/// Determine whether the given set of template arguments has a pack
/// expansion that is not the last template argument.
static bool hasPackExpansionBeforeEnd(ArrayRef<TemplateArgument> Args) {
  bool FoundPackExpansion = false;
  for (const auto &A : Args) {
    if (FoundPackExpansion)
      return true;

    if (A.getKind() == TemplateArgument::Pack)
      return hasPackExpansionBeforeEnd(A.pack_elements());

    // FIXME: If this is a fixed-arity pack expansion from an outer level of
    // templates, it should not be treated as a pack expansion.
    if (A.isPackExpansion())
      FoundPackExpansion = true;
  }

  return false;
}

static Sema::TemplateDeductionResult
DeduceTemplateArguments(Sema &S, TemplateParameterList *TemplateParams,
                        ArrayRef<TemplateArgument> Params,
                        ArrayRef<TemplateArgument> Args,
                        TemplateDeductionInfo &Info,
                        SmallVectorImpl<DeducedTemplateArgument> &Deduced,
                        bool NumberOfArgumentsMustMatch) {
  // C++0x [temp.deduct.type]p9:
  //   If the template argument list of P contains a pack expansion that is not
  //   the last template argument, the entire template argument list is a
  //   non-deduced context.
  if (hasPackExpansionBeforeEnd(Params))
    return Sema::TDK_Success;

  // C++0x [temp.deduct.type]p9:
  //   If P has a form that contains <T> or <i>, then each argument Pi of the
  //   respective template argument list P is compared with the corresponding
  //   argument Ai of the corresponding template argument list of A.
  unsigned ArgIdx = 0, ParamIdx = 0;
  for (; hasTemplateArgumentForDeduction(Params, ParamIdx); ++ParamIdx) {
    if (!Params[ParamIdx].isPackExpansion()) {
      // The simple case: deduce template arguments by matching Pi and Ai.

      // Check whether we have enough arguments.
      if (!hasTemplateArgumentForDeduction(Args, ArgIdx))
        return NumberOfArgumentsMustMatch
                   ? Sema::TDK_MiscellaneousDeductionFailure
                   : Sema::TDK_Success;

      // C++1z [temp.deduct.type]p9:
      //   During partial ordering, if Ai was originally a pack expansion [and]
      //   Pi is not a pack expansion, template argument deduction fails.
      if (Args[ArgIdx].isPackExpansion())
        return Sema::TDK_MiscellaneousDeductionFailure;

      // Perform deduction for this Pi/Ai pair.
      if (Sema::TemplateDeductionResult Result
            = DeduceTemplateArguments(S, TemplateParams,
                                      Params[ParamIdx], Args[ArgIdx],
                                      Info, Deduced))
        return Result;

      // Move to the next argument.
      ++ArgIdx;
      continue;
    }

    // The parameter is a pack expansion.

    // C++0x [temp.deduct.type]p9:
    //   If Pi is a pack expansion, then the pattern of Pi is compared with
    //   each remaining argument in the template argument list of A. Each
    //   comparison deduces template arguments for subsequent positions in the
    //   template parameter packs expanded by Pi.
    TemplateArgument Pattern = Params[ParamIdx].getPackExpansionPattern();

    // Prepare to deduce the packs within the pattern.
    PackDeductionScope PackScope(S, TemplateParams, Deduced, Info, Pattern);

    // Keep track of the deduced template arguments for each parameter pack
    // expanded by this pack expansion (the outer index) and for each
    // template argument (the inner SmallVectors).
    for (; hasTemplateArgumentForDeduction(Args, ArgIdx) &&
           PackScope.hasNextElement();
         ++ArgIdx) {
      // Deduce template arguments from the pattern.
      if (Sema::TemplateDeductionResult Result
            = DeduceTemplateArguments(S, TemplateParams, Pattern, Args[ArgIdx],
                                      Info, Deduced))
        return Result;

      PackScope.nextPackElement();
    }

    // Build argument packs for each of the parameter packs expanded by this
    // pack expansion.
    if (auto Result = PackScope.finish())
      return Result;
  }

  return Sema::TDK_Success;
}

static Sema::TemplateDeductionResult
DeduceTemplateArguments(Sema &S,
                        TemplateParameterList *TemplateParams,
                        const TemplateArgumentList &ParamList,
                        const TemplateArgumentList &ArgList,
                        TemplateDeductionInfo &Info,
                        SmallVectorImpl<DeducedTemplateArgument> &Deduced) {
  return DeduceTemplateArguments(S, TemplateParams, ParamList.asArray(),
                                 ArgList.asArray(), Info, Deduced,
                                 /*NumberOfArgumentsMustMatch*/false);
}

/// Determine whether two template arguments are the same.
static bool isSameTemplateArg(ASTContext &Context,
                              TemplateArgument X,
                              const TemplateArgument &Y,
                              bool PackExpansionMatchesPack = false) {
  // If we're checking deduced arguments (X) against original arguments (Y),
  // we will have flattened packs to non-expansions in X.
  if (PackExpansionMatchesPack && X.isPackExpansion() && !Y.isPackExpansion())
    X = X.getPackExpansionPattern();

  if (X.getKind() != Y.getKind())
    return false;

  switch (X.getKind()) {
    case TemplateArgument::Null:
      llvm_unreachable("Comparing NULL template argument");

    case TemplateArgument::Type:
      return Context.getCanonicalType(X.getAsType()) ==
             Context.getCanonicalType(Y.getAsType());

    case TemplateArgument::Declaration:
      return isSameDeclaration(X.getAsDecl(), Y.getAsDecl());

    case TemplateArgument::NullPtr:
      return Context.hasSameType(X.getNullPtrType(), Y.getNullPtrType());

    case TemplateArgument::Template:
    case TemplateArgument::TemplateExpansion:
      return Context.getCanonicalTemplateName(
                    X.getAsTemplateOrTemplatePattern()).getAsVoidPointer() ==
             Context.getCanonicalTemplateName(
                    Y.getAsTemplateOrTemplatePattern()).getAsVoidPointer();

    case TemplateArgument::Integral:
      return hasSameExtendedValue(X.getAsIntegral(), Y.getAsIntegral());

    case TemplateArgument::Expression: {
      llvm::FoldingSetNodeID XID, YID;
      X.getAsExpr()->Profile(XID, Context, true);
      Y.getAsExpr()->Profile(YID, Context, true);
      return XID == YID;
    }

    case TemplateArgument::Pack:
      if (X.pack_size() != Y.pack_size())
        return false;

      for (TemplateArgument::pack_iterator XP = X.pack_begin(),
                                        XPEnd = X.pack_end(),
                                           YP = Y.pack_begin();
           XP != XPEnd; ++XP, ++YP)
        if (!isSameTemplateArg(Context, *XP, *YP, PackExpansionMatchesPack))
          return false;

      return true;
  }

  llvm_unreachable("Invalid TemplateArgument Kind!");
}

/// Allocate a TemplateArgumentLoc where all locations have
/// been initialized to the given location.
///
/// \param Arg The template argument we are producing template argument
/// location information for.
///
/// \param NTTPType For a declaration template argument, the type of
/// the non-type template parameter that corresponds to this template
/// argument. Can be null if no type sugar is available to add to the
/// type from the template argument.
///
/// \param Loc The source location to use for the resulting template
/// argument.
TemplateArgumentLoc
Sema::getTrivialTemplateArgumentLoc(const TemplateArgument &Arg,
                                    QualType NTTPType, SourceLocation Loc) {
  switch (Arg.getKind()) {
  case TemplateArgument::Null:
    llvm_unreachable("Can't get a NULL template argument here");

  case TemplateArgument::Type:
    return TemplateArgumentLoc(
        Arg, Context.getTrivialTypeSourceInfo(Arg.getAsType(), Loc));

  case TemplateArgument::Declaration: {
    if (NTTPType.isNull())
      NTTPType = Arg.getParamTypeForDecl();
    Expr *E = BuildExpressionFromDeclTemplateArgument(Arg, NTTPType, Loc)
                  .getAs<Expr>();
    return TemplateArgumentLoc(TemplateArgument(E), E);
  }

  case TemplateArgument::NullPtr: {
    if (NTTPType.isNull())
      NTTPType = Arg.getNullPtrType();
    Expr *E = BuildExpressionFromDeclTemplateArgument(Arg, NTTPType, Loc)
                  .getAs<Expr>();
    return TemplateArgumentLoc(TemplateArgument(NTTPType, /*isNullPtr*/true),
                               E);
  }

  case TemplateArgument::Integral: {
    Expr *E =
        BuildExpressionFromIntegralTemplateArgument(Arg, Loc).getAs<Expr>();
    return TemplateArgumentLoc(TemplateArgument(E), E);
  }

    case TemplateArgument::Template:
    case TemplateArgument::TemplateExpansion: {
      NestedNameSpecifierLocBuilder Builder;
      TemplateName Template = Arg.getAsTemplate();
      if (DependentTemplateName *DTN = Template.getAsDependentTemplateName())
        Builder.MakeTrivial(Context, DTN->getQualifier(), Loc);
      else if (QualifiedTemplateName *QTN =
                   Template.getAsQualifiedTemplateName())
        Builder.MakeTrivial(Context, QTN->getQualifier(), Loc);

      if (Arg.getKind() == TemplateArgument::Template)
        return TemplateArgumentLoc(Arg, Builder.getWithLocInContext(Context),
                                   Loc);

      return TemplateArgumentLoc(Arg, Builder.getWithLocInContext(Context),
                                 Loc, Loc);
    }

  case TemplateArgument::Expression:
    return TemplateArgumentLoc(Arg, Arg.getAsExpr());

  case TemplateArgument::Pack:
    return TemplateArgumentLoc(Arg, TemplateArgumentLocInfo());
  }

  llvm_unreachable("Invalid TemplateArgument Kind!");
}

/// Convert the given deduced template argument and add it to the set of
/// fully-converted template arguments.
static bool
ConvertDeducedTemplateArgument(Sema &S, NamedDecl *Param,
                               DeducedTemplateArgument Arg,
                               NamedDecl *Template,
                               TemplateDeductionInfo &Info,
                               bool IsDeduced,
                               SmallVectorImpl<TemplateArgument> &Output) {
  auto ConvertArg = [&](DeducedTemplateArgument Arg,
                        unsigned ArgumentPackIndex) {
    // Convert the deduced template argument into a template
    // argument that we can check, almost as if the user had written
    // the template argument explicitly.
    TemplateArgumentLoc ArgLoc =
        S.getTrivialTemplateArgumentLoc(Arg, QualType(), Info.getLocation());

    // Check the template argument, converting it as necessary.
    return S.CheckTemplateArgument(
        Param, ArgLoc, Template, Template->getLocation(),
        Template->getSourceRange().getEnd(), ArgumentPackIndex, Output,
        IsDeduced
            ? (Arg.wasDeducedFromArrayBound() ? Sema::CTAK_DeducedFromArrayBound
                                              : Sema::CTAK_Deduced)
            : Sema::CTAK_Specified);
  };

  if (Arg.getKind() == TemplateArgument::Pack) {
    // This is a template argument pack, so check each of its arguments against
    // the template parameter.
    SmallVector<TemplateArgument, 2> PackedArgsBuilder;
    for (const auto &P : Arg.pack_elements()) {
      // When converting the deduced template argument, append it to the
      // general output list. We need to do this so that the template argument
      // checking logic has all of the prior template arguments available.
      DeducedTemplateArgument InnerArg(P);
      InnerArg.setDeducedFromArrayBound(Arg.wasDeducedFromArrayBound());
      assert(InnerArg.getKind() != TemplateArgument::Pack &&
             "deduced nested pack");
      if (P.isNull()) {
        // We deduced arguments for some elements of this pack, but not for
        // all of them. This happens if we get a conditionally-non-deduced
        // context in a pack expansion (such as an overload set in one of the
        // arguments).
        S.Diag(Param->getLocation(),
               diag::err_template_arg_deduced_incomplete_pack)
          << Arg << Param;
        return true;
      }
      if (ConvertArg(InnerArg, PackedArgsBuilder.size()))
        return true;

      // Move the converted template argument into our argument pack.
      PackedArgsBuilder.push_back(Output.pop_back_val());
    }

    // If the pack is empty, we still need to substitute into the parameter
    // itself, in case that substitution fails.
    if (PackedArgsBuilder.empty()) {
      LocalInstantiationScope Scope(S);
      TemplateArgumentList TemplateArgs(TemplateArgumentList::OnStack, Output);
      MultiLevelTemplateArgumentList Args(TemplateArgs);

      if (auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(Param)) {
        Sema::InstantiatingTemplate Inst(S, Template->getLocation(), Template,
                                         NTTP, Output,
                                         Template->getSourceRange());
        if (Inst.isInvalid() ||
            S.SubstType(NTTP->getType(), Args, NTTP->getLocation(),
                        NTTP->getDeclName()).isNull())
          return true;
      } else if (auto *TTP = dyn_cast<TemplateTemplateParmDecl>(Param)) {
        Sema::InstantiatingTemplate Inst(S, Template->getLocation(), Template,
                                         TTP, Output,
                                         Template->getSourceRange());
        if (Inst.isInvalid() || !S.SubstDecl(TTP, S.CurContext, Args))
          return true;
      }
      // For type parameters, no substitution is ever required.
    }

    // Create the resulting argument pack.
    Output.push_back(
        TemplateArgument::CreatePackCopy(S.Context, PackedArgsBuilder));
    return false;
  }

  return ConvertArg(Arg, 0);
}

// FIXME: This should not be a template, but
// ClassTemplatePartialSpecializationDecl sadly does not derive from
// TemplateDecl.
template<typename TemplateDeclT>
static Sema::TemplateDeductionResult ConvertDeducedTemplateArguments(
    Sema &S, TemplateDeclT *Template, bool IsDeduced,
    SmallVectorImpl<DeducedTemplateArgument> &Deduced,
    TemplateDeductionInfo &Info, SmallVectorImpl<TemplateArgument> &Builder,
    LocalInstantiationScope *CurrentInstantiationScope = nullptr,
    unsigned NumAlreadyConverted = 0, bool PartialOverloading = false) {
  TemplateParameterList *TemplateParams = Template->getTemplateParameters();

  for (unsigned I = 0, N = TemplateParams->size(); I != N; ++I) {
    NamedDecl *Param = TemplateParams->getParam(I);

    // C++0x [temp.arg.explicit]p3:
    //    A trailing template parameter pack (14.5.3) not otherwise deduced will
    //    be deduced to an empty sequence of template arguments.
    // FIXME: Where did the word "trailing" come from?
    if (Deduced[I].isNull() && Param->isTemplateParameterPack()) {
      if (auto Result = PackDeductionScope(S, TemplateParams, Deduced, Info, I)
                            .finish(/*TreatNoDeductionsAsNonDeduced*/false))
        return Result;
    }

    if (!Deduced[I].isNull()) {
      if (I < NumAlreadyConverted) {
        // We may have had explicitly-specified template arguments for a
        // template parameter pack (that may or may not have been extended
        // via additional deduced arguments).
        if (Param->isParameterPack() && CurrentInstantiationScope &&
            CurrentInstantiationScope->getPartiallySubstitutedPack() == Param) {
          // Forget the partially-substituted pack; its substitution is now
          // complete.
          CurrentInstantiationScope->ResetPartiallySubstitutedPack();
          // We still need to check the argument in case it was extended by
          // deduction.
        } else {
          // We have already fully type-checked and converted this
          // argument, because it was explicitly-specified. Just record the
          // presence of this argument.
          Builder.push_back(Deduced[I]);
          continue;
        }
      }

      // We may have deduced this argument, so it still needs to be
      // checked and converted.
      if (ConvertDeducedTemplateArgument(S, Param, Deduced[I], Template, Info,
                                         IsDeduced, Builder)) {
        Info.Param = makeTemplateParameter(Param);
        // FIXME: These template arguments are temporary. Free them!
        Info.reset(TemplateArgumentList::CreateCopy(S.Context, Builder));
        return Sema::TDK_SubstitutionFailure;
      }

      continue;
    }

    // Substitute into the default template argument, if available.
    bool HasDefaultArg = false;
    TemplateDecl *TD = dyn_cast<TemplateDecl>(Template);
    if (!TD) {
      assert(isa<ClassTemplatePartialSpecializationDecl>(Template) ||
             isa<VarTemplatePartialSpecializationDecl>(Template));
      return Sema::TDK_Incomplete;
    }

    TemplateArgumentLoc DefArg = S.SubstDefaultTemplateArgumentIfAvailable(
        TD, TD->getLocation(), TD->getSourceRange().getEnd(), Param, Builder,
        HasDefaultArg);

    // If there was no default argument, deduction is incomplete.
    if (DefArg.getArgument().isNull()) {
      Info.Param = makeTemplateParameter(
          const_cast<NamedDecl *>(TemplateParams->getParam(I)));
      Info.reset(TemplateArgumentList::CreateCopy(S.Context, Builder));
      if (PartialOverloading) break;

      return HasDefaultArg ? Sema::TDK_SubstitutionFailure
                           : Sema::TDK_Incomplete;
    }

    // Check whether we can actually use the default argument.
    if (S.CheckTemplateArgument(Param, DefArg, TD, TD->getLocation(),
                                TD->getSourceRange().getEnd(), 0, Builder,
                                Sema::CTAK_Specified)) {
      Info.Param = makeTemplateParameter(
                         const_cast<NamedDecl *>(TemplateParams->getParam(I)));
      // FIXME: These template arguments are temporary. Free them!
      Info.reset(TemplateArgumentList::CreateCopy(S.Context, Builder));
      return Sema::TDK_SubstitutionFailure;
    }

    // If we get here, we successfully used the default template argument.
  }

  return Sema::TDK_Success;
}

static DeclContext *getAsDeclContextOrEnclosing(Decl *D) {
  if (auto *DC = dyn_cast<DeclContext>(D))
    return DC;
  return D->getDeclContext();
}

template<typename T> struct IsPartialSpecialization {
  static constexpr bool value = false;
};
template<>
struct IsPartialSpecialization<ClassTemplatePartialSpecializationDecl> {
  static constexpr bool value = true;
};
template<>
struct IsPartialSpecialization<VarTemplatePartialSpecializationDecl> {
  static constexpr bool value = true;
};

/// Complete template argument deduction for a partial specialization.
template <typename T>
static typename std::enable_if<IsPartialSpecialization<T>::value,
                               Sema::TemplateDeductionResult>::type
FinishTemplateArgumentDeduction(
    Sema &S, T *Partial, bool IsPartialOrdering,
    const TemplateArgumentList &TemplateArgs,
    SmallVectorImpl<DeducedTemplateArgument> &Deduced,
    TemplateDeductionInfo &Info) {
  // Unevaluated SFINAE context.
  EnterExpressionEvaluationContext Unevaluated(
      S, Sema::ExpressionEvaluationContext::Unevaluated);
  Sema::SFINAETrap Trap(S);

  Sema::ContextRAII SavedContext(S, getAsDeclContextOrEnclosing(Partial));

  // C++ [temp.deduct.type]p2:
  //   [...] or if any template argument remains neither deduced nor
  //   explicitly specified, template argument deduction fails.
  SmallVector<TemplateArgument, 4> Builder;
  if (auto Result = ConvertDeducedTemplateArguments(
          S, Partial, IsPartialOrdering, Deduced, Info, Builder))
    return Result;

  // Form the template argument list from the deduced template arguments.
  TemplateArgumentList *DeducedArgumentList
    = TemplateArgumentList::CreateCopy(S.Context, Builder);

  Info.reset(DeducedArgumentList);

  // Substitute the deduced template arguments into the template
  // arguments of the class template partial specialization, and
  // verify that the instantiated template arguments are both valid
  // and are equivalent to the template arguments originally provided
  // to the class template.
  LocalInstantiationScope InstScope(S);
  auto *Template = Partial->getSpecializedTemplate();
  const ASTTemplateArgumentListInfo *PartialTemplArgInfo =
      Partial->getTemplateArgsAsWritten();
  const TemplateArgumentLoc *PartialTemplateArgs =
      PartialTemplArgInfo->getTemplateArgs();

  TemplateArgumentListInfo InstArgs(PartialTemplArgInfo->LAngleLoc,
                                    PartialTemplArgInfo->RAngleLoc);

  if (S.Subst(PartialTemplateArgs, PartialTemplArgInfo->NumTemplateArgs,
              InstArgs, MultiLevelTemplateArgumentList(*DeducedArgumentList))) {
    unsigned ArgIdx = InstArgs.size(), ParamIdx = ArgIdx;
    if (ParamIdx >= Partial->getTemplateParameters()->size())
      ParamIdx = Partial->getTemplateParameters()->size() - 1;

    Decl *Param = const_cast<NamedDecl *>(
        Partial->getTemplateParameters()->getParam(ParamIdx));
    Info.Param = makeTemplateParameter(Param);
    Info.FirstArg = PartialTemplateArgs[ArgIdx].getArgument();
    return Sema::TDK_SubstitutionFailure;
  }

  SmallVector<TemplateArgument, 4> ConvertedInstArgs;
  if (S.CheckTemplateArgumentList(Template, Partial->getLocation(), InstArgs,
                                  false, ConvertedInstArgs))
    return Sema::TDK_SubstitutionFailure;

  TemplateParameterList *TemplateParams = Template->getTemplateParameters();
  for (unsigned I = 0, E = TemplateParams->size(); I != E; ++I) {
    TemplateArgument InstArg = ConvertedInstArgs.data()[I];
    if (!isSameTemplateArg(S.Context, TemplateArgs[I], InstArg)) {
      Info.Param = makeTemplateParameter(TemplateParams->getParam(I));
      Info.FirstArg = TemplateArgs[I];
      Info.SecondArg = InstArg;
      return Sema::TDK_NonDeducedMismatch;
    }
  }

  if (Trap.hasErrorOccurred())
    return Sema::TDK_SubstitutionFailure;

  return Sema::TDK_Success;
}

/// Complete template argument deduction for a class or variable template,
/// when partial ordering against a partial specialization.
// FIXME: Factor out duplication with partial specialization version above.
static Sema::TemplateDeductionResult FinishTemplateArgumentDeduction(
    Sema &S, TemplateDecl *Template, bool PartialOrdering,
    const TemplateArgumentList &TemplateArgs,
    SmallVectorImpl<DeducedTemplateArgument> &Deduced,
    TemplateDeductionInfo &Info) {
  // Unevaluated SFINAE context.
  EnterExpressionEvaluationContext Unevaluated(
      S, Sema::ExpressionEvaluationContext::Unevaluated);
  Sema::SFINAETrap Trap(S);

  Sema::ContextRAII SavedContext(S, getAsDeclContextOrEnclosing(Template));

  // C++ [temp.deduct.type]p2:
  //   [...] or if any template argument remains neither deduced nor
  //   explicitly specified, template argument deduction fails.
  SmallVector<TemplateArgument, 4> Builder;
  if (auto Result = ConvertDeducedTemplateArguments(
          S, Template, /*IsDeduced*/PartialOrdering, Deduced, Info, Builder))
    return Result;

  // Check that we produced the correct argument list.
  TemplateParameterList *TemplateParams = Template->getTemplateParameters();
  for (unsigned I = 0, E = TemplateParams->size(); I != E; ++I) {
    TemplateArgument InstArg = Builder[I];
    if (!isSameTemplateArg(S.Context, TemplateArgs[I], InstArg,
                           /*PackExpansionMatchesPack*/true)) {
      Info.Param = makeTemplateParameter(TemplateParams->getParam(I));
      Info.FirstArg = TemplateArgs[I];
      Info.SecondArg = InstArg;
      return Sema::TDK_NonDeducedMismatch;
    }
  }

  if (Trap.hasErrorOccurred())
    return Sema::TDK_SubstitutionFailure;

  return Sema::TDK_Success;
}


/// Perform template argument deduction to determine whether
/// the given template arguments match the given class template
/// partial specialization per C++ [temp.class.spec.match].
Sema::TemplateDeductionResult
Sema::DeduceTemplateArguments(ClassTemplatePartialSpecializationDecl *Partial,
                              const TemplateArgumentList &TemplateArgs,
                              TemplateDeductionInfo &Info) {
  if (Partial->isInvalidDecl())
    return TDK_Invalid;

  // C++ [temp.class.spec.match]p2:
  //   A partial specialization matches a given actual template
  //   argument list if the template arguments of the partial
  //   specialization can be deduced from the actual template argument
  //   list (14.8.2).

  // Unevaluated SFINAE context.
  EnterExpressionEvaluationContext Unevaluated(
      *this, Sema::ExpressionEvaluationContext::Unevaluated);
  SFINAETrap Trap(*this);

  SmallVector<DeducedTemplateArgument, 4> Deduced;
  Deduced.resize(Partial->getTemplateParameters()->size());
  if (TemplateDeductionResult Result
        = ::DeduceTemplateArguments(*this,
                                    Partial->getTemplateParameters(),
                                    Partial->getTemplateArgs(),
                                    TemplateArgs, Info, Deduced))
    return Result;

  SmallVector<TemplateArgument, 4> DeducedArgs(Deduced.begin(), Deduced.end());
  InstantiatingTemplate Inst(*this, Info.getLocation(), Partial, DeducedArgs,
                             Info);
  if (Inst.isInvalid())
    return TDK_InstantiationDepth;

  if (Trap.hasErrorOccurred())
    return Sema::TDK_SubstitutionFailure;

  return ::FinishTemplateArgumentDeduction(
      *this, Partial, /*PartialOrdering=*/false, TemplateArgs, Deduced, Info);
}

/// Perform template argument deduction to determine whether
/// the given template arguments match the given variable template
/// partial specialization per C++ [temp.class.spec.match].
Sema::TemplateDeductionResult
Sema::DeduceTemplateArguments(VarTemplatePartialSpecializationDecl *Partial,
                              const TemplateArgumentList &TemplateArgs,
                              TemplateDeductionInfo &Info) {
  if (Partial->isInvalidDecl())
    return TDK_Invalid;

  // C++ [temp.class.spec.match]p2:
  //   A partial specialization matches a given actual template
  //   argument list if the template arguments of the partial
  //   specialization can be deduced from the actual template argument
  //   list (14.8.2).

  // Unevaluated SFINAE context.
  EnterExpressionEvaluationContext Unevaluated(
      *this, Sema::ExpressionEvaluationContext::Unevaluated);
  SFINAETrap Trap(*this);

  SmallVector<DeducedTemplateArgument, 4> Deduced;
  Deduced.resize(Partial->getTemplateParameters()->size());
  if (TemplateDeductionResult Result = ::DeduceTemplateArguments(
          *this, Partial->getTemplateParameters(), Partial->getTemplateArgs(),
          TemplateArgs, Info, Deduced))
    return Result;

  SmallVector<TemplateArgument, 4> DeducedArgs(Deduced.begin(), Deduced.end());
  InstantiatingTemplate Inst(*this, Info.getLocation(), Partial, DeducedArgs,
                             Info);
  if (Inst.isInvalid())
    return TDK_InstantiationDepth;

  if (Trap.hasErrorOccurred())
    return Sema::TDK_SubstitutionFailure;

  return ::FinishTemplateArgumentDeduction(
      *this, Partial, /*PartialOrdering=*/false, TemplateArgs, Deduced, Info);
}

/// Determine whether the given type T is a simple-template-id type.
static bool isSimpleTemplateIdType(QualType T) {
  if (const TemplateSpecializationType *Spec
        = T->getAs<TemplateSpecializationType>())
    return Spec->getTemplateName().getAsTemplateDecl() != nullptr;

  // C++17 [temp.local]p2:
  //   the injected-class-name [...] is equivalent to the template-name followed
  //   by the template-arguments of the class template specialization or partial
  //   specialization enclosed in <>
  // ... which means it's equivalent to a simple-template-id.
  //
  // This only arises during class template argument deduction for a copy
  // deduction candidate, where it permits slicing.
  if (T->getAs<InjectedClassNameType>())
    return true;

  return false;
}

/// Substitute the explicitly-provided template arguments into the
/// given function template according to C++ [temp.arg.explicit].
///
/// \param FunctionTemplate the function template into which the explicit
/// template arguments will be substituted.
///
/// \param ExplicitTemplateArgs the explicitly-specified template
/// arguments.
///
/// \param Deduced the deduced template arguments, which will be populated
/// with the converted and checked explicit template arguments.
///
/// \param ParamTypes will be populated with the instantiated function
/// parameters.
///
/// \param FunctionType if non-NULL, the result type of the function template
/// will also be instantiated and the pointed-to value will be updated with
/// the instantiated function type.
///
/// \param Info if substitution fails for any reason, this object will be
/// populated with more information about the failure.
///
/// \returns TDK_Success if substitution was successful, or some failure
/// condition.
Sema::TemplateDeductionResult
Sema::SubstituteExplicitTemplateArguments(
                                      FunctionTemplateDecl *FunctionTemplate,
                               TemplateArgumentListInfo &ExplicitTemplateArgs,
                       SmallVectorImpl<DeducedTemplateArgument> &Deduced,
                                 SmallVectorImpl<QualType> &ParamTypes,
                                          QualType *FunctionType,
                                          TemplateDeductionInfo &Info) {
  FunctionDecl *Function = FunctionTemplate->getTemplatedDecl();
  TemplateParameterList *TemplateParams
    = FunctionTemplate->getTemplateParameters();

  if (ExplicitTemplateArgs.size() == 0) {
    // No arguments to substitute; just copy over the parameter types and
    // fill in the function type.
    for (auto P : Function->parameters())
      ParamTypes.push_back(P->getType());

    if (FunctionType)
      *FunctionType = Function->getType();
    return TDK_Success;
  }

  // Unevaluated SFINAE context.
  EnterExpressionEvaluationContext Unevaluated(
      *this, Sema::ExpressionEvaluationContext::Unevaluated);
  SFINAETrap Trap(*this);

  // C++ [temp.arg.explicit]p3:
  //   Template arguments that are present shall be specified in the
  //   declaration order of their corresponding template-parameters. The
  //   template argument list shall not specify more template-arguments than
  //   there are corresponding template-parameters.
  SmallVector<TemplateArgument, 4> Builder;

  // Enter a new template instantiation context where we check the
  // explicitly-specified template arguments against this function template,
  // and then substitute them into the function parameter types.
  SmallVector<TemplateArgument, 4> DeducedArgs;
  InstantiatingTemplate Inst(
      *this, Info.getLocation(), FunctionTemplate, DeducedArgs,
      CodeSynthesisContext::ExplicitTemplateArgumentSubstitution, Info);
  if (Inst.isInvalid())
    return TDK_InstantiationDepth;

  if (CheckTemplateArgumentList(FunctionTemplate, SourceLocation(),
                                ExplicitTemplateArgs, true, Builder, false) ||
      Trap.hasErrorOccurred()) {
    unsigned Index = Builder.size();
    if (Index >= TemplateParams->size())
      return TDK_SubstitutionFailure;
    Info.Param = makeTemplateParameter(TemplateParams->getParam(Index));
    return TDK_InvalidExplicitArguments;
  }

  // Form the template argument list from the explicitly-specified
  // template arguments.
  TemplateArgumentList *ExplicitArgumentList
    = TemplateArgumentList::CreateCopy(Context, Builder);
  Info.setExplicitArgs(ExplicitArgumentList);

  // Template argument deduction and the final substitution should be
  // done in the context of the templated declaration.  Explicit
  // argument substitution, on the other hand, needs to happen in the
  // calling context.
  ContextRAII SavedContext(*this, FunctionTemplate->getTemplatedDecl());

  // If we deduced template arguments for a template parameter pack,
  // note that the template argument pack is partially substituted and record
  // the explicit template arguments. They'll be used as part of deduction
  // for this template parameter pack.
  unsigned PartiallySubstitutedPackIndex = -1u;
  if (!Builder.empty()) {
    const TemplateArgument &Arg = Builder.back();
    if (Arg.getKind() == TemplateArgument::Pack) {
      auto *Param = TemplateParams->getParam(Builder.size() - 1);
      // If this is a fully-saturated fixed-size pack, it should be
      // fully-substituted, not partially-substituted.
      Optional<unsigned> Expansions = getExpandedPackSize(Param);
      if (!Expansions || Arg.pack_size() < *Expansions) {
        PartiallySubstitutedPackIndex = Builder.size() - 1;
        CurrentInstantiationScope->SetPartiallySubstitutedPack(
            Param, Arg.pack_begin(), Arg.pack_size());
      }
    }
  }

  const FunctionProtoType *Proto
    = Function->getType()->getAs<FunctionProtoType>();
  assert(Proto && "Function template does not have a prototype?");

  // Isolate our substituted parameters from our caller.
  LocalInstantiationScope InstScope(*this, /*MergeWithOuterScope*/true);

  ExtParameterInfoBuilder ExtParamInfos;

  // Instantiate the types of each of the function parameters given the
  // explicitly-specified template arguments. If the function has a trailing
  // return type, substitute it after the arguments to ensure we substitute
  // in lexical order.
  if (Proto->hasTrailingReturn()) {
    if (SubstParmTypes(Function->getLocation(), Function->parameters(),
                       Proto->getExtParameterInfosOrNull(),
                       MultiLevelTemplateArgumentList(*ExplicitArgumentList),
                       ParamTypes, /*params*/ nullptr, ExtParamInfos))
      return TDK_SubstitutionFailure;
  }

  // Instantiate the return type.
  QualType ResultType;
  {
    // C++11 [expr.prim.general]p3:
    //   If a declaration declares a member function or member function
    //   template of a class X, the expression this is a prvalue of type
    //   "pointer to cv-qualifier-seq X" between the optional cv-qualifer-seq
    //   and the end of the function-definition, member-declarator, or
    //   declarator.
    Qualifiers ThisTypeQuals;
    CXXRecordDecl *ThisContext = nullptr;
    if (CXXMethodDecl *Method = dyn_cast<CXXMethodDecl>(Function)) {
      ThisContext = Method->getParent();
      ThisTypeQuals = Method->getTypeQualifiers();
    }

    CXXThisScopeRAII ThisScope(*this, ThisContext, ThisTypeQuals,
                               getLangOpts().CPlusPlus11);

    ResultType =
        SubstType(Proto->getReturnType(),
                  MultiLevelTemplateArgumentList(*ExplicitArgumentList),
                  Function->getTypeSpecStartLoc(), Function->getDeclName());
    if (ResultType.isNull() || Trap.hasErrorOccurred())
      return TDK_SubstitutionFailure;
  }

  // Instantiate the types of each of the function parameters given the
  // explicitly-specified template arguments if we didn't do so earlier.
  if (!Proto->hasTrailingReturn() &&
      SubstParmTypes(Function->getLocation(), Function->parameters(),
                     Proto->getExtParameterInfosOrNull(),
                     MultiLevelTemplateArgumentList(*ExplicitArgumentList),
                     ParamTypes, /*params*/ nullptr, ExtParamInfos))
    return TDK_SubstitutionFailure;

  if (FunctionType) {
    auto EPI = Proto->getExtProtoInfo();
    EPI.ExtParameterInfos = ExtParamInfos.getPointerOrNull(ParamTypes.size());

    // In C++1z onwards, exception specifications are part of the function type,
    // so substitution into the type must also substitute into the exception
    // specification.
    SmallVector<QualType, 4> ExceptionStorage;
    if (getLangOpts().CPlusPlus17 &&
        SubstExceptionSpec(
            Function->getLocation(), EPI.ExceptionSpec, ExceptionStorage,
            MultiLevelTemplateArgumentList(*ExplicitArgumentList)))
      return TDK_SubstitutionFailure;

    *FunctionType = BuildFunctionType(ResultType, ParamTypes,
                                      Function->getLocation(),
                                      Function->getDeclName(),
                                      EPI);
    if (FunctionType->isNull() || Trap.hasErrorOccurred())
      return TDK_SubstitutionFailure;
  }

  // C++ [temp.arg.explicit]p2:
  //   Trailing template arguments that can be deduced (14.8.2) may be
  //   omitted from the list of explicit template-arguments. If all of the
  //   template arguments can be deduced, they may all be omitted; in this
  //   case, the empty template argument list <> itself may also be omitted.
  //
  // Take all of the explicitly-specified arguments and put them into
  // the set of deduced template arguments. The partially-substituted
  // parameter pack, however, will be set to NULL since the deduction
  // mechanism handles the partially-substituted argument pack directly.
  Deduced.reserve(TemplateParams->size());
  for (unsigned I = 0, N = ExplicitArgumentList->size(); I != N; ++I) {
    const TemplateArgument &Arg = ExplicitArgumentList->get(I);
    if (I == PartiallySubstitutedPackIndex)
      Deduced.push_back(DeducedTemplateArgument());
    else
      Deduced.push_back(Arg);
  }

  return TDK_Success;
}

/// Check whether the deduced argument type for a call to a function
/// template matches the actual argument type per C++ [temp.deduct.call]p4.
static Sema::TemplateDeductionResult
CheckOriginalCallArgDeduction(Sema &S, TemplateDeductionInfo &Info,
                              Sema::OriginalCallArg OriginalArg,
                              QualType DeducedA) {
  ASTContext &Context = S.Context;

  auto Failed = [&]() -> Sema::TemplateDeductionResult {
    Info.FirstArg = TemplateArgument(DeducedA);
    Info.SecondArg = TemplateArgument(OriginalArg.OriginalArgType);
    Info.CallArgIndex = OriginalArg.ArgIdx;
    return OriginalArg.DecomposedParam ? Sema::TDK_DeducedMismatchNested
                                       : Sema::TDK_DeducedMismatch;
  };

  QualType A = OriginalArg.OriginalArgType;
  QualType OriginalParamType = OriginalArg.OriginalParamType;

  // Check for type equality (top-level cv-qualifiers are ignored).
  if (Context.hasSameUnqualifiedType(A, DeducedA))
    return Sema::TDK_Success;

  // Strip off references on the argument types; they aren't needed for
  // the following checks.
  if (const ReferenceType *DeducedARef = DeducedA->getAs<ReferenceType>())
    DeducedA = DeducedARef->getPointeeType();
  if (const ReferenceType *ARef = A->getAs<ReferenceType>())
    A = ARef->getPointeeType();

  // C++ [temp.deduct.call]p4:
  //   [...] However, there are three cases that allow a difference:
  //     - If the original P is a reference type, the deduced A (i.e., the
  //       type referred to by the reference) can be more cv-qualified than
  //       the transformed A.
  if (const ReferenceType *OriginalParamRef
      = OriginalParamType->getAs<ReferenceType>()) {
    // We don't want to keep the reference around any more.
    OriginalParamType = OriginalParamRef->getPointeeType();

    // FIXME: Resolve core issue (no number yet): if the original P is a
    // reference type and the transformed A is function type "noexcept F",
    // the deduced A can be F.
    QualType Tmp;
    if (A->isFunctionType() && S.IsFunctionConversion(A, DeducedA, Tmp))
      return Sema::TDK_Success;

    Qualifiers AQuals = A.getQualifiers();
    Qualifiers DeducedAQuals = DeducedA.getQualifiers();

    // Under Objective-C++ ARC, the deduced type may have implicitly
    // been given strong or (when dealing with a const reference)
    // unsafe_unretained lifetime. If so, update the original
    // qualifiers to include this lifetime.
    if (S.getLangOpts().ObjCAutoRefCount &&
        ((DeducedAQuals.getObjCLifetime() == Qualifiers::OCL_Strong &&
          AQuals.getObjCLifetime() == Qualifiers::OCL_None) ||
         (DeducedAQuals.hasConst() &&
          DeducedAQuals.getObjCLifetime() == Qualifiers::OCL_ExplicitNone))) {
      AQuals.setObjCLifetime(DeducedAQuals.getObjCLifetime());
    }

    if (AQuals == DeducedAQuals) {
      // Qualifiers match; there's nothing to do.
    } else if (!DeducedAQuals.compatiblyIncludes(AQuals)) {
      return Failed();
    } else {
      // Qualifiers are compatible, so have the argument type adopt the
      // deduced argument type's qualifiers as if we had performed the
      // qualification conversion.
      A = Context.getQualifiedType(A.getUnqualifiedType(), DeducedAQuals);
    }
  }

  //    - The transformed A can be another pointer or pointer to member
  //      type that can be converted to the deduced A via a function pointer
  //      conversion and/or a qualification conversion.
  //
  // Also allow conversions which merely strip __attribute__((noreturn)) from
  // function types (recursively).
  bool ObjCLifetimeConversion = false;
  QualType ResultTy;
  if ((A->isAnyPointerType() || A->isMemberPointerType()) &&
      (S.IsQualificationConversion(A, DeducedA, false,
                                   ObjCLifetimeConversion) ||
       S.IsFunctionConversion(A, DeducedA, ResultTy)))
    return Sema::TDK_Success;

  //    - If P is a class and P has the form simple-template-id, then the
  //      transformed A can be a derived class of the deduced A. [...]
  //     [...] Likewise, if P is a pointer to a class of the form
  //      simple-template-id, the transformed A can be a pointer to a
  //      derived class pointed to by the deduced A.
  if (const PointerType *OriginalParamPtr
      = OriginalParamType->getAs<PointerType>()) {
    if (const PointerType *DeducedAPtr = DeducedA->getAs<PointerType>()) {
      if (const PointerType *APtr = A->getAs<PointerType>()) {
        if (A->getPointeeType()->isRecordType()) {
          OriginalParamType = OriginalParamPtr->getPointeeType();
          DeducedA = DeducedAPtr->getPointeeType();
          A = APtr->getPointeeType();
        }
      }
    }
  }

  if (Context.hasSameUnqualifiedType(A, DeducedA))
    return Sema::TDK_Success;

  if (A->isRecordType() && isSimpleTemplateIdType(OriginalParamType) &&
      S.IsDerivedFrom(Info.getLocation(), A, DeducedA))
    return Sema::TDK_Success;

  return Failed();
}

/// Find the pack index for a particular parameter index in an instantiation of
/// a function template with specific arguments.
///
/// \return The pack index for whichever pack produced this parameter, or -1
///         if this was not produced by a parameter. Intended to be used as the
///         ArgumentPackSubstitutionIndex for further substitutions.
// FIXME: We should track this in OriginalCallArgs so we don't need to
// reconstruct it here.
static unsigned getPackIndexForParam(Sema &S,
                                     FunctionTemplateDecl *FunctionTemplate,
                                     const MultiLevelTemplateArgumentList &Args,
                                     unsigned ParamIdx) {
  unsigned Idx = 0;
  for (auto *PD : FunctionTemplate->getTemplatedDecl()->parameters()) {
    if (PD->isParameterPack()) {
      unsigned NumExpansions =
          S.getNumArgumentsInExpansion(PD->getType(), Args).getValueOr(1);
      if (Idx + NumExpansions > ParamIdx)
        return ParamIdx - Idx;
      Idx += NumExpansions;
    } else {
      if (Idx == ParamIdx)
        return -1; // Not a pack expansion
      ++Idx;
    }
  }

  llvm_unreachable("parameter index would not be produced from template");
}

/// Finish template argument deduction for a function template,
/// checking the deduced template arguments for completeness and forming
/// the function template specialization.
///
/// \param OriginalCallArgs If non-NULL, the original call arguments against
/// which the deduced argument types should be compared.
Sema::TemplateDeductionResult Sema::FinishTemplateArgumentDeduction(
    FunctionTemplateDecl *FunctionTemplate,
    SmallVectorImpl<DeducedTemplateArgument> &Deduced,
    unsigned NumExplicitlySpecified, FunctionDecl *&Specialization,
    TemplateDeductionInfo &Info,
    SmallVectorImpl<OriginalCallArg> const *OriginalCallArgs,
    bool PartialOverloading, llvm::function_ref<bool()> CheckNonDependent) {
  // Unevaluated SFINAE context.
  EnterExpressionEvaluationContext Unevaluated(
      *this, Sema::ExpressionEvaluationContext::Unevaluated);
  SFINAETrap Trap(*this);

  // Enter a new template instantiation context while we instantiate the
  // actual function declaration.
  SmallVector<TemplateArgument, 4> DeducedArgs(Deduced.begin(), Deduced.end());
  InstantiatingTemplate Inst(
      *this, Info.getLocation(), FunctionTemplate, DeducedArgs,
      CodeSynthesisContext::DeducedTemplateArgumentSubstitution, Info);
  if (Inst.isInvalid())
    return TDK_InstantiationDepth;

  ContextRAII SavedContext(*this, FunctionTemplate->getTemplatedDecl());

  // C++ [temp.deduct.type]p2:
  //   [...] or if any template argument remains neither deduced nor
  //   explicitly specified, template argument deduction fails.
  SmallVector<TemplateArgument, 4> Builder;
  if (auto Result = ConvertDeducedTemplateArguments(
          *this, FunctionTemplate, /*IsDeduced*/true, Deduced, Info, Builder,
          CurrentInstantiationScope, NumExplicitlySpecified,
          PartialOverloading))
    return Result;

  // C++ [temp.deduct.call]p10: [DR1391]
  //   If deduction succeeds for all parameters that contain
  //   template-parameters that participate in template argument deduction,
  //   and all template arguments are explicitly specified, deduced, or
  //   obtained from default template arguments, remaining parameters are then
  //   compared with the corresponding arguments. For each remaining parameter
  //   P with a type that was non-dependent before substitution of any
  //   explicitly-specified template arguments, if the corresponding argument
  //   A cannot be implicitly converted to P, deduction fails.
  if (CheckNonDependent())
    return TDK_NonDependentConversionFailure;

  // Form the template argument list from the deduced template arguments.
  TemplateArgumentList *DeducedArgumentList
    = TemplateArgumentList::CreateCopy(Context, Builder);
  Info.reset(DeducedArgumentList);

  // Substitute the deduced template arguments into the function template
  // declaration to produce the function template specialization.
  DeclContext *Owner = FunctionTemplate->getDeclContext();
  if (FunctionTemplate->getFriendObjectKind())
    Owner = FunctionTemplate->getLexicalDeclContext();
  MultiLevelTemplateArgumentList SubstArgs(*DeducedArgumentList);
  Specialization = cast_or_null<FunctionDecl>(
      SubstDecl(FunctionTemplate->getTemplatedDecl(), Owner, SubstArgs));
  if (!Specialization || Specialization->isInvalidDecl())
    return TDK_SubstitutionFailure;

  assert(Specialization->getPrimaryTemplate()->getCanonicalDecl() ==
         FunctionTemplate->getCanonicalDecl());

  // If the template argument list is owned by the function template
  // specialization, release it.
  if (Specialization->getTemplateSpecializationArgs() == DeducedArgumentList &&
      !Trap.hasErrorOccurred())
    Info.take();

  // There may have been an error that did not prevent us from constructing a
  // declaration. Mark the declaration invalid and return with a substitution
  // failure.
  if (Trap.hasErrorOccurred()) {
    Specialization->setInvalidDecl(true);
    return TDK_SubstitutionFailure;
  }

  if (OriginalCallArgs) {
    // C++ [temp.deduct.call]p4:
    //   In general, the deduction process attempts to find template argument
    //   values that will make the deduced A identical to A (after the type A
    //   is transformed as described above). [...]
    llvm::SmallDenseMap<std::pair<unsigned, QualType>, QualType> DeducedATypes;
    for (unsigned I = 0, N = OriginalCallArgs->size(); I != N; ++I) {
      OriginalCallArg OriginalArg = (*OriginalCallArgs)[I];

      auto ParamIdx = OriginalArg.ArgIdx;
      if (ParamIdx >= Specialization->getNumParams())
        // FIXME: This presumably means a pack ended up smaller than we
        // expected while deducing. Should this not result in deduction
        // failure? Can it even happen?
        continue;

      QualType DeducedA;
      if (!OriginalArg.DecomposedParam) {
        // P is one of the function parameters, just look up its substituted
        // type.
        DeducedA = Specialization->getParamDecl(ParamIdx)->getType();
      } else {
        // P is a decomposed element of a parameter corresponding to a
        // braced-init-list argument. Substitute back into P to find the
        // deduced A.
        QualType &CacheEntry =
            DeducedATypes[{ParamIdx, OriginalArg.OriginalParamType}];
        if (CacheEntry.isNull()) {
          ArgumentPackSubstitutionIndexRAII PackIndex(
              *this, getPackIndexForParam(*this, FunctionTemplate, SubstArgs,
                                          ParamIdx));
          CacheEntry =
              SubstType(OriginalArg.OriginalParamType, SubstArgs,
                        Specialization->getTypeSpecStartLoc(),
                        Specialization->getDeclName());
        }
        DeducedA = CacheEntry;
      }

      if (auto TDK =
              CheckOriginalCallArgDeduction(*this, Info, OriginalArg, DeducedA))
        return TDK;
    }
  }

  // If we suppressed any diagnostics while performing template argument
  // deduction, and if we haven't already instantiated this declaration,
  // keep track of these diagnostics. They'll be emitted if this specialization
  // is actually used.
  if (Info.diag_begin() != Info.diag_end()) {
    SuppressedDiagnosticsMap::iterator
      Pos = SuppressedDiagnostics.find(Specialization->getCanonicalDecl());
    if (Pos == SuppressedDiagnostics.end())
        SuppressedDiagnostics[Specialization->getCanonicalDecl()]
          .append(Info.diag_begin(), Info.diag_end());
  }

  return TDK_Success;
}

/// Gets the type of a function for template-argument-deducton
/// purposes when it's considered as part of an overload set.
static QualType GetTypeOfFunction(Sema &S, const OverloadExpr::FindResult &R,
                                  FunctionDecl *Fn) {
  // We may need to deduce the return type of the function now.
  if (S.getLangOpts().CPlusPlus14 && Fn->getReturnType()->isUndeducedType() &&
      S.DeduceReturnType(Fn, R.Expression->getExprLoc(), /*Diagnose*/ false))
    return {};

  if (CXXMethodDecl *Method = dyn_cast<CXXMethodDecl>(Fn))
    if (Method->isInstance()) {
      // An instance method that's referenced in a form that doesn't
      // look like a member pointer is just invalid.
      if (!R.HasFormOfMemberPointer)
        return {};

      return S.Context.getMemberPointerType(Fn->getType(),
               S.Context.getTypeDeclType(Method->getParent()).getTypePtr());
    }

  if (!R.IsAddressOfOperand) return Fn->getType();
  return S.Context.getPointerType(Fn->getType());
}

/// Apply the deduction rules for overload sets.
///
/// \return the null type if this argument should be treated as an
/// undeduced context
static QualType
ResolveOverloadForDeduction(Sema &S, TemplateParameterList *TemplateParams,
                            Expr *Arg, QualType ParamType,
                            bool ParamWasReference) {

  OverloadExpr::FindResult R = OverloadExpr::find(Arg);

  OverloadExpr *Ovl = R.Expression;

  // C++0x [temp.deduct.call]p4
  unsigned TDF = 0;
  if (ParamWasReference)
    TDF |= TDF_ParamWithReferenceType;
  if (R.IsAddressOfOperand)
    TDF |= TDF_IgnoreQualifiers;

  // C++0x [temp.deduct.call]p6:
  //   When P is a function type, pointer to function type, or pointer
  //   to member function type:

  if (!ParamType->isFunctionType() &&
      !ParamType->isFunctionPointerType() &&
      !ParamType->isMemberFunctionPointerType()) {
    if (Ovl->hasExplicitTemplateArgs()) {
      // But we can still look for an explicit specialization.
      if (FunctionDecl *ExplicitSpec
            = S.ResolveSingleFunctionTemplateSpecialization(Ovl))
        return GetTypeOfFunction(S, R, ExplicitSpec);
    }

    DeclAccessPair DAP;
    if (FunctionDecl *Viable =
            S.resolveAddressOfOnlyViableOverloadCandidate(Arg, DAP))
      return GetTypeOfFunction(S, R, Viable);

    return {};
  }

  // Gather the explicit template arguments, if any.
  TemplateArgumentListInfo ExplicitTemplateArgs;
  if (Ovl->hasExplicitTemplateArgs())
    Ovl->copyTemplateArgumentsInto(ExplicitTemplateArgs);
  QualType Match;
  for (UnresolvedSetIterator I = Ovl->decls_begin(),
         E = Ovl->decls_end(); I != E; ++I) {
    NamedDecl *D = (*I)->getUnderlyingDecl();

    if (FunctionTemplateDecl *FunTmpl = dyn_cast<FunctionTemplateDecl>(D)) {
      //   - If the argument is an overload set containing one or more
      //     function templates, the parameter is treated as a
      //     non-deduced context.
      if (!Ovl->hasExplicitTemplateArgs())
        return {};

      // Otherwise, see if we can resolve a function type
      FunctionDecl *Specialization = nullptr;
      TemplateDeductionInfo Info(Ovl->getNameLoc());
      if (S.DeduceTemplateArguments(FunTmpl, &ExplicitTemplateArgs,
                                    Specialization, Info))
        continue;

      D = Specialization;
    }

    FunctionDecl *Fn = cast<FunctionDecl>(D);
    QualType ArgType = GetTypeOfFunction(S, R, Fn);
    if (ArgType.isNull()) continue;

    // Function-to-pointer conversion.
    if (!ParamWasReference && ParamType->isPointerType() &&
        ArgType->isFunctionType())
      ArgType = S.Context.getPointerType(ArgType);

    //   - If the argument is an overload set (not containing function
    //     templates), trial argument deduction is attempted using each
    //     of the members of the set. If deduction succeeds for only one
    //     of the overload set members, that member is used as the
    //     argument value for the deduction. If deduction succeeds for
    //     more than one member of the overload set the parameter is
    //     treated as a non-deduced context.

    // We do all of this in a fresh context per C++0x [temp.deduct.type]p2:
    //   Type deduction is done independently for each P/A pair, and
    //   the deduced template argument values are then combined.
    // So we do not reject deductions which were made elsewhere.
    SmallVector<DeducedTemplateArgument, 8>
      Deduced(TemplateParams->size());
    TemplateDeductionInfo Info(Ovl->getNameLoc());
    Sema::TemplateDeductionResult Result
      = DeduceTemplateArgumentsByTypeMatch(S, TemplateParams, ParamType,
                                           ArgType, Info, Deduced, TDF);
    if (Result) continue;
    if (!Match.isNull())
      return {};
    Match = ArgType;
  }

  return Match;
}

/// Perform the adjustments to the parameter and argument types
/// described in C++ [temp.deduct.call].
///
/// \returns true if the caller should not attempt to perform any template
/// argument deduction based on this P/A pair because the argument is an
/// overloaded function set that could not be resolved.
static bool AdjustFunctionParmAndArgTypesForDeduction(
    Sema &S, TemplateParameterList *TemplateParams, unsigned FirstInnerIndex,
    QualType &ParamType, QualType &ArgType, Expr *Arg, unsigned &TDF) {
  // C++0x [temp.deduct.call]p3:
  //   If P is a cv-qualified type, the top level cv-qualifiers of P's type
  //   are ignored for type deduction.
  if (ParamType.hasQualifiers())
    ParamType = ParamType.getUnqualifiedType();

  //   [...] If P is a reference type, the type referred to by P is
  //   used for type deduction.
  const ReferenceType *ParamRefType = ParamType->getAs<ReferenceType>();
  if (ParamRefType)
    ParamType = ParamRefType->getPointeeType();

  // Overload sets usually make this parameter an undeduced context,
  // but there are sometimes special circumstances.  Typically
  // involving a template-id-expr.
  if (ArgType == S.Context.OverloadTy) {
    ArgType = ResolveOverloadForDeduction(S, TemplateParams,
                                          Arg, ParamType,
                                          ParamRefType != nullptr);
    if (ArgType.isNull())
      return true;
  }

  if (ParamRefType) {
    // If the argument has incomplete array type, try to complete its type.
    if (ArgType->isIncompleteArrayType()) {
      S.completeExprArrayBound(Arg);
      ArgType = Arg->getType();
    }

    // C++1z [temp.deduct.call]p3:
    //   If P is a forwarding reference and the argument is an lvalue, the type
    //   "lvalue reference to A" is used in place of A for type deduction.
    if (isForwardingReference(QualType(ParamRefType, 0), FirstInnerIndex) &&
        Arg->isLValue())
      ArgType = S.Context.getLValueReferenceType(ArgType);
  } else {
    // C++ [temp.deduct.call]p2:
    //   If P is not a reference type:
    //   - If A is an array type, the pointer type produced by the
    //     array-to-pointer standard conversion (4.2) is used in place of
    //     A for type deduction; otherwise,
    if (ArgType->isArrayType())
      ArgType = S.Context.getArrayDecayedType(ArgType);
    //   - If A is a function type, the pointer type produced by the
    //     function-to-pointer standard conversion (4.3) is used in place
    //     of A for type deduction; otherwise,
    else if (ArgType->isFunctionType())
      ArgType = S.Context.getPointerType(ArgType);
    else {
      // - If A is a cv-qualified type, the top level cv-qualifiers of A's
      //   type are ignored for type deduction.
      ArgType = ArgType.getUnqualifiedType();
    }
  }

  // C++0x [temp.deduct.call]p4:
  //   In general, the deduction process attempts to find template argument
  //   values that will make the deduced A identical to A (after the type A
  //   is transformed as described above). [...]
  TDF = TDF_SkipNonDependent;

  //     - If the original P is a reference type, the deduced A (i.e., the
  //       type referred to by the reference) can be more cv-qualified than
  //       the transformed A.
  if (ParamRefType)
    TDF |= TDF_ParamWithReferenceType;
  //     - The transformed A can be another pointer or pointer to member
  //       type that can be converted to the deduced A via a qualification
  //       conversion (4.4).
  if (ArgType->isPointerType() || ArgType->isMemberPointerType() ||
      ArgType->isObjCObjectPointerType())
    TDF |= TDF_IgnoreQualifiers;
  //     - If P is a class and P has the form simple-template-id, then the
  //       transformed A can be a derived class of the deduced A. Likewise,
  //       if P is a pointer to a class of the form simple-template-id, the
  //       transformed A can be a pointer to a derived class pointed to by
  //       the deduced A.
  if (isSimpleTemplateIdType(ParamType) ||
      (isa<PointerType>(ParamType) &&
       isSimpleTemplateIdType(
                              ParamType->getAs<PointerType>()->getPointeeType())))
    TDF |= TDF_DerivedClass;

  return false;
}

static bool
hasDeducibleTemplateParameters(Sema &S, FunctionTemplateDecl *FunctionTemplate,
                               QualType T);

static Sema::TemplateDeductionResult DeduceTemplateArgumentsFromCallArgument(
    Sema &S, TemplateParameterList *TemplateParams, unsigned FirstInnerIndex,
    QualType ParamType, Expr *Arg, TemplateDeductionInfo &Info,
    SmallVectorImpl<DeducedTemplateArgument> &Deduced,
    SmallVectorImpl<Sema::OriginalCallArg> &OriginalCallArgs,
    bool DecomposedParam, unsigned ArgIdx, unsigned TDF);

/// Attempt template argument deduction from an initializer list
///        deemed to be an argument in a function call.
static Sema::TemplateDeductionResult DeduceFromInitializerList(
    Sema &S, TemplateParameterList *TemplateParams, QualType AdjustedParamType,
    InitListExpr *ILE, TemplateDeductionInfo &Info,
    SmallVectorImpl<DeducedTemplateArgument> &Deduced,
    SmallVectorImpl<Sema::OriginalCallArg> &OriginalCallArgs, unsigned ArgIdx,
    unsigned TDF) {
  // C++ [temp.deduct.call]p1: (CWG 1591)
  //   If removing references and cv-qualifiers from P gives
  //   std::initializer_list<P0> or P0[N] for some P0 and N and the argument is
  //   a non-empty initializer list, then deduction is performed instead for
  //   each element of the initializer list, taking P0 as a function template
  //   parameter type and the initializer element as its argument
  //
  // We've already removed references and cv-qualifiers here.
  if (!ILE->getNumInits())
    return Sema::TDK_Success;

  QualType ElTy;
  auto *ArrTy = S.Context.getAsArrayType(AdjustedParamType);
  if (ArrTy)
    ElTy = ArrTy->getElementType();
  else if (!S.isStdInitializerList(AdjustedParamType, &ElTy)) {
    //   Otherwise, an initializer list argument causes the parameter to be
    //   considered a non-deduced context
    return Sema::TDK_Success;
  }

  // Deduction only needs to be done for dependent types.
  if (ElTy->isDependentType()) {
    for (Expr *E : ILE->inits()) {
      if (auto Result = DeduceTemplateArgumentsFromCallArgument(
              S, TemplateParams, 0, ElTy, E, Info, Deduced, OriginalCallArgs, true,
              ArgIdx, TDF))
        return Result;
    }
  }

  //   in the P0[N] case, if N is a non-type template parameter, N is deduced
  //   from the length of the initializer list.
  if (auto *DependentArrTy = dyn_cast_or_null<DependentSizedArrayType>(ArrTy)) {
    // Determine the array bound is something we can deduce.
    if (NonTypeTemplateParmDecl *NTTP =
            getDeducedParameterFromExpr(Info, DependentArrTy->getSizeExpr())) {
      // We can perform template argument deduction for the given non-type
      // template parameter.
      // C++ [temp.deduct.type]p13:
      //   The type of N in the type T[N] is std::size_t.
      QualType T = S.Context.getSizeType();
      llvm::APInt Size(S.Context.getIntWidth(T), ILE->getNumInits());
      if (auto Result = DeduceNonTypeTemplateArgument(
              S, TemplateParams, NTTP, llvm::APSInt(Size), T,
              /*ArrayBound=*/true, Info, Deduced))
        return Result;
    }
  }

  return Sema::TDK_Success;
}

/// Perform template argument deduction per [temp.deduct.call] for a
///        single parameter / argument pair.
static Sema::TemplateDeductionResult DeduceTemplateArgumentsFromCallArgument(
    Sema &S, TemplateParameterList *TemplateParams, unsigned FirstInnerIndex,
    QualType ParamType, Expr *Arg, TemplateDeductionInfo &Info,
    SmallVectorImpl<DeducedTemplateArgument> &Deduced,
    SmallVectorImpl<Sema::OriginalCallArg> &OriginalCallArgs,
    bool DecomposedParam, unsigned ArgIdx, unsigned TDF) {
  QualType ArgType = Arg->getType();
  QualType OrigParamType = ParamType;

  //   If P is a reference type [...]
  //   If P is a cv-qualified type [...]
  if (AdjustFunctionParmAndArgTypesForDeduction(
          S, TemplateParams, FirstInnerIndex, ParamType, ArgType, Arg, TDF))
    return Sema::TDK_Success;

  //   If [...] the argument is a non-empty initializer list [...]
  if (InitListExpr *ILE = dyn_cast<InitListExpr>(Arg))
    return DeduceFromInitializerList(S, TemplateParams, ParamType, ILE, Info,
                                     Deduced, OriginalCallArgs, ArgIdx, TDF);

  //   [...] the deduction process attempts to find template argument values
  //   that will make the deduced A identical to A
  //
  // Keep track of the argument type and corresponding parameter index,
  // so we can check for compatibility between the deduced A and A.
  OriginalCallArgs.push_back(
      Sema::OriginalCallArg(OrigParamType, DecomposedParam, ArgIdx, ArgType));
  return DeduceTemplateArgumentsByTypeMatch(S, TemplateParams, ParamType,
                                            ArgType, Info, Deduced, TDF);
}

/// Perform template argument deduction from a function call
/// (C++ [temp.deduct.call]).
///
/// \param FunctionTemplate the function template for which we are performing
/// template argument deduction.
///
/// \param ExplicitTemplateArgs the explicit template arguments provided
/// for this call.
///
/// \param Args the function call arguments
///
/// \param Specialization if template argument deduction was successful,
/// this will be set to the function template specialization produced by
/// template argument deduction.
///
/// \param Info the argument will be updated to provide additional information
/// about template argument deduction.
///
/// \param CheckNonDependent A callback to invoke to check conversions for
/// non-dependent parameters, between deduction and substitution, per DR1391.
/// If this returns true, substitution will be skipped and we return
/// TDK_NonDependentConversionFailure. The callback is passed the parameter
/// types (after substituting explicit template arguments).
///
/// \returns the result of template argument deduction.
Sema::TemplateDeductionResult Sema::DeduceTemplateArguments(
    FunctionTemplateDecl *FunctionTemplate,
    TemplateArgumentListInfo *ExplicitTemplateArgs, ArrayRef<Expr *> Args,
    FunctionDecl *&Specialization, TemplateDeductionInfo &Info,
    bool PartialOverloading,
    llvm::function_ref<bool(ArrayRef<QualType>)> CheckNonDependent) {
  if (FunctionTemplate->isInvalidDecl())
    return TDK_Invalid;

  FunctionDecl *Function = FunctionTemplate->getTemplatedDecl();
  unsigned NumParams = Function->getNumParams();

  unsigned FirstInnerIndex = getFirstInnerIndex(FunctionTemplate);

  // C++ [temp.deduct.call]p1:
  //   Template argument deduction is done by comparing each function template
  //   parameter type (call it P) with the type of the corresponding argument
  //   of the call (call it A) as described below.
  if (Args.size() < Function->getMinRequiredArguments() && !PartialOverloading)
    return TDK_TooFewArguments;
  else if (TooManyArguments(NumParams, Args.size(), PartialOverloading)) {
    const FunctionProtoType *Proto
      = Function->getType()->getAs<FunctionProtoType>();
    if (Proto->isTemplateVariadic())
      /* Do nothing */;
    else if (!Proto->isVariadic())
      return TDK_TooManyArguments;
  }

  // The types of the parameters from which we will perform template argument
  // deduction.
  LocalInstantiationScope InstScope(*this);
  TemplateParameterList *TemplateParams
    = FunctionTemplate->getTemplateParameters();
  SmallVector<DeducedTemplateArgument, 4> Deduced;
  SmallVector<QualType, 8> ParamTypes;
  unsigned NumExplicitlySpecified = 0;
  if (ExplicitTemplateArgs) {
    TemplateDeductionResult Result =
      SubstituteExplicitTemplateArguments(FunctionTemplate,
                                          *ExplicitTemplateArgs,
                                          Deduced,
                                          ParamTypes,
                                          nullptr,
                                          Info);
    if (Result)
      return Result;

    NumExplicitlySpecified = Deduced.size();
  } else {
    // Just fill in the parameter types from the function declaration.
    for (unsigned I = 0; I != NumParams; ++I)
      ParamTypes.push_back(Function->getParamDecl(I)->getType());
  }

  SmallVector<OriginalCallArg, 8> OriginalCallArgs;

  // Deduce an argument of type ParamType from an expression with index ArgIdx.
  auto DeduceCallArgument = [&](QualType ParamType, unsigned ArgIdx) {
    // C++ [demp.deduct.call]p1: (DR1391)
    //   Template argument deduction is done by comparing each function template
    //   parameter that contains template-parameters that participate in
    //   template argument deduction ...
    if (!hasDeducibleTemplateParameters(*this, FunctionTemplate, ParamType))
      return Sema::TDK_Success;

    //   ... with the type of the corresponding argument
    return DeduceTemplateArgumentsFromCallArgument(
        *this, TemplateParams, FirstInnerIndex, ParamType, Args[ArgIdx], Info, Deduced,
        OriginalCallArgs, /*Decomposed*/false, ArgIdx, /*TDF*/ 0);
  };

  // Deduce template arguments from the function parameters.
  Deduced.resize(TemplateParams->size());
  SmallVector<QualType, 8> ParamTypesForArgChecking;
  for (unsigned ParamIdx = 0, NumParamTypes = ParamTypes.size(), ArgIdx = 0;
       ParamIdx != NumParamTypes; ++ParamIdx) {
    QualType ParamType = ParamTypes[ParamIdx];

    const PackExpansionType *ParamExpansion =
        dyn_cast<PackExpansionType>(ParamType);
    if (!ParamExpansion) {
      // Simple case: matching a function parameter to a function argument.
      if (ArgIdx >= Args.size())
        break;

      ParamTypesForArgChecking.push_back(ParamType);
      if (auto Result = DeduceCallArgument(ParamType, ArgIdx++))
        return Result;

      continue;
    }

    QualType ParamPattern = ParamExpansion->getPattern();
    PackDeductionScope PackScope(*this, TemplateParams, Deduced, Info,
                                 ParamPattern);

    // C++0x [temp.deduct.call]p1:
    //   For a function parameter pack that occurs at the end of the
    //   parameter-declaration-list, the type A of each remaining argument of
    //   the call is compared with the type P of the declarator-id of the
    //   function parameter pack. Each comparison deduces template arguments
    //   for subsequent positions in the template parameter packs expanded by
    //   the function parameter pack. When a function parameter pack appears
    //   in a non-deduced context [not at the end of the list], the type of
    //   that parameter pack is never deduced.
    //
    // FIXME: The above rule allows the size of the parameter pack to change
    // after we skip it (in the non-deduced case). That makes no sense, so
    // we instead notionally deduce the pack against N arguments, where N is
    // the length of the explicitly-specified pack if it's expanded by the
    // parameter pack and 0 otherwise, and we treat each deduction as a
    // non-deduced context.
    if (ParamIdx + 1 == NumParamTypes || PackScope.hasFixedArity()) {
      for (; ArgIdx < Args.size() && PackScope.hasNextElement();
           PackScope.nextPackElement(), ++ArgIdx) {
        ParamTypesForArgChecking.push_back(ParamPattern);
        if (auto Result = DeduceCallArgument(ParamPattern, ArgIdx))
          return Result;
      }
    } else {
      // If the parameter type contains an explicitly-specified pack that we
      // could not expand, skip the number of parameters notionally created
      // by the expansion.
      Optional<unsigned> NumExpansions = ParamExpansion->getNumExpansions();
      if (NumExpansions && !PackScope.isPartiallyExpanded()) {
        for (unsigned I = 0; I != *NumExpansions && ArgIdx < Args.size();
             ++I, ++ArgIdx) {
          ParamTypesForArgChecking.push_back(ParamPattern);
          // FIXME: Should we add OriginalCallArgs for these? What if the
          // corresponding argument is a list?
          PackScope.nextPackElement();
        }
      }
    }

    // Build argument packs for each of the parameter packs expanded by this
    // pack expansion.
    if (auto Result = PackScope.finish())
      return Result;
  }

  // Capture the context in which the function call is made. This is the context
  // that is needed when the accessibility of template arguments is checked.
  DeclContext *CallingCtx = CurContext;

  return FinishTemplateArgumentDeduction(
      FunctionTemplate, Deduced, NumExplicitlySpecified, Specialization, Info,
      &OriginalCallArgs, PartialOverloading, [&, CallingCtx]() {
        ContextRAII SavedContext(*this, CallingCtx);
        return CheckNonDependent(ParamTypesForArgChecking);
      });
}

QualType Sema::adjustCCAndNoReturn(QualType ArgFunctionType,
                                   QualType FunctionType,
                                   bool AdjustExceptionSpec) {
  if (ArgFunctionType.isNull())
    return ArgFunctionType;

  const FunctionProtoType *FunctionTypeP =
      FunctionType->castAs<FunctionProtoType>();
  const FunctionProtoType *ArgFunctionTypeP =
      ArgFunctionType->getAs<FunctionProtoType>();

  FunctionProtoType::ExtProtoInfo EPI = ArgFunctionTypeP->getExtProtoInfo();
  bool Rebuild = false;

  CallingConv CC = FunctionTypeP->getCallConv();
  if (EPI.ExtInfo.getCC() != CC) {
    EPI.ExtInfo = EPI.ExtInfo.withCallingConv(CC);
    Rebuild = true;
  }

  bool NoReturn = FunctionTypeP->getNoReturnAttr();
  if (EPI.ExtInfo.getNoReturn() != NoReturn) {
    EPI.ExtInfo = EPI.ExtInfo.withNoReturn(NoReturn);
    Rebuild = true;
  }

  if (AdjustExceptionSpec && (FunctionTypeP->hasExceptionSpec() ||
                              ArgFunctionTypeP->hasExceptionSpec())) {
    EPI.ExceptionSpec = FunctionTypeP->getExtProtoInfo().ExceptionSpec;
    Rebuild = true;
  }

  if (!Rebuild)
    return ArgFunctionType;

  return Context.getFunctionType(ArgFunctionTypeP->getReturnType(),
                                 ArgFunctionTypeP->getParamTypes(), EPI);
}

/// Deduce template arguments when taking the address of a function
/// template (C++ [temp.deduct.funcaddr]) or matching a specialization to
/// a template.
///
/// \param FunctionTemplate the function template for which we are performing
/// template argument deduction.
///
/// \param ExplicitTemplateArgs the explicitly-specified template
/// arguments.
///
/// \param ArgFunctionType the function type that will be used as the
/// "argument" type (A) when performing template argument deduction from the
/// function template's function type. This type may be NULL, if there is no
/// argument type to compare against, in C++0x [temp.arg.explicit]p3.
///
/// \param Specialization if template argument deduction was successful,
/// this will be set to the function template specialization produced by
/// template argument deduction.
///
/// \param Info the argument will be updated to provide additional information
/// about template argument deduction.
///
/// \param IsAddressOfFunction If \c true, we are deducing as part of taking
/// the address of a function template per [temp.deduct.funcaddr] and
/// [over.over]. If \c false, we are looking up a function template
/// specialization based on its signature, per [temp.deduct.decl].
///
/// \returns the result of template argument deduction.
Sema::TemplateDeductionResult Sema::DeduceTemplateArguments(
    FunctionTemplateDecl *FunctionTemplate,
    TemplateArgumentListInfo *ExplicitTemplateArgs, QualType ArgFunctionType,
    FunctionDecl *&Specialization, TemplateDeductionInfo &Info,
    bool IsAddressOfFunction) {
  if (FunctionTemplate->isInvalidDecl())
    return TDK_Invalid;

  FunctionDecl *Function = FunctionTemplate->getTemplatedDecl();
  TemplateParameterList *TemplateParams
    = FunctionTemplate->getTemplateParameters();
  QualType FunctionType = Function->getType();

  // Substitute any explicit template arguments.
  LocalInstantiationScope InstScope(*this);
  SmallVector<DeducedTemplateArgument, 4> Deduced;
  unsigned NumExplicitlySpecified = 0;
  SmallVector<QualType, 4> ParamTypes;
  if (ExplicitTemplateArgs) {
    if (TemplateDeductionResult Result
          = SubstituteExplicitTemplateArguments(FunctionTemplate,
                                                *ExplicitTemplateArgs,
                                                Deduced, ParamTypes,
                                                &FunctionType, Info))
      return Result;

    NumExplicitlySpecified = Deduced.size();
  }

  // When taking the address of a function, we require convertibility of
  // the resulting function type. Otherwise, we allow arbitrary mismatches
  // of calling convention and noreturn.
  if (!IsAddressOfFunction)
    ArgFunctionType = adjustCCAndNoReturn(ArgFunctionType, FunctionType,
                                          /*AdjustExceptionSpec*/false);

  // Unevaluated SFINAE context.
  EnterExpressionEvaluationContext Unevaluated(
      *this, Sema::ExpressionEvaluationContext::Unevaluated);
  SFINAETrap Trap(*this);

  Deduced.resize(TemplateParams->size());

  // If the function has a deduced return type, substitute it for a dependent
  // type so that we treat it as a non-deduced context in what follows. If we
  // are looking up by signature, the signature type should also have a deduced
  // return type, which we instead expect to exactly match.
  bool HasDeducedReturnType = false;
  if (getLangOpts().CPlusPlus14 && IsAddressOfFunction &&
      Function->getReturnType()->getContainedAutoType()) {
    FunctionType = SubstAutoType(FunctionType, Context.DependentTy);
    HasDeducedReturnType = true;
  }

  if (!ArgFunctionType.isNull()) {
    unsigned TDF =
        TDF_TopLevelParameterTypeList | TDF_AllowCompatibleFunctionType;
    // Deduce template arguments from the function type.
    if (TemplateDeductionResult Result
          = DeduceTemplateArgumentsByTypeMatch(*this, TemplateParams,
                                               FunctionType, ArgFunctionType,
                                               Info, Deduced, TDF))
      return Result;
  }

  if (TemplateDeductionResult Result
        = FinishTemplateArgumentDeduction(FunctionTemplate, Deduced,
                                          NumExplicitlySpecified,
                                          Specialization, Info))
    return Result;

  // If the function has a deduced return type, deduce it now, so we can check
  // that the deduced function type matches the requested type.
  if (HasDeducedReturnType &&
      Specialization->getReturnType()->isUndeducedType() &&
      DeduceReturnType(Specialization, Info.getLocation(), false))
    return TDK_MiscellaneousDeductionFailure;

  // If the function has a dependent exception specification, resolve it now,
  // so we can check that the exception specification matches.
  auto *SpecializationFPT =
      Specialization->getType()->castAs<FunctionProtoType>();
  if (getLangOpts().CPlusPlus17 &&
      isUnresolvedExceptionSpec(SpecializationFPT->getExceptionSpecType()) &&
      !ResolveExceptionSpec(Info.getLocation(), SpecializationFPT))
    return TDK_MiscellaneousDeductionFailure;

  // Adjust the exception specification of the argument to match the
  // substituted and resolved type we just formed. (Calling convention and
  // noreturn can't be dependent, so we don't actually need this for them
  // right now.)
  QualType SpecializationType = Specialization->getType();
  if (!IsAddressOfFunction)
    ArgFunctionType = adjustCCAndNoReturn(ArgFunctionType, SpecializationType,
                                          /*AdjustExceptionSpec*/true);

  // If the requested function type does not match the actual type of the
  // specialization with respect to arguments of compatible pointer to function
  // types, template argument deduction fails.
  if (!ArgFunctionType.isNull()) {
    if (IsAddressOfFunction &&
        !isSameOrCompatibleFunctionType(
            Context.getCanonicalType(SpecializationType),
            Context.getCanonicalType(ArgFunctionType)))
      return TDK_MiscellaneousDeductionFailure;

    if (!IsAddressOfFunction &&
        !Context.hasSameType(SpecializationType, ArgFunctionType))
      return TDK_MiscellaneousDeductionFailure;
  }

  return TDK_Success;
}

/// Deduce template arguments for a templated conversion
/// function (C++ [temp.deduct.conv]) and, if successful, produce a
/// conversion function template specialization.
Sema::TemplateDeductionResult
Sema::DeduceTemplateArguments(FunctionTemplateDecl *ConversionTemplate,
                              QualType ToType,
                              CXXConversionDecl *&Specialization,
                              TemplateDeductionInfo &Info) {
  if (ConversionTemplate->isInvalidDecl())
    return TDK_Invalid;

  CXXConversionDecl *ConversionGeneric
    = cast<CXXConversionDecl>(ConversionTemplate->getTemplatedDecl());

  QualType FromType = ConversionGeneric->getConversionType();

  // Canonicalize the types for deduction.
  QualType P = Context.getCanonicalType(FromType);
  QualType A = Context.getCanonicalType(ToType);

  // C++0x [temp.deduct.conv]p2:
  //   If P is a reference type, the type referred to by P is used for
  //   type deduction.
  if (const ReferenceType *PRef = P->getAs<ReferenceType>())
    P = PRef->getPointeeType();

  // C++0x [temp.deduct.conv]p4:
  //   [...] If A is a reference type, the type referred to by A is used
  //   for type deduction.
  if (const ReferenceType *ARef = A->getAs<ReferenceType>()) {
    A = ARef->getPointeeType();
    // We work around a defect in the standard here: cv-qualifiers are also
    // removed from P and A in this case, unless P was a reference type. This
    // seems to mostly match what other compilers are doing.
    if (!FromType->getAs<ReferenceType>()) {
      A = A.getUnqualifiedType();
      P = P.getUnqualifiedType();
    }

  // C++ [temp.deduct.conv]p3:
  //
  //   If A is not a reference type:
  } else {
    assert(!A->isReferenceType() && "Reference types were handled above");

    //   - If P is an array type, the pointer type produced by the
    //     array-to-pointer standard conversion (4.2) is used in place
    //     of P for type deduction; otherwise,
    if (P->isArrayType())
      P = Context.getArrayDecayedType(P);
    //   - If P is a function type, the pointer type produced by the
    //     function-to-pointer standard conversion (4.3) is used in
    //     place of P for type deduction; otherwise,
    else if (P->isFunctionType())
      P = Context.getPointerType(P);
    //   - If P is a cv-qualified type, the top level cv-qualifiers of
    //     P's type are ignored for type deduction.
    else
      P = P.getUnqualifiedType();

    // C++0x [temp.deduct.conv]p4:
    //   If A is a cv-qualified type, the top level cv-qualifiers of A's
    //   type are ignored for type deduction. If A is a reference type, the type
    //   referred to by A is used for type deduction.
    A = A.getUnqualifiedType();
  }

  // Unevaluated SFINAE context.
  EnterExpressionEvaluationContext Unevaluated(
      *this, Sema::ExpressionEvaluationContext::Unevaluated);
  SFINAETrap Trap(*this);

  // C++ [temp.deduct.conv]p1:
  //   Template argument deduction is done by comparing the return
  //   type of the template conversion function (call it P) with the
  //   type that is required as the result of the conversion (call it
  //   A) as described in 14.8.2.4.
  TemplateParameterList *TemplateParams
    = ConversionTemplate->getTemplateParameters();
  SmallVector<DeducedTemplateArgument, 4> Deduced;
  Deduced.resize(TemplateParams->size());

  // C++0x [temp.deduct.conv]p4:
  //   In general, the deduction process attempts to find template
  //   argument values that will make the deduced A identical to
  //   A. However, there are two cases that allow a difference:
  unsigned TDF = 0;
  //     - If the original A is a reference type, A can be more
  //       cv-qualified than the deduced A (i.e., the type referred to
  //       by the reference)
  if (ToType->isReferenceType())
    TDF |= TDF_ArgWithReferenceType;
  //     - The deduced A can be another pointer or pointer to member
  //       type that can be converted to A via a qualification
  //       conversion.
  //
  // (C++0x [temp.deduct.conv]p6 clarifies that this only happens when
  // both P and A are pointers or member pointers. In this case, we
  // just ignore cv-qualifiers completely).
  if ((P->isPointerType() && A->isPointerType()) ||
      (P->isMemberPointerType() && A->isMemberPointerType()))
    TDF |= TDF_IgnoreQualifiers;
  if (TemplateDeductionResult Result
        = DeduceTemplateArgumentsByTypeMatch(*this, TemplateParams,
                                             P, A, Info, Deduced, TDF))
    return Result;

  // Create an Instantiation Scope for finalizing the operator.
  LocalInstantiationScope InstScope(*this);
  // Finish template argument deduction.
  FunctionDecl *ConversionSpecialized = nullptr;
  TemplateDeductionResult Result
      = FinishTemplateArgumentDeduction(ConversionTemplate, Deduced, 0,
                                        ConversionSpecialized, Info);
  Specialization = cast_or_null<CXXConversionDecl>(ConversionSpecialized);
  return Result;
}

/// Deduce template arguments for a function template when there is
/// nothing to deduce against (C++0x [temp.arg.explicit]p3).
///
/// \param FunctionTemplate the function template for which we are performing
/// template argument deduction.
///
/// \param ExplicitTemplateArgs the explicitly-specified template
/// arguments.
///
/// \param Specialization if template argument deduction was successful,
/// this will be set to the function template specialization produced by
/// template argument deduction.
///
/// \param Info the argument will be updated to provide additional information
/// about template argument deduction.
///
/// \param IsAddressOfFunction If \c true, we are deducing as part of taking
/// the address of a function template in a context where we do not have a
/// target type, per [over.over]. If \c false, we are looking up a function
/// template specialization based on its signature, which only happens when
/// deducing a function parameter type from an argument that is a template-id
/// naming a function template specialization.
///
/// \returns the result of template argument deduction.
Sema::TemplateDeductionResult Sema::DeduceTemplateArguments(
    FunctionTemplateDecl *FunctionTemplate,
    TemplateArgumentListInfo *ExplicitTemplateArgs,
    FunctionDecl *&Specialization, TemplateDeductionInfo &Info,
    bool IsAddressOfFunction) {
  return DeduceTemplateArguments(FunctionTemplate, ExplicitTemplateArgs,
                                 QualType(), Specialization, Info,
                                 IsAddressOfFunction);
}

namespace {

  /// Substitute the 'auto' specifier or deduced template specialization type
  /// specifier within a type for a given replacement type.
  class SubstituteDeducedTypeTransform :
      public TreeTransform<SubstituteDeducedTypeTransform> {
    QualType Replacement;
    bool UseTypeSugar;

  public:
    SubstituteDeducedTypeTransform(Sema &SemaRef, QualType Replacement,
                            bool UseTypeSugar = true)
        : TreeTransform<SubstituteDeducedTypeTransform>(SemaRef),
          Replacement(Replacement), UseTypeSugar(UseTypeSugar) {}

    QualType TransformDesugared(TypeLocBuilder &TLB, DeducedTypeLoc TL) {
      assert(isa<TemplateTypeParmType>(Replacement) &&
             "unexpected unsugared replacement kind");
      QualType Result = Replacement;
      TemplateTypeParmTypeLoc NewTL = TLB.push<TemplateTypeParmTypeLoc>(Result);
      NewTL.setNameLoc(TL.getNameLoc());
      return Result;
    }

    QualType TransformAutoType(TypeLocBuilder &TLB, AutoTypeLoc TL) {
      // If we're building the type pattern to deduce against, don't wrap the
      // substituted type in an AutoType. Certain template deduction rules
      // apply only when a template type parameter appears directly (and not if
      // the parameter is found through desugaring). For instance:
      //   auto &&lref = lvalue;
      // must transform into "rvalue reference to T" not "rvalue reference to
      // auto type deduced as T" in order for [temp.deduct.call]p3 to apply.
      //
      // FIXME: Is this still necessary?
      if (!UseTypeSugar)
        return TransformDesugared(TLB, TL);

      QualType Result = SemaRef.Context.getAutoType(
          Replacement, TL.getTypePtr()->getKeyword(), Replacement.isNull());
      auto NewTL = TLB.push<AutoTypeLoc>(Result);
      NewTL.setNameLoc(TL.getNameLoc());
      return Result;
    }

    QualType TransformDeducedTemplateSpecializationType(
        TypeLocBuilder &TLB, DeducedTemplateSpecializationTypeLoc TL) {
      if (!UseTypeSugar)
        return TransformDesugared(TLB, TL);

      QualType Result = SemaRef.Context.getDeducedTemplateSpecializationType(
          TL.getTypePtr()->getTemplateName(),
          Replacement, Replacement.isNull());
      auto NewTL = TLB.push<DeducedTemplateSpecializationTypeLoc>(Result);
      NewTL.setNameLoc(TL.getNameLoc());
      return Result;
    }

    ExprResult TransformLambdaExpr(LambdaExpr *E) {
      // Lambdas never need to be transformed.
      return E;
    }

    QualType Apply(TypeLoc TL) {
      // Create some scratch storage for the transformed type locations.
      // FIXME: We're just going to throw this information away. Don't build it.
      TypeLocBuilder TLB;
      TLB.reserve(TL.getFullDataSize());
      return TransformType(TLB, TL);
    }
  };

} // namespace

Sema::DeduceAutoResult
Sema::DeduceAutoType(TypeSourceInfo *Type, Expr *&Init, QualType &Result,
                     Optional<unsigned> DependentDeductionDepth) {
  return DeduceAutoType(Type->getTypeLoc(), Init, Result,
                        DependentDeductionDepth);
}

/// Attempt to produce an informative diagostic explaining why auto deduction
/// failed.
/// \return \c true if diagnosed, \c false if not.
static bool diagnoseAutoDeductionFailure(Sema &S,
                                         Sema::TemplateDeductionResult TDK,
                                         TemplateDeductionInfo &Info,
                                         ArrayRef<SourceRange> Ranges) {
  switch (TDK) {
  case Sema::TDK_Inconsistent: {
    // Inconsistent deduction means we were deducing from an initializer list.
    auto D = S.Diag(Info.getLocation(), diag::err_auto_inconsistent_deduction);
    D << Info.FirstArg << Info.SecondArg;
    for (auto R : Ranges)
      D << R;
    return true;
  }

  // FIXME: Are there other cases for which a custom diagnostic is more useful
  // than the basic "types don't match" diagnostic?

  default:
    return false;
  }
}

/// Deduce the type for an auto type-specifier (C++11 [dcl.spec.auto]p6)
///
/// Note that this is done even if the initializer is dependent. (This is
/// necessary to support partial ordering of templates using 'auto'.)
/// A dependent type will be produced when deducing from a dependent type.
///
/// \param Type the type pattern using the auto type-specifier.
/// \param Init the initializer for the variable whose type is to be deduced.
/// \param Result if type deduction was successful, this will be set to the
///        deduced type.
/// \param DependentDeductionDepth Set if we should permit deduction in
///        dependent cases. This is necessary for template partial ordering with
///        'auto' template parameters. The value specified is the template
///        parameter depth at which we should perform 'auto' deduction.
Sema::DeduceAutoResult
Sema::DeduceAutoType(TypeLoc Type, Expr *&Init, QualType &Result,
                     Optional<unsigned> DependentDeductionDepth) {
  if (Init->getType()->isNonOverloadPlaceholderType()) {
    ExprResult NonPlaceholder = CheckPlaceholderExpr(Init);
    if (NonPlaceholder.isInvalid())
      return DAR_FailedAlreadyDiagnosed;
    Init = NonPlaceholder.get();
  }

  if (!DependentDeductionDepth &&
      (Type.getType()->isDependentType() || Init->isTypeDependent())) {
    Result = SubstituteDeducedTypeTransform(*this, QualType()).Apply(Type);
    assert(!Result.isNull() && "substituting DependentTy can't fail");
    return DAR_Succeeded;
  }

  // Find the depth of template parameter to synthesize.
  unsigned Depth = DependentDeductionDepth.getValueOr(0);

  // If this is a 'decltype(auto)' specifier, do the decltype dance.
  // Since 'decltype(auto)' can only occur at the top of the type, we
  // don't need to go digging for it.
  if (const AutoType *AT = Type.getType()->getAs<AutoType>()) {
    if (AT->isDecltypeAuto()) {
      if (isa<InitListExpr>(Init)) {
        Diag(Init->getBeginLoc(), diag::err_decltype_auto_initializer_list);
        return DAR_FailedAlreadyDiagnosed;
      }

      ExprResult ER = CheckPlaceholderExpr(Init);
      if (ER.isInvalid())
        return DAR_FailedAlreadyDiagnosed;
      Init = ER.get();
      QualType Deduced = BuildDecltypeType(Init, Init->getBeginLoc(), false);
      if (Deduced.isNull())
        return DAR_FailedAlreadyDiagnosed;
      // FIXME: Support a non-canonical deduced type for 'auto'.
      Deduced = Context.getCanonicalType(Deduced);
      Result = SubstituteDeducedTypeTransform(*this, Deduced).Apply(Type);
      if (Result.isNull())
        return DAR_FailedAlreadyDiagnosed;
      return DAR_Succeeded;
    } else if (!getLangOpts().CPlusPlus) {
      if (isa<InitListExpr>(Init)) {
        Diag(Init->getBeginLoc(), diag::err_auto_init_list_from_c);
        return DAR_FailedAlreadyDiagnosed;
      }
    }
  }

  SourceLocation Loc = Init->getExprLoc();

  LocalInstantiationScope InstScope(*this);

  // Build template<class TemplParam> void Func(FuncParam);
  TemplateTypeParmDecl *TemplParam = TemplateTypeParmDecl::Create(
      Context, nullptr, SourceLocation(), Loc, Depth, 0, nullptr, false, false);
  QualType TemplArg = QualType(TemplParam->getTypeForDecl(), 0);
  NamedDecl *TemplParamPtr = TemplParam;
  FixedSizeTemplateParameterListStorage<1, false> TemplateParamsSt(
      Loc, Loc, TemplParamPtr, Loc, nullptr);

  QualType FuncParam =
      SubstituteDeducedTypeTransform(*this, TemplArg, /*UseTypeSugar*/false)
          .Apply(Type);
  assert(!FuncParam.isNull() &&
         "substituting template parameter for 'auto' failed");

  // Deduce type of TemplParam in Func(Init)
  SmallVector<DeducedTemplateArgument, 1> Deduced;
  Deduced.resize(1);

  TemplateDeductionInfo Info(Loc, Depth);

  // If deduction failed, don't diagnose if the initializer is dependent; it
  // might acquire a matching type in the instantiation.
  auto DeductionFailed = [&](TemplateDeductionResult TDK,
                             ArrayRef<SourceRange> Ranges) -> DeduceAutoResult {
    if (Init->isTypeDependent()) {
      Result = SubstituteDeducedTypeTransform(*this, QualType()).Apply(Type);
      assert(!Result.isNull() && "substituting DependentTy can't fail");
      return DAR_Succeeded;
    }
    if (diagnoseAutoDeductionFailure(*this, TDK, Info, Ranges))
      return DAR_FailedAlreadyDiagnosed;
    return DAR_Failed;
  };

  SmallVector<OriginalCallArg, 4> OriginalCallArgs;

  InitListExpr *InitList = dyn_cast<InitListExpr>(Init);
  if (InitList) {
    // Notionally, we substitute std::initializer_list<T> for 'auto' and deduce
    // against that. Such deduction only succeeds if removing cv-qualifiers and
    // references results in std::initializer_list<T>.
    if (!Type.getType().getNonReferenceType()->getAs<AutoType>())
      return DAR_Failed;

    SourceRange DeducedFromInitRange;
    for (unsigned i = 0, e = InitList->getNumInits(); i < e; ++i) {
      Expr *Init = InitList->getInit(i);

      if (auto TDK = DeduceTemplateArgumentsFromCallArgument(
              *this, TemplateParamsSt.get(), 0, TemplArg, Init,
              Info, Deduced, OriginalCallArgs, /*Decomposed*/ true,
              /*ArgIdx*/ 0, /*TDF*/ 0))
        return DeductionFailed(TDK, {DeducedFromInitRange,
                                     Init->getSourceRange()});

      if (DeducedFromInitRange.isInvalid() &&
          Deduced[0].getKind() != TemplateArgument::Null)
        DeducedFromInitRange = Init->getSourceRange();
    }
  } else {
    if (!getLangOpts().CPlusPlus && Init->refersToBitField()) {
      Diag(Loc, diag::err_auto_bitfield);
      return DAR_FailedAlreadyDiagnosed;
    }

    if (auto TDK = DeduceTemplateArgumentsFromCallArgument(
            *this, TemplateParamsSt.get(), 0, FuncParam, Init, Info, Deduced,
            OriginalCallArgs, /*Decomposed*/ false, /*ArgIdx*/ 0, /*TDF*/ 0))
      return DeductionFailed(TDK, {});
  }

  // Could be null if somehow 'auto' appears in a non-deduced context.
  if (Deduced[0].getKind() != TemplateArgument::Type)
    return DeductionFailed(TDK_Incomplete, {});

  QualType DeducedType = Deduced[0].getAsType();

  if (InitList) {
    DeducedType = BuildStdInitializerList(DeducedType, Loc);
    if (DeducedType.isNull())
      return DAR_FailedAlreadyDiagnosed;
  }

  Result = SubstituteDeducedTypeTransform(*this, DeducedType).Apply(Type);
  if (Result.isNull())
    return DAR_FailedAlreadyDiagnosed;

  // Check that the deduced argument type is compatible with the original
  // argument type per C++ [temp.deduct.call]p4.
  QualType DeducedA = InitList ? Deduced[0].getAsType() : Result;
  for (const OriginalCallArg &OriginalArg : OriginalCallArgs) {
    assert((bool)InitList == OriginalArg.DecomposedParam &&
           "decomposed non-init-list in auto deduction?");
    if (auto TDK =
            CheckOriginalCallArgDeduction(*this, Info, OriginalArg, DeducedA)) {
      Result = QualType();
      return DeductionFailed(TDK, {});
    }
  }

  return DAR_Succeeded;
}

QualType Sema::SubstAutoType(QualType TypeWithAuto,
                             QualType TypeToReplaceAuto) {
  if (TypeToReplaceAuto->isDependentType())
    TypeToReplaceAuto = QualType();
  return SubstituteDeducedTypeTransform(*this, TypeToReplaceAuto)
      .TransformType(TypeWithAuto);
}

TypeSourceInfo *Sema::SubstAutoTypeSourceInfo(TypeSourceInfo *TypeWithAuto,
                                              QualType TypeToReplaceAuto) {
  if (TypeToReplaceAuto->isDependentType())
    TypeToReplaceAuto = QualType();
  return SubstituteDeducedTypeTransform(*this, TypeToReplaceAuto)
      .TransformType(TypeWithAuto);
}

QualType Sema::ReplaceAutoType(QualType TypeWithAuto,
                               QualType TypeToReplaceAuto) {
  return SubstituteDeducedTypeTransform(*this, TypeToReplaceAuto,
                                        /*UseTypeSugar*/ false)
      .TransformType(TypeWithAuto);
}

void Sema::DiagnoseAutoDeductionFailure(VarDecl *VDecl, Expr *Init) {
  if (isa<InitListExpr>(Init))
    Diag(VDecl->getLocation(),
         VDecl->isInitCapture()
             ? diag::err_init_capture_deduction_failure_from_init_list
             : diag::err_auto_var_deduction_failure_from_init_list)
      << VDecl->getDeclName() << VDecl->getType() << Init->getSourceRange();
  else
    Diag(VDecl->getLocation(),
         VDecl->isInitCapture() ? diag::err_init_capture_deduction_failure
                                : diag::err_auto_var_deduction_failure)
      << VDecl->getDeclName() << VDecl->getType() << Init->getType()
      << Init->getSourceRange();
}

bool Sema::DeduceReturnType(FunctionDecl *FD, SourceLocation Loc,
                            bool Diagnose) {
  assert(FD->getReturnType()->isUndeducedType());

  // For a lambda's conversion operator, deduce any 'auto' or 'decltype(auto)'
  // within the return type from the call operator's type.
  if (isLambdaConversionOperator(FD)) {
    CXXRecordDecl *Lambda = cast<CXXMethodDecl>(FD)->getParent();
    FunctionDecl *CallOp = Lambda->getLambdaCallOperator();

    // For a generic lambda, instantiate the call operator if needed.
    if (auto *Args = FD->getTemplateSpecializationArgs()) {
      CallOp = InstantiateFunctionDeclaration(
          CallOp->getDescribedFunctionTemplate(), Args, Loc);
      if (!CallOp || CallOp->isInvalidDecl())
        return true;

      // We might need to deduce the return type by instantiating the definition
      // of the operator() function.
      if (CallOp->getReturnType()->isUndeducedType())
        InstantiateFunctionDefinition(Loc, CallOp);
    }

    if (CallOp->isInvalidDecl())
      return true;
    assert(!CallOp->getReturnType()->isUndeducedType() &&
           "failed to deduce lambda return type");

    // Build the new return type from scratch.
    QualType RetType = getLambdaConversionFunctionResultType(
        CallOp->getType()->castAs<FunctionProtoType>());
    if (FD->getReturnType()->getAs<PointerType>())
      RetType = Context.getPointerType(RetType);
    else {
      assert(FD->getReturnType()->getAs<BlockPointerType>());
      RetType = Context.getBlockPointerType(RetType);
    }
    Context.adjustDeducedFunctionResultType(FD, RetType);
    return false;
  }

  if (FD->getTemplateInstantiationPattern())
    InstantiateFunctionDefinition(Loc, FD);

  bool StillUndeduced = FD->getReturnType()->isUndeducedType();
  if (StillUndeduced && Diagnose && !FD->isInvalidDecl()) {
    Diag(Loc, diag::err_auto_fn_used_before_defined) << FD;
    Diag(FD->getLocation(), diag::note_callee_decl) << FD;
  }

  return StillUndeduced;
}

/// If this is a non-static member function,
static void
AddImplicitObjectParameterType(ASTContext &Context,
                               CXXMethodDecl *Method,
                               SmallVectorImpl<QualType> &ArgTypes) {
  // C++11 [temp.func.order]p3:
  //   [...] The new parameter is of type "reference to cv A," where cv are
  //   the cv-qualifiers of the function template (if any) and A is
  //   the class of which the function template is a member.
  //
  // The standard doesn't say explicitly, but we pick the appropriate kind of
  // reference type based on [over.match.funcs]p4.
  QualType ArgTy = Context.getTypeDeclType(Method->getParent());
  ArgTy = Context.getQualifiedType(ArgTy, Method->getTypeQualifiers());
  if (Method->getRefQualifier() == RQ_RValue)
    ArgTy = Context.getRValueReferenceType(ArgTy);
  else
    ArgTy = Context.getLValueReferenceType(ArgTy);
  ArgTypes.push_back(ArgTy);
}

/// Determine whether the function template \p FT1 is at least as
/// specialized as \p FT2.
static bool isAtLeastAsSpecializedAs(Sema &S,
                                     SourceLocation Loc,
                                     FunctionTemplateDecl *FT1,
                                     FunctionTemplateDecl *FT2,
                                     TemplatePartialOrderingContext TPOC,
                                     unsigned NumCallArguments1) {
  FunctionDecl *FD1 = FT1->getTemplatedDecl();
  FunctionDecl *FD2 = FT2->getTemplatedDecl();
  const FunctionProtoType *Proto1 = FD1->getType()->getAs<FunctionProtoType>();
  const FunctionProtoType *Proto2 = FD2->getType()->getAs<FunctionProtoType>();

  assert(Proto1 && Proto2 && "Function templates must have prototypes");
  TemplateParameterList *TemplateParams = FT2->getTemplateParameters();
  SmallVector<DeducedTemplateArgument, 4> Deduced;
  Deduced.resize(TemplateParams->size());

  // C++0x [temp.deduct.partial]p3:
  //   The types used to determine the ordering depend on the context in which
  //   the partial ordering is done:
  TemplateDeductionInfo Info(Loc);
  SmallVector<QualType, 4> Args2;
  switch (TPOC) {
  case TPOC_Call: {
    //   - In the context of a function call, the function parameter types are
    //     used.
    CXXMethodDecl *Method1 = dyn_cast<CXXMethodDecl>(FD1);
    CXXMethodDecl *Method2 = dyn_cast<CXXMethodDecl>(FD2);

    // C++11 [temp.func.order]p3:
    //   [...] If only one of the function templates is a non-static
    //   member, that function template is considered to have a new
    //   first parameter inserted in its function parameter list. The
    //   new parameter is of type "reference to cv A," where cv are
    //   the cv-qualifiers of the function template (if any) and A is
    //   the class of which the function template is a member.
    //
    // Note that we interpret this to mean "if one of the function
    // templates is a non-static member and the other is a non-member";
    // otherwise, the ordering rules for static functions against non-static
    // functions don't make any sense.
    //
    // C++98/03 doesn't have this provision but we've extended DR532 to cover
    // it as wording was broken prior to it.
    SmallVector<QualType, 4> Args1;

    unsigned NumComparedArguments = NumCallArguments1;

    if (!Method2 && Method1 && !Method1->isStatic()) {
      // Compare 'this' from Method1 against first parameter from Method2.
      AddImplicitObjectParameterType(S.Context, Method1, Args1);
      ++NumComparedArguments;
    } else if (!Method1 && Method2 && !Method2->isStatic()) {
      // Compare 'this' from Method2 against first parameter from Method1.
      AddImplicitObjectParameterType(S.Context, Method2, Args2);
    }

    Args1.insert(Args1.end(), Proto1->param_type_begin(),
                 Proto1->param_type_end());
    Args2.insert(Args2.end(), Proto2->param_type_begin(),
                 Proto2->param_type_end());

    // C++ [temp.func.order]p5:
    //   The presence of unused ellipsis and default arguments has no effect on
    //   the partial ordering of function templates.
    if (Args1.size() > NumComparedArguments)
      Args1.resize(NumComparedArguments);
    if (Args2.size() > NumComparedArguments)
      Args2.resize(NumComparedArguments);
    if (DeduceTemplateArguments(S, TemplateParams, Args2.data(), Args2.size(),
                                Args1.data(), Args1.size(), Info, Deduced,
                                TDF_None, /*PartialOrdering=*/true))
      return false;

    break;
  }

  case TPOC_Conversion:
    //   - In the context of a call to a conversion operator, the return types
    //     of the conversion function templates are used.
    if (DeduceTemplateArgumentsByTypeMatch(
            S, TemplateParams, Proto2->getReturnType(), Proto1->getReturnType(),
            Info, Deduced, TDF_None,
            /*PartialOrdering=*/true))
      return false;
    break;

  case TPOC_Other:
    //   - In other contexts (14.6.6.2) the function template's function type
    //     is used.
    if (DeduceTemplateArgumentsByTypeMatch(S, TemplateParams,
                                           FD2->getType(), FD1->getType(),
                                           Info, Deduced, TDF_None,
                                           /*PartialOrdering=*/true))
      return false;
    break;
  }

  // C++0x [temp.deduct.partial]p11:
  //   In most cases, all template parameters must have values in order for
  //   deduction to succeed, but for partial ordering purposes a template
  //   parameter may remain without a value provided it is not used in the
  //   types being used for partial ordering. [ Note: a template parameter used
  //   in a non-deduced context is considered used. -end note]
  unsigned ArgIdx = 0, NumArgs = Deduced.size();
  for (; ArgIdx != NumArgs; ++ArgIdx)
    if (Deduced[ArgIdx].isNull())
      break;

  // FIXME: We fail to implement [temp.deduct.type]p1 along this path. We need
  // to substitute the deduced arguments back into the template and check that
  // we get the right type.

  if (ArgIdx == NumArgs) {
    // All template arguments were deduced. FT1 is at least as specialized
    // as FT2.
    return true;
  }

  // Figure out which template parameters were used.
  llvm::SmallBitVector UsedParameters(TemplateParams->size());
  switch (TPOC) {
  case TPOC_Call:
    for (unsigned I = 0, N = Args2.size(); I != N; ++I)
      ::MarkUsedTemplateParameters(S.Context, Args2[I], false,
                                   TemplateParams->getDepth(),
                                   UsedParameters);
    break;

  case TPOC_Conversion:
    ::MarkUsedTemplateParameters(S.Context, Proto2->getReturnType(), false,
                                 TemplateParams->getDepth(), UsedParameters);
    break;

  case TPOC_Other:
    ::MarkUsedTemplateParameters(S.Context, FD2->getType(), false,
                                 TemplateParams->getDepth(),
                                 UsedParameters);
    break;
  }

  for (; ArgIdx != NumArgs; ++ArgIdx)
    // If this argument had no value deduced but was used in one of the types
    // used for partial ordering, then deduction fails.
    if (Deduced[ArgIdx].isNull() && UsedParameters[ArgIdx])
      return false;

  return true;
}

/// Determine whether this a function template whose parameter-type-list
/// ends with a function parameter pack.
static bool isVariadicFunctionTemplate(FunctionTemplateDecl *FunTmpl) {
  FunctionDecl *Function = FunTmpl->getTemplatedDecl();
  unsigned NumParams = Function->getNumParams();
  if (NumParams == 0)
    return false;

  ParmVarDecl *Last = Function->getParamDecl(NumParams - 1);
  if (!Last->isParameterPack())
    return false;

  // Make sure that no previous parameter is a parameter pack.
  while (--NumParams > 0) {
    if (Function->getParamDecl(NumParams - 1)->isParameterPack())
      return false;
  }

  return true;
}

/// Returns the more specialized function template according
/// to the rules of function template partial ordering (C++ [temp.func.order]).
///
/// \param FT1 the first function template
///
/// \param FT2 the second function template
///
/// \param TPOC the context in which we are performing partial ordering of
/// function templates.
///
/// \param NumCallArguments1 The number of arguments in the call to FT1, used
/// only when \c TPOC is \c TPOC_Call.
///
/// \param NumCallArguments2 The number of arguments in the call to FT2, used
/// only when \c TPOC is \c TPOC_Call.
///
/// \returns the more specialized function template. If neither
/// template is more specialized, returns NULL.
FunctionTemplateDecl *
Sema::getMoreSpecializedTemplate(FunctionTemplateDecl *FT1,
                                 FunctionTemplateDecl *FT2,
                                 SourceLocation Loc,
                                 TemplatePartialOrderingContext TPOC,
                                 unsigned NumCallArguments1,
                                 unsigned NumCallArguments2) {
  bool Better1 = isAtLeastAsSpecializedAs(*this, Loc, FT1, FT2, TPOC,
                                          NumCallArguments1);
  bool Better2 = isAtLeastAsSpecializedAs(*this, Loc, FT2, FT1, TPOC,
                                          NumCallArguments2);

  if (Better1 != Better2) // We have a clear winner
    return Better1 ? FT1 : FT2;

  if (!Better1 && !Better2) // Neither is better than the other
    return nullptr;

  // FIXME: This mimics what GCC implements, but doesn't match up with the
  // proposed resolution for core issue 692. This area needs to be sorted out,
  // but for now we attempt to maintain compatibility.
  bool Variadic1 = isVariadicFunctionTemplate(FT1);
  bool Variadic2 = isVariadicFunctionTemplate(FT2);
  if (Variadic1 != Variadic2)
    return Variadic1? FT2 : FT1;

  return nullptr;
}

/// Determine if the two templates are equivalent.
static bool isSameTemplate(TemplateDecl *T1, TemplateDecl *T2) {
  if (T1 == T2)
    return true;

  if (!T1 || !T2)
    return false;

  return T1->getCanonicalDecl() == T2->getCanonicalDecl();
}

/// Retrieve the most specialized of the given function template
/// specializations.
///
/// \param SpecBegin the start iterator of the function template
/// specializations that we will be comparing.
///
/// \param SpecEnd the end iterator of the function template
/// specializations, paired with \p SpecBegin.
///
/// \param Loc the location where the ambiguity or no-specializations
/// diagnostic should occur.
///
/// \param NoneDiag partial diagnostic used to diagnose cases where there are
/// no matching candidates.
///
/// \param AmbigDiag partial diagnostic used to diagnose an ambiguity, if one
/// occurs.
///
/// \param CandidateDiag partial diagnostic used for each function template
/// specialization that is a candidate in the ambiguous ordering. One parameter
/// in this diagnostic should be unbound, which will correspond to the string
/// describing the template arguments for the function template specialization.
///
/// \returns the most specialized function template specialization, if
/// found. Otherwise, returns SpecEnd.
UnresolvedSetIterator Sema::getMostSpecialized(
    UnresolvedSetIterator SpecBegin, UnresolvedSetIterator SpecEnd,
    TemplateSpecCandidateSet &FailedCandidates,
    SourceLocation Loc, const PartialDiagnostic &NoneDiag,
    const PartialDiagnostic &AmbigDiag, const PartialDiagnostic &CandidateDiag,
    bool Complain, QualType TargetType) {
  if (SpecBegin == SpecEnd) {
    if (Complain) {
      Diag(Loc, NoneDiag);
      FailedCandidates.NoteCandidates(*this, Loc);
    }
    return SpecEnd;
  }

  if (SpecBegin + 1 == SpecEnd)
    return SpecBegin;

  // Find the function template that is better than all of the templates it
  // has been compared to.
  UnresolvedSetIterator Best = SpecBegin;
  FunctionTemplateDecl *BestTemplate
    = cast<FunctionDecl>(*Best)->getPrimaryTemplate();
  assert(BestTemplate && "Not a function template specialization?");
  for (UnresolvedSetIterator I = SpecBegin + 1; I != SpecEnd; ++I) {
    FunctionTemplateDecl *Challenger
      = cast<FunctionDecl>(*I)->getPrimaryTemplate();
    assert(Challenger && "Not a function template specialization?");
    if (isSameTemplate(getMoreSpecializedTemplate(BestTemplate, Challenger,
                                                  Loc, TPOC_Other, 0, 0),
                       Challenger)) {
      Best = I;
      BestTemplate = Challenger;
    }
  }

  // Make sure that the "best" function template is more specialized than all
  // of the others.
  bool Ambiguous = false;
  for (UnresolvedSetIterator I = SpecBegin; I != SpecEnd; ++I) {
    FunctionTemplateDecl *Challenger
      = cast<FunctionDecl>(*I)->getPrimaryTemplate();
    if (I != Best &&
        !isSameTemplate(getMoreSpecializedTemplate(BestTemplate, Challenger,
                                                   Loc, TPOC_Other, 0, 0),
                        BestTemplate)) {
      Ambiguous = true;
      break;
    }
  }

  if (!Ambiguous) {
    // We found an answer. Return it.
    return Best;
  }

  // Diagnose the ambiguity.
  if (Complain) {
    Diag(Loc, AmbigDiag);

    // FIXME: Can we order the candidates in some sane way?
    for (UnresolvedSetIterator I = SpecBegin; I != SpecEnd; ++I) {
      PartialDiagnostic PD = CandidateDiag;
      const auto *FD = cast<FunctionDecl>(*I);
      PD << FD << getTemplateArgumentBindingsText(
                      FD->getPrimaryTemplate()->getTemplateParameters(),
                      *FD->getTemplateSpecializationArgs());
      if (!TargetType.isNull())
        HandleFunctionTypeMismatch(PD, FD->getType(), TargetType);
      Diag((*I)->getLocation(), PD);
    }
  }

  return SpecEnd;
}

/// Determine whether one partial specialization, P1, is at least as
/// specialized than another, P2.
///
/// \tparam TemplateLikeDecl The kind of P2, which must be a
/// TemplateDecl or {Class,Var}TemplatePartialSpecializationDecl.
/// \param T1 The injected-class-name of P1 (faked for a variable template).
/// \param T2 The injected-class-name of P2 (faked for a variable template).
template<typename TemplateLikeDecl>
static bool isAtLeastAsSpecializedAs(Sema &S, QualType T1, QualType T2,
                                     TemplateLikeDecl *P2,
                                     TemplateDeductionInfo &Info) {
  // C++ [temp.class.order]p1:
  //   For two class template partial specializations, the first is at least as
  //   specialized as the second if, given the following rewrite to two
  //   function templates, the first function template is at least as
  //   specialized as the second according to the ordering rules for function
  //   templates (14.6.6.2):
  //     - the first function template has the same template parameters as the
  //       first partial specialization and has a single function parameter
  //       whose type is a class template specialization with the template
  //       arguments of the first partial specialization, and
  //     - the second function template has the same template parameters as the
  //       second partial specialization and has a single function parameter
  //       whose type is a class template specialization with the template
  //       arguments of the second partial specialization.
  //
  // Rather than synthesize function templates, we merely perform the
  // equivalent partial ordering by performing deduction directly on
  // the template arguments of the class template partial
  // specializations. This computation is slightly simpler than the
  // general problem of function template partial ordering, because
  // class template partial specializations are more constrained. We
  // know that every template parameter is deducible from the class
  // template partial specialization's template arguments, for
  // example.
  SmallVector<DeducedTemplateArgument, 4> Deduced;

  // Determine whether P1 is at least as specialized as P2.
  Deduced.resize(P2->getTemplateParameters()->size());
  if (DeduceTemplateArgumentsByTypeMatch(S, P2->getTemplateParameters(),
                                         T2, T1, Info, Deduced, TDF_None,
                                         /*PartialOrdering=*/true))
    return false;

  SmallVector<TemplateArgument, 4> DeducedArgs(Deduced.begin(),
                                               Deduced.end());
  Sema::InstantiatingTemplate Inst(S, Info.getLocation(), P2, DeducedArgs,
                                   Info);
  auto *TST1 = T1->castAs<TemplateSpecializationType>();
  if (FinishTemplateArgumentDeduction(
          S, P2, /*PartialOrdering=*/true,
          TemplateArgumentList(TemplateArgumentList::OnStack,
                               TST1->template_arguments()),
          Deduced, Info))
    return false;

  return true;
}

/// Returns the more specialized class template partial specialization
/// according to the rules of partial ordering of class template partial
/// specializations (C++ [temp.class.order]).
///
/// \param PS1 the first class template partial specialization
///
/// \param PS2 the second class template partial specialization
///
/// \returns the more specialized class template partial specialization. If
/// neither partial specialization is more specialized, returns NULL.
ClassTemplatePartialSpecializationDecl *
Sema::getMoreSpecializedPartialSpecialization(
                                  ClassTemplatePartialSpecializationDecl *PS1,
                                  ClassTemplatePartialSpecializationDecl *PS2,
                                              SourceLocation Loc) {
  QualType PT1 = PS1->getInjectedSpecializationType();
  QualType PT2 = PS2->getInjectedSpecializationType();

  TemplateDeductionInfo Info(Loc);
  bool Better1 = isAtLeastAsSpecializedAs(*this, PT1, PT2, PS2, Info);
  bool Better2 = isAtLeastAsSpecializedAs(*this, PT2, PT1, PS1, Info);

  if (Better1 == Better2)
    return nullptr;

  return Better1 ? PS1 : PS2;
}

bool Sema::isMoreSpecializedThanPrimary(
    ClassTemplatePartialSpecializationDecl *Spec, TemplateDeductionInfo &Info) {
  ClassTemplateDecl *Primary = Spec->getSpecializedTemplate();
  QualType PrimaryT = Primary->getInjectedClassNameSpecialization();
  QualType PartialT = Spec->getInjectedSpecializationType();
  if (!isAtLeastAsSpecializedAs(*this, PartialT, PrimaryT, Primary, Info))
    return false;
  if (isAtLeastAsSpecializedAs(*this, PrimaryT, PartialT, Spec, Info)) {
    Info.clearSFINAEDiagnostic();
    return false;
  }
  return true;
}

VarTemplatePartialSpecializationDecl *
Sema::getMoreSpecializedPartialSpecialization(
    VarTemplatePartialSpecializationDecl *PS1,
    VarTemplatePartialSpecializationDecl *PS2, SourceLocation Loc) {
  // Pretend the variable template specializations are class template
  // specializations and form a fake injected class name type for comparison.
  assert(PS1->getSpecializedTemplate() == PS2->getSpecializedTemplate() &&
         "the partial specializations being compared should specialize"
         " the same template.");
  TemplateName Name(PS1->getSpecializedTemplate());
  TemplateName CanonTemplate = Context.getCanonicalTemplateName(Name);
  QualType PT1 = Context.getTemplateSpecializationType(
      CanonTemplate, PS1->getTemplateArgs().asArray());
  QualType PT2 = Context.getTemplateSpecializationType(
      CanonTemplate, PS2->getTemplateArgs().asArray());

  TemplateDeductionInfo Info(Loc);
  bool Better1 = isAtLeastAsSpecializedAs(*this, PT1, PT2, PS2, Info);
  bool Better2 = isAtLeastAsSpecializedAs(*this, PT2, PT1, PS1, Info);

  if (Better1 == Better2)
    return nullptr;

  return Better1 ? PS1 : PS2;
}

bool Sema::isMoreSpecializedThanPrimary(
    VarTemplatePartialSpecializationDecl *Spec, TemplateDeductionInfo &Info) {
  TemplateDecl *Primary = Spec->getSpecializedTemplate();
  // FIXME: Cache the injected template arguments rather than recomputing
  // them for each partial specialization.
  SmallVector<TemplateArgument, 8> PrimaryArgs;
  Context.getInjectedTemplateArgs(Primary->getTemplateParameters(),
                                  PrimaryArgs);

  TemplateName CanonTemplate =
      Context.getCanonicalTemplateName(TemplateName(Primary));
  QualType PrimaryT = Context.getTemplateSpecializationType(
      CanonTemplate, PrimaryArgs);
  QualType PartialT = Context.getTemplateSpecializationType(
      CanonTemplate, Spec->getTemplateArgs().asArray());
  if (!isAtLeastAsSpecializedAs(*this, PartialT, PrimaryT, Primary, Info))
    return false;
  if (isAtLeastAsSpecializedAs(*this, PrimaryT, PartialT, Spec, Info)) {
    Info.clearSFINAEDiagnostic();
    return false;
  }
  return true;
}

bool Sema::isTemplateTemplateParameterAtLeastAsSpecializedAs(
     TemplateParameterList *P, TemplateDecl *AArg, SourceLocation Loc) {
  // C++1z [temp.arg.template]p4: (DR 150)
  //   A template template-parameter P is at least as specialized as a
  //   template template-argument A if, given the following rewrite to two
  //   function templates...

  // Rather than synthesize function templates, we merely perform the
  // equivalent partial ordering by performing deduction directly on
  // the template parameter lists of the template template parameters.
  //
  //   Given an invented class template X with the template parameter list of
  //   A (including default arguments):
  TemplateName X = Context.getCanonicalTemplateName(TemplateName(AArg));
  TemplateParameterList *A = AArg->getTemplateParameters();

  //    - Each function template has a single function parameter whose type is
  //      a specialization of X with template arguments corresponding to the
  //      template parameters from the respective function template
  SmallVector<TemplateArgument, 8> AArgs;
  Context.getInjectedTemplateArgs(A, AArgs);

  // Check P's arguments against A's parameter list. This will fill in default
  // template arguments as needed. AArgs are already correct by construction.
  // We can't just use CheckTemplateIdType because that will expand alias
  // templates.
  SmallVector<TemplateArgument, 4> PArgs;
  {
    SFINAETrap Trap(*this);

    Context.getInjectedTemplateArgs(P, PArgs);
    TemplateArgumentListInfo PArgList(P->getLAngleLoc(), P->getRAngleLoc());
    for (unsigned I = 0, N = P->size(); I != N; ++I) {
      // Unwrap packs that getInjectedTemplateArgs wrapped around pack
      // expansions, to form an "as written" argument list.
      TemplateArgument Arg = PArgs[I];
      if (Arg.getKind() == TemplateArgument::Pack) {
        assert(Arg.pack_size() == 1 && Arg.pack_begin()->isPackExpansion());
        Arg = *Arg.pack_begin();
      }
      PArgList.addArgument(getTrivialTemplateArgumentLoc(
          Arg, QualType(), P->getParam(I)->getLocation()));
    }
    PArgs.clear();

    // C++1z [temp.arg.template]p3:
    //   If the rewrite produces an invalid type, then P is not at least as
    //   specialized as A.
    if (CheckTemplateArgumentList(AArg, Loc, PArgList, false, PArgs) ||
        Trap.hasErrorOccurred())
      return false;
  }

  QualType AType = Context.getTemplateSpecializationType(X, AArgs);
  QualType PType = Context.getTemplateSpecializationType(X, PArgs);

  //   ... the function template corresponding to P is at least as specialized
  //   as the function template corresponding to A according to the partial
  //   ordering rules for function templates.
  TemplateDeductionInfo Info(Loc, A->getDepth());
  return isAtLeastAsSpecializedAs(*this, PType, AType, AArg, Info);
}

/// Mark the template parameters that are used by the given
/// expression.
static void
MarkUsedTemplateParameters(ASTContext &Ctx,
                           const Expr *E,
                           bool OnlyDeduced,
                           unsigned Depth,
                           llvm::SmallBitVector &Used) {
  // We can deduce from a pack expansion.
  if (const PackExpansionExpr *Expansion = dyn_cast<PackExpansionExpr>(E))
    E = Expansion->getPattern();

  // Skip through any implicit casts we added while type-checking, and any
  // substitutions performed by template alias expansion.
  while (true) {
    if (const ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(E))
      E = ICE->getSubExpr();
    else if (const ConstantExpr *CE = dyn_cast<ConstantExpr>(E))
      E = CE->getSubExpr();
    else if (const SubstNonTypeTemplateParmExpr *Subst =
               dyn_cast<SubstNonTypeTemplateParmExpr>(E))
      E = Subst->getReplacement();
    else
      break;
  }

  // FIXME: if !OnlyDeduced, we have to walk the whole subexpression to
  // find other occurrences of template parameters.
  const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E);
  if (!DRE)
    return;

  const NonTypeTemplateParmDecl *NTTP
    = dyn_cast<NonTypeTemplateParmDecl>(DRE->getDecl());
  if (!NTTP)
    return;

  if (NTTP->getDepth() == Depth)
    Used[NTTP->getIndex()] = true;

  // In C++17 mode, additional arguments may be deduced from the type of a
  // non-type argument.
  if (Ctx.getLangOpts().CPlusPlus17)
    MarkUsedTemplateParameters(Ctx, NTTP->getType(), OnlyDeduced, Depth, Used);
}

/// Mark the template parameters that are used by the given
/// nested name specifier.
static void
MarkUsedTemplateParameters(ASTContext &Ctx,
                           NestedNameSpecifier *NNS,
                           bool OnlyDeduced,
                           unsigned Depth,
                           llvm::SmallBitVector &Used) {
  if (!NNS)
    return;

  MarkUsedTemplateParameters(Ctx, NNS->getPrefix(), OnlyDeduced, Depth,
                             Used);
  MarkUsedTemplateParameters(Ctx, QualType(NNS->getAsType(), 0),
                             OnlyDeduced, Depth, Used);
}

/// Mark the template parameters that are used by the given
/// template name.
static void
MarkUsedTemplateParameters(ASTContext &Ctx,
                           TemplateName Name,
                           bool OnlyDeduced,
                           unsigned Depth,
                           llvm::SmallBitVector &Used) {
  if (TemplateDecl *Template = Name.getAsTemplateDecl()) {
    if (TemplateTemplateParmDecl *TTP
          = dyn_cast<TemplateTemplateParmDecl>(Template)) {
      if (TTP->getDepth() == Depth)
        Used[TTP->getIndex()] = true;
    }
    return;
  }

  if (QualifiedTemplateName *QTN = Name.getAsQualifiedTemplateName())
    MarkUsedTemplateParameters(Ctx, QTN->getQualifier(), OnlyDeduced,
                               Depth, Used);
  if (DependentTemplateName *DTN = Name.getAsDependentTemplateName())
    MarkUsedTemplateParameters(Ctx, DTN->getQualifier(), OnlyDeduced,
                               Depth, Used);
}

/// Mark the template parameters that are used by the given
/// type.
static void
MarkUsedTemplateParameters(ASTContext &Ctx, QualType T,
                           bool OnlyDeduced,
                           unsigned Depth,
                           llvm::SmallBitVector &Used) {
  if (T.isNull())
    return;

  // Non-dependent types have nothing deducible
  if (!T->isDependentType())
    return;

  T = Ctx.getCanonicalType(T);
  switch (T->getTypeClass()) {
  case Type::Pointer:
    MarkUsedTemplateParameters(Ctx,
                               cast<PointerType>(T)->getPointeeType(),
                               OnlyDeduced,
                               Depth,
                               Used);
    break;

  case Type::BlockPointer:
    MarkUsedTemplateParameters(Ctx,
                               cast<BlockPointerType>(T)->getPointeeType(),
                               OnlyDeduced,
                               Depth,
                               Used);
    break;

  case Type::LValueReference:
  case Type::RValueReference:
    MarkUsedTemplateParameters(Ctx,
                               cast<ReferenceType>(T)->getPointeeType(),
                               OnlyDeduced,
                               Depth,
                               Used);
    break;

  case Type::MemberPointer: {
    const MemberPointerType *MemPtr = cast<MemberPointerType>(T.getTypePtr());
    MarkUsedTemplateParameters(Ctx, MemPtr->getPointeeType(), OnlyDeduced,
                               Depth, Used);
    MarkUsedTemplateParameters(Ctx, QualType(MemPtr->getClass(), 0),
                               OnlyDeduced, Depth, Used);
    break;
  }

  case Type::DependentSizedArray:
    MarkUsedTemplateParameters(Ctx,
                               cast<DependentSizedArrayType>(T)->getSizeExpr(),
                               OnlyDeduced, Depth, Used);
    // Fall through to check the element type
    LLVM_FALLTHROUGH;

  case Type::ConstantArray:
  case Type::IncompleteArray:
    MarkUsedTemplateParameters(Ctx,
                               cast<ArrayType>(T)->getElementType(),
                               OnlyDeduced, Depth, Used);
    break;

  case Type::Vector:
  case Type::ExtVector:
    MarkUsedTemplateParameters(Ctx,
                               cast<VectorType>(T)->getElementType(),
                               OnlyDeduced, Depth, Used);
    break;

  case Type::DependentVector: {
    const auto *VecType = cast<DependentVectorType>(T);
    MarkUsedTemplateParameters(Ctx, VecType->getElementType(), OnlyDeduced,
                               Depth, Used);
    MarkUsedTemplateParameters(Ctx, VecType->getSizeExpr(), OnlyDeduced, Depth,
                               Used);
    break;
  }
  case Type::DependentSizedExtVector: {
    const DependentSizedExtVectorType *VecType
      = cast<DependentSizedExtVectorType>(T);
    MarkUsedTemplateParameters(Ctx, VecType->getElementType(), OnlyDeduced,
                               Depth, Used);
    MarkUsedTemplateParameters(Ctx, VecType->getSizeExpr(), OnlyDeduced,
                               Depth, Used);
    break;
  }

  case Type::DependentAddressSpace: {
    const DependentAddressSpaceType *DependentASType =
        cast<DependentAddressSpaceType>(T);
    MarkUsedTemplateParameters(Ctx, DependentASType->getPointeeType(),
                               OnlyDeduced, Depth, Used);
    MarkUsedTemplateParameters(Ctx,
                               DependentASType->getAddrSpaceExpr(),
                               OnlyDeduced, Depth, Used);
    break;
  }

  case Type::FunctionProto: {
    const FunctionProtoType *Proto = cast<FunctionProtoType>(T);
    MarkUsedTemplateParameters(Ctx, Proto->getReturnType(), OnlyDeduced, Depth,
                               Used);
    for (unsigned I = 0, N = Proto->getNumParams(); I != N; ++I) {
      // C++17 [temp.deduct.type]p5:
      //   The non-deduced contexts are: [...]
      //   -- A function parameter pack that does not occur at the end of the
      //      parameter-declaration-list.
      if (!OnlyDeduced || I + 1 == N ||
          !Proto->getParamType(I)->getAs<PackExpansionType>()) {
        MarkUsedTemplateParameters(Ctx, Proto->getParamType(I), OnlyDeduced,
                                   Depth, Used);
      } else {
        // FIXME: C++17 [temp.deduct.call]p1:
        //   When a function parameter pack appears in a non-deduced context,
        //   the type of that pack is never deduced.
        //
        // We should also track a set of "never deduced" parameters, and
        // subtract that from the list of deduced parameters after marking.
      }
    }
    if (auto *E = Proto->getNoexceptExpr())
      MarkUsedTemplateParameters(Ctx, E, OnlyDeduced, Depth, Used);
    break;
  }

  case Type::TemplateTypeParm: {
    const TemplateTypeParmType *TTP = cast<TemplateTypeParmType>(T);
    if (TTP->getDepth() == Depth)
      Used[TTP->getIndex()] = true;
    break;
  }

  case Type::SubstTemplateTypeParmPack: {
    const SubstTemplateTypeParmPackType *Subst
      = cast<SubstTemplateTypeParmPackType>(T);
    MarkUsedTemplateParameters(Ctx,
                               QualType(Subst->getReplacedParameter(), 0),
                               OnlyDeduced, Depth, Used);
    MarkUsedTemplateParameters(Ctx, Subst->getArgumentPack(),
                               OnlyDeduced, Depth, Used);
    break;
  }

  case Type::InjectedClassName:
    T = cast<InjectedClassNameType>(T)->getInjectedSpecializationType();
    LLVM_FALLTHROUGH;

  case Type::TemplateSpecialization: {
    const TemplateSpecializationType *Spec
      = cast<TemplateSpecializationType>(T);
    MarkUsedTemplateParameters(Ctx, Spec->getTemplateName(), OnlyDeduced,
                               Depth, Used);

    // C++0x [temp.deduct.type]p9:
    //   If the template argument list of P contains a pack expansion that is
    //   not the last template argument, the entire template argument list is a
    //   non-deduced context.
    if (OnlyDeduced &&
        hasPackExpansionBeforeEnd(Spec->template_arguments()))
      break;

    for (unsigned I = 0, N = Spec->getNumArgs(); I != N; ++I)
      MarkUsedTemplateParameters(Ctx, Spec->getArg(I), OnlyDeduced, Depth,
                                 Used);
    break;
  }

  case Type::Complex:
    if (!OnlyDeduced)
      MarkUsedTemplateParameters(Ctx,
                                 cast<ComplexType>(T)->getElementType(),
                                 OnlyDeduced, Depth, Used);
    break;

  case Type::Atomic:
    if (!OnlyDeduced)
      MarkUsedTemplateParameters(Ctx,
                                 cast<AtomicType>(T)->getValueType(),
                                 OnlyDeduced, Depth, Used);
    break;

  case Type::DependentName:
    if (!OnlyDeduced)
      MarkUsedTemplateParameters(Ctx,
                                 cast<DependentNameType>(T)->getQualifier(),
                                 OnlyDeduced, Depth, Used);
    break;

  case Type::DependentTemplateSpecialization: {
    // C++14 [temp.deduct.type]p5:
    //   The non-deduced contexts are:
    //     -- The nested-name-specifier of a type that was specified using a
    //        qualified-id
    //
    // C++14 [temp.deduct.type]p6:
    //   When a type name is specified in a way that includes a non-deduced
    //   context, all of the types that comprise that type name are also
    //   non-deduced.
    if (OnlyDeduced)
      break;

    const DependentTemplateSpecializationType *Spec
      = cast<DependentTemplateSpecializationType>(T);

    MarkUsedTemplateParameters(Ctx, Spec->getQualifier(),
                               OnlyDeduced, Depth, Used);

    for (unsigned I = 0, N = Spec->getNumArgs(); I != N; ++I)
      MarkUsedTemplateParameters(Ctx, Spec->getArg(I), OnlyDeduced, Depth,
                                 Used);
    break;
  }

  case Type::TypeOf:
    if (!OnlyDeduced)
      MarkUsedTemplateParameters(Ctx,
                                 cast<TypeOfType>(T)->getUnderlyingType(),
                                 OnlyDeduced, Depth, Used);
    break;

  case Type::TypeOfExpr:
    if (!OnlyDeduced)
      MarkUsedTemplateParameters(Ctx,
                                 cast<TypeOfExprType>(T)->getUnderlyingExpr(),
                                 OnlyDeduced, Depth, Used);
    break;

  case Type::Decltype:
    if (!OnlyDeduced)
      MarkUsedTemplateParameters(Ctx,
                                 cast<DecltypeType>(T)->getUnderlyingExpr(),
                                 OnlyDeduced, Depth, Used);
    break;

  case Type::UnaryTransform:
    if (!OnlyDeduced)
      MarkUsedTemplateParameters(Ctx,
                                 cast<UnaryTransformType>(T)->getUnderlyingType(),
                                 OnlyDeduced, Depth, Used);
    break;

  case Type::PackExpansion:
    MarkUsedTemplateParameters(Ctx,
                               cast<PackExpansionType>(T)->getPattern(),
                               OnlyDeduced, Depth, Used);
    break;

  case Type::Auto:
  case Type::DeducedTemplateSpecialization:
    MarkUsedTemplateParameters(Ctx,
                               cast<DeducedType>(T)->getDeducedType(),
                               OnlyDeduced, Depth, Used);
    break;

  // None of these types have any template parameters in them.
  case Type::Builtin:
  case Type::VariableArray:
  case Type::FunctionNoProto:
  case Type::Record:
  case Type::Enum:
  case Type::ObjCInterface:
  case Type::ObjCObject:
  case Type::ObjCObjectPointer:
  case Type::UnresolvedUsing:
  case Type::Pipe:
#define TYPE(Class, Base)
#define ABSTRACT_TYPE(Class, Base)
#define DEPENDENT_TYPE(Class, Base)
#define NON_CANONICAL_TYPE(Class, Base) case Type::Class:
#include "clang/AST/TypeNodes.def"
    break;
  }
}

/// Mark the template parameters that are used by this
/// template argument.
static void
MarkUsedTemplateParameters(ASTContext &Ctx,
                           const TemplateArgument &TemplateArg,
                           bool OnlyDeduced,
                           unsigned Depth,
                           llvm::SmallBitVector &Used) {
  switch (TemplateArg.getKind()) {
  case TemplateArgument::Null:
  case TemplateArgument::Integral:
  case TemplateArgument::Declaration:
    break;

  case TemplateArgument::NullPtr:
    MarkUsedTemplateParameters(Ctx, TemplateArg.getNullPtrType(), OnlyDeduced,
                               Depth, Used);
    break;

  case TemplateArgument::Type:
    MarkUsedTemplateParameters(Ctx, TemplateArg.getAsType(), OnlyDeduced,
                               Depth, Used);
    break;

  case TemplateArgument::Template:
  case TemplateArgument::TemplateExpansion:
    MarkUsedTemplateParameters(Ctx,
                               TemplateArg.getAsTemplateOrTemplatePattern(),
                               OnlyDeduced, Depth, Used);
    break;

  case TemplateArgument::Expression:
    MarkUsedTemplateParameters(Ctx, TemplateArg.getAsExpr(), OnlyDeduced,
                               Depth, Used);
    break;

  case TemplateArgument::Pack:
    for (const auto &P : TemplateArg.pack_elements())
      MarkUsedTemplateParameters(Ctx, P, OnlyDeduced, Depth, Used);
    break;
  }
}

/// Mark which template parameters can be deduced from a given
/// template argument list.
///
/// \param TemplateArgs the template argument list from which template
/// parameters will be deduced.
///
/// \param Used a bit vector whose elements will be set to \c true
/// to indicate when the corresponding template parameter will be
/// deduced.
void
Sema::MarkUsedTemplateParameters(const TemplateArgumentList &TemplateArgs,
                                 bool OnlyDeduced, unsigned Depth,
                                 llvm::SmallBitVector &Used) {
  // C++0x [temp.deduct.type]p9:
  //   If the template argument list of P contains a pack expansion that is not
  //   the last template argument, the entire template argument list is a
  //   non-deduced context.
  if (OnlyDeduced &&
      hasPackExpansionBeforeEnd(TemplateArgs.asArray()))
    return;

  for (unsigned I = 0, N = TemplateArgs.size(); I != N; ++I)
    ::MarkUsedTemplateParameters(Context, TemplateArgs[I], OnlyDeduced,
                                 Depth, Used);
}

/// Marks all of the template parameters that will be deduced by a
/// call to the given function template.
void Sema::MarkDeducedTemplateParameters(
    ASTContext &Ctx, const FunctionTemplateDecl *FunctionTemplate,
    llvm::SmallBitVector &Deduced) {
  TemplateParameterList *TemplateParams
    = FunctionTemplate->getTemplateParameters();
  Deduced.clear();
  Deduced.resize(TemplateParams->size());

  FunctionDecl *Function = FunctionTemplate->getTemplatedDecl();
  for (unsigned I = 0, N = Function->getNumParams(); I != N; ++I)
    ::MarkUsedTemplateParameters(Ctx, Function->getParamDecl(I)->getType(),
                                 true, TemplateParams->getDepth(), Deduced);
}

bool hasDeducibleTemplateParameters(Sema &S,
                                    FunctionTemplateDecl *FunctionTemplate,
                                    QualType T) {
  if (!T->isDependentType())
    return false;

  TemplateParameterList *TemplateParams
    = FunctionTemplate->getTemplateParameters();
  llvm::SmallBitVector Deduced(TemplateParams->size());
  ::MarkUsedTemplateParameters(S.Context, T, true, TemplateParams->getDepth(),
                               Deduced);

  return Deduced.any();
}
