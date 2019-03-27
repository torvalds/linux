//===-- ODRHash.cpp - Hashing to diagnose ODR failures ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the ODRHash class, which calculates a hash based
/// on AST nodes, which is stable across different runs.
///
//===----------------------------------------------------------------------===//

#include "clang/AST/ODRHash.h"

#include "clang/AST/DeclVisitor.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/AST/TypeVisitor.h"

using namespace clang;

void ODRHash::AddStmt(const Stmt *S) {
  assert(S && "Expecting non-null pointer.");
  S->ProcessODRHash(ID, *this);
}

void ODRHash::AddIdentifierInfo(const IdentifierInfo *II) {
  assert(II && "Expecting non-null pointer.");
  ID.AddString(II->getName());
}

void ODRHash::AddDeclarationName(DeclarationName Name, bool TreatAsDecl) {
  if (TreatAsDecl)
    // Matches the NamedDecl check in AddDecl
    AddBoolean(true);

  AddDeclarationNameImpl(Name);

  if (TreatAsDecl)
    // Matches the ClassTemplateSpecializationDecl check in AddDecl
    AddBoolean(false);
}

void ODRHash::AddDeclarationNameImpl(DeclarationName Name) {
  // Index all DeclarationName and use index numbers to refer to them.
  auto Result = DeclNameMap.insert(std::make_pair(Name, DeclNameMap.size()));
  ID.AddInteger(Result.first->second);
  if (!Result.second) {
    // If found in map, the DeclarationName has previously been processed.
    return;
  }

  // First time processing each DeclarationName, also process its details.
  AddBoolean(Name.isEmpty());
  if (Name.isEmpty())
    return;

  auto Kind = Name.getNameKind();
  ID.AddInteger(Kind);
  switch (Kind) {
  case DeclarationName::Identifier:
    AddIdentifierInfo(Name.getAsIdentifierInfo());
    break;
  case DeclarationName::ObjCZeroArgSelector:
  case DeclarationName::ObjCOneArgSelector:
  case DeclarationName::ObjCMultiArgSelector: {
    Selector S = Name.getObjCSelector();
    AddBoolean(S.isNull());
    AddBoolean(S.isKeywordSelector());
    AddBoolean(S.isUnarySelector());
    unsigned NumArgs = S.getNumArgs();
    for (unsigned i = 0; i < NumArgs; ++i) {
      AddIdentifierInfo(S.getIdentifierInfoForSlot(i));
    }
    break;
  }
  case DeclarationName::CXXConstructorName:
  case DeclarationName::CXXDestructorName:
    AddQualType(Name.getCXXNameType());
    break;
  case DeclarationName::CXXOperatorName:
    ID.AddInteger(Name.getCXXOverloadedOperator());
    break;
  case DeclarationName::CXXLiteralOperatorName:
    AddIdentifierInfo(Name.getCXXLiteralIdentifier());
    break;
  case DeclarationName::CXXConversionFunctionName:
    AddQualType(Name.getCXXNameType());
    break;
  case DeclarationName::CXXUsingDirective:
    break;
  case DeclarationName::CXXDeductionGuideName: {
    auto *Template = Name.getCXXDeductionGuideTemplate();
    AddBoolean(Template);
    if (Template) {
      AddDecl(Template);
    }
  }
  }
}

void ODRHash::AddNestedNameSpecifier(const NestedNameSpecifier *NNS) {
  assert(NNS && "Expecting non-null pointer.");
  const auto *Prefix = NNS->getPrefix();
  AddBoolean(Prefix);
  if (Prefix) {
    AddNestedNameSpecifier(Prefix);
  }
  auto Kind = NNS->getKind();
  ID.AddInteger(Kind);
  switch (Kind) {
  case NestedNameSpecifier::Identifier:
    AddIdentifierInfo(NNS->getAsIdentifier());
    break;
  case NestedNameSpecifier::Namespace:
    AddDecl(NNS->getAsNamespace());
    break;
  case NestedNameSpecifier::NamespaceAlias:
    AddDecl(NNS->getAsNamespaceAlias());
    break;
  case NestedNameSpecifier::TypeSpec:
  case NestedNameSpecifier::TypeSpecWithTemplate:
    AddType(NNS->getAsType());
    break;
  case NestedNameSpecifier::Global:
  case NestedNameSpecifier::Super:
    break;
  }
}

void ODRHash::AddTemplateName(TemplateName Name) {
  auto Kind = Name.getKind();
  ID.AddInteger(Kind);

  switch (Kind) {
  case TemplateName::Template:
    AddDecl(Name.getAsTemplateDecl());
    break;
  // TODO: Support these cases.
  case TemplateName::OverloadedTemplate:
  case TemplateName::QualifiedTemplate:
  case TemplateName::DependentTemplate:
  case TemplateName::SubstTemplateTemplateParm:
  case TemplateName::SubstTemplateTemplateParmPack:
    break;
  }
}

void ODRHash::AddTemplateArgument(TemplateArgument TA) {
  const auto Kind = TA.getKind();
  ID.AddInteger(Kind);

  switch (Kind) {
    case TemplateArgument::Null:
      llvm_unreachable("Expected valid TemplateArgument");
    case TemplateArgument::Type:
      AddQualType(TA.getAsType());
      break;
    case TemplateArgument::Declaration:
      AddDecl(TA.getAsDecl());
      break;
    case TemplateArgument::NullPtr:
    case TemplateArgument::Integral:
      break;
    case TemplateArgument::Template:
    case TemplateArgument::TemplateExpansion:
      AddTemplateName(TA.getAsTemplateOrTemplatePattern());
      break;
    case TemplateArgument::Expression:
      AddStmt(TA.getAsExpr());
      break;
    case TemplateArgument::Pack:
      ID.AddInteger(TA.pack_size());
      for (auto SubTA : TA.pack_elements()) {
        AddTemplateArgument(SubTA);
      }
      break;
  }
}

void ODRHash::AddTemplateParameterList(const TemplateParameterList *TPL) {
  assert(TPL && "Expecting non-null pointer.");

  ID.AddInteger(TPL->size());
  for (auto *ND : TPL->asArray()) {
    AddSubDecl(ND);
  }
}

void ODRHash::clear() {
  DeclNameMap.clear();
  Bools.clear();
  ID.clear();
}

unsigned ODRHash::CalculateHash() {
  // Append the bools to the end of the data segment backwards.  This allows
  // for the bools data to be compressed 32 times smaller compared to using
  // ID.AddBoolean
  const unsigned unsigned_bits = sizeof(unsigned) * CHAR_BIT;
  const unsigned size = Bools.size();
  const unsigned remainder = size % unsigned_bits;
  const unsigned loops = size / unsigned_bits;
  auto I = Bools.rbegin();
  unsigned value = 0;
  for (unsigned i = 0; i < remainder; ++i) {
    value <<= 1;
    value |= *I;
    ++I;
  }
  ID.AddInteger(value);

  for (unsigned i = 0; i < loops; ++i) {
    value = 0;
    for (unsigned j = 0; j < unsigned_bits; ++j) {
      value <<= 1;
      value |= *I;
      ++I;
    }
    ID.AddInteger(value);
  }

  assert(I == Bools.rend());
  Bools.clear();
  return ID.ComputeHash();
}

namespace {
// Process a Decl pointer.  Add* methods call back into ODRHash while Visit*
// methods process the relevant parts of the Decl.
class ODRDeclVisitor : public ConstDeclVisitor<ODRDeclVisitor> {
  typedef ConstDeclVisitor<ODRDeclVisitor> Inherited;
  llvm::FoldingSetNodeID &ID;
  ODRHash &Hash;

public:
  ODRDeclVisitor(llvm::FoldingSetNodeID &ID, ODRHash &Hash)
      : ID(ID), Hash(Hash) {}

  void AddStmt(const Stmt *S) {
    Hash.AddBoolean(S);
    if (S) {
      Hash.AddStmt(S);
    }
  }

  void AddIdentifierInfo(const IdentifierInfo *II) {
    Hash.AddBoolean(II);
    if (II) {
      Hash.AddIdentifierInfo(II);
    }
  }

  void AddQualType(QualType T) {
    Hash.AddQualType(T);
  }

  void AddDecl(const Decl *D) {
    Hash.AddBoolean(D);
    if (D) {
      Hash.AddDecl(D);
    }
  }

  void AddTemplateArgument(TemplateArgument TA) {
    Hash.AddTemplateArgument(TA);
  }

  void Visit(const Decl *D) {
    ID.AddInteger(D->getKind());
    Inherited::Visit(D);
  }

  void VisitNamedDecl(const NamedDecl *D) {
    Hash.AddDeclarationName(D->getDeclName());
    Inherited::VisitNamedDecl(D);
  }

  void VisitValueDecl(const ValueDecl *D) {
    if (!isa<FunctionDecl>(D)) {
      AddQualType(D->getType());
    }
    Inherited::VisitValueDecl(D);
  }

  void VisitVarDecl(const VarDecl *D) {
    Hash.AddBoolean(D->isStaticLocal());
    Hash.AddBoolean(D->isConstexpr());
    const bool HasInit = D->hasInit();
    Hash.AddBoolean(HasInit);
    if (HasInit) {
      AddStmt(D->getInit());
    }
    Inherited::VisitVarDecl(D);
  }

  void VisitParmVarDecl(const ParmVarDecl *D) {
    // TODO: Handle default arguments.
    Inherited::VisitParmVarDecl(D);
  }

  void VisitAccessSpecDecl(const AccessSpecDecl *D) {
    ID.AddInteger(D->getAccess());
    Inherited::VisitAccessSpecDecl(D);
  }

  void VisitStaticAssertDecl(const StaticAssertDecl *D) {
    AddStmt(D->getAssertExpr());
    AddStmt(D->getMessage());

    Inherited::VisitStaticAssertDecl(D);
  }

  void VisitFieldDecl(const FieldDecl *D) {
    const bool IsBitfield = D->isBitField();
    Hash.AddBoolean(IsBitfield);

    if (IsBitfield) {
      AddStmt(D->getBitWidth());
    }

    Hash.AddBoolean(D->isMutable());
    AddStmt(D->getInClassInitializer());

    Inherited::VisitFieldDecl(D);
  }

  void VisitFunctionDecl(const FunctionDecl *D) {
    // Handled by the ODRHash for FunctionDecl
    ID.AddInteger(D->getODRHash());

    Inherited::VisitFunctionDecl(D);
  }

  void VisitCXXMethodDecl(const CXXMethodDecl *D) {
    // Handled by the ODRHash for FunctionDecl

    Inherited::VisitCXXMethodDecl(D);
  }

  void VisitTypedefNameDecl(const TypedefNameDecl *D) {
    AddQualType(D->getUnderlyingType());

    Inherited::VisitTypedefNameDecl(D);
  }

  void VisitTypedefDecl(const TypedefDecl *D) {
    Inherited::VisitTypedefDecl(D);
  }

  void VisitTypeAliasDecl(const TypeAliasDecl *D) {
    Inherited::VisitTypeAliasDecl(D);
  }

  void VisitFriendDecl(const FriendDecl *D) {
    TypeSourceInfo *TSI = D->getFriendType();
    Hash.AddBoolean(TSI);
    if (TSI) {
      AddQualType(TSI->getType());
    } else {
      AddDecl(D->getFriendDecl());
    }
  }

  void VisitTemplateTypeParmDecl(const TemplateTypeParmDecl *D) {
    // Only care about default arguments as part of the definition.
    const bool hasDefaultArgument =
        D->hasDefaultArgument() && !D->defaultArgumentWasInherited();
    Hash.AddBoolean(hasDefaultArgument);
    if (hasDefaultArgument) {
      AddTemplateArgument(D->getDefaultArgument());
    }
    Hash.AddBoolean(D->isParameterPack());

    Inherited::VisitTemplateTypeParmDecl(D);
  }

  void VisitNonTypeTemplateParmDecl(const NonTypeTemplateParmDecl *D) {
    // Only care about default arguments as part of the definition.
    const bool hasDefaultArgument =
        D->hasDefaultArgument() && !D->defaultArgumentWasInherited();
    Hash.AddBoolean(hasDefaultArgument);
    if (hasDefaultArgument) {
      AddStmt(D->getDefaultArgument());
    }
    Hash.AddBoolean(D->isParameterPack());

    Inherited::VisitNonTypeTemplateParmDecl(D);
  }

  void VisitTemplateTemplateParmDecl(const TemplateTemplateParmDecl *D) {
    // Only care about default arguments as part of the definition.
    const bool hasDefaultArgument =
        D->hasDefaultArgument() && !D->defaultArgumentWasInherited();
    Hash.AddBoolean(hasDefaultArgument);
    if (hasDefaultArgument) {
      AddTemplateArgument(D->getDefaultArgument().getArgument());
    }
    Hash.AddBoolean(D->isParameterPack());

    Inherited::VisitTemplateTemplateParmDecl(D);
  }

  void VisitTemplateDecl(const TemplateDecl *D) {
    Hash.AddTemplateParameterList(D->getTemplateParameters());

    Inherited::VisitTemplateDecl(D);
  }

  void VisitRedeclarableTemplateDecl(const RedeclarableTemplateDecl *D) {
    Hash.AddBoolean(D->isMemberSpecialization());
    Inherited::VisitRedeclarableTemplateDecl(D);
  }

  void VisitFunctionTemplateDecl(const FunctionTemplateDecl *D) {
    AddDecl(D->getTemplatedDecl());
    ID.AddInteger(D->getTemplatedDecl()->getODRHash());
    Inherited::VisitFunctionTemplateDecl(D);
  }

  void VisitEnumConstantDecl(const EnumConstantDecl *D) {
    AddStmt(D->getInitExpr());
    Inherited::VisitEnumConstantDecl(D);
  }
};
} // namespace

// Only allow a small portion of Decl's to be processed.  Remove this once
// all Decl's can be handled.
bool ODRHash::isWhitelistedDecl(const Decl *D, const DeclContext *Parent) {
  if (D->isImplicit()) return false;
  if (D->getDeclContext() != Parent) return false;

  switch (D->getKind()) {
    default:
      return false;
    case Decl::AccessSpec:
    case Decl::CXXConstructor:
    case Decl::CXXDestructor:
    case Decl::CXXMethod:
    case Decl::EnumConstant: // Only found in EnumDecl's.
    case Decl::Field:
    case Decl::Friend:
    case Decl::FunctionTemplate:
    case Decl::StaticAssert:
    case Decl::TypeAlias:
    case Decl::Typedef:
    case Decl::Var:
      return true;
  }
}

void ODRHash::AddSubDecl(const Decl *D) {
  assert(D && "Expecting non-null pointer.");

  ODRDeclVisitor(ID, *this).Visit(D);
}

void ODRHash::AddCXXRecordDecl(const CXXRecordDecl *Record) {
  assert(Record && Record->hasDefinition() &&
         "Expected non-null record to be a definition.");

  const DeclContext *DC = Record;
  while (DC) {
    if (isa<ClassTemplateSpecializationDecl>(DC)) {
      return;
    }
    DC = DC->getParent();
  }

  AddDecl(Record);

  // Filter out sub-Decls which will not be processed in order to get an
  // accurate count of Decl's.
  llvm::SmallVector<const Decl *, 16> Decls;
  for (Decl *SubDecl : Record->decls()) {
    if (isWhitelistedDecl(SubDecl, Record)) {
      Decls.push_back(SubDecl);
      if (auto *Function = dyn_cast<FunctionDecl>(SubDecl)) {
        // Compute/Preload ODRHash into FunctionDecl.
        Function->getODRHash();
      }
    }
  }

  ID.AddInteger(Decls.size());
  for (auto SubDecl : Decls) {
    AddSubDecl(SubDecl);
  }

  const ClassTemplateDecl *TD = Record->getDescribedClassTemplate();
  AddBoolean(TD);
  if (TD) {
    AddTemplateParameterList(TD->getTemplateParameters());
  }

  ID.AddInteger(Record->getNumBases());
  auto Bases = Record->bases();
  for (auto Base : Bases) {
    AddQualType(Base.getType());
    ID.AddInteger(Base.isVirtual());
    ID.AddInteger(Base.getAccessSpecifierAsWritten());
  }
}

void ODRHash::AddFunctionDecl(const FunctionDecl *Function,
                              bool SkipBody) {
  assert(Function && "Expecting non-null pointer.");

  // Skip functions that are specializations or in specialization context.
  const DeclContext *DC = Function;
  while (DC) {
    if (isa<ClassTemplateSpecializationDecl>(DC)) return;
    if (auto *F = dyn_cast<FunctionDecl>(DC)) {
      if (F->isFunctionTemplateSpecialization()) {
        if (!isa<CXXMethodDecl>(DC)) return;
        if (DC->getLexicalParent()->isFileContext()) return;
        // Inline method specializations are the only supported
        // specialization for now.
      }
    }
    DC = DC->getParent();
  }

  ID.AddInteger(Function->getDeclKind());

  const auto *SpecializationArgs = Function->getTemplateSpecializationArgs();
  AddBoolean(SpecializationArgs);
  if (SpecializationArgs) {
    ID.AddInteger(SpecializationArgs->size());
    for (const TemplateArgument &TA : SpecializationArgs->asArray()) {
      AddTemplateArgument(TA);
    }
  }

  if (const auto *Method = dyn_cast<CXXMethodDecl>(Function)) {
    AddBoolean(Method->isConst());
    AddBoolean(Method->isVolatile());
  }

  ID.AddInteger(Function->getStorageClass());
  AddBoolean(Function->isInlineSpecified());
  AddBoolean(Function->isVirtualAsWritten());
  AddBoolean(Function->isPure());
  AddBoolean(Function->isDeletedAsWritten());
  AddBoolean(Function->isExplicitlyDefaulted());

  AddDecl(Function);

  AddQualType(Function->getReturnType());

  ID.AddInteger(Function->param_size());
  for (auto Param : Function->parameters())
    AddSubDecl(Param);

  if (SkipBody) {
    AddBoolean(false);
    return;
  }

  const bool HasBody = Function->isThisDeclarationADefinition() &&
                       !Function->isDefaulted() && !Function->isDeleted() &&
                       !Function->isLateTemplateParsed();
  AddBoolean(HasBody);
  if (!HasBody) {
    return;
  }

  auto *Body = Function->getBody();
  AddBoolean(Body);
  if (Body)
    AddStmt(Body);

  // Filter out sub-Decls which will not be processed in order to get an
  // accurate count of Decl's.
  llvm::SmallVector<const Decl *, 16> Decls;
  for (Decl *SubDecl : Function->decls()) {
    if (isWhitelistedDecl(SubDecl, Function)) {
      Decls.push_back(SubDecl);
    }
  }

  ID.AddInteger(Decls.size());
  for (auto SubDecl : Decls) {
    AddSubDecl(SubDecl);
  }
}

void ODRHash::AddEnumDecl(const EnumDecl *Enum) {
  assert(Enum);
  AddDeclarationName(Enum->getDeclName());

  AddBoolean(Enum->isScoped());
  if (Enum->isScoped())
    AddBoolean(Enum->isScopedUsingClassTag());

  if (Enum->getIntegerTypeSourceInfo())
    AddQualType(Enum->getIntegerType());

  // Filter out sub-Decls which will not be processed in order to get an
  // accurate count of Decl's.
  llvm::SmallVector<const Decl *, 16> Decls;
  for (Decl *SubDecl : Enum->decls()) {
    if (isWhitelistedDecl(SubDecl, Enum)) {
      assert(isa<EnumConstantDecl>(SubDecl) && "Unexpected Decl");
      Decls.push_back(SubDecl);
    }
  }

  ID.AddInteger(Decls.size());
  for (auto SubDecl : Decls) {
    AddSubDecl(SubDecl);
  }

}

void ODRHash::AddDecl(const Decl *D) {
  assert(D && "Expecting non-null pointer.");
  D = D->getCanonicalDecl();

  const NamedDecl *ND = dyn_cast<NamedDecl>(D);
  AddBoolean(ND);
  if (!ND) {
    ID.AddInteger(D->getKind());
    return;
  }

  AddDeclarationName(ND->getDeclName());

  const auto *Specialization =
            dyn_cast<ClassTemplateSpecializationDecl>(D);
  AddBoolean(Specialization);
  if (Specialization) {
    const TemplateArgumentList &List = Specialization->getTemplateArgs();
    ID.AddInteger(List.size());
    for (const TemplateArgument &TA : List.asArray())
      AddTemplateArgument(TA);
  }
}

namespace {
// Process a Type pointer.  Add* methods call back into ODRHash while Visit*
// methods process the relevant parts of the Type.
class ODRTypeVisitor : public TypeVisitor<ODRTypeVisitor> {
  typedef TypeVisitor<ODRTypeVisitor> Inherited;
  llvm::FoldingSetNodeID &ID;
  ODRHash &Hash;

public:
  ODRTypeVisitor(llvm::FoldingSetNodeID &ID, ODRHash &Hash)
      : ID(ID), Hash(Hash) {}

  void AddStmt(Stmt *S) {
    Hash.AddBoolean(S);
    if (S) {
      Hash.AddStmt(S);
    }
  }

  void AddDecl(Decl *D) {
    Hash.AddBoolean(D);
    if (D) {
      Hash.AddDecl(D);
    }
  }

  void AddQualType(QualType T) {
    Hash.AddQualType(T);
  }

  void AddType(const Type *T) {
    Hash.AddBoolean(T);
    if (T) {
      Hash.AddType(T);
    }
  }

  void AddNestedNameSpecifier(const NestedNameSpecifier *NNS) {
    Hash.AddBoolean(NNS);
    if (NNS) {
      Hash.AddNestedNameSpecifier(NNS);
    }
  }

  void AddIdentifierInfo(const IdentifierInfo *II) {
    Hash.AddBoolean(II);
    if (II) {
      Hash.AddIdentifierInfo(II);
    }
  }

  void VisitQualifiers(Qualifiers Quals) {
    ID.AddInteger(Quals.getAsOpaqueValue());
  }

  void Visit(const Type *T) {
    ID.AddInteger(T->getTypeClass());
    Inherited::Visit(T);
  }

  void VisitType(const Type *T) {}

  void VisitAdjustedType(const AdjustedType *T) {
    AddQualType(T->getOriginalType());
    AddQualType(T->getAdjustedType());
    VisitType(T);
  }

  void VisitDecayedType(const DecayedType *T) {
    AddQualType(T->getDecayedType());
    AddQualType(T->getPointeeType());
    VisitAdjustedType(T);
  }

  void VisitArrayType(const ArrayType *T) {
    AddQualType(T->getElementType());
    ID.AddInteger(T->getSizeModifier());
    VisitQualifiers(T->getIndexTypeQualifiers());
    VisitType(T);
  }
  void VisitConstantArrayType(const ConstantArrayType *T) {
    T->getSize().Profile(ID);
    VisitArrayType(T);
  }

  void VisitDependentSizedArrayType(const DependentSizedArrayType *T) {
    AddStmt(T->getSizeExpr());
    VisitArrayType(T);
  }

  void VisitIncompleteArrayType(const IncompleteArrayType *T) {
    VisitArrayType(T);
  }

  void VisitVariableArrayType(const VariableArrayType *T) {
    AddStmt(T->getSizeExpr());
    VisitArrayType(T);
  }

  void VisitAttributedType(const AttributedType *T) {
    ID.AddInteger(T->getAttrKind());
    AddQualType(T->getModifiedType());
    AddQualType(T->getEquivalentType());

    VisitType(T);
  }

  void VisitBlockPointerType(const BlockPointerType *T) {
    AddQualType(T->getPointeeType());
    VisitType(T);
  }

  void VisitBuiltinType(const BuiltinType *T) {
    ID.AddInteger(T->getKind());
    VisitType(T);
  }

  void VisitComplexType(const ComplexType *T) {
    AddQualType(T->getElementType());
    VisitType(T);
  }

  void VisitDecltypeType(const DecltypeType *T) {
    AddStmt(T->getUnderlyingExpr());
    AddQualType(T->getUnderlyingType());
    VisitType(T);
  }

  void VisitDependentDecltypeType(const DependentDecltypeType *T) {
    VisitDecltypeType(T);
  }

  void VisitDeducedType(const DeducedType *T) {
    AddQualType(T->getDeducedType());
    VisitType(T);
  }

  void VisitAutoType(const AutoType *T) {
    ID.AddInteger((unsigned)T->getKeyword());
    VisitDeducedType(T);
  }

  void VisitDeducedTemplateSpecializationType(
      const DeducedTemplateSpecializationType *T) {
    Hash.AddTemplateName(T->getTemplateName());
    VisitDeducedType(T);
  }

  void VisitDependentAddressSpaceType(const DependentAddressSpaceType *T) {
    AddQualType(T->getPointeeType());
    AddStmt(T->getAddrSpaceExpr());
    VisitType(T);
  }

  void VisitDependentSizedExtVectorType(const DependentSizedExtVectorType *T) {
    AddQualType(T->getElementType());
    AddStmt(T->getSizeExpr());
    VisitType(T);
  }

  void VisitFunctionType(const FunctionType *T) {
    AddQualType(T->getReturnType());
    T->getExtInfo().Profile(ID);
    Hash.AddBoolean(T->isConst());
    Hash.AddBoolean(T->isVolatile());
    Hash.AddBoolean(T->isRestrict());
    VisitType(T);
  }

  void VisitFunctionNoProtoType(const FunctionNoProtoType *T) {
    VisitFunctionType(T);
  }

  void VisitFunctionProtoType(const FunctionProtoType *T) {
    ID.AddInteger(T->getNumParams());
    for (auto ParamType : T->getParamTypes())
      AddQualType(ParamType);

    VisitFunctionType(T);
  }

  void VisitInjectedClassNameType(const InjectedClassNameType *T) {
    AddDecl(T->getDecl());
    VisitType(T);
  }

  void VisitMemberPointerType(const MemberPointerType *T) {
    AddQualType(T->getPointeeType());
    AddType(T->getClass());
    VisitType(T);
  }

  void VisitObjCObjectPointerType(const ObjCObjectPointerType *T) {
    AddQualType(T->getPointeeType());
    VisitType(T);
  }

  void VisitObjCObjectType(const ObjCObjectType *T) {
    AddDecl(T->getInterface());

    auto TypeArgs = T->getTypeArgsAsWritten();
    ID.AddInteger(TypeArgs.size());
    for (auto Arg : TypeArgs) {
      AddQualType(Arg);
    }

    auto Protocols = T->getProtocols();
    ID.AddInteger(Protocols.size());
    for (auto Protocol : Protocols) {
      AddDecl(Protocol);
    }

    Hash.AddBoolean(T->isKindOfType());

    VisitType(T);
  }

  void VisitObjCInterfaceType(const ObjCInterfaceType *T) {
    // This type is handled by the parent type ObjCObjectType.
    VisitObjCObjectType(T);
  }

  void VisitObjCTypeParamType(const ObjCTypeParamType *T) {
    AddDecl(T->getDecl());
    auto Protocols = T->getProtocols();
    ID.AddInteger(Protocols.size());
    for (auto Protocol : Protocols) {
      AddDecl(Protocol);
    }

    VisitType(T);
  }

  void VisitPackExpansionType(const PackExpansionType *T) {
    AddQualType(T->getPattern());
    VisitType(T);
  }

  void VisitParenType(const ParenType *T) {
    AddQualType(T->getInnerType());
    VisitType(T);
  }

  void VisitPipeType(const PipeType *T) {
    AddQualType(T->getElementType());
    Hash.AddBoolean(T->isReadOnly());
    VisitType(T);
  }

  void VisitPointerType(const PointerType *T) {
    AddQualType(T->getPointeeType());
    VisitType(T);
  }

  void VisitReferenceType(const ReferenceType *T) {
    AddQualType(T->getPointeeTypeAsWritten());
    VisitType(T);
  }

  void VisitLValueReferenceType(const LValueReferenceType *T) {
    VisitReferenceType(T);
  }

  void VisitRValueReferenceType(const RValueReferenceType *T) {
    VisitReferenceType(T);
  }

  void
  VisitSubstTemplateTypeParmPackType(const SubstTemplateTypeParmPackType *T) {
    AddType(T->getReplacedParameter());
    Hash.AddTemplateArgument(T->getArgumentPack());
    VisitType(T);
  }

  void VisitSubstTemplateTypeParmType(const SubstTemplateTypeParmType *T) {
    AddType(T->getReplacedParameter());
    AddQualType(T->getReplacementType());
    VisitType(T);
  }

  void VisitTagType(const TagType *T) {
    AddDecl(T->getDecl());
    VisitType(T);
  }

  void VisitRecordType(const RecordType *T) { VisitTagType(T); }
  void VisitEnumType(const EnumType *T) { VisitTagType(T); }

  void VisitTemplateSpecializationType(const TemplateSpecializationType *T) {
    ID.AddInteger(T->getNumArgs());
    for (const auto &TA : T->template_arguments()) {
      Hash.AddTemplateArgument(TA);
    }
    Hash.AddTemplateName(T->getTemplateName());
    VisitType(T);
  }

  void VisitTemplateTypeParmType(const TemplateTypeParmType *T) {
    ID.AddInteger(T->getDepth());
    ID.AddInteger(T->getIndex());
    Hash.AddBoolean(T->isParameterPack());
    AddDecl(T->getDecl());
  }

  void VisitTypedefType(const TypedefType *T) {
    AddDecl(T->getDecl());
    QualType UnderlyingType = T->getDecl()->getUnderlyingType();
    VisitQualifiers(UnderlyingType.getQualifiers());
    while (true) {
      if (const TypedefType *Underlying =
              dyn_cast<TypedefType>(UnderlyingType.getTypePtr())) {
        UnderlyingType = Underlying->getDecl()->getUnderlyingType();
        continue;
      }
      if (const ElaboratedType *Underlying =
              dyn_cast<ElaboratedType>(UnderlyingType.getTypePtr())) {
        UnderlyingType = Underlying->getNamedType();
        continue;
      }

      break;
    }
    AddType(UnderlyingType.getTypePtr());
    VisitType(T);
  }

  void VisitTypeOfExprType(const TypeOfExprType *T) {
    AddStmt(T->getUnderlyingExpr());
    Hash.AddBoolean(T->isSugared());
    if (T->isSugared())
      AddQualType(T->desugar());

    VisitType(T);
  }
  void VisitTypeOfType(const TypeOfType *T) {
    AddQualType(T->getUnderlyingType());
    VisitType(T);
  }

  void VisitTypeWithKeyword(const TypeWithKeyword *T) {
    ID.AddInteger(T->getKeyword());
    VisitType(T);
  };

  void VisitDependentNameType(const DependentNameType *T) {
    AddNestedNameSpecifier(T->getQualifier());
    AddIdentifierInfo(T->getIdentifier());
    VisitTypeWithKeyword(T);
  }

  void VisitDependentTemplateSpecializationType(
      const DependentTemplateSpecializationType *T) {
    AddIdentifierInfo(T->getIdentifier());
    AddNestedNameSpecifier(T->getQualifier());
    ID.AddInteger(T->getNumArgs());
    for (const auto &TA : T->template_arguments()) {
      Hash.AddTemplateArgument(TA);
    }
    VisitTypeWithKeyword(T);
  }

  void VisitElaboratedType(const ElaboratedType *T) {
    AddNestedNameSpecifier(T->getQualifier());
    AddQualType(T->getNamedType());
    VisitTypeWithKeyword(T);
  }

  void VisitUnaryTransformType(const UnaryTransformType *T) {
    AddQualType(T->getUnderlyingType());
    AddQualType(T->getBaseType());
    VisitType(T);
  }

  void VisitUnresolvedUsingType(const UnresolvedUsingType *T) {
    AddDecl(T->getDecl());
    VisitType(T);
  }

  void VisitVectorType(const VectorType *T) {
    AddQualType(T->getElementType());
    ID.AddInteger(T->getNumElements());
    ID.AddInteger(T->getVectorKind());
    VisitType(T);
  }

  void VisitExtVectorType(const ExtVectorType * T) {
    VisitVectorType(T);
  }
};
} // namespace

void ODRHash::AddType(const Type *T) {
  assert(T && "Expecting non-null pointer.");
  ODRTypeVisitor(ID, *this).Visit(T);
}

void ODRHash::AddQualType(QualType T) {
  AddBoolean(T.isNull());
  if (T.isNull())
    return;
  SplitQualType split = T.split();
  ID.AddInteger(split.Quals.getAsOpaqueValue());
  AddType(split.Ty);
}

void ODRHash::AddBoolean(bool Value) {
  Bools.push_back(Value);
}
