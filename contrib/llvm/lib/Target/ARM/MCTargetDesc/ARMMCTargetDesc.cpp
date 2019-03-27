//===-- ARMMCTargetDesc.cpp - ARM Target Descriptions ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides ARM specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "ARMMCTargetDesc.h"
#include "ARMBaseInfo.h"
#include "ARMMCAsmInfo.h"
#include "InstPrinter/ARMInstPrinter.h"
#include "llvm/ADT/Triple.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TargetParser.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

#define GET_REGINFO_MC_DESC
#include "ARMGenRegisterInfo.inc"

static bool getMCRDeprecationInfo(MCInst &MI, const MCSubtargetInfo &STI,
                                  std::string &Info) {
  if (STI.getFeatureBits()[llvm::ARM::HasV7Ops] &&
      (MI.getOperand(0).isImm() && MI.getOperand(0).getImm() == 15) &&
      (MI.getOperand(1).isImm() && MI.getOperand(1).getImm() == 0) &&
      // Checks for the deprecated CP15ISB encoding:
      // mcr p15, #0, rX, c7, c5, #4
      (MI.getOperand(3).isImm() && MI.getOperand(3).getImm() == 7)) {
    if ((MI.getOperand(5).isImm() && MI.getOperand(5).getImm() == 4)) {
      if (MI.getOperand(4).isImm() && MI.getOperand(4).getImm() == 5) {
        Info = "deprecated since v7, use 'isb'";
        return true;
      }

      // Checks for the deprecated CP15DSB encoding:
      // mcr p15, #0, rX, c7, c10, #4
      if (MI.getOperand(4).isImm() && MI.getOperand(4).getImm() == 10) {
        Info = "deprecated since v7, use 'dsb'";
        return true;
      }
    }
    // Checks for the deprecated CP15DMB encoding:
    // mcr p15, #0, rX, c7, c10, #5
    if (MI.getOperand(4).isImm() && MI.getOperand(4).getImm() == 10 &&
        (MI.getOperand(5).isImm() && MI.getOperand(5).getImm() == 5)) {
      Info = "deprecated since v7, use 'dmb'";
      return true;
    }
  }
  return false;
}

static bool getITDeprecationInfo(MCInst &MI, const MCSubtargetInfo &STI,
                                 std::string &Info) {
  if (STI.getFeatureBits()[llvm::ARM::HasV8Ops] && MI.getOperand(1).isImm() &&
      MI.getOperand(1).getImm() != 8) {
    Info = "applying IT instruction to more than one subsequent instruction is "
           "deprecated";
    return true;
  }

  return false;
}

static bool getARMStoreDeprecationInfo(MCInst &MI, const MCSubtargetInfo &STI,
                                       std::string &Info) {
  assert(!STI.getFeatureBits()[llvm::ARM::ModeThumb] &&
         "cannot predicate thumb instructions");

  assert(MI.getNumOperands() >= 4 && "expected >= 4 arguments");
  for (unsigned OI = 4, OE = MI.getNumOperands(); OI < OE; ++OI) {
    assert(MI.getOperand(OI).isReg() && "expected register");
    if (MI.getOperand(OI).getReg() == ARM::SP ||
        MI.getOperand(OI).getReg() == ARM::PC) {
      Info = "use of SP or PC in the list is deprecated";
      return true;
    }
  }
  return false;
}

static bool getARMLoadDeprecationInfo(MCInst &MI, const MCSubtargetInfo &STI,
                                      std::string &Info) {
  assert(!STI.getFeatureBits()[llvm::ARM::ModeThumb] &&
         "cannot predicate thumb instructions");

  assert(MI.getNumOperands() >= 4 && "expected >= 4 arguments");
  bool ListContainsPC = false, ListContainsLR = false;
  for (unsigned OI = 4, OE = MI.getNumOperands(); OI < OE; ++OI) {
    assert(MI.getOperand(OI).isReg() && "expected register");
    switch (MI.getOperand(OI).getReg()) {
    default:
      break;
    case ARM::LR:
      ListContainsLR = true;
      break;
    case ARM::PC:
      ListContainsPC = true;
      break;
    case ARM::SP:
      Info = "use of SP in the list is deprecated";
      return true;
    }
  }

  if (ListContainsPC && ListContainsLR) {
    Info = "use of LR and PC simultaneously in the list is deprecated";
    return true;
  }

  return false;
}

#define GET_INSTRINFO_MC_DESC
#include "ARMGenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "ARMGenSubtargetInfo.inc"

std::string ARM_MC::ParseARMTriple(const Triple &TT, StringRef CPU) {
  std::string ARMArchFeature;

  ARM::ArchKind ArchID = ARM::parseArch(TT.getArchName());
  if (ArchID != ARM::ArchKind::INVALID &&  (CPU.empty() || CPU == "generic"))
    ARMArchFeature = (ARMArchFeature + "+" + ARM::getArchName(ArchID)).str();

  if (TT.isThumb()) {
    if (!ARMArchFeature.empty())
      ARMArchFeature += ",";
    ARMArchFeature += "+thumb-mode,+v4t";
  }

  if (TT.isOSNaCl()) {
    if (!ARMArchFeature.empty())
      ARMArchFeature += ",";
    ARMArchFeature += "+nacl-trap";
  }

  if (TT.isOSWindows()) {
    if (!ARMArchFeature.empty())
      ARMArchFeature += ",";
    ARMArchFeature += "+noarm";
  }

  return ARMArchFeature;
}

MCSubtargetInfo *ARM_MC::createARMMCSubtargetInfo(const Triple &TT,
                                                  StringRef CPU, StringRef FS) {
  std::string ArchFS = ARM_MC::ParseARMTriple(TT, CPU);
  if (!FS.empty()) {
    if (!ArchFS.empty())
      ArchFS = (Twine(ArchFS) + "," + FS).str();
    else
      ArchFS = FS;
  }

  return createARMMCSubtargetInfoImpl(TT, CPU, ArchFS);
}

static MCInstrInfo *createARMMCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitARMMCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createARMMCRegisterInfo(const Triple &Triple) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitARMMCRegisterInfo(X, ARM::LR, 0, 0, ARM::PC);
  return X;
}

static MCAsmInfo *createARMMCAsmInfo(const MCRegisterInfo &MRI,
                                     const Triple &TheTriple) {
  MCAsmInfo *MAI;
  if (TheTriple.isOSDarwin() || TheTriple.isOSBinFormatMachO())
    MAI = new ARMMCAsmInfoDarwin(TheTriple);
  else if (TheTriple.isWindowsMSVCEnvironment())
    MAI = new ARMCOFFMCAsmInfoMicrosoft();
  else if (TheTriple.isOSWindows())
    MAI = new ARMCOFFMCAsmInfoGNU();
  else
    MAI = new ARMELFMCAsmInfo(TheTriple);

  unsigned Reg = MRI.getDwarfRegNum(ARM::SP, true);
  MAI->addInitialFrameState(MCCFIInstruction::createDefCfa(nullptr, Reg, 0));

  return MAI;
}

static MCStreamer *createELFStreamer(const Triple &T, MCContext &Ctx,
                                     std::unique_ptr<MCAsmBackend> &&MAB,
                                     std::unique_ptr<MCObjectWriter> &&OW,
                                     std::unique_ptr<MCCodeEmitter> &&Emitter,
                                     bool RelaxAll) {
  return createARMELFStreamer(
      Ctx, std::move(MAB), std::move(OW), std::move(Emitter), false,
      (T.getArch() == Triple::thumb || T.getArch() == Triple::thumbeb));
}

static MCStreamer *
createARMMachOStreamer(MCContext &Ctx, std::unique_ptr<MCAsmBackend> &&MAB,
                       std::unique_ptr<MCObjectWriter> &&OW,
                       std::unique_ptr<MCCodeEmitter> &&Emitter, bool RelaxAll,
                       bool DWARFMustBeAtTheEnd) {
  return createMachOStreamer(Ctx, std::move(MAB), std::move(OW),
                             std::move(Emitter), false, DWARFMustBeAtTheEnd);
}

static MCInstPrinter *createARMMCInstPrinter(const Triple &T,
                                             unsigned SyntaxVariant,
                                             const MCAsmInfo &MAI,
                                             const MCInstrInfo &MII,
                                             const MCRegisterInfo &MRI) {
  if (SyntaxVariant == 0)
    return new ARMInstPrinter(MAI, MII, MRI);
  return nullptr;
}

static MCRelocationInfo *createARMMCRelocationInfo(const Triple &TT,
                                                   MCContext &Ctx) {
  if (TT.isOSBinFormatMachO())
    return createARMMachORelocationInfo(Ctx);
  // Default to the stock relocation info.
  return llvm::createMCRelocationInfo(TT, Ctx);
}

namespace {

class ARMMCInstrAnalysis : public MCInstrAnalysis {
public:
  ARMMCInstrAnalysis(const MCInstrInfo *Info) : MCInstrAnalysis(Info) {}

  bool isUnconditionalBranch(const MCInst &Inst) const override {
    // BCCs with the "always" predicate are unconditional branches.
    if (Inst.getOpcode() == ARM::Bcc && Inst.getOperand(1).getImm()==ARMCC::AL)
      return true;
    return MCInstrAnalysis::isUnconditionalBranch(Inst);
  }

  bool isConditionalBranch(const MCInst &Inst) const override {
    // BCCs with the "always" predicate are unconditional branches.
    if (Inst.getOpcode() == ARM::Bcc && Inst.getOperand(1).getImm()==ARMCC::AL)
      return false;
    return MCInstrAnalysis::isConditionalBranch(Inst);
  }

  bool evaluateBranch(const MCInst &Inst, uint64_t Addr,
                      uint64_t Size, uint64_t &Target) const override {
    // We only handle PCRel branches for now.
    if (Info->get(Inst.getOpcode()).OpInfo[0].OperandType!=MCOI::OPERAND_PCREL)
      return false;

    int64_t Imm = Inst.getOperand(0).getImm();
    Target = Addr+Imm+8; // In ARM mode the PC is always off by 8 bytes.
    return true;
  }
};

class ThumbMCInstrAnalysis : public ARMMCInstrAnalysis {
public:
  ThumbMCInstrAnalysis(const MCInstrInfo *Info) : ARMMCInstrAnalysis(Info) {}

  bool evaluateBranch(const MCInst &Inst, uint64_t Addr,
                      uint64_t Size, uint64_t &Target) const override {
    // We only handle PCRel branches for now.
    if (Info->get(Inst.getOpcode()).OpInfo[0].OperandType!=MCOI::OPERAND_PCREL)
      return false;

    int64_t Imm = Inst.getOperand(0).getImm();
    Target = Addr+Imm+4; // In Thumb mode the PC is always off by 4 bytes.
    return true;
  }
};

}

static MCInstrAnalysis *createARMMCInstrAnalysis(const MCInstrInfo *Info) {
  return new ARMMCInstrAnalysis(Info);
}

static MCInstrAnalysis *createThumbMCInstrAnalysis(const MCInstrInfo *Info) {
  return new ThumbMCInstrAnalysis(Info);
}

// Force static initialization.
extern "C" void LLVMInitializeARMTargetMC() {
  for (Target *T : {&getTheARMLETarget(), &getTheARMBETarget(),
                    &getTheThumbLETarget(), &getTheThumbBETarget()}) {
    // Register the MC asm info.
    RegisterMCAsmInfoFn X(*T, createARMMCAsmInfo);

    // Register the MC instruction info.
    TargetRegistry::RegisterMCInstrInfo(*T, createARMMCInstrInfo);

    // Register the MC register info.
    TargetRegistry::RegisterMCRegInfo(*T, createARMMCRegisterInfo);

    // Register the MC subtarget info.
    TargetRegistry::RegisterMCSubtargetInfo(*T,
                                            ARM_MC::createARMMCSubtargetInfo);

    TargetRegistry::RegisterELFStreamer(*T, createELFStreamer);
    TargetRegistry::RegisterCOFFStreamer(*T, createARMWinCOFFStreamer);
    TargetRegistry::RegisterMachOStreamer(*T, createARMMachOStreamer);

    // Register the obj target streamer.
    TargetRegistry::RegisterObjectTargetStreamer(*T,
                                                 createARMObjectTargetStreamer);

    // Register the asm streamer.
    TargetRegistry::RegisterAsmTargetStreamer(*T, createARMTargetAsmStreamer);

    // Register the null TargetStreamer.
    TargetRegistry::RegisterNullTargetStreamer(*T, createARMNullTargetStreamer);

    // Register the MCInstPrinter.
    TargetRegistry::RegisterMCInstPrinter(*T, createARMMCInstPrinter);

    // Register the MC relocation info.
    TargetRegistry::RegisterMCRelocationInfo(*T, createARMMCRelocationInfo);
  }

  // Register the MC instruction analyzer.
  for (Target *T : {&getTheARMLETarget(), &getTheARMBETarget()})
    TargetRegistry::RegisterMCInstrAnalysis(*T, createARMMCInstrAnalysis);
  for (Target *T : {&getTheThumbLETarget(), &getTheThumbBETarget()})
    TargetRegistry::RegisterMCInstrAnalysis(*T, createThumbMCInstrAnalysis);

  for (Target *T : {&getTheARMLETarget(), &getTheThumbLETarget()}) {
    TargetRegistry::RegisterMCCodeEmitter(*T, createARMLEMCCodeEmitter);
    TargetRegistry::RegisterMCAsmBackend(*T, createARMLEAsmBackend);
  }
  for (Target *T : {&getTheARMBETarget(), &getTheThumbBETarget()}) {
    TargetRegistry::RegisterMCCodeEmitter(*T, createARMBEMCCodeEmitter);
    TargetRegistry::RegisterMCAsmBackend(*T, createARMBEAsmBackend);
  }
}
