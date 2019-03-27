//===- LambdaResolverMM - Redirect symbol lookup via a functor --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//   Defines a RuntimeDyld::SymbolResolver subclass that uses a user-supplied
// functor for symbol resolution.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_LAMBDARESOLVER_H
#define LLVM_EXECUTIONENGINE_ORC_LAMBDARESOLVER_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include <memory>

namespace llvm {
namespace orc {

template <typename DylibLookupFtorT, typename ExternalLookupFtorT>
class LambdaResolver : public LegacyJITSymbolResolver {
public:
  LambdaResolver(DylibLookupFtorT DylibLookupFtor,
                 ExternalLookupFtorT ExternalLookupFtor)
      : DylibLookupFtor(DylibLookupFtor),
        ExternalLookupFtor(ExternalLookupFtor) {}

  JITSymbol findSymbolInLogicalDylib(const std::string &Name) final {
    return DylibLookupFtor(Name);
  }

  JITSymbol findSymbol(const std::string &Name) final {
    return ExternalLookupFtor(Name);
  }

private:
  DylibLookupFtorT DylibLookupFtor;
  ExternalLookupFtorT ExternalLookupFtor;
};

template <typename DylibLookupFtorT,
          typename ExternalLookupFtorT>
std::shared_ptr<LambdaResolver<DylibLookupFtorT, ExternalLookupFtorT>>
createLambdaResolver(DylibLookupFtorT DylibLookupFtor,
                     ExternalLookupFtorT ExternalLookupFtor) {
  using LR = LambdaResolver<DylibLookupFtorT, ExternalLookupFtorT>;
  return make_unique<LR>(std::move(DylibLookupFtor),
                         std::move(ExternalLookupFtor));
}

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_LAMBDARESOLVER_H
