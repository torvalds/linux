//===--- SemaExceptionSpec.cpp - C++ Exception Specifications ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides Sema routines for C++ exception specification testing.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/SemaInternal.h"
#include "clang/AST/ASTMutationListener.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"

namespace clang {

static const FunctionProtoType *GetUnderlyingFunction(QualType T)
{
  if (const PointerType *PtrTy = T->getAs<PointerType>())
    T = PtrTy->getPointeeType();
  else if (const ReferenceType *RefTy = T->getAs<ReferenceType>())
    T = RefTy->getPointeeType();
  else if (const MemberPointerType *MPTy = T->getAs<MemberPointerType>())
    T = MPTy->getPointeeType();
  return T->getAs<FunctionProtoType>();
}

/// HACK: libstdc++ has a bug where it shadows std::swap with a member
/// swap function then tries to call std::swap unqualified from the exception
/// specification of that function. This function detects whether we're in
/// such a case and turns off delay-parsing of exception specifications.
bool Sema::isLibstdcxxEagerExceptionSpecHack(const Declarator &D) {
  auto *RD = dyn_cast<CXXRecordDecl>(CurContext);

  // All the problem cases are member functions named "swap" within class
  // templates declared directly within namespace std or std::__debug or
  // std::__profile.
  if (!RD || !RD->getIdentifier() || !RD->getDescribedClassTemplate() ||
      !D.getIdentifier() || !D.getIdentifier()->isStr("swap"))
    return false;

  auto *ND = dyn_cast<NamespaceDecl>(RD->getDeclContext());
  if (!ND)
    return false;

  bool IsInStd = ND->isStdNamespace();
  if (!IsInStd) {
    // This isn't a direct member of namespace std, but it might still be
    // libstdc++'s std::__debug::array or std::__profile::array.
    IdentifierInfo *II = ND->getIdentifier();
    if (!II || !(II->isStr("__debug") || II->isStr("__profile")) ||
        !ND->isInStdNamespace())
      return false;
  }

  // Only apply this hack within a system header.
  if (!Context.getSourceManager().isInSystemHeader(D.getBeginLoc()))
    return false;

  return llvm::StringSwitch<bool>(RD->getIdentifier()->getName())
      .Case("array", true)
      .Case("pair", IsInStd)
      .Case("priority_queue", IsInStd)
      .Case("stack", IsInStd)
      .Case("queue", IsInStd)
      .Default(false);
}

ExprResult Sema::ActOnNoexceptSpec(SourceLocation NoexceptLoc,
                                   Expr *NoexceptExpr,
                                   ExceptionSpecificationType &EST) {
  // FIXME: This is bogus, a noexcept expression is not a condition.
  ExprResult Converted = CheckBooleanCondition(NoexceptLoc, NoexceptExpr);
  if (Converted.isInvalid())
    return Converted;

  if (Converted.get()->isValueDependent()) {
    EST = EST_DependentNoexcept;
    return Converted;
  }

  llvm::APSInt Result;
  Converted = VerifyIntegerConstantExpression(
      Converted.get(), &Result,
      diag::err_noexcept_needs_constant_expression,
      /*AllowFold*/ false);
  if (!Converted.isInvalid())
    EST = !Result ? EST_NoexceptFalse : EST_NoexceptTrue;
  return Converted;
}

/// CheckSpecifiedExceptionType - Check if the given type is valid in an
/// exception specification. Incomplete types, or pointers to incomplete types
/// other than void are not allowed.
///
/// \param[in,out] T  The exception type. This will be decayed to a pointer type
///                   when the input is an array or a function type.
bool Sema::CheckSpecifiedExceptionType(QualType &T, SourceRange Range) {
  // C++11 [except.spec]p2:
  //   A type cv T, "array of T", or "function returning T" denoted
  //   in an exception-specification is adjusted to type T, "pointer to T", or
  //   "pointer to function returning T", respectively.
  //
  // We also apply this rule in C++98.
  if (T->isArrayType())
    T = Context.getArrayDecayedType(T);
  else if (T->isFunctionType())
    T = Context.getPointerType(T);

  int Kind = 0;
  QualType PointeeT = T;
  if (const PointerType *PT = T->getAs<PointerType>()) {
    PointeeT = PT->getPointeeType();
    Kind = 1;

    // cv void* is explicitly permitted, despite being a pointer to an
    // incomplete type.
    if (PointeeT->isVoidType())
      return false;
  } else if (const ReferenceType *RT = T->getAs<ReferenceType>()) {
    PointeeT = RT->getPointeeType();
    Kind = 2;

    if (RT->isRValueReferenceType()) {
      // C++11 [except.spec]p2:
      //   A type denoted in an exception-specification shall not denote [...]
      //   an rvalue reference type.
      Diag(Range.getBegin(), diag::err_rref_in_exception_spec)
        << T << Range;
      return true;
    }
  }

  // C++11 [except.spec]p2:
  //   A type denoted in an exception-specification shall not denote an
  //   incomplete type other than a class currently being defined [...].
  //   A type denoted in an exception-specification shall not denote a
  //   pointer or reference to an incomplete type, other than (cv) void* or a
  //   pointer or reference to a class currently being defined.
  // In Microsoft mode, downgrade this to a warning.
  unsigned DiagID = diag::err_incomplete_in_exception_spec;
  bool ReturnValueOnError = true;
  if (getLangOpts().MicrosoftExt) {
    DiagID = diag::ext_incomplete_in_exception_spec;
    ReturnValueOnError = false;
  }
  if (!(PointeeT->isRecordType() &&
        PointeeT->getAs<RecordType>()->isBeingDefined()) &&
      RequireCompleteType(Range.getBegin(), PointeeT, DiagID, Kind, Range))
    return ReturnValueOnError;

  return false;
}

/// CheckDistantExceptionSpec - Check if the given type is a pointer or pointer
/// to member to a function with an exception specification. This means that
/// it is invalid to add another level of indirection.
bool Sema::CheckDistantExceptionSpec(QualType T) {
  // C++17 removes this rule in favor of putting exception specifications into
  // the type system.
  if (getLangOpts().CPlusPlus17)
    return false;

  if (const PointerType *PT = T->getAs<PointerType>())
    T = PT->getPointeeType();
  else if (const MemberPointerType *PT = T->getAs<MemberPointerType>())
    T = PT->getPointeeType();
  else
    return false;

  const FunctionProtoType *FnT = T->getAs<FunctionProtoType>();
  if (!FnT)
    return false;

  return FnT->hasExceptionSpec();
}

const FunctionProtoType *
Sema::ResolveExceptionSpec(SourceLocation Loc, const FunctionProtoType *FPT) {
  if (FPT->getExceptionSpecType() == EST_Unparsed) {
    Diag(Loc, diag::err_exception_spec_not_parsed);
    return nullptr;
  }

  if (!isUnresolvedExceptionSpec(FPT->getExceptionSpecType()))
    return FPT;

  FunctionDecl *SourceDecl = FPT->getExceptionSpecDecl();
  const FunctionProtoType *SourceFPT =
      SourceDecl->getType()->castAs<FunctionProtoType>();

  // If the exception specification has already been resolved, just return it.
  if (!isUnresolvedExceptionSpec(SourceFPT->getExceptionSpecType()))
    return SourceFPT;

  // Compute or instantiate the exception specification now.
  if (SourceFPT->getExceptionSpecType() == EST_Unevaluated)
    EvaluateImplicitExceptionSpec(Loc, cast<CXXMethodDecl>(SourceDecl));
  else
    InstantiateExceptionSpec(Loc, SourceDecl);

  const FunctionProtoType *Proto =
    SourceDecl->getType()->castAs<FunctionProtoType>();
  if (Proto->getExceptionSpecType() == clang::EST_Unparsed) {
    Diag(Loc, diag::err_exception_spec_not_parsed);
    Proto = nullptr;
  }
  return Proto;
}

void
Sema::UpdateExceptionSpec(FunctionDecl *FD,
                          const FunctionProtoType::ExceptionSpecInfo &ESI) {
  // If we've fully resolved the exception specification, notify listeners.
  if (!isUnresolvedExceptionSpec(ESI.Type))
    if (auto *Listener = getASTMutationListener())
      Listener->ResolvedExceptionSpec(FD);

  for (FunctionDecl *Redecl : FD->redecls())
    Context.adjustExceptionSpec(Redecl, ESI);
}

static bool exceptionSpecNotKnownYet(const FunctionDecl *FD) {
  auto *MD = dyn_cast<CXXMethodDecl>(FD);
  if (!MD)
    return false;

  auto EST = MD->getType()->castAs<FunctionProtoType>()->getExceptionSpecType();
  return EST == EST_Unparsed ||
         (EST == EST_Unevaluated && MD->getParent()->isBeingDefined());
}

static bool CheckEquivalentExceptionSpecImpl(
    Sema &S, const PartialDiagnostic &DiagID, const PartialDiagnostic &NoteID,
    const FunctionProtoType *Old, SourceLocation OldLoc,
    const FunctionProtoType *New, SourceLocation NewLoc,
    bool *MissingExceptionSpecification = nullptr,
    bool *MissingEmptyExceptionSpecification = nullptr,
    bool AllowNoexceptAllMatchWithNoSpec = false, bool IsOperatorNew = false);

/// Determine whether a function has an implicitly-generated exception
/// specification.
static bool hasImplicitExceptionSpec(FunctionDecl *Decl) {
  if (!isa<CXXDestructorDecl>(Decl) &&
      Decl->getDeclName().getCXXOverloadedOperator() != OO_Delete &&
      Decl->getDeclName().getCXXOverloadedOperator() != OO_Array_Delete)
    return false;

  // For a function that the user didn't declare:
  //  - if this is a destructor, its exception specification is implicit.
  //  - if this is 'operator delete' or 'operator delete[]', the exception
  //    specification is as-if an explicit exception specification was given
  //    (per [basic.stc.dynamic]p2).
  if (!Decl->getTypeSourceInfo())
    return isa<CXXDestructorDecl>(Decl);

  const FunctionProtoType *Ty =
    Decl->getTypeSourceInfo()->getType()->getAs<FunctionProtoType>();
  return !Ty->hasExceptionSpec();
}

bool Sema::CheckEquivalentExceptionSpec(FunctionDecl *Old, FunctionDecl *New) {
  // Just completely ignore this under -fno-exceptions prior to C++17.
  // In C++17 onwards, the exception specification is part of the type and
  // we will diagnose mismatches anyway, so it's better to check for them here.
  if (!getLangOpts().CXXExceptions && !getLangOpts().CPlusPlus17)
    return false;

  OverloadedOperatorKind OO = New->getDeclName().getCXXOverloadedOperator();
  bool IsOperatorNew = OO == OO_New || OO == OO_Array_New;
  bool MissingExceptionSpecification = false;
  bool MissingEmptyExceptionSpecification = false;

  unsigned DiagID = diag::err_mismatched_exception_spec;
  bool ReturnValueOnError = true;
  if (getLangOpts().MicrosoftExt) {
    DiagID = diag::ext_mismatched_exception_spec;
    ReturnValueOnError = false;
  }

  // If we're befriending a member function of a class that's currently being
  // defined, we might not be able to work out its exception specification yet.
  // If not, defer the check until later.
  if (exceptionSpecNotKnownYet(Old) || exceptionSpecNotKnownYet(New)) {
    DelayedEquivalentExceptionSpecChecks.push_back({New, Old});
    return false;
  }

  // Check the types as written: they must match before any exception
  // specification adjustment is applied.
  if (!CheckEquivalentExceptionSpecImpl(
        *this, PDiag(DiagID), PDiag(diag::note_previous_declaration),
        Old->getType()->getAs<FunctionProtoType>(), Old->getLocation(),
        New->getType()->getAs<FunctionProtoType>(), New->getLocation(),
        &MissingExceptionSpecification, &MissingEmptyExceptionSpecification,
        /*AllowNoexceptAllMatchWithNoSpec=*/true, IsOperatorNew)) {
    // C++11 [except.spec]p4 [DR1492]:
    //   If a declaration of a function has an implicit
    //   exception-specification, other declarations of the function shall
    //   not specify an exception-specification.
    if (getLangOpts().CPlusPlus11 && getLangOpts().CXXExceptions &&
        hasImplicitExceptionSpec(Old) != hasImplicitExceptionSpec(New)) {
      Diag(New->getLocation(), diag::ext_implicit_exception_spec_mismatch)
        << hasImplicitExceptionSpec(Old);
      if (Old->getLocation().isValid())
        Diag(Old->getLocation(), diag::note_previous_declaration);
    }
    return false;
  }

  // The failure was something other than an missing exception
  // specification; return an error, except in MS mode where this is a warning.
  if (!MissingExceptionSpecification)
    return ReturnValueOnError;

  const FunctionProtoType *NewProto =
    New->getType()->castAs<FunctionProtoType>();

  // The new function declaration is only missing an empty exception
  // specification "throw()". If the throw() specification came from a
  // function in a system header that has C linkage, just add an empty
  // exception specification to the "new" declaration. Note that C library
  // implementations are permitted to add these nothrow exception
  // specifications.
  //
  // Likewise if the old function is a builtin.
  if (MissingEmptyExceptionSpecification && NewProto &&
      (Old->getLocation().isInvalid() ||
       Context.getSourceManager().isInSystemHeader(Old->getLocation()) ||
       Old->getBuiltinID()) &&
      Old->isExternC()) {
    New->setType(Context.getFunctionType(
        NewProto->getReturnType(), NewProto->getParamTypes(),
        NewProto->getExtProtoInfo().withExceptionSpec(EST_DynamicNone)));
    return false;
  }

  const FunctionProtoType *OldProto =
    Old->getType()->castAs<FunctionProtoType>();

  FunctionProtoType::ExceptionSpecInfo ESI = OldProto->getExceptionSpecType();
  if (ESI.Type == EST_Dynamic) {
    // FIXME: What if the exceptions are described in terms of the old
    // prototype's parameters?
    ESI.Exceptions = OldProto->exceptions();
  }

  if (ESI.Type == EST_NoexceptFalse)
    ESI.Type = EST_None;
  if (ESI.Type == EST_NoexceptTrue)
    ESI.Type = EST_BasicNoexcept;

  // For dependent noexcept, we can't just take the expression from the old
  // prototype. It likely contains references to the old prototype's parameters.
  if (ESI.Type == EST_DependentNoexcept) {
    New->setInvalidDecl();
  } else {
    // Update the type of the function with the appropriate exception
    // specification.
    New->setType(Context.getFunctionType(
        NewProto->getReturnType(), NewProto->getParamTypes(),
        NewProto->getExtProtoInfo().withExceptionSpec(ESI)));
  }

  if (getLangOpts().MicrosoftExt && ESI.Type != EST_DependentNoexcept) {
    // Allow missing exception specifications in redeclarations as an extension.
    DiagID = diag::ext_ms_missing_exception_specification;
    ReturnValueOnError = false;
  } else if (New->isReplaceableGlobalAllocationFunction() &&
             ESI.Type != EST_DependentNoexcept) {
    // Allow missing exception specifications in redeclarations as an extension,
    // when declaring a replaceable global allocation function.
    DiagID = diag::ext_missing_exception_specification;
    ReturnValueOnError = false;
  } else {
    DiagID = diag::err_missing_exception_specification;
    ReturnValueOnError = true;
  }

  // Warn about the lack of exception specification.
  SmallString<128> ExceptionSpecString;
  llvm::raw_svector_ostream OS(ExceptionSpecString);
  switch (OldProto->getExceptionSpecType()) {
  case EST_DynamicNone:
    OS << "throw()";
    break;

  case EST_Dynamic: {
    OS << "throw(";
    bool OnFirstException = true;
    for (const auto &E : OldProto->exceptions()) {
      if (OnFirstException)
        OnFirstException = false;
      else
        OS << ", ";

      OS << E.getAsString(getPrintingPolicy());
    }
    OS << ")";
    break;
  }

  case EST_BasicNoexcept:
    OS << "noexcept";
    break;

  case EST_DependentNoexcept:
  case EST_NoexceptFalse:
  case EST_NoexceptTrue:
    OS << "noexcept(";
    assert(OldProto->getNoexceptExpr() != nullptr && "Expected non-null Expr");
    OldProto->getNoexceptExpr()->printPretty(OS, nullptr, getPrintingPolicy());
    OS << ")";
    break;

  default:
    llvm_unreachable("This spec type is compatible with none.");
  }

  SourceLocation FixItLoc;
  if (TypeSourceInfo *TSInfo = New->getTypeSourceInfo()) {
    TypeLoc TL = TSInfo->getTypeLoc().IgnoreParens();
    // FIXME: Preserve enough information so that we can produce a correct fixit
    // location when there is a trailing return type.
    if (auto FTLoc = TL.getAs<FunctionProtoTypeLoc>())
      if (!FTLoc.getTypePtr()->hasTrailingReturn())
        FixItLoc = getLocForEndOfToken(FTLoc.getLocalRangeEnd());
  }

  if (FixItLoc.isInvalid())
    Diag(New->getLocation(), DiagID)
      << New << OS.str();
  else {
    Diag(New->getLocation(), DiagID)
      << New << OS.str()
      << FixItHint::CreateInsertion(FixItLoc, " " + OS.str().str());
  }

  if (Old->getLocation().isValid())
    Diag(Old->getLocation(), diag::note_previous_declaration);

  return ReturnValueOnError;
}

/// CheckEquivalentExceptionSpec - Check if the two types have equivalent
/// exception specifications. Exception specifications are equivalent if
/// they allow exactly the same set of exception types. It does not matter how
/// that is achieved. See C++ [except.spec]p2.
bool Sema::CheckEquivalentExceptionSpec(
    const FunctionProtoType *Old, SourceLocation OldLoc,
    const FunctionProtoType *New, SourceLocation NewLoc) {
  if (!getLangOpts().CXXExceptions)
    return false;

  unsigned DiagID = diag::err_mismatched_exception_spec;
  if (getLangOpts().MicrosoftExt)
    DiagID = diag::ext_mismatched_exception_spec;
  bool Result = CheckEquivalentExceptionSpecImpl(
      *this, PDiag(DiagID), PDiag(diag::note_previous_declaration),
      Old, OldLoc, New, NewLoc);

  // In Microsoft mode, mismatching exception specifications just cause a warning.
  if (getLangOpts().MicrosoftExt)
    return false;
  return Result;
}

/// CheckEquivalentExceptionSpec - Check if the two types have compatible
/// exception specifications. See C++ [except.spec]p3.
///
/// \return \c false if the exception specifications match, \c true if there is
/// a problem. If \c true is returned, either a diagnostic has already been
/// produced or \c *MissingExceptionSpecification is set to \c true.
static bool CheckEquivalentExceptionSpecImpl(
    Sema &S, const PartialDiagnostic &DiagID, const PartialDiagnostic &NoteID,
    const FunctionProtoType *Old, SourceLocation OldLoc,
    const FunctionProtoType *New, SourceLocation NewLoc,
    bool *MissingExceptionSpecification,
    bool *MissingEmptyExceptionSpecification,
    bool AllowNoexceptAllMatchWithNoSpec, bool IsOperatorNew) {
  if (MissingExceptionSpecification)
    *MissingExceptionSpecification = false;

  if (MissingEmptyExceptionSpecification)
    *MissingEmptyExceptionSpecification = false;

  Old = S.ResolveExceptionSpec(NewLoc, Old);
  if (!Old)
    return false;
  New = S.ResolveExceptionSpec(NewLoc, New);
  if (!New)
    return false;

  // C++0x [except.spec]p3: Two exception-specifications are compatible if:
  //   - both are non-throwing, regardless of their form,
  //   - both have the form noexcept(constant-expression) and the constant-
  //     expressions are equivalent,
  //   - both are dynamic-exception-specifications that have the same set of
  //     adjusted types.
  //
  // C++0x [except.spec]p12: An exception-specification is non-throwing if it is
  //   of the form throw(), noexcept, or noexcept(constant-expression) where the
  //   constant-expression yields true.
  //
  // C++0x [except.spec]p4: If any declaration of a function has an exception-
  //   specifier that is not a noexcept-specification allowing all exceptions,
  //   all declarations [...] of that function shall have a compatible
  //   exception-specification.
  //
  // That last point basically means that noexcept(false) matches no spec.
  // It's considered when AllowNoexceptAllMatchWithNoSpec is true.

  ExceptionSpecificationType OldEST = Old->getExceptionSpecType();
  ExceptionSpecificationType NewEST = New->getExceptionSpecType();

  assert(!isUnresolvedExceptionSpec(OldEST) &&
         !isUnresolvedExceptionSpec(NewEST) &&
         "Shouldn't see unknown exception specifications here");

  CanThrowResult OldCanThrow = Old->canThrow();
  CanThrowResult NewCanThrow = New->canThrow();

  // Any non-throwing specifications are compatible.
  if (OldCanThrow == CT_Cannot && NewCanThrow == CT_Cannot)
    return false;

  // Any throws-anything specifications are usually compatible.
  if (OldCanThrow == CT_Can && OldEST != EST_Dynamic &&
      NewCanThrow == CT_Can && NewEST != EST_Dynamic) {
    // The exception is that the absence of an exception specification only
    // matches noexcept(false) for functions, as described above.
    if (!AllowNoexceptAllMatchWithNoSpec &&
        ((OldEST == EST_None && NewEST == EST_NoexceptFalse) ||
         (OldEST == EST_NoexceptFalse && NewEST == EST_None))) {
      // This is the disallowed case.
    } else {
      return false;
    }
  }

  // C++14 [except.spec]p3:
  //   Two exception-specifications are compatible if [...] both have the form
  //   noexcept(constant-expression) and the constant-expressions are equivalent
  if (OldEST == EST_DependentNoexcept && NewEST == EST_DependentNoexcept) {
    llvm::FoldingSetNodeID OldFSN, NewFSN;
    Old->getNoexceptExpr()->Profile(OldFSN, S.Context, true);
    New->getNoexceptExpr()->Profile(NewFSN, S.Context, true);
    if (OldFSN == NewFSN)
      return false;
  }

  // Dynamic exception specifications with the same set of adjusted types
  // are compatible.
  if (OldEST == EST_Dynamic && NewEST == EST_Dynamic) {
    bool Success = true;
    // Both have a dynamic exception spec. Collect the first set, then compare
    // to the second.
    llvm::SmallPtrSet<CanQualType, 8> OldTypes, NewTypes;
    for (const auto &I : Old->exceptions())
      OldTypes.insert(S.Context.getCanonicalType(I).getUnqualifiedType());

    for (const auto &I : New->exceptions()) {
      CanQualType TypePtr = S.Context.getCanonicalType(I).getUnqualifiedType();
      if (OldTypes.count(TypePtr))
        NewTypes.insert(TypePtr);
      else {
        Success = false;
        break;
      }
    }

    if (Success && OldTypes.size() == NewTypes.size())
      return false;
  }

  // As a special compatibility feature, under C++0x we accept no spec and
  // throw(std::bad_alloc) as equivalent for operator new and operator new[].
  // This is because the implicit declaration changed, but old code would break.
  if (S.getLangOpts().CPlusPlus11 && IsOperatorNew) {
    const FunctionProtoType *WithExceptions = nullptr;
    if (OldEST == EST_None && NewEST == EST_Dynamic)
      WithExceptions = New;
    else if (OldEST == EST_Dynamic && NewEST == EST_None)
      WithExceptions = Old;
    if (WithExceptions && WithExceptions->getNumExceptions() == 1) {
      // One has no spec, the other throw(something). If that something is
      // std::bad_alloc, all conditions are met.
      QualType Exception = *WithExceptions->exception_begin();
      if (CXXRecordDecl *ExRecord = Exception->getAsCXXRecordDecl()) {
        IdentifierInfo* Name = ExRecord->getIdentifier();
        if (Name && Name->getName() == "bad_alloc") {
          // It's called bad_alloc, but is it in std?
          if (ExRecord->isInStdNamespace()) {
            return false;
          }
        }
      }
    }
  }

  // If the caller wants to handle the case that the new function is
  // incompatible due to a missing exception specification, let it.
  if (MissingExceptionSpecification && OldEST != EST_None &&
      NewEST == EST_None) {
    // The old type has an exception specification of some sort, but
    // the new type does not.
    *MissingExceptionSpecification = true;

    if (MissingEmptyExceptionSpecification && OldCanThrow == CT_Cannot) {
      // The old type has a throw() or noexcept(true) exception specification
      // and the new type has no exception specification, and the caller asked
      // to handle this itself.
      *MissingEmptyExceptionSpecification = true;
    }

    return true;
  }

  S.Diag(NewLoc, DiagID);
  if (NoteID.getDiagID() != 0 && OldLoc.isValid())
    S.Diag(OldLoc, NoteID);
  return true;
}

bool Sema::CheckEquivalentExceptionSpec(const PartialDiagnostic &DiagID,
                                        const PartialDiagnostic &NoteID,
                                        const FunctionProtoType *Old,
                                        SourceLocation OldLoc,
                                        const FunctionProtoType *New,
                                        SourceLocation NewLoc) {
  if (!getLangOpts().CXXExceptions)
    return false;
  return CheckEquivalentExceptionSpecImpl(*this, DiagID, NoteID, Old, OldLoc,
                                          New, NewLoc);
}

bool Sema::handlerCanCatch(QualType HandlerType, QualType ExceptionType) {
  // [except.handle]p3:
  //   A handler is a match for an exception object of type E if:

  // HandlerType must be ExceptionType or derived from it, or pointer or
  // reference to such types.
  const ReferenceType *RefTy = HandlerType->getAs<ReferenceType>();
  if (RefTy)
    HandlerType = RefTy->getPointeeType();

  //   -- the handler is of type cv T or cv T& and E and T are the same type
  if (Context.hasSameUnqualifiedType(ExceptionType, HandlerType))
    return true;

  // FIXME: ObjC pointer types?
  if (HandlerType->isPointerType() || HandlerType->isMemberPointerType()) {
    if (RefTy && (!HandlerType.isConstQualified() ||
                  HandlerType.isVolatileQualified()))
      return false;

    // -- the handler is of type cv T or const T& where T is a pointer or
    //    pointer to member type and E is std::nullptr_t
    if (ExceptionType->isNullPtrType())
      return true;

    // -- the handler is of type cv T or const T& where T is a pointer or
    //    pointer to member type and E is a pointer or pointer to member type
    //    that can be converted to T by one or more of
    //    -- a qualification conversion
    //    -- a function pointer conversion
    bool LifetimeConv;
    QualType Result;
    // FIXME: Should we treat the exception as catchable if a lifetime
    // conversion is required?
    if (IsQualificationConversion(ExceptionType, HandlerType, false,
                                  LifetimeConv) ||
        IsFunctionConversion(ExceptionType, HandlerType, Result))
      return true;

    //    -- a standard pointer conversion [...]
    if (!ExceptionType->isPointerType() || !HandlerType->isPointerType())
      return false;

    // Handle the "qualification conversion" portion.
    Qualifiers EQuals, HQuals;
    ExceptionType = Context.getUnqualifiedArrayType(
        ExceptionType->getPointeeType(), EQuals);
    HandlerType = Context.getUnqualifiedArrayType(
        HandlerType->getPointeeType(), HQuals);
    if (!HQuals.compatiblyIncludes(EQuals))
      return false;

    if (HandlerType->isVoidType() && ExceptionType->isObjectType())
      return true;

    // The only remaining case is a derived-to-base conversion.
  }

  //   -- the handler is of type cg T or cv T& and T is an unambiguous public
  //      base class of E
  if (!ExceptionType->isRecordType() || !HandlerType->isRecordType())
    return false;
  CXXBasePaths Paths(/*FindAmbiguities=*/true, /*RecordPaths=*/true,
                     /*DetectVirtual=*/false);
  if (!IsDerivedFrom(SourceLocation(), ExceptionType, HandlerType, Paths) ||
      Paths.isAmbiguous(Context.getCanonicalType(HandlerType)))
    return false;

  // Do this check from a context without privileges.
  switch (CheckBaseClassAccess(SourceLocation(), HandlerType, ExceptionType,
                               Paths.front(),
                               /*Diagnostic*/ 0,
                               /*ForceCheck*/ true,
                               /*ForceUnprivileged*/ true)) {
  case AR_accessible: return true;
  case AR_inaccessible: return false;
  case AR_dependent:
    llvm_unreachable("access check dependent for unprivileged context");
  case AR_delayed:
    llvm_unreachable("access check delayed in non-declaration");
  }
  llvm_unreachable("unexpected access check result");
}

/// CheckExceptionSpecSubset - Check whether the second function type's
/// exception specification is a subset (or equivalent) of the first function
/// type. This is used by override and pointer assignment checks.
bool Sema::CheckExceptionSpecSubset(const PartialDiagnostic &DiagID,
                                    const PartialDiagnostic &NestedDiagID,
                                    const PartialDiagnostic &NoteID,
                                    const FunctionProtoType *Superset,
                                    SourceLocation SuperLoc,
                                    const FunctionProtoType *Subset,
                                    SourceLocation SubLoc) {

  // Just auto-succeed under -fno-exceptions.
  if (!getLangOpts().CXXExceptions)
    return false;

  // FIXME: As usual, we could be more specific in our error messages, but
  // that better waits until we've got types with source locations.

  if (!SubLoc.isValid())
    SubLoc = SuperLoc;

  // Resolve the exception specifications, if needed.
  Superset = ResolveExceptionSpec(SuperLoc, Superset);
  if (!Superset)
    return false;
  Subset = ResolveExceptionSpec(SubLoc, Subset);
  if (!Subset)
    return false;

  ExceptionSpecificationType SuperEST = Superset->getExceptionSpecType();
  ExceptionSpecificationType SubEST = Subset->getExceptionSpecType();
  assert(!isUnresolvedExceptionSpec(SuperEST) &&
         !isUnresolvedExceptionSpec(SubEST) &&
         "Shouldn't see unknown exception specifications here");

  // If there are dependent noexcept specs, assume everything is fine. Unlike
  // with the equivalency check, this is safe in this case, because we don't
  // want to merge declarations. Checks after instantiation will catch any
  // omissions we make here.
  if (SuperEST == EST_DependentNoexcept || SubEST == EST_DependentNoexcept)
    return false;

  CanThrowResult SuperCanThrow = Superset->canThrow();
  CanThrowResult SubCanThrow = Subset->canThrow();

  // If the superset contains everything or the subset contains nothing, we're
  // done.
  if ((SuperCanThrow == CT_Can && SuperEST != EST_Dynamic) ||
      SubCanThrow == CT_Cannot)
    return CheckParamExceptionSpec(NestedDiagID, NoteID, Superset, SuperLoc,
                                   Subset, SubLoc);

  // If the subset contains everything or the superset contains nothing, we've
  // failed.
  if ((SubCanThrow == CT_Can && SubEST != EST_Dynamic) ||
      SuperCanThrow == CT_Cannot) {
    Diag(SubLoc, DiagID);
    if (NoteID.getDiagID() != 0)
      Diag(SuperLoc, NoteID);
    return true;
  }

  assert(SuperEST == EST_Dynamic && SubEST == EST_Dynamic &&
         "Exception spec subset: non-dynamic case slipped through.");

  // Neither contains everything or nothing. Do a proper comparison.
  for (QualType SubI : Subset->exceptions()) {
    if (const ReferenceType *RefTy = SubI->getAs<ReferenceType>())
      SubI = RefTy->getPointeeType();

    // Make sure it's in the superset.
    bool Contained = false;
    for (QualType SuperI : Superset->exceptions()) {
      // [except.spec]p5:
      //   the target entity shall allow at least the exceptions allowed by the
      //   source
      //
      // We interpret this as meaning that a handler for some target type would
      // catch an exception of each source type.
      if (handlerCanCatch(SuperI, SubI)) {
        Contained = true;
        break;
      }
    }
    if (!Contained) {
      Diag(SubLoc, DiagID);
      if (NoteID.getDiagID() != 0)
        Diag(SuperLoc, NoteID);
      return true;
    }
  }
  // We've run half the gauntlet.
  return CheckParamExceptionSpec(NestedDiagID, NoteID, Superset, SuperLoc,
                                 Subset, SubLoc);
}

static bool
CheckSpecForTypesEquivalent(Sema &S, const PartialDiagnostic &DiagID,
                            const PartialDiagnostic &NoteID, QualType Target,
                            SourceLocation TargetLoc, QualType Source,
                            SourceLocation SourceLoc) {
  const FunctionProtoType *TFunc = GetUnderlyingFunction(Target);
  if (!TFunc)
    return false;
  const FunctionProtoType *SFunc = GetUnderlyingFunction(Source);
  if (!SFunc)
    return false;

  return S.CheckEquivalentExceptionSpec(DiagID, NoteID, TFunc, TargetLoc,
                                        SFunc, SourceLoc);
}

/// CheckParamExceptionSpec - Check if the parameter and return types of the
/// two functions have equivalent exception specs. This is part of the
/// assignment and override compatibility check. We do not check the parameters
/// of parameter function pointers recursively, as no sane programmer would
/// even be able to write such a function type.
bool Sema::CheckParamExceptionSpec(const PartialDiagnostic &DiagID,
                                   const PartialDiagnostic &NoteID,
                                   const FunctionProtoType *Target,
                                   SourceLocation TargetLoc,
                                   const FunctionProtoType *Source,
                                   SourceLocation SourceLoc) {
  auto RetDiag = DiagID;
  RetDiag << 0;
  if (CheckSpecForTypesEquivalent(
          *this, RetDiag, PDiag(),
          Target->getReturnType(), TargetLoc, Source->getReturnType(),
          SourceLoc))
    return true;

  // We shouldn't even be testing this unless the arguments are otherwise
  // compatible.
  assert(Target->getNumParams() == Source->getNumParams() &&
         "Functions have different argument counts.");
  for (unsigned i = 0, E = Target->getNumParams(); i != E; ++i) {
    auto ParamDiag = DiagID;
    ParamDiag << 1;
    if (CheckSpecForTypesEquivalent(
            *this, ParamDiag, PDiag(),
            Target->getParamType(i), TargetLoc, Source->getParamType(i),
            SourceLoc))
      return true;
  }
  return false;
}

bool Sema::CheckExceptionSpecCompatibility(Expr *From, QualType ToType) {
  // First we check for applicability.
  // Target type must be a function, function pointer or function reference.
  const FunctionProtoType *ToFunc = GetUnderlyingFunction(ToType);
  if (!ToFunc || ToFunc->hasDependentExceptionSpec())
    return false;

  // SourceType must be a function or function pointer.
  const FunctionProtoType *FromFunc = GetUnderlyingFunction(From->getType());
  if (!FromFunc || FromFunc->hasDependentExceptionSpec())
    return false;

  unsigned DiagID = diag::err_incompatible_exception_specs;
  unsigned NestedDiagID = diag::err_deep_exception_specs_differ;
  // This is not an error in C++17 onwards, unless the noexceptness doesn't
  // match, but in that case we have a full-on type mismatch, not just a
  // type sugar mismatch.
  if (getLangOpts().CPlusPlus17) {
    DiagID = diag::warn_incompatible_exception_specs;
    NestedDiagID = diag::warn_deep_exception_specs_differ;
  }

  // Now we've got the correct types on both sides, check their compatibility.
  // This means that the source of the conversion can only throw a subset of
  // the exceptions of the target, and any exception specs on arguments or
  // return types must be equivalent.
  //
  // FIXME: If there is a nested dependent exception specification, we should
  // not be checking it here. This is fine:
  //   template<typename T> void f() {
  //     void (*p)(void (*) throw(T));
  //     void (*q)(void (*) throw(int)) = p;
  //   }
  // ... because it might be instantiated with T=int.
  return CheckExceptionSpecSubset(PDiag(DiagID), PDiag(NestedDiagID), PDiag(),
                                  ToFunc, From->getSourceRange().getBegin(),
                                  FromFunc, SourceLocation()) &&
         !getLangOpts().CPlusPlus17;
}

bool Sema::CheckOverridingFunctionExceptionSpec(const CXXMethodDecl *New,
                                                const CXXMethodDecl *Old) {
  // If the new exception specification hasn't been parsed yet, skip the check.
  // We'll get called again once it's been parsed.
  if (New->getType()->castAs<FunctionProtoType>()->getExceptionSpecType() ==
      EST_Unparsed)
    return false;

  // Don't check uninstantiated template destructors at all. We can only
  // synthesize correct specs after the template is instantiated.
  if (isa<CXXDestructorDecl>(New) && New->getParent()->isDependentType())
    return false;

  // If the old exception specification hasn't been parsed yet, or the new
  // exception specification can't be computed yet, remember that we need to
  // perform this check when we get to the end of the outermost
  // lexically-surrounding class.
  if (exceptionSpecNotKnownYet(Old) || exceptionSpecNotKnownYet(New)) {
    DelayedOverridingExceptionSpecChecks.push_back({New, Old});
    return false;
  }

  unsigned DiagID = diag::err_override_exception_spec;
  if (getLangOpts().MicrosoftExt)
    DiagID = diag::ext_override_exception_spec;
  return CheckExceptionSpecSubset(PDiag(DiagID),
                                  PDiag(diag::err_deep_exception_specs_differ),
                                  PDiag(diag::note_overridden_virtual_function),
                                  Old->getType()->getAs<FunctionProtoType>(),
                                  Old->getLocation(),
                                  New->getType()->getAs<FunctionProtoType>(),
                                  New->getLocation());
}

static CanThrowResult canSubExprsThrow(Sema &S, const Expr *E) {
  CanThrowResult R = CT_Cannot;
  for (const Stmt *SubStmt : E->children()) {
    R = mergeCanThrow(R, S.canThrow(cast<Expr>(SubStmt)));
    if (R == CT_Can)
      break;
  }
  return R;
}

static CanThrowResult canCalleeThrow(Sema &S, const Expr *E, const Decl *D) {
  // As an extension, we assume that __attribute__((nothrow)) functions don't
  // throw.
  if (D && isa<FunctionDecl>(D) && D->hasAttr<NoThrowAttr>())
    return CT_Cannot;

  QualType T;

  // In C++1z, just look at the function type of the callee.
  if (S.getLangOpts().CPlusPlus17 && isa<CallExpr>(E)) {
    E = cast<CallExpr>(E)->getCallee();
    T = E->getType();
    if (T->isSpecificPlaceholderType(BuiltinType::BoundMember)) {
      // Sadly we don't preserve the actual type as part of the "bound member"
      // placeholder, so we need to reconstruct it.
      E = E->IgnoreParenImpCasts();

      // Could be a call to a pointer-to-member or a plain member access.
      if (auto *Op = dyn_cast<BinaryOperator>(E)) {
        assert(Op->getOpcode() == BO_PtrMemD || Op->getOpcode() == BO_PtrMemI);
        T = Op->getRHS()->getType()
              ->castAs<MemberPointerType>()->getPointeeType();
      } else {
        T = cast<MemberExpr>(E)->getMemberDecl()->getType();
      }
    }
  } else if (const ValueDecl *VD = dyn_cast_or_null<ValueDecl>(D))
    T = VD->getType();
  else
    // If we have no clue what we're calling, assume the worst.
    return CT_Can;

  const FunctionProtoType *FT;
  if ((FT = T->getAs<FunctionProtoType>())) {
  } else if (const PointerType *PT = T->getAs<PointerType>())
    FT = PT->getPointeeType()->getAs<FunctionProtoType>();
  else if (const ReferenceType *RT = T->getAs<ReferenceType>())
    FT = RT->getPointeeType()->getAs<FunctionProtoType>();
  else if (const MemberPointerType *MT = T->getAs<MemberPointerType>())
    FT = MT->getPointeeType()->getAs<FunctionProtoType>();
  else if (const BlockPointerType *BT = T->getAs<BlockPointerType>())
    FT = BT->getPointeeType()->getAs<FunctionProtoType>();

  if (!FT)
    return CT_Can;

  FT = S.ResolveExceptionSpec(E->getBeginLoc(), FT);
  if (!FT)
    return CT_Can;

  return FT->canThrow();
}

static CanThrowResult canDynamicCastThrow(const CXXDynamicCastExpr *DC) {
  if (DC->isTypeDependent())
    return CT_Dependent;

  if (!DC->getTypeAsWritten()->isReferenceType())
    return CT_Cannot;

  if (DC->getSubExpr()->isTypeDependent())
    return CT_Dependent;

  return DC->getCastKind() == clang::CK_Dynamic? CT_Can : CT_Cannot;
}

static CanThrowResult canTypeidThrow(Sema &S, const CXXTypeidExpr *DC) {
  if (DC->isTypeOperand())
    return CT_Cannot;

  Expr *Op = DC->getExprOperand();
  if (Op->isTypeDependent())
    return CT_Dependent;

  const RecordType *RT = Op->getType()->getAs<RecordType>();
  if (!RT)
    return CT_Cannot;

  if (!cast<CXXRecordDecl>(RT->getDecl())->isPolymorphic())
    return CT_Cannot;

  if (Op->Classify(S.Context).isPRValue())
    return CT_Cannot;

  return CT_Can;
}

CanThrowResult Sema::canThrow(const Expr *E) {
  // C++ [expr.unary.noexcept]p3:
  //   [Can throw] if in a potentially-evaluated context the expression would
  //   contain:
  switch (E->getStmtClass()) {
  case Expr::ConstantExprClass:
    return canThrow(cast<ConstantExpr>(E)->getSubExpr());

  case Expr::CXXThrowExprClass:
    //   - a potentially evaluated throw-expression
    return CT_Can;

  case Expr::CXXDynamicCastExprClass: {
    //   - a potentially evaluated dynamic_cast expression dynamic_cast<T>(v),
    //     where T is a reference type, that requires a run-time check
    CanThrowResult CT = canDynamicCastThrow(cast<CXXDynamicCastExpr>(E));
    if (CT == CT_Can)
      return CT;
    return mergeCanThrow(CT, canSubExprsThrow(*this, E));
  }

  case Expr::CXXTypeidExprClass:
    //   - a potentially evaluated typeid expression applied to a glvalue
    //     expression whose type is a polymorphic class type
    return canTypeidThrow(*this, cast<CXXTypeidExpr>(E));

    //   - a potentially evaluated call to a function, member function, function
    //     pointer, or member function pointer that does not have a non-throwing
    //     exception-specification
  case Expr::CallExprClass:
  case Expr::CXXMemberCallExprClass:
  case Expr::CXXOperatorCallExprClass:
  case Expr::UserDefinedLiteralClass: {
    const CallExpr *CE = cast<CallExpr>(E);
    CanThrowResult CT;
    if (E->isTypeDependent())
      CT = CT_Dependent;
    else if (isa<CXXPseudoDestructorExpr>(CE->getCallee()->IgnoreParens()))
      CT = CT_Cannot;
    else
      CT = canCalleeThrow(*this, E, CE->getCalleeDecl());
    if (CT == CT_Can)
      return CT;
    return mergeCanThrow(CT, canSubExprsThrow(*this, E));
  }

  case Expr::CXXConstructExprClass:
  case Expr::CXXTemporaryObjectExprClass: {
    CanThrowResult CT = canCalleeThrow(*this, E,
        cast<CXXConstructExpr>(E)->getConstructor());
    if (CT == CT_Can)
      return CT;
    return mergeCanThrow(CT, canSubExprsThrow(*this, E));
  }

  case Expr::CXXInheritedCtorInitExprClass:
    return canCalleeThrow(*this, E,
                          cast<CXXInheritedCtorInitExpr>(E)->getConstructor());

  case Expr::LambdaExprClass: {
    const LambdaExpr *Lambda = cast<LambdaExpr>(E);
    CanThrowResult CT = CT_Cannot;
    for (LambdaExpr::const_capture_init_iterator
             Cap = Lambda->capture_init_begin(),
             CapEnd = Lambda->capture_init_end();
         Cap != CapEnd; ++Cap)
      CT = mergeCanThrow(CT, canThrow(*Cap));
    return CT;
  }

  case Expr::CXXNewExprClass: {
    CanThrowResult CT;
    if (E->isTypeDependent())
      CT = CT_Dependent;
    else
      CT = canCalleeThrow(*this, E, cast<CXXNewExpr>(E)->getOperatorNew());
    if (CT == CT_Can)
      return CT;
    return mergeCanThrow(CT, canSubExprsThrow(*this, E));
  }

  case Expr::CXXDeleteExprClass: {
    CanThrowResult CT;
    QualType DTy = cast<CXXDeleteExpr>(E)->getDestroyedType();
    if (DTy.isNull() || DTy->isDependentType()) {
      CT = CT_Dependent;
    } else {
      CT = canCalleeThrow(*this, E,
                          cast<CXXDeleteExpr>(E)->getOperatorDelete());
      if (const RecordType *RT = DTy->getAs<RecordType>()) {
        const CXXRecordDecl *RD = cast<CXXRecordDecl>(RT->getDecl());
        const CXXDestructorDecl *DD = RD->getDestructor();
        if (DD)
          CT = mergeCanThrow(CT, canCalleeThrow(*this, E, DD));
      }
      if (CT == CT_Can)
        return CT;
    }
    return mergeCanThrow(CT, canSubExprsThrow(*this, E));
  }

  case Expr::CXXBindTemporaryExprClass: {
    // The bound temporary has to be destroyed again, which might throw.
    CanThrowResult CT = canCalleeThrow(*this, E,
      cast<CXXBindTemporaryExpr>(E)->getTemporary()->getDestructor());
    if (CT == CT_Can)
      return CT;
    return mergeCanThrow(CT, canSubExprsThrow(*this, E));
  }

    // ObjC message sends are like function calls, but never have exception
    // specs.
  case Expr::ObjCMessageExprClass:
  case Expr::ObjCPropertyRefExprClass:
  case Expr::ObjCSubscriptRefExprClass:
    return CT_Can;

    // All the ObjC literals that are implemented as calls are
    // potentially throwing unless we decide to close off that
    // possibility.
  case Expr::ObjCArrayLiteralClass:
  case Expr::ObjCDictionaryLiteralClass:
  case Expr::ObjCBoxedExprClass:
    return CT_Can;

    // Many other things have subexpressions, so we have to test those.
    // Some are simple:
  case Expr::CoawaitExprClass:
  case Expr::ConditionalOperatorClass:
  case Expr::CompoundLiteralExprClass:
  case Expr::CoyieldExprClass:
  case Expr::CXXConstCastExprClass:
  case Expr::CXXReinterpretCastExprClass:
  case Expr::CXXStdInitializerListExprClass:
  case Expr::DesignatedInitExprClass:
  case Expr::DesignatedInitUpdateExprClass:
  case Expr::ExprWithCleanupsClass:
  case Expr::ExtVectorElementExprClass:
  case Expr::InitListExprClass:
  case Expr::ArrayInitLoopExprClass:
  case Expr::MemberExprClass:
  case Expr::ObjCIsaExprClass:
  case Expr::ObjCIvarRefExprClass:
  case Expr::ParenExprClass:
  case Expr::ParenListExprClass:
  case Expr::ShuffleVectorExprClass:
  case Expr::ConvertVectorExprClass:
  case Expr::VAArgExprClass:
    return canSubExprsThrow(*this, E);

    // Some might be dependent for other reasons.
  case Expr::ArraySubscriptExprClass:
  case Expr::OMPArraySectionExprClass:
  case Expr::BinaryOperatorClass:
  case Expr::DependentCoawaitExprClass:
  case Expr::CompoundAssignOperatorClass:
  case Expr::CStyleCastExprClass:
  case Expr::CXXStaticCastExprClass:
  case Expr::CXXFunctionalCastExprClass:
  case Expr::ImplicitCastExprClass:
  case Expr::MaterializeTemporaryExprClass:
  case Expr::UnaryOperatorClass: {
    CanThrowResult CT = E->isTypeDependent() ? CT_Dependent : CT_Cannot;
    return mergeCanThrow(CT, canSubExprsThrow(*this, E));
  }

    // FIXME: We should handle StmtExpr, but that opens a MASSIVE can of worms.
  case Expr::StmtExprClass:
    return CT_Can;

  case Expr::CXXDefaultArgExprClass:
    return canThrow(cast<CXXDefaultArgExpr>(E)->getExpr());

  case Expr::CXXDefaultInitExprClass:
    return canThrow(cast<CXXDefaultInitExpr>(E)->getExpr());

  case Expr::ChooseExprClass:
    if (E->isTypeDependent() || E->isValueDependent())
      return CT_Dependent;
    return canThrow(cast<ChooseExpr>(E)->getChosenSubExpr());

  case Expr::GenericSelectionExprClass:
    if (cast<GenericSelectionExpr>(E)->isResultDependent())
      return CT_Dependent;
    return canThrow(cast<GenericSelectionExpr>(E)->getResultExpr());

    // Some expressions are always dependent.
  case Expr::CXXDependentScopeMemberExprClass:
  case Expr::CXXUnresolvedConstructExprClass:
  case Expr::DependentScopeDeclRefExprClass:
  case Expr::CXXFoldExprClass:
    return CT_Dependent;

  case Expr::AsTypeExprClass:
  case Expr::BinaryConditionalOperatorClass:
  case Expr::BlockExprClass:
  case Expr::CUDAKernelCallExprClass:
  case Expr::DeclRefExprClass:
  case Expr::ObjCBridgedCastExprClass:
  case Expr::ObjCIndirectCopyRestoreExprClass:
  case Expr::ObjCProtocolExprClass:
  case Expr::ObjCSelectorExprClass:
  case Expr::ObjCAvailabilityCheckExprClass:
  case Expr::OffsetOfExprClass:
  case Expr::PackExpansionExprClass:
  case Expr::PseudoObjectExprClass:
  case Expr::SubstNonTypeTemplateParmExprClass:
  case Expr::SubstNonTypeTemplateParmPackExprClass:
  case Expr::FunctionParmPackExprClass:
  case Expr::UnaryExprOrTypeTraitExprClass:
  case Expr::UnresolvedLookupExprClass:
  case Expr::UnresolvedMemberExprClass:
  case Expr::TypoExprClass:
    // FIXME: Can any of the above throw?  If so, when?
    return CT_Cannot;

  case Expr::AddrLabelExprClass:
  case Expr::ArrayTypeTraitExprClass:
  case Expr::AtomicExprClass:
  case Expr::TypeTraitExprClass:
  case Expr::CXXBoolLiteralExprClass:
  case Expr::CXXNoexceptExprClass:
  case Expr::CXXNullPtrLiteralExprClass:
  case Expr::CXXPseudoDestructorExprClass:
  case Expr::CXXScalarValueInitExprClass:
  case Expr::CXXThisExprClass:
  case Expr::CXXUuidofExprClass:
  case Expr::CharacterLiteralClass:
  case Expr::ExpressionTraitExprClass:
  case Expr::FloatingLiteralClass:
  case Expr::GNUNullExprClass:
  case Expr::ImaginaryLiteralClass:
  case Expr::ImplicitValueInitExprClass:
  case Expr::IntegerLiteralClass:
  case Expr::FixedPointLiteralClass:
  case Expr::ArrayInitIndexExprClass:
  case Expr::NoInitExprClass:
  case Expr::ObjCEncodeExprClass:
  case Expr::ObjCStringLiteralClass:
  case Expr::ObjCBoolLiteralExprClass:
  case Expr::OpaqueValueExprClass:
  case Expr::PredefinedExprClass:
  case Expr::SizeOfPackExprClass:
  case Expr::StringLiteralClass:
    // These expressions can never throw.
    return CT_Cannot;

  case Expr::MSPropertyRefExprClass:
  case Expr::MSPropertySubscriptExprClass:
    llvm_unreachable("Invalid class for expression");

#define STMT(CLASS, PARENT) case Expr::CLASS##Class:
#define STMT_RANGE(Base, First, Last)
#define LAST_STMT_RANGE(BASE, FIRST, LAST)
#define EXPR(CLASS, PARENT)
#define ABSTRACT_STMT(STMT)
#include "clang/AST/StmtNodes.inc"
  case Expr::NoStmtClass:
    llvm_unreachable("Invalid class for expression");
  }
  llvm_unreachable("Bogus StmtClass");
}

} // end namespace clang
