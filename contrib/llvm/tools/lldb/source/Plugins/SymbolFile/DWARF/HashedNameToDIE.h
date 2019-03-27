//===-- HashedNameToDIE.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef SymbolFileDWARF_HashedNameToDIE_h_
#define SymbolFileDWARF_HashedNameToDIE_h_

#include <vector>

#include "lldb/Core/MappedHash.h"
#include "lldb/Core/dwarf.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/lldb-defines.h"

#include "DWARFDefines.h"
#include "DWARFFormValue.h"
#include "NameToDIE.h"

class DWARFMappedHash {
public:
  enum AtomType : uint16_t {
    eAtomTypeNULL = 0u,
    eAtomTypeDIEOffset = 1u, // DIE offset, check form for encoding
    eAtomTypeCUOffset = 2u,  // DIE offset of the compiler unit header that
                             // contains the item in question
    eAtomTypeTag = 3u, // DW_TAG_xxx value, should be encoded as DW_FORM_data1
                       // (if no tags exceed 255) or DW_FORM_data2
    eAtomTypeNameFlags = 4u,   // Flags from enum NameFlags
    eAtomTypeTypeFlags = 5u,   // Flags from enum TypeFlags,
    eAtomTypeQualNameHash = 6u // A 32 bit hash of the full qualified name
                               // (since all hash entries are basename only)
    // For example a type like "std::vector<int>::iterator" would have a name of
    // "iterator"
    // and a 32 bit hash for "std::vector<int>::iterator" to allow us to not
    // have to pull
    // in debug info for a type when we know the fully qualified name.
  };

  // Bit definitions for the eAtomTypeTypeFlags flags
  enum TypeFlags {
    // Always set for C++, only set for ObjC if this is the
    // @implementation for class
    eTypeFlagClassIsImplementation = (1u << 1)
  };

  struct DIEInfo {
    dw_offset_t cu_offset;
    dw_offset_t offset; // The DIE offset
    dw_tag_t tag;
    uint32_t type_flags;          // Any flags for this DIEInfo
    uint32_t qualified_name_hash; // A 32 bit hash of the fully qualified name

    DIEInfo();
    DIEInfo(dw_offset_t c, dw_offset_t o, dw_tag_t t, uint32_t f, uint32_t h);
  };

  struct Atom {
    AtomType type;
    dw_form_t form;
  };

  typedef std::vector<DIEInfo> DIEInfoArray;
  typedef std::vector<Atom> AtomArray;

  class Prologue {
  public:
    Prologue(dw_offset_t _die_base_offset = 0);

    void ClearAtoms();

    bool ContainsAtom(AtomType atom_type) const;

    void Clear();

    void AppendAtom(AtomType type, dw_form_t form);

    lldb::offset_t Read(const lldb_private::DataExtractor &data,
                        lldb::offset_t offset);

    size_t GetByteSize() const;

    size_t GetMinimumHashDataByteSize() const;

    bool HashDataHasFixedByteSize() const;

    // DIE offset base so die offsets in hash_data can be CU relative
    dw_offset_t die_base_offset;
    AtomArray atoms;
    uint32_t atom_mask;
    size_t min_hash_data_byte_size;
    bool hash_data_has_fixed_byte_size;
  };

  class Header : public MappedHash::Header<Prologue> {
  public:
    size_t GetByteSize(const HeaderData &header_data) override;

    lldb::offset_t Read(lldb_private::DataExtractor &data,
                        lldb::offset_t offset) override;

    bool Read(const lldb_private::DWARFDataExtractor &data,
              lldb::offset_t *offset_ptr, DIEInfo &hash_data) const;

    void Dump(lldb_private::Stream &strm, const DIEInfo &hash_data) const;
  };

  // A class for reading and using a saved hash table from a block of data
  // in memory
  class MemoryTable
      : public MappedHash::MemoryTable<uint32_t, DWARFMappedHash::Header,
                                       DIEInfoArray> {
  public:
    MemoryTable(lldb_private::DWARFDataExtractor &table_data,
                const lldb_private::DWARFDataExtractor &string_table,
                const char *name);

    const char *GetStringForKeyType(KeyType key) const override;

    bool ReadHashData(uint32_t hash_data_offset,
                      HashData &hash_data) const override;

    size_t
    AppendAllDIEsThatMatchingRegex(const lldb_private::RegularExpression &regex,
                                   DIEInfoArray &die_info_array) const;

    size_t AppendAllDIEsInRange(const uint32_t die_offset_start,
                                const uint32_t die_offset_end,
                                DIEInfoArray &die_info_array) const;

    size_t FindByName(llvm::StringRef name, DIEArray &die_offsets);

    size_t FindByNameAndTag(llvm::StringRef name, const dw_tag_t tag,
                            DIEArray &die_offsets);

    size_t FindByNameAndTagAndQualifiedNameHash(
        llvm::StringRef name, const dw_tag_t tag,
        const uint32_t qualified_name_hash, DIEArray &die_offsets);

    size_t FindCompleteObjCClassByName(llvm::StringRef name,
                                       DIEArray &die_offsets,
                                       bool must_be_implementation);

  protected:
    Result AppendHashDataForRegularExpression(
        const lldb_private::RegularExpression &regex,
        lldb::offset_t *hash_data_offset_ptr, Pair &pair) const;

    size_t FindByName(llvm::StringRef name, DIEInfoArray &die_info_array);

    Result GetHashDataForName(llvm::StringRef name,
                              lldb::offset_t *hash_data_offset_ptr,
                              Pair &pair) const override;

    lldb_private::DWARFDataExtractor m_data;
    lldb_private::DWARFDataExtractor m_string_table;
    std::string m_name;
  };

  static void ExtractDIEArray(const DIEInfoArray &die_info_array,
                              DIEArray &die_offsets);

protected:
  static void ExtractDIEArray(const DIEInfoArray &die_info_array,
                              const dw_tag_t tag, DIEArray &die_offsets);

  static void ExtractDIEArray(const DIEInfoArray &die_info_array,
                              const dw_tag_t tag,
                              const uint32_t qualified_name_hash,
                              DIEArray &die_offsets);

  static void
  ExtractClassOrStructDIEArray(const DIEInfoArray &die_info_array,
                               bool return_implementation_only_if_available,
                               DIEArray &die_offsets);

  static void ExtractTypesFromDIEArray(const DIEInfoArray &die_info_array,
                                       uint32_t type_flag_mask,
                                       uint32_t type_flag_value,
                                       DIEArray &die_offsets);

  static const char *GetAtomTypeName(uint16_t atom);
};

#endif // SymbolFileDWARF_HashedNameToDIE_h_
