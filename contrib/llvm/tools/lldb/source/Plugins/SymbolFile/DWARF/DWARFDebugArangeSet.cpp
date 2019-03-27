//===-- DWARFDebugArangeSet.cpp ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "DWARFDebugArangeSet.h"

#include "SymbolFileDWARF.h"
#include "lldb/Utility/Stream.h"
#include <assert.h>

using namespace lldb_private;

DWARFDebugArangeSet::DWARFDebugArangeSet()
    : m_offset(DW_INVALID_OFFSET), m_header(), m_arange_descriptors() {
  m_header.length = 0;
  m_header.version = 0;
  m_header.cu_offset = 0;
  m_header.addr_size = 0;
  m_header.seg_size = 0;
}

void DWARFDebugArangeSet::Clear() {
  m_offset = DW_INVALID_OFFSET;
  m_header.length = 0;
  m_header.version = 0;
  m_header.cu_offset = 0;
  m_header.addr_size = 0;
  m_header.seg_size = 0;
  m_arange_descriptors.clear();
}

void DWARFDebugArangeSet::SetHeader(uint16_t version, uint32_t cu_offset,
                                    uint8_t addr_size, uint8_t seg_size) {
  m_header.version = version;
  m_header.cu_offset = cu_offset;
  m_header.addr_size = addr_size;
  m_header.seg_size = seg_size;
}

void DWARFDebugArangeSet::Compact() {
  if (m_arange_descriptors.empty())
    return;

  // Iterate through all arange descriptors and combine any ranges that overlap
  // or have matching boundaries. The m_arange_descriptors are assumed to be in
  // ascending order after being built by adding descriptors using the
  // AddDescriptor method.
  uint32_t i = 0;
  while (i + 1 < m_arange_descriptors.size()) {
    if (m_arange_descriptors[i].end_address() >=
        m_arange_descriptors[i + 1].address) {
      // The current range ends at or exceeds the start of the next address
      // range. Compute the max end address between the two and use that to
      // make the new length.
      const dw_addr_t max_end_addr =
          std::max(m_arange_descriptors[i].end_address(),
                   m_arange_descriptors[i + 1].end_address());
      m_arange_descriptors[i].length =
          max_end_addr - m_arange_descriptors[i].address;
      // Now remove the next entry as it was just combined with the previous
      // one.
      m_arange_descriptors.erase(m_arange_descriptors.begin() + i + 1);
    } else {
      // Discontiguous address range, just proceed to the next one.
      ++i;
    }
  }
}
//----------------------------------------------------------------------
// Compare function DWARFDebugArangeSet::Descriptor structures
//----------------------------------------------------------------------
static bool DescriptorLessThan(const DWARFDebugArangeSet::Descriptor &range1,
                               const DWARFDebugArangeSet::Descriptor &range2) {
  return range1.address < range2.address;
}

//----------------------------------------------------------------------
// Add a range descriptor and keep things sorted so we can easily compact the
// ranges before being saved or used.
//----------------------------------------------------------------------
void DWARFDebugArangeSet::AddDescriptor(
    const DWARFDebugArangeSet::Descriptor &range) {
  if (m_arange_descriptors.empty()) {
    m_arange_descriptors.push_back(range);
    return;
  }

  DescriptorIter end = m_arange_descriptors.end();
  DescriptorIter pos =
      lower_bound(m_arange_descriptors.begin(), end, range, DescriptorLessThan);
  const dw_addr_t range_end_addr = range.end_address();
  if (pos != end) {
    const dw_addr_t found_end_addr = pos->end_address();
    if (range.address < pos->address) {
      if (range_end_addr < pos->address) {
        // Non-contiguous entries, add this one before the found entry
        m_arange_descriptors.insert(pos, range);
      } else if (range_end_addr == pos->address) {
        // The top end of 'range' is the lower end of the entry pointed to by
        // 'pos'. We can combine range with the entry we found by setting the
        // starting address and increasing the length since they don't overlap.
        pos->address = range.address;
        pos->length += range.length;
      } else {
        // We can combine these two and make sure the largest end address is
        // used to make end address.
        pos->address = range.address;
        pos->length = std::max(found_end_addr, range_end_addr) - pos->address;
      }
    } else if (range.address == pos->address) {
      pos->length = std::max(pos->length, range.length);
    }
  } else {
    // NOTE: 'pos' points to entry past the end which is ok for insert,
    // don't use otherwise!!!
    const dw_addr_t max_addr = m_arange_descriptors.back().end_address();
    if (max_addr < range.address) {
      // Non-contiguous entries, add this one before the found entry
      m_arange_descriptors.insert(pos, range);
    } else if (max_addr == range.address) {
      m_arange_descriptors.back().length += range.length;
    } else {
      m_arange_descriptors.back().length = std::max(max_addr, range_end_addr) -
                                           m_arange_descriptors.back().address;
    }
  }
}

bool DWARFDebugArangeSet::Extract(const DWARFDataExtractor &data,
                                  lldb::offset_t *offset_ptr) {
  if (data.ValidOffset(*offset_ptr)) {
    m_arange_descriptors.clear();
    m_offset = *offset_ptr;

    // 7.20 Address Range Table
    //
    // Each set of entries in the table of address ranges contained in the
    // .debug_aranges section begins with a header consisting of: a 4-byte
    // length containing the length of the set of entries for this compilation
    // unit, not including the length field itself; a 2-byte version identifier
    // containing the value 2 for DWARF Version 2; a 4-byte offset into
    // the.debug_infosection; a 1-byte unsigned integer containing the size in
    // bytes of an address (or the offset portion of an address for segmented
    // addressing) on the target system; and a 1-byte unsigned integer
    // containing the size in bytes of a segment descriptor on the target
    // system. This header is followed by a series of tuples. Each tuple
    // consists of an address and a length, each in the size appropriate for an
    // address on the target architecture.
    m_header.length = data.GetDWARFInitialLength(offset_ptr);
    m_header.version = data.GetU16(offset_ptr);
    m_header.cu_offset = data.GetDWARFOffset(offset_ptr);
    m_header.addr_size = data.GetU8(offset_ptr);
    m_header.seg_size = data.GetU8(offset_ptr);

    // Try to avoid reading invalid arange sets by making sure:
    // 1 - the version looks good
    // 2 - the address byte size looks plausible
    // 3 - the length seems to make sense
    // size looks plausible
    if ((m_header.version >= 2 && m_header.version <= 5) &&
        (m_header.addr_size == 4 || m_header.addr_size == 8) &&
        (m_header.length > 0)) {
      if (data.ValidOffset(m_offset + sizeof(m_header.length) +
                           m_header.length - 1)) {
        // The first tuple following the header in each set begins at an offset
        // that is a multiple of the size of a single tuple (that is, twice the
        // size of an address). The header is padded, if necessary, to the
        // appropriate boundary.
        const uint32_t header_size = *offset_ptr - m_offset;
        const uint32_t tuple_size = m_header.addr_size << 1;
        uint32_t first_tuple_offset = 0;
        while (first_tuple_offset < header_size)
          first_tuple_offset += tuple_size;

        *offset_ptr = m_offset + first_tuple_offset;

        Descriptor arangeDescriptor;

        static_assert(
            sizeof(arangeDescriptor.address) == sizeof(arangeDescriptor.length),
            "DWARFDebugArangeSet::Descriptor.address and "
            "DWARFDebugArangeSet::Descriptor.length must have same size");

        while (data.ValidOffset(*offset_ptr)) {
          arangeDescriptor.address =
              data.GetMaxU64(offset_ptr, m_header.addr_size);
          arangeDescriptor.length =
              data.GetMaxU64(offset_ptr, m_header.addr_size);

          // Each set of tuples is terminated by a 0 for the address and 0 for
          // the length.
          if (arangeDescriptor.address || arangeDescriptor.length)
            m_arange_descriptors.push_back(arangeDescriptor);
          else
            break; // We are done if we get a zero address and length
        }
      }
#if defined(LLDB_CONFIGURATION_DEBUG)
      else {
        printf("warning: .debug_arange set length is too large arange data at "
               "0x%8.8x: length=0x%8.8x, version=0x%4.4x, cu_offset=0x%8.8x, "
               "addr_size=%u, seg_size=%u\n",
               m_offset, m_header.length, m_header.version, m_header.cu_offset,
               m_header.addr_size, m_header.seg_size);
      }
#endif
    }
#if defined(LLDB_CONFIGURATION_DEBUG)
    else {
      printf("warning: .debug_arange set has bad header at 0x%8.8x: "
             "length=0x%8.8x, version=0x%4.4x, cu_offset=0x%8.8x, "
             "addr_size=%u, seg_size=%u\n",
             m_offset, m_header.length, m_header.version, m_header.cu_offset,
             m_header.addr_size, m_header.seg_size);
    }
#endif

    return !m_arange_descriptors.empty();
  }
  return false;
}

dw_offset_t DWARFDebugArangeSet::GetOffsetOfNextEntry() const {
  return m_offset + m_header.length + 4;
}

void DWARFDebugArangeSet::Dump(Stream *s) const {
  s->Printf("Address Range Header: length = 0x%8.8x, version = 0x%4.4x, "
            "cu_offset = 0x%8.8x, addr_size = 0x%2.2x, seg_size = 0x%2.2x\n",
            m_header.length, m_header.version, m_header.cu_offset,
            m_header.addr_size, m_header.seg_size);

  const uint32_t hex_width = m_header.addr_size * 2;
  DescriptorConstIter pos;
  DescriptorConstIter end = m_arange_descriptors.end();
  for (pos = m_arange_descriptors.begin(); pos != end; ++pos)
    s->Printf("[0x%*.*" PRIx64 " - 0x%*.*" PRIx64 ")\n", hex_width, hex_width,
              pos->address, hex_width, hex_width, pos->end_address());
}

class DescriptorContainsAddress {
public:
  DescriptorContainsAddress(dw_addr_t address) : m_address(address) {}
  bool operator()(const DWARFDebugArangeSet::Descriptor &desc) const {
    return (m_address >= desc.address) &&
           (m_address < (desc.address + desc.length));
  }

private:
  const dw_addr_t m_address;
};

dw_offset_t DWARFDebugArangeSet::FindAddress(dw_addr_t address) const {
  DescriptorConstIter end = m_arange_descriptors.end();
  DescriptorConstIter pos =
      std::find_if(m_arange_descriptors.begin(), end,   // Range
                   DescriptorContainsAddress(address)); // Predicate
  if (pos != end)
    return m_header.cu_offset;

  return DW_INVALID_OFFSET;
}
