//===- ProvenanceAnalysisEvaluator.cpp - ObjC ARC Optimization ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ProvenanceAnalysis.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::objcarc;

namespace {
class PAEval : public FunctionPass {

public:
  static char ID;
  PAEval();
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnFunction(Function &F) override;
};
}

char PAEval::ID = 0;
PAEval::PAEval() : FunctionPass(ID) {}

void PAEval::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<AAResultsWrapperPass>();
}

static StringRef getName(Value *V) {
  StringRef Name = V->getName();
  if (Name.startswith("\1"))
    return Name.substr(1);
  return Name;
}

static void insertIfNamed(SetVector<Value *> &Values, Value *V) {
  if (!V->hasName())
    return;
  Values.insert(V);
}

bool PAEval::runOnFunction(Function &F) {
  SetVector<Value *> Values;

  for (auto &Arg : F.args())
    insertIfNamed(Values, &Arg);

  for (auto I = inst_begin(F), E = inst_end(F); I != E; ++I) {
    insertIfNamed(Values, &*I);

    for (auto &Op : I->operands())
    insertIfNamed(Values, Op);
  }

  ProvenanceAnalysis PA;
  PA.setAA(&getAnalysis<AAResultsWrapperPass>().getAAResults());
  const DataLayout &DL = F.getParent()->getDataLayout();

  for (Value *V1 : Values) {
    StringRef NameV1 = getName(V1);
    for (Value *V2 : Values) {
      StringRef NameV2 = getName(V2);
      if (NameV1 >= NameV2)
        continue;
      errs() << NameV1 << " and " << NameV2;
      if (PA.related(V1, V2, DL))
        errs() << " are related.\n";
      else
        errs() << " are not related.\n";
    }
  }

  return false;
}

FunctionPass *llvm::createPAEvalPass() { return new PAEval(); }

INITIALIZE_PASS_BEGIN(PAEval, "pa-eval",
                      "Evaluate ProvenanceAnalysis on all pairs", false, true)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_END(PAEval, "pa-eval",
                    "Evaluate ProvenanceAnalysis on all pairs", false, true)
