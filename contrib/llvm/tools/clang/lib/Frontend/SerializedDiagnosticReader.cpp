//===- SerializedDiagnosticReader.cpp - Reads diagnostics -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/SerializedDiagnosticReader.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/FileSystemOptions.h"
#include "clang/Frontend/SerializedDiagnostics.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Bitcode/BitCodes.h"
#include "llvm/Bitcode/BitstreamReader.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/ManagedStatic.h"
#include <cstdint>
#include <system_error>

using namespace clang;
using namespace serialized_diags;

std::error_code SerializedDiagnosticReader::readDiagnostics(StringRef File) {
  // Open the diagnostics file.
  FileSystemOptions FO;
  FileManager FileMgr(FO);

  auto Buffer = FileMgr.getBufferForFile(File);
  if (!Buffer)
    return SDError::CouldNotLoad;

  llvm::BitstreamCursor Stream(**Buffer);
  Optional<llvm::BitstreamBlockInfo> BlockInfo;

  if (Stream.AtEndOfStream())
    return SDError::InvalidSignature;

  // Sniff for the signature.
  if (Stream.Read(8) != 'D' ||
      Stream.Read(8) != 'I' ||
      Stream.Read(8) != 'A' ||
      Stream.Read(8) != 'G')
    return SDError::InvalidSignature;

  // Read the top level blocks.
  while (!Stream.AtEndOfStream()) {
    if (Stream.ReadCode() != llvm::bitc::ENTER_SUBBLOCK)
      return SDError::InvalidDiagnostics;

    std::error_code EC;
    switch (Stream.ReadSubBlockID()) {
    case llvm::bitc::BLOCKINFO_BLOCK_ID:
      BlockInfo = Stream.ReadBlockInfoBlock();
      if (!BlockInfo)
        return SDError::MalformedBlockInfoBlock;
      Stream.setBlockInfo(&*BlockInfo);
      continue;
    case BLOCK_META:
      if ((EC = readMetaBlock(Stream)))
        return EC;
      continue;
    case BLOCK_DIAG:
      if ((EC = readDiagnosticBlock(Stream)))
        return EC;
      continue;
    default:
      if (!Stream.SkipBlock())
        return SDError::MalformedTopLevelBlock;
      continue;
    }
  }
  return {};
}

enum class SerializedDiagnosticReader::Cursor {
  Record = 1,
  BlockEnd,
  BlockBegin
};

llvm::ErrorOr<SerializedDiagnosticReader::Cursor>
SerializedDiagnosticReader::skipUntilRecordOrBlock(
    llvm::BitstreamCursor &Stream, unsigned &BlockOrRecordID) {
  BlockOrRecordID = 0;

  while (!Stream.AtEndOfStream()) {
    unsigned Code = Stream.ReadCode();

    switch ((llvm::bitc::FixedAbbrevIDs)Code) {
    case llvm::bitc::ENTER_SUBBLOCK:
      BlockOrRecordID = Stream.ReadSubBlockID();
      return Cursor::BlockBegin;

    case llvm::bitc::END_BLOCK:
      if (Stream.ReadBlockEnd())
        return SDError::InvalidDiagnostics;
      return Cursor::BlockEnd;

    case llvm::bitc::DEFINE_ABBREV:
      Stream.ReadAbbrevRecord();
      continue;

    case llvm::bitc::UNABBREV_RECORD:
      return SDError::UnsupportedConstruct;

    default:
      // We found a record.
      BlockOrRecordID = Code;
      return Cursor::Record;
    }
  }

  return SDError::InvalidDiagnostics;
}

std::error_code
SerializedDiagnosticReader::readMetaBlock(llvm::BitstreamCursor &Stream) {
  if (Stream.EnterSubBlock(clang::serialized_diags::BLOCK_META))
    return SDError::MalformedMetadataBlock;

  bool VersionChecked = false;

  while (true) {
    unsigned BlockOrCode = 0;
    llvm::ErrorOr<Cursor> Res = skipUntilRecordOrBlock(Stream, BlockOrCode);
    if (!Res)
      Res.getError();

    switch (Res.get()) {
    case Cursor::Record:
      break;
    case Cursor::BlockBegin:
      if (Stream.SkipBlock())
        return SDError::MalformedMetadataBlock;
      LLVM_FALLTHROUGH;
    case Cursor::BlockEnd:
      if (!VersionChecked)
        return SDError::MissingVersion;
      return {};
    }

    SmallVector<uint64_t, 1> Record;
    unsigned RecordID = Stream.readRecord(BlockOrCode, Record);

    if (RecordID == RECORD_VERSION) {
      if (Record.size() < 1)
        return SDError::MissingVersion;
      if (Record[0] > VersionNumber)
        return SDError::VersionMismatch;
      VersionChecked = true;
    }
  }
}

std::error_code
SerializedDiagnosticReader::readDiagnosticBlock(llvm::BitstreamCursor &Stream) {
  if (Stream.EnterSubBlock(clang::serialized_diags::BLOCK_DIAG))
    return SDError::MalformedDiagnosticBlock;

  std::error_code EC;
  if ((EC = visitStartOfDiagnostic()))
    return EC;

  SmallVector<uint64_t, 16> Record;
  while (true) {
    unsigned BlockOrCode = 0;
    llvm::ErrorOr<Cursor> Res = skipUntilRecordOrBlock(Stream, BlockOrCode);
    if (!Res)
      Res.getError();

    switch (Res.get()) {
    case Cursor::BlockBegin:
      // The only blocks we care about are subdiagnostics.
      if (BlockOrCode == serialized_diags::BLOCK_DIAG) {
        if ((EC = readDiagnosticBlock(Stream)))
          return EC;
      } else if (!Stream.SkipBlock())
        return SDError::MalformedSubBlock;
      continue;
    case Cursor::BlockEnd:
      if ((EC = visitEndOfDiagnostic()))
        return EC;
      return {};
    case Cursor::Record:
      break;
    }

    // Read the record.
    Record.clear();
    StringRef Blob;
    unsigned RecID = Stream.readRecord(BlockOrCode, Record, &Blob);

    if (RecID < serialized_diags::RECORD_FIRST ||
        RecID > serialized_diags::RECORD_LAST)
      continue;

    switch ((RecordIDs)RecID) {
    case RECORD_CATEGORY:
      // A category has ID and name size.
      if (Record.size() != 2)
        return SDError::MalformedDiagnosticRecord;
      if ((EC = visitCategoryRecord(Record[0], Blob)))
        return EC;
      continue;
    case RECORD_DIAG:
      // A diagnostic has severity, location (4), category, flag, and message
      // size.
      if (Record.size() != 8)
        return SDError::MalformedDiagnosticRecord;
      if ((EC = visitDiagnosticRecord(
               Record[0], Location(Record[1], Record[2], Record[3], Record[4]),
               Record[5], Record[6], Blob)))
        return EC;
      continue;
    case RECORD_DIAG_FLAG:
      // A diagnostic flag has ID and name size.
      if (Record.size() != 2)
        return SDError::MalformedDiagnosticRecord;
      if ((EC = visitDiagFlagRecord(Record[0], Blob)))
        return EC;
      continue;
    case RECORD_FILENAME:
      // A filename has ID, size, timestamp, and name size. The size and
      // timestamp are legacy fields that are always zero these days.
      if (Record.size() != 4)
        return SDError::MalformedDiagnosticRecord;
      if ((EC = visitFilenameRecord(Record[0], Record[1], Record[2], Blob)))
        return EC;
      continue;
    case RECORD_FIXIT:
      // A fixit has two locations (4 each) and message size.
      if (Record.size() != 9)
        return SDError::MalformedDiagnosticRecord;
      if ((EC = visitFixitRecord(
               Location(Record[0], Record[1], Record[2], Record[3]),
               Location(Record[4], Record[5], Record[6], Record[7]), Blob)))
        return EC;
      continue;
    case RECORD_SOURCE_RANGE:
      // A source range is two locations (4 each).
      if (Record.size() != 8)
        return SDError::MalformedDiagnosticRecord;
      if ((EC = visitSourceRangeRecord(
               Location(Record[0], Record[1], Record[2], Record[3]),
               Location(Record[4], Record[5], Record[6], Record[7]))))
        return EC;
      continue;
    case RECORD_VERSION:
      // A version is just a number.
      if (Record.size() != 1)
        return SDError::MalformedDiagnosticRecord;
      if ((EC = visitVersionRecord(Record[0])))
        return EC;
      continue;
    }
  }
}

namespace {

class SDErrorCategoryType final : public std::error_category {
  const char *name() const noexcept override {
    return "clang.serialized_diags";
  }

  std::string message(int IE) const override {
    auto E = static_cast<SDError>(IE);
    switch (E) {
    case SDError::CouldNotLoad:
      return "Failed to open diagnostics file";
    case SDError::InvalidSignature:
      return "Invalid diagnostics signature";
    case SDError::InvalidDiagnostics:
      return "Parse error reading diagnostics";
    case SDError::MalformedTopLevelBlock:
      return "Malformed block at top-level of diagnostics";
    case SDError::MalformedSubBlock:
      return "Malformed sub-block in a diagnostic";
    case SDError::MalformedBlockInfoBlock:
      return "Malformed BlockInfo block";
    case SDError::MalformedMetadataBlock:
      return "Malformed Metadata block";
    case SDError::MalformedDiagnosticBlock:
      return "Malformed Diagnostic block";
    case SDError::MalformedDiagnosticRecord:
      return "Malformed Diagnostic record";
    case SDError::MissingVersion:
      return "No version provided in diagnostics";
    case SDError::VersionMismatch:
      return "Unsupported diagnostics version";
    case SDError::UnsupportedConstruct:
      return "Bitcode constructs that are not supported in diagnostics appear";
    case SDError::HandlerFailed:
      return "Generic error occurred while handling a record";
    }
    llvm_unreachable("Unknown error type!");
  }
};

} // namespace

static llvm::ManagedStatic<SDErrorCategoryType> ErrorCategory;
const std::error_category &clang::serialized_diags::SDErrorCategory() {
  return *ErrorCategory;
}
