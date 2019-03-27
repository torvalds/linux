//===- RuntimeDyld.h - Run-time dynamic linker for MC-JIT -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Interface for the runtime dynamic linker facilities of the MC-JIT.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_RUNTIMEDYLD_H
#define LLVM_EXECUTIONENGINE_RUNTIMEDYLD_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <system_error>

namespace llvm {

namespace object {

template <typename T> class OwningBinary;

} // end namespace object

/// Base class for errors originating in RuntimeDyld, e.g. missing relocation
/// support.
class RuntimeDyldError : public ErrorInfo<RuntimeDyldError> {
public:
  static char ID;

  RuntimeDyldError(std::string ErrMsg) : ErrMsg(std::move(ErrMsg)) {}

  void log(raw_ostream &OS) const override;
  const std::string &getErrorMessage() const { return ErrMsg; }
  std::error_code convertToErrorCode() const override;

private:
  std::string ErrMsg;
};

class RuntimeDyldCheckerImpl;
class RuntimeDyldImpl;

class RuntimeDyld {
  friend class RuntimeDyldCheckerImpl;

protected:
  // Change the address associated with a section when resolving relocations.
  // Any relocations already associated with the symbol will be re-resolved.
  void reassignSectionAddress(unsigned SectionID, uint64_t Addr);

public:
  /// Information about the loaded object.
  class LoadedObjectInfo : public llvm::LoadedObjectInfo {
    friend class RuntimeDyldImpl;

  public:
    using ObjSectionToIDMap = std::map<object::SectionRef, unsigned>;

    LoadedObjectInfo(RuntimeDyldImpl &RTDyld, ObjSectionToIDMap ObjSecToIDMap)
        : RTDyld(RTDyld), ObjSecToIDMap(std::move(ObjSecToIDMap)) {}

    virtual object::OwningBinary<object::ObjectFile>
    getObjectForDebug(const object::ObjectFile &Obj) const = 0;

    uint64_t
    getSectionLoadAddress(const object::SectionRef &Sec) const override;

  protected:
    virtual void anchor();

    RuntimeDyldImpl &RTDyld;
    ObjSectionToIDMap ObjSecToIDMap;
  };

  /// Memory Management.
  class MemoryManager {
    friend class RuntimeDyld;

  public:
    MemoryManager() = default;
    virtual ~MemoryManager() = default;

    /// Allocate a memory block of (at least) the given size suitable for
    /// executable code. The SectionID is a unique identifier assigned by the
    /// RuntimeDyld instance, and optionally recorded by the memory manager to
    /// access a loaded section.
    virtual uint8_t *allocateCodeSection(uintptr_t Size, unsigned Alignment,
                                         unsigned SectionID,
                                         StringRef SectionName) = 0;

    /// Allocate a memory block of (at least) the given size suitable for data.
    /// The SectionID is a unique identifier assigned by the JIT engine, and
    /// optionally recorded by the memory manager to access a loaded section.
    virtual uint8_t *allocateDataSection(uintptr_t Size, unsigned Alignment,
                                         unsigned SectionID,
                                         StringRef SectionName,
                                         bool IsReadOnly) = 0;

    /// Inform the memory manager about the total amount of memory required to
    /// allocate all sections to be loaded:
    /// \p CodeSize - the total size of all code sections
    /// \p DataSizeRO - the total size of all read-only data sections
    /// \p DataSizeRW - the total size of all read-write data sections
    ///
    /// Note that by default the callback is disabled. To enable it
    /// redefine the method needsToReserveAllocationSpace to return true.
    virtual void reserveAllocationSpace(uintptr_t CodeSize, uint32_t CodeAlign,
                                        uintptr_t RODataSize,
                                        uint32_t RODataAlign,
                                        uintptr_t RWDataSize,
                                        uint32_t RWDataAlign) {}

    /// Override to return true to enable the reserveAllocationSpace callback.
    virtual bool needsToReserveAllocationSpace() { return false; }

    /// Register the EH frames with the runtime so that c++ exceptions work.
    ///
    /// \p Addr parameter provides the local address of the EH frame section
    /// data, while \p LoadAddr provides the address of the data in the target
    /// address space.  If the section has not been remapped (which will usually
    /// be the case for local execution) these two values will be the same.
    virtual void registerEHFrames(uint8_t *Addr, uint64_t LoadAddr,
                                  size_t Size) = 0;
    virtual void deregisterEHFrames() = 0;

    /// This method is called when object loading is complete and section page
    /// permissions can be applied.  It is up to the memory manager implementation
    /// to decide whether or not to act on this method.  The memory manager will
    /// typically allocate all sections as read-write and then apply specific
    /// permissions when this method is called.  Code sections cannot be executed
    /// until this function has been called.  In addition, any cache coherency
    /// operations needed to reliably use the memory are also performed.
    ///
    /// Returns true if an error occurred, false otherwise.
    virtual bool finalizeMemory(std::string *ErrMsg = nullptr) = 0;

    /// This method is called after an object has been loaded into memory but
    /// before relocations are applied to the loaded sections.
    ///
    /// Memory managers which are preparing code for execution in an external
    /// address space can use this call to remap the section addresses for the
    /// newly loaded object.
    ///
    /// For clients that do not need access to an ExecutionEngine instance this
    /// method should be preferred to its cousin
    /// MCJITMemoryManager::notifyObjectLoaded as this method is compatible with
    /// ORC JIT stacks.
    virtual void notifyObjectLoaded(RuntimeDyld &RTDyld,
                                    const object::ObjectFile &Obj) {}

  private:
    virtual void anchor();

    bool FinalizationLocked = false;
  };

  /// Construct a RuntimeDyld instance.
  RuntimeDyld(MemoryManager &MemMgr, JITSymbolResolver &Resolver);
  RuntimeDyld(const RuntimeDyld &) = delete;
  RuntimeDyld &operator=(const RuntimeDyld &) = delete;
  ~RuntimeDyld();

  /// Add the referenced object file to the list of objects to be loaded and
  /// relocated.
  std::unique_ptr<LoadedObjectInfo> loadObject(const object::ObjectFile &O);

  /// Get the address of our local copy of the symbol. This may or may not
  /// be the address used for relocation (clients can copy the data around
  /// and resolve relocatons based on where they put it).
  void *getSymbolLocalAddress(StringRef Name) const;

  /// Get the target address and flags for the named symbol.
  /// This address is the one used for relocation.
  JITEvaluatedSymbol getSymbol(StringRef Name) const;

  /// Returns a copy of the symbol table. This can be used by on-finalized
  /// callbacks to extract the symbol table before throwing away the
  /// RuntimeDyld instance. Because the map keys (StringRefs) are backed by
  /// strings inside the RuntimeDyld instance, the map should be processed
  /// before the RuntimeDyld instance is discarded.
  std::map<StringRef, JITEvaluatedSymbol> getSymbolTable() const;

  /// Resolve the relocations for all symbols we currently know about.
  void resolveRelocations();

  /// Map a section to its target address space value.
  /// Map the address of a JIT section as returned from the memory manager
  /// to the address in the target process as the running code will see it.
  /// This is the address which will be used for relocation resolution.
  void mapSectionAddress(const void *LocalAddress, uint64_t TargetAddress);

  /// Register any EH frame sections that have been loaded but not previously
  /// registered with the memory manager.  Note, RuntimeDyld is responsible
  /// for identifying the EH frame and calling the memory manager with the
  /// EH frame section data.  However, the memory manager itself will handle
  /// the actual target-specific EH frame registration.
  void registerEHFrames();

  void deregisterEHFrames();

  bool hasError();
  StringRef getErrorString();

  /// By default, only sections that are "required for execution" are passed to
  /// the RTDyldMemoryManager, and other sections are discarded. Passing 'true'
  /// to this method will cause RuntimeDyld to pass all sections to its
  /// memory manager regardless of whether they are "required to execute" in the
  /// usual sense. This is useful for inspecting metadata sections that may not
  /// contain relocations, E.g. Debug info, stackmaps.
  ///
  /// Must be called before the first object file is loaded.
  void setProcessAllSections(bool ProcessAllSections) {
    assert(!Dyld && "setProcessAllSections must be called before loadObject.");
    this->ProcessAllSections = ProcessAllSections;
  }

  /// Perform all actions needed to make the code owned by this RuntimeDyld
  /// instance executable:
  ///
  /// 1) Apply relocations.
  /// 2) Register EH frames.
  /// 3) Update memory permissions*.
  ///
  /// * Finalization is potentially recursive**, and the 3rd step will only be
  ///   applied by the outermost call to finalize. This allows different
  ///   RuntimeDyld instances to share a memory manager without the innermost
  ///   finalization locking the memory and causing relocation fixup errors in
  ///   outer instances.
  ///
  /// ** Recursive finalization occurs when one RuntimeDyld instances needs the
  ///   address of a symbol owned by some other instance in order to apply
  ///   relocations.
  ///
  void finalizeWithMemoryManagerLocking();

private:
  friend void
  jitLinkForORC(object::ObjectFile &Obj,
                std::unique_ptr<MemoryBuffer> UnderlyingBuffer,
                RuntimeDyld::MemoryManager &MemMgr, JITSymbolResolver &Resolver,
                bool ProcessAllSections,
                std::function<Error(std::unique_ptr<LoadedObjectInfo>,
                                    std::map<StringRef, JITEvaluatedSymbol>)>
                    OnLoaded,
                std::function<void(Error)> OnEmitted);

  // RuntimeDyldImpl is the actual class. RuntimeDyld is just the public
  // interface.
  std::unique_ptr<RuntimeDyldImpl> Dyld;
  MemoryManager &MemMgr;
  JITSymbolResolver &Resolver;
  bool ProcessAllSections;
  RuntimeDyldCheckerImpl *Checker;
};

// Asynchronous JIT link for ORC.
//
// Warning: This API is experimental and probably should not be used by anyone
// but ORC's RTDyldObjectLinkingLayer2. Internally it constructs a RuntimeDyld
// instance and uses continuation passing to perform the fix-up and finalize
// steps asynchronously.
void jitLinkForORC(object::ObjectFile &Obj,
                   std::unique_ptr<MemoryBuffer> UnderlyingBuffer,
                   RuntimeDyld::MemoryManager &MemMgr,
                   JITSymbolResolver &Resolver, bool ProcessAllSections,
                   std::function<Error(std::unique_ptr<LoadedObjectInfo>,
                                       std::map<StringRef, JITEvaluatedSymbol>)>
                       OnLoaded,
                   std::function<void(Error)> OnEmitted);

} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_RUNTIMEDYLD_H
