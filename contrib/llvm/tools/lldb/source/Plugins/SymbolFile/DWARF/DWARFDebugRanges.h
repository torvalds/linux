//===-- DWARFDebugRanges.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef SymbolFileDWARF_DWARFDebugRanges_h_
#define SymbolFileDWARF_DWARFDebugRanges_h_

#include "DWARFDIE.h"
#include "SymbolFileDWARF.h"

#include <map>

class DWARFDebugRangesBase {
public:
  virtual ~DWARFDebugRangesBase(){};

  virtual void Extract(SymbolFileDWARF *dwarf2Data) = 0;
  virtual bool FindRanges(const DWARFUnit *cu, dw_offset_t debug_ranges_offset,
                          DWARFRangeList &range_list) const = 0;
  virtual uint64_t GetOffset(size_t Index) const = 0;
};

class DWARFDebugRanges final : public DWARFDebugRangesBase {
public:
  DWARFDebugRanges();

  void Extract(SymbolFileDWARF *dwarf2Data) override;
  bool FindRanges(const DWARFUnit *cu, dw_offset_t debug_ranges_offset,
                  DWARFRangeList &range_list) const override;
  uint64_t GetOffset(size_t Index) const override;

  static void Dump(lldb_private::Stream &s,
                   const lldb_private::DWARFDataExtractor &debug_ranges_data,
                   lldb::offset_t *offset_ptr, dw_addr_t cu_base_addr);

protected:
  bool Extract(SymbolFileDWARF *dwarf2Data, lldb::offset_t *offset_ptr,
               DWARFRangeList &range_list);

  typedef std::map<dw_offset_t, DWARFRangeList> range_map;
  typedef range_map::iterator range_map_iterator;
  typedef range_map::const_iterator range_map_const_iterator;
  range_map m_range_map;
};

// DWARF v5 .debug_rnglists section.
class DWARFDebugRngLists final : public DWARFDebugRangesBase {
  struct RngListEntry {
    uint8_t encoding;
    uint64_t value0;
    uint64_t value1;
  };

public:
  void Extract(SymbolFileDWARF *dwarf2Data) override;
  bool FindRanges(const DWARFUnit *cu, dw_offset_t debug_ranges_offset,
                  DWARFRangeList &range_list) const override;
  uint64_t GetOffset(size_t Index) const override;

protected:
  bool ExtractRangeList(const lldb_private::DWARFDataExtractor &data,
                        uint8_t addrSize, lldb::offset_t *offset_ptr,
                        std::vector<RngListEntry> &list);

  std::vector<uint64_t> Offsets;
  std::map<dw_offset_t, std::vector<RngListEntry>> m_range_map;
};

#endif // SymbolFileDWARF_DWARFDebugRanges_h_
