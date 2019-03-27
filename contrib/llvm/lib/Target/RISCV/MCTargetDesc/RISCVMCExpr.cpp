//===-- RISCVMCExpr.cpp - RISCV specific MC expression classes ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation of the assembly expression modifiers
// accepted by the RISCV architecture (e.g. ":lo12:", ":gottprel_g1:", ...).
//
//===----------------------------------------------------------------------===//

#include "RISCV.h"
#include "RISCVMCExpr.h"
#include "RISCVFixupKinds.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define DEBUG_TYPE "riscvmcexpr"

const RISCVMCExpr *RISCVMCExpr::create(const MCExpr *Expr, VariantKind Kind,
                                       MCContext &Ctx) {
  return new (Ctx) RISCVMCExpr(Expr, Kind);
}

void RISCVMCExpr::printImpl(raw_ostream &OS, const MCAsmInfo *MAI) const {
  bool HasVariant =
      ((getKind() != VK_RISCV_None) && (getKind() != VK_RISCV_CALL));
  if (HasVariant)
    OS << '%' << getVariantKindName(getKind()) << '(';
  Expr->print(OS, MAI);
  if (HasVariant)
    OS << ')';
}

const MCFixup *RISCVMCExpr::getPCRelHiFixup() const {
  MCValue AUIPCLoc;
  if (!getSubExpr()->evaluateAsRelocatable(AUIPCLoc, nullptr, nullptr))
    return nullptr;

  const MCSymbolRefExpr *AUIPCSRE = AUIPCLoc.getSymA();
  if (!AUIPCSRE)
    return nullptr;

  const auto *DF =
      dyn_cast_or_null<MCDataFragment>(AUIPCSRE->findAssociatedFragment());
  if (!DF)
    return nullptr;

  const MCSymbol *AUIPCSymbol = &AUIPCSRE->getSymbol();
  for (const MCFixup &F : DF->getFixups()) {
    if (F.getOffset() != AUIPCSymbol->getOffset())
      continue;

    switch ((unsigned)F.getKind()) {
    default:
      continue;
    case RISCV::fixup_riscv_pcrel_hi20:
      return &F;
    }
  }

  return nullptr;
}

bool RISCVMCExpr::evaluatePCRelLo(MCValue &Res, const MCAsmLayout *Layout,
                                  const MCFixup *Fixup) const {
  // VK_RISCV_PCREL_LO has to be handled specially.  The MCExpr inside is
  // actually the location of a auipc instruction with a VK_RISCV_PCREL_HI fixup
  // pointing to the real target.  We need to generate an MCValue in the form of
  // (<real target> + <offset from this fixup to the auipc fixup>).  The Fixup
  // is pcrel relative to the VK_RISCV_PCREL_LO fixup, so we need to add the
  // offset to the VK_RISCV_PCREL_HI Fixup from VK_RISCV_PCREL_LO to correct.
  MCValue AUIPCLoc;
  if (!getSubExpr()->evaluateAsValue(AUIPCLoc, *Layout))
    return false;

  const MCSymbolRefExpr *AUIPCSRE = AUIPCLoc.getSymA();
  // Don't try to evaluate %pcrel_hi/%pcrel_lo pairs that cross fragment
  // boundries.
  if (!AUIPCSRE ||
      findAssociatedFragment() != AUIPCSRE->findAssociatedFragment())
    return false;

  const MCSymbol *AUIPCSymbol = &AUIPCSRE->getSymbol();
  if (!AUIPCSymbol)
    return false;

  const MCFixup *TargetFixup = getPCRelHiFixup();
  if (!TargetFixup)
    return false;

  if ((unsigned)TargetFixup->getKind() != RISCV::fixup_riscv_pcrel_hi20)
    return false;

  MCValue Target;
  if (!TargetFixup->getValue()->evaluateAsValue(Target, *Layout))
    return false;

  if (!Target.getSymA() || !Target.getSymA()->getSymbol().isInSection())
    return false;

  if (&Target.getSymA()->getSymbol().getSection() !=
      findAssociatedFragment()->getParent())
    return false;

  uint64_t AUIPCOffset = AUIPCSymbol->getOffset();

  Res = MCValue::get(Target.getSymA(), nullptr,
                     Target.getConstant() + (Fixup->getOffset() - AUIPCOffset));
  return true;
}

bool RISCVMCExpr::evaluateAsRelocatableImpl(MCValue &Res,
                                            const MCAsmLayout *Layout,
                                            const MCFixup *Fixup) const {
  if (Kind == VK_RISCV_PCREL_LO && evaluatePCRelLo(Res, Layout, Fixup))
    return true;

  if (!getSubExpr()->evaluateAsRelocatable(Res, Layout, Fixup))
    return false;

  // Some custom fixup types are not valid with symbol difference expressions
  if (Res.getSymA() && Res.getSymB()) {
    switch (getKind()) {
    default:
      return true;
    case VK_RISCV_LO:
    case VK_RISCV_HI:
    case VK_RISCV_PCREL_LO:
    case VK_RISCV_PCREL_HI:
      return false;
    }
  }

  return true;
}

void RISCVMCExpr::visitUsedExpr(MCStreamer &Streamer) const {
  Streamer.visitUsedExpr(*getSubExpr());
}

RISCVMCExpr::VariantKind RISCVMCExpr::getVariantKindForName(StringRef name) {
  return StringSwitch<RISCVMCExpr::VariantKind>(name)
      .Case("lo", VK_RISCV_LO)
      .Case("hi", VK_RISCV_HI)
      .Case("pcrel_lo", VK_RISCV_PCREL_LO)
      .Case("pcrel_hi", VK_RISCV_PCREL_HI)
      .Default(VK_RISCV_Invalid);
}

StringRef RISCVMCExpr::getVariantKindName(VariantKind Kind) {
  switch (Kind) {
  default:
    llvm_unreachable("Invalid ELF symbol kind");
  case VK_RISCV_LO:
    return "lo";
  case VK_RISCV_HI:
    return "hi";
  case VK_RISCV_PCREL_LO:
    return "pcrel_lo";
  case VK_RISCV_PCREL_HI:
    return "pcrel_hi";
  }
}

bool RISCVMCExpr::evaluateAsConstant(int64_t &Res) const {
  MCValue Value;

  if (Kind == VK_RISCV_PCREL_HI || Kind == VK_RISCV_PCREL_LO ||
      Kind == VK_RISCV_CALL)
    return false;

  if (!getSubExpr()->evaluateAsRelocatable(Value, nullptr, nullptr))
    return false;

  if (!Value.isAbsolute())
    return false;

  Res = evaluateAsInt64(Value.getConstant());
  return true;
}

int64_t RISCVMCExpr::evaluateAsInt64(int64_t Value) const {
  switch (Kind) {
  default:
    llvm_unreachable("Invalid kind");
  case VK_RISCV_LO:
    return SignExtend64<12>(Value);
  case VK_RISCV_HI:
    // Add 1 if bit 11 is 1, to compensate for low 12 bits being negative.
    return ((Value + 0x800) >> 12) & 0xfffff;
  }
}
