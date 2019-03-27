//===- CoverageMappingReader.h - Code coverage mapping reader ---*- C++ -*-===//
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

#ifndef LLVM_PROFILEDATA_COVERAGE_COVERAGEMAPPINGREADER_H
#define LLVM_PROFILEDATA_COVERAGE_COVERAGEMAPPINGREADER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ProfileData/Coverage/CoverageMapping.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <vector>

namespace llvm {
namespace coverage {

class CoverageMappingReader;

/// Coverage mapping information for a single function.
struct CoverageMappingRecord {
  StringRef FunctionName;
  uint64_t FunctionHash;
  ArrayRef<StringRef> Filenames;
  ArrayRef<CounterExpression> Expressions;
  ArrayRef<CounterMappingRegion> MappingRegions;
};

/// A file format agnostic iterator over coverage mapping data.
class CoverageMappingIterator
    : public std::iterator<std::input_iterator_tag, CoverageMappingRecord> {
  CoverageMappingReader *Reader;
  CoverageMappingRecord Record;
  coveragemap_error ReadErr;

  void increment();

public:
  CoverageMappingIterator()
      : Reader(nullptr), Record(), ReadErr(coveragemap_error::success) {}

  CoverageMappingIterator(CoverageMappingReader *Reader)
      : Reader(Reader), Record(), ReadErr(coveragemap_error::success) {
    increment();
  }

  ~CoverageMappingIterator() {
    if (ReadErr != coveragemap_error::success)
      llvm_unreachable("Unexpected error in coverage mapping iterator");
  }

  CoverageMappingIterator &operator++() {
    increment();
    return *this;
  }
  bool operator==(const CoverageMappingIterator &RHS) {
    return Reader == RHS.Reader;
  }
  bool operator!=(const CoverageMappingIterator &RHS) {
    return Reader != RHS.Reader;
  }
  Expected<CoverageMappingRecord &> operator*() {
    if (ReadErr != coveragemap_error::success) {
      auto E = make_error<CoverageMapError>(ReadErr);
      ReadErr = coveragemap_error::success;
      return std::move(E);
    }
    return Record;
  }
  Expected<CoverageMappingRecord *> operator->() {
    if (ReadErr != coveragemap_error::success) {
      auto E = make_error<CoverageMapError>(ReadErr);
      ReadErr = coveragemap_error::success;
      return std::move(E);
    }
    return &Record;
  }
};

class CoverageMappingReader {
public:
  virtual ~CoverageMappingReader() = default;

  virtual Error readNextRecord(CoverageMappingRecord &Record) = 0;
  CoverageMappingIterator begin() { return CoverageMappingIterator(this); }
  CoverageMappingIterator end() { return CoverageMappingIterator(); }
};

/// Base class for the raw coverage mapping and filenames data readers.
class RawCoverageReader {
protected:
  StringRef Data;

  RawCoverageReader(StringRef Data) : Data(Data) {}

  Error readULEB128(uint64_t &Result);
  Error readIntMax(uint64_t &Result, uint64_t MaxPlus1);
  Error readSize(uint64_t &Result);
  Error readString(StringRef &Result);
};

/// Reader for the raw coverage filenames.
class RawCoverageFilenamesReader : public RawCoverageReader {
  std::vector<StringRef> &Filenames;

public:
  RawCoverageFilenamesReader(StringRef Data, std::vector<StringRef> &Filenames)
      : RawCoverageReader(Data), Filenames(Filenames) {}
  RawCoverageFilenamesReader(const RawCoverageFilenamesReader &) = delete;
  RawCoverageFilenamesReader &
  operator=(const RawCoverageFilenamesReader &) = delete;

  Error read();
};

/// Checks if the given coverage mapping data is exported for
/// an unused function.
class RawCoverageMappingDummyChecker : public RawCoverageReader {
public:
  RawCoverageMappingDummyChecker(StringRef MappingData)
      : RawCoverageReader(MappingData) {}

  Expected<bool> isDummy();
};

/// Reader for the raw coverage mapping data.
class RawCoverageMappingReader : public RawCoverageReader {
  ArrayRef<StringRef> TranslationUnitFilenames;
  std::vector<StringRef> &Filenames;
  std::vector<CounterExpression> &Expressions;
  std::vector<CounterMappingRegion> &MappingRegions;

public:
  RawCoverageMappingReader(StringRef MappingData,
                           ArrayRef<StringRef> TranslationUnitFilenames,
                           std::vector<StringRef> &Filenames,
                           std::vector<CounterExpression> &Expressions,
                           std::vector<CounterMappingRegion> &MappingRegions)
      : RawCoverageReader(MappingData),
        TranslationUnitFilenames(TranslationUnitFilenames),
        Filenames(Filenames), Expressions(Expressions),
        MappingRegions(MappingRegions) {}
  RawCoverageMappingReader(const RawCoverageMappingReader &) = delete;
  RawCoverageMappingReader &
  operator=(const RawCoverageMappingReader &) = delete;

  Error read();

private:
  Error decodeCounter(unsigned Value, Counter &C);
  Error readCounter(Counter &C);
  Error
  readMappingRegionsSubArray(std::vector<CounterMappingRegion> &MappingRegions,
                             unsigned InferredFileID, size_t NumFileIDs);
};

/// Reader for the coverage mapping data that is emitted by the
/// frontend and stored in an object file.
class BinaryCoverageReader : public CoverageMappingReader {
public:
  struct ProfileMappingRecord {
    CovMapVersion Version;
    StringRef FunctionName;
    uint64_t FunctionHash;
    StringRef CoverageMapping;
    size_t FilenamesBegin;
    size_t FilenamesSize;

    ProfileMappingRecord(CovMapVersion Version, StringRef FunctionName,
                         uint64_t FunctionHash, StringRef CoverageMapping,
                         size_t FilenamesBegin, size_t FilenamesSize)
        : Version(Version), FunctionName(FunctionName),
          FunctionHash(FunctionHash), CoverageMapping(CoverageMapping),
          FilenamesBegin(FilenamesBegin), FilenamesSize(FilenamesSize) {}
  };

private:
  std::vector<StringRef> Filenames;
  std::vector<ProfileMappingRecord> MappingRecords;
  InstrProfSymtab ProfileNames;
  size_t CurrentRecord = 0;
  std::vector<StringRef> FunctionsFilenames;
  std::vector<CounterExpression> Expressions;
  std::vector<CounterMappingRegion> MappingRegions;

  BinaryCoverageReader() = default;

public:
  BinaryCoverageReader(const BinaryCoverageReader &) = delete;
  BinaryCoverageReader &operator=(const BinaryCoverageReader &) = delete;

  static Expected<std::unique_ptr<BinaryCoverageReader>>
  create(std::unique_ptr<MemoryBuffer> &ObjectBuffer,
         StringRef Arch);

  Error readNextRecord(CoverageMappingRecord &Record) override;
};

} // end namespace coverage
} // end namespace llvm

#endif // LLVM_PROFILEDATA_COVERAGE_COVERAGEMAPPINGREADER_H
