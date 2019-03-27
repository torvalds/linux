//===---------- LazyReexports.cpp - Utilities for lazy reexports ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/LazyReexports.h"

#include "llvm/ADT/Triple.h"
#include "llvm/ExecutionEngine/Orc/OrcABISupport.h"

#define DEBUG_TYPE "orc"

namespace llvm {
namespace orc {

void LazyCallThroughManager::NotifyResolvedFunction::anchor() {}

LazyCallThroughManager::LazyCallThroughManager(
    ExecutionSession &ES, JITTargetAddress ErrorHandlerAddr,
    std::unique_ptr<TrampolinePool> TP)
    : ES(ES), ErrorHandlerAddr(ErrorHandlerAddr), TP(std::move(TP)) {}

Expected<JITTargetAddress> LazyCallThroughManager::getCallThroughTrampoline(
    JITDylib &SourceJD, SymbolStringPtr SymbolName,
    std::shared_ptr<NotifyResolvedFunction> NotifyResolved) {
  std::lock_guard<std::mutex> Lock(LCTMMutex);
  auto Trampoline = TP->getTrampoline();

  if (!Trampoline)
    return Trampoline.takeError();

  Reexports[*Trampoline] = std::make_pair(&SourceJD, std::move(SymbolName));
  Notifiers[*Trampoline] = std::move(NotifyResolved);
  return *Trampoline;
}

JITTargetAddress
LazyCallThroughManager::callThroughToSymbol(JITTargetAddress TrampolineAddr) {
  JITDylib *SourceJD = nullptr;
  SymbolStringPtr SymbolName;

  {
    std::lock_guard<std::mutex> Lock(LCTMMutex);
    auto I = Reexports.find(TrampolineAddr);
    if (I == Reexports.end())
      return ErrorHandlerAddr;
    SourceJD = I->second.first;
    SymbolName = I->second.second;
  }

  auto LookupResult = ES.lookup(JITDylibSearchList({{SourceJD, true}}),
                                {SymbolName}, NoDependenciesToRegister, true);

  if (!LookupResult) {
    ES.reportError(LookupResult.takeError());
    return ErrorHandlerAddr;
  }

  assert(LookupResult->size() == 1 && "Unexpected number of results");
  assert(LookupResult->count(SymbolName) && "Unexpected result");

  auto ResolvedAddr = LookupResult->begin()->second.getAddress();

  std::shared_ptr<NotifyResolvedFunction> NotifyResolved = nullptr;
  {
    std::lock_guard<std::mutex> Lock(LCTMMutex);
    auto I = Notifiers.find(TrampolineAddr);
    if (I != Notifiers.end()) {
      NotifyResolved = I->second;
      Notifiers.erase(I);
    }
  }

  if (NotifyResolved) {
    if (auto Err = (*NotifyResolved)(*SourceJD, SymbolName, ResolvedAddr)) {
      ES.reportError(std::move(Err));
      return ErrorHandlerAddr;
    }
  }

  return ResolvedAddr;
}

Expected<std::unique_ptr<LazyCallThroughManager>>
createLocalLazyCallThroughManager(const Triple &T, ExecutionSession &ES,
                                  JITTargetAddress ErrorHandlerAddr) {
  switch (T.getArch()) {
  default:
    return make_error<StringError>(
        std::string("No callback manager available for ") + T.str(),
        inconvertibleErrorCode());

  case Triple::aarch64:
    return LocalLazyCallThroughManager::Create<OrcAArch64>(ES,
                                                           ErrorHandlerAddr);

  case Triple::x86:
    return LocalLazyCallThroughManager::Create<OrcI386>(ES, ErrorHandlerAddr);

  case Triple::mips:
    return LocalLazyCallThroughManager::Create<OrcMips32Be>(ES,
                                                            ErrorHandlerAddr);

  case Triple::mipsel:
    return LocalLazyCallThroughManager::Create<OrcMips32Le>(ES,
                                                            ErrorHandlerAddr);

  case Triple::mips64:
  case Triple::mips64el:
    return LocalLazyCallThroughManager::Create<OrcMips64>(ES, ErrorHandlerAddr);

  case Triple::x86_64:
    if (T.getOS() == Triple::OSType::Win32)
      return LocalLazyCallThroughManager::Create<OrcX86_64_Win32>(
          ES, ErrorHandlerAddr);
    else
      return LocalLazyCallThroughManager::Create<OrcX86_64_SysV>(
          ES, ErrorHandlerAddr);
  }
}

LazyReexportsMaterializationUnit::LazyReexportsMaterializationUnit(
    LazyCallThroughManager &LCTManager, IndirectStubsManager &ISManager,
    JITDylib &SourceJD, SymbolAliasMap CallableAliases, VModuleKey K)
    : MaterializationUnit(extractFlags(CallableAliases), std::move(K)),
      LCTManager(LCTManager), ISManager(ISManager), SourceJD(SourceJD),
      CallableAliases(std::move(CallableAliases)),
      NotifyResolved(LazyCallThroughManager::createNotifyResolvedFunction(
          [&ISManager](JITDylib &JD, const SymbolStringPtr &SymbolName,
                       JITTargetAddress ResolvedAddr) {
            return ISManager.updatePointer(*SymbolName, ResolvedAddr);
          })) {}

StringRef LazyReexportsMaterializationUnit::getName() const {
  return "<Lazy Reexports>";
}

void LazyReexportsMaterializationUnit::materialize(
    MaterializationResponsibility R) {
  auto RequestedSymbols = R.getRequestedSymbols();

  SymbolAliasMap RequestedAliases;
  for (auto &RequestedSymbol : RequestedSymbols) {
    auto I = CallableAliases.find(RequestedSymbol);
    assert(I != CallableAliases.end() && "Symbol not found in alias map?");
    RequestedAliases[I->first] = std::move(I->second);
    CallableAliases.erase(I);
  }

  if (!CallableAliases.empty())
    R.replace(lazyReexports(LCTManager, ISManager, SourceJD,
                            std::move(CallableAliases)));

  IndirectStubsManager::StubInitsMap StubInits;
  for (auto &Alias : RequestedAliases) {

    auto CallThroughTrampoline = LCTManager.getCallThroughTrampoline(
        SourceJD, Alias.second.Aliasee, NotifyResolved);

    if (!CallThroughTrampoline) {
      SourceJD.getExecutionSession().reportError(
          CallThroughTrampoline.takeError());
      R.failMaterialization();
      return;
    }

    StubInits[*Alias.first] =
        std::make_pair(*CallThroughTrampoline, Alias.second.AliasFlags);
  }

  if (auto Err = ISManager.createStubs(StubInits)) {
    SourceJD.getExecutionSession().reportError(std::move(Err));
    R.failMaterialization();
    return;
  }

  SymbolMap Stubs;
  for (auto &Alias : RequestedAliases)
    Stubs[Alias.first] = ISManager.findStub(*Alias.first, false);

  R.resolve(Stubs);
  R.emit();
}

void LazyReexportsMaterializationUnit::discard(const JITDylib &JD,
                                               const SymbolStringPtr &Name) {
  assert(CallableAliases.count(Name) &&
         "Symbol not covered by this MaterializationUnit");
  CallableAliases.erase(Name);
}

SymbolFlagsMap
LazyReexportsMaterializationUnit::extractFlags(const SymbolAliasMap &Aliases) {
  SymbolFlagsMap SymbolFlags;
  for (auto &KV : Aliases) {
    assert(KV.second.AliasFlags.isCallable() &&
           "Lazy re-exports must be callable symbols");
    SymbolFlags[KV.first] = KV.second.AliasFlags;
  }
  return SymbolFlags;
}

} // End namespace orc.
} // End namespace llvm.
