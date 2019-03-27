//===- BitstreamReader.h - Low-level bitstream reader interface -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This header defines the BitstreamReader class.  This class can be used to
// read an arbitrary bitstream, regardless of its contents.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BITCODE_BITSTREAMREADER_H
#define LLVM_BITCODE_BITSTREAMREADER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Bitcode/BitCodes.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBuffer.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace llvm {

/// This class maintains the abbreviations read from a block info block.
class BitstreamBlockInfo {
public:
  /// This contains information emitted to BLOCKINFO_BLOCK blocks. These
  /// describe abbreviations that all blocks of the specified ID inherit.
  struct BlockInfo {
    unsigned BlockID;
    std::vector<std::shared_ptr<BitCodeAbbrev>> Abbrevs;
    std::string Name;
    std::vector<std::pair<unsigned, std::string>> RecordNames;
  };

private:
  std::vector<BlockInfo> BlockInfoRecords;

public:
  /// If there is block info for the specified ID, return it, otherwise return
  /// null.
  const BlockInfo *getBlockInfo(unsigned BlockID) const {
    // Common case, the most recent entry matches BlockID.
    if (!BlockInfoRecords.empty() && BlockInfoRecords.back().BlockID == BlockID)
      return &BlockInfoRecords.back();

    for (unsigned i = 0, e = static_cast<unsigned>(BlockInfoRecords.size());
         i != e; ++i)
      if (BlockInfoRecords[i].BlockID == BlockID)
        return &BlockInfoRecords[i];
    return nullptr;
  }

  BlockInfo &getOrCreateBlockInfo(unsigned BlockID) {
    if (const BlockInfo *BI = getBlockInfo(BlockID))
      return *const_cast<BlockInfo*>(BI);

    // Otherwise, add a new record.
    BlockInfoRecords.emplace_back();
    BlockInfoRecords.back().BlockID = BlockID;
    return BlockInfoRecords.back();
  }
};

/// This represents a position within a bitstream. There may be multiple
/// independent cursors reading within one bitstream, each maintaining their
/// own local state.
class SimpleBitstreamCursor {
  ArrayRef<uint8_t> BitcodeBytes;
  size_t NextChar = 0;

public:
  /// This is the current data we have pulled from the stream but have not
  /// returned to the client. This is specifically and intentionally defined to
  /// follow the word size of the host machine for efficiency. We use word_t in
  /// places that are aware of this to make it perfectly explicit what is going
  /// on.
  using word_t = size_t;

private:
  word_t CurWord = 0;

  /// This is the number of bits in CurWord that are valid. This is always from
  /// [0...bits_of(size_t)-1] inclusive.
  unsigned BitsInCurWord = 0;

public:
  static const size_t MaxChunkSize = sizeof(word_t) * 8;

  SimpleBitstreamCursor() = default;
  explicit SimpleBitstreamCursor(ArrayRef<uint8_t> BitcodeBytes)
      : BitcodeBytes(BitcodeBytes) {}
  explicit SimpleBitstreamCursor(StringRef BitcodeBytes)
      : BitcodeBytes(reinterpret_cast<const uint8_t *>(BitcodeBytes.data()),
                     BitcodeBytes.size()) {}
  explicit SimpleBitstreamCursor(MemoryBufferRef BitcodeBytes)
      : SimpleBitstreamCursor(BitcodeBytes.getBuffer()) {}

  bool canSkipToPos(size_t pos) const {
    // pos can be skipped to if it is a valid address or one byte past the end.
    return pos <= BitcodeBytes.size();
  }

  bool AtEndOfStream() {
    return BitsInCurWord == 0 && BitcodeBytes.size() <= NextChar;
  }

  /// Return the bit # of the bit we are reading.
  uint64_t GetCurrentBitNo() const {
    return NextChar*CHAR_BIT - BitsInCurWord;
  }

  // Return the byte # of the current bit.
  uint64_t getCurrentByteNo() const { return GetCurrentBitNo() / 8; }

  ArrayRef<uint8_t> getBitcodeBytes() const { return BitcodeBytes; }

  /// Reset the stream to the specified bit number.
  void JumpToBit(uint64_t BitNo) {
    size_t ByteNo = size_t(BitNo/8) & ~(sizeof(word_t)-1);
    unsigned WordBitNo = unsigned(BitNo & (sizeof(word_t)*8-1));
    assert(canSkipToPos(ByteNo) && "Invalid location");

    // Move the cursor to the right word.
    NextChar = ByteNo;
    BitsInCurWord = 0;

    // Skip over any bits that are already consumed.
    if (WordBitNo)
      Read(WordBitNo);
  }

  /// Get a pointer into the bitstream at the specified byte offset.
  const uint8_t *getPointerToByte(uint64_t ByteNo, uint64_t NumBytes) {
    return BitcodeBytes.data() + ByteNo;
  }

  /// Get a pointer into the bitstream at the specified bit offset.
  ///
  /// The bit offset must be on a byte boundary.
  const uint8_t *getPointerToBit(uint64_t BitNo, uint64_t NumBytes) {
    assert(!(BitNo % 8) && "Expected bit on byte boundary");
    return getPointerToByte(BitNo / 8, NumBytes);
  }

  void fillCurWord() {
    if (NextChar >= BitcodeBytes.size())
      report_fatal_error("Unexpected end of file");

    // Read the next word from the stream.
    const uint8_t *NextCharPtr = BitcodeBytes.data() + NextChar;
    unsigned BytesRead;
    if (BitcodeBytes.size() >= NextChar + sizeof(word_t)) {
      BytesRead = sizeof(word_t);
      CurWord =
          support::endian::read<word_t, support::little, support::unaligned>(
              NextCharPtr);
    } else {
      // Short read.
      BytesRead = BitcodeBytes.size() - NextChar;
      CurWord = 0;
      for (unsigned B = 0; B != BytesRead; ++B)
        CurWord |= uint64_t(NextCharPtr[B]) << (B * 8);
    }
    NextChar += BytesRead;
    BitsInCurWord = BytesRead * 8;
  }

  word_t Read(unsigned NumBits) {
    static const unsigned BitsInWord = MaxChunkSize;

    assert(NumBits && NumBits <= BitsInWord &&
           "Cannot return zero or more than BitsInWord bits!");

    static const unsigned Mask = sizeof(word_t) > 4 ? 0x3f : 0x1f;

    // If the field is fully contained by CurWord, return it quickly.
    if (BitsInCurWord >= NumBits) {
      word_t R = CurWord & (~word_t(0) >> (BitsInWord - NumBits));

      // Use a mask to avoid undefined behavior.
      CurWord >>= (NumBits & Mask);

      BitsInCurWord -= NumBits;
      return R;
    }

    word_t R = BitsInCurWord ? CurWord : 0;
    unsigned BitsLeft = NumBits - BitsInCurWord;

    fillCurWord();

    // If we run out of data, abort.
    if (BitsLeft > BitsInCurWord)
      report_fatal_error("Unexpected end of file");

    word_t R2 = CurWord & (~word_t(0) >> (BitsInWord - BitsLeft));

    // Use a mask to avoid undefined behavior.
    CurWord >>= (BitsLeft & Mask);

    BitsInCurWord -= BitsLeft;

    R |= R2 << (NumBits - BitsLeft);

    return R;
  }

  uint32_t ReadVBR(unsigned NumBits) {
    uint32_t Piece = Read(NumBits);
    if ((Piece & (1U << (NumBits-1))) == 0)
      return Piece;

    uint32_t Result = 0;
    unsigned NextBit = 0;
    while (true) {
      Result |= (Piece & ((1U << (NumBits-1))-1)) << NextBit;

      if ((Piece & (1U << (NumBits-1))) == 0)
        return Result;

      NextBit += NumBits-1;
      Piece = Read(NumBits);
    }
  }

  // Read a VBR that may have a value up to 64-bits in size. The chunk size of
  // the VBR must still be <= 32 bits though.
  uint64_t ReadVBR64(unsigned NumBits) {
    uint32_t Piece = Read(NumBits);
    if ((Piece & (1U << (NumBits-1))) == 0)
      return uint64_t(Piece);

    uint64_t Result = 0;
    unsigned NextBit = 0;
    while (true) {
      Result |= uint64_t(Piece & ((1U << (NumBits-1))-1)) << NextBit;

      if ((Piece & (1U << (NumBits-1))) == 0)
        return Result;

      NextBit += NumBits-1;
      Piece = Read(NumBits);
    }
  }

  void SkipToFourByteBoundary() {
    // If word_t is 64-bits and if we've read less than 32 bits, just dump
    // the bits we have up to the next 32-bit boundary.
    if (sizeof(word_t) > 4 &&
        BitsInCurWord >= 32) {
      CurWord >>= BitsInCurWord-32;
      BitsInCurWord = 32;
      return;
    }

    BitsInCurWord = 0;
  }

  /// Skip to the end of the file.
  void skipToEnd() { NextChar = BitcodeBytes.size(); }
};

/// When advancing through a bitstream cursor, each advance can discover a few
/// different kinds of entries:
struct BitstreamEntry {
  enum {
    Error,    // Malformed bitcode was found.
    EndBlock, // We've reached the end of the current block, (or the end of the
              // file, which is treated like a series of EndBlock records.
    SubBlock, // This is the start of a new subblock of a specific ID.
    Record    // This is a record with a specific AbbrevID.
  } Kind;

  unsigned ID;

  static BitstreamEntry getError() {
    BitstreamEntry E; E.Kind = Error; return E;
  }

  static BitstreamEntry getEndBlock() {
    BitstreamEntry E; E.Kind = EndBlock; return E;
  }

  static BitstreamEntry getSubBlock(unsigned ID) {
    BitstreamEntry E; E.Kind = SubBlock; E.ID = ID; return E;
  }

  static BitstreamEntry getRecord(unsigned AbbrevID) {
    BitstreamEntry E; E.Kind = Record; E.ID = AbbrevID; return E;
  }
};

/// This represents a position within a bitcode file, implemented on top of a
/// SimpleBitstreamCursor.
///
/// Unlike iterators, BitstreamCursors are heavy-weight objects that should not
/// be passed by value.
class BitstreamCursor : SimpleBitstreamCursor {
  // This is the declared size of code values used for the current block, in
  // bits.
  unsigned CurCodeSize = 2;

  /// Abbrevs installed at in this block.
  std::vector<std::shared_ptr<BitCodeAbbrev>> CurAbbrevs;

  struct Block {
    unsigned PrevCodeSize;
    std::vector<std::shared_ptr<BitCodeAbbrev>> PrevAbbrevs;

    explicit Block(unsigned PCS) : PrevCodeSize(PCS) {}
  };

  /// This tracks the codesize of parent blocks.
  SmallVector<Block, 8> BlockScope;

  BitstreamBlockInfo *BlockInfo = nullptr;

public:
  static const size_t MaxChunkSize = sizeof(word_t) * 8;

  BitstreamCursor() = default;
  explicit BitstreamCursor(ArrayRef<uint8_t> BitcodeBytes)
      : SimpleBitstreamCursor(BitcodeBytes) {}
  explicit BitstreamCursor(StringRef BitcodeBytes)
      : SimpleBitstreamCursor(BitcodeBytes) {}
  explicit BitstreamCursor(MemoryBufferRef BitcodeBytes)
      : SimpleBitstreamCursor(BitcodeBytes) {}

  using SimpleBitstreamCursor::canSkipToPos;
  using SimpleBitstreamCursor::AtEndOfStream;
  using SimpleBitstreamCursor::getBitcodeBytes;
  using SimpleBitstreamCursor::GetCurrentBitNo;
  using SimpleBitstreamCursor::getCurrentByteNo;
  using SimpleBitstreamCursor::getPointerToByte;
  using SimpleBitstreamCursor::JumpToBit;
  using SimpleBitstreamCursor::fillCurWord;
  using SimpleBitstreamCursor::Read;
  using SimpleBitstreamCursor::ReadVBR;
  using SimpleBitstreamCursor::ReadVBR64;

  /// Return the number of bits used to encode an abbrev #.
  unsigned getAbbrevIDWidth() const { return CurCodeSize; }

  /// Flags that modify the behavior of advance().
  enum {
    /// If this flag is used, the advance() method does not automatically pop
    /// the block scope when the end of a block is reached.
    AF_DontPopBlockAtEnd = 1,

    /// If this flag is used, abbrev entries are returned just like normal
    /// records.
    AF_DontAutoprocessAbbrevs = 2
  };

  /// Advance the current bitstream, returning the next entry in the stream.
  BitstreamEntry advance(unsigned Flags = 0) {
    while (true) {
      if (AtEndOfStream())
        return BitstreamEntry::getError();

      unsigned Code = ReadCode();
      if (Code == bitc::END_BLOCK) {
        // Pop the end of the block unless Flags tells us not to.
        if (!(Flags & AF_DontPopBlockAtEnd) && ReadBlockEnd())
          return BitstreamEntry::getError();
        return BitstreamEntry::getEndBlock();
      }

      if (Code == bitc::ENTER_SUBBLOCK)
        return BitstreamEntry::getSubBlock(ReadSubBlockID());

      if (Code == bitc::DEFINE_ABBREV &&
          !(Flags & AF_DontAutoprocessAbbrevs)) {
        // We read and accumulate abbrev's, the client can't do anything with
        // them anyway.
        ReadAbbrevRecord();
        continue;
      }

      return BitstreamEntry::getRecord(Code);
    }
  }

  /// This is a convenience function for clients that don't expect any
  /// subblocks. This just skips over them automatically.
  BitstreamEntry advanceSkippingSubblocks(unsigned Flags = 0) {
    while (true) {
      // If we found a normal entry, return it.
      BitstreamEntry Entry = advance(Flags);
      if (Entry.Kind != BitstreamEntry::SubBlock)
        return Entry;

      // If we found a sub-block, just skip over it and check the next entry.
      if (SkipBlock())
        return BitstreamEntry::getError();
    }
  }

  unsigned ReadCode() {
    return Read(CurCodeSize);
  }

  // Block header:
  //    [ENTER_SUBBLOCK, blockid, newcodelen, <align4bytes>, blocklen]

  /// Having read the ENTER_SUBBLOCK code, read the BlockID for the block.
  unsigned ReadSubBlockID() {
    return ReadVBR(bitc::BlockIDWidth);
  }

  /// Having read the ENTER_SUBBLOCK abbrevid and a BlockID, skip over the body
  /// of this block. If the block record is malformed, return true.
  bool SkipBlock() {
    // Read and ignore the codelen value.  Since we are skipping this block, we
    // don't care what code widths are used inside of it.
    ReadVBR(bitc::CodeLenWidth);
    SkipToFourByteBoundary();
    size_t NumFourBytes = Read(bitc::BlockSizeWidth);

    // Check that the block wasn't partially defined, and that the offset isn't
    // bogus.
    size_t SkipTo = GetCurrentBitNo() + NumFourBytes*4*8;
    if (AtEndOfStream() || !canSkipToPos(SkipTo/8))
      return true;

    JumpToBit(SkipTo);
    return false;
  }

  /// Having read the ENTER_SUBBLOCK abbrevid, enter the block, and return true
  /// if the block has an error.
  bool EnterSubBlock(unsigned BlockID, unsigned *NumWordsP = nullptr);

  bool ReadBlockEnd() {
    if (BlockScope.empty()) return true;

    // Block tail:
    //    [END_BLOCK, <align4bytes>]
    SkipToFourByteBoundary();

    popBlockScope();
    return false;
  }

private:
  void popBlockScope() {
    CurCodeSize = BlockScope.back().PrevCodeSize;

    CurAbbrevs = std::move(BlockScope.back().PrevAbbrevs);
    BlockScope.pop_back();
  }

  //===--------------------------------------------------------------------===//
  // Record Processing
  //===--------------------------------------------------------------------===//

public:
  /// Return the abbreviation for the specified AbbrevId.
  const BitCodeAbbrev *getAbbrev(unsigned AbbrevID) {
    unsigned AbbrevNo = AbbrevID - bitc::FIRST_APPLICATION_ABBREV;
    if (AbbrevNo >= CurAbbrevs.size())
      report_fatal_error("Invalid abbrev number");
    return CurAbbrevs[AbbrevNo].get();
  }

  /// Read the current record and discard it, returning the code for the record.
  unsigned skipRecord(unsigned AbbrevID);

  unsigned readRecord(unsigned AbbrevID, SmallVectorImpl<uint64_t> &Vals,
                      StringRef *Blob = nullptr);

  //===--------------------------------------------------------------------===//
  // Abbrev Processing
  //===--------------------------------------------------------------------===//
  void ReadAbbrevRecord();

  /// Read and return a block info block from the bitstream. If an error was
  /// encountered, return None.
  ///
  /// \param ReadBlockInfoNames Whether to read block/record name information in
  /// the BlockInfo block. Only llvm-bcanalyzer uses this.
  Optional<BitstreamBlockInfo>
  ReadBlockInfoBlock(bool ReadBlockInfoNames = false);

  /// Set the block info to be used by this BitstreamCursor to interpret
  /// abbreviated records.
  void setBlockInfo(BitstreamBlockInfo *BI) { BlockInfo = BI; }
};

} // end llvm namespace

#endif // LLVM_BITCODE_BITSTREAMREADER_H
