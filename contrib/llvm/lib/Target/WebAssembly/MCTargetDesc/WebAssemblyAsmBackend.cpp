//===-- WebAssemblyAsmBackend.cpp - WebAssembly Assembler Backend ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the WebAssemblyAsmBackend class.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/WebAssemblyFixupKinds.h"
#include "MCTargetDesc/WebAssemblyMCTargetDesc.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixupKindInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCWasmObjectWriter.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

class WebAssemblyAsmBackend final : public MCAsmBackend {
  bool Is64Bit;

public:
  explicit WebAssemblyAsmBackend(bool Is64Bit)
      : MCAsmBackend(support::little), Is64Bit(Is64Bit) {}
  ~WebAssemblyAsmBackend() override {}

  unsigned getNumFixupKinds() const override {
    return WebAssembly::NumTargetFixupKinds;
  }

  const MCFixupKindInfo &getFixupKindInfo(MCFixupKind Kind) const override;

  void applyFixup(const MCAssembler &Asm, const MCFixup &Fixup,
                  const MCValue &Target, MutableArrayRef<char> Data,
                  uint64_t Value, bool IsPCRel,
                  const MCSubtargetInfo *STI) const override;

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override;

  // No instruction requires relaxation
  bool fixupNeedsRelaxation(const MCFixup &Fixup, uint64_t Value,
                            const MCRelaxableFragment *DF,
                            const MCAsmLayout &Layout) const override {
    return false;
  }

  bool mayNeedRelaxation(const MCInst &Inst,
                         const MCSubtargetInfo &STI) const override {
    return false;
  }

  void relaxInstruction(const MCInst &Inst, const MCSubtargetInfo &STI,
                        MCInst &Res) const override {}

  bool writeNopData(raw_ostream &OS, uint64_t Count) const override;
};

const MCFixupKindInfo &
WebAssemblyAsmBackend::getFixupKindInfo(MCFixupKind Kind) const {
  const static MCFixupKindInfo Infos[WebAssembly::NumTargetFixupKinds] = {
      // This table *must* be in the order that the fixup_* kinds are defined in
      // WebAssemblyFixupKinds.h.
      //
      // Name                     Offset (bits) Size (bits)     Flags
      {"fixup_code_sleb128_i32", 0, 5 * 8, 0},
      {"fixup_code_sleb128_i64", 0, 10 * 8, 0},
      {"fixup_code_uleb128_i32", 0, 5 * 8, 0},
  };

  if (Kind < FirstTargetFixupKind)
    return MCAsmBackend::getFixupKindInfo(Kind);

  assert(unsigned(Kind - FirstTargetFixupKind) < getNumFixupKinds() &&
         "Invalid kind!");
  return Infos[Kind - FirstTargetFixupKind];
}

bool WebAssemblyAsmBackend::writeNopData(raw_ostream &OS,
                                         uint64_t Count) const {
  for (uint64_t i = 0; i < Count; ++i)
    OS << char(WebAssembly::Nop);

  return true;
}

void WebAssemblyAsmBackend::applyFixup(const MCAssembler &Asm,
                                       const MCFixup &Fixup,
                                       const MCValue &Target,
                                       MutableArrayRef<char> Data,
                                       uint64_t Value, bool IsPCRel,
                                       const MCSubtargetInfo *STI) const {
  const MCFixupKindInfo &Info = getFixupKindInfo(Fixup.getKind());
  assert(Info.Flags == 0 && "WebAssembly does not use MCFixupKindInfo flags");

  unsigned NumBytes = alignTo(Info.TargetSize, 8) / 8;
  if (Value == 0)
    return; // Doesn't change encoding.

  // Shift the value into position.
  Value <<= Info.TargetOffset;

  unsigned Offset = Fixup.getOffset();
  assert(Offset + NumBytes <= Data.size() && "Invalid fixup offset!");

  // For each byte of the fragment that the fixup touches, mask in the
  // bits from the fixup value.
  for (unsigned i = 0; i != NumBytes; ++i)
    Data[Offset + i] |= uint8_t((Value >> (i * 8)) & 0xff);
}

std::unique_ptr<MCObjectTargetWriter>
WebAssemblyAsmBackend::createObjectTargetWriter() const {
  return createWebAssemblyWasmObjectWriter(Is64Bit);
}

} // end anonymous namespace

MCAsmBackend *llvm::createWebAssemblyAsmBackend(const Triple &TT) {
  return new WebAssemblyAsmBackend(TT.isArch64Bit());
}
