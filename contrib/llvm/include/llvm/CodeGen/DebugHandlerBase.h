//===-- llvm/CodeGen/DebugHandlerBase.h -----------------------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Common functionality for different debug information format backends.
// LLVM currently supports DWARF and CodeView.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_DEBUGHANDLERBASE_H
#define LLVM_CODEGEN_DEBUGHANDLERBASE_H

#include "llvm/ADT/Optional.h"
#include "llvm/CodeGen/AsmPrinterHandler.h"
#include "llvm/CodeGen/DbgEntityHistoryCalculator.h"
#include "llvm/CodeGen/LexicalScopes.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/IR/DebugInfoMetadata.h"

namespace llvm {

class AsmPrinter;
class MachineInstr;
class MachineModuleInfo;

/// Represents the location at which a variable is stored.
struct DbgVariableLocation {
  /// Base register.
  unsigned Register;

  /// Chain of offsetted loads necessary to load the value if it lives in
  /// memory. Every load except for the last is pointer-sized.
  SmallVector<int64_t, 1> LoadChain;

  /// Present if the location is part of a larger variable.
  llvm::Optional<llvm::DIExpression::FragmentInfo> FragmentInfo;

  /// Extract a VariableLocation from a MachineInstr.
  /// This will only work if Instruction is a debug value instruction
  /// and the associated DIExpression is in one of the supported forms.
  /// If these requirements are not met, the returned Optional will not
  /// have a value.
  static Optional<DbgVariableLocation>
  extractFromMachineInstruction(const MachineInstr &Instruction);
};

/// Base class for debug information backends. Common functionality related to
/// tracking which variables and scopes are alive at a given PC live here.
class DebugHandlerBase : public AsmPrinterHandler {
protected:
  DebugHandlerBase(AsmPrinter *A);

  /// Target of debug info emission.
  AsmPrinter *Asm;

  /// Collected machine module information.
  MachineModuleInfo *MMI;

  /// Previous instruction's location information. This is used to
  /// determine label location to indicate scope boundaries in debug info.
  /// We track the previous instruction's source location (if not line 0),
  /// whether it was a label, and its parent BB.
  DebugLoc PrevInstLoc;
  MCSymbol *PrevLabel = nullptr;
  const MachineBasicBlock *PrevInstBB = nullptr;

  /// This location indicates end of function prologue and beginning of
  /// function body.
  DebugLoc PrologEndLoc;

  /// If nonnull, stores the current machine instruction we're processing.
  const MachineInstr *CurMI = nullptr;

  LexicalScopes LScopes;

  /// History of DBG_VALUE and clobber instructions for each user
  /// variable.  Variables are listed in order of appearance.
  DbgValueHistoryMap DbgValues;

  /// Mapping of inlined labels and DBG_LABEL machine instruction.
  DbgLabelInstrMap DbgLabels;

  /// Maps instruction with label emitted before instruction.
  /// FIXME: Make this private from DwarfDebug, we have the necessary accessors
  /// for it.
  DenseMap<const MachineInstr *, MCSymbol *> LabelsBeforeInsn;

  /// Maps instruction with label emitted after instruction.
  DenseMap<const MachineInstr *, MCSymbol *> LabelsAfterInsn;

  /// Indentify instructions that are marking the beginning of or
  /// ending of a scope.
  void identifyScopeMarkers();

  /// Ensure that a label will be emitted before MI.
  void requestLabelBeforeInsn(const MachineInstr *MI) {
    LabelsBeforeInsn.insert(std::make_pair(MI, nullptr));
  }

  /// Ensure that a label will be emitted after MI.
  void requestLabelAfterInsn(const MachineInstr *MI) {
    LabelsAfterInsn.insert(std::make_pair(MI, nullptr));
  }

  virtual void beginFunctionImpl(const MachineFunction *MF) = 0;
  virtual void endFunctionImpl(const MachineFunction *MF) = 0;
  virtual void skippedNonDebugFunction() {}

  // AsmPrinterHandler overrides.
public:
  void beginInstruction(const MachineInstr *MI) override;
  void endInstruction() override;

  void beginFunction(const MachineFunction *MF) override;
  void endFunction(const MachineFunction *MF) override;

  /// Return Label preceding the instruction.
  MCSymbol *getLabelBeforeInsn(const MachineInstr *MI);

  /// Return Label immediately following the instruction.
  MCSymbol *getLabelAfterInsn(const MachineInstr *MI);

  /// Return the function-local offset of an instruction. A label for the
  /// instruction \p MI should exist (\ref getLabelAfterInsn).
  const MCExpr *getFunctionLocalOffsetAfterInsn(const MachineInstr *MI);

  /// If this type is derived from a base type then return base type size.
  static uint64_t getBaseTypeSize(const DITypeRef TyRef);
};

}

#endif
