//===--- ASTDumper.cpp - Dumping implementation for ASTs ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the AST dump methods, which dump out the
// AST in a form that exposes type details and other fields.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTDumperUtils.h"
#include "clang/AST/Attr.h"
#include "clang/AST/AttrVisitor.h"
#include "clang/AST/CommentVisitor.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclLookups.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclOpenMP.h"
#include "clang/AST/DeclVisitor.h"
#include "clang/AST/LocInfoType.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/AST/TemplateArgumentVisitor.h"
#include "clang/AST/TextNodeDumper.h"
#include "clang/AST/TypeVisitor.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/Module.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/Support/raw_ostream.h"
using namespace clang;
using namespace clang::comments;

//===----------------------------------------------------------------------===//
// ASTDumper Visitor
//===----------------------------------------------------------------------===//

namespace  {

  class ASTDumper
      : public ConstDeclVisitor<ASTDumper>,
        public ConstStmtVisitor<ASTDumper>,
        public ConstCommentVisitor<ASTDumper, void, const FullComment *>,
        public TypeVisitor<ASTDumper>,
        public ConstAttrVisitor<ASTDumper>,
        public ConstTemplateArgumentVisitor<ASTDumper> {

    TextNodeDumper NodeDumper;

    raw_ostream &OS;

    /// The policy to use for printing; can be defaulted.
    PrintingPolicy PrintPolicy;

    /// Indicates whether we should trigger deserialization of nodes that had
    /// not already been loaded.
    bool Deserialize = false;

    const bool ShowColors;

    /// Dump a child of the current node.
    template<typename Fn> void dumpChild(Fn DoDumpChild) {
      NodeDumper.AddChild(DoDumpChild);
    }
    template <typename Fn> void dumpChild(StringRef Label, Fn DoDumpChild) {
      NodeDumper.AddChild(Label, DoDumpChild);
    }

  public:
    ASTDumper(raw_ostream &OS, const CommandTraits *Traits,
              const SourceManager *SM)
        : ASTDumper(OS, Traits, SM,
                    SM && SM->getDiagnostics().getShowColors()) {}

    ASTDumper(raw_ostream &OS, const CommandTraits *Traits,
              const SourceManager *SM, bool ShowColors)
        : ASTDumper(OS, Traits, SM, ShowColors, LangOptions()) {}
    ASTDumper(raw_ostream &OS, const CommandTraits *Traits,
              const SourceManager *SM, bool ShowColors,
              const PrintingPolicy &PrintPolicy)
        : NodeDumper(OS, ShowColors, SM, PrintPolicy, Traits), OS(OS),
          PrintPolicy(PrintPolicy), ShowColors(ShowColors) {}

    void setDeserialize(bool D) { Deserialize = D; }

    void dumpDecl(const Decl *D);
    void dumpStmt(const Stmt *S, StringRef Label = {});

    // Utilities
    void dumpTypeAsChild(QualType T);
    void dumpTypeAsChild(const Type *T);
    void dumpDeclContext(const DeclContext *DC);
    void dumpLookups(const DeclContext *DC, bool DumpDecls);
    void dumpAttr(const Attr *A);

    // C++ Utilities
    void dumpCXXCtorInitializer(const CXXCtorInitializer *Init);
    void dumpTemplateParameters(const TemplateParameterList *TPL);
    void dumpTemplateArgumentListInfo(const TemplateArgumentListInfo &TALI);
    void dumpTemplateArgumentLoc(const TemplateArgumentLoc &A,
                                 const Decl *From = nullptr,
                                 const char *Label = nullptr);
    void dumpTemplateArgumentList(const TemplateArgumentList &TAL);
    void dumpTemplateArgument(const TemplateArgument &A,
                              SourceRange R = SourceRange(),
                              const Decl *From = nullptr,
                              const char *Label = nullptr);
    template <typename SpecializationDecl>
    void dumpTemplateDeclSpecialization(const SpecializationDecl *D,
                                        bool DumpExplicitInst,
                                        bool DumpRefOnly);
    template <typename TemplateDecl>
    void dumpTemplateDecl(const TemplateDecl *D, bool DumpExplicitInst);

    // Objective-C utilities.
    void dumpObjCTypeParamList(const ObjCTypeParamList *typeParams);

    // Types
    void VisitComplexType(const ComplexType *T) {
      dumpTypeAsChild(T->getElementType());
    }
    void VisitLocInfoType(const LocInfoType *T) {
      dumpTypeAsChild(T->getTypeSourceInfo()->getType());
    }
    void VisitPointerType(const PointerType *T) {
      dumpTypeAsChild(T->getPointeeType());
    }
    void VisitBlockPointerType(const BlockPointerType *T) {
      dumpTypeAsChild(T->getPointeeType());
    }
    void VisitReferenceType(const ReferenceType *T) {
      dumpTypeAsChild(T->getPointeeType());
    }
    void VisitMemberPointerType(const MemberPointerType *T) {
      dumpTypeAsChild(T->getClass());
      dumpTypeAsChild(T->getPointeeType());
    }
    void VisitArrayType(const ArrayType *T) {
      dumpTypeAsChild(T->getElementType());
    }
    void VisitVariableArrayType(const VariableArrayType *T) {
      VisitArrayType(T);
      dumpStmt(T->getSizeExpr());
    }
    void VisitDependentSizedArrayType(const DependentSizedArrayType *T) {
      dumpTypeAsChild(T->getElementType());
      dumpStmt(T->getSizeExpr());
    }
    void VisitDependentSizedExtVectorType(
        const DependentSizedExtVectorType *T) {
      dumpTypeAsChild(T->getElementType());
      dumpStmt(T->getSizeExpr());
    }
    void VisitVectorType(const VectorType *T) {
      dumpTypeAsChild(T->getElementType());
    }
    void VisitFunctionType(const FunctionType *T) {
      dumpTypeAsChild(T->getReturnType());
    }
    void VisitFunctionProtoType(const FunctionProtoType *T) {
      VisitFunctionType(T);
      for (QualType PT : T->getParamTypes())
        dumpTypeAsChild(PT);
      if (T->getExtProtoInfo().Variadic)
        dumpChild([=] { OS << "..."; });
    }
    void VisitTypeOfExprType(const TypeOfExprType *T) {
      dumpStmt(T->getUnderlyingExpr());
    }
    void VisitDecltypeType(const DecltypeType *T) {
      dumpStmt(T->getUnderlyingExpr());
    }
    void VisitUnaryTransformType(const UnaryTransformType *T) {
      dumpTypeAsChild(T->getBaseType());
    }
    void VisitAttributedType(const AttributedType *T) {
      // FIXME: AttrKind
      dumpTypeAsChild(T->getModifiedType());
    }
    void VisitSubstTemplateTypeParmType(const SubstTemplateTypeParmType *T) {
      dumpTypeAsChild(T->getReplacedParameter());
    }
    void VisitSubstTemplateTypeParmPackType(
        const SubstTemplateTypeParmPackType *T) {
      dumpTypeAsChild(T->getReplacedParameter());
      dumpTemplateArgument(T->getArgumentPack());
    }
    void VisitTemplateSpecializationType(const TemplateSpecializationType *T) {
      for (auto &Arg : *T)
        dumpTemplateArgument(Arg);
      if (T->isTypeAlias())
        dumpTypeAsChild(T->getAliasedType());
    }
    void VisitObjCObjectPointerType(const ObjCObjectPointerType *T) {
      dumpTypeAsChild(T->getPointeeType());
    }
    void VisitAtomicType(const AtomicType *T) {
      dumpTypeAsChild(T->getValueType());
    }
    void VisitPipeType(const PipeType *T) {
      dumpTypeAsChild(T->getElementType());
    }
    void VisitAdjustedType(const AdjustedType *T) {
      dumpTypeAsChild(T->getOriginalType());
    }
    void VisitPackExpansionType(const PackExpansionType *T) {
      if (!T->isSugared())
        dumpTypeAsChild(T->getPattern());
    }
    // FIXME: ElaboratedType, DependentNameType,
    // DependentTemplateSpecializationType, ObjCObjectType

    // Decls
    void VisitLabelDecl(const LabelDecl *D);
    void VisitTypedefDecl(const TypedefDecl *D);
    void VisitEnumDecl(const EnumDecl *D);
    void VisitRecordDecl(const RecordDecl *D);
    void VisitEnumConstantDecl(const EnumConstantDecl *D);
    void VisitIndirectFieldDecl(const IndirectFieldDecl *D);
    void VisitFunctionDecl(const FunctionDecl *D);
    void VisitFieldDecl(const FieldDecl *D);
    void VisitVarDecl(const VarDecl *D);
    void VisitDecompositionDecl(const DecompositionDecl *D);
    void VisitBindingDecl(const BindingDecl *D);
    void VisitFileScopeAsmDecl(const FileScopeAsmDecl *D);
    void VisitImportDecl(const ImportDecl *D);
    void VisitPragmaCommentDecl(const PragmaCommentDecl *D);
    void VisitPragmaDetectMismatchDecl(const PragmaDetectMismatchDecl *D);
    void VisitCapturedDecl(const CapturedDecl *D);

    // OpenMP decls
    void VisitOMPThreadPrivateDecl(const OMPThreadPrivateDecl *D);
    void VisitOMPDeclareReductionDecl(const OMPDeclareReductionDecl *D);
    void VisitOMPRequiresDecl(const OMPRequiresDecl *D);
    void VisitOMPCapturedExprDecl(const OMPCapturedExprDecl *D);

    // C++ Decls
    void VisitNamespaceDecl(const NamespaceDecl *D);
    void VisitUsingDirectiveDecl(const UsingDirectiveDecl *D);
    void VisitNamespaceAliasDecl(const NamespaceAliasDecl *D);
    void VisitTypeAliasDecl(const TypeAliasDecl *D);
    void VisitTypeAliasTemplateDecl(const TypeAliasTemplateDecl *D);
    void VisitCXXRecordDecl(const CXXRecordDecl *D);
    void VisitStaticAssertDecl(const StaticAssertDecl *D);
    void VisitFunctionTemplateDecl(const FunctionTemplateDecl *D);
    void VisitClassTemplateDecl(const ClassTemplateDecl *D);
    void VisitClassTemplateSpecializationDecl(
        const ClassTemplateSpecializationDecl *D);
    void VisitClassTemplatePartialSpecializationDecl(
        const ClassTemplatePartialSpecializationDecl *D);
    void VisitClassScopeFunctionSpecializationDecl(
        const ClassScopeFunctionSpecializationDecl *D);
    void VisitBuiltinTemplateDecl(const BuiltinTemplateDecl *D);
    void VisitVarTemplateDecl(const VarTemplateDecl *D);
    void VisitVarTemplateSpecializationDecl(
        const VarTemplateSpecializationDecl *D);
    void VisitVarTemplatePartialSpecializationDecl(
        const VarTemplatePartialSpecializationDecl *D);
    void VisitTemplateTypeParmDecl(const TemplateTypeParmDecl *D);
    void VisitNonTypeTemplateParmDecl(const NonTypeTemplateParmDecl *D);
    void VisitTemplateTemplateParmDecl(const TemplateTemplateParmDecl *D);
    void VisitUsingDecl(const UsingDecl *D);
    void VisitUnresolvedUsingTypenameDecl(const UnresolvedUsingTypenameDecl *D);
    void VisitUnresolvedUsingValueDecl(const UnresolvedUsingValueDecl *D);
    void VisitUsingShadowDecl(const UsingShadowDecl *D);
    void VisitConstructorUsingShadowDecl(const ConstructorUsingShadowDecl *D);
    void VisitLinkageSpecDecl(const LinkageSpecDecl *D);
    void VisitAccessSpecDecl(const AccessSpecDecl *D);
    void VisitFriendDecl(const FriendDecl *D);

    // ObjC Decls
    void VisitObjCIvarDecl(const ObjCIvarDecl *D);
    void VisitObjCMethodDecl(const ObjCMethodDecl *D);
    void VisitObjCTypeParamDecl(const ObjCTypeParamDecl *D);
    void VisitObjCCategoryDecl(const ObjCCategoryDecl *D);
    void VisitObjCCategoryImplDecl(const ObjCCategoryImplDecl *D);
    void VisitObjCProtocolDecl(const ObjCProtocolDecl *D);
    void VisitObjCInterfaceDecl(const ObjCInterfaceDecl *D);
    void VisitObjCImplementationDecl(const ObjCImplementationDecl *D);
    void VisitObjCCompatibleAliasDecl(const ObjCCompatibleAliasDecl *D);
    void VisitObjCPropertyDecl(const ObjCPropertyDecl *D);
    void VisitObjCPropertyImplDecl(const ObjCPropertyImplDecl *D);
    void Visit(const BlockDecl::Capture &C);
    void VisitBlockDecl(const BlockDecl *D);

    // Stmts.
    void VisitDeclStmt(const DeclStmt *Node);
    void VisitAttributedStmt(const AttributedStmt *Node);
    void VisitCXXCatchStmt(const CXXCatchStmt *Node);
    void VisitCapturedStmt(const CapturedStmt *Node);

    // OpenMP
    void Visit(const OMPClause *C);
    void VisitOMPExecutableDirective(const OMPExecutableDirective *Node);

    // Exprs
    void VisitInitListExpr(const InitListExpr *ILE);
    void VisitBlockExpr(const BlockExpr *Node);
    void VisitOpaqueValueExpr(const OpaqueValueExpr *Node);
    void VisitGenericSelectionExpr(const GenericSelectionExpr *E);

    // C++
    void VisitLambdaExpr(const LambdaExpr *Node) {
      dumpDecl(Node->getLambdaClass());
    }
    void VisitSizeOfPackExpr(const SizeOfPackExpr *Node);

    // ObjC
    void VisitObjCAtCatchStmt(const ObjCAtCatchStmt *Node);

    // Comments.
    void dumpComment(const Comment *C, const FullComment *FC);

    void VisitExpressionTemplateArgument(const TemplateArgument &TA) {
      dumpStmt(TA.getAsExpr());
    }
    void VisitPackTemplateArgument(const TemplateArgument &TA) {
      for (const auto &TArg : TA.pack_elements())
        dumpTemplateArgument(TArg);
    }

// Implements Visit methods for Attrs.
#include "clang/AST/AttrNodeTraverse.inc"
  };
}

//===----------------------------------------------------------------------===//
//  Utilities
//===----------------------------------------------------------------------===//

void ASTDumper::dumpTypeAsChild(QualType T) {
  SplitQualType SQT = T.split();
  if (!SQT.Quals.hasQualifiers())
    return dumpTypeAsChild(SQT.Ty);

  dumpChild([=] {
    NodeDumper.Visit(T);
    dumpTypeAsChild(T.split().Ty);
  });
}

void ASTDumper::dumpTypeAsChild(const Type *T) {
  dumpChild([=] {
    NodeDumper.Visit(T);
    if (!T)
      return;
    TypeVisitor<ASTDumper>::Visit(T);

    QualType SingleStepDesugar =
        T->getLocallyUnqualifiedSingleStepDesugaredType();
    if (SingleStepDesugar != QualType(T, 0))
      dumpTypeAsChild(SingleStepDesugar);
  });
}

void ASTDumper::dumpDeclContext(const DeclContext *DC) {
  if (!DC)
    return;

  for (auto *D : (Deserialize ? DC->decls() : DC->noload_decls()))
    dumpDecl(D);

  if (DC->hasExternalLexicalStorage()) {
    dumpChild([=] {
      ColorScope Color(OS, ShowColors, UndeserializedColor);
      OS << "<undeserialized declarations>";
    });
  }
}

void ASTDumper::dumpLookups(const DeclContext *DC, bool DumpDecls) {
  dumpChild([=] {
    OS << "StoredDeclsMap ";
    NodeDumper.dumpBareDeclRef(cast<Decl>(DC));

    const DeclContext *Primary = DC->getPrimaryContext();
    if (Primary != DC) {
      OS << " primary";
      NodeDumper.dumpPointer(cast<Decl>(Primary));
    }

    bool HasUndeserializedLookups = Primary->hasExternalVisibleStorage();

    auto Range = Deserialize
                     ? Primary->lookups()
                     : Primary->noload_lookups(/*PreserveInternalState=*/true);
    for (auto I = Range.begin(), E = Range.end(); I != E; ++I) {
      DeclarationName Name = I.getLookupName();
      DeclContextLookupResult R = *I;

      dumpChild([=] {
        OS << "DeclarationName ";
        {
          ColorScope Color(OS, ShowColors, DeclNameColor);
          OS << '\'' << Name << '\'';
        }

        for (DeclContextLookupResult::iterator RI = R.begin(), RE = R.end();
             RI != RE; ++RI) {
          dumpChild([=] {
            NodeDumper.dumpBareDeclRef(*RI);

            if ((*RI)->isHidden())
              OS << " hidden";

            // If requested, dump the redecl chain for this lookup.
            if (DumpDecls) {
              // Dump earliest decl first.
              std::function<void(Decl *)> DumpWithPrev = [&](Decl *D) {
                if (Decl *Prev = D->getPreviousDecl())
                  DumpWithPrev(Prev);
                dumpDecl(D);
              };
              DumpWithPrev(*RI);
            }
          });
        }
      });
    }

    if (HasUndeserializedLookups) {
      dumpChild([=] {
        ColorScope Color(OS, ShowColors, UndeserializedColor);
        OS << "<undeserialized lookups>";
      });
    }
  });
}

void ASTDumper::dumpAttr(const Attr *A) {
  dumpChild([=] {
    NodeDumper.Visit(A);
    ConstAttrVisitor<ASTDumper>::Visit(A);
  });
}

//===----------------------------------------------------------------------===//
//  C++ Utilities
//===----------------------------------------------------------------------===//

void ASTDumper::dumpCXXCtorInitializer(const CXXCtorInitializer *Init) {
  dumpChild([=] {
    NodeDumper.Visit(Init);
    dumpStmt(Init->getInit());
  });
}

void ASTDumper::dumpTemplateParameters(const TemplateParameterList *TPL) {
  if (!TPL)
    return;

  for (TemplateParameterList::const_iterator I = TPL->begin(), E = TPL->end();
       I != E; ++I)
    dumpDecl(*I);
}

void ASTDumper::dumpTemplateArgumentListInfo(
    const TemplateArgumentListInfo &TALI) {
  for (unsigned i = 0, e = TALI.size(); i < e; ++i)
    dumpTemplateArgumentLoc(TALI[i]);
}

void ASTDumper::dumpTemplateArgumentLoc(const TemplateArgumentLoc &A,
                                        const Decl *From, const char *Label) {
  dumpTemplateArgument(A.getArgument(), A.getSourceRange(), From, Label);
}

void ASTDumper::dumpTemplateArgumentList(const TemplateArgumentList &TAL) {
  for (unsigned i = 0, e = TAL.size(); i < e; ++i)
    dumpTemplateArgument(TAL[i]);
}

void ASTDumper::dumpTemplateArgument(const TemplateArgument &A, SourceRange R,
                                     const Decl *From, const char *Label) {
  dumpChild([=] {
    NodeDumper.Visit(A, R, From, Label);
    ConstTemplateArgumentVisitor<ASTDumper>::Visit(A);
  });
}

//===----------------------------------------------------------------------===//
//  Objective-C Utilities
//===----------------------------------------------------------------------===//
void ASTDumper::dumpObjCTypeParamList(const ObjCTypeParamList *typeParams) {
  if (!typeParams)
    return;

  for (auto typeParam : *typeParams) {
    dumpDecl(typeParam);
  }
}

//===----------------------------------------------------------------------===//
//  Decl dumping methods.
//===----------------------------------------------------------------------===//

void ASTDumper::dumpDecl(const Decl *D) {
  dumpChild([=] {
    NodeDumper.Visit(D);
    if (!D)
      return;

    ConstDeclVisitor<ASTDumper>::Visit(D);

    for (Decl::attr_iterator I = D->attr_begin(), E = D->attr_end(); I != E;
         ++I)
      dumpAttr(*I);

    if (const FullComment *Comment =
            D->getASTContext().getLocalCommentForDeclUncached(D))
      dumpComment(Comment, Comment);

    // Decls within functions are visited by the body.
    if (!isa<FunctionDecl>(*D) && !isa<ObjCMethodDecl>(*D)) {
      auto DC = dyn_cast<DeclContext>(D);
      if (DC &&
          (DC->hasExternalLexicalStorage() ||
           (Deserialize ? DC->decls_begin() != DC->decls_end()
                        : DC->noload_decls_begin() != DC->noload_decls_end())))
        dumpDeclContext(DC);
    }
  });
}

void ASTDumper::VisitLabelDecl(const LabelDecl *D) { NodeDumper.dumpName(D); }

void ASTDumper::VisitTypedefDecl(const TypedefDecl *D) {
  NodeDumper.dumpName(D);
  NodeDumper.dumpType(D->getUnderlyingType());
  if (D->isModulePrivate())
    OS << " __module_private__";
  dumpTypeAsChild(D->getUnderlyingType());
}

void ASTDumper::VisitEnumDecl(const EnumDecl *D) {
  if (D->isScoped()) {
    if (D->isScopedUsingClassTag())
      OS << " class";
    else
      OS << " struct";
  }
  NodeDumper.dumpName(D);
  if (D->isModulePrivate())
    OS << " __module_private__";
  if (D->isFixed())
    NodeDumper.dumpType(D->getIntegerType());
}

void ASTDumper::VisitRecordDecl(const RecordDecl *D) {
  OS << ' ' << D->getKindName();
  NodeDumper.dumpName(D);
  if (D->isModulePrivate())
    OS << " __module_private__";
  if (D->isCompleteDefinition())
    OS << " definition";
}

void ASTDumper::VisitEnumConstantDecl(const EnumConstantDecl *D) {
  NodeDumper.dumpName(D);
  NodeDumper.dumpType(D->getType());
  if (const Expr *Init = D->getInitExpr())
    dumpStmt(Init);
}

void ASTDumper::VisitIndirectFieldDecl(const IndirectFieldDecl *D) {
  NodeDumper.dumpName(D);
  NodeDumper.dumpType(D->getType());

  for (auto *Child : D->chain())
    NodeDumper.dumpDeclRef(Child);
}

void ASTDumper::VisitFunctionDecl(const FunctionDecl *D) {
  NodeDumper.dumpName(D);
  NodeDumper.dumpType(D->getType());

  StorageClass SC = D->getStorageClass();
  if (SC != SC_None)
    OS << ' ' << VarDecl::getStorageClassSpecifierString(SC);
  if (D->isInlineSpecified())
    OS << " inline";
  if (D->isVirtualAsWritten())
    OS << " virtual";
  if (D->isModulePrivate())
    OS << " __module_private__";

  if (D->isPure())
    OS << " pure";
  if (D->isDefaulted()) {
    OS << " default";
    if (D->isDeleted())
      OS << "_delete";
  }
  if (D->isDeletedAsWritten())
    OS << " delete";
  if (D->isTrivial())
    OS << " trivial";

  if (const auto *FPT = D->getType()->getAs<FunctionProtoType>()) {
    FunctionProtoType::ExtProtoInfo EPI = FPT->getExtProtoInfo();
    switch (EPI.ExceptionSpec.Type) {
    default: break;
    case EST_Unevaluated:
      OS << " noexcept-unevaluated " << EPI.ExceptionSpec.SourceDecl;
      break;
    case EST_Uninstantiated:
      OS << " noexcept-uninstantiated " << EPI.ExceptionSpec.SourceTemplate;
      break;
    }
  }

  if (const auto *MD = dyn_cast<CXXMethodDecl>(D)) {
    if (MD->size_overridden_methods() != 0) {
      auto dumpOverride = [=](const CXXMethodDecl *D) {
        SplitQualType T_split = D->getType().split();
        OS << D << " " << D->getParent()->getName()
           << "::" << D->getNameAsString() << " '"
           << QualType::getAsString(T_split, PrintPolicy) << "'";
      };

      dumpChild([=] {
        auto Overrides = MD->overridden_methods();
        OS << "Overrides: [ ";
        dumpOverride(*Overrides.begin());
        for (const auto *Override :
             llvm::make_range(Overrides.begin() + 1, Overrides.end())) {
          OS << ", ";
          dumpOverride(Override);
        }
        OS << " ]";
      });
    }
  }

  if (const auto *FTSI = D->getTemplateSpecializationInfo())
    dumpTemplateArgumentList(*FTSI->TemplateArguments);

  if (!D->param_begin() && D->getNumParams())
    dumpChild([=] { OS << "<<NULL params x " << D->getNumParams() << ">>"; });
  else
    for (const ParmVarDecl *Parameter : D->parameters())
      dumpDecl(Parameter);

  if (const auto *C = dyn_cast<CXXConstructorDecl>(D))
    for (const auto *I : C->inits())
      dumpCXXCtorInitializer(I);

  if (D->doesThisDeclarationHaveABody())
    dumpStmt(D->getBody());
}

void ASTDumper::VisitFieldDecl(const FieldDecl *D) {
  NodeDumper.dumpName(D);
  NodeDumper.dumpType(D->getType());
  if (D->isMutable())
    OS << " mutable";
  if (D->isModulePrivate())
    OS << " __module_private__";

  if (D->isBitField())
    dumpStmt(D->getBitWidth());
  if (Expr *Init = D->getInClassInitializer())
    dumpStmt(Init);
}

void ASTDumper::VisitVarDecl(const VarDecl *D) {
  NodeDumper.dumpName(D);
  NodeDumper.dumpType(D->getType());
  StorageClass SC = D->getStorageClass();
  if (SC != SC_None)
    OS << ' ' << VarDecl::getStorageClassSpecifierString(SC);
  switch (D->getTLSKind()) {
  case VarDecl::TLS_None: break;
  case VarDecl::TLS_Static: OS << " tls"; break;
  case VarDecl::TLS_Dynamic: OS << " tls_dynamic"; break;
  }
  if (D->isModulePrivate())
    OS << " __module_private__";
  if (D->isNRVOVariable())
    OS << " nrvo";
  if (D->isInline())
    OS << " inline";
  if (D->isConstexpr())
    OS << " constexpr";
  if (D->hasInit()) {
    switch (D->getInitStyle()) {
    case VarDecl::CInit: OS << " cinit"; break;
    case VarDecl::CallInit: OS << " callinit"; break;
    case VarDecl::ListInit: OS << " listinit"; break;
    }
    dumpStmt(D->getInit());
  }
}

void ASTDumper::VisitDecompositionDecl(const DecompositionDecl *D) {
  VisitVarDecl(D);
  for (auto *B : D->bindings())
    dumpDecl(B);
}

void ASTDumper::VisitBindingDecl(const BindingDecl *D) {
  NodeDumper.dumpName(D);
  NodeDumper.dumpType(D->getType());
  if (auto *E = D->getBinding())
    dumpStmt(E);
}

void ASTDumper::VisitFileScopeAsmDecl(const FileScopeAsmDecl *D) {
  dumpStmt(D->getAsmString());
}

void ASTDumper::VisitImportDecl(const ImportDecl *D) {
  OS << ' ' << D->getImportedModule()->getFullModuleName();
}

void ASTDumper::VisitPragmaCommentDecl(const PragmaCommentDecl *D) {
  OS << ' ';
  switch (D->getCommentKind()) {
  case PCK_Unknown:  llvm_unreachable("unexpected pragma comment kind");
  case PCK_Compiler: OS << "compiler"; break;
  case PCK_ExeStr:   OS << "exestr"; break;
  case PCK_Lib:      OS << "lib"; break;
  case PCK_Linker:   OS << "linker"; break;
  case PCK_User:     OS << "user"; break;
  }
  StringRef Arg = D->getArg();
  if (!Arg.empty())
    OS << " \"" << Arg << "\"";
}

void ASTDumper::VisitPragmaDetectMismatchDecl(
    const PragmaDetectMismatchDecl *D) {
  OS << " \"" << D->getName() << "\" \"" << D->getValue() << "\"";
}

void ASTDumper::VisitCapturedDecl(const CapturedDecl *D) {
  dumpStmt(D->getBody());
}

//===----------------------------------------------------------------------===//
// OpenMP Declarations
//===----------------------------------------------------------------------===//

void ASTDumper::VisitOMPThreadPrivateDecl(const OMPThreadPrivateDecl *D) {
  for (auto *E : D->varlists())
    dumpStmt(E);
}

void ASTDumper::VisitOMPDeclareReductionDecl(const OMPDeclareReductionDecl *D) {
  NodeDumper.dumpName(D);
  NodeDumper.dumpType(D->getType());
  OS << " combiner";
  NodeDumper.dumpPointer(D->getCombiner());
  if (const auto *Initializer = D->getInitializer()) {
    OS << " initializer";
    NodeDumper.dumpPointer(Initializer);
    switch (D->getInitializerKind()) {
    case OMPDeclareReductionDecl::DirectInit:
      OS << " omp_priv = ";
      break;
    case OMPDeclareReductionDecl::CopyInit:
      OS << " omp_priv ()";
      break;
    case OMPDeclareReductionDecl::CallInit:
      break;
    }
  }

  dumpStmt(D->getCombiner());
  if (const auto *Initializer = D->getInitializer())
    dumpStmt(Initializer);
}

void ASTDumper::VisitOMPRequiresDecl(const OMPRequiresDecl *D) {
  for (auto *C : D->clauselists()) {
    dumpChild([=] {
      if (!C) {
        ColorScope Color(OS, ShowColors, NullColor);
        OS << "<<<NULL>>> OMPClause";
        return;
      }
      {
        ColorScope Color(OS, ShowColors, AttrColor);
        StringRef ClauseName(getOpenMPClauseName(C->getClauseKind()));
        OS << "OMP" << ClauseName.substr(/*Start=*/0, /*N=*/1).upper()
           << ClauseName.drop_front() << "Clause";
      }
      NodeDumper.dumpPointer(C);
      NodeDumper.dumpSourceRange(SourceRange(C->getBeginLoc(), C->getEndLoc()));
    });
  }
}

void ASTDumper::VisitOMPCapturedExprDecl(const OMPCapturedExprDecl *D) {
  NodeDumper.dumpName(D);
  NodeDumper.dumpType(D->getType());
  dumpStmt(D->getInit());
}

//===----------------------------------------------------------------------===//
// C++ Declarations
//===----------------------------------------------------------------------===//

void ASTDumper::VisitNamespaceDecl(const NamespaceDecl *D) {
  NodeDumper.dumpName(D);
  if (D->isInline())
    OS << " inline";
  if (!D->isOriginalNamespace())
    NodeDumper.dumpDeclRef(D->getOriginalNamespace(), "original");
}

void ASTDumper::VisitUsingDirectiveDecl(const UsingDirectiveDecl *D) {
  OS << ' ';
  NodeDumper.dumpBareDeclRef(D->getNominatedNamespace());
}

void ASTDumper::VisitNamespaceAliasDecl(const NamespaceAliasDecl *D) {
  NodeDumper.dumpName(D);
  NodeDumper.dumpDeclRef(D->getAliasedNamespace());
}

void ASTDumper::VisitTypeAliasDecl(const TypeAliasDecl *D) {
  NodeDumper.dumpName(D);
  NodeDumper.dumpType(D->getUnderlyingType());
  dumpTypeAsChild(D->getUnderlyingType());
}

void ASTDumper::VisitTypeAliasTemplateDecl(const TypeAliasTemplateDecl *D) {
  NodeDumper.dumpName(D);
  dumpTemplateParameters(D->getTemplateParameters());
  dumpDecl(D->getTemplatedDecl());
}

void ASTDumper::VisitCXXRecordDecl(const CXXRecordDecl *D) {
  VisitRecordDecl(D);
  if (!D->isCompleteDefinition())
    return;

  dumpChild([=] {
    {
      ColorScope Color(OS, ShowColors, DeclKindNameColor);
      OS << "DefinitionData";
    }
#define FLAG(fn, name) if (D->fn()) OS << " " #name;
    FLAG(isParsingBaseSpecifiers, parsing_base_specifiers);

    FLAG(isGenericLambda, generic);
    FLAG(isLambda, lambda);

    FLAG(canPassInRegisters, pass_in_registers);
    FLAG(isEmpty, empty);
    FLAG(isAggregate, aggregate);
    FLAG(isStandardLayout, standard_layout);
    FLAG(isTriviallyCopyable, trivially_copyable);
    FLAG(isPOD, pod);
    FLAG(isTrivial, trivial);
    FLAG(isPolymorphic, polymorphic);
    FLAG(isAbstract, abstract);
    FLAG(isLiteral, literal);

    FLAG(hasUserDeclaredConstructor, has_user_declared_ctor);
    FLAG(hasConstexprNonCopyMoveConstructor, has_constexpr_non_copy_move_ctor);
    FLAG(hasMutableFields, has_mutable_fields);
    FLAG(hasVariantMembers, has_variant_members);
    FLAG(allowConstDefaultInit, can_const_default_init);

    dumpChild([=] {
      {
        ColorScope Color(OS, ShowColors, DeclKindNameColor);
        OS << "DefaultConstructor";
      }
      FLAG(hasDefaultConstructor, exists);
      FLAG(hasTrivialDefaultConstructor, trivial);
      FLAG(hasNonTrivialDefaultConstructor, non_trivial);
      FLAG(hasUserProvidedDefaultConstructor, user_provided);
      FLAG(hasConstexprDefaultConstructor, constexpr);
      FLAG(needsImplicitDefaultConstructor, needs_implicit);
      FLAG(defaultedDefaultConstructorIsConstexpr, defaulted_is_constexpr);
    });

    dumpChild([=] {
      {
        ColorScope Color(OS, ShowColors, DeclKindNameColor);
        OS << "CopyConstructor";
      }
      FLAG(hasSimpleCopyConstructor, simple);
      FLAG(hasTrivialCopyConstructor, trivial);
      FLAG(hasNonTrivialCopyConstructor, non_trivial);
      FLAG(hasUserDeclaredCopyConstructor, user_declared);
      FLAG(hasCopyConstructorWithConstParam, has_const_param);
      FLAG(needsImplicitCopyConstructor, needs_implicit);
      FLAG(needsOverloadResolutionForCopyConstructor,
           needs_overload_resolution);
      if (!D->needsOverloadResolutionForCopyConstructor())
        FLAG(defaultedCopyConstructorIsDeleted, defaulted_is_deleted);
      FLAG(implicitCopyConstructorHasConstParam, implicit_has_const_param);
    });

    dumpChild([=] {
      {
        ColorScope Color(OS, ShowColors, DeclKindNameColor);
        OS << "MoveConstructor";
      }
      FLAG(hasMoveConstructor, exists);
      FLAG(hasSimpleMoveConstructor, simple);
      FLAG(hasTrivialMoveConstructor, trivial);
      FLAG(hasNonTrivialMoveConstructor, non_trivial);
      FLAG(hasUserDeclaredMoveConstructor, user_declared);
      FLAG(needsImplicitMoveConstructor, needs_implicit);
      FLAG(needsOverloadResolutionForMoveConstructor,
           needs_overload_resolution);
      if (!D->needsOverloadResolutionForMoveConstructor())
        FLAG(defaultedMoveConstructorIsDeleted, defaulted_is_deleted);
    });

    dumpChild([=] {
      {
        ColorScope Color(OS, ShowColors, DeclKindNameColor);
        OS << "CopyAssignment";
      }
      FLAG(hasTrivialCopyAssignment, trivial);
      FLAG(hasNonTrivialCopyAssignment, non_trivial);
      FLAG(hasCopyAssignmentWithConstParam, has_const_param);
      FLAG(hasUserDeclaredCopyAssignment, user_declared);
      FLAG(needsImplicitCopyAssignment, needs_implicit);
      FLAG(needsOverloadResolutionForCopyAssignment, needs_overload_resolution);
      FLAG(implicitCopyAssignmentHasConstParam, implicit_has_const_param);
    });

    dumpChild([=] {
      {
        ColorScope Color(OS, ShowColors, DeclKindNameColor);
        OS << "MoveAssignment";
      }
      FLAG(hasMoveAssignment, exists);
      FLAG(hasSimpleMoveAssignment, simple);
      FLAG(hasTrivialMoveAssignment, trivial);
      FLAG(hasNonTrivialMoveAssignment, non_trivial);
      FLAG(hasUserDeclaredMoveAssignment, user_declared);
      FLAG(needsImplicitMoveAssignment, needs_implicit);
      FLAG(needsOverloadResolutionForMoveAssignment, needs_overload_resolution);
    });

    dumpChild([=] {
      {
        ColorScope Color(OS, ShowColors, DeclKindNameColor);
        OS << "Destructor";
      }
      FLAG(hasSimpleDestructor, simple);
      FLAG(hasIrrelevantDestructor, irrelevant);
      FLAG(hasTrivialDestructor, trivial);
      FLAG(hasNonTrivialDestructor, non_trivial);
      FLAG(hasUserDeclaredDestructor, user_declared);
      FLAG(needsImplicitDestructor, needs_implicit);
      FLAG(needsOverloadResolutionForDestructor, needs_overload_resolution);
      if (!D->needsOverloadResolutionForDestructor())
        FLAG(defaultedDestructorIsDeleted, defaulted_is_deleted);
    });
  });

  for (const auto &I : D->bases()) {
    dumpChild([=] {
      if (I.isVirtual())
        OS << "virtual ";
      NodeDumper.dumpAccessSpecifier(I.getAccessSpecifier());
      NodeDumper.dumpType(I.getType());
      if (I.isPackExpansion())
        OS << "...";
    });
  }
}

void ASTDumper::VisitStaticAssertDecl(const StaticAssertDecl *D) {
  dumpStmt(D->getAssertExpr());
  dumpStmt(D->getMessage());
}

template <typename SpecializationDecl>
void ASTDumper::dumpTemplateDeclSpecialization(const SpecializationDecl *D,
                                               bool DumpExplicitInst,
                                               bool DumpRefOnly) {
  bool DumpedAny = false;
  for (auto *RedeclWithBadType : D->redecls()) {
    // FIXME: The redecls() range sometimes has elements of a less-specific
    // type. (In particular, ClassTemplateSpecializationDecl::redecls() gives
    // us TagDecls, and should give CXXRecordDecls).
    auto *Redecl = dyn_cast<SpecializationDecl>(RedeclWithBadType);
    if (!Redecl) {
      // Found the injected-class-name for a class template. This will be dumped
      // as part of its surrounding class so we don't need to dump it here.
      assert(isa<CXXRecordDecl>(RedeclWithBadType) &&
             "expected an injected-class-name");
      continue;
    }

    switch (Redecl->getTemplateSpecializationKind()) {
    case TSK_ExplicitInstantiationDeclaration:
    case TSK_ExplicitInstantiationDefinition:
      if (!DumpExplicitInst)
        break;
      LLVM_FALLTHROUGH;
    case TSK_Undeclared:
    case TSK_ImplicitInstantiation:
      if (DumpRefOnly)
        NodeDumper.dumpDeclRef(Redecl);
      else
        dumpDecl(Redecl);
      DumpedAny = true;
      break;
    case TSK_ExplicitSpecialization:
      break;
    }
  }

  // Ensure we dump at least one decl for each specialization.
  if (!DumpedAny)
    NodeDumper.dumpDeclRef(D);
}

template <typename TemplateDecl>
void ASTDumper::dumpTemplateDecl(const TemplateDecl *D, bool DumpExplicitInst) {
  NodeDumper.dumpName(D);
  dumpTemplateParameters(D->getTemplateParameters());

  dumpDecl(D->getTemplatedDecl());

  for (auto *Child : D->specializations())
    dumpTemplateDeclSpecialization(Child, DumpExplicitInst,
                                   !D->isCanonicalDecl());
}

void ASTDumper::VisitFunctionTemplateDecl(const FunctionTemplateDecl *D) {
  // FIXME: We don't add a declaration of a function template specialization
  // to its context when it's explicitly instantiated, so dump explicit
  // instantiations when we dump the template itself.
  dumpTemplateDecl(D, true);
}

void ASTDumper::VisitClassTemplateDecl(const ClassTemplateDecl *D) {
  dumpTemplateDecl(D, false);
}

void ASTDumper::VisitClassTemplateSpecializationDecl(
    const ClassTemplateSpecializationDecl *D) {
  VisitCXXRecordDecl(D);
  dumpTemplateArgumentList(D->getTemplateArgs());
}

void ASTDumper::VisitClassTemplatePartialSpecializationDecl(
    const ClassTemplatePartialSpecializationDecl *D) {
  VisitClassTemplateSpecializationDecl(D);
  dumpTemplateParameters(D->getTemplateParameters());
}

void ASTDumper::VisitClassScopeFunctionSpecializationDecl(
    const ClassScopeFunctionSpecializationDecl *D) {
  dumpDecl(D->getSpecialization());
  if (D->hasExplicitTemplateArgs())
    dumpTemplateArgumentListInfo(D->templateArgs());
}

void ASTDumper::VisitVarTemplateDecl(const VarTemplateDecl *D) {
  dumpTemplateDecl(D, false);
}

void ASTDumper::VisitBuiltinTemplateDecl(const BuiltinTemplateDecl *D) {
  NodeDumper.dumpName(D);
  dumpTemplateParameters(D->getTemplateParameters());
}

void ASTDumper::VisitVarTemplateSpecializationDecl(
    const VarTemplateSpecializationDecl *D) {
  dumpTemplateArgumentList(D->getTemplateArgs());
  VisitVarDecl(D);
}

void ASTDumper::VisitVarTemplatePartialSpecializationDecl(
    const VarTemplatePartialSpecializationDecl *D) {
  dumpTemplateParameters(D->getTemplateParameters());
  VisitVarTemplateSpecializationDecl(D);
}

void ASTDumper::VisitTemplateTypeParmDecl(const TemplateTypeParmDecl *D) {
  if (D->wasDeclaredWithTypename())
    OS << " typename";
  else
    OS << " class";
  OS << " depth " << D->getDepth() << " index " << D->getIndex();
  if (D->isParameterPack())
    OS << " ...";
  NodeDumper.dumpName(D);
  if (D->hasDefaultArgument())
    dumpTemplateArgument(D->getDefaultArgument(), SourceRange(),
                         D->getDefaultArgStorage().getInheritedFrom(),
                         D->defaultArgumentWasInherited() ? "inherited from"
                                                          : "previous");
}

void ASTDumper::VisitNonTypeTemplateParmDecl(const NonTypeTemplateParmDecl *D) {
  NodeDumper.dumpType(D->getType());
  OS << " depth " << D->getDepth() << " index " << D->getIndex();
  if (D->isParameterPack())
    OS << " ...";
  NodeDumper.dumpName(D);
  if (D->hasDefaultArgument())
    dumpTemplateArgument(D->getDefaultArgument(), SourceRange(),
                         D->getDefaultArgStorage().getInheritedFrom(),
                         D->defaultArgumentWasInherited() ? "inherited from"
                                                          : "previous");
}

void ASTDumper::VisitTemplateTemplateParmDecl(
    const TemplateTemplateParmDecl *D) {
  OS << " depth " << D->getDepth() << " index " << D->getIndex();
  if (D->isParameterPack())
    OS << " ...";
  NodeDumper.dumpName(D);
  dumpTemplateParameters(D->getTemplateParameters());
  if (D->hasDefaultArgument())
    dumpTemplateArgumentLoc(
        D->getDefaultArgument(), D->getDefaultArgStorage().getInheritedFrom(),
        D->defaultArgumentWasInherited() ? "inherited from" : "previous");
}

void ASTDumper::VisitUsingDecl(const UsingDecl *D) {
  OS << ' ';
  if (D->getQualifier())
    D->getQualifier()->print(OS, D->getASTContext().getPrintingPolicy());
  OS << D->getNameAsString();
}

void ASTDumper::VisitUnresolvedUsingTypenameDecl(
    const UnresolvedUsingTypenameDecl *D) {
  OS << ' ';
  if (D->getQualifier())
    D->getQualifier()->print(OS, D->getASTContext().getPrintingPolicy());
  OS << D->getNameAsString();
}

void ASTDumper::VisitUnresolvedUsingValueDecl(const UnresolvedUsingValueDecl *D) {
  OS << ' ';
  if (D->getQualifier())
    D->getQualifier()->print(OS, D->getASTContext().getPrintingPolicy());
  OS << D->getNameAsString();
  NodeDumper.dumpType(D->getType());
}

void ASTDumper::VisitUsingShadowDecl(const UsingShadowDecl *D) {
  OS << ' ';
  NodeDumper.dumpBareDeclRef(D->getTargetDecl());
  if (auto *TD = dyn_cast<TypeDecl>(D->getUnderlyingDecl()))
    dumpTypeAsChild(TD->getTypeForDecl());
}

void ASTDumper::VisitConstructorUsingShadowDecl(
    const ConstructorUsingShadowDecl *D) {
  if (D->constructsVirtualBase())
    OS << " virtual";

  dumpChild([=] {
    OS << "target ";
    NodeDumper.dumpBareDeclRef(D->getTargetDecl());
  });

  dumpChild([=] {
    OS << "nominated ";
    NodeDumper.dumpBareDeclRef(D->getNominatedBaseClass());
    OS << ' ';
    NodeDumper.dumpBareDeclRef(D->getNominatedBaseClassShadowDecl());
  });

  dumpChild([=] {
    OS << "constructed ";
    NodeDumper.dumpBareDeclRef(D->getConstructedBaseClass());
    OS << ' ';
    NodeDumper.dumpBareDeclRef(D->getConstructedBaseClassShadowDecl());
  });
}

void ASTDumper::VisitLinkageSpecDecl(const LinkageSpecDecl *D) {
  switch (D->getLanguage()) {
  case LinkageSpecDecl::lang_c: OS << " C"; break;
  case LinkageSpecDecl::lang_cxx: OS << " C++"; break;
  }
}

void ASTDumper::VisitAccessSpecDecl(const AccessSpecDecl *D) {
  OS << ' ';
  NodeDumper.dumpAccessSpecifier(D->getAccess());
}

void ASTDumper::VisitFriendDecl(const FriendDecl *D) {
  if (TypeSourceInfo *T = D->getFriendType())
    NodeDumper.dumpType(T->getType());
  else
    dumpDecl(D->getFriendDecl());
}

//===----------------------------------------------------------------------===//
// Obj-C Declarations
//===----------------------------------------------------------------------===//

void ASTDumper::VisitObjCIvarDecl(const ObjCIvarDecl *D) {
  NodeDumper.dumpName(D);
  NodeDumper.dumpType(D->getType());
  if (D->getSynthesize())
    OS << " synthesize";

  switch (D->getAccessControl()) {
  case ObjCIvarDecl::None:
    OS << " none";
    break;
  case ObjCIvarDecl::Private:
    OS << " private";
    break;
  case ObjCIvarDecl::Protected:
    OS << " protected";
    break;
  case ObjCIvarDecl::Public:
    OS << " public";
    break;
  case ObjCIvarDecl::Package:
    OS << " package";
    break;
  }
}

void ASTDumper::VisitObjCMethodDecl(const ObjCMethodDecl *D) {
  if (D->isInstanceMethod())
    OS << " -";
  else
    OS << " +";
  NodeDumper.dumpName(D);
  NodeDumper.dumpType(D->getReturnType());

  if (D->isThisDeclarationADefinition()) {
    dumpDeclContext(D);
  } else {
    for (const ParmVarDecl *Parameter : D->parameters())
      dumpDecl(Parameter);
  }

  if (D->isVariadic())
    dumpChild([=] { OS << "..."; });

  if (D->hasBody())
    dumpStmt(D->getBody());
}

void ASTDumper::VisitObjCTypeParamDecl(const ObjCTypeParamDecl *D) {
  NodeDumper.dumpName(D);
  switch (D->getVariance()) {
  case ObjCTypeParamVariance::Invariant:
    break;

  case ObjCTypeParamVariance::Covariant:
    OS << " covariant";
    break;

  case ObjCTypeParamVariance::Contravariant:
    OS << " contravariant";
    break;
  }

  if (D->hasExplicitBound())
    OS << " bounded";
  NodeDumper.dumpType(D->getUnderlyingType());
}

void ASTDumper::VisitObjCCategoryDecl(const ObjCCategoryDecl *D) {
  NodeDumper.dumpName(D);
  NodeDumper.dumpDeclRef(D->getClassInterface());
  NodeDumper.dumpDeclRef(D->getImplementation());
  for (ObjCCategoryDecl::protocol_iterator I = D->protocol_begin(),
                                           E = D->protocol_end();
       I != E; ++I)
    NodeDumper.dumpDeclRef(*I);
  dumpObjCTypeParamList(D->getTypeParamList());
}

void ASTDumper::VisitObjCCategoryImplDecl(const ObjCCategoryImplDecl *D) {
  NodeDumper.dumpName(D);
  NodeDumper.dumpDeclRef(D->getClassInterface());
  NodeDumper.dumpDeclRef(D->getCategoryDecl());
}

void ASTDumper::VisitObjCProtocolDecl(const ObjCProtocolDecl *D) {
  NodeDumper.dumpName(D);

  for (auto *Child : D->protocols())
    NodeDumper.dumpDeclRef(Child);
}

void ASTDumper::VisitObjCInterfaceDecl(const ObjCInterfaceDecl *D) {
  NodeDumper.dumpName(D);
  NodeDumper.dumpDeclRef(D->getSuperClass(), "super");

  NodeDumper.dumpDeclRef(D->getImplementation());
  for (auto *Child : D->protocols())
    NodeDumper.dumpDeclRef(Child);
  dumpObjCTypeParamList(D->getTypeParamListAsWritten());
}

void ASTDumper::VisitObjCImplementationDecl(const ObjCImplementationDecl *D) {
  NodeDumper.dumpName(D);
  NodeDumper.dumpDeclRef(D->getSuperClass(), "super");
  NodeDumper.dumpDeclRef(D->getClassInterface());
  for (ObjCImplementationDecl::init_const_iterator I = D->init_begin(),
                                                   E = D->init_end();
       I != E; ++I)
    dumpCXXCtorInitializer(*I);
}

void ASTDumper::VisitObjCCompatibleAliasDecl(const ObjCCompatibleAliasDecl *D) {
  NodeDumper.dumpName(D);
  NodeDumper.dumpDeclRef(D->getClassInterface());
}

void ASTDumper::VisitObjCPropertyDecl(const ObjCPropertyDecl *D) {
  NodeDumper.dumpName(D);
  NodeDumper.dumpType(D->getType());

  if (D->getPropertyImplementation() == ObjCPropertyDecl::Required)
    OS << " required";
  else if (D->getPropertyImplementation() == ObjCPropertyDecl::Optional)
    OS << " optional";

  ObjCPropertyDecl::PropertyAttributeKind Attrs = D->getPropertyAttributes();
  if (Attrs != ObjCPropertyDecl::OBJC_PR_noattr) {
    if (Attrs & ObjCPropertyDecl::OBJC_PR_readonly)
      OS << " readonly";
    if (Attrs & ObjCPropertyDecl::OBJC_PR_assign)
      OS << " assign";
    if (Attrs & ObjCPropertyDecl::OBJC_PR_readwrite)
      OS << " readwrite";
    if (Attrs & ObjCPropertyDecl::OBJC_PR_retain)
      OS << " retain";
    if (Attrs & ObjCPropertyDecl::OBJC_PR_copy)
      OS << " copy";
    if (Attrs & ObjCPropertyDecl::OBJC_PR_nonatomic)
      OS << " nonatomic";
    if (Attrs & ObjCPropertyDecl::OBJC_PR_atomic)
      OS << " atomic";
    if (Attrs & ObjCPropertyDecl::OBJC_PR_weak)
      OS << " weak";
    if (Attrs & ObjCPropertyDecl::OBJC_PR_strong)
      OS << " strong";
    if (Attrs & ObjCPropertyDecl::OBJC_PR_unsafe_unretained)
      OS << " unsafe_unretained";
    if (Attrs & ObjCPropertyDecl::OBJC_PR_class)
      OS << " class";
    if (Attrs & ObjCPropertyDecl::OBJC_PR_getter)
      NodeDumper.dumpDeclRef(D->getGetterMethodDecl(), "getter");
    if (Attrs & ObjCPropertyDecl::OBJC_PR_setter)
      NodeDumper.dumpDeclRef(D->getSetterMethodDecl(), "setter");
  }
}

void ASTDumper::VisitObjCPropertyImplDecl(const ObjCPropertyImplDecl *D) {
  NodeDumper.dumpName(D->getPropertyDecl());
  if (D->getPropertyImplementation() == ObjCPropertyImplDecl::Synthesize)
    OS << " synthesize";
  else
    OS << " dynamic";
  NodeDumper.dumpDeclRef(D->getPropertyDecl());
  NodeDumper.dumpDeclRef(D->getPropertyIvarDecl());
}

void ASTDumper::Visit(const BlockDecl::Capture &C) {
  dumpChild([=] {
    NodeDumper.Visit(C);
    if (C.hasCopyExpr())
      dumpStmt(C.getCopyExpr());
  });
}

void ASTDumper::VisitBlockDecl(const BlockDecl *D) {
  for (auto I : D->parameters())
    dumpDecl(I);

  if (D->isVariadic())
    dumpChild([=]{ OS << "..."; });

  if (D->capturesCXXThis())
    dumpChild([=]{ OS << "capture this"; });

  for (const auto &I : D->captures())
    Visit(I);
  dumpStmt(D->getBody());
}

//===----------------------------------------------------------------------===//
//  Stmt dumping methods.
//===----------------------------------------------------------------------===//

void ASTDumper::dumpStmt(const Stmt *S, StringRef Label) {
  dumpChild(Label, [=] {
    NodeDumper.Visit(S);

    if (!S) {
      return;
    }

    ConstStmtVisitor<ASTDumper>::Visit(S);

    // Some statements have custom mechanisms for dumping their children.
    if (isa<DeclStmt>(S) || isa<GenericSelectionExpr>(S)) {
      return;
    }

    for (const Stmt *SubStmt : S->children())
      dumpStmt(SubStmt);
  });
}

void ASTDumper::VisitDeclStmt(const DeclStmt *Node) {
  for (DeclStmt::const_decl_iterator I = Node->decl_begin(),
                                     E = Node->decl_end();
       I != E; ++I)
    dumpDecl(*I);
}

void ASTDumper::VisitAttributedStmt(const AttributedStmt *Node) {
  for (ArrayRef<const Attr *>::iterator I = Node->getAttrs().begin(),
                                        E = Node->getAttrs().end();
       I != E; ++I)
    dumpAttr(*I);
}

void ASTDumper::VisitCXXCatchStmt(const CXXCatchStmt *Node) {
  dumpDecl(Node->getExceptionDecl());
}

void ASTDumper::VisitCapturedStmt(const CapturedStmt *Node) {
  dumpDecl(Node->getCapturedDecl());
}

//===----------------------------------------------------------------------===//
//  OpenMP dumping methods.
//===----------------------------------------------------------------------===//

void ASTDumper::Visit(const OMPClause *C) {
  dumpChild([=] {
    NodeDumper.Visit(C);
    for (auto *S : C->children())
      dumpStmt(S);
  });
}

void ASTDumper::VisitOMPExecutableDirective(
    const OMPExecutableDirective *Node) {
  for (const auto *C : Node->clauses())
    Visit(C);
}

//===----------------------------------------------------------------------===//
//  Expr dumping methods.
//===----------------------------------------------------------------------===//


void ASTDumper::VisitInitListExpr(const InitListExpr *ILE) {
  if (auto *Filler = ILE->getArrayFiller()) {
    dumpStmt(Filler, "array_filler");
  }
}

void ASTDumper::VisitBlockExpr(const BlockExpr *Node) {
  dumpDecl(Node->getBlockDecl());
}

void ASTDumper::VisitOpaqueValueExpr(const OpaqueValueExpr *Node) {
  if (Expr *Source = Node->getSourceExpr())
    dumpStmt(Source);
}

void ASTDumper::VisitGenericSelectionExpr(const GenericSelectionExpr *E) {
  if (E->isResultDependent())
    OS << " result_dependent";
  dumpStmt(E->getControllingExpr());
  dumpTypeAsChild(E->getControllingExpr()->getType()); // FIXME: remove

  for (unsigned I = 0, N = E->getNumAssocs(); I != N; ++I) {
    dumpChild([=] {
      if (const TypeSourceInfo *TSI = E->getAssocTypeSourceInfo(I)) {
        OS << "case ";
        NodeDumper.dumpType(TSI->getType());
      } else {
        OS << "default";
      }

      if (!E->isResultDependent() && E->getResultIndex() == I)
        OS << " selected";

      if (const TypeSourceInfo *TSI = E->getAssocTypeSourceInfo(I))
        dumpTypeAsChild(TSI->getType());
      dumpStmt(E->getAssocExpr(I));
    });
  }
}

//===----------------------------------------------------------------------===//
// C++ Expressions
//===----------------------------------------------------------------------===//

void ASTDumper::VisitSizeOfPackExpr(const SizeOfPackExpr *Node) {
  if (Node->isPartiallySubstituted())
    for (const auto &A : Node->getPartialArguments())
      dumpTemplateArgument(A);
}

//===----------------------------------------------------------------------===//
// Obj-C Expressions
//===----------------------------------------------------------------------===//

void ASTDumper::VisitObjCAtCatchStmt(const ObjCAtCatchStmt *Node) {
  if (const VarDecl *CatchParam = Node->getCatchParamDecl())
    dumpDecl(CatchParam);
}

//===----------------------------------------------------------------------===//
// Comments
//===----------------------------------------------------------------------===//

void ASTDumper::dumpComment(const Comment *C, const FullComment *FC) {
  dumpChild([=] {
    NodeDumper.Visit(C, FC);
    if (!C) {
      return;
    }
    ConstCommentVisitor<ASTDumper, void, const FullComment *>::visit(C, FC);
    for (Comment::child_iterator I = C->child_begin(), E = C->child_end();
         I != E; ++I)
      dumpComment(*I, FC);
  });
}

//===----------------------------------------------------------------------===//
// Type method implementations
//===----------------------------------------------------------------------===//

void QualType::dump(const char *msg) const {
  if (msg)
    llvm::errs() << msg << ": ";
  dump();
}

LLVM_DUMP_METHOD void QualType::dump() const { dump(llvm::errs()); }

LLVM_DUMP_METHOD void QualType::dump(llvm::raw_ostream &OS) const {
  ASTDumper Dumper(OS, nullptr, nullptr);
  Dumper.dumpTypeAsChild(*this);
}

LLVM_DUMP_METHOD void Type::dump() const { dump(llvm::errs()); }

LLVM_DUMP_METHOD void Type::dump(llvm::raw_ostream &OS) const {
  QualType(this, 0).dump(OS);
}

//===----------------------------------------------------------------------===//
// Decl method implementations
//===----------------------------------------------------------------------===//

LLVM_DUMP_METHOD void Decl::dump() const { dump(llvm::errs()); }

LLVM_DUMP_METHOD void Decl::dump(raw_ostream &OS, bool Deserialize) const {
  const ASTContext &Ctx = getASTContext();
  const SourceManager &SM = Ctx.getSourceManager();
  ASTDumper P(OS, &Ctx.getCommentCommandTraits(), &SM,
              SM.getDiagnostics().getShowColors(), Ctx.getPrintingPolicy());
  P.setDeserialize(Deserialize);
  P.dumpDecl(this);
}

LLVM_DUMP_METHOD void Decl::dumpColor() const {
  const ASTContext &Ctx = getASTContext();
  ASTDumper P(llvm::errs(), &Ctx.getCommentCommandTraits(),
              &Ctx.getSourceManager(), /*ShowColors*/ true,
              Ctx.getPrintingPolicy());
  P.dumpDecl(this);
}

LLVM_DUMP_METHOD void DeclContext::dumpLookups() const {
  dumpLookups(llvm::errs());
}

LLVM_DUMP_METHOD void DeclContext::dumpLookups(raw_ostream &OS,
                                               bool DumpDecls,
                                               bool Deserialize) const {
  const DeclContext *DC = this;
  while (!DC->isTranslationUnit())
    DC = DC->getParent();
  ASTContext &Ctx = cast<TranslationUnitDecl>(DC)->getASTContext();
  const SourceManager &SM = Ctx.getSourceManager();
  ASTDumper P(OS, &Ctx.getCommentCommandTraits(), &Ctx.getSourceManager(),
              SM.getDiagnostics().getShowColors(), Ctx.getPrintingPolicy());
  P.setDeserialize(Deserialize);
  P.dumpLookups(this, DumpDecls);
}

//===----------------------------------------------------------------------===//
// Stmt method implementations
//===----------------------------------------------------------------------===//

LLVM_DUMP_METHOD void Stmt::dump(SourceManager &SM) const {
  dump(llvm::errs(), SM);
}

LLVM_DUMP_METHOD void Stmt::dump(raw_ostream &OS, SourceManager &SM) const {
  ASTDumper P(OS, nullptr, &SM);
  P.dumpStmt(this);
}

LLVM_DUMP_METHOD void Stmt::dump(raw_ostream &OS) const {
  ASTDumper P(OS, nullptr, nullptr);
  P.dumpStmt(this);
}

LLVM_DUMP_METHOD void Stmt::dump() const {
  ASTDumper P(llvm::errs(), nullptr, nullptr);
  P.dumpStmt(this);
}

LLVM_DUMP_METHOD void Stmt::dumpColor() const {
  ASTDumper P(llvm::errs(), nullptr, nullptr, /*ShowColors*/true);
  P.dumpStmt(this);
}

//===----------------------------------------------------------------------===//
// Comment method implementations
//===----------------------------------------------------------------------===//

LLVM_DUMP_METHOD void Comment::dump() const {
  dump(llvm::errs(), nullptr, nullptr);
}

LLVM_DUMP_METHOD void Comment::dump(const ASTContext &Context) const {
  dump(llvm::errs(), &Context.getCommentCommandTraits(),
       &Context.getSourceManager());
}

void Comment::dump(raw_ostream &OS, const CommandTraits *Traits,
                   const SourceManager *SM) const {
  const FullComment *FC = dyn_cast<FullComment>(this);
  if (!FC)
    return;
  ASTDumper D(OS, Traits, SM);
  D.dumpComment(FC, FC);
}

LLVM_DUMP_METHOD void Comment::dumpColor() const {
  const FullComment *FC = dyn_cast<FullComment>(this);
  if (!FC)
    return;
  ASTDumper D(llvm::errs(), nullptr, nullptr, /*ShowColors*/true);
  D.dumpComment(FC, FC);
}
