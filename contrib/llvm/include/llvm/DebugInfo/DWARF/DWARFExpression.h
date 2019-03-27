//===--- DWARFExpression.h - DWARF Expression handling ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARFEXPRESSION_H
#define LLVM_DEBUGINFO_DWARFEXPRESSION_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/DataExtractor.h"

namespace llvm {
class DWARFUnit;
class MCRegisterInfo;
class raw_ostream;

class DWARFExpression {
public:
  class iterator;

  /// This class represents an Operation in the Expression. Each operation can
  /// have up to 2 oprerands.
  ///
  /// An Operation can be in Error state (check with isError()). This
  /// means that it couldn't be decoded successfully and if it is the
  /// case, all others fields contain undefined values.
  class Operation {
  public:
    /// Size and signedness of expression operations' operands.
    enum Encoding : uint8_t {
      Size1 = 0,
      Size2 = 1,
      Size4 = 2,
      Size8 = 3,
      SizeLEB = 4,
      SizeAddr = 5,
      SizeRefAddr = 6,
      SizeBlock = 7, ///< Preceding operand contains block size
      SignBit = 0x8,
      SignedSize1 = SignBit | Size1,
      SignedSize2 = SignBit | Size2,
      SignedSize4 = SignBit | Size4,
      SignedSize8 = SignBit | Size8,
      SignedSizeLEB = SignBit | SizeLEB,
      SizeNA = 0xFF ///< Unused operands get this encoding.
    };

    enum DwarfVersion : uint8_t {
      DwarfNA, ///< Serves as a marker for unused entries
      Dwarf2 = 2,
      Dwarf3,
      Dwarf4
    };

    /// Description of the encoding of one expression Op.
    struct Description {
      DwarfVersion Version; ///< Dwarf version where the Op was introduced.
      Encoding Op[2];       ///< Encoding for Op operands, or SizeNA.

      Description(DwarfVersion Version = DwarfNA, Encoding Op1 = SizeNA,
                  Encoding Op2 = SizeNA)
          : Version(Version) {
        Op[0] = Op1;
        Op[1] = Op2;
      }
    };

  private:
    friend class DWARFExpression::iterator;
    uint8_t Opcode; ///< The Op Opcode, DW_OP_<something>.
    Description Desc;
    bool Error;
    uint32_t EndOffset;
    uint64_t Operands[2];

  public:
    Description &getDescription() { return Desc; }
    uint8_t getCode() { return Opcode; }
    uint64_t getRawOperand(unsigned Idx) { return Operands[Idx]; }
    uint32_t getEndOffset() { return EndOffset; }
    bool extract(DataExtractor Data, uint16_t Version, uint8_t AddressSize,
                 uint32_t Offset);
    bool isError() { return Error; }
    bool print(raw_ostream &OS, const DWARFExpression *U,
               const MCRegisterInfo *RegInfo, bool isEH);
  };

  /// An iterator to go through the expression operations.
  class iterator
      : public iterator_facade_base<iterator, std::forward_iterator_tag,
                                    Operation> {
    friend class DWARFExpression;
    const DWARFExpression *Expr;
    uint32_t Offset;
    Operation Op;
    iterator(const DWARFExpression *Expr, uint32_t Offset)
        : Expr(Expr), Offset(Offset) {
      Op.Error =
          Offset >= Expr->Data.getData().size() ||
          !Op.extract(Expr->Data, Expr->Version, Expr->AddressSize, Offset);
    }

  public:
    class Operation &operator++() {
      Offset = Op.isError() ? Expr->Data.getData().size() : Op.EndOffset;
      Op.Error =
          Offset >= Expr->Data.getData().size() ||
          !Op.extract(Expr->Data, Expr->Version, Expr->AddressSize, Offset);
      return Op;
    }

    class Operation &operator*() {
      return Op;
    }

    // Comparison operators are provided out of line.
    friend bool operator==(const iterator &, const iterator &);
  };

  DWARFExpression(DataExtractor Data, uint16_t Version, uint8_t AddressSize)
      : Data(Data), Version(Version), AddressSize(AddressSize) {
    assert(AddressSize == 8 || AddressSize == 4);
  }

  iterator begin() const { return iterator(this, 0); }
  iterator end() const { return iterator(this, Data.getData().size()); }

  void print(raw_ostream &OS, const MCRegisterInfo *RegInfo,
             bool IsEH = false) const;

private:
  DataExtractor Data;
  uint16_t Version;
  uint8_t AddressSize;
};

inline bool operator==(const DWARFExpression::iterator &LHS,
                       const DWARFExpression::iterator &RHS) {
  return LHS.Expr == RHS.Expr && LHS.Offset == RHS.Offset;
}

inline bool operator!=(const DWARFExpression::iterator &LHS,
                       const DWARFExpression::iterator &RHS) {
  return !(LHS == RHS);
}
}
#endif
