//===-- llvm/CodeGen/GlobalISel/ConstantFoldingMIRBuilder.h  --*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements a version of MachineIRBuilder which does trivial
/// constant folding.
//===----------------------------------------------------------------------===//
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"

namespace llvm {

/// An MIRBuilder which does trivial constant folding of binary ops.
/// Calls to buildInstr will also try to constant fold binary ops.
class ConstantFoldingMIRBuilder : public MachineIRBuilder {
public:
  // Pull in base class constructors.
  using MachineIRBuilder::MachineIRBuilder;

  virtual ~ConstantFoldingMIRBuilder() = default;

  // Try to provide an overload for buildInstr for binary ops in order to
  // constant fold.
  MachineInstrBuilder buildInstr(unsigned Opc, ArrayRef<DstOp> DstOps,
                                 ArrayRef<SrcOp> SrcOps,
                                 Optional<unsigned> Flags = None) override {
    switch (Opc) {
    default:
      break;
    case TargetOpcode::G_ADD:
    case TargetOpcode::G_AND:
    case TargetOpcode::G_ASHR:
    case TargetOpcode::G_LSHR:
    case TargetOpcode::G_MUL:
    case TargetOpcode::G_OR:
    case TargetOpcode::G_SHL:
    case TargetOpcode::G_SUB:
    case TargetOpcode::G_XOR:
    case TargetOpcode::G_UDIV:
    case TargetOpcode::G_SDIV:
    case TargetOpcode::G_UREM:
    case TargetOpcode::G_SREM: {
      assert(DstOps.size() == 1 && "Invalid dst ops");
      assert(SrcOps.size() == 2 && "Invalid src ops");
      const DstOp &Dst = DstOps[0];
      const SrcOp &Src0 = SrcOps[0];
      const SrcOp &Src1 = SrcOps[1];
      if (auto MaybeCst =
              ConstantFoldBinOp(Opc, Src0.getReg(), Src1.getReg(), *getMRI()))
        return buildConstant(Dst, MaybeCst->getSExtValue());
      break;
    }
    }
    return MachineIRBuilder::buildInstr(Opc, DstOps, SrcOps);
  }
};
} // namespace llvm
