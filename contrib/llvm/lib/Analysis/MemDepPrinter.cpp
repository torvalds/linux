//===- MemDepPrinter.cpp - Printer for MemoryDependenceAnalysis -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SetVector.h"
#include "llvm/Analysis/MemoryDependenceAnalysis.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

namespace {
  struct MemDepPrinter : public FunctionPass {
    const Function *F;

    enum DepType {
      Clobber = 0,
      Def,
      NonFuncLocal,
      Unknown
    };

    static const char *const DepTypeStr[];

    typedef PointerIntPair<const Instruction *, 2, DepType> InstTypePair;
    typedef std::pair<InstTypePair, const BasicBlock *> Dep;
    typedef SmallSetVector<Dep, 4> DepSet;
    typedef DenseMap<const Instruction *, DepSet> DepSetMap;
    DepSetMap Deps;

    static char ID; // Pass identifcation, replacement for typeid
    MemDepPrinter() : FunctionPass(ID) {
      initializeMemDepPrinterPass(*PassRegistry::getPassRegistry());
    }

    bool runOnFunction(Function &F) override;

    void print(raw_ostream &OS, const Module * = nullptr) const override;

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequiredTransitive<AAResultsWrapperPass>();
      AU.addRequiredTransitive<MemoryDependenceWrapperPass>();
      AU.setPreservesAll();
    }

    void releaseMemory() override {
      Deps.clear();
      F = nullptr;
    }

  private:
    static InstTypePair getInstTypePair(MemDepResult dep) {
      if (dep.isClobber())
        return InstTypePair(dep.getInst(), Clobber);
      if (dep.isDef())
        return InstTypePair(dep.getInst(), Def);
      if (dep.isNonFuncLocal())
        return InstTypePair(dep.getInst(), NonFuncLocal);
      assert(dep.isUnknown() && "unexpected dependence type");
      return InstTypePair(dep.getInst(), Unknown);
    }
    static InstTypePair getInstTypePair(const Instruction* inst, DepType type) {
      return InstTypePair(inst, type);
    }
  };
}

char MemDepPrinter::ID = 0;
INITIALIZE_PASS_BEGIN(MemDepPrinter, "print-memdeps",
                      "Print MemDeps of function", false, true)
INITIALIZE_PASS_DEPENDENCY(MemoryDependenceWrapperPass)
INITIALIZE_PASS_END(MemDepPrinter, "print-memdeps",
                      "Print MemDeps of function", false, true)

FunctionPass *llvm::createMemDepPrinter() {
  return new MemDepPrinter();
}

const char *const MemDepPrinter::DepTypeStr[]
  = {"Clobber", "Def", "NonFuncLocal", "Unknown"};

bool MemDepPrinter::runOnFunction(Function &F) {
  this->F = &F;
  MemoryDependenceResults &MDA = getAnalysis<MemoryDependenceWrapperPass>().getMemDep();

  // All this code uses non-const interfaces because MemDep is not
  // const-friendly, though nothing is actually modified.
  for (auto &I : instructions(F)) {
    Instruction *Inst = &I;

    if (!Inst->mayReadFromMemory() && !Inst->mayWriteToMemory())
      continue;

    MemDepResult Res = MDA.getDependency(Inst);
    if (!Res.isNonLocal()) {
      Deps[Inst].insert(std::make_pair(getInstTypePair(Res),
                                       static_cast<BasicBlock *>(nullptr)));
    } else if (auto *Call = dyn_cast<CallBase>(Inst)) {
      const MemoryDependenceResults::NonLocalDepInfo &NLDI =
          MDA.getNonLocalCallDependency(Call);

      DepSet &InstDeps = Deps[Inst];
      for (const NonLocalDepEntry &I : NLDI) {
        const MemDepResult &Res = I.getResult();
        InstDeps.insert(std::make_pair(getInstTypePair(Res), I.getBB()));
      }
    } else {
      SmallVector<NonLocalDepResult, 4> NLDI;
      assert( (isa<LoadInst>(Inst) || isa<StoreInst>(Inst) ||
               isa<VAArgInst>(Inst)) && "Unknown memory instruction!");
      MDA.getNonLocalPointerDependency(Inst, NLDI);

      DepSet &InstDeps = Deps[Inst];
      for (const NonLocalDepResult &I : NLDI) {
        const MemDepResult &Res = I.getResult();
        InstDeps.insert(std::make_pair(getInstTypePair(Res), I.getBB()));
      }
    }
  }

  return false;
}

void MemDepPrinter::print(raw_ostream &OS, const Module *M) const {
  for (const auto &I : instructions(*F)) {
    const Instruction *Inst = &I;

    DepSetMap::const_iterator DI = Deps.find(Inst);
    if (DI == Deps.end())
      continue;

    const DepSet &InstDeps = DI->second;

    for (const auto &I : InstDeps) {
      const Instruction *DepInst = I.first.getPointer();
      DepType type = I.first.getInt();
      const BasicBlock *DepBB = I.second;

      OS << "    ";
      OS << DepTypeStr[type];
      if (DepBB) {
        OS << " in block ";
        DepBB->printAsOperand(OS, /*PrintType=*/false, M);
      }
      if (DepInst) {
        OS << " from: ";
        DepInst->print(OS);
      }
      OS << "\n";
    }

    Inst->print(OS);
    OS << "\n\n";
  }
}
