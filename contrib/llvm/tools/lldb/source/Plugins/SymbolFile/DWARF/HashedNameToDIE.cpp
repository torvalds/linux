//===-- HashedNameToDIE.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "HashedNameToDIE.h"
#include "llvm/ADT/StringRef.h"

void DWARFMappedHash::ExtractDIEArray(const DIEInfoArray &die_info_array,
                                      DIEArray &die_offsets) {
  const size_t count = die_info_array.size();
  for (size_t i = 0; i < count; ++i)
    die_offsets.emplace_back(die_info_array[i].cu_offset,
                             die_info_array[i].offset);
}

void DWARFMappedHash::ExtractDIEArray(const DIEInfoArray &die_info_array,
                                      const dw_tag_t tag,
                                      DIEArray &die_offsets) {
  if (tag == 0) {
    ExtractDIEArray(die_info_array, die_offsets);
  } else {
    const size_t count = die_info_array.size();
    for (size_t i = 0; i < count; ++i) {
      const dw_tag_t die_tag = die_info_array[i].tag;
      bool tag_matches = die_tag == 0 || tag == die_tag;
      if (!tag_matches) {
        if (die_tag == DW_TAG_class_type || die_tag == DW_TAG_structure_type)
          tag_matches =
              tag == DW_TAG_structure_type || tag == DW_TAG_class_type;
      }
      if (tag_matches)
        die_offsets.emplace_back(die_info_array[i].cu_offset,
                                 die_info_array[i].offset);
    }
  }
}

void DWARFMappedHash::ExtractDIEArray(const DIEInfoArray &die_info_array,
                                      const dw_tag_t tag,
                                      const uint32_t qualified_name_hash,
                                      DIEArray &die_offsets) {
  if (tag == 0) {
    ExtractDIEArray(die_info_array, die_offsets);
  } else {
    const size_t count = die_info_array.size();
    for (size_t i = 0; i < count; ++i) {
      if (qualified_name_hash != die_info_array[i].qualified_name_hash)
        continue;
      const dw_tag_t die_tag = die_info_array[i].tag;
      bool tag_matches = die_tag == 0 || tag == die_tag;
      if (!tag_matches) {
        if (die_tag == DW_TAG_class_type || die_tag == DW_TAG_structure_type)
          tag_matches =
              tag == DW_TAG_structure_type || tag == DW_TAG_class_type;
      }
      if (tag_matches)
        die_offsets.emplace_back(die_info_array[i].cu_offset,
                                 die_info_array[i].offset);
    }
  }
}

void DWARFMappedHash::ExtractClassOrStructDIEArray(
    const DIEInfoArray &die_info_array,
    bool return_implementation_only_if_available, DIEArray &die_offsets) {
  const size_t count = die_info_array.size();
  for (size_t i = 0; i < count; ++i) {
    const dw_tag_t die_tag = die_info_array[i].tag;
    if (die_tag == 0 || die_tag == DW_TAG_class_type ||
        die_tag == DW_TAG_structure_type) {
      if (die_info_array[i].type_flags & eTypeFlagClassIsImplementation) {
        if (return_implementation_only_if_available) {
          // We found the one true definition for this class, so only return
          // that
          die_offsets.clear();
          die_offsets.emplace_back(die_info_array[i].cu_offset,
                                   die_info_array[i].offset);
          return;
        } else {
          // Put the one true definition as the first entry so it matches first
          die_offsets.emplace(die_offsets.begin(), die_info_array[i].cu_offset,
                              die_info_array[i].offset);
        }
      } else {
        die_offsets.emplace_back(die_info_array[i].cu_offset,
                                 die_info_array[i].offset);
      }
    }
  }
}

void DWARFMappedHash::ExtractTypesFromDIEArray(
    const DIEInfoArray &die_info_array, uint32_t type_flag_mask,
    uint32_t type_flag_value, DIEArray &die_offsets) {
  const size_t count = die_info_array.size();
  for (size_t i = 0; i < count; ++i) {
    if ((die_info_array[i].type_flags & type_flag_mask) == type_flag_value)
      die_offsets.emplace_back(die_info_array[i].cu_offset,
                               die_info_array[i].offset);
  }
}

const char *DWARFMappedHash::GetAtomTypeName(uint16_t atom) {
  switch (atom) {
  case eAtomTypeNULL:
    return "NULL";
  case eAtomTypeDIEOffset:
    return "die-offset";
  case eAtomTypeCUOffset:
    return "cu-offset";
  case eAtomTypeTag:
    return "die-tag";
  case eAtomTypeNameFlags:
    return "name-flags";
  case eAtomTypeTypeFlags:
    return "type-flags";
  case eAtomTypeQualNameHash:
    return "qualified-name-hash";
  }
  return "<invalid>";
}

DWARFMappedHash::DIEInfo::DIEInfo()
    : cu_offset(DW_INVALID_OFFSET), offset(DW_INVALID_OFFSET), tag(0),
      type_flags(0), qualified_name_hash(0) {}

DWARFMappedHash::DIEInfo::DIEInfo(dw_offset_t c, dw_offset_t o, dw_tag_t t,
                                  uint32_t f, uint32_t h)
    : cu_offset(c), offset(o), tag(t), type_flags(f), qualified_name_hash(h) {}

DWARFMappedHash::Prologue::Prologue(dw_offset_t _die_base_offset)
    : die_base_offset(_die_base_offset), atoms(), atom_mask(0),
      min_hash_data_byte_size(0), hash_data_has_fixed_byte_size(true) {
  // Define an array of DIE offsets by first defining an array, and then define
  // the atom type for the array, in this case we have an array of DIE offsets
  AppendAtom(eAtomTypeDIEOffset, DW_FORM_data4);
}

void DWARFMappedHash::Prologue::ClearAtoms() {
  hash_data_has_fixed_byte_size = true;
  min_hash_data_byte_size = 0;
  atom_mask = 0;
  atoms.clear();
}

bool DWARFMappedHash::Prologue::ContainsAtom(AtomType atom_type) const {
  return (atom_mask & (1u << atom_type)) != 0;
}

void DWARFMappedHash::Prologue::Clear() {
  die_base_offset = 0;
  ClearAtoms();
}

void DWARFMappedHash::Prologue::AppendAtom(AtomType type, dw_form_t form) {
  atoms.push_back({type, form});
  atom_mask |= 1u << type;
  switch (form) {
  case DW_FORM_indirect:
  case DW_FORM_exprloc:
  case DW_FORM_flag_present:
  case DW_FORM_ref_sig8:
    llvm_unreachable("Unhandled atom form");

  case DW_FORM_string:
  case DW_FORM_block:
  case DW_FORM_block1:
  case DW_FORM_sdata:
  case DW_FORM_udata:
  case DW_FORM_ref_udata:
  case DW_FORM_GNU_addr_index:
  case DW_FORM_GNU_str_index:
    hash_data_has_fixed_byte_size = false;
    LLVM_FALLTHROUGH;
  case DW_FORM_flag:
  case DW_FORM_data1:
  case DW_FORM_ref1:
  case DW_FORM_sec_offset:
    min_hash_data_byte_size += 1;
    break;

  case DW_FORM_block2:
    hash_data_has_fixed_byte_size = false;
    LLVM_FALLTHROUGH;
  case DW_FORM_data2:
  case DW_FORM_ref2:
    min_hash_data_byte_size += 2;
    break;

  case DW_FORM_block4:
    hash_data_has_fixed_byte_size = false;
    LLVM_FALLTHROUGH;
  case DW_FORM_data4:
  case DW_FORM_ref4:
  case DW_FORM_addr:
  case DW_FORM_ref_addr:
  case DW_FORM_strp:
    min_hash_data_byte_size += 4;
    break;

  case DW_FORM_data8:
  case DW_FORM_ref8:
    min_hash_data_byte_size += 8;
    break;
  }
}

lldb::offset_t
DWARFMappedHash::Prologue::Read(const lldb_private::DataExtractor &data,
                                lldb::offset_t offset) {
  ClearAtoms();

  die_base_offset = data.GetU32(&offset);

  const uint32_t atom_count = data.GetU32(&offset);
  if (atom_count == 0x00060003u) {
    // Old format, deal with contents of old pre-release format
    while (data.GetU32(&offset))
      /* do nothing */;

    // Hardcode to the only known value for now.
    AppendAtom(eAtomTypeDIEOffset, DW_FORM_data4);
  } else {
    for (uint32_t i = 0; i < atom_count; ++i) {
      AtomType type = (AtomType)data.GetU16(&offset);
      dw_form_t form = (dw_form_t)data.GetU16(&offset);
      AppendAtom(type, form);
    }
  }
  return offset;
}

size_t DWARFMappedHash::Prologue::GetByteSize() const {
  // Add an extra count to the atoms size for the zero termination Atom that
  // gets written to disk
  return sizeof(die_base_offset) + sizeof(uint32_t) +
         atoms.size() * sizeof(Atom);
}

size_t DWARFMappedHash::Prologue::GetMinimumHashDataByteSize() const {
  return min_hash_data_byte_size;
}

bool DWARFMappedHash::Prologue::HashDataHasFixedByteSize() const {
  return hash_data_has_fixed_byte_size;
}

size_t DWARFMappedHash::Header::GetByteSize(const HeaderData &header_data) {
  return header_data.GetByteSize();
}

lldb::offset_t DWARFMappedHash::Header::Read(lldb_private::DataExtractor &data,
                                             lldb::offset_t offset) {
  offset = MappedHash::Header<Prologue>::Read(data, offset);
  if (offset != UINT32_MAX) {
    offset = header_data.Read(data, offset);
  }
  return offset;
}

bool DWARFMappedHash::Header::Read(const lldb_private::DWARFDataExtractor &data,
                                   lldb::offset_t *offset_ptr,
                                   DIEInfo &hash_data) const {
  const size_t num_atoms = header_data.atoms.size();
  if (num_atoms == 0)
    return false;

  for (size_t i = 0; i < num_atoms; ++i) {
    DWARFFormValue form_value(NULL, header_data.atoms[i].form);

    if (!form_value.ExtractValue(data, offset_ptr))
      return false;

    switch (header_data.atoms[i].type) {
    case eAtomTypeDIEOffset: // DIE offset, check form for encoding
      hash_data.offset =
          DWARFFormValue::IsDataForm(form_value.Form())
              ? form_value.Unsigned()
              : form_value.Reference(header_data.die_base_offset);
      break;

    case eAtomTypeTag: // DW_TAG value for the DIE
      hash_data.tag = (dw_tag_t)form_value.Unsigned();
      break;

    case eAtomTypeTypeFlags: // Flags from enum TypeFlags
      hash_data.type_flags = (uint32_t)form_value.Unsigned();
      break;

    case eAtomTypeQualNameHash: // Flags from enum TypeFlags
      hash_data.qualified_name_hash = form_value.Unsigned();
      break;

    default:
      // We can always skip atoms we don't know about
      break;
    }
  }
  return true;
}

void DWARFMappedHash::Header::Dump(lldb_private::Stream &strm,
                                   const DIEInfo &hash_data) const {
  const size_t num_atoms = header_data.atoms.size();
  for (size_t i = 0; i < num_atoms; ++i) {
    if (i > 0)
      strm.PutCString(", ");

    DWARFFormValue form_value(NULL, header_data.atoms[i].form);
    switch (header_data.atoms[i].type) {
    case eAtomTypeDIEOffset: // DIE offset, check form for encoding
      strm.Printf("{0x%8.8x}", hash_data.offset);
      break;

    case eAtomTypeTag: // DW_TAG value for the DIE
    {
      const char *tag_cstr = lldb_private::DW_TAG_value_to_name(hash_data.tag);
      if (tag_cstr)
        strm.PutCString(tag_cstr);
      else
        strm.Printf("DW_TAG_(0x%4.4x)", hash_data.tag);
    } break;

    case eAtomTypeTypeFlags: // Flags from enum TypeFlags
      strm.Printf("0x%2.2x", hash_data.type_flags);
      if (hash_data.type_flags) {
        strm.PutCString(" (");
        if (hash_data.type_flags & eTypeFlagClassIsImplementation)
          strm.PutCString(" implementation");
        strm.PutCString(" )");
      }
      break;

    case eAtomTypeQualNameHash: // Flags from enum TypeFlags
      strm.Printf("0x%8.8x", hash_data.qualified_name_hash);
      break;

    default:
      strm.Printf("AtomType(0x%x)", header_data.atoms[i].type);
      break;
    }
  }
}

DWARFMappedHash::MemoryTable::MemoryTable(
    lldb_private::DWARFDataExtractor &table_data,
    const lldb_private::DWARFDataExtractor &string_table, const char *name)
    : MappedHash::MemoryTable<uint32_t, Header, DIEInfoArray>(table_data),
      m_data(table_data), m_string_table(string_table), m_name(name) {}

const char *
DWARFMappedHash::MemoryTable::GetStringForKeyType(KeyType key) const {
  // The key in the DWARF table is the .debug_str offset for the string
  return m_string_table.PeekCStr(key);
}

bool DWARFMappedHash::MemoryTable::ReadHashData(uint32_t hash_data_offset,
                                                HashData &hash_data) const {
  lldb::offset_t offset = hash_data_offset;
  offset += 4; // Skip string table offset that contains offset of hash name in
               // .debug_str
  const uint32_t count = m_data.GetU32(&offset);
  if (count > 0) {
    hash_data.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
      if (!m_header.Read(m_data, &offset, hash_data[i]))
        return false;
    }
  } else
    hash_data.clear();
  return true;
}

DWARFMappedHash::MemoryTable::Result
DWARFMappedHash::MemoryTable::GetHashDataForName(
    llvm::StringRef name, lldb::offset_t *hash_data_offset_ptr,
    Pair &pair) const {
  pair.key = m_data.GetU32(hash_data_offset_ptr);
  pair.value.clear();

  // If the key is zero, this terminates our chain of HashData objects for this
  // hash value.
  if (pair.key == 0)
    return eResultEndOfHashData;

  // There definitely should be a string for this string offset, if there
  // isn't, there is something wrong, return and error
  const char *strp_cstr = m_string_table.PeekCStr(pair.key);
  if (strp_cstr == NULL) {
    *hash_data_offset_ptr = UINT32_MAX;
    return eResultError;
  }

  const uint32_t count = m_data.GetU32(hash_data_offset_ptr);
  const size_t min_total_hash_data_size =
      count * m_header.header_data.GetMinimumHashDataByteSize();
  if (count > 0 &&
      m_data.ValidOffsetForDataOfSize(*hash_data_offset_ptr,
                                      min_total_hash_data_size)) {
    // We have at least one HashData entry, and we have enough data to parse at
    // least "count" HashData entries.

    // First make sure the entire C string matches...
    const bool match = name == strp_cstr;

    if (!match && m_header.header_data.HashDataHasFixedByteSize()) {
      // If the string doesn't match and we have fixed size data, we can just
      // add the total byte size of all HashData objects to the hash data
      // offset and be done...
      *hash_data_offset_ptr += min_total_hash_data_size;
    } else {
      // If the string does match, or we don't have fixed size data then we
      // need to read the hash data as a stream. If the string matches we also
      // append all HashData objects to the value array.
      for (uint32_t i = 0; i < count; ++i) {
        DIEInfo die_info;
        if (m_header.Read(m_data, hash_data_offset_ptr, die_info)) {
          // Only happened if the HashData of the string matched...
          if (match)
            pair.value.push_back(die_info);
        } else {
          // Something went wrong while reading the data
          *hash_data_offset_ptr = UINT32_MAX;
          return eResultError;
        }
      }
    }
    // Return the correct response depending on if the string matched or not...
    if (match)
      return eResultKeyMatch; // The key (cstring) matches and we have lookup
                              // results!
    else
      return eResultKeyMismatch; // The key doesn't match, this function will
                                 // get called
    // again for the next key/value or the key terminator which in our case is
    // a zero .debug_str offset.
  } else {
    *hash_data_offset_ptr = UINT32_MAX;
    return eResultError;
  }
}

DWARFMappedHash::MemoryTable::Result
DWARFMappedHash::MemoryTable::AppendHashDataForRegularExpression(
    const lldb_private::RegularExpression &regex,
    lldb::offset_t *hash_data_offset_ptr, Pair &pair) const {
  pair.key = m_data.GetU32(hash_data_offset_ptr);
  // If the key is zero, this terminates our chain of HashData objects for this
  // hash value.
  if (pair.key == 0)
    return eResultEndOfHashData;

  // There definitely should be a string for this string offset, if there
  // isn't, there is something wrong, return and error
  const char *strp_cstr = m_string_table.PeekCStr(pair.key);
  if (strp_cstr == NULL)
    return eResultError;

  const uint32_t count = m_data.GetU32(hash_data_offset_ptr);
  const size_t min_total_hash_data_size =
      count * m_header.header_data.GetMinimumHashDataByteSize();
  if (count > 0 &&
      m_data.ValidOffsetForDataOfSize(*hash_data_offset_ptr,
                                      min_total_hash_data_size)) {
    const bool match = regex.Execute(llvm::StringRef(strp_cstr));

    if (!match && m_header.header_data.HashDataHasFixedByteSize()) {
      // If the regex doesn't match and we have fixed size data, we can just
      // add the total byte size of all HashData objects to the hash data
      // offset and be done...
      *hash_data_offset_ptr += min_total_hash_data_size;
    } else {
      // If the string does match, or we don't have fixed size data then we
      // need to read the hash data as a stream. If the string matches we also
      // append all HashData objects to the value array.
      for (uint32_t i = 0; i < count; ++i) {
        DIEInfo die_info;
        if (m_header.Read(m_data, hash_data_offset_ptr, die_info)) {
          // Only happened if the HashData of the string matched...
          if (match)
            pair.value.push_back(die_info);
        } else {
          // Something went wrong while reading the data
          *hash_data_offset_ptr = UINT32_MAX;
          return eResultError;
        }
      }
    }
    // Return the correct response depending on if the string matched or not...
    if (match)
      return eResultKeyMatch; // The key (cstring) matches and we have lookup
                              // results!
    else
      return eResultKeyMismatch; // The key doesn't match, this function will
                                 // get called
    // again for the next key/value or the key terminator which in our case is
    // a zero .debug_str offset.
  } else {
    *hash_data_offset_ptr = UINT32_MAX;
    return eResultError;
  }
}

size_t DWARFMappedHash::MemoryTable::AppendAllDIEsThatMatchingRegex(
    const lldb_private::RegularExpression &regex,
    DIEInfoArray &die_info_array) const {
  const uint32_t hash_count = m_header.hashes_count;
  Pair pair;
  for (uint32_t offset_idx = 0; offset_idx < hash_count; ++offset_idx) {
    lldb::offset_t hash_data_offset = GetHashDataOffset(offset_idx);
    while (hash_data_offset != UINT32_MAX) {
      const lldb::offset_t prev_hash_data_offset = hash_data_offset;
      Result hash_result =
          AppendHashDataForRegularExpression(regex, &hash_data_offset, pair);
      if (prev_hash_data_offset == hash_data_offset)
        break;

      // Check the result of getting our hash data
      switch (hash_result) {
      case eResultKeyMatch:
      case eResultKeyMismatch:
        // Whether we matches or not, it doesn't matter, we keep looking.
        break;

      case eResultEndOfHashData:
      case eResultError:
        hash_data_offset = UINT32_MAX;
        break;
      }
    }
  }
  die_info_array.swap(pair.value);
  return die_info_array.size();
}

size_t DWARFMappedHash::MemoryTable::AppendAllDIEsInRange(
    const uint32_t die_offset_start, const uint32_t die_offset_end,
    DIEInfoArray &die_info_array) const {
  const uint32_t hash_count = m_header.hashes_count;
  for (uint32_t offset_idx = 0; offset_idx < hash_count; ++offset_idx) {
    bool done = false;
    lldb::offset_t hash_data_offset = GetHashDataOffset(offset_idx);
    while (!done && hash_data_offset != UINT32_MAX) {
      KeyType key = m_data.GetU32(&hash_data_offset);
      // If the key is zero, this terminates our chain of HashData objects for
      // this hash value.
      if (key == 0)
        break;

      const uint32_t count = m_data.GetU32(&hash_data_offset);
      for (uint32_t i = 0; i < count; ++i) {
        DIEInfo die_info;
        if (m_header.Read(m_data, &hash_data_offset, die_info)) {
          if (die_info.offset == 0)
            done = true;
          if (die_offset_start <= die_info.offset &&
              die_info.offset < die_offset_end)
            die_info_array.push_back(die_info);
        }
      }
    }
  }
  return die_info_array.size();
}

size_t DWARFMappedHash::MemoryTable::FindByName(llvm::StringRef name,
                                                DIEArray &die_offsets) {
  if (name.empty())
    return 0;

  DIEInfoArray die_info_array;
  if (FindByName(name, die_info_array))
    DWARFMappedHash::ExtractDIEArray(die_info_array, die_offsets);
  return die_info_array.size();
}

size_t DWARFMappedHash::MemoryTable::FindByNameAndTag(llvm::StringRef name,
                                                      const dw_tag_t tag,
                                                      DIEArray &die_offsets) {
  DIEInfoArray die_info_array;
  if (FindByName(name, die_info_array))
    DWARFMappedHash::ExtractDIEArray(die_info_array, tag, die_offsets);
  return die_info_array.size();
}

size_t DWARFMappedHash::MemoryTable::FindByNameAndTagAndQualifiedNameHash(
    llvm::StringRef name, const dw_tag_t tag,
    const uint32_t qualified_name_hash, DIEArray &die_offsets) {
  DIEInfoArray die_info_array;
  if (FindByName(name, die_info_array))
    DWARFMappedHash::ExtractDIEArray(die_info_array, tag, qualified_name_hash,
                                     die_offsets);
  return die_info_array.size();
}

size_t DWARFMappedHash::MemoryTable::FindCompleteObjCClassByName(
    llvm::StringRef name, DIEArray &die_offsets, bool must_be_implementation) {
  DIEInfoArray die_info_array;
  if (FindByName(name, die_info_array)) {
    if (must_be_implementation &&
        GetHeader().header_data.ContainsAtom(eAtomTypeTypeFlags)) {
      // If we have two atoms, then we have the DIE offset and the type flags
      // so we can find the objective C class efficiently.
      DWARFMappedHash::ExtractTypesFromDIEArray(die_info_array, UINT32_MAX,
                                                eTypeFlagClassIsImplementation,
                                                die_offsets);
    } else {
      // We don't only want the one true definition, so try and see what we can
      // find, and only return class or struct DIEs. If we do have the full
      // implementation, then return it alone, else return all possible
      // matches.
      const bool return_implementation_only_if_available = true;
      DWARFMappedHash::ExtractClassOrStructDIEArray(
          die_info_array, return_implementation_only_if_available, die_offsets);
    }
  }
  return die_offsets.size();
}

size_t DWARFMappedHash::MemoryTable::FindByName(llvm::StringRef name,
                                                DIEInfoArray &die_info_array) {
  if (name.empty())
    return 0;

  Pair kv_pair;
  size_t old_size = die_info_array.size();
  if (Find(name, kv_pair)) {
    die_info_array.swap(kv_pair.value);
    return die_info_array.size() - old_size;
  }
  return 0;
}
