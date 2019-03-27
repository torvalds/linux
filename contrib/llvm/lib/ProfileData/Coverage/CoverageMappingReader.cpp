//===- CoverageMappingReader.cpp - Code coverage mapping reader -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains support for reading coverage mapping data for
// instrumentation based coverage.
//
//===----------------------------------------------------------------------===//

#include "llvm/ProfileData/Coverage/CoverageMappingReader.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/Error.h"
#include "llvm/Object/MachOUniversal.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <vector>

using namespace llvm;
using namespace coverage;
using namespace object;

#define DEBUG_TYPE "coverage-mapping"

void CoverageMappingIterator::increment() {
  if (ReadErr != coveragemap_error::success)
    return;

  // Check if all the records were read or if an error occurred while reading
  // the next record.
  if (auto E = Reader->readNextRecord(Record))
    handleAllErrors(std::move(E), [&](const CoverageMapError &CME) {
      if (CME.get() == coveragemap_error::eof)
        *this = CoverageMappingIterator();
      else
        ReadErr = CME.get();
    });
}

Error RawCoverageReader::readULEB128(uint64_t &Result) {
  if (Data.empty())
    return make_error<CoverageMapError>(coveragemap_error::truncated);
  unsigned N = 0;
  Result = decodeULEB128(reinterpret_cast<const uint8_t *>(Data.data()), &N);
  if (N > Data.size())
    return make_error<CoverageMapError>(coveragemap_error::malformed);
  Data = Data.substr(N);
  return Error::success();
}

Error RawCoverageReader::readIntMax(uint64_t &Result, uint64_t MaxPlus1) {
  if (auto Err = readULEB128(Result))
    return Err;
  if (Result >= MaxPlus1)
    return make_error<CoverageMapError>(coveragemap_error::malformed);
  return Error::success();
}

Error RawCoverageReader::readSize(uint64_t &Result) {
  if (auto Err = readULEB128(Result))
    return Err;
  // Sanity check the number.
  if (Result > Data.size())
    return make_error<CoverageMapError>(coveragemap_error::malformed);
  return Error::success();
}

Error RawCoverageReader::readString(StringRef &Result) {
  uint64_t Length;
  if (auto Err = readSize(Length))
    return Err;
  Result = Data.substr(0, Length);
  Data = Data.substr(Length);
  return Error::success();
}

Error RawCoverageFilenamesReader::read() {
  uint64_t NumFilenames;
  if (auto Err = readSize(NumFilenames))
    return Err;
  for (size_t I = 0; I < NumFilenames; ++I) {
    StringRef Filename;
    if (auto Err = readString(Filename))
      return Err;
    Filenames.push_back(Filename);
  }
  return Error::success();
}

Error RawCoverageMappingReader::decodeCounter(unsigned Value, Counter &C) {
  auto Tag = Value & Counter::EncodingTagMask;
  switch (Tag) {
  case Counter::Zero:
    C = Counter::getZero();
    return Error::success();
  case Counter::CounterValueReference:
    C = Counter::getCounter(Value >> Counter::EncodingTagBits);
    return Error::success();
  default:
    break;
  }
  Tag -= Counter::Expression;
  switch (Tag) {
  case CounterExpression::Subtract:
  case CounterExpression::Add: {
    auto ID = Value >> Counter::EncodingTagBits;
    if (ID >= Expressions.size())
      return make_error<CoverageMapError>(coveragemap_error::malformed);
    Expressions[ID].Kind = CounterExpression::ExprKind(Tag);
    C = Counter::getExpression(ID);
    break;
  }
  default:
    return make_error<CoverageMapError>(coveragemap_error::malformed);
  }
  return Error::success();
}

Error RawCoverageMappingReader::readCounter(Counter &C) {
  uint64_t EncodedCounter;
  if (auto Err =
          readIntMax(EncodedCounter, std::numeric_limits<unsigned>::max()))
    return Err;
  if (auto Err = decodeCounter(EncodedCounter, C))
    return Err;
  return Error::success();
}

static const unsigned EncodingExpansionRegionBit = 1
                                                   << Counter::EncodingTagBits;

/// Read the sub-array of regions for the given inferred file id.
/// \param NumFileIDs the number of file ids that are defined for this
/// function.
Error RawCoverageMappingReader::readMappingRegionsSubArray(
    std::vector<CounterMappingRegion> &MappingRegions, unsigned InferredFileID,
    size_t NumFileIDs) {
  uint64_t NumRegions;
  if (auto Err = readSize(NumRegions))
    return Err;
  unsigned LineStart = 0;
  for (size_t I = 0; I < NumRegions; ++I) {
    Counter C;
    CounterMappingRegion::RegionKind Kind = CounterMappingRegion::CodeRegion;

    // Read the combined counter + region kind.
    uint64_t EncodedCounterAndRegion;
    if (auto Err = readIntMax(EncodedCounterAndRegion,
                              std::numeric_limits<unsigned>::max()))
      return Err;
    unsigned Tag = EncodedCounterAndRegion & Counter::EncodingTagMask;
    uint64_t ExpandedFileID = 0;
    if (Tag != Counter::Zero) {
      if (auto Err = decodeCounter(EncodedCounterAndRegion, C))
        return Err;
    } else {
      // Is it an expansion region?
      if (EncodedCounterAndRegion & EncodingExpansionRegionBit) {
        Kind = CounterMappingRegion::ExpansionRegion;
        ExpandedFileID = EncodedCounterAndRegion >>
                         Counter::EncodingCounterTagAndExpansionRegionTagBits;
        if (ExpandedFileID >= NumFileIDs)
          return make_error<CoverageMapError>(coveragemap_error::malformed);
      } else {
        switch (EncodedCounterAndRegion >>
                Counter::EncodingCounterTagAndExpansionRegionTagBits) {
        case CounterMappingRegion::CodeRegion:
          // Don't do anything when we have a code region with a zero counter.
          break;
        case CounterMappingRegion::SkippedRegion:
          Kind = CounterMappingRegion::SkippedRegion;
          break;
        default:
          return make_error<CoverageMapError>(coveragemap_error::malformed);
        }
      }
    }

    // Read the source range.
    uint64_t LineStartDelta, ColumnStart, NumLines, ColumnEnd;
    if (auto Err =
            readIntMax(LineStartDelta, std::numeric_limits<unsigned>::max()))
      return Err;
    if (auto Err = readULEB128(ColumnStart))
      return Err;
    if (ColumnStart > std::numeric_limits<unsigned>::max())
      return make_error<CoverageMapError>(coveragemap_error::malformed);
    if (auto Err = readIntMax(NumLines, std::numeric_limits<unsigned>::max()))
      return Err;
    if (auto Err = readIntMax(ColumnEnd, std::numeric_limits<unsigned>::max()))
      return Err;
    LineStart += LineStartDelta;

    // If the high bit of ColumnEnd is set, this is a gap region.
    if (ColumnEnd & (1U << 31)) {
      Kind = CounterMappingRegion::GapRegion;
      ColumnEnd &= ~(1U << 31);
    }

    // Adjust the column locations for the empty regions that are supposed to
    // cover whole lines. Those regions should be encoded with the
    // column range (1 -> std::numeric_limits<unsigned>::max()), but because
    // the encoded std::numeric_limits<unsigned>::max() is several bytes long,
    // we set the column range to (0 -> 0) to ensure that the column start and
    // column end take up one byte each.
    // The std::numeric_limits<unsigned>::max() is used to represent a column
    // position at the end of the line without knowing the length of that line.
    if (ColumnStart == 0 && ColumnEnd == 0) {
      ColumnStart = 1;
      ColumnEnd = std::numeric_limits<unsigned>::max();
    }

    LLVM_DEBUG({
      dbgs() << "Counter in file " << InferredFileID << " " << LineStart << ":"
             << ColumnStart << " -> " << (LineStart + NumLines) << ":"
             << ColumnEnd << ", ";
      if (Kind == CounterMappingRegion::ExpansionRegion)
        dbgs() << "Expands to file " << ExpandedFileID;
      else
        CounterMappingContext(Expressions).dump(C, dbgs());
      dbgs() << "\n";
    });

    auto CMR = CounterMappingRegion(C, InferredFileID, ExpandedFileID,
                                    LineStart, ColumnStart,
                                    LineStart + NumLines, ColumnEnd, Kind);
    if (CMR.startLoc() > CMR.endLoc())
      return make_error<CoverageMapError>(coveragemap_error::malformed);
    MappingRegions.push_back(CMR);
  }
  return Error::success();
}

Error RawCoverageMappingReader::read() {
  // Read the virtual file mapping.
  SmallVector<unsigned, 8> VirtualFileMapping;
  uint64_t NumFileMappings;
  if (auto Err = readSize(NumFileMappings))
    return Err;
  for (size_t I = 0; I < NumFileMappings; ++I) {
    uint64_t FilenameIndex;
    if (auto Err = readIntMax(FilenameIndex, TranslationUnitFilenames.size()))
      return Err;
    VirtualFileMapping.push_back(FilenameIndex);
  }

  // Construct the files using unique filenames and virtual file mapping.
  for (auto I : VirtualFileMapping) {
    Filenames.push_back(TranslationUnitFilenames[I]);
  }

  // Read the expressions.
  uint64_t NumExpressions;
  if (auto Err = readSize(NumExpressions))
    return Err;
  // Create an array of dummy expressions that get the proper counters
  // when the expressions are read, and the proper kinds when the counters
  // are decoded.
  Expressions.resize(
      NumExpressions,
      CounterExpression(CounterExpression::Subtract, Counter(), Counter()));
  for (size_t I = 0; I < NumExpressions; ++I) {
    if (auto Err = readCounter(Expressions[I].LHS))
      return Err;
    if (auto Err = readCounter(Expressions[I].RHS))
      return Err;
  }

  // Read the mapping regions sub-arrays.
  for (unsigned InferredFileID = 0, S = VirtualFileMapping.size();
       InferredFileID < S; ++InferredFileID) {
    if (auto Err = readMappingRegionsSubArray(MappingRegions, InferredFileID,
                                              VirtualFileMapping.size()))
      return Err;
  }

  // Set the counters for the expansion regions.
  // i.e. Counter of expansion region = counter of the first region
  // from the expanded file.
  // Perform multiple passes to correctly propagate the counters through
  // all the nested expansion regions.
  SmallVector<CounterMappingRegion *, 8> FileIDExpansionRegionMapping;
  FileIDExpansionRegionMapping.resize(VirtualFileMapping.size(), nullptr);
  for (unsigned Pass = 1, S = VirtualFileMapping.size(); Pass < S; ++Pass) {
    for (auto &R : MappingRegions) {
      if (R.Kind != CounterMappingRegion::ExpansionRegion)
        continue;
      assert(!FileIDExpansionRegionMapping[R.ExpandedFileID]);
      FileIDExpansionRegionMapping[R.ExpandedFileID] = &R;
    }
    for (auto &R : MappingRegions) {
      if (FileIDExpansionRegionMapping[R.FileID]) {
        FileIDExpansionRegionMapping[R.FileID]->Count = R.Count;
        FileIDExpansionRegionMapping[R.FileID] = nullptr;
      }
    }
  }

  return Error::success();
}

Expected<bool> RawCoverageMappingDummyChecker::isDummy() {
  // A dummy coverage mapping data consists of just one region with zero count.
  uint64_t NumFileMappings;
  if (Error Err = readSize(NumFileMappings))
    return std::move(Err);
  if (NumFileMappings != 1)
    return false;
  // We don't expect any specific value for the filename index, just skip it.
  uint64_t FilenameIndex;
  if (Error Err =
          readIntMax(FilenameIndex, std::numeric_limits<unsigned>::max()))
    return std::move(Err);
  uint64_t NumExpressions;
  if (Error Err = readSize(NumExpressions))
    return std::move(Err);
  if (NumExpressions != 0)
    return false;
  uint64_t NumRegions;
  if (Error Err = readSize(NumRegions))
    return std::move(Err);
  if (NumRegions != 1)
    return false;
  uint64_t EncodedCounterAndRegion;
  if (Error Err = readIntMax(EncodedCounterAndRegion,
                             std::numeric_limits<unsigned>::max()))
    return std::move(Err);
  unsigned Tag = EncodedCounterAndRegion & Counter::EncodingTagMask;
  return Tag == Counter::Zero;
}

Error InstrProfSymtab::create(SectionRef &Section) {
  if (auto EC = Section.getContents(Data))
    return errorCodeToError(EC);
  Address = Section.getAddress();
  return Error::success();
}

StringRef InstrProfSymtab::getFuncName(uint64_t Pointer, size_t Size) {
  if (Pointer < Address)
    return StringRef();
  auto Offset = Pointer - Address;
  if (Offset + Size > Data.size())
    return StringRef();
  return Data.substr(Pointer - Address, Size);
}

// Check if the mapping data is a dummy, i.e. is emitted for an unused function.
static Expected<bool> isCoverageMappingDummy(uint64_t Hash, StringRef Mapping) {
  // The hash value of dummy mapping records is always zero.
  if (Hash)
    return false;
  return RawCoverageMappingDummyChecker(Mapping).isDummy();
}

namespace {

struct CovMapFuncRecordReader {
  virtual ~CovMapFuncRecordReader() = default;

  // The interface to read coverage mapping function records for a module.
  //
  // \p Buf points to the buffer containing the \c CovHeader of the coverage
  // mapping data associated with the module.
  //
  // Returns a pointer to the next \c CovHeader if it exists, or a pointer
  // greater than \p End if not.
  virtual Expected<const char *> readFunctionRecords(const char *Buf,
                                                     const char *End) = 0;

  template <class IntPtrT, support::endianness Endian>
  static Expected<std::unique_ptr<CovMapFuncRecordReader>>
  get(CovMapVersion Version, InstrProfSymtab &P,
      std::vector<BinaryCoverageReader::ProfileMappingRecord> &R,
      std::vector<StringRef> &F);
};

// A class for reading coverage mapping function records for a module.
template <CovMapVersion Version, class IntPtrT, support::endianness Endian>
class VersionedCovMapFuncRecordReader : public CovMapFuncRecordReader {
  using FuncRecordType =
      typename CovMapTraits<Version, IntPtrT>::CovMapFuncRecordType;
  using NameRefType = typename CovMapTraits<Version, IntPtrT>::NameRefType;

  // Maps function's name references to the indexes of their records
  // in \c Records.
  DenseMap<NameRefType, size_t> FunctionRecords;
  InstrProfSymtab &ProfileNames;
  std::vector<StringRef> &Filenames;
  std::vector<BinaryCoverageReader::ProfileMappingRecord> &Records;

  // Add the record to the collection if we don't already have a record that
  // points to the same function name. This is useful to ignore the redundant
  // records for the functions with ODR linkage.
  // In addition, prefer records with real coverage mapping data to dummy
  // records, which were emitted for inline functions which were seen but
  // not used in the corresponding translation unit.
  Error insertFunctionRecordIfNeeded(const FuncRecordType *CFR,
                                     StringRef Mapping, size_t FilenamesBegin) {
    uint64_t FuncHash = CFR->template getFuncHash<Endian>();
    NameRefType NameRef = CFR->template getFuncNameRef<Endian>();
    auto InsertResult =
        FunctionRecords.insert(std::make_pair(NameRef, Records.size()));
    if (InsertResult.second) {
      StringRef FuncName;
      if (Error Err = CFR->template getFuncName<Endian>(ProfileNames, FuncName))
        return Err;
      if (FuncName.empty())
        return make_error<InstrProfError>(instrprof_error::malformed);
      Records.emplace_back(Version, FuncName, FuncHash, Mapping, FilenamesBegin,
                           Filenames.size() - FilenamesBegin);
      return Error::success();
    }
    // Update the existing record if it's a dummy and the new record is real.
    size_t OldRecordIndex = InsertResult.first->second;
    BinaryCoverageReader::ProfileMappingRecord &OldRecord =
        Records[OldRecordIndex];
    Expected<bool> OldIsDummyExpected = isCoverageMappingDummy(
        OldRecord.FunctionHash, OldRecord.CoverageMapping);
    if (Error Err = OldIsDummyExpected.takeError())
      return Err;
    if (!*OldIsDummyExpected)
      return Error::success();
    Expected<bool> NewIsDummyExpected =
        isCoverageMappingDummy(FuncHash, Mapping);
    if (Error Err = NewIsDummyExpected.takeError())
      return Err;
    if (*NewIsDummyExpected)
      return Error::success();
    OldRecord.FunctionHash = FuncHash;
    OldRecord.CoverageMapping = Mapping;
    OldRecord.FilenamesBegin = FilenamesBegin;
    OldRecord.FilenamesSize = Filenames.size() - FilenamesBegin;
    return Error::success();
  }

public:
  VersionedCovMapFuncRecordReader(
      InstrProfSymtab &P,
      std::vector<BinaryCoverageReader::ProfileMappingRecord> &R,
      std::vector<StringRef> &F)
      : ProfileNames(P), Filenames(F), Records(R) {}

  ~VersionedCovMapFuncRecordReader() override = default;

  Expected<const char *> readFunctionRecords(const char *Buf,
                                             const char *End) override {
    using namespace support;

    if (Buf + sizeof(CovMapHeader) > End)
      return make_error<CoverageMapError>(coveragemap_error::malformed);
    auto CovHeader = reinterpret_cast<const CovMapHeader *>(Buf);
    uint32_t NRecords = CovHeader->getNRecords<Endian>();
    uint32_t FilenamesSize = CovHeader->getFilenamesSize<Endian>();
    uint32_t CoverageSize = CovHeader->getCoverageSize<Endian>();
    assert((CovMapVersion)CovHeader->getVersion<Endian>() == Version);
    Buf = reinterpret_cast<const char *>(CovHeader + 1);

    // Skip past the function records, saving the start and end for later.
    const char *FunBuf = Buf;
    Buf += NRecords * sizeof(FuncRecordType);
    const char *FunEnd = Buf;

    // Get the filenames.
    if (Buf + FilenamesSize > End)
      return make_error<CoverageMapError>(coveragemap_error::malformed);
    size_t FilenamesBegin = Filenames.size();
    RawCoverageFilenamesReader Reader(StringRef(Buf, FilenamesSize), Filenames);
    if (auto Err = Reader.read())
      return std::move(Err);
    Buf += FilenamesSize;

    // We'll read the coverage mapping records in the loop below.
    const char *CovBuf = Buf;
    Buf += CoverageSize;
    const char *CovEnd = Buf;

    if (Buf > End)
      return make_error<CoverageMapError>(coveragemap_error::malformed);
    // Each coverage map has an alignment of 8, so we need to adjust alignment
    // before reading the next map.
    Buf += alignmentAdjustment(Buf, 8);

    auto CFR = reinterpret_cast<const FuncRecordType *>(FunBuf);
    while ((const char *)CFR < FunEnd) {
      // Read the function information
      uint32_t DataSize = CFR->template getDataSize<Endian>();

      // Now use that to read the coverage data.
      if (CovBuf + DataSize > CovEnd)
        return make_error<CoverageMapError>(coveragemap_error::malformed);
      auto Mapping = StringRef(CovBuf, DataSize);
      CovBuf += DataSize;

      if (Error Err =
              insertFunctionRecordIfNeeded(CFR, Mapping, FilenamesBegin))
        return std::move(Err);
      CFR++;
    }
    return Buf;
  }
};

} // end anonymous namespace

template <class IntPtrT, support::endianness Endian>
Expected<std::unique_ptr<CovMapFuncRecordReader>> CovMapFuncRecordReader::get(
    CovMapVersion Version, InstrProfSymtab &P,
    std::vector<BinaryCoverageReader::ProfileMappingRecord> &R,
    std::vector<StringRef> &F) {
  using namespace coverage;

  switch (Version) {
  case CovMapVersion::Version1:
    return llvm::make_unique<VersionedCovMapFuncRecordReader<
        CovMapVersion::Version1, IntPtrT, Endian>>(P, R, F);
  case CovMapVersion::Version2:
  case CovMapVersion::Version3:
    // Decompress the name data.
    if (Error E = P.create(P.getNameData()))
      return std::move(E);
    if (Version == CovMapVersion::Version2)
      return llvm::make_unique<VersionedCovMapFuncRecordReader<
          CovMapVersion::Version2, IntPtrT, Endian>>(P, R, F);
    else
      return llvm::make_unique<VersionedCovMapFuncRecordReader<
          CovMapVersion::Version3, IntPtrT, Endian>>(P, R, F);
  }
  llvm_unreachable("Unsupported version");
}

template <typename T, support::endianness Endian>
static Error readCoverageMappingData(
    InstrProfSymtab &ProfileNames, StringRef Data,
    std::vector<BinaryCoverageReader::ProfileMappingRecord> &Records,
    std::vector<StringRef> &Filenames) {
  using namespace coverage;

  // Read the records in the coverage data section.
  auto CovHeader =
      reinterpret_cast<const CovMapHeader *>(Data.data());
  CovMapVersion Version = (CovMapVersion)CovHeader->getVersion<Endian>();
  if (Version > CovMapVersion::CurrentVersion)
    return make_error<CoverageMapError>(coveragemap_error::unsupported_version);
  Expected<std::unique_ptr<CovMapFuncRecordReader>> ReaderExpected =
      CovMapFuncRecordReader::get<T, Endian>(Version, ProfileNames, Records,
                                             Filenames);
  if (Error E = ReaderExpected.takeError())
    return E;
  auto Reader = std::move(ReaderExpected.get());
  for (const char *Buf = Data.data(), *End = Buf + Data.size(); Buf < End;) {
    auto NextHeaderOrErr = Reader->readFunctionRecords(Buf, End);
    if (auto E = NextHeaderOrErr.takeError())
      return E;
    Buf = NextHeaderOrErr.get();
  }
  return Error::success();
}

static const char *TestingFormatMagic = "llvmcovmtestdata";

static Error loadTestingFormat(StringRef Data, InstrProfSymtab &ProfileNames,
                               StringRef &CoverageMapping,
                               uint8_t &BytesInAddress,
                               support::endianness &Endian) {
  BytesInAddress = 8;
  Endian = support::endianness::little;

  Data = Data.substr(StringRef(TestingFormatMagic).size());
  if (Data.empty())
    return make_error<CoverageMapError>(coveragemap_error::truncated);
  unsigned N = 0;
  auto ProfileNamesSize =
      decodeULEB128(reinterpret_cast<const uint8_t *>(Data.data()), &N);
  if (N > Data.size())
    return make_error<CoverageMapError>(coveragemap_error::malformed);
  Data = Data.substr(N);
  if (Data.empty())
    return make_error<CoverageMapError>(coveragemap_error::truncated);
  N = 0;
  uint64_t Address =
      decodeULEB128(reinterpret_cast<const uint8_t *>(Data.data()), &N);
  if (N > Data.size())
    return make_error<CoverageMapError>(coveragemap_error::malformed);
  Data = Data.substr(N);
  if (Data.size() < ProfileNamesSize)
    return make_error<CoverageMapError>(coveragemap_error::malformed);
  if (Error E = ProfileNames.create(Data.substr(0, ProfileNamesSize), Address))
    return E;
  CoverageMapping = Data.substr(ProfileNamesSize);
  // Skip the padding bytes because coverage map data has an alignment of 8.
  if (CoverageMapping.empty())
    return make_error<CoverageMapError>(coveragemap_error::truncated);
  size_t Pad = alignmentAdjustment(CoverageMapping.data(), 8);
  if (CoverageMapping.size() < Pad)
    return make_error<CoverageMapError>(coveragemap_error::malformed);
  CoverageMapping = CoverageMapping.substr(Pad);
  return Error::success();
}

static Expected<SectionRef> lookupSection(ObjectFile &OF, StringRef Name) {
  StringRef FoundName;
  for (const auto &Section : OF.sections()) {
    if (auto EC = Section.getName(FoundName))
      return errorCodeToError(EC);
    if (FoundName == Name)
      return Section;
  }
  return make_error<CoverageMapError>(coveragemap_error::no_data_found);
}

static Error loadBinaryFormat(MemoryBufferRef ObjectBuffer,
                              InstrProfSymtab &ProfileNames,
                              StringRef &CoverageMapping,
                              uint8_t &BytesInAddress,
                              support::endianness &Endian, StringRef Arch) {
  auto BinOrErr = createBinary(ObjectBuffer);
  if (!BinOrErr)
    return BinOrErr.takeError();
  auto Bin = std::move(BinOrErr.get());
  std::unique_ptr<ObjectFile> OF;
  if (auto *Universal = dyn_cast<MachOUniversalBinary>(Bin.get())) {
    // If we have a universal binary, try to look up the object for the
    // appropriate architecture.
    auto ObjectFileOrErr = Universal->getObjectForArch(Arch);
    if (!ObjectFileOrErr)
      return ObjectFileOrErr.takeError();
    OF = std::move(ObjectFileOrErr.get());
  } else if (isa<ObjectFile>(Bin.get())) {
    // For any other object file, upcast and take ownership.
    OF.reset(cast<ObjectFile>(Bin.release()));
    // If we've asked for a particular arch, make sure they match.
    if (!Arch.empty() && OF->getArch() != Triple(Arch).getArch())
      return errorCodeToError(object_error::arch_not_found);
  } else
    // We can only handle object files.
    return make_error<CoverageMapError>(coveragemap_error::malformed);

  // The coverage uses native pointer sizes for the object it's written in.
  BytesInAddress = OF->getBytesInAddress();
  Endian = OF->isLittleEndian() ? support::endianness::little
                                : support::endianness::big;

  // Look for the sections that we are interested in.
  auto ObjFormat = OF->getTripleObjectFormat();
  auto NamesSection =
      lookupSection(*OF, getInstrProfSectionName(IPSK_name, ObjFormat,
                                                 /*AddSegmentInfo=*/false));
  if (auto E = NamesSection.takeError())
    return E;
  auto CoverageSection =
      lookupSection(*OF, getInstrProfSectionName(IPSK_covmap, ObjFormat,
                                                 /*AddSegmentInfo=*/false));
  if (auto E = CoverageSection.takeError())
    return E;

  // Get the contents of the given sections.
  if (auto EC = CoverageSection->getContents(CoverageMapping))
    return errorCodeToError(EC);
  if (Error E = ProfileNames.create(*NamesSection))
    return E;

  return Error::success();
}

Expected<std::unique_ptr<BinaryCoverageReader>>
BinaryCoverageReader::create(std::unique_ptr<MemoryBuffer> &ObjectBuffer,
                             StringRef Arch) {
  std::unique_ptr<BinaryCoverageReader> Reader(new BinaryCoverageReader());

  StringRef Coverage;
  uint8_t BytesInAddress;
  support::endianness Endian;
  Error E = Error::success();
  consumeError(std::move(E));
  if (ObjectBuffer->getBuffer().startswith(TestingFormatMagic))
    // This is a special format used for testing.
    E = loadTestingFormat(ObjectBuffer->getBuffer(), Reader->ProfileNames,
                          Coverage, BytesInAddress, Endian);
  else
    E = loadBinaryFormat(ObjectBuffer->getMemBufferRef(), Reader->ProfileNames,
                         Coverage, BytesInAddress, Endian, Arch);
  if (E)
    return std::move(E);

  if (BytesInAddress == 4 && Endian == support::endianness::little)
    E = readCoverageMappingData<uint32_t, support::endianness::little>(
        Reader->ProfileNames, Coverage, Reader->MappingRecords,
        Reader->Filenames);
  else if (BytesInAddress == 4 && Endian == support::endianness::big)
    E = readCoverageMappingData<uint32_t, support::endianness::big>(
        Reader->ProfileNames, Coverage, Reader->MappingRecords,
        Reader->Filenames);
  else if (BytesInAddress == 8 && Endian == support::endianness::little)
    E = readCoverageMappingData<uint64_t, support::endianness::little>(
        Reader->ProfileNames, Coverage, Reader->MappingRecords,
        Reader->Filenames);
  else if (BytesInAddress == 8 && Endian == support::endianness::big)
    E = readCoverageMappingData<uint64_t, support::endianness::big>(
        Reader->ProfileNames, Coverage, Reader->MappingRecords,
        Reader->Filenames);
  else
    return make_error<CoverageMapError>(coveragemap_error::malformed);
  if (E)
    return std::move(E);
  return std::move(Reader);
}

Error BinaryCoverageReader::readNextRecord(CoverageMappingRecord &Record) {
  if (CurrentRecord >= MappingRecords.size())
    return make_error<CoverageMapError>(coveragemap_error::eof);

  FunctionsFilenames.clear();
  Expressions.clear();
  MappingRegions.clear();
  auto &R = MappingRecords[CurrentRecord];
  RawCoverageMappingReader Reader(
      R.CoverageMapping,
      makeArrayRef(Filenames).slice(R.FilenamesBegin, R.FilenamesSize),
      FunctionsFilenames, Expressions, MappingRegions);
  if (auto Err = Reader.read())
    return Err;

  Record.FunctionName = R.FunctionName;
  Record.FunctionHash = R.FunctionHash;
  Record.Filenames = FunctionsFilenames;
  Record.Expressions = Expressions;
  Record.MappingRegions = MappingRegions;

  ++CurrentRecord;
  return Error::success();
}
