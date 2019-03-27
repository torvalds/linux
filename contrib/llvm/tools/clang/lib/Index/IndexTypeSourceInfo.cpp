//===- IndexTypeSourceInfo.cpp - Indexing types ---------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "IndexingContext.h"
#include "clang/AST/RecursiveASTVisitor.h"

using namespace clang;
using namespace index;

namespace {

class TypeIndexer : public RecursiveASTVisitor<TypeIndexer> {
  IndexingContext &IndexCtx;
  const NamedDecl *Parent;
  const DeclContext *ParentDC;
  bool IsBase;
  SmallVector<SymbolRelation, 3> Relations;

  typedef RecursiveASTVisitor<TypeIndexer> base;

public:
  TypeIndexer(IndexingContext &indexCtx, const NamedDecl *parent,
              const DeclContext *DC, bool isBase, bool isIBType)
    : IndexCtx(indexCtx), Parent(parent), ParentDC(DC), IsBase(isBase) {
    if (IsBase) {
      assert(Parent);
      Relations.emplace_back((unsigned)SymbolRole::RelationBaseOf, Parent);
    }
    if (isIBType) {
      assert(Parent);
      Relations.emplace_back((unsigned)SymbolRole::RelationIBTypeOf, Parent);
    }
  }

  bool shouldWalkTypesOfTypeLocs() const { return false; }

#define TRY_TO(CALL_EXPR)                                                      \
  do {                                                                         \
    if (!CALL_EXPR)                                                            \
      return false;                                                            \
  } while (0)

  bool VisitTypedefTypeLoc(TypedefTypeLoc TL) {
    SourceLocation Loc = TL.getNameLoc();
    TypedefNameDecl *ND = TL.getTypedefNameDecl();
    if (ND->isTransparentTag()) {
      TagDecl *Underlying = ND->getUnderlyingType()->getAsTagDecl();
      return IndexCtx.handleReference(Underlying, Loc, Parent,
                                      ParentDC, SymbolRoleSet(), Relations);
    }
    if (IsBase) {
      TRY_TO(IndexCtx.handleReference(ND, Loc,
                                      Parent, ParentDC, SymbolRoleSet()));
      if (auto *CD = TL.getType()->getAsCXXRecordDecl()) {
        TRY_TO(IndexCtx.handleReference(CD, Loc, Parent, ParentDC,
                                        (unsigned)SymbolRole::Implicit,
                                        Relations));
      }
    } else {
      TRY_TO(IndexCtx.handleReference(ND, Loc,
                                      Parent, ParentDC, SymbolRoleSet(),
                                      Relations));
    }
    return true;
  }

  bool traverseParamVarHelper(ParmVarDecl *D) {
    TRY_TO(TraverseNestedNameSpecifierLoc(D->getQualifierLoc()));
    if (D->getTypeSourceInfo())
      TRY_TO(TraverseTypeLoc(D->getTypeSourceInfo()->getTypeLoc()));
    return true;
  }

  bool TraverseParmVarDecl(ParmVarDecl *D) {
    // Avoid visiting default arguments from the definition that were already
    // visited in the declaration.
    // FIXME: A free function definition can have default arguments.
    // Avoiding double visitaiton of default arguments should be handled by the
    // visitor probably with a bit in the AST to indicate if the attached
    // default argument was 'inherited' or written in source.
    if (auto FD = dyn_cast<FunctionDecl>(D->getDeclContext())) {
      if (FD->isThisDeclarationADefinition()) {
        return traverseParamVarHelper(D);
      }
    }

    return base::TraverseParmVarDecl(D);
  }

  bool TraverseNestedNameSpecifierLoc(NestedNameSpecifierLoc NNS) {
    IndexCtx.indexNestedNameSpecifierLoc(NNS, Parent, ParentDC);
    return true;
  }

  bool VisitTagTypeLoc(TagTypeLoc TL) {
    TagDecl *D = TL.getDecl();
    if (!IndexCtx.shouldIndexFunctionLocalSymbols() &&
        D->getParentFunctionOrMethod())
      return true;

    if (TL.isDefinition()) {
      IndexCtx.indexTagDecl(D);
      return true;
    }

    return IndexCtx.handleReference(D, TL.getNameLoc(),
                                    Parent, ParentDC, SymbolRoleSet(),
                                    Relations);
  }

  bool VisitObjCInterfaceTypeLoc(ObjCInterfaceTypeLoc TL) {
    return IndexCtx.handleReference(TL.getIFaceDecl(), TL.getNameLoc(),
                                    Parent, ParentDC, SymbolRoleSet(), Relations);
  }

  bool VisitObjCObjectTypeLoc(ObjCObjectTypeLoc TL) {
    for (unsigned i = 0, e = TL.getNumProtocols(); i != e; ++i) {
      IndexCtx.handleReference(TL.getProtocol(i), TL.getProtocolLoc(i),
                               Parent, ParentDC, SymbolRoleSet(), Relations);
    }
    return true;
  }

  template<typename TypeLocType>
  bool HandleTemplateSpecializationTypeLoc(TypeLocType TL) {
    if (const auto *T = TL.getTypePtr()) {
      if (IndexCtx.shouldIndexImplicitInstantiation()) {
        if (CXXRecordDecl *RD = T->getAsCXXRecordDecl()) {
          IndexCtx.handleReference(RD, TL.getTemplateNameLoc(),
                                   Parent, ParentDC, SymbolRoleSet(), Relations);
          return true;
        }
      }
      if (const TemplateDecl *D = T->getTemplateName().getAsTemplateDecl())
        IndexCtx.handleReference(D, TL.getTemplateNameLoc(), Parent, ParentDC,
                                 SymbolRoleSet(), Relations);
    }
    return true;
  }

  bool VisitTemplateSpecializationTypeLoc(TemplateSpecializationTypeLoc TL) {
    return HandleTemplateSpecializationTypeLoc(TL);
  }

  bool VisitDeducedTemplateSpecializationTypeLoc(DeducedTemplateSpecializationTypeLoc TL) {
    return HandleTemplateSpecializationTypeLoc(TL);
  }

  bool VisitDependentNameTypeLoc(DependentNameTypeLoc TL) {
    const DependentNameType *DNT = TL.getTypePtr();
    const NestedNameSpecifier *NNS = DNT->getQualifier();
    const Type *T = NNS->getAsType();
    if (!T)
      return true;
    const TemplateSpecializationType *TST =
        T->getAs<TemplateSpecializationType>();
    if (!TST)
      return true;
    TemplateName TN = TST->getTemplateName();
    const ClassTemplateDecl *TD =
        dyn_cast_or_null<ClassTemplateDecl>(TN.getAsTemplateDecl());
    if (!TD)
      return true;
    CXXRecordDecl *RD = TD->getTemplatedDecl();
    if (!RD->hasDefinition())
      return true;
    RD = RD->getDefinition();
    DeclarationName Name(DNT->getIdentifier());
    std::vector<const NamedDecl *> Symbols = RD->lookupDependentName(
        Name, [](const NamedDecl *ND) { return isa<TypeDecl>(ND); });
    if (Symbols.size() != 1)
      return true;
    return IndexCtx.handleReference(Symbols[0], TL.getNameLoc(), Parent,
                                    ParentDC, SymbolRoleSet(), Relations);
  }

  bool TraverseStmt(Stmt *S) {
    IndexCtx.indexBody(S, Parent, ParentDC);
    return true;
  }
};

} // anonymous namespace

void IndexingContext::indexTypeSourceInfo(TypeSourceInfo *TInfo,
                                          const NamedDecl *Parent,
                                          const DeclContext *DC,
                                          bool isBase,
                                          bool isIBType) {
  if (!TInfo || TInfo->getTypeLoc().isNull())
    return;

  indexTypeLoc(TInfo->getTypeLoc(), Parent, DC, isBase, isIBType);
}

void IndexingContext::indexTypeLoc(TypeLoc TL,
                                   const NamedDecl *Parent,
                                   const DeclContext *DC,
                                   bool isBase,
                                   bool isIBType) {
  if (TL.isNull())
    return;

  if (!DC)
    DC = Parent->getLexicalDeclContext();
  TypeIndexer(*this, Parent, DC, isBase, isIBType).TraverseTypeLoc(TL);
}

void IndexingContext::indexNestedNameSpecifierLoc(NestedNameSpecifierLoc NNS,
                                                  const NamedDecl *Parent,
                                                  const DeclContext *DC) {
  if (!NNS)
    return;

  if (NestedNameSpecifierLoc Prefix = NNS.getPrefix())
    indexNestedNameSpecifierLoc(Prefix, Parent, DC);

  if (!DC)
    DC = Parent->getLexicalDeclContext();
  SourceLocation Loc = NNS.getLocalBeginLoc();

  switch (NNS.getNestedNameSpecifier()->getKind()) {
  case NestedNameSpecifier::Identifier:
  case NestedNameSpecifier::Global:
  case NestedNameSpecifier::Super:
    break;

  case NestedNameSpecifier::Namespace:
    handleReference(NNS.getNestedNameSpecifier()->getAsNamespace(),
                    Loc, Parent, DC, SymbolRoleSet());
    break;
  case NestedNameSpecifier::NamespaceAlias:
    handleReference(NNS.getNestedNameSpecifier()->getAsNamespaceAlias(),
                    Loc, Parent, DC, SymbolRoleSet());
    break;

  case NestedNameSpecifier::TypeSpec:
  case NestedNameSpecifier::TypeSpecWithTemplate:
    indexTypeLoc(NNS.getTypeLoc(), Parent, DC);
    break;
  }
}

void IndexingContext::indexTagDecl(const TagDecl *D,
                                   ArrayRef<SymbolRelation> Relations) {
  if (!shouldIndex(D))
    return;
  if (!shouldIndexFunctionLocalSymbols() && isFunctionLocalSymbol(D))
    return;

  if (handleDecl(D, /*Roles=*/SymbolRoleSet(), Relations)) {
    if (D->isThisDeclarationADefinition()) {
      indexNestedNameSpecifierLoc(D->getQualifierLoc(), D);
      if (auto CXXRD = dyn_cast<CXXRecordDecl>(D)) {
        for (const auto &I : CXXRD->bases()) {
          indexTypeSourceInfo(I.getTypeSourceInfo(), CXXRD, CXXRD, /*isBase=*/true);
        }
      }
      indexDeclContext(D);
    }
  }
}
