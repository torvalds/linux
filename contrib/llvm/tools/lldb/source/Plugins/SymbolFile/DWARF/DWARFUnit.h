//===-- DWARFUnit.h ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef SymbolFileDWARF_DWARFUnit_h_
#define SymbolFileDWARF_DWARFUnit_h_

#include "DWARFDIE.h"
#include "DWARFDebugInfoEntry.h"
#include "lldb/lldb-enumerations.h"
#include "llvm/Support/RWMutex.h"
#include <atomic>

class DWARFUnit;
class DWARFCompileUnit;
class NameToDIE;
class SymbolFileDWARF;
class SymbolFileDWARFDwo;

typedef std::shared_ptr<DWARFUnit> DWARFUnitSP;

enum DWARFProducer {
  eProducerInvalid = 0,
  eProducerClang,
  eProducerGCC,
  eProducerLLVMGCC,
  eProcucerOther
};

class DWARFUnit {
  using die_iterator_range =
      llvm::iterator_range<DWARFDebugInfoEntry::collection::iterator>;

public:
  virtual ~DWARFUnit();

  void ExtractUnitDIEIfNeeded();
  void ExtractDIEsIfNeeded();

  class ScopedExtractDIEs {
    DWARFUnit *m_cu;
  public:
    bool m_clear_dies = false;
    ScopedExtractDIEs(DWARFUnit *cu);
    ~ScopedExtractDIEs();
    DISALLOW_COPY_AND_ASSIGN(ScopedExtractDIEs);
    ScopedExtractDIEs(ScopedExtractDIEs &&rhs);
    ScopedExtractDIEs &operator=(ScopedExtractDIEs &&rhs);
  };
  ScopedExtractDIEs ExtractDIEsScoped();

  DWARFDIE LookupAddress(const dw_addr_t address);
  size_t AppendDIEsWithTag(const dw_tag_t tag,
                           DWARFDIECollection &matching_dies,
                           uint32_t depth = UINT32_MAX) const;
  bool Verify(lldb_private::Stream *s) const;
  virtual void Dump(lldb_private::Stream *s) const = 0;
  //------------------------------------------------------------------
  /// Get the data that contains the DIE information for this unit.
  ///
  /// This will return the correct bytes that contain the data for
  /// this DWARFUnit. It could be .debug_info or .debug_types
  /// depending on where the data for this unit originates.
  ///
  /// @return
  ///   The correct data for the DIE information in this unit.
  //------------------------------------------------------------------
  virtual const lldb_private::DWARFDataExtractor &GetData() const = 0;
  //------------------------------------------------------------------
  /// Get the size in bytes of the compile unit header.
  ///
  /// @return
  ///     Byte size of the compile unit header
  //------------------------------------------------------------------
  virtual uint32_t GetHeaderByteSize() const = 0;
  // Offset of the initial length field.
  dw_offset_t GetOffset() const { return m_offset; }
  lldb::user_id_t GetID() const;
  //------------------------------------------------------------------
  /// Get the size in bytes of the length field in the header.
  ///
  /// In DWARF32 this is just 4 bytes, and DWARF64 it is 12 where 4
  /// are 0xFFFFFFFF followed by the actual 64 bit length.
  ///
  /// @return
  ///     Byte size of the compile unit header length field
  //------------------------------------------------------------------
  size_t GetLengthByteSize() const { return IsDWARF64() ? 12 : 4; }
  
  bool ContainsDIEOffset(dw_offset_t die_offset) const {
    return die_offset >= GetFirstDIEOffset() &&
           die_offset < GetNextCompileUnitOffset();
  }
  dw_offset_t GetFirstDIEOffset() const {
    return m_offset + GetHeaderByteSize();
  }
  dw_offset_t GetNextCompileUnitOffset() const;
  // Size of the CU data (without initial length and without header).
  size_t GetDebugInfoSize() const;
  // Size of the CU data incl. header but without initial length.
  uint32_t GetLength() const { return m_length; }
  uint16_t GetVersion() const { return m_version; }
  const DWARFAbbreviationDeclarationSet *GetAbbreviations() const;
  dw_offset_t GetAbbrevOffset() const;
  uint8_t GetAddressByteSize() const { return m_addr_size; }
  dw_addr_t GetBaseAddress() const { return m_base_addr; }
  dw_addr_t GetAddrBase() const { return m_addr_base; }
  dw_addr_t GetRangesBase() const { return m_ranges_base; }
  dw_addr_t GetStrOffsetsBase() const { return m_str_offsets_base; }
  void SetAddrBase(dw_addr_t addr_base);
  void SetRangesBase(dw_addr_t ranges_base);
  void SetBaseObjOffset(dw_offset_t base_obj_offset);
  void SetStrOffsetsBase(dw_offset_t str_offsets_base);
  void BuildAddressRangeTable(SymbolFileDWARF *dwarf,
                              DWARFDebugAranges *debug_aranges);

  lldb::ByteOrder GetByteOrder() const;

  lldb_private::TypeSystem *GetTypeSystem();

  const DWARFDebugAranges &GetFunctionAranges();

  DWARFFormValue::FixedFormSizes GetFixedFormSizes();

  void SetBaseAddress(dw_addr_t base_addr);

  DWARFBaseDIE GetUnitDIEOnly() { return DWARFDIE(this, GetUnitDIEPtrOnly()); }

  DWARFDIE DIE() { return DWARFDIE(this, DIEPtr()); }

  DWARFDIE GetDIE(dw_offset_t die_offset);

  static uint8_t GetAddressByteSize(const DWARFUnit *cu);

  static bool IsDWARF64(const DWARFUnit *cu);

  static uint8_t GetDefaultAddressSize();

  void *GetUserData() const;

  void SetUserData(void *d);

  bool Supports_DW_AT_APPLE_objc_complete_type();

  bool DW_AT_decl_file_attributes_are_invalid();

  bool Supports_unnamed_objc_bitfields();

  SymbolFileDWARF *GetSymbolFileDWARF() const;

  DWARFProducer GetProducer();

  uint32_t GetProducerVersionMajor();

  uint32_t GetProducerVersionMinor();

  uint32_t GetProducerVersionUpdate();

  static lldb::LanguageType LanguageTypeFromDWARF(uint64_t val);

  lldb::LanguageType GetLanguageType();

  bool IsDWARF64() const { return m_is_dwarf64; }

  bool GetIsOptimized();

  SymbolFileDWARFDwo *GetDwoSymbolFile() const;

  dw_offset_t GetBaseObjOffset() const;

  die_iterator_range dies() {
    ExtractDIEsIfNeeded();
    return die_iterator_range(m_die_array.begin(), m_die_array.end());
  }

protected:
  DWARFUnit(SymbolFileDWARF *dwarf);

  SymbolFileDWARF *m_dwarf = nullptr;
  std::unique_ptr<SymbolFileDWARFDwo> m_dwo_symbol_file;
  const DWARFAbbreviationDeclarationSet *m_abbrevs = nullptr;
  void *m_user_data = nullptr;
  // The compile unit debug information entry item
  DWARFDebugInfoEntry::collection m_die_array;
  mutable llvm::sys::RWMutex m_die_array_mutex;
  // It is used for tracking of ScopedExtractDIEs instances.
  mutable llvm::sys::RWMutex m_die_array_scoped_mutex;
  // ScopedExtractDIEs instances should not call ClearDIEsRWLocked()
  // as someone called ExtractDIEsIfNeeded().
  std::atomic<bool> m_cancel_scopes;
  // GetUnitDIEPtrOnly() needs to return pointer to the first DIE.
  // But the first element of m_die_array after ExtractUnitDIEIfNeeded()
  // would possibly move in memory after later ExtractDIEsIfNeeded().
  DWARFDebugInfoEntry m_first_die;
  llvm::sys::RWMutex m_first_die_mutex;
  // A table similar to the .debug_aranges table, but this one points to the
  // exact DW_TAG_subprogram DIEs
  std::unique_ptr<DWARFDebugAranges> m_func_aranges_ap;
  dw_addr_t m_base_addr = 0;
  dw_offset_t m_length = 0;
  uint16_t m_version = 0;
  uint8_t m_addr_size = 0;
  uint8_t m_unit_type = 0;
  uint64_t m_dwo_id = 0;
  DWARFProducer m_producer = eProducerInvalid;
  uint32_t m_producer_version_major = 0;
  uint32_t m_producer_version_minor = 0;
  uint32_t m_producer_version_update = 0;
  lldb::LanguageType m_language_type = lldb::eLanguageTypeUnknown;
  bool m_is_dwarf64 = false;
  lldb_private::LazyBool m_is_optimized = lldb_private::eLazyBoolCalculate;
  dw_addr_t m_addr_base = 0;   // Value of DW_AT_addr_base
  dw_addr_t m_ranges_base = 0; // Value of DW_AT_ranges_base
  // If this is a dwo compile unit this is the offset of the base compile unit
  // in the main object file
  dw_offset_t m_base_obj_offset = DW_INVALID_OFFSET;
  dw_offset_t m_str_offsets_base = 0; // Value of DW_AT_str_offsets_base.
  // Offset of the initial length field.
  dw_offset_t m_offset;

private:
  void ParseProducerInfo();
  void ExtractDIEsRWLocked();
  void ClearDIEsRWLocked();

  // Get the DWARF unit DWARF debug informration entry. Parse the single DIE
  // if needed.
  const DWARFDebugInfoEntry *GetUnitDIEPtrOnly() {
    ExtractUnitDIEIfNeeded();
    // m_first_die_mutex is not required as m_first_die is never cleared.
    if (!m_first_die)
      return NULL;
    return &m_first_die;
  }

  // Get all DWARF debug informration entries. Parse all DIEs if needed.
  const DWARFDebugInfoEntry *DIEPtr() {
    ExtractDIEsIfNeeded();
    if (m_die_array.empty())
      return NULL;
    return &m_die_array[0];
  }

  void AddUnitDIE(const DWARFDebugInfoEntry &cu_die);
  void ExtractDIEsEndCheck(lldb::offset_t offset) const;

  DISALLOW_COPY_AND_ASSIGN(DWARFUnit);
};

#endif // SymbolFileDWARF_DWARFUnit_h_
