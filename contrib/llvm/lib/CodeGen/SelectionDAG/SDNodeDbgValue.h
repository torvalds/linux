//===-- llvm/CodeGen/SDNodeDbgValue.h - SelectionDAG dbg_value --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the SDDbgValue class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_SELECTIONDAG_SDNODEDBGVALUE_H
#define LLVM_LIB_CODEGEN_SELECTIONDAG_SDNODEDBGVALUE_H

#include "llvm/IR/DebugLoc.h"
#include "llvm/Support/DataTypes.h"
#include <utility>

namespace llvm {

class DIVariable;
class DIExpression;
class SDNode;
class Value;
class raw_ostream;

/// Holds the information from a dbg_value node through SDISel.
/// We do not use SDValue here to avoid including its header.
class SDDbgValue {
public:
  enum DbgValueKind {
    SDNODE = 0,             ///< Value is the result of an expression.
    CONST = 1,              ///< Value is a constant.
    FRAMEIX = 2,            ///< Value is contents of a stack location.
    VREG = 3                ///< Value is a virtual register.
  };
private:
  union {
    struct {
      SDNode *Node;         ///< Valid for expressions.
      unsigned ResNo;       ///< Valid for expressions.
    } s;
    const Value *Const;     ///< Valid for constants.
    unsigned FrameIx;       ///< Valid for stack objects.
    unsigned VReg;          ///< Valid for registers.
  } u;
  DIVariable *Var;
  DIExpression *Expr;
  DebugLoc DL;
  unsigned Order;
  enum DbgValueKind kind;
  bool IsIndirect;
  bool Invalid = false;
  bool Emitted = false;

public:
  /// Constructor for non-constants.
  SDDbgValue(DIVariable *Var, DIExpression *Expr, SDNode *N, unsigned R,
             bool indir, DebugLoc dl, unsigned O)
      : Var(Var), Expr(Expr), DL(std::move(dl)), Order(O), IsIndirect(indir) {
    kind = SDNODE;
    u.s.Node = N;
    u.s.ResNo = R;
  }

  /// Constructor for constants.
  SDDbgValue(DIVariable *Var, DIExpression *Expr, const Value *C, DebugLoc dl,
             unsigned O)
      : Var(Var), Expr(Expr), DL(std::move(dl)), Order(O), IsIndirect(false) {
    kind = CONST;
    u.Const = C;
  }

  /// Constructor for virtual registers and frame indices.
  SDDbgValue(DIVariable *Var, DIExpression *Expr, unsigned VRegOrFrameIdx,
             bool IsIndirect, DebugLoc DL, unsigned Order,
             enum DbgValueKind Kind)
      : Var(Var), Expr(Expr), DL(DL), Order(Order), IsIndirect(IsIndirect) {
    assert((Kind == VREG || Kind == FRAMEIX) &&
           "Invalid SDDbgValue constructor");
    kind = Kind;
    if (kind == VREG)
      u.VReg = VRegOrFrameIdx;
    else
      u.FrameIx = VRegOrFrameIdx;
  }

  /// Returns the kind.
  DbgValueKind getKind() const { return kind; }

  /// Returns the DIVariable pointer for the variable.
  DIVariable *getVariable() const { return Var; }

  /// Returns the DIExpression pointer for the expression.
  DIExpression *getExpression() const { return Expr; }

  /// Returns the SDNode* for a register ref
  SDNode *getSDNode() const { assert (kind==SDNODE); return u.s.Node; }

  /// Returns the ResNo for a register ref
  unsigned getResNo() const { assert (kind==SDNODE); return u.s.ResNo; }

  /// Returns the Value* for a constant
  const Value *getConst() const { assert (kind==CONST); return u.Const; }

  /// Returns the FrameIx for a stack object
  unsigned getFrameIx() const { assert (kind==FRAMEIX); return u.FrameIx; }

  /// Returns the Virtual Register for a VReg
  unsigned getVReg() const { assert (kind==VREG); return u.VReg; }

  /// Returns whether this is an indirect value.
  bool isIndirect() const { return IsIndirect; }

  /// Returns the DebugLoc.
  DebugLoc getDebugLoc() const { return DL; }

  /// Returns the SDNodeOrder.  This is the order of the preceding node in the
  /// input.
  unsigned getOrder() const { return Order; }

  /// setIsInvalidated / isInvalidated - Setter / getter of the "Invalidated"
  /// property. A SDDbgValue is invalid if the SDNode that produces the value is
  /// deleted.
  void setIsInvalidated() { Invalid = true; }
  bool isInvalidated() const { return Invalid; }

  /// setIsEmitted / isEmitted - Getter/Setter for flag indicating that this
  /// SDDbgValue has been emitted to an MBB.
  void setIsEmitted() { Emitted = true; }
  bool isEmitted() const { return Emitted; }

  /// clearIsEmitted - Reset Emitted flag, for certain special cases where
  /// dbg.addr is emitted twice.
  void clearIsEmitted() { Emitted = false; }

  LLVM_DUMP_METHOD void dump(raw_ostream &OS) const;
};

/// Holds the information from a dbg_label node through SDISel.
/// We do not use SDValue here to avoid including its header.
class SDDbgLabel {
  MDNode *Label;
  DebugLoc DL;
  unsigned Order;

public:
  SDDbgLabel(MDNode *Label, DebugLoc dl, unsigned O)
      : Label(Label), DL(std::move(dl)), Order(O) {}

  /// Returns the MDNode pointer for the label.
  MDNode *getLabel() const { return Label; }

  /// Returns the DebugLoc.
  DebugLoc getDebugLoc() const { return DL; }

  /// Returns the SDNodeOrder.  This is the order of the preceding node in the
  /// input.
  unsigned getOrder() const { return Order; }
};

} // end llvm namespace

#endif
