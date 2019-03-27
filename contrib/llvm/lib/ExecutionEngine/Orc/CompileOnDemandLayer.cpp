//===----- CompileOnDemandLayer.cpp - Lazily emit IR on first call --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/CompileOnDemandLayer.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Module.h"

using namespace llvm;
using namespace llvm::orc;

static ThreadSafeModule extractSubModule(ThreadSafeModule &TSM,
                                         StringRef Suffix,
                                         GVPredicate ShouldExtract) {

  auto DeleteExtractedDefs = [](GlobalValue &GV) {
    // Bump the linkage: this global will be provided by the external module.
    GV.setLinkage(GlobalValue::ExternalLinkage);

    // Delete the definition in the source module.
    if (isa<Function>(GV)) {
      auto &F = cast<Function>(GV);
      F.deleteBody();
      F.setPersonalityFn(nullptr);
    } else if (isa<GlobalVariable>(GV)) {
      cast<GlobalVariable>(GV).setInitializer(nullptr);
    } else if (isa<GlobalAlias>(GV)) {
      // We need to turn deleted aliases into function or variable decls based
      // on the type of their aliasee.
      auto &A = cast<GlobalAlias>(GV);
      Constant *Aliasee = A.getAliasee();
      assert(A.hasName() && "Anonymous alias?");
      assert(Aliasee->hasName() && "Anonymous aliasee");
      std::string AliasName = A.getName();

      if (isa<Function>(Aliasee)) {
        auto *F = cloneFunctionDecl(*A.getParent(), *cast<Function>(Aliasee));
        A.replaceAllUsesWith(F);
        A.eraseFromParent();
        F->setName(AliasName);
      } else if (isa<GlobalVariable>(Aliasee)) {
        auto *G = cloneGlobalVariableDecl(*A.getParent(),
                                          *cast<GlobalVariable>(Aliasee));
        A.replaceAllUsesWith(G);
        A.eraseFromParent();
        G->setName(AliasName);
      } else
        llvm_unreachable("Alias to unsupported type");
    } else
      llvm_unreachable("Unsupported global type");
  };

  auto NewTSMod = cloneToNewContext(TSM, ShouldExtract, DeleteExtractedDefs);
  auto &M = *NewTSMod.getModule();
  M.setModuleIdentifier((M.getModuleIdentifier() + Suffix).str());

  return NewTSMod;
}

namespace llvm {
namespace orc {

class PartitioningIRMaterializationUnit : public IRMaterializationUnit {
public:
  PartitioningIRMaterializationUnit(ExecutionSession &ES, ThreadSafeModule TSM,
                                    VModuleKey K, CompileOnDemandLayer &Parent)
      : IRMaterializationUnit(ES, std::move(TSM), std::move(K)),
        Parent(Parent) {}

  PartitioningIRMaterializationUnit(
      ThreadSafeModule TSM, SymbolFlagsMap SymbolFlags,
      SymbolNameToDefinitionMap SymbolToDefinition,
      CompileOnDemandLayer &Parent)
      : IRMaterializationUnit(std::move(TSM), std::move(K),
                              std::move(SymbolFlags),
                              std::move(SymbolToDefinition)),
        Parent(Parent) {}

private:
  void materialize(MaterializationResponsibility R) override {
    Parent.emitPartition(std::move(R), std::move(TSM),
                         std::move(SymbolToDefinition));
  }

  void discard(const JITDylib &V, const SymbolStringPtr &Name) override {
    // All original symbols were materialized by the CODLayer and should be
    // final. The function bodies provided by M should never be overridden.
    llvm_unreachable("Discard should never be called on an "
                     "ExtractingIRMaterializationUnit");
  }

  mutable std::mutex SourceModuleMutex;
  CompileOnDemandLayer &Parent;
};

Optional<CompileOnDemandLayer::GlobalValueSet>
CompileOnDemandLayer::compileRequested(GlobalValueSet Requested) {
  return std::move(Requested);
}

Optional<CompileOnDemandLayer::GlobalValueSet>
CompileOnDemandLayer::compileWholeModule(GlobalValueSet Requested) {
  return None;
}

CompileOnDemandLayer::CompileOnDemandLayer(
    ExecutionSession &ES, IRLayer &BaseLayer, LazyCallThroughManager &LCTMgr,
    IndirectStubsManagerBuilder BuildIndirectStubsManager)
    : IRLayer(ES), BaseLayer(BaseLayer), LCTMgr(LCTMgr),
      BuildIndirectStubsManager(std::move(BuildIndirectStubsManager)) {}

void CompileOnDemandLayer::setPartitionFunction(PartitionFunction Partition) {
  this->Partition = std::move(Partition);
}

void CompileOnDemandLayer::emit(MaterializationResponsibility R,
                                ThreadSafeModule TSM) {
  assert(TSM.getModule() && "Null module");

  auto &ES = getExecutionSession();
  auto &M = *TSM.getModule();

  // First, do some cleanup on the module:
  cleanUpModule(M);

  // Now sort the callables and non-callables, build re-exports and lodge the
  // actual module with the implementation dylib.
  auto &PDR = getPerDylibResources(R.getTargetJITDylib());

  MangleAndInterner Mangle(ES, M.getDataLayout());
  SymbolAliasMap NonCallables;
  SymbolAliasMap Callables;
  for (auto &GV : M.global_values()) {
    if (GV.isDeclaration() || GV.hasLocalLinkage() || GV.hasAppendingLinkage())
      continue;

    auto Name = Mangle(GV.getName());
    auto Flags = JITSymbolFlags::fromGlobalValue(GV);
    if (Flags.isCallable())
      Callables[Name] = SymbolAliasMapEntry(Name, Flags);
    else
      NonCallables[Name] = SymbolAliasMapEntry(Name, Flags);
  }

  // Create a partitioning materialization unit and lodge it with the
  // implementation dylib.
  if (auto Err = PDR.getImplDylib().define(
          llvm::make_unique<PartitioningIRMaterializationUnit>(
              ES, std::move(TSM), R.getVModuleKey(), *this))) {
    ES.reportError(std::move(Err));
    R.failMaterialization();
    return;
  }

  R.replace(reexports(PDR.getImplDylib(), std::move(NonCallables), true));
  R.replace(lazyReexports(LCTMgr, PDR.getISManager(), PDR.getImplDylib(),
                          std::move(Callables)));
}

CompileOnDemandLayer::PerDylibResources &
CompileOnDemandLayer::getPerDylibResources(JITDylib &TargetD) {
  auto I = DylibResources.find(&TargetD);
  if (I == DylibResources.end()) {
    auto &ImplD = getExecutionSession().createJITDylib(
        TargetD.getName() + ".impl", false);
    TargetD.withSearchOrderDo([&](const JITDylibSearchList &TargetSearchOrder) {
      auto NewSearchOrder = TargetSearchOrder;
      assert(!NewSearchOrder.empty() &&
             NewSearchOrder.front().first == &TargetD &&
             NewSearchOrder.front().second == true &&
             "TargetD must be at the front of its own search order and match "
             "non-exported symbol");
      NewSearchOrder.insert(std::next(NewSearchOrder.begin()), {&ImplD, true});
      ImplD.setSearchOrder(std::move(NewSearchOrder), false);
    });
    PerDylibResources PDR(ImplD, BuildIndirectStubsManager());
    I = DylibResources.insert(std::make_pair(&TargetD, std::move(PDR))).first;
  }

  return I->second;
}

void CompileOnDemandLayer::cleanUpModule(Module &M) {
  for (auto &F : M.functions()) {
    if (F.isDeclaration())
      continue;

    if (F.hasAvailableExternallyLinkage()) {
      F.deleteBody();
      F.setPersonalityFn(nullptr);
      continue;
    }
  }
}

void CompileOnDemandLayer::expandPartition(GlobalValueSet &Partition) {
  // Expands the partition to ensure the following rules hold:
  // (1) If any alias is in the partition, its aliasee is also in the partition.
  // (2) If any aliasee is in the partition, its aliases are also in the
  //     partiton.
  // (3) If any global variable is in the partition then all global variables
  //     are in the partition.
  assert(!Partition.empty() && "Unexpected empty partition");

  const Module &M = *(*Partition.begin())->getParent();
  bool ContainsGlobalVariables = false;
  std::vector<const GlobalValue *> GVsToAdd;

  for (auto *GV : Partition)
    if (isa<GlobalAlias>(GV))
      GVsToAdd.push_back(
          cast<GlobalValue>(cast<GlobalAlias>(GV)->getAliasee()));
    else if (isa<GlobalVariable>(GV))
      ContainsGlobalVariables = true;

  for (auto &A : M.aliases())
    if (Partition.count(cast<GlobalValue>(A.getAliasee())))
      GVsToAdd.push_back(&A);

  if (ContainsGlobalVariables)
    for (auto &G : M.globals())
      GVsToAdd.push_back(&G);

  for (auto *GV : GVsToAdd)
    Partition.insert(GV);
}

void CompileOnDemandLayer::emitPartition(
    MaterializationResponsibility R, ThreadSafeModule TSM,
    IRMaterializationUnit::SymbolNameToDefinitionMap Defs) {

  // FIXME: Need a 'notify lazy-extracting/emitting' callback to tie the
  //        extracted module key, extracted module, and source module key
  //        together. This could be used, for example, to provide a specific
  //        memory manager instance to the linking layer.

  auto &ES = getExecutionSession();

  GlobalValueSet RequestedGVs;
  for (auto &Name : R.getRequestedSymbols()) {
    assert(Defs.count(Name) && "No definition for symbol");
    RequestedGVs.insert(Defs[Name]);
  }

  auto GVsToExtract = Partition(RequestedGVs);

  // Take a 'None' partition to mean the whole module (as opposed to an empty
  // partition, which means "materialize nothing"). Emit the whole module
  // unmodified to the base layer.
  if (GVsToExtract == None) {
    Defs.clear();
    BaseLayer.emit(std::move(R), std::move(TSM));
    return;
  }

  // If the partition is empty, return the whole module to the symbol table.
  if (GVsToExtract->empty()) {
    R.replace(llvm::make_unique<PartitioningIRMaterializationUnit>(
        std::move(TSM), R.getSymbols(), std::move(Defs), *this));
    return;
  }

  // Ok -- we actually need to partition the symbols. Promote the symbol
  // linkages/names.
  // FIXME: We apply this once per partitioning. It's safe, but overkill.
  {
    auto PromotedGlobals = PromoteSymbols(*TSM.getModule());
    if (!PromotedGlobals.empty()) {
      MangleAndInterner Mangle(ES, TSM.getModule()->getDataLayout());
      SymbolFlagsMap SymbolFlags;
      for (auto &GV : PromotedGlobals)
        SymbolFlags[Mangle(GV->getName())] =
            JITSymbolFlags::fromGlobalValue(*GV);
      if (auto Err = R.defineMaterializing(SymbolFlags)) {
        ES.reportError(std::move(Err));
        R.failMaterialization();
        return;
      }
    }
  }

  expandPartition(*GVsToExtract);

  // Extract the requested partiton (plus any necessary aliases) and
  // put the rest back into the impl dylib.
  auto ShouldExtract = [&](const GlobalValue &GV) -> bool {
    return GVsToExtract->count(&GV);
  };

  auto ExtractedTSM = extractSubModule(TSM, ".submodule", ShouldExtract);
  R.replace(llvm::make_unique<PartitioningIRMaterializationUnit>(
      ES, std::move(TSM), R.getVModuleKey(), *this));

  BaseLayer.emit(std::move(R), std::move(ExtractedTSM));
}

} // end namespace orc
} // end namespace llvm
