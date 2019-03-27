//===-- DWARFDebugRanges.cpp ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "DWARFDebugRanges.h"
#include "DWARFUnit.h"
#include "SymbolFileDWARF.h"
#include "lldb/Utility/Stream.h"
#include <assert.h>

using namespace lldb_private;
using namespace std;

static dw_addr_t GetBaseAddressMarker(uint32_t addr_size) {
  switch(addr_size) {
    case 2:
      return 0xffff;
    case 4:
      return 0xffffffff;
    case 8:
      return 0xffffffffffffffff;
  }
  llvm_unreachable("GetBaseAddressMarker unsupported address size.");
}

DWARFDebugRanges::DWARFDebugRanges() : m_range_map() {}

void DWARFDebugRanges::Extract(SymbolFileDWARF *dwarf2Data) {
  DWARFRangeList range_list;
  lldb::offset_t offset = 0;
  dw_offset_t debug_ranges_offset = offset;
  while (Extract(dwarf2Data, &offset, range_list)) {
    range_list.Sort();
    m_range_map[debug_ranges_offset] = range_list;
    debug_ranges_offset = offset;
  }
}

bool DWARFDebugRanges::Extract(SymbolFileDWARF *dwarf2Data,
                               lldb::offset_t *offset_ptr,
                               DWARFRangeList &range_list) {
  range_list.Clear();

  lldb::offset_t range_offset = *offset_ptr;
  const DWARFDataExtractor &debug_ranges_data =
      dwarf2Data->get_debug_ranges_data();
  uint32_t addr_size = debug_ranges_data.GetAddressByteSize();
  dw_addr_t base_addr = 0;
  dw_addr_t base_addr_marker = GetBaseAddressMarker(addr_size);

  while (
      debug_ranges_data.ValidOffsetForDataOfSize(*offset_ptr, 2 * addr_size)) {
    dw_addr_t begin = debug_ranges_data.GetMaxU64(offset_ptr, addr_size);
    dw_addr_t end = debug_ranges_data.GetMaxU64(offset_ptr, addr_size);

    if (!begin && !end) {
      // End of range list
      break;
    }

    if (begin == base_addr_marker) {
      base_addr = end;
      continue;
    }

    // Filter out empty ranges
    if (begin < end)
      range_list.Append(DWARFRangeList::Entry(begin + base_addr, end - begin));
  }

  // Make sure we consumed at least something
  return range_offset != *offset_ptr;
}

void DWARFDebugRanges::Dump(Stream &s,
                            const DWARFDataExtractor &debug_ranges_data,
                            lldb::offset_t *offset_ptr,
                            dw_addr_t cu_base_addr) {
  uint32_t addr_size = s.GetAddressByteSize();

  dw_addr_t base_addr = cu_base_addr;
  while (
      debug_ranges_data.ValidOffsetForDataOfSize(*offset_ptr, 2 * addr_size)) {
    dw_addr_t begin = debug_ranges_data.GetMaxU64(offset_ptr, addr_size);
    dw_addr_t end = debug_ranges_data.GetMaxU64(offset_ptr, addr_size);
    // Extend 4 byte addresses that consists of 32 bits of 1's to be 64 bits of
    // ones
    if (begin == 0xFFFFFFFFull && addr_size == 4)
      begin = LLDB_INVALID_ADDRESS;

    s.Indent();
    if (begin == 0 && end == 0) {
      s.PutCString(" End");
      break;
    } else if (begin == LLDB_INVALID_ADDRESS) {
      // A base address selection entry
      base_addr = end;
      s.Address(base_addr, sizeof(dw_addr_t), " Base address = ");
    } else {
      // Convert from offset to an address
      dw_addr_t begin_addr = begin + base_addr;
      dw_addr_t end_addr = end + base_addr;

      s.AddressRange(begin_addr, end_addr, sizeof(dw_addr_t), NULL);
    }
  }
}

bool DWARFDebugRanges::FindRanges(const DWARFUnit *cu,
                                  dw_offset_t debug_ranges_offset,
                                  DWARFRangeList &range_list) const {
  dw_addr_t debug_ranges_address = cu->GetRangesBase() + debug_ranges_offset;
  range_map_const_iterator pos = m_range_map.find(debug_ranges_address);
  if (pos != m_range_map.end()) {
    range_list = pos->second;

    // All DW_AT_ranges are relative to the base address of the compile
    // unit. We add the compile unit base address to make sure all the
    // addresses are properly fixed up.
    range_list.Slide(cu->GetBaseAddress());
    return true;
  }
  return false;
}

uint64_t DWARFDebugRanges::GetOffset(size_t Index) const {
  lldbassert(false && "DW_FORM_rnglistx is not present before DWARF5");
  return 0;
}

bool DWARFDebugRngLists::ExtractRangeList(
    const DWARFDataExtractor &data, uint8_t addrSize,
    lldb::offset_t *offset_ptr, std::vector<RngListEntry> &rangeList) {
  rangeList.clear();

  bool error = false;
  while (!error) {
    switch (data.GetU8(offset_ptr)) {
    case DW_RLE_end_of_list:
      return true;

    case DW_RLE_start_length: {
      dw_addr_t begin = data.GetMaxU64(offset_ptr, addrSize);
      dw_addr_t len = data.GetULEB128(offset_ptr);
      rangeList.push_back({DW_RLE_start_length, begin, len});
      break;
    }

    case DW_RLE_start_end: {
      dw_addr_t begin = data.GetMaxU64(offset_ptr, addrSize);
      dw_addr_t end = data.GetMaxU64(offset_ptr, addrSize);
      rangeList.push_back({DW_RLE_start_end, begin, end});
      break;
    }

    case DW_RLE_base_address: {
      dw_addr_t base = data.GetMaxU64(offset_ptr, addrSize);
      rangeList.push_back({DW_RLE_base_address, base, 0});
      break;
    }

    case DW_RLE_offset_pair: {
      dw_addr_t begin = data.GetULEB128(offset_ptr);
      dw_addr_t end = data.GetULEB128(offset_ptr);
      rangeList.push_back({DW_RLE_offset_pair, begin, end});
      break;
    }

    case DW_RLE_base_addressx: {
      dw_addr_t base = data.GetULEB128(offset_ptr);
      rangeList.push_back({DW_RLE_base_addressx, base, 0});
      break;
    }

    case DW_RLE_startx_endx: {
      dw_addr_t start = data.GetULEB128(offset_ptr);
      dw_addr_t end = data.GetULEB128(offset_ptr);
      rangeList.push_back({DW_RLE_startx_endx, start, end});
      break;
    }

    case DW_RLE_startx_length: {
      dw_addr_t start = data.GetULEB128(offset_ptr);
      dw_addr_t length = data.GetULEB128(offset_ptr);
      rangeList.push_back({DW_RLE_startx_length, start, length});
      break;
    }

    default:
      lldbassert(0 && "unknown range list entry encoding");
      error = true;
    }
  }

  return false;
}

static uint64_t ReadAddressFromDebugAddrSection(const DWARFUnit *cu,
                                                uint32_t index) {
  uint32_t index_size = cu->GetAddressByteSize();
  dw_offset_t addr_base = cu->GetAddrBase();
  lldb::offset_t offset = addr_base + index * index_size;
  return cu->GetSymbolFileDWARF()->get_debug_addr_data().GetMaxU64(&offset,
                                                                   index_size);
}

bool DWARFDebugRngLists::FindRanges(const DWARFUnit *cu,
                                    dw_offset_t debug_ranges_offset,
                                    DWARFRangeList &range_list) const {
  range_list.Clear();
  dw_addr_t debug_ranges_address = cu->GetRangesBase() + debug_ranges_offset;
  auto pos = m_range_map.find(debug_ranges_address);
  if (pos != m_range_map.end()) {
    dw_addr_t BaseAddr = cu->GetBaseAddress();
    for (const RngListEntry &E : pos->second) {
      switch (E.encoding) {
      case DW_RLE_start_length:
        range_list.Append(DWARFRangeList::Entry(E.value0, E.value1));
        break;
      case DW_RLE_base_address:
        BaseAddr = E.value0;
        break;
      case DW_RLE_start_end:
        range_list.Append(DWARFRangeList::Entry(E.value0, E.value1 - E.value0));
        break;
      case DW_RLE_offset_pair:
        range_list.Append(
            DWARFRangeList::Entry(BaseAddr + E.value0, E.value1 - E.value0));
        break;
      case DW_RLE_base_addressx: {
        BaseAddr = ReadAddressFromDebugAddrSection(cu, E.value0);
        break;
      }
      case DW_RLE_startx_endx: {
        dw_addr_t start = ReadAddressFromDebugAddrSection(cu, E.value0);
        dw_addr_t end = ReadAddressFromDebugAddrSection(cu, E.value1);
        range_list.Append(DWARFRangeList::Entry(start, end - start));
        break;
      }
      case DW_RLE_startx_length: {
        dw_addr_t start = ReadAddressFromDebugAddrSection(cu, E.value0);
        range_list.Append(DWARFRangeList::Entry(start, E.value1));
        break;
      }
      default:
        llvm_unreachable("unexpected encoding");
      }
    }
    return true;
  }
  return false;
}

void DWARFDebugRngLists::Extract(SymbolFileDWARF *dwarf2Data) {
  const DWARFDataExtractor &data = dwarf2Data->get_debug_rnglists_data();
  lldb::offset_t offset = 0;

  uint64_t length = data.GetU32(&offset);
  bool isDwarf64 = (length == 0xffffffff);
  if (isDwarf64)
    length = data.GetU64(&offset);
  lldb::offset_t end = offset + length;

  // Check version.
  if (data.GetU16(&offset) < 5)
    return;

  uint8_t addrSize = data.GetU8(&offset);

  // We do not support non-zero segment selector size.
  if (data.GetU8(&offset) != 0) {
    lldbassert(0 && "not implemented");
    return;
  }

  uint32_t offsetsAmount = data.GetU32(&offset);
  for (uint32_t i = 0; i < offsetsAmount; ++i)
    Offsets.push_back(data.GetMaxU64(&offset, isDwarf64 ? 8 : 4));

  lldb::offset_t listOffset = offset;
  std::vector<RngListEntry> rangeList;
  while (offset < end && ExtractRangeList(data, addrSize, &offset, rangeList)) {
    m_range_map[listOffset] = rangeList;
    listOffset = offset;
  }
}

uint64_t DWARFDebugRngLists::GetOffset(size_t Index) const {
  return Offsets[Index];
}
