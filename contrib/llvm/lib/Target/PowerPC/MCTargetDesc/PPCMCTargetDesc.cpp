//===-- PPCMCTargetDesc.cpp - PowerPC Target Descriptions -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides PowerPC specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/PPCMCTargetDesc.h"
#include "InstPrinter/PPCInstPrinter.h"
#include "MCTargetDesc/PPCMCAsmInfo.h"
#include "PPCTargetStreamer.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#include "PPCGenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "PPCGenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "PPCGenRegisterInfo.inc"

// Pin the vtable to this file.
PPCTargetStreamer::PPCTargetStreamer(MCStreamer &S) : MCTargetStreamer(S) {}

PPCTargetStreamer::~PPCTargetStreamer() = default;

static MCInstrInfo *createPPCMCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitPPCMCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createPPCMCRegisterInfo(const Triple &TT) {
  bool isPPC64 =
      (TT.getArch() == Triple::ppc64 || TT.getArch() == Triple::ppc64le);
  unsigned Flavour = isPPC64 ? 0 : 1;
  unsigned RA = isPPC64 ? PPC::LR8 : PPC::LR;

  MCRegisterInfo *X = new MCRegisterInfo();
  InitPPCMCRegisterInfo(X, RA, Flavour, Flavour);
  return X;
}

static MCSubtargetInfo *createPPCMCSubtargetInfo(const Triple &TT,
                                                 StringRef CPU, StringRef FS) {
  return createPPCMCSubtargetInfoImpl(TT, CPU, FS);
}

static MCAsmInfo *createPPCMCAsmInfo(const MCRegisterInfo &MRI,
                                     const Triple &TheTriple) {
  bool isPPC64 = (TheTriple.getArch() == Triple::ppc64 ||
                  TheTriple.getArch() == Triple::ppc64le);

  MCAsmInfo *MAI;
  if (TheTriple.isOSDarwin())
    MAI = new PPCMCAsmInfoDarwin(isPPC64, TheTriple);
  else
    MAI = new PPCELFMCAsmInfo(isPPC64, TheTriple);

  // Initial state of the frame pointer is R1.
  unsigned Reg = isPPC64 ? PPC::X1 : PPC::R1;
  MCCFIInstruction Inst =
      MCCFIInstruction::createDefCfa(nullptr, MRI.getDwarfRegNum(Reg, true), 0);
  MAI->addInitialFrameState(Inst);

  return MAI;
}

namespace {

class PPCTargetAsmStreamer : public PPCTargetStreamer {
  formatted_raw_ostream &OS;

public:
  PPCTargetAsmStreamer(MCStreamer &S, formatted_raw_ostream &OS)
      : PPCTargetStreamer(S), OS(OS) {}

  void emitTCEntry(const MCSymbol &S) override {
    OS << "\t.tc ";
    OS << S.getName();
    OS << "[TC],";
    OS << S.getName();
    OS << '\n';
  }

  void emitMachine(StringRef CPU) override {
    OS << "\t.machine " << CPU << '\n';
  }

  void emitAbiVersion(int AbiVersion) override {
    OS << "\t.abiversion " << AbiVersion << '\n';
  }

  void emitLocalEntry(MCSymbolELF *S, const MCExpr *LocalOffset) override {
    const MCAsmInfo *MAI = Streamer.getContext().getAsmInfo();

    OS << "\t.localentry\t";
    S->print(OS, MAI);
    OS << ", ";
    LocalOffset->print(OS, MAI);
    OS << '\n';
  }
};

class PPCTargetELFStreamer : public PPCTargetStreamer {
public:
  PPCTargetELFStreamer(MCStreamer &S) : PPCTargetStreamer(S) {}

  MCELFStreamer &getStreamer() {
    return static_cast<MCELFStreamer &>(Streamer);
  }

  void emitTCEntry(const MCSymbol &S) override {
    // Creates a R_PPC64_TOC relocation
    Streamer.EmitValueToAlignment(8);
    Streamer.EmitSymbolValue(&S, 8);
  }

  void emitMachine(StringRef CPU) override {
    // FIXME: Is there anything to do in here or does this directive only
    // limit the parser?
  }

  void emitAbiVersion(int AbiVersion) override {
    MCAssembler &MCA = getStreamer().getAssembler();
    unsigned Flags = MCA.getELFHeaderEFlags();
    Flags &= ~ELF::EF_PPC64_ABI;
    Flags |= (AbiVersion & ELF::EF_PPC64_ABI);
    MCA.setELFHeaderEFlags(Flags);
  }

  void emitLocalEntry(MCSymbolELF *S, const MCExpr *LocalOffset) override {
    MCAssembler &MCA = getStreamer().getAssembler();

    int64_t Res;
    if (!LocalOffset->evaluateAsAbsolute(Res, MCA))
      report_fatal_error(".localentry expression must be absolute.");

    unsigned Encoded = ELF::encodePPC64LocalEntryOffset(Res);
    if (Res != ELF::decodePPC64LocalEntryOffset(Encoded))
      report_fatal_error(".localentry expression cannot be encoded.");

    unsigned Other = S->getOther();
    Other &= ~ELF::STO_PPC64_LOCAL_MASK;
    Other |= Encoded;
    S->setOther(Other);

    // For GAS compatibility, unless we already saw a .abiversion directive,
    // set e_flags to indicate ELFv2 ABI.
    unsigned Flags = MCA.getELFHeaderEFlags();
    if ((Flags & ELF::EF_PPC64_ABI) == 0)
      MCA.setELFHeaderEFlags(Flags | 2);
  }

  void emitAssignment(MCSymbol *S, const MCExpr *Value) override {
    auto *Symbol = cast<MCSymbolELF>(S);
    // When encoding an assignment to set symbol A to symbol B, also copy
    // the st_other bits encoding the local entry point offset.
    if (Value->getKind() != MCExpr::SymbolRef)
      return;
    const auto &RhsSym = cast<MCSymbolELF>(
        static_cast<const MCSymbolRefExpr *>(Value)->getSymbol());
    unsigned Other = Symbol->getOther();
    Other &= ~ELF::STO_PPC64_LOCAL_MASK;
    Other |= RhsSym.getOther() & ELF::STO_PPC64_LOCAL_MASK;
    Symbol->setOther(Other);
  }
};

class PPCTargetMachOStreamer : public PPCTargetStreamer {
public:
  PPCTargetMachOStreamer(MCStreamer &S) : PPCTargetStreamer(S) {}

  void emitTCEntry(const MCSymbol &S) override {
    llvm_unreachable("Unknown pseudo-op: .tc");
  }

  void emitMachine(StringRef CPU) override {
    // FIXME: We should update the CPUType, CPUSubType in the Object file if
    // the new values are different from the defaults.
  }

  void emitAbiVersion(int AbiVersion) override {
    llvm_unreachable("Unknown pseudo-op: .abiversion");
  }

  void emitLocalEntry(MCSymbolELF *S, const MCExpr *LocalOffset) override {
    llvm_unreachable("Unknown pseudo-op: .localentry");
  }
};

} // end anonymous namespace

static MCTargetStreamer *createAsmTargetStreamer(MCStreamer &S,
                                                 formatted_raw_ostream &OS,
                                                 MCInstPrinter *InstPrint,
                                                 bool isVerboseAsm) {
  return new PPCTargetAsmStreamer(S, OS);
}

static MCTargetStreamer *
createObjectTargetStreamer(MCStreamer &S, const MCSubtargetInfo &STI) {
  const Triple &TT = STI.getTargetTriple();
  if (TT.isOSBinFormatELF())
    return new PPCTargetELFStreamer(S);
  return new PPCTargetMachOStreamer(S);
}

static MCInstPrinter *createPPCMCInstPrinter(const Triple &T,
                                             unsigned SyntaxVariant,
                                             const MCAsmInfo &MAI,
                                             const MCInstrInfo &MII,
                                             const MCRegisterInfo &MRI) {
  return new PPCInstPrinter(MAI, MII, MRI, T);
}

extern "C" void LLVMInitializePowerPCTargetMC() {
  for (Target *T :
       {&getThePPC32Target(), &getThePPC64Target(), &getThePPC64LETarget()}) {
    // Register the MC asm info.
    RegisterMCAsmInfoFn C(*T, createPPCMCAsmInfo);

    // Register the MC instruction info.
    TargetRegistry::RegisterMCInstrInfo(*T, createPPCMCInstrInfo);

    // Register the MC register info.
    TargetRegistry::RegisterMCRegInfo(*T, createPPCMCRegisterInfo);

    // Register the MC subtarget info.
    TargetRegistry::RegisterMCSubtargetInfo(*T, createPPCMCSubtargetInfo);

    // Register the MC Code Emitter
    TargetRegistry::RegisterMCCodeEmitter(*T, createPPCMCCodeEmitter);

    // Register the asm backend.
    TargetRegistry::RegisterMCAsmBackend(*T, createPPCAsmBackend);

    // Register the object target streamer.
    TargetRegistry::RegisterObjectTargetStreamer(*T,
                                                 createObjectTargetStreamer);

    // Register the asm target streamer.
    TargetRegistry::RegisterAsmTargetStreamer(*T, createAsmTargetStreamer);

    // Register the MCInstPrinter.
    TargetRegistry::RegisterMCInstPrinter(*T, createPPCMCInstPrinter);
  }
}
