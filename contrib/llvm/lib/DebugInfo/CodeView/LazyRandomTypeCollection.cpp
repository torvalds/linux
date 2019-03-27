//===- LazyRandomTypeCollection.cpp ---------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/LazyRandomTypeCollection.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/CodeViewError.h"
#include "llvm/DebugInfo/CodeView/RecordName.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>

using namespace llvm;
using namespace llvm::codeview;

static void error(Error &&EC) {
  assert(!static_cast<bool>(EC));
  if (EC)
    consumeError(std::move(EC));
}

LazyRandomTypeCollection::LazyRandomTypeCollection(uint32_t RecordCountHint)
    : LazyRandomTypeCollection(CVTypeArray(), RecordCountHint,
                               PartialOffsetArray()) {}

LazyRandomTypeCollection::LazyRandomTypeCollection(
    const CVTypeArray &Types, uint32_t RecordCountHint,
    PartialOffsetArray PartialOffsets)
    : NameStorage(Allocator), Types(Types), PartialOffsets(PartialOffsets) {
  Records.resize(RecordCountHint);
}

LazyRandomTypeCollection::LazyRandomTypeCollection(ArrayRef<uint8_t> Data,
                                                   uint32_t RecordCountHint)
    : LazyRandomTypeCollection(RecordCountHint) {
}

LazyRandomTypeCollection::LazyRandomTypeCollection(StringRef Data,
                                                   uint32_t RecordCountHint)
    : LazyRandomTypeCollection(
          makeArrayRef(Data.bytes_begin(), Data.bytes_end()), RecordCountHint) {
}

LazyRandomTypeCollection::LazyRandomTypeCollection(const CVTypeArray &Types,
                                                   uint32_t NumRecords)
    : LazyRandomTypeCollection(Types, NumRecords, PartialOffsetArray()) {}

void LazyRandomTypeCollection::reset(BinaryStreamReader &Reader,
                                     uint32_t RecordCountHint) {
  Count = 0;
  PartialOffsets = PartialOffsetArray();

  error(Reader.readArray(Types, Reader.bytesRemaining()));

  // Clear and then resize, to make sure existing data gets destroyed.
  Records.clear();
  Records.resize(RecordCountHint);
}

void LazyRandomTypeCollection::reset(StringRef Data, uint32_t RecordCountHint) {
  BinaryStreamReader Reader(Data, support::little);
  reset(Reader, RecordCountHint);
}

void LazyRandomTypeCollection::reset(ArrayRef<uint8_t> Data,
                                     uint32_t RecordCountHint) {
  BinaryStreamReader Reader(Data, support::little);
  reset(Reader, RecordCountHint);
}

uint32_t LazyRandomTypeCollection::getOffsetOfType(TypeIndex Index) {
  error(ensureTypeExists(Index));
  assert(contains(Index));

  return Records[Index.toArrayIndex()].Offset;
}

CVType LazyRandomTypeCollection::getType(TypeIndex Index) {
  assert(!Index.isSimple());

  auto EC = ensureTypeExists(Index);
  error(std::move(EC));
  assert(contains(Index));

  return Records[Index.toArrayIndex()].Type;
}

Optional<CVType> LazyRandomTypeCollection::tryGetType(TypeIndex Index) {
  if (Index.isSimple())
    return None;

  if (auto EC = ensureTypeExists(Index)) {
    consumeError(std::move(EC));
    return None;
  }

  assert(contains(Index));
  return Records[Index.toArrayIndex()].Type;
}

StringRef LazyRandomTypeCollection::getTypeName(TypeIndex Index) {
  if (Index.isNoneType() || Index.isSimple())
    return TypeIndex::simpleTypeName(Index);

  // Try to make sure the type exists.  Even if it doesn't though, it may be
  // because we're dumping a symbol stream with no corresponding type stream
  // present, in which case we still want to be able to print <unknown UDT>
  // for the type names.
  if (auto EC = ensureTypeExists(Index)) {
    consumeError(std::move(EC));
    return "<unknown UDT>";
  }

  uint32_t I = Index.toArrayIndex();
  ensureCapacityFor(Index);
  if (Records[I].Name.data() == nullptr) {
    StringRef Result = NameStorage.save(computeTypeName(*this, Index));
    Records[I].Name = Result;
  }
  return Records[I].Name;
}

bool LazyRandomTypeCollection::contains(TypeIndex Index) {
  if (Index.isSimple() || Index.isNoneType())
    return false;

  if (Records.size() <= Index.toArrayIndex())
    return false;
  if (!Records[Index.toArrayIndex()].Type.valid())
    return false;
  return true;
}

uint32_t LazyRandomTypeCollection::size() { return Count; }

uint32_t LazyRandomTypeCollection::capacity() { return Records.size(); }

Error LazyRandomTypeCollection::ensureTypeExists(TypeIndex TI) {
  if (contains(TI))
    return Error::success();

  return visitRangeForType(TI);
}

void LazyRandomTypeCollection::ensureCapacityFor(TypeIndex Index) {
  assert(!Index.isSimple());
  uint32_t MinSize = Index.toArrayIndex() + 1;

  if (MinSize <= capacity())
    return;

  uint32_t NewCapacity = MinSize * 3 / 2;

  assert(NewCapacity > capacity());
  Records.resize(NewCapacity);
}

Error LazyRandomTypeCollection::visitRangeForType(TypeIndex TI) {
  assert(!TI.isSimple());
  if (PartialOffsets.empty())
    return fullScanForType(TI);

  auto Next = std::upper_bound(PartialOffsets.begin(), PartialOffsets.end(), TI,
                               [](TypeIndex Value, const TypeIndexOffset &IO) {
                                 return Value < IO.Type;
                               });

  assert(Next != PartialOffsets.begin());
  auto Prev = std::prev(Next);

  TypeIndex TIB = Prev->Type;
  if (contains(TIB)) {
    // They've asked us to fetch a type index, but the entry we found in the
    // partial offsets array has already been visited.  Since we visit an entire
    // block every time, that means this record should have been previously
    // discovered.  Ultimately, this means this is a request for a non-existant
    // type index.
    return make_error<CodeViewError>("Invalid type index");
  }

  TypeIndex TIE;
  if (Next == PartialOffsets.end()) {
    TIE = TypeIndex::fromArrayIndex(capacity());
  } else {
    TIE = Next->Type;
  }

  visitRange(TIB, Prev->Offset, TIE);
  return Error::success();
}

Optional<TypeIndex> LazyRandomTypeCollection::getFirst() {
  TypeIndex TI = TypeIndex::fromArrayIndex(0);
  if (auto EC = ensureTypeExists(TI)) {
    consumeError(std::move(EC));
    return None;
  }
  return TI;
}

Optional<TypeIndex> LazyRandomTypeCollection::getNext(TypeIndex Prev) {
  // We can't be sure how long this type stream is, given that the initial count
  // given to the constructor is just a hint.  So just try to make sure the next
  // record exists, and if anything goes wrong, we must be at the end.
  if (auto EC = ensureTypeExists(Prev + 1)) {
    consumeError(std::move(EC));
    return None;
  }

  return Prev + 1;
}

Error LazyRandomTypeCollection::fullScanForType(TypeIndex TI) {
  assert(!TI.isSimple());
  assert(PartialOffsets.empty());

  TypeIndex CurrentTI = TypeIndex::fromArrayIndex(0);
  auto Begin = Types.begin();

  if (Count > 0) {
    // In the case of type streams which we don't know the number of records of,
    // it's possible to search for a type index triggering a full scan, but then
    // later additional records are added since we didn't know how many there
    // would be until we did a full visitation, then you try to access the new
    // type triggering another full scan.  To avoid this, we assume that if the
    // database has some records, this must be what's going on.  We can also
    // assume that this index must be larger than the largest type index we've
    // visited, so we start from there and scan forward.
    uint32_t Offset = Records[LargestTypeIndex.toArrayIndex()].Offset;
    CurrentTI = LargestTypeIndex + 1;
    Begin = Types.at(Offset);
    ++Begin;
  }

  auto End = Types.end();
  while (Begin != End) {
    ensureCapacityFor(CurrentTI);
    LargestTypeIndex = std::max(LargestTypeIndex, CurrentTI);
    auto Idx = CurrentTI.toArrayIndex();
    Records[Idx].Type = *Begin;
    Records[Idx].Offset = Begin.offset();
    ++Count;
    ++Begin;
    ++CurrentTI;
  }
  if (CurrentTI <= TI) {
    return make_error<CodeViewError>("Type Index does not exist!");
  }
  return Error::success();
}

void LazyRandomTypeCollection::visitRange(TypeIndex Begin, uint32_t BeginOffset,
                                          TypeIndex End) {
  auto RI = Types.at(BeginOffset);
  assert(RI != Types.end());

  ensureCapacityFor(End);
  while (Begin != End) {
    LargestTypeIndex = std::max(LargestTypeIndex, Begin);
    auto Idx = Begin.toArrayIndex();
    Records[Idx].Type = *RI;
    Records[Idx].Offset = RI.offset();
    ++Count;
    ++Begin;
    ++RI;
  }
}
