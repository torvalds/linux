//===- ARCMCTargetDesc.cpp - ARC Target Descriptions ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides ARC specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "ARCMCTargetDesc.h"
#include "ARCMCAsmInfo.h"
#include "ARCTargetStreamer.h"
#include "InstPrinter/ARCInstPrinter.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#include "ARCGenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "ARCGenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "ARCGenRegisterInfo.inc"

static MCInstrInfo *createARCMCInstrInfo() {
  auto *X = new MCInstrInfo();
  InitARCMCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createARCMCRegisterInfo(const Triple &TT) {
  auto *X = new MCRegisterInfo();
  InitARCMCRegisterInfo(X, ARC::BLINK);
  return X;
}

static MCSubtargetInfo *createARCMCSubtargetInfo(const Triple &TT,
                                                 StringRef CPU, StringRef FS) {
  return createARCMCSubtargetInfoImpl(TT, CPU, FS);
}

static MCAsmInfo *createARCMCAsmInfo(const MCRegisterInfo &MRI,
                                     const Triple &TT) {
  MCAsmInfo *MAI = new ARCMCAsmInfo(TT);

  // Initial state of the frame pointer is SP.
  MCCFIInstruction Inst = MCCFIInstruction::createDefCfa(nullptr, ARC::SP, 0);
  MAI->addInitialFrameState(Inst);

  return MAI;
}

static MCInstPrinter *createARCMCInstPrinter(const Triple &T,
                                             unsigned SyntaxVariant,
                                             const MCAsmInfo &MAI,
                                             const MCInstrInfo &MII,
                                             const MCRegisterInfo &MRI) {
  return new ARCInstPrinter(MAI, MII, MRI);
}

ARCTargetStreamer::ARCTargetStreamer(MCStreamer &S) : MCTargetStreamer(S) {}
ARCTargetStreamer::~ARCTargetStreamer() = default;

static MCTargetStreamer *createTargetAsmStreamer(MCStreamer &S,
                                                 formatted_raw_ostream &OS,
                                                 MCInstPrinter *InstPrint,
                                                 bool isVerboseAsm) {
  return new ARCTargetStreamer(S);
}

// Force static initialization.
extern "C" void LLVMInitializeARCTargetMC() {
  // Register the MC asm info.
  Target &TheARCTarget = getTheARCTarget();
  RegisterMCAsmInfoFn X(TheARCTarget, createARCMCAsmInfo);

  // Register the MC instruction info.
  TargetRegistry::RegisterMCInstrInfo(TheARCTarget, createARCMCInstrInfo);

  // Register the MC register info.
  TargetRegistry::RegisterMCRegInfo(TheARCTarget, createARCMCRegisterInfo);

  // Register the MC subtarget info.
  TargetRegistry::RegisterMCSubtargetInfo(TheARCTarget,
                                          createARCMCSubtargetInfo);

  // Register the MCInstPrinter
  TargetRegistry::RegisterMCInstPrinter(TheARCTarget, createARCMCInstPrinter);

  TargetRegistry::RegisterAsmTargetStreamer(TheARCTarget,
                                            createTargetAsmStreamer);
}
