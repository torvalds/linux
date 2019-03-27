//===--- SemaLambda.cpp - Semantic Analysis for C++11 Lambdas -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis for C++ lambda expressions.
//
//===----------------------------------------------------------------------===//
#include "clang/Sema/DeclSpec.h"
#include "TypeLocBuilder.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/ExprCXX.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Sema/Initialization.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/SemaInternal.h"
#include "clang/Sema/SemaLambda.h"
using namespace clang;
using namespace sema;

/// Examines the FunctionScopeInfo stack to determine the nearest
/// enclosing lambda (to the current lambda) that is 'capture-ready' for
/// the variable referenced in the current lambda (i.e. \p VarToCapture).
/// If successful, returns the index into Sema's FunctionScopeInfo stack
/// of the capture-ready lambda's LambdaScopeInfo.
///
/// Climbs down the stack of lambdas (deepest nested lambda - i.e. current
/// lambda - is on top) to determine the index of the nearest enclosing/outer
/// lambda that is ready to capture the \p VarToCapture being referenced in
/// the current lambda.
/// As we climb down the stack, we want the index of the first such lambda -
/// that is the lambda with the highest index that is 'capture-ready'.
///
/// A lambda 'L' is capture-ready for 'V' (var or this) if:
///  - its enclosing context is non-dependent
///  - and if the chain of lambdas between L and the lambda in which
///    V is potentially used (i.e. the lambda at the top of the scope info
///    stack), can all capture or have already captured V.
/// If \p VarToCapture is 'null' then we are trying to capture 'this'.
///
/// Note that a lambda that is deemed 'capture-ready' still needs to be checked
/// for whether it is 'capture-capable' (see
/// getStackIndexOfNearestEnclosingCaptureCapableLambda), before it can truly
/// capture.
///
/// \param FunctionScopes - Sema's stack of nested FunctionScopeInfo's (which a
///  LambdaScopeInfo inherits from).  The current/deepest/innermost lambda
///  is at the top of the stack and has the highest index.
/// \param VarToCapture - the variable to capture.  If NULL, capture 'this'.
///
/// \returns An Optional<unsigned> Index that if evaluates to 'true' contains
/// the index (into Sema's FunctionScopeInfo stack) of the innermost lambda
/// which is capture-ready.  If the return value evaluates to 'false' then
/// no lambda is capture-ready for \p VarToCapture.

static inline Optional<unsigned>
getStackIndexOfNearestEnclosingCaptureReadyLambda(
    ArrayRef<const clang::sema::FunctionScopeInfo *> FunctionScopes,
    VarDecl *VarToCapture) {
  // Label failure to capture.
  const Optional<unsigned> NoLambdaIsCaptureReady;

  // Ignore all inner captured regions.
  unsigned CurScopeIndex = FunctionScopes.size() - 1;
  while (CurScopeIndex > 0 && isa<clang::sema::CapturedRegionScopeInfo>(
                                  FunctionScopes[CurScopeIndex]))
    --CurScopeIndex;
  assert(
      isa<clang::sema::LambdaScopeInfo>(FunctionScopes[CurScopeIndex]) &&
      "The function on the top of sema's function-info stack must be a lambda");

  // If VarToCapture is null, we are attempting to capture 'this'.
  const bool IsCapturingThis = !VarToCapture;
  const bool IsCapturingVariable = !IsCapturingThis;

  // Start with the current lambda at the top of the stack (highest index).
  DeclContext *EnclosingDC =
      cast<sema::LambdaScopeInfo>(FunctionScopes[CurScopeIndex])->CallOperator;

  do {
    const clang::sema::LambdaScopeInfo *LSI =
        cast<sema::LambdaScopeInfo>(FunctionScopes[CurScopeIndex]);
    // IF we have climbed down to an intervening enclosing lambda that contains
    // the variable declaration - it obviously can/must not capture the
    // variable.
    // Since its enclosing DC is dependent, all the lambdas between it and the
    // innermost nested lambda are dependent (otherwise we wouldn't have
    // arrived here) - so we don't yet have a lambda that can capture the
    // variable.
    if (IsCapturingVariable &&
        VarToCapture->getDeclContext()->Equals(EnclosingDC))
      return NoLambdaIsCaptureReady;

    // For an enclosing lambda to be capture ready for an entity, all
    // intervening lambda's have to be able to capture that entity. If even
    // one of the intervening lambda's is not capable of capturing the entity
    // then no enclosing lambda can ever capture that entity.
    // For e.g.
    // const int x = 10;
    // [=](auto a) {    #1
    //   [](auto b) {   #2 <-- an intervening lambda that can never capture 'x'
    //    [=](auto c) { #3
    //       f(x, c);  <-- can not lead to x's speculative capture by #1 or #2
    //    }; }; };
    // If they do not have a default implicit capture, check to see
    // if the entity has already been explicitly captured.
    // If even a single dependent enclosing lambda lacks the capability
    // to ever capture this variable, there is no further enclosing
    // non-dependent lambda that can capture this variable.
    if (LSI->ImpCaptureStyle == sema::LambdaScopeInfo::ImpCap_None) {
      if (IsCapturingVariable && !LSI->isCaptured(VarToCapture))
        return NoLambdaIsCaptureReady;
      if (IsCapturingThis && !LSI->isCXXThisCaptured())
        return NoLambdaIsCaptureReady;
    }
    EnclosingDC = getLambdaAwareParentOfDeclContext(EnclosingDC);

    assert(CurScopeIndex);
    --CurScopeIndex;
  } while (!EnclosingDC->isTranslationUnit() &&
           EnclosingDC->isDependentContext() &&
           isLambdaCallOperator(EnclosingDC));

  assert(CurScopeIndex < (FunctionScopes.size() - 1));
  // If the enclosingDC is not dependent, then the immediately nested lambda
  // (one index above) is capture-ready.
  if (!EnclosingDC->isDependentContext())
    return CurScopeIndex + 1;
  return NoLambdaIsCaptureReady;
}

/// Examines the FunctionScopeInfo stack to determine the nearest
/// enclosing lambda (to the current lambda) that is 'capture-capable' for
/// the variable referenced in the current lambda (i.e. \p VarToCapture).
/// If successful, returns the index into Sema's FunctionScopeInfo stack
/// of the capture-capable lambda's LambdaScopeInfo.
///
/// Given the current stack of lambdas being processed by Sema and
/// the variable of interest, to identify the nearest enclosing lambda (to the
/// current lambda at the top of the stack) that can truly capture
/// a variable, it has to have the following two properties:
///  a) 'capture-ready' - be the innermost lambda that is 'capture-ready':
///     - climb down the stack (i.e. starting from the innermost and examining
///       each outer lambda step by step) checking if each enclosing
///       lambda can either implicitly or explicitly capture the variable.
///       Record the first such lambda that is enclosed in a non-dependent
///       context. If no such lambda currently exists return failure.
///  b) 'capture-capable' - make sure the 'capture-ready' lambda can truly
///  capture the variable by checking all its enclosing lambdas:
///     - check if all outer lambdas enclosing the 'capture-ready' lambda
///       identified above in 'a' can also capture the variable (this is done
///       via tryCaptureVariable for variables and CheckCXXThisCapture for
///       'this' by passing in the index of the Lambda identified in step 'a')
///
/// \param FunctionScopes - Sema's stack of nested FunctionScopeInfo's (which a
/// LambdaScopeInfo inherits from).  The current/deepest/innermost lambda
/// is at the top of the stack.
///
/// \param VarToCapture - the variable to capture.  If NULL, capture 'this'.
///
///
/// \returns An Optional<unsigned> Index that if evaluates to 'true' contains
/// the index (into Sema's FunctionScopeInfo stack) of the innermost lambda
/// which is capture-capable.  If the return value evaluates to 'false' then
/// no lambda is capture-capable for \p VarToCapture.

Optional<unsigned> clang::getStackIndexOfNearestEnclosingCaptureCapableLambda(
    ArrayRef<const sema::FunctionScopeInfo *> FunctionScopes,
    VarDecl *VarToCapture, Sema &S) {

  const Optional<unsigned> NoLambdaIsCaptureCapable;

  const Optional<unsigned> OptionalStackIndex =
      getStackIndexOfNearestEnclosingCaptureReadyLambda(FunctionScopes,
                                                        VarToCapture);
  if (!OptionalStackIndex)
    return NoLambdaIsCaptureCapable;

  const unsigned IndexOfCaptureReadyLambda = OptionalStackIndex.getValue();
  assert(((IndexOfCaptureReadyLambda != (FunctionScopes.size() - 1)) ||
          S.getCurGenericLambda()) &&
         "The capture ready lambda for a potential capture can only be the "
         "current lambda if it is a generic lambda");

  const sema::LambdaScopeInfo *const CaptureReadyLambdaLSI =
      cast<sema::LambdaScopeInfo>(FunctionScopes[IndexOfCaptureReadyLambda]);

  // If VarToCapture is null, we are attempting to capture 'this'
  const bool IsCapturingThis = !VarToCapture;
  const bool IsCapturingVariable = !IsCapturingThis;

  if (IsCapturingVariable) {
    // Check if the capture-ready lambda can truly capture the variable, by
    // checking whether all enclosing lambdas of the capture-ready lambda allow
    // the capture - i.e. make sure it is capture-capable.
    QualType CaptureType, DeclRefType;
    const bool CanCaptureVariable =
        !S.tryCaptureVariable(VarToCapture,
                              /*ExprVarIsUsedInLoc*/ SourceLocation(),
                              clang::Sema::TryCapture_Implicit,
                              /*EllipsisLoc*/ SourceLocation(),
                              /*BuildAndDiagnose*/ false, CaptureType,
                              DeclRefType, &IndexOfCaptureReadyLambda);
    if (!CanCaptureVariable)
      return NoLambdaIsCaptureCapable;
  } else {
    // Check if the capture-ready lambda can truly capture 'this' by checking
    // whether all enclosing lambdas of the capture-ready lambda can capture
    // 'this'.
    const bool CanCaptureThis =
        !S.CheckCXXThisCapture(
             CaptureReadyLambdaLSI->PotentialThisCaptureLocation,
             /*Explicit*/ false, /*BuildAndDiagnose*/ false,
             &IndexOfCaptureReadyLambda);
    if (!CanCaptureThis)
      return NoLambdaIsCaptureCapable;
  }
  return IndexOfCaptureReadyLambda;
}

static inline TemplateParameterList *
getGenericLambdaTemplateParameterList(LambdaScopeInfo *LSI, Sema &SemaRef) {
  if (LSI->GLTemplateParameterList)
    return LSI->GLTemplateParameterList;

  if (!LSI->AutoTemplateParams.empty()) {
    SourceRange IntroRange = LSI->IntroducerRange;
    SourceLocation LAngleLoc = IntroRange.getBegin();
    SourceLocation RAngleLoc = IntroRange.getEnd();
    LSI->GLTemplateParameterList = TemplateParameterList::Create(
        SemaRef.Context,
        /*Template kw loc*/ SourceLocation(), LAngleLoc,
        llvm::makeArrayRef((NamedDecl *const *)LSI->AutoTemplateParams.data(),
                           LSI->AutoTemplateParams.size()),
        RAngleLoc, nullptr);
  }
  return LSI->GLTemplateParameterList;
}

CXXRecordDecl *Sema::createLambdaClosureType(SourceRange IntroducerRange,
                                             TypeSourceInfo *Info,
                                             bool KnownDependent,
                                             LambdaCaptureDefault CaptureDefault) {
  DeclContext *DC = CurContext;
  while (!(DC->isFunctionOrMethod() || DC->isRecord() || DC->isFileContext()))
    DC = DC->getParent();
  bool IsGenericLambda = getGenericLambdaTemplateParameterList(getCurLambda(),
                                                               *this);
  // Start constructing the lambda class.
  CXXRecordDecl *Class = CXXRecordDecl::CreateLambda(Context, DC, Info,
                                                     IntroducerRange.getBegin(),
                                                     KnownDependent,
                                                     IsGenericLambda,
                                                     CaptureDefault);
  DC->addDecl(Class);

  return Class;
}

/// Determine whether the given context is or is enclosed in an inline
/// function.
static bool isInInlineFunction(const DeclContext *DC) {
  while (!DC->isFileContext()) {
    if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(DC))
      if (FD->isInlined())
        return true;

    DC = DC->getLexicalParent();
  }

  return false;
}

MangleNumberingContext *
Sema::getCurrentMangleNumberContext(const DeclContext *DC,
                                    Decl *&ManglingContextDecl) {
  // Compute the context for allocating mangling numbers in the current
  // expression, if the ABI requires them.
  ManglingContextDecl = ExprEvalContexts.back().ManglingContextDecl;

  enum ContextKind {
    Normal,
    DefaultArgument,
    DataMember,
    StaticDataMember,
    InlineVariable,
    VariableTemplate
  } Kind = Normal;

  // Default arguments of member function parameters that appear in a class
  // definition, as well as the initializers of data members, receive special
  // treatment. Identify them.
  if (ManglingContextDecl) {
    if (ParmVarDecl *Param = dyn_cast<ParmVarDecl>(ManglingContextDecl)) {
      if (const DeclContext *LexicalDC
          = Param->getDeclContext()->getLexicalParent())
        if (LexicalDC->isRecord())
          Kind = DefaultArgument;
    } else if (VarDecl *Var = dyn_cast<VarDecl>(ManglingContextDecl)) {
      if (Var->getDeclContext()->isRecord())
        Kind = StaticDataMember;
      else if (Var->getMostRecentDecl()->isInline())
        Kind = InlineVariable;
      else if (Var->getDescribedVarTemplate())
        Kind = VariableTemplate;
      else if (auto *VTS = dyn_cast<VarTemplateSpecializationDecl>(Var)) {
        if (!VTS->isExplicitSpecialization())
          Kind = VariableTemplate;
      }
    } else if (isa<FieldDecl>(ManglingContextDecl)) {
      Kind = DataMember;
    }
  }

  // Itanium ABI [5.1.7]:
  //   In the following contexts [...] the one-definition rule requires closure
  //   types in different translation units to "correspond":
  bool IsInNonspecializedTemplate =
      inTemplateInstantiation() || CurContext->isDependentContext();
  switch (Kind) {
  case Normal: {
    //  -- the bodies of non-exported nonspecialized template functions
    //  -- the bodies of inline functions
    if ((IsInNonspecializedTemplate &&
         !(ManglingContextDecl && isa<ParmVarDecl>(ManglingContextDecl))) ||
        isInInlineFunction(CurContext)) {
      ManglingContextDecl = nullptr;
      while (auto *CD = dyn_cast<CapturedDecl>(DC))
        DC = CD->getParent();
      return &Context.getManglingNumberContext(DC);
    }

    ManglingContextDecl = nullptr;
    return nullptr;
  }

  case StaticDataMember:
    //  -- the initializers of nonspecialized static members of template classes
    if (!IsInNonspecializedTemplate) {
      ManglingContextDecl = nullptr;
      return nullptr;
    }
    // Fall through to get the current context.
    LLVM_FALLTHROUGH;

  case DataMember:
    //  -- the in-class initializers of class members
  case DefaultArgument:
    //  -- default arguments appearing in class definitions
  case InlineVariable:
    //  -- the initializers of inline variables
  case VariableTemplate:
    //  -- the initializers of templated variables
    return &ExprEvalContexts.back().getMangleNumberingContext(Context);
  }

  llvm_unreachable("unexpected context");
}

MangleNumberingContext &
Sema::ExpressionEvaluationContextRecord::getMangleNumberingContext(
    ASTContext &Ctx) {
  assert(ManglingContextDecl && "Need to have a context declaration");
  if (!MangleNumbering)
    MangleNumbering = Ctx.createMangleNumberingContext();
  return *MangleNumbering;
}

CXXMethodDecl *Sema::startLambdaDefinition(CXXRecordDecl *Class,
                                           SourceRange IntroducerRange,
                                           TypeSourceInfo *MethodTypeInfo,
                                           SourceLocation EndLoc,
                                           ArrayRef<ParmVarDecl *> Params,
                                           const bool IsConstexprSpecified) {
  QualType MethodType = MethodTypeInfo->getType();
  TemplateParameterList *TemplateParams =
            getGenericLambdaTemplateParameterList(getCurLambda(), *this);
  // If a lambda appears in a dependent context or is a generic lambda (has
  // template parameters) and has an 'auto' return type, deduce it to a
  // dependent type.
  if (Class->isDependentContext() || TemplateParams) {
    const FunctionProtoType *FPT = MethodType->castAs<FunctionProtoType>();
    QualType Result = FPT->getReturnType();
    if (Result->isUndeducedType()) {
      Result = SubstAutoType(Result, Context.DependentTy);
      MethodType = Context.getFunctionType(Result, FPT->getParamTypes(),
                                           FPT->getExtProtoInfo());
    }
  }

  // C++11 [expr.prim.lambda]p5:
  //   The closure type for a lambda-expression has a public inline function
  //   call operator (13.5.4) whose parameters and return type are described by
  //   the lambda-expression's parameter-declaration-clause and
  //   trailing-return-type respectively.
  DeclarationName MethodName
    = Context.DeclarationNames.getCXXOperatorName(OO_Call);
  DeclarationNameLoc MethodNameLoc;
  MethodNameLoc.CXXOperatorName.BeginOpNameLoc
    = IntroducerRange.getBegin().getRawEncoding();
  MethodNameLoc.CXXOperatorName.EndOpNameLoc
    = IntroducerRange.getEnd().getRawEncoding();
  CXXMethodDecl *Method
    = CXXMethodDecl::Create(Context, Class, EndLoc,
                            DeclarationNameInfo(MethodName,
                                                IntroducerRange.getBegin(),
                                                MethodNameLoc),
                            MethodType, MethodTypeInfo,
                            SC_None,
                            /*isInline=*/true,
                            IsConstexprSpecified,
                            EndLoc);
  Method->setAccess(AS_public);

  // Temporarily set the lexical declaration context to the current
  // context, so that the Scope stack matches the lexical nesting.
  Method->setLexicalDeclContext(CurContext);
  // Create a function template if we have a template parameter list
  FunctionTemplateDecl *const TemplateMethod = TemplateParams ?
            FunctionTemplateDecl::Create(Context, Class,
                                         Method->getLocation(), MethodName,
                                         TemplateParams,
                                         Method) : nullptr;
  if (TemplateMethod) {
    TemplateMethod->setLexicalDeclContext(CurContext);
    TemplateMethod->setAccess(AS_public);
    Method->setDescribedFunctionTemplate(TemplateMethod);
  }

  // Add parameters.
  if (!Params.empty()) {
    Method->setParams(Params);
    CheckParmsForFunctionDef(Params,
                             /*CheckParameterNames=*/false);

    for (auto P : Method->parameters())
      P->setOwningFunction(Method);
  }

  Decl *ManglingContextDecl;
  if (MangleNumberingContext *MCtx =
          getCurrentMangleNumberContext(Class->getDeclContext(),
                                        ManglingContextDecl)) {
    unsigned ManglingNumber = MCtx->getManglingNumber(Method);
    Class->setLambdaMangling(ManglingNumber, ManglingContextDecl);
  }

  return Method;
}

void Sema::buildLambdaScope(LambdaScopeInfo *LSI,
                                        CXXMethodDecl *CallOperator,
                                        SourceRange IntroducerRange,
                                        LambdaCaptureDefault CaptureDefault,
                                        SourceLocation CaptureDefaultLoc,
                                        bool ExplicitParams,
                                        bool ExplicitResultType,
                                        bool Mutable) {
  LSI->CallOperator = CallOperator;
  CXXRecordDecl *LambdaClass = CallOperator->getParent();
  LSI->Lambda = LambdaClass;
  if (CaptureDefault == LCD_ByCopy)
    LSI->ImpCaptureStyle = LambdaScopeInfo::ImpCap_LambdaByval;
  else if (CaptureDefault == LCD_ByRef)
    LSI->ImpCaptureStyle = LambdaScopeInfo::ImpCap_LambdaByref;
  LSI->CaptureDefaultLoc = CaptureDefaultLoc;
  LSI->IntroducerRange = IntroducerRange;
  LSI->ExplicitParams = ExplicitParams;
  LSI->Mutable = Mutable;

  if (ExplicitResultType) {
    LSI->ReturnType = CallOperator->getReturnType();

    if (!LSI->ReturnType->isDependentType() &&
        !LSI->ReturnType->isVoidType()) {
      if (RequireCompleteType(CallOperator->getBeginLoc(), LSI->ReturnType,
                              diag::err_lambda_incomplete_result)) {
        // Do nothing.
      }
    }
  } else {
    LSI->HasImplicitReturnType = true;
  }
}

void Sema::finishLambdaExplicitCaptures(LambdaScopeInfo *LSI) {
  LSI->finishedExplicitCaptures();
}

void Sema::addLambdaParameters(
    ArrayRef<LambdaIntroducer::LambdaCapture> Captures,
    CXXMethodDecl *CallOperator, Scope *CurScope) {
  // Introduce our parameters into the function scope
  for (unsigned p = 0, NumParams = CallOperator->getNumParams();
       p < NumParams; ++p) {
    ParmVarDecl *Param = CallOperator->getParamDecl(p);

    // If this has an identifier, add it to the scope stack.
    if (CurScope && Param->getIdentifier()) {
      bool Error = false;
      // Resolution of CWG 2211 in C++17 renders shadowing ill-formed, but we
      // retroactively apply it.
      for (const auto &Capture : Captures) {
        if (Capture.Id == Param->getIdentifier()) {
          Error = true;
          Diag(Param->getLocation(), diag::err_parameter_shadow_capture);
          Diag(Capture.Loc, diag::note_var_explicitly_captured_here)
              << Capture.Id << true;
        }
      }
      if (!Error)
        CheckShadow(CurScope, Param);

      PushOnScopeChains(Param, CurScope);
    }
  }
}

/// If this expression is an enumerator-like expression of some type
/// T, return the type T; otherwise, return null.
///
/// Pointer comparisons on the result here should always work because
/// it's derived from either the parent of an EnumConstantDecl
/// (i.e. the definition) or the declaration returned by
/// EnumType::getDecl() (i.e. the definition).
static EnumDecl *findEnumForBlockReturn(Expr *E) {
  // An expression is an enumerator-like expression of type T if,
  // ignoring parens and parens-like expressions:
  E = E->IgnoreParens();

  //  - it is an enumerator whose enum type is T or
  if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (EnumConstantDecl *D
          = dyn_cast<EnumConstantDecl>(DRE->getDecl())) {
      return cast<EnumDecl>(D->getDeclContext());
    }
    return nullptr;
  }

  //  - it is a comma expression whose RHS is an enumerator-like
  //    expression of type T or
  if (BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
    if (BO->getOpcode() == BO_Comma)
      return findEnumForBlockReturn(BO->getRHS());
    return nullptr;
  }

  //  - it is a statement-expression whose value expression is an
  //    enumerator-like expression of type T or
  if (StmtExpr *SE = dyn_cast<StmtExpr>(E)) {
    if (Expr *last = dyn_cast_or_null<Expr>(SE->getSubStmt()->body_back()))
      return findEnumForBlockReturn(last);
    return nullptr;
  }

  //   - it is a ternary conditional operator (not the GNU ?:
  //     extension) whose second and third operands are
  //     enumerator-like expressions of type T or
  if (ConditionalOperator *CO = dyn_cast<ConditionalOperator>(E)) {
    if (EnumDecl *ED = findEnumForBlockReturn(CO->getTrueExpr()))
      if (ED == findEnumForBlockReturn(CO->getFalseExpr()))
        return ED;
    return nullptr;
  }

  // (implicitly:)
  //   - it is an implicit integral conversion applied to an
  //     enumerator-like expression of type T or
  if (ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(E)) {
    // We can sometimes see integral conversions in valid
    // enumerator-like expressions.
    if (ICE->getCastKind() == CK_IntegralCast)
      return findEnumForBlockReturn(ICE->getSubExpr());

    // Otherwise, just rely on the type.
  }

  //   - it is an expression of that formal enum type.
  if (const EnumType *ET = E->getType()->getAs<EnumType>()) {
    return ET->getDecl();
  }

  // Otherwise, nope.
  return nullptr;
}

/// Attempt to find a type T for which the returned expression of the
/// given statement is an enumerator-like expression of that type.
static EnumDecl *findEnumForBlockReturn(ReturnStmt *ret) {
  if (Expr *retValue = ret->getRetValue())
    return findEnumForBlockReturn(retValue);
  return nullptr;
}

/// Attempt to find a common type T for which all of the returned
/// expressions in a block are enumerator-like expressions of that
/// type.
static EnumDecl *findCommonEnumForBlockReturns(ArrayRef<ReturnStmt*> returns) {
  ArrayRef<ReturnStmt*>::iterator i = returns.begin(), e = returns.end();

  // Try to find one for the first return.
  EnumDecl *ED = findEnumForBlockReturn(*i);
  if (!ED) return nullptr;

  // Check that the rest of the returns have the same enum.
  for (++i; i != e; ++i) {
    if (findEnumForBlockReturn(*i) != ED)
      return nullptr;
  }

  // Never infer an anonymous enum type.
  if (!ED->hasNameForLinkage()) return nullptr;

  return ED;
}

/// Adjust the given return statements so that they formally return
/// the given type.  It should require, at most, an IntegralCast.
static void adjustBlockReturnsToEnum(Sema &S, ArrayRef<ReturnStmt*> returns,
                                     QualType returnType) {
  for (ArrayRef<ReturnStmt*>::iterator
         i = returns.begin(), e = returns.end(); i != e; ++i) {
    ReturnStmt *ret = *i;
    Expr *retValue = ret->getRetValue();
    if (S.Context.hasSameType(retValue->getType(), returnType))
      continue;

    // Right now we only support integral fixup casts.
    assert(returnType->isIntegralOrUnscopedEnumerationType());
    assert(retValue->getType()->isIntegralOrUnscopedEnumerationType());

    ExprWithCleanups *cleanups = dyn_cast<ExprWithCleanups>(retValue);

    Expr *E = (cleanups ? cleanups->getSubExpr() : retValue);
    E = ImplicitCastExpr::Create(S.Context, returnType, CK_IntegralCast,
                                 E, /*base path*/ nullptr, VK_RValue);
    if (cleanups) {
      cleanups->setSubExpr(E);
    } else {
      ret->setRetValue(E);
    }
  }
}

void Sema::deduceClosureReturnType(CapturingScopeInfo &CSI) {
  assert(CSI.HasImplicitReturnType);
  // If it was ever a placeholder, it had to been deduced to DependentTy.
  assert(CSI.ReturnType.isNull() || !CSI.ReturnType->isUndeducedType());
  assert((!isa<LambdaScopeInfo>(CSI) || !getLangOpts().CPlusPlus14) &&
         "lambda expressions use auto deduction in C++14 onwards");

  // C++ core issue 975:
  //   If a lambda-expression does not include a trailing-return-type,
  //   it is as if the trailing-return-type denotes the following type:
  //     - if there are no return statements in the compound-statement,
  //       or all return statements return either an expression of type
  //       void or no expression or braced-init-list, the type void;
  //     - otherwise, if all return statements return an expression
  //       and the types of the returned expressions after
  //       lvalue-to-rvalue conversion (4.1 [conv.lval]),
  //       array-to-pointer conversion (4.2 [conv.array]), and
  //       function-to-pointer conversion (4.3 [conv.func]) are the
  //       same, that common type;
  //     - otherwise, the program is ill-formed.
  //
  // C++ core issue 1048 additionally removes top-level cv-qualifiers
  // from the types of returned expressions to match the C++14 auto
  // deduction rules.
  //
  // In addition, in blocks in non-C++ modes, if all of the return
  // statements are enumerator-like expressions of some type T, where
  // T has a name for linkage, then we infer the return type of the
  // block to be that type.

  // First case: no return statements, implicit void return type.
  ASTContext &Ctx = getASTContext();
  if (CSI.Returns.empty()) {
    // It's possible there were simply no /valid/ return statements.
    // In this case, the first one we found may have at least given us a type.
    if (CSI.ReturnType.isNull())
      CSI.ReturnType = Ctx.VoidTy;
    return;
  }

  // Second case: at least one return statement has dependent type.
  // Delay type checking until instantiation.
  assert(!CSI.ReturnType.isNull() && "We should have a tentative return type.");
  if (CSI.ReturnType->isDependentType())
    return;

  // Try to apply the enum-fuzz rule.
  if (!getLangOpts().CPlusPlus) {
    assert(isa<BlockScopeInfo>(CSI));
    const EnumDecl *ED = findCommonEnumForBlockReturns(CSI.Returns);
    if (ED) {
      CSI.ReturnType = Context.getTypeDeclType(ED);
      adjustBlockReturnsToEnum(*this, CSI.Returns, CSI.ReturnType);
      return;
    }
  }

  // Third case: only one return statement. Don't bother doing extra work!
  if (CSI.Returns.size() == 1)
    return;

  // General case: many return statements.
  // Check that they all have compatible return types.

  // We require the return types to strictly match here.
  // Note that we've already done the required promotions as part of
  // processing the return statement.
  for (const ReturnStmt *RS : CSI.Returns) {
    const Expr *RetE = RS->getRetValue();

    QualType ReturnType =
        (RetE ? RetE->getType() : Context.VoidTy).getUnqualifiedType();
    if (Context.getCanonicalFunctionResultType(ReturnType) ==
          Context.getCanonicalFunctionResultType(CSI.ReturnType)) {
      // Use the return type with the strictest possible nullability annotation.
      auto RetTyNullability = ReturnType->getNullability(Ctx);
      auto BlockNullability = CSI.ReturnType->getNullability(Ctx);
      if (BlockNullability &&
          (!RetTyNullability ||
           hasWeakerNullability(*RetTyNullability, *BlockNullability)))
        CSI.ReturnType = ReturnType;
      continue;
    }

    // FIXME: This is a poor diagnostic for ReturnStmts without expressions.
    // TODO: It's possible that the *first* return is the divergent one.
    Diag(RS->getBeginLoc(),
         diag::err_typecheck_missing_return_type_incompatible)
        << ReturnType << CSI.ReturnType << isa<LambdaScopeInfo>(CSI);
    // Continue iterating so that we keep emitting diagnostics.
  }
}

QualType Sema::buildLambdaInitCaptureInitialization(SourceLocation Loc,
                                                    bool ByRef,
                                                    IdentifierInfo *Id,
                                                    bool IsDirectInit,
                                                    Expr *&Init) {
  // Create an 'auto' or 'auto&' TypeSourceInfo that we can use to
  // deduce against.
  QualType DeductType = Context.getAutoDeductType();
  TypeLocBuilder TLB;
  TLB.pushTypeSpec(DeductType).setNameLoc(Loc);
  if (ByRef) {
    DeductType = BuildReferenceType(DeductType, true, Loc, Id);
    assert(!DeductType.isNull() && "can't build reference to auto");
    TLB.push<ReferenceTypeLoc>(DeductType).setSigilLoc(Loc);
  }
  TypeSourceInfo *TSI = TLB.getTypeSourceInfo(Context, DeductType);

  // Deduce the type of the init capture.
  Expr *DeduceInit = Init;
  QualType DeducedType = deduceVarTypeFromInitializer(
      /*VarDecl*/nullptr, DeclarationName(Id), DeductType, TSI,
      SourceRange(Loc, Loc), IsDirectInit, DeduceInit);
  if (DeducedType.isNull())
    return QualType();

  // Are we a non-list direct initialization?
  bool CXXDirectInit = isa<ParenListExpr>(Init);

  // Perform initialization analysis and ensure any implicit conversions
  // (such as lvalue-to-rvalue) are enforced.
  InitializedEntity Entity =
      InitializedEntity::InitializeLambdaCapture(Id, DeducedType, Loc);
  InitializationKind Kind =
      IsDirectInit
          ? (CXXDirectInit ? InitializationKind::CreateDirect(
                                 Loc, Init->getBeginLoc(), Init->getEndLoc())
                           : InitializationKind::CreateDirectList(Loc))
          : InitializationKind::CreateCopy(Loc, Init->getBeginLoc());

  MultiExprArg Args = DeduceInit;
  QualType DclT;
  InitializationSequence InitSeq(*this, Entity, Kind, Args);
  ExprResult Result = InitSeq.Perform(*this, Entity, Kind, Args, &DclT);

  if (Result.isInvalid())
    return QualType();

  Init = Result.getAs<Expr>();
  return DeducedType;
}

VarDecl *Sema::createLambdaInitCaptureVarDecl(SourceLocation Loc,
                                              QualType InitCaptureType,
                                              IdentifierInfo *Id,
                                              unsigned InitStyle, Expr *Init) {
  TypeSourceInfo *TSI = Context.getTrivialTypeSourceInfo(InitCaptureType,
      Loc);
  // Create a dummy variable representing the init-capture. This is not actually
  // used as a variable, and only exists as a way to name and refer to the
  // init-capture.
  // FIXME: Pass in separate source locations for '&' and identifier.
  VarDecl *NewVD = VarDecl::Create(Context, CurContext, Loc,
                                   Loc, Id, InitCaptureType, TSI, SC_Auto);
  NewVD->setInitCapture(true);
  NewVD->setReferenced(true);
  // FIXME: Pass in a VarDecl::InitializationStyle.
  NewVD->setInitStyle(static_cast<VarDecl::InitializationStyle>(InitStyle));
  NewVD->markUsed(Context);
  NewVD->setInit(Init);
  return NewVD;
}

FieldDecl *Sema::buildInitCaptureField(LambdaScopeInfo *LSI, VarDecl *Var) {
  FieldDecl *Field = FieldDecl::Create(
      Context, LSI->Lambda, Var->getLocation(), Var->getLocation(),
      nullptr, Var->getType(), Var->getTypeSourceInfo(), nullptr, false,
      ICIS_NoInit);
  Field->setImplicit(true);
  Field->setAccess(AS_private);
  LSI->Lambda->addDecl(Field);

  LSI->addCapture(Var, /*isBlock*/false, Var->getType()->isReferenceType(),
                  /*isNested*/false, Var->getLocation(), SourceLocation(),
                  Var->getType(), Var->getInit());
  return Field;
}

void Sema::ActOnStartOfLambdaDefinition(LambdaIntroducer &Intro,
                                        Declarator &ParamInfo,
                                        Scope *CurScope) {
  // Determine if we're within a context where we know that the lambda will
  // be dependent, because there are template parameters in scope.
  bool KnownDependent = false;
  LambdaScopeInfo *const LSI = getCurLambda();
  assert(LSI && "LambdaScopeInfo should be on stack!");

  // The lambda-expression's closure type might be dependent even if its
  // semantic context isn't, if it appears within a default argument of a
  // function template.
  if (CurScope->getTemplateParamParent())
    KnownDependent = true;

  // Determine the signature of the call operator.
  TypeSourceInfo *MethodTyInfo;
  bool ExplicitParams = true;
  bool ExplicitResultType = true;
  bool ContainsUnexpandedParameterPack = false;
  SourceLocation EndLoc;
  SmallVector<ParmVarDecl *, 8> Params;
  if (ParamInfo.getNumTypeObjects() == 0) {
    // C++11 [expr.prim.lambda]p4:
    //   If a lambda-expression does not include a lambda-declarator, it is as
    //   if the lambda-declarator were ().
    FunctionProtoType::ExtProtoInfo EPI(Context.getDefaultCallingConvention(
        /*IsVariadic=*/false, /*IsCXXMethod=*/true));
    EPI.HasTrailingReturn = true;
    EPI.TypeQuals.addConst();
    // C++1y [expr.prim.lambda]:
    //   The lambda return type is 'auto', which is replaced by the
    //   trailing-return type if provided and/or deduced from 'return'
    //   statements
    // We don't do this before C++1y, because we don't support deduced return
    // types there.
    QualType DefaultTypeForNoTrailingReturn =
        getLangOpts().CPlusPlus14 ? Context.getAutoDeductType()
                                  : Context.DependentTy;
    QualType MethodTy =
        Context.getFunctionType(DefaultTypeForNoTrailingReturn, None, EPI);
    MethodTyInfo = Context.getTrivialTypeSourceInfo(MethodTy);
    ExplicitParams = false;
    ExplicitResultType = false;
    EndLoc = Intro.Range.getEnd();
  } else {
    assert(ParamInfo.isFunctionDeclarator() &&
           "lambda-declarator is a function");
    DeclaratorChunk::FunctionTypeInfo &FTI = ParamInfo.getFunctionTypeInfo();

    // C++11 [expr.prim.lambda]p5:
    //   This function call operator is declared const (9.3.1) if and only if
    //   the lambda-expression's parameter-declaration-clause is not followed
    //   by mutable. It is neither virtual nor declared volatile. [...]
    if (!FTI.hasMutableQualifier()) {
      FTI.getOrCreateMethodQualifiers().SetTypeQual(DeclSpec::TQ_const,
                                                    SourceLocation());
    }

    MethodTyInfo = GetTypeForDeclarator(ParamInfo, CurScope);
    assert(MethodTyInfo && "no type from lambda-declarator");
    EndLoc = ParamInfo.getSourceRange().getEnd();

    ExplicitResultType = FTI.hasTrailingReturnType();

    if (FTIHasNonVoidParameters(FTI)) {
      Params.reserve(FTI.NumParams);
      for (unsigned i = 0, e = FTI.NumParams; i != e; ++i)
        Params.push_back(cast<ParmVarDecl>(FTI.Params[i].Param));
    }

    // Check for unexpanded parameter packs in the method type.
    if (MethodTyInfo->getType()->containsUnexpandedParameterPack())
      ContainsUnexpandedParameterPack = true;
  }

  CXXRecordDecl *Class = createLambdaClosureType(Intro.Range, MethodTyInfo,
                                                 KnownDependent, Intro.Default);

  CXXMethodDecl *Method =
      startLambdaDefinition(Class, Intro.Range, MethodTyInfo, EndLoc, Params,
                            ParamInfo.getDeclSpec().isConstexprSpecified());
  if (ExplicitParams)
    CheckCXXDefaultArguments(Method);

  // This represents the function body for the lambda function, check if we
  // have to apply optnone due to a pragma.
  AddRangeBasedOptnone(Method);

  // code_seg attribute on lambda apply to the method.
  if (Attr *A = getImplicitCodeSegOrSectionAttrForFunction(Method, /*IsDefinition=*/true))
    Method->addAttr(A);

  // Attributes on the lambda apply to the method.
  ProcessDeclAttributes(CurScope, Method, ParamInfo);

  // CUDA lambdas get implicit attributes based on the scope in which they're
  // declared.
  if (getLangOpts().CUDA)
    CUDASetLambdaAttrs(Method);

  // Introduce the function call operator as the current declaration context.
  PushDeclContext(CurScope, Method);

  // Build the lambda scope.
  buildLambdaScope(LSI, Method, Intro.Range, Intro.Default, Intro.DefaultLoc,
                   ExplicitParams, ExplicitResultType, !Method->isConst());

  // C++11 [expr.prim.lambda]p9:
  //   A lambda-expression whose smallest enclosing scope is a block scope is a
  //   local lambda expression; any other lambda expression shall not have a
  //   capture-default or simple-capture in its lambda-introducer.
  //
  // For simple-captures, this is covered by the check below that any named
  // entity is a variable that can be captured.
  //
  // For DR1632, we also allow a capture-default in any context where we can
  // odr-use 'this' (in particular, in a default initializer for a non-static
  // data member).
  if (Intro.Default != LCD_None && !Class->getParent()->isFunctionOrMethod() &&
      (getCurrentThisType().isNull() ||
       CheckCXXThisCapture(SourceLocation(), /*Explicit*/true,
                           /*BuildAndDiagnose*/false)))
    Diag(Intro.DefaultLoc, diag::err_capture_default_non_local);

  // Distinct capture names, for diagnostics.
  llvm::SmallSet<IdentifierInfo*, 8> CaptureNames;

  // Handle explicit captures.
  SourceLocation PrevCaptureLoc
    = Intro.Default == LCD_None? Intro.Range.getBegin() : Intro.DefaultLoc;
  for (auto C = Intro.Captures.begin(), E = Intro.Captures.end(); C != E;
       PrevCaptureLoc = C->Loc, ++C) {
    if (C->Kind == LCK_This || C->Kind == LCK_StarThis) {
      if (C->Kind == LCK_StarThis)
        Diag(C->Loc, !getLangOpts().CPlusPlus17
                             ? diag::ext_star_this_lambda_capture_cxx17
                             : diag::warn_cxx14_compat_star_this_lambda_capture);

      // C++11 [expr.prim.lambda]p8:
      //   An identifier or this shall not appear more than once in a
      //   lambda-capture.
      if (LSI->isCXXThisCaptured()) {
        Diag(C->Loc, diag::err_capture_more_than_once)
            << "'this'" << SourceRange(LSI->getCXXThisCapture().getLocation())
            << FixItHint::CreateRemoval(
                   SourceRange(getLocForEndOfToken(PrevCaptureLoc), C->Loc));
        continue;
      }

      // C++2a [expr.prim.lambda]p8:
      //  If a lambda-capture includes a capture-default that is =,
      //  each simple-capture of that lambda-capture shall be of the form
      //  "&identifier", "this", or "* this". [ Note: The form [&,this] is
      //  redundant but accepted for compatibility with ISO C++14. --end note ]
      if (Intro.Default == LCD_ByCopy && C->Kind != LCK_StarThis)
        Diag(C->Loc, !getLangOpts().CPlusPlus2a
                         ? diag::ext_equals_this_lambda_capture_cxx2a
                         : diag::warn_cxx17_compat_equals_this_lambda_capture);

      // C++11 [expr.prim.lambda]p12:
      //   If this is captured by a local lambda expression, its nearest
      //   enclosing function shall be a non-static member function.
      QualType ThisCaptureType = getCurrentThisType();
      if (ThisCaptureType.isNull()) {
        Diag(C->Loc, diag::err_this_capture) << true;
        continue;
      }

      CheckCXXThisCapture(C->Loc, /*Explicit=*/true, /*BuildAndDiagnose*/ true,
                          /*FunctionScopeIndexToStopAtPtr*/ nullptr,
                          C->Kind == LCK_StarThis);
      if (!LSI->Captures.empty())
        LSI->ExplicitCaptureRanges[LSI->Captures.size() - 1] = C->ExplicitRange;
      continue;
    }

    assert(C->Id && "missing identifier for capture");

    if (C->Init.isInvalid())
      continue;

    VarDecl *Var = nullptr;
    if (C->Init.isUsable()) {
      Diag(C->Loc, getLangOpts().CPlusPlus14
                       ? diag::warn_cxx11_compat_init_capture
                       : diag::ext_init_capture);

      if (C->Init.get()->containsUnexpandedParameterPack())
        ContainsUnexpandedParameterPack = true;
      // If the initializer expression is usable, but the InitCaptureType
      // is not, then an error has occurred - so ignore the capture for now.
      // for e.g., [n{0}] { }; <-- if no <initializer_list> is included.
      // FIXME: we should create the init capture variable and mark it invalid
      // in this case.
      if (C->InitCaptureType.get().isNull())
        continue;

      unsigned InitStyle;
      switch (C->InitKind) {
      case LambdaCaptureInitKind::NoInit:
        llvm_unreachable("not an init-capture?");
      case LambdaCaptureInitKind::CopyInit:
        InitStyle = VarDecl::CInit;
        break;
      case LambdaCaptureInitKind::DirectInit:
        InitStyle = VarDecl::CallInit;
        break;
      case LambdaCaptureInitKind::ListInit:
        InitStyle = VarDecl::ListInit;
        break;
      }
      Var = createLambdaInitCaptureVarDecl(C->Loc, C->InitCaptureType.get(),
                                           C->Id, InitStyle, C->Init.get());
      // C++1y [expr.prim.lambda]p11:
      //   An init-capture behaves as if it declares and explicitly
      //   captures a variable [...] whose declarative region is the
      //   lambda-expression's compound-statement
      if (Var)
        PushOnScopeChains(Var, CurScope, false);
    } else {
      assert(C->InitKind == LambdaCaptureInitKind::NoInit &&
             "init capture has valid but null init?");

      // C++11 [expr.prim.lambda]p8:
      //   If a lambda-capture includes a capture-default that is &, the
      //   identifiers in the lambda-capture shall not be preceded by &.
      //   If a lambda-capture includes a capture-default that is =, [...]
      //   each identifier it contains shall be preceded by &.
      if (C->Kind == LCK_ByRef && Intro.Default == LCD_ByRef) {
        Diag(C->Loc, diag::err_reference_capture_with_reference_default)
            << FixItHint::CreateRemoval(
                SourceRange(getLocForEndOfToken(PrevCaptureLoc), C->Loc));
        continue;
      } else if (C->Kind == LCK_ByCopy && Intro.Default == LCD_ByCopy) {
        Diag(C->Loc, diag::err_copy_capture_with_copy_default)
            << FixItHint::CreateRemoval(
                SourceRange(getLocForEndOfToken(PrevCaptureLoc), C->Loc));
        continue;
      }

      // C++11 [expr.prim.lambda]p10:
      //   The identifiers in a capture-list are looked up using the usual
      //   rules for unqualified name lookup (3.4.1)
      DeclarationNameInfo Name(C->Id, C->Loc);
      LookupResult R(*this, Name, LookupOrdinaryName);
      LookupName(R, CurScope);
      if (R.isAmbiguous())
        continue;
      if (R.empty()) {
        // FIXME: Disable corrections that would add qualification?
        CXXScopeSpec ScopeSpec;
        if (DiagnoseEmptyLookup(CurScope, ScopeSpec, R,
                                llvm::make_unique<DeclFilterCCC<VarDecl>>()))
          continue;
      }

      Var = R.getAsSingle<VarDecl>();
      if (Var && DiagnoseUseOfDecl(Var, C->Loc))
        continue;
    }

    // C++11 [expr.prim.lambda]p8:
    //   An identifier or this shall not appear more than once in a
    //   lambda-capture.
    if (!CaptureNames.insert(C->Id).second) {
      if (Var && LSI->isCaptured(Var)) {
        Diag(C->Loc, diag::err_capture_more_than_once)
            << C->Id << SourceRange(LSI->getCapture(Var).getLocation())
            << FixItHint::CreateRemoval(
                   SourceRange(getLocForEndOfToken(PrevCaptureLoc), C->Loc));
      } else
        // Previous capture captured something different (one or both was
        // an init-cpature): no fixit.
        Diag(C->Loc, diag::err_capture_more_than_once) << C->Id;
      continue;
    }

    // C++11 [expr.prim.lambda]p10:
    //   [...] each such lookup shall find a variable with automatic storage
    //   duration declared in the reaching scope of the local lambda expression.
    // Note that the 'reaching scope' check happens in tryCaptureVariable().
    if (!Var) {
      Diag(C->Loc, diag::err_capture_does_not_name_variable) << C->Id;
      continue;
    }

    // Ignore invalid decls; they'll just confuse the code later.
    if (Var->isInvalidDecl())
      continue;

    if (!Var->hasLocalStorage()) {
      Diag(C->Loc, diag::err_capture_non_automatic_variable) << C->Id;
      Diag(Var->getLocation(), diag::note_previous_decl) << C->Id;
      continue;
    }

    // C++11 [expr.prim.lambda]p23:
    //   A capture followed by an ellipsis is a pack expansion (14.5.3).
    SourceLocation EllipsisLoc;
    if (C->EllipsisLoc.isValid()) {
      if (Var->isParameterPack()) {
        EllipsisLoc = C->EllipsisLoc;
      } else {
        Diag(C->EllipsisLoc, diag::err_pack_expansion_without_parameter_packs)
          << SourceRange(C->Loc);

        // Just ignore the ellipsis.
      }
    } else if (Var->isParameterPack()) {
      ContainsUnexpandedParameterPack = true;
    }

    if (C->Init.isUsable()) {
      buildInitCaptureField(LSI, Var);
    } else {
      TryCaptureKind Kind = C->Kind == LCK_ByRef ? TryCapture_ExplicitByRef :
                                                   TryCapture_ExplicitByVal;
      tryCaptureVariable(Var, C->Loc, Kind, EllipsisLoc);
    }
    if (!LSI->Captures.empty())
      LSI->ExplicitCaptureRanges[LSI->Captures.size() - 1] = C->ExplicitRange;
  }
  finishLambdaExplicitCaptures(LSI);

  LSI->ContainsUnexpandedParameterPack = ContainsUnexpandedParameterPack;

  // Add lambda parameters into scope.
  addLambdaParameters(Intro.Captures, Method, CurScope);

  // Enter a new evaluation context to insulate the lambda from any
  // cleanups from the enclosing full-expression.
  PushExpressionEvaluationContext(
      ExpressionEvaluationContext::PotentiallyEvaluated);
}

void Sema::ActOnLambdaError(SourceLocation StartLoc, Scope *CurScope,
                            bool IsInstantiation) {
  LambdaScopeInfo *LSI = cast<LambdaScopeInfo>(FunctionScopes.back());

  // Leave the expression-evaluation context.
  DiscardCleanupsInEvaluationContext();
  PopExpressionEvaluationContext();

  // Leave the context of the lambda.
  if (!IsInstantiation)
    PopDeclContext();

  // Finalize the lambda.
  CXXRecordDecl *Class = LSI->Lambda;
  Class->setInvalidDecl();
  SmallVector<Decl*, 4> Fields(Class->fields());
  ActOnFields(nullptr, Class->getLocation(), Class, Fields, SourceLocation(),
              SourceLocation(), ParsedAttributesView());
  CheckCompletedCXXClass(Class);

  PopFunctionScopeInfo();
}

QualType Sema::getLambdaConversionFunctionResultType(
    const FunctionProtoType *CallOpProto) {
  // The function type inside the pointer type is the same as the call
  // operator with some tweaks. The calling convention is the default free
  // function convention, and the type qualifications are lost.
  const FunctionProtoType::ExtProtoInfo CallOpExtInfo =
      CallOpProto->getExtProtoInfo();
  FunctionProtoType::ExtProtoInfo InvokerExtInfo = CallOpExtInfo;
  CallingConv CC = Context.getDefaultCallingConvention(
      CallOpProto->isVariadic(), /*IsCXXMethod=*/false);
  InvokerExtInfo.ExtInfo = InvokerExtInfo.ExtInfo.withCallingConv(CC);
  InvokerExtInfo.TypeQuals = Qualifiers();
  assert(InvokerExtInfo.RefQualifier == RQ_None &&
      "Lambda's call operator should not have a reference qualifier");
  return Context.getFunctionType(CallOpProto->getReturnType(),
                                 CallOpProto->getParamTypes(), InvokerExtInfo);
}

/// Add a lambda's conversion to function pointer, as described in
/// C++11 [expr.prim.lambda]p6.
static void addFunctionPointerConversion(Sema &S,
                                         SourceRange IntroducerRange,
                                         CXXRecordDecl *Class,
                                         CXXMethodDecl *CallOperator) {
  // This conversion is explicitly disabled if the lambda's function has
  // pass_object_size attributes on any of its parameters.
  auto HasPassObjectSizeAttr = [](const ParmVarDecl *P) {
    return P->hasAttr<PassObjectSizeAttr>();
  };
  if (llvm::any_of(CallOperator->parameters(), HasPassObjectSizeAttr))
    return;

  // Add the conversion to function pointer.
  QualType InvokerFunctionTy = S.getLambdaConversionFunctionResultType(
      CallOperator->getType()->castAs<FunctionProtoType>());
  QualType PtrToFunctionTy = S.Context.getPointerType(InvokerFunctionTy);

  // Create the type of the conversion function.
  FunctionProtoType::ExtProtoInfo ConvExtInfo(
      S.Context.getDefaultCallingConvention(
      /*IsVariadic=*/false, /*IsCXXMethod=*/true));
  // The conversion function is always const.
  ConvExtInfo.TypeQuals = Qualifiers();
  ConvExtInfo.TypeQuals.addConst();
  QualType ConvTy =
      S.Context.getFunctionType(PtrToFunctionTy, None, ConvExtInfo);

  SourceLocation Loc = IntroducerRange.getBegin();
  DeclarationName ConversionName
    = S.Context.DeclarationNames.getCXXConversionFunctionName(
        S.Context.getCanonicalType(PtrToFunctionTy));
  DeclarationNameLoc ConvNameLoc;
  // Construct a TypeSourceInfo for the conversion function, and wire
  // all the parameters appropriately for the FunctionProtoTypeLoc
  // so that everything works during transformation/instantiation of
  // generic lambdas.
  // The main reason for wiring up the parameters of the conversion
  // function with that of the call operator is so that constructs
  // like the following work:
  // auto L = [](auto b) {                <-- 1
  //   return [](auto a) -> decltype(a) { <-- 2
  //      return a;
  //   };
  // };
  // int (*fp)(int) = L(5);
  // Because the trailing return type can contain DeclRefExprs that refer
  // to the original call operator's variables, we hijack the call
  // operators ParmVarDecls below.
  TypeSourceInfo *ConvNamePtrToFunctionTSI =
      S.Context.getTrivialTypeSourceInfo(PtrToFunctionTy, Loc);
  ConvNameLoc.NamedType.TInfo = ConvNamePtrToFunctionTSI;

  // The conversion function is a conversion to a pointer-to-function.
  TypeSourceInfo *ConvTSI = S.Context.getTrivialTypeSourceInfo(ConvTy, Loc);
  FunctionProtoTypeLoc ConvTL =
      ConvTSI->getTypeLoc().getAs<FunctionProtoTypeLoc>();
  // Get the result of the conversion function which is a pointer-to-function.
  PointerTypeLoc PtrToFunctionTL =
      ConvTL.getReturnLoc().getAs<PointerTypeLoc>();
  // Do the same for the TypeSourceInfo that is used to name the conversion
  // operator.
  PointerTypeLoc ConvNamePtrToFunctionTL =
      ConvNamePtrToFunctionTSI->getTypeLoc().getAs<PointerTypeLoc>();

  // Get the underlying function types that the conversion function will
  // be converting to (should match the type of the call operator).
  FunctionProtoTypeLoc CallOpConvTL =
      PtrToFunctionTL.getPointeeLoc().getAs<FunctionProtoTypeLoc>();
  FunctionProtoTypeLoc CallOpConvNameTL =
    ConvNamePtrToFunctionTL.getPointeeLoc().getAs<FunctionProtoTypeLoc>();

  // Wire up the FunctionProtoTypeLocs with the call operator's parameters.
  // These parameter's are essentially used to transform the name and
  // the type of the conversion operator.  By using the same parameters
  // as the call operator's we don't have to fix any back references that
  // the trailing return type of the call operator's uses (such as
  // decltype(some_type<decltype(a)>::type{} + decltype(a){}) etc.)
  // - we can simply use the return type of the call operator, and
  // everything should work.
  SmallVector<ParmVarDecl *, 4> InvokerParams;
  for (unsigned I = 0, N = CallOperator->getNumParams(); I != N; ++I) {
    ParmVarDecl *From = CallOperator->getParamDecl(I);

    InvokerParams.push_back(ParmVarDecl::Create(
        S.Context,
        // Temporarily add to the TU. This is set to the invoker below.
        S.Context.getTranslationUnitDecl(), From->getBeginLoc(),
        From->getLocation(), From->getIdentifier(), From->getType(),
        From->getTypeSourceInfo(), From->getStorageClass(),
        /*DefaultArg=*/nullptr));
    CallOpConvTL.setParam(I, From);
    CallOpConvNameTL.setParam(I, From);
  }

  CXXConversionDecl *Conversion = CXXConversionDecl::Create(
      S.Context, Class, Loc,
      DeclarationNameInfo(ConversionName, Loc, ConvNameLoc), ConvTy, ConvTSI,
      /*isInline=*/true, /*isExplicit=*/false,
      /*isConstexpr=*/S.getLangOpts().CPlusPlus17,
      CallOperator->getBody()->getEndLoc());
  Conversion->setAccess(AS_public);
  Conversion->setImplicit(true);

  if (Class->isGenericLambda()) {
    // Create a template version of the conversion operator, using the template
    // parameter list of the function call operator.
    FunctionTemplateDecl *TemplateCallOperator =
            CallOperator->getDescribedFunctionTemplate();
    FunctionTemplateDecl *ConversionTemplate =
                  FunctionTemplateDecl::Create(S.Context, Class,
                                      Loc, ConversionName,
                                      TemplateCallOperator->getTemplateParameters(),
                                      Conversion);
    ConversionTemplate->setAccess(AS_public);
    ConversionTemplate->setImplicit(true);
    Conversion->setDescribedFunctionTemplate(ConversionTemplate);
    Class->addDecl(ConversionTemplate);
  } else
    Class->addDecl(Conversion);
  // Add a non-static member function that will be the result of
  // the conversion with a certain unique ID.
  DeclarationName InvokerName = &S.Context.Idents.get(
                                                 getLambdaStaticInvokerName());
  // FIXME: Instead of passing in the CallOperator->getTypeSourceInfo()
  // we should get a prebuilt TrivialTypeSourceInfo from Context
  // using FunctionTy & Loc and get its TypeLoc as a FunctionProtoTypeLoc
  // then rewire the parameters accordingly, by hoisting up the InvokeParams
  // loop below and then use its Params to set Invoke->setParams(...) below.
  // This would avoid the 'const' qualifier of the calloperator from
  // contaminating the type of the invoker, which is currently adjusted
  // in SemaTemplateDeduction.cpp:DeduceTemplateArguments.  Fixing the
  // trailing return type of the invoker would require a visitor to rebuild
  // the trailing return type and adjusting all back DeclRefExpr's to refer
  // to the new static invoker parameters - not the call operator's.
  CXXMethodDecl *Invoke = CXXMethodDecl::Create(
      S.Context, Class, Loc, DeclarationNameInfo(InvokerName, Loc),
      InvokerFunctionTy, CallOperator->getTypeSourceInfo(), SC_Static,
      /*IsInline=*/true,
      /*IsConstexpr=*/false, CallOperator->getBody()->getEndLoc());
  for (unsigned I = 0, N = CallOperator->getNumParams(); I != N; ++I)
    InvokerParams[I]->setOwningFunction(Invoke);
  Invoke->setParams(InvokerParams);
  Invoke->setAccess(AS_private);
  Invoke->setImplicit(true);
  if (Class->isGenericLambda()) {
    FunctionTemplateDecl *TemplateCallOperator =
            CallOperator->getDescribedFunctionTemplate();
    FunctionTemplateDecl *StaticInvokerTemplate = FunctionTemplateDecl::Create(
                          S.Context, Class, Loc, InvokerName,
                          TemplateCallOperator->getTemplateParameters(),
                          Invoke);
    StaticInvokerTemplate->setAccess(AS_private);
    StaticInvokerTemplate->setImplicit(true);
    Invoke->setDescribedFunctionTemplate(StaticInvokerTemplate);
    Class->addDecl(StaticInvokerTemplate);
  } else
    Class->addDecl(Invoke);
}

/// Add a lambda's conversion to block pointer.
static void addBlockPointerConversion(Sema &S,
                                      SourceRange IntroducerRange,
                                      CXXRecordDecl *Class,
                                      CXXMethodDecl *CallOperator) {
  QualType FunctionTy = S.getLambdaConversionFunctionResultType(
      CallOperator->getType()->castAs<FunctionProtoType>());
  QualType BlockPtrTy = S.Context.getBlockPointerType(FunctionTy);

  FunctionProtoType::ExtProtoInfo ConversionEPI(
      S.Context.getDefaultCallingConvention(
          /*IsVariadic=*/false, /*IsCXXMethod=*/true));
  ConversionEPI.TypeQuals = Qualifiers();
  ConversionEPI.TypeQuals.addConst();
  QualType ConvTy = S.Context.getFunctionType(BlockPtrTy, None, ConversionEPI);

  SourceLocation Loc = IntroducerRange.getBegin();
  DeclarationName Name
    = S.Context.DeclarationNames.getCXXConversionFunctionName(
        S.Context.getCanonicalType(BlockPtrTy));
  DeclarationNameLoc NameLoc;
  NameLoc.NamedType.TInfo = S.Context.getTrivialTypeSourceInfo(BlockPtrTy, Loc);
  CXXConversionDecl *Conversion = CXXConversionDecl::Create(
      S.Context, Class, Loc, DeclarationNameInfo(Name, Loc, NameLoc), ConvTy,
      S.Context.getTrivialTypeSourceInfo(ConvTy, Loc),
      /*isInline=*/true, /*isExplicit=*/false,
      /*isConstexpr=*/false, CallOperator->getBody()->getEndLoc());
  Conversion->setAccess(AS_public);
  Conversion->setImplicit(true);
  Class->addDecl(Conversion);
}

static ExprResult performLambdaVarCaptureInitialization(
    Sema &S, const Capture &Capture, FieldDecl *Field,
    SourceLocation ImplicitCaptureLoc, bool IsImplicitCapture) {
  assert(Capture.isVariableCapture() && "not a variable capture");

  auto *Var = Capture.getVariable();
  SourceLocation Loc =
      IsImplicitCapture ? ImplicitCaptureLoc : Capture.getLocation();

  // C++11 [expr.prim.lambda]p21:
  //   When the lambda-expression is evaluated, the entities that
  //   are captured by copy are used to direct-initialize each
  //   corresponding non-static data member of the resulting closure
  //   object. (For array members, the array elements are
  //   direct-initialized in increasing subscript order.) These
  //   initializations are performed in the (unspecified) order in
  //   which the non-static data members are declared.

  // C++ [expr.prim.lambda]p12:
  //   An entity captured by a lambda-expression is odr-used (3.2) in
  //   the scope containing the lambda-expression.
  ExprResult RefResult = S.BuildDeclarationNameExpr(
      CXXScopeSpec(), DeclarationNameInfo(Var->getDeclName(), Loc), Var);
  if (RefResult.isInvalid())
    return ExprError();
  Expr *Ref = RefResult.get();

  auto Entity = InitializedEntity::InitializeLambdaCapture(
      Var->getIdentifier(), Field->getType(), Loc);
  InitializationKind InitKind = InitializationKind::CreateDirect(Loc, Loc, Loc);
  InitializationSequence Init(S, Entity, InitKind, Ref);
  return Init.Perform(S, Entity, InitKind, Ref);
}

ExprResult Sema::ActOnLambdaExpr(SourceLocation StartLoc, Stmt *Body,
                                 Scope *CurScope) {
  LambdaScopeInfo LSI = *cast<LambdaScopeInfo>(FunctionScopes.back());
  ActOnFinishFunctionBody(LSI.CallOperator, Body);
  return BuildLambdaExpr(StartLoc, Body->getEndLoc(), &LSI);
}

static LambdaCaptureDefault
mapImplicitCaptureStyle(CapturingScopeInfo::ImplicitCaptureStyle ICS) {
  switch (ICS) {
  case CapturingScopeInfo::ImpCap_None:
    return LCD_None;
  case CapturingScopeInfo::ImpCap_LambdaByval:
    return LCD_ByCopy;
  case CapturingScopeInfo::ImpCap_CapturedRegion:
  case CapturingScopeInfo::ImpCap_LambdaByref:
    return LCD_ByRef;
  case CapturingScopeInfo::ImpCap_Block:
    llvm_unreachable("block capture in lambda");
  }
  llvm_unreachable("Unknown implicit capture style");
}

bool Sema::CaptureHasSideEffects(const Capture &From) {
  if (!From.isVLATypeCapture()) {
    Expr *Init = From.getInitExpr();
    if (Init && Init->HasSideEffects(Context))
      return true;
  }

  if (!From.isCopyCapture())
    return false;

  const QualType T = From.isThisCapture()
                         ? getCurrentThisType()->getPointeeType()
                         : From.getCaptureType();

  if (T.isVolatileQualified())
    return true;

  const Type *BaseT = T->getBaseElementTypeUnsafe();
  if (const CXXRecordDecl *RD = BaseT->getAsCXXRecordDecl())
    return !RD->isCompleteDefinition() || !RD->hasTrivialCopyConstructor() ||
           !RD->hasTrivialDestructor();

  return false;
}

bool Sema::DiagnoseUnusedLambdaCapture(SourceRange CaptureRange,
                                       const Capture &From) {
  if (CaptureHasSideEffects(From))
    return false;

  if (From.isVLATypeCapture())
    return false;

  auto diag = Diag(From.getLocation(), diag::warn_unused_lambda_capture);
  if (From.isThisCapture())
    diag << "'this'";
  else
    diag << From.getVariable();
  diag << From.isNonODRUsed();
  diag << FixItHint::CreateRemoval(CaptureRange);
  return true;
}

ExprResult Sema::BuildLambdaExpr(SourceLocation StartLoc, SourceLocation EndLoc,
                                 LambdaScopeInfo *LSI) {
  // Collect information from the lambda scope.
  SmallVector<LambdaCapture, 4> Captures;
  SmallVector<Expr *, 4> CaptureInits;
  SourceLocation CaptureDefaultLoc = LSI->CaptureDefaultLoc;
  LambdaCaptureDefault CaptureDefault =
      mapImplicitCaptureStyle(LSI->ImpCaptureStyle);
  CXXRecordDecl *Class;
  CXXMethodDecl *CallOperator;
  SourceRange IntroducerRange;
  bool ExplicitParams;
  bool ExplicitResultType;
  CleanupInfo LambdaCleanup;
  bool ContainsUnexpandedParameterPack;
  bool IsGenericLambda;
  {
    CallOperator = LSI->CallOperator;
    Class = LSI->Lambda;
    IntroducerRange = LSI->IntroducerRange;
    ExplicitParams = LSI->ExplicitParams;
    ExplicitResultType = !LSI->HasImplicitReturnType;
    LambdaCleanup = LSI->Cleanup;
    ContainsUnexpandedParameterPack = LSI->ContainsUnexpandedParameterPack;
    IsGenericLambda = Class->isGenericLambda();

    CallOperator->setLexicalDeclContext(Class);
    Decl *TemplateOrNonTemplateCallOperatorDecl =
        CallOperator->getDescribedFunctionTemplate()
        ? CallOperator->getDescribedFunctionTemplate()
        : cast<Decl>(CallOperator);

    TemplateOrNonTemplateCallOperatorDecl->setLexicalDeclContext(Class);
    Class->addDecl(TemplateOrNonTemplateCallOperatorDecl);

    PopExpressionEvaluationContext();

    // Translate captures.
    auto CurField = Class->field_begin();
    // True if the current capture has a used capture or default before it.
    bool CurHasPreviousCapture = CaptureDefault != LCD_None;
    SourceLocation PrevCaptureLoc = CurHasPreviousCapture ?
        CaptureDefaultLoc : IntroducerRange.getBegin();

    for (unsigned I = 0, N = LSI->Captures.size(); I != N; ++I, ++CurField) {
      const Capture &From = LSI->Captures[I];

      assert(!From.isBlockCapture() && "Cannot capture __block variables");
      bool IsImplicit = I >= LSI->NumExplicitCaptures;

      // Use source ranges of explicit captures for fixits where available.
      SourceRange CaptureRange = LSI->ExplicitCaptureRanges[I];

      // Warn about unused explicit captures.
      bool IsCaptureUsed = true;
      if (!CurContext->isDependentContext() && !IsImplicit && !From.isODRUsed()) {
        // Initialized captures that are non-ODR used may not be eliminated.
        bool NonODRUsedInitCapture =
            IsGenericLambda && From.isNonODRUsed() && From.getInitExpr();
        if (!NonODRUsedInitCapture) {
          bool IsLast = (I + 1) == LSI->NumExplicitCaptures;
          SourceRange FixItRange;
          if (CaptureRange.isValid()) {
            if (!CurHasPreviousCapture && !IsLast) {
              // If there are no captures preceding this capture, remove the
              // following comma.
              FixItRange = SourceRange(CaptureRange.getBegin(),
                                       getLocForEndOfToken(CaptureRange.getEnd()));
            } else {
              // Otherwise, remove the comma since the last used capture.
              FixItRange = SourceRange(getLocForEndOfToken(PrevCaptureLoc),
                                       CaptureRange.getEnd());
            }
          }

          IsCaptureUsed = !DiagnoseUnusedLambdaCapture(FixItRange, From);
        }
      }

      if (CaptureRange.isValid()) {
        CurHasPreviousCapture |= IsCaptureUsed;
        PrevCaptureLoc = CaptureRange.getEnd();
      }

      // Handle 'this' capture.
      if (From.isThisCapture()) {
        // Capturing 'this' implicitly with a default of '[=]' is deprecated,
        // because it results in a reference capture. Don't warn prior to
        // C++2a; there's nothing that can be done about it before then.
        if (getLangOpts().CPlusPlus2a && IsImplicit &&
            CaptureDefault == LCD_ByCopy) {
          Diag(From.getLocation(), diag::warn_deprecated_this_capture);
          Diag(CaptureDefaultLoc, diag::note_deprecated_this_capture)
              << FixItHint::CreateInsertion(
                     getLocForEndOfToken(CaptureDefaultLoc), ", this");
        }

        Captures.push_back(
            LambdaCapture(From.getLocation(), IsImplicit,
                          From.isCopyCapture() ? LCK_StarThis : LCK_This));
        CaptureInits.push_back(From.getInitExpr());
        continue;
      }
      if (From.isVLATypeCapture()) {
        Captures.push_back(
            LambdaCapture(From.getLocation(), IsImplicit, LCK_VLAType));
        CaptureInits.push_back(nullptr);
        continue;
      }

      VarDecl *Var = From.getVariable();
      LambdaCaptureKind Kind = From.isCopyCapture() ? LCK_ByCopy : LCK_ByRef;
      Captures.push_back(LambdaCapture(From.getLocation(), IsImplicit, Kind,
                                       Var, From.getEllipsisLoc()));
      Expr *Init = From.getInitExpr();
      if (!Init) {
        auto InitResult = performLambdaVarCaptureInitialization(
            *this, From, *CurField, CaptureDefaultLoc, IsImplicit);
        if (InitResult.isInvalid())
          return ExprError();
        Init = InitResult.get();
      }
      CaptureInits.push_back(Init);
    }

    // C++11 [expr.prim.lambda]p6:
    //   The closure type for a lambda-expression with no lambda-capture
    //   has a public non-virtual non-explicit const conversion function
    //   to pointer to function having the same parameter and return
    //   types as the closure type's function call operator.
    if (Captures.empty() && CaptureDefault == LCD_None)
      addFunctionPointerConversion(*this, IntroducerRange, Class,
                                   CallOperator);

    // Objective-C++:
    //   The closure type for a lambda-expression has a public non-virtual
    //   non-explicit const conversion function to a block pointer having the
    //   same parameter and return types as the closure type's function call
    //   operator.
    // FIXME: Fix generic lambda to block conversions.
    if (getLangOpts().Blocks && getLangOpts().ObjC && !IsGenericLambda)
      addBlockPointerConversion(*this, IntroducerRange, Class, CallOperator);

    // Finalize the lambda class.
    SmallVector<Decl*, 4> Fields(Class->fields());
    ActOnFields(nullptr, Class->getLocation(), Class, Fields, SourceLocation(),
                SourceLocation(), ParsedAttributesView());
    CheckCompletedCXXClass(Class);
  }

  Cleanup.mergeFrom(LambdaCleanup);

  LambdaExpr *Lambda = LambdaExpr::Create(Context, Class, IntroducerRange,
                                          CaptureDefault, CaptureDefaultLoc,
                                          Captures,
                                          ExplicitParams, ExplicitResultType,
                                          CaptureInits, EndLoc,
                                          ContainsUnexpandedParameterPack);
  // If the lambda expression's call operator is not explicitly marked constexpr
  // and we are not in a dependent context, analyze the call operator to infer
  // its constexpr-ness, suppressing diagnostics while doing so.
  if (getLangOpts().CPlusPlus17 && !CallOperator->isInvalidDecl() &&
      !CallOperator->isConstexpr() &&
      !isa<CoroutineBodyStmt>(CallOperator->getBody()) &&
      !Class->getDeclContext()->isDependentContext()) {
    TentativeAnalysisScope DiagnosticScopeGuard(*this);
    CallOperator->setConstexpr(
        CheckConstexprFunctionDecl(CallOperator) &&
        CheckConstexprFunctionBody(CallOperator, CallOperator->getBody()));
  }

  // Emit delayed shadowing warnings now that the full capture list is known.
  DiagnoseShadowingLambdaDecls(LSI);

  if (!CurContext->isDependentContext()) {
    switch (ExprEvalContexts.back().Context) {
    // C++11 [expr.prim.lambda]p2:
    //   A lambda-expression shall not appear in an unevaluated operand
    //   (Clause 5).
    case ExpressionEvaluationContext::Unevaluated:
    case ExpressionEvaluationContext::UnevaluatedList:
    case ExpressionEvaluationContext::UnevaluatedAbstract:
    // C++1y [expr.const]p2:
    //   A conditional-expression e is a core constant expression unless the
    //   evaluation of e, following the rules of the abstract machine, would
    //   evaluate [...] a lambda-expression.
    //
    // This is technically incorrect, there are some constant evaluated contexts
    // where this should be allowed.  We should probably fix this when DR1607 is
    // ratified, it lays out the exact set of conditions where we shouldn't
    // allow a lambda-expression.
    case ExpressionEvaluationContext::ConstantEvaluated:
      // We don't actually diagnose this case immediately, because we
      // could be within a context where we might find out later that
      // the expression is potentially evaluated (e.g., for typeid).
      ExprEvalContexts.back().Lambdas.push_back(Lambda);
      break;

    case ExpressionEvaluationContext::DiscardedStatement:
    case ExpressionEvaluationContext::PotentiallyEvaluated:
    case ExpressionEvaluationContext::PotentiallyEvaluatedIfUsed:
      break;
    }
  }

  return MaybeBindToTemporary(Lambda);
}

ExprResult Sema::BuildBlockForLambdaConversion(SourceLocation CurrentLocation,
                                               SourceLocation ConvLocation,
                                               CXXConversionDecl *Conv,
                                               Expr *Src) {
  // Make sure that the lambda call operator is marked used.
  CXXRecordDecl *Lambda = Conv->getParent();
  CXXMethodDecl *CallOperator
    = cast<CXXMethodDecl>(
        Lambda->lookup(
          Context.DeclarationNames.getCXXOperatorName(OO_Call)).front());
  CallOperator->setReferenced();
  CallOperator->markUsed(Context);

  ExprResult Init = PerformCopyInitialization(
      InitializedEntity::InitializeLambdaToBlock(ConvLocation, Src->getType(),
                                                 /*NRVO=*/false),
      CurrentLocation, Src);
  if (!Init.isInvalid())
    Init = ActOnFinishFullExpr(Init.get());

  if (Init.isInvalid())
    return ExprError();

  // Create the new block to be returned.
  BlockDecl *Block = BlockDecl::Create(Context, CurContext, ConvLocation);

  // Set the type information.
  Block->setSignatureAsWritten(CallOperator->getTypeSourceInfo());
  Block->setIsVariadic(CallOperator->isVariadic());
  Block->setBlockMissingReturnType(false);

  // Add parameters.
  SmallVector<ParmVarDecl *, 4> BlockParams;
  for (unsigned I = 0, N = CallOperator->getNumParams(); I != N; ++I) {
    ParmVarDecl *From = CallOperator->getParamDecl(I);
    BlockParams.push_back(ParmVarDecl::Create(
        Context, Block, From->getBeginLoc(), From->getLocation(),
        From->getIdentifier(), From->getType(), From->getTypeSourceInfo(),
        From->getStorageClass(),
        /*DefaultArg=*/nullptr));
  }
  Block->setParams(BlockParams);

  Block->setIsConversionFromLambda(true);

  // Add capture. The capture uses a fake variable, which doesn't correspond
  // to any actual memory location. However, the initializer copy-initializes
  // the lambda object.
  TypeSourceInfo *CapVarTSI =
      Context.getTrivialTypeSourceInfo(Src->getType());
  VarDecl *CapVar = VarDecl::Create(Context, Block, ConvLocation,
                                    ConvLocation, nullptr,
                                    Src->getType(), CapVarTSI,
                                    SC_None);
  BlockDecl::Capture Capture(/*Variable=*/CapVar, /*ByRef=*/false,
                             /*Nested=*/false, /*Copy=*/Init.get());
  Block->setCaptures(Context, Capture, /*CapturesCXXThis=*/false);

  // Add a fake function body to the block. IR generation is responsible
  // for filling in the actual body, which cannot be expressed as an AST.
  Block->setBody(new (Context) CompoundStmt(ConvLocation));

  // Create the block literal expression.
  Expr *BuildBlock = new (Context) BlockExpr(Block, Conv->getConversionType());
  ExprCleanupObjects.push_back(Block);
  Cleanup.setExprNeedsCleanups(true);

  return BuildBlock;
}
