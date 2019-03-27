//===- OrcCBindingsStack.h - Orc JIT stack for C bindings -----*- C++ -*---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_EXECUTIONENGINE_ORC_ORCCBINDINGSSTACK_H
#define LLVM_LIB_EXECUTIONENGINE_ORC_ORCCBINDINGSSTACK_H

#include "llvm-c/OrcBindings.h"
#include "llvm-c/TargetMachine.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/JITEventListener.h"
#include "llvm/ExecutionEngine/Orc/CompileOnDemandLayer.h"
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/LambdaResolver.h"
#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/RuntimeDyld.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CBindingWrapping.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace llvm {

class OrcCBindingsStack;

DEFINE_SIMPLE_CONVERSION_FUNCTIONS(OrcCBindingsStack, LLVMOrcJITStackRef)
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(TargetMachine, LLVMTargetMachineRef)

namespace detail {

// FIXME: Kill this off once the Layer concept becomes an interface.
class GenericLayer {
public:
  virtual ~GenericLayer() = default;

  virtual JITSymbol findSymbolIn(orc::VModuleKey K, const std::string &Name,
                                 bool ExportedSymbolsOnly) = 0;
  virtual Error removeModule(orc::VModuleKey K) = 0;
  };

  template <typename LayerT> class GenericLayerImpl : public GenericLayer {
  public:
    GenericLayerImpl(LayerT &Layer) : Layer(Layer) {}

    JITSymbol findSymbolIn(orc::VModuleKey K, const std::string &Name,
                           bool ExportedSymbolsOnly) override {
      return Layer.findSymbolIn(K, Name, ExportedSymbolsOnly);
    }

    Error removeModule(orc::VModuleKey K) override {
      return Layer.removeModule(K);
    }

  private:
    LayerT &Layer;
  };

  template <>
  class GenericLayerImpl<orc::LegacyRTDyldObjectLinkingLayer> : public GenericLayer {
  private:
    using LayerT = orc::LegacyRTDyldObjectLinkingLayer;
  public:
    GenericLayerImpl(LayerT &Layer) : Layer(Layer) {}

    JITSymbol findSymbolIn(orc::VModuleKey K, const std::string &Name,
                           bool ExportedSymbolsOnly) override {
      return Layer.findSymbolIn(K, Name, ExportedSymbolsOnly);
    }

    Error removeModule(orc::VModuleKey K) override {
      return Layer.removeObject(K);
    }

  private:
    LayerT &Layer;
  };

  template <typename LayerT>
  std::unique_ptr<GenericLayerImpl<LayerT>> createGenericLayer(LayerT &Layer) {
    return llvm::make_unique<GenericLayerImpl<LayerT>>(Layer);
  }

} // end namespace detail

class OrcCBindingsStack {
public:

  using CompileCallbackMgr = orc::JITCompileCallbackManager;
  using ObjLayerT = orc::LegacyRTDyldObjectLinkingLayer;
  using CompileLayerT = orc::LegacyIRCompileLayer<ObjLayerT, orc::SimpleCompiler>;
  using CODLayerT =
        orc::LegacyCompileOnDemandLayer<CompileLayerT, CompileCallbackMgr>;

  using CallbackManagerBuilder =
      std::function<std::unique_ptr<CompileCallbackMgr>()>;

  using IndirectStubsManagerBuilder = CODLayerT::IndirectStubsManagerBuilderT;

private:

  using OwningObject = object::OwningBinary<object::ObjectFile>;

  class CBindingsResolver : public orc::SymbolResolver {
  public:
    CBindingsResolver(OrcCBindingsStack &Stack,
                      LLVMOrcSymbolResolverFn ExternalResolver,
                      void *ExternalResolverCtx)
        : Stack(Stack), ExternalResolver(std::move(ExternalResolver)),
          ExternalResolverCtx(std::move(ExternalResolverCtx)) {}

    orc::SymbolNameSet
    getResponsibilitySet(const orc::SymbolNameSet &Symbols) override {
      orc::SymbolNameSet Result;

      for (auto &S : Symbols) {
        if (auto Sym = findSymbol(*S)) {
          if (!Sym.getFlags().isStrong())
            Result.insert(S);
        } else if (auto Err = Sym.takeError()) {
          Stack.reportError(std::move(Err));
          return orc::SymbolNameSet();
        }
      }

      return Result;
    }

    orc::SymbolNameSet
    lookup(std::shared_ptr<orc::AsynchronousSymbolQuery> Query,
           orc::SymbolNameSet Symbols) override {
      orc::SymbolNameSet UnresolvedSymbols;

      for (auto &S : Symbols) {
        if (auto Sym = findSymbol(*S)) {
          if (auto Addr = Sym.getAddress()) {
            Query->resolve(S, JITEvaluatedSymbol(*Addr, Sym.getFlags()));
            Query->notifySymbolReady();
          } else {
            Stack.ES.legacyFailQuery(*Query, Addr.takeError());
            return orc::SymbolNameSet();
          }
        } else if (auto Err = Sym.takeError()) {
          Stack.ES.legacyFailQuery(*Query, std::move(Err));
          return orc::SymbolNameSet();
        } else
          UnresolvedSymbols.insert(S);
      }

      if (Query->isFullyResolved())
        Query->handleFullyResolved();

      if (Query->isFullyReady())
        Query->handleFullyReady();

      return UnresolvedSymbols;
    }

  private:
    JITSymbol findSymbol(const std::string &Name) {
      // Search order:
      // 1. JIT'd symbols.
      // 2. Runtime overrides.
      // 3. External resolver (if present).

      if (Stack.CODLayer) {
        if (auto Sym = Stack.CODLayer->findSymbol(Name, true))
          return Sym;
        else if (auto Err = Sym.takeError())
          return Sym.takeError();
      } else {
        if (auto Sym = Stack.CompileLayer.findSymbol(Name, true))
          return Sym;
        else if (auto Err = Sym.takeError())
          return Sym.takeError();
      }

      if (auto Sym = Stack.CXXRuntimeOverrides.searchOverrides(Name))
        return Sym;

      if (ExternalResolver)
        return JITSymbol(ExternalResolver(Name.c_str(), ExternalResolverCtx),
                         JITSymbolFlags::Exported);

      return JITSymbol(nullptr);
    }

    OrcCBindingsStack &Stack;
    LLVMOrcSymbolResolverFn ExternalResolver;
    void *ExternalResolverCtx = nullptr;
  };

public:
  OrcCBindingsStack(TargetMachine &TM,
                    IndirectStubsManagerBuilder IndirectStubsMgrBuilder)
      : CCMgr(createCompileCallbackManager(TM, ES)), DL(TM.createDataLayout()),
        IndirectStubsMgr(IndirectStubsMgrBuilder()),
        ObjectLayer(ES,
                    [this](orc::VModuleKey K) {
                      auto ResolverI = Resolvers.find(K);
                      assert(ResolverI != Resolvers.end() &&
                             "No resolver for module K");
                      auto Resolver = std::move(ResolverI->second);
                      Resolvers.erase(ResolverI);
                      return ObjLayerT::Resources{
                          std::make_shared<SectionMemoryManager>(), Resolver};
                    },
                    nullptr,
                    [this](orc::VModuleKey K, const object::ObjectFile &Obj,
                           const RuntimeDyld::LoadedObjectInfo &LoadedObjInfo) {
		      this->notifyFinalized(K, Obj, LoadedObjInfo);
                    },
                    [this](orc::VModuleKey K, const object::ObjectFile &Obj) {
		      this->notifyFreed(K, Obj);
                    }),
        CompileLayer(ObjectLayer, orc::SimpleCompiler(TM)),
        CODLayer(createCODLayer(ES, CompileLayer, CCMgr.get(),
                                std::move(IndirectStubsMgrBuilder), Resolvers)),
        CXXRuntimeOverrides(
            [this](const std::string &S) { return mangle(S); }) {}

  Error shutdown() {
    // Run any destructors registered with __cxa_atexit.
    CXXRuntimeOverrides.runDestructors();
    // Run any IR destructors.
    for (auto &DtorRunner : IRStaticDestructorRunners)
      if (auto Err = DtorRunner.runViaLayer(*this))
        return Err;
    return Error::success();
  }

  std::string mangle(StringRef Name) {
    std::string MangledName;
    {
      raw_string_ostream MangledNameStream(MangledName);
      Mangler::getNameWithPrefix(MangledNameStream, Name, DL);
    }
    return MangledName;
  }

  template <typename PtrTy>
  static PtrTy fromTargetAddress(JITTargetAddress Addr) {
    return reinterpret_cast<PtrTy>(static_cast<uintptr_t>(Addr));
  }

  Expected<JITTargetAddress>
  createLazyCompileCallback(LLVMOrcLazyCompileCallbackFn Callback,
                            void *CallbackCtx) {
    auto WrappedCallback = [=]() -> JITTargetAddress {
      return Callback(wrap(this), CallbackCtx);
    };

    return CCMgr->getCompileCallback(std::move(WrappedCallback));
  }

  Error createIndirectStub(StringRef StubName, JITTargetAddress Addr) {
    return IndirectStubsMgr->createStub(StubName, Addr,
                                        JITSymbolFlags::Exported);
  }

  Error setIndirectStubPointer(StringRef Name, JITTargetAddress Addr) {
    return IndirectStubsMgr->updatePointer(Name, Addr);
  }

  template <typename LayerT>
  Expected<orc::VModuleKey>
  addIRModule(LayerT &Layer, std::unique_ptr<Module> M,
              std::unique_ptr<RuntimeDyld::MemoryManager> MemMgr,
              LLVMOrcSymbolResolverFn ExternalResolver,
              void *ExternalResolverCtx) {

    // Attach a data-layout if one isn't already present.
    if (M->getDataLayout().isDefault())
      M->setDataLayout(DL);

    // Record the static constructors and destructors. We have to do this before
    // we hand over ownership of the module to the JIT.
    std::vector<std::string> CtorNames, DtorNames;
    for (auto Ctor : orc::getConstructors(*M))
      CtorNames.push_back(mangle(Ctor.Func->getName()));
    for (auto Dtor : orc::getDestructors(*M))
      DtorNames.push_back(mangle(Dtor.Func->getName()));

    // Add the module to the JIT.
    auto K = ES.allocateVModule();
    Resolvers[K] = std::make_shared<CBindingsResolver>(*this, ExternalResolver,
                                                       ExternalResolverCtx);
    if (auto Err = Layer.addModule(K, std::move(M)))
      return std::move(Err);

    KeyLayers[K] = detail::createGenericLayer(Layer);

    // Run the static constructors, and save the static destructor runner for
    // execution when the JIT is torn down.
    orc::LegacyCtorDtorRunner<OrcCBindingsStack> CtorRunner(std::move(CtorNames), K);
    if (auto Err = CtorRunner.runViaLayer(*this))
      return std::move(Err);

    IRStaticDestructorRunners.emplace_back(std::move(DtorNames), K);

    return K;
  }

  Expected<orc::VModuleKey>
  addIRModuleEager(std::unique_ptr<Module> M,
                   LLVMOrcSymbolResolverFn ExternalResolver,
                   void *ExternalResolverCtx) {
    return addIRModule(CompileLayer, std::move(M),
                       llvm::make_unique<SectionMemoryManager>(),
                       std::move(ExternalResolver), ExternalResolverCtx);
  }

  Expected<orc::VModuleKey>
  addIRModuleLazy(std::unique_ptr<Module> M,
                  LLVMOrcSymbolResolverFn ExternalResolver,
                  void *ExternalResolverCtx) {
    if (!CODLayer)
      return make_error<StringError>("Can not add lazy module: No compile "
                                     "callback manager available",
                                     inconvertibleErrorCode());

    return addIRModule(*CODLayer, std::move(M),
                       llvm::make_unique<SectionMemoryManager>(),
                       std::move(ExternalResolver), ExternalResolverCtx);
  }

  Error removeModule(orc::VModuleKey K) {
    // FIXME: Should error release the module key?
    if (auto Err = KeyLayers[K]->removeModule(K))
      return Err;
    ES.releaseVModule(K);
    KeyLayers.erase(K);
    return Error::success();
  }

  Expected<orc::VModuleKey> addObject(std::unique_ptr<MemoryBuffer> ObjBuffer,
                                      LLVMOrcSymbolResolverFn ExternalResolver,
                                      void *ExternalResolverCtx) {
    if (auto Obj = object::ObjectFile::createObjectFile(
            ObjBuffer->getMemBufferRef())) {

      auto K = ES.allocateVModule();
      Resolvers[K] = std::make_shared<CBindingsResolver>(
          *this, ExternalResolver, ExternalResolverCtx);

      if (auto Err = ObjectLayer.addObject(K, std::move(ObjBuffer)))
        return std::move(Err);

      KeyLayers[K] = detail::createGenericLayer(ObjectLayer);

      return K;
    } else
      return Obj.takeError();
  }

  JITSymbol findSymbol(const std::string &Name,
                                 bool ExportedSymbolsOnly) {
    if (auto Sym = IndirectStubsMgr->findStub(Name, ExportedSymbolsOnly))
      return Sym;
    if (CODLayer)
      return CODLayer->findSymbol(mangle(Name), ExportedSymbolsOnly);
    return CompileLayer.findSymbol(mangle(Name), ExportedSymbolsOnly);
  }

  JITSymbol findSymbolIn(orc::VModuleKey K, const std::string &Name,
                         bool ExportedSymbolsOnly) {
    assert(KeyLayers.count(K) && "looking up symbol in unknown module");
    return KeyLayers[K]->findSymbolIn(K, mangle(Name), ExportedSymbolsOnly);
  }

  Expected<JITTargetAddress> findSymbolAddress(const std::string &Name,
                                               bool ExportedSymbolsOnly) {
    if (auto Sym = findSymbol(Name, ExportedSymbolsOnly)) {
      // Successful lookup, non-null symbol:
      if (auto AddrOrErr = Sym.getAddress())
        return *AddrOrErr;
      else
        return AddrOrErr.takeError();
    } else if (auto Err = Sym.takeError()) {
      // Lookup failure - report error.
      return std::move(Err);
    }

    // No symbol not found. Return 0.
    return 0;
  }

  Expected<JITTargetAddress> findSymbolAddressIn(orc::VModuleKey K,
                                                 const std::string &Name,
                                                 bool ExportedSymbolsOnly) {
    if (auto Sym = findSymbolIn(K, Name, ExportedSymbolsOnly)) {
      // Successful lookup, non-null symbol:
      if (auto AddrOrErr = Sym.getAddress())
        return *AddrOrErr;
      else
        return AddrOrErr.takeError();
    } else if (auto Err = Sym.takeError()) {
      // Lookup failure - report error.
      return std::move(Err);
    }

    // Symbol not found. Return 0.
    return 0;
  }

  const std::string &getErrorMessage() const { return ErrMsg; }

  void RegisterJITEventListener(JITEventListener *L) {
    if (!L)
      return;
    EventListeners.push_back(L);
  }

  void UnregisterJITEventListener(JITEventListener *L) {
    if (!L)
      return;

    auto I = find(reverse(EventListeners), L);
    if (I != EventListeners.rend()) {
      std::swap(*I, EventListeners.back());
      EventListeners.pop_back();
    }
  }

private:
  using ResolverMap =
      std::map<orc::VModuleKey, std::shared_ptr<orc::SymbolResolver>>;

  static std::unique_ptr<CompileCallbackMgr>
  createCompileCallbackManager(TargetMachine &TM, orc::ExecutionSession &ES) {
    auto CCMgr = createLocalCompileCallbackManager(TM.getTargetTriple(), ES, 0);
    if (!CCMgr) {
      // FIXME: It would be good if we could report this somewhere, but we do
      //        have an instance yet.
      logAllUnhandledErrors(CCMgr.takeError(), errs(), "ORC error: ");
      return nullptr;
    }
    return std::move(*CCMgr);
  }

  static std::unique_ptr<CODLayerT>
  createCODLayer(orc::ExecutionSession &ES, CompileLayerT &CompileLayer,
                 CompileCallbackMgr *CCMgr,
                 IndirectStubsManagerBuilder IndirectStubsMgrBuilder,
                 ResolverMap &Resolvers) {
    // If there is no compile callback manager available we can not create a
    // compile on demand layer.
    if (!CCMgr)
      return nullptr;

    return llvm::make_unique<CODLayerT>(
        ES, CompileLayer,
        [&Resolvers](orc::VModuleKey K) {
          auto ResolverI = Resolvers.find(K);
          assert(ResolverI != Resolvers.end() && "No resolver for module K");
          return ResolverI->second;
        },
        [&Resolvers](orc::VModuleKey K,
                     std::shared_ptr<orc::SymbolResolver> Resolver) {
          assert(!Resolvers.count(K) && "Resolver already present");
          Resolvers[K] = std::move(Resolver);
        },
        [](Function &F) { return std::set<Function *>({&F}); }, *CCMgr,
        std::move(IndirectStubsMgrBuilder), false);
  }

  void reportError(Error Err) {
    // FIXME: Report errors on the execution session.
    logAllUnhandledErrors(std::move(Err), errs(), "ORC error: ");
  };

  void notifyFinalized(orc::VModuleKey K,
		       const object::ObjectFile &Obj,
		       const RuntimeDyld::LoadedObjectInfo &LoadedObjInfo) {
    uint64_t Key = static_cast<uint64_t>(
        reinterpret_cast<uintptr_t>(Obj.getData().data()));
    for (auto &Listener : EventListeners)
      Listener->notifyObjectLoaded(Key, Obj, LoadedObjInfo);
  }

  void notifyFreed(orc::VModuleKey K, const object::ObjectFile &Obj) {
    uint64_t Key = static_cast<uint64_t>(
        reinterpret_cast<uintptr_t>(Obj.getData().data()));
    for (auto &Listener : EventListeners)
      Listener->notifyFreeingObject(Key);
  }

  orc::ExecutionSession ES;
  std::unique_ptr<CompileCallbackMgr> CCMgr;

  std::vector<JITEventListener *> EventListeners;

  DataLayout DL;
  SectionMemoryManager CCMgrMemMgr;

  std::unique_ptr<orc::IndirectStubsManager> IndirectStubsMgr;

  ObjLayerT ObjectLayer;
  CompileLayerT CompileLayer;
  std::unique_ptr<CODLayerT> CODLayer;

  std::map<orc::VModuleKey, std::unique_ptr<detail::GenericLayer>> KeyLayers;

  orc::LegacyLocalCXXRuntimeOverrides CXXRuntimeOverrides;
  std::vector<orc::LegacyCtorDtorRunner<OrcCBindingsStack>> IRStaticDestructorRunners;
  std::string ErrMsg;

  ResolverMap Resolvers;
};

} // end namespace llvm

#endif // LLVM_LIB_EXECUTIONENGINE_ORC_ORCCBINDINGSSTACK_H
