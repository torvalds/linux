//==- WebAssemblyTargetTransformInfo.h - WebAssembly-specific TTI -*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file a TargetTransformInfo::Concept conforming object specific
/// to the WebAssembly target machine.
///
/// It uses the target's detailed information to provide more precise answers to
/// certain TTI queries, while letting the target independent and default TTI
/// implementations handle the rest.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYTARGETTRANSFORMINFO_H
#define LLVM_LIB_TARGET_WEBASSEMBLY_WEBASSEMBLYTARGETTRANSFORMINFO_H

#include "WebAssemblyTargetMachine.h"
#include "llvm/CodeGen/BasicTTIImpl.h"
#include <algorithm>

namespace llvm {

class WebAssemblyTTIImpl final : public BasicTTIImplBase<WebAssemblyTTIImpl> {
  typedef BasicTTIImplBase<WebAssemblyTTIImpl> BaseT;
  typedef TargetTransformInfo TTI;
  friend BaseT;

  const WebAssemblySubtarget *ST;
  const WebAssemblyTargetLowering *TLI;

  const WebAssemblySubtarget *getST() const { return ST; }
  const WebAssemblyTargetLowering *getTLI() const { return TLI; }

public:
  WebAssemblyTTIImpl(const WebAssemblyTargetMachine *TM, const Function &F)
      : BaseT(TM, F.getParent()->getDataLayout()), ST(TM->getSubtargetImpl(F)),
        TLI(ST->getTargetLowering()) {}

  /// \name Scalar TTI Implementations
  /// @{

  // TODO: Implement more Scalar TTI for WebAssembly

  TTI::PopcntSupportKind getPopcntSupport(unsigned TyWidth) const;

  /// @}

  /// \name Vector TTI Implementations
  /// @{

  unsigned getNumberOfRegisters(bool Vector);
  unsigned getRegisterBitWidth(bool Vector) const;
  unsigned getArithmeticInstrCost(
      unsigned Opcode, Type *Ty,
      TTI::OperandValueKind Opd1Info = TTI::OK_AnyValue,
      TTI::OperandValueKind Opd2Info = TTI::OK_AnyValue,
      TTI::OperandValueProperties Opd1PropInfo = TTI::OP_None,
      TTI::OperandValueProperties Opd2PropInfo = TTI::OP_None,
      ArrayRef<const Value *> Args = ArrayRef<const Value *>());
  unsigned getVectorInstrCost(unsigned Opcode, Type *Val, unsigned Index);

  /// @}
};

} // end namespace llvm

#endif
