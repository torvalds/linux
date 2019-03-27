//===-- DWARFDebugMacinfoEntry.h --------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef SymbolFileDWARF_DWARFDebugMacinfoEntry_h_
#define SymbolFileDWARF_DWARFDebugMacinfoEntry_h_

#include "SymbolFileDWARF.h"

class DWARFDebugMacinfoEntry {
public:
  DWARFDebugMacinfoEntry();

  ~DWARFDebugMacinfoEntry();

  uint8_t TypeCode() const { return m_type_code; }

  uint8_t GetLineNumber() const { return m_line; }

  void Dump(lldb_private::Stream *s) const;

  const char *GetCString() const;

  bool Extract(const lldb_private::DWARFDataExtractor &mac_info_data,
               lldb::offset_t *offset_ptr);

protected:
private:
  uint8_t m_type_code;
  dw_uleb128_t m_line;
  union {
    dw_uleb128_t file_idx;
    const char *cstr;
  } m_op2;
};

#endif // SymbolFileDWARF_DWARFDebugMacinfoEntry_h_
