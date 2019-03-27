//===--- SemaDeclObjC.cpp - Semantic Analysis for ObjC Declarations -------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis for Objective C declarations.
//
//===----------------------------------------------------------------------===//

#include "TypeLocBuilder.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTMutationListener.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/SemaInternal.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"

using namespace clang;

/// Check whether the given method, which must be in the 'init'
/// family, is a valid member of that family.
///
/// \param receiverTypeIfCall - if null, check this as if declaring it;
///   if non-null, check this as if making a call to it with the given
///   receiver type
///
/// \return true to indicate that there was an error and appropriate
///   actions were taken
bool Sema::checkInitMethod(ObjCMethodDecl *method,
                           QualType receiverTypeIfCall) {
  if (method->isInvalidDecl()) return true;

  // This castAs is safe: methods that don't return an object
  // pointer won't be inferred as inits and will reject an explicit
  // objc_method_family(init).

  // We ignore protocols here.  Should we?  What about Class?

  const ObjCObjectType *result =
      method->getReturnType()->castAs<ObjCObjectPointerType>()->getObjectType();

  if (result->isObjCId()) {
    return false;
  } else if (result->isObjCClass()) {
    // fall through: always an error
  } else {
    ObjCInterfaceDecl *resultClass = result->getInterface();
    assert(resultClass && "unexpected object type!");

    // It's okay for the result type to still be a forward declaration
    // if we're checking an interface declaration.
    if (!resultClass->hasDefinition()) {
      if (receiverTypeIfCall.isNull() &&
          !isa<ObjCImplementationDecl>(method->getDeclContext()))
        return false;

    // Otherwise, we try to compare class types.
    } else {
      // If this method was declared in a protocol, we can't check
      // anything unless we have a receiver type that's an interface.
      const ObjCInterfaceDecl *receiverClass = nullptr;
      if (isa<ObjCProtocolDecl>(method->getDeclContext())) {
        if (receiverTypeIfCall.isNull())
          return false;

        receiverClass = receiverTypeIfCall->castAs<ObjCObjectPointerType>()
          ->getInterfaceDecl();

        // This can be null for calls to e.g. id<Foo>.
        if (!receiverClass) return false;
      } else {
        receiverClass = method->getClassInterface();
        assert(receiverClass && "method not associated with a class!");
      }

      // If either class is a subclass of the other, it's fine.
      if (receiverClass->isSuperClassOf(resultClass) ||
          resultClass->isSuperClassOf(receiverClass))
        return false;
    }
  }

  SourceLocation loc = method->getLocation();

  // If we're in a system header, and this is not a call, just make
  // the method unusable.
  if (receiverTypeIfCall.isNull() && getSourceManager().isInSystemHeader(loc)) {
    method->addAttr(UnavailableAttr::CreateImplicit(Context, "",
                      UnavailableAttr::IR_ARCInitReturnsUnrelated, loc));
    return true;
  }

  // Otherwise, it's an error.
  Diag(loc, diag::err_arc_init_method_unrelated_result_type);
  method->setInvalidDecl();
  return true;
}

/// Issue a warning if the parameter of the overridden method is non-escaping
/// but the parameter of the overriding method is not.
static bool diagnoseNoescape(const ParmVarDecl *NewD, const ParmVarDecl *OldD,
                             Sema &S) {
  if (OldD->hasAttr<NoEscapeAttr>() && !NewD->hasAttr<NoEscapeAttr>()) {
    S.Diag(NewD->getLocation(), diag::warn_overriding_method_missing_noescape);
    S.Diag(OldD->getLocation(), diag::note_overridden_marked_noescape);
    return false;
  }

  return true;
}

/// Produce additional diagnostics if a category conforms to a protocol that
/// defines a method taking a non-escaping parameter.
static void diagnoseNoescape(const ParmVarDecl *NewD, const ParmVarDecl *OldD,
                             const ObjCCategoryDecl *CD,
                             const ObjCProtocolDecl *PD, Sema &S) {
  if (!diagnoseNoescape(NewD, OldD, S))
    S.Diag(CD->getLocation(), diag::note_cat_conform_to_noescape_prot)
        << CD->IsClassExtension() << PD
        << cast<ObjCMethodDecl>(NewD->getDeclContext());
}

void Sema::CheckObjCMethodOverride(ObjCMethodDecl *NewMethod,
                                   const ObjCMethodDecl *Overridden) {
  if (Overridden->hasRelatedResultType() &&
      !NewMethod->hasRelatedResultType()) {
    // This can only happen when the method follows a naming convention that
    // implies a related result type, and the original (overridden) method has
    // a suitable return type, but the new (overriding) method does not have
    // a suitable return type.
    QualType ResultType = NewMethod->getReturnType();
    SourceRange ResultTypeRange = NewMethod->getReturnTypeSourceRange();

    // Figure out which class this method is part of, if any.
    ObjCInterfaceDecl *CurrentClass
      = dyn_cast<ObjCInterfaceDecl>(NewMethod->getDeclContext());
    if (!CurrentClass) {
      DeclContext *DC = NewMethod->getDeclContext();
      if (ObjCCategoryDecl *Cat = dyn_cast<ObjCCategoryDecl>(DC))
        CurrentClass = Cat->getClassInterface();
      else if (ObjCImplDecl *Impl = dyn_cast<ObjCImplDecl>(DC))
        CurrentClass = Impl->getClassInterface();
      else if (ObjCCategoryImplDecl *CatImpl
               = dyn_cast<ObjCCategoryImplDecl>(DC))
        CurrentClass = CatImpl->getClassInterface();
    }

    if (CurrentClass) {
      Diag(NewMethod->getLocation(),
           diag::warn_related_result_type_compatibility_class)
        << Context.getObjCInterfaceType(CurrentClass)
        << ResultType
        << ResultTypeRange;
    } else {
      Diag(NewMethod->getLocation(),
           diag::warn_related_result_type_compatibility_protocol)
        << ResultType
        << ResultTypeRange;
    }

    if (ObjCMethodFamily Family = Overridden->getMethodFamily())
      Diag(Overridden->getLocation(),
           diag::note_related_result_type_family)
        << /*overridden method*/ 0
        << Family;
    else
      Diag(Overridden->getLocation(),
           diag::note_related_result_type_overridden);
  }

  if ((NewMethod->hasAttr<NSReturnsRetainedAttr>() !=
       Overridden->hasAttr<NSReturnsRetainedAttr>())) {
    Diag(NewMethod->getLocation(),
         getLangOpts().ObjCAutoRefCount
             ? diag::err_nsreturns_retained_attribute_mismatch
             : diag::warn_nsreturns_retained_attribute_mismatch)
        << 1;
    Diag(Overridden->getLocation(), diag::note_previous_decl) << "method";
  }
  if ((NewMethod->hasAttr<NSReturnsNotRetainedAttr>() !=
       Overridden->hasAttr<NSReturnsNotRetainedAttr>())) {
    Diag(NewMethod->getLocation(),
         getLangOpts().ObjCAutoRefCount
             ? diag::err_nsreturns_retained_attribute_mismatch
             : diag::warn_nsreturns_retained_attribute_mismatch)
        << 0;
    Diag(Overridden->getLocation(), diag::note_previous_decl)  << "method";
  }

  ObjCMethodDecl::param_const_iterator oi = Overridden->param_begin(),
                                       oe = Overridden->param_end();
  for (ObjCMethodDecl::param_iterator ni = NewMethod->param_begin(),
                                      ne = NewMethod->param_end();
       ni != ne && oi != oe; ++ni, ++oi) {
    const ParmVarDecl *oldDecl = (*oi);
    ParmVarDecl *newDecl = (*ni);
    if (newDecl->hasAttr<NSConsumedAttr>() !=
        oldDecl->hasAttr<NSConsumedAttr>()) {
      Diag(newDecl->getLocation(),
           getLangOpts().ObjCAutoRefCount
               ? diag::err_nsconsumed_attribute_mismatch
               : diag::warn_nsconsumed_attribute_mismatch);
      Diag(oldDecl->getLocation(), diag::note_previous_decl) << "parameter";
    }

    diagnoseNoescape(newDecl, oldDecl, *this);
  }
}

/// Check a method declaration for compatibility with the Objective-C
/// ARC conventions.
bool Sema::CheckARCMethodDecl(ObjCMethodDecl *method) {
  ObjCMethodFamily family = method->getMethodFamily();
  switch (family) {
  case OMF_None:
  case OMF_finalize:
  case OMF_retain:
  case OMF_release:
  case OMF_autorelease:
  case OMF_retainCount:
  case OMF_self:
  case OMF_initialize:
  case OMF_performSelector:
    return false;

  case OMF_dealloc:
    if (!Context.hasSameType(method->getReturnType(), Context.VoidTy)) {
      SourceRange ResultTypeRange = method->getReturnTypeSourceRange();
      if (ResultTypeRange.isInvalid())
        Diag(method->getLocation(), diag::err_dealloc_bad_result_type)
            << method->getReturnType()
            << FixItHint::CreateInsertion(method->getSelectorLoc(0), "(void)");
      else
        Diag(method->getLocation(), diag::err_dealloc_bad_result_type)
            << method->getReturnType()
            << FixItHint::CreateReplacement(ResultTypeRange, "void");
      return true;
    }
    return false;

  case OMF_init:
    // If the method doesn't obey the init rules, don't bother annotating it.
    if (checkInitMethod(method, QualType()))
      return true;

    method->addAttr(NSConsumesSelfAttr::CreateImplicit(Context));

    // Don't add a second copy of this attribute, but otherwise don't
    // let it be suppressed.
    if (method->hasAttr<NSReturnsRetainedAttr>())
      return false;
    break;

  case OMF_alloc:
  case OMF_copy:
  case OMF_mutableCopy:
  case OMF_new:
    if (method->hasAttr<NSReturnsRetainedAttr>() ||
        method->hasAttr<NSReturnsNotRetainedAttr>() ||
        method->hasAttr<NSReturnsAutoreleasedAttr>())
      return false;
    break;
  }

  method->addAttr(NSReturnsRetainedAttr::CreateImplicit(Context));
  return false;
}

static void DiagnoseObjCImplementedDeprecations(Sema &S, const NamedDecl *ND,
                                                SourceLocation ImplLoc) {
  if (!ND)
    return;
  bool IsCategory = false;
  StringRef RealizedPlatform;
  AvailabilityResult Availability = ND->getAvailability(
      /*Message=*/nullptr, /*EnclosingVersion=*/VersionTuple(),
      &RealizedPlatform);
  if (Availability != AR_Deprecated) {
    if (isa<ObjCMethodDecl>(ND)) {
      if (Availability != AR_Unavailable)
        return;
      if (RealizedPlatform.empty())
        RealizedPlatform = S.Context.getTargetInfo().getPlatformName();
      // Warn about implementing unavailable methods, unless the unavailable
      // is for an app extension.
      if (RealizedPlatform.endswith("_app_extension"))
        return;
      S.Diag(ImplLoc, diag::warn_unavailable_def);
      S.Diag(ND->getLocation(), diag::note_method_declared_at)
          << ND->getDeclName();
      return;
    }
    if (const auto *CD = dyn_cast<ObjCCategoryDecl>(ND)) {
      if (!CD->getClassInterface()->isDeprecated())
        return;
      ND = CD->getClassInterface();
      IsCategory = true;
    } else
      return;
  }
  S.Diag(ImplLoc, diag::warn_deprecated_def)
      << (isa<ObjCMethodDecl>(ND)
              ? /*Method*/ 0
              : isa<ObjCCategoryDecl>(ND) || IsCategory ? /*Category*/ 2
                                                        : /*Class*/ 1);
  if (isa<ObjCMethodDecl>(ND))
    S.Diag(ND->getLocation(), diag::note_method_declared_at)
        << ND->getDeclName();
  else
    S.Diag(ND->getLocation(), diag::note_previous_decl)
        << (isa<ObjCCategoryDecl>(ND) ? "category" : "class");
}

/// AddAnyMethodToGlobalPool - Add any method, instance or factory to global
/// pool.
void Sema::AddAnyMethodToGlobalPool(Decl *D) {
  ObjCMethodDecl *MDecl = dyn_cast_or_null<ObjCMethodDecl>(D);

  // If we don't have a valid method decl, simply return.
  if (!MDecl)
    return;
  if (MDecl->isInstanceMethod())
    AddInstanceMethodToGlobalPool(MDecl, true);
  else
    AddFactoryMethodToGlobalPool(MDecl, true);
}

/// HasExplicitOwnershipAttr - returns true when pointer to ObjC pointer
/// has explicit ownership attribute; false otherwise.
static bool
HasExplicitOwnershipAttr(Sema &S, ParmVarDecl *Param) {
  QualType T = Param->getType();

  if (const PointerType *PT = T->getAs<PointerType>()) {
    T = PT->getPointeeType();
  } else if (const ReferenceType *RT = T->getAs<ReferenceType>()) {
    T = RT->getPointeeType();
  } else {
    return true;
  }

  // If we have a lifetime qualifier, but it's local, we must have
  // inferred it. So, it is implicit.
  return !T.getLocalQualifiers().hasObjCLifetime();
}

/// ActOnStartOfObjCMethodDef - This routine sets up parameters; invisible
/// and user declared, in the method definition's AST.
void Sema::ActOnStartOfObjCMethodDef(Scope *FnBodyScope, Decl *D) {
  assert((getCurMethodDecl() == nullptr) && "Methodparsing confused");
  ObjCMethodDecl *MDecl = dyn_cast_or_null<ObjCMethodDecl>(D);

  PushExpressionEvaluationContext(ExprEvalContexts.back().Context);

  // If we don't have a valid method decl, simply return.
  if (!MDecl)
    return;

  QualType ResultType = MDecl->getReturnType();
  if (!ResultType->isDependentType() && !ResultType->isVoidType() &&
      !MDecl->isInvalidDecl() &&
      RequireCompleteType(MDecl->getLocation(), ResultType,
                          diag::err_func_def_incomplete_result))
    MDecl->setInvalidDecl();

  // Allow all of Sema to see that we are entering a method definition.
  PushDeclContext(FnBodyScope, MDecl);
  PushFunctionScope();

  // Create Decl objects for each parameter, entrring them in the scope for
  // binding to their use.

  // Insert the invisible arguments, self and _cmd!
  MDecl->createImplicitParams(Context, MDecl->getClassInterface());

  PushOnScopeChains(MDecl->getSelfDecl(), FnBodyScope);
  PushOnScopeChains(MDecl->getCmdDecl(), FnBodyScope);

  // The ObjC parser requires parameter names so there's no need to check.
  CheckParmsForFunctionDef(MDecl->parameters(),
                           /*CheckParameterNames=*/false);

  // Introduce all of the other parameters into this scope.
  for (auto *Param : MDecl->parameters()) {
    if (!Param->isInvalidDecl() &&
        getLangOpts().ObjCAutoRefCount &&
        !HasExplicitOwnershipAttr(*this, Param))
      Diag(Param->getLocation(), diag::warn_arc_strong_pointer_objc_pointer) <<
            Param->getType();

    if (Param->getIdentifier())
      PushOnScopeChains(Param, FnBodyScope);
  }

  // In ARC, disallow definition of retain/release/autorelease/retainCount
  if (getLangOpts().ObjCAutoRefCount) {
    switch (MDecl->getMethodFamily()) {
    case OMF_retain:
    case OMF_retainCount:
    case OMF_release:
    case OMF_autorelease:
      Diag(MDecl->getLocation(), diag::err_arc_illegal_method_def)
        << 0 << MDecl->getSelector();
      break;

    case OMF_None:
    case OMF_dealloc:
    case OMF_finalize:
    case OMF_alloc:
    case OMF_init:
    case OMF_mutableCopy:
    case OMF_copy:
    case OMF_new:
    case OMF_self:
    case OMF_initialize:
    case OMF_performSelector:
      break;
    }
  }

  // Warn on deprecated methods under -Wdeprecated-implementations,
  // and prepare for warning on missing super calls.
  if (ObjCInterfaceDecl *IC = MDecl->getClassInterface()) {
    ObjCMethodDecl *IMD =
      IC->lookupMethod(MDecl->getSelector(), MDecl->isInstanceMethod());

    if (IMD) {
      ObjCImplDecl *ImplDeclOfMethodDef =
        dyn_cast<ObjCImplDecl>(MDecl->getDeclContext());
      ObjCContainerDecl *ContDeclOfMethodDecl =
        dyn_cast<ObjCContainerDecl>(IMD->getDeclContext());
      ObjCImplDecl *ImplDeclOfMethodDecl = nullptr;
      if (ObjCInterfaceDecl *OID = dyn_cast<ObjCInterfaceDecl>(ContDeclOfMethodDecl))
        ImplDeclOfMethodDecl = OID->getImplementation();
      else if (ObjCCategoryDecl *CD = dyn_cast<ObjCCategoryDecl>(ContDeclOfMethodDecl)) {
        if (CD->IsClassExtension()) {
          if (ObjCInterfaceDecl *OID = CD->getClassInterface())
            ImplDeclOfMethodDecl = OID->getImplementation();
        } else
            ImplDeclOfMethodDecl = CD->getImplementation();
      }
      // No need to issue deprecated warning if deprecated mehod in class/category
      // is being implemented in its own implementation (no overriding is involved).
      if (!ImplDeclOfMethodDecl || ImplDeclOfMethodDecl != ImplDeclOfMethodDef)
        DiagnoseObjCImplementedDeprecations(*this, IMD, MDecl->getLocation());
    }

    if (MDecl->getMethodFamily() == OMF_init) {
      if (MDecl->isDesignatedInitializerForTheInterface()) {
        getCurFunction()->ObjCIsDesignatedInit = true;
        getCurFunction()->ObjCWarnForNoDesignatedInitChain =
            IC->getSuperClass() != nullptr;
      } else if (IC->hasDesignatedInitializers()) {
        getCurFunction()->ObjCIsSecondaryInit = true;
        getCurFunction()->ObjCWarnForNoInitDelegation = true;
      }
    }

    // If this is "dealloc" or "finalize", set some bit here.
    // Then in ActOnSuperMessage() (SemaExprObjC), set it back to false.
    // Finally, in ActOnFinishFunctionBody() (SemaDecl), warn if flag is set.
    // Only do this if the current class actually has a superclass.
    if (const ObjCInterfaceDecl *SuperClass = IC->getSuperClass()) {
      ObjCMethodFamily Family = MDecl->getMethodFamily();
      if (Family == OMF_dealloc) {
        if (!(getLangOpts().ObjCAutoRefCount ||
              getLangOpts().getGC() == LangOptions::GCOnly))
          getCurFunction()->ObjCShouldCallSuper = true;

      } else if (Family == OMF_finalize) {
        if (Context.getLangOpts().getGC() != LangOptions::NonGC)
          getCurFunction()->ObjCShouldCallSuper = true;

      } else {
        const ObjCMethodDecl *SuperMethod =
          SuperClass->lookupMethod(MDecl->getSelector(),
                                   MDecl->isInstanceMethod());
        getCurFunction()->ObjCShouldCallSuper =
          (SuperMethod && SuperMethod->hasAttr<ObjCRequiresSuperAttr>());
      }
    }
  }
}

namespace {

// Callback to only accept typo corrections that are Objective-C classes.
// If an ObjCInterfaceDecl* is given to the constructor, then the validation
// function will reject corrections to that class.
class ObjCInterfaceValidatorCCC : public CorrectionCandidateCallback {
 public:
  ObjCInterfaceValidatorCCC() : CurrentIDecl(nullptr) {}
  explicit ObjCInterfaceValidatorCCC(ObjCInterfaceDecl *IDecl)
      : CurrentIDecl(IDecl) {}

  bool ValidateCandidate(const TypoCorrection &candidate) override {
    ObjCInterfaceDecl *ID = candidate.getCorrectionDeclAs<ObjCInterfaceDecl>();
    return ID && !declaresSameEntity(ID, CurrentIDecl);
  }

 private:
  ObjCInterfaceDecl *CurrentIDecl;
};

} // end anonymous namespace

static void diagnoseUseOfProtocols(Sema &TheSema,
                                   ObjCContainerDecl *CD,
                                   ObjCProtocolDecl *const *ProtoRefs,
                                   unsigned NumProtoRefs,
                                   const SourceLocation *ProtoLocs) {
  assert(ProtoRefs);
  // Diagnose availability in the context of the ObjC container.
  Sema::ContextRAII SavedContext(TheSema, CD);
  for (unsigned i = 0; i < NumProtoRefs; ++i) {
    (void)TheSema.DiagnoseUseOfDecl(ProtoRefs[i], ProtoLocs[i],
                                    /*UnknownObjCClass=*/nullptr,
                                    /*ObjCPropertyAccess=*/false,
                                    /*AvoidPartialAvailabilityChecks=*/true);
  }
}

void Sema::
ActOnSuperClassOfClassInterface(Scope *S,
                                SourceLocation AtInterfaceLoc,
                                ObjCInterfaceDecl *IDecl,
                                IdentifierInfo *ClassName,
                                SourceLocation ClassLoc,
                                IdentifierInfo *SuperName,
                                SourceLocation SuperLoc,
                                ArrayRef<ParsedType> SuperTypeArgs,
                                SourceRange SuperTypeArgsRange) {
  // Check if a different kind of symbol declared in this scope.
  NamedDecl *PrevDecl = LookupSingleName(TUScope, SuperName, SuperLoc,
                                         LookupOrdinaryName);

  if (!PrevDecl) {
    // Try to correct for a typo in the superclass name without correcting
    // to the class we're defining.
    if (TypoCorrection Corrected = CorrectTypo(
            DeclarationNameInfo(SuperName, SuperLoc),
            LookupOrdinaryName, TUScope,
            nullptr, llvm::make_unique<ObjCInterfaceValidatorCCC>(IDecl),
            CTK_ErrorRecovery)) {
      diagnoseTypo(Corrected, PDiag(diag::err_undef_superclass_suggest)
                   << SuperName << ClassName);
      PrevDecl = Corrected.getCorrectionDeclAs<ObjCInterfaceDecl>();
    }
  }

  if (declaresSameEntity(PrevDecl, IDecl)) {
    Diag(SuperLoc, diag::err_recursive_superclass)
      << SuperName << ClassName << SourceRange(AtInterfaceLoc, ClassLoc);
    IDecl->setEndOfDefinitionLoc(ClassLoc);
  } else {
    ObjCInterfaceDecl *SuperClassDecl =
    dyn_cast_or_null<ObjCInterfaceDecl>(PrevDecl);
    QualType SuperClassType;

    // Diagnose classes that inherit from deprecated classes.
    if (SuperClassDecl) {
      (void)DiagnoseUseOfDecl(SuperClassDecl, SuperLoc);
      SuperClassType = Context.getObjCInterfaceType(SuperClassDecl);
    }

    if (PrevDecl && !SuperClassDecl) {
      // The previous declaration was not a class decl. Check if we have a
      // typedef. If we do, get the underlying class type.
      if (const TypedefNameDecl *TDecl =
          dyn_cast_or_null<TypedefNameDecl>(PrevDecl)) {
        QualType T = TDecl->getUnderlyingType();
        if (T->isObjCObjectType()) {
          if (NamedDecl *IDecl = T->getAs<ObjCObjectType>()->getInterface()) {
            SuperClassDecl = dyn_cast<ObjCInterfaceDecl>(IDecl);
            SuperClassType = Context.getTypeDeclType(TDecl);

            // This handles the following case:
            // @interface NewI @end
            // typedef NewI DeprI __attribute__((deprecated("blah")))
            // @interface SI : DeprI /* warn here */ @end
            (void)DiagnoseUseOfDecl(const_cast<TypedefNameDecl*>(TDecl), SuperLoc);
          }
        }
      }

      // This handles the following case:
      //
      // typedef int SuperClass;
      // @interface MyClass : SuperClass {} @end
      //
      if (!SuperClassDecl) {
        Diag(SuperLoc, diag::err_redefinition_different_kind) << SuperName;
        Diag(PrevDecl->getLocation(), diag::note_previous_definition);
      }
    }

    if (!dyn_cast_or_null<TypedefNameDecl>(PrevDecl)) {
      if (!SuperClassDecl)
        Diag(SuperLoc, diag::err_undef_superclass)
          << SuperName << ClassName << SourceRange(AtInterfaceLoc, ClassLoc);
      else if (RequireCompleteType(SuperLoc,
                                   SuperClassType,
                                   diag::err_forward_superclass,
                                   SuperClassDecl->getDeclName(),
                                   ClassName,
                                   SourceRange(AtInterfaceLoc, ClassLoc))) {
        SuperClassDecl = nullptr;
        SuperClassType = QualType();
      }
    }

    if (SuperClassType.isNull()) {
      assert(!SuperClassDecl && "Failed to set SuperClassType?");
      return;
    }

    // Handle type arguments on the superclass.
    TypeSourceInfo *SuperClassTInfo = nullptr;
    if (!SuperTypeArgs.empty()) {
      TypeResult fullSuperClassType = actOnObjCTypeArgsAndProtocolQualifiers(
                                        S,
                                        SuperLoc,
                                        CreateParsedType(SuperClassType,
                                                         nullptr),
                                        SuperTypeArgsRange.getBegin(),
                                        SuperTypeArgs,
                                        SuperTypeArgsRange.getEnd(),
                                        SourceLocation(),
                                        { },
                                        { },
                                        SourceLocation());
      if (!fullSuperClassType.isUsable())
        return;

      SuperClassType = GetTypeFromParser(fullSuperClassType.get(),
                                         &SuperClassTInfo);
    }

    if (!SuperClassTInfo) {
      SuperClassTInfo = Context.getTrivialTypeSourceInfo(SuperClassType,
                                                         SuperLoc);
    }

    IDecl->setSuperClass(SuperClassTInfo);
    IDecl->setEndOfDefinitionLoc(SuperClassTInfo->getTypeLoc().getEndLoc());
  }
}

DeclResult Sema::actOnObjCTypeParam(Scope *S,
                                    ObjCTypeParamVariance variance,
                                    SourceLocation varianceLoc,
                                    unsigned index,
                                    IdentifierInfo *paramName,
                                    SourceLocation paramLoc,
                                    SourceLocation colonLoc,
                                    ParsedType parsedTypeBound) {
  // If there was an explicitly-provided type bound, check it.
  TypeSourceInfo *typeBoundInfo = nullptr;
  if (parsedTypeBound) {
    // The type bound can be any Objective-C pointer type.
    QualType typeBound = GetTypeFromParser(parsedTypeBound, &typeBoundInfo);
    if (typeBound->isObjCObjectPointerType()) {
      // okay
    } else if (typeBound->isObjCObjectType()) {
      // The user forgot the * on an Objective-C pointer type, e.g.,
      // "T : NSView".
      SourceLocation starLoc = getLocForEndOfToken(
                                 typeBoundInfo->getTypeLoc().getEndLoc());
      Diag(typeBoundInfo->getTypeLoc().getBeginLoc(),
           diag::err_objc_type_param_bound_missing_pointer)
        << typeBound << paramName
        << FixItHint::CreateInsertion(starLoc, " *");

      // Create a new type location builder so we can update the type
      // location information we have.
      TypeLocBuilder builder;
      builder.pushFullCopy(typeBoundInfo->getTypeLoc());

      // Create the Objective-C pointer type.
      typeBound = Context.getObjCObjectPointerType(typeBound);
      ObjCObjectPointerTypeLoc newT
        = builder.push<ObjCObjectPointerTypeLoc>(typeBound);
      newT.setStarLoc(starLoc);

      // Form the new type source information.
      typeBoundInfo = builder.getTypeSourceInfo(Context, typeBound);
    } else {
      // Not a valid type bound.
      Diag(typeBoundInfo->getTypeLoc().getBeginLoc(),
           diag::err_objc_type_param_bound_nonobject)
        << typeBound << paramName;

      // Forget the bound; we'll default to id later.
      typeBoundInfo = nullptr;
    }

    // Type bounds cannot have qualifiers (even indirectly) or explicit
    // nullability.
    if (typeBoundInfo) {
      QualType typeBound = typeBoundInfo->getType();
      TypeLoc qual = typeBoundInfo->getTypeLoc().findExplicitQualifierLoc();
      if (qual || typeBound.hasQualifiers()) {
        bool diagnosed = false;
        SourceRange rangeToRemove;
        if (qual) {
          if (auto attr = qual.getAs<AttributedTypeLoc>()) {
            rangeToRemove = attr.getLocalSourceRange();
            if (attr.getTypePtr()->getImmediateNullability()) {
              Diag(attr.getBeginLoc(),
                   diag::err_objc_type_param_bound_explicit_nullability)
                  << paramName << typeBound
                  << FixItHint::CreateRemoval(rangeToRemove);
              diagnosed = true;
            }
          }
        }

        if (!diagnosed) {
          Diag(qual ? qual.getBeginLoc()
                    : typeBoundInfo->getTypeLoc().getBeginLoc(),
               diag::err_objc_type_param_bound_qualified)
              << paramName << typeBound
              << typeBound.getQualifiers().getAsString()
              << FixItHint::CreateRemoval(rangeToRemove);
        }

        // If the type bound has qualifiers other than CVR, we need to strip
        // them or we'll probably assert later when trying to apply new
        // qualifiers.
        Qualifiers quals = typeBound.getQualifiers();
        quals.removeCVRQualifiers();
        if (!quals.empty()) {
          typeBoundInfo =
             Context.getTrivialTypeSourceInfo(typeBound.getUnqualifiedType());
        }
      }
    }
  }

  // If there was no explicit type bound (or we removed it due to an error),
  // use 'id' instead.
  if (!typeBoundInfo) {
    colonLoc = SourceLocation();
    typeBoundInfo = Context.getTrivialTypeSourceInfo(Context.getObjCIdType());
  }

  // Create the type parameter.
  return ObjCTypeParamDecl::Create(Context, CurContext, variance, varianceLoc,
                                   index, paramLoc, paramName, colonLoc,
                                   typeBoundInfo);
}

ObjCTypeParamList *Sema::actOnObjCTypeParamList(Scope *S,
                                                SourceLocation lAngleLoc,
                                                ArrayRef<Decl *> typeParamsIn,
                                                SourceLocation rAngleLoc) {
  // We know that the array only contains Objective-C type parameters.
  ArrayRef<ObjCTypeParamDecl *>
    typeParams(
      reinterpret_cast<ObjCTypeParamDecl * const *>(typeParamsIn.data()),
      typeParamsIn.size());

  // Diagnose redeclarations of type parameters.
  // We do this now because Objective-C type parameters aren't pushed into
  // scope until later (after the instance variable block), but we want the
  // diagnostics to occur right after we parse the type parameter list.
  llvm::SmallDenseMap<IdentifierInfo *, ObjCTypeParamDecl *> knownParams;
  for (auto typeParam : typeParams) {
    auto known = knownParams.find(typeParam->getIdentifier());
    if (known != knownParams.end()) {
      Diag(typeParam->getLocation(), diag::err_objc_type_param_redecl)
        << typeParam->getIdentifier()
        << SourceRange(known->second->getLocation());

      typeParam->setInvalidDecl();
    } else {
      knownParams.insert(std::make_pair(typeParam->getIdentifier(), typeParam));

      // Push the type parameter into scope.
      PushOnScopeChains(typeParam, S, /*AddToContext=*/false);
    }
  }

  // Create the parameter list.
  return ObjCTypeParamList::create(Context, lAngleLoc, typeParams, rAngleLoc);
}

void Sema::popObjCTypeParamList(Scope *S, ObjCTypeParamList *typeParamList) {
  for (auto typeParam : *typeParamList) {
    if (!typeParam->isInvalidDecl()) {
      S->RemoveDecl(typeParam);
      IdResolver.RemoveDecl(typeParam);
    }
  }
}

namespace {
  /// The context in which an Objective-C type parameter list occurs, for use
  /// in diagnostics.
  enum class TypeParamListContext {
    ForwardDeclaration,
    Definition,
    Category,
    Extension
  };
} // end anonymous namespace

/// Check consistency between two Objective-C type parameter lists, e.g.,
/// between a category/extension and an \@interface or between an \@class and an
/// \@interface.
static bool checkTypeParamListConsistency(Sema &S,
                                          ObjCTypeParamList *prevTypeParams,
                                          ObjCTypeParamList *newTypeParams,
                                          TypeParamListContext newContext) {
  // If the sizes don't match, complain about that.
  if (prevTypeParams->size() != newTypeParams->size()) {
    SourceLocation diagLoc;
    if (newTypeParams->size() > prevTypeParams->size()) {
      diagLoc = newTypeParams->begin()[prevTypeParams->size()]->getLocation();
    } else {
      diagLoc = S.getLocForEndOfToken(newTypeParams->back()->getEndLoc());
    }

    S.Diag(diagLoc, diag::err_objc_type_param_arity_mismatch)
      << static_cast<unsigned>(newContext)
      << (newTypeParams->size() > prevTypeParams->size())
      << prevTypeParams->size()
      << newTypeParams->size();

    return true;
  }

  // Match up the type parameters.
  for (unsigned i = 0, n = prevTypeParams->size(); i != n; ++i) {
    ObjCTypeParamDecl *prevTypeParam = prevTypeParams->begin()[i];
    ObjCTypeParamDecl *newTypeParam = newTypeParams->begin()[i];

    // Check for consistency of the variance.
    if (newTypeParam->getVariance() != prevTypeParam->getVariance()) {
      if (newTypeParam->getVariance() == ObjCTypeParamVariance::Invariant &&
          newContext != TypeParamListContext::Definition) {
        // When the new type parameter is invariant and is not part
        // of the definition, just propagate the variance.
        newTypeParam->setVariance(prevTypeParam->getVariance());
      } else if (prevTypeParam->getVariance()
                   == ObjCTypeParamVariance::Invariant &&
                 !(isa<ObjCInterfaceDecl>(prevTypeParam->getDeclContext()) &&
                   cast<ObjCInterfaceDecl>(prevTypeParam->getDeclContext())
                     ->getDefinition() == prevTypeParam->getDeclContext())) {
        // When the old parameter is invariant and was not part of the
        // definition, just ignore the difference because it doesn't
        // matter.
      } else {
        {
          // Diagnose the conflict and update the second declaration.
          SourceLocation diagLoc = newTypeParam->getVarianceLoc();
          if (diagLoc.isInvalid())
            diagLoc = newTypeParam->getBeginLoc();

          auto diag = S.Diag(diagLoc,
                             diag::err_objc_type_param_variance_conflict)
                        << static_cast<unsigned>(newTypeParam->getVariance())
                        << newTypeParam->getDeclName()
                        << static_cast<unsigned>(prevTypeParam->getVariance())
                        << prevTypeParam->getDeclName();
          switch (prevTypeParam->getVariance()) {
          case ObjCTypeParamVariance::Invariant:
            diag << FixItHint::CreateRemoval(newTypeParam->getVarianceLoc());
            break;

          case ObjCTypeParamVariance::Covariant:
          case ObjCTypeParamVariance::Contravariant: {
            StringRef newVarianceStr
               = prevTypeParam->getVariance() == ObjCTypeParamVariance::Covariant
                   ? "__covariant"
                   : "__contravariant";
            if (newTypeParam->getVariance()
                  == ObjCTypeParamVariance::Invariant) {
              diag << FixItHint::CreateInsertion(newTypeParam->getBeginLoc(),
                                                 (newVarianceStr + " ").str());
            } else {
              diag << FixItHint::CreateReplacement(newTypeParam->getVarianceLoc(),
                                               newVarianceStr);
            }
          }
          }
        }

        S.Diag(prevTypeParam->getLocation(), diag::note_objc_type_param_here)
          << prevTypeParam->getDeclName();

        // Override the variance.
        newTypeParam->setVariance(prevTypeParam->getVariance());
      }
    }

    // If the bound types match, there's nothing to do.
    if (S.Context.hasSameType(prevTypeParam->getUnderlyingType(),
                              newTypeParam->getUnderlyingType()))
      continue;

    // If the new type parameter's bound was explicit, complain about it being
    // different from the original.
    if (newTypeParam->hasExplicitBound()) {
      SourceRange newBoundRange = newTypeParam->getTypeSourceInfo()
                                    ->getTypeLoc().getSourceRange();
      S.Diag(newBoundRange.getBegin(), diag::err_objc_type_param_bound_conflict)
        << newTypeParam->getUnderlyingType()
        << newTypeParam->getDeclName()
        << prevTypeParam->hasExplicitBound()
        << prevTypeParam->getUnderlyingType()
        << (newTypeParam->getDeclName() == prevTypeParam->getDeclName())
        << prevTypeParam->getDeclName()
        << FixItHint::CreateReplacement(
             newBoundRange,
             prevTypeParam->getUnderlyingType().getAsString(
               S.Context.getPrintingPolicy()));

      S.Diag(prevTypeParam->getLocation(), diag::note_objc_type_param_here)
        << prevTypeParam->getDeclName();

      // Override the new type parameter's bound type with the previous type,
      // so that it's consistent.
      newTypeParam->setTypeSourceInfo(
        S.Context.getTrivialTypeSourceInfo(prevTypeParam->getUnderlyingType()));
      continue;
    }

    // The new type parameter got the implicit bound of 'id'. That's okay for
    // categories and extensions (overwrite it later), but not for forward
    // declarations and @interfaces, because those must be standalone.
    if (newContext == TypeParamListContext::ForwardDeclaration ||
        newContext == TypeParamListContext::Definition) {
      // Diagnose this problem for forward declarations and definitions.
      SourceLocation insertionLoc
        = S.getLocForEndOfToken(newTypeParam->getLocation());
      std::string newCode
        = " : " + prevTypeParam->getUnderlyingType().getAsString(
                    S.Context.getPrintingPolicy());
      S.Diag(newTypeParam->getLocation(),
             diag::err_objc_type_param_bound_missing)
        << prevTypeParam->getUnderlyingType()
        << newTypeParam->getDeclName()
        << (newContext == TypeParamListContext::ForwardDeclaration)
        << FixItHint::CreateInsertion(insertionLoc, newCode);

      S.Diag(prevTypeParam->getLocation(), diag::note_objc_type_param_here)
        << prevTypeParam->getDeclName();
    }

    // Update the new type parameter's bound to match the previous one.
    newTypeParam->setTypeSourceInfo(
      S.Context.getTrivialTypeSourceInfo(prevTypeParam->getUnderlyingType()));
  }

  return false;
}

Decl *Sema::ActOnStartClassInterface(
    Scope *S, SourceLocation AtInterfaceLoc, IdentifierInfo *ClassName,
    SourceLocation ClassLoc, ObjCTypeParamList *typeParamList,
    IdentifierInfo *SuperName, SourceLocation SuperLoc,
    ArrayRef<ParsedType> SuperTypeArgs, SourceRange SuperTypeArgsRange,
    Decl *const *ProtoRefs, unsigned NumProtoRefs,
    const SourceLocation *ProtoLocs, SourceLocation EndProtoLoc,
    const ParsedAttributesView &AttrList) {
  assert(ClassName && "Missing class identifier");

  // Check for another declaration kind with the same name.
  NamedDecl *PrevDecl =
      LookupSingleName(TUScope, ClassName, ClassLoc, LookupOrdinaryName,
                       forRedeclarationInCurContext());

  if (PrevDecl && !isa<ObjCInterfaceDecl>(PrevDecl)) {
    Diag(ClassLoc, diag::err_redefinition_different_kind) << ClassName;
    Diag(PrevDecl->getLocation(), diag::note_previous_definition);
  }

  // Create a declaration to describe this @interface.
  ObjCInterfaceDecl* PrevIDecl = dyn_cast_or_null<ObjCInterfaceDecl>(PrevDecl);

  if (PrevIDecl && PrevIDecl->getIdentifier() != ClassName) {
    // A previous decl with a different name is because of
    // @compatibility_alias, for example:
    // \code
    //   @class NewImage;
    //   @compatibility_alias OldImage NewImage;
    // \endcode
    // A lookup for 'OldImage' will return the 'NewImage' decl.
    //
    // In such a case use the real declaration name, instead of the alias one,
    // otherwise we will break IdentifierResolver and redecls-chain invariants.
    // FIXME: If necessary, add a bit to indicate that this ObjCInterfaceDecl
    // has been aliased.
    ClassName = PrevIDecl->getIdentifier();
  }

  // If there was a forward declaration with type parameters, check
  // for consistency.
  if (PrevIDecl) {
    if (ObjCTypeParamList *prevTypeParamList = PrevIDecl->getTypeParamList()) {
      if (typeParamList) {
        // Both have type parameter lists; check for consistency.
        if (checkTypeParamListConsistency(*this, prevTypeParamList,
                                          typeParamList,
                                          TypeParamListContext::Definition)) {
          typeParamList = nullptr;
        }
      } else {
        Diag(ClassLoc, diag::err_objc_parameterized_forward_class_first)
          << ClassName;
        Diag(prevTypeParamList->getLAngleLoc(), diag::note_previous_decl)
          << ClassName;

        // Clone the type parameter list.
        SmallVector<ObjCTypeParamDecl *, 4> clonedTypeParams;
        for (auto typeParam : *prevTypeParamList) {
          clonedTypeParams.push_back(
            ObjCTypeParamDecl::Create(
              Context,
              CurContext,
              typeParam->getVariance(),
              SourceLocation(),
              typeParam->getIndex(),
              SourceLocation(),
              typeParam->getIdentifier(),
              SourceLocation(),
              Context.getTrivialTypeSourceInfo(typeParam->getUnderlyingType())));
        }

        typeParamList = ObjCTypeParamList::create(Context,
                                                  SourceLocation(),
                                                  clonedTypeParams,
                                                  SourceLocation());
      }
    }
  }

  ObjCInterfaceDecl *IDecl
    = ObjCInterfaceDecl::Create(Context, CurContext, AtInterfaceLoc, ClassName,
                                typeParamList, PrevIDecl, ClassLoc);
  if (PrevIDecl) {
    // Class already seen. Was it a definition?
    if (ObjCInterfaceDecl *Def = PrevIDecl->getDefinition()) {
      Diag(AtInterfaceLoc, diag::err_duplicate_class_def)
        << PrevIDecl->getDeclName();
      Diag(Def->getLocation(), diag::note_previous_definition);
      IDecl->setInvalidDecl();
    }
  }

  ProcessDeclAttributeList(TUScope, IDecl, AttrList);
  AddPragmaAttributes(TUScope, IDecl);
  PushOnScopeChains(IDecl, TUScope);

  // Start the definition of this class. If we're in a redefinition case, there
  // may already be a definition, so we'll end up adding to it.
  if (!IDecl->hasDefinition())
    IDecl->startDefinition();

  if (SuperName) {
    // Diagnose availability in the context of the @interface.
    ContextRAII SavedContext(*this, IDecl);

    ActOnSuperClassOfClassInterface(S, AtInterfaceLoc, IDecl,
                                    ClassName, ClassLoc,
                                    SuperName, SuperLoc, SuperTypeArgs,
                                    SuperTypeArgsRange);
  } else { // we have a root class.
    IDecl->setEndOfDefinitionLoc(ClassLoc);
  }

  // Check then save referenced protocols.
  if (NumProtoRefs) {
    diagnoseUseOfProtocols(*this, IDecl, (ObjCProtocolDecl*const*)ProtoRefs,
                           NumProtoRefs, ProtoLocs);
    IDecl->setProtocolList((ObjCProtocolDecl*const*)ProtoRefs, NumProtoRefs,
                           ProtoLocs, Context);
    IDecl->setEndOfDefinitionLoc(EndProtoLoc);
  }

  CheckObjCDeclScope(IDecl);
  return ActOnObjCContainerStartDefinition(IDecl);
}

/// ActOnTypedefedProtocols - this action finds protocol list as part of the
/// typedef'ed use for a qualified super class and adds them to the list
/// of the protocols.
void Sema::ActOnTypedefedProtocols(SmallVectorImpl<Decl *> &ProtocolRefs,
                                  SmallVectorImpl<SourceLocation> &ProtocolLocs,
                                   IdentifierInfo *SuperName,
                                   SourceLocation SuperLoc) {
  if (!SuperName)
    return;
  NamedDecl* IDecl = LookupSingleName(TUScope, SuperName, SuperLoc,
                                      LookupOrdinaryName);
  if (!IDecl)
    return;

  if (const TypedefNameDecl *TDecl = dyn_cast_or_null<TypedefNameDecl>(IDecl)) {
    QualType T = TDecl->getUnderlyingType();
    if (T->isObjCObjectType())
      if (const ObjCObjectType *OPT = T->getAs<ObjCObjectType>()) {
        ProtocolRefs.append(OPT->qual_begin(), OPT->qual_end());
        // FIXME: Consider whether this should be an invalid loc since the loc
        // is not actually pointing to a protocol name reference but to the
        // typedef reference. Note that the base class name loc is also pointing
        // at the typedef.
        ProtocolLocs.append(OPT->getNumProtocols(), SuperLoc);
      }
  }
}

/// ActOnCompatibilityAlias - this action is called after complete parsing of
/// a \@compatibility_alias declaration. It sets up the alias relationships.
Decl *Sema::ActOnCompatibilityAlias(SourceLocation AtLoc,
                                    IdentifierInfo *AliasName,
                                    SourceLocation AliasLocation,
                                    IdentifierInfo *ClassName,
                                    SourceLocation ClassLocation) {
  // Look for previous declaration of alias name
  NamedDecl *ADecl =
      LookupSingleName(TUScope, AliasName, AliasLocation, LookupOrdinaryName,
                       forRedeclarationInCurContext());
  if (ADecl) {
    Diag(AliasLocation, diag::err_conflicting_aliasing_type) << AliasName;
    Diag(ADecl->getLocation(), diag::note_previous_declaration);
    return nullptr;
  }
  // Check for class declaration
  NamedDecl *CDeclU =
      LookupSingleName(TUScope, ClassName, ClassLocation, LookupOrdinaryName,
                       forRedeclarationInCurContext());
  if (const TypedefNameDecl *TDecl =
        dyn_cast_or_null<TypedefNameDecl>(CDeclU)) {
    QualType T = TDecl->getUnderlyingType();
    if (T->isObjCObjectType()) {
      if (NamedDecl *IDecl = T->getAs<ObjCObjectType>()->getInterface()) {
        ClassName = IDecl->getIdentifier();
        CDeclU = LookupSingleName(TUScope, ClassName, ClassLocation,
                                  LookupOrdinaryName,
                                  forRedeclarationInCurContext());
      }
    }
  }
  ObjCInterfaceDecl *CDecl = dyn_cast_or_null<ObjCInterfaceDecl>(CDeclU);
  if (!CDecl) {
    Diag(ClassLocation, diag::warn_undef_interface) << ClassName;
    if (CDeclU)
      Diag(CDeclU->getLocation(), diag::note_previous_declaration);
    return nullptr;
  }

  // Everything checked out, instantiate a new alias declaration AST.
  ObjCCompatibleAliasDecl *AliasDecl =
    ObjCCompatibleAliasDecl::Create(Context, CurContext, AtLoc, AliasName, CDecl);

  if (!CheckObjCDeclScope(AliasDecl))
    PushOnScopeChains(AliasDecl, TUScope);

  return AliasDecl;
}

bool Sema::CheckForwardProtocolDeclarationForCircularDependency(
  IdentifierInfo *PName,
  SourceLocation &Ploc, SourceLocation PrevLoc,
  const ObjCList<ObjCProtocolDecl> &PList) {

  bool res = false;
  for (ObjCList<ObjCProtocolDecl>::iterator I = PList.begin(),
       E = PList.end(); I != E; ++I) {
    if (ObjCProtocolDecl *PDecl = LookupProtocol((*I)->getIdentifier(),
                                                 Ploc)) {
      if (PDecl->getIdentifier() == PName) {
        Diag(Ploc, diag::err_protocol_has_circular_dependency);
        Diag(PrevLoc, diag::note_previous_definition);
        res = true;
      }

      if (!PDecl->hasDefinition())
        continue;

      if (CheckForwardProtocolDeclarationForCircularDependency(PName, Ploc,
            PDecl->getLocation(), PDecl->getReferencedProtocols()))
        res = true;
    }
  }
  return res;
}

Decl *Sema::ActOnStartProtocolInterface(
    SourceLocation AtProtoInterfaceLoc, IdentifierInfo *ProtocolName,
    SourceLocation ProtocolLoc, Decl *const *ProtoRefs, unsigned NumProtoRefs,
    const SourceLocation *ProtoLocs, SourceLocation EndProtoLoc,
    const ParsedAttributesView &AttrList) {
  bool err = false;
  // FIXME: Deal with AttrList.
  assert(ProtocolName && "Missing protocol identifier");
  ObjCProtocolDecl *PrevDecl = LookupProtocol(ProtocolName, ProtocolLoc,
                                              forRedeclarationInCurContext());
  ObjCProtocolDecl *PDecl = nullptr;
  if (ObjCProtocolDecl *Def = PrevDecl? PrevDecl->getDefinition() : nullptr) {
    // If we already have a definition, complain.
    Diag(ProtocolLoc, diag::warn_duplicate_protocol_def) << ProtocolName;
    Diag(Def->getLocation(), diag::note_previous_definition);

    // Create a new protocol that is completely distinct from previous
    // declarations, and do not make this protocol available for name lookup.
    // That way, we'll end up completely ignoring the duplicate.
    // FIXME: Can we turn this into an error?
    PDecl = ObjCProtocolDecl::Create(Context, CurContext, ProtocolName,
                                     ProtocolLoc, AtProtoInterfaceLoc,
                                     /*PrevDecl=*/nullptr);

    // If we are using modules, add the decl to the context in order to
    // serialize something meaningful.
    if (getLangOpts().Modules)
      PushOnScopeChains(PDecl, TUScope);
    PDecl->startDefinition();
  } else {
    if (PrevDecl) {
      // Check for circular dependencies among protocol declarations. This can
      // only happen if this protocol was forward-declared.
      ObjCList<ObjCProtocolDecl> PList;
      PList.set((ObjCProtocolDecl *const*)ProtoRefs, NumProtoRefs, Context);
      err = CheckForwardProtocolDeclarationForCircularDependency(
              ProtocolName, ProtocolLoc, PrevDecl->getLocation(), PList);
    }

    // Create the new declaration.
    PDecl = ObjCProtocolDecl::Create(Context, CurContext, ProtocolName,
                                     ProtocolLoc, AtProtoInterfaceLoc,
                                     /*PrevDecl=*/PrevDecl);

    PushOnScopeChains(PDecl, TUScope);
    PDecl->startDefinition();
  }

  ProcessDeclAttributeList(TUScope, PDecl, AttrList);
  AddPragmaAttributes(TUScope, PDecl);

  // Merge attributes from previous declarations.
  if (PrevDecl)
    mergeDeclAttributes(PDecl, PrevDecl);

  if (!err && NumProtoRefs ) {
    /// Check then save referenced protocols.
    diagnoseUseOfProtocols(*this, PDecl, (ObjCProtocolDecl*const*)ProtoRefs,
                           NumProtoRefs, ProtoLocs);
    PDecl->setProtocolList((ObjCProtocolDecl*const*)ProtoRefs, NumProtoRefs,
                           ProtoLocs, Context);
  }

  CheckObjCDeclScope(PDecl);
  return ActOnObjCContainerStartDefinition(PDecl);
}

static bool NestedProtocolHasNoDefinition(ObjCProtocolDecl *PDecl,
                                          ObjCProtocolDecl *&UndefinedProtocol) {
  if (!PDecl->hasDefinition() || PDecl->getDefinition()->isHidden()) {
    UndefinedProtocol = PDecl;
    return true;
  }

  for (auto *PI : PDecl->protocols())
    if (NestedProtocolHasNoDefinition(PI, UndefinedProtocol)) {
      UndefinedProtocol = PI;
      return true;
    }
  return false;
}

/// FindProtocolDeclaration - This routine looks up protocols and
/// issues an error if they are not declared. It returns list of
/// protocol declarations in its 'Protocols' argument.
void
Sema::FindProtocolDeclaration(bool WarnOnDeclarations, bool ForObjCContainer,
                              ArrayRef<IdentifierLocPair> ProtocolId,
                              SmallVectorImpl<Decl *> &Protocols) {
  for (const IdentifierLocPair &Pair : ProtocolId) {
    ObjCProtocolDecl *PDecl = LookupProtocol(Pair.first, Pair.second);
    if (!PDecl) {
      TypoCorrection Corrected = CorrectTypo(
          DeclarationNameInfo(Pair.first, Pair.second),
          LookupObjCProtocolName, TUScope, nullptr,
          llvm::make_unique<DeclFilterCCC<ObjCProtocolDecl>>(),
          CTK_ErrorRecovery);
      if ((PDecl = Corrected.getCorrectionDeclAs<ObjCProtocolDecl>()))
        diagnoseTypo(Corrected, PDiag(diag::err_undeclared_protocol_suggest)
                                    << Pair.first);
    }

    if (!PDecl) {
      Diag(Pair.second, diag::err_undeclared_protocol) << Pair.first;
      continue;
    }
    // If this is a forward protocol declaration, get its definition.
    if (!PDecl->isThisDeclarationADefinition() && PDecl->getDefinition())
      PDecl = PDecl->getDefinition();

    // For an objc container, delay protocol reference checking until after we
    // can set the objc decl as the availability context, otherwise check now.
    if (!ForObjCContainer) {
      (void)DiagnoseUseOfDecl(PDecl, Pair.second);
    }

    // If this is a forward declaration and we are supposed to warn in this
    // case, do it.
    // FIXME: Recover nicely in the hidden case.
    ObjCProtocolDecl *UndefinedProtocol;

    if (WarnOnDeclarations &&
        NestedProtocolHasNoDefinition(PDecl, UndefinedProtocol)) {
      Diag(Pair.second, diag::warn_undef_protocolref) << Pair.first;
      Diag(UndefinedProtocol->getLocation(), diag::note_protocol_decl_undefined)
        << UndefinedProtocol;
    }
    Protocols.push_back(PDecl);
  }
}

namespace {
// Callback to only accept typo corrections that are either
// Objective-C protocols or valid Objective-C type arguments.
class ObjCTypeArgOrProtocolValidatorCCC : public CorrectionCandidateCallback {
  ASTContext &Context;
  Sema::LookupNameKind LookupKind;
 public:
  ObjCTypeArgOrProtocolValidatorCCC(ASTContext &context,
                                    Sema::LookupNameKind lookupKind)
    : Context(context), LookupKind(lookupKind) { }

  bool ValidateCandidate(const TypoCorrection &candidate) override {
    // If we're allowed to find protocols and we have a protocol, accept it.
    if (LookupKind != Sema::LookupOrdinaryName) {
      if (candidate.getCorrectionDeclAs<ObjCProtocolDecl>())
        return true;
    }

    // If we're allowed to find type names and we have one, accept it.
    if (LookupKind != Sema::LookupObjCProtocolName) {
      // If we have a type declaration, we might accept this result.
      if (auto typeDecl = candidate.getCorrectionDeclAs<TypeDecl>()) {
        // If we found a tag declaration outside of C++, skip it. This
        // can happy because we look for any name when there is no
        // bias to protocol or type names.
        if (isa<RecordDecl>(typeDecl) && !Context.getLangOpts().CPlusPlus)
          return false;

        // Make sure the type is something we would accept as a type
        // argument.
        auto type = Context.getTypeDeclType(typeDecl);
        if (type->isObjCObjectPointerType() ||
            type->isBlockPointerType() ||
            type->isDependentType() ||
            type->isObjCObjectType())
          return true;

        return false;
      }

      // If we have an Objective-C class type, accept it; there will
      // be another fix to add the '*'.
      if (candidate.getCorrectionDeclAs<ObjCInterfaceDecl>())
        return true;

      return false;
    }

    return false;
  }
};
} // end anonymous namespace

void Sema::DiagnoseTypeArgsAndProtocols(IdentifierInfo *ProtocolId,
                                        SourceLocation ProtocolLoc,
                                        IdentifierInfo *TypeArgId,
                                        SourceLocation TypeArgLoc,
                                        bool SelectProtocolFirst) {
  Diag(TypeArgLoc, diag::err_objc_type_args_and_protocols)
      << SelectProtocolFirst << TypeArgId << ProtocolId
      << SourceRange(ProtocolLoc);
}

void Sema::actOnObjCTypeArgsOrProtocolQualifiers(
       Scope *S,
       ParsedType baseType,
       SourceLocation lAngleLoc,
       ArrayRef<IdentifierInfo *> identifiers,
       ArrayRef<SourceLocation> identifierLocs,
       SourceLocation rAngleLoc,
       SourceLocation &typeArgsLAngleLoc,
       SmallVectorImpl<ParsedType> &typeArgs,
       SourceLocation &typeArgsRAngleLoc,
       SourceLocation &protocolLAngleLoc,
       SmallVectorImpl<Decl *> &protocols,
       SourceLocation &protocolRAngleLoc,
       bool warnOnIncompleteProtocols) {
  // Local function that updates the declaration specifiers with
  // protocol information.
  unsigned numProtocolsResolved = 0;
  auto resolvedAsProtocols = [&] {
    assert(numProtocolsResolved == identifiers.size() && "Unresolved protocols");

    // Determine whether the base type is a parameterized class, in
    // which case we want to warn about typos such as
    // "NSArray<NSObject>" (that should be NSArray<NSObject *>).
    ObjCInterfaceDecl *baseClass = nullptr;
    QualType base = GetTypeFromParser(baseType, nullptr);
    bool allAreTypeNames = false;
    SourceLocation firstClassNameLoc;
    if (!base.isNull()) {
      if (const auto *objcObjectType = base->getAs<ObjCObjectType>()) {
        baseClass = objcObjectType->getInterface();
        if (baseClass) {
          if (auto typeParams = baseClass->getTypeParamList()) {
            if (typeParams->size() == numProtocolsResolved) {
              // Note that we should be looking for type names, too.
              allAreTypeNames = true;
            }
          }
        }
      }
    }

    for (unsigned i = 0, n = protocols.size(); i != n; ++i) {
      ObjCProtocolDecl *&proto
        = reinterpret_cast<ObjCProtocolDecl *&>(protocols[i]);
      // For an objc container, delay protocol reference checking until after we
      // can set the objc decl as the availability context, otherwise check now.
      if (!warnOnIncompleteProtocols) {
        (void)DiagnoseUseOfDecl(proto, identifierLocs[i]);
      }

      // If this is a forward protocol declaration, get its definition.
      if (!proto->isThisDeclarationADefinition() && proto->getDefinition())
        proto = proto->getDefinition();

      // If this is a forward declaration and we are supposed to warn in this
      // case, do it.
      // FIXME: Recover nicely in the hidden case.
      ObjCProtocolDecl *forwardDecl = nullptr;
      if (warnOnIncompleteProtocols &&
          NestedProtocolHasNoDefinition(proto, forwardDecl)) {
        Diag(identifierLocs[i], diag::warn_undef_protocolref)
          << proto->getDeclName();
        Diag(forwardDecl->getLocation(), diag::note_protocol_decl_undefined)
          << forwardDecl;
      }

      // If everything this far has been a type name (and we care
      // about such things), check whether this name refers to a type
      // as well.
      if (allAreTypeNames) {
        if (auto *decl = LookupSingleName(S, identifiers[i], identifierLocs[i],
                                          LookupOrdinaryName)) {
          if (isa<ObjCInterfaceDecl>(decl)) {
            if (firstClassNameLoc.isInvalid())
              firstClassNameLoc = identifierLocs[i];
          } else if (!isa<TypeDecl>(decl)) {
            // Not a type.
            allAreTypeNames = false;
          }
        } else {
          allAreTypeNames = false;
        }
      }
    }

    // All of the protocols listed also have type names, and at least
    // one is an Objective-C class name. Check whether all of the
    // protocol conformances are declared by the base class itself, in
    // which case we warn.
    if (allAreTypeNames && firstClassNameLoc.isValid()) {
      llvm::SmallPtrSet<ObjCProtocolDecl*, 8> knownProtocols;
      Context.CollectInheritedProtocols(baseClass, knownProtocols);
      bool allProtocolsDeclared = true;
      for (auto proto : protocols) {
        if (knownProtocols.count(static_cast<ObjCProtocolDecl *>(proto)) == 0) {
          allProtocolsDeclared = false;
          break;
        }
      }

      if (allProtocolsDeclared) {
        Diag(firstClassNameLoc, diag::warn_objc_redundant_qualified_class_type)
          << baseClass->getDeclName() << SourceRange(lAngleLoc, rAngleLoc)
          << FixItHint::CreateInsertion(getLocForEndOfToken(firstClassNameLoc),
                                        " *");
      }
    }

    protocolLAngleLoc = lAngleLoc;
    protocolRAngleLoc = rAngleLoc;
    assert(protocols.size() == identifierLocs.size());
  };

  // Attempt to resolve all of the identifiers as protocols.
  for (unsigned i = 0, n = identifiers.size(); i != n; ++i) {
    ObjCProtocolDecl *proto = LookupProtocol(identifiers[i], identifierLocs[i]);
    protocols.push_back(proto);
    if (proto)
      ++numProtocolsResolved;
  }

  // If all of the names were protocols, these were protocol qualifiers.
  if (numProtocolsResolved == identifiers.size())
    return resolvedAsProtocols();

  // Attempt to resolve all of the identifiers as type names or
  // Objective-C class names. The latter is technically ill-formed,
  // but is probably something like \c NSArray<NSView *> missing the
  // \c*.
  typedef llvm::PointerUnion<TypeDecl *, ObjCInterfaceDecl *> TypeOrClassDecl;
  SmallVector<TypeOrClassDecl, 4> typeDecls;
  unsigned numTypeDeclsResolved = 0;
  for (unsigned i = 0, n = identifiers.size(); i != n; ++i) {
    NamedDecl *decl = LookupSingleName(S, identifiers[i], identifierLocs[i],
                                       LookupOrdinaryName);
    if (!decl) {
      typeDecls.push_back(TypeOrClassDecl());
      continue;
    }

    if (auto typeDecl = dyn_cast<TypeDecl>(decl)) {
      typeDecls.push_back(typeDecl);
      ++numTypeDeclsResolved;
      continue;
    }

    if (auto objcClass = dyn_cast<ObjCInterfaceDecl>(decl)) {
      typeDecls.push_back(objcClass);
      ++numTypeDeclsResolved;
      continue;
    }

    typeDecls.push_back(TypeOrClassDecl());
  }

  AttributeFactory attrFactory;

  // Local function that forms a reference to the given type or
  // Objective-C class declaration.
  auto resolveTypeReference = [&](TypeOrClassDecl typeDecl, SourceLocation loc)
                                -> TypeResult {
    // Form declaration specifiers. They simply refer to the type.
    DeclSpec DS(attrFactory);
    const char* prevSpec; // unused
    unsigned diagID; // unused
    QualType type;
    if (auto *actualTypeDecl = typeDecl.dyn_cast<TypeDecl *>())
      type = Context.getTypeDeclType(actualTypeDecl);
    else
      type = Context.getObjCInterfaceType(typeDecl.get<ObjCInterfaceDecl *>());
    TypeSourceInfo *parsedTSInfo = Context.getTrivialTypeSourceInfo(type, loc);
    ParsedType parsedType = CreateParsedType(type, parsedTSInfo);
    DS.SetTypeSpecType(DeclSpec::TST_typename, loc, prevSpec, diagID,
                       parsedType, Context.getPrintingPolicy());
    // Use the identifier location for the type source range.
    DS.SetRangeStart(loc);
    DS.SetRangeEnd(loc);

    // Form the declarator.
    Declarator D(DS, DeclaratorContext::TypeNameContext);

    // If we have a typedef of an Objective-C class type that is missing a '*',
    // add the '*'.
    if (type->getAs<ObjCInterfaceType>()) {
      SourceLocation starLoc = getLocForEndOfToken(loc);
      D.AddTypeInfo(DeclaratorChunk::getPointer(/*typeQuals=*/0, starLoc,
                                                SourceLocation(),
                                                SourceLocation(),
                                                SourceLocation(),
                                                SourceLocation(),
                                                SourceLocation()),
                                                starLoc);

      // Diagnose the missing '*'.
      Diag(loc, diag::err_objc_type_arg_missing_star)
        << type
        << FixItHint::CreateInsertion(starLoc, " *");
    }

    // Convert this to a type.
    return ActOnTypeName(S, D);
  };

  // Local function that updates the declaration specifiers with
  // type argument information.
  auto resolvedAsTypeDecls = [&] {
    // We did not resolve these as protocols.
    protocols.clear();

    assert(numTypeDeclsResolved == identifiers.size() && "Unresolved type decl");
    // Map type declarations to type arguments.
    for (unsigned i = 0, n = identifiers.size(); i != n; ++i) {
      // Map type reference to a type.
      TypeResult type = resolveTypeReference(typeDecls[i], identifierLocs[i]);
      if (!type.isUsable()) {
        typeArgs.clear();
        return;
      }

      typeArgs.push_back(type.get());
    }

    typeArgsLAngleLoc = lAngleLoc;
    typeArgsRAngleLoc = rAngleLoc;
  };

  // If all of the identifiers can be resolved as type names or
  // Objective-C class names, we have type arguments.
  if (numTypeDeclsResolved == identifiers.size())
    return resolvedAsTypeDecls();

  // Error recovery: some names weren't found, or we have a mix of
  // type and protocol names. Go resolve all of the unresolved names
  // and complain if we can't find a consistent answer.
  LookupNameKind lookupKind = LookupAnyName;
  for (unsigned i = 0, n = identifiers.size(); i != n; ++i) {
    // If we already have a protocol or type. Check whether it is the
    // right thing.
    if (protocols[i] || typeDecls[i]) {
      // If we haven't figured out whether we want types or protocols
      // yet, try to figure it out from this name.
      if (lookupKind == LookupAnyName) {
        // If this name refers to both a protocol and a type (e.g., \c
        // NSObject), don't conclude anything yet.
        if (protocols[i] && typeDecls[i])
          continue;

        // Otherwise, let this name decide whether we'll be correcting
        // toward types or protocols.
        lookupKind = protocols[i] ? LookupObjCProtocolName
                                  : LookupOrdinaryName;
        continue;
      }

      // If we want protocols and we have a protocol, there's nothing
      // more to do.
      if (lookupKind == LookupObjCProtocolName && protocols[i])
        continue;

      // If we want types and we have a type declaration, there's
      // nothing more to do.
      if (lookupKind == LookupOrdinaryName && typeDecls[i])
        continue;

      // We have a conflict: some names refer to protocols and others
      // refer to types.
      DiagnoseTypeArgsAndProtocols(identifiers[0], identifierLocs[0],
                                   identifiers[i], identifierLocs[i],
                                   protocols[i] != nullptr);

      protocols.clear();
      typeArgs.clear();
      return;
    }

    // Perform typo correction on the name.
    TypoCorrection corrected = CorrectTypo(
        DeclarationNameInfo(identifiers[i], identifierLocs[i]), lookupKind, S,
        nullptr,
        llvm::make_unique<ObjCTypeArgOrProtocolValidatorCCC>(Context,
                                                             lookupKind),
        CTK_ErrorRecovery);
    if (corrected) {
      // Did we find a protocol?
      if (auto proto = corrected.getCorrectionDeclAs<ObjCProtocolDecl>()) {
        diagnoseTypo(corrected,
                     PDiag(diag::err_undeclared_protocol_suggest)
                       << identifiers[i]);
        lookupKind = LookupObjCProtocolName;
        protocols[i] = proto;
        ++numProtocolsResolved;
        continue;
      }

      // Did we find a type?
      if (auto typeDecl = corrected.getCorrectionDeclAs<TypeDecl>()) {
        diagnoseTypo(corrected,
                     PDiag(diag::err_unknown_typename_suggest)
                       << identifiers[i]);
        lookupKind = LookupOrdinaryName;
        typeDecls[i] = typeDecl;
        ++numTypeDeclsResolved;
        continue;
      }

      // Did we find an Objective-C class?
      if (auto objcClass = corrected.getCorrectionDeclAs<ObjCInterfaceDecl>()) {
        diagnoseTypo(corrected,
                     PDiag(diag::err_unknown_type_or_class_name_suggest)
                       << identifiers[i] << true);
        lookupKind = LookupOrdinaryName;
        typeDecls[i] = objcClass;
        ++numTypeDeclsResolved;
        continue;
      }
    }

    // We couldn't find anything.
    Diag(identifierLocs[i],
         (lookupKind == LookupAnyName ? diag::err_objc_type_arg_missing
          : lookupKind == LookupObjCProtocolName ? diag::err_undeclared_protocol
          : diag::err_unknown_typename))
      << identifiers[i];
    protocols.clear();
    typeArgs.clear();
    return;
  }

  // If all of the names were (corrected to) protocols, these were
  // protocol qualifiers.
  if (numProtocolsResolved == identifiers.size())
    return resolvedAsProtocols();

  // Otherwise, all of the names were (corrected to) types.
  assert(numTypeDeclsResolved == identifiers.size() && "Not all types?");
  return resolvedAsTypeDecls();
}

/// DiagnoseClassExtensionDupMethods - Check for duplicate declaration of
/// a class method in its extension.
///
void Sema::DiagnoseClassExtensionDupMethods(ObjCCategoryDecl *CAT,
                                            ObjCInterfaceDecl *ID) {
  if (!ID)
    return;  // Possibly due to previous error

  llvm::DenseMap<Selector, const ObjCMethodDecl*> MethodMap;
  for (auto *MD : ID->methods())
    MethodMap[MD->getSelector()] = MD;

  if (MethodMap.empty())
    return;
  for (const auto *Method : CAT->methods()) {
    const ObjCMethodDecl *&PrevMethod = MethodMap[Method->getSelector()];
    if (PrevMethod &&
        (PrevMethod->isInstanceMethod() == Method->isInstanceMethod()) &&
        !MatchTwoMethodDeclarations(Method, PrevMethod)) {
      Diag(Method->getLocation(), diag::err_duplicate_method_decl)
            << Method->getDeclName();
      Diag(PrevMethod->getLocation(), diag::note_previous_declaration);
    }
  }
}

/// ActOnForwardProtocolDeclaration - Handle \@protocol foo;
Sema::DeclGroupPtrTy
Sema::ActOnForwardProtocolDeclaration(SourceLocation AtProtocolLoc,
                                      ArrayRef<IdentifierLocPair> IdentList,
                                      const ParsedAttributesView &attrList) {
  SmallVector<Decl *, 8> DeclsInGroup;
  for (const IdentifierLocPair &IdentPair : IdentList) {
    IdentifierInfo *Ident = IdentPair.first;
    ObjCProtocolDecl *PrevDecl = LookupProtocol(Ident, IdentPair.second,
                                                forRedeclarationInCurContext());
    ObjCProtocolDecl *PDecl
      = ObjCProtocolDecl::Create(Context, CurContext, Ident,
                                 IdentPair.second, AtProtocolLoc,
                                 PrevDecl);

    PushOnScopeChains(PDecl, TUScope);
    CheckObjCDeclScope(PDecl);

    ProcessDeclAttributeList(TUScope, PDecl, attrList);
    AddPragmaAttributes(TUScope, PDecl);

    if (PrevDecl)
      mergeDeclAttributes(PDecl, PrevDecl);

    DeclsInGroup.push_back(PDecl);
  }

  return BuildDeclaratorGroup(DeclsInGroup);
}

Decl *Sema::ActOnStartCategoryInterface(
    SourceLocation AtInterfaceLoc, IdentifierInfo *ClassName,
    SourceLocation ClassLoc, ObjCTypeParamList *typeParamList,
    IdentifierInfo *CategoryName, SourceLocation CategoryLoc,
    Decl *const *ProtoRefs, unsigned NumProtoRefs,
    const SourceLocation *ProtoLocs, SourceLocation EndProtoLoc,
    const ParsedAttributesView &AttrList) {
  ObjCCategoryDecl *CDecl;
  ObjCInterfaceDecl *IDecl = getObjCInterfaceDecl(ClassName, ClassLoc, true);

  /// Check that class of this category is already completely declared.

  if (!IDecl
      || RequireCompleteType(ClassLoc, Context.getObjCInterfaceType(IDecl),
                             diag::err_category_forward_interface,
                             CategoryName == nullptr)) {
    // Create an invalid ObjCCategoryDecl to serve as context for
    // the enclosing method declarations.  We mark the decl invalid
    // to make it clear that this isn't a valid AST.
    CDecl = ObjCCategoryDecl::Create(Context, CurContext, AtInterfaceLoc,
                                     ClassLoc, CategoryLoc, CategoryName,
                                     IDecl, typeParamList);
    CDecl->setInvalidDecl();
    CurContext->addDecl(CDecl);

    if (!IDecl)
      Diag(ClassLoc, diag::err_undef_interface) << ClassName;
    return ActOnObjCContainerStartDefinition(CDecl);
  }

  if (!CategoryName && IDecl->getImplementation()) {
    Diag(ClassLoc, diag::err_class_extension_after_impl) << ClassName;
    Diag(IDecl->getImplementation()->getLocation(),
          diag::note_implementation_declared);
  }

  if (CategoryName) {
    /// Check for duplicate interface declaration for this category
    if (ObjCCategoryDecl *Previous
          = IDecl->FindCategoryDeclaration(CategoryName)) {
      // Class extensions can be declared multiple times, categories cannot.
      Diag(CategoryLoc, diag::warn_dup_category_def)
        << ClassName << CategoryName;
      Diag(Previous->getLocation(), diag::note_previous_definition);
    }
  }

  // If we have a type parameter list, check it.
  if (typeParamList) {
    if (auto prevTypeParamList = IDecl->getTypeParamList()) {
      if (checkTypeParamListConsistency(*this, prevTypeParamList, typeParamList,
                                        CategoryName
                                          ? TypeParamListContext::Category
                                          : TypeParamListContext::Extension))
        typeParamList = nullptr;
    } else {
      Diag(typeParamList->getLAngleLoc(),
           diag::err_objc_parameterized_category_nonclass)
        << (CategoryName != nullptr)
        << ClassName
        << typeParamList->getSourceRange();

      typeParamList = nullptr;
    }
  }

  CDecl = ObjCCategoryDecl::Create(Context, CurContext, AtInterfaceLoc,
                                   ClassLoc, CategoryLoc, CategoryName, IDecl,
                                   typeParamList);
  // FIXME: PushOnScopeChains?
  CurContext->addDecl(CDecl);

  // Process the attributes before looking at protocols to ensure that the
  // availability attribute is attached to the category to provide availability
  // checking for protocol uses.
  ProcessDeclAttributeList(TUScope, CDecl, AttrList);
  AddPragmaAttributes(TUScope, CDecl);

  if (NumProtoRefs) {
    diagnoseUseOfProtocols(*this, CDecl, (ObjCProtocolDecl*const*)ProtoRefs,
                           NumProtoRefs, ProtoLocs);
    CDecl->setProtocolList((ObjCProtocolDecl*const*)ProtoRefs, NumProtoRefs,
                           ProtoLocs, Context);
    // Protocols in the class extension belong to the class.
    if (CDecl->IsClassExtension())
     IDecl->mergeClassExtensionProtocolList((ObjCProtocolDecl*const*)ProtoRefs,
                                            NumProtoRefs, Context);
  }

  CheckObjCDeclScope(CDecl);
  return ActOnObjCContainerStartDefinition(CDecl);
}

/// ActOnStartCategoryImplementation - Perform semantic checks on the
/// category implementation declaration and build an ObjCCategoryImplDecl
/// object.
Decl *Sema::ActOnStartCategoryImplementation(
                      SourceLocation AtCatImplLoc,
                      IdentifierInfo *ClassName, SourceLocation ClassLoc,
                      IdentifierInfo *CatName, SourceLocation CatLoc) {
  ObjCInterfaceDecl *IDecl = getObjCInterfaceDecl(ClassName, ClassLoc, true);
  ObjCCategoryDecl *CatIDecl = nullptr;
  if (IDecl && IDecl->hasDefinition()) {
    CatIDecl = IDecl->FindCategoryDeclaration(CatName);
    if (!CatIDecl) {
      // Category @implementation with no corresponding @interface.
      // Create and install one.
      CatIDecl = ObjCCategoryDecl::Create(Context, CurContext, AtCatImplLoc,
                                          ClassLoc, CatLoc,
                                          CatName, IDecl,
                                          /*typeParamList=*/nullptr);
      CatIDecl->setImplicit();
    }
  }

  ObjCCategoryImplDecl *CDecl =
    ObjCCategoryImplDecl::Create(Context, CurContext, CatName, IDecl,
                                 ClassLoc, AtCatImplLoc, CatLoc);
  /// Check that class of this category is already completely declared.
  if (!IDecl) {
    Diag(ClassLoc, diag::err_undef_interface) << ClassName;
    CDecl->setInvalidDecl();
  } else if (RequireCompleteType(ClassLoc, Context.getObjCInterfaceType(IDecl),
                                 diag::err_undef_interface)) {
    CDecl->setInvalidDecl();
  }

  // FIXME: PushOnScopeChains?
  CurContext->addDecl(CDecl);

  // If the interface has the objc_runtime_visible attribute, we
  // cannot implement a category for it.
  if (IDecl && IDecl->hasAttr<ObjCRuntimeVisibleAttr>()) {
    Diag(ClassLoc, diag::err_objc_runtime_visible_category)
      << IDecl->getDeclName();
  }

  /// Check that CatName, category name, is not used in another implementation.
  if (CatIDecl) {
    if (CatIDecl->getImplementation()) {
      Diag(ClassLoc, diag::err_dup_implementation_category) << ClassName
        << CatName;
      Diag(CatIDecl->getImplementation()->getLocation(),
           diag::note_previous_definition);
      CDecl->setInvalidDecl();
    } else {
      CatIDecl->setImplementation(CDecl);
      // Warn on implementating category of deprecated class under
      // -Wdeprecated-implementations flag.
      DiagnoseObjCImplementedDeprecations(*this, CatIDecl,
                                          CDecl->getLocation());
    }
  }

  CheckObjCDeclScope(CDecl);
  return ActOnObjCContainerStartDefinition(CDecl);
}

Decl *Sema::ActOnStartClassImplementation(
                      SourceLocation AtClassImplLoc,
                      IdentifierInfo *ClassName, SourceLocation ClassLoc,
                      IdentifierInfo *SuperClassname,
                      SourceLocation SuperClassLoc) {
  ObjCInterfaceDecl *IDecl = nullptr;
  // Check for another declaration kind with the same name.
  NamedDecl *PrevDecl
    = LookupSingleName(TUScope, ClassName, ClassLoc, LookupOrdinaryName,
                       forRedeclarationInCurContext());
  if (PrevDecl && !isa<ObjCInterfaceDecl>(PrevDecl)) {
    Diag(ClassLoc, diag::err_redefinition_different_kind) << ClassName;
    Diag(PrevDecl->getLocation(), diag::note_previous_definition);
  } else if ((IDecl = dyn_cast_or_null<ObjCInterfaceDecl>(PrevDecl))) {
    // FIXME: This will produce an error if the definition of the interface has
    // been imported from a module but is not visible.
    RequireCompleteType(ClassLoc, Context.getObjCInterfaceType(IDecl),
                        diag::warn_undef_interface);
  } else {
    // We did not find anything with the name ClassName; try to correct for
    // typos in the class name.
    TypoCorrection Corrected = CorrectTypo(
        DeclarationNameInfo(ClassName, ClassLoc), LookupOrdinaryName, TUScope,
        nullptr, llvm::make_unique<ObjCInterfaceValidatorCCC>(), CTK_NonError);
    if (Corrected.getCorrectionDeclAs<ObjCInterfaceDecl>()) {
      // Suggest the (potentially) correct interface name. Don't provide a
      // code-modification hint or use the typo name for recovery, because
      // this is just a warning. The program may actually be correct.
      diagnoseTypo(Corrected,
                   PDiag(diag::warn_undef_interface_suggest) << ClassName,
                   /*ErrorRecovery*/false);
    } else {
      Diag(ClassLoc, diag::warn_undef_interface) << ClassName;
    }
  }

  // Check that super class name is valid class name
  ObjCInterfaceDecl *SDecl = nullptr;
  if (SuperClassname) {
    // Check if a different kind of symbol declared in this scope.
    PrevDecl = LookupSingleName(TUScope, SuperClassname, SuperClassLoc,
                                LookupOrdinaryName);
    if (PrevDecl && !isa<ObjCInterfaceDecl>(PrevDecl)) {
      Diag(SuperClassLoc, diag::err_redefinition_different_kind)
        << SuperClassname;
      Diag(PrevDecl->getLocation(), diag::note_previous_definition);
    } else {
      SDecl = dyn_cast_or_null<ObjCInterfaceDecl>(PrevDecl);
      if (SDecl && !SDecl->hasDefinition())
        SDecl = nullptr;
      if (!SDecl)
        Diag(SuperClassLoc, diag::err_undef_superclass)
          << SuperClassname << ClassName;
      else if (IDecl && !declaresSameEntity(IDecl->getSuperClass(), SDecl)) {
        // This implementation and its interface do not have the same
        // super class.
        Diag(SuperClassLoc, diag::err_conflicting_super_class)
          << SDecl->getDeclName();
        Diag(SDecl->getLocation(), diag::note_previous_definition);
      }
    }
  }

  if (!IDecl) {
    // Legacy case of @implementation with no corresponding @interface.
    // Build, chain & install the interface decl into the identifier.

    // FIXME: Do we support attributes on the @implementation? If so we should
    // copy them over.
    IDecl = ObjCInterfaceDecl::Create(Context, CurContext, AtClassImplLoc,
                                      ClassName, /*typeParamList=*/nullptr,
                                      /*PrevDecl=*/nullptr, ClassLoc,
                                      true);
    AddPragmaAttributes(TUScope, IDecl);
    IDecl->startDefinition();
    if (SDecl) {
      IDecl->setSuperClass(Context.getTrivialTypeSourceInfo(
                             Context.getObjCInterfaceType(SDecl),
                             SuperClassLoc));
      IDecl->setEndOfDefinitionLoc(SuperClassLoc);
    } else {
      IDecl->setEndOfDefinitionLoc(ClassLoc);
    }

    PushOnScopeChains(IDecl, TUScope);
  } else {
    // Mark the interface as being completed, even if it was just as
    //   @class ....;
    // declaration; the user cannot reopen it.
    if (!IDecl->hasDefinition())
      IDecl->startDefinition();
  }

  ObjCImplementationDecl* IMPDecl =
    ObjCImplementationDecl::Create(Context, CurContext, IDecl, SDecl,
                                   ClassLoc, AtClassImplLoc, SuperClassLoc);

  if (CheckObjCDeclScope(IMPDecl))
    return ActOnObjCContainerStartDefinition(IMPDecl);

  // Check that there is no duplicate implementation of this class.
  if (IDecl->getImplementation()) {
    // FIXME: Don't leak everything!
    Diag(ClassLoc, diag::err_dup_implementation_class) << ClassName;
    Diag(IDecl->getImplementation()->getLocation(),
         diag::note_previous_definition);
    IMPDecl->setInvalidDecl();
  } else { // add it to the list.
    IDecl->setImplementation(IMPDecl);
    PushOnScopeChains(IMPDecl, TUScope);
    // Warn on implementating deprecated class under
    // -Wdeprecated-implementations flag.
    DiagnoseObjCImplementedDeprecations(*this, IDecl, IMPDecl->getLocation());
  }

  // If the superclass has the objc_runtime_visible attribute, we
  // cannot implement a subclass of it.
  if (IDecl->getSuperClass() &&
      IDecl->getSuperClass()->hasAttr<ObjCRuntimeVisibleAttr>()) {
    Diag(ClassLoc, diag::err_objc_runtime_visible_subclass)
      << IDecl->getDeclName()
      << IDecl->getSuperClass()->getDeclName();
  }

  return ActOnObjCContainerStartDefinition(IMPDecl);
}

Sema::DeclGroupPtrTy
Sema::ActOnFinishObjCImplementation(Decl *ObjCImpDecl, ArrayRef<Decl *> Decls) {
  SmallVector<Decl *, 64> DeclsInGroup;
  DeclsInGroup.reserve(Decls.size() + 1);

  for (unsigned i = 0, e = Decls.size(); i != e; ++i) {
    Decl *Dcl = Decls[i];
    if (!Dcl)
      continue;
    if (Dcl->getDeclContext()->isFileContext())
      Dcl->setTopLevelDeclInObjCContainer();
    DeclsInGroup.push_back(Dcl);
  }

  DeclsInGroup.push_back(ObjCImpDecl);

  return BuildDeclaratorGroup(DeclsInGroup);
}

void Sema::CheckImplementationIvars(ObjCImplementationDecl *ImpDecl,
                                    ObjCIvarDecl **ivars, unsigned numIvars,
                                    SourceLocation RBrace) {
  assert(ImpDecl && "missing implementation decl");
  ObjCInterfaceDecl* IDecl = ImpDecl->getClassInterface();
  if (!IDecl)
    return;
  /// Check case of non-existing \@interface decl.
  /// (legacy objective-c \@implementation decl without an \@interface decl).
  /// Add implementations's ivar to the synthesize class's ivar list.
  if (IDecl->isImplicitInterfaceDecl()) {
    IDecl->setEndOfDefinitionLoc(RBrace);
    // Add ivar's to class's DeclContext.
    for (unsigned i = 0, e = numIvars; i != e; ++i) {
      ivars[i]->setLexicalDeclContext(ImpDecl);
      IDecl->makeDeclVisibleInContext(ivars[i]);
      ImpDecl->addDecl(ivars[i]);
    }

    return;
  }
  // If implementation has empty ivar list, just return.
  if (numIvars == 0)
    return;

  assert(ivars && "missing @implementation ivars");
  if (LangOpts.ObjCRuntime.isNonFragile()) {
    if (ImpDecl->getSuperClass())
      Diag(ImpDecl->getLocation(), diag::warn_on_superclass_use);
    for (unsigned i = 0; i < numIvars; i++) {
      ObjCIvarDecl* ImplIvar = ivars[i];
      if (const ObjCIvarDecl *ClsIvar =
            IDecl->getIvarDecl(ImplIvar->getIdentifier())) {
        Diag(ImplIvar->getLocation(), diag::err_duplicate_ivar_declaration);
        Diag(ClsIvar->getLocation(), diag::note_previous_definition);
        continue;
      }
      // Check class extensions (unnamed categories) for duplicate ivars.
      for (const auto *CDecl : IDecl->visible_extensions()) {
        if (const ObjCIvarDecl *ClsExtIvar =
            CDecl->getIvarDecl(ImplIvar->getIdentifier())) {
          Diag(ImplIvar->getLocation(), diag::err_duplicate_ivar_declaration);
          Diag(ClsExtIvar->getLocation(), diag::note_previous_definition);
          continue;
        }
      }
      // Instance ivar to Implementation's DeclContext.
      ImplIvar->setLexicalDeclContext(ImpDecl);
      IDecl->makeDeclVisibleInContext(ImplIvar);
      ImpDecl->addDecl(ImplIvar);
    }
    return;
  }
  // Check interface's Ivar list against those in the implementation.
  // names and types must match.
  //
  unsigned j = 0;
  ObjCInterfaceDecl::ivar_iterator
    IVI = IDecl->ivar_begin(), IVE = IDecl->ivar_end();
  for (; numIvars > 0 && IVI != IVE; ++IVI) {
    ObjCIvarDecl* ImplIvar = ivars[j++];
    ObjCIvarDecl* ClsIvar = *IVI;
    assert (ImplIvar && "missing implementation ivar");
    assert (ClsIvar && "missing class ivar");

    // First, make sure the types match.
    if (!Context.hasSameType(ImplIvar->getType(), ClsIvar->getType())) {
      Diag(ImplIvar->getLocation(), diag::err_conflicting_ivar_type)
        << ImplIvar->getIdentifier()
        << ImplIvar->getType() << ClsIvar->getType();
      Diag(ClsIvar->getLocation(), diag::note_previous_definition);
    } else if (ImplIvar->isBitField() && ClsIvar->isBitField() &&
               ImplIvar->getBitWidthValue(Context) !=
               ClsIvar->getBitWidthValue(Context)) {
      Diag(ImplIvar->getBitWidth()->getBeginLoc(),
           diag::err_conflicting_ivar_bitwidth)
          << ImplIvar->getIdentifier();
      Diag(ClsIvar->getBitWidth()->getBeginLoc(),
           diag::note_previous_definition);
    }
    // Make sure the names are identical.
    if (ImplIvar->getIdentifier() != ClsIvar->getIdentifier()) {
      Diag(ImplIvar->getLocation(), diag::err_conflicting_ivar_name)
        << ImplIvar->getIdentifier() << ClsIvar->getIdentifier();
      Diag(ClsIvar->getLocation(), diag::note_previous_definition);
    }
    --numIvars;
  }

  if (numIvars > 0)
    Diag(ivars[j]->getLocation(), diag::err_inconsistent_ivar_count);
  else if (IVI != IVE)
    Diag(IVI->getLocation(), diag::err_inconsistent_ivar_count);
}

static void WarnUndefinedMethod(Sema &S, SourceLocation ImpLoc,
                                ObjCMethodDecl *method,
                                bool &IncompleteImpl,
                                unsigned DiagID,
                                NamedDecl *NeededFor = nullptr) {
  // No point warning no definition of method which is 'unavailable'.
  if (method->getAvailability() == AR_Unavailable)
    return;

  // FIXME: For now ignore 'IncompleteImpl'.
  // Previously we grouped all unimplemented methods under a single
  // warning, but some users strongly voiced that they would prefer
  // separate warnings.  We will give that approach a try, as that
  // matches what we do with protocols.
  {
    const Sema::SemaDiagnosticBuilder &B = S.Diag(ImpLoc, DiagID);
    B << method;
    if (NeededFor)
      B << NeededFor;
  }

  // Issue a note to the original declaration.
  SourceLocation MethodLoc = method->getBeginLoc();
  if (MethodLoc.isValid())
    S.Diag(MethodLoc, diag::note_method_declared_at) << method;
}

/// Determines if type B can be substituted for type A.  Returns true if we can
/// guarantee that anything that the user will do to an object of type A can
/// also be done to an object of type B.  This is trivially true if the two
/// types are the same, or if B is a subclass of A.  It becomes more complex
/// in cases where protocols are involved.
///
/// Object types in Objective-C describe the minimum requirements for an
/// object, rather than providing a complete description of a type.  For
/// example, if A is a subclass of B, then B* may refer to an instance of A.
/// The principle of substitutability means that we may use an instance of A
/// anywhere that we may use an instance of B - it will implement all of the
/// ivars of B and all of the methods of B.
///
/// This substitutability is important when type checking methods, because
/// the implementation may have stricter type definitions than the interface.
/// The interface specifies minimum requirements, but the implementation may
/// have more accurate ones.  For example, a method may privately accept
/// instances of B, but only publish that it accepts instances of A.  Any
/// object passed to it will be type checked against B, and so will implicitly
/// by a valid A*.  Similarly, a method may return a subclass of the class that
/// it is declared as returning.
///
/// This is most important when considering subclassing.  A method in a
/// subclass must accept any object as an argument that its superclass's
/// implementation accepts.  It may, however, accept a more general type
/// without breaking substitutability (i.e. you can still use the subclass
/// anywhere that you can use the superclass, but not vice versa).  The
/// converse requirement applies to return types: the return type for a
/// subclass method must be a valid object of the kind that the superclass
/// advertises, but it may be specified more accurately.  This avoids the need
/// for explicit down-casting by callers.
///
/// Note: This is a stricter requirement than for assignment.
static bool isObjCTypeSubstitutable(ASTContext &Context,
                                    const ObjCObjectPointerType *A,
                                    const ObjCObjectPointerType *B,
                                    bool rejectId) {
  // Reject a protocol-unqualified id.
  if (rejectId && B->isObjCIdType()) return false;

  // If B is a qualified id, then A must also be a qualified id and it must
  // implement all of the protocols in B.  It may not be a qualified class.
  // For example, MyClass<A> can be assigned to id<A>, but MyClass<A> is a
  // stricter definition so it is not substitutable for id<A>.
  if (B->isObjCQualifiedIdType()) {
    return A->isObjCQualifiedIdType() &&
           Context.ObjCQualifiedIdTypesAreCompatible(QualType(A, 0),
                                                     QualType(B,0),
                                                     false);
  }

  /*
  // id is a special type that bypasses type checking completely.  We want a
  // warning when it is used in one place but not another.
  if (C.isObjCIdType(A) || C.isObjCIdType(B)) return false;


  // If B is a qualified id, then A must also be a qualified id (which it isn't
  // if we've got this far)
  if (B->isObjCQualifiedIdType()) return false;
  */

  // Now we know that A and B are (potentially-qualified) class types.  The
  // normal rules for assignment apply.
  return Context.canAssignObjCInterfaces(A, B);
}

static SourceRange getTypeRange(TypeSourceInfo *TSI) {
  return (TSI ? TSI->getTypeLoc().getSourceRange() : SourceRange());
}

/// Determine whether two set of Objective-C declaration qualifiers conflict.
static bool objcModifiersConflict(Decl::ObjCDeclQualifier x,
                                  Decl::ObjCDeclQualifier y) {
  return (x & ~Decl::OBJC_TQ_CSNullability) !=
         (y & ~Decl::OBJC_TQ_CSNullability);
}

static bool CheckMethodOverrideReturn(Sema &S,
                                      ObjCMethodDecl *MethodImpl,
                                      ObjCMethodDecl *MethodDecl,
                                      bool IsProtocolMethodDecl,
                                      bool IsOverridingMode,
                                      bool Warn) {
  if (IsProtocolMethodDecl &&
      objcModifiersConflict(MethodDecl->getObjCDeclQualifier(),
                            MethodImpl->getObjCDeclQualifier())) {
    if (Warn) {
      S.Diag(MethodImpl->getLocation(),
             (IsOverridingMode
                  ? diag::warn_conflicting_overriding_ret_type_modifiers
                  : diag::warn_conflicting_ret_type_modifiers))
          << MethodImpl->getDeclName()
          << MethodImpl->getReturnTypeSourceRange();
      S.Diag(MethodDecl->getLocation(), diag::note_previous_declaration)
          << MethodDecl->getReturnTypeSourceRange();
    }
    else
      return false;
  }
  if (Warn && IsOverridingMode &&
      !isa<ObjCImplementationDecl>(MethodImpl->getDeclContext()) &&
      !S.Context.hasSameNullabilityTypeQualifier(MethodImpl->getReturnType(),
                                                 MethodDecl->getReturnType(),
                                                 false)) {
    auto nullabilityMethodImpl =
      *MethodImpl->getReturnType()->getNullability(S.Context);
    auto nullabilityMethodDecl =
      *MethodDecl->getReturnType()->getNullability(S.Context);
      S.Diag(MethodImpl->getLocation(),
             diag::warn_conflicting_nullability_attr_overriding_ret_types)
        << DiagNullabilityKind(
             nullabilityMethodImpl,
             ((MethodImpl->getObjCDeclQualifier() & Decl::OBJC_TQ_CSNullability)
              != 0))
        << DiagNullabilityKind(
             nullabilityMethodDecl,
             ((MethodDecl->getObjCDeclQualifier() & Decl::OBJC_TQ_CSNullability)
                != 0));
      S.Diag(MethodDecl->getLocation(), diag::note_previous_declaration);
  }

  if (S.Context.hasSameUnqualifiedType(MethodImpl->getReturnType(),
                                       MethodDecl->getReturnType()))
    return true;
  if (!Warn)
    return false;

  unsigned DiagID =
    IsOverridingMode ? diag::warn_conflicting_overriding_ret_types
                     : diag::warn_conflicting_ret_types;

  // Mismatches between ObjC pointers go into a different warning
  // category, and sometimes they're even completely whitelisted.
  if (const ObjCObjectPointerType *ImplPtrTy =
          MethodImpl->getReturnType()->getAs<ObjCObjectPointerType>()) {
    if (const ObjCObjectPointerType *IfacePtrTy =
            MethodDecl->getReturnType()->getAs<ObjCObjectPointerType>()) {
      // Allow non-matching return types as long as they don't violate
      // the principle of substitutability.  Specifically, we permit
      // return types that are subclasses of the declared return type,
      // or that are more-qualified versions of the declared type.
      if (isObjCTypeSubstitutable(S.Context, IfacePtrTy, ImplPtrTy, false))
        return false;

      DiagID =
        IsOverridingMode ? diag::warn_non_covariant_overriding_ret_types
                         : diag::warn_non_covariant_ret_types;
    }
  }

  S.Diag(MethodImpl->getLocation(), DiagID)
      << MethodImpl->getDeclName() << MethodDecl->getReturnType()
      << MethodImpl->getReturnType()
      << MethodImpl->getReturnTypeSourceRange();
  S.Diag(MethodDecl->getLocation(), IsOverridingMode
                                        ? diag::note_previous_declaration
                                        : diag::note_previous_definition)
      << MethodDecl->getReturnTypeSourceRange();
  return false;
}

static bool CheckMethodOverrideParam(Sema &S,
                                     ObjCMethodDecl *MethodImpl,
                                     ObjCMethodDecl *MethodDecl,
                                     ParmVarDecl *ImplVar,
                                     ParmVarDecl *IfaceVar,
                                     bool IsProtocolMethodDecl,
                                     bool IsOverridingMode,
                                     bool Warn) {
  if (IsProtocolMethodDecl &&
      objcModifiersConflict(ImplVar->getObjCDeclQualifier(),
                            IfaceVar->getObjCDeclQualifier())) {
    if (Warn) {
      if (IsOverridingMode)
        S.Diag(ImplVar->getLocation(),
               diag::warn_conflicting_overriding_param_modifiers)
            << getTypeRange(ImplVar->getTypeSourceInfo())
            << MethodImpl->getDeclName();
      else S.Diag(ImplVar->getLocation(),
             diag::warn_conflicting_param_modifiers)
          << getTypeRange(ImplVar->getTypeSourceInfo())
          << MethodImpl->getDeclName();
      S.Diag(IfaceVar->getLocation(), diag::note_previous_declaration)
          << getTypeRange(IfaceVar->getTypeSourceInfo());
    }
    else
      return false;
  }

  QualType ImplTy = ImplVar->getType();
  QualType IfaceTy = IfaceVar->getType();
  if (Warn && IsOverridingMode &&
      !isa<ObjCImplementationDecl>(MethodImpl->getDeclContext()) &&
      !S.Context.hasSameNullabilityTypeQualifier(ImplTy, IfaceTy, true)) {
    S.Diag(ImplVar->getLocation(),
           diag::warn_conflicting_nullability_attr_overriding_param_types)
      << DiagNullabilityKind(
           *ImplTy->getNullability(S.Context),
           ((ImplVar->getObjCDeclQualifier() & Decl::OBJC_TQ_CSNullability)
            != 0))
      << DiagNullabilityKind(
           *IfaceTy->getNullability(S.Context),
           ((IfaceVar->getObjCDeclQualifier() & Decl::OBJC_TQ_CSNullability)
            != 0));
    S.Diag(IfaceVar->getLocation(), diag::note_previous_declaration);
  }
  if (S.Context.hasSameUnqualifiedType(ImplTy, IfaceTy))
    return true;

  if (!Warn)
    return false;
  unsigned DiagID =
    IsOverridingMode ? diag::warn_conflicting_overriding_param_types
                     : diag::warn_conflicting_param_types;

  // Mismatches between ObjC pointers go into a different warning
  // category, and sometimes they're even completely whitelisted.
  if (const ObjCObjectPointerType *ImplPtrTy =
        ImplTy->getAs<ObjCObjectPointerType>()) {
    if (const ObjCObjectPointerType *IfacePtrTy =
          IfaceTy->getAs<ObjCObjectPointerType>()) {
      // Allow non-matching argument types as long as they don't
      // violate the principle of substitutability.  Specifically, the
      // implementation must accept any objects that the superclass
      // accepts, however it may also accept others.
      if (isObjCTypeSubstitutable(S.Context, ImplPtrTy, IfacePtrTy, true))
        return false;

      DiagID =
      IsOverridingMode ? diag::warn_non_contravariant_overriding_param_types
                       : diag::warn_non_contravariant_param_types;
    }
  }

  S.Diag(ImplVar->getLocation(), DiagID)
    << getTypeRange(ImplVar->getTypeSourceInfo())
    << MethodImpl->getDeclName() << IfaceTy << ImplTy;
  S.Diag(IfaceVar->getLocation(),
         (IsOverridingMode ? diag::note_previous_declaration
                           : diag::note_previous_definition))
    << getTypeRange(IfaceVar->getTypeSourceInfo());
  return false;
}

/// In ARC, check whether the conventional meanings of the two methods
/// match.  If they don't, it's a hard error.
static bool checkMethodFamilyMismatch(Sema &S, ObjCMethodDecl *impl,
                                      ObjCMethodDecl *decl) {
  ObjCMethodFamily implFamily = impl->getMethodFamily();
  ObjCMethodFamily declFamily = decl->getMethodFamily();
  if (implFamily == declFamily) return false;

  // Since conventions are sorted by selector, the only possibility is
  // that the types differ enough to cause one selector or the other
  // to fall out of the family.
  assert(implFamily == OMF_None || declFamily == OMF_None);

  // No further diagnostics required on invalid declarations.
  if (impl->isInvalidDecl() || decl->isInvalidDecl()) return true;

  const ObjCMethodDecl *unmatched = impl;
  ObjCMethodFamily family = declFamily;
  unsigned errorID = diag::err_arc_lost_method_convention;
  unsigned noteID = diag::note_arc_lost_method_convention;
  if (declFamily == OMF_None) {
    unmatched = decl;
    family = implFamily;
    errorID = diag::err_arc_gained_method_convention;
    noteID = diag::note_arc_gained_method_convention;
  }

  // Indexes into a %select clause in the diagnostic.
  enum FamilySelector {
    F_alloc, F_copy, F_mutableCopy = F_copy, F_init, F_new
  };
  FamilySelector familySelector = FamilySelector();

  switch (family) {
  case OMF_None: llvm_unreachable("logic error, no method convention");
  case OMF_retain:
  case OMF_release:
  case OMF_autorelease:
  case OMF_dealloc:
  case OMF_finalize:
  case OMF_retainCount:
  case OMF_self:
  case OMF_initialize:
  case OMF_performSelector:
    // Mismatches for these methods don't change ownership
    // conventions, so we don't care.
    return false;

  case OMF_init: familySelector = F_init; break;
  case OMF_alloc: familySelector = F_alloc; break;
  case OMF_copy: familySelector = F_copy; break;
  case OMF_mutableCopy: familySelector = F_mutableCopy; break;
  case OMF_new: familySelector = F_new; break;
  }

  enum ReasonSelector { R_NonObjectReturn, R_UnrelatedReturn };
  ReasonSelector reasonSelector;

  // The only reason these methods don't fall within their families is
  // due to unusual result types.
  if (unmatched->getReturnType()->isObjCObjectPointerType()) {
    reasonSelector = R_UnrelatedReturn;
  } else {
    reasonSelector = R_NonObjectReturn;
  }

  S.Diag(impl->getLocation(), errorID) << int(familySelector) << int(reasonSelector);
  S.Diag(decl->getLocation(), noteID) << int(familySelector) << int(reasonSelector);

  return true;
}

void Sema::WarnConflictingTypedMethods(ObjCMethodDecl *ImpMethodDecl,
                                       ObjCMethodDecl *MethodDecl,
                                       bool IsProtocolMethodDecl) {
  if (getLangOpts().ObjCAutoRefCount &&
      checkMethodFamilyMismatch(*this, ImpMethodDecl, MethodDecl))
    return;

  CheckMethodOverrideReturn(*this, ImpMethodDecl, MethodDecl,
                            IsProtocolMethodDecl, false,
                            true);

  for (ObjCMethodDecl::param_iterator IM = ImpMethodDecl->param_begin(),
       IF = MethodDecl->param_begin(), EM = ImpMethodDecl->param_end(),
       EF = MethodDecl->param_end();
       IM != EM && IF != EF; ++IM, ++IF) {
    CheckMethodOverrideParam(*this, ImpMethodDecl, MethodDecl, *IM, *IF,
                             IsProtocolMethodDecl, false, true);
  }

  if (ImpMethodDecl->isVariadic() != MethodDecl->isVariadic()) {
    Diag(ImpMethodDecl->getLocation(),
         diag::warn_conflicting_variadic);
    Diag(MethodDecl->getLocation(), diag::note_previous_declaration);
  }
}

void Sema::CheckConflictingOverridingMethod(ObjCMethodDecl *Method,
                                       ObjCMethodDecl *Overridden,
                                       bool IsProtocolMethodDecl) {

  CheckMethodOverrideReturn(*this, Method, Overridden,
                            IsProtocolMethodDecl, true,
                            true);

  for (ObjCMethodDecl::param_iterator IM = Method->param_begin(),
       IF = Overridden->param_begin(), EM = Method->param_end(),
       EF = Overridden->param_end();
       IM != EM && IF != EF; ++IM, ++IF) {
    CheckMethodOverrideParam(*this, Method, Overridden, *IM, *IF,
                             IsProtocolMethodDecl, true, true);
  }

  if (Method->isVariadic() != Overridden->isVariadic()) {
    Diag(Method->getLocation(),
         diag::warn_conflicting_overriding_variadic);
    Diag(Overridden->getLocation(), diag::note_previous_declaration);
  }
}

/// WarnExactTypedMethods - This routine issues a warning if method
/// implementation declaration matches exactly that of its declaration.
void Sema::WarnExactTypedMethods(ObjCMethodDecl *ImpMethodDecl,
                                 ObjCMethodDecl *MethodDecl,
                                 bool IsProtocolMethodDecl) {
  // don't issue warning when protocol method is optional because primary
  // class is not required to implement it and it is safe for protocol
  // to implement it.
  if (MethodDecl->getImplementationControl() == ObjCMethodDecl::Optional)
    return;
  // don't issue warning when primary class's method is
  // depecated/unavailable.
  if (MethodDecl->hasAttr<UnavailableAttr>() ||
      MethodDecl->hasAttr<DeprecatedAttr>())
    return;

  bool match = CheckMethodOverrideReturn(*this, ImpMethodDecl, MethodDecl,
                                      IsProtocolMethodDecl, false, false);
  if (match)
    for (ObjCMethodDecl::param_iterator IM = ImpMethodDecl->param_begin(),
         IF = MethodDecl->param_begin(), EM = ImpMethodDecl->param_end(),
         EF = MethodDecl->param_end();
         IM != EM && IF != EF; ++IM, ++IF) {
      match = CheckMethodOverrideParam(*this, ImpMethodDecl, MethodDecl,
                                       *IM, *IF,
                                       IsProtocolMethodDecl, false, false);
      if (!match)
        break;
    }
  if (match)
    match = (ImpMethodDecl->isVariadic() == MethodDecl->isVariadic());
  if (match)
    match = !(MethodDecl->isClassMethod() &&
              MethodDecl->getSelector() == GetNullarySelector("load", Context));

  if (match) {
    Diag(ImpMethodDecl->getLocation(),
         diag::warn_category_method_impl_match);
    Diag(MethodDecl->getLocation(), diag::note_method_declared_at)
      << MethodDecl->getDeclName();
  }
}

/// FIXME: Type hierarchies in Objective-C can be deep. We could most likely
/// improve the efficiency of selector lookups and type checking by associating
/// with each protocol / interface / category the flattened instance tables. If
/// we used an immutable set to keep the table then it wouldn't add significant
/// memory cost and it would be handy for lookups.

typedef llvm::DenseSet<IdentifierInfo*> ProtocolNameSet;
typedef std::unique_ptr<ProtocolNameSet> LazyProtocolNameSet;

static void findProtocolsWithExplicitImpls(const ObjCProtocolDecl *PDecl,
                                           ProtocolNameSet &PNS) {
  if (PDecl->hasAttr<ObjCExplicitProtocolImplAttr>())
    PNS.insert(PDecl->getIdentifier());
  for (const auto *PI : PDecl->protocols())
    findProtocolsWithExplicitImpls(PI, PNS);
}

/// Recursively populates a set with all conformed protocols in a class
/// hierarchy that have the 'objc_protocol_requires_explicit_implementation'
/// attribute.
static void findProtocolsWithExplicitImpls(const ObjCInterfaceDecl *Super,
                                           ProtocolNameSet &PNS) {
  if (!Super)
    return;

  for (const auto *I : Super->all_referenced_protocols())
    findProtocolsWithExplicitImpls(I, PNS);

  findProtocolsWithExplicitImpls(Super->getSuperClass(), PNS);
}

/// CheckProtocolMethodDefs - This routine checks unimplemented methods
/// Declared in protocol, and those referenced by it.
static void CheckProtocolMethodDefs(Sema &S,
                                    SourceLocation ImpLoc,
                                    ObjCProtocolDecl *PDecl,
                                    bool& IncompleteImpl,
                                    const Sema::SelectorSet &InsMap,
                                    const Sema::SelectorSet &ClsMap,
                                    ObjCContainerDecl *CDecl,
                                    LazyProtocolNameSet &ProtocolsExplictImpl) {
  ObjCCategoryDecl *C = dyn_cast<ObjCCategoryDecl>(CDecl);
  ObjCInterfaceDecl *IDecl = C ? C->getClassInterface()
                               : dyn_cast<ObjCInterfaceDecl>(CDecl);
  assert (IDecl && "CheckProtocolMethodDefs - IDecl is null");

  ObjCInterfaceDecl *Super = IDecl->getSuperClass();
  ObjCInterfaceDecl *NSIDecl = nullptr;

  // If this protocol is marked 'objc_protocol_requires_explicit_implementation'
  // then we should check if any class in the super class hierarchy also
  // conforms to this protocol, either directly or via protocol inheritance.
  // If so, we can skip checking this protocol completely because we
  // know that a parent class already satisfies this protocol.
  //
  // Note: we could generalize this logic for all protocols, and merely
  // add the limit on looking at the super class chain for just
  // specially marked protocols.  This may be a good optimization.  This
  // change is restricted to 'objc_protocol_requires_explicit_implementation'
  // protocols for now for controlled evaluation.
  if (PDecl->hasAttr<ObjCExplicitProtocolImplAttr>()) {
    if (!ProtocolsExplictImpl) {
      ProtocolsExplictImpl.reset(new ProtocolNameSet);
      findProtocolsWithExplicitImpls(Super, *ProtocolsExplictImpl);
    }
    if (ProtocolsExplictImpl->find(PDecl->getIdentifier()) !=
        ProtocolsExplictImpl->end())
      return;

    // If no super class conforms to the protocol, we should not search
    // for methods in the super class to implicitly satisfy the protocol.
    Super = nullptr;
  }

  if (S.getLangOpts().ObjCRuntime.isNeXTFamily()) {
    // check to see if class implements forwardInvocation method and objects
    // of this class are derived from 'NSProxy' so that to forward requests
    // from one object to another.
    // Under such conditions, which means that every method possible is
    // implemented in the class, we should not issue "Method definition not
    // found" warnings.
    // FIXME: Use a general GetUnarySelector method for this.
    IdentifierInfo* II = &S.Context.Idents.get("forwardInvocation");
    Selector fISelector = S.Context.Selectors.getSelector(1, &II);
    if (InsMap.count(fISelector))
      // Is IDecl derived from 'NSProxy'? If so, no instance methods
      // need be implemented in the implementation.
      NSIDecl = IDecl->lookupInheritedClass(&S.Context.Idents.get("NSProxy"));
  }

  // If this is a forward protocol declaration, get its definition.
  if (!PDecl->isThisDeclarationADefinition() &&
      PDecl->getDefinition())
    PDecl = PDecl->getDefinition();

  // If a method lookup fails locally we still need to look and see if
  // the method was implemented by a base class or an inherited
  // protocol. This lookup is slow, but occurs rarely in correct code
  // and otherwise would terminate in a warning.

  // check unimplemented instance methods.
  if (!NSIDecl)
    for (auto *method : PDecl->instance_methods()) {
      if (method->getImplementationControl() != ObjCMethodDecl::Optional &&
          !method->isPropertyAccessor() &&
          !InsMap.count(method->getSelector()) &&
          (!Super || !Super->lookupMethod(method->getSelector(),
                                          true /* instance */,
                                          false /* shallowCategory */,
                                          true /* followsSuper */,
                                          nullptr /* category */))) {
            // If a method is not implemented in the category implementation but
            // has been declared in its primary class, superclass,
            // or in one of their protocols, no need to issue the warning.
            // This is because method will be implemented in the primary class
            // or one of its super class implementation.

            // Ugly, but necessary. Method declared in protocol might have
            // have been synthesized due to a property declared in the class which
            // uses the protocol.
            if (ObjCMethodDecl *MethodInClass =
                  IDecl->lookupMethod(method->getSelector(),
                                      true /* instance */,
                                      true /* shallowCategoryLookup */,
                                      false /* followSuper */))
              if (C || MethodInClass->isPropertyAccessor())
                continue;
            unsigned DIAG = diag::warn_unimplemented_protocol_method;
            if (!S.Diags.isIgnored(DIAG, ImpLoc)) {
              WarnUndefinedMethod(S, ImpLoc, method, IncompleteImpl, DIAG,
                                  PDecl);
            }
          }
    }
  // check unimplemented class methods
  for (auto *method : PDecl->class_methods()) {
    if (method->getImplementationControl() != ObjCMethodDecl::Optional &&
        !ClsMap.count(method->getSelector()) &&
        (!Super || !Super->lookupMethod(method->getSelector(),
                                        false /* class method */,
                                        false /* shallowCategoryLookup */,
                                        true  /* followSuper */,
                                        nullptr /* category */))) {
      // See above comment for instance method lookups.
      if (C && IDecl->lookupMethod(method->getSelector(),
                                   false /* class */,
                                   true /* shallowCategoryLookup */,
                                   false /* followSuper */))
        continue;

      unsigned DIAG = diag::warn_unimplemented_protocol_method;
      if (!S.Diags.isIgnored(DIAG, ImpLoc)) {
        WarnUndefinedMethod(S, ImpLoc, method, IncompleteImpl, DIAG, PDecl);
      }
    }
  }
  // Check on this protocols's referenced protocols, recursively.
  for (auto *PI : PDecl->protocols())
    CheckProtocolMethodDefs(S, ImpLoc, PI, IncompleteImpl, InsMap, ClsMap,
                            CDecl, ProtocolsExplictImpl);
}

/// MatchAllMethodDeclarations - Check methods declared in interface
/// or protocol against those declared in their implementations.
///
void Sema::MatchAllMethodDeclarations(const SelectorSet &InsMap,
                                      const SelectorSet &ClsMap,
                                      SelectorSet &InsMapSeen,
                                      SelectorSet &ClsMapSeen,
                                      ObjCImplDecl* IMPDecl,
                                      ObjCContainerDecl* CDecl,
                                      bool &IncompleteImpl,
                                      bool ImmediateClass,
                                      bool WarnCategoryMethodImpl) {
  // Check and see if instance methods in class interface have been
  // implemented in the implementation class. If so, their types match.
  for (auto *I : CDecl->instance_methods()) {
    if (!InsMapSeen.insert(I->getSelector()).second)
      continue;
    if (!I->isPropertyAccessor() &&
        !InsMap.count(I->getSelector())) {
      if (ImmediateClass)
        WarnUndefinedMethod(*this, IMPDecl->getLocation(), I, IncompleteImpl,
                            diag::warn_undef_method_impl);
      continue;
    } else {
      ObjCMethodDecl *ImpMethodDecl =
        IMPDecl->getInstanceMethod(I->getSelector());
      assert(CDecl->getInstanceMethod(I->getSelector(), true/*AllowHidden*/) &&
             "Expected to find the method through lookup as well");
      // ImpMethodDecl may be null as in a @dynamic property.
      if (ImpMethodDecl) {
        if (!WarnCategoryMethodImpl)
          WarnConflictingTypedMethods(ImpMethodDecl, I,
                                      isa<ObjCProtocolDecl>(CDecl));
        else if (!I->isPropertyAccessor())
          WarnExactTypedMethods(ImpMethodDecl, I, isa<ObjCProtocolDecl>(CDecl));
      }
    }
  }

  // Check and see if class methods in class interface have been
  // implemented in the implementation class. If so, their types match.
  for (auto *I : CDecl->class_methods()) {
    if (!ClsMapSeen.insert(I->getSelector()).second)
      continue;
    if (!I->isPropertyAccessor() &&
        !ClsMap.count(I->getSelector())) {
      if (ImmediateClass)
        WarnUndefinedMethod(*this, IMPDecl->getLocation(), I, IncompleteImpl,
                            diag::warn_undef_method_impl);
    } else {
      ObjCMethodDecl *ImpMethodDecl =
        IMPDecl->getClassMethod(I->getSelector());
      assert(CDecl->getClassMethod(I->getSelector(), true/*AllowHidden*/) &&
             "Expected to find the method through lookup as well");
      // ImpMethodDecl may be null as in a @dynamic property.
      if (ImpMethodDecl) {
        if (!WarnCategoryMethodImpl)
          WarnConflictingTypedMethods(ImpMethodDecl, I,
                                      isa<ObjCProtocolDecl>(CDecl));
        else if (!I->isPropertyAccessor())
          WarnExactTypedMethods(ImpMethodDecl, I, isa<ObjCProtocolDecl>(CDecl));
      }
    }
  }

  if (ObjCProtocolDecl *PD = dyn_cast<ObjCProtocolDecl> (CDecl)) {
    // Also, check for methods declared in protocols inherited by
    // this protocol.
    for (auto *PI : PD->protocols())
      MatchAllMethodDeclarations(InsMap, ClsMap, InsMapSeen, ClsMapSeen,
                                 IMPDecl, PI, IncompleteImpl, false,
                                 WarnCategoryMethodImpl);
  }

  if (ObjCInterfaceDecl *I = dyn_cast<ObjCInterfaceDecl> (CDecl)) {
    // when checking that methods in implementation match their declaration,
    // i.e. when WarnCategoryMethodImpl is false, check declarations in class
    // extension; as well as those in categories.
    if (!WarnCategoryMethodImpl) {
      for (auto *Cat : I->visible_categories())
        MatchAllMethodDeclarations(InsMap, ClsMap, InsMapSeen, ClsMapSeen,
                                   IMPDecl, Cat, IncompleteImpl,
                                   ImmediateClass && Cat->IsClassExtension(),
                                   WarnCategoryMethodImpl);
    } else {
      // Also methods in class extensions need be looked at next.
      for (auto *Ext : I->visible_extensions())
        MatchAllMethodDeclarations(InsMap, ClsMap, InsMapSeen, ClsMapSeen,
                                   IMPDecl, Ext, IncompleteImpl, false,
                                   WarnCategoryMethodImpl);
    }

    // Check for any implementation of a methods declared in protocol.
    for (auto *PI : I->all_referenced_protocols())
      MatchAllMethodDeclarations(InsMap, ClsMap, InsMapSeen, ClsMapSeen,
                                 IMPDecl, PI, IncompleteImpl, false,
                                 WarnCategoryMethodImpl);

    // FIXME. For now, we are not checking for exact match of methods
    // in category implementation and its primary class's super class.
    if (!WarnCategoryMethodImpl && I->getSuperClass())
      MatchAllMethodDeclarations(InsMap, ClsMap, InsMapSeen, ClsMapSeen,
                                 IMPDecl,
                                 I->getSuperClass(), IncompleteImpl, false);
  }
}

/// CheckCategoryVsClassMethodMatches - Checks that methods implemented in
/// category matches with those implemented in its primary class and
/// warns each time an exact match is found.
void Sema::CheckCategoryVsClassMethodMatches(
                                  ObjCCategoryImplDecl *CatIMPDecl) {
  // Get category's primary class.
  ObjCCategoryDecl *CatDecl = CatIMPDecl->getCategoryDecl();
  if (!CatDecl)
    return;
  ObjCInterfaceDecl *IDecl = CatDecl->getClassInterface();
  if (!IDecl)
    return;
  ObjCInterfaceDecl *SuperIDecl = IDecl->getSuperClass();
  SelectorSet InsMap, ClsMap;

  for (const auto *I : CatIMPDecl->instance_methods()) {
    Selector Sel = I->getSelector();
    // When checking for methods implemented in the category, skip over
    // those declared in category class's super class. This is because
    // the super class must implement the method.
    if (SuperIDecl && SuperIDecl->lookupMethod(Sel, true))
      continue;
    InsMap.insert(Sel);
  }

  for (const auto *I : CatIMPDecl->class_methods()) {
    Selector Sel = I->getSelector();
    if (SuperIDecl && SuperIDecl->lookupMethod(Sel, false))
      continue;
    ClsMap.insert(Sel);
  }
  if (InsMap.empty() && ClsMap.empty())
    return;

  SelectorSet InsMapSeen, ClsMapSeen;
  bool IncompleteImpl = false;
  MatchAllMethodDeclarations(InsMap, ClsMap, InsMapSeen, ClsMapSeen,
                             CatIMPDecl, IDecl,
                             IncompleteImpl, false,
                             true /*WarnCategoryMethodImpl*/);
}

void Sema::ImplMethodsVsClassMethods(Scope *S, ObjCImplDecl* IMPDecl,
                                     ObjCContainerDecl* CDecl,
                                     bool IncompleteImpl) {
  SelectorSet InsMap;
  // Check and see if instance methods in class interface have been
  // implemented in the implementation class.
  for (const auto *I : IMPDecl->instance_methods())
    InsMap.insert(I->getSelector());

  // Add the selectors for getters/setters of @dynamic properties.
  for (const auto *PImpl : IMPDecl->property_impls()) {
    // We only care about @dynamic implementations.
    if (PImpl->getPropertyImplementation() != ObjCPropertyImplDecl::Dynamic)
      continue;

    const auto *P = PImpl->getPropertyDecl();
    if (!P) continue;

    InsMap.insert(P->getGetterName());
    if (!P->getSetterName().isNull())
      InsMap.insert(P->getSetterName());
  }

  // Check and see if properties declared in the interface have either 1)
  // an implementation or 2) there is a @synthesize/@dynamic implementation
  // of the property in the @implementation.
  if (const ObjCInterfaceDecl *IDecl = dyn_cast<ObjCInterfaceDecl>(CDecl)) {
    bool SynthesizeProperties = LangOpts.ObjCDefaultSynthProperties &&
                                LangOpts.ObjCRuntime.isNonFragile() &&
                                !IDecl->isObjCRequiresPropertyDefs();
    DiagnoseUnimplementedProperties(S, IMPDecl, CDecl, SynthesizeProperties);
  }

  // Diagnose null-resettable synthesized setters.
  diagnoseNullResettableSynthesizedSetters(IMPDecl);

  SelectorSet ClsMap;
  for (const auto *I : IMPDecl->class_methods())
    ClsMap.insert(I->getSelector());

  // Check for type conflict of methods declared in a class/protocol and
  // its implementation; if any.
  SelectorSet InsMapSeen, ClsMapSeen;
  MatchAllMethodDeclarations(InsMap, ClsMap, InsMapSeen, ClsMapSeen,
                             IMPDecl, CDecl,
                             IncompleteImpl, true);

  // check all methods implemented in category against those declared
  // in its primary class.
  if (ObjCCategoryImplDecl *CatDecl =
        dyn_cast<ObjCCategoryImplDecl>(IMPDecl))
    CheckCategoryVsClassMethodMatches(CatDecl);

  // Check the protocol list for unimplemented methods in the @implementation
  // class.
  // Check and see if class methods in class interface have been
  // implemented in the implementation class.

  LazyProtocolNameSet ExplicitImplProtocols;

  if (ObjCInterfaceDecl *I = dyn_cast<ObjCInterfaceDecl> (CDecl)) {
    for (auto *PI : I->all_referenced_protocols())
      CheckProtocolMethodDefs(*this, IMPDecl->getLocation(), PI, IncompleteImpl,
                              InsMap, ClsMap, I, ExplicitImplProtocols);
  } else if (ObjCCategoryDecl *C = dyn_cast<ObjCCategoryDecl>(CDecl)) {
    // For extended class, unimplemented methods in its protocols will
    // be reported in the primary class.
    if (!C->IsClassExtension()) {
      for (auto *P : C->protocols())
        CheckProtocolMethodDefs(*this, IMPDecl->getLocation(), P,
                                IncompleteImpl, InsMap, ClsMap, CDecl,
                                ExplicitImplProtocols);
      DiagnoseUnimplementedProperties(S, IMPDecl, CDecl,
                                      /*SynthesizeProperties=*/false);
    }
  } else
    llvm_unreachable("invalid ObjCContainerDecl type.");
}

Sema::DeclGroupPtrTy
Sema::ActOnForwardClassDeclaration(SourceLocation AtClassLoc,
                                   IdentifierInfo **IdentList,
                                   SourceLocation *IdentLocs,
                                   ArrayRef<ObjCTypeParamList *> TypeParamLists,
                                   unsigned NumElts) {
  SmallVector<Decl *, 8> DeclsInGroup;
  for (unsigned i = 0; i != NumElts; ++i) {
    // Check for another declaration kind with the same name.
    NamedDecl *PrevDecl
      = LookupSingleName(TUScope, IdentList[i], IdentLocs[i],
                         LookupOrdinaryName, forRedeclarationInCurContext());
    if (PrevDecl && !isa<ObjCInterfaceDecl>(PrevDecl)) {
      // GCC apparently allows the following idiom:
      //
      // typedef NSObject < XCElementTogglerP > XCElementToggler;
      // @class XCElementToggler;
      //
      // Here we have chosen to ignore the forward class declaration
      // with a warning. Since this is the implied behavior.
      TypedefNameDecl *TDD = dyn_cast<TypedefNameDecl>(PrevDecl);
      if (!TDD || !TDD->getUnderlyingType()->isObjCObjectType()) {
        Diag(AtClassLoc, diag::err_redefinition_different_kind) << IdentList[i];
        Diag(PrevDecl->getLocation(), diag::note_previous_definition);
      } else {
        // a forward class declaration matching a typedef name of a class refers
        // to the underlying class. Just ignore the forward class with a warning
        // as this will force the intended behavior which is to lookup the
        // typedef name.
        if (isa<ObjCObjectType>(TDD->getUnderlyingType())) {
          Diag(AtClassLoc, diag::warn_forward_class_redefinition)
              << IdentList[i];
          Diag(PrevDecl->getLocation(), diag::note_previous_definition);
          continue;
        }
      }
    }

    // Create a declaration to describe this forward declaration.
    ObjCInterfaceDecl *PrevIDecl
      = dyn_cast_or_null<ObjCInterfaceDecl>(PrevDecl);

    IdentifierInfo *ClassName = IdentList[i];
    if (PrevIDecl && PrevIDecl->getIdentifier() != ClassName) {
      // A previous decl with a different name is because of
      // @compatibility_alias, for example:
      // \code
      //   @class NewImage;
      //   @compatibility_alias OldImage NewImage;
      // \endcode
      // A lookup for 'OldImage' will return the 'NewImage' decl.
      //
      // In such a case use the real declaration name, instead of the alias one,
      // otherwise we will break IdentifierResolver and redecls-chain invariants.
      // FIXME: If necessary, add a bit to indicate that this ObjCInterfaceDecl
      // has been aliased.
      ClassName = PrevIDecl->getIdentifier();
    }

    // If this forward declaration has type parameters, compare them with the
    // type parameters of the previous declaration.
    ObjCTypeParamList *TypeParams = TypeParamLists[i];
    if (PrevIDecl && TypeParams) {
      if (ObjCTypeParamList *PrevTypeParams = PrevIDecl->getTypeParamList()) {
        // Check for consistency with the previous declaration.
        if (checkTypeParamListConsistency(
              *this, PrevTypeParams, TypeParams,
              TypeParamListContext::ForwardDeclaration)) {
          TypeParams = nullptr;
        }
      } else if (ObjCInterfaceDecl *Def = PrevIDecl->getDefinition()) {
        // The @interface does not have type parameters. Complain.
        Diag(IdentLocs[i], diag::err_objc_parameterized_forward_class)
          << ClassName
          << TypeParams->getSourceRange();
        Diag(Def->getLocation(), diag::note_defined_here)
          << ClassName;

        TypeParams = nullptr;
      }
    }

    ObjCInterfaceDecl *IDecl
      = ObjCInterfaceDecl::Create(Context, CurContext, AtClassLoc,
                                  ClassName, TypeParams, PrevIDecl,
                                  IdentLocs[i]);
    IDecl->setAtEndRange(IdentLocs[i]);

    PushOnScopeChains(IDecl, TUScope);
    CheckObjCDeclScope(IDecl);
    DeclsInGroup.push_back(IDecl);
  }

  return BuildDeclaratorGroup(DeclsInGroup);
}

static bool tryMatchRecordTypes(ASTContext &Context,
                                Sema::MethodMatchStrategy strategy,
                                const Type *left, const Type *right);

static bool matchTypes(ASTContext &Context, Sema::MethodMatchStrategy strategy,
                       QualType leftQT, QualType rightQT) {
  const Type *left =
    Context.getCanonicalType(leftQT).getUnqualifiedType().getTypePtr();
  const Type *right =
    Context.getCanonicalType(rightQT).getUnqualifiedType().getTypePtr();

  if (left == right) return true;

  // If we're doing a strict match, the types have to match exactly.
  if (strategy == Sema::MMS_strict) return false;

  if (left->isIncompleteType() || right->isIncompleteType()) return false;

  // Otherwise, use this absurdly complicated algorithm to try to
  // validate the basic, low-level compatibility of the two types.

  // As a minimum, require the sizes and alignments to match.
  TypeInfo LeftTI = Context.getTypeInfo(left);
  TypeInfo RightTI = Context.getTypeInfo(right);
  if (LeftTI.Width != RightTI.Width)
    return false;

  if (LeftTI.Align != RightTI.Align)
    return false;

  // Consider all the kinds of non-dependent canonical types:
  // - functions and arrays aren't possible as return and parameter types

  // - vector types of equal size can be arbitrarily mixed
  if (isa<VectorType>(left)) return isa<VectorType>(right);
  if (isa<VectorType>(right)) return false;

  // - references should only match references of identical type
  // - structs, unions, and Objective-C objects must match more-or-less
  //   exactly
  // - everything else should be a scalar
  if (!left->isScalarType() || !right->isScalarType())
    return tryMatchRecordTypes(Context, strategy, left, right);

  // Make scalars agree in kind, except count bools as chars, and group
  // all non-member pointers together.
  Type::ScalarTypeKind leftSK = left->getScalarTypeKind();
  Type::ScalarTypeKind rightSK = right->getScalarTypeKind();
  if (leftSK == Type::STK_Bool) leftSK = Type::STK_Integral;
  if (rightSK == Type::STK_Bool) rightSK = Type::STK_Integral;
  if (leftSK == Type::STK_CPointer || leftSK == Type::STK_BlockPointer)
    leftSK = Type::STK_ObjCObjectPointer;
  if (rightSK == Type::STK_CPointer || rightSK == Type::STK_BlockPointer)
    rightSK = Type::STK_ObjCObjectPointer;

  // Note that data member pointers and function member pointers don't
  // intermix because of the size differences.

  return (leftSK == rightSK);
}

static bool tryMatchRecordTypes(ASTContext &Context,
                                Sema::MethodMatchStrategy strategy,
                                const Type *lt, const Type *rt) {
  assert(lt && rt && lt != rt);

  if (!isa<RecordType>(lt) || !isa<RecordType>(rt)) return false;
  RecordDecl *left = cast<RecordType>(lt)->getDecl();
  RecordDecl *right = cast<RecordType>(rt)->getDecl();

  // Require union-hood to match.
  if (left->isUnion() != right->isUnion()) return false;

  // Require an exact match if either is non-POD.
  if ((isa<CXXRecordDecl>(left) && !cast<CXXRecordDecl>(left)->isPOD()) ||
      (isa<CXXRecordDecl>(right) && !cast<CXXRecordDecl>(right)->isPOD()))
    return false;

  // Require size and alignment to match.
  TypeInfo LeftTI = Context.getTypeInfo(lt);
  TypeInfo RightTI = Context.getTypeInfo(rt);
  if (LeftTI.Width != RightTI.Width)
    return false;

  if (LeftTI.Align != RightTI.Align)
    return false;

  // Require fields to match.
  RecordDecl::field_iterator li = left->field_begin(), le = left->field_end();
  RecordDecl::field_iterator ri = right->field_begin(), re = right->field_end();
  for (; li != le && ri != re; ++li, ++ri) {
    if (!matchTypes(Context, strategy, li->getType(), ri->getType()))
      return false;
  }
  return (li == le && ri == re);
}

/// MatchTwoMethodDeclarations - Checks that two methods have matching type and
/// returns true, or false, accordingly.
/// TODO: Handle protocol list; such as id<p1,p2> in type comparisons
bool Sema::MatchTwoMethodDeclarations(const ObjCMethodDecl *left,
                                      const ObjCMethodDecl *right,
                                      MethodMatchStrategy strategy) {
  if (!matchTypes(Context, strategy, left->getReturnType(),
                  right->getReturnType()))
    return false;

  // If either is hidden, it is not considered to match.
  if (left->isHidden() || right->isHidden())
    return false;

  if (getLangOpts().ObjCAutoRefCount &&
      (left->hasAttr<NSReturnsRetainedAttr>()
         != right->hasAttr<NSReturnsRetainedAttr>() ||
       left->hasAttr<NSConsumesSelfAttr>()
         != right->hasAttr<NSConsumesSelfAttr>()))
    return false;

  ObjCMethodDecl::param_const_iterator
    li = left->param_begin(), le = left->param_end(), ri = right->param_begin(),
    re = right->param_end();

  for (; li != le && ri != re; ++li, ++ri) {
    assert(ri != right->param_end() && "Param mismatch");
    const ParmVarDecl *lparm = *li, *rparm = *ri;

    if (!matchTypes(Context, strategy, lparm->getType(), rparm->getType()))
      return false;

    if (getLangOpts().ObjCAutoRefCount &&
        lparm->hasAttr<NSConsumedAttr>() != rparm->hasAttr<NSConsumedAttr>())
      return false;
  }
  return true;
}

static bool isMethodContextSameForKindofLookup(ObjCMethodDecl *Method,
                                               ObjCMethodDecl *MethodInList) {
  auto *MethodProtocol = dyn_cast<ObjCProtocolDecl>(Method->getDeclContext());
  auto *MethodInListProtocol =
      dyn_cast<ObjCProtocolDecl>(MethodInList->getDeclContext());
  // If this method belongs to a protocol but the method in list does not, or
  // vice versa, we say the context is not the same.
  if ((MethodProtocol && !MethodInListProtocol) ||
      (!MethodProtocol && MethodInListProtocol))
    return false;

  if (MethodProtocol && MethodInListProtocol)
    return true;

  ObjCInterfaceDecl *MethodInterface = Method->getClassInterface();
  ObjCInterfaceDecl *MethodInListInterface =
      MethodInList->getClassInterface();
  return MethodInterface == MethodInListInterface;
}

void Sema::addMethodToGlobalList(ObjCMethodList *List,
                                 ObjCMethodDecl *Method) {
  // Record at the head of the list whether there were 0, 1, or >= 2 methods
  // inside categories.
  if (ObjCCategoryDecl *CD =
          dyn_cast<ObjCCategoryDecl>(Method->getDeclContext()))
    if (!CD->IsClassExtension() && List->getBits() < 2)
      List->setBits(List->getBits() + 1);

  // If the list is empty, make it a singleton list.
  if (List->getMethod() == nullptr) {
    List->setMethod(Method);
    List->setNext(nullptr);
    return;
  }

  // We've seen a method with this name, see if we have already seen this type
  // signature.
  ObjCMethodList *Previous = List;
  ObjCMethodList *ListWithSameDeclaration = nullptr;
  for (; List; Previous = List, List = List->getNext()) {
    // If we are building a module, keep all of the methods.
    if (getLangOpts().isCompilingModule())
      continue;

    bool SameDeclaration = MatchTwoMethodDeclarations(Method,
                                                      List->getMethod());
    // Looking for method with a type bound requires the correct context exists.
    // We need to insert a method into the list if the context is different.
    // If the method's declaration matches the list
    // a> the method belongs to a different context: we need to insert it, in
    //    order to emit the availability message, we need to prioritize over
    //    availability among the methods with the same declaration.
    // b> the method belongs to the same context: there is no need to insert a
    //    new entry.
    // If the method's declaration does not match the list, we insert it to the
    // end.
    if (!SameDeclaration ||
        !isMethodContextSameForKindofLookup(Method, List->getMethod())) {
      // Even if two method types do not match, we would like to say
      // there is more than one declaration so unavailability/deprecated
      // warning is not too noisy.
      if (!Method->isDefined())
        List->setHasMoreThanOneDecl(true);

      // For methods with the same declaration, the one that is deprecated
      // should be put in the front for better diagnostics.
      if (Method->isDeprecated() && SameDeclaration &&
          !ListWithSameDeclaration && !List->getMethod()->isDeprecated())
        ListWithSameDeclaration = List;

      if (Method->isUnavailable() && SameDeclaration &&
          !ListWithSameDeclaration &&
          List->getMethod()->getAvailability() < AR_Deprecated)
        ListWithSameDeclaration = List;
      continue;
    }

    ObjCMethodDecl *PrevObjCMethod = List->getMethod();

    // Propagate the 'defined' bit.
    if (Method->isDefined())
      PrevObjCMethod->setDefined(true);
    else {
      // Objective-C doesn't allow an @interface for a class after its
      // @implementation. So if Method is not defined and there already is
      // an entry for this type signature, Method has to be for a different
      // class than PrevObjCMethod.
      List->setHasMoreThanOneDecl(true);
    }

    // If a method is deprecated, push it in the global pool.
    // This is used for better diagnostics.
    if (Method->isDeprecated()) {
      if (!PrevObjCMethod->isDeprecated())
        List->setMethod(Method);
    }
    // If the new method is unavailable, push it into global pool
    // unless previous one is deprecated.
    if (Method->isUnavailable()) {
      if (PrevObjCMethod->getAvailability() < AR_Deprecated)
        List->setMethod(Method);
    }

    return;
  }

  // We have a new signature for an existing method - add it.
  // This is extremely rare. Only 1% of Cocoa selectors are "overloaded".
  ObjCMethodList *Mem = BumpAlloc.Allocate<ObjCMethodList>();

  // We insert it right before ListWithSameDeclaration.
  if (ListWithSameDeclaration) {
    auto *List = new (Mem) ObjCMethodList(*ListWithSameDeclaration);
    // FIXME: should we clear the other bits in ListWithSameDeclaration?
    ListWithSameDeclaration->setMethod(Method);
    ListWithSameDeclaration->setNext(List);
    return;
  }

  Previous->setNext(new (Mem) ObjCMethodList(Method));
}

/// Read the contents of the method pool for a given selector from
/// external storage.
void Sema::ReadMethodPool(Selector Sel) {
  assert(ExternalSource && "We need an external AST source");
  ExternalSource->ReadMethodPool(Sel);
}

void Sema::updateOutOfDateSelector(Selector Sel) {
  if (!ExternalSource)
    return;
  ExternalSource->updateOutOfDateSelector(Sel);
}

void Sema::AddMethodToGlobalPool(ObjCMethodDecl *Method, bool impl,
                                 bool instance) {
  // Ignore methods of invalid containers.
  if (cast<Decl>(Method->getDeclContext())->isInvalidDecl())
    return;

  if (ExternalSource)
    ReadMethodPool(Method->getSelector());

  GlobalMethodPool::iterator Pos = MethodPool.find(Method->getSelector());
  if (Pos == MethodPool.end())
    Pos = MethodPool.insert(std::make_pair(Method->getSelector(),
                                           GlobalMethods())).first;

  Method->setDefined(impl);

  ObjCMethodList &Entry = instance ? Pos->second.first : Pos->second.second;
  addMethodToGlobalList(&Entry, Method);
}

/// Determines if this is an "acceptable" loose mismatch in the global
/// method pool.  This exists mostly as a hack to get around certain
/// global mismatches which we can't afford to make warnings / errors.
/// Really, what we want is a way to take a method out of the global
/// method pool.
static bool isAcceptableMethodMismatch(ObjCMethodDecl *chosen,
                                       ObjCMethodDecl *other) {
  if (!chosen->isInstanceMethod())
    return false;

  Selector sel = chosen->getSelector();
  if (!sel.isUnarySelector() || sel.getNameForSlot(0) != "length")
    return false;

  // Don't complain about mismatches for -length if the method we
  // chose has an integral result type.
  return (chosen->getReturnType()->isIntegerType());
}

/// Return true if the given method is wthin the type bound.
static bool FilterMethodsByTypeBound(ObjCMethodDecl *Method,
                                     const ObjCObjectType *TypeBound) {
  if (!TypeBound)
    return true;

  if (TypeBound->isObjCId())
    // FIXME: should we handle the case of bounding to id<A, B> differently?
    return true;

  auto *BoundInterface = TypeBound->getInterface();
  assert(BoundInterface && "unexpected object type!");

  // Check if the Method belongs to a protocol. We should allow any method
  // defined in any protocol, because any subclass could adopt the protocol.
  auto *MethodProtocol = dyn_cast<ObjCProtocolDecl>(Method->getDeclContext());
  if (MethodProtocol) {
    return true;
  }

  // If the Method belongs to a class, check if it belongs to the class
  // hierarchy of the class bound.
  if (ObjCInterfaceDecl *MethodInterface = Method->getClassInterface()) {
    // We allow methods declared within classes that are part of the hierarchy
    // of the class bound (superclass of, subclass of, or the same as the class
    // bound).
    return MethodInterface == BoundInterface ||
           MethodInterface->isSuperClassOf(BoundInterface) ||
           BoundInterface->isSuperClassOf(MethodInterface);
  }
  llvm_unreachable("unknown method context");
}

/// We first select the type of the method: Instance or Factory, then collect
/// all methods with that type.
bool Sema::CollectMultipleMethodsInGlobalPool(
    Selector Sel, SmallVectorImpl<ObjCMethodDecl *> &Methods,
    bool InstanceFirst, bool CheckTheOther,
    const ObjCObjectType *TypeBound) {
  if (ExternalSource)
    ReadMethodPool(Sel);

  GlobalMethodPool::iterator Pos = MethodPool.find(Sel);
  if (Pos == MethodPool.end())
    return false;

  // Gather the non-hidden methods.
  ObjCMethodList &MethList = InstanceFirst ? Pos->second.first :
                             Pos->second.second;
  for (ObjCMethodList *M = &MethList; M; M = M->getNext())
    if (M->getMethod() && !M->getMethod()->isHidden()) {
      if (FilterMethodsByTypeBound(M->getMethod(), TypeBound))
        Methods.push_back(M->getMethod());
    }

  // Return if we find any method with the desired kind.
  if (!Methods.empty())
    return Methods.size() > 1;

  if (!CheckTheOther)
    return false;

  // Gather the other kind.
  ObjCMethodList &MethList2 = InstanceFirst ? Pos->second.second :
                              Pos->second.first;
  for (ObjCMethodList *M = &MethList2; M; M = M->getNext())
    if (M->getMethod() && !M->getMethod()->isHidden()) {
      if (FilterMethodsByTypeBound(M->getMethod(), TypeBound))
        Methods.push_back(M->getMethod());
    }

  return Methods.size() > 1;
}

bool Sema::AreMultipleMethodsInGlobalPool(
    Selector Sel, ObjCMethodDecl *BestMethod, SourceRange R,
    bool receiverIdOrClass, SmallVectorImpl<ObjCMethodDecl *> &Methods) {
  // Diagnose finding more than one method in global pool.
  SmallVector<ObjCMethodDecl *, 4> FilteredMethods;
  FilteredMethods.push_back(BestMethod);

  for (auto *M : Methods)
    if (M != BestMethod && !M->hasAttr<UnavailableAttr>())
      FilteredMethods.push_back(M);

  if (FilteredMethods.size() > 1)
    DiagnoseMultipleMethodInGlobalPool(FilteredMethods, Sel, R,
                                       receiverIdOrClass);

  GlobalMethodPool::iterator Pos = MethodPool.find(Sel);
  // Test for no method in the pool which should not trigger any warning by
  // caller.
  if (Pos == MethodPool.end())
    return true;
  ObjCMethodList &MethList =
    BestMethod->isInstanceMethod() ? Pos->second.first : Pos->second.second;
  return MethList.hasMoreThanOneDecl();
}

ObjCMethodDecl *Sema::LookupMethodInGlobalPool(Selector Sel, SourceRange R,
                                               bool receiverIdOrClass,
                                               bool instance) {
  if (ExternalSource)
    ReadMethodPool(Sel);

  GlobalMethodPool::iterator Pos = MethodPool.find(Sel);
  if (Pos == MethodPool.end())
    return nullptr;

  // Gather the non-hidden methods.
  ObjCMethodList &MethList = instance ? Pos->second.first : Pos->second.second;
  SmallVector<ObjCMethodDecl *, 4> Methods;
  for (ObjCMethodList *M = &MethList; M; M = M->getNext()) {
    if (M->getMethod() && !M->getMethod()->isHidden())
      return M->getMethod();
  }
  return nullptr;
}

void Sema::DiagnoseMultipleMethodInGlobalPool(SmallVectorImpl<ObjCMethodDecl*> &Methods,
                                              Selector Sel, SourceRange R,
                                              bool receiverIdOrClass) {
  // We found multiple methods, so we may have to complain.
  bool issueDiagnostic = false, issueError = false;

  // We support a warning which complains about *any* difference in
  // method signature.
  bool strictSelectorMatch =
  receiverIdOrClass &&
  !Diags.isIgnored(diag::warn_strict_multiple_method_decl, R.getBegin());
  if (strictSelectorMatch) {
    for (unsigned I = 1, N = Methods.size(); I != N; ++I) {
      if (!MatchTwoMethodDeclarations(Methods[0], Methods[I], MMS_strict)) {
        issueDiagnostic = true;
        break;
      }
    }
  }

  // If we didn't see any strict differences, we won't see any loose
  // differences.  In ARC, however, we also need to check for loose
  // mismatches, because most of them are errors.
  if (!strictSelectorMatch ||
      (issueDiagnostic && getLangOpts().ObjCAutoRefCount))
    for (unsigned I = 1, N = Methods.size(); I != N; ++I) {
      // This checks if the methods differ in type mismatch.
      if (!MatchTwoMethodDeclarations(Methods[0], Methods[I], MMS_loose) &&
          !isAcceptableMethodMismatch(Methods[0], Methods[I])) {
        issueDiagnostic = true;
        if (getLangOpts().ObjCAutoRefCount)
          issueError = true;
        break;
      }
    }

  if (issueDiagnostic) {
    if (issueError)
      Diag(R.getBegin(), diag::err_arc_multiple_method_decl) << Sel << R;
    else if (strictSelectorMatch)
      Diag(R.getBegin(), diag::warn_strict_multiple_method_decl) << Sel << R;
    else
      Diag(R.getBegin(), diag::warn_multiple_method_decl) << Sel << R;

    Diag(Methods[0]->getBeginLoc(),
         issueError ? diag::note_possibility : diag::note_using)
        << Methods[0]->getSourceRange();
    for (unsigned I = 1, N = Methods.size(); I != N; ++I) {
      Diag(Methods[I]->getBeginLoc(), diag::note_also_found)
          << Methods[I]->getSourceRange();
    }
  }
}

ObjCMethodDecl *Sema::LookupImplementedMethodInGlobalPool(Selector Sel) {
  GlobalMethodPool::iterator Pos = MethodPool.find(Sel);
  if (Pos == MethodPool.end())
    return nullptr;

  GlobalMethods &Methods = Pos->second;
  for (const ObjCMethodList *Method = &Methods.first; Method;
       Method = Method->getNext())
    if (Method->getMethod() &&
        (Method->getMethod()->isDefined() ||
         Method->getMethod()->isPropertyAccessor()))
      return Method->getMethod();

  for (const ObjCMethodList *Method = &Methods.second; Method;
       Method = Method->getNext())
    if (Method->getMethod() &&
        (Method->getMethod()->isDefined() ||
         Method->getMethod()->isPropertyAccessor()))
      return Method->getMethod();
  return nullptr;
}

static void
HelperSelectorsForTypoCorrection(
                      SmallVectorImpl<const ObjCMethodDecl *> &BestMethod,
                      StringRef Typo, const ObjCMethodDecl * Method) {
  const unsigned MaxEditDistance = 1;
  unsigned BestEditDistance = MaxEditDistance + 1;
  std::string MethodName = Method->getSelector().getAsString();

  unsigned MinPossibleEditDistance = abs((int)MethodName.size() - (int)Typo.size());
  if (MinPossibleEditDistance > 0 &&
      Typo.size() / MinPossibleEditDistance < 1)
    return;
  unsigned EditDistance = Typo.edit_distance(MethodName, true, MaxEditDistance);
  if (EditDistance > MaxEditDistance)
    return;
  if (EditDistance == BestEditDistance)
    BestMethod.push_back(Method);
  else if (EditDistance < BestEditDistance) {
    BestMethod.clear();
    BestMethod.push_back(Method);
  }
}

static bool HelperIsMethodInObjCType(Sema &S, Selector Sel,
                                     QualType ObjectType) {
  if (ObjectType.isNull())
    return true;
  if (S.LookupMethodInObjectType(Sel, ObjectType, true/*Instance method*/))
    return true;
  return S.LookupMethodInObjectType(Sel, ObjectType, false/*Class method*/) !=
         nullptr;
}

const ObjCMethodDecl *
Sema::SelectorsForTypoCorrection(Selector Sel,
                                 QualType ObjectType) {
  unsigned NumArgs = Sel.getNumArgs();
  SmallVector<const ObjCMethodDecl *, 8> Methods;
  bool ObjectIsId = true, ObjectIsClass = true;
  if (ObjectType.isNull())
    ObjectIsId = ObjectIsClass = false;
  else if (!ObjectType->isObjCObjectPointerType())
    return nullptr;
  else if (const ObjCObjectPointerType *ObjCPtr =
           ObjectType->getAsObjCInterfacePointerType()) {
    ObjectType = QualType(ObjCPtr->getInterfaceType(), 0);
    ObjectIsId = ObjectIsClass = false;
  }
  else if (ObjectType->isObjCIdType() || ObjectType->isObjCQualifiedIdType())
    ObjectIsClass = false;
  else if (ObjectType->isObjCClassType() || ObjectType->isObjCQualifiedClassType())
    ObjectIsId = false;
  else
    return nullptr;

  for (GlobalMethodPool::iterator b = MethodPool.begin(),
       e = MethodPool.end(); b != e; b++) {
    // instance methods
    for (ObjCMethodList *M = &b->second.first; M; M=M->getNext())
      if (M->getMethod() &&
          (M->getMethod()->getSelector().getNumArgs() == NumArgs) &&
          (M->getMethod()->getSelector() != Sel)) {
        if (ObjectIsId)
          Methods.push_back(M->getMethod());
        else if (!ObjectIsClass &&
                 HelperIsMethodInObjCType(*this, M->getMethod()->getSelector(),
                                          ObjectType))
          Methods.push_back(M->getMethod());
      }
    // class methods
    for (ObjCMethodList *M = &b->second.second; M; M=M->getNext())
      if (M->getMethod() &&
          (M->getMethod()->getSelector().getNumArgs() == NumArgs) &&
          (M->getMethod()->getSelector() != Sel)) {
        if (ObjectIsClass)
          Methods.push_back(M->getMethod());
        else if (!ObjectIsId &&
                 HelperIsMethodInObjCType(*this, M->getMethod()->getSelector(),
                                          ObjectType))
          Methods.push_back(M->getMethod());
      }
  }

  SmallVector<const ObjCMethodDecl *, 8> SelectedMethods;
  for (unsigned i = 0, e = Methods.size(); i < e; i++) {
    HelperSelectorsForTypoCorrection(SelectedMethods,
                                     Sel.getAsString(), Methods[i]);
  }
  return (SelectedMethods.size() == 1) ? SelectedMethods[0] : nullptr;
}

/// DiagnoseDuplicateIvars -
/// Check for duplicate ivars in the entire class at the start of
/// \@implementation. This becomes necesssary because class extension can
/// add ivars to a class in random order which will not be known until
/// class's \@implementation is seen.
void Sema::DiagnoseDuplicateIvars(ObjCInterfaceDecl *ID,
                                  ObjCInterfaceDecl *SID) {
  for (auto *Ivar : ID->ivars()) {
    if (Ivar->isInvalidDecl())
      continue;
    if (IdentifierInfo *II = Ivar->getIdentifier()) {
      ObjCIvarDecl* prevIvar = SID->lookupInstanceVariable(II);
      if (prevIvar) {
        Diag(Ivar->getLocation(), diag::err_duplicate_member) << II;
        Diag(prevIvar->getLocation(), diag::note_previous_declaration);
        Ivar->setInvalidDecl();
      }
    }
  }
}

/// Diagnose attempts to define ARC-__weak ivars when __weak is disabled.
static void DiagnoseWeakIvars(Sema &S, ObjCImplementationDecl *ID) {
  if (S.getLangOpts().ObjCWeak) return;

  for (auto ivar = ID->getClassInterface()->all_declared_ivar_begin();
         ivar; ivar = ivar->getNextIvar()) {
    if (ivar->isInvalidDecl()) continue;
    if (ivar->getType().getObjCLifetime() == Qualifiers::OCL_Weak) {
      if (S.getLangOpts().ObjCWeakRuntime) {
        S.Diag(ivar->getLocation(), diag::err_arc_weak_disabled);
      } else {
        S.Diag(ivar->getLocation(), diag::err_arc_weak_no_runtime);
      }
    }
  }
}

/// Diagnose attempts to use flexible array member with retainable object type.
static void DiagnoseRetainableFlexibleArrayMember(Sema &S,
                                                  ObjCInterfaceDecl *ID) {
  if (!S.getLangOpts().ObjCAutoRefCount)
    return;

  for (auto ivar = ID->all_declared_ivar_begin(); ivar;
       ivar = ivar->getNextIvar()) {
    if (ivar->isInvalidDecl())
      continue;
    QualType IvarTy = ivar->getType();
    if (IvarTy->isIncompleteArrayType() &&
        (IvarTy.getObjCLifetime() != Qualifiers::OCL_ExplicitNone) &&
        IvarTy->isObjCLifetimeType()) {
      S.Diag(ivar->getLocation(), diag::err_flexible_array_arc_retainable);
      ivar->setInvalidDecl();
    }
  }
}

Sema::ObjCContainerKind Sema::getObjCContainerKind() const {
  switch (CurContext->getDeclKind()) {
    case Decl::ObjCInterface:
      return Sema::OCK_Interface;
    case Decl::ObjCProtocol:
      return Sema::OCK_Protocol;
    case Decl::ObjCCategory:
      if (cast<ObjCCategoryDecl>(CurContext)->IsClassExtension())
        return Sema::OCK_ClassExtension;
      return Sema::OCK_Category;
    case Decl::ObjCImplementation:
      return Sema::OCK_Implementation;
    case Decl::ObjCCategoryImpl:
      return Sema::OCK_CategoryImplementation;

    default:
      return Sema::OCK_None;
  }
}

static bool IsVariableSizedType(QualType T) {
  if (T->isIncompleteArrayType())
    return true;
  const auto *RecordTy = T->getAs<RecordType>();
  return (RecordTy && RecordTy->getDecl()->hasFlexibleArrayMember());
}

static void DiagnoseVariableSizedIvars(Sema &S, ObjCContainerDecl *OCD) {
  ObjCInterfaceDecl *IntfDecl = nullptr;
  ObjCInterfaceDecl::ivar_range Ivars = llvm::make_range(
      ObjCInterfaceDecl::ivar_iterator(), ObjCInterfaceDecl::ivar_iterator());
  if ((IntfDecl = dyn_cast<ObjCInterfaceDecl>(OCD))) {
    Ivars = IntfDecl->ivars();
  } else if (auto *ImplDecl = dyn_cast<ObjCImplementationDecl>(OCD)) {
    IntfDecl = ImplDecl->getClassInterface();
    Ivars = ImplDecl->ivars();
  } else if (auto *CategoryDecl = dyn_cast<ObjCCategoryDecl>(OCD)) {
    if (CategoryDecl->IsClassExtension()) {
      IntfDecl = CategoryDecl->getClassInterface();
      Ivars = CategoryDecl->ivars();
    }
  }

  // Check if variable sized ivar is in interface and visible to subclasses.
  if (!isa<ObjCInterfaceDecl>(OCD)) {
    for (auto ivar : Ivars) {
      if (!ivar->isInvalidDecl() && IsVariableSizedType(ivar->getType())) {
        S.Diag(ivar->getLocation(), diag::warn_variable_sized_ivar_visibility)
            << ivar->getDeclName() << ivar->getType();
      }
    }
  }

  // Subsequent checks require interface decl.
  if (!IntfDecl)
    return;

  // Check if variable sized ivar is followed by another ivar.
  for (ObjCIvarDecl *ivar = IntfDecl->all_declared_ivar_begin(); ivar;
       ivar = ivar->getNextIvar()) {
    if (ivar->isInvalidDecl() || !ivar->getNextIvar())
      continue;
    QualType IvarTy = ivar->getType();
    bool IsInvalidIvar = false;
    if (IvarTy->isIncompleteArrayType()) {
      S.Diag(ivar->getLocation(), diag::err_flexible_array_not_at_end)
          << ivar->getDeclName() << IvarTy
          << TTK_Class; // Use "class" for Obj-C.
      IsInvalidIvar = true;
    } else if (const RecordType *RecordTy = IvarTy->getAs<RecordType>()) {
      if (RecordTy->getDecl()->hasFlexibleArrayMember()) {
        S.Diag(ivar->getLocation(),
               diag::err_objc_variable_sized_type_not_at_end)
            << ivar->getDeclName() << IvarTy;
        IsInvalidIvar = true;
      }
    }
    if (IsInvalidIvar) {
      S.Diag(ivar->getNextIvar()->getLocation(),
             diag::note_next_ivar_declaration)
          << ivar->getNextIvar()->getSynthesize();
      ivar->setInvalidDecl();
    }
  }

  // Check if ObjC container adds ivars after variable sized ivar in superclass.
  // Perform the check only if OCD is the first container to declare ivars to
  // avoid multiple warnings for the same ivar.
  ObjCIvarDecl *FirstIvar =
      (Ivars.begin() == Ivars.end()) ? nullptr : *Ivars.begin();
  if (FirstIvar && (FirstIvar == IntfDecl->all_declared_ivar_begin())) {
    const ObjCInterfaceDecl *SuperClass = IntfDecl->getSuperClass();
    while (SuperClass && SuperClass->ivar_empty())
      SuperClass = SuperClass->getSuperClass();
    if (SuperClass) {
      auto IvarIter = SuperClass->ivar_begin();
      std::advance(IvarIter, SuperClass->ivar_size() - 1);
      const ObjCIvarDecl *LastIvar = *IvarIter;
      if (IsVariableSizedType(LastIvar->getType())) {
        S.Diag(FirstIvar->getLocation(),
               diag::warn_superclass_variable_sized_type_not_at_end)
            << FirstIvar->getDeclName() << LastIvar->getDeclName()
            << LastIvar->getType() << SuperClass->getDeclName();
        S.Diag(LastIvar->getLocation(), diag::note_entity_declared_at)
            << LastIvar->getDeclName();
      }
    }
  }
}

// Note: For class/category implementations, allMethods is always null.
Decl *Sema::ActOnAtEnd(Scope *S, SourceRange AtEnd, ArrayRef<Decl *> allMethods,
                       ArrayRef<DeclGroupPtrTy> allTUVars) {
  if (getObjCContainerKind() == Sema::OCK_None)
    return nullptr;

  assert(AtEnd.isValid() && "Invalid location for '@end'");

  auto *OCD = cast<ObjCContainerDecl>(CurContext);
  Decl *ClassDecl = OCD;

  bool isInterfaceDeclKind =
        isa<ObjCInterfaceDecl>(ClassDecl) || isa<ObjCCategoryDecl>(ClassDecl)
         || isa<ObjCProtocolDecl>(ClassDecl);
  bool checkIdenticalMethods = isa<ObjCImplementationDecl>(ClassDecl);

  // FIXME: Remove these and use the ObjCContainerDecl/DeclContext.
  llvm::DenseMap<Selector, const ObjCMethodDecl*> InsMap;
  llvm::DenseMap<Selector, const ObjCMethodDecl*> ClsMap;

  for (unsigned i = 0, e = allMethods.size(); i != e; i++ ) {
    ObjCMethodDecl *Method =
      cast_or_null<ObjCMethodDecl>(allMethods[i]);

    if (!Method) continue;  // Already issued a diagnostic.
    if (Method->isInstanceMethod()) {
      /// Check for instance method of the same name with incompatible types
      const ObjCMethodDecl *&PrevMethod = InsMap[Method->getSelector()];
      bool match = PrevMethod ? MatchTwoMethodDeclarations(Method, PrevMethod)
                              : false;
      if ((isInterfaceDeclKind && PrevMethod && !match)
          || (checkIdenticalMethods && match)) {
          Diag(Method->getLocation(), diag::err_duplicate_method_decl)
            << Method->getDeclName();
          Diag(PrevMethod->getLocation(), diag::note_previous_declaration);
        Method->setInvalidDecl();
      } else {
        if (PrevMethod) {
          Method->setAsRedeclaration(PrevMethod);
          if (!Context.getSourceManager().isInSystemHeader(
                 Method->getLocation()))
            Diag(Method->getLocation(), diag::warn_duplicate_method_decl)
              << Method->getDeclName();
          Diag(PrevMethod->getLocation(), diag::note_previous_declaration);
        }
        InsMap[Method->getSelector()] = Method;
        /// The following allows us to typecheck messages to "id".
        AddInstanceMethodToGlobalPool(Method);
      }
    } else {
      /// Check for class method of the same name with incompatible types
      const ObjCMethodDecl *&PrevMethod = ClsMap[Method->getSelector()];
      bool match = PrevMethod ? MatchTwoMethodDeclarations(Method, PrevMethod)
                              : false;
      if ((isInterfaceDeclKind && PrevMethod && !match)
          || (checkIdenticalMethods && match)) {
        Diag(Method->getLocation(), diag::err_duplicate_method_decl)
          << Method->getDeclName();
        Diag(PrevMethod->getLocation(), diag::note_previous_declaration);
        Method->setInvalidDecl();
      } else {
        if (PrevMethod) {
          Method->setAsRedeclaration(PrevMethod);
          if (!Context.getSourceManager().isInSystemHeader(
                 Method->getLocation()))
            Diag(Method->getLocation(), diag::warn_duplicate_method_decl)
              << Method->getDeclName();
          Diag(PrevMethod->getLocation(), diag::note_previous_declaration);
        }
        ClsMap[Method->getSelector()] = Method;
        AddFactoryMethodToGlobalPool(Method);
      }
    }
  }
  if (isa<ObjCInterfaceDecl>(ClassDecl)) {
    // Nothing to do here.
  } else if (ObjCCategoryDecl *C = dyn_cast<ObjCCategoryDecl>(ClassDecl)) {
    // Categories are used to extend the class by declaring new methods.
    // By the same token, they are also used to add new properties. No
    // need to compare the added property to those in the class.

    if (C->IsClassExtension()) {
      ObjCInterfaceDecl *CCPrimary = C->getClassInterface();
      DiagnoseClassExtensionDupMethods(C, CCPrimary);
    }
  }
  if (ObjCContainerDecl *CDecl = dyn_cast<ObjCContainerDecl>(ClassDecl)) {
    if (CDecl->getIdentifier())
      // ProcessPropertyDecl is responsible for diagnosing conflicts with any
      // user-defined setter/getter. It also synthesizes setter/getter methods
      // and adds them to the DeclContext and global method pools.
      for (auto *I : CDecl->properties())
        ProcessPropertyDecl(I);
    CDecl->setAtEndRange(AtEnd);
  }
  if (ObjCImplementationDecl *IC=dyn_cast<ObjCImplementationDecl>(ClassDecl)) {
    IC->setAtEndRange(AtEnd);
    if (ObjCInterfaceDecl* IDecl = IC->getClassInterface()) {
      // Any property declared in a class extension might have user
      // declared setter or getter in current class extension or one
      // of the other class extensions. Mark them as synthesized as
      // property will be synthesized when property with same name is
      // seen in the @implementation.
      for (const auto *Ext : IDecl->visible_extensions()) {
        for (const auto *Property : Ext->instance_properties()) {
          // Skip over properties declared @dynamic
          if (const ObjCPropertyImplDecl *PIDecl
              = IC->FindPropertyImplDecl(Property->getIdentifier(),
                                         Property->getQueryKind()))
            if (PIDecl->getPropertyImplementation()
                  == ObjCPropertyImplDecl::Dynamic)
              continue;

          for (const auto *Ext : IDecl->visible_extensions()) {
            if (ObjCMethodDecl *GetterMethod
                  = Ext->getInstanceMethod(Property->getGetterName()))
              GetterMethod->setPropertyAccessor(true);
            if (!Property->isReadOnly())
              if (ObjCMethodDecl *SetterMethod
                    = Ext->getInstanceMethod(Property->getSetterName()))
                SetterMethod->setPropertyAccessor(true);
          }
        }
      }
      ImplMethodsVsClassMethods(S, IC, IDecl);
      AtomicPropertySetterGetterRules(IC, IDecl);
      DiagnoseOwningPropertyGetterSynthesis(IC);
      DiagnoseUnusedBackingIvarInAccessor(S, IC);
      if (IDecl->hasDesignatedInitializers())
        DiagnoseMissingDesignatedInitOverrides(IC, IDecl);
      DiagnoseWeakIvars(*this, IC);
      DiagnoseRetainableFlexibleArrayMember(*this, IDecl);

      bool HasRootClassAttr = IDecl->hasAttr<ObjCRootClassAttr>();
      if (IDecl->getSuperClass() == nullptr) {
        // This class has no superclass, so check that it has been marked with
        // __attribute((objc_root_class)).
        if (!HasRootClassAttr) {
          SourceLocation DeclLoc(IDecl->getLocation());
          SourceLocation SuperClassLoc(getLocForEndOfToken(DeclLoc));
          Diag(DeclLoc, diag::warn_objc_root_class_missing)
            << IDecl->getIdentifier();
          // See if NSObject is in the current scope, and if it is, suggest
          // adding " : NSObject " to the class declaration.
          NamedDecl *IF = LookupSingleName(TUScope,
                                           NSAPIObj->getNSClassId(NSAPI::ClassId_NSObject),
                                           DeclLoc, LookupOrdinaryName);
          ObjCInterfaceDecl *NSObjectDecl = dyn_cast_or_null<ObjCInterfaceDecl>(IF);
          if (NSObjectDecl && NSObjectDecl->getDefinition()) {
            Diag(SuperClassLoc, diag::note_objc_needs_superclass)
              << FixItHint::CreateInsertion(SuperClassLoc, " : NSObject ");
          } else {
            Diag(SuperClassLoc, diag::note_objc_needs_superclass);
          }
        }
      } else if (HasRootClassAttr) {
        // Complain that only root classes may have this attribute.
        Diag(IDecl->getLocation(), diag::err_objc_root_class_subclass);
      }

      if (const ObjCInterfaceDecl *Super = IDecl->getSuperClass()) {
        // An interface can subclass another interface with a
        // objc_subclassing_restricted attribute when it has that attribute as
        // well (because of interfaces imported from Swift). Therefore we have
        // to check if we can subclass in the implementation as well.
        if (IDecl->hasAttr<ObjCSubclassingRestrictedAttr>() &&
            Super->hasAttr<ObjCSubclassingRestrictedAttr>()) {
          Diag(IC->getLocation(), diag::err_restricted_superclass_mismatch);
          Diag(Super->getLocation(), diag::note_class_declared);
        }
      }

      if (LangOpts.ObjCRuntime.isNonFragile()) {
        while (IDecl->getSuperClass()) {
          DiagnoseDuplicateIvars(IDecl, IDecl->getSuperClass());
          IDecl = IDecl->getSuperClass();
        }
      }
    }
    SetIvarInitializers(IC);
  } else if (ObjCCategoryImplDecl* CatImplClass =
                                   dyn_cast<ObjCCategoryImplDecl>(ClassDecl)) {
    CatImplClass->setAtEndRange(AtEnd);

    // Find category interface decl and then check that all methods declared
    // in this interface are implemented in the category @implementation.
    if (ObjCInterfaceDecl* IDecl = CatImplClass->getClassInterface()) {
      if (ObjCCategoryDecl *Cat
            = IDecl->FindCategoryDeclaration(CatImplClass->getIdentifier())) {
        ImplMethodsVsClassMethods(S, CatImplClass, Cat);
      }
    }
  } else if (const auto *IntfDecl = dyn_cast<ObjCInterfaceDecl>(ClassDecl)) {
    if (const ObjCInterfaceDecl *Super = IntfDecl->getSuperClass()) {
      if (!IntfDecl->hasAttr<ObjCSubclassingRestrictedAttr>() &&
          Super->hasAttr<ObjCSubclassingRestrictedAttr>()) {
        Diag(IntfDecl->getLocation(), diag::err_restricted_superclass_mismatch);
        Diag(Super->getLocation(), diag::note_class_declared);
      }
    }
  }
  DiagnoseVariableSizedIvars(*this, OCD);
  if (isInterfaceDeclKind) {
    // Reject invalid vardecls.
    for (unsigned i = 0, e = allTUVars.size(); i != e; i++) {
      DeclGroupRef DG = allTUVars[i].get();
      for (DeclGroupRef::iterator I = DG.begin(), E = DG.end(); I != E; ++I)
        if (VarDecl *VDecl = dyn_cast<VarDecl>(*I)) {
          if (!VDecl->hasExternalStorage())
            Diag(VDecl->getLocation(), diag::err_objc_var_decl_inclass);
        }
    }
  }
  ActOnObjCContainerFinishDefinition();

  for (unsigned i = 0, e = allTUVars.size(); i != e; i++) {
    DeclGroupRef DG = allTUVars[i].get();
    for (DeclGroupRef::iterator I = DG.begin(), E = DG.end(); I != E; ++I)
      (*I)->setTopLevelDeclInObjCContainer();
    Consumer.HandleTopLevelDeclInObjCContainer(DG);
  }

  ActOnDocumentableDecl(ClassDecl);
  return ClassDecl;
}

/// CvtQTToAstBitMask - utility routine to produce an AST bitmask for
/// objective-c's type qualifier from the parser version of the same info.
static Decl::ObjCDeclQualifier
CvtQTToAstBitMask(ObjCDeclSpec::ObjCDeclQualifier PQTVal) {
  return (Decl::ObjCDeclQualifier) (unsigned) PQTVal;
}

/// Check whether the declared result type of the given Objective-C
/// method declaration is compatible with the method's class.
///
static Sema::ResultTypeCompatibilityKind
CheckRelatedResultTypeCompatibility(Sema &S, ObjCMethodDecl *Method,
                                    ObjCInterfaceDecl *CurrentClass) {
  QualType ResultType = Method->getReturnType();

  // If an Objective-C method inherits its related result type, then its
  // declared result type must be compatible with its own class type. The
  // declared result type is compatible if:
  if (const ObjCObjectPointerType *ResultObjectType
                                = ResultType->getAs<ObjCObjectPointerType>()) {
    //   - it is id or qualified id, or
    if (ResultObjectType->isObjCIdType() ||
        ResultObjectType->isObjCQualifiedIdType())
      return Sema::RTC_Compatible;

    if (CurrentClass) {
      if (ObjCInterfaceDecl *ResultClass
                                      = ResultObjectType->getInterfaceDecl()) {
        //   - it is the same as the method's class type, or
        if (declaresSameEntity(CurrentClass, ResultClass))
          return Sema::RTC_Compatible;

        //   - it is a superclass of the method's class type
        if (ResultClass->isSuperClassOf(CurrentClass))
          return Sema::RTC_Compatible;
      }
    } else {
      // Any Objective-C pointer type might be acceptable for a protocol
      // method; we just don't know.
      return Sema::RTC_Unknown;
    }
  }

  return Sema::RTC_Incompatible;
}

namespace {
/// A helper class for searching for methods which a particular method
/// overrides.
class OverrideSearch {
public:
  Sema &S;
  ObjCMethodDecl *Method;
  llvm::SmallSetVector<ObjCMethodDecl*, 4> Overridden;
  bool Recursive;

public:
  OverrideSearch(Sema &S, ObjCMethodDecl *method) : S(S), Method(method) {
    Selector selector = method->getSelector();

    // Bypass this search if we've never seen an instance/class method
    // with this selector before.
    Sema::GlobalMethodPool::iterator it = S.MethodPool.find(selector);
    if (it == S.MethodPool.end()) {
      if (!S.getExternalSource()) return;
      S.ReadMethodPool(selector);

      it = S.MethodPool.find(selector);
      if (it == S.MethodPool.end())
        return;
    }
    ObjCMethodList &list =
      method->isInstanceMethod() ? it->second.first : it->second.second;
    if (!list.getMethod()) return;

    ObjCContainerDecl *container
      = cast<ObjCContainerDecl>(method->getDeclContext());

    // Prevent the search from reaching this container again.  This is
    // important with categories, which override methods from the
    // interface and each other.
    if (ObjCCategoryDecl *Category = dyn_cast<ObjCCategoryDecl>(container)) {
      searchFromContainer(container);
      if (ObjCInterfaceDecl *Interface = Category->getClassInterface())
        searchFromContainer(Interface);
    } else {
      searchFromContainer(container);
    }
  }

  typedef decltype(Overridden)::iterator iterator;
  iterator begin() const { return Overridden.begin(); }
  iterator end() const { return Overridden.end(); }

private:
  void searchFromContainer(ObjCContainerDecl *container) {
    if (container->isInvalidDecl()) return;

    switch (container->getDeclKind()) {
#define OBJCCONTAINER(type, base) \
    case Decl::type: \
      searchFrom(cast<type##Decl>(container)); \
      break;
#define ABSTRACT_DECL(expansion)
#define DECL(type, base) \
    case Decl::type:
#include "clang/AST/DeclNodes.inc"
      llvm_unreachable("not an ObjC container!");
    }
  }

  void searchFrom(ObjCProtocolDecl *protocol) {
    if (!protocol->hasDefinition())
      return;

    // A method in a protocol declaration overrides declarations from
    // referenced ("parent") protocols.
    search(protocol->getReferencedProtocols());
  }

  void searchFrom(ObjCCategoryDecl *category) {
    // A method in a category declaration overrides declarations from
    // the main class and from protocols the category references.
    // The main class is handled in the constructor.
    search(category->getReferencedProtocols());
  }

  void searchFrom(ObjCCategoryImplDecl *impl) {
    // A method in a category definition that has a category
    // declaration overrides declarations from the category
    // declaration.
    if (ObjCCategoryDecl *category = impl->getCategoryDecl()) {
      search(category);
      if (ObjCInterfaceDecl *Interface = category->getClassInterface())
        search(Interface);

    // Otherwise it overrides declarations from the class.
    } else if (ObjCInterfaceDecl *Interface = impl->getClassInterface()) {
      search(Interface);
    }
  }

  void searchFrom(ObjCInterfaceDecl *iface) {
    // A method in a class declaration overrides declarations from
    if (!iface->hasDefinition())
      return;

    //   - categories,
    for (auto *Cat : iface->known_categories())
      search(Cat);

    //   - the super class, and
    if (ObjCInterfaceDecl *super = iface->getSuperClass())
      search(super);

    //   - any referenced protocols.
    search(iface->getReferencedProtocols());
  }

  void searchFrom(ObjCImplementationDecl *impl) {
    // A method in a class implementation overrides declarations from
    // the class interface.
    if (ObjCInterfaceDecl *Interface = impl->getClassInterface())
      search(Interface);
  }

  void search(const ObjCProtocolList &protocols) {
    for (ObjCProtocolList::iterator i = protocols.begin(), e = protocols.end();
         i != e; ++i)
      search(*i);
  }

  void search(ObjCContainerDecl *container) {
    // Check for a method in this container which matches this selector.
    ObjCMethodDecl *meth = container->getMethod(Method->getSelector(),
                                                Method->isInstanceMethod(),
                                                /*AllowHidden=*/true);

    // If we find one, record it and bail out.
    if (meth) {
      Overridden.insert(meth);
      return;
    }

    // Otherwise, search for methods that a hypothetical method here
    // would have overridden.

    // Note that we're now in a recursive case.
    Recursive = true;

    searchFromContainer(container);
  }
};
} // end anonymous namespace

void Sema::CheckObjCMethodOverrides(ObjCMethodDecl *ObjCMethod,
                                    ObjCInterfaceDecl *CurrentClass,
                                    ResultTypeCompatibilityKind RTC) {
  // Search for overridden methods and merge information down from them.
  OverrideSearch overrides(*this, ObjCMethod);
  // Keep track if the method overrides any method in the class's base classes,
  // its protocols, or its categories' protocols; we will keep that info
  // in the ObjCMethodDecl.
  // For this info, a method in an implementation is not considered as
  // overriding the same method in the interface or its categories.
  bool hasOverriddenMethodsInBaseOrProtocol = false;
  for (OverrideSearch::iterator
         i = overrides.begin(), e = overrides.end(); i != e; ++i) {
    ObjCMethodDecl *overridden = *i;

    if (!hasOverriddenMethodsInBaseOrProtocol) {
      if (isa<ObjCProtocolDecl>(overridden->getDeclContext()) ||
          CurrentClass != overridden->getClassInterface() ||
          overridden->isOverriding()) {
        hasOverriddenMethodsInBaseOrProtocol = true;

      } else if (isa<ObjCImplDecl>(ObjCMethod->getDeclContext())) {
        // OverrideSearch will return as "overridden" the same method in the
        // interface. For hasOverriddenMethodsInBaseOrProtocol, we need to
        // check whether a category of a base class introduced a method with the
        // same selector, after the interface method declaration.
        // To avoid unnecessary lookups in the majority of cases, we use the
        // extra info bits in GlobalMethodPool to check whether there were any
        // category methods with this selector.
        GlobalMethodPool::iterator It =
            MethodPool.find(ObjCMethod->getSelector());
        if (It != MethodPool.end()) {
          ObjCMethodList &List =
            ObjCMethod->isInstanceMethod()? It->second.first: It->second.second;
          unsigned CategCount = List.getBits();
          if (CategCount > 0) {
            // If the method is in a category we'll do lookup if there were at
            // least 2 category methods recorded, otherwise only one will do.
            if (CategCount > 1 ||
                !isa<ObjCCategoryImplDecl>(overridden->getDeclContext())) {
              OverrideSearch overrides(*this, overridden);
              for (OverrideSearch::iterator
                     OI= overrides.begin(), OE= overrides.end(); OI!=OE; ++OI) {
                ObjCMethodDecl *SuperOverridden = *OI;
                if (isa<ObjCProtocolDecl>(SuperOverridden->getDeclContext()) ||
                    CurrentClass != SuperOverridden->getClassInterface()) {
                  hasOverriddenMethodsInBaseOrProtocol = true;
                  overridden->setOverriding(true);
                  break;
                }
              }
            }
          }
        }
      }
    }

    // Propagate down the 'related result type' bit from overridden methods.
    if (RTC != Sema::RTC_Incompatible && overridden->hasRelatedResultType())
      ObjCMethod->setRelatedResultType();

    // Then merge the declarations.
    mergeObjCMethodDecls(ObjCMethod, overridden);

    if (ObjCMethod->isImplicit() && overridden->isImplicit())
      continue; // Conflicting properties are detected elsewhere.

    // Check for overriding methods
    if (isa<ObjCInterfaceDecl>(ObjCMethod->getDeclContext()) ||
        isa<ObjCImplementationDecl>(ObjCMethod->getDeclContext()))
      CheckConflictingOverridingMethod(ObjCMethod, overridden,
              isa<ObjCProtocolDecl>(overridden->getDeclContext()));

    if (CurrentClass && overridden->getDeclContext() != CurrentClass &&
        isa<ObjCInterfaceDecl>(overridden->getDeclContext()) &&
        !overridden->isImplicit() /* not meant for properties */) {
      ObjCMethodDecl::param_iterator ParamI = ObjCMethod->param_begin(),
                                          E = ObjCMethod->param_end();
      ObjCMethodDecl::param_iterator PrevI = overridden->param_begin(),
                                     PrevE = overridden->param_end();
      for (; ParamI != E && PrevI != PrevE; ++ParamI, ++PrevI) {
        assert(PrevI != overridden->param_end() && "Param mismatch");
        QualType T1 = Context.getCanonicalType((*ParamI)->getType());
        QualType T2 = Context.getCanonicalType((*PrevI)->getType());
        // If type of argument of method in this class does not match its
        // respective argument type in the super class method, issue warning;
        if (!Context.typesAreCompatible(T1, T2)) {
          Diag((*ParamI)->getLocation(), diag::ext_typecheck_base_super)
            << T1 << T2;
          Diag(overridden->getLocation(), diag::note_previous_declaration);
          break;
        }
      }
    }
  }

  ObjCMethod->setOverriding(hasOverriddenMethodsInBaseOrProtocol);
}

/// Merge type nullability from for a redeclaration of the same entity,
/// producing the updated type of the redeclared entity.
static QualType mergeTypeNullabilityForRedecl(Sema &S, SourceLocation loc,
                                              QualType type,
                                              bool usesCSKeyword,
                                              SourceLocation prevLoc,
                                              QualType prevType,
                                              bool prevUsesCSKeyword) {
  // Determine the nullability of both types.
  auto nullability = type->getNullability(S.Context);
  auto prevNullability = prevType->getNullability(S.Context);

  // Easy case: both have nullability.
  if (nullability.hasValue() == prevNullability.hasValue()) {
    // Neither has nullability; continue.
    if (!nullability)
      return type;

    // The nullabilities are equivalent; do nothing.
    if (*nullability == *prevNullability)
      return type;

    // Complain about mismatched nullability.
    S.Diag(loc, diag::err_nullability_conflicting)
      << DiagNullabilityKind(*nullability, usesCSKeyword)
      << DiagNullabilityKind(*prevNullability, prevUsesCSKeyword);
    return type;
  }

  // If it's the redeclaration that has nullability, don't change anything.
  if (nullability)
    return type;

  // Otherwise, provide the result with the same nullability.
  return S.Context.getAttributedType(
           AttributedType::getNullabilityAttrKind(*prevNullability),
           type, type);
}

/// Merge information from the declaration of a method in the \@interface
/// (or a category/extension) into the corresponding method in the
/// @implementation (for a class or category).
static void mergeInterfaceMethodToImpl(Sema &S,
                                       ObjCMethodDecl *method,
                                       ObjCMethodDecl *prevMethod) {
  // Merge the objc_requires_super attribute.
  if (prevMethod->hasAttr<ObjCRequiresSuperAttr>() &&
      !method->hasAttr<ObjCRequiresSuperAttr>()) {
    // merge the attribute into implementation.
    method->addAttr(
      ObjCRequiresSuperAttr::CreateImplicit(S.Context,
                                            method->getLocation()));
  }

  // Merge nullability of the result type.
  QualType newReturnType
    = mergeTypeNullabilityForRedecl(
        S, method->getReturnTypeSourceRange().getBegin(),
        method->getReturnType(),
        method->getObjCDeclQualifier() & Decl::OBJC_TQ_CSNullability,
        prevMethod->getReturnTypeSourceRange().getBegin(),
        prevMethod->getReturnType(),
        prevMethod->getObjCDeclQualifier() & Decl::OBJC_TQ_CSNullability);
  method->setReturnType(newReturnType);

  // Handle each of the parameters.
  unsigned numParams = method->param_size();
  unsigned numPrevParams = prevMethod->param_size();
  for (unsigned i = 0, n = std::min(numParams, numPrevParams); i != n; ++i) {
    ParmVarDecl *param = method->param_begin()[i];
    ParmVarDecl *prevParam = prevMethod->param_begin()[i];

    // Merge nullability.
    QualType newParamType
      = mergeTypeNullabilityForRedecl(
          S, param->getLocation(), param->getType(),
          param->getObjCDeclQualifier() & Decl::OBJC_TQ_CSNullability,
          prevParam->getLocation(), prevParam->getType(),
          prevParam->getObjCDeclQualifier() & Decl::OBJC_TQ_CSNullability);
    param->setType(newParamType);
  }
}

/// Verify that the method parameters/return value have types that are supported
/// by the x86 target.
static void checkObjCMethodX86VectorTypes(Sema &SemaRef,
                                          const ObjCMethodDecl *Method) {
  assert(SemaRef.getASTContext().getTargetInfo().getTriple().getArch() ==
             llvm::Triple::x86 &&
         "x86-specific check invoked for a different target");
  SourceLocation Loc;
  QualType T;
  for (const ParmVarDecl *P : Method->parameters()) {
    if (P->getType()->isVectorType()) {
      Loc = P->getBeginLoc();
      T = P->getType();
      break;
    }
  }
  if (Loc.isInvalid()) {
    if (Method->getReturnType()->isVectorType()) {
      Loc = Method->getReturnTypeSourceRange().getBegin();
      T = Method->getReturnType();
    } else
      return;
  }

  // Vector parameters/return values are not supported by objc_msgSend on x86 in
  // iOS < 9 and macOS < 10.11.
  const auto &Triple = SemaRef.getASTContext().getTargetInfo().getTriple();
  VersionTuple AcceptedInVersion;
  if (Triple.getOS() == llvm::Triple::IOS)
    AcceptedInVersion = VersionTuple(/*Major=*/9);
  else if (Triple.isMacOSX())
    AcceptedInVersion = VersionTuple(/*Major=*/10, /*Minor=*/11);
  else
    return;
  if (SemaRef.getASTContext().getTargetInfo().getPlatformMinVersion() >=
      AcceptedInVersion)
    return;
  SemaRef.Diag(Loc, diag::err_objc_method_unsupported_param_ret_type)
      << T << (Method->getReturnType()->isVectorType() ? /*return value*/ 1
                                                       : /*parameter*/ 0)
      << (Triple.isMacOSX() ? "macOS 10.11" : "iOS 9");
}

Decl *Sema::ActOnMethodDeclaration(
    Scope *S, SourceLocation MethodLoc, SourceLocation EndLoc,
    tok::TokenKind MethodType, ObjCDeclSpec &ReturnQT, ParsedType ReturnType,
    ArrayRef<SourceLocation> SelectorLocs, Selector Sel,
    // optional arguments. The number of types/arguments is obtained
    // from the Sel.getNumArgs().
    ObjCArgInfo *ArgInfo, DeclaratorChunk::ParamInfo *CParamInfo,
    unsigned CNumArgs, // c-style args
    const ParsedAttributesView &AttrList, tok::ObjCKeywordKind MethodDeclKind,
    bool isVariadic, bool MethodDefinition) {
  // Make sure we can establish a context for the method.
  if (!CurContext->isObjCContainer()) {
    Diag(MethodLoc, diag::err_missing_method_context);
    return nullptr;
  }
  Decl *ClassDecl = cast<ObjCContainerDecl>(CurContext);
  QualType resultDeclType;

  bool HasRelatedResultType = false;
  TypeSourceInfo *ReturnTInfo = nullptr;
  if (ReturnType) {
    resultDeclType = GetTypeFromParser(ReturnType, &ReturnTInfo);

    if (CheckFunctionReturnType(resultDeclType, MethodLoc))
      return nullptr;

    QualType bareResultType = resultDeclType;
    (void)AttributedType::stripOuterNullability(bareResultType);
    HasRelatedResultType = (bareResultType == Context.getObjCInstanceType());
  } else { // get the type for "id".
    resultDeclType = Context.getObjCIdType();
    Diag(MethodLoc, diag::warn_missing_method_return_type)
      << FixItHint::CreateInsertion(SelectorLocs.front(), "(id)");
  }

  ObjCMethodDecl *ObjCMethod = ObjCMethodDecl::Create(
      Context, MethodLoc, EndLoc, Sel, resultDeclType, ReturnTInfo, CurContext,
      MethodType == tok::minus, isVariadic,
      /*isPropertyAccessor=*/false,
      /*isImplicitlyDeclared=*/false, /*isDefined=*/false,
      MethodDeclKind == tok::objc_optional ? ObjCMethodDecl::Optional
                                           : ObjCMethodDecl::Required,
      HasRelatedResultType);

  SmallVector<ParmVarDecl*, 16> Params;

  for (unsigned i = 0, e = Sel.getNumArgs(); i != e; ++i) {
    QualType ArgType;
    TypeSourceInfo *DI;

    if (!ArgInfo[i].Type) {
      ArgType = Context.getObjCIdType();
      DI = nullptr;
    } else {
      ArgType = GetTypeFromParser(ArgInfo[i].Type, &DI);
    }

    LookupResult R(*this, ArgInfo[i].Name, ArgInfo[i].NameLoc,
                   LookupOrdinaryName, forRedeclarationInCurContext());
    LookupName(R, S);
    if (R.isSingleResult()) {
      NamedDecl *PrevDecl = R.getFoundDecl();
      if (S->isDeclScope(PrevDecl)) {
        Diag(ArgInfo[i].NameLoc,
             (MethodDefinition ? diag::warn_method_param_redefinition
                               : diag::warn_method_param_declaration))
          << ArgInfo[i].Name;
        Diag(PrevDecl->getLocation(),
             diag::note_previous_declaration);
      }
    }

    SourceLocation StartLoc = DI
      ? DI->getTypeLoc().getBeginLoc()
      : ArgInfo[i].NameLoc;

    ParmVarDecl* Param = CheckParameter(ObjCMethod, StartLoc,
                                        ArgInfo[i].NameLoc, ArgInfo[i].Name,
                                        ArgType, DI, SC_None);

    Param->setObjCMethodScopeInfo(i);

    Param->setObjCDeclQualifier(
      CvtQTToAstBitMask(ArgInfo[i].DeclSpec.getObjCDeclQualifier()));

    // Apply the attributes to the parameter.
    ProcessDeclAttributeList(TUScope, Param, ArgInfo[i].ArgAttrs);
    AddPragmaAttributes(TUScope, Param);

    if (Param->hasAttr<BlocksAttr>()) {
      Diag(Param->getLocation(), diag::err_block_on_nonlocal);
      Param->setInvalidDecl();
    }
    S->AddDecl(Param);
    IdResolver.AddDecl(Param);

    Params.push_back(Param);
  }

  for (unsigned i = 0, e = CNumArgs; i != e; ++i) {
    ParmVarDecl *Param = cast<ParmVarDecl>(CParamInfo[i].Param);
    QualType ArgType = Param->getType();
    if (ArgType.isNull())
      ArgType = Context.getObjCIdType();
    else
      // Perform the default array/function conversions (C99 6.7.5.3p[7,8]).
      ArgType = Context.getAdjustedParameterType(ArgType);

    Param->setDeclContext(ObjCMethod);
    Params.push_back(Param);
  }

  ObjCMethod->setMethodParams(Context, Params, SelectorLocs);
  ObjCMethod->setObjCDeclQualifier(
    CvtQTToAstBitMask(ReturnQT.getObjCDeclQualifier()));

  ProcessDeclAttributeList(TUScope, ObjCMethod, AttrList);
  AddPragmaAttributes(TUScope, ObjCMethod);

  // Add the method now.
  const ObjCMethodDecl *PrevMethod = nullptr;
  if (ObjCImplDecl *ImpDecl = dyn_cast<ObjCImplDecl>(ClassDecl)) {
    if (MethodType == tok::minus) {
      PrevMethod = ImpDecl->getInstanceMethod(Sel);
      ImpDecl->addInstanceMethod(ObjCMethod);
    } else {
      PrevMethod = ImpDecl->getClassMethod(Sel);
      ImpDecl->addClassMethod(ObjCMethod);
    }

    // Merge information from the @interface declaration into the
    // @implementation.
    if (ObjCInterfaceDecl *IDecl = ImpDecl->getClassInterface()) {
      if (auto *IMD = IDecl->lookupMethod(ObjCMethod->getSelector(),
                                          ObjCMethod->isInstanceMethod())) {
        mergeInterfaceMethodToImpl(*this, ObjCMethod, IMD);

        // Warn about defining -dealloc in a category.
        if (isa<ObjCCategoryImplDecl>(ImpDecl) && IMD->isOverriding() &&
            ObjCMethod->getSelector().getMethodFamily() == OMF_dealloc) {
          Diag(ObjCMethod->getLocation(), diag::warn_dealloc_in_category)
            << ObjCMethod->getDeclName();
        }
      }

      // Warn if a method declared in a protocol to which a category or
      // extension conforms is non-escaping and the implementation's method is
      // escaping.
      for (auto *C : IDecl->visible_categories())
        for (auto &P : C->protocols())
          if (auto *IMD = P->lookupMethod(ObjCMethod->getSelector(),
                                          ObjCMethod->isInstanceMethod())) {
            assert(ObjCMethod->parameters().size() ==
                       IMD->parameters().size() &&
                   "Methods have different number of parameters");
            auto OI = IMD->param_begin(), OE = IMD->param_end();
            auto NI = ObjCMethod->param_begin();
            for (; OI != OE; ++OI, ++NI)
              diagnoseNoescape(*NI, *OI, C, P, *this);
          }
    }
  } else {
    cast<DeclContext>(ClassDecl)->addDecl(ObjCMethod);
  }

  if (PrevMethod) {
    // You can never have two method definitions with the same name.
    Diag(ObjCMethod->getLocation(), diag::err_duplicate_method_decl)
      << ObjCMethod->getDeclName();
    Diag(PrevMethod->getLocation(), diag::note_previous_declaration);
    ObjCMethod->setInvalidDecl();
    return ObjCMethod;
  }

  // If this Objective-C method does not have a related result type, but we
  // are allowed to infer related result types, try to do so based on the
  // method family.
  ObjCInterfaceDecl *CurrentClass = dyn_cast<ObjCInterfaceDecl>(ClassDecl);
  if (!CurrentClass) {
    if (ObjCCategoryDecl *Cat = dyn_cast<ObjCCategoryDecl>(ClassDecl))
      CurrentClass = Cat->getClassInterface();
    else if (ObjCImplDecl *Impl = dyn_cast<ObjCImplDecl>(ClassDecl))
      CurrentClass = Impl->getClassInterface();
    else if (ObjCCategoryImplDecl *CatImpl
                                   = dyn_cast<ObjCCategoryImplDecl>(ClassDecl))
      CurrentClass = CatImpl->getClassInterface();
  }

  ResultTypeCompatibilityKind RTC
    = CheckRelatedResultTypeCompatibility(*this, ObjCMethod, CurrentClass);

  CheckObjCMethodOverrides(ObjCMethod, CurrentClass, RTC);

  bool ARCError = false;
  if (getLangOpts().ObjCAutoRefCount)
    ARCError = CheckARCMethodDecl(ObjCMethod);

  // Infer the related result type when possible.
  if (!ARCError && RTC == Sema::RTC_Compatible &&
      !ObjCMethod->hasRelatedResultType() &&
      LangOpts.ObjCInferRelatedResultType) {
    bool InferRelatedResultType = false;
    switch (ObjCMethod->getMethodFamily()) {
    case OMF_None:
    case OMF_copy:
    case OMF_dealloc:
    case OMF_finalize:
    case OMF_mutableCopy:
    case OMF_release:
    case OMF_retainCount:
    case OMF_initialize:
    case OMF_performSelector:
      break;

    case OMF_alloc:
    case OMF_new:
        InferRelatedResultType = ObjCMethod->isClassMethod();
      break;

    case OMF_init:
    case OMF_autorelease:
    case OMF_retain:
    case OMF_self:
      InferRelatedResultType = ObjCMethod->isInstanceMethod();
      break;
    }

    if (InferRelatedResultType &&
        !ObjCMethod->getReturnType()->isObjCIndependentClassType())
      ObjCMethod->setRelatedResultType();
  }

  if (MethodDefinition &&
      Context.getTargetInfo().getTriple().getArch() == llvm::Triple::x86)
    checkObjCMethodX86VectorTypes(*this, ObjCMethod);

  // + load method cannot have availability attributes. It get called on
  // startup, so it has to have the availability of the deployment target.
  if (const auto *attr = ObjCMethod->getAttr<AvailabilityAttr>()) {
    if (ObjCMethod->isClassMethod() &&
        ObjCMethod->getSelector().getAsString() == "load") {
      Diag(attr->getLocation(), diag::warn_availability_on_static_initializer)
          << 0;
      ObjCMethod->dropAttr<AvailabilityAttr>();
    }
  }

  ActOnDocumentableDecl(ObjCMethod);

  return ObjCMethod;
}

bool Sema::CheckObjCDeclScope(Decl *D) {
  // Following is also an error. But it is caused by a missing @end
  // and diagnostic is issued elsewhere.
  if (isa<ObjCContainerDecl>(CurContext->getRedeclContext()))
    return false;

  // If we switched context to translation unit while we are still lexically in
  // an objc container, it means the parser missed emitting an error.
  if (isa<TranslationUnitDecl>(getCurLexicalContext()->getRedeclContext()))
    return false;

  Diag(D->getLocation(), diag::err_objc_decls_may_only_appear_in_global_scope);
  D->setInvalidDecl();

  return true;
}

/// Called whenever \@defs(ClassName) is encountered in the source.  Inserts the
/// instance variables of ClassName into Decls.
void Sema::ActOnDefs(Scope *S, Decl *TagD, SourceLocation DeclStart,
                     IdentifierInfo *ClassName,
                     SmallVectorImpl<Decl*> &Decls) {
  // Check that ClassName is a valid class
  ObjCInterfaceDecl *Class = getObjCInterfaceDecl(ClassName, DeclStart);
  if (!Class) {
    Diag(DeclStart, diag::err_undef_interface) << ClassName;
    return;
  }
  if (LangOpts.ObjCRuntime.isNonFragile()) {
    Diag(DeclStart, diag::err_atdef_nonfragile_interface);
    return;
  }

  // Collect the instance variables
  SmallVector<const ObjCIvarDecl*, 32> Ivars;
  Context.DeepCollectObjCIvars(Class, true, Ivars);
  // For each ivar, create a fresh ObjCAtDefsFieldDecl.
  for (unsigned i = 0; i < Ivars.size(); i++) {
    const FieldDecl* ID = Ivars[i];
    RecordDecl *Record = dyn_cast<RecordDecl>(TagD);
    Decl *FD = ObjCAtDefsFieldDecl::Create(Context, Record,
                                           /*FIXME: StartL=*/ID->getLocation(),
                                           ID->getLocation(),
                                           ID->getIdentifier(), ID->getType(),
                                           ID->getBitWidth());
    Decls.push_back(FD);
  }

  // Introduce all of these fields into the appropriate scope.
  for (SmallVectorImpl<Decl*>::iterator D = Decls.begin();
       D != Decls.end(); ++D) {
    FieldDecl *FD = cast<FieldDecl>(*D);
    if (getLangOpts().CPlusPlus)
      PushOnScopeChains(FD, S);
    else if (RecordDecl *Record = dyn_cast<RecordDecl>(TagD))
      Record->addDecl(FD);
  }
}

/// Build a type-check a new Objective-C exception variable declaration.
VarDecl *Sema::BuildObjCExceptionDecl(TypeSourceInfo *TInfo, QualType T,
                                      SourceLocation StartLoc,
                                      SourceLocation IdLoc,
                                      IdentifierInfo *Id,
                                      bool Invalid) {
  // ISO/IEC TR 18037 S6.7.3: "The type of an object with automatic storage
  // duration shall not be qualified by an address-space qualifier."
  // Since all parameters have automatic store duration, they can not have
  // an address space.
  if (T.getAddressSpace() != LangAS::Default) {
    Diag(IdLoc, diag::err_arg_with_address_space);
    Invalid = true;
  }

  // An @catch parameter must be an unqualified object pointer type;
  // FIXME: Recover from "NSObject foo" by inserting the * in "NSObject *foo"?
  if (Invalid) {
    // Don't do any further checking.
  } else if (T->isDependentType()) {
    // Okay: we don't know what this type will instantiate to.
  } else if (T->isObjCQualifiedIdType()) {
    Invalid = true;
    Diag(IdLoc, diag::err_illegal_qualifiers_on_catch_parm);
  } else if (T->isObjCIdType()) {
    // Okay: we don't know what this type will instantiate to.
  } else if (!T->isObjCObjectPointerType()) {
    Invalid = true;
    Diag(IdLoc, diag::err_catch_param_not_objc_type);
  } else if (!T->getAs<ObjCObjectPointerType>()->getInterfaceType()) {
    Invalid = true;
    Diag(IdLoc, diag::err_catch_param_not_objc_type);
  }

  VarDecl *New = VarDecl::Create(Context, CurContext, StartLoc, IdLoc, Id,
                                 T, TInfo, SC_None);
  New->setExceptionVariable(true);

  // In ARC, infer 'retaining' for variables of retainable type.
  if (getLangOpts().ObjCAutoRefCount && inferObjCARCLifetime(New))
    Invalid = true;

  if (Invalid)
    New->setInvalidDecl();
  return New;
}

Decl *Sema::ActOnObjCExceptionDecl(Scope *S, Declarator &D) {
  const DeclSpec &DS = D.getDeclSpec();

  // We allow the "register" storage class on exception variables because
  // GCC did, but we drop it completely. Any other storage class is an error.
  if (DS.getStorageClassSpec() == DeclSpec::SCS_register) {
    Diag(DS.getStorageClassSpecLoc(), diag::warn_register_objc_catch_parm)
      << FixItHint::CreateRemoval(SourceRange(DS.getStorageClassSpecLoc()));
  } else if (DeclSpec::SCS SCS = DS.getStorageClassSpec()) {
    Diag(DS.getStorageClassSpecLoc(), diag::err_storage_spec_on_catch_parm)
      << DeclSpec::getSpecifierName(SCS);
  }
  if (DS.isInlineSpecified())
    Diag(DS.getInlineSpecLoc(), diag::err_inline_non_function)
        << getLangOpts().CPlusPlus17;
  if (DeclSpec::TSCS TSCS = D.getDeclSpec().getThreadStorageClassSpec())
    Diag(D.getDeclSpec().getThreadStorageClassSpecLoc(),
         diag::err_invalid_thread)
     << DeclSpec::getSpecifierName(TSCS);
  D.getMutableDeclSpec().ClearStorageClassSpecs();

  DiagnoseFunctionSpecifiers(D.getDeclSpec());

  // Check that there are no default arguments inside the type of this
  // exception object (C++ only).
  if (getLangOpts().CPlusPlus)
    CheckExtraCXXDefaultArguments(D);

  TypeSourceInfo *TInfo = GetTypeForDeclarator(D, S);
  QualType ExceptionType = TInfo->getType();

  VarDecl *New = BuildObjCExceptionDecl(TInfo, ExceptionType,
                                        D.getSourceRange().getBegin(),
                                        D.getIdentifierLoc(),
                                        D.getIdentifier(),
                                        D.isInvalidType());

  // Parameter declarators cannot be qualified (C++ [dcl.meaning]p1).
  if (D.getCXXScopeSpec().isSet()) {
    Diag(D.getIdentifierLoc(), diag::err_qualified_objc_catch_parm)
      << D.getCXXScopeSpec().getRange();
    New->setInvalidDecl();
  }

  // Add the parameter declaration into this scope.
  S->AddDecl(New);
  if (D.getIdentifier())
    IdResolver.AddDecl(New);

  ProcessDeclAttributes(S, New, D);

  if (New->hasAttr<BlocksAttr>())
    Diag(New->getLocation(), diag::err_block_on_nonlocal);
  return New;
}

/// CollectIvarsToConstructOrDestruct - Collect those ivars which require
/// initialization.
void Sema::CollectIvarsToConstructOrDestruct(ObjCInterfaceDecl *OI,
                                SmallVectorImpl<ObjCIvarDecl*> &Ivars) {
  for (ObjCIvarDecl *Iv = OI->all_declared_ivar_begin(); Iv;
       Iv= Iv->getNextIvar()) {
    QualType QT = Context.getBaseElementType(Iv->getType());
    if (QT->isRecordType())
      Ivars.push_back(Iv);
  }
}

void Sema::DiagnoseUseOfUnimplementedSelectors() {
  // Load referenced selectors from the external source.
  if (ExternalSource) {
    SmallVector<std::pair<Selector, SourceLocation>, 4> Sels;
    ExternalSource->ReadReferencedSelectors(Sels);
    for (unsigned I = 0, N = Sels.size(); I != N; ++I)
      ReferencedSelectors[Sels[I].first] = Sels[I].second;
  }

  // Warning will be issued only when selector table is
  // generated (which means there is at lease one implementation
  // in the TU). This is to match gcc's behavior.
  if (ReferencedSelectors.empty() ||
      !Context.AnyObjCImplementation())
    return;
  for (auto &SelectorAndLocation : ReferencedSelectors) {
    Selector Sel = SelectorAndLocation.first;
    SourceLocation Loc = SelectorAndLocation.second;
    if (!LookupImplementedMethodInGlobalPool(Sel))
      Diag(Loc, diag::warn_unimplemented_selector) << Sel;
  }
}

ObjCIvarDecl *
Sema::GetIvarBackingPropertyAccessor(const ObjCMethodDecl *Method,
                                     const ObjCPropertyDecl *&PDecl) const {
  if (Method->isClassMethod())
    return nullptr;
  const ObjCInterfaceDecl *IDecl = Method->getClassInterface();
  if (!IDecl)
    return nullptr;
  Method = IDecl->lookupMethod(Method->getSelector(), /*isInstance=*/true,
                               /*shallowCategoryLookup=*/false,
                               /*followSuper=*/false);
  if (!Method || !Method->isPropertyAccessor())
    return nullptr;
  if ((PDecl = Method->findPropertyDecl()))
    if (ObjCIvarDecl *IV = PDecl->getPropertyIvarDecl()) {
      // property backing ivar must belong to property's class
      // or be a private ivar in class's implementation.
      // FIXME. fix the const-ness issue.
      IV = const_cast<ObjCInterfaceDecl *>(IDecl)->lookupInstanceVariable(
                                                        IV->getIdentifier());
      return IV;
    }
  return nullptr;
}

namespace {
  /// Used by Sema::DiagnoseUnusedBackingIvarInAccessor to check if a property
  /// accessor references the backing ivar.
  class UnusedBackingIvarChecker :
      public RecursiveASTVisitor<UnusedBackingIvarChecker> {
  public:
    Sema &S;
    const ObjCMethodDecl *Method;
    const ObjCIvarDecl *IvarD;
    bool AccessedIvar;
    bool InvokedSelfMethod;

    UnusedBackingIvarChecker(Sema &S, const ObjCMethodDecl *Method,
                             const ObjCIvarDecl *IvarD)
      : S(S), Method(Method), IvarD(IvarD),
        AccessedIvar(false), InvokedSelfMethod(false) {
      assert(IvarD);
    }

    bool VisitObjCIvarRefExpr(ObjCIvarRefExpr *E) {
      if (E->getDecl() == IvarD) {
        AccessedIvar = true;
        return false;
      }
      return true;
    }

    bool VisitObjCMessageExpr(ObjCMessageExpr *E) {
      if (E->getReceiverKind() == ObjCMessageExpr::Instance &&
          S.isSelfExpr(E->getInstanceReceiver(), Method)) {
        InvokedSelfMethod = true;
      }
      return true;
    }
  };
} // end anonymous namespace

void Sema::DiagnoseUnusedBackingIvarInAccessor(Scope *S,
                                          const ObjCImplementationDecl *ImplD) {
  if (S->hasUnrecoverableErrorOccurred())
    return;

  for (const auto *CurMethod : ImplD->instance_methods()) {
    unsigned DIAG = diag::warn_unused_property_backing_ivar;
    SourceLocation Loc = CurMethod->getLocation();
    if (Diags.isIgnored(DIAG, Loc))
      continue;

    const ObjCPropertyDecl *PDecl;
    const ObjCIvarDecl *IV = GetIvarBackingPropertyAccessor(CurMethod, PDecl);
    if (!IV)
      continue;

    UnusedBackingIvarChecker Checker(*this, CurMethod, IV);
    Checker.TraverseStmt(CurMethod->getBody());
    if (Checker.AccessedIvar)
      continue;

    // Do not issue this warning if backing ivar is used somewhere and accessor
    // implementation makes a self call. This is to prevent false positive in
    // cases where the ivar is accessed by another method that the accessor
    // delegates to.
    if (!IV->isReferenced() || !Checker.InvokedSelfMethod) {
      Diag(Loc, DIAG) << IV;
      Diag(PDecl->getLocation(), diag::note_property_declare);
    }
  }
}
