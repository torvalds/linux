//===-- llvm/CodeGen/GlobalISel/CombinerHelper.h --------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===--------------------------------------------------------------------===//
//
/// This contains common combine transformations that may be used in a combine
/// pass,or by the target elsewhere.
/// Targets can pick individual opcode transformations from the helper or use
/// tryCombine which invokes all transformations. All of the transformations
/// return true if the MachineInstruction changed and false otherwise.
//
//===--------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_COMBINER_HELPER_H
#define LLVM_CODEGEN_GLOBALISEL_COMBINER_HELPER_H

namespace llvm {

class GISelChangeObserver;
class MachineIRBuilder;
class MachineRegisterInfo;
class MachineInstr;
class MachineOperand;

class CombinerHelper {
  MachineIRBuilder &Builder;
  MachineRegisterInfo &MRI;
  GISelChangeObserver &Observer;

public:
  CombinerHelper(GISelChangeObserver &Observer, MachineIRBuilder &B);

  /// MachineRegisterInfo::replaceRegWith() and inform the observer of the changes
  void replaceRegWith(MachineRegisterInfo &MRI, unsigned FromReg, unsigned ToReg) const;

  /// Replace a single register operand with a new register and inform the
  /// observer of the changes.
  void replaceRegOpWith(MachineRegisterInfo &MRI, MachineOperand &FromRegOp,
                        unsigned ToReg) const;

  /// If \p MI is COPY, try to combine it.
  /// Returns true if MI changed.
  bool tryCombineCopy(MachineInstr &MI);

  /// If \p MI is extend that consumes the result of a load, try to combine it.
  /// Returns true if MI changed.
  bool tryCombineExtendingLoads(MachineInstr &MI);

  /// Try to transform \p MI by using all of the above
  /// combine functions. Returns true if changed.
  bool tryCombine(MachineInstr &MI);
};
} // namespace llvm

#endif
