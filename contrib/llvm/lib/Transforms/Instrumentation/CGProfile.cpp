//===-- CGProfile.cpp -----------------------------------------------------===//
//
//                      The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Instrumentation/CGProfile.h"

#include "llvm/ADT/MapVector.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/PassManager.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Transforms/Instrumentation.h"

#include <array>

using namespace llvm;

PreservedAnalyses CGProfilePass::run(Module &M, ModuleAnalysisManager &MAM) {
  MapVector<std::pair<Function *, Function *>, uint64_t> Counts;
  FunctionAnalysisManager &FAM =
      MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  InstrProfSymtab Symtab;
  auto UpdateCounts = [&](TargetTransformInfo &TTI, Function *F,
                          Function *CalledF, uint64_t NewCount) {
    if (!CalledF || !TTI.isLoweredToCall(CalledF))
      return;
    uint64_t &Count = Counts[std::make_pair(F, CalledF)];
    Count = SaturatingAdd(Count, NewCount);
  };
  // Ignore error here.  Indirect calls are ignored if this fails.
  (void)(bool)Symtab.create(M);
  for (auto &F : M) {
    if (F.isDeclaration())
      continue;
    auto &BFI = FAM.getResult<BlockFrequencyAnalysis>(F);
    if (BFI.getEntryFreq() == 0)
      continue;
    TargetTransformInfo &TTI = FAM.getResult<TargetIRAnalysis>(F);
    for (auto &BB : F) {
      Optional<uint64_t> BBCount = BFI.getBlockProfileCount(&BB);
      if (!BBCount)
        continue;
      for (auto &I : BB) {
        CallSite CS(&I);
        if (!CS)
          continue;
        if (CS.isIndirectCall()) {
          InstrProfValueData ValueData[8];
          uint32_t ActualNumValueData;
          uint64_t TotalC;
          if (!getValueProfDataFromInst(*CS.getInstruction(),
                                        IPVK_IndirectCallTarget, 8, ValueData,
                                        ActualNumValueData, TotalC))
            continue;
          for (const auto &VD :
               ArrayRef<InstrProfValueData>(ValueData, ActualNumValueData)) {
            UpdateCounts(TTI, &F, Symtab.getFunction(VD.Value), VD.Count);
          }
          continue;
        }
        UpdateCounts(TTI, &F, CS.getCalledFunction(), *BBCount);
      }
    }
  }

  addModuleFlags(M, Counts);

  return PreservedAnalyses::all();
}

void CGProfilePass::addModuleFlags(
    Module &M,
    MapVector<std::pair<Function *, Function *>, uint64_t> &Counts) const {
  if (Counts.empty())
    return;

  LLVMContext &Context = M.getContext();
  MDBuilder MDB(Context);
  std::vector<Metadata *> Nodes;

  for (auto E : Counts) {
    Metadata *Vals[] = {ValueAsMetadata::get(E.first.first),
                        ValueAsMetadata::get(E.first.second),
                        MDB.createConstant(ConstantInt::get(
                            Type::getInt64Ty(Context), E.second))};
    Nodes.push_back(MDNode::get(Context, Vals));
  }

  M.addModuleFlag(Module::Append, "CG Profile", MDNode::get(Context, Nodes));
}
