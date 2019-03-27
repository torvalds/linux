//===- llvm/CodeGen/GlobalISel/CombinerInfo.h ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// Interface for Targets to specify which operations are combined how and when.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_COMBINER_INFO_H
#define LLVM_CODEGEN_GLOBALISEL_COMBINER_INFO_H

#include <cassert>
namespace llvm {

class GISelChangeObserver;
class LegalizerInfo;
class MachineInstr;
class MachineIRBuilder;
class MachineRegisterInfo;

// Contains information relevant to enabling/disabling various combines for a
// pass.
class CombinerInfo {
public:
  CombinerInfo(bool AllowIllegalOps, bool ShouldLegalizeIllegal,
               LegalizerInfo *LInfo)
      : IllegalOpsAllowed(AllowIllegalOps),
        LegalizeIllegalOps(ShouldLegalizeIllegal), LInfo(LInfo) {
    assert(((AllowIllegalOps || !LegalizeIllegalOps) || LInfo) &&
           "Expecting legalizerInfo when illegalops not allowed");
  }
  virtual ~CombinerInfo() = default;
  /// If \p IllegalOpsAllowed is false, the CombinerHelper will make use of
  /// the legalizerInfo to check for legality before each transformation.
  bool IllegalOpsAllowed; // TODO: Make use of this.

  /// If \p LegalizeIllegalOps is true, the Combiner will also legalize the
  /// illegal ops that are created.
  bool LegalizeIllegalOps; // TODO: Make use of this.
  const LegalizerInfo *LInfo;

  /// Attempt to combine instructions using MI as the root.
  ///
  /// Use Observer to report the creation, modification, and erasure of
  /// instructions. GISelChangeObserver will automatically report certain
  /// kinds of operations. These operations are:
  /// * Instructions that are newly inserted into the MachineFunction
  /// * Instructions that are erased from the MachineFunction.
  ///
  /// However, it is important to report instruction modification and this is
  /// not automatic.
  virtual bool combine(GISelChangeObserver &Observer, MachineInstr &MI,
                       MachineIRBuilder &B) const = 0;
};
} // namespace llvm

#endif
