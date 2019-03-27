//===-- AArch64MCTargetDesc.cpp - AArch64 Target Descriptions ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides AArch64 specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "AArch64MCTargetDesc.h"
#include "AArch64ELFStreamer.h"
#include "AArch64MCAsmInfo.h"
#include "AArch64WinCOFFStreamer.h"
#include "InstPrinter/AArch64InstPrinter.h"
#include "MCTargetDesc/AArch64AddressingModes.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#define GET_INSTRINFO_MC_HELPERS
#include "AArch64GenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "AArch64GenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "AArch64GenRegisterInfo.inc"

static MCInstrInfo *createAArch64MCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitAArch64MCInstrInfo(X);
  return X;
}

static MCSubtargetInfo *
createAArch64MCSubtargetInfo(const Triple &TT, StringRef CPU, StringRef FS) {
  if (CPU.empty())
    CPU = "generic";

  return createAArch64MCSubtargetInfoImpl(TT, CPU, FS);
}

void AArch64_MC::initLLVMToCVRegMapping(MCRegisterInfo *MRI) {
  for (unsigned Reg = AArch64::NoRegister + 1;
       Reg < AArch64::NUM_TARGET_REGS; ++Reg) {
    unsigned CV = MRI->getEncodingValue(Reg);
    MRI->mapLLVMRegToCVReg(Reg, CV);
  }
}

static MCRegisterInfo *createAArch64MCRegisterInfo(const Triple &Triple) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitAArch64MCRegisterInfo(X, AArch64::LR);
  AArch64_MC::initLLVMToCVRegMapping(X);
  return X;
}

static MCAsmInfo *createAArch64MCAsmInfo(const MCRegisterInfo &MRI,
                                         const Triple &TheTriple) {
  MCAsmInfo *MAI;
  if (TheTriple.isOSBinFormatMachO())
    MAI = new AArch64MCAsmInfoDarwin();
  else if (TheTriple.isWindowsMSVCEnvironment())
    MAI = new AArch64MCAsmInfoMicrosoftCOFF();
  else if (TheTriple.isOSBinFormatCOFF())
    MAI = new AArch64MCAsmInfoGNUCOFF();
  else {
    assert(TheTriple.isOSBinFormatELF() && "Invalid target");
    MAI = new AArch64MCAsmInfoELF(TheTriple);
  }

  // Initial state of the frame pointer is SP.
  unsigned Reg = MRI.getDwarfRegNum(AArch64::SP, true);
  MCCFIInstruction Inst = MCCFIInstruction::createDefCfa(nullptr, Reg, 0);
  MAI->addInitialFrameState(Inst);

  return MAI;
}

static MCInstPrinter *createAArch64MCInstPrinter(const Triple &T,
                                                 unsigned SyntaxVariant,
                                                 const MCAsmInfo &MAI,
                                                 const MCInstrInfo &MII,
                                                 const MCRegisterInfo &MRI) {
  if (SyntaxVariant == 0)
    return new AArch64InstPrinter(MAI, MII, MRI);
  if (SyntaxVariant == 1)
    return new AArch64AppleInstPrinter(MAI, MII, MRI);

  return nullptr;
}

static MCStreamer *createELFStreamer(const Triple &T, MCContext &Ctx,
                                     std::unique_ptr<MCAsmBackend> &&TAB,
                                     std::unique_ptr<MCObjectWriter> &&OW,
                                     std::unique_ptr<MCCodeEmitter> &&Emitter,
                                     bool RelaxAll) {
  return createAArch64ELFStreamer(Ctx, std::move(TAB), std::move(OW),
                                  std::move(Emitter), RelaxAll);
}

static MCStreamer *createMachOStreamer(MCContext &Ctx,
                                       std::unique_ptr<MCAsmBackend> &&TAB,
                                       std::unique_ptr<MCObjectWriter> &&OW,
                                       std::unique_ptr<MCCodeEmitter> &&Emitter,
                                       bool RelaxAll,
                                       bool DWARFMustBeAtTheEnd) {
  return createMachOStreamer(Ctx, std::move(TAB), std::move(OW),
                             std::move(Emitter), RelaxAll, DWARFMustBeAtTheEnd,
                             /*LabelSections*/ true);
}

static MCStreamer *
createWinCOFFStreamer(MCContext &Ctx, std::unique_ptr<MCAsmBackend> &&TAB,
                      std::unique_ptr<MCObjectWriter> &&OW,
                      std::unique_ptr<MCCodeEmitter> &&Emitter, bool RelaxAll,
                      bool IncrementalLinkerCompatible) {
  return createAArch64WinCOFFStreamer(Ctx, std::move(TAB), std::move(OW),
                                      std::move(Emitter), RelaxAll,
                                      IncrementalLinkerCompatible);
}

namespace {

class AArch64MCInstrAnalysis : public MCInstrAnalysis {
public:
  AArch64MCInstrAnalysis(const MCInstrInfo *Info) : MCInstrAnalysis(Info) {}

  bool evaluateBranch(const MCInst &Inst, uint64_t Addr, uint64_t Size,
                      uint64_t &Target) const override {
    // Search for a PC-relative argument.
    // This will handle instructions like bcc (where the first argument is the
    // condition code) and cbz (where it is a register).
    const auto &Desc = Info->get(Inst.getOpcode());
    for (unsigned i = 0, e = Inst.getNumOperands(); i != e; i++) {
      if (Desc.OpInfo[i].OperandType == MCOI::OPERAND_PCREL) {
        int64_t Imm = Inst.getOperand(i).getImm() * 4;
        Target = Addr + Imm;
        return true;
      }
    }
    return false;
  }

  std::vector<std::pair<uint64_t, uint64_t>>
  findPltEntries(uint64_t PltSectionVA, ArrayRef<uint8_t> PltContents,
                 uint64_t GotPltSectionVA,
                 const Triple &TargetTriple) const override {
    // Do a lightweight parsing of PLT entries.
    std::vector<std::pair<uint64_t, uint64_t>> Result;
    for (uint64_t Byte = 0, End = PltContents.size(); Byte + 7 < End;
         Byte += 4) {
      uint32_t Insn = support::endian::read32le(PltContents.data() + Byte);
      // Check for adrp.
      if ((Insn & 0x9f000000) != 0x90000000)
        continue;
      uint64_t Imm = (((PltSectionVA + Byte) >> 12) << 12) +
            (((Insn >> 29) & 3) << 12) + (((Insn >> 5) & 0x3ffff) << 14);
      uint32_t Insn2 = support::endian::read32le(PltContents.data() + Byte + 4);
      // Check for: ldr Xt, [Xn, #pimm].
      if (Insn2 >> 22 == 0x3e5) {
        Imm += ((Insn2 >> 10) & 0xfff) << 3;
        Result.push_back(std::make_pair(PltSectionVA + Byte, Imm));
        Byte += 4;
      }
    }
    return Result;
  }
};

} // end anonymous namespace

static MCInstrAnalysis *createAArch64InstrAnalysis(const MCInstrInfo *Info) {
  return new AArch64MCInstrAnalysis(Info);
}

// Force static initialization.
extern "C" void LLVMInitializeAArch64TargetMC() {
  for (Target *T : {&getTheAArch64leTarget(), &getTheAArch64beTarget(),
                    &getTheARM64Target()}) {
    // Register the MC asm info.
    RegisterMCAsmInfoFn X(*T, createAArch64MCAsmInfo);

    // Register the MC instruction info.
    TargetRegistry::RegisterMCInstrInfo(*T, createAArch64MCInstrInfo);

    // Register the MC register info.
    TargetRegistry::RegisterMCRegInfo(*T, createAArch64MCRegisterInfo);

    // Register the MC subtarget info.
    TargetRegistry::RegisterMCSubtargetInfo(*T, createAArch64MCSubtargetInfo);

    // Register the MC instruction analyzer.
    TargetRegistry::RegisterMCInstrAnalysis(*T, createAArch64InstrAnalysis);

    // Register the MC Code Emitter
    TargetRegistry::RegisterMCCodeEmitter(*T, createAArch64MCCodeEmitter);

    // Register the obj streamers.
    TargetRegistry::RegisterELFStreamer(*T, createELFStreamer);
    TargetRegistry::RegisterMachOStreamer(*T, createMachOStreamer);
    TargetRegistry::RegisterCOFFStreamer(*T, createWinCOFFStreamer);

    // Register the obj target streamer.
    TargetRegistry::RegisterObjectTargetStreamer(
        *T, createAArch64ObjectTargetStreamer);

    // Register the asm streamer.
    TargetRegistry::RegisterAsmTargetStreamer(*T,
                                              createAArch64AsmTargetStreamer);
    // Register the MCInstPrinter.
    TargetRegistry::RegisterMCInstPrinter(*T, createAArch64MCInstPrinter);
  }

  // Register the asm backend.
  for (Target *T : {&getTheAArch64leTarget(), &getTheARM64Target()})
    TargetRegistry::RegisterMCAsmBackend(*T, createAArch64leAsmBackend);
  TargetRegistry::RegisterMCAsmBackend(getTheAArch64beTarget(),
                                       createAArch64beAsmBackend);
}
