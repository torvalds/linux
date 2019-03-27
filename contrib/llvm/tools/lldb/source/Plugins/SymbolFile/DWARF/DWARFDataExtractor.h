//===-- DWARFDataExtractor.h ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_DWARFDataExtractor_h_
#define liblldb_DWARFDataExtractor_h_

#include "lldb/Core/dwarf.h"
#include "lldb/Utility/DataExtractor.h"

namespace lldb_private {

class DWARFDataExtractor : public DataExtractor {
public:
  DWARFDataExtractor() : DataExtractor(), m_is_dwarf64(false) {}

  DWARFDataExtractor(const DWARFDataExtractor &data, lldb::offset_t offset,
                     lldb::offset_t length)
      : DataExtractor(data, offset, length), m_is_dwarf64(false) {}

  uint64_t GetDWARFInitialLength(lldb::offset_t *offset_ptr) const;

  dw_offset_t GetDWARFOffset(lldb::offset_t *offset_ptr) const;

  size_t GetDWARFSizeofInitialLength() const { return m_is_dwarf64 ? 12 : 4; }
  size_t GetDWARFSizeOfOffset() const { return m_is_dwarf64 ? 8 : 4; }
  bool IsDWARF64() const { return m_is_dwarf64; }

protected:
  mutable bool m_is_dwarf64;
};
}

#endif // liblldb_DWARFDataExtractor_h_
