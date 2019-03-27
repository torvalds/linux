//===--- RefactoringOptionVisitor.h - Clang refactoring library -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_REFACTOR_REFACTORING_OPTION_VISITOR_H
#define LLVM_CLANG_TOOLING_REFACTOR_REFACTORING_OPTION_VISITOR_H

#include "clang/Basic/LLVM.h"
#include <type_traits>

namespace clang {
namespace tooling {

class RefactoringOption;

/// An interface that declares functions that handle different refactoring
/// option types.
///
/// A valid refactoring option type must have a corresponding \c visit
/// declaration in this interface.
class RefactoringOptionVisitor {
public:
  virtual ~RefactoringOptionVisitor() {}

  virtual void visit(const RefactoringOption &Opt,
                     Optional<std::string> &Value) = 0;
};

namespace traits {
namespace internal {

template <typename T> struct HasHandle {
private:
  template <typename ClassT>
  static auto check(ClassT *) -> typename std::is_same<
      decltype(std::declval<RefactoringOptionVisitor>().visit(
          std::declval<RefactoringOption>(), *std::declval<Optional<T> *>())),
      void>::type;

  template <typename> static std::false_type check(...);

public:
  using Type = decltype(check<RefactoringOptionVisitor>(nullptr));
};

} // end namespace internal

/// A type trait that returns true iff the given type is a type that can be
/// stored in a refactoring option.
template <typename T>
struct IsValidOptionType : internal::HasHandle<T>::Type {};

} // end namespace traits
} // end namespace tooling
} // end namespace clang

#endif // LLVM_CLANG_TOOLING_REFACTOR_REFACTORING_OPTION_VISITOR_H
