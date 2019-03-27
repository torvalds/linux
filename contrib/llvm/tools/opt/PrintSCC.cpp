//===- PrintSCC.cpp - Enumerate SCCs in some key graphs -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides passes to print out SCCs in a CFG or a CallGraph.
// Normally, you would not use these passes; instead, you would use the
// scc_iterator directly to enumerate SCCs and process them in some way.  These
// passes serve three purposes:
//
// (1) As a reference for how to use the scc_iterator.
// (2) To print out the SCCs for a CFG or a CallGraph:
//       analyze -print-cfg-sccs            to print the SCCs in each CFG of a module.
//       analyze -print-cfg-sccs -stats     to print the #SCCs and the maximum SCC size.
//       analyze -print-cfg-sccs -debug > /dev/null to watch the algorithm in action.
//
//     and similarly:
//       analyze -print-callgraph-sccs [-stats] [-debug] to print SCCs in the CallGraph
//
// (3) To test the scc_iterator.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SCCIterator.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

namespace {
  struct CFGSCC : public FunctionPass {
    static char ID;  // Pass identification, replacement for typeid
    CFGSCC() : FunctionPass(ID) {}
    bool runOnFunction(Function& func) override;

    void print(raw_ostream &O, const Module* = nullptr) const override { }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesAll();
    }
  };

  struct CallGraphSCC : public ModulePass {
    static char ID;  // Pass identification, replacement for typeid
    CallGraphSCC() : ModulePass(ID) {}

    // run - Print out SCCs in the call graph for the specified module.
    bool runOnModule(Module &M) override;

    void print(raw_ostream &O, const Module* = nullptr) const override { }

    // getAnalysisUsage - This pass requires the CallGraph.
    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesAll();
      AU.addRequired<CallGraphWrapperPass>();
    }
  };
}

char CFGSCC::ID = 0;
static RegisterPass<CFGSCC>
Y("print-cfg-sccs", "Print SCCs of each function CFG");

char CallGraphSCC::ID = 0;
static RegisterPass<CallGraphSCC>
Z("print-callgraph-sccs", "Print SCCs of the Call Graph");

bool CFGSCC::runOnFunction(Function &F) {
  unsigned sccNum = 0;
  errs() << "SCCs for Function " << F.getName() << " in PostOrder:";
  for (scc_iterator<Function*> SCCI = scc_begin(&F); !SCCI.isAtEnd(); ++SCCI) {
    const std::vector<BasicBlock *> &nextSCC = *SCCI;
    errs() << "\nSCC #" << ++sccNum << " : ";
    for (std::vector<BasicBlock*>::const_iterator I = nextSCC.begin(),
           E = nextSCC.end(); I != E; ++I)
      errs() << (*I)->getName() << ", ";
    if (nextSCC.size() == 1 && SCCI.hasLoop())
      errs() << " (Has self-loop).";
  }
  errs() << "\n";

  return true;
}


// run - Print out SCCs in the call graph for the specified module.
bool CallGraphSCC::runOnModule(Module &M) {
  CallGraph &CG = getAnalysis<CallGraphWrapperPass>().getCallGraph();
  unsigned sccNum = 0;
  errs() << "SCCs for the program in PostOrder:";
  for (scc_iterator<CallGraph*> SCCI = scc_begin(&CG); !SCCI.isAtEnd();
       ++SCCI) {
    const std::vector<CallGraphNode*> &nextSCC = *SCCI;
    errs() << "\nSCC #" << ++sccNum << " : ";
    for (std::vector<CallGraphNode*>::const_iterator I = nextSCC.begin(),
           E = nextSCC.end(); I != E; ++I)
      errs() << ((*I)->getFunction() ? (*I)->getFunction()->getName()
                                     : "external node") << ", ";
    if (nextSCC.size() == 1 && SCCI.hasLoop())
      errs() << " (Has self-loop).";
  }
  errs() << "\n";

  return true;
}
