//===-- DWARFDebugAranges.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef SymbolFileDWARF_DWARFDebugAranges_h_
#define SymbolFileDWARF_DWARFDebugAranges_h_

#include "DWARFDebugArangeSet.h"
#include <list>

#include "lldb/Core/RangeMap.h"

class SymbolFileDWARF;

class DWARFDebugAranges {
protected:
  typedef lldb_private::RangeDataVector<dw_addr_t, uint32_t, dw_offset_t>
      RangeToDIE;

public:
  typedef RangeToDIE::Entry Range;
  typedef std::vector<RangeToDIE::Entry> RangeColl;

  DWARFDebugAranges();

  void Clear() { m_aranges.Clear(); }

  bool Extract(const lldb_private::DWARFDataExtractor &debug_aranges_data);

  bool Generate(SymbolFileDWARF *dwarf2Data);

  // Use append range multiple times and then call sort
  void AppendRange(dw_offset_t cu_offset, dw_addr_t low_pc, dw_addr_t high_pc);

  void Sort(bool minimize);

  const Range *RangeAtIndex(uint32_t idx) const {
    return m_aranges.GetEntryAtIndex(idx);
  }

  void Dump(lldb_private::Log *log) const;

  dw_offset_t FindAddress(dw_addr_t address) const;

  bool IsEmpty() const { return m_aranges.IsEmpty(); }
  size_t GetNumRanges() const { return m_aranges.GetSize(); }

  dw_offset_t OffsetAtIndex(uint32_t idx) const {
    const Range *range = m_aranges.GetEntryAtIndex(idx);
    if (range)
      return range->data;
    return DW_INVALID_OFFSET;
  }

  static void Dump(SymbolFileDWARF *dwarf2Data, lldb_private::Stream *s);

protected:
  RangeToDIE m_aranges;
};

#endif // SymbolFileDWARF_DWARFDebugAranges_h_
