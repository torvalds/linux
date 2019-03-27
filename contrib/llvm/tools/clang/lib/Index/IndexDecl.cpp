//===- IndexDecl.cpp - Indexing declarations ------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "IndexingContext.h"
#include "clang/Index/IndexDataConsumer.h"
#include "clang/AST/DeclVisitor.h"

using namespace clang;
using namespace index;

#define TRY_DECL(D,CALL_EXPR)                                                  \
  do {                                                                         \
    if (!IndexCtx.shouldIndex(D)) return true;                                 \
    if (!CALL_EXPR)                                                            \
      return false;                                                            \
  } while (0)

#define TRY_TO(CALL_EXPR)                                                      \
  do {                                                                         \
    if (!CALL_EXPR)                                                            \
      return false;                                                            \
  } while (0)

namespace {

class IndexingDeclVisitor : public ConstDeclVisitor<IndexingDeclVisitor, bool> {
  IndexingContext &IndexCtx;

public:
  explicit IndexingDeclVisitor(IndexingContext &indexCtx)
    : IndexCtx(indexCtx) { }

  bool Handled = true;

  bool VisitDecl(const Decl *D) {
    Handled = false;
    return true;
  }

  /// Returns true if the given method has been defined explicitly by the
  /// user.
  static bool hasUserDefined(const ObjCMethodDecl *D,
                             const ObjCImplDecl *Container) {
    const ObjCMethodDecl *MD = Container->getMethod(D->getSelector(),
                                                    D->isInstanceMethod());
    return MD && !MD->isImplicit() && MD->isThisDeclarationADefinition();
  }

  void handleTemplateArgumentLoc(const TemplateArgumentLoc &TALoc,
                                 const NamedDecl *Parent,
                                 const DeclContext *DC) {
    const TemplateArgumentLocInfo &LocInfo = TALoc.getLocInfo();
    switch (TALoc.getArgument().getKind()) {
    case TemplateArgument::Expression:
      IndexCtx.indexBody(LocInfo.getAsExpr(), Parent, DC);
      break;
    case TemplateArgument::Type:
      IndexCtx.indexTypeSourceInfo(LocInfo.getAsTypeSourceInfo(), Parent, DC);
      break;
    case TemplateArgument::Template:
    case TemplateArgument::TemplateExpansion:
      IndexCtx.indexNestedNameSpecifierLoc(TALoc.getTemplateQualifierLoc(),
                                           Parent, DC);
      if (const TemplateDecl *TD = TALoc.getArgument()
                                       .getAsTemplateOrTemplatePattern()
                                       .getAsTemplateDecl()) {
        if (const NamedDecl *TTD = TD->getTemplatedDecl())
          IndexCtx.handleReference(TTD, TALoc.getTemplateNameLoc(), Parent, DC);
      }
      break;
    default:
      break;
    }
  }

  void handleDeclarator(const DeclaratorDecl *D,
                        const NamedDecl *Parent = nullptr,
                        bool isIBType = false) {
    if (!Parent) Parent = D;

    IndexCtx.indexTypeSourceInfo(D->getTypeSourceInfo(), Parent,
                                 Parent->getLexicalDeclContext(),
                                 /*isBase=*/false, isIBType);
    IndexCtx.indexNestedNameSpecifierLoc(D->getQualifierLoc(), Parent);
    if (IndexCtx.shouldIndexFunctionLocalSymbols()) {
      // Only index parameters in definitions, parameters in declarations are
      // not useful.
      if (const ParmVarDecl *Parm = dyn_cast<ParmVarDecl>(D)) {
        auto *DC = Parm->getDeclContext();
        if (auto *FD = dyn_cast<FunctionDecl>(DC)) {
          if (FD->isThisDeclarationADefinition())
            IndexCtx.handleDecl(Parm);
        } else if (auto *MD = dyn_cast<ObjCMethodDecl>(DC)) {
          if (MD->isThisDeclarationADefinition())
            IndexCtx.handleDecl(Parm);
        } else {
          IndexCtx.handleDecl(Parm);
        }
      } else if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
        if (FD->isThisDeclarationADefinition()) {
          for (auto PI : FD->parameters()) {
            IndexCtx.handleDecl(PI);
          }
        }
      }
    } else {
      // Index the default parameter value for function definitions.
      if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
        if (FD->isThisDeclarationADefinition()) {
          for (const auto *PV : FD->parameters()) {
            if (PV->hasDefaultArg() && !PV->hasUninstantiatedDefaultArg() &&
                !PV->hasUnparsedDefaultArg())
              IndexCtx.indexBody(PV->getDefaultArg(), D);
          }
        }
      }
    }
  }

  bool handleObjCMethod(const ObjCMethodDecl *D,
                        const ObjCPropertyDecl *AssociatedProp = nullptr) {
    SmallVector<SymbolRelation, 4> Relations;
    SmallVector<const ObjCMethodDecl*, 4> Overriden;

    D->getOverriddenMethods(Overriden);
    for(auto overridden: Overriden) {
      Relations.emplace_back((unsigned) SymbolRole::RelationOverrideOf,
                             overridden);
    }
    if (AssociatedProp)
      Relations.emplace_back((unsigned)SymbolRole::RelationAccessorOf,
                             AssociatedProp);

    // getLocation() returns beginning token of a method declaration, but for
    // indexing purposes we want to point to the base name.
    SourceLocation MethodLoc = D->getSelectorStartLoc();
    if (MethodLoc.isInvalid())
      MethodLoc = D->getLocation();

    SourceLocation AttrLoc;

    // check for (getter=/setter=)
    if (AssociatedProp) {
      bool isGetter = !D->param_size();
      AttrLoc = isGetter ?
        AssociatedProp->getGetterNameLoc():
        AssociatedProp->getSetterNameLoc();
    }

    SymbolRoleSet Roles = (SymbolRoleSet)SymbolRole::Dynamic;
    if (D->isImplicit()) {
      if (AttrLoc.isValid()) {
        MethodLoc = AttrLoc;
      } else {
        Roles |= (SymbolRoleSet)SymbolRole::Implicit;
      }
    } else if (AttrLoc.isValid()) {
      IndexCtx.handleReference(D, AttrLoc, cast<NamedDecl>(D->getDeclContext()),
                               D->getDeclContext(), 0);
    }

    TRY_DECL(D, IndexCtx.handleDecl(D, MethodLoc, Roles, Relations));
    IndexCtx.indexTypeSourceInfo(D->getReturnTypeSourceInfo(), D);
    bool hasIBActionAndFirst = D->hasAttr<IBActionAttr>();
    for (const auto *I : D->parameters()) {
      handleDeclarator(I, D, /*isIBType=*/hasIBActionAndFirst);
      hasIBActionAndFirst = false;
    }

    if (D->isThisDeclarationADefinition()) {
      const Stmt *Body = D->getBody();
      if (Body) {
        IndexCtx.indexBody(Body, D, D);
      }
    }
    return true;
  }

  /// Gather the declarations which the given declaration \D overrides in a
  /// pseudo-override manner.
  ///
  /// Pseudo-overrides occur when a class template specialization declares
  /// a declaration that has the same name as a similar declaration in the
  /// non-specialized template.
  void
  gatherTemplatePseudoOverrides(const NamedDecl *D,
                                SmallVectorImpl<SymbolRelation> &Relations) {
    if (!IndexCtx.getLangOpts().CPlusPlus)
      return;
    const auto *CTSD =
        dyn_cast<ClassTemplateSpecializationDecl>(D->getLexicalDeclContext());
    if (!CTSD)
      return;
    llvm::PointerUnion<ClassTemplateDecl *,
                       ClassTemplatePartialSpecializationDecl *>
        Template = CTSD->getSpecializedTemplateOrPartial();
    if (const auto *CTD = Template.dyn_cast<ClassTemplateDecl *>()) {
      const CXXRecordDecl *Pattern = CTD->getTemplatedDecl();
      bool TypeOverride = isa<TypeDecl>(D);
      for (const NamedDecl *ND : Pattern->lookup(D->getDeclName())) {
        if (const auto *CTD = dyn_cast<ClassTemplateDecl>(ND))
          ND = CTD->getTemplatedDecl();
        if (ND->isImplicit())
          continue;
        // Types can override other types.
        if (!TypeOverride) {
          if (ND->getKind() != D->getKind())
            continue;
        } else if (!isa<TypeDecl>(ND))
          continue;
        if (const auto *FD = dyn_cast<FunctionDecl>(ND)) {
          const auto *DFD = cast<FunctionDecl>(D);
          // Function overrides are approximated using the number of parameters.
          if (FD->getStorageClass() != DFD->getStorageClass() ||
              FD->getNumParams() != DFD->getNumParams())
            continue;
        }
        Relations.emplace_back(
            SymbolRoleSet(SymbolRole::RelationSpecializationOf), ND);
      }
    }
  }

  bool VisitFunctionDecl(const FunctionDecl *D) {
    SymbolRoleSet Roles{};
    SmallVector<SymbolRelation, 4> Relations;
    if (auto *CXXMD = dyn_cast<CXXMethodDecl>(D)) {
      if (CXXMD->isVirtual())
        Roles |= (unsigned)SymbolRole::Dynamic;
      for (const CXXMethodDecl *O : CXXMD->overridden_methods()) {
        Relations.emplace_back((unsigned)SymbolRole::RelationOverrideOf, O);
      }
    }
    gatherTemplatePseudoOverrides(D, Relations);
    if (const auto *Base = D->getPrimaryTemplate())
      Relations.push_back(
          SymbolRelation(SymbolRoleSet(SymbolRole::RelationSpecializationOf),
                         Base->getTemplatedDecl()));

    TRY_DECL(D, IndexCtx.handleDecl(D, Roles, Relations));
    handleDeclarator(D);

    if (const CXXConstructorDecl *Ctor = dyn_cast<CXXConstructorDecl>(D)) {
      IndexCtx.handleReference(Ctor->getParent(), Ctor->getLocation(),
                               Ctor->getParent(), Ctor->getDeclContext());

      // Constructor initializers.
      for (const auto *Init : Ctor->inits()) {
        if (Init->isWritten()) {
          IndexCtx.indexTypeSourceInfo(Init->getTypeSourceInfo(), D);
          if (const FieldDecl *Member = Init->getAnyMember())
            IndexCtx.handleReference(Member, Init->getMemberLocation(), D, D,
                                     (unsigned)SymbolRole::Write);
          IndexCtx.indexBody(Init->getInit(), D, D);
        }
      }
    } else if (const CXXDestructorDecl *Dtor = dyn_cast<CXXDestructorDecl>(D)) {
      if (auto TypeNameInfo = Dtor->getNameInfo().getNamedTypeInfo()) {
        IndexCtx.handleReference(Dtor->getParent(),
                                 TypeNameInfo->getTypeLoc().getBeginLoc(),
                                 Dtor->getParent(), Dtor->getDeclContext());
      }
    } else if (const auto *Guide = dyn_cast<CXXDeductionGuideDecl>(D)) {
      IndexCtx.handleReference(Guide->getDeducedTemplate()->getTemplatedDecl(),
                               Guide->getLocation(), Guide,
                               Guide->getDeclContext());
    }
    // Template specialization arguments.
    if (const ASTTemplateArgumentListInfo *TemplateArgInfo =
            D->getTemplateSpecializationArgsAsWritten()) {
      for (const auto &Arg : TemplateArgInfo->arguments())
        handleTemplateArgumentLoc(Arg, D, D->getLexicalDeclContext());
    }

    if (D->isThisDeclarationADefinition()) {
      const Stmt *Body = D->getBody();
      if (Body) {
        IndexCtx.indexBody(Body, D, D);
      }
    }
    return true;
  }

  bool VisitVarDecl(const VarDecl *D) {
    SmallVector<SymbolRelation, 4> Relations;
    gatherTemplatePseudoOverrides(D, Relations);
    TRY_DECL(D, IndexCtx.handleDecl(D, SymbolRoleSet(), Relations));
    handleDeclarator(D);
    IndexCtx.indexBody(D->getInit(), D);
    return true;
  }

  bool VisitDecompositionDecl(const DecompositionDecl *D) {
    for (const auto *Binding : D->bindings())
      TRY_DECL(Binding, IndexCtx.handleDecl(Binding));
    return Base::VisitDecompositionDecl(D);
  }

  bool VisitFieldDecl(const FieldDecl *D) {
    SmallVector<SymbolRelation, 4> Relations;
    gatherTemplatePseudoOverrides(D, Relations);
    TRY_DECL(D, IndexCtx.handleDecl(D, SymbolRoleSet(), Relations));
    handleDeclarator(D);
    if (D->isBitField())
      IndexCtx.indexBody(D->getBitWidth(), D);
    else if (D->hasInClassInitializer())
      IndexCtx.indexBody(D->getInClassInitializer(), D);
    return true;
  }

  bool VisitObjCIvarDecl(const ObjCIvarDecl *D) {
    if (D->getSynthesize()) {
      // handled in VisitObjCPropertyImplDecl
      return true;
    }
    TRY_DECL(D, IndexCtx.handleDecl(D));
    handleDeclarator(D);
    return true;
  }

  bool VisitMSPropertyDecl(const MSPropertyDecl *D) {
    handleDeclarator(D);
    return true;
  }

  bool VisitEnumConstantDecl(const EnumConstantDecl *D) {
    TRY_DECL(D, IndexCtx.handleDecl(D));
    IndexCtx.indexBody(D->getInitExpr(), D);
    return true;
  }

  bool VisitTypedefNameDecl(const TypedefNameDecl *D) {
    if (!D->isTransparentTag()) {
      SmallVector<SymbolRelation, 4> Relations;
      gatherTemplatePseudoOverrides(D, Relations);
      TRY_DECL(D, IndexCtx.handleDecl(D, SymbolRoleSet(), Relations));
      IndexCtx.indexTypeSourceInfo(D->getTypeSourceInfo(), D);
    }
    return true;
  }

  bool VisitTagDecl(const TagDecl *D) {
    // Non-free standing tags are handled in indexTypeSourceInfo.
    if (D->isFreeStanding()) {
      if (D->isThisDeclarationADefinition()) {
        SmallVector<SymbolRelation, 4> Relations;
        gatherTemplatePseudoOverrides(D, Relations);
        IndexCtx.indexTagDecl(D, Relations);
      } else {
        SmallVector<SymbolRelation, 1> Relations;
        gatherTemplatePseudoOverrides(D, Relations);
        return IndexCtx.handleDecl(D, D->getLocation(), SymbolRoleSet(),
                                   Relations, D->getLexicalDeclContext());
      }
    }
    return true;
  }

  bool handleReferencedProtocols(const ObjCProtocolList &ProtList,
                                 const ObjCContainerDecl *ContD,
                                 SourceLocation SuperLoc) {
    ObjCInterfaceDecl::protocol_loc_iterator LI = ProtList.loc_begin();
    for (ObjCInterfaceDecl::protocol_iterator
         I = ProtList.begin(), E = ProtList.end(); I != E; ++I, ++LI) {
      SourceLocation Loc = *LI;
      ObjCProtocolDecl *PD = *I;
      SymbolRoleSet roles{};
      if (Loc == SuperLoc)
        roles |= (SymbolRoleSet)SymbolRole::Implicit;
      TRY_TO(IndexCtx.handleReference(PD, Loc, ContD, ContD, roles,
          SymbolRelation{(unsigned)SymbolRole::RelationBaseOf, ContD}));
    }
    return true;
  }

  bool VisitObjCInterfaceDecl(const ObjCInterfaceDecl *D) {
    if (D->isThisDeclarationADefinition()) {
      TRY_DECL(D, IndexCtx.handleDecl(D));
      SourceLocation SuperLoc = D->getSuperClassLoc();
      if (auto *SuperD = D->getSuperClass()) {
        bool hasSuperTypedef = false;
        if (auto *TInfo = D->getSuperClassTInfo()) {
          if (auto *TT = TInfo->getType()->getAs<TypedefType>()) {
            if (auto *TD = TT->getDecl()) {
              hasSuperTypedef = true;
              TRY_TO(IndexCtx.handleReference(TD, SuperLoc, D, D,
                                              SymbolRoleSet()));
            }
          }
        }
        SymbolRoleSet superRoles{};
        if (hasSuperTypedef)
          superRoles |= (SymbolRoleSet)SymbolRole::Implicit;
        TRY_TO(IndexCtx.handleReference(SuperD, SuperLoc, D, D, superRoles,
            SymbolRelation{(unsigned)SymbolRole::RelationBaseOf, D}));
      }
      TRY_TO(handleReferencedProtocols(D->getReferencedProtocols(), D,
                                       SuperLoc));
      TRY_TO(IndexCtx.indexDeclContext(D));
    } else {
      return IndexCtx.handleReference(D, D->getLocation(), nullptr,
                                      D->getDeclContext(), SymbolRoleSet());
    }
    return true;
  }

  bool VisitObjCProtocolDecl(const ObjCProtocolDecl *D) {
    if (D->isThisDeclarationADefinition()) {
      TRY_DECL(D, IndexCtx.handleDecl(D));
      TRY_TO(handleReferencedProtocols(D->getReferencedProtocols(), D,
                                       /*superLoc=*/SourceLocation()));
      TRY_TO(IndexCtx.indexDeclContext(D));
    } else {
      return IndexCtx.handleReference(D, D->getLocation(), nullptr,
                                      D->getDeclContext(), SymbolRoleSet());
    }
    return true;
  }

  bool VisitObjCImplementationDecl(const ObjCImplementationDecl *D) {
    const ObjCInterfaceDecl *Class = D->getClassInterface();
    if (!Class)
      return true;

    if (Class->isImplicitInterfaceDecl())
      IndexCtx.handleDecl(Class);

    TRY_DECL(D, IndexCtx.handleDecl(D));

    // Visit implicit @synthesize property implementations first as their
    // location is reported at the name of the @implementation block. This
    // serves no purpose other than to simplify the FileCheck-based tests.
    for (const auto *I : D->property_impls()) {
      if (I->getLocation().isInvalid())
        IndexCtx.indexDecl(I);
    }
    for (const auto *I : D->decls()) {
      if (!isa<ObjCPropertyImplDecl>(I) ||
          cast<ObjCPropertyImplDecl>(I)->getLocation().isValid())
        IndexCtx.indexDecl(I);
    }

    return true;
  }

  bool VisitObjCCategoryDecl(const ObjCCategoryDecl *D) {
    if (!IndexCtx.shouldIndex(D))
      return true;
    const ObjCInterfaceDecl *C = D->getClassInterface();
    if (!C)
      return true;
    TRY_TO(IndexCtx.handleReference(C, D->getLocation(), D, D, SymbolRoleSet(),
                                   SymbolRelation{
                                     (unsigned)SymbolRole::RelationExtendedBy, D
                                   }));
    SourceLocation CategoryLoc = D->getCategoryNameLoc();
    if (!CategoryLoc.isValid())
      CategoryLoc = D->getLocation();
    TRY_TO(IndexCtx.handleDecl(D, CategoryLoc));
    TRY_TO(handleReferencedProtocols(D->getReferencedProtocols(), D,
                                     /*superLoc=*/SourceLocation()));
    TRY_TO(IndexCtx.indexDeclContext(D));
    return true;
  }

  bool VisitObjCCategoryImplDecl(const ObjCCategoryImplDecl *D) {
    const ObjCCategoryDecl *Cat = D->getCategoryDecl();
    if (!Cat)
      return true;
    const ObjCInterfaceDecl *C = D->getClassInterface();
    if (C)
      TRY_TO(IndexCtx.handleReference(C, D->getLocation(), D, D,
                                      SymbolRoleSet()));
    SourceLocation CategoryLoc = D->getCategoryNameLoc();
    if (!CategoryLoc.isValid())
      CategoryLoc = D->getLocation();
    TRY_DECL(D, IndexCtx.handleDecl(D, CategoryLoc));
    IndexCtx.indexDeclContext(D);
    return true;
  }

  bool VisitObjCMethodDecl(const ObjCMethodDecl *D) {
    // Methods associated with a property, even user-declared ones, are
    // handled when we handle the property.
    if (D->isPropertyAccessor())
      return true;

    handleObjCMethod(D);
    return true;
  }

  bool VisitObjCPropertyDecl(const ObjCPropertyDecl *D) {
    if (ObjCMethodDecl *MD = D->getGetterMethodDecl())
      if (MD->getLexicalDeclContext() == D->getLexicalDeclContext())
        handleObjCMethod(MD, D);
    if (ObjCMethodDecl *MD = D->getSetterMethodDecl())
      if (MD->getLexicalDeclContext() == D->getLexicalDeclContext())
        handleObjCMethod(MD, D);
    TRY_DECL(D, IndexCtx.handleDecl(D));
    if (IBOutletCollectionAttr *attr = D->getAttr<IBOutletCollectionAttr>())
      IndexCtx.indexTypeSourceInfo(attr->getInterfaceLoc(), D,
                                   D->getLexicalDeclContext(), false, true);
    IndexCtx.indexTypeSourceInfo(D->getTypeSourceInfo(), D);
    return true;
  }

  bool VisitObjCPropertyImplDecl(const ObjCPropertyImplDecl *D) {
    ObjCPropertyDecl *PD = D->getPropertyDecl();
    auto *Container = cast<ObjCImplDecl>(D->getDeclContext());
    SourceLocation Loc = D->getLocation();
    SymbolRoleSet Roles = 0;
    SmallVector<SymbolRelation, 1> Relations;

    if (ObjCIvarDecl *ID = D->getPropertyIvarDecl())
      Relations.push_back({(SymbolRoleSet)SymbolRole::RelationAccessorOf, ID});
    if (Loc.isInvalid()) {
      Loc = Container->getLocation();
      Roles |= (SymbolRoleSet)SymbolRole::Implicit;
    }
    TRY_DECL(D, IndexCtx.handleDecl(D, Loc, Roles, Relations));

    if (D->getPropertyImplementation() == ObjCPropertyImplDecl::Dynamic)
      return true;

    assert(D->getPropertyImplementation() == ObjCPropertyImplDecl::Synthesize);
    SymbolRoleSet AccessorMethodRoles =
      SymbolRoleSet(SymbolRole::Dynamic) | SymbolRoleSet(SymbolRole::Implicit);
    if (ObjCMethodDecl *MD = PD->getGetterMethodDecl()) {
      if (MD->isPropertyAccessor() &&
          !hasUserDefined(MD, Container))
        IndexCtx.handleDecl(MD, Loc, AccessorMethodRoles, {}, Container);
    }
    if (ObjCMethodDecl *MD = PD->getSetterMethodDecl()) {
      if (MD->isPropertyAccessor() &&
          !hasUserDefined(MD, Container))
        IndexCtx.handleDecl(MD, Loc, AccessorMethodRoles, {}, Container);
    }
    if (ObjCIvarDecl *IvarD = D->getPropertyIvarDecl()) {
      if (IvarD->getSynthesize()) {
        // For synthesized ivars, use the location of its name in the
        // corresponding @synthesize. If there isn't one, use the containing
        // @implementation's location, rather than the property's location,
        // otherwise the header file containing the @interface will have different
        // indexing contents based on whether the @implementation was present or
        // not in the translation unit.
        SymbolRoleSet IvarRoles = 0;
        SourceLocation IvarLoc = D->getPropertyIvarDeclLoc();
        if (D->getLocation().isInvalid()) {
          IvarLoc = Container->getLocation();
          IvarRoles = (SymbolRoleSet)SymbolRole::Implicit;
        } else if (D->getLocation() == IvarLoc) {
          IvarRoles = (SymbolRoleSet)SymbolRole::Implicit;
        }
        TRY_DECL(IvarD, IndexCtx.handleDecl(IvarD, IvarLoc, IvarRoles));
      } else {
        IndexCtx.handleReference(IvarD, D->getPropertyIvarDeclLoc(), nullptr,
                                 D->getDeclContext(), SymbolRoleSet());
      }
    }
    return true;
  }

  bool VisitNamespaceDecl(const NamespaceDecl *D) {
    TRY_DECL(D, IndexCtx.handleDecl(D));
    IndexCtx.indexDeclContext(D);
    return true;
  }

  bool VisitNamespaceAliasDecl(const NamespaceAliasDecl *D) {
    TRY_DECL(D, IndexCtx.handleDecl(D));
    IndexCtx.indexNestedNameSpecifierLoc(D->getQualifierLoc(), D);
    IndexCtx.handleReference(D->getAliasedNamespace(), D->getTargetNameLoc(), D,
                             D->getLexicalDeclContext());
    return true;
  }

  bool VisitUsingDecl(const UsingDecl *D) {
    const DeclContext *DC = D->getDeclContext()->getRedeclContext();
    const NamedDecl *Parent = dyn_cast<NamedDecl>(DC);

    IndexCtx.indexNestedNameSpecifierLoc(D->getQualifierLoc(), Parent,
                                         D->getLexicalDeclContext());
    for (const auto *I : D->shadows())
      IndexCtx.handleReference(I->getUnderlyingDecl(), D->getLocation(), Parent,
                               D->getLexicalDeclContext(), SymbolRoleSet());
    return true;
  }

  bool VisitUsingDirectiveDecl(const UsingDirectiveDecl *D) {
    const DeclContext *DC = D->getDeclContext()->getRedeclContext();
    const NamedDecl *Parent = dyn_cast<NamedDecl>(DC);

    // NNS for the local 'using namespace' directives is visited by the body
    // visitor.
    if (!D->getParentFunctionOrMethod())
      IndexCtx.indexNestedNameSpecifierLoc(D->getQualifierLoc(), Parent,
                                           D->getLexicalDeclContext());

    return IndexCtx.handleReference(D->getNominatedNamespaceAsWritten(),
                                    D->getLocation(), Parent,
                                    D->getLexicalDeclContext(),
                                    SymbolRoleSet());
  }

  bool VisitUnresolvedUsingValueDecl(const UnresolvedUsingValueDecl *D) {
    TRY_DECL(D, IndexCtx.handleDecl(D));
    const DeclContext *DC = D->getDeclContext()->getRedeclContext();
    const NamedDecl *Parent = dyn_cast<NamedDecl>(DC);
    IndexCtx.indexNestedNameSpecifierLoc(D->getQualifierLoc(), Parent,
                                         D->getLexicalDeclContext());
    return true;
  }

  bool VisitUnresolvedUsingTypenameDecl(const UnresolvedUsingTypenameDecl *D) {
    TRY_DECL(D, IndexCtx.handleDecl(D));
    const DeclContext *DC = D->getDeclContext()->getRedeclContext();
    const NamedDecl *Parent = dyn_cast<NamedDecl>(DC);
    IndexCtx.indexNestedNameSpecifierLoc(D->getQualifierLoc(), Parent,
                                         D->getLexicalDeclContext());
    return true;
  }

  bool VisitClassTemplateSpecializationDecl(const
                                           ClassTemplateSpecializationDecl *D) {
    // FIXME: Notify subsequent callbacks if info comes from implicit
    // instantiation.
    llvm::PointerUnion<ClassTemplateDecl *,
                       ClassTemplatePartialSpecializationDecl *>
        Template = D->getSpecializedTemplateOrPartial();
    const Decl *SpecializationOf =
        Template.is<ClassTemplateDecl *>()
            ? (Decl *)Template.get<ClassTemplateDecl *>()
            : Template.get<ClassTemplatePartialSpecializationDecl *>();
    if (!D->isThisDeclarationADefinition())
      IndexCtx.indexNestedNameSpecifierLoc(D->getQualifierLoc(), D);
    IndexCtx.indexTagDecl(
        D, SymbolRelation(SymbolRoleSet(SymbolRole::RelationSpecializationOf),
                          SpecializationOf));
    if (TypeSourceInfo *TSI = D->getTypeAsWritten())
      IndexCtx.indexTypeSourceInfo(TSI, /*Parent=*/nullptr,
                                   D->getLexicalDeclContext());
    return true;
  }

  static bool shouldIndexTemplateParameterDefaultValue(const NamedDecl *D) {
    if (!D)
      return false;
    // We want to index the template parameters only once when indexing the
    // canonical declaration.
    if (const auto *FD = dyn_cast<FunctionDecl>(D))
      return FD->getCanonicalDecl() == FD;
    else if (const auto *TD = dyn_cast<TagDecl>(D))
      return TD->getCanonicalDecl() == TD;
    else if (const auto *VD = dyn_cast<VarDecl>(D))
      return VD->getCanonicalDecl() == VD;
    return true;
  }

  bool VisitTemplateDecl(const TemplateDecl *D) {

    const NamedDecl *Parent = D->getTemplatedDecl();
    if (!Parent)
      return true;

    // Index the default values for the template parameters.
    if (D->getTemplateParameters() &&
        shouldIndexTemplateParameterDefaultValue(Parent)) {
      const TemplateParameterList *Params = D->getTemplateParameters();
      for (const NamedDecl *TP : *Params) {
        if (const auto *TTP = dyn_cast<TemplateTypeParmDecl>(TP)) {
          if (TTP->hasDefaultArgument())
            IndexCtx.indexTypeSourceInfo(TTP->getDefaultArgumentInfo(), Parent);
        } else if (const auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(TP)) {
          if (NTTP->hasDefaultArgument())
            IndexCtx.indexBody(NTTP->getDefaultArgument(), Parent);
        } else if (const auto *TTPD = dyn_cast<TemplateTemplateParmDecl>(TP)) {
          if (TTPD->hasDefaultArgument())
            handleTemplateArgumentLoc(TTPD->getDefaultArgument(), Parent,
                                      TP->getLexicalDeclContext());
        }
      }
    }

    return Visit(Parent);
  }

  bool VisitFriendDecl(const FriendDecl *D) {
    if (auto ND = D->getFriendDecl()) {
      // FIXME: Ignore a class template in a dependent context, these are not
      // linked properly with their redeclarations, ending up with duplicate
      // USRs.
      // See comment "Friend templates are visible in fairly strange ways." in
      // SemaTemplate.cpp which precedes code that prevents the friend template
      // from becoming visible from the enclosing context.
      if (isa<ClassTemplateDecl>(ND) && D->getDeclContext()->isDependentContext())
        return true;
      return Visit(ND);
    }
    if (auto Ty = D->getFriendType()) {
      IndexCtx.indexTypeSourceInfo(Ty, cast<NamedDecl>(D->getDeclContext()));
    }
    return true;
  }

  bool VisitImportDecl(const ImportDecl *D) {
    return IndexCtx.importedModule(D);
  }

  bool VisitStaticAssertDecl(const StaticAssertDecl *D) {
    IndexCtx.indexBody(D->getAssertExpr(),
                       dyn_cast<NamedDecl>(D->getDeclContext()),
                       D->getLexicalDeclContext());
    return true;
  }
};

} // anonymous namespace

bool IndexingContext::indexDecl(const Decl *D) {
  if (D->isImplicit() && shouldIgnoreIfImplicit(D))
    return true;

  if (isTemplateImplicitInstantiation(D) && !shouldIndexImplicitInstantiation())
    return true;

  IndexingDeclVisitor Visitor(*this);
  bool ShouldContinue = Visitor.Visit(D);
  if (!ShouldContinue)
    return false;

  if (!Visitor.Handled && isa<DeclContext>(D))
    return indexDeclContext(cast<DeclContext>(D));

  return true;
}

bool IndexingContext::indexDeclContext(const DeclContext *DC) {
  for (const auto *I : DC->decls())
    if (!indexDecl(I))
      return false;
  return true;
}

bool IndexingContext::indexTopLevelDecl(const Decl *D) {
  if (D->getLocation().isInvalid())
    return true;

  if (isa<ObjCMethodDecl>(D))
    return true; // Wait for the objc container.

  return indexDecl(D);
}

bool IndexingContext::indexDeclGroupRef(DeclGroupRef DG) {
  for (DeclGroupRef::iterator I = DG.begin(), E = DG.end(); I != E; ++I)
    if (!indexTopLevelDecl(*I))
      return false;
  return true;
}
