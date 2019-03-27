//===-- AVRMCExpr.cpp - AVR specific MC expression classes ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "AVRMCExpr.h"

#include "llvm/MC/MCAsmLayout.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCValue.h"

namespace llvm {

namespace {

const struct ModifierEntry {
  const char * const Spelling;
  AVRMCExpr::VariantKind VariantKind;
} ModifierNames[] = {
    {"lo8", AVRMCExpr::VK_AVR_LO8},       {"hi8", AVRMCExpr::VK_AVR_HI8},
    {"hh8", AVRMCExpr::VK_AVR_HH8}, // synonym with hlo8
    {"hlo8", AVRMCExpr::VK_AVR_HH8},      {"hhi8", AVRMCExpr::VK_AVR_HHI8},

    {"pm_lo8", AVRMCExpr::VK_AVR_PM_LO8}, {"pm_hi8", AVRMCExpr::VK_AVR_PM_HI8},
    {"pm_hh8", AVRMCExpr::VK_AVR_PM_HH8},

    {"lo8_gs", AVRMCExpr::VK_AVR_LO8_GS}, {"hi8_gs", AVRMCExpr::VK_AVR_HI8_GS},
    {"gs", AVRMCExpr::VK_AVR_GS},
};

} // end of anonymous namespace

const AVRMCExpr *AVRMCExpr::create(VariantKind Kind, const MCExpr *Expr,
                                   bool Negated, MCContext &Ctx) {
  return new (Ctx) AVRMCExpr(Kind, Expr, Negated);
}

void AVRMCExpr::printImpl(raw_ostream &OS, const MCAsmInfo *MAI) const {
  assert(Kind != VK_AVR_None);

  if (isNegated())
    OS << '-';

  OS << getName() << '(';
  getSubExpr()->print(OS, MAI);
  OS << ')';
}

bool AVRMCExpr::evaluateAsConstant(int64_t &Result) const {
  MCValue Value;

  bool isRelocatable =
      getSubExpr()->evaluateAsRelocatable(Value, nullptr, nullptr);

  if (!isRelocatable)
    return false;

  if (Value.isAbsolute()) {
    Result = evaluateAsInt64(Value.getConstant());
    return true;
  }

  return false;
}

bool AVRMCExpr::evaluateAsRelocatableImpl(MCValue &Result,
                                          const MCAsmLayout *Layout,
                                          const MCFixup *Fixup) const {
  MCValue Value;
  bool isRelocatable = SubExpr->evaluateAsRelocatable(Value, Layout, Fixup);

  if (!isRelocatable)
    return false;

  if (Value.isAbsolute()) {
    Result = MCValue::get(evaluateAsInt64(Value.getConstant()));
  } else {
    if (!Layout) return false;

    MCContext &Context = Layout->getAssembler().getContext();
    const MCSymbolRefExpr *Sym = Value.getSymA();
    MCSymbolRefExpr::VariantKind Modifier = Sym->getKind();
    if (Modifier != MCSymbolRefExpr::VK_None)
      return false;

    Sym = MCSymbolRefExpr::create(&Sym->getSymbol(), Modifier, Context);
    Result = MCValue::get(Sym, Value.getSymB(), Value.getConstant());
  }

  return true;
}

int64_t AVRMCExpr::evaluateAsInt64(int64_t Value) const {
  if (Negated)
    Value *= -1;

  switch (Kind) {
  case AVRMCExpr::VK_AVR_LO8:
    Value &= 0xff;
    break;
  case AVRMCExpr::VK_AVR_HI8:
    Value &= 0xff00;
    Value >>= 8;
    break;
  case AVRMCExpr::VK_AVR_HH8:
    Value &= 0xff0000;
    Value >>= 16;
    break;
  case AVRMCExpr::VK_AVR_HHI8:
    Value &= 0xff000000;
    Value >>= 24;
    break;
  case AVRMCExpr::VK_AVR_PM_LO8:
  case AVRMCExpr::VK_AVR_LO8_GS:
    Value >>= 1; // Program memory addresses must always be shifted by one.
    Value &= 0xff;
    break;
  case AVRMCExpr::VK_AVR_PM_HI8:
  case AVRMCExpr::VK_AVR_HI8_GS:
    Value >>= 1; // Program memory addresses must always be shifted by one.
    Value &= 0xff00;
    Value >>= 8;
    break;
  case AVRMCExpr::VK_AVR_PM_HH8:
    Value >>= 1; // Program memory addresses must always be shifted by one.
    Value &= 0xff0000;
    Value >>= 16;
    break;
  case AVRMCExpr::VK_AVR_GS:
    Value >>= 1; // Program memory addresses must always be shifted by one.
    break;

  case AVRMCExpr::VK_AVR_None:
    llvm_unreachable("Uninitialized expression.");
  }
  return static_cast<uint64_t>(Value) & 0xff;
}

AVR::Fixups AVRMCExpr::getFixupKind() const {
  AVR::Fixups Kind = AVR::Fixups::LastTargetFixupKind;

  switch (getKind()) {
  case VK_AVR_LO8:
    Kind = isNegated() ? AVR::fixup_lo8_ldi_neg : AVR::fixup_lo8_ldi;
    break;
  case VK_AVR_HI8:
    Kind = isNegated() ? AVR::fixup_hi8_ldi_neg : AVR::fixup_hi8_ldi;
    break;
  case VK_AVR_HH8:
    Kind = isNegated() ? AVR::fixup_hh8_ldi_neg : AVR::fixup_hh8_ldi;
    break;
  case VK_AVR_HHI8:
    Kind = isNegated() ? AVR::fixup_ms8_ldi_neg : AVR::fixup_ms8_ldi;
    break;

  case VK_AVR_PM_LO8:
    Kind = isNegated() ? AVR::fixup_lo8_ldi_pm_neg : AVR::fixup_lo8_ldi_pm;
    break;
  case VK_AVR_PM_HI8:
    Kind = isNegated() ? AVR::fixup_hi8_ldi_pm_neg : AVR::fixup_hi8_ldi_pm;
    break;
  case VK_AVR_PM_HH8:
    Kind = isNegated() ? AVR::fixup_hh8_ldi_pm_neg : AVR::fixup_hh8_ldi_pm;
    break;
  case VK_AVR_GS:
    Kind = AVR::fixup_16_pm;
    break;
  case VK_AVR_LO8_GS:
    Kind = AVR::fixup_lo8_ldi_gs;
    break;
  case VK_AVR_HI8_GS:
    Kind = AVR::fixup_hi8_ldi_gs;
    break;

  case VK_AVR_None:
    llvm_unreachable("Uninitialized expression");
  }

  return Kind;
}

void AVRMCExpr::visitUsedExpr(MCStreamer &Streamer) const {
  Streamer.visitUsedExpr(*getSubExpr());
}

const char *AVRMCExpr::getName() const {
  const auto &Modifier = std::find_if(
      std::begin(ModifierNames), std::end(ModifierNames),
      [this](ModifierEntry const &Mod) { return Mod.VariantKind == Kind; });

  if (Modifier != std::end(ModifierNames)) {
    return Modifier->Spelling;
  }
  return nullptr;
}

AVRMCExpr::VariantKind AVRMCExpr::getKindByName(StringRef Name) {
  const auto &Modifier = std::find_if(
      std::begin(ModifierNames), std::end(ModifierNames),
      [&Name](ModifierEntry const &Mod) { return Mod.Spelling == Name; });

  if (Modifier != std::end(ModifierNames)) {
    return Modifier->VariantKind;
  }
  return VK_AVR_None;
}

} // end of namespace llvm

