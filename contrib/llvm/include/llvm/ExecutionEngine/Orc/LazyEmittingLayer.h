//===- LazyEmittingLayer.h - Lazily emit IR to lower JIT layers -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Contains the definition for a lazy-emitting layer for the JIT.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_LAZYEMITTINGLAYER_H
#define LLVM_EXECUTIONENGINE_ORC_LAZYEMITTINGLAYER_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <list>
#include <memory>
#include <string>

namespace llvm {
namespace orc {

/// Lazy-emitting IR layer.
///
///   This layer accepts LLVM IR Modules (via addModule), but does not
/// immediately emit them the layer below. Instead, emissing to the base layer
/// is deferred until the first time the client requests the address (via
/// JITSymbol::getAddress) for a symbol contained in this layer.
template <typename BaseLayerT> class LazyEmittingLayer {
private:
  class EmissionDeferredModule {
  public:
    EmissionDeferredModule(VModuleKey K, std::unique_ptr<Module> M)
        : K(std::move(K)), M(std::move(M)) {}

    JITSymbol find(StringRef Name, bool ExportedSymbolsOnly, BaseLayerT &B) {
      switch (EmitState) {
      case NotEmitted:
        if (auto GV = searchGVs(Name, ExportedSymbolsOnly)) {
          // Create a std::string version of Name to capture here - the argument
          // (a StringRef) may go away before the lambda is executed.
          // FIXME: Use capture-init when we move to C++14.
          std::string PName = Name;
          JITSymbolFlags Flags = JITSymbolFlags::fromGlobalValue(*GV);
          auto GetAddress =
            [this, ExportedSymbolsOnly, PName, &B]() -> Expected<JITTargetAddress> {
              if (this->EmitState == Emitting)
                return 0;
              else if (this->EmitState == NotEmitted) {
                this->EmitState = Emitting;
                if (auto Err = this->emitToBaseLayer(B))
                  return std::move(Err);
                this->EmitState = Emitted;
              }
              if (auto Sym = B.findSymbolIn(K, PName, ExportedSymbolsOnly))
                return Sym.getAddress();
              else if (auto Err = Sym.takeError())
                return std::move(Err);
              else
                llvm_unreachable("Successful symbol lookup should return "
                                 "definition address here");
          };
          return JITSymbol(std::move(GetAddress), Flags);
        } else
          return nullptr;
      case Emitting:
        // Calling "emit" can trigger a recursive call to 'find' (e.g. to check
        // for pre-existing definitions of common-symbol), but any symbol in
        // this module would already have been found internally (in the
        // RuntimeDyld that did the lookup), so just return a nullptr here.
        return nullptr;
      case Emitted:
        return B.findSymbolIn(K, Name, ExportedSymbolsOnly);
      }
      llvm_unreachable("Invalid emit-state.");
    }

    Error removeModuleFromBaseLayer(BaseLayerT& BaseLayer) {
      return EmitState != NotEmitted ? BaseLayer.removeModule(K)
                                     : Error::success();
    }

    void emitAndFinalize(BaseLayerT &BaseLayer) {
      assert(EmitState != Emitting &&
             "Cannot emitAndFinalize while already emitting");
      if (EmitState == NotEmitted) {
        EmitState = Emitting;
        emitToBaseLayer(BaseLayer);
        EmitState = Emitted;
      }
      BaseLayer.emitAndFinalize(K);
    }

  private:

    const GlobalValue* searchGVs(StringRef Name,
                                 bool ExportedSymbolsOnly) const {
      // FIXME: We could clean all this up if we had a way to reliably demangle
      //        names: We could just demangle name and search, rather than
      //        mangling everything else.

      // If we have already built the mangled name set then just search it.
      if (MangledSymbols) {
        auto VI = MangledSymbols->find(Name);
        if (VI == MangledSymbols->end())
          return nullptr;
        auto GV = VI->second;
        if (!ExportedSymbolsOnly || GV->hasDefaultVisibility())
          return GV;
        return nullptr;
      }

      // If we haven't built the mangled name set yet, try to build it. As an
      // optimization this will leave MangledNames set to nullptr if we find
      // Name in the process of building the set.
      return buildMangledSymbols(Name, ExportedSymbolsOnly);
    }

    Error emitToBaseLayer(BaseLayerT &BaseLayer) {
      // We don't need the mangled names set any more: Once we've emitted this
      // to the base layer we'll just look for symbols there.
      MangledSymbols.reset();
      return BaseLayer.addModule(std::move(K), std::move(M));
    }

    // If the mangled name of the given GlobalValue matches the given search
    // name (and its visibility conforms to the ExportedSymbolsOnly flag) then
    // return the symbol. Otherwise, add the mangled name to the Names map and
    // return nullptr.
    const GlobalValue* addGlobalValue(StringMap<const GlobalValue*> &Names,
                                      const GlobalValue &GV,
                                      const Mangler &Mang, StringRef SearchName,
                                      bool ExportedSymbolsOnly) const {
      // Modules don't "provide" decls or common symbols.
      if (GV.isDeclaration() || GV.hasCommonLinkage())
        return nullptr;

      // Mangle the GV name.
      std::string MangledName;
      {
        raw_string_ostream MangledNameStream(MangledName);
        Mang.getNameWithPrefix(MangledNameStream, &GV, false);
      }

      // Check whether this is the name we were searching for, and if it is then
      // bail out early.
      if (MangledName == SearchName)
        if (!ExportedSymbolsOnly || GV.hasDefaultVisibility())
          return &GV;

      // Otherwise add this to the map for later.
      Names[MangledName] = &GV;
      return nullptr;
    }

    // Build the MangledSymbols map. Bails out early (with MangledSymbols left set
    // to nullptr) if the given SearchName is found while building the map.
    const GlobalValue* buildMangledSymbols(StringRef SearchName,
                                           bool ExportedSymbolsOnly) const {
      assert(!MangledSymbols && "Mangled symbols map already exists?");

      auto Symbols = llvm::make_unique<StringMap<const GlobalValue*>>();

      Mangler Mang;

      for (const auto &GO : M->global_objects())
          if (auto GV = addGlobalValue(*Symbols, GO, Mang, SearchName,
                                       ExportedSymbolsOnly))
            return GV;

      MangledSymbols = std::move(Symbols);
      return nullptr;
    }

    enum { NotEmitted, Emitting, Emitted } EmitState = NotEmitted;
    VModuleKey K;
    std::unique_ptr<Module> M;
    mutable std::unique_ptr<StringMap<const GlobalValue*>> MangledSymbols;
  };

  BaseLayerT &BaseLayer;
  std::map<VModuleKey, std::unique_ptr<EmissionDeferredModule>> ModuleMap;

public:

  /// Construct a lazy emitting layer.
  LazyEmittingLayer(BaseLayerT &BaseLayer) : BaseLayer(BaseLayer) {}

  /// Add the given module to the lazy emitting layer.
  Error addModule(VModuleKey K, std::unique_ptr<Module> M) {
    assert(!ModuleMap.count(K) && "VModuleKey K already in use");
    ModuleMap[K] =
        llvm::make_unique<EmissionDeferredModule>(std::move(K), std::move(M));
    return Error::success();
  }

  /// Remove the module represented by the given handle.
  ///
  ///   This method will free the memory associated with the given module, both
  /// in this layer, and the base layer.
  Error removeModule(VModuleKey K) {
    auto I = ModuleMap.find(K);
    assert(I != ModuleMap.end() && "VModuleKey K not valid here");
    auto EDM = std::move(I.second);
    ModuleMap.erase(I);
    return EDM->removeModuleFromBaseLayer(BaseLayer);
  }

  /// Search for the given named symbol.
  /// @param Name The name of the symbol to search for.
  /// @param ExportedSymbolsOnly If true, search only for exported symbols.
  /// @return A handle for the given named symbol, if it exists.
  JITSymbol findSymbol(const std::string &Name, bool ExportedSymbolsOnly) {
    // Look for the symbol among existing definitions.
    if (auto Symbol = BaseLayer.findSymbol(Name, ExportedSymbolsOnly))
      return Symbol;

    // If not found then search the deferred modules. If any of these contain a
    // definition of 'Name' then they will return a JITSymbol that will emit
    // the corresponding module when the symbol address is requested.
    for (auto &KV : ModuleMap)
      if (auto Symbol = KV.second->find(Name, ExportedSymbolsOnly, BaseLayer))
        return Symbol;

    // If no definition found anywhere return a null symbol.
    return nullptr;
  }

  /// Get the address of the given symbol in the context of the of
  ///        compiled modules represented by the key K.
  JITSymbol findSymbolIn(VModuleKey K, const std::string &Name,
                         bool ExportedSymbolsOnly) {
    assert(ModuleMap.count(K) && "VModuleKey K not valid here");
    return ModuleMap[K]->find(Name, ExportedSymbolsOnly, BaseLayer);
  }

  /// Immediately emit and finalize the module represented by the given
  ///        key.
  Error emitAndFinalize(VModuleKey K) {
    assert(ModuleMap.count(K) && "VModuleKey K not valid here");
    return ModuleMap[K]->emitAndFinalize(BaseLayer);
  }
};

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_LAZYEMITTINGLAYER_H
