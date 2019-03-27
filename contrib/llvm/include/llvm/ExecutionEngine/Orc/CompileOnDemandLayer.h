//===- CompileOnDemandLayer.h - Compile each function on demand -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// JIT layer for breaking up modules and inserting callbacks to allow
// individual functions to be compiled on demand.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_COMPILEONDEMANDLAYER_H
#define LLVM_EXECUTIONENGINE_ORC_COMPILEONDEMANDLAYER_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/IndirectionUtils.h"
#include "llvm/ExecutionEngine/Orc/LambdaResolver.h"
#include "llvm/ExecutionEngine/Orc/Layer.h"
#include "llvm/ExecutionEngine/Orc/LazyReexports.h"
#include "llvm/ExecutionEngine/Orc/Legacy.h"
#include "llvm/ExecutionEngine/Orc/OrcError.h"
#include "llvm/ExecutionEngine/RuntimeDyld.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <algorithm>
#include <cassert>
#include <functional>
#include <iterator>
#include <list>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

class Value;

namespace orc {

class ExtractingIRMaterializationUnit;

class CompileOnDemandLayer : public IRLayer {
  friend class PartitioningIRMaterializationUnit;

public:
  /// Builder for IndirectStubsManagers.
  using IndirectStubsManagerBuilder =
      std::function<std::unique_ptr<IndirectStubsManager>()>;

  using GlobalValueSet = std::set<const GlobalValue *>;

  /// Partitioning function.
  using PartitionFunction =
      std::function<Optional<GlobalValueSet>(GlobalValueSet Requested)>;

  /// Off-the-shelf partitioning which compiles all requested symbols (usually
  /// a single function at a time).
  static Optional<GlobalValueSet> compileRequested(GlobalValueSet Requested);

  /// Off-the-shelf partitioning which compiles whole modules whenever any
  /// symbol in them is requested.
  static Optional<GlobalValueSet> compileWholeModule(GlobalValueSet Requested);

  /// Construct a CompileOnDemandLayer.
  CompileOnDemandLayer(ExecutionSession &ES, IRLayer &BaseLayer,
                        LazyCallThroughManager &LCTMgr,
                        IndirectStubsManagerBuilder BuildIndirectStubsManager);

  /// Sets the partition function.
  void setPartitionFunction(PartitionFunction Partition);

  /// Emits the given module. This should not be called by clients: it will be
  /// called by the JIT when a definition added via the add method is requested.
  void emit(MaterializationResponsibility R, ThreadSafeModule TSM) override;

private:
  struct PerDylibResources {
  public:
    PerDylibResources(JITDylib &ImplD,
                      std::unique_ptr<IndirectStubsManager> ISMgr)
        : ImplD(ImplD), ISMgr(std::move(ISMgr)) {}
    JITDylib &getImplDylib() { return ImplD; }
    IndirectStubsManager &getISManager() { return *ISMgr; }

  private:
    JITDylib &ImplD;
    std::unique_ptr<IndirectStubsManager> ISMgr;
  };

  using PerDylibResourcesMap = std::map<const JITDylib *, PerDylibResources>;

  PerDylibResources &getPerDylibResources(JITDylib &TargetD);

  void cleanUpModule(Module &M);

  void expandPartition(GlobalValueSet &Partition);

  void emitPartition(MaterializationResponsibility R, ThreadSafeModule TSM,
                     IRMaterializationUnit::SymbolNameToDefinitionMap Defs);

  mutable std::mutex CODLayerMutex;

  IRLayer &BaseLayer;
  LazyCallThroughManager &LCTMgr;
  IndirectStubsManagerBuilder BuildIndirectStubsManager;
  PerDylibResourcesMap DylibResources;
  PartitionFunction Partition = compileRequested;
  SymbolLinkagePromoter PromoteSymbols;
};

/// Compile-on-demand layer.
///
///   When a module is added to this layer a stub is created for each of its
/// function definitions. The stubs and other global values are immediately
/// added to the layer below. When a stub is called it triggers the extraction
/// of the function body from the original module. The extracted body is then
/// compiled and executed.
template <typename BaseLayerT,
          typename CompileCallbackMgrT = JITCompileCallbackManager,
          typename IndirectStubsMgrT = IndirectStubsManager>
class LegacyCompileOnDemandLayer {
private:
  template <typename MaterializerFtor>
  class LambdaMaterializer final : public ValueMaterializer {
  public:
    LambdaMaterializer(MaterializerFtor M) : M(std::move(M)) {}

    Value *materialize(Value *V) final { return M(V); }

  private:
    MaterializerFtor M;
  };

  template <typename MaterializerFtor>
  LambdaMaterializer<MaterializerFtor>
  createLambdaMaterializer(MaterializerFtor M) {
    return LambdaMaterializer<MaterializerFtor>(std::move(M));
  }

  // Provide type-erasure for the Modules and MemoryManagers.
  template <typename ResourceT>
  class ResourceOwner {
  public:
    ResourceOwner() = default;
    ResourceOwner(const ResourceOwner &) = delete;
    ResourceOwner &operator=(const ResourceOwner &) = delete;
    virtual ~ResourceOwner() = default;

    virtual ResourceT& getResource() const = 0;
  };

  template <typename ResourceT, typename ResourcePtrT>
  class ResourceOwnerImpl : public ResourceOwner<ResourceT> {
  public:
    ResourceOwnerImpl(ResourcePtrT ResourcePtr)
      : ResourcePtr(std::move(ResourcePtr)) {}

    ResourceT& getResource() const override { return *ResourcePtr; }

  private:
    ResourcePtrT ResourcePtr;
  };

  template <typename ResourceT, typename ResourcePtrT>
  std::unique_ptr<ResourceOwner<ResourceT>>
  wrapOwnership(ResourcePtrT ResourcePtr) {
    using RO = ResourceOwnerImpl<ResourceT, ResourcePtrT>;
    return llvm::make_unique<RO>(std::move(ResourcePtr));
  }

  struct LogicalDylib {
    struct SourceModuleEntry {
      std::unique_ptr<Module> SourceMod;
      std::set<Function*> StubsToClone;
    };

    using SourceModulesList = std::vector<SourceModuleEntry>;
    using SourceModuleHandle = typename SourceModulesList::size_type;

    LogicalDylib() = default;

    LogicalDylib(VModuleKey K, std::shared_ptr<SymbolResolver> BackingResolver,
                 std::unique_ptr<IndirectStubsMgrT> StubsMgr)
        : K(std::move(K)), BackingResolver(std::move(BackingResolver)),
          StubsMgr(std::move(StubsMgr)) {}

    SourceModuleHandle addSourceModule(std::unique_ptr<Module> M) {
      SourceModuleHandle H = SourceModules.size();
      SourceModules.push_back(SourceModuleEntry());
      SourceModules.back().SourceMod = std::move(M);
      return H;
    }

    Module& getSourceModule(SourceModuleHandle H) {
      return *SourceModules[H].SourceMod;
    }

    std::set<Function*>& getStubsToClone(SourceModuleHandle H) {
      return SourceModules[H].StubsToClone;
    }

    JITSymbol findSymbol(BaseLayerT &BaseLayer, const std::string &Name,
                         bool ExportedSymbolsOnly) {
      if (auto Sym = StubsMgr->findStub(Name, ExportedSymbolsOnly))
        return Sym;
      for (auto BLK : BaseLayerVModuleKeys)
        if (auto Sym = BaseLayer.findSymbolIn(BLK, Name, ExportedSymbolsOnly))
          return Sym;
        else if (auto Err = Sym.takeError())
          return std::move(Err);
      return nullptr;
    }

    Error removeModulesFromBaseLayer(BaseLayerT &BaseLayer) {
      for (auto &BLK : BaseLayerVModuleKeys)
        if (auto Err = BaseLayer.removeModule(BLK))
          return Err;
      return Error::success();
    }

    VModuleKey K;
    std::shared_ptr<SymbolResolver> BackingResolver;
    std::unique_ptr<IndirectStubsMgrT> StubsMgr;
    SymbolLinkagePromoter PromoteSymbols;
    SourceModulesList SourceModules;
    std::vector<VModuleKey> BaseLayerVModuleKeys;
  };

public:

  /// Module partitioning functor.
  using PartitioningFtor = std::function<std::set<Function*>(Function&)>;

  /// Builder for IndirectStubsManagers.
  using IndirectStubsManagerBuilderT =
      std::function<std::unique_ptr<IndirectStubsMgrT>()>;

  using SymbolResolverGetter =
      std::function<std::shared_ptr<SymbolResolver>(VModuleKey K)>;

  using SymbolResolverSetter =
      std::function<void(VModuleKey K, std::shared_ptr<SymbolResolver> R)>;

  /// Construct a compile-on-demand layer instance.
  LegacyCompileOnDemandLayer(ExecutionSession &ES, BaseLayerT &BaseLayer,
                             SymbolResolverGetter GetSymbolResolver,
                             SymbolResolverSetter SetSymbolResolver,
                             PartitioningFtor Partition,
                             CompileCallbackMgrT &CallbackMgr,
                             IndirectStubsManagerBuilderT CreateIndirectStubsManager,
                             bool CloneStubsIntoPartitions = true)
      : ES(ES), BaseLayer(BaseLayer),
        GetSymbolResolver(std::move(GetSymbolResolver)),
        SetSymbolResolver(std::move(SetSymbolResolver)),
        Partition(std::move(Partition)), CompileCallbackMgr(CallbackMgr),
        CreateIndirectStubsManager(std::move(CreateIndirectStubsManager)),
        CloneStubsIntoPartitions(CloneStubsIntoPartitions) {}

  ~LegacyCompileOnDemandLayer() {
    // FIXME: Report error on log.
    while (!LogicalDylibs.empty())
      consumeError(removeModule(LogicalDylibs.begin()->first));
  }

  /// Add a module to the compile-on-demand layer.
  Error addModule(VModuleKey K, std::unique_ptr<Module> M) {

    assert(!LogicalDylibs.count(K) && "VModuleKey K already in use");
    auto I = LogicalDylibs.insert(
        LogicalDylibs.end(),
        std::make_pair(K, LogicalDylib(K, GetSymbolResolver(K),
                                       CreateIndirectStubsManager())));

    return addLogicalModule(I->second, std::move(M));
  }

  /// Add extra modules to an existing logical module.
  Error addExtraModule(VModuleKey K, std::unique_ptr<Module> M) {
    return addLogicalModule(LogicalDylibs[K], std::move(M));
  }

  /// Remove the module represented by the given key.
  ///
  ///   This will remove all modules in the layers below that were derived from
  /// the module represented by K.
  Error removeModule(VModuleKey K) {
    auto I = LogicalDylibs.find(K);
    assert(I != LogicalDylibs.end() && "VModuleKey K not valid here");
    auto Err = I->second.removeModulesFromBaseLayer(BaseLayer);
    LogicalDylibs.erase(I);
    return Err;
  }

  /// Search for the given named symbol.
  /// @param Name The name of the symbol to search for.
  /// @param ExportedSymbolsOnly If true, search only for exported symbols.
  /// @return A handle for the given named symbol, if it exists.
  JITSymbol findSymbol(StringRef Name, bool ExportedSymbolsOnly) {
    for (auto &KV : LogicalDylibs) {
      if (auto Sym = KV.second.StubsMgr->findStub(Name, ExportedSymbolsOnly))
        return Sym;
      if (auto Sym = findSymbolIn(KV.first, Name, ExportedSymbolsOnly))
        return Sym;
      else if (auto Err = Sym.takeError())
        return std::move(Err);
    }
    return BaseLayer.findSymbol(Name, ExportedSymbolsOnly);
  }

  /// Get the address of a symbol provided by this layer, or some layer
  ///        below this one.
  JITSymbol findSymbolIn(VModuleKey K, const std::string &Name,
                         bool ExportedSymbolsOnly) {
    assert(LogicalDylibs.count(K) && "VModuleKey K is not valid here");
    return LogicalDylibs[K].findSymbol(BaseLayer, Name, ExportedSymbolsOnly);
  }

  /// Update the stub for the given function to point at FnBodyAddr.
  /// This can be used to support re-optimization.
  /// @return true if the function exists and the stub is updated, false
  ///         otherwise.
  //
  // FIXME: We should track and free associated resources (unused compile
  //        callbacks, uncompiled IR, and no-longer-needed/reachable function
  //        implementations).
  Error updatePointer(std::string FuncName, JITTargetAddress FnBodyAddr) {
    //Find out which logical dylib contains our symbol
    auto LDI = LogicalDylibs.begin();
    for (auto LDE = LogicalDylibs.end(); LDI != LDE; ++LDI) {
      if (auto LMResources =
            LDI->getLogicalModuleResourcesForSymbol(FuncName, false)) {
        Module &SrcM = LMResources->SourceModule->getResource();
        std::string CalledFnName = mangle(FuncName, SrcM.getDataLayout());
        if (auto Err = LMResources->StubsMgr->updatePointer(CalledFnName,
                                                            FnBodyAddr))
          return Err;
        return Error::success();
      }
    }
    return make_error<JITSymbolNotFound>(FuncName);
  }

private:
  Error addLogicalModule(LogicalDylib &LD, std::unique_ptr<Module> SrcMPtr) {

    // Rename anonymous globals and promote linkage to ensure that everything
    // will resolve properly after we partition SrcM.
    LD.PromoteSymbols(*SrcMPtr);

    // Create a logical module handle for SrcM within the logical dylib.
    Module &SrcM = *SrcMPtr;
    auto LMId = LD.addSourceModule(std::move(SrcMPtr));

    // Create stub functions.
    const DataLayout &DL = SrcM.getDataLayout();
    {
      typename IndirectStubsMgrT::StubInitsMap StubInits;
      for (auto &F : SrcM) {
        // Skip declarations.
        if (F.isDeclaration())
          continue;

        // Skip weak functions for which we already have definitions.
        auto MangledName = mangle(F.getName(), DL);
        if (F.hasWeakLinkage() || F.hasLinkOnceLinkage()) {
          if (auto Sym = LD.findSymbol(BaseLayer, MangledName, false))
            continue;
          else if (auto Err = Sym.takeError())
            return std::move(Err);
        }

        // Record all functions defined by this module.
        if (CloneStubsIntoPartitions)
          LD.getStubsToClone(LMId).insert(&F);

        // Create a callback, associate it with the stub for the function,
        // and set the compile action to compile the partition containing the
        // function.
        auto CompileAction = [this, &LD, LMId, &F]() -> JITTargetAddress {
          if (auto FnImplAddrOrErr = this->extractAndCompile(LD, LMId, F))
            return *FnImplAddrOrErr;
          else {
            // FIXME: Report error, return to 'abort' or something similar.
            consumeError(FnImplAddrOrErr.takeError());
            return 0;
          }
        };
        if (auto CCAddr =
                CompileCallbackMgr.getCompileCallback(std::move(CompileAction)))
          StubInits[MangledName] =
              std::make_pair(*CCAddr, JITSymbolFlags::fromGlobalValue(F));
        else
          return CCAddr.takeError();
      }

      if (auto Err = LD.StubsMgr->createStubs(StubInits))
        return Err;
    }

    // If this module doesn't contain any globals, aliases, or module flags then
    // we can bail out early and avoid the overhead of creating and managing an
    // empty globals module.
    if (SrcM.global_empty() && SrcM.alias_empty() &&
        !SrcM.getModuleFlagsMetadata())
      return Error::success();

    // Create the GlobalValues module.
    auto GVsM = llvm::make_unique<Module>((SrcM.getName() + ".globals").str(),
                                          SrcM.getContext());
    GVsM->setDataLayout(DL);

    ValueToValueMapTy VMap;

    // Clone global variable decls.
    for (auto &GV : SrcM.globals())
      if (!GV.isDeclaration() && !VMap.count(&GV))
        cloneGlobalVariableDecl(*GVsM, GV, &VMap);

    // And the aliases.
    for (auto &A : SrcM.aliases())
      if (!VMap.count(&A))
        cloneGlobalAliasDecl(*GVsM, A, VMap);

    // Clone the module flags.
    cloneModuleFlagsMetadata(*GVsM, SrcM, VMap);

    // Now we need to clone the GV and alias initializers.

    // Initializers may refer to functions declared (but not defined) in this
    // module. Build a materializer to clone decls on demand.
    auto Materializer = createLambdaMaterializer(
      [&LD, &GVsM](Value *V) -> Value* {
        if (auto *F = dyn_cast<Function>(V)) {
          // Decls in the original module just get cloned.
          if (F->isDeclaration())
            return cloneFunctionDecl(*GVsM, *F);

          // Definitions in the original module (which we have emitted stubs
          // for at this point) get turned into a constant alias to the stub
          // instead.
          const DataLayout &DL = GVsM->getDataLayout();
          std::string FName = mangle(F->getName(), DL);
          unsigned PtrBitWidth = DL.getPointerTypeSizeInBits(F->getType());
          JITTargetAddress StubAddr =
            LD.StubsMgr->findStub(FName, false).getAddress();

          ConstantInt *StubAddrCI =
            ConstantInt::get(GVsM->getContext(), APInt(PtrBitWidth, StubAddr));
          Constant *Init = ConstantExpr::getCast(Instruction::IntToPtr,
                                                 StubAddrCI, F->getType());
          return GlobalAlias::create(F->getFunctionType(),
                                     F->getType()->getAddressSpace(),
                                     F->getLinkage(), F->getName(),
                                     Init, GVsM.get());
        }
        // else....
        return nullptr;
      });

    // Clone the global variable initializers.
    for (auto &GV : SrcM.globals())
      if (!GV.isDeclaration())
        moveGlobalVariableInitializer(GV, VMap, &Materializer);

    // Clone the global alias initializers.
    for (auto &A : SrcM.aliases()) {
      auto *NewA = cast<GlobalAlias>(VMap[&A]);
      assert(NewA && "Alias not cloned?");
      Value *Init = MapValue(A.getAliasee(), VMap, RF_None, nullptr,
                             &Materializer);
      NewA->setAliasee(cast<Constant>(Init));
    }

    // Build a resolver for the globals module and add it to the base layer.
    auto LegacyLookup = [this, &LD](const std::string &Name) -> JITSymbol {
      if (auto Sym = LD.StubsMgr->findStub(Name, false))
        return Sym;

      if (auto Sym = LD.findSymbol(BaseLayer, Name, false))
        return Sym;
      else if (auto Err = Sym.takeError())
        return std::move(Err);

      return nullptr;
    };

    auto GVsResolver = createSymbolResolver(
        [&LD, LegacyLookup](const SymbolNameSet &Symbols) {
          auto RS = getResponsibilitySetWithLegacyFn(Symbols, LegacyLookup);

          if (!RS) {
            logAllUnhandledErrors(
                RS.takeError(), errs(),
                "CODLayer/GVsResolver responsibility set lookup failed: ");
            return SymbolNameSet();
          }

          if (RS->size() == Symbols.size())
            return *RS;

          SymbolNameSet NotFoundViaLegacyLookup;
          for (auto &S : Symbols)
            if (!RS->count(S))
              NotFoundViaLegacyLookup.insert(S);
          auto RS2 =
              LD.BackingResolver->getResponsibilitySet(NotFoundViaLegacyLookup);

          for (auto &S : RS2)
            (*RS).insert(S);

          return *RS;
        },
        [this, &LD,
         LegacyLookup](std::shared_ptr<AsynchronousSymbolQuery> Query,
                       SymbolNameSet Symbols) {
          auto NotFoundViaLegacyLookup =
              lookupWithLegacyFn(ES, *Query, Symbols, LegacyLookup);
          return LD.BackingResolver->lookup(Query, NotFoundViaLegacyLookup);
        });

    SetSymbolResolver(LD.K, std::move(GVsResolver));

    if (auto Err = BaseLayer.addModule(LD.K, std::move(GVsM)))
      return Err;

    LD.BaseLayerVModuleKeys.push_back(LD.K);

    return Error::success();
  }

  static std::string mangle(StringRef Name, const DataLayout &DL) {
    std::string MangledName;
    {
      raw_string_ostream MangledNameStream(MangledName);
      Mangler::getNameWithPrefix(MangledNameStream, Name, DL);
    }
    return MangledName;
  }

  Expected<JITTargetAddress>
  extractAndCompile(LogicalDylib &LD,
                    typename LogicalDylib::SourceModuleHandle LMId,
                    Function &F) {
    Module &SrcM = LD.getSourceModule(LMId);

    // If F is a declaration we must already have compiled it.
    if (F.isDeclaration())
      return 0;

    // Grab the name of the function being called here.
    std::string CalledFnName = mangle(F.getName(), SrcM.getDataLayout());

    JITTargetAddress CalledAddr = 0;
    auto Part = Partition(F);
    if (auto PartKeyOrErr = emitPartition(LD, LMId, Part)) {
      auto &PartKey = *PartKeyOrErr;
      for (auto *SubF : Part) {
        std::string FnName = mangle(SubF->getName(), SrcM.getDataLayout());
        if (auto FnBodySym = BaseLayer.findSymbolIn(PartKey, FnName, false)) {
          if (auto FnBodyAddrOrErr = FnBodySym.getAddress()) {
            JITTargetAddress FnBodyAddr = *FnBodyAddrOrErr;

            // If this is the function we're calling record the address so we can
            // return it from this function.
            if (SubF == &F)
              CalledAddr = FnBodyAddr;

            // Update the function body pointer for the stub.
            if (auto EC = LD.StubsMgr->updatePointer(FnName, FnBodyAddr))
              return 0;

          } else
            return FnBodyAddrOrErr.takeError();
        } else if (auto Err = FnBodySym.takeError())
          return std::move(Err);
        else
          llvm_unreachable("Function not emitted for partition");
      }

      LD.BaseLayerVModuleKeys.push_back(PartKey);
    } else
      return PartKeyOrErr.takeError();

    return CalledAddr;
  }

  template <typename PartitionT>
  Expected<VModuleKey>
  emitPartition(LogicalDylib &LD,
                typename LogicalDylib::SourceModuleHandle LMId,
                const PartitionT &Part) {
    Module &SrcM = LD.getSourceModule(LMId);

    // Create the module.
    std::string NewName = SrcM.getName();
    for (auto *F : Part) {
      NewName += ".";
      NewName += F->getName();
    }

    auto M = llvm::make_unique<Module>(NewName, SrcM.getContext());
    M->setDataLayout(SrcM.getDataLayout());
    ValueToValueMapTy VMap;

    auto Materializer = createLambdaMaterializer([&LD, &LMId,
                                                  &M](Value *V) -> Value * {
      if (auto *GV = dyn_cast<GlobalVariable>(V))
        return cloneGlobalVariableDecl(*M, *GV);

      if (auto *F = dyn_cast<Function>(V)) {
        // Check whether we want to clone an available_externally definition.
        if (!LD.getStubsToClone(LMId).count(F))
          return cloneFunctionDecl(*M, *F);

        // Ok - we want an inlinable stub. For that to work we need a decl
        // for the stub pointer.
        auto *StubPtr = createImplPointer(*F->getType(), *M,
                                          F->getName() + "$stub_ptr", nullptr);
        auto *ClonedF = cloneFunctionDecl(*M, *F);
        makeStub(*ClonedF, *StubPtr);
        ClonedF->setLinkage(GlobalValue::AvailableExternallyLinkage);
        ClonedF->addFnAttr(Attribute::AlwaysInline);
        return ClonedF;
      }

      if (auto *A = dyn_cast<GlobalAlias>(V)) {
        auto *Ty = A->getValueType();
        if (Ty->isFunctionTy())
          return Function::Create(cast<FunctionType>(Ty),
                                  GlobalValue::ExternalLinkage, A->getName(),
                                  M.get());

        return new GlobalVariable(*M, Ty, false, GlobalValue::ExternalLinkage,
                                  nullptr, A->getName(), nullptr,
                                  GlobalValue::NotThreadLocal,
                                  A->getType()->getAddressSpace());
      }

      return nullptr;
    });

    // Create decls in the new module.
    for (auto *F : Part)
      cloneFunctionDecl(*M, *F, &VMap);

    // Move the function bodies.
    for (auto *F : Part)
      moveFunctionBody(*F, VMap, &Materializer);

    auto K = ES.allocateVModule();

    auto LegacyLookup = [this, &LD](const std::string &Name) -> JITSymbol {
      return LD.findSymbol(BaseLayer, Name, false);
    };

    // Create memory manager and symbol resolver.
    auto Resolver = createSymbolResolver(
        [&LD, LegacyLookup](const SymbolNameSet &Symbols) {
          auto RS = getResponsibilitySetWithLegacyFn(Symbols, LegacyLookup);
          if (!RS) {
            logAllUnhandledErrors(
                RS.takeError(), errs(),
                "CODLayer/SubResolver responsibility set lookup failed: ");
            return SymbolNameSet();
          }

          if (RS->size() == Symbols.size())
            return *RS;

          SymbolNameSet NotFoundViaLegacyLookup;
          for (auto &S : Symbols)
            if (!RS->count(S))
              NotFoundViaLegacyLookup.insert(S);

          auto RS2 =
              LD.BackingResolver->getResponsibilitySet(NotFoundViaLegacyLookup);

          for (auto &S : RS2)
            (*RS).insert(S);

          return *RS;
        },
        [this, &LD, LegacyLookup](std::shared_ptr<AsynchronousSymbolQuery> Q,
                                  SymbolNameSet Symbols) {
          auto NotFoundViaLegacyLookup =
              lookupWithLegacyFn(ES, *Q, Symbols, LegacyLookup);
          return LD.BackingResolver->lookup(Q,
                                            std::move(NotFoundViaLegacyLookup));
        });
    SetSymbolResolver(K, std::move(Resolver));

    if (auto Err = BaseLayer.addModule(std::move(K), std::move(M)))
      return std::move(Err);

    return K;
  }

  ExecutionSession &ES;
  BaseLayerT &BaseLayer;
  SymbolResolverGetter GetSymbolResolver;
  SymbolResolverSetter SetSymbolResolver;
  PartitioningFtor Partition;
  CompileCallbackMgrT &CompileCallbackMgr;
  IndirectStubsManagerBuilderT CreateIndirectStubsManager;

  std::map<VModuleKey, LogicalDylib> LogicalDylibs;
  bool CloneStubsIntoPartitions;
};

} // end namespace orc

} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_COMPILEONDEMANDLAYER_H
