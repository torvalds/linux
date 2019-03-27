//===- ThreadSafety.h -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//
// A intra-procedural analysis for thread safety (e.g. deadlocks and race
// conditions), based off of an annotation system.
//
// See http://clang.llvm.org/docs/LanguageExtensions.html#thread-safety-annotation-checking
// for more information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_ANALYSES_THREADSAFETY_H
#define LLVM_CLANG_ANALYSIS_ANALYSES_THREADSAFETY_H

#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/StringRef.h"

namespace clang {

class AnalysisDeclContext;
class FunctionDecl;
class NamedDecl;

namespace threadSafety {

class BeforeSet;

/// This enum distinguishes between different kinds of operations that may
/// need to be protected by locks. We use this enum in error handling.
enum ProtectedOperationKind {
  /// Dereferencing a variable (e.g. p in *p = 5;)
  POK_VarDereference,

  /// Reading or writing a variable (e.g. x in x = 5;)
  POK_VarAccess,

  /// Making a function call (e.g. fool())
  POK_FunctionCall,

  /// Passing a guarded variable by reference.
  POK_PassByRef,

  /// Passing a pt-guarded variable by reference.
  POK_PtPassByRef
};

/// This enum distinguishes between different kinds of lock actions. For
/// example, it is an error to write a variable protected by shared version of a
/// mutex.
enum LockKind {
  /// Shared/reader lock of a mutex.
  LK_Shared,

  /// Exclusive/writer lock of a mutex.
  LK_Exclusive,

  /// Can be either Shared or Exclusive.
  LK_Generic
};

/// This enum distinguishes between different ways to access (read or write) a
/// variable.
enum AccessKind {
  /// Reading a variable.
  AK_Read,

  /// Writing a variable.
  AK_Written
};

/// This enum distinguishes between different situations where we warn due to
/// inconsistent locking.
/// \enum SK_LockedSomeLoopIterations -- a mutex is locked for some but not all
/// loop iterations.
/// \enum SK_LockedSomePredecessors -- a mutex is locked in some but not all
/// predecessors of a CFGBlock.
/// \enum SK_LockedAtEndOfFunction -- a mutex is still locked at the end of a
/// function.
enum LockErrorKind {
  LEK_LockedSomeLoopIterations,
  LEK_LockedSomePredecessors,
  LEK_LockedAtEndOfFunction,
  LEK_NotLockedAtEndOfFunction
};

/// Handler class for thread safety warnings.
class ThreadSafetyHandler {
public:
  using Name = StringRef;

  ThreadSafetyHandler() = default;
  virtual ~ThreadSafetyHandler();

  /// Warn about lock expressions which fail to resolve to lockable objects.
  /// \param Kind -- the capability's name parameter (role, mutex, etc).
  /// \param Loc -- the SourceLocation of the unresolved expression.
  virtual void handleInvalidLockExp(StringRef Kind, SourceLocation Loc) {}

  /// Warn about unlock function calls that do not have a prior matching lock
  /// expression.
  /// \param Kind -- the capability's name parameter (role, mutex, etc).
  /// \param LockName -- A StringRef name for the lock expression, to be printed
  /// in the error message.
  /// \param Loc -- The SourceLocation of the Unlock
  virtual void handleUnmatchedUnlock(StringRef Kind, Name LockName,
                                     SourceLocation Loc) {}

  /// Warn about an unlock function call that attempts to unlock a lock with
  /// the incorrect lock kind. For instance, a shared lock being unlocked
  /// exclusively, or vice versa.
  /// \param LockName -- A StringRef name for the lock expression, to be printed
  /// in the error message.
  /// \param Kind -- the capability's name parameter (role, mutex, etc).
  /// \param Expected -- the kind of lock expected.
  /// \param Received -- the kind of lock received.
  /// \param Loc -- The SourceLocation of the Unlock.
  virtual void handleIncorrectUnlockKind(StringRef Kind, Name LockName,
                                         LockKind Expected, LockKind Received,
                                         SourceLocation Loc) {}

  /// Warn about lock function calls for locks which are already held.
  /// \param Kind -- the capability's name parameter (role, mutex, etc).
  /// \param LockName -- A StringRef name for the lock expression, to be printed
  /// in the error message.
  /// \param Loc -- The location of the second lock expression.
  virtual void handleDoubleLock(StringRef Kind, Name LockName,
                                SourceLocation Loc) {}

  /// Warn about situations where a mutex is sometimes held and sometimes not.
  /// The three situations are:
  /// 1. a mutex is locked on an "if" branch but not the "else" branch,
  /// 2, or a mutex is only held at the start of some loop iterations,
  /// 3. or when a mutex is locked but not unlocked inside a function.
  /// \param Kind -- the capability's name parameter (role, mutex, etc).
  /// \param LockName -- A StringRef name for the lock expression, to be printed
  /// in the error message.
  /// \param LocLocked -- The location of the lock expression where the mutex is
  ///               locked
  /// \param LocEndOfScope -- The location of the end of the scope where the
  ///               mutex is no longer held
  /// \param LEK -- which of the three above cases we should warn for
  virtual void handleMutexHeldEndOfScope(StringRef Kind, Name LockName,
                                         SourceLocation LocLocked,
                                         SourceLocation LocEndOfScope,
                                         LockErrorKind LEK) {}

  /// Warn when a mutex is held exclusively and shared at the same point. For
  /// example, if a mutex is locked exclusively during an if branch and shared
  /// during the else branch.
  /// \param Kind -- the capability's name parameter (role, mutex, etc).
  /// \param LockName -- A StringRef name for the lock expression, to be printed
  /// in the error message.
  /// \param Loc1 -- The location of the first lock expression.
  /// \param Loc2 -- The location of the second lock expression.
  virtual void handleExclusiveAndShared(StringRef Kind, Name LockName,
                                        SourceLocation Loc1,
                                        SourceLocation Loc2) {}

  /// Warn when a protected operation occurs while no locks are held.
  /// \param Kind -- the capability's name parameter (role, mutex, etc).
  /// \param D -- The decl for the protected variable or function
  /// \param POK -- The kind of protected operation (e.g. variable access)
  /// \param AK -- The kind of access (i.e. read or write) that occurred
  /// \param Loc -- The location of the protected operation.
  virtual void handleNoMutexHeld(StringRef Kind, const NamedDecl *D,
                                 ProtectedOperationKind POK, AccessKind AK,
                                 SourceLocation Loc) {}

  /// Warn when a protected operation occurs while the specific mutex protecting
  /// the operation is not locked.
  /// \param Kind -- the capability's name parameter (role, mutex, etc).
  /// \param D -- The decl for the protected variable or function
  /// \param POK -- The kind of protected operation (e.g. variable access)
  /// \param LockName -- A StringRef name for the lock expression, to be printed
  /// in the error message.
  /// \param LK -- The kind of access (i.e. read or write) that occurred
  /// \param Loc -- The location of the protected operation.
  virtual void handleMutexNotHeld(StringRef Kind, const NamedDecl *D,
                                  ProtectedOperationKind POK, Name LockName,
                                  LockKind LK, SourceLocation Loc,
                                  Name *PossibleMatch = nullptr) {}

  /// Warn when acquiring a lock that the negative capability is not held.
  /// \param Kind -- the capability's name parameter (role, mutex, etc).
  /// \param LockName -- The name for the lock expression, to be printed in the
  /// diagnostic.
  /// \param Neg -- The name of the negative capability to be printed in the
  /// diagnostic.
  /// \param Loc -- The location of the protected operation.
  virtual void handleNegativeNotHeld(StringRef Kind, Name LockName, Name Neg,
                                     SourceLocation Loc) {}

  /// Warn when a function is called while an excluded mutex is locked. For
  /// example, the mutex may be locked inside the function.
  /// \param Kind -- the capability's name parameter (role, mutex, etc).
  /// \param FunName -- The name of the function
  /// \param LockName -- A StringRef name for the lock expression, to be printed
  /// in the error message.
  /// \param Loc -- The location of the function call.
  virtual void handleFunExcludesLock(StringRef Kind, Name FunName,
                                     Name LockName, SourceLocation Loc) {}

  /// Warn that L1 cannot be acquired before L2.
  virtual void handleLockAcquiredBefore(StringRef Kind, Name L1Name,
                                        Name L2Name, SourceLocation Loc) {}

  /// Warn that there is a cycle in acquired_before/after dependencies.
  virtual void handleBeforeAfterCycle(Name L1Name, SourceLocation Loc) {}

  /// Called by the analysis when starting analysis of a function.
  /// Used to issue suggestions for changes to annotations.
  virtual void enterFunction(const FunctionDecl *FD) {}

  /// Called by the analysis when finishing analysis of a function.
  virtual void leaveFunction(const FunctionDecl *FD) {}

  bool issueBetaWarnings() { return IssueBetaWarnings; }
  void setIssueBetaWarnings(bool b) { IssueBetaWarnings = b; }

private:
  bool IssueBetaWarnings = false;
};

/// Check a function's CFG for thread-safety violations.
///
/// We traverse the blocks in the CFG, compute the set of mutexes that are held
/// at the end of each block, and issue warnings for thread safety violations.
/// Each block in the CFG is traversed exactly once.
void runThreadSafetyAnalysis(AnalysisDeclContext &AC,
                             ThreadSafetyHandler &Handler,
                             BeforeSet **Bset);

void threadSafetyCleanup(BeforeSet *Cache);

/// Helper function that returns a LockKind required for the given level
/// of access.
LockKind getLockKindFromAccessKind(AccessKind AK);

} // namespace threadSafety
} // namespace clang

#endif // LLVM_CLANG_ANALYSIS_ANALYSES_THREADSAFETY_H
