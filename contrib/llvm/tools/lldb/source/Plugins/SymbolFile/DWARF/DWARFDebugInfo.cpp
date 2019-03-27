//===-- DWARFDebugInfo.cpp --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "SymbolFileDWARF.h"

#include <algorithm>
#include <set>

#include "lldb/Host/PosixApi.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/Utility/Stream.h"

#include "DWARFCompileUnit.h"
#include "DWARFDebugAranges.h"
#include "DWARFDebugAranges.h"
#include "DWARFDebugInfo.h"
#include "DWARFDebugInfoEntry.h"
#include "DWARFFormValue.h"
#include "LogChannelDWARF.h"

using namespace lldb;
using namespace lldb_private;
using namespace std;

//----------------------------------------------------------------------
// Constructor
//----------------------------------------------------------------------
DWARFDebugInfo::DWARFDebugInfo()
    : m_dwarf2Data(NULL), m_compile_units(), m_cu_aranges_ap() {}

//----------------------------------------------------------------------
// SetDwarfData
//----------------------------------------------------------------------
void DWARFDebugInfo::SetDwarfData(SymbolFileDWARF *dwarf2Data) {
  m_dwarf2Data = dwarf2Data;
  m_compile_units.clear();
}

DWARFDebugAranges &DWARFDebugInfo::GetCompileUnitAranges() {
  if (m_cu_aranges_ap.get() == NULL && m_dwarf2Data) {
    Log *log(LogChannelDWARF::GetLogIfAll(DWARF_LOG_DEBUG_ARANGES));

    m_cu_aranges_ap.reset(new DWARFDebugAranges());
    const DWARFDataExtractor &debug_aranges_data =
        m_dwarf2Data->get_debug_aranges_data();
    if (debug_aranges_data.GetByteSize() > 0) {
      if (log)
        log->Printf(
            "DWARFDebugInfo::GetCompileUnitAranges() for \"%s\" from "
            ".debug_aranges",
            m_dwarf2Data->GetObjectFile()->GetFileSpec().GetPath().c_str());
      m_cu_aranges_ap->Extract(debug_aranges_data);
    }

    // Make a list of all CUs represented by the arange data in the file.
    std::set<dw_offset_t> cus_with_data;
    for (size_t n = 0; n < m_cu_aranges_ap.get()->GetNumRanges(); n++) {
      dw_offset_t offset = m_cu_aranges_ap.get()->OffsetAtIndex(n);
      if (offset != DW_INVALID_OFFSET)
        cus_with_data.insert(offset);
    }

    // Manually build arange data for everything that wasn't in the
    // .debug_aranges table.
    bool printed = false;
    const size_t num_compile_units = GetNumCompileUnits();
    for (size_t idx = 0; idx < num_compile_units; ++idx) {
      DWARFUnit *cu = GetCompileUnitAtIndex(idx);

      dw_offset_t offset = cu->GetOffset();
      if (cus_with_data.find(offset) == cus_with_data.end()) {
        if (log) {
          if (!printed)
            log->Printf(
                "DWARFDebugInfo::GetCompileUnitAranges() for \"%s\" by parsing",
                m_dwarf2Data->GetObjectFile()->GetFileSpec().GetPath().c_str());
          printed = true;
        }
        cu->BuildAddressRangeTable(m_dwarf2Data, m_cu_aranges_ap.get());
      }
    }

    const bool minimize = true;
    m_cu_aranges_ap->Sort(minimize);
  }
  return *m_cu_aranges_ap.get();
}

void DWARFDebugInfo::ParseCompileUnitHeadersIfNeeded() {
  if (m_compile_units.empty()) {
    if (m_dwarf2Data != NULL) {
      lldb::offset_t offset = 0;
      DWARFUnitSP cu_sp;
      const auto &debug_info_data = m_dwarf2Data->get_debug_info_data();
      while ((cu_sp = DWARFCompileUnit::Extract(m_dwarf2Data, debug_info_data,
                                                &offset))) {
        m_compile_units.push_back(cu_sp);

        offset = cu_sp->GetNextCompileUnitOffset();
      }
    }
  }
}

size_t DWARFDebugInfo::GetNumCompileUnits() {
  ParseCompileUnitHeadersIfNeeded();
  return m_compile_units.size();
}

DWARFUnit *DWARFDebugInfo::GetCompileUnitAtIndex(uint32_t idx) {
  DWARFUnit *cu = NULL;
  if (idx < GetNumCompileUnits())
    cu = m_compile_units[idx].get();
  return cu;
}

bool DWARFDebugInfo::ContainsCompileUnit(const DWARFUnit *cu) const {
  // Not a verify efficient function, but it is handy for use in assertions to
  // make sure that a compile unit comes from a debug information file.
  CompileUnitColl::const_iterator end_pos = m_compile_units.end();
  CompileUnitColl::const_iterator pos;

  for (pos = m_compile_units.begin(); pos != end_pos; ++pos) {
    if (pos->get() == cu)
      return true;
  }
  return false;
}

bool DWARFDebugInfo::OffsetLessThanCompileUnitOffset(
    dw_offset_t offset, const DWARFUnitSP &cu_sp) {
  return offset < cu_sp->GetOffset();
}

DWARFUnit *DWARFDebugInfo::GetCompileUnit(dw_offset_t cu_offset,
                                                 uint32_t *idx_ptr) {
  DWARFUnitSP cu_sp;
  uint32_t cu_idx = DW_INVALID_INDEX;
  if (cu_offset != DW_INVALID_OFFSET) {
    ParseCompileUnitHeadersIfNeeded();

    // Watch out for single compile unit executable as they are pretty common
    const size_t num_cus = m_compile_units.size();
    if (num_cus == 1) {
      if (m_compile_units[0]->GetOffset() == cu_offset) {
        cu_sp = m_compile_units[0];
        cu_idx = 0;
      }
    } else if (num_cus) {
      CompileUnitColl::const_iterator end_pos = m_compile_units.end();
      CompileUnitColl::const_iterator begin_pos = m_compile_units.begin();
      CompileUnitColl::const_iterator pos = std::upper_bound(
          begin_pos, end_pos, cu_offset, OffsetLessThanCompileUnitOffset);
      if (pos != begin_pos) {
        --pos;
        if ((*pos)->GetOffset() == cu_offset) {
          cu_sp = *pos;
          cu_idx = std::distance(begin_pos, pos);
        }
      }
    }
  }
  if (idx_ptr)
    *idx_ptr = cu_idx;
  return cu_sp.get();
}

DWARFUnit *DWARFDebugInfo::GetCompileUnit(const DIERef &die_ref) {
  if (die_ref.cu_offset == DW_INVALID_OFFSET)
    return GetCompileUnitContainingDIEOffset(die_ref.die_offset);
  else
    return GetCompileUnit(die_ref.cu_offset);
}

DWARFUnit *
DWARFDebugInfo::GetCompileUnitContainingDIEOffset(dw_offset_t die_offset) {
  ParseCompileUnitHeadersIfNeeded();

  DWARFUnitSP cu_sp;

  // Watch out for single compile unit executable as they are pretty common
  const size_t num_cus = m_compile_units.size();
  if (num_cus == 1) {
    if (m_compile_units[0]->ContainsDIEOffset(die_offset))
      return m_compile_units[0].get();
  } else if (num_cus) {
    CompileUnitColl::const_iterator end_pos = m_compile_units.end();
    CompileUnitColl::const_iterator begin_pos = m_compile_units.begin();
    CompileUnitColl::const_iterator pos = std::upper_bound(
        begin_pos, end_pos, die_offset, OffsetLessThanCompileUnitOffset);
    if (pos != begin_pos) {
      --pos;
      if ((*pos)->ContainsDIEOffset(die_offset))
        return (*pos).get();
    }
  }

  return nullptr;
}

DWARFDIE
DWARFDebugInfo::GetDIEForDIEOffset(dw_offset_t die_offset) {
  DWARFUnit *cu = GetCompileUnitContainingDIEOffset(die_offset);
  if (cu)
    return cu->GetDIE(die_offset);
  return DWARFDIE();
}

//----------------------------------------------------------------------
// GetDIE()
//
// Get the DIE (Debug Information Entry) with the specified offset.
//----------------------------------------------------------------------
DWARFDIE
DWARFDebugInfo::GetDIE(const DIERef &die_ref) {
  DWARFUnit *cu = GetCompileUnit(die_ref);
  if (cu)
    return cu->GetDIE(die_ref.die_offset);
  return DWARFDIE(); // Not found
}

