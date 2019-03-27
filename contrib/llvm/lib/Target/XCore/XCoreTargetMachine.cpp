//===-- XCoreTargetMachine.cpp - Define TargetMachine for XCore -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "XCoreTargetMachine.h"
#include "MCTargetDesc/XCoreMCTargetDesc.h"
#include "XCore.h"
#include "XCoreTargetObjectFile.h"
#include "XCoreTargetTransformInfo.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

static Reloc::Model getEffectiveRelocModel(Optional<Reloc::Model> RM) {
  if (!RM.hasValue())
    return Reloc::Static;
  return *RM;
}

static CodeModel::Model
getEffectiveXCoreCodeModel(Optional<CodeModel::Model> CM) {
  if (CM) {
    if (*CM != CodeModel::Small && *CM != CodeModel::Large)
      report_fatal_error("Target only supports CodeModel Small or Large");
    return *CM;
  }
  return CodeModel::Small;
}

/// Create an ILP32 architecture model
///
XCoreTargetMachine::XCoreTargetMachine(const Target &T, const Triple &TT,
                                       StringRef CPU, StringRef FS,
                                       const TargetOptions &Options,
                                       Optional<Reloc::Model> RM,
                                       Optional<CodeModel::Model> CM,
                                       CodeGenOpt::Level OL, bool JIT)
    : LLVMTargetMachine(
          T, "e-m:e-p:32:32-i1:8:32-i8:8:32-i16:16:32-i64:32-f64:32-a:0:32-n32",
          TT, CPU, FS, Options, getEffectiveRelocModel(RM),
          getEffectiveXCoreCodeModel(CM), OL),
      TLOF(llvm::make_unique<XCoreTargetObjectFile>()),
      Subtarget(TT, CPU, FS, *this) {
  initAsmInfo();
}

XCoreTargetMachine::~XCoreTargetMachine() = default;

namespace {

/// XCore Code Generator Pass Configuration Options.
class XCorePassConfig : public TargetPassConfig {
public:
  XCorePassConfig(XCoreTargetMachine &TM, PassManagerBase &PM)
    : TargetPassConfig(TM, PM) {}

  XCoreTargetMachine &getXCoreTargetMachine() const {
    return getTM<XCoreTargetMachine>();
  }

  void addIRPasses() override;
  bool addPreISel() override;
  bool addInstSelector() override;
  void addPreEmitPass() override;
};

} // end anonymous namespace

TargetPassConfig *XCoreTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new XCorePassConfig(*this, PM);
}

void XCorePassConfig::addIRPasses() {
  addPass(createAtomicExpandPass());

  TargetPassConfig::addIRPasses();
}

bool XCorePassConfig::addPreISel() {
  addPass(createXCoreLowerThreadLocalPass());
  return false;
}

bool XCorePassConfig::addInstSelector() {
  addPass(createXCoreISelDag(getXCoreTargetMachine(), getOptLevel()));
  return false;
}

void XCorePassConfig::addPreEmitPass() {
  addPass(createXCoreFrameToArgsOffsetEliminationPass(), false);
}

// Force static initialization.
extern "C" void LLVMInitializeXCoreTarget() {
  RegisterTargetMachine<XCoreTargetMachine> X(getTheXCoreTarget());
}

TargetTransformInfo
XCoreTargetMachine::getTargetTransformInfo(const Function &F) {
  return TargetTransformInfo(XCoreTTIImpl(this, F));
}
