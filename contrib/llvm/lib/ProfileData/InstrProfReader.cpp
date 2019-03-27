//===- InstrProfReader.cpp - Instrumented profiling reader ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains support for reading profiling data for clang's
// instrumentation based PGO and coverage.
//
//===----------------------------------------------------------------------===//

#include "llvm/ProfileData/InstrProfReader.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/ProfileSummary.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/ProfileData/ProfileCommon.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SymbolRemappingReader.h"
#include "llvm/Support/SwapByteOrder.h"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <system_error>
#include <utility>
#include <vector>

using namespace llvm;

static Expected<std::unique_ptr<MemoryBuffer>>
setupMemoryBuffer(const Twine &Path) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> BufferOrErr =
      MemoryBuffer::getFileOrSTDIN(Path);
  if (std::error_code EC = BufferOrErr.getError())
    return errorCodeToError(EC);
  return std::move(BufferOrErr.get());
}

static Error initializeReader(InstrProfReader &Reader) {
  return Reader.readHeader();
}

Expected<std::unique_ptr<InstrProfReader>>
InstrProfReader::create(const Twine &Path) {
  // Set up the buffer to read.
  auto BufferOrError = setupMemoryBuffer(Path);
  if (Error E = BufferOrError.takeError())
    return std::move(E);
  return InstrProfReader::create(std::move(BufferOrError.get()));
}

Expected<std::unique_ptr<InstrProfReader>>
InstrProfReader::create(std::unique_ptr<MemoryBuffer> Buffer) {
  // Sanity check the buffer.
  if (uint64_t(Buffer->getBufferSize()) > std::numeric_limits<unsigned>::max())
    return make_error<InstrProfError>(instrprof_error::too_large);

  if (Buffer->getBufferSize() == 0)
    return make_error<InstrProfError>(instrprof_error::empty_raw_profile);

  std::unique_ptr<InstrProfReader> Result;
  // Create the reader.
  if (IndexedInstrProfReader::hasFormat(*Buffer))
    Result.reset(new IndexedInstrProfReader(std::move(Buffer)));
  else if (RawInstrProfReader64::hasFormat(*Buffer))
    Result.reset(new RawInstrProfReader64(std::move(Buffer)));
  else if (RawInstrProfReader32::hasFormat(*Buffer))
    Result.reset(new RawInstrProfReader32(std::move(Buffer)));
  else if (TextInstrProfReader::hasFormat(*Buffer))
    Result.reset(new TextInstrProfReader(std::move(Buffer)));
  else
    return make_error<InstrProfError>(instrprof_error::unrecognized_format);

  // Initialize the reader and return the result.
  if (Error E = initializeReader(*Result))
    return std::move(E);

  return std::move(Result);
}

Expected<std::unique_ptr<IndexedInstrProfReader>>
IndexedInstrProfReader::create(const Twine &Path, const Twine &RemappingPath) {
  // Set up the buffer to read.
  auto BufferOrError = setupMemoryBuffer(Path);
  if (Error E = BufferOrError.takeError())
    return std::move(E);

  // Set up the remapping buffer if requested.
  std::unique_ptr<MemoryBuffer> RemappingBuffer;
  std::string RemappingPathStr = RemappingPath.str();
  if (!RemappingPathStr.empty()) {
    auto RemappingBufferOrError = setupMemoryBuffer(RemappingPathStr);
    if (Error E = RemappingBufferOrError.takeError())
      return std::move(E);
    RemappingBuffer = std::move(RemappingBufferOrError.get());
  }

  return IndexedInstrProfReader::create(std::move(BufferOrError.get()),
                                        std::move(RemappingBuffer));
}

Expected<std::unique_ptr<IndexedInstrProfReader>>
IndexedInstrProfReader::create(std::unique_ptr<MemoryBuffer> Buffer,
                               std::unique_ptr<MemoryBuffer> RemappingBuffer) {
  // Sanity check the buffer.
  if (uint64_t(Buffer->getBufferSize()) > std::numeric_limits<unsigned>::max())
    return make_error<InstrProfError>(instrprof_error::too_large);

  // Create the reader.
  if (!IndexedInstrProfReader::hasFormat(*Buffer))
    return make_error<InstrProfError>(instrprof_error::bad_magic);
  auto Result = llvm::make_unique<IndexedInstrProfReader>(
      std::move(Buffer), std::move(RemappingBuffer));

  // Initialize the reader and return the result.
  if (Error E = initializeReader(*Result))
    return std::move(E);

  return std::move(Result);
}

void InstrProfIterator::Increment() {
  if (auto E = Reader->readNextRecord(Record)) {
    // Handle errors in the reader.
    InstrProfError::take(std::move(E));
    *this = InstrProfIterator();
  }
}

bool TextInstrProfReader::hasFormat(const MemoryBuffer &Buffer) {
  // Verify that this really looks like plain ASCII text by checking a
  // 'reasonable' number of characters (up to profile magic size).
  size_t count = std::min(Buffer.getBufferSize(), sizeof(uint64_t));
  StringRef buffer = Buffer.getBufferStart();
  return count == 0 ||
         std::all_of(buffer.begin(), buffer.begin() + count,
                     [](char c) { return isPrint(c) || ::isspace(c); });
}

// Read the profile variant flag from the header: ":FE" means this is a FE
// generated profile. ":IR" means this is an IR level profile. Other strings
// with a leading ':' will be reported an error format.
Error TextInstrProfReader::readHeader() {
  Symtab.reset(new InstrProfSymtab());
  bool IsIRInstr = false;
  if (!Line->startswith(":")) {
    IsIRLevelProfile = false;
    return success();
  }
  StringRef Str = (Line)->substr(1);
  if (Str.equals_lower("ir"))
    IsIRInstr = true;
  else if (Str.equals_lower("fe"))
    IsIRInstr = false;
  else
    return error(instrprof_error::bad_header);

  ++Line;
  IsIRLevelProfile = IsIRInstr;
  return success();
}

Error
TextInstrProfReader::readValueProfileData(InstrProfRecord &Record) {

#define CHECK_LINE_END(Line)                                                   \
  if (Line.is_at_end())                                                        \
    return error(instrprof_error::truncated);
#define READ_NUM(Str, Dst)                                                     \
  if ((Str).getAsInteger(10, (Dst)))                                           \
    return error(instrprof_error::malformed);
#define VP_READ_ADVANCE(Val)                                                   \
  CHECK_LINE_END(Line);                                                        \
  uint32_t Val;                                                                \
  READ_NUM((*Line), (Val));                                                    \
  Line++;

  if (Line.is_at_end())
    return success();

  uint32_t NumValueKinds;
  if (Line->getAsInteger(10, NumValueKinds)) {
    // No value profile data
    return success();
  }
  if (NumValueKinds == 0 || NumValueKinds > IPVK_Last + 1)
    return error(instrprof_error::malformed);
  Line++;

  for (uint32_t VK = 0; VK < NumValueKinds; VK++) {
    VP_READ_ADVANCE(ValueKind);
    if (ValueKind > IPVK_Last)
      return error(instrprof_error::malformed);
    VP_READ_ADVANCE(NumValueSites);
    if (!NumValueSites)
      continue;

    Record.reserveSites(VK, NumValueSites);
    for (uint32_t S = 0; S < NumValueSites; S++) {
      VP_READ_ADVANCE(NumValueData);

      std::vector<InstrProfValueData> CurrentValues;
      for (uint32_t V = 0; V < NumValueData; V++) {
        CHECK_LINE_END(Line);
        std::pair<StringRef, StringRef> VD = Line->rsplit(':');
        uint64_t TakenCount, Value;
        if (ValueKind == IPVK_IndirectCallTarget) {
          if (InstrProfSymtab::isExternalSymbol(VD.first)) {
            Value = 0;
          } else {
            if (Error E = Symtab->addFuncName(VD.first))
              return E;
            Value = IndexedInstrProf::ComputeHash(VD.first);
          }
        } else {
          READ_NUM(VD.first, Value);
        }
        READ_NUM(VD.second, TakenCount);
        CurrentValues.push_back({Value, TakenCount});
        Line++;
      }
      Record.addValueData(ValueKind, S, CurrentValues.data(), NumValueData,
                          nullptr);
    }
  }
  return success();

#undef CHECK_LINE_END
#undef READ_NUM
#undef VP_READ_ADVANCE
}

Error TextInstrProfReader::readNextRecord(NamedInstrProfRecord &Record) {
  // Skip empty lines and comments.
  while (!Line.is_at_end() && (Line->empty() || Line->startswith("#")))
    ++Line;
  // If we hit EOF while looking for a name, we're done.
  if (Line.is_at_end()) {
    return error(instrprof_error::eof);
  }

  // Read the function name.
  Record.Name = *Line++;
  if (Error E = Symtab->addFuncName(Record.Name))
    return error(std::move(E));

  // Read the function hash.
  if (Line.is_at_end())
    return error(instrprof_error::truncated);
  if ((Line++)->getAsInteger(0, Record.Hash))
    return error(instrprof_error::malformed);

  // Read the number of counters.
  uint64_t NumCounters;
  if (Line.is_at_end())
    return error(instrprof_error::truncated);
  if ((Line++)->getAsInteger(10, NumCounters))
    return error(instrprof_error::malformed);
  if (NumCounters == 0)
    return error(instrprof_error::malformed);

  // Read each counter and fill our internal storage with the values.
  Record.Clear();
  Record.Counts.reserve(NumCounters);
  for (uint64_t I = 0; I < NumCounters; ++I) {
    if (Line.is_at_end())
      return error(instrprof_error::truncated);
    uint64_t Count;
    if ((Line++)->getAsInteger(10, Count))
      return error(instrprof_error::malformed);
    Record.Counts.push_back(Count);
  }

  // Check if value profile data exists and read it if so.
  if (Error E = readValueProfileData(Record))
    return error(std::move(E));

  return success();
}

template <class IntPtrT>
bool RawInstrProfReader<IntPtrT>::hasFormat(const MemoryBuffer &DataBuffer) {
  if (DataBuffer.getBufferSize() < sizeof(uint64_t))
    return false;
  uint64_t Magic =
    *reinterpret_cast<const uint64_t *>(DataBuffer.getBufferStart());
  return RawInstrProf::getMagic<IntPtrT>() == Magic ||
         sys::getSwappedBytes(RawInstrProf::getMagic<IntPtrT>()) == Magic;
}

template <class IntPtrT>
Error RawInstrProfReader<IntPtrT>::readHeader() {
  if (!hasFormat(*DataBuffer))
    return error(instrprof_error::bad_magic);
  if (DataBuffer->getBufferSize() < sizeof(RawInstrProf::Header))
    return error(instrprof_error::bad_header);
  auto *Header = reinterpret_cast<const RawInstrProf::Header *>(
      DataBuffer->getBufferStart());
  ShouldSwapBytes = Header->Magic != RawInstrProf::getMagic<IntPtrT>();
  return readHeader(*Header);
}

template <class IntPtrT>
Error RawInstrProfReader<IntPtrT>::readNextHeader(const char *CurrentPos) {
  const char *End = DataBuffer->getBufferEnd();
  // Skip zero padding between profiles.
  while (CurrentPos != End && *CurrentPos == 0)
    ++CurrentPos;
  // If there's nothing left, we're done.
  if (CurrentPos == End)
    return make_error<InstrProfError>(instrprof_error::eof);
  // If there isn't enough space for another header, this is probably just
  // garbage at the end of the file.
  if (CurrentPos + sizeof(RawInstrProf::Header) > End)
    return make_error<InstrProfError>(instrprof_error::malformed);
  // The writer ensures each profile is padded to start at an aligned address.
  if (reinterpret_cast<size_t>(CurrentPos) % alignof(uint64_t))
    return make_error<InstrProfError>(instrprof_error::malformed);
  // The magic should have the same byte order as in the previous header.
  uint64_t Magic = *reinterpret_cast<const uint64_t *>(CurrentPos);
  if (Magic != swap(RawInstrProf::getMagic<IntPtrT>()))
    return make_error<InstrProfError>(instrprof_error::bad_magic);

  // There's another profile to read, so we need to process the header.
  auto *Header = reinterpret_cast<const RawInstrProf::Header *>(CurrentPos);
  return readHeader(*Header);
}

template <class IntPtrT>
Error RawInstrProfReader<IntPtrT>::createSymtab(InstrProfSymtab &Symtab) {
  if (Error E = Symtab.create(StringRef(NamesStart, NamesSize)))
    return error(std::move(E));
  for (const RawInstrProf::ProfileData<IntPtrT> *I = Data; I != DataEnd; ++I) {
    const IntPtrT FPtr = swap(I->FunctionPointer);
    if (!FPtr)
      continue;
    Symtab.mapAddress(FPtr, I->NameRef);
  }
  return success();
}

template <class IntPtrT>
Error RawInstrProfReader<IntPtrT>::readHeader(
    const RawInstrProf::Header &Header) {
  Version = swap(Header.Version);
  if (GET_VERSION(Version) != RawInstrProf::Version)
    return error(instrprof_error::unsupported_version);

  CountersDelta = swap(Header.CountersDelta);
  NamesDelta = swap(Header.NamesDelta);
  auto DataSize = swap(Header.DataSize);
  auto CountersSize = swap(Header.CountersSize);
  NamesSize = swap(Header.NamesSize);
  ValueKindLast = swap(Header.ValueKindLast);

  auto DataSizeInBytes = DataSize * sizeof(RawInstrProf::ProfileData<IntPtrT>);
  auto PaddingSize = getNumPaddingBytes(NamesSize);

  ptrdiff_t DataOffset = sizeof(RawInstrProf::Header);
  ptrdiff_t CountersOffset = DataOffset + DataSizeInBytes;
  ptrdiff_t NamesOffset = CountersOffset + sizeof(uint64_t) * CountersSize;
  ptrdiff_t ValueDataOffset = NamesOffset + NamesSize + PaddingSize;

  auto *Start = reinterpret_cast<const char *>(&Header);
  if (Start + ValueDataOffset > DataBuffer->getBufferEnd())
    return error(instrprof_error::bad_header);

  Data = reinterpret_cast<const RawInstrProf::ProfileData<IntPtrT> *>(
      Start + DataOffset);
  DataEnd = Data + DataSize;
  CountersStart = reinterpret_cast<const uint64_t *>(Start + CountersOffset);
  NamesStart = Start + NamesOffset;
  ValueDataStart = reinterpret_cast<const uint8_t *>(Start + ValueDataOffset);

  std::unique_ptr<InstrProfSymtab> NewSymtab = make_unique<InstrProfSymtab>();
  if (Error E = createSymtab(*NewSymtab.get()))
    return E;

  Symtab = std::move(NewSymtab);
  return success();
}

template <class IntPtrT>
Error RawInstrProfReader<IntPtrT>::readName(NamedInstrProfRecord &Record) {
  Record.Name = getName(Data->NameRef);
  return success();
}

template <class IntPtrT>
Error RawInstrProfReader<IntPtrT>::readFuncHash(NamedInstrProfRecord &Record) {
  Record.Hash = swap(Data->FuncHash);
  return success();
}

template <class IntPtrT>
Error RawInstrProfReader<IntPtrT>::readRawCounts(
    InstrProfRecord &Record) {
  uint32_t NumCounters = swap(Data->NumCounters);
  IntPtrT CounterPtr = Data->CounterPtr;
  if (NumCounters == 0)
    return error(instrprof_error::malformed);

  auto RawCounts = makeArrayRef(getCounter(CounterPtr), NumCounters);
  auto *NamesStartAsCounter = reinterpret_cast<const uint64_t *>(NamesStart);

  // Check bounds.
  if (RawCounts.data() < CountersStart ||
      RawCounts.data() + RawCounts.size() > NamesStartAsCounter)
    return error(instrprof_error::malformed);

  if (ShouldSwapBytes) {
    Record.Counts.clear();
    Record.Counts.reserve(RawCounts.size());
    for (uint64_t Count : RawCounts)
      Record.Counts.push_back(swap(Count));
  } else
    Record.Counts = RawCounts;

  return success();
}

template <class IntPtrT>
Error RawInstrProfReader<IntPtrT>::readValueProfilingData(
    InstrProfRecord &Record) {
  Record.clearValueData();
  CurValueDataSize = 0;
  // Need to match the logic in value profile dumper code in compiler-rt:
  uint32_t NumValueKinds = 0;
  for (uint32_t I = 0; I < IPVK_Last + 1; I++)
    NumValueKinds += (Data->NumValueSites[I] != 0);

  if (!NumValueKinds)
    return success();

  Expected<std::unique_ptr<ValueProfData>> VDataPtrOrErr =
      ValueProfData::getValueProfData(
          ValueDataStart, (const unsigned char *)DataBuffer->getBufferEnd(),
          getDataEndianness());

  if (Error E = VDataPtrOrErr.takeError())
    return E;

  // Note that besides deserialization, this also performs the conversion for
  // indirect call targets.  The function pointers from the raw profile are
  // remapped into function name hashes.
  VDataPtrOrErr.get()->deserializeTo(Record, Symtab.get());
  CurValueDataSize = VDataPtrOrErr.get()->getSize();
  return success();
}

template <class IntPtrT>
Error RawInstrProfReader<IntPtrT>::readNextRecord(NamedInstrProfRecord &Record) {
  if (atEnd())
    // At this point, ValueDataStart field points to the next header.
    if (Error E = readNextHeader(getNextHeaderPos()))
      return error(std::move(E));

  // Read name ad set it in Record.
  if (Error E = readName(Record))
    return error(std::move(E));

  // Read FuncHash and set it in Record.
  if (Error E = readFuncHash(Record))
    return error(std::move(E));

  // Read raw counts and set Record.
  if (Error E = readRawCounts(Record))
    return error(std::move(E));

  // Read value data and set Record.
  if (Error E = readValueProfilingData(Record))
    return error(std::move(E));

  // Iterate.
  advanceData();
  return success();
}

namespace llvm {

template class RawInstrProfReader<uint32_t>;
template class RawInstrProfReader<uint64_t>;

} // end namespace llvm

InstrProfLookupTrait::hash_value_type
InstrProfLookupTrait::ComputeHash(StringRef K) {
  return IndexedInstrProf::ComputeHash(HashType, K);
}

using data_type = InstrProfLookupTrait::data_type;
using offset_type = InstrProfLookupTrait::offset_type;

bool InstrProfLookupTrait::readValueProfilingData(
    const unsigned char *&D, const unsigned char *const End) {
  Expected<std::unique_ptr<ValueProfData>> VDataPtrOrErr =
      ValueProfData::getValueProfData(D, End, ValueProfDataEndianness);

  if (VDataPtrOrErr.takeError())
    return false;

  VDataPtrOrErr.get()->deserializeTo(DataBuffer.back(), nullptr);
  D += VDataPtrOrErr.get()->TotalSize;

  return true;
}

data_type InstrProfLookupTrait::ReadData(StringRef K, const unsigned char *D,
                                         offset_type N) {
  using namespace support;

  // Check if the data is corrupt. If so, don't try to read it.
  if (N % sizeof(uint64_t))
    return data_type();

  DataBuffer.clear();
  std::vector<uint64_t> CounterBuffer;

  const unsigned char *End = D + N;
  while (D < End) {
    // Read hash.
    if (D + sizeof(uint64_t) >= End)
      return data_type();
    uint64_t Hash = endian::readNext<uint64_t, little, unaligned>(D);

    // Initialize number of counters for GET_VERSION(FormatVersion) == 1.
    uint64_t CountsSize = N / sizeof(uint64_t) - 1;
    // If format version is different then read the number of counters.
    if (GET_VERSION(FormatVersion) != IndexedInstrProf::ProfVersion::Version1) {
      if (D + sizeof(uint64_t) > End)
        return data_type();
      CountsSize = endian::readNext<uint64_t, little, unaligned>(D);
    }
    // Read counter values.
    if (D + CountsSize * sizeof(uint64_t) > End)
      return data_type();

    CounterBuffer.clear();
    CounterBuffer.reserve(CountsSize);
    for (uint64_t J = 0; J < CountsSize; ++J)
      CounterBuffer.push_back(endian::readNext<uint64_t, little, unaligned>(D));

    DataBuffer.emplace_back(K, Hash, std::move(CounterBuffer));

    // Read value profiling data.
    if (GET_VERSION(FormatVersion) > IndexedInstrProf::ProfVersion::Version2 &&
        !readValueProfilingData(D, End)) {
      DataBuffer.clear();
      return data_type();
    }
  }
  return DataBuffer;
}

template <typename HashTableImpl>
Error InstrProfReaderIndex<HashTableImpl>::getRecords(
    StringRef FuncName, ArrayRef<NamedInstrProfRecord> &Data) {
  auto Iter = HashTable->find(FuncName);
  if (Iter == HashTable->end())
    return make_error<InstrProfError>(instrprof_error::unknown_function);

  Data = (*Iter);
  if (Data.empty())
    return make_error<InstrProfError>(instrprof_error::malformed);

  return Error::success();
}

template <typename HashTableImpl>
Error InstrProfReaderIndex<HashTableImpl>::getRecords(
    ArrayRef<NamedInstrProfRecord> &Data) {
  if (atEnd())
    return make_error<InstrProfError>(instrprof_error::eof);

  Data = *RecordIterator;

  if (Data.empty())
    return make_error<InstrProfError>(instrprof_error::malformed);

  return Error::success();
}

template <typename HashTableImpl>
InstrProfReaderIndex<HashTableImpl>::InstrProfReaderIndex(
    const unsigned char *Buckets, const unsigned char *const Payload,
    const unsigned char *const Base, IndexedInstrProf::HashT HashType,
    uint64_t Version) {
  FormatVersion = Version;
  HashTable.reset(HashTableImpl::Create(
      Buckets, Payload, Base,
      typename HashTableImpl::InfoType(HashType, Version)));
  RecordIterator = HashTable->data_begin();
}

namespace {
/// A remapper that does not apply any remappings.
class InstrProfReaderNullRemapper : public InstrProfReaderRemapper {
  InstrProfReaderIndexBase &Underlying;

public:
  InstrProfReaderNullRemapper(InstrProfReaderIndexBase &Underlying)
      : Underlying(Underlying) {}

  Error getRecords(StringRef FuncName,
                   ArrayRef<NamedInstrProfRecord> &Data) override {
    return Underlying.getRecords(FuncName, Data);
  }
};
}

/// A remapper that applies remappings based on a symbol remapping file.
template <typename HashTableImpl>
class llvm::InstrProfReaderItaniumRemapper
    : public InstrProfReaderRemapper {
public:
  InstrProfReaderItaniumRemapper(
      std::unique_ptr<MemoryBuffer> RemapBuffer,
      InstrProfReaderIndex<HashTableImpl> &Underlying)
      : RemapBuffer(std::move(RemapBuffer)), Underlying(Underlying) {
  }

  /// Extract the original function name from a PGO function name.
  static StringRef extractName(StringRef Name) {
    // We can have multiple :-separated pieces; there can be pieces both
    // before and after the mangled name. Find the first part that starts
    // with '_Z'; we'll assume that's the mangled name we want.
    std::pair<StringRef, StringRef> Parts = {StringRef(), Name};
    while (true) {
      Parts = Parts.second.split(':');
      if (Parts.first.startswith("_Z"))
        return Parts.first;
      if (Parts.second.empty())
        return Name;
    }
  }

  /// Given a mangled name extracted from a PGO function name, and a new
  /// form for that mangled name, reconstitute the name.
  static void reconstituteName(StringRef OrigName, StringRef ExtractedName,
                               StringRef Replacement,
                               SmallVectorImpl<char> &Out) {
    Out.reserve(OrigName.size() + Replacement.size() - ExtractedName.size());
    Out.insert(Out.end(), OrigName.begin(), ExtractedName.begin());
    Out.insert(Out.end(), Replacement.begin(), Replacement.end());
    Out.insert(Out.end(), ExtractedName.end(), OrigName.end());
  }

  Error populateRemappings() override {
    if (Error E = Remappings.read(*RemapBuffer))
      return E;
    for (StringRef Name : Underlying.HashTable->keys()) {
      StringRef RealName = extractName(Name);
      if (auto Key = Remappings.insert(RealName)) {
        // FIXME: We could theoretically map the same equivalence class to
        // multiple names in the profile data. If that happens, we should
        // return NamedInstrProfRecords from all of them.
        MappedNames.insert({Key, RealName});
      }
    }
    return Error::success();
  }

  Error getRecords(StringRef FuncName,
                   ArrayRef<NamedInstrProfRecord> &Data) override {
    StringRef RealName = extractName(FuncName);
    if (auto Key = Remappings.lookup(RealName)) {
      StringRef Remapped = MappedNames.lookup(Key);
      if (!Remapped.empty()) {
        if (RealName.begin() == FuncName.begin() &&
            RealName.end() == FuncName.end())
          FuncName = Remapped;
        else {
          // Try rebuilding the name from the given remapping.
          SmallString<256> Reconstituted;
          reconstituteName(FuncName, RealName, Remapped, Reconstituted);
          Error E = Underlying.getRecords(Reconstituted, Data);
          if (!E)
            return E;

          // If we failed because the name doesn't exist, fall back to asking
          // about the original name.
          if (Error Unhandled = handleErrors(
                  std::move(E), [](std::unique_ptr<InstrProfError> Err) {
                    return Err->get() == instrprof_error::unknown_function
                               ? Error::success()
                               : Error(std::move(Err));
                  }))
            return Unhandled;
        }
      }
    }
    return Underlying.getRecords(FuncName, Data);
  }

private:
  /// The memory buffer containing the remapping configuration. Remappings
  /// holds pointers into this buffer.
  std::unique_ptr<MemoryBuffer> RemapBuffer;

  /// The mangling remapper.
  SymbolRemappingReader Remappings;

  /// Mapping from mangled name keys to the name used for the key in the
  /// profile data.
  /// FIXME: Can we store a location within the on-disk hash table instead of
  /// redoing lookup?
  DenseMap<SymbolRemappingReader::Key, StringRef> MappedNames;

  /// The real profile data reader.
  InstrProfReaderIndex<HashTableImpl> &Underlying;
};

bool IndexedInstrProfReader::hasFormat(const MemoryBuffer &DataBuffer) {
  using namespace support;

  if (DataBuffer.getBufferSize() < 8)
    return false;
  uint64_t Magic =
      endian::read<uint64_t, little, aligned>(DataBuffer.getBufferStart());
  // Verify that it's magical.
  return Magic == IndexedInstrProf::Magic;
}

const unsigned char *
IndexedInstrProfReader::readSummary(IndexedInstrProf::ProfVersion Version,
                                    const unsigned char *Cur) {
  using namespace IndexedInstrProf;
  using namespace support;

  if (Version >= IndexedInstrProf::Version4) {
    const IndexedInstrProf::Summary *SummaryInLE =
        reinterpret_cast<const IndexedInstrProf::Summary *>(Cur);
    uint64_t NFields =
        endian::byte_swap<uint64_t, little>(SummaryInLE->NumSummaryFields);
    uint64_t NEntries =
        endian::byte_swap<uint64_t, little>(SummaryInLE->NumCutoffEntries);
    uint32_t SummarySize =
        IndexedInstrProf::Summary::getSize(NFields, NEntries);
    std::unique_ptr<IndexedInstrProf::Summary> SummaryData =
        IndexedInstrProf::allocSummary(SummarySize);

    const uint64_t *Src = reinterpret_cast<const uint64_t *>(SummaryInLE);
    uint64_t *Dst = reinterpret_cast<uint64_t *>(SummaryData.get());
    for (unsigned I = 0; I < SummarySize / sizeof(uint64_t); I++)
      Dst[I] = endian::byte_swap<uint64_t, little>(Src[I]);

    SummaryEntryVector DetailedSummary;
    for (unsigned I = 0; I < SummaryData->NumCutoffEntries; I++) {
      const IndexedInstrProf::Summary::Entry &Ent = SummaryData->getEntry(I);
      DetailedSummary.emplace_back((uint32_t)Ent.Cutoff, Ent.MinBlockCount,
                                   Ent.NumBlocks);
    }
    // initialize InstrProfSummary using the SummaryData from disk.
    this->Summary = llvm::make_unique<ProfileSummary>(
        ProfileSummary::PSK_Instr, DetailedSummary,
        SummaryData->get(Summary::TotalBlockCount),
        SummaryData->get(Summary::MaxBlockCount),
        SummaryData->get(Summary::MaxInternalBlockCount),
        SummaryData->get(Summary::MaxFunctionCount),
        SummaryData->get(Summary::TotalNumBlocks),
        SummaryData->get(Summary::TotalNumFunctions));
    return Cur + SummarySize;
  } else {
    // For older version of profile data, we need to compute on the fly:
    using namespace IndexedInstrProf;

    InstrProfSummaryBuilder Builder(ProfileSummaryBuilder::DefaultCutoffs);
    // FIXME: This only computes an empty summary. Need to call addRecord for
    // all NamedInstrProfRecords to get the correct summary.
    this->Summary = Builder.getSummary();
    return Cur;
  }
}

Error IndexedInstrProfReader::readHeader() {
  using namespace support;

  const unsigned char *Start =
      (const unsigned char *)DataBuffer->getBufferStart();
  const unsigned char *Cur = Start;
  if ((const unsigned char *)DataBuffer->getBufferEnd() - Cur < 24)
    return error(instrprof_error::truncated);

  auto *Header = reinterpret_cast<const IndexedInstrProf::Header *>(Cur);
  Cur += sizeof(IndexedInstrProf::Header);

  // Check the magic number.
  uint64_t Magic = endian::byte_swap<uint64_t, little>(Header->Magic);
  if (Magic != IndexedInstrProf::Magic)
    return error(instrprof_error::bad_magic);

  // Read the version.
  uint64_t FormatVersion = endian::byte_swap<uint64_t, little>(Header->Version);
  if (GET_VERSION(FormatVersion) >
      IndexedInstrProf::ProfVersion::CurrentVersion)
    return error(instrprof_error::unsupported_version);

  Cur = readSummary((IndexedInstrProf::ProfVersion)FormatVersion, Cur);

  // Read the hash type and start offset.
  IndexedInstrProf::HashT HashType = static_cast<IndexedInstrProf::HashT>(
      endian::byte_swap<uint64_t, little>(Header->HashType));
  if (HashType > IndexedInstrProf::HashT::Last)
    return error(instrprof_error::unsupported_hash_type);

  uint64_t HashOffset = endian::byte_swap<uint64_t, little>(Header->HashOffset);

  // The rest of the file is an on disk hash table.
  auto IndexPtr =
      llvm::make_unique<InstrProfReaderIndex<OnDiskHashTableImplV3>>(
          Start + HashOffset, Cur, Start, HashType, FormatVersion);

  // Load the remapping table now if requested.
  if (RemappingBuffer) {
    Remapper = llvm::make_unique<
        InstrProfReaderItaniumRemapper<OnDiskHashTableImplV3>>(
        std::move(RemappingBuffer), *IndexPtr);
    if (Error E = Remapper->populateRemappings())
      return E;
  } else {
    Remapper = llvm::make_unique<InstrProfReaderNullRemapper>(*IndexPtr);
  }
  Index = std::move(IndexPtr);

  return success();
}

InstrProfSymtab &IndexedInstrProfReader::getSymtab() {
  if (Symtab.get())
    return *Symtab.get();

  std::unique_ptr<InstrProfSymtab> NewSymtab = make_unique<InstrProfSymtab>();
  if (Error E = Index->populateSymtab(*NewSymtab.get())) {
    consumeError(error(InstrProfError::take(std::move(E))));
  }

  Symtab = std::move(NewSymtab);
  return *Symtab.get();
}

Expected<InstrProfRecord>
IndexedInstrProfReader::getInstrProfRecord(StringRef FuncName,
                                           uint64_t FuncHash) {
  ArrayRef<NamedInstrProfRecord> Data;
  Error Err = Remapper->getRecords(FuncName, Data);
  if (Err)
    return std::move(Err);
  // Found it. Look for counters with the right hash.
  for (unsigned I = 0, E = Data.size(); I < E; ++I) {
    // Check for a match and fill the vector if there is one.
    if (Data[I].Hash == FuncHash) {
      return std::move(Data[I]);
    }
  }
  return error(instrprof_error::hash_mismatch);
}

Error IndexedInstrProfReader::getFunctionCounts(StringRef FuncName,
                                                uint64_t FuncHash,
                                                std::vector<uint64_t> &Counts) {
  Expected<InstrProfRecord> Record = getInstrProfRecord(FuncName, FuncHash);
  if (Error E = Record.takeError())
    return error(std::move(E));

  Counts = Record.get().Counts;
  return success();
}

Error IndexedInstrProfReader::readNextRecord(NamedInstrProfRecord &Record) {
  ArrayRef<NamedInstrProfRecord> Data;

  Error E = Index->getRecords(Data);
  if (E)
    return error(std::move(E));

  Record = Data[RecordIndex++];
  if (RecordIndex >= Data.size()) {
    Index->advanceToNextKey();
    RecordIndex = 0;
  }
  return success();
}
