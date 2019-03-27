//===- ThinLTOBitcodeWriter.cpp - Bitcode writing pass for ThinLTO --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/ThinLTOBitcodeWriter.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/ModuleSummaryAnalysis.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/Analysis/TypeMetadataUtils.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Object/ModuleSymbolTable.h"
#include "llvm/Pass.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/IPO/FunctionAttrs.h"
#include "llvm/Transforms/IPO/FunctionImport.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
using namespace llvm;

namespace {

// Promote each local-linkage entity defined by ExportM and used by ImportM by
// changing visibility and appending the given ModuleId.
void promoteInternals(Module &ExportM, Module &ImportM, StringRef ModuleId,
                      SetVector<GlobalValue *> &PromoteExtra) {
  DenseMap<const Comdat *, Comdat *> RenamedComdats;
  for (auto &ExportGV : ExportM.global_values()) {
    if (!ExportGV.hasLocalLinkage())
      continue;

    auto Name = ExportGV.getName();
    GlobalValue *ImportGV = nullptr;
    if (!PromoteExtra.count(&ExportGV)) {
      ImportGV = ImportM.getNamedValue(Name);
      if (!ImportGV)
        continue;
      ImportGV->removeDeadConstantUsers();
      if (ImportGV->use_empty()) {
        ImportGV->eraseFromParent();
        continue;
      }
    }

    std::string NewName = (Name + ModuleId).str();

    if (const auto *C = ExportGV.getComdat())
      if (C->getName() == Name)
        RenamedComdats.try_emplace(C, ExportM.getOrInsertComdat(NewName));

    ExportGV.setName(NewName);
    ExportGV.setLinkage(GlobalValue::ExternalLinkage);
    ExportGV.setVisibility(GlobalValue::HiddenVisibility);

    if (ImportGV) {
      ImportGV->setName(NewName);
      ImportGV->setVisibility(GlobalValue::HiddenVisibility);
    }
  }

  if (!RenamedComdats.empty())
    for (auto &GO : ExportM.global_objects())
      if (auto *C = GO.getComdat()) {
        auto Replacement = RenamedComdats.find(C);
        if (Replacement != RenamedComdats.end())
          GO.setComdat(Replacement->second);
      }
}

// Promote all internal (i.e. distinct) type ids used by the module by replacing
// them with external type ids formed using the module id.
//
// Note that this needs to be done before we clone the module because each clone
// will receive its own set of distinct metadata nodes.
void promoteTypeIds(Module &M, StringRef ModuleId) {
  DenseMap<Metadata *, Metadata *> LocalToGlobal;
  auto ExternalizeTypeId = [&](CallInst *CI, unsigned ArgNo) {
    Metadata *MD =
        cast<MetadataAsValue>(CI->getArgOperand(ArgNo))->getMetadata();

    if (isa<MDNode>(MD) && cast<MDNode>(MD)->isDistinct()) {
      Metadata *&GlobalMD = LocalToGlobal[MD];
      if (!GlobalMD) {
        std::string NewName = (Twine(LocalToGlobal.size()) + ModuleId).str();
        GlobalMD = MDString::get(M.getContext(), NewName);
      }

      CI->setArgOperand(ArgNo,
                        MetadataAsValue::get(M.getContext(), GlobalMD));
    }
  };

  if (Function *TypeTestFunc =
          M.getFunction(Intrinsic::getName(Intrinsic::type_test))) {
    for (const Use &U : TypeTestFunc->uses()) {
      auto CI = cast<CallInst>(U.getUser());
      ExternalizeTypeId(CI, 1);
    }
  }

  if (Function *TypeCheckedLoadFunc =
          M.getFunction(Intrinsic::getName(Intrinsic::type_checked_load))) {
    for (const Use &U : TypeCheckedLoadFunc->uses()) {
      auto CI = cast<CallInst>(U.getUser());
      ExternalizeTypeId(CI, 2);
    }
  }

  for (GlobalObject &GO : M.global_objects()) {
    SmallVector<MDNode *, 1> MDs;
    GO.getMetadata(LLVMContext::MD_type, MDs);

    GO.eraseMetadata(LLVMContext::MD_type);
    for (auto MD : MDs) {
      auto I = LocalToGlobal.find(MD->getOperand(1));
      if (I == LocalToGlobal.end()) {
        GO.addMetadata(LLVMContext::MD_type, *MD);
        continue;
      }
      GO.addMetadata(
          LLVMContext::MD_type,
          *MDNode::get(M.getContext(), {MD->getOperand(0), I->second}));
    }
  }
}

// Drop unused globals, and drop type information from function declarations.
// FIXME: If we made functions typeless then there would be no need to do this.
void simplifyExternals(Module &M) {
  FunctionType *EmptyFT =
      FunctionType::get(Type::getVoidTy(M.getContext()), false);

  for (auto I = M.begin(), E = M.end(); I != E;) {
    Function &F = *I++;
    if (F.isDeclaration() && F.use_empty()) {
      F.eraseFromParent();
      continue;
    }

    if (!F.isDeclaration() || F.getFunctionType() == EmptyFT ||
        // Changing the type of an intrinsic may invalidate the IR.
        F.getName().startswith("llvm."))
      continue;

    Function *NewF =
        Function::Create(EmptyFT, GlobalValue::ExternalLinkage,
                         F.getAddressSpace(), "", &M);
    NewF->setVisibility(F.getVisibility());
    NewF->takeName(&F);
    F.replaceAllUsesWith(ConstantExpr::getBitCast(NewF, F.getType()));
    F.eraseFromParent();
  }

  for (auto I = M.global_begin(), E = M.global_end(); I != E;) {
    GlobalVariable &GV = *I++;
    if (GV.isDeclaration() && GV.use_empty()) {
      GV.eraseFromParent();
      continue;
    }
  }
}

static void
filterModule(Module *M,
             function_ref<bool(const GlobalValue *)> ShouldKeepDefinition) {
  std::vector<GlobalValue *> V;
  for (GlobalValue &GV : M->global_values())
    if (!ShouldKeepDefinition(&GV))
      V.push_back(&GV);

  for (GlobalValue *GV : V)
    if (!convertToDeclaration(*GV))
      GV->eraseFromParent();
}

void forEachVirtualFunction(Constant *C, function_ref<void(Function *)> Fn) {
  if (auto *F = dyn_cast<Function>(C))
    return Fn(F);
  if (isa<GlobalValue>(C))
    return;
  for (Value *Op : C->operands())
    forEachVirtualFunction(cast<Constant>(Op), Fn);
}

// If it's possible to split M into regular and thin LTO parts, do so and write
// a multi-module bitcode file with the two parts to OS. Otherwise, write only a
// regular LTO bitcode file to OS.
void splitAndWriteThinLTOBitcode(
    raw_ostream &OS, raw_ostream *ThinLinkOS,
    function_ref<AAResults &(Function &)> AARGetter, Module &M) {
  std::string ModuleId = getUniqueModuleId(&M);
  if (ModuleId.empty()) {
    // We couldn't generate a module ID for this module, write it out as a
    // regular LTO module with an index for summary-based dead stripping.
    ProfileSummaryInfo PSI(M);
    M.addModuleFlag(Module::Error, "ThinLTO", uint32_t(0));
    ModuleSummaryIndex Index = buildModuleSummaryIndex(M, nullptr, &PSI);
    WriteBitcodeToFile(M, OS, /*ShouldPreserveUseListOrder=*/false, &Index);

    if (ThinLinkOS)
      // We don't have a ThinLTO part, but still write the module to the
      // ThinLinkOS if requested so that the expected output file is produced.
      WriteBitcodeToFile(M, *ThinLinkOS, /*ShouldPreserveUseListOrder=*/false,
                         &Index);

    return;
  }

  promoteTypeIds(M, ModuleId);

  // Returns whether a global has attached type metadata. Such globals may
  // participate in CFI or whole-program devirtualization, so they need to
  // appear in the merged module instead of the thin LTO module.
  auto HasTypeMetadata = [](const GlobalObject *GO) {
    return GO->hasMetadata(LLVMContext::MD_type);
  };

  // Collect the set of virtual functions that are eligible for virtual constant
  // propagation. Each eligible function must not access memory, must return
  // an integer of width <=64 bits, must take at least one argument, must not
  // use its first argument (assumed to be "this") and all arguments other than
  // the first one must be of <=64 bit integer type.
  //
  // Note that we test whether this copy of the function is readnone, rather
  // than testing function attributes, which must hold for any copy of the
  // function, even a less optimized version substituted at link time. This is
  // sound because the virtual constant propagation optimizations effectively
  // inline all implementations of the virtual function into each call site,
  // rather than using function attributes to perform local optimization.
  DenseSet<const Function *> EligibleVirtualFns;
  // If any member of a comdat lives in MergedM, put all members of that
  // comdat in MergedM to keep the comdat together.
  DenseSet<const Comdat *> MergedMComdats;
  for (GlobalVariable &GV : M.globals())
    if (HasTypeMetadata(&GV)) {
      if (const auto *C = GV.getComdat())
        MergedMComdats.insert(C);
      forEachVirtualFunction(GV.getInitializer(), [&](Function *F) {
        auto *RT = dyn_cast<IntegerType>(F->getReturnType());
        if (!RT || RT->getBitWidth() > 64 || F->arg_empty() ||
            !F->arg_begin()->use_empty())
          return;
        for (auto &Arg : make_range(std::next(F->arg_begin()), F->arg_end())) {
          auto *ArgT = dyn_cast<IntegerType>(Arg.getType());
          if (!ArgT || ArgT->getBitWidth() > 64)
            return;
        }
        if (!F->isDeclaration() &&
            computeFunctionBodyMemoryAccess(*F, AARGetter(*F)) == MAK_ReadNone)
          EligibleVirtualFns.insert(F);
      });
    }

  ValueToValueMapTy VMap;
  std::unique_ptr<Module> MergedM(
      CloneModule(M, VMap, [&](const GlobalValue *GV) -> bool {
        if (const auto *C = GV->getComdat())
          if (MergedMComdats.count(C))
            return true;
        if (auto *F = dyn_cast<Function>(GV))
          return EligibleVirtualFns.count(F);
        if (auto *GVar = dyn_cast_or_null<GlobalVariable>(GV->getBaseObject()))
          return HasTypeMetadata(GVar);
        return false;
      }));
  StripDebugInfo(*MergedM);
  MergedM->setModuleInlineAsm("");

  for (Function &F : *MergedM)
    if (!F.isDeclaration()) {
      // Reset the linkage of all functions eligible for virtual constant
      // propagation. The canonical definitions live in the thin LTO module so
      // that they can be imported.
      F.setLinkage(GlobalValue::AvailableExternallyLinkage);
      F.setComdat(nullptr);
    }

  SetVector<GlobalValue *> CfiFunctions;
  for (auto &F : M)
    if ((!F.hasLocalLinkage() || F.hasAddressTaken()) && HasTypeMetadata(&F))
      CfiFunctions.insert(&F);

  // Remove all globals with type metadata, globals with comdats that live in
  // MergedM, and aliases pointing to such globals from the thin LTO module.
  filterModule(&M, [&](const GlobalValue *GV) {
    if (auto *GVar = dyn_cast_or_null<GlobalVariable>(GV->getBaseObject()))
      if (HasTypeMetadata(GVar))
        return false;
    if (const auto *C = GV->getComdat())
      if (MergedMComdats.count(C))
        return false;
    return true;
  });

  promoteInternals(*MergedM, M, ModuleId, CfiFunctions);
  promoteInternals(M, *MergedM, ModuleId, CfiFunctions);

  auto &Ctx = MergedM->getContext();
  SmallVector<MDNode *, 8> CfiFunctionMDs;
  for (auto V : CfiFunctions) {
    Function &F = *cast<Function>(V);
    SmallVector<MDNode *, 2> Types;
    F.getMetadata(LLVMContext::MD_type, Types);

    SmallVector<Metadata *, 4> Elts;
    Elts.push_back(MDString::get(Ctx, F.getName()));
    CfiFunctionLinkage Linkage;
    if (!F.isDeclarationForLinker())
      Linkage = CFL_Definition;
    else if (F.isWeakForLinker())
      Linkage = CFL_WeakDeclaration;
    else
      Linkage = CFL_Declaration;
    Elts.push_back(ConstantAsMetadata::get(
        llvm::ConstantInt::get(Type::getInt8Ty(Ctx), Linkage)));
    for (auto Type : Types)
      Elts.push_back(Type);
    CfiFunctionMDs.push_back(MDTuple::get(Ctx, Elts));
  }

  if(!CfiFunctionMDs.empty()) {
    NamedMDNode *NMD = MergedM->getOrInsertNamedMetadata("cfi.functions");
    for (auto MD : CfiFunctionMDs)
      NMD->addOperand(MD);
  }

  SmallVector<MDNode *, 8> FunctionAliases;
  for (auto &A : M.aliases()) {
    if (!isa<Function>(A.getAliasee()))
      continue;

    auto *F = cast<Function>(A.getAliasee());

    Metadata *Elts[] = {
        MDString::get(Ctx, A.getName()),
        MDString::get(Ctx, F->getName()),
        ConstantAsMetadata::get(
            ConstantInt::get(Type::getInt8Ty(Ctx), A.getVisibility())),
        ConstantAsMetadata::get(
            ConstantInt::get(Type::getInt8Ty(Ctx), A.isWeakForLinker())),
    };

    FunctionAliases.push_back(MDTuple::get(Ctx, Elts));
  }

  if (!FunctionAliases.empty()) {
    NamedMDNode *NMD = MergedM->getOrInsertNamedMetadata("aliases");
    for (auto MD : FunctionAliases)
      NMD->addOperand(MD);
  }

  SmallVector<MDNode *, 8> Symvers;
  ModuleSymbolTable::CollectAsmSymvers(M, [&](StringRef Name, StringRef Alias) {
    Function *F = M.getFunction(Name);
    if (!F || F->use_empty())
      return;

    Symvers.push_back(MDTuple::get(
        Ctx, {MDString::get(Ctx, Name), MDString::get(Ctx, Alias)}));
  });

  if (!Symvers.empty()) {
    NamedMDNode *NMD = MergedM->getOrInsertNamedMetadata("symvers");
    for (auto MD : Symvers)
      NMD->addOperand(MD);
  }

  simplifyExternals(*MergedM);

  // FIXME: Try to re-use BSI and PFI from the original module here.
  ProfileSummaryInfo PSI(M);
  ModuleSummaryIndex Index = buildModuleSummaryIndex(M, nullptr, &PSI);

  // Mark the merged module as requiring full LTO. We still want an index for
  // it though, so that it can participate in summary-based dead stripping.
  MergedM->addModuleFlag(Module::Error, "ThinLTO", uint32_t(0));
  ModuleSummaryIndex MergedMIndex =
      buildModuleSummaryIndex(*MergedM, nullptr, &PSI);

  SmallVector<char, 0> Buffer;

  BitcodeWriter W(Buffer);
  // Save the module hash produced for the full bitcode, which will
  // be used in the backends, and use that in the minimized bitcode
  // produced for the full link.
  ModuleHash ModHash = {{0}};
  W.writeModule(M, /*ShouldPreserveUseListOrder=*/false, &Index,
                /*GenerateHash=*/true, &ModHash);
  W.writeModule(*MergedM, /*ShouldPreserveUseListOrder=*/false, &MergedMIndex);
  W.writeSymtab();
  W.writeStrtab();
  OS << Buffer;

  // If a minimized bitcode module was requested for the thin link, only
  // the information that is needed by thin link will be written in the
  // given OS (the merged module will be written as usual).
  if (ThinLinkOS) {
    Buffer.clear();
    BitcodeWriter W2(Buffer);
    StripDebugInfo(M);
    W2.writeThinLinkBitcode(M, Index, ModHash);
    W2.writeModule(*MergedM, /*ShouldPreserveUseListOrder=*/false,
                   &MergedMIndex);
    W2.writeSymtab();
    W2.writeStrtab();
    *ThinLinkOS << Buffer;
  }
}

// Returns whether this module needs to be split because splitting is
// enabled and it uses type metadata.
bool requiresSplit(Module &M) {
  // First check if the LTO Unit splitting has been enabled.
  bool EnableSplitLTOUnit = false;
  if (auto *MD = mdconst::extract_or_null<ConstantInt>(
          M.getModuleFlag("EnableSplitLTOUnit")))
    EnableSplitLTOUnit = MD->getZExtValue();
  if (!EnableSplitLTOUnit)
    return false;

  // Module only needs to be split if it contains type metadata.
  for (auto &GO : M.global_objects()) {
    if (GO.hasMetadata(LLVMContext::MD_type))
      return true;
  }

  return false;
}

void writeThinLTOBitcode(raw_ostream &OS, raw_ostream *ThinLinkOS,
                         function_ref<AAResults &(Function &)> AARGetter,
                         Module &M, const ModuleSummaryIndex *Index) {
  // Split module if splitting is enabled and it contains any type metadata.
  if (requiresSplit(M))
    return splitAndWriteThinLTOBitcode(OS, ThinLinkOS, AARGetter, M);

  // Otherwise we can just write it out as a regular module.

  // Save the module hash produced for the full bitcode, which will
  // be used in the backends, and use that in the minimized bitcode
  // produced for the full link.
  ModuleHash ModHash = {{0}};
  WriteBitcodeToFile(M, OS, /*ShouldPreserveUseListOrder=*/false, Index,
                     /*GenerateHash=*/true, &ModHash);
  // If a minimized bitcode module was requested for the thin link, only
  // the information that is needed by thin link will be written in the
  // given OS.
  if (ThinLinkOS && Index)
    WriteThinLinkBitcodeToFile(M, *ThinLinkOS, *Index, ModHash);
}

class WriteThinLTOBitcode : public ModulePass {
  raw_ostream &OS; // raw_ostream to print on
  // The output stream on which to emit a minimized module for use
  // just in the thin link, if requested.
  raw_ostream *ThinLinkOS;

public:
  static char ID; // Pass identification, replacement for typeid
  WriteThinLTOBitcode() : ModulePass(ID), OS(dbgs()), ThinLinkOS(nullptr) {
    initializeWriteThinLTOBitcodePass(*PassRegistry::getPassRegistry());
  }

  explicit WriteThinLTOBitcode(raw_ostream &o, raw_ostream *ThinLinkOS)
      : ModulePass(ID), OS(o), ThinLinkOS(ThinLinkOS) {
    initializeWriteThinLTOBitcodePass(*PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override { return "ThinLTO Bitcode Writer"; }

  bool runOnModule(Module &M) override {
    const ModuleSummaryIndex *Index =
        &(getAnalysis<ModuleSummaryIndexWrapperPass>().getIndex());
    writeThinLTOBitcode(OS, ThinLinkOS, LegacyAARGetter(*this), M, Index);
    return true;
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    AU.addRequired<AssumptionCacheTracker>();
    AU.addRequired<ModuleSummaryIndexWrapperPass>();
    AU.addRequired<TargetLibraryInfoWrapperPass>();
  }
};
} // anonymous namespace

char WriteThinLTOBitcode::ID = 0;
INITIALIZE_PASS_BEGIN(WriteThinLTOBitcode, "write-thinlto-bitcode",
                      "Write ThinLTO Bitcode", false, true)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(ModuleSummaryIndexWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_END(WriteThinLTOBitcode, "write-thinlto-bitcode",
                    "Write ThinLTO Bitcode", false, true)

ModulePass *llvm::createWriteThinLTOBitcodePass(raw_ostream &Str,
                                                raw_ostream *ThinLinkOS) {
  return new WriteThinLTOBitcode(Str, ThinLinkOS);
}

PreservedAnalyses
llvm::ThinLTOBitcodeWriterPass::run(Module &M, ModuleAnalysisManager &AM) {
  FunctionAnalysisManager &FAM =
      AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  writeThinLTOBitcode(OS, ThinLinkOS,
                      [&FAM](Function &F) -> AAResults & {
                        return FAM.getResult<AAManager>(F);
                      },
                      M, &AM.getResult<ModuleSummaryIndexAnalysis>(M));
  return PreservedAnalyses::all();
}
