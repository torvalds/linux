//===--- RefactoringActionRule.h - Clang refactoring library -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_REFACTOR_REFACTORING_ACTION_RULE_H
#define LLVM_CLANG_TOOLING_REFACTOR_REFACTORING_ACTION_RULE_H

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/StringRef.h"
#include <vector>

namespace clang {
namespace tooling {

class RefactoringOptionVisitor;
class RefactoringResultConsumer;
class RefactoringRuleContext;

struct RefactoringDescriptor {
  /// A unique identifier for the specific refactoring.
  StringRef Name;
  /// A human readable title for the refactoring.
  StringRef Title;
  /// A human readable description of what the refactoring does.
  StringRef Description;
};

/// A common refactoring action rule interface that defines the 'invoke'
/// function that performs the refactoring operation (either fully or
/// partially).
class RefactoringActionRuleBase {
public:
  virtual ~RefactoringActionRuleBase() {}

  /// Initiates and performs a specific refactoring action.
  ///
  /// The specific rule will invoke an appropriate \c handle method on a
  /// consumer to propagate the result of the refactoring action.
  virtual void invoke(RefactoringResultConsumer &Consumer,
                      RefactoringRuleContext &Context) = 0;

  /// Returns the structure that describes the refactoring.
  // static const RefactoringDescriptor &describe() = 0;
};

/// A refactoring action rule is a wrapper class around a specific refactoring
/// action rule (SourceChangeRefactoringRule, etc) that, in addition to invoking
/// the action, describes the requirements that determine when the action can be
/// initiated.
class RefactoringActionRule : public RefactoringActionRuleBase {
public:
  /// Returns true when the rule has a source selection requirement that has
  /// to be fulfilled before refactoring can be performed.
  virtual bool hasSelectionRequirement() = 0;

  /// Traverses each refactoring option used by the rule and invokes the
  /// \c visit callback in the consumer for each option.
  ///
  /// Options are visited in the order of use, e.g. if a rule has two
  /// requirements that use options, the options from the first requirement
  /// are visited before the options in the second requirement.
  virtual void visitRefactoringOptions(RefactoringOptionVisitor &Visitor) = 0;
};

} // end namespace tooling
} // end namespace clang

#endif // LLVM_CLANG_TOOLING_REFACTOR_REFACTORING_ACTION_RULE_H
