//===-- LanaiAsmBackend.cpp - Lanai Assembler Backend ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "LanaiFixupKinds.h"
#include "MCTargetDesc/LanaiMCTargetDesc.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixupKindInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

// Prepare value for the target space
static unsigned adjustFixupValue(unsigned Kind, uint64_t Value) {
  switch (Kind) {
  case FK_Data_1:
  case FK_Data_2:
  case FK_Data_4:
  case FK_Data_8:
    return Value;
  case Lanai::FIXUP_LANAI_21:
  case Lanai::FIXUP_LANAI_21_F:
  case Lanai::FIXUP_LANAI_25:
  case Lanai::FIXUP_LANAI_32:
  case Lanai::FIXUP_LANAI_HI16:
  case Lanai::FIXUP_LANAI_LO16:
    return Value;
  default:
    llvm_unreachable("Unknown fixup kind!");
  }
}

namespace {
class LanaiAsmBackend : public MCAsmBackend {
  Triple::OSType OSType;

public:
  LanaiAsmBackend(const Target &T, Triple::OSType OST)
      : MCAsmBackend(support::big), OSType(OST) {}

  void applyFixup(const MCAssembler &Asm, const MCFixup &Fixup,
                  const MCValue &Target, MutableArrayRef<char> Data,
                  uint64_t Value, bool IsResolved,
                  const MCSubtargetInfo *STI) const override;

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override;

  // No instruction requires relaxation
  bool fixupNeedsRelaxation(const MCFixup & /*Fixup*/, uint64_t /*Value*/,
                            const MCRelaxableFragment * /*DF*/,
                            const MCAsmLayout & /*Layout*/) const override {
    return false;
  }

  const MCFixupKindInfo &getFixupKindInfo(MCFixupKind Kind) const override;

  unsigned getNumFixupKinds() const override {
    return Lanai::NumTargetFixupKinds;
  }

  bool mayNeedRelaxation(const MCInst & /*Inst*/,
                         const MCSubtargetInfo &STI) const override {
    return false;
  }

  void relaxInstruction(const MCInst & /*Inst*/,
                        const MCSubtargetInfo & /*STI*/,
                        MCInst & /*Res*/) const override {}

  bool writeNopData(raw_ostream &OS, uint64_t Count) const override;
};

bool LanaiAsmBackend::writeNopData(raw_ostream &OS, uint64_t Count) const {
  if ((Count % 4) != 0)
    return false;

  for (uint64_t i = 0; i < Count; i += 4)
    OS.write("\x15\0\0\0", 4);

  return true;
}

void LanaiAsmBackend::applyFixup(const MCAssembler &Asm, const MCFixup &Fixup,
                                 const MCValue &Target,
                                 MutableArrayRef<char> Data, uint64_t Value,
                                 bool /*IsResolved*/,
                                 const MCSubtargetInfo * /*STI*/) const {
  MCFixupKind Kind = Fixup.getKind();
  Value = adjustFixupValue(static_cast<unsigned>(Kind), Value);

  if (!Value)
    return; // This value doesn't change the encoding

  // Where in the object and where the number of bytes that need
  // fixing up
  unsigned Offset = Fixup.getOffset();
  unsigned NumBytes = (getFixupKindInfo(Kind).TargetSize + 7) / 8;
  unsigned FullSize = 4;

  // Grab current value, if any, from bits.
  uint64_t CurVal = 0;

  // Load instruction and apply value
  for (unsigned i = 0; i != NumBytes; ++i) {
    unsigned Idx = (FullSize - 1 - i);
    CurVal |= static_cast<uint64_t>(static_cast<uint8_t>(Data[Offset + Idx]))
              << (i * 8);
  }

  uint64_t Mask =
      (static_cast<uint64_t>(-1) >> (64 - getFixupKindInfo(Kind).TargetSize));
  CurVal |= Value & Mask;

  // Write out the fixed up bytes back to the code/data bits.
  for (unsigned i = 0; i != NumBytes; ++i) {
    unsigned Idx = (FullSize - 1 - i);
    Data[Offset + Idx] = static_cast<uint8_t>((CurVal >> (i * 8)) & 0xff);
  }
}

std::unique_ptr<MCObjectTargetWriter>
LanaiAsmBackend::createObjectTargetWriter() const {
  return createLanaiELFObjectWriter(MCELFObjectTargetWriter::getOSABI(OSType));
}

const MCFixupKindInfo &
LanaiAsmBackend::getFixupKindInfo(MCFixupKind Kind) const {
  static const MCFixupKindInfo Infos[Lanai::NumTargetFixupKinds] = {
      // This table *must* be in same the order of fixup_* kinds in
      // LanaiFixupKinds.h.
      // Note: The number of bits indicated here are assumed to be contiguous.
      //   This does not hold true for LANAI_21 and LANAI_21_F which are applied
      //   to bits 0x7cffff and 0x7cfffc, respectively. Since the 'bits' counts
      //   here are used only for cosmetic purposes, we set the size to 16 bits
      //   for these 21-bit relocation as llvm/lib/MC/MCAsmStreamer.cpp checks
      //   no bits are set in the fixup range.
      //
      // name          offset bits flags
      {"FIXUP_LANAI_NONE", 0, 32, 0},
      {"FIXUP_LANAI_21", 16, 16 /*21*/, 0},
      {"FIXUP_LANAI_21_F", 16, 16 /*21*/, 0},
      {"FIXUP_LANAI_25", 7, 25, 0},
      {"FIXUP_LANAI_32", 0, 32, 0},
      {"FIXUP_LANAI_HI16", 16, 16, 0},
      {"FIXUP_LANAI_LO16", 16, 16, 0}};

  if (Kind < FirstTargetFixupKind)
    return MCAsmBackend::getFixupKindInfo(Kind);

  assert(unsigned(Kind - FirstTargetFixupKind) < getNumFixupKinds() &&
         "Invalid kind!");
  return Infos[Kind - FirstTargetFixupKind];
}

} // namespace

MCAsmBackend *llvm::createLanaiAsmBackend(const Target &T,
                                          const MCSubtargetInfo &STI,
                                          const MCRegisterInfo & /*MRI*/,
                                          const MCTargetOptions & /*Options*/) {
  const Triple &TT = STI.getTargetTriple();
  if (!TT.isOSBinFormatELF())
    llvm_unreachable("OS not supported");

  return new LanaiAsmBackend(T, TT.getOS());
}
