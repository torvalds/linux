//===--- SemaCXXScopeSpec.cpp - Semantic Analysis for C++ scope specifiers-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements C++ semantic analysis for scope specifiers.
//
//===----------------------------------------------------------------------===//

#include "TypeLocBuilder.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/Basic/PartialDiagnostic.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/SemaInternal.h"
#include "clang/Sema/Template.h"
#include "llvm/ADT/STLExtras.h"
using namespace clang;

/// Find the current instantiation that associated with the given type.
static CXXRecordDecl *getCurrentInstantiationOf(QualType T,
                                                DeclContext *CurContext) {
  if (T.isNull())
    return nullptr;

  const Type *Ty = T->getCanonicalTypeInternal().getTypePtr();
  if (const RecordType *RecordTy = dyn_cast<RecordType>(Ty)) {
    CXXRecordDecl *Record = cast<CXXRecordDecl>(RecordTy->getDecl());
    if (!Record->isDependentContext() ||
        Record->isCurrentInstantiation(CurContext))
      return Record;

    return nullptr;
  } else if (isa<InjectedClassNameType>(Ty))
    return cast<InjectedClassNameType>(Ty)->getDecl();
  else
    return nullptr;
}

/// Compute the DeclContext that is associated with the given type.
///
/// \param T the type for which we are attempting to find a DeclContext.
///
/// \returns the declaration context represented by the type T,
/// or NULL if the declaration context cannot be computed (e.g., because it is
/// dependent and not the current instantiation).
DeclContext *Sema::computeDeclContext(QualType T) {
  if (!T->isDependentType())
    if (const TagType *Tag = T->getAs<TagType>())
      return Tag->getDecl();

  return ::getCurrentInstantiationOf(T, CurContext);
}

/// Compute the DeclContext that is associated with the given
/// scope specifier.
///
/// \param SS the C++ scope specifier as it appears in the source
///
/// \param EnteringContext when true, we will be entering the context of
/// this scope specifier, so we can retrieve the declaration context of a
/// class template or class template partial specialization even if it is
/// not the current instantiation.
///
/// \returns the declaration context represented by the scope specifier @p SS,
/// or NULL if the declaration context cannot be computed (e.g., because it is
/// dependent and not the current instantiation).
DeclContext *Sema::computeDeclContext(const CXXScopeSpec &SS,
                                      bool EnteringContext) {
  if (!SS.isSet() || SS.isInvalid())
    return nullptr;

  NestedNameSpecifier *NNS = SS.getScopeRep();
  if (NNS->isDependent()) {
    // If this nested-name-specifier refers to the current
    // instantiation, return its DeclContext.
    if (CXXRecordDecl *Record = getCurrentInstantiationOf(NNS))
      return Record;

    if (EnteringContext) {
      const Type *NNSType = NNS->getAsType();
      if (!NNSType) {
        return nullptr;
      }

      // Look through type alias templates, per C++0x [temp.dep.type]p1.
      NNSType = Context.getCanonicalType(NNSType);
      if (const TemplateSpecializationType *SpecType
            = NNSType->getAs<TemplateSpecializationType>()) {
        // We are entering the context of the nested name specifier, so try to
        // match the nested name specifier to either a primary class template
        // or a class template partial specialization.
        if (ClassTemplateDecl *ClassTemplate
              = dyn_cast_or_null<ClassTemplateDecl>(
                            SpecType->getTemplateName().getAsTemplateDecl())) {
          QualType ContextType
            = Context.getCanonicalType(QualType(SpecType, 0));

          // If the type of the nested name specifier is the same as the
          // injected class name of the named class template, we're entering
          // into that class template definition.
          QualType Injected
            = ClassTemplate->getInjectedClassNameSpecialization();
          if (Context.hasSameType(Injected, ContextType))
            return ClassTemplate->getTemplatedDecl();

          // If the type of the nested name specifier is the same as the
          // type of one of the class template's class template partial
          // specializations, we're entering into the definition of that
          // class template partial specialization.
          if (ClassTemplatePartialSpecializationDecl *PartialSpec
                = ClassTemplate->findPartialSpecialization(ContextType)) {
            // A declaration of the partial specialization must be visible.
            // We can always recover here, because this only happens when we're
            // entering the context, and that can't happen in a SFINAE context.
            assert(!isSFINAEContext() &&
                   "partial specialization scope specifier in SFINAE context?");
            if (!hasVisibleDeclaration(PartialSpec))
              diagnoseMissingImport(SS.getLastQualifierNameLoc(), PartialSpec,
                                    MissingImportKind::PartialSpecialization,
                                    /*Recover*/true);
            return PartialSpec;
          }
        }
      } else if (const RecordType *RecordT = NNSType->getAs<RecordType>()) {
        // The nested name specifier refers to a member of a class template.
        return RecordT->getDecl();
      }
    }

    return nullptr;
  }

  switch (NNS->getKind()) {
  case NestedNameSpecifier::Identifier:
    llvm_unreachable("Dependent nested-name-specifier has no DeclContext");

  case NestedNameSpecifier::Namespace:
    return NNS->getAsNamespace();

  case NestedNameSpecifier::NamespaceAlias:
    return NNS->getAsNamespaceAlias()->getNamespace();

  case NestedNameSpecifier::TypeSpec:
  case NestedNameSpecifier::TypeSpecWithTemplate: {
    const TagType *Tag = NNS->getAsType()->getAs<TagType>();
    assert(Tag && "Non-tag type in nested-name-specifier");
    return Tag->getDecl();
  }

  case NestedNameSpecifier::Global:
    return Context.getTranslationUnitDecl();

  case NestedNameSpecifier::Super:
    return NNS->getAsRecordDecl();
  }

  llvm_unreachable("Invalid NestedNameSpecifier::Kind!");
}

bool Sema::isDependentScopeSpecifier(const CXXScopeSpec &SS) {
  if (!SS.isSet() || SS.isInvalid())
    return false;

  return SS.getScopeRep()->isDependent();
}

/// If the given nested name specifier refers to the current
/// instantiation, return the declaration that corresponds to that
/// current instantiation (C++0x [temp.dep.type]p1).
///
/// \param NNS a dependent nested name specifier.
CXXRecordDecl *Sema::getCurrentInstantiationOf(NestedNameSpecifier *NNS) {
  assert(getLangOpts().CPlusPlus && "Only callable in C++");
  assert(NNS->isDependent() && "Only dependent nested-name-specifier allowed");

  if (!NNS->getAsType())
    return nullptr;

  QualType T = QualType(NNS->getAsType(), 0);
  return ::getCurrentInstantiationOf(T, CurContext);
}

/// Require that the context specified by SS be complete.
///
/// If SS refers to a type, this routine checks whether the type is
/// complete enough (or can be made complete enough) for name lookup
/// into the DeclContext. A type that is not yet completed can be
/// considered "complete enough" if it is a class/struct/union/enum
/// that is currently being defined. Or, if we have a type that names
/// a class template specialization that is not a complete type, we
/// will attempt to instantiate that class template.
bool Sema::RequireCompleteDeclContext(CXXScopeSpec &SS,
                                      DeclContext *DC) {
  assert(DC && "given null context");

  TagDecl *tag = dyn_cast<TagDecl>(DC);

  // If this is a dependent type, then we consider it complete.
  // FIXME: This is wrong; we should require a (visible) definition to
  // exist in this case too.
  if (!tag || tag->isDependentContext())
    return false;

  // Grab the tag definition, if there is one.
  QualType type = Context.getTypeDeclType(tag);
  tag = type->getAsTagDecl();

  // If we're currently defining this type, then lookup into the
  // type is okay: don't complain that it isn't complete yet.
  if (tag->isBeingDefined())
    return false;

  SourceLocation loc = SS.getLastQualifierNameLoc();
  if (loc.isInvalid()) loc = SS.getRange().getBegin();

  // The type must be complete.
  if (RequireCompleteType(loc, type, diag::err_incomplete_nested_name_spec,
                          SS.getRange())) {
    SS.SetInvalid(SS.getRange());
    return true;
  }

  // Fixed enum types are complete, but they aren't valid as scopes
  // until we see a definition, so awkwardly pull out this special
  // case.
  auto *EnumD = dyn_cast<EnumDecl>(tag);
  if (!EnumD)
    return false;
  if (EnumD->isCompleteDefinition()) {
    // If we know about the definition but it is not visible, complain.
    NamedDecl *SuggestedDef = nullptr;
    if (!hasVisibleDefinition(EnumD, &SuggestedDef,
                              /*OnlyNeedComplete*/false)) {
      // If the user is going to see an error here, recover by making the
      // definition visible.
      bool TreatAsComplete = !isSFINAEContext();
      diagnoseMissingImport(loc, SuggestedDef, MissingImportKind::Definition,
                            /*Recover*/TreatAsComplete);
      return !TreatAsComplete;
    }
    return false;
  }

  // Try to instantiate the definition, if this is a specialization of an
  // enumeration temploid.
  if (EnumDecl *Pattern = EnumD->getInstantiatedFromMemberEnum()) {
    MemberSpecializationInfo *MSI = EnumD->getMemberSpecializationInfo();
    if (MSI->getTemplateSpecializationKind() != TSK_ExplicitSpecialization) {
      if (InstantiateEnum(loc, EnumD, Pattern,
                          getTemplateInstantiationArgs(EnumD),
                          TSK_ImplicitInstantiation)) {
        SS.SetInvalid(SS.getRange());
        return true;
      }
      return false;
    }
  }

  Diag(loc, diag::err_incomplete_nested_name_spec)
    << type << SS.getRange();
  SS.SetInvalid(SS.getRange());
  return true;
}

bool Sema::ActOnCXXGlobalScopeSpecifier(SourceLocation CCLoc,
                                        CXXScopeSpec &SS) {
  SS.MakeGlobal(Context, CCLoc);
  return false;
}

bool Sema::ActOnSuperScopeSpecifier(SourceLocation SuperLoc,
                                    SourceLocation ColonColonLoc,
                                    CXXScopeSpec &SS) {
  CXXRecordDecl *RD = nullptr;
  for (Scope *S = getCurScope(); S; S = S->getParent()) {
    if (S->isFunctionScope()) {
      if (CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(S->getEntity()))
        RD = MD->getParent();
      break;
    }
    if (S->isClassScope()) {
      RD = cast<CXXRecordDecl>(S->getEntity());
      break;
    }
  }

  if (!RD) {
    Diag(SuperLoc, diag::err_invalid_super_scope);
    return true;
  } else if (RD->isLambda()) {
    Diag(SuperLoc, diag::err_super_in_lambda_unsupported);
    return true;
  } else if (RD->getNumBases() == 0) {
    Diag(SuperLoc, diag::err_no_base_classes) << RD->getName();
    return true;
  }

  SS.MakeSuper(Context, RD, SuperLoc, ColonColonLoc);
  return false;
}

/// Determines whether the given declaration is an valid acceptable
/// result for name lookup of a nested-name-specifier.
/// \param SD Declaration checked for nested-name-specifier.
/// \param IsExtension If not null and the declaration is accepted as an
/// extension, the pointed variable is assigned true.
bool Sema::isAcceptableNestedNameSpecifier(const NamedDecl *SD,
                                           bool *IsExtension) {
  if (!SD)
    return false;

  SD = SD->getUnderlyingDecl();

  // Namespace and namespace aliases are fine.
  if (isa<NamespaceDecl>(SD))
    return true;

  if (!isa<TypeDecl>(SD))
    return false;

  // Determine whether we have a class (or, in C++11, an enum) or
  // a typedef thereof. If so, build the nested-name-specifier.
  QualType T = Context.getTypeDeclType(cast<TypeDecl>(SD));
  if (T->isDependentType())
    return true;
  if (const TypedefNameDecl *TD = dyn_cast<TypedefNameDecl>(SD)) {
    if (TD->getUnderlyingType()->isRecordType())
      return true;
    if (TD->getUnderlyingType()->isEnumeralType()) {
      if (Context.getLangOpts().CPlusPlus11)
        return true;
      if (IsExtension)
        *IsExtension = true;
    }
  } else if (isa<RecordDecl>(SD)) {
    return true;
  } else if (isa<EnumDecl>(SD)) {
    if (Context.getLangOpts().CPlusPlus11)
      return true;
    if (IsExtension)
      *IsExtension = true;
  }

  return false;
}

/// If the given nested-name-specifier begins with a bare identifier
/// (e.g., Base::), perform name lookup for that identifier as a
/// nested-name-specifier within the given scope, and return the result of that
/// name lookup.
NamedDecl *Sema::FindFirstQualifierInScope(Scope *S, NestedNameSpecifier *NNS) {
  if (!S || !NNS)
    return nullptr;

  while (NNS->getPrefix())
    NNS = NNS->getPrefix();

  if (NNS->getKind() != NestedNameSpecifier::Identifier)
    return nullptr;

  LookupResult Found(*this, NNS->getAsIdentifier(), SourceLocation(),
                     LookupNestedNameSpecifierName);
  LookupName(Found, S);
  assert(!Found.isAmbiguous() && "Cannot handle ambiguities here yet");

  if (!Found.isSingleResult())
    return nullptr;

  NamedDecl *Result = Found.getFoundDecl();
  if (isAcceptableNestedNameSpecifier(Result))
    return Result;

  return nullptr;
}

bool Sema::isNonTypeNestedNameSpecifier(Scope *S, CXXScopeSpec &SS,
                                        NestedNameSpecInfo &IdInfo) {
  QualType ObjectType = GetTypeFromParser(IdInfo.ObjectType);
  LookupResult Found(*this, IdInfo.Identifier, IdInfo.IdentifierLoc,
                     LookupNestedNameSpecifierName);

  // Determine where to perform name lookup
  DeclContext *LookupCtx = nullptr;
  bool isDependent = false;
  if (!ObjectType.isNull()) {
    // This nested-name-specifier occurs in a member access expression, e.g.,
    // x->B::f, and we are looking into the type of the object.
    assert(!SS.isSet() && "ObjectType and scope specifier cannot coexist");
    LookupCtx = computeDeclContext(ObjectType);
    isDependent = ObjectType->isDependentType();
  } else if (SS.isSet()) {
    // This nested-name-specifier occurs after another nested-name-specifier,
    // so long into the context associated with the prior nested-name-specifier.
    LookupCtx = computeDeclContext(SS, false);
    isDependent = isDependentScopeSpecifier(SS);
    Found.setContextRange(SS.getRange());
  }

  if (LookupCtx) {
    // Perform "qualified" name lookup into the declaration context we
    // computed, which is either the type of the base of a member access
    // expression or the declaration context associated with a prior
    // nested-name-specifier.

    // The declaration context must be complete.
    if (!LookupCtx->isDependentContext() &&
        RequireCompleteDeclContext(SS, LookupCtx))
      return false;

    LookupQualifiedName(Found, LookupCtx);
  } else if (isDependent) {
    return false;
  } else {
    LookupName(Found, S);
  }
  Found.suppressDiagnostics();

  return Found.getAsSingle<NamespaceDecl>();
}

namespace {

// Callback to only accept typo corrections that can be a valid C++ member
// intializer: either a non-static field member or a base class.
class NestedNameSpecifierValidatorCCC : public CorrectionCandidateCallback {
 public:
  explicit NestedNameSpecifierValidatorCCC(Sema &SRef)
      : SRef(SRef) {}

  bool ValidateCandidate(const TypoCorrection &candidate) override {
    return SRef.isAcceptableNestedNameSpecifier(candidate.getCorrectionDecl());
  }

 private:
  Sema &SRef;
};

}

/// Build a new nested-name-specifier for "identifier::", as described
/// by ActOnCXXNestedNameSpecifier.
///
/// \param S Scope in which the nested-name-specifier occurs.
/// \param IdInfo Parser information about an identifier in the
///        nested-name-spec.
/// \param EnteringContext If true, enter the context specified by the
///        nested-name-specifier.
/// \param SS Optional nested name specifier preceding the identifier.
/// \param ScopeLookupResult Provides the result of name lookup within the
///        scope of the nested-name-specifier that was computed at template
///        definition time.
/// \param ErrorRecoveryLookup Specifies if the method is called to improve
///        error recovery and what kind of recovery is performed.
/// \param IsCorrectedToColon If not null, suggestion of replace '::' -> ':'
///        are allowed.  The bool value pointed by this parameter is set to
///       'true' if the identifier is treated as if it was followed by ':',
///        not '::'.
/// \param OnlyNamespace If true, only considers namespaces in lookup.
///
/// This routine differs only slightly from ActOnCXXNestedNameSpecifier, in
/// that it contains an extra parameter \p ScopeLookupResult, which provides
/// the result of name lookup within the scope of the nested-name-specifier
/// that was computed at template definition time.
///
/// If ErrorRecoveryLookup is true, then this call is used to improve error
/// recovery.  This means that it should not emit diagnostics, it should
/// just return true on failure.  It also means it should only return a valid
/// scope if it *knows* that the result is correct.  It should not return in a
/// dependent context, for example. Nor will it extend \p SS with the scope
/// specifier.
bool Sema::BuildCXXNestedNameSpecifier(Scope *S, NestedNameSpecInfo &IdInfo,
                                       bool EnteringContext, CXXScopeSpec &SS,
                                       NamedDecl *ScopeLookupResult,
                                       bool ErrorRecoveryLookup,
                                       bool *IsCorrectedToColon,
                                       bool OnlyNamespace) {
  if (IdInfo.Identifier->isEditorPlaceholder())
    return true;
  LookupResult Found(*this, IdInfo.Identifier, IdInfo.IdentifierLoc,
                     OnlyNamespace ? LookupNamespaceName
                                   : LookupNestedNameSpecifierName);
  QualType ObjectType = GetTypeFromParser(IdInfo.ObjectType);

  // Determine where to perform name lookup
  DeclContext *LookupCtx = nullptr;
  bool isDependent = false;
  if (IsCorrectedToColon)
    *IsCorrectedToColon = false;
  if (!ObjectType.isNull()) {
    // This nested-name-specifier occurs in a member access expression, e.g.,
    // x->B::f, and we are looking into the type of the object.
    assert(!SS.isSet() && "ObjectType and scope specifier cannot coexist");
    LookupCtx = computeDeclContext(ObjectType);
    isDependent = ObjectType->isDependentType();
  } else if (SS.isSet()) {
    // This nested-name-specifier occurs after another nested-name-specifier,
    // so look into the context associated with the prior nested-name-specifier.
    LookupCtx = computeDeclContext(SS, EnteringContext);
    isDependent = isDependentScopeSpecifier(SS);
    Found.setContextRange(SS.getRange());
  }

  bool ObjectTypeSearchedInScope = false;
  if (LookupCtx) {
    // Perform "qualified" name lookup into the declaration context we
    // computed, which is either the type of the base of a member access
    // expression or the declaration context associated with a prior
    // nested-name-specifier.

    // The declaration context must be complete.
    if (!LookupCtx->isDependentContext() &&
        RequireCompleteDeclContext(SS, LookupCtx))
      return true;

    LookupQualifiedName(Found, LookupCtx);

    if (!ObjectType.isNull() && Found.empty()) {
      // C++ [basic.lookup.classref]p4:
      //   If the id-expression in a class member access is a qualified-id of
      //   the form
      //
      //        class-name-or-namespace-name::...
      //
      //   the class-name-or-namespace-name following the . or -> operator is
      //   looked up both in the context of the entire postfix-expression and in
      //   the scope of the class of the object expression. If the name is found
      //   only in the scope of the class of the object expression, the name
      //   shall refer to a class-name. If the name is found only in the
      //   context of the entire postfix-expression, the name shall refer to a
      //   class-name or namespace-name. [...]
      //
      // Qualified name lookup into a class will not find a namespace-name,
      // so we do not need to diagnose that case specifically. However,
      // this qualified name lookup may find nothing. In that case, perform
      // unqualified name lookup in the given scope (if available) or
      // reconstruct the result from when name lookup was performed at template
      // definition time.
      if (S)
        LookupName(Found, S);
      else if (ScopeLookupResult)
        Found.addDecl(ScopeLookupResult);

      ObjectTypeSearchedInScope = true;
    }
  } else if (!isDependent) {
    // Perform unqualified name lookup in the current scope.
    LookupName(Found, S);
  }

  if (Found.isAmbiguous())
    return true;

  // If we performed lookup into a dependent context and did not find anything,
  // that's fine: just build a dependent nested-name-specifier.
  if (Found.empty() && isDependent &&
      !(LookupCtx && LookupCtx->isRecord() &&
        (!cast<CXXRecordDecl>(LookupCtx)->hasDefinition() ||
         !cast<CXXRecordDecl>(LookupCtx)->hasAnyDependentBases()))) {
    // Don't speculate if we're just trying to improve error recovery.
    if (ErrorRecoveryLookup)
      return true;

    // We were not able to compute the declaration context for a dependent
    // base object type or prior nested-name-specifier, so this
    // nested-name-specifier refers to an unknown specialization. Just build
    // a dependent nested-name-specifier.
    SS.Extend(Context, IdInfo.Identifier, IdInfo.IdentifierLoc, IdInfo.CCLoc);
    return false;
  }

  if (Found.empty() && !ErrorRecoveryLookup) {
    // If identifier is not found as class-name-or-namespace-name, but is found
    // as other entity, don't look for typos.
    LookupResult R(*this, Found.getLookupNameInfo(), LookupOrdinaryName);
    if (LookupCtx)
      LookupQualifiedName(R, LookupCtx);
    else if (S && !isDependent)
      LookupName(R, S);
    if (!R.empty()) {
      // Don't diagnose problems with this speculative lookup.
      R.suppressDiagnostics();
      // The identifier is found in ordinary lookup. If correction to colon is
      // allowed, suggest replacement to ':'.
      if (IsCorrectedToColon) {
        *IsCorrectedToColon = true;
        Diag(IdInfo.CCLoc, diag::err_nested_name_spec_is_not_class)
            << IdInfo.Identifier << getLangOpts().CPlusPlus
            << FixItHint::CreateReplacement(IdInfo.CCLoc, ":");
        if (NamedDecl *ND = R.getAsSingle<NamedDecl>())
          Diag(ND->getLocation(), diag::note_declared_at);
        return true;
      }
      // Replacement '::' -> ':' is not allowed, just issue respective error.
      Diag(R.getNameLoc(), OnlyNamespace
                               ? unsigned(diag::err_expected_namespace_name)
                               : unsigned(diag::err_expected_class_or_namespace))
          << IdInfo.Identifier << getLangOpts().CPlusPlus;
      if (NamedDecl *ND = R.getAsSingle<NamedDecl>())
        Diag(ND->getLocation(), diag::note_entity_declared_at)
            << IdInfo.Identifier;
      return true;
    }
  }

  if (Found.empty() && !ErrorRecoveryLookup && !getLangOpts().MSVCCompat) {
    // We haven't found anything, and we're not recovering from a
    // different kind of error, so look for typos.
    DeclarationName Name = Found.getLookupName();
    Found.clear();
    if (TypoCorrection Corrected = CorrectTypo(
            Found.getLookupNameInfo(), Found.getLookupKind(), S, &SS,
            llvm::make_unique<NestedNameSpecifierValidatorCCC>(*this),
            CTK_ErrorRecovery, LookupCtx, EnteringContext)) {
      if (LookupCtx) {
        bool DroppedSpecifier =
            Corrected.WillReplaceSpecifier() &&
            Name.getAsString() == Corrected.getAsString(getLangOpts());
        if (DroppedSpecifier)
          SS.clear();
        diagnoseTypo(Corrected, PDiag(diag::err_no_member_suggest)
                                  << Name << LookupCtx << DroppedSpecifier
                                  << SS.getRange());
      } else
        diagnoseTypo(Corrected, PDiag(diag::err_undeclared_var_use_suggest)
                                  << Name);

      if (Corrected.getCorrectionSpecifier())
        SS.MakeTrivial(Context, Corrected.getCorrectionSpecifier(),
                       SourceRange(Found.getNameLoc()));

      if (NamedDecl *ND = Corrected.getFoundDecl())
        Found.addDecl(ND);
      Found.setLookupName(Corrected.getCorrection());
    } else {
      Found.setLookupName(IdInfo.Identifier);
    }
  }

  NamedDecl *SD =
      Found.isSingleResult() ? Found.getRepresentativeDecl() : nullptr;
  bool IsExtension = false;
  bool AcceptSpec = isAcceptableNestedNameSpecifier(SD, &IsExtension);
  if (!AcceptSpec && IsExtension) {
    AcceptSpec = true;
    Diag(IdInfo.IdentifierLoc, diag::ext_nested_name_spec_is_enum);
  }
  if (AcceptSpec) {
    if (!ObjectType.isNull() && !ObjectTypeSearchedInScope &&
        !getLangOpts().CPlusPlus11) {
      // C++03 [basic.lookup.classref]p4:
      //   [...] If the name is found in both contexts, the
      //   class-name-or-namespace-name shall refer to the same entity.
      //
      // We already found the name in the scope of the object. Now, look
      // into the current scope (the scope of the postfix-expression) to
      // see if we can find the same name there. As above, if there is no
      // scope, reconstruct the result from the template instantiation itself.
      //
      // Note that C++11 does *not* perform this redundant lookup.
      NamedDecl *OuterDecl;
      if (S) {
        LookupResult FoundOuter(*this, IdInfo.Identifier, IdInfo.IdentifierLoc,
                                LookupNestedNameSpecifierName);
        LookupName(FoundOuter, S);
        OuterDecl = FoundOuter.getAsSingle<NamedDecl>();
      } else
        OuterDecl = ScopeLookupResult;

      if (isAcceptableNestedNameSpecifier(OuterDecl) &&
          OuterDecl->getCanonicalDecl() != SD->getCanonicalDecl() &&
          (!isa<TypeDecl>(OuterDecl) || !isa<TypeDecl>(SD) ||
           !Context.hasSameType(
                            Context.getTypeDeclType(cast<TypeDecl>(OuterDecl)),
                               Context.getTypeDeclType(cast<TypeDecl>(SD))))) {
        if (ErrorRecoveryLookup)
          return true;

         Diag(IdInfo.IdentifierLoc,
              diag::err_nested_name_member_ref_lookup_ambiguous)
           << IdInfo.Identifier;
         Diag(SD->getLocation(), diag::note_ambig_member_ref_object_type)
           << ObjectType;
         Diag(OuterDecl->getLocation(), diag::note_ambig_member_ref_scope);

         // Fall through so that we'll pick the name we found in the object
         // type, since that's probably what the user wanted anyway.
       }
    }

    if (auto *TD = dyn_cast_or_null<TypedefNameDecl>(SD))
      MarkAnyDeclReferenced(TD->getLocation(), TD, /*OdrUse=*/false);

    // If we're just performing this lookup for error-recovery purposes,
    // don't extend the nested-name-specifier. Just return now.
    if (ErrorRecoveryLookup)
      return false;

    // The use of a nested name specifier may trigger deprecation warnings.
    DiagnoseUseOfDecl(SD, IdInfo.CCLoc);

    if (NamespaceDecl *Namespace = dyn_cast<NamespaceDecl>(SD)) {
      SS.Extend(Context, Namespace, IdInfo.IdentifierLoc, IdInfo.CCLoc);
      return false;
    }

    if (NamespaceAliasDecl *Alias = dyn_cast<NamespaceAliasDecl>(SD)) {
      SS.Extend(Context, Alias, IdInfo.IdentifierLoc, IdInfo.CCLoc);
      return false;
    }

    QualType T =
        Context.getTypeDeclType(cast<TypeDecl>(SD->getUnderlyingDecl()));
    TypeLocBuilder TLB;
    if (isa<InjectedClassNameType>(T)) {
      InjectedClassNameTypeLoc InjectedTL
        = TLB.push<InjectedClassNameTypeLoc>(T);
      InjectedTL.setNameLoc(IdInfo.IdentifierLoc);
    } else if (isa<RecordType>(T)) {
      RecordTypeLoc RecordTL = TLB.push<RecordTypeLoc>(T);
      RecordTL.setNameLoc(IdInfo.IdentifierLoc);
    } else if (isa<TypedefType>(T)) {
      TypedefTypeLoc TypedefTL = TLB.push<TypedefTypeLoc>(T);
      TypedefTL.setNameLoc(IdInfo.IdentifierLoc);
    } else if (isa<EnumType>(T)) {
      EnumTypeLoc EnumTL = TLB.push<EnumTypeLoc>(T);
      EnumTL.setNameLoc(IdInfo.IdentifierLoc);
    } else if (isa<TemplateTypeParmType>(T)) {
      TemplateTypeParmTypeLoc TemplateTypeTL
        = TLB.push<TemplateTypeParmTypeLoc>(T);
      TemplateTypeTL.setNameLoc(IdInfo.IdentifierLoc);
    } else if (isa<UnresolvedUsingType>(T)) {
      UnresolvedUsingTypeLoc UnresolvedTL
        = TLB.push<UnresolvedUsingTypeLoc>(T);
      UnresolvedTL.setNameLoc(IdInfo.IdentifierLoc);
    } else if (isa<SubstTemplateTypeParmType>(T)) {
      SubstTemplateTypeParmTypeLoc TL
        = TLB.push<SubstTemplateTypeParmTypeLoc>(T);
      TL.setNameLoc(IdInfo.IdentifierLoc);
    } else if (isa<SubstTemplateTypeParmPackType>(T)) {
      SubstTemplateTypeParmPackTypeLoc TL
        = TLB.push<SubstTemplateTypeParmPackTypeLoc>(T);
      TL.setNameLoc(IdInfo.IdentifierLoc);
    } else {
      llvm_unreachable("Unhandled TypeDecl node in nested-name-specifier");
    }

    if (T->isEnumeralType())
      Diag(IdInfo.IdentifierLoc, diag::warn_cxx98_compat_enum_nested_name_spec);

    SS.Extend(Context, SourceLocation(), TLB.getTypeLocInContext(Context, T),
              IdInfo.CCLoc);
    return false;
  }

  // Otherwise, we have an error case.  If we don't want diagnostics, just
  // return an error now.
  if (ErrorRecoveryLookup)
    return true;

  // If we didn't find anything during our lookup, try again with
  // ordinary name lookup, which can help us produce better error
  // messages.
  if (Found.empty()) {
    Found.clear(LookupOrdinaryName);
    LookupName(Found, S);
  }

  // In Microsoft mode, if we are within a templated function and we can't
  // resolve Identifier, then extend the SS with Identifier. This will have
  // the effect of resolving Identifier during template instantiation.
  // The goal is to be able to resolve a function call whose
  // nested-name-specifier is located inside a dependent base class.
  // Example:
  //
  // class C {
  // public:
  //    static void foo2() {  }
  // };
  // template <class T> class A { public: typedef C D; };
  //
  // template <class T> class B : public A<T> {
  // public:
  //   void foo() { D::foo2(); }
  // };
  if (getLangOpts().MSVCCompat) {
    DeclContext *DC = LookupCtx ? LookupCtx : CurContext;
    if (DC->isDependentContext() && DC->isFunctionOrMethod()) {
      CXXRecordDecl *ContainingClass = dyn_cast<CXXRecordDecl>(DC->getParent());
      if (ContainingClass && ContainingClass->hasAnyDependentBases()) {
        Diag(IdInfo.IdentifierLoc,
             diag::ext_undeclared_unqual_id_with_dependent_base)
            << IdInfo.Identifier << ContainingClass;
        SS.Extend(Context, IdInfo.Identifier, IdInfo.IdentifierLoc,
                  IdInfo.CCLoc);
        return false;
      }
    }
  }

  if (!Found.empty()) {
    if (TypeDecl *TD = Found.getAsSingle<TypeDecl>())
      Diag(IdInfo.IdentifierLoc, diag::err_expected_class_or_namespace)
          << Context.getTypeDeclType(TD) << getLangOpts().CPlusPlus;
    else {
      Diag(IdInfo.IdentifierLoc, diag::err_expected_class_or_namespace)
          << IdInfo.Identifier << getLangOpts().CPlusPlus;
      if (NamedDecl *ND = Found.getAsSingle<NamedDecl>())
        Diag(ND->getLocation(), diag::note_entity_declared_at)
            << IdInfo.Identifier;
    }
  } else if (SS.isSet())
    Diag(IdInfo.IdentifierLoc, diag::err_no_member) << IdInfo.Identifier
        << LookupCtx << SS.getRange();
  else
    Diag(IdInfo.IdentifierLoc, diag::err_undeclared_var_use)
        << IdInfo.Identifier;

  return true;
}

bool Sema::ActOnCXXNestedNameSpecifier(Scope *S, NestedNameSpecInfo &IdInfo,
                                       bool EnteringContext, CXXScopeSpec &SS,
                                       bool ErrorRecoveryLookup,
                                       bool *IsCorrectedToColon,
                                       bool OnlyNamespace) {
  if (SS.isInvalid())
    return true;

  return BuildCXXNestedNameSpecifier(S, IdInfo, EnteringContext, SS,
                                     /*ScopeLookupResult=*/nullptr, false,
                                     IsCorrectedToColon, OnlyNamespace);
}

bool Sema::ActOnCXXNestedNameSpecifierDecltype(CXXScopeSpec &SS,
                                               const DeclSpec &DS,
                                               SourceLocation ColonColonLoc) {
  if (SS.isInvalid() || DS.getTypeSpecType() == DeclSpec::TST_error)
    return true;

  assert(DS.getTypeSpecType() == DeclSpec::TST_decltype);

  QualType T = BuildDecltypeType(DS.getRepAsExpr(), DS.getTypeSpecTypeLoc());
  if (T.isNull())
    return true;

  if (!T->isDependentType() && !T->getAs<TagType>()) {
    Diag(DS.getTypeSpecTypeLoc(), diag::err_expected_class_or_namespace)
      << T << getLangOpts().CPlusPlus;
    return true;
  }

  TypeLocBuilder TLB;
  DecltypeTypeLoc DecltypeTL = TLB.push<DecltypeTypeLoc>(T);
  DecltypeTL.setNameLoc(DS.getTypeSpecTypeLoc());
  SS.Extend(Context, SourceLocation(), TLB.getTypeLocInContext(Context, T),
            ColonColonLoc);
  return false;
}

/// IsInvalidUnlessNestedName - This method is used for error recovery
/// purposes to determine whether the specified identifier is only valid as
/// a nested name specifier, for example a namespace name.  It is
/// conservatively correct to always return false from this method.
///
/// The arguments are the same as those passed to ActOnCXXNestedNameSpecifier.
bool Sema::IsInvalidUnlessNestedName(Scope *S, CXXScopeSpec &SS,
                                     NestedNameSpecInfo &IdInfo,
                                     bool EnteringContext) {
  if (SS.isInvalid())
    return false;

  return !BuildCXXNestedNameSpecifier(S, IdInfo, EnteringContext, SS,
                                      /*ScopeLookupResult=*/nullptr, true);
}

bool Sema::ActOnCXXNestedNameSpecifier(Scope *S,
                                       CXXScopeSpec &SS,
                                       SourceLocation TemplateKWLoc,
                                       TemplateTy Template,
                                       SourceLocation TemplateNameLoc,
                                       SourceLocation LAngleLoc,
                                       ASTTemplateArgsPtr TemplateArgsIn,
                                       SourceLocation RAngleLoc,
                                       SourceLocation CCLoc,
                                       bool EnteringContext) {
  if (SS.isInvalid())
    return true;

  // Translate the parser's template argument list in our AST format.
  TemplateArgumentListInfo TemplateArgs(LAngleLoc, RAngleLoc);
  translateTemplateArguments(TemplateArgsIn, TemplateArgs);

  DependentTemplateName *DTN = Template.get().getAsDependentTemplateName();
  if (DTN && DTN->isIdentifier()) {
    // Handle a dependent template specialization for which we cannot resolve
    // the template name.
    assert(DTN->getQualifier() == SS.getScopeRep());
    QualType T = Context.getDependentTemplateSpecializationType(ETK_None,
                                                          DTN->getQualifier(),
                                                          DTN->getIdentifier(),
                                                                TemplateArgs);

    // Create source-location information for this type.
    TypeLocBuilder Builder;
    DependentTemplateSpecializationTypeLoc SpecTL
      = Builder.push<DependentTemplateSpecializationTypeLoc>(T);
    SpecTL.setElaboratedKeywordLoc(SourceLocation());
    SpecTL.setQualifierLoc(SS.getWithLocInContext(Context));
    SpecTL.setTemplateKeywordLoc(TemplateKWLoc);
    SpecTL.setTemplateNameLoc(TemplateNameLoc);
    SpecTL.setLAngleLoc(LAngleLoc);
    SpecTL.setRAngleLoc(RAngleLoc);
    for (unsigned I = 0, N = TemplateArgs.size(); I != N; ++I)
      SpecTL.setArgLocInfo(I, TemplateArgs[I].getLocInfo());

    SS.Extend(Context, TemplateKWLoc, Builder.getTypeLocInContext(Context, T),
              CCLoc);
    return false;
  }

  TemplateDecl *TD = Template.get().getAsTemplateDecl();
  if (Template.get().getAsOverloadedTemplate() || DTN ||
      isa<FunctionTemplateDecl>(TD) || isa<VarTemplateDecl>(TD)) {
    SourceRange R(TemplateNameLoc, RAngleLoc);
    if (SS.getRange().isValid())
      R.setBegin(SS.getRange().getBegin());

    Diag(CCLoc, diag::err_non_type_template_in_nested_name_specifier)
      << (TD && isa<VarTemplateDecl>(TD)) << Template.get() << R;
    NoteAllFoundTemplates(Template.get());
    return true;
  }

  // We were able to resolve the template name to an actual template.
  // Build an appropriate nested-name-specifier.
  QualType T =
      CheckTemplateIdType(Template.get(), TemplateNameLoc, TemplateArgs);
  if (T.isNull())
    return true;

  // Alias template specializations can produce types which are not valid
  // nested name specifiers.
  if (!T->isDependentType() && !T->getAs<TagType>()) {
    Diag(TemplateNameLoc, diag::err_nested_name_spec_non_tag) << T;
    NoteAllFoundTemplates(Template.get());
    return true;
  }

  // Provide source-location information for the template specialization type.
  TypeLocBuilder Builder;
  TemplateSpecializationTypeLoc SpecTL
    = Builder.push<TemplateSpecializationTypeLoc>(T);
  SpecTL.setTemplateKeywordLoc(TemplateKWLoc);
  SpecTL.setTemplateNameLoc(TemplateNameLoc);
  SpecTL.setLAngleLoc(LAngleLoc);
  SpecTL.setRAngleLoc(RAngleLoc);
  for (unsigned I = 0, N = TemplateArgs.size(); I != N; ++I)
    SpecTL.setArgLocInfo(I, TemplateArgs[I].getLocInfo());


  SS.Extend(Context, TemplateKWLoc, Builder.getTypeLocInContext(Context, T),
            CCLoc);
  return false;
}

namespace {
  /// A structure that stores a nested-name-specifier annotation,
  /// including both the nested-name-specifier
  struct NestedNameSpecifierAnnotation {
    NestedNameSpecifier *NNS;
  };
}

void *Sema::SaveNestedNameSpecifierAnnotation(CXXScopeSpec &SS) {
  if (SS.isEmpty() || SS.isInvalid())
    return nullptr;

  void *Mem = Context.Allocate(
      (sizeof(NestedNameSpecifierAnnotation) + SS.location_size()),
      alignof(NestedNameSpecifierAnnotation));
  NestedNameSpecifierAnnotation *Annotation
    = new (Mem) NestedNameSpecifierAnnotation;
  Annotation->NNS = SS.getScopeRep();
  memcpy(Annotation + 1, SS.location_data(), SS.location_size());
  return Annotation;
}

void Sema::RestoreNestedNameSpecifierAnnotation(void *AnnotationPtr,
                                                SourceRange AnnotationRange,
                                                CXXScopeSpec &SS) {
  if (!AnnotationPtr) {
    SS.SetInvalid(AnnotationRange);
    return;
  }

  NestedNameSpecifierAnnotation *Annotation
    = static_cast<NestedNameSpecifierAnnotation *>(AnnotationPtr);
  SS.Adopt(NestedNameSpecifierLoc(Annotation->NNS, Annotation + 1));
}

bool Sema::ShouldEnterDeclaratorScope(Scope *S, const CXXScopeSpec &SS) {
  assert(SS.isSet() && "Parser passed invalid CXXScopeSpec.");

  // Don't enter a declarator context when the current context is an Objective-C
  // declaration.
  if (isa<ObjCContainerDecl>(CurContext) || isa<ObjCMethodDecl>(CurContext))
    return false;

  NestedNameSpecifier *Qualifier = SS.getScopeRep();

  // There are only two places a well-formed program may qualify a
  // declarator: first, when defining a namespace or class member
  // out-of-line, and second, when naming an explicitly-qualified
  // friend function.  The latter case is governed by
  // C++03 [basic.lookup.unqual]p10:
  //   In a friend declaration naming a member function, a name used
  //   in the function declarator and not part of a template-argument
  //   in a template-id is first looked up in the scope of the member
  //   function's class. If it is not found, or if the name is part of
  //   a template-argument in a template-id, the look up is as
  //   described for unqualified names in the definition of the class
  //   granting friendship.
  // i.e. we don't push a scope unless it's a class member.

  switch (Qualifier->getKind()) {
  case NestedNameSpecifier::Global:
  case NestedNameSpecifier::Namespace:
  case NestedNameSpecifier::NamespaceAlias:
    // These are always namespace scopes.  We never want to enter a
    // namespace scope from anything but a file context.
    return CurContext->getRedeclContext()->isFileContext();

  case NestedNameSpecifier::Identifier:
  case NestedNameSpecifier::TypeSpec:
  case NestedNameSpecifier::TypeSpecWithTemplate:
  case NestedNameSpecifier::Super:
    // These are never namespace scopes.
    return true;
  }

  llvm_unreachable("Invalid NestedNameSpecifier::Kind!");
}

/// ActOnCXXEnterDeclaratorScope - Called when a C++ scope specifier (global
/// scope or nested-name-specifier) is parsed, part of a declarator-id.
/// After this method is called, according to [C++ 3.4.3p3], names should be
/// looked up in the declarator-id's scope, until the declarator is parsed and
/// ActOnCXXExitDeclaratorScope is called.
/// The 'SS' should be a non-empty valid CXXScopeSpec.
bool Sema::ActOnCXXEnterDeclaratorScope(Scope *S, CXXScopeSpec &SS) {
  assert(SS.isSet() && "Parser passed invalid CXXScopeSpec.");

  if (SS.isInvalid()) return true;

  DeclContext *DC = computeDeclContext(SS, true);
  if (!DC) return true;

  // Before we enter a declarator's context, we need to make sure that
  // it is a complete declaration context.
  if (!DC->isDependentContext() && RequireCompleteDeclContext(SS, DC))
    return true;

  EnterDeclaratorContext(S, DC);

  // Rebuild the nested name specifier for the new scope.
  if (DC->isDependentContext())
    RebuildNestedNameSpecifierInCurrentInstantiation(SS);

  return false;
}

/// ActOnCXXExitDeclaratorScope - Called when a declarator that previously
/// invoked ActOnCXXEnterDeclaratorScope(), is finished. 'SS' is the same
/// CXXScopeSpec that was passed to ActOnCXXEnterDeclaratorScope as well.
/// Used to indicate that names should revert to being looked up in the
/// defining scope.
void Sema::ActOnCXXExitDeclaratorScope(Scope *S, const CXXScopeSpec &SS) {
  assert(SS.isSet() && "Parser passed invalid CXXScopeSpec.");
  if (SS.isInvalid())
    return;
  assert(!SS.isInvalid() && computeDeclContext(SS, true) &&
         "exiting declarator scope we never really entered");
  ExitDeclaratorContext(S);
}
