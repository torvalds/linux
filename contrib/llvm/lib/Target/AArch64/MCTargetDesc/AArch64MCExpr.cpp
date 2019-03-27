//===-- AArch64MCExpr.cpp - AArch64 specific MC expression classes --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation of the assembly expression modifiers
// accepted by the AArch64 architecture (e.g. ":lo12:", ":gottprel_g1:", ...).
//
//===----------------------------------------------------------------------===//

#include "AArch64MCExpr.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define DEBUG_TYPE "aarch64symbolrefexpr"

const AArch64MCExpr *AArch64MCExpr::create(const MCExpr *Expr, VariantKind Kind,
                                       MCContext &Ctx) {
  return new (Ctx) AArch64MCExpr(Expr, Kind);
}

StringRef AArch64MCExpr::getVariantKindName() const {
  switch (static_cast<uint32_t>(getKind())) {
  case VK_CALL:                return "";
  case VK_LO12:                return ":lo12:";
  case VK_ABS_G3:              return ":abs_g3:";
  case VK_ABS_G2:              return ":abs_g2:";
  case VK_ABS_G2_S:            return ":abs_g2_s:";
  case VK_ABS_G2_NC:           return ":abs_g2_nc:";
  case VK_ABS_G1:              return ":abs_g1:";
  case VK_ABS_G1_S:            return ":abs_g1_s:";
  case VK_ABS_G1_NC:           return ":abs_g1_nc:";
  case VK_ABS_G0:              return ":abs_g0:";
  case VK_ABS_G0_S:            return ":abs_g0_s:";
  case VK_ABS_G0_NC:           return ":abs_g0_nc:";
  case VK_DTPREL_G2:           return ":dtprel_g2:";
  case VK_DTPREL_G1:           return ":dtprel_g1:";
  case VK_DTPREL_G1_NC:        return ":dtprel_g1_nc:";
  case VK_DTPREL_G0:           return ":dtprel_g0:";
  case VK_DTPREL_G0_NC:        return ":dtprel_g0_nc:";
  case VK_DTPREL_HI12:         return ":dtprel_hi12:";
  case VK_DTPREL_LO12:         return ":dtprel_lo12:";
  case VK_DTPREL_LO12_NC:      return ":dtprel_lo12_nc:";
  case VK_TPREL_G2:            return ":tprel_g2:";
  case VK_TPREL_G1:            return ":tprel_g1:";
  case VK_TPREL_G1_NC:         return ":tprel_g1_nc:";
  case VK_TPREL_G0:            return ":tprel_g0:";
  case VK_TPREL_G0_NC:         return ":tprel_g0_nc:";
  case VK_TPREL_HI12:          return ":tprel_hi12:";
  case VK_TPREL_LO12:          return ":tprel_lo12:";
  case VK_TPREL_LO12_NC:       return ":tprel_lo12_nc:";
  case VK_TLSDESC_LO12:        return ":tlsdesc_lo12:";
  case VK_ABS_PAGE:            return "";
  case VK_ABS_PAGE_NC:         return ":pg_hi21_nc:";
  case VK_GOT:                 return ":got:";
  case VK_GOT_PAGE:            return ":got:";
  case VK_GOT_LO12:            return ":got_lo12:";
  case VK_GOTTPREL:            return ":gottprel:";
  case VK_GOTTPREL_PAGE:       return ":gottprel:";
  case VK_GOTTPREL_LO12_NC:    return ":gottprel_lo12:";
  case VK_GOTTPREL_G1:         return ":gottprel_g1:";
  case VK_GOTTPREL_G0_NC:      return ":gottprel_g0_nc:";
  case VK_TLSDESC:             return "";
  case VK_TLSDESC_PAGE:        return ":tlsdesc:";
  case VK_SECREL_LO12:         return ":secrel_lo12:";
  case VK_SECREL_HI12:         return ":secrel_hi12:";
  default:
    llvm_unreachable("Invalid ELF symbol kind");
  }
}

void AArch64MCExpr::printImpl(raw_ostream &OS, const MCAsmInfo *MAI) const {
  if (getKind() != VK_NONE)
    OS << getVariantKindName();
  Expr->print(OS, MAI);
}

void AArch64MCExpr::visitUsedExpr(MCStreamer &Streamer) const {
  Streamer.visitUsedExpr(*getSubExpr());
}

MCFragment *AArch64MCExpr::findAssociatedFragment() const {
  llvm_unreachable("FIXME: what goes here?");
}

bool AArch64MCExpr::evaluateAsRelocatableImpl(MCValue &Res,
                                              const MCAsmLayout *Layout,
                                              const MCFixup *Fixup) const {
  if (!getSubExpr()->evaluateAsRelocatable(Res, Layout, Fixup))
    return false;

  Res =
      MCValue::get(Res.getSymA(), Res.getSymB(), Res.getConstant(), getKind());

  return true;
}

static void fixELFSymbolsInTLSFixupsImpl(const MCExpr *Expr, MCAssembler &Asm) {
  switch (Expr->getKind()) {
  case MCExpr::Target:
    llvm_unreachable("Can't handle nested target expression");
    break;
  case MCExpr::Constant:
    break;

  case MCExpr::Binary: {
    const MCBinaryExpr *BE = cast<MCBinaryExpr>(Expr);
    fixELFSymbolsInTLSFixupsImpl(BE->getLHS(), Asm);
    fixELFSymbolsInTLSFixupsImpl(BE->getRHS(), Asm);
    break;
  }

  case MCExpr::SymbolRef: {
    // We're known to be under a TLS fixup, so any symbol should be
    // modified. There should be only one.
    const MCSymbolRefExpr &SymRef = *cast<MCSymbolRefExpr>(Expr);
    cast<MCSymbolELF>(SymRef.getSymbol()).setType(ELF::STT_TLS);
    break;
  }

  case MCExpr::Unary:
    fixELFSymbolsInTLSFixupsImpl(cast<MCUnaryExpr>(Expr)->getSubExpr(), Asm);
    break;
  }
}

void AArch64MCExpr::fixELFSymbolsInTLSFixups(MCAssembler &Asm) const {
  switch (getSymbolLoc(Kind)) {
  default:
    return;
  case VK_DTPREL:
  case VK_GOTTPREL:
  case VK_TPREL:
  case VK_TLSDESC:
    break;
  }

  fixELFSymbolsInTLSFixupsImpl(getSubExpr(), Asm);
}
