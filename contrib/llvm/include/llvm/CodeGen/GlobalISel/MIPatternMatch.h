//== ----- llvm/CodeGen/GlobalISel/MIPatternMatch.h --------------------- == //
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// Contains matchers for matching SSA Machine Instructions.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_GMIR_PATTERNMATCH_H
#define LLVM_GMIR_PATTERNMATCH_H

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"

namespace llvm {
namespace MIPatternMatch {

template <typename Reg, typename Pattern>
bool mi_match(Reg R, MachineRegisterInfo &MRI, Pattern &&P) {
  return P.match(MRI, R);
}

// TODO: Extend for N use.
template <typename SubPatternT> struct OneUse_match {
  SubPatternT SubPat;
  OneUse_match(const SubPatternT &SP) : SubPat(SP) {}

  template <typename OpTy>
  bool match(const MachineRegisterInfo &MRI, unsigned Reg) {
    return MRI.hasOneUse(Reg) && SubPat.match(MRI, Reg);
  }
};

template <typename SubPat>
inline OneUse_match<SubPat> m_OneUse(const SubPat &SP) {
  return SP;
}

struct ConstantMatch {
  int64_t &CR;
  ConstantMatch(int64_t &C) : CR(C) {}
  bool match(const MachineRegisterInfo &MRI, unsigned Reg) {
    if (auto MaybeCst = getConstantVRegVal(Reg, MRI)) {
      CR = *MaybeCst;
      return true;
    }
    return false;
  }
};

inline ConstantMatch m_ICst(int64_t &Cst) { return ConstantMatch(Cst); }

// TODO: Rework this for different kinds of MachineOperand.
// Currently assumes the Src for a match is a register.
// We might want to support taking in some MachineOperands and call getReg on
// that.

struct operand_type_match {
  bool match(const MachineRegisterInfo &MRI, unsigned Reg) { return true; }
  bool match(const MachineRegisterInfo &MRI, MachineOperand *MO) {
    return MO->isReg();
  }
};

inline operand_type_match m_Reg() { return operand_type_match(); }

/// Matching combinators.
template <typename... Preds> struct And {
  template <typename MatchSrc>
  bool match(MachineRegisterInfo &MRI, MatchSrc &&src) {
    return true;
  }
};

template <typename Pred, typename... Preds>
struct And<Pred, Preds...> : And<Preds...> {
  Pred P;
  And(Pred &&p, Preds &&... preds)
      : And<Preds...>(std::forward<Preds>(preds)...), P(std::forward<Pred>(p)) {
  }
  template <typename MatchSrc>
  bool match(MachineRegisterInfo &MRI, MatchSrc &&src) {
    return P.match(MRI, src) && And<Preds...>::match(MRI, src);
  }
};

template <typename... Preds> struct Or {
  template <typename MatchSrc>
  bool match(MachineRegisterInfo &MRI, MatchSrc &&src) {
    return false;
  }
};

template <typename Pred, typename... Preds>
struct Or<Pred, Preds...> : Or<Preds...> {
  Pred P;
  Or(Pred &&p, Preds &&... preds)
      : Or<Preds...>(std::forward<Preds>(preds)...), P(std::forward<Pred>(p)) {}
  template <typename MatchSrc>
  bool match(MachineRegisterInfo &MRI, MatchSrc &&src) {
    return P.match(MRI, src) || Or<Preds...>::match(MRI, src);
  }
};

template <typename... Preds> And<Preds...> m_all_of(Preds &&... preds) {
  return And<Preds...>(std::forward<Preds>(preds)...);
}

template <typename... Preds> Or<Preds...> m_any_of(Preds &&... preds) {
  return Or<Preds...>(std::forward<Preds>(preds)...);
}

template <typename BindTy> struct bind_helper {
  static bool bind(const MachineRegisterInfo &MRI, BindTy &VR, BindTy &V) {
    VR = V;
    return true;
  }
};

template <> struct bind_helper<MachineInstr *> {
  static bool bind(const MachineRegisterInfo &MRI, MachineInstr *&MI,
                   unsigned Reg) {
    MI = MRI.getVRegDef(Reg);
    if (MI)
      return true;
    return false;
  }
};

template <> struct bind_helper<LLT> {
  static bool bind(const MachineRegisterInfo &MRI, LLT &Ty, unsigned Reg) {
    Ty = MRI.getType(Reg);
    if (Ty.isValid())
      return true;
    return false;
  }
};

template <> struct bind_helper<const ConstantFP *> {
  static bool bind(const MachineRegisterInfo &MRI, const ConstantFP *&F,
                   unsigned Reg) {
    F = getConstantFPVRegVal(Reg, MRI);
    if (F)
      return true;
    return false;
  }
};

template <typename Class> struct bind_ty {
  Class &VR;

  bind_ty(Class &V) : VR(V) {}

  template <typename ITy> bool match(const MachineRegisterInfo &MRI, ITy &&V) {
    return bind_helper<Class>::bind(MRI, VR, V);
  }
};

inline bind_ty<unsigned> m_Reg(unsigned &R) { return R; }
inline bind_ty<MachineInstr *> m_MInstr(MachineInstr *&MI) { return MI; }
inline bind_ty<LLT> m_Type(LLT &Ty) { return Ty; }

// Helper for matching G_FCONSTANT
inline bind_ty<const ConstantFP *> m_GFCst(const ConstantFP *&C) { return C; }

// General helper for all the binary generic MI such as G_ADD/G_SUB etc
template <typename LHS_P, typename RHS_P, unsigned Opcode,
          bool Commutable = false>
struct BinaryOp_match {
  LHS_P L;
  RHS_P R;

  BinaryOp_match(const LHS_P &LHS, const RHS_P &RHS) : L(LHS), R(RHS) {}
  template <typename OpTy> bool match(MachineRegisterInfo &MRI, OpTy &&Op) {
    MachineInstr *TmpMI;
    if (mi_match(Op, MRI, m_MInstr(TmpMI))) {
      if (TmpMI->getOpcode() == Opcode && TmpMI->getNumOperands() == 3) {
        return (L.match(MRI, TmpMI->getOperand(1).getReg()) &&
                R.match(MRI, TmpMI->getOperand(2).getReg())) ||
               (Commutable && (R.match(MRI, TmpMI->getOperand(1).getReg()) &&
                               L.match(MRI, TmpMI->getOperand(2).getReg())));
      }
    }
    return false;
  }
};

template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, TargetOpcode::G_ADD, true>
m_GAdd(const LHS &L, const RHS &R) {
  return BinaryOp_match<LHS, RHS, TargetOpcode::G_ADD, true>(L, R);
}

template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, TargetOpcode::G_SUB> m_GSub(const LHS &L,
                                                            const RHS &R) {
  return BinaryOp_match<LHS, RHS, TargetOpcode::G_SUB>(L, R);
}

template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, TargetOpcode::G_MUL, true>
m_GMul(const LHS &L, const RHS &R) {
  return BinaryOp_match<LHS, RHS, TargetOpcode::G_MUL, true>(L, R);
}

template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, TargetOpcode::G_FADD, true>
m_GFAdd(const LHS &L, const RHS &R) {
  return BinaryOp_match<LHS, RHS, TargetOpcode::G_FADD, true>(L, R);
}

template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, TargetOpcode::G_FMUL, true>
m_GFMul(const LHS &L, const RHS &R) {
  return BinaryOp_match<LHS, RHS, TargetOpcode::G_FMUL, true>(L, R);
}

template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, TargetOpcode::G_FSUB, false>
m_GFSub(const LHS &L, const RHS &R) {
  return BinaryOp_match<LHS, RHS, TargetOpcode::G_FSUB, false>(L, R);
}

template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, TargetOpcode::G_AND, true>
m_GAnd(const LHS &L, const RHS &R) {
  return BinaryOp_match<LHS, RHS, TargetOpcode::G_AND, true>(L, R);
}

template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, TargetOpcode::G_OR, true> m_GOr(const LHS &L,
                                                                const RHS &R) {
  return BinaryOp_match<LHS, RHS, TargetOpcode::G_OR, true>(L, R);
}

// Helper for unary instructions (G_[ZSA]EXT/G_TRUNC) etc
template <typename SrcTy, unsigned Opcode> struct UnaryOp_match {
  SrcTy L;

  UnaryOp_match(const SrcTy &LHS) : L(LHS) {}
  template <typename OpTy> bool match(MachineRegisterInfo &MRI, OpTy &&Op) {
    MachineInstr *TmpMI;
    if (mi_match(Op, MRI, m_MInstr(TmpMI))) {
      if (TmpMI->getOpcode() == Opcode && TmpMI->getNumOperands() == 2) {
        return L.match(MRI, TmpMI->getOperand(1).getReg());
      }
    }
    return false;
  }
};

template <typename SrcTy>
inline UnaryOp_match<SrcTy, TargetOpcode::G_ANYEXT>
m_GAnyExt(const SrcTy &Src) {
  return UnaryOp_match<SrcTy, TargetOpcode::G_ANYEXT>(Src);
}

template <typename SrcTy>
inline UnaryOp_match<SrcTy, TargetOpcode::G_SEXT> m_GSExt(const SrcTy &Src) {
  return UnaryOp_match<SrcTy, TargetOpcode::G_SEXT>(Src);
}

template <typename SrcTy>
inline UnaryOp_match<SrcTy, TargetOpcode::G_ZEXT> m_GZExt(const SrcTy &Src) {
  return UnaryOp_match<SrcTy, TargetOpcode::G_ZEXT>(Src);
}

template <typename SrcTy>
inline UnaryOp_match<SrcTy, TargetOpcode::G_FPEXT> m_GFPExt(const SrcTy &Src) {
  return UnaryOp_match<SrcTy, TargetOpcode::G_FPEXT>(Src);
}

template <typename SrcTy>
inline UnaryOp_match<SrcTy, TargetOpcode::G_TRUNC> m_GTrunc(const SrcTy &Src) {
  return UnaryOp_match<SrcTy, TargetOpcode::G_TRUNC>(Src);
}

template <typename SrcTy>
inline UnaryOp_match<SrcTy, TargetOpcode::G_BITCAST>
m_GBitcast(const SrcTy &Src) {
  return UnaryOp_match<SrcTy, TargetOpcode::G_BITCAST>(Src);
}

template <typename SrcTy>
inline UnaryOp_match<SrcTy, TargetOpcode::G_PTRTOINT>
m_GPtrToInt(const SrcTy &Src) {
  return UnaryOp_match<SrcTy, TargetOpcode::G_PTRTOINT>(Src);
}

template <typename SrcTy>
inline UnaryOp_match<SrcTy, TargetOpcode::G_INTTOPTR>
m_GIntToPtr(const SrcTy &Src) {
  return UnaryOp_match<SrcTy, TargetOpcode::G_INTTOPTR>(Src);
}

template <typename SrcTy>
inline UnaryOp_match<SrcTy, TargetOpcode::G_FPTRUNC>
m_GFPTrunc(const SrcTy &Src) {
  return UnaryOp_match<SrcTy, TargetOpcode::G_FPTRUNC>(Src);
}

template <typename SrcTy>
inline UnaryOp_match<SrcTy, TargetOpcode::G_FABS> m_GFabs(const SrcTy &Src) {
  return UnaryOp_match<SrcTy, TargetOpcode::G_FABS>(Src);
}

template <typename SrcTy>
inline UnaryOp_match<SrcTy, TargetOpcode::G_FNEG> m_GFNeg(const SrcTy &Src) {
  return UnaryOp_match<SrcTy, TargetOpcode::G_FNEG>(Src);
}

template <typename SrcTy>
inline UnaryOp_match<SrcTy, TargetOpcode::COPY> m_Copy(SrcTy &&Src) {
  return UnaryOp_match<SrcTy, TargetOpcode::COPY>(std::forward<SrcTy>(Src));
}

// Helper for checking if a Reg is of specific type.
struct CheckType {
  LLT Ty;
  CheckType(const LLT &Ty) : Ty(Ty) {}

  bool match(MachineRegisterInfo &MRI, unsigned Reg) {
    return MRI.getType(Reg) == Ty;
  }
};

inline CheckType m_SpecificType(LLT Ty) { return Ty; }

} // namespace GMIPatternMatch
} // namespace llvm

#endif
