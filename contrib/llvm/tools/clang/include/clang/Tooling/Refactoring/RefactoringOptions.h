//===--- RefactoringOptions.h - Clang refactoring library -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_REFACTOR_REFACTORING_OPTIONS_H
#define LLVM_CLANG_TOOLING_REFACTOR_REFACTORING_OPTIONS_H

#include "clang/Basic/LLVM.h"
#include "clang/Tooling/Refactoring/RefactoringActionRuleRequirements.h"
#include "clang/Tooling/Refactoring/RefactoringOption.h"
#include "clang/Tooling/Refactoring/RefactoringOptionVisitor.h"
#include "llvm/Support/Error.h"
#include <type_traits>

namespace clang {
namespace tooling {

/// A refactoring option that stores a value of type \c T.
template <typename T, typename = typename std::enable_if<
                          traits::IsValidOptionType<T>::value>::type>
class OptionalRefactoringOption : public RefactoringOption {
public:
  void passToVisitor(RefactoringOptionVisitor &Visitor) final override {
    Visitor.visit(*this, Value);
  }

  bool isRequired() const override { return false; }

  using ValueType = Optional<T>;

  const ValueType &getValue() const { return Value; }

protected:
  Optional<T> Value;
};

/// A required refactoring option that stores a value of type \c T.
template <typename T, typename = typename std::enable_if<
                          traits::IsValidOptionType<T>::value>::type>
class RequiredRefactoringOption : public OptionalRefactoringOption<T> {
public:
  using ValueType = T;

  const ValueType &getValue() const {
    return *OptionalRefactoringOption<T>::Value;
  }
  bool isRequired() const final override { return true; }
};

} // end namespace tooling
} // end namespace clang

#endif // LLVM_CLANG_TOOLING_REFACTOR_REFACTORING_OPTIONS_H
