//===--- DeclPrinter.cpp - Printing implementation for Decl ASTs ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Decl::print method, which pretty prints the
// AST back out to C/Objective-C/C++/Objective-C++ code.
//
//===----------------------------------------------------------------------===//
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclVisitor.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/Basic/Module.h"
#include "llvm/Support/raw_ostream.h"
using namespace clang;

namespace {
  class DeclPrinter : public DeclVisitor<DeclPrinter> {
    raw_ostream &Out;
    PrintingPolicy Policy;
    const ASTContext &Context;
    unsigned Indentation;
    bool PrintInstantiation;

    raw_ostream& Indent() { return Indent(Indentation); }
    raw_ostream& Indent(unsigned Indentation);
    void ProcessDeclGroup(SmallVectorImpl<Decl*>& Decls);

    void Print(AccessSpecifier AS);
    void PrintConstructorInitializers(CXXConstructorDecl *CDecl,
                                      std::string &Proto);

    /// Print an Objective-C method type in parentheses.
    ///
    /// \param Quals The Objective-C declaration qualifiers.
    /// \param T The type to print.
    void PrintObjCMethodType(ASTContext &Ctx, Decl::ObjCDeclQualifier Quals,
                             QualType T);

    void PrintObjCTypeParams(ObjCTypeParamList *Params);

  public:
    DeclPrinter(raw_ostream &Out, const PrintingPolicy &Policy,
                const ASTContext &Context, unsigned Indentation = 0,
                bool PrintInstantiation = false)
        : Out(Out), Policy(Policy), Context(Context), Indentation(Indentation),
          PrintInstantiation(PrintInstantiation) {}

    void VisitDeclContext(DeclContext *DC, bool Indent = true);

    void VisitTranslationUnitDecl(TranslationUnitDecl *D);
    void VisitTypedefDecl(TypedefDecl *D);
    void VisitTypeAliasDecl(TypeAliasDecl *D);
    void VisitEnumDecl(EnumDecl *D);
    void VisitRecordDecl(RecordDecl *D);
    void VisitEnumConstantDecl(EnumConstantDecl *D);
    void VisitEmptyDecl(EmptyDecl *D);
    void VisitFunctionDecl(FunctionDecl *D);
    void VisitFriendDecl(FriendDecl *D);
    void VisitFieldDecl(FieldDecl *D);
    void VisitVarDecl(VarDecl *D);
    void VisitLabelDecl(LabelDecl *D);
    void VisitParmVarDecl(ParmVarDecl *D);
    void VisitFileScopeAsmDecl(FileScopeAsmDecl *D);
    void VisitImportDecl(ImportDecl *D);
    void VisitStaticAssertDecl(StaticAssertDecl *D);
    void VisitNamespaceDecl(NamespaceDecl *D);
    void VisitUsingDirectiveDecl(UsingDirectiveDecl *D);
    void VisitNamespaceAliasDecl(NamespaceAliasDecl *D);
    void VisitCXXRecordDecl(CXXRecordDecl *D);
    void VisitLinkageSpecDecl(LinkageSpecDecl *D);
    void VisitTemplateDecl(const TemplateDecl *D);
    void VisitFunctionTemplateDecl(FunctionTemplateDecl *D);
    void VisitClassTemplateDecl(ClassTemplateDecl *D);
    void VisitClassTemplateSpecializationDecl(
                                            ClassTemplateSpecializationDecl *D);
    void VisitClassTemplatePartialSpecializationDecl(
                                     ClassTemplatePartialSpecializationDecl *D);
    void VisitObjCMethodDecl(ObjCMethodDecl *D);
    void VisitObjCImplementationDecl(ObjCImplementationDecl *D);
    void VisitObjCInterfaceDecl(ObjCInterfaceDecl *D);
    void VisitObjCProtocolDecl(ObjCProtocolDecl *D);
    void VisitObjCCategoryImplDecl(ObjCCategoryImplDecl *D);
    void VisitObjCCategoryDecl(ObjCCategoryDecl *D);
    void VisitObjCCompatibleAliasDecl(ObjCCompatibleAliasDecl *D);
    void VisitObjCPropertyDecl(ObjCPropertyDecl *D);
    void VisitObjCPropertyImplDecl(ObjCPropertyImplDecl *D);
    void VisitUnresolvedUsingTypenameDecl(UnresolvedUsingTypenameDecl *D);
    void VisitUnresolvedUsingValueDecl(UnresolvedUsingValueDecl *D);
    void VisitUsingDecl(UsingDecl *D);
    void VisitUsingShadowDecl(UsingShadowDecl *D);
    void VisitOMPThreadPrivateDecl(OMPThreadPrivateDecl *D);
    void VisitOMPRequiresDecl(OMPRequiresDecl *D);
    void VisitOMPDeclareReductionDecl(OMPDeclareReductionDecl *D);
    void VisitOMPCapturedExprDecl(OMPCapturedExprDecl *D);

    void printTemplateParameters(const TemplateParameterList *Params);
    void printTemplateArguments(const TemplateArgumentList &Args,
                                const TemplateParameterList *Params = nullptr);
    void prettyPrintAttributes(Decl *D);
    void prettyPrintPragmas(Decl *D);
    void printDeclType(QualType T, StringRef DeclName, bool Pack = false);
  };
}

void Decl::print(raw_ostream &Out, unsigned Indentation,
                 bool PrintInstantiation) const {
  print(Out, getASTContext().getPrintingPolicy(), Indentation, PrintInstantiation);
}

void Decl::print(raw_ostream &Out, const PrintingPolicy &Policy,
                 unsigned Indentation, bool PrintInstantiation) const {
  DeclPrinter Printer(Out, Policy, getASTContext(), Indentation,
                      PrintInstantiation);
  Printer.Visit(const_cast<Decl*>(this));
}

static QualType GetBaseType(QualType T) {
  // FIXME: This should be on the Type class!
  QualType BaseType = T;
  while (!BaseType->isSpecifierType()) {
    if (const PointerType *PTy = BaseType->getAs<PointerType>())
      BaseType = PTy->getPointeeType();
    else if (const BlockPointerType *BPy = BaseType->getAs<BlockPointerType>())
      BaseType = BPy->getPointeeType();
    else if (const ArrayType* ATy = dyn_cast<ArrayType>(BaseType))
      BaseType = ATy->getElementType();
    else if (const FunctionType* FTy = BaseType->getAs<FunctionType>())
      BaseType = FTy->getReturnType();
    else if (const VectorType *VTy = BaseType->getAs<VectorType>())
      BaseType = VTy->getElementType();
    else if (const ReferenceType *RTy = BaseType->getAs<ReferenceType>())
      BaseType = RTy->getPointeeType();
    else if (const AutoType *ATy = BaseType->getAs<AutoType>())
      BaseType = ATy->getDeducedType();
    else if (const ParenType *PTy = BaseType->getAs<ParenType>())
      BaseType = PTy->desugar();
    else
      // This must be a syntax error.
      break;
  }
  return BaseType;
}

static QualType getDeclType(Decl* D) {
  if (TypedefNameDecl* TDD = dyn_cast<TypedefNameDecl>(D))
    return TDD->getUnderlyingType();
  if (ValueDecl* VD = dyn_cast<ValueDecl>(D))
    return VD->getType();
  return QualType();
}

void Decl::printGroup(Decl** Begin, unsigned NumDecls,
                      raw_ostream &Out, const PrintingPolicy &Policy,
                      unsigned Indentation) {
  if (NumDecls == 1) {
    (*Begin)->print(Out, Policy, Indentation);
    return;
  }

  Decl** End = Begin + NumDecls;
  TagDecl* TD = dyn_cast<TagDecl>(*Begin);
  if (TD)
    ++Begin;

  PrintingPolicy SubPolicy(Policy);

  bool isFirst = true;
  for ( ; Begin != End; ++Begin) {
    if (isFirst) {
      if(TD)
        SubPolicy.IncludeTagDefinition = true;
      SubPolicy.SuppressSpecifiers = false;
      isFirst = false;
    } else {
      if (!isFirst) Out << ", ";
      SubPolicy.IncludeTagDefinition = false;
      SubPolicy.SuppressSpecifiers = true;
    }

    (*Begin)->print(Out, SubPolicy, Indentation);
  }
}

LLVM_DUMP_METHOD void DeclContext::dumpDeclContext() const {
  // Get the translation unit
  const DeclContext *DC = this;
  while (!DC->isTranslationUnit())
    DC = DC->getParent();

  ASTContext &Ctx = cast<TranslationUnitDecl>(DC)->getASTContext();
  DeclPrinter Printer(llvm::errs(), Ctx.getPrintingPolicy(), Ctx, 0);
  Printer.VisitDeclContext(const_cast<DeclContext *>(this), /*Indent=*/false);
}

raw_ostream& DeclPrinter::Indent(unsigned Indentation) {
  for (unsigned i = 0; i != Indentation; ++i)
    Out << "  ";
  return Out;
}

void DeclPrinter::prettyPrintAttributes(Decl *D) {
  if (Policy.PolishForDeclaration)
    return;

  if (D->hasAttrs()) {
    AttrVec &Attrs = D->getAttrs();
    for (auto *A : Attrs) {
      if (A->isInherited() || A->isImplicit())
        continue;
      switch (A->getKind()) {
#define ATTR(X)
#define PRAGMA_SPELLING_ATTR(X) case attr::X:
#include "clang/Basic/AttrList.inc"
        break;
      default:
        A->printPretty(Out, Policy);
        break;
      }
    }
  }
}

void DeclPrinter::prettyPrintPragmas(Decl *D) {
  if (Policy.PolishForDeclaration)
    return;

  if (D->hasAttrs()) {
    AttrVec &Attrs = D->getAttrs();
    for (auto *A : Attrs) {
      switch (A->getKind()) {
#define ATTR(X)
#define PRAGMA_SPELLING_ATTR(X) case attr::X:
#include "clang/Basic/AttrList.inc"
        A->printPretty(Out, Policy);
        Indent();
        break;
      default:
        break;
      }
    }
  }
}

void DeclPrinter::printDeclType(QualType T, StringRef DeclName, bool Pack) {
  // Normally, a PackExpansionType is written as T[3]... (for instance, as a
  // template argument), but if it is the type of a declaration, the ellipsis
  // is placed before the name being declared.
  if (auto *PET = T->getAs<PackExpansionType>()) {
    Pack = true;
    T = PET->getPattern();
  }
  T.print(Out, Policy, (Pack ? "..." : "") + DeclName, Indentation);
}

void DeclPrinter::ProcessDeclGroup(SmallVectorImpl<Decl*>& Decls) {
  this->Indent();
  Decl::printGroup(Decls.data(), Decls.size(), Out, Policy, Indentation);
  Out << ";\n";
  Decls.clear();

}

void DeclPrinter::Print(AccessSpecifier AS) {
  switch(AS) {
  case AS_none:      llvm_unreachable("No access specifier!");
  case AS_public:    Out << "public"; break;
  case AS_protected: Out << "protected"; break;
  case AS_private:   Out << "private"; break;
  }
}

void DeclPrinter::PrintConstructorInitializers(CXXConstructorDecl *CDecl,
                                               std::string &Proto) {
  bool HasInitializerList = false;
  for (const auto *BMInitializer : CDecl->inits()) {
    if (BMInitializer->isInClassMemberInitializer())
      continue;

    if (!HasInitializerList) {
      Proto += " : ";
      Out << Proto;
      Proto.clear();
      HasInitializerList = true;
    } else
      Out << ", ";

    if (BMInitializer->isAnyMemberInitializer()) {
      FieldDecl *FD = BMInitializer->getAnyMember();
      Out << *FD;
    } else {
      Out << QualType(BMInitializer->getBaseClass(), 0).getAsString(Policy);
    }

    Out << "(";
    if (!BMInitializer->getInit()) {
      // Nothing to print
    } else {
      Expr *Init = BMInitializer->getInit();
      if (ExprWithCleanups *Tmp = dyn_cast<ExprWithCleanups>(Init))
        Init = Tmp->getSubExpr();

      Init = Init->IgnoreParens();

      Expr *SimpleInit = nullptr;
      Expr **Args = nullptr;
      unsigned NumArgs = 0;
      if (ParenListExpr *ParenList = dyn_cast<ParenListExpr>(Init)) {
        Args = ParenList->getExprs();
        NumArgs = ParenList->getNumExprs();
      } else if (CXXConstructExpr *Construct =
                     dyn_cast<CXXConstructExpr>(Init)) {
        Args = Construct->getArgs();
        NumArgs = Construct->getNumArgs();
      } else
        SimpleInit = Init;

      if (SimpleInit)
        SimpleInit->printPretty(Out, nullptr, Policy, Indentation);
      else {
        for (unsigned I = 0; I != NumArgs; ++I) {
          assert(Args[I] != nullptr && "Expected non-null Expr");
          if (isa<CXXDefaultArgExpr>(Args[I]))
            break;

          if (I)
            Out << ", ";
          Args[I]->printPretty(Out, nullptr, Policy, Indentation);
        }
      }
    }
    Out << ")";
    if (BMInitializer->isPackExpansion())
      Out << "...";
  }
}

//----------------------------------------------------------------------------
// Common C declarations
//----------------------------------------------------------------------------

void DeclPrinter::VisitDeclContext(DeclContext *DC, bool Indent) {
  if (Policy.TerseOutput)
    return;

  if (Indent)
    Indentation += Policy.Indentation;

  SmallVector<Decl*, 2> Decls;
  for (DeclContext::decl_iterator D = DC->decls_begin(), DEnd = DC->decls_end();
       D != DEnd; ++D) {

    // Don't print ObjCIvarDecls, as they are printed when visiting the
    // containing ObjCInterfaceDecl.
    if (isa<ObjCIvarDecl>(*D))
      continue;

    // Skip over implicit declarations in pretty-printing mode.
    if (D->isImplicit())
      continue;

    // Don't print implicit specializations, as they are printed when visiting
    // corresponding templates.
    if (auto FD = dyn_cast<FunctionDecl>(*D))
      if (FD->getTemplateSpecializationKind() == TSK_ImplicitInstantiation &&
          !isa<ClassTemplateSpecializationDecl>(DC))
        continue;

    // The next bits of code handle stuff like "struct {int x;} a,b"; we're
    // forced to merge the declarations because there's no other way to
    // refer to the struct in question.  When that struct is named instead, we
    // also need to merge to avoid splitting off a stand-alone struct
    // declaration that produces the warning ext_no_declarators in some
    // contexts.
    //
    // This limited merging is safe without a bunch of other checks because it
    // only merges declarations directly referring to the tag, not typedefs.
    //
    // Check whether the current declaration should be grouped with a previous
    // non-free-standing tag declaration.
    QualType CurDeclType = getDeclType(*D);
    if (!Decls.empty() && !CurDeclType.isNull()) {
      QualType BaseType = GetBaseType(CurDeclType);
      if (!BaseType.isNull() && isa<ElaboratedType>(BaseType) &&
          cast<ElaboratedType>(BaseType)->getOwnedTagDecl() == Decls[0]) {
        Decls.push_back(*D);
        continue;
      }
    }

    // If we have a merged group waiting to be handled, handle it now.
    if (!Decls.empty())
      ProcessDeclGroup(Decls);

    // If the current declaration is not a free standing declaration, save it
    // so we can merge it with the subsequent declaration(s) using it.
    if (isa<TagDecl>(*D) && !cast<TagDecl>(*D)->isFreeStanding()) {
      Decls.push_back(*D);
      continue;
    }

    if (isa<AccessSpecDecl>(*D)) {
      Indentation -= Policy.Indentation;
      this->Indent();
      Print(D->getAccess());
      Out << ":\n";
      Indentation += Policy.Indentation;
      continue;
    }

    this->Indent();
    Visit(*D);

    // FIXME: Need to be able to tell the DeclPrinter when
    const char *Terminator = nullptr;
    if (isa<OMPThreadPrivateDecl>(*D) || isa<OMPDeclareReductionDecl>(*D) ||
        isa<OMPRequiresDecl>(*D))
      Terminator = nullptr;
    else if (isa<ObjCMethodDecl>(*D) && cast<ObjCMethodDecl>(*D)->hasBody())
      Terminator = nullptr;
    else if (auto FD = dyn_cast<FunctionDecl>(*D)) {
      if (FD->isThisDeclarationADefinition())
        Terminator = nullptr;
      else
        Terminator = ";";
    } else if (auto TD = dyn_cast<FunctionTemplateDecl>(*D)) {
      if (TD->getTemplatedDecl()->isThisDeclarationADefinition())
        Terminator = nullptr;
      else
        Terminator = ";";
    } else if (isa<NamespaceDecl>(*D) || isa<LinkageSpecDecl>(*D) ||
             isa<ObjCImplementationDecl>(*D) ||
             isa<ObjCInterfaceDecl>(*D) ||
             isa<ObjCProtocolDecl>(*D) ||
             isa<ObjCCategoryImplDecl>(*D) ||
             isa<ObjCCategoryDecl>(*D))
      Terminator = nullptr;
    else if (isa<EnumConstantDecl>(*D)) {
      DeclContext::decl_iterator Next = D;
      ++Next;
      if (Next != DEnd)
        Terminator = ",";
    } else
      Terminator = ";";

    if (Terminator)
      Out << Terminator;
    if (!Policy.TerseOutput &&
        ((isa<FunctionDecl>(*D) &&
          cast<FunctionDecl>(*D)->doesThisDeclarationHaveABody()) ||
         (isa<FunctionTemplateDecl>(*D) &&
          cast<FunctionTemplateDecl>(*D)->getTemplatedDecl()->doesThisDeclarationHaveABody())))
      ; // StmtPrinter already added '\n' after CompoundStmt.
    else
      Out << "\n";

    // Declare target attribute is special one, natural spelling for the pragma
    // assumes "ending" construct so print it here.
    if (D->hasAttr<OMPDeclareTargetDeclAttr>())
      Out << "#pragma omp end declare target\n";
  }

  if (!Decls.empty())
    ProcessDeclGroup(Decls);

  if (Indent)
    Indentation -= Policy.Indentation;
}

void DeclPrinter::VisitTranslationUnitDecl(TranslationUnitDecl *D) {
  VisitDeclContext(D, false);
}

void DeclPrinter::VisitTypedefDecl(TypedefDecl *D) {
  if (!Policy.SuppressSpecifiers) {
    Out << "typedef ";

    if (D->isModulePrivate())
      Out << "__module_private__ ";
  }
  QualType Ty = D->getTypeSourceInfo()->getType();
  Ty.print(Out, Policy, D->getName(), Indentation);
  prettyPrintAttributes(D);
}

void DeclPrinter::VisitTypeAliasDecl(TypeAliasDecl *D) {
  Out << "using " << *D;
  prettyPrintAttributes(D);
  Out << " = " << D->getTypeSourceInfo()->getType().getAsString(Policy);
}

void DeclPrinter::VisitEnumDecl(EnumDecl *D) {
  if (!Policy.SuppressSpecifiers && D->isModulePrivate())
    Out << "__module_private__ ";
  Out << "enum";
  if (D->isScoped()) {
    if (D->isScopedUsingClassTag())
      Out << " class";
    else
      Out << " struct";
  }

  prettyPrintAttributes(D);

  Out << ' ' << *D;

  if (D->isFixed() && D->getASTContext().getLangOpts().CPlusPlus11)
    Out << " : " << D->getIntegerType().stream(Policy);

  if (D->isCompleteDefinition()) {
    Out << " {\n";
    VisitDeclContext(D);
    Indent() << "}";
  }
}

void DeclPrinter::VisitRecordDecl(RecordDecl *D) {
  if (!Policy.SuppressSpecifiers && D->isModulePrivate())
    Out << "__module_private__ ";
  Out << D->getKindName();

  prettyPrintAttributes(D);

  if (D->getIdentifier())
    Out << ' ' << *D;

  if (D->isCompleteDefinition()) {
    Out << " {\n";
    VisitDeclContext(D);
    Indent() << "}";
  }
}

void DeclPrinter::VisitEnumConstantDecl(EnumConstantDecl *D) {
  Out << *D;
  prettyPrintAttributes(D);
  if (Expr *Init = D->getInitExpr()) {
    Out << " = ";
    Init->printPretty(Out, nullptr, Policy, Indentation, "\n", &Context);
  }
}

void DeclPrinter::VisitFunctionDecl(FunctionDecl *D) {
  if (!D->getDescribedFunctionTemplate() &&
      !D->isFunctionTemplateSpecialization())
    prettyPrintPragmas(D);

  if (D->isFunctionTemplateSpecialization())
    Out << "template<> ";
  else if (!D->getDescribedFunctionTemplate()) {
    for (unsigned I = 0, NumTemplateParams = D->getNumTemplateParameterLists();
         I < NumTemplateParams; ++I)
      printTemplateParameters(D->getTemplateParameterList(I));
  }

  CXXConstructorDecl *CDecl = dyn_cast<CXXConstructorDecl>(D);
  CXXConversionDecl *ConversionDecl = dyn_cast<CXXConversionDecl>(D);
  CXXDeductionGuideDecl *GuideDecl = dyn_cast<CXXDeductionGuideDecl>(D);
  if (!Policy.SuppressSpecifiers) {
    switch (D->getStorageClass()) {
    case SC_None: break;
    case SC_Extern: Out << "extern "; break;
    case SC_Static: Out << "static "; break;
    case SC_PrivateExtern: Out << "__private_extern__ "; break;
    case SC_Auto: case SC_Register:
      llvm_unreachable("invalid for functions");
    }

    if (D->isInlineSpecified())  Out << "inline ";
    if (D->isVirtualAsWritten()) Out << "virtual ";
    if (D->isModulePrivate())    Out << "__module_private__ ";
    if (D->isConstexpr() && !D->isExplicitlyDefaulted()) Out << "constexpr ";
    if ((CDecl && CDecl->isExplicitSpecified()) ||
        (ConversionDecl && ConversionDecl->isExplicitSpecified()) ||
        (GuideDecl && GuideDecl->isExplicitSpecified()))
      Out << "explicit ";
  }

  PrintingPolicy SubPolicy(Policy);
  SubPolicy.SuppressSpecifiers = false;
  std::string Proto;

  if (Policy.FullyQualifiedName) {
    Proto += D->getQualifiedNameAsString();
  } else {
    if (!Policy.SuppressScope) {
      if (const NestedNameSpecifier *NS = D->getQualifier()) {
        llvm::raw_string_ostream OS(Proto);
        NS->print(OS, Policy);
      }
    }
    Proto += D->getNameInfo().getAsString();
  }

  if (GuideDecl)
    Proto = GuideDecl->getDeducedTemplate()->getDeclName().getAsString();
  if (const TemplateArgumentList *TArgs = D->getTemplateSpecializationArgs()) {
    llvm::raw_string_ostream POut(Proto);
    DeclPrinter TArgPrinter(POut, SubPolicy, Context, Indentation);
    TArgPrinter.printTemplateArguments(*TArgs);
  }

  QualType Ty = D->getType();
  while (const ParenType *PT = dyn_cast<ParenType>(Ty)) {
    Proto = '(' + Proto + ')';
    Ty = PT->getInnerType();
  }

  if (const FunctionType *AFT = Ty->getAs<FunctionType>()) {
    const FunctionProtoType *FT = nullptr;
    if (D->hasWrittenPrototype())
      FT = dyn_cast<FunctionProtoType>(AFT);

    Proto += "(";
    if (FT) {
      llvm::raw_string_ostream POut(Proto);
      DeclPrinter ParamPrinter(POut, SubPolicy, Context, Indentation);
      for (unsigned i = 0, e = D->getNumParams(); i != e; ++i) {
        if (i) POut << ", ";
        ParamPrinter.VisitParmVarDecl(D->getParamDecl(i));
      }

      if (FT->isVariadic()) {
        if (D->getNumParams()) POut << ", ";
        POut << "...";
      }
    } else if (D->doesThisDeclarationHaveABody() && !D->hasPrototype()) {
      for (unsigned i = 0, e = D->getNumParams(); i != e; ++i) {
        if (i)
          Proto += ", ";
        Proto += D->getParamDecl(i)->getNameAsString();
      }
    }

    Proto += ")";

    if (FT) {
      if (FT->isConst())
        Proto += " const";
      if (FT->isVolatile())
        Proto += " volatile";
      if (FT->isRestrict())
        Proto += " restrict";

      switch (FT->getRefQualifier()) {
      case RQ_None:
        break;
      case RQ_LValue:
        Proto += " &";
        break;
      case RQ_RValue:
        Proto += " &&";
        break;
      }
    }

    if (FT && FT->hasDynamicExceptionSpec()) {
      Proto += " throw(";
      if (FT->getExceptionSpecType() == EST_MSAny)
        Proto += "...";
      else
        for (unsigned I = 0, N = FT->getNumExceptions(); I != N; ++I) {
          if (I)
            Proto += ", ";

          Proto += FT->getExceptionType(I).getAsString(SubPolicy);
        }
      Proto += ")";
    } else if (FT && isNoexceptExceptionSpec(FT->getExceptionSpecType())) {
      Proto += " noexcept";
      if (isComputedNoexcept(FT->getExceptionSpecType())) {
        Proto += "(";
        llvm::raw_string_ostream EOut(Proto);
        FT->getNoexceptExpr()->printPretty(EOut, nullptr, SubPolicy,
                                           Indentation);
        EOut.flush();
        Proto += EOut.str();
        Proto += ")";
      }
    }

    if (CDecl) {
      if (!Policy.TerseOutput)
        PrintConstructorInitializers(CDecl, Proto);
    } else if (!ConversionDecl && !isa<CXXDestructorDecl>(D)) {
      if (FT && FT->hasTrailingReturn()) {
        if (!GuideDecl)
          Out << "auto ";
        Out << Proto << " -> ";
        Proto.clear();
      }
      AFT->getReturnType().print(Out, Policy, Proto);
      Proto.clear();
    }
    Out << Proto;
  } else {
    Ty.print(Out, Policy, Proto);
  }

  prettyPrintAttributes(D);

  if (D->isPure())
    Out << " = 0";
  else if (D->isDeletedAsWritten())
    Out << " = delete";
  else if (D->isExplicitlyDefaulted())
    Out << " = default";
  else if (D->doesThisDeclarationHaveABody()) {
    if (!Policy.TerseOutput) {
      if (!D->hasPrototype() && D->getNumParams()) {
        // This is a K&R function definition, so we need to print the
        // parameters.
        Out << '\n';
        DeclPrinter ParamPrinter(Out, SubPolicy, Context, Indentation);
        Indentation += Policy.Indentation;
        for (unsigned i = 0, e = D->getNumParams(); i != e; ++i) {
          Indent();
          ParamPrinter.VisitParmVarDecl(D->getParamDecl(i));
          Out << ";\n";
        }
        Indentation -= Policy.Indentation;
      } else
        Out << ' ';

      if (D->getBody())
        D->getBody()->printPretty(Out, nullptr, SubPolicy, Indentation);
    } else {
      if (!Policy.TerseOutput && isa<CXXConstructorDecl>(*D))
        Out << " {}";
    }
  }
}

void DeclPrinter::VisitFriendDecl(FriendDecl *D) {
  if (TypeSourceInfo *TSI = D->getFriendType()) {
    unsigned NumTPLists = D->getFriendTypeNumTemplateParameterLists();
    for (unsigned i = 0; i < NumTPLists; ++i)
      printTemplateParameters(D->getFriendTypeTemplateParameterList(i));
    Out << "friend ";
    Out << " " << TSI->getType().getAsString(Policy);
  }
  else if (FunctionDecl *FD =
      dyn_cast<FunctionDecl>(D->getFriendDecl())) {
    Out << "friend ";
    VisitFunctionDecl(FD);
  }
  else if (FunctionTemplateDecl *FTD =
           dyn_cast<FunctionTemplateDecl>(D->getFriendDecl())) {
    Out << "friend ";
    VisitFunctionTemplateDecl(FTD);
  }
  else if (ClassTemplateDecl *CTD =
           dyn_cast<ClassTemplateDecl>(D->getFriendDecl())) {
    Out << "friend ";
    VisitRedeclarableTemplateDecl(CTD);
  }
}

void DeclPrinter::VisitFieldDecl(FieldDecl *D) {
  // FIXME: add printing of pragma attributes if required.
  if (!Policy.SuppressSpecifiers && D->isMutable())
    Out << "mutable ";
  if (!Policy.SuppressSpecifiers && D->isModulePrivate())
    Out << "__module_private__ ";

  Out << D->getASTContext().getUnqualifiedObjCPointerType(D->getType()).
         stream(Policy, D->getName(), Indentation);

  if (D->isBitField()) {
    Out << " : ";
    D->getBitWidth()->printPretty(Out, nullptr, Policy, Indentation);
  }

  Expr *Init = D->getInClassInitializer();
  if (!Policy.SuppressInitializers && Init) {
    if (D->getInClassInitStyle() == ICIS_ListInit)
      Out << " ";
    else
      Out << " = ";
    Init->printPretty(Out, nullptr, Policy, Indentation);
  }
  prettyPrintAttributes(D);
}

void DeclPrinter::VisitLabelDecl(LabelDecl *D) {
  Out << *D << ":";
}

void DeclPrinter::VisitVarDecl(VarDecl *D) {
  prettyPrintPragmas(D);

  QualType T = D->getTypeSourceInfo()
    ? D->getTypeSourceInfo()->getType()
    : D->getASTContext().getUnqualifiedObjCPointerType(D->getType());

  if (!Policy.SuppressSpecifiers) {
    StorageClass SC = D->getStorageClass();
    if (SC != SC_None)
      Out << VarDecl::getStorageClassSpecifierString(SC) << " ";

    switch (D->getTSCSpec()) {
    case TSCS_unspecified:
      break;
    case TSCS___thread:
      Out << "__thread ";
      break;
    case TSCS__Thread_local:
      Out << "_Thread_local ";
      break;
    case TSCS_thread_local:
      Out << "thread_local ";
      break;
    }

    if (D->isModulePrivate())
      Out << "__module_private__ ";

    if (D->isConstexpr()) {
      Out << "constexpr ";
      T.removeLocalConst();
    }
  }

  printDeclType(T, D->getName());
  Expr *Init = D->getInit();
  if (!Policy.SuppressInitializers && Init) {
    bool ImplicitInit = false;
    if (CXXConstructExpr *Construct =
            dyn_cast<CXXConstructExpr>(Init->IgnoreImplicit())) {
      if (D->getInitStyle() == VarDecl::CallInit &&
          !Construct->isListInitialization()) {
        ImplicitInit = Construct->getNumArgs() == 0 ||
          Construct->getArg(0)->isDefaultArgument();
      }
    }
    if (!ImplicitInit) {
      if ((D->getInitStyle() == VarDecl::CallInit) && !isa<ParenListExpr>(Init))
        Out << "(";
      else if (D->getInitStyle() == VarDecl::CInit) {
        Out << " = ";
      }
      PrintingPolicy SubPolicy(Policy);
      SubPolicy.SuppressSpecifiers = false;
      SubPolicy.IncludeTagDefinition = false;
      Init->printPretty(Out, nullptr, SubPolicy, Indentation);
      if ((D->getInitStyle() == VarDecl::CallInit) && !isa<ParenListExpr>(Init))
        Out << ")";
    }
  }
  prettyPrintAttributes(D);
}

void DeclPrinter::VisitParmVarDecl(ParmVarDecl *D) {
  VisitVarDecl(D);
}

void DeclPrinter::VisitFileScopeAsmDecl(FileScopeAsmDecl *D) {
  Out << "__asm (";
  D->getAsmString()->printPretty(Out, nullptr, Policy, Indentation);
  Out << ")";
}

void DeclPrinter::VisitImportDecl(ImportDecl *D) {
  Out << "@import " << D->getImportedModule()->getFullModuleName()
      << ";\n";
}

void DeclPrinter::VisitStaticAssertDecl(StaticAssertDecl *D) {
  Out << "static_assert(";
  D->getAssertExpr()->printPretty(Out, nullptr, Policy, Indentation);
  if (StringLiteral *SL = D->getMessage()) {
    Out << ", ";
    SL->printPretty(Out, nullptr, Policy, Indentation);
  }
  Out << ")";
}

//----------------------------------------------------------------------------
// C++ declarations
//----------------------------------------------------------------------------
void DeclPrinter::VisitNamespaceDecl(NamespaceDecl *D) {
  if (D->isInline())
    Out << "inline ";
  Out << "namespace " << *D << " {\n";
  VisitDeclContext(D);
  Indent() << "}";
}

void DeclPrinter::VisitUsingDirectiveDecl(UsingDirectiveDecl *D) {
  Out << "using namespace ";
  if (D->getQualifier())
    D->getQualifier()->print(Out, Policy);
  Out << *D->getNominatedNamespaceAsWritten();
}

void DeclPrinter::VisitNamespaceAliasDecl(NamespaceAliasDecl *D) {
  Out << "namespace " << *D << " = ";
  if (D->getQualifier())
    D->getQualifier()->print(Out, Policy);
  Out << *D->getAliasedNamespace();
}

void DeclPrinter::VisitEmptyDecl(EmptyDecl *D) {
  prettyPrintAttributes(D);
}

void DeclPrinter::VisitCXXRecordDecl(CXXRecordDecl *D) {
  // FIXME: add printing of pragma attributes if required.
  if (!Policy.SuppressSpecifiers && D->isModulePrivate())
    Out << "__module_private__ ";
  Out << D->getKindName();

  prettyPrintAttributes(D);

  if (D->getIdentifier()) {
    Out << ' ' << *D;

    if (auto S = dyn_cast<ClassTemplatePartialSpecializationDecl>(D))
      printTemplateArguments(S->getTemplateArgs(), S->getTemplateParameters());
    else if (auto S = dyn_cast<ClassTemplateSpecializationDecl>(D))
      printTemplateArguments(S->getTemplateArgs());
  }

  if (D->isCompleteDefinition()) {
    // Print the base classes
    if (D->getNumBases()) {
      Out << " : ";
      for (CXXRecordDecl::base_class_iterator Base = D->bases_begin(),
             BaseEnd = D->bases_end(); Base != BaseEnd; ++Base) {
        if (Base != D->bases_begin())
          Out << ", ";

        if (Base->isVirtual())
          Out << "virtual ";

        AccessSpecifier AS = Base->getAccessSpecifierAsWritten();
        if (AS != AS_none) {
          Print(AS);
          Out << " ";
        }
        Out << Base->getType().getAsString(Policy);

        if (Base->isPackExpansion())
          Out << "...";
      }
    }

    // Print the class definition
    // FIXME: Doesn't print access specifiers, e.g., "public:"
    if (Policy.TerseOutput) {
      Out << " {}";
    } else {
      Out << " {\n";
      VisitDeclContext(D);
      Indent() << "}";
    }
  }
}

void DeclPrinter::VisitLinkageSpecDecl(LinkageSpecDecl *D) {
  const char *l;
  if (D->getLanguage() == LinkageSpecDecl::lang_c)
    l = "C";
  else {
    assert(D->getLanguage() == LinkageSpecDecl::lang_cxx &&
           "unknown language in linkage specification");
    l = "C++";
  }

  Out << "extern \"" << l << "\" ";
  if (D->hasBraces()) {
    Out << "{\n";
    VisitDeclContext(D);
    Indent() << "}";
  } else
    Visit(*D->decls_begin());
}

void DeclPrinter::printTemplateParameters(const TemplateParameterList *Params) {
  assert(Params);

  Out << "template <";

  for (unsigned i = 0, e = Params->size(); i != e; ++i) {
    if (i != 0)
      Out << ", ";

    const Decl *Param = Params->getParam(i);
    if (auto TTP = dyn_cast<TemplateTypeParmDecl>(Param)) {

      if (TTP->wasDeclaredWithTypename())
        Out << "typename ";
      else
        Out << "class ";

      if (TTP->isParameterPack())
        Out << "...";

      Out << *TTP;

      if (TTP->hasDefaultArgument()) {
        Out << " = ";
        Out << TTP->getDefaultArgument().getAsString(Policy);
      };
    } else if (auto NTTP = dyn_cast<NonTypeTemplateParmDecl>(Param)) {
      StringRef Name;
      if (IdentifierInfo *II = NTTP->getIdentifier())
        Name = II->getName();
      printDeclType(NTTP->getType(), Name, NTTP->isParameterPack());

      if (NTTP->hasDefaultArgument()) {
        Out << " = ";
        NTTP->getDefaultArgument()->printPretty(Out, nullptr, Policy,
                                                Indentation);
      }
    } else if (auto TTPD = dyn_cast<TemplateTemplateParmDecl>(Param)) {
      VisitTemplateDecl(TTPD);
      // FIXME: print the default argument, if present.
    }
  }

  Out << "> ";
}

void DeclPrinter::printTemplateArguments(const TemplateArgumentList &Args,
                                         const TemplateParameterList *Params) {
  Out << "<";
  for (size_t I = 0, E = Args.size(); I < E; ++I) {
    const TemplateArgument &A = Args[I];
    if (I)
      Out << ", ";
    if (Params) {
      if (A.getKind() == TemplateArgument::Type)
        if (auto T = A.getAsType()->getAs<TemplateTypeParmType>()) {
          auto P = cast<TemplateTypeParmDecl>(Params->getParam(T->getIndex()));
          Out << *P;
          continue;
        }
      if (A.getKind() == TemplateArgument::Template) {
        if (auto T = A.getAsTemplate().getAsTemplateDecl())
          if (auto TD = dyn_cast<TemplateTemplateParmDecl>(T)) {
            auto P = cast<TemplateTemplateParmDecl>(
                                              Params->getParam(TD->getIndex()));
            Out << *P;
            continue;
          }
      }
      if (A.getKind() == TemplateArgument::Expression) {
        if (auto E = dyn_cast<DeclRefExpr>(A.getAsExpr()))
          if (auto N = dyn_cast<NonTypeTemplateParmDecl>(E->getDecl())) {
            auto P = cast<NonTypeTemplateParmDecl>(
                                               Params->getParam(N->getIndex()));
            Out << *P;
            continue;
          }
      }
    }
    A.print(Policy, Out);
  }
  Out << ">";
}

void DeclPrinter::VisitTemplateDecl(const TemplateDecl *D) {
  printTemplateParameters(D->getTemplateParameters());

  if (const TemplateTemplateParmDecl *TTP =
        dyn_cast<TemplateTemplateParmDecl>(D)) {
    Out << "class ";
    if (TTP->isParameterPack())
      Out << "...";
    Out << D->getName();
  } else {
    Visit(D->getTemplatedDecl());
  }
}

void DeclPrinter::VisitFunctionTemplateDecl(FunctionTemplateDecl *D) {
  prettyPrintPragmas(D->getTemplatedDecl());
  // Print any leading template parameter lists.
  if (const FunctionDecl *FD = D->getTemplatedDecl()) {
    for (unsigned I = 0, NumTemplateParams = FD->getNumTemplateParameterLists();
         I < NumTemplateParams; ++I)
      printTemplateParameters(FD->getTemplateParameterList(I));
  }
  VisitRedeclarableTemplateDecl(D);
  // Declare target attribute is special one, natural spelling for the pragma
  // assumes "ending" construct so print it here.
  if (D->getTemplatedDecl()->hasAttr<OMPDeclareTargetDeclAttr>())
    Out << "#pragma omp end declare target\n";

  // Never print "instantiations" for deduction guides (they don't really
  // have them).
  if (PrintInstantiation &&
      !isa<CXXDeductionGuideDecl>(D->getTemplatedDecl())) {
    FunctionDecl *PrevDecl = D->getTemplatedDecl();
    const FunctionDecl *Def;
    if (PrevDecl->isDefined(Def) && Def != PrevDecl)
      return;
    for (auto *I : D->specializations())
      if (I->getTemplateSpecializationKind() == TSK_ImplicitInstantiation) {
        if (!PrevDecl->isThisDeclarationADefinition())
          Out << ";\n";
        Indent();
        prettyPrintPragmas(I);
        Visit(I);
      }
  }
}

void DeclPrinter::VisitClassTemplateDecl(ClassTemplateDecl *D) {
  VisitRedeclarableTemplateDecl(D);

  if (PrintInstantiation) {
    for (auto *I : D->specializations())
      if (I->getSpecializationKind() == TSK_ImplicitInstantiation) {
        if (D->isThisDeclarationADefinition())
          Out << ";";
        Out << "\n";
        Visit(I);
      }
  }
}

void DeclPrinter::VisitClassTemplateSpecializationDecl(
                                           ClassTemplateSpecializationDecl *D) {
  Out << "template<> ";
  VisitCXXRecordDecl(D);
}

void DeclPrinter::VisitClassTemplatePartialSpecializationDecl(
                                    ClassTemplatePartialSpecializationDecl *D) {
  printTemplateParameters(D->getTemplateParameters());
  VisitCXXRecordDecl(D);
}

//----------------------------------------------------------------------------
// Objective-C declarations
//----------------------------------------------------------------------------

void DeclPrinter::PrintObjCMethodType(ASTContext &Ctx,
                                      Decl::ObjCDeclQualifier Quals,
                                      QualType T) {
  Out << '(';
  if (Quals & Decl::ObjCDeclQualifier::OBJC_TQ_In)
    Out << "in ";
  if (Quals & Decl::ObjCDeclQualifier::OBJC_TQ_Inout)
    Out << "inout ";
  if (Quals & Decl::ObjCDeclQualifier::OBJC_TQ_Out)
    Out << "out ";
  if (Quals & Decl::ObjCDeclQualifier::OBJC_TQ_Bycopy)
    Out << "bycopy ";
  if (Quals & Decl::ObjCDeclQualifier::OBJC_TQ_Byref)
    Out << "byref ";
  if (Quals & Decl::ObjCDeclQualifier::OBJC_TQ_Oneway)
    Out << "oneway ";
  if (Quals & Decl::ObjCDeclQualifier::OBJC_TQ_CSNullability) {
    if (auto nullability = AttributedType::stripOuterNullability(T))
      Out << getNullabilitySpelling(*nullability, true) << ' ';
  }

  Out << Ctx.getUnqualifiedObjCPointerType(T).getAsString(Policy);
  Out << ')';
}

void DeclPrinter::PrintObjCTypeParams(ObjCTypeParamList *Params) {
  Out << "<";
  unsigned First = true;
  for (auto *Param : *Params) {
    if (First) {
      First = false;
    } else {
      Out << ", ";
    }

    switch (Param->getVariance()) {
    case ObjCTypeParamVariance::Invariant:
      break;

    case ObjCTypeParamVariance::Covariant:
      Out << "__covariant ";
      break;

    case ObjCTypeParamVariance::Contravariant:
      Out << "__contravariant ";
      break;
    }

    Out << Param->getDeclName().getAsString();

    if (Param->hasExplicitBound()) {
      Out << " : " << Param->getUnderlyingType().getAsString(Policy);
    }
  }
  Out << ">";
}

void DeclPrinter::VisitObjCMethodDecl(ObjCMethodDecl *OMD) {
  if (OMD->isInstanceMethod())
    Out << "- ";
  else
    Out << "+ ";
  if (!OMD->getReturnType().isNull()) {
    PrintObjCMethodType(OMD->getASTContext(), OMD->getObjCDeclQualifier(),
                        OMD->getReturnType());
  }

  std::string name = OMD->getSelector().getAsString();
  std::string::size_type pos, lastPos = 0;
  for (const auto *PI : OMD->parameters()) {
    // FIXME: selector is missing here!
    pos = name.find_first_of(':', lastPos);
    if (lastPos != 0)
      Out << " ";
    Out << name.substr(lastPos, pos - lastPos) << ':';
    PrintObjCMethodType(OMD->getASTContext(),
                        PI->getObjCDeclQualifier(),
                        PI->getType());
    Out << *PI;
    lastPos = pos + 1;
  }

  if (OMD->param_begin() == OMD->param_end())
    Out << name;

  if (OMD->isVariadic())
      Out << ", ...";

  prettyPrintAttributes(OMD);

  if (OMD->getBody() && !Policy.TerseOutput) {
    Out << ' ';
    OMD->getBody()->printPretty(Out, nullptr, Policy);
  }
  else if (Policy.PolishForDeclaration)
    Out << ';';
}

void DeclPrinter::VisitObjCImplementationDecl(ObjCImplementationDecl *OID) {
  std::string I = OID->getNameAsString();
  ObjCInterfaceDecl *SID = OID->getSuperClass();

  bool eolnOut = false;
  if (SID)
    Out << "@implementation " << I << " : " << *SID;
  else
    Out << "@implementation " << I;

  if (OID->ivar_size() > 0) {
    Out << "{\n";
    eolnOut = true;
    Indentation += Policy.Indentation;
    for (const auto *I : OID->ivars()) {
      Indent() << I->getASTContext().getUnqualifiedObjCPointerType(I->getType()).
                    getAsString(Policy) << ' ' << *I << ";\n";
    }
    Indentation -= Policy.Indentation;
    Out << "}\n";
  }
  else if (SID || (OID->decls_begin() != OID->decls_end())) {
    Out << "\n";
    eolnOut = true;
  }
  VisitDeclContext(OID, false);
  if (!eolnOut)
    Out << "\n";
  Out << "@end";
}

void DeclPrinter::VisitObjCInterfaceDecl(ObjCInterfaceDecl *OID) {
  std::string I = OID->getNameAsString();
  ObjCInterfaceDecl *SID = OID->getSuperClass();

  if (!OID->isThisDeclarationADefinition()) {
    Out << "@class " << I;

    if (auto TypeParams = OID->getTypeParamListAsWritten()) {
      PrintObjCTypeParams(TypeParams);
    }

    Out << ";";
    return;
  }
  bool eolnOut = false;
  Out << "@interface " << I;

  if (auto TypeParams = OID->getTypeParamListAsWritten()) {
    PrintObjCTypeParams(TypeParams);
  }

  if (SID)
    Out << " : " << QualType(OID->getSuperClassType(), 0).getAsString(Policy);

  // Protocols?
  const ObjCList<ObjCProtocolDecl> &Protocols = OID->getReferencedProtocols();
  if (!Protocols.empty()) {
    for (ObjCList<ObjCProtocolDecl>::iterator I = Protocols.begin(),
         E = Protocols.end(); I != E; ++I)
      Out << (I == Protocols.begin() ? '<' : ',') << **I;
    Out << "> ";
  }

  if (OID->ivar_size() > 0) {
    Out << "{\n";
    eolnOut = true;
    Indentation += Policy.Indentation;
    for (const auto *I : OID->ivars()) {
      Indent() << I->getASTContext()
                      .getUnqualifiedObjCPointerType(I->getType())
                      .getAsString(Policy) << ' ' << *I << ";\n";
    }
    Indentation -= Policy.Indentation;
    Out << "}\n";
  }
  else if (SID || (OID->decls_begin() != OID->decls_end())) {
    Out << "\n";
    eolnOut = true;
  }

  VisitDeclContext(OID, false);
  if (!eolnOut)
    Out << "\n";
  Out << "@end";
  // FIXME: implement the rest...
}

void DeclPrinter::VisitObjCProtocolDecl(ObjCProtocolDecl *PID) {
  if (!PID->isThisDeclarationADefinition()) {
    Out << "@protocol " << *PID << ";\n";
    return;
  }
  // Protocols?
  const ObjCList<ObjCProtocolDecl> &Protocols = PID->getReferencedProtocols();
  if (!Protocols.empty()) {
    Out << "@protocol " << *PID;
    for (ObjCList<ObjCProtocolDecl>::iterator I = Protocols.begin(),
         E = Protocols.end(); I != E; ++I)
      Out << (I == Protocols.begin() ? '<' : ',') << **I;
    Out << ">\n";
  } else
    Out << "@protocol " << *PID << '\n';
  VisitDeclContext(PID, false);
  Out << "@end";
}

void DeclPrinter::VisitObjCCategoryImplDecl(ObjCCategoryImplDecl *PID) {
  Out << "@implementation " << *PID->getClassInterface() << '(' << *PID <<")\n";

  VisitDeclContext(PID, false);
  Out << "@end";
  // FIXME: implement the rest...
}

void DeclPrinter::VisitObjCCategoryDecl(ObjCCategoryDecl *PID) {
  Out << "@interface " << *PID->getClassInterface();
  if (auto TypeParams = PID->getTypeParamList()) {
    PrintObjCTypeParams(TypeParams);
  }
  Out << "(" << *PID << ")\n";
  if (PID->ivar_size() > 0) {
    Out << "{\n";
    Indentation += Policy.Indentation;
    for (const auto *I : PID->ivars())
      Indent() << I->getASTContext().getUnqualifiedObjCPointerType(I->getType()).
                    getAsString(Policy) << ' ' << *I << ";\n";
    Indentation -= Policy.Indentation;
    Out << "}\n";
  }

  VisitDeclContext(PID, false);
  Out << "@end";

  // FIXME: implement the rest...
}

void DeclPrinter::VisitObjCCompatibleAliasDecl(ObjCCompatibleAliasDecl *AID) {
  Out << "@compatibility_alias " << *AID
      << ' ' << *AID->getClassInterface() << ";\n";
}

/// PrintObjCPropertyDecl - print a property declaration.
///
void DeclPrinter::VisitObjCPropertyDecl(ObjCPropertyDecl *PDecl) {
  if (PDecl->getPropertyImplementation() == ObjCPropertyDecl::Required)
    Out << "@required\n";
  else if (PDecl->getPropertyImplementation() == ObjCPropertyDecl::Optional)
    Out << "@optional\n";

  QualType T = PDecl->getType();

  Out << "@property";
  if (PDecl->getPropertyAttributes() != ObjCPropertyDecl::OBJC_PR_noattr) {
    bool first = true;
    Out << " (";
    if (PDecl->getPropertyAttributes() &
        ObjCPropertyDecl::OBJC_PR_readonly) {
      Out << (first ? ' ' : ',') << "readonly";
      first = false;
    }

    if (PDecl->getPropertyAttributes() & ObjCPropertyDecl::OBJC_PR_getter) {
      Out << (first ? ' ' : ',') << "getter = ";
      PDecl->getGetterName().print(Out);
      first = false;
    }
    if (PDecl->getPropertyAttributes() & ObjCPropertyDecl::OBJC_PR_setter) {
      Out << (first ? ' ' : ',') << "setter = ";
      PDecl->getSetterName().print(Out);
      first = false;
    }

    if (PDecl->getPropertyAttributes() & ObjCPropertyDecl::OBJC_PR_assign) {
      Out << (first ? ' ' : ',') << "assign";
      first = false;
    }

    if (PDecl->getPropertyAttributes() &
        ObjCPropertyDecl::OBJC_PR_readwrite) {
      Out << (first ? ' ' : ',') << "readwrite";
      first = false;
    }

    if (PDecl->getPropertyAttributes() & ObjCPropertyDecl::OBJC_PR_retain) {
      Out << (first ? ' ' : ',') << "retain";
      first = false;
    }

    if (PDecl->getPropertyAttributes() & ObjCPropertyDecl::OBJC_PR_strong) {
      Out << (first ? ' ' : ',') << "strong";
      first = false;
    }

    if (PDecl->getPropertyAttributes() & ObjCPropertyDecl::OBJC_PR_copy) {
      Out << (first ? ' ' : ',') << "copy";
      first = false;
    }

    if (PDecl->getPropertyAttributes() &
        ObjCPropertyDecl::OBJC_PR_nonatomic) {
      Out << (first ? ' ' : ',') << "nonatomic";
      first = false;
    }
    if (PDecl->getPropertyAttributes() &
        ObjCPropertyDecl::OBJC_PR_atomic) {
      Out << (first ? ' ' : ',') << "atomic";
      first = false;
    }

    if (PDecl->getPropertyAttributes() &
        ObjCPropertyDecl::OBJC_PR_nullability) {
      if (auto nullability = AttributedType::stripOuterNullability(T)) {
        if (*nullability == NullabilityKind::Unspecified &&
            (PDecl->getPropertyAttributes() &
               ObjCPropertyDecl::OBJC_PR_null_resettable)) {
          Out << (first ? ' ' : ',') << "null_resettable";
        } else {
          Out << (first ? ' ' : ',')
              << getNullabilitySpelling(*nullability, true);
        }
        first = false;
      }
    }

    if (PDecl->getPropertyAttributes() & ObjCPropertyDecl::OBJC_PR_class) {
      Out << (first ? ' ' : ',') << "class";
      first = false;
    }

    (void) first; // Silence dead store warning due to idiomatic code.
    Out << " )";
  }
  Out << ' ' << PDecl->getASTContext().getUnqualifiedObjCPointerType(T).
                  getAsString(Policy) << ' ' << *PDecl;
  if (Policy.PolishForDeclaration)
    Out << ';';
}

void DeclPrinter::VisitObjCPropertyImplDecl(ObjCPropertyImplDecl *PID) {
  if (PID->getPropertyImplementation() == ObjCPropertyImplDecl::Synthesize)
    Out << "@synthesize ";
  else
    Out << "@dynamic ";
  Out << *PID->getPropertyDecl();
  if (PID->getPropertyIvarDecl())
    Out << '=' << *PID->getPropertyIvarDecl();
}

void DeclPrinter::VisitUsingDecl(UsingDecl *D) {
  if (!D->isAccessDeclaration())
    Out << "using ";
  if (D->hasTypename())
    Out << "typename ";
  D->getQualifier()->print(Out, Policy);

  // Use the correct record name when the using declaration is used for
  // inheriting constructors.
  for (const auto *Shadow : D->shadows()) {
    if (const auto *ConstructorShadow =
            dyn_cast<ConstructorUsingShadowDecl>(Shadow)) {
      assert(Shadow->getDeclContext() == ConstructorShadow->getDeclContext());
      Out << *ConstructorShadow->getNominatedBaseClass();
      return;
    }
  }
  Out << *D;
}

void
DeclPrinter::VisitUnresolvedUsingTypenameDecl(UnresolvedUsingTypenameDecl *D) {
  Out << "using typename ";
  D->getQualifier()->print(Out, Policy);
  Out << D->getDeclName();
}

void DeclPrinter::VisitUnresolvedUsingValueDecl(UnresolvedUsingValueDecl *D) {
  if (!D->isAccessDeclaration())
    Out << "using ";
  D->getQualifier()->print(Out, Policy);
  Out << D->getDeclName();
}

void DeclPrinter::VisitUsingShadowDecl(UsingShadowDecl *D) {
  // ignore
}

void DeclPrinter::VisitOMPThreadPrivateDecl(OMPThreadPrivateDecl *D) {
  Out << "#pragma omp threadprivate";
  if (!D->varlist_empty()) {
    for (OMPThreadPrivateDecl::varlist_iterator I = D->varlist_begin(),
                                                E = D->varlist_end();
                                                I != E; ++I) {
      Out << (I == D->varlist_begin() ? '(' : ',');
      NamedDecl *ND = cast<DeclRefExpr>(*I)->getDecl();
      ND->printQualifiedName(Out);
    }
    Out << ")";
  }
}

void DeclPrinter::VisitOMPRequiresDecl(OMPRequiresDecl *D) {
  Out << "#pragma omp requires ";
  if (!D->clauselist_empty()) {
    OMPClausePrinter Printer(Out, Policy);
    for (auto I = D->clauselist_begin(), E = D->clauselist_end(); I != E; ++I)
      Printer.Visit(*I);
  }
}

void DeclPrinter::VisitOMPDeclareReductionDecl(OMPDeclareReductionDecl *D) {
  if (!D->isInvalidDecl()) {
    Out << "#pragma omp declare reduction (";
    if (D->getDeclName().getNameKind() == DeclarationName::CXXOperatorName) {
      static const char *const OperatorNames[NUM_OVERLOADED_OPERATORS] = {
          nullptr,
#define OVERLOADED_OPERATOR(Name, Spelling, Token, Unary, Binary, MemberOnly)  \
          Spelling,
#include "clang/Basic/OperatorKinds.def"
      };
      const char *OpName =
          OperatorNames[D->getDeclName().getCXXOverloadedOperator()];
      assert(OpName && "not an overloaded operator");
      Out << OpName;
    } else {
      assert(D->getDeclName().isIdentifier());
      D->printName(Out);
    }
    Out << " : ";
    D->getType().print(Out, Policy);
    Out << " : ";
    D->getCombiner()->printPretty(Out, nullptr, Policy, 0);
    Out << ")";
    if (auto *Init = D->getInitializer()) {
      Out << " initializer(";
      switch (D->getInitializerKind()) {
      case OMPDeclareReductionDecl::DirectInit:
        Out << "omp_priv(";
        break;
      case OMPDeclareReductionDecl::CopyInit:
        Out << "omp_priv = ";
        break;
      case OMPDeclareReductionDecl::CallInit:
        break;
      }
      Init->printPretty(Out, nullptr, Policy, 0);
      if (D->getInitializerKind() == OMPDeclareReductionDecl::DirectInit)
        Out << ")";
      Out << ")";
    }
  }
}

void DeclPrinter::VisitOMPCapturedExprDecl(OMPCapturedExprDecl *D) {
  D->getInit()->printPretty(Out, nullptr, Policy, Indentation);
}

