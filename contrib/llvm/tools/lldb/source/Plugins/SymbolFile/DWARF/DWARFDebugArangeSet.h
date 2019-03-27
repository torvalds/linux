//===-- DWARFDebugArangeSet.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef SymbolFileDWARF_DWARFDebugArangeSet_h_
#define SymbolFileDWARF_DWARFDebugArangeSet_h_

#include "SymbolFileDWARF.h"
#include <vector>

class SymbolFileDWARF;

class DWARFDebugArangeSet {
public:
  struct Header {
    uint32_t length;    // The total length of the entries for that set, not
                        // including the length field itself.
    uint16_t version;   // The DWARF version number
    uint32_t cu_offset; // The offset from the beginning of the .debug_info
                        // section of the compilation unit entry referenced by
                        // the table.
    uint8_t addr_size;  // The size in bytes of an address on the target
                        // architecture. For segmented addressing, this is the
                        // size of the offset portion of the address
    uint8_t seg_size; // The size in bytes of a segment descriptor on the target
                      // architecture. If the target system uses a flat address
                      // space, this value is 0.
  };

  struct Descriptor {
    dw_addr_t address;
    dw_addr_t length;
    dw_addr_t end_address() const { return address + length; }
  };

  DWARFDebugArangeSet();
  void Clear();
  void SetOffset(uint32_t offset) { m_offset = offset; }
  void SetHeader(uint16_t version, uint32_t cu_offset, uint8_t addr_size,
                 uint8_t seg_size);
  void AddDescriptor(const DWARFDebugArangeSet::Descriptor &range);
  void Compact();
  bool Extract(const lldb_private::DWARFDataExtractor &data,
               lldb::offset_t *offset_ptr);
  void Dump(lldb_private::Stream *s) const;
  dw_offset_t GetCompileUnitDIEOffset() const { return m_header.cu_offset; }
  dw_offset_t GetOffsetOfNextEntry() const;
  dw_offset_t FindAddress(dw_addr_t address) const;
  size_t NumDescriptors() const { return m_arange_descriptors.size(); }
  const Header &GetHeader() const { return m_header; }
  const Descriptor *GetDescriptor(uint32_t i) const {
    if (i < m_arange_descriptors.size())
      return &m_arange_descriptors[i];
    return NULL;
  }

  const Descriptor &GetDescriptorRef(uint32_t i) const {
    return m_arange_descriptors[i];
  }

protected:
  typedef std::vector<Descriptor> DescriptorColl;
  typedef DescriptorColl::iterator DescriptorIter;
  typedef DescriptorColl::const_iterator DescriptorConstIter;

  uint32_t m_offset;
  Header m_header;
  DescriptorColl m_arange_descriptors;
};

#endif // SymbolFileDWARF_DWARFDebugArangeSet_h_
