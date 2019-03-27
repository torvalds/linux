//=-- HexagonTargetMachine.h - Define TargetMachine for Hexagon ---*- C++ -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the Hexagon specific subclass of TargetMachine.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_HEXAGONTARGETMACHINE_H
#define LLVM_LIB_TARGET_HEXAGON_HEXAGONTARGETMACHINE_H

#include "HexagonInstrInfo.h"
#include "HexagonSubtarget.h"
#include "HexagonTargetObjectFile.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {

class Module;

class HexagonTargetMachine : public LLVMTargetMachine {
  std::unique_ptr<TargetLoweringObjectFile> TLOF;
  mutable StringMap<std::unique_ptr<HexagonSubtarget>> SubtargetMap;

public:
  HexagonTargetMachine(const Target &T, const Triple &TT, StringRef CPU,
                       StringRef FS, const TargetOptions &Options,
                       Optional<Reloc::Model> RM, Optional<CodeModel::Model> CM,
                       CodeGenOpt::Level OL, bool JIT);
  ~HexagonTargetMachine() override;
  const HexagonSubtarget *getSubtargetImpl(const Function &F) const override;

  static unsigned getModuleMatchQuality(const Module &M);

  void adjustPassManager(PassManagerBuilder &PMB) override;
  TargetPassConfig *createPassConfig(PassManagerBase &PM) override;
  TargetTransformInfo getTargetTransformInfo(const Function &F) override;

  HexagonTargetObjectFile *getObjFileLowering() const override {
    return static_cast<HexagonTargetObjectFile*>(TLOF.get());
  }
};

} // end namespace llvm

#endif
