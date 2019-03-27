//===-- LanaiMCExpr.cpp - Lanai specific MC expression classes ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "LanaiMCExpr.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCStreamer.h"
using namespace llvm;

#define DEBUG_TYPE "lanaimcexpr"

const LanaiMCExpr *LanaiMCExpr::create(VariantKind Kind, const MCExpr *Expr,
                                       MCContext &Ctx) {
  return new (Ctx) LanaiMCExpr(Kind, Expr);
}

void LanaiMCExpr::printImpl(raw_ostream &OS, const MCAsmInfo *MAI) const {
  if (Kind == VK_Lanai_None) {
    Expr->print(OS, MAI);
    return;
  }

  switch (Kind) {
  default:
    llvm_unreachable("Invalid kind!");
  case VK_Lanai_ABS_HI:
    OS << "hi";
    break;
  case VK_Lanai_ABS_LO:
    OS << "lo";
    break;
  }

  OS << '(';
  const MCExpr *Expr = getSubExpr();
  Expr->print(OS, MAI);
  OS << ')';
}

void LanaiMCExpr::visitUsedExpr(MCStreamer &Streamer) const {
  Streamer.visitUsedExpr(*getSubExpr());
}

bool LanaiMCExpr::evaluateAsRelocatableImpl(MCValue &Res,
                                            const MCAsmLayout *Layout,
                                            const MCFixup *Fixup) const {
  if (!getSubExpr()->evaluateAsRelocatable(Res, Layout, Fixup))
    return false;

  Res =
      MCValue::get(Res.getSymA(), Res.getSymB(), Res.getConstant(), getKind());

  return true;
}
