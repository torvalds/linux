//===- MIRPrintingPass.cpp - Pass that prints out using the MIR format ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements a pass that prints out the LLVM module using the MIR
// serialization format.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MIRPrinter.h"

#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

/// This pass prints out the LLVM IR to an output stream using the MIR
/// serialization format.
struct MIRPrintingPass : public MachineFunctionPass {
  static char ID;
  raw_ostream &OS;
  std::string MachineFunctions;

  MIRPrintingPass() : MachineFunctionPass(ID), OS(dbgs()) {}
  MIRPrintingPass(raw_ostream &OS) : MachineFunctionPass(ID), OS(OS) {}

  StringRef getPassName() const override { return "MIR Printing Pass"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override {
    std::string Str;
    raw_string_ostream StrOS(Str);
    printMIR(StrOS, MF);
    MachineFunctions.append(StrOS.str());
    return false;
  }

  bool doFinalization(Module &M) override {
    printMIR(OS, M);
    OS << MachineFunctions;
    return false;
  }
};

char MIRPrintingPass::ID = 0;

} // end anonymous namespace

char &llvm::MIRPrintingPassID = MIRPrintingPass::ID;
INITIALIZE_PASS(MIRPrintingPass, "mir-printer", "MIR Printer", false, false)

namespace llvm {

MachineFunctionPass *createPrintMIRPass(raw_ostream &OS) {
  return new MIRPrintingPass(OS);
}

} // end namespace llvm
