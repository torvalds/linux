//===--- TextNodeDumper.h - Printing of AST nodes -------------------------===//
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

#ifndef LLVM_CLANG_AST_TEXTNODEDUMPER_H
#define LLVM_CLANG_AST_TEXTNODEDUMPER_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTDumperUtils.h"
#include "clang/AST/AttrVisitor.h"
#include "clang/AST/CommentCommandTraits.h"
#include "clang/AST/CommentVisitor.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/AST/TemplateArgumentVisitor.h"
#include "clang/AST/TypeVisitor.h"

namespace clang {

class TextTreeStructure {
  raw_ostream &OS;
  const bool ShowColors;

  /// Pending[i] is an action to dump an entity at level i.
  llvm::SmallVector<std::function<void(bool IsLastChild)>, 32> Pending;

  /// Indicates whether we're at the top level.
  bool TopLevel = true;

  /// Indicates if we're handling the first child after entering a new depth.
  bool FirstChild = true;

  /// Prefix for currently-being-dumped entity.
  std::string Prefix;

public:
  /// Add a child of the current node.  Calls DoAddChild without arguments
  template <typename Fn> void AddChild(Fn DoAddChild) {
    return AddChild("", DoAddChild);
  }

  /// Add a child of the current node with an optional label.
  /// Calls DoAddChild without arguments.
  template <typename Fn> void AddChild(StringRef Label, Fn DoAddChild) {
    // If we're at the top level, there's nothing interesting to do; just
    // run the dumper.
    if (TopLevel) {
      TopLevel = false;
      DoAddChild();
      while (!Pending.empty()) {
        Pending.back()(true);
        Pending.pop_back();
      }
      Prefix.clear();
      OS << "\n";
      TopLevel = true;
      return;
    }

    // We need to capture an owning-string in the lambda because the lambda
    // is invoked in a deferred manner.
    std::string LabelStr = Label;
    auto DumpWithIndent = [this, DoAddChild, LabelStr](bool IsLastChild) {
      // Print out the appropriate tree structure and work out the prefix for
      // children of this node. For instance:
      //
      //   A        Prefix = ""
      //   |-B      Prefix = "| "
      //   | `-C    Prefix = "|   "
      //   `-D      Prefix = "  "
      //     |-E    Prefix = "  | "
      //     `-F    Prefix = "    "
      //   G        Prefix = ""
      //
      // Note that the first level gets no prefix.
      {
        OS << '\n';
        ColorScope Color(OS, ShowColors, IndentColor);
        OS << Prefix << (IsLastChild ? '`' : '|') << '-';
        if (!LabelStr.empty())
          OS << LabelStr << ": ";

        this->Prefix.push_back(IsLastChild ? ' ' : '|');
        this->Prefix.push_back(' ');
      }

      FirstChild = true;
      unsigned Depth = Pending.size();

      DoAddChild();

      // If any children are left, they're the last at their nesting level.
      // Dump those ones out now.
      while (Depth < Pending.size()) {
        Pending.back()(true);
        this->Pending.pop_back();
      }

      // Restore the old prefix.
      this->Prefix.resize(Prefix.size() - 2);
    };

    if (FirstChild) {
      Pending.push_back(std::move(DumpWithIndent));
    } else {
      Pending.back()(false);
      Pending.back() = std::move(DumpWithIndent);
    }
    FirstChild = false;
  }

  TextTreeStructure(raw_ostream &OS, bool ShowColors)
      : OS(OS), ShowColors(ShowColors) {}
};

class TextNodeDumper
    : public TextTreeStructure,
      public comments::ConstCommentVisitor<TextNodeDumper, void,
                                           const comments::FullComment *>,
      public ConstAttrVisitor<TextNodeDumper>,
      public ConstTemplateArgumentVisitor<TextNodeDumper>,
      public ConstStmtVisitor<TextNodeDumper>,
      public TypeVisitor<TextNodeDumper> {
  raw_ostream &OS;
  const bool ShowColors;

  /// Keep track of the last location we print out so that we can
  /// print out deltas from then on out.
  const char *LastLocFilename = "";
  unsigned LastLocLine = ~0U;

  const SourceManager *SM;

  /// The policy to use for printing; can be defaulted.
  PrintingPolicy PrintPolicy;

  const comments::CommandTraits *Traits;

  const char *getCommandName(unsigned CommandID);

public:
  TextNodeDumper(raw_ostream &OS, bool ShowColors, const SourceManager *SM,
                 const PrintingPolicy &PrintPolicy,
                 const comments::CommandTraits *Traits);

  void Visit(const comments::Comment *C, const comments::FullComment *FC);

  void Visit(const Attr *A);

  void Visit(const TemplateArgument &TA, SourceRange R,
             const Decl *From = nullptr, StringRef Label = {});

  void Visit(const Stmt *Node);

  void Visit(const Type *T);

  void Visit(QualType T);

  void Visit(const Decl *D);

  void Visit(const CXXCtorInitializer *Init);

  void Visit(const OMPClause *C);

  void Visit(const BlockDecl::Capture &C);

  void dumpPointer(const void *Ptr);
  void dumpLocation(SourceLocation Loc);
  void dumpSourceRange(SourceRange R);
  void dumpBareType(QualType T, bool Desugar = true);
  void dumpType(QualType T);
  void dumpBareDeclRef(const Decl *D);
  void dumpName(const NamedDecl *ND);
  void dumpAccessSpecifier(AccessSpecifier AS);

  void dumpDeclRef(const Decl *D, StringRef Label = {});

  void visitTextComment(const comments::TextComment *C,
                        const comments::FullComment *);
  void visitInlineCommandComment(const comments::InlineCommandComment *C,
                                 const comments::FullComment *);
  void visitHTMLStartTagComment(const comments::HTMLStartTagComment *C,
                                const comments::FullComment *);
  void visitHTMLEndTagComment(const comments::HTMLEndTagComment *C,
                              const comments::FullComment *);
  void visitBlockCommandComment(const comments::BlockCommandComment *C,
                                const comments::FullComment *);
  void visitParamCommandComment(const comments::ParamCommandComment *C,
                                const comments::FullComment *FC);
  void visitTParamCommandComment(const comments::TParamCommandComment *C,
                                 const comments::FullComment *FC);
  void visitVerbatimBlockComment(const comments::VerbatimBlockComment *C,
                                 const comments::FullComment *);
  void
  visitVerbatimBlockLineComment(const comments::VerbatimBlockLineComment *C,
                                const comments::FullComment *);
  void visitVerbatimLineComment(const comments::VerbatimLineComment *C,
                                const comments::FullComment *);

// Implements Visit methods for Attrs.
#include "clang/AST/AttrTextNodeDump.inc"

  void VisitNullTemplateArgument(const TemplateArgument &TA);
  void VisitTypeTemplateArgument(const TemplateArgument &TA);
  void VisitDeclarationTemplateArgument(const TemplateArgument &TA);
  void VisitNullPtrTemplateArgument(const TemplateArgument &TA);
  void VisitIntegralTemplateArgument(const TemplateArgument &TA);
  void VisitTemplateTemplateArgument(const TemplateArgument &TA);
  void VisitTemplateExpansionTemplateArgument(const TemplateArgument &TA);
  void VisitExpressionTemplateArgument(const TemplateArgument &TA);
  void VisitPackTemplateArgument(const TemplateArgument &TA);

  void VisitIfStmt(const IfStmt *Node);
  void VisitSwitchStmt(const SwitchStmt *Node);
  void VisitWhileStmt(const WhileStmt *Node);
  void VisitLabelStmt(const LabelStmt *Node);
  void VisitGotoStmt(const GotoStmt *Node);
  void VisitCaseStmt(const CaseStmt *Node);
  void VisitCallExpr(const CallExpr *Node);
  void VisitCastExpr(const CastExpr *Node);
  void VisitImplicitCastExpr(const ImplicitCastExpr *Node);
  void VisitDeclRefExpr(const DeclRefExpr *Node);
  void VisitPredefinedExpr(const PredefinedExpr *Node);
  void VisitCharacterLiteral(const CharacterLiteral *Node);
  void VisitIntegerLiteral(const IntegerLiteral *Node);
  void VisitFixedPointLiteral(const FixedPointLiteral *Node);
  void VisitFloatingLiteral(const FloatingLiteral *Node);
  void VisitStringLiteral(const StringLiteral *Str);
  void VisitInitListExpr(const InitListExpr *ILE);
  void VisitUnaryOperator(const UnaryOperator *Node);
  void VisitUnaryExprOrTypeTraitExpr(const UnaryExprOrTypeTraitExpr *Node);
  void VisitMemberExpr(const MemberExpr *Node);
  void VisitExtVectorElementExpr(const ExtVectorElementExpr *Node);
  void VisitBinaryOperator(const BinaryOperator *Node);
  void VisitCompoundAssignOperator(const CompoundAssignOperator *Node);
  void VisitAddrLabelExpr(const AddrLabelExpr *Node);
  void VisitCXXNamedCastExpr(const CXXNamedCastExpr *Node);
  void VisitCXXBoolLiteralExpr(const CXXBoolLiteralExpr *Node);
  void VisitCXXThisExpr(const CXXThisExpr *Node);
  void VisitCXXFunctionalCastExpr(const CXXFunctionalCastExpr *Node);
  void VisitCXXUnresolvedConstructExpr(const CXXUnresolvedConstructExpr *Node);
  void VisitCXXConstructExpr(const CXXConstructExpr *Node);
  void VisitCXXBindTemporaryExpr(const CXXBindTemporaryExpr *Node);
  void VisitCXXNewExpr(const CXXNewExpr *Node);
  void VisitCXXDeleteExpr(const CXXDeleteExpr *Node);
  void VisitMaterializeTemporaryExpr(const MaterializeTemporaryExpr *Node);
  void VisitExprWithCleanups(const ExprWithCleanups *Node);
  void VisitUnresolvedLookupExpr(const UnresolvedLookupExpr *Node);
  void VisitSizeOfPackExpr(const SizeOfPackExpr *Node);
  void
  VisitCXXDependentScopeMemberExpr(const CXXDependentScopeMemberExpr *Node);
  void VisitObjCAtCatchStmt(const ObjCAtCatchStmt *Node);
  void VisitObjCEncodeExpr(const ObjCEncodeExpr *Node);
  void VisitObjCMessageExpr(const ObjCMessageExpr *Node);
  void VisitObjCBoxedExpr(const ObjCBoxedExpr *Node);
  void VisitObjCSelectorExpr(const ObjCSelectorExpr *Node);
  void VisitObjCProtocolExpr(const ObjCProtocolExpr *Node);
  void VisitObjCPropertyRefExpr(const ObjCPropertyRefExpr *Node);
  void VisitObjCSubscriptRefExpr(const ObjCSubscriptRefExpr *Node);
  void VisitObjCIvarRefExpr(const ObjCIvarRefExpr *Node);
  void VisitObjCBoolLiteralExpr(const ObjCBoolLiteralExpr *Node);

  void VisitRValueReferenceType(const ReferenceType *T);
  void VisitArrayType(const ArrayType *T);
  void VisitConstantArrayType(const ConstantArrayType *T);
  void VisitVariableArrayType(const VariableArrayType *T);
  void VisitDependentSizedArrayType(const DependentSizedArrayType *T);
  void VisitDependentSizedExtVectorType(const DependentSizedExtVectorType *T);
  void VisitVectorType(const VectorType *T);
  void VisitFunctionType(const FunctionType *T);
  void VisitFunctionProtoType(const FunctionProtoType *T);
  void VisitUnresolvedUsingType(const UnresolvedUsingType *T);
  void VisitTypedefType(const TypedefType *T);
  void VisitUnaryTransformType(const UnaryTransformType *T);
  void VisitTagType(const TagType *T);
  void VisitTemplateTypeParmType(const TemplateTypeParmType *T);
  void VisitAutoType(const AutoType *T);
  void VisitTemplateSpecializationType(const TemplateSpecializationType *T);
  void VisitInjectedClassNameType(const InjectedClassNameType *T);
  void VisitObjCInterfaceType(const ObjCInterfaceType *T);
  void VisitPackExpansionType(const PackExpansionType *T);

private:
  void dumpCXXTemporary(const CXXTemporary *Temporary);
};

} // namespace clang

#endif // LLVM_CLANG_AST_TEXTNODEDUMPER_H
