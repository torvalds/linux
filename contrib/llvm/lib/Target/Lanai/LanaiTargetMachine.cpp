//===-- LanaiTargetMachine.cpp - Define TargetMachine for Lanai ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implements the info about Lanai target spec.
//
//===----------------------------------------------------------------------===//

#include "LanaiTargetMachine.h"

#include "Lanai.h"
#include "LanaiTargetObjectFile.h"
#include "LanaiTargetTransformInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Target/TargetOptions.h"

using namespace llvm;

namespace llvm {
void initializeLanaiMemAluCombinerPass(PassRegistry &);
} // namespace llvm

extern "C" void LLVMInitializeLanaiTarget() {
  // Register the target.
  RegisterTargetMachine<LanaiTargetMachine> registered_target(
      getTheLanaiTarget());
}

static std::string computeDataLayout() {
  // Data layout (keep in sync with clang/lib/Basic/Targets.cpp)
  return "E"        // Big endian
         "-m:e"     // ELF name manging
         "-p:32:32" // 32-bit pointers, 32 bit aligned
         "-i64:64"  // 64 bit integers, 64 bit aligned
         "-a:0:32"  // 32 bit alignment of objects of aggregate type
         "-n32"     // 32 bit native integer width
         "-S64";    // 64 bit natural stack alignment
}

static Reloc::Model getEffectiveRelocModel(Optional<Reloc::Model> RM) {
  if (!RM.hasValue())
    return Reloc::PIC_;
  return *RM;
}

LanaiTargetMachine::LanaiTargetMachine(const Target &T, const Triple &TT,
                                       StringRef Cpu, StringRef FeatureString,
                                       const TargetOptions &Options,
                                       Optional<Reloc::Model> RM,
                                       Optional<CodeModel::Model> CodeModel,
                                       CodeGenOpt::Level OptLevel, bool JIT)
    : LLVMTargetMachine(T, computeDataLayout(), TT, Cpu, FeatureString, Options,
                        getEffectiveRelocModel(RM),
                        getEffectiveCodeModel(CodeModel, CodeModel::Medium),
                        OptLevel),
      Subtarget(TT, Cpu, FeatureString, *this, Options, getCodeModel(),
                OptLevel),
      TLOF(new LanaiTargetObjectFile()) {
  initAsmInfo();
}

TargetTransformInfo
LanaiTargetMachine::getTargetTransformInfo(const Function &F) {
  return TargetTransformInfo(LanaiTTIImpl(this, F));
}

namespace {
// Lanai Code Generator Pass Configuration Options.
class LanaiPassConfig : public TargetPassConfig {
public:
  LanaiPassConfig(LanaiTargetMachine &TM, PassManagerBase *PassManager)
      : TargetPassConfig(TM, *PassManager) {}

  LanaiTargetMachine &getLanaiTargetMachine() const {
    return getTM<LanaiTargetMachine>();
  }

  bool addInstSelector() override;
  void addPreSched2() override;
  void addPreEmitPass() override;
};
} // namespace

TargetPassConfig *
LanaiTargetMachine::createPassConfig(PassManagerBase &PassManager) {
  return new LanaiPassConfig(*this, &PassManager);
}

// Install an instruction selector pass.
bool LanaiPassConfig::addInstSelector() {
  addPass(createLanaiISelDag(getLanaiTargetMachine()));
  return false;
}

// Implemented by targets that want to run passes immediately before
// machine code is emitted.
void LanaiPassConfig::addPreEmitPass() {
  addPass(createLanaiDelaySlotFillerPass(getLanaiTargetMachine()));
}

// Run passes after prolog-epilog insertion and before the second instruction
// scheduling pass.
void LanaiPassConfig::addPreSched2() {
  addPass(createLanaiMemAluCombinerPass());
}
