//===- Decl.cpp - Declaration AST Node Implementation ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Decl subclasses.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/Decl.h"
#include "Linkage.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTDiagnostic.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/ASTMutationListener.h"
#include "clang/AST/CanonicalType.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclOpenMP.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExternalASTSource.h"
#include "clang/AST/ODRHash.h"
#include "clang/AST/PrettyDeclStackTrace.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/Redeclarable.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/TemplateBase.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/Linkage.h"
#include "clang/Basic/Module.h"
#include "clang/Basic/PartialDiagnostic.h"
#include "clang/Basic/SanitizerBlacklist.h"
#include "clang/Basic/Sanitizers.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Basic/TargetCXXABI.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/Visibility.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>

using namespace clang;

Decl *clang::getPrimaryMergedDecl(Decl *D) {
  return D->getASTContext().getPrimaryMergedDecl(D);
}

void PrettyDeclStackTraceEntry::print(raw_ostream &OS) const {
  SourceLocation Loc = this->Loc;
  if (!Loc.isValid() && TheDecl) Loc = TheDecl->getLocation();
  if (Loc.isValid()) {
    Loc.print(OS, Context.getSourceManager());
    OS << ": ";
  }
  OS << Message;

  if (auto *ND = dyn_cast_or_null<NamedDecl>(TheDecl)) {
    OS << " '";
    ND->getNameForDiagnostic(OS, Context.getPrintingPolicy(), true);
    OS << "'";
  }

  OS << '\n';
}

// Defined here so that it can be inlined into its direct callers.
bool Decl::isOutOfLine() const {
  return !getLexicalDeclContext()->Equals(getDeclContext());
}

TranslationUnitDecl::TranslationUnitDecl(ASTContext &ctx)
    : Decl(TranslationUnit, nullptr, SourceLocation()),
      DeclContext(TranslationUnit), Ctx(ctx) {}

//===----------------------------------------------------------------------===//
// NamedDecl Implementation
//===----------------------------------------------------------------------===//

// Visibility rules aren't rigorously externally specified, but here
// are the basic principles behind what we implement:
//
// 1. An explicit visibility attribute is generally a direct expression
// of the user's intent and should be honored.  Only the innermost
// visibility attribute applies.  If no visibility attribute applies,
// global visibility settings are considered.
//
// 2. There is one caveat to the above: on or in a template pattern,
// an explicit visibility attribute is just a default rule, and
// visibility can be decreased by the visibility of template
// arguments.  But this, too, has an exception: an attribute on an
// explicit specialization or instantiation causes all the visibility
// restrictions of the template arguments to be ignored.
//
// 3. A variable that does not otherwise have explicit visibility can
// be restricted by the visibility of its type.
//
// 4. A visibility restriction is explicit if it comes from an
// attribute (or something like it), not a global visibility setting.
// When emitting a reference to an external symbol, visibility
// restrictions are ignored unless they are explicit.
//
// 5. When computing the visibility of a non-type, including a
// non-type member of a class, only non-type visibility restrictions
// are considered: the 'visibility' attribute, global value-visibility
// settings, and a few special cases like __private_extern.
//
// 6. When computing the visibility of a type, including a type member
// of a class, only type visibility restrictions are considered:
// the 'type_visibility' attribute and global type-visibility settings.
// However, a 'visibility' attribute counts as a 'type_visibility'
// attribute on any declaration that only has the former.
//
// The visibility of a "secondary" entity, like a template argument,
// is computed using the kind of that entity, not the kind of the
// primary entity for which we are computing visibility.  For example,
// the visibility of a specialization of either of these templates:
//   template <class T, bool (&compare)(T, X)> bool has_match(list<T>, X);
//   template <class T, bool (&compare)(T, X)> class matcher;
// is restricted according to the type visibility of the argument 'T',
// the type visibility of 'bool(&)(T,X)', and the value visibility of
// the argument function 'compare'.  That 'has_match' is a value
// and 'matcher' is a type only matters when looking for attributes
// and settings from the immediate context.

/// Does this computation kind permit us to consider additional
/// visibility settings from attributes and the like?
static bool hasExplicitVisibilityAlready(LVComputationKind computation) {
  return computation.IgnoreExplicitVisibility;
}

/// Given an LVComputationKind, return one of the same type/value sort
/// that records that it already has explicit visibility.
static LVComputationKind
withExplicitVisibilityAlready(LVComputationKind Kind) {
  Kind.IgnoreExplicitVisibility = true;
  return Kind;
}

static Optional<Visibility> getExplicitVisibility(const NamedDecl *D,
                                                  LVComputationKind kind) {
  assert(!kind.IgnoreExplicitVisibility &&
         "asking for explicit visibility when we shouldn't be");
  return D->getExplicitVisibility(kind.getExplicitVisibilityKind());
}

/// Is the given declaration a "type" or a "value" for the purposes of
/// visibility computation?
static bool usesTypeVisibility(const NamedDecl *D) {
  return isa<TypeDecl>(D) ||
         isa<ClassTemplateDecl>(D) ||
         isa<ObjCInterfaceDecl>(D);
}

/// Does the given declaration have member specialization information,
/// and if so, is it an explicit specialization?
template <class T> static typename
std::enable_if<!std::is_base_of<RedeclarableTemplateDecl, T>::value, bool>::type
isExplicitMemberSpecialization(const T *D) {
  if (const MemberSpecializationInfo *member =
        D->getMemberSpecializationInfo()) {
    return member->isExplicitSpecialization();
  }
  return false;
}

/// For templates, this question is easier: a member template can't be
/// explicitly instantiated, so there's a single bit indicating whether
/// or not this is an explicit member specialization.
static bool isExplicitMemberSpecialization(const RedeclarableTemplateDecl *D) {
  return D->isMemberSpecialization();
}

/// Given a visibility attribute, return the explicit visibility
/// associated with it.
template <class T>
static Visibility getVisibilityFromAttr(const T *attr) {
  switch (attr->getVisibility()) {
  case T::Default:
    return DefaultVisibility;
  case T::Hidden:
    return HiddenVisibility;
  case T::Protected:
    return ProtectedVisibility;
  }
  llvm_unreachable("bad visibility kind");
}

/// Return the explicit visibility of the given declaration.
static Optional<Visibility> getVisibilityOf(const NamedDecl *D,
                                    NamedDecl::ExplicitVisibilityKind kind) {
  // If we're ultimately computing the visibility of a type, look for
  // a 'type_visibility' attribute before looking for 'visibility'.
  if (kind == NamedDecl::VisibilityForType) {
    if (const auto *A = D->getAttr<TypeVisibilityAttr>()) {
      return getVisibilityFromAttr(A);
    }
  }

  // If this declaration has an explicit visibility attribute, use it.
  if (const auto *A = D->getAttr<VisibilityAttr>()) {
    return getVisibilityFromAttr(A);
  }

  return None;
}

LinkageInfo LinkageComputer::getLVForType(const Type &T,
                                          LVComputationKind computation) {
  if (computation.IgnoreAllVisibility)
    return LinkageInfo(T.getLinkage(), DefaultVisibility, true);
  return getTypeLinkageAndVisibility(&T);
}

/// Get the most restrictive linkage for the types in the given
/// template parameter list.  For visibility purposes, template
/// parameters are part of the signature of a template.
LinkageInfo LinkageComputer::getLVForTemplateParameterList(
    const TemplateParameterList *Params, LVComputationKind computation) {
  LinkageInfo LV;
  for (const NamedDecl *P : *Params) {
    // Template type parameters are the most common and never
    // contribute to visibility, pack or not.
    if (isa<TemplateTypeParmDecl>(P))
      continue;

    // Non-type template parameters can be restricted by the value type, e.g.
    //   template <enum X> class A { ... };
    // We have to be careful here, though, because we can be dealing with
    // dependent types.
    if (const auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(P)) {
      // Handle the non-pack case first.
      if (!NTTP->isExpandedParameterPack()) {
        if (!NTTP->getType()->isDependentType()) {
          LV.merge(getLVForType(*NTTP->getType(), computation));
        }
        continue;
      }

      // Look at all the types in an expanded pack.
      for (unsigned i = 0, n = NTTP->getNumExpansionTypes(); i != n; ++i) {
        QualType type = NTTP->getExpansionType(i);
        if (!type->isDependentType())
          LV.merge(getTypeLinkageAndVisibility(type));
      }
      continue;
    }

    // Template template parameters can be restricted by their
    // template parameters, recursively.
    const auto *TTP = cast<TemplateTemplateParmDecl>(P);

    // Handle the non-pack case first.
    if (!TTP->isExpandedParameterPack()) {
      LV.merge(getLVForTemplateParameterList(TTP->getTemplateParameters(),
                                             computation));
      continue;
    }

    // Look at all expansions in an expanded pack.
    for (unsigned i = 0, n = TTP->getNumExpansionTemplateParameters();
           i != n; ++i) {
      LV.merge(getLVForTemplateParameterList(
          TTP->getExpansionTemplateParameters(i), computation));
    }
  }

  return LV;
}

static const Decl *getOutermostFuncOrBlockContext(const Decl *D) {
  const Decl *Ret = nullptr;
  const DeclContext *DC = D->getDeclContext();
  while (DC->getDeclKind() != Decl::TranslationUnit) {
    if (isa<FunctionDecl>(DC) || isa<BlockDecl>(DC))
      Ret = cast<Decl>(DC);
    DC = DC->getParent();
  }
  return Ret;
}

/// Get the most restrictive linkage for the types and
/// declarations in the given template argument list.
///
/// Note that we don't take an LVComputationKind because we always
/// want to honor the visibility of template arguments in the same way.
LinkageInfo
LinkageComputer::getLVForTemplateArgumentList(ArrayRef<TemplateArgument> Args,
                                              LVComputationKind computation) {
  LinkageInfo LV;

  for (const TemplateArgument &Arg : Args) {
    switch (Arg.getKind()) {
    case TemplateArgument::Null:
    case TemplateArgument::Integral:
    case TemplateArgument::Expression:
      continue;

    case TemplateArgument::Type:
      LV.merge(getLVForType(*Arg.getAsType(), computation));
      continue;

    case TemplateArgument::Declaration: {
      const NamedDecl *ND = Arg.getAsDecl();
      assert(!usesTypeVisibility(ND));
      LV.merge(getLVForDecl(ND, computation));
      continue;
    }

    case TemplateArgument::NullPtr:
      LV.merge(getTypeLinkageAndVisibility(Arg.getNullPtrType()));
      continue;

    case TemplateArgument::Template:
    case TemplateArgument::TemplateExpansion:
      if (TemplateDecl *Template =
              Arg.getAsTemplateOrTemplatePattern().getAsTemplateDecl())
        LV.merge(getLVForDecl(Template, computation));
      continue;

    case TemplateArgument::Pack:
      LV.merge(getLVForTemplateArgumentList(Arg.getPackAsArray(), computation));
      continue;
    }
    llvm_unreachable("bad template argument kind");
  }

  return LV;
}

LinkageInfo
LinkageComputer::getLVForTemplateArgumentList(const TemplateArgumentList &TArgs,
                                              LVComputationKind computation) {
  return getLVForTemplateArgumentList(TArgs.asArray(), computation);
}

static bool shouldConsiderTemplateVisibility(const FunctionDecl *fn,
                        const FunctionTemplateSpecializationInfo *specInfo) {
  // Include visibility from the template parameters and arguments
  // only if this is not an explicit instantiation or specialization
  // with direct explicit visibility.  (Implicit instantiations won't
  // have a direct attribute.)
  if (!specInfo->isExplicitInstantiationOrSpecialization())
    return true;

  return !fn->hasAttr<VisibilityAttr>();
}

/// Merge in template-related linkage and visibility for the given
/// function template specialization.
///
/// We don't need a computation kind here because we can assume
/// LVForValue.
///
/// \param[out] LV the computation to use for the parent
void LinkageComputer::mergeTemplateLV(
    LinkageInfo &LV, const FunctionDecl *fn,
    const FunctionTemplateSpecializationInfo *specInfo,
    LVComputationKind computation) {
  bool considerVisibility =
    shouldConsiderTemplateVisibility(fn, specInfo);

  // Merge information from the template parameters.
  FunctionTemplateDecl *temp = specInfo->getTemplate();
  LinkageInfo tempLV =
    getLVForTemplateParameterList(temp->getTemplateParameters(), computation);
  LV.mergeMaybeWithVisibility(tempLV, considerVisibility);

  // Merge information from the template arguments.
  const TemplateArgumentList &templateArgs = *specInfo->TemplateArguments;
  LinkageInfo argsLV = getLVForTemplateArgumentList(templateArgs, computation);
  LV.mergeMaybeWithVisibility(argsLV, considerVisibility);
}

/// Does the given declaration have a direct visibility attribute
/// that would match the given rules?
static bool hasDirectVisibilityAttribute(const NamedDecl *D,
                                         LVComputationKind computation) {
  if (computation.IgnoreAllVisibility)
    return false;

  return (computation.isTypeVisibility() && D->hasAttr<TypeVisibilityAttr>()) ||
         D->hasAttr<VisibilityAttr>();
}

/// Should we consider visibility associated with the template
/// arguments and parameters of the given class template specialization?
static bool shouldConsiderTemplateVisibility(
                                 const ClassTemplateSpecializationDecl *spec,
                                 LVComputationKind computation) {
  // Include visibility from the template parameters and arguments
  // only if this is not an explicit instantiation or specialization
  // with direct explicit visibility (and note that implicit
  // instantiations won't have a direct attribute).
  //
  // Furthermore, we want to ignore template parameters and arguments
  // for an explicit specialization when computing the visibility of a
  // member thereof with explicit visibility.
  //
  // This is a bit complex; let's unpack it.
  //
  // An explicit class specialization is an independent, top-level
  // declaration.  As such, if it or any of its members has an
  // explicit visibility attribute, that must directly express the
  // user's intent, and we should honor it.  The same logic applies to
  // an explicit instantiation of a member of such a thing.

  // Fast path: if this is not an explicit instantiation or
  // specialization, we always want to consider template-related
  // visibility restrictions.
  if (!spec->isExplicitInstantiationOrSpecialization())
    return true;

  // This is the 'member thereof' check.
  if (spec->isExplicitSpecialization() &&
      hasExplicitVisibilityAlready(computation))
    return false;

  return !hasDirectVisibilityAttribute(spec, computation);
}

/// Merge in template-related linkage and visibility for the given
/// class template specialization.
void LinkageComputer::mergeTemplateLV(
    LinkageInfo &LV, const ClassTemplateSpecializationDecl *spec,
    LVComputationKind computation) {
  bool considerVisibility = shouldConsiderTemplateVisibility(spec, computation);

  // Merge information from the template parameters, but ignore
  // visibility if we're only considering template arguments.

  ClassTemplateDecl *temp = spec->getSpecializedTemplate();
  LinkageInfo tempLV =
    getLVForTemplateParameterList(temp->getTemplateParameters(), computation);
  LV.mergeMaybeWithVisibility(tempLV,
           considerVisibility && !hasExplicitVisibilityAlready(computation));

  // Merge information from the template arguments.  We ignore
  // template-argument visibility if we've got an explicit
  // instantiation with a visibility attribute.
  const TemplateArgumentList &templateArgs = spec->getTemplateArgs();
  LinkageInfo argsLV = getLVForTemplateArgumentList(templateArgs, computation);
  if (considerVisibility)
    LV.mergeVisibility(argsLV);
  LV.mergeExternalVisibility(argsLV);
}

/// Should we consider visibility associated with the template
/// arguments and parameters of the given variable template
/// specialization? As usual, follow class template specialization
/// logic up to initialization.
static bool shouldConsiderTemplateVisibility(
                                 const VarTemplateSpecializationDecl *spec,
                                 LVComputationKind computation) {
  // Include visibility from the template parameters and arguments
  // only if this is not an explicit instantiation or specialization
  // with direct explicit visibility (and note that implicit
  // instantiations won't have a direct attribute).
  if (!spec->isExplicitInstantiationOrSpecialization())
    return true;

  // An explicit variable specialization is an independent, top-level
  // declaration.  As such, if it has an explicit visibility attribute,
  // that must directly express the user's intent, and we should honor
  // it.
  if (spec->isExplicitSpecialization() &&
      hasExplicitVisibilityAlready(computation))
    return false;

  return !hasDirectVisibilityAttribute(spec, computation);
}

/// Merge in template-related linkage and visibility for the given
/// variable template specialization. As usual, follow class template
/// specialization logic up to initialization.
void LinkageComputer::mergeTemplateLV(LinkageInfo &LV,
                                      const VarTemplateSpecializationDecl *spec,
                                      LVComputationKind computation) {
  bool considerVisibility = shouldConsiderTemplateVisibility(spec, computation);

  // Merge information from the template parameters, but ignore
  // visibility if we're only considering template arguments.

  VarTemplateDecl *temp = spec->getSpecializedTemplate();
  LinkageInfo tempLV =
    getLVForTemplateParameterList(temp->getTemplateParameters(), computation);
  LV.mergeMaybeWithVisibility(tempLV,
           considerVisibility && !hasExplicitVisibilityAlready(computation));

  // Merge information from the template arguments.  We ignore
  // template-argument visibility if we've got an explicit
  // instantiation with a visibility attribute.
  const TemplateArgumentList &templateArgs = spec->getTemplateArgs();
  LinkageInfo argsLV = getLVForTemplateArgumentList(templateArgs, computation);
  if (considerVisibility)
    LV.mergeVisibility(argsLV);
  LV.mergeExternalVisibility(argsLV);
}

static bool useInlineVisibilityHidden(const NamedDecl *D) {
  // FIXME: we should warn if -fvisibility-inlines-hidden is used with c.
  const LangOptions &Opts = D->getASTContext().getLangOpts();
  if (!Opts.CPlusPlus || !Opts.InlineVisibilityHidden)
    return false;

  const auto *FD = dyn_cast<FunctionDecl>(D);
  if (!FD)
    return false;

  TemplateSpecializationKind TSK = TSK_Undeclared;
  if (FunctionTemplateSpecializationInfo *spec
      = FD->getTemplateSpecializationInfo()) {
    TSK = spec->getTemplateSpecializationKind();
  } else if (MemberSpecializationInfo *MSI =
             FD->getMemberSpecializationInfo()) {
    TSK = MSI->getTemplateSpecializationKind();
  }

  const FunctionDecl *Def = nullptr;
  // InlineVisibilityHidden only applies to definitions, and
  // isInlined() only gives meaningful answers on definitions
  // anyway.
  return TSK != TSK_ExplicitInstantiationDeclaration &&
    TSK != TSK_ExplicitInstantiationDefinition &&
    FD->hasBody(Def) && Def->isInlined() && !Def->hasAttr<GNUInlineAttr>();
}

template <typename T> static bool isFirstInExternCContext(T *D) {
  const T *First = D->getFirstDecl();
  return First->isInExternCContext();
}

static bool isSingleLineLanguageLinkage(const Decl &D) {
  if (const auto *SD = dyn_cast<LinkageSpecDecl>(D.getDeclContext()))
    if (!SD->hasBraces())
      return true;
  return false;
}

static bool isExportedFromModuleIntefaceUnit(const NamedDecl *D) {
  // FIXME: Handle isModulePrivate.
  switch (D->getModuleOwnershipKind()) {
  case Decl::ModuleOwnershipKind::Unowned:
  case Decl::ModuleOwnershipKind::ModulePrivate:
    return false;
  case Decl::ModuleOwnershipKind::Visible:
  case Decl::ModuleOwnershipKind::VisibleWhenImported:
    if (auto *M = D->getOwningModule())
      return M->Kind == Module::ModuleInterfaceUnit;
  }
  llvm_unreachable("unexpected module ownership kind");
}

static LinkageInfo getInternalLinkageFor(const NamedDecl *D) {
  // Internal linkage declarations within a module interface unit are modeled
  // as "module-internal linkage", which means that they have internal linkage
  // formally but can be indirectly accessed from outside the module via inline
  // functions and templates defined within the module.
  if (auto *M = D->getOwningModule())
    if (M->Kind == Module::ModuleInterfaceUnit)
      return LinkageInfo(ModuleInternalLinkage, DefaultVisibility, false);

  return LinkageInfo::internal();
}

static LinkageInfo getExternalLinkageFor(const NamedDecl *D) {
  // C++ Modules TS [basic.link]/6.8:
  //   - A name declared at namespace scope that does not have internal linkage
  //     by the previous rules and that is introduced by a non-exported
  //     declaration has module linkage.
  if (auto *M = D->getOwningModule())
    if (M->Kind == Module::ModuleInterfaceUnit)
      if (!isExportedFromModuleIntefaceUnit(
              cast<NamedDecl>(D->getCanonicalDecl())))
        return LinkageInfo(ModuleLinkage, DefaultVisibility, false);

  return LinkageInfo::external();
}

LinkageInfo
LinkageComputer::getLVForNamespaceScopeDecl(const NamedDecl *D,
                                            LVComputationKind computation,
                                            bool IgnoreVarTypeLinkage) {
  assert(D->getDeclContext()->getRedeclContext()->isFileContext() &&
         "Not a name having namespace scope");
  ASTContext &Context = D->getASTContext();

  // C++ [basic.link]p3:
  //   A name having namespace scope (3.3.6) has internal linkage if it
  //   is the name of
  //     - an object, reference, function or function template that is
  //       explicitly declared static; or,
  // (This bullet corresponds to C99 6.2.2p3.)
  if (const auto *Var = dyn_cast<VarDecl>(D)) {
    // Explicitly declared static.
    if (Var->getStorageClass() == SC_Static)
      return getInternalLinkageFor(Var);

    // - a non-inline, non-volatile object or reference that is explicitly
    //   declared const or constexpr and neither explicitly declared extern
    //   nor previously declared to have external linkage; or (there is no
    //   equivalent in C99)
    // The C++ modules TS adds "non-exported" to this list.
    if (Context.getLangOpts().CPlusPlus &&
        Var->getType().isConstQualified() &&
        !Var->getType().isVolatileQualified() &&
        !Var->isInline() &&
        !isExportedFromModuleIntefaceUnit(Var)) {
      const VarDecl *PrevVar = Var->getPreviousDecl();
      if (PrevVar)
        return getLVForDecl(PrevVar, computation);

      if (Var->getStorageClass() != SC_Extern &&
          Var->getStorageClass() != SC_PrivateExtern &&
          !isSingleLineLanguageLinkage(*Var))
        return getInternalLinkageFor(Var);
    }

    for (const VarDecl *PrevVar = Var->getPreviousDecl(); PrevVar;
         PrevVar = PrevVar->getPreviousDecl()) {
      if (PrevVar->getStorageClass() == SC_PrivateExtern &&
          Var->getStorageClass() == SC_None)
        return getDeclLinkageAndVisibility(PrevVar);
      // Explicitly declared static.
      if (PrevVar->getStorageClass() == SC_Static)
        return getInternalLinkageFor(Var);
    }
  } else if (const FunctionDecl *Function = D->getAsFunction()) {
    // C++ [temp]p4:
    //   A non-member function template can have internal linkage; any
    //   other template name shall have external linkage.

    // Explicitly declared static.
    if (Function->getCanonicalDecl()->getStorageClass() == SC_Static)
      return getInternalLinkageFor(Function);
  } else if (const auto *IFD = dyn_cast<IndirectFieldDecl>(D)) {
    //   - a data member of an anonymous union.
    const VarDecl *VD = IFD->getVarDecl();
    assert(VD && "Expected a VarDecl in this IndirectFieldDecl!");
    return getLVForNamespaceScopeDecl(VD, computation, IgnoreVarTypeLinkage);
  }
  assert(!isa<FieldDecl>(D) && "Didn't expect a FieldDecl!");

  if (D->isInAnonymousNamespace()) {
    const auto *Var = dyn_cast<VarDecl>(D);
    const auto *Func = dyn_cast<FunctionDecl>(D);
    // FIXME: The check for extern "C" here is not justified by the standard
    // wording, but we retain it from the pre-DR1113 model to avoid breaking
    // code.
    //
    // C++11 [basic.link]p4:
    //   An unnamed namespace or a namespace declared directly or indirectly
    //   within an unnamed namespace has internal linkage.
    if ((!Var || !isFirstInExternCContext(Var)) &&
        (!Func || !isFirstInExternCContext(Func)))
      return getInternalLinkageFor(D);
  }

  // Set up the defaults.

  // C99 6.2.2p5:
  //   If the declaration of an identifier for an object has file
  //   scope and no storage-class specifier, its linkage is
  //   external.
  LinkageInfo LV = getExternalLinkageFor(D);

  if (!hasExplicitVisibilityAlready(computation)) {
    if (Optional<Visibility> Vis = getExplicitVisibility(D, computation)) {
      LV.mergeVisibility(*Vis, true);
    } else {
      // If we're declared in a namespace with a visibility attribute,
      // use that namespace's visibility, and it still counts as explicit.
      for (const DeclContext *DC = D->getDeclContext();
           !isa<TranslationUnitDecl>(DC);
           DC = DC->getParent()) {
        const auto *ND = dyn_cast<NamespaceDecl>(DC);
        if (!ND) continue;
        if (Optional<Visibility> Vis = getExplicitVisibility(ND, computation)) {
          LV.mergeVisibility(*Vis, true);
          break;
        }
      }
    }

    // Add in global settings if the above didn't give us direct visibility.
    if (!LV.isVisibilityExplicit()) {
      // Use global type/value visibility as appropriate.
      Visibility globalVisibility =
          computation.isValueVisibility()
              ? Context.getLangOpts().getValueVisibilityMode()
              : Context.getLangOpts().getTypeVisibilityMode();
      LV.mergeVisibility(globalVisibility, /*explicit*/ false);

      // If we're paying attention to global visibility, apply
      // -finline-visibility-hidden if this is an inline method.
      if (useInlineVisibilityHidden(D))
        LV.mergeVisibility(HiddenVisibility, /*visibilityExplicit=*/false);
    }
  }

  // C++ [basic.link]p4:

  //   A name having namespace scope has external linkage if it is the
  //   name of
  //
  //     - an object or reference, unless it has internal linkage; or
  if (const auto *Var = dyn_cast<VarDecl>(D)) {
    // GCC applies the following optimization to variables and static
    // data members, but not to functions:
    //
    // Modify the variable's LV by the LV of its type unless this is
    // C or extern "C".  This follows from [basic.link]p9:
    //   A type without linkage shall not be used as the type of a
    //   variable or function with external linkage unless
    //    - the entity has C language linkage, or
    //    - the entity is declared within an unnamed namespace, or
    //    - the entity is not used or is defined in the same
    //      translation unit.
    // and [basic.link]p10:
    //   ...the types specified by all declarations referring to a
    //   given variable or function shall be identical...
    // C does not have an equivalent rule.
    //
    // Ignore this if we've got an explicit attribute;  the user
    // probably knows what they're doing.
    //
    // Note that we don't want to make the variable non-external
    // because of this, but unique-external linkage suits us.
    if (Context.getLangOpts().CPlusPlus && !isFirstInExternCContext(Var) &&
        !IgnoreVarTypeLinkage) {
      LinkageInfo TypeLV = getLVForType(*Var->getType(), computation);
      if (!isExternallyVisible(TypeLV.getLinkage()))
        return LinkageInfo::uniqueExternal();
      if (!LV.isVisibilityExplicit())
        LV.mergeVisibility(TypeLV);
    }

    if (Var->getStorageClass() == SC_PrivateExtern)
      LV.mergeVisibility(HiddenVisibility, true);

    // Note that Sema::MergeVarDecl already takes care of implementing
    // C99 6.2.2p4 and propagating the visibility attribute, so we don't have
    // to do it here.

    // As per function and class template specializations (below),
    // consider LV for the template and template arguments.  We're at file
    // scope, so we do not need to worry about nested specializations.
    if (const auto *spec = dyn_cast<VarTemplateSpecializationDecl>(Var)) {
      mergeTemplateLV(LV, spec, computation);
    }

  //     - a function, unless it has internal linkage; or
  } else if (const auto *Function = dyn_cast<FunctionDecl>(D)) {
    // In theory, we can modify the function's LV by the LV of its
    // type unless it has C linkage (see comment above about variables
    // for justification).  In practice, GCC doesn't do this, so it's
    // just too painful to make work.

    if (Function->getStorageClass() == SC_PrivateExtern)
      LV.mergeVisibility(HiddenVisibility, true);

    // Note that Sema::MergeCompatibleFunctionDecls already takes care of
    // merging storage classes and visibility attributes, so we don't have to
    // look at previous decls in here.

    // In C++, then if the type of the function uses a type with
    // unique-external linkage, it's not legally usable from outside
    // this translation unit.  However, we should use the C linkage
    // rules instead for extern "C" declarations.
    if (Context.getLangOpts().CPlusPlus && !isFirstInExternCContext(Function)) {
      // Only look at the type-as-written. Otherwise, deducing the return type
      // of a function could change its linkage.
      QualType TypeAsWritten = Function->getType();
      if (TypeSourceInfo *TSI = Function->getTypeSourceInfo())
        TypeAsWritten = TSI->getType();
      if (!isExternallyVisible(TypeAsWritten->getLinkage()))
        return LinkageInfo::uniqueExternal();
    }

    // Consider LV from the template and the template arguments.
    // We're at file scope, so we do not need to worry about nested
    // specializations.
    if (FunctionTemplateSpecializationInfo *specInfo
                               = Function->getTemplateSpecializationInfo()) {
      mergeTemplateLV(LV, Function, specInfo, computation);
    }

  //     - a named class (Clause 9), or an unnamed class defined in a
  //       typedef declaration in which the class has the typedef name
  //       for linkage purposes (7.1.3); or
  //     - a named enumeration (7.2), or an unnamed enumeration
  //       defined in a typedef declaration in which the enumeration
  //       has the typedef name for linkage purposes (7.1.3); or
  } else if (const auto *Tag = dyn_cast<TagDecl>(D)) {
    // Unnamed tags have no linkage.
    if (!Tag->hasNameForLinkage())
      return LinkageInfo::none();

    // If this is a class template specialization, consider the
    // linkage of the template and template arguments.  We're at file
    // scope, so we do not need to worry about nested specializations.
    if (const auto *spec = dyn_cast<ClassTemplateSpecializationDecl>(Tag)) {
      mergeTemplateLV(LV, spec, computation);
    }

  //     - an enumerator belonging to an enumeration with external linkage;
  } else if (isa<EnumConstantDecl>(D)) {
    LinkageInfo EnumLV = getLVForDecl(cast<NamedDecl>(D->getDeclContext()),
                                      computation);
    if (!isExternalFormalLinkage(EnumLV.getLinkage()))
      return LinkageInfo::none();
    LV.merge(EnumLV);

  //     - a template, unless it is a function template that has
  //       internal linkage (Clause 14);
  } else if (const auto *temp = dyn_cast<TemplateDecl>(D)) {
    bool considerVisibility = !hasExplicitVisibilityAlready(computation);
    LinkageInfo tempLV =
      getLVForTemplateParameterList(temp->getTemplateParameters(), computation);
    LV.mergeMaybeWithVisibility(tempLV, considerVisibility);

  //     - a namespace (7.3), unless it is declared within an unnamed
  //       namespace.
  //
  // We handled names in anonymous namespaces above.
  } else if (isa<NamespaceDecl>(D)) {
    return LV;

  // By extension, we assign external linkage to Objective-C
  // interfaces.
  } else if (isa<ObjCInterfaceDecl>(D)) {
    // fallout

  } else if (auto *TD = dyn_cast<TypedefNameDecl>(D)) {
    // A typedef declaration has linkage if it gives a type a name for
    // linkage purposes.
    if (!TD->getAnonDeclWithTypedefName(/*AnyRedecl*/true))
      return LinkageInfo::none();

  // Everything not covered here has no linkage.
  } else {
    return LinkageInfo::none();
  }

  // If we ended up with non-externally-visible linkage, visibility should
  // always be default.
  if (!isExternallyVisible(LV.getLinkage()))
    return LinkageInfo(LV.getLinkage(), DefaultVisibility, false);

  return LV;
}

LinkageInfo
LinkageComputer::getLVForClassMember(const NamedDecl *D,
                                     LVComputationKind computation,
                                     bool IgnoreVarTypeLinkage) {
  // Only certain class members have linkage.  Note that fields don't
  // really have linkage, but it's convenient to say they do for the
  // purposes of calculating linkage of pointer-to-data-member
  // template arguments.
  //
  // Templates also don't officially have linkage, but since we ignore
  // the C++ standard and look at template arguments when determining
  // linkage and visibility of a template specialization, we might hit
  // a template template argument that way. If we do, we need to
  // consider its linkage.
  if (!(isa<CXXMethodDecl>(D) ||
        isa<VarDecl>(D) ||
        isa<FieldDecl>(D) ||
        isa<IndirectFieldDecl>(D) ||
        isa<TagDecl>(D) ||
        isa<TemplateDecl>(D)))
    return LinkageInfo::none();

  LinkageInfo LV;

  // If we have an explicit visibility attribute, merge that in.
  if (!hasExplicitVisibilityAlready(computation)) {
    if (Optional<Visibility> Vis = getExplicitVisibility(D, computation))
      LV.mergeVisibility(*Vis, true);
    // If we're paying attention to global visibility, apply
    // -finline-visibility-hidden if this is an inline method.
    //
    // Note that we do this before merging information about
    // the class visibility.
    if (!LV.isVisibilityExplicit() && useInlineVisibilityHidden(D))
      LV.mergeVisibility(HiddenVisibility, /*visibilityExplicit=*/false);
  }

  // If this class member has an explicit visibility attribute, the only
  // thing that can change its visibility is the template arguments, so
  // only look for them when processing the class.
  LVComputationKind classComputation = computation;
  if (LV.isVisibilityExplicit())
    classComputation = withExplicitVisibilityAlready(computation);

  LinkageInfo classLV =
    getLVForDecl(cast<RecordDecl>(D->getDeclContext()), classComputation);
  // The member has the same linkage as the class. If that's not externally
  // visible, we don't need to compute anything about the linkage.
  // FIXME: If we're only computing linkage, can we bail out here?
  if (!isExternallyVisible(classLV.getLinkage()))
    return classLV;


  // Otherwise, don't merge in classLV yet, because in certain cases
  // we need to completely ignore the visibility from it.

  // Specifically, if this decl exists and has an explicit attribute.
  const NamedDecl *explicitSpecSuppressor = nullptr;

  if (const auto *MD = dyn_cast<CXXMethodDecl>(D)) {
    // Only look at the type-as-written. Otherwise, deducing the return type
    // of a function could change its linkage.
    QualType TypeAsWritten = MD->getType();
    if (TypeSourceInfo *TSI = MD->getTypeSourceInfo())
      TypeAsWritten = TSI->getType();
    if (!isExternallyVisible(TypeAsWritten->getLinkage()))
      return LinkageInfo::uniqueExternal();

    // If this is a method template specialization, use the linkage for
    // the template parameters and arguments.
    if (FunctionTemplateSpecializationInfo *spec
           = MD->getTemplateSpecializationInfo()) {
      mergeTemplateLV(LV, MD, spec, computation);
      if (spec->isExplicitSpecialization()) {
        explicitSpecSuppressor = MD;
      } else if (isExplicitMemberSpecialization(spec->getTemplate())) {
        explicitSpecSuppressor = spec->getTemplate()->getTemplatedDecl();
      }
    } else if (isExplicitMemberSpecialization(MD)) {
      explicitSpecSuppressor = MD;
    }

  } else if (const auto *RD = dyn_cast<CXXRecordDecl>(D)) {
    if (const auto *spec = dyn_cast<ClassTemplateSpecializationDecl>(RD)) {
      mergeTemplateLV(LV, spec, computation);
      if (spec->isExplicitSpecialization()) {
        explicitSpecSuppressor = spec;
      } else {
        const ClassTemplateDecl *temp = spec->getSpecializedTemplate();
        if (isExplicitMemberSpecialization(temp)) {
          explicitSpecSuppressor = temp->getTemplatedDecl();
        }
      }
    } else if (isExplicitMemberSpecialization(RD)) {
      explicitSpecSuppressor = RD;
    }

  // Static data members.
  } else if (const auto *VD = dyn_cast<VarDecl>(D)) {
    if (const auto *spec = dyn_cast<VarTemplateSpecializationDecl>(VD))
      mergeTemplateLV(LV, spec, computation);

    // Modify the variable's linkage by its type, but ignore the
    // type's visibility unless it's a definition.
    if (!IgnoreVarTypeLinkage) {
      LinkageInfo typeLV = getLVForType(*VD->getType(), computation);
      // FIXME: If the type's linkage is not externally visible, we can
      // give this static data member UniqueExternalLinkage.
      if (!LV.isVisibilityExplicit() && !classLV.isVisibilityExplicit())
        LV.mergeVisibility(typeLV);
      LV.mergeExternalVisibility(typeLV);
    }

    if (isExplicitMemberSpecialization(VD)) {
      explicitSpecSuppressor = VD;
    }

  // Template members.
  } else if (const auto *temp = dyn_cast<TemplateDecl>(D)) {
    bool considerVisibility =
      (!LV.isVisibilityExplicit() &&
       !classLV.isVisibilityExplicit() &&
       !hasExplicitVisibilityAlready(computation));
    LinkageInfo tempLV =
      getLVForTemplateParameterList(temp->getTemplateParameters(), computation);
    LV.mergeMaybeWithVisibility(tempLV, considerVisibility);

    if (const auto *redeclTemp = dyn_cast<RedeclarableTemplateDecl>(temp)) {
      if (isExplicitMemberSpecialization(redeclTemp)) {
        explicitSpecSuppressor = temp->getTemplatedDecl();
      }
    }
  }

  // We should never be looking for an attribute directly on a template.
  assert(!explicitSpecSuppressor || !isa<TemplateDecl>(explicitSpecSuppressor));

  // If this member is an explicit member specialization, and it has
  // an explicit attribute, ignore visibility from the parent.
  bool considerClassVisibility = true;
  if (explicitSpecSuppressor &&
      // optimization: hasDVA() is true only with explicit visibility.
      LV.isVisibilityExplicit() &&
      classLV.getVisibility() != DefaultVisibility &&
      hasDirectVisibilityAttribute(explicitSpecSuppressor, computation)) {
    considerClassVisibility = false;
  }

  // Finally, merge in information from the class.
  LV.mergeMaybeWithVisibility(classLV, considerClassVisibility);
  return LV;
}

void NamedDecl::anchor() {}

bool NamedDecl::isLinkageValid() const {
  if (!hasCachedLinkage())
    return true;

  Linkage L = LinkageComputer{}
                  .computeLVForDecl(this, LVComputationKind::forLinkageOnly())
                  .getLinkage();
  return L == getCachedLinkage();
}

ObjCStringFormatFamily NamedDecl::getObjCFStringFormattingFamily() const {
  StringRef name = getName();
  if (name.empty()) return SFF_None;

  if (name.front() == 'C')
    if (name == "CFStringCreateWithFormat" ||
        name == "CFStringCreateWithFormatAndArguments" ||
        name == "CFStringAppendFormat" ||
        name == "CFStringAppendFormatAndArguments")
      return SFF_CFString;
  return SFF_None;
}

Linkage NamedDecl::getLinkageInternal() const {
  // We don't care about visibility here, so ask for the cheapest
  // possible visibility analysis.
  return LinkageComputer{}
      .getLVForDecl(this, LVComputationKind::forLinkageOnly())
      .getLinkage();
}

LinkageInfo NamedDecl::getLinkageAndVisibility() const {
  return LinkageComputer{}.getDeclLinkageAndVisibility(this);
}

static Optional<Visibility>
getExplicitVisibilityAux(const NamedDecl *ND,
                         NamedDecl::ExplicitVisibilityKind kind,
                         bool IsMostRecent) {
  assert(!IsMostRecent || ND == ND->getMostRecentDecl());

  // Check the declaration itself first.
  if (Optional<Visibility> V = getVisibilityOf(ND, kind))
    return V;

  // If this is a member class of a specialization of a class template
  // and the corresponding decl has explicit visibility, use that.
  if (const auto *RD = dyn_cast<CXXRecordDecl>(ND)) {
    CXXRecordDecl *InstantiatedFrom = RD->getInstantiatedFromMemberClass();
    if (InstantiatedFrom)
      return getVisibilityOf(InstantiatedFrom, kind);
  }

  // If there wasn't explicit visibility there, and this is a
  // specialization of a class template, check for visibility
  // on the pattern.
  if (const auto *spec = dyn_cast<ClassTemplateSpecializationDecl>(ND)) {
    // Walk all the template decl till this point to see if there are
    // explicit visibility attributes.
    const auto *TD = spec->getSpecializedTemplate()->getTemplatedDecl();
    while (TD != nullptr) {
      auto Vis = getVisibilityOf(TD, kind);
      if (Vis != None)
        return Vis;
      TD = TD->getPreviousDecl();
    }
    return None;
  }

  // Use the most recent declaration.
  if (!IsMostRecent && !isa<NamespaceDecl>(ND)) {
    const NamedDecl *MostRecent = ND->getMostRecentDecl();
    if (MostRecent != ND)
      return getExplicitVisibilityAux(MostRecent, kind, true);
  }

  if (const auto *Var = dyn_cast<VarDecl>(ND)) {
    if (Var->isStaticDataMember()) {
      VarDecl *InstantiatedFrom = Var->getInstantiatedFromStaticDataMember();
      if (InstantiatedFrom)
        return getVisibilityOf(InstantiatedFrom, kind);
    }

    if (const auto *VTSD = dyn_cast<VarTemplateSpecializationDecl>(Var))
      return getVisibilityOf(VTSD->getSpecializedTemplate()->getTemplatedDecl(),
                             kind);

    return None;
  }
  // Also handle function template specializations.
  if (const auto *fn = dyn_cast<FunctionDecl>(ND)) {
    // If the function is a specialization of a template with an
    // explicit visibility attribute, use that.
    if (FunctionTemplateSpecializationInfo *templateInfo
          = fn->getTemplateSpecializationInfo())
      return getVisibilityOf(templateInfo->getTemplate()->getTemplatedDecl(),
                             kind);

    // If the function is a member of a specialization of a class template
    // and the corresponding decl has explicit visibility, use that.
    FunctionDecl *InstantiatedFrom = fn->getInstantiatedFromMemberFunction();
    if (InstantiatedFrom)
      return getVisibilityOf(InstantiatedFrom, kind);

    return None;
  }

  // The visibility of a template is stored in the templated decl.
  if (const auto *TD = dyn_cast<TemplateDecl>(ND))
    return getVisibilityOf(TD->getTemplatedDecl(), kind);

  return None;
}

Optional<Visibility>
NamedDecl::getExplicitVisibility(ExplicitVisibilityKind kind) const {
  return getExplicitVisibilityAux(this, kind, false);
}

LinkageInfo LinkageComputer::getLVForClosure(const DeclContext *DC,
                                             Decl *ContextDecl,
                                             LVComputationKind computation) {
  // This lambda has its linkage/visibility determined by its owner.
  const NamedDecl *Owner;
  if (!ContextDecl)
    Owner = dyn_cast<NamedDecl>(DC);
  else if (isa<ParmVarDecl>(ContextDecl))
    Owner =
        dyn_cast<NamedDecl>(ContextDecl->getDeclContext()->getRedeclContext());
  else
    Owner = cast<NamedDecl>(ContextDecl);

  if (!Owner)
    return LinkageInfo::none();

  // If the owner has a deduced type, we need to skip querying the linkage and
  // visibility of that type, because it might involve this closure type.  The
  // only effect of this is that we might give a lambda VisibleNoLinkage rather
  // than NoLinkage when we don't strictly need to, which is benign.
  auto *VD = dyn_cast<VarDecl>(Owner);
  LinkageInfo OwnerLV =
      VD && VD->getType()->getContainedDeducedType()
          ? computeLVForDecl(Owner, computation, /*IgnoreVarTypeLinkage*/true)
          : getLVForDecl(Owner, computation);

  // A lambda never formally has linkage. But if the owner is externally
  // visible, then the lambda is too. We apply the same rules to blocks.
  if (!isExternallyVisible(OwnerLV.getLinkage()))
    return LinkageInfo::none();
  return LinkageInfo(VisibleNoLinkage, OwnerLV.getVisibility(),
                     OwnerLV.isVisibilityExplicit());
}

LinkageInfo LinkageComputer::getLVForLocalDecl(const NamedDecl *D,
                                               LVComputationKind computation) {
  if (const auto *Function = dyn_cast<FunctionDecl>(D)) {
    if (Function->isInAnonymousNamespace() &&
        !isFirstInExternCContext(Function))
      return getInternalLinkageFor(Function);

    // This is a "void f();" which got merged with a file static.
    if (Function->getCanonicalDecl()->getStorageClass() == SC_Static)
      return getInternalLinkageFor(Function);

    LinkageInfo LV;
    if (!hasExplicitVisibilityAlready(computation)) {
      if (Optional<Visibility> Vis =
              getExplicitVisibility(Function, computation))
        LV.mergeVisibility(*Vis, true);
    }

    // Note that Sema::MergeCompatibleFunctionDecls already takes care of
    // merging storage classes and visibility attributes, so we don't have to
    // look at previous decls in here.

    return LV;
  }

  if (const auto *Var = dyn_cast<VarDecl>(D)) {
    if (Var->hasExternalStorage()) {
      if (Var->isInAnonymousNamespace() && !isFirstInExternCContext(Var))
        return getInternalLinkageFor(Var);

      LinkageInfo LV;
      if (Var->getStorageClass() == SC_PrivateExtern)
        LV.mergeVisibility(HiddenVisibility, true);
      else if (!hasExplicitVisibilityAlready(computation)) {
        if (Optional<Visibility> Vis = getExplicitVisibility(Var, computation))
          LV.mergeVisibility(*Vis, true);
      }

      if (const VarDecl *Prev = Var->getPreviousDecl()) {
        LinkageInfo PrevLV = getLVForDecl(Prev, computation);
        if (PrevLV.getLinkage())
          LV.setLinkage(PrevLV.getLinkage());
        LV.mergeVisibility(PrevLV);
      }

      return LV;
    }

    if (!Var->isStaticLocal())
      return LinkageInfo::none();
  }

  ASTContext &Context = D->getASTContext();
  if (!Context.getLangOpts().CPlusPlus)
    return LinkageInfo::none();

  const Decl *OuterD = getOutermostFuncOrBlockContext(D);
  if (!OuterD || OuterD->isInvalidDecl())
    return LinkageInfo::none();

  LinkageInfo LV;
  if (const auto *BD = dyn_cast<BlockDecl>(OuterD)) {
    if (!BD->getBlockManglingNumber())
      return LinkageInfo::none();

    LV = getLVForClosure(BD->getDeclContext()->getRedeclContext(),
                         BD->getBlockManglingContextDecl(), computation);
  } else {
    const auto *FD = cast<FunctionDecl>(OuterD);
    if (!FD->isInlined() &&
        !isTemplateInstantiation(FD->getTemplateSpecializationKind()))
      return LinkageInfo::none();

    // If a function is hidden by -fvisibility-inlines-hidden option and
    // is not explicitly attributed as a hidden function,
    // we should not make static local variables in the function hidden.
    LV = getLVForDecl(FD, computation);
    if (isa<VarDecl>(D) && useInlineVisibilityHidden(FD) &&
        !LV.isVisibilityExplicit()) {
      assert(cast<VarDecl>(D)->isStaticLocal());
      // If this was an implicitly hidden inline method, check again for
      // explicit visibility on the parent class, and use that for static locals
      // if present.
      if (const auto *MD = dyn_cast<CXXMethodDecl>(FD))
        LV = getLVForDecl(MD->getParent(), computation);
      if (!LV.isVisibilityExplicit()) {
        Visibility globalVisibility =
            computation.isValueVisibility()
                ? Context.getLangOpts().getValueVisibilityMode()
                : Context.getLangOpts().getTypeVisibilityMode();
        return LinkageInfo(VisibleNoLinkage, globalVisibility,
                           /*visibilityExplicit=*/false);
      }
    }
  }
  if (!isExternallyVisible(LV.getLinkage()))
    return LinkageInfo::none();
  return LinkageInfo(VisibleNoLinkage, LV.getVisibility(),
                     LV.isVisibilityExplicit());
}

static inline const CXXRecordDecl*
getOutermostEnclosingLambda(const CXXRecordDecl *Record) {
  const CXXRecordDecl *Ret = Record;
  while (Record && Record->isLambda()) {
    Ret = Record;
    if (!Record->getParent()) break;
    // Get the Containing Class of this Lambda Class
    Record = dyn_cast_or_null<CXXRecordDecl>(
      Record->getParent()->getParent());
  }
  return Ret;
}

LinkageInfo LinkageComputer::computeLVForDecl(const NamedDecl *D,
                                              LVComputationKind computation,
                                              bool IgnoreVarTypeLinkage) {
  // Internal_linkage attribute overrides other considerations.
  if (D->hasAttr<InternalLinkageAttr>())
    return getInternalLinkageFor(D);

  // Objective-C: treat all Objective-C declarations as having external
  // linkage.
  switch (D->getKind()) {
    default:
      break;

    // Per C++ [basic.link]p2, only the names of objects, references,
    // functions, types, templates, namespaces, and values ever have linkage.
    //
    // Note that the name of a typedef, namespace alias, using declaration,
    // and so on are not the name of the corresponding type, namespace, or
    // declaration, so they do *not* have linkage.
    case Decl::ImplicitParam:
    case Decl::Label:
    case Decl::NamespaceAlias:
    case Decl::ParmVar:
    case Decl::Using:
    case Decl::UsingShadow:
    case Decl::UsingDirective:
      return LinkageInfo::none();

    case Decl::EnumConstant:
      // C++ [basic.link]p4: an enumerator has the linkage of its enumeration.
      if (D->getASTContext().getLangOpts().CPlusPlus)
        return getLVForDecl(cast<EnumDecl>(D->getDeclContext()), computation);
      return LinkageInfo::visible_none();

    case Decl::Typedef:
    case Decl::TypeAlias:
      // A typedef declaration has linkage if it gives a type a name for
      // linkage purposes.
      if (!cast<TypedefNameDecl>(D)
               ->getAnonDeclWithTypedefName(/*AnyRedecl*/true))
        return LinkageInfo::none();
      break;

    case Decl::TemplateTemplateParm: // count these as external
    case Decl::NonTypeTemplateParm:
    case Decl::ObjCAtDefsField:
    case Decl::ObjCCategory:
    case Decl::ObjCCategoryImpl:
    case Decl::ObjCCompatibleAlias:
    case Decl::ObjCImplementation:
    case Decl::ObjCMethod:
    case Decl::ObjCProperty:
    case Decl::ObjCPropertyImpl:
    case Decl::ObjCProtocol:
      return getExternalLinkageFor(D);

    case Decl::CXXRecord: {
      const auto *Record = cast<CXXRecordDecl>(D);
      if (Record->isLambda()) {
        if (!Record->getLambdaManglingNumber()) {
          // This lambda has no mangling number, so it's internal.
          return getInternalLinkageFor(D);
        }

        // This lambda has its linkage/visibility determined:
        //  - either by the outermost lambda if that lambda has no mangling
        //    number.
        //  - or by the parent of the outer most lambda
        // This prevents infinite recursion in settings such as nested lambdas
        // used in NSDMI's, for e.g.
        //  struct L {
        //    int t{};
        //    int t2 = ([](int a) { return [](int b) { return b; };})(t)(t);
        //  };
        const CXXRecordDecl *OuterMostLambda =
            getOutermostEnclosingLambda(Record);
        if (!OuterMostLambda->getLambdaManglingNumber())
          return getInternalLinkageFor(D);

        return getLVForClosure(
                  OuterMostLambda->getDeclContext()->getRedeclContext(),
                  OuterMostLambda->getLambdaContextDecl(), computation);
      }

      break;
    }
  }

  // Handle linkage for namespace-scope names.
  if (D->getDeclContext()->getRedeclContext()->isFileContext())
    return getLVForNamespaceScopeDecl(D, computation, IgnoreVarTypeLinkage);

  // C++ [basic.link]p5:
  //   In addition, a member function, static data member, a named
  //   class or enumeration of class scope, or an unnamed class or
  //   enumeration defined in a class-scope typedef declaration such
  //   that the class or enumeration has the typedef name for linkage
  //   purposes (7.1.3), has external linkage if the name of the class
  //   has external linkage.
  if (D->getDeclContext()->isRecord())
    return getLVForClassMember(D, computation, IgnoreVarTypeLinkage);

  // C++ [basic.link]p6:
  //   The name of a function declared in block scope and the name of
  //   an object declared by a block scope extern declaration have
  //   linkage. If there is a visible declaration of an entity with
  //   linkage having the same name and type, ignoring entities
  //   declared outside the innermost enclosing namespace scope, the
  //   block scope declaration declares that same entity and receives
  //   the linkage of the previous declaration. If there is more than
  //   one such matching entity, the program is ill-formed. Otherwise,
  //   if no matching entity is found, the block scope entity receives
  //   external linkage.
  if (D->getDeclContext()->isFunctionOrMethod())
    return getLVForLocalDecl(D, computation);

  // C++ [basic.link]p6:
  //   Names not covered by these rules have no linkage.
  return LinkageInfo::none();
}

/// getLVForDecl - Get the linkage and visibility for the given declaration.
LinkageInfo LinkageComputer::getLVForDecl(const NamedDecl *D,
                                          LVComputationKind computation) {
  // Internal_linkage attribute overrides other considerations.
  if (D->hasAttr<InternalLinkageAttr>())
    return getInternalLinkageFor(D);

  if (computation.IgnoreAllVisibility && D->hasCachedLinkage())
    return LinkageInfo(D->getCachedLinkage(), DefaultVisibility, false);

  if (llvm::Optional<LinkageInfo> LI = lookup(D, computation))
    return *LI;

  LinkageInfo LV = computeLVForDecl(D, computation);
  if (D->hasCachedLinkage())
    assert(D->getCachedLinkage() == LV.getLinkage());

  D->setCachedLinkage(LV.getLinkage());
  cache(D, computation, LV);

#ifndef NDEBUG
  // In C (because of gnu inline) and in c++ with microsoft extensions an
  // static can follow an extern, so we can have two decls with different
  // linkages.
  const LangOptions &Opts = D->getASTContext().getLangOpts();
  if (!Opts.CPlusPlus || Opts.MicrosoftExt)
    return LV;

  // We have just computed the linkage for this decl. By induction we know
  // that all other computed linkages match, check that the one we just
  // computed also does.
  NamedDecl *Old = nullptr;
  for (auto I : D->redecls()) {
    auto *T = cast<NamedDecl>(I);
    if (T == D)
      continue;
    if (!T->isInvalidDecl() && T->hasCachedLinkage()) {
      Old = T;
      break;
    }
  }
  assert(!Old || Old->getCachedLinkage() == D->getCachedLinkage());
#endif

  return LV;
}

LinkageInfo LinkageComputer::getDeclLinkageAndVisibility(const NamedDecl *D) {
  return getLVForDecl(D,
                      LVComputationKind(usesTypeVisibility(D)
                                            ? NamedDecl::VisibilityForType
                                            : NamedDecl::VisibilityForValue));
}

Module *Decl::getOwningModuleForLinkage(bool IgnoreLinkage) const {
  Module *M = getOwningModule();
  if (!M)
    return nullptr;

  switch (M->Kind) {
  case Module::ModuleMapModule:
    // Module map modules have no special linkage semantics.
    return nullptr;

  case Module::ModuleInterfaceUnit:
    return M;

  case Module::GlobalModuleFragment: {
    // External linkage declarations in the global module have no owning module
    // for linkage purposes. But internal linkage declarations in the global
    // module fragment of a particular module are owned by that module for
    // linkage purposes.
    if (IgnoreLinkage)
      return nullptr;
    bool InternalLinkage;
    if (auto *ND = dyn_cast<NamedDecl>(this))
      InternalLinkage = !ND->hasExternalFormalLinkage();
    else {
      auto *NSD = dyn_cast<NamespaceDecl>(this);
      InternalLinkage = (NSD && NSD->isAnonymousNamespace()) ||
                        isInAnonymousNamespace();
    }
    return InternalLinkage ? M->Parent : nullptr;
  }
  }

  llvm_unreachable("unknown module kind");
}

void NamedDecl::printName(raw_ostream &os) const {
  os << Name;
}

std::string NamedDecl::getQualifiedNameAsString() const {
  std::string QualName;
  llvm::raw_string_ostream OS(QualName);
  printQualifiedName(OS, getASTContext().getPrintingPolicy());
  return OS.str();
}

void NamedDecl::printQualifiedName(raw_ostream &OS) const {
  printQualifiedName(OS, getASTContext().getPrintingPolicy());
}

void NamedDecl::printQualifiedName(raw_ostream &OS,
                                   const PrintingPolicy &P) const {
  const DeclContext *Ctx = getDeclContext();

  // For ObjC methods, look through categories and use the interface as context.
  if (auto *MD = dyn_cast<ObjCMethodDecl>(this))
    if (auto *ID = MD->getClassInterface())
      Ctx = ID;

  if (Ctx->isFunctionOrMethod()) {
    printName(OS);
    return;
  }

  using ContextsTy = SmallVector<const DeclContext *, 8>;
  ContextsTy Contexts;

  // Collect named contexts.
  while (Ctx) {
    if (isa<NamedDecl>(Ctx))
      Contexts.push_back(Ctx);
    Ctx = Ctx->getParent();
  }

  for (const DeclContext *DC : llvm::reverse(Contexts)) {
    if (const auto *Spec = dyn_cast<ClassTemplateSpecializationDecl>(DC)) {
      OS << Spec->getName();
      const TemplateArgumentList &TemplateArgs = Spec->getTemplateArgs();
      printTemplateArgumentList(OS, TemplateArgs.asArray(), P);
    } else if (const auto *ND = dyn_cast<NamespaceDecl>(DC)) {
      if (P.SuppressUnwrittenScope &&
          (ND->isAnonymousNamespace() || ND->isInline()))
        continue;
      if (ND->isAnonymousNamespace()) {
        OS << (P.MSVCFormatting ? "`anonymous namespace\'"
                                : "(anonymous namespace)");
      }
      else
        OS << *ND;
    } else if (const auto *RD = dyn_cast<RecordDecl>(DC)) {
      if (!RD->getIdentifier())
        OS << "(anonymous " << RD->getKindName() << ')';
      else
        OS << *RD;
    } else if (const auto *FD = dyn_cast<FunctionDecl>(DC)) {
      const FunctionProtoType *FT = nullptr;
      if (FD->hasWrittenPrototype())
        FT = dyn_cast<FunctionProtoType>(FD->getType()->castAs<FunctionType>());

      OS << *FD << '(';
      if (FT) {
        unsigned NumParams = FD->getNumParams();
        for (unsigned i = 0; i < NumParams; ++i) {
          if (i)
            OS << ", ";
          OS << FD->getParamDecl(i)->getType().stream(P);
        }

        if (FT->isVariadic()) {
          if (NumParams > 0)
            OS << ", ";
          OS << "...";
        }
      }
      OS << ')';
    } else if (const auto *ED = dyn_cast<EnumDecl>(DC)) {
      // C++ [dcl.enum]p10: Each enum-name and each unscoped
      // enumerator is declared in the scope that immediately contains
      // the enum-specifier. Each scoped enumerator is declared in the
      // scope of the enumeration.
      // For the case of unscoped enumerator, do not include in the qualified
      // name any information about its enum enclosing scope, as its visibility
      // is global.
      if (ED->isScoped())
        OS << *ED;
      else
        continue;
    } else {
      OS << *cast<NamedDecl>(DC);
    }
    OS << "::";
  }

  if (getDeclName() || isa<DecompositionDecl>(this))
    OS << *this;
  else
    OS << "(anonymous)";
}

void NamedDecl::getNameForDiagnostic(raw_ostream &OS,
                                     const PrintingPolicy &Policy,
                                     bool Qualified) const {
  if (Qualified)
    printQualifiedName(OS, Policy);
  else
    printName(OS);
}

template<typename T> static bool isRedeclarableImpl(Redeclarable<T> *) {
  return true;
}
static bool isRedeclarableImpl(...) { return false; }
static bool isRedeclarable(Decl::Kind K) {
  switch (K) {
#define DECL(Type, Base) \
  case Decl::Type: \
    return isRedeclarableImpl((Type##Decl *)nullptr);
#define ABSTRACT_DECL(DECL)
#include "clang/AST/DeclNodes.inc"
  }
  llvm_unreachable("unknown decl kind");
}

bool NamedDecl::declarationReplaces(NamedDecl *OldD, bool IsKnownNewer) const {
  assert(getDeclName() == OldD->getDeclName() && "Declaration name mismatch");

  // Never replace one imported declaration with another; we need both results
  // when re-exporting.
  if (OldD->isFromASTFile() && isFromASTFile())
    return false;

  // A kind mismatch implies that the declaration is not replaced.
  if (OldD->getKind() != getKind())
    return false;

  // For method declarations, we never replace. (Why?)
  if (isa<ObjCMethodDecl>(this))
    return false;

  // For parameters, pick the newer one. This is either an error or (in
  // Objective-C) permitted as an extension.
  if (isa<ParmVarDecl>(this))
    return true;

  // Inline namespaces can give us two declarations with the same
  // name and kind in the same scope but different contexts; we should
  // keep both declarations in this case.
  if (!this->getDeclContext()->getRedeclContext()->Equals(
          OldD->getDeclContext()->getRedeclContext()))
    return false;

  // Using declarations can be replaced if they import the same name from the
  // same context.
  if (auto *UD = dyn_cast<UsingDecl>(this)) {
    ASTContext &Context = getASTContext();
    return Context.getCanonicalNestedNameSpecifier(UD->getQualifier()) ==
           Context.getCanonicalNestedNameSpecifier(
               cast<UsingDecl>(OldD)->getQualifier());
  }
  if (auto *UUVD = dyn_cast<UnresolvedUsingValueDecl>(this)) {
    ASTContext &Context = getASTContext();
    return Context.getCanonicalNestedNameSpecifier(UUVD->getQualifier()) ==
           Context.getCanonicalNestedNameSpecifier(
                        cast<UnresolvedUsingValueDecl>(OldD)->getQualifier());
  }

  if (isRedeclarable(getKind())) {
    if (getCanonicalDecl() != OldD->getCanonicalDecl())
      return false;

    if (IsKnownNewer)
      return true;

    // Check whether this is actually newer than OldD. We want to keep the
    // newer declaration. This loop will usually only iterate once, because
    // OldD is usually the previous declaration.
    for (auto D : redecls()) {
      if (D == OldD)
        break;

      // If we reach the canonical declaration, then OldD is not actually older
      // than this one.
      //
      // FIXME: In this case, we should not add this decl to the lookup table.
      if (D->isCanonicalDecl())
        return false;
    }

    // It's a newer declaration of the same kind of declaration in the same
    // scope: we want this decl instead of the existing one.
    return true;
  }

  // In all other cases, we need to keep both declarations in case they have
  // different visibility. Any attempt to use the name will result in an
  // ambiguity if more than one is visible.
  return false;
}

bool NamedDecl::hasLinkage() const {
  return getFormalLinkage() != NoLinkage;
}

NamedDecl *NamedDecl::getUnderlyingDeclImpl() {
  NamedDecl *ND = this;
  while (auto *UD = dyn_cast<UsingShadowDecl>(ND))
    ND = UD->getTargetDecl();

  if (auto *AD = dyn_cast<ObjCCompatibleAliasDecl>(ND))
    return AD->getClassInterface();

  if (auto *AD = dyn_cast<NamespaceAliasDecl>(ND))
    return AD->getNamespace();

  return ND;
}

bool NamedDecl::isCXXInstanceMember() const {
  if (!isCXXClassMember())
    return false;

  const NamedDecl *D = this;
  if (isa<UsingShadowDecl>(D))
    D = cast<UsingShadowDecl>(D)->getTargetDecl();

  if (isa<FieldDecl>(D) || isa<IndirectFieldDecl>(D) || isa<MSPropertyDecl>(D))
    return true;
  if (const auto *MD = dyn_cast_or_null<CXXMethodDecl>(D->getAsFunction()))
    return MD->isInstance();
  return false;
}

//===----------------------------------------------------------------------===//
// DeclaratorDecl Implementation
//===----------------------------------------------------------------------===//

template <typename DeclT>
static SourceLocation getTemplateOrInnerLocStart(const DeclT *decl) {
  if (decl->getNumTemplateParameterLists() > 0)
    return decl->getTemplateParameterList(0)->getTemplateLoc();
  else
    return decl->getInnerLocStart();
}

SourceLocation DeclaratorDecl::getTypeSpecStartLoc() const {
  TypeSourceInfo *TSI = getTypeSourceInfo();
  if (TSI) return TSI->getTypeLoc().getBeginLoc();
  return SourceLocation();
}

void DeclaratorDecl::setQualifierInfo(NestedNameSpecifierLoc QualifierLoc) {
  if (QualifierLoc) {
    // Make sure the extended decl info is allocated.
    if (!hasExtInfo()) {
      // Save (non-extended) type source info pointer.
      auto *savedTInfo = DeclInfo.get<TypeSourceInfo*>();
      // Allocate external info struct.
      DeclInfo = new (getASTContext()) ExtInfo;
      // Restore savedTInfo into (extended) decl info.
      getExtInfo()->TInfo = savedTInfo;
    }
    // Set qualifier info.
    getExtInfo()->QualifierLoc = QualifierLoc;
  } else {
    // Here Qualifier == 0, i.e., we are removing the qualifier (if any).
    if (hasExtInfo()) {
      if (getExtInfo()->NumTemplParamLists == 0) {
        // Save type source info pointer.
        TypeSourceInfo *savedTInfo = getExtInfo()->TInfo;
        // Deallocate the extended decl info.
        getASTContext().Deallocate(getExtInfo());
        // Restore savedTInfo into (non-extended) decl info.
        DeclInfo = savedTInfo;
      }
      else
        getExtInfo()->QualifierLoc = QualifierLoc;
    }
  }
}

void DeclaratorDecl::setTemplateParameterListsInfo(
    ASTContext &Context, ArrayRef<TemplateParameterList *> TPLists) {
  assert(!TPLists.empty());
  // Make sure the extended decl info is allocated.
  if (!hasExtInfo()) {
    // Save (non-extended) type source info pointer.
    auto *savedTInfo = DeclInfo.get<TypeSourceInfo*>();
    // Allocate external info struct.
    DeclInfo = new (getASTContext()) ExtInfo;
    // Restore savedTInfo into (extended) decl info.
    getExtInfo()->TInfo = savedTInfo;
  }
  // Set the template parameter lists info.
  getExtInfo()->setTemplateParameterListsInfo(Context, TPLists);
}

SourceLocation DeclaratorDecl::getOuterLocStart() const {
  return getTemplateOrInnerLocStart(this);
}

// Helper function: returns true if QT is or contains a type
// having a postfix component.
static bool typeIsPostfix(QualType QT) {
  while (true) {
    const Type* T = QT.getTypePtr();
    switch (T->getTypeClass()) {
    default:
      return false;
    case Type::Pointer:
      QT = cast<PointerType>(T)->getPointeeType();
      break;
    case Type::BlockPointer:
      QT = cast<BlockPointerType>(T)->getPointeeType();
      break;
    case Type::MemberPointer:
      QT = cast<MemberPointerType>(T)->getPointeeType();
      break;
    case Type::LValueReference:
    case Type::RValueReference:
      QT = cast<ReferenceType>(T)->getPointeeType();
      break;
    case Type::PackExpansion:
      QT = cast<PackExpansionType>(T)->getPattern();
      break;
    case Type::Paren:
    case Type::ConstantArray:
    case Type::DependentSizedArray:
    case Type::IncompleteArray:
    case Type::VariableArray:
    case Type::FunctionProto:
    case Type::FunctionNoProto:
      return true;
    }
  }
}

SourceRange DeclaratorDecl::getSourceRange() const {
  SourceLocation RangeEnd = getLocation();
  if (TypeSourceInfo *TInfo = getTypeSourceInfo()) {
    // If the declaration has no name or the type extends past the name take the
    // end location of the type.
    if (!getDeclName() || typeIsPostfix(TInfo->getType()))
      RangeEnd = TInfo->getTypeLoc().getSourceRange().getEnd();
  }
  return SourceRange(getOuterLocStart(), RangeEnd);
}

void QualifierInfo::setTemplateParameterListsInfo(
    ASTContext &Context, ArrayRef<TemplateParameterList *> TPLists) {
  // Free previous template parameters (if any).
  if (NumTemplParamLists > 0) {
    Context.Deallocate(TemplParamLists);
    TemplParamLists = nullptr;
    NumTemplParamLists = 0;
  }
  // Set info on matched template parameter lists (if any).
  if (!TPLists.empty()) {
    TemplParamLists = new (Context) TemplateParameterList *[TPLists.size()];
    NumTemplParamLists = TPLists.size();
    std::copy(TPLists.begin(), TPLists.end(), TemplParamLists);
  }
}

//===----------------------------------------------------------------------===//
// VarDecl Implementation
//===----------------------------------------------------------------------===//

const char *VarDecl::getStorageClassSpecifierString(StorageClass SC) {
  switch (SC) {
  case SC_None:                 break;
  case SC_Auto:                 return "auto";
  case SC_Extern:               return "extern";
  case SC_PrivateExtern:        return "__private_extern__";
  case SC_Register:             return "register";
  case SC_Static:               return "static";
  }

  llvm_unreachable("Invalid storage class");
}

VarDecl::VarDecl(Kind DK, ASTContext &C, DeclContext *DC,
                 SourceLocation StartLoc, SourceLocation IdLoc,
                 IdentifierInfo *Id, QualType T, TypeSourceInfo *TInfo,
                 StorageClass SC)
    : DeclaratorDecl(DK, DC, IdLoc, Id, T, TInfo, StartLoc),
      redeclarable_base(C) {
  static_assert(sizeof(VarDeclBitfields) <= sizeof(unsigned),
                "VarDeclBitfields too large!");
  static_assert(sizeof(ParmVarDeclBitfields) <= sizeof(unsigned),
                "ParmVarDeclBitfields too large!");
  static_assert(sizeof(NonParmVarDeclBitfields) <= sizeof(unsigned),
                "NonParmVarDeclBitfields too large!");
  AllBits = 0;
  VarDeclBits.SClass = SC;
  // Everything else is implicitly initialized to false.
}

VarDecl *VarDecl::Create(ASTContext &C, DeclContext *DC,
                         SourceLocation StartL, SourceLocation IdL,
                         IdentifierInfo *Id, QualType T, TypeSourceInfo *TInfo,
                         StorageClass S) {
  return new (C, DC) VarDecl(Var, C, DC, StartL, IdL, Id, T, TInfo, S);
}

VarDecl *VarDecl::CreateDeserialized(ASTContext &C, unsigned ID) {
  return new (C, ID)
      VarDecl(Var, C, nullptr, SourceLocation(), SourceLocation(), nullptr,
              QualType(), nullptr, SC_None);
}

void VarDecl::setStorageClass(StorageClass SC) {
  assert(isLegalForVariable(SC));
  VarDeclBits.SClass = SC;
}

VarDecl::TLSKind VarDecl::getTLSKind() const {
  switch (VarDeclBits.TSCSpec) {
  case TSCS_unspecified:
    if (!hasAttr<ThreadAttr>() &&
        !(getASTContext().getLangOpts().OpenMPUseTLS &&
          getASTContext().getTargetInfo().isTLSSupported() &&
          hasAttr<OMPThreadPrivateDeclAttr>()))
      return TLS_None;
    return ((getASTContext().getLangOpts().isCompatibleWithMSVC(
                LangOptions::MSVC2015)) ||
            hasAttr<OMPThreadPrivateDeclAttr>())
               ? TLS_Dynamic
               : TLS_Static;
  case TSCS___thread: // Fall through.
  case TSCS__Thread_local:
    return TLS_Static;
  case TSCS_thread_local:
    return TLS_Dynamic;
  }
  llvm_unreachable("Unknown thread storage class specifier!");
}

SourceRange VarDecl::getSourceRange() const {
  if (const Expr *Init = getInit()) {
    SourceLocation InitEnd = Init->getEndLoc();
    // If Init is implicit, ignore its source range and fallback on
    // DeclaratorDecl::getSourceRange() to handle postfix elements.
    if (InitEnd.isValid() && InitEnd != getLocation())
      return SourceRange(getOuterLocStart(), InitEnd);
  }
  return DeclaratorDecl::getSourceRange();
}

template<typename T>
static LanguageLinkage getDeclLanguageLinkage(const T &D) {
  // C++ [dcl.link]p1: All function types, function names with external linkage,
  // and variable names with external linkage have a language linkage.
  if (!D.hasExternalFormalLinkage())
    return NoLanguageLinkage;

  // Language linkage is a C++ concept, but saying that everything else in C has
  // C language linkage fits the implementation nicely.
  ASTContext &Context = D.getASTContext();
  if (!Context.getLangOpts().CPlusPlus)
    return CLanguageLinkage;

  // C++ [dcl.link]p4: A C language linkage is ignored in determining the
  // language linkage of the names of class members and the function type of
  // class member functions.
  const DeclContext *DC = D.getDeclContext();
  if (DC->isRecord())
    return CXXLanguageLinkage;

  // If the first decl is in an extern "C" context, any other redeclaration
  // will have C language linkage. If the first one is not in an extern "C"
  // context, we would have reported an error for any other decl being in one.
  if (isFirstInExternCContext(&D))
    return CLanguageLinkage;
  return CXXLanguageLinkage;
}

template<typename T>
static bool isDeclExternC(const T &D) {
  // Since the context is ignored for class members, they can only have C++
  // language linkage or no language linkage.
  const DeclContext *DC = D.getDeclContext();
  if (DC->isRecord()) {
    assert(D.getASTContext().getLangOpts().CPlusPlus);
    return false;
  }

  return D.getLanguageLinkage() == CLanguageLinkage;
}

LanguageLinkage VarDecl::getLanguageLinkage() const {
  return getDeclLanguageLinkage(*this);
}

bool VarDecl::isExternC() const {
  return isDeclExternC(*this);
}

bool VarDecl::isInExternCContext() const {
  return getLexicalDeclContext()->isExternCContext();
}

bool VarDecl::isInExternCXXContext() const {
  return getLexicalDeclContext()->isExternCXXContext();
}

VarDecl *VarDecl::getCanonicalDecl() { return getFirstDecl(); }

VarDecl::DefinitionKind
VarDecl::isThisDeclarationADefinition(ASTContext &C) const {
  if (isThisDeclarationADemotedDefinition())
    return DeclarationOnly;

  // C++ [basic.def]p2:
  //   A declaration is a definition unless [...] it contains the 'extern'
  //   specifier or a linkage-specification and neither an initializer [...],
  //   it declares a non-inline static data member in a class declaration [...],
  //   it declares a static data member outside a class definition and the variable
  //   was defined within the class with the constexpr specifier [...],
  // C++1y [temp.expl.spec]p15:
  //   An explicit specialization of a static data member or an explicit
  //   specialization of a static data member template is a definition if the
  //   declaration includes an initializer; otherwise, it is a declaration.
  //
  // FIXME: How do you declare (but not define) a partial specialization of
  // a static data member template outside the containing class?
  if (isStaticDataMember()) {
    if (isOutOfLine() &&
        !(getCanonicalDecl()->isInline() &&
          getCanonicalDecl()->isConstexpr()) &&
        (hasInit() ||
         // If the first declaration is out-of-line, this may be an
         // instantiation of an out-of-line partial specialization of a variable
         // template for which we have not yet instantiated the initializer.
         (getFirstDecl()->isOutOfLine()
              ? getTemplateSpecializationKind() == TSK_Undeclared
              : getTemplateSpecializationKind() !=
                    TSK_ExplicitSpecialization) ||
         isa<VarTemplatePartialSpecializationDecl>(this)))
      return Definition;
    else if (!isOutOfLine() && isInline())
      return Definition;
    else
      return DeclarationOnly;
  }
  // C99 6.7p5:
  //   A definition of an identifier is a declaration for that identifier that
  //   [...] causes storage to be reserved for that object.
  // Note: that applies for all non-file-scope objects.
  // C99 6.9.2p1:
  //   If the declaration of an identifier for an object has file scope and an
  //   initializer, the declaration is an external definition for the identifier
  if (hasInit())
    return Definition;

  if (hasDefiningAttr())
    return Definition;

  if (const auto *SAA = getAttr<SelectAnyAttr>())
    if (!SAA->isInherited())
      return Definition;

  // A variable template specialization (other than a static data member
  // template or an explicit specialization) is a declaration until we
  // instantiate its initializer.
  if (auto *VTSD = dyn_cast<VarTemplateSpecializationDecl>(this)) {
    if (VTSD->getTemplateSpecializationKind() != TSK_ExplicitSpecialization &&
        !isa<VarTemplatePartialSpecializationDecl>(VTSD) &&
        !VTSD->IsCompleteDefinition)
      return DeclarationOnly;
  }

  if (hasExternalStorage())
    return DeclarationOnly;

  // [dcl.link] p7:
  //   A declaration directly contained in a linkage-specification is treated
  //   as if it contains the extern specifier for the purpose of determining
  //   the linkage of the declared name and whether it is a definition.
  if (isSingleLineLanguageLinkage(*this))
    return DeclarationOnly;

  // C99 6.9.2p2:
  //   A declaration of an object that has file scope without an initializer,
  //   and without a storage class specifier or the scs 'static', constitutes
  //   a tentative definition.
  // No such thing in C++.
  if (!C.getLangOpts().CPlusPlus && isFileVarDecl())
    return TentativeDefinition;

  // What's left is (in C, block-scope) declarations without initializers or
  // external storage. These are definitions.
  return Definition;
}

VarDecl *VarDecl::getActingDefinition() {
  DefinitionKind Kind = isThisDeclarationADefinition();
  if (Kind != TentativeDefinition)
    return nullptr;

  VarDecl *LastTentative = nullptr;
  VarDecl *First = getFirstDecl();
  for (auto I : First->redecls()) {
    Kind = I->isThisDeclarationADefinition();
    if (Kind == Definition)
      return nullptr;
    else if (Kind == TentativeDefinition)
      LastTentative = I;
  }
  return LastTentative;
}

VarDecl *VarDecl::getDefinition(ASTContext &C) {
  VarDecl *First = getFirstDecl();
  for (auto I : First->redecls()) {
    if (I->isThisDeclarationADefinition(C) == Definition)
      return I;
  }
  return nullptr;
}

VarDecl::DefinitionKind VarDecl::hasDefinition(ASTContext &C) const {
  DefinitionKind Kind = DeclarationOnly;

  const VarDecl *First = getFirstDecl();
  for (auto I : First->redecls()) {
    Kind = std::max(Kind, I->isThisDeclarationADefinition(C));
    if (Kind == Definition)
      break;
  }

  return Kind;
}

const Expr *VarDecl::getAnyInitializer(const VarDecl *&D) const {
  for (auto I : redecls()) {
    if (auto Expr = I->getInit()) {
      D = I;
      return Expr;
    }
  }
  return nullptr;
}

bool VarDecl::hasInit() const {
  if (auto *P = dyn_cast<ParmVarDecl>(this))
    if (P->hasUnparsedDefaultArg() || P->hasUninstantiatedDefaultArg())
      return false;

  return !Init.isNull();
}

Expr *VarDecl::getInit() {
  if (!hasInit())
    return nullptr;

  if (auto *S = Init.dyn_cast<Stmt *>())
    return cast<Expr>(S);

  return cast_or_null<Expr>(Init.get<EvaluatedStmt *>()->Value);
}

Stmt **VarDecl::getInitAddress() {
  if (auto *ES = Init.dyn_cast<EvaluatedStmt *>())
    return &ES->Value;

  return Init.getAddrOfPtr1();
}

bool VarDecl::isOutOfLine() const {
  if (Decl::isOutOfLine())
    return true;

  if (!isStaticDataMember())
    return false;

  // If this static data member was instantiated from a static data member of
  // a class template, check whether that static data member was defined
  // out-of-line.
  if (VarDecl *VD = getInstantiatedFromStaticDataMember())
    return VD->isOutOfLine();

  return false;
}

void VarDecl::setInit(Expr *I) {
  if (auto *Eval = Init.dyn_cast<EvaluatedStmt *>()) {
    Eval->~EvaluatedStmt();
    getASTContext().Deallocate(Eval);
  }

  Init = I;
}

bool VarDecl::isUsableInConstantExpressions(ASTContext &C) const {
  const LangOptions &Lang = C.getLangOpts();

  if (!Lang.CPlusPlus)
    return false;

  // In C++11, any variable of reference type can be used in a constant
  // expression if it is initialized by a constant expression.
  if (Lang.CPlusPlus11 && getType()->isReferenceType())
    return true;

  // Only const objects can be used in constant expressions in C++. C++98 does
  // not require the variable to be non-volatile, but we consider this to be a
  // defect.
  if (!getType().isConstQualified() || getType().isVolatileQualified())
    return false;

  // In C++, const, non-volatile variables of integral or enumeration types
  // can be used in constant expressions.
  if (getType()->isIntegralOrEnumerationType())
    return true;

  // Additionally, in C++11, non-volatile constexpr variables can be used in
  // constant expressions.
  return Lang.CPlusPlus11 && isConstexpr();
}

/// Convert the initializer for this declaration to the elaborated EvaluatedStmt
/// form, which contains extra information on the evaluated value of the
/// initializer.
EvaluatedStmt *VarDecl::ensureEvaluatedStmt() const {
  auto *Eval = Init.dyn_cast<EvaluatedStmt *>();
  if (!Eval) {
    // Note: EvaluatedStmt contains an APValue, which usually holds
    // resources not allocated from the ASTContext.  We need to do some
    // work to avoid leaking those, but we do so in VarDecl::evaluateValue
    // where we can detect whether there's anything to clean up or not.
    Eval = new (getASTContext()) EvaluatedStmt;
    Eval->Value = Init.get<Stmt *>();
    Init = Eval;
  }
  return Eval;
}

APValue *VarDecl::evaluateValue() const {
  SmallVector<PartialDiagnosticAt, 8> Notes;
  return evaluateValue(Notes);
}

APValue *VarDecl::evaluateValue(
    SmallVectorImpl<PartialDiagnosticAt> &Notes) const {
  EvaluatedStmt *Eval = ensureEvaluatedStmt();

  // We only produce notes indicating why an initializer is non-constant the
  // first time it is evaluated. FIXME: The notes won't always be emitted the
  // first time we try evaluation, so might not be produced at all.
  if (Eval->WasEvaluated)
    return Eval->Evaluated.isUninit() ? nullptr : &Eval->Evaluated;

  const auto *Init = cast<Expr>(Eval->Value);
  assert(!Init->isValueDependent());

  if (Eval->IsEvaluating) {
    // FIXME: Produce a diagnostic for self-initialization.
    Eval->CheckedICE = true;
    Eval->IsICE = false;
    return nullptr;
  }

  Eval->IsEvaluating = true;

  bool Result = Init->EvaluateAsInitializer(Eval->Evaluated, getASTContext(),
                                            this, Notes);

  // Ensure the computed APValue is cleaned up later if evaluation succeeded,
  // or that it's empty (so that there's nothing to clean up) if evaluation
  // failed.
  if (!Result)
    Eval->Evaluated = APValue();
  else if (Eval->Evaluated.needsCleanup())
    getASTContext().addDestruction(&Eval->Evaluated);

  Eval->IsEvaluating = false;
  Eval->WasEvaluated = true;

  // In C++11, we have determined whether the initializer was a constant
  // expression as a side-effect.
  if (getASTContext().getLangOpts().CPlusPlus11 && !Eval->CheckedICE) {
    Eval->CheckedICE = true;
    Eval->IsICE = Result && Notes.empty();
  }

  return Result ? &Eval->Evaluated : nullptr;
}

APValue *VarDecl::getEvaluatedValue() const {
  if (EvaluatedStmt *Eval = Init.dyn_cast<EvaluatedStmt *>())
    if (Eval->WasEvaluated)
      return &Eval->Evaluated;

  return nullptr;
}

bool VarDecl::isInitKnownICE() const {
  if (EvaluatedStmt *Eval = Init.dyn_cast<EvaluatedStmt *>())
    return Eval->CheckedICE;

  return false;
}

bool VarDecl::isInitICE() const {
  assert(isInitKnownICE() &&
         "Check whether we already know that the initializer is an ICE");
  return Init.get<EvaluatedStmt *>()->IsICE;
}

bool VarDecl::checkInitIsICE() const {
  // Initializers of weak variables are never ICEs.
  if (isWeak())
    return false;

  EvaluatedStmt *Eval = ensureEvaluatedStmt();
  if (Eval->CheckedICE)
    // We have already checked whether this subexpression is an
    // integral constant expression.
    return Eval->IsICE;

  const auto *Init = cast<Expr>(Eval->Value);
  assert(!Init->isValueDependent());

  // In C++11, evaluate the initializer to check whether it's a constant
  // expression.
  if (getASTContext().getLangOpts().CPlusPlus11) {
    SmallVector<PartialDiagnosticAt, 8> Notes;
    evaluateValue(Notes);
    return Eval->IsICE;
  }

  // It's an ICE whether or not the definition we found is
  // out-of-line.  See DR 721 and the discussion in Clang PR
  // 6206 for details.

  if (Eval->CheckingICE)
    return false;
  Eval->CheckingICE = true;

  Eval->IsICE = Init->isIntegerConstantExpr(getASTContext());
  Eval->CheckingICE = false;
  Eval->CheckedICE = true;
  return Eval->IsICE;
}

template<typename DeclT>
static DeclT *getDefinitionOrSelf(DeclT *D) {
  assert(D);
  if (auto *Def = D->getDefinition())
    return Def;
  return D;
}

bool VarDecl::isEscapingByref() const {
  return hasAttr<BlocksAttr>() && NonParmVarDeclBits.EscapingByref;
}

bool VarDecl::isNonEscapingByref() const {
  return hasAttr<BlocksAttr>() && !NonParmVarDeclBits.EscapingByref;
}

VarDecl *VarDecl::getTemplateInstantiationPattern() const {
  // If it's a variable template specialization, find the template or partial
  // specialization from which it was instantiated.
  if (auto *VDTemplSpec = dyn_cast<VarTemplateSpecializationDecl>(this)) {
    auto From = VDTemplSpec->getInstantiatedFrom();
    if (auto *VTD = From.dyn_cast<VarTemplateDecl *>()) {
      while (auto *NewVTD = VTD->getInstantiatedFromMemberTemplate()) {
        if (NewVTD->isMemberSpecialization())
          break;
        VTD = NewVTD;
      }
      return getDefinitionOrSelf(VTD->getTemplatedDecl());
    }
    if (auto *VTPSD =
            From.dyn_cast<VarTemplatePartialSpecializationDecl *>()) {
      while (auto *NewVTPSD = VTPSD->getInstantiatedFromMember()) {
        if (NewVTPSD->isMemberSpecialization())
          break;
        VTPSD = NewVTPSD;
      }
      return getDefinitionOrSelf<VarDecl>(VTPSD);
    }
  }

  if (MemberSpecializationInfo *MSInfo = getMemberSpecializationInfo()) {
    if (isTemplateInstantiation(MSInfo->getTemplateSpecializationKind())) {
      VarDecl *VD = getInstantiatedFromStaticDataMember();
      while (auto *NewVD = VD->getInstantiatedFromStaticDataMember())
        VD = NewVD;
      return getDefinitionOrSelf(VD);
    }
  }

  if (VarTemplateDecl *VarTemplate = getDescribedVarTemplate()) {
    while (VarTemplate->getInstantiatedFromMemberTemplate()) {
      if (VarTemplate->isMemberSpecialization())
        break;
      VarTemplate = VarTemplate->getInstantiatedFromMemberTemplate();
    }

    return getDefinitionOrSelf(VarTemplate->getTemplatedDecl());
  }
  return nullptr;
}

VarDecl *VarDecl::getInstantiatedFromStaticDataMember() const {
  if (MemberSpecializationInfo *MSI = getMemberSpecializationInfo())
    return cast<VarDecl>(MSI->getInstantiatedFrom());

  return nullptr;
}

TemplateSpecializationKind VarDecl::getTemplateSpecializationKind() const {
  if (const auto *Spec = dyn_cast<VarTemplateSpecializationDecl>(this))
    return Spec->getSpecializationKind();

  if (MemberSpecializationInfo *MSI = getMemberSpecializationInfo())
    return MSI->getTemplateSpecializationKind();

  return TSK_Undeclared;
}

SourceLocation VarDecl::getPointOfInstantiation() const {
  if (const auto *Spec = dyn_cast<VarTemplateSpecializationDecl>(this))
    return Spec->getPointOfInstantiation();

  if (MemberSpecializationInfo *MSI = getMemberSpecializationInfo())
    return MSI->getPointOfInstantiation();

  return SourceLocation();
}

VarTemplateDecl *VarDecl::getDescribedVarTemplate() const {
  return getASTContext().getTemplateOrSpecializationInfo(this)
      .dyn_cast<VarTemplateDecl *>();
}

void VarDecl::setDescribedVarTemplate(VarTemplateDecl *Template) {
  getASTContext().setTemplateOrSpecializationInfo(this, Template);
}

bool VarDecl::isKnownToBeDefined() const {
  const auto &LangOpts = getASTContext().getLangOpts();
  // In CUDA mode without relocatable device code, variables of form 'extern
  // __shared__ Foo foo[]' are pointers to the base of the GPU core's shared
  // memory pool.  These are never undefined variables, even if they appear
  // inside of an anon namespace or static function.
  //
  // With CUDA relocatable device code enabled, these variables don't get
  // special handling; they're treated like regular extern variables.
  if (LangOpts.CUDA && !LangOpts.GPURelocatableDeviceCode &&
      hasExternalStorage() && hasAttr<CUDASharedAttr>() &&
      isa<IncompleteArrayType>(getType()))
    return true;

  return hasDefinition();
}

bool VarDecl::isNoDestroy(const ASTContext &Ctx) const {
  return hasGlobalStorage() && (hasAttr<NoDestroyAttr>() ||
                                (!Ctx.getLangOpts().RegisterStaticDestructors &&
                                 !hasAttr<AlwaysDestroyAttr>()));
}

MemberSpecializationInfo *VarDecl::getMemberSpecializationInfo() const {
  if (isStaticDataMember())
    // FIXME: Remove ?
    // return getASTContext().getInstantiatedFromStaticDataMember(this);
    return getASTContext().getTemplateOrSpecializationInfo(this)
        .dyn_cast<MemberSpecializationInfo *>();
  return nullptr;
}

void VarDecl::setTemplateSpecializationKind(TemplateSpecializationKind TSK,
                                         SourceLocation PointOfInstantiation) {
  assert((isa<VarTemplateSpecializationDecl>(this) ||
          getMemberSpecializationInfo()) &&
         "not a variable or static data member template specialization");

  if (VarTemplateSpecializationDecl *Spec =
          dyn_cast<VarTemplateSpecializationDecl>(this)) {
    Spec->setSpecializationKind(TSK);
    if (TSK != TSK_ExplicitSpecialization && PointOfInstantiation.isValid() &&
        Spec->getPointOfInstantiation().isInvalid()) {
      Spec->setPointOfInstantiation(PointOfInstantiation);
      if (ASTMutationListener *L = getASTContext().getASTMutationListener())
        L->InstantiationRequested(this);
    }
  }

  if (MemberSpecializationInfo *MSI = getMemberSpecializationInfo()) {
    MSI->setTemplateSpecializationKind(TSK);
    if (TSK != TSK_ExplicitSpecialization && PointOfInstantiation.isValid() &&
        MSI->getPointOfInstantiation().isInvalid()) {
      MSI->setPointOfInstantiation(PointOfInstantiation);
      if (ASTMutationListener *L = getASTContext().getASTMutationListener())
        L->InstantiationRequested(this);
    }
  }
}

void
VarDecl::setInstantiationOfStaticDataMember(VarDecl *VD,
                                            TemplateSpecializationKind TSK) {
  assert(getASTContext().getTemplateOrSpecializationInfo(this).isNull() &&
         "Previous template or instantiation?");
  getASTContext().setInstantiatedFromStaticDataMember(this, VD, TSK);
}

//===----------------------------------------------------------------------===//
// ParmVarDecl Implementation
//===----------------------------------------------------------------------===//

ParmVarDecl *ParmVarDecl::Create(ASTContext &C, DeclContext *DC,
                                 SourceLocation StartLoc,
                                 SourceLocation IdLoc, IdentifierInfo *Id,
                                 QualType T, TypeSourceInfo *TInfo,
                                 StorageClass S, Expr *DefArg) {
  return new (C, DC) ParmVarDecl(ParmVar, C, DC, StartLoc, IdLoc, Id, T, TInfo,
                                 S, DefArg);
}

QualType ParmVarDecl::getOriginalType() const {
  TypeSourceInfo *TSI = getTypeSourceInfo();
  QualType T = TSI ? TSI->getType() : getType();
  if (const auto *DT = dyn_cast<DecayedType>(T))
    return DT->getOriginalType();
  return T;
}

ParmVarDecl *ParmVarDecl::CreateDeserialized(ASTContext &C, unsigned ID) {
  return new (C, ID)
      ParmVarDecl(ParmVar, C, nullptr, SourceLocation(), SourceLocation(),
                  nullptr, QualType(), nullptr, SC_None, nullptr);
}

SourceRange ParmVarDecl::getSourceRange() const {
  if (!hasInheritedDefaultArg()) {
    SourceRange ArgRange = getDefaultArgRange();
    if (ArgRange.isValid())
      return SourceRange(getOuterLocStart(), ArgRange.getEnd());
  }

  // DeclaratorDecl considers the range of postfix types as overlapping with the
  // declaration name, but this is not the case with parameters in ObjC methods.
  if (isa<ObjCMethodDecl>(getDeclContext()))
    return SourceRange(DeclaratorDecl::getBeginLoc(), getLocation());

  return DeclaratorDecl::getSourceRange();
}

Expr *ParmVarDecl::getDefaultArg() {
  assert(!hasUnparsedDefaultArg() && "Default argument is not yet parsed!");
  assert(!hasUninstantiatedDefaultArg() &&
         "Default argument is not yet instantiated!");

  Expr *Arg = getInit();
  if (auto *E = dyn_cast_or_null<FullExpr>(Arg))
    return E->getSubExpr();

  return Arg;
}

void ParmVarDecl::setDefaultArg(Expr *defarg) {
  ParmVarDeclBits.DefaultArgKind = DAK_Normal;
  Init = defarg;
}

SourceRange ParmVarDecl::getDefaultArgRange() const {
  switch (ParmVarDeclBits.DefaultArgKind) {
  case DAK_None:
  case DAK_Unparsed:
    // Nothing we can do here.
    return SourceRange();

  case DAK_Uninstantiated:
    return getUninstantiatedDefaultArg()->getSourceRange();

  case DAK_Normal:
    if (const Expr *E = getInit())
      return E->getSourceRange();

    // Missing an actual expression, may be invalid.
    return SourceRange();
  }
  llvm_unreachable("Invalid default argument kind.");
}

void ParmVarDecl::setUninstantiatedDefaultArg(Expr *arg) {
  ParmVarDeclBits.DefaultArgKind = DAK_Uninstantiated;
  Init = arg;
}

Expr *ParmVarDecl::getUninstantiatedDefaultArg() {
  assert(hasUninstantiatedDefaultArg() &&
         "Wrong kind of initialization expression!");
  return cast_or_null<Expr>(Init.get<Stmt *>());
}

bool ParmVarDecl::hasDefaultArg() const {
  // FIXME: We should just return false for DAK_None here once callers are
  // prepared for the case that we encountered an invalid default argument and
  // were unable to even build an invalid expression.
  return hasUnparsedDefaultArg() || hasUninstantiatedDefaultArg() ||
         !Init.isNull();
}

bool ParmVarDecl::isParameterPack() const {
  return isa<PackExpansionType>(getType());
}

void ParmVarDecl::setParameterIndexLarge(unsigned parameterIndex) {
  getASTContext().setParameterIndex(this, parameterIndex);
  ParmVarDeclBits.ParameterIndex = ParameterIndexSentinel;
}

unsigned ParmVarDecl::getParameterIndexLarge() const {
  return getASTContext().getParameterIndex(this);
}

//===----------------------------------------------------------------------===//
// FunctionDecl Implementation
//===----------------------------------------------------------------------===//

FunctionDecl::FunctionDecl(Kind DK, ASTContext &C, DeclContext *DC,
                           SourceLocation StartLoc,
                           const DeclarationNameInfo &NameInfo, QualType T,
                           TypeSourceInfo *TInfo, StorageClass S,
                           bool isInlineSpecified, bool isConstexprSpecified)
    : DeclaratorDecl(DK, DC, NameInfo.getLoc(), NameInfo.getName(), T, TInfo,
                     StartLoc),
      DeclContext(DK), redeclarable_base(C), ODRHash(0),
      EndRangeLoc(NameInfo.getEndLoc()), DNLoc(NameInfo.getInfo()) {
  assert(T.isNull() || T->isFunctionType());
  FunctionDeclBits.SClass = S;
  FunctionDeclBits.IsInline = isInlineSpecified;
  FunctionDeclBits.IsInlineSpecified = isInlineSpecified;
  FunctionDeclBits.IsExplicitSpecified = false;
  FunctionDeclBits.IsVirtualAsWritten = false;
  FunctionDeclBits.IsPure = false;
  FunctionDeclBits.HasInheritedPrototype = false;
  FunctionDeclBits.HasWrittenPrototype = true;
  FunctionDeclBits.IsDeleted = false;
  FunctionDeclBits.IsTrivial = false;
  FunctionDeclBits.IsTrivialForCall = false;
  FunctionDeclBits.IsDefaulted = false;
  FunctionDeclBits.IsExplicitlyDefaulted = false;
  FunctionDeclBits.HasImplicitReturnZero = false;
  FunctionDeclBits.IsLateTemplateParsed = false;
  FunctionDeclBits.IsConstexpr = isConstexprSpecified;
  FunctionDeclBits.InstantiationIsPending = false;
  FunctionDeclBits.UsesSEHTry = false;
  FunctionDeclBits.HasSkippedBody = false;
  FunctionDeclBits.WillHaveBody = false;
  FunctionDeclBits.IsMultiVersion = false;
  FunctionDeclBits.IsCopyDeductionCandidate = false;
  FunctionDeclBits.HasODRHash = false;
}

void FunctionDecl::getNameForDiagnostic(
    raw_ostream &OS, const PrintingPolicy &Policy, bool Qualified) const {
  NamedDecl::getNameForDiagnostic(OS, Policy, Qualified);
  const TemplateArgumentList *TemplateArgs = getTemplateSpecializationArgs();
  if (TemplateArgs)
    printTemplateArgumentList(OS, TemplateArgs->asArray(), Policy);
}

bool FunctionDecl::isVariadic() const {
  if (const auto *FT = getType()->getAs<FunctionProtoType>())
    return FT->isVariadic();
  return false;
}

bool FunctionDecl::hasBody(const FunctionDecl *&Definition) const {
  for (auto I : redecls()) {
    if (I->doesThisDeclarationHaveABody()) {
      Definition = I;
      return true;
    }
  }

  return false;
}

bool FunctionDecl::hasTrivialBody() const
{
  Stmt *S = getBody();
  if (!S) {
    // Since we don't have a body for this function, we don't know if it's
    // trivial or not.
    return false;
  }

  if (isa<CompoundStmt>(S) && cast<CompoundStmt>(S)->body_empty())
    return true;
  return false;
}

bool FunctionDecl::isDefined(const FunctionDecl *&Definition) const {
  for (auto I : redecls()) {
    if (I->isThisDeclarationADefinition()) {
      Definition = I;
      return true;
    }
  }

  return false;
}

Stmt *FunctionDecl::getBody(const FunctionDecl *&Definition) const {
  if (!hasBody(Definition))
    return nullptr;

  if (Definition->Body)
    return Definition->Body.get(getASTContext().getExternalSource());

  return nullptr;
}

void FunctionDecl::setBody(Stmt *B) {
  Body = B;
  if (B)
    EndRangeLoc = B->getEndLoc();
}

void FunctionDecl::setPure(bool P) {
  FunctionDeclBits.IsPure = P;
  if (P)
    if (auto *Parent = dyn_cast<CXXRecordDecl>(getDeclContext()))
      Parent->markedVirtualFunctionPure();
}

template<std::size_t Len>
static bool isNamed(const NamedDecl *ND, const char (&Str)[Len]) {
  IdentifierInfo *II = ND->getIdentifier();
  return II && II->isStr(Str);
}

bool FunctionDecl::isMain() const {
  const TranslationUnitDecl *tunit =
    dyn_cast<TranslationUnitDecl>(getDeclContext()->getRedeclContext());
  return tunit &&
         !tunit->getASTContext().getLangOpts().Freestanding &&
         isNamed(this, "main");
}

bool FunctionDecl::isMSVCRTEntryPoint() const {
  const TranslationUnitDecl *TUnit =
      dyn_cast<TranslationUnitDecl>(getDeclContext()->getRedeclContext());
  if (!TUnit)
    return false;

  // Even though we aren't really targeting MSVCRT if we are freestanding,
  // semantic analysis for these functions remains the same.

  // MSVCRT entry points only exist on MSVCRT targets.
  if (!TUnit->getASTContext().getTargetInfo().getTriple().isOSMSVCRT())
    return false;

  // Nameless functions like constructors cannot be entry points.
  if (!getIdentifier())
    return false;

  return llvm::StringSwitch<bool>(getName())
      .Cases("main",     // an ANSI console app
             "wmain",    // a Unicode console App
             "WinMain",  // an ANSI GUI app
             "wWinMain", // a Unicode GUI app
             "DllMain",  // a DLL
             true)
      .Default(false);
}

bool FunctionDecl::isReservedGlobalPlacementOperator() const {
  assert(getDeclName().getNameKind() == DeclarationName::CXXOperatorName);
  assert(getDeclName().getCXXOverloadedOperator() == OO_New ||
         getDeclName().getCXXOverloadedOperator() == OO_Delete ||
         getDeclName().getCXXOverloadedOperator() == OO_Array_New ||
         getDeclName().getCXXOverloadedOperator() == OO_Array_Delete);

  if (!getDeclContext()->getRedeclContext()->isTranslationUnit())
    return false;

  const auto *proto = getType()->castAs<FunctionProtoType>();
  if (proto->getNumParams() != 2 || proto->isVariadic())
    return false;

  ASTContext &Context =
    cast<TranslationUnitDecl>(getDeclContext()->getRedeclContext())
      ->getASTContext();

  // The result type and first argument type are constant across all
  // these operators.  The second argument must be exactly void*.
  return (proto->getParamType(1).getCanonicalType() == Context.VoidPtrTy);
}

bool FunctionDecl::isReplaceableGlobalAllocationFunction(bool *IsAligned) const {
  if (getDeclName().getNameKind() != DeclarationName::CXXOperatorName)
    return false;
  if (getDeclName().getCXXOverloadedOperator() != OO_New &&
      getDeclName().getCXXOverloadedOperator() != OO_Delete &&
      getDeclName().getCXXOverloadedOperator() != OO_Array_New &&
      getDeclName().getCXXOverloadedOperator() != OO_Array_Delete)
    return false;

  if (isa<CXXRecordDecl>(getDeclContext()))
    return false;

  // This can only fail for an invalid 'operator new' declaration.
  if (!getDeclContext()->getRedeclContext()->isTranslationUnit())
    return false;

  const auto *FPT = getType()->castAs<FunctionProtoType>();
  if (FPT->getNumParams() == 0 || FPT->getNumParams() > 3 || FPT->isVariadic())
    return false;

  // If this is a single-parameter function, it must be a replaceable global
  // allocation or deallocation function.
  if (FPT->getNumParams() == 1)
    return true;

  unsigned Params = 1;
  QualType Ty = FPT->getParamType(Params);
  ASTContext &Ctx = getASTContext();

  auto Consume = [&] {
    ++Params;
    Ty = Params < FPT->getNumParams() ? FPT->getParamType(Params) : QualType();
  };

  // In C++14, the next parameter can be a 'std::size_t' for sized delete.
  bool IsSizedDelete = false;
  if (Ctx.getLangOpts().SizedDeallocation &&
      (getDeclName().getCXXOverloadedOperator() == OO_Delete ||
       getDeclName().getCXXOverloadedOperator() == OO_Array_Delete) &&
      Ctx.hasSameType(Ty, Ctx.getSizeType())) {
    IsSizedDelete = true;
    Consume();
  }

  // In C++17, the next parameter can be a 'std::align_val_t' for aligned
  // new/delete.
  if (Ctx.getLangOpts().AlignedAllocation && !Ty.isNull() && Ty->isAlignValT()) {
    if (IsAligned)
      *IsAligned = true;
    Consume();
  }

  // Finally, if this is not a sized delete, the final parameter can
  // be a 'const std::nothrow_t&'.
  if (!IsSizedDelete && !Ty.isNull() && Ty->isReferenceType()) {
    Ty = Ty->getPointeeType();
    if (Ty.getCVRQualifiers() != Qualifiers::Const)
      return false;
    const CXXRecordDecl *RD = Ty->getAsCXXRecordDecl();
    if (RD && isNamed(RD, "nothrow_t") && RD->isInStdNamespace())
      Consume();
  }

  return Params == FPT->getNumParams();
}

bool FunctionDecl::isDestroyingOperatorDelete() const {
  // C++ P0722:
  //   Within a class C, a single object deallocation function with signature
  //     (T, std::destroying_delete_t, <more params>)
  //   is a destroying operator delete.
  if (!isa<CXXMethodDecl>(this) || getOverloadedOperator() != OO_Delete ||
      getNumParams() < 2)
    return false;

  auto *RD = getParamDecl(1)->getType()->getAsCXXRecordDecl();
  return RD && RD->isInStdNamespace() && RD->getIdentifier() &&
         RD->getIdentifier()->isStr("destroying_delete_t");
}

LanguageLinkage FunctionDecl::getLanguageLinkage() const {
  return getDeclLanguageLinkage(*this);
}

bool FunctionDecl::isExternC() const {
  return isDeclExternC(*this);
}

bool FunctionDecl::isInExternCContext() const {
  return getLexicalDeclContext()->isExternCContext();
}

bool FunctionDecl::isInExternCXXContext() const {
  return getLexicalDeclContext()->isExternCXXContext();
}

bool FunctionDecl::isGlobal() const {
  if (const auto *Method = dyn_cast<CXXMethodDecl>(this))
    return Method->isStatic();

  if (getCanonicalDecl()->getStorageClass() == SC_Static)
    return false;

  for (const DeclContext *DC = getDeclContext();
       DC->isNamespace();
       DC = DC->getParent()) {
    if (const auto *Namespace = cast<NamespaceDecl>(DC)) {
      if (!Namespace->getDeclName())
        return false;
      break;
    }
  }

  return true;
}

bool FunctionDecl::isNoReturn() const {
  if (hasAttr<NoReturnAttr>() || hasAttr<CXX11NoReturnAttr>() ||
      hasAttr<C11NoReturnAttr>())
    return true;

  if (auto *FnTy = getType()->getAs<FunctionType>())
    return FnTy->getNoReturnAttr();

  return false;
}


MultiVersionKind FunctionDecl::getMultiVersionKind() const {
  if (hasAttr<TargetAttr>())
    return MultiVersionKind::Target;
  if (hasAttr<CPUDispatchAttr>())
    return MultiVersionKind::CPUDispatch;
  if (hasAttr<CPUSpecificAttr>())
    return MultiVersionKind::CPUSpecific;
  return MultiVersionKind::None;
}

bool FunctionDecl::isCPUDispatchMultiVersion() const {
  return isMultiVersion() && hasAttr<CPUDispatchAttr>();
}

bool FunctionDecl::isCPUSpecificMultiVersion() const {
  return isMultiVersion() && hasAttr<CPUSpecificAttr>();
}

bool FunctionDecl::isTargetMultiVersion() const {
  return isMultiVersion() && hasAttr<TargetAttr>();
}

void
FunctionDecl::setPreviousDeclaration(FunctionDecl *PrevDecl) {
  redeclarable_base::setPreviousDecl(PrevDecl);

  if (FunctionTemplateDecl *FunTmpl = getDescribedFunctionTemplate()) {
    FunctionTemplateDecl *PrevFunTmpl
      = PrevDecl? PrevDecl->getDescribedFunctionTemplate() : nullptr;
    assert((!PrevDecl || PrevFunTmpl) && "Function/function template mismatch");
    FunTmpl->setPreviousDecl(PrevFunTmpl);
  }

  if (PrevDecl && PrevDecl->isInlined())
    setImplicitlyInline(true);
}

FunctionDecl *FunctionDecl::getCanonicalDecl() { return getFirstDecl(); }

/// Returns a value indicating whether this function
/// corresponds to a builtin function.
///
/// The function corresponds to a built-in function if it is
/// declared at translation scope or within an extern "C" block and
/// its name matches with the name of a builtin. The returned value
/// will be 0 for functions that do not correspond to a builtin, a
/// value of type \c Builtin::ID if in the target-independent range
/// \c [1,Builtin::First), or a target-specific builtin value.
unsigned FunctionDecl::getBuiltinID() const {
  if (!getIdentifier())
    return 0;

  unsigned BuiltinID = getIdentifier()->getBuiltinID();
  if (!BuiltinID)
    return 0;

  ASTContext &Context = getASTContext();
  if (Context.getLangOpts().CPlusPlus) {
    const auto *LinkageDecl =
        dyn_cast<LinkageSpecDecl>(getFirstDecl()->getDeclContext());
    // In C++, the first declaration of a builtin is always inside an implicit
    // extern "C".
    // FIXME: A recognised library function may not be directly in an extern "C"
    // declaration, for instance "extern "C" { namespace std { decl } }".
    if (!LinkageDecl) {
      if (BuiltinID == Builtin::BI__GetExceptionInfo &&
          Context.getTargetInfo().getCXXABI().isMicrosoft())
        return Builtin::BI__GetExceptionInfo;
      return 0;
    }
    if (LinkageDecl->getLanguage() != LinkageSpecDecl::lang_c)
      return 0;
  }

  // If the function is marked "overloadable", it has a different mangled name
  // and is not the C library function.
  if (hasAttr<OverloadableAttr>())
    return 0;

  if (!Context.BuiltinInfo.isPredefinedLibFunction(BuiltinID))
    return BuiltinID;

  // This function has the name of a known C library
  // function. Determine whether it actually refers to the C library
  // function or whether it just has the same name.

  // If this is a static function, it's not a builtin.
  if (getStorageClass() == SC_Static)
    return 0;

  // OpenCL v1.2 s6.9.f - The library functions defined in
  // the C99 standard headers are not available.
  if (Context.getLangOpts().OpenCL &&
      Context.BuiltinInfo.isPredefinedLibFunction(BuiltinID))
    return 0;

  // CUDA does not have device-side standard library. printf and malloc are the
  // only special cases that are supported by device-side runtime.
  if (Context.getLangOpts().CUDA && hasAttr<CUDADeviceAttr>() &&
      !hasAttr<CUDAHostAttr>() &&
      !(BuiltinID == Builtin::BIprintf || BuiltinID == Builtin::BImalloc))
    return 0;

  return BuiltinID;
}

/// getNumParams - Return the number of parameters this function must have
/// based on its FunctionType.  This is the length of the ParamInfo array
/// after it has been created.
unsigned FunctionDecl::getNumParams() const {
  const auto *FPT = getType()->getAs<FunctionProtoType>();
  return FPT ? FPT->getNumParams() : 0;
}

void FunctionDecl::setParams(ASTContext &C,
                             ArrayRef<ParmVarDecl *> NewParamInfo) {
  assert(!ParamInfo && "Already has param info!");
  assert(NewParamInfo.size() == getNumParams() && "Parameter count mismatch!");

  // Zero params -> null pointer.
  if (!NewParamInfo.empty()) {
    ParamInfo = new (C) ParmVarDecl*[NewParamInfo.size()];
    std::copy(NewParamInfo.begin(), NewParamInfo.end(), ParamInfo);
  }
}

/// getMinRequiredArguments - Returns the minimum number of arguments
/// needed to call this function. This may be fewer than the number of
/// function parameters, if some of the parameters have default
/// arguments (in C++) or are parameter packs (C++11).
unsigned FunctionDecl::getMinRequiredArguments() const {
  if (!getASTContext().getLangOpts().CPlusPlus)
    return getNumParams();

  unsigned NumRequiredArgs = 0;
  for (auto *Param : parameters())
    if (!Param->isParameterPack() && !Param->hasDefaultArg())
      ++NumRequiredArgs;
  return NumRequiredArgs;
}

/// The combination of the extern and inline keywords under MSVC forces
/// the function to be required.
///
/// Note: This function assumes that we will only get called when isInlined()
/// would return true for this FunctionDecl.
bool FunctionDecl::isMSExternInline() const {
  assert(isInlined() && "expected to get called on an inlined function!");

  const ASTContext &Context = getASTContext();
  if (!Context.getTargetInfo().getCXXABI().isMicrosoft() &&
      !hasAttr<DLLExportAttr>())
    return false;

  for (const FunctionDecl *FD = getMostRecentDecl(); FD;
       FD = FD->getPreviousDecl())
    if (!FD->isImplicit() && FD->getStorageClass() == SC_Extern)
      return true;

  return false;
}

static bool redeclForcesDefMSVC(const FunctionDecl *Redecl) {
  if (Redecl->getStorageClass() != SC_Extern)
    return false;

  for (const FunctionDecl *FD = Redecl->getPreviousDecl(); FD;
       FD = FD->getPreviousDecl())
    if (!FD->isImplicit() && FD->getStorageClass() == SC_Extern)
      return false;

  return true;
}

static bool RedeclForcesDefC99(const FunctionDecl *Redecl) {
  // Only consider file-scope declarations in this test.
  if (!Redecl->getLexicalDeclContext()->isTranslationUnit())
    return false;

  // Only consider explicit declarations; the presence of a builtin for a
  // libcall shouldn't affect whether a definition is externally visible.
  if (Redecl->isImplicit())
    return false;

  if (!Redecl->isInlineSpecified() || Redecl->getStorageClass() == SC_Extern)
    return true; // Not an inline definition

  return false;
}

/// For a function declaration in C or C++, determine whether this
/// declaration causes the definition to be externally visible.
///
/// For instance, this determines if adding the current declaration to the set
/// of redeclarations of the given functions causes
/// isInlineDefinitionExternallyVisible to change from false to true.
bool FunctionDecl::doesDeclarationForceExternallyVisibleDefinition() const {
  assert(!doesThisDeclarationHaveABody() &&
         "Must have a declaration without a body.");

  ASTContext &Context = getASTContext();

  if (Context.getLangOpts().MSVCCompat) {
    const FunctionDecl *Definition;
    if (hasBody(Definition) && Definition->isInlined() &&
        redeclForcesDefMSVC(this))
      return true;
  }

  if (Context.getLangOpts().GNUInline || hasAttr<GNUInlineAttr>()) {
    // With GNU inlining, a declaration with 'inline' but not 'extern', forces
    // an externally visible definition.
    //
    // FIXME: What happens if gnu_inline gets added on after the first
    // declaration?
    if (!isInlineSpecified() || getStorageClass() == SC_Extern)
      return false;

    const FunctionDecl *Prev = this;
    bool FoundBody = false;
    while ((Prev = Prev->getPreviousDecl())) {
      FoundBody |= Prev->Body.isValid();

      if (Prev->Body) {
        // If it's not the case that both 'inline' and 'extern' are
        // specified on the definition, then it is always externally visible.
        if (!Prev->isInlineSpecified() ||
            Prev->getStorageClass() != SC_Extern)
          return false;
      } else if (Prev->isInlineSpecified() &&
                 Prev->getStorageClass() != SC_Extern) {
        return false;
      }
    }
    return FoundBody;
  }

  if (Context.getLangOpts().CPlusPlus)
    return false;

  // C99 6.7.4p6:
  //   [...] If all of the file scope declarations for a function in a
  //   translation unit include the inline function specifier without extern,
  //   then the definition in that translation unit is an inline definition.
  if (isInlineSpecified() && getStorageClass() != SC_Extern)
    return false;
  const FunctionDecl *Prev = this;
  bool FoundBody = false;
  while ((Prev = Prev->getPreviousDecl())) {
    FoundBody |= Prev->Body.isValid();
    if (RedeclForcesDefC99(Prev))
      return false;
  }
  return FoundBody;
}

SourceRange FunctionDecl::getReturnTypeSourceRange() const {
  const TypeSourceInfo *TSI = getTypeSourceInfo();
  if (!TSI)
    return SourceRange();
  FunctionTypeLoc FTL =
      TSI->getTypeLoc().IgnoreParens().getAs<FunctionTypeLoc>();
  if (!FTL)
    return SourceRange();

  // Skip self-referential return types.
  const SourceManager &SM = getASTContext().getSourceManager();
  SourceRange RTRange = FTL.getReturnLoc().getSourceRange();
  SourceLocation Boundary = getNameInfo().getBeginLoc();
  if (RTRange.isInvalid() || Boundary.isInvalid() ||
      !SM.isBeforeInTranslationUnit(RTRange.getEnd(), Boundary))
    return SourceRange();

  return RTRange;
}

SourceRange FunctionDecl::getExceptionSpecSourceRange() const {
  const TypeSourceInfo *TSI = getTypeSourceInfo();
  if (!TSI)
    return SourceRange();
  FunctionTypeLoc FTL =
    TSI->getTypeLoc().IgnoreParens().getAs<FunctionTypeLoc>();
  if (!FTL)
    return SourceRange();

  return FTL.getExceptionSpecRange();
}

/// For an inline function definition in C, or for a gnu_inline function
/// in C++, determine whether the definition will be externally visible.
///
/// Inline function definitions are always available for inlining optimizations.
/// However, depending on the language dialect, declaration specifiers, and
/// attributes, the definition of an inline function may or may not be
/// "externally" visible to other translation units in the program.
///
/// In C99, inline definitions are not externally visible by default. However,
/// if even one of the global-scope declarations is marked "extern inline", the
/// inline definition becomes externally visible (C99 6.7.4p6).
///
/// In GNU89 mode, or if the gnu_inline attribute is attached to the function
/// definition, we use the GNU semantics for inline, which are nearly the
/// opposite of C99 semantics. In particular, "inline" by itself will create
/// an externally visible symbol, but "extern inline" will not create an
/// externally visible symbol.
bool FunctionDecl::isInlineDefinitionExternallyVisible() const {
  assert((doesThisDeclarationHaveABody() || willHaveBody()) &&
         "Must be a function definition");
  assert(isInlined() && "Function must be inline");
  ASTContext &Context = getASTContext();

  if (Context.getLangOpts().GNUInline || hasAttr<GNUInlineAttr>()) {
    // Note: If you change the logic here, please change
    // doesDeclarationForceExternallyVisibleDefinition as well.
    //
    // If it's not the case that both 'inline' and 'extern' are
    // specified on the definition, then this inline definition is
    // externally visible.
    if (!(isInlineSpecified() && getStorageClass() == SC_Extern))
      return true;

    // If any declaration is 'inline' but not 'extern', then this definition
    // is externally visible.
    for (auto Redecl : redecls()) {
      if (Redecl->isInlineSpecified() &&
          Redecl->getStorageClass() != SC_Extern)
        return true;
    }

    return false;
  }

  // The rest of this function is C-only.
  assert(!Context.getLangOpts().CPlusPlus &&
         "should not use C inline rules in C++");

  // C99 6.7.4p6:
  //   [...] If all of the file scope declarations for a function in a
  //   translation unit include the inline function specifier without extern,
  //   then the definition in that translation unit is an inline definition.
  for (auto Redecl : redecls()) {
    if (RedeclForcesDefC99(Redecl))
      return true;
  }

  // C99 6.7.4p6:
  //   An inline definition does not provide an external definition for the
  //   function, and does not forbid an external definition in another
  //   translation unit.
  return false;
}

/// getOverloadedOperator - Which C++ overloaded operator this
/// function represents, if any.
OverloadedOperatorKind FunctionDecl::getOverloadedOperator() const {
  if (getDeclName().getNameKind() == DeclarationName::CXXOperatorName)
    return getDeclName().getCXXOverloadedOperator();
  else
    return OO_None;
}

/// getLiteralIdentifier - The literal suffix identifier this function
/// represents, if any.
const IdentifierInfo *FunctionDecl::getLiteralIdentifier() const {
  if (getDeclName().getNameKind() == DeclarationName::CXXLiteralOperatorName)
    return getDeclName().getCXXLiteralIdentifier();
  else
    return nullptr;
}

FunctionDecl::TemplatedKind FunctionDecl::getTemplatedKind() const {
  if (TemplateOrSpecialization.isNull())
    return TK_NonTemplate;
  if (TemplateOrSpecialization.is<FunctionTemplateDecl *>())
    return TK_FunctionTemplate;
  if (TemplateOrSpecialization.is<MemberSpecializationInfo *>())
    return TK_MemberSpecialization;
  if (TemplateOrSpecialization.is<FunctionTemplateSpecializationInfo *>())
    return TK_FunctionTemplateSpecialization;
  if (TemplateOrSpecialization.is
                               <DependentFunctionTemplateSpecializationInfo*>())
    return TK_DependentFunctionTemplateSpecialization;

  llvm_unreachable("Did we miss a TemplateOrSpecialization type?");
}

FunctionDecl *FunctionDecl::getInstantiatedFromMemberFunction() const {
  if (MemberSpecializationInfo *Info = getMemberSpecializationInfo())
    return cast<FunctionDecl>(Info->getInstantiatedFrom());

  return nullptr;
}

MemberSpecializationInfo *FunctionDecl::getMemberSpecializationInfo() const {
  return TemplateOrSpecialization.dyn_cast<MemberSpecializationInfo *>();
}

void
FunctionDecl::setInstantiationOfMemberFunction(ASTContext &C,
                                               FunctionDecl *FD,
                                               TemplateSpecializationKind TSK) {
  assert(TemplateOrSpecialization.isNull() &&
         "Member function is already a specialization");
  MemberSpecializationInfo *Info
    = new (C) MemberSpecializationInfo(FD, TSK);
  TemplateOrSpecialization = Info;
}

FunctionTemplateDecl *FunctionDecl::getDescribedFunctionTemplate() const {
  return TemplateOrSpecialization.dyn_cast<FunctionTemplateDecl *>();
}

void FunctionDecl::setDescribedFunctionTemplate(FunctionTemplateDecl *Template) {
  TemplateOrSpecialization = Template;
}

bool FunctionDecl::isImplicitlyInstantiable() const {
  // If the function is invalid, it can't be implicitly instantiated.
  if (isInvalidDecl())
    return false;

  switch (getTemplateSpecializationKind()) {
  case TSK_Undeclared:
  case TSK_ExplicitInstantiationDefinition:
    return false;

  case TSK_ImplicitInstantiation:
    return true;

  // It is possible to instantiate TSK_ExplicitSpecialization kind
  // if the FunctionDecl has a class scope specialization pattern.
  case TSK_ExplicitSpecialization:
    return getClassScopeSpecializationPattern() != nullptr;

  case TSK_ExplicitInstantiationDeclaration:
    // Handled below.
    break;
  }

  // Find the actual template from which we will instantiate.
  const FunctionDecl *PatternDecl = getTemplateInstantiationPattern();
  bool HasPattern = false;
  if (PatternDecl)
    HasPattern = PatternDecl->hasBody(PatternDecl);

  // C++0x [temp.explicit]p9:
  //   Except for inline functions, other explicit instantiation declarations
  //   have the effect of suppressing the implicit instantiation of the entity
  //   to which they refer.
  if (!HasPattern || !PatternDecl)
    return true;

  return PatternDecl->isInlined();
}

bool FunctionDecl::isTemplateInstantiation() const {
  switch (getTemplateSpecializationKind()) {
    case TSK_Undeclared:
    case TSK_ExplicitSpecialization:
      return false;
    case TSK_ImplicitInstantiation:
    case TSK_ExplicitInstantiationDeclaration:
    case TSK_ExplicitInstantiationDefinition:
      return true;
  }
  llvm_unreachable("All TSK values handled.");
}

FunctionDecl *FunctionDecl::getTemplateInstantiationPattern() const {
  // Handle class scope explicit specialization special case.
  if (getTemplateSpecializationKind() == TSK_ExplicitSpecialization) {
    if (auto *Spec = getClassScopeSpecializationPattern())
      return getDefinitionOrSelf(Spec);
    return nullptr;
  }

  // If this is a generic lambda call operator specialization, its
  // instantiation pattern is always its primary template's pattern
  // even if its primary template was instantiated from another
  // member template (which happens with nested generic lambdas).
  // Since a lambda's call operator's body is transformed eagerly,
  // we don't have to go hunting for a prototype definition template
  // (i.e. instantiated-from-member-template) to use as an instantiation
  // pattern.

  if (isGenericLambdaCallOperatorSpecialization(
          dyn_cast<CXXMethodDecl>(this))) {
    assert(getPrimaryTemplate() && "not a generic lambda call operator?");
    return getDefinitionOrSelf(getPrimaryTemplate()->getTemplatedDecl());
  }

  if (FunctionTemplateDecl *Primary = getPrimaryTemplate()) {
    while (Primary->getInstantiatedFromMemberTemplate()) {
      // If we have hit a point where the user provided a specialization of
      // this template, we're done looking.
      if (Primary->isMemberSpecialization())
        break;
      Primary = Primary->getInstantiatedFromMemberTemplate();
    }

    return getDefinitionOrSelf(Primary->getTemplatedDecl());
  }

  if (auto *MFD = getInstantiatedFromMemberFunction())
    return getDefinitionOrSelf(MFD);

  return nullptr;
}

FunctionTemplateDecl *FunctionDecl::getPrimaryTemplate() const {
  if (FunctionTemplateSpecializationInfo *Info
        = TemplateOrSpecialization
            .dyn_cast<FunctionTemplateSpecializationInfo*>()) {
    return Info->Template.getPointer();
  }
  return nullptr;
}

FunctionDecl *FunctionDecl::getClassScopeSpecializationPattern() const {
    return getASTContext().getClassScopeSpecializationPattern(this);
}

FunctionTemplateSpecializationInfo *
FunctionDecl::getTemplateSpecializationInfo() const {
  return TemplateOrSpecialization
      .dyn_cast<FunctionTemplateSpecializationInfo *>();
}

const TemplateArgumentList *
FunctionDecl::getTemplateSpecializationArgs() const {
  if (FunctionTemplateSpecializationInfo *Info
        = TemplateOrSpecialization
            .dyn_cast<FunctionTemplateSpecializationInfo*>()) {
    return Info->TemplateArguments;
  }
  return nullptr;
}

const ASTTemplateArgumentListInfo *
FunctionDecl::getTemplateSpecializationArgsAsWritten() const {
  if (FunctionTemplateSpecializationInfo *Info
        = TemplateOrSpecialization
            .dyn_cast<FunctionTemplateSpecializationInfo*>()) {
    return Info->TemplateArgumentsAsWritten;
  }
  return nullptr;
}

void
FunctionDecl::setFunctionTemplateSpecialization(ASTContext &C,
                                                FunctionTemplateDecl *Template,
                                     const TemplateArgumentList *TemplateArgs,
                                                void *InsertPos,
                                                TemplateSpecializationKind TSK,
                        const TemplateArgumentListInfo *TemplateArgsAsWritten,
                                          SourceLocation PointOfInstantiation) {
  assert(TSK != TSK_Undeclared &&
         "Must specify the type of function template specialization");
  FunctionTemplateSpecializationInfo *Info
    = TemplateOrSpecialization.dyn_cast<FunctionTemplateSpecializationInfo*>();
  if (!Info)
    Info = FunctionTemplateSpecializationInfo::Create(C, this, Template, TSK,
                                                      TemplateArgs,
                                                      TemplateArgsAsWritten,
                                                      PointOfInstantiation);
  TemplateOrSpecialization = Info;
  Template->addSpecialization(Info, InsertPos);
}

void
FunctionDecl::setDependentTemplateSpecialization(ASTContext &Context,
                                    const UnresolvedSetImpl &Templates,
                             const TemplateArgumentListInfo &TemplateArgs) {
  assert(TemplateOrSpecialization.isNull());
  DependentFunctionTemplateSpecializationInfo *Info =
      DependentFunctionTemplateSpecializationInfo::Create(Context, Templates,
                                                          TemplateArgs);
  TemplateOrSpecialization = Info;
}

DependentFunctionTemplateSpecializationInfo *
FunctionDecl::getDependentSpecializationInfo() const {
  return TemplateOrSpecialization
      .dyn_cast<DependentFunctionTemplateSpecializationInfo *>();
}

DependentFunctionTemplateSpecializationInfo *
DependentFunctionTemplateSpecializationInfo::Create(
    ASTContext &Context, const UnresolvedSetImpl &Ts,
    const TemplateArgumentListInfo &TArgs) {
  void *Buffer = Context.Allocate(
      totalSizeToAlloc<TemplateArgumentLoc, FunctionTemplateDecl *>(
          TArgs.size(), Ts.size()));
  return new (Buffer) DependentFunctionTemplateSpecializationInfo(Ts, TArgs);
}

DependentFunctionTemplateSpecializationInfo::
DependentFunctionTemplateSpecializationInfo(const UnresolvedSetImpl &Ts,
                                      const TemplateArgumentListInfo &TArgs)
  : AngleLocs(TArgs.getLAngleLoc(), TArgs.getRAngleLoc()) {
  NumTemplates = Ts.size();
  NumArgs = TArgs.size();

  FunctionTemplateDecl **TsArray = getTrailingObjects<FunctionTemplateDecl *>();
  for (unsigned I = 0, E = Ts.size(); I != E; ++I)
    TsArray[I] = cast<FunctionTemplateDecl>(Ts[I]->getUnderlyingDecl());

  TemplateArgumentLoc *ArgsArray = getTrailingObjects<TemplateArgumentLoc>();
  for (unsigned I = 0, E = TArgs.size(); I != E; ++I)
    new (&ArgsArray[I]) TemplateArgumentLoc(TArgs[I]);
}

TemplateSpecializationKind FunctionDecl::getTemplateSpecializationKind() const {
  // For a function template specialization, query the specialization
  // information object.
  FunctionTemplateSpecializationInfo *FTSInfo
    = TemplateOrSpecialization.dyn_cast<FunctionTemplateSpecializationInfo*>();
  if (FTSInfo)
    return FTSInfo->getTemplateSpecializationKind();

  MemberSpecializationInfo *MSInfo
    = TemplateOrSpecialization.dyn_cast<MemberSpecializationInfo*>();
  if (MSInfo)
    return MSInfo->getTemplateSpecializationKind();

  return TSK_Undeclared;
}

void
FunctionDecl::setTemplateSpecializationKind(TemplateSpecializationKind TSK,
                                          SourceLocation PointOfInstantiation) {
  if (FunctionTemplateSpecializationInfo *FTSInfo
        = TemplateOrSpecialization.dyn_cast<
                                    FunctionTemplateSpecializationInfo*>()) {
    FTSInfo->setTemplateSpecializationKind(TSK);
    if (TSK != TSK_ExplicitSpecialization &&
        PointOfInstantiation.isValid() &&
        FTSInfo->getPointOfInstantiation().isInvalid()) {
      FTSInfo->setPointOfInstantiation(PointOfInstantiation);
      if (ASTMutationListener *L = getASTContext().getASTMutationListener())
        L->InstantiationRequested(this);
    }
  } else if (MemberSpecializationInfo *MSInfo
             = TemplateOrSpecialization.dyn_cast<MemberSpecializationInfo*>()) {
    MSInfo->setTemplateSpecializationKind(TSK);
    if (TSK != TSK_ExplicitSpecialization &&
        PointOfInstantiation.isValid() &&
        MSInfo->getPointOfInstantiation().isInvalid()) {
      MSInfo->setPointOfInstantiation(PointOfInstantiation);
      if (ASTMutationListener *L = getASTContext().getASTMutationListener())
        L->InstantiationRequested(this);
    }
  } else
    llvm_unreachable("Function cannot have a template specialization kind");
}

SourceLocation FunctionDecl::getPointOfInstantiation() const {
  if (FunctionTemplateSpecializationInfo *FTSInfo
        = TemplateOrSpecialization.dyn_cast<
                                        FunctionTemplateSpecializationInfo*>())
    return FTSInfo->getPointOfInstantiation();
  else if (MemberSpecializationInfo *MSInfo
             = TemplateOrSpecialization.dyn_cast<MemberSpecializationInfo*>())
    return MSInfo->getPointOfInstantiation();

  return SourceLocation();
}

bool FunctionDecl::isOutOfLine() const {
  if (Decl::isOutOfLine())
    return true;

  // If this function was instantiated from a member function of a
  // class template, check whether that member function was defined out-of-line.
  if (FunctionDecl *FD = getInstantiatedFromMemberFunction()) {
    const FunctionDecl *Definition;
    if (FD->hasBody(Definition))
      return Definition->isOutOfLine();
  }

  // If this function was instantiated from a function template,
  // check whether that function template was defined out-of-line.
  if (FunctionTemplateDecl *FunTmpl = getPrimaryTemplate()) {
    const FunctionDecl *Definition;
    if (FunTmpl->getTemplatedDecl()->hasBody(Definition))
      return Definition->isOutOfLine();
  }

  return false;
}

SourceRange FunctionDecl::getSourceRange() const {
  return SourceRange(getOuterLocStart(), EndRangeLoc);
}

unsigned FunctionDecl::getMemoryFunctionKind() const {
  IdentifierInfo *FnInfo = getIdentifier();

  if (!FnInfo)
    return 0;

  // Builtin handling.
  switch (getBuiltinID()) {
  case Builtin::BI__builtin_memset:
  case Builtin::BI__builtin___memset_chk:
  case Builtin::BImemset:
    return Builtin::BImemset;

  case Builtin::BI__builtin_memcpy:
  case Builtin::BI__builtin___memcpy_chk:
  case Builtin::BImemcpy:
    return Builtin::BImemcpy;

  case Builtin::BI__builtin_memmove:
  case Builtin::BI__builtin___memmove_chk:
  case Builtin::BImemmove:
    return Builtin::BImemmove;

  case Builtin::BIstrlcpy:
  case Builtin::BI__builtin___strlcpy_chk:
    return Builtin::BIstrlcpy;

  case Builtin::BIstrlcat:
  case Builtin::BI__builtin___strlcat_chk:
    return Builtin::BIstrlcat;

  case Builtin::BI__builtin_memcmp:
  case Builtin::BImemcmp:
    return Builtin::BImemcmp;

  case Builtin::BI__builtin_strncpy:
  case Builtin::BI__builtin___strncpy_chk:
  case Builtin::BIstrncpy:
    return Builtin::BIstrncpy;

  case Builtin::BI__builtin_strncmp:
  case Builtin::BIstrncmp:
    return Builtin::BIstrncmp;

  case Builtin::BI__builtin_strncasecmp:
  case Builtin::BIstrncasecmp:
    return Builtin::BIstrncasecmp;

  case Builtin::BI__builtin_strncat:
  case Builtin::BI__builtin___strncat_chk:
  case Builtin::BIstrncat:
    return Builtin::BIstrncat;

  case Builtin::BI__builtin_strndup:
  case Builtin::BIstrndup:
    return Builtin::BIstrndup;

  case Builtin::BI__builtin_strlen:
  case Builtin::BIstrlen:
    return Builtin::BIstrlen;

  case Builtin::BI__builtin_bzero:
  case Builtin::BIbzero:
    return Builtin::BIbzero;

  default:
    if (isExternC()) {
      if (FnInfo->isStr("memset"))
        return Builtin::BImemset;
      else if (FnInfo->isStr("memcpy"))
        return Builtin::BImemcpy;
      else if (FnInfo->isStr("memmove"))
        return Builtin::BImemmove;
      else if (FnInfo->isStr("memcmp"))
        return Builtin::BImemcmp;
      else if (FnInfo->isStr("strncpy"))
        return Builtin::BIstrncpy;
      else if (FnInfo->isStr("strncmp"))
        return Builtin::BIstrncmp;
      else if (FnInfo->isStr("strncasecmp"))
        return Builtin::BIstrncasecmp;
      else if (FnInfo->isStr("strncat"))
        return Builtin::BIstrncat;
      else if (FnInfo->isStr("strndup"))
        return Builtin::BIstrndup;
      else if (FnInfo->isStr("strlen"))
        return Builtin::BIstrlen;
      else if (FnInfo->isStr("bzero"))
        return Builtin::BIbzero;
    }
    break;
  }
  return 0;
}

unsigned FunctionDecl::getODRHash() const {
  assert(hasODRHash());
  return ODRHash;
}

unsigned FunctionDecl::getODRHash() {
  if (hasODRHash())
    return ODRHash;

  if (auto *FT = getInstantiatedFromMemberFunction()) {
    setHasODRHash(true);
    ODRHash = FT->getODRHash();
    return ODRHash;
  }

  class ODRHash Hash;
  Hash.AddFunctionDecl(this);
  setHasODRHash(true);
  ODRHash = Hash.CalculateHash();
  return ODRHash;
}

//===----------------------------------------------------------------------===//
// FieldDecl Implementation
//===----------------------------------------------------------------------===//

FieldDecl *FieldDecl::Create(const ASTContext &C, DeclContext *DC,
                             SourceLocation StartLoc, SourceLocation IdLoc,
                             IdentifierInfo *Id, QualType T,
                             TypeSourceInfo *TInfo, Expr *BW, bool Mutable,
                             InClassInitStyle InitStyle) {
  return new (C, DC) FieldDecl(Decl::Field, DC, StartLoc, IdLoc, Id, T, TInfo,
                               BW, Mutable, InitStyle);
}

FieldDecl *FieldDecl::CreateDeserialized(ASTContext &C, unsigned ID) {
  return new (C, ID) FieldDecl(Field, nullptr, SourceLocation(),
                               SourceLocation(), nullptr, QualType(), nullptr,
                               nullptr, false, ICIS_NoInit);
}

bool FieldDecl::isAnonymousStructOrUnion() const {
  if (!isImplicit() || getDeclName())
    return false;

  if (const auto *Record = getType()->getAs<RecordType>())
    return Record->getDecl()->isAnonymousStructOrUnion();

  return false;
}

unsigned FieldDecl::getBitWidthValue(const ASTContext &Ctx) const {
  assert(isBitField() && "not a bitfield");
  return getBitWidth()->EvaluateKnownConstInt(Ctx).getZExtValue();
}

bool FieldDecl::isZeroLengthBitField(const ASTContext &Ctx) const {
  return isUnnamedBitfield() && !getBitWidth()->isValueDependent() &&
         getBitWidthValue(Ctx) == 0;
}

unsigned FieldDecl::getFieldIndex() const {
  const FieldDecl *Canonical = getCanonicalDecl();
  if (Canonical != this)
    return Canonical->getFieldIndex();

  if (CachedFieldIndex) return CachedFieldIndex - 1;

  unsigned Index = 0;
  const RecordDecl *RD = getParent()->getDefinition();
  assert(RD && "requested index for field of struct with no definition");

  for (auto *Field : RD->fields()) {
    Field->getCanonicalDecl()->CachedFieldIndex = Index + 1;
    ++Index;
  }

  assert(CachedFieldIndex && "failed to find field in parent");
  return CachedFieldIndex - 1;
}

SourceRange FieldDecl::getSourceRange() const {
  const Expr *FinalExpr = getInClassInitializer();
  if (!FinalExpr)
    FinalExpr = getBitWidth();
  if (FinalExpr)
    return SourceRange(getInnerLocStart(), FinalExpr->getEndLoc());
  return DeclaratorDecl::getSourceRange();
}

void FieldDecl::setCapturedVLAType(const VariableArrayType *VLAType) {
  assert((getParent()->isLambda() || getParent()->isCapturedRecord()) &&
         "capturing type in non-lambda or captured record.");
  assert(InitStorage.getInt() == ISK_NoInit &&
         InitStorage.getPointer() == nullptr &&
         "bit width, initializer or captured type already set");
  InitStorage.setPointerAndInt(const_cast<VariableArrayType *>(VLAType),
                               ISK_CapturedVLAType);
}

//===----------------------------------------------------------------------===//
// TagDecl Implementation
//===----------------------------------------------------------------------===//

TagDecl::TagDecl(Kind DK, TagKind TK, const ASTContext &C, DeclContext *DC,
                 SourceLocation L, IdentifierInfo *Id, TagDecl *PrevDecl,
                 SourceLocation StartL)
    : TypeDecl(DK, DC, L, Id, StartL), DeclContext(DK), redeclarable_base(C),
      TypedefNameDeclOrQualifier((TypedefNameDecl *)nullptr) {
  assert((DK != Enum || TK == TTK_Enum) &&
         "EnumDecl not matched with TTK_Enum");
  setPreviousDecl(PrevDecl);
  setTagKind(TK);
  setCompleteDefinition(false);
  setBeingDefined(false);
  setEmbeddedInDeclarator(false);
  setFreeStanding(false);
  setCompleteDefinitionRequired(false);
}

SourceLocation TagDecl::getOuterLocStart() const {
  return getTemplateOrInnerLocStart(this);
}

SourceRange TagDecl::getSourceRange() const {
  SourceLocation RBraceLoc = BraceRange.getEnd();
  SourceLocation E = RBraceLoc.isValid() ? RBraceLoc : getLocation();
  return SourceRange(getOuterLocStart(), E);
}

TagDecl *TagDecl::getCanonicalDecl() { return getFirstDecl(); }

void TagDecl::setTypedefNameForAnonDecl(TypedefNameDecl *TDD) {
  TypedefNameDeclOrQualifier = TDD;
  if (const Type *T = getTypeForDecl()) {
    (void)T;
    assert(T->isLinkageValid());
  }
  assert(isLinkageValid());
}

void TagDecl::startDefinition() {
  setBeingDefined(true);

  if (auto *D = dyn_cast<CXXRecordDecl>(this)) {
    struct CXXRecordDecl::DefinitionData *Data =
      new (getASTContext()) struct CXXRecordDecl::DefinitionData(D);
    for (auto I : redecls())
      cast<CXXRecordDecl>(I)->DefinitionData = Data;
  }
}

void TagDecl::completeDefinition() {
  assert((!isa<CXXRecordDecl>(this) ||
          cast<CXXRecordDecl>(this)->hasDefinition()) &&
         "definition completed but not started");

  setCompleteDefinition(true);
  setBeingDefined(false);

  if (ASTMutationListener *L = getASTMutationListener())
    L->CompletedTagDefinition(this);
}

TagDecl *TagDecl::getDefinition() const {
  if (isCompleteDefinition())
    return const_cast<TagDecl *>(this);

  // If it's possible for us to have an out-of-date definition, check now.
  if (mayHaveOutOfDateDef()) {
    if (IdentifierInfo *II = getIdentifier()) {
      if (II->isOutOfDate()) {
        updateOutOfDate(*II);
      }
    }
  }

  if (const auto *CXXRD = dyn_cast<CXXRecordDecl>(this))
    return CXXRD->getDefinition();

  for (auto R : redecls())
    if (R->isCompleteDefinition())
      return R;

  return nullptr;
}

void TagDecl::setQualifierInfo(NestedNameSpecifierLoc QualifierLoc) {
  if (QualifierLoc) {
    // Make sure the extended qualifier info is allocated.
    if (!hasExtInfo())
      TypedefNameDeclOrQualifier = new (getASTContext()) ExtInfo;
    // Set qualifier info.
    getExtInfo()->QualifierLoc = QualifierLoc;
  } else {
    // Here Qualifier == 0, i.e., we are removing the qualifier (if any).
    if (hasExtInfo()) {
      if (getExtInfo()->NumTemplParamLists == 0) {
        getASTContext().Deallocate(getExtInfo());
        TypedefNameDeclOrQualifier = (TypedefNameDecl *)nullptr;
      }
      else
        getExtInfo()->QualifierLoc = QualifierLoc;
    }
  }
}

void TagDecl::setTemplateParameterListsInfo(
    ASTContext &Context, ArrayRef<TemplateParameterList *> TPLists) {
  assert(!TPLists.empty());
  // Make sure the extended decl info is allocated.
  if (!hasExtInfo())
    // Allocate external info struct.
    TypedefNameDeclOrQualifier = new (getASTContext()) ExtInfo;
  // Set the template parameter lists info.
  getExtInfo()->setTemplateParameterListsInfo(Context, TPLists);
}

//===----------------------------------------------------------------------===//
// EnumDecl Implementation
//===----------------------------------------------------------------------===//

EnumDecl::EnumDecl(ASTContext &C, DeclContext *DC, SourceLocation StartLoc,
                   SourceLocation IdLoc, IdentifierInfo *Id, EnumDecl *PrevDecl,
                   bool Scoped, bool ScopedUsingClassTag, bool Fixed)
    : TagDecl(Enum, TTK_Enum, C, DC, IdLoc, Id, PrevDecl, StartLoc) {
  assert(Scoped || !ScopedUsingClassTag);
  IntegerType = nullptr;
  setNumPositiveBits(0);
  setNumNegativeBits(0);
  setScoped(Scoped);
  setScopedUsingClassTag(ScopedUsingClassTag);
  setFixed(Fixed);
  setHasODRHash(false);
  ODRHash = 0;
}

void EnumDecl::anchor() {}

EnumDecl *EnumDecl::Create(ASTContext &C, DeclContext *DC,
                           SourceLocation StartLoc, SourceLocation IdLoc,
                           IdentifierInfo *Id,
                           EnumDecl *PrevDecl, bool IsScoped,
                           bool IsScopedUsingClassTag, bool IsFixed) {
  auto *Enum = new (C, DC) EnumDecl(C, DC, StartLoc, IdLoc, Id, PrevDecl,
                                    IsScoped, IsScopedUsingClassTag, IsFixed);
  Enum->setMayHaveOutOfDateDef(C.getLangOpts().Modules);
  C.getTypeDeclType(Enum, PrevDecl);
  return Enum;
}

EnumDecl *EnumDecl::CreateDeserialized(ASTContext &C, unsigned ID) {
  EnumDecl *Enum =
      new (C, ID) EnumDecl(C, nullptr, SourceLocation(), SourceLocation(),
                           nullptr, nullptr, false, false, false);
  Enum->setMayHaveOutOfDateDef(C.getLangOpts().Modules);
  return Enum;
}

SourceRange EnumDecl::getIntegerTypeRange() const {
  if (const TypeSourceInfo *TI = getIntegerTypeSourceInfo())
    return TI->getTypeLoc().getSourceRange();
  return SourceRange();
}

void EnumDecl::completeDefinition(QualType NewType,
                                  QualType NewPromotionType,
                                  unsigned NumPositiveBits,
                                  unsigned NumNegativeBits) {
  assert(!isCompleteDefinition() && "Cannot redefine enums!");
  if (!IntegerType)
    IntegerType = NewType.getTypePtr();
  PromotionType = NewPromotionType;
  setNumPositiveBits(NumPositiveBits);
  setNumNegativeBits(NumNegativeBits);
  TagDecl::completeDefinition();
}

bool EnumDecl::isClosed() const {
  if (const auto *A = getAttr<EnumExtensibilityAttr>())
    return A->getExtensibility() == EnumExtensibilityAttr::Closed;
  return true;
}

bool EnumDecl::isClosedFlag() const {
  return isClosed() && hasAttr<FlagEnumAttr>();
}

bool EnumDecl::isClosedNonFlag() const {
  return isClosed() && !hasAttr<FlagEnumAttr>();
}

TemplateSpecializationKind EnumDecl::getTemplateSpecializationKind() const {
  if (MemberSpecializationInfo *MSI = getMemberSpecializationInfo())
    return MSI->getTemplateSpecializationKind();

  return TSK_Undeclared;
}

void EnumDecl::setTemplateSpecializationKind(TemplateSpecializationKind TSK,
                                         SourceLocation PointOfInstantiation) {
  MemberSpecializationInfo *MSI = getMemberSpecializationInfo();
  assert(MSI && "Not an instantiated member enumeration?");
  MSI->setTemplateSpecializationKind(TSK);
  if (TSK != TSK_ExplicitSpecialization &&
      PointOfInstantiation.isValid() &&
      MSI->getPointOfInstantiation().isInvalid())
    MSI->setPointOfInstantiation(PointOfInstantiation);
}

EnumDecl *EnumDecl::getTemplateInstantiationPattern() const {
  if (MemberSpecializationInfo *MSInfo = getMemberSpecializationInfo()) {
    if (isTemplateInstantiation(MSInfo->getTemplateSpecializationKind())) {
      EnumDecl *ED = getInstantiatedFromMemberEnum();
      while (auto *NewED = ED->getInstantiatedFromMemberEnum())
        ED = NewED;
      return getDefinitionOrSelf(ED);
    }
  }

  assert(!isTemplateInstantiation(getTemplateSpecializationKind()) &&
         "couldn't find pattern for enum instantiation");
  return nullptr;
}

EnumDecl *EnumDecl::getInstantiatedFromMemberEnum() const {
  if (SpecializationInfo)
    return cast<EnumDecl>(SpecializationInfo->getInstantiatedFrom());

  return nullptr;
}

void EnumDecl::setInstantiationOfMemberEnum(ASTContext &C, EnumDecl *ED,
                                            TemplateSpecializationKind TSK) {
  assert(!SpecializationInfo && "Member enum is already a specialization");
  SpecializationInfo = new (C) MemberSpecializationInfo(ED, TSK);
}

unsigned EnumDecl::getODRHash() {
  if (hasODRHash())
    return ODRHash;

  class ODRHash Hash;
  Hash.AddEnumDecl(this);
  setHasODRHash(true);
  ODRHash = Hash.CalculateHash();
  return ODRHash;
}

//===----------------------------------------------------------------------===//
// RecordDecl Implementation
//===----------------------------------------------------------------------===//

RecordDecl::RecordDecl(Kind DK, TagKind TK, const ASTContext &C,
                       DeclContext *DC, SourceLocation StartLoc,
                       SourceLocation IdLoc, IdentifierInfo *Id,
                       RecordDecl *PrevDecl)
    : TagDecl(DK, TK, C, DC, IdLoc, Id, PrevDecl, StartLoc) {
  assert(classof(static_cast<Decl *>(this)) && "Invalid Kind!");
  setHasFlexibleArrayMember(false);
  setAnonymousStructOrUnion(false);
  setHasObjectMember(false);
  setHasVolatileMember(false);
  setHasLoadedFieldsFromExternalStorage(false);
  setNonTrivialToPrimitiveDefaultInitialize(false);
  setNonTrivialToPrimitiveCopy(false);
  setNonTrivialToPrimitiveDestroy(false);
  setParamDestroyedInCallee(false);
  setArgPassingRestrictions(APK_CanPassInRegs);
}

RecordDecl *RecordDecl::Create(const ASTContext &C, TagKind TK, DeclContext *DC,
                               SourceLocation StartLoc, SourceLocation IdLoc,
                               IdentifierInfo *Id, RecordDecl* PrevDecl) {
  RecordDecl *R = new (C, DC) RecordDecl(Record, TK, C, DC,
                                         StartLoc, IdLoc, Id, PrevDecl);
  R->setMayHaveOutOfDateDef(C.getLangOpts().Modules);

  C.getTypeDeclType(R, PrevDecl);
  return R;
}

RecordDecl *RecordDecl::CreateDeserialized(const ASTContext &C, unsigned ID) {
  RecordDecl *R =
      new (C, ID) RecordDecl(Record, TTK_Struct, C, nullptr, SourceLocation(),
                             SourceLocation(), nullptr, nullptr);
  R->setMayHaveOutOfDateDef(C.getLangOpts().Modules);
  return R;
}

bool RecordDecl::isInjectedClassName() const {
  return isImplicit() && getDeclName() && getDeclContext()->isRecord() &&
    cast<RecordDecl>(getDeclContext())->getDeclName() == getDeclName();
}

bool RecordDecl::isLambda() const {
  if (auto RD = dyn_cast<CXXRecordDecl>(this))
    return RD->isLambda();
  return false;
}

bool RecordDecl::isCapturedRecord() const {
  return hasAttr<CapturedRecordAttr>();
}

void RecordDecl::setCapturedRecord() {
  addAttr(CapturedRecordAttr::CreateImplicit(getASTContext()));
}

RecordDecl::field_iterator RecordDecl::field_begin() const {
  if (hasExternalLexicalStorage() && !hasLoadedFieldsFromExternalStorage())
    LoadFieldsFromExternalStorage();

  return field_iterator(decl_iterator(FirstDecl));
}

/// completeDefinition - Notes that the definition of this type is now
/// complete.
void RecordDecl::completeDefinition() {
  assert(!isCompleteDefinition() && "Cannot redefine record!");
  TagDecl::completeDefinition();
}

/// isMsStruct - Get whether or not this record uses ms_struct layout.
/// This which can be turned on with an attribute, pragma, or the
/// -mms-bitfields command-line option.
bool RecordDecl::isMsStruct(const ASTContext &C) const {
  return hasAttr<MSStructAttr>() || C.getLangOpts().MSBitfields == 1;
}

void RecordDecl::LoadFieldsFromExternalStorage() const {
  ExternalASTSource *Source = getASTContext().getExternalSource();
  assert(hasExternalLexicalStorage() && Source && "No external storage?");

  // Notify that we have a RecordDecl doing some initialization.
  ExternalASTSource::Deserializing TheFields(Source);

  SmallVector<Decl*, 64> Decls;
  setHasLoadedFieldsFromExternalStorage(true);
  Source->FindExternalLexicalDecls(this, [](Decl::Kind K) {
    return FieldDecl::classofKind(K) || IndirectFieldDecl::classofKind(K);
  }, Decls);

#ifndef NDEBUG
  // Check that all decls we got were FieldDecls.
  for (unsigned i=0, e=Decls.size(); i != e; ++i)
    assert(isa<FieldDecl>(Decls[i]) || isa<IndirectFieldDecl>(Decls[i]));
#endif

  if (Decls.empty())
    return;

  std::tie(FirstDecl, LastDecl) = BuildDeclChain(Decls,
                                                 /*FieldsAlreadyLoaded=*/false);
}

bool RecordDecl::mayInsertExtraPadding(bool EmitRemark) const {
  ASTContext &Context = getASTContext();
  const SanitizerMask EnabledAsanMask = Context.getLangOpts().Sanitize.Mask &
      (SanitizerKind::Address | SanitizerKind::KernelAddress);
  if (!EnabledAsanMask || !Context.getLangOpts().SanitizeAddressFieldPadding)
    return false;
  const auto &Blacklist = Context.getSanitizerBlacklist();
  const auto *CXXRD = dyn_cast<CXXRecordDecl>(this);
  // We may be able to relax some of these requirements.
  int ReasonToReject = -1;
  if (!CXXRD || CXXRD->isExternCContext())
    ReasonToReject = 0;  // is not C++.
  else if (CXXRD->hasAttr<PackedAttr>())
    ReasonToReject = 1;  // is packed.
  else if (CXXRD->isUnion())
    ReasonToReject = 2;  // is a union.
  else if (CXXRD->isTriviallyCopyable())
    ReasonToReject = 3;  // is trivially copyable.
  else if (CXXRD->hasTrivialDestructor())
    ReasonToReject = 4;  // has trivial destructor.
  else if (CXXRD->isStandardLayout())
    ReasonToReject = 5;  // is standard layout.
  else if (Blacklist.isBlacklistedLocation(EnabledAsanMask, getLocation(),
                                           "field-padding"))
    ReasonToReject = 6;  // is in a blacklisted file.
  else if (Blacklist.isBlacklistedType(EnabledAsanMask,
                                       getQualifiedNameAsString(),
                                       "field-padding"))
    ReasonToReject = 7;  // is blacklisted.

  if (EmitRemark) {
    if (ReasonToReject >= 0)
      Context.getDiagnostics().Report(
          getLocation(),
          diag::remark_sanitize_address_insert_extra_padding_rejected)
          << getQualifiedNameAsString() << ReasonToReject;
    else
      Context.getDiagnostics().Report(
          getLocation(),
          diag::remark_sanitize_address_insert_extra_padding_accepted)
          << getQualifiedNameAsString();
  }
  return ReasonToReject < 0;
}

const FieldDecl *RecordDecl::findFirstNamedDataMember() const {
  for (const auto *I : fields()) {
    if (I->getIdentifier())
      return I;

    if (const auto *RT = I->getType()->getAs<RecordType>())
      if (const FieldDecl *NamedDataMember =
              RT->getDecl()->findFirstNamedDataMember())
        return NamedDataMember;
  }

  // We didn't find a named data member.
  return nullptr;
}

//===----------------------------------------------------------------------===//
// BlockDecl Implementation
//===----------------------------------------------------------------------===//

BlockDecl::BlockDecl(DeclContext *DC, SourceLocation CaretLoc)
    : Decl(Block, DC, CaretLoc), DeclContext(Block) {
  setIsVariadic(false);
  setCapturesCXXThis(false);
  setBlockMissingReturnType(true);
  setIsConversionFromLambda(false);
  setDoesNotEscape(false);
}

void BlockDecl::setParams(ArrayRef<ParmVarDecl *> NewParamInfo) {
  assert(!ParamInfo && "Already has param info!");

  // Zero params -> null pointer.
  if (!NewParamInfo.empty()) {
    NumParams = NewParamInfo.size();
    ParamInfo = new (getASTContext()) ParmVarDecl*[NewParamInfo.size()];
    std::copy(NewParamInfo.begin(), NewParamInfo.end(), ParamInfo);
  }
}

void BlockDecl::setCaptures(ASTContext &Context, ArrayRef<Capture> Captures,
                            bool CapturesCXXThis) {
  this->setCapturesCXXThis(CapturesCXXThis);
  this->NumCaptures = Captures.size();

  if (Captures.empty()) {
    this->Captures = nullptr;
    return;
  }

  this->Captures = Captures.copy(Context).data();
}

bool BlockDecl::capturesVariable(const VarDecl *variable) const {
  for (const auto &I : captures())
    // Only auto vars can be captured, so no redeclaration worries.
    if (I.getVariable() == variable)
      return true;

  return false;
}

SourceRange BlockDecl::getSourceRange() const {
  return SourceRange(getLocation(), Body ? Body->getEndLoc() : getLocation());
}

//===----------------------------------------------------------------------===//
// Other Decl Allocation/Deallocation Method Implementations
//===----------------------------------------------------------------------===//

void TranslationUnitDecl::anchor() {}

TranslationUnitDecl *TranslationUnitDecl::Create(ASTContext &C) {
  return new (C, (DeclContext *)nullptr) TranslationUnitDecl(C);
}

void PragmaCommentDecl::anchor() {}

PragmaCommentDecl *PragmaCommentDecl::Create(const ASTContext &C,
                                             TranslationUnitDecl *DC,
                                             SourceLocation CommentLoc,
                                             PragmaMSCommentKind CommentKind,
                                             StringRef Arg) {
  PragmaCommentDecl *PCD =
      new (C, DC, additionalSizeToAlloc<char>(Arg.size() + 1))
          PragmaCommentDecl(DC, CommentLoc, CommentKind);
  memcpy(PCD->getTrailingObjects<char>(), Arg.data(), Arg.size());
  PCD->getTrailingObjects<char>()[Arg.size()] = '\0';
  return PCD;
}

PragmaCommentDecl *PragmaCommentDecl::CreateDeserialized(ASTContext &C,
                                                         unsigned ID,
                                                         unsigned ArgSize) {
  return new (C, ID, additionalSizeToAlloc<char>(ArgSize + 1))
      PragmaCommentDecl(nullptr, SourceLocation(), PCK_Unknown);
}

void PragmaDetectMismatchDecl::anchor() {}

PragmaDetectMismatchDecl *
PragmaDetectMismatchDecl::Create(const ASTContext &C, TranslationUnitDecl *DC,
                                 SourceLocation Loc, StringRef Name,
                                 StringRef Value) {
  size_t ValueStart = Name.size() + 1;
  PragmaDetectMismatchDecl *PDMD =
      new (C, DC, additionalSizeToAlloc<char>(ValueStart + Value.size() + 1))
          PragmaDetectMismatchDecl(DC, Loc, ValueStart);
  memcpy(PDMD->getTrailingObjects<char>(), Name.data(), Name.size());
  PDMD->getTrailingObjects<char>()[Name.size()] = '\0';
  memcpy(PDMD->getTrailingObjects<char>() + ValueStart, Value.data(),
         Value.size());
  PDMD->getTrailingObjects<char>()[ValueStart + Value.size()] = '\0';
  return PDMD;
}

PragmaDetectMismatchDecl *
PragmaDetectMismatchDecl::CreateDeserialized(ASTContext &C, unsigned ID,
                                             unsigned NameValueSize) {
  return new (C, ID, additionalSizeToAlloc<char>(NameValueSize + 1))
      PragmaDetectMismatchDecl(nullptr, SourceLocation(), 0);
}

void ExternCContextDecl::anchor() {}

ExternCContextDecl *ExternCContextDecl::Create(const ASTContext &C,
                                               TranslationUnitDecl *DC) {
  return new (C, DC) ExternCContextDecl(DC);
}

void LabelDecl::anchor() {}

LabelDecl *LabelDecl::Create(ASTContext &C, DeclContext *DC,
                             SourceLocation IdentL, IdentifierInfo *II) {
  return new (C, DC) LabelDecl(DC, IdentL, II, nullptr, IdentL);
}

LabelDecl *LabelDecl::Create(ASTContext &C, DeclContext *DC,
                             SourceLocation IdentL, IdentifierInfo *II,
                             SourceLocation GnuLabelL) {
  assert(GnuLabelL != IdentL && "Use this only for GNU local labels");
  return new (C, DC) LabelDecl(DC, IdentL, II, nullptr, GnuLabelL);
}

LabelDecl *LabelDecl::CreateDeserialized(ASTContext &C, unsigned ID) {
  return new (C, ID) LabelDecl(nullptr, SourceLocation(), nullptr, nullptr,
                               SourceLocation());
}

void LabelDecl::setMSAsmLabel(StringRef Name) {
  char *Buffer = new (getASTContext(), 1) char[Name.size() + 1];
  memcpy(Buffer, Name.data(), Name.size());
  Buffer[Name.size()] = '\0';
  MSAsmName = Buffer;
}

void ValueDecl::anchor() {}

bool ValueDecl::isWeak() const {
  for (const auto *I : attrs())
    if (isa<WeakAttr>(I) || isa<WeakRefAttr>(I))
      return true;

  return isWeakImported();
}

void ImplicitParamDecl::anchor() {}

ImplicitParamDecl *ImplicitParamDecl::Create(ASTContext &C, DeclContext *DC,
                                             SourceLocation IdLoc,
                                             IdentifierInfo *Id, QualType Type,
                                             ImplicitParamKind ParamKind) {
  return new (C, DC) ImplicitParamDecl(C, DC, IdLoc, Id, Type, ParamKind);
}

ImplicitParamDecl *ImplicitParamDecl::Create(ASTContext &C, QualType Type,
                                             ImplicitParamKind ParamKind) {
  return new (C, nullptr) ImplicitParamDecl(C, Type, ParamKind);
}

ImplicitParamDecl *ImplicitParamDecl::CreateDeserialized(ASTContext &C,
                                                         unsigned ID) {
  return new (C, ID) ImplicitParamDecl(C, QualType(), ImplicitParamKind::Other);
}

FunctionDecl *FunctionDecl::Create(ASTContext &C, DeclContext *DC,
                                   SourceLocation StartLoc,
                                   const DeclarationNameInfo &NameInfo,
                                   QualType T, TypeSourceInfo *TInfo,
                                   StorageClass SC,
                                   bool isInlineSpecified,
                                   bool hasWrittenPrototype,
                                   bool isConstexprSpecified) {
  FunctionDecl *New =
      new (C, DC) FunctionDecl(Function, C, DC, StartLoc, NameInfo, T, TInfo,
                               SC, isInlineSpecified, isConstexprSpecified);
  New->setHasWrittenPrototype(hasWrittenPrototype);
  return New;
}

FunctionDecl *FunctionDecl::CreateDeserialized(ASTContext &C, unsigned ID) {
  return new (C, ID) FunctionDecl(Function, C, nullptr, SourceLocation(),
                                  DeclarationNameInfo(), QualType(), nullptr,
                                  SC_None, false, false);
}

BlockDecl *BlockDecl::Create(ASTContext &C, DeclContext *DC, SourceLocation L) {
  return new (C, DC) BlockDecl(DC, L);
}

BlockDecl *BlockDecl::CreateDeserialized(ASTContext &C, unsigned ID) {
  return new (C, ID) BlockDecl(nullptr, SourceLocation());
}

CapturedDecl::CapturedDecl(DeclContext *DC, unsigned NumParams)
    : Decl(Captured, DC, SourceLocation()), DeclContext(Captured),
      NumParams(NumParams), ContextParam(0), BodyAndNothrow(nullptr, false) {}

CapturedDecl *CapturedDecl::Create(ASTContext &C, DeclContext *DC,
                                   unsigned NumParams) {
  return new (C, DC, additionalSizeToAlloc<ImplicitParamDecl *>(NumParams))
      CapturedDecl(DC, NumParams);
}

CapturedDecl *CapturedDecl::CreateDeserialized(ASTContext &C, unsigned ID,
                                               unsigned NumParams) {
  return new (C, ID, additionalSizeToAlloc<ImplicitParamDecl *>(NumParams))
      CapturedDecl(nullptr, NumParams);
}

Stmt *CapturedDecl::getBody() const { return BodyAndNothrow.getPointer(); }
void CapturedDecl::setBody(Stmt *B) { BodyAndNothrow.setPointer(B); }

bool CapturedDecl::isNothrow() const { return BodyAndNothrow.getInt(); }
void CapturedDecl::setNothrow(bool Nothrow) { BodyAndNothrow.setInt(Nothrow); }

EnumConstantDecl *EnumConstantDecl::Create(ASTContext &C, EnumDecl *CD,
                                           SourceLocation L,
                                           IdentifierInfo *Id, QualType T,
                                           Expr *E, const llvm::APSInt &V) {
  return new (C, CD) EnumConstantDecl(CD, L, Id, T, E, V);
}

EnumConstantDecl *
EnumConstantDecl::CreateDeserialized(ASTContext &C, unsigned ID) {
  return new (C, ID) EnumConstantDecl(nullptr, SourceLocation(), nullptr,
                                      QualType(), nullptr, llvm::APSInt());
}

void IndirectFieldDecl::anchor() {}

IndirectFieldDecl::IndirectFieldDecl(ASTContext &C, DeclContext *DC,
                                     SourceLocation L, DeclarationName N,
                                     QualType T,
                                     MutableArrayRef<NamedDecl *> CH)
    : ValueDecl(IndirectField, DC, L, N, T), Chaining(CH.data()),
      ChainingSize(CH.size()) {
  // In C++, indirect field declarations conflict with tag declarations in the
  // same scope, so add them to IDNS_Tag so that tag redeclaration finds them.
  if (C.getLangOpts().CPlusPlus)
    IdentifierNamespace |= IDNS_Tag;
}

IndirectFieldDecl *
IndirectFieldDecl::Create(ASTContext &C, DeclContext *DC, SourceLocation L,
                          IdentifierInfo *Id, QualType T,
                          llvm::MutableArrayRef<NamedDecl *> CH) {
  return new (C, DC) IndirectFieldDecl(C, DC, L, Id, T, CH);
}

IndirectFieldDecl *IndirectFieldDecl::CreateDeserialized(ASTContext &C,
                                                         unsigned ID) {
  return new (C, ID) IndirectFieldDecl(C, nullptr, SourceLocation(),
                                       DeclarationName(), QualType(), None);
}

SourceRange EnumConstantDecl::getSourceRange() const {
  SourceLocation End = getLocation();
  if (Init)
    End = Init->getEndLoc();
  return SourceRange(getLocation(), End);
}

void TypeDecl::anchor() {}

TypedefDecl *TypedefDecl::Create(ASTContext &C, DeclContext *DC,
                                 SourceLocation StartLoc, SourceLocation IdLoc,
                                 IdentifierInfo *Id, TypeSourceInfo *TInfo) {
  return new (C, DC) TypedefDecl(C, DC, StartLoc, IdLoc, Id, TInfo);
}

void TypedefNameDecl::anchor() {}

TagDecl *TypedefNameDecl::getAnonDeclWithTypedefName(bool AnyRedecl) const {
  if (auto *TT = getTypeSourceInfo()->getType()->getAs<TagType>()) {
    auto *OwningTypedef = TT->getDecl()->getTypedefNameForAnonDecl();
    auto *ThisTypedef = this;
    if (AnyRedecl && OwningTypedef) {
      OwningTypedef = OwningTypedef->getCanonicalDecl();
      ThisTypedef = ThisTypedef->getCanonicalDecl();
    }
    if (OwningTypedef == ThisTypedef)
      return TT->getDecl();
  }

  return nullptr;
}

bool TypedefNameDecl::isTransparentTagSlow() const {
  auto determineIsTransparent = [&]() {
    if (auto *TT = getUnderlyingType()->getAs<TagType>()) {
      if (auto *TD = TT->getDecl()) {
        if (TD->getName() != getName())
          return false;
        SourceLocation TTLoc = getLocation();
        SourceLocation TDLoc = TD->getLocation();
        if (!TTLoc.isMacroID() || !TDLoc.isMacroID())
          return false;
        SourceManager &SM = getASTContext().getSourceManager();
        return SM.getSpellingLoc(TTLoc) == SM.getSpellingLoc(TDLoc);
      }
    }
    return false;
  };

  bool isTransparent = determineIsTransparent();
  MaybeModedTInfo.setInt((isTransparent << 1) | 1);
  return isTransparent;
}

TypedefDecl *TypedefDecl::CreateDeserialized(ASTContext &C, unsigned ID) {
  return new (C, ID) TypedefDecl(C, nullptr, SourceLocation(), SourceLocation(),
                                 nullptr, nullptr);
}

TypeAliasDecl *TypeAliasDecl::Create(ASTContext &C, DeclContext *DC,
                                     SourceLocation StartLoc,
                                     SourceLocation IdLoc, IdentifierInfo *Id,
                                     TypeSourceInfo *TInfo) {
  return new (C, DC) TypeAliasDecl(C, DC, StartLoc, IdLoc, Id, TInfo);
}

TypeAliasDecl *TypeAliasDecl::CreateDeserialized(ASTContext &C, unsigned ID) {
  return new (C, ID) TypeAliasDecl(C, nullptr, SourceLocation(),
                                   SourceLocation(), nullptr, nullptr);
}

SourceRange TypedefDecl::getSourceRange() const {
  SourceLocation RangeEnd = getLocation();
  if (TypeSourceInfo *TInfo = getTypeSourceInfo()) {
    if (typeIsPostfix(TInfo->getType()))
      RangeEnd = TInfo->getTypeLoc().getSourceRange().getEnd();
  }
  return SourceRange(getBeginLoc(), RangeEnd);
}

SourceRange TypeAliasDecl::getSourceRange() const {
  SourceLocation RangeEnd = getBeginLoc();
  if (TypeSourceInfo *TInfo = getTypeSourceInfo())
    RangeEnd = TInfo->getTypeLoc().getSourceRange().getEnd();
  return SourceRange(getBeginLoc(), RangeEnd);
}

void FileScopeAsmDecl::anchor() {}

FileScopeAsmDecl *FileScopeAsmDecl::Create(ASTContext &C, DeclContext *DC,
                                           StringLiteral *Str,
                                           SourceLocation AsmLoc,
                                           SourceLocation RParenLoc) {
  return new (C, DC) FileScopeAsmDecl(DC, Str, AsmLoc, RParenLoc);
}

FileScopeAsmDecl *FileScopeAsmDecl::CreateDeserialized(ASTContext &C,
                                                       unsigned ID) {
  return new (C, ID) FileScopeAsmDecl(nullptr, nullptr, SourceLocation(),
                                      SourceLocation());
}

void EmptyDecl::anchor() {}

EmptyDecl *EmptyDecl::Create(ASTContext &C, DeclContext *DC, SourceLocation L) {
  return new (C, DC) EmptyDecl(DC, L);
}

EmptyDecl *EmptyDecl::CreateDeserialized(ASTContext &C, unsigned ID) {
  return new (C, ID) EmptyDecl(nullptr, SourceLocation());
}

//===----------------------------------------------------------------------===//
// ImportDecl Implementation
//===----------------------------------------------------------------------===//

/// Retrieve the number of module identifiers needed to name the given
/// module.
static unsigned getNumModuleIdentifiers(Module *Mod) {
  unsigned Result = 1;
  while (Mod->Parent) {
    Mod = Mod->Parent;
    ++Result;
  }
  return Result;
}

ImportDecl::ImportDecl(DeclContext *DC, SourceLocation StartLoc,
                       Module *Imported,
                       ArrayRef<SourceLocation> IdentifierLocs)
  : Decl(Import, DC, StartLoc), ImportedAndComplete(Imported, true) {
  assert(getNumModuleIdentifiers(Imported) == IdentifierLocs.size());
  auto *StoredLocs = getTrailingObjects<SourceLocation>();
  std::uninitialized_copy(IdentifierLocs.begin(), IdentifierLocs.end(),
                          StoredLocs);
}

ImportDecl::ImportDecl(DeclContext *DC, SourceLocation StartLoc,
                       Module *Imported, SourceLocation EndLoc)
  : Decl(Import, DC, StartLoc), ImportedAndComplete(Imported, false) {
  *getTrailingObjects<SourceLocation>() = EndLoc;
}

ImportDecl *ImportDecl::Create(ASTContext &C, DeclContext *DC,
                               SourceLocation StartLoc, Module *Imported,
                               ArrayRef<SourceLocation> IdentifierLocs) {
  return new (C, DC,
              additionalSizeToAlloc<SourceLocation>(IdentifierLocs.size()))
      ImportDecl(DC, StartLoc, Imported, IdentifierLocs);
}

ImportDecl *ImportDecl::CreateImplicit(ASTContext &C, DeclContext *DC,
                                       SourceLocation StartLoc,
                                       Module *Imported,
                                       SourceLocation EndLoc) {
  ImportDecl *Import = new (C, DC, additionalSizeToAlloc<SourceLocation>(1))
      ImportDecl(DC, StartLoc, Imported, EndLoc);
  Import->setImplicit();
  return Import;
}

ImportDecl *ImportDecl::CreateDeserialized(ASTContext &C, unsigned ID,
                                           unsigned NumLocations) {
  return new (C, ID, additionalSizeToAlloc<SourceLocation>(NumLocations))
      ImportDecl(EmptyShell());
}

ArrayRef<SourceLocation> ImportDecl::getIdentifierLocs() const {
  if (!ImportedAndComplete.getInt())
    return None;

  const auto *StoredLocs = getTrailingObjects<SourceLocation>();
  return llvm::makeArrayRef(StoredLocs,
                            getNumModuleIdentifiers(getImportedModule()));
}

SourceRange ImportDecl::getSourceRange() const {
  if (!ImportedAndComplete.getInt())
    return SourceRange(getLocation(), *getTrailingObjects<SourceLocation>());

  return SourceRange(getLocation(), getIdentifierLocs().back());
}

//===----------------------------------------------------------------------===//
// ExportDecl Implementation
//===----------------------------------------------------------------------===//

void ExportDecl::anchor() {}

ExportDecl *ExportDecl::Create(ASTContext &C, DeclContext *DC,
                               SourceLocation ExportLoc) {
  return new (C, DC) ExportDecl(DC, ExportLoc);
}

ExportDecl *ExportDecl::CreateDeserialized(ASTContext &C, unsigned ID) {
  return new (C, ID) ExportDecl(nullptr, SourceLocation());
}
