//===-- RTDyldMemoryManager.cpp - Memory manager for MC-JIT -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Interface of the runtime dynamic memory manager base class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_RTDYLDMEMORYMANAGER_H
#define LLVM_EXECUTIONENGINE_RTDYLDMEMORYMANAGER_H

#include "llvm-c/ExecutionEngine.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/RuntimeDyld.h"
#include "llvm/Support/CBindingWrapping.h"
#include <cstddef>
#include <cstdint>
#include <string>

namespace llvm {

class ExecutionEngine;

namespace object {
  class ObjectFile;
} // end namespace object

class MCJITMemoryManager : public RuntimeDyld::MemoryManager {
public:
  // Don't hide the notifyObjectLoaded method from RuntimeDyld::MemoryManager.
  using RuntimeDyld::MemoryManager::notifyObjectLoaded;

  /// This method is called after an object has been loaded into memory but
  /// before relocations are applied to the loaded sections.  The object load
  /// may have been initiated by MCJIT to resolve an external symbol for another
  /// object that is being finalized.  In that case, the object about which
  /// the memory manager is being notified will be finalized immediately after
  /// the memory manager returns from this call.
  ///
  /// Memory managers which are preparing code for execution in an external
  /// address space can use this call to remap the section addresses for the
  /// newly loaded object.
  virtual void notifyObjectLoaded(ExecutionEngine *EE,
                                  const object::ObjectFile &) {}

private:
  void anchor() override;
};

// RuntimeDyld clients often want to handle the memory management of
// what gets placed where. For JIT clients, this is the subset of
// JITMemoryManager required for dynamic loading of binaries.
//
// FIXME: As the RuntimeDyld fills out, additional routines will be needed
//        for the varying types of objects to be allocated.
class RTDyldMemoryManager : public MCJITMemoryManager,
                            public LegacyJITSymbolResolver {
public:
  RTDyldMemoryManager() = default;
  RTDyldMemoryManager(const RTDyldMemoryManager&) = delete;
  void operator=(const RTDyldMemoryManager&) = delete;
  ~RTDyldMemoryManager() override;

  /// Register EH frames in the current process.
  static void registerEHFramesInProcess(uint8_t *Addr, size_t Size);

  /// Deregister EH frames in the current proces.
  static void deregisterEHFramesInProcess(uint8_t *Addr, size_t Size);

  void registerEHFrames(uint8_t *Addr, uint64_t LoadAddr, size_t Size) override;
  void deregisterEHFrames() override;

  /// This method returns the address of the specified function or variable in
  /// the current process.
  static uint64_t getSymbolAddressInProcess(const std::string &Name);

  /// Legacy symbol lookup - DEPRECATED! Please override findSymbol instead.
  ///
  /// This method returns the address of the specified function or variable.
  /// It is used to resolve symbols during module linking.
  virtual uint64_t getSymbolAddress(const std::string &Name) {
    return getSymbolAddressInProcess(Name);
  }

  /// This method returns a RuntimeDyld::SymbolInfo for the specified function
  /// or variable. It is used to resolve symbols during module linking.
  ///
  /// By default this falls back on the legacy lookup method:
  /// 'getSymbolAddress'. The address returned by getSymbolAddress is treated as
  /// a strong, exported symbol, consistent with historical treatment by
  /// RuntimeDyld.
  ///
  /// Clients writing custom RTDyldMemoryManagers are encouraged to override
  /// this method and return a SymbolInfo with the flags set correctly. This is
  /// necessary for RuntimeDyld to correctly handle weak and non-exported symbols.
  JITSymbol findSymbol(const std::string &Name) override {
    return JITSymbol(getSymbolAddress(Name), JITSymbolFlags::Exported);
  }

  /// Legacy symbol lookup -- DEPRECATED! Please override
  /// findSymbolInLogicalDylib instead.
  ///
  /// Default to treating all modules as separate.
  virtual uint64_t getSymbolAddressInLogicalDylib(const std::string &Name) {
    return 0;
  }

  /// Default to treating all modules as separate.
  ///
  /// By default this falls back on the legacy lookup method:
  /// 'getSymbolAddressInLogicalDylib'. The address returned by
  /// getSymbolAddressInLogicalDylib is treated as a strong, exported symbol,
  /// consistent with historical treatment by RuntimeDyld.
  ///
  /// Clients writing custom RTDyldMemoryManagers are encouraged to override
  /// this method and return a SymbolInfo with the flags set correctly. This is
  /// necessary for RuntimeDyld to correctly handle weak and non-exported symbols.
  JITSymbol
  findSymbolInLogicalDylib(const std::string &Name) override {
    return JITSymbol(getSymbolAddressInLogicalDylib(Name),
                          JITSymbolFlags::Exported);
  }

  /// This method returns the address of the specified function. As such it is
  /// only useful for resolving library symbols, not code generated symbols.
  ///
  /// If \p AbortOnFailure is false and no function with the given name is
  /// found, this function returns a null pointer. Otherwise, it prints a
  /// message to stderr and aborts.
  ///
  /// This function is deprecated for memory managers to be used with
  /// MCJIT or RuntimeDyld.  Use getSymbolAddress instead.
  virtual void *getPointerToNamedFunction(const std::string &Name,
                                          bool AbortOnFailure = true);

protected:
  struct EHFrame {
    uint8_t *Addr;
    size_t Size;
  };
  typedef std::vector<EHFrame> EHFrameInfos;
  EHFrameInfos EHFrames;

private:
  void anchor() override;
};

// Create wrappers for C Binding types (see CBindingWrapping.h).
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(
    RTDyldMemoryManager, LLVMMCJITMemoryManagerRef)

} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_RTDYLDMEMORYMANAGER_H
