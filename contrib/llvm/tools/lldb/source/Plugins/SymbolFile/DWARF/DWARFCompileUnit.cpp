//===-- DWARFCompileUnit.cpp ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "DWARFCompileUnit.h"

#include "SymbolFileDWARF.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

extern int g_verbose;

DWARFCompileUnit::DWARFCompileUnit(SymbolFileDWARF *dwarf2Data)
    : DWARFUnit(dwarf2Data) {}

DWARFUnitSP DWARFCompileUnit::Extract(SymbolFileDWARF *dwarf2Data,
                                      const DWARFDataExtractor &debug_info,
                                      lldb::offset_t *offset_ptr) {
  // std::make_shared would require the ctor to be public.
  std::shared_ptr<DWARFCompileUnit> cu_sp(new DWARFCompileUnit(dwarf2Data));

  cu_sp->m_offset = *offset_ptr;

  if (debug_info.ValidOffset(*offset_ptr)) {
    dw_offset_t abbr_offset;
    const DWARFDebugAbbrev *abbr = dwarf2Data->DebugAbbrev();
    cu_sp->m_length = debug_info.GetDWARFInitialLength(offset_ptr);
    cu_sp->m_is_dwarf64 = debug_info.IsDWARF64();
    cu_sp->m_version = debug_info.GetU16(offset_ptr);

    if (cu_sp->m_version == 5) {
      cu_sp->m_unit_type = debug_info.GetU8(offset_ptr);
      cu_sp->m_addr_size = debug_info.GetU8(offset_ptr);
      abbr_offset = debug_info.GetDWARFOffset(offset_ptr);

      if (cu_sp->m_unit_type == llvm::dwarf::DW_UT_skeleton)
        cu_sp->m_dwo_id = debug_info.GetU64(offset_ptr);
    } else {
      abbr_offset = debug_info.GetDWARFOffset(offset_ptr);
      cu_sp->m_addr_size = debug_info.GetU8(offset_ptr);
    }

    bool length_OK =
        debug_info.ValidOffset(cu_sp->GetNextCompileUnitOffset() - 1);
    bool version_OK = SymbolFileDWARF::SupportedVersion(cu_sp->m_version);
    bool abbr_offset_OK =
        dwarf2Data->get_debug_abbrev_data().ValidOffset(abbr_offset);
    bool addr_size_OK = (cu_sp->m_addr_size == 4) || (cu_sp->m_addr_size == 8);

    if (length_OK && version_OK && addr_size_OK && abbr_offset_OK &&
        abbr != NULL) {
      cu_sp->m_abbrevs = abbr->GetAbbreviationDeclarationSet(abbr_offset);
      return cu_sp;
    }

    // reset the offset to where we tried to parse from if anything went wrong
    *offset_ptr = cu_sp->m_offset;
  }

  return nullptr;
}

void DWARFCompileUnit::Dump(Stream *s) const {
  s->Printf("0x%8.8x: Compile Unit: length = 0x%8.8x, version = 0x%4.4x, "
            "abbr_offset = 0x%8.8x, addr_size = 0x%2.2x (next CU at "
            "{0x%8.8x})\n",
            m_offset, m_length, m_version, GetAbbrevOffset(), m_addr_size,
            GetNextCompileUnitOffset());
}

uint32_t DWARFCompileUnit::GetHeaderByteSize() const {
  if (m_version < 5)
    return m_is_dwarf64 ? 23 : 11;

  switch (m_unit_type) {
  case llvm::dwarf::DW_UT_compile:
  case llvm::dwarf::DW_UT_partial:
    return 12;
  case llvm::dwarf::DW_UT_skeleton:
  case llvm::dwarf::DW_UT_split_compile:
    return 20;
  case llvm::dwarf::DW_UT_type:
  case llvm::dwarf::DW_UT_split_type:
    return 24;
  }
  llvm_unreachable("invalid UnitType.");
}

const lldb_private::DWARFDataExtractor &DWARFCompileUnit::GetData() const {
  return m_dwarf->get_debug_info_data();
}
