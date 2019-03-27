//===-- DWARFDebugMacinfoEntry.cpp ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "DWARFDebugMacinfoEntry.h"

#include "lldb/Utility/Stream.h"

using namespace lldb_private;
using namespace std;

DWARFDebugMacinfoEntry::DWARFDebugMacinfoEntry()
    : m_type_code(0), m_line(0), m_op2() {
  m_op2.cstr = NULL;
}

DWARFDebugMacinfoEntry::~DWARFDebugMacinfoEntry() {}

const char *DWARFDebugMacinfoEntry::GetCString() const {
  switch (m_type_code) {
  case 0:
  case DW_MACINFO_start_file:
  case DW_MACINFO_end_file:
    return NULL;
  default:
    break;
  }
  return m_op2.cstr;
}

void DWARFDebugMacinfoEntry::Dump(Stream *s) const {
  if (m_type_code) {
    s->PutCString(DW_MACINFO_value_to_name(m_type_code));

    switch (m_type_code) {
    case DW_MACINFO_define:
      s->Printf(" line:%u  #define %s\n", (uint32_t)m_line, m_op2.cstr);
      break;

    case DW_MACINFO_undef:
      s->Printf(" line:%u  #undef %s\n", (uint32_t)m_line, m_op2.cstr);
      break;

    default:
      s->Printf(" line:%u  str: '%s'\n", (uint32_t)m_line, m_op2.cstr);
      break;

    case DW_MACINFO_start_file:
      s->Printf(" line:%u  file index: '%u'\n", (uint32_t)m_line,
                (uint32_t)m_op2.file_idx);
      break;

    case DW_MACINFO_end_file:
      break;
    }
  } else {
    s->PutCString(" END\n");
  }
}

bool DWARFDebugMacinfoEntry::Extract(const DWARFDataExtractor &mac_info_data,
                                     lldb::offset_t *offset_ptr) {
  if (mac_info_data.ValidOffset(*offset_ptr)) {
    m_type_code = mac_info_data.GetU8(offset_ptr);

    switch (m_type_code) {

    case DW_MACINFO_define:
    case DW_MACINFO_undef:
      // 2 operands:
      // Arg 1: operand encodes the line number of the source line on which
      //      the relevant defining or undefining pre-processor directives
      //      appeared.
      m_line = mac_info_data.GetULEB128(offset_ptr);
      // Arg 2: define string
      m_op2.cstr = mac_info_data.GetCStr(offset_ptr);
      break;

    case DW_MACINFO_start_file:
      // 2 operands:
      // Op 1: line number of the source line on which the inclusion
      //      pre-processor directive occurred.
      m_line = mac_info_data.GetULEB128(offset_ptr);
      // Op 2: a source file name index to a file number in the statement
      //      information table for the relevant compilation unit.
      m_op2.file_idx = mac_info_data.GetULEB128(offset_ptr);
      break;

    case 0: // End of list
    case DW_MACINFO_end_file:
      // No operands
      m_line = DW_INVALID_OFFSET;
      m_op2.cstr = NULL;
      break;
    default:
      // Vendor specific entries always have a ULEB128 and a string
      m_line = mac_info_data.GetULEB128(offset_ptr);
      m_op2.cstr = mac_info_data.GetCStr(offset_ptr);
      break;
    }
    return true;
  } else
    m_type_code = 0;

  return false;
}
