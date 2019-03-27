//===- RTDyldObjectLinkingLayer.h - RTDyld-based jit linking  ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Contains the definition for an RTDyld-based, in-process object linking layer.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_RTDYLDOBJECTLINKINGLAYER_H
#define LLVM_EXECUTIONENGINE_ORC_RTDYLDOBJECTLINKINGLAYER_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/Layer.h"
#include "llvm/ExecutionEngine/Orc/Legacy.h"
#include "llvm/ExecutionEngine/RuntimeDyld.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <cassert>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace llvm {
namespace orc {

class RTDyldObjectLinkingLayer : public ObjectLayer {
public:
  /// Functor for receiving object-loaded notifications.
  using NotifyLoadedFunction =
      std::function<void(VModuleKey, const object::ObjectFile &Obj,
                         const RuntimeDyld::LoadedObjectInfo &)>;

  /// Functor for receiving finalization notifications.
  using NotifyEmittedFunction = std::function<void(VModuleKey)>;

  using GetMemoryManagerFunction =
      std::function<std::unique_ptr<RuntimeDyld::MemoryManager>()>;

  /// Construct an ObjectLinkingLayer with the given NotifyLoaded,
  ///        and NotifyEmitted functors.
  RTDyldObjectLinkingLayer(
      ExecutionSession &ES, GetMemoryManagerFunction GetMemoryManager,
      NotifyLoadedFunction NotifyLoaded = NotifyLoadedFunction(),
      NotifyEmittedFunction NotifyEmitted = NotifyEmittedFunction());

  /// Emit the object.
  void emit(MaterializationResponsibility R,
            std::unique_ptr<MemoryBuffer> O) override;

  /// Set the 'ProcessAllSections' flag.
  ///
  /// If set to true, all sections in each object file will be allocated using
  /// the memory manager, rather than just the sections required for execution.
  ///
  /// This is kludgy, and may be removed in the future.
  RTDyldObjectLinkingLayer &setProcessAllSections(bool ProcessAllSections) {
    this->ProcessAllSections = ProcessAllSections;
    return *this;
  }

  /// Instructs this RTDyldLinkingLayer2 instance to override the symbol flags
  /// returned by RuntimeDyld for any given object file with the flags supplied
  /// by the MaterializationResponsibility instance. This is a workaround to
  /// support symbol visibility in COFF, which does not use the libObject's
  /// SF_Exported flag. Use only when generating / adding COFF object files.
  ///
  /// FIXME: We should be able to remove this if/when COFF properly tracks
  /// exported symbols.
  RTDyldObjectLinkingLayer &
  setOverrideObjectFlagsWithResponsibilityFlags(bool OverrideObjectFlags) {
    this->OverrideObjectFlags = OverrideObjectFlags;
    return *this;
  }

  /// If set, this RTDyldObjectLinkingLayer instance will claim responsibility
  /// for any symbols provided by a given object file that were not already in
  /// the MaterializationResponsibility instance. Setting this flag allows
  /// higher-level program representations (e.g. LLVM IR) to be added based on
  /// only a subset of the symbols they provide, without having to write
  /// intervening layers to scan and add the additional symbols. This trades
  /// diagnostic quality for convenience however: If all symbols are enumerated
  /// up-front then clashes can be detected and reported early (and usually
  /// deterministically). If this option is set, clashes for the additional
  /// symbols may not be detected until late, and detection may depend on
  /// the flow of control through JIT'd code. Use with care.
  RTDyldObjectLinkingLayer &
  setAutoClaimResponsibilityForObjectSymbols(bool AutoClaimObjectSymbols) {
    this->AutoClaimObjectSymbols = AutoClaimObjectSymbols;
    return *this;
  }

private:
  Error onObjLoad(VModuleKey K, MaterializationResponsibility &R,
                  object::ObjectFile &Obj,
                  std::unique_ptr<RuntimeDyld::LoadedObjectInfo> LoadedObjInfo,
                  std::map<StringRef, JITEvaluatedSymbol> Resolved,
                  std::set<StringRef> &InternalSymbols);

  void onObjEmit(VModuleKey K, MaterializationResponsibility &R, Error Err);

  mutable std::mutex RTDyldLayerMutex;
  GetMemoryManagerFunction GetMemoryManager;
  NotifyLoadedFunction NotifyLoaded;
  NotifyEmittedFunction NotifyEmitted;
  bool ProcessAllSections = false;
  bool OverrideObjectFlags = false;
  bool AutoClaimObjectSymbols = false;
  std::vector<std::unique_ptr<RuntimeDyld::MemoryManager>> MemMgrs;
};

class LegacyRTDyldObjectLinkingLayerBase {
public:
  using ObjectPtr = std::unique_ptr<MemoryBuffer>;

protected:

  /// Holds an object to be allocated/linked as a unit in the JIT.
  ///
  /// An instance of this class will be created for each object added
  /// via JITObjectLayer::addObject. Deleting the instance (via
  /// removeObject) frees its memory, removing all symbol definitions that
  /// had been provided by this instance. Higher level layers are responsible
  /// for taking any action required to handle the missing symbols.
  class LinkedObject {
  public:
    LinkedObject() = default;
    LinkedObject(const LinkedObject&) = delete;
    void operator=(const LinkedObject&) = delete;
    virtual ~LinkedObject() = default;

    virtual Error finalize() = 0;

    virtual JITSymbol::GetAddressFtor
    getSymbolMaterializer(std::string Name) = 0;

    virtual void mapSectionAddress(const void *LocalAddress,
                                   JITTargetAddress TargetAddr) const = 0;

    JITSymbol getSymbol(StringRef Name, bool ExportedSymbolsOnly) {
      auto SymEntry = SymbolTable.find(Name);
      if (SymEntry == SymbolTable.end())
        return nullptr;
      if (!SymEntry->second.getFlags().isExported() && ExportedSymbolsOnly)
        return nullptr;
      if (!Finalized)
        return JITSymbol(getSymbolMaterializer(Name),
                         SymEntry->second.getFlags());
      return JITSymbol(SymEntry->second);
    }

  protected:
    StringMap<JITEvaluatedSymbol> SymbolTable;
    bool Finalized = false;
  };
};

/// Bare bones object linking layer.
///
///   This class is intended to be used as the base layer for a JIT. It allows
/// object files to be loaded into memory, linked, and the addresses of their
/// symbols queried. All objects added to this layer can see each other's
/// symbols.
class LegacyRTDyldObjectLinkingLayer : public LegacyRTDyldObjectLinkingLayerBase {
public:

  using LegacyRTDyldObjectLinkingLayerBase::ObjectPtr;

  /// Functor for receiving object-loaded notifications.
  using NotifyLoadedFtor =
      std::function<void(VModuleKey, const object::ObjectFile &Obj,
                         const RuntimeDyld::LoadedObjectInfo &)>;

  /// Functor for receiving finalization notifications.
  using NotifyFinalizedFtor =
      std::function<void(VModuleKey, const object::ObjectFile &Obj,
                         const RuntimeDyld::LoadedObjectInfo &)>;

  /// Functor for receiving deallocation notifications.
  using NotifyFreedFtor = std::function<void(VModuleKey, const object::ObjectFile &Obj)>;

private:
  using OwnedObject = object::OwningBinary<object::ObjectFile>;

  template <typename MemoryManagerPtrT>
  class ConcreteLinkedObject : public LinkedObject {
  public:
    ConcreteLinkedObject(LegacyRTDyldObjectLinkingLayer &Parent, VModuleKey K,
                         OwnedObject Obj, MemoryManagerPtrT MemMgr,
                         std::shared_ptr<SymbolResolver> Resolver,
                         bool ProcessAllSections)
        : K(std::move(K)),
          Parent(Parent),
          MemMgr(std::move(MemMgr)),
          PFC(llvm::make_unique<PreFinalizeContents>(
              std::move(Obj), std::move(Resolver),
              ProcessAllSections)) {
      buildInitialSymbolTable(PFC->Obj);
    }

    ~ConcreteLinkedObject() override {
      if (this->Parent.NotifyFreed && ObjForNotify.getBinary())
        this->Parent.NotifyFreed(K, *ObjForNotify.getBinary());

      MemMgr->deregisterEHFrames();
    }

    Error finalize() override {
      assert(PFC && "mapSectionAddress called on finalized LinkedObject");

      JITSymbolResolverAdapter ResolverAdapter(Parent.ES, *PFC->Resolver,
					       nullptr);
      PFC->RTDyld = llvm::make_unique<RuntimeDyld>(*MemMgr, ResolverAdapter);
      PFC->RTDyld->setProcessAllSections(PFC->ProcessAllSections);

      Finalized = true;

      std::unique_ptr<RuntimeDyld::LoadedObjectInfo> Info =
          PFC->RTDyld->loadObject(*PFC->Obj.getBinary());

      // Copy the symbol table out of the RuntimeDyld instance.
      {
        auto SymTab = PFC->RTDyld->getSymbolTable();
        for (auto &KV : SymTab)
          SymbolTable[KV.first] = KV.second;
      }

      if (Parent.NotifyLoaded)
        Parent.NotifyLoaded(K, *PFC->Obj.getBinary(), *Info);

      PFC->RTDyld->finalizeWithMemoryManagerLocking();

      if (PFC->RTDyld->hasError())
        return make_error<StringError>(PFC->RTDyld->getErrorString(),
                                       inconvertibleErrorCode());

      if (Parent.NotifyFinalized)
        Parent.NotifyFinalized(K, *PFC->Obj.getBinary(), *Info);

      // Release resources.
      if (this->Parent.NotifyFreed)
        ObjForNotify = std::move(PFC->Obj); // needed for callback
      PFC = nullptr;
      return Error::success();
    }

    JITSymbol::GetAddressFtor getSymbolMaterializer(std::string Name) override {
      return [this, Name]() -> Expected<JITTargetAddress> {
        // The symbol may be materialized between the creation of this lambda
        // and its execution, so we need to double check.
        if (!this->Finalized)
          if (auto Err = this->finalize())
            return std::move(Err);
        return this->getSymbol(Name, false).getAddress();
      };
    }

    void mapSectionAddress(const void *LocalAddress,
                           JITTargetAddress TargetAddr) const override {
      assert(PFC && "mapSectionAddress called on finalized LinkedObject");
      assert(PFC->RTDyld && "mapSectionAddress called on raw LinkedObject");
      PFC->RTDyld->mapSectionAddress(LocalAddress, TargetAddr);
    }

  private:
    void buildInitialSymbolTable(const OwnedObject &Obj) {
      for (auto &Symbol : Obj.getBinary()->symbols()) {
        if (Symbol.getFlags() & object::SymbolRef::SF_Undefined)
          continue;
        Expected<StringRef> SymbolName = Symbol.getName();
        // FIXME: Raise an error for bad symbols.
        if (!SymbolName) {
          consumeError(SymbolName.takeError());
          continue;
        }
        // FIXME: Raise an error for bad symbols.
        auto Flags = JITSymbolFlags::fromObjectSymbol(Symbol);
        if (!Flags) {
          consumeError(Flags.takeError());
          continue;
        }
        SymbolTable.insert(
            std::make_pair(*SymbolName, JITEvaluatedSymbol(0, *Flags)));
      }
    }

    // Contains the information needed prior to finalization: the object files,
    // memory manager, resolver, and flags needed for RuntimeDyld.
    struct PreFinalizeContents {
      PreFinalizeContents(OwnedObject Obj,
                          std::shared_ptr<SymbolResolver> Resolver,
                          bool ProcessAllSections)
          : Obj(std::move(Obj)),
            Resolver(std::move(Resolver)),
            ProcessAllSections(ProcessAllSections) {}

      OwnedObject Obj;
      std::shared_ptr<SymbolResolver> Resolver;
      bool ProcessAllSections;
      std::unique_ptr<RuntimeDyld> RTDyld;
    };

    VModuleKey K;
    LegacyRTDyldObjectLinkingLayer &Parent;
    MemoryManagerPtrT MemMgr;
    OwnedObject ObjForNotify;
    std::unique_ptr<PreFinalizeContents> PFC;
  };

  template <typename MemoryManagerPtrT>
  std::unique_ptr<ConcreteLinkedObject<MemoryManagerPtrT>>
  createLinkedObject(LegacyRTDyldObjectLinkingLayer &Parent, VModuleKey K,
                     OwnedObject Obj, MemoryManagerPtrT MemMgr,
                     std::shared_ptr<SymbolResolver> Resolver,
                     bool ProcessAllSections) {
    using LOS = ConcreteLinkedObject<MemoryManagerPtrT>;
    return llvm::make_unique<LOS>(Parent, std::move(K), std::move(Obj),
                                  std::move(MemMgr), std::move(Resolver),
                                  ProcessAllSections);
  }

public:
  struct Resources {
    std::shared_ptr<RuntimeDyld::MemoryManager> MemMgr;
    std::shared_ptr<SymbolResolver> Resolver;
  };

  using ResourcesGetter = std::function<Resources(VModuleKey)>;

  /// Construct an ObjectLinkingLayer with the given NotifyLoaded,
  ///        and NotifyFinalized functors.
  LegacyRTDyldObjectLinkingLayer(
      ExecutionSession &ES, ResourcesGetter GetResources,
      NotifyLoadedFtor NotifyLoaded = NotifyLoadedFtor(),
      NotifyFinalizedFtor NotifyFinalized = NotifyFinalizedFtor(),
      NotifyFreedFtor NotifyFreed = NotifyFreedFtor())
      : ES(ES), GetResources(std::move(GetResources)),
        NotifyLoaded(std::move(NotifyLoaded)),
        NotifyFinalized(std::move(NotifyFinalized)),
        NotifyFreed(std::move(NotifyFreed)),
        ProcessAllSections(false) {
  }

  /// Set the 'ProcessAllSections' flag.
  ///
  /// If set to true, all sections in each object file will be allocated using
  /// the memory manager, rather than just the sections required for execution.
  ///
  /// This is kludgy, and may be removed in the future.
  void setProcessAllSections(bool ProcessAllSections) {
    this->ProcessAllSections = ProcessAllSections;
  }

  /// Add an object to the JIT.
  Error addObject(VModuleKey K, ObjectPtr ObjBuffer) {

    auto Obj =
        object::ObjectFile::createObjectFile(ObjBuffer->getMemBufferRef());
    if (!Obj)
      return Obj.takeError();

    assert(!LinkedObjects.count(K) && "VModuleKey already in use");

    auto R = GetResources(K);

    LinkedObjects[K] = createLinkedObject(
        *this, K, OwnedObject(std::move(*Obj), std::move(ObjBuffer)),
        std::move(R.MemMgr), std::move(R.Resolver), ProcessAllSections);

    return Error::success();
  }

  /// Remove the object associated with VModuleKey K.
  ///
  ///   All memory allocated for the object will be freed, and the sections and
  /// symbols it provided will no longer be available. No attempt is made to
  /// re-emit the missing symbols, and any use of these symbols (directly or
  /// indirectly) will result in undefined behavior. If dependence tracking is
  /// required to detect or resolve such issues it should be added at a higher
  /// layer.
  Error removeObject(VModuleKey K) {
    assert(LinkedObjects.count(K) && "VModuleKey not associated with object");
    // How do we invalidate the symbols in H?
    LinkedObjects.erase(K);
    return Error::success();
  }

  /// Search for the given named symbol.
  /// @param Name The name of the symbol to search for.
  /// @param ExportedSymbolsOnly If true, search only for exported symbols.
  /// @return A handle for the given named symbol, if it exists.
  JITSymbol findSymbol(StringRef Name, bool ExportedSymbolsOnly) {
    for (auto &KV : LinkedObjects)
      if (auto Sym = KV.second->getSymbol(Name, ExportedSymbolsOnly))
        return Sym;
      else if (auto Err = Sym.takeError())
        return std::move(Err);

    return nullptr;
  }

  /// Search for the given named symbol in the context of the loaded
  ///        object represented by the VModuleKey K.
  /// @param K The VModuleKey for the object to search in.
  /// @param Name The name of the symbol to search for.
  /// @param ExportedSymbolsOnly If true, search only for exported symbols.
  /// @return A handle for the given named symbol, if it is found in the
  ///         given object.
  JITSymbol findSymbolIn(VModuleKey K, StringRef Name,
                         bool ExportedSymbolsOnly) {
    assert(LinkedObjects.count(K) && "VModuleKey not associated with object");
    return LinkedObjects[K]->getSymbol(Name, ExportedSymbolsOnly);
  }

  /// Map section addresses for the object associated with the
  ///        VModuleKey K.
  void mapSectionAddress(VModuleKey K, const void *LocalAddress,
                         JITTargetAddress TargetAddr) {
    assert(LinkedObjects.count(K) && "VModuleKey not associated with object");
    LinkedObjects[K]->mapSectionAddress(LocalAddress, TargetAddr);
  }

  /// Immediately emit and finalize the object represented by the given
  ///        VModuleKey.
  /// @param K VModuleKey for object to emit/finalize.
  Error emitAndFinalize(VModuleKey K) {
    assert(LinkedObjects.count(K) && "VModuleKey not associated with object");
    return LinkedObjects[K]->finalize();
  }

private:
  ExecutionSession &ES;

  ResourcesGetter GetResources;
  NotifyLoadedFtor NotifyLoaded;
  NotifyFinalizedFtor NotifyFinalized;
  NotifyFreedFtor NotifyFreed;

  // NB!  `LinkedObjects` needs to be destroyed before `NotifyFreed` because
  // `~ConcreteLinkedObject` calls `NotifyFreed`
  std::map<VModuleKey, std::unique_ptr<LinkedObject>> LinkedObjects;
  bool ProcessAllSections = false;
};

} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_RTDYLDOBJECTLINKINGLAYER_H
