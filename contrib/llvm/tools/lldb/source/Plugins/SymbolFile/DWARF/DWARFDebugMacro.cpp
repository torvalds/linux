//===-- DWARFDebugMacro.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "DWARFDebugMacro.h"
#include "SymbolFileDWARF.h"

#include "lldb/Symbol/DebugMacros.h"

#include "DWARFDataExtractor.h"

using namespace lldb_private;

DWARFDebugMacroHeader
DWARFDebugMacroHeader::ParseHeader(const DWARFDataExtractor &debug_macro_data,
                                   lldb::offset_t *offset) {
  DWARFDebugMacroHeader header;

  // Skip over the version field in header.
  header.m_version = debug_macro_data.GetU16(offset);

  uint8_t flags = debug_macro_data.GetU8(offset);
  header.m_offset_is_64_bit = (flags & OFFSET_SIZE_MASK) != 0;

  if (flags & DEBUG_LINE_OFFSET_MASK) {
    if (header.m_offset_is_64_bit)
      header.m_debug_line_offset = debug_macro_data.GetU64(offset);
    else
      header.m_debug_line_offset = debug_macro_data.GetU32(offset);
  }

  // Skip over the operands table if it is present.
  if (flags & OPCODE_OPERANDS_TABLE_MASK)
    SkipOperandTable(debug_macro_data, offset);

  return header;
}

void DWARFDebugMacroHeader::SkipOperandTable(
    const DWARFDataExtractor &debug_macro_data, lldb::offset_t *offset) {
  uint8_t entry_count = debug_macro_data.GetU8(offset);
  for (uint8_t i = 0; i < entry_count; i++) {
    // Skip over the opcode number.
    debug_macro_data.GetU8(offset);

    uint64_t operand_count = debug_macro_data.GetULEB128(offset);

    for (uint64_t j = 0; j < operand_count; j++) {
      // Skip over the operand form
      debug_macro_data.GetU8(offset);
    }
  }
}

void DWARFDebugMacroEntry::ReadMacroEntries(
    const DWARFDataExtractor &debug_macro_data,
    const DWARFDataExtractor &debug_str_data, const bool offset_is_64_bit,
    lldb::offset_t *offset, SymbolFileDWARF *sym_file_dwarf,
    DebugMacrosSP &debug_macros_sp) {
  llvm::dwarf::MacroEntryType type =
      static_cast<llvm::dwarf::MacroEntryType>(debug_macro_data.GetU8(offset));
  while (type != 0) {
    lldb::offset_t new_offset = 0, str_offset = 0;
    uint32_t line = 0;
    const char *macro_str = nullptr;
    uint32_t debug_line_file_idx = 0;

    switch (type) {
    case DW_MACRO_define:
    case DW_MACRO_undef:
      line = debug_macro_data.GetULEB128(offset);
      macro_str = debug_macro_data.GetCStr(offset);
      if (type == DW_MACRO_define)
        debug_macros_sp->AddMacroEntry(
            DebugMacroEntry::CreateDefineEntry(line, macro_str));
      else
        debug_macros_sp->AddMacroEntry(
            DebugMacroEntry::CreateUndefEntry(line, macro_str));
      break;
    case DW_MACRO_define_strp:
    case DW_MACRO_undef_strp:
      line = debug_macro_data.GetULEB128(offset);
      if (offset_is_64_bit)
        str_offset = debug_macro_data.GetU64(offset);
      else
        str_offset = debug_macro_data.GetU32(offset);
      macro_str = debug_str_data.GetCStr(&str_offset);
      if (type == DW_MACRO_define_strp)
        debug_macros_sp->AddMacroEntry(
            DebugMacroEntry::CreateDefineEntry(line, macro_str));
      else
        debug_macros_sp->AddMacroEntry(
            DebugMacroEntry::CreateUndefEntry(line, macro_str));
      break;
    case DW_MACRO_start_file:
      line = debug_macro_data.GetULEB128(offset);
      debug_line_file_idx = debug_macro_data.GetULEB128(offset);
      debug_macros_sp->AddMacroEntry(
          DebugMacroEntry::CreateStartFileEntry(line, debug_line_file_idx));
      break;
    case DW_MACRO_end_file:
      // This operation has no operands.
      debug_macros_sp->AddMacroEntry(DebugMacroEntry::CreateEndFileEntry());
      break;
    case DW_MACRO_import:
      if (offset_is_64_bit)
        new_offset = debug_macro_data.GetU64(offset);
      else
        new_offset = debug_macro_data.GetU32(offset);
      debug_macros_sp->AddMacroEntry(DebugMacroEntry::CreateIndirectEntry(
          sym_file_dwarf->ParseDebugMacros(&new_offset)));
      break;
    default:
      // TODO: Add support for other standard operations.
      // TODO: Provide mechanism to hook handling of non-standard/extension
      // operands.
      return;
    }
    type = static_cast<llvm::dwarf::MacroEntryType>(
        debug_macro_data.GetU8(offset));
  }
}
