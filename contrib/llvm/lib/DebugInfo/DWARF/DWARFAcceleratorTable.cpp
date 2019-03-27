//===- DWARFAcceleratorTable.cpp ------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/DWARF/DWARFAcceleratorTable.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFRelocMap.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DJB.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/raw_ostream.h"
#include <cstddef>
#include <cstdint>
#include <utility>

using namespace llvm;

namespace {
struct Atom {
  unsigned Value;
};

static raw_ostream &operator<<(raw_ostream &OS, const Atom &A) {
  StringRef Str = dwarf::AtomTypeString(A.Value);
  if (!Str.empty())
    return OS << Str;
  return OS << "DW_ATOM_unknown_" << format("%x", A.Value);
}
} // namespace

static Atom formatAtom(unsigned Atom) { return {Atom}; }

DWARFAcceleratorTable::~DWARFAcceleratorTable() = default;

llvm::Error AppleAcceleratorTable::extract() {
  uint32_t Offset = 0;

  // Check that we can at least read the header.
  if (!AccelSection.isValidOffset(offsetof(Header, HeaderDataLength) + 4))
    return createStringError(errc::illegal_byte_sequence,
                             "Section too small: cannot read header.");

  Hdr.Magic = AccelSection.getU32(&Offset);
  Hdr.Version = AccelSection.getU16(&Offset);
  Hdr.HashFunction = AccelSection.getU16(&Offset);
  Hdr.BucketCount = AccelSection.getU32(&Offset);
  Hdr.HashCount = AccelSection.getU32(&Offset);
  Hdr.HeaderDataLength = AccelSection.getU32(&Offset);

  // Check that we can read all the hashes and offsets from the
  // section (see SourceLevelDebugging.rst for the structure of the index).
  // We need to substract one because we're checking for an *offset* which is
  // equal to the size for an empty table and hence pointer after the section.
  if (!AccelSection.isValidOffset(sizeof(Hdr) + Hdr.HeaderDataLength +
                                  Hdr.BucketCount * 4 + Hdr.HashCount * 8 - 1))
    return createStringError(
        errc::illegal_byte_sequence,
        "Section too small: cannot read buckets and hashes.");

  HdrData.DIEOffsetBase = AccelSection.getU32(&Offset);
  uint32_t NumAtoms = AccelSection.getU32(&Offset);

  for (unsigned i = 0; i < NumAtoms; ++i) {
    uint16_t AtomType = AccelSection.getU16(&Offset);
    auto AtomForm = static_cast<dwarf::Form>(AccelSection.getU16(&Offset));
    HdrData.Atoms.push_back(std::make_pair(AtomType, AtomForm));
  }

  IsValid = true;
  return Error::success();
}

uint32_t AppleAcceleratorTable::getNumBuckets() { return Hdr.BucketCount; }
uint32_t AppleAcceleratorTable::getNumHashes() { return Hdr.HashCount; }
uint32_t AppleAcceleratorTable::getSizeHdr() { return sizeof(Hdr); }
uint32_t AppleAcceleratorTable::getHeaderDataLength() {
  return Hdr.HeaderDataLength;
}

ArrayRef<std::pair<AppleAcceleratorTable::HeaderData::AtomType,
                   AppleAcceleratorTable::HeaderData::Form>>
AppleAcceleratorTable::getAtomsDesc() {
  return HdrData.Atoms;
}

bool AppleAcceleratorTable::validateForms() {
  for (auto Atom : getAtomsDesc()) {
    DWARFFormValue FormValue(Atom.second);
    switch (Atom.first) {
    case dwarf::DW_ATOM_die_offset:
    case dwarf::DW_ATOM_die_tag:
    case dwarf::DW_ATOM_type_flags:
      if ((!FormValue.isFormClass(DWARFFormValue::FC_Constant) &&
           !FormValue.isFormClass(DWARFFormValue::FC_Flag)) ||
          FormValue.getForm() == dwarf::DW_FORM_sdata)
        return false;
      break;
    default:
      break;
    }
  }
  return true;
}

std::pair<uint32_t, dwarf::Tag>
AppleAcceleratorTable::readAtoms(uint32_t &HashDataOffset) {
  uint32_t DieOffset = dwarf::DW_INVALID_OFFSET;
  dwarf::Tag DieTag = dwarf::DW_TAG_null;
  dwarf::FormParams FormParams = {Hdr.Version, 0, dwarf::DwarfFormat::DWARF32};

  for (auto Atom : getAtomsDesc()) {
    DWARFFormValue FormValue(Atom.second);
    FormValue.extractValue(AccelSection, &HashDataOffset, FormParams);
    switch (Atom.first) {
    case dwarf::DW_ATOM_die_offset:
      DieOffset = *FormValue.getAsUnsignedConstant();
      break;
    case dwarf::DW_ATOM_die_tag:
      DieTag = (dwarf::Tag)*FormValue.getAsUnsignedConstant();
      break;
    default:
      break;
    }
  }
  return {DieOffset, DieTag};
}

void AppleAcceleratorTable::Header::dump(ScopedPrinter &W) const {
  DictScope HeaderScope(W, "Header");
  W.printHex("Magic", Magic);
  W.printHex("Version", Version);
  W.printHex("Hash function", HashFunction);
  W.printNumber("Bucket count", BucketCount);
  W.printNumber("Hashes count", HashCount);
  W.printNumber("HeaderData length", HeaderDataLength);
}

Optional<uint64_t> AppleAcceleratorTable::HeaderData::extractOffset(
    Optional<DWARFFormValue> Value) const {
  if (!Value)
    return None;

  switch (Value->getForm()) {
  case dwarf::DW_FORM_ref1:
  case dwarf::DW_FORM_ref2:
  case dwarf::DW_FORM_ref4:
  case dwarf::DW_FORM_ref8:
  case dwarf::DW_FORM_ref_udata:
    return Value->getRawUValue() + DIEOffsetBase;
  default:
    return Value->getAsSectionOffset();
  }
}

bool AppleAcceleratorTable::dumpName(ScopedPrinter &W,
                                     SmallVectorImpl<DWARFFormValue> &AtomForms,
                                     uint32_t *DataOffset) const {
  dwarf::FormParams FormParams = {Hdr.Version, 0, dwarf::DwarfFormat::DWARF32};
  uint32_t NameOffset = *DataOffset;
  if (!AccelSection.isValidOffsetForDataOfSize(*DataOffset, 4)) {
    W.printString("Incorrectly terminated list.");
    return false;
  }
  unsigned StringOffset = AccelSection.getRelocatedValue(4, DataOffset);
  if (!StringOffset)
    return false; // End of list

  DictScope NameScope(W, ("Name@0x" + Twine::utohexstr(NameOffset)).str());
  W.startLine() << format("String: 0x%08x", StringOffset);
  W.getOStream() << " \"" << StringSection.getCStr(&StringOffset) << "\"\n";

  unsigned NumData = AccelSection.getU32(DataOffset);
  for (unsigned Data = 0; Data < NumData; ++Data) {
    ListScope DataScope(W, ("Data " + Twine(Data)).str());
    unsigned i = 0;
    for (auto &Atom : AtomForms) {
      W.startLine() << format("Atom[%d]: ", i);
      if (Atom.extractValue(AccelSection, DataOffset, FormParams)) {
        Atom.dump(W.getOStream());
        if (Optional<uint64_t> Val = Atom.getAsUnsignedConstant()) {
          StringRef Str = dwarf::AtomValueString(HdrData.Atoms[i].first, *Val);
          if (!Str.empty())
            W.getOStream() << " (" << Str << ")";
        }
      } else
        W.getOStream() << "Error extracting the value";
      W.getOStream() << "\n";
      i++;
    }
  }
  return true; // more entries follow
}

LLVM_DUMP_METHOD void AppleAcceleratorTable::dump(raw_ostream &OS) const {
  if (!IsValid)
    return;

  ScopedPrinter W(OS);

  Hdr.dump(W);

  W.printNumber("DIE offset base", HdrData.DIEOffsetBase);
  W.printNumber("Number of atoms", uint64_t(HdrData.Atoms.size()));
  SmallVector<DWARFFormValue, 3> AtomForms;
  {
    ListScope AtomsScope(W, "Atoms");
    unsigned i = 0;
    for (const auto &Atom : HdrData.Atoms) {
      DictScope AtomScope(W, ("Atom " + Twine(i++)).str());
      W.startLine() << "Type: " << formatAtom(Atom.first) << '\n';
      W.startLine() << "Form: " << formatv("{0}", Atom.second) << '\n';
      AtomForms.push_back(DWARFFormValue(Atom.second));
    }
  }

  // Now go through the actual tables and dump them.
  uint32_t Offset = sizeof(Hdr) + Hdr.HeaderDataLength;
  unsigned HashesBase = Offset + Hdr.BucketCount * 4;
  unsigned OffsetsBase = HashesBase + Hdr.HashCount * 4;

  for (unsigned Bucket = 0; Bucket < Hdr.BucketCount; ++Bucket) {
    unsigned Index = AccelSection.getU32(&Offset);

    ListScope BucketScope(W, ("Bucket " + Twine(Bucket)).str());
    if (Index == UINT32_MAX) {
      W.printString("EMPTY");
      continue;
    }

    for (unsigned HashIdx = Index; HashIdx < Hdr.HashCount; ++HashIdx) {
      unsigned HashOffset = HashesBase + HashIdx*4;
      unsigned OffsetsOffset = OffsetsBase + HashIdx*4;
      uint32_t Hash = AccelSection.getU32(&HashOffset);

      if (Hash % Hdr.BucketCount != Bucket)
        break;

      unsigned DataOffset = AccelSection.getU32(&OffsetsOffset);
      ListScope HashScope(W, ("Hash 0x" + Twine::utohexstr(Hash)).str());
      if (!AccelSection.isValidOffset(DataOffset)) {
        W.printString("Invalid section offset");
        continue;
      }
      while (dumpName(W, AtomForms, &DataOffset))
        /*empty*/;
    }
  }
}

AppleAcceleratorTable::Entry::Entry(
    const AppleAcceleratorTable::HeaderData &HdrData)
    : HdrData(&HdrData) {
  Values.reserve(HdrData.Atoms.size());
  for (const auto &Atom : HdrData.Atoms)
    Values.push_back(DWARFFormValue(Atom.second));
}

void AppleAcceleratorTable::Entry::extract(
    const AppleAcceleratorTable &AccelTable, uint32_t *Offset) {

  dwarf::FormParams FormParams = {AccelTable.Hdr.Version, 0,
                                  dwarf::DwarfFormat::DWARF32};
  for (auto &Atom : Values)
    Atom.extractValue(AccelTable.AccelSection, Offset, FormParams);
}

Optional<DWARFFormValue>
AppleAcceleratorTable::Entry::lookup(HeaderData::AtomType Atom) const {
  assert(HdrData && "Dereferencing end iterator?");
  assert(HdrData->Atoms.size() == Values.size());
  for (const auto &Tuple : zip_first(HdrData->Atoms, Values)) {
    if (std::get<0>(Tuple).first == Atom)
      return std::get<1>(Tuple);
  }
  return None;
}

Optional<uint64_t> AppleAcceleratorTable::Entry::getDIESectionOffset() const {
  return HdrData->extractOffset(lookup(dwarf::DW_ATOM_die_offset));
}

Optional<uint64_t> AppleAcceleratorTable::Entry::getCUOffset() const {
  return HdrData->extractOffset(lookup(dwarf::DW_ATOM_cu_offset));
}

Optional<dwarf::Tag> AppleAcceleratorTable::Entry::getTag() const {
  Optional<DWARFFormValue> Tag = lookup(dwarf::DW_ATOM_die_tag);
  if (!Tag)
    return None;
  if (Optional<uint64_t> Value = Tag->getAsUnsignedConstant())
    return dwarf::Tag(*Value);
  return None;
}

AppleAcceleratorTable::ValueIterator::ValueIterator(
    const AppleAcceleratorTable &AccelTable, unsigned Offset)
    : AccelTable(&AccelTable), Current(AccelTable.HdrData), DataOffset(Offset) {
  if (!AccelTable.AccelSection.isValidOffsetForDataOfSize(DataOffset, 4))
    return;

  // Read the first entry.
  NumData = AccelTable.AccelSection.getU32(&DataOffset);
  Next();
}

void AppleAcceleratorTable::ValueIterator::Next() {
  assert(NumData > 0 && "attempted to increment iterator past the end");
  auto &AccelSection = AccelTable->AccelSection;
  if (Data >= NumData ||
      !AccelSection.isValidOffsetForDataOfSize(DataOffset, 4)) {
    NumData = 0;
    DataOffset = 0;
    return;
  }
  Current.extract(*AccelTable, &DataOffset);
  ++Data;
}

iterator_range<AppleAcceleratorTable::ValueIterator>
AppleAcceleratorTable::equal_range(StringRef Key) const {
  if (!IsValid)
    return make_range(ValueIterator(), ValueIterator());

  // Find the bucket.
  unsigned HashValue = djbHash(Key);
  unsigned Bucket = HashValue % Hdr.BucketCount;
  unsigned BucketBase = sizeof(Hdr) + Hdr.HeaderDataLength;
  unsigned HashesBase = BucketBase + Hdr.BucketCount * 4;
  unsigned OffsetsBase = HashesBase + Hdr.HashCount * 4;

  unsigned BucketOffset = BucketBase + Bucket * 4;
  unsigned Index = AccelSection.getU32(&BucketOffset);

  // Search through all hashes in the bucket.
  for (unsigned HashIdx = Index; HashIdx < Hdr.HashCount; ++HashIdx) {
    unsigned HashOffset = HashesBase + HashIdx * 4;
    unsigned OffsetsOffset = OffsetsBase + HashIdx * 4;
    uint32_t Hash = AccelSection.getU32(&HashOffset);

    if (Hash % Hdr.BucketCount != Bucket)
      // We are already in the next bucket.
      break;

    unsigned DataOffset = AccelSection.getU32(&OffsetsOffset);
    unsigned StringOffset = AccelSection.getRelocatedValue(4, &DataOffset);
    if (!StringOffset)
      break;

    // Finally, compare the key.
    if (Key == StringSection.getCStr(&StringOffset))
      return make_range({*this, DataOffset}, ValueIterator());
  }
  return make_range(ValueIterator(), ValueIterator());
}

void DWARFDebugNames::Header::dump(ScopedPrinter &W) const {
  DictScope HeaderScope(W, "Header");
  W.printHex("Length", UnitLength);
  W.printNumber("Version", Version);
  W.printHex("Padding", Padding);
  W.printNumber("CU count", CompUnitCount);
  W.printNumber("Local TU count", LocalTypeUnitCount);
  W.printNumber("Foreign TU count", ForeignTypeUnitCount);
  W.printNumber("Bucket count", BucketCount);
  W.printNumber("Name count", NameCount);
  W.printHex("Abbreviations table size", AbbrevTableSize);
  W.startLine() << "Augmentation: '" << AugmentationString << "'\n";
}

llvm::Error DWARFDebugNames::Header::extract(const DWARFDataExtractor &AS,
                                             uint32_t *Offset) {
  // Check that we can read the fixed-size part.
  if (!AS.isValidOffset(*Offset + sizeof(HeaderPOD) - 1))
    return createStringError(errc::illegal_byte_sequence,
                             "Section too small: cannot read header.");

  UnitLength = AS.getU32(Offset);
  Version = AS.getU16(Offset);
  Padding = AS.getU16(Offset);
  CompUnitCount = AS.getU32(Offset);
  LocalTypeUnitCount = AS.getU32(Offset);
  ForeignTypeUnitCount = AS.getU32(Offset);
  BucketCount = AS.getU32(Offset);
  NameCount = AS.getU32(Offset);
  AbbrevTableSize = AS.getU32(Offset);
  AugmentationStringSize = alignTo(AS.getU32(Offset), 4);

  if (!AS.isValidOffsetForDataOfSize(*Offset, AugmentationStringSize))
    return createStringError(
        errc::illegal_byte_sequence,
        "Section too small: cannot read header augmentation.");
  AugmentationString.resize(AugmentationStringSize);
  AS.getU8(Offset, reinterpret_cast<uint8_t *>(AugmentationString.data()),
           AugmentationStringSize);
  return Error::success();
}

void DWARFDebugNames::Abbrev::dump(ScopedPrinter &W) const {
  DictScope AbbrevScope(W, ("Abbreviation 0x" + Twine::utohexstr(Code)).str());
  W.startLine() << formatv("Tag: {0}\n", Tag);

  for (const auto &Attr : Attributes)
    W.startLine() << formatv("{0}: {1}\n", Attr.Index, Attr.Form);
}

static constexpr DWARFDebugNames::AttributeEncoding sentinelAttrEnc() {
  return {dwarf::Index(0), dwarf::Form(0)};
}

static bool isSentinel(const DWARFDebugNames::AttributeEncoding &AE) {
  return AE == sentinelAttrEnc();
}

static DWARFDebugNames::Abbrev sentinelAbbrev() {
  return DWARFDebugNames::Abbrev(0, dwarf::Tag(0), {});
}

static bool isSentinel(const DWARFDebugNames::Abbrev &Abbr) {
  return Abbr.Code == 0;
}

DWARFDebugNames::Abbrev DWARFDebugNames::AbbrevMapInfo::getEmptyKey() {
  return sentinelAbbrev();
}

DWARFDebugNames::Abbrev DWARFDebugNames::AbbrevMapInfo::getTombstoneKey() {
  return DWARFDebugNames::Abbrev(~0, dwarf::Tag(0), {});
}

Expected<DWARFDebugNames::AttributeEncoding>
DWARFDebugNames::NameIndex::extractAttributeEncoding(uint32_t *Offset) {
  if (*Offset >= EntriesBase) {
    return createStringError(errc::illegal_byte_sequence,
                             "Incorrectly terminated abbreviation table.");
  }

  uint32_t Index = Section.AccelSection.getULEB128(Offset);
  uint32_t Form = Section.AccelSection.getULEB128(Offset);
  return AttributeEncoding(dwarf::Index(Index), dwarf::Form(Form));
}

Expected<std::vector<DWARFDebugNames::AttributeEncoding>>
DWARFDebugNames::NameIndex::extractAttributeEncodings(uint32_t *Offset) {
  std::vector<AttributeEncoding> Result;
  for (;;) {
    auto AttrEncOr = extractAttributeEncoding(Offset);
    if (!AttrEncOr)
      return AttrEncOr.takeError();
    if (isSentinel(*AttrEncOr))
      return std::move(Result);

    Result.emplace_back(*AttrEncOr);
  }
}

Expected<DWARFDebugNames::Abbrev>
DWARFDebugNames::NameIndex::extractAbbrev(uint32_t *Offset) {
  if (*Offset >= EntriesBase) {
    return createStringError(errc::illegal_byte_sequence,
                             "Incorrectly terminated abbreviation table.");
  }

  uint32_t Code = Section.AccelSection.getULEB128(Offset);
  if (Code == 0)
    return sentinelAbbrev();

  uint32_t Tag = Section.AccelSection.getULEB128(Offset);
  auto AttrEncOr = extractAttributeEncodings(Offset);
  if (!AttrEncOr)
    return AttrEncOr.takeError();
  return Abbrev(Code, dwarf::Tag(Tag), std::move(*AttrEncOr));
}

Error DWARFDebugNames::NameIndex::extract() {
  const DWARFDataExtractor &AS = Section.AccelSection;
  uint32_t Offset = Base;
  if (Error E = Hdr.extract(AS, &Offset))
    return E;

  CUsBase = Offset;
  Offset += Hdr.CompUnitCount * 4;
  Offset += Hdr.LocalTypeUnitCount * 4;
  Offset += Hdr.ForeignTypeUnitCount * 8;
  BucketsBase = Offset;
  Offset += Hdr.BucketCount * 4;
  HashesBase = Offset;
  if (Hdr.BucketCount > 0)
    Offset += Hdr.NameCount * 4;
  StringOffsetsBase = Offset;
  Offset += Hdr.NameCount * 4;
  EntryOffsetsBase = Offset;
  Offset += Hdr.NameCount * 4;

  if (!AS.isValidOffsetForDataOfSize(Offset, Hdr.AbbrevTableSize))
    return createStringError(errc::illegal_byte_sequence,
                             "Section too small: cannot read abbreviations.");

  EntriesBase = Offset + Hdr.AbbrevTableSize;

  for (;;) {
    auto AbbrevOr = extractAbbrev(&Offset);
    if (!AbbrevOr)
      return AbbrevOr.takeError();
    if (isSentinel(*AbbrevOr))
      return Error::success();

    if (!Abbrevs.insert(std::move(*AbbrevOr)).second)
      return createStringError(errc::invalid_argument,
                               "Duplicate abbreviation code.");
  }
}
DWARFDebugNames::Entry::Entry(const NameIndex &NameIdx, const Abbrev &Abbr)
    : NameIdx(&NameIdx), Abbr(&Abbr) {
  // This merely creates form values. It is up to the caller
  // (NameIndex::getEntry) to populate them.
  Values.reserve(Abbr.Attributes.size());
  for (const auto &Attr : Abbr.Attributes)
    Values.emplace_back(Attr.Form);
}

Optional<DWARFFormValue>
DWARFDebugNames::Entry::lookup(dwarf::Index Index) const {
  assert(Abbr->Attributes.size() == Values.size());
  for (const auto &Tuple : zip_first(Abbr->Attributes, Values)) {
    if (std::get<0>(Tuple).Index == Index)
      return std::get<1>(Tuple);
  }
  return None;
}

Optional<uint64_t> DWARFDebugNames::Entry::getDIEUnitOffset() const {
  if (Optional<DWARFFormValue> Off = lookup(dwarf::DW_IDX_die_offset))
    return Off->getAsReferenceUVal();
  return None;
}

Optional<uint64_t> DWARFDebugNames::Entry::getCUIndex() const {
  if (Optional<DWARFFormValue> Off = lookup(dwarf::DW_IDX_compile_unit))
    return Off->getAsUnsignedConstant();
  // In a per-CU index, the entries without a DW_IDX_compile_unit attribute
  // implicitly refer to the single CU.
  if (NameIdx->getCUCount() == 1)
    return 0;
  return None;
}

Optional<uint64_t> DWARFDebugNames::Entry::getCUOffset() const {
  Optional<uint64_t> Index = getCUIndex();
  if (!Index || *Index >= NameIdx->getCUCount())
    return None;
  return NameIdx->getCUOffset(*Index);
}

void DWARFDebugNames::Entry::dump(ScopedPrinter &W) const {
  W.printHex("Abbrev", Abbr->Code);
  W.startLine() << formatv("Tag: {0}\n", Abbr->Tag);
  assert(Abbr->Attributes.size() == Values.size());
  for (const auto &Tuple : zip_first(Abbr->Attributes, Values)) {
    W.startLine() << formatv("{0}: ", std::get<0>(Tuple).Index);
    std::get<1>(Tuple).dump(W.getOStream());
    W.getOStream() << '\n';
  }
}

char DWARFDebugNames::SentinelError::ID;
std::error_code DWARFDebugNames::SentinelError::convertToErrorCode() const {
  return inconvertibleErrorCode();
}

uint32_t DWARFDebugNames::NameIndex::getCUOffset(uint32_t CU) const {
  assert(CU < Hdr.CompUnitCount);
  uint32_t Offset = CUsBase + 4 * CU;
  return Section.AccelSection.getRelocatedValue(4, &Offset);
}

uint32_t DWARFDebugNames::NameIndex::getLocalTUOffset(uint32_t TU) const {
  assert(TU < Hdr.LocalTypeUnitCount);
  uint32_t Offset = CUsBase + Hdr.CompUnitCount * 4;
  return Section.AccelSection.getRelocatedValue(4, &Offset);
}

uint64_t DWARFDebugNames::NameIndex::getForeignTUSignature(uint32_t TU) const {
  assert(TU < Hdr.ForeignTypeUnitCount);
  uint32_t Offset = CUsBase + (Hdr.CompUnitCount + Hdr.LocalTypeUnitCount) * 4;
  return Section.AccelSection.getU64(&Offset);
}

Expected<DWARFDebugNames::Entry>
DWARFDebugNames::NameIndex::getEntry(uint32_t *Offset) const {
  const DWARFDataExtractor &AS = Section.AccelSection;
  if (!AS.isValidOffset(*Offset))
    return createStringError(errc::illegal_byte_sequence,
                             "Incorrectly terminated entry list.");

  uint32_t AbbrevCode = AS.getULEB128(Offset);
  if (AbbrevCode == 0)
    return make_error<SentinelError>();

  const auto AbbrevIt = Abbrevs.find_as(AbbrevCode);
  if (AbbrevIt == Abbrevs.end())
    return createStringError(errc::invalid_argument, "Invalid abbreviation.");

  Entry E(*this, *AbbrevIt);

  dwarf::FormParams FormParams = {Hdr.Version, 0, dwarf::DwarfFormat::DWARF32};
  for (auto &Value : E.Values) {
    if (!Value.extractValue(AS, Offset, FormParams))
      return createStringError(errc::io_error,
                               "Error extracting index attribute values.");
  }
  return std::move(E);
}

DWARFDebugNames::NameTableEntry
DWARFDebugNames::NameIndex::getNameTableEntry(uint32_t Index) const {
  assert(0 < Index && Index <= Hdr.NameCount);
  uint32_t StringOffsetOffset = StringOffsetsBase + 4 * (Index - 1);
  uint32_t EntryOffsetOffset = EntryOffsetsBase + 4 * (Index - 1);
  const DWARFDataExtractor &AS = Section.AccelSection;

  uint32_t StringOffset = AS.getRelocatedValue(4, &StringOffsetOffset);
  uint32_t EntryOffset = AS.getU32(&EntryOffsetOffset);
  EntryOffset += EntriesBase;
  return {Section.StringSection, Index, StringOffset, EntryOffset};
}

uint32_t
DWARFDebugNames::NameIndex::getBucketArrayEntry(uint32_t Bucket) const {
  assert(Bucket < Hdr.BucketCount);
  uint32_t BucketOffset = BucketsBase + 4 * Bucket;
  return Section.AccelSection.getU32(&BucketOffset);
}

uint32_t DWARFDebugNames::NameIndex::getHashArrayEntry(uint32_t Index) const {
  assert(0 < Index && Index <= Hdr.NameCount);
  uint32_t HashOffset = HashesBase + 4 * (Index - 1);
  return Section.AccelSection.getU32(&HashOffset);
}

// Returns true if we should continue scanning for entries, false if this is the
// last (sentinel) entry). In case of a parsing error we also return false, as
// it's not possible to recover this entry list (but the other lists may still
// parse OK).
bool DWARFDebugNames::NameIndex::dumpEntry(ScopedPrinter &W,
                                           uint32_t *Offset) const {
  uint32_t EntryId = *Offset;
  auto EntryOr = getEntry(Offset);
  if (!EntryOr) {
    handleAllErrors(EntryOr.takeError(), [](const SentinelError &) {},
                    [&W](const ErrorInfoBase &EI) { EI.log(W.startLine()); });
    return false;
  }

  DictScope EntryScope(W, ("Entry @ 0x" + Twine::utohexstr(EntryId)).str());
  EntryOr->dump(W);
  return true;
}

void DWARFDebugNames::NameIndex::dumpName(ScopedPrinter &W,
                                          const NameTableEntry &NTE,
                                          Optional<uint32_t> Hash) const {
  DictScope NameScope(W, ("Name " + Twine(NTE.getIndex())).str());
  if (Hash)
    W.printHex("Hash", *Hash);

  W.startLine() << format("String: 0x%08x", NTE.getStringOffset());
  W.getOStream() << " \"" << NTE.getString() << "\"\n";

  uint32_t EntryOffset = NTE.getEntryOffset();
  while (dumpEntry(W, &EntryOffset))
    /*empty*/;
}

void DWARFDebugNames::NameIndex::dumpCUs(ScopedPrinter &W) const {
  ListScope CUScope(W, "Compilation Unit offsets");
  for (uint32_t CU = 0; CU < Hdr.CompUnitCount; ++CU)
    W.startLine() << format("CU[%u]: 0x%08x\n", CU, getCUOffset(CU));
}

void DWARFDebugNames::NameIndex::dumpLocalTUs(ScopedPrinter &W) const {
  if (Hdr.LocalTypeUnitCount == 0)
    return;

  ListScope TUScope(W, "Local Type Unit offsets");
  for (uint32_t TU = 0; TU < Hdr.LocalTypeUnitCount; ++TU)
    W.startLine() << format("LocalTU[%u]: 0x%08x\n", TU, getLocalTUOffset(TU));
}

void DWARFDebugNames::NameIndex::dumpForeignTUs(ScopedPrinter &W) const {
  if (Hdr.ForeignTypeUnitCount == 0)
    return;

  ListScope TUScope(W, "Foreign Type Unit signatures");
  for (uint32_t TU = 0; TU < Hdr.ForeignTypeUnitCount; ++TU) {
    W.startLine() << format("ForeignTU[%u]: 0x%016" PRIx64 "\n", TU,
                            getForeignTUSignature(TU));
  }
}

void DWARFDebugNames::NameIndex::dumpAbbreviations(ScopedPrinter &W) const {
  ListScope AbbrevsScope(W, "Abbreviations");
  for (const auto &Abbr : Abbrevs)
    Abbr.dump(W);
}

void DWARFDebugNames::NameIndex::dumpBucket(ScopedPrinter &W,
                                            uint32_t Bucket) const {
  ListScope BucketScope(W, ("Bucket " + Twine(Bucket)).str());
  uint32_t Index = getBucketArrayEntry(Bucket);
  if (Index == 0) {
    W.printString("EMPTY");
    return;
  }
  if (Index > Hdr.NameCount) {
    W.printString("Name index is invalid");
    return;
  }

  for (; Index <= Hdr.NameCount; ++Index) {
    uint32_t Hash = getHashArrayEntry(Index);
    if (Hash % Hdr.BucketCount != Bucket)
      break;

    dumpName(W, getNameTableEntry(Index), Hash);
  }
}

LLVM_DUMP_METHOD void DWARFDebugNames::NameIndex::dump(ScopedPrinter &W) const {
  DictScope UnitScope(W, ("Name Index @ 0x" + Twine::utohexstr(Base)).str());
  Hdr.dump(W);
  dumpCUs(W);
  dumpLocalTUs(W);
  dumpForeignTUs(W);
  dumpAbbreviations(W);

  if (Hdr.BucketCount > 0) {
    for (uint32_t Bucket = 0; Bucket < Hdr.BucketCount; ++Bucket)
      dumpBucket(W, Bucket);
    return;
  }

  W.startLine() << "Hash table not present\n";
  for (NameTableEntry NTE : *this)
    dumpName(W, NTE, None);
}

llvm::Error DWARFDebugNames::extract() {
  uint32_t Offset = 0;
  while (AccelSection.isValidOffset(Offset)) {
    NameIndex Next(*this, Offset);
    if (llvm::Error E = Next.extract())
      return E;
    Offset = Next.getNextUnitOffset();
    NameIndices.push_back(std::move(Next));
  }
  return Error::success();
}

iterator_range<DWARFDebugNames::ValueIterator>
DWARFDebugNames::NameIndex::equal_range(StringRef Key) const {
  return make_range(ValueIterator(*this, Key), ValueIterator());
}

LLVM_DUMP_METHOD void DWARFDebugNames::dump(raw_ostream &OS) const {
  ScopedPrinter W(OS);
  for (const NameIndex &NI : NameIndices)
    NI.dump(W);
}

Optional<uint32_t>
DWARFDebugNames::ValueIterator::findEntryOffsetInCurrentIndex() {
  const Header &Hdr = CurrentIndex->Hdr;
  if (Hdr.BucketCount == 0) {
    // No Hash Table, We need to search through all names in the Name Index.
    for (NameTableEntry NTE : *CurrentIndex) {
      if (NTE.getString() == Key)
        return NTE.getEntryOffset();
    }
    return None;
  }

  // The Name Index has a Hash Table, so use that to speed up the search.
  // Compute the Key Hash, if it has not been done already.
  if (!Hash)
    Hash = caseFoldingDjbHash(Key);
  uint32_t Bucket = *Hash % Hdr.BucketCount;
  uint32_t Index = CurrentIndex->getBucketArrayEntry(Bucket);
  if (Index == 0)
    return None; // Empty bucket

  for (; Index <= Hdr.NameCount; ++Index) {
    uint32_t Hash = CurrentIndex->getHashArrayEntry(Index);
    if (Hash % Hdr.BucketCount != Bucket)
      return None; // End of bucket

    NameTableEntry NTE = CurrentIndex->getNameTableEntry(Index);
    if (NTE.getString() == Key)
      return NTE.getEntryOffset();
  }
  return None;
}

bool DWARFDebugNames::ValueIterator::getEntryAtCurrentOffset() {
  auto EntryOr = CurrentIndex->getEntry(&DataOffset);
  if (!EntryOr) {
    consumeError(EntryOr.takeError());
    return false;
  }
  CurrentEntry = std::move(*EntryOr);
  return true;
}

bool DWARFDebugNames::ValueIterator::findInCurrentIndex() {
  Optional<uint32_t> Offset = findEntryOffsetInCurrentIndex();
  if (!Offset)
    return false;
  DataOffset = *Offset;
  return getEntryAtCurrentOffset();
}

void DWARFDebugNames::ValueIterator::searchFromStartOfCurrentIndex() {
  for (const NameIndex *End = CurrentIndex->Section.NameIndices.end();
       CurrentIndex != End; ++CurrentIndex) {
    if (findInCurrentIndex())
      return;
  }
  setEnd();
}

void DWARFDebugNames::ValueIterator::next() {
  assert(CurrentIndex && "Incrementing an end() iterator?");

  // First try the next entry in the current Index.
  if (getEntryAtCurrentOffset())
    return;

  // If we're a local iterator or we have reached the last Index, we're done.
  if (IsLocal || CurrentIndex == &CurrentIndex->Section.NameIndices.back()) {
    setEnd();
    return;
  }

  // Otherwise, try the next index.
  ++CurrentIndex;
  searchFromStartOfCurrentIndex();
}

DWARFDebugNames::ValueIterator::ValueIterator(const DWARFDebugNames &AccelTable,
                                              StringRef Key)
    : CurrentIndex(AccelTable.NameIndices.begin()), IsLocal(false), Key(Key) {
  searchFromStartOfCurrentIndex();
}

DWARFDebugNames::ValueIterator::ValueIterator(
    const DWARFDebugNames::NameIndex &NI, StringRef Key)
    : CurrentIndex(&NI), IsLocal(true), Key(Key) {
  if (!findInCurrentIndex())
    setEnd();
}

iterator_range<DWARFDebugNames::ValueIterator>
DWARFDebugNames::equal_range(StringRef Key) const {
  if (NameIndices.empty())
    return make_range(ValueIterator(), ValueIterator());
  return make_range(ValueIterator(*this, Key), ValueIterator());
}

const DWARFDebugNames::NameIndex *
DWARFDebugNames::getCUNameIndex(uint32_t CUOffset) {
  if (CUToNameIndex.size() == 0 && NameIndices.size() > 0) {
    for (const auto &NI : *this) {
      for (uint32_t CU = 0; CU < NI.getCUCount(); ++CU)
        CUToNameIndex.try_emplace(NI.getCUOffset(CU), &NI);
    }
  }
  return CUToNameIndex.lookup(CUOffset);
}
