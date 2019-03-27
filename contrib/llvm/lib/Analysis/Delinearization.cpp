//===---- Delinearization.cpp - MultiDimensional Index Delinearization ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This implements an analysis pass that tries to delinearize all GEP
// instructions in all loops using the SCEV analysis functionality. This pass is
// only used for testing purposes: if your pass needs delinearization, please
// use the on-demand SCEVAddRecExpr::delinearize() function.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Type.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DL_NAME "delinearize"
#define DEBUG_TYPE DL_NAME

namespace {

class Delinearization : public FunctionPass {
  Delinearization(const Delinearization &); // do not implement
protected:
  Function *F;
  LoopInfo *LI;
  ScalarEvolution *SE;

public:
  static char ID; // Pass identification, replacement for typeid

  Delinearization() : FunctionPass(ID) {
    initializeDelinearizationPass(*PassRegistry::getPassRegistry());
  }
  bool runOnFunction(Function &F) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  void print(raw_ostream &O, const Module *M = nullptr) const override;
};

} // end anonymous namespace

void Delinearization::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<LoopInfoWrapperPass>();
  AU.addRequired<ScalarEvolutionWrapperPass>();
}

bool Delinearization::runOnFunction(Function &F) {
  this->F = &F;
  SE = &getAnalysis<ScalarEvolutionWrapperPass>().getSE();
  LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  return false;
}

void Delinearization::print(raw_ostream &O, const Module *) const {
  O << "Delinearization on function " << F->getName() << ":\n";
  for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
    Instruction *Inst = &(*I);

    // Only analyze loads and stores.
    if (!isa<StoreInst>(Inst) && !isa<LoadInst>(Inst) &&
        !isa<GetElementPtrInst>(Inst))
      continue;

    const BasicBlock *BB = Inst->getParent();
    // Delinearize the memory access as analyzed in all the surrounding loops.
    // Do not analyze memory accesses outside loops.
    for (Loop *L = LI->getLoopFor(BB); L != nullptr; L = L->getParentLoop()) {
      const SCEV *AccessFn = SE->getSCEVAtScope(getPointerOperand(Inst), L);

      const SCEVUnknown *BasePointer =
          dyn_cast<SCEVUnknown>(SE->getPointerBase(AccessFn));
      // Do not delinearize if we cannot find the base pointer.
      if (!BasePointer)
        break;
      AccessFn = SE->getMinusSCEV(AccessFn, BasePointer);

      O << "\n";
      O << "Inst:" << *Inst << "\n";
      O << "In Loop with Header: " << L->getHeader()->getName() << "\n";
      O << "AccessFunction: " << *AccessFn << "\n";

      SmallVector<const SCEV *, 3> Subscripts, Sizes;
      SE->delinearize(AccessFn, Subscripts, Sizes, SE->getElementSize(Inst));
      if (Subscripts.size() == 0 || Sizes.size() == 0 ||
          Subscripts.size() != Sizes.size()) {
        O << "failed to delinearize\n";
        continue;
      }

      O << "Base offset: " << *BasePointer << "\n";
      O << "ArrayDecl[UnknownSize]";
      int Size = Subscripts.size();
      for (int i = 0; i < Size - 1; i++)
        O << "[" << *Sizes[i] << "]";
      O << " with elements of " << *Sizes[Size - 1] << " bytes.\n";

      O << "ArrayRef";
      for (int i = 0; i < Size; i++)
        O << "[" << *Subscripts[i] << "]";
      O << "\n";
    }
  }
}

char Delinearization::ID = 0;
static const char delinearization_name[] = "Delinearization";
INITIALIZE_PASS_BEGIN(Delinearization, DL_NAME, delinearization_name, true,
                      true)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_END(Delinearization, DL_NAME, delinearization_name, true, true)

FunctionPass *llvm::createDelinearizationPass() { return new Delinearization; }
