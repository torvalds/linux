//===------ SemaDeclCXX.cpp - Semantic Analysis for C++ Declarations ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis for C++ declarations.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/ASTMutationListener.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/CharUnits.h"
#include "clang/AST/ComparisonCategories.h"
#include "clang/AST/EvaluatedExprVisitor.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/AST/TypeLoc.h"
#include "clang/AST/TypeOrdering.h"
#include "clang/Basic/PartialDiagnostic.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Lex/LiteralSupport.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/CXXFieldCollector.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/Initialization.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/ParsedTemplate.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/SemaInternal.h"
#include "clang/Sema/Template.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include <map>
#include <set>

using namespace clang;

//===----------------------------------------------------------------------===//
// CheckDefaultArgumentVisitor
//===----------------------------------------------------------------------===//

namespace {
  /// CheckDefaultArgumentVisitor - C++ [dcl.fct.default] Traverses
  /// the default argument of a parameter to determine whether it
  /// contains any ill-formed subexpressions. For example, this will
  /// diagnose the use of local variables or parameters within the
  /// default argument expression.
  class CheckDefaultArgumentVisitor
    : public StmtVisitor<CheckDefaultArgumentVisitor, bool> {
    Expr *DefaultArg;
    Sema *S;

  public:
    CheckDefaultArgumentVisitor(Expr *defarg, Sema *s)
      : DefaultArg(defarg), S(s) {}

    bool VisitExpr(Expr *Node);
    bool VisitDeclRefExpr(DeclRefExpr *DRE);
    bool VisitCXXThisExpr(CXXThisExpr *ThisE);
    bool VisitLambdaExpr(LambdaExpr *Lambda);
    bool VisitPseudoObjectExpr(PseudoObjectExpr *POE);
  };

  /// VisitExpr - Visit all of the children of this expression.
  bool CheckDefaultArgumentVisitor::VisitExpr(Expr *Node) {
    bool IsInvalid = false;
    for (Stmt *SubStmt : Node->children())
      IsInvalid |= Visit(SubStmt);
    return IsInvalid;
  }

  /// VisitDeclRefExpr - Visit a reference to a declaration, to
  /// determine whether this declaration can be used in the default
  /// argument expression.
  bool CheckDefaultArgumentVisitor::VisitDeclRefExpr(DeclRefExpr *DRE) {
    NamedDecl *Decl = DRE->getDecl();
    if (ParmVarDecl *Param = dyn_cast<ParmVarDecl>(Decl)) {
      // C++ [dcl.fct.default]p9
      //   Default arguments are evaluated each time the function is
      //   called. The order of evaluation of function arguments is
      //   unspecified. Consequently, parameters of a function shall not
      //   be used in default argument expressions, even if they are not
      //   evaluated. Parameters of a function declared before a default
      //   argument expression are in scope and can hide namespace and
      //   class member names.
      return S->Diag(DRE->getBeginLoc(),
                     diag::err_param_default_argument_references_param)
             << Param->getDeclName() << DefaultArg->getSourceRange();
    } else if (VarDecl *VDecl = dyn_cast<VarDecl>(Decl)) {
      // C++ [dcl.fct.default]p7
      //   Local variables shall not be used in default argument
      //   expressions.
      if (VDecl->isLocalVarDecl())
        return S->Diag(DRE->getBeginLoc(),
                       diag::err_param_default_argument_references_local)
               << VDecl->getDeclName() << DefaultArg->getSourceRange();
    }

    return false;
  }

  /// VisitCXXThisExpr - Visit a C++ "this" expression.
  bool CheckDefaultArgumentVisitor::VisitCXXThisExpr(CXXThisExpr *ThisE) {
    // C++ [dcl.fct.default]p8:
    //   The keyword this shall not be used in a default argument of a
    //   member function.
    return S->Diag(ThisE->getBeginLoc(),
                   diag::err_param_default_argument_references_this)
           << ThisE->getSourceRange();
  }

  bool CheckDefaultArgumentVisitor::VisitPseudoObjectExpr(PseudoObjectExpr *POE) {
    bool Invalid = false;
    for (PseudoObjectExpr::semantics_iterator
           i = POE->semantics_begin(), e = POE->semantics_end(); i != e; ++i) {
      Expr *E = *i;

      // Look through bindings.
      if (OpaqueValueExpr *OVE = dyn_cast<OpaqueValueExpr>(E)) {
        E = OVE->getSourceExpr();
        assert(E && "pseudo-object binding without source expression?");
      }

      Invalid |= Visit(E);
    }
    return Invalid;
  }

  bool CheckDefaultArgumentVisitor::VisitLambdaExpr(LambdaExpr *Lambda) {
    // C++11 [expr.lambda.prim]p13:
    //   A lambda-expression appearing in a default argument shall not
    //   implicitly or explicitly capture any entity.
    if (Lambda->capture_begin() == Lambda->capture_end())
      return false;

    return S->Diag(Lambda->getBeginLoc(), diag::err_lambda_capture_default_arg);
  }
}

void
Sema::ImplicitExceptionSpecification::CalledDecl(SourceLocation CallLoc,
                                                 const CXXMethodDecl *Method) {
  // If we have an MSAny spec already, don't bother.
  if (!Method || ComputedEST == EST_MSAny)
    return;

  const FunctionProtoType *Proto
    = Method->getType()->getAs<FunctionProtoType>();
  Proto = Self->ResolveExceptionSpec(CallLoc, Proto);
  if (!Proto)
    return;

  ExceptionSpecificationType EST = Proto->getExceptionSpecType();

  // If we have a throw-all spec at this point, ignore the function.
  if (ComputedEST == EST_None)
    return;

  if (EST == EST_None && Method->hasAttr<NoThrowAttr>())
    EST = EST_BasicNoexcept;

  switch (EST) {
  case EST_Unparsed:
  case EST_Uninstantiated:
  case EST_Unevaluated:
    llvm_unreachable("should not see unresolved exception specs here");

  // If this function can throw any exceptions, make a note of that.
  case EST_MSAny:
  case EST_None:
    // FIXME: Whichever we see last of MSAny and None determines our result.
    // We should make a consistent, order-independent choice here.
    ClearExceptions();
    ComputedEST = EST;
    return;
  case EST_NoexceptFalse:
    ClearExceptions();
    ComputedEST = EST_None;
    return;
  // FIXME: If the call to this decl is using any of its default arguments, we
  // need to search them for potentially-throwing calls.
  // If this function has a basic noexcept, it doesn't affect the outcome.
  case EST_BasicNoexcept:
  case EST_NoexceptTrue:
    return;
  // If we're still at noexcept(true) and there's a throw() callee,
  // change to that specification.
  case EST_DynamicNone:
    if (ComputedEST == EST_BasicNoexcept)
      ComputedEST = EST_DynamicNone;
    return;
  case EST_DependentNoexcept:
    llvm_unreachable(
        "should not generate implicit declarations for dependent cases");
  case EST_Dynamic:
    break;
  }
  assert(EST == EST_Dynamic && "EST case not considered earlier.");
  assert(ComputedEST != EST_None &&
         "Shouldn't collect exceptions when throw-all is guaranteed.");
  ComputedEST = EST_Dynamic;
  // Record the exceptions in this function's exception specification.
  for (const auto &E : Proto->exceptions())
    if (ExceptionsSeen.insert(Self->Context.getCanonicalType(E)).second)
      Exceptions.push_back(E);
}

void Sema::ImplicitExceptionSpecification::CalledExpr(Expr *E) {
  if (!E || ComputedEST == EST_MSAny)
    return;

  // FIXME:
  //
  // C++0x [except.spec]p14:
  //   [An] implicit exception-specification specifies the type-id T if and
  // only if T is allowed by the exception-specification of a function directly
  // invoked by f's implicit definition; f shall allow all exceptions if any
  // function it directly invokes allows all exceptions, and f shall allow no
  // exceptions if every function it directly invokes allows no exceptions.
  //
  // Note in particular that if an implicit exception-specification is generated
  // for a function containing a throw-expression, that specification can still
  // be noexcept(true).
  //
  // Note also that 'directly invoked' is not defined in the standard, and there
  // is no indication that we should only consider potentially-evaluated calls.
  //
  // Ultimately we should implement the intent of the standard: the exception
  // specification should be the set of exceptions which can be thrown by the
  // implicit definition. For now, we assume that any non-nothrow expression can
  // throw any exception.

  if (Self->canThrow(E))
    ComputedEST = EST_None;
}

bool
Sema::SetParamDefaultArgument(ParmVarDecl *Param, Expr *Arg,
                              SourceLocation EqualLoc) {
  if (RequireCompleteType(Param->getLocation(), Param->getType(),
                          diag::err_typecheck_decl_incomplete_type)) {
    Param->setInvalidDecl();
    return true;
  }

  // C++ [dcl.fct.default]p5
  //   A default argument expression is implicitly converted (clause
  //   4) to the parameter type. The default argument expression has
  //   the same semantic constraints as the initializer expression in
  //   a declaration of a variable of the parameter type, using the
  //   copy-initialization semantics (8.5).
  InitializedEntity Entity = InitializedEntity::InitializeParameter(Context,
                                                                    Param);
  InitializationKind Kind = InitializationKind::CreateCopy(Param->getLocation(),
                                                           EqualLoc);
  InitializationSequence InitSeq(*this, Entity, Kind, Arg);
  ExprResult Result = InitSeq.Perform(*this, Entity, Kind, Arg);
  if (Result.isInvalid())
    return true;
  Arg = Result.getAs<Expr>();

  CheckCompletedExpr(Arg, EqualLoc);
  Arg = MaybeCreateExprWithCleanups(Arg);

  // Okay: add the default argument to the parameter
  Param->setDefaultArg(Arg);

  // We have already instantiated this parameter; provide each of the
  // instantiations with the uninstantiated default argument.
  UnparsedDefaultArgInstantiationsMap::iterator InstPos
    = UnparsedDefaultArgInstantiations.find(Param);
  if (InstPos != UnparsedDefaultArgInstantiations.end()) {
    for (unsigned I = 0, N = InstPos->second.size(); I != N; ++I)
      InstPos->second[I]->setUninstantiatedDefaultArg(Arg);

    // We're done tracking this parameter's instantiations.
    UnparsedDefaultArgInstantiations.erase(InstPos);
  }

  return false;
}

/// ActOnParamDefaultArgument - Check whether the default argument
/// provided for a function parameter is well-formed. If so, attach it
/// to the parameter declaration.
void
Sema::ActOnParamDefaultArgument(Decl *param, SourceLocation EqualLoc,
                                Expr *DefaultArg) {
  if (!param || !DefaultArg)
    return;

  ParmVarDecl *Param = cast<ParmVarDecl>(param);
  UnparsedDefaultArgLocs.erase(Param);

  // Default arguments are only permitted in C++
  if (!getLangOpts().CPlusPlus) {
    Diag(EqualLoc, diag::err_param_default_argument)
      << DefaultArg->getSourceRange();
    Param->setInvalidDecl();
    return;
  }

  // Check for unexpanded parameter packs.
  if (DiagnoseUnexpandedParameterPack(DefaultArg, UPPC_DefaultArgument)) {
    Param->setInvalidDecl();
    return;
  }

  // C++11 [dcl.fct.default]p3
  //   A default argument expression [...] shall not be specified for a
  //   parameter pack.
  if (Param->isParameterPack()) {
    Diag(EqualLoc, diag::err_param_default_argument_on_parameter_pack)
        << DefaultArg->getSourceRange();
    return;
  }

  // Check that the default argument is well-formed
  CheckDefaultArgumentVisitor DefaultArgChecker(DefaultArg, this);
  if (DefaultArgChecker.Visit(DefaultArg)) {
    Param->setInvalidDecl();
    return;
  }

  SetParamDefaultArgument(Param, DefaultArg, EqualLoc);
}

/// ActOnParamUnparsedDefaultArgument - We've seen a default
/// argument for a function parameter, but we can't parse it yet
/// because we're inside a class definition. Note that this default
/// argument will be parsed later.
void Sema::ActOnParamUnparsedDefaultArgument(Decl *param,
                                             SourceLocation EqualLoc,
                                             SourceLocation ArgLoc) {
  if (!param)
    return;

  ParmVarDecl *Param = cast<ParmVarDecl>(param);
  Param->setUnparsedDefaultArg();
  UnparsedDefaultArgLocs[Param] = ArgLoc;
}

/// ActOnParamDefaultArgumentError - Parsing or semantic analysis of
/// the default argument for the parameter param failed.
void Sema::ActOnParamDefaultArgumentError(Decl *param,
                                          SourceLocation EqualLoc) {
  if (!param)
    return;

  ParmVarDecl *Param = cast<ParmVarDecl>(param);
  Param->setInvalidDecl();
  UnparsedDefaultArgLocs.erase(Param);
  Param->setDefaultArg(new(Context)
                       OpaqueValueExpr(EqualLoc,
                                       Param->getType().getNonReferenceType(),
                                       VK_RValue));
}

/// CheckExtraCXXDefaultArguments - Check for any extra default
/// arguments in the declarator, which is not a function declaration
/// or definition and therefore is not permitted to have default
/// arguments. This routine should be invoked for every declarator
/// that is not a function declaration or definition.
void Sema::CheckExtraCXXDefaultArguments(Declarator &D) {
  // C++ [dcl.fct.default]p3
  //   A default argument expression shall be specified only in the
  //   parameter-declaration-clause of a function declaration or in a
  //   template-parameter (14.1). It shall not be specified for a
  //   parameter pack. If it is specified in a
  //   parameter-declaration-clause, it shall not occur within a
  //   declarator or abstract-declarator of a parameter-declaration.
  bool MightBeFunction = D.isFunctionDeclarationContext();
  for (unsigned i = 0, e = D.getNumTypeObjects(); i != e; ++i) {
    DeclaratorChunk &chunk = D.getTypeObject(i);
    if (chunk.Kind == DeclaratorChunk::Function) {
      if (MightBeFunction) {
        // This is a function declaration. It can have default arguments, but
        // keep looking in case its return type is a function type with default
        // arguments.
        MightBeFunction = false;
        continue;
      }
      for (unsigned argIdx = 0, e = chunk.Fun.NumParams; argIdx != e;
           ++argIdx) {
        ParmVarDecl *Param = cast<ParmVarDecl>(chunk.Fun.Params[argIdx].Param);
        if (Param->hasUnparsedDefaultArg()) {
          std::unique_ptr<CachedTokens> Toks =
              std::move(chunk.Fun.Params[argIdx].DefaultArgTokens);
          SourceRange SR;
          if (Toks->size() > 1)
            SR = SourceRange((*Toks)[1].getLocation(),
                             Toks->back().getLocation());
          else
            SR = UnparsedDefaultArgLocs[Param];
          Diag(Param->getLocation(), diag::err_param_default_argument_nonfunc)
            << SR;
        } else if (Param->getDefaultArg()) {
          Diag(Param->getLocation(), diag::err_param_default_argument_nonfunc)
            << Param->getDefaultArg()->getSourceRange();
          Param->setDefaultArg(nullptr);
        }
      }
    } else if (chunk.Kind != DeclaratorChunk::Paren) {
      MightBeFunction = false;
    }
  }
}

static bool functionDeclHasDefaultArgument(const FunctionDecl *FD) {
  for (unsigned NumParams = FD->getNumParams(); NumParams > 0; --NumParams) {
    const ParmVarDecl *PVD = FD->getParamDecl(NumParams-1);
    if (!PVD->hasDefaultArg())
      return false;
    if (!PVD->hasInheritedDefaultArg())
      return true;
  }
  return false;
}

/// MergeCXXFunctionDecl - Merge two declarations of the same C++
/// function, once we already know that they have the same
/// type. Subroutine of MergeFunctionDecl. Returns true if there was an
/// error, false otherwise.
bool Sema::MergeCXXFunctionDecl(FunctionDecl *New, FunctionDecl *Old,
                                Scope *S) {
  bool Invalid = false;

  // The declaration context corresponding to the scope is the semantic
  // parent, unless this is a local function declaration, in which case
  // it is that surrounding function.
  DeclContext *ScopeDC = New->isLocalExternDecl()
                             ? New->getLexicalDeclContext()
                             : New->getDeclContext();

  // Find the previous declaration for the purpose of default arguments.
  FunctionDecl *PrevForDefaultArgs = Old;
  for (/**/; PrevForDefaultArgs;
       // Don't bother looking back past the latest decl if this is a local
       // extern declaration; nothing else could work.
       PrevForDefaultArgs = New->isLocalExternDecl()
                                ? nullptr
                                : PrevForDefaultArgs->getPreviousDecl()) {
    // Ignore hidden declarations.
    if (!LookupResult::isVisible(*this, PrevForDefaultArgs))
      continue;

    if (S && !isDeclInScope(PrevForDefaultArgs, ScopeDC, S) &&
        !New->isCXXClassMember()) {
      // Ignore default arguments of old decl if they are not in
      // the same scope and this is not an out-of-line definition of
      // a member function.
      continue;
    }

    if (PrevForDefaultArgs->isLocalExternDecl() != New->isLocalExternDecl()) {
      // If only one of these is a local function declaration, then they are
      // declared in different scopes, even though isDeclInScope may think
      // they're in the same scope. (If both are local, the scope check is
      // sufficient, and if neither is local, then they are in the same scope.)
      continue;
    }

    // We found the right previous declaration.
    break;
  }

  // C++ [dcl.fct.default]p4:
  //   For non-template functions, default arguments can be added in
  //   later declarations of a function in the same
  //   scope. Declarations in different scopes have completely
  //   distinct sets of default arguments. That is, declarations in
  //   inner scopes do not acquire default arguments from
  //   declarations in outer scopes, and vice versa. In a given
  //   function declaration, all parameters subsequent to a
  //   parameter with a default argument shall have default
  //   arguments supplied in this or previous declarations. A
  //   default argument shall not be redefined by a later
  //   declaration (not even to the same value).
  //
  // C++ [dcl.fct.default]p6:
  //   Except for member functions of class templates, the default arguments
  //   in a member function definition that appears outside of the class
  //   definition are added to the set of default arguments provided by the
  //   member function declaration in the class definition.
  for (unsigned p = 0, NumParams = PrevForDefaultArgs
                                       ? PrevForDefaultArgs->getNumParams()
                                       : 0;
       p < NumParams; ++p) {
    ParmVarDecl *OldParam = PrevForDefaultArgs->getParamDecl(p);
    ParmVarDecl *NewParam = New->getParamDecl(p);

    bool OldParamHasDfl = OldParam ? OldParam->hasDefaultArg() : false;
    bool NewParamHasDfl = NewParam->hasDefaultArg();

    if (OldParamHasDfl && NewParamHasDfl) {
      unsigned DiagDefaultParamID =
        diag::err_param_default_argument_redefinition;

      // MSVC accepts that default parameters be redefined for member functions
      // of template class. The new default parameter's value is ignored.
      Invalid = true;
      if (getLangOpts().MicrosoftExt) {
        CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(New);
        if (MD && MD->getParent()->getDescribedClassTemplate()) {
          // Merge the old default argument into the new parameter.
          NewParam->setHasInheritedDefaultArg();
          if (OldParam->hasUninstantiatedDefaultArg())
            NewParam->setUninstantiatedDefaultArg(
                                      OldParam->getUninstantiatedDefaultArg());
          else
            NewParam->setDefaultArg(OldParam->getInit());
          DiagDefaultParamID = diag::ext_param_default_argument_redefinition;
          Invalid = false;
        }
      }

      // FIXME: If we knew where the '=' was, we could easily provide a fix-it
      // hint here. Alternatively, we could walk the type-source information
      // for NewParam to find the last source location in the type... but it
      // isn't worth the effort right now. This is the kind of test case that
      // is hard to get right:
      //   int f(int);
      //   void g(int (*fp)(int) = f);
      //   void g(int (*fp)(int) = &f);
      Diag(NewParam->getLocation(), DiagDefaultParamID)
        << NewParam->getDefaultArgRange();

      // Look for the function declaration where the default argument was
      // actually written, which may be a declaration prior to Old.
      for (auto Older = PrevForDefaultArgs;
           OldParam->hasInheritedDefaultArg(); /**/) {
        Older = Older->getPreviousDecl();
        OldParam = Older->getParamDecl(p);
      }

      Diag(OldParam->getLocation(), diag::note_previous_definition)
        << OldParam->getDefaultArgRange();
    } else if (OldParamHasDfl) {
      // Merge the old default argument into the new parameter unless the new
      // function is a friend declaration in a template class. In the latter
      // case the default arguments will be inherited when the friend
      // declaration will be instantiated.
      if (New->getFriendObjectKind() == Decl::FOK_None ||
          !New->getLexicalDeclContext()->isDependentContext()) {
        // It's important to use getInit() here;  getDefaultArg()
        // strips off any top-level ExprWithCleanups.
        NewParam->setHasInheritedDefaultArg();
        if (OldParam->hasUnparsedDefaultArg())
          NewParam->setUnparsedDefaultArg();
        else if (OldParam->hasUninstantiatedDefaultArg())
          NewParam->setUninstantiatedDefaultArg(
                                       OldParam->getUninstantiatedDefaultArg());
        else
          NewParam->setDefaultArg(OldParam->getInit());
      }
    } else if (NewParamHasDfl) {
      if (New->getDescribedFunctionTemplate()) {
        // Paragraph 4, quoted above, only applies to non-template functions.
        Diag(NewParam->getLocation(),
             diag::err_param_default_argument_template_redecl)
          << NewParam->getDefaultArgRange();
        Diag(PrevForDefaultArgs->getLocation(),
             diag::note_template_prev_declaration)
            << false;
      } else if (New->getTemplateSpecializationKind()
                   != TSK_ImplicitInstantiation &&
                 New->getTemplateSpecializationKind() != TSK_Undeclared) {
        // C++ [temp.expr.spec]p21:
        //   Default function arguments shall not be specified in a declaration
        //   or a definition for one of the following explicit specializations:
        //     - the explicit specialization of a function template;
        //     - the explicit specialization of a member function template;
        //     - the explicit specialization of a member function of a class
        //       template where the class template specialization to which the
        //       member function specialization belongs is implicitly
        //       instantiated.
        Diag(NewParam->getLocation(), diag::err_template_spec_default_arg)
          << (New->getTemplateSpecializationKind() ==TSK_ExplicitSpecialization)
          << New->getDeclName()
          << NewParam->getDefaultArgRange();
      } else if (New->getDeclContext()->isDependentContext()) {
        // C++ [dcl.fct.default]p6 (DR217):
        //   Default arguments for a member function of a class template shall
        //   be specified on the initial declaration of the member function
        //   within the class template.
        //
        // Reading the tea leaves a bit in DR217 and its reference to DR205
        // leads me to the conclusion that one cannot add default function
        // arguments for an out-of-line definition of a member function of a
        // dependent type.
        int WhichKind = 2;
        if (CXXRecordDecl *Record
              = dyn_cast<CXXRecordDecl>(New->getDeclContext())) {
          if (Record->getDescribedClassTemplate())
            WhichKind = 0;
          else if (isa<ClassTemplatePartialSpecializationDecl>(Record))
            WhichKind = 1;
          else
            WhichKind = 2;
        }

        Diag(NewParam->getLocation(),
             diag::err_param_default_argument_member_template_redecl)
          << WhichKind
          << NewParam->getDefaultArgRange();
      }
    }
  }

  // DR1344: If a default argument is added outside a class definition and that
  // default argument makes the function a special member function, the program
  // is ill-formed. This can only happen for constructors.
  if (isa<CXXConstructorDecl>(New) &&
      New->getMinRequiredArguments() < Old->getMinRequiredArguments()) {
    CXXSpecialMember NewSM = getSpecialMember(cast<CXXMethodDecl>(New)),
                     OldSM = getSpecialMember(cast<CXXMethodDecl>(Old));
    if (NewSM != OldSM) {
      ParmVarDecl *NewParam = New->getParamDecl(New->getMinRequiredArguments());
      assert(NewParam->hasDefaultArg());
      Diag(NewParam->getLocation(), diag::err_default_arg_makes_ctor_special)
        << NewParam->getDefaultArgRange() << NewSM;
      Diag(Old->getLocation(), diag::note_previous_declaration);
    }
  }

  const FunctionDecl *Def;
  // C++11 [dcl.constexpr]p1: If any declaration of a function or function
  // template has a constexpr specifier then all its declarations shall
  // contain the constexpr specifier.
  if (New->isConstexpr() != Old->isConstexpr()) {
    Diag(New->getLocation(), diag::err_constexpr_redecl_mismatch)
      << New << New->isConstexpr();
    Diag(Old->getLocation(), diag::note_previous_declaration);
    Invalid = true;
  } else if (!Old->getMostRecentDecl()->isInlined() && New->isInlined() &&
             Old->isDefined(Def) &&
             // If a friend function is inlined but does not have 'inline'
             // specifier, it is a definition. Do not report attribute conflict
             // in this case, redefinition will be diagnosed later.
             (New->isInlineSpecified() ||
              New->getFriendObjectKind() == Decl::FOK_None)) {
    // C++11 [dcl.fcn.spec]p4:
    //   If the definition of a function appears in a translation unit before its
    //   first declaration as inline, the program is ill-formed.
    Diag(New->getLocation(), diag::err_inline_decl_follows_def) << New;
    Diag(Def->getLocation(), diag::note_previous_definition);
    Invalid = true;
  }

  // FIXME: It's not clear what should happen if multiple declarations of a
  // deduction guide have different explicitness. For now at least we simply
  // reject any case where the explicitness changes.
  auto *NewGuide = dyn_cast<CXXDeductionGuideDecl>(New);
  if (NewGuide && NewGuide->isExplicitSpecified() !=
                      cast<CXXDeductionGuideDecl>(Old)->isExplicitSpecified()) {
    Diag(New->getLocation(), diag::err_deduction_guide_explicit_mismatch)
      << NewGuide->isExplicitSpecified();
    Diag(Old->getLocation(), diag::note_previous_declaration);
  }

  // C++11 [dcl.fct.default]p4: If a friend declaration specifies a default
  // argument expression, that declaration shall be a definition and shall be
  // the only declaration of the function or function template in the
  // translation unit.
  if (Old->getFriendObjectKind() == Decl::FOK_Undeclared &&
      functionDeclHasDefaultArgument(Old)) {
    Diag(New->getLocation(), diag::err_friend_decl_with_def_arg_redeclared);
    Diag(Old->getLocation(), diag::note_previous_declaration);
    Invalid = true;
  }

  return Invalid;
}

NamedDecl *
Sema::ActOnDecompositionDeclarator(Scope *S, Declarator &D,
                                   MultiTemplateParamsArg TemplateParamLists) {
  assert(D.isDecompositionDeclarator());
  const DecompositionDeclarator &Decomp = D.getDecompositionDeclarator();

  // The syntax only allows a decomposition declarator as a simple-declaration,
  // a for-range-declaration, or a condition in Clang, but we parse it in more
  // cases than that.
  if (!D.mayHaveDecompositionDeclarator()) {
    Diag(Decomp.getLSquareLoc(), diag::err_decomp_decl_context)
      << Decomp.getSourceRange();
    return nullptr;
  }

  if (!TemplateParamLists.empty()) {
    // FIXME: There's no rule against this, but there are also no rules that
    // would actually make it usable, so we reject it for now.
    Diag(TemplateParamLists.front()->getTemplateLoc(),
         diag::err_decomp_decl_template);
    return nullptr;
  }

  Diag(Decomp.getLSquareLoc(),
       !getLangOpts().CPlusPlus17
           ? diag::ext_decomp_decl
           : D.getContext() == DeclaratorContext::ConditionContext
                 ? diag::ext_decomp_decl_cond
                 : diag::warn_cxx14_compat_decomp_decl)
      << Decomp.getSourceRange();

  // The semantic context is always just the current context.
  DeclContext *const DC = CurContext;

  // C++1z [dcl.dcl]/8:
  //   The decl-specifier-seq shall contain only the type-specifier auto
  //   and cv-qualifiers.
  auto &DS = D.getDeclSpec();
  {
    SmallVector<StringRef, 8> BadSpecifiers;
    SmallVector<SourceLocation, 8> BadSpecifierLocs;
    if (auto SCS = DS.getStorageClassSpec()) {
      BadSpecifiers.push_back(DeclSpec::getSpecifierName(SCS));
      BadSpecifierLocs.push_back(DS.getStorageClassSpecLoc());
    }
    if (auto TSCS = DS.getThreadStorageClassSpec()) {
      BadSpecifiers.push_back(DeclSpec::getSpecifierName(TSCS));
      BadSpecifierLocs.push_back(DS.getThreadStorageClassSpecLoc());
    }
    if (DS.isConstexprSpecified()) {
      BadSpecifiers.push_back("constexpr");
      BadSpecifierLocs.push_back(DS.getConstexprSpecLoc());
    }
    if (DS.isInlineSpecified()) {
      BadSpecifiers.push_back("inline");
      BadSpecifierLocs.push_back(DS.getInlineSpecLoc());
    }
    if (!BadSpecifiers.empty()) {
      auto &&Err = Diag(BadSpecifierLocs.front(), diag::err_decomp_decl_spec);
      Err << (int)BadSpecifiers.size()
          << llvm::join(BadSpecifiers.begin(), BadSpecifiers.end(), " ");
      // Don't add FixItHints to remove the specifiers; we do still respect
      // them when building the underlying variable.
      for (auto Loc : BadSpecifierLocs)
        Err << SourceRange(Loc, Loc);
    }
    // We can't recover from it being declared as a typedef.
    if (DS.getStorageClassSpec() == DeclSpec::SCS_typedef)
      return nullptr;
  }

  TypeSourceInfo *TInfo = GetTypeForDeclarator(D, S);
  QualType R = TInfo->getType();

  if (DiagnoseUnexpandedParameterPack(D.getIdentifierLoc(), TInfo,
                                      UPPC_DeclarationType))
    D.setInvalidType();

  // The syntax only allows a single ref-qualifier prior to the decomposition
  // declarator. No other declarator chunks are permitted. Also check the type
  // specifier here.
  if (DS.getTypeSpecType() != DeclSpec::TST_auto ||
      D.hasGroupingParens() || D.getNumTypeObjects() > 1 ||
      (D.getNumTypeObjects() == 1 &&
       D.getTypeObject(0).Kind != DeclaratorChunk::Reference)) {
    Diag(Decomp.getLSquareLoc(),
         (D.hasGroupingParens() ||
          (D.getNumTypeObjects() &&
           D.getTypeObject(0).Kind == DeclaratorChunk::Paren))
             ? diag::err_decomp_decl_parens
             : diag::err_decomp_decl_type)
        << R;

    // In most cases, there's no actual problem with an explicitly-specified
    // type, but a function type won't work here, and ActOnVariableDeclarator
    // shouldn't be called for such a type.
    if (R->isFunctionType())
      D.setInvalidType();
  }

  // Build the BindingDecls.
  SmallVector<BindingDecl*, 8> Bindings;

  // Build the BindingDecls.
  for (auto &B : D.getDecompositionDeclarator().bindings()) {
    // Check for name conflicts.
    DeclarationNameInfo NameInfo(B.Name, B.NameLoc);
    LookupResult Previous(*this, NameInfo, LookupOrdinaryName,
                          ForVisibleRedeclaration);
    LookupName(Previous, S,
               /*CreateBuiltins*/DC->getRedeclContext()->isTranslationUnit());

    // It's not permitted to shadow a template parameter name.
    if (Previous.isSingleResult() &&
        Previous.getFoundDecl()->isTemplateParameter()) {
      DiagnoseTemplateParameterShadow(D.getIdentifierLoc(),
                                      Previous.getFoundDecl());
      Previous.clear();
    }

    bool ConsiderLinkage = DC->isFunctionOrMethod() &&
                           DS.getStorageClassSpec() == DeclSpec::SCS_extern;
    FilterLookupForScope(Previous, DC, S, ConsiderLinkage,
                         /*AllowInlineNamespace*/false);
    if (!Previous.empty()) {
      auto *Old = Previous.getRepresentativeDecl();
      Diag(B.NameLoc, diag::err_redefinition) << B.Name;
      Diag(Old->getLocation(), diag::note_previous_definition);
    }

    auto *BD = BindingDecl::Create(Context, DC, B.NameLoc, B.Name);
    PushOnScopeChains(BD, S, true);
    Bindings.push_back(BD);
    ParsingInitForAutoVars.insert(BD);
  }

  // There are no prior lookup results for the variable itself, because it
  // is unnamed.
  DeclarationNameInfo NameInfo((IdentifierInfo *)nullptr,
                               Decomp.getLSquareLoc());
  LookupResult Previous(*this, NameInfo, LookupOrdinaryName,
                        ForVisibleRedeclaration);

  // Build the variable that holds the non-decomposed object.
  bool AddToScope = true;
  NamedDecl *New =
      ActOnVariableDeclarator(S, D, DC, TInfo, Previous,
                              MultiTemplateParamsArg(), AddToScope, Bindings);
  if (AddToScope) {
    S->AddDecl(New);
    CurContext->addHiddenDecl(New);
  }

  if (isInOpenMPDeclareTargetContext())
    checkDeclIsAllowedInOpenMPTarget(nullptr, New);

  return New;
}

static bool checkSimpleDecomposition(
    Sema &S, ArrayRef<BindingDecl *> Bindings, ValueDecl *Src,
    QualType DecompType, const llvm::APSInt &NumElems, QualType ElemType,
    llvm::function_ref<ExprResult(SourceLocation, Expr *, unsigned)> GetInit) {
  if ((int64_t)Bindings.size() != NumElems) {
    S.Diag(Src->getLocation(), diag::err_decomp_decl_wrong_number_bindings)
        << DecompType << (unsigned)Bindings.size() << NumElems.toString(10)
        << (NumElems < Bindings.size());
    return true;
  }

  unsigned I = 0;
  for (auto *B : Bindings) {
    SourceLocation Loc = B->getLocation();
    ExprResult E = S.BuildDeclRefExpr(Src, DecompType, VK_LValue, Loc);
    if (E.isInvalid())
      return true;
    E = GetInit(Loc, E.get(), I++);
    if (E.isInvalid())
      return true;
    B->setBinding(ElemType, E.get());
  }

  return false;
}

static bool checkArrayLikeDecomposition(Sema &S,
                                        ArrayRef<BindingDecl *> Bindings,
                                        ValueDecl *Src, QualType DecompType,
                                        const llvm::APSInt &NumElems,
                                        QualType ElemType) {
  return checkSimpleDecomposition(
      S, Bindings, Src, DecompType, NumElems, ElemType,
      [&](SourceLocation Loc, Expr *Base, unsigned I) -> ExprResult {
        ExprResult E = S.ActOnIntegerConstant(Loc, I);
        if (E.isInvalid())
          return ExprError();
        return S.CreateBuiltinArraySubscriptExpr(Base, Loc, E.get(), Loc);
      });
}

static bool checkArrayDecomposition(Sema &S, ArrayRef<BindingDecl*> Bindings,
                                    ValueDecl *Src, QualType DecompType,
                                    const ConstantArrayType *CAT) {
  return checkArrayLikeDecomposition(S, Bindings, Src, DecompType,
                                     llvm::APSInt(CAT->getSize()),
                                     CAT->getElementType());
}

static bool checkVectorDecomposition(Sema &S, ArrayRef<BindingDecl*> Bindings,
                                     ValueDecl *Src, QualType DecompType,
                                     const VectorType *VT) {
  return checkArrayLikeDecomposition(
      S, Bindings, Src, DecompType, llvm::APSInt::get(VT->getNumElements()),
      S.Context.getQualifiedType(VT->getElementType(),
                                 DecompType.getQualifiers()));
}

static bool checkComplexDecomposition(Sema &S,
                                      ArrayRef<BindingDecl *> Bindings,
                                      ValueDecl *Src, QualType DecompType,
                                      const ComplexType *CT) {
  return checkSimpleDecomposition(
      S, Bindings, Src, DecompType, llvm::APSInt::get(2),
      S.Context.getQualifiedType(CT->getElementType(),
                                 DecompType.getQualifiers()),
      [&](SourceLocation Loc, Expr *Base, unsigned I) -> ExprResult {
        return S.CreateBuiltinUnaryOp(Loc, I ? UO_Imag : UO_Real, Base);
      });
}

static std::string printTemplateArgs(const PrintingPolicy &PrintingPolicy,
                                     TemplateArgumentListInfo &Args) {
  SmallString<128> SS;
  llvm::raw_svector_ostream OS(SS);
  bool First = true;
  for (auto &Arg : Args.arguments()) {
    if (!First)
      OS << ", ";
    Arg.getArgument().print(PrintingPolicy, OS);
    First = false;
  }
  return OS.str();
}

static bool lookupStdTypeTraitMember(Sema &S, LookupResult &TraitMemberLookup,
                                     SourceLocation Loc, StringRef Trait,
                                     TemplateArgumentListInfo &Args,
                                     unsigned DiagID) {
  auto DiagnoseMissing = [&] {
    if (DiagID)
      S.Diag(Loc, DiagID) << printTemplateArgs(S.Context.getPrintingPolicy(),
                                               Args);
    return true;
  };

  // FIXME: Factor out duplication with lookupPromiseType in SemaCoroutine.
  NamespaceDecl *Std = S.getStdNamespace();
  if (!Std)
    return DiagnoseMissing();

  // Look up the trait itself, within namespace std. We can diagnose various
  // problems with this lookup even if we've been asked to not diagnose a
  // missing specialization, because this can only fail if the user has been
  // declaring their own names in namespace std or we don't support the
  // standard library implementation in use.
  LookupResult Result(S, &S.PP.getIdentifierTable().get(Trait),
                      Loc, Sema::LookupOrdinaryName);
  if (!S.LookupQualifiedName(Result, Std))
    return DiagnoseMissing();
  if (Result.isAmbiguous())
    return true;

  ClassTemplateDecl *TraitTD = Result.getAsSingle<ClassTemplateDecl>();
  if (!TraitTD) {
    Result.suppressDiagnostics();
    NamedDecl *Found = *Result.begin();
    S.Diag(Loc, diag::err_std_type_trait_not_class_template) << Trait;
    S.Diag(Found->getLocation(), diag::note_declared_at);
    return true;
  }

  // Build the template-id.
  QualType TraitTy = S.CheckTemplateIdType(TemplateName(TraitTD), Loc, Args);
  if (TraitTy.isNull())
    return true;
  if (!S.isCompleteType(Loc, TraitTy)) {
    if (DiagID)
      S.RequireCompleteType(
          Loc, TraitTy, DiagID,
          printTemplateArgs(S.Context.getPrintingPolicy(), Args));
    return true;
  }

  CXXRecordDecl *RD = TraitTy->getAsCXXRecordDecl();
  assert(RD && "specialization of class template is not a class?");

  // Look up the member of the trait type.
  S.LookupQualifiedName(TraitMemberLookup, RD);
  return TraitMemberLookup.isAmbiguous();
}

static TemplateArgumentLoc
getTrivialIntegralTemplateArgument(Sema &S, SourceLocation Loc, QualType T,
                                   uint64_t I) {
  TemplateArgument Arg(S.Context, S.Context.MakeIntValue(I, T), T);
  return S.getTrivialTemplateArgumentLoc(Arg, T, Loc);
}

static TemplateArgumentLoc
getTrivialTypeTemplateArgument(Sema &S, SourceLocation Loc, QualType T) {
  return S.getTrivialTemplateArgumentLoc(TemplateArgument(T), QualType(), Loc);
}

namespace { enum class IsTupleLike { TupleLike, NotTupleLike, Error }; }

static IsTupleLike isTupleLike(Sema &S, SourceLocation Loc, QualType T,
                               llvm::APSInt &Size) {
  EnterExpressionEvaluationContext ContextRAII(
      S, Sema::ExpressionEvaluationContext::ConstantEvaluated);

  DeclarationName Value = S.PP.getIdentifierInfo("value");
  LookupResult R(S, Value, Loc, Sema::LookupOrdinaryName);

  // Form template argument list for tuple_size<T>.
  TemplateArgumentListInfo Args(Loc, Loc);
  Args.addArgument(getTrivialTypeTemplateArgument(S, Loc, T));

  // If there's no tuple_size specialization, it's not tuple-like.
  if (lookupStdTypeTraitMember(S, R, Loc, "tuple_size", Args, /*DiagID*/0))
    return IsTupleLike::NotTupleLike;

  // If we get this far, we've committed to the tuple interpretation, but
  // we can still fail if there actually isn't a usable ::value.

  struct ICEDiagnoser : Sema::VerifyICEDiagnoser {
    LookupResult &R;
    TemplateArgumentListInfo &Args;
    ICEDiagnoser(LookupResult &R, TemplateArgumentListInfo &Args)
        : R(R), Args(Args) {}
    void diagnoseNotICE(Sema &S, SourceLocation Loc, SourceRange SR) {
      S.Diag(Loc, diag::err_decomp_decl_std_tuple_size_not_constant)
          << printTemplateArgs(S.Context.getPrintingPolicy(), Args);
    }
  } Diagnoser(R, Args);

  if (R.empty()) {
    Diagnoser.diagnoseNotICE(S, Loc, SourceRange());
    return IsTupleLike::Error;
  }

  ExprResult E =
      S.BuildDeclarationNameExpr(CXXScopeSpec(), R, /*NeedsADL*/false);
  if (E.isInvalid())
    return IsTupleLike::Error;

  E = S.VerifyIntegerConstantExpression(E.get(), &Size, Diagnoser, false);
  if (E.isInvalid())
    return IsTupleLike::Error;

  return IsTupleLike::TupleLike;
}

/// \return std::tuple_element<I, T>::type.
static QualType getTupleLikeElementType(Sema &S, SourceLocation Loc,
                                        unsigned I, QualType T) {
  // Form template argument list for tuple_element<I, T>.
  TemplateArgumentListInfo Args(Loc, Loc);
  Args.addArgument(
      getTrivialIntegralTemplateArgument(S, Loc, S.Context.getSizeType(), I));
  Args.addArgument(getTrivialTypeTemplateArgument(S, Loc, T));

  DeclarationName TypeDN = S.PP.getIdentifierInfo("type");
  LookupResult R(S, TypeDN, Loc, Sema::LookupOrdinaryName);
  if (lookupStdTypeTraitMember(
          S, R, Loc, "tuple_element", Args,
          diag::err_decomp_decl_std_tuple_element_not_specialized))
    return QualType();

  auto *TD = R.getAsSingle<TypeDecl>();
  if (!TD) {
    R.suppressDiagnostics();
    S.Diag(Loc, diag::err_decomp_decl_std_tuple_element_not_specialized)
      << printTemplateArgs(S.Context.getPrintingPolicy(), Args);
    if (!R.empty())
      S.Diag(R.getRepresentativeDecl()->getLocation(), diag::note_declared_at);
    return QualType();
  }

  return S.Context.getTypeDeclType(TD);
}

namespace {
struct BindingDiagnosticTrap {
  Sema &S;
  DiagnosticErrorTrap Trap;
  BindingDecl *BD;

  BindingDiagnosticTrap(Sema &S, BindingDecl *BD)
      : S(S), Trap(S.Diags), BD(BD) {}
  ~BindingDiagnosticTrap() {
    if (Trap.hasErrorOccurred())
      S.Diag(BD->getLocation(), diag::note_in_binding_decl_init) << BD;
  }
};
}

static bool checkTupleLikeDecomposition(Sema &S,
                                        ArrayRef<BindingDecl *> Bindings,
                                        VarDecl *Src, QualType DecompType,
                                        const llvm::APSInt &TupleSize) {
  if ((int64_t)Bindings.size() != TupleSize) {
    S.Diag(Src->getLocation(), diag::err_decomp_decl_wrong_number_bindings)
        << DecompType << (unsigned)Bindings.size() << TupleSize.toString(10)
        << (TupleSize < Bindings.size());
    return true;
  }

  if (Bindings.empty())
    return false;

  DeclarationName GetDN = S.PP.getIdentifierInfo("get");

  // [dcl.decomp]p3:
  //   The unqualified-id get is looked up in the scope of E by class member
  //   access lookup ...
  LookupResult MemberGet(S, GetDN, Src->getLocation(), Sema::LookupMemberName);
  bool UseMemberGet = false;
  if (S.isCompleteType(Src->getLocation(), DecompType)) {
    if (auto *RD = DecompType->getAsCXXRecordDecl())
      S.LookupQualifiedName(MemberGet, RD);
    if (MemberGet.isAmbiguous())
      return true;
    //   ... and if that finds at least one declaration that is a function
    //   template whose first template parameter is a non-type parameter ...
    for (NamedDecl *D : MemberGet) {
      if (FunctionTemplateDecl *FTD =
              dyn_cast<FunctionTemplateDecl>(D->getUnderlyingDecl())) {
        TemplateParameterList *TPL = FTD->getTemplateParameters();
        if (TPL->size() != 0 &&
            isa<NonTypeTemplateParmDecl>(TPL->getParam(0))) {
          //   ... the initializer is e.get<i>().
          UseMemberGet = true;
          break;
        }
      }
    }
    S.FilterAcceptableTemplateNames(MemberGet);
  }

  unsigned I = 0;
  for (auto *B : Bindings) {
    BindingDiagnosticTrap Trap(S, B);
    SourceLocation Loc = B->getLocation();

    ExprResult E = S.BuildDeclRefExpr(Src, DecompType, VK_LValue, Loc);
    if (E.isInvalid())
      return true;

    //   e is an lvalue if the type of the entity is an lvalue reference and
    //   an xvalue otherwise
    if (!Src->getType()->isLValueReferenceType())
      E = ImplicitCastExpr::Create(S.Context, E.get()->getType(), CK_NoOp,
                                   E.get(), nullptr, VK_XValue);

    TemplateArgumentListInfo Args(Loc, Loc);
    Args.addArgument(
        getTrivialIntegralTemplateArgument(S, Loc, S.Context.getSizeType(), I));

    if (UseMemberGet) {
      //   if [lookup of member get] finds at least one declaration, the
      //   initializer is e.get<i-1>().
      E = S.BuildMemberReferenceExpr(E.get(), DecompType, Loc, false,
                                     CXXScopeSpec(), SourceLocation(), nullptr,
                                     MemberGet, &Args, nullptr);
      if (E.isInvalid())
        return true;

      E = S.ActOnCallExpr(nullptr, E.get(), Loc, None, Loc);
    } else {
      //   Otherwise, the initializer is get<i-1>(e), where get is looked up
      //   in the associated namespaces.
      Expr *Get = UnresolvedLookupExpr::Create(
          S.Context, nullptr, NestedNameSpecifierLoc(), SourceLocation(),
          DeclarationNameInfo(GetDN, Loc), /*RequiresADL*/true, &Args,
          UnresolvedSetIterator(), UnresolvedSetIterator());

      Expr *Arg = E.get();
      E = S.ActOnCallExpr(nullptr, Get, Loc, Arg, Loc);
    }
    if (E.isInvalid())
      return true;
    Expr *Init = E.get();

    //   Given the type T designated by std::tuple_element<i - 1, E>::type,
    QualType T = getTupleLikeElementType(S, Loc, I, DecompType);
    if (T.isNull())
      return true;

    //   each vi is a variable of type "reference to T" initialized with the
    //   initializer, where the reference is an lvalue reference if the
    //   initializer is an lvalue and an rvalue reference otherwise
    QualType RefType =
        S.BuildReferenceType(T, E.get()->isLValue(), Loc, B->getDeclName());
    if (RefType.isNull())
      return true;
    auto *RefVD = VarDecl::Create(
        S.Context, Src->getDeclContext(), Loc, Loc,
        B->getDeclName().getAsIdentifierInfo(), RefType,
        S.Context.getTrivialTypeSourceInfo(T, Loc), Src->getStorageClass());
    RefVD->setLexicalDeclContext(Src->getLexicalDeclContext());
    RefVD->setTSCSpec(Src->getTSCSpec());
    RefVD->setImplicit();
    if (Src->isInlineSpecified())
      RefVD->setInlineSpecified();
    RefVD->getLexicalDeclContext()->addHiddenDecl(RefVD);

    InitializedEntity Entity = InitializedEntity::InitializeBinding(RefVD);
    InitializationKind Kind = InitializationKind::CreateCopy(Loc, Loc);
    InitializationSequence Seq(S, Entity, Kind, Init);
    E = Seq.Perform(S, Entity, Kind, Init);
    if (E.isInvalid())
      return true;
    E = S.ActOnFinishFullExpr(E.get(), Loc);
    if (E.isInvalid())
      return true;
    RefVD->setInit(E.get());
    RefVD->checkInitIsICE();

    E = S.BuildDeclarationNameExpr(CXXScopeSpec(),
                                   DeclarationNameInfo(B->getDeclName(), Loc),
                                   RefVD);
    if (E.isInvalid())
      return true;

    B->setBinding(T, E.get());
    I++;
  }

  return false;
}

/// Find the base class to decompose in a built-in decomposition of a class type.
/// This base class search is, unfortunately, not quite like any other that we
/// perform anywhere else in C++.
static DeclAccessPair findDecomposableBaseClass(Sema &S, SourceLocation Loc,
                                                const CXXRecordDecl *RD,
                                                CXXCastPath &BasePath) {
  auto BaseHasFields = [](const CXXBaseSpecifier *Specifier,
                          CXXBasePath &Path) {
    return Specifier->getType()->getAsCXXRecordDecl()->hasDirectFields();
  };

  const CXXRecordDecl *ClassWithFields = nullptr;
  AccessSpecifier AS = AS_public;
  if (RD->hasDirectFields())
    // [dcl.decomp]p4:
    //   Otherwise, all of E's non-static data members shall be public direct
    //   members of E ...
    ClassWithFields = RD;
  else {
    //   ... or of ...
    CXXBasePaths Paths;
    Paths.setOrigin(const_cast<CXXRecordDecl*>(RD));
    if (!RD->lookupInBases(BaseHasFields, Paths)) {
      // If no classes have fields, just decompose RD itself. (This will work
      // if and only if zero bindings were provided.)
      return DeclAccessPair::make(const_cast<CXXRecordDecl*>(RD), AS_public);
    }

    CXXBasePath *BestPath = nullptr;
    for (auto &P : Paths) {
      if (!BestPath)
        BestPath = &P;
      else if (!S.Context.hasSameType(P.back().Base->getType(),
                                      BestPath->back().Base->getType())) {
        //   ... the same ...
        S.Diag(Loc, diag::err_decomp_decl_multiple_bases_with_members)
          << false << RD << BestPath->back().Base->getType()
          << P.back().Base->getType();
        return DeclAccessPair();
      } else if (P.Access < BestPath->Access) {
        BestPath = &P;
      }
    }

    //   ... unambiguous ...
    QualType BaseType = BestPath->back().Base->getType();
    if (Paths.isAmbiguous(S.Context.getCanonicalType(BaseType))) {
      S.Diag(Loc, diag::err_decomp_decl_ambiguous_base)
        << RD << BaseType << S.getAmbiguousPathsDisplayString(Paths);
      return DeclAccessPair();
    }

    //   ... [accessible, implied by other rules] base class of E.
    S.CheckBaseClassAccess(Loc, BaseType, S.Context.getRecordType(RD),
                           *BestPath, diag::err_decomp_decl_inaccessible_base);
    AS = BestPath->Access;

    ClassWithFields = BaseType->getAsCXXRecordDecl();
    S.BuildBasePathArray(Paths, BasePath);
  }

  // The above search did not check whether the selected class itself has base
  // classes with fields, so check that now.
  CXXBasePaths Paths;
  if (ClassWithFields->lookupInBases(BaseHasFields, Paths)) {
    S.Diag(Loc, diag::err_decomp_decl_multiple_bases_with_members)
      << (ClassWithFields == RD) << RD << ClassWithFields
      << Paths.front().back().Base->getType();
    return DeclAccessPair();
  }

  return DeclAccessPair::make(const_cast<CXXRecordDecl*>(ClassWithFields), AS);
}

static bool checkMemberDecomposition(Sema &S, ArrayRef<BindingDecl*> Bindings,
                                     ValueDecl *Src, QualType DecompType,
                                     const CXXRecordDecl *OrigRD) {
  if (S.RequireCompleteType(Src->getLocation(), DecompType,
                            diag::err_incomplete_type))
    return true;

  CXXCastPath BasePath;
  DeclAccessPair BasePair =
      findDecomposableBaseClass(S, Src->getLocation(), OrigRD, BasePath);
  const CXXRecordDecl *RD = cast_or_null<CXXRecordDecl>(BasePair.getDecl());
  if (!RD)
    return true;
  QualType BaseType = S.Context.getQualifiedType(S.Context.getRecordType(RD),
                                                 DecompType.getQualifiers());

  auto DiagnoseBadNumberOfBindings = [&]() -> bool {
    unsigned NumFields =
        std::count_if(RD->field_begin(), RD->field_end(),
                      [](FieldDecl *FD) { return !FD->isUnnamedBitfield(); });
    assert(Bindings.size() != NumFields);
    S.Diag(Src->getLocation(), diag::err_decomp_decl_wrong_number_bindings)
        << DecompType << (unsigned)Bindings.size() << NumFields
        << (NumFields < Bindings.size());
    return true;
  };

  //   all of E's non-static data members shall be [...] well-formed
  //   when named as e.name in the context of the structured binding,
  //   E shall not have an anonymous union member, ...
  unsigned I = 0;
  for (auto *FD : RD->fields()) {
    if (FD->isUnnamedBitfield())
      continue;

    if (FD->isAnonymousStructOrUnion()) {
      S.Diag(Src->getLocation(), diag::err_decomp_decl_anon_union_member)
        << DecompType << FD->getType()->isUnionType();
      S.Diag(FD->getLocation(), diag::note_declared_at);
      return true;
    }

    // We have a real field to bind.
    if (I >= Bindings.size())
      return DiagnoseBadNumberOfBindings();
    auto *B = Bindings[I++];
    SourceLocation Loc = B->getLocation();

    // The field must be accessible in the context of the structured binding.
    // We already checked that the base class is accessible.
    // FIXME: Add 'const' to AccessedEntity's classes so we can remove the
    // const_cast here.
    S.CheckStructuredBindingMemberAccess(
        Loc, const_cast<CXXRecordDecl *>(OrigRD),
        DeclAccessPair::make(FD, CXXRecordDecl::MergeAccess(
                                     BasePair.getAccess(), FD->getAccess())));

    // Initialize the binding to Src.FD.
    ExprResult E = S.BuildDeclRefExpr(Src, DecompType, VK_LValue, Loc);
    if (E.isInvalid())
      return true;
    E = S.ImpCastExprToType(E.get(), BaseType, CK_UncheckedDerivedToBase,
                            VK_LValue, &BasePath);
    if (E.isInvalid())
      return true;
    E = S.BuildFieldReferenceExpr(E.get(), /*IsArrow*/ false, Loc,
                                  CXXScopeSpec(), FD,
                                  DeclAccessPair::make(FD, FD->getAccess()),
                                  DeclarationNameInfo(FD->getDeclName(), Loc));
    if (E.isInvalid())
      return true;

    // If the type of the member is T, the referenced type is cv T, where cv is
    // the cv-qualification of the decomposition expression.
    //
    // FIXME: We resolve a defect here: if the field is mutable, we do not add
    // 'const' to the type of the field.
    Qualifiers Q = DecompType.getQualifiers();
    if (FD->isMutable())
      Q.removeConst();
    B->setBinding(S.BuildQualifiedType(FD->getType(), Loc, Q), E.get());
  }

  if (I != Bindings.size())
    return DiagnoseBadNumberOfBindings();

  return false;
}

void Sema::CheckCompleteDecompositionDeclaration(DecompositionDecl *DD) {
  QualType DecompType = DD->getType();

  // If the type of the decomposition is dependent, then so is the type of
  // each binding.
  if (DecompType->isDependentType()) {
    for (auto *B : DD->bindings())
      B->setType(Context.DependentTy);
    return;
  }

  DecompType = DecompType.getNonReferenceType();
  ArrayRef<BindingDecl*> Bindings = DD->bindings();

  // C++1z [dcl.decomp]/2:
  //   If E is an array type [...]
  // As an extension, we also support decomposition of built-in complex and
  // vector types.
  if (auto *CAT = Context.getAsConstantArrayType(DecompType)) {
    if (checkArrayDecomposition(*this, Bindings, DD, DecompType, CAT))
      DD->setInvalidDecl();
    return;
  }
  if (auto *VT = DecompType->getAs<VectorType>()) {
    if (checkVectorDecomposition(*this, Bindings, DD, DecompType, VT))
      DD->setInvalidDecl();
    return;
  }
  if (auto *CT = DecompType->getAs<ComplexType>()) {
    if (checkComplexDecomposition(*this, Bindings, DD, DecompType, CT))
      DD->setInvalidDecl();
    return;
  }

  // C++1z [dcl.decomp]/3:
  //   if the expression std::tuple_size<E>::value is a well-formed integral
  //   constant expression, [...]
  llvm::APSInt TupleSize(32);
  switch (isTupleLike(*this, DD->getLocation(), DecompType, TupleSize)) {
  case IsTupleLike::Error:
    DD->setInvalidDecl();
    return;

  case IsTupleLike::TupleLike:
    if (checkTupleLikeDecomposition(*this, Bindings, DD, DecompType, TupleSize))
      DD->setInvalidDecl();
    return;

  case IsTupleLike::NotTupleLike:
    break;
  }

  // C++1z [dcl.dcl]/8:
  //   [E shall be of array or non-union class type]
  CXXRecordDecl *RD = DecompType->getAsCXXRecordDecl();
  if (!RD || RD->isUnion()) {
    Diag(DD->getLocation(), diag::err_decomp_decl_unbindable_type)
        << DD << !RD << DecompType;
    DD->setInvalidDecl();
    return;
  }

  // C++1z [dcl.decomp]/4:
  //   all of E's non-static data members shall be [...] direct members of
  //   E or of the same unambiguous public base class of E, ...
  if (checkMemberDecomposition(*this, Bindings, DD, DecompType, RD))
    DD->setInvalidDecl();
}

/// Merge the exception specifications of two variable declarations.
///
/// This is called when there's a redeclaration of a VarDecl. The function
/// checks if the redeclaration might have an exception specification and
/// validates compatibility and merges the specs if necessary.
void Sema::MergeVarDeclExceptionSpecs(VarDecl *New, VarDecl *Old) {
  // Shortcut if exceptions are disabled.
  if (!getLangOpts().CXXExceptions)
    return;

  assert(Context.hasSameType(New->getType(), Old->getType()) &&
         "Should only be called if types are otherwise the same.");

  QualType NewType = New->getType();
  QualType OldType = Old->getType();

  // We're only interested in pointers and references to functions, as well
  // as pointers to member functions.
  if (const ReferenceType *R = NewType->getAs<ReferenceType>()) {
    NewType = R->getPointeeType();
    OldType = OldType->getAs<ReferenceType>()->getPointeeType();
  } else if (const PointerType *P = NewType->getAs<PointerType>()) {
    NewType = P->getPointeeType();
    OldType = OldType->getAs<PointerType>()->getPointeeType();
  } else if (const MemberPointerType *M = NewType->getAs<MemberPointerType>()) {
    NewType = M->getPointeeType();
    OldType = OldType->getAs<MemberPointerType>()->getPointeeType();
  }

  if (!NewType->isFunctionProtoType())
    return;

  // There's lots of special cases for functions. For function pointers, system
  // libraries are hopefully not as broken so that we don't need these
  // workarounds.
  if (CheckEquivalentExceptionSpec(
        OldType->getAs<FunctionProtoType>(), Old->getLocation(),
        NewType->getAs<FunctionProtoType>(), New->getLocation())) {
    New->setInvalidDecl();
  }
}

/// CheckCXXDefaultArguments - Verify that the default arguments for a
/// function declaration are well-formed according to C++
/// [dcl.fct.default].
void Sema::CheckCXXDefaultArguments(FunctionDecl *FD) {
  unsigned NumParams = FD->getNumParams();
  unsigned p;

  // Find first parameter with a default argument
  for (p = 0; p < NumParams; ++p) {
    ParmVarDecl *Param = FD->getParamDecl(p);
    if (Param->hasDefaultArg())
      break;
  }

  // C++11 [dcl.fct.default]p4:
  //   In a given function declaration, each parameter subsequent to a parameter
  //   with a default argument shall have a default argument supplied in this or
  //   a previous declaration or shall be a function parameter pack. A default
  //   argument shall not be redefined by a later declaration (not even to the
  //   same value).
  unsigned LastMissingDefaultArg = 0;
  for (; p < NumParams; ++p) {
    ParmVarDecl *Param = FD->getParamDecl(p);
    if (!Param->hasDefaultArg() && !Param->isParameterPack()) {
      if (Param->isInvalidDecl())
        /* We already complained about this parameter. */;
      else if (Param->getIdentifier())
        Diag(Param->getLocation(),
             diag::err_param_default_argument_missing_name)
          << Param->getIdentifier();
      else
        Diag(Param->getLocation(),
             diag::err_param_default_argument_missing);

      LastMissingDefaultArg = p;
    }
  }

  if (LastMissingDefaultArg > 0) {
    // Some default arguments were missing. Clear out all of the
    // default arguments up to (and including) the last missing
    // default argument, so that we leave the function parameters
    // in a semantically valid state.
    for (p = 0; p <= LastMissingDefaultArg; ++p) {
      ParmVarDecl *Param = FD->getParamDecl(p);
      if (Param->hasDefaultArg()) {
        Param->setDefaultArg(nullptr);
      }
    }
  }
}

// CheckConstexprParameterTypes - Check whether a function's parameter types
// are all literal types. If so, return true. If not, produce a suitable
// diagnostic and return false.
static bool CheckConstexprParameterTypes(Sema &SemaRef,
                                         const FunctionDecl *FD) {
  unsigned ArgIndex = 0;
  const FunctionProtoType *FT = FD->getType()->getAs<FunctionProtoType>();
  for (FunctionProtoType::param_type_iterator i = FT->param_type_begin(),
                                              e = FT->param_type_end();
       i != e; ++i, ++ArgIndex) {
    const ParmVarDecl *PD = FD->getParamDecl(ArgIndex);
    SourceLocation ParamLoc = PD->getLocation();
    if (!(*i)->isDependentType() &&
        SemaRef.RequireLiteralType(ParamLoc, *i,
                                   diag::err_constexpr_non_literal_param,
                                   ArgIndex+1, PD->getSourceRange(),
                                   isa<CXXConstructorDecl>(FD)))
      return false;
  }
  return true;
}

/// Get diagnostic %select index for tag kind for
/// record diagnostic message.
/// WARNING: Indexes apply to particular diagnostics only!
///
/// \returns diagnostic %select index.
static unsigned getRecordDiagFromTagKind(TagTypeKind Tag) {
  switch (Tag) {
  case TTK_Struct: return 0;
  case TTK_Interface: return 1;
  case TTK_Class:  return 2;
  default: llvm_unreachable("Invalid tag kind for record diagnostic!");
  }
}

// CheckConstexprFunctionDecl - Check whether a function declaration satisfies
// the requirements of a constexpr function definition or a constexpr
// constructor definition. If so, return true. If not, produce appropriate
// diagnostics and return false.
//
// This implements C++11 [dcl.constexpr]p3,4, as amended by DR1360.
bool Sema::CheckConstexprFunctionDecl(const FunctionDecl *NewFD) {
  const CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(NewFD);
  if (MD && MD->isInstance()) {
    // C++11 [dcl.constexpr]p4:
    //  The definition of a constexpr constructor shall satisfy the following
    //  constraints:
    //  - the class shall not have any virtual base classes;
    const CXXRecordDecl *RD = MD->getParent();
    if (RD->getNumVBases()) {
      Diag(NewFD->getLocation(), diag::err_constexpr_virtual_base)
        << isa<CXXConstructorDecl>(NewFD)
        << getRecordDiagFromTagKind(RD->getTagKind()) << RD->getNumVBases();
      for (const auto &I : RD->vbases())
        Diag(I.getBeginLoc(), diag::note_constexpr_virtual_base_here)
            << I.getSourceRange();
      return false;
    }
  }

  if (!isa<CXXConstructorDecl>(NewFD)) {
    // C++11 [dcl.constexpr]p3:
    //  The definition of a constexpr function shall satisfy the following
    //  constraints:
    // - it shall not be virtual;
    const CXXMethodDecl *Method = dyn_cast<CXXMethodDecl>(NewFD);
    if (Method && Method->isVirtual()) {
      Method = Method->getCanonicalDecl();
      Diag(Method->getLocation(), diag::err_constexpr_virtual);

      // If it's not obvious why this function is virtual, find an overridden
      // function which uses the 'virtual' keyword.
      const CXXMethodDecl *WrittenVirtual = Method;
      while (!WrittenVirtual->isVirtualAsWritten())
        WrittenVirtual = *WrittenVirtual->begin_overridden_methods();
      if (WrittenVirtual != Method)
        Diag(WrittenVirtual->getLocation(),
             diag::note_overridden_virtual_function);
      return false;
    }

    // - its return type shall be a literal type;
    QualType RT = NewFD->getReturnType();
    if (!RT->isDependentType() &&
        RequireLiteralType(NewFD->getLocation(), RT,
                           diag::err_constexpr_non_literal_return))
      return false;
  }

  // - each of its parameter types shall be a literal type;
  if (!CheckConstexprParameterTypes(*this, NewFD))
    return false;

  return true;
}

/// Check the given declaration statement is legal within a constexpr function
/// body. C++11 [dcl.constexpr]p3,p4, and C++1y [dcl.constexpr]p3.
///
/// \return true if the body is OK (maybe only as an extension), false if we
///         have diagnosed a problem.
static bool CheckConstexprDeclStmt(Sema &SemaRef, const FunctionDecl *Dcl,
                                   DeclStmt *DS, SourceLocation &Cxx1yLoc) {
  // C++11 [dcl.constexpr]p3 and p4:
  //  The definition of a constexpr function(p3) or constructor(p4) [...] shall
  //  contain only
  for (const auto *DclIt : DS->decls()) {
    switch (DclIt->getKind()) {
    case Decl::StaticAssert:
    case Decl::Using:
    case Decl::UsingShadow:
    case Decl::UsingDirective:
    case Decl::UnresolvedUsingTypename:
    case Decl::UnresolvedUsingValue:
      //   - static_assert-declarations
      //   - using-declarations,
      //   - using-directives,
      continue;

    case Decl::Typedef:
    case Decl::TypeAlias: {
      //   - typedef declarations and alias-declarations that do not define
      //     classes or enumerations,
      const auto *TN = cast<TypedefNameDecl>(DclIt);
      if (TN->getUnderlyingType()->isVariablyModifiedType()) {
        // Don't allow variably-modified types in constexpr functions.
        TypeLoc TL = TN->getTypeSourceInfo()->getTypeLoc();
        SemaRef.Diag(TL.getBeginLoc(), diag::err_constexpr_vla)
          << TL.getSourceRange() << TL.getType()
          << isa<CXXConstructorDecl>(Dcl);
        return false;
      }
      continue;
    }

    case Decl::Enum:
    case Decl::CXXRecord:
      // C++1y allows types to be defined, not just declared.
      if (cast<TagDecl>(DclIt)->isThisDeclarationADefinition())
        SemaRef.Diag(DS->getBeginLoc(),
                     SemaRef.getLangOpts().CPlusPlus14
                         ? diag::warn_cxx11_compat_constexpr_type_definition
                         : diag::ext_constexpr_type_definition)
            << isa<CXXConstructorDecl>(Dcl);
      continue;

    case Decl::EnumConstant:
    case Decl::IndirectField:
    case Decl::ParmVar:
      // These can only appear with other declarations which are banned in
      // C++11 and permitted in C++1y, so ignore them.
      continue;

    case Decl::Var:
    case Decl::Decomposition: {
      // C++1y [dcl.constexpr]p3 allows anything except:
      //   a definition of a variable of non-literal type or of static or
      //   thread storage duration or for which no initialization is performed.
      const auto *VD = cast<VarDecl>(DclIt);
      if (VD->isThisDeclarationADefinition()) {
        if (VD->isStaticLocal()) {
          SemaRef.Diag(VD->getLocation(),
                       diag::err_constexpr_local_var_static)
            << isa<CXXConstructorDecl>(Dcl)
            << (VD->getTLSKind() == VarDecl::TLS_Dynamic);
          return false;
        }
        if (!VD->getType()->isDependentType() &&
            SemaRef.RequireLiteralType(
              VD->getLocation(), VD->getType(),
              diag::err_constexpr_local_var_non_literal_type,
              isa<CXXConstructorDecl>(Dcl)))
          return false;
        if (!VD->getType()->isDependentType() &&
            !VD->hasInit() && !VD->isCXXForRangeDecl()) {
          SemaRef.Diag(VD->getLocation(),
                       diag::err_constexpr_local_var_no_init)
            << isa<CXXConstructorDecl>(Dcl);
          return false;
        }
      }
      SemaRef.Diag(VD->getLocation(),
                   SemaRef.getLangOpts().CPlusPlus14
                    ? diag::warn_cxx11_compat_constexpr_local_var
                    : diag::ext_constexpr_local_var)
        << isa<CXXConstructorDecl>(Dcl);
      continue;
    }

    case Decl::NamespaceAlias:
    case Decl::Function:
      // These are disallowed in C++11 and permitted in C++1y. Allow them
      // everywhere as an extension.
      if (!Cxx1yLoc.isValid())
        Cxx1yLoc = DS->getBeginLoc();
      continue;

    default:
      SemaRef.Diag(DS->getBeginLoc(), diag::err_constexpr_body_invalid_stmt)
          << isa<CXXConstructorDecl>(Dcl);
      return false;
    }
  }

  return true;
}

/// Check that the given field is initialized within a constexpr constructor.
///
/// \param Dcl The constexpr constructor being checked.
/// \param Field The field being checked. This may be a member of an anonymous
///        struct or union nested within the class being checked.
/// \param Inits All declarations, including anonymous struct/union members and
///        indirect members, for which any initialization was provided.
/// \param Diagnosed Set to true if an error is produced.
static void CheckConstexprCtorInitializer(Sema &SemaRef,
                                          const FunctionDecl *Dcl,
                                          FieldDecl *Field,
                                          llvm::SmallSet<Decl*, 16> &Inits,
                                          bool &Diagnosed) {
  if (Field->isInvalidDecl())
    return;

  if (Field->isUnnamedBitfield())
    return;

  // Anonymous unions with no variant members and empty anonymous structs do not
  // need to be explicitly initialized. FIXME: Anonymous structs that contain no
  // indirect fields don't need initializing.
  if (Field->isAnonymousStructOrUnion() &&
      (Field->getType()->isUnionType()
           ? !Field->getType()->getAsCXXRecordDecl()->hasVariantMembers()
           : Field->getType()->getAsCXXRecordDecl()->isEmpty()))
    return;

  if (!Inits.count(Field)) {
    if (!Diagnosed) {
      SemaRef.Diag(Dcl->getLocation(), diag::err_constexpr_ctor_missing_init);
      Diagnosed = true;
    }
    SemaRef.Diag(Field->getLocation(), diag::note_constexpr_ctor_missing_init);
  } else if (Field->isAnonymousStructOrUnion()) {
    const RecordDecl *RD = Field->getType()->castAs<RecordType>()->getDecl();
    for (auto *I : RD->fields())
      // If an anonymous union contains an anonymous struct of which any member
      // is initialized, all members must be initialized.
      if (!RD->isUnion() || Inits.count(I))
        CheckConstexprCtorInitializer(SemaRef, Dcl, I, Inits, Diagnosed);
  }
}

/// Check the provided statement is allowed in a constexpr function
/// definition.
static bool
CheckConstexprFunctionStmt(Sema &SemaRef, const FunctionDecl *Dcl, Stmt *S,
                           SmallVectorImpl<SourceLocation> &ReturnStmts,
                           SourceLocation &Cxx1yLoc, SourceLocation &Cxx2aLoc) {
  // - its function-body shall be [...] a compound-statement that contains only
  switch (S->getStmtClass()) {
  case Stmt::NullStmtClass:
    //   - null statements,
    return true;

  case Stmt::DeclStmtClass:
    //   - static_assert-declarations
    //   - using-declarations,
    //   - using-directives,
    //   - typedef declarations and alias-declarations that do not define
    //     classes or enumerations,
    if (!CheckConstexprDeclStmt(SemaRef, Dcl, cast<DeclStmt>(S), Cxx1yLoc))
      return false;
    return true;

  case Stmt::ReturnStmtClass:
    //   - and exactly one return statement;
    if (isa<CXXConstructorDecl>(Dcl)) {
      // C++1y allows return statements in constexpr constructors.
      if (!Cxx1yLoc.isValid())
        Cxx1yLoc = S->getBeginLoc();
      return true;
    }

    ReturnStmts.push_back(S->getBeginLoc());
    return true;

  case Stmt::CompoundStmtClass: {
    // C++1y allows compound-statements.
    if (!Cxx1yLoc.isValid())
      Cxx1yLoc = S->getBeginLoc();

    CompoundStmt *CompStmt = cast<CompoundStmt>(S);
    for (auto *BodyIt : CompStmt->body()) {
      if (!CheckConstexprFunctionStmt(SemaRef, Dcl, BodyIt, ReturnStmts,
                                      Cxx1yLoc, Cxx2aLoc))
        return false;
    }
    return true;
  }

  case Stmt::AttributedStmtClass:
    if (!Cxx1yLoc.isValid())
      Cxx1yLoc = S->getBeginLoc();
    return true;

  case Stmt::IfStmtClass: {
    // C++1y allows if-statements.
    if (!Cxx1yLoc.isValid())
      Cxx1yLoc = S->getBeginLoc();

    IfStmt *If = cast<IfStmt>(S);
    if (!CheckConstexprFunctionStmt(SemaRef, Dcl, If->getThen(), ReturnStmts,
                                    Cxx1yLoc, Cxx2aLoc))
      return false;
    if (If->getElse() &&
        !CheckConstexprFunctionStmt(SemaRef, Dcl, If->getElse(), ReturnStmts,
                                    Cxx1yLoc, Cxx2aLoc))
      return false;
    return true;
  }

  case Stmt::WhileStmtClass:
  case Stmt::DoStmtClass:
  case Stmt::ForStmtClass:
  case Stmt::CXXForRangeStmtClass:
  case Stmt::ContinueStmtClass:
    // C++1y allows all of these. We don't allow them as extensions in C++11,
    // because they don't make sense without variable mutation.
    if (!SemaRef.getLangOpts().CPlusPlus14)
      break;
    if (!Cxx1yLoc.isValid())
      Cxx1yLoc = S->getBeginLoc();
    for (Stmt *SubStmt : S->children())
      if (SubStmt &&
          !CheckConstexprFunctionStmt(SemaRef, Dcl, SubStmt, ReturnStmts,
                                      Cxx1yLoc, Cxx2aLoc))
        return false;
    return true;

  case Stmt::SwitchStmtClass:
  case Stmt::CaseStmtClass:
  case Stmt::DefaultStmtClass:
  case Stmt::BreakStmtClass:
    // C++1y allows switch-statements, and since they don't need variable
    // mutation, we can reasonably allow them in C++11 as an extension.
    if (!Cxx1yLoc.isValid())
      Cxx1yLoc = S->getBeginLoc();
    for (Stmt *SubStmt : S->children())
      if (SubStmt &&
          !CheckConstexprFunctionStmt(SemaRef, Dcl, SubStmt, ReturnStmts,
                                      Cxx1yLoc, Cxx2aLoc))
        return false;
    return true;

  case Stmt::CXXTryStmtClass:
    if (Cxx2aLoc.isInvalid())
      Cxx2aLoc = S->getBeginLoc();
    for (Stmt *SubStmt : S->children()) {
      if (SubStmt &&
          !CheckConstexprFunctionStmt(SemaRef, Dcl, SubStmt, ReturnStmts,
                                      Cxx1yLoc, Cxx2aLoc))
        return false;
    }
    return true;

  case Stmt::CXXCatchStmtClass:
    // Do not bother checking the language mode (already covered by the
    // try block check).
    if (!CheckConstexprFunctionStmt(SemaRef, Dcl,
                                    cast<CXXCatchStmt>(S)->getHandlerBlock(),
                                    ReturnStmts, Cxx1yLoc, Cxx2aLoc))
      return false;
    return true;

  default:
    if (!isa<Expr>(S))
      break;

    // C++1y allows expression-statements.
    if (!Cxx1yLoc.isValid())
      Cxx1yLoc = S->getBeginLoc();
    return true;
  }

  SemaRef.Diag(S->getBeginLoc(), diag::err_constexpr_body_invalid_stmt)
      << isa<CXXConstructorDecl>(Dcl);
  return false;
}

/// Check the body for the given constexpr function declaration only contains
/// the permitted types of statement. C++11 [dcl.constexpr]p3,p4.
///
/// \return true if the body is OK, false if we have diagnosed a problem.
bool Sema::CheckConstexprFunctionBody(const FunctionDecl *Dcl, Stmt *Body) {
  SmallVector<SourceLocation, 4> ReturnStmts;

  if (isa<CXXTryStmt>(Body)) {
    // C++11 [dcl.constexpr]p3:
    //  The definition of a constexpr function shall satisfy the following
    //  constraints: [...]
    // - its function-body shall be = delete, = default, or a
    //   compound-statement
    //
    // C++11 [dcl.constexpr]p4:
    //  In the definition of a constexpr constructor, [...]
    // - its function-body shall not be a function-try-block;
    //
    // This restriction is lifted in C++2a, as long as inner statements also
    // apply the general constexpr rules.
    Diag(Body->getBeginLoc(),
         !getLangOpts().CPlusPlus2a
             ? diag::ext_constexpr_function_try_block_cxx2a
             : diag::warn_cxx17_compat_constexpr_function_try_block)
        << isa<CXXConstructorDecl>(Dcl);
  }

  // - its function-body shall be [...] a compound-statement that contains only
  //   [... list of cases ...]
  //
  // Note that walking the children here is enough to properly check for
  // CompoundStmt and CXXTryStmt body.
  SourceLocation Cxx1yLoc, Cxx2aLoc;
  for (Stmt *SubStmt : Body->children()) {
    if (SubStmt &&
        !CheckConstexprFunctionStmt(*this, Dcl, SubStmt, ReturnStmts,
                                    Cxx1yLoc, Cxx2aLoc))
      return false;
  }

  if (Cxx2aLoc.isValid())
    Diag(Cxx2aLoc,
         getLangOpts().CPlusPlus2a
           ? diag::warn_cxx17_compat_constexpr_body_invalid_stmt
           : diag::ext_constexpr_body_invalid_stmt_cxx2a)
      << isa<CXXConstructorDecl>(Dcl);
  if (Cxx1yLoc.isValid())
    Diag(Cxx1yLoc,
         getLangOpts().CPlusPlus14
           ? diag::warn_cxx11_compat_constexpr_body_invalid_stmt
           : diag::ext_constexpr_body_invalid_stmt)
      << isa<CXXConstructorDecl>(Dcl);

  if (const CXXConstructorDecl *Constructor
        = dyn_cast<CXXConstructorDecl>(Dcl)) {
    const CXXRecordDecl *RD = Constructor->getParent();
    // DR1359:
    // - every non-variant non-static data member and base class sub-object
    //   shall be initialized;
    // DR1460:
    // - if the class is a union having variant members, exactly one of them
    //   shall be initialized;
    if (RD->isUnion()) {
      if (Constructor->getNumCtorInitializers() == 0 &&
          RD->hasVariantMembers()) {
        Diag(Dcl->getLocation(), diag::err_constexpr_union_ctor_no_init);
        return false;
      }
    } else if (!Constructor->isDependentContext() &&
               !Constructor->isDelegatingConstructor()) {
      assert(RD->getNumVBases() == 0 && "constexpr ctor with virtual bases");

      // Skip detailed checking if we have enough initializers, and we would
      // allow at most one initializer per member.
      bool AnyAnonStructUnionMembers = false;
      unsigned Fields = 0;
      for (CXXRecordDecl::field_iterator I = RD->field_begin(),
           E = RD->field_end(); I != E; ++I, ++Fields) {
        if (I->isAnonymousStructOrUnion()) {
          AnyAnonStructUnionMembers = true;
          break;
        }
      }
      // DR1460:
      // - if the class is a union-like class, but is not a union, for each of
      //   its anonymous union members having variant members, exactly one of
      //   them shall be initialized;
      if (AnyAnonStructUnionMembers ||
          Constructor->getNumCtorInitializers() != RD->getNumBases() + Fields) {
        // Check initialization of non-static data members. Base classes are
        // always initialized so do not need to be checked. Dependent bases
        // might not have initializers in the member initializer list.
        llvm::SmallSet<Decl*, 16> Inits;
        for (const auto *I: Constructor->inits()) {
          if (FieldDecl *FD = I->getMember())
            Inits.insert(FD);
          else if (IndirectFieldDecl *ID = I->getIndirectMember())
            Inits.insert(ID->chain_begin(), ID->chain_end());
        }

        bool Diagnosed = false;
        for (auto *I : RD->fields())
          CheckConstexprCtorInitializer(*this, Dcl, I, Inits, Diagnosed);
        if (Diagnosed)
          return false;
      }
    }
  } else {
    if (ReturnStmts.empty()) {
      // C++1y doesn't require constexpr functions to contain a 'return'
      // statement. We still do, unless the return type might be void, because
      // otherwise if there's no return statement, the function cannot
      // be used in a core constant expression.
      bool OK = getLangOpts().CPlusPlus14 &&
                (Dcl->getReturnType()->isVoidType() ||
                 Dcl->getReturnType()->isDependentType());
      Diag(Dcl->getLocation(),
           OK ? diag::warn_cxx11_compat_constexpr_body_no_return
              : diag::err_constexpr_body_no_return);
      if (!OK)
        return false;
    } else if (ReturnStmts.size() > 1) {
      Diag(ReturnStmts.back(),
           getLangOpts().CPlusPlus14
             ? diag::warn_cxx11_compat_constexpr_body_multiple_return
             : diag::ext_constexpr_body_multiple_return);
      for (unsigned I = 0; I < ReturnStmts.size() - 1; ++I)
        Diag(ReturnStmts[I], diag::note_constexpr_body_previous_return);
    }
  }

  // C++11 [dcl.constexpr]p5:
  //   if no function argument values exist such that the function invocation
  //   substitution would produce a constant expression, the program is
  //   ill-formed; no diagnostic required.
  // C++11 [dcl.constexpr]p3:
  //   - every constructor call and implicit conversion used in initializing the
  //     return value shall be one of those allowed in a constant expression.
  // C++11 [dcl.constexpr]p4:
  //   - every constructor involved in initializing non-static data members and
  //     base class sub-objects shall be a constexpr constructor.
  SmallVector<PartialDiagnosticAt, 8> Diags;
  if (!Expr::isPotentialConstantExpr(Dcl, Diags)) {
    Diag(Dcl->getLocation(), diag::ext_constexpr_function_never_constant_expr)
      << isa<CXXConstructorDecl>(Dcl);
    for (size_t I = 0, N = Diags.size(); I != N; ++I)
      Diag(Diags[I].first, Diags[I].second);
    // Don't return false here: we allow this for compatibility in
    // system headers.
  }

  return true;
}

/// Get the class that is directly named by the current context. This is the
/// class for which an unqualified-id in this scope could name a constructor
/// or destructor.
///
/// If the scope specifier denotes a class, this will be that class.
/// If the scope specifier is empty, this will be the class whose
/// member-specification we are currently within. Otherwise, there
/// is no such class.
CXXRecordDecl *Sema::getCurrentClass(Scope *, const CXXScopeSpec *SS) {
  assert(getLangOpts().CPlusPlus && "No class names in C!");

  if (SS && SS->isInvalid())
    return nullptr;

  if (SS && SS->isNotEmpty()) {
    DeclContext *DC = computeDeclContext(*SS, true);
    return dyn_cast_or_null<CXXRecordDecl>(DC);
  }

  return dyn_cast_or_null<CXXRecordDecl>(CurContext);
}

/// isCurrentClassName - Determine whether the identifier II is the
/// name of the class type currently being defined. In the case of
/// nested classes, this will only return true if II is the name of
/// the innermost class.
bool Sema::isCurrentClassName(const IdentifierInfo &II, Scope *S,
                              const CXXScopeSpec *SS) {
  CXXRecordDecl *CurDecl = getCurrentClass(S, SS);
  return CurDecl && &II == CurDecl->getIdentifier();
}

/// Determine whether the identifier II is a typo for the name of
/// the class type currently being defined. If so, update it to the identifier
/// that should have been used.
bool Sema::isCurrentClassNameTypo(IdentifierInfo *&II, const CXXScopeSpec *SS) {
  assert(getLangOpts().CPlusPlus && "No class names in C!");

  if (!getLangOpts().SpellChecking)
    return false;

  CXXRecordDecl *CurDecl;
  if (SS && SS->isSet() && !SS->isInvalid()) {
    DeclContext *DC = computeDeclContext(*SS, true);
    CurDecl = dyn_cast_or_null<CXXRecordDecl>(DC);
  } else
    CurDecl = dyn_cast_or_null<CXXRecordDecl>(CurContext);

  if (CurDecl && CurDecl->getIdentifier() && II != CurDecl->getIdentifier() &&
      3 * II->getName().edit_distance(CurDecl->getIdentifier()->getName())
          < II->getLength()) {
    II = CurDecl->getIdentifier();
    return true;
  }

  return false;
}

/// Determine whether the given class is a base class of the given
/// class, including looking at dependent bases.
static bool findCircularInheritance(const CXXRecordDecl *Class,
                                    const CXXRecordDecl *Current) {
  SmallVector<const CXXRecordDecl*, 8> Queue;

  Class = Class->getCanonicalDecl();
  while (true) {
    for (const auto &I : Current->bases()) {
      CXXRecordDecl *Base = I.getType()->getAsCXXRecordDecl();
      if (!Base)
        continue;

      Base = Base->getDefinition();
      if (!Base)
        continue;

      if (Base->getCanonicalDecl() == Class)
        return true;

      Queue.push_back(Base);
    }

    if (Queue.empty())
      return false;

    Current = Queue.pop_back_val();
  }

  return false;
}

/// Check the validity of a C++ base class specifier.
///
/// \returns a new CXXBaseSpecifier if well-formed, emits diagnostics
/// and returns NULL otherwise.
CXXBaseSpecifier *
Sema::CheckBaseSpecifier(CXXRecordDecl *Class,
                         SourceRange SpecifierRange,
                         bool Virtual, AccessSpecifier Access,
                         TypeSourceInfo *TInfo,
                         SourceLocation EllipsisLoc) {
  QualType BaseType = TInfo->getType();

  // C++ [class.union]p1:
  //   A union shall not have base classes.
  if (Class->isUnion()) {
    Diag(Class->getLocation(), diag::err_base_clause_on_union)
      << SpecifierRange;
    return nullptr;
  }

  if (EllipsisLoc.isValid() &&
      !TInfo->getType()->containsUnexpandedParameterPack()) {
    Diag(EllipsisLoc, diag::err_pack_expansion_without_parameter_packs)
      << TInfo->getTypeLoc().getSourceRange();
    EllipsisLoc = SourceLocation();
  }

  SourceLocation BaseLoc = TInfo->getTypeLoc().getBeginLoc();

  if (BaseType->isDependentType()) {
    // Make sure that we don't have circular inheritance among our dependent
    // bases. For non-dependent bases, the check for completeness below handles
    // this.
    if (CXXRecordDecl *BaseDecl = BaseType->getAsCXXRecordDecl()) {
      if (BaseDecl->getCanonicalDecl() == Class->getCanonicalDecl() ||
          ((BaseDecl = BaseDecl->getDefinition()) &&
           findCircularInheritance(Class, BaseDecl))) {
        Diag(BaseLoc, diag::err_circular_inheritance)
          << BaseType << Context.getTypeDeclType(Class);

        if (BaseDecl->getCanonicalDecl() != Class->getCanonicalDecl())
          Diag(BaseDecl->getLocation(), diag::note_previous_decl)
            << BaseType;

        return nullptr;
      }
    }

    return new (Context) CXXBaseSpecifier(SpecifierRange, Virtual,
                                          Class->getTagKind() == TTK_Class,
                                          Access, TInfo, EllipsisLoc);
  }

  // Base specifiers must be record types.
  if (!BaseType->isRecordType()) {
    Diag(BaseLoc, diag::err_base_must_be_class) << SpecifierRange;
    return nullptr;
  }

  // C++ [class.union]p1:
  //   A union shall not be used as a base class.
  if (BaseType->isUnionType()) {
    Diag(BaseLoc, diag::err_union_as_base_class) << SpecifierRange;
    return nullptr;
  }

  // For the MS ABI, propagate DLL attributes to base class templates.
  if (Context.getTargetInfo().getCXXABI().isMicrosoft()) {
    if (Attr *ClassAttr = getDLLAttr(Class)) {
      if (auto *BaseTemplate = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
              BaseType->getAsCXXRecordDecl())) {
        propagateDLLAttrToBaseClassTemplate(Class, ClassAttr, BaseTemplate,
                                            BaseLoc);
      }
    }
  }

  // C++ [class.derived]p2:
  //   The class-name in a base-specifier shall not be an incompletely
  //   defined class.
  if (RequireCompleteType(BaseLoc, BaseType,
                          diag::err_incomplete_base_class, SpecifierRange)) {
    Class->setInvalidDecl();
    return nullptr;
  }

  // If the base class is polymorphic or isn't empty, the new one is/isn't, too.
  RecordDecl *BaseDecl = BaseType->getAs<RecordType>()->getDecl();
  assert(BaseDecl && "Record type has no declaration");
  BaseDecl = BaseDecl->getDefinition();
  assert(BaseDecl && "Base type is not incomplete, but has no definition");
  CXXRecordDecl *CXXBaseDecl = cast<CXXRecordDecl>(BaseDecl);
  assert(CXXBaseDecl && "Base type is not a C++ type");

  // Microsoft docs say:
  // "If a base-class has a code_seg attribute, derived classes must have the
  // same attribute."
  const auto *BaseCSA = CXXBaseDecl->getAttr<CodeSegAttr>();
  const auto *DerivedCSA = Class->getAttr<CodeSegAttr>();
  if ((DerivedCSA || BaseCSA) &&
      (!BaseCSA || !DerivedCSA || BaseCSA->getName() != DerivedCSA->getName())) {
    Diag(Class->getLocation(), diag::err_mismatched_code_seg_base);
    Diag(CXXBaseDecl->getLocation(), diag::note_base_class_specified_here)
      << CXXBaseDecl;
    return nullptr;
  }

  // A class which contains a flexible array member is not suitable for use as a
  // base class:
  //   - If the layout determines that a base comes before another base,
  //     the flexible array member would index into the subsequent base.
  //   - If the layout determines that base comes before the derived class,
  //     the flexible array member would index into the derived class.
  if (CXXBaseDecl->hasFlexibleArrayMember()) {
    Diag(BaseLoc, diag::err_base_class_has_flexible_array_member)
      << CXXBaseDecl->getDeclName();
    return nullptr;
  }

  // C++ [class]p3:
  //   If a class is marked final and it appears as a base-type-specifier in
  //   base-clause, the program is ill-formed.
  if (FinalAttr *FA = CXXBaseDecl->getAttr<FinalAttr>()) {
    Diag(BaseLoc, diag::err_class_marked_final_used_as_base)
      << CXXBaseDecl->getDeclName()
      << FA->isSpelledAsSealed();
    Diag(CXXBaseDecl->getLocation(), diag::note_entity_declared_at)
        << CXXBaseDecl->getDeclName() << FA->getRange();
    return nullptr;
  }

  if (BaseDecl->isInvalidDecl())
    Class->setInvalidDecl();

  // Create the base specifier.
  return new (Context) CXXBaseSpecifier(SpecifierRange, Virtual,
                                        Class->getTagKind() == TTK_Class,
                                        Access, TInfo, EllipsisLoc);
}

/// ActOnBaseSpecifier - Parsed a base specifier. A base specifier is
/// one entry in the base class list of a class specifier, for
/// example:
///    class foo : public bar, virtual private baz {
/// 'public bar' and 'virtual private baz' are each base-specifiers.
BaseResult
Sema::ActOnBaseSpecifier(Decl *classdecl, SourceRange SpecifierRange,
                         ParsedAttributes &Attributes,
                         bool Virtual, AccessSpecifier Access,
                         ParsedType basetype, SourceLocation BaseLoc,
                         SourceLocation EllipsisLoc) {
  if (!classdecl)
    return true;

  AdjustDeclIfTemplate(classdecl);
  CXXRecordDecl *Class = dyn_cast<CXXRecordDecl>(classdecl);
  if (!Class)
    return true;

  // We haven't yet attached the base specifiers.
  Class->setIsParsingBaseSpecifiers();

  // We do not support any C++11 attributes on base-specifiers yet.
  // Diagnose any attributes we see.
  for (const ParsedAttr &AL : Attributes) {
    if (AL.isInvalid() || AL.getKind() == ParsedAttr::IgnoredAttribute)
      continue;
    Diag(AL.getLoc(), AL.getKind() == ParsedAttr::UnknownAttribute
                          ? (unsigned)diag::warn_unknown_attribute_ignored
                          : (unsigned)diag::err_base_specifier_attribute)
        << AL.getName();
  }

  TypeSourceInfo *TInfo = nullptr;
  GetTypeFromParser(basetype, &TInfo);

  if (EllipsisLoc.isInvalid() &&
      DiagnoseUnexpandedParameterPack(SpecifierRange.getBegin(), TInfo,
                                      UPPC_BaseType))
    return true;

  if (CXXBaseSpecifier *BaseSpec = CheckBaseSpecifier(Class, SpecifierRange,
                                                      Virtual, Access, TInfo,
                                                      EllipsisLoc))
    return BaseSpec;
  else
    Class->setInvalidDecl();

  return true;
}

/// Use small set to collect indirect bases.  As this is only used
/// locally, there's no need to abstract the small size parameter.
typedef llvm::SmallPtrSet<QualType, 4> IndirectBaseSet;

/// Recursively add the bases of Type.  Don't add Type itself.
static void
NoteIndirectBases(ASTContext &Context, IndirectBaseSet &Set,
                  const QualType &Type)
{
  // Even though the incoming type is a base, it might not be
  // a class -- it could be a template parm, for instance.
  if (auto Rec = Type->getAs<RecordType>()) {
    auto Decl = Rec->getAsCXXRecordDecl();

    // Iterate over its bases.
    for (const auto &BaseSpec : Decl->bases()) {
      QualType Base = Context.getCanonicalType(BaseSpec.getType())
        .getUnqualifiedType();
      if (Set.insert(Base).second)
        // If we've not already seen it, recurse.
        NoteIndirectBases(Context, Set, Base);
    }
  }
}

/// Performs the actual work of attaching the given base class
/// specifiers to a C++ class.
bool Sema::AttachBaseSpecifiers(CXXRecordDecl *Class,
                                MutableArrayRef<CXXBaseSpecifier *> Bases) {
 if (Bases.empty())
    return false;

  // Used to keep track of which base types we have already seen, so
  // that we can properly diagnose redundant direct base types. Note
  // that the key is always the unqualified canonical type of the base
  // class.
  std::map<QualType, CXXBaseSpecifier*, QualTypeOrdering> KnownBaseTypes;

  // Used to track indirect bases so we can see if a direct base is
  // ambiguous.
  IndirectBaseSet IndirectBaseTypes;

  // Copy non-redundant base specifiers into permanent storage.
  unsigned NumGoodBases = 0;
  bool Invalid = false;
  for (unsigned idx = 0; idx < Bases.size(); ++idx) {
    QualType NewBaseType
      = Context.getCanonicalType(Bases[idx]->getType());
    NewBaseType = NewBaseType.getLocalUnqualifiedType();

    CXXBaseSpecifier *&KnownBase = KnownBaseTypes[NewBaseType];
    if (KnownBase) {
      // C++ [class.mi]p3:
      //   A class shall not be specified as a direct base class of a
      //   derived class more than once.
      Diag(Bases[idx]->getBeginLoc(), diag::err_duplicate_base_class)
          << KnownBase->getType() << Bases[idx]->getSourceRange();

      // Delete the duplicate base class specifier; we're going to
      // overwrite its pointer later.
      Context.Deallocate(Bases[idx]);

      Invalid = true;
    } else {
      // Okay, add this new base class.
      KnownBase = Bases[idx];
      Bases[NumGoodBases++] = Bases[idx];

      // Note this base's direct & indirect bases, if there could be ambiguity.
      if (Bases.size() > 1)
        NoteIndirectBases(Context, IndirectBaseTypes, NewBaseType);

      if (const RecordType *Record = NewBaseType->getAs<RecordType>()) {
        const CXXRecordDecl *RD = cast<CXXRecordDecl>(Record->getDecl());
        if (Class->isInterface() &&
              (!RD->isInterfaceLike() ||
               KnownBase->getAccessSpecifier() != AS_public)) {
          // The Microsoft extension __interface does not permit bases that
          // are not themselves public interfaces.
          Diag(KnownBase->getBeginLoc(), diag::err_invalid_base_in_interface)
              << getRecordDiagFromTagKind(RD->getTagKind()) << RD
              << RD->getSourceRange();
          Invalid = true;
        }
        if (RD->hasAttr<WeakAttr>())
          Class->addAttr(WeakAttr::CreateImplicit(Context));
      }
    }
  }

  // Attach the remaining base class specifiers to the derived class.
  Class->setBases(Bases.data(), NumGoodBases);

  // Check that the only base classes that are duplicate are virtual.
  for (unsigned idx = 0; idx < NumGoodBases; ++idx) {
    // Check whether this direct base is inaccessible due to ambiguity.
    QualType BaseType = Bases[idx]->getType();

    // Skip all dependent types in templates being used as base specifiers.
    // Checks below assume that the base specifier is a CXXRecord.
    if (BaseType->isDependentType())
      continue;

    CanQualType CanonicalBase = Context.getCanonicalType(BaseType)
      .getUnqualifiedType();

    if (IndirectBaseTypes.count(CanonicalBase)) {
      CXXBasePaths Paths(/*FindAmbiguities=*/true, /*RecordPaths=*/true,
                         /*DetectVirtual=*/true);
      bool found
        = Class->isDerivedFrom(CanonicalBase->getAsCXXRecordDecl(), Paths);
      assert(found);
      (void)found;

      if (Paths.isAmbiguous(CanonicalBase))
        Diag(Bases[idx]->getBeginLoc(), diag::warn_inaccessible_base_class)
            << BaseType << getAmbiguousPathsDisplayString(Paths)
            << Bases[idx]->getSourceRange();
      else
        assert(Bases[idx]->isVirtual());
    }

    // Delete the base class specifier, since its data has been copied
    // into the CXXRecordDecl.
    Context.Deallocate(Bases[idx]);
  }

  return Invalid;
}

/// ActOnBaseSpecifiers - Attach the given base specifiers to the
/// class, after checking whether there are any duplicate base
/// classes.
void Sema::ActOnBaseSpecifiers(Decl *ClassDecl,
                               MutableArrayRef<CXXBaseSpecifier *> Bases) {
  if (!ClassDecl || Bases.empty())
    return;

  AdjustDeclIfTemplate(ClassDecl);
  AttachBaseSpecifiers(cast<CXXRecordDecl>(ClassDecl), Bases);
}

/// Determine whether the type \p Derived is a C++ class that is
/// derived from the type \p Base.
bool Sema::IsDerivedFrom(SourceLocation Loc, QualType Derived, QualType Base) {
  if (!getLangOpts().CPlusPlus)
    return false;

  CXXRecordDecl *DerivedRD = Derived->getAsCXXRecordDecl();
  if (!DerivedRD)
    return false;

  CXXRecordDecl *BaseRD = Base->getAsCXXRecordDecl();
  if (!BaseRD)
    return false;

  // If either the base or the derived type is invalid, don't try to
  // check whether one is derived from the other.
  if (BaseRD->isInvalidDecl() || DerivedRD->isInvalidDecl())
    return false;

  // FIXME: In a modules build, do we need the entire path to be visible for us
  // to be able to use the inheritance relationship?
  if (!isCompleteType(Loc, Derived) && !DerivedRD->isBeingDefined())
    return false;

  return DerivedRD->isDerivedFrom(BaseRD);
}

/// Determine whether the type \p Derived is a C++ class that is
/// derived from the type \p Base.
bool Sema::IsDerivedFrom(SourceLocation Loc, QualType Derived, QualType Base,
                         CXXBasePaths &Paths) {
  if (!getLangOpts().CPlusPlus)
    return false;

  CXXRecordDecl *DerivedRD = Derived->getAsCXXRecordDecl();
  if (!DerivedRD)
    return false;

  CXXRecordDecl *BaseRD = Base->getAsCXXRecordDecl();
  if (!BaseRD)
    return false;

  if (!isCompleteType(Loc, Derived) && !DerivedRD->isBeingDefined())
    return false;

  return DerivedRD->isDerivedFrom(BaseRD, Paths);
}

static void BuildBasePathArray(const CXXBasePath &Path,
                               CXXCastPath &BasePathArray) {
  // We first go backward and check if we have a virtual base.
  // FIXME: It would be better if CXXBasePath had the base specifier for
  // the nearest virtual base.
  unsigned Start = 0;
  for (unsigned I = Path.size(); I != 0; --I) {
    if (Path[I - 1].Base->isVirtual()) {
      Start = I - 1;
      break;
    }
  }

  // Now add all bases.
  for (unsigned I = Start, E = Path.size(); I != E; ++I)
    BasePathArray.push_back(const_cast<CXXBaseSpecifier*>(Path[I].Base));
}


void Sema::BuildBasePathArray(const CXXBasePaths &Paths,
                              CXXCastPath &BasePathArray) {
  assert(BasePathArray.empty() && "Base path array must be empty!");
  assert(Paths.isRecordingPaths() && "Must record paths!");
  return ::BuildBasePathArray(Paths.front(), BasePathArray);
}
/// CheckDerivedToBaseConversion - Check whether the Derived-to-Base
/// conversion (where Derived and Base are class types) is
/// well-formed, meaning that the conversion is unambiguous (and
/// that all of the base classes are accessible). Returns true
/// and emits a diagnostic if the code is ill-formed, returns false
/// otherwise. Loc is the location where this routine should point to
/// if there is an error, and Range is the source range to highlight
/// if there is an error.
///
/// If either InaccessibleBaseID or AmbigiousBaseConvID are 0, then the
/// diagnostic for the respective type of error will be suppressed, but the
/// check for ill-formed code will still be performed.
bool
Sema::CheckDerivedToBaseConversion(QualType Derived, QualType Base,
                                   unsigned InaccessibleBaseID,
                                   unsigned AmbigiousBaseConvID,
                                   SourceLocation Loc, SourceRange Range,
                                   DeclarationName Name,
                                   CXXCastPath *BasePath,
                                   bool IgnoreAccess) {
  // First, determine whether the path from Derived to Base is
  // ambiguous. This is slightly more expensive than checking whether
  // the Derived to Base conversion exists, because here we need to
  // explore multiple paths to determine if there is an ambiguity.
  CXXBasePaths Paths(/*FindAmbiguities=*/true, /*RecordPaths=*/true,
                     /*DetectVirtual=*/false);
  bool DerivationOkay = IsDerivedFrom(Loc, Derived, Base, Paths);
  if (!DerivationOkay)
    return true;

  const CXXBasePath *Path = nullptr;
  if (!Paths.isAmbiguous(Context.getCanonicalType(Base).getUnqualifiedType()))
    Path = &Paths.front();

  // For MSVC compatibility, check if Derived directly inherits from Base. Clang
  // warns about this hierarchy under -Winaccessible-base, but MSVC allows the
  // user to access such bases.
  if (!Path && getLangOpts().MSVCCompat) {
    for (const CXXBasePath &PossiblePath : Paths) {
      if (PossiblePath.size() == 1) {
        Path = &PossiblePath;
        if (AmbigiousBaseConvID)
          Diag(Loc, diag::ext_ms_ambiguous_direct_base)
              << Base << Derived << Range;
        break;
      }
    }
  }

  if (Path) {
    if (!IgnoreAccess) {
      // Check that the base class can be accessed.
      switch (
          CheckBaseClassAccess(Loc, Base, Derived, *Path, InaccessibleBaseID)) {
      case AR_inaccessible:
        return true;
      case AR_accessible:
      case AR_dependent:
      case AR_delayed:
        break;
      }
    }

    // Build a base path if necessary.
    if (BasePath)
      ::BuildBasePathArray(*Path, *BasePath);
    return false;
  }

  if (AmbigiousBaseConvID) {
    // We know that the derived-to-base conversion is ambiguous, and
    // we're going to produce a diagnostic. Perform the derived-to-base
    // search just one more time to compute all of the possible paths so
    // that we can print them out. This is more expensive than any of
    // the previous derived-to-base checks we've done, but at this point
    // performance isn't as much of an issue.
    Paths.clear();
    Paths.setRecordingPaths(true);
    bool StillOkay = IsDerivedFrom(Loc, Derived, Base, Paths);
    assert(StillOkay && "Can only be used with a derived-to-base conversion");
    (void)StillOkay;

    // Build up a textual representation of the ambiguous paths, e.g.,
    // D -> B -> A, that will be used to illustrate the ambiguous
    // conversions in the diagnostic. We only print one of the paths
    // to each base class subobject.
    std::string PathDisplayStr = getAmbiguousPathsDisplayString(Paths);

    Diag(Loc, AmbigiousBaseConvID)
    << Derived << Base << PathDisplayStr << Range << Name;
  }
  return true;
}

bool
Sema::CheckDerivedToBaseConversion(QualType Derived, QualType Base,
                                   SourceLocation Loc, SourceRange Range,
                                   CXXCastPath *BasePath,
                                   bool IgnoreAccess) {
  return CheckDerivedToBaseConversion(
      Derived, Base, diag::err_upcast_to_inaccessible_base,
      diag::err_ambiguous_derived_to_base_conv, Loc, Range, DeclarationName(),
      BasePath, IgnoreAccess);
}


/// Builds a string representing ambiguous paths from a
/// specific derived class to different subobjects of the same base
/// class.
///
/// This function builds a string that can be used in error messages
/// to show the different paths that one can take through the
/// inheritance hierarchy to go from the derived class to different
/// subobjects of a base class. The result looks something like this:
/// @code
/// struct D -> struct B -> struct A
/// struct D -> struct C -> struct A
/// @endcode
std::string Sema::getAmbiguousPathsDisplayString(CXXBasePaths &Paths) {
  std::string PathDisplayStr;
  std::set<unsigned> DisplayedPaths;
  for (CXXBasePaths::paths_iterator Path = Paths.begin();
       Path != Paths.end(); ++Path) {
    if (DisplayedPaths.insert(Path->back().SubobjectNumber).second) {
      // We haven't displayed a path to this particular base
      // class subobject yet.
      PathDisplayStr += "\n    ";
      PathDisplayStr += Context.getTypeDeclType(Paths.getOrigin()).getAsString();
      for (CXXBasePath::const_iterator Element = Path->begin();
           Element != Path->end(); ++Element)
        PathDisplayStr += " -> " + Element->Base->getType().getAsString();
    }
  }

  return PathDisplayStr;
}

//===----------------------------------------------------------------------===//
// C++ class member Handling
//===----------------------------------------------------------------------===//

/// ActOnAccessSpecifier - Parsed an access specifier followed by a colon.
bool Sema::ActOnAccessSpecifier(AccessSpecifier Access, SourceLocation ASLoc,
                                SourceLocation ColonLoc,
                                const ParsedAttributesView &Attrs) {
  assert(Access != AS_none && "Invalid kind for syntactic access specifier!");
  AccessSpecDecl *ASDecl = AccessSpecDecl::Create(Context, Access, CurContext,
                                                  ASLoc, ColonLoc);
  CurContext->addHiddenDecl(ASDecl);
  return ProcessAccessDeclAttributeList(ASDecl, Attrs);
}

/// CheckOverrideControl - Check C++11 override control semantics.
void Sema::CheckOverrideControl(NamedDecl *D) {
  if (D->isInvalidDecl())
    return;

  // We only care about "override" and "final" declarations.
  if (!D->hasAttr<OverrideAttr>() && !D->hasAttr<FinalAttr>())
    return;

  CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(D);

  // We can't check dependent instance methods.
  if (MD && MD->isInstance() &&
      (MD->getParent()->hasAnyDependentBases() ||
       MD->getType()->isDependentType()))
    return;

  if (MD && !MD->isVirtual()) {
    // If we have a non-virtual method, check if if hides a virtual method.
    // (In that case, it's most likely the method has the wrong type.)
    SmallVector<CXXMethodDecl *, 8> OverloadedMethods;
    FindHiddenVirtualMethods(MD, OverloadedMethods);

    if (!OverloadedMethods.empty()) {
      if (OverrideAttr *OA = D->getAttr<OverrideAttr>()) {
        Diag(OA->getLocation(),
             diag::override_keyword_hides_virtual_member_function)
          << "override" << (OverloadedMethods.size() > 1);
      } else if (FinalAttr *FA = D->getAttr<FinalAttr>()) {
        Diag(FA->getLocation(),
             diag::override_keyword_hides_virtual_member_function)
          << (FA->isSpelledAsSealed() ? "sealed" : "final")
          << (OverloadedMethods.size() > 1);
      }
      NoteHiddenVirtualMethods(MD, OverloadedMethods);
      MD->setInvalidDecl();
      return;
    }
    // Fall through into the general case diagnostic.
    // FIXME: We might want to attempt typo correction here.
  }

  if (!MD || !MD->isVirtual()) {
    if (OverrideAttr *OA = D->getAttr<OverrideAttr>()) {
      Diag(OA->getLocation(),
           diag::override_keyword_only_allowed_on_virtual_member_functions)
        << "override" << FixItHint::CreateRemoval(OA->getLocation());
      D->dropAttr<OverrideAttr>();
    }
    if (FinalAttr *FA = D->getAttr<FinalAttr>()) {
      Diag(FA->getLocation(),
           diag::override_keyword_only_allowed_on_virtual_member_functions)
        << (FA->isSpelledAsSealed() ? "sealed" : "final")
        << FixItHint::CreateRemoval(FA->getLocation());
      D->dropAttr<FinalAttr>();
    }
    return;
  }

  // C++11 [class.virtual]p5:
  //   If a function is marked with the virt-specifier override and
  //   does not override a member function of a base class, the program is
  //   ill-formed.
  bool HasOverriddenMethods = MD->size_overridden_methods() != 0;
  if (MD->hasAttr<OverrideAttr>() && !HasOverriddenMethods)
    Diag(MD->getLocation(), diag::err_function_marked_override_not_overriding)
      << MD->getDeclName();
}

void Sema::DiagnoseAbsenceOfOverrideControl(NamedDecl *D) {
  if (D->isInvalidDecl() || D->hasAttr<OverrideAttr>())
    return;
  CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(D);
  if (!MD || MD->isImplicit() || MD->hasAttr<FinalAttr>())
    return;

  SourceLocation Loc = MD->getLocation();
  SourceLocation SpellingLoc = Loc;
  if (getSourceManager().isMacroArgExpansion(Loc))
    SpellingLoc = getSourceManager().getImmediateExpansionRange(Loc).getBegin();
  SpellingLoc = getSourceManager().getSpellingLoc(SpellingLoc);
  if (SpellingLoc.isValid() && getSourceManager().isInSystemHeader(SpellingLoc))
      return;

  if (MD->size_overridden_methods() > 0) {
    unsigned DiagID = isa<CXXDestructorDecl>(MD)
                          ? diag::warn_destructor_marked_not_override_overriding
                          : diag::warn_function_marked_not_override_overriding;
    Diag(MD->getLocation(), DiagID) << MD->getDeclName();
    const CXXMethodDecl *OMD = *MD->begin_overridden_methods();
    Diag(OMD->getLocation(), diag::note_overridden_virtual_function);
  }
}

/// CheckIfOverriddenFunctionIsMarkedFinal - Checks whether a virtual member
/// function overrides a virtual member function marked 'final', according to
/// C++11 [class.virtual]p4.
bool Sema::CheckIfOverriddenFunctionIsMarkedFinal(const CXXMethodDecl *New,
                                                  const CXXMethodDecl *Old) {
  FinalAttr *FA = Old->getAttr<FinalAttr>();
  if (!FA)
    return false;

  Diag(New->getLocation(), diag::err_final_function_overridden)
    << New->getDeclName()
    << FA->isSpelledAsSealed();
  Diag(Old->getLocation(), diag::note_overridden_virtual_function);
  return true;
}

static bool InitializationHasSideEffects(const FieldDecl &FD) {
  const Type *T = FD.getType()->getBaseElementTypeUnsafe();
  // FIXME: Destruction of ObjC lifetime types has side-effects.
  if (const CXXRecordDecl *RD = T->getAsCXXRecordDecl())
    return !RD->isCompleteDefinition() ||
           !RD->hasTrivialDefaultConstructor() ||
           !RD->hasTrivialDestructor();
  return false;
}

static const ParsedAttr *getMSPropertyAttr(const ParsedAttributesView &list) {
  ParsedAttributesView::const_iterator Itr =
      llvm::find_if(list, [](const ParsedAttr &AL) {
        return AL.isDeclspecPropertyAttribute();
      });
  if (Itr != list.end())
    return &*Itr;
  return nullptr;
}

// Check if there is a field shadowing.
void Sema::CheckShadowInheritedFields(const SourceLocation &Loc,
                                      DeclarationName FieldName,
                                      const CXXRecordDecl *RD,
                                      bool DeclIsField) {
  if (Diags.isIgnored(diag::warn_shadow_field, Loc))
    return;

  // To record a shadowed field in a base
  std::map<CXXRecordDecl*, NamedDecl*> Bases;
  auto FieldShadowed = [&](const CXXBaseSpecifier *Specifier,
                           CXXBasePath &Path) {
    const auto Base = Specifier->getType()->getAsCXXRecordDecl();
    // Record an ambiguous path directly
    if (Bases.find(Base) != Bases.end())
      return true;
    for (const auto Field : Base->lookup(FieldName)) {
      if ((isa<FieldDecl>(Field) || isa<IndirectFieldDecl>(Field)) &&
          Field->getAccess() != AS_private) {
        assert(Field->getAccess() != AS_none);
        assert(Bases.find(Base) == Bases.end());
        Bases[Base] = Field;
        return true;
      }
    }
    return false;
  };

  CXXBasePaths Paths(/*FindAmbiguities=*/true, /*RecordPaths=*/true,
                     /*DetectVirtual=*/true);
  if (!RD->lookupInBases(FieldShadowed, Paths))
    return;

  for (const auto &P : Paths) {
    auto Base = P.back().Base->getType()->getAsCXXRecordDecl();
    auto It = Bases.find(Base);
    // Skip duplicated bases
    if (It == Bases.end())
      continue;
    auto BaseField = It->second;
    assert(BaseField->getAccess() != AS_private);
    if (AS_none !=
        CXXRecordDecl::MergeAccess(P.Access, BaseField->getAccess())) {
      Diag(Loc, diag::warn_shadow_field)
        << FieldName << RD << Base << DeclIsField;
      Diag(BaseField->getLocation(), diag::note_shadow_field);
      Bases.erase(It);
    }
  }
}

/// ActOnCXXMemberDeclarator - This is invoked when a C++ class member
/// declarator is parsed. 'AS' is the access specifier, 'BW' specifies the
/// bitfield width if there is one, 'InitExpr' specifies the initializer if
/// one has been parsed, and 'InitStyle' is set if an in-class initializer is
/// present (but parsing it has been deferred).
NamedDecl *
Sema::ActOnCXXMemberDeclarator(Scope *S, AccessSpecifier AS, Declarator &D,
                               MultiTemplateParamsArg TemplateParameterLists,
                               Expr *BW, const VirtSpecifiers &VS,
                               InClassInitStyle InitStyle) {
  const DeclSpec &DS = D.getDeclSpec();
  DeclarationNameInfo NameInfo = GetNameForDeclarator(D);
  DeclarationName Name = NameInfo.getName();
  SourceLocation Loc = NameInfo.getLoc();

  // For anonymous bitfields, the location should point to the type.
  if (Loc.isInvalid())
    Loc = D.getBeginLoc();

  Expr *BitWidth = static_cast<Expr*>(BW);

  assert(isa<CXXRecordDecl>(CurContext));
  assert(!DS.isFriendSpecified());

  bool isFunc = D.isDeclarationOfFunction();
  const ParsedAttr *MSPropertyAttr =
      getMSPropertyAttr(D.getDeclSpec().getAttributes());

  if (cast<CXXRecordDecl>(CurContext)->isInterface()) {
    // The Microsoft extension __interface only permits public member functions
    // and prohibits constructors, destructors, operators, non-public member
    // functions, static methods and data members.
    unsigned InvalidDecl;
    bool ShowDeclName = true;
    if (!isFunc &&
        (DS.getStorageClassSpec() == DeclSpec::SCS_typedef || MSPropertyAttr))
      InvalidDecl = 0;
    else if (!isFunc)
      InvalidDecl = 1;
    else if (AS != AS_public)
      InvalidDecl = 2;
    else if (DS.getStorageClassSpec() == DeclSpec::SCS_static)
      InvalidDecl = 3;
    else switch (Name.getNameKind()) {
      case DeclarationName::CXXConstructorName:
        InvalidDecl = 4;
        ShowDeclName = false;
        break;

      case DeclarationName::CXXDestructorName:
        InvalidDecl = 5;
        ShowDeclName = false;
        break;

      case DeclarationName::CXXOperatorName:
      case DeclarationName::CXXConversionFunctionName:
        InvalidDecl = 6;
        break;

      default:
        InvalidDecl = 0;
        break;
    }

    if (InvalidDecl) {
      if (ShowDeclName)
        Diag(Loc, diag::err_invalid_member_in_interface)
          << (InvalidDecl-1) << Name;
      else
        Diag(Loc, diag::err_invalid_member_in_interface)
          << (InvalidDecl-1) << "";
      return nullptr;
    }
  }

  // C++ 9.2p6: A member shall not be declared to have automatic storage
  // duration (auto, register) or with the extern storage-class-specifier.
  // C++ 7.1.1p8: The mutable specifier can be applied only to names of class
  // data members and cannot be applied to names declared const or static,
  // and cannot be applied to reference members.
  switch (DS.getStorageClassSpec()) {
  case DeclSpec::SCS_unspecified:
  case DeclSpec::SCS_typedef:
  case DeclSpec::SCS_static:
    break;
  case DeclSpec::SCS_mutable:
    if (isFunc) {
      Diag(DS.getStorageClassSpecLoc(), diag::err_mutable_function);

      // FIXME: It would be nicer if the keyword was ignored only for this
      // declarator. Otherwise we could get follow-up errors.
      D.getMutableDeclSpec().ClearStorageClassSpecs();
    }
    break;
  default:
    Diag(DS.getStorageClassSpecLoc(),
         diag::err_storageclass_invalid_for_member);
    D.getMutableDeclSpec().ClearStorageClassSpecs();
    break;
  }

  bool isInstField = ((DS.getStorageClassSpec() == DeclSpec::SCS_unspecified ||
                       DS.getStorageClassSpec() == DeclSpec::SCS_mutable) &&
                      !isFunc);

  if (DS.isConstexprSpecified() && isInstField) {
    SemaDiagnosticBuilder B =
        Diag(DS.getConstexprSpecLoc(), diag::err_invalid_constexpr_member);
    SourceLocation ConstexprLoc = DS.getConstexprSpecLoc();
    if (InitStyle == ICIS_NoInit) {
      B << 0 << 0;
      if (D.getDeclSpec().getTypeQualifiers() & DeclSpec::TQ_const)
        B << FixItHint::CreateRemoval(ConstexprLoc);
      else {
        B << FixItHint::CreateReplacement(ConstexprLoc, "const");
        D.getMutableDeclSpec().ClearConstexprSpec();
        const char *PrevSpec;
        unsigned DiagID;
        bool Failed = D.getMutableDeclSpec().SetTypeQual(
            DeclSpec::TQ_const, ConstexprLoc, PrevSpec, DiagID, getLangOpts());
        (void)Failed;
        assert(!Failed && "Making a constexpr member const shouldn't fail");
      }
    } else {
      B << 1;
      const char *PrevSpec;
      unsigned DiagID;
      if (D.getMutableDeclSpec().SetStorageClassSpec(
          *this, DeclSpec::SCS_static, ConstexprLoc, PrevSpec, DiagID,
          Context.getPrintingPolicy())) {
        assert(DS.getStorageClassSpec() == DeclSpec::SCS_mutable &&
               "This is the only DeclSpec that should fail to be applied");
        B << 1;
      } else {
        B << 0 << FixItHint::CreateInsertion(ConstexprLoc, "static ");
        isInstField = false;
      }
    }
  }

  NamedDecl *Member;
  if (isInstField) {
    CXXScopeSpec &SS = D.getCXXScopeSpec();

    // Data members must have identifiers for names.
    if (!Name.isIdentifier()) {
      Diag(Loc, diag::err_bad_variable_name)
        << Name;
      return nullptr;
    }

    IdentifierInfo *II = Name.getAsIdentifierInfo();

    // Member field could not be with "template" keyword.
    // So TemplateParameterLists should be empty in this case.
    if (TemplateParameterLists.size()) {
      TemplateParameterList* TemplateParams = TemplateParameterLists[0];
      if (TemplateParams->size()) {
        // There is no such thing as a member field template.
        Diag(D.getIdentifierLoc(), diag::err_template_member)
            << II
            << SourceRange(TemplateParams->getTemplateLoc(),
                TemplateParams->getRAngleLoc());
      } else {
        // There is an extraneous 'template<>' for this member.
        Diag(TemplateParams->getTemplateLoc(),
            diag::err_template_member_noparams)
            << II
            << SourceRange(TemplateParams->getTemplateLoc(),
                TemplateParams->getRAngleLoc());
      }
      return nullptr;
    }

    if (SS.isSet() && !SS.isInvalid()) {
      // The user provided a superfluous scope specifier inside a class
      // definition:
      //
      // class X {
      //   int X::member;
      // };
      if (DeclContext *DC = computeDeclContext(SS, false))
        diagnoseQualifiedDeclaration(SS, DC, Name, D.getIdentifierLoc(),
                                     D.getName().getKind() ==
                                         UnqualifiedIdKind::IK_TemplateId);
      else
        Diag(D.getIdentifierLoc(), diag::err_member_qualification)
          << Name << SS.getRange();

      SS.clear();
    }

    if (MSPropertyAttr) {
      Member = HandleMSProperty(S, cast<CXXRecordDecl>(CurContext), Loc, D,
                                BitWidth, InitStyle, AS, *MSPropertyAttr);
      if (!Member)
        return nullptr;
      isInstField = false;
    } else {
      Member = HandleField(S, cast<CXXRecordDecl>(CurContext), Loc, D,
                                BitWidth, InitStyle, AS);
      if (!Member)
        return nullptr;
    }

    CheckShadowInheritedFields(Loc, Name, cast<CXXRecordDecl>(CurContext));
  } else {
    Member = HandleDeclarator(S, D, TemplateParameterLists);
    if (!Member)
      return nullptr;

    // Non-instance-fields can't have a bitfield.
    if (BitWidth) {
      if (Member->isInvalidDecl()) {
        // don't emit another diagnostic.
      } else if (isa<VarDecl>(Member) || isa<VarTemplateDecl>(Member)) {
        // C++ 9.6p3: A bit-field shall not be a static member.
        // "static member 'A' cannot be a bit-field"
        Diag(Loc, diag::err_static_not_bitfield)
          << Name << BitWidth->getSourceRange();
      } else if (isa<TypedefDecl>(Member)) {
        // "typedef member 'x' cannot be a bit-field"
        Diag(Loc, diag::err_typedef_not_bitfield)
          << Name << BitWidth->getSourceRange();
      } else {
        // A function typedef ("typedef int f(); f a;").
        // C++ 9.6p3: A bit-field shall have integral or enumeration type.
        Diag(Loc, diag::err_not_integral_type_bitfield)
          << Name << cast<ValueDecl>(Member)->getType()
          << BitWidth->getSourceRange();
      }

      BitWidth = nullptr;
      Member->setInvalidDecl();
    }

    NamedDecl *NonTemplateMember = Member;
    if (FunctionTemplateDecl *FunTmpl = dyn_cast<FunctionTemplateDecl>(Member))
      NonTemplateMember = FunTmpl->getTemplatedDecl();
    else if (VarTemplateDecl *VarTmpl = dyn_cast<VarTemplateDecl>(Member))
      NonTemplateMember = VarTmpl->getTemplatedDecl();

    Member->setAccess(AS);

    // If we have declared a member function template or static data member
    // template, set the access of the templated declaration as well.
    if (NonTemplateMember != Member)
      NonTemplateMember->setAccess(AS);

    // C++ [temp.deduct.guide]p3:
    //   A deduction guide [...] for a member class template [shall be
    //   declared] with the same access [as the template].
    if (auto *DG = dyn_cast<CXXDeductionGuideDecl>(NonTemplateMember)) {
      auto *TD = DG->getDeducedTemplate();
      if (AS != TD->getAccess()) {
        Diag(DG->getBeginLoc(), diag::err_deduction_guide_wrong_access);
        Diag(TD->getBeginLoc(), diag::note_deduction_guide_template_access)
            << TD->getAccess();
        const AccessSpecDecl *LastAccessSpec = nullptr;
        for (const auto *D : cast<CXXRecordDecl>(CurContext)->decls()) {
          if (const auto *AccessSpec = dyn_cast<AccessSpecDecl>(D))
            LastAccessSpec = AccessSpec;
        }
        assert(LastAccessSpec && "differing access with no access specifier");
        Diag(LastAccessSpec->getBeginLoc(), diag::note_deduction_guide_access)
            << AS;
      }
    }
  }

  if (VS.isOverrideSpecified())
    Member->addAttr(new (Context) OverrideAttr(VS.getOverrideLoc(), Context, 0));
  if (VS.isFinalSpecified())
    Member->addAttr(new (Context) FinalAttr(VS.getFinalLoc(), Context,
                                            VS.isFinalSpelledSealed()));

  if (VS.getLastLocation().isValid()) {
    // Update the end location of a method that has a virt-specifiers.
    if (CXXMethodDecl *MD = dyn_cast_or_null<CXXMethodDecl>(Member))
      MD->setRangeEnd(VS.getLastLocation());
  }

  CheckOverrideControl(Member);

  assert((Name || isInstField) && "No identifier for non-field ?");

  if (isInstField) {
    FieldDecl *FD = cast<FieldDecl>(Member);
    FieldCollector->Add(FD);

    if (!Diags.isIgnored(diag::warn_unused_private_field, FD->getLocation())) {
      // Remember all explicit private FieldDecls that have a name, no side
      // effects and are not part of a dependent type declaration.
      if (!FD->isImplicit() && FD->getDeclName() &&
          FD->getAccess() == AS_private &&
          !FD->hasAttr<UnusedAttr>() &&
          !FD->getParent()->isDependentContext() &&
          !InitializationHasSideEffects(*FD))
        UnusedPrivateFields.insert(FD);
    }
  }

  return Member;
}

namespace {
  class UninitializedFieldVisitor
      : public EvaluatedExprVisitor<UninitializedFieldVisitor> {
    Sema &S;
    // List of Decls to generate a warning on.  Also remove Decls that become
    // initialized.
    llvm::SmallPtrSetImpl<ValueDecl*> &Decls;
    // List of base classes of the record.  Classes are removed after their
    // initializers.
    llvm::SmallPtrSetImpl<QualType> &BaseClasses;
    // Vector of decls to be removed from the Decl set prior to visiting the
    // nodes.  These Decls may have been initialized in the prior initializer.
    llvm::SmallVector<ValueDecl*, 4> DeclsToRemove;
    // If non-null, add a note to the warning pointing back to the constructor.
    const CXXConstructorDecl *Constructor;
    // Variables to hold state when processing an initializer list.  When
    // InitList is true, special case initialization of FieldDecls matching
    // InitListFieldDecl.
    bool InitList;
    FieldDecl *InitListFieldDecl;
    llvm::SmallVector<unsigned, 4> InitFieldIndex;

  public:
    typedef EvaluatedExprVisitor<UninitializedFieldVisitor> Inherited;
    UninitializedFieldVisitor(Sema &S,
                              llvm::SmallPtrSetImpl<ValueDecl*> &Decls,
                              llvm::SmallPtrSetImpl<QualType> &BaseClasses)
      : Inherited(S.Context), S(S), Decls(Decls), BaseClasses(BaseClasses),
        Constructor(nullptr), InitList(false), InitListFieldDecl(nullptr) {}

    // Returns true if the use of ME is not an uninitialized use.
    bool IsInitListMemberExprInitialized(MemberExpr *ME,
                                         bool CheckReferenceOnly) {
      llvm::SmallVector<FieldDecl*, 4> Fields;
      bool ReferenceField = false;
      while (ME) {
        FieldDecl *FD = dyn_cast<FieldDecl>(ME->getMemberDecl());
        if (!FD)
          return false;
        Fields.push_back(FD);
        if (FD->getType()->isReferenceType())
          ReferenceField = true;
        ME = dyn_cast<MemberExpr>(ME->getBase()->IgnoreParenImpCasts());
      }

      // Binding a reference to an uninitialized field is not an
      // uninitialized use.
      if (CheckReferenceOnly && !ReferenceField)
        return true;

      llvm::SmallVector<unsigned, 4> UsedFieldIndex;
      // Discard the first field since it is the field decl that is being
      // initialized.
      for (auto I = Fields.rbegin() + 1, E = Fields.rend(); I != E; ++I) {
        UsedFieldIndex.push_back((*I)->getFieldIndex());
      }

      for (auto UsedIter = UsedFieldIndex.begin(),
                UsedEnd = UsedFieldIndex.end(),
                OrigIter = InitFieldIndex.begin(),
                OrigEnd = InitFieldIndex.end();
           UsedIter != UsedEnd && OrigIter != OrigEnd; ++UsedIter, ++OrigIter) {
        if (*UsedIter < *OrigIter)
          return true;
        if (*UsedIter > *OrigIter)
          break;
      }

      return false;
    }

    void HandleMemberExpr(MemberExpr *ME, bool CheckReferenceOnly,
                          bool AddressOf) {
      if (isa<EnumConstantDecl>(ME->getMemberDecl()))
        return;

      // FieldME is the inner-most MemberExpr that is not an anonymous struct
      // or union.
      MemberExpr *FieldME = ME;

      bool AllPODFields = FieldME->getType().isPODType(S.Context);

      Expr *Base = ME;
      while (MemberExpr *SubME =
                 dyn_cast<MemberExpr>(Base->IgnoreParenImpCasts())) {

        if (isa<VarDecl>(SubME->getMemberDecl()))
          return;

        if (FieldDecl *FD = dyn_cast<FieldDecl>(SubME->getMemberDecl()))
          if (!FD->isAnonymousStructOrUnion())
            FieldME = SubME;

        if (!FieldME->getType().isPODType(S.Context))
          AllPODFields = false;

        Base = SubME->getBase();
      }

      if (!isa<CXXThisExpr>(Base->IgnoreParenImpCasts()))
        return;

      if (AddressOf && AllPODFields)
        return;

      ValueDecl* FoundVD = FieldME->getMemberDecl();

      if (ImplicitCastExpr *BaseCast = dyn_cast<ImplicitCastExpr>(Base)) {
        while (isa<ImplicitCastExpr>(BaseCast->getSubExpr())) {
          BaseCast = cast<ImplicitCastExpr>(BaseCast->getSubExpr());
        }

        if (BaseCast->getCastKind() == CK_UncheckedDerivedToBase) {
          QualType T = BaseCast->getType();
          if (T->isPointerType() &&
              BaseClasses.count(T->getPointeeType())) {
            S.Diag(FieldME->getExprLoc(), diag::warn_base_class_is_uninit)
                << T->getPointeeType() << FoundVD;
          }
        }
      }

      if (!Decls.count(FoundVD))
        return;

      const bool IsReference = FoundVD->getType()->isReferenceType();

      if (InitList && !AddressOf && FoundVD == InitListFieldDecl) {
        // Special checking for initializer lists.
        if (IsInitListMemberExprInitialized(ME, CheckReferenceOnly)) {
          return;
        }
      } else {
        // Prevent double warnings on use of unbounded references.
        if (CheckReferenceOnly && !IsReference)
          return;
      }

      unsigned diag = IsReference
          ? diag::warn_reference_field_is_uninit
          : diag::warn_field_is_uninit;
      S.Diag(FieldME->getExprLoc(), diag) << FoundVD;
      if (Constructor)
        S.Diag(Constructor->getLocation(),
               diag::note_uninit_in_this_constructor)
          << (Constructor->isDefaultConstructor() && Constructor->isImplicit());

    }

    void HandleValue(Expr *E, bool AddressOf) {
      E = E->IgnoreParens();

      if (MemberExpr *ME = dyn_cast<MemberExpr>(E)) {
        HandleMemberExpr(ME, false /*CheckReferenceOnly*/,
                         AddressOf /*AddressOf*/);
        return;
      }

      if (ConditionalOperator *CO = dyn_cast<ConditionalOperator>(E)) {
        Visit(CO->getCond());
        HandleValue(CO->getTrueExpr(), AddressOf);
        HandleValue(CO->getFalseExpr(), AddressOf);
        return;
      }

      if (BinaryConditionalOperator *BCO =
              dyn_cast<BinaryConditionalOperator>(E)) {
        Visit(BCO->getCond());
        HandleValue(BCO->getFalseExpr(), AddressOf);
        return;
      }

      if (OpaqueValueExpr *OVE = dyn_cast<OpaqueValueExpr>(E)) {
        HandleValue(OVE->getSourceExpr(), AddressOf);
        return;
      }

      if (BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
        switch (BO->getOpcode()) {
        default:
          break;
        case(BO_PtrMemD):
        case(BO_PtrMemI):
          HandleValue(BO->getLHS(), AddressOf);
          Visit(BO->getRHS());
          return;
        case(BO_Comma):
          Visit(BO->getLHS());
          HandleValue(BO->getRHS(), AddressOf);
          return;
        }
      }

      Visit(E);
    }

    void CheckInitListExpr(InitListExpr *ILE) {
      InitFieldIndex.push_back(0);
      for (auto Child : ILE->children()) {
        if (InitListExpr *SubList = dyn_cast<InitListExpr>(Child)) {
          CheckInitListExpr(SubList);
        } else {
          Visit(Child);
        }
        ++InitFieldIndex.back();
      }
      InitFieldIndex.pop_back();
    }

    void CheckInitializer(Expr *E, const CXXConstructorDecl *FieldConstructor,
                          FieldDecl *Field, const Type *BaseClass) {
      // Remove Decls that may have been initialized in the previous
      // initializer.
      for (ValueDecl* VD : DeclsToRemove)
        Decls.erase(VD);
      DeclsToRemove.clear();

      Constructor = FieldConstructor;
      InitListExpr *ILE = dyn_cast<InitListExpr>(E);

      if (ILE && Field) {
        InitList = true;
        InitListFieldDecl = Field;
        InitFieldIndex.clear();
        CheckInitListExpr(ILE);
      } else {
        InitList = false;
        Visit(E);
      }

      if (Field)
        Decls.erase(Field);
      if (BaseClass)
        BaseClasses.erase(BaseClass->getCanonicalTypeInternal());
    }

    void VisitMemberExpr(MemberExpr *ME) {
      // All uses of unbounded reference fields will warn.
      HandleMemberExpr(ME, true /*CheckReferenceOnly*/, false /*AddressOf*/);
    }

    void VisitImplicitCastExpr(ImplicitCastExpr *E) {
      if (E->getCastKind() == CK_LValueToRValue) {
        HandleValue(E->getSubExpr(), false /*AddressOf*/);
        return;
      }

      Inherited::VisitImplicitCastExpr(E);
    }

    void VisitCXXConstructExpr(CXXConstructExpr *E) {
      if (E->getConstructor()->isCopyConstructor()) {
        Expr *ArgExpr = E->getArg(0);
        if (InitListExpr *ILE = dyn_cast<InitListExpr>(ArgExpr))
          if (ILE->getNumInits() == 1)
            ArgExpr = ILE->getInit(0);
        if (ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(ArgExpr))
          if (ICE->getCastKind() == CK_NoOp)
            ArgExpr = ICE->getSubExpr();
        HandleValue(ArgExpr, false /*AddressOf*/);
        return;
      }
      Inherited::VisitCXXConstructExpr(E);
    }

    void VisitCXXMemberCallExpr(CXXMemberCallExpr *E) {
      Expr *Callee = E->getCallee();
      if (isa<MemberExpr>(Callee)) {
        HandleValue(Callee, false /*AddressOf*/);
        for (auto Arg : E->arguments())
          Visit(Arg);
        return;
      }

      Inherited::VisitCXXMemberCallExpr(E);
    }

    void VisitCallExpr(CallExpr *E) {
      // Treat std::move as a use.
      if (E->isCallToStdMove()) {
        HandleValue(E->getArg(0), /*AddressOf=*/false);
        return;
      }

      Inherited::VisitCallExpr(E);
    }

    void VisitCXXOperatorCallExpr(CXXOperatorCallExpr *E) {
      Expr *Callee = E->getCallee();

      if (isa<UnresolvedLookupExpr>(Callee))
        return Inherited::VisitCXXOperatorCallExpr(E);

      Visit(Callee);
      for (auto Arg : E->arguments())
        HandleValue(Arg->IgnoreParenImpCasts(), false /*AddressOf*/);
    }

    void VisitBinaryOperator(BinaryOperator *E) {
      // If a field assignment is detected, remove the field from the
      // uninitiailized field set.
      if (E->getOpcode() == BO_Assign)
        if (MemberExpr *ME = dyn_cast<MemberExpr>(E->getLHS()))
          if (FieldDecl *FD = dyn_cast<FieldDecl>(ME->getMemberDecl()))
            if (!FD->getType()->isReferenceType())
              DeclsToRemove.push_back(FD);

      if (E->isCompoundAssignmentOp()) {
        HandleValue(E->getLHS(), false /*AddressOf*/);
        Visit(E->getRHS());
        return;
      }

      Inherited::VisitBinaryOperator(E);
    }

    void VisitUnaryOperator(UnaryOperator *E) {
      if (E->isIncrementDecrementOp()) {
        HandleValue(E->getSubExpr(), false /*AddressOf*/);
        return;
      }
      if (E->getOpcode() == UO_AddrOf) {
        if (MemberExpr *ME = dyn_cast<MemberExpr>(E->getSubExpr())) {
          HandleValue(ME->getBase(), true /*AddressOf*/);
          return;
        }
      }

      Inherited::VisitUnaryOperator(E);
    }
  };

  // Diagnose value-uses of fields to initialize themselves, e.g.
  //   foo(foo)
  // where foo is not also a parameter to the constructor.
  // Also diagnose across field uninitialized use such as
  //   x(y), y(x)
  // TODO: implement -Wuninitialized and fold this into that framework.
  static void DiagnoseUninitializedFields(
      Sema &SemaRef, const CXXConstructorDecl *Constructor) {

    if (SemaRef.getDiagnostics().isIgnored(diag::warn_field_is_uninit,
                                           Constructor->getLocation())) {
      return;
    }

    if (Constructor->isInvalidDecl())
      return;

    const CXXRecordDecl *RD = Constructor->getParent();

    if (RD->getDescribedClassTemplate())
      return;

    // Holds fields that are uninitialized.
    llvm::SmallPtrSet<ValueDecl*, 4> UninitializedFields;

    // At the beginning, all fields are uninitialized.
    for (auto *I : RD->decls()) {
      if (auto *FD = dyn_cast<FieldDecl>(I)) {
        UninitializedFields.insert(FD);
      } else if (auto *IFD = dyn_cast<IndirectFieldDecl>(I)) {
        UninitializedFields.insert(IFD->getAnonField());
      }
    }

    llvm::SmallPtrSet<QualType, 4> UninitializedBaseClasses;
    for (auto I : RD->bases())
      UninitializedBaseClasses.insert(I.getType().getCanonicalType());

    if (UninitializedFields.empty() && UninitializedBaseClasses.empty())
      return;

    UninitializedFieldVisitor UninitializedChecker(SemaRef,
                                                   UninitializedFields,
                                                   UninitializedBaseClasses);

    for (const auto *FieldInit : Constructor->inits()) {
      if (UninitializedFields.empty() && UninitializedBaseClasses.empty())
        break;

      Expr *InitExpr = FieldInit->getInit();
      if (!InitExpr)
        continue;

      if (CXXDefaultInitExpr *Default =
              dyn_cast<CXXDefaultInitExpr>(InitExpr)) {
        InitExpr = Default->getExpr();
        if (!InitExpr)
          continue;
        // In class initializers will point to the constructor.
        UninitializedChecker.CheckInitializer(InitExpr, Constructor,
                                              FieldInit->getAnyMember(),
                                              FieldInit->getBaseClass());
      } else {
        UninitializedChecker.CheckInitializer(InitExpr, nullptr,
                                              FieldInit->getAnyMember(),
                                              FieldInit->getBaseClass());
      }
    }
  }
} // namespace

/// Enter a new C++ default initializer scope. After calling this, the
/// caller must call \ref ActOnFinishCXXInClassMemberInitializer, even if
/// parsing or instantiating the initializer failed.
void Sema::ActOnStartCXXInClassMemberInitializer() {
  // Create a synthetic function scope to represent the call to the constructor
  // that notionally surrounds a use of this initializer.
  PushFunctionScope();
}

/// This is invoked after parsing an in-class initializer for a
/// non-static C++ class member, and after instantiating an in-class initializer
/// in a class template. Such actions are deferred until the class is complete.
void Sema::ActOnFinishCXXInClassMemberInitializer(Decl *D,
                                                  SourceLocation InitLoc,
                                                  Expr *InitExpr) {
  // Pop the notional constructor scope we created earlier.
  PopFunctionScopeInfo(nullptr, D);

  FieldDecl *FD = dyn_cast<FieldDecl>(D);
  assert((isa<MSPropertyDecl>(D) || FD->getInClassInitStyle() != ICIS_NoInit) &&
         "must set init style when field is created");

  if (!InitExpr) {
    D->setInvalidDecl();
    if (FD)
      FD->removeInClassInitializer();
    return;
  }

  if (DiagnoseUnexpandedParameterPack(InitExpr, UPPC_Initializer)) {
    FD->setInvalidDecl();
    FD->removeInClassInitializer();
    return;
  }

  ExprResult Init = InitExpr;
  if (!FD->getType()->isDependentType() && !InitExpr->isTypeDependent()) {
    InitializedEntity Entity =
        InitializedEntity::InitializeMemberFromDefaultMemberInitializer(FD);
    InitializationKind Kind =
        FD->getInClassInitStyle() == ICIS_ListInit
            ? InitializationKind::CreateDirectList(InitExpr->getBeginLoc(),
                                                   InitExpr->getBeginLoc(),
                                                   InitExpr->getEndLoc())
            : InitializationKind::CreateCopy(InitExpr->getBeginLoc(), InitLoc);
    InitializationSequence Seq(*this, Entity, Kind, InitExpr);
    Init = Seq.Perform(*this, Entity, Kind, InitExpr);
    if (Init.isInvalid()) {
      FD->setInvalidDecl();
      return;
    }
  }

  // C++11 [class.base.init]p7:
  //   The initialization of each base and member constitutes a
  //   full-expression.
  Init = ActOnFinishFullExpr(Init.get(), InitLoc);
  if (Init.isInvalid()) {
    FD->setInvalidDecl();
    return;
  }

  InitExpr = Init.get();

  FD->setInClassInitializer(InitExpr);
}

/// Find the direct and/or virtual base specifiers that
/// correspond to the given base type, for use in base initialization
/// within a constructor.
static bool FindBaseInitializer(Sema &SemaRef,
                                CXXRecordDecl *ClassDecl,
                                QualType BaseType,
                                const CXXBaseSpecifier *&DirectBaseSpec,
                                const CXXBaseSpecifier *&VirtualBaseSpec) {
  // First, check for a direct base class.
  DirectBaseSpec = nullptr;
  for (const auto &Base : ClassDecl->bases()) {
    if (SemaRef.Context.hasSameUnqualifiedType(BaseType, Base.getType())) {
      // We found a direct base of this type. That's what we're
      // initializing.
      DirectBaseSpec = &Base;
      break;
    }
  }

  // Check for a virtual base class.
  // FIXME: We might be able to short-circuit this if we know in advance that
  // there are no virtual bases.
  VirtualBaseSpec = nullptr;
  if (!DirectBaseSpec || !DirectBaseSpec->isVirtual()) {
    // We haven't found a base yet; search the class hierarchy for a
    // virtual base class.
    CXXBasePaths Paths(/*FindAmbiguities=*/true, /*RecordPaths=*/true,
                       /*DetectVirtual=*/false);
    if (SemaRef.IsDerivedFrom(ClassDecl->getLocation(),
                              SemaRef.Context.getTypeDeclType(ClassDecl),
                              BaseType, Paths)) {
      for (CXXBasePaths::paths_iterator Path = Paths.begin();
           Path != Paths.end(); ++Path) {
        if (Path->back().Base->isVirtual()) {
          VirtualBaseSpec = Path->back().Base;
          break;
        }
      }
    }
  }

  return DirectBaseSpec || VirtualBaseSpec;
}

/// Handle a C++ member initializer using braced-init-list syntax.
MemInitResult
Sema::ActOnMemInitializer(Decl *ConstructorD,
                          Scope *S,
                          CXXScopeSpec &SS,
                          IdentifierInfo *MemberOrBase,
                          ParsedType TemplateTypeTy,
                          const DeclSpec &DS,
                          SourceLocation IdLoc,
                          Expr *InitList,
                          SourceLocation EllipsisLoc) {
  return BuildMemInitializer(ConstructorD, S, SS, MemberOrBase, TemplateTypeTy,
                             DS, IdLoc, InitList,
                             EllipsisLoc);
}

/// Handle a C++ member initializer using parentheses syntax.
MemInitResult
Sema::ActOnMemInitializer(Decl *ConstructorD,
                          Scope *S,
                          CXXScopeSpec &SS,
                          IdentifierInfo *MemberOrBase,
                          ParsedType TemplateTypeTy,
                          const DeclSpec &DS,
                          SourceLocation IdLoc,
                          SourceLocation LParenLoc,
                          ArrayRef<Expr *> Args,
                          SourceLocation RParenLoc,
                          SourceLocation EllipsisLoc) {
  Expr *List = ParenListExpr::Create(Context, LParenLoc, Args, RParenLoc);
  return BuildMemInitializer(ConstructorD, S, SS, MemberOrBase, TemplateTypeTy,
                             DS, IdLoc, List, EllipsisLoc);
}

namespace {

// Callback to only accept typo corrections that can be a valid C++ member
// intializer: either a non-static field member or a base class.
class MemInitializerValidatorCCC : public CorrectionCandidateCallback {
public:
  explicit MemInitializerValidatorCCC(CXXRecordDecl *ClassDecl)
      : ClassDecl(ClassDecl) {}

  bool ValidateCandidate(const TypoCorrection &candidate) override {
    if (NamedDecl *ND = candidate.getCorrectionDecl()) {
      if (FieldDecl *Member = dyn_cast<FieldDecl>(ND))
        return Member->getDeclContext()->getRedeclContext()->Equals(ClassDecl);
      return isa<TypeDecl>(ND);
    }
    return false;
  }

private:
  CXXRecordDecl *ClassDecl;
};

}

ValueDecl *Sema::tryLookupCtorInitMemberDecl(CXXRecordDecl *ClassDecl,
                                             CXXScopeSpec &SS,
                                             ParsedType TemplateTypeTy,
                                             IdentifierInfo *MemberOrBase) {
  if (SS.getScopeRep() || TemplateTypeTy)
    return nullptr;
  DeclContext::lookup_result Result = ClassDecl->lookup(MemberOrBase);
  if (Result.empty())
    return nullptr;
  ValueDecl *Member;
  if ((Member = dyn_cast<FieldDecl>(Result.front())) ||
      (Member = dyn_cast<IndirectFieldDecl>(Result.front())))
    return Member;
  return nullptr;
}

/// Handle a C++ member initializer.
MemInitResult
Sema::BuildMemInitializer(Decl *ConstructorD,
                          Scope *S,
                          CXXScopeSpec &SS,
                          IdentifierInfo *MemberOrBase,
                          ParsedType TemplateTypeTy,
                          const DeclSpec &DS,
                          SourceLocation IdLoc,
                          Expr *Init,
                          SourceLocation EllipsisLoc) {
  ExprResult Res = CorrectDelayedTyposInExpr(Init);
  if (!Res.isUsable())
    return true;
  Init = Res.get();

  if (!ConstructorD)
    return true;

  AdjustDeclIfTemplate(ConstructorD);

  CXXConstructorDecl *Constructor
    = dyn_cast<CXXConstructorDecl>(ConstructorD);
  if (!Constructor) {
    // The user wrote a constructor initializer on a function that is
    // not a C++ constructor. Ignore the error for now, because we may
    // have more member initializers coming; we'll diagnose it just
    // once in ActOnMemInitializers.
    return true;
  }

  CXXRecordDecl *ClassDecl = Constructor->getParent();

  // C++ [class.base.init]p2:
  //   Names in a mem-initializer-id are looked up in the scope of the
  //   constructor's class and, if not found in that scope, are looked
  //   up in the scope containing the constructor's definition.
  //   [Note: if the constructor's class contains a member with the
  //   same name as a direct or virtual base class of the class, a
  //   mem-initializer-id naming the member or base class and composed
  //   of a single identifier refers to the class member. A
  //   mem-initializer-id for the hidden base class may be specified
  //   using a qualified name. ]

  // Look for a member, first.
  if (ValueDecl *Member = tryLookupCtorInitMemberDecl(
          ClassDecl, SS, TemplateTypeTy, MemberOrBase)) {
    if (EllipsisLoc.isValid())
      Diag(EllipsisLoc, diag::err_pack_expansion_member_init)
          << MemberOrBase
          << SourceRange(IdLoc, Init->getSourceRange().getEnd());

    return BuildMemberInitializer(Member, Init, IdLoc);
  }
  // It didn't name a member, so see if it names a class.
  QualType BaseType;
  TypeSourceInfo *TInfo = nullptr;

  if (TemplateTypeTy) {
    BaseType = GetTypeFromParser(TemplateTypeTy, &TInfo);
  } else if (DS.getTypeSpecType() == TST_decltype) {
    BaseType = BuildDecltypeType(DS.getRepAsExpr(), DS.getTypeSpecTypeLoc());
  } else if (DS.getTypeSpecType() == TST_decltype_auto) {
    Diag(DS.getTypeSpecTypeLoc(), diag::err_decltype_auto_invalid);
    return true;
  } else {
    LookupResult R(*this, MemberOrBase, IdLoc, LookupOrdinaryName);
    LookupParsedName(R, S, &SS);

    TypeDecl *TyD = R.getAsSingle<TypeDecl>();
    if (!TyD) {
      if (R.isAmbiguous()) return true;

      // We don't want access-control diagnostics here.
      R.suppressDiagnostics();

      if (SS.isSet() && isDependentScopeSpecifier(SS)) {
        bool NotUnknownSpecialization = false;
        DeclContext *DC = computeDeclContext(SS, false);
        if (CXXRecordDecl *Record = dyn_cast_or_null<CXXRecordDecl>(DC))
          NotUnknownSpecialization = !Record->hasAnyDependentBases();

        if (!NotUnknownSpecialization) {
          // When the scope specifier can refer to a member of an unknown
          // specialization, we take it as a type name.
          BaseType = CheckTypenameType(ETK_None, SourceLocation(),
                                       SS.getWithLocInContext(Context),
                                       *MemberOrBase, IdLoc);
          if (BaseType.isNull())
            return true;

          TInfo = Context.CreateTypeSourceInfo(BaseType);
          DependentNameTypeLoc TL =
              TInfo->getTypeLoc().castAs<DependentNameTypeLoc>();
          if (!TL.isNull()) {
            TL.setNameLoc(IdLoc);
            TL.setElaboratedKeywordLoc(SourceLocation());
            TL.setQualifierLoc(SS.getWithLocInContext(Context));
          }

          R.clear();
          R.setLookupName(MemberOrBase);
        }
      }

      // If no results were found, try to correct typos.
      TypoCorrection Corr;
      if (R.empty() && BaseType.isNull() &&
          (Corr = CorrectTypo(
               R.getLookupNameInfo(), R.getLookupKind(), S, &SS,
               llvm::make_unique<MemInitializerValidatorCCC>(ClassDecl),
               CTK_ErrorRecovery, ClassDecl))) {
        if (FieldDecl *Member = Corr.getCorrectionDeclAs<FieldDecl>()) {
          // We have found a non-static data member with a similar
          // name to what was typed; complain and initialize that
          // member.
          diagnoseTypo(Corr,
                       PDiag(diag::err_mem_init_not_member_or_class_suggest)
                         << MemberOrBase << true);
          return BuildMemberInitializer(Member, Init, IdLoc);
        } else if (TypeDecl *Type = Corr.getCorrectionDeclAs<TypeDecl>()) {
          const CXXBaseSpecifier *DirectBaseSpec;
          const CXXBaseSpecifier *VirtualBaseSpec;
          if (FindBaseInitializer(*this, ClassDecl,
                                  Context.getTypeDeclType(Type),
                                  DirectBaseSpec, VirtualBaseSpec)) {
            // We have found a direct or virtual base class with a
            // similar name to what was typed; complain and initialize
            // that base class.
            diagnoseTypo(Corr,
                         PDiag(diag::err_mem_init_not_member_or_class_suggest)
                           << MemberOrBase << false,
                         PDiag() /*Suppress note, we provide our own.*/);

            const CXXBaseSpecifier *BaseSpec = DirectBaseSpec ? DirectBaseSpec
                                                              : VirtualBaseSpec;
            Diag(BaseSpec->getBeginLoc(), diag::note_base_class_specified_here)
                << BaseSpec->getType() << BaseSpec->getSourceRange();

            TyD = Type;
          }
        }
      }

      if (!TyD && BaseType.isNull()) {
        Diag(IdLoc, diag::err_mem_init_not_member_or_class)
          << MemberOrBase << SourceRange(IdLoc,Init->getSourceRange().getEnd());
        return true;
      }
    }

    if (BaseType.isNull()) {
      BaseType = Context.getTypeDeclType(TyD);
      MarkAnyDeclReferenced(TyD->getLocation(), TyD, /*OdrUse=*/false);
      if (SS.isSet()) {
        BaseType = Context.getElaboratedType(ETK_None, SS.getScopeRep(),
                                             BaseType);
        TInfo = Context.CreateTypeSourceInfo(BaseType);
        ElaboratedTypeLoc TL = TInfo->getTypeLoc().castAs<ElaboratedTypeLoc>();
        TL.getNamedTypeLoc().castAs<TypeSpecTypeLoc>().setNameLoc(IdLoc);
        TL.setElaboratedKeywordLoc(SourceLocation());
        TL.setQualifierLoc(SS.getWithLocInContext(Context));
      }
    }
  }

  if (!TInfo)
    TInfo = Context.getTrivialTypeSourceInfo(BaseType, IdLoc);

  return BuildBaseInitializer(BaseType, TInfo, Init, ClassDecl, EllipsisLoc);
}

MemInitResult
Sema::BuildMemberInitializer(ValueDecl *Member, Expr *Init,
                             SourceLocation IdLoc) {
  FieldDecl *DirectMember = dyn_cast<FieldDecl>(Member);
  IndirectFieldDecl *IndirectMember = dyn_cast<IndirectFieldDecl>(Member);
  assert((DirectMember || IndirectMember) &&
         "Member must be a FieldDecl or IndirectFieldDecl");

  if (DiagnoseUnexpandedParameterPack(Init, UPPC_Initializer))
    return true;

  if (Member->isInvalidDecl())
    return true;

  MultiExprArg Args;
  if (ParenListExpr *ParenList = dyn_cast<ParenListExpr>(Init)) {
    Args = MultiExprArg(ParenList->getExprs(), ParenList->getNumExprs());
  } else if (InitListExpr *InitList = dyn_cast<InitListExpr>(Init)) {
    Args = MultiExprArg(InitList->getInits(), InitList->getNumInits());
  } else {
    // Template instantiation doesn't reconstruct ParenListExprs for us.
    Args = Init;
  }

  SourceRange InitRange = Init->getSourceRange();

  if (Member->getType()->isDependentType() || Init->isTypeDependent()) {
    // Can't check initialization for a member of dependent type or when
    // any of the arguments are type-dependent expressions.
    DiscardCleanupsInEvaluationContext();
  } else {
    bool InitList = false;
    if (isa<InitListExpr>(Init)) {
      InitList = true;
      Args = Init;
    }

    // Initialize the member.
    InitializedEntity MemberEntity =
      DirectMember ? InitializedEntity::InitializeMember(DirectMember, nullptr)
                   : InitializedEntity::InitializeMember(IndirectMember,
                                                         nullptr);
    InitializationKind Kind =
        InitList ? InitializationKind::CreateDirectList(
                       IdLoc, Init->getBeginLoc(), Init->getEndLoc())
                 : InitializationKind::CreateDirect(IdLoc, InitRange.getBegin(),
                                                    InitRange.getEnd());

    InitializationSequence InitSeq(*this, MemberEntity, Kind, Args);
    ExprResult MemberInit = InitSeq.Perform(*this, MemberEntity, Kind, Args,
                                            nullptr);
    if (MemberInit.isInvalid())
      return true;

    // C++11 [class.base.init]p7:
    //   The initialization of each base and member constitutes a
    //   full-expression.
    MemberInit = ActOnFinishFullExpr(MemberInit.get(), InitRange.getBegin());
    if (MemberInit.isInvalid())
      return true;

    Init = MemberInit.get();
  }

  if (DirectMember) {
    return new (Context) CXXCtorInitializer(Context, DirectMember, IdLoc,
                                            InitRange.getBegin(), Init,
                                            InitRange.getEnd());
  } else {
    return new (Context) CXXCtorInitializer(Context, IndirectMember, IdLoc,
                                            InitRange.getBegin(), Init,
                                            InitRange.getEnd());
  }
}

MemInitResult
Sema::BuildDelegatingInitializer(TypeSourceInfo *TInfo, Expr *Init,
                                 CXXRecordDecl *ClassDecl) {
  SourceLocation NameLoc = TInfo->getTypeLoc().getLocalSourceRange().getBegin();
  if (!LangOpts.CPlusPlus11)
    return Diag(NameLoc, diag::err_delegating_ctor)
      << TInfo->getTypeLoc().getLocalSourceRange();
  Diag(NameLoc, diag::warn_cxx98_compat_delegating_ctor);

  bool InitList = true;
  MultiExprArg Args = Init;
  if (ParenListExpr *ParenList = dyn_cast<ParenListExpr>(Init)) {
    InitList = false;
    Args = MultiExprArg(ParenList->getExprs(), ParenList->getNumExprs());
  }

  SourceRange InitRange = Init->getSourceRange();
  // Initialize the object.
  InitializedEntity DelegationEntity = InitializedEntity::InitializeDelegation(
                                     QualType(ClassDecl->getTypeForDecl(), 0));
  InitializationKind Kind =
      InitList ? InitializationKind::CreateDirectList(
                     NameLoc, Init->getBeginLoc(), Init->getEndLoc())
               : InitializationKind::CreateDirect(NameLoc, InitRange.getBegin(),
                                                  InitRange.getEnd());
  InitializationSequence InitSeq(*this, DelegationEntity, Kind, Args);
  ExprResult DelegationInit = InitSeq.Perform(*this, DelegationEntity, Kind,
                                              Args, nullptr);
  if (DelegationInit.isInvalid())
    return true;

  assert(cast<CXXConstructExpr>(DelegationInit.get())->getConstructor() &&
         "Delegating constructor with no target?");

  // C++11 [class.base.init]p7:
  //   The initialization of each base and member constitutes a
  //   full-expression.
  DelegationInit = ActOnFinishFullExpr(DelegationInit.get(),
                                       InitRange.getBegin());
  if (DelegationInit.isInvalid())
    return true;

  // If we are in a dependent context, template instantiation will
  // perform this type-checking again. Just save the arguments that we
  // received in a ParenListExpr.
  // FIXME: This isn't quite ideal, since our ASTs don't capture all
  // of the information that we have about the base
  // initializer. However, deconstructing the ASTs is a dicey process,
  // and this approach is far more likely to get the corner cases right.
  if (CurContext->isDependentContext())
    DelegationInit = Init;

  return new (Context) CXXCtorInitializer(Context, TInfo, InitRange.getBegin(),
                                          DelegationInit.getAs<Expr>(),
                                          InitRange.getEnd());
}

MemInitResult
Sema::BuildBaseInitializer(QualType BaseType, TypeSourceInfo *BaseTInfo,
                           Expr *Init, CXXRecordDecl *ClassDecl,
                           SourceLocation EllipsisLoc) {
  SourceLocation BaseLoc
    = BaseTInfo->getTypeLoc().getLocalSourceRange().getBegin();

  if (!BaseType->isDependentType() && !BaseType->isRecordType())
    return Diag(BaseLoc, diag::err_base_init_does_not_name_class)
             << BaseType << BaseTInfo->getTypeLoc().getLocalSourceRange();

  // C++ [class.base.init]p2:
  //   [...] Unless the mem-initializer-id names a nonstatic data
  //   member of the constructor's class or a direct or virtual base
  //   of that class, the mem-initializer is ill-formed. A
  //   mem-initializer-list can initialize a base class using any
  //   name that denotes that base class type.
  bool Dependent = BaseType->isDependentType() || Init->isTypeDependent();

  SourceRange InitRange = Init->getSourceRange();
  if (EllipsisLoc.isValid()) {
    // This is a pack expansion.
    if (!BaseType->containsUnexpandedParameterPack())  {
      Diag(EllipsisLoc, diag::err_pack_expansion_without_parameter_packs)
        << SourceRange(BaseLoc, InitRange.getEnd());

      EllipsisLoc = SourceLocation();
    }
  } else {
    // Check for any unexpanded parameter packs.
    if (DiagnoseUnexpandedParameterPack(BaseLoc, BaseTInfo, UPPC_Initializer))
      return true;

    if (DiagnoseUnexpandedParameterPack(Init, UPPC_Initializer))
      return true;
  }

  // Check for direct and virtual base classes.
  const CXXBaseSpecifier *DirectBaseSpec = nullptr;
  const CXXBaseSpecifier *VirtualBaseSpec = nullptr;
  if (!Dependent) {
    if (Context.hasSameUnqualifiedType(QualType(ClassDecl->getTypeForDecl(),0),
                                       BaseType))
      return BuildDelegatingInitializer(BaseTInfo, Init, ClassDecl);

    FindBaseInitializer(*this, ClassDecl, BaseType, DirectBaseSpec,
                        VirtualBaseSpec);

    // C++ [base.class.init]p2:
    // Unless the mem-initializer-id names a nonstatic data member of the
    // constructor's class or a direct or virtual base of that class, the
    // mem-initializer is ill-formed.
    if (!DirectBaseSpec && !VirtualBaseSpec) {
      // If the class has any dependent bases, then it's possible that
      // one of those types will resolve to the same type as
      // BaseType. Therefore, just treat this as a dependent base
      // class initialization.  FIXME: Should we try to check the
      // initialization anyway? It seems odd.
      if (ClassDecl->hasAnyDependentBases())
        Dependent = true;
      else
        return Diag(BaseLoc, diag::err_not_direct_base_or_virtual)
          << BaseType << Context.getTypeDeclType(ClassDecl)
          << BaseTInfo->getTypeLoc().getLocalSourceRange();
    }
  }

  if (Dependent) {
    DiscardCleanupsInEvaluationContext();

    return new (Context) CXXCtorInitializer(Context, BaseTInfo,
                                            /*IsVirtual=*/false,
                                            InitRange.getBegin(), Init,
                                            InitRange.getEnd(), EllipsisLoc);
  }

  // C++ [base.class.init]p2:
  //   If a mem-initializer-id is ambiguous because it designates both
  //   a direct non-virtual base class and an inherited virtual base
  //   class, the mem-initializer is ill-formed.
  if (DirectBaseSpec && VirtualBaseSpec)
    return Diag(BaseLoc, diag::err_base_init_direct_and_virtual)
      << BaseType << BaseTInfo->getTypeLoc().getLocalSourceRange();

  const CXXBaseSpecifier *BaseSpec = DirectBaseSpec;
  if (!BaseSpec)
    BaseSpec = VirtualBaseSpec;

  // Initialize the base.
  bool InitList = true;
  MultiExprArg Args = Init;
  if (ParenListExpr *ParenList = dyn_cast<ParenListExpr>(Init)) {
    InitList = false;
    Args = MultiExprArg(ParenList->getExprs(), ParenList->getNumExprs());
  }

  InitializedEntity BaseEntity =
    InitializedEntity::InitializeBase(Context, BaseSpec, VirtualBaseSpec);
  InitializationKind Kind =
      InitList ? InitializationKind::CreateDirectList(BaseLoc)
               : InitializationKind::CreateDirect(BaseLoc, InitRange.getBegin(),
                                                  InitRange.getEnd());
  InitializationSequence InitSeq(*this, BaseEntity, Kind, Args);
  ExprResult BaseInit = InitSeq.Perform(*this, BaseEntity, Kind, Args, nullptr);
  if (BaseInit.isInvalid())
    return true;

  // C++11 [class.base.init]p7:
  //   The initialization of each base and member constitutes a
  //   full-expression.
  BaseInit = ActOnFinishFullExpr(BaseInit.get(), InitRange.getBegin());
  if (BaseInit.isInvalid())
    return true;

  // If we are in a dependent context, template instantiation will
  // perform this type-checking again. Just save the arguments that we
  // received in a ParenListExpr.
  // FIXME: This isn't quite ideal, since our ASTs don't capture all
  // of the information that we have about the base
  // initializer. However, deconstructing the ASTs is a dicey process,
  // and this approach is far more likely to get the corner cases right.
  if (CurContext->isDependentContext())
    BaseInit = Init;

  return new (Context) CXXCtorInitializer(Context, BaseTInfo,
                                          BaseSpec->isVirtual(),
                                          InitRange.getBegin(),
                                          BaseInit.getAs<Expr>(),
                                          InitRange.getEnd(), EllipsisLoc);
}

// Create a static_cast\<T&&>(expr).
static Expr *CastForMoving(Sema &SemaRef, Expr *E, QualType T = QualType()) {
  if (T.isNull()) T = E->getType();
  QualType TargetType = SemaRef.BuildReferenceType(
      T, /*SpelledAsLValue*/false, SourceLocation(), DeclarationName());
  SourceLocation ExprLoc = E->getBeginLoc();
  TypeSourceInfo *TargetLoc = SemaRef.Context.getTrivialTypeSourceInfo(
      TargetType, ExprLoc);

  return SemaRef.BuildCXXNamedCast(ExprLoc, tok::kw_static_cast, TargetLoc, E,
                                   SourceRange(ExprLoc, ExprLoc),
                                   E->getSourceRange()).get();
}

/// ImplicitInitializerKind - How an implicit base or member initializer should
/// initialize its base or member.
enum ImplicitInitializerKind {
  IIK_Default,
  IIK_Copy,
  IIK_Move,
  IIK_Inherit
};

static bool
BuildImplicitBaseInitializer(Sema &SemaRef, CXXConstructorDecl *Constructor,
                             ImplicitInitializerKind ImplicitInitKind,
                             CXXBaseSpecifier *BaseSpec,
                             bool IsInheritedVirtualBase,
                             CXXCtorInitializer *&CXXBaseInit) {
  InitializedEntity InitEntity
    = InitializedEntity::InitializeBase(SemaRef.Context, BaseSpec,
                                        IsInheritedVirtualBase);

  ExprResult BaseInit;

  switch (ImplicitInitKind) {
  case IIK_Inherit:
  case IIK_Default: {
    InitializationKind InitKind
      = InitializationKind::CreateDefault(Constructor->getLocation());
    InitializationSequence InitSeq(SemaRef, InitEntity, InitKind, None);
    BaseInit = InitSeq.Perform(SemaRef, InitEntity, InitKind, None);
    break;
  }

  case IIK_Move:
  case IIK_Copy: {
    bool Moving = ImplicitInitKind == IIK_Move;
    ParmVarDecl *Param = Constructor->getParamDecl(0);
    QualType ParamType = Param->getType().getNonReferenceType();

    Expr *CopyCtorArg =
      DeclRefExpr::Create(SemaRef.Context, NestedNameSpecifierLoc(),
                          SourceLocation(), Param, false,
                          Constructor->getLocation(), ParamType,
                          VK_LValue, nullptr);

    SemaRef.MarkDeclRefReferenced(cast<DeclRefExpr>(CopyCtorArg));

    // Cast to the base class to avoid ambiguities.
    QualType ArgTy =
      SemaRef.Context.getQualifiedType(BaseSpec->getType().getUnqualifiedType(),
                                       ParamType.getQualifiers());

    if (Moving) {
      CopyCtorArg = CastForMoving(SemaRef, CopyCtorArg);
    }

    CXXCastPath BasePath;
    BasePath.push_back(BaseSpec);
    CopyCtorArg = SemaRef.ImpCastExprToType(CopyCtorArg, ArgTy,
                                            CK_UncheckedDerivedToBase,
                                            Moving ? VK_XValue : VK_LValue,
                                            &BasePath).get();

    InitializationKind InitKind
      = InitializationKind::CreateDirect(Constructor->getLocation(),
                                         SourceLocation(), SourceLocation());
    InitializationSequence InitSeq(SemaRef, InitEntity, InitKind, CopyCtorArg);
    BaseInit = InitSeq.Perform(SemaRef, InitEntity, InitKind, CopyCtorArg);
    break;
  }
  }

  BaseInit = SemaRef.MaybeCreateExprWithCleanups(BaseInit);
  if (BaseInit.isInvalid())
    return true;

  CXXBaseInit =
    new (SemaRef.Context) CXXCtorInitializer(SemaRef.Context,
               SemaRef.Context.getTrivialTypeSourceInfo(BaseSpec->getType(),
                                                        SourceLocation()),
                                             BaseSpec->isVirtual(),
                                             SourceLocation(),
                                             BaseInit.getAs<Expr>(),
                                             SourceLocation(),
                                             SourceLocation());

  return false;
}

static bool RefersToRValueRef(Expr *MemRef) {
  ValueDecl *Referenced = cast<MemberExpr>(MemRef)->getMemberDecl();
  return Referenced->getType()->isRValueReferenceType();
}

static bool
BuildImplicitMemberInitializer(Sema &SemaRef, CXXConstructorDecl *Constructor,
                               ImplicitInitializerKind ImplicitInitKind,
                               FieldDecl *Field, IndirectFieldDecl *Indirect,
                               CXXCtorInitializer *&CXXMemberInit) {
  if (Field->isInvalidDecl())
    return true;

  SourceLocation Loc = Constructor->getLocation();

  if (ImplicitInitKind == IIK_Copy || ImplicitInitKind == IIK_Move) {
    bool Moving = ImplicitInitKind == IIK_Move;
    ParmVarDecl *Param = Constructor->getParamDecl(0);
    QualType ParamType = Param->getType().getNonReferenceType();

    // Suppress copying zero-width bitfields.
    if (Field->isZeroLengthBitField(SemaRef.Context))
      return false;

    Expr *MemberExprBase =
      DeclRefExpr::Create(SemaRef.Context, NestedNameSpecifierLoc(),
                          SourceLocation(), Param, false,
                          Loc, ParamType, VK_LValue, nullptr);

    SemaRef.MarkDeclRefReferenced(cast<DeclRefExpr>(MemberExprBase));

    if (Moving) {
      MemberExprBase = CastForMoving(SemaRef, MemberExprBase);
    }

    // Build a reference to this field within the parameter.
    CXXScopeSpec SS;
    LookupResult MemberLookup(SemaRef, Field->getDeclName(), Loc,
                              Sema::LookupMemberName);
    MemberLookup.addDecl(Indirect ? cast<ValueDecl>(Indirect)
                                  : cast<ValueDecl>(Field), AS_public);
    MemberLookup.resolveKind();
    ExprResult CtorArg
      = SemaRef.BuildMemberReferenceExpr(MemberExprBase,
                                         ParamType, Loc,
                                         /*IsArrow=*/false,
                                         SS,
                                         /*TemplateKWLoc=*/SourceLocation(),
                                         /*FirstQualifierInScope=*/nullptr,
                                         MemberLookup,
                                         /*TemplateArgs=*/nullptr,
                                         /*S*/nullptr);
    if (CtorArg.isInvalid())
      return true;

    // C++11 [class.copy]p15:
    //   - if a member m has rvalue reference type T&&, it is direct-initialized
    //     with static_cast<T&&>(x.m);
    if (RefersToRValueRef(CtorArg.get())) {
      CtorArg = CastForMoving(SemaRef, CtorArg.get());
    }

    InitializedEntity Entity =
        Indirect ? InitializedEntity::InitializeMember(Indirect, nullptr,
                                                       /*Implicit*/ true)
                 : InitializedEntity::InitializeMember(Field, nullptr,
                                                       /*Implicit*/ true);

    // Direct-initialize to use the copy constructor.
    InitializationKind InitKind =
      InitializationKind::CreateDirect(Loc, SourceLocation(), SourceLocation());

    Expr *CtorArgE = CtorArg.getAs<Expr>();
    InitializationSequence InitSeq(SemaRef, Entity, InitKind, CtorArgE);
    ExprResult MemberInit =
        InitSeq.Perform(SemaRef, Entity, InitKind, MultiExprArg(&CtorArgE, 1));
    MemberInit = SemaRef.MaybeCreateExprWithCleanups(MemberInit);
    if (MemberInit.isInvalid())
      return true;

    if (Indirect)
      CXXMemberInit = new (SemaRef.Context) CXXCtorInitializer(
          SemaRef.Context, Indirect, Loc, Loc, MemberInit.getAs<Expr>(), Loc);
    else
      CXXMemberInit = new (SemaRef.Context) CXXCtorInitializer(
          SemaRef.Context, Field, Loc, Loc, MemberInit.getAs<Expr>(), Loc);
    return false;
  }

  assert((ImplicitInitKind == IIK_Default || ImplicitInitKind == IIK_Inherit) &&
         "Unhandled implicit init kind!");

  QualType FieldBaseElementType =
    SemaRef.Context.getBaseElementType(Field->getType());

  if (FieldBaseElementType->isRecordType()) {
    InitializedEntity InitEntity =
        Indirect ? InitializedEntity::InitializeMember(Indirect, nullptr,
                                                       /*Implicit*/ true)
                 : InitializedEntity::InitializeMember(Field, nullptr,
                                                       /*Implicit*/ true);
    InitializationKind InitKind =
      InitializationKind::CreateDefault(Loc);

    InitializationSequence InitSeq(SemaRef, InitEntity, InitKind, None);
    ExprResult MemberInit =
      InitSeq.Perform(SemaRef, InitEntity, InitKind, None);

    MemberInit = SemaRef.MaybeCreateExprWithCleanups(MemberInit);
    if (MemberInit.isInvalid())
      return true;

    if (Indirect)
      CXXMemberInit = new (SemaRef.Context) CXXCtorInitializer(SemaRef.Context,
                                                               Indirect, Loc,
                                                               Loc,
                                                               MemberInit.get(),
                                                               Loc);
    else
      CXXMemberInit = new (SemaRef.Context) CXXCtorInitializer(SemaRef.Context,
                                                               Field, Loc, Loc,
                                                               MemberInit.get(),
                                                               Loc);
    return false;
  }

  if (!Field->getParent()->isUnion()) {
    if (FieldBaseElementType->isReferenceType()) {
      SemaRef.Diag(Constructor->getLocation(),
                   diag::err_uninitialized_member_in_ctor)
      << (int)Constructor->isImplicit()
      << SemaRef.Context.getTagDeclType(Constructor->getParent())
      << 0 << Field->getDeclName();
      SemaRef.Diag(Field->getLocation(), diag::note_declared_at);
      return true;
    }

    if (FieldBaseElementType.isConstQualified()) {
      SemaRef.Diag(Constructor->getLocation(),
                   diag::err_uninitialized_member_in_ctor)
      << (int)Constructor->isImplicit()
      << SemaRef.Context.getTagDeclType(Constructor->getParent())
      << 1 << Field->getDeclName();
      SemaRef.Diag(Field->getLocation(), diag::note_declared_at);
      return true;
    }
  }

  if (FieldBaseElementType.hasNonTrivialObjCLifetime()) {
    // ARC and Weak:
    //   Default-initialize Objective-C pointers to NULL.
    CXXMemberInit
      = new (SemaRef.Context) CXXCtorInitializer(SemaRef.Context, Field,
                                                 Loc, Loc,
                 new (SemaRef.Context) ImplicitValueInitExpr(Field->getType()),
                                                 Loc);
    return false;
  }

  // Nothing to initialize.
  CXXMemberInit = nullptr;
  return false;
}

namespace {
struct BaseAndFieldInfo {
  Sema &S;
  CXXConstructorDecl *Ctor;
  bool AnyErrorsInInits;
  ImplicitInitializerKind IIK;
  llvm::DenseMap<const void *, CXXCtorInitializer*> AllBaseFields;
  SmallVector<CXXCtorInitializer*, 8> AllToInit;
  llvm::DenseMap<TagDecl*, FieldDecl*> ActiveUnionMember;

  BaseAndFieldInfo(Sema &S, CXXConstructorDecl *Ctor, bool ErrorsInInits)
    : S(S), Ctor(Ctor), AnyErrorsInInits(ErrorsInInits) {
    bool Generated = Ctor->isImplicit() || Ctor->isDefaulted();
    if (Ctor->getInheritedConstructor())
      IIK = IIK_Inherit;
    else if (Generated && Ctor->isCopyConstructor())
      IIK = IIK_Copy;
    else if (Generated && Ctor->isMoveConstructor())
      IIK = IIK_Move;
    else
      IIK = IIK_Default;
  }

  bool isImplicitCopyOrMove() const {
    switch (IIK) {
    case IIK_Copy:
    case IIK_Move:
      return true;

    case IIK_Default:
    case IIK_Inherit:
      return false;
    }

    llvm_unreachable("Invalid ImplicitInitializerKind!");
  }

  bool addFieldInitializer(CXXCtorInitializer *Init) {
    AllToInit.push_back(Init);

    // Check whether this initializer makes the field "used".
    if (Init->getInit()->HasSideEffects(S.Context))
      S.UnusedPrivateFields.remove(Init->getAnyMember());

    return false;
  }

  bool isInactiveUnionMember(FieldDecl *Field) {
    RecordDecl *Record = Field->getParent();
    if (!Record->isUnion())
      return false;

    if (FieldDecl *Active =
            ActiveUnionMember.lookup(Record->getCanonicalDecl()))
      return Active != Field->getCanonicalDecl();

    // In an implicit copy or move constructor, ignore any in-class initializer.
    if (isImplicitCopyOrMove())
      return true;

    // If there's no explicit initialization, the field is active only if it
    // has an in-class initializer...
    if (Field->hasInClassInitializer())
      return false;
    // ... or it's an anonymous struct or union whose class has an in-class
    // initializer.
    if (!Field->isAnonymousStructOrUnion())
      return true;
    CXXRecordDecl *FieldRD = Field->getType()->getAsCXXRecordDecl();
    return !FieldRD->hasInClassInitializer();
  }

  /// Determine whether the given field is, or is within, a union member
  /// that is inactive (because there was an initializer given for a different
  /// member of the union, or because the union was not initialized at all).
  bool isWithinInactiveUnionMember(FieldDecl *Field,
                                   IndirectFieldDecl *Indirect) {
    if (!Indirect)
      return isInactiveUnionMember(Field);

    for (auto *C : Indirect->chain()) {
      FieldDecl *Field = dyn_cast<FieldDecl>(C);
      if (Field && isInactiveUnionMember(Field))
        return true;
    }
    return false;
  }
};
}

/// Determine whether the given type is an incomplete or zero-lenfgth
/// array type.
static bool isIncompleteOrZeroLengthArrayType(ASTContext &Context, QualType T) {
  if (T->isIncompleteArrayType())
    return true;

  while (const ConstantArrayType *ArrayT = Context.getAsConstantArrayType(T)) {
    if (!ArrayT->getSize())
      return true;

    T = ArrayT->getElementType();
  }

  return false;
}

static bool CollectFieldInitializer(Sema &SemaRef, BaseAndFieldInfo &Info,
                                    FieldDecl *Field,
                                    IndirectFieldDecl *Indirect = nullptr) {
  if (Field->isInvalidDecl())
    return false;

  // Overwhelmingly common case: we have a direct initializer for this field.
  if (CXXCtorInitializer *Init =
          Info.AllBaseFields.lookup(Field->getCanonicalDecl()))
    return Info.addFieldInitializer(Init);

  // C++11 [class.base.init]p8:
  //   if the entity is a non-static data member that has a
  //   brace-or-equal-initializer and either
  //   -- the constructor's class is a union and no other variant member of that
  //      union is designated by a mem-initializer-id or
  //   -- the constructor's class is not a union, and, if the entity is a member
  //      of an anonymous union, no other member of that union is designated by
  //      a mem-initializer-id,
  //   the entity is initialized as specified in [dcl.init].
  //
  // We also apply the same rules to handle anonymous structs within anonymous
  // unions.
  if (Info.isWithinInactiveUnionMember(Field, Indirect))
    return false;

  if (Field->hasInClassInitializer() && !Info.isImplicitCopyOrMove()) {
    ExprResult DIE =
        SemaRef.BuildCXXDefaultInitExpr(Info.Ctor->getLocation(), Field);
    if (DIE.isInvalid())
      return true;

    auto Entity = InitializedEntity::InitializeMember(Field, nullptr, true);
    SemaRef.checkInitializerLifetime(Entity, DIE.get());

    CXXCtorInitializer *Init;
    if (Indirect)
      Init = new (SemaRef.Context)
          CXXCtorInitializer(SemaRef.Context, Indirect, SourceLocation(),
                             SourceLocation(), DIE.get(), SourceLocation());
    else
      Init = new (SemaRef.Context)
          CXXCtorInitializer(SemaRef.Context, Field, SourceLocation(),
                             SourceLocation(), DIE.get(), SourceLocation());
    return Info.addFieldInitializer(Init);
  }

  // Don't initialize incomplete or zero-length arrays.
  if (isIncompleteOrZeroLengthArrayType(SemaRef.Context, Field->getType()))
    return false;

  // Don't try to build an implicit initializer if there were semantic
  // errors in any of the initializers (and therefore we might be
  // missing some that the user actually wrote).
  if (Info.AnyErrorsInInits)
    return false;

  CXXCtorInitializer *Init = nullptr;
  if (BuildImplicitMemberInitializer(Info.S, Info.Ctor, Info.IIK, Field,
                                     Indirect, Init))
    return true;

  if (!Init)
    return false;

  return Info.addFieldInitializer(Init);
}

bool
Sema::SetDelegatingInitializer(CXXConstructorDecl *Constructor,
                               CXXCtorInitializer *Initializer) {
  assert(Initializer->isDelegatingInitializer());
  Constructor->setNumCtorInitializers(1);
  CXXCtorInitializer **initializer =
    new (Context) CXXCtorInitializer*[1];
  memcpy(initializer, &Initializer, sizeof (CXXCtorInitializer*));
  Constructor->setCtorInitializers(initializer);

  if (CXXDestructorDecl *Dtor = LookupDestructor(Constructor->getParent())) {
    MarkFunctionReferenced(Initializer->getSourceLocation(), Dtor);
    DiagnoseUseOfDecl(Dtor, Initializer->getSourceLocation());
  }

  DelegatingCtorDecls.push_back(Constructor);

  DiagnoseUninitializedFields(*this, Constructor);

  return false;
}

bool Sema::SetCtorInitializers(CXXConstructorDecl *Constructor, bool AnyErrors,
                               ArrayRef<CXXCtorInitializer *> Initializers) {
  if (Constructor->isDependentContext()) {
    // Just store the initializers as written, they will be checked during
    // instantiation.
    if (!Initializers.empty()) {
      Constructor->setNumCtorInitializers(Initializers.size());
      CXXCtorInitializer **baseOrMemberInitializers =
        new (Context) CXXCtorInitializer*[Initializers.size()];
      memcpy(baseOrMemberInitializers, Initializers.data(),
             Initializers.size() * sizeof(CXXCtorInitializer*));
      Constructor->setCtorInitializers(baseOrMemberInitializers);
    }

    // Let template instantiation know whether we had errors.
    if (AnyErrors)
      Constructor->setInvalidDecl();

    return false;
  }

  BaseAndFieldInfo Info(*this, Constructor, AnyErrors);

  // We need to build the initializer AST according to order of construction
  // and not what user specified in the Initializers list.
  CXXRecordDecl *ClassDecl = Constructor->getParent()->getDefinition();
  if (!ClassDecl)
    return true;

  bool HadError = false;

  for (unsigned i = 0; i < Initializers.size(); i++) {
    CXXCtorInitializer *Member = Initializers[i];

    if (Member->isBaseInitializer())
      Info.AllBaseFields[Member->getBaseClass()->getAs<RecordType>()] = Member;
    else {
      Info.AllBaseFields[Member->getAnyMember()->getCanonicalDecl()] = Member;

      if (IndirectFieldDecl *F = Member->getIndirectMember()) {
        for (auto *C : F->chain()) {
          FieldDecl *FD = dyn_cast<FieldDecl>(C);
          if (FD && FD->getParent()->isUnion())
            Info.ActiveUnionMember.insert(std::make_pair(
                FD->getParent()->getCanonicalDecl(), FD->getCanonicalDecl()));
        }
      } else if (FieldDecl *FD = Member->getMember()) {
        if (FD->getParent()->isUnion())
          Info.ActiveUnionMember.insert(std::make_pair(
              FD->getParent()->getCanonicalDecl(), FD->getCanonicalDecl()));
      }
    }
  }

  // Keep track of the direct virtual bases.
  llvm::SmallPtrSet<CXXBaseSpecifier *, 16> DirectVBases;
  for (auto &I : ClassDecl->bases()) {
    if (I.isVirtual())
      DirectVBases.insert(&I);
  }

  // Push virtual bases before others.
  for (auto &VBase : ClassDecl->vbases()) {
    if (CXXCtorInitializer *Value
        = Info.AllBaseFields.lookup(VBase.getType()->getAs<RecordType>())) {
      // [class.base.init]p7, per DR257:
      //   A mem-initializer where the mem-initializer-id names a virtual base
      //   class is ignored during execution of a constructor of any class that
      //   is not the most derived class.
      if (ClassDecl->isAbstract()) {
        // FIXME: Provide a fixit to remove the base specifier. This requires
        // tracking the location of the associated comma for a base specifier.
        Diag(Value->getSourceLocation(), diag::warn_abstract_vbase_init_ignored)
          << VBase.getType() << ClassDecl;
        DiagnoseAbstractType(ClassDecl);
      }

      Info.AllToInit.push_back(Value);
    } else if (!AnyErrors && !ClassDecl->isAbstract()) {
      // [class.base.init]p8, per DR257:
      //   If a given [...] base class is not named by a mem-initializer-id
      //   [...] and the entity is not a virtual base class of an abstract
      //   class, then [...] the entity is default-initialized.
      bool IsInheritedVirtualBase = !DirectVBases.count(&VBase);
      CXXCtorInitializer *CXXBaseInit;
      if (BuildImplicitBaseInitializer(*this, Constructor, Info.IIK,
                                       &VBase, IsInheritedVirtualBase,
                                       CXXBaseInit)) {
        HadError = true;
        continue;
      }

      Info.AllToInit.push_back(CXXBaseInit);
    }
  }

  // Non-virtual bases.
  for (auto &Base : ClassDecl->bases()) {
    // Virtuals are in the virtual base list and already constructed.
    if (Base.isVirtual())
      continue;

    if (CXXCtorInitializer *Value
          = Info.AllBaseFields.lookup(Base.getType()->getAs<RecordType>())) {
      Info.AllToInit.push_back(Value);
    } else if (!AnyErrors) {
      CXXCtorInitializer *CXXBaseInit;
      if (BuildImplicitBaseInitializer(*this, Constructor, Info.IIK,
                                       &Base, /*IsInheritedVirtualBase=*/false,
                                       CXXBaseInit)) {
        HadError = true;
        continue;
      }

      Info.AllToInit.push_back(CXXBaseInit);
    }
  }

  // Fields.
  for (auto *Mem : ClassDecl->decls()) {
    if (auto *F = dyn_cast<FieldDecl>(Mem)) {
      // C++ [class.bit]p2:
      //   A declaration for a bit-field that omits the identifier declares an
      //   unnamed bit-field. Unnamed bit-fields are not members and cannot be
      //   initialized.
      if (F->isUnnamedBitfield())
        continue;

      // If we're not generating the implicit copy/move constructor, then we'll
      // handle anonymous struct/union fields based on their individual
      // indirect fields.
      if (F->isAnonymousStructOrUnion() && !Info.isImplicitCopyOrMove())
        continue;

      if (CollectFieldInitializer(*this, Info, F))
        HadError = true;
      continue;
    }

    // Beyond this point, we only consider default initialization.
    if (Info.isImplicitCopyOrMove())
      continue;

    if (auto *F = dyn_cast<IndirectFieldDecl>(Mem)) {
      if (F->getType()->isIncompleteArrayType()) {
        assert(ClassDecl->hasFlexibleArrayMember() &&
               "Incomplete array type is not valid");
        continue;
      }

      // Initialize each field of an anonymous struct individually.
      if (CollectFieldInitializer(*this, Info, F->getAnonField(), F))
        HadError = true;

      continue;
    }
  }

  unsigned NumInitializers = Info.AllToInit.size();
  if (NumInitializers > 0) {
    Constructor->setNumCtorInitializers(NumInitializers);
    CXXCtorInitializer **baseOrMemberInitializers =
      new (Context) CXXCtorInitializer*[NumInitializers];
    memcpy(baseOrMemberInitializers, Info.AllToInit.data(),
           NumInitializers * sizeof(CXXCtorInitializer*));
    Constructor->setCtorInitializers(baseOrMemberInitializers);

    // Constructors implicitly reference the base and member
    // destructors.
    MarkBaseAndMemberDestructorsReferenced(Constructor->getLocation(),
                                           Constructor->getParent());
  }

  return HadError;
}

static void PopulateKeysForFields(FieldDecl *Field, SmallVectorImpl<const void*> &IdealInits) {
  if (const RecordType *RT = Field->getType()->getAs<RecordType>()) {
    const RecordDecl *RD = RT->getDecl();
    if (RD->isAnonymousStructOrUnion()) {
      for (auto *Field : RD->fields())
        PopulateKeysForFields(Field, IdealInits);
      return;
    }
  }
  IdealInits.push_back(Field->getCanonicalDecl());
}

static const void *GetKeyForBase(ASTContext &Context, QualType BaseType) {
  return Context.getCanonicalType(BaseType).getTypePtr();
}

static const void *GetKeyForMember(ASTContext &Context,
                                   CXXCtorInitializer *Member) {
  if (!Member->isAnyMemberInitializer())
    return GetKeyForBase(Context, QualType(Member->getBaseClass(), 0));

  return Member->getAnyMember()->getCanonicalDecl();
}

static void DiagnoseBaseOrMemInitializerOrder(
    Sema &SemaRef, const CXXConstructorDecl *Constructor,
    ArrayRef<CXXCtorInitializer *> Inits) {
  if (Constructor->getDeclContext()->isDependentContext())
    return;

  // Don't check initializers order unless the warning is enabled at the
  // location of at least one initializer.
  bool ShouldCheckOrder = false;
  for (unsigned InitIndex = 0; InitIndex != Inits.size(); ++InitIndex) {
    CXXCtorInitializer *Init = Inits[InitIndex];
    if (!SemaRef.Diags.isIgnored(diag::warn_initializer_out_of_order,
                                 Init->getSourceLocation())) {
      ShouldCheckOrder = true;
      break;
    }
  }
  if (!ShouldCheckOrder)
    return;

  // Build the list of bases and members in the order that they'll
  // actually be initialized.  The explicit initializers should be in
  // this same order but may be missing things.
  SmallVector<const void*, 32> IdealInitKeys;

  const CXXRecordDecl *ClassDecl = Constructor->getParent();

  // 1. Virtual bases.
  for (const auto &VBase : ClassDecl->vbases())
    IdealInitKeys.push_back(GetKeyForBase(SemaRef.Context, VBase.getType()));

  // 2. Non-virtual bases.
  for (const auto &Base : ClassDecl->bases()) {
    if (Base.isVirtual())
      continue;
    IdealInitKeys.push_back(GetKeyForBase(SemaRef.Context, Base.getType()));
  }

  // 3. Direct fields.
  for (auto *Field : ClassDecl->fields()) {
    if (Field->isUnnamedBitfield())
      continue;

    PopulateKeysForFields(Field, IdealInitKeys);
  }

  unsigned NumIdealInits = IdealInitKeys.size();
  unsigned IdealIndex = 0;

  CXXCtorInitializer *PrevInit = nullptr;
  for (unsigned InitIndex = 0; InitIndex != Inits.size(); ++InitIndex) {
    CXXCtorInitializer *Init = Inits[InitIndex];
    const void *InitKey = GetKeyForMember(SemaRef.Context, Init);

    // Scan forward to try to find this initializer in the idealized
    // initializers list.
    for (; IdealIndex != NumIdealInits; ++IdealIndex)
      if (InitKey == IdealInitKeys[IdealIndex])
        break;

    // If we didn't find this initializer, it must be because we
    // scanned past it on a previous iteration.  That can only
    // happen if we're out of order;  emit a warning.
    if (IdealIndex == NumIdealInits && PrevInit) {
      Sema::SemaDiagnosticBuilder D =
        SemaRef.Diag(PrevInit->getSourceLocation(),
                     diag::warn_initializer_out_of_order);

      if (PrevInit->isAnyMemberInitializer())
        D << 0 << PrevInit->getAnyMember()->getDeclName();
      else
        D << 1 << PrevInit->getTypeSourceInfo()->getType();

      if (Init->isAnyMemberInitializer())
        D << 0 << Init->getAnyMember()->getDeclName();
      else
        D << 1 << Init->getTypeSourceInfo()->getType();

      // Move back to the initializer's location in the ideal list.
      for (IdealIndex = 0; IdealIndex != NumIdealInits; ++IdealIndex)
        if (InitKey == IdealInitKeys[IdealIndex])
          break;

      assert(IdealIndex < NumIdealInits &&
             "initializer not found in initializer list");
    }

    PrevInit = Init;
  }
}

namespace {
bool CheckRedundantInit(Sema &S,
                        CXXCtorInitializer *Init,
                        CXXCtorInitializer *&PrevInit) {
  if (!PrevInit) {
    PrevInit = Init;
    return false;
  }

  if (FieldDecl *Field = Init->getAnyMember())
    S.Diag(Init->getSourceLocation(),
           diag::err_multiple_mem_initialization)
      << Field->getDeclName()
      << Init->getSourceRange();
  else {
    const Type *BaseClass = Init->getBaseClass();
    assert(BaseClass && "neither field nor base");
    S.Diag(Init->getSourceLocation(),
           diag::err_multiple_base_initialization)
      << QualType(BaseClass, 0)
      << Init->getSourceRange();
  }
  S.Diag(PrevInit->getSourceLocation(), diag::note_previous_initializer)
    << 0 << PrevInit->getSourceRange();

  return true;
}

typedef std::pair<NamedDecl *, CXXCtorInitializer *> UnionEntry;
typedef llvm::DenseMap<RecordDecl*, UnionEntry> RedundantUnionMap;

bool CheckRedundantUnionInit(Sema &S,
                             CXXCtorInitializer *Init,
                             RedundantUnionMap &Unions) {
  FieldDecl *Field = Init->getAnyMember();
  RecordDecl *Parent = Field->getParent();
  NamedDecl *Child = Field;

  while (Parent->isAnonymousStructOrUnion() || Parent->isUnion()) {
    if (Parent->isUnion()) {
      UnionEntry &En = Unions[Parent];
      if (En.first && En.first != Child) {
        S.Diag(Init->getSourceLocation(),
               diag::err_multiple_mem_union_initialization)
          << Field->getDeclName()
          << Init->getSourceRange();
        S.Diag(En.second->getSourceLocation(), diag::note_previous_initializer)
          << 0 << En.second->getSourceRange();
        return true;
      }
      if (!En.first) {
        En.first = Child;
        En.second = Init;
      }
      if (!Parent->isAnonymousStructOrUnion())
        return false;
    }

    Child = Parent;
    Parent = cast<RecordDecl>(Parent->getDeclContext());
  }

  return false;
}
}

/// ActOnMemInitializers - Handle the member initializers for a constructor.
void Sema::ActOnMemInitializers(Decl *ConstructorDecl,
                                SourceLocation ColonLoc,
                                ArrayRef<CXXCtorInitializer*> MemInits,
                                bool AnyErrors) {
  if (!ConstructorDecl)
    return;

  AdjustDeclIfTemplate(ConstructorDecl);

  CXXConstructorDecl *Constructor
    = dyn_cast<CXXConstructorDecl>(ConstructorDecl);

  if (!Constructor) {
    Diag(ColonLoc, diag::err_only_constructors_take_base_inits);
    return;
  }

  // Mapping for the duplicate initializers check.
  // For member initializers, this is keyed with a FieldDecl*.
  // For base initializers, this is keyed with a Type*.
  llvm::DenseMap<const void *, CXXCtorInitializer *> Members;

  // Mapping for the inconsistent anonymous-union initializers check.
  RedundantUnionMap MemberUnions;

  bool HadError = false;
  for (unsigned i = 0; i < MemInits.size(); i++) {
    CXXCtorInitializer *Init = MemInits[i];

    // Set the source order index.
    Init->setSourceOrder(i);

    if (Init->isAnyMemberInitializer()) {
      const void *Key = GetKeyForMember(Context, Init);
      if (CheckRedundantInit(*this, Init, Members[Key]) ||
          CheckRedundantUnionInit(*this, Init, MemberUnions))
        HadError = true;
    } else if (Init->isBaseInitializer()) {
      const void *Key = GetKeyForMember(Context, Init);
      if (CheckRedundantInit(*this, Init, Members[Key]))
        HadError = true;
    } else {
      assert(Init->isDelegatingInitializer());
      // This must be the only initializer
      if (MemInits.size() != 1) {
        Diag(Init->getSourceLocation(),
             diag::err_delegating_initializer_alone)
          << Init->getSourceRange() << MemInits[i ? 0 : 1]->getSourceRange();
        // We will treat this as being the only initializer.
      }
      SetDelegatingInitializer(Constructor, MemInits[i]);
      // Return immediately as the initializer is set.
      return;
    }
  }

  if (HadError)
    return;

  DiagnoseBaseOrMemInitializerOrder(*this, Constructor, MemInits);

  SetCtorInitializers(Constructor, AnyErrors, MemInits);

  DiagnoseUninitializedFields(*this, Constructor);
}

void
Sema::MarkBaseAndMemberDestructorsReferenced(SourceLocation Location,
                                             CXXRecordDecl *ClassDecl) {
  // Ignore dependent contexts. Also ignore unions, since their members never
  // have destructors implicitly called.
  if (ClassDecl->isDependentContext() || ClassDecl->isUnion())
    return;

  // FIXME: all the access-control diagnostics are positioned on the
  // field/base declaration.  That's probably good; that said, the
  // user might reasonably want to know why the destructor is being
  // emitted, and we currently don't say.

  // Non-static data members.
  for (auto *Field : ClassDecl->fields()) {
    if (Field->isInvalidDecl())
      continue;

    // Don't destroy incomplete or zero-length arrays.
    if (isIncompleteOrZeroLengthArrayType(Context, Field->getType()))
      continue;

    QualType FieldType = Context.getBaseElementType(Field->getType());

    const RecordType* RT = FieldType->getAs<RecordType>();
    if (!RT)
      continue;

    CXXRecordDecl *FieldClassDecl = cast<CXXRecordDecl>(RT->getDecl());
    if (FieldClassDecl->isInvalidDecl())
      continue;
    if (FieldClassDecl->hasIrrelevantDestructor())
      continue;
    // The destructor for an implicit anonymous union member is never invoked.
    if (FieldClassDecl->isUnion() && FieldClassDecl->isAnonymousStructOrUnion())
      continue;

    CXXDestructorDecl *Dtor = LookupDestructor(FieldClassDecl);
    assert(Dtor && "No dtor found for FieldClassDecl!");
    CheckDestructorAccess(Field->getLocation(), Dtor,
                          PDiag(diag::err_access_dtor_field)
                            << Field->getDeclName()
                            << FieldType);

    MarkFunctionReferenced(Location, Dtor);
    DiagnoseUseOfDecl(Dtor, Location);
  }

  // We only potentially invoke the destructors of potentially constructed
  // subobjects.
  bool VisitVirtualBases = !ClassDecl->isAbstract();

  llvm::SmallPtrSet<const RecordType *, 8> DirectVirtualBases;

  // Bases.
  for (const auto &Base : ClassDecl->bases()) {
    // Bases are always records in a well-formed non-dependent class.
    const RecordType *RT = Base.getType()->getAs<RecordType>();

    // Remember direct virtual bases.
    if (Base.isVirtual()) {
      if (!VisitVirtualBases)
        continue;
      DirectVirtualBases.insert(RT);
    }

    CXXRecordDecl *BaseClassDecl = cast<CXXRecordDecl>(RT->getDecl());
    // If our base class is invalid, we probably can't get its dtor anyway.
    if (BaseClassDecl->isInvalidDecl())
      continue;
    if (BaseClassDecl->hasIrrelevantDestructor())
      continue;

    CXXDestructorDecl *Dtor = LookupDestructor(BaseClassDecl);
    assert(Dtor && "No dtor found for BaseClassDecl!");

    // FIXME: caret should be on the start of the class name
    CheckDestructorAccess(Base.getBeginLoc(), Dtor,
                          PDiag(diag::err_access_dtor_base)
                              << Base.getType() << Base.getSourceRange(),
                          Context.getTypeDeclType(ClassDecl));

    MarkFunctionReferenced(Location, Dtor);
    DiagnoseUseOfDecl(Dtor, Location);
  }

  if (!VisitVirtualBases)
    return;

  // Virtual bases.
  for (const auto &VBase : ClassDecl->vbases()) {
    // Bases are always records in a well-formed non-dependent class.
    const RecordType *RT = VBase.getType()->castAs<RecordType>();

    // Ignore direct virtual bases.
    if (DirectVirtualBases.count(RT))
      continue;

    CXXRecordDecl *BaseClassDecl = cast<CXXRecordDecl>(RT->getDecl());
    // If our base class is invalid, we probably can't get its dtor anyway.
    if (BaseClassDecl->isInvalidDecl())
      continue;
    if (BaseClassDecl->hasIrrelevantDestructor())
      continue;

    CXXDestructorDecl *Dtor = LookupDestructor(BaseClassDecl);
    assert(Dtor && "No dtor found for BaseClassDecl!");
    if (CheckDestructorAccess(
            ClassDecl->getLocation(), Dtor,
            PDiag(diag::err_access_dtor_vbase)
                << Context.getTypeDeclType(ClassDecl) << VBase.getType(),
            Context.getTypeDeclType(ClassDecl)) ==
        AR_accessible) {
      CheckDerivedToBaseConversion(
          Context.getTypeDeclType(ClassDecl), VBase.getType(),
          diag::err_access_dtor_vbase, 0, ClassDecl->getLocation(),
          SourceRange(), DeclarationName(), nullptr);
    }

    MarkFunctionReferenced(Location, Dtor);
    DiagnoseUseOfDecl(Dtor, Location);
  }
}

void Sema::ActOnDefaultCtorInitializers(Decl *CDtorDecl) {
  if (!CDtorDecl)
    return;

  if (CXXConstructorDecl *Constructor
      = dyn_cast<CXXConstructorDecl>(CDtorDecl)) {
    SetCtorInitializers(Constructor, /*AnyErrors=*/false);
    DiagnoseUninitializedFields(*this, Constructor);
  }
}

bool Sema::isAbstractType(SourceLocation Loc, QualType T) {
  if (!getLangOpts().CPlusPlus)
    return false;

  const auto *RD = Context.getBaseElementType(T)->getAsCXXRecordDecl();
  if (!RD)
    return false;

  // FIXME: Per [temp.inst]p1, we are supposed to trigger instantiation of a
  // class template specialization here, but doing so breaks a lot of code.

  // We can't answer whether something is abstract until it has a
  // definition. If it's currently being defined, we'll walk back
  // over all the declarations when we have a full definition.
  const CXXRecordDecl *Def = RD->getDefinition();
  if (!Def || Def->isBeingDefined())
    return false;

  return RD->isAbstract();
}

bool Sema::RequireNonAbstractType(SourceLocation Loc, QualType T,
                                  TypeDiagnoser &Diagnoser) {
  if (!isAbstractType(Loc, T))
    return false;

  T = Context.getBaseElementType(T);
  Diagnoser.diagnose(*this, Loc, T);
  DiagnoseAbstractType(T->getAsCXXRecordDecl());
  return true;
}

void Sema::DiagnoseAbstractType(const CXXRecordDecl *RD) {
  // Check if we've already emitted the list of pure virtual functions
  // for this class.
  if (PureVirtualClassDiagSet && PureVirtualClassDiagSet->count(RD))
    return;

  // If the diagnostic is suppressed, don't emit the notes. We're only
  // going to emit them once, so try to attach them to a diagnostic we're
  // actually going to show.
  if (Diags.isLastDiagnosticIgnored())
    return;

  CXXFinalOverriderMap FinalOverriders;
  RD->getFinalOverriders(FinalOverriders);

  // Keep a set of seen pure methods so we won't diagnose the same method
  // more than once.
  llvm::SmallPtrSet<const CXXMethodDecl *, 8> SeenPureMethods;

  for (CXXFinalOverriderMap::iterator M = FinalOverriders.begin(),
                                   MEnd = FinalOverriders.end();
       M != MEnd;
       ++M) {
    for (OverridingMethods::iterator SO = M->second.begin(),
                                  SOEnd = M->second.end();
         SO != SOEnd; ++SO) {
      // C++ [class.abstract]p4:
      //   A class is abstract if it contains or inherits at least one
      //   pure virtual function for which the final overrider is pure
      //   virtual.

      //
      if (SO->second.size() != 1)
        continue;

      if (!SO->second.front().Method->isPure())
        continue;

      if (!SeenPureMethods.insert(SO->second.front().Method).second)
        continue;

      Diag(SO->second.front().Method->getLocation(),
           diag::note_pure_virtual_function)
        << SO->second.front().Method->getDeclName() << RD->getDeclName();
    }
  }

  if (!PureVirtualClassDiagSet)
    PureVirtualClassDiagSet.reset(new RecordDeclSetTy);
  PureVirtualClassDiagSet->insert(RD);
}

namespace {
struct AbstractUsageInfo {
  Sema &S;
  CXXRecordDecl *Record;
  CanQualType AbstractType;
  bool Invalid;

  AbstractUsageInfo(Sema &S, CXXRecordDecl *Record)
    : S(S), Record(Record),
      AbstractType(S.Context.getCanonicalType(
                   S.Context.getTypeDeclType(Record))),
      Invalid(false) {}

  void DiagnoseAbstractType() {
    if (Invalid) return;
    S.DiagnoseAbstractType(Record);
    Invalid = true;
  }

  void CheckType(const NamedDecl *D, TypeLoc TL, Sema::AbstractDiagSelID Sel);
};

struct CheckAbstractUsage {
  AbstractUsageInfo &Info;
  const NamedDecl *Ctx;

  CheckAbstractUsage(AbstractUsageInfo &Info, const NamedDecl *Ctx)
    : Info(Info), Ctx(Ctx) {}

  void Visit(TypeLoc TL, Sema::AbstractDiagSelID Sel) {
    switch (TL.getTypeLocClass()) {
#define ABSTRACT_TYPELOC(CLASS, PARENT)
#define TYPELOC(CLASS, PARENT) \
    case TypeLoc::CLASS: Check(TL.castAs<CLASS##TypeLoc>(), Sel); break;
#include "clang/AST/TypeLocNodes.def"
    }
  }

  void Check(FunctionProtoTypeLoc TL, Sema::AbstractDiagSelID Sel) {
    Visit(TL.getReturnLoc(), Sema::AbstractReturnType);
    for (unsigned I = 0, E = TL.getNumParams(); I != E; ++I) {
      if (!TL.getParam(I))
        continue;

      TypeSourceInfo *TSI = TL.getParam(I)->getTypeSourceInfo();
      if (TSI) Visit(TSI->getTypeLoc(), Sema::AbstractParamType);
    }
  }

  void Check(ArrayTypeLoc TL, Sema::AbstractDiagSelID Sel) {
    Visit(TL.getElementLoc(), Sema::AbstractArrayType);
  }

  void Check(TemplateSpecializationTypeLoc TL, Sema::AbstractDiagSelID Sel) {
    // Visit the type parameters from a permissive context.
    for (unsigned I = 0, E = TL.getNumArgs(); I != E; ++I) {
      TemplateArgumentLoc TAL = TL.getArgLoc(I);
      if (TAL.getArgument().getKind() == TemplateArgument::Type)
        if (TypeSourceInfo *TSI = TAL.getTypeSourceInfo())
          Visit(TSI->getTypeLoc(), Sema::AbstractNone);
      // TODO: other template argument types?
    }
  }

  // Visit pointee types from a permissive context.
#define CheckPolymorphic(Type) \
  void Check(Type TL, Sema::AbstractDiagSelID Sel) { \
    Visit(TL.getNextTypeLoc(), Sema::AbstractNone); \
  }
  CheckPolymorphic(PointerTypeLoc)
  CheckPolymorphic(ReferenceTypeLoc)
  CheckPolymorphic(MemberPointerTypeLoc)
  CheckPolymorphic(BlockPointerTypeLoc)
  CheckPolymorphic(AtomicTypeLoc)

  /// Handle all the types we haven't given a more specific
  /// implementation for above.
  void Check(TypeLoc TL, Sema::AbstractDiagSelID Sel) {
    // Every other kind of type that we haven't called out already
    // that has an inner type is either (1) sugar or (2) contains that
    // inner type in some way as a subobject.
    if (TypeLoc Next = TL.getNextTypeLoc())
      return Visit(Next, Sel);

    // If there's no inner type and we're in a permissive context,
    // don't diagnose.
    if (Sel == Sema::AbstractNone) return;

    // Check whether the type matches the abstract type.
    QualType T = TL.getType();
    if (T->isArrayType()) {
      Sel = Sema::AbstractArrayType;
      T = Info.S.Context.getBaseElementType(T);
    }
    CanQualType CT = T->getCanonicalTypeUnqualified().getUnqualifiedType();
    if (CT != Info.AbstractType) return;

    // It matched; do some magic.
    if (Sel == Sema::AbstractArrayType) {
      Info.S.Diag(Ctx->getLocation(), diag::err_array_of_abstract_type)
        << T << TL.getSourceRange();
    } else {
      Info.S.Diag(Ctx->getLocation(), diag::err_abstract_type_in_decl)
        << Sel << T << TL.getSourceRange();
    }
    Info.DiagnoseAbstractType();
  }
};

void AbstractUsageInfo::CheckType(const NamedDecl *D, TypeLoc TL,
                                  Sema::AbstractDiagSelID Sel) {
  CheckAbstractUsage(*this, D).Visit(TL, Sel);
}

}

/// Check for invalid uses of an abstract type in a method declaration.
static void CheckAbstractClassUsage(AbstractUsageInfo &Info,
                                    CXXMethodDecl *MD) {
  // No need to do the check on definitions, which require that
  // the return/param types be complete.
  if (MD->doesThisDeclarationHaveABody())
    return;

  // For safety's sake, just ignore it if we don't have type source
  // information.  This should never happen for non-implicit methods,
  // but...
  if (TypeSourceInfo *TSI = MD->getTypeSourceInfo())
    Info.CheckType(MD, TSI->getTypeLoc(), Sema::AbstractNone);
}

/// Check for invalid uses of an abstract type within a class definition.
static void CheckAbstractClassUsage(AbstractUsageInfo &Info,
                                    CXXRecordDecl *RD) {
  for (auto *D : RD->decls()) {
    if (D->isImplicit()) continue;

    // Methods and method templates.
    if (isa<CXXMethodDecl>(D)) {
      CheckAbstractClassUsage(Info, cast<CXXMethodDecl>(D));
    } else if (isa<FunctionTemplateDecl>(D)) {
      FunctionDecl *FD = cast<FunctionTemplateDecl>(D)->getTemplatedDecl();
      CheckAbstractClassUsage(Info, cast<CXXMethodDecl>(FD));

    // Fields and static variables.
    } else if (isa<FieldDecl>(D)) {
      FieldDecl *FD = cast<FieldDecl>(D);
      if (TypeSourceInfo *TSI = FD->getTypeSourceInfo())
        Info.CheckType(FD, TSI->getTypeLoc(), Sema::AbstractFieldType);
    } else if (isa<VarDecl>(D)) {
      VarDecl *VD = cast<VarDecl>(D);
      if (TypeSourceInfo *TSI = VD->getTypeSourceInfo())
        Info.CheckType(VD, TSI->getTypeLoc(), Sema::AbstractVariableType);

    // Nested classes and class templates.
    } else if (isa<CXXRecordDecl>(D)) {
      CheckAbstractClassUsage(Info, cast<CXXRecordDecl>(D));
    } else if (isa<ClassTemplateDecl>(D)) {
      CheckAbstractClassUsage(Info,
                             cast<ClassTemplateDecl>(D)->getTemplatedDecl());
    }
  }
}

static void ReferenceDllExportedMembers(Sema &S, CXXRecordDecl *Class) {
  Attr *ClassAttr = getDLLAttr(Class);
  if (!ClassAttr)
    return;

  assert(ClassAttr->getKind() == attr::DLLExport);

  TemplateSpecializationKind TSK = Class->getTemplateSpecializationKind();

  if (TSK == TSK_ExplicitInstantiationDeclaration)
    // Don't go any further if this is just an explicit instantiation
    // declaration.
    return;

  if (S.Context.getTargetInfo().getTriple().isWindowsGNUEnvironment())
    S.MarkVTableUsed(Class->getLocation(), Class, true);

  for (Decl *Member : Class->decls()) {
    // Defined static variables that are members of an exported base
    // class must be marked export too.
    auto *VD = dyn_cast<VarDecl>(Member);
    if (VD && Member->getAttr<DLLExportAttr>() &&
        VD->getStorageClass() == SC_Static &&
        TSK == TSK_ImplicitInstantiation)
      S.MarkVariableReferenced(VD->getLocation(), VD);

    auto *MD = dyn_cast<CXXMethodDecl>(Member);
    if (!MD)
      continue;

    if (Member->getAttr<DLLExportAttr>()) {
      if (MD->isUserProvided()) {
        // Instantiate non-default class member functions ...

        // .. except for certain kinds of template specializations.
        if (TSK == TSK_ImplicitInstantiation && !ClassAttr->isInherited())
          continue;

        S.MarkFunctionReferenced(Class->getLocation(), MD);

        // The function will be passed to the consumer when its definition is
        // encountered.
      } else if (!MD->isTrivial() || MD->isExplicitlyDefaulted() ||
                 MD->isCopyAssignmentOperator() ||
                 MD->isMoveAssignmentOperator()) {
        // Synthesize and instantiate non-trivial implicit methods, explicitly
        // defaulted methods, and the copy and move assignment operators. The
        // latter are exported even if they are trivial, because the address of
        // an operator can be taken and should compare equal across libraries.
        DiagnosticErrorTrap Trap(S.Diags);
        S.MarkFunctionReferenced(Class->getLocation(), MD);
        if (Trap.hasErrorOccurred()) {
          S.Diag(ClassAttr->getLocation(), diag::note_due_to_dllexported_class)
              << Class << !S.getLangOpts().CPlusPlus11;
          break;
        }

        // There is no later point when we will see the definition of this
        // function, so pass it to the consumer now.
        S.Consumer.HandleTopLevelDecl(DeclGroupRef(MD));
      }
    }
  }
}

static void checkForMultipleExportedDefaultConstructors(Sema &S,
                                                        CXXRecordDecl *Class) {
  // Only the MS ABI has default constructor closures, so we don't need to do
  // this semantic checking anywhere else.
  if (!S.Context.getTargetInfo().getCXXABI().isMicrosoft())
    return;

  CXXConstructorDecl *LastExportedDefaultCtor = nullptr;
  for (Decl *Member : Class->decls()) {
    // Look for exported default constructors.
    auto *CD = dyn_cast<CXXConstructorDecl>(Member);
    if (!CD || !CD->isDefaultConstructor())
      continue;
    auto *Attr = CD->getAttr<DLLExportAttr>();
    if (!Attr)
      continue;

    // If the class is non-dependent, mark the default arguments as ODR-used so
    // that we can properly codegen the constructor closure.
    if (!Class->isDependentContext()) {
      for (ParmVarDecl *PD : CD->parameters()) {
        (void)S.CheckCXXDefaultArgExpr(Attr->getLocation(), CD, PD);
        S.DiscardCleanupsInEvaluationContext();
      }
    }

    if (LastExportedDefaultCtor) {
      S.Diag(LastExportedDefaultCtor->getLocation(),
             diag::err_attribute_dll_ambiguous_default_ctor)
          << Class;
      S.Diag(CD->getLocation(), diag::note_entity_declared_at)
          << CD->getDeclName();
      return;
    }
    LastExportedDefaultCtor = CD;
  }
}

void Sema::checkClassLevelCodeSegAttribute(CXXRecordDecl *Class) {
  // Mark any compiler-generated routines with the implicit code_seg attribute.
  for (auto *Method : Class->methods()) {
    if (Method->isUserProvided())
      continue;
    if (Attr *A = getImplicitCodeSegOrSectionAttrForFunction(Method, /*IsDefinition=*/true))
      Method->addAttr(A);
  }
}

/// Check class-level dllimport/dllexport attribute.
void Sema::checkClassLevelDLLAttribute(CXXRecordDecl *Class) {
  Attr *ClassAttr = getDLLAttr(Class);

  // MSVC inherits DLL attributes to partial class template specializations.
  if (Context.getTargetInfo().getCXXABI().isMicrosoft() && !ClassAttr) {
    if (auto *Spec = dyn_cast<ClassTemplatePartialSpecializationDecl>(Class)) {
      if (Attr *TemplateAttr =
              getDLLAttr(Spec->getSpecializedTemplate()->getTemplatedDecl())) {
        auto *A = cast<InheritableAttr>(TemplateAttr->clone(getASTContext()));
        A->setInherited(true);
        ClassAttr = A;
      }
    }
  }

  if (!ClassAttr)
    return;

  if (!Class->isExternallyVisible()) {
    Diag(Class->getLocation(), diag::err_attribute_dll_not_extern)
        << Class << ClassAttr;
    return;
  }

  if (Context.getTargetInfo().getCXXABI().isMicrosoft() &&
      !ClassAttr->isInherited()) {
    // Diagnose dll attributes on members of class with dll attribute.
    for (Decl *Member : Class->decls()) {
      if (!isa<VarDecl>(Member) && !isa<CXXMethodDecl>(Member))
        continue;
      InheritableAttr *MemberAttr = getDLLAttr(Member);
      if (!MemberAttr || MemberAttr->isInherited() || Member->isInvalidDecl())
        continue;

      Diag(MemberAttr->getLocation(),
             diag::err_attribute_dll_member_of_dll_class)
          << MemberAttr << ClassAttr;
      Diag(ClassAttr->getLocation(), diag::note_previous_attribute);
      Member->setInvalidDecl();
    }
  }

  if (Class->getDescribedClassTemplate())
    // Don't inherit dll attribute until the template is instantiated.
    return;

  // The class is either imported or exported.
  const bool ClassExported = ClassAttr->getKind() == attr::DLLExport;

  // Check if this was a dllimport attribute propagated from a derived class to
  // a base class template specialization. We don't apply these attributes to
  // static data members.
  const bool PropagatedImport =
      !ClassExported &&
      cast<DLLImportAttr>(ClassAttr)->wasPropagatedToBaseTemplate();

  TemplateSpecializationKind TSK = Class->getTemplateSpecializationKind();

  // Ignore explicit dllexport on explicit class template instantiation declarations.
  if (ClassExported && !ClassAttr->isInherited() &&
      TSK == TSK_ExplicitInstantiationDeclaration) {
    Class->dropAttr<DLLExportAttr>();
    return;
  }

  // Force declaration of implicit members so they can inherit the attribute.
  ForceDeclarationOfImplicitMembers(Class);

  // FIXME: MSVC's docs say all bases must be exportable, but this doesn't
  // seem to be true in practice?

  for (Decl *Member : Class->decls()) {
    VarDecl *VD = dyn_cast<VarDecl>(Member);
    CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(Member);

    // Only methods and static fields inherit the attributes.
    if (!VD && !MD)
      continue;

    if (MD) {
      // Don't process deleted methods.
      if (MD->isDeleted())
        continue;

      if (MD->isInlined()) {
        // MinGW does not import or export inline methods.
        if (!Context.getTargetInfo().getCXXABI().isMicrosoft() &&
            !Context.getTargetInfo().getTriple().isWindowsItaniumEnvironment())
          continue;

        // MSVC versions before 2015 don't export the move assignment operators
        // and move constructor, so don't attempt to import/export them if
        // we have a definition.
        auto *Ctor = dyn_cast<CXXConstructorDecl>(MD);
        if ((MD->isMoveAssignmentOperator() ||
             (Ctor && Ctor->isMoveConstructor())) &&
            !getLangOpts().isCompatibleWithMSVC(LangOptions::MSVC2015))
          continue;

        // MSVC2015 doesn't export trivial defaulted x-tor but copy assign
        // operator is exported anyway.
        if (getLangOpts().isCompatibleWithMSVC(LangOptions::MSVC2015) &&
            (Ctor || isa<CXXDestructorDecl>(MD)) && MD->isTrivial())
          continue;
      }
    }

    // Don't apply dllimport attributes to static data members of class template
    // instantiations when the attribute is propagated from a derived class.
    if (VD && PropagatedImport)
      continue;

    if (!cast<NamedDecl>(Member)->isExternallyVisible())
      continue;

    if (!getDLLAttr(Member)) {
      InheritableAttr *NewAttr = nullptr;

      // Do not export/import inline function when -fno-dllexport-inlines is
      // passed. But add attribute for later local static var check.
      if (!getLangOpts().DllExportInlines && MD && MD->isInlined() &&
          TSK != TSK_ExplicitInstantiationDeclaration &&
          TSK != TSK_ExplicitInstantiationDefinition) {
        if (ClassExported) {
          NewAttr = ::new (getASTContext())
            DLLExportStaticLocalAttr(ClassAttr->getRange(),
                                     getASTContext(),
                                     ClassAttr->getSpellingListIndex());
        } else {
          NewAttr = ::new (getASTContext())
            DLLImportStaticLocalAttr(ClassAttr->getRange(),
                                     getASTContext(),
                                     ClassAttr->getSpellingListIndex());
        }
      } else {
        NewAttr = cast<InheritableAttr>(ClassAttr->clone(getASTContext()));
      }

      NewAttr->setInherited(true);
      Member->addAttr(NewAttr);

      if (MD) {
        // Propagate DLLAttr to friend re-declarations of MD that have already
        // been constructed.
        for (FunctionDecl *FD = MD->getMostRecentDecl(); FD;
             FD = FD->getPreviousDecl()) {
          if (FD->getFriendObjectKind() == Decl::FOK_None)
            continue;
          assert(!getDLLAttr(FD) &&
                 "friend re-decl should not already have a DLLAttr");
          NewAttr = cast<InheritableAttr>(ClassAttr->clone(getASTContext()));
          NewAttr->setInherited(true);
          FD->addAttr(NewAttr);
        }
      }
    }
  }

  if (ClassExported)
    DelayedDllExportClasses.push_back(Class);
}

/// Perform propagation of DLL attributes from a derived class to a
/// templated base class for MS compatibility.
void Sema::propagateDLLAttrToBaseClassTemplate(
    CXXRecordDecl *Class, Attr *ClassAttr,
    ClassTemplateSpecializationDecl *BaseTemplateSpec, SourceLocation BaseLoc) {
  if (getDLLAttr(
          BaseTemplateSpec->getSpecializedTemplate()->getTemplatedDecl())) {
    // If the base class template has a DLL attribute, don't try to change it.
    return;
  }

  auto TSK = BaseTemplateSpec->getSpecializationKind();
  if (!getDLLAttr(BaseTemplateSpec) &&
      (TSK == TSK_Undeclared || TSK == TSK_ExplicitInstantiationDeclaration ||
       TSK == TSK_ImplicitInstantiation)) {
    // The template hasn't been instantiated yet (or it has, but only as an
    // explicit instantiation declaration or implicit instantiation, which means
    // we haven't codegenned any members yet), so propagate the attribute.
    auto *NewAttr = cast<InheritableAttr>(ClassAttr->clone(getASTContext()));
    NewAttr->setInherited(true);
    BaseTemplateSpec->addAttr(NewAttr);

    // If this was an import, mark that we propagated it from a derived class to
    // a base class template specialization.
    if (auto *ImportAttr = dyn_cast<DLLImportAttr>(NewAttr))
      ImportAttr->setPropagatedToBaseTemplate();

    // If the template is already instantiated, checkDLLAttributeRedeclaration()
    // needs to be run again to work see the new attribute. Otherwise this will
    // get run whenever the template is instantiated.
    if (TSK != TSK_Undeclared)
      checkClassLevelDLLAttribute(BaseTemplateSpec);

    return;
  }

  if (getDLLAttr(BaseTemplateSpec)) {
    // The template has already been specialized or instantiated with an
    // attribute, explicitly or through propagation. We should not try to change
    // it.
    return;
  }

  // The template was previously instantiated or explicitly specialized without
  // a dll attribute, It's too late for us to add an attribute, so warn that
  // this is unsupported.
  Diag(BaseLoc, diag::warn_attribute_dll_instantiated_base_class)
      << BaseTemplateSpec->isExplicitSpecialization();
  Diag(ClassAttr->getLocation(), diag::note_attribute);
  if (BaseTemplateSpec->isExplicitSpecialization()) {
    Diag(BaseTemplateSpec->getLocation(),
           diag::note_template_class_explicit_specialization_was_here)
        << BaseTemplateSpec;
  } else {
    Diag(BaseTemplateSpec->getPointOfInstantiation(),
           diag::note_template_class_instantiation_was_here)
        << BaseTemplateSpec;
  }
}

static void DefineImplicitSpecialMember(Sema &S, CXXMethodDecl *MD,
                                        SourceLocation DefaultLoc) {
  switch (S.getSpecialMember(MD)) {
  case Sema::CXXDefaultConstructor:
    S.DefineImplicitDefaultConstructor(DefaultLoc,
                                       cast<CXXConstructorDecl>(MD));
    break;
  case Sema::CXXCopyConstructor:
    S.DefineImplicitCopyConstructor(DefaultLoc, cast<CXXConstructorDecl>(MD));
    break;
  case Sema::CXXCopyAssignment:
    S.DefineImplicitCopyAssignment(DefaultLoc, MD);
    break;
  case Sema::CXXDestructor:
    S.DefineImplicitDestructor(DefaultLoc, cast<CXXDestructorDecl>(MD));
    break;
  case Sema::CXXMoveConstructor:
    S.DefineImplicitMoveConstructor(DefaultLoc, cast<CXXConstructorDecl>(MD));
    break;
  case Sema::CXXMoveAssignment:
    S.DefineImplicitMoveAssignment(DefaultLoc, MD);
    break;
  case Sema::CXXInvalid:
    llvm_unreachable("Invalid special member.");
  }
}

/// Determine whether a type is permitted to be passed or returned in
/// registers, per C++ [class.temporary]p3.
static bool canPassInRegisters(Sema &S, CXXRecordDecl *D,
                               TargetInfo::CallingConvKind CCK) {
  if (D->isDependentType() || D->isInvalidDecl())
    return false;

  // Clang <= 4 used the pre-C++11 rule, which ignores move operations.
  // The PS4 platform ABI follows the behavior of Clang 3.2.
  if (CCK == TargetInfo::CCK_ClangABI4OrPS4)
    return !D->hasNonTrivialDestructorForCall() &&
           !D->hasNonTrivialCopyConstructorForCall();

  if (CCK == TargetInfo::CCK_MicrosoftWin64) {
    bool CopyCtorIsTrivial = false, CopyCtorIsTrivialForCall = false;
    bool DtorIsTrivialForCall = false;

    // If a class has at least one non-deleted, trivial copy constructor, it
    // is passed according to the C ABI. Otherwise, it is passed indirectly.
    //
    // Note: This permits classes with non-trivial copy or move ctors to be
    // passed in registers, so long as they *also* have a trivial copy ctor,
    // which is non-conforming.
    if (D->needsImplicitCopyConstructor()) {
      if (!D->defaultedCopyConstructorIsDeleted()) {
        if (D->hasTrivialCopyConstructor())
          CopyCtorIsTrivial = true;
        if (D->hasTrivialCopyConstructorForCall())
          CopyCtorIsTrivialForCall = true;
      }
    } else {
      for (const CXXConstructorDecl *CD : D->ctors()) {
        if (CD->isCopyConstructor() && !CD->isDeleted()) {
          if (CD->isTrivial())
            CopyCtorIsTrivial = true;
          if (CD->isTrivialForCall())
            CopyCtorIsTrivialForCall = true;
        }
      }
    }

    if (D->needsImplicitDestructor()) {
      if (!D->defaultedDestructorIsDeleted() &&
          D->hasTrivialDestructorForCall())
        DtorIsTrivialForCall = true;
    } else if (const auto *DD = D->getDestructor()) {
      if (!DD->isDeleted() && DD->isTrivialForCall())
        DtorIsTrivialForCall = true;
    }

    // If the copy ctor and dtor are both trivial-for-calls, pass direct.
    if (CopyCtorIsTrivialForCall && DtorIsTrivialForCall)
      return true;

    // If a class has a destructor, we'd really like to pass it indirectly
    // because it allows us to elide copies.  Unfortunately, MSVC makes that
    // impossible for small types, which it will pass in a single register or
    // stack slot. Most objects with dtors are large-ish, so handle that early.
    // We can't call out all large objects as being indirect because there are
    // multiple x64 calling conventions and the C++ ABI code shouldn't dictate
    // how we pass large POD types.

    // Note: This permits small classes with nontrivial destructors to be
    // passed in registers, which is non-conforming.
    if (CopyCtorIsTrivial &&
        S.getASTContext().getTypeSize(D->getTypeForDecl()) <= 64)
      return true;
    return false;
  }

  // Per C++ [class.temporary]p3, the relevant condition is:
  //   each copy constructor, move constructor, and destructor of X is
  //   either trivial or deleted, and X has at least one non-deleted copy
  //   or move constructor
  bool HasNonDeletedCopyOrMove = false;

  if (D->needsImplicitCopyConstructor() &&
      !D->defaultedCopyConstructorIsDeleted()) {
    if (!D->hasTrivialCopyConstructorForCall())
      return false;
    HasNonDeletedCopyOrMove = true;
  }

  if (S.getLangOpts().CPlusPlus11 && D->needsImplicitMoveConstructor() &&
      !D->defaultedMoveConstructorIsDeleted()) {
    if (!D->hasTrivialMoveConstructorForCall())
      return false;
    HasNonDeletedCopyOrMove = true;
  }

  if (D->needsImplicitDestructor() && !D->defaultedDestructorIsDeleted() &&
      !D->hasTrivialDestructorForCall())
    return false;

  for (const CXXMethodDecl *MD : D->methods()) {
    if (MD->isDeleted())
      continue;

    auto *CD = dyn_cast<CXXConstructorDecl>(MD);
    if (CD && CD->isCopyOrMoveConstructor())
      HasNonDeletedCopyOrMove = true;
    else if (!isa<CXXDestructorDecl>(MD))
      continue;

    if (!MD->isTrivialForCall())
      return false;
  }

  return HasNonDeletedCopyOrMove;
}

/// Perform semantic checks on a class definition that has been
/// completing, introducing implicitly-declared members, checking for
/// abstract types, etc.
void Sema::CheckCompletedCXXClass(CXXRecordDecl *Record) {
  if (!Record)
    return;

  if (Record->isAbstract() && !Record->isInvalidDecl()) {
    AbstractUsageInfo Info(*this, Record);
    CheckAbstractClassUsage(Info, Record);
  }

  // If this is not an aggregate type and has no user-declared constructor,
  // complain about any non-static data members of reference or const scalar
  // type, since they will never get initializers.
  if (!Record->isInvalidDecl() && !Record->isDependentType() &&
      !Record->isAggregate() && !Record->hasUserDeclaredConstructor() &&
      !Record->isLambda()) {
    bool Complained = false;
    for (const auto *F : Record->fields()) {
      if (F->hasInClassInitializer() || F->isUnnamedBitfield())
        continue;

      if (F->getType()->isReferenceType() ||
          (F->getType().isConstQualified() && F->getType()->isScalarType())) {
        if (!Complained) {
          Diag(Record->getLocation(), diag::warn_no_constructor_for_refconst)
            << Record->getTagKind() << Record;
          Complained = true;
        }

        Diag(F->getLocation(), diag::note_refconst_member_not_initialized)
          << F->getType()->isReferenceType()
          << F->getDeclName();
      }
    }
  }

  if (Record->getIdentifier()) {
    // C++ [class.mem]p13:
    //   If T is the name of a class, then each of the following shall have a
    //   name different from T:
    //     - every member of every anonymous union that is a member of class T.
    //
    // C++ [class.mem]p14:
    //   In addition, if class T has a user-declared constructor (12.1), every
    //   non-static data member of class T shall have a name different from T.
    DeclContext::lookup_result R = Record->lookup(Record->getDeclName());
    for (DeclContext::lookup_iterator I = R.begin(), E = R.end(); I != E;
         ++I) {
      NamedDecl *D = (*I)->getUnderlyingDecl();
      if (((isa<FieldDecl>(D) || isa<UnresolvedUsingValueDecl>(D)) &&
           Record->hasUserDeclaredConstructor()) ||
          isa<IndirectFieldDecl>(D)) {
        Diag((*I)->getLocation(), diag::err_member_name_of_class)
          << D->getDeclName();
        break;
      }
    }
  }

  // Warn if the class has virtual methods but non-virtual public destructor.
  if (Record->isPolymorphic() && !Record->isDependentType()) {
    CXXDestructorDecl *dtor = Record->getDestructor();
    if ((!dtor || (!dtor->isVirtual() && dtor->getAccess() == AS_public)) &&
        !Record->hasAttr<FinalAttr>())
      Diag(dtor ? dtor->getLocation() : Record->getLocation(),
           diag::warn_non_virtual_dtor) << Context.getRecordType(Record);
  }

  if (Record->isAbstract()) {
    if (FinalAttr *FA = Record->getAttr<FinalAttr>()) {
      Diag(Record->getLocation(), diag::warn_abstract_final_class)
        << FA->isSpelledAsSealed();
      DiagnoseAbstractType(Record);
    }
  }

  // See if trivial_abi has to be dropped.
  if (Record->hasAttr<TrivialABIAttr>())
    checkIllFormedTrivialABIStruct(*Record);

  // Set HasTrivialSpecialMemberForCall if the record has attribute
  // "trivial_abi".
  bool HasTrivialABI = Record->hasAttr<TrivialABIAttr>();

  if (HasTrivialABI)
    Record->setHasTrivialSpecialMemberForCall();

  bool HasMethodWithOverrideControl = false,
       HasOverridingMethodWithoutOverrideControl = false;
  if (!Record->isDependentType()) {
    for (auto *M : Record->methods()) {
      // See if a method overloads virtual methods in a base
      // class without overriding any.
      if (!M->isStatic())
        DiagnoseHiddenVirtualMethods(M);
      if (M->hasAttr<OverrideAttr>())
        HasMethodWithOverrideControl = true;
      else if (M->size_overridden_methods() > 0)
        HasOverridingMethodWithoutOverrideControl = true;
      // Check whether the explicitly-defaulted special members are valid.
      if (!M->isInvalidDecl() && M->isExplicitlyDefaulted())
        CheckExplicitlyDefaultedSpecialMember(M);

      // For an explicitly defaulted or deleted special member, we defer
      // determining triviality until the class is complete. That time is now!
      CXXSpecialMember CSM = getSpecialMember(M);
      if (!M->isImplicit() && !M->isUserProvided()) {
        if (CSM != CXXInvalid) {
          M->setTrivial(SpecialMemberIsTrivial(M, CSM));
          // Inform the class that we've finished declaring this member.
          Record->finishedDefaultedOrDeletedMember(M);
          M->setTrivialForCall(
              HasTrivialABI ||
              SpecialMemberIsTrivial(M, CSM, TAH_ConsiderTrivialABI));
          Record->setTrivialForCallFlags(M);
        }
      }

      // Set triviality for the purpose of calls if this is a user-provided
      // copy/move constructor or destructor.
      if ((CSM == CXXCopyConstructor || CSM == CXXMoveConstructor ||
           CSM == CXXDestructor) && M->isUserProvided()) {
        M->setTrivialForCall(HasTrivialABI);
        Record->setTrivialForCallFlags(M);
      }

      if (!M->isInvalidDecl() && M->isExplicitlyDefaulted() &&
          M->hasAttr<DLLExportAttr>()) {
        if (getLangOpts().isCompatibleWithMSVC(LangOptions::MSVC2015) &&
            M->isTrivial() &&
            (CSM == CXXDefaultConstructor || CSM == CXXCopyConstructor ||
             CSM == CXXDestructor))
          M->dropAttr<DLLExportAttr>();

        if (M->hasAttr<DLLExportAttr>()) {
          DefineImplicitSpecialMember(*this, M, M->getLocation());
          ActOnFinishInlineFunctionDef(M);
        }
      }
    }
  }

  if (HasMethodWithOverrideControl &&
      HasOverridingMethodWithoutOverrideControl) {
    // At least one method has the 'override' control declared.
    // Diagnose all other overridden methods which do not have 'override' specified on them.
    for (auto *M : Record->methods())
      DiagnoseAbsenceOfOverrideControl(M);
  }

  // ms_struct is a request to use the same ABI rules as MSVC.  Check
  // whether this class uses any C++ features that are implemented
  // completely differently in MSVC, and if so, emit a diagnostic.
  // That diagnostic defaults to an error, but we allow projects to
  // map it down to a warning (or ignore it).  It's a fairly common
  // practice among users of the ms_struct pragma to mass-annotate
  // headers, sweeping up a bunch of types that the project doesn't
  // really rely on MSVC-compatible layout for.  We must therefore
  // support "ms_struct except for C++ stuff" as a secondary ABI.
  if (Record->isMsStruct(Context) &&
      (Record->isPolymorphic() || Record->getNumBases())) {
    Diag(Record->getLocation(), diag::warn_cxx_ms_struct);
  }

  checkClassLevelDLLAttribute(Record);
  checkClassLevelCodeSegAttribute(Record);

  bool ClangABICompat4 =
      Context.getLangOpts().getClangABICompat() <= LangOptions::ClangABI::Ver4;
  TargetInfo::CallingConvKind CCK =
      Context.getTargetInfo().getCallingConvKind(ClangABICompat4);
  bool CanPass = canPassInRegisters(*this, Record, CCK);

  // Do not change ArgPassingRestrictions if it has already been set to
  // APK_CanNeverPassInRegs.
  if (Record->getArgPassingRestrictions() != RecordDecl::APK_CanNeverPassInRegs)
    Record->setArgPassingRestrictions(CanPass
                                          ? RecordDecl::APK_CanPassInRegs
                                          : RecordDecl::APK_CannotPassInRegs);

  // If canPassInRegisters returns true despite the record having a non-trivial
  // destructor, the record is destructed in the callee. This happens only when
  // the record or one of its subobjects has a field annotated with trivial_abi
  // or a field qualified with ObjC __strong/__weak.
  if (Context.getTargetInfo().getCXXABI().areArgsDestroyedLeftToRightInCallee())
    Record->setParamDestroyedInCallee(true);
  else if (Record->hasNonTrivialDestructor())
    Record->setParamDestroyedInCallee(CanPass);

  if (getLangOpts().ForceEmitVTables) {
    // If we want to emit all the vtables, we need to mark it as used.  This
    // is especially required for cases like vtable assumption loads.
    MarkVTableUsed(Record->getInnerLocStart(), Record);
  }
}

/// Look up the special member function that would be called by a special
/// member function for a subobject of class type.
///
/// \param Class The class type of the subobject.
/// \param CSM The kind of special member function.
/// \param FieldQuals If the subobject is a field, its cv-qualifiers.
/// \param ConstRHS True if this is a copy operation with a const object
///        on its RHS, that is, if the argument to the outer special member
///        function is 'const' and this is not a field marked 'mutable'.
static Sema::SpecialMemberOverloadResult lookupCallFromSpecialMember(
    Sema &S, CXXRecordDecl *Class, Sema::CXXSpecialMember CSM,
    unsigned FieldQuals, bool ConstRHS) {
  unsigned LHSQuals = 0;
  if (CSM == Sema::CXXCopyAssignment || CSM == Sema::CXXMoveAssignment)
    LHSQuals = FieldQuals;

  unsigned RHSQuals = FieldQuals;
  if (CSM == Sema::CXXDefaultConstructor || CSM == Sema::CXXDestructor)
    RHSQuals = 0;
  else if (ConstRHS)
    RHSQuals |= Qualifiers::Const;

  return S.LookupSpecialMember(Class, CSM,
                               RHSQuals & Qualifiers::Const,
                               RHSQuals & Qualifiers::Volatile,
                               false,
                               LHSQuals & Qualifiers::Const,
                               LHSQuals & Qualifiers::Volatile);
}

class Sema::InheritedConstructorInfo {
  Sema &S;
  SourceLocation UseLoc;

  /// A mapping from the base classes through which the constructor was
  /// inherited to the using shadow declaration in that base class (or a null
  /// pointer if the constructor was declared in that base class).
  llvm::DenseMap<CXXRecordDecl *, ConstructorUsingShadowDecl *>
      InheritedFromBases;

public:
  InheritedConstructorInfo(Sema &S, SourceLocation UseLoc,
                           ConstructorUsingShadowDecl *Shadow)
      : S(S), UseLoc(UseLoc) {
    bool DiagnosedMultipleConstructedBases = false;
    CXXRecordDecl *ConstructedBase = nullptr;
    UsingDecl *ConstructedBaseUsing = nullptr;

    // Find the set of such base class subobjects and check that there's a
    // unique constructed subobject.
    for (auto *D : Shadow->redecls()) {
      auto *DShadow = cast<ConstructorUsingShadowDecl>(D);
      auto *DNominatedBase = DShadow->getNominatedBaseClass();
      auto *DConstructedBase = DShadow->getConstructedBaseClass();

      InheritedFromBases.insert(
          std::make_pair(DNominatedBase->getCanonicalDecl(),
                         DShadow->getNominatedBaseClassShadowDecl()));
      if (DShadow->constructsVirtualBase())
        InheritedFromBases.insert(
            std::make_pair(DConstructedBase->getCanonicalDecl(),
                           DShadow->getConstructedBaseClassShadowDecl()));
      else
        assert(DNominatedBase == DConstructedBase);

      // [class.inhctor.init]p2:
      //   If the constructor was inherited from multiple base class subobjects
      //   of type B, the program is ill-formed.
      if (!ConstructedBase) {
        ConstructedBase = DConstructedBase;
        ConstructedBaseUsing = D->getUsingDecl();
      } else if (ConstructedBase != DConstructedBase &&
                 !Shadow->isInvalidDecl()) {
        if (!DiagnosedMultipleConstructedBases) {
          S.Diag(UseLoc, diag::err_ambiguous_inherited_constructor)
              << Shadow->getTargetDecl();
          S.Diag(ConstructedBaseUsing->getLocation(),
               diag::note_ambiguous_inherited_constructor_using)
              << ConstructedBase;
          DiagnosedMultipleConstructedBases = true;
        }
        S.Diag(D->getUsingDecl()->getLocation(),
               diag::note_ambiguous_inherited_constructor_using)
            << DConstructedBase;
      }
    }

    if (DiagnosedMultipleConstructedBases)
      Shadow->setInvalidDecl();
  }

  /// Find the constructor to use for inherited construction of a base class,
  /// and whether that base class constructor inherits the constructor from a
  /// virtual base class (in which case it won't actually invoke it).
  std::pair<CXXConstructorDecl *, bool>
  findConstructorForBase(CXXRecordDecl *Base, CXXConstructorDecl *Ctor) const {
    auto It = InheritedFromBases.find(Base->getCanonicalDecl());
    if (It == InheritedFromBases.end())
      return std::make_pair(nullptr, false);

    // This is an intermediary class.
    if (It->second)
      return std::make_pair(
          S.findInheritingConstructor(UseLoc, Ctor, It->second),
          It->second->constructsVirtualBase());

    // This is the base class from which the constructor was inherited.
    return std::make_pair(Ctor, false);
  }
};

/// Is the special member function which would be selected to perform the
/// specified operation on the specified class type a constexpr constructor?
static bool
specialMemberIsConstexpr(Sema &S, CXXRecordDecl *ClassDecl,
                         Sema::CXXSpecialMember CSM, unsigned Quals,
                         bool ConstRHS,
                         CXXConstructorDecl *InheritedCtor = nullptr,
                         Sema::InheritedConstructorInfo *Inherited = nullptr) {
  // If we're inheriting a constructor, see if we need to call it for this base
  // class.
  if (InheritedCtor) {
    assert(CSM == Sema::CXXDefaultConstructor);
    auto BaseCtor =
        Inherited->findConstructorForBase(ClassDecl, InheritedCtor).first;
    if (BaseCtor)
      return BaseCtor->isConstexpr();
  }

  if (CSM == Sema::CXXDefaultConstructor)
    return ClassDecl->hasConstexprDefaultConstructor();

  Sema::SpecialMemberOverloadResult SMOR =
      lookupCallFromSpecialMember(S, ClassDecl, CSM, Quals, ConstRHS);
  if (!SMOR.getMethod())
    // A constructor we wouldn't select can't be "involved in initializing"
    // anything.
    return true;
  return SMOR.getMethod()->isConstexpr();
}

/// Determine whether the specified special member function would be constexpr
/// if it were implicitly defined.
static bool defaultedSpecialMemberIsConstexpr(
    Sema &S, CXXRecordDecl *ClassDecl, Sema::CXXSpecialMember CSM,
    bool ConstArg, CXXConstructorDecl *InheritedCtor = nullptr,
    Sema::InheritedConstructorInfo *Inherited = nullptr) {
  if (!S.getLangOpts().CPlusPlus11)
    return false;

  // C++11 [dcl.constexpr]p4:
  // In the definition of a constexpr constructor [...]
  bool Ctor = true;
  switch (CSM) {
  case Sema::CXXDefaultConstructor:
    if (Inherited)
      break;
    // Since default constructor lookup is essentially trivial (and cannot
    // involve, for instance, template instantiation), we compute whether a
    // defaulted default constructor is constexpr directly within CXXRecordDecl.
    //
    // This is important for performance; we need to know whether the default
    // constructor is constexpr to determine whether the type is a literal type.
    return ClassDecl->defaultedDefaultConstructorIsConstexpr();

  case Sema::CXXCopyConstructor:
  case Sema::CXXMoveConstructor:
    // For copy or move constructors, we need to perform overload resolution.
    break;

  case Sema::CXXCopyAssignment:
  case Sema::CXXMoveAssignment:
    if (!S.getLangOpts().CPlusPlus14)
      return false;
    // In C++1y, we need to perform overload resolution.
    Ctor = false;
    break;

  case Sema::CXXDestructor:
  case Sema::CXXInvalid:
    return false;
  }

  //   -- if the class is a non-empty union, or for each non-empty anonymous
  //      union member of a non-union class, exactly one non-static data member
  //      shall be initialized; [DR1359]
  //
  // If we squint, this is guaranteed, since exactly one non-static data member
  // will be initialized (if the constructor isn't deleted), we just don't know
  // which one.
  if (Ctor && ClassDecl->isUnion())
    return CSM == Sema::CXXDefaultConstructor
               ? ClassDecl->hasInClassInitializer() ||
                     !ClassDecl->hasVariantMembers()
               : true;

  //   -- the class shall not have any virtual base classes;
  if (Ctor && ClassDecl->getNumVBases())
    return false;

  // C++1y [class.copy]p26:
  //   -- [the class] is a literal type, and
  if (!Ctor && !ClassDecl->isLiteral())
    return false;

  //   -- every constructor involved in initializing [...] base class
  //      sub-objects shall be a constexpr constructor;
  //   -- the assignment operator selected to copy/move each direct base
  //      class is a constexpr function, and
  for (const auto &B : ClassDecl->bases()) {
    const RecordType *BaseType = B.getType()->getAs<RecordType>();
    if (!BaseType) continue;

    CXXRecordDecl *BaseClassDecl = cast<CXXRecordDecl>(BaseType->getDecl());
    if (!specialMemberIsConstexpr(S, BaseClassDecl, CSM, 0, ConstArg,
                                  InheritedCtor, Inherited))
      return false;
  }

  //   -- every constructor involved in initializing non-static data members
  //      [...] shall be a constexpr constructor;
  //   -- every non-static data member and base class sub-object shall be
  //      initialized
  //   -- for each non-static data member of X that is of class type (or array
  //      thereof), the assignment operator selected to copy/move that member is
  //      a constexpr function
  for (const auto *F : ClassDecl->fields()) {
    if (F->isInvalidDecl())
      continue;
    if (CSM == Sema::CXXDefaultConstructor && F->hasInClassInitializer())
      continue;
    QualType BaseType = S.Context.getBaseElementType(F->getType());
    if (const RecordType *RecordTy = BaseType->getAs<RecordType>()) {
      CXXRecordDecl *FieldRecDecl = cast<CXXRecordDecl>(RecordTy->getDecl());
      if (!specialMemberIsConstexpr(S, FieldRecDecl, CSM,
                                    BaseType.getCVRQualifiers(),
                                    ConstArg && !F->isMutable()))
        return false;
    } else if (CSM == Sema::CXXDefaultConstructor) {
      return false;
    }
  }

  // All OK, it's constexpr!
  return true;
}

static Sema::ImplicitExceptionSpecification
ComputeDefaultedSpecialMemberExceptionSpec(
    Sema &S, SourceLocation Loc, CXXMethodDecl *MD, Sema::CXXSpecialMember CSM,
    Sema::InheritedConstructorInfo *ICI);

static Sema::ImplicitExceptionSpecification
computeImplicitExceptionSpec(Sema &S, SourceLocation Loc, CXXMethodDecl *MD) {
  auto CSM = S.getSpecialMember(MD);
  if (CSM != Sema::CXXInvalid)
    return ComputeDefaultedSpecialMemberExceptionSpec(S, Loc, MD, CSM, nullptr);

  auto *CD = cast<CXXConstructorDecl>(MD);
  assert(CD->getInheritedConstructor() &&
         "only special members have implicit exception specs");
  Sema::InheritedConstructorInfo ICI(
      S, Loc, CD->getInheritedConstructor().getShadowDecl());
  return ComputeDefaultedSpecialMemberExceptionSpec(
      S, Loc, CD, Sema::CXXDefaultConstructor, &ICI);
}

static FunctionProtoType::ExtProtoInfo getImplicitMethodEPI(Sema &S,
                                                            CXXMethodDecl *MD) {
  FunctionProtoType::ExtProtoInfo EPI;

  // Build an exception specification pointing back at this member.
  EPI.ExceptionSpec.Type = EST_Unevaluated;
  EPI.ExceptionSpec.SourceDecl = MD;

  // Set the calling convention to the default for C++ instance methods.
  EPI.ExtInfo = EPI.ExtInfo.withCallingConv(
      S.Context.getDefaultCallingConvention(/*IsVariadic=*/false,
                                            /*IsCXXMethod=*/true));
  return EPI;
}

void Sema::EvaluateImplicitExceptionSpec(SourceLocation Loc, CXXMethodDecl *MD) {
  const FunctionProtoType *FPT = MD->getType()->castAs<FunctionProtoType>();
  if (FPT->getExceptionSpecType() != EST_Unevaluated)
    return;

  // Evaluate the exception specification.
  auto IES = computeImplicitExceptionSpec(*this, Loc, MD);
  auto ESI = IES.getExceptionSpec();

  // Update the type of the special member to use it.
  UpdateExceptionSpec(MD, ESI);

  // A user-provided destructor can be defined outside the class. When that
  // happens, be sure to update the exception specification on both
  // declarations.
  const FunctionProtoType *CanonicalFPT =
    MD->getCanonicalDecl()->getType()->castAs<FunctionProtoType>();
  if (CanonicalFPT->getExceptionSpecType() == EST_Unevaluated)
    UpdateExceptionSpec(MD->getCanonicalDecl(), ESI);
}

void Sema::CheckExplicitlyDefaultedSpecialMember(CXXMethodDecl *MD) {
  CXXRecordDecl *RD = MD->getParent();
  CXXSpecialMember CSM = getSpecialMember(MD);

  assert(MD->isExplicitlyDefaulted() && CSM != CXXInvalid &&
         "not an explicitly-defaulted special member");

  // Whether this was the first-declared instance of the constructor.
  // This affects whether we implicitly add an exception spec and constexpr.
  bool First = MD == MD->getCanonicalDecl();

  bool HadError = false;

  // C++11 [dcl.fct.def.default]p1:
  //   A function that is explicitly defaulted shall
  //     -- be a special member function (checked elsewhere),
  //     -- have the same type (except for ref-qualifiers, and except that a
  //        copy operation can take a non-const reference) as an implicit
  //        declaration, and
  //     -- not have default arguments.
  // C++2a changes the second bullet to instead delete the function if it's
  // defaulted on its first declaration, unless it's "an assignment operator,
  // and its return type differs or its parameter type is not a reference".
  bool DeleteOnTypeMismatch = getLangOpts().CPlusPlus2a && First;
  bool ShouldDeleteForTypeMismatch = false;
  unsigned ExpectedParams = 1;
  if (CSM == CXXDefaultConstructor || CSM == CXXDestructor)
    ExpectedParams = 0;
  if (MD->getNumParams() != ExpectedParams) {
    // This checks for default arguments: a copy or move constructor with a
    // default argument is classified as a default constructor, and assignment
    // operations and destructors can't have default arguments.
    Diag(MD->getLocation(), diag::err_defaulted_special_member_params)
      << CSM << MD->getSourceRange();
    HadError = true;
  } else if (MD->isVariadic()) {
    if (DeleteOnTypeMismatch)
      ShouldDeleteForTypeMismatch = true;
    else {
      Diag(MD->getLocation(), diag::err_defaulted_special_member_variadic)
        << CSM << MD->getSourceRange();
      HadError = true;
    }
  }

  const FunctionProtoType *Type = MD->getType()->getAs<FunctionProtoType>();

  bool CanHaveConstParam = false;
  if (CSM == CXXCopyConstructor)
    CanHaveConstParam = RD->implicitCopyConstructorHasConstParam();
  else if (CSM == CXXCopyAssignment)
    CanHaveConstParam = RD->implicitCopyAssignmentHasConstParam();

  QualType ReturnType = Context.VoidTy;
  if (CSM == CXXCopyAssignment || CSM == CXXMoveAssignment) {
    // Check for return type matching.
    ReturnType = Type->getReturnType();

    QualType DeclType = Context.getTypeDeclType(RD);
    DeclType = Context.getAddrSpaceQualType(DeclType, MD->getTypeQualifiers().getAddressSpace());
    QualType ExpectedReturnType = Context.getLValueReferenceType(DeclType);

    if (!Context.hasSameType(ReturnType, ExpectedReturnType)) {
      Diag(MD->getLocation(), diag::err_defaulted_special_member_return_type)
        << (CSM == CXXMoveAssignment) << ExpectedReturnType;
      HadError = true;
    }

    // A defaulted special member cannot have cv-qualifiers.
    if (Type->getTypeQuals().hasConst() || Type->getTypeQuals().hasVolatile()) {
      if (DeleteOnTypeMismatch)
        ShouldDeleteForTypeMismatch = true;
      else {
        Diag(MD->getLocation(), diag::err_defaulted_special_member_quals)
          << (CSM == CXXMoveAssignment) << getLangOpts().CPlusPlus14;
        HadError = true;
      }
    }
  }

  // Check for parameter type matching.
  QualType ArgType = ExpectedParams ? Type->getParamType(0) : QualType();
  bool HasConstParam = false;
  if (ExpectedParams && ArgType->isReferenceType()) {
    // Argument must be reference to possibly-const T.
    QualType ReferentType = ArgType->getPointeeType();
    HasConstParam = ReferentType.isConstQualified();

    if (ReferentType.isVolatileQualified()) {
      if (DeleteOnTypeMismatch)
        ShouldDeleteForTypeMismatch = true;
      else {
        Diag(MD->getLocation(),
             diag::err_defaulted_special_member_volatile_param) << CSM;
        HadError = true;
      }
    }

    if (HasConstParam && !CanHaveConstParam) {
      if (DeleteOnTypeMismatch)
        ShouldDeleteForTypeMismatch = true;
      else if (CSM == CXXCopyConstructor || CSM == CXXCopyAssignment) {
        Diag(MD->getLocation(),
             diag::err_defaulted_special_member_copy_const_param)
          << (CSM == CXXCopyAssignment);
        // FIXME: Explain why this special member can't be const.
        HadError = true;
      } else {
        Diag(MD->getLocation(),
             diag::err_defaulted_special_member_move_const_param)
          << (CSM == CXXMoveAssignment);
        HadError = true;
      }
    }
  } else if (ExpectedParams) {
    // A copy assignment operator can take its argument by value, but a
    // defaulted one cannot.
    assert(CSM == CXXCopyAssignment && "unexpected non-ref argument");
    Diag(MD->getLocation(), diag::err_defaulted_copy_assign_not_ref);
    HadError = true;
  }

  // C++11 [dcl.fct.def.default]p2:
  //   An explicitly-defaulted function may be declared constexpr only if it
  //   would have been implicitly declared as constexpr,
  // Do not apply this rule to members of class templates, since core issue 1358
  // makes such functions always instantiate to constexpr functions. For
  // functions which cannot be constexpr (for non-constructors in C++11 and for
  // destructors in C++1y), this is checked elsewhere.
  bool Constexpr = defaultedSpecialMemberIsConstexpr(*this, RD, CSM,
                                                     HasConstParam);
  if ((getLangOpts().CPlusPlus14 ? !isa<CXXDestructorDecl>(MD)
                                 : isa<CXXConstructorDecl>(MD)) &&
      MD->isConstexpr() && !Constexpr &&
      MD->getTemplatedKind() == FunctionDecl::TK_NonTemplate) {
    Diag(MD->getBeginLoc(), diag::err_incorrect_defaulted_constexpr) << CSM;
    // FIXME: Explain why the special member can't be constexpr.
    HadError = true;
  }

  //   and may have an explicit exception-specification only if it is compatible
  //   with the exception-specification on the implicit declaration.
  if (Type->hasExceptionSpec()) {
    // Delay the check if this is the first declaration of the special member,
    // since we may not have parsed some necessary in-class initializers yet.
    if (First) {
      // If the exception specification needs to be instantiated, do so now,
      // before we clobber it with an EST_Unevaluated specification below.
      if (Type->getExceptionSpecType() == EST_Uninstantiated) {
        InstantiateExceptionSpec(MD->getBeginLoc(), MD);
        Type = MD->getType()->getAs<FunctionProtoType>();
      }
      DelayedDefaultedMemberExceptionSpecs.push_back(std::make_pair(MD, Type));
    } else
      CheckExplicitlyDefaultedMemberExceptionSpec(MD, Type);
  }

  //   If a function is explicitly defaulted on its first declaration,
  if (First) {
    //  -- it is implicitly considered to be constexpr if the implicit
    //     definition would be,
    MD->setConstexpr(Constexpr);

    //  -- it is implicitly considered to have the same exception-specification
    //     as if it had been implicitly declared,
    FunctionProtoType::ExtProtoInfo EPI = Type->getExtProtoInfo();
    EPI.ExceptionSpec.Type = EST_Unevaluated;
    EPI.ExceptionSpec.SourceDecl = MD;
    MD->setType(Context.getFunctionType(ReturnType,
                                        llvm::makeArrayRef(&ArgType,
                                                           ExpectedParams),
                                        EPI));
  }

  if (ShouldDeleteForTypeMismatch || ShouldDeleteSpecialMember(MD, CSM)) {
    if (First) {
      SetDeclDeleted(MD, MD->getLocation());
      if (!inTemplateInstantiation() && !HadError) {
        Diag(MD->getLocation(), diag::warn_defaulted_method_deleted) << CSM;
        if (ShouldDeleteForTypeMismatch) {
          Diag(MD->getLocation(), diag::note_deleted_type_mismatch) << CSM;
        } else {
          ShouldDeleteSpecialMember(MD, CSM, nullptr, /*Diagnose*/true);
        }
      }
      if (ShouldDeleteForTypeMismatch && !HadError) {
        Diag(MD->getLocation(),
             diag::warn_cxx17_compat_defaulted_method_type_mismatch) << CSM;
      }
    } else {
      // C++11 [dcl.fct.def.default]p4:
      //   [For a] user-provided explicitly-defaulted function [...] if such a
      //   function is implicitly defined as deleted, the program is ill-formed.
      Diag(MD->getLocation(), diag::err_out_of_line_default_deletes) << CSM;
      assert(!ShouldDeleteForTypeMismatch && "deleted non-first decl");
      ShouldDeleteSpecialMember(MD, CSM, nullptr, /*Diagnose*/true);
      HadError = true;
    }
  }

  if (HadError)
    MD->setInvalidDecl();
}

/// Check whether the exception specification provided for an
/// explicitly-defaulted special member matches the exception specification
/// that would have been generated for an implicit special member, per
/// C++11 [dcl.fct.def.default]p2.
void Sema::CheckExplicitlyDefaultedMemberExceptionSpec(
    CXXMethodDecl *MD, const FunctionProtoType *SpecifiedType) {
  // If the exception specification was explicitly specified but hadn't been
  // parsed when the method was defaulted, grab it now.
  if (SpecifiedType->getExceptionSpecType() == EST_Unparsed)
    SpecifiedType =
        MD->getTypeSourceInfo()->getType()->castAs<FunctionProtoType>();

  // Compute the implicit exception specification.
  CallingConv CC = Context.getDefaultCallingConvention(/*IsVariadic=*/false,
                                                       /*IsCXXMethod=*/true);
  FunctionProtoType::ExtProtoInfo EPI(CC);
  auto IES = computeImplicitExceptionSpec(*this, MD->getLocation(), MD);
  EPI.ExceptionSpec = IES.getExceptionSpec();
  const FunctionProtoType *ImplicitType = cast<FunctionProtoType>(
    Context.getFunctionType(Context.VoidTy, None, EPI));

  // Ensure that it matches.
  CheckEquivalentExceptionSpec(
    PDiag(diag::err_incorrect_defaulted_exception_spec)
      << getSpecialMember(MD), PDiag(),
    ImplicitType, SourceLocation(),
    SpecifiedType, MD->getLocation());
}

void Sema::CheckDelayedMemberExceptionSpecs() {
  decltype(DelayedOverridingExceptionSpecChecks) Overriding;
  decltype(DelayedEquivalentExceptionSpecChecks) Equivalent;
  decltype(DelayedDefaultedMemberExceptionSpecs) Defaulted;

  std::swap(Overriding, DelayedOverridingExceptionSpecChecks);
  std::swap(Equivalent, DelayedEquivalentExceptionSpecChecks);
  std::swap(Defaulted, DelayedDefaultedMemberExceptionSpecs);

  // Perform any deferred checking of exception specifications for virtual
  // destructors.
  for (auto &Check : Overriding)
    CheckOverridingFunctionExceptionSpec(Check.first, Check.second);

  // Perform any deferred checking of exception specifications for befriended
  // special members.
  for (auto &Check : Equivalent)
    CheckEquivalentExceptionSpec(Check.second, Check.first);

  // Check that any explicitly-defaulted methods have exception specifications
  // compatible with their implicit exception specifications.
  for (auto &Spec : Defaulted)
    CheckExplicitlyDefaultedMemberExceptionSpec(Spec.first, Spec.second);
}

namespace {
/// CRTP base class for visiting operations performed by a special member
/// function (or inherited constructor).
template<typename Derived>
struct SpecialMemberVisitor {
  Sema &S;
  CXXMethodDecl *MD;
  Sema::CXXSpecialMember CSM;
  Sema::InheritedConstructorInfo *ICI;

  // Properties of the special member, computed for convenience.
  bool IsConstructor = false, IsAssignment = false, ConstArg = false;

  SpecialMemberVisitor(Sema &S, CXXMethodDecl *MD, Sema::CXXSpecialMember CSM,
                       Sema::InheritedConstructorInfo *ICI)
      : S(S), MD(MD), CSM(CSM), ICI(ICI) {
    switch (CSM) {
    case Sema::CXXDefaultConstructor:
    case Sema::CXXCopyConstructor:
    case Sema::CXXMoveConstructor:
      IsConstructor = true;
      break;
    case Sema::CXXCopyAssignment:
    case Sema::CXXMoveAssignment:
      IsAssignment = true;
      break;
    case Sema::CXXDestructor:
      break;
    case Sema::CXXInvalid:
      llvm_unreachable("invalid special member kind");
    }

    if (MD->getNumParams()) {
      if (const ReferenceType *RT =
              MD->getParamDecl(0)->getType()->getAs<ReferenceType>())
        ConstArg = RT->getPointeeType().isConstQualified();
    }
  }

  Derived &getDerived() { return static_cast<Derived&>(*this); }

  /// Is this a "move" special member?
  bool isMove() const {
    return CSM == Sema::CXXMoveConstructor || CSM == Sema::CXXMoveAssignment;
  }

  /// Look up the corresponding special member in the given class.
  Sema::SpecialMemberOverloadResult lookupIn(CXXRecordDecl *Class,
                                             unsigned Quals, bool IsMutable) {
    return lookupCallFromSpecialMember(S, Class, CSM, Quals,
                                       ConstArg && !IsMutable);
  }

  /// Look up the constructor for the specified base class to see if it's
  /// overridden due to this being an inherited constructor.
  Sema::SpecialMemberOverloadResult lookupInheritedCtor(CXXRecordDecl *Class) {
    if (!ICI)
      return {};
    assert(CSM == Sema::CXXDefaultConstructor);
    auto *BaseCtor =
      cast<CXXConstructorDecl>(MD)->getInheritedConstructor().getConstructor();
    if (auto *MD = ICI->findConstructorForBase(Class, BaseCtor).first)
      return MD;
    return {};
  }

  /// A base or member subobject.
  typedef llvm::PointerUnion<CXXBaseSpecifier*, FieldDecl*> Subobject;

  /// Get the location to use for a subobject in diagnostics.
  static SourceLocation getSubobjectLoc(Subobject Subobj) {
    // FIXME: For an indirect virtual base, the direct base leading to
    // the indirect virtual base would be a more useful choice.
    if (auto *B = Subobj.dyn_cast<CXXBaseSpecifier*>())
      return B->getBaseTypeLoc();
    else
      return Subobj.get<FieldDecl*>()->getLocation();
  }

  enum BasesToVisit {
    /// Visit all non-virtual (direct) bases.
    VisitNonVirtualBases,
    /// Visit all direct bases, virtual or not.
    VisitDirectBases,
    /// Visit all non-virtual bases, and all virtual bases if the class
    /// is not abstract.
    VisitPotentiallyConstructedBases,
    /// Visit all direct or virtual bases.
    VisitAllBases
  };

  // Visit the bases and members of the class.
  bool visit(BasesToVisit Bases) {
    CXXRecordDecl *RD = MD->getParent();

    if (Bases == VisitPotentiallyConstructedBases)
      Bases = RD->isAbstract() ? VisitNonVirtualBases : VisitAllBases;

    for (auto &B : RD->bases())
      if ((Bases == VisitDirectBases || !B.isVirtual()) &&
          getDerived().visitBase(&B))
        return true;

    if (Bases == VisitAllBases)
      for (auto &B : RD->vbases())
        if (getDerived().visitBase(&B))
          return true;

    for (auto *F : RD->fields())
      if (!F->isInvalidDecl() && !F->isUnnamedBitfield() &&
          getDerived().visitField(F))
        return true;

    return false;
  }
};
}

namespace {
struct SpecialMemberDeletionInfo
    : SpecialMemberVisitor<SpecialMemberDeletionInfo> {
  bool Diagnose;

  SourceLocation Loc;

  bool AllFieldsAreConst;

  SpecialMemberDeletionInfo(Sema &S, CXXMethodDecl *MD,
                            Sema::CXXSpecialMember CSM,
                            Sema::InheritedConstructorInfo *ICI, bool Diagnose)
      : SpecialMemberVisitor(S, MD, CSM, ICI), Diagnose(Diagnose),
        Loc(MD->getLocation()), AllFieldsAreConst(true) {}

  bool inUnion() const { return MD->getParent()->isUnion(); }

  Sema::CXXSpecialMember getEffectiveCSM() {
    return ICI ? Sema::CXXInvalid : CSM;
  }

  bool visitBase(CXXBaseSpecifier *Base) { return shouldDeleteForBase(Base); }
  bool visitField(FieldDecl *Field) { return shouldDeleteForField(Field); }

  bool shouldDeleteForBase(CXXBaseSpecifier *Base);
  bool shouldDeleteForField(FieldDecl *FD);
  bool shouldDeleteForAllConstMembers();

  bool shouldDeleteForClassSubobject(CXXRecordDecl *Class, Subobject Subobj,
                                     unsigned Quals);
  bool shouldDeleteForSubobjectCall(Subobject Subobj,
                                    Sema::SpecialMemberOverloadResult SMOR,
                                    bool IsDtorCallInCtor);

  bool isAccessible(Subobject Subobj, CXXMethodDecl *D);
};
}

/// Is the given special member inaccessible when used on the given
/// sub-object.
bool SpecialMemberDeletionInfo::isAccessible(Subobject Subobj,
                                             CXXMethodDecl *target) {
  /// If we're operating on a base class, the object type is the
  /// type of this special member.
  QualType objectTy;
  AccessSpecifier access = target->getAccess();
  if (CXXBaseSpecifier *base = Subobj.dyn_cast<CXXBaseSpecifier*>()) {
    objectTy = S.Context.getTypeDeclType(MD->getParent());
    access = CXXRecordDecl::MergeAccess(base->getAccessSpecifier(), access);

  // If we're operating on a field, the object type is the type of the field.
  } else {
    objectTy = S.Context.getTypeDeclType(target->getParent());
  }

  return S.isSpecialMemberAccessibleForDeletion(target, access, objectTy);
}

/// Check whether we should delete a special member due to the implicit
/// definition containing a call to a special member of a subobject.
bool SpecialMemberDeletionInfo::shouldDeleteForSubobjectCall(
    Subobject Subobj, Sema::SpecialMemberOverloadResult SMOR,
    bool IsDtorCallInCtor) {
  CXXMethodDecl *Decl = SMOR.getMethod();
  FieldDecl *Field = Subobj.dyn_cast<FieldDecl*>();

  int DiagKind = -1;

  if (SMOR.getKind() == Sema::SpecialMemberOverloadResult::NoMemberOrDeleted)
    DiagKind = !Decl ? 0 : 1;
  else if (SMOR.getKind() == Sema::SpecialMemberOverloadResult::Ambiguous)
    DiagKind = 2;
  else if (!isAccessible(Subobj, Decl))
    DiagKind = 3;
  else if (!IsDtorCallInCtor && Field && Field->getParent()->isUnion() &&
           !Decl->isTrivial()) {
    // A member of a union must have a trivial corresponding special member.
    // As a weird special case, a destructor call from a union's constructor
    // must be accessible and non-deleted, but need not be trivial. Such a
    // destructor is never actually called, but is semantically checked as
    // if it were.
    DiagKind = 4;
  }

  if (DiagKind == -1)
    return false;

  if (Diagnose) {
    if (Field) {
      S.Diag(Field->getLocation(),
             diag::note_deleted_special_member_class_subobject)
        << getEffectiveCSM() << MD->getParent() << /*IsField*/true
        << Field << DiagKind << IsDtorCallInCtor;
    } else {
      CXXBaseSpecifier *Base = Subobj.get<CXXBaseSpecifier*>();
      S.Diag(Base->getBeginLoc(),
             diag::note_deleted_special_member_class_subobject)
          << getEffectiveCSM() << MD->getParent() << /*IsField*/ false
          << Base->getType() << DiagKind << IsDtorCallInCtor;
    }

    if (DiagKind == 1)
      S.NoteDeletedFunction(Decl);
    // FIXME: Explain inaccessibility if DiagKind == 3.
  }

  return true;
}

/// Check whether we should delete a special member function due to having a
/// direct or virtual base class or non-static data member of class type M.
bool SpecialMemberDeletionInfo::shouldDeleteForClassSubobject(
    CXXRecordDecl *Class, Subobject Subobj, unsigned Quals) {
  FieldDecl *Field = Subobj.dyn_cast<FieldDecl*>();
  bool IsMutable = Field && Field->isMutable();

  // C++11 [class.ctor]p5:
  // -- any direct or virtual base class, or non-static data member with no
  //    brace-or-equal-initializer, has class type M (or array thereof) and
  //    either M has no default constructor or overload resolution as applied
  //    to M's default constructor results in an ambiguity or in a function
  //    that is deleted or inaccessible
  // C++11 [class.copy]p11, C++11 [class.copy]p23:
  // -- a direct or virtual base class B that cannot be copied/moved because
  //    overload resolution, as applied to B's corresponding special member,
  //    results in an ambiguity or a function that is deleted or inaccessible
  //    from the defaulted special member
  // C++11 [class.dtor]p5:
  // -- any direct or virtual base class [...] has a type with a destructor
  //    that is deleted or inaccessible
  if (!(CSM == Sema::CXXDefaultConstructor &&
        Field && Field->hasInClassInitializer()) &&
      shouldDeleteForSubobjectCall(Subobj, lookupIn(Class, Quals, IsMutable),
                                   false))
    return true;

  // C++11 [class.ctor]p5, C++11 [class.copy]p11:
  // -- any direct or virtual base class or non-static data member has a
  //    type with a destructor that is deleted or inaccessible
  if (IsConstructor) {
    Sema::SpecialMemberOverloadResult SMOR =
        S.LookupSpecialMember(Class, Sema::CXXDestructor,
                              false, false, false, false, false);
    if (shouldDeleteForSubobjectCall(Subobj, SMOR, true))
      return true;
  }

  return false;
}

/// Check whether we should delete a special member function due to the class
/// having a particular direct or virtual base class.
bool SpecialMemberDeletionInfo::shouldDeleteForBase(CXXBaseSpecifier *Base) {
  CXXRecordDecl *BaseClass = Base->getType()->getAsCXXRecordDecl();
  // If program is correct, BaseClass cannot be null, but if it is, the error
  // must be reported elsewhere.
  if (!BaseClass)
    return false;
  // If we have an inheriting constructor, check whether we're calling an
  // inherited constructor instead of a default constructor.
  Sema::SpecialMemberOverloadResult SMOR = lookupInheritedCtor(BaseClass);
  if (auto *BaseCtor = SMOR.getMethod()) {
    // Note that we do not check access along this path; other than that,
    // this is the same as shouldDeleteForSubobjectCall(Base, BaseCtor, false);
    // FIXME: Check that the base has a usable destructor! Sink this into
    // shouldDeleteForClassSubobject.
    if (BaseCtor->isDeleted() && Diagnose) {
      S.Diag(Base->getBeginLoc(),
             diag::note_deleted_special_member_class_subobject)
          << getEffectiveCSM() << MD->getParent() << /*IsField*/ false
          << Base->getType() << /*Deleted*/ 1 << /*IsDtorCallInCtor*/ false;
      S.NoteDeletedFunction(BaseCtor);
    }
    return BaseCtor->isDeleted();
  }
  return shouldDeleteForClassSubobject(BaseClass, Base, 0);
}

/// Check whether we should delete a special member function due to the class
/// having a particular non-static data member.
bool SpecialMemberDeletionInfo::shouldDeleteForField(FieldDecl *FD) {
  QualType FieldType = S.Context.getBaseElementType(FD->getType());
  CXXRecordDecl *FieldRecord = FieldType->getAsCXXRecordDecl();

  if (CSM == Sema::CXXDefaultConstructor) {
    // For a default constructor, all references must be initialized in-class
    // and, if a union, it must have a non-const member.
    if (FieldType->isReferenceType() && !FD->hasInClassInitializer()) {
      if (Diagnose)
        S.Diag(FD->getLocation(), diag::note_deleted_default_ctor_uninit_field)
          << !!ICI << MD->getParent() << FD << FieldType << /*Reference*/0;
      return true;
    }
    // C++11 [class.ctor]p5: any non-variant non-static data member of
    // const-qualified type (or array thereof) with no
    // brace-or-equal-initializer does not have a user-provided default
    // constructor.
    if (!inUnion() && FieldType.isConstQualified() &&
        !FD->hasInClassInitializer() &&
        (!FieldRecord || !FieldRecord->hasUserProvidedDefaultConstructor())) {
      if (Diagnose)
        S.Diag(FD->getLocation(), diag::note_deleted_default_ctor_uninit_field)
          << !!ICI << MD->getParent() << FD << FD->getType() << /*Const*/1;
      return true;
    }

    if (inUnion() && !FieldType.isConstQualified())
      AllFieldsAreConst = false;
  } else if (CSM == Sema::CXXCopyConstructor) {
    // For a copy constructor, data members must not be of rvalue reference
    // type.
    if (FieldType->isRValueReferenceType()) {
      if (Diagnose)
        S.Diag(FD->getLocation(), diag::note_deleted_copy_ctor_rvalue_reference)
          << MD->getParent() << FD << FieldType;
      return true;
    }
  } else if (IsAssignment) {
    // For an assignment operator, data members must not be of reference type.
    if (FieldType->isReferenceType()) {
      if (Diagnose)
        S.Diag(FD->getLocation(), diag::note_deleted_assign_field)
          << isMove() << MD->getParent() << FD << FieldType << /*Reference*/0;
      return true;
    }
    if (!FieldRecord && FieldType.isConstQualified()) {
      // C++11 [class.copy]p23:
      // -- a non-static data member of const non-class type (or array thereof)
      if (Diagnose)
        S.Diag(FD->getLocation(), diag::note_deleted_assign_field)
          << isMove() << MD->getParent() << FD << FD->getType() << /*Const*/1;
      return true;
    }
  }

  if (FieldRecord) {
    // Some additional restrictions exist on the variant members.
    if (!inUnion() && FieldRecord->isUnion() &&
        FieldRecord->isAnonymousStructOrUnion()) {
      bool AllVariantFieldsAreConst = true;

      // FIXME: Handle anonymous unions declared within anonymous unions.
      for (auto *UI : FieldRecord->fields()) {
        QualType UnionFieldType = S.Context.getBaseElementType(UI->getType());

        if (!UnionFieldType.isConstQualified())
          AllVariantFieldsAreConst = false;

        CXXRecordDecl *UnionFieldRecord = UnionFieldType->getAsCXXRecordDecl();
        if (UnionFieldRecord &&
            shouldDeleteForClassSubobject(UnionFieldRecord, UI,
                                          UnionFieldType.getCVRQualifiers()))
          return true;
      }

      // At least one member in each anonymous union must be non-const
      if (CSM == Sema::CXXDefaultConstructor && AllVariantFieldsAreConst &&
          !FieldRecord->field_empty()) {
        if (Diagnose)
          S.Diag(FieldRecord->getLocation(),
                 diag::note_deleted_default_ctor_all_const)
            << !!ICI << MD->getParent() << /*anonymous union*/1;
        return true;
      }

      // Don't check the implicit member of the anonymous union type.
      // This is technically non-conformant, but sanity demands it.
      return false;
    }

    if (shouldDeleteForClassSubobject(FieldRecord, FD,
                                      FieldType.getCVRQualifiers()))
      return true;
  }

  return false;
}

/// C++11 [class.ctor] p5:
///   A defaulted default constructor for a class X is defined as deleted if
/// X is a union and all of its variant members are of const-qualified type.
bool SpecialMemberDeletionInfo::shouldDeleteForAllConstMembers() {
  // This is a silly definition, because it gives an empty union a deleted
  // default constructor. Don't do that.
  if (CSM == Sema::CXXDefaultConstructor && inUnion() && AllFieldsAreConst) {
    bool AnyFields = false;
    for (auto *F : MD->getParent()->fields())
      if ((AnyFields = !F->isUnnamedBitfield()))
        break;
    if (!AnyFields)
      return false;
    if (Diagnose)
      S.Diag(MD->getParent()->getLocation(),
             diag::note_deleted_default_ctor_all_const)
        << !!ICI << MD->getParent() << /*not anonymous union*/0;
    return true;
  }
  return false;
}

/// Determine whether a defaulted special member function should be defined as
/// deleted, as specified in C++11 [class.ctor]p5, C++11 [class.copy]p11,
/// C++11 [class.copy]p23, and C++11 [class.dtor]p5.
bool Sema::ShouldDeleteSpecialMember(CXXMethodDecl *MD, CXXSpecialMember CSM,
                                     InheritedConstructorInfo *ICI,
                                     bool Diagnose) {
  if (MD->isInvalidDecl())
    return false;
  CXXRecordDecl *RD = MD->getParent();
  assert(!RD->isDependentType() && "do deletion after instantiation");
  if (!LangOpts.CPlusPlus11 || RD->isInvalidDecl())
    return false;

  // C++11 [expr.lambda.prim]p19:
  //   The closure type associated with a lambda-expression has a
  //   deleted (8.4.3) default constructor and a deleted copy
  //   assignment operator.
  // C++2a adds back these operators if the lambda has no capture-default.
  if (RD->isLambda() && !RD->lambdaIsDefaultConstructibleAndAssignable() &&
      (CSM == CXXDefaultConstructor || CSM == CXXCopyAssignment)) {
    if (Diagnose)
      Diag(RD->getLocation(), diag::note_lambda_decl);
    return true;
  }

  // For an anonymous struct or union, the copy and assignment special members
  // will never be used, so skip the check. For an anonymous union declared at
  // namespace scope, the constructor and destructor are used.
  if (CSM != CXXDefaultConstructor && CSM != CXXDestructor &&
      RD->isAnonymousStructOrUnion())
    return false;

  // C++11 [class.copy]p7, p18:
  //   If the class definition declares a move constructor or move assignment
  //   operator, an implicitly declared copy constructor or copy assignment
  //   operator is defined as deleted.
  if (MD->isImplicit() &&
      (CSM == CXXCopyConstructor || CSM == CXXCopyAssignment)) {
    CXXMethodDecl *UserDeclaredMove = nullptr;

    // In Microsoft mode up to MSVC 2013, a user-declared move only causes the
    // deletion of the corresponding copy operation, not both copy operations.
    // MSVC 2015 has adopted the standards conforming behavior.
    bool DeletesOnlyMatchingCopy =
        getLangOpts().MSVCCompat &&
        !getLangOpts().isCompatibleWithMSVC(LangOptions::MSVC2015);

    if (RD->hasUserDeclaredMoveConstructor() &&
        (!DeletesOnlyMatchingCopy || CSM == CXXCopyConstructor)) {
      if (!Diagnose) return true;

      // Find any user-declared move constructor.
      for (auto *I : RD->ctors()) {
        if (I->isMoveConstructor()) {
          UserDeclaredMove = I;
          break;
        }
      }
      assert(UserDeclaredMove);
    } else if (RD->hasUserDeclaredMoveAssignment() &&
               (!DeletesOnlyMatchingCopy || CSM == CXXCopyAssignment)) {
      if (!Diagnose) return true;

      // Find any user-declared move assignment operator.
      for (auto *I : RD->methods()) {
        if (I->isMoveAssignmentOperator()) {
          UserDeclaredMove = I;
          break;
        }
      }
      assert(UserDeclaredMove);
    }

    if (UserDeclaredMove) {
      Diag(UserDeclaredMove->getLocation(),
           diag::note_deleted_copy_user_declared_move)
        << (CSM == CXXCopyAssignment) << RD
        << UserDeclaredMove->isMoveAssignmentOperator();
      return true;
    }
  }

  // Do access control from the special member function
  ContextRAII MethodContext(*this, MD);

  // C++11 [class.dtor]p5:
  // -- for a virtual destructor, lookup of the non-array deallocation function
  //    results in an ambiguity or in a function that is deleted or inaccessible
  if (CSM == CXXDestructor && MD->isVirtual()) {
    FunctionDecl *OperatorDelete = nullptr;
    DeclarationName Name =
      Context.DeclarationNames.getCXXOperatorName(OO_Delete);
    if (FindDeallocationFunction(MD->getLocation(), MD->getParent(), Name,
                                 OperatorDelete, /*Diagnose*/false)) {
      if (Diagnose)
        Diag(RD->getLocation(), diag::note_deleted_dtor_no_operator_delete);
      return true;
    }
  }

  SpecialMemberDeletionInfo SMI(*this, MD, CSM, ICI, Diagnose);

  // Per DR1611, do not consider virtual bases of constructors of abstract
  // classes, since we are not going to construct them.
  // Per DR1658, do not consider virtual bases of destructors of abstract
  // classes either.
  // Per DR2180, for assignment operators we only assign (and thus only
  // consider) direct bases.
  if (SMI.visit(SMI.IsAssignment ? SMI.VisitDirectBases
                                 : SMI.VisitPotentiallyConstructedBases))
    return true;

  if (SMI.shouldDeleteForAllConstMembers())
    return true;

  if (getLangOpts().CUDA) {
    // We should delete the special member in CUDA mode if target inference
    // failed.
    // For inherited constructors (non-null ICI), CSM may be passed so that MD
    // is treated as certain special member, which may not reflect what special
    // member MD really is. However inferCUDATargetForImplicitSpecialMember
    // expects CSM to match MD, therefore recalculate CSM.
    assert(ICI || CSM == getSpecialMember(MD));
    auto RealCSM = CSM;
    if (ICI)
      RealCSM = getSpecialMember(MD);

    return inferCUDATargetForImplicitSpecialMember(RD, RealCSM, MD,
                                                   SMI.ConstArg, Diagnose);
  }

  return false;
}

/// Perform lookup for a special member of the specified kind, and determine
/// whether it is trivial. If the triviality can be determined without the
/// lookup, skip it. This is intended for use when determining whether a
/// special member of a containing object is trivial, and thus does not ever
/// perform overload resolution for default constructors.
///
/// If \p Selected is not \c NULL, \c *Selected will be filled in with the
/// member that was most likely to be intended to be trivial, if any.
///
/// If \p ForCall is true, look at CXXRecord::HasTrivialSpecialMembersForCall to
/// determine whether the special member is trivial.
static bool findTrivialSpecialMember(Sema &S, CXXRecordDecl *RD,
                                     Sema::CXXSpecialMember CSM, unsigned Quals,
                                     bool ConstRHS,
                                     Sema::TrivialABIHandling TAH,
                                     CXXMethodDecl **Selected) {
  if (Selected)
    *Selected = nullptr;

  switch (CSM) {
  case Sema::CXXInvalid:
    llvm_unreachable("not a special member");

  case Sema::CXXDefaultConstructor:
    // C++11 [class.ctor]p5:
    //   A default constructor is trivial if:
    //    - all the [direct subobjects] have trivial default constructors
    //
    // Note, no overload resolution is performed in this case.
    if (RD->hasTrivialDefaultConstructor())
      return true;

    if (Selected) {
      // If there's a default constructor which could have been trivial, dig it
      // out. Otherwise, if there's any user-provided default constructor, point
      // to that as an example of why there's not a trivial one.
      CXXConstructorDecl *DefCtor = nullptr;
      if (RD->needsImplicitDefaultConstructor())
        S.DeclareImplicitDefaultConstructor(RD);
      for (auto *CI : RD->ctors()) {
        if (!CI->isDefaultConstructor())
          continue;
        DefCtor = CI;
        if (!DefCtor->isUserProvided())
          break;
      }

      *Selected = DefCtor;
    }

    return false;

  case Sema::CXXDestructor:
    // C++11 [class.dtor]p5:
    //   A destructor is trivial if:
    //    - all the direct [subobjects] have trivial destructors
    if (RD->hasTrivialDestructor() ||
        (TAH == Sema::TAH_ConsiderTrivialABI &&
         RD->hasTrivialDestructorForCall()))
      return true;

    if (Selected) {
      if (RD->needsImplicitDestructor())
        S.DeclareImplicitDestructor(RD);
      *Selected = RD->getDestructor();
    }

    return false;

  case Sema::CXXCopyConstructor:
    // C++11 [class.copy]p12:
    //   A copy constructor is trivial if:
    //    - the constructor selected to copy each direct [subobject] is trivial
    if (RD->hasTrivialCopyConstructor() ||
        (TAH == Sema::TAH_ConsiderTrivialABI &&
         RD->hasTrivialCopyConstructorForCall())) {
      if (Quals == Qualifiers::Const)
        // We must either select the trivial copy constructor or reach an
        // ambiguity; no need to actually perform overload resolution.
        return true;
    } else if (!Selected) {
      return false;
    }
    // In C++98, we are not supposed to perform overload resolution here, but we
    // treat that as a language defect, as suggested on cxx-abi-dev, to treat
    // cases like B as having a non-trivial copy constructor:
    //   struct A { template<typename T> A(T&); };
    //   struct B { mutable A a; };
    goto NeedOverloadResolution;

  case Sema::CXXCopyAssignment:
    // C++11 [class.copy]p25:
    //   A copy assignment operator is trivial if:
    //    - the assignment operator selected to copy each direct [subobject] is
    //      trivial
    if (RD->hasTrivialCopyAssignment()) {
      if (Quals == Qualifiers::Const)
        return true;
    } else if (!Selected) {
      return false;
    }
    // In C++98, we are not supposed to perform overload resolution here, but we
    // treat that as a language defect.
    goto NeedOverloadResolution;

  case Sema::CXXMoveConstructor:
  case Sema::CXXMoveAssignment:
  NeedOverloadResolution:
    Sema::SpecialMemberOverloadResult SMOR =
        lookupCallFromSpecialMember(S, RD, CSM, Quals, ConstRHS);

    // The standard doesn't describe how to behave if the lookup is ambiguous.
    // We treat it as not making the member non-trivial, just like the standard
    // mandates for the default constructor. This should rarely matter, because
    // the member will also be deleted.
    if (SMOR.getKind() == Sema::SpecialMemberOverloadResult::Ambiguous)
      return true;

    if (!SMOR.getMethod()) {
      assert(SMOR.getKind() ==
             Sema::SpecialMemberOverloadResult::NoMemberOrDeleted);
      return false;
    }

    // We deliberately don't check if we found a deleted special member. We're
    // not supposed to!
    if (Selected)
      *Selected = SMOR.getMethod();

    if (TAH == Sema::TAH_ConsiderTrivialABI &&
        (CSM == Sema::CXXCopyConstructor || CSM == Sema::CXXMoveConstructor))
      return SMOR.getMethod()->isTrivialForCall();
    return SMOR.getMethod()->isTrivial();
  }

  llvm_unreachable("unknown special method kind");
}

static CXXConstructorDecl *findUserDeclaredCtor(CXXRecordDecl *RD) {
  for (auto *CI : RD->ctors())
    if (!CI->isImplicit())
      return CI;

  // Look for constructor templates.
  typedef CXXRecordDecl::specific_decl_iterator<FunctionTemplateDecl> tmpl_iter;
  for (tmpl_iter TI(RD->decls_begin()), TE(RD->decls_end()); TI != TE; ++TI) {
    if (CXXConstructorDecl *CD =
          dyn_cast<CXXConstructorDecl>(TI->getTemplatedDecl()))
      return CD;
  }

  return nullptr;
}

/// The kind of subobject we are checking for triviality. The values of this
/// enumeration are used in diagnostics.
enum TrivialSubobjectKind {
  /// The subobject is a base class.
  TSK_BaseClass,
  /// The subobject is a non-static data member.
  TSK_Field,
  /// The object is actually the complete object.
  TSK_CompleteObject
};

/// Check whether the special member selected for a given type would be trivial.
static bool checkTrivialSubobjectCall(Sema &S, SourceLocation SubobjLoc,
                                      QualType SubType, bool ConstRHS,
                                      Sema::CXXSpecialMember CSM,
                                      TrivialSubobjectKind Kind,
                                      Sema::TrivialABIHandling TAH, bool Diagnose) {
  CXXRecordDecl *SubRD = SubType->getAsCXXRecordDecl();
  if (!SubRD)
    return true;

  CXXMethodDecl *Selected;
  if (findTrivialSpecialMember(S, SubRD, CSM, SubType.getCVRQualifiers(),
                               ConstRHS, TAH, Diagnose ? &Selected : nullptr))
    return true;

  if (Diagnose) {
    if (ConstRHS)
      SubType.addConst();

    if (!Selected && CSM == Sema::CXXDefaultConstructor) {
      S.Diag(SubobjLoc, diag::note_nontrivial_no_def_ctor)
        << Kind << SubType.getUnqualifiedType();
      if (CXXConstructorDecl *CD = findUserDeclaredCtor(SubRD))
        S.Diag(CD->getLocation(), diag::note_user_declared_ctor);
    } else if (!Selected)
      S.Diag(SubobjLoc, diag::note_nontrivial_no_copy)
        << Kind << SubType.getUnqualifiedType() << CSM << SubType;
    else if (Selected->isUserProvided()) {
      if (Kind == TSK_CompleteObject)
        S.Diag(Selected->getLocation(), diag::note_nontrivial_user_provided)
          << Kind << SubType.getUnqualifiedType() << CSM;
      else {
        S.Diag(SubobjLoc, diag::note_nontrivial_user_provided)
          << Kind << SubType.getUnqualifiedType() << CSM;
        S.Diag(Selected->getLocation(), diag::note_declared_at);
      }
    } else {
      if (Kind != TSK_CompleteObject)
        S.Diag(SubobjLoc, diag::note_nontrivial_subobject)
          << Kind << SubType.getUnqualifiedType() << CSM;

      // Explain why the defaulted or deleted special member isn't trivial.
      S.SpecialMemberIsTrivial(Selected, CSM, Sema::TAH_IgnoreTrivialABI,
                               Diagnose);
    }
  }

  return false;
}

/// Check whether the members of a class type allow a special member to be
/// trivial.
static bool checkTrivialClassMembers(Sema &S, CXXRecordDecl *RD,
                                     Sema::CXXSpecialMember CSM,
                                     bool ConstArg,
                                     Sema::TrivialABIHandling TAH,
                                     bool Diagnose) {
  for (const auto *FI : RD->fields()) {
    if (FI->isInvalidDecl() || FI->isUnnamedBitfield())
      continue;

    QualType FieldType = S.Context.getBaseElementType(FI->getType());

    // Pretend anonymous struct or union members are members of this class.
    if (FI->isAnonymousStructOrUnion()) {
      if (!checkTrivialClassMembers(S, FieldType->getAsCXXRecordDecl(),
                                    CSM, ConstArg, TAH, Diagnose))
        return false;
      continue;
    }

    // C++11 [class.ctor]p5:
    //   A default constructor is trivial if [...]
    //    -- no non-static data member of its class has a
    //       brace-or-equal-initializer
    if (CSM == Sema::CXXDefaultConstructor && FI->hasInClassInitializer()) {
      if (Diagnose)
        S.Diag(FI->getLocation(), diag::note_nontrivial_in_class_init) << FI;
      return false;
    }

    // Objective C ARC 4.3.5:
    //   [...] nontrivally ownership-qualified types are [...] not trivially
    //   default constructible, copy constructible, move constructible, copy
    //   assignable, move assignable, or destructible [...]
    if (FieldType.hasNonTrivialObjCLifetime()) {
      if (Diagnose)
        S.Diag(FI->getLocation(), diag::note_nontrivial_objc_ownership)
          << RD << FieldType.getObjCLifetime();
      return false;
    }

    bool ConstRHS = ConstArg && !FI->isMutable();
    if (!checkTrivialSubobjectCall(S, FI->getLocation(), FieldType, ConstRHS,
                                   CSM, TSK_Field, TAH, Diagnose))
      return false;
  }

  return true;
}

/// Diagnose why the specified class does not have a trivial special member of
/// the given kind.
void Sema::DiagnoseNontrivial(const CXXRecordDecl *RD, CXXSpecialMember CSM) {
  QualType Ty = Context.getRecordType(RD);

  bool ConstArg = (CSM == CXXCopyConstructor || CSM == CXXCopyAssignment);
  checkTrivialSubobjectCall(*this, RD->getLocation(), Ty, ConstArg, CSM,
                            TSK_CompleteObject, TAH_IgnoreTrivialABI,
                            /*Diagnose*/true);
}

/// Determine whether a defaulted or deleted special member function is trivial,
/// as specified in C++11 [class.ctor]p5, C++11 [class.copy]p12,
/// C++11 [class.copy]p25, and C++11 [class.dtor]p5.
bool Sema::SpecialMemberIsTrivial(CXXMethodDecl *MD, CXXSpecialMember CSM,
                                  TrivialABIHandling TAH, bool Diagnose) {
  assert(!MD->isUserProvided() && CSM != CXXInvalid && "not special enough");

  CXXRecordDecl *RD = MD->getParent();

  bool ConstArg = false;

  // C++11 [class.copy]p12, p25: [DR1593]
  //   A [special member] is trivial if [...] its parameter-type-list is
  //   equivalent to the parameter-type-list of an implicit declaration [...]
  switch (CSM) {
  case CXXDefaultConstructor:
  case CXXDestructor:
    // Trivial default constructors and destructors cannot have parameters.
    break;

  case CXXCopyConstructor:
  case CXXCopyAssignment: {
    // Trivial copy operations always have const, non-volatile parameter types.
    ConstArg = true;
    const ParmVarDecl *Param0 = MD->getParamDecl(0);
    const ReferenceType *RT = Param0->getType()->getAs<ReferenceType>();
    if (!RT || RT->getPointeeType().getCVRQualifiers() != Qualifiers::Const) {
      if (Diagnose)
        Diag(Param0->getLocation(), diag::note_nontrivial_param_type)
          << Param0->getSourceRange() << Param0->getType()
          << Context.getLValueReferenceType(
               Context.getRecordType(RD).withConst());
      return false;
    }
    break;
  }

  case CXXMoveConstructor:
  case CXXMoveAssignment: {
    // Trivial move operations always have non-cv-qualified parameters.
    const ParmVarDecl *Param0 = MD->getParamDecl(0);
    const RValueReferenceType *RT =
      Param0->getType()->getAs<RValueReferenceType>();
    if (!RT || RT->getPointeeType().getCVRQualifiers()) {
      if (Diagnose)
        Diag(Param0->getLocation(), diag::note_nontrivial_param_type)
          << Param0->getSourceRange() << Param0->getType()
          << Context.getRValueReferenceType(Context.getRecordType(RD));
      return false;
    }
    break;
  }

  case CXXInvalid:
    llvm_unreachable("not a special member");
  }

  if (MD->getMinRequiredArguments() < MD->getNumParams()) {
    if (Diagnose)
      Diag(MD->getParamDecl(MD->getMinRequiredArguments())->getLocation(),
           diag::note_nontrivial_default_arg)
        << MD->getParamDecl(MD->getMinRequiredArguments())->getSourceRange();
    return false;
  }
  if (MD->isVariadic()) {
    if (Diagnose)
      Diag(MD->getLocation(), diag::note_nontrivial_variadic);
    return false;
  }

  // C++11 [class.ctor]p5, C++11 [class.dtor]p5:
  //   A copy/move [constructor or assignment operator] is trivial if
  //    -- the [member] selected to copy/move each direct base class subobject
  //       is trivial
  //
  // C++11 [class.copy]p12, C++11 [class.copy]p25:
  //   A [default constructor or destructor] is trivial if
  //    -- all the direct base classes have trivial [default constructors or
  //       destructors]
  for (const auto &BI : RD->bases())
    if (!checkTrivialSubobjectCall(*this, BI.getBeginLoc(), BI.getType(),
                                   ConstArg, CSM, TSK_BaseClass, TAH, Diagnose))
      return false;

  // C++11 [class.ctor]p5, C++11 [class.dtor]p5:
  //   A copy/move [constructor or assignment operator] for a class X is
  //   trivial if
  //    -- for each non-static data member of X that is of class type (or array
  //       thereof), the constructor selected to copy/move that member is
  //       trivial
  //
  // C++11 [class.copy]p12, C++11 [class.copy]p25:
  //   A [default constructor or destructor] is trivial if
  //    -- for all of the non-static data members of its class that are of class
  //       type (or array thereof), each such class has a trivial [default
  //       constructor or destructor]
  if (!checkTrivialClassMembers(*this, RD, CSM, ConstArg, TAH, Diagnose))
    return false;

  // C++11 [class.dtor]p5:
  //   A destructor is trivial if [...]
  //    -- the destructor is not virtual
  if (CSM == CXXDestructor && MD->isVirtual()) {
    if (Diagnose)
      Diag(MD->getLocation(), diag::note_nontrivial_virtual_dtor) << RD;
    return false;
  }

  // C++11 [class.ctor]p5, C++11 [class.copy]p12, C++11 [class.copy]p25:
  //   A [special member] for class X is trivial if [...]
  //    -- class X has no virtual functions and no virtual base classes
  if (CSM != CXXDestructor && MD->getParent()->isDynamicClass()) {
    if (!Diagnose)
      return false;

    if (RD->getNumVBases()) {
      // Check for virtual bases. We already know that the corresponding
      // member in all bases is trivial, so vbases must all be direct.
      CXXBaseSpecifier &BS = *RD->vbases_begin();
      assert(BS.isVirtual());
      Diag(BS.getBeginLoc(), diag::note_nontrivial_has_virtual) << RD << 1;
      return false;
    }

    // Must have a virtual method.
    for (const auto *MI : RD->methods()) {
      if (MI->isVirtual()) {
        SourceLocation MLoc = MI->getBeginLoc();
        Diag(MLoc, diag::note_nontrivial_has_virtual) << RD << 0;
        return false;
      }
    }

    llvm_unreachable("dynamic class with no vbases and no virtual functions");
  }

  // Looks like it's trivial!
  return true;
}

namespace {
struct FindHiddenVirtualMethod {
  Sema *S;
  CXXMethodDecl *Method;
  llvm::SmallPtrSet<const CXXMethodDecl *, 8> OverridenAndUsingBaseMethods;
  SmallVector<CXXMethodDecl *, 8> OverloadedMethods;

private:
  /// Check whether any most overridden method from MD in Methods
  static bool CheckMostOverridenMethods(
      const CXXMethodDecl *MD,
      const llvm::SmallPtrSetImpl<const CXXMethodDecl *> &Methods) {
    if (MD->size_overridden_methods() == 0)
      return Methods.count(MD->getCanonicalDecl());
    for (const CXXMethodDecl *O : MD->overridden_methods())
      if (CheckMostOverridenMethods(O, Methods))
        return true;
    return false;
  }

public:
  /// Member lookup function that determines whether a given C++
  /// method overloads virtual methods in a base class without overriding any,
  /// to be used with CXXRecordDecl::lookupInBases().
  bool operator()(const CXXBaseSpecifier *Specifier, CXXBasePath &Path) {
    RecordDecl *BaseRecord =
        Specifier->getType()->getAs<RecordType>()->getDecl();

    DeclarationName Name = Method->getDeclName();
    assert(Name.getNameKind() == DeclarationName::Identifier);

    bool foundSameNameMethod = false;
    SmallVector<CXXMethodDecl *, 8> overloadedMethods;
    for (Path.Decls = BaseRecord->lookup(Name); !Path.Decls.empty();
         Path.Decls = Path.Decls.slice(1)) {
      NamedDecl *D = Path.Decls.front();
      if (CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(D)) {
        MD = MD->getCanonicalDecl();
        foundSameNameMethod = true;
        // Interested only in hidden virtual methods.
        if (!MD->isVirtual())
          continue;
        // If the method we are checking overrides a method from its base
        // don't warn about the other overloaded methods. Clang deviates from
        // GCC by only diagnosing overloads of inherited virtual functions that
        // do not override any other virtual functions in the base. GCC's
        // -Woverloaded-virtual diagnoses any derived function hiding a virtual
        // function from a base class. These cases may be better served by a
        // warning (not specific to virtual functions) on call sites when the
        // call would select a different function from the base class, were it
        // visible.
        // See FIXME in test/SemaCXX/warn-overload-virtual.cpp for an example.
        if (!S->IsOverload(Method, MD, false))
          return true;
        // Collect the overload only if its hidden.
        if (!CheckMostOverridenMethods(MD, OverridenAndUsingBaseMethods))
          overloadedMethods.push_back(MD);
      }
    }

    if (foundSameNameMethod)
      OverloadedMethods.append(overloadedMethods.begin(),
                               overloadedMethods.end());
    return foundSameNameMethod;
  }
};
} // end anonymous namespace

/// Add the most overriden methods from MD to Methods
static void AddMostOverridenMethods(const CXXMethodDecl *MD,
                        llvm::SmallPtrSetImpl<const CXXMethodDecl *>& Methods) {
  if (MD->size_overridden_methods() == 0)
    Methods.insert(MD->getCanonicalDecl());
  else
    for (const CXXMethodDecl *O : MD->overridden_methods())
      AddMostOverridenMethods(O, Methods);
}

/// Check if a method overloads virtual methods in a base class without
/// overriding any.
void Sema::FindHiddenVirtualMethods(CXXMethodDecl *MD,
                          SmallVectorImpl<CXXMethodDecl*> &OverloadedMethods) {
  if (!MD->getDeclName().isIdentifier())
    return;

  CXXBasePaths Paths(/*FindAmbiguities=*/true, // true to look in all bases.
                     /*bool RecordPaths=*/false,
                     /*bool DetectVirtual=*/false);
  FindHiddenVirtualMethod FHVM;
  FHVM.Method = MD;
  FHVM.S = this;

  // Keep the base methods that were overridden or introduced in the subclass
  // by 'using' in a set. A base method not in this set is hidden.
  CXXRecordDecl *DC = MD->getParent();
  DeclContext::lookup_result R = DC->lookup(MD->getDeclName());
  for (DeclContext::lookup_iterator I = R.begin(), E = R.end(); I != E; ++I) {
    NamedDecl *ND = *I;
    if (UsingShadowDecl *shad = dyn_cast<UsingShadowDecl>(*I))
      ND = shad->getTargetDecl();
    if (CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(ND))
      AddMostOverridenMethods(MD, FHVM.OverridenAndUsingBaseMethods);
  }

  if (DC->lookupInBases(FHVM, Paths))
    OverloadedMethods = FHVM.OverloadedMethods;
}

void Sema::NoteHiddenVirtualMethods(CXXMethodDecl *MD,
                          SmallVectorImpl<CXXMethodDecl*> &OverloadedMethods) {
  for (unsigned i = 0, e = OverloadedMethods.size(); i != e; ++i) {
    CXXMethodDecl *overloadedMD = OverloadedMethods[i];
    PartialDiagnostic PD = PDiag(
         diag::note_hidden_overloaded_virtual_declared_here) << overloadedMD;
    HandleFunctionTypeMismatch(PD, MD->getType(), overloadedMD->getType());
    Diag(overloadedMD->getLocation(), PD);
  }
}

/// Diagnose methods which overload virtual methods in a base class
/// without overriding any.
void Sema::DiagnoseHiddenVirtualMethods(CXXMethodDecl *MD) {
  if (MD->isInvalidDecl())
    return;

  if (Diags.isIgnored(diag::warn_overloaded_virtual, MD->getLocation()))
    return;

  SmallVector<CXXMethodDecl *, 8> OverloadedMethods;
  FindHiddenVirtualMethods(MD, OverloadedMethods);
  if (!OverloadedMethods.empty()) {
    Diag(MD->getLocation(), diag::warn_overloaded_virtual)
      << MD << (OverloadedMethods.size() > 1);

    NoteHiddenVirtualMethods(MD, OverloadedMethods);
  }
}

void Sema::checkIllFormedTrivialABIStruct(CXXRecordDecl &RD) {
  auto PrintDiagAndRemoveAttr = [&]() {
    // No diagnostics if this is a template instantiation.
    if (!isTemplateInstantiation(RD.getTemplateSpecializationKind()))
      Diag(RD.getAttr<TrivialABIAttr>()->getLocation(),
           diag::ext_cannot_use_trivial_abi) << &RD;
    RD.dropAttr<TrivialABIAttr>();
  };

  // Ill-formed if the struct has virtual functions.
  if (RD.isPolymorphic()) {
    PrintDiagAndRemoveAttr();
    return;
  }

  for (const auto &B : RD.bases()) {
    // Ill-formed if the base class is non-trivial for the purpose of calls or a
    // virtual base.
    if ((!B.getType()->isDependentType() &&
         !B.getType()->getAsCXXRecordDecl()->canPassInRegisters()) ||
        B.isVirtual()) {
      PrintDiagAndRemoveAttr();
      return;
    }
  }

  for (const auto *FD : RD.fields()) {
    // Ill-formed if the field is an ObjectiveC pointer or of a type that is
    // non-trivial for the purpose of calls.
    QualType FT = FD->getType();
    if (FT.getObjCLifetime() == Qualifiers::OCL_Weak) {
      PrintDiagAndRemoveAttr();
      return;
    }

    if (const auto *RT = FT->getBaseElementTypeUnsafe()->getAs<RecordType>())
      if (!RT->isDependentType() &&
          !cast<CXXRecordDecl>(RT->getDecl())->canPassInRegisters()) {
        PrintDiagAndRemoveAttr();
        return;
      }
  }
}

void Sema::ActOnFinishCXXMemberSpecification(
    Scope *S, SourceLocation RLoc, Decl *TagDecl, SourceLocation LBrac,
    SourceLocation RBrac, const ParsedAttributesView &AttrList) {
  if (!TagDecl)
    return;

  AdjustDeclIfTemplate(TagDecl);

  for (const ParsedAttr &AL : AttrList) {
    if (AL.getKind() != ParsedAttr::AT_Visibility)
      continue;
    AL.setInvalid();
    Diag(AL.getLoc(), diag::warn_attribute_after_definition_ignored)
        << AL.getName();
  }

  ActOnFields(S, RLoc, TagDecl, llvm::makeArrayRef(
              // strict aliasing violation!
              reinterpret_cast<Decl**>(FieldCollector->getCurFields()),
              FieldCollector->getCurNumFields()), LBrac, RBrac, AttrList);

  CheckCompletedCXXClass(cast<CXXRecordDecl>(TagDecl));
}

/// AddImplicitlyDeclaredMembersToClass - Adds any implicitly-declared
/// special functions, such as the default constructor, copy
/// constructor, or destructor, to the given C++ class (C++
/// [special]p1).  This routine can only be executed just before the
/// definition of the class is complete.
void Sema::AddImplicitlyDeclaredMembersToClass(CXXRecordDecl *ClassDecl) {
  if (ClassDecl->needsImplicitDefaultConstructor()) {
    ++ASTContext::NumImplicitDefaultConstructors;

    if (ClassDecl->hasInheritedConstructor())
      DeclareImplicitDefaultConstructor(ClassDecl);
  }

  if (ClassDecl->needsImplicitCopyConstructor()) {
    ++ASTContext::NumImplicitCopyConstructors;

    // If the properties or semantics of the copy constructor couldn't be
    // determined while the class was being declared, force a declaration
    // of it now.
    if (ClassDecl->needsOverloadResolutionForCopyConstructor() ||
        ClassDecl->hasInheritedConstructor())
      DeclareImplicitCopyConstructor(ClassDecl);
    // For the MS ABI we need to know whether the copy ctor is deleted. A
    // prerequisite for deleting the implicit copy ctor is that the class has a
    // move ctor or move assignment that is either user-declared or whose
    // semantics are inherited from a subobject. FIXME: We should provide a more
    // direct way for CodeGen to ask whether the constructor was deleted.
    else if (Context.getTargetInfo().getCXXABI().isMicrosoft() &&
             (ClassDecl->hasUserDeclaredMoveConstructor() ||
              ClassDecl->needsOverloadResolutionForMoveConstructor() ||
              ClassDecl->hasUserDeclaredMoveAssignment() ||
              ClassDecl->needsOverloadResolutionForMoveAssignment()))
      DeclareImplicitCopyConstructor(ClassDecl);
  }

  if (getLangOpts().CPlusPlus11 && ClassDecl->needsImplicitMoveConstructor()) {
    ++ASTContext::NumImplicitMoveConstructors;

    if (ClassDecl->needsOverloadResolutionForMoveConstructor() ||
        ClassDecl->hasInheritedConstructor())
      DeclareImplicitMoveConstructor(ClassDecl);
  }

  if (ClassDecl->needsImplicitCopyAssignment()) {
    ++ASTContext::NumImplicitCopyAssignmentOperators;

    // If we have a dynamic class, then the copy assignment operator may be
    // virtual, so we have to declare it immediately. This ensures that, e.g.,
    // it shows up in the right place in the vtable and that we diagnose
    // problems with the implicit exception specification.
    if (ClassDecl->isDynamicClass() ||
        ClassDecl->needsOverloadResolutionForCopyAssignment() ||
        ClassDecl->hasInheritedAssignment())
      DeclareImplicitCopyAssignment(ClassDecl);
  }

  if (getLangOpts().CPlusPlus11 && ClassDecl->needsImplicitMoveAssignment()) {
    ++ASTContext::NumImplicitMoveAssignmentOperators;

    // Likewise for the move assignment operator.
    if (ClassDecl->isDynamicClass() ||
        ClassDecl->needsOverloadResolutionForMoveAssignment() ||
        ClassDecl->hasInheritedAssignment())
      DeclareImplicitMoveAssignment(ClassDecl);
  }

  if (ClassDecl->needsImplicitDestructor()) {
    ++ASTContext::NumImplicitDestructors;

    // If we have a dynamic class, then the destructor may be virtual, so we
    // have to declare the destructor immediately. This ensures that, e.g., it
    // shows up in the right place in the vtable and that we diagnose problems
    // with the implicit exception specification.
    if (ClassDecl->isDynamicClass() ||
        ClassDecl->needsOverloadResolutionForDestructor())
      DeclareImplicitDestructor(ClassDecl);
  }
}

unsigned Sema::ActOnReenterTemplateScope(Scope *S, Decl *D) {
  if (!D)
    return 0;

  // The order of template parameters is not important here. All names
  // get added to the same scope.
  SmallVector<TemplateParameterList *, 4> ParameterLists;

  if (TemplateDecl *TD = dyn_cast<TemplateDecl>(D))
    D = TD->getTemplatedDecl();

  if (auto *PSD = dyn_cast<ClassTemplatePartialSpecializationDecl>(D))
    ParameterLists.push_back(PSD->getTemplateParameters());

  if (DeclaratorDecl *DD = dyn_cast<DeclaratorDecl>(D)) {
    for (unsigned i = 0; i < DD->getNumTemplateParameterLists(); ++i)
      ParameterLists.push_back(DD->getTemplateParameterList(i));

    if (FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
      if (FunctionTemplateDecl *FTD = FD->getDescribedFunctionTemplate())
        ParameterLists.push_back(FTD->getTemplateParameters());
    }
  }

  if (TagDecl *TD = dyn_cast<TagDecl>(D)) {
    for (unsigned i = 0; i < TD->getNumTemplateParameterLists(); ++i)
      ParameterLists.push_back(TD->getTemplateParameterList(i));

    if (CXXRecordDecl *RD = dyn_cast<CXXRecordDecl>(TD)) {
      if (ClassTemplateDecl *CTD = RD->getDescribedClassTemplate())
        ParameterLists.push_back(CTD->getTemplateParameters());
    }
  }

  unsigned Count = 0;
  for (TemplateParameterList *Params : ParameterLists) {
    if (Params->size() > 0)
      // Ignore explicit specializations; they don't contribute to the template
      // depth.
      ++Count;
    for (NamedDecl *Param : *Params) {
      if (Param->getDeclName()) {
        S->AddDecl(Param);
        IdResolver.AddDecl(Param);
      }
    }
  }

  return Count;
}

void Sema::ActOnStartDelayedMemberDeclarations(Scope *S, Decl *RecordD) {
  if (!RecordD) return;
  AdjustDeclIfTemplate(RecordD);
  CXXRecordDecl *Record = cast<CXXRecordDecl>(RecordD);
  PushDeclContext(S, Record);
}

void Sema::ActOnFinishDelayedMemberDeclarations(Scope *S, Decl *RecordD) {
  if (!RecordD) return;
  PopDeclContext();
}

/// This is used to implement the constant expression evaluation part of the
/// attribute enable_if extension. There is nothing in standard C++ which would
/// require reentering parameters.
void Sema::ActOnReenterCXXMethodParameter(Scope *S, ParmVarDecl *Param) {
  if (!Param)
    return;

  S->AddDecl(Param);
  if (Param->getDeclName())
    IdResolver.AddDecl(Param);
}

/// ActOnStartDelayedCXXMethodDeclaration - We have completed
/// parsing a top-level (non-nested) C++ class, and we are now
/// parsing those parts of the given Method declaration that could
/// not be parsed earlier (C++ [class.mem]p2), such as default
/// arguments. This action should enter the scope of the given
/// Method declaration as if we had just parsed the qualified method
/// name. However, it should not bring the parameters into scope;
/// that will be performed by ActOnDelayedCXXMethodParameter.
void Sema::ActOnStartDelayedCXXMethodDeclaration(Scope *S, Decl *MethodD) {
}

/// ActOnDelayedCXXMethodParameter - We've already started a delayed
/// C++ method declaration. We're (re-)introducing the given
/// function parameter into scope for use in parsing later parts of
/// the method declaration. For example, we could see an
/// ActOnParamDefaultArgument event for this parameter.
void Sema::ActOnDelayedCXXMethodParameter(Scope *S, Decl *ParamD) {
  if (!ParamD)
    return;

  ParmVarDecl *Param = cast<ParmVarDecl>(ParamD);

  // If this parameter has an unparsed default argument, clear it out
  // to make way for the parsed default argument.
  if (Param->hasUnparsedDefaultArg())
    Param->setDefaultArg(nullptr);

  S->AddDecl(Param);
  if (Param->getDeclName())
    IdResolver.AddDecl(Param);
}

/// ActOnFinishDelayedCXXMethodDeclaration - We have finished
/// processing the delayed method declaration for Method. The method
/// declaration is now considered finished. There may be a separate
/// ActOnStartOfFunctionDef action later (not necessarily
/// immediately!) for this method, if it was also defined inside the
/// class body.
void Sema::ActOnFinishDelayedCXXMethodDeclaration(Scope *S, Decl *MethodD) {
  if (!MethodD)
    return;

  AdjustDeclIfTemplate(MethodD);

  FunctionDecl *Method = cast<FunctionDecl>(MethodD);

  // Now that we have our default arguments, check the constructor
  // again. It could produce additional diagnostics or affect whether
  // the class has implicitly-declared destructors, among other
  // things.
  if (CXXConstructorDecl *Constructor = dyn_cast<CXXConstructorDecl>(Method))
    CheckConstructor(Constructor);

  // Check the default arguments, which we may have added.
  if (!Method->isInvalidDecl())
    CheckCXXDefaultArguments(Method);
}

/// CheckConstructorDeclarator - Called by ActOnDeclarator to check
/// the well-formedness of the constructor declarator @p D with type @p
/// R. If there are any errors in the declarator, this routine will
/// emit diagnostics and set the invalid bit to true.  In any case, the type
/// will be updated to reflect a well-formed type for the constructor and
/// returned.
QualType Sema::CheckConstructorDeclarator(Declarator &D, QualType R,
                                          StorageClass &SC) {
  bool isVirtual = D.getDeclSpec().isVirtualSpecified();

  // C++ [class.ctor]p3:
  //   A constructor shall not be virtual (10.3) or static (9.4). A
  //   constructor can be invoked for a const, volatile or const
  //   volatile object. A constructor shall not be declared const,
  //   volatile, or const volatile (9.3.2).
  if (isVirtual) {
    if (!D.isInvalidType())
      Diag(D.getIdentifierLoc(), diag::err_constructor_cannot_be)
        << "virtual" << SourceRange(D.getDeclSpec().getVirtualSpecLoc())
        << SourceRange(D.getIdentifierLoc());
    D.setInvalidType();
  }
  if (SC == SC_Static) {
    if (!D.isInvalidType())
      Diag(D.getIdentifierLoc(), diag::err_constructor_cannot_be)
        << "static" << SourceRange(D.getDeclSpec().getStorageClassSpecLoc())
        << SourceRange(D.getIdentifierLoc());
    D.setInvalidType();
    SC = SC_None;
  }

  if (unsigned TypeQuals = D.getDeclSpec().getTypeQualifiers()) {
    diagnoseIgnoredQualifiers(
        diag::err_constructor_return_type, TypeQuals, SourceLocation(),
        D.getDeclSpec().getConstSpecLoc(), D.getDeclSpec().getVolatileSpecLoc(),
        D.getDeclSpec().getRestrictSpecLoc(),
        D.getDeclSpec().getAtomicSpecLoc());
    D.setInvalidType();
  }

  DeclaratorChunk::FunctionTypeInfo &FTI = D.getFunctionTypeInfo();
  if (FTI.hasMethodTypeQualifiers()) {
    FTI.MethodQualifiers->forEachQualifier(
        [&](DeclSpec::TQ TypeQual, StringRef QualName, SourceLocation SL) {
          Diag(SL, diag::err_invalid_qualified_constructor)
              << QualName << SourceRange(SL);
        });
    D.setInvalidType();
  }

  // C++0x [class.ctor]p4:
  //   A constructor shall not be declared with a ref-qualifier.
  if (FTI.hasRefQualifier()) {
    Diag(FTI.getRefQualifierLoc(), diag::err_ref_qualifier_constructor)
      << FTI.RefQualifierIsLValueRef
      << FixItHint::CreateRemoval(FTI.getRefQualifierLoc());
    D.setInvalidType();
  }

  // Rebuild the function type "R" without any type qualifiers (in
  // case any of the errors above fired) and with "void" as the
  // return type, since constructors don't have return types.
  const FunctionProtoType *Proto = R->getAs<FunctionProtoType>();
  if (Proto->getReturnType() == Context.VoidTy && !D.isInvalidType())
    return R;

  FunctionProtoType::ExtProtoInfo EPI = Proto->getExtProtoInfo();
  EPI.TypeQuals = Qualifiers();
  EPI.RefQualifier = RQ_None;

  return Context.getFunctionType(Context.VoidTy, Proto->getParamTypes(), EPI);
}

/// CheckConstructor - Checks a fully-formed constructor for
/// well-formedness, issuing any diagnostics required. Returns true if
/// the constructor declarator is invalid.
void Sema::CheckConstructor(CXXConstructorDecl *Constructor) {
  CXXRecordDecl *ClassDecl
    = dyn_cast<CXXRecordDecl>(Constructor->getDeclContext());
  if (!ClassDecl)
    return Constructor->setInvalidDecl();

  // C++ [class.copy]p3:
  //   A declaration of a constructor for a class X is ill-formed if
  //   its first parameter is of type (optionally cv-qualified) X and
  //   either there are no other parameters or else all other
  //   parameters have default arguments.
  if (!Constructor->isInvalidDecl() &&
      ((Constructor->getNumParams() == 1) ||
       (Constructor->getNumParams() > 1 &&
        Constructor->getParamDecl(1)->hasDefaultArg())) &&
      Constructor->getTemplateSpecializationKind()
                                              != TSK_ImplicitInstantiation) {
    QualType ParamType = Constructor->getParamDecl(0)->getType();
    QualType ClassTy = Context.getTagDeclType(ClassDecl);
    if (Context.getCanonicalType(ParamType).getUnqualifiedType() == ClassTy) {
      SourceLocation ParamLoc = Constructor->getParamDecl(0)->getLocation();
      const char *ConstRef
        = Constructor->getParamDecl(0)->getIdentifier() ? "const &"
                                                        : " const &";
      Diag(ParamLoc, diag::err_constructor_byvalue_arg)
        << FixItHint::CreateInsertion(ParamLoc, ConstRef);

      // FIXME: Rather that making the constructor invalid, we should endeavor
      // to fix the type.
      Constructor->setInvalidDecl();
    }
  }
}

/// CheckDestructor - Checks a fully-formed destructor definition for
/// well-formedness, issuing any diagnostics required.  Returns true
/// on error.
bool Sema::CheckDestructor(CXXDestructorDecl *Destructor) {
  CXXRecordDecl *RD = Destructor->getParent();

  if (!Destructor->getOperatorDelete() && Destructor->isVirtual()) {
    SourceLocation Loc;

    if (!Destructor->isImplicit())
      Loc = Destructor->getLocation();
    else
      Loc = RD->getLocation();

    // If we have a virtual destructor, look up the deallocation function
    if (FunctionDecl *OperatorDelete =
            FindDeallocationFunctionForDestructor(Loc, RD)) {
      Expr *ThisArg = nullptr;

      // If the notional 'delete this' expression requires a non-trivial
      // conversion from 'this' to the type of a destroying operator delete's
      // first parameter, perform that conversion now.
      if (OperatorDelete->isDestroyingOperatorDelete()) {
        QualType ParamType = OperatorDelete->getParamDecl(0)->getType();
        if (!declaresSameEntity(ParamType->getAsCXXRecordDecl(), RD)) {
          // C++ [class.dtor]p13:
          //   ... as if for the expression 'delete this' appearing in a
          //   non-virtual destructor of the destructor's class.
          ContextRAII SwitchContext(*this, Destructor);
          ExprResult This =
              ActOnCXXThis(OperatorDelete->getParamDecl(0)->getLocation());
          assert(!This.isInvalid() && "couldn't form 'this' expr in dtor?");
          This = PerformImplicitConversion(This.get(), ParamType, AA_Passing);
          if (This.isInvalid()) {
            // FIXME: Register this as a context note so that it comes out
            // in the right order.
            Diag(Loc, diag::note_implicit_delete_this_in_destructor_here);
            return true;
          }
          ThisArg = This.get();
        }
      }

      DiagnoseUseOfDecl(OperatorDelete, Loc);
      MarkFunctionReferenced(Loc, OperatorDelete);
      Destructor->setOperatorDelete(OperatorDelete, ThisArg);
    }
  }

  return false;
}

/// CheckDestructorDeclarator - Called by ActOnDeclarator to check
/// the well-formednes of the destructor declarator @p D with type @p
/// R. If there are any errors in the declarator, this routine will
/// emit diagnostics and set the declarator to invalid.  Even if this happens,
/// will be updated to reflect a well-formed type for the destructor and
/// returned.
QualType Sema::CheckDestructorDeclarator(Declarator &D, QualType R,
                                         StorageClass& SC) {
  // C++ [class.dtor]p1:
  //   [...] A typedef-name that names a class is a class-name
  //   (7.1.3); however, a typedef-name that names a class shall not
  //   be used as the identifier in the declarator for a destructor
  //   declaration.
  QualType DeclaratorType = GetTypeFromParser(D.getName().DestructorName);
  if (const TypedefType *TT = DeclaratorType->getAs<TypedefType>())
    Diag(D.getIdentifierLoc(), diag::err_destructor_typedef_name)
      << DeclaratorType << isa<TypeAliasDecl>(TT->getDecl());
  else if (const TemplateSpecializationType *TST =
             DeclaratorType->getAs<TemplateSpecializationType>())
    if (TST->isTypeAlias())
      Diag(D.getIdentifierLoc(), diag::err_destructor_typedef_name)
        << DeclaratorType << 1;

  // C++ [class.dtor]p2:
  //   A destructor is used to destroy objects of its class type. A
  //   destructor takes no parameters, and no return type can be
  //   specified for it (not even void). The address of a destructor
  //   shall not be taken. A destructor shall not be static. A
  //   destructor can be invoked for a const, volatile or const
  //   volatile object. A destructor shall not be declared const,
  //   volatile or const volatile (9.3.2).
  if (SC == SC_Static) {
    if (!D.isInvalidType())
      Diag(D.getIdentifierLoc(), diag::err_destructor_cannot_be)
        << "static" << SourceRange(D.getDeclSpec().getStorageClassSpecLoc())
        << SourceRange(D.getIdentifierLoc())
        << FixItHint::CreateRemoval(D.getDeclSpec().getStorageClassSpecLoc());

    SC = SC_None;
  }
  if (!D.isInvalidType()) {
    // Destructors don't have return types, but the parser will
    // happily parse something like:
    //
    //   class X {
    //     float ~X();
    //   };
    //
    // The return type will be eliminated later.
    if (D.getDeclSpec().hasTypeSpecifier())
      Diag(D.getIdentifierLoc(), diag::err_destructor_return_type)
        << SourceRange(D.getDeclSpec().getTypeSpecTypeLoc())
        << SourceRange(D.getIdentifierLoc());
    else if (unsigned TypeQuals = D.getDeclSpec().getTypeQualifiers()) {
      diagnoseIgnoredQualifiers(diag::err_destructor_return_type, TypeQuals,
                                SourceLocation(),
                                D.getDeclSpec().getConstSpecLoc(),
                                D.getDeclSpec().getVolatileSpecLoc(),
                                D.getDeclSpec().getRestrictSpecLoc(),
                                D.getDeclSpec().getAtomicSpecLoc());
      D.setInvalidType();
    }
  }

  DeclaratorChunk::FunctionTypeInfo &FTI = D.getFunctionTypeInfo();
  if (FTI.hasMethodTypeQualifiers() && !D.isInvalidType()) {
    FTI.MethodQualifiers->forEachQualifier(
        [&](DeclSpec::TQ TypeQual, StringRef QualName, SourceLocation SL) {
          Diag(SL, diag::err_invalid_qualified_destructor)
              << QualName << SourceRange(SL);
        });
    D.setInvalidType();
  }

  // C++0x [class.dtor]p2:
  //   A destructor shall not be declared with a ref-qualifier.
  if (FTI.hasRefQualifier()) {
    Diag(FTI.getRefQualifierLoc(), diag::err_ref_qualifier_destructor)
      << FTI.RefQualifierIsLValueRef
      << FixItHint::CreateRemoval(FTI.getRefQualifierLoc());
    D.setInvalidType();
  }

  // Make sure we don't have any parameters.
  if (FTIHasNonVoidParameters(FTI)) {
    Diag(D.getIdentifierLoc(), diag::err_destructor_with_params);

    // Delete the parameters.
    FTI.freeParams();
    D.setInvalidType();
  }

  // Make sure the destructor isn't variadic.
  if (FTI.isVariadic) {
    Diag(D.getIdentifierLoc(), diag::err_destructor_variadic);
    D.setInvalidType();
  }

  // Rebuild the function type "R" without any type qualifiers or
  // parameters (in case any of the errors above fired) and with
  // "void" as the return type, since destructors don't have return
  // types.
  if (!D.isInvalidType())
    return R;

  const FunctionProtoType *Proto = R->getAs<FunctionProtoType>();
  FunctionProtoType::ExtProtoInfo EPI = Proto->getExtProtoInfo();
  EPI.Variadic = false;
  EPI.TypeQuals = Qualifiers();
  EPI.RefQualifier = RQ_None;
  return Context.getFunctionType(Context.VoidTy, None, EPI);
}

static void extendLeft(SourceRange &R, SourceRange Before) {
  if (Before.isInvalid())
    return;
  R.setBegin(Before.getBegin());
  if (R.getEnd().isInvalid())
    R.setEnd(Before.getEnd());
}

static void extendRight(SourceRange &R, SourceRange After) {
  if (After.isInvalid())
    return;
  if (R.getBegin().isInvalid())
    R.setBegin(After.getBegin());
  R.setEnd(After.getEnd());
}

/// CheckConversionDeclarator - Called by ActOnDeclarator to check the
/// well-formednes of the conversion function declarator @p D with
/// type @p R. If there are any errors in the declarator, this routine
/// will emit diagnostics and return true. Otherwise, it will return
/// false. Either way, the type @p R will be updated to reflect a
/// well-formed type for the conversion operator.
void Sema::CheckConversionDeclarator(Declarator &D, QualType &R,
                                     StorageClass& SC) {
  // C++ [class.conv.fct]p1:
  //   Neither parameter types nor return type can be specified. The
  //   type of a conversion function (8.3.5) is "function taking no
  //   parameter returning conversion-type-id."
  if (SC == SC_Static) {
    if (!D.isInvalidType())
      Diag(D.getIdentifierLoc(), diag::err_conv_function_not_member)
        << SourceRange(D.getDeclSpec().getStorageClassSpecLoc())
        << D.getName().getSourceRange();
    D.setInvalidType();
    SC = SC_None;
  }

  TypeSourceInfo *ConvTSI = nullptr;
  QualType ConvType =
      GetTypeFromParser(D.getName().ConversionFunctionId, &ConvTSI);

  const DeclSpec &DS = D.getDeclSpec();
  if (DS.hasTypeSpecifier() && !D.isInvalidType()) {
    // Conversion functions don't have return types, but the parser will
    // happily parse something like:
    //
    //   class X {
    //     float operator bool();
    //   };
    //
    // The return type will be changed later anyway.
    Diag(D.getIdentifierLoc(), diag::err_conv_function_return_type)
      << SourceRange(DS.getTypeSpecTypeLoc())
      << SourceRange(D.getIdentifierLoc());
    D.setInvalidType();
  } else if (DS.getTypeQualifiers() && !D.isInvalidType()) {
    // It's also plausible that the user writes type qualifiers in the wrong
    // place, such as:
    //   struct S { const operator int(); };
    // FIXME: we could provide a fixit to move the qualifiers onto the
    // conversion type.
    Diag(D.getIdentifierLoc(), diag::err_conv_function_with_complex_decl)
        << SourceRange(D.getIdentifierLoc()) << 0;
    D.setInvalidType();
  }

  const FunctionProtoType *Proto = R->getAs<FunctionProtoType>();

  // Make sure we don't have any parameters.
  if (Proto->getNumParams() > 0) {
    Diag(D.getIdentifierLoc(), diag::err_conv_function_with_params);

    // Delete the parameters.
    D.getFunctionTypeInfo().freeParams();
    D.setInvalidType();
  } else if (Proto->isVariadic()) {
    Diag(D.getIdentifierLoc(), diag::err_conv_function_variadic);
    D.setInvalidType();
  }

  // Diagnose "&operator bool()" and other such nonsense.  This
  // is actually a gcc extension which we don't support.
  if (Proto->getReturnType() != ConvType) {
    bool NeedsTypedef = false;
    SourceRange Before, After;

    // Walk the chunks and extract information on them for our diagnostic.
    bool PastFunctionChunk = false;
    for (auto &Chunk : D.type_objects()) {
      switch (Chunk.Kind) {
      case DeclaratorChunk::Function:
        if (!PastFunctionChunk) {
          if (Chunk.Fun.HasTrailingReturnType) {
            TypeSourceInfo *TRT = nullptr;
            GetTypeFromParser(Chunk.Fun.getTrailingReturnType(), &TRT);
            if (TRT) extendRight(After, TRT->getTypeLoc().getSourceRange());
          }
          PastFunctionChunk = true;
          break;
        }
        LLVM_FALLTHROUGH;
      case DeclaratorChunk::Array:
        NeedsTypedef = true;
        extendRight(After, Chunk.getSourceRange());
        break;

      case DeclaratorChunk::Pointer:
      case DeclaratorChunk::BlockPointer:
      case DeclaratorChunk::Reference:
      case DeclaratorChunk::MemberPointer:
      case DeclaratorChunk::Pipe:
        extendLeft(Before, Chunk.getSourceRange());
        break;

      case DeclaratorChunk::Paren:
        extendLeft(Before, Chunk.Loc);
        extendRight(After, Chunk.EndLoc);
        break;
      }
    }

    SourceLocation Loc = Before.isValid() ? Before.getBegin() :
                         After.isValid()  ? After.getBegin() :
                                            D.getIdentifierLoc();
    auto &&DB = Diag(Loc, diag::err_conv_function_with_complex_decl);
    DB << Before << After;

    if (!NeedsTypedef) {
      DB << /*don't need a typedef*/0;

      // If we can provide a correct fix-it hint, do so.
      if (After.isInvalid() && ConvTSI) {
        SourceLocation InsertLoc =
            getLocForEndOfToken(ConvTSI->getTypeLoc().getEndLoc());
        DB << FixItHint::CreateInsertion(InsertLoc, " ")
           << FixItHint::CreateInsertionFromRange(
                  InsertLoc, CharSourceRange::getTokenRange(Before))
           << FixItHint::CreateRemoval(Before);
      }
    } else if (!Proto->getReturnType()->isDependentType()) {
      DB << /*typedef*/1 << Proto->getReturnType();
    } else if (getLangOpts().CPlusPlus11) {
      DB << /*alias template*/2 << Proto->getReturnType();
    } else {
      DB << /*might not be fixable*/3;
    }

    // Recover by incorporating the other type chunks into the result type.
    // Note, this does *not* change the name of the function. This is compatible
    // with the GCC extension:
    //   struct S { &operator int(); } s;
    //   int &r = s.operator int(); // ok in GCC
    //   S::operator int&() {} // error in GCC, function name is 'operator int'.
    ConvType = Proto->getReturnType();
  }

  // C++ [class.conv.fct]p4:
  //   The conversion-type-id shall not represent a function type nor
  //   an array type.
  if (ConvType->isArrayType()) {
    Diag(D.getIdentifierLoc(), diag::err_conv_function_to_array);
    ConvType = Context.getPointerType(ConvType);
    D.setInvalidType();
  } else if (ConvType->isFunctionType()) {
    Diag(D.getIdentifierLoc(), diag::err_conv_function_to_function);
    ConvType = Context.getPointerType(ConvType);
    D.setInvalidType();
  }

  // Rebuild the function type "R" without any parameters (in case any
  // of the errors above fired) and with the conversion type as the
  // return type.
  if (D.isInvalidType())
    R = Context.getFunctionType(ConvType, None, Proto->getExtProtoInfo());

  // C++0x explicit conversion operators.
  if (DS.isExplicitSpecified())
    Diag(DS.getExplicitSpecLoc(),
         getLangOpts().CPlusPlus11
             ? diag::warn_cxx98_compat_explicit_conversion_functions
             : diag::ext_explicit_conversion_functions)
        << SourceRange(DS.getExplicitSpecLoc());
}

/// ActOnConversionDeclarator - Called by ActOnDeclarator to complete
/// the declaration of the given C++ conversion function. This routine
/// is responsible for recording the conversion function in the C++
/// class, if possible.
Decl *Sema::ActOnConversionDeclarator(CXXConversionDecl *Conversion) {
  assert(Conversion && "Expected to receive a conversion function declaration");

  CXXRecordDecl *ClassDecl = cast<CXXRecordDecl>(Conversion->getDeclContext());

  // Make sure we aren't redeclaring the conversion function.
  QualType ConvType = Context.getCanonicalType(Conversion->getConversionType());

  // C++ [class.conv.fct]p1:
  //   [...] A conversion function is never used to convert a
  //   (possibly cv-qualified) object to the (possibly cv-qualified)
  //   same object type (or a reference to it), to a (possibly
  //   cv-qualified) base class of that type (or a reference to it),
  //   or to (possibly cv-qualified) void.
  // FIXME: Suppress this warning if the conversion function ends up being a
  // virtual function that overrides a virtual function in a base class.
  QualType ClassType
    = Context.getCanonicalType(Context.getTypeDeclType(ClassDecl));
  if (const ReferenceType *ConvTypeRef = ConvType->getAs<ReferenceType>())
    ConvType = ConvTypeRef->getPointeeType();
  if (Conversion->getTemplateSpecializationKind() != TSK_Undeclared &&
      Conversion->getTemplateSpecializationKind() != TSK_ExplicitSpecialization)
    /* Suppress diagnostics for instantiations. */;
  else if (ConvType->isRecordType()) {
    ConvType = Context.getCanonicalType(ConvType).getUnqualifiedType();
    if (ConvType == ClassType)
      Diag(Conversion->getLocation(), diag::warn_conv_to_self_not_used)
        << ClassType;
    else if (IsDerivedFrom(Conversion->getLocation(), ClassType, ConvType))
      Diag(Conversion->getLocation(), diag::warn_conv_to_base_not_used)
        <<  ClassType << ConvType;
  } else if (ConvType->isVoidType()) {
    Diag(Conversion->getLocation(), diag::warn_conv_to_void_not_used)
      << ClassType << ConvType;
  }

  if (FunctionTemplateDecl *ConversionTemplate
                                = Conversion->getDescribedFunctionTemplate())
    return ConversionTemplate;

  return Conversion;
}

namespace {
/// Utility class to accumulate and print a diagnostic listing the invalid
/// specifier(s) on a declaration.
struct BadSpecifierDiagnoser {
  BadSpecifierDiagnoser(Sema &S, SourceLocation Loc, unsigned DiagID)
      : S(S), Diagnostic(S.Diag(Loc, DiagID)) {}
  ~BadSpecifierDiagnoser() {
    Diagnostic << Specifiers;
  }

  template<typename T> void check(SourceLocation SpecLoc, T Spec) {
    return check(SpecLoc, DeclSpec::getSpecifierName(Spec));
  }
  void check(SourceLocation SpecLoc, DeclSpec::TST Spec) {
    return check(SpecLoc,
                 DeclSpec::getSpecifierName(Spec, S.getPrintingPolicy()));
  }
  void check(SourceLocation SpecLoc, const char *Spec) {
    if (SpecLoc.isInvalid()) return;
    Diagnostic << SourceRange(SpecLoc, SpecLoc);
    if (!Specifiers.empty()) Specifiers += " ";
    Specifiers += Spec;
  }

  Sema &S;
  Sema::SemaDiagnosticBuilder Diagnostic;
  std::string Specifiers;
};
}

/// Check the validity of a declarator that we parsed for a deduction-guide.
/// These aren't actually declarators in the grammar, so we need to check that
/// the user didn't specify any pieces that are not part of the deduction-guide
/// grammar.
void Sema::CheckDeductionGuideDeclarator(Declarator &D, QualType &R,
                                         StorageClass &SC) {
  TemplateName GuidedTemplate = D.getName().TemplateName.get().get();
  TemplateDecl *GuidedTemplateDecl = GuidedTemplate.getAsTemplateDecl();
  assert(GuidedTemplateDecl && "missing template decl for deduction guide");

  // C++ [temp.deduct.guide]p3:
  //   A deduction-gide shall be declared in the same scope as the
  //   corresponding class template.
  if (!CurContext->getRedeclContext()->Equals(
          GuidedTemplateDecl->getDeclContext()->getRedeclContext())) {
    Diag(D.getIdentifierLoc(), diag::err_deduction_guide_wrong_scope)
      << GuidedTemplateDecl;
    Diag(GuidedTemplateDecl->getLocation(), diag::note_template_decl_here);
  }

  auto &DS = D.getMutableDeclSpec();
  // We leave 'friend' and 'virtual' to be rejected in the normal way.
  if (DS.hasTypeSpecifier() || DS.getTypeQualifiers() ||
      DS.getStorageClassSpecLoc().isValid() || DS.isInlineSpecified() ||
      DS.isNoreturnSpecified() || DS.isConstexprSpecified()) {
    BadSpecifierDiagnoser Diagnoser(
        *this, D.getIdentifierLoc(),
        diag::err_deduction_guide_invalid_specifier);

    Diagnoser.check(DS.getStorageClassSpecLoc(), DS.getStorageClassSpec());
    DS.ClearStorageClassSpecs();
    SC = SC_None;

    // 'explicit' is permitted.
    Diagnoser.check(DS.getInlineSpecLoc(), "inline");
    Diagnoser.check(DS.getNoreturnSpecLoc(), "_Noreturn");
    Diagnoser.check(DS.getConstexprSpecLoc(), "constexpr");
    DS.ClearConstexprSpec();

    Diagnoser.check(DS.getConstSpecLoc(), "const");
    Diagnoser.check(DS.getRestrictSpecLoc(), "__restrict");
    Diagnoser.check(DS.getVolatileSpecLoc(), "volatile");
    Diagnoser.check(DS.getAtomicSpecLoc(), "_Atomic");
    Diagnoser.check(DS.getUnalignedSpecLoc(), "__unaligned");
    DS.ClearTypeQualifiers();

    Diagnoser.check(DS.getTypeSpecComplexLoc(), DS.getTypeSpecComplex());
    Diagnoser.check(DS.getTypeSpecSignLoc(), DS.getTypeSpecSign());
    Diagnoser.check(DS.getTypeSpecWidthLoc(), DS.getTypeSpecWidth());
    Diagnoser.check(DS.getTypeSpecTypeLoc(), DS.getTypeSpecType());
    DS.ClearTypeSpecType();
  }

  if (D.isInvalidType())
    return;

  // Check the declarator is simple enough.
  bool FoundFunction = false;
  for (const DeclaratorChunk &Chunk : llvm::reverse(D.type_objects())) {
    if (Chunk.Kind == DeclaratorChunk::Paren)
      continue;
    if (Chunk.Kind != DeclaratorChunk::Function || FoundFunction) {
      Diag(D.getDeclSpec().getBeginLoc(),
           diag::err_deduction_guide_with_complex_decl)
          << D.getSourceRange();
      break;
    }
    if (!Chunk.Fun.hasTrailingReturnType()) {
      Diag(D.getName().getBeginLoc(),
           diag::err_deduction_guide_no_trailing_return_type);
      break;
    }

    // Check that the return type is written as a specialization of
    // the template specified as the deduction-guide's name.
    ParsedType TrailingReturnType = Chunk.Fun.getTrailingReturnType();
    TypeSourceInfo *TSI = nullptr;
    QualType RetTy = GetTypeFromParser(TrailingReturnType, &TSI);
    assert(TSI && "deduction guide has valid type but invalid return type?");
    bool AcceptableReturnType = false;
    bool MightInstantiateToSpecialization = false;
    if (auto RetTST =
            TSI->getTypeLoc().getAs<TemplateSpecializationTypeLoc>()) {
      TemplateName SpecifiedName = RetTST.getTypePtr()->getTemplateName();
      bool TemplateMatches =
          Context.hasSameTemplateName(SpecifiedName, GuidedTemplate);
      if (SpecifiedName.getKind() == TemplateName::Template && TemplateMatches)
        AcceptableReturnType = true;
      else {
        // This could still instantiate to the right type, unless we know it
        // names the wrong class template.
        auto *TD = SpecifiedName.getAsTemplateDecl();
        MightInstantiateToSpecialization = !(TD && isa<ClassTemplateDecl>(TD) &&
                                             !TemplateMatches);
      }
    } else if (!RetTy.hasQualifiers() && RetTy->isDependentType()) {
      MightInstantiateToSpecialization = true;
    }

    if (!AcceptableReturnType) {
      Diag(TSI->getTypeLoc().getBeginLoc(),
           diag::err_deduction_guide_bad_trailing_return_type)
          << GuidedTemplate << TSI->getType()
          << MightInstantiateToSpecialization
          << TSI->getTypeLoc().getSourceRange();
    }

    // Keep going to check that we don't have any inner declarator pieces (we
    // could still have a function returning a pointer to a function).
    FoundFunction = true;
  }

  if (D.isFunctionDefinition())
    Diag(D.getIdentifierLoc(), diag::err_deduction_guide_defines_function);
}

//===----------------------------------------------------------------------===//
// Namespace Handling
//===----------------------------------------------------------------------===//

/// Diagnose a mismatch in 'inline' qualifiers when a namespace is
/// reopened.
static void DiagnoseNamespaceInlineMismatch(Sema &S, SourceLocation KeywordLoc,
                                            SourceLocation Loc,
                                            IdentifierInfo *II, bool *IsInline,
                                            NamespaceDecl *PrevNS) {
  assert(*IsInline != PrevNS->isInline());

  // HACK: Work around a bug in libstdc++4.6's <atomic>, where
  // std::__atomic[0,1,2] are defined as non-inline namespaces, then reopened as
  // inline namespaces, with the intention of bringing names into namespace std.
  //
  // We support this just well enough to get that case working; this is not
  // sufficient to support reopening namespaces as inline in general.
  if (*IsInline && II && II->getName().startswith("__atomic") &&
      S.getSourceManager().isInSystemHeader(Loc)) {
    // Mark all prior declarations of the namespace as inline.
    for (NamespaceDecl *NS = PrevNS->getMostRecentDecl(); NS;
         NS = NS->getPreviousDecl())
      NS->setInline(*IsInline);
    // Patch up the lookup table for the containing namespace. This isn't really
    // correct, but it's good enough for this particular case.
    for (auto *I : PrevNS->decls())
      if (auto *ND = dyn_cast<NamedDecl>(I))
        PrevNS->getParent()->makeDeclVisibleInContext(ND);
    return;
  }

  if (PrevNS->isInline())
    // The user probably just forgot the 'inline', so suggest that it
    // be added back.
    S.Diag(Loc, diag::warn_inline_namespace_reopened_noninline)
      << FixItHint::CreateInsertion(KeywordLoc, "inline ");
  else
    S.Diag(Loc, diag::err_inline_namespace_mismatch);

  S.Diag(PrevNS->getLocation(), diag::note_previous_definition);
  *IsInline = PrevNS->isInline();
}

/// ActOnStartNamespaceDef - This is called at the start of a namespace
/// definition.
Decl *Sema::ActOnStartNamespaceDef(
    Scope *NamespcScope, SourceLocation InlineLoc, SourceLocation NamespaceLoc,
    SourceLocation IdentLoc, IdentifierInfo *II, SourceLocation LBrace,
    const ParsedAttributesView &AttrList, UsingDirectiveDecl *&UD) {
  SourceLocation StartLoc = InlineLoc.isValid() ? InlineLoc : NamespaceLoc;
  // For anonymous namespace, take the location of the left brace.
  SourceLocation Loc = II ? IdentLoc : LBrace;
  bool IsInline = InlineLoc.isValid();
  bool IsInvalid = false;
  bool IsStd = false;
  bool AddToKnown = false;
  Scope *DeclRegionScope = NamespcScope->getParent();

  NamespaceDecl *PrevNS = nullptr;
  if (II) {
    // C++ [namespace.def]p2:
    //   The identifier in an original-namespace-definition shall not
    //   have been previously defined in the declarative region in
    //   which the original-namespace-definition appears. The
    //   identifier in an original-namespace-definition is the name of
    //   the namespace. Subsequently in that declarative region, it is
    //   treated as an original-namespace-name.
    //
    // Since namespace names are unique in their scope, and we don't
    // look through using directives, just look for any ordinary names
    // as if by qualified name lookup.
    LookupResult R(*this, II, IdentLoc, LookupOrdinaryName,
                   ForExternalRedeclaration);
    LookupQualifiedName(R, CurContext->getRedeclContext());
    NamedDecl *PrevDecl =
        R.isSingleResult() ? R.getRepresentativeDecl() : nullptr;
    PrevNS = dyn_cast_or_null<NamespaceDecl>(PrevDecl);

    if (PrevNS) {
      // This is an extended namespace definition.
      if (IsInline != PrevNS->isInline())
        DiagnoseNamespaceInlineMismatch(*this, NamespaceLoc, Loc, II,
                                        &IsInline, PrevNS);
    } else if (PrevDecl) {
      // This is an invalid name redefinition.
      Diag(Loc, diag::err_redefinition_different_kind)
        << II;
      Diag(PrevDecl->getLocation(), diag::note_previous_definition);
      IsInvalid = true;
      // Continue on to push Namespc as current DeclContext and return it.
    } else if (II->isStr("std") &&
               CurContext->getRedeclContext()->isTranslationUnit()) {
      // This is the first "real" definition of the namespace "std", so update
      // our cache of the "std" namespace to point at this definition.
      PrevNS = getStdNamespace();
      IsStd = true;
      AddToKnown = !IsInline;
    } else {
      // We've seen this namespace for the first time.
      AddToKnown = !IsInline;
    }
  } else {
    // Anonymous namespaces.

    // Determine whether the parent already has an anonymous namespace.
    DeclContext *Parent = CurContext->getRedeclContext();
    if (TranslationUnitDecl *TU = dyn_cast<TranslationUnitDecl>(Parent)) {
      PrevNS = TU->getAnonymousNamespace();
    } else {
      NamespaceDecl *ND = cast<NamespaceDecl>(Parent);
      PrevNS = ND->getAnonymousNamespace();
    }

    if (PrevNS && IsInline != PrevNS->isInline())
      DiagnoseNamespaceInlineMismatch(*this, NamespaceLoc, NamespaceLoc, II,
                                      &IsInline, PrevNS);
  }

  NamespaceDecl *Namespc = NamespaceDecl::Create(Context, CurContext, IsInline,
                                                 StartLoc, Loc, II, PrevNS);
  if (IsInvalid)
    Namespc->setInvalidDecl();

  ProcessDeclAttributeList(DeclRegionScope, Namespc, AttrList);
  AddPragmaAttributes(DeclRegionScope, Namespc);

  // FIXME: Should we be merging attributes?
  if (const VisibilityAttr *Attr = Namespc->getAttr<VisibilityAttr>())
    PushNamespaceVisibilityAttr(Attr, Loc);

  if (IsStd)
    StdNamespace = Namespc;
  if (AddToKnown)
    KnownNamespaces[Namespc] = false;

  if (II) {
    PushOnScopeChains(Namespc, DeclRegionScope);
  } else {
    // Link the anonymous namespace into its parent.
    DeclContext *Parent = CurContext->getRedeclContext();
    if (TranslationUnitDecl *TU = dyn_cast<TranslationUnitDecl>(Parent)) {
      TU->setAnonymousNamespace(Namespc);
    } else {
      cast<NamespaceDecl>(Parent)->setAnonymousNamespace(Namespc);
    }

    CurContext->addDecl(Namespc);

    // C++ [namespace.unnamed]p1.  An unnamed-namespace-definition
    //   behaves as if it were replaced by
    //     namespace unique { /* empty body */ }
    //     using namespace unique;
    //     namespace unique { namespace-body }
    //   where all occurrences of 'unique' in a translation unit are
    //   replaced by the same identifier and this identifier differs
    //   from all other identifiers in the entire program.

    // We just create the namespace with an empty name and then add an
    // implicit using declaration, just like the standard suggests.
    //
    // CodeGen enforces the "universally unique" aspect by giving all
    // declarations semantically contained within an anonymous
    // namespace internal linkage.

    if (!PrevNS) {
      UD = UsingDirectiveDecl::Create(Context, Parent,
                                      /* 'using' */ LBrace,
                                      /* 'namespace' */ SourceLocation(),
                                      /* qualifier */ NestedNameSpecifierLoc(),
                                      /* identifier */ SourceLocation(),
                                      Namespc,
                                      /* Ancestor */ Parent);
      UD->setImplicit();
      Parent->addDecl(UD);
    }
  }

  ActOnDocumentableDecl(Namespc);

  // Although we could have an invalid decl (i.e. the namespace name is a
  // redefinition), push it as current DeclContext and try to continue parsing.
  // FIXME: We should be able to push Namespc here, so that the each DeclContext
  // for the namespace has the declarations that showed up in that particular
  // namespace definition.
  PushDeclContext(NamespcScope, Namespc);
  return Namespc;
}

/// getNamespaceDecl - Returns the namespace a decl represents. If the decl
/// is a namespace alias, returns the namespace it points to.
static inline NamespaceDecl *getNamespaceDecl(NamedDecl *D) {
  if (NamespaceAliasDecl *AD = dyn_cast_or_null<NamespaceAliasDecl>(D))
    return AD->getNamespace();
  return dyn_cast_or_null<NamespaceDecl>(D);
}

/// ActOnFinishNamespaceDef - This callback is called after a namespace is
/// exited. Decl is the DeclTy returned by ActOnStartNamespaceDef.
void Sema::ActOnFinishNamespaceDef(Decl *Dcl, SourceLocation RBrace) {
  NamespaceDecl *Namespc = dyn_cast_or_null<NamespaceDecl>(Dcl);
  assert(Namespc && "Invalid parameter, expected NamespaceDecl");
  Namespc->setRBraceLoc(RBrace);
  PopDeclContext();
  if (Namespc->hasAttr<VisibilityAttr>())
    PopPragmaVisibility(true, RBrace);
}

CXXRecordDecl *Sema::getStdBadAlloc() const {
  return cast_or_null<CXXRecordDecl>(
                                  StdBadAlloc.get(Context.getExternalSource()));
}

EnumDecl *Sema::getStdAlignValT() const {
  return cast_or_null<EnumDecl>(StdAlignValT.get(Context.getExternalSource()));
}

NamespaceDecl *Sema::getStdNamespace() const {
  return cast_or_null<NamespaceDecl>(
                                 StdNamespace.get(Context.getExternalSource()));
}

NamespaceDecl *Sema::lookupStdExperimentalNamespace() {
  if (!StdExperimentalNamespaceCache) {
    if (auto Std = getStdNamespace()) {
      LookupResult Result(*this, &PP.getIdentifierTable().get("experimental"),
                          SourceLocation(), LookupNamespaceName);
      if (!LookupQualifiedName(Result, Std) ||
          !(StdExperimentalNamespaceCache =
                Result.getAsSingle<NamespaceDecl>()))
        Result.suppressDiagnostics();
    }
  }
  return StdExperimentalNamespaceCache;
}

namespace {

enum UnsupportedSTLSelect {
  USS_InvalidMember,
  USS_MissingMember,
  USS_NonTrivial,
  USS_Other
};

struct InvalidSTLDiagnoser {
  Sema &S;
  SourceLocation Loc;
  QualType TyForDiags;

  QualType operator()(UnsupportedSTLSelect Sel = USS_Other, StringRef Name = "",
                      const VarDecl *VD = nullptr) {
    {
      auto D = S.Diag(Loc, diag::err_std_compare_type_not_supported)
               << TyForDiags << ((int)Sel);
      if (Sel == USS_InvalidMember || Sel == USS_MissingMember) {
        assert(!Name.empty());
        D << Name;
      }
    }
    if (Sel == USS_InvalidMember) {
      S.Diag(VD->getLocation(), diag::note_var_declared_here)
          << VD << VD->getSourceRange();
    }
    return QualType();
  }
};
} // namespace

QualType Sema::CheckComparisonCategoryType(ComparisonCategoryType Kind,
                                           SourceLocation Loc) {
  assert(getLangOpts().CPlusPlus &&
         "Looking for comparison category type outside of C++.");

  // Check if we've already successfully checked the comparison category type
  // before. If so, skip checking it again.
  ComparisonCategoryInfo *Info = Context.CompCategories.lookupInfo(Kind);
  if (Info && FullyCheckedComparisonCategories[static_cast<unsigned>(Kind)])
    return Info->getType();

  // If lookup failed
  if (!Info) {
    std::string NameForDiags = "std::";
    NameForDiags += ComparisonCategories::getCategoryString(Kind);
    Diag(Loc, diag::err_implied_comparison_category_type_not_found)
        << NameForDiags;
    return QualType();
  }

  assert(Info->Kind == Kind);
  assert(Info->Record);

  // Update the Record decl in case we encountered a forward declaration on our
  // first pass. FIXME: This is a bit of a hack.
  if (Info->Record->hasDefinition())
    Info->Record = Info->Record->getDefinition();

  // Use an elaborated type for diagnostics which has a name containing the
  // prepended 'std' namespace but not any inline namespace names.
  QualType TyForDiags = [&]() {
    auto *NNS =
        NestedNameSpecifier::Create(Context, nullptr, getStdNamespace());
    return Context.getElaboratedType(ETK_None, NNS, Info->getType());
  }();

  if (RequireCompleteType(Loc, TyForDiags, diag::err_incomplete_type))
    return QualType();

  InvalidSTLDiagnoser UnsupportedSTLError{*this, Loc, TyForDiags};

  if (!Info->Record->isTriviallyCopyable())
    return UnsupportedSTLError(USS_NonTrivial);

  for (const CXXBaseSpecifier &BaseSpec : Info->Record->bases()) {
    CXXRecordDecl *Base = BaseSpec.getType()->getAsCXXRecordDecl();
    // Tolerate empty base classes.
    if (Base->isEmpty())
      continue;
    // Reject STL implementations which have at least one non-empty base.
    return UnsupportedSTLError();
  }

  // Check that the STL has implemented the types using a single integer field.
  // This expectation allows better codegen for builtin operators. We require:
  //   (1) The class has exactly one field.
  //   (2) The field is an integral or enumeration type.
  auto FIt = Info->Record->field_begin(), FEnd = Info->Record->field_end();
  if (std::distance(FIt, FEnd) != 1 ||
      !FIt->getType()->isIntegralOrEnumerationType()) {
    return UnsupportedSTLError();
  }

  // Build each of the require values and store them in Info.
  for (ComparisonCategoryResult CCR :
       ComparisonCategories::getPossibleResultsForType(Kind)) {
    StringRef MemName = ComparisonCategories::getResultString(CCR);
    ComparisonCategoryInfo::ValueInfo *ValInfo = Info->lookupValueInfo(CCR);

    if (!ValInfo)
      return UnsupportedSTLError(USS_MissingMember, MemName);

    VarDecl *VD = ValInfo->VD;
    assert(VD && "should not be null!");

    // Attempt to diagnose reasons why the STL definition of this type
    // might be foobar, including it failing to be a constant expression.
    // TODO Handle more ways the lookup or result can be invalid.
    if (!VD->isStaticDataMember() || !VD->isConstexpr() || !VD->hasInit() ||
        !VD->checkInitIsICE())
      return UnsupportedSTLError(USS_InvalidMember, MemName, VD);

    // Attempt to evaluate the var decl as a constant expression and extract
    // the value of its first field as a ICE. If this fails, the STL
    // implementation is not supported.
    if (!ValInfo->hasValidIntValue())
      return UnsupportedSTLError();

    MarkVariableReferenced(Loc, VD);
  }

  // We've successfully built the required types and expressions. Update
  // the cache and return the newly cached value.
  FullyCheckedComparisonCategories[static_cast<unsigned>(Kind)] = true;
  return Info->getType();
}

/// Retrieve the special "std" namespace, which may require us to
/// implicitly define the namespace.
NamespaceDecl *Sema::getOrCreateStdNamespace() {
  if (!StdNamespace) {
    // The "std" namespace has not yet been defined, so build one implicitly.
    StdNamespace = NamespaceDecl::Create(Context,
                                         Context.getTranslationUnitDecl(),
                                         /*Inline=*/false,
                                         SourceLocation(), SourceLocation(),
                                         &PP.getIdentifierTable().get("std"),
                                         /*PrevDecl=*/nullptr);
    getStdNamespace()->setImplicit(true);
  }

  return getStdNamespace();
}

bool Sema::isStdInitializerList(QualType Ty, QualType *Element) {
  assert(getLangOpts().CPlusPlus &&
         "Looking for std::initializer_list outside of C++.");

  // We're looking for implicit instantiations of
  // template <typename E> class std::initializer_list.

  if (!StdNamespace) // If we haven't seen namespace std yet, this can't be it.
    return false;

  ClassTemplateDecl *Template = nullptr;
  const TemplateArgument *Arguments = nullptr;

  if (const RecordType *RT = Ty->getAs<RecordType>()) {

    ClassTemplateSpecializationDecl *Specialization =
        dyn_cast<ClassTemplateSpecializationDecl>(RT->getDecl());
    if (!Specialization)
      return false;

    Template = Specialization->getSpecializedTemplate();
    Arguments = Specialization->getTemplateArgs().data();
  } else if (const TemplateSpecializationType *TST =
                 Ty->getAs<TemplateSpecializationType>()) {
    Template = dyn_cast_or_null<ClassTemplateDecl>(
        TST->getTemplateName().getAsTemplateDecl());
    Arguments = TST->getArgs();
  }
  if (!Template)
    return false;

  if (!StdInitializerList) {
    // Haven't recognized std::initializer_list yet, maybe this is it.
    CXXRecordDecl *TemplateClass = Template->getTemplatedDecl();
    if (TemplateClass->getIdentifier() !=
            &PP.getIdentifierTable().get("initializer_list") ||
        !getStdNamespace()->InEnclosingNamespaceSetOf(
            TemplateClass->getDeclContext()))
      return false;
    // This is a template called std::initializer_list, but is it the right
    // template?
    TemplateParameterList *Params = Template->getTemplateParameters();
    if (Params->getMinRequiredArguments() != 1)
      return false;
    if (!isa<TemplateTypeParmDecl>(Params->getParam(0)))
      return false;

    // It's the right template.
    StdInitializerList = Template;
  }

  if (Template->getCanonicalDecl() != StdInitializerList->getCanonicalDecl())
    return false;

  // This is an instance of std::initializer_list. Find the argument type.
  if (Element)
    *Element = Arguments[0].getAsType();
  return true;
}

static ClassTemplateDecl *LookupStdInitializerList(Sema &S, SourceLocation Loc){
  NamespaceDecl *Std = S.getStdNamespace();
  if (!Std) {
    S.Diag(Loc, diag::err_implied_std_initializer_list_not_found);
    return nullptr;
  }

  LookupResult Result(S, &S.PP.getIdentifierTable().get("initializer_list"),
                      Loc, Sema::LookupOrdinaryName);
  if (!S.LookupQualifiedName(Result, Std)) {
    S.Diag(Loc, diag::err_implied_std_initializer_list_not_found);
    return nullptr;
  }
  ClassTemplateDecl *Template = Result.getAsSingle<ClassTemplateDecl>();
  if (!Template) {
    Result.suppressDiagnostics();
    // We found something weird. Complain about the first thing we found.
    NamedDecl *Found = *Result.begin();
    S.Diag(Found->getLocation(), diag::err_malformed_std_initializer_list);
    return nullptr;
  }

  // We found some template called std::initializer_list. Now verify that it's
  // correct.
  TemplateParameterList *Params = Template->getTemplateParameters();
  if (Params->getMinRequiredArguments() != 1 ||
      !isa<TemplateTypeParmDecl>(Params->getParam(0))) {
    S.Diag(Template->getLocation(), diag::err_malformed_std_initializer_list);
    return nullptr;
  }

  return Template;
}

QualType Sema::BuildStdInitializerList(QualType Element, SourceLocation Loc) {
  if (!StdInitializerList) {
    StdInitializerList = LookupStdInitializerList(*this, Loc);
    if (!StdInitializerList)
      return QualType();
  }

  TemplateArgumentListInfo Args(Loc, Loc);
  Args.addArgument(TemplateArgumentLoc(TemplateArgument(Element),
                                       Context.getTrivialTypeSourceInfo(Element,
                                                                        Loc)));
  return Context.getCanonicalType(
      CheckTemplateIdType(TemplateName(StdInitializerList), Loc, Args));
}

bool Sema::isInitListConstructor(const FunctionDecl *Ctor) {
  // C++ [dcl.init.list]p2:
  //   A constructor is an initializer-list constructor if its first parameter
  //   is of type std::initializer_list<E> or reference to possibly cv-qualified
  //   std::initializer_list<E> for some type E, and either there are no other
  //   parameters or else all other parameters have default arguments.
  if (Ctor->getNumParams() < 1 ||
      (Ctor->getNumParams() > 1 && !Ctor->getParamDecl(1)->hasDefaultArg()))
    return false;

  QualType ArgType = Ctor->getParamDecl(0)->getType();
  if (const ReferenceType *RT = ArgType->getAs<ReferenceType>())
    ArgType = RT->getPointeeType().getUnqualifiedType();

  return isStdInitializerList(ArgType, nullptr);
}

/// Determine whether a using statement is in a context where it will be
/// apply in all contexts.
static bool IsUsingDirectiveInToplevelContext(DeclContext *CurContext) {
  switch (CurContext->getDeclKind()) {
    case Decl::TranslationUnit:
      return true;
    case Decl::LinkageSpec:
      return IsUsingDirectiveInToplevelContext(CurContext->getParent());
    default:
      return false;
  }
}

namespace {

// Callback to only accept typo corrections that are namespaces.
class NamespaceValidatorCCC : public CorrectionCandidateCallback {
public:
  bool ValidateCandidate(const TypoCorrection &candidate) override {
    if (NamedDecl *ND = candidate.getCorrectionDecl())
      return isa<NamespaceDecl>(ND) || isa<NamespaceAliasDecl>(ND);
    return false;
  }
};

}

static bool TryNamespaceTypoCorrection(Sema &S, LookupResult &R, Scope *Sc,
                                       CXXScopeSpec &SS,
                                       SourceLocation IdentLoc,
                                       IdentifierInfo *Ident) {
  R.clear();
  if (TypoCorrection Corrected =
          S.CorrectTypo(R.getLookupNameInfo(), R.getLookupKind(), Sc, &SS,
                        llvm::make_unique<NamespaceValidatorCCC>(),
                        Sema::CTK_ErrorRecovery)) {
    if (DeclContext *DC = S.computeDeclContext(SS, false)) {
      std::string CorrectedStr(Corrected.getAsString(S.getLangOpts()));
      bool DroppedSpecifier = Corrected.WillReplaceSpecifier() &&
                              Ident->getName().equals(CorrectedStr);
      S.diagnoseTypo(Corrected,
                     S.PDiag(diag::err_using_directive_member_suggest)
                       << Ident << DC << DroppedSpecifier << SS.getRange(),
                     S.PDiag(diag::note_namespace_defined_here));
    } else {
      S.diagnoseTypo(Corrected,
                     S.PDiag(diag::err_using_directive_suggest) << Ident,
                     S.PDiag(diag::note_namespace_defined_here));
    }
    R.addDecl(Corrected.getFoundDecl());
    return true;
  }
  return false;
}

Decl *Sema::ActOnUsingDirective(Scope *S, SourceLocation UsingLoc,
                                SourceLocation NamespcLoc, CXXScopeSpec &SS,
                                SourceLocation IdentLoc,
                                IdentifierInfo *NamespcName,
                                const ParsedAttributesView &AttrList) {
  assert(!SS.isInvalid() && "Invalid CXXScopeSpec.");
  assert(NamespcName && "Invalid NamespcName.");
  assert(IdentLoc.isValid() && "Invalid NamespceName location.");

  // This can only happen along a recovery path.
  while (S->isTemplateParamScope())
    S = S->getParent();
  assert(S->getFlags() & Scope::DeclScope && "Invalid Scope.");

  UsingDirectiveDecl *UDir = nullptr;
  NestedNameSpecifier *Qualifier = nullptr;
  if (SS.isSet())
    Qualifier = SS.getScopeRep();

  // Lookup namespace name.
  LookupResult R(*this, NamespcName, IdentLoc, LookupNamespaceName);
  LookupParsedName(R, S, &SS);
  if (R.isAmbiguous())
    return nullptr;

  if (R.empty()) {
    R.clear();
    // Allow "using namespace std;" or "using namespace ::std;" even if
    // "std" hasn't been defined yet, for GCC compatibility.
    if ((!Qualifier || Qualifier->getKind() == NestedNameSpecifier::Global) &&
        NamespcName->isStr("std")) {
      Diag(IdentLoc, diag::ext_using_undefined_std);
      R.addDecl(getOrCreateStdNamespace());
      R.resolveKind();
    }
    // Otherwise, attempt typo correction.
    else TryNamespaceTypoCorrection(*this, R, S, SS, IdentLoc, NamespcName);
  }

  if (!R.empty()) {
    NamedDecl *Named = R.getRepresentativeDecl();
    NamespaceDecl *NS = R.getAsSingle<NamespaceDecl>();
    assert(NS && "expected namespace decl");

    // The use of a nested name specifier may trigger deprecation warnings.
    DiagnoseUseOfDecl(Named, IdentLoc);

    // C++ [namespace.udir]p1:
    //   A using-directive specifies that the names in the nominated
    //   namespace can be used in the scope in which the
    //   using-directive appears after the using-directive. During
    //   unqualified name lookup (3.4.1), the names appear as if they
    //   were declared in the nearest enclosing namespace which
    //   contains both the using-directive and the nominated
    //   namespace. [Note: in this context, "contains" means "contains
    //   directly or indirectly". ]

    // Find enclosing context containing both using-directive and
    // nominated namespace.
    DeclContext *CommonAncestor = NS;
    while (CommonAncestor && !CommonAncestor->Encloses(CurContext))
      CommonAncestor = CommonAncestor->getParent();

    UDir = UsingDirectiveDecl::Create(Context, CurContext, UsingLoc, NamespcLoc,
                                      SS.getWithLocInContext(Context),
                                      IdentLoc, Named, CommonAncestor);

    if (IsUsingDirectiveInToplevelContext(CurContext) &&
        !SourceMgr.isInMainFile(SourceMgr.getExpansionLoc(IdentLoc))) {
      Diag(IdentLoc, diag::warn_using_directive_in_header);
    }

    PushUsingDirective(S, UDir);
  } else {
    Diag(IdentLoc, diag::err_expected_namespace_name) << SS.getRange();
  }

  if (UDir)
    ProcessDeclAttributeList(S, UDir, AttrList);

  return UDir;
}

void Sema::PushUsingDirective(Scope *S, UsingDirectiveDecl *UDir) {
  // If the scope has an associated entity and the using directive is at
  // namespace or translation unit scope, add the UsingDirectiveDecl into
  // its lookup structure so qualified name lookup can find it.
  DeclContext *Ctx = S->getEntity();
  if (Ctx && !Ctx->isFunctionOrMethod())
    Ctx->addDecl(UDir);
  else
    // Otherwise, it is at block scope. The using-directives will affect lookup
    // only to the end of the scope.
    S->PushUsingDirective(UDir);
}

Decl *Sema::ActOnUsingDeclaration(Scope *S, AccessSpecifier AS,
                                  SourceLocation UsingLoc,
                                  SourceLocation TypenameLoc, CXXScopeSpec &SS,
                                  UnqualifiedId &Name,
                                  SourceLocation EllipsisLoc,
                                  const ParsedAttributesView &AttrList) {
  assert(S->getFlags() & Scope::DeclScope && "Invalid Scope.");

  if (SS.isEmpty()) {
    Diag(Name.getBeginLoc(), diag::err_using_requires_qualname);
    return nullptr;
  }

  switch (Name.getKind()) {
  case UnqualifiedIdKind::IK_ImplicitSelfParam:
  case UnqualifiedIdKind::IK_Identifier:
  case UnqualifiedIdKind::IK_OperatorFunctionId:
  case UnqualifiedIdKind::IK_LiteralOperatorId:
  case UnqualifiedIdKind::IK_ConversionFunctionId:
    break;

  case UnqualifiedIdKind::IK_ConstructorName:
  case UnqualifiedIdKind::IK_ConstructorTemplateId:
    // C++11 inheriting constructors.
    Diag(Name.getBeginLoc(),
         getLangOpts().CPlusPlus11
             ? diag::warn_cxx98_compat_using_decl_constructor
             : diag::err_using_decl_constructor)
        << SS.getRange();

    if (getLangOpts().CPlusPlus11) break;

    return nullptr;

  case UnqualifiedIdKind::IK_DestructorName:
    Diag(Name.getBeginLoc(), diag::err_using_decl_destructor) << SS.getRange();
    return nullptr;

  case UnqualifiedIdKind::IK_TemplateId:
    Diag(Name.getBeginLoc(), diag::err_using_decl_template_id)
        << SourceRange(Name.TemplateId->LAngleLoc, Name.TemplateId->RAngleLoc);
    return nullptr;

  case UnqualifiedIdKind::IK_DeductionGuideName:
    llvm_unreachable("cannot parse qualified deduction guide name");
  }

  DeclarationNameInfo TargetNameInfo = GetNameFromUnqualifiedId(Name);
  DeclarationName TargetName = TargetNameInfo.getName();
  if (!TargetName)
    return nullptr;

  // Warn about access declarations.
  if (UsingLoc.isInvalid()) {
    Diag(Name.getBeginLoc(), getLangOpts().CPlusPlus11
                                 ? diag::err_access_decl
                                 : diag::warn_access_decl_deprecated)
        << FixItHint::CreateInsertion(SS.getRange().getBegin(), "using ");
  }

  if (EllipsisLoc.isInvalid()) {
    if (DiagnoseUnexpandedParameterPack(SS, UPPC_UsingDeclaration) ||
        DiagnoseUnexpandedParameterPack(TargetNameInfo, UPPC_UsingDeclaration))
      return nullptr;
  } else {
    if (!SS.getScopeRep()->containsUnexpandedParameterPack() &&
        !TargetNameInfo.containsUnexpandedParameterPack()) {
      Diag(EllipsisLoc, diag::err_pack_expansion_without_parameter_packs)
        << SourceRange(SS.getBeginLoc(), TargetNameInfo.getEndLoc());
      EllipsisLoc = SourceLocation();
    }
  }

  NamedDecl *UD =
      BuildUsingDeclaration(S, AS, UsingLoc, TypenameLoc.isValid(), TypenameLoc,
                            SS, TargetNameInfo, EllipsisLoc, AttrList,
                            /*IsInstantiation*/false);
  if (UD)
    PushOnScopeChains(UD, S, /*AddToContext*/ false);

  return UD;
}

/// Determine whether a using declaration considers the given
/// declarations as "equivalent", e.g., if they are redeclarations of
/// the same entity or are both typedefs of the same type.
static bool
IsEquivalentForUsingDecl(ASTContext &Context, NamedDecl *D1, NamedDecl *D2) {
  if (D1->getCanonicalDecl() == D2->getCanonicalDecl())
    return true;

  if (TypedefNameDecl *TD1 = dyn_cast<TypedefNameDecl>(D1))
    if (TypedefNameDecl *TD2 = dyn_cast<TypedefNameDecl>(D2))
      return Context.hasSameType(TD1->getUnderlyingType(),
                                 TD2->getUnderlyingType());

  return false;
}


/// Determines whether to create a using shadow decl for a particular
/// decl, given the set of decls existing prior to this using lookup.
bool Sema::CheckUsingShadowDecl(UsingDecl *Using, NamedDecl *Orig,
                                const LookupResult &Previous,
                                UsingShadowDecl *&PrevShadow) {
  // Diagnose finding a decl which is not from a base class of the
  // current class.  We do this now because there are cases where this
  // function will silently decide not to build a shadow decl, which
  // will pre-empt further diagnostics.
  //
  // We don't need to do this in C++11 because we do the check once on
  // the qualifier.
  //
  // FIXME: diagnose the following if we care enough:
  //   struct A { int foo; };
  //   struct B : A { using A::foo; };
  //   template <class T> struct C : A {};
  //   template <class T> struct D : C<T> { using B::foo; } // <---
  // This is invalid (during instantiation) in C++03 because B::foo
  // resolves to the using decl in B, which is not a base class of D<T>.
  // We can't diagnose it immediately because C<T> is an unknown
  // specialization.  The UsingShadowDecl in D<T> then points directly
  // to A::foo, which will look well-formed when we instantiate.
  // The right solution is to not collapse the shadow-decl chain.
  if (!getLangOpts().CPlusPlus11 && CurContext->isRecord()) {
    DeclContext *OrigDC = Orig->getDeclContext();

    // Handle enums and anonymous structs.
    if (isa<EnumDecl>(OrigDC)) OrigDC = OrigDC->getParent();
    CXXRecordDecl *OrigRec = cast<CXXRecordDecl>(OrigDC);
    while (OrigRec->isAnonymousStructOrUnion())
      OrigRec = cast<CXXRecordDecl>(OrigRec->getDeclContext());

    if (cast<CXXRecordDecl>(CurContext)->isProvablyNotDerivedFrom(OrigRec)) {
      if (OrigDC == CurContext) {
        Diag(Using->getLocation(),
             diag::err_using_decl_nested_name_specifier_is_current_class)
          << Using->getQualifierLoc().getSourceRange();
        Diag(Orig->getLocation(), diag::note_using_decl_target);
        Using->setInvalidDecl();
        return true;
      }

      Diag(Using->getQualifierLoc().getBeginLoc(),
           diag::err_using_decl_nested_name_specifier_is_not_base_class)
        << Using->getQualifier()
        << cast<CXXRecordDecl>(CurContext)
        << Using->getQualifierLoc().getSourceRange();
      Diag(Orig->getLocation(), diag::note_using_decl_target);
      Using->setInvalidDecl();
      return true;
    }
  }

  if (Previous.empty()) return false;

  NamedDecl *Target = Orig;
  if (isa<UsingShadowDecl>(Target))
    Target = cast<UsingShadowDecl>(Target)->getTargetDecl();

  // If the target happens to be one of the previous declarations, we
  // don't have a conflict.
  //
  // FIXME: but we might be increasing its access, in which case we
  // should redeclare it.
  NamedDecl *NonTag = nullptr, *Tag = nullptr;
  bool FoundEquivalentDecl = false;
  for (LookupResult::iterator I = Previous.begin(), E = Previous.end();
         I != E; ++I) {
    NamedDecl *D = (*I)->getUnderlyingDecl();
    // We can have UsingDecls in our Previous results because we use the same
    // LookupResult for checking whether the UsingDecl itself is a valid
    // redeclaration.
    if (isa<UsingDecl>(D) || isa<UsingPackDecl>(D))
      continue;

    if (auto *RD = dyn_cast<CXXRecordDecl>(D)) {
      // C++ [class.mem]p19:
      //   If T is the name of a class, then [every named member other than
      //   a non-static data member] shall have a name different from T
      if (RD->isInjectedClassName() && !isa<FieldDecl>(Target) &&
          !isa<IndirectFieldDecl>(Target) &&
          !isa<UnresolvedUsingValueDecl>(Target) &&
          DiagnoseClassNameShadow(
              CurContext,
              DeclarationNameInfo(Using->getDeclName(), Using->getLocation())))
        return true;
    }

    if (IsEquivalentForUsingDecl(Context, D, Target)) {
      if (UsingShadowDecl *Shadow = dyn_cast<UsingShadowDecl>(*I))
        PrevShadow = Shadow;
      FoundEquivalentDecl = true;
    } else if (isEquivalentInternalLinkageDeclaration(D, Target)) {
      // We don't conflict with an existing using shadow decl of an equivalent
      // declaration, but we're not a redeclaration of it.
      FoundEquivalentDecl = true;
    }

    if (isVisible(D))
      (isa<TagDecl>(D) ? Tag : NonTag) = D;
  }

  if (FoundEquivalentDecl)
    return false;

  if (FunctionDecl *FD = Target->getAsFunction()) {
    NamedDecl *OldDecl = nullptr;
    switch (CheckOverload(nullptr, FD, Previous, OldDecl,
                          /*IsForUsingDecl*/ true)) {
    case Ovl_Overload:
      return false;

    case Ovl_NonFunction:
      Diag(Using->getLocation(), diag::err_using_decl_conflict);
      break;

    // We found a decl with the exact signature.
    case Ovl_Match:
      // If we're in a record, we want to hide the target, so we
      // return true (without a diagnostic) to tell the caller not to
      // build a shadow decl.
      if (CurContext->isRecord())
        return true;

      // If we're not in a record, this is an error.
      Diag(Using->getLocation(), diag::err_using_decl_conflict);
      break;
    }

    Diag(Target->getLocation(), diag::note_using_decl_target);
    Diag(OldDecl->getLocation(), diag::note_using_decl_conflict);
    Using->setInvalidDecl();
    return true;
  }

  // Target is not a function.

  if (isa<TagDecl>(Target)) {
    // No conflict between a tag and a non-tag.
    if (!Tag) return false;

    Diag(Using->getLocation(), diag::err_using_decl_conflict);
    Diag(Target->getLocation(), diag::note_using_decl_target);
    Diag(Tag->getLocation(), diag::note_using_decl_conflict);
    Using->setInvalidDecl();
    return true;
  }

  // No conflict between a tag and a non-tag.
  if (!NonTag) return false;

  Diag(Using->getLocation(), diag::err_using_decl_conflict);
  Diag(Target->getLocation(), diag::note_using_decl_target);
  Diag(NonTag->getLocation(), diag::note_using_decl_conflict);
  Using->setInvalidDecl();
  return true;
}

/// Determine whether a direct base class is a virtual base class.
static bool isVirtualDirectBase(CXXRecordDecl *Derived, CXXRecordDecl *Base) {
  if (!Derived->getNumVBases())
    return false;
  for (auto &B : Derived->bases())
    if (B.getType()->getAsCXXRecordDecl() == Base)
      return B.isVirtual();
  llvm_unreachable("not a direct base class");
}

/// Builds a shadow declaration corresponding to a 'using' declaration.
UsingShadowDecl *Sema::BuildUsingShadowDecl(Scope *S,
                                            UsingDecl *UD,
                                            NamedDecl *Orig,
                                            UsingShadowDecl *PrevDecl) {
  // If we resolved to another shadow declaration, just coalesce them.
  NamedDecl *Target = Orig;
  if (isa<UsingShadowDecl>(Target)) {
    Target = cast<UsingShadowDecl>(Target)->getTargetDecl();
    assert(!isa<UsingShadowDecl>(Target) && "nested shadow declaration");
  }

  NamedDecl *NonTemplateTarget = Target;
  if (auto *TargetTD = dyn_cast<TemplateDecl>(Target))
    NonTemplateTarget = TargetTD->getTemplatedDecl();

  UsingShadowDecl *Shadow;
  if (isa<CXXConstructorDecl>(NonTemplateTarget)) {
    bool IsVirtualBase =
        isVirtualDirectBase(cast<CXXRecordDecl>(CurContext),
                            UD->getQualifier()->getAsRecordDecl());
    Shadow = ConstructorUsingShadowDecl::Create(
        Context, CurContext, UD->getLocation(), UD, Orig, IsVirtualBase);
  } else {
    Shadow = UsingShadowDecl::Create(Context, CurContext, UD->getLocation(), UD,
                                     Target);
  }
  UD->addShadowDecl(Shadow);

  Shadow->setAccess(UD->getAccess());
  if (Orig->isInvalidDecl() || UD->isInvalidDecl())
    Shadow->setInvalidDecl();

  Shadow->setPreviousDecl(PrevDecl);

  if (S)
    PushOnScopeChains(Shadow, S);
  else
    CurContext->addDecl(Shadow);


  return Shadow;
}

/// Hides a using shadow declaration.  This is required by the current
/// using-decl implementation when a resolvable using declaration in a
/// class is followed by a declaration which would hide or override
/// one or more of the using decl's targets; for example:
///
///   struct Base { void foo(int); };
///   struct Derived : Base {
///     using Base::foo;
///     void foo(int);
///   };
///
/// The governing language is C++03 [namespace.udecl]p12:
///
///   When a using-declaration brings names from a base class into a
///   derived class scope, member functions in the derived class
///   override and/or hide member functions with the same name and
///   parameter types in a base class (rather than conflicting).
///
/// There are two ways to implement this:
///   (1) optimistically create shadow decls when they're not hidden
///       by existing declarations, or
///   (2) don't create any shadow decls (or at least don't make them
///       visible) until we've fully parsed/instantiated the class.
/// The problem with (1) is that we might have to retroactively remove
/// a shadow decl, which requires several O(n) operations because the
/// decl structures are (very reasonably) not designed for removal.
/// (2) avoids this but is very fiddly and phase-dependent.
void Sema::HideUsingShadowDecl(Scope *S, UsingShadowDecl *Shadow) {
  if (Shadow->getDeclName().getNameKind() ==
        DeclarationName::CXXConversionFunctionName)
    cast<CXXRecordDecl>(Shadow->getDeclContext())->removeConversion(Shadow);

  // Remove it from the DeclContext...
  Shadow->getDeclContext()->removeDecl(Shadow);

  // ...and the scope, if applicable...
  if (S) {
    S->RemoveDecl(Shadow);
    IdResolver.RemoveDecl(Shadow);
  }

  // ...and the using decl.
  Shadow->getUsingDecl()->removeShadowDecl(Shadow);

  // TODO: complain somehow if Shadow was used.  It shouldn't
  // be possible for this to happen, because...?
}

/// Find the base specifier for a base class with the given type.
static CXXBaseSpecifier *findDirectBaseWithType(CXXRecordDecl *Derived,
                                                QualType DesiredBase,
                                                bool &AnyDependentBases) {
  // Check whether the named type is a direct base class.
  CanQualType CanonicalDesiredBase = DesiredBase->getCanonicalTypeUnqualified();
  for (auto &Base : Derived->bases()) {
    CanQualType BaseType = Base.getType()->getCanonicalTypeUnqualified();
    if (CanonicalDesiredBase == BaseType)
      return &Base;
    if (BaseType->isDependentType())
      AnyDependentBases = true;
  }
  return nullptr;
}

namespace {
class UsingValidatorCCC : public CorrectionCandidateCallback {
public:
  UsingValidatorCCC(bool HasTypenameKeyword, bool IsInstantiation,
                    NestedNameSpecifier *NNS, CXXRecordDecl *RequireMemberOf)
      : HasTypenameKeyword(HasTypenameKeyword),
        IsInstantiation(IsInstantiation), OldNNS(NNS),
        RequireMemberOf(RequireMemberOf) {}

  bool ValidateCandidate(const TypoCorrection &Candidate) override {
    NamedDecl *ND = Candidate.getCorrectionDecl();

    // Keywords are not valid here.
    if (!ND || isa<NamespaceDecl>(ND))
      return false;

    // Completely unqualified names are invalid for a 'using' declaration.
    if (Candidate.WillReplaceSpecifier() && !Candidate.getCorrectionSpecifier())
      return false;

    // FIXME: Don't correct to a name that CheckUsingDeclRedeclaration would
    // reject.

    if (RequireMemberOf) {
      auto *FoundRecord = dyn_cast<CXXRecordDecl>(ND);
      if (FoundRecord && FoundRecord->isInjectedClassName()) {
        // No-one ever wants a using-declaration to name an injected-class-name
        // of a base class, unless they're declaring an inheriting constructor.
        ASTContext &Ctx = ND->getASTContext();
        if (!Ctx.getLangOpts().CPlusPlus11)
          return false;
        QualType FoundType = Ctx.getRecordType(FoundRecord);

        // Check that the injected-class-name is named as a member of its own
        // type; we don't want to suggest 'using Derived::Base;', since that
        // means something else.
        NestedNameSpecifier *Specifier =
            Candidate.WillReplaceSpecifier()
                ? Candidate.getCorrectionSpecifier()
                : OldNNS;
        if (!Specifier->getAsType() ||
            !Ctx.hasSameType(QualType(Specifier->getAsType(), 0), FoundType))
          return false;

        // Check that this inheriting constructor declaration actually names a
        // direct base class of the current class.
        bool AnyDependentBases = false;
        if (!findDirectBaseWithType(RequireMemberOf,
                                    Ctx.getRecordType(FoundRecord),
                                    AnyDependentBases) &&
            !AnyDependentBases)
          return false;
      } else {
        auto *RD = dyn_cast<CXXRecordDecl>(ND->getDeclContext());
        if (!RD || RequireMemberOf->isProvablyNotDerivedFrom(RD))
          return false;

        // FIXME: Check that the base class member is accessible?
      }
    } else {
      auto *FoundRecord = dyn_cast<CXXRecordDecl>(ND);
      if (FoundRecord && FoundRecord->isInjectedClassName())
        return false;
    }

    if (isa<TypeDecl>(ND))
      return HasTypenameKeyword || !IsInstantiation;

    return !HasTypenameKeyword;
  }

private:
  bool HasTypenameKeyword;
  bool IsInstantiation;
  NestedNameSpecifier *OldNNS;
  CXXRecordDecl *RequireMemberOf;
};
} // end anonymous namespace

/// Builds a using declaration.
///
/// \param IsInstantiation - Whether this call arises from an
///   instantiation of an unresolved using declaration.  We treat
///   the lookup differently for these declarations.
NamedDecl *Sema::BuildUsingDeclaration(
    Scope *S, AccessSpecifier AS, SourceLocation UsingLoc,
    bool HasTypenameKeyword, SourceLocation TypenameLoc, CXXScopeSpec &SS,
    DeclarationNameInfo NameInfo, SourceLocation EllipsisLoc,
    const ParsedAttributesView &AttrList, bool IsInstantiation) {
  assert(!SS.isInvalid() && "Invalid CXXScopeSpec.");
  SourceLocation IdentLoc = NameInfo.getLoc();
  assert(IdentLoc.isValid() && "Invalid TargetName location.");

  // FIXME: We ignore attributes for now.

  // For an inheriting constructor declaration, the name of the using
  // declaration is the name of a constructor in this class, not in the
  // base class.
  DeclarationNameInfo UsingName = NameInfo;
  if (UsingName.getName().getNameKind() == DeclarationName::CXXConstructorName)
    if (auto *RD = dyn_cast<CXXRecordDecl>(CurContext))
      UsingName.setName(Context.DeclarationNames.getCXXConstructorName(
          Context.getCanonicalType(Context.getRecordType(RD))));

  // Do the redeclaration lookup in the current scope.
  LookupResult Previous(*this, UsingName, LookupUsingDeclName,
                        ForVisibleRedeclaration);
  Previous.setHideTags(false);
  if (S) {
    LookupName(Previous, S);

    // It is really dumb that we have to do this.
    LookupResult::Filter F = Previous.makeFilter();
    while (F.hasNext()) {
      NamedDecl *D = F.next();
      if (!isDeclInScope(D, CurContext, S))
        F.erase();
      // If we found a local extern declaration that's not ordinarily visible,
      // and this declaration is being added to a non-block scope, ignore it.
      // We're only checking for scope conflicts here, not also for violations
      // of the linkage rules.
      else if (!CurContext->isFunctionOrMethod() && D->isLocalExternDecl() &&
               !(D->getIdentifierNamespace() & Decl::IDNS_Ordinary))
        F.erase();
    }
    F.done();
  } else {
    assert(IsInstantiation && "no scope in non-instantiation");
    if (CurContext->isRecord())
      LookupQualifiedName(Previous, CurContext);
    else {
      // No redeclaration check is needed here; in non-member contexts we
      // diagnosed all possible conflicts with other using-declarations when
      // building the template:
      //
      // For a dependent non-type using declaration, the only valid case is
      // if we instantiate to a single enumerator. We check for conflicts
      // between shadow declarations we introduce, and we check in the template
      // definition for conflicts between a non-type using declaration and any
      // other declaration, which together covers all cases.
      //
      // A dependent typename using declaration will never successfully
      // instantiate, since it will always name a class member, so we reject
      // that in the template definition.
    }
  }

  // Check for invalid redeclarations.
  if (CheckUsingDeclRedeclaration(UsingLoc, HasTypenameKeyword,
                                  SS, IdentLoc, Previous))
    return nullptr;

  // Check for bad qualifiers.
  if (CheckUsingDeclQualifier(UsingLoc, HasTypenameKeyword, SS, NameInfo,
                              IdentLoc))
    return nullptr;

  DeclContext *LookupContext = computeDeclContext(SS);
  NamedDecl *D;
  NestedNameSpecifierLoc QualifierLoc = SS.getWithLocInContext(Context);
  if (!LookupContext || EllipsisLoc.isValid()) {
    if (HasTypenameKeyword) {
      // FIXME: not all declaration name kinds are legal here
      D = UnresolvedUsingTypenameDecl::Create(Context, CurContext,
                                              UsingLoc, TypenameLoc,
                                              QualifierLoc,
                                              IdentLoc, NameInfo.getName(),
                                              EllipsisLoc);
    } else {
      D = UnresolvedUsingValueDecl::Create(Context, CurContext, UsingLoc,
                                           QualifierLoc, NameInfo, EllipsisLoc);
    }
    D->setAccess(AS);
    CurContext->addDecl(D);
    return D;
  }

  auto Build = [&](bool Invalid) {
    UsingDecl *UD =
        UsingDecl::Create(Context, CurContext, UsingLoc, QualifierLoc,
                          UsingName, HasTypenameKeyword);
    UD->setAccess(AS);
    CurContext->addDecl(UD);
    UD->setInvalidDecl(Invalid);
    return UD;
  };
  auto BuildInvalid = [&]{ return Build(true); };
  auto BuildValid = [&]{ return Build(false); };

  if (RequireCompleteDeclContext(SS, LookupContext))
    return BuildInvalid();

  // Look up the target name.
  LookupResult R(*this, NameInfo, LookupOrdinaryName);

  // Unlike most lookups, we don't always want to hide tag
  // declarations: tag names are visible through the using declaration
  // even if hidden by ordinary names, *except* in a dependent context
  // where it's important for the sanity of two-phase lookup.
  if (!IsInstantiation)
    R.setHideTags(false);

  // For the purposes of this lookup, we have a base object type
  // equal to that of the current context.
  if (CurContext->isRecord()) {
    R.setBaseObjectType(
                   Context.getTypeDeclType(cast<CXXRecordDecl>(CurContext)));
  }

  LookupQualifiedName(R, LookupContext);

  // Try to correct typos if possible. If constructor name lookup finds no
  // results, that means the named class has no explicit constructors, and we
  // suppressed declaring implicit ones (probably because it's dependent or
  // invalid).
  if (R.empty() &&
      NameInfo.getName().getNameKind() != DeclarationName::CXXConstructorName) {
    // HACK: Work around a bug in libstdc++'s detection of ::gets. Sometimes
    // it will believe that glibc provides a ::gets in cases where it does not,
    // and will try to pull it into namespace std with a using-declaration.
    // Just ignore the using-declaration in that case.
    auto *II = NameInfo.getName().getAsIdentifierInfo();
    if (getLangOpts().CPlusPlus14 && II && II->isStr("gets") &&
        CurContext->isStdNamespace() &&
        isa<TranslationUnitDecl>(LookupContext) &&
        getSourceManager().isInSystemHeader(UsingLoc))
      return nullptr;
    if (TypoCorrection Corrected = CorrectTypo(
            R.getLookupNameInfo(), R.getLookupKind(), S, &SS,
            llvm::make_unique<UsingValidatorCCC>(
                HasTypenameKeyword, IsInstantiation, SS.getScopeRep(),
                dyn_cast<CXXRecordDecl>(CurContext)),
            CTK_ErrorRecovery)) {
      // We reject candidates where DroppedSpecifier == true, hence the
      // literal '0' below.
      diagnoseTypo(Corrected, PDiag(diag::err_no_member_suggest)
                                << NameInfo.getName() << LookupContext << 0
                                << SS.getRange());

      // If we picked a correction with no attached Decl we can't do anything
      // useful with it, bail out.
      NamedDecl *ND = Corrected.getCorrectionDecl();
      if (!ND)
        return BuildInvalid();

      // If we corrected to an inheriting constructor, handle it as one.
      auto *RD = dyn_cast<CXXRecordDecl>(ND);
      if (RD && RD->isInjectedClassName()) {
        // The parent of the injected class name is the class itself.
        RD = cast<CXXRecordDecl>(RD->getParent());

        // Fix up the information we'll use to build the using declaration.
        if (Corrected.WillReplaceSpecifier()) {
          NestedNameSpecifierLocBuilder Builder;
          Builder.MakeTrivial(Context, Corrected.getCorrectionSpecifier(),
                              QualifierLoc.getSourceRange());
          QualifierLoc = Builder.getWithLocInContext(Context);
        }

        // In this case, the name we introduce is the name of a derived class
        // constructor.
        auto *CurClass = cast<CXXRecordDecl>(CurContext);
        UsingName.setName(Context.DeclarationNames.getCXXConstructorName(
            Context.getCanonicalType(Context.getRecordType(CurClass))));
        UsingName.setNamedTypeInfo(nullptr);
        for (auto *Ctor : LookupConstructors(RD))
          R.addDecl(Ctor);
        R.resolveKind();
      } else {
        // FIXME: Pick up all the declarations if we found an overloaded
        // function.
        UsingName.setName(ND->getDeclName());
        R.addDecl(ND);
      }
    } else {
      Diag(IdentLoc, diag::err_no_member)
        << NameInfo.getName() << LookupContext << SS.getRange();
      return BuildInvalid();
    }
  }

  if (R.isAmbiguous())
    return BuildInvalid();

  if (HasTypenameKeyword) {
    // If we asked for a typename and got a non-type decl, error out.
    if (!R.getAsSingle<TypeDecl>()) {
      Diag(IdentLoc, diag::err_using_typename_non_type);
      for (LookupResult::iterator I = R.begin(), E = R.end(); I != E; ++I)
        Diag((*I)->getUnderlyingDecl()->getLocation(),
             diag::note_using_decl_target);
      return BuildInvalid();
    }
  } else {
    // If we asked for a non-typename and we got a type, error out,
    // but only if this is an instantiation of an unresolved using
    // decl.  Otherwise just silently find the type name.
    if (IsInstantiation && R.getAsSingle<TypeDecl>()) {
      Diag(IdentLoc, diag::err_using_dependent_value_is_type);
      Diag(R.getFoundDecl()->getLocation(), diag::note_using_decl_target);
      return BuildInvalid();
    }
  }

  // C++14 [namespace.udecl]p6:
  // A using-declaration shall not name a namespace.
  if (R.getAsSingle<NamespaceDecl>()) {
    Diag(IdentLoc, diag::err_using_decl_can_not_refer_to_namespace)
      << SS.getRange();
    return BuildInvalid();
  }

  // C++14 [namespace.udecl]p7:
  // A using-declaration shall not name a scoped enumerator.
  if (auto *ED = R.getAsSingle<EnumConstantDecl>()) {
    if (cast<EnumDecl>(ED->getDeclContext())->isScoped()) {
      Diag(IdentLoc, diag::err_using_decl_can_not_refer_to_scoped_enum)
        << SS.getRange();
      return BuildInvalid();
    }
  }

  UsingDecl *UD = BuildValid();

  // Some additional rules apply to inheriting constructors.
  if (UsingName.getName().getNameKind() ==
        DeclarationName::CXXConstructorName) {
    // Suppress access diagnostics; the access check is instead performed at the
    // point of use for an inheriting constructor.
    R.suppressDiagnostics();
    if (CheckInheritingConstructorUsingDecl(UD))
      return UD;
  }

  for (LookupResult::iterator I = R.begin(), E = R.end(); I != E; ++I) {
    UsingShadowDecl *PrevDecl = nullptr;
    if (!CheckUsingShadowDecl(UD, *I, Previous, PrevDecl))
      BuildUsingShadowDecl(S, UD, *I, PrevDecl);
  }

  return UD;
}

NamedDecl *Sema::BuildUsingPackDecl(NamedDecl *InstantiatedFrom,
                                    ArrayRef<NamedDecl *> Expansions) {
  assert(isa<UnresolvedUsingValueDecl>(InstantiatedFrom) ||
         isa<UnresolvedUsingTypenameDecl>(InstantiatedFrom) ||
         isa<UsingPackDecl>(InstantiatedFrom));

  auto *UPD =
      UsingPackDecl::Create(Context, CurContext, InstantiatedFrom, Expansions);
  UPD->setAccess(InstantiatedFrom->getAccess());
  CurContext->addDecl(UPD);
  return UPD;
}

/// Additional checks for a using declaration referring to a constructor name.
bool Sema::CheckInheritingConstructorUsingDecl(UsingDecl *UD) {
  assert(!UD->hasTypename() && "expecting a constructor name");

  const Type *SourceType = UD->getQualifier()->getAsType();
  assert(SourceType &&
         "Using decl naming constructor doesn't have type in scope spec.");
  CXXRecordDecl *TargetClass = cast<CXXRecordDecl>(CurContext);

  // Check whether the named type is a direct base class.
  bool AnyDependentBases = false;
  auto *Base = findDirectBaseWithType(TargetClass, QualType(SourceType, 0),
                                      AnyDependentBases);
  if (!Base && !AnyDependentBases) {
    Diag(UD->getUsingLoc(),
         diag::err_using_decl_constructor_not_in_direct_base)
      << UD->getNameInfo().getSourceRange()
      << QualType(SourceType, 0) << TargetClass;
    UD->setInvalidDecl();
    return true;
  }

  if (Base)
    Base->setInheritConstructors();

  return false;
}

/// Checks that the given using declaration is not an invalid
/// redeclaration.  Note that this is checking only for the using decl
/// itself, not for any ill-formedness among the UsingShadowDecls.
bool Sema::CheckUsingDeclRedeclaration(SourceLocation UsingLoc,
                                       bool HasTypenameKeyword,
                                       const CXXScopeSpec &SS,
                                       SourceLocation NameLoc,
                                       const LookupResult &Prev) {
  NestedNameSpecifier *Qual = SS.getScopeRep();

  // C++03 [namespace.udecl]p8:
  // C++0x [namespace.udecl]p10:
  //   A using-declaration is a declaration and can therefore be used
  //   repeatedly where (and only where) multiple declarations are
  //   allowed.
  //
  // That's in non-member contexts.
  if (!CurContext->getRedeclContext()->isRecord()) {
    // A dependent qualifier outside a class can only ever resolve to an
    // enumeration type. Therefore it conflicts with any other non-type
    // declaration in the same scope.
    // FIXME: How should we check for dependent type-type conflicts at block
    // scope?
    if (Qual->isDependent() && !HasTypenameKeyword) {
      for (auto *D : Prev) {
        if (!isa<TypeDecl>(D) && !isa<UsingDecl>(D) && !isa<UsingPackDecl>(D)) {
          bool OldCouldBeEnumerator =
              isa<UnresolvedUsingValueDecl>(D) || isa<EnumConstantDecl>(D);
          Diag(NameLoc,
               OldCouldBeEnumerator ? diag::err_redefinition
                                    : diag::err_redefinition_different_kind)
              << Prev.getLookupName();
          Diag(D->getLocation(), diag::note_previous_definition);
          return true;
        }
      }
    }
    return false;
  }

  for (LookupResult::iterator I = Prev.begin(), E = Prev.end(); I != E; ++I) {
    NamedDecl *D = *I;

    bool DTypename;
    NestedNameSpecifier *DQual;
    if (UsingDecl *UD = dyn_cast<UsingDecl>(D)) {
      DTypename = UD->hasTypename();
      DQual = UD->getQualifier();
    } else if (UnresolvedUsingValueDecl *UD
                 = dyn_cast<UnresolvedUsingValueDecl>(D)) {
      DTypename = false;
      DQual = UD->getQualifier();
    } else if (UnresolvedUsingTypenameDecl *UD
                 = dyn_cast<UnresolvedUsingTypenameDecl>(D)) {
      DTypename = true;
      DQual = UD->getQualifier();
    } else continue;

    // using decls differ if one says 'typename' and the other doesn't.
    // FIXME: non-dependent using decls?
    if (HasTypenameKeyword != DTypename) continue;

    // using decls differ if they name different scopes (but note that
    // template instantiation can cause this check to trigger when it
    // didn't before instantiation).
    if (Context.getCanonicalNestedNameSpecifier(Qual) !=
        Context.getCanonicalNestedNameSpecifier(DQual))
      continue;

    Diag(NameLoc, diag::err_using_decl_redeclaration) << SS.getRange();
    Diag(D->getLocation(), diag::note_using_decl) << 1;
    return true;
  }

  return false;
}


/// Checks that the given nested-name qualifier used in a using decl
/// in the current context is appropriately related to the current
/// scope.  If an error is found, diagnoses it and returns true.
bool Sema::CheckUsingDeclQualifier(SourceLocation UsingLoc,
                                   bool HasTypename,
                                   const CXXScopeSpec &SS,
                                   const DeclarationNameInfo &NameInfo,
                                   SourceLocation NameLoc) {
  DeclContext *NamedContext = computeDeclContext(SS);

  if (!CurContext->isRecord()) {
    // C++03 [namespace.udecl]p3:
    // C++0x [namespace.udecl]p8:
    //   A using-declaration for a class member shall be a member-declaration.

    // If we weren't able to compute a valid scope, it might validly be a
    // dependent class scope or a dependent enumeration unscoped scope. If
    // we have a 'typename' keyword, the scope must resolve to a class type.
    if ((HasTypename && !NamedContext) ||
        (NamedContext && NamedContext->getRedeclContext()->isRecord())) {
      auto *RD = NamedContext
                     ? cast<CXXRecordDecl>(NamedContext->getRedeclContext())
                     : nullptr;
      if (RD && RequireCompleteDeclContext(const_cast<CXXScopeSpec&>(SS), RD))
        RD = nullptr;

      Diag(NameLoc, diag::err_using_decl_can_not_refer_to_class_member)
        << SS.getRange();

      // If we have a complete, non-dependent source type, try to suggest a
      // way to get the same effect.
      if (!RD)
        return true;

      // Find what this using-declaration was referring to.
      LookupResult R(*this, NameInfo, LookupOrdinaryName);
      R.setHideTags(false);
      R.suppressDiagnostics();
      LookupQualifiedName(R, RD);

      if (R.getAsSingle<TypeDecl>()) {
        if (getLangOpts().CPlusPlus11) {
          // Convert 'using X::Y;' to 'using Y = X::Y;'.
          Diag(SS.getBeginLoc(), diag::note_using_decl_class_member_workaround)
            << 0 // alias declaration
            << FixItHint::CreateInsertion(SS.getBeginLoc(),
                                          NameInfo.getName().getAsString() +
                                              " = ");
        } else {
          // Convert 'using X::Y;' to 'typedef X::Y Y;'.
          SourceLocation InsertLoc = getLocForEndOfToken(NameInfo.getEndLoc());
          Diag(InsertLoc, diag::note_using_decl_class_member_workaround)
            << 1 // typedef declaration
            << FixItHint::CreateReplacement(UsingLoc, "typedef")
            << FixItHint::CreateInsertion(
                   InsertLoc, " " + NameInfo.getName().getAsString());
        }
      } else if (R.getAsSingle<VarDecl>()) {
        // Don't provide a fixit outside C++11 mode; we don't want to suggest
        // repeating the type of the static data member here.
        FixItHint FixIt;
        if (getLangOpts().CPlusPlus11) {
          // Convert 'using X::Y;' to 'auto &Y = X::Y;'.
          FixIt = FixItHint::CreateReplacement(
              UsingLoc, "auto &" + NameInfo.getName().getAsString() + " = ");
        }

        Diag(UsingLoc, diag::note_using_decl_class_member_workaround)
          << 2 // reference declaration
          << FixIt;
      } else if (R.getAsSingle<EnumConstantDecl>()) {
        // Don't provide a fixit outside C++11 mode; we don't want to suggest
        // repeating the type of the enumeration here, and we can't do so if
        // the type is anonymous.
        FixItHint FixIt;
        if (getLangOpts().CPlusPlus11) {
          // Convert 'using X::Y;' to 'auto &Y = X::Y;'.
          FixIt = FixItHint::CreateReplacement(
              UsingLoc,
              "constexpr auto " + NameInfo.getName().getAsString() + " = ");
        }

        Diag(UsingLoc, diag::note_using_decl_class_member_workaround)
          << (getLangOpts().CPlusPlus11 ? 4 : 3) // const[expr] variable
          << FixIt;
      }
      return true;
    }

    // Otherwise, this might be valid.
    return false;
  }

  // The current scope is a record.

  // If the named context is dependent, we can't decide much.
  if (!NamedContext) {
    // FIXME: in C++0x, we can diagnose if we can prove that the
    // nested-name-specifier does not refer to a base class, which is
    // still possible in some cases.

    // Otherwise we have to conservatively report that things might be
    // okay.
    return false;
  }

  if (!NamedContext->isRecord()) {
    // Ideally this would point at the last name in the specifier,
    // but we don't have that level of source info.
    Diag(SS.getRange().getBegin(),
         diag::err_using_decl_nested_name_specifier_is_not_class)
      << SS.getScopeRep() << SS.getRange();
    return true;
  }

  if (!NamedContext->isDependentContext() &&
      RequireCompleteDeclContext(const_cast<CXXScopeSpec&>(SS), NamedContext))
    return true;

  if (getLangOpts().CPlusPlus11) {
    // C++11 [namespace.udecl]p3:
    //   In a using-declaration used as a member-declaration, the
    //   nested-name-specifier shall name a base class of the class
    //   being defined.

    if (cast<CXXRecordDecl>(CurContext)->isProvablyNotDerivedFrom(
                                 cast<CXXRecordDecl>(NamedContext))) {
      if (CurContext == NamedContext) {
        Diag(NameLoc,
             diag::err_using_decl_nested_name_specifier_is_current_class)
          << SS.getRange();
        return true;
      }

      if (!cast<CXXRecordDecl>(NamedContext)->isInvalidDecl()) {
        Diag(SS.getRange().getBegin(),
             diag::err_using_decl_nested_name_specifier_is_not_base_class)
          << SS.getScopeRep()
          << cast<CXXRecordDecl>(CurContext)
          << SS.getRange();
      }
      return true;
    }

    return false;
  }

  // C++03 [namespace.udecl]p4:
  //   A using-declaration used as a member-declaration shall refer
  //   to a member of a base class of the class being defined [etc.].

  // Salient point: SS doesn't have to name a base class as long as
  // lookup only finds members from base classes.  Therefore we can
  // diagnose here only if we can prove that that can't happen,
  // i.e. if the class hierarchies provably don't intersect.

  // TODO: it would be nice if "definitely valid" results were cached
  // in the UsingDecl and UsingShadowDecl so that these checks didn't
  // need to be repeated.

  llvm::SmallPtrSet<const CXXRecordDecl *, 4> Bases;
  auto Collect = [&Bases](const CXXRecordDecl *Base) {
    Bases.insert(Base);
    return true;
  };

  // Collect all bases. Return false if we find a dependent base.
  if (!cast<CXXRecordDecl>(CurContext)->forallBases(Collect))
    return false;

  // Returns true if the base is dependent or is one of the accumulated base
  // classes.
  auto IsNotBase = [&Bases](const CXXRecordDecl *Base) {
    return !Bases.count(Base);
  };

  // Return false if the class has a dependent base or if it or one
  // of its bases is present in the base set of the current context.
  if (Bases.count(cast<CXXRecordDecl>(NamedContext)) ||
      !cast<CXXRecordDecl>(NamedContext)->forallBases(IsNotBase))
    return false;

  Diag(SS.getRange().getBegin(),
       diag::err_using_decl_nested_name_specifier_is_not_base_class)
    << SS.getScopeRep()
    << cast<CXXRecordDecl>(CurContext)
    << SS.getRange();

  return true;
}

Decl *Sema::ActOnAliasDeclaration(Scope *S, AccessSpecifier AS,
                                  MultiTemplateParamsArg TemplateParamLists,
                                  SourceLocation UsingLoc, UnqualifiedId &Name,
                                  const ParsedAttributesView &AttrList,
                                  TypeResult Type, Decl *DeclFromDeclSpec) {
  // Skip up to the relevant declaration scope.
  while (S->isTemplateParamScope())
    S = S->getParent();
  assert((S->getFlags() & Scope::DeclScope) &&
         "got alias-declaration outside of declaration scope");

  if (Type.isInvalid())
    return nullptr;

  bool Invalid = false;
  DeclarationNameInfo NameInfo = GetNameFromUnqualifiedId(Name);
  TypeSourceInfo *TInfo = nullptr;
  GetTypeFromParser(Type.get(), &TInfo);

  if (DiagnoseClassNameShadow(CurContext, NameInfo))
    return nullptr;

  if (DiagnoseUnexpandedParameterPack(Name.StartLocation, TInfo,
                                      UPPC_DeclarationType)) {
    Invalid = true;
    TInfo = Context.getTrivialTypeSourceInfo(Context.IntTy,
                                             TInfo->getTypeLoc().getBeginLoc());
  }

  LookupResult Previous(*this, NameInfo, LookupOrdinaryName,
                        TemplateParamLists.size()
                            ? forRedeclarationInCurContext()
                            : ForVisibleRedeclaration);
  LookupName(Previous, S);

  // Warn about shadowing the name of a template parameter.
  if (Previous.isSingleResult() &&
      Previous.getFoundDecl()->isTemplateParameter()) {
    DiagnoseTemplateParameterShadow(Name.StartLocation,Previous.getFoundDecl());
    Previous.clear();
  }

  assert(Name.Kind == UnqualifiedIdKind::IK_Identifier &&
         "name in alias declaration must be an identifier");
  TypeAliasDecl *NewTD = TypeAliasDecl::Create(Context, CurContext, UsingLoc,
                                               Name.StartLocation,
                                               Name.Identifier, TInfo);

  NewTD->setAccess(AS);

  if (Invalid)
    NewTD->setInvalidDecl();

  ProcessDeclAttributeList(S, NewTD, AttrList);
  AddPragmaAttributes(S, NewTD);

  CheckTypedefForVariablyModifiedType(S, NewTD);
  Invalid |= NewTD->isInvalidDecl();

  bool Redeclaration = false;

  NamedDecl *NewND;
  if (TemplateParamLists.size()) {
    TypeAliasTemplateDecl *OldDecl = nullptr;
    TemplateParameterList *OldTemplateParams = nullptr;

    if (TemplateParamLists.size() != 1) {
      Diag(UsingLoc, diag::err_alias_template_extra_headers)
        << SourceRange(TemplateParamLists[1]->getTemplateLoc(),
         TemplateParamLists[TemplateParamLists.size()-1]->getRAngleLoc());
    }
    TemplateParameterList *TemplateParams = TemplateParamLists[0];

    // Check that we can declare a template here.
    if (CheckTemplateDeclScope(S, TemplateParams))
      return nullptr;

    // Only consider previous declarations in the same scope.
    FilterLookupForScope(Previous, CurContext, S, /*ConsiderLinkage*/false,
                         /*ExplicitInstantiationOrSpecialization*/false);
    if (!Previous.empty()) {
      Redeclaration = true;

      OldDecl = Previous.getAsSingle<TypeAliasTemplateDecl>();
      if (!OldDecl && !Invalid) {
        Diag(UsingLoc, diag::err_redefinition_different_kind)
          << Name.Identifier;

        NamedDecl *OldD = Previous.getRepresentativeDecl();
        if (OldD->getLocation().isValid())
          Diag(OldD->getLocation(), diag::note_previous_definition);

        Invalid = true;
      }

      if (!Invalid && OldDecl && !OldDecl->isInvalidDecl()) {
        if (TemplateParameterListsAreEqual(TemplateParams,
                                           OldDecl->getTemplateParameters(),
                                           /*Complain=*/true,
                                           TPL_TemplateMatch))
          OldTemplateParams =
              OldDecl->getMostRecentDecl()->getTemplateParameters();
        else
          Invalid = true;

        TypeAliasDecl *OldTD = OldDecl->getTemplatedDecl();
        if (!Invalid &&
            !Context.hasSameType(OldTD->getUnderlyingType(),
                                 NewTD->getUnderlyingType())) {
          // FIXME: The C++0x standard does not clearly say this is ill-formed,
          // but we can't reasonably accept it.
          Diag(NewTD->getLocation(), diag::err_redefinition_different_typedef)
            << 2 << NewTD->getUnderlyingType() << OldTD->getUnderlyingType();
          if (OldTD->getLocation().isValid())
            Diag(OldTD->getLocation(), diag::note_previous_definition);
          Invalid = true;
        }
      }
    }

    // Merge any previous default template arguments into our parameters,
    // and check the parameter list.
    if (CheckTemplateParameterList(TemplateParams, OldTemplateParams,
                                   TPC_TypeAliasTemplate))
      return nullptr;

    TypeAliasTemplateDecl *NewDecl =
      TypeAliasTemplateDecl::Create(Context, CurContext, UsingLoc,
                                    Name.Identifier, TemplateParams,
                                    NewTD);
    NewTD->setDescribedAliasTemplate(NewDecl);

    NewDecl->setAccess(AS);

    if (Invalid)
      NewDecl->setInvalidDecl();
    else if (OldDecl) {
      NewDecl->setPreviousDecl(OldDecl);
      CheckRedeclarationModuleOwnership(NewDecl, OldDecl);
    }

    NewND = NewDecl;
  } else {
    if (auto *TD = dyn_cast_or_null<TagDecl>(DeclFromDeclSpec)) {
      setTagNameForLinkagePurposes(TD, NewTD);
      handleTagNumbering(TD, S);
    }
    ActOnTypedefNameDecl(S, CurContext, NewTD, Previous, Redeclaration);
    NewND = NewTD;
  }

  PushOnScopeChains(NewND, S);
  ActOnDocumentableDecl(NewND);
  return NewND;
}

Decl *Sema::ActOnNamespaceAliasDef(Scope *S, SourceLocation NamespaceLoc,
                                   SourceLocation AliasLoc,
                                   IdentifierInfo *Alias, CXXScopeSpec &SS,
                                   SourceLocation IdentLoc,
                                   IdentifierInfo *Ident) {

  // Lookup the namespace name.
  LookupResult R(*this, Ident, IdentLoc, LookupNamespaceName);
  LookupParsedName(R, S, &SS);

  if (R.isAmbiguous())
    return nullptr;

  if (R.empty()) {
    if (!TryNamespaceTypoCorrection(*this, R, S, SS, IdentLoc, Ident)) {
      Diag(IdentLoc, diag::err_expected_namespace_name) << SS.getRange();
      return nullptr;
    }
  }
  assert(!R.isAmbiguous() && !R.empty());
  NamedDecl *ND = R.getRepresentativeDecl();

  // Check if we have a previous declaration with the same name.
  LookupResult PrevR(*this, Alias, AliasLoc, LookupOrdinaryName,
                     ForVisibleRedeclaration);
  LookupName(PrevR, S);

  // Check we're not shadowing a template parameter.
  if (PrevR.isSingleResult() && PrevR.getFoundDecl()->isTemplateParameter()) {
    DiagnoseTemplateParameterShadow(AliasLoc, PrevR.getFoundDecl());
    PrevR.clear();
  }

  // Filter out any other lookup result from an enclosing scope.
  FilterLookupForScope(PrevR, CurContext, S, /*ConsiderLinkage*/false,
                       /*AllowInlineNamespace*/false);

  // Find the previous declaration and check that we can redeclare it.
  NamespaceAliasDecl *Prev = nullptr;
  if (PrevR.isSingleResult()) {
    NamedDecl *PrevDecl = PrevR.getRepresentativeDecl();
    if (NamespaceAliasDecl *AD = dyn_cast<NamespaceAliasDecl>(PrevDecl)) {
      // We already have an alias with the same name that points to the same
      // namespace; check that it matches.
      if (AD->getNamespace()->Equals(getNamespaceDecl(ND))) {
        Prev = AD;
      } else if (isVisible(PrevDecl)) {
        Diag(AliasLoc, diag::err_redefinition_different_namespace_alias)
          << Alias;
        Diag(AD->getLocation(), diag::note_previous_namespace_alias)
          << AD->getNamespace();
        return nullptr;
      }
    } else if (isVisible(PrevDecl)) {
      unsigned DiagID = isa<NamespaceDecl>(PrevDecl->getUnderlyingDecl())
                            ? diag::err_redefinition
                            : diag::err_redefinition_different_kind;
      Diag(AliasLoc, DiagID) << Alias;
      Diag(PrevDecl->getLocation(), diag::note_previous_definition);
      return nullptr;
    }
  }

  // The use of a nested name specifier may trigger deprecation warnings.
  DiagnoseUseOfDecl(ND, IdentLoc);

  NamespaceAliasDecl *AliasDecl =
    NamespaceAliasDecl::Create(Context, CurContext, NamespaceLoc, AliasLoc,
                               Alias, SS.getWithLocInContext(Context),
                               IdentLoc, ND);
  if (Prev)
    AliasDecl->setPreviousDecl(Prev);

  PushOnScopeChains(AliasDecl, S);
  return AliasDecl;
}

namespace {
struct SpecialMemberExceptionSpecInfo
    : SpecialMemberVisitor<SpecialMemberExceptionSpecInfo> {
  SourceLocation Loc;
  Sema::ImplicitExceptionSpecification ExceptSpec;

  SpecialMemberExceptionSpecInfo(Sema &S, CXXMethodDecl *MD,
                                 Sema::CXXSpecialMember CSM,
                                 Sema::InheritedConstructorInfo *ICI,
                                 SourceLocation Loc)
      : SpecialMemberVisitor(S, MD, CSM, ICI), Loc(Loc), ExceptSpec(S) {}

  bool visitBase(CXXBaseSpecifier *Base);
  bool visitField(FieldDecl *FD);

  void visitClassSubobject(CXXRecordDecl *Class, Subobject Subobj,
                           unsigned Quals);

  void visitSubobjectCall(Subobject Subobj,
                          Sema::SpecialMemberOverloadResult SMOR);
};
}

bool SpecialMemberExceptionSpecInfo::visitBase(CXXBaseSpecifier *Base) {
  auto *RT = Base->getType()->getAs<RecordType>();
  if (!RT)
    return false;

  auto *BaseClass = cast<CXXRecordDecl>(RT->getDecl());
  Sema::SpecialMemberOverloadResult SMOR = lookupInheritedCtor(BaseClass);
  if (auto *BaseCtor = SMOR.getMethod()) {
    visitSubobjectCall(Base, BaseCtor);
    return false;
  }

  visitClassSubobject(BaseClass, Base, 0);
  return false;
}

bool SpecialMemberExceptionSpecInfo::visitField(FieldDecl *FD) {
  if (CSM == Sema::CXXDefaultConstructor && FD->hasInClassInitializer()) {
    Expr *E = FD->getInClassInitializer();
    if (!E)
      // FIXME: It's a little wasteful to build and throw away a
      // CXXDefaultInitExpr here.
      // FIXME: We should have a single context note pointing at Loc, and
      // this location should be MD->getLocation() instead, since that's
      // the location where we actually use the default init expression.
      E = S.BuildCXXDefaultInitExpr(Loc, FD).get();
    if (E)
      ExceptSpec.CalledExpr(E);
  } else if (auto *RT = S.Context.getBaseElementType(FD->getType())
                            ->getAs<RecordType>()) {
    visitClassSubobject(cast<CXXRecordDecl>(RT->getDecl()), FD,
                        FD->getType().getCVRQualifiers());
  }
  return false;
}

void SpecialMemberExceptionSpecInfo::visitClassSubobject(CXXRecordDecl *Class,
                                                         Subobject Subobj,
                                                         unsigned Quals) {
  FieldDecl *Field = Subobj.dyn_cast<FieldDecl*>();
  bool IsMutable = Field && Field->isMutable();
  visitSubobjectCall(Subobj, lookupIn(Class, Quals, IsMutable));
}

void SpecialMemberExceptionSpecInfo::visitSubobjectCall(
    Subobject Subobj, Sema::SpecialMemberOverloadResult SMOR) {
  // Note, if lookup fails, it doesn't matter what exception specification we
  // choose because the special member will be deleted.
  if (CXXMethodDecl *MD = SMOR.getMethod())
    ExceptSpec.CalledDecl(getSubobjectLoc(Subobj), MD);
}

namespace {
/// RAII object to register a special member as being currently declared.
struct ComputingExceptionSpec {
  Sema &S;

  ComputingExceptionSpec(Sema &S, CXXMethodDecl *MD, SourceLocation Loc)
      : S(S) {
    Sema::CodeSynthesisContext Ctx;
    Ctx.Kind = Sema::CodeSynthesisContext::ExceptionSpecEvaluation;
    Ctx.PointOfInstantiation = Loc;
    Ctx.Entity = MD;
    S.pushCodeSynthesisContext(Ctx);
  }
  ~ComputingExceptionSpec() {
    S.popCodeSynthesisContext();
  }
};
}

static Sema::ImplicitExceptionSpecification
ComputeDefaultedSpecialMemberExceptionSpec(
    Sema &S, SourceLocation Loc, CXXMethodDecl *MD, Sema::CXXSpecialMember CSM,
    Sema::InheritedConstructorInfo *ICI) {
  ComputingExceptionSpec CES(S, MD, Loc);

  CXXRecordDecl *ClassDecl = MD->getParent();

  // C++ [except.spec]p14:
  //   An implicitly declared special member function (Clause 12) shall have an
  //   exception-specification. [...]
  SpecialMemberExceptionSpecInfo Info(S, MD, CSM, ICI, MD->getLocation());
  if (ClassDecl->isInvalidDecl())
    return Info.ExceptSpec;

  // FIXME: If this diagnostic fires, we're probably missing a check for
  // attempting to resolve an exception specification before it's known
  // at a higher level.
  if (S.RequireCompleteType(MD->getLocation(),
                            S.Context.getRecordType(ClassDecl),
                            diag::err_exception_spec_incomplete_type))
    return Info.ExceptSpec;

  // C++1z [except.spec]p7:
  //   [Look for exceptions thrown by] a constructor selected [...] to
  //   initialize a potentially constructed subobject,
  // C++1z [except.spec]p8:
  //   The exception specification for an implicitly-declared destructor, or a
  //   destructor without a noexcept-specifier, is potentially-throwing if and
  //   only if any of the destructors for any of its potentially constructed
  //   subojects is potentially throwing.
  // FIXME: We respect the first rule but ignore the "potentially constructed"
  // in the second rule to resolve a core issue (no number yet) that would have
  // us reject:
  //   struct A { virtual void f() = 0; virtual ~A() noexcept(false) = 0; };
  //   struct B : A {};
  //   struct C : B { void f(); };
  // ... due to giving B::~B() a non-throwing exception specification.
  Info.visit(Info.IsConstructor ? Info.VisitPotentiallyConstructedBases
                                : Info.VisitAllBases);

  return Info.ExceptSpec;
}

namespace {
/// RAII object to register a special member as being currently declared.
struct DeclaringSpecialMember {
  Sema &S;
  Sema::SpecialMemberDecl D;
  Sema::ContextRAII SavedContext;
  bool WasAlreadyBeingDeclared;

  DeclaringSpecialMember(Sema &S, CXXRecordDecl *RD, Sema::CXXSpecialMember CSM)
      : S(S), D(RD, CSM), SavedContext(S, RD) {
    WasAlreadyBeingDeclared = !S.SpecialMembersBeingDeclared.insert(D).second;
    if (WasAlreadyBeingDeclared)
      // This almost never happens, but if it does, ensure that our cache
      // doesn't contain a stale result.
      S.SpecialMemberCache.clear();
    else {
      // Register a note to be produced if we encounter an error while
      // declaring the special member.
      Sema::CodeSynthesisContext Ctx;
      Ctx.Kind = Sema::CodeSynthesisContext::DeclaringSpecialMember;
      // FIXME: We don't have a location to use here. Using the class's
      // location maintains the fiction that we declare all special members
      // with the class, but (1) it's not clear that lying about that helps our
      // users understand what's going on, and (2) there may be outer contexts
      // on the stack (some of which are relevant) and printing them exposes
      // our lies.
      Ctx.PointOfInstantiation = RD->getLocation();
      Ctx.Entity = RD;
      Ctx.SpecialMember = CSM;
      S.pushCodeSynthesisContext(Ctx);
    }
  }
  ~DeclaringSpecialMember() {
    if (!WasAlreadyBeingDeclared) {
      S.SpecialMembersBeingDeclared.erase(D);
      S.popCodeSynthesisContext();
    }
  }

  /// Are we already trying to declare this special member?
  bool isAlreadyBeingDeclared() const {
    return WasAlreadyBeingDeclared;
  }
};
}

void Sema::CheckImplicitSpecialMemberDeclaration(Scope *S, FunctionDecl *FD) {
  // Look up any existing declarations, but don't trigger declaration of all
  // implicit special members with this name.
  DeclarationName Name = FD->getDeclName();
  LookupResult R(*this, Name, SourceLocation(), LookupOrdinaryName,
                 ForExternalRedeclaration);
  for (auto *D : FD->getParent()->lookup(Name))
    if (auto *Acceptable = R.getAcceptableDecl(D))
      R.addDecl(Acceptable);
  R.resolveKind();
  R.suppressDiagnostics();

  CheckFunctionDeclaration(S, FD, R, /*IsMemberSpecialization*/false);
}

void Sema::setupImplicitSpecialMemberType(CXXMethodDecl *SpecialMem,
                                          QualType ResultTy,
                                          ArrayRef<QualType> Args) {
  // Build an exception specification pointing back at this constructor.
  FunctionProtoType::ExtProtoInfo EPI = getImplicitMethodEPI(*this, SpecialMem);

  if (getLangOpts().OpenCLCPlusPlus) {
    // OpenCL: Implicitly defaulted special member are of the generic address
    // space.
    EPI.TypeQuals.addAddressSpace(LangAS::opencl_generic);
  }

  auto QT = Context.getFunctionType(ResultTy, Args, EPI);
  SpecialMem->setType(QT);
}

CXXConstructorDecl *Sema::DeclareImplicitDefaultConstructor(
                                                     CXXRecordDecl *ClassDecl) {
  // C++ [class.ctor]p5:
  //   A default constructor for a class X is a constructor of class X
  //   that can be called without an argument. If there is no
  //   user-declared constructor for class X, a default constructor is
  //   implicitly declared. An implicitly-declared default constructor
  //   is an inline public member of its class.
  assert(ClassDecl->needsImplicitDefaultConstructor() &&
         "Should not build implicit default constructor!");

  DeclaringSpecialMember DSM(*this, ClassDecl, CXXDefaultConstructor);
  if (DSM.isAlreadyBeingDeclared())
    return nullptr;

  bool Constexpr = defaultedSpecialMemberIsConstexpr(*this, ClassDecl,
                                                     CXXDefaultConstructor,
                                                     false);

  // Create the actual constructor declaration.
  CanQualType ClassType
    = Context.getCanonicalType(Context.getTypeDeclType(ClassDecl));
  SourceLocation ClassLoc = ClassDecl->getLocation();
  DeclarationName Name
    = Context.DeclarationNames.getCXXConstructorName(ClassType);
  DeclarationNameInfo NameInfo(Name, ClassLoc);
  CXXConstructorDecl *DefaultCon = CXXConstructorDecl::Create(
      Context, ClassDecl, ClassLoc, NameInfo, /*Type*/QualType(),
      /*TInfo=*/nullptr, /*isExplicit=*/false, /*isInline=*/true,
      /*isImplicitlyDeclared=*/true, Constexpr);
  DefaultCon->setAccess(AS_public);
  DefaultCon->setDefaulted();

  if (getLangOpts().CUDA) {
    inferCUDATargetForImplicitSpecialMember(ClassDecl, CXXDefaultConstructor,
                                            DefaultCon,
                                            /* ConstRHS */ false,
                                            /* Diagnose */ false);
  }

  setupImplicitSpecialMemberType(DefaultCon, Context.VoidTy, None);

  // We don't need to use SpecialMemberIsTrivial here; triviality for default
  // constructors is easy to compute.
  DefaultCon->setTrivial(ClassDecl->hasTrivialDefaultConstructor());

  // Note that we have declared this constructor.
  ++ASTContext::NumImplicitDefaultConstructorsDeclared;

  Scope *S = getScopeForContext(ClassDecl);
  CheckImplicitSpecialMemberDeclaration(S, DefaultCon);

  if (ShouldDeleteSpecialMember(DefaultCon, CXXDefaultConstructor))
    SetDeclDeleted(DefaultCon, ClassLoc);

  if (S)
    PushOnScopeChains(DefaultCon, S, false);
  ClassDecl->addDecl(DefaultCon);

  return DefaultCon;
}

void Sema::DefineImplicitDefaultConstructor(SourceLocation CurrentLocation,
                                            CXXConstructorDecl *Constructor) {
  assert((Constructor->isDefaulted() && Constructor->isDefaultConstructor() &&
          !Constructor->doesThisDeclarationHaveABody() &&
          !Constructor->isDeleted()) &&
    "DefineImplicitDefaultConstructor - call it for implicit default ctor");
  if (Constructor->willHaveBody() || Constructor->isInvalidDecl())
    return;

  CXXRecordDecl *ClassDecl = Constructor->getParent();
  assert(ClassDecl && "DefineImplicitDefaultConstructor - invalid constructor");

  SynthesizedFunctionScope Scope(*this, Constructor);

  // The exception specification is needed because we are defining the
  // function.
  ResolveExceptionSpec(CurrentLocation,
                       Constructor->getType()->castAs<FunctionProtoType>());
  MarkVTableUsed(CurrentLocation, ClassDecl);

  // Add a context note for diagnostics produced after this point.
  Scope.addContextNote(CurrentLocation);

  if (SetCtorInitializers(Constructor, /*AnyErrors=*/false)) {
    Constructor->setInvalidDecl();
    return;
  }

  SourceLocation Loc = Constructor->getEndLoc().isValid()
                           ? Constructor->getEndLoc()
                           : Constructor->getLocation();
  Constructor->setBody(new (Context) CompoundStmt(Loc));
  Constructor->markUsed(Context);

  if (ASTMutationListener *L = getASTMutationListener()) {
    L->CompletedImplicitDefinition(Constructor);
  }

  DiagnoseUninitializedFields(*this, Constructor);
}

void Sema::ActOnFinishDelayedMemberInitializers(Decl *D) {
  // Perform any delayed checks on exception specifications.
  CheckDelayedMemberExceptionSpecs();
}

/// Find or create the fake constructor we synthesize to model constructing an
/// object of a derived class via a constructor of a base class.
CXXConstructorDecl *
Sema::findInheritingConstructor(SourceLocation Loc,
                                CXXConstructorDecl *BaseCtor,
                                ConstructorUsingShadowDecl *Shadow) {
  CXXRecordDecl *Derived = Shadow->getParent();
  SourceLocation UsingLoc = Shadow->getLocation();

  // FIXME: Add a new kind of DeclarationName for an inherited constructor.
  // For now we use the name of the base class constructor as a member of the
  // derived class to indicate a (fake) inherited constructor name.
  DeclarationName Name = BaseCtor->getDeclName();

  // Check to see if we already have a fake constructor for this inherited
  // constructor call.
  for (NamedDecl *Ctor : Derived->lookup(Name))
    if (declaresSameEntity(cast<CXXConstructorDecl>(Ctor)
                               ->getInheritedConstructor()
                               .getConstructor(),
                           BaseCtor))
      return cast<CXXConstructorDecl>(Ctor);

  DeclarationNameInfo NameInfo(Name, UsingLoc);
  TypeSourceInfo *TInfo =
      Context.getTrivialTypeSourceInfo(BaseCtor->getType(), UsingLoc);
  FunctionProtoTypeLoc ProtoLoc =
      TInfo->getTypeLoc().IgnoreParens().castAs<FunctionProtoTypeLoc>();

  // Check the inherited constructor is valid and find the list of base classes
  // from which it was inherited.
  InheritedConstructorInfo ICI(*this, Loc, Shadow);

  bool Constexpr =
      BaseCtor->isConstexpr() &&
      defaultedSpecialMemberIsConstexpr(*this, Derived, CXXDefaultConstructor,
                                        false, BaseCtor, &ICI);

  CXXConstructorDecl *DerivedCtor = CXXConstructorDecl::Create(
      Context, Derived, UsingLoc, NameInfo, TInfo->getType(), TInfo,
      BaseCtor->isExplicit(), /*Inline=*/true,
      /*ImplicitlyDeclared=*/true, Constexpr,
      InheritedConstructor(Shadow, BaseCtor));
  if (Shadow->isInvalidDecl())
    DerivedCtor->setInvalidDecl();

  // Build an unevaluated exception specification for this fake constructor.
  const FunctionProtoType *FPT = TInfo->getType()->castAs<FunctionProtoType>();
  FunctionProtoType::ExtProtoInfo EPI = FPT->getExtProtoInfo();
  EPI.ExceptionSpec.Type = EST_Unevaluated;
  EPI.ExceptionSpec.SourceDecl = DerivedCtor;
  DerivedCtor->setType(Context.getFunctionType(FPT->getReturnType(),
                                               FPT->getParamTypes(), EPI));

  // Build the parameter declarations.
  SmallVector<ParmVarDecl *, 16> ParamDecls;
  for (unsigned I = 0, N = FPT->getNumParams(); I != N; ++I) {
    TypeSourceInfo *TInfo =
        Context.getTrivialTypeSourceInfo(FPT->getParamType(I), UsingLoc);
    ParmVarDecl *PD = ParmVarDecl::Create(
        Context, DerivedCtor, UsingLoc, UsingLoc, /*IdentifierInfo=*/nullptr,
        FPT->getParamType(I), TInfo, SC_None, /*DefaultArg=*/nullptr);
    PD->setScopeInfo(0, I);
    PD->setImplicit();
    // Ensure attributes are propagated onto parameters (this matters for
    // format, pass_object_size, ...).
    mergeDeclAttributes(PD, BaseCtor->getParamDecl(I));
    ParamDecls.push_back(PD);
    ProtoLoc.setParam(I, PD);
  }

  // Set up the new constructor.
  assert(!BaseCtor->isDeleted() && "should not use deleted constructor");
  DerivedCtor->setAccess(BaseCtor->getAccess());
  DerivedCtor->setParams(ParamDecls);
  Derived->addDecl(DerivedCtor);

  if (ShouldDeleteSpecialMember(DerivedCtor, CXXDefaultConstructor, &ICI))
    SetDeclDeleted(DerivedCtor, UsingLoc);

  return DerivedCtor;
}

void Sema::NoteDeletedInheritingConstructor(CXXConstructorDecl *Ctor) {
  InheritedConstructorInfo ICI(*this, Ctor->getLocation(),
                               Ctor->getInheritedConstructor().getShadowDecl());
  ShouldDeleteSpecialMember(Ctor, CXXDefaultConstructor, &ICI,
                            /*Diagnose*/true);
}

void Sema::DefineInheritingConstructor(SourceLocation CurrentLocation,
                                       CXXConstructorDecl *Constructor) {
  CXXRecordDecl *ClassDecl = Constructor->getParent();
  assert(Constructor->getInheritedConstructor() &&
         !Constructor->doesThisDeclarationHaveABody() &&
         !Constructor->isDeleted());
  if (Constructor->willHaveBody() || Constructor->isInvalidDecl())
    return;

  // Initializations are performed "as if by a defaulted default constructor",
  // so enter the appropriate scope.
  SynthesizedFunctionScope Scope(*this, Constructor);

  // The exception specification is needed because we are defining the
  // function.
  ResolveExceptionSpec(CurrentLocation,
                       Constructor->getType()->castAs<FunctionProtoType>());
  MarkVTableUsed(CurrentLocation, ClassDecl);

  // Add a context note for diagnostics produced after this point.
  Scope.addContextNote(CurrentLocation);

  ConstructorUsingShadowDecl *Shadow =
      Constructor->getInheritedConstructor().getShadowDecl();
  CXXConstructorDecl *InheritedCtor =
      Constructor->getInheritedConstructor().getConstructor();

  // [class.inhctor.init]p1:
  //   initialization proceeds as if a defaulted default constructor is used to
  //   initialize the D object and each base class subobject from which the
  //   constructor was inherited

  InheritedConstructorInfo ICI(*this, CurrentLocation, Shadow);
  CXXRecordDecl *RD = Shadow->getParent();
  SourceLocation InitLoc = Shadow->getLocation();

  // Build explicit initializers for all base classes from which the
  // constructor was inherited.
  SmallVector<CXXCtorInitializer*, 8> Inits;
  for (bool VBase : {false, true}) {
    for (CXXBaseSpecifier &B : VBase ? RD->vbases() : RD->bases()) {
      if (B.isVirtual() != VBase)
        continue;

      auto *BaseRD = B.getType()->getAsCXXRecordDecl();
      if (!BaseRD)
        continue;

      auto BaseCtor = ICI.findConstructorForBase(BaseRD, InheritedCtor);
      if (!BaseCtor.first)
        continue;

      MarkFunctionReferenced(CurrentLocation, BaseCtor.first);
      ExprResult Init = new (Context) CXXInheritedCtorInitExpr(
          InitLoc, B.getType(), BaseCtor.first, VBase, BaseCtor.second);

      auto *TInfo = Context.getTrivialTypeSourceInfo(B.getType(), InitLoc);
      Inits.push_back(new (Context) CXXCtorInitializer(
          Context, TInfo, VBase, InitLoc, Init.get(), InitLoc,
          SourceLocation()));
    }
  }

  // We now proceed as if for a defaulted default constructor, with the relevant
  // initializers replaced.

  if (SetCtorInitializers(Constructor, /*AnyErrors*/false, Inits)) {
    Constructor->setInvalidDecl();
    return;
  }

  Constructor->setBody(new (Context) CompoundStmt(InitLoc));
  Constructor->markUsed(Context);

  if (ASTMutationListener *L = getASTMutationListener()) {
    L->CompletedImplicitDefinition(Constructor);
  }

  DiagnoseUninitializedFields(*this, Constructor);
}

CXXDestructorDecl *Sema::DeclareImplicitDestructor(CXXRecordDecl *ClassDecl) {
  // C++ [class.dtor]p2:
  //   If a class has no user-declared destructor, a destructor is
  //   declared implicitly. An implicitly-declared destructor is an
  //   inline public member of its class.
  assert(ClassDecl->needsImplicitDestructor());

  DeclaringSpecialMember DSM(*this, ClassDecl, CXXDestructor);
  if (DSM.isAlreadyBeingDeclared())
    return nullptr;

  // Create the actual destructor declaration.
  CanQualType ClassType
    = Context.getCanonicalType(Context.getTypeDeclType(ClassDecl));
  SourceLocation ClassLoc = ClassDecl->getLocation();
  DeclarationName Name
    = Context.DeclarationNames.getCXXDestructorName(ClassType);
  DeclarationNameInfo NameInfo(Name, ClassLoc);
  CXXDestructorDecl *Destructor
      = CXXDestructorDecl::Create(Context, ClassDecl, ClassLoc, NameInfo,
                                  QualType(), nullptr, /*isInline=*/true,
                                  /*isImplicitlyDeclared=*/true);
  Destructor->setAccess(AS_public);
  Destructor->setDefaulted();

  if (getLangOpts().CUDA) {
    inferCUDATargetForImplicitSpecialMember(ClassDecl, CXXDestructor,
                                            Destructor,
                                            /* ConstRHS */ false,
                                            /* Diagnose */ false);
  }

  setupImplicitSpecialMemberType(Destructor, Context.VoidTy, None);

  // We don't need to use SpecialMemberIsTrivial here; triviality for
  // destructors is easy to compute.
  Destructor->setTrivial(ClassDecl->hasTrivialDestructor());
  Destructor->setTrivialForCall(ClassDecl->hasAttr<TrivialABIAttr>() ||
                                ClassDecl->hasTrivialDestructorForCall());

  // Note that we have declared this destructor.
  ++ASTContext::NumImplicitDestructorsDeclared;

  Scope *S = getScopeForContext(ClassDecl);
  CheckImplicitSpecialMemberDeclaration(S, Destructor);

  // We can't check whether an implicit destructor is deleted before we complete
  // the definition of the class, because its validity depends on the alignment
  // of the class. We'll check this from ActOnFields once the class is complete.
  if (ClassDecl->isCompleteDefinition() &&
      ShouldDeleteSpecialMember(Destructor, CXXDestructor))
    SetDeclDeleted(Destructor, ClassLoc);

  // Introduce this destructor into its scope.
  if (S)
    PushOnScopeChains(Destructor, S, false);
  ClassDecl->addDecl(Destructor);

  return Destructor;
}

void Sema::DefineImplicitDestructor(SourceLocation CurrentLocation,
                                    CXXDestructorDecl *Destructor) {
  assert((Destructor->isDefaulted() &&
          !Destructor->doesThisDeclarationHaveABody() &&
          !Destructor->isDeleted()) &&
         "DefineImplicitDestructor - call it for implicit default dtor");
  if (Destructor->willHaveBody() || Destructor->isInvalidDecl())
    return;

  CXXRecordDecl *ClassDecl = Destructor->getParent();
  assert(ClassDecl && "DefineImplicitDestructor - invalid destructor");

  SynthesizedFunctionScope Scope(*this, Destructor);

  // The exception specification is needed because we are defining the
  // function.
  ResolveExceptionSpec(CurrentLocation,
                       Destructor->getType()->castAs<FunctionProtoType>());
  MarkVTableUsed(CurrentLocation, ClassDecl);

  // Add a context note for diagnostics produced after this point.
  Scope.addContextNote(CurrentLocation);

  MarkBaseAndMemberDestructorsReferenced(Destructor->getLocation(),
                                         Destructor->getParent());

  if (CheckDestructor(Destructor)) {
    Destructor->setInvalidDecl();
    return;
  }

  SourceLocation Loc = Destructor->getEndLoc().isValid()
                           ? Destructor->getEndLoc()
                           : Destructor->getLocation();
  Destructor->setBody(new (Context) CompoundStmt(Loc));
  Destructor->markUsed(Context);

  if (ASTMutationListener *L = getASTMutationListener()) {
    L->CompletedImplicitDefinition(Destructor);
  }
}

/// Perform any semantic analysis which needs to be delayed until all
/// pending class member declarations have been parsed.
void Sema::ActOnFinishCXXMemberDecls() {
  // If the context is an invalid C++ class, just suppress these checks.
  if (CXXRecordDecl *Record = dyn_cast<CXXRecordDecl>(CurContext)) {
    if (Record->isInvalidDecl()) {
      DelayedOverridingExceptionSpecChecks.clear();
      DelayedEquivalentExceptionSpecChecks.clear();
      DelayedDefaultedMemberExceptionSpecs.clear();
      return;
    }
    checkForMultipleExportedDefaultConstructors(*this, Record);
  }
}

void Sema::ActOnFinishCXXNonNestedClass(Decl *D) {
  referenceDLLExportedClassMethods();
}

void Sema::referenceDLLExportedClassMethods() {
  if (!DelayedDllExportClasses.empty()) {
    // Calling ReferenceDllExportedMembers might cause the current function to
    // be called again, so use a local copy of DelayedDllExportClasses.
    SmallVector<CXXRecordDecl *, 4> WorkList;
    std::swap(DelayedDllExportClasses, WorkList);
    for (CXXRecordDecl *Class : WorkList)
      ReferenceDllExportedMembers(*this, Class);
  }
}

void Sema::AdjustDestructorExceptionSpec(CXXDestructorDecl *Destructor) {
  assert(getLangOpts().CPlusPlus11 &&
         "adjusting dtor exception specs was introduced in c++11");

  if (Destructor->isDependentContext())
    return;

  // C++11 [class.dtor]p3:
  //   A declaration of a destructor that does not have an exception-
  //   specification is implicitly considered to have the same exception-
  //   specification as an implicit declaration.
  const FunctionProtoType *DtorType = Destructor->getType()->
                                        getAs<FunctionProtoType>();
  if (DtorType->hasExceptionSpec())
    return;

  // Replace the destructor's type, building off the existing one. Fortunately,
  // the only thing of interest in the destructor type is its extended info.
  // The return and arguments are fixed.
  FunctionProtoType::ExtProtoInfo EPI = DtorType->getExtProtoInfo();
  EPI.ExceptionSpec.Type = EST_Unevaluated;
  EPI.ExceptionSpec.SourceDecl = Destructor;
  Destructor->setType(Context.getFunctionType(Context.VoidTy, None, EPI));

  // FIXME: If the destructor has a body that could throw, and the newly created
  // spec doesn't allow exceptions, we should emit a warning, because this
  // change in behavior can break conforming C++03 programs at runtime.
  // However, we don't have a body or an exception specification yet, so it
  // needs to be done somewhere else.
}

namespace {
/// An abstract base class for all helper classes used in building the
//  copy/move operators. These classes serve as factory functions and help us
//  avoid using the same Expr* in the AST twice.
class ExprBuilder {
  ExprBuilder(const ExprBuilder&) = delete;
  ExprBuilder &operator=(const ExprBuilder&) = delete;

protected:
  static Expr *assertNotNull(Expr *E) {
    assert(E && "Expression construction must not fail.");
    return E;
  }

public:
  ExprBuilder() {}
  virtual ~ExprBuilder() {}

  virtual Expr *build(Sema &S, SourceLocation Loc) const = 0;
};

class RefBuilder: public ExprBuilder {
  VarDecl *Var;
  QualType VarType;

public:
  Expr *build(Sema &S, SourceLocation Loc) const override {
    return assertNotNull(S.BuildDeclRefExpr(Var, VarType, VK_LValue, Loc).get());
  }

  RefBuilder(VarDecl *Var, QualType VarType)
      : Var(Var), VarType(VarType) {}
};

class ThisBuilder: public ExprBuilder {
public:
  Expr *build(Sema &S, SourceLocation Loc) const override {
    return assertNotNull(S.ActOnCXXThis(Loc).getAs<Expr>());
  }
};

class CastBuilder: public ExprBuilder {
  const ExprBuilder &Builder;
  QualType Type;
  ExprValueKind Kind;
  const CXXCastPath &Path;

public:
  Expr *build(Sema &S, SourceLocation Loc) const override {
    return assertNotNull(S.ImpCastExprToType(Builder.build(S, Loc), Type,
                                             CK_UncheckedDerivedToBase, Kind,
                                             &Path).get());
  }

  CastBuilder(const ExprBuilder &Builder, QualType Type, ExprValueKind Kind,
              const CXXCastPath &Path)
      : Builder(Builder), Type(Type), Kind(Kind), Path(Path) {}
};

class DerefBuilder: public ExprBuilder {
  const ExprBuilder &Builder;

public:
  Expr *build(Sema &S, SourceLocation Loc) const override {
    return assertNotNull(
        S.CreateBuiltinUnaryOp(Loc, UO_Deref, Builder.build(S, Loc)).get());
  }

  DerefBuilder(const ExprBuilder &Builder) : Builder(Builder) {}
};

class MemberBuilder: public ExprBuilder {
  const ExprBuilder &Builder;
  QualType Type;
  CXXScopeSpec SS;
  bool IsArrow;
  LookupResult &MemberLookup;

public:
  Expr *build(Sema &S, SourceLocation Loc) const override {
    return assertNotNull(S.BuildMemberReferenceExpr(
        Builder.build(S, Loc), Type, Loc, IsArrow, SS, SourceLocation(),
        nullptr, MemberLookup, nullptr, nullptr).get());
  }

  MemberBuilder(const ExprBuilder &Builder, QualType Type, bool IsArrow,
                LookupResult &MemberLookup)
      : Builder(Builder), Type(Type), IsArrow(IsArrow),
        MemberLookup(MemberLookup) {}
};

class MoveCastBuilder: public ExprBuilder {
  const ExprBuilder &Builder;

public:
  Expr *build(Sema &S, SourceLocation Loc) const override {
    return assertNotNull(CastForMoving(S, Builder.build(S, Loc)));
  }

  MoveCastBuilder(const ExprBuilder &Builder) : Builder(Builder) {}
};

class LvalueConvBuilder: public ExprBuilder {
  const ExprBuilder &Builder;

public:
  Expr *build(Sema &S, SourceLocation Loc) const override {
    return assertNotNull(
        S.DefaultLvalueConversion(Builder.build(S, Loc)).get());
  }

  LvalueConvBuilder(const ExprBuilder &Builder) : Builder(Builder) {}
};

class SubscriptBuilder: public ExprBuilder {
  const ExprBuilder &Base;
  const ExprBuilder &Index;

public:
  Expr *build(Sema &S, SourceLocation Loc) const override {
    return assertNotNull(S.CreateBuiltinArraySubscriptExpr(
        Base.build(S, Loc), Loc, Index.build(S, Loc), Loc).get());
  }

  SubscriptBuilder(const ExprBuilder &Base, const ExprBuilder &Index)
      : Base(Base), Index(Index) {}
};

} // end anonymous namespace

/// When generating a defaulted copy or move assignment operator, if a field
/// should be copied with __builtin_memcpy rather than via explicit assignments,
/// do so. This optimization only applies for arrays of scalars, and for arrays
/// of class type where the selected copy/move-assignment operator is trivial.
static StmtResult
buildMemcpyForAssignmentOp(Sema &S, SourceLocation Loc, QualType T,
                           const ExprBuilder &ToB, const ExprBuilder &FromB) {
  // Compute the size of the memory buffer to be copied.
  QualType SizeType = S.Context.getSizeType();
  llvm::APInt Size(S.Context.getTypeSize(SizeType),
                   S.Context.getTypeSizeInChars(T).getQuantity());

  // Take the address of the field references for "from" and "to". We
  // directly construct UnaryOperators here because semantic analysis
  // does not permit us to take the address of an xvalue.
  Expr *From = FromB.build(S, Loc);
  From = new (S.Context) UnaryOperator(From, UO_AddrOf,
                         S.Context.getPointerType(From->getType()),
                         VK_RValue, OK_Ordinary, Loc, false);
  Expr *To = ToB.build(S, Loc);
  To = new (S.Context) UnaryOperator(To, UO_AddrOf,
                       S.Context.getPointerType(To->getType()),
                       VK_RValue, OK_Ordinary, Loc, false);

  const Type *E = T->getBaseElementTypeUnsafe();
  bool NeedsCollectableMemCpy =
    E->isRecordType() && E->getAs<RecordType>()->getDecl()->hasObjectMember();

  // Create a reference to the __builtin_objc_memmove_collectable function
  StringRef MemCpyName = NeedsCollectableMemCpy ?
    "__builtin_objc_memmove_collectable" :
    "__builtin_memcpy";
  LookupResult R(S, &S.Context.Idents.get(MemCpyName), Loc,
                 Sema::LookupOrdinaryName);
  S.LookupName(R, S.TUScope, true);

  FunctionDecl *MemCpy = R.getAsSingle<FunctionDecl>();
  if (!MemCpy)
    // Something went horribly wrong earlier, and we will have complained
    // about it.
    return StmtError();

  ExprResult MemCpyRef = S.BuildDeclRefExpr(MemCpy, S.Context.BuiltinFnTy,
                                            VK_RValue, Loc, nullptr);
  assert(MemCpyRef.isUsable() && "Builtin reference cannot fail");

  Expr *CallArgs[] = {
    To, From, IntegerLiteral::Create(S.Context, Size, SizeType, Loc)
  };
  ExprResult Call = S.ActOnCallExpr(/*Scope=*/nullptr, MemCpyRef.get(),
                                    Loc, CallArgs, Loc);

  assert(!Call.isInvalid() && "Call to __builtin_memcpy cannot fail!");
  return Call.getAs<Stmt>();
}

/// Builds a statement that copies/moves the given entity from \p From to
/// \c To.
///
/// This routine is used to copy/move the members of a class with an
/// implicitly-declared copy/move assignment operator. When the entities being
/// copied are arrays, this routine builds for loops to copy them.
///
/// \param S The Sema object used for type-checking.
///
/// \param Loc The location where the implicit copy/move is being generated.
///
/// \param T The type of the expressions being copied/moved. Both expressions
/// must have this type.
///
/// \param To The expression we are copying/moving to.
///
/// \param From The expression we are copying/moving from.
///
/// \param CopyingBaseSubobject Whether we're copying/moving a base subobject.
/// Otherwise, it's a non-static member subobject.
///
/// \param Copying Whether we're copying or moving.
///
/// \param Depth Internal parameter recording the depth of the recursion.
///
/// \returns A statement or a loop that copies the expressions, or StmtResult(0)
/// if a memcpy should be used instead.
static StmtResult
buildSingleCopyAssignRecursively(Sema &S, SourceLocation Loc, QualType T,
                                 const ExprBuilder &To, const ExprBuilder &From,
                                 bool CopyingBaseSubobject, bool Copying,
                                 unsigned Depth = 0) {
  // C++11 [class.copy]p28:
  //   Each subobject is assigned in the manner appropriate to its type:
  //
  //     - if the subobject is of class type, as if by a call to operator= with
  //       the subobject as the object expression and the corresponding
  //       subobject of x as a single function argument (as if by explicit
  //       qualification; that is, ignoring any possible virtual overriding
  //       functions in more derived classes);
  //
  // C++03 [class.copy]p13:
  //     - if the subobject is of class type, the copy assignment operator for
  //       the class is used (as if by explicit qualification; that is,
  //       ignoring any possible virtual overriding functions in more derived
  //       classes);
  if (const RecordType *RecordTy = T->getAs<RecordType>()) {
    CXXRecordDecl *ClassDecl = cast<CXXRecordDecl>(RecordTy->getDecl());

    // Look for operator=.
    DeclarationName Name
      = S.Context.DeclarationNames.getCXXOperatorName(OO_Equal);
    LookupResult OpLookup(S, Name, Loc, Sema::LookupOrdinaryName);
    S.LookupQualifiedName(OpLookup, ClassDecl, false);

    // Prior to C++11, filter out any result that isn't a copy/move-assignment
    // operator.
    if (!S.getLangOpts().CPlusPlus11) {
      LookupResult::Filter F = OpLookup.makeFilter();
      while (F.hasNext()) {
        NamedDecl *D = F.next();
        if (CXXMethodDecl *Method = dyn_cast<CXXMethodDecl>(D))
          if (Method->isCopyAssignmentOperator() ||
              (!Copying && Method->isMoveAssignmentOperator()))
            continue;

        F.erase();
      }
      F.done();
    }

    // Suppress the protected check (C++ [class.protected]) for each of the
    // assignment operators we found. This strange dance is required when
    // we're assigning via a base classes's copy-assignment operator. To
    // ensure that we're getting the right base class subobject (without
    // ambiguities), we need to cast "this" to that subobject type; to
    // ensure that we don't go through the virtual call mechanism, we need
    // to qualify the operator= name with the base class (see below). However,
    // this means that if the base class has a protected copy assignment
    // operator, the protected member access check will fail. So, we
    // rewrite "protected" access to "public" access in this case, since we
    // know by construction that we're calling from a derived class.
    if (CopyingBaseSubobject) {
      for (LookupResult::iterator L = OpLookup.begin(), LEnd = OpLookup.end();
           L != LEnd; ++L) {
        if (L.getAccess() == AS_protected)
          L.setAccess(AS_public);
      }
    }

    // Create the nested-name-specifier that will be used to qualify the
    // reference to operator=; this is required to suppress the virtual
    // call mechanism.
    CXXScopeSpec SS;
    const Type *CanonicalT = S.Context.getCanonicalType(T.getTypePtr());
    SS.MakeTrivial(S.Context,
                   NestedNameSpecifier::Create(S.Context, nullptr, false,
                                               CanonicalT),
                   Loc);

    // Create the reference to operator=.
    ExprResult OpEqualRef
      = S.BuildMemberReferenceExpr(To.build(S, Loc), T, Loc, /*isArrow=*/false,
                                   SS, /*TemplateKWLoc=*/SourceLocation(),
                                   /*FirstQualifierInScope=*/nullptr,
                                   OpLookup,
                                   /*TemplateArgs=*/nullptr, /*S*/nullptr,
                                   /*SuppressQualifierCheck=*/true);
    if (OpEqualRef.isInvalid())
      return StmtError();

    // Build the call to the assignment operator.

    Expr *FromInst = From.build(S, Loc);
    ExprResult Call = S.BuildCallToMemberFunction(/*Scope=*/nullptr,
                                                  OpEqualRef.getAs<Expr>(),
                                                  Loc, FromInst, Loc);
    if (Call.isInvalid())
      return StmtError();

    // If we built a call to a trivial 'operator=' while copying an array,
    // bail out. We'll replace the whole shebang with a memcpy.
    CXXMemberCallExpr *CE = dyn_cast<CXXMemberCallExpr>(Call.get());
    if (CE && CE->getMethodDecl()->isTrivial() && Depth)
      return StmtResult((Stmt*)nullptr);

    // Convert to an expression-statement, and clean up any produced
    // temporaries.
    return S.ActOnExprStmt(Call);
  }

  //     - if the subobject is of scalar type, the built-in assignment
  //       operator is used.
  const ConstantArrayType *ArrayTy = S.Context.getAsConstantArrayType(T);
  if (!ArrayTy) {
    ExprResult Assignment = S.CreateBuiltinBinOp(
        Loc, BO_Assign, To.build(S, Loc), From.build(S, Loc));
    if (Assignment.isInvalid())
      return StmtError();
    return S.ActOnExprStmt(Assignment);
  }

  //     - if the subobject is an array, each element is assigned, in the
  //       manner appropriate to the element type;

  // Construct a loop over the array bounds, e.g.,
  //
  //   for (__SIZE_TYPE__ i0 = 0; i0 != array-size; ++i0)
  //
  // that will copy each of the array elements.
  QualType SizeType = S.Context.getSizeType();

  // Create the iteration variable.
  IdentifierInfo *IterationVarName = nullptr;
  {
    SmallString<8> Str;
    llvm::raw_svector_ostream OS(Str);
    OS << "__i" << Depth;
    IterationVarName = &S.Context.Idents.get(OS.str());
  }
  VarDecl *IterationVar = VarDecl::Create(S.Context, S.CurContext, Loc, Loc,
                                          IterationVarName, SizeType,
                            S.Context.getTrivialTypeSourceInfo(SizeType, Loc),
                                          SC_None);

  // Initialize the iteration variable to zero.
  llvm::APInt Zero(S.Context.getTypeSize(SizeType), 0);
  IterationVar->setInit(IntegerLiteral::Create(S.Context, Zero, SizeType, Loc));

  // Creates a reference to the iteration variable.
  RefBuilder IterationVarRef(IterationVar, SizeType);
  LvalueConvBuilder IterationVarRefRVal(IterationVarRef);

  // Create the DeclStmt that holds the iteration variable.
  Stmt *InitStmt = new (S.Context) DeclStmt(DeclGroupRef(IterationVar),Loc,Loc);

  // Subscript the "from" and "to" expressions with the iteration variable.
  SubscriptBuilder FromIndexCopy(From, IterationVarRefRVal);
  MoveCastBuilder FromIndexMove(FromIndexCopy);
  const ExprBuilder *FromIndex;
  if (Copying)
    FromIndex = &FromIndexCopy;
  else
    FromIndex = &FromIndexMove;

  SubscriptBuilder ToIndex(To, IterationVarRefRVal);

  // Build the copy/move for an individual element of the array.
  StmtResult Copy =
    buildSingleCopyAssignRecursively(S, Loc, ArrayTy->getElementType(),
                                     ToIndex, *FromIndex, CopyingBaseSubobject,
                                     Copying, Depth + 1);
  // Bail out if copying fails or if we determined that we should use memcpy.
  if (Copy.isInvalid() || !Copy.get())
    return Copy;

  // Create the comparison against the array bound.
  llvm::APInt Upper
    = ArrayTy->getSize().zextOrTrunc(S.Context.getTypeSize(SizeType));
  Expr *Comparison
    = new (S.Context) BinaryOperator(IterationVarRefRVal.build(S, Loc),
                     IntegerLiteral::Create(S.Context, Upper, SizeType, Loc),
                                     BO_NE, S.Context.BoolTy,
                                     VK_RValue, OK_Ordinary, Loc, FPOptions());

  // Create the pre-increment of the iteration variable. We can determine
  // whether the increment will overflow based on the value of the array
  // bound.
  Expr *Increment = new (S.Context)
      UnaryOperator(IterationVarRef.build(S, Loc), UO_PreInc, SizeType,
                    VK_LValue, OK_Ordinary, Loc, Upper.isMaxValue());

  // Construct the loop that copies all elements of this array.
  return S.ActOnForStmt(
      Loc, Loc, InitStmt,
      S.ActOnCondition(nullptr, Loc, Comparison, Sema::ConditionKind::Boolean),
      S.MakeFullDiscardedValueExpr(Increment), Loc, Copy.get());
}

static StmtResult
buildSingleCopyAssign(Sema &S, SourceLocation Loc, QualType T,
                      const ExprBuilder &To, const ExprBuilder &From,
                      bool CopyingBaseSubobject, bool Copying) {
  // Maybe we should use a memcpy?
  if (T->isArrayType() && !T.isConstQualified() && !T.isVolatileQualified() &&
      T.isTriviallyCopyableType(S.Context))
    return buildMemcpyForAssignmentOp(S, Loc, T, To, From);

  StmtResult Result(buildSingleCopyAssignRecursively(S, Loc, T, To, From,
                                                     CopyingBaseSubobject,
                                                     Copying, 0));

  // If we ended up picking a trivial assignment operator for an array of a
  // non-trivially-copyable class type, just emit a memcpy.
  if (!Result.isInvalid() && !Result.get())
    return buildMemcpyForAssignmentOp(S, Loc, T, To, From);

  return Result;
}

CXXMethodDecl *Sema::DeclareImplicitCopyAssignment(CXXRecordDecl *ClassDecl) {
  // Note: The following rules are largely analoguous to the copy
  // constructor rules. Note that virtual bases are not taken into account
  // for determining the argument type of the operator. Note also that
  // operators taking an object instead of a reference are allowed.
  assert(ClassDecl->needsImplicitCopyAssignment());

  DeclaringSpecialMember DSM(*this, ClassDecl, CXXCopyAssignment);
  if (DSM.isAlreadyBeingDeclared())
    return nullptr;

  QualType ArgType = Context.getTypeDeclType(ClassDecl);
  QualType RetType = Context.getLValueReferenceType(ArgType);
  bool Const = ClassDecl->implicitCopyAssignmentHasConstParam();
  if (Const)
    ArgType = ArgType.withConst();

  if (Context.getLangOpts().OpenCLCPlusPlus)
    ArgType = Context.getAddrSpaceQualType(ArgType, LangAS::opencl_generic);

  ArgType = Context.getLValueReferenceType(ArgType);

  bool Constexpr = defaultedSpecialMemberIsConstexpr(*this, ClassDecl,
                                                     CXXCopyAssignment,
                                                     Const);

  //   An implicitly-declared copy assignment operator is an inline public
  //   member of its class.
  DeclarationName Name = Context.DeclarationNames.getCXXOperatorName(OO_Equal);
  SourceLocation ClassLoc = ClassDecl->getLocation();
  DeclarationNameInfo NameInfo(Name, ClassLoc);
  CXXMethodDecl *CopyAssignment =
      CXXMethodDecl::Create(Context, ClassDecl, ClassLoc, NameInfo, QualType(),
                            /*TInfo=*/nullptr, /*StorageClass=*/SC_None,
                            /*isInline=*/true, Constexpr, SourceLocation());
  CopyAssignment->setAccess(AS_public);
  CopyAssignment->setDefaulted();
  CopyAssignment->setImplicit();

  if (getLangOpts().CUDA) {
    inferCUDATargetForImplicitSpecialMember(ClassDecl, CXXCopyAssignment,
                                            CopyAssignment,
                                            /* ConstRHS */ Const,
                                            /* Diagnose */ false);
  }

  setupImplicitSpecialMemberType(CopyAssignment, RetType, ArgType);

  // Add the parameter to the operator.
  ParmVarDecl *FromParam = ParmVarDecl::Create(Context, CopyAssignment,
                                               ClassLoc, ClassLoc,
                                               /*Id=*/nullptr, ArgType,
                                               /*TInfo=*/nullptr, SC_None,
                                               nullptr);
  CopyAssignment->setParams(FromParam);

  CopyAssignment->setTrivial(
    ClassDecl->needsOverloadResolutionForCopyAssignment()
      ? SpecialMemberIsTrivial(CopyAssignment, CXXCopyAssignment)
      : ClassDecl->hasTrivialCopyAssignment());

  // Note that we have added this copy-assignment operator.
  ++ASTContext::NumImplicitCopyAssignmentOperatorsDeclared;

  Scope *S = getScopeForContext(ClassDecl);
  CheckImplicitSpecialMemberDeclaration(S, CopyAssignment);

  if (ShouldDeleteSpecialMember(CopyAssignment, CXXCopyAssignment))
    SetDeclDeleted(CopyAssignment, ClassLoc);

  if (S)
    PushOnScopeChains(CopyAssignment, S, false);
  ClassDecl->addDecl(CopyAssignment);

  return CopyAssignment;
}

/// Diagnose an implicit copy operation for a class which is odr-used, but
/// which is deprecated because the class has a user-declared copy constructor,
/// copy assignment operator, or destructor.
static void diagnoseDeprecatedCopyOperation(Sema &S, CXXMethodDecl *CopyOp) {
  assert(CopyOp->isImplicit());

  CXXRecordDecl *RD = CopyOp->getParent();
  CXXMethodDecl *UserDeclaredOperation = nullptr;

  // In Microsoft mode, assignment operations don't affect constructors and
  // vice versa.
  if (RD->hasUserDeclaredDestructor()) {
    UserDeclaredOperation = RD->getDestructor();
  } else if (!isa<CXXConstructorDecl>(CopyOp) &&
             RD->hasUserDeclaredCopyConstructor() &&
             !S.getLangOpts().MSVCCompat) {
    // Find any user-declared copy constructor.
    for (auto *I : RD->ctors()) {
      if (I->isCopyConstructor()) {
        UserDeclaredOperation = I;
        break;
      }
    }
    assert(UserDeclaredOperation);
  } else if (isa<CXXConstructorDecl>(CopyOp) &&
             RD->hasUserDeclaredCopyAssignment() &&
             !S.getLangOpts().MSVCCompat) {
    // Find any user-declared move assignment operator.
    for (auto *I : RD->methods()) {
      if (I->isCopyAssignmentOperator()) {
        UserDeclaredOperation = I;
        break;
      }
    }
    assert(UserDeclaredOperation);
  }

  if (UserDeclaredOperation) {
    S.Diag(UserDeclaredOperation->getLocation(),
         diag::warn_deprecated_copy_operation)
      << RD << /*copy assignment*/!isa<CXXConstructorDecl>(CopyOp)
      << /*destructor*/isa<CXXDestructorDecl>(UserDeclaredOperation);
  }
}

void Sema::DefineImplicitCopyAssignment(SourceLocation CurrentLocation,
                                        CXXMethodDecl *CopyAssignOperator) {
  assert((CopyAssignOperator->isDefaulted() &&
          CopyAssignOperator->isOverloadedOperator() &&
          CopyAssignOperator->getOverloadedOperator() == OO_Equal &&
          !CopyAssignOperator->doesThisDeclarationHaveABody() &&
          !CopyAssignOperator->isDeleted()) &&
         "DefineImplicitCopyAssignment called for wrong function");
  if (CopyAssignOperator->willHaveBody() || CopyAssignOperator->isInvalidDecl())
    return;

  CXXRecordDecl *ClassDecl = CopyAssignOperator->getParent();
  if (ClassDecl->isInvalidDecl()) {
    CopyAssignOperator->setInvalidDecl();
    return;
  }

  SynthesizedFunctionScope Scope(*this, CopyAssignOperator);

  // The exception specification is needed because we are defining the
  // function.
  ResolveExceptionSpec(CurrentLocation,
                       CopyAssignOperator->getType()->castAs<FunctionProtoType>());

  // Add a context note for diagnostics produced after this point.
  Scope.addContextNote(CurrentLocation);

  // C++11 [class.copy]p18:
  //   The [definition of an implicitly declared copy assignment operator] is
  //   deprecated if the class has a user-declared copy constructor or a
  //   user-declared destructor.
  if (getLangOpts().CPlusPlus11 && CopyAssignOperator->isImplicit())
    diagnoseDeprecatedCopyOperation(*this, CopyAssignOperator);

  // C++0x [class.copy]p30:
  //   The implicitly-defined or explicitly-defaulted copy assignment operator
  //   for a non-union class X performs memberwise copy assignment of its
  //   subobjects. The direct base classes of X are assigned first, in the
  //   order of their declaration in the base-specifier-list, and then the
  //   immediate non-static data members of X are assigned, in the order in
  //   which they were declared in the class definition.

  // The statements that form the synthesized function body.
  SmallVector<Stmt*, 8> Statements;

  // The parameter for the "other" object, which we are copying from.
  ParmVarDecl *Other = CopyAssignOperator->getParamDecl(0);
  Qualifiers OtherQuals = Other->getType().getQualifiers();
  QualType OtherRefType = Other->getType();
  if (const LValueReferenceType *OtherRef
                                = OtherRefType->getAs<LValueReferenceType>()) {
    OtherRefType = OtherRef->getPointeeType();
    OtherQuals = OtherRefType.getQualifiers();
  }

  // Our location for everything implicitly-generated.
  SourceLocation Loc = CopyAssignOperator->getEndLoc().isValid()
                           ? CopyAssignOperator->getEndLoc()
                           : CopyAssignOperator->getLocation();

  // Builds a DeclRefExpr for the "other" object.
  RefBuilder OtherRef(Other, OtherRefType);

  // Builds the "this" pointer.
  ThisBuilder This;

  // Assign base classes.
  bool Invalid = false;
  for (auto &Base : ClassDecl->bases()) {
    // Form the assignment:
    //   static_cast<Base*>(this)->Base::operator=(static_cast<Base&>(other));
    QualType BaseType = Base.getType().getUnqualifiedType();
    if (!BaseType->isRecordType()) {
      Invalid = true;
      continue;
    }

    CXXCastPath BasePath;
    BasePath.push_back(&Base);

    // Construct the "from" expression, which is an implicit cast to the
    // appropriately-qualified base type.
    CastBuilder From(OtherRef, Context.getQualifiedType(BaseType, OtherQuals),
                     VK_LValue, BasePath);

    // Dereference "this".
    DerefBuilder DerefThis(This);
    CastBuilder To(DerefThis,
                   Context.getQualifiedType(
                       BaseType, CopyAssignOperator->getTypeQualifiers()),
                   VK_LValue, BasePath);

    // Build the copy.
    StmtResult Copy = buildSingleCopyAssign(*this, Loc, BaseType,
                                            To, From,
                                            /*CopyingBaseSubobject=*/true,
                                            /*Copying=*/true);
    if (Copy.isInvalid()) {
      CopyAssignOperator->setInvalidDecl();
      return;
    }

    // Success! Record the copy.
    Statements.push_back(Copy.getAs<Expr>());
  }

  // Assign non-static members.
  for (auto *Field : ClassDecl->fields()) {
    // FIXME: We should form some kind of AST representation for the implied
    // memcpy in a union copy operation.
    if (Field->isUnnamedBitfield() || Field->getParent()->isUnion())
      continue;

    if (Field->isInvalidDecl()) {
      Invalid = true;
      continue;
    }

    // Check for members of reference type; we can't copy those.
    if (Field->getType()->isReferenceType()) {
      Diag(ClassDecl->getLocation(), diag::err_uninitialized_member_for_assign)
        << Context.getTagDeclType(ClassDecl) << 0 << Field->getDeclName();
      Diag(Field->getLocation(), diag::note_declared_at);
      Invalid = true;
      continue;
    }

    // Check for members of const-qualified, non-class type.
    QualType BaseType = Context.getBaseElementType(Field->getType());
    if (!BaseType->getAs<RecordType>() && BaseType.isConstQualified()) {
      Diag(ClassDecl->getLocation(), diag::err_uninitialized_member_for_assign)
        << Context.getTagDeclType(ClassDecl) << 1 << Field->getDeclName();
      Diag(Field->getLocation(), diag::note_declared_at);
      Invalid = true;
      continue;
    }

    // Suppress assigning zero-width bitfields.
    if (Field->isZeroLengthBitField(Context))
      continue;

    QualType FieldType = Field->getType().getNonReferenceType();
    if (FieldType->isIncompleteArrayType()) {
      assert(ClassDecl->hasFlexibleArrayMember() &&
             "Incomplete array type is not valid");
      continue;
    }

    // Build references to the field in the object we're copying from and to.
    CXXScopeSpec SS; // Intentionally empty
    LookupResult MemberLookup(*this, Field->getDeclName(), Loc,
                              LookupMemberName);
    MemberLookup.addDecl(Field);
    MemberLookup.resolveKind();

    MemberBuilder From(OtherRef, OtherRefType, /*IsArrow=*/false, MemberLookup);

    MemberBuilder To(This, getCurrentThisType(), /*IsArrow=*/true, MemberLookup);

    // Build the copy of this field.
    StmtResult Copy = buildSingleCopyAssign(*this, Loc, FieldType,
                                            To, From,
                                            /*CopyingBaseSubobject=*/false,
                                            /*Copying=*/true);
    if (Copy.isInvalid()) {
      CopyAssignOperator->setInvalidDecl();
      return;
    }

    // Success! Record the copy.
    Statements.push_back(Copy.getAs<Stmt>());
  }

  if (!Invalid) {
    // Add a "return *this;"
    ExprResult ThisObj = CreateBuiltinUnaryOp(Loc, UO_Deref, This.build(*this, Loc));

    StmtResult Return = BuildReturnStmt(Loc, ThisObj.get());
    if (Return.isInvalid())
      Invalid = true;
    else
      Statements.push_back(Return.getAs<Stmt>());
  }

  if (Invalid) {
    CopyAssignOperator->setInvalidDecl();
    return;
  }

  StmtResult Body;
  {
    CompoundScopeRAII CompoundScope(*this);
    Body = ActOnCompoundStmt(Loc, Loc, Statements,
                             /*isStmtExpr=*/false);
    assert(!Body.isInvalid() && "Compound statement creation cannot fail");
  }
  CopyAssignOperator->setBody(Body.getAs<Stmt>());
  CopyAssignOperator->markUsed(Context);

  if (ASTMutationListener *L = getASTMutationListener()) {
    L->CompletedImplicitDefinition(CopyAssignOperator);
  }
}

CXXMethodDecl *Sema::DeclareImplicitMoveAssignment(CXXRecordDecl *ClassDecl) {
  assert(ClassDecl->needsImplicitMoveAssignment());

  DeclaringSpecialMember DSM(*this, ClassDecl, CXXMoveAssignment);
  if (DSM.isAlreadyBeingDeclared())
    return nullptr;

  // Note: The following rules are largely analoguous to the move
  // constructor rules.

  QualType ArgType = Context.getTypeDeclType(ClassDecl);
  QualType RetType = Context.getLValueReferenceType(ArgType);
  ArgType = Context.getRValueReferenceType(ArgType);

  bool Constexpr = defaultedSpecialMemberIsConstexpr(*this, ClassDecl,
                                                     CXXMoveAssignment,
                                                     false);

  //   An implicitly-declared move assignment operator is an inline public
  //   member of its class.
  DeclarationName Name = Context.DeclarationNames.getCXXOperatorName(OO_Equal);
  SourceLocation ClassLoc = ClassDecl->getLocation();
  DeclarationNameInfo NameInfo(Name, ClassLoc);
  CXXMethodDecl *MoveAssignment =
      CXXMethodDecl::Create(Context, ClassDecl, ClassLoc, NameInfo, QualType(),
                            /*TInfo=*/nullptr, /*StorageClass=*/SC_None,
                            /*isInline=*/true, Constexpr, SourceLocation());
  MoveAssignment->setAccess(AS_public);
  MoveAssignment->setDefaulted();
  MoveAssignment->setImplicit();

  if (getLangOpts().CUDA) {
    inferCUDATargetForImplicitSpecialMember(ClassDecl, CXXMoveAssignment,
                                            MoveAssignment,
                                            /* ConstRHS */ false,
                                            /* Diagnose */ false);
  }

  // Build an exception specification pointing back at this member.
  FunctionProtoType::ExtProtoInfo EPI =
      getImplicitMethodEPI(*this, MoveAssignment);
  MoveAssignment->setType(Context.getFunctionType(RetType, ArgType, EPI));

  // Add the parameter to the operator.
  ParmVarDecl *FromParam = ParmVarDecl::Create(Context, MoveAssignment,
                                               ClassLoc, ClassLoc,
                                               /*Id=*/nullptr, ArgType,
                                               /*TInfo=*/nullptr, SC_None,
                                               nullptr);
  MoveAssignment->setParams(FromParam);

  MoveAssignment->setTrivial(
    ClassDecl->needsOverloadResolutionForMoveAssignment()
      ? SpecialMemberIsTrivial(MoveAssignment, CXXMoveAssignment)
      : ClassDecl->hasTrivialMoveAssignment());

  // Note that we have added this copy-assignment operator.
  ++ASTContext::NumImplicitMoveAssignmentOperatorsDeclared;

  Scope *S = getScopeForContext(ClassDecl);
  CheckImplicitSpecialMemberDeclaration(S, MoveAssignment);

  if (ShouldDeleteSpecialMember(MoveAssignment, CXXMoveAssignment)) {
    ClassDecl->setImplicitMoveAssignmentIsDeleted();
    SetDeclDeleted(MoveAssignment, ClassLoc);
  }

  if (S)
    PushOnScopeChains(MoveAssignment, S, false);
  ClassDecl->addDecl(MoveAssignment);

  return MoveAssignment;
}

/// Check if we're implicitly defining a move assignment operator for a class
/// with virtual bases. Such a move assignment might move-assign the virtual
/// base multiple times.
static void checkMoveAssignmentForRepeatedMove(Sema &S, CXXRecordDecl *Class,
                                               SourceLocation CurrentLocation) {
  assert(!Class->isDependentContext() && "should not define dependent move");

  // Only a virtual base could get implicitly move-assigned multiple times.
  // Only a non-trivial move assignment can observe this. We only want to
  // diagnose if we implicitly define an assignment operator that assigns
  // two base classes, both of which move-assign the same virtual base.
  if (Class->getNumVBases() == 0 || Class->hasTrivialMoveAssignment() ||
      Class->getNumBases() < 2)
    return;

  llvm::SmallVector<CXXBaseSpecifier *, 16> Worklist;
  typedef llvm::DenseMap<CXXRecordDecl*, CXXBaseSpecifier*> VBaseMap;
  VBaseMap VBases;

  for (auto &BI : Class->bases()) {
    Worklist.push_back(&BI);
    while (!Worklist.empty()) {
      CXXBaseSpecifier *BaseSpec = Worklist.pop_back_val();
      CXXRecordDecl *Base = BaseSpec->getType()->getAsCXXRecordDecl();

      // If the base has no non-trivial move assignment operators,
      // we don't care about moves from it.
      if (!Base->hasNonTrivialMoveAssignment())
        continue;

      // If there's nothing virtual here, skip it.
      if (!BaseSpec->isVirtual() && !Base->getNumVBases())
        continue;

      // If we're not actually going to call a move assignment for this base,
      // or the selected move assignment is trivial, skip it.
      Sema::SpecialMemberOverloadResult SMOR =
        S.LookupSpecialMember(Base, Sema::CXXMoveAssignment,
                              /*ConstArg*/false, /*VolatileArg*/false,
                              /*RValueThis*/true, /*ConstThis*/false,
                              /*VolatileThis*/false);
      if (!SMOR.getMethod() || SMOR.getMethod()->isTrivial() ||
          !SMOR.getMethod()->isMoveAssignmentOperator())
        continue;

      if (BaseSpec->isVirtual()) {
        // We're going to move-assign this virtual base, and its move
        // assignment operator is not trivial. If this can happen for
        // multiple distinct direct bases of Class, diagnose it. (If it
        // only happens in one base, we'll diagnose it when synthesizing
        // that base class's move assignment operator.)
        CXXBaseSpecifier *&Existing =
            VBases.insert(std::make_pair(Base->getCanonicalDecl(), &BI))
                .first->second;
        if (Existing && Existing != &BI) {
          S.Diag(CurrentLocation, diag::warn_vbase_moved_multiple_times)
            << Class << Base;
          S.Diag(Existing->getBeginLoc(), diag::note_vbase_moved_here)
              << (Base->getCanonicalDecl() ==
                  Existing->getType()->getAsCXXRecordDecl()->getCanonicalDecl())
              << Base << Existing->getType() << Existing->getSourceRange();
          S.Diag(BI.getBeginLoc(), diag::note_vbase_moved_here)
              << (Base->getCanonicalDecl() ==
                  BI.getType()->getAsCXXRecordDecl()->getCanonicalDecl())
              << Base << BI.getType() << BaseSpec->getSourceRange();

          // Only diagnose each vbase once.
          Existing = nullptr;
        }
      } else {
        // Only walk over bases that have defaulted move assignment operators.
        // We assume that any user-provided move assignment operator handles
        // the multiple-moves-of-vbase case itself somehow.
        if (!SMOR.getMethod()->isDefaulted())
          continue;

        // We're going to move the base classes of Base. Add them to the list.
        for (auto &BI : Base->bases())
          Worklist.push_back(&BI);
      }
    }
  }
}

void Sema::DefineImplicitMoveAssignment(SourceLocation CurrentLocation,
                                        CXXMethodDecl *MoveAssignOperator) {
  assert((MoveAssignOperator->isDefaulted() &&
          MoveAssignOperator->isOverloadedOperator() &&
          MoveAssignOperator->getOverloadedOperator() == OO_Equal &&
          !MoveAssignOperator->doesThisDeclarationHaveABody() &&
          !MoveAssignOperator->isDeleted()) &&
         "DefineImplicitMoveAssignment called for wrong function");
  if (MoveAssignOperator->willHaveBody() || MoveAssignOperator->isInvalidDecl())
    return;

  CXXRecordDecl *ClassDecl = MoveAssignOperator->getParent();
  if (ClassDecl->isInvalidDecl()) {
    MoveAssignOperator->setInvalidDecl();
    return;
  }

  // C++0x [class.copy]p28:
  //   The implicitly-defined or move assignment operator for a non-union class
  //   X performs memberwise move assignment of its subobjects. The direct base
  //   classes of X are assigned first, in the order of their declaration in the
  //   base-specifier-list, and then the immediate non-static data members of X
  //   are assigned, in the order in which they were declared in the class
  //   definition.

  // Issue a warning if our implicit move assignment operator will move
  // from a virtual base more than once.
  checkMoveAssignmentForRepeatedMove(*this, ClassDecl, CurrentLocation);

  SynthesizedFunctionScope Scope(*this, MoveAssignOperator);

  // The exception specification is needed because we are defining the
  // function.
  ResolveExceptionSpec(CurrentLocation,
                       MoveAssignOperator->getType()->castAs<FunctionProtoType>());

  // Add a context note for diagnostics produced after this point.
  Scope.addContextNote(CurrentLocation);

  // The statements that form the synthesized function body.
  SmallVector<Stmt*, 8> Statements;

  // The parameter for the "other" object, which we are move from.
  ParmVarDecl *Other = MoveAssignOperator->getParamDecl(0);
  QualType OtherRefType = Other->getType()->
      getAs<RValueReferenceType>()->getPointeeType();

  // Our location for everything implicitly-generated.
  SourceLocation Loc = MoveAssignOperator->getEndLoc().isValid()
                           ? MoveAssignOperator->getEndLoc()
                           : MoveAssignOperator->getLocation();

  // Builds a reference to the "other" object.
  RefBuilder OtherRef(Other, OtherRefType);
  // Cast to rvalue.
  MoveCastBuilder MoveOther(OtherRef);

  // Builds the "this" pointer.
  ThisBuilder This;

  // Assign base classes.
  bool Invalid = false;
  for (auto &Base : ClassDecl->bases()) {
    // C++11 [class.copy]p28:
    //   It is unspecified whether subobjects representing virtual base classes
    //   are assigned more than once by the implicitly-defined copy assignment
    //   operator.
    // FIXME: Do not assign to a vbase that will be assigned by some other base
    // class. For a move-assignment, this can result in the vbase being moved
    // multiple times.

    // Form the assignment:
    //   static_cast<Base*>(this)->Base::operator=(static_cast<Base&&>(other));
    QualType BaseType = Base.getType().getUnqualifiedType();
    if (!BaseType->isRecordType()) {
      Invalid = true;
      continue;
    }

    CXXCastPath BasePath;
    BasePath.push_back(&Base);

    // Construct the "from" expression, which is an implicit cast to the
    // appropriately-qualified base type.
    CastBuilder From(OtherRef, BaseType, VK_XValue, BasePath);

    // Dereference "this".
    DerefBuilder DerefThis(This);

    // Implicitly cast "this" to the appropriately-qualified base type.
    CastBuilder To(DerefThis,
                   Context.getQualifiedType(
                       BaseType, MoveAssignOperator->getTypeQualifiers()),
                   VK_LValue, BasePath);

    // Build the move.
    StmtResult Move = buildSingleCopyAssign(*this, Loc, BaseType,
                                            To, From,
                                            /*CopyingBaseSubobject=*/true,
                                            /*Copying=*/false);
    if (Move.isInvalid()) {
      MoveAssignOperator->setInvalidDecl();
      return;
    }

    // Success! Record the move.
    Statements.push_back(Move.getAs<Expr>());
  }

  // Assign non-static members.
  for (auto *Field : ClassDecl->fields()) {
    // FIXME: We should form some kind of AST representation for the implied
    // memcpy in a union copy operation.
    if (Field->isUnnamedBitfield() || Field->getParent()->isUnion())
      continue;

    if (Field->isInvalidDecl()) {
      Invalid = true;
      continue;
    }

    // Check for members of reference type; we can't move those.
    if (Field->getType()->isReferenceType()) {
      Diag(ClassDecl->getLocation(), diag::err_uninitialized_member_for_assign)
        << Context.getTagDeclType(ClassDecl) << 0 << Field->getDeclName();
      Diag(Field->getLocation(), diag::note_declared_at);
      Invalid = true;
      continue;
    }

    // Check for members of const-qualified, non-class type.
    QualType BaseType = Context.getBaseElementType(Field->getType());
    if (!BaseType->getAs<RecordType>() && BaseType.isConstQualified()) {
      Diag(ClassDecl->getLocation(), diag::err_uninitialized_member_for_assign)
        << Context.getTagDeclType(ClassDecl) << 1 << Field->getDeclName();
      Diag(Field->getLocation(), diag::note_declared_at);
      Invalid = true;
      continue;
    }

    // Suppress assigning zero-width bitfields.
    if (Field->isZeroLengthBitField(Context))
      continue;

    QualType FieldType = Field->getType().getNonReferenceType();
    if (FieldType->isIncompleteArrayType()) {
      assert(ClassDecl->hasFlexibleArrayMember() &&
             "Incomplete array type is not valid");
      continue;
    }

    // Build references to the field in the object we're copying from and to.
    LookupResult MemberLookup(*this, Field->getDeclName(), Loc,
                              LookupMemberName);
    MemberLookup.addDecl(Field);
    MemberLookup.resolveKind();
    MemberBuilder From(MoveOther, OtherRefType,
                       /*IsArrow=*/false, MemberLookup);
    MemberBuilder To(This, getCurrentThisType(),
                     /*IsArrow=*/true, MemberLookup);

    assert(!From.build(*this, Loc)->isLValue() && // could be xvalue or prvalue
        "Member reference with rvalue base must be rvalue except for reference "
        "members, which aren't allowed for move assignment.");

    // Build the move of this field.
    StmtResult Move = buildSingleCopyAssign(*this, Loc, FieldType,
                                            To, From,
                                            /*CopyingBaseSubobject=*/false,
                                            /*Copying=*/false);
    if (Move.isInvalid()) {
      MoveAssignOperator->setInvalidDecl();
      return;
    }

    // Success! Record the copy.
    Statements.push_back(Move.getAs<Stmt>());
  }

  if (!Invalid) {
    // Add a "return *this;"
    ExprResult ThisObj =
        CreateBuiltinUnaryOp(Loc, UO_Deref, This.build(*this, Loc));

    StmtResult Return = BuildReturnStmt(Loc, ThisObj.get());
    if (Return.isInvalid())
      Invalid = true;
    else
      Statements.push_back(Return.getAs<Stmt>());
  }

  if (Invalid) {
    MoveAssignOperator->setInvalidDecl();
    return;
  }

  StmtResult Body;
  {
    CompoundScopeRAII CompoundScope(*this);
    Body = ActOnCompoundStmt(Loc, Loc, Statements,
                             /*isStmtExpr=*/false);
    assert(!Body.isInvalid() && "Compound statement creation cannot fail");
  }
  MoveAssignOperator->setBody(Body.getAs<Stmt>());
  MoveAssignOperator->markUsed(Context);

  if (ASTMutationListener *L = getASTMutationListener()) {
    L->CompletedImplicitDefinition(MoveAssignOperator);
  }
}

CXXConstructorDecl *Sema::DeclareImplicitCopyConstructor(
                                                    CXXRecordDecl *ClassDecl) {
  // C++ [class.copy]p4:
  //   If the class definition does not explicitly declare a copy
  //   constructor, one is declared implicitly.
  assert(ClassDecl->needsImplicitCopyConstructor());

  DeclaringSpecialMember DSM(*this, ClassDecl, CXXCopyConstructor);
  if (DSM.isAlreadyBeingDeclared())
    return nullptr;

  QualType ClassType = Context.getTypeDeclType(ClassDecl);
  QualType ArgType = ClassType;
  bool Const = ClassDecl->implicitCopyConstructorHasConstParam();
  if (Const)
    ArgType = ArgType.withConst();

  if (Context.getLangOpts().OpenCLCPlusPlus)
    ArgType = Context.getAddrSpaceQualType(ArgType, LangAS::opencl_generic);

  ArgType = Context.getLValueReferenceType(ArgType);

  bool Constexpr = defaultedSpecialMemberIsConstexpr(*this, ClassDecl,
                                                     CXXCopyConstructor,
                                                     Const);

  DeclarationName Name
    = Context.DeclarationNames.getCXXConstructorName(
                                           Context.getCanonicalType(ClassType));
  SourceLocation ClassLoc = ClassDecl->getLocation();
  DeclarationNameInfo NameInfo(Name, ClassLoc);

  //   An implicitly-declared copy constructor is an inline public
  //   member of its class.
  CXXConstructorDecl *CopyConstructor = CXXConstructorDecl::Create(
      Context, ClassDecl, ClassLoc, NameInfo, QualType(), /*TInfo=*/nullptr,
      /*isExplicit=*/false, /*isInline=*/true, /*isImplicitlyDeclared=*/true,
      Constexpr);
  CopyConstructor->setAccess(AS_public);
  CopyConstructor->setDefaulted();

  if (getLangOpts().CUDA) {
    inferCUDATargetForImplicitSpecialMember(ClassDecl, CXXCopyConstructor,
                                            CopyConstructor,
                                            /* ConstRHS */ Const,
                                            /* Diagnose */ false);
  }

  setupImplicitSpecialMemberType(CopyConstructor, Context.VoidTy, ArgType);

  // Add the parameter to the constructor.
  ParmVarDecl *FromParam = ParmVarDecl::Create(Context, CopyConstructor,
                                               ClassLoc, ClassLoc,
                                               /*IdentifierInfo=*/nullptr,
                                               ArgType, /*TInfo=*/nullptr,
                                               SC_None, nullptr);
  CopyConstructor->setParams(FromParam);

  CopyConstructor->setTrivial(
      ClassDecl->needsOverloadResolutionForCopyConstructor()
          ? SpecialMemberIsTrivial(CopyConstructor, CXXCopyConstructor)
          : ClassDecl->hasTrivialCopyConstructor());

  CopyConstructor->setTrivialForCall(
      ClassDecl->hasAttr<TrivialABIAttr>() ||
      (ClassDecl->needsOverloadResolutionForCopyConstructor()
           ? SpecialMemberIsTrivial(CopyConstructor, CXXCopyConstructor,
             TAH_ConsiderTrivialABI)
           : ClassDecl->hasTrivialCopyConstructorForCall()));

  // Note that we have declared this constructor.
  ++ASTContext::NumImplicitCopyConstructorsDeclared;

  Scope *S = getScopeForContext(ClassDecl);
  CheckImplicitSpecialMemberDeclaration(S, CopyConstructor);

  if (ShouldDeleteSpecialMember(CopyConstructor, CXXCopyConstructor)) {
    ClassDecl->setImplicitCopyConstructorIsDeleted();
    SetDeclDeleted(CopyConstructor, ClassLoc);
  }

  if (S)
    PushOnScopeChains(CopyConstructor, S, false);
  ClassDecl->addDecl(CopyConstructor);

  return CopyConstructor;
}

void Sema::DefineImplicitCopyConstructor(SourceLocation CurrentLocation,
                                         CXXConstructorDecl *CopyConstructor) {
  assert((CopyConstructor->isDefaulted() &&
          CopyConstructor->isCopyConstructor() &&
          !CopyConstructor->doesThisDeclarationHaveABody() &&
          !CopyConstructor->isDeleted()) &&
         "DefineImplicitCopyConstructor - call it for implicit copy ctor");
  if (CopyConstructor->willHaveBody() || CopyConstructor->isInvalidDecl())
    return;

  CXXRecordDecl *ClassDecl = CopyConstructor->getParent();
  assert(ClassDecl && "DefineImplicitCopyConstructor - invalid constructor");

  SynthesizedFunctionScope Scope(*this, CopyConstructor);

  // The exception specification is needed because we are defining the
  // function.
  ResolveExceptionSpec(CurrentLocation,
                       CopyConstructor->getType()->castAs<FunctionProtoType>());
  MarkVTableUsed(CurrentLocation, ClassDecl);

  // Add a context note for diagnostics produced after this point.
  Scope.addContextNote(CurrentLocation);

  // C++11 [class.copy]p7:
  //   The [definition of an implicitly declared copy constructor] is
  //   deprecated if the class has a user-declared copy assignment operator
  //   or a user-declared destructor.
  if (getLangOpts().CPlusPlus11 && CopyConstructor->isImplicit())
    diagnoseDeprecatedCopyOperation(*this, CopyConstructor);

  if (SetCtorInitializers(CopyConstructor, /*AnyErrors=*/false)) {
    CopyConstructor->setInvalidDecl();
  }  else {
    SourceLocation Loc = CopyConstructor->getEndLoc().isValid()
                             ? CopyConstructor->getEndLoc()
                             : CopyConstructor->getLocation();
    Sema::CompoundScopeRAII CompoundScope(*this);
    CopyConstructor->setBody(
        ActOnCompoundStmt(Loc, Loc, None, /*isStmtExpr=*/false).getAs<Stmt>());
    CopyConstructor->markUsed(Context);
  }

  if (ASTMutationListener *L = getASTMutationListener()) {
    L->CompletedImplicitDefinition(CopyConstructor);
  }
}

CXXConstructorDecl *Sema::DeclareImplicitMoveConstructor(
                                                    CXXRecordDecl *ClassDecl) {
  assert(ClassDecl->needsImplicitMoveConstructor());

  DeclaringSpecialMember DSM(*this, ClassDecl, CXXMoveConstructor);
  if (DSM.isAlreadyBeingDeclared())
    return nullptr;

  QualType ClassType = Context.getTypeDeclType(ClassDecl);

  QualType ArgType = ClassType;
  if (Context.getLangOpts().OpenCLCPlusPlus)
    ArgType = Context.getAddrSpaceQualType(ClassType, LangAS::opencl_generic);
  ArgType = Context.getRValueReferenceType(ArgType);

  bool Constexpr = defaultedSpecialMemberIsConstexpr(*this, ClassDecl,
                                                     CXXMoveConstructor,
                                                     false);

  DeclarationName Name
    = Context.DeclarationNames.getCXXConstructorName(
                                           Context.getCanonicalType(ClassType));
  SourceLocation ClassLoc = ClassDecl->getLocation();
  DeclarationNameInfo NameInfo(Name, ClassLoc);

  // C++11 [class.copy]p11:
  //   An implicitly-declared copy/move constructor is an inline public
  //   member of its class.
  CXXConstructorDecl *MoveConstructor = CXXConstructorDecl::Create(
      Context, ClassDecl, ClassLoc, NameInfo, QualType(), /*TInfo=*/nullptr,
      /*isExplicit=*/false, /*isInline=*/true, /*isImplicitlyDeclared=*/true,
      Constexpr);
  MoveConstructor->setAccess(AS_public);
  MoveConstructor->setDefaulted();

  if (getLangOpts().CUDA) {
    inferCUDATargetForImplicitSpecialMember(ClassDecl, CXXMoveConstructor,
                                            MoveConstructor,
                                            /* ConstRHS */ false,
                                            /* Diagnose */ false);
  }

  setupImplicitSpecialMemberType(MoveConstructor, Context.VoidTy, ArgType);

  // Add the parameter to the constructor.
  ParmVarDecl *FromParam = ParmVarDecl::Create(Context, MoveConstructor,
                                               ClassLoc, ClassLoc,
                                               /*IdentifierInfo=*/nullptr,
                                               ArgType, /*TInfo=*/nullptr,
                                               SC_None, nullptr);
  MoveConstructor->setParams(FromParam);

  MoveConstructor->setTrivial(
      ClassDecl->needsOverloadResolutionForMoveConstructor()
          ? SpecialMemberIsTrivial(MoveConstructor, CXXMoveConstructor)
          : ClassDecl->hasTrivialMoveConstructor());

  MoveConstructor->setTrivialForCall(
      ClassDecl->hasAttr<TrivialABIAttr>() ||
      (ClassDecl->needsOverloadResolutionForMoveConstructor()
           ? SpecialMemberIsTrivial(MoveConstructor, CXXMoveConstructor,
                                    TAH_ConsiderTrivialABI)
           : ClassDecl->hasTrivialMoveConstructorForCall()));

  // Note that we have declared this constructor.
  ++ASTContext::NumImplicitMoveConstructorsDeclared;

  Scope *S = getScopeForContext(ClassDecl);
  CheckImplicitSpecialMemberDeclaration(S, MoveConstructor);

  if (ShouldDeleteSpecialMember(MoveConstructor, CXXMoveConstructor)) {
    ClassDecl->setImplicitMoveConstructorIsDeleted();
    SetDeclDeleted(MoveConstructor, ClassLoc);
  }

  if (S)
    PushOnScopeChains(MoveConstructor, S, false);
  ClassDecl->addDecl(MoveConstructor);

  return MoveConstructor;
}

void Sema::DefineImplicitMoveConstructor(SourceLocation CurrentLocation,
                                         CXXConstructorDecl *MoveConstructor) {
  assert((MoveConstructor->isDefaulted() &&
          MoveConstructor->isMoveConstructor() &&
          !MoveConstructor->doesThisDeclarationHaveABody() &&
          !MoveConstructor->isDeleted()) &&
         "DefineImplicitMoveConstructor - call it for implicit move ctor");
  if (MoveConstructor->willHaveBody() || MoveConstructor->isInvalidDecl())
    return;

  CXXRecordDecl *ClassDecl = MoveConstructor->getParent();
  assert(ClassDecl && "DefineImplicitMoveConstructor - invalid constructor");

  SynthesizedFunctionScope Scope(*this, MoveConstructor);

  // The exception specification is needed because we are defining the
  // function.
  ResolveExceptionSpec(CurrentLocation,
                       MoveConstructor->getType()->castAs<FunctionProtoType>());
  MarkVTableUsed(CurrentLocation, ClassDecl);

  // Add a context note for diagnostics produced after this point.
  Scope.addContextNote(CurrentLocation);

  if (SetCtorInitializers(MoveConstructor, /*AnyErrors=*/false)) {
    MoveConstructor->setInvalidDecl();
  } else {
    SourceLocation Loc = MoveConstructor->getEndLoc().isValid()
                             ? MoveConstructor->getEndLoc()
                             : MoveConstructor->getLocation();
    Sema::CompoundScopeRAII CompoundScope(*this);
    MoveConstructor->setBody(ActOnCompoundStmt(
        Loc, Loc, None, /*isStmtExpr=*/ false).getAs<Stmt>());
    MoveConstructor->markUsed(Context);
  }

  if (ASTMutationListener *L = getASTMutationListener()) {
    L->CompletedImplicitDefinition(MoveConstructor);
  }
}

bool Sema::isImplicitlyDeleted(FunctionDecl *FD) {
  return FD->isDeleted() && FD->isDefaulted() && isa<CXXMethodDecl>(FD);
}

void Sema::DefineImplicitLambdaToFunctionPointerConversion(
                            SourceLocation CurrentLocation,
                            CXXConversionDecl *Conv) {
  SynthesizedFunctionScope Scope(*this, Conv);
  assert(!Conv->getReturnType()->isUndeducedType());

  CXXRecordDecl *Lambda = Conv->getParent();
  FunctionDecl *CallOp = Lambda->getLambdaCallOperator();
  FunctionDecl *Invoker = Lambda->getLambdaStaticInvoker();

  if (auto *TemplateArgs = Conv->getTemplateSpecializationArgs()) {
    CallOp = InstantiateFunctionDeclaration(
        CallOp->getDescribedFunctionTemplate(), TemplateArgs, CurrentLocation);
    if (!CallOp)
      return;

    Invoker = InstantiateFunctionDeclaration(
        Invoker->getDescribedFunctionTemplate(), TemplateArgs, CurrentLocation);
    if (!Invoker)
      return;
  }

  if (CallOp->isInvalidDecl())
    return;

  // Mark the call operator referenced (and add to pending instantiations
  // if necessary).
  // For both the conversion and static-invoker template specializations
  // we construct their body's in this function, so no need to add them
  // to the PendingInstantiations.
  MarkFunctionReferenced(CurrentLocation, CallOp);

  // Fill in the __invoke function with a dummy implementation. IR generation
  // will fill in the actual details. Update its type in case it contained
  // an 'auto'.
  Invoker->markUsed(Context);
  Invoker->setReferenced();
  Invoker->setType(Conv->getReturnType()->getPointeeType());
  Invoker->setBody(new (Context) CompoundStmt(Conv->getLocation()));

  // Construct the body of the conversion function { return __invoke; }.
  Expr *FunctionRef = BuildDeclRefExpr(Invoker, Invoker->getType(),
                                       VK_LValue, Conv->getLocation()).get();
  assert(FunctionRef && "Can't refer to __invoke function?");
  Stmt *Return = BuildReturnStmt(Conv->getLocation(), FunctionRef).get();
  Conv->setBody(CompoundStmt::Create(Context, Return, Conv->getLocation(),
                                     Conv->getLocation()));
  Conv->markUsed(Context);
  Conv->setReferenced();

  if (ASTMutationListener *L = getASTMutationListener()) {
    L->CompletedImplicitDefinition(Conv);
    L->CompletedImplicitDefinition(Invoker);
  }
}



void Sema::DefineImplicitLambdaToBlockPointerConversion(
       SourceLocation CurrentLocation,
       CXXConversionDecl *Conv)
{
  assert(!Conv->getParent()->isGenericLambda());

  SynthesizedFunctionScope Scope(*this, Conv);

  // Copy-initialize the lambda object as needed to capture it.
  Expr *This = ActOnCXXThis(CurrentLocation).get();
  Expr *DerefThis =CreateBuiltinUnaryOp(CurrentLocation, UO_Deref, This).get();

  ExprResult BuildBlock = BuildBlockForLambdaConversion(CurrentLocation,
                                                        Conv->getLocation(),
                                                        Conv, DerefThis);

  // If we're not under ARC, make sure we still get the _Block_copy/autorelease
  // behavior.  Note that only the general conversion function does this
  // (since it's unusable otherwise); in the case where we inline the
  // block literal, it has block literal lifetime semantics.
  if (!BuildBlock.isInvalid() && !getLangOpts().ObjCAutoRefCount)
    BuildBlock = ImplicitCastExpr::Create(Context, BuildBlock.get()->getType(),
                                          CK_CopyAndAutoreleaseBlockObject,
                                          BuildBlock.get(), nullptr, VK_RValue);

  if (BuildBlock.isInvalid()) {
    Diag(CurrentLocation, diag::note_lambda_to_block_conv);
    Conv->setInvalidDecl();
    return;
  }

  // Create the return statement that returns the block from the conversion
  // function.
  StmtResult Return = BuildReturnStmt(Conv->getLocation(), BuildBlock.get());
  if (Return.isInvalid()) {
    Diag(CurrentLocation, diag::note_lambda_to_block_conv);
    Conv->setInvalidDecl();
    return;
  }

  // Set the body of the conversion function.
  Stmt *ReturnS = Return.get();
  Conv->setBody(CompoundStmt::Create(Context, ReturnS, Conv->getLocation(),
                                     Conv->getLocation()));
  Conv->markUsed(Context);

  // We're done; notify the mutation listener, if any.
  if (ASTMutationListener *L = getASTMutationListener()) {
    L->CompletedImplicitDefinition(Conv);
  }
}

/// Determine whether the given list arguments contains exactly one
/// "real" (non-default) argument.
static bool hasOneRealArgument(MultiExprArg Args) {
  switch (Args.size()) {
  case 0:
    return false;

  default:
    if (!Args[1]->isDefaultArgument())
      return false;

    LLVM_FALLTHROUGH;
  case 1:
    return !Args[0]->isDefaultArgument();
  }

  return false;
}

ExprResult
Sema::BuildCXXConstructExpr(SourceLocation ConstructLoc, QualType DeclInitType,
                            NamedDecl *FoundDecl,
                            CXXConstructorDecl *Constructor,
                            MultiExprArg ExprArgs,
                            bool HadMultipleCandidates,
                            bool IsListInitialization,
                            bool IsStdInitListInitialization,
                            bool RequiresZeroInit,
                            unsigned ConstructKind,
                            SourceRange ParenRange) {
  bool Elidable = false;

  // C++0x [class.copy]p34:
  //   When certain criteria are met, an implementation is allowed to
  //   omit the copy/move construction of a class object, even if the
  //   copy/move constructor and/or destructor for the object have
  //   side effects. [...]
  //     - when a temporary class object that has not been bound to a
  //       reference (12.2) would be copied/moved to a class object
  //       with the same cv-unqualified type, the copy/move operation
  //       can be omitted by constructing the temporary object
  //       directly into the target of the omitted copy/move
  if (ConstructKind == CXXConstructExpr::CK_Complete && Constructor &&
      Constructor->isCopyOrMoveConstructor() && hasOneRealArgument(ExprArgs)) {
    Expr *SubExpr = ExprArgs[0];
    Elidable = SubExpr->isTemporaryObject(
        Context, cast<CXXRecordDecl>(FoundDecl->getDeclContext()));
  }

  return BuildCXXConstructExpr(ConstructLoc, DeclInitType,
                               FoundDecl, Constructor,
                               Elidable, ExprArgs, HadMultipleCandidates,
                               IsListInitialization,
                               IsStdInitListInitialization, RequiresZeroInit,
                               ConstructKind, ParenRange);
}

ExprResult
Sema::BuildCXXConstructExpr(SourceLocation ConstructLoc, QualType DeclInitType,
                            NamedDecl *FoundDecl,
                            CXXConstructorDecl *Constructor,
                            bool Elidable,
                            MultiExprArg ExprArgs,
                            bool HadMultipleCandidates,
                            bool IsListInitialization,
                            bool IsStdInitListInitialization,
                            bool RequiresZeroInit,
                            unsigned ConstructKind,
                            SourceRange ParenRange) {
  if (auto *Shadow = dyn_cast<ConstructorUsingShadowDecl>(FoundDecl)) {
    Constructor = findInheritingConstructor(ConstructLoc, Constructor, Shadow);
    if (DiagnoseUseOfDecl(Constructor, ConstructLoc))
      return ExprError();
  }

  return BuildCXXConstructExpr(
      ConstructLoc, DeclInitType, Constructor, Elidable, ExprArgs,
      HadMultipleCandidates, IsListInitialization, IsStdInitListInitialization,
      RequiresZeroInit, ConstructKind, ParenRange);
}

/// BuildCXXConstructExpr - Creates a complete call to a constructor,
/// including handling of its default argument expressions.
ExprResult
Sema::BuildCXXConstructExpr(SourceLocation ConstructLoc, QualType DeclInitType,
                            CXXConstructorDecl *Constructor,
                            bool Elidable,
                            MultiExprArg ExprArgs,
                            bool HadMultipleCandidates,
                            bool IsListInitialization,
                            bool IsStdInitListInitialization,
                            bool RequiresZeroInit,
                            unsigned ConstructKind,
                            SourceRange ParenRange) {
  assert(declaresSameEntity(
             Constructor->getParent(),
             DeclInitType->getBaseElementTypeUnsafe()->getAsCXXRecordDecl()) &&
         "given constructor for wrong type");
  MarkFunctionReferenced(ConstructLoc, Constructor);
  if (getLangOpts().CUDA && !CheckCUDACall(ConstructLoc, Constructor))
    return ExprError();

  return CXXConstructExpr::Create(
      Context, DeclInitType, ConstructLoc, Constructor, Elidable,
      ExprArgs, HadMultipleCandidates, IsListInitialization,
      IsStdInitListInitialization, RequiresZeroInit,
      static_cast<CXXConstructExpr::ConstructionKind>(ConstructKind),
      ParenRange);
}

ExprResult Sema::BuildCXXDefaultInitExpr(SourceLocation Loc, FieldDecl *Field) {
  assert(Field->hasInClassInitializer());

  // If we already have the in-class initializer nothing needs to be done.
  if (Field->getInClassInitializer())
    return CXXDefaultInitExpr::Create(Context, Loc, Field);

  // If we might have already tried and failed to instantiate, don't try again.
  if (Field->isInvalidDecl())
    return ExprError();

  // Maybe we haven't instantiated the in-class initializer. Go check the
  // pattern FieldDecl to see if it has one.
  CXXRecordDecl *ParentRD = cast<CXXRecordDecl>(Field->getParent());

  if (isTemplateInstantiation(ParentRD->getTemplateSpecializationKind())) {
    CXXRecordDecl *ClassPattern = ParentRD->getTemplateInstantiationPattern();
    DeclContext::lookup_result Lookup =
        ClassPattern->lookup(Field->getDeclName());

    // Lookup can return at most two results: the pattern for the field, or the
    // injected class name of the parent record. No other member can have the
    // same name as the field.
    // In modules mode, lookup can return multiple results (coming from
    // different modules).
    assert((getLangOpts().Modules || (!Lookup.empty() && Lookup.size() <= 2)) &&
           "more than two lookup results for field name");
    FieldDecl *Pattern = dyn_cast<FieldDecl>(Lookup[0]);
    if (!Pattern) {
      assert(isa<CXXRecordDecl>(Lookup[0]) &&
             "cannot have other non-field member with same name");
      for (auto L : Lookup)
        if (isa<FieldDecl>(L)) {
          Pattern = cast<FieldDecl>(L);
          break;
        }
      assert(Pattern && "We must have set the Pattern!");
    }

    if (!Pattern->hasInClassInitializer() ||
        InstantiateInClassInitializer(Loc, Field, Pattern,
                                      getTemplateInstantiationArgs(Field))) {
      // Don't diagnose this again.
      Field->setInvalidDecl();
      return ExprError();
    }
    return CXXDefaultInitExpr::Create(Context, Loc, Field);
  }

  // DR1351:
  //   If the brace-or-equal-initializer of a non-static data member
  //   invokes a defaulted default constructor of its class or of an
  //   enclosing class in a potentially evaluated subexpression, the
  //   program is ill-formed.
  //
  // This resolution is unworkable: the exception specification of the
  // default constructor can be needed in an unevaluated context, in
  // particular, in the operand of a noexcept-expression, and we can be
  // unable to compute an exception specification for an enclosed class.
  //
  // Any attempt to resolve the exception specification of a defaulted default
  // constructor before the initializer is lexically complete will ultimately
  // come here at which point we can diagnose it.
  RecordDecl *OutermostClass = ParentRD->getOuterLexicalRecordContext();
  Diag(Loc, diag::err_in_class_initializer_not_yet_parsed)
      << OutermostClass << Field;
  Diag(Field->getEndLoc(), diag::note_in_class_initializer_not_yet_parsed);
  // Recover by marking the field invalid, unless we're in a SFINAE context.
  if (!isSFINAEContext())
    Field->setInvalidDecl();
  return ExprError();
}

void Sema::FinalizeVarWithDestructor(VarDecl *VD, const RecordType *Record) {
  if (VD->isInvalidDecl()) return;

  CXXRecordDecl *ClassDecl = cast<CXXRecordDecl>(Record->getDecl());
  if (ClassDecl->isInvalidDecl()) return;
  if (ClassDecl->hasIrrelevantDestructor()) return;
  if (ClassDecl->isDependentContext()) return;

  if (VD->isNoDestroy(getASTContext()))
    return;

  CXXDestructorDecl *Destructor = LookupDestructor(ClassDecl);
  MarkFunctionReferenced(VD->getLocation(), Destructor);
  CheckDestructorAccess(VD->getLocation(), Destructor,
                        PDiag(diag::err_access_dtor_var)
                        << VD->getDeclName()
                        << VD->getType());
  DiagnoseUseOfDecl(Destructor, VD->getLocation());

  if (Destructor->isTrivial()) return;
  if (!VD->hasGlobalStorage()) return;

  // Emit warning for non-trivial dtor in global scope (a real global,
  // class-static, function-static).
  Diag(VD->getLocation(), diag::warn_exit_time_destructor);

  // TODO: this should be re-enabled for static locals by !CXAAtExit
  if (!VD->isStaticLocal())
    Diag(VD->getLocation(), diag::warn_global_destructor);
}

/// Given a constructor and the set of arguments provided for the
/// constructor, convert the arguments and add any required default arguments
/// to form a proper call to this constructor.
///
/// \returns true if an error occurred, false otherwise.
bool
Sema::CompleteConstructorCall(CXXConstructorDecl *Constructor,
                              MultiExprArg ArgsPtr,
                              SourceLocation Loc,
                              SmallVectorImpl<Expr*> &ConvertedArgs,
                              bool AllowExplicit,
                              bool IsListInitialization) {
  // FIXME: This duplicates a lot of code from Sema::ConvertArgumentsForCall.
  unsigned NumArgs = ArgsPtr.size();
  Expr **Args = ArgsPtr.data();

  const FunctionProtoType *Proto
    = Constructor->getType()->getAs<FunctionProtoType>();
  assert(Proto && "Constructor without a prototype?");
  unsigned NumParams = Proto->getNumParams();

  // If too few arguments are available, we'll fill in the rest with defaults.
  if (NumArgs < NumParams)
    ConvertedArgs.reserve(NumParams);
  else
    ConvertedArgs.reserve(NumArgs);

  VariadicCallType CallType =
    Proto->isVariadic() ? VariadicConstructor : VariadicDoesNotApply;
  SmallVector<Expr *, 8> AllArgs;
  bool Invalid = GatherArgumentsForCall(Loc, Constructor,
                                        Proto, 0,
                                        llvm::makeArrayRef(Args, NumArgs),
                                        AllArgs,
                                        CallType, AllowExplicit,
                                        IsListInitialization);
  ConvertedArgs.append(AllArgs.begin(), AllArgs.end());

  DiagnoseSentinelCalls(Constructor, Loc, AllArgs);

  CheckConstructorCall(Constructor,
                       llvm::makeArrayRef(AllArgs.data(), AllArgs.size()),
                       Proto, Loc);

  return Invalid;
}

static inline bool
CheckOperatorNewDeleteDeclarationScope(Sema &SemaRef,
                                       const FunctionDecl *FnDecl) {
  const DeclContext *DC = FnDecl->getDeclContext()->getRedeclContext();
  if (isa<NamespaceDecl>(DC)) {
    return SemaRef.Diag(FnDecl->getLocation(),
                        diag::err_operator_new_delete_declared_in_namespace)
      << FnDecl->getDeclName();
  }

  if (isa<TranslationUnitDecl>(DC) &&
      FnDecl->getStorageClass() == SC_Static) {
    return SemaRef.Diag(FnDecl->getLocation(),
                        diag::err_operator_new_delete_declared_static)
      << FnDecl->getDeclName();
  }

  return false;
}

static QualType
RemoveAddressSpaceFromPtr(Sema &SemaRef, const PointerType *PtrTy) {
  QualType QTy = PtrTy->getPointeeType();
  QTy = SemaRef.Context.removeAddrSpaceQualType(QTy);
  return SemaRef.Context.getPointerType(QTy);
}

static inline bool
CheckOperatorNewDeleteTypes(Sema &SemaRef, const FunctionDecl *FnDecl,
                            CanQualType ExpectedResultType,
                            CanQualType ExpectedFirstParamType,
                            unsigned DependentParamTypeDiag,
                            unsigned InvalidParamTypeDiag) {
  QualType ResultType =
      FnDecl->getType()->getAs<FunctionType>()->getReturnType();

  // Check that the result type is not dependent.
  if (ResultType->isDependentType())
    return SemaRef.Diag(FnDecl->getLocation(),
                        diag::err_operator_new_delete_dependent_result_type)
    << FnDecl->getDeclName() << ExpectedResultType;

  // OpenCL C++: the operator is valid on any address space.
  if (SemaRef.getLangOpts().OpenCLCPlusPlus) {
    if (auto *PtrTy = ResultType->getAs<PointerType>()) {
      ResultType = RemoveAddressSpaceFromPtr(SemaRef, PtrTy);
    }
  }

  // Check that the result type is what we expect.
  if (SemaRef.Context.getCanonicalType(ResultType) != ExpectedResultType)
    return SemaRef.Diag(FnDecl->getLocation(),
                        diag::err_operator_new_delete_invalid_result_type)
    << FnDecl->getDeclName() << ExpectedResultType;

  // A function template must have at least 2 parameters.
  if (FnDecl->getDescribedFunctionTemplate() && FnDecl->getNumParams() < 2)
    return SemaRef.Diag(FnDecl->getLocation(),
                      diag::err_operator_new_delete_template_too_few_parameters)
        << FnDecl->getDeclName();

  // The function decl must have at least 1 parameter.
  if (FnDecl->getNumParams() == 0)
    return SemaRef.Diag(FnDecl->getLocation(),
                        diag::err_operator_new_delete_too_few_parameters)
      << FnDecl->getDeclName();

  // Check the first parameter type is not dependent.
  QualType FirstParamType = FnDecl->getParamDecl(0)->getType();
  if (FirstParamType->isDependentType())
    return SemaRef.Diag(FnDecl->getLocation(), DependentParamTypeDiag)
      << FnDecl->getDeclName() << ExpectedFirstParamType;

  // Check that the first parameter type is what we expect.
  if (SemaRef.getLangOpts().OpenCLCPlusPlus) {
    // OpenCL C++: the operator is valid on any address space.
    if (auto *PtrTy =
            FnDecl->getParamDecl(0)->getType()->getAs<PointerType>()) {
      FirstParamType = RemoveAddressSpaceFromPtr(SemaRef, PtrTy);
    }
  }
  if (SemaRef.Context.getCanonicalType(FirstParamType).getUnqualifiedType() !=
      ExpectedFirstParamType)
    return SemaRef.Diag(FnDecl->getLocation(), InvalidParamTypeDiag)
    << FnDecl->getDeclName() << ExpectedFirstParamType;

  return false;
}

static bool
CheckOperatorNewDeclaration(Sema &SemaRef, const FunctionDecl *FnDecl) {
  // C++ [basic.stc.dynamic.allocation]p1:
  //   A program is ill-formed if an allocation function is declared in a
  //   namespace scope other than global scope or declared static in global
  //   scope.
  if (CheckOperatorNewDeleteDeclarationScope(SemaRef, FnDecl))
    return true;

  CanQualType SizeTy =
    SemaRef.Context.getCanonicalType(SemaRef.Context.getSizeType());

  // C++ [basic.stc.dynamic.allocation]p1:
  //  The return type shall be void*. The first parameter shall have type
  //  std::size_t.
  if (CheckOperatorNewDeleteTypes(SemaRef, FnDecl, SemaRef.Context.VoidPtrTy,
                                  SizeTy,
                                  diag::err_operator_new_dependent_param_type,
                                  diag::err_operator_new_param_type))
    return true;

  // C++ [basic.stc.dynamic.allocation]p1:
  //  The first parameter shall not have an associated default argument.
  if (FnDecl->getParamDecl(0)->hasDefaultArg())
    return SemaRef.Diag(FnDecl->getLocation(),
                        diag::err_operator_new_default_arg)
      << FnDecl->getDeclName() << FnDecl->getParamDecl(0)->getDefaultArgRange();

  return false;
}

static bool
CheckOperatorDeleteDeclaration(Sema &SemaRef, FunctionDecl *FnDecl) {
  // C++ [basic.stc.dynamic.deallocation]p1:
  //   A program is ill-formed if deallocation functions are declared in a
  //   namespace scope other than global scope or declared static in global
  //   scope.
  if (CheckOperatorNewDeleteDeclarationScope(SemaRef, FnDecl))
    return true;

  auto *MD = dyn_cast<CXXMethodDecl>(FnDecl);

  // C++ P0722:
  //   Within a class C, the first parameter of a destroying operator delete
  //   shall be of type C *. The first parameter of any other deallocation
  //   function shall be of type void *.
  CanQualType ExpectedFirstParamType =
      MD && MD->isDestroyingOperatorDelete()
          ? SemaRef.Context.getCanonicalType(SemaRef.Context.getPointerType(
                SemaRef.Context.getRecordType(MD->getParent())))
          : SemaRef.Context.VoidPtrTy;

  // C++ [basic.stc.dynamic.deallocation]p2:
  //   Each deallocation function shall return void
  if (CheckOperatorNewDeleteTypes(
          SemaRef, FnDecl, SemaRef.Context.VoidTy, ExpectedFirstParamType,
          diag::err_operator_delete_dependent_param_type,
          diag::err_operator_delete_param_type))
    return true;

  // C++ P0722:
  //   A destroying operator delete shall be a usual deallocation function.
  if (MD && !MD->getParent()->isDependentContext() &&
      MD->isDestroyingOperatorDelete() &&
      !SemaRef.isUsualDeallocationFunction(MD)) {
    SemaRef.Diag(MD->getLocation(),
                 diag::err_destroying_operator_delete_not_usual);
    return true;
  }

  return false;
}

/// CheckOverloadedOperatorDeclaration - Check whether the declaration
/// of this overloaded operator is well-formed. If so, returns false;
/// otherwise, emits appropriate diagnostics and returns true.
bool Sema::CheckOverloadedOperatorDeclaration(FunctionDecl *FnDecl) {
  assert(FnDecl && FnDecl->isOverloadedOperator() &&
         "Expected an overloaded operator declaration");

  OverloadedOperatorKind Op = FnDecl->getOverloadedOperator();

  // C++ [over.oper]p5:
  //   The allocation and deallocation functions, operator new,
  //   operator new[], operator delete and operator delete[], are
  //   described completely in 3.7.3. The attributes and restrictions
  //   found in the rest of this subclause do not apply to them unless
  //   explicitly stated in 3.7.3.
  if (Op == OO_Delete || Op == OO_Array_Delete)
    return CheckOperatorDeleteDeclaration(*this, FnDecl);

  if (Op == OO_New || Op == OO_Array_New)
    return CheckOperatorNewDeclaration(*this, FnDecl);

  // C++ [over.oper]p6:
  //   An operator function shall either be a non-static member
  //   function or be a non-member function and have at least one
  //   parameter whose type is a class, a reference to a class, an
  //   enumeration, or a reference to an enumeration.
  if (CXXMethodDecl *MethodDecl = dyn_cast<CXXMethodDecl>(FnDecl)) {
    if (MethodDecl->isStatic())
      return Diag(FnDecl->getLocation(),
                  diag::err_operator_overload_static) << FnDecl->getDeclName();
  } else {
    bool ClassOrEnumParam = false;
    for (auto Param : FnDecl->parameters()) {
      QualType ParamType = Param->getType().getNonReferenceType();
      if (ParamType->isDependentType() || ParamType->isRecordType() ||
          ParamType->isEnumeralType()) {
        ClassOrEnumParam = true;
        break;
      }
    }

    if (!ClassOrEnumParam)
      return Diag(FnDecl->getLocation(),
                  diag::err_operator_overload_needs_class_or_enum)
        << FnDecl->getDeclName();
  }

  // C++ [over.oper]p8:
  //   An operator function cannot have default arguments (8.3.6),
  //   except where explicitly stated below.
  //
  // Only the function-call operator allows default arguments
  // (C++ [over.call]p1).
  if (Op != OO_Call) {
    for (auto Param : FnDecl->parameters()) {
      if (Param->hasDefaultArg())
        return Diag(Param->getLocation(),
                    diag::err_operator_overload_default_arg)
          << FnDecl->getDeclName() << Param->getDefaultArgRange();
    }
  }

  static const bool OperatorUses[NUM_OVERLOADED_OPERATORS][3] = {
    { false, false, false }
#define OVERLOADED_OPERATOR(Name,Spelling,Token,Unary,Binary,MemberOnly) \
    , { Unary, Binary, MemberOnly }
#include "clang/Basic/OperatorKinds.def"
  };

  bool CanBeUnaryOperator = OperatorUses[Op][0];
  bool CanBeBinaryOperator = OperatorUses[Op][1];
  bool MustBeMemberOperator = OperatorUses[Op][2];

  // C++ [over.oper]p8:
  //   [...] Operator functions cannot have more or fewer parameters
  //   than the number required for the corresponding operator, as
  //   described in the rest of this subclause.
  unsigned NumParams = FnDecl->getNumParams()
                     + (isa<CXXMethodDecl>(FnDecl)? 1 : 0);
  if (Op != OO_Call &&
      ((NumParams == 1 && !CanBeUnaryOperator) ||
       (NumParams == 2 && !CanBeBinaryOperator) ||
       (NumParams < 1) || (NumParams > 2))) {
    // We have the wrong number of parameters.
    unsigned ErrorKind;
    if (CanBeUnaryOperator && CanBeBinaryOperator) {
      ErrorKind = 2;  // 2 -> unary or binary.
    } else if (CanBeUnaryOperator) {
      ErrorKind = 0;  // 0 -> unary
    } else {
      assert(CanBeBinaryOperator &&
             "All non-call overloaded operators are unary or binary!");
      ErrorKind = 1;  // 1 -> binary
    }

    return Diag(FnDecl->getLocation(), diag::err_operator_overload_must_be)
      << FnDecl->getDeclName() << NumParams << ErrorKind;
  }

  // Overloaded operators other than operator() cannot be variadic.
  if (Op != OO_Call &&
      FnDecl->getType()->getAs<FunctionProtoType>()->isVariadic()) {
    return Diag(FnDecl->getLocation(), diag::err_operator_overload_variadic)
      << FnDecl->getDeclName();
  }

  // Some operators must be non-static member functions.
  if (MustBeMemberOperator && !isa<CXXMethodDecl>(FnDecl)) {
    return Diag(FnDecl->getLocation(),
                diag::err_operator_overload_must_be_member)
      << FnDecl->getDeclName();
  }

  // C++ [over.inc]p1:
  //   The user-defined function called operator++ implements the
  //   prefix and postfix ++ operator. If this function is a member
  //   function with no parameters, or a non-member function with one
  //   parameter of class or enumeration type, it defines the prefix
  //   increment operator ++ for objects of that type. If the function
  //   is a member function with one parameter (which shall be of type
  //   int) or a non-member function with two parameters (the second
  //   of which shall be of type int), it defines the postfix
  //   increment operator ++ for objects of that type.
  if ((Op == OO_PlusPlus || Op == OO_MinusMinus) && NumParams == 2) {
    ParmVarDecl *LastParam = FnDecl->getParamDecl(FnDecl->getNumParams() - 1);
    QualType ParamType = LastParam->getType();

    if (!ParamType->isSpecificBuiltinType(BuiltinType::Int) &&
        !ParamType->isDependentType())
      return Diag(LastParam->getLocation(),
                  diag::err_operator_overload_post_incdec_must_be_int)
        << LastParam->getType() << (Op == OO_MinusMinus);
  }

  return false;
}

static bool
checkLiteralOperatorTemplateParameterList(Sema &SemaRef,
                                          FunctionTemplateDecl *TpDecl) {
  TemplateParameterList *TemplateParams = TpDecl->getTemplateParameters();

  // Must have one or two template parameters.
  if (TemplateParams->size() == 1) {
    NonTypeTemplateParmDecl *PmDecl =
        dyn_cast<NonTypeTemplateParmDecl>(TemplateParams->getParam(0));

    // The template parameter must be a char parameter pack.
    if (PmDecl && PmDecl->isTemplateParameterPack() &&
        SemaRef.Context.hasSameType(PmDecl->getType(), SemaRef.Context.CharTy))
      return false;

  } else if (TemplateParams->size() == 2) {
    TemplateTypeParmDecl *PmType =
        dyn_cast<TemplateTypeParmDecl>(TemplateParams->getParam(0));
    NonTypeTemplateParmDecl *PmArgs =
        dyn_cast<NonTypeTemplateParmDecl>(TemplateParams->getParam(1));

    // The second template parameter must be a parameter pack with the
    // first template parameter as its type.
    if (PmType && PmArgs && !PmType->isTemplateParameterPack() &&
        PmArgs->isTemplateParameterPack()) {
      const TemplateTypeParmType *TArgs =
          PmArgs->getType()->getAs<TemplateTypeParmType>();
      if (TArgs && TArgs->getDepth() == PmType->getDepth() &&
          TArgs->getIndex() == PmType->getIndex()) {
        if (!SemaRef.inTemplateInstantiation())
          SemaRef.Diag(TpDecl->getLocation(),
                       diag::ext_string_literal_operator_template);
        return false;
      }
    }
  }

  SemaRef.Diag(TpDecl->getTemplateParameters()->getSourceRange().getBegin(),
               diag::err_literal_operator_template)
      << TpDecl->getTemplateParameters()->getSourceRange();
  return true;
}

/// CheckLiteralOperatorDeclaration - Check whether the declaration
/// of this literal operator function is well-formed. If so, returns
/// false; otherwise, emits appropriate diagnostics and returns true.
bool Sema::CheckLiteralOperatorDeclaration(FunctionDecl *FnDecl) {
  if (isa<CXXMethodDecl>(FnDecl)) {
    Diag(FnDecl->getLocation(), diag::err_literal_operator_outside_namespace)
      << FnDecl->getDeclName();
    return true;
  }

  if (FnDecl->isExternC()) {
    Diag(FnDecl->getLocation(), diag::err_literal_operator_extern_c);
    if (const LinkageSpecDecl *LSD =
            FnDecl->getDeclContext()->getExternCContext())
      Diag(LSD->getExternLoc(), diag::note_extern_c_begins_here);
    return true;
  }

  // This might be the definition of a literal operator template.
  FunctionTemplateDecl *TpDecl = FnDecl->getDescribedFunctionTemplate();

  // This might be a specialization of a literal operator template.
  if (!TpDecl)
    TpDecl = FnDecl->getPrimaryTemplate();

  // template <char...> type operator "" name() and
  // template <class T, T...> type operator "" name() are the only valid
  // template signatures, and the only valid signatures with no parameters.
  if (TpDecl) {
    if (FnDecl->param_size() != 0) {
      Diag(FnDecl->getLocation(),
           diag::err_literal_operator_template_with_params);
      return true;
    }

    if (checkLiteralOperatorTemplateParameterList(*this, TpDecl))
      return true;

  } else if (FnDecl->param_size() == 1) {
    const ParmVarDecl *Param = FnDecl->getParamDecl(0);

    QualType ParamType = Param->getType().getUnqualifiedType();

    // Only unsigned long long int, long double, any character type, and const
    // char * are allowed as the only parameters.
    if (ParamType->isSpecificBuiltinType(BuiltinType::ULongLong) ||
        ParamType->isSpecificBuiltinType(BuiltinType::LongDouble) ||
        Context.hasSameType(ParamType, Context.CharTy) ||
        Context.hasSameType(ParamType, Context.WideCharTy) ||
        Context.hasSameType(ParamType, Context.Char8Ty) ||
        Context.hasSameType(ParamType, Context.Char16Ty) ||
        Context.hasSameType(ParamType, Context.Char32Ty)) {
    } else if (const PointerType *Ptr = ParamType->getAs<PointerType>()) {
      QualType InnerType = Ptr->getPointeeType();

      // Pointer parameter must be a const char *.
      if (!(Context.hasSameType(InnerType.getUnqualifiedType(),
                                Context.CharTy) &&
            InnerType.isConstQualified() && !InnerType.isVolatileQualified())) {
        Diag(Param->getSourceRange().getBegin(),
             diag::err_literal_operator_param)
            << ParamType << "'const char *'" << Param->getSourceRange();
        return true;
      }

    } else if (ParamType->isRealFloatingType()) {
      Diag(Param->getSourceRange().getBegin(), diag::err_literal_operator_param)
          << ParamType << Context.LongDoubleTy << Param->getSourceRange();
      return true;

    } else if (ParamType->isIntegerType()) {
      Diag(Param->getSourceRange().getBegin(), diag::err_literal_operator_param)
          << ParamType << Context.UnsignedLongLongTy << Param->getSourceRange();
      return true;

    } else {
      Diag(Param->getSourceRange().getBegin(),
           diag::err_literal_operator_invalid_param)
          << ParamType << Param->getSourceRange();
      return true;
    }

  } else if (FnDecl->param_size() == 2) {
    FunctionDecl::param_iterator Param = FnDecl->param_begin();

    // First, verify that the first parameter is correct.

    QualType FirstParamType = (*Param)->getType().getUnqualifiedType();

    // Two parameter function must have a pointer to const as a
    // first parameter; let's strip those qualifiers.
    const PointerType *PT = FirstParamType->getAs<PointerType>();

    if (!PT) {
      Diag((*Param)->getSourceRange().getBegin(),
           diag::err_literal_operator_param)
          << FirstParamType << "'const char *'" << (*Param)->getSourceRange();
      return true;
    }

    QualType PointeeType = PT->getPointeeType();
    // First parameter must be const
    if (!PointeeType.isConstQualified() || PointeeType.isVolatileQualified()) {
      Diag((*Param)->getSourceRange().getBegin(),
           diag::err_literal_operator_param)
          << FirstParamType << "'const char *'" << (*Param)->getSourceRange();
      return true;
    }

    QualType InnerType = PointeeType.getUnqualifiedType();
    // Only const char *, const wchar_t*, const char8_t*, const char16_t*, and
    // const char32_t* are allowed as the first parameter to a two-parameter
    // function
    if (!(Context.hasSameType(InnerType, Context.CharTy) ||
          Context.hasSameType(InnerType, Context.WideCharTy) ||
          Context.hasSameType(InnerType, Context.Char8Ty) ||
          Context.hasSameType(InnerType, Context.Char16Ty) ||
          Context.hasSameType(InnerType, Context.Char32Ty))) {
      Diag((*Param)->getSourceRange().getBegin(),
           diag::err_literal_operator_param)
          << FirstParamType << "'const char *'" << (*Param)->getSourceRange();
      return true;
    }

    // Move on to the second and final parameter.
    ++Param;

    // The second parameter must be a std::size_t.
    QualType SecondParamType = (*Param)->getType().getUnqualifiedType();
    if (!Context.hasSameType(SecondParamType, Context.getSizeType())) {
      Diag((*Param)->getSourceRange().getBegin(),
           diag::err_literal_operator_param)
          << SecondParamType << Context.getSizeType()
          << (*Param)->getSourceRange();
      return true;
    }
  } else {
    Diag(FnDecl->getLocation(), diag::err_literal_operator_bad_param_count);
    return true;
  }

  // Parameters are good.

  // A parameter-declaration-clause containing a default argument is not
  // equivalent to any of the permitted forms.
  for (auto Param : FnDecl->parameters()) {
    if (Param->hasDefaultArg()) {
      Diag(Param->getDefaultArgRange().getBegin(),
           diag::err_literal_operator_default_argument)
        << Param->getDefaultArgRange();
      break;
    }
  }

  StringRef LiteralName
    = FnDecl->getDeclName().getCXXLiteralIdentifier()->getName();
  if (LiteralName[0] != '_' &&
      !getSourceManager().isInSystemHeader(FnDecl->getLocation())) {
    // C++11 [usrlit.suffix]p1:
    //   Literal suffix identifiers that do not start with an underscore
    //   are reserved for future standardization.
    Diag(FnDecl->getLocation(), diag::warn_user_literal_reserved)
      << StringLiteralParser::isValidUDSuffix(getLangOpts(), LiteralName);
  }

  return false;
}

/// ActOnStartLinkageSpecification - Parsed the beginning of a C++
/// linkage specification, including the language and (if present)
/// the '{'. ExternLoc is the location of the 'extern', Lang is the
/// language string literal. LBraceLoc, if valid, provides the location of
/// the '{' brace. Otherwise, this linkage specification does not
/// have any braces.
Decl *Sema::ActOnStartLinkageSpecification(Scope *S, SourceLocation ExternLoc,
                                           Expr *LangStr,
                                           SourceLocation LBraceLoc) {
  StringLiteral *Lit = cast<StringLiteral>(LangStr);
  if (!Lit->isAscii()) {
    Diag(LangStr->getExprLoc(), diag::err_language_linkage_spec_not_ascii)
      << LangStr->getSourceRange();
    return nullptr;
  }

  StringRef Lang = Lit->getString();
  LinkageSpecDecl::LanguageIDs Language;
  if (Lang == "C")
    Language = LinkageSpecDecl::lang_c;
  else if (Lang == "C++")
    Language = LinkageSpecDecl::lang_cxx;
  else {
    Diag(LangStr->getExprLoc(), diag::err_language_linkage_spec_unknown)
      << LangStr->getSourceRange();
    return nullptr;
  }

  // FIXME: Add all the various semantics of linkage specifications

  LinkageSpecDecl *D = LinkageSpecDecl::Create(Context, CurContext, ExternLoc,
                                               LangStr->getExprLoc(), Language,
                                               LBraceLoc.isValid());
  CurContext->addDecl(D);
  PushDeclContext(S, D);
  return D;
}

/// ActOnFinishLinkageSpecification - Complete the definition of
/// the C++ linkage specification LinkageSpec. If RBraceLoc is
/// valid, it's the position of the closing '}' brace in a linkage
/// specification that uses braces.
Decl *Sema::ActOnFinishLinkageSpecification(Scope *S,
                                            Decl *LinkageSpec,
                                            SourceLocation RBraceLoc) {
  if (RBraceLoc.isValid()) {
    LinkageSpecDecl* LSDecl = cast<LinkageSpecDecl>(LinkageSpec);
    LSDecl->setRBraceLoc(RBraceLoc);
  }
  PopDeclContext();
  return LinkageSpec;
}

Decl *Sema::ActOnEmptyDeclaration(Scope *S,
                                  const ParsedAttributesView &AttrList,
                                  SourceLocation SemiLoc) {
  Decl *ED = EmptyDecl::Create(Context, CurContext, SemiLoc);
  // Attribute declarations appertain to empty declaration so we handle
  // them here.
  ProcessDeclAttributeList(S, ED, AttrList);

  CurContext->addDecl(ED);
  return ED;
}

/// Perform semantic analysis for the variable declaration that
/// occurs within a C++ catch clause, returning the newly-created
/// variable.
VarDecl *Sema::BuildExceptionDeclaration(Scope *S,
                                         TypeSourceInfo *TInfo,
                                         SourceLocation StartLoc,
                                         SourceLocation Loc,
                                         IdentifierInfo *Name) {
  bool Invalid = false;
  QualType ExDeclType = TInfo->getType();

  // Arrays and functions decay.
  if (ExDeclType->isArrayType())
    ExDeclType = Context.getArrayDecayedType(ExDeclType);
  else if (ExDeclType->isFunctionType())
    ExDeclType = Context.getPointerType(ExDeclType);

  // C++ 15.3p1: The exception-declaration shall not denote an incomplete type.
  // The exception-declaration shall not denote a pointer or reference to an
  // incomplete type, other than [cv] void*.
  // N2844 forbids rvalue references.
  if (!ExDeclType->isDependentType() && ExDeclType->isRValueReferenceType()) {
    Diag(Loc, diag::err_catch_rvalue_ref);
    Invalid = true;
  }

  if (ExDeclType->isVariablyModifiedType()) {
    Diag(Loc, diag::err_catch_variably_modified) << ExDeclType;
    Invalid = true;
  }

  QualType BaseType = ExDeclType;
  int Mode = 0; // 0 for direct type, 1 for pointer, 2 for reference
  unsigned DK = diag::err_catch_incomplete;
  if (const PointerType *Ptr = BaseType->getAs<PointerType>()) {
    BaseType = Ptr->getPointeeType();
    Mode = 1;
    DK = diag::err_catch_incomplete_ptr;
  } else if (const ReferenceType *Ref = BaseType->getAs<ReferenceType>()) {
    // For the purpose of error recovery, we treat rvalue refs like lvalue refs.
    BaseType = Ref->getPointeeType();
    Mode = 2;
    DK = diag::err_catch_incomplete_ref;
  }
  if (!Invalid && (Mode == 0 || !BaseType->isVoidType()) &&
      !BaseType->isDependentType() && RequireCompleteType(Loc, BaseType, DK))
    Invalid = true;

  if (!Invalid && !ExDeclType->isDependentType() &&
      RequireNonAbstractType(Loc, ExDeclType,
                             diag::err_abstract_type_in_decl,
                             AbstractVariableType))
    Invalid = true;

  // Only the non-fragile NeXT runtime currently supports C++ catches
  // of ObjC types, and no runtime supports catching ObjC types by value.
  if (!Invalid && getLangOpts().ObjC) {
    QualType T = ExDeclType;
    if (const ReferenceType *RT = T->getAs<ReferenceType>())
      T = RT->getPointeeType();

    if (T->isObjCObjectType()) {
      Diag(Loc, diag::err_objc_object_catch);
      Invalid = true;
    } else if (T->isObjCObjectPointerType()) {
      // FIXME: should this be a test for macosx-fragile specifically?
      if (getLangOpts().ObjCRuntime.isFragile())
        Diag(Loc, diag::warn_objc_pointer_cxx_catch_fragile);
    }
  }

  VarDecl *ExDecl = VarDecl::Create(Context, CurContext, StartLoc, Loc, Name,
                                    ExDeclType, TInfo, SC_None);
  ExDecl->setExceptionVariable(true);

  // In ARC, infer 'retaining' for variables of retainable type.
  if (getLangOpts().ObjCAutoRefCount && inferObjCARCLifetime(ExDecl))
    Invalid = true;

  if (!Invalid && !ExDeclType->isDependentType()) {
    if (const RecordType *recordType = ExDeclType->getAs<RecordType>()) {
      // Insulate this from anything else we might currently be parsing.
      EnterExpressionEvaluationContext scope(
          *this, ExpressionEvaluationContext::PotentiallyEvaluated);

      // C++ [except.handle]p16:
      //   The object declared in an exception-declaration or, if the
      //   exception-declaration does not specify a name, a temporary (12.2) is
      //   copy-initialized (8.5) from the exception object. [...]
      //   The object is destroyed when the handler exits, after the destruction
      //   of any automatic objects initialized within the handler.
      //
      // We just pretend to initialize the object with itself, then make sure
      // it can be destroyed later.
      QualType initType = Context.getExceptionObjectType(ExDeclType);

      InitializedEntity entity =
        InitializedEntity::InitializeVariable(ExDecl);
      InitializationKind initKind =
        InitializationKind::CreateCopy(Loc, SourceLocation());

      Expr *opaqueValue =
        new (Context) OpaqueValueExpr(Loc, initType, VK_LValue, OK_Ordinary);
      InitializationSequence sequence(*this, entity, initKind, opaqueValue);
      ExprResult result = sequence.Perform(*this, entity, initKind, opaqueValue);
      if (result.isInvalid())
        Invalid = true;
      else {
        // If the constructor used was non-trivial, set this as the
        // "initializer".
        CXXConstructExpr *construct = result.getAs<CXXConstructExpr>();
        if (!construct->getConstructor()->isTrivial()) {
          Expr *init = MaybeCreateExprWithCleanups(construct);
          ExDecl->setInit(init);
        }

        // And make sure it's destructable.
        FinalizeVarWithDestructor(ExDecl, recordType);
      }
    }
  }

  if (Invalid)
    ExDecl->setInvalidDecl();

  return ExDecl;
}

/// ActOnExceptionDeclarator - Parsed the exception-declarator in a C++ catch
/// handler.
Decl *Sema::ActOnExceptionDeclarator(Scope *S, Declarator &D) {
  TypeSourceInfo *TInfo = GetTypeForDeclarator(D, S);
  bool Invalid = D.isInvalidType();

  // Check for unexpanded parameter packs.
  if (DiagnoseUnexpandedParameterPack(D.getIdentifierLoc(), TInfo,
                                      UPPC_ExceptionType)) {
    TInfo = Context.getTrivialTypeSourceInfo(Context.IntTy,
                                             D.getIdentifierLoc());
    Invalid = true;
  }

  IdentifierInfo *II = D.getIdentifier();
  if (NamedDecl *PrevDecl = LookupSingleName(S, II, D.getIdentifierLoc(),
                                             LookupOrdinaryName,
                                             ForVisibleRedeclaration)) {
    // The scope should be freshly made just for us. There is just no way
    // it contains any previous declaration, except for function parameters in
    // a function-try-block's catch statement.
    assert(!S->isDeclScope(PrevDecl));
    if (isDeclInScope(PrevDecl, CurContext, S)) {
      Diag(D.getIdentifierLoc(), diag::err_redefinition)
        << D.getIdentifier();
      Diag(PrevDecl->getLocation(), diag::note_previous_definition);
      Invalid = true;
    } else if (PrevDecl->isTemplateParameter())
      // Maybe we will complain about the shadowed template parameter.
      DiagnoseTemplateParameterShadow(D.getIdentifierLoc(), PrevDecl);
  }

  if (D.getCXXScopeSpec().isSet() && !Invalid) {
    Diag(D.getIdentifierLoc(), diag::err_qualified_catch_declarator)
      << D.getCXXScopeSpec().getRange();
    Invalid = true;
  }

  VarDecl *ExDecl = BuildExceptionDeclaration(
      S, TInfo, D.getBeginLoc(), D.getIdentifierLoc(), D.getIdentifier());
  if (Invalid)
    ExDecl->setInvalidDecl();

  // Add the exception declaration into this scope.
  if (II)
    PushOnScopeChains(ExDecl, S);
  else
    CurContext->addDecl(ExDecl);

  ProcessDeclAttributes(S, ExDecl, D);
  return ExDecl;
}

Decl *Sema::ActOnStaticAssertDeclaration(SourceLocation StaticAssertLoc,
                                         Expr *AssertExpr,
                                         Expr *AssertMessageExpr,
                                         SourceLocation RParenLoc) {
  StringLiteral *AssertMessage =
      AssertMessageExpr ? cast<StringLiteral>(AssertMessageExpr) : nullptr;

  if (DiagnoseUnexpandedParameterPack(AssertExpr, UPPC_StaticAssertExpression))
    return nullptr;

  return BuildStaticAssertDeclaration(StaticAssertLoc, AssertExpr,
                                      AssertMessage, RParenLoc, false);
}

Decl *Sema::BuildStaticAssertDeclaration(SourceLocation StaticAssertLoc,
                                         Expr *AssertExpr,
                                         StringLiteral *AssertMessage,
                                         SourceLocation RParenLoc,
                                         bool Failed) {
  assert(AssertExpr != nullptr && "Expected non-null condition");
  if (!AssertExpr->isTypeDependent() && !AssertExpr->isValueDependent() &&
      !Failed) {
    // In a static_assert-declaration, the constant-expression shall be a
    // constant expression that can be contextually converted to bool.
    ExprResult Converted = PerformContextuallyConvertToBool(AssertExpr);
    if (Converted.isInvalid())
      Failed = true;
    else
      Converted = ConstantExpr::Create(Context, Converted.get());

    llvm::APSInt Cond;
    if (!Failed && VerifyIntegerConstantExpression(Converted.get(), &Cond,
          diag::err_static_assert_expression_is_not_constant,
          /*AllowFold=*/false).isInvalid())
      Failed = true;

    if (!Failed && !Cond) {
      SmallString<256> MsgBuffer;
      llvm::raw_svector_ostream Msg(MsgBuffer);
      if (AssertMessage)
        AssertMessage->printPretty(Msg, nullptr, getPrintingPolicy());

      Expr *InnerCond = nullptr;
      std::string InnerCondDescription;
      std::tie(InnerCond, InnerCondDescription) =
        findFailedBooleanCondition(Converted.get());
      if (InnerCond && !isa<CXXBoolLiteralExpr>(InnerCond)
                    && !isa<IntegerLiteral>(InnerCond)) {
        Diag(StaticAssertLoc, diag::err_static_assert_requirement_failed)
          << InnerCondDescription << !AssertMessage
          << Msg.str() << InnerCond->getSourceRange();
      } else {
        Diag(StaticAssertLoc, diag::err_static_assert_failed)
          << !AssertMessage << Msg.str() << AssertExpr->getSourceRange();
      }
      Failed = true;
    }
  }

  ExprResult FullAssertExpr = ActOnFinishFullExpr(AssertExpr, StaticAssertLoc,
                                                  /*DiscardedValue*/false,
                                                  /*IsConstexpr*/true);
  if (FullAssertExpr.isInvalid())
    Failed = true;
  else
    AssertExpr = FullAssertExpr.get();

  Decl *Decl = StaticAssertDecl::Create(Context, CurContext, StaticAssertLoc,
                                        AssertExpr, AssertMessage, RParenLoc,
                                        Failed);

  CurContext->addDecl(Decl);
  return Decl;
}

/// Perform semantic analysis of the given friend type declaration.
///
/// \returns A friend declaration that.
FriendDecl *Sema::CheckFriendTypeDecl(SourceLocation LocStart,
                                      SourceLocation FriendLoc,
                                      TypeSourceInfo *TSInfo) {
  assert(TSInfo && "NULL TypeSourceInfo for friend type declaration");

  QualType T = TSInfo->getType();
  SourceRange TypeRange = TSInfo->getTypeLoc().getLocalSourceRange();

  // C++03 [class.friend]p2:
  //   An elaborated-type-specifier shall be used in a friend declaration
  //   for a class.*
  //
  //   * The class-key of the elaborated-type-specifier is required.
  if (!CodeSynthesisContexts.empty()) {
    // Do not complain about the form of friend template types during any kind
    // of code synthesis. For template instantiation, we will have complained
    // when the template was defined.
  } else {
    if (!T->isElaboratedTypeSpecifier()) {
      // If we evaluated the type to a record type, suggest putting
      // a tag in front.
      if (const RecordType *RT = T->getAs<RecordType>()) {
        RecordDecl *RD = RT->getDecl();

        SmallString<16> InsertionText(" ");
        InsertionText += RD->getKindName();

        Diag(TypeRange.getBegin(),
             getLangOpts().CPlusPlus11 ?
               diag::warn_cxx98_compat_unelaborated_friend_type :
               diag::ext_unelaborated_friend_type)
          << (unsigned) RD->getTagKind()
          << T
          << FixItHint::CreateInsertion(getLocForEndOfToken(FriendLoc),
                                        InsertionText);
      } else {
        Diag(FriendLoc,
             getLangOpts().CPlusPlus11 ?
               diag::warn_cxx98_compat_nonclass_type_friend :
               diag::ext_nonclass_type_friend)
          << T
          << TypeRange;
      }
    } else if (T->getAs<EnumType>()) {
      Diag(FriendLoc,
           getLangOpts().CPlusPlus11 ?
             diag::warn_cxx98_compat_enum_friend :
             diag::ext_enum_friend)
        << T
        << TypeRange;
    }

    // C++11 [class.friend]p3:
    //   A friend declaration that does not declare a function shall have one
    //   of the following forms:
    //     friend elaborated-type-specifier ;
    //     friend simple-type-specifier ;
    //     friend typename-specifier ;
    if (getLangOpts().CPlusPlus11 && LocStart != FriendLoc)
      Diag(FriendLoc, diag::err_friend_not_first_in_declaration) << T;
  }

  //   If the type specifier in a friend declaration designates a (possibly
  //   cv-qualified) class type, that class is declared as a friend; otherwise,
  //   the friend declaration is ignored.
  return FriendDecl::Create(Context, CurContext,
                            TSInfo->getTypeLoc().getBeginLoc(), TSInfo,
                            FriendLoc);
}

/// Handle a friend tag declaration where the scope specifier was
/// templated.
Decl *Sema::ActOnTemplatedFriendTag(Scope *S, SourceLocation FriendLoc,
                                    unsigned TagSpec, SourceLocation TagLoc,
                                    CXXScopeSpec &SS, IdentifierInfo *Name,
                                    SourceLocation NameLoc,
                                    const ParsedAttributesView &Attr,
                                    MultiTemplateParamsArg TempParamLists) {
  TagTypeKind Kind = TypeWithKeyword::getTagTypeKindForTypeSpec(TagSpec);

  bool IsMemberSpecialization = false;
  bool Invalid = false;

  if (TemplateParameterList *TemplateParams =
          MatchTemplateParametersToScopeSpecifier(
              TagLoc, NameLoc, SS, nullptr, TempParamLists, /*friend*/ true,
              IsMemberSpecialization, Invalid)) {
    if (TemplateParams->size() > 0) {
      // This is a declaration of a class template.
      if (Invalid)
        return nullptr;

      return CheckClassTemplate(S, TagSpec, TUK_Friend, TagLoc, SS, Name,
                                NameLoc, Attr, TemplateParams, AS_public,
                                /*ModulePrivateLoc=*/SourceLocation(),
                                FriendLoc, TempParamLists.size() - 1,
                                TempParamLists.data()).get();
    } else {
      // The "template<>" header is extraneous.
      Diag(TemplateParams->getTemplateLoc(), diag::err_template_tag_noparams)
        << TypeWithKeyword::getTagTypeKindName(Kind) << Name;
      IsMemberSpecialization = true;
    }
  }

  if (Invalid) return nullptr;

  bool isAllExplicitSpecializations = true;
  for (unsigned I = TempParamLists.size(); I-- > 0; ) {
    if (TempParamLists[I]->size()) {
      isAllExplicitSpecializations = false;
      break;
    }
  }

  // FIXME: don't ignore attributes.

  // If it's explicit specializations all the way down, just forget
  // about the template header and build an appropriate non-templated
  // friend.  TODO: for source fidelity, remember the headers.
  if (isAllExplicitSpecializations) {
    if (SS.isEmpty()) {
      bool Owned = false;
      bool IsDependent = false;
      return ActOnTag(S, TagSpec, TUK_Friend, TagLoc, SS, Name, NameLoc,
                      Attr, AS_public,
                      /*ModulePrivateLoc=*/SourceLocation(),
                      MultiTemplateParamsArg(), Owned, IsDependent,
                      /*ScopedEnumKWLoc=*/SourceLocation(),
                      /*ScopedEnumUsesClassTag=*/false,
                      /*UnderlyingType=*/TypeResult(),
                      /*IsTypeSpecifier=*/false,
                      /*IsTemplateParamOrArg=*/false);
    }

    NestedNameSpecifierLoc QualifierLoc = SS.getWithLocInContext(Context);
    ElaboratedTypeKeyword Keyword
      = TypeWithKeyword::getKeywordForTagTypeKind(Kind);
    QualType T = CheckTypenameType(Keyword, TagLoc, QualifierLoc,
                                   *Name, NameLoc);
    if (T.isNull())
      return nullptr;

    TypeSourceInfo *TSI = Context.CreateTypeSourceInfo(T);
    if (isa<DependentNameType>(T)) {
      DependentNameTypeLoc TL =
          TSI->getTypeLoc().castAs<DependentNameTypeLoc>();
      TL.setElaboratedKeywordLoc(TagLoc);
      TL.setQualifierLoc(QualifierLoc);
      TL.setNameLoc(NameLoc);
    } else {
      ElaboratedTypeLoc TL = TSI->getTypeLoc().castAs<ElaboratedTypeLoc>();
      TL.setElaboratedKeywordLoc(TagLoc);
      TL.setQualifierLoc(QualifierLoc);
      TL.getNamedTypeLoc().castAs<TypeSpecTypeLoc>().setNameLoc(NameLoc);
    }

    FriendDecl *Friend = FriendDecl::Create(Context, CurContext, NameLoc,
                                            TSI, FriendLoc, TempParamLists);
    Friend->setAccess(AS_public);
    CurContext->addDecl(Friend);
    return Friend;
  }

  assert(SS.isNotEmpty() && "valid templated tag with no SS and no direct?");



  // Handle the case of a templated-scope friend class.  e.g.
  //   template <class T> class A<T>::B;
  // FIXME: we don't support these right now.
  Diag(NameLoc, diag::warn_template_qualified_friend_unsupported)
    << SS.getScopeRep() << SS.getRange() << cast<CXXRecordDecl>(CurContext);
  ElaboratedTypeKeyword ETK = TypeWithKeyword::getKeywordForTagTypeKind(Kind);
  QualType T = Context.getDependentNameType(ETK, SS.getScopeRep(), Name);
  TypeSourceInfo *TSI = Context.CreateTypeSourceInfo(T);
  DependentNameTypeLoc TL = TSI->getTypeLoc().castAs<DependentNameTypeLoc>();
  TL.setElaboratedKeywordLoc(TagLoc);
  TL.setQualifierLoc(SS.getWithLocInContext(Context));
  TL.setNameLoc(NameLoc);

  FriendDecl *Friend = FriendDecl::Create(Context, CurContext, NameLoc,
                                          TSI, FriendLoc, TempParamLists);
  Friend->setAccess(AS_public);
  Friend->setUnsupportedFriend(true);
  CurContext->addDecl(Friend);
  return Friend;
}

/// Handle a friend type declaration.  This works in tandem with
/// ActOnTag.
///
/// Notes on friend class templates:
///
/// We generally treat friend class declarations as if they were
/// declaring a class.  So, for example, the elaborated type specifier
/// in a friend declaration is required to obey the restrictions of a
/// class-head (i.e. no typedefs in the scope chain), template
/// parameters are required to match up with simple template-ids, &c.
/// However, unlike when declaring a template specialization, it's
/// okay to refer to a template specialization without an empty
/// template parameter declaration, e.g.
///   friend class A<T>::B<unsigned>;
/// We permit this as a special case; if there are any template
/// parameters present at all, require proper matching, i.e.
///   template <> template \<class T> friend class A<int>::B;
Decl *Sema::ActOnFriendTypeDecl(Scope *S, const DeclSpec &DS,
                                MultiTemplateParamsArg TempParams) {
  SourceLocation Loc = DS.getBeginLoc();

  assert(DS.isFriendSpecified());
  assert(DS.getStorageClassSpec() == DeclSpec::SCS_unspecified);

  // C++ [class.friend]p3:
  // A friend declaration that does not declare a function shall have one of
  // the following forms:
  //     friend elaborated-type-specifier ;
  //     friend simple-type-specifier ;
  //     friend typename-specifier ;
  //
  // Any declaration with a type qualifier does not have that form. (It's
  // legal to specify a qualified type as a friend, you just can't write the
  // keywords.)
  if (DS.getTypeQualifiers()) {
    if (DS.getTypeQualifiers() & DeclSpec::TQ_const)
      Diag(DS.getConstSpecLoc(), diag::err_friend_decl_spec) << "const";
    if (DS.getTypeQualifiers() & DeclSpec::TQ_volatile)
      Diag(DS.getVolatileSpecLoc(), diag::err_friend_decl_spec) << "volatile";
    if (DS.getTypeQualifiers() & DeclSpec::TQ_restrict)
      Diag(DS.getRestrictSpecLoc(), diag::err_friend_decl_spec) << "restrict";
    if (DS.getTypeQualifiers() & DeclSpec::TQ_atomic)
      Diag(DS.getAtomicSpecLoc(), diag::err_friend_decl_spec) << "_Atomic";
    if (DS.getTypeQualifiers() & DeclSpec::TQ_unaligned)
      Diag(DS.getUnalignedSpecLoc(), diag::err_friend_decl_spec) << "__unaligned";
  }

  // Try to convert the decl specifier to a type.  This works for
  // friend templates because ActOnTag never produces a ClassTemplateDecl
  // for a TUK_Friend.
  Declarator TheDeclarator(DS, DeclaratorContext::MemberContext);
  TypeSourceInfo *TSI = GetTypeForDeclarator(TheDeclarator, S);
  QualType T = TSI->getType();
  if (TheDeclarator.isInvalidType())
    return nullptr;

  if (DiagnoseUnexpandedParameterPack(Loc, TSI, UPPC_FriendDeclaration))
    return nullptr;

  // This is definitely an error in C++98.  It's probably meant to
  // be forbidden in C++0x, too, but the specification is just
  // poorly written.
  //
  // The problem is with declarations like the following:
  //   template <T> friend A<T>::foo;
  // where deciding whether a class C is a friend or not now hinges
  // on whether there exists an instantiation of A that causes
  // 'foo' to equal C.  There are restrictions on class-heads
  // (which we declare (by fiat) elaborated friend declarations to
  // be) that makes this tractable.
  //
  // FIXME: handle "template <> friend class A<T>;", which
  // is possibly well-formed?  Who even knows?
  if (TempParams.size() && !T->isElaboratedTypeSpecifier()) {
    Diag(Loc, diag::err_tagless_friend_type_template)
      << DS.getSourceRange();
    return nullptr;
  }

  // C++98 [class.friend]p1: A friend of a class is a function
  //   or class that is not a member of the class . . .
  // This is fixed in DR77, which just barely didn't make the C++03
  // deadline.  It's also a very silly restriction that seriously
  // affects inner classes and which nobody else seems to implement;
  // thus we never diagnose it, not even in -pedantic.
  //
  // But note that we could warn about it: it's always useless to
  // friend one of your own members (it's not, however, worthless to
  // friend a member of an arbitrary specialization of your template).

  Decl *D;
  if (!TempParams.empty())
    D = FriendTemplateDecl::Create(Context, CurContext, Loc,
                                   TempParams,
                                   TSI,
                                   DS.getFriendSpecLoc());
  else
    D = CheckFriendTypeDecl(Loc, DS.getFriendSpecLoc(), TSI);

  if (!D)
    return nullptr;

  D->setAccess(AS_public);
  CurContext->addDecl(D);

  return D;
}

NamedDecl *Sema::ActOnFriendFunctionDecl(Scope *S, Declarator &D,
                                        MultiTemplateParamsArg TemplateParams) {
  const DeclSpec &DS = D.getDeclSpec();

  assert(DS.isFriendSpecified());
  assert(DS.getStorageClassSpec() == DeclSpec::SCS_unspecified);

  SourceLocation Loc = D.getIdentifierLoc();
  TypeSourceInfo *TInfo = GetTypeForDeclarator(D, S);

  // C++ [class.friend]p1
  //   A friend of a class is a function or class....
  // Note that this sees through typedefs, which is intended.
  // It *doesn't* see through dependent types, which is correct
  // according to [temp.arg.type]p3:
  //   If a declaration acquires a function type through a
  //   type dependent on a template-parameter and this causes
  //   a declaration that does not use the syntactic form of a
  //   function declarator to have a function type, the program
  //   is ill-formed.
  if (!TInfo->getType()->isFunctionType()) {
    Diag(Loc, diag::err_unexpected_friend);

    // It might be worthwhile to try to recover by creating an
    // appropriate declaration.
    return nullptr;
  }

  // C++ [namespace.memdef]p3
  //  - If a friend declaration in a non-local class first declares a
  //    class or function, the friend class or function is a member
  //    of the innermost enclosing namespace.
  //  - The name of the friend is not found by simple name lookup
  //    until a matching declaration is provided in that namespace
  //    scope (either before or after the class declaration granting
  //    friendship).
  //  - If a friend function is called, its name may be found by the
  //    name lookup that considers functions from namespaces and
  //    classes associated with the types of the function arguments.
  //  - When looking for a prior declaration of a class or a function
  //    declared as a friend, scopes outside the innermost enclosing
  //    namespace scope are not considered.

  CXXScopeSpec &SS = D.getCXXScopeSpec();
  DeclarationNameInfo NameInfo = GetNameForDeclarator(D);
  assert(NameInfo.getName());

  // Check for unexpanded parameter packs.
  if (DiagnoseUnexpandedParameterPack(Loc, TInfo, UPPC_FriendDeclaration) ||
      DiagnoseUnexpandedParameterPack(NameInfo, UPPC_FriendDeclaration) ||
      DiagnoseUnexpandedParameterPack(SS, UPPC_FriendDeclaration))
    return nullptr;

  // The context we found the declaration in, or in which we should
  // create the declaration.
  DeclContext *DC;
  Scope *DCScope = S;
  LookupResult Previous(*this, NameInfo, LookupOrdinaryName,
                        ForExternalRedeclaration);

  // There are five cases here.
  //   - There's no scope specifier and we're in a local class. Only look
  //     for functions declared in the immediately-enclosing block scope.
  // We recover from invalid scope qualifiers as if they just weren't there.
  FunctionDecl *FunctionContainingLocalClass = nullptr;
  if ((SS.isInvalid() || !SS.isSet()) &&
      (FunctionContainingLocalClass =
           cast<CXXRecordDecl>(CurContext)->isLocalClass())) {
    // C++11 [class.friend]p11:
    //   If a friend declaration appears in a local class and the name
    //   specified is an unqualified name, a prior declaration is
    //   looked up without considering scopes that are outside the
    //   innermost enclosing non-class scope. For a friend function
    //   declaration, if there is no prior declaration, the program is
    //   ill-formed.

    // Find the innermost enclosing non-class scope. This is the block
    // scope containing the local class definition (or for a nested class,
    // the outer local class).
    DCScope = S->getFnParent();

    // Look up the function name in the scope.
    Previous.clear(LookupLocalFriendName);
    LookupName(Previous, S, /*AllowBuiltinCreation*/false);

    if (!Previous.empty()) {
      // All possible previous declarations must have the same context:
      // either they were declared at block scope or they are members of
      // one of the enclosing local classes.
      DC = Previous.getRepresentativeDecl()->getDeclContext();
    } else {
      // This is ill-formed, but provide the context that we would have
      // declared the function in, if we were permitted to, for error recovery.
      DC = FunctionContainingLocalClass;
    }
    adjustContextForLocalExternDecl(DC);

    // C++ [class.friend]p6:
    //   A function can be defined in a friend declaration of a class if and
    //   only if the class is a non-local class (9.8), the function name is
    //   unqualified, and the function has namespace scope.
    if (D.isFunctionDefinition()) {
      Diag(NameInfo.getBeginLoc(), diag::err_friend_def_in_local_class);
    }

  //   - There's no scope specifier, in which case we just go to the
  //     appropriate scope and look for a function or function template
  //     there as appropriate.
  } else if (SS.isInvalid() || !SS.isSet()) {
    // C++11 [namespace.memdef]p3:
    //   If the name in a friend declaration is neither qualified nor
    //   a template-id and the declaration is a function or an
    //   elaborated-type-specifier, the lookup to determine whether
    //   the entity has been previously declared shall not consider
    //   any scopes outside the innermost enclosing namespace.
    bool isTemplateId =
        D.getName().getKind() == UnqualifiedIdKind::IK_TemplateId;

    // Find the appropriate context according to the above.
    DC = CurContext;

    // Skip class contexts.  If someone can cite chapter and verse
    // for this behavior, that would be nice --- it's what GCC and
    // EDG do, and it seems like a reasonable intent, but the spec
    // really only says that checks for unqualified existing
    // declarations should stop at the nearest enclosing namespace,
    // not that they should only consider the nearest enclosing
    // namespace.
    while (DC->isRecord())
      DC = DC->getParent();

    DeclContext *LookupDC = DC;
    while (LookupDC->isTransparentContext())
      LookupDC = LookupDC->getParent();

    while (true) {
      LookupQualifiedName(Previous, LookupDC);

      if (!Previous.empty()) {
        DC = LookupDC;
        break;
      }

      if (isTemplateId) {
        if (isa<TranslationUnitDecl>(LookupDC)) break;
      } else {
        if (LookupDC->isFileContext()) break;
      }
      LookupDC = LookupDC->getParent();
    }

    DCScope = getScopeForDeclContext(S, DC);

  //   - There's a non-dependent scope specifier, in which case we
  //     compute it and do a previous lookup there for a function
  //     or function template.
  } else if (!SS.getScopeRep()->isDependent()) {
    DC = computeDeclContext(SS);
    if (!DC) return nullptr;

    if (RequireCompleteDeclContext(SS, DC)) return nullptr;

    LookupQualifiedName(Previous, DC);

    // C++ [class.friend]p1: A friend of a class is a function or
    //   class that is not a member of the class . . .
    if (DC->Equals(CurContext))
      Diag(DS.getFriendSpecLoc(),
           getLangOpts().CPlusPlus11 ?
             diag::warn_cxx98_compat_friend_is_member :
             diag::err_friend_is_member);

    if (D.isFunctionDefinition()) {
      // C++ [class.friend]p6:
      //   A function can be defined in a friend declaration of a class if and
      //   only if the class is a non-local class (9.8), the function name is
      //   unqualified, and the function has namespace scope.
      //
      // FIXME: We should only do this if the scope specifier names the
      // innermost enclosing namespace; otherwise the fixit changes the
      // meaning of the code.
      SemaDiagnosticBuilder DB
        = Diag(SS.getRange().getBegin(), diag::err_qualified_friend_def);

      DB << SS.getScopeRep();
      if (DC->isFileContext())
        DB << FixItHint::CreateRemoval(SS.getRange());
      SS.clear();
    }

  //   - There's a scope specifier that does not match any template
  //     parameter lists, in which case we use some arbitrary context,
  //     create a method or method template, and wait for instantiation.
  //   - There's a scope specifier that does match some template
  //     parameter lists, which we don't handle right now.
  } else {
    if (D.isFunctionDefinition()) {
      // C++ [class.friend]p6:
      //   A function can be defined in a friend declaration of a class if and
      //   only if the class is a non-local class (9.8), the function name is
      //   unqualified, and the function has namespace scope.
      Diag(SS.getRange().getBegin(), diag::err_qualified_friend_def)
        << SS.getScopeRep();
    }

    DC = CurContext;
    assert(isa<CXXRecordDecl>(DC) && "friend declaration not in class?");
  }

  if (!DC->isRecord()) {
    int DiagArg = -1;
    switch (D.getName().getKind()) {
    case UnqualifiedIdKind::IK_ConstructorTemplateId:
    case UnqualifiedIdKind::IK_ConstructorName:
      DiagArg = 0;
      break;
    case UnqualifiedIdKind::IK_DestructorName:
      DiagArg = 1;
      break;
    case UnqualifiedIdKind::IK_ConversionFunctionId:
      DiagArg = 2;
      break;
    case UnqualifiedIdKind::IK_DeductionGuideName:
      DiagArg = 3;
      break;
    case UnqualifiedIdKind::IK_Identifier:
    case UnqualifiedIdKind::IK_ImplicitSelfParam:
    case UnqualifiedIdKind::IK_LiteralOperatorId:
    case UnqualifiedIdKind::IK_OperatorFunctionId:
    case UnqualifiedIdKind::IK_TemplateId:
      break;
    }
    // This implies that it has to be an operator or function.
    if (DiagArg >= 0) {
      Diag(Loc, diag::err_introducing_special_friend) << DiagArg;
      return nullptr;
    }
  }

  // FIXME: This is an egregious hack to cope with cases where the scope stack
  // does not contain the declaration context, i.e., in an out-of-line
  // definition of a class.
  Scope FakeDCScope(S, Scope::DeclScope, Diags);
  if (!DCScope) {
    FakeDCScope.setEntity(DC);
    DCScope = &FakeDCScope;
  }

  bool AddToScope = true;
  NamedDecl *ND = ActOnFunctionDeclarator(DCScope, D, DC, TInfo, Previous,
                                          TemplateParams, AddToScope);
  if (!ND) return nullptr;

  assert(ND->getLexicalDeclContext() == CurContext);

  // If we performed typo correction, we might have added a scope specifier
  // and changed the decl context.
  DC = ND->getDeclContext();

  // Add the function declaration to the appropriate lookup tables,
  // adjusting the redeclarations list as necessary.  We don't
  // want to do this yet if the friending class is dependent.
  //
  // Also update the scope-based lookup if the target context's
  // lookup context is in lexical scope.
  if (!CurContext->isDependentContext()) {
    DC = DC->getRedeclContext();
    DC->makeDeclVisibleInContext(ND);
    if (Scope *EnclosingScope = getScopeForDeclContext(S, DC))
      PushOnScopeChains(ND, EnclosingScope, /*AddToContext=*/ false);
  }

  FriendDecl *FrD = FriendDecl::Create(Context, CurContext,
                                       D.getIdentifierLoc(), ND,
                                       DS.getFriendSpecLoc());
  FrD->setAccess(AS_public);
  CurContext->addDecl(FrD);

  if (ND->isInvalidDecl()) {
    FrD->setInvalidDecl();
  } else {
    if (DC->isRecord()) CheckFriendAccess(ND);

    FunctionDecl *FD;
    if (FunctionTemplateDecl *FTD = dyn_cast<FunctionTemplateDecl>(ND))
      FD = FTD->getTemplatedDecl();
    else
      FD = cast<FunctionDecl>(ND);

    // C++11 [dcl.fct.default]p4: If a friend declaration specifies a
    // default argument expression, that declaration shall be a definition
    // and shall be the only declaration of the function or function
    // template in the translation unit.
    if (functionDeclHasDefaultArgument(FD)) {
      // We can't look at FD->getPreviousDecl() because it may not have been set
      // if we're in a dependent context. If the function is known to be a
      // redeclaration, we will have narrowed Previous down to the right decl.
      if (D.isRedeclaration()) {
        Diag(FD->getLocation(), diag::err_friend_decl_with_def_arg_redeclared);
        Diag(Previous.getRepresentativeDecl()->getLocation(),
             diag::note_previous_declaration);
      } else if (!D.isFunctionDefinition())
        Diag(FD->getLocation(), diag::err_friend_decl_with_def_arg_must_be_def);
    }

    // Mark templated-scope function declarations as unsupported.
    if (FD->getNumTemplateParameterLists() && SS.isValid()) {
      Diag(FD->getLocation(), diag::warn_template_qualified_friend_unsupported)
        << SS.getScopeRep() << SS.getRange()
        << cast<CXXRecordDecl>(CurContext);
      FrD->setUnsupportedFriend(true);
    }
  }

  return ND;
}

void Sema::SetDeclDeleted(Decl *Dcl, SourceLocation DelLoc) {
  AdjustDeclIfTemplate(Dcl);

  FunctionDecl *Fn = dyn_cast_or_null<FunctionDecl>(Dcl);
  if (!Fn) {
    Diag(DelLoc, diag::err_deleted_non_function);
    return;
  }

  // Deleted function does not have a body.
  Fn->setWillHaveBody(false);

  if (const FunctionDecl *Prev = Fn->getPreviousDecl()) {
    // Don't consider the implicit declaration we generate for explicit
    // specializations. FIXME: Do not generate these implicit declarations.
    if ((Prev->getTemplateSpecializationKind() != TSK_ExplicitSpecialization ||
         Prev->getPreviousDecl()) &&
        !Prev->isDefined()) {
      Diag(DelLoc, diag::err_deleted_decl_not_first);
      Diag(Prev->getLocation().isInvalid() ? DelLoc : Prev->getLocation(),
           Prev->isImplicit() ? diag::note_previous_implicit_declaration
                              : diag::note_previous_declaration);
    }
    // If the declaration wasn't the first, we delete the function anyway for
    // recovery.
    Fn = Fn->getCanonicalDecl();
  }

  // dllimport/dllexport cannot be deleted.
  if (const InheritableAttr *DLLAttr = getDLLAttr(Fn)) {
    Diag(Fn->getLocation(), diag::err_attribute_dll_deleted) << DLLAttr;
    Fn->setInvalidDecl();
  }

  if (Fn->isDeleted())
    return;

  // See if we're deleting a function which is already known to override a
  // non-deleted virtual function.
  if (CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(Fn)) {
    bool IssuedDiagnostic = false;
    for (const CXXMethodDecl *O : MD->overridden_methods()) {
      if (!(*MD->begin_overridden_methods())->isDeleted()) {
        if (!IssuedDiagnostic) {
          Diag(DelLoc, diag::err_deleted_override) << MD->getDeclName();
          IssuedDiagnostic = true;
        }
        Diag(O->getLocation(), diag::note_overridden_virtual_function);
      }
    }
    // If this function was implicitly deleted because it was defaulted,
    // explain why it was deleted.
    if (IssuedDiagnostic && MD->isDefaulted())
      ShouldDeleteSpecialMember(MD, getSpecialMember(MD), nullptr,
                                /*Diagnose*/true);
  }

  // C++11 [basic.start.main]p3:
  //   A program that defines main as deleted [...] is ill-formed.
  if (Fn->isMain())
    Diag(DelLoc, diag::err_deleted_main);

  // C++11 [dcl.fct.def.delete]p4:
  //  A deleted function is implicitly inline.
  Fn->setImplicitlyInline();
  Fn->setDeletedAsWritten();
}

void Sema::SetDeclDefaulted(Decl *Dcl, SourceLocation DefaultLoc) {
  CXXMethodDecl *MD = dyn_cast_or_null<CXXMethodDecl>(Dcl);

  if (MD) {
    if (MD->getParent()->isDependentType()) {
      MD->setDefaulted();
      MD->setExplicitlyDefaulted();
      return;
    }

    CXXSpecialMember Member = getSpecialMember(MD);
    if (Member == CXXInvalid) {
      if (!MD->isInvalidDecl())
        Diag(DefaultLoc, diag::err_default_special_members);
      return;
    }

    MD->setDefaulted();
    MD->setExplicitlyDefaulted();

    // Unset that we will have a body for this function. We might not,
    // if it turns out to be trivial, and we don't need this marking now
    // that we've marked it as defaulted.
    MD->setWillHaveBody(false);

    // If this definition appears within the record, do the checking when
    // the record is complete.
    const FunctionDecl *Primary = MD;
    if (const FunctionDecl *Pattern = MD->getTemplateInstantiationPattern())
      // Ask the template instantiation pattern that actually had the
      // '= default' on it.
      Primary = Pattern;

    // If the method was defaulted on its first declaration, we will have
    // already performed the checking in CheckCompletedCXXClass. Such a
    // declaration doesn't trigger an implicit definition.
    if (Primary->getCanonicalDecl()->isDefaulted())
      return;

    CheckExplicitlyDefaultedSpecialMember(MD);

    if (!MD->isInvalidDecl())
      DefineImplicitSpecialMember(*this, MD, DefaultLoc);
  } else {
    Diag(DefaultLoc, diag::err_default_special_members);
  }
}

static void SearchForReturnInStmt(Sema &Self, Stmt *S) {
  for (Stmt *SubStmt : S->children()) {
    if (!SubStmt)
      continue;
    if (isa<ReturnStmt>(SubStmt))
      Self.Diag(SubStmt->getBeginLoc(),
                diag::err_return_in_constructor_handler);
    if (!isa<Expr>(SubStmt))
      SearchForReturnInStmt(Self, SubStmt);
  }
}

void Sema::DiagnoseReturnInConstructorExceptionHandler(CXXTryStmt *TryBlock) {
  for (unsigned I = 0, E = TryBlock->getNumHandlers(); I != E; ++I) {
    CXXCatchStmt *Handler = TryBlock->getHandler(I);
    SearchForReturnInStmt(*this, Handler);
  }
}

bool Sema::CheckOverridingFunctionAttributes(const CXXMethodDecl *New,
                                             const CXXMethodDecl *Old) {
  const auto *NewFT = New->getType()->getAs<FunctionProtoType>();
  const auto *OldFT = Old->getType()->getAs<FunctionProtoType>();

  if (OldFT->hasExtParameterInfos()) {
    for (unsigned I = 0, E = OldFT->getNumParams(); I != E; ++I)
      // A parameter of the overriding method should be annotated with noescape
      // if the corresponding parameter of the overridden method is annotated.
      if (OldFT->getExtParameterInfo(I).isNoEscape() &&
          !NewFT->getExtParameterInfo(I).isNoEscape()) {
        Diag(New->getParamDecl(I)->getLocation(),
             diag::warn_overriding_method_missing_noescape);
        Diag(Old->getParamDecl(I)->getLocation(),
             diag::note_overridden_marked_noescape);
      }
  }

  // Virtual overrides must have the same code_seg.
  const auto *OldCSA = Old->getAttr<CodeSegAttr>();
  const auto *NewCSA = New->getAttr<CodeSegAttr>();
  if ((NewCSA || OldCSA) &&
      (!OldCSA || !NewCSA || NewCSA->getName() != OldCSA->getName())) {
    Diag(New->getLocation(), diag::err_mismatched_code_seg_override);
    Diag(Old->getLocation(), diag::note_previous_declaration);
    return true;
  }

  CallingConv NewCC = NewFT->getCallConv(), OldCC = OldFT->getCallConv();

  // If the calling conventions match, everything is fine
  if (NewCC == OldCC)
    return false;

  // If the calling conventions mismatch because the new function is static,
  // suppress the calling convention mismatch error; the error about static
  // function override (err_static_overrides_virtual from
  // Sema::CheckFunctionDeclaration) is more clear.
  if (New->getStorageClass() == SC_Static)
    return false;

  Diag(New->getLocation(),
       diag::err_conflicting_overriding_cc_attributes)
    << New->getDeclName() << New->getType() << Old->getType();
  Diag(Old->getLocation(), diag::note_overridden_virtual_function);
  return true;
}

bool Sema::CheckOverridingFunctionReturnType(const CXXMethodDecl *New,
                                             const CXXMethodDecl *Old) {
  QualType NewTy = New->getType()->getAs<FunctionType>()->getReturnType();
  QualType OldTy = Old->getType()->getAs<FunctionType>()->getReturnType();

  if (Context.hasSameType(NewTy, OldTy) ||
      NewTy->isDependentType() || OldTy->isDependentType())
    return false;

  // Check if the return types are covariant
  QualType NewClassTy, OldClassTy;

  /// Both types must be pointers or references to classes.
  if (const PointerType *NewPT = NewTy->getAs<PointerType>()) {
    if (const PointerType *OldPT = OldTy->getAs<PointerType>()) {
      NewClassTy = NewPT->getPointeeType();
      OldClassTy = OldPT->getPointeeType();
    }
  } else if (const ReferenceType *NewRT = NewTy->getAs<ReferenceType>()) {
    if (const ReferenceType *OldRT = OldTy->getAs<ReferenceType>()) {
      if (NewRT->getTypeClass() == OldRT->getTypeClass()) {
        NewClassTy = NewRT->getPointeeType();
        OldClassTy = OldRT->getPointeeType();
      }
    }
  }

  // The return types aren't either both pointers or references to a class type.
  if (NewClassTy.isNull()) {
    Diag(New->getLocation(),
         diag::err_different_return_type_for_overriding_virtual_function)
        << New->getDeclName() << NewTy << OldTy
        << New->getReturnTypeSourceRange();
    Diag(Old->getLocation(), diag::note_overridden_virtual_function)
        << Old->getReturnTypeSourceRange();

    return true;
  }

  if (!Context.hasSameUnqualifiedType(NewClassTy, OldClassTy)) {
    // C++14 [class.virtual]p8:
    //   If the class type in the covariant return type of D::f differs from
    //   that of B::f, the class type in the return type of D::f shall be
    //   complete at the point of declaration of D::f or shall be the class
    //   type D.
    if (const RecordType *RT = NewClassTy->getAs<RecordType>()) {
      if (!RT->isBeingDefined() &&
          RequireCompleteType(New->getLocation(), NewClassTy,
                              diag::err_covariant_return_incomplete,
                              New->getDeclName()))
        return true;
    }

    // Check if the new class derives from the old class.
    if (!IsDerivedFrom(New->getLocation(), NewClassTy, OldClassTy)) {
      Diag(New->getLocation(), diag::err_covariant_return_not_derived)
          << New->getDeclName() << NewTy << OldTy
          << New->getReturnTypeSourceRange();
      Diag(Old->getLocation(), diag::note_overridden_virtual_function)
          << Old->getReturnTypeSourceRange();
      return true;
    }

    // Check if we the conversion from derived to base is valid.
    if (CheckDerivedToBaseConversion(
            NewClassTy, OldClassTy,
            diag::err_covariant_return_inaccessible_base,
            diag::err_covariant_return_ambiguous_derived_to_base_conv,
            New->getLocation(), New->getReturnTypeSourceRange(),
            New->getDeclName(), nullptr)) {
      // FIXME: this note won't trigger for delayed access control
      // diagnostics, and it's impossible to get an undelayed error
      // here from access control during the original parse because
      // the ParsingDeclSpec/ParsingDeclarator are still in scope.
      Diag(Old->getLocation(), diag::note_overridden_virtual_function)
          << Old->getReturnTypeSourceRange();
      return true;
    }
  }

  // The qualifiers of the return types must be the same.
  if (NewTy.getLocalCVRQualifiers() != OldTy.getLocalCVRQualifiers()) {
    Diag(New->getLocation(),
         diag::err_covariant_return_type_different_qualifications)
        << New->getDeclName() << NewTy << OldTy
        << New->getReturnTypeSourceRange();
    Diag(Old->getLocation(), diag::note_overridden_virtual_function)
        << Old->getReturnTypeSourceRange();
    return true;
  }


  // The new class type must have the same or less qualifiers as the old type.
  if (NewClassTy.isMoreQualifiedThan(OldClassTy)) {
    Diag(New->getLocation(),
         diag::err_covariant_return_type_class_type_more_qualified)
        << New->getDeclName() << NewTy << OldTy
        << New->getReturnTypeSourceRange();
    Diag(Old->getLocation(), diag::note_overridden_virtual_function)
        << Old->getReturnTypeSourceRange();
    return true;
  }

  return false;
}

/// Mark the given method pure.
///
/// \param Method the method to be marked pure.
///
/// \param InitRange the source range that covers the "0" initializer.
bool Sema::CheckPureMethod(CXXMethodDecl *Method, SourceRange InitRange) {
  SourceLocation EndLoc = InitRange.getEnd();
  if (EndLoc.isValid())
    Method->setRangeEnd(EndLoc);

  if (Method->isVirtual() || Method->getParent()->isDependentContext()) {
    Method->setPure();
    return false;
  }

  if (!Method->isInvalidDecl())
    Diag(Method->getLocation(), diag::err_non_virtual_pure)
      << Method->getDeclName() << InitRange;
  return true;
}

void Sema::ActOnPureSpecifier(Decl *D, SourceLocation ZeroLoc) {
  if (D->getFriendObjectKind())
    Diag(D->getLocation(), diag::err_pure_friend);
  else if (auto *M = dyn_cast<CXXMethodDecl>(D))
    CheckPureMethod(M, ZeroLoc);
  else
    Diag(D->getLocation(), diag::err_illegal_initializer);
}

/// Determine whether the given declaration is a global variable or
/// static data member.
static bool isNonlocalVariable(const Decl *D) {
  if (const VarDecl *Var = dyn_cast_or_null<VarDecl>(D))
    return Var->hasGlobalStorage();

  return false;
}

/// Invoked when we are about to parse an initializer for the declaration
/// 'Dcl'.
///
/// After this method is called, according to [C++ 3.4.1p13], if 'Dcl' is a
/// static data member of class X, names should be looked up in the scope of
/// class X. If the declaration had a scope specifier, a scope will have
/// been created and passed in for this purpose. Otherwise, S will be null.
void Sema::ActOnCXXEnterDeclInitializer(Scope *S, Decl *D) {
  // If there is no declaration, there was an error parsing it.
  if (!D || D->isInvalidDecl())
    return;

  // We will always have a nested name specifier here, but this declaration
  // might not be out of line if the specifier names the current namespace:
  //   extern int n;
  //   int ::n = 0;
  if (S && D->isOutOfLine())
    EnterDeclaratorContext(S, D->getDeclContext());

  // If we are parsing the initializer for a static data member, push a
  // new expression evaluation context that is associated with this static
  // data member.
  if (isNonlocalVariable(D))
    PushExpressionEvaluationContext(
        ExpressionEvaluationContext::PotentiallyEvaluated, D);
}

/// Invoked after we are finished parsing an initializer for the declaration D.
void Sema::ActOnCXXExitDeclInitializer(Scope *S, Decl *D) {
  // If there is no declaration, there was an error parsing it.
  if (!D || D->isInvalidDecl())
    return;

  if (isNonlocalVariable(D))
    PopExpressionEvaluationContext();

  if (S && D->isOutOfLine())
    ExitDeclaratorContext(S);
}

/// ActOnCXXConditionDeclarationExpr - Parsed a condition declaration of a
/// C++ if/switch/while/for statement.
/// e.g: "if (int x = f()) {...}"
DeclResult Sema::ActOnCXXConditionDeclaration(Scope *S, Declarator &D) {
  // C++ 6.4p2:
  // The declarator shall not specify a function or an array.
  // The type-specifier-seq shall not contain typedef and shall not declare a
  // new class or enumeration.
  assert(D.getDeclSpec().getStorageClassSpec() != DeclSpec::SCS_typedef &&
         "Parser allowed 'typedef' as storage class of condition decl.");

  Decl *Dcl = ActOnDeclarator(S, D);
  if (!Dcl)
    return true;

  if (isa<FunctionDecl>(Dcl)) { // The declarator shall not specify a function.
    Diag(Dcl->getLocation(), diag::err_invalid_use_of_function_type)
      << D.getSourceRange();
    return true;
  }

  return Dcl;
}

void Sema::LoadExternalVTableUses() {
  if (!ExternalSource)
    return;

  SmallVector<ExternalVTableUse, 4> VTables;
  ExternalSource->ReadUsedVTables(VTables);
  SmallVector<VTableUse, 4> NewUses;
  for (unsigned I = 0, N = VTables.size(); I != N; ++I) {
    llvm::DenseMap<CXXRecordDecl *, bool>::iterator Pos
      = VTablesUsed.find(VTables[I].Record);
    // Even if a definition wasn't required before, it may be required now.
    if (Pos != VTablesUsed.end()) {
      if (!Pos->second && VTables[I].DefinitionRequired)
        Pos->second = true;
      continue;
    }

    VTablesUsed[VTables[I].Record] = VTables[I].DefinitionRequired;
    NewUses.push_back(VTableUse(VTables[I].Record, VTables[I].Location));
  }

  VTableUses.insert(VTableUses.begin(), NewUses.begin(), NewUses.end());
}

void Sema::MarkVTableUsed(SourceLocation Loc, CXXRecordDecl *Class,
                          bool DefinitionRequired) {
  // Ignore any vtable uses in unevaluated operands or for classes that do
  // not have a vtable.
  if (!Class->isDynamicClass() || Class->isDependentContext() ||
      CurContext->isDependentContext() || isUnevaluatedContext())
    return;
  // Do not mark as used if compiling for the device outside of the target
  // region.
  if (LangOpts.OpenMP && LangOpts.OpenMPIsDevice &&
      !isInOpenMPDeclareTargetContext() &&
      !isInOpenMPTargetExecutionDirective()) {
    if (!DefinitionRequired)
      MarkVirtualMembersReferenced(Loc, Class);
    return;
  }

  // Try to insert this class into the map.
  LoadExternalVTableUses();
  Class = Class->getCanonicalDecl();
  std::pair<llvm::DenseMap<CXXRecordDecl *, bool>::iterator, bool>
    Pos = VTablesUsed.insert(std::make_pair(Class, DefinitionRequired));
  if (!Pos.second) {
    // If we already had an entry, check to see if we are promoting this vtable
    // to require a definition. If so, we need to reappend to the VTableUses
    // list, since we may have already processed the first entry.
    if (DefinitionRequired && !Pos.first->second) {
      Pos.first->second = true;
    } else {
      // Otherwise, we can early exit.
      return;
    }
  } else {
    // The Microsoft ABI requires that we perform the destructor body
    // checks (i.e. operator delete() lookup) when the vtable is marked used, as
    // the deleting destructor is emitted with the vtable, not with the
    // destructor definition as in the Itanium ABI.
    if (Context.getTargetInfo().getCXXABI().isMicrosoft()) {
      CXXDestructorDecl *DD = Class->getDestructor();
      if (DD && DD->isVirtual() && !DD->isDeleted()) {
        if (Class->hasUserDeclaredDestructor() && !DD->isDefined()) {
          // If this is an out-of-line declaration, marking it referenced will
          // not do anything. Manually call CheckDestructor to look up operator
          // delete().
          ContextRAII SavedContext(*this, DD);
          CheckDestructor(DD);
        } else {
          MarkFunctionReferenced(Loc, Class->getDestructor());
        }
      }
    }
  }

  // Local classes need to have their virtual members marked
  // immediately. For all other classes, we mark their virtual members
  // at the end of the translation unit.
  if (Class->isLocalClass())
    MarkVirtualMembersReferenced(Loc, Class);
  else
    VTableUses.push_back(std::make_pair(Class, Loc));
}

bool Sema::DefineUsedVTables() {
  LoadExternalVTableUses();
  if (VTableUses.empty())
    return false;

  // Note: The VTableUses vector could grow as a result of marking
  // the members of a class as "used", so we check the size each
  // time through the loop and prefer indices (which are stable) to
  // iterators (which are not).
  bool DefinedAnything = false;
  for (unsigned I = 0; I != VTableUses.size(); ++I) {
    CXXRecordDecl *Class = VTableUses[I].first->getDefinition();
    if (!Class)
      continue;
    TemplateSpecializationKind ClassTSK =
        Class->getTemplateSpecializationKind();

    SourceLocation Loc = VTableUses[I].second;

    bool DefineVTable = true;

    // If this class has a key function, but that key function is
    // defined in another translation unit, we don't need to emit the
    // vtable even though we're using it.
    const CXXMethodDecl *KeyFunction = Context.getCurrentKeyFunction(Class);
    if (KeyFunction && !KeyFunction->hasBody()) {
      // The key function is in another translation unit.
      DefineVTable = false;
      TemplateSpecializationKind TSK =
          KeyFunction->getTemplateSpecializationKind();
      assert(TSK != TSK_ExplicitInstantiationDefinition &&
             TSK != TSK_ImplicitInstantiation &&
             "Instantiations don't have key functions");
      (void)TSK;
    } else if (!KeyFunction) {
      // If we have a class with no key function that is the subject
      // of an explicit instantiation declaration, suppress the
      // vtable; it will live with the explicit instantiation
      // definition.
      bool IsExplicitInstantiationDeclaration =
          ClassTSK == TSK_ExplicitInstantiationDeclaration;
      for (auto R : Class->redecls()) {
        TemplateSpecializationKind TSK
          = cast<CXXRecordDecl>(R)->getTemplateSpecializationKind();
        if (TSK == TSK_ExplicitInstantiationDeclaration)
          IsExplicitInstantiationDeclaration = true;
        else if (TSK == TSK_ExplicitInstantiationDefinition) {
          IsExplicitInstantiationDeclaration = false;
          break;
        }
      }

      if (IsExplicitInstantiationDeclaration)
        DefineVTable = false;
    }

    // The exception specifications for all virtual members may be needed even
    // if we are not providing an authoritative form of the vtable in this TU.
    // We may choose to emit it available_externally anyway.
    if (!DefineVTable) {
      MarkVirtualMemberExceptionSpecsNeeded(Loc, Class);
      continue;
    }

    // Mark all of the virtual members of this class as referenced, so
    // that we can build a vtable. Then, tell the AST consumer that a
    // vtable for this class is required.
    DefinedAnything = true;
    MarkVirtualMembersReferenced(Loc, Class);
    CXXRecordDecl *Canonical = Class->getCanonicalDecl();
    if (VTablesUsed[Canonical])
      Consumer.HandleVTable(Class);

    // Warn if we're emitting a weak vtable. The vtable will be weak if there is
    // no key function or the key function is inlined. Don't warn in C++ ABIs
    // that lack key functions, since the user won't be able to make one.
    if (Context.getTargetInfo().getCXXABI().hasKeyFunctions() &&
        Class->isExternallyVisible() && ClassTSK != TSK_ImplicitInstantiation) {
      const FunctionDecl *KeyFunctionDef = nullptr;
      if (!KeyFunction || (KeyFunction->hasBody(KeyFunctionDef) &&
                           KeyFunctionDef->isInlined())) {
        Diag(Class->getLocation(),
             ClassTSK == TSK_ExplicitInstantiationDefinition
                 ? diag::warn_weak_template_vtable
                 : diag::warn_weak_vtable)
            << Class;
      }
    }
  }
  VTableUses.clear();

  return DefinedAnything;
}

void Sema::MarkVirtualMemberExceptionSpecsNeeded(SourceLocation Loc,
                                                 const CXXRecordDecl *RD) {
  for (const auto *I : RD->methods())
    if (I->isVirtual() && !I->isPure())
      ResolveExceptionSpec(Loc, I->getType()->castAs<FunctionProtoType>());
}

void Sema::MarkVirtualMembersReferenced(SourceLocation Loc,
                                        const CXXRecordDecl *RD) {
  // Mark all functions which will appear in RD's vtable as used.
  CXXFinalOverriderMap FinalOverriders;
  RD->getFinalOverriders(FinalOverriders);
  for (CXXFinalOverriderMap::const_iterator I = FinalOverriders.begin(),
                                            E = FinalOverriders.end();
       I != E; ++I) {
    for (OverridingMethods::const_iterator OI = I->second.begin(),
                                           OE = I->second.end();
         OI != OE; ++OI) {
      assert(OI->second.size() > 0 && "no final overrider");
      CXXMethodDecl *Overrider = OI->second.front().Method;

      // C++ [basic.def.odr]p2:
      //   [...] A virtual member function is used if it is not pure. [...]
      if (!Overrider->isPure())
        MarkFunctionReferenced(Loc, Overrider);
    }
  }

  // Only classes that have virtual bases need a VTT.
  if (RD->getNumVBases() == 0)
    return;

  for (const auto &I : RD->bases()) {
    const CXXRecordDecl *Base =
        cast<CXXRecordDecl>(I.getType()->getAs<RecordType>()->getDecl());
    if (Base->getNumVBases() == 0)
      continue;
    MarkVirtualMembersReferenced(Loc, Base);
  }
}

/// SetIvarInitializers - This routine builds initialization ASTs for the
/// Objective-C implementation whose ivars need be initialized.
void Sema::SetIvarInitializers(ObjCImplementationDecl *ObjCImplementation) {
  if (!getLangOpts().CPlusPlus)
    return;
  if (ObjCInterfaceDecl *OID = ObjCImplementation->getClassInterface()) {
    SmallVector<ObjCIvarDecl*, 8> ivars;
    CollectIvarsToConstructOrDestruct(OID, ivars);
    if (ivars.empty())
      return;
    SmallVector<CXXCtorInitializer*, 32> AllToInit;
    for (unsigned i = 0; i < ivars.size(); i++) {
      FieldDecl *Field = ivars[i];
      if (Field->isInvalidDecl())
        continue;

      CXXCtorInitializer *Member;
      InitializedEntity InitEntity = InitializedEntity::InitializeMember(Field);
      InitializationKind InitKind =
        InitializationKind::CreateDefault(ObjCImplementation->getLocation());

      InitializationSequence InitSeq(*this, InitEntity, InitKind, None);
      ExprResult MemberInit =
        InitSeq.Perform(*this, InitEntity, InitKind, None);
      MemberInit = MaybeCreateExprWithCleanups(MemberInit);
      // Note, MemberInit could actually come back empty if no initialization
      // is required (e.g., because it would call a trivial default constructor)
      if (!MemberInit.get() || MemberInit.isInvalid())
        continue;

      Member =
        new (Context) CXXCtorInitializer(Context, Field, SourceLocation(),
                                         SourceLocation(),
                                         MemberInit.getAs<Expr>(),
                                         SourceLocation());
      AllToInit.push_back(Member);

      // Be sure that the destructor is accessible and is marked as referenced.
      if (const RecordType *RecordTy =
              Context.getBaseElementType(Field->getType())
                  ->getAs<RecordType>()) {
        CXXRecordDecl *RD = cast<CXXRecordDecl>(RecordTy->getDecl());
        if (CXXDestructorDecl *Destructor = LookupDestructor(RD)) {
          MarkFunctionReferenced(Field->getLocation(), Destructor);
          CheckDestructorAccess(Field->getLocation(), Destructor,
                            PDiag(diag::err_access_dtor_ivar)
                              << Context.getBaseElementType(Field->getType()));
        }
      }
    }
    ObjCImplementation->setIvarInitializers(Context,
                                            AllToInit.data(), AllToInit.size());
  }
}

static
void DelegatingCycleHelper(CXXConstructorDecl* Ctor,
                           llvm::SmallPtrSet<CXXConstructorDecl*, 4> &Valid,
                           llvm::SmallPtrSet<CXXConstructorDecl*, 4> &Invalid,
                           llvm::SmallPtrSet<CXXConstructorDecl*, 4> &Current,
                           Sema &S) {
  if (Ctor->isInvalidDecl())
    return;

  CXXConstructorDecl *Target = Ctor->getTargetConstructor();

  // Target may not be determinable yet, for instance if this is a dependent
  // call in an uninstantiated template.
  if (Target) {
    const FunctionDecl *FNTarget = nullptr;
    (void)Target->hasBody(FNTarget);
    Target = const_cast<CXXConstructorDecl*>(
      cast_or_null<CXXConstructorDecl>(FNTarget));
  }

  CXXConstructorDecl *Canonical = Ctor->getCanonicalDecl(),
                     // Avoid dereferencing a null pointer here.
                     *TCanonical = Target? Target->getCanonicalDecl() : nullptr;

  if (!Current.insert(Canonical).second)
    return;

  // We know that beyond here, we aren't chaining into a cycle.
  if (!Target || !Target->isDelegatingConstructor() ||
      Target->isInvalidDecl() || Valid.count(TCanonical)) {
    Valid.insert(Current.begin(), Current.end());
    Current.clear();
  // We've hit a cycle.
  } else if (TCanonical == Canonical || Invalid.count(TCanonical) ||
             Current.count(TCanonical)) {
    // If we haven't diagnosed this cycle yet, do so now.
    if (!Invalid.count(TCanonical)) {
      S.Diag((*Ctor->init_begin())->getSourceLocation(),
             diag::warn_delegating_ctor_cycle)
        << Ctor;

      // Don't add a note for a function delegating directly to itself.
      if (TCanonical != Canonical)
        S.Diag(Target->getLocation(), diag::note_it_delegates_to);

      CXXConstructorDecl *C = Target;
      while (C->getCanonicalDecl() != Canonical) {
        const FunctionDecl *FNTarget = nullptr;
        (void)C->getTargetConstructor()->hasBody(FNTarget);
        assert(FNTarget && "Ctor cycle through bodiless function");

        C = const_cast<CXXConstructorDecl*>(
          cast<CXXConstructorDecl>(FNTarget));
        S.Diag(C->getLocation(), diag::note_which_delegates_to);
      }
    }

    Invalid.insert(Current.begin(), Current.end());
    Current.clear();
  } else {
    DelegatingCycleHelper(Target, Valid, Invalid, Current, S);
  }
}


void Sema::CheckDelegatingCtorCycles() {
  llvm::SmallPtrSet<CXXConstructorDecl*, 4> Valid, Invalid, Current;

  for (DelegatingCtorDeclsType::iterator
         I = DelegatingCtorDecls.begin(ExternalSource),
         E = DelegatingCtorDecls.end();
       I != E; ++I)
    DelegatingCycleHelper(*I, Valid, Invalid, Current, *this);

  for (auto CI = Invalid.begin(), CE = Invalid.end(); CI != CE; ++CI)
    (*CI)->setInvalidDecl();
}

namespace {
  /// AST visitor that finds references to the 'this' expression.
  class FindCXXThisExpr : public RecursiveASTVisitor<FindCXXThisExpr> {
    Sema &S;

  public:
    explicit FindCXXThisExpr(Sema &S) : S(S) { }

    bool VisitCXXThisExpr(CXXThisExpr *E) {
      S.Diag(E->getLocation(), diag::err_this_static_member_func)
        << E->isImplicit();
      return false;
    }
  };
}

bool Sema::checkThisInStaticMemberFunctionType(CXXMethodDecl *Method) {
  TypeSourceInfo *TSInfo = Method->getTypeSourceInfo();
  if (!TSInfo)
    return false;

  TypeLoc TL = TSInfo->getTypeLoc();
  FunctionProtoTypeLoc ProtoTL = TL.getAs<FunctionProtoTypeLoc>();
  if (!ProtoTL)
    return false;

  // C++11 [expr.prim.general]p3:
  //   [The expression this] shall not appear before the optional
  //   cv-qualifier-seq and it shall not appear within the declaration of a
  //   static member function (although its type and value category are defined
  //   within a static member function as they are within a non-static member
  //   function). [ Note: this is because declaration matching does not occur
  //  until the complete declarator is known. - end note ]
  const FunctionProtoType *Proto = ProtoTL.getTypePtr();
  FindCXXThisExpr Finder(*this);

  // If the return type came after the cv-qualifier-seq, check it now.
  if (Proto->hasTrailingReturn() &&
      !Finder.TraverseTypeLoc(ProtoTL.getReturnLoc()))
    return true;

  // Check the exception specification.
  if (checkThisInStaticMemberFunctionExceptionSpec(Method))
    return true;

  return checkThisInStaticMemberFunctionAttributes(Method);
}

bool Sema::checkThisInStaticMemberFunctionExceptionSpec(CXXMethodDecl *Method) {
  TypeSourceInfo *TSInfo = Method->getTypeSourceInfo();
  if (!TSInfo)
    return false;

  TypeLoc TL = TSInfo->getTypeLoc();
  FunctionProtoTypeLoc ProtoTL = TL.getAs<FunctionProtoTypeLoc>();
  if (!ProtoTL)
    return false;

  const FunctionProtoType *Proto = ProtoTL.getTypePtr();
  FindCXXThisExpr Finder(*this);

  switch (Proto->getExceptionSpecType()) {
  case EST_Unparsed:
  case EST_Uninstantiated:
  case EST_Unevaluated:
  case EST_BasicNoexcept:
  case EST_DynamicNone:
  case EST_MSAny:
  case EST_None:
    break;

  case EST_DependentNoexcept:
  case EST_NoexceptFalse:
  case EST_NoexceptTrue:
    if (!Finder.TraverseStmt(Proto->getNoexceptExpr()))
      return true;
    LLVM_FALLTHROUGH;

  case EST_Dynamic:
    for (const auto &E : Proto->exceptions()) {
      if (!Finder.TraverseType(E))
        return true;
    }
    break;
  }

  return false;
}

bool Sema::checkThisInStaticMemberFunctionAttributes(CXXMethodDecl *Method) {
  FindCXXThisExpr Finder(*this);

  // Check attributes.
  for (const auto *A : Method->attrs()) {
    // FIXME: This should be emitted by tblgen.
    Expr *Arg = nullptr;
    ArrayRef<Expr *> Args;
    if (const auto *G = dyn_cast<GuardedByAttr>(A))
      Arg = G->getArg();
    else if (const auto *G = dyn_cast<PtGuardedByAttr>(A))
      Arg = G->getArg();
    else if (const auto *AA = dyn_cast<AcquiredAfterAttr>(A))
      Args = llvm::makeArrayRef(AA->args_begin(), AA->args_size());
    else if (const auto *AB = dyn_cast<AcquiredBeforeAttr>(A))
      Args = llvm::makeArrayRef(AB->args_begin(), AB->args_size());
    else if (const auto *ETLF = dyn_cast<ExclusiveTrylockFunctionAttr>(A)) {
      Arg = ETLF->getSuccessValue();
      Args = llvm::makeArrayRef(ETLF->args_begin(), ETLF->args_size());
    } else if (const auto *STLF = dyn_cast<SharedTrylockFunctionAttr>(A)) {
      Arg = STLF->getSuccessValue();
      Args = llvm::makeArrayRef(STLF->args_begin(), STLF->args_size());
    } else if (const auto *LR = dyn_cast<LockReturnedAttr>(A))
      Arg = LR->getArg();
    else if (const auto *LE = dyn_cast<LocksExcludedAttr>(A))
      Args = llvm::makeArrayRef(LE->args_begin(), LE->args_size());
    else if (const auto *RC = dyn_cast<RequiresCapabilityAttr>(A))
      Args = llvm::makeArrayRef(RC->args_begin(), RC->args_size());
    else if (const auto *AC = dyn_cast<AcquireCapabilityAttr>(A))
      Args = llvm::makeArrayRef(AC->args_begin(), AC->args_size());
    else if (const auto *AC = dyn_cast<TryAcquireCapabilityAttr>(A))
      Args = llvm::makeArrayRef(AC->args_begin(), AC->args_size());
    else if (const auto *RC = dyn_cast<ReleaseCapabilityAttr>(A))
      Args = llvm::makeArrayRef(RC->args_begin(), RC->args_size());

    if (Arg && !Finder.TraverseStmt(Arg))
      return true;

    for (unsigned I = 0, N = Args.size(); I != N; ++I) {
      if (!Finder.TraverseStmt(Args[I]))
        return true;
    }
  }

  return false;
}

void Sema::checkExceptionSpecification(
    bool IsTopLevel, ExceptionSpecificationType EST,
    ArrayRef<ParsedType> DynamicExceptions,
    ArrayRef<SourceRange> DynamicExceptionRanges, Expr *NoexceptExpr,
    SmallVectorImpl<QualType> &Exceptions,
    FunctionProtoType::ExceptionSpecInfo &ESI) {
  Exceptions.clear();
  ESI.Type = EST;
  if (EST == EST_Dynamic) {
    Exceptions.reserve(DynamicExceptions.size());
    for (unsigned ei = 0, ee = DynamicExceptions.size(); ei != ee; ++ei) {
      // FIXME: Preserve type source info.
      QualType ET = GetTypeFromParser(DynamicExceptions[ei]);

      if (IsTopLevel) {
        SmallVector<UnexpandedParameterPack, 2> Unexpanded;
        collectUnexpandedParameterPacks(ET, Unexpanded);
        if (!Unexpanded.empty()) {
          DiagnoseUnexpandedParameterPacks(
              DynamicExceptionRanges[ei].getBegin(), UPPC_ExceptionType,
              Unexpanded);
          continue;
        }
      }

      // Check that the type is valid for an exception spec, and
      // drop it if not.
      if (!CheckSpecifiedExceptionType(ET, DynamicExceptionRanges[ei]))
        Exceptions.push_back(ET);
    }
    ESI.Exceptions = Exceptions;
    return;
  }

  if (isComputedNoexcept(EST)) {
    assert((NoexceptExpr->isTypeDependent() ||
            NoexceptExpr->getType()->getCanonicalTypeUnqualified() ==
            Context.BoolTy) &&
           "Parser should have made sure that the expression is boolean");
    if (IsTopLevel && DiagnoseUnexpandedParameterPack(NoexceptExpr)) {
      ESI.Type = EST_BasicNoexcept;
      return;
    }

    ESI.NoexceptExpr = NoexceptExpr;
    return;
  }
}

void Sema::actOnDelayedExceptionSpecification(Decl *MethodD,
             ExceptionSpecificationType EST,
             SourceRange SpecificationRange,
             ArrayRef<ParsedType> DynamicExceptions,
             ArrayRef<SourceRange> DynamicExceptionRanges,
             Expr *NoexceptExpr) {
  if (!MethodD)
    return;

  // Dig out the method we're referring to.
  if (FunctionTemplateDecl *FunTmpl = dyn_cast<FunctionTemplateDecl>(MethodD))
    MethodD = FunTmpl->getTemplatedDecl();

  CXXMethodDecl *Method = dyn_cast<CXXMethodDecl>(MethodD);
  if (!Method)
    return;

  // Check the exception specification.
  llvm::SmallVector<QualType, 4> Exceptions;
  FunctionProtoType::ExceptionSpecInfo ESI;
  checkExceptionSpecification(/*IsTopLevel*/true, EST, DynamicExceptions,
                              DynamicExceptionRanges, NoexceptExpr, Exceptions,
                              ESI);

  // Update the exception specification on the function type.
  Context.adjustExceptionSpec(Method, ESI, /*AsWritten*/true);

  if (Method->isStatic())
    checkThisInStaticMemberFunctionExceptionSpec(Method);

  if (Method->isVirtual()) {
    // Check overrides, which we previously had to delay.
    for (const CXXMethodDecl *O : Method->overridden_methods())
      CheckOverridingFunctionExceptionSpec(Method, O);
  }
}

/// HandleMSProperty - Analyze a __delcspec(property) field of a C++ class.
///
MSPropertyDecl *Sema::HandleMSProperty(Scope *S, RecordDecl *Record,
                                       SourceLocation DeclStart, Declarator &D,
                                       Expr *BitWidth,
                                       InClassInitStyle InitStyle,
                                       AccessSpecifier AS,
                                       const ParsedAttr &MSPropertyAttr) {
  IdentifierInfo *II = D.getIdentifier();
  if (!II) {
    Diag(DeclStart, diag::err_anonymous_property);
    return nullptr;
  }
  SourceLocation Loc = D.getIdentifierLoc();

  TypeSourceInfo *TInfo = GetTypeForDeclarator(D, S);
  QualType T = TInfo->getType();
  if (getLangOpts().CPlusPlus) {
    CheckExtraCXXDefaultArguments(D);

    if (DiagnoseUnexpandedParameterPack(D.getIdentifierLoc(), TInfo,
                                        UPPC_DataMemberType)) {
      D.setInvalidType();
      T = Context.IntTy;
      TInfo = Context.getTrivialTypeSourceInfo(T, Loc);
    }
  }

  DiagnoseFunctionSpecifiers(D.getDeclSpec());

  if (D.getDeclSpec().isInlineSpecified())
    Diag(D.getDeclSpec().getInlineSpecLoc(), diag::err_inline_non_function)
        << getLangOpts().CPlusPlus17;
  if (DeclSpec::TSCS TSCS = D.getDeclSpec().getThreadStorageClassSpec())
    Diag(D.getDeclSpec().getThreadStorageClassSpecLoc(),
         diag::err_invalid_thread)
      << DeclSpec::getSpecifierName(TSCS);

  // Check to see if this name was declared as a member previously
  NamedDecl *PrevDecl = nullptr;
  LookupResult Previous(*this, II, Loc, LookupMemberName,
                        ForVisibleRedeclaration);
  LookupName(Previous, S);
  switch (Previous.getResultKind()) {
  case LookupResult::Found:
  case LookupResult::FoundUnresolvedValue:
    PrevDecl = Previous.getAsSingle<NamedDecl>();
    break;

  case LookupResult::FoundOverloaded:
    PrevDecl = Previous.getRepresentativeDecl();
    break;

  case LookupResult::NotFound:
  case LookupResult::NotFoundInCurrentInstantiation:
  case LookupResult::Ambiguous:
    break;
  }

  if (PrevDecl && PrevDecl->isTemplateParameter()) {
    // Maybe we will complain about the shadowed template parameter.
    DiagnoseTemplateParameterShadow(D.getIdentifierLoc(), PrevDecl);
    // Just pretend that we didn't see the previous declaration.
    PrevDecl = nullptr;
  }

  if (PrevDecl && !isDeclInScope(PrevDecl, Record, S))
    PrevDecl = nullptr;

  SourceLocation TSSL = D.getBeginLoc();
  MSPropertyDecl *NewPD =
      MSPropertyDecl::Create(Context, Record, Loc, II, T, TInfo, TSSL,
                             MSPropertyAttr.getPropertyDataGetter(),
                             MSPropertyAttr.getPropertyDataSetter());
  ProcessDeclAttributes(TUScope, NewPD, D);
  NewPD->setAccess(AS);

  if (NewPD->isInvalidDecl())
    Record->setInvalidDecl();

  if (D.getDeclSpec().isModulePrivateSpecified())
    NewPD->setModulePrivate();

  if (NewPD->isInvalidDecl() && PrevDecl) {
    // Don't introduce NewFD into scope; there's already something
    // with the same name in the same scope.
  } else if (II) {
    PushOnScopeChains(NewPD, S);
  } else
    Record->addDecl(NewPD);

  return NewPD;
}
