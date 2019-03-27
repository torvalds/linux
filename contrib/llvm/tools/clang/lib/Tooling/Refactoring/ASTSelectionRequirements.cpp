//===--- ASTSelectionRequirements.cpp - Clang refactoring library ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/Refactoring/RefactoringActionRuleRequirements.h"

using namespace clang;
using namespace tooling;

Expected<SelectedASTNode>
ASTSelectionRequirement::evaluate(RefactoringRuleContext &Context) const {
  // FIXME: Memoize so that selection is evaluated only once.
  Expected<SourceRange> Range =
      SourceRangeSelectionRequirement::evaluate(Context);
  if (!Range)
    return Range.takeError();

  Optional<SelectedASTNode> Selection =
      findSelectedASTNodes(Context.getASTContext(), *Range);
  if (!Selection)
    return Context.createDiagnosticError(
        Range->getBegin(), diag::err_refactor_selection_invalid_ast);
  return std::move(*Selection);
}

Expected<CodeRangeASTSelection> CodeRangeASTSelectionRequirement::evaluate(
    RefactoringRuleContext &Context) const {
  // FIXME: Memoize so that selection is evaluated only once.
  Expected<SelectedASTNode> ASTSelection =
      ASTSelectionRequirement::evaluate(Context);
  if (!ASTSelection)
    return ASTSelection.takeError();
  std::unique_ptr<SelectedASTNode> StoredSelection =
      llvm::make_unique<SelectedASTNode>(std::move(*ASTSelection));
  Optional<CodeRangeASTSelection> CodeRange = CodeRangeASTSelection::create(
      Context.getSelectionRange(), *StoredSelection);
  if (!CodeRange)
    return Context.createDiagnosticError(
        Context.getSelectionRange().getBegin(),
        diag::err_refactor_selection_invalid_ast);
  Context.setASTSelection(std::move(StoredSelection));
  return std::move(*CodeRange);
}
