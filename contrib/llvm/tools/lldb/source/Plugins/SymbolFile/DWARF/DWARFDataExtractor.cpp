//===-- DWARFDataExtractor.cpp ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "DWARFDataExtractor.h"

namespace lldb_private {

uint64_t
DWARFDataExtractor::GetDWARFInitialLength(lldb::offset_t *offset_ptr) const {
  uint64_t length = GetU32(offset_ptr);
  m_is_dwarf64 = (length == UINT32_MAX);
  if (m_is_dwarf64)
    length = GetU64(offset_ptr);
  return length;
}

dw_offset_t
DWARFDataExtractor::GetDWARFOffset(lldb::offset_t *offset_ptr) const {
  return GetMaxU64(offset_ptr, GetDWARFSizeOfOffset());
}
}
