//===--- ASTWriterDecl.cpp - Declaration Serialization --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements serialization for Declarations.
//
//===----------------------------------------------------------------------===//

#include "ASTCommon.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclContextInternals.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/DeclVisitor.h"
#include "clang/AST/Expr.h"
#include "clang/AST/OpenMPClause.h"
#include "clang/AST/PrettyDeclStackTrace.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Serialization/ASTReader.h"
#include "clang/Serialization/ASTWriter.h"
#include "llvm/Bitcode/BitstreamWriter.h"
#include "llvm/Support/ErrorHandling.h"
using namespace clang;
using namespace serialization;

//===----------------------------------------------------------------------===//
// Declaration serialization
//===----------------------------------------------------------------------===//

namespace clang {
  class ASTDeclWriter : public DeclVisitor<ASTDeclWriter, void> {
    ASTWriter &Writer;
    ASTContext &Context;
    ASTRecordWriter Record;

    serialization::DeclCode Code;
    unsigned AbbrevToUse;

  public:
    ASTDeclWriter(ASTWriter &Writer, ASTContext &Context,
                  ASTWriter::RecordDataImpl &Record)
        : Writer(Writer), Context(Context), Record(Writer, Record),
          Code((serialization::DeclCode)0), AbbrevToUse(0) {}

    uint64_t Emit(Decl *D) {
      if (!Code)
        llvm::report_fatal_error(StringRef("unexpected declaration kind '") +
            D->getDeclKindName() + "'");
      return Record.Emit(Code, AbbrevToUse);
    }

    void Visit(Decl *D);

    void VisitDecl(Decl *D);
    void VisitPragmaCommentDecl(PragmaCommentDecl *D);
    void VisitPragmaDetectMismatchDecl(PragmaDetectMismatchDecl *D);
    void VisitTranslationUnitDecl(TranslationUnitDecl *D);
    void VisitNamedDecl(NamedDecl *D);
    void VisitLabelDecl(LabelDecl *LD);
    void VisitNamespaceDecl(NamespaceDecl *D);
    void VisitUsingDirectiveDecl(UsingDirectiveDecl *D);
    void VisitNamespaceAliasDecl(NamespaceAliasDecl *D);
    void VisitTypeDecl(TypeDecl *D);
    void VisitTypedefNameDecl(TypedefNameDecl *D);
    void VisitTypedefDecl(TypedefDecl *D);
    void VisitTypeAliasDecl(TypeAliasDecl *D);
    void VisitUnresolvedUsingTypenameDecl(UnresolvedUsingTypenameDecl *D);
    void VisitTagDecl(TagDecl *D);
    void VisitEnumDecl(EnumDecl *D);
    void VisitRecordDecl(RecordDecl *D);
    void VisitCXXRecordDecl(CXXRecordDecl *D);
    void VisitClassTemplateSpecializationDecl(
                                            ClassTemplateSpecializationDecl *D);
    void VisitClassTemplatePartialSpecializationDecl(
                                     ClassTemplatePartialSpecializationDecl *D);
    void VisitVarTemplateSpecializationDecl(VarTemplateSpecializationDecl *D);
    void VisitVarTemplatePartialSpecializationDecl(
        VarTemplatePartialSpecializationDecl *D);
    void VisitClassScopeFunctionSpecializationDecl(
                                       ClassScopeFunctionSpecializationDecl *D);
    void VisitTemplateTypeParmDecl(TemplateTypeParmDecl *D);
    void VisitValueDecl(ValueDecl *D);
    void VisitEnumConstantDecl(EnumConstantDecl *D);
    void VisitUnresolvedUsingValueDecl(UnresolvedUsingValueDecl *D);
    void VisitDeclaratorDecl(DeclaratorDecl *D);
    void VisitFunctionDecl(FunctionDecl *D);
    void VisitCXXDeductionGuideDecl(CXXDeductionGuideDecl *D);
    void VisitCXXMethodDecl(CXXMethodDecl *D);
    void VisitCXXConstructorDecl(CXXConstructorDecl *D);
    void VisitCXXDestructorDecl(CXXDestructorDecl *D);
    void VisitCXXConversionDecl(CXXConversionDecl *D);
    void VisitFieldDecl(FieldDecl *D);
    void VisitMSPropertyDecl(MSPropertyDecl *D);
    void VisitIndirectFieldDecl(IndirectFieldDecl *D);
    void VisitVarDecl(VarDecl *D);
    void VisitImplicitParamDecl(ImplicitParamDecl *D);
    void VisitParmVarDecl(ParmVarDecl *D);
    void VisitDecompositionDecl(DecompositionDecl *D);
    void VisitBindingDecl(BindingDecl *D);
    void VisitNonTypeTemplateParmDecl(NonTypeTemplateParmDecl *D);
    void VisitTemplateDecl(TemplateDecl *D);
    void VisitRedeclarableTemplateDecl(RedeclarableTemplateDecl *D);
    void VisitClassTemplateDecl(ClassTemplateDecl *D);
    void VisitVarTemplateDecl(VarTemplateDecl *D);
    void VisitFunctionTemplateDecl(FunctionTemplateDecl *D);
    void VisitTemplateTemplateParmDecl(TemplateTemplateParmDecl *D);
    void VisitTypeAliasTemplateDecl(TypeAliasTemplateDecl *D);
    void VisitUsingDecl(UsingDecl *D);
    void VisitUsingPackDecl(UsingPackDecl *D);
    void VisitUsingShadowDecl(UsingShadowDecl *D);
    void VisitConstructorUsingShadowDecl(ConstructorUsingShadowDecl *D);
    void VisitLinkageSpecDecl(LinkageSpecDecl *D);
    void VisitExportDecl(ExportDecl *D);
    void VisitFileScopeAsmDecl(FileScopeAsmDecl *D);
    void VisitImportDecl(ImportDecl *D);
    void VisitAccessSpecDecl(AccessSpecDecl *D);
    void VisitFriendDecl(FriendDecl *D);
    void VisitFriendTemplateDecl(FriendTemplateDecl *D);
    void VisitStaticAssertDecl(StaticAssertDecl *D);
    void VisitBlockDecl(BlockDecl *D);
    void VisitCapturedDecl(CapturedDecl *D);
    void VisitEmptyDecl(EmptyDecl *D);

    void VisitDeclContext(DeclContext *DC);
    template <typename T> void VisitRedeclarable(Redeclarable<T> *D);


    // FIXME: Put in the same order is DeclNodes.td?
    void VisitObjCMethodDecl(ObjCMethodDecl *D);
    void VisitObjCTypeParamDecl(ObjCTypeParamDecl *D);
    void VisitObjCContainerDecl(ObjCContainerDecl *D);
    void VisitObjCInterfaceDecl(ObjCInterfaceDecl *D);
    void VisitObjCIvarDecl(ObjCIvarDecl *D);
    void VisitObjCProtocolDecl(ObjCProtocolDecl *D);
    void VisitObjCAtDefsFieldDecl(ObjCAtDefsFieldDecl *D);
    void VisitObjCCategoryDecl(ObjCCategoryDecl *D);
    void VisitObjCImplDecl(ObjCImplDecl *D);
    void VisitObjCCategoryImplDecl(ObjCCategoryImplDecl *D);
    void VisitObjCImplementationDecl(ObjCImplementationDecl *D);
    void VisitObjCCompatibleAliasDecl(ObjCCompatibleAliasDecl *D);
    void VisitObjCPropertyDecl(ObjCPropertyDecl *D);
    void VisitObjCPropertyImplDecl(ObjCPropertyImplDecl *D);
    void VisitOMPThreadPrivateDecl(OMPThreadPrivateDecl *D);
    void VisitOMPRequiresDecl(OMPRequiresDecl *D);
    void VisitOMPDeclareReductionDecl(OMPDeclareReductionDecl *D);
    void VisitOMPCapturedExprDecl(OMPCapturedExprDecl *D);

    /// Add an Objective-C type parameter list to the given record.
    void AddObjCTypeParamList(ObjCTypeParamList *typeParams) {
      // Empty type parameter list.
      if (!typeParams) {
        Record.push_back(0);
        return;
      }

      Record.push_back(typeParams->size());
      for (auto typeParam : *typeParams) {
        Record.AddDeclRef(typeParam);
      }
      Record.AddSourceLocation(typeParams->getLAngleLoc());
      Record.AddSourceLocation(typeParams->getRAngleLoc());
    }

    /// Add to the record the first declaration from each module file that
    /// provides a declaration of D. The intent is to provide a sufficient
    /// set such that reloading this set will load all current redeclarations.
    void AddFirstDeclFromEachModule(const Decl *D, bool IncludeLocal) {
      llvm::MapVector<ModuleFile*, const Decl*> Firsts;
      // FIXME: We can skip entries that we know are implied by others.
      for (const Decl *R = D->getMostRecentDecl(); R; R = R->getPreviousDecl()) {
        if (R->isFromASTFile())
          Firsts[Writer.Chain->getOwningModuleFile(R)] = R;
        else if (IncludeLocal)
          Firsts[nullptr] = R;
      }
      for (const auto &F : Firsts)
        Record.AddDeclRef(F.second);
    }

    /// Get the specialization decl from an entry in the specialization list.
    template <typename EntryType>
    typename RedeclarableTemplateDecl::SpecEntryTraits<EntryType>::DeclType *
    getSpecializationDecl(EntryType &T) {
      return RedeclarableTemplateDecl::SpecEntryTraits<EntryType>::getDecl(&T);
    }

    /// Get the list of partial specializations from a template's common ptr.
    template<typename T>
    decltype(T::PartialSpecializations) &getPartialSpecializations(T *Common) {
      return Common->PartialSpecializations;
    }
    ArrayRef<Decl> getPartialSpecializations(FunctionTemplateDecl::Common *) {
      return None;
    }

    template<typename DeclTy>
    void AddTemplateSpecializations(DeclTy *D) {
      auto *Common = D->getCommonPtr();

      // If we have any lazy specializations, and the external AST source is
      // our chained AST reader, we can just write out the DeclIDs. Otherwise,
      // we need to resolve them to actual declarations.
      if (Writer.Chain != Writer.Context->getExternalSource() &&
          Common->LazySpecializations) {
        D->LoadLazySpecializations();
        assert(!Common->LazySpecializations);
      }

      ArrayRef<DeclID> LazySpecializations;
      if (auto *LS = Common->LazySpecializations)
        LazySpecializations = llvm::makeArrayRef(LS + 1, LS[0]);

      // Add a slot to the record for the number of specializations.
      unsigned I = Record.size();
      Record.push_back(0);

      // AddFirstDeclFromEachModule might trigger deserialization, invalidating
      // *Specializations iterators.
      llvm::SmallVector<const Decl*, 16> Specs;
      for (auto &Entry : Common->Specializations)
        Specs.push_back(getSpecializationDecl(Entry));
      for (auto &Entry : getPartialSpecializations(Common))
        Specs.push_back(getSpecializationDecl(Entry));

      for (auto *D : Specs) {
        assert(D->isCanonicalDecl() && "non-canonical decl in set");
        AddFirstDeclFromEachModule(D, /*IncludeLocal*/true);
      }
      Record.append(LazySpecializations.begin(), LazySpecializations.end());

      // Update the size entry we added earlier.
      Record[I] = Record.size() - I - 1;
    }

    /// Ensure that this template specialization is associated with the specified
    /// template on reload.
    void RegisterTemplateSpecialization(const Decl *Template,
                                        const Decl *Specialization) {
      Template = Template->getCanonicalDecl();

      // If the canonical template is local, we'll write out this specialization
      // when we emit it.
      // FIXME: We can do the same thing if there is any local declaration of
      // the template, to avoid emitting an update record.
      if (!Template->isFromASTFile())
        return;

      // We only need to associate the first local declaration of the
      // specialization. The other declarations will get pulled in by it.
      if (Writer.getFirstLocalDecl(Specialization) != Specialization)
        return;

      Writer.DeclUpdates[Template].push_back(ASTWriter::DeclUpdate(
          UPD_CXX_ADDED_TEMPLATE_SPECIALIZATION, Specialization));
    }
  };
}

void ASTDeclWriter::Visit(Decl *D) {
  DeclVisitor<ASTDeclWriter>::Visit(D);

  // Source locations require array (variable-length) abbreviations.  The
  // abbreviation infrastructure requires that arrays are encoded last, so
  // we handle it here in the case of those classes derived from DeclaratorDecl
  if (DeclaratorDecl *DD = dyn_cast<DeclaratorDecl>(D)) {
    if (auto *TInfo = DD->getTypeSourceInfo())
      Record.AddTypeLoc(TInfo->getTypeLoc());
  }

  // Handle FunctionDecl's body here and write it after all other Stmts/Exprs
  // have been written. We want it last because we will not read it back when
  // retrieving it from the AST, we'll just lazily set the offset.
  if (FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
    Record.push_back(FD->doesThisDeclarationHaveABody());
    if (FD->doesThisDeclarationHaveABody())
      Record.AddFunctionDefinition(FD);
  }

  // If this declaration is also a DeclContext, write blocks for the
  // declarations that lexically stored inside its context and those
  // declarations that are visible from its context.
  if (DeclContext *DC = dyn_cast<DeclContext>(D))
    VisitDeclContext(DC);
}

void ASTDeclWriter::VisitDecl(Decl *D) {
  Record.AddDeclRef(cast_or_null<Decl>(D->getDeclContext()));
  if (D->getDeclContext() != D->getLexicalDeclContext())
    Record.AddDeclRef(cast_or_null<Decl>(D->getLexicalDeclContext()));
  else
    Record.push_back(0);
  Record.push_back(D->isInvalidDecl());
  Record.push_back(D->hasAttrs());
  if (D->hasAttrs())
    Record.AddAttributes(D->getAttrs());
  Record.push_back(D->isImplicit());
  Record.push_back(D->isUsed(false));
  Record.push_back(D->isReferenced());
  Record.push_back(D->isTopLevelDeclInObjCContainer());
  Record.push_back(D->getAccess());
  Record.push_back(D->isModulePrivate());
  Record.push_back(Writer.getSubmoduleID(D->getOwningModule()));

  // If this declaration injected a name into a context different from its
  // lexical context, and that context is an imported namespace, we need to
  // update its visible declarations to include this name.
  //
  // This happens when we instantiate a class with a friend declaration or a
  // function with a local extern declaration, for instance.
  //
  // FIXME: Can we handle this in AddedVisibleDecl instead?
  if (D->isOutOfLine()) {
    auto *DC = D->getDeclContext();
    while (auto *NS = dyn_cast<NamespaceDecl>(DC->getRedeclContext())) {
      if (!NS->isFromASTFile())
        break;
      Writer.UpdatedDeclContexts.insert(NS->getPrimaryContext());
      if (!NS->isInlineNamespace())
        break;
      DC = NS->getParent();
    }
  }
}

void ASTDeclWriter::VisitPragmaCommentDecl(PragmaCommentDecl *D) {
  StringRef Arg = D->getArg();
  Record.push_back(Arg.size());
  VisitDecl(D);
  Record.AddSourceLocation(D->getBeginLoc());
  Record.push_back(D->getCommentKind());
  Record.AddString(Arg);
  Code = serialization::DECL_PRAGMA_COMMENT;
}

void ASTDeclWriter::VisitPragmaDetectMismatchDecl(
    PragmaDetectMismatchDecl *D) {
  StringRef Name = D->getName();
  StringRef Value = D->getValue();
  Record.push_back(Name.size() + 1 + Value.size());
  VisitDecl(D);
  Record.AddSourceLocation(D->getBeginLoc());
  Record.AddString(Name);
  Record.AddString(Value);
  Code = serialization::DECL_PRAGMA_DETECT_MISMATCH;
}

void ASTDeclWriter::VisitTranslationUnitDecl(TranslationUnitDecl *D) {
  llvm_unreachable("Translation units aren't directly serialized");
}

void ASTDeclWriter::VisitNamedDecl(NamedDecl *D) {
  VisitDecl(D);
  Record.AddDeclarationName(D->getDeclName());
  Record.push_back(needsAnonymousDeclarationNumber(D)
                       ? Writer.getAnonymousDeclarationNumber(D)
                       : 0);
}

void ASTDeclWriter::VisitTypeDecl(TypeDecl *D) {
  VisitNamedDecl(D);
  Record.AddSourceLocation(D->getBeginLoc());
  Record.AddTypeRef(QualType(D->getTypeForDecl(), 0));
}

void ASTDeclWriter::VisitTypedefNameDecl(TypedefNameDecl *D) {
  VisitRedeclarable(D);
  VisitTypeDecl(D);
  Record.AddTypeSourceInfo(D->getTypeSourceInfo());
  Record.push_back(D->isModed());
  if (D->isModed())
    Record.AddTypeRef(D->getUnderlyingType());
  Record.AddDeclRef(D->getAnonDeclWithTypedefName(false));
}

void ASTDeclWriter::VisitTypedefDecl(TypedefDecl *D) {
  VisitTypedefNameDecl(D);
  if (D->getDeclContext() == D->getLexicalDeclContext() &&
      !D->hasAttrs() &&
      !D->isImplicit() &&
      D->getFirstDecl() == D->getMostRecentDecl() &&
      !D->isInvalidDecl() &&
      !D->isTopLevelDeclInObjCContainer() &&
      !D->isModulePrivate() &&
      !needsAnonymousDeclarationNumber(D) &&
      D->getDeclName().getNameKind() == DeclarationName::Identifier)
    AbbrevToUse = Writer.getDeclTypedefAbbrev();

  Code = serialization::DECL_TYPEDEF;
}

void ASTDeclWriter::VisitTypeAliasDecl(TypeAliasDecl *D) {
  VisitTypedefNameDecl(D);
  Record.AddDeclRef(D->getDescribedAliasTemplate());
  Code = serialization::DECL_TYPEALIAS;
}

void ASTDeclWriter::VisitTagDecl(TagDecl *D) {
  VisitRedeclarable(D);
  VisitTypeDecl(D);
  Record.push_back(D->getIdentifierNamespace());
  Record.push_back((unsigned)D->getTagKind()); // FIXME: stable encoding
  if (!isa<CXXRecordDecl>(D))
    Record.push_back(D->isCompleteDefinition());
  Record.push_back(D->isEmbeddedInDeclarator());
  Record.push_back(D->isFreeStanding());
  Record.push_back(D->isCompleteDefinitionRequired());
  Record.AddSourceRange(D->getBraceRange());

  if (D->hasExtInfo()) {
    Record.push_back(1);
    Record.AddQualifierInfo(*D->getExtInfo());
  } else if (auto *TD = D->getTypedefNameForAnonDecl()) {
    Record.push_back(2);
    Record.AddDeclRef(TD);
    Record.AddIdentifierRef(TD->getDeclName().getAsIdentifierInfo());
  } else {
    Record.push_back(0);
  }
}

void ASTDeclWriter::VisitEnumDecl(EnumDecl *D) {
  VisitTagDecl(D);
  Record.AddTypeSourceInfo(D->getIntegerTypeSourceInfo());
  if (!D->getIntegerTypeSourceInfo())
    Record.AddTypeRef(D->getIntegerType());
  Record.AddTypeRef(D->getPromotionType());
  Record.push_back(D->getNumPositiveBits());
  Record.push_back(D->getNumNegativeBits());
  Record.push_back(D->isScoped());
  Record.push_back(D->isScopedUsingClassTag());
  Record.push_back(D->isFixed());
  Record.push_back(D->getODRHash());

  if (MemberSpecializationInfo *MemberInfo = D->getMemberSpecializationInfo()) {
    Record.AddDeclRef(MemberInfo->getInstantiatedFrom());
    Record.push_back(MemberInfo->getTemplateSpecializationKind());
    Record.AddSourceLocation(MemberInfo->getPointOfInstantiation());
  } else {
    Record.AddDeclRef(nullptr);
  }

  if (D->getDeclContext() == D->getLexicalDeclContext() &&
      !D->hasAttrs() &&
      !D->isImplicit() &&
      !D->isUsed(false) &&
      !D->hasExtInfo() &&
      !D->getTypedefNameForAnonDecl() &&
      D->getFirstDecl() == D->getMostRecentDecl() &&
      !D->isInvalidDecl() &&
      !D->isReferenced() &&
      !D->isTopLevelDeclInObjCContainer() &&
      D->getAccess() == AS_none &&
      !D->isModulePrivate() &&
      !CXXRecordDecl::classofKind(D->getKind()) &&
      !D->getIntegerTypeSourceInfo() &&
      !D->getMemberSpecializationInfo() &&
      !needsAnonymousDeclarationNumber(D) &&
      D->getDeclName().getNameKind() == DeclarationName::Identifier)
    AbbrevToUse = Writer.getDeclEnumAbbrev();

  Code = serialization::DECL_ENUM;
}

void ASTDeclWriter::VisitRecordDecl(RecordDecl *D) {
  VisitTagDecl(D);
  Record.push_back(D->hasFlexibleArrayMember());
  Record.push_back(D->isAnonymousStructOrUnion());
  Record.push_back(D->hasObjectMember());
  Record.push_back(D->hasVolatileMember());
  Record.push_back(D->isNonTrivialToPrimitiveDefaultInitialize());
  Record.push_back(D->isNonTrivialToPrimitiveCopy());
  Record.push_back(D->isNonTrivialToPrimitiveDestroy());
  Record.push_back(D->isParamDestroyedInCallee());
  Record.push_back(D->getArgPassingRestrictions());

  if (D->getDeclContext() == D->getLexicalDeclContext() &&
      !D->hasAttrs() &&
      !D->isImplicit() &&
      !D->isUsed(false) &&
      !D->hasExtInfo() &&
      !D->getTypedefNameForAnonDecl() &&
      D->getFirstDecl() == D->getMostRecentDecl() &&
      !D->isInvalidDecl() &&
      !D->isReferenced() &&
      !D->isTopLevelDeclInObjCContainer() &&
      D->getAccess() == AS_none &&
      !D->isModulePrivate() &&
      !CXXRecordDecl::classofKind(D->getKind()) &&
      !needsAnonymousDeclarationNumber(D) &&
      D->getDeclName().getNameKind() == DeclarationName::Identifier)
    AbbrevToUse = Writer.getDeclRecordAbbrev();

  Code = serialization::DECL_RECORD;
}

void ASTDeclWriter::VisitValueDecl(ValueDecl *D) {
  VisitNamedDecl(D);
  Record.AddTypeRef(D->getType());
}

void ASTDeclWriter::VisitEnumConstantDecl(EnumConstantDecl *D) {
  VisitValueDecl(D);
  Record.push_back(D->getInitExpr()? 1 : 0);
  if (D->getInitExpr())
    Record.AddStmt(D->getInitExpr());
  Record.AddAPSInt(D->getInitVal());

  Code = serialization::DECL_ENUM_CONSTANT;
}

void ASTDeclWriter::VisitDeclaratorDecl(DeclaratorDecl *D) {
  VisitValueDecl(D);
  Record.AddSourceLocation(D->getInnerLocStart());
  Record.push_back(D->hasExtInfo());
  if (D->hasExtInfo())
    Record.AddQualifierInfo(*D->getExtInfo());
  // The location information is deferred until the end of the record.
  Record.AddTypeRef(D->getTypeSourceInfo() ? D->getTypeSourceInfo()->getType()
                                           : QualType());
}

void ASTDeclWriter::VisitFunctionDecl(FunctionDecl *D) {
  VisitRedeclarable(D);
  VisitDeclaratorDecl(D);
  Record.AddDeclarationNameLoc(D->DNLoc, D->getDeclName());
  Record.push_back(D->getIdentifierNamespace());

  // FunctionDecl's body is handled last at ASTWriterDecl::Visit,
  // after everything else is written.
  Record.push_back(static_cast<int>(D->getStorageClass())); // FIXME: stable encoding
  Record.push_back(D->isInlineSpecified());
  Record.push_back(D->isInlined());
  Record.push_back(D->isExplicitSpecified());
  Record.push_back(D->isVirtualAsWritten());
  Record.push_back(D->isPure());
  Record.push_back(D->hasInheritedPrototype());
  Record.push_back(D->hasWrittenPrototype());
  Record.push_back(D->isDeletedBit());
  Record.push_back(D->isTrivial());
  Record.push_back(D->isTrivialForCall());
  Record.push_back(D->isDefaulted());
  Record.push_back(D->isExplicitlyDefaulted());
  Record.push_back(D->hasImplicitReturnZero());
  Record.push_back(D->isConstexpr());
  Record.push_back(D->usesSEHTry());
  Record.push_back(D->hasSkippedBody());
  Record.push_back(D->isMultiVersion());
  Record.push_back(D->isLateTemplateParsed());
  Record.push_back(D->getLinkageInternal());
  Record.AddSourceLocation(D->getEndLoc());

  Record.push_back(D->getODRHash());

  Record.push_back(D->getTemplatedKind());
  switch (D->getTemplatedKind()) {
  case FunctionDecl::TK_NonTemplate:
    break;
  case FunctionDecl::TK_FunctionTemplate:
    Record.AddDeclRef(D->getDescribedFunctionTemplate());
    break;
  case FunctionDecl::TK_MemberSpecialization: {
    MemberSpecializationInfo *MemberInfo = D->getMemberSpecializationInfo();
    Record.AddDeclRef(MemberInfo->getInstantiatedFrom());
    Record.push_back(MemberInfo->getTemplateSpecializationKind());
    Record.AddSourceLocation(MemberInfo->getPointOfInstantiation());
    break;
  }
  case FunctionDecl::TK_FunctionTemplateSpecialization: {
    FunctionTemplateSpecializationInfo *
      FTSInfo = D->getTemplateSpecializationInfo();

    RegisterTemplateSpecialization(FTSInfo->getTemplate(), D);

    Record.AddDeclRef(FTSInfo->getTemplate());
    Record.push_back(FTSInfo->getTemplateSpecializationKind());

    // Template arguments.
    Record.AddTemplateArgumentList(FTSInfo->TemplateArguments);

    // Template args as written.
    Record.push_back(FTSInfo->TemplateArgumentsAsWritten != nullptr);
    if (FTSInfo->TemplateArgumentsAsWritten) {
      Record.push_back(FTSInfo->TemplateArgumentsAsWritten->NumTemplateArgs);
      for (int i=0, e = FTSInfo->TemplateArgumentsAsWritten->NumTemplateArgs;
             i!=e; ++i)
        Record.AddTemplateArgumentLoc(
            (*FTSInfo->TemplateArgumentsAsWritten)[i]);
      Record.AddSourceLocation(FTSInfo->TemplateArgumentsAsWritten->LAngleLoc);
      Record.AddSourceLocation(FTSInfo->TemplateArgumentsAsWritten->RAngleLoc);
    }

    Record.AddSourceLocation(FTSInfo->getPointOfInstantiation());

    if (D->isCanonicalDecl()) {
      // Write the template that contains the specializations set. We will
      // add a FunctionTemplateSpecializationInfo to it when reading.
      Record.AddDeclRef(FTSInfo->getTemplate()->getCanonicalDecl());
    }
    break;
  }
  case FunctionDecl::TK_DependentFunctionTemplateSpecialization: {
    DependentFunctionTemplateSpecializationInfo *
      DFTSInfo = D->getDependentSpecializationInfo();

    // Templates.
    Record.push_back(DFTSInfo->getNumTemplates());
    for (int i=0, e = DFTSInfo->getNumTemplates(); i != e; ++i)
      Record.AddDeclRef(DFTSInfo->getTemplate(i));

    // Templates args.
    Record.push_back(DFTSInfo->getNumTemplateArgs());
    for (int i=0, e = DFTSInfo->getNumTemplateArgs(); i != e; ++i)
      Record.AddTemplateArgumentLoc(DFTSInfo->getTemplateArg(i));
    Record.AddSourceLocation(DFTSInfo->getLAngleLoc());
    Record.AddSourceLocation(DFTSInfo->getRAngleLoc());
    break;
  }
  }

  Record.push_back(D->param_size());
  for (auto P : D->parameters())
    Record.AddDeclRef(P);
  Code = serialization::DECL_FUNCTION;
}

void ASTDeclWriter::VisitCXXDeductionGuideDecl(CXXDeductionGuideDecl *D) {
  VisitFunctionDecl(D);
  Record.push_back(D->isCopyDeductionCandidate());
  Code = serialization::DECL_CXX_DEDUCTION_GUIDE;
}

void ASTDeclWriter::VisitObjCMethodDecl(ObjCMethodDecl *D) {
  VisitNamedDecl(D);
  // FIXME: convert to LazyStmtPtr?
  // Unlike C/C++, method bodies will never be in header files.
  bool HasBodyStuff = D->getBody() != nullptr     ||
                      D->getSelfDecl() != nullptr || D->getCmdDecl() != nullptr;
  Record.push_back(HasBodyStuff);
  if (HasBodyStuff) {
    Record.AddStmt(D->getBody());
    Record.AddDeclRef(D->getSelfDecl());
    Record.AddDeclRef(D->getCmdDecl());
  }
  Record.push_back(D->isInstanceMethod());
  Record.push_back(D->isVariadic());
  Record.push_back(D->isPropertyAccessor());
  Record.push_back(D->isDefined());
  Record.push_back(D->isOverriding());
  Record.push_back(D->hasSkippedBody());

  Record.push_back(D->isRedeclaration());
  Record.push_back(D->hasRedeclaration());
  if (D->hasRedeclaration()) {
    assert(Context.getObjCMethodRedeclaration(D));
    Record.AddDeclRef(Context.getObjCMethodRedeclaration(D));
  }

  // FIXME: stable encoding for @required/@optional
  Record.push_back(D->getImplementationControl());
  // FIXME: stable encoding for in/out/inout/bycopy/byref/oneway/nullability
  Record.push_back(D->getObjCDeclQualifier());
  Record.push_back(D->hasRelatedResultType());
  Record.AddTypeRef(D->getReturnType());
  Record.AddTypeSourceInfo(D->getReturnTypeSourceInfo());
  Record.AddSourceLocation(D->getEndLoc());
  Record.push_back(D->param_size());
  for (const auto *P : D->parameters())
    Record.AddDeclRef(P);

  Record.push_back(D->getSelLocsKind());
  unsigned NumStoredSelLocs = D->getNumStoredSelLocs();
  SourceLocation *SelLocs = D->getStoredSelLocs();
  Record.push_back(NumStoredSelLocs);
  for (unsigned i = 0; i != NumStoredSelLocs; ++i)
    Record.AddSourceLocation(SelLocs[i]);

  Code = serialization::DECL_OBJC_METHOD;
}

void ASTDeclWriter::VisitObjCTypeParamDecl(ObjCTypeParamDecl *D) {
  VisitTypedefNameDecl(D);
  Record.push_back(D->Variance);
  Record.push_back(D->Index);
  Record.AddSourceLocation(D->VarianceLoc);
  Record.AddSourceLocation(D->ColonLoc);

  Code = serialization::DECL_OBJC_TYPE_PARAM;
}

void ASTDeclWriter::VisitObjCContainerDecl(ObjCContainerDecl *D) {
  VisitNamedDecl(D);
  Record.AddSourceLocation(D->getAtStartLoc());
  Record.AddSourceRange(D->getAtEndRange());
  // Abstract class (no need to define a stable serialization::DECL code).
}

void ASTDeclWriter::VisitObjCInterfaceDecl(ObjCInterfaceDecl *D) {
  VisitRedeclarable(D);
  VisitObjCContainerDecl(D);
  Record.AddTypeRef(QualType(D->getTypeForDecl(), 0));
  AddObjCTypeParamList(D->TypeParamList);

  Record.push_back(D->isThisDeclarationADefinition());
  if (D->isThisDeclarationADefinition()) {
    // Write the DefinitionData
    ObjCInterfaceDecl::DefinitionData &Data = D->data();

    Record.AddTypeSourceInfo(D->getSuperClassTInfo());
    Record.AddSourceLocation(D->getEndOfDefinitionLoc());
    Record.push_back(Data.HasDesignatedInitializers);

    // Write out the protocols that are directly referenced by the @interface.
    Record.push_back(Data.ReferencedProtocols.size());
    for (const auto *P : D->protocols())
      Record.AddDeclRef(P);
    for (const auto &PL : D->protocol_locs())
      Record.AddSourceLocation(PL);

    // Write out the protocols that are transitively referenced.
    Record.push_back(Data.AllReferencedProtocols.size());
    for (ObjCList<ObjCProtocolDecl>::iterator
              P = Data.AllReferencedProtocols.begin(),
           PEnd = Data.AllReferencedProtocols.end();
         P != PEnd; ++P)
      Record.AddDeclRef(*P);


    if (ObjCCategoryDecl *Cat = D->getCategoryListRaw()) {
      // Ensure that we write out the set of categories for this class.
      Writer.ObjCClassesWithCategories.insert(D);

      // Make sure that the categories get serialized.
      for (; Cat; Cat = Cat->getNextClassCategoryRaw())
        (void)Writer.GetDeclRef(Cat);
    }
  }

  Code = serialization::DECL_OBJC_INTERFACE;
}

void ASTDeclWriter::VisitObjCIvarDecl(ObjCIvarDecl *D) {
  VisitFieldDecl(D);
  // FIXME: stable encoding for @public/@private/@protected/@package
  Record.push_back(D->getAccessControl());
  Record.push_back(D->getSynthesize());

  if (D->getDeclContext() == D->getLexicalDeclContext() &&
      !D->hasAttrs() &&
      !D->isImplicit() &&
      !D->isUsed(false) &&
      !D->isInvalidDecl() &&
      !D->isReferenced() &&
      !D->isModulePrivate() &&
      !D->getBitWidth() &&
      !D->hasExtInfo() &&
      D->getDeclName())
    AbbrevToUse = Writer.getDeclObjCIvarAbbrev();

  Code = serialization::DECL_OBJC_IVAR;
}

void ASTDeclWriter::VisitObjCProtocolDecl(ObjCProtocolDecl *D) {
  VisitRedeclarable(D);
  VisitObjCContainerDecl(D);

  Record.push_back(D->isThisDeclarationADefinition());
  if (D->isThisDeclarationADefinition()) {
    Record.push_back(D->protocol_size());
    for (const auto *I : D->protocols())
      Record.AddDeclRef(I);
    for (const auto &PL : D->protocol_locs())
      Record.AddSourceLocation(PL);
  }

  Code = serialization::DECL_OBJC_PROTOCOL;
}

void ASTDeclWriter::VisitObjCAtDefsFieldDecl(ObjCAtDefsFieldDecl *D) {
  VisitFieldDecl(D);
  Code = serialization::DECL_OBJC_AT_DEFS_FIELD;
}

void ASTDeclWriter::VisitObjCCategoryDecl(ObjCCategoryDecl *D) {
  VisitObjCContainerDecl(D);
  Record.AddSourceLocation(D->getCategoryNameLoc());
  Record.AddSourceLocation(D->getIvarLBraceLoc());
  Record.AddSourceLocation(D->getIvarRBraceLoc());
  Record.AddDeclRef(D->getClassInterface());
  AddObjCTypeParamList(D->TypeParamList);
  Record.push_back(D->protocol_size());
  for (const auto *I : D->protocols())
    Record.AddDeclRef(I);
  for (const auto &PL : D->protocol_locs())
    Record.AddSourceLocation(PL);
  Code = serialization::DECL_OBJC_CATEGORY;
}

void ASTDeclWriter::VisitObjCCompatibleAliasDecl(ObjCCompatibleAliasDecl *D) {
  VisitNamedDecl(D);
  Record.AddDeclRef(D->getClassInterface());
  Code = serialization::DECL_OBJC_COMPATIBLE_ALIAS;
}

void ASTDeclWriter::VisitObjCPropertyDecl(ObjCPropertyDecl *D) {
  VisitNamedDecl(D);
  Record.AddSourceLocation(D->getAtLoc());
  Record.AddSourceLocation(D->getLParenLoc());
  Record.AddTypeRef(D->getType());
  Record.AddTypeSourceInfo(D->getTypeSourceInfo());
  // FIXME: stable encoding
  Record.push_back((unsigned)D->getPropertyAttributes());
  Record.push_back((unsigned)D->getPropertyAttributesAsWritten());
  // FIXME: stable encoding
  Record.push_back((unsigned)D->getPropertyImplementation());
  Record.AddDeclarationName(D->getGetterName());
  Record.AddSourceLocation(D->getGetterNameLoc());
  Record.AddDeclarationName(D->getSetterName());
  Record.AddSourceLocation(D->getSetterNameLoc());
  Record.AddDeclRef(D->getGetterMethodDecl());
  Record.AddDeclRef(D->getSetterMethodDecl());
  Record.AddDeclRef(D->getPropertyIvarDecl());
  Code = serialization::DECL_OBJC_PROPERTY;
}

void ASTDeclWriter::VisitObjCImplDecl(ObjCImplDecl *D) {
  VisitObjCContainerDecl(D);
  Record.AddDeclRef(D->getClassInterface());
  // Abstract class (no need to define a stable serialization::DECL code).
}

void ASTDeclWriter::VisitObjCCategoryImplDecl(ObjCCategoryImplDecl *D) {
  VisitObjCImplDecl(D);
  Record.AddSourceLocation(D->getCategoryNameLoc());
  Code = serialization::DECL_OBJC_CATEGORY_IMPL;
}

void ASTDeclWriter::VisitObjCImplementationDecl(ObjCImplementationDecl *D) {
  VisitObjCImplDecl(D);
  Record.AddDeclRef(D->getSuperClass());
  Record.AddSourceLocation(D->getSuperClassLoc());
  Record.AddSourceLocation(D->getIvarLBraceLoc());
  Record.AddSourceLocation(D->getIvarRBraceLoc());
  Record.push_back(D->hasNonZeroConstructors());
  Record.push_back(D->hasDestructors());
  Record.push_back(D->NumIvarInitializers);
  if (D->NumIvarInitializers)
    Record.AddCXXCtorInitializers(
        llvm::makeArrayRef(D->init_begin(), D->init_end()));
  Code = serialization::DECL_OBJC_IMPLEMENTATION;
}

void ASTDeclWriter::VisitObjCPropertyImplDecl(ObjCPropertyImplDecl *D) {
  VisitDecl(D);
  Record.AddSourceLocation(D->getBeginLoc());
  Record.AddDeclRef(D->getPropertyDecl());
  Record.AddDeclRef(D->getPropertyIvarDecl());
  Record.AddSourceLocation(D->getPropertyIvarDeclLoc());
  Record.AddStmt(D->getGetterCXXConstructor());
  Record.AddStmt(D->getSetterCXXAssignment());
  Code = serialization::DECL_OBJC_PROPERTY_IMPL;
}

void ASTDeclWriter::VisitFieldDecl(FieldDecl *D) {
  VisitDeclaratorDecl(D);
  Record.push_back(D->isMutable());

  FieldDecl::InitStorageKind ISK = D->InitStorage.getInt();
  Record.push_back(ISK);
  if (ISK == FieldDecl::ISK_CapturedVLAType)
    Record.AddTypeRef(QualType(D->getCapturedVLAType(), 0));
  else if (ISK)
    Record.AddStmt(D->getInClassInitializer());

  Record.AddStmt(D->getBitWidth());

  if (!D->getDeclName())
    Record.AddDeclRef(Context.getInstantiatedFromUnnamedFieldDecl(D));

  if (D->getDeclContext() == D->getLexicalDeclContext() &&
      !D->hasAttrs() &&
      !D->isImplicit() &&
      !D->isUsed(false) &&
      !D->isInvalidDecl() &&
      !D->isReferenced() &&
      !D->isTopLevelDeclInObjCContainer() &&
      !D->isModulePrivate() &&
      !D->getBitWidth() &&
      !D->hasInClassInitializer() &&
      !D->hasCapturedVLAType() &&
      !D->hasExtInfo() &&
      !ObjCIvarDecl::classofKind(D->getKind()) &&
      !ObjCAtDefsFieldDecl::classofKind(D->getKind()) &&
      D->getDeclName())
    AbbrevToUse = Writer.getDeclFieldAbbrev();

  Code = serialization::DECL_FIELD;
}

void ASTDeclWriter::VisitMSPropertyDecl(MSPropertyDecl *D) {
  VisitDeclaratorDecl(D);
  Record.AddIdentifierRef(D->getGetterId());
  Record.AddIdentifierRef(D->getSetterId());
  Code = serialization::DECL_MS_PROPERTY;
}

void ASTDeclWriter::VisitIndirectFieldDecl(IndirectFieldDecl *D) {
  VisitValueDecl(D);
  Record.push_back(D->getChainingSize());

  for (const auto *P : D->chain())
    Record.AddDeclRef(P);
  Code = serialization::DECL_INDIRECTFIELD;
}

void ASTDeclWriter::VisitVarDecl(VarDecl *D) {
  VisitRedeclarable(D);
  VisitDeclaratorDecl(D);
  Record.push_back(D->getStorageClass());
  Record.push_back(D->getTSCSpec());
  Record.push_back(D->getInitStyle());
  Record.push_back(D->isARCPseudoStrong());
  if (!isa<ParmVarDecl>(D)) {
    Record.push_back(D->isThisDeclarationADemotedDefinition());
    Record.push_back(D->isExceptionVariable());
    Record.push_back(D->isNRVOVariable());
    Record.push_back(D->isCXXForRangeDecl());
    Record.push_back(D->isObjCForDecl());
    Record.push_back(D->isInline());
    Record.push_back(D->isInlineSpecified());
    Record.push_back(D->isConstexpr());
    Record.push_back(D->isInitCapture());
    Record.push_back(D->isPreviousDeclInSameBlockScope());
    if (const auto *IPD = dyn_cast<ImplicitParamDecl>(D))
      Record.push_back(static_cast<unsigned>(IPD->getParameterKind()));
    else
      Record.push_back(0);
    Record.push_back(D->isEscapingByref());
  }
  Record.push_back(D->getLinkageInternal());

  if (D->getInit()) {
    Record.push_back(!D->isInitKnownICE() ? 1 : (D->isInitICE() ? 3 : 2));
    Record.AddStmt(D->getInit());
  } else {
    Record.push_back(0);
  }

  if (D->hasAttr<BlocksAttr>() && D->getType()->getAsCXXRecordDecl()) {
    ASTContext::BlockVarCopyInit Init = Writer.Context->getBlockVarCopyInit(D);
    Record.AddStmt(Init.getCopyExpr());
    if (Init.getCopyExpr())
      Record.push_back(Init.canThrow());
  }

  if (D->getStorageDuration() == SD_Static) {
    bool ModulesCodegen = false;
    if (Writer.WritingModule &&
        !D->getDescribedVarTemplate() && !D->getMemberSpecializationInfo() &&
        !isa<VarTemplateSpecializationDecl>(D)) {
      // When building a C++ Modules TS module interface unit, a strong
      // definition in the module interface is provided by the compilation of
      // that module interface unit, not by its users. (Inline variables are
      // still emitted in module users.)
      ModulesCodegen =
          (Writer.WritingModule->Kind == Module::ModuleInterfaceUnit &&
           Writer.Context->GetGVALinkageForVariable(D) == GVA_StrongExternal);
    }
    Record.push_back(ModulesCodegen);
    if (ModulesCodegen)
      Writer.ModularCodegenDecls.push_back(Writer.GetDeclRef(D));
  }

  enum {
    VarNotTemplate = 0, VarTemplate, StaticDataMemberSpecialization
  };
  if (VarTemplateDecl *TemplD = D->getDescribedVarTemplate()) {
    Record.push_back(VarTemplate);
    Record.AddDeclRef(TemplD);
  } else if (MemberSpecializationInfo *SpecInfo
               = D->getMemberSpecializationInfo()) {
    Record.push_back(StaticDataMemberSpecialization);
    Record.AddDeclRef(SpecInfo->getInstantiatedFrom());
    Record.push_back(SpecInfo->getTemplateSpecializationKind());
    Record.AddSourceLocation(SpecInfo->getPointOfInstantiation());
  } else {
    Record.push_back(VarNotTemplate);
  }

  if (D->getDeclContext() == D->getLexicalDeclContext() &&
      !D->hasAttrs() &&
      !D->isImplicit() &&
      !D->isUsed(false) &&
      !D->isInvalidDecl() &&
      !D->isReferenced() &&
      !D->isTopLevelDeclInObjCContainer() &&
      D->getAccess() == AS_none &&
      !D->isModulePrivate() &&
      !needsAnonymousDeclarationNumber(D) &&
      D->getDeclName().getNameKind() == DeclarationName::Identifier &&
      !D->hasExtInfo() &&
      D->getFirstDecl() == D->getMostRecentDecl() &&
      D->getKind() == Decl::Var &&
      !D->isInline() &&
      !D->isConstexpr() &&
      !D->isInitCapture() &&
      !D->isPreviousDeclInSameBlockScope() &&
      !(D->hasAttr<BlocksAttr>() && D->getType()->getAsCXXRecordDecl()) &&
      !D->isEscapingByref() &&
      D->getStorageDuration() != SD_Static &&
      !D->getMemberSpecializationInfo())
    AbbrevToUse = Writer.getDeclVarAbbrev();

  Code = serialization::DECL_VAR;
}

void ASTDeclWriter::VisitImplicitParamDecl(ImplicitParamDecl *D) {
  VisitVarDecl(D);
  Code = serialization::DECL_IMPLICIT_PARAM;
}

void ASTDeclWriter::VisitParmVarDecl(ParmVarDecl *D) {
  VisitVarDecl(D);
  Record.push_back(D->isObjCMethodParameter());
  Record.push_back(D->getFunctionScopeDepth());
  Record.push_back(D->getFunctionScopeIndex());
  Record.push_back(D->getObjCDeclQualifier()); // FIXME: stable encoding
  Record.push_back(D->isKNRPromoted());
  Record.push_back(D->hasInheritedDefaultArg());
  Record.push_back(D->hasUninstantiatedDefaultArg());
  if (D->hasUninstantiatedDefaultArg())
    Record.AddStmt(D->getUninstantiatedDefaultArg());
  Code = serialization::DECL_PARM_VAR;

  assert(!D->isARCPseudoStrong()); // can be true of ImplicitParamDecl

  // If the assumptions about the DECL_PARM_VAR abbrev are true, use it.  Here
  // we dynamically check for the properties that we optimize for, but don't
  // know are true of all PARM_VAR_DECLs.
  if (D->getDeclContext() == D->getLexicalDeclContext() &&
      !D->hasAttrs() &&
      !D->hasExtInfo() &&
      !D->isImplicit() &&
      !D->isUsed(false) &&
      !D->isInvalidDecl() &&
      !D->isReferenced() &&
      D->getAccess() == AS_none &&
      !D->isModulePrivate() &&
      D->getStorageClass() == 0 &&
      D->getInitStyle() == VarDecl::CInit && // Can params have anything else?
      D->getFunctionScopeDepth() == 0 &&
      D->getObjCDeclQualifier() == 0 &&
      !D->isKNRPromoted() &&
      !D->hasInheritedDefaultArg() &&
      D->getInit() == nullptr &&
      !D->hasUninstantiatedDefaultArg())  // No default expr.
    AbbrevToUse = Writer.getDeclParmVarAbbrev();

  // Check things we know are true of *every* PARM_VAR_DECL, which is more than
  // just us assuming it.
  assert(!D->getTSCSpec() && "PARM_VAR_DECL can't use TLS");
  assert(!D->isThisDeclarationADemotedDefinition()
         && "PARM_VAR_DECL can't be demoted definition.");
  assert(D->getAccess() == AS_none && "PARM_VAR_DECL can't be public/private");
  assert(!D->isExceptionVariable() && "PARM_VAR_DECL can't be exception var");
  assert(D->getPreviousDecl() == nullptr && "PARM_VAR_DECL can't be redecl");
  assert(!D->isStaticDataMember() &&
         "PARM_VAR_DECL can't be static data member");
}

void ASTDeclWriter::VisitDecompositionDecl(DecompositionDecl *D) {
  // Record the number of bindings first to simplify deserialization.
  Record.push_back(D->bindings().size());

  VisitVarDecl(D);
  for (auto *B : D->bindings())
    Record.AddDeclRef(B);
  Code = serialization::DECL_DECOMPOSITION;
}

void ASTDeclWriter::VisitBindingDecl(BindingDecl *D) {
  VisitValueDecl(D);
  Record.AddStmt(D->getBinding());
  Code = serialization::DECL_BINDING;
}

void ASTDeclWriter::VisitFileScopeAsmDecl(FileScopeAsmDecl *D) {
  VisitDecl(D);
  Record.AddStmt(D->getAsmString());
  Record.AddSourceLocation(D->getRParenLoc());
  Code = serialization::DECL_FILE_SCOPE_ASM;
}

void ASTDeclWriter::VisitEmptyDecl(EmptyDecl *D) {
  VisitDecl(D);
  Code = serialization::DECL_EMPTY;
}

void ASTDeclWriter::VisitBlockDecl(BlockDecl *D) {
  VisitDecl(D);
  Record.AddStmt(D->getBody());
  Record.AddTypeSourceInfo(D->getSignatureAsWritten());
  Record.push_back(D->param_size());
  for (ParmVarDecl *P : D->parameters())
    Record.AddDeclRef(P);
  Record.push_back(D->isVariadic());
  Record.push_back(D->blockMissingReturnType());
  Record.push_back(D->isConversionFromLambda());
  Record.push_back(D->doesNotEscape());
  Record.push_back(D->capturesCXXThis());
  Record.push_back(D->getNumCaptures());
  for (const auto &capture : D->captures()) {
    Record.AddDeclRef(capture.getVariable());

    unsigned flags = 0;
    if (capture.isByRef()) flags |= 1;
    if (capture.isNested()) flags |= 2;
    if (capture.hasCopyExpr()) flags |= 4;
    Record.push_back(flags);

    if (capture.hasCopyExpr()) Record.AddStmt(capture.getCopyExpr());
  }

  Code = serialization::DECL_BLOCK;
}

void ASTDeclWriter::VisitCapturedDecl(CapturedDecl *CD) {
  Record.push_back(CD->getNumParams());
  VisitDecl(CD);
  Record.push_back(CD->getContextParamPosition());
  Record.push_back(CD->isNothrow() ? 1 : 0);
  // Body is stored by VisitCapturedStmt.
  for (unsigned I = 0; I < CD->getNumParams(); ++I)
    Record.AddDeclRef(CD->getParam(I));
  Code = serialization::DECL_CAPTURED;
}

void ASTDeclWriter::VisitLinkageSpecDecl(LinkageSpecDecl *D) {
  VisitDecl(D);
  Record.push_back(D->getLanguage());
  Record.AddSourceLocation(D->getExternLoc());
  Record.AddSourceLocation(D->getRBraceLoc());
  Code = serialization::DECL_LINKAGE_SPEC;
}

void ASTDeclWriter::VisitExportDecl(ExportDecl *D) {
  VisitDecl(D);
  Record.AddSourceLocation(D->getRBraceLoc());
  Code = serialization::DECL_EXPORT;
}

void ASTDeclWriter::VisitLabelDecl(LabelDecl *D) {
  VisitNamedDecl(D);
  Record.AddSourceLocation(D->getBeginLoc());
  Code = serialization::DECL_LABEL;
}


void ASTDeclWriter::VisitNamespaceDecl(NamespaceDecl *D) {
  VisitRedeclarable(D);
  VisitNamedDecl(D);
  Record.push_back(D->isInline());
  Record.AddSourceLocation(D->getBeginLoc());
  Record.AddSourceLocation(D->getRBraceLoc());

  if (D->isOriginalNamespace())
    Record.AddDeclRef(D->getAnonymousNamespace());
  Code = serialization::DECL_NAMESPACE;

  if (Writer.hasChain() && D->isAnonymousNamespace() &&
      D == D->getMostRecentDecl()) {
    // This is a most recent reopening of the anonymous namespace. If its parent
    // is in a previous PCH (or is the TU), mark that parent for update, because
    // the original namespace always points to the latest re-opening of its
    // anonymous namespace.
    Decl *Parent = cast<Decl>(
        D->getParent()->getRedeclContext()->getPrimaryContext());
    if (Parent->isFromASTFile() || isa<TranslationUnitDecl>(Parent)) {
      Writer.DeclUpdates[Parent].push_back(
          ASTWriter::DeclUpdate(UPD_CXX_ADDED_ANONYMOUS_NAMESPACE, D));
    }
  }
}

void ASTDeclWriter::VisitNamespaceAliasDecl(NamespaceAliasDecl *D) {
  VisitRedeclarable(D);
  VisitNamedDecl(D);
  Record.AddSourceLocation(D->getNamespaceLoc());
  Record.AddSourceLocation(D->getTargetNameLoc());
  Record.AddNestedNameSpecifierLoc(D->getQualifierLoc());
  Record.AddDeclRef(D->getNamespace());
  Code = serialization::DECL_NAMESPACE_ALIAS;
}

void ASTDeclWriter::VisitUsingDecl(UsingDecl *D) {
  VisitNamedDecl(D);
  Record.AddSourceLocation(D->getUsingLoc());
  Record.AddNestedNameSpecifierLoc(D->getQualifierLoc());
  Record.AddDeclarationNameLoc(D->DNLoc, D->getDeclName());
  Record.AddDeclRef(D->FirstUsingShadow.getPointer());
  Record.push_back(D->hasTypename());
  Record.AddDeclRef(Context.getInstantiatedFromUsingDecl(D));
  Code = serialization::DECL_USING;
}

void ASTDeclWriter::VisitUsingPackDecl(UsingPackDecl *D) {
  Record.push_back(D->NumExpansions);
  VisitNamedDecl(D);
  Record.AddDeclRef(D->getInstantiatedFromUsingDecl());
  for (auto *E : D->expansions())
    Record.AddDeclRef(E);
  Code = serialization::DECL_USING_PACK;
}

void ASTDeclWriter::VisitUsingShadowDecl(UsingShadowDecl *D) {
  VisitRedeclarable(D);
  VisitNamedDecl(D);
  Record.AddDeclRef(D->getTargetDecl());
  Record.push_back(D->getIdentifierNamespace());
  Record.AddDeclRef(D->UsingOrNextShadow);
  Record.AddDeclRef(Context.getInstantiatedFromUsingShadowDecl(D));
  Code = serialization::DECL_USING_SHADOW;
}

void ASTDeclWriter::VisitConstructorUsingShadowDecl(
    ConstructorUsingShadowDecl *D) {
  VisitUsingShadowDecl(D);
  Record.AddDeclRef(D->NominatedBaseClassShadowDecl);
  Record.AddDeclRef(D->ConstructedBaseClassShadowDecl);
  Record.push_back(D->IsVirtual);
  Code = serialization::DECL_CONSTRUCTOR_USING_SHADOW;
}

void ASTDeclWriter::VisitUsingDirectiveDecl(UsingDirectiveDecl *D) {
  VisitNamedDecl(D);
  Record.AddSourceLocation(D->getUsingLoc());
  Record.AddSourceLocation(D->getNamespaceKeyLocation());
  Record.AddNestedNameSpecifierLoc(D->getQualifierLoc());
  Record.AddDeclRef(D->getNominatedNamespace());
  Record.AddDeclRef(dyn_cast<Decl>(D->getCommonAncestor()));
  Code = serialization::DECL_USING_DIRECTIVE;
}

void ASTDeclWriter::VisitUnresolvedUsingValueDecl(UnresolvedUsingValueDecl *D) {
  VisitValueDecl(D);
  Record.AddSourceLocation(D->getUsingLoc());
  Record.AddNestedNameSpecifierLoc(D->getQualifierLoc());
  Record.AddDeclarationNameLoc(D->DNLoc, D->getDeclName());
  Record.AddSourceLocation(D->getEllipsisLoc());
  Code = serialization::DECL_UNRESOLVED_USING_VALUE;
}

void ASTDeclWriter::VisitUnresolvedUsingTypenameDecl(
                                               UnresolvedUsingTypenameDecl *D) {
  VisitTypeDecl(D);
  Record.AddSourceLocation(D->getTypenameLoc());
  Record.AddNestedNameSpecifierLoc(D->getQualifierLoc());
  Record.AddSourceLocation(D->getEllipsisLoc());
  Code = serialization::DECL_UNRESOLVED_USING_TYPENAME;
}

void ASTDeclWriter::VisitCXXRecordDecl(CXXRecordDecl *D) {
  VisitRecordDecl(D);

  enum {
    CXXRecNotTemplate = 0, CXXRecTemplate, CXXRecMemberSpecialization
  };
  if (ClassTemplateDecl *TemplD = D->getDescribedClassTemplate()) {
    Record.push_back(CXXRecTemplate);
    Record.AddDeclRef(TemplD);
  } else if (MemberSpecializationInfo *MSInfo
               = D->getMemberSpecializationInfo()) {
    Record.push_back(CXXRecMemberSpecialization);
    Record.AddDeclRef(MSInfo->getInstantiatedFrom());
    Record.push_back(MSInfo->getTemplateSpecializationKind());
    Record.AddSourceLocation(MSInfo->getPointOfInstantiation());
  } else {
    Record.push_back(CXXRecNotTemplate);
  }

  Record.push_back(D->isThisDeclarationADefinition());
  if (D->isThisDeclarationADefinition())
    Record.AddCXXDefinitionData(D);

  // Store (what we currently believe to be) the key function to avoid
  // deserializing every method so we can compute it.
  if (D->isCompleteDefinition())
    Record.AddDeclRef(Context.getCurrentKeyFunction(D));

  Code = serialization::DECL_CXX_RECORD;
}

void ASTDeclWriter::VisitCXXMethodDecl(CXXMethodDecl *D) {
  VisitFunctionDecl(D);
  if (D->isCanonicalDecl()) {
    Record.push_back(D->size_overridden_methods());
    for (const CXXMethodDecl *MD : D->overridden_methods())
      Record.AddDeclRef(MD);
  } else {
    // We only need to record overridden methods once for the canonical decl.
    Record.push_back(0);
  }

  if (D->getDeclContext() == D->getLexicalDeclContext() &&
      D->getFirstDecl() == D->getMostRecentDecl() &&
      !D->isInvalidDecl() &&
      !D->hasAttrs() &&
      !D->isTopLevelDeclInObjCContainer() &&
      D->getDeclName().getNameKind() == DeclarationName::Identifier &&
      !D->hasExtInfo() &&
      !D->hasInheritedPrototype() &&
      D->hasWrittenPrototype())
    AbbrevToUse = Writer.getDeclCXXMethodAbbrev();

  Code = serialization::DECL_CXX_METHOD;
}

void ASTDeclWriter::VisitCXXConstructorDecl(CXXConstructorDecl *D) {
  if (auto Inherited = D->getInheritedConstructor()) {
    Record.AddDeclRef(Inherited.getShadowDecl());
    Record.AddDeclRef(Inherited.getConstructor());
    Code = serialization::DECL_CXX_INHERITED_CONSTRUCTOR;
  } else {
    Code = serialization::DECL_CXX_CONSTRUCTOR;
  }

  VisitCXXMethodDecl(D);

  Code = D->isInheritingConstructor()
             ? serialization::DECL_CXX_INHERITED_CONSTRUCTOR
             : serialization::DECL_CXX_CONSTRUCTOR;
}

void ASTDeclWriter::VisitCXXDestructorDecl(CXXDestructorDecl *D) {
  VisitCXXMethodDecl(D);

  Record.AddDeclRef(D->getOperatorDelete());
  if (D->getOperatorDelete())
    Record.AddStmt(D->getOperatorDeleteThisArg());

  Code = serialization::DECL_CXX_DESTRUCTOR;
}

void ASTDeclWriter::VisitCXXConversionDecl(CXXConversionDecl *D) {
  VisitCXXMethodDecl(D);
  Code = serialization::DECL_CXX_CONVERSION;
}

void ASTDeclWriter::VisitImportDecl(ImportDecl *D) {
  VisitDecl(D);
  Record.push_back(Writer.getSubmoduleID(D->getImportedModule()));
  ArrayRef<SourceLocation> IdentifierLocs = D->getIdentifierLocs();
  Record.push_back(!IdentifierLocs.empty());
  if (IdentifierLocs.empty()) {
    Record.AddSourceLocation(D->getEndLoc());
    Record.push_back(1);
  } else {
    for (unsigned I = 0, N = IdentifierLocs.size(); I != N; ++I)
      Record.AddSourceLocation(IdentifierLocs[I]);
    Record.push_back(IdentifierLocs.size());
  }
  // Note: the number of source locations must always be the last element in
  // the record.
  Code = serialization::DECL_IMPORT;
}

void ASTDeclWriter::VisitAccessSpecDecl(AccessSpecDecl *D) {
  VisitDecl(D);
  Record.AddSourceLocation(D->getColonLoc());
  Code = serialization::DECL_ACCESS_SPEC;
}

void ASTDeclWriter::VisitFriendDecl(FriendDecl *D) {
  // Record the number of friend type template parameter lists here
  // so as to simplify memory allocation during deserialization.
  Record.push_back(D->NumTPLists);
  VisitDecl(D);
  bool hasFriendDecl = D->Friend.is<NamedDecl*>();
  Record.push_back(hasFriendDecl);
  if (hasFriendDecl)
    Record.AddDeclRef(D->getFriendDecl());
  else
    Record.AddTypeSourceInfo(D->getFriendType());
  for (unsigned i = 0; i < D->NumTPLists; ++i)
    Record.AddTemplateParameterList(D->getFriendTypeTemplateParameterList(i));
  Record.AddDeclRef(D->getNextFriend());
  Record.push_back(D->UnsupportedFriend);
  Record.AddSourceLocation(D->FriendLoc);
  Code = serialization::DECL_FRIEND;
}

void ASTDeclWriter::VisitFriendTemplateDecl(FriendTemplateDecl *D) {
  VisitDecl(D);
  Record.push_back(D->getNumTemplateParameters());
  for (unsigned i = 0, e = D->getNumTemplateParameters(); i != e; ++i)
    Record.AddTemplateParameterList(D->getTemplateParameterList(i));
  Record.push_back(D->getFriendDecl() != nullptr);
  if (D->getFriendDecl())
    Record.AddDeclRef(D->getFriendDecl());
  else
    Record.AddTypeSourceInfo(D->getFriendType());
  Record.AddSourceLocation(D->getFriendLoc());
  Code = serialization::DECL_FRIEND_TEMPLATE;
}

void ASTDeclWriter::VisitTemplateDecl(TemplateDecl *D) {
  VisitNamedDecl(D);

  Record.AddDeclRef(D->getTemplatedDecl());
  Record.AddTemplateParameterList(D->getTemplateParameters());
}

void ASTDeclWriter::VisitRedeclarableTemplateDecl(RedeclarableTemplateDecl *D) {
  VisitRedeclarable(D);

  // Emit data to initialize CommonOrPrev before VisitTemplateDecl so that
  // getCommonPtr() can be used while this is still initializing.
  if (D->isFirstDecl()) {
    // This declaration owns the 'common' pointer, so serialize that data now.
    Record.AddDeclRef(D->getInstantiatedFromMemberTemplate());
    if (D->getInstantiatedFromMemberTemplate())
      Record.push_back(D->isMemberSpecialization());
  }

  VisitTemplateDecl(D);
  Record.push_back(D->getIdentifierNamespace());
}

void ASTDeclWriter::VisitClassTemplateDecl(ClassTemplateDecl *D) {
  VisitRedeclarableTemplateDecl(D);

  if (D->isFirstDecl())
    AddTemplateSpecializations(D);
  Code = serialization::DECL_CLASS_TEMPLATE;
}

void ASTDeclWriter::VisitClassTemplateSpecializationDecl(
                                           ClassTemplateSpecializationDecl *D) {
  RegisterTemplateSpecialization(D->getSpecializedTemplate(), D);

  VisitCXXRecordDecl(D);

  llvm::PointerUnion<ClassTemplateDecl *,
                     ClassTemplatePartialSpecializationDecl *> InstFrom
    = D->getSpecializedTemplateOrPartial();
  if (Decl *InstFromD = InstFrom.dyn_cast<ClassTemplateDecl *>()) {
    Record.AddDeclRef(InstFromD);
  } else {
    Record.AddDeclRef(InstFrom.get<ClassTemplatePartialSpecializationDecl *>());
    Record.AddTemplateArgumentList(&D->getTemplateInstantiationArgs());
  }

  Record.AddTemplateArgumentList(&D->getTemplateArgs());
  Record.AddSourceLocation(D->getPointOfInstantiation());
  Record.push_back(D->getSpecializationKind());
  Record.push_back(D->isCanonicalDecl());

  if (D->isCanonicalDecl()) {
    // When reading, we'll add it to the folding set of the following template.
    Record.AddDeclRef(D->getSpecializedTemplate()->getCanonicalDecl());
  }

  // Explicit info.
  Record.AddTypeSourceInfo(D->getTypeAsWritten());
  if (D->getTypeAsWritten()) {
    Record.AddSourceLocation(D->getExternLoc());
    Record.AddSourceLocation(D->getTemplateKeywordLoc());
  }

  Code = serialization::DECL_CLASS_TEMPLATE_SPECIALIZATION;
}

void ASTDeclWriter::VisitClassTemplatePartialSpecializationDecl(
                                    ClassTemplatePartialSpecializationDecl *D) {
  VisitClassTemplateSpecializationDecl(D);

  Record.AddTemplateParameterList(D->getTemplateParameters());
  Record.AddASTTemplateArgumentListInfo(D->getTemplateArgsAsWritten());

  // These are read/set from/to the first declaration.
  if (D->getPreviousDecl() == nullptr) {
    Record.AddDeclRef(D->getInstantiatedFromMember());
    Record.push_back(D->isMemberSpecialization());
  }

  Code = serialization::DECL_CLASS_TEMPLATE_PARTIAL_SPECIALIZATION;
}

void ASTDeclWriter::VisitVarTemplateDecl(VarTemplateDecl *D) {
  VisitRedeclarableTemplateDecl(D);

  if (D->isFirstDecl())
    AddTemplateSpecializations(D);
  Code = serialization::DECL_VAR_TEMPLATE;
}

void ASTDeclWriter::VisitVarTemplateSpecializationDecl(
    VarTemplateSpecializationDecl *D) {
  RegisterTemplateSpecialization(D->getSpecializedTemplate(), D);

  VisitVarDecl(D);

  llvm::PointerUnion<VarTemplateDecl *, VarTemplatePartialSpecializationDecl *>
  InstFrom = D->getSpecializedTemplateOrPartial();
  if (Decl *InstFromD = InstFrom.dyn_cast<VarTemplateDecl *>()) {
    Record.AddDeclRef(InstFromD);
  } else {
    Record.AddDeclRef(InstFrom.get<VarTemplatePartialSpecializationDecl *>());
    Record.AddTemplateArgumentList(&D->getTemplateInstantiationArgs());
  }

  // Explicit info.
  Record.AddTypeSourceInfo(D->getTypeAsWritten());
  if (D->getTypeAsWritten()) {
    Record.AddSourceLocation(D->getExternLoc());
    Record.AddSourceLocation(D->getTemplateKeywordLoc());
  }

  Record.AddTemplateArgumentList(&D->getTemplateArgs());
  Record.AddSourceLocation(D->getPointOfInstantiation());
  Record.push_back(D->getSpecializationKind());
  Record.push_back(D->IsCompleteDefinition);
  Record.push_back(D->isCanonicalDecl());

  if (D->isCanonicalDecl()) {
    // When reading, we'll add it to the folding set of the following template.
    Record.AddDeclRef(D->getSpecializedTemplate()->getCanonicalDecl());
  }

  Code = serialization::DECL_VAR_TEMPLATE_SPECIALIZATION;
}

void ASTDeclWriter::VisitVarTemplatePartialSpecializationDecl(
    VarTemplatePartialSpecializationDecl *D) {
  VisitVarTemplateSpecializationDecl(D);

  Record.AddTemplateParameterList(D->getTemplateParameters());
  Record.AddASTTemplateArgumentListInfo(D->getTemplateArgsAsWritten());

  // These are read/set from/to the first declaration.
  if (D->getPreviousDecl() == nullptr) {
    Record.AddDeclRef(D->getInstantiatedFromMember());
    Record.push_back(D->isMemberSpecialization());
  }

  Code = serialization::DECL_VAR_TEMPLATE_PARTIAL_SPECIALIZATION;
}

void ASTDeclWriter::VisitClassScopeFunctionSpecializationDecl(
                                    ClassScopeFunctionSpecializationDecl *D) {
  VisitDecl(D);
  Record.AddDeclRef(D->getSpecialization());
  Code = serialization::DECL_CLASS_SCOPE_FUNCTION_SPECIALIZATION;
}


void ASTDeclWriter::VisitFunctionTemplateDecl(FunctionTemplateDecl *D) {
  VisitRedeclarableTemplateDecl(D);

  if (D->isFirstDecl())
    AddTemplateSpecializations(D);
  Code = serialization::DECL_FUNCTION_TEMPLATE;
}

void ASTDeclWriter::VisitTemplateTypeParmDecl(TemplateTypeParmDecl *D) {
  VisitTypeDecl(D);

  Record.push_back(D->wasDeclaredWithTypename());

  bool OwnsDefaultArg = D->hasDefaultArgument() &&
                        !D->defaultArgumentWasInherited();
  Record.push_back(OwnsDefaultArg);
  if (OwnsDefaultArg)
    Record.AddTypeSourceInfo(D->getDefaultArgumentInfo());

  Code = serialization::DECL_TEMPLATE_TYPE_PARM;
}

void ASTDeclWriter::VisitNonTypeTemplateParmDecl(NonTypeTemplateParmDecl *D) {
  // For an expanded parameter pack, record the number of expansion types here
  // so that it's easier for deserialization to allocate the right amount of
  // memory.
  if (D->isExpandedParameterPack())
    Record.push_back(D->getNumExpansionTypes());

  VisitDeclaratorDecl(D);
  // TemplateParmPosition.
  Record.push_back(D->getDepth());
  Record.push_back(D->getPosition());

  if (D->isExpandedParameterPack()) {
    for (unsigned I = 0, N = D->getNumExpansionTypes(); I != N; ++I) {
      Record.AddTypeRef(D->getExpansionType(I));
      Record.AddTypeSourceInfo(D->getExpansionTypeSourceInfo(I));
    }

    Code = serialization::DECL_EXPANDED_NON_TYPE_TEMPLATE_PARM_PACK;
  } else {
    // Rest of NonTypeTemplateParmDecl.
    Record.push_back(D->isParameterPack());
    bool OwnsDefaultArg = D->hasDefaultArgument() &&
                          !D->defaultArgumentWasInherited();
    Record.push_back(OwnsDefaultArg);
    if (OwnsDefaultArg)
      Record.AddStmt(D->getDefaultArgument());
    Code = serialization::DECL_NON_TYPE_TEMPLATE_PARM;
  }
}

void ASTDeclWriter::VisitTemplateTemplateParmDecl(TemplateTemplateParmDecl *D) {
  // For an expanded parameter pack, record the number of expansion types here
  // so that it's easier for deserialization to allocate the right amount of
  // memory.
  if (D->isExpandedParameterPack())
    Record.push_back(D->getNumExpansionTemplateParameters());

  VisitTemplateDecl(D);
  // TemplateParmPosition.
  Record.push_back(D->getDepth());
  Record.push_back(D->getPosition());

  if (D->isExpandedParameterPack()) {
    for (unsigned I = 0, N = D->getNumExpansionTemplateParameters();
         I != N; ++I)
      Record.AddTemplateParameterList(D->getExpansionTemplateParameters(I));
    Code = serialization::DECL_EXPANDED_TEMPLATE_TEMPLATE_PARM_PACK;
  } else {
    // Rest of TemplateTemplateParmDecl.
    Record.push_back(D->isParameterPack());
    bool OwnsDefaultArg = D->hasDefaultArgument() &&
                          !D->defaultArgumentWasInherited();
    Record.push_back(OwnsDefaultArg);
    if (OwnsDefaultArg)
      Record.AddTemplateArgumentLoc(D->getDefaultArgument());
    Code = serialization::DECL_TEMPLATE_TEMPLATE_PARM;
  }
}

void ASTDeclWriter::VisitTypeAliasTemplateDecl(TypeAliasTemplateDecl *D) {
  VisitRedeclarableTemplateDecl(D);
  Code = serialization::DECL_TYPE_ALIAS_TEMPLATE;
}

void ASTDeclWriter::VisitStaticAssertDecl(StaticAssertDecl *D) {
  VisitDecl(D);
  Record.AddStmt(D->getAssertExpr());
  Record.push_back(D->isFailed());
  Record.AddStmt(D->getMessage());
  Record.AddSourceLocation(D->getRParenLoc());
  Code = serialization::DECL_STATIC_ASSERT;
}

/// Emit the DeclContext part of a declaration context decl.
void ASTDeclWriter::VisitDeclContext(DeclContext *DC) {
  Record.AddOffset(Writer.WriteDeclContextLexicalBlock(Context, DC));
  Record.AddOffset(Writer.WriteDeclContextVisibleBlock(Context, DC));
}

const Decl *ASTWriter::getFirstLocalDecl(const Decl *D) {
  assert(IsLocalDecl(D) && "expected a local declaration");

  const Decl *Canon = D->getCanonicalDecl();
  if (IsLocalDecl(Canon))
    return Canon;

  const Decl *&CacheEntry = FirstLocalDeclCache[Canon];
  if (CacheEntry)
    return CacheEntry;

  for (const Decl *Redecl = D; Redecl; Redecl = Redecl->getPreviousDecl())
    if (IsLocalDecl(Redecl))
      D = Redecl;
  return CacheEntry = D;
}

template <typename T>
void ASTDeclWriter::VisitRedeclarable(Redeclarable<T> *D) {
  T *First = D->getFirstDecl();
  T *MostRecent = First->getMostRecentDecl();
  T *DAsT = static_cast<T *>(D);
  if (MostRecent != First) {
    assert(isRedeclarableDeclKind(DAsT->getKind()) &&
           "Not considered redeclarable?");

    Record.AddDeclRef(First);

    // Write out a list of local redeclarations of this declaration if it's the
    // first local declaration in the chain.
    const Decl *FirstLocal = Writer.getFirstLocalDecl(DAsT);
    if (DAsT == FirstLocal) {
      // Emit a list of all imported first declarations so that we can be sure
      // that all redeclarations visible to this module are before D in the
      // redecl chain.
      unsigned I = Record.size();
      Record.push_back(0);
      if (Writer.Chain)
        AddFirstDeclFromEachModule(DAsT, /*IncludeLocal*/false);
      // This is the number of imported first declarations + 1.
      Record[I] = Record.size() - I;

      // Collect the set of local redeclarations of this declaration, from
      // newest to oldest.
      ASTWriter::RecordData LocalRedecls;
      ASTRecordWriter LocalRedeclWriter(Record, LocalRedecls);
      for (const Decl *Prev = FirstLocal->getMostRecentDecl();
           Prev != FirstLocal; Prev = Prev->getPreviousDecl())
        if (!Prev->isFromASTFile())
          LocalRedeclWriter.AddDeclRef(Prev);

      // If we have any redecls, write them now as a separate record preceding
      // the declaration itself.
      if (LocalRedecls.empty())
        Record.push_back(0);
      else
        Record.AddOffset(LocalRedeclWriter.Emit(LOCAL_REDECLARATIONS));
    } else {
      Record.push_back(0);
      Record.AddDeclRef(FirstLocal);
    }

    // Make sure that we serialize both the previous and the most-recent
    // declarations, which (transitively) ensures that all declarations in the
    // chain get serialized.
    //
    // FIXME: This is not correct; when we reach an imported declaration we
    // won't emit its previous declaration.
    (void)Writer.GetDeclRef(D->getPreviousDecl());
    (void)Writer.GetDeclRef(MostRecent);
  } else {
    // We use the sentinel value 0 to indicate an only declaration.
    Record.push_back(0);
  }
}

void ASTDeclWriter::VisitOMPThreadPrivateDecl(OMPThreadPrivateDecl *D) {
  Record.push_back(D->varlist_size());
  VisitDecl(D);
  for (auto *I : D->varlists())
    Record.AddStmt(I);
  Code = serialization::DECL_OMP_THREADPRIVATE;
}

void ASTDeclWriter::VisitOMPRequiresDecl(OMPRequiresDecl *D) {
  Record.push_back(D->clauselist_size());
  VisitDecl(D);
  OMPClauseWriter ClauseWriter(Record); 
  for (OMPClause *C : D->clauselists())
    ClauseWriter.writeClause(C);
  Code = serialization::DECL_OMP_REQUIRES;
}

void ASTDeclWriter::VisitOMPDeclareReductionDecl(OMPDeclareReductionDecl *D) {
  VisitValueDecl(D);
  Record.AddSourceLocation(D->getBeginLoc());
  Record.AddStmt(D->getCombinerIn());
  Record.AddStmt(D->getCombinerOut());
  Record.AddStmt(D->getCombiner());
  Record.AddStmt(D->getInitOrig());
  Record.AddStmt(D->getInitPriv());
  Record.AddStmt(D->getInitializer());
  Record.push_back(D->getInitializerKind());
  Record.AddDeclRef(D->getPrevDeclInScope());
  Code = serialization::DECL_OMP_DECLARE_REDUCTION;
}

void ASTDeclWriter::VisitOMPCapturedExprDecl(OMPCapturedExprDecl *D) {
  VisitVarDecl(D);
  Code = serialization::DECL_OMP_CAPTUREDEXPR;
}

//===----------------------------------------------------------------------===//
// ASTWriter Implementation
//===----------------------------------------------------------------------===//

void ASTWriter::WriteDeclAbbrevs() {
  using namespace llvm;

  std::shared_ptr<BitCodeAbbrev> Abv;

  // Abbreviation for DECL_FIELD
  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::DECL_FIELD));
  // Decl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // DeclContext
  Abv->Add(BitCodeAbbrevOp(0));                       // LexicalDeclContext
  Abv->Add(BitCodeAbbrevOp(0));                       // isInvalidDecl
  Abv->Add(BitCodeAbbrevOp(0));                       // HasAttrs
  Abv->Add(BitCodeAbbrevOp(0));                       // isImplicit
  Abv->Add(BitCodeAbbrevOp(0));                       // isUsed
  Abv->Add(BitCodeAbbrevOp(0));                       // isReferenced
  Abv->Add(BitCodeAbbrevOp(0));                   // TopLevelDeclInObjCContainer
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 2));  // AccessSpecifier
  Abv->Add(BitCodeAbbrevOp(0));                       // ModulePrivate
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // SubmoduleID
  // NamedDecl
  Abv->Add(BitCodeAbbrevOp(0));                       // NameKind = Identifier
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Name
  Abv->Add(BitCodeAbbrevOp(0));                       // AnonDeclNumber
  // ValueDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Type
  // DeclaratorDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // InnerStartLoc
  Abv->Add(BitCodeAbbrevOp(0));                       // hasExtInfo
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // TSIType
  // FieldDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // isMutable
  Abv->Add(BitCodeAbbrevOp(0));                       // InitStyle
  // Type Source Info
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // TypeLoc
  DeclFieldAbbrev = Stream.EmitAbbrev(std::move(Abv));

  // Abbreviation for DECL_OBJC_IVAR
  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::DECL_OBJC_IVAR));
  // Decl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // DeclContext
  Abv->Add(BitCodeAbbrevOp(0));                       // LexicalDeclContext
  Abv->Add(BitCodeAbbrevOp(0));                       // isInvalidDecl
  Abv->Add(BitCodeAbbrevOp(0));                       // HasAttrs
  Abv->Add(BitCodeAbbrevOp(0));                       // isImplicit
  Abv->Add(BitCodeAbbrevOp(0));                       // isUsed
  Abv->Add(BitCodeAbbrevOp(0));                       // isReferenced
  Abv->Add(BitCodeAbbrevOp(0));                   // TopLevelDeclInObjCContainer
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 2));  // AccessSpecifier
  Abv->Add(BitCodeAbbrevOp(0));                       // ModulePrivate
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // SubmoduleID
  // NamedDecl
  Abv->Add(BitCodeAbbrevOp(0));                       // NameKind = Identifier
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Name
  Abv->Add(BitCodeAbbrevOp(0));                       // AnonDeclNumber
  // ValueDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Type
  // DeclaratorDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // InnerStartLoc
  Abv->Add(BitCodeAbbrevOp(0));                       // hasExtInfo
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // TSIType
  // FieldDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // isMutable
  Abv->Add(BitCodeAbbrevOp(0));                       // InitStyle
  // ObjC Ivar
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // getAccessControl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // getSynthesize
  // Type Source Info
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // TypeLoc
  DeclObjCIvarAbbrev = Stream.EmitAbbrev(std::move(Abv));

  // Abbreviation for DECL_ENUM
  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::DECL_ENUM));
  // Redeclarable
  Abv->Add(BitCodeAbbrevOp(0));                       // No redeclaration
  // Decl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // DeclContext
  Abv->Add(BitCodeAbbrevOp(0));                       // LexicalDeclContext
  Abv->Add(BitCodeAbbrevOp(0));                       // isInvalidDecl
  Abv->Add(BitCodeAbbrevOp(0));                       // HasAttrs
  Abv->Add(BitCodeAbbrevOp(0));                       // isImplicit
  Abv->Add(BitCodeAbbrevOp(0));                       // isUsed
  Abv->Add(BitCodeAbbrevOp(0));                       // isReferenced
  Abv->Add(BitCodeAbbrevOp(0));                   // TopLevelDeclInObjCContainer
  Abv->Add(BitCodeAbbrevOp(AS_none));                 // C++ AccessSpecifier
  Abv->Add(BitCodeAbbrevOp(0));                       // ModulePrivate
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // SubmoduleID
  // NamedDecl
  Abv->Add(BitCodeAbbrevOp(0));                       // NameKind = Identifier
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Name
  Abv->Add(BitCodeAbbrevOp(0));                       // AnonDeclNumber
  // TypeDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Source Location
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Type Ref
  // TagDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // IdentifierNamespace
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // getTagKind
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // isCompleteDefinition
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // EmbeddedInDeclarator
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // IsFreeStanding
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // IsCompleteDefinitionRequired
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // SourceLocation
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // SourceLocation
  Abv->Add(BitCodeAbbrevOp(0));                         // ExtInfoKind
  // EnumDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // AddTypeRef
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // IntegerType
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // getPromotionType
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // getNumPositiveBits
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // getNumNegativeBits
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // isScoped
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // isScopedUsingClassTag
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // isFixed
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32));// ODRHash
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // InstantiatedMembEnum
  // DC
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // LexicalOffset
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // VisibleOffset
  DeclEnumAbbrev = Stream.EmitAbbrev(std::move(Abv));

  // Abbreviation for DECL_RECORD
  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::DECL_RECORD));
  // Redeclarable
  Abv->Add(BitCodeAbbrevOp(0));                       // No redeclaration
  // Decl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // DeclContext
  Abv->Add(BitCodeAbbrevOp(0));                       // LexicalDeclContext
  Abv->Add(BitCodeAbbrevOp(0));                       // isInvalidDecl
  Abv->Add(BitCodeAbbrevOp(0));                       // HasAttrs
  Abv->Add(BitCodeAbbrevOp(0));                       // isImplicit
  Abv->Add(BitCodeAbbrevOp(0));                       // isUsed
  Abv->Add(BitCodeAbbrevOp(0));                       // isReferenced
  Abv->Add(BitCodeAbbrevOp(0));                   // TopLevelDeclInObjCContainer
  Abv->Add(BitCodeAbbrevOp(AS_none));                 // C++ AccessSpecifier
  Abv->Add(BitCodeAbbrevOp(0));                       // ModulePrivate
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // SubmoduleID
  // NamedDecl
  Abv->Add(BitCodeAbbrevOp(0));                       // NameKind = Identifier
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Name
  Abv->Add(BitCodeAbbrevOp(0));                       // AnonDeclNumber
  // TypeDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Source Location
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Type Ref
  // TagDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // IdentifierNamespace
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // getTagKind
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // isCompleteDefinition
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // EmbeddedInDeclarator
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // IsFreeStanding
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // IsCompleteDefinitionRequired
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // SourceLocation
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // SourceLocation
  Abv->Add(BitCodeAbbrevOp(0));                         // ExtInfoKind
  // RecordDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // FlexibleArrayMember
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // AnonymousStructUnion
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // hasObjectMember
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // hasVolatileMember

  // isNonTrivialToPrimitiveDefaultInitialize
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1));
  // isNonTrivialToPrimitiveCopy
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1));
  // isNonTrivialToPrimitiveDestroy
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1));
  // isParamDestroyedInCallee
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1));
  // getArgPassingRestrictions
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 2));

  // DC
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // LexicalOffset
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // VisibleOffset
  DeclRecordAbbrev = Stream.EmitAbbrev(std::move(Abv));

  // Abbreviation for DECL_PARM_VAR
  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::DECL_PARM_VAR));
  // Redeclarable
  Abv->Add(BitCodeAbbrevOp(0));                       // No redeclaration
  // Decl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // DeclContext
  Abv->Add(BitCodeAbbrevOp(0));                       // LexicalDeclContext
  Abv->Add(BitCodeAbbrevOp(0));                       // isInvalidDecl
  Abv->Add(BitCodeAbbrevOp(0));                       // HasAttrs
  Abv->Add(BitCodeAbbrevOp(0));                       // isImplicit
  Abv->Add(BitCodeAbbrevOp(0));                       // isUsed
  Abv->Add(BitCodeAbbrevOp(0));                       // isReferenced
  Abv->Add(BitCodeAbbrevOp(0));                   // TopLevelDeclInObjCContainer
  Abv->Add(BitCodeAbbrevOp(AS_none));                 // C++ AccessSpecifier
  Abv->Add(BitCodeAbbrevOp(0));                       // ModulePrivate
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // SubmoduleID
  // NamedDecl
  Abv->Add(BitCodeAbbrevOp(0));                       // NameKind = Identifier
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Name
  Abv->Add(BitCodeAbbrevOp(0));                       // AnonDeclNumber
  // ValueDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Type
  // DeclaratorDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // InnerStartLoc
  Abv->Add(BitCodeAbbrevOp(0));                       // hasExtInfo
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // TSIType
  // VarDecl
  Abv->Add(BitCodeAbbrevOp(0));                       // SClass
  Abv->Add(BitCodeAbbrevOp(0));                       // TSCSpec
  Abv->Add(BitCodeAbbrevOp(0));                       // InitStyle
  Abv->Add(BitCodeAbbrevOp(0));                       // ARCPseudoStrong
  Abv->Add(BitCodeAbbrevOp(0));                       // Linkage
  Abv->Add(BitCodeAbbrevOp(0));                       // HasInit
  Abv->Add(BitCodeAbbrevOp(0));                   // HasMemberSpecializationInfo
  // ParmVarDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // IsObjCMethodParameter
  Abv->Add(BitCodeAbbrevOp(0));                       // ScopeDepth
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // ScopeIndex
  Abv->Add(BitCodeAbbrevOp(0));                       // ObjCDeclQualifier
  Abv->Add(BitCodeAbbrevOp(0));                       // KNRPromoted
  Abv->Add(BitCodeAbbrevOp(0));                       // HasInheritedDefaultArg
  Abv->Add(BitCodeAbbrevOp(0));                   // HasUninstantiatedDefaultArg
  // Type Source Info
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // TypeLoc
  DeclParmVarAbbrev = Stream.EmitAbbrev(std::move(Abv));

  // Abbreviation for DECL_TYPEDEF
  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::DECL_TYPEDEF));
  // Redeclarable
  Abv->Add(BitCodeAbbrevOp(0));                       // No redeclaration
  // Decl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // DeclContext
  Abv->Add(BitCodeAbbrevOp(0));                       // LexicalDeclContext
  Abv->Add(BitCodeAbbrevOp(0));                       // isInvalidDecl
  Abv->Add(BitCodeAbbrevOp(0));                       // HasAttrs
  Abv->Add(BitCodeAbbrevOp(0));                       // isImplicit
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // isUsed
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // isReferenced
  Abv->Add(BitCodeAbbrevOp(0));                   // TopLevelDeclInObjCContainer
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 2)); // C++ AccessSpecifier
  Abv->Add(BitCodeAbbrevOp(0));                       // ModulePrivate
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // SubmoduleID
  // NamedDecl
  Abv->Add(BitCodeAbbrevOp(0));                       // NameKind = Identifier
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Name
  Abv->Add(BitCodeAbbrevOp(0));                       // AnonDeclNumber
  // TypeDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Source Location
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Type Ref
  // TypedefDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // TypeLoc
  DeclTypedefAbbrev = Stream.EmitAbbrev(std::move(Abv));

  // Abbreviation for DECL_VAR
  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::DECL_VAR));
  // Redeclarable
  Abv->Add(BitCodeAbbrevOp(0));                       // No redeclaration
  // Decl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // DeclContext
  Abv->Add(BitCodeAbbrevOp(0));                       // LexicalDeclContext
  Abv->Add(BitCodeAbbrevOp(0));                       // isInvalidDecl
  Abv->Add(BitCodeAbbrevOp(0));                       // HasAttrs
  Abv->Add(BitCodeAbbrevOp(0));                       // isImplicit
  Abv->Add(BitCodeAbbrevOp(0));                       // isUsed
  Abv->Add(BitCodeAbbrevOp(0));                       // isReferenced
  Abv->Add(BitCodeAbbrevOp(0));                   // TopLevelDeclInObjCContainer
  Abv->Add(BitCodeAbbrevOp(AS_none));                 // C++ AccessSpecifier
  Abv->Add(BitCodeAbbrevOp(0));                       // ModulePrivate
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // SubmoduleID
  // NamedDecl
  Abv->Add(BitCodeAbbrevOp(0));                       // NameKind = Identifier
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Name
  Abv->Add(BitCodeAbbrevOp(0));                       // AnonDeclNumber
  // ValueDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Type
  // DeclaratorDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // InnerStartLoc
  Abv->Add(BitCodeAbbrevOp(0));                       // hasExtInfo
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // TSIType
  // VarDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 3)); // SClass
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 2)); // TSCSpec
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 2)); // InitStyle
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // isARCPseudoStrong
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // IsThisDeclarationADemotedDefinition
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // isExceptionVariable
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // isNRVOVariable
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // isCXXForRangeDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // isObjCForDecl
  Abv->Add(BitCodeAbbrevOp(0));                         // isInline
  Abv->Add(BitCodeAbbrevOp(0));                         // isInlineSpecified
  Abv->Add(BitCodeAbbrevOp(0));                         // isConstexpr
  Abv->Add(BitCodeAbbrevOp(0));                         // isInitCapture
  Abv->Add(BitCodeAbbrevOp(0));                         // isPrevDeclInSameScope
  Abv->Add(BitCodeAbbrevOp(0));                         // ImplicitParamKind
  Abv->Add(BitCodeAbbrevOp(0));                         // EscapingByref
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 3)); // Linkage
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 2)); // IsInitICE (local)
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 2)); // VarKind (local enum)
  // Type Source Info
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // TypeLoc
  DeclVarAbbrev = Stream.EmitAbbrev(std::move(Abv));

  // Abbreviation for DECL_CXX_METHOD
  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::DECL_CXX_METHOD));
  // RedeclarableDecl
  Abv->Add(BitCodeAbbrevOp(0));                         // CanonicalDecl
  // Decl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // DeclContext
  Abv->Add(BitCodeAbbrevOp(0));                         // LexicalDeclContext
  Abv->Add(BitCodeAbbrevOp(0));                         // Invalid
  Abv->Add(BitCodeAbbrevOp(0));                         // HasAttrs
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // Implicit
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // Used
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // Referenced
  Abv->Add(BitCodeAbbrevOp(0));                         // InObjCContainer
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 2)); // Access
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // ModulePrivate
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // SubmoduleID
  // NamedDecl
  Abv->Add(BitCodeAbbrevOp(DeclarationName::Identifier)); // NameKind
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // Identifier
  Abv->Add(BitCodeAbbrevOp(0));                         // AnonDeclNumber
  // ValueDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // Type
  // DeclaratorDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // InnerLocStart
  Abv->Add(BitCodeAbbrevOp(0));                         // HasExtInfo
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // TSIType
  // FunctionDecl
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 11)); // IDNS
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 3)); // StorageClass
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // Inline
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // InlineSpecified
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // ExplicitSpecified
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // VirtualAsWritten
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // Pure
  Abv->Add(BitCodeAbbrevOp(0));                         // HasInheritedProto
  Abv->Add(BitCodeAbbrevOp(1));                         // HasWrittenProto
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // Deleted
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // Trivial
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // TrivialForCall
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // Defaulted
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // ExplicitlyDefaulted
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // ImplicitReturnZero
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // Constexpr
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // UsesSEHTry
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // SkippedBody
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // MultiVersion
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // LateParsed
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 3)); // Linkage
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));   // LocEnd
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 32)); // ODRHash
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 3)); // TemplateKind
  // This Array slurps the rest of the record. Fortunately we want to encode
  // (nearly) all the remaining (variable number of) fields in the same way.
  //
  // This is the function template information if any, then
  //         NumParams and Params[] from FunctionDecl, and
  //         NumOverriddenMethods, OverriddenMethods[] from CXXMethodDecl.
  //
  //  Add an AbbrevOp for 'size then elements' and use it here.
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Array));
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6));
  DeclCXXMethodAbbrev = Stream.EmitAbbrev(std::move(Abv));

  // Abbreviation for EXPR_DECL_REF
  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::EXPR_DECL_REF));
  //Stmt
  //Expr
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Type
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); //TypeDependent
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); //ValueDependent
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); //InstantiationDependent
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); //UnexpandedParamPack
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 3)); //GetValueKind
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 3)); //GetObjectKind
  //DeclRefExpr
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); //HasQualifier
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); //GetDeclFound
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); //ExplicitTemplateArgs
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); //HadMultipleCandidates
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed,
                           1)); // RefersToEnclosingVariableOrCapture
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // DeclRef
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Location
  DeclRefExprAbbrev = Stream.EmitAbbrev(std::move(Abv));

  // Abbreviation for EXPR_INTEGER_LITERAL
  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::EXPR_INTEGER_LITERAL));
  //Stmt
  //Expr
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Type
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); //TypeDependent
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); //ValueDependent
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); //InstantiationDependent
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); //UnexpandedParamPack
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 3)); //GetValueKind
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 3)); //GetObjectKind
  //Integer Literal
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Location
  Abv->Add(BitCodeAbbrevOp(32));                      // Bit Width
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Value
  IntegerLiteralAbbrev = Stream.EmitAbbrev(std::move(Abv));

  // Abbreviation for EXPR_CHARACTER_LITERAL
  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::EXPR_CHARACTER_LITERAL));
  //Stmt
  //Expr
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Type
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); //TypeDependent
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); //ValueDependent
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); //InstantiationDependent
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); //UnexpandedParamPack
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 3)); //GetValueKind
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 3)); //GetObjectKind
  //Character Literal
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // getValue
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Location
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 3)); // getKind
  CharacterLiteralAbbrev = Stream.EmitAbbrev(std::move(Abv));

  // Abbreviation for EXPR_IMPLICIT_CAST
  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::EXPR_IMPLICIT_CAST));
  // Stmt
  // Expr
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::VBR, 6)); // Type
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); //TypeDependent
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); //ValueDependent
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); //InstantiationDependent
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); //UnexpandedParamPack
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 3)); //GetValueKind
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 3)); //GetObjectKind
  // CastExpr
  Abv->Add(BitCodeAbbrevOp(0)); // PathSize
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 6)); // CastKind
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, 1)); // PartOfExplicitCast
  // ImplicitCastExpr
  ExprImplicitCastAbbrev = Stream.EmitAbbrev(std::move(Abv));

  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::DECL_CONTEXT_LEXICAL));
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));
  DeclContextLexicalAbbrev = Stream.EmitAbbrev(std::move(Abv));

  Abv = std::make_shared<BitCodeAbbrev>();
  Abv->Add(BitCodeAbbrevOp(serialization::DECL_CONTEXT_VISIBLE));
  Abv->Add(BitCodeAbbrevOp(BitCodeAbbrevOp::Blob));
  DeclContextVisibleLookupAbbrev = Stream.EmitAbbrev(std::move(Abv));
}

/// isRequiredDecl - Check if this is a "required" Decl, which must be seen by
/// consumers of the AST.
///
/// Such decls will always be deserialized from the AST file, so we would like
/// this to be as restrictive as possible. Currently the predicate is driven by
/// code generation requirements, if other clients have a different notion of
/// what is "required" then we may have to consider an alternate scheme where
/// clients can iterate over the top-level decls and get information on them,
/// without necessary deserializing them. We could explicitly require such
/// clients to use a separate API call to "realize" the decl. This should be
/// relatively painless since they would presumably only do it for top-level
/// decls.
static bool isRequiredDecl(const Decl *D, ASTContext &Context,
                           bool WritingModule) {
  // An ObjCMethodDecl is never considered as "required" because its
  // implementation container always is.

  // File scoped assembly or obj-c or OMP declare target implementation must be
  // seen.
  if (isa<FileScopeAsmDecl>(D) || isa<ObjCImplDecl>(D))
    return true;

  if (WritingModule && (isa<VarDecl>(D) || isa<ImportDecl>(D))) {
    // These declarations are part of the module initializer, and are emitted
    // if and when the module is imported, rather than being emitted eagerly.
    return false;
  }

  return Context.DeclMustBeEmitted(D);
}

void ASTWriter::WriteDecl(ASTContext &Context, Decl *D) {
  PrettyDeclStackTraceEntry CrashInfo(Context, D, SourceLocation(),
                                      "serializing");

  // Determine the ID for this declaration.
  serialization::DeclID ID;
  assert(!D->isFromASTFile() && "should not be emitting imported decl");
  serialization::DeclID &IDR = DeclIDs[D];
  if (IDR == 0)
    IDR = NextDeclID++;

  ID = IDR;

  assert(ID >= FirstDeclID && "invalid decl ID");

  RecordData Record;
  ASTDeclWriter W(*this, Context, Record);

  // Build a record for this declaration
  W.Visit(D);

  // Emit this declaration to the bitstream.
  uint64_t Offset = W.Emit(D);

  // Record the offset for this declaration
  SourceLocation Loc = D->getLocation();
  unsigned Index = ID - FirstDeclID;
  if (DeclOffsets.size() == Index)
    DeclOffsets.push_back(DeclOffset(Loc, Offset));
  else if (DeclOffsets.size() < Index) {
    // FIXME: Can/should this happen?
    DeclOffsets.resize(Index+1);
    DeclOffsets[Index].setLocation(Loc);
    DeclOffsets[Index].BitOffset = Offset;
  } else {
    llvm_unreachable("declarations should be emitted in ID order");
  }

  SourceManager &SM = Context.getSourceManager();
  if (Loc.isValid() && SM.isLocalSourceLocation(Loc))
    associateDeclWithFile(D, ID);

  // Note declarations that should be deserialized eagerly so that we can add
  // them to a record in the AST file later.
  if (isRequiredDecl(D, Context, WritingModule))
    EagerlyDeserializedDecls.push_back(ID);
}

void ASTRecordWriter::AddFunctionDefinition(const FunctionDecl *FD) {
  // Switch case IDs are per function body.
  Writer->ClearSwitchCaseIDs();

  assert(FD->doesThisDeclarationHaveABody());
  bool ModulesCodegen = false;
  if (Writer->WritingModule && !FD->isDependentContext()) {
    Optional<GVALinkage> Linkage;
    if (Writer->WritingModule->Kind == Module::ModuleInterfaceUnit) {
      // When building a C++ Modules TS module interface unit, a strong
      // definition in the module interface is provided by the compilation of
      // that module interface unit, not by its users. (Inline functions are
      // still emitted in module users.)
      Linkage = Writer->Context->GetGVALinkageForFunction(FD);
      ModulesCodegen = *Linkage == GVA_StrongExternal;
    }
    if (Writer->Context->getLangOpts().ModulesCodegen) {
      // Under -fmodules-codegen, codegen is performed for all non-internal,
      // non-always_inline functions.
      if (!FD->hasAttr<AlwaysInlineAttr>()) {
        if (!Linkage)
          Linkage = Writer->Context->GetGVALinkageForFunction(FD);
        ModulesCodegen = *Linkage != GVA_Internal;
      }
    }
  }
  Record->push_back(ModulesCodegen);
  if (ModulesCodegen)
    Writer->ModularCodegenDecls.push_back(Writer->GetDeclRef(FD));
  if (auto *CD = dyn_cast<CXXConstructorDecl>(FD)) {
    Record->push_back(CD->getNumCtorInitializers());
    if (CD->getNumCtorInitializers())
      AddCXXCtorInitializers(
          llvm::makeArrayRef(CD->init_begin(), CD->init_end()));
  }
  AddStmt(FD->getBody());
}
