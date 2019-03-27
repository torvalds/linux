//===-- NVPTXTargetMachine.h - Define TargetMachine for NVPTX ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the NVPTX specific subclass of TargetMachine.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_NVPTX_NVPTXTARGETMACHINE_H
#define LLVM_LIB_TARGET_NVPTX_NVPTXTARGETMACHINE_H

#include "ManagedStringPool.h"
#include "NVPTXSubtarget.h"
#include "llvm/CodeGen/SelectionDAGTargetInfo.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {

/// NVPTXTargetMachine
///
class NVPTXTargetMachine : public LLVMTargetMachine {
  bool is64bit;
  // Use 32-bit pointers for accessing const/local/short AS.
  bool UseShortPointers;
  std::unique_ptr<TargetLoweringObjectFile> TLOF;
  NVPTX::DrvInterface drvInterface;
  NVPTXSubtarget Subtarget;

  // Hold Strings that can be free'd all together with NVPTXTargetMachine
  ManagedStringPool ManagedStrPool;

public:
  NVPTXTargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                     StringRef FS, const TargetOptions &Options,
                     Optional<Reloc::Model> RM, Optional<CodeModel::Model> CM,
                     CodeGenOpt::Level OP, bool is64bit);

  ~NVPTXTargetMachine() override;
  const NVPTXSubtarget *getSubtargetImpl(const Function &) const override {
    return &Subtarget;
  }
  const NVPTXSubtarget *getSubtargetImpl() const { return &Subtarget; }
  bool is64Bit() const { return is64bit; }
  bool useShortPointers() const { return UseShortPointers; }
  NVPTX::DrvInterface getDrvInterface() const { return drvInterface; }
  ManagedStringPool *getManagedStrPool() const {
    return const_cast<ManagedStringPool *>(&ManagedStrPool);
  }

  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;

  // Emission of machine code through MCJIT is not supported.
  bool addPassesToEmitMC(PassManagerBase &, MCContext *&, raw_pwrite_stream &,
                         bool = true) override {
    return true;
  }
  TargetLoweringObjectFile *getObjFileLowering() const override {
    return TLOF.get();
  }

  void adjustPassManager(PassManagerBuilder &) override;

  TargetTransformInfo getTargetTransformInfo(const Function &F) override;

  bool isMachineVerifierClean() const override {
    return false;
  }
}; // NVPTXTargetMachine.

class NVPTXTargetMachine32 : public NVPTXTargetMachine {
  virtual void anchor();
public:
  NVPTXTargetMachine32(const Target &T, const Triple &TT, StringRef CPU,
                       StringRef FS, const TargetOptions &Options,
                       Optional<Reloc::Model> RM, Optional<CodeModel::Model> CM,
                       CodeGenOpt::Level OL, bool JIT);
};

class NVPTXTargetMachine64 : public NVPTXTargetMachine {
  virtual void anchor();
public:
  NVPTXTargetMachine64(const Target &T, const Triple &TT, StringRef CPU,
                       StringRef FS, const TargetOptions &Options,
                       Optional<Reloc::Model> RM, Optional<CodeModel::Model> CM,
                       CodeGenOpt::Level OL, bool JIT);
};

} // end namespace llvm

#endif
