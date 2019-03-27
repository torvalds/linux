//===- llvm/CodeGen/DwarfExpression.h - Dwarf Compile Unit ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains support for writing dwarf compile unit.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_ASMPRINTER_DWARFEXPRESSION_H
#define LLVM_LIB_CODEGEN_ASMPRINTER_DWARFEXPRESSION_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include <cassert>
#include <cstdint>
#include <iterator>

namespace llvm {

class AsmPrinter;
class APInt;
class ByteStreamer;
class DwarfUnit;
class DIELoc;
class TargetRegisterInfo;

/// Holds a DIExpression and keeps track of how many operands have been consumed
/// so far.
class DIExpressionCursor {
  DIExpression::expr_op_iterator Start, End;

public:
  DIExpressionCursor(const DIExpression *Expr) {
    if (!Expr) {
      assert(Start == End);
      return;
    }
    Start = Expr->expr_op_begin();
    End = Expr->expr_op_end();
  }

  DIExpressionCursor(ArrayRef<uint64_t> Expr)
      : Start(Expr.begin()), End(Expr.end()) {}

  DIExpressionCursor(const DIExpressionCursor &) = default;

  /// Consume one operation.
  Optional<DIExpression::ExprOperand> take() {
    if (Start == End)
      return None;
    return *(Start++);
  }

  /// Consume N operations.
  void consume(unsigned N) { std::advance(Start, N); }

  /// Return the current operation.
  Optional<DIExpression::ExprOperand> peek() const {
    if (Start == End)
      return None;
    return *(Start);
  }

  /// Return the next operation.
  Optional<DIExpression::ExprOperand> peekNext() const {
    if (Start == End)
      return None;

    auto Next = Start.getNext();
    if (Next == End)
      return None;

    return *Next;
  }

  /// Determine whether there are any operations left in this expression.
  operator bool() const { return Start != End; }

  DIExpression::expr_op_iterator begin() const { return Start; }
  DIExpression::expr_op_iterator end() const { return End; }

  /// Retrieve the fragment information, if any.
  Optional<DIExpression::FragmentInfo> getFragmentInfo() const {
    return DIExpression::getFragmentInfo(Start, End);
  }
};

/// Base class containing the logic for constructing DWARF expressions
/// independently of whether they are emitted into a DIE or into a .debug_loc
/// entry.
class DwarfExpression {
protected:
  /// Holds information about all subregisters comprising a register location.
  struct Register {
    int DwarfRegNo;
    unsigned Size;
    const char *Comment;
  };

  /// The register location, if any.
  SmallVector<Register, 2> DwarfRegs;

  /// Current Fragment Offset in Bits.
  uint64_t OffsetInBits = 0;
  unsigned DwarfVersion;

  /// Sometimes we need to add a DW_OP_bit_piece to describe a subregister.
  unsigned SubRegisterSizeInBits = 0;
  unsigned SubRegisterOffsetInBits = 0;

  /// The kind of location description being produced.
  enum { Unknown = 0, Register, Memory, Implicit } LocationKind = Unknown;

  /// Push a DW_OP_piece / DW_OP_bit_piece for emitting later, if one is needed
  /// to represent a subregister.
  void setSubRegisterPiece(unsigned SizeInBits, unsigned OffsetInBits) {
    SubRegisterSizeInBits = SizeInBits;
    SubRegisterOffsetInBits = OffsetInBits;
  }

  /// Add masking operations to stencil out a subregister.
  void maskSubRegister();

  /// Output a dwarf operand and an optional assembler comment.
  virtual void emitOp(uint8_t Op, const char *Comment = nullptr) = 0;

  /// Emit a raw signed value.
  virtual void emitSigned(int64_t Value) = 0;

  /// Emit a raw unsigned value.
  virtual void emitUnsigned(uint64_t Value) = 0;

  /// Emit a normalized unsigned constant.
  void emitConstu(uint64_t Value);

  /// Return whether the given machine register is the frame register in the
  /// current function.
  virtual bool isFrameRegister(const TargetRegisterInfo &TRI, unsigned MachineReg) = 0;

  /// Emit a DW_OP_reg operation. Note that this is only legal inside a DWARF
  /// register location description.
  void addReg(int DwarfReg, const char *Comment = nullptr);

  /// Emit a DW_OP_breg operation.
  void addBReg(int DwarfReg, int Offset);

  /// Emit DW_OP_fbreg <Offset>.
  void addFBReg(int Offset);

  /// Emit a partial DWARF register operation.
  ///
  /// \param MachineReg           The register number.
  /// \param MaxSize              If the register must be composed from
  ///                             sub-registers this is an upper bound
  ///                             for how many bits the emitted DW_OP_piece
  ///                             may cover.
  ///
  /// If size and offset is zero an operation for the entire register is
  /// emitted: Some targets do not provide a DWARF register number for every
  /// register.  If this is the case, this function will attempt to emit a DWARF
  /// register by emitting a fragment of a super-register or by piecing together
  /// multiple subregisters that alias the register.
  ///
  /// \return false if no DWARF register exists for MachineReg.
  bool addMachineReg(const TargetRegisterInfo &TRI, unsigned MachineReg,
                     unsigned MaxSize = ~1U);

  /// Emit a DW_OP_piece or DW_OP_bit_piece operation for a variable fragment.
  /// \param OffsetInBits    This is an optional offset into the location that
  /// is at the top of the DWARF stack.
  void addOpPiece(unsigned SizeInBits, unsigned OffsetInBits = 0);

  /// Emit a shift-right dwarf operation.
  void addShr(unsigned ShiftBy);

  /// Emit a bitwise and dwarf operation.
  void addAnd(unsigned Mask);

  /// Emit a DW_OP_stack_value, if supported.
  ///
  /// The proper way to describe a constant value is DW_OP_constu <const>,
  /// DW_OP_stack_value.  Unfortunately, DW_OP_stack_value was not available
  /// until DWARF 4, so we will continue to generate DW_OP_constu <const> for
  /// DWARF 2 and DWARF 3. Technically, this is incorrect since DW_OP_const
  /// <const> actually describes a value at a constant address, not a constant
  /// value.  However, in the past there was no better way to describe a
  /// constant value, so the producers and consumers started to rely on
  /// heuristics to disambiguate the value vs. location status of the
  /// expression.  See PR21176 for more details.
  void addStackValue();

  ~DwarfExpression() = default;

public:
  DwarfExpression(unsigned DwarfVersion) : DwarfVersion(DwarfVersion) {}

  /// This needs to be called last to commit any pending changes.
  void finalize();

  /// Emit a signed constant.
  void addSignedConstant(int64_t Value);

  /// Emit an unsigned constant.
  void addUnsignedConstant(uint64_t Value);

  /// Emit an unsigned constant.
  void addUnsignedConstant(const APInt &Value);

  bool isMemoryLocation() const { return LocationKind == Memory; }
  bool isUnknownLocation() const { return LocationKind == Unknown; }

  /// Lock this down to become a memory location description.
  void setMemoryLocationKind() {
    assert(LocationKind == Unknown);
    LocationKind = Memory;
  }

  /// Emit a machine register location. As an optimization this may also consume
  /// the prefix of a DwarfExpression if a more efficient representation for
  /// combining the register location and the first operation exists.
  ///
  /// \param FragmentOffsetInBits     If this is one fragment out of a
  /// fragmented
  ///                                 location, this is the offset of the
  ///                                 fragment inside the entire variable.
  /// \return                         false if no DWARF register exists
  ///                                 for MachineReg.
  bool addMachineRegExpression(const TargetRegisterInfo &TRI,
                               DIExpressionCursor &Expr, unsigned MachineReg,
                               unsigned FragmentOffsetInBits = 0);

  /// Emit all remaining operations in the DIExpressionCursor.
  ///
  /// \param FragmentOffsetInBits     If this is one fragment out of multiple
  ///                                 locations, this is the offset of the
  ///                                 fragment inside the entire variable.
  void addExpression(DIExpressionCursor &&Expr,
                     unsigned FragmentOffsetInBits = 0);

  /// If applicable, emit an empty DW_OP_piece / DW_OP_bit_piece to advance to
  /// the fragment described by \c Expr.
  void addFragmentOffset(const DIExpression *Expr);
};

/// DwarfExpression implementation for .debug_loc entries.
class DebugLocDwarfExpression final : public DwarfExpression {
  ByteStreamer &BS;

  void emitOp(uint8_t Op, const char *Comment = nullptr) override;
  void emitSigned(int64_t Value) override;
  void emitUnsigned(uint64_t Value) override;
  bool isFrameRegister(const TargetRegisterInfo &TRI,
                       unsigned MachineReg) override;

public:
  DebugLocDwarfExpression(unsigned DwarfVersion, ByteStreamer &BS)
      : DwarfExpression(DwarfVersion), BS(BS) {}
};

/// DwarfExpression implementation for singular DW_AT_location.
class DIEDwarfExpression final : public DwarfExpression {
const AsmPrinter &AP;
  DwarfUnit &DU;
  DIELoc &DIE;

  void emitOp(uint8_t Op, const char *Comment = nullptr) override;
  void emitSigned(int64_t Value) override;
  void emitUnsigned(uint64_t Value) override;
  bool isFrameRegister(const TargetRegisterInfo &TRI,
                       unsigned MachineReg) override;
public:
  DIEDwarfExpression(const AsmPrinter &AP, DwarfUnit &DU, DIELoc &DIE);

  DIELoc *finalize() {
    DwarfExpression::finalize();
    return &DIE;
  }
};

} // end namespace llvm

#endif // LLVM_LIB_CODEGEN_ASMPRINTER_DWARFEXPRESSION_H
