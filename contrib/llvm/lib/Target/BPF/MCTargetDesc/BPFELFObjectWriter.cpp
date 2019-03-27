//===-- BPFELFObjectWriter.cpp - BPF ELF Writer ---------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/BPFMCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"
#include <cstdint>

using namespace llvm;

namespace {

class BPFELFObjectWriter : public MCELFObjectTargetWriter {
public:
  BPFELFObjectWriter(uint8_t OSABI);
  ~BPFELFObjectWriter() override = default;

protected:
  unsigned getRelocType(MCContext &Ctx, const MCValue &Target,
                        const MCFixup &Fixup, bool IsPCRel) const override;
};

} // end anonymous namespace

BPFELFObjectWriter::BPFELFObjectWriter(uint8_t OSABI)
    : MCELFObjectTargetWriter(/*Is64Bit*/ true, OSABI, ELF::EM_BPF,
                              /*HasRelocationAddend*/ false) {}

unsigned BPFELFObjectWriter::getRelocType(MCContext &Ctx, const MCValue &Target,
                                          const MCFixup &Fixup,
                                          bool IsPCRel) const {
  // determine the type of the relocation
  switch ((unsigned)Fixup.getKind()) {
  default:
    llvm_unreachable("invalid fixup kind!");
  case FK_SecRel_8:
    return ELF::R_BPF_64_64;
  case FK_PCRel_4:
  case FK_SecRel_4:
    return ELF::R_BPF_64_32;
  case FK_Data_8:
    return ELF::R_BPF_64_64;
  case FK_Data_4:
    // .BTF.ext generates FK_Data_4 relocations for
    // insn offset by creating temporary labels.
    // The insn offset is within the code section and
    // already been fulfilled by applyFixup(). No
    // further relocation is needed.
    if (const MCSymbolRefExpr *A = Target.getSymA()) {
      if (A->getSymbol().isTemporary()) {
        MCSection &Section = A->getSymbol().getSection();
        const MCSectionELF *SectionELF = dyn_cast<MCSectionELF>(&Section);
        assert(SectionELF && "Null section for reloc symbol");

        // The reloc symbol should be in text section.
        unsigned Flags = SectionELF->getFlags();
        if ((Flags & ELF::SHF_ALLOC) && (Flags & ELF::SHF_EXECINSTR))
          return ELF::R_BPF_NONE;
      }
    }
    return ELF::R_BPF_64_32;
  }
}

std::unique_ptr<MCObjectTargetWriter>
llvm::createBPFELFObjectWriter(uint8_t OSABI) {
  return llvm::make_unique<BPFELFObjectWriter>(OSABI);
}
