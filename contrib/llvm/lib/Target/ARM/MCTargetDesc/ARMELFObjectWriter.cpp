//===-- ARMELFObjectWriter.cpp - ARM ELF Writer ---------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/ARMFixupKinds.h"
#include "MCTargetDesc/ARMMCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>

using namespace llvm;

namespace {

  class ARMELFObjectWriter : public MCELFObjectTargetWriter {
    enum { DefaultEABIVersion = 0x05000000U };

    unsigned GetRelocTypeInner(const MCValue &Target, const MCFixup &Fixup,
                               bool IsPCRel, MCContext &Ctx) const;

  public:
    ARMELFObjectWriter(uint8_t OSABI);

    ~ARMELFObjectWriter() override = default;

    unsigned getRelocType(MCContext &Ctx, const MCValue &Target,
                          const MCFixup &Fixup, bool IsPCRel) const override;

    bool needsRelocateWithSymbol(const MCSymbol &Sym,
                                 unsigned Type) const override;

    void addTargetSectionFlags(MCContext &Ctx, MCSectionELF &Sec) override;
  };

} // end anonymous namespace

ARMELFObjectWriter::ARMELFObjectWriter(uint8_t OSABI)
  : MCELFObjectTargetWriter(/*Is64Bit*/ false, OSABI,
                            ELF::EM_ARM,
                            /*HasRelocationAddend*/ false) {}

bool ARMELFObjectWriter::needsRelocateWithSymbol(const MCSymbol &Sym,
                                                 unsigned Type) const {
  // FIXME: This is extremely conservative. This really needs to use a
  // whitelist with a clear explanation for why each realocation needs to
  // point to the symbol, not to the section.
  switch (Type) {
  default:
    return true;

  case ELF::R_ARM_PREL31:
  case ELF::R_ARM_ABS32:
    return false;
  }
}

// Need to examine the Fixup when determining whether to
// emit the relocation as an explicit symbol or as a section relative
// offset
unsigned ARMELFObjectWriter::getRelocType(MCContext &Ctx, const MCValue &Target,
                                          const MCFixup &Fixup,
                                          bool IsPCRel) const {
  return GetRelocTypeInner(Target, Fixup, IsPCRel, Ctx);
}

unsigned ARMELFObjectWriter::GetRelocTypeInner(const MCValue &Target,
                                               const MCFixup &Fixup,
                                               bool IsPCRel,
                                               MCContext &Ctx) const {
  MCSymbolRefExpr::VariantKind Modifier = Target.getAccessVariant();

  if (IsPCRel) {
    switch ((unsigned)Fixup.getKind()) {
    default:
      Ctx.reportFatalError(Fixup.getLoc(), "unsupported relocation on symbol");
      return ELF::R_ARM_NONE;
    case FK_Data_4:
      switch (Modifier) {
      default:
        llvm_unreachable("Unsupported Modifier");
      case MCSymbolRefExpr::VK_None:
        return ELF::R_ARM_REL32;
      case MCSymbolRefExpr::VK_GOTTPOFF:
        return ELF::R_ARM_TLS_IE32;
      case MCSymbolRefExpr::VK_ARM_GOT_PREL:
        return ELF::R_ARM_GOT_PREL;
      case MCSymbolRefExpr::VK_ARM_PREL31:
        return ELF::R_ARM_PREL31;
      }
    case ARM::fixup_arm_blx:
    case ARM::fixup_arm_uncondbl:
      switch (Modifier) {
      case MCSymbolRefExpr::VK_PLT:
        return ELF::R_ARM_CALL;
      case MCSymbolRefExpr::VK_TLSCALL:
        return ELF::R_ARM_TLS_CALL;
      default:
        return ELF::R_ARM_CALL;
      }
    case ARM::fixup_arm_condbl:
    case ARM::fixup_arm_condbranch:
    case ARM::fixup_arm_uncondbranch:
      return ELF::R_ARM_JUMP24;
    case ARM::fixup_t2_condbranch:
      return ELF::R_ARM_THM_JUMP19;
    case ARM::fixup_t2_uncondbranch:
      return ELF::R_ARM_THM_JUMP24;
    case ARM::fixup_arm_movt_hi16:
      return ELF::R_ARM_MOVT_PREL;
    case ARM::fixup_arm_movw_lo16:
      return ELF::R_ARM_MOVW_PREL_NC;
    case ARM::fixup_t2_movt_hi16:
      return ELF::R_ARM_THM_MOVT_PREL;
    case ARM::fixup_t2_movw_lo16:
      return ELF::R_ARM_THM_MOVW_PREL_NC;
    case ARM::fixup_arm_thumb_br:
      return ELF::R_ARM_THM_JUMP11;
    case ARM::fixup_arm_thumb_bcc:
      return ELF::R_ARM_THM_JUMP8;
    case ARM::fixup_arm_thumb_bl:
    case ARM::fixup_arm_thumb_blx:
      switch (Modifier) {
      case MCSymbolRefExpr::VK_TLSCALL:
        return ELF::R_ARM_THM_TLS_CALL;
      default:
        return ELF::R_ARM_THM_CALL;
      }
    }
  }
  switch ((unsigned)Fixup.getKind()) {
  default:
    Ctx.reportFatalError(Fixup.getLoc(), "unsupported relocation on symbol");
    return ELF::R_ARM_NONE;
  case FK_Data_1:
    switch (Modifier) {
    default:
      llvm_unreachable("unsupported Modifier");
    case MCSymbolRefExpr::VK_None:
      return ELF::R_ARM_ABS8;
    }
  case FK_Data_2:
    switch (Modifier) {
    default:
      llvm_unreachable("unsupported modifier");
    case MCSymbolRefExpr::VK_None:
      return ELF::R_ARM_ABS16;
    }
  case FK_Data_4:
    switch (Modifier) {
    default:
      llvm_unreachable("Unsupported Modifier");
    case MCSymbolRefExpr::VK_ARM_NONE:
      return ELF::R_ARM_NONE;
    case MCSymbolRefExpr::VK_GOT:
      return ELF::R_ARM_GOT_BREL;
    case MCSymbolRefExpr::VK_TLSGD:
      return ELF::R_ARM_TLS_GD32;
    case MCSymbolRefExpr::VK_TPOFF:
      return ELF::R_ARM_TLS_LE32;
    case MCSymbolRefExpr::VK_GOTTPOFF:
      return ELF::R_ARM_TLS_IE32;
    case MCSymbolRefExpr::VK_None:
      return ELF::R_ARM_ABS32;
    case MCSymbolRefExpr::VK_GOTOFF:
      return ELF::R_ARM_GOTOFF32;
    case MCSymbolRefExpr::VK_ARM_GOT_PREL:
      return ELF::R_ARM_GOT_PREL;
    case MCSymbolRefExpr::VK_ARM_TARGET1:
      return ELF::R_ARM_TARGET1;
    case MCSymbolRefExpr::VK_ARM_TARGET2:
      return ELF::R_ARM_TARGET2;
    case MCSymbolRefExpr::VK_ARM_PREL31:
      return ELF::R_ARM_PREL31;
    case MCSymbolRefExpr::VK_ARM_SBREL:
      return ELF::R_ARM_SBREL32;
    case MCSymbolRefExpr::VK_ARM_TLSLDO:
      return ELF::R_ARM_TLS_LDO32;
    case MCSymbolRefExpr::VK_TLSCALL:
      return ELF::R_ARM_TLS_CALL;
    case MCSymbolRefExpr::VK_TLSDESC:
      return ELF::R_ARM_TLS_GOTDESC;
    case MCSymbolRefExpr::VK_TLSLDM:
      return ELF::R_ARM_TLS_LDM32;
    case MCSymbolRefExpr::VK_ARM_TLSDESCSEQ:
      return ELF::R_ARM_TLS_DESCSEQ;
    }
  case ARM::fixup_arm_condbranch:
  case ARM::fixup_arm_uncondbranch:
    return ELF::R_ARM_JUMP24;
  case ARM::fixup_arm_movt_hi16:
    switch (Modifier) {
    default:
      llvm_unreachable("Unsupported Modifier");
    case MCSymbolRefExpr::VK_None:
      return ELF::R_ARM_MOVT_ABS;
    case MCSymbolRefExpr::VK_ARM_SBREL:
      return ELF::R_ARM_MOVT_BREL;
    }
  case ARM::fixup_arm_movw_lo16:
    switch (Modifier) {
    default:
      llvm_unreachable("Unsupported Modifier");
    case MCSymbolRefExpr::VK_None:
      return ELF::R_ARM_MOVW_ABS_NC;
    case MCSymbolRefExpr::VK_ARM_SBREL:
      return ELF::R_ARM_MOVW_BREL_NC;
    }
  case ARM::fixup_t2_movt_hi16:
    switch (Modifier) {
    default:
      llvm_unreachable("Unsupported Modifier");
    case MCSymbolRefExpr::VK_None:
      return ELF::R_ARM_THM_MOVT_ABS;
    case MCSymbolRefExpr::VK_ARM_SBREL:
      return ELF::R_ARM_THM_MOVT_BREL;
    }
  case ARM::fixup_t2_movw_lo16:
    switch (Modifier) {
    default:
      llvm_unreachable("Unsupported Modifier");
    case MCSymbolRefExpr::VK_None:
      return ELF::R_ARM_THM_MOVW_ABS_NC;
    case MCSymbolRefExpr::VK_ARM_SBREL:
      return ELF::R_ARM_THM_MOVW_BREL_NC;
    }
  }
}

void ARMELFObjectWriter::addTargetSectionFlags(MCContext &Ctx,
                                               MCSectionELF &Sec) {
  // The mix of execute-only and non-execute-only at link time is
  // non-execute-only. To avoid the empty implicitly created .text
  // section from making the whole .text section non-execute-only, we
  // mark it execute-only if it is empty and there is at least one
  // execute-only section in the object.
  MCSectionELF *TextSection =
      static_cast<MCSectionELF *>(Ctx.getObjectFileInfo()->getTextSection());
  if (Sec.getKind().isExecuteOnly() && !TextSection->hasInstructions() &&
      !TextSection->hasData()) {
    TextSection->setFlags(TextSection->getFlags() | ELF::SHF_ARM_PURECODE);
  }
}

std::unique_ptr<MCObjectTargetWriter>
llvm::createARMELFObjectWriter(uint8_t OSABI) {
  return llvm::make_unique<ARMELFObjectWriter>(OSABI);
}
