//===- GlobalMappingLayer.h - Run all IR through a functor ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Convenience layer for injecting symbols that will appear in calls to
// findSymbol.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_GLOBALMAPPINGLAYER_H
#define LLVM_EXECUTIONENGINE_ORC_GLOBALMAPPINGLAYER_H

#include "llvm/ExecutionEngine/JITSymbol.h"
#include <map>
#include <memory>
#include <string>

namespace llvm {

class Module;
class JITSymbolResolver;

namespace orc {

/// Global mapping layer.
///
///   This layer overrides the findSymbol method to first search a local symbol
/// table that the client can define. It can be used to inject new symbol
/// mappings into the JIT. Beware, however: symbols within a single IR module or
/// object file will still resolve locally (via RuntimeDyld's symbol table) -
/// such internal references cannot be overriden via this layer.
template <typename BaseLayerT>
class GlobalMappingLayer {
public:

  /// Handle to an added module.
  using ModuleHandleT = typename BaseLayerT::ModuleHandleT;

  /// Construct an GlobalMappingLayer with the given BaseLayer
  GlobalMappingLayer(BaseLayerT &BaseLayer) : BaseLayer(BaseLayer) {}

  /// Add the given module to the JIT.
  /// @return A handle for the added modules.
  Expected<ModuleHandleT>
  addModule(std::shared_ptr<Module> M,
            std::shared_ptr<JITSymbolResolver> Resolver) {
    return BaseLayer.addModule(std::move(M), std::move(Resolver));
  }

  /// Remove the module set associated with the handle H.
  Error removeModule(ModuleHandleT H) { return BaseLayer.removeModule(H); }

  /// Manually set the address to return for the given symbol.
  void setGlobalMapping(const std::string &Name, JITTargetAddress Addr) {
    SymbolTable[Name] = Addr;
  }

  /// Remove the given symbol from the global mapping.
  void eraseGlobalMapping(const std::string &Name) {
    SymbolTable.erase(Name);
  }

  /// Search for the given named symbol.
  ///
  ///          This method will first search the local symbol table, returning
  ///        any symbol found there. If the symbol is not found in the local
  ///        table then this call will be passed through to the base layer.
  ///
  /// @param Name The name of the symbol to search for.
  /// @param ExportedSymbolsOnly If true, search only for exported symbols.
  /// @return A handle for the given named symbol, if it exists.
  JITSymbol findSymbol(const std::string &Name, bool ExportedSymbolsOnly) {
    auto I = SymbolTable.find(Name);
    if (I != SymbolTable.end())
      return JITSymbol(I->second, JITSymbolFlags::Exported);
    return BaseLayer.findSymbol(Name, ExportedSymbolsOnly);
  }

  /// Get the address of the given symbol in the context of the of the
  ///        module represented by the handle H. This call is forwarded to the
  ///        base layer's implementation.
  /// @param H The handle for the module to search in.
  /// @param Name The name of the symbol to search for.
  /// @param ExportedSymbolsOnly If true, search only for exported symbols.
  /// @return A handle for the given named symbol, if it is found in the
  ///         given module.
  JITSymbol findSymbolIn(ModuleHandleT H, const std::string &Name,
                         bool ExportedSymbolsOnly) {
    return BaseLayer.findSymbolIn(H, Name, ExportedSymbolsOnly);
  }

  /// Immediately emit and finalize the module set represented by the
  ///        given handle.
  /// @param H Handle for module set to emit/finalize.
  Error emitAndFinalize(ModuleHandleT H) {
    return BaseLayer.emitAndFinalize(H);
  }

private:
  BaseLayerT &BaseLayer;
  std::map<std::string, JITTargetAddress> SymbolTable;
};

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_GLOBALMAPPINGLAYER_H
