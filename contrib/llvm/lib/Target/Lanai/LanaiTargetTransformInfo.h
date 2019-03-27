//===-- LanaiTargetTransformInfo.h - Lanai specific TTI ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file a TargetTransformInfo::Concept conforming object specific to the
// Lanai target machine. It uses the target's detailed information to
// provide more precise answers to certain TTI queries, while letting the
// target independent and default TTI implementations handle the rest.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LANAI_LANAITARGETTRANSFORMINFO_H
#define LLVM_LIB_TARGET_LANAI_LANAITARGETTRANSFORMINFO_H

#include "Lanai.h"
#include "LanaiSubtarget.h"
#include "LanaiTargetMachine.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/BasicTTIImpl.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/Support/MathExtras.h"

namespace llvm {
class LanaiTTIImpl : public BasicTTIImplBase<LanaiTTIImpl> {
  typedef BasicTTIImplBase<LanaiTTIImpl> BaseT;
  typedef TargetTransformInfo TTI;
  friend BaseT;

  const LanaiSubtarget *ST;
  const LanaiTargetLowering *TLI;

  const LanaiSubtarget *getST() const { return ST; }
  const LanaiTargetLowering *getTLI() const { return TLI; }

public:
  explicit LanaiTTIImpl(const LanaiTargetMachine *TM, const Function &F)
      : BaseT(TM, F.getParent()->getDataLayout()), ST(TM->getSubtargetImpl(F)),
        TLI(ST->getTargetLowering()) {}

  bool shouldBuildLookupTables() const { return false; }

  TargetTransformInfo::PopcntSupportKind getPopcntSupport(unsigned TyWidth) {
    if (TyWidth == 32)
      return TTI::PSK_FastHardware;
    return TTI::PSK_Software;
  }

  int getIntImmCost(const APInt &Imm, Type *Ty) {
    assert(Ty->isIntegerTy());
    if (Imm == 0)
      return TTI::TCC_Free;
    if (isInt<16>(Imm.getSExtValue()))
      return TTI::TCC_Basic;
    if (isInt<21>(Imm.getZExtValue()))
      return TTI::TCC_Basic;
    if (isInt<32>(Imm.getSExtValue())) {
      if ((Imm.getSExtValue() & 0xFFFF) == 0)
        return TTI::TCC_Basic;
      return 2 * TTI::TCC_Basic;
    }

    return 4 * TTI::TCC_Basic;
  }

  int getIntImmCost(unsigned Opc, unsigned Idx, const APInt &Imm, Type *Ty) {
    return getIntImmCost(Imm, Ty);
  }

  int getIntImmCost(Intrinsic::ID IID, unsigned Idx, const APInt &Imm,
                    Type *Ty) {
    return getIntImmCost(Imm, Ty);
  }

  unsigned getArithmeticInstrCost(
      unsigned Opcode, Type *Ty,
      TTI::OperandValueKind Opd1Info = TTI::OK_AnyValue,
      TTI::OperandValueKind Opd2Info = TTI::OK_AnyValue,
      TTI::OperandValueProperties Opd1PropInfo = TTI::OP_None,
      TTI::OperandValueProperties Opd2PropInfo = TTI::OP_None,
      ArrayRef<const Value *> Args = ArrayRef<const Value *>()) {
    int ISD = TLI->InstructionOpcodeToISD(Opcode);

    switch (ISD) {
    default:
      return BaseT::getArithmeticInstrCost(Opcode, Ty, Opd1Info, Opd2Info,
                                           Opd1PropInfo, Opd2PropInfo);
    case ISD::MUL:
    case ISD::SDIV:
    case ISD::UDIV:
    case ISD::UREM:
      // This increases the cost associated with multiplication and division
      // to 64 times what the baseline arithmetic cost is. The arithmetic
      // instruction cost was arbitrarily chosen to reduce the desirability
      // of emitting arithmetic instructions that are emulated in software.
      // TODO: Investigate the performance impact given specialized lowerings.
      return 64 * BaseT::getArithmeticInstrCost(Opcode, Ty, Opd1Info, Opd2Info,
                                                Opd1PropInfo, Opd2PropInfo);
    }
  }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_LANAI_LANAITARGETTRANSFORMINFO_H
