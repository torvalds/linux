//===-- PPCMCExpr.h - PPC specific MC expression classes --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_POWERPC_MCTARGETDESC_PPCMCEXPR_H
#define LLVM_LIB_TARGET_POWERPC_MCTARGETDESC_PPCMCEXPR_H

#include "llvm/MC/MCAsmLayout.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCValue.h"

namespace llvm {

class PPCMCExpr : public MCTargetExpr {
public:
  enum VariantKind {
    VK_PPC_None,
    VK_PPC_LO,
    VK_PPC_HI,
    VK_PPC_HA,
    VK_PPC_HIGH,
    VK_PPC_HIGHA,
    VK_PPC_HIGHER,
    VK_PPC_HIGHERA,
    VK_PPC_HIGHEST,
    VK_PPC_HIGHESTA
  };

private:
  const VariantKind Kind;
  const MCExpr *Expr;
  bool IsDarwin;

  int64_t evaluateAsInt64(int64_t Value) const;

  explicit PPCMCExpr(VariantKind Kind, const MCExpr *Expr, bool IsDarwin)
      : Kind(Kind), Expr(Expr), IsDarwin(IsDarwin) {}

public:
  /// @name Construction
  /// @{

  static const PPCMCExpr *create(VariantKind Kind, const MCExpr *Expr,
                                 bool isDarwin, MCContext &Ctx);

  static const PPCMCExpr *createLo(const MCExpr *Expr,
                                   bool isDarwin, MCContext &Ctx) {
    return create(VK_PPC_LO, Expr, isDarwin, Ctx);
  }

  static const PPCMCExpr *createHi(const MCExpr *Expr,
                                   bool isDarwin, MCContext &Ctx) {
    return create(VK_PPC_HI, Expr, isDarwin, Ctx);
  }

  static const PPCMCExpr *createHa(const MCExpr *Expr,
                                   bool isDarwin, MCContext &Ctx) {
    return create(VK_PPC_HA, Expr, isDarwin, Ctx);
  }

  /// @}
  /// @name Accessors
  /// @{

  /// getOpcode - Get the kind of this expression.
  VariantKind getKind() const { return Kind; }

  /// getSubExpr - Get the child of this expression.
  const MCExpr *getSubExpr() const { return Expr; }

  /// isDarwinSyntax - True if expression is to be printed using Darwin syntax.
  bool isDarwinSyntax() const { return IsDarwin; }


  /// @}

  void printImpl(raw_ostream &OS, const MCAsmInfo *MAI) const override;
  bool evaluateAsRelocatableImpl(MCValue &Res,
                                 const MCAsmLayout *Layout,
                                 const MCFixup *Fixup) const override;
  void visitUsedExpr(MCStreamer &Streamer) const override;
  MCFragment *findAssociatedFragment() const override {
    return getSubExpr()->findAssociatedFragment();
  }

  // There are no TLS PPCMCExprs at the moment.
  void fixELFSymbolsInTLSFixups(MCAssembler &Asm) const override {}

  bool evaluateAsConstant(int64_t &Res) const;

  static bool classof(const MCExpr *E) {
    return E->getKind() == MCExpr::Target;
  }
};
} // end namespace llvm

#endif
