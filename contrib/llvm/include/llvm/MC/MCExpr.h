//===- MCExpr.h - Assembly Level Expressions --------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCEXPR_H
#define LLVM_MC_MCEXPR_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/SMLoc.h"
#include <cstdint>

namespace llvm {

class MCAsmInfo;
class MCAsmLayout;
class MCAssembler;
class MCContext;
class MCFixup;
class MCFragment;
class MCSection;
class MCStreamer;
class MCSymbol;
class MCValue;
class raw_ostream;
class StringRef;

using SectionAddrMap = DenseMap<const MCSection *, uint64_t>;

/// Base class for the full range of assembler expressions which are
/// needed for parsing.
class MCExpr {
public:
  enum ExprKind {
    Binary,    ///< Binary expressions.
    Constant,  ///< Constant expressions.
    SymbolRef, ///< References to labels and assigned expressions.
    Unary,     ///< Unary expressions.
    Target     ///< Target specific expression.
  };

private:
  ExprKind Kind;
  SMLoc Loc;

  bool evaluateAsAbsolute(int64_t &Res, const MCAssembler *Asm,
                          const MCAsmLayout *Layout,
                          const SectionAddrMap *Addrs) const;

  bool evaluateAsAbsolute(int64_t &Res, const MCAssembler *Asm,
                          const MCAsmLayout *Layout,
                          const SectionAddrMap *Addrs, bool InSet) const;

protected:
  explicit MCExpr(ExprKind Kind, SMLoc Loc) : Kind(Kind), Loc(Loc) {}

  bool evaluateAsRelocatableImpl(MCValue &Res, const MCAssembler *Asm,
                                 const MCAsmLayout *Layout,
                                 const MCFixup *Fixup,
                                 const SectionAddrMap *Addrs, bool InSet) const;

public:
  MCExpr(const MCExpr &) = delete;
  MCExpr &operator=(const MCExpr &) = delete;

  /// \name Accessors
  /// @{

  ExprKind getKind() const { return Kind; }
  SMLoc getLoc() const { return Loc; }

  /// @}
  /// \name Utility Methods
  /// @{

  void print(raw_ostream &OS, const MCAsmInfo *MAI,
             bool InParens = false) const;
  void dump() const;

  /// @}
  /// \name Expression Evaluation
  /// @{

  /// Try to evaluate the expression to an absolute value.
  ///
  /// \param Res - The absolute value, if evaluation succeeds.
  /// \param Layout - The assembler layout object to use for evaluating symbol
  /// values. If not given, then only non-symbolic expressions will be
  /// evaluated.
  /// \return - True on success.
  bool evaluateAsAbsolute(int64_t &Res, const MCAsmLayout &Layout,
                          const SectionAddrMap &Addrs) const;
  bool evaluateAsAbsolute(int64_t &Res) const;
  bool evaluateAsAbsolute(int64_t &Res, const MCAssembler &Asm) const;
  bool evaluateAsAbsolute(int64_t &Res, const MCAssembler *Asm) const;
  bool evaluateAsAbsolute(int64_t &Res, const MCAsmLayout &Layout) const;

  bool evaluateKnownAbsolute(int64_t &Res, const MCAsmLayout &Layout) const;

  /// Try to evaluate the expression to a relocatable value, i.e. an
  /// expression of the fixed form (a - b + constant).
  ///
  /// \param Res - The relocatable value, if evaluation succeeds.
  /// \param Layout - The assembler layout object to use for evaluating values.
  /// \param Fixup - The Fixup object if available.
  /// \return - True on success.
  bool evaluateAsRelocatable(MCValue &Res, const MCAsmLayout *Layout,
                             const MCFixup *Fixup) const;

  /// Try to evaluate the expression to the form (a - b + constant) where
  /// neither a nor b are variables.
  ///
  /// This is a more aggressive variant of evaluateAsRelocatable. The intended
  /// use is for when relocations are not available, like the .size directive.
  bool evaluateAsValue(MCValue &Res, const MCAsmLayout &Layout) const;

  /// Find the "associated section" for this expression, which is
  /// currently defined as the absolute section for constants, or
  /// otherwise the section associated with the first defined symbol in the
  /// expression.
  MCFragment *findAssociatedFragment() const;

  /// @}
};

inline raw_ostream &operator<<(raw_ostream &OS, const MCExpr &E) {
  E.print(OS, nullptr);
  return OS;
}

////  Represent a constant integer expression.
class MCConstantExpr : public MCExpr {
  int64_t Value;

  explicit MCConstantExpr(int64_t Value)
      : MCExpr(MCExpr::Constant, SMLoc()), Value(Value) {}

public:
  /// \name Construction
  /// @{

  static const MCConstantExpr *create(int64_t Value, MCContext &Ctx);

  /// @}
  /// \name Accessors
  /// @{

  int64_t getValue() const { return Value; }

  /// @}

  static bool classof(const MCExpr *E) {
    return E->getKind() == MCExpr::Constant;
  }
};

///  Represent a reference to a symbol from inside an expression.
///
/// A symbol reference in an expression may be a use of a label, a use of an
/// assembler variable (defined constant), or constitute an implicit definition
/// of the symbol as external.
class MCSymbolRefExpr : public MCExpr {
public:
  enum VariantKind : uint16_t {
    VK_None,
    VK_Invalid,

    VK_GOT,
    VK_GOTOFF,
    VK_GOTREL,
    VK_GOTPCREL,
    VK_GOTTPOFF,
    VK_INDNTPOFF,
    VK_NTPOFF,
    VK_GOTNTPOFF,
    VK_PLT,
    VK_TLSGD,
    VK_TLSLD,
    VK_TLSLDM,
    VK_TPOFF,
    VK_DTPOFF,
    VK_TLSCALL,   // symbol(tlscall)
    VK_TLSDESC,   // symbol(tlsdesc)
    VK_TLVP,      // Mach-O thread local variable relocations
    VK_TLVPPAGE,
    VK_TLVPPAGEOFF,
    VK_PAGE,
    VK_PAGEOFF,
    VK_GOTPAGE,
    VK_GOTPAGEOFF,
    VK_SECREL,
    VK_SIZE,      // symbol@SIZE
    VK_WEAKREF,   // The link between the symbols in .weakref foo, bar

    VK_X86_ABS8,

    VK_ARM_NONE,
    VK_ARM_GOT_PREL,
    VK_ARM_TARGET1,
    VK_ARM_TARGET2,
    VK_ARM_PREL31,
    VK_ARM_SBREL,          // symbol(sbrel)
    VK_ARM_TLSLDO,         // symbol(tlsldo)
    VK_ARM_TLSDESCSEQ,

    VK_AVR_NONE,
    VK_AVR_LO8,
    VK_AVR_HI8,
    VK_AVR_HLO8,
    VK_AVR_DIFF8,
    VK_AVR_DIFF16,
    VK_AVR_DIFF32,

    VK_PPC_LO,             // symbol@l
    VK_PPC_HI,             // symbol@h
    VK_PPC_HA,             // symbol@ha
    VK_PPC_HIGH,           // symbol@high
    VK_PPC_HIGHA,          // symbol@higha
    VK_PPC_HIGHER,         // symbol@higher
    VK_PPC_HIGHERA,        // symbol@highera
    VK_PPC_HIGHEST,        // symbol@highest
    VK_PPC_HIGHESTA,       // symbol@highesta
    VK_PPC_GOT_LO,         // symbol@got@l
    VK_PPC_GOT_HI,         // symbol@got@h
    VK_PPC_GOT_HA,         // symbol@got@ha
    VK_PPC_TOCBASE,        // symbol@tocbase
    VK_PPC_TOC,            // symbol@toc
    VK_PPC_TOC_LO,         // symbol@toc@l
    VK_PPC_TOC_HI,         // symbol@toc@h
    VK_PPC_TOC_HA,         // symbol@toc@ha
    VK_PPC_DTPMOD,         // symbol@dtpmod
    VK_PPC_TPREL_LO,       // symbol@tprel@l
    VK_PPC_TPREL_HI,       // symbol@tprel@h
    VK_PPC_TPREL_HA,       // symbol@tprel@ha
    VK_PPC_TPREL_HIGH,     // symbol@tprel@high
    VK_PPC_TPREL_HIGHA,    // symbol@tprel@higha
    VK_PPC_TPREL_HIGHER,   // symbol@tprel@higher
    VK_PPC_TPREL_HIGHERA,  // symbol@tprel@highera
    VK_PPC_TPREL_HIGHEST,  // symbol@tprel@highest
    VK_PPC_TPREL_HIGHESTA, // symbol@tprel@highesta
    VK_PPC_DTPREL_LO,      // symbol@dtprel@l
    VK_PPC_DTPREL_HI,      // symbol@dtprel@h
    VK_PPC_DTPREL_HA,      // symbol@dtprel@ha
    VK_PPC_DTPREL_HIGH,    // symbol@dtprel@high
    VK_PPC_DTPREL_HIGHA,   // symbol@dtprel@higha
    VK_PPC_DTPREL_HIGHER,  // symbol@dtprel@higher
    VK_PPC_DTPREL_HIGHERA, // symbol@dtprel@highera
    VK_PPC_DTPREL_HIGHEST, // symbol@dtprel@highest
    VK_PPC_DTPREL_HIGHESTA,// symbol@dtprel@highesta
    VK_PPC_GOT_TPREL,      // symbol@got@tprel
    VK_PPC_GOT_TPREL_LO,   // symbol@got@tprel@l
    VK_PPC_GOT_TPREL_HI,   // symbol@got@tprel@h
    VK_PPC_GOT_TPREL_HA,   // symbol@got@tprel@ha
    VK_PPC_GOT_DTPREL,     // symbol@got@dtprel
    VK_PPC_GOT_DTPREL_LO,  // symbol@got@dtprel@l
    VK_PPC_GOT_DTPREL_HI,  // symbol@got@dtprel@h
    VK_PPC_GOT_DTPREL_HA,  // symbol@got@dtprel@ha
    VK_PPC_TLS,            // symbol@tls
    VK_PPC_GOT_TLSGD,      // symbol@got@tlsgd
    VK_PPC_GOT_TLSGD_LO,   // symbol@got@tlsgd@l
    VK_PPC_GOT_TLSGD_HI,   // symbol@got@tlsgd@h
    VK_PPC_GOT_TLSGD_HA,   // symbol@got@tlsgd@ha
    VK_PPC_TLSGD,          // symbol@tlsgd
    VK_PPC_GOT_TLSLD,      // symbol@got@tlsld
    VK_PPC_GOT_TLSLD_LO,   // symbol@got@tlsld@l
    VK_PPC_GOT_TLSLD_HI,   // symbol@got@tlsld@h
    VK_PPC_GOT_TLSLD_HA,   // symbol@got@tlsld@ha
    VK_PPC_TLSLD,          // symbol@tlsld
    VK_PPC_LOCAL,          // symbol@local

    VK_COFF_IMGREL32, // symbol@imgrel (image-relative)

    VK_Hexagon_PCREL,
    VK_Hexagon_LO16,
    VK_Hexagon_HI16,
    VK_Hexagon_GPREL,
    VK_Hexagon_GD_GOT,
    VK_Hexagon_LD_GOT,
    VK_Hexagon_GD_PLT,
    VK_Hexagon_LD_PLT,
    VK_Hexagon_IE,
    VK_Hexagon_IE_GOT,

    VK_WebAssembly_FUNCTION, // Function table index, rather than virtual addr
    VK_WebAssembly_GLOBAL,   // Global object index
    VK_WebAssembly_TYPEINDEX,// Type table index
    VK_WebAssembly_EVENT,    // Event index

    VK_AMDGPU_GOTPCREL32_LO, // symbol@gotpcrel32@lo
    VK_AMDGPU_GOTPCREL32_HI, // symbol@gotpcrel32@hi
    VK_AMDGPU_REL32_LO,      // symbol@rel32@lo
    VK_AMDGPU_REL32_HI,      // symbol@rel32@hi
    VK_AMDGPU_REL64,         // symbol@rel64

    VK_TPREL,
    VK_DTPREL
  };

private:
  /// The symbol reference modifier.
  const VariantKind Kind;

  /// Specifies how the variant kind should be printed.
  const unsigned UseParensForSymbolVariant : 1;

  // FIXME: Remove this bit.
  const unsigned HasSubsectionsViaSymbols : 1;

  /// The symbol being referenced.
  const MCSymbol *Symbol;

  explicit MCSymbolRefExpr(const MCSymbol *Symbol, VariantKind Kind,
                           const MCAsmInfo *MAI, SMLoc Loc = SMLoc());

public:
  /// \name Construction
  /// @{

  static const MCSymbolRefExpr *create(const MCSymbol *Symbol, MCContext &Ctx) {
    return MCSymbolRefExpr::create(Symbol, VK_None, Ctx);
  }

  static const MCSymbolRefExpr *create(const MCSymbol *Symbol, VariantKind Kind,
                                       MCContext &Ctx, SMLoc Loc = SMLoc());
  static const MCSymbolRefExpr *create(StringRef Name, VariantKind Kind,
                                       MCContext &Ctx);

  /// @}
  /// \name Accessors
  /// @{

  const MCSymbol &getSymbol() const { return *Symbol; }

  VariantKind getKind() const { return Kind; }

  void printVariantKind(raw_ostream &OS) const;

  bool hasSubsectionsViaSymbols() const { return HasSubsectionsViaSymbols; }

  /// @}
  /// \name Static Utility Functions
  /// @{

  static StringRef getVariantKindName(VariantKind Kind);

  static VariantKind getVariantKindForName(StringRef Name);

  /// @}

  static bool classof(const MCExpr *E) {
    return E->getKind() == MCExpr::SymbolRef;
  }
};

/// Unary assembler expressions.
class MCUnaryExpr : public MCExpr {
public:
  enum Opcode {
    LNot,  ///< Logical negation.
    Minus, ///< Unary minus.
    Not,   ///< Bitwise negation.
    Plus   ///< Unary plus.
  };

private:
  Opcode Op;
  const MCExpr *Expr;

  MCUnaryExpr(Opcode Op, const MCExpr *Expr, SMLoc Loc)
      : MCExpr(MCExpr::Unary, Loc), Op(Op), Expr(Expr) {}

public:
  /// \name Construction
  /// @{

  static const MCUnaryExpr *create(Opcode Op, const MCExpr *Expr,
                                   MCContext &Ctx, SMLoc Loc = SMLoc());

  static const MCUnaryExpr *createLNot(const MCExpr *Expr, MCContext &Ctx, SMLoc Loc = SMLoc()) {
    return create(LNot, Expr, Ctx, Loc);
  }

  static const MCUnaryExpr *createMinus(const MCExpr *Expr, MCContext &Ctx, SMLoc Loc = SMLoc()) {
    return create(Minus, Expr, Ctx, Loc);
  }

  static const MCUnaryExpr *createNot(const MCExpr *Expr, MCContext &Ctx, SMLoc Loc = SMLoc()) {
    return create(Not, Expr, Ctx, Loc);
  }

  static const MCUnaryExpr *createPlus(const MCExpr *Expr, MCContext &Ctx, SMLoc Loc = SMLoc()) {
    return create(Plus, Expr, Ctx, Loc);
  }

  /// @}
  /// \name Accessors
  /// @{

  /// Get the kind of this unary expression.
  Opcode getOpcode() const { return Op; }

  /// Get the child of this unary expression.
  const MCExpr *getSubExpr() const { return Expr; }

  /// @}

  static bool classof(const MCExpr *E) {
    return E->getKind() == MCExpr::Unary;
  }
};

/// Binary assembler expressions.
class MCBinaryExpr : public MCExpr {
public:
  enum Opcode {
    Add,  ///< Addition.
    And,  ///< Bitwise and.
    Div,  ///< Signed division.
    EQ,   ///< Equality comparison.
    GT,   ///< Signed greater than comparison (result is either 0 or some
          ///< target-specific non-zero value)
    GTE,  ///< Signed greater than or equal comparison (result is either 0 or
          ///< some target-specific non-zero value).
    LAnd, ///< Logical and.
    LOr,  ///< Logical or.
    LT,   ///< Signed less than comparison (result is either 0 or
          ///< some target-specific non-zero value).
    LTE,  ///< Signed less than or equal comparison (result is either 0 or
          ///< some target-specific non-zero value).
    Mod,  ///< Signed remainder.
    Mul,  ///< Multiplication.
    NE,   ///< Inequality comparison.
    Or,   ///< Bitwise or.
    Shl,  ///< Shift left.
    AShr, ///< Arithmetic shift right.
    LShr, ///< Logical shift right.
    Sub,  ///< Subtraction.
    Xor   ///< Bitwise exclusive or.
  };

private:
  Opcode Op;
  const MCExpr *LHS, *RHS;

  MCBinaryExpr(Opcode Op, const MCExpr *LHS, const MCExpr *RHS,
               SMLoc Loc = SMLoc())
      : MCExpr(MCExpr::Binary, Loc), Op(Op), LHS(LHS), RHS(RHS) {}

public:
  /// \name Construction
  /// @{

  static const MCBinaryExpr *create(Opcode Op, const MCExpr *LHS,
                                    const MCExpr *RHS, MCContext &Ctx,
                                    SMLoc Loc = SMLoc());

  static const MCBinaryExpr *createAdd(const MCExpr *LHS, const MCExpr *RHS,
                                       MCContext &Ctx) {
    return create(Add, LHS, RHS, Ctx);
  }

  static const MCBinaryExpr *createAnd(const MCExpr *LHS, const MCExpr *RHS,
                                       MCContext &Ctx) {
    return create(And, LHS, RHS, Ctx);
  }

  static const MCBinaryExpr *createDiv(const MCExpr *LHS, const MCExpr *RHS,
                                       MCContext &Ctx) {
    return create(Div, LHS, RHS, Ctx);
  }

  static const MCBinaryExpr *createEQ(const MCExpr *LHS, const MCExpr *RHS,
                                      MCContext &Ctx) {
    return create(EQ, LHS, RHS, Ctx);
  }

  static const MCBinaryExpr *createGT(const MCExpr *LHS, const MCExpr *RHS,
                                      MCContext &Ctx) {
    return create(GT, LHS, RHS, Ctx);
  }

  static const MCBinaryExpr *createGTE(const MCExpr *LHS, const MCExpr *RHS,
                                       MCContext &Ctx) {
    return create(GTE, LHS, RHS, Ctx);
  }

  static const MCBinaryExpr *createLAnd(const MCExpr *LHS, const MCExpr *RHS,
                                        MCContext &Ctx) {
    return create(LAnd, LHS, RHS, Ctx);
  }

  static const MCBinaryExpr *createLOr(const MCExpr *LHS, const MCExpr *RHS,
                                       MCContext &Ctx) {
    return create(LOr, LHS, RHS, Ctx);
  }

  static const MCBinaryExpr *createLT(const MCExpr *LHS, const MCExpr *RHS,
                                      MCContext &Ctx) {
    return create(LT, LHS, RHS, Ctx);
  }

  static const MCBinaryExpr *createLTE(const MCExpr *LHS, const MCExpr *RHS,
                                       MCContext &Ctx) {
    return create(LTE, LHS, RHS, Ctx);
  }

  static const MCBinaryExpr *createMod(const MCExpr *LHS, const MCExpr *RHS,
                                       MCContext &Ctx) {
    return create(Mod, LHS, RHS, Ctx);
  }

  static const MCBinaryExpr *createMul(const MCExpr *LHS, const MCExpr *RHS,
                                       MCContext &Ctx) {
    return create(Mul, LHS, RHS, Ctx);
  }

  static const MCBinaryExpr *createNE(const MCExpr *LHS, const MCExpr *RHS,
                                      MCContext &Ctx) {
    return create(NE, LHS, RHS, Ctx);
  }

  static const MCBinaryExpr *createOr(const MCExpr *LHS, const MCExpr *RHS,
                                      MCContext &Ctx) {
    return create(Or, LHS, RHS, Ctx);
  }

  static const MCBinaryExpr *createShl(const MCExpr *LHS, const MCExpr *RHS,
                                       MCContext &Ctx) {
    return create(Shl, LHS, RHS, Ctx);
  }

  static const MCBinaryExpr *createAShr(const MCExpr *LHS, const MCExpr *RHS,
                                       MCContext &Ctx) {
    return create(AShr, LHS, RHS, Ctx);
  }

  static const MCBinaryExpr *createLShr(const MCExpr *LHS, const MCExpr *RHS,
                                       MCContext &Ctx) {
    return create(LShr, LHS, RHS, Ctx);
  }

  static const MCBinaryExpr *createSub(const MCExpr *LHS, const MCExpr *RHS,
                                       MCContext &Ctx) {
    return create(Sub, LHS, RHS, Ctx);
  }

  static const MCBinaryExpr *createXor(const MCExpr *LHS, const MCExpr *RHS,
                                       MCContext &Ctx) {
    return create(Xor, LHS, RHS, Ctx);
  }

  /// @}
  /// \name Accessors
  /// @{

  /// Get the kind of this binary expression.
  Opcode getOpcode() const { return Op; }

  /// Get the left-hand side expression of the binary operator.
  const MCExpr *getLHS() const { return LHS; }

  /// Get the right-hand side expression of the binary operator.
  const MCExpr *getRHS() const { return RHS; }

  /// @}

  static bool classof(const MCExpr *E) {
    return E->getKind() == MCExpr::Binary;
  }
};

/// This is an extension point for target-specific MCExpr subclasses to
/// implement.
///
/// NOTE: All subclasses are required to have trivial destructors because
/// MCExprs are bump pointer allocated and not destructed.
class MCTargetExpr : public MCExpr {
  virtual void anchor();

protected:
  MCTargetExpr() : MCExpr(Target, SMLoc()) {}
  virtual ~MCTargetExpr() = default;

public:
  virtual void printImpl(raw_ostream &OS, const MCAsmInfo *MAI) const = 0;
  virtual bool evaluateAsRelocatableImpl(MCValue &Res,
                                         const MCAsmLayout *Layout,
                                         const MCFixup *Fixup) const = 0;
  // allow Target Expressions to be checked for equality
  virtual bool isEqualTo(const MCExpr *x) const { return false; }
  // This should be set when assigned expressions are not valid ".set"
  // expressions, e.g. registers, and must be inlined.
  virtual bool inlineAssignedExpr() const { return false; }
  virtual void visitUsedExpr(MCStreamer& Streamer) const = 0;
  virtual MCFragment *findAssociatedFragment() const = 0;

  virtual void fixELFSymbolsInTLSFixups(MCAssembler &) const = 0;

  static bool classof(const MCExpr *E) {
    return E->getKind() == MCExpr::Target;
  }
};

} // end namespace llvm

#endif // LLVM_MC_MCEXPR_H
