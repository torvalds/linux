//===------ LazyReexports.h -- Utilities for lazy reexports -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Lazy re-exports are similar to normal re-exports, except that for callable
// symbols the definitions are replaced with trampolines that will look up and
// call through to the re-exported symbol at runtime. This can be used to
// enable lazy compilation.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_LAZYREEXPORTS_H
#define LLVM_EXECUTIONENGINE_ORC_LAZYREEXPORTS_H

#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/IndirectionUtils.h"

namespace llvm {

class Triple;

namespace orc {

/// Manages a set of 'lazy call-through' trampolines. These are compiler
/// re-entry trampolines that are pre-bound to look up a given symbol in a given
/// JITDylib, then jump to that address. Since compilation of symbols is
/// triggered on first lookup, these call-through trampolines can be used to
/// implement lazy compilation.
///
/// The easiest way to construct these call-throughs is using the lazyReexport
/// function.
class LazyCallThroughManager {
public:
  /// Clients will want to take some action on first resolution, e.g. updating
  /// a stub pointer. Instances of this class can be used to implement this.
  class NotifyResolvedFunction {
  public:
    virtual ~NotifyResolvedFunction() {}

    /// Called the first time a lazy call through is executed and the target
    /// symbol resolved.
    virtual Error operator()(JITDylib &SourceJD,
                             const SymbolStringPtr &SymbolName,
                             JITTargetAddress ResolvedAddr) = 0;

  private:
    virtual void anchor();
  };

  template <typename NotifyResolvedImpl>
  class NotifyResolvedFunctionImpl : public NotifyResolvedFunction {
  public:
    NotifyResolvedFunctionImpl(NotifyResolvedImpl NotifyResolved)
        : NotifyResolved(std::move(NotifyResolved)) {}
    Error operator()(JITDylib &SourceJD, const SymbolStringPtr &SymbolName,
                     JITTargetAddress ResolvedAddr) {
      return NotifyResolved(SourceJD, SymbolName, ResolvedAddr);
    }

  private:
    NotifyResolvedImpl NotifyResolved;
  };

  /// Create a shared NotifyResolvedFunction from a given type that is
  /// callable with the correct signature.
  template <typename NotifyResolvedImpl>
  static std::unique_ptr<NotifyResolvedFunction>
  createNotifyResolvedFunction(NotifyResolvedImpl NotifyResolved) {
    return llvm::make_unique<NotifyResolvedFunctionImpl<NotifyResolvedImpl>>(
        std::move(NotifyResolved));
  }

  // Return a free call-through trampoline and bind it to look up and call
  // through to the given symbol.
  Expected<JITTargetAddress> getCallThroughTrampoline(
      JITDylib &SourceJD, SymbolStringPtr SymbolName,
      std::shared_ptr<NotifyResolvedFunction> NotifyResolved);

protected:
  LazyCallThroughManager(ExecutionSession &ES,
                         JITTargetAddress ErrorHandlerAddr,
                         std::unique_ptr<TrampolinePool> TP);

  JITTargetAddress callThroughToSymbol(JITTargetAddress TrampolineAddr);

  void setTrampolinePool(std::unique_ptr<TrampolinePool> TP) {
    this->TP = std::move(TP);
  }

private:
  using ReexportsMap =
      std::map<JITTargetAddress, std::pair<JITDylib *, SymbolStringPtr>>;

  using NotifiersMap =
      std::map<JITTargetAddress, std::shared_ptr<NotifyResolvedFunction>>;

  std::mutex LCTMMutex;
  ExecutionSession &ES;
  JITTargetAddress ErrorHandlerAddr;
  std::unique_ptr<TrampolinePool> TP;
  ReexportsMap Reexports;
  NotifiersMap Notifiers;
};

/// A lazy call-through manager that builds trampolines in the current process.
class LocalLazyCallThroughManager : public LazyCallThroughManager {
private:
  LocalLazyCallThroughManager(ExecutionSession &ES,
                              JITTargetAddress ErrorHandlerAddr)
      : LazyCallThroughManager(ES, ErrorHandlerAddr, nullptr) {}

  template <typename ORCABI> Error init() {
    auto TP = LocalTrampolinePool<ORCABI>::Create(
        [this](JITTargetAddress TrampolineAddr) {
          return callThroughToSymbol(TrampolineAddr);
        });

    if (!TP)
      return TP.takeError();

    setTrampolinePool(std::move(*TP));
    return Error::success();
  }

public:
  /// Create a LocalLazyCallThroughManager using the given ABI. See
  /// createLocalLazyCallThroughManager.
  template <typename ORCABI>
  static Expected<std::unique_ptr<LocalLazyCallThroughManager>>
  Create(ExecutionSession &ES, JITTargetAddress ErrorHandlerAddr) {
    auto LLCTM = std::unique_ptr<LocalLazyCallThroughManager>(
        new LocalLazyCallThroughManager(ES, ErrorHandlerAddr));

    if (auto Err = LLCTM->init<ORCABI>())
      return std::move(Err);

    return std::move(LLCTM);
  }
};

/// Create a LocalLazyCallThroughManager from the given triple and execution
/// session.
Expected<std::unique_ptr<LazyCallThroughManager>>
createLocalLazyCallThroughManager(const Triple &T, ExecutionSession &ES,
                                  JITTargetAddress ErrorHandlerAddr);

/// A materialization unit that builds lazy re-exports. These are callable
/// entry points that call through to the given symbols.
/// Unlike a 'true' re-export, the address of the lazy re-export will not
/// match the address of the re-exported symbol, but calling it will behave
/// the same as calling the re-exported symbol.
class LazyReexportsMaterializationUnit : public MaterializationUnit {
public:
  LazyReexportsMaterializationUnit(LazyCallThroughManager &LCTManager,
                                   IndirectStubsManager &ISManager,
                                   JITDylib &SourceJD,
                                   SymbolAliasMap CallableAliases,
                                   VModuleKey K);

  StringRef getName() const override;

private:
  void materialize(MaterializationResponsibility R) override;
  void discard(const JITDylib &JD, const SymbolStringPtr &Name) override;
  static SymbolFlagsMap extractFlags(const SymbolAliasMap &Aliases);

  LazyCallThroughManager &LCTManager;
  IndirectStubsManager &ISManager;
  JITDylib &SourceJD;
  SymbolAliasMap CallableAliases;
  std::shared_ptr<LazyCallThroughManager::NotifyResolvedFunction>
      NotifyResolved;
};

/// Define lazy-reexports based on the given SymbolAliasMap. Each lazy re-export
/// is a callable symbol that will look up and dispatch to the given aliasee on
/// first call. All subsequent calls will go directly to the aliasee.
inline std::unique_ptr<LazyReexportsMaterializationUnit>
lazyReexports(LazyCallThroughManager &LCTManager,
              IndirectStubsManager &ISManager, JITDylib &SourceJD,
              SymbolAliasMap CallableAliases, VModuleKey K = VModuleKey()) {
  return llvm::make_unique<LazyReexportsMaterializationUnit>(
      LCTManager, ISManager, SourceJD, std::move(CallableAliases),
      std::move(K));
}

} // End namespace orc
} // End namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_LAZYREEXPORTS_H
