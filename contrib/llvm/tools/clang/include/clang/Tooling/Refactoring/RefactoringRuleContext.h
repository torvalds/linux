//===--- RefactoringRuleContext.h - Clang refactoring library -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_REFACTOR_REFACTORING_RULE_CONTEXT_H
#define LLVM_CLANG_TOOLING_REFACTOR_REFACTORING_RULE_CONTEXT_H

#include "clang/Basic/DiagnosticError.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Tooling/Refactoring/ASTSelection.h"

namespace clang {

class ASTContext;

namespace tooling {

/// The refactoring rule context stores all of the inputs that might be needed
/// by a refactoring action rule. It can create the specialized
/// \c ASTRefactoringOperation or \c PreprocessorRefactoringOperation values
/// that can be used by the refactoring action rules.
///
/// The following inputs are stored by the operation:
///
///   - SourceManager: a reference to a valid source manager.
///
///   - SelectionRange: an optional source selection ranges that can be used
///     to represent a selection in an editor.
class RefactoringRuleContext {
public:
  RefactoringRuleContext(const SourceManager &SM) : SM(SM) {}

  const SourceManager &getSources() const { return SM; }

  /// Returns the current source selection range as set by the
  /// refactoring engine. Can be invalid.
  SourceRange getSelectionRange() const { return SelectionRange; }

  void setSelectionRange(SourceRange R) { SelectionRange = R; }

  bool hasASTContext() const { return AST; }

  ASTContext &getASTContext() const {
    assert(AST && "no AST!");
    return *AST;
  }

  void setASTContext(ASTContext &Context) { AST = &Context; }

  /// Creates an llvm::Error value that contains a diagnostic.
  ///
  /// The errors should not outlive the context.
  llvm::Error createDiagnosticError(SourceLocation Loc, unsigned DiagID) {
    return DiagnosticError::create(Loc, PartialDiagnostic(DiagID, DiagStorage));
  }

  llvm::Error createDiagnosticError(unsigned DiagID) {
    return createDiagnosticError(SourceLocation(), DiagID);
  }

  void setASTSelection(std::unique_ptr<SelectedASTNode> Node) {
    ASTNodeSelection = std::move(Node);
  }

private:
  /// The source manager for the translation unit / file on which a refactoring
  /// action might operate on.
  const SourceManager &SM;
  /// An optional source selection range that's commonly used to represent
  /// a selection in an editor.
  SourceRange SelectionRange;
  /// An optional AST for the translation unit on which a refactoring action
  /// might operate on.
  ASTContext *AST = nullptr;
  /// The allocator for diagnostics.
  PartialDiagnostic::StorageAllocator DiagStorage;

  // FIXME: Remove when memoized.
  std::unique_ptr<SelectedASTNode> ASTNodeSelection;
};

} // end namespace tooling
} // end namespace clang

#endif // LLVM_CLANG_TOOLING_REFACTOR_REFACTORING_RULE_CONTEXT_H
