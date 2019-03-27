//===--- RefactoringActions.cpp - Constructs refactoring actions ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/Refactoring/Extract/Extract.h"
#include "clang/Tooling/Refactoring/RefactoringAction.h"
#include "clang/Tooling/Refactoring/RefactoringOptions.h"
#include "clang/Tooling/Refactoring/Rename/RenamingAction.h"

namespace clang {
namespace tooling {

namespace {

class DeclNameOption final : public OptionalRefactoringOption<std::string> {
public:
  StringRef getName() const { return "name"; }
  StringRef getDescription() const {
    return "Name of the extracted declaration";
  }
};

// FIXME: Rewrite the Actions to avoid duplication of descriptions/names with
// rules.
class ExtractRefactoring final : public RefactoringAction {
public:
  StringRef getCommand() const override { return "extract"; }

  StringRef getDescription() const override {
    return "(WIP action; use with caution!) Extracts code into a new function";
  }

  /// Returns a set of refactoring actions rules that are defined by this
  /// action.
  RefactoringActionRules createActionRules() const override {
    RefactoringActionRules Rules;
    Rules.push_back(createRefactoringActionRule<ExtractFunction>(
        CodeRangeASTSelectionRequirement(),
        OptionRequirement<DeclNameOption>()));
    return Rules;
  }
};

class OldQualifiedNameOption : public RequiredRefactoringOption<std::string> {
public:
  StringRef getName() const override { return "old-qualified-name"; }
  StringRef getDescription() const override {
    return "The old qualified name to be renamed";
  }
};

class NewQualifiedNameOption : public RequiredRefactoringOption<std::string> {
public:
  StringRef getName() const override { return "new-qualified-name"; }
  StringRef getDescription() const override {
    return "The new qualified name to change the symbol to";
  }
};

class NewNameOption : public RequiredRefactoringOption<std::string> {
public:
  StringRef getName() const override { return "new-name"; }
  StringRef getDescription() const override {
    return "The new name to change the symbol to";
  }
};

// FIXME: Rewrite the Actions to avoid duplication of descriptions/names with
// rules.
class LocalRename final : public RefactoringAction {
public:
  StringRef getCommand() const override { return "local-rename"; }

  StringRef getDescription() const override {
    return "Finds and renames symbols in code with no indexer support";
  }

  /// Returns a set of refactoring actions rules that are defined by this
  /// action.
  RefactoringActionRules createActionRules() const override {
    RefactoringActionRules Rules;
    Rules.push_back(createRefactoringActionRule<RenameOccurrences>(
        SourceRangeSelectionRequirement(), OptionRequirement<NewNameOption>()));
    // FIXME: Use NewNameOption.
    Rules.push_back(createRefactoringActionRule<QualifiedRenameRule>(
        OptionRequirement<OldQualifiedNameOption>(),
        OptionRequirement<NewQualifiedNameOption>()));
    return Rules;
  }
};

} // end anonymous namespace

std::vector<std::unique_ptr<RefactoringAction>> createRefactoringActions() {
  std::vector<std::unique_ptr<RefactoringAction>> Actions;

  Actions.push_back(llvm::make_unique<LocalRename>());
  Actions.push_back(llvm::make_unique<ExtractRefactoring>());

  return Actions;
}

RefactoringActionRules RefactoringAction::createActiveActionRules() {
  // FIXME: Filter out rules that are not supported by a particular client.
  return createActionRules();
}

} // end namespace tooling
} // end namespace clang
