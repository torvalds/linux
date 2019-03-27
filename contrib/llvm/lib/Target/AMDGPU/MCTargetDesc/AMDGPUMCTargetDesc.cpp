//===-- AMDGPUMCTargetDesc.cpp - AMDGPU Target Descriptions ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file provides AMDGPU specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "AMDGPUMCTargetDesc.h"
#include "AMDGPUELFStreamer.h"
#include "AMDGPUMCAsmInfo.h"
#include "AMDGPUTargetStreamer.h"
#include "InstPrinter/AMDGPUInstPrinter.h"
#include "SIDefines.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MachineLocation.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#include "AMDGPUGenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "AMDGPUGenSubtargetInfo.inc"

#define NoSchedModel NoSchedModelR600
#define GET_SUBTARGETINFO_MC_DESC
#include "R600GenSubtargetInfo.inc"
#undef NoSchedModelR600

#define GET_REGINFO_MC_DESC
#include "AMDGPUGenRegisterInfo.inc"

#define GET_REGINFO_MC_DESC
#include "R600GenRegisterInfo.inc"

static MCInstrInfo *createAMDGPUMCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitAMDGPUMCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createAMDGPUMCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  if (TT.getArch() == Triple::r600)
    InitR600MCRegisterInfo(X, 0);
  else
    InitAMDGPUMCRegisterInfo(X, 0);
  return X;
}

static MCSubtargetInfo *
createAMDGPUMCSubtargetInfo(const Triple &TT, StringRef CPU, StringRef FS) {
  if (TT.getArch() == Triple::r600)
    return createR600MCSubtargetInfoImpl(TT, CPU, FS);
  return createAMDGPUMCSubtargetInfoImpl(TT, CPU, FS);
}

static MCInstPrinter *createAMDGPUMCInstPrinter(const Triple &T,
                                                unsigned SyntaxVariant,
                                                const MCAsmInfo &MAI,
                                                const MCInstrInfo &MII,
                                                const MCRegisterInfo &MRI) {
  if (T.getArch() == Triple::r600)
    return new R600InstPrinter(MAI, MII, MRI);
  else
    return new AMDGPUInstPrinter(MAI, MII, MRI);
}

static MCTargetStreamer *createAMDGPUAsmTargetStreamer(MCStreamer &S,
                                                      formatted_raw_ostream &OS,
                                                      MCInstPrinter *InstPrint,
                                                      bool isVerboseAsm) {
  return new AMDGPUTargetAsmStreamer(S, OS);
}

static MCTargetStreamer * createAMDGPUObjectTargetStreamer(
                                                   MCStreamer &S,
                                                   const MCSubtargetInfo &STI) {
  return new AMDGPUTargetELFStreamer(S, STI);
}

static MCStreamer *createMCStreamer(const Triple &T, MCContext &Context,
                                    std::unique_ptr<MCAsmBackend> &&MAB,
                                    std::unique_ptr<MCObjectWriter> &&OW,
                                    std::unique_ptr<MCCodeEmitter> &&Emitter,
                                    bool RelaxAll) {
  return createAMDGPUELFStreamer(T, Context, std::move(MAB), std::move(OW),
                                 std::move(Emitter), RelaxAll);
}

extern "C" void LLVMInitializeAMDGPUTargetMC() {

  TargetRegistry::RegisterMCInstrInfo(getTheGCNTarget(), createAMDGPUMCInstrInfo);
  TargetRegistry::RegisterMCInstrInfo(getTheAMDGPUTarget(), createR600MCInstrInfo);
  for (Target *T : {&getTheAMDGPUTarget(), &getTheGCNTarget()}) {
    RegisterMCAsmInfo<AMDGPUMCAsmInfo> X(*T);

    TargetRegistry::RegisterMCRegInfo(*T, createAMDGPUMCRegisterInfo);
    TargetRegistry::RegisterMCSubtargetInfo(*T, createAMDGPUMCSubtargetInfo);
    TargetRegistry::RegisterMCInstPrinter(*T, createAMDGPUMCInstPrinter);
    TargetRegistry::RegisterMCAsmBackend(*T, createAMDGPUAsmBackend);
    TargetRegistry::RegisterELFStreamer(*T, createMCStreamer);
  }

  // R600 specific registration
  TargetRegistry::RegisterMCCodeEmitter(getTheAMDGPUTarget(),
                                        createR600MCCodeEmitter);
  TargetRegistry::RegisterObjectTargetStreamer(
      getTheAMDGPUTarget(), createAMDGPUObjectTargetStreamer);

  // GCN specific registration
  TargetRegistry::RegisterMCCodeEmitter(getTheGCNTarget(),
                                        createSIMCCodeEmitter);

  TargetRegistry::RegisterAsmTargetStreamer(getTheGCNTarget(),
                                            createAMDGPUAsmTargetStreamer);
  TargetRegistry::RegisterObjectTargetStreamer(
      getTheGCNTarget(), createAMDGPUObjectTargetStreamer);
}
