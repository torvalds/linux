//==- include/llvm/CodeGen/AccelTable.h - Accelerator Tables -----*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains support for writing accelerator tables.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_DWARFACCELTABLE_H
#define LLVM_CODEGEN_DWARFACCELTABLE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/CodeGen/DIE.h"
#include "llvm/CodeGen/DwarfStringPoolEntry.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/DJB.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <cstddef>
#include <cstdint>
#include <vector>

/// The DWARF and Apple accelerator tables are an indirect hash table optimized
/// for null lookup rather than access to known data. The Apple accelerator
/// tables are a precursor of the newer DWARF v5 accelerator tables. Both
/// formats share common design ideas.
///
/// The Apple accelerator table are output into an on-disk format that looks
/// like this:
///
/// .------------------.
/// |  HEADER          |
/// |------------------|
/// |  BUCKETS         |
/// |------------------|
/// |  HASHES          |
/// |------------------|
/// |  OFFSETS         |
/// |------------------|
/// |  DATA            |
/// `------------------'
///
/// The header contains a magic number, version, type of hash function,
/// the number of buckets, total number of hashes, and room for a special struct
/// of data and the length of that struct.
///
/// The buckets contain an index (e.g. 6) into the hashes array. The hashes
/// section contains all of the 32-bit hash values in contiguous memory, and the
/// offsets contain the offset into the data area for the particular hash.
///
/// For a lookup example, we could hash a function name and take it modulo the
/// number of buckets giving us our bucket. From there we take the bucket value
/// as an index into the hashes table and look at each successive hash as long
/// as the hash value is still the same modulo result (bucket value) as earlier.
/// If we have a match we look at that same entry in the offsets table and grab
/// the offset in the data for our final match.
///
/// The DWARF v5 accelerator table consists of zero or more name indices that
/// are output into an on-disk format that looks like this:
///
/// .------------------.
/// |  HEADER          |
/// |------------------|
/// |  CU LIST         |
/// |------------------|
/// |  LOCAL TU LIST   |
/// |------------------|
/// |  FOREIGN TU LIST |
/// |------------------|
/// |  HASH TABLE      |
/// |------------------|
/// |  NAME TABLE      |
/// |------------------|
/// |  ABBREV TABLE    |
/// |------------------|
/// |  ENTRY POOL      |
/// `------------------'
///
/// For the full documentation please refer to the DWARF 5 standard.
///
///
/// This file defines the class template AccelTable, which is represents an
/// abstract view of an Accelerator table, without any notion of an on-disk
/// layout. This class is parameterized by an entry type, which should derive
/// from AccelTableData. This is the type of individual entries in the table,
/// and it should store the data necessary to emit them. AppleAccelTableData is
/// the base class for Apple Accelerator Table entries, which have a uniform
/// structure based on a sequence of Atoms. There are different sub-classes
/// derived from AppleAccelTable, which differ in the set of Atoms and how they
/// obtain their values.
///
/// An Apple Accelerator Table can be serialized by calling emitAppleAccelTable
/// function.
///
/// TODO: Add DWARF v5 emission code.

namespace llvm {

class AsmPrinter;
class DwarfCompileUnit;
class DwarfDebug;

/// Interface which the different types of accelerator table data have to
/// conform. It serves as a base class for different values of the template
/// argument of the AccelTable class template.
class AccelTableData {
public:
  virtual ~AccelTableData() = default;

  bool operator<(const AccelTableData &Other) const {
    return order() < Other.order();
  }

    // Subclasses should implement:
    // static uint32_t hash(StringRef Name);

#ifndef NDEBUG
  virtual void print(raw_ostream &OS) const = 0;
#endif
protected:
  virtual uint64_t order() const = 0;
};

/// A base class holding non-template-dependant functionality of the AccelTable
/// class. Clients should not use this class directly but rather instantiate
/// AccelTable with a type derived from AccelTableData.
class AccelTableBase {
public:
  using HashFn = uint32_t(StringRef);

  /// Represents a group of entries with identical name (and hence, hash value).
  struct HashData {
    DwarfStringPoolEntryRef Name;
    uint32_t HashValue;
    std::vector<AccelTableData *> Values;
    MCSymbol *Sym;

    HashData(DwarfStringPoolEntryRef Name, HashFn *Hash)
        : Name(Name), HashValue(Hash(Name.getString())) {}

#ifndef NDEBUG
    void print(raw_ostream &OS) const;
    void dump() const { print(dbgs()); }
#endif
  };
  using HashList = std::vector<HashData *>;
  using BucketList = std::vector<HashList>;

protected:
  /// Allocator for HashData and Values.
  BumpPtrAllocator Allocator;

  using StringEntries = StringMap<HashData, BumpPtrAllocator &>;
  StringEntries Entries;

  HashFn *Hash;
  uint32_t BucketCount;
  uint32_t UniqueHashCount;

  HashList Hashes;
  BucketList Buckets;

  void computeBucketCount();

  AccelTableBase(HashFn *Hash) : Entries(Allocator), Hash(Hash) {}

public:
  void finalize(AsmPrinter *Asm, StringRef Prefix);
  ArrayRef<HashList> getBuckets() const { return Buckets; }
  uint32_t getBucketCount() const { return BucketCount; }
  uint32_t getUniqueHashCount() const { return UniqueHashCount; }
  uint32_t getUniqueNameCount() const { return Entries.size(); }

#ifndef NDEBUG
  void print(raw_ostream &OS) const;
  void dump() const { print(dbgs()); }
#endif

  AccelTableBase(const AccelTableBase &) = delete;
  void operator=(const AccelTableBase &) = delete;
};

/// This class holds an abstract representation of an Accelerator Table,
/// consisting of a sequence of buckets, each bucket containint a sequence of
/// HashData entries. The class is parameterized by the type of entries it
/// holds. The type template parameter also defines the hash function to use for
/// hashing names.
template <typename DataT> class AccelTable : public AccelTableBase {
public:
  AccelTable() : AccelTableBase(DataT::hash) {}

  template <typename... Types>
  void addName(DwarfStringPoolEntryRef Name, Types &&... Args);
};

template <typename AccelTableDataT>
template <typename... Types>
void AccelTable<AccelTableDataT>::addName(DwarfStringPoolEntryRef Name,
                                          Types &&... Args) {
  assert(Buckets.empty() && "Already finalized!");
  // If the string is in the list already then add this die to the list
  // otherwise add a new one.
  auto Iter = Entries.try_emplace(Name.getString(), Name, Hash).first;
  assert(Iter->second.Name == Name);
  Iter->second.Values.push_back(
      new (Allocator) AccelTableDataT(std::forward<Types>(Args)...));
}

/// A base class for different implementations of Data classes for Apple
/// Accelerator Tables. The columns in the table are defined by the static Atoms
/// variable defined on the subclasses.
class AppleAccelTableData : public AccelTableData {
public:
  /// An Atom defines the form of the data in an Apple accelerator table.
  /// Conceptually it is a column in the accelerator consisting of a type and a
  /// specification of the form of its data.
  struct Atom {
    /// Atom Type.
    const uint16_t Type;
    /// DWARF Form.
    const uint16_t Form;

    constexpr Atom(uint16_t Type, uint16_t Form) : Type(Type), Form(Form) {}

#ifndef NDEBUG
    void print(raw_ostream &OS) const;
    void dump() const { print(dbgs()); }
#endif
  };
  // Subclasses should define:
  // static constexpr Atom Atoms[];

  virtual void emit(AsmPrinter *Asm) const = 0;

  static uint32_t hash(StringRef Buffer) { return djbHash(Buffer); }
};

/// The Data class implementation for DWARF v5 accelerator table. Unlike the
/// Apple Data classes, this class is just a DIE wrapper, and does not know to
/// serialize itself. The complete serialization logic is in the
/// emitDWARF5AccelTable function.
class DWARF5AccelTableData : public AccelTableData {
public:
  static uint32_t hash(StringRef Name) { return caseFoldingDjbHash(Name); }

  DWARF5AccelTableData(const DIE &Die) : Die(Die) {}

#ifndef NDEBUG
  void print(raw_ostream &OS) const override;
#endif

  const DIE &getDie() const { return Die; }
  uint64_t getDieOffset() const { return Die.getOffset(); }
  unsigned getDieTag() const { return Die.getTag(); }

protected:
  const DIE &Die;

  uint64_t order() const override { return Die.getOffset(); }
};

class DWARF5AccelTableStaticData : public AccelTableData {
public:
  static uint32_t hash(StringRef Name) { return caseFoldingDjbHash(Name); }

  DWARF5AccelTableStaticData(uint64_t DieOffset, unsigned DieTag,
                             unsigned CUIndex)
      : DieOffset(DieOffset), DieTag(DieTag), CUIndex(CUIndex) {}

#ifndef NDEBUG
  void print(raw_ostream &OS) const override;
#endif

  uint64_t getDieOffset() const { return DieOffset; }
  unsigned getDieTag() const { return DieTag; }
  unsigned getCUIndex() const { return CUIndex; }

protected:
  uint64_t DieOffset;
  unsigned DieTag;
  unsigned CUIndex;

  uint64_t order() const override { return DieOffset; }
};

void emitAppleAccelTableImpl(AsmPrinter *Asm, AccelTableBase &Contents,
                             StringRef Prefix, const MCSymbol *SecBegin,
                             ArrayRef<AppleAccelTableData::Atom> Atoms);

/// Emit an Apple Accelerator Table consisting of entries in the specified
/// AccelTable. The DataT template parameter should be derived from
/// AppleAccelTableData.
template <typename DataT>
void emitAppleAccelTable(AsmPrinter *Asm, AccelTable<DataT> &Contents,
                         StringRef Prefix, const MCSymbol *SecBegin) {
  static_assert(std::is_convertible<DataT *, AppleAccelTableData *>::value, "");
  emitAppleAccelTableImpl(Asm, Contents, Prefix, SecBegin, DataT::Atoms);
}

void emitDWARF5AccelTable(AsmPrinter *Asm,
                          AccelTable<DWARF5AccelTableData> &Contents,
                          const DwarfDebug &DD,
                          ArrayRef<std::unique_ptr<DwarfCompileUnit>> CUs);

void emitDWARF5AccelTable(
    AsmPrinter *Asm, AccelTable<DWARF5AccelTableStaticData> &Contents,
    ArrayRef<MCSymbol *> CUs,
    llvm::function_ref<unsigned(const DWARF5AccelTableStaticData &)>
        getCUIndexForEntry);

/// Accelerator table data implementation for simple Apple accelerator tables
/// with just a DIE reference.
class AppleAccelTableOffsetData : public AppleAccelTableData {
public:
  AppleAccelTableOffsetData(const DIE &D) : Die(D) {}

  void emit(AsmPrinter *Asm) const override;

#ifndef _MSC_VER
  // The line below is rejected by older versions (TBD) of MSVC.
  static constexpr Atom Atoms[] = {
      Atom(dwarf::DW_ATOM_die_offset, dwarf::DW_FORM_data4)};
#else
  // FIXME: Erase this path once the minimum MSCV version has been bumped.
  static const SmallVector<Atom, 4> Atoms;
#endif

#ifndef NDEBUG
  void print(raw_ostream &OS) const override;
#endif
protected:
  uint64_t order() const override { return Die.getOffset(); }

  const DIE &Die;
};

/// Accelerator table data implementation for Apple type accelerator tables.
class AppleAccelTableTypeData : public AppleAccelTableOffsetData {
public:
  AppleAccelTableTypeData(const DIE &D) : AppleAccelTableOffsetData(D) {}

  void emit(AsmPrinter *Asm) const override;

#ifndef _MSC_VER
  // The line below is rejected by older versions (TBD) of MSVC.
  static constexpr Atom Atoms[] = {
      Atom(dwarf::DW_ATOM_die_offset, dwarf::DW_FORM_data4),
      Atom(dwarf::DW_ATOM_die_tag, dwarf::DW_FORM_data2),
      Atom(dwarf::DW_ATOM_type_flags, dwarf::DW_FORM_data1)};
#else
  // FIXME: Erase this path once the minimum MSCV version has been bumped.
  static const SmallVector<Atom, 4> Atoms;
#endif

#ifndef NDEBUG
  void print(raw_ostream &OS) const override;
#endif
};

/// Accelerator table data implementation for simple Apple accelerator tables
/// with a DIE offset but no actual DIE pointer.
class AppleAccelTableStaticOffsetData : public AppleAccelTableData {
public:
  AppleAccelTableStaticOffsetData(uint32_t Offset) : Offset(Offset) {}

  void emit(AsmPrinter *Asm) const override;

#ifndef _MSC_VER
  // The line below is rejected by older versions (TBD) of MSVC.
  static constexpr Atom Atoms[] = {
      Atom(dwarf::DW_ATOM_die_offset, dwarf::DW_FORM_data4)};
#else
  // FIXME: Erase this path once the minimum MSCV version has been bumped.
  static const SmallVector<Atom, 4> Atoms;
#endif

#ifndef NDEBUG
  void print(raw_ostream &OS) const override;
#endif
protected:
  uint64_t order() const override { return Offset; }

  uint32_t Offset;
};

/// Accelerator table data implementation for type accelerator tables with
/// a DIE offset but no actual DIE pointer.
class AppleAccelTableStaticTypeData : public AppleAccelTableStaticOffsetData {
public:
  AppleAccelTableStaticTypeData(uint32_t Offset, uint16_t Tag,
                                bool ObjCClassIsImplementation,
                                uint32_t QualifiedNameHash)
      : AppleAccelTableStaticOffsetData(Offset),
        QualifiedNameHash(QualifiedNameHash), Tag(Tag),
        ObjCClassIsImplementation(ObjCClassIsImplementation) {}

  void emit(AsmPrinter *Asm) const override;

#ifndef _MSC_VER
  // The line below is rejected by older versions (TBD) of MSVC.
  static constexpr Atom Atoms[] = {
      Atom(dwarf::DW_ATOM_die_offset, dwarf::DW_FORM_data4),
      Atom(dwarf::DW_ATOM_die_tag, dwarf::DW_FORM_data2),
      Atom(5, dwarf::DW_FORM_data1), Atom(6, dwarf::DW_FORM_data4)};
#else
  // FIXME: Erase this path once the minimum MSCV version has been bumped.
  static const SmallVector<Atom, 4> Atoms;
#endif

#ifndef NDEBUG
  void print(raw_ostream &OS) const override;
#endif
protected:
  uint64_t order() const override { return Offset; }

  uint32_t QualifiedNameHash;
  uint16_t Tag;
  bool ObjCClassIsImplementation;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_DWARFACCELTABLE_H
