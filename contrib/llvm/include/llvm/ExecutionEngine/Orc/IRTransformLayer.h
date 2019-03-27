//===- IRTransformLayer.h - Run all IR through a functor --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Run all IR passed in through a user supplied functor.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_IRTRANSFORMLAYER_H
#define LLVM_EXECUTIONENGINE_ORC_IRTRANSFORMLAYER_H

#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/Layer.h"
#include <memory>
#include <string>

namespace llvm {
class Module;
namespace orc {

class IRTransformLayer : public IRLayer {
public:
  using TransformFunction = std::function<Expected<ThreadSafeModule>(
      ThreadSafeModule, const MaterializationResponsibility &R)>;

  IRTransformLayer(ExecutionSession &ES, IRLayer &BaseLayer,
                   TransformFunction Transform = identityTransform);

  void setTransform(TransformFunction Transform) {
    this->Transform = std::move(Transform);
  }

  void emit(MaterializationResponsibility R, ThreadSafeModule TSM) override;

  static ThreadSafeModule
  identityTransform(ThreadSafeModule TSM,
                    const MaterializationResponsibility &R) {
    return TSM;
  }

private:
  IRLayer &BaseLayer;
  TransformFunction Transform;
};

/// IR mutating layer.
///
///   This layer applies a user supplied transform to each module that is added,
/// then adds the transformed module to the layer below.
template <typename BaseLayerT, typename TransformFtor>
class LegacyIRTransformLayer {
public:

  /// Construct an LegacyIRTransformLayer with the given BaseLayer
  LegacyIRTransformLayer(BaseLayerT &BaseLayer,
                   TransformFtor Transform = TransformFtor())
    : BaseLayer(BaseLayer), Transform(std::move(Transform)) {}

  /// Apply the transform functor to the module, then add the module to
  ///        the layer below, along with the memory manager and symbol resolver.
  ///
  /// @return A handle for the added modules.
  Error addModule(VModuleKey K, std::unique_ptr<Module> M) {
    return BaseLayer.addModule(std::move(K), Transform(std::move(M)));
  }

  /// Remove the module associated with the VModuleKey K.
  Error removeModule(VModuleKey K) { return BaseLayer.removeModule(K); }

  /// Search for the given named symbol.
  /// @param Name The name of the symbol to search for.
  /// @param ExportedSymbolsOnly If true, search only for exported symbols.
  /// @return A handle for the given named symbol, if it exists.
  JITSymbol findSymbol(const std::string &Name, bool ExportedSymbolsOnly) {
    return BaseLayer.findSymbol(Name, ExportedSymbolsOnly);
  }

  /// Get the address of the given symbol in the context of the module
  ///        represented by the VModuleKey K. This call is forwarded to the base
  ///        layer's implementation.
  /// @param K The VModuleKey for the module to search in.
  /// @param Name The name of the symbol to search for.
  /// @param ExportedSymbolsOnly If true, search only for exported symbols.
  /// @return A handle for the given named symbol, if it is found in the
  ///         given module.
  JITSymbol findSymbolIn(VModuleKey K, const std::string &Name,
                         bool ExportedSymbolsOnly) {
    return BaseLayer.findSymbolIn(K, Name, ExportedSymbolsOnly);
  }

  /// Immediately emit and finalize the module represented by the given
  ///        VModuleKey.
  /// @param K The VModuleKey for the module to emit/finalize.
  Error emitAndFinalize(VModuleKey K) { return BaseLayer.emitAndFinalize(K); }

  /// Access the transform functor directly.
  TransformFtor& getTransform() { return Transform; }

  /// Access the mumate functor directly.
  const TransformFtor& getTransform() const { return Transform; }

private:
  BaseLayerT &BaseLayer;
  TransformFtor Transform;
};

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_IRTRANSFORMLAYER_H
