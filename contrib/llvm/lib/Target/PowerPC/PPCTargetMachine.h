//===-- PPCTargetMachine.h - Define TargetMachine for PowerPC ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the PowerPC specific subclass of TargetMachine.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_POWERPC_PPCTARGETMACHINE_H
#define LLVM_LIB_TARGET_POWERPC_PPCTARGETMACHINE_H

#include "PPCInstrInfo.h"
#include "PPCSubtarget.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {

/// Common code between 32-bit and 64-bit PowerPC targets.
///
class PPCTargetMachine final : public LLVMTargetMachine {
public:
  enum PPCABI { PPC_ABI_UNKNOWN, PPC_ABI_ELFv1, PPC_ABI_ELFv2 };
private:
  std::unique_ptr<TargetLoweringObjectFile> TLOF;
  PPCABI TargetABI;

  mutable StringMap<std::unique_ptr<PPCSubtarget>> SubtargetMap;

public:
  PPCTargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                   StringRef FS, const TargetOptions &Options,
                   Optional<Reloc::Model> RM, Optional<CodeModel::Model> CM,
                   CodeGenOpt::Level OL, bool JIT);

  ~PPCTargetMachine() override;

  const PPCSubtarget *getSubtargetImpl(const Function &F) const override;
  // DO NOT IMPLEMENT: There is no such thing as a valid default subtarget,
  // subtargets are per-function entities based on the target-specific
  // attributes of each function.
  const PPCSubtarget *getSubtargetImpl() const = delete;

  // Pass Pipeline Configuration
  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;

  TargetTransformInfo getTargetTransformInfo(const Function &F) override;

  TargetLoweringObjectFile *getObjFileLowering() const override {
    return TLOF.get();
  }
  bool isELFv2ABI() const { return TargetABI == PPC_ABI_ELFv2; }
  bool isPPC64() const {
    const Triple &TT = getTargetTriple();
    return (TT.getArch() == Triple::ppc64 || TT.getArch() == Triple::ppc64le);
  };

  bool isMachineVerifierClean() const override {
    return false;
  }
};
} // end namespace llvm

#endif
