//===-- DWARFUnit.cpp -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "DWARFUnit.h"

#include "lldb/Core/Module.h"
#include "lldb/Host/StringConvert.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/LineTable.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/Timer.h"

#include "DWARFDIECollection.h"
#include "DWARFDebugAranges.h"
#include "DWARFDebugInfo.h"
#include "LogChannelDWARF.h"
#include "SymbolFileDWARFDebugMap.h"
#include "SymbolFileDWARFDwo.h"

using namespace lldb;
using namespace lldb_private;
using namespace std;

extern int g_verbose;

DWARFUnit::DWARFUnit(SymbolFileDWARF *dwarf)
    : m_dwarf(dwarf), m_cancel_scopes(false) {}

DWARFUnit::~DWARFUnit() {}

//----------------------------------------------------------------------
// Parses first DIE of a compile unit.
//----------------------------------------------------------------------
void DWARFUnit::ExtractUnitDIEIfNeeded() {
  {
    llvm::sys::ScopedReader lock(m_first_die_mutex);
    if (m_first_die)
      return; // Already parsed
  }
  llvm::sys::ScopedWriter lock(m_first_die_mutex);
  if (m_first_die)
    return; // Already parsed

  static Timer::Category func_cat(LLVM_PRETTY_FUNCTION);
  Timer scoped_timer(
      func_cat, "%8.8x: DWARFUnit::ExtractUnitDIEIfNeeded()", m_offset);

  // Set the offset to that of the first DIE and calculate the start of the
  // next compilation unit header.
  lldb::offset_t offset = GetFirstDIEOffset();

  // We are in our compile unit, parse starting at the offset we were told to
  // parse
  const DWARFDataExtractor &data = GetData();
  DWARFFormValue::FixedFormSizes fixed_form_sizes =
      DWARFFormValue::GetFixedFormSizesForAddressSize(GetAddressByteSize(),
                                                      IsDWARF64());
  if (offset < GetNextCompileUnitOffset() &&
      m_first_die.FastExtract(data, this, fixed_form_sizes, &offset)) {
    AddUnitDIE(m_first_die);
    return;
  }

  ExtractDIEsEndCheck(offset);
}

//----------------------------------------------------------------------
// Parses a compile unit and indexes its DIEs if it hasn't already been done.
// It will leave this compile unit extracted forever.
//----------------------------------------------------------------------
void DWARFUnit::ExtractDIEsIfNeeded() {
  m_cancel_scopes = true;

  {
    llvm::sys::ScopedReader lock(m_die_array_mutex);
    if (!m_die_array.empty())
      return; // Already parsed
  }
  llvm::sys::ScopedWriter lock(m_die_array_mutex);
  if (!m_die_array.empty())
    return; // Already parsed

  ExtractDIEsRWLocked();
}

//----------------------------------------------------------------------
// Parses a compile unit and indexes its DIEs if it hasn't already been done.
// It will clear this compile unit after returned instance gets out of scope,
// no other ScopedExtractDIEs instance is running for this compile unit
// and no ExtractDIEsIfNeeded() has been executed during this ScopedExtractDIEs
// lifetime.
//----------------------------------------------------------------------
DWARFUnit::ScopedExtractDIEs DWARFUnit::ExtractDIEsScoped() {
  ScopedExtractDIEs scoped(this);

  {
    llvm::sys::ScopedReader lock(m_die_array_mutex);
    if (!m_die_array.empty())
      return scoped; // Already parsed
  }
  llvm::sys::ScopedWriter lock(m_die_array_mutex);
  if (!m_die_array.empty())
    return scoped; // Already parsed

  // Otherwise m_die_array would be already populated.
  lldbassert(!m_cancel_scopes);

  ExtractDIEsRWLocked();
  scoped.m_clear_dies = true;
  return scoped;
}

DWARFUnit::ScopedExtractDIEs::ScopedExtractDIEs(DWARFUnit *cu) : m_cu(cu) {
  lldbassert(m_cu);
  m_cu->m_die_array_scoped_mutex.lock_shared();
}

DWARFUnit::ScopedExtractDIEs::~ScopedExtractDIEs() {
  if (!m_cu)
    return;
  m_cu->m_die_array_scoped_mutex.unlock_shared();
  if (!m_clear_dies || m_cu->m_cancel_scopes)
    return;
  // Be sure no other ScopedExtractDIEs is running anymore.
  llvm::sys::ScopedWriter lock_scoped(m_cu->m_die_array_scoped_mutex);
  llvm::sys::ScopedWriter lock(m_cu->m_die_array_mutex);
  if (m_cu->m_cancel_scopes)
    return;
  m_cu->ClearDIEsRWLocked();
}

DWARFUnit::ScopedExtractDIEs::ScopedExtractDIEs(ScopedExtractDIEs &&rhs)
    : m_cu(rhs.m_cu), m_clear_dies(rhs.m_clear_dies) {
  rhs.m_cu = nullptr;
}

DWARFUnit::ScopedExtractDIEs &DWARFUnit::ScopedExtractDIEs::operator=(
    DWARFUnit::ScopedExtractDIEs &&rhs) {
  m_cu = rhs.m_cu;
  rhs.m_cu = nullptr;
  m_clear_dies = rhs.m_clear_dies;
  return *this;
}

//----------------------------------------------------------------------
// Parses a compile unit and indexes its DIEs, m_die_array_mutex must be
// held R/W and m_die_array must be empty.
//----------------------------------------------------------------------
void DWARFUnit::ExtractDIEsRWLocked() {
  llvm::sys::ScopedWriter first_die_lock(m_first_die_mutex);

  static Timer::Category func_cat(LLVM_PRETTY_FUNCTION);
  Timer scoped_timer(
      func_cat, "%8.8x: DWARFUnit::ExtractDIEsIfNeeded()", m_offset);

  // Set the offset to that of the first DIE and calculate the start of the
  // next compilation unit header.
  lldb::offset_t offset = GetFirstDIEOffset();
  lldb::offset_t next_cu_offset = GetNextCompileUnitOffset();

  DWARFDebugInfoEntry die;
  // Keep a flat array of the DIE for binary lookup by DIE offset
  Log *log(
      LogChannelDWARF::GetLogIfAny(DWARF_LOG_DEBUG_INFO | DWARF_LOG_LOOKUPS));
  if (log) {
    m_dwarf->GetObjectFile()->GetModule()->LogMessageVerboseBacktrace(
        log,
        "DWARFUnit::ExtractDIEsIfNeeded () for compile unit at "
        ".debug_info[0x%8.8x]",
        GetOffset());
  }

  uint32_t depth = 0;
  // We are in our compile unit, parse starting at the offset we were told to
  // parse
  const DWARFDataExtractor &data = GetData();
  std::vector<uint32_t> die_index_stack;
  die_index_stack.reserve(32);
  die_index_stack.push_back(0);
  bool prev_die_had_children = false;
  DWARFFormValue::FixedFormSizes fixed_form_sizes =
      DWARFFormValue::GetFixedFormSizesForAddressSize(GetAddressByteSize(),
                                                      IsDWARF64());
  while (offset < next_cu_offset &&
         die.FastExtract(data, this, fixed_form_sizes, &offset)) {
    const bool null_die = die.IsNULL();
    if (depth == 0) {
      assert(m_die_array.empty() && "Compile unit DIE already added");

      // The average bytes per DIE entry has been seen to be around 14-20 so
      // lets pre-reserve half of that since we are now stripping the NULL
      // tags.

      // Only reserve the memory if we are adding children of the main
      // compile unit DIE. The compile unit DIE is always the first entry, so
      // if our size is 1, then we are adding the first compile unit child
      // DIE and should reserve the memory.
      m_die_array.reserve(GetDebugInfoSize() / 24);
      m_die_array.push_back(die);

      if (!m_first_die)
        AddUnitDIE(m_die_array.front());
    } else {
      if (null_die) {
        if (prev_die_had_children) {
          // This will only happen if a DIE says is has children but all it
          // contains is a NULL tag. Since we are removing the NULL DIEs from
          // the list (saves up to 25% in C++ code), we need a way to let the
          // DIE know that it actually doesn't have children.
          if (!m_die_array.empty())
            m_die_array.back().SetHasChildren(false);
        }
      } else {
        die.SetParentIndex(m_die_array.size() - die_index_stack[depth - 1]);

        if (die_index_stack.back())
          m_die_array[die_index_stack.back()].SetSiblingIndex(
              m_die_array.size() - die_index_stack.back());

        // Only push the DIE if it isn't a NULL DIE
        m_die_array.push_back(die);
      }
    }

    if (null_die) {
      // NULL DIE.
      if (!die_index_stack.empty())
        die_index_stack.pop_back();

      if (depth > 0)
        --depth;
      prev_die_had_children = false;
    } else {
      die_index_stack.back() = m_die_array.size() - 1;
      // Normal DIE
      const bool die_has_children = die.HasChildren();
      if (die_has_children) {
        die_index_stack.push_back(0);
        ++depth;
      }
      prev_die_had_children = die_has_children;
    }

    if (depth == 0)
      break; // We are done with this compile unit!
  }

  if (!m_die_array.empty()) {
    if (m_first_die) {
      // Only needed for the assertion.
      m_first_die.SetHasChildren(m_die_array.front().HasChildren());
      lldbassert(m_first_die == m_die_array.front());
    }
    m_first_die = m_die_array.front();
  }

  m_die_array.shrink_to_fit();

  ExtractDIEsEndCheck(offset);

  if (m_dwo_symbol_file) {
    DWARFUnit *dwo_cu = m_dwo_symbol_file->GetCompileUnit();
    dwo_cu->ExtractDIEsIfNeeded();
  }
}

//--------------------------------------------------------------------------
// Final checks for both ExtractUnitDIEIfNeeded() and ExtractDIEsIfNeeded().
//--------------------------------------------------------------------------
void DWARFUnit::ExtractDIEsEndCheck(lldb::offset_t offset) const {
  // Give a little bit of info if we encounter corrupt DWARF (our offset should
  // always terminate at or before the start of the next compilation unit
  // header).
  if (offset > GetNextCompileUnitOffset()) {
    m_dwarf->GetObjectFile()->GetModule()->ReportWarning(
        "DWARF compile unit extends beyond its bounds cu 0x%8.8x at "
        "0x%8.8" PRIx64 "\n",
        GetOffset(), offset);
  }

  Log *log(LogChannelDWARF::GetLogIfAll(DWARF_LOG_DEBUG_INFO));
  if (log && log->GetVerbose()) {
    StreamString strm;
    Dump(&strm);
    if (m_die_array.empty())
      strm.Printf("error: no DIE for compile unit");
    else
      m_die_array[0].Dump(m_dwarf, this, strm, UINT32_MAX);
    log->PutString(strm.GetString());
  }
}

// This is used when a split dwarf is enabled.
// A skeleton compilation unit may contain the DW_AT_str_offsets_base attribute
// that points to the first string offset of the CU contribution to the
// .debug_str_offsets. At the same time, the corresponding split debug unit also
// may use DW_FORM_strx* forms pointing to its own .debug_str_offsets.dwo and
// for that case, we should find the offset (skip the section header).
static void SetDwoStrOffsetsBase(DWARFUnit *dwo_cu) {
  lldb::offset_t baseOffset = 0;

  const DWARFDataExtractor &strOffsets =
      dwo_cu->GetSymbolFileDWARF()->get_debug_str_offsets_data();
  uint64_t length = strOffsets.GetU32(&baseOffset);
  if (length == 0xffffffff)
    length = strOffsets.GetU64(&baseOffset);

  // Check version.
  if (strOffsets.GetU16(&baseOffset) < 5)
    return;

  // Skip padding.
  baseOffset += 2;

  dwo_cu->SetStrOffsetsBase(baseOffset);
}

// m_die_array_mutex must be already held as read/write.
void DWARFUnit::AddUnitDIE(const DWARFDebugInfoEntry &cu_die) {
  dw_addr_t addr_base = cu_die.GetAttributeValueAsUnsigned(
      m_dwarf, this, DW_AT_addr_base, LLDB_INVALID_ADDRESS);
  if (addr_base != LLDB_INVALID_ADDRESS)
    SetAddrBase(addr_base);

  dw_addr_t ranges_base = cu_die.GetAttributeValueAsUnsigned(
      m_dwarf, this, DW_AT_rnglists_base, LLDB_INVALID_ADDRESS);
  if (ranges_base != LLDB_INVALID_ADDRESS)
    SetRangesBase(ranges_base);

  SetStrOffsetsBase(cu_die.GetAttributeValueAsUnsigned(
      m_dwarf, this, DW_AT_str_offsets_base, 0));

  uint64_t base_addr = cu_die.GetAttributeValueAsAddress(
      m_dwarf, this, DW_AT_low_pc, LLDB_INVALID_ADDRESS);
  if (base_addr == LLDB_INVALID_ADDRESS)
    base_addr = cu_die.GetAttributeValueAsAddress(
        m_dwarf, this, DW_AT_entry_pc, 0);
  SetBaseAddress(base_addr);

  std::unique_ptr<SymbolFileDWARFDwo> dwo_symbol_file =
      m_dwarf->GetDwoSymbolFileForCompileUnit(*this, cu_die);
  if (!dwo_symbol_file)
    return;

  DWARFUnit *dwo_cu = dwo_symbol_file->GetCompileUnit();
  if (!dwo_cu)
    return; // Can't fetch the compile unit from the dwo file.

  DWARFBaseDIE dwo_cu_die = dwo_cu->GetUnitDIEOnly();
  if (!dwo_cu_die.IsValid())
    return; // Can't fetch the compile unit DIE from the dwo file.

  uint64_t main_dwo_id =
      cu_die.GetAttributeValueAsUnsigned(m_dwarf, this, DW_AT_GNU_dwo_id, 0);
  uint64_t sub_dwo_id =
      dwo_cu_die.GetAttributeValueAsUnsigned(DW_AT_GNU_dwo_id, 0);
  if (main_dwo_id != sub_dwo_id)
    return; // The 2 dwo ID isn't match. Don't use the dwo file as it belongs to
  // a differectn compilation.

  m_dwo_symbol_file = std::move(dwo_symbol_file);

  // Here for DWO CU we want to use the address base set in the skeleton unit
  // (DW_AT_addr_base) if it is available and use the DW_AT_GNU_addr_base
  // otherwise. We do that because pre-DWARF v5 could use the DW_AT_GNU_*
  // attributes which were applicable to the DWO units. The corresponding
  // DW_AT_* attributes standardized in DWARF v5 are also applicable to the main
  // unit in contrast.
  if (addr_base == LLDB_INVALID_ADDRESS)
    addr_base = cu_die.GetAttributeValueAsUnsigned(m_dwarf, this,
                                                   DW_AT_GNU_addr_base, 0);
  dwo_cu->SetAddrBase(addr_base);

  if (ranges_base == LLDB_INVALID_ADDRESS)
    ranges_base = cu_die.GetAttributeValueAsUnsigned(m_dwarf, this,
                                                     DW_AT_GNU_ranges_base, 0);
  dwo_cu->SetRangesBase(ranges_base);

  dwo_cu->SetBaseObjOffset(m_offset);

  SetDwoStrOffsetsBase(dwo_cu);
}

DWARFDIE DWARFUnit::LookupAddress(const dw_addr_t address) {
  if (DIE()) {
    const DWARFDebugAranges &func_aranges = GetFunctionAranges();

    // Re-check the aranges auto pointer contents in case it was created above
    if (!func_aranges.IsEmpty())
      return GetDIE(func_aranges.FindAddress(address));
  }
  return DWARFDIE();
}

size_t DWARFUnit::AppendDIEsWithTag(const dw_tag_t tag,
				    DWARFDIECollection &dies,
				    uint32_t depth) const {
  size_t old_size = dies.Size();
  {
    llvm::sys::ScopedReader lock(m_die_array_mutex);
    DWARFDebugInfoEntry::const_iterator pos;
    DWARFDebugInfoEntry::const_iterator end = m_die_array.end();
    for (pos = m_die_array.begin(); pos != end; ++pos) {
      if (pos->Tag() == tag)
        dies.Append(DWARFDIE(this, &(*pos)));
    }
  }

  // Return the number of DIEs added to the collection
  return dies.Size() - old_size;
}


lldb::user_id_t DWARFUnit::GetID() const {
  dw_offset_t local_id =
      m_base_obj_offset != DW_INVALID_OFFSET ? m_base_obj_offset : m_offset;
  if (m_dwarf)
    return DIERef(local_id, local_id).GetUID(m_dwarf);
  else
    return local_id;
}

dw_offset_t DWARFUnit::GetNextCompileUnitOffset() const {
  return m_offset + GetLengthByteSize() + GetLength();
}

size_t DWARFUnit::GetDebugInfoSize() const {
  return GetLengthByteSize() + GetLength() - GetHeaderByteSize();
}

const DWARFAbbreviationDeclarationSet *DWARFUnit::GetAbbreviations() const {
  return m_abbrevs;
}

dw_offset_t DWARFUnit::GetAbbrevOffset() const {
  return m_abbrevs ? m_abbrevs->GetOffset() : DW_INVALID_OFFSET;
}

void DWARFUnit::SetAddrBase(dw_addr_t addr_base) { m_addr_base = addr_base; }

void DWARFUnit::SetRangesBase(dw_addr_t ranges_base) {
  m_ranges_base = ranges_base;
}

void DWARFUnit::SetBaseObjOffset(dw_offset_t base_obj_offset) {
  m_base_obj_offset = base_obj_offset;
}

void DWARFUnit::SetStrOffsetsBase(dw_offset_t str_offsets_base) {
  m_str_offsets_base = str_offsets_base;
}

// It may be called only with m_die_array_mutex held R/W.
void DWARFUnit::ClearDIEsRWLocked() {
  m_die_array.clear();
  m_die_array.shrink_to_fit();

  if (m_dwo_symbol_file)
    m_dwo_symbol_file->GetCompileUnit()->ClearDIEsRWLocked();
}

void DWARFUnit::BuildAddressRangeTable(SymbolFileDWARF *dwarf,
                                       DWARFDebugAranges *debug_aranges) {
  // This function is usually called if there in no .debug_aranges section in
  // order to produce a compile unit level set of address ranges that is
  // accurate.

  size_t num_debug_aranges = debug_aranges->GetNumRanges();

  // First get the compile unit DIE only and check if it has a DW_AT_ranges
  const DWARFDebugInfoEntry *die = GetUnitDIEPtrOnly();

  const dw_offset_t cu_offset = GetOffset();
  if (die) {
    DWARFRangeList ranges;
    const size_t num_ranges =
        die->GetAttributeAddressRanges(dwarf, this, ranges, false);
    if (num_ranges > 0) {
      // This compile unit has DW_AT_ranges, assume this is correct if it is
      // present since clang no longer makes .debug_aranges by default and it
      // emits DW_AT_ranges for DW_TAG_compile_units. GCC also does this with
      // recent GCC builds.
      for (size_t i = 0; i < num_ranges; ++i) {
        const DWARFRangeList::Entry &range = ranges.GetEntryRef(i);
        debug_aranges->AppendRange(cu_offset, range.GetRangeBase(),
                                   range.GetRangeEnd());
      }

      return; // We got all of our ranges from the DW_AT_ranges attribute
    }
  }
  // We don't have a DW_AT_ranges attribute, so we need to parse the DWARF

  // If the DIEs weren't parsed, then we don't want all dies for all compile
  // units to stay loaded when they weren't needed. So we can end up parsing
  // the DWARF and then throwing them all away to keep memory usage down.
  ScopedExtractDIEs clear_dies(ExtractDIEsScoped());

  die = DIEPtr();
  if (die)
    die->BuildAddressRangeTable(dwarf, this, debug_aranges);

  if (debug_aranges->GetNumRanges() == num_debug_aranges) {
    // We got nothing from the functions, maybe we have a line tables only
    // situation. Check the line tables and build the arange table from this.
    SymbolContext sc;
    sc.comp_unit = dwarf->GetCompUnitForDWARFCompUnit(this);
    if (sc.comp_unit) {
      SymbolFileDWARFDebugMap *debug_map_sym_file =
          m_dwarf->GetDebugMapSymfile();
      if (debug_map_sym_file == NULL) {
        LineTable *line_table = sc.comp_unit->GetLineTable();

        if (line_table) {
          LineTable::FileAddressRanges file_ranges;
          const bool append = true;
          const size_t num_ranges =
              line_table->GetContiguousFileAddressRanges(file_ranges, append);
          for (uint32_t idx = 0; idx < num_ranges; ++idx) {
            const LineTable::FileAddressRanges::Entry &range =
                file_ranges.GetEntryRef(idx);
            debug_aranges->AppendRange(cu_offset, range.GetRangeBase(),
                                       range.GetRangeEnd());
          }
        }
      } else
        debug_map_sym_file->AddOSOARanges(dwarf, debug_aranges);
    }
  }

  if (debug_aranges->GetNumRanges() == num_debug_aranges) {
    // We got nothing from the functions, maybe we have a line tables only
    // situation. Check the line tables and build the arange table from this.
    SymbolContext sc;
    sc.comp_unit = dwarf->GetCompUnitForDWARFCompUnit(this);
    if (sc.comp_unit) {
      LineTable *line_table = sc.comp_unit->GetLineTable();

      if (line_table) {
        LineTable::FileAddressRanges file_ranges;
        const bool append = true;
        const size_t num_ranges =
            line_table->GetContiguousFileAddressRanges(file_ranges, append);
        for (uint32_t idx = 0; idx < num_ranges; ++idx) {
          const LineTable::FileAddressRanges::Entry &range =
              file_ranges.GetEntryRef(idx);
          debug_aranges->AppendRange(GetOffset(), range.GetRangeBase(),
                                     range.GetRangeEnd());
        }
      }
    }
  }
}

lldb::ByteOrder DWARFUnit::GetByteOrder() const {
  return m_dwarf->GetObjectFile()->GetByteOrder();
}

TypeSystem *DWARFUnit::GetTypeSystem() {
  if (m_dwarf)
    return m_dwarf->GetTypeSystemForLanguage(GetLanguageType());
  else
    return nullptr;
}

DWARFFormValue::FixedFormSizes DWARFUnit::GetFixedFormSizes() {
  return DWARFFormValue::GetFixedFormSizesForAddressSize(GetAddressByteSize(),
                                                         IsDWARF64());
}

void DWARFUnit::SetBaseAddress(dw_addr_t base_addr) { m_base_addr = base_addr; }

//----------------------------------------------------------------------
// Compare function DWARFDebugAranges::Range structures
//----------------------------------------------------------------------
static bool CompareDIEOffset(const DWARFDebugInfoEntry &die,
                             const dw_offset_t die_offset) {
  return die.GetOffset() < die_offset;
}

//----------------------------------------------------------------------
// GetDIE()
//
// Get the DIE (Debug Information Entry) with the specified offset by first
// checking if the DIE is contained within this compile unit and grabbing the
// DIE from this compile unit. Otherwise we grab the DIE from the DWARF file.
//----------------------------------------------------------------------
DWARFDIE
DWARFUnit::GetDIE(dw_offset_t die_offset) {
  if (die_offset != DW_INVALID_OFFSET) {
    if (GetDwoSymbolFile())
      return GetDwoSymbolFile()->GetCompileUnit()->GetDIE(die_offset);

    if (ContainsDIEOffset(die_offset)) {
      ExtractDIEsIfNeeded();
      DWARFDebugInfoEntry::const_iterator end = m_die_array.cend();
      DWARFDebugInfoEntry::const_iterator pos =
          lower_bound(m_die_array.cbegin(), end, die_offset, CompareDIEOffset);
      if (pos != end) {
        if (die_offset == (*pos).GetOffset())
          return DWARFDIE(this, &(*pos));
      }
    } else {
      // Don't specify the compile unit offset as we don't know it because the
      // DIE belongs to
      // a different compile unit in the same symbol file.
      return m_dwarf->DebugInfo()->GetDIEForDIEOffset(die_offset);
    }
  }
  return DWARFDIE(); // Not found
}

uint8_t DWARFUnit::GetAddressByteSize(const DWARFUnit *cu) {
  if (cu)
    return cu->GetAddressByteSize();
  return DWARFUnit::GetDefaultAddressSize();
}

bool DWARFUnit::IsDWARF64(const DWARFUnit *cu) {
  if (cu)
    return cu->IsDWARF64();
  return false;
}

uint8_t DWARFUnit::GetDefaultAddressSize() { return 4; }

void *DWARFUnit::GetUserData() const { return m_user_data; }

void DWARFUnit::SetUserData(void *d) {
  m_user_data = d;
  if (m_dwo_symbol_file)
    m_dwo_symbol_file->GetCompileUnit()->SetUserData(d);
}

bool DWARFUnit::Supports_DW_AT_APPLE_objc_complete_type() {
  return GetProducer() != eProducerLLVMGCC;
}

bool DWARFUnit::DW_AT_decl_file_attributes_are_invalid() {
  // llvm-gcc makes completely invalid decl file attributes and won't ever be
  // fixed, so we need to know to ignore these.
  return GetProducer() == eProducerLLVMGCC;
}

bool DWARFUnit::Supports_unnamed_objc_bitfields() {
  if (GetProducer() == eProducerClang) {
    const uint32_t major_version = GetProducerVersionMajor();
    return major_version > 425 ||
           (major_version == 425 && GetProducerVersionUpdate() >= 13);
  }
  return true; // Assume all other compilers didn't have incorrect ObjC bitfield
               // info
}

SymbolFileDWARF *DWARFUnit::GetSymbolFileDWARF() const { return m_dwarf; }

void DWARFUnit::ParseProducerInfo() {
  m_producer_version_major = UINT32_MAX;
  m_producer_version_minor = UINT32_MAX;
  m_producer_version_update = UINT32_MAX;

  const DWARFDebugInfoEntry *die = GetUnitDIEPtrOnly();
  if (die) {

    const char *producer_cstr =
        die->GetAttributeValueAsString(m_dwarf, this, DW_AT_producer, NULL);
    if (producer_cstr) {
      RegularExpression llvm_gcc_regex(
          llvm::StringRef("^4\\.[012]\\.[01] \\(Based on Apple "
                          "Inc\\. build [0-9]+\\) \\(LLVM build "
                          "[\\.0-9]+\\)$"));
      if (llvm_gcc_regex.Execute(llvm::StringRef(producer_cstr))) {
        m_producer = eProducerLLVMGCC;
      } else if (strstr(producer_cstr, "clang")) {
        static RegularExpression g_clang_version_regex(
            llvm::StringRef("clang-([0-9]+)\\.([0-9]+)\\.([0-9]+)"));
        RegularExpression::Match regex_match(3);
        if (g_clang_version_regex.Execute(llvm::StringRef(producer_cstr),
                                          &regex_match)) {
          std::string str;
          if (regex_match.GetMatchAtIndex(producer_cstr, 1, str))
            m_producer_version_major =
                StringConvert::ToUInt32(str.c_str(), UINT32_MAX, 10);
          if (regex_match.GetMatchAtIndex(producer_cstr, 2, str))
            m_producer_version_minor =
                StringConvert::ToUInt32(str.c_str(), UINT32_MAX, 10);
          if (regex_match.GetMatchAtIndex(producer_cstr, 3, str))
            m_producer_version_update =
                StringConvert::ToUInt32(str.c_str(), UINT32_MAX, 10);
        }
        m_producer = eProducerClang;
      } else if (strstr(producer_cstr, "GNU"))
        m_producer = eProducerGCC;
    }
  }
  if (m_producer == eProducerInvalid)
    m_producer = eProcucerOther;
}

DWARFProducer DWARFUnit::GetProducer() {
  if (m_producer == eProducerInvalid)
    ParseProducerInfo();
  return m_producer;
}

uint32_t DWARFUnit::GetProducerVersionMajor() {
  if (m_producer_version_major == 0)
    ParseProducerInfo();
  return m_producer_version_major;
}

uint32_t DWARFUnit::GetProducerVersionMinor() {
  if (m_producer_version_minor == 0)
    ParseProducerInfo();
  return m_producer_version_minor;
}

uint32_t DWARFUnit::GetProducerVersionUpdate() {
  if (m_producer_version_update == 0)
    ParseProducerInfo();
  return m_producer_version_update;
}
LanguageType DWARFUnit::LanguageTypeFromDWARF(uint64_t val) {
  // Note: user languages between lo_user and hi_user must be handled
  // explicitly here.
  switch (val) {
  case DW_LANG_Mips_Assembler:
    return eLanguageTypeMipsAssembler;
  case DW_LANG_GOOGLE_RenderScript:
    return eLanguageTypeExtRenderScript;
  default:
    return static_cast<LanguageType>(val);
  }
}

LanguageType DWARFUnit::GetLanguageType() {
  if (m_language_type != eLanguageTypeUnknown)
    return m_language_type;

  const DWARFDebugInfoEntry *die = GetUnitDIEPtrOnly();
  if (die)
    m_language_type = LanguageTypeFromDWARF(
        die->GetAttributeValueAsUnsigned(m_dwarf, this, DW_AT_language, 0));
  return m_language_type;
}

bool DWARFUnit::GetIsOptimized() {
  if (m_is_optimized == eLazyBoolCalculate) {
    const DWARFDebugInfoEntry *die = GetUnitDIEPtrOnly();
    if (die) {
      m_is_optimized = eLazyBoolNo;
      if (die->GetAttributeValueAsUnsigned(m_dwarf, this, DW_AT_APPLE_optimized,
                                           0) == 1) {
        m_is_optimized = eLazyBoolYes;
      }
    }
  }
  return m_is_optimized == eLazyBoolYes;
}

SymbolFileDWARFDwo *DWARFUnit::GetDwoSymbolFile() const {
  return m_dwo_symbol_file.get();
}

dw_offset_t DWARFUnit::GetBaseObjOffset() const { return m_base_obj_offset; }

const DWARFDebugAranges &DWARFUnit::GetFunctionAranges() {
  if (m_func_aranges_ap.get() == NULL) {
    m_func_aranges_ap.reset(new DWARFDebugAranges());
    Log *log(LogChannelDWARF::GetLogIfAll(DWARF_LOG_DEBUG_ARANGES));

    if (log) {
      m_dwarf->GetObjectFile()->GetModule()->LogMessage(
          log,
          "DWARFUnit::GetFunctionAranges() for compile unit at "
          ".debug_info[0x%8.8x]",
          GetOffset());
    }
    const DWARFDebugInfoEntry *die = DIEPtr();
    if (die)
      die->BuildFunctionAddressRangeTable(m_dwarf, this,
                                          m_func_aranges_ap.get());

    if (m_dwo_symbol_file) {
      DWARFUnit *dwo_cu = m_dwo_symbol_file->GetCompileUnit();
      const DWARFDebugInfoEntry *dwo_die = dwo_cu->DIEPtr();
      if (dwo_die)
        dwo_die->BuildFunctionAddressRangeTable(m_dwo_symbol_file.get(), dwo_cu,
                                                m_func_aranges_ap.get());
    }

    const bool minimize = false;
    m_func_aranges_ap->Sort(minimize);
  }
  return *m_func_aranges_ap.get();
}

