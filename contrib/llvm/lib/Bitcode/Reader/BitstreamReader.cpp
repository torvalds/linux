//===- BitstreamReader.cpp - BitstreamReader implementation ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Bitcode/BitstreamReader.h"
#include "llvm/ADT/StringRef.h"
#include <cassert>
#include <string>

using namespace llvm;

//===----------------------------------------------------------------------===//
//  BitstreamCursor implementation
//===----------------------------------------------------------------------===//

/// EnterSubBlock - Having read the ENTER_SUBBLOCK abbrevid, enter
/// the block, and return true if the block has an error.
bool BitstreamCursor::EnterSubBlock(unsigned BlockID, unsigned *NumWordsP) {
  // Save the current block's state on BlockScope.
  BlockScope.push_back(Block(CurCodeSize));
  BlockScope.back().PrevAbbrevs.swap(CurAbbrevs);

  // Add the abbrevs specific to this block to the CurAbbrevs list.
  if (BlockInfo) {
    if (const BitstreamBlockInfo::BlockInfo *Info =
            BlockInfo->getBlockInfo(BlockID)) {
      CurAbbrevs.insert(CurAbbrevs.end(), Info->Abbrevs.begin(),
                        Info->Abbrevs.end());
    }
  }

  // Get the codesize of this block.
  CurCodeSize = ReadVBR(bitc::CodeLenWidth);
  // We can't read more than MaxChunkSize at a time
  if (CurCodeSize > MaxChunkSize)
    return true;

  SkipToFourByteBoundary();
  unsigned NumWords = Read(bitc::BlockSizeWidth);
  if (NumWordsP) *NumWordsP = NumWords;

  // Validate that this block is sane.
  return CurCodeSize == 0 || AtEndOfStream();
}

static uint64_t readAbbreviatedField(BitstreamCursor &Cursor,
                                     const BitCodeAbbrevOp &Op) {
  assert(!Op.isLiteral() && "Not to be used with literals!");

  // Decode the value as we are commanded.
  switch (Op.getEncoding()) {
  case BitCodeAbbrevOp::Array:
  case BitCodeAbbrevOp::Blob:
    llvm_unreachable("Should not reach here");
  case BitCodeAbbrevOp::Fixed:
    assert((unsigned)Op.getEncodingData() <= Cursor.MaxChunkSize);
    return Cursor.Read((unsigned)Op.getEncodingData());
  case BitCodeAbbrevOp::VBR:
    assert((unsigned)Op.getEncodingData() <= Cursor.MaxChunkSize);
    return Cursor.ReadVBR64((unsigned)Op.getEncodingData());
  case BitCodeAbbrevOp::Char6:
    return BitCodeAbbrevOp::DecodeChar6(Cursor.Read(6));
  }
  llvm_unreachable("invalid abbreviation encoding");
}

static void skipAbbreviatedField(BitstreamCursor &Cursor,
                                 const BitCodeAbbrevOp &Op) {
  assert(!Op.isLiteral() && "Not to be used with literals!");

  // Decode the value as we are commanded.
  switch (Op.getEncoding()) {
  case BitCodeAbbrevOp::Array:
  case BitCodeAbbrevOp::Blob:
    llvm_unreachable("Should not reach here");
  case BitCodeAbbrevOp::Fixed:
    assert((unsigned)Op.getEncodingData() <= Cursor.MaxChunkSize);
    Cursor.Read((unsigned)Op.getEncodingData());
    break;
  case BitCodeAbbrevOp::VBR:
    assert((unsigned)Op.getEncodingData() <= Cursor.MaxChunkSize);
    Cursor.ReadVBR64((unsigned)Op.getEncodingData());
    break;
  case BitCodeAbbrevOp::Char6:
    Cursor.Read(6);
    break;
  }
}

/// skipRecord - Read the current record and discard it.
unsigned BitstreamCursor::skipRecord(unsigned AbbrevID) {
  // Skip unabbreviated records by reading past their entries.
  if (AbbrevID == bitc::UNABBREV_RECORD) {
    unsigned Code = ReadVBR(6);
    unsigned NumElts = ReadVBR(6);
    for (unsigned i = 0; i != NumElts; ++i)
      (void)ReadVBR64(6);
    return Code;
  }

  const BitCodeAbbrev *Abbv = getAbbrev(AbbrevID);
  const BitCodeAbbrevOp &CodeOp = Abbv->getOperandInfo(0);
  unsigned Code;
  if (CodeOp.isLiteral())
    Code = CodeOp.getLiteralValue();
  else {
    if (CodeOp.getEncoding() == BitCodeAbbrevOp::Array ||
        CodeOp.getEncoding() == BitCodeAbbrevOp::Blob)
      report_fatal_error("Abbreviation starts with an Array or a Blob");
    Code = readAbbreviatedField(*this, CodeOp);
  }

  for (unsigned i = 1, e = Abbv->getNumOperandInfos(); i < e; ++i) {
    const BitCodeAbbrevOp &Op = Abbv->getOperandInfo(i);
    if (Op.isLiteral())
      continue;

    if (Op.getEncoding() != BitCodeAbbrevOp::Array &&
        Op.getEncoding() != BitCodeAbbrevOp::Blob) {
      skipAbbreviatedField(*this, Op);
      continue;
    }

    if (Op.getEncoding() == BitCodeAbbrevOp::Array) {
      // Array case.  Read the number of elements as a vbr6.
      unsigned NumElts = ReadVBR(6);

      // Get the element encoding.
      assert(i+2 == e && "array op not second to last?");
      const BitCodeAbbrevOp &EltEnc = Abbv->getOperandInfo(++i);

      // Read all the elements.
      // Decode the value as we are commanded.
      switch (EltEnc.getEncoding()) {
      default:
        report_fatal_error("Array element type can't be an Array or a Blob");
      case BitCodeAbbrevOp::Fixed:
        assert((unsigned)EltEnc.getEncodingData() <= MaxChunkSize);
        JumpToBit(GetCurrentBitNo() + NumElts * EltEnc.getEncodingData());
        break;
      case BitCodeAbbrevOp::VBR:
        assert((unsigned)EltEnc.getEncodingData() <= MaxChunkSize);
        for (; NumElts; --NumElts)
          ReadVBR64((unsigned)EltEnc.getEncodingData());
        break;
      case BitCodeAbbrevOp::Char6:
        JumpToBit(GetCurrentBitNo() + NumElts * 6);
        break;
      }
      continue;
    }

    assert(Op.getEncoding() == BitCodeAbbrevOp::Blob);
    // Blob case.  Read the number of bytes as a vbr6.
    unsigned NumElts = ReadVBR(6);
    SkipToFourByteBoundary();  // 32-bit alignment

    // Figure out where the end of this blob will be including tail padding.
    size_t NewEnd = GetCurrentBitNo()+((NumElts+3)&~3)*8;

    // If this would read off the end of the bitcode file, just set the
    // record to empty and return.
    if (!canSkipToPos(NewEnd/8)) {
      skipToEnd();
      break;
    }

    // Skip over the blob.
    JumpToBit(NewEnd);
  }
  return Code;
}

unsigned BitstreamCursor::readRecord(unsigned AbbrevID,
                                     SmallVectorImpl<uint64_t> &Vals,
                                     StringRef *Blob) {
  if (AbbrevID == bitc::UNABBREV_RECORD) {
    unsigned Code = ReadVBR(6);
    unsigned NumElts = ReadVBR(6);
    for (unsigned i = 0; i != NumElts; ++i)
      Vals.push_back(ReadVBR64(6));
    return Code;
  }

  const BitCodeAbbrev *Abbv = getAbbrev(AbbrevID);

  // Read the record code first.
  assert(Abbv->getNumOperandInfos() != 0 && "no record code in abbreviation?");
  const BitCodeAbbrevOp &CodeOp = Abbv->getOperandInfo(0);
  unsigned Code;
  if (CodeOp.isLiteral())
    Code = CodeOp.getLiteralValue();
  else {
    if (CodeOp.getEncoding() == BitCodeAbbrevOp::Array ||
        CodeOp.getEncoding() == BitCodeAbbrevOp::Blob)
      report_fatal_error("Abbreviation starts with an Array or a Blob");
    Code = readAbbreviatedField(*this, CodeOp);
  }

  for (unsigned i = 1, e = Abbv->getNumOperandInfos(); i != e; ++i) {
    const BitCodeAbbrevOp &Op = Abbv->getOperandInfo(i);
    if (Op.isLiteral()) {
      Vals.push_back(Op.getLiteralValue());
      continue;
    }

    if (Op.getEncoding() != BitCodeAbbrevOp::Array &&
        Op.getEncoding() != BitCodeAbbrevOp::Blob) {
      Vals.push_back(readAbbreviatedField(*this, Op));
      continue;
    }

    if (Op.getEncoding() == BitCodeAbbrevOp::Array) {
      // Array case.  Read the number of elements as a vbr6.
      unsigned NumElts = ReadVBR(6);

      // Get the element encoding.
      if (i + 2 != e)
        report_fatal_error("Array op not second to last");
      const BitCodeAbbrevOp &EltEnc = Abbv->getOperandInfo(++i);
      if (!EltEnc.isEncoding())
        report_fatal_error(
            "Array element type has to be an encoding of a type");

      // Read all the elements.
      switch (EltEnc.getEncoding()) {
      default:
        report_fatal_error("Array element type can't be an Array or a Blob");
      case BitCodeAbbrevOp::Fixed:
        for (; NumElts; --NumElts)
          Vals.push_back(Read((unsigned)EltEnc.getEncodingData()));
        break;
      case BitCodeAbbrevOp::VBR:
        for (; NumElts; --NumElts)
          Vals.push_back(ReadVBR64((unsigned)EltEnc.getEncodingData()));
        break;
      case BitCodeAbbrevOp::Char6:
        for (; NumElts; --NumElts)
          Vals.push_back(BitCodeAbbrevOp::DecodeChar6(Read(6)));
      }
      continue;
    }

    assert(Op.getEncoding() == BitCodeAbbrevOp::Blob);
    // Blob case.  Read the number of bytes as a vbr6.
    unsigned NumElts = ReadVBR(6);
    SkipToFourByteBoundary();  // 32-bit alignment

    // Figure out where the end of this blob will be including tail padding.
    size_t CurBitPos = GetCurrentBitNo();
    size_t NewEnd = CurBitPos+((NumElts+3)&~3)*8;

    // If this would read off the end of the bitcode file, just set the
    // record to empty and return.
    if (!canSkipToPos(NewEnd/8)) {
      Vals.append(NumElts, 0);
      skipToEnd();
      break;
    }

    // Otherwise, inform the streamer that we need these bytes in memory.  Skip
    // over tail padding first, in case jumping to NewEnd invalidates the Blob
    // pointer.
    JumpToBit(NewEnd);
    const char *Ptr = (const char *)getPointerToBit(CurBitPos, NumElts);

    // If we can return a reference to the data, do so to avoid copying it.
    if (Blob) {
      *Blob = StringRef(Ptr, NumElts);
    } else {
      // Otherwise, unpack into Vals with zero extension.
      for (; NumElts; --NumElts)
        Vals.push_back((unsigned char)*Ptr++);
    }
  }

  return Code;
}

void BitstreamCursor::ReadAbbrevRecord() {
  auto Abbv = std::make_shared<BitCodeAbbrev>();
  unsigned NumOpInfo = ReadVBR(5);
  for (unsigned i = 0; i != NumOpInfo; ++i) {
    bool IsLiteral = Read(1);
    if (IsLiteral) {
      Abbv->Add(BitCodeAbbrevOp(ReadVBR64(8)));
      continue;
    }

    BitCodeAbbrevOp::Encoding E = (BitCodeAbbrevOp::Encoding)Read(3);
    if (BitCodeAbbrevOp::hasEncodingData(E)) {
      uint64_t Data = ReadVBR64(5);

      // As a special case, handle fixed(0) (i.e., a fixed field with zero bits)
      // and vbr(0) as a literal zero.  This is decoded the same way, and avoids
      // a slow path in Read() to have to handle reading zero bits.
      if ((E == BitCodeAbbrevOp::Fixed || E == BitCodeAbbrevOp::VBR) &&
          Data == 0) {
        Abbv->Add(BitCodeAbbrevOp(0));
        continue;
      }

      if ((E == BitCodeAbbrevOp::Fixed || E == BitCodeAbbrevOp::VBR) &&
          Data > MaxChunkSize)
        report_fatal_error(
            "Fixed or VBR abbrev record with size > MaxChunkData");

      Abbv->Add(BitCodeAbbrevOp(E, Data));
    } else
      Abbv->Add(BitCodeAbbrevOp(E));
  }

  if (Abbv->getNumOperandInfos() == 0)
    report_fatal_error("Abbrev record with no operands");
  CurAbbrevs.push_back(std::move(Abbv));
}

Optional<BitstreamBlockInfo>
BitstreamCursor::ReadBlockInfoBlock(bool ReadBlockInfoNames) {
  if (EnterSubBlock(bitc::BLOCKINFO_BLOCK_ID)) return None;

  BitstreamBlockInfo NewBlockInfo;

  SmallVector<uint64_t, 64> Record;
  BitstreamBlockInfo::BlockInfo *CurBlockInfo = nullptr;

  // Read all the records for this module.
  while (true) {
    BitstreamEntry Entry = advanceSkippingSubblocks(AF_DontAutoprocessAbbrevs);

    switch (Entry.Kind) {
    case llvm::BitstreamEntry::SubBlock: // Handled for us already.
    case llvm::BitstreamEntry::Error:
      return None;
    case llvm::BitstreamEntry::EndBlock:
      return std::move(NewBlockInfo);
    case llvm::BitstreamEntry::Record:
      // The interesting case.
      break;
    }

    // Read abbrev records, associate them with CurBID.
    if (Entry.ID == bitc::DEFINE_ABBREV) {
      if (!CurBlockInfo) return None;
      ReadAbbrevRecord();

      // ReadAbbrevRecord installs the abbrev in CurAbbrevs.  Move it to the
      // appropriate BlockInfo.
      CurBlockInfo->Abbrevs.push_back(std::move(CurAbbrevs.back()));
      CurAbbrevs.pop_back();
      continue;
    }

    // Read a record.
    Record.clear();
    switch (readRecord(Entry.ID, Record)) {
      default: break;  // Default behavior, ignore unknown content.
      case bitc::BLOCKINFO_CODE_SETBID:
        if (Record.size() < 1) return None;
        CurBlockInfo = &NewBlockInfo.getOrCreateBlockInfo((unsigned)Record[0]);
        break;
      case bitc::BLOCKINFO_CODE_BLOCKNAME: {
        if (!CurBlockInfo) return None;
        if (!ReadBlockInfoNames)
          break; // Ignore name.
        std::string Name;
        for (unsigned i = 0, e = Record.size(); i != e; ++i)
          Name += (char)Record[i];
        CurBlockInfo->Name = Name;
        break;
      }
      case bitc::BLOCKINFO_CODE_SETRECORDNAME: {
        if (!CurBlockInfo) return None;
        if (!ReadBlockInfoNames)
          break; // Ignore name.
        std::string Name;
        for (unsigned i = 1, e = Record.size(); i != e; ++i)
          Name += (char)Record[i];
        CurBlockInfo->RecordNames.push_back(std::make_pair((unsigned)Record[0],
                                                           Name));
        break;
      }
    }
  }
}
