//===-- AMDGPUAnnotateUniformValues.cpp - ---------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This pass adds amdgpu.uniform metadata to IR values so this information
/// can be used during instruction selection.
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "AMDGPUIntrinsicInfo.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Analysis/LegacyDivergenceAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemoryDependenceAnalysis.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "amdgpu-annotate-uniform"

using namespace llvm;

namespace {

class AMDGPUAnnotateUniformValues : public FunctionPass,
                       public InstVisitor<AMDGPUAnnotateUniformValues> {
  LegacyDivergenceAnalysis *DA;
  MemoryDependenceResults *MDR;
  LoopInfo *LI;
  DenseMap<Value*, GetElementPtrInst*> noClobberClones;
  bool isKernelFunc;

public:
  static char ID;
  AMDGPUAnnotateUniformValues() :
    FunctionPass(ID) { }
  bool doInitialization(Module &M) override;
  bool runOnFunction(Function &F) override;
  StringRef getPassName() const override {
    return "AMDGPU Annotate Uniform Values";
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<LegacyDivergenceAnalysis>();
    AU.addRequired<MemoryDependenceWrapperPass>();
    AU.addRequired<LoopInfoWrapperPass>();
    AU.setPreservesAll();
 }

  void visitBranchInst(BranchInst &I);
  void visitLoadInst(LoadInst &I);
  bool isClobberedInFunction(LoadInst * Load);
};

} // End anonymous namespace

INITIALIZE_PASS_BEGIN(AMDGPUAnnotateUniformValues, DEBUG_TYPE,
                      "Add AMDGPU uniform metadata", false, false)
INITIALIZE_PASS_DEPENDENCY(LegacyDivergenceAnalysis)
INITIALIZE_PASS_DEPENDENCY(MemoryDependenceWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_END(AMDGPUAnnotateUniformValues, DEBUG_TYPE,
                    "Add AMDGPU uniform metadata", false, false)

char AMDGPUAnnotateUniformValues::ID = 0;

static void setUniformMetadata(Instruction *I) {
  I->setMetadata("amdgpu.uniform", MDNode::get(I->getContext(), {}));
}
static void setNoClobberMetadata(Instruction *I) {
  I->setMetadata("amdgpu.noclobber", MDNode::get(I->getContext(), {}));
}

static void DFS(BasicBlock *Root, SetVector<BasicBlock*> & Set) {
  for (auto I : predecessors(Root))
    if (Set.insert(I))
      DFS(I, Set);
}

bool AMDGPUAnnotateUniformValues::isClobberedInFunction(LoadInst * Load) {
  // 1. get Loop for the Load->getparent();
  // 2. if it exists, collect all the BBs from the most outer
  // loop and check for the writes. If NOT - start DFS over all preds.
  // 3. Start DFS over all preds from the most outer loop header.
  SetVector<BasicBlock *> Checklist;
  BasicBlock *Start = Load->getParent();
  Checklist.insert(Start);
  const Value *Ptr = Load->getPointerOperand();
  const Loop *L = LI->getLoopFor(Start);
  if (L) {
    const Loop *P = L;
    do {
      L = P;
      P = P->getParentLoop();
    } while (P);
    Checklist.insert(L->block_begin(), L->block_end());
    Start = L->getHeader();
  }

  DFS(Start, Checklist);
  for (auto &BB : Checklist) {
    BasicBlock::iterator StartIt = (!L && (BB == Load->getParent())) ?
      BasicBlock::iterator(Load) : BB->end();
    auto Q = MDR->getPointerDependencyFrom(MemoryLocation(Ptr), true,
                                           StartIt, BB, Load);
    if (Q.isClobber() || Q.isUnknown())
      return true;
  }
  return false;
}

void AMDGPUAnnotateUniformValues::visitBranchInst(BranchInst &I) {
  if (DA->isUniform(&I))
    setUniformMetadata(I.getParent()->getTerminator());
}

void AMDGPUAnnotateUniformValues::visitLoadInst(LoadInst &I) {
  Value *Ptr = I.getPointerOperand();
  if (!DA->isUniform(Ptr))
    return;
  auto isGlobalLoad = [&](LoadInst &Load)->bool {
    return Load.getPointerAddressSpace() == AMDGPUAS::GLOBAL_ADDRESS;
  };
  // We're tracking up to the Function boundaries
  // We cannot go beyond because of FunctionPass restrictions
  // Thus we can ensure that memory not clobbered for memory
  // operations that live in kernel only.
  bool NotClobbered = isKernelFunc &&   !isClobberedInFunction(&I);
  Instruction *PtrI = dyn_cast<Instruction>(Ptr);
  if (!PtrI && NotClobbered && isGlobalLoad(I)) {
    if (isa<Argument>(Ptr) || isa<GlobalValue>(Ptr)) {
      // Lookup for the existing GEP
      if (noClobberClones.count(Ptr)) {
        PtrI = noClobberClones[Ptr];
      } else {
        // Create GEP of the Value
        Function *F = I.getParent()->getParent();
        Value *Idx = Constant::getIntegerValue(
          Type::getInt32Ty(Ptr->getContext()), APInt(64, 0));
        // Insert GEP at the entry to make it dominate all uses
        PtrI = GetElementPtrInst::Create(
          Ptr->getType()->getPointerElementType(), Ptr,
          ArrayRef<Value*>(Idx), Twine(""), F->getEntryBlock().getFirstNonPHI());
      }
      I.replaceUsesOfWith(Ptr, PtrI);
    }
  }

  if (PtrI) {
    setUniformMetadata(PtrI);
    if (NotClobbered)
      setNoClobberMetadata(PtrI);
  }
}

bool AMDGPUAnnotateUniformValues::doInitialization(Module &M) {
  return false;
}

bool AMDGPUAnnotateUniformValues::runOnFunction(Function &F) {
  if (skipFunction(F))
    return false;

  DA  = &getAnalysis<LegacyDivergenceAnalysis>();
  MDR = &getAnalysis<MemoryDependenceWrapperPass>().getMemDep();
  LI  = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  isKernelFunc = F.getCallingConv() == CallingConv::AMDGPU_KERNEL;

  visit(F);
  noClobberClones.clear();
  return true;
}

FunctionPass *
llvm::createAMDGPUAnnotateUniformValues() {
  return new AMDGPUAnnotateUniformValues();
}
