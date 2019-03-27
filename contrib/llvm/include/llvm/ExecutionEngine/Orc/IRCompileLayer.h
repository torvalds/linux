//===- IRCompileLayer.h -- Eagerly compile IR for JIT -----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Contains the definition for a basic, eagerly compiling layer of the JIT.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_IRCOMPILELAYER_H
#define LLVM_EXECUTIONENGINE_ORC_IRCOMPILELAYER_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/Layer.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include <memory>
#include <string>

namespace llvm {

class Module;

namespace orc {

class IRCompileLayer : public IRLayer {
public:
  using CompileFunction =
      std::function<Expected<std::unique_ptr<MemoryBuffer>>(Module &)>;

  using NotifyCompiledFunction =
      std::function<void(VModuleKey K, ThreadSafeModule TSM)>;

  IRCompileLayer(ExecutionSession &ES, ObjectLayer &BaseLayer,
                 CompileFunction Compile);

  void setNotifyCompiled(NotifyCompiledFunction NotifyCompiled);

  void emit(MaterializationResponsibility R, ThreadSafeModule TSM) override;

private:
  mutable std::mutex IRLayerMutex;
  ObjectLayer &BaseLayer;
  CompileFunction Compile;
  NotifyCompiledFunction NotifyCompiled = NotifyCompiledFunction();
};

/// Eager IR compiling layer.
///
///   This layer immediately compiles each IR module added via addModule to an
/// object file and adds this module file to the layer below, which must
/// implement the object layer concept.
template <typename BaseLayerT, typename CompileFtor>
class LegacyIRCompileLayer {
public:
  /// Callback type for notifications when modules are compiled.
  using NotifyCompiledCallback =
      std::function<void(VModuleKey K, std::unique_ptr<Module>)>;

  /// Construct an LegacyIRCompileLayer with the given BaseLayer, which must
  ///        implement the ObjectLayer concept.
  LegacyIRCompileLayer(
      BaseLayerT &BaseLayer, CompileFtor Compile,
      NotifyCompiledCallback NotifyCompiled = NotifyCompiledCallback())
      : BaseLayer(BaseLayer), Compile(std::move(Compile)),
        NotifyCompiled(std::move(NotifyCompiled)) {}

  /// Get a reference to the compiler functor.
  CompileFtor& getCompiler() { return Compile; }

  /// (Re)set the NotifyCompiled callback.
  void setNotifyCompiled(NotifyCompiledCallback NotifyCompiled) {
    this->NotifyCompiled = std::move(NotifyCompiled);
  }

  /// Compile the module, and add the resulting object to the base layer
  ///        along with the given memory manager and symbol resolver.
  Error addModule(VModuleKey K, std::unique_ptr<Module> M) {
    if (auto Err = BaseLayer.addObject(std::move(K), Compile(*M)))
      return Err;
    if (NotifyCompiled)
      NotifyCompiled(std::move(K), std::move(M));
    return Error::success();
  }

  /// Remove the module associated with the VModuleKey K.
  Error removeModule(VModuleKey K) { return BaseLayer.removeObject(K); }

  /// Search for the given named symbol.
  /// @param Name The name of the symbol to search for.
  /// @param ExportedSymbolsOnly If true, search only for exported symbols.
  /// @return A handle for the given named symbol, if it exists.
  JITSymbol findSymbol(const std::string &Name, bool ExportedSymbolsOnly) {
    return BaseLayer.findSymbol(Name, ExportedSymbolsOnly);
  }

  /// Get the address of the given symbol in compiled module represented
  ///        by the handle H. This call is forwarded to the base layer's
  ///        implementation.
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
  ///        handle.
  /// @param K The VModuleKey for the module to emit/finalize.
  Error emitAndFinalize(VModuleKey K) { return BaseLayer.emitAndFinalize(K); }

private:
  BaseLayerT &BaseLayer;
  CompileFtor Compile;
  NotifyCompiledCallback NotifyCompiled;
};

} // end namespace orc

} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_IRCOMPILINGLAYER_H
