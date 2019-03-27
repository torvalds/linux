//===-- DWARFDebugMacinfo.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef SymbolFileDWARF_DWARFDebugMacinfo_h_
#define SymbolFileDWARF_DWARFDebugMacinfo_h_

#include "SymbolFileDWARF.h"

class DWARFDebugMacinfo {
public:
  DWARFDebugMacinfo();

  ~DWARFDebugMacinfo();

  static void Dump(lldb_private::Stream *s,
                   const lldb_private::DWARFDataExtractor &macinfo_data,
                   lldb::offset_t offset = LLDB_INVALID_OFFSET);
};

#endif // SymbolFileDWARF_DWARFDebugMacinfo_h_
