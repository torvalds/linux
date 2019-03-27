//===---------------- Layer.h -- Layer interfaces --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Layer interfaces.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_LAYER_H
#define LLVM_EXECUTIONENGINE_ORC_LAYER_H

#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/MemoryBuffer.h"

namespace llvm {
namespace orc {

/// Interface for layers that accept LLVM IR.
class IRLayer {
public:
  IRLayer(ExecutionSession &ES);
  virtual ~IRLayer();

  /// Returns the ExecutionSession for this layer.
  ExecutionSession &getExecutionSession() { return ES; }

  /// Sets the CloneToNewContextOnEmit flag (false by default).
  ///
  /// When set, IR modules added to this layer will be cloned on to a new
  /// context before emit is called. This can be used by clients who want
  /// to load all IR using one LLVMContext (to save memory via type and
  /// constant uniquing), but want to move Modules to fresh contexts before
  /// compiling them to enable concurrent compilation.
  /// Single threaded clients, or clients who load every module on a new
  /// context, need not set this.
  void setCloneToNewContextOnEmit(bool CloneToNewContextOnEmit) {
    this->CloneToNewContextOnEmit = CloneToNewContextOnEmit;
  }

  /// Returns the current value of the CloneToNewContextOnEmit flag.
  bool getCloneToNewContextOnEmit() const { return CloneToNewContextOnEmit; }

  /// Adds a MaterializationUnit representing the given IR to the given
  /// JITDylib.
  virtual Error add(JITDylib &JD, ThreadSafeModule TSM,
                    VModuleKey K = VModuleKey());

  /// Emit should materialize the given IR.
  virtual void emit(MaterializationResponsibility R, ThreadSafeModule TSM) = 0;

private:
  bool CloneToNewContextOnEmit = false;
  ExecutionSession &ES;
};

/// IRMaterializationUnit is a convenient base class for MaterializationUnits
/// wrapping LLVM IR. Represents materialization responsibility for all symbols
/// in the given module. If symbols are overridden by other definitions, then
/// their linkage is changed to available-externally.
class IRMaterializationUnit : public MaterializationUnit {
public:
  using SymbolNameToDefinitionMap = std::map<SymbolStringPtr, GlobalValue *>;

  /// Create an IRMaterializationLayer. Scans the module to build the
  /// SymbolFlags and SymbolToDefinition maps.
  IRMaterializationUnit(ExecutionSession &ES, ThreadSafeModule TSM,
                        VModuleKey K);

  /// Create an IRMaterializationLayer from a module, and pre-existing
  /// SymbolFlags and SymbolToDefinition maps. The maps must provide
  /// entries for each definition in M.
  /// This constructor is useful for delegating work from one
  /// IRMaterializationUnit to another.
  IRMaterializationUnit(ThreadSafeModule TSM, VModuleKey K,
                        SymbolFlagsMap SymbolFlags,
                        SymbolNameToDefinitionMap SymbolToDefinition);

  /// Return the ModuleIdentifier as the name for this MaterializationUnit.
  StringRef getName() const override;

  const ThreadSafeModule &getModule() const { return TSM; }

protected:
  ThreadSafeModule TSM;
  SymbolNameToDefinitionMap SymbolToDefinition;

private:
  void discard(const JITDylib &JD, const SymbolStringPtr &Name) override;
};

/// MaterializationUnit that materializes modules by calling the 'emit' method
/// on the given IRLayer.
class BasicIRLayerMaterializationUnit : public IRMaterializationUnit {
public:
  BasicIRLayerMaterializationUnit(IRLayer &L, VModuleKey K,
                                  ThreadSafeModule TSM);

private:

  void materialize(MaterializationResponsibility R) override;

  IRLayer &L;
  VModuleKey K;
};

/// Interface for Layers that accept object files.
class ObjectLayer {
public:
  ObjectLayer(ExecutionSession &ES);
  virtual ~ObjectLayer();

  /// Returns the execution session for this layer.
  ExecutionSession &getExecutionSession() { return ES; }

  /// Adds a MaterializationUnit representing the given IR to the given
  /// JITDylib.
  virtual Error add(JITDylib &JD, std::unique_ptr<MemoryBuffer> O,
                    VModuleKey K = VModuleKey());

  /// Emit should materialize the given IR.
  virtual void emit(MaterializationResponsibility R,
                    std::unique_ptr<MemoryBuffer> O) = 0;

private:
  ExecutionSession &ES;
};

/// Materializes the given object file (represented by a MemoryBuffer
/// instance) by calling 'emit' on the given ObjectLayer.
class BasicObjectLayerMaterializationUnit : public MaterializationUnit {
public:
  static Expected<std::unique_ptr<BasicObjectLayerMaterializationUnit>>
  Create(ObjectLayer &L, VModuleKey K, std::unique_ptr<MemoryBuffer> O);

  BasicObjectLayerMaterializationUnit(ObjectLayer &L, VModuleKey K,
                                      std::unique_ptr<MemoryBuffer> O,
                                      SymbolFlagsMap SymbolFlags);

  /// Return the buffer's identifier as the name for this MaterializationUnit.
  StringRef getName() const override;

private:

  void materialize(MaterializationResponsibility R) override;
  void discard(const JITDylib &JD, const SymbolStringPtr &Name) override;

  ObjectLayer &L;
  std::unique_ptr<MemoryBuffer> O;
};

/// Returns a SymbolFlagsMap for the object file represented by the given
/// buffer, or an error if the buffer does not contain a valid object file.
// FIXME: Maybe move to Core.h?
Expected<SymbolFlagsMap> getObjectSymbolFlags(ExecutionSession &ES,
                                              MemoryBufferRef ObjBuffer);

} // End namespace orc
} // End namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_LAYER_H
