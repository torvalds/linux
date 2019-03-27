//===--- Extract.h - Clang refactoring library ----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_REFACTOR_EXTRACT_EXTRACT_H
#define LLVM_CLANG_TOOLING_REFACTOR_EXTRACT_EXTRACT_H

#include "clang/Tooling/Refactoring/ASTSelection.h"
#include "clang/Tooling/Refactoring/RefactoringActionRules.h"

namespace clang {
namespace tooling {

/// An "Extract Function" refactoring moves code into a new function that's
/// then called from the place where the original code was.
class ExtractFunction final : public SourceChangeRefactoringRule {
public:
  /// Initiates the extract function refactoring operation.
  ///
  /// \param Code     The selected set of statements.
  /// \param DeclName The name name of the extract function. If None,
  ///                 "extracted" is used.
  static Expected<ExtractFunction> initiate(RefactoringRuleContext &Context,
                                            CodeRangeASTSelection Code,
                                            Optional<std::string> DeclName);

  static const RefactoringDescriptor &describe();

private:
  ExtractFunction(CodeRangeASTSelection Code, Optional<std::string> DeclName)
      : Code(std::move(Code)),
        DeclName(DeclName ? std::move(*DeclName) : "extracted") {}

  Expected<AtomicChanges>
  createSourceReplacements(RefactoringRuleContext &Context) override;

  CodeRangeASTSelection Code;

  // FIXME: Account for naming collisions:
  //  - error when name is specified by user.
  //  - rename to "extractedN" when name is implicit.
  std::string DeclName;
};

} // end namespace tooling
} // end namespace clang

#endif // LLVM_CLANG_TOOLING_REFACTOR_EXTRACT_EXTRACT_H
