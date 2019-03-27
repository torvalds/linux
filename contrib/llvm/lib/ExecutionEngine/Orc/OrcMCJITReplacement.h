//===- OrcMCJITReplacement.h - Orc based MCJIT replacement ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Orc based MCJIT replacement.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_EXECUTIONENGINE_ORC_ORCMCJITREPLACEMENT_H
#define LLVM_LIB_EXECUTIONENGINE_ORC_ORCMCJITREPLACEMENT_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/LazyEmittingLayer.h"
#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/RTDyldMemoryManager.h"
#include "llvm/ExecutionEngine/RuntimeDyld.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Module.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace llvm {

class ObjectCache;

namespace orc {

class OrcMCJITReplacement : public ExecutionEngine {

  // OrcMCJITReplacement needs to do a little extra book-keeping to ensure that
  // Orc's automatic finalization doesn't kick in earlier than MCJIT clients are
  // expecting - see finalizeMemory.
  class MCJITReplacementMemMgr : public MCJITMemoryManager {
  public:
    MCJITReplacementMemMgr(OrcMCJITReplacement &M,
                           std::shared_ptr<MCJITMemoryManager> ClientMM)
      : M(M), ClientMM(std::move(ClientMM)) {}

    uint8_t *allocateCodeSection(uintptr_t Size, unsigned Alignment,
                                 unsigned SectionID,
                                 StringRef SectionName) override {
      uint8_t *Addr =
          ClientMM->allocateCodeSection(Size, Alignment, SectionID,
                                        SectionName);
      M.SectionsAllocatedSinceLastLoad.insert(Addr);
      return Addr;
    }

    uint8_t *allocateDataSection(uintptr_t Size, unsigned Alignment,
                                 unsigned SectionID, StringRef SectionName,
                                 bool IsReadOnly) override {
      uint8_t *Addr = ClientMM->allocateDataSection(Size, Alignment, SectionID,
                                                    SectionName, IsReadOnly);
      M.SectionsAllocatedSinceLastLoad.insert(Addr);
      return Addr;
    }

    void reserveAllocationSpace(uintptr_t CodeSize, uint32_t CodeAlign,
                                uintptr_t RODataSize, uint32_t RODataAlign,
                                uintptr_t RWDataSize,
                                uint32_t RWDataAlign) override {
      return ClientMM->reserveAllocationSpace(CodeSize, CodeAlign,
                                              RODataSize, RODataAlign,
                                              RWDataSize, RWDataAlign);
    }

    bool needsToReserveAllocationSpace() override {
      return ClientMM->needsToReserveAllocationSpace();
    }

    void registerEHFrames(uint8_t *Addr, uint64_t LoadAddr,
                          size_t Size) override {
      return ClientMM->registerEHFrames(Addr, LoadAddr, Size);
    }

    void deregisterEHFrames() override {
      return ClientMM->deregisterEHFrames();
    }

    void notifyObjectLoaded(RuntimeDyld &RTDyld,
                            const object::ObjectFile &O) override {
      return ClientMM->notifyObjectLoaded(RTDyld, O);
    }

    void notifyObjectLoaded(ExecutionEngine *EE,
                            const object::ObjectFile &O) override {
      return ClientMM->notifyObjectLoaded(EE, O);
    }

    bool finalizeMemory(std::string *ErrMsg = nullptr) override {
      // Each set of objects loaded will be finalized exactly once, but since
      // symbol lookup during relocation may recursively trigger the
      // loading/relocation of other modules, and since we're forwarding all
      // finalizeMemory calls to a single underlying memory manager, we need to
      // defer forwarding the call on until all necessary objects have been
      // loaded. Otherwise, during the relocation of a leaf object, we will end
      // up finalizing memory, causing a crash further up the stack when we
      // attempt to apply relocations to finalized memory.
      // To avoid finalizing too early, look at how many objects have been
      // loaded but not yet finalized. This is a bit of a hack that relies on
      // the fact that we're lazily emitting object files: The only way you can
      // get more than one set of objects loaded but not yet finalized is if
      // they were loaded during relocation of another set.
      if (M.UnfinalizedSections.size() == 1)
        return ClientMM->finalizeMemory(ErrMsg);
      return false;
    }

  private:
    OrcMCJITReplacement &M;
    std::shared_ptr<MCJITMemoryManager> ClientMM;
  };

  class LinkingORCResolver : public orc::SymbolResolver {
  public:
    LinkingORCResolver(OrcMCJITReplacement &M) : M(M) {}

    SymbolNameSet getResponsibilitySet(const SymbolNameSet &Symbols) override {
      SymbolNameSet Result;

      for (auto &S : Symbols) {
        if (auto Sym = M.findMangledSymbol(*S)) {
          if (!Sym.getFlags().isStrong())
            Result.insert(S);
        } else if (auto Err = Sym.takeError()) {
          M.reportError(std::move(Err));
          return SymbolNameSet();
        } else {
          if (auto Sym2 = M.ClientResolver->findSymbolInLogicalDylib(*S)) {
            if (!Sym2.getFlags().isStrong())
              Result.insert(S);
          } else if (auto Err = Sym2.takeError()) {
            M.reportError(std::move(Err));
            return SymbolNameSet();
          } else
            Result.insert(S);
        }
      }

      return Result;
    }

    SymbolNameSet lookup(std::shared_ptr<AsynchronousSymbolQuery> Query,
                         SymbolNameSet Symbols) override {
      SymbolNameSet UnresolvedSymbols;
      bool NewSymbolsResolved = false;

      for (auto &S : Symbols) {
        if (auto Sym = M.findMangledSymbol(*S)) {
          if (auto Addr = Sym.getAddress()) {
            Query->resolve(S, JITEvaluatedSymbol(*Addr, Sym.getFlags()));
            Query->notifySymbolReady();
            NewSymbolsResolved = true;
          } else {
            M.ES.legacyFailQuery(*Query, Addr.takeError());
            return SymbolNameSet();
          }
        } else if (auto Err = Sym.takeError()) {
          M.ES.legacyFailQuery(*Query, std::move(Err));
          return SymbolNameSet();
        } else {
          if (auto Sym2 = M.ClientResolver->findSymbol(*S)) {
            if (auto Addr = Sym2.getAddress()) {
              Query->resolve(S, JITEvaluatedSymbol(*Addr, Sym2.getFlags()));
              Query->notifySymbolReady();
              NewSymbolsResolved = true;
            } else {
              M.ES.legacyFailQuery(*Query, Addr.takeError());
              return SymbolNameSet();
            }
          } else if (auto Err = Sym2.takeError()) {
            M.ES.legacyFailQuery(*Query, std::move(Err));
            return SymbolNameSet();
          } else
            UnresolvedSymbols.insert(S);
        }
      }

      if (NewSymbolsResolved && Query->isFullyResolved())
        Query->handleFullyResolved();

      if (NewSymbolsResolved && Query->isFullyReady())
        Query->handleFullyReady();

      return UnresolvedSymbols;
    }

  private:
    OrcMCJITReplacement &M;
  };

private:
  static ExecutionEngine *
  createOrcMCJITReplacement(std::string *ErrorMsg,
                            std::shared_ptr<MCJITMemoryManager> MemMgr,
                            std::shared_ptr<LegacyJITSymbolResolver> Resolver,
                            std::unique_ptr<TargetMachine> TM) {
    return new OrcMCJITReplacement(std::move(MemMgr), std::move(Resolver),
                                   std::move(TM));
  }

  void reportError(Error Err) {
    logAllUnhandledErrors(std::move(Err), errs(), "MCJIT error: ");
  }

public:
  OrcMCJITReplacement(std::shared_ptr<MCJITMemoryManager> MemMgr,
                      std::shared_ptr<LegacyJITSymbolResolver> ClientResolver,
                      std::unique_ptr<TargetMachine> TM)
      : ExecutionEngine(TM->createDataLayout()),
        TM(std::move(TM)),
        MemMgr(
            std::make_shared<MCJITReplacementMemMgr>(*this, std::move(MemMgr))),
        Resolver(std::make_shared<LinkingORCResolver>(*this)),
        ClientResolver(std::move(ClientResolver)), NotifyObjectLoaded(*this),
        NotifyFinalized(*this),
        ObjectLayer(
            ES,
            [this](VModuleKey K) {
              return ObjectLayerT::Resources{this->MemMgr, this->Resolver};
            },
            NotifyObjectLoaded, NotifyFinalized),
        CompileLayer(ObjectLayer, SimpleCompiler(*this->TM),
                     [this](VModuleKey K, std::unique_ptr<Module> M) {
                       Modules.push_back(std::move(M));
                     }),
        LazyEmitLayer(CompileLayer) {}

  static void Register() {
    OrcMCJITReplacementCtor = createOrcMCJITReplacement;
  }

  void addModule(std::unique_ptr<Module> M) override {
    // If this module doesn't have a DataLayout attached then attach the
    // default.
    if (M->getDataLayout().isDefault()) {
      M->setDataLayout(getDataLayout());
    } else {
      assert(M->getDataLayout() == getDataLayout() && "DataLayout Mismatch");
    }

    // Rename, bump linkage and record static constructors and destructors.
    // We have to do this before we hand over ownership of the module to the
    // JIT.
    std::vector<std::string> CtorNames, DtorNames;
    {
      unsigned CtorId = 0, DtorId = 0;
      for (auto Ctor : orc::getConstructors(*M)) {
        std::string NewCtorName = ("__ORCstatic_ctor." + Twine(CtorId++)).str();
        Ctor.Func->setName(NewCtorName);
        Ctor.Func->setLinkage(GlobalValue::ExternalLinkage);
        Ctor.Func->setVisibility(GlobalValue::HiddenVisibility);
        CtorNames.push_back(mangle(NewCtorName));
      }
      for (auto Dtor : orc::getDestructors(*M)) {
        std::string NewDtorName = ("__ORCstatic_dtor." + Twine(DtorId++)).str();
        dbgs() << "Found dtor: " << NewDtorName << "\n";
        Dtor.Func->setName(NewDtorName);
        Dtor.Func->setLinkage(GlobalValue::ExternalLinkage);
        Dtor.Func->setVisibility(GlobalValue::HiddenVisibility);
        DtorNames.push_back(mangle(NewDtorName));
      }
    }

    auto K = ES.allocateVModule();

    UnexecutedConstructors[K] = std::move(CtorNames);
    UnexecutedDestructors[K] = std::move(DtorNames);

    cantFail(LazyEmitLayer.addModule(K, std::move(M)));
  }

  void addObjectFile(std::unique_ptr<object::ObjectFile> O) override {
    cantFail(ObjectLayer.addObject(
        ES.allocateVModule(), MemoryBuffer::getMemBufferCopy(O->getData())));
  }

  void addObjectFile(object::OwningBinary<object::ObjectFile> O) override {
    std::unique_ptr<object::ObjectFile> Obj;
    std::unique_ptr<MemoryBuffer> ObjBuffer;
    std::tie(Obj, ObjBuffer) = O.takeBinary();
    cantFail(ObjectLayer.addObject(ES.allocateVModule(), std::move(ObjBuffer)));
  }

  void addArchive(object::OwningBinary<object::Archive> A) override {
    Archives.push_back(std::move(A));
  }

  bool removeModule(Module *M) override {
    auto I = Modules.begin();
    for (auto E = Modules.end(); I != E; ++I)
      if (I->get() == M)
        break;
    if (I == Modules.end())
      return false;
    Modules.erase(I);
    return true;
  }

  uint64_t getSymbolAddress(StringRef Name) {
    return cantFail(findSymbol(Name).getAddress());
  }

  JITSymbol findSymbol(StringRef Name) {
    return findMangledSymbol(mangle(Name));
  }

  void finalizeObject() override {
    // This is deprecated - Aim to remove in ExecutionEngine.
    // REMOVE IF POSSIBLE - Doesn't make sense for New JIT.
  }

  void mapSectionAddress(const void *LocalAddress,
                         uint64_t TargetAddress) override {
    for (auto &P : UnfinalizedSections)
      if (P.second.count(LocalAddress))
        ObjectLayer.mapSectionAddress(P.first, LocalAddress, TargetAddress);
  }

  uint64_t getGlobalValueAddress(const std::string &Name) override {
    return getSymbolAddress(Name);
  }

  uint64_t getFunctionAddress(const std::string &Name) override {
    return getSymbolAddress(Name);
  }

  void *getPointerToFunction(Function *F) override {
    uint64_t FAddr = getSymbolAddress(F->getName());
    return reinterpret_cast<void *>(static_cast<uintptr_t>(FAddr));
  }

  void *getPointerToNamedFunction(StringRef Name,
                                  bool AbortOnFailure = true) override {
    uint64_t Addr = getSymbolAddress(Name);
    if (!Addr && AbortOnFailure)
      llvm_unreachable("Missing symbol!");
    return reinterpret_cast<void *>(static_cast<uintptr_t>(Addr));
  }

  GenericValue runFunction(Function *F,
                           ArrayRef<GenericValue> ArgValues) override;

  void setObjectCache(ObjectCache *NewCache) override {
    CompileLayer.getCompiler().setObjectCache(NewCache);
  }

  void setProcessAllSections(bool ProcessAllSections) override {
    ObjectLayer.setProcessAllSections(ProcessAllSections);
  }

  void runStaticConstructorsDestructors(bool isDtors) override;

private:
  JITSymbol findMangledSymbol(StringRef Name) {
    if (auto Sym = LazyEmitLayer.findSymbol(Name, false))
      return Sym;
    if (auto Sym = ClientResolver->findSymbol(Name))
      return Sym;
    if (auto Sym = scanArchives(Name))
      return Sym;

    return nullptr;
  }

  JITSymbol scanArchives(StringRef Name) {
    for (object::OwningBinary<object::Archive> &OB : Archives) {
      object::Archive *A = OB.getBinary();
      // Look for our symbols in each Archive
      auto OptionalChildOrErr = A->findSym(Name);
      if (!OptionalChildOrErr)
        report_fatal_error(OptionalChildOrErr.takeError());
      auto &OptionalChild = *OptionalChildOrErr;
      if (OptionalChild) {
        // FIXME: Support nested archives?
        Expected<std::unique_ptr<object::Binary>> ChildBinOrErr =
            OptionalChild->getAsBinary();
        if (!ChildBinOrErr) {
          // TODO: Actually report errors helpfully.
          consumeError(ChildBinOrErr.takeError());
          continue;
        }
        std::unique_ptr<object::Binary> &ChildBin = ChildBinOrErr.get();
        if (ChildBin->isObject()) {
          cantFail(ObjectLayer.addObject(
              ES.allocateVModule(),
              MemoryBuffer::getMemBufferCopy(ChildBin->getData())));
          if (auto Sym = ObjectLayer.findSymbol(Name, true))
            return Sym;
        }
      }
    }
    return nullptr;
  }

  class NotifyObjectLoadedT {
  public:
    using LoadedObjInfoListT =
        std::vector<std::unique_ptr<RuntimeDyld::LoadedObjectInfo>>;

    NotifyObjectLoadedT(OrcMCJITReplacement &M) : M(M) {}

    void operator()(VModuleKey K, const object::ObjectFile &Obj,
                    const RuntimeDyld::LoadedObjectInfo &Info) const {
      M.UnfinalizedSections[K] = std::move(M.SectionsAllocatedSinceLastLoad);
      M.SectionsAllocatedSinceLastLoad = SectionAddrSet();
      M.MemMgr->notifyObjectLoaded(&M, Obj);
    }
  private:
    OrcMCJITReplacement &M;
  };

  class NotifyFinalizedT {
  public:
    NotifyFinalizedT(OrcMCJITReplacement &M) : M(M) {}

    void operator()(VModuleKey K, const object::ObjectFile &Obj,
                    const RuntimeDyld::LoadedObjectInfo &Info) {
      M.UnfinalizedSections.erase(K);
    }

  private:
    OrcMCJITReplacement &M;
  };

  std::string mangle(StringRef Name) {
    std::string MangledName;
    {
      raw_string_ostream MangledNameStream(MangledName);
      Mang.getNameWithPrefix(MangledNameStream, Name, getDataLayout());
    }
    return MangledName;
  }

  using ObjectLayerT = LegacyRTDyldObjectLinkingLayer;
  using CompileLayerT = LegacyIRCompileLayer<ObjectLayerT, orc::SimpleCompiler>;
  using LazyEmitLayerT = LazyEmittingLayer<CompileLayerT>;

  ExecutionSession ES;

  std::unique_ptr<TargetMachine> TM;
  std::shared_ptr<MCJITReplacementMemMgr> MemMgr;
  std::shared_ptr<LinkingORCResolver> Resolver;
  std::shared_ptr<LegacyJITSymbolResolver> ClientResolver;
  Mangler Mang;

  // IMPORTANT: ShouldDelete *must* come before LocalModules: The shared_ptr
  // delete blocks in LocalModules refer to the ShouldDelete map, so
  // LocalModules needs to be destructed before ShouldDelete.
  std::map<Module*, bool> ShouldDelete;

  NotifyObjectLoadedT NotifyObjectLoaded;
  NotifyFinalizedT NotifyFinalized;

  ObjectLayerT ObjectLayer;
  CompileLayerT CompileLayer;
  LazyEmitLayerT LazyEmitLayer;

  std::map<VModuleKey, std::vector<std::string>> UnexecutedConstructors;
  std::map<VModuleKey, std::vector<std::string>> UnexecutedDestructors;

  // We need to store ObjLayerT::ObjSetHandles for each of the object sets
  // that have been emitted but not yet finalized so that we can forward the
  // mapSectionAddress calls appropriately.
  using SectionAddrSet = std::set<const void *>;
  SectionAddrSet SectionsAllocatedSinceLastLoad;
  std::map<VModuleKey, SectionAddrSet> UnfinalizedSections;

  std::vector<object::OwningBinary<object::Archive>> Archives;
};

} // end namespace orc

} // end namespace llvm

#endif // LLVM_LIB_EXECUTIONENGINE_ORC_MCJITREPLACEMENT_H
