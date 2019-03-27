//===-- llvm/CodeGen/PseudoSourceValue.h ------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the PseudoSourceValue class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_PSEUDOSOURCEVALUE_H
#define LLVM_CODEGEN_PSEUDOSOURCEVALUE_H

#include "llvm/ADT/StringMap.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueMap.h"
#include <map>

namespace llvm {

class MachineFrameInfo;
class MachineMemOperand;
class raw_ostream;
class TargetInstrInfo;

raw_ostream &operator<<(raw_ostream &OS, const MachineMemOperand &MMO);
class PseudoSourceValue;
raw_ostream &operator<<(raw_ostream &OS, const PseudoSourceValue* PSV);

/// Special value supplied for machine level alias analysis. It indicates that
/// a memory access references the functions stack frame (e.g., a spill slot),
/// below the stack frame (e.g., argument space), or constant pool.
class PseudoSourceValue {
public:
  enum PSVKind : unsigned {
    Stack,
    GOT,
    JumpTable,
    ConstantPool,
    FixedStack,
    GlobalValueCallEntry,
    ExternalSymbolCallEntry,
    TargetCustom
  };

private:
  unsigned Kind;
  unsigned AddressSpace;
  friend raw_ostream &llvm::operator<<(raw_ostream &OS,
                                       const PseudoSourceValue* PSV);

  friend class MachineMemOperand; // For printCustom().

  /// Implement printing for PseudoSourceValue. This is called from
  /// Value::print or Value's operator<<.
  virtual void printCustom(raw_ostream &O) const;

public:
  explicit PseudoSourceValue(unsigned Kind, const TargetInstrInfo &TII);

  virtual ~PseudoSourceValue();

  unsigned kind() const { return Kind; }

  bool isStack() const { return Kind == Stack; }
  bool isGOT() const { return Kind == GOT; }
  bool isConstantPool() const { return Kind == ConstantPool; }
  bool isJumpTable() const { return Kind == JumpTable; }

  unsigned getAddressSpace() const { return AddressSpace; }

  unsigned getTargetCustom() const {
    return (Kind >= TargetCustom) ? ((Kind+1) - TargetCustom) : 0;
  }

  /// Test whether the memory pointed to by this PseudoSourceValue has a
  /// constant value.
  virtual bool isConstant(const MachineFrameInfo *) const;

  /// Test whether the memory pointed to by this PseudoSourceValue may also be
  /// pointed to by an LLVM IR Value.
  virtual bool isAliased(const MachineFrameInfo *) const;

  /// Return true if the memory pointed to by this PseudoSourceValue can ever
  /// alias an LLVM IR Value.
  virtual bool mayAlias(const MachineFrameInfo *) const;
};

/// A specialized PseudoSourceValue for holding FixedStack values, which must
/// include a frame index.
class FixedStackPseudoSourceValue : public PseudoSourceValue {
  const int FI;

public:
  explicit FixedStackPseudoSourceValue(int FI, const TargetInstrInfo &TII)
      : PseudoSourceValue(FixedStack, TII), FI(FI) {}

  static bool classof(const PseudoSourceValue *V) {
    return V->kind() == FixedStack;
  }

  bool isConstant(const MachineFrameInfo *MFI) const override;

  bool isAliased(const MachineFrameInfo *MFI) const override;

  bool mayAlias(const MachineFrameInfo *) const override;

  void printCustom(raw_ostream &OS) const override;

  int getFrameIndex() const { return FI; }
};

class CallEntryPseudoSourceValue : public PseudoSourceValue {
protected:
  CallEntryPseudoSourceValue(unsigned Kind, const TargetInstrInfo &TII);

public:
  bool isConstant(const MachineFrameInfo *) const override;
  bool isAliased(const MachineFrameInfo *) const override;
  bool mayAlias(const MachineFrameInfo *) const override;
};

/// A specialized pseudo soruce value for holding GlobalValue values.
class GlobalValuePseudoSourceValue : public CallEntryPseudoSourceValue {
  const GlobalValue *GV;

public:
  GlobalValuePseudoSourceValue(const GlobalValue *GV,
                               const TargetInstrInfo &TII);

  static bool classof(const PseudoSourceValue *V) {
    return V->kind() == GlobalValueCallEntry;
  }

  const GlobalValue *getValue() const { return GV; }
};

/// A specialized pseudo source value for holding external symbol values.
class ExternalSymbolPseudoSourceValue : public CallEntryPseudoSourceValue {
  const char *ES;

public:
  ExternalSymbolPseudoSourceValue(const char *ES, const TargetInstrInfo &TII);

  static bool classof(const PseudoSourceValue *V) {
    return V->kind() == ExternalSymbolCallEntry;
  }

  const char *getSymbol() const { return ES; }
};

/// Manages creation of pseudo source values.
class PseudoSourceValueManager {
  const TargetInstrInfo &TII;
  const PseudoSourceValue StackPSV, GOTPSV, JumpTablePSV, ConstantPoolPSV;
  std::map<int, std::unique_ptr<FixedStackPseudoSourceValue>> FSValues;
  StringMap<std::unique_ptr<const ExternalSymbolPseudoSourceValue>>
      ExternalCallEntries;
  ValueMap<const GlobalValue *,
           std::unique_ptr<const GlobalValuePseudoSourceValue>>
      GlobalCallEntries;

public:
  PseudoSourceValueManager(const TargetInstrInfo &TII);

  /// Return a pseudo source value referencing the area below the stack frame of
  /// a function, e.g., the argument space.
  const PseudoSourceValue *getStack();

  /// Return a pseudo source value referencing the global offset table
  /// (or something the like).
  const PseudoSourceValue *getGOT();

  /// Return a pseudo source value referencing the constant pool. Since constant
  /// pools are constant, this doesn't need to identify a specific constant
  /// pool entry.
  const PseudoSourceValue *getConstantPool();

  /// Return a pseudo source value referencing a jump table. Since jump tables
  /// are constant, this doesn't need to identify a specific jump table.
  const PseudoSourceValue *getJumpTable();

  /// Return a pseudo source value referencing a fixed stack frame entry,
  /// e.g., a spill slot.
  const PseudoSourceValue *getFixedStack(int FI);

  const PseudoSourceValue *getGlobalValueCallEntry(const GlobalValue *GV);

  const PseudoSourceValue *getExternalSymbolCallEntry(const char *ES);
};

} // end namespace llvm

#endif
