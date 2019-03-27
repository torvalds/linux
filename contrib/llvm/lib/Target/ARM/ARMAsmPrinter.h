//===-- ARMAsmPrinter.h - ARM implementation of AsmPrinter ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_ARMASMPRINTER_H
#define LLVM_LIB_TARGET_ARM_ARMASMPRINTER_H

#include "ARMSubtarget.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {

class ARMFunctionInfo;
class MCOperand;
class MachineConstantPool;
class MachineOperand;
class MCSymbol;

namespace ARM {
  enum DW_ISA {
    DW_ISA_ARM_thumb = 1,
    DW_ISA_ARM_arm = 2
  };
}

class LLVM_LIBRARY_VISIBILITY ARMAsmPrinter : public AsmPrinter {

  /// Subtarget - Keep a pointer to the ARMSubtarget around so that we can
  /// make the right decision when printing asm code for different targets.
  const ARMSubtarget *Subtarget;

  /// AFI - Keep a pointer to ARMFunctionInfo for the current
  /// MachineFunction.
  ARMFunctionInfo *AFI;

  /// MCP - Keep a pointer to constantpool entries of the current
  /// MachineFunction.
  const MachineConstantPool *MCP;

  /// InConstantPool - Maintain state when emitting a sequence of constant
  /// pool entries so we can properly mark them as data regions.
  bool InConstantPool;

  /// ThumbIndirectPads - These maintain a per-function list of jump pad
  /// labels used for ARMv4t thumb code to make register indirect calls.
  SmallVector<std::pair<unsigned, MCSymbol*>, 4> ThumbIndirectPads;

  /// OptimizationGoals - Maintain a combined optimization goal for all
  /// functions in a module: one of Tag_ABI_optimization_goals values,
  /// -1 if uninitialized, 0 if conflicting goals
  int OptimizationGoals;

  /// List of globals that have had their storage promoted to a constant
  /// pool. This lives between calls to runOnMachineFunction and collects
  /// data from every MachineFunction. It is used during doFinalization
  /// when all non-function globals are emitted.
  SmallPtrSet<const GlobalVariable*,2> PromotedGlobals;
  /// Set of globals in PromotedGlobals that we've emitted labels for.
  /// We need to emit labels even for promoted globals so that DWARF
  /// debug info can link properly.
  SmallPtrSet<const GlobalVariable*,2> EmittedPromotedGlobalLabels;

public:
  explicit ARMAsmPrinter(TargetMachine &TM,
                         std::unique_ptr<MCStreamer> Streamer);

  StringRef getPassName() const override {
    return "ARM Assembly Printer";
  }

  void printOperand(const MachineInstr *MI, int OpNum, raw_ostream &O);

  bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNum,
                       unsigned AsmVariant, const char *ExtraCode,
                       raw_ostream &O) override;
  bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNum,
                             unsigned AsmVariant, const char *ExtraCode,
                             raw_ostream &O) override;

  void emitInlineAsmEnd(const MCSubtargetInfo &StartInfo,
                        const MCSubtargetInfo *EndInfo) const override;

  void EmitJumpTableAddrs(const MachineInstr *MI);
  void EmitJumpTableInsts(const MachineInstr *MI);
  void EmitJumpTableTBInst(const MachineInstr *MI, unsigned OffsetWidth);
  void EmitInstruction(const MachineInstr *MI) override;
  bool runOnMachineFunction(MachineFunction &F) override;

  void EmitConstantPool() override {
    // we emit constant pools customly!
  }
  void EmitFunctionBodyEnd() override;
  void EmitFunctionEntryLabel() override;
  void EmitStartOfAsmFile(Module &M) override;
  void EmitEndOfAsmFile(Module &M) override;
  void EmitXXStructor(const DataLayout &DL, const Constant *CV) override;
  void EmitGlobalVariable(const GlobalVariable *GV) override;

  MCSymbol *GetCPISymbol(unsigned CPID) const override;

  // lowerOperand - Convert a MachineOperand into the equivalent MCOperand.
  bool lowerOperand(const MachineOperand &MO, MCOperand &MCOp);

  //===------------------------------------------------------------------===//
  // XRay implementation
  //===------------------------------------------------------------------===//
public:
  // XRay-specific lowering for ARM.
  void LowerPATCHABLE_FUNCTION_ENTER(const MachineInstr &MI);
  void LowerPATCHABLE_FUNCTION_EXIT(const MachineInstr &MI);
  void LowerPATCHABLE_TAIL_CALL(const MachineInstr &MI);

private:
  void EmitSled(const MachineInstr &MI, SledKind Kind);

  // Helpers for EmitStartOfAsmFile() and EmitEndOfAsmFile()
  void emitAttributes();

  // Generic helper used to emit e.g. ARMv5 mul pseudos
  void EmitPatchedInstruction(const MachineInstr *MI, unsigned TargetOpc);

  void EmitUnwindingInstruction(const MachineInstr *MI);

  // emitPseudoExpansionLowering - tblgen'erated.
  bool emitPseudoExpansionLowering(MCStreamer &OutStreamer,
                                   const MachineInstr *MI);

public:
  unsigned getISAEncoding() override {
    // ARM/Darwin adds ISA to the DWARF info for each function.
    const Triple &TT = TM.getTargetTriple();
    if (!TT.isOSBinFormatMachO())
      return 0;
    bool isThumb = TT.isThumb() ||
                   TT.getSubArch() == Triple::ARMSubArch_v7m ||
                   TT.getSubArch() == Triple::ARMSubArch_v6m;
    return isThumb ? ARM::DW_ISA_ARM_thumb : ARM::DW_ISA_ARM_arm;
  }

private:
  MCOperand GetSymbolRef(const MachineOperand &MO, const MCSymbol *Symbol);
  MCSymbol *GetARMJTIPICJumpTableLabel(unsigned uid) const;

  MCSymbol *GetARMGVSymbol(const GlobalValue *GV, unsigned char TargetFlags);

public:
  /// EmitMachineConstantPoolValue - Print a machine constantpool value to
  /// the .s file.
  void EmitMachineConstantPoolValue(MachineConstantPoolValue *MCPV) override;
};
} // end namespace llvm

#endif
