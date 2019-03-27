//===- Trace.cpp - XRay Trace Loading implementation. ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// XRay log reader implementation.
//
//===----------------------------------------------------------------------===//
#include "llvm/XRay/Trace.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/XRay/BlockIndexer.h"
#include "llvm/XRay/BlockVerifier.h"
#include "llvm/XRay/FDRRecordConsumer.h"
#include "llvm/XRay/FDRRecordProducer.h"
#include "llvm/XRay/FDRRecords.h"
#include "llvm/XRay/FDRTraceExpander.h"
#include "llvm/XRay/FileHeaderReader.h"
#include "llvm/XRay/YAMLXRayRecord.h"
#include <memory>
#include <vector>

using namespace llvm;
using namespace llvm::xray;
using llvm::yaml::Input;

namespace {
using XRayRecordStorage =
    std::aligned_storage<sizeof(XRayRecord), alignof(XRayRecord)>::type;

Error loadNaiveFormatLog(StringRef Data, bool IsLittleEndian,
                         XRayFileHeader &FileHeader,
                         std::vector<XRayRecord> &Records) {
  if (Data.size() < 32)
    return make_error<StringError>(
        "Not enough bytes for an XRay log.",
        std::make_error_code(std::errc::invalid_argument));

  if (Data.size() - 32 == 0 || Data.size() % 32 != 0)
    return make_error<StringError>(
        "Invalid-sized XRay data.",
        std::make_error_code(std::errc::invalid_argument));

  DataExtractor Reader(Data, IsLittleEndian, 8);
  uint32_t OffsetPtr = 0;
  auto FileHeaderOrError = readBinaryFormatHeader(Reader, OffsetPtr);
  if (!FileHeaderOrError)
    return FileHeaderOrError.takeError();
  FileHeader = std::move(FileHeaderOrError.get());

  // Each record after the header will be 32 bytes, in the following format:
  //
  //   (2)   uint16 : record type
  //   (1)   uint8  : cpu id
  //   (1)   uint8  : type
  //   (4)   sint32 : function id
  //   (8)   uint64 : tsc
  //   (4)   uint32 : thread id
  //   (4)   uint32 : process id
  //   (8)   -      : padding
  while (Reader.isValidOffset(OffsetPtr)) {
    if (!Reader.isValidOffsetForDataOfSize(OffsetPtr, 32))
      return createStringError(
          std::make_error_code(std::errc::executable_format_error),
          "Not enough bytes to read a full record at offset %d.", OffsetPtr);
    auto PreReadOffset = OffsetPtr;
    auto RecordType = Reader.getU16(&OffsetPtr);
    if (OffsetPtr == PreReadOffset)
      return createStringError(
          std::make_error_code(std::errc::executable_format_error),
          "Failed reading record type at offset %d.", OffsetPtr);

    switch (RecordType) {
    case 0: { // Normal records.
      Records.emplace_back();
      auto &Record = Records.back();
      Record.RecordType = RecordType;

      PreReadOffset = OffsetPtr;
      Record.CPU = Reader.getU8(&OffsetPtr);
      if (OffsetPtr == PreReadOffset)
        return createStringError(
            std::make_error_code(std::errc::executable_format_error),
            "Failed reading CPU field at offset %d.", OffsetPtr);

      PreReadOffset = OffsetPtr;
      auto Type = Reader.getU8(&OffsetPtr);
      if (OffsetPtr == PreReadOffset)
        return createStringError(
            std::make_error_code(std::errc::executable_format_error),
            "Failed reading record type field at offset %d.", OffsetPtr);

      switch (Type) {
      case 0:
        Record.Type = RecordTypes::ENTER;
        break;
      case 1:
        Record.Type = RecordTypes::EXIT;
        break;
      case 2:
        Record.Type = RecordTypes::TAIL_EXIT;
        break;
      case 3:
        Record.Type = RecordTypes::ENTER_ARG;
        break;
      default:
        return createStringError(
            std::make_error_code(std::errc::executable_format_error),
            "Unknown record type '%d' at offset %d.", Type, OffsetPtr);
      }

      PreReadOffset = OffsetPtr;
      Record.FuncId = Reader.getSigned(&OffsetPtr, sizeof(int32_t));
      if (OffsetPtr == PreReadOffset)
        return createStringError(
            std::make_error_code(std::errc::executable_format_error),
            "Failed reading function id field at offset %d.", OffsetPtr);

      PreReadOffset = OffsetPtr;
      Record.TSC = Reader.getU64(&OffsetPtr);
      if (OffsetPtr == PreReadOffset)
        return createStringError(
            std::make_error_code(std::errc::executable_format_error),
            "Failed reading TSC field at offset %d.", OffsetPtr);

      PreReadOffset = OffsetPtr;
      Record.TId = Reader.getU32(&OffsetPtr);
      if (OffsetPtr == PreReadOffset)
        return createStringError(
            std::make_error_code(std::errc::executable_format_error),
            "Failed reading thread id field at offset %d.", OffsetPtr);

      PreReadOffset = OffsetPtr;
      Record.PId = Reader.getU32(&OffsetPtr);
      if (OffsetPtr == PreReadOffset)
        return createStringError(
            std::make_error_code(std::errc::executable_format_error),
            "Failed reading process id at offset %d.", OffsetPtr);

      break;
    }
    case 1: { // Arg payload record.
      auto &Record = Records.back();

      // We skip the next two bytes of the record, because we don't need the
      // type and the CPU record for arg payloads.
      OffsetPtr += 2;
      PreReadOffset = OffsetPtr;
      int32_t FuncId = Reader.getSigned(&OffsetPtr, sizeof(int32_t));
      if (OffsetPtr == PreReadOffset)
        return createStringError(
            std::make_error_code(std::errc::executable_format_error),
            "Failed reading function id field at offset %d.", OffsetPtr);

      PreReadOffset = OffsetPtr;
      auto TId = Reader.getU32(&OffsetPtr);
      if (OffsetPtr == PreReadOffset)
        return createStringError(
            std::make_error_code(std::errc::executable_format_error),
            "Failed reading thread id field at offset %d.", OffsetPtr);

      PreReadOffset = OffsetPtr;
      auto PId = Reader.getU32(&OffsetPtr);
      if (OffsetPtr == PreReadOffset)
        return createStringError(
            std::make_error_code(std::errc::executable_format_error),
            "Failed reading process id field at offset %d.", OffsetPtr);

      // Make a check for versions above 3 for the Pid field
      if (Record.FuncId != FuncId || Record.TId != TId ||
          (FileHeader.Version >= 3 ? Record.PId != PId : false))
        return createStringError(
            std::make_error_code(std::errc::executable_format_error),
            "Corrupted log, found arg payload following non-matching "
            "function+thread record. Record for function %d != %d at offset "
            "%d",
            Record.FuncId, FuncId, OffsetPtr);

      PreReadOffset = OffsetPtr;
      auto Arg = Reader.getU64(&OffsetPtr);
      if (OffsetPtr == PreReadOffset)
        return createStringError(
            std::make_error_code(std::errc::executable_format_error),
            "Failed reading argument payload at offset %d.", OffsetPtr);

      Record.CallArgs.push_back(Arg);
      break;
    }
    default:
      return createStringError(
          std::make_error_code(std::errc::executable_format_error),
          "Unknown record type '%d' at offset %d.", RecordType, OffsetPtr);
    }
    // Advance the offset pointer enough bytes to align to 32-byte records for
    // basic mode logs.
    OffsetPtr += 8;
  }
  return Error::success();
}

/// Reads a log in FDR mode for version 1 of this binary format. FDR mode is
/// defined as part of the compiler-rt project in xray_fdr_logging.h, and such
/// a log consists of the familiar 32 bit XRayHeader, followed by sequences of
/// of interspersed 16 byte Metadata Records and 8 byte Function Records.
///
/// The following is an attempt to document the grammar of the format, which is
/// parsed by this function for little-endian machines. Since the format makes
/// use of BitFields, when we support big-endian architectures, we will need to
/// adjust not only the endianness parameter to llvm's RecordExtractor, but also
/// the bit twiddling logic, which is consistent with the little-endian
/// convention that BitFields within a struct will first be packed into the
/// least significant bits the address they belong to.
///
/// We expect a format complying with the grammar in the following pseudo-EBNF
/// in Version 1 of the FDR log.
///
/// FDRLog: XRayFileHeader ThreadBuffer*
/// XRayFileHeader: 32 bytes to identify the log as FDR with machine metadata.
///     Includes BufferSize
/// ThreadBuffer: NewBuffer WallClockTime NewCPUId FunctionSequence EOB
/// BufSize: 8 byte unsigned integer indicating how large the buffer is.
/// NewBuffer: 16 byte metadata record with Thread Id.
/// WallClockTime: 16 byte metadata record with human readable time.
/// Pid: 16 byte metadata record with Pid
/// NewCPUId: 16 byte metadata record with CPUId and a 64 bit TSC reading.
/// EOB: 16 byte record in a thread buffer plus mem garbage to fill BufSize.
/// FunctionSequence: NewCPUId | TSCWrap | FunctionRecord
/// TSCWrap: 16 byte metadata record with a full 64 bit TSC reading.
/// FunctionRecord: 8 byte record with FunctionId, entry/exit, and TSC delta.
///
/// In Version 2, we make the following changes:
///
/// ThreadBuffer: BufferExtents NewBuffer WallClockTime NewCPUId
///               FunctionSequence
/// BufferExtents: 16 byte metdata record describing how many usable bytes are
///                in the buffer. This is measured from the start of the buffer
///                and must always be at least 48 (bytes).
///
/// In Version 3, we make the following changes:
///
/// ThreadBuffer: BufferExtents NewBuffer WallClockTime Pid NewCPUId
///               FunctionSequence
/// EOB: *deprecated*
///
/// In Version 4, we make the following changes:
///
/// CustomEventRecord now includes the CPU data.
///
/// In Version 5, we make the following changes:
///
/// CustomEventRecord and TypedEventRecord now use TSC delta encoding similar to
/// what FunctionRecord instances use, and we no longer need to include the CPU
/// id in the CustomEventRecord.
///
Error loadFDRLog(StringRef Data, bool IsLittleEndian,
                 XRayFileHeader &FileHeader, std::vector<XRayRecord> &Records) {

  if (Data.size() < 32)
    return createStringError(std::make_error_code(std::errc::invalid_argument),
                             "Not enough bytes for an XRay FDR log.");
  DataExtractor DE(Data, IsLittleEndian, 8);

  uint32_t OffsetPtr = 0;
  auto FileHeaderOrError = readBinaryFormatHeader(DE, OffsetPtr);
  if (!FileHeaderOrError)
    return FileHeaderOrError.takeError();
  FileHeader = std::move(FileHeaderOrError.get());

  // First we load the records into memory.
  std::vector<std::unique_ptr<Record>> FDRRecords;

  {
    FileBasedRecordProducer P(FileHeader, DE, OffsetPtr);
    LogBuilderConsumer C(FDRRecords);
    while (DE.isValidOffsetForDataOfSize(OffsetPtr, 1)) {
      auto R = P.produce();
      if (!R)
        return R.takeError();
      if (auto E = C.consume(std::move(R.get())))
        return E;
    }
  }

  // Next we index the records into blocks.
  BlockIndexer::Index Index;
  {
    BlockIndexer Indexer(Index);
    for (auto &R : FDRRecords)
      if (auto E = R->apply(Indexer))
        return E;
    if (auto E = Indexer.flush())
      return E;
  }

  // Then we verify the consistency of the blocks.
  {
    for (auto &PTB : Index) {
      auto &Blocks = PTB.second;
      for (auto &B : Blocks) {
        BlockVerifier Verifier;
        for (auto *R : B.Records)
          if (auto E = R->apply(Verifier))
            return E;
        if (auto E = Verifier.verify())
          return E;
      }
    }
  }

  // This is now the meat of the algorithm. Here we sort the blocks according to
  // the Walltime record in each of the blocks for the same thread. This allows
  // us to more consistently recreate the execution trace in temporal order.
  // After the sort, we then reconstitute `Trace` records using a stateful
  // visitor associated with a single process+thread pair.
  {
    for (auto &PTB : Index) {
      auto &Blocks = PTB.second;
      llvm::sort(Blocks, [](const BlockIndexer::Block &L,
                            const BlockIndexer::Block &R) {
        return (L.WallclockTime->seconds() < R.WallclockTime->seconds() &&
                L.WallclockTime->nanos() < R.WallclockTime->nanos());
      });
      auto Adder = [&](const XRayRecord &R) { Records.push_back(R); };
      TraceExpander Expander(Adder, FileHeader.Version);
      for (auto &B : Blocks) {
        for (auto *R : B.Records)
          if (auto E = R->apply(Expander))
            return E;
      }
      if (auto E = Expander.flush())
        return E;
    }
  }

  return Error::success();
}

Error loadYAMLLog(StringRef Data, XRayFileHeader &FileHeader,
                  std::vector<XRayRecord> &Records) {
  YAMLXRayTrace Trace;
  Input In(Data);
  In >> Trace;
  if (In.error())
    return make_error<StringError>("Failed loading YAML Data.", In.error());

  FileHeader.Version = Trace.Header.Version;
  FileHeader.Type = Trace.Header.Type;
  FileHeader.ConstantTSC = Trace.Header.ConstantTSC;
  FileHeader.NonstopTSC = Trace.Header.NonstopTSC;
  FileHeader.CycleFrequency = Trace.Header.CycleFrequency;

  if (FileHeader.Version != 1)
    return make_error<StringError>(
        Twine("Unsupported XRay file version: ") + Twine(FileHeader.Version),
        std::make_error_code(std::errc::invalid_argument));

  Records.clear();
  std::transform(Trace.Records.begin(), Trace.Records.end(),
                 std::back_inserter(Records), [&](const YAMLXRayRecord &R) {
                   return XRayRecord{R.RecordType, R.CPU,      R.Type,
                                     R.FuncId,     R.TSC,      R.TId,
                                     R.PId,        R.CallArgs, R.Data};
                 });
  return Error::success();
}
} // namespace

Expected<Trace> llvm::xray::loadTraceFile(StringRef Filename, bool Sort) {
  int Fd;
  if (auto EC = sys::fs::openFileForRead(Filename, Fd)) {
    return make_error<StringError>(
        Twine("Cannot read log from '") + Filename + "'", EC);
  }

  uint64_t FileSize;
  if (auto EC = sys::fs::file_size(Filename, FileSize)) {
    return make_error<StringError>(
        Twine("Cannot read log from '") + Filename + "'", EC);
  }
  if (FileSize < 4) {
    return make_error<StringError>(
        Twine("File '") + Filename + "' too small for XRay.",
        std::make_error_code(std::errc::executable_format_error));
  }

  // Map the opened file into memory and use a StringRef to access it later.
  std::error_code EC;
  sys::fs::mapped_file_region MappedFile(
      Fd, sys::fs::mapped_file_region::mapmode::readonly, FileSize, 0, EC);
  if (EC) {
    return make_error<StringError>(
        Twine("Cannot read log from '") + Filename + "'", EC);
  }
  auto Data = StringRef(MappedFile.data(), MappedFile.size());

  // TODO: Lift the endianness and implementation selection here.
  DataExtractor LittleEndianDE(Data, true, 8);
  auto TraceOrError = loadTrace(LittleEndianDE, Sort);
  if (!TraceOrError) {
    DataExtractor BigEndianDE(Data, false, 8);
    TraceOrError = loadTrace(BigEndianDE, Sort);
  }
  return TraceOrError;
}

Expected<Trace> llvm::xray::loadTrace(const DataExtractor &DE, bool Sort) {
  // Attempt to detect the file type using file magic. We have a slight bias
  // towards the binary format, and we do this by making sure that the first 4
  // bytes of the binary file is some combination of the following byte
  // patterns: (observe the code loading them assumes they're little endian)
  //
  //   0x01 0x00 0x00 0x00 - version 1, "naive" format
  //   0x01 0x00 0x01 0x00 - version 1, "flight data recorder" format
  //   0x02 0x00 0x01 0x00 - version 2, "flight data recorder" format
  //
  // YAML files don't typically have those first four bytes as valid text so we
  // try loading assuming YAML if we don't find these bytes.
  //
  // Only if we can't load either the binary or the YAML format will we yield an
  // error.
  DataExtractor HeaderExtractor(DE.getData(), DE.isLittleEndian(), 8);
  uint32_t OffsetPtr = 0;
  uint16_t Version = HeaderExtractor.getU16(&OffsetPtr);
  uint16_t Type = HeaderExtractor.getU16(&OffsetPtr);

  enum BinaryFormatType { NAIVE_FORMAT = 0, FLIGHT_DATA_RECORDER_FORMAT = 1 };

  Trace T;
  switch (Type) {
  case NAIVE_FORMAT:
    if (Version == 1 || Version == 2 || Version == 3) {
      if (auto E = loadNaiveFormatLog(DE.getData(), DE.isLittleEndian(),
                                      T.FileHeader, T.Records))
        return std::move(E);
    } else {
      return make_error<StringError>(
          Twine("Unsupported version for Basic/Naive Mode logging: ") +
              Twine(Version),
          std::make_error_code(std::errc::executable_format_error));
    }
    break;
  case FLIGHT_DATA_RECORDER_FORMAT:
    if (Version >= 1 && Version <= 5) {
      if (auto E = loadFDRLog(DE.getData(), DE.isLittleEndian(), T.FileHeader,
                              T.Records))
        return std::move(E);
    } else {
      return make_error<StringError>(
          Twine("Unsupported version for FDR Mode logging: ") + Twine(Version),
          std::make_error_code(std::errc::executable_format_error));
    }
    break;
  default:
    if (auto E = loadYAMLLog(DE.getData(), T.FileHeader, T.Records))
      return std::move(E);
  }

  if (Sort)
    std::stable_sort(T.Records.begin(), T.Records.end(),
                     [&](const XRayRecord &L, const XRayRecord &R) {
                       return L.TSC < R.TSC;
                     });

  return std::move(T);
}
