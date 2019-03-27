//===-- ARMMCExpr.h - ARM specific MC expression classes --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_MCTARGETDESC_ARMMCEXPR_H
#define LLVM_LIB_TARGET_ARM_MCTARGETDESC_ARMMCEXPR_H

#include "llvm/MC/MCExpr.h"

namespace llvm {

class ARMMCExpr : public MCTargetExpr {
public:
  enum VariantKind {
    VK_ARM_None,
    VK_ARM_HI16,  // The R_ARM_MOVT_ABS relocation (:upper16: in the .s file)
    VK_ARM_LO16   // The R_ARM_MOVW_ABS_NC relocation (:lower16: in the .s file)
  };

private:
  const VariantKind Kind;
  const MCExpr *Expr;

  explicit ARMMCExpr(VariantKind Kind, const MCExpr *Expr)
      : Kind(Kind), Expr(Expr) {}

public:
  /// @name Construction
  /// @{

  static const ARMMCExpr *create(VariantKind Kind, const MCExpr *Expr,
                                      MCContext &Ctx);

  static const ARMMCExpr *createUpper16(const MCExpr *Expr, MCContext &Ctx) {
    return create(VK_ARM_HI16, Expr, Ctx);
  }

  static const ARMMCExpr *createLower16(const MCExpr *Expr, MCContext &Ctx) {
    return create(VK_ARM_LO16, Expr, Ctx);
  }

  /// @}
  /// @name Accessors
  /// @{

  /// getOpcode - Get the kind of this expression.
  VariantKind getKind() const { return Kind; }

  /// getSubExpr - Get the child of this expression.
  const MCExpr *getSubExpr() const { return Expr; }

  /// @}

  void printImpl(raw_ostream &OS, const MCAsmInfo *MAI) const override;
  bool evaluateAsRelocatableImpl(MCValue &Res,
                                 const MCAsmLayout *Layout,
                                 const MCFixup *Fixup) const override {
    return false;
  }
  void visitUsedExpr(MCStreamer &Streamer) const override;
  MCFragment *findAssociatedFragment() const override {
    return getSubExpr()->findAssociatedFragment();
  }

  // There are no TLS ARMMCExprs at the moment.
  void fixELFSymbolsInTLSFixups(MCAssembler &Asm) const override {}

  static bool classof(const MCExpr *E) {
    return E->getKind() == MCExpr::Target;
  }
};
} // end namespace llvm

#endif
