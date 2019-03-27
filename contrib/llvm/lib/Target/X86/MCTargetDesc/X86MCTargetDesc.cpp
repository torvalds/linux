//===-- X86MCTargetDesc.cpp - X86 Target Descriptions ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides X86 specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "X86MCTargetDesc.h"
#include "InstPrinter/X86ATTInstPrinter.h"
#include "InstPrinter/X86IntelInstPrinter.h"
#include "X86BaseInfo.h"
#include "X86MCAsmInfo.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/Triple.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MachineLocation.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/TargetRegistry.h"

#if _MSC_VER
#include <intrin.h>
#endif

using namespace llvm;

#define GET_REGINFO_MC_DESC
#include "X86GenRegisterInfo.inc"

#define GET_INSTRINFO_MC_DESC
#define GET_INSTRINFO_MC_HELPERS
#include "X86GenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "X86GenSubtargetInfo.inc"

std::string X86_MC::ParseX86Triple(const Triple &TT) {
  std::string FS;
  if (TT.getArch() == Triple::x86_64)
    FS = "+64bit-mode,-32bit-mode,-16bit-mode";
  else if (TT.getEnvironment() != Triple::CODE16)
    FS = "-64bit-mode,+32bit-mode,-16bit-mode";
  else
    FS = "-64bit-mode,-32bit-mode,+16bit-mode";

  return FS;
}

unsigned X86_MC::getDwarfRegFlavour(const Triple &TT, bool isEH) {
  if (TT.getArch() == Triple::x86_64)
    return DWARFFlavour::X86_64;

  if (TT.isOSDarwin())
    return isEH ? DWARFFlavour::X86_32_DarwinEH : DWARFFlavour::X86_32_Generic;
  if (TT.isOSCygMing())
    // Unsupported by now, just quick fallback
    return DWARFFlavour::X86_32_Generic;
  return DWARFFlavour::X86_32_Generic;
}

void X86_MC::initLLVMToSEHAndCVRegMapping(MCRegisterInfo *MRI) {
  // FIXME: TableGen these.
  for (unsigned Reg = X86::NoRegister + 1; Reg < X86::NUM_TARGET_REGS; ++Reg) {
    unsigned SEH = MRI->getEncodingValue(Reg);
    MRI->mapLLVMRegToSEHReg(Reg, SEH);
  }

  // Mapping from CodeView to MC register id.
  static const struct {
    codeview::RegisterId CVReg;
    MCPhysReg Reg;
  } RegMap[] = {
      {codeview::RegisterId::AL, X86::AL},
      {codeview::RegisterId::CL, X86::CL},
      {codeview::RegisterId::DL, X86::DL},
      {codeview::RegisterId::BL, X86::BL},
      {codeview::RegisterId::AH, X86::AH},
      {codeview::RegisterId::CH, X86::CH},
      {codeview::RegisterId::DH, X86::DH},
      {codeview::RegisterId::BH, X86::BH},
      {codeview::RegisterId::AX, X86::AX},
      {codeview::RegisterId::CX, X86::CX},
      {codeview::RegisterId::DX, X86::DX},
      {codeview::RegisterId::BX, X86::BX},
      {codeview::RegisterId::SP, X86::SP},
      {codeview::RegisterId::BP, X86::BP},
      {codeview::RegisterId::SI, X86::SI},
      {codeview::RegisterId::DI, X86::DI},
      {codeview::RegisterId::EAX, X86::EAX},
      {codeview::RegisterId::ECX, X86::ECX},
      {codeview::RegisterId::EDX, X86::EDX},
      {codeview::RegisterId::EBX, X86::EBX},
      {codeview::RegisterId::ESP, X86::ESP},
      {codeview::RegisterId::EBP, X86::EBP},
      {codeview::RegisterId::ESI, X86::ESI},
      {codeview::RegisterId::EDI, X86::EDI},

      {codeview::RegisterId::EFLAGS, X86::EFLAGS},

      {codeview::RegisterId::ST0, X86::FP0},
      {codeview::RegisterId::ST1, X86::FP1},
      {codeview::RegisterId::ST2, X86::FP2},
      {codeview::RegisterId::ST3, X86::FP3},
      {codeview::RegisterId::ST4, X86::FP4},
      {codeview::RegisterId::ST5, X86::FP5},
      {codeview::RegisterId::ST6, X86::FP6},
      {codeview::RegisterId::ST7, X86::FP7},

      {codeview::RegisterId::XMM0, X86::XMM0},
      {codeview::RegisterId::XMM1, X86::XMM1},
      {codeview::RegisterId::XMM2, X86::XMM2},
      {codeview::RegisterId::XMM3, X86::XMM3},
      {codeview::RegisterId::XMM4, X86::XMM4},
      {codeview::RegisterId::XMM5, X86::XMM5},
      {codeview::RegisterId::XMM6, X86::XMM6},
      {codeview::RegisterId::XMM7, X86::XMM7},

      {codeview::RegisterId::XMM8, X86::XMM8},
      {codeview::RegisterId::XMM9, X86::XMM9},
      {codeview::RegisterId::XMM10, X86::XMM10},
      {codeview::RegisterId::XMM11, X86::XMM11},
      {codeview::RegisterId::XMM12, X86::XMM12},
      {codeview::RegisterId::XMM13, X86::XMM13},
      {codeview::RegisterId::XMM14, X86::XMM14},
      {codeview::RegisterId::XMM15, X86::XMM15},

      {codeview::RegisterId::SIL, X86::SIL},
      {codeview::RegisterId::DIL, X86::DIL},
      {codeview::RegisterId::BPL, X86::BPL},
      {codeview::RegisterId::SPL, X86::SPL},
      {codeview::RegisterId::RAX, X86::RAX},
      {codeview::RegisterId::RBX, X86::RBX},
      {codeview::RegisterId::RCX, X86::RCX},
      {codeview::RegisterId::RDX, X86::RDX},
      {codeview::RegisterId::RSI, X86::RSI},
      {codeview::RegisterId::RDI, X86::RDI},
      {codeview::RegisterId::RBP, X86::RBP},
      {codeview::RegisterId::RSP, X86::RSP},
      {codeview::RegisterId::R8, X86::R8},
      {codeview::RegisterId::R9, X86::R9},
      {codeview::RegisterId::R10, X86::R10},
      {codeview::RegisterId::R11, X86::R11},
      {codeview::RegisterId::R12, X86::R12},
      {codeview::RegisterId::R13, X86::R13},
      {codeview::RegisterId::R14, X86::R14},
      {codeview::RegisterId::R15, X86::R15},
      {codeview::RegisterId::R8B, X86::R8B},
      {codeview::RegisterId::R9B, X86::R9B},
      {codeview::RegisterId::R10B, X86::R10B},
      {codeview::RegisterId::R11B, X86::R11B},
      {codeview::RegisterId::R12B, X86::R12B},
      {codeview::RegisterId::R13B, X86::R13B},
      {codeview::RegisterId::R14B, X86::R14B},
      {codeview::RegisterId::R15B, X86::R15B},
      {codeview::RegisterId::R8W, X86::R8W},
      {codeview::RegisterId::R9W, X86::R9W},
      {codeview::RegisterId::R10W, X86::R10W},
      {codeview::RegisterId::R11W, X86::R11W},
      {codeview::RegisterId::R12W, X86::R12W},
      {codeview::RegisterId::R13W, X86::R13W},
      {codeview::RegisterId::R14W, X86::R14W},
      {codeview::RegisterId::R15W, X86::R15W},
      {codeview::RegisterId::R8D, X86::R8D},
      {codeview::RegisterId::R9D, X86::R9D},
      {codeview::RegisterId::R10D, X86::R10D},
      {codeview::RegisterId::R11D, X86::R11D},
      {codeview::RegisterId::R12D, X86::R12D},
      {codeview::RegisterId::R13D, X86::R13D},
      {codeview::RegisterId::R14D, X86::R14D},
      {codeview::RegisterId::R15D, X86::R15D},
      {codeview::RegisterId::AMD64_YMM0, X86::YMM0},
      {codeview::RegisterId::AMD64_YMM1, X86::YMM1},
      {codeview::RegisterId::AMD64_YMM2, X86::YMM2},
      {codeview::RegisterId::AMD64_YMM3, X86::YMM3},
      {codeview::RegisterId::AMD64_YMM4, X86::YMM4},
      {codeview::RegisterId::AMD64_YMM5, X86::YMM5},
      {codeview::RegisterId::AMD64_YMM6, X86::YMM6},
      {codeview::RegisterId::AMD64_YMM7, X86::YMM7},
      {codeview::RegisterId::AMD64_YMM8, X86::YMM8},
      {codeview::RegisterId::AMD64_YMM9, X86::YMM9},
      {codeview::RegisterId::AMD64_YMM10, X86::YMM10},
      {codeview::RegisterId::AMD64_YMM11, X86::YMM11},
      {codeview::RegisterId::AMD64_YMM12, X86::YMM12},
      {codeview::RegisterId::AMD64_YMM13, X86::YMM13},
      {codeview::RegisterId::AMD64_YMM14, X86::YMM14},
      {codeview::RegisterId::AMD64_YMM15, X86::YMM15},
      {codeview::RegisterId::AMD64_YMM16, X86::YMM16},
      {codeview::RegisterId::AMD64_YMM17, X86::YMM17},
      {codeview::RegisterId::AMD64_YMM18, X86::YMM18},
      {codeview::RegisterId::AMD64_YMM19, X86::YMM19},
      {codeview::RegisterId::AMD64_YMM20, X86::YMM20},
      {codeview::RegisterId::AMD64_YMM21, X86::YMM21},
      {codeview::RegisterId::AMD64_YMM22, X86::YMM22},
      {codeview::RegisterId::AMD64_YMM23, X86::YMM23},
      {codeview::RegisterId::AMD64_YMM24, X86::YMM24},
      {codeview::RegisterId::AMD64_YMM25, X86::YMM25},
      {codeview::RegisterId::AMD64_YMM26, X86::YMM26},
      {codeview::RegisterId::AMD64_YMM27, X86::YMM27},
      {codeview::RegisterId::AMD64_YMM28, X86::YMM28},
      {codeview::RegisterId::AMD64_YMM29, X86::YMM29},
      {codeview::RegisterId::AMD64_YMM30, X86::YMM30},
      {codeview::RegisterId::AMD64_YMM31, X86::YMM31},
      {codeview::RegisterId::AMD64_ZMM0, X86::ZMM0},
      {codeview::RegisterId::AMD64_ZMM1, X86::ZMM1},
      {codeview::RegisterId::AMD64_ZMM2, X86::ZMM2},
      {codeview::RegisterId::AMD64_ZMM3, X86::ZMM3},
      {codeview::RegisterId::AMD64_ZMM4, X86::ZMM4},
      {codeview::RegisterId::AMD64_ZMM5, X86::ZMM5},
      {codeview::RegisterId::AMD64_ZMM6, X86::ZMM6},
      {codeview::RegisterId::AMD64_ZMM7, X86::ZMM7},
      {codeview::RegisterId::AMD64_ZMM8, X86::ZMM8},
      {codeview::RegisterId::AMD64_ZMM9, X86::ZMM9},
      {codeview::RegisterId::AMD64_ZMM10, X86::ZMM10},
      {codeview::RegisterId::AMD64_ZMM11, X86::ZMM11},
      {codeview::RegisterId::AMD64_ZMM12, X86::ZMM12},
      {codeview::RegisterId::AMD64_ZMM13, X86::ZMM13},
      {codeview::RegisterId::AMD64_ZMM14, X86::ZMM14},
      {codeview::RegisterId::AMD64_ZMM15, X86::ZMM15},
      {codeview::RegisterId::AMD64_ZMM16, X86::ZMM16},
      {codeview::RegisterId::AMD64_ZMM17, X86::ZMM17},
      {codeview::RegisterId::AMD64_ZMM18, X86::ZMM18},
      {codeview::RegisterId::AMD64_ZMM19, X86::ZMM19},
      {codeview::RegisterId::AMD64_ZMM20, X86::ZMM20},
      {codeview::RegisterId::AMD64_ZMM21, X86::ZMM21},
      {codeview::RegisterId::AMD64_ZMM22, X86::ZMM22},
      {codeview::RegisterId::AMD64_ZMM23, X86::ZMM23},
      {codeview::RegisterId::AMD64_ZMM24, X86::ZMM24},
      {codeview::RegisterId::AMD64_ZMM25, X86::ZMM25},
      {codeview::RegisterId::AMD64_ZMM26, X86::ZMM26},
      {codeview::RegisterId::AMD64_ZMM27, X86::ZMM27},
      {codeview::RegisterId::AMD64_ZMM28, X86::ZMM28},
      {codeview::RegisterId::AMD64_ZMM29, X86::ZMM29},
      {codeview::RegisterId::AMD64_ZMM30, X86::ZMM30},
      {codeview::RegisterId::AMD64_ZMM31, X86::ZMM31},
      {codeview::RegisterId::AMD64_K0, X86::K0},
      {codeview::RegisterId::AMD64_K1, X86::K1},
      {codeview::RegisterId::AMD64_K2, X86::K2},
      {codeview::RegisterId::AMD64_K3, X86::K3},
      {codeview::RegisterId::AMD64_K4, X86::K4},
      {codeview::RegisterId::AMD64_K5, X86::K5},
      {codeview::RegisterId::AMD64_K6, X86::K6},
      {codeview::RegisterId::AMD64_K7, X86::K7},
      {codeview::RegisterId::AMD64_XMM16, X86::XMM16},
      {codeview::RegisterId::AMD64_XMM17, X86::XMM17},
      {codeview::RegisterId::AMD64_XMM18, X86::XMM18},
      {codeview::RegisterId::AMD64_XMM19, X86::XMM19},
      {codeview::RegisterId::AMD64_XMM20, X86::XMM20},
      {codeview::RegisterId::AMD64_XMM21, X86::XMM21},
      {codeview::RegisterId::AMD64_XMM22, X86::XMM22},
      {codeview::RegisterId::AMD64_XMM23, X86::XMM23},
      {codeview::RegisterId::AMD64_XMM24, X86::XMM24},
      {codeview::RegisterId::AMD64_XMM25, X86::XMM25},
      {codeview::RegisterId::AMD64_XMM26, X86::XMM26},
      {codeview::RegisterId::AMD64_XMM27, X86::XMM27},
      {codeview::RegisterId::AMD64_XMM28, X86::XMM28},
      {codeview::RegisterId::AMD64_XMM29, X86::XMM29},
      {codeview::RegisterId::AMD64_XMM30, X86::XMM30},
      {codeview::RegisterId::AMD64_XMM31, X86::XMM31},

  };
  for (unsigned I = 0; I < array_lengthof(RegMap); ++I)
    MRI->mapLLVMRegToCVReg(RegMap[I].Reg, static_cast<int>(RegMap[I].CVReg));
}

MCSubtargetInfo *X86_MC::createX86MCSubtargetInfo(const Triple &TT,
                                                  StringRef CPU, StringRef FS) {
  std::string ArchFS = X86_MC::ParseX86Triple(TT);
  if (!FS.empty()) {
    if (!ArchFS.empty())
      ArchFS = (Twine(ArchFS) + "," + FS).str();
    else
      ArchFS = FS;
  }

  std::string CPUName = CPU;
  if (CPUName.empty())
    CPUName = "generic";

  return createX86MCSubtargetInfoImpl(TT, CPUName, ArchFS);
}

static MCInstrInfo *createX86MCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitX86MCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createX86MCRegisterInfo(const Triple &TT) {
  unsigned RA = (TT.getArch() == Triple::x86_64)
                    ? X86::RIP  // Should have dwarf #16.
                    : X86::EIP; // Should have dwarf #8.

  MCRegisterInfo *X = new MCRegisterInfo();
  InitX86MCRegisterInfo(X, RA, X86_MC::getDwarfRegFlavour(TT, false),
                        X86_MC::getDwarfRegFlavour(TT, true), RA);
  X86_MC::initLLVMToSEHAndCVRegMapping(X);
  return X;
}

static MCAsmInfo *createX86MCAsmInfo(const MCRegisterInfo &MRI,
                                     const Triple &TheTriple) {
  bool is64Bit = TheTriple.getArch() == Triple::x86_64;

  MCAsmInfo *MAI;
  if (TheTriple.isOSBinFormatMachO()) {
    if (is64Bit)
      MAI = new X86_64MCAsmInfoDarwin(TheTriple);
    else
      MAI = new X86MCAsmInfoDarwin(TheTriple);
  } else if (TheTriple.isOSBinFormatELF()) {
    // Force the use of an ELF container.
    MAI = new X86ELFMCAsmInfo(TheTriple);
  } else if (TheTriple.isWindowsMSVCEnvironment() ||
             TheTriple.isWindowsCoreCLREnvironment()) {
    MAI = new X86MCAsmInfoMicrosoft(TheTriple);
  } else if (TheTriple.isOSCygMing() ||
             TheTriple.isWindowsItaniumEnvironment()) {
    MAI = new X86MCAsmInfoGNUCOFF(TheTriple);
  } else {
    // The default is ELF.
    MAI = new X86ELFMCAsmInfo(TheTriple);
  }

  // Initialize initial frame state.
  // Calculate amount of bytes used for return address storing
  int stackGrowth = is64Bit ? -8 : -4;

  // Initial state of the frame pointer is esp+stackGrowth.
  unsigned StackPtr = is64Bit ? X86::RSP : X86::ESP;
  MCCFIInstruction Inst = MCCFIInstruction::createDefCfa(
      nullptr, MRI.getDwarfRegNum(StackPtr, true), -stackGrowth);
  MAI->addInitialFrameState(Inst);

  // Add return address to move list
  unsigned InstPtr = is64Bit ? X86::RIP : X86::EIP;
  MCCFIInstruction Inst2 = MCCFIInstruction::createOffset(
      nullptr, MRI.getDwarfRegNum(InstPtr, true), stackGrowth);
  MAI->addInitialFrameState(Inst2);

  return MAI;
}

static MCInstPrinter *createX86MCInstPrinter(const Triple &T,
                                             unsigned SyntaxVariant,
                                             const MCAsmInfo &MAI,
                                             const MCInstrInfo &MII,
                                             const MCRegisterInfo &MRI) {
  if (SyntaxVariant == 0)
    return new X86ATTInstPrinter(MAI, MII, MRI);
  if (SyntaxVariant == 1)
    return new X86IntelInstPrinter(MAI, MII, MRI);
  return nullptr;
}

static MCRelocationInfo *createX86MCRelocationInfo(const Triple &TheTriple,
                                                   MCContext &Ctx) {
  // Default to the stock relocation info.
  return llvm::createMCRelocationInfo(TheTriple, Ctx);
}

namespace llvm {
namespace X86_MC {

class X86MCInstrAnalysis : public MCInstrAnalysis {
  X86MCInstrAnalysis(const X86MCInstrAnalysis &) = delete;
  X86MCInstrAnalysis &operator=(const X86MCInstrAnalysis &) = delete;
  virtual ~X86MCInstrAnalysis() = default;

public:
  X86MCInstrAnalysis(const MCInstrInfo *MCII) : MCInstrAnalysis(MCII) {}

#define GET_STIPREDICATE_DECLS_FOR_MC_ANALYSIS
#include "X86GenSubtargetInfo.inc"

  bool clearsSuperRegisters(const MCRegisterInfo &MRI, const MCInst &Inst,
                            APInt &Mask) const override;
  std::vector<std::pair<uint64_t, uint64_t>>
  findPltEntries(uint64_t PltSectionVA, ArrayRef<uint8_t> PltContents,
                 uint64_t GotSectionVA,
                 const Triple &TargetTriple) const override;
};

#define GET_STIPREDICATE_DEFS_FOR_MC_ANALYSIS
#include "X86GenSubtargetInfo.inc"

bool X86MCInstrAnalysis::clearsSuperRegisters(const MCRegisterInfo &MRI,
                                              const MCInst &Inst,
                                              APInt &Mask) const {
  const MCInstrDesc &Desc = Info->get(Inst.getOpcode());
  unsigned NumDefs = Desc.getNumDefs();
  unsigned NumImplicitDefs = Desc.getNumImplicitDefs();
  assert(Mask.getBitWidth() == NumDefs + NumImplicitDefs &&
         "Unexpected number of bits in the mask!");

  bool HasVEX = (Desc.TSFlags & X86II::EncodingMask) == X86II::VEX;
  bool HasEVEX = (Desc.TSFlags & X86II::EncodingMask) == X86II::EVEX;
  bool HasXOP = (Desc.TSFlags & X86II::EncodingMask) == X86II::XOP;

  const MCRegisterClass &GR32RC = MRI.getRegClass(X86::GR32RegClassID);
  const MCRegisterClass &VR128XRC = MRI.getRegClass(X86::VR128XRegClassID);
  const MCRegisterClass &VR256XRC = MRI.getRegClass(X86::VR256XRegClassID);

  auto ClearsSuperReg = [=](unsigned RegID) {
    // On X86-64, a general purpose integer register is viewed as a 64-bit
    // register internal to the processor.
    // An update to the lower 32 bits of a 64 bit integer register is
    // architecturally defined to zero extend the upper 32 bits.
    if (GR32RC.contains(RegID))
      return true;

    // Early exit if this instruction has no vex/evex/xop prefix.
    if (!HasEVEX && !HasVEX && !HasXOP)
      return false;

    // All VEX and EVEX encoded instructions are defined to zero the high bits
    // of the destination register up to VLMAX (i.e. the maximum vector register
    // width pertaining to the instruction).
    // We assume the same behavior for XOP instructions too.
    return VR128XRC.contains(RegID) || VR256XRC.contains(RegID);
  };

  Mask.clearAllBits();
  for (unsigned I = 0, E = NumDefs; I < E; ++I) {
    const MCOperand &Op = Inst.getOperand(I);
    if (ClearsSuperReg(Op.getReg()))
      Mask.setBit(I);
  }

  for (unsigned I = 0, E = NumImplicitDefs; I < E; ++I) {
    const MCPhysReg Reg = Desc.getImplicitDefs()[I];
    if (ClearsSuperReg(Reg))
      Mask.setBit(NumDefs + I);
  }

  return Mask.getBoolValue();
}

static std::vector<std::pair<uint64_t, uint64_t>>
findX86PltEntries(uint64_t PltSectionVA, ArrayRef<uint8_t> PltContents,
                  uint64_t GotPltSectionVA) {
  // Do a lightweight parsing of PLT entries.
  std::vector<std::pair<uint64_t, uint64_t>> Result;
  for (uint64_t Byte = 0, End = PltContents.size(); Byte + 6 < End; ) {
    // Recognize a jmp.
    if (PltContents[Byte] == 0xff && PltContents[Byte + 1] == 0xa3) {
      // The jmp instruction at the beginning of each PLT entry jumps to the
      // address of the base of the .got.plt section plus the immediate.
      uint32_t Imm = support::endian::read32le(PltContents.data() + Byte + 2);
      Result.push_back(
          std::make_pair(PltSectionVA + Byte, GotPltSectionVA + Imm));
      Byte += 6;
    } else if (PltContents[Byte] == 0xff && PltContents[Byte + 1] == 0x25) {
      // The jmp instruction at the beginning of each PLT entry jumps to the
      // immediate.
      uint32_t Imm = support::endian::read32le(PltContents.data() + Byte + 2);
      Result.push_back(std::make_pair(PltSectionVA + Byte, Imm));
      Byte += 6;
    } else
      Byte++;
  }
  return Result;
}

static std::vector<std::pair<uint64_t, uint64_t>>
findX86_64PltEntries(uint64_t PltSectionVA, ArrayRef<uint8_t> PltContents) {
  // Do a lightweight parsing of PLT entries.
  std::vector<std::pair<uint64_t, uint64_t>> Result;
  for (uint64_t Byte = 0, End = PltContents.size(); Byte + 6 < End; ) {
    // Recognize a jmp.
    if (PltContents[Byte] == 0xff && PltContents[Byte + 1] == 0x25) {
      // The jmp instruction at the beginning of each PLT entry jumps to the
      // address of the next instruction plus the immediate.
      uint32_t Imm = support::endian::read32le(PltContents.data() + Byte + 2);
      Result.push_back(
          std::make_pair(PltSectionVA + Byte, PltSectionVA + Byte + 6 + Imm));
      Byte += 6;
    } else
      Byte++;
  }
  return Result;
}

std::vector<std::pair<uint64_t, uint64_t>> X86MCInstrAnalysis::findPltEntries(
    uint64_t PltSectionVA, ArrayRef<uint8_t> PltContents,
    uint64_t GotPltSectionVA, const Triple &TargetTriple) const {
  switch (TargetTriple.getArch()) {
    case Triple::x86:
      return findX86PltEntries(PltSectionVA, PltContents, GotPltSectionVA);
    case Triple::x86_64:
      return findX86_64PltEntries(PltSectionVA, PltContents);
    default:
      return {};
  }
}

} // end of namespace X86_MC

} // end of namespace llvm

static MCInstrAnalysis *createX86MCInstrAnalysis(const MCInstrInfo *Info) {
  return new X86_MC::X86MCInstrAnalysis(Info);
}

// Force static initialization.
extern "C" void LLVMInitializeX86TargetMC() {
  for (Target *T : {&getTheX86_32Target(), &getTheX86_64Target()}) {
    // Register the MC asm info.
    RegisterMCAsmInfoFn X(*T, createX86MCAsmInfo);

    // Register the MC instruction info.
    TargetRegistry::RegisterMCInstrInfo(*T, createX86MCInstrInfo);

    // Register the MC register info.
    TargetRegistry::RegisterMCRegInfo(*T, createX86MCRegisterInfo);

    // Register the MC subtarget info.
    TargetRegistry::RegisterMCSubtargetInfo(*T,
                                            X86_MC::createX86MCSubtargetInfo);

    // Register the MC instruction analyzer.
    TargetRegistry::RegisterMCInstrAnalysis(*T, createX86MCInstrAnalysis);

    // Register the code emitter.
    TargetRegistry::RegisterMCCodeEmitter(*T, createX86MCCodeEmitter);

    // Register the obj target streamer.
    TargetRegistry::RegisterObjectTargetStreamer(*T,
                                                 createX86ObjectTargetStreamer);

    // Register the asm target streamer.
    TargetRegistry::RegisterAsmTargetStreamer(*T, createX86AsmTargetStreamer);

    TargetRegistry::RegisterCOFFStreamer(*T, createX86WinCOFFStreamer);

    // Register the MCInstPrinter.
    TargetRegistry::RegisterMCInstPrinter(*T, createX86MCInstPrinter);

    // Register the MC relocation info.
    TargetRegistry::RegisterMCRelocationInfo(*T, createX86MCRelocationInfo);
  }

  // Register the asm backend.
  TargetRegistry::RegisterMCAsmBackend(getTheX86_32Target(),
                                       createX86_32AsmBackend);
  TargetRegistry::RegisterMCAsmBackend(getTheX86_64Target(),
                                       createX86_64AsmBackend);
}

unsigned llvm::getX86SubSuperRegisterOrZero(unsigned Reg, unsigned Size,
                                            bool High) {
  switch (Size) {
  default: return 0;
  case 8:
    if (High) {
      switch (Reg) {
      default: return getX86SubSuperRegisterOrZero(Reg, 64);
      case X86::SIL: case X86::SI: case X86::ESI: case X86::RSI:
        return X86::SI;
      case X86::DIL: case X86::DI: case X86::EDI: case X86::RDI:
        return X86::DI;
      case X86::BPL: case X86::BP: case X86::EBP: case X86::RBP:
        return X86::BP;
      case X86::SPL: case X86::SP: case X86::ESP: case X86::RSP:
        return X86::SP;
      case X86::AH: case X86::AL: case X86::AX: case X86::EAX: case X86::RAX:
        return X86::AH;
      case X86::DH: case X86::DL: case X86::DX: case X86::EDX: case X86::RDX:
        return X86::DH;
      case X86::CH: case X86::CL: case X86::CX: case X86::ECX: case X86::RCX:
        return X86::CH;
      case X86::BH: case X86::BL: case X86::BX: case X86::EBX: case X86::RBX:
        return X86::BH;
      }
    } else {
      switch (Reg) {
      default: return 0;
      case X86::AH: case X86::AL: case X86::AX: case X86::EAX: case X86::RAX:
        return X86::AL;
      case X86::DH: case X86::DL: case X86::DX: case X86::EDX: case X86::RDX:
        return X86::DL;
      case X86::CH: case X86::CL: case X86::CX: case X86::ECX: case X86::RCX:
        return X86::CL;
      case X86::BH: case X86::BL: case X86::BX: case X86::EBX: case X86::RBX:
        return X86::BL;
      case X86::SIL: case X86::SI: case X86::ESI: case X86::RSI:
        return X86::SIL;
      case X86::DIL: case X86::DI: case X86::EDI: case X86::RDI:
        return X86::DIL;
      case X86::BPL: case X86::BP: case X86::EBP: case X86::RBP:
        return X86::BPL;
      case X86::SPL: case X86::SP: case X86::ESP: case X86::RSP:
        return X86::SPL;
      case X86::R8B: case X86::R8W: case X86::R8D: case X86::R8:
        return X86::R8B;
      case X86::R9B: case X86::R9W: case X86::R9D: case X86::R9:
        return X86::R9B;
      case X86::R10B: case X86::R10W: case X86::R10D: case X86::R10:
        return X86::R10B;
      case X86::R11B: case X86::R11W: case X86::R11D: case X86::R11:
        return X86::R11B;
      case X86::R12B: case X86::R12W: case X86::R12D: case X86::R12:
        return X86::R12B;
      case X86::R13B: case X86::R13W: case X86::R13D: case X86::R13:
        return X86::R13B;
      case X86::R14B: case X86::R14W: case X86::R14D: case X86::R14:
        return X86::R14B;
      case X86::R15B: case X86::R15W: case X86::R15D: case X86::R15:
        return X86::R15B;
      }
    }
  case 16:
    switch (Reg) {
    default: return 0;
    case X86::AH: case X86::AL: case X86::AX: case X86::EAX: case X86::RAX:
      return X86::AX;
    case X86::DH: case X86::DL: case X86::DX: case X86::EDX: case X86::RDX:
      return X86::DX;
    case X86::CH: case X86::CL: case X86::CX: case X86::ECX: case X86::RCX:
      return X86::CX;
    case X86::BH: case X86::BL: case X86::BX: case X86::EBX: case X86::RBX:
      return X86::BX;
    case X86::SIL: case X86::SI: case X86::ESI: case X86::RSI:
      return X86::SI;
    case X86::DIL: case X86::DI: case X86::EDI: case X86::RDI:
      return X86::DI;
    case X86::BPL: case X86::BP: case X86::EBP: case X86::RBP:
      return X86::BP;
    case X86::SPL: case X86::SP: case X86::ESP: case X86::RSP:
      return X86::SP;
    case X86::R8B: case X86::R8W: case X86::R8D: case X86::R8:
      return X86::R8W;
    case X86::R9B: case X86::R9W: case X86::R9D: case X86::R9:
      return X86::R9W;
    case X86::R10B: case X86::R10W: case X86::R10D: case X86::R10:
      return X86::R10W;
    case X86::R11B: case X86::R11W: case X86::R11D: case X86::R11:
      return X86::R11W;
    case X86::R12B: case X86::R12W: case X86::R12D: case X86::R12:
      return X86::R12W;
    case X86::R13B: case X86::R13W: case X86::R13D: case X86::R13:
      return X86::R13W;
    case X86::R14B: case X86::R14W: case X86::R14D: case X86::R14:
      return X86::R14W;
    case X86::R15B: case X86::R15W: case X86::R15D: case X86::R15:
      return X86::R15W;
    }
  case 32:
    switch (Reg) {
    default: return 0;
    case X86::AH: case X86::AL: case X86::AX: case X86::EAX: case X86::RAX:
      return X86::EAX;
    case X86::DH: case X86::DL: case X86::DX: case X86::EDX: case X86::RDX:
      return X86::EDX;
    case X86::CH: case X86::CL: case X86::CX: case X86::ECX: case X86::RCX:
      return X86::ECX;
    case X86::BH: case X86::BL: case X86::BX: case X86::EBX: case X86::RBX:
      return X86::EBX;
    case X86::SIL: case X86::SI: case X86::ESI: case X86::RSI:
      return X86::ESI;
    case X86::DIL: case X86::DI: case X86::EDI: case X86::RDI:
      return X86::EDI;
    case X86::BPL: case X86::BP: case X86::EBP: case X86::RBP:
      return X86::EBP;
    case X86::SPL: case X86::SP: case X86::ESP: case X86::RSP:
      return X86::ESP;
    case X86::R8B: case X86::R8W: case X86::R8D: case X86::R8:
      return X86::R8D;
    case X86::R9B: case X86::R9W: case X86::R9D: case X86::R9:
      return X86::R9D;
    case X86::R10B: case X86::R10W: case X86::R10D: case X86::R10:
      return X86::R10D;
    case X86::R11B: case X86::R11W: case X86::R11D: case X86::R11:
      return X86::R11D;
    case X86::R12B: case X86::R12W: case X86::R12D: case X86::R12:
      return X86::R12D;
    case X86::R13B: case X86::R13W: case X86::R13D: case X86::R13:
      return X86::R13D;
    case X86::R14B: case X86::R14W: case X86::R14D: case X86::R14:
      return X86::R14D;
    case X86::R15B: case X86::R15W: case X86::R15D: case X86::R15:
      return X86::R15D;
    }
  case 64:
    switch (Reg) {
    default: return 0;
    case X86::AH: case X86::AL: case X86::AX: case X86::EAX: case X86::RAX:
      return X86::RAX;
    case X86::DH: case X86::DL: case X86::DX: case X86::EDX: case X86::RDX:
      return X86::RDX;
    case X86::CH: case X86::CL: case X86::CX: case X86::ECX: case X86::RCX:
      return X86::RCX;
    case X86::BH: case X86::BL: case X86::BX: case X86::EBX: case X86::RBX:
      return X86::RBX;
    case X86::SIL: case X86::SI: case X86::ESI: case X86::RSI:
      return X86::RSI;
    case X86::DIL: case X86::DI: case X86::EDI: case X86::RDI:
      return X86::RDI;
    case X86::BPL: case X86::BP: case X86::EBP: case X86::RBP:
      return X86::RBP;
    case X86::SPL: case X86::SP: case X86::ESP: case X86::RSP:
      return X86::RSP;
    case X86::R8B: case X86::R8W: case X86::R8D: case X86::R8:
      return X86::R8;
    case X86::R9B: case X86::R9W: case X86::R9D: case X86::R9:
      return X86::R9;
    case X86::R10B: case X86::R10W: case X86::R10D: case X86::R10:
      return X86::R10;
    case X86::R11B: case X86::R11W: case X86::R11D: case X86::R11:
      return X86::R11;
    case X86::R12B: case X86::R12W: case X86::R12D: case X86::R12:
      return X86::R12;
    case X86::R13B: case X86::R13W: case X86::R13D: case X86::R13:
      return X86::R13;
    case X86::R14B: case X86::R14W: case X86::R14D: case X86::R14:
      return X86::R14;
    case X86::R15B: case X86::R15W: case X86::R15D: case X86::R15:
      return X86::R15;
    }
  }
}

unsigned llvm::getX86SubSuperRegister(unsigned Reg, unsigned Size, bool High) {
  unsigned Res = getX86SubSuperRegisterOrZero(Reg, Size, High);
  assert(Res != 0 && "Unexpected register or VT");
  return Res;
}


