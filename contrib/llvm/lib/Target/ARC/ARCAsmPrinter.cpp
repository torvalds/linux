//===- ARCAsmPrinter.cpp - ARC LLVM assembly writer -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains a printer that converts from our internal representation
// of machine-dependent LLVM code to GNU format ARC assembly language.
//
//===----------------------------------------------------------------------===//

#include "ARC.h"
#include "ARCInstrInfo.h"
#include "ARCMCInstLower.h"
#include "ARCSubtarget.h"
#include "ARCTargetMachine.h"
#include "ARCTargetStreamer.h"
#include "InstPrinter/ARCInstPrinter.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include <algorithm>

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

namespace {

class ARCAsmPrinter : public AsmPrinter {
  ARCMCInstLower MCInstLowering;
  ARCTargetStreamer &getTargetStreamer();

public:
  explicit ARCAsmPrinter(TargetMachine &TM,
                         std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer)),
        MCInstLowering(&OutContext, *this) {}

  StringRef getPassName() const override { return "ARC Assembly Printer"; }
  void EmitInstruction(const MachineInstr *MI) override;
};

} // end anonymous namespace

ARCTargetStreamer &ARCAsmPrinter::getTargetStreamer() {
  return static_cast<ARCTargetStreamer &>(*OutStreamer->getTargetStreamer());
}

void ARCAsmPrinter::EmitInstruction(const MachineInstr *MI) {
  SmallString<128> Str;
  raw_svector_ostream O(Str);

  switch (MI->getOpcode()) {
  case ARC::DBG_VALUE:
    llvm_unreachable("Should be handled target independently");
    break;
  }

  MCInst TmpInst;
  MCInstLowering.Lower(MI, TmpInst);
  EmitToStreamer(*OutStreamer, TmpInst);
}

// Force static initialization.
extern "C" void LLVMInitializeARCAsmPrinter() {
  RegisterAsmPrinter<ARCAsmPrinter> X(getTheARCTarget());
}
