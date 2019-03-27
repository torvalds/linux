//===- llvm/IR/PassInstrumentation.h ----------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines the Pass Instrumentation classes that provide
/// instrumentation points into the pass execution by PassManager.
///
/// There are two main classes:
///   - PassInstrumentation provides a set of instrumentation points for
///     pass managers to call on.
///
///   - PassInstrumentationCallbacks registers callbacks and provides access
///     to them for PassInstrumentation.
///
/// PassInstrumentation object is being used as a result of
/// PassInstrumentationAnalysis (so it is intended to be easily copyable).
///
/// Intended scheme of use for Pass Instrumentation is as follows:
///    - register instrumentation callbacks in PassInstrumentationCallbacks
///      instance. PassBuilder provides helper for that.
///
///    - register PassInstrumentationAnalysis with all the PassManagers.
///      PassBuilder handles that automatically when registering analyses.
///
///    - Pass Manager requests PassInstrumentationAnalysis from analysis manager
///      and gets PassInstrumentation as its result.
///
///    - Pass Manager invokes PassInstrumentation entry points appropriately,
///      passing StringRef identification ("name") of the pass currently being
///      executed and IRUnit it works on. There can be different schemes of
///      providing names in future, currently it is just a name() of the pass.
///
///    - PassInstrumentation wraps address of IRUnit into llvm::Any and passes
///      control to all the registered callbacks. Note that we specifically wrap
///      'const IRUnitT*' so as to avoid any accidental changes to IR in
///      instrumenting callbacks.
///
///    - Some instrumentation points (BeforePass) allow to control execution
///      of a pass. For those callbacks returning false means pass will not be
///      executed.
///
/// TODO: currently there is no way for a pass to opt-out of execution control
/// (e.g. become unskippable). PassManager is the only entity that determines
/// how pass instrumentation affects pass execution.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_PASSINSTRUMENTATION_H
#define LLVM_IR_PASSINSTRUMENTATION_H

#include "llvm/ADT/Any.h"
#include "llvm/ADT/FunctionExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/TypeName.h"
#include <type_traits>

namespace llvm {

class PreservedAnalyses;

/// This class manages callbacks registration, as well as provides a way for
/// PassInstrumentation to pass control to the registered callbacks.
class PassInstrumentationCallbacks {
public:
  // Before/After callbacks accept IRUnits whenever appropriate, so they need
  // to take them as constant pointers, wrapped with llvm::Any.
  // For the case when IRUnit has been invalidated there is a different
  // callback to use - AfterPassInvalidated.
  // TODO: currently AfterPassInvalidated does not accept IRUnit, since passing
  // already invalidated IRUnit is unsafe. There are ways to handle invalidated IRUnits
  // in a safe way, and we might pursue that as soon as there is a useful instrumentation
  // that needs it.
  using BeforePassFunc = bool(StringRef, Any);
  using AfterPassFunc = void(StringRef, Any);
  using AfterPassInvalidatedFunc = void(StringRef);
  using BeforeAnalysisFunc = void(StringRef, Any);
  using AfterAnalysisFunc = void(StringRef, Any);

public:
  PassInstrumentationCallbacks() {}

  /// Copying PassInstrumentationCallbacks is not intended.
  PassInstrumentationCallbacks(const PassInstrumentationCallbacks &) = delete;
  void operator=(const PassInstrumentationCallbacks &) = delete;

  template <typename CallableT> void registerBeforePassCallback(CallableT C) {
    BeforePassCallbacks.emplace_back(std::move(C));
  }

  template <typename CallableT> void registerAfterPassCallback(CallableT C) {
    AfterPassCallbacks.emplace_back(std::move(C));
  }

  template <typename CallableT>
  void registerAfterPassInvalidatedCallback(CallableT C) {
    AfterPassInvalidatedCallbacks.emplace_back(std::move(C));
  }

  template <typename CallableT>
  void registerBeforeAnalysisCallback(CallableT C) {
    BeforeAnalysisCallbacks.emplace_back(std::move(C));
  }

  template <typename CallableT>
  void registerAfterAnalysisCallback(CallableT C) {
    AfterAnalysisCallbacks.emplace_back(std::move(C));
  }

private:
  friend class PassInstrumentation;

  SmallVector<llvm::unique_function<BeforePassFunc>, 4> BeforePassCallbacks;
  SmallVector<llvm::unique_function<AfterPassFunc>, 4> AfterPassCallbacks;
  SmallVector<llvm::unique_function<AfterPassInvalidatedFunc>, 4>
      AfterPassInvalidatedCallbacks;
  SmallVector<llvm::unique_function<BeforeAnalysisFunc>, 4>
      BeforeAnalysisCallbacks;
  SmallVector<llvm::unique_function<AfterAnalysisFunc>, 4>
      AfterAnalysisCallbacks;
};

/// This class provides instrumentation entry points for the Pass Manager,
/// doing calls to callbacks registered in PassInstrumentationCallbacks.
class PassInstrumentation {
  PassInstrumentationCallbacks *Callbacks;

public:
  /// Callbacks object is not owned by PassInstrumentation, its life-time
  /// should at least match the life-time of corresponding
  /// PassInstrumentationAnalysis (which usually is till the end of current
  /// compilation).
  PassInstrumentation(PassInstrumentationCallbacks *CB = nullptr)
      : Callbacks(CB) {}

  /// BeforePass instrumentation point - takes \p Pass instance to be executed
  /// and constant reference to IR it operates on. \Returns true if pass is
  /// allowed to be executed.
  template <typename IRUnitT, typename PassT>
  bool runBeforePass(const PassT &Pass, const IRUnitT &IR) const {
    if (!Callbacks)
      return true;

    bool ShouldRun = true;
    for (auto &C : Callbacks->BeforePassCallbacks)
      ShouldRun &= C(Pass.name(), llvm::Any(&IR));
    return ShouldRun;
  }

  /// AfterPass instrumentation point - takes \p Pass instance that has
  /// just been executed and constant reference to \p IR it operates on.
  /// \p IR is guaranteed to be valid at this point.
  template <typename IRUnitT, typename PassT>
  void runAfterPass(const PassT &Pass, const IRUnitT &IR) const {
    if (Callbacks)
      for (auto &C : Callbacks->AfterPassCallbacks)
        C(Pass.name(), llvm::Any(&IR));
  }

  /// AfterPassInvalidated instrumentation point - takes \p Pass instance
  /// that has just been executed. For use when IR has been invalidated
  /// by \p Pass execution.
  template <typename IRUnitT, typename PassT>
  void runAfterPassInvalidated(const PassT &Pass) const {
    if (Callbacks)
      for (auto &C : Callbacks->AfterPassInvalidatedCallbacks)
        C(Pass.name());
  }

  /// BeforeAnalysis instrumentation point - takes \p Analysis instance
  /// to be executed and constant reference to IR it operates on.
  template <typename IRUnitT, typename PassT>
  void runBeforeAnalysis(const PassT &Analysis, const IRUnitT &IR) const {
    if (Callbacks)
      for (auto &C : Callbacks->BeforeAnalysisCallbacks)
        C(Analysis.name(), llvm::Any(&IR));
  }

  /// AfterAnalysis instrumentation point - takes \p Analysis instance
  /// that has just been executed and constant reference to IR it operated on.
  template <typename IRUnitT, typename PassT>
  void runAfterAnalysis(const PassT &Analysis, const IRUnitT &IR) const {
    if (Callbacks)
      for (auto &C : Callbacks->AfterAnalysisCallbacks)
        C(Analysis.name(), llvm::Any(&IR));
  }

  /// Handle invalidation from the pass manager when PassInstrumentation
  /// is used as the result of PassInstrumentationAnalysis.
  ///
  /// On attempt to invalidate just return false. There is nothing to become
  /// invalid here.
  template <typename IRUnitT, typename... ExtraArgsT>
  bool invalidate(IRUnitT &, const class llvm::PreservedAnalyses &,
                  ExtraArgsT...) {
    return false;
  }
};

} // namespace llvm

#endif
