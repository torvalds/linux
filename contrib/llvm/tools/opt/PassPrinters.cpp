//===- PassPrinters.cpp - Utilities to print analysis info for passes -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Utilities to print analysis info for various kinds of passes.
///
//===----------------------------------------------------------------------===//

#include "PassPrinters.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/RegionInfo.h"
#include "llvm/Analysis/RegionPass.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include <string>

using namespace llvm;

namespace {

struct FunctionPassPrinter : public FunctionPass {
  const PassInfo *PassToPrint;
  raw_ostream &Out;
  static char ID;
  std::string PassName;
  bool QuietPass;

  FunctionPassPrinter(const PassInfo *PI, raw_ostream &out, bool Quiet)
      : FunctionPass(ID), PassToPrint(PI), Out(out), QuietPass(Quiet) {
    std::string PassToPrintName = PassToPrint->getPassName();
    PassName = "FunctionPass Printer: " + PassToPrintName;
  }

  bool runOnFunction(Function &F) override {
    if (!QuietPass)
      Out << "Printing analysis '" << PassToPrint->getPassName()
          << "' for function '" << F.getName() << "':\n";

    // Get and print pass...
    getAnalysisID<Pass>(PassToPrint->getTypeInfo()).print(Out, F.getParent());
    return false;
  }

  StringRef getPassName() const override { return PassName; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequiredID(PassToPrint->getTypeInfo());
    AU.setPreservesAll();
  }
};

char FunctionPassPrinter::ID = 0;

struct CallGraphSCCPassPrinter : public CallGraphSCCPass {
  static char ID;
  const PassInfo *PassToPrint;
  raw_ostream &Out;
  std::string PassName;
  bool QuietPass;

  CallGraphSCCPassPrinter(const PassInfo *PI, raw_ostream &out, bool Quiet)
      : CallGraphSCCPass(ID), PassToPrint(PI), Out(out), QuietPass(Quiet) {
    std::string PassToPrintName = PassToPrint->getPassName();
    PassName = "CallGraphSCCPass Printer: " + PassToPrintName;
  }

  bool runOnSCC(CallGraphSCC &SCC) override {
    if (!QuietPass)
      Out << "Printing analysis '" << PassToPrint->getPassName() << "':\n";

    // Get and print pass...
    for (CallGraphSCC::iterator I = SCC.begin(), E = SCC.end(); I != E; ++I) {
      Function *F = (*I)->getFunction();
      if (F)
        getAnalysisID<Pass>(PassToPrint->getTypeInfo())
            .print(Out, F->getParent());
    }
    return false;
  }

  StringRef getPassName() const override { return PassName; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequiredID(PassToPrint->getTypeInfo());
    AU.setPreservesAll();
  }
};

char CallGraphSCCPassPrinter::ID = 0;

struct ModulePassPrinter : public ModulePass {
  static char ID;
  const PassInfo *PassToPrint;
  raw_ostream &Out;
  std::string PassName;
  bool QuietPass;

  ModulePassPrinter(const PassInfo *PI, raw_ostream &out, bool Quiet)
      : ModulePass(ID), PassToPrint(PI), Out(out), QuietPass(Quiet) {
    std::string PassToPrintName = PassToPrint->getPassName();
    PassName = "ModulePass Printer: " + PassToPrintName;
  }

  bool runOnModule(Module &M) override {
    if (!QuietPass)
      Out << "Printing analysis '" << PassToPrint->getPassName() << "':\n";

    // Get and print pass...
    getAnalysisID<Pass>(PassToPrint->getTypeInfo()).print(Out, &M);
    return false;
  }

  StringRef getPassName() const override { return PassName; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequiredID(PassToPrint->getTypeInfo());
    AU.setPreservesAll();
  }
};

char ModulePassPrinter::ID = 0;

struct LoopPassPrinter : public LoopPass {
  static char ID;
  const PassInfo *PassToPrint;
  raw_ostream &Out;
  std::string PassName;
  bool QuietPass;

  LoopPassPrinter(const PassInfo *PI, raw_ostream &out, bool Quiet)
      : LoopPass(ID), PassToPrint(PI), Out(out), QuietPass(Quiet) {
    std::string PassToPrintName = PassToPrint->getPassName();
    PassName = "LoopPass Printer: " + PassToPrintName;
  }

  bool runOnLoop(Loop *L, LPPassManager &LPM) override {
    if (!QuietPass)
      Out << "Printing analysis '" << PassToPrint->getPassName() << "':\n";

    // Get and print pass...
    getAnalysisID<Pass>(PassToPrint->getTypeInfo())
        .print(Out, L->getHeader()->getParent()->getParent());
    return false;
  }

  StringRef getPassName() const override { return PassName; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequiredID(PassToPrint->getTypeInfo());
    AU.setPreservesAll();
  }
};

char LoopPassPrinter::ID = 0;

struct RegionPassPrinter : public RegionPass {
  static char ID;
  const PassInfo *PassToPrint;
  raw_ostream &Out;
  std::string PassName;
  bool QuietPass;

  RegionPassPrinter(const PassInfo *PI, raw_ostream &out, bool Quiet)
      : RegionPass(ID), PassToPrint(PI), Out(out), QuietPass(Quiet) {
    std::string PassToPrintName = PassToPrint->getPassName();
    PassName = "RegionPass Printer: " + PassToPrintName;
  }

  bool runOnRegion(Region *R, RGPassManager &RGM) override {
    if (!QuietPass) {
      Out << "Printing analysis '" << PassToPrint->getPassName() << "' for "
          << "region: '" << R->getNameStr() << "' in function '"
          << R->getEntry()->getParent()->getName() << "':\n";
    }
    // Get and print pass...
    getAnalysisID<Pass>(PassToPrint->getTypeInfo())
        .print(Out, R->getEntry()->getParent()->getParent());
    return false;
  }

  StringRef getPassName() const override { return PassName; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequiredID(PassToPrint->getTypeInfo());
    AU.setPreservesAll();
  }
};

char RegionPassPrinter::ID = 0;

struct BasicBlockPassPrinter : public BasicBlockPass {
  const PassInfo *PassToPrint;
  raw_ostream &Out;
  static char ID;
  std::string PassName;
  bool QuietPass;

  BasicBlockPassPrinter(const PassInfo *PI, raw_ostream &out, bool Quiet)
      : BasicBlockPass(ID), PassToPrint(PI), Out(out), QuietPass(Quiet) {
    std::string PassToPrintName = PassToPrint->getPassName();
    PassName = "BasicBlockPass Printer: " + PassToPrintName;
  }

  bool runOnBasicBlock(BasicBlock &BB) override {
    if (!QuietPass)
      Out << "Printing Analysis info for BasicBlock '" << BB.getName()
          << "': Pass " << PassToPrint->getPassName() << ":\n";

    // Get and print pass...
    getAnalysisID<Pass>(PassToPrint->getTypeInfo())
        .print(Out, BB.getParent()->getParent());
    return false;
  }

  StringRef getPassName() const override { return PassName; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequiredID(PassToPrint->getTypeInfo());
    AU.setPreservesAll();
  }
};

char BasicBlockPassPrinter::ID = 0;

} // end anonymous namespace

FunctionPass *llvm::createFunctionPassPrinter(const PassInfo *PI,
                                              raw_ostream &OS, bool Quiet) {
  return new FunctionPassPrinter(PI, OS, Quiet);
}

CallGraphSCCPass *llvm::createCallGraphPassPrinter(const PassInfo *PI,
                                                   raw_ostream &OS,
                                                   bool Quiet) {
  return new CallGraphSCCPassPrinter(PI, OS, Quiet);
}

ModulePass *llvm::createModulePassPrinter(const PassInfo *PI, raw_ostream &OS,
                                          bool Quiet) {
  return new ModulePassPrinter(PI, OS, Quiet);
}

LoopPass *llvm::createLoopPassPrinter(const PassInfo *PI, raw_ostream &OS,
                                      bool Quiet) {
  return new LoopPassPrinter(PI, OS, Quiet);
}

RegionPass *llvm::createRegionPassPrinter(const PassInfo *PI, raw_ostream &OS,
                                          bool Quiet) {
  return new RegionPassPrinter(PI, OS, Quiet);
}

BasicBlockPass *llvm::createBasicBlockPassPrinter(const PassInfo *PI,
                                                  raw_ostream &OS, bool Quiet) {
  return new BasicBlockPassPrinter(PI, OS, Quiet);
}
