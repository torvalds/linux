//===----------- ThreadSafeModule.h -- Layer interfaces ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Thread safe wrappers and utilities for Module and LLVMContext.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_THREADSAFEMODULEWRAPPER_H
#define LLVM_EXECUTIONENGINE_ORC_THREADSAFEMODULEWRAPPER_H

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Compiler.h"

#include <functional>
#include <memory>
#include <mutex>

namespace llvm {
namespace orc {

/// An LLVMContext together with an associated mutex that can be used to lock
/// the context to prevent concurrent access by other threads.
class ThreadSafeContext {
private:
  struct State {
    State(std::unique_ptr<LLVMContext> Ctx) : Ctx(std::move(Ctx)) {}

    std::unique_ptr<LLVMContext> Ctx;
    std::recursive_mutex Mutex;
  };

public:
  // RAII based lock for ThreadSafeContext.
  class LLVM_NODISCARD Lock {
  private:
    using UnderlyingLock = std::lock_guard<std::recursive_mutex>;

  public:
    Lock(std::shared_ptr<State> S)
        : S(std::move(S)),
          L(llvm::make_unique<UnderlyingLock>(this->S->Mutex)) {}

  private:
    std::shared_ptr<State> S;
    std::unique_ptr<UnderlyingLock> L;
  };

  /// Construct a null context.
  ThreadSafeContext() = default;

  /// Construct a ThreadSafeContext from the given LLVMContext.
  ThreadSafeContext(std::unique_ptr<LLVMContext> NewCtx)
      : S(std::make_shared<State>(std::move(NewCtx))) {
    assert(S->Ctx != nullptr &&
           "Can not construct a ThreadSafeContext from a nullptr");
  }

  /// Returns a pointer to the LLVMContext that was used to construct this
  /// instance, or null if the instance was default constructed.
  LLVMContext *getContext() { return S ? S->Ctx.get() : nullptr; }

  /// Returns a pointer to the LLVMContext that was used to construct this
  /// instance, or null if the instance was default constructed.
  const LLVMContext *getContext() const { return S ? S->Ctx.get() : nullptr; }

  Lock getLock() {
    assert(S && "Can not lock an empty ThreadSafeContext");
    return Lock(S);
  }

private:
  std::shared_ptr<State> S;
};

/// An LLVM Module together with a shared ThreadSafeContext.
class ThreadSafeModule {
public:
  /// Default construct a ThreadSafeModule. This results in a null module and
  /// null context.
  ThreadSafeModule() = default;

  ThreadSafeModule(ThreadSafeModule &&Other) = default;

  ThreadSafeModule &operator=(ThreadSafeModule &&Other) {
    // We have to explicitly define this move operator to copy the fields in
    // reverse order (i.e. module first) to ensure the dependencies are
    // protected: The old module that is being overwritten must be destroyed
    // *before* the context that it depends on.
    // We also need to lock the context to make sure the module tear-down
    // does not overlap any other work on the context.
    if (M) {
      auto L = getContextLock();
      M = nullptr;
    }
    M = std::move(Other.M);
    TSCtx = std::move(Other.TSCtx);
    return *this;
  }

  /// Construct a ThreadSafeModule from a unique_ptr<Module> and a
  /// unique_ptr<LLVMContext>. This creates a new ThreadSafeContext from the
  /// given context.
  ThreadSafeModule(std::unique_ptr<Module> M, std::unique_ptr<LLVMContext> Ctx)
      : M(std::move(M)), TSCtx(std::move(Ctx)) {}

  /// Construct a ThreadSafeModule from a unique_ptr<Module> and an
  /// existing ThreadSafeContext.
  ThreadSafeModule(std::unique_ptr<Module> M, ThreadSafeContext TSCtx)
      : M(std::move(M)), TSCtx(std::move(TSCtx)) {}

  ~ThreadSafeModule() {
    // We need to lock the context while we destruct the module.
    if (M) {
      auto L = getContextLock();
      M = nullptr;
    }
  }

  /// Get the module wrapped by this ThreadSafeModule.
  Module *getModule() { return M.get(); }

  /// Get the module wrapped by this ThreadSafeModule.
  const Module *getModule() const { return M.get(); }

  /// Take out a lock on the ThreadSafeContext for this module.
  ThreadSafeContext::Lock getContextLock() { return TSCtx.getLock(); }

  /// Boolean conversion: This ThreadSafeModule will evaluate to true if it
  /// wraps a non-null module.
  explicit operator bool() {
    if (M) {
      assert(TSCtx.getContext() &&
             "Non-null module must have non-null context");
      return true;
    }
    return false;
  }

private:
  std::unique_ptr<Module> M;
  ThreadSafeContext TSCtx;
};

using GVPredicate = std::function<bool(const GlobalValue &)>;
using GVModifier = std::function<void(GlobalValue &)>;

/// Clones the given module on to a new context.
ThreadSafeModule
cloneToNewContext(ThreadSafeModule &TSMW,
                  GVPredicate ShouldCloneDef = GVPredicate(),
                  GVModifier UpdateClonedDefSource = GVModifier());

} // End namespace orc
} // End namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_THREADSAFEMODULEWRAPPER_H
