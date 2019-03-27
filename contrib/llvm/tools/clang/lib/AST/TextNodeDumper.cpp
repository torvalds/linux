//===--- TextNodeDumper.cpp - Printing of AST nodes -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements AST dumping of components of individual AST nodes.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/TextNodeDumper.h"
#include "clang/AST/DeclFriend.h"
#include "clang/AST/DeclOpenMP.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/LocInfoType.h"

using namespace clang;

static void dumpPreviousDeclImpl(raw_ostream &OS, ...) {}

template <typename T>
static void dumpPreviousDeclImpl(raw_ostream &OS, const Mergeable<T> *D) {
  const T *First = D->getFirstDecl();
  if (First != D)
    OS << " first " << First;
}

template <typename T>
static void dumpPreviousDeclImpl(raw_ostream &OS, const Redeclarable<T> *D) {
  const T *Prev = D->getPreviousDecl();
  if (Prev)
    OS << " prev " << Prev;
}

/// Dump the previous declaration in the redeclaration chain for a declaration,
/// if any.
static void dumpPreviousDecl(raw_ostream &OS, const Decl *D) {
  switch (D->getKind()) {
#define DECL(DERIVED, BASE)                                                    \
  case Decl::DERIVED:                                                          \
    return dumpPreviousDeclImpl(OS, cast<DERIVED##Decl>(D));
#define ABSTRACT_DECL(DECL)
#include "clang/AST/DeclNodes.inc"
  }
  llvm_unreachable("Decl that isn't part of DeclNodes.inc!");
}

TextNodeDumper::TextNodeDumper(raw_ostream &OS, bool ShowColors,
                               const SourceManager *SM,
                               const PrintingPolicy &PrintPolicy,
                               const comments::CommandTraits *Traits)
    : TextTreeStructure(OS, ShowColors), OS(OS), ShowColors(ShowColors), SM(SM),
      PrintPolicy(PrintPolicy), Traits(Traits) {}

void TextNodeDumper::Visit(const comments::Comment *C,
                           const comments::FullComment *FC) {
  if (!C) {
    ColorScope Color(OS, ShowColors, NullColor);
    OS << "<<<NULL>>>";
    return;
  }

  {
    ColorScope Color(OS, ShowColors, CommentColor);
    OS << C->getCommentKindName();
  }
  dumpPointer(C);
  dumpSourceRange(C->getSourceRange());

  ConstCommentVisitor<TextNodeDumper, void,
                      const comments::FullComment *>::visit(C, FC);
}

void TextNodeDumper::Visit(const Attr *A) {
  {
    ColorScope Color(OS, ShowColors, AttrColor);

    switch (A->getKind()) {
#define ATTR(X)                                                                \
  case attr::X:                                                                \
    OS << #X;                                                                  \
    break;
#include "clang/Basic/AttrList.inc"
    }
    OS << "Attr";
  }
  dumpPointer(A);
  dumpSourceRange(A->getRange());
  if (A->isInherited())
    OS << " Inherited";
  if (A->isImplicit())
    OS << " Implicit";

  ConstAttrVisitor<TextNodeDumper>::Visit(A);
}

void TextNodeDumper::Visit(const TemplateArgument &TA, SourceRange R,
                           const Decl *From, StringRef Label) {
  OS << "TemplateArgument";
  if (R.isValid())
    dumpSourceRange(R);

  if (From)
    dumpDeclRef(From, Label);

  ConstTemplateArgumentVisitor<TextNodeDumper>::Visit(TA);
}

void TextNodeDumper::Visit(const Stmt *Node) {
  if (!Node) {
    ColorScope Color(OS, ShowColors, NullColor);
    OS << "<<<NULL>>>";
    return;
  }
  {
    ColorScope Color(OS, ShowColors, StmtColor);
    OS << Node->getStmtClassName();
  }
  dumpPointer(Node);
  dumpSourceRange(Node->getSourceRange());

  if (const auto *E = dyn_cast<Expr>(Node)) {
    dumpType(E->getType());

    {
      ColorScope Color(OS, ShowColors, ValueKindColor);
      switch (E->getValueKind()) {
      case VK_RValue:
        break;
      case VK_LValue:
        OS << " lvalue";
        break;
      case VK_XValue:
        OS << " xvalue";
        break;
      }
    }

    {
      ColorScope Color(OS, ShowColors, ObjectKindColor);
      switch (E->getObjectKind()) {
      case OK_Ordinary:
        break;
      case OK_BitField:
        OS << " bitfield";
        break;
      case OK_ObjCProperty:
        OS << " objcproperty";
        break;
      case OK_ObjCSubscript:
        OS << " objcsubscript";
        break;
      case OK_VectorComponent:
        OS << " vectorcomponent";
        break;
      }
    }
  }

  ConstStmtVisitor<TextNodeDumper>::Visit(Node);
}

void TextNodeDumper::Visit(const Type *T) {
  if (!T) {
    ColorScope Color(OS, ShowColors, NullColor);
    OS << "<<<NULL>>>";
    return;
  }
  if (isa<LocInfoType>(T)) {
    {
      ColorScope Color(OS, ShowColors, TypeColor);
      OS << "LocInfo Type";
    }
    dumpPointer(T);
    return;
  }

  {
    ColorScope Color(OS, ShowColors, TypeColor);
    OS << T->getTypeClassName() << "Type";
  }
  dumpPointer(T);
  OS << " ";
  dumpBareType(QualType(T, 0), false);

  QualType SingleStepDesugar =
      T->getLocallyUnqualifiedSingleStepDesugaredType();
  if (SingleStepDesugar != QualType(T, 0))
    OS << " sugar";

  if (T->isDependentType())
    OS << " dependent";
  else if (T->isInstantiationDependentType())
    OS << " instantiation_dependent";

  if (T->isVariablyModifiedType())
    OS << " variably_modified";
  if (T->containsUnexpandedParameterPack())
    OS << " contains_unexpanded_pack";
  if (T->isFromAST())
    OS << " imported";

  TypeVisitor<TextNodeDumper>::Visit(T);
}

void TextNodeDumper::Visit(QualType T) {
  OS << "QualType";
  dumpPointer(T.getAsOpaquePtr());
  OS << " ";
  dumpBareType(T, false);
  OS << " " << T.split().Quals.getAsString();
}

void TextNodeDumper::Visit(const Decl *D) {
  if (!D) {
    ColorScope Color(OS, ShowColors, NullColor);
    OS << "<<<NULL>>>";
    return;
  }

  {
    ColorScope Color(OS, ShowColors, DeclKindNameColor);
    OS << D->getDeclKindName() << "Decl";
  }
  dumpPointer(D);
  if (D->getLexicalDeclContext() != D->getDeclContext())
    OS << " parent " << cast<Decl>(D->getDeclContext());
  dumpPreviousDecl(OS, D);
  dumpSourceRange(D->getSourceRange());
  OS << ' ';
  dumpLocation(D->getLocation());
  if (D->isFromASTFile())
    OS << " imported";
  if (Module *M = D->getOwningModule())
    OS << " in " << M->getFullModuleName();
  if (auto *ND = dyn_cast<NamedDecl>(D))
    for (Module *M : D->getASTContext().getModulesWithMergedDefinition(
             const_cast<NamedDecl *>(ND)))
      AddChild([=] { OS << "also in " << M->getFullModuleName(); });
  if (const NamedDecl *ND = dyn_cast<NamedDecl>(D))
    if (ND->isHidden())
      OS << " hidden";
  if (D->isImplicit())
    OS << " implicit";

  if (D->isUsed())
    OS << " used";
  else if (D->isThisDeclarationReferenced())
    OS << " referenced";

  if (D->isInvalidDecl())
    OS << " invalid";
  if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D))
    if (FD->isConstexpr())
      OS << " constexpr";
}

void TextNodeDumper::Visit(const CXXCtorInitializer *Init) {
  OS << "CXXCtorInitializer";
  if (Init->isAnyMemberInitializer()) {
    OS << ' ';
    dumpBareDeclRef(Init->getAnyMember());
  } else if (Init->isBaseInitializer()) {
    dumpType(QualType(Init->getBaseClass(), 0));
  } else if (Init->isDelegatingInitializer()) {
    dumpType(Init->getTypeSourceInfo()->getType());
  } else {
    llvm_unreachable("Unknown initializer type");
  }
}

void TextNodeDumper::Visit(const BlockDecl::Capture &C) {
  OS << "capture";
  if (C.isByRef())
    OS << " byref";
  if (C.isNested())
    OS << " nested";
  if (C.getVariable()) {
    OS << ' ';
    dumpBareDeclRef(C.getVariable());
  }
}

void TextNodeDumper::Visit(const OMPClause *C) {
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
  dumpPointer(C);
  dumpSourceRange(SourceRange(C->getBeginLoc(), C->getEndLoc()));
  if (C->isImplicit())
    OS << " <implicit>";
}

void TextNodeDumper::dumpPointer(const void *Ptr) {
  ColorScope Color(OS, ShowColors, AddressColor);
  OS << ' ' << Ptr;
}

void TextNodeDumper::dumpLocation(SourceLocation Loc) {
  if (!SM)
    return;

  ColorScope Color(OS, ShowColors, LocationColor);
  SourceLocation SpellingLoc = SM->getSpellingLoc(Loc);

  // The general format we print out is filename:line:col, but we drop pieces
  // that haven't changed since the last loc printed.
  PresumedLoc PLoc = SM->getPresumedLoc(SpellingLoc);

  if (PLoc.isInvalid()) {
    OS << "<invalid sloc>";
    return;
  }

  if (strcmp(PLoc.getFilename(), LastLocFilename) != 0) {
    OS << PLoc.getFilename() << ':' << PLoc.getLine() << ':'
       << PLoc.getColumn();
    LastLocFilename = PLoc.getFilename();
    LastLocLine = PLoc.getLine();
  } else if (PLoc.getLine() != LastLocLine) {
    OS << "line" << ':' << PLoc.getLine() << ':' << PLoc.getColumn();
    LastLocLine = PLoc.getLine();
  } else {
    OS << "col" << ':' << PLoc.getColumn();
  }
}

void TextNodeDumper::dumpSourceRange(SourceRange R) {
  // Can't translate locations if a SourceManager isn't available.
  if (!SM)
    return;

  OS << " <";
  dumpLocation(R.getBegin());
  if (R.getBegin() != R.getEnd()) {
    OS << ", ";
    dumpLocation(R.getEnd());
  }
  OS << ">";

  // <t2.c:123:421[blah], t2.c:412:321>
}

void TextNodeDumper::dumpBareType(QualType T, bool Desugar) {
  ColorScope Color(OS, ShowColors, TypeColor);

  SplitQualType T_split = T.split();
  OS << "'" << QualType::getAsString(T_split, PrintPolicy) << "'";

  if (Desugar && !T.isNull()) {
    // If the type is sugared, also dump a (shallow) desugared type.
    SplitQualType D_split = T.getSplitDesugaredType();
    if (T_split != D_split)
      OS << ":'" << QualType::getAsString(D_split, PrintPolicy) << "'";
  }
}

void TextNodeDumper::dumpType(QualType T) {
  OS << ' ';
  dumpBareType(T);
}

void TextNodeDumper::dumpBareDeclRef(const Decl *D) {
  if (!D) {
    ColorScope Color(OS, ShowColors, NullColor);
    OS << "<<<NULL>>>";
    return;
  }

  {
    ColorScope Color(OS, ShowColors, DeclKindNameColor);
    OS << D->getDeclKindName();
  }
  dumpPointer(D);

  if (const NamedDecl *ND = dyn_cast<NamedDecl>(D)) {
    ColorScope Color(OS, ShowColors, DeclNameColor);
    OS << " '" << ND->getDeclName() << '\'';
  }

  if (const ValueDecl *VD = dyn_cast<ValueDecl>(D))
    dumpType(VD->getType());
}

void TextNodeDumper::dumpName(const NamedDecl *ND) {
  if (ND->getDeclName()) {
    ColorScope Color(OS, ShowColors, DeclNameColor);
    OS << ' ' << ND->getNameAsString();
  }
}

void TextNodeDumper::dumpAccessSpecifier(AccessSpecifier AS) {
  switch (AS) {
  case AS_none:
    break;
  case AS_public:
    OS << "public";
    break;
  case AS_protected:
    OS << "protected";
    break;
  case AS_private:
    OS << "private";
    break;
  }
}

void TextNodeDumper::dumpCXXTemporary(const CXXTemporary *Temporary) {
  OS << "(CXXTemporary";
  dumpPointer(Temporary);
  OS << ")";
}

void TextNodeDumper::dumpDeclRef(const Decl *D, StringRef Label) {
  if (!D)
    return;

  AddChild([=] {
    if (!Label.empty())
      OS << Label << ' ';
    dumpBareDeclRef(D);
  });
}

const char *TextNodeDumper::getCommandName(unsigned CommandID) {
  if (Traits)
    return Traits->getCommandInfo(CommandID)->Name;
  const comments::CommandInfo *Info =
      comments::CommandTraits::getBuiltinCommandInfo(CommandID);
  if (Info)
    return Info->Name;
  return "<not a builtin command>";
}

void TextNodeDumper::visitTextComment(const comments::TextComment *C,
                                      const comments::FullComment *) {
  OS << " Text=\"" << C->getText() << "\"";
}

void TextNodeDumper::visitInlineCommandComment(
    const comments::InlineCommandComment *C, const comments::FullComment *) {
  OS << " Name=\"" << getCommandName(C->getCommandID()) << "\"";
  switch (C->getRenderKind()) {
  case comments::InlineCommandComment::RenderNormal:
    OS << " RenderNormal";
    break;
  case comments::InlineCommandComment::RenderBold:
    OS << " RenderBold";
    break;
  case comments::InlineCommandComment::RenderMonospaced:
    OS << " RenderMonospaced";
    break;
  case comments::InlineCommandComment::RenderEmphasized:
    OS << " RenderEmphasized";
    break;
  }

  for (unsigned i = 0, e = C->getNumArgs(); i != e; ++i)
    OS << " Arg[" << i << "]=\"" << C->getArgText(i) << "\"";
}

void TextNodeDumper::visitHTMLStartTagComment(
    const comments::HTMLStartTagComment *C, const comments::FullComment *) {
  OS << " Name=\"" << C->getTagName() << "\"";
  if (C->getNumAttrs() != 0) {
    OS << " Attrs: ";
    for (unsigned i = 0, e = C->getNumAttrs(); i != e; ++i) {
      const comments::HTMLStartTagComment::Attribute &Attr = C->getAttr(i);
      OS << " \"" << Attr.Name << "=\"" << Attr.Value << "\"";
    }
  }
  if (C->isSelfClosing())
    OS << " SelfClosing";
}

void TextNodeDumper::visitHTMLEndTagComment(
    const comments::HTMLEndTagComment *C, const comments::FullComment *) {
  OS << " Name=\"" << C->getTagName() << "\"";
}

void TextNodeDumper::visitBlockCommandComment(
    const comments::BlockCommandComment *C, const comments::FullComment *) {
  OS << " Name=\"" << getCommandName(C->getCommandID()) << "\"";
  for (unsigned i = 0, e = C->getNumArgs(); i != e; ++i)
    OS << " Arg[" << i << "]=\"" << C->getArgText(i) << "\"";
}

void TextNodeDumper::visitParamCommandComment(
    const comments::ParamCommandComment *C, const comments::FullComment *FC) {
  OS << " "
     << comments::ParamCommandComment::getDirectionAsString(C->getDirection());

  if (C->isDirectionExplicit())
    OS << " explicitly";
  else
    OS << " implicitly";

  if (C->hasParamName()) {
    if (C->isParamIndexValid())
      OS << " Param=\"" << C->getParamName(FC) << "\"";
    else
      OS << " Param=\"" << C->getParamNameAsWritten() << "\"";
  }

  if (C->isParamIndexValid() && !C->isVarArgParam())
    OS << " ParamIndex=" << C->getParamIndex();
}

void TextNodeDumper::visitTParamCommandComment(
    const comments::TParamCommandComment *C, const comments::FullComment *FC) {
  if (C->hasParamName()) {
    if (C->isPositionValid())
      OS << " Param=\"" << C->getParamName(FC) << "\"";
    else
      OS << " Param=\"" << C->getParamNameAsWritten() << "\"";
  }

  if (C->isPositionValid()) {
    OS << " Position=<";
    for (unsigned i = 0, e = C->getDepth(); i != e; ++i) {
      OS << C->getIndex(i);
      if (i != e - 1)
        OS << ", ";
    }
    OS << ">";
  }
}

void TextNodeDumper::visitVerbatimBlockComment(
    const comments::VerbatimBlockComment *C, const comments::FullComment *) {
  OS << " Name=\"" << getCommandName(C->getCommandID())
     << "\""
        " CloseName=\""
     << C->getCloseName() << "\"";
}

void TextNodeDumper::visitVerbatimBlockLineComment(
    const comments::VerbatimBlockLineComment *C,
    const comments::FullComment *) {
  OS << " Text=\"" << C->getText() << "\"";
}

void TextNodeDumper::visitVerbatimLineComment(
    const comments::VerbatimLineComment *C, const comments::FullComment *) {
  OS << " Text=\"" << C->getText() << "\"";
}

void TextNodeDumper::VisitNullTemplateArgument(const TemplateArgument &) {
  OS << " null";
}

void TextNodeDumper::VisitTypeTemplateArgument(const TemplateArgument &TA) {
  OS << " type";
  dumpType(TA.getAsType());
}

void TextNodeDumper::VisitDeclarationTemplateArgument(
    const TemplateArgument &TA) {
  OS << " decl";
  dumpDeclRef(TA.getAsDecl());
}

void TextNodeDumper::VisitNullPtrTemplateArgument(const TemplateArgument &) {
  OS << " nullptr";
}

void TextNodeDumper::VisitIntegralTemplateArgument(const TemplateArgument &TA) {
  OS << " integral " << TA.getAsIntegral();
}

void TextNodeDumper::VisitTemplateTemplateArgument(const TemplateArgument &TA) {
  OS << " template ";
  TA.getAsTemplate().dump(OS);
}

void TextNodeDumper::VisitTemplateExpansionTemplateArgument(
    const TemplateArgument &TA) {
  OS << " template expansion ";
  TA.getAsTemplateOrTemplatePattern().dump(OS);
}

void TextNodeDumper::VisitExpressionTemplateArgument(const TemplateArgument &) {
  OS << " expr";
}

void TextNodeDumper::VisitPackTemplateArgument(const TemplateArgument &) {
  OS << " pack";
}

static void dumpBasePath(raw_ostream &OS, const CastExpr *Node) {
  if (Node->path_empty())
    return;

  OS << " (";
  bool First = true;
  for (CastExpr::path_const_iterator I = Node->path_begin(),
                                     E = Node->path_end();
       I != E; ++I) {
    const CXXBaseSpecifier *Base = *I;
    if (!First)
      OS << " -> ";

    const CXXRecordDecl *RD =
        cast<CXXRecordDecl>(Base->getType()->getAs<RecordType>()->getDecl());

    if (Base->isVirtual())
      OS << "virtual ";
    OS << RD->getName();
    First = false;
  }

  OS << ')';
}

void TextNodeDumper::VisitIfStmt(const IfStmt *Node) {
  if (Node->hasInitStorage())
    OS << " has_init";
  if (Node->hasVarStorage())
    OS << " has_var";
  if (Node->hasElseStorage())
    OS << " has_else";
}

void TextNodeDumper::VisitSwitchStmt(const SwitchStmt *Node) {
  if (Node->hasInitStorage())
    OS << " has_init";
  if (Node->hasVarStorage())
    OS << " has_var";
}

void TextNodeDumper::VisitWhileStmt(const WhileStmt *Node) {
  if (Node->hasVarStorage())
    OS << " has_var";
}

void TextNodeDumper::VisitLabelStmt(const LabelStmt *Node) {
  OS << " '" << Node->getName() << "'";
}

void TextNodeDumper::VisitGotoStmt(const GotoStmt *Node) {
  OS << " '" << Node->getLabel()->getName() << "'";
  dumpPointer(Node->getLabel());
}

void TextNodeDumper::VisitCaseStmt(const CaseStmt *Node) {
  if (Node->caseStmtIsGNURange())
    OS << " gnu_range";
}

void TextNodeDumper::VisitCallExpr(const CallExpr *Node) {
  if (Node->usesADL())
    OS << " adl";
}

void TextNodeDumper::VisitCastExpr(const CastExpr *Node) {
  OS << " <";
  {
    ColorScope Color(OS, ShowColors, CastColor);
    OS << Node->getCastKindName();
  }
  dumpBasePath(OS, Node);
  OS << ">";
}

void TextNodeDumper::VisitImplicitCastExpr(const ImplicitCastExpr *Node) {
  VisitCastExpr(Node);
  if (Node->isPartOfExplicitCast())
    OS << " part_of_explicit_cast";
}

void TextNodeDumper::VisitDeclRefExpr(const DeclRefExpr *Node) {
  OS << " ";
  dumpBareDeclRef(Node->getDecl());
  if (Node->getDecl() != Node->getFoundDecl()) {
    OS << " (";
    dumpBareDeclRef(Node->getFoundDecl());
    OS << ")";
  }
}

void TextNodeDumper::VisitUnresolvedLookupExpr(
    const UnresolvedLookupExpr *Node) {
  OS << " (";
  if (!Node->requiresADL())
    OS << "no ";
  OS << "ADL) = '" << Node->getName() << '\'';

  UnresolvedLookupExpr::decls_iterator I = Node->decls_begin(),
                                       E = Node->decls_end();
  if (I == E)
    OS << " empty";
  for (; I != E; ++I)
    dumpPointer(*I);
}

void TextNodeDumper::VisitObjCIvarRefExpr(const ObjCIvarRefExpr *Node) {
  {
    ColorScope Color(OS, ShowColors, DeclKindNameColor);
    OS << " " << Node->getDecl()->getDeclKindName() << "Decl";
  }
  OS << "='" << *Node->getDecl() << "'";
  dumpPointer(Node->getDecl());
  if (Node->isFreeIvar())
    OS << " isFreeIvar";
}

void TextNodeDumper::VisitPredefinedExpr(const PredefinedExpr *Node) {
  OS << " " << PredefinedExpr::getIdentKindName(Node->getIdentKind());
}

void TextNodeDumper::VisitCharacterLiteral(const CharacterLiteral *Node) {
  ColorScope Color(OS, ShowColors, ValueColor);
  OS << " " << Node->getValue();
}

void TextNodeDumper::VisitIntegerLiteral(const IntegerLiteral *Node) {
  bool isSigned = Node->getType()->isSignedIntegerType();
  ColorScope Color(OS, ShowColors, ValueColor);
  OS << " " << Node->getValue().toString(10, isSigned);
}

void TextNodeDumper::VisitFixedPointLiteral(const FixedPointLiteral *Node) {
  ColorScope Color(OS, ShowColors, ValueColor);
  OS << " " << Node->getValueAsString(/*Radix=*/10);
}

void TextNodeDumper::VisitFloatingLiteral(const FloatingLiteral *Node) {
  ColorScope Color(OS, ShowColors, ValueColor);
  OS << " " << Node->getValueAsApproximateDouble();
}

void TextNodeDumper::VisitStringLiteral(const StringLiteral *Str) {
  ColorScope Color(OS, ShowColors, ValueColor);
  OS << " ";
  Str->outputString(OS);
}

void TextNodeDumper::VisitInitListExpr(const InitListExpr *ILE) {
  if (auto *Field = ILE->getInitializedFieldInUnion()) {
    OS << " field ";
    dumpBareDeclRef(Field);
  }
}

void TextNodeDumper::VisitUnaryOperator(const UnaryOperator *Node) {
  OS << " " << (Node->isPostfix() ? "postfix" : "prefix") << " '"
     << UnaryOperator::getOpcodeStr(Node->getOpcode()) << "'";
  if (!Node->canOverflow())
    OS << " cannot overflow";
}

void TextNodeDumper::VisitUnaryExprOrTypeTraitExpr(
    const UnaryExprOrTypeTraitExpr *Node) {
  switch (Node->getKind()) {
  case UETT_SizeOf:
    OS << " sizeof";
    break;
  case UETT_AlignOf:
    OS << " alignof";
    break;
  case UETT_VecStep:
    OS << " vec_step";
    break;
  case UETT_OpenMPRequiredSimdAlign:
    OS << " __builtin_omp_required_simd_align";
    break;
  case UETT_PreferredAlignOf:
    OS << " __alignof";
    break;
  }
  if (Node->isArgumentType())
    dumpType(Node->getArgumentType());
}

void TextNodeDumper::VisitMemberExpr(const MemberExpr *Node) {
  OS << " " << (Node->isArrow() ? "->" : ".") << *Node->getMemberDecl();
  dumpPointer(Node->getMemberDecl());
}

void TextNodeDumper::VisitExtVectorElementExpr(
    const ExtVectorElementExpr *Node) {
  OS << " " << Node->getAccessor().getNameStart();
}

void TextNodeDumper::VisitBinaryOperator(const BinaryOperator *Node) {
  OS << " '" << BinaryOperator::getOpcodeStr(Node->getOpcode()) << "'";
}

void TextNodeDumper::VisitCompoundAssignOperator(
    const CompoundAssignOperator *Node) {
  OS << " '" << BinaryOperator::getOpcodeStr(Node->getOpcode())
     << "' ComputeLHSTy=";
  dumpBareType(Node->getComputationLHSType());
  OS << " ComputeResultTy=";
  dumpBareType(Node->getComputationResultType());
}

void TextNodeDumper::VisitAddrLabelExpr(const AddrLabelExpr *Node) {
  OS << " " << Node->getLabel()->getName();
  dumpPointer(Node->getLabel());
}

void TextNodeDumper::VisitCXXNamedCastExpr(const CXXNamedCastExpr *Node) {
  OS << " " << Node->getCastName() << "<"
     << Node->getTypeAsWritten().getAsString() << ">"
     << " <" << Node->getCastKindName();
  dumpBasePath(OS, Node);
  OS << ">";
}

void TextNodeDumper::VisitCXXBoolLiteralExpr(const CXXBoolLiteralExpr *Node) {
  OS << " " << (Node->getValue() ? "true" : "false");
}

void TextNodeDumper::VisitCXXThisExpr(const CXXThisExpr *Node) {
  OS << " this";
}

void TextNodeDumper::VisitCXXFunctionalCastExpr(
    const CXXFunctionalCastExpr *Node) {
  OS << " functional cast to " << Node->getTypeAsWritten().getAsString() << " <"
     << Node->getCastKindName() << ">";
}

void TextNodeDumper::VisitCXXUnresolvedConstructExpr(
    const CXXUnresolvedConstructExpr *Node) {
  dumpType(Node->getTypeAsWritten());
  if (Node->isListInitialization())
    OS << " list";
}

void TextNodeDumper::VisitCXXConstructExpr(const CXXConstructExpr *Node) {
  CXXConstructorDecl *Ctor = Node->getConstructor();
  dumpType(Ctor->getType());
  if (Node->isElidable())
    OS << " elidable";
  if (Node->isListInitialization())
    OS << " list";
  if (Node->isStdInitListInitialization())
    OS << " std::initializer_list";
  if (Node->requiresZeroInitialization())
    OS << " zeroing";
}

void TextNodeDumper::VisitCXXBindTemporaryExpr(
    const CXXBindTemporaryExpr *Node) {
  OS << " ";
  dumpCXXTemporary(Node->getTemporary());
}

void TextNodeDumper::VisitCXXNewExpr(const CXXNewExpr *Node) {
  if (Node->isGlobalNew())
    OS << " global";
  if (Node->isArray())
    OS << " array";
  if (Node->getOperatorNew()) {
    OS << ' ';
    dumpBareDeclRef(Node->getOperatorNew());
  }
  // We could dump the deallocation function used in case of error, but it's
  // usually not that interesting.
}

void TextNodeDumper::VisitCXXDeleteExpr(const CXXDeleteExpr *Node) {
  if (Node->isGlobalDelete())
    OS << " global";
  if (Node->isArrayForm())
    OS << " array";
  if (Node->getOperatorDelete()) {
    OS << ' ';
    dumpBareDeclRef(Node->getOperatorDelete());
  }
}

void TextNodeDumper::VisitMaterializeTemporaryExpr(
    const MaterializeTemporaryExpr *Node) {
  if (const ValueDecl *VD = Node->getExtendingDecl()) {
    OS << " extended by ";
    dumpBareDeclRef(VD);
  }
}

void TextNodeDumper::VisitExprWithCleanups(const ExprWithCleanups *Node) {
  for (unsigned i = 0, e = Node->getNumObjects(); i != e; ++i)
    dumpDeclRef(Node->getObject(i), "cleanup");
}

void TextNodeDumper::VisitSizeOfPackExpr(const SizeOfPackExpr *Node) {
  dumpPointer(Node->getPack());
  dumpName(Node->getPack());
}

void TextNodeDumper::VisitCXXDependentScopeMemberExpr(
    const CXXDependentScopeMemberExpr *Node) {
  OS << " " << (Node->isArrow() ? "->" : ".") << Node->getMember();
}

void TextNodeDumper::VisitObjCMessageExpr(const ObjCMessageExpr *Node) {
  OS << " selector=";
  Node->getSelector().print(OS);
  switch (Node->getReceiverKind()) {
  case ObjCMessageExpr::Instance:
    break;

  case ObjCMessageExpr::Class:
    OS << " class=";
    dumpBareType(Node->getClassReceiver());
    break;

  case ObjCMessageExpr::SuperInstance:
    OS << " super (instance)";
    break;

  case ObjCMessageExpr::SuperClass:
    OS << " super (class)";
    break;
  }
}

void TextNodeDumper::VisitObjCBoxedExpr(const ObjCBoxedExpr *Node) {
  if (auto *BoxingMethod = Node->getBoxingMethod()) {
    OS << " selector=";
    BoxingMethod->getSelector().print(OS);
  }
}

void TextNodeDumper::VisitObjCAtCatchStmt(const ObjCAtCatchStmt *Node) {
  if (!Node->getCatchParamDecl())
    OS << " catch all";
}

void TextNodeDumper::VisitObjCEncodeExpr(const ObjCEncodeExpr *Node) {
  dumpType(Node->getEncodedType());
}

void TextNodeDumper::VisitObjCSelectorExpr(const ObjCSelectorExpr *Node) {
  OS << " ";
  Node->getSelector().print(OS);
}

void TextNodeDumper::VisitObjCProtocolExpr(const ObjCProtocolExpr *Node) {
  OS << ' ' << *Node->getProtocol();
}

void TextNodeDumper::VisitObjCPropertyRefExpr(const ObjCPropertyRefExpr *Node) {
  if (Node->isImplicitProperty()) {
    OS << " Kind=MethodRef Getter=\"";
    if (Node->getImplicitPropertyGetter())
      Node->getImplicitPropertyGetter()->getSelector().print(OS);
    else
      OS << "(null)";

    OS << "\" Setter=\"";
    if (ObjCMethodDecl *Setter = Node->getImplicitPropertySetter())
      Setter->getSelector().print(OS);
    else
      OS << "(null)";
    OS << "\"";
  } else {
    OS << " Kind=PropertyRef Property=\"" << *Node->getExplicitProperty()
       << '"';
  }

  if (Node->isSuperReceiver())
    OS << " super";

  OS << " Messaging=";
  if (Node->isMessagingGetter() && Node->isMessagingSetter())
    OS << "Getter&Setter";
  else if (Node->isMessagingGetter())
    OS << "Getter";
  else if (Node->isMessagingSetter())
    OS << "Setter";
}

void TextNodeDumper::VisitObjCSubscriptRefExpr(
    const ObjCSubscriptRefExpr *Node) {
  if (Node->isArraySubscriptRefExpr())
    OS << " Kind=ArraySubscript GetterForArray=\"";
  else
    OS << " Kind=DictionarySubscript GetterForDictionary=\"";
  if (Node->getAtIndexMethodDecl())
    Node->getAtIndexMethodDecl()->getSelector().print(OS);
  else
    OS << "(null)";

  if (Node->isArraySubscriptRefExpr())
    OS << "\" SetterForArray=\"";
  else
    OS << "\" SetterForDictionary=\"";
  if (Node->setAtIndexMethodDecl())
    Node->setAtIndexMethodDecl()->getSelector().print(OS);
  else
    OS << "(null)";
}

void TextNodeDumper::VisitObjCBoolLiteralExpr(const ObjCBoolLiteralExpr *Node) {
  OS << " " << (Node->getValue() ? "__objc_yes" : "__objc_no");
}

void TextNodeDumper::VisitRValueReferenceType(const ReferenceType *T) {
  if (T->isSpelledAsLValue())
    OS << " written as lvalue reference";
}

void TextNodeDumper::VisitArrayType(const ArrayType *T) {
  switch (T->getSizeModifier()) {
  case ArrayType::Normal:
    break;
  case ArrayType::Static:
    OS << " static";
    break;
  case ArrayType::Star:
    OS << " *";
    break;
  }
  OS << " " << T->getIndexTypeQualifiers().getAsString();
}

void TextNodeDumper::VisitConstantArrayType(const ConstantArrayType *T) {
  OS << " " << T->getSize();
  VisitArrayType(T);
}

void TextNodeDumper::VisitVariableArrayType(const VariableArrayType *T) {
  OS << " ";
  dumpSourceRange(T->getBracketsRange());
  VisitArrayType(T);
}

void TextNodeDumper::VisitDependentSizedArrayType(
    const DependentSizedArrayType *T) {
  VisitArrayType(T);
  OS << " ";
  dumpSourceRange(T->getBracketsRange());
}

void TextNodeDumper::VisitDependentSizedExtVectorType(
    const DependentSizedExtVectorType *T) {
  OS << " ";
  dumpLocation(T->getAttributeLoc());
}

void TextNodeDumper::VisitVectorType(const VectorType *T) {
  switch (T->getVectorKind()) {
  case VectorType::GenericVector:
    break;
  case VectorType::AltiVecVector:
    OS << " altivec";
    break;
  case VectorType::AltiVecPixel:
    OS << " altivec pixel";
    break;
  case VectorType::AltiVecBool:
    OS << " altivec bool";
    break;
  case VectorType::NeonVector:
    OS << " neon";
    break;
  case VectorType::NeonPolyVector:
    OS << " neon poly";
    break;
  }
  OS << " " << T->getNumElements();
}

void TextNodeDumper::VisitFunctionType(const FunctionType *T) {
  auto EI = T->getExtInfo();
  if (EI.getNoReturn())
    OS << " noreturn";
  if (EI.getProducesResult())
    OS << " produces_result";
  if (EI.getHasRegParm())
    OS << " regparm " << EI.getRegParm();
  OS << " " << FunctionType::getNameForCallConv(EI.getCC());
}

void TextNodeDumper::VisitFunctionProtoType(const FunctionProtoType *T) {
  auto EPI = T->getExtProtoInfo();
  if (EPI.HasTrailingReturn)
    OS << " trailing_return";
  if (T->isConst())
    OS << " const";
  if (T->isVolatile())
    OS << " volatile";
  if (T->isRestrict())
    OS << " restrict";
  switch (EPI.RefQualifier) {
  case RQ_None:
    break;
  case RQ_LValue:
    OS << " &";
    break;
  case RQ_RValue:
    OS << " &&";
    break;
  }
  // FIXME: Exception specification.
  // FIXME: Consumed parameters.
  VisitFunctionType(T);
}

void TextNodeDumper::VisitUnresolvedUsingType(const UnresolvedUsingType *T) {
  dumpDeclRef(T->getDecl());
}

void TextNodeDumper::VisitTypedefType(const TypedefType *T) {
  dumpDeclRef(T->getDecl());
}

void TextNodeDumper::VisitUnaryTransformType(const UnaryTransformType *T) {
  switch (T->getUTTKind()) {
  case UnaryTransformType::EnumUnderlyingType:
    OS << " underlying_type";
    break;
  }
}

void TextNodeDumper::VisitTagType(const TagType *T) {
  dumpDeclRef(T->getDecl());
}

void TextNodeDumper::VisitTemplateTypeParmType(const TemplateTypeParmType *T) {
  OS << " depth " << T->getDepth() << " index " << T->getIndex();
  if (T->isParameterPack())
    OS << " pack";
  dumpDeclRef(T->getDecl());
}

void TextNodeDumper::VisitAutoType(const AutoType *T) {
  if (T->isDecltypeAuto())
    OS << " decltype(auto)";
  if (!T->isDeduced())
    OS << " undeduced";
}

void TextNodeDumper::VisitTemplateSpecializationType(
    const TemplateSpecializationType *T) {
  if (T->isTypeAlias())
    OS << " alias";
  OS << " ";
  T->getTemplateName().dump(OS);
}

void TextNodeDumper::VisitInjectedClassNameType(
    const InjectedClassNameType *T) {
  dumpDeclRef(T->getDecl());
}

void TextNodeDumper::VisitObjCInterfaceType(const ObjCInterfaceType *T) {
  dumpDeclRef(T->getDecl());
}

void TextNodeDumper::VisitPackExpansionType(const PackExpansionType *T) {
  if (auto N = T->getNumExpansions())
    OS << " expansions " << *N;
}
