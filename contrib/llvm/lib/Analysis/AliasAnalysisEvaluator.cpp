//===- AliasAnalysisEvaluator.cpp - Alias Analysis Accuracy Evaluator -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/AliasAnalysisEvaluator.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

static cl::opt<bool> PrintAll("print-all-alias-modref-info", cl::ReallyHidden);

static cl::opt<bool> PrintNoAlias("print-no-aliases", cl::ReallyHidden);
static cl::opt<bool> PrintMayAlias("print-may-aliases", cl::ReallyHidden);
static cl::opt<bool> PrintPartialAlias("print-partial-aliases", cl::ReallyHidden);
static cl::opt<bool> PrintMustAlias("print-must-aliases", cl::ReallyHidden);

static cl::opt<bool> PrintNoModRef("print-no-modref", cl::ReallyHidden);
static cl::opt<bool> PrintRef("print-ref", cl::ReallyHidden);
static cl::opt<bool> PrintMod("print-mod", cl::ReallyHidden);
static cl::opt<bool> PrintModRef("print-modref", cl::ReallyHidden);
static cl::opt<bool> PrintMust("print-must", cl::ReallyHidden);
static cl::opt<bool> PrintMustRef("print-mustref", cl::ReallyHidden);
static cl::opt<bool> PrintMustMod("print-mustmod", cl::ReallyHidden);
static cl::opt<bool> PrintMustModRef("print-mustmodref", cl::ReallyHidden);

static cl::opt<bool> EvalAAMD("evaluate-aa-metadata", cl::ReallyHidden);

static void PrintResults(AliasResult AR, bool P, const Value *V1,
                         const Value *V2, const Module *M) {
  if (PrintAll || P) {
    std::string o1, o2;
    {
      raw_string_ostream os1(o1), os2(o2);
      V1->printAsOperand(os1, true, M);
      V2->printAsOperand(os2, true, M);
    }

    if (o2 < o1)
      std::swap(o1, o2);
    errs() << "  " << AR << ":\t" << o1 << ", " << o2 << "\n";
  }
}

static inline void PrintModRefResults(const char *Msg, bool P, Instruction *I,
                                      Value *Ptr, Module *M) {
  if (PrintAll || P) {
    errs() << "  " << Msg << ":  Ptr: ";
    Ptr->printAsOperand(errs(), true, M);
    errs() << "\t<->" << *I << '\n';
  }
}

static inline void PrintModRefResults(const char *Msg, bool P, CallBase *CallA,
                                      CallBase *CallB, Module *M) {
  if (PrintAll || P) {
    errs() << "  " << Msg << ": " << *CallA << " <-> " << *CallB << '\n';
  }
}

static inline void PrintLoadStoreResults(AliasResult AR, bool P,
                                         const Value *V1, const Value *V2,
                                         const Module *M) {
  if (PrintAll || P) {
    errs() << "  " << AR << ": " << *V1 << " <-> " << *V2 << '\n';
  }
}

static inline bool isInterestingPointer(Value *V) {
  return V->getType()->isPointerTy()
      && !isa<ConstantPointerNull>(V);
}

PreservedAnalyses AAEvaluator::run(Function &F, FunctionAnalysisManager &AM) {
  runInternal(F, AM.getResult<AAManager>(F));
  return PreservedAnalyses::all();
}

void AAEvaluator::runInternal(Function &F, AAResults &AA) {
  const DataLayout &DL = F.getParent()->getDataLayout();

  ++FunctionCount;

  SetVector<Value *> Pointers;
  SmallSetVector<CallBase *, 16> Calls;
  SetVector<Value *> Loads;
  SetVector<Value *> Stores;

  for (auto &I : F.args())
    if (I.getType()->isPointerTy())    // Add all pointer arguments.
      Pointers.insert(&I);

  for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
    if (I->getType()->isPointerTy()) // Add all pointer instructions.
      Pointers.insert(&*I);
    if (EvalAAMD && isa<LoadInst>(&*I))
      Loads.insert(&*I);
    if (EvalAAMD && isa<StoreInst>(&*I))
      Stores.insert(&*I);
    Instruction &Inst = *I;
    if (auto *Call = dyn_cast<CallBase>(&Inst)) {
      Value *Callee = Call->getCalledValue();
      // Skip actual functions for direct function calls.
      if (!isa<Function>(Callee) && isInterestingPointer(Callee))
        Pointers.insert(Callee);
      // Consider formals.
      for (Use &DataOp : Call->data_ops())
        if (isInterestingPointer(DataOp))
          Pointers.insert(DataOp);
      Calls.insert(Call);
    } else {
      // Consider all operands.
      for (Instruction::op_iterator OI = Inst.op_begin(), OE = Inst.op_end();
           OI != OE; ++OI)
        if (isInterestingPointer(*OI))
          Pointers.insert(*OI);
    }
  }

  if (PrintAll || PrintNoAlias || PrintMayAlias || PrintPartialAlias ||
      PrintMustAlias || PrintNoModRef || PrintMod || PrintRef || PrintModRef)
    errs() << "Function: " << F.getName() << ": " << Pointers.size()
           << " pointers, " << Calls.size() << " call sites\n";

  // iterate over the worklist, and run the full (n^2)/2 disambiguations
  for (SetVector<Value *>::iterator I1 = Pointers.begin(), E = Pointers.end();
       I1 != E; ++I1) {
    auto I1Size = LocationSize::unknown();
    Type *I1ElTy = cast<PointerType>((*I1)->getType())->getElementType();
    if (I1ElTy->isSized())
      I1Size = LocationSize::precise(DL.getTypeStoreSize(I1ElTy));

    for (SetVector<Value *>::iterator I2 = Pointers.begin(); I2 != I1; ++I2) {
      auto I2Size = LocationSize::unknown();
      Type *I2ElTy = cast<PointerType>((*I2)->getType())->getElementType();
      if (I2ElTy->isSized())
        I2Size = LocationSize::precise(DL.getTypeStoreSize(I2ElTy));

      AliasResult AR = AA.alias(*I1, I1Size, *I2, I2Size);
      switch (AR) {
      case NoAlias:
        PrintResults(AR, PrintNoAlias, *I1, *I2, F.getParent());
        ++NoAliasCount;
        break;
      case MayAlias:
        PrintResults(AR, PrintMayAlias, *I1, *I2, F.getParent());
        ++MayAliasCount;
        break;
      case PartialAlias:
        PrintResults(AR, PrintPartialAlias, *I1, *I2, F.getParent());
        ++PartialAliasCount;
        break;
      case MustAlias:
        PrintResults(AR, PrintMustAlias, *I1, *I2, F.getParent());
        ++MustAliasCount;
        break;
      }
    }
  }

  if (EvalAAMD) {
    // iterate over all pairs of load, store
    for (Value *Load : Loads) {
      for (Value *Store : Stores) {
        AliasResult AR = AA.alias(MemoryLocation::get(cast<LoadInst>(Load)),
                                  MemoryLocation::get(cast<StoreInst>(Store)));
        switch (AR) {
        case NoAlias:
          PrintLoadStoreResults(AR, PrintNoAlias, Load, Store, F.getParent());
          ++NoAliasCount;
          break;
        case MayAlias:
          PrintLoadStoreResults(AR, PrintMayAlias, Load, Store, F.getParent());
          ++MayAliasCount;
          break;
        case PartialAlias:
          PrintLoadStoreResults(AR, PrintPartialAlias, Load, Store, F.getParent());
          ++PartialAliasCount;
          break;
        case MustAlias:
          PrintLoadStoreResults(AR, PrintMustAlias, Load, Store, F.getParent());
          ++MustAliasCount;
          break;
        }
      }
    }

    // iterate over all pairs of store, store
    for (SetVector<Value *>::iterator I1 = Stores.begin(), E = Stores.end();
         I1 != E; ++I1) {
      for (SetVector<Value *>::iterator I2 = Stores.begin(); I2 != I1; ++I2) {
        AliasResult AR = AA.alias(MemoryLocation::get(cast<StoreInst>(*I1)),
                                  MemoryLocation::get(cast<StoreInst>(*I2)));
        switch (AR) {
        case NoAlias:
          PrintLoadStoreResults(AR, PrintNoAlias, *I1, *I2, F.getParent());
          ++NoAliasCount;
          break;
        case MayAlias:
          PrintLoadStoreResults(AR, PrintMayAlias, *I1, *I2, F.getParent());
          ++MayAliasCount;
          break;
        case PartialAlias:
          PrintLoadStoreResults(AR, PrintPartialAlias, *I1, *I2, F.getParent());
          ++PartialAliasCount;
          break;
        case MustAlias:
          PrintLoadStoreResults(AR, PrintMustAlias, *I1, *I2, F.getParent());
          ++MustAliasCount;
          break;
        }
      }
    }
  }

  // Mod/ref alias analysis: compare all pairs of calls and values
  for (CallBase *Call : Calls) {
    for (auto Pointer : Pointers) {
      auto Size = LocationSize::unknown();
      Type *ElTy = cast<PointerType>(Pointer->getType())->getElementType();
      if (ElTy->isSized())
        Size = LocationSize::precise(DL.getTypeStoreSize(ElTy));

      switch (AA.getModRefInfo(Call, Pointer, Size)) {
      case ModRefInfo::NoModRef:
        PrintModRefResults("NoModRef", PrintNoModRef, Call, Pointer,
                           F.getParent());
        ++NoModRefCount;
        break;
      case ModRefInfo::Mod:
        PrintModRefResults("Just Mod", PrintMod, Call, Pointer, F.getParent());
        ++ModCount;
        break;
      case ModRefInfo::Ref:
        PrintModRefResults("Just Ref", PrintRef, Call, Pointer, F.getParent());
        ++RefCount;
        break;
      case ModRefInfo::ModRef:
        PrintModRefResults("Both ModRef", PrintModRef, Call, Pointer,
                           F.getParent());
        ++ModRefCount;
        break;
      case ModRefInfo::Must:
        PrintModRefResults("Must", PrintMust, Call, Pointer, F.getParent());
        ++MustCount;
        break;
      case ModRefInfo::MustMod:
        PrintModRefResults("Just Mod (MustAlias)", PrintMustMod, Call, Pointer,
                           F.getParent());
        ++MustModCount;
        break;
      case ModRefInfo::MustRef:
        PrintModRefResults("Just Ref (MustAlias)", PrintMustRef, Call, Pointer,
                           F.getParent());
        ++MustRefCount;
        break;
      case ModRefInfo::MustModRef:
        PrintModRefResults("Both ModRef (MustAlias)", PrintMustModRef, Call,
                           Pointer, F.getParent());
        ++MustModRefCount;
        break;
      }
    }
  }

  // Mod/ref alias analysis: compare all pairs of calls
  for (CallBase *CallA : Calls) {
    for (CallBase *CallB : Calls) {
      if (CallA == CallB)
        continue;
      switch (AA.getModRefInfo(CallA, CallB)) {
      case ModRefInfo::NoModRef:
        PrintModRefResults("NoModRef", PrintNoModRef, CallA, CallB,
                           F.getParent());
        ++NoModRefCount;
        break;
      case ModRefInfo::Mod:
        PrintModRefResults("Just Mod", PrintMod, CallA, CallB, F.getParent());
        ++ModCount;
        break;
      case ModRefInfo::Ref:
        PrintModRefResults("Just Ref", PrintRef, CallA, CallB, F.getParent());
        ++RefCount;
        break;
      case ModRefInfo::ModRef:
        PrintModRefResults("Both ModRef", PrintModRef, CallA, CallB,
                           F.getParent());
        ++ModRefCount;
        break;
      case ModRefInfo::Must:
        PrintModRefResults("Must", PrintMust, CallA, CallB, F.getParent());
        ++MustCount;
        break;
      case ModRefInfo::MustMod:
        PrintModRefResults("Just Mod (MustAlias)", PrintMustMod, CallA, CallB,
                           F.getParent());
        ++MustModCount;
        break;
      case ModRefInfo::MustRef:
        PrintModRefResults("Just Ref (MustAlias)", PrintMustRef, CallA, CallB,
                           F.getParent());
        ++MustRefCount;
        break;
      case ModRefInfo::MustModRef:
        PrintModRefResults("Both ModRef (MustAlias)", PrintMustModRef, CallA,
                           CallB, F.getParent());
        ++MustModRefCount;
        break;
      }
    }
  }
}

static void PrintPercent(int64_t Num, int64_t Sum) {
  errs() << "(" << Num * 100LL / Sum << "." << ((Num * 1000LL / Sum) % 10)
         << "%)\n";
}

AAEvaluator::~AAEvaluator() {
  if (FunctionCount == 0)
    return;

  int64_t AliasSum =
      NoAliasCount + MayAliasCount + PartialAliasCount + MustAliasCount;
  errs() << "===== Alias Analysis Evaluator Report =====\n";
  if (AliasSum == 0) {
    errs() << "  Alias Analysis Evaluator Summary: No pointers!\n";
  } else {
    errs() << "  " << AliasSum << " Total Alias Queries Performed\n";
    errs() << "  " << NoAliasCount << " no alias responses ";
    PrintPercent(NoAliasCount, AliasSum);
    errs() << "  " << MayAliasCount << " may alias responses ";
    PrintPercent(MayAliasCount, AliasSum);
    errs() << "  " << PartialAliasCount << " partial alias responses ";
    PrintPercent(PartialAliasCount, AliasSum);
    errs() << "  " << MustAliasCount << " must alias responses ";
    PrintPercent(MustAliasCount, AliasSum);
    errs() << "  Alias Analysis Evaluator Pointer Alias Summary: "
           << NoAliasCount * 100 / AliasSum << "%/"
           << MayAliasCount * 100 / AliasSum << "%/"
           << PartialAliasCount * 100 / AliasSum << "%/"
           << MustAliasCount * 100 / AliasSum << "%\n";
  }

  // Display the summary for mod/ref analysis
  int64_t ModRefSum = NoModRefCount + RefCount + ModCount + ModRefCount +
                      MustCount + MustRefCount + MustModCount + MustModRefCount;
  if (ModRefSum == 0) {
    errs() << "  Alias Analysis Mod/Ref Evaluator Summary: no "
              "mod/ref!\n";
  } else {
    errs() << "  " << ModRefSum << " Total ModRef Queries Performed\n";
    errs() << "  " << NoModRefCount << " no mod/ref responses ";
    PrintPercent(NoModRefCount, ModRefSum);
    errs() << "  " << ModCount << " mod responses ";
    PrintPercent(ModCount, ModRefSum);
    errs() << "  " << RefCount << " ref responses ";
    PrintPercent(RefCount, ModRefSum);
    errs() << "  " << ModRefCount << " mod & ref responses ";
    PrintPercent(ModRefCount, ModRefSum);
    errs() << "  " << MustCount << " must responses ";
    PrintPercent(MustCount, ModRefSum);
    errs() << "  " << MustModCount << " must mod responses ";
    PrintPercent(MustModCount, ModRefSum);
    errs() << "  " << MustRefCount << " must ref responses ";
    PrintPercent(MustRefCount, ModRefSum);
    errs() << "  " << MustModRefCount << " must mod & ref responses ";
    PrintPercent(MustModRefCount, ModRefSum);
    errs() << "  Alias Analysis Evaluator Mod/Ref Summary: "
           << NoModRefCount * 100 / ModRefSum << "%/"
           << ModCount * 100 / ModRefSum << "%/" << RefCount * 100 / ModRefSum
           << "%/" << ModRefCount * 100 / ModRefSum << "%/"
           << MustCount * 100 / ModRefSum << "%/"
           << MustRefCount * 100 / ModRefSum << "%/"
           << MustModCount * 100 / ModRefSum << "%/"
           << MustModRefCount * 100 / ModRefSum << "%\n";
  }
}

namespace llvm {
class AAEvalLegacyPass : public FunctionPass {
  std::unique_ptr<AAEvaluator> P;

public:
  static char ID; // Pass identification, replacement for typeid
  AAEvalLegacyPass() : FunctionPass(ID) {
    initializeAAEvalLegacyPassPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<AAResultsWrapperPass>();
    AU.setPreservesAll();
  }

  bool doInitialization(Module &M) override {
    P.reset(new AAEvaluator());
    return false;
  }

  bool runOnFunction(Function &F) override {
    P->runInternal(F, getAnalysis<AAResultsWrapperPass>().getAAResults());
    return false;
  }
  bool doFinalization(Module &M) override {
    P.reset();
    return false;
  }
};
}

char AAEvalLegacyPass::ID = 0;
INITIALIZE_PASS_BEGIN(AAEvalLegacyPass, "aa-eval",
                      "Exhaustive Alias Analysis Precision Evaluator", false,
                      true)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_END(AAEvalLegacyPass, "aa-eval",
                    "Exhaustive Alias Analysis Precision Evaluator", false,
                    true)

FunctionPass *llvm::createAAEvalPass() { return new AAEvalLegacyPass(); }
