//===- ARCTargetMachine.h - Define TargetMachine for ARC --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the ARC specific subclass of TargetMachine.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARC_ARCTARGETMACHINE_H
#define LLVM_LIB_TARGET_ARC_ARCTARGETMACHINE_H

#include "ARCSubtarget.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {

class TargetPassConfig;

class ARCTargetMachine : public LLVMTargetMachine {
  std::unique_ptr<TargetLoweringObjectFile> TLOF;
  ARCSubtarget Subtarget;

public:
  ARCTargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                   StringRef FS, const TargetOptions &Options,
                   Optional<Reloc::Model> RM, Optional<CodeModel::Model> CM,
                   CodeGenOpt::Level OL, bool JIT);
  ~ARCTargetMachine() override;

  const ARCSubtarget *getSubtargetImpl() const { return &Subtarget; }
  const ARCSubtarget *getSubtargetImpl(const Function &) const override {
    return &Subtarget;
  }

  // Pass Pipeline Configuration
  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;

  TargetTransformInfo getTargetTransformInfo(const Function &F) override;
  TargetLoweringObjectFile *getObjFileLowering() const override {
    return TLOF.get();
  }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_ARC_ARCTARGETMACHINE_H
