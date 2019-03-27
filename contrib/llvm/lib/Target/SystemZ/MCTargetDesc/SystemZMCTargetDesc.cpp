//===-- SystemZMCTargetDesc.cpp - SystemZ target descriptions -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "SystemZMCTargetDesc.h"
#include "InstPrinter/SystemZInstPrinter.h"
#include "SystemZMCAsmInfo.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#include "SystemZGenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "SystemZGenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "SystemZGenRegisterInfo.inc"

const unsigned SystemZMC::GR32Regs[16] = {
  SystemZ::R0L, SystemZ::R1L, SystemZ::R2L, SystemZ::R3L,
  SystemZ::R4L, SystemZ::R5L, SystemZ::R6L, SystemZ::R7L,
  SystemZ::R8L, SystemZ::R9L, SystemZ::R10L, SystemZ::R11L,
  SystemZ::R12L, SystemZ::R13L, SystemZ::R14L, SystemZ::R15L
};

const unsigned SystemZMC::GRH32Regs[16] = {
  SystemZ::R0H, SystemZ::R1H, SystemZ::R2H, SystemZ::R3H,
  SystemZ::R4H, SystemZ::R5H, SystemZ::R6H, SystemZ::R7H,
  SystemZ::R8H, SystemZ::R9H, SystemZ::R10H, SystemZ::R11H,
  SystemZ::R12H, SystemZ::R13H, SystemZ::R14H, SystemZ::R15H
};

const unsigned SystemZMC::GR64Regs[16] = {
  SystemZ::R0D, SystemZ::R1D, SystemZ::R2D, SystemZ::R3D,
  SystemZ::R4D, SystemZ::R5D, SystemZ::R6D, SystemZ::R7D,
  SystemZ::R8D, SystemZ::R9D, SystemZ::R10D, SystemZ::R11D,
  SystemZ::R12D, SystemZ::R13D, SystemZ::R14D, SystemZ::R15D
};

const unsigned SystemZMC::GR128Regs[16] = {
  SystemZ::R0Q, 0, SystemZ::R2Q, 0,
  SystemZ::R4Q, 0, SystemZ::R6Q, 0,
  SystemZ::R8Q, 0, SystemZ::R10Q, 0,
  SystemZ::R12Q, 0, SystemZ::R14Q, 0
};

const unsigned SystemZMC::FP32Regs[16] = {
  SystemZ::F0S, SystemZ::F1S, SystemZ::F2S, SystemZ::F3S,
  SystemZ::F4S, SystemZ::F5S, SystemZ::F6S, SystemZ::F7S,
  SystemZ::F8S, SystemZ::F9S, SystemZ::F10S, SystemZ::F11S,
  SystemZ::F12S, SystemZ::F13S, SystemZ::F14S, SystemZ::F15S
};

const unsigned SystemZMC::FP64Regs[16] = {
  SystemZ::F0D, SystemZ::F1D, SystemZ::F2D, SystemZ::F3D,
  SystemZ::F4D, SystemZ::F5D, SystemZ::F6D, SystemZ::F7D,
  SystemZ::F8D, SystemZ::F9D, SystemZ::F10D, SystemZ::F11D,
  SystemZ::F12D, SystemZ::F13D, SystemZ::F14D, SystemZ::F15D
};

const unsigned SystemZMC::FP128Regs[16] = {
  SystemZ::F0Q, SystemZ::F1Q, 0, 0,
  SystemZ::F4Q, SystemZ::F5Q, 0, 0,
  SystemZ::F8Q, SystemZ::F9Q, 0, 0,
  SystemZ::F12Q, SystemZ::F13Q, 0, 0
};

const unsigned SystemZMC::VR32Regs[32] = {
  SystemZ::F0S, SystemZ::F1S, SystemZ::F2S, SystemZ::F3S,
  SystemZ::F4S, SystemZ::F5S, SystemZ::F6S, SystemZ::F7S,
  SystemZ::F8S, SystemZ::F9S, SystemZ::F10S, SystemZ::F11S,
  SystemZ::F12S, SystemZ::F13S, SystemZ::F14S, SystemZ::F15S,
  SystemZ::F16S, SystemZ::F17S, SystemZ::F18S, SystemZ::F19S,
  SystemZ::F20S, SystemZ::F21S, SystemZ::F22S, SystemZ::F23S,
  SystemZ::F24S, SystemZ::F25S, SystemZ::F26S, SystemZ::F27S,
  SystemZ::F28S, SystemZ::F29S, SystemZ::F30S, SystemZ::F31S
};

const unsigned SystemZMC::VR64Regs[32] = {
  SystemZ::F0D, SystemZ::F1D, SystemZ::F2D, SystemZ::F3D,
  SystemZ::F4D, SystemZ::F5D, SystemZ::F6D, SystemZ::F7D,
  SystemZ::F8D, SystemZ::F9D, SystemZ::F10D, SystemZ::F11D,
  SystemZ::F12D, SystemZ::F13D, SystemZ::F14D, SystemZ::F15D,
  SystemZ::F16D, SystemZ::F17D, SystemZ::F18D, SystemZ::F19D,
  SystemZ::F20D, SystemZ::F21D, SystemZ::F22D, SystemZ::F23D,
  SystemZ::F24D, SystemZ::F25D, SystemZ::F26D, SystemZ::F27D,
  SystemZ::F28D, SystemZ::F29D, SystemZ::F30D, SystemZ::F31D
};

const unsigned SystemZMC::VR128Regs[32] = {
  SystemZ::V0, SystemZ::V1, SystemZ::V2, SystemZ::V3,
  SystemZ::V4, SystemZ::V5, SystemZ::V6, SystemZ::V7,
  SystemZ::V8, SystemZ::V9, SystemZ::V10, SystemZ::V11,
  SystemZ::V12, SystemZ::V13, SystemZ::V14, SystemZ::V15,
  SystemZ::V16, SystemZ::V17, SystemZ::V18, SystemZ::V19,
  SystemZ::V20, SystemZ::V21, SystemZ::V22, SystemZ::V23,
  SystemZ::V24, SystemZ::V25, SystemZ::V26, SystemZ::V27,
  SystemZ::V28, SystemZ::V29, SystemZ::V30, SystemZ::V31
};

const unsigned SystemZMC::AR32Regs[16] = {
  SystemZ::A0, SystemZ::A1, SystemZ::A2, SystemZ::A3,
  SystemZ::A4, SystemZ::A5, SystemZ::A6, SystemZ::A7,
  SystemZ::A8, SystemZ::A9, SystemZ::A10, SystemZ::A11,
  SystemZ::A12, SystemZ::A13, SystemZ::A14, SystemZ::A15
};

const unsigned SystemZMC::CR64Regs[16] = {
  SystemZ::C0, SystemZ::C1, SystemZ::C2, SystemZ::C3,
  SystemZ::C4, SystemZ::C5, SystemZ::C6, SystemZ::C7,
  SystemZ::C8, SystemZ::C9, SystemZ::C10, SystemZ::C11,
  SystemZ::C12, SystemZ::C13, SystemZ::C14, SystemZ::C15
};

unsigned SystemZMC::getFirstReg(unsigned Reg) {
  static unsigned Map[SystemZ::NUM_TARGET_REGS];
  static bool Initialized = false;
  if (!Initialized) {
    for (unsigned I = 0; I < 16; ++I) {
      Map[GR32Regs[I]] = I;
      Map[GRH32Regs[I]] = I;
      Map[GR64Regs[I]] = I;
      Map[GR128Regs[I]] = I;
      Map[FP128Regs[I]] = I;
      Map[AR32Regs[I]] = I;
    }
    for (unsigned I = 0; I < 32; ++I) {
      Map[VR32Regs[I]] = I;
      Map[VR64Regs[I]] = I;
      Map[VR128Regs[I]] = I;
    }
  }
  assert(Reg < SystemZ::NUM_TARGET_REGS);
  return Map[Reg];
}

static MCAsmInfo *createSystemZMCAsmInfo(const MCRegisterInfo &MRI,
                                         const Triple &TT) {
  MCAsmInfo *MAI = new SystemZMCAsmInfo(TT);
  MCCFIInstruction Inst =
      MCCFIInstruction::createDefCfa(nullptr,
                                     MRI.getDwarfRegNum(SystemZ::R15D, true),
                                     SystemZMC::CFAOffsetFromInitialSP);
  MAI->addInitialFrameState(Inst);
  return MAI;
}

static MCInstrInfo *createSystemZMCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitSystemZMCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createSystemZMCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitSystemZMCRegisterInfo(X, SystemZ::R14D);
  return X;
}

static MCSubtargetInfo *
createSystemZMCSubtargetInfo(const Triple &TT, StringRef CPU, StringRef FS) {
  return createSystemZMCSubtargetInfoImpl(TT, CPU, FS);
}

static MCInstPrinter *createSystemZMCInstPrinter(const Triple &T,
                                                 unsigned SyntaxVariant,
                                                 const MCAsmInfo &MAI,
                                                 const MCInstrInfo &MII,
                                                 const MCRegisterInfo &MRI) {
  return new SystemZInstPrinter(MAI, MII, MRI);
}

extern "C" void LLVMInitializeSystemZTargetMC() {
  // Register the MCAsmInfo.
  TargetRegistry::RegisterMCAsmInfo(getTheSystemZTarget(),
                                    createSystemZMCAsmInfo);

  // Register the MCCodeEmitter.
  TargetRegistry::RegisterMCCodeEmitter(getTheSystemZTarget(),
                                        createSystemZMCCodeEmitter);

  // Register the MCInstrInfo.
  TargetRegistry::RegisterMCInstrInfo(getTheSystemZTarget(),
                                      createSystemZMCInstrInfo);

  // Register the MCRegisterInfo.
  TargetRegistry::RegisterMCRegInfo(getTheSystemZTarget(),
                                    createSystemZMCRegisterInfo);

  // Register the MCSubtargetInfo.
  TargetRegistry::RegisterMCSubtargetInfo(getTheSystemZTarget(),
                                          createSystemZMCSubtargetInfo);

  // Register the MCAsmBackend.
  TargetRegistry::RegisterMCAsmBackend(getTheSystemZTarget(),
                                       createSystemZMCAsmBackend);

  // Register the MCInstPrinter.
  TargetRegistry::RegisterMCInstPrinter(getTheSystemZTarget(),
                                        createSystemZMCInstPrinter);
}
