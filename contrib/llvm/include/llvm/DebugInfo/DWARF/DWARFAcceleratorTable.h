//===- DWARFAcceleratorTable.h ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARFACCELERATORTABLE_H
#define LLVM_DEBUGINFO_DWARFACCELERATORTABLE_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFDataExtractor.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include <cstdint>
#include <utility>

namespace llvm {

class raw_ostream;
class ScopedPrinter;

/// The accelerator tables are designed to allow efficient random access
/// (using a symbol name as a key) into debug info by providing an index of the
/// debug info DIEs. This class implements the common functionality of Apple and
/// DWARF 5 accelerator tables.
/// TODO: Generalize the rest of the AppleAcceleratorTable interface and move it
/// to this class.
class DWARFAcceleratorTable {
protected:
  DWARFDataExtractor AccelSection;
  DataExtractor StringSection;

public:
  /// An abstract class representing a single entry in the accelerator tables.
  class Entry {
  protected:
    SmallVector<DWARFFormValue, 3> Values;

    Entry() = default;

    // Make these protected so only (final) subclasses can be copied around.
    Entry(const Entry &) = default;
    Entry(Entry &&) = default;
    Entry &operator=(const Entry &) = default;
    Entry &operator=(Entry &&) = default;
    ~Entry() = default;


  public:
    /// Returns the Offset of the Compilation Unit associated with this
    /// Accelerator Entry or None if the Compilation Unit offset is not recorded
    /// in this Accelerator Entry.
    virtual Optional<uint64_t> getCUOffset() const = 0;

    /// Returns the Tag of the Debug Info Entry associated with this
    /// Accelerator Entry or None if the Tag is not recorded in this
    /// Accelerator Entry.
    virtual Optional<dwarf::Tag> getTag() const = 0;

    /// Returns the raw values of fields in the Accelerator Entry. In general,
    /// these can only be interpreted with the help of the metadata in the
    /// owning Accelerator Table.
    ArrayRef<DWARFFormValue> getValues() const { return Values; }
  };

  DWARFAcceleratorTable(const DWARFDataExtractor &AccelSection,
                        DataExtractor StringSection)
      : AccelSection(AccelSection), StringSection(StringSection) {}
  virtual ~DWARFAcceleratorTable();

  virtual llvm::Error extract() = 0;
  virtual void dump(raw_ostream &OS) const = 0;

  DWARFAcceleratorTable(const DWARFAcceleratorTable &) = delete;
  void operator=(const DWARFAcceleratorTable &) = delete;
};

/// This implements the Apple accelerator table format, a precursor of the
/// DWARF 5 accelerator table format.
class AppleAcceleratorTable : public DWARFAcceleratorTable {
  struct Header {
    uint32_t Magic;
    uint16_t Version;
    uint16_t HashFunction;
    uint32_t BucketCount;
    uint32_t HashCount;
    uint32_t HeaderDataLength;

    void dump(ScopedPrinter &W) const;
  };

  struct HeaderData {
    using AtomType = uint16_t;
    using Form = dwarf::Form;

    uint32_t DIEOffsetBase;
    SmallVector<std::pair<AtomType, Form>, 3> Atoms;

    Optional<uint64_t> extractOffset(Optional<DWARFFormValue> Value) const;
  };

  struct Header Hdr;
  struct HeaderData HdrData;
  bool IsValid = false;

  /// Returns true if we should continue scanning for entries or false if we've
  /// reached the last (sentinel) entry of encountered a parsing error.
  bool dumpName(ScopedPrinter &W, SmallVectorImpl<DWARFFormValue> &AtomForms,
                uint32_t *DataOffset) const;

public:
  /// Apple-specific implementation of an Accelerator Entry.
  class Entry final : public DWARFAcceleratorTable::Entry {
    const HeaderData *HdrData = nullptr;

    Entry(const HeaderData &Data);
    Entry() = default;

    void extract(const AppleAcceleratorTable &AccelTable, uint32_t *Offset);

  public:
    Optional<uint64_t> getCUOffset() const override;

    /// Returns the Section Offset of the Debug Info Entry associated with this
    /// Accelerator Entry or None if the DIE offset is not recorded in this
    /// Accelerator Entry. The returned offset is relative to the start of the
    /// Section containing the DIE.
    Optional<uint64_t> getDIESectionOffset() const;

    Optional<dwarf::Tag> getTag() const override;

    /// Returns the value of the Atom in this Accelerator Entry, if the Entry
    /// contains such Atom.
    Optional<DWARFFormValue> lookup(HeaderData::AtomType Atom) const;

    friend class AppleAcceleratorTable;
    friend class ValueIterator;
  };

  class ValueIterator : public std::iterator<std::input_iterator_tag, Entry> {
    const AppleAcceleratorTable *AccelTable = nullptr;
    Entry Current;           ///< The current entry.
    unsigned DataOffset = 0; ///< Offset into the section.
    unsigned Data = 0; ///< Current data entry.
    unsigned NumData = 0; ///< Number of data entries.

    /// Advance the iterator.
    void Next();
  public:
    /// Construct a new iterator for the entries at \p DataOffset.
    ValueIterator(const AppleAcceleratorTable &AccelTable, unsigned DataOffset);
    /// End marker.
    ValueIterator() = default;

    const Entry &operator*() const { return Current; }
    ValueIterator &operator++() { Next(); return *this; }
    ValueIterator operator++(int) {
      ValueIterator I = *this;
      Next();
      return I;
    }
    friend bool operator==(const ValueIterator &A, const ValueIterator &B) {
      return A.NumData == B.NumData && A.DataOffset == B.DataOffset;
    }
    friend bool operator!=(const ValueIterator &A, const ValueIterator &B) {
      return !(A == B);
    }
  };

  AppleAcceleratorTable(const DWARFDataExtractor &AccelSection,
                        DataExtractor StringSection)
      : DWARFAcceleratorTable(AccelSection, StringSection) {}

  llvm::Error extract() override;
  uint32_t getNumBuckets();
  uint32_t getNumHashes();
  uint32_t getSizeHdr();
  uint32_t getHeaderDataLength();

  /// Return the Atom description, which can be used to interpret the raw values
  /// of the Accelerator Entries in this table.
  ArrayRef<std::pair<HeaderData::AtomType, HeaderData::Form>> getAtomsDesc();
  bool validateForms();

  /// Return information related to the DWARF DIE we're looking for when
  /// performing a lookup by name.
  ///
  /// \param HashDataOffset an offset into the hash data table
  /// \returns <DieOffset, DieTag>
  /// DieOffset is the offset into the .debug_info section for the DIE
  /// related to the input hash data offset.
  /// DieTag is the tag of the DIE
  std::pair<uint32_t, dwarf::Tag> readAtoms(uint32_t &HashDataOffset);
  void dump(raw_ostream &OS) const override;

  /// Look up all entries in the accelerator table matching \c Key.
  iterator_range<ValueIterator> equal_range(StringRef Key) const;
};

/// .debug_names section consists of one or more units. Each unit starts with a
/// header, which is followed by a list of compilation units, local and foreign
/// type units.
///
/// These may be followed by an (optional) hash lookup table, which consists of
/// an array of buckets and hashes similar to the apple tables above. The only
/// difference is that the hashes array is 1-based, and consequently an empty
/// bucket is denoted by 0 and not UINT32_MAX.
///
/// Next is the name table, which consists of an array of names and array of
/// entry offsets. This is different from the apple tables, which store names
/// next to the actual entries.
///
/// The structure of the entries is described by an abbreviations table, which
/// comes after the name table. Unlike the apple tables, which have a uniform
/// entry structure described in the header, each .debug_names entry may have
/// different index attributes (DW_IDX_???) attached to it.
///
/// The last segment consists of a list of entries, which is a 0-terminated list
/// referenced by the name table and interpreted with the help of the
/// abbreviation table.
class DWARFDebugNames : public DWARFAcceleratorTable {
  /// The fixed-size part of a Dwarf 5 Name Index header
  struct HeaderPOD {
    uint32_t UnitLength;
    uint16_t Version;
    uint16_t Padding;
    uint32_t CompUnitCount;
    uint32_t LocalTypeUnitCount;
    uint32_t ForeignTypeUnitCount;
    uint32_t BucketCount;
    uint32_t NameCount;
    uint32_t AbbrevTableSize;
    uint32_t AugmentationStringSize;
  };

public:
  class NameIndex;
  class NameIterator;
  class ValueIterator;

  /// Dwarf 5 Name Index header.
  struct Header : public HeaderPOD {
    SmallString<8> AugmentationString;

    Error extract(const DWARFDataExtractor &AS, uint32_t *Offset);
    void dump(ScopedPrinter &W) const;
  };

  /// Index attribute and its encoding.
  struct AttributeEncoding {
    dwarf::Index Index;
    dwarf::Form Form;

    constexpr AttributeEncoding(dwarf::Index Index, dwarf::Form Form)
        : Index(Index), Form(Form) {}

    friend bool operator==(const AttributeEncoding &LHS,
                           const AttributeEncoding &RHS) {
      return LHS.Index == RHS.Index && LHS.Form == RHS.Form;
    }
  };

  /// Abbreviation describing the encoding of Name Index entries.
  struct Abbrev {
    uint32_t Code;  ///< Abbreviation code
    dwarf::Tag Tag; ///< Dwarf Tag of the described entity.
    std::vector<AttributeEncoding> Attributes; ///< List of index attributes.

    Abbrev(uint32_t Code, dwarf::Tag Tag,
           std::vector<AttributeEncoding> Attributes)
        : Code(Code), Tag(Tag), Attributes(std::move(Attributes)) {}

    void dump(ScopedPrinter &W) const;
  };

  /// DWARF v5-specific implementation of an Accelerator Entry.
  class Entry final : public DWARFAcceleratorTable::Entry {
    const NameIndex *NameIdx;
    const Abbrev *Abbr;

    Entry(const NameIndex &NameIdx, const Abbrev &Abbr);

  public:
    Optional<uint64_t> getCUOffset() const override;
    Optional<dwarf::Tag> getTag() const override { return tag(); }

    /// Returns the Index into the Compilation Unit list of the owning Name
    /// Index or None if this Accelerator Entry does not have an associated
    /// Compilation Unit. It is up to the user to verify that the returned Index
    /// is valid in the owning NameIndex (or use getCUOffset(), which will
    /// handle that check itself). Note that entries in NameIndexes which index
    /// just a single Compilation Unit are implicitly associated with that unit,
    /// so this function will return 0 even without an explicit
    /// DW_IDX_compile_unit attribute.
    Optional<uint64_t> getCUIndex() const;

    /// .debug_names-specific getter, which always succeeds (DWARF v5 index
    /// entries always have a tag).
    dwarf::Tag tag() const { return Abbr->Tag; }

    /// Returns the Offset of the DIE within the containing CU or TU.
    Optional<uint64_t> getDIEUnitOffset() const;

    /// Return the Abbreviation that can be used to interpret the raw values of
    /// this Accelerator Entry.
    const Abbrev &getAbbrev() const { return *Abbr; }

    /// Returns the value of the Index Attribute in this Accelerator Entry, if
    /// the Entry contains such Attribute.
    Optional<DWARFFormValue> lookup(dwarf::Index Index) const;

    void dump(ScopedPrinter &W) const;

    friend class NameIndex;
    friend class ValueIterator;
  };

  /// Error returned by NameIndex::getEntry to report it has reached the end of
  /// the entry list.
  class SentinelError : public ErrorInfo<SentinelError> {
  public:
    static char ID;

    void log(raw_ostream &OS) const override { OS << "Sentinel"; }
    std::error_code convertToErrorCode() const override;
  };

private:
  /// DenseMapInfo for struct Abbrev.
  struct AbbrevMapInfo {
    static Abbrev getEmptyKey();
    static Abbrev getTombstoneKey();
    static unsigned getHashValue(uint32_t Code) {
      return DenseMapInfo<uint32_t>::getHashValue(Code);
    }
    static unsigned getHashValue(const Abbrev &Abbr) {
      return getHashValue(Abbr.Code);
    }
    static bool isEqual(uint32_t LHS, const Abbrev &RHS) {
      return LHS == RHS.Code;
    }
    static bool isEqual(const Abbrev &LHS, const Abbrev &RHS) {
      return LHS.Code == RHS.Code;
    }
  };

public:
  /// A single entry in the Name Table (Dwarf 5 sect. 6.1.1.4.6) of the Name
  /// Index.
  class NameTableEntry {
    DataExtractor StrData;

    uint32_t Index;
    uint32_t StringOffset;
    uint32_t EntryOffset;

  public:
    NameTableEntry(const DataExtractor &StrData, uint32_t Index,
                   uint32_t StringOffset, uint32_t EntryOffset)
        : StrData(StrData), Index(Index), StringOffset(StringOffset),
          EntryOffset(EntryOffset) {}

    /// Return the index of this name in the parent Name Index.
    uint32_t getIndex() const { return Index; }

    /// Returns the offset of the name of the described entities.
    uint32_t getStringOffset() const { return StringOffset; }

    /// Return the string referenced by this name table entry or nullptr if the
    /// string offset is not valid.
    const char *getString() const {
      uint32_t Off = StringOffset;
      return StrData.getCStr(&Off);
    }

    /// Returns the offset of the first Entry in the list.
    uint32_t getEntryOffset() const { return EntryOffset; }
  };

  /// Represents a single accelerator table within the Dwarf 5 .debug_names
  /// section.
  class NameIndex {
    DenseSet<Abbrev, AbbrevMapInfo> Abbrevs;
    struct Header Hdr;
    const DWARFDebugNames &Section;

    // Base of the whole unit and of various important tables, as offsets from
    // the start of the section.
    uint32_t Base;
    uint32_t CUsBase;
    uint32_t BucketsBase;
    uint32_t HashesBase;
    uint32_t StringOffsetsBase;
    uint32_t EntryOffsetsBase;
    uint32_t EntriesBase;

    void dumpCUs(ScopedPrinter &W) const;
    void dumpLocalTUs(ScopedPrinter &W) const;
    void dumpForeignTUs(ScopedPrinter &W) const;
    void dumpAbbreviations(ScopedPrinter &W) const;
    bool dumpEntry(ScopedPrinter &W, uint32_t *Offset) const;
    void dumpName(ScopedPrinter &W, const NameTableEntry &NTE,
                  Optional<uint32_t> Hash) const;
    void dumpBucket(ScopedPrinter &W, uint32_t Bucket) const;

    Expected<AttributeEncoding> extractAttributeEncoding(uint32_t *Offset);

    Expected<std::vector<AttributeEncoding>>
    extractAttributeEncodings(uint32_t *Offset);

    Expected<Abbrev> extractAbbrev(uint32_t *Offset);

  public:
    NameIndex(const DWARFDebugNames &Section, uint32_t Base)
        : Section(Section), Base(Base) {}

    /// Reads offset of compilation unit CU. CU is 0-based.
    uint32_t getCUOffset(uint32_t CU) const;
    uint32_t getCUCount() const { return Hdr.CompUnitCount; }

    /// Reads offset of local type unit TU, TU is 0-based.
    uint32_t getLocalTUOffset(uint32_t TU) const;
    uint32_t getLocalTUCount() const { return Hdr.LocalTypeUnitCount; }

    /// Reads signature of foreign type unit TU. TU is 0-based.
    uint64_t getForeignTUSignature(uint32_t TU) const;
    uint32_t getForeignTUCount() const { return Hdr.ForeignTypeUnitCount; }

    /// Reads an entry in the Bucket Array for the given Bucket. The returned
    /// value is a (1-based) index into the Names, StringOffsets and
    /// EntryOffsets arrays. The input Bucket index is 0-based.
    uint32_t getBucketArrayEntry(uint32_t Bucket) const;
    uint32_t getBucketCount() const { return Hdr.BucketCount; }

    /// Reads an entry in the Hash Array for the given Index. The input Index
    /// is 1-based.
    uint32_t getHashArrayEntry(uint32_t Index) const;

    /// Reads an entry in the Name Table for the given Index. The Name Table
    /// consists of two arrays -- String Offsets and Entry Offsets. The returned
    /// offsets are relative to the starts of respective sections. Input Index
    /// is 1-based.
    NameTableEntry getNameTableEntry(uint32_t Index) const;

    uint32_t getNameCount() const { return Hdr.NameCount; }

    const DenseSet<Abbrev, AbbrevMapInfo> &getAbbrevs() const {
      return Abbrevs;
    }

    Expected<Entry> getEntry(uint32_t *Offset) const;

    /// Look up all entries in this Name Index matching \c Key.
    iterator_range<ValueIterator> equal_range(StringRef Key) const;

    NameIterator begin() const { return NameIterator(this, 1); }
    NameIterator end() const { return NameIterator(this, getNameCount() + 1); }

    llvm::Error extract();
    uint32_t getUnitOffset() const { return Base; }
    uint32_t getNextUnitOffset() const { return Base + 4 + Hdr.UnitLength; }
    void dump(ScopedPrinter &W) const;

    friend class DWARFDebugNames;
  };

  class ValueIterator : public std::iterator<std::input_iterator_tag, Entry> {

    /// The Name Index we are currently iterating through. The implementation
    /// relies on the fact that this can also be used as an iterator into the
    /// "NameIndices" vector in the Accelerator section.
    const NameIndex *CurrentIndex = nullptr;

    /// Whether this is a local iterator (searches in CurrentIndex only) or not
    /// (searches all name indices).
    bool IsLocal;

    Optional<Entry> CurrentEntry;
    unsigned DataOffset = 0; ///< Offset into the section.
    std::string Key;         ///< The Key we are searching for.
    Optional<uint32_t> Hash; ///< Hash of Key, if it has been computed.

    bool getEntryAtCurrentOffset();
    Optional<uint32_t> findEntryOffsetInCurrentIndex();
    bool findInCurrentIndex();
    void searchFromStartOfCurrentIndex();
    void next();

    /// Set the iterator to the "end" state.
    void setEnd() { *this = ValueIterator(); }

  public:
    /// Create a "begin" iterator for looping over all entries in the
    /// accelerator table matching Key. The iterator will run through all Name
    /// Indexes in the section in sequence.
    ValueIterator(const DWARFDebugNames &AccelTable, StringRef Key);

    /// Create a "begin" iterator for looping over all entries in a specific
    /// Name Index. Other indices in the section will not be visited.
    ValueIterator(const NameIndex &NI, StringRef Key);

    /// End marker.
    ValueIterator() = default;

    const Entry &operator*() const { return *CurrentEntry; }
    ValueIterator &operator++() {
      next();
      return *this;
    }
    ValueIterator operator++(int) {
      ValueIterator I = *this;
      next();
      return I;
    }

    friend bool operator==(const ValueIterator &A, const ValueIterator &B) {
      return A.CurrentIndex == B.CurrentIndex && A.DataOffset == B.DataOffset;
    }
    friend bool operator!=(const ValueIterator &A, const ValueIterator &B) {
      return !(A == B);
    }
  };

  class NameIterator {

    /// The Name Index we are iterating through.
    const NameIndex *CurrentIndex;

    /// The current name in the Name Index.
    uint32_t CurrentName;

    void next() {
      assert(CurrentName <= CurrentIndex->getNameCount());
      ++CurrentName;
    }

  public:
    using iterator_category = std::input_iterator_tag;
    using value_type = NameTableEntry;
    using difference_type = uint32_t;
    using pointer = NameTableEntry *;
    using reference = NameTableEntry; // We return entries by value.

    /// Creates an iterator whose initial position is name CurrentName in
    /// CurrentIndex.
    NameIterator(const NameIndex *CurrentIndex, uint32_t CurrentName)
        : CurrentIndex(CurrentIndex), CurrentName(CurrentName) {}

    NameTableEntry operator*() const {
      return CurrentIndex->getNameTableEntry(CurrentName);
    }
    NameIterator &operator++() {
      next();
      return *this;
    }
    NameIterator operator++(int) {
      NameIterator I = *this;
      next();
      return I;
    }

    friend bool operator==(const NameIterator &A, const NameIterator &B) {
      return A.CurrentIndex == B.CurrentIndex && A.CurrentName == B.CurrentName;
    }
    friend bool operator!=(const NameIterator &A, const NameIterator &B) {
      return !(A == B);
    }
  };

private:
  SmallVector<NameIndex, 0> NameIndices;
  DenseMap<uint32_t, const NameIndex *> CUToNameIndex;

public:
  DWARFDebugNames(const DWARFDataExtractor &AccelSection,
                  DataExtractor StringSection)
      : DWARFAcceleratorTable(AccelSection, StringSection) {}

  llvm::Error extract() override;
  void dump(raw_ostream &OS) const override;

  /// Look up all entries in the accelerator table matching \c Key.
  iterator_range<ValueIterator> equal_range(StringRef Key) const;

  using const_iterator = SmallVector<NameIndex, 0>::const_iterator;
  const_iterator begin() const { return NameIndices.begin(); }
  const_iterator end() const { return NameIndices.end(); }

  /// Return the Name Index covering the compile unit at CUOffset, or nullptr if
  /// there is no Name Index covering that unit.
  const NameIndex *getCUNameIndex(uint32_t CUOffset);
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARFACCELERATORTABLE_H
