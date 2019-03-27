//===-- R600AsmPrinter.h - Print R600 assembly code -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// R600 Assembly printer class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_R600ASMPRINTER_H
#define LLVM_LIB_TARGET_AMDGPU_R600ASMPRINTER_H

#include "llvm/CodeGen/AsmPrinter.h"

namespace llvm {

class R600AsmPrinter final : public AsmPrinter {

public:
  explicit R600AsmPrinter(TargetMachine &TM,
                          std::unique_ptr<MCStreamer> Streamer);
  StringRef getPassName() const override;
  bool runOnMachineFunction(MachineFunction &MF) override;
  /// Implemented in AMDGPUMCInstLower.cpp
  void EmitInstruction(const MachineInstr *MI) override;
  /// Lower the specified LLVM Constant to an MCExpr.
  /// The AsmPrinter::lowerConstantof does not know how to lower
  /// addrspacecast, therefore they should be lowered by this function.
  const MCExpr *lowerConstant(const Constant *CV) override;

private:
  void EmitProgramInfoR600(const MachineFunction &MF);
};

AsmPrinter *
createR600AsmPrinterPass(TargetMachine &TM,
                         std::unique_ptr<MCStreamer> &&Streamer);

} // namespace llvm

#endif // LLVM_LIB_TARGET_AMDGPU_R600ASMPRINTER_H
