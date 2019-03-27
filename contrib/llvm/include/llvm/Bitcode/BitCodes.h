//===- BitCodes.h - Enum values for the bitcode format ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This header Bitcode enum values.
//
// The enum values defined in this file should be considered permanent.  If
// new features are added, they should have values added at the end of the
// respective lists.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BITCODE_BITCODES_H
#define LLVM_BITCODE_BITCODES_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>

namespace llvm {
/// Offsets of the 32-bit fields of bitcode wrapper header.
static const unsigned BWH_MagicField = 0 * 4;
static const unsigned BWH_VersionField = 1 * 4;
static const unsigned BWH_OffsetField = 2 * 4;
static const unsigned BWH_SizeField = 3 * 4;
static const unsigned BWH_CPUTypeField = 4 * 4;
static const unsigned BWH_HeaderSize = 5 * 4;

namespace bitc {
  enum StandardWidths {
    BlockIDWidth   = 8,  // We use VBR-8 for block IDs.
    CodeLenWidth   = 4,  // Codelen are VBR-4.
    BlockSizeWidth = 32  // BlockSize up to 2^32 32-bit words = 16GB per block.
  };

  // The standard abbrev namespace always has a way to exit a block, enter a
  // nested block, define abbrevs, and define an unabbreviated record.
  enum FixedAbbrevIDs {
    END_BLOCK = 0,  // Must be zero to guarantee termination for broken bitcode.
    ENTER_SUBBLOCK = 1,

    /// DEFINE_ABBREV - Defines an abbrev for the current block.  It consists
    /// of a vbr5 for # operand infos.  Each operand info is emitted with a
    /// single bit to indicate if it is a literal encoding.  If so, the value is
    /// emitted with a vbr8.  If not, the encoding is emitted as 3 bits followed
    /// by the info value as a vbr5 if needed.
    DEFINE_ABBREV = 2,

    // UNABBREV_RECORDs are emitted with a vbr6 for the record code, followed by
    // a vbr6 for the # operands, followed by vbr6's for each operand.
    UNABBREV_RECORD = 3,

    // This is not a code, this is a marker for the first abbrev assignment.
    FIRST_APPLICATION_ABBREV = 4
  };

  /// StandardBlockIDs - All bitcode files can optionally include a BLOCKINFO
  /// block, which contains metadata about other blocks in the file.
  enum StandardBlockIDs {
    /// BLOCKINFO_BLOCK is used to define metadata about blocks, for example,
    /// standard abbrevs that should be available to all blocks of a specified
    /// ID.
    BLOCKINFO_BLOCK_ID = 0,

    // Block IDs 1-7 are reserved for future expansion.
    FIRST_APPLICATION_BLOCKID = 8
  };

  /// BlockInfoCodes - The blockinfo block contains metadata about user-defined
  /// blocks.
  enum BlockInfoCodes {
    // DEFINE_ABBREV has magic semantics here, applying to the current SETBID'd
    // block, instead of the BlockInfo block.

    BLOCKINFO_CODE_SETBID        = 1, // SETBID: [blockid#]
    BLOCKINFO_CODE_BLOCKNAME     = 2, // BLOCKNAME: [name]
    BLOCKINFO_CODE_SETRECORDNAME = 3  // BLOCKINFO_CODE_SETRECORDNAME:
                                      //                             [id, name]
  };

} // End bitc namespace

/// BitCodeAbbrevOp - This describes one or more operands in an abbreviation.
/// This is actually a union of two different things:
///   1. It could be a literal integer value ("the operand is always 17").
///   2. It could be an encoding specification ("this operand encoded like so").
///
class BitCodeAbbrevOp {
  uint64_t Val;           // A literal value or data for an encoding.
  bool IsLiteral : 1;     // Indicate whether this is a literal value or not.
  unsigned Enc   : 3;     // The encoding to use.
public:
  enum Encoding {
    Fixed = 1,  // A fixed width field, Val specifies number of bits.
    VBR   = 2,  // A VBR field where Val specifies the width of each chunk.
    Array = 3,  // A sequence of fields, next field species elt encoding.
    Char6 = 4,  // A 6-bit fixed field which maps to [a-zA-Z0-9._].
    Blob  = 5   // 32-bit aligned array of 8-bit characters.
  };

  explicit BitCodeAbbrevOp(uint64_t V) :  Val(V), IsLiteral(true) {}
  explicit BitCodeAbbrevOp(Encoding E, uint64_t Data = 0)
    : Val(Data), IsLiteral(false), Enc(E) {}

  bool isLiteral() const  { return IsLiteral; }
  bool isEncoding() const { return !IsLiteral; }

  // Accessors for literals.
  uint64_t getLiteralValue() const { assert(isLiteral()); return Val; }

  // Accessors for encoding info.
  Encoding getEncoding() const { assert(isEncoding()); return (Encoding)Enc; }
  uint64_t getEncodingData() const {
    assert(isEncoding() && hasEncodingData());
    return Val;
  }

  bool hasEncodingData() const { return hasEncodingData(getEncoding()); }
  static bool hasEncodingData(Encoding E) {
    switch (E) {
    case Fixed:
    case VBR:
      return true;
    case Array:
    case Char6:
    case Blob:
      return false;
    }
    report_fatal_error("Invalid encoding");
  }

  /// isChar6 - Return true if this character is legal in the Char6 encoding.
  static bool isChar6(char C) {
    if (C >= 'a' && C <= 'z') return true;
    if (C >= 'A' && C <= 'Z') return true;
    if (C >= '0' && C <= '9') return true;
    if (C == '.' || C == '_') return true;
    return false;
  }
  static unsigned EncodeChar6(char C) {
    if (C >= 'a' && C <= 'z') return C-'a';
    if (C >= 'A' && C <= 'Z') return C-'A'+26;
    if (C >= '0' && C <= '9') return C-'0'+26+26;
    if (C == '.')             return 62;
    if (C == '_')             return 63;
    llvm_unreachable("Not a value Char6 character!");
  }

  static char DecodeChar6(unsigned V) {
    assert((V & ~63) == 0 && "Not a Char6 encoded character!");
    return "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._"
        [V];
  }

};

template <> struct isPodLike<BitCodeAbbrevOp> { static const bool value=true; };

/// BitCodeAbbrev - This class represents an abbreviation record.  An
/// abbreviation allows a complex record that has redundancy to be stored in a
/// specialized format instead of the fully-general, fully-vbr, format.
class BitCodeAbbrev {
  SmallVector<BitCodeAbbrevOp, 32> OperandList;

public:
  unsigned getNumOperandInfos() const {
    return static_cast<unsigned>(OperandList.size());
  }
  const BitCodeAbbrevOp &getOperandInfo(unsigned N) const {
    return OperandList[N];
  }

  void Add(const BitCodeAbbrevOp &OpInfo) {
    OperandList.push_back(OpInfo);
  }
};
} // End llvm namespace

#endif
