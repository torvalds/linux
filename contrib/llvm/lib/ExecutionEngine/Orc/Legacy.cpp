//===------- Legacy.cpp - Adapters for ExecutionEngine API interop --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/Legacy.h"

namespace llvm {
namespace orc {

void SymbolResolver::anchor() {}

JITSymbolResolverAdapter::JITSymbolResolverAdapter(
    ExecutionSession &ES, SymbolResolver &R, MaterializationResponsibility *MR)
    : ES(ES), R(R), MR(MR) {}

void JITSymbolResolverAdapter::lookup(const LookupSet &Symbols,
                                      OnResolvedFunction OnResolved) {
  SymbolNameSet InternedSymbols;
  for (auto &S : Symbols)
    InternedSymbols.insert(ES.intern(S));

  auto OnResolvedWithUnwrap = [OnResolved](Expected<SymbolMap> InternedResult) {
    if (!InternedResult) {
      OnResolved(InternedResult.takeError());
      return;
    }

    LookupResult Result;
    for (auto &KV : *InternedResult)
      Result[*KV.first] = std::move(KV.second);
    OnResolved(Result);
  };

  auto Q = std::make_shared<AsynchronousSymbolQuery>(
      InternedSymbols, OnResolvedWithUnwrap,
      [this](Error Err) { ES.reportError(std::move(Err)); });

  auto Unresolved = R.lookup(Q, InternedSymbols);
  if (Unresolved.empty()) {
    if (MR)
      MR->addDependenciesForAll(Q->QueryRegistrations);
  } else
    ES.legacyFailQuery(*Q, make_error<SymbolsNotFound>(std::move(Unresolved)));
}

Expected<JITSymbolResolverAdapter::LookupSet>
JITSymbolResolverAdapter::getResponsibilitySet(const LookupSet &Symbols) {
  SymbolNameSet InternedSymbols;
  for (auto &S : Symbols)
    InternedSymbols.insert(ES.intern(S));

  auto InternedResult = R.getResponsibilitySet(InternedSymbols);
  LookupSet Result;
  for (auto &S : InternedResult) {
    ResolvedStrings.insert(S);
    Result.insert(*S);
  }

  return Result;
}

} // End namespace orc.
} // End namespace llvm.
