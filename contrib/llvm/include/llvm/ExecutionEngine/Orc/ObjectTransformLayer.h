//===- ObjectTransformLayer.h - Run all objects through functor -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Run all objects passed in through a user supplied functor.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_OBJECTTRANSFORMLAYER_H
#define LLVM_EXECUTIONENGINE_ORC_OBJECTTRANSFORMLAYER_H

#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/Layer.h"
#include <algorithm>
#include <memory>
#include <string>

namespace llvm {
namespace orc {

class ObjectTransformLayer : public ObjectLayer {
public:
  using TransformFunction =
      std::function<Expected<std::unique_ptr<MemoryBuffer>>(
          std::unique_ptr<MemoryBuffer>)>;

  ObjectTransformLayer(ExecutionSession &ES, ObjectLayer &BaseLayer,
                       TransformFunction Transform);

  void emit(MaterializationResponsibility R,
            std::unique_ptr<MemoryBuffer> O) override;

private:
  ObjectLayer &BaseLayer;
  TransformFunction Transform;
};

/// Object mutating layer.
///
///   This layer accepts sets of ObjectFiles (via addObject). It
/// immediately applies the user supplied functor to each object, then adds
/// the set of transformed objects to the layer below.
template <typename BaseLayerT, typename TransformFtor>
class LegacyObjectTransformLayer {
public:
  /// Construct an ObjectTransformLayer with the given BaseLayer
  LegacyObjectTransformLayer(BaseLayerT &BaseLayer,
                             TransformFtor Transform = TransformFtor())
      : BaseLayer(BaseLayer), Transform(std::move(Transform)) {}

  /// Apply the transform functor to each object in the object set, then
  ///        add the resulting set of objects to the base layer, along with the
  ///        memory manager and symbol resolver.
  ///
  /// @return A handle for the added objects.
  template <typename ObjectPtr> Error addObject(VModuleKey K, ObjectPtr Obj) {
    return BaseLayer.addObject(std::move(K), Transform(std::move(Obj)));
  }

  /// Remove the object set associated with the VModuleKey K.
  Error removeObject(VModuleKey K) { return BaseLayer.removeObject(K); }

  /// Search for the given named symbol.
  /// @param Name The name of the symbol to search for.
  /// @param ExportedSymbolsOnly If true, search only for exported symbols.
  /// @return A handle for the given named symbol, if it exists.
  JITSymbol findSymbol(const std::string &Name, bool ExportedSymbolsOnly) {
    return BaseLayer.findSymbol(Name, ExportedSymbolsOnly);
  }

  /// Get the address of the given symbol in the context of the set of
  ///        objects represented by the VModuleKey K. This call is forwarded to
  ///        the base layer's implementation.
  /// @param K The VModuleKey associated with the object set to search in.
  /// @param Name The name of the symbol to search for.
  /// @param ExportedSymbolsOnly If true, search only for exported symbols.
  /// @return A handle for the given named symbol, if it is found in the
  ///         given object set.
  JITSymbol findSymbolIn(VModuleKey K, const std::string &Name,
                         bool ExportedSymbolsOnly) {
    return BaseLayer.findSymbolIn(K, Name, ExportedSymbolsOnly);
  }

  /// Immediately emit and finalize the object set represented by the
  ///        given VModuleKey K.
  Error emitAndFinalize(VModuleKey K) { return BaseLayer.emitAndFinalize(K); }

  /// Map section addresses for the objects associated with the
  /// VModuleKey K.
  void mapSectionAddress(VModuleKey K, const void *LocalAddress,
                         JITTargetAddress TargetAddr) {
    BaseLayer.mapSectionAddress(K, LocalAddress, TargetAddr);
  }

  /// Access the transform functor directly.
  TransformFtor &getTransform() { return Transform; }

  /// Access the mumate functor directly.
  const TransformFtor &getTransform() const { return Transform; }

private:
  BaseLayerT &BaseLayer;
  TransformFtor Transform;
};

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_OBJECTTRANSFORMLAYER_H
