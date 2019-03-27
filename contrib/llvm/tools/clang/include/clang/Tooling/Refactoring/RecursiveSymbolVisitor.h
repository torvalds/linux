//===--- RecursiveSymbolVisitor.h - Clang refactoring library -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// A wrapper class around \c RecursiveASTVisitor that visits each
/// occurrences of a named symbol.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_REFACTOR_RECURSIVE_SYMBOL_VISITOR_H
#define LLVM_CLANG_TOOLING_REFACTOR_RECURSIVE_SYMBOL_VISITOR_H

#include "clang/AST/AST.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Lex/Lexer.h"

namespace clang {
namespace tooling {

/// Traverses the AST and visits the occurrence of each named symbol in the
/// given nodes.
template <typename T>
class RecursiveSymbolVisitor
    : public RecursiveASTVisitor<RecursiveSymbolVisitor<T>> {
  using BaseType = RecursiveASTVisitor<RecursiveSymbolVisitor<T>>;

public:
  RecursiveSymbolVisitor(const SourceManager &SM, const LangOptions &LangOpts)
      : SM(SM), LangOpts(LangOpts) {}

  bool visitSymbolOccurrence(const NamedDecl *ND,
                             ArrayRef<SourceRange> NameRanges) {
    return true;
  }

  // Declaration visitors:

  bool VisitNamedDecl(const NamedDecl *D) {
    return isa<CXXConversionDecl>(D) ? true : visit(D, D->getLocation());
  }

  bool VisitCXXConstructorDecl(const CXXConstructorDecl *CD) {
    for (const auto *Initializer : CD->inits()) {
      // Ignore implicit initializers.
      if (!Initializer->isWritten())
        continue;
      if (const FieldDecl *FD = Initializer->getMember()) {
        if (!visit(FD, Initializer->getSourceLocation(),
                   Lexer::getLocForEndOfToken(Initializer->getSourceLocation(),
                                              0, SM, LangOpts)))
          return false;
      }
    }
    return true;
  }

  // Expression visitors:

  bool VisitDeclRefExpr(const DeclRefExpr *Expr) {
    return visit(Expr->getFoundDecl(), Expr->getLocation());
  }

  bool VisitMemberExpr(const MemberExpr *Expr) {
    return visit(Expr->getFoundDecl().getDecl(), Expr->getMemberLoc());
  }

  bool VisitOffsetOfExpr(const OffsetOfExpr *S) {
    for (unsigned I = 0, E = S->getNumComponents(); I != E; ++I) {
      const OffsetOfNode &Component = S->getComponent(I);
      if (Component.getKind() == OffsetOfNode::Field) {
        if (!visit(Component.getField(), Component.getEndLoc()))
          return false;
      }
      // FIXME: Try to resolve dependent field references.
    }
    return true;
  }

  // Other visitors:

  bool VisitTypeLoc(const TypeLoc Loc) {
    const SourceLocation TypeBeginLoc = Loc.getBeginLoc();
    const SourceLocation TypeEndLoc =
        Lexer::getLocForEndOfToken(TypeBeginLoc, 0, SM, LangOpts);
    if (const auto *TemplateTypeParm =
            dyn_cast<TemplateTypeParmType>(Loc.getType())) {
      if (!visit(TemplateTypeParm->getDecl(), TypeBeginLoc, TypeEndLoc))
        return false;
    }
    if (const auto *TemplateSpecType =
            dyn_cast<TemplateSpecializationType>(Loc.getType())) {
      if (!visit(TemplateSpecType->getTemplateName().getAsTemplateDecl(),
                 TypeBeginLoc, TypeEndLoc))
        return false;
    }
    return visit(Loc.getType()->getAsCXXRecordDecl(), TypeBeginLoc, TypeEndLoc);
  }

  bool TraverseNestedNameSpecifierLoc(NestedNameSpecifierLoc NNS) {
    // The base visitor will visit NNSL prefixes, so we should only look at
    // the current NNS.
    if (NNS) {
      const NamespaceDecl *ND = NNS.getNestedNameSpecifier()->getAsNamespace();
      if (!visit(ND, NNS.getLocalBeginLoc(), NNS.getLocalEndLoc()))
        return false;
    }
    return BaseType::TraverseNestedNameSpecifierLoc(NNS);
  }

private:
  const SourceManager &SM;
  const LangOptions &LangOpts;

  bool visit(const NamedDecl *ND, SourceLocation BeginLoc,
             SourceLocation EndLoc) {
    return static_cast<T *>(this)->visitSymbolOccurrence(
        ND, SourceRange(BeginLoc, EndLoc));
  }
  bool visit(const NamedDecl *ND, SourceLocation Loc) {
    return visit(ND, Loc,
                 Loc.getLocWithOffset(ND->getNameAsString().length() - 1));
  }
};

} // end namespace tooling
} // end namespace clang

#endif // LLVM_CLANG_TOOLING_REFACTOR_RECURSIVE_SYMBOL_VISITOR_H
