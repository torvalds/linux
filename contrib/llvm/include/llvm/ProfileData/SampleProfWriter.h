//===- SampleProfWriter.h - Write LLVM sample profile data ------*- C++ -*-===//
//
//                      The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains definitions needed for writing sample profiles.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_PROFILEDATA_SAMPLEPROFWRITER_H
#define LLVM_PROFILEDATA_SAMPLEPROFWRITER_H

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/ProfileSummary.h"
#include "llvm/ProfileData/SampleProf.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <set>
#include <system_error>

namespace llvm {
namespace sampleprof {

/// Sample-based profile writer. Base class.
class SampleProfileWriter {
public:
  virtual ~SampleProfileWriter() = default;

  /// Write sample profiles in \p S.
  ///
  /// \returns status code of the file update operation.
  virtual std::error_code write(const FunctionSamples &S) = 0;

  /// Write all the sample profiles in the given map of samples.
  ///
  /// \returns status code of the file update operation.
  virtual std::error_code write(const StringMap<FunctionSamples> &ProfileMap);

  raw_ostream &getOutputStream() { return *OutputStream; }

  /// Profile writer factory.
  ///
  /// Create a new file writer based on the value of \p Format.
  static ErrorOr<std::unique_ptr<SampleProfileWriter>>
  create(StringRef Filename, SampleProfileFormat Format);

  /// Create a new stream writer based on the value of \p Format.
  /// For testing.
  static ErrorOr<std::unique_ptr<SampleProfileWriter>>
  create(std::unique_ptr<raw_ostream> &OS, SampleProfileFormat Format);

protected:
  SampleProfileWriter(std::unique_ptr<raw_ostream> &OS)
      : OutputStream(std::move(OS)) {}

  /// Write a file header for the profile file.
  virtual std::error_code
  writeHeader(const StringMap<FunctionSamples> &ProfileMap) = 0;

  /// Output stream where to emit the profile to.
  std::unique_ptr<raw_ostream> OutputStream;

  /// Profile summary.
  std::unique_ptr<ProfileSummary> Summary;

  /// Compute summary for this profile.
  void computeSummary(const StringMap<FunctionSamples> &ProfileMap);
};

/// Sample-based profile writer (text format).
class SampleProfileWriterText : public SampleProfileWriter {
public:
  std::error_code write(const FunctionSamples &S) override;

protected:
  SampleProfileWriterText(std::unique_ptr<raw_ostream> &OS)
      : SampleProfileWriter(OS), Indent(0) {}

  std::error_code
  writeHeader(const StringMap<FunctionSamples> &ProfileMap) override {
    return sampleprof_error::success;
  }

private:
  /// Indent level to use when writing.
  ///
  /// This is used when printing inlined callees.
  unsigned Indent;

  friend ErrorOr<std::unique_ptr<SampleProfileWriter>>
  SampleProfileWriter::create(std::unique_ptr<raw_ostream> &OS,
                              SampleProfileFormat Format);
};

/// Sample-based profile writer (binary format).
class SampleProfileWriterBinary : public SampleProfileWriter {
public:
  virtual std::error_code write(const FunctionSamples &S) override;
  SampleProfileWriterBinary(std::unique_ptr<raw_ostream> &OS)
      : SampleProfileWriter(OS) {}

protected:
  virtual std::error_code writeNameTable() = 0;
  virtual std::error_code writeMagicIdent() = 0;
  virtual std::error_code
  writeHeader(const StringMap<FunctionSamples> &ProfileMap) override;
  std::error_code writeSummary();
  std::error_code writeNameIdx(StringRef FName);
  std::error_code writeBody(const FunctionSamples &S);
  inline void stablizeNameTable(std::set<StringRef> &V);

  MapVector<StringRef, uint32_t> NameTable;

private:
  void addName(StringRef FName);
  void addNames(const FunctionSamples &S);

  friend ErrorOr<std::unique_ptr<SampleProfileWriter>>
  SampleProfileWriter::create(std::unique_ptr<raw_ostream> &OS,
                              SampleProfileFormat Format);
};

class SampleProfileWriterRawBinary : public SampleProfileWriterBinary {
  using SampleProfileWriterBinary::SampleProfileWriterBinary;

protected:
  virtual std::error_code writeNameTable() override;
  virtual std::error_code writeMagicIdent() override;
};

// CompactBinary is a compact format of binary profile which both reduces
// the profile size and the load time needed when compiling. It has two
// major difference with Binary format.
// 1. It represents all the strings in name table using md5 hash.
// 2. It saves a function offset table which maps function name index to
// the offset of its function profile to the start of the binary profile,
// so by using the function offset table, for those function profiles which
// will not be needed when compiling a module, the profile reader does't
// have to read them and it saves compile time if the profile size is huge.
// The layout of the compact format is shown as follows:
//
//    Part1: Profile header, the same as binary format, containing magic
//           number, version, summary, name table...
//    Part2: Function Offset Table Offset, which saves the position of
//           Part4.
//    Part3: Function profile collection
//             function1 profile start
//                 ....
//             function2 profile start
//                 ....
//             function3 profile start
//                 ....
//                ......
//    Part4: Function Offset Table
//             function1 name index --> function1 profile start
//             function2 name index --> function2 profile start
//             function3 name index --> function3 profile start
//
// We need Part2 because profile reader can use it to find out and read
// function offset table without reading Part3 first.
class SampleProfileWriterCompactBinary : public SampleProfileWriterBinary {
  using SampleProfileWriterBinary::SampleProfileWriterBinary;

public:
  virtual std::error_code write(const FunctionSamples &S) override;
  virtual std::error_code
  write(const StringMap<FunctionSamples> &ProfileMap) override;

protected:
  /// The table mapping from function name to the offset of its FunctionSample
  /// towards profile start.
  MapVector<StringRef, uint64_t> FuncOffsetTable;
  /// The offset of the slot to be filled with the offset of FuncOffsetTable
  /// towards profile start.
  uint64_t TableOffset;
  virtual std::error_code writeNameTable() override;
  virtual std::error_code writeMagicIdent() override;
  virtual std::error_code
  writeHeader(const StringMap<FunctionSamples> &ProfileMap) override;
  std::error_code writeFuncOffsetTable();
};

} // end namespace sampleprof
} // end namespace llvm

#endif // LLVM_PROFILEDATA_SAMPLEPROFWRITER_H
