//===-- DWARFDebugAranges.cpp -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "DWARFDebugAranges.h"

#include <assert.h>
#include <stdio.h>

#include <algorithm>

#include "lldb/Utility/Log.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/Timer.h"

#include "DWARFUnit.h"
#include "DWARFDebugInfo.h"
#include "LogChannelDWARF.h"
#include "SymbolFileDWARF.h"

using namespace lldb;
using namespace lldb_private;

//----------------------------------------------------------------------
// Constructor
//----------------------------------------------------------------------
DWARFDebugAranges::DWARFDebugAranges() : m_aranges() {}

//----------------------------------------------------------------------
// CountArangeDescriptors
//----------------------------------------------------------------------
class CountArangeDescriptors {
public:
  CountArangeDescriptors(uint32_t &count_ref) : count(count_ref) {
    //      printf("constructor CountArangeDescriptors()\n");
  }
  void operator()(const DWARFDebugArangeSet &set) {
    count += set.NumDescriptors();
  }
  uint32_t &count;
};

//----------------------------------------------------------------------
// Extract
//----------------------------------------------------------------------
bool DWARFDebugAranges::Extract(const DWARFDataExtractor &debug_aranges_data) {
  if (debug_aranges_data.ValidOffset(0)) {
    lldb::offset_t offset = 0;

    DWARFDebugArangeSet set;
    Range range;
    while (set.Extract(debug_aranges_data, &offset)) {
      const uint32_t num_descriptors = set.NumDescriptors();
      if (num_descriptors > 0) {
        const dw_offset_t cu_offset = set.GetCompileUnitDIEOffset();

        for (uint32_t i = 0; i < num_descriptors; ++i) {
          const DWARFDebugArangeSet::Descriptor &descriptor =
              set.GetDescriptorRef(i);
          m_aranges.Append(RangeToDIE::Entry(descriptor.address,
                                             descriptor.length, cu_offset));
        }
      }
      set.Clear();
    }
  }
  return false;
}

//----------------------------------------------------------------------
// Generate
//----------------------------------------------------------------------
bool DWARFDebugAranges::Generate(SymbolFileDWARF *dwarf2Data) {
  Clear();
  DWARFDebugInfo *debug_info = dwarf2Data->DebugInfo();
  if (debug_info) {
    uint32_t cu_idx = 0;
    const uint32_t num_compile_units = dwarf2Data->GetNumCompileUnits();
    for (cu_idx = 0; cu_idx < num_compile_units; ++cu_idx) {
      DWARFUnit *cu = debug_info->GetCompileUnitAtIndex(cu_idx);
      if (cu)
        cu->BuildAddressRangeTable(dwarf2Data, this);
    }
  }
  return !IsEmpty();
}

void DWARFDebugAranges::Dump(Log *log) const {
  if (log == NULL)
    return;

  const size_t num_entries = m_aranges.GetSize();
  for (size_t i = 0; i < num_entries; ++i) {
    const RangeToDIE::Entry *entry = m_aranges.GetEntryAtIndex(i);
    if (entry)
      log->Printf("0x%8.8x: [0x%" PRIx64 " - 0x%" PRIx64 ")", entry->data,
                  entry->GetRangeBase(), entry->GetRangeEnd());
  }
}

void DWARFDebugAranges::AppendRange(dw_offset_t offset, dw_addr_t low_pc,
                                    dw_addr_t high_pc) {
  if (high_pc > low_pc)
    m_aranges.Append(RangeToDIE::Entry(low_pc, high_pc - low_pc, offset));
}

void DWARFDebugAranges::Sort(bool minimize) {
  static Timer::Category func_cat(LLVM_PRETTY_FUNCTION);
  Timer scoped_timer(func_cat, "%s this = %p", LLVM_PRETTY_FUNCTION,
                     static_cast<void *>(this));

  Log *log(LogChannelDWARF::GetLogIfAll(DWARF_LOG_DEBUG_ARANGES));
  size_t orig_arange_size = 0;
  if (log) {
    orig_arange_size = m_aranges.GetSize();
    log->Printf("DWARFDebugAranges::Sort(minimize = %u) with %" PRIu64
                " entries",
                minimize, (uint64_t)orig_arange_size);
  }

  m_aranges.Sort();
  m_aranges.CombineConsecutiveEntriesWithEqualData();

  if (log) {
    if (minimize) {
      const size_t new_arange_size = m_aranges.GetSize();
      const size_t delta = orig_arange_size - new_arange_size;
      log->Printf("DWARFDebugAranges::Sort() %" PRIu64
                  " entries after minimizing (%" PRIu64
                  " entries combined for %" PRIu64 " bytes saved)",
                  (uint64_t)new_arange_size, (uint64_t)delta,
                  (uint64_t)delta * sizeof(Range));
    }
    Dump(log);
  }
}

//----------------------------------------------------------------------
// FindAddress
//----------------------------------------------------------------------
dw_offset_t DWARFDebugAranges::FindAddress(dw_addr_t address) const {
  const RangeToDIE::Entry *entry = m_aranges.FindEntryThatContains(address);
  if (entry)
    return entry->data;
  return DW_INVALID_OFFSET;
}
