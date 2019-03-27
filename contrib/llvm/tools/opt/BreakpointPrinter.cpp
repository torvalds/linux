//===- BreakpointPrinter.cpp - Breakpoint location printer ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Breakpoint location printer.
///
//===----------------------------------------------------------------------===//
#include "BreakpointPrinter.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

struct BreakpointPrinter : public ModulePass {
  raw_ostream &Out;
  static char ID;

  BreakpointPrinter(raw_ostream &out) : ModulePass(ID), Out(out) {}

  void getContextName(const DIScope *Context, std::string &N) {
    if (auto *NS = dyn_cast<DINamespace>(Context)) {
      if (!NS->getName().empty()) {
        getContextName(NS->getScope(), N);
        N = N + NS->getName().str() + "::";
      }
    } else if (auto *TY = dyn_cast<DIType>(Context)) {
      if (!TY->getName().empty()) {
        getContextName(TY->getScope().resolve(), N);
        N = N + TY->getName().str() + "::";
      }
    }
  }

  bool runOnModule(Module &M) override {
    StringSet<> Processed;
    if (NamedMDNode *NMD = M.getNamedMetadata("llvm.dbg.sp"))
      for (unsigned i = 0, e = NMD->getNumOperands(); i != e; ++i) {
        std::string Name;
        auto *SP = cast_or_null<DISubprogram>(NMD->getOperand(i));
        if (!SP)
          continue;
        getContextName(SP->getScope().resolve(), Name);
        Name = Name + SP->getName().str();
        if (!Name.empty() && Processed.insert(Name).second) {
          Out << Name << "\n";
        }
      }
    return false;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }
};

char BreakpointPrinter::ID = 0;
}

ModulePass *llvm::createBreakpointPrinter(raw_ostream &out) {
  return new BreakpointPrinter(out);
}
