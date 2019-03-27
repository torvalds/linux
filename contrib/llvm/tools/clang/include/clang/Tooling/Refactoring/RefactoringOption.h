//===--- RefactoringOption.h - Clang refactoring library ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_REFACTOR_REFACTORING_OPTION_H
#define LLVM_CLANG_TOOLING_REFACTOR_REFACTORING_OPTION_H

#include "clang/Basic/LLVM.h"
#include <memory>
#include <type_traits>

namespace clang {
namespace tooling {

class RefactoringOptionVisitor;

/// A refactoring option is an interface that describes a value that
/// has an impact on the outcome of a refactoring.
///
/// Refactoring options can be specified using command-line arguments when
/// the clang-refactor tool is used.
class RefactoringOption {
public:
  virtual ~RefactoringOption() {}

  /// Returns the name of the refactoring option.
  ///
  /// Each refactoring option must have a unique name.
  virtual StringRef getName() const = 0;

  virtual StringRef getDescription() const = 0;

  /// True when this option must be specified before invoking the refactoring
  /// action.
  virtual bool isRequired() const = 0;

  /// Invokes the \c visit method in the option consumer that's appropriate
  /// for the option's value type.
  ///
  /// For example, if the option stores a string value, this method will
  /// invoke the \c visit method with a reference to an std::string value.
  virtual void passToVisitor(RefactoringOptionVisitor &Visitor) = 0;
};

/// Constructs a refactoring option of the given type.
///
/// The ownership of options is shared among requirements that use it because
/// one option can be used by multiple rules in a refactoring action.
template <typename OptionType>
std::shared_ptr<OptionType> createRefactoringOption() {
  static_assert(std::is_base_of<RefactoringOption, OptionType>::value,
                "invalid option type");
  return std::make_shared<OptionType>();
}

} // end namespace tooling
} // end namespace clang

#endif // LLVM_CLANG_TOOLING_REFACTOR_REFACTORING_OPTION_H
