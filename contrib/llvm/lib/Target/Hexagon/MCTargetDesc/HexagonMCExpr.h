//==- HexagonMCExpr.h - Hexagon specific MC expression classes --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_HEXAGON_HEXAGONMCEXPR_H
#define LLVM_LIB_TARGET_HEXAGON_HEXAGONMCEXPR_H

#include "llvm/MC/MCExpr.h"

namespace llvm {
class MCInst;
class HexagonMCExpr : public MCTargetExpr {
public:
  static HexagonMCExpr *create(MCExpr const *Expr, MCContext &Ctx);
  void printImpl(raw_ostream &OS, const MCAsmInfo *MAI) const override;
  bool evaluateAsRelocatableImpl(MCValue &Res, const MCAsmLayout *Layout,
                                 const MCFixup *Fixup) const override;
  void visitUsedExpr(MCStreamer &Streamer) const override;
  MCFragment *findAssociatedFragment() const override;
  void fixELFSymbolsInTLSFixups(MCAssembler &Asm) const override;
  static bool classof(MCExpr const *E);
  MCExpr const *getExpr() const;
  void setMustExtend(bool Val = true);
  bool mustExtend() const;
  void setMustNotExtend(bool Val = true);
  bool mustNotExtend() const;
  void setS27_2_reloc(bool Val = true);
  bool s27_2_reloc() const;
  void setSignMismatch(bool Val = true);
  bool signMismatch() const;

private:
  HexagonMCExpr(MCExpr const *Expr);
  MCExpr const *Expr;
  bool MustNotExtend;
  bool MustExtend;
  bool S27_2_reloc;
  bool SignMismatch;
};
} // end namespace llvm

#endif // LLVM_LIB_TARGET_HEXAGON_HEXAGONMCEXPR_H
