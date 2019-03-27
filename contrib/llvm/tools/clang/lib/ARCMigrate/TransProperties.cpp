//===--- TransProperties.cpp - Transformations to ARC mode ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// rewriteProperties:
//
// - Adds strong/weak/unsafe_unretained ownership specifier to properties that
//   are missing one.
// - Migrates properties from (retain) to (strong) and (assign) to
//   (unsafe_unretained/weak).
// - If a property is synthesized, adds the ownership specifier in the ivar
//   backing the property.
//
//  @interface Foo : NSObject {
//      NSObject *x;
//  }
//  @property (assign) id x;
//  @end
// ---->
//  @interface Foo : NSObject {
//      NSObject *__weak x;
//  }
//  @property (weak) id x;
//  @end
//
//===----------------------------------------------------------------------===//

#include "Transforms.h"
#include "Internals.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "clang/Sema/SemaDiagnostic.h"
#include <map>

using namespace clang;
using namespace arcmt;
using namespace trans;

namespace {

class PropertiesRewriter {
  MigrationContext &MigrateCtx;
  MigrationPass &Pass;
  ObjCImplementationDecl *CurImplD;

  enum PropActionKind {
    PropAction_None,
    PropAction_RetainReplacedWithStrong,
    PropAction_AssignRemoved,
    PropAction_AssignRewritten,
    PropAction_MaybeAddWeakOrUnsafe
  };

  struct PropData {
    ObjCPropertyDecl *PropD;
    ObjCIvarDecl *IvarD;
    ObjCPropertyImplDecl *ImplD;

    PropData(ObjCPropertyDecl *propD)
      : PropD(propD), IvarD(nullptr), ImplD(nullptr) {}
  };

  typedef SmallVector<PropData, 2> PropsTy;
  typedef std::map<unsigned, PropsTy> AtPropDeclsTy;
  AtPropDeclsTy AtProps;
  llvm::DenseMap<IdentifierInfo *, PropActionKind> ActionOnProp;

public:
  explicit PropertiesRewriter(MigrationContext &MigrateCtx)
    : MigrateCtx(MigrateCtx), Pass(MigrateCtx.Pass) { }

  static void collectProperties(ObjCContainerDecl *D, AtPropDeclsTy &AtProps,
                                AtPropDeclsTy *PrevAtProps = nullptr) {
    for (auto *Prop : D->instance_properties()) {
      if (Prop->getAtLoc().isInvalid())
        continue;
      unsigned RawLoc = Prop->getAtLoc().getRawEncoding();
      if (PrevAtProps)
        if (PrevAtProps->find(RawLoc) != PrevAtProps->end())
          continue;
      PropsTy &props = AtProps[RawLoc];
      props.push_back(Prop);
    }
  }

  void doTransform(ObjCImplementationDecl *D) {
    CurImplD = D;
    ObjCInterfaceDecl *iface = D->getClassInterface();
    if (!iface)
      return;

    collectProperties(iface, AtProps);

    // Look through extensions.
    for (auto *Ext : iface->visible_extensions())
      collectProperties(Ext, AtProps);

    typedef DeclContext::specific_decl_iterator<ObjCPropertyImplDecl>
        prop_impl_iterator;
    for (prop_impl_iterator
           I = prop_impl_iterator(D->decls_begin()),
           E = prop_impl_iterator(D->decls_end()); I != E; ++I) {
      ObjCPropertyImplDecl *implD = *I;
      if (implD->getPropertyImplementation() != ObjCPropertyImplDecl::Synthesize)
        continue;
      ObjCPropertyDecl *propD = implD->getPropertyDecl();
      if (!propD || propD->isInvalidDecl())
        continue;
      ObjCIvarDecl *ivarD = implD->getPropertyIvarDecl();
      if (!ivarD || ivarD->isInvalidDecl())
        continue;
      unsigned rawAtLoc = propD->getAtLoc().getRawEncoding();
      AtPropDeclsTy::iterator findAtLoc = AtProps.find(rawAtLoc);
      if (findAtLoc == AtProps.end())
        continue;

      PropsTy &props = findAtLoc->second;
      for (PropsTy::iterator I = props.begin(), E = props.end(); I != E; ++I) {
        if (I->PropD == propD) {
          I->IvarD = ivarD;
          I->ImplD = implD;
          break;
        }
      }
    }

    for (AtPropDeclsTy::iterator
           I = AtProps.begin(), E = AtProps.end(); I != E; ++I) {
      SourceLocation atLoc = SourceLocation::getFromRawEncoding(I->first);
      PropsTy &props = I->second;
      if (!getPropertyType(props)->isObjCRetainableType())
        continue;
      if (hasIvarWithExplicitARCOwnership(props))
        continue;

      Transaction Trans(Pass.TA);
      rewriteProperty(props, atLoc);
    }
  }

private:
  void doPropAction(PropActionKind kind,
                    PropsTy &props, SourceLocation atLoc,
                    bool markAction = true) {
    if (markAction)
      for (PropsTy::iterator I = props.begin(), E = props.end(); I != E; ++I)
        ActionOnProp[I->PropD->getIdentifier()] = kind;

    switch (kind) {
    case PropAction_None:
      return;
    case PropAction_RetainReplacedWithStrong: {
      StringRef toAttr = "strong";
      MigrateCtx.rewritePropertyAttribute("retain", toAttr, atLoc);
      return;
    }
    case PropAction_AssignRemoved:
      return removeAssignForDefaultStrong(props, atLoc);
    case PropAction_AssignRewritten:
      return rewriteAssign(props, atLoc);
    case PropAction_MaybeAddWeakOrUnsafe:
      return maybeAddWeakOrUnsafeUnretainedAttr(props, atLoc);
    }
  }

  void rewriteProperty(PropsTy &props, SourceLocation atLoc) {
    ObjCPropertyDecl::PropertyAttributeKind propAttrs = getPropertyAttrs(props);

    if (propAttrs & (ObjCPropertyDecl::OBJC_PR_copy |
                     ObjCPropertyDecl::OBJC_PR_unsafe_unretained |
                     ObjCPropertyDecl::OBJC_PR_strong |
                     ObjCPropertyDecl::OBJC_PR_weak))
      return;

    if (propAttrs & ObjCPropertyDecl::OBJC_PR_retain) {
      // strong is the default.
      return doPropAction(PropAction_RetainReplacedWithStrong, props, atLoc);
    }

    bool HasIvarAssignedAPlusOneObject = hasIvarAssignedAPlusOneObject(props);

    if (propAttrs & ObjCPropertyDecl::OBJC_PR_assign) {
      if (HasIvarAssignedAPlusOneObject)
        return doPropAction(PropAction_AssignRemoved, props, atLoc);
      return doPropAction(PropAction_AssignRewritten, props, atLoc);
    }

    if (HasIvarAssignedAPlusOneObject ||
        (Pass.isGCMigration() && !hasGCWeak(props, atLoc)))
      return; // 'strong' by default.

    return doPropAction(PropAction_MaybeAddWeakOrUnsafe, props, atLoc);
  }

  void removeAssignForDefaultStrong(PropsTy &props,
                                    SourceLocation atLoc) const {
    removeAttribute("retain", atLoc);
    if (!removeAttribute("assign", atLoc))
      return;

    for (PropsTy::iterator I = props.begin(), E = props.end(); I != E; ++I) {
      if (I->ImplD)
        Pass.TA.clearDiagnostic(diag::err_arc_strong_property_ownership,
                                diag::err_arc_assign_property_ownership,
                                diag::err_arc_inconsistent_property_ownership,
                                I->IvarD->getLocation());
    }
  }

  void rewriteAssign(PropsTy &props, SourceLocation atLoc) const {
    bool canUseWeak = canApplyWeak(Pass.Ctx, getPropertyType(props),
                                  /*AllowOnUnknownClass=*/Pass.isGCMigration());
    const char *toWhich =
      (Pass.isGCMigration() && !hasGCWeak(props, atLoc)) ? "strong" :
      (canUseWeak ? "weak" : "unsafe_unretained");

    bool rewroteAttr = rewriteAttribute("assign", toWhich, atLoc);
    if (!rewroteAttr)
      canUseWeak = false;

    for (PropsTy::iterator I = props.begin(), E = props.end(); I != E; ++I) {
      if (isUserDeclared(I->IvarD)) {
        if (I->IvarD &&
            I->IvarD->getType().getObjCLifetime() != Qualifiers::OCL_Weak) {
          const char *toWhich =
            (Pass.isGCMigration() && !hasGCWeak(props, atLoc)) ? "__strong " :
              (canUseWeak ? "__weak " : "__unsafe_unretained ");
          Pass.TA.insert(I->IvarD->getLocation(), toWhich);
        }
      }
      if (I->ImplD)
        Pass.TA.clearDiagnostic(diag::err_arc_strong_property_ownership,
                                diag::err_arc_assign_property_ownership,
                                diag::err_arc_inconsistent_property_ownership,
                                I->IvarD->getLocation());
    }
  }

  void maybeAddWeakOrUnsafeUnretainedAttr(PropsTy &props,
                                          SourceLocation atLoc) const {
    bool canUseWeak = canApplyWeak(Pass.Ctx, getPropertyType(props),
                                  /*AllowOnUnknownClass=*/Pass.isGCMigration());

    bool addedAttr = addAttribute(canUseWeak ? "weak" : "unsafe_unretained",
                                  atLoc);
    if (!addedAttr)
      canUseWeak = false;

    for (PropsTy::iterator I = props.begin(), E = props.end(); I != E; ++I) {
      if (isUserDeclared(I->IvarD)) {
        if (I->IvarD &&
            I->IvarD->getType().getObjCLifetime() != Qualifiers::OCL_Weak)
          Pass.TA.insert(I->IvarD->getLocation(),
                         canUseWeak ? "__weak " : "__unsafe_unretained ");
      }
      if (I->ImplD) {
        Pass.TA.clearDiagnostic(diag::err_arc_strong_property_ownership,
                                diag::err_arc_assign_property_ownership,
                                diag::err_arc_inconsistent_property_ownership,
                                I->IvarD->getLocation());
        Pass.TA.clearDiagnostic(
                           diag::err_arc_objc_property_default_assign_on_object,
                           I->ImplD->getLocation());
      }
    }
  }

  bool removeAttribute(StringRef fromAttr, SourceLocation atLoc) const {
    return MigrateCtx.removePropertyAttribute(fromAttr, atLoc);
  }

  bool rewriteAttribute(StringRef fromAttr, StringRef toAttr,
                        SourceLocation atLoc) const {
    return MigrateCtx.rewritePropertyAttribute(fromAttr, toAttr, atLoc);
  }

  bool addAttribute(StringRef attr, SourceLocation atLoc) const {
    return MigrateCtx.addPropertyAttribute(attr, atLoc);
  }

  class PlusOneAssign : public RecursiveASTVisitor<PlusOneAssign> {
    ObjCIvarDecl *Ivar;
  public:
    PlusOneAssign(ObjCIvarDecl *D) : Ivar(D) {}

    bool VisitBinAssign(BinaryOperator *E) {
      Expr *lhs = E->getLHS()->IgnoreParenImpCasts();
      if (ObjCIvarRefExpr *RE = dyn_cast<ObjCIvarRefExpr>(lhs)) {
        if (RE->getDecl() != Ivar)
          return true;

        if (isPlusOneAssign(E))
          return false;
      }

      return true;
    }
  };

  bool hasIvarAssignedAPlusOneObject(PropsTy &props) const {
    for (PropsTy::iterator I = props.begin(), E = props.end(); I != E; ++I) {
      PlusOneAssign oneAssign(I->IvarD);
      bool notFound = oneAssign.TraverseDecl(CurImplD);
      if (!notFound)
        return true;
    }

    return false;
  }

  bool hasIvarWithExplicitARCOwnership(PropsTy &props) const {
    if (Pass.isGCMigration())
      return false;

    for (PropsTy::iterator I = props.begin(), E = props.end(); I != E; ++I) {
      if (isUserDeclared(I->IvarD)) {
        if (isa<AttributedType>(I->IvarD->getType()))
          return true;
        if (I->IvarD->getType().getLocalQualifiers().getObjCLifetime()
              != Qualifiers::OCL_Strong)
          return true;
      }
    }

    return false;
  }

  // Returns true if all declarations in the @property have GC __weak.
  bool hasGCWeak(PropsTy &props, SourceLocation atLoc) const {
    if (!Pass.isGCMigration())
      return false;
    if (props.empty())
      return false;
    return MigrateCtx.AtPropsWeak.count(atLoc.getRawEncoding());
  }

  bool isUserDeclared(ObjCIvarDecl *ivarD) const {
    return ivarD && !ivarD->getSynthesize();
  }

  QualType getPropertyType(PropsTy &props) const {
    assert(!props.empty());
    QualType ty = props[0].PropD->getType().getUnqualifiedType();

#ifndef NDEBUG
    for (PropsTy::iterator I = props.begin(), E = props.end(); I != E; ++I)
      assert(ty == I->PropD->getType().getUnqualifiedType());
#endif

    return ty;
  }

  ObjCPropertyDecl::PropertyAttributeKind
  getPropertyAttrs(PropsTy &props) const {
    assert(!props.empty());
    ObjCPropertyDecl::PropertyAttributeKind
      attrs = props[0].PropD->getPropertyAttributesAsWritten();

#ifndef NDEBUG
    for (PropsTy::iterator I = props.begin(), E = props.end(); I != E; ++I)
      assert(attrs == I->PropD->getPropertyAttributesAsWritten());
#endif

    return attrs;
  }
};

} // anonymous namespace

void PropertyRewriteTraverser::traverseObjCImplementation(
                                           ObjCImplementationContext &ImplCtx) {
  PropertiesRewriter(ImplCtx.getMigrationContext())
                                  .doTransform(ImplCtx.getImplementationDecl());
}
