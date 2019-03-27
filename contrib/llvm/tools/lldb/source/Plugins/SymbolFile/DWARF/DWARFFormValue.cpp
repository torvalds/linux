//===-- DWARFFormValue.cpp --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <assert.h>

#include "lldb/Core/dwarf.h"
#include "lldb/Utility/Stream.h"

#include "DWARFUnit.h"
#include "DWARFFormValue.h"

class DWARFUnit;

using namespace lldb_private;

static uint8_t g_form_sizes_addr4[] = {
    0, // 0x00 unused
    4, // 0x01 DW_FORM_addr
    0, // 0x02 unused
    0, // 0x03 DW_FORM_block2
    0, // 0x04 DW_FORM_block4
    2, // 0x05 DW_FORM_data2
    4, // 0x06 DW_FORM_data4
    8, // 0x07 DW_FORM_data8
    0, // 0x08 DW_FORM_string
    0, // 0x09 DW_FORM_block
    0, // 0x0a DW_FORM_block1
    1, // 0x0b DW_FORM_data1
    1, // 0x0c DW_FORM_flag
    0, // 0x0d DW_FORM_sdata
    4, // 0x0e DW_FORM_strp
    0, // 0x0f DW_FORM_udata
    0, // 0x10 DW_FORM_ref_addr (addr size for DWARF2 and earlier, 4 bytes for
       // DWARF32, 8 bytes for DWARF32 in DWARF 3 and later
    1, // 0x11 DW_FORM_ref1
    2, // 0x12 DW_FORM_ref2
    4, // 0x13 DW_FORM_ref4
    8, // 0x14 DW_FORM_ref8
    0, // 0x15 DW_FORM_ref_udata
    0, // 0x16 DW_FORM_indirect
    4, // 0x17 DW_FORM_sec_offset
    0, // 0x18 DW_FORM_exprloc
    0, // 0x19 DW_FORM_flag_present
    0, // 0x1a
    0, // 0x1b
    0, // 0x1c
    0, // 0x1d
    0, // 0x1e
    0, // 0x1f
    8, // 0x20 DW_FORM_ref_sig8

};

static uint8_t g_form_sizes_addr8[] = {
    0, // 0x00 unused
    8, // 0x01 DW_FORM_addr
    0, // 0x02 unused
    0, // 0x03 DW_FORM_block2
    0, // 0x04 DW_FORM_block4
    2, // 0x05 DW_FORM_data2
    4, // 0x06 DW_FORM_data4
    8, // 0x07 DW_FORM_data8
    0, // 0x08 DW_FORM_string
    0, // 0x09 DW_FORM_block
    0, // 0x0a DW_FORM_block1
    1, // 0x0b DW_FORM_data1
    1, // 0x0c DW_FORM_flag
    0, // 0x0d DW_FORM_sdata
    4, // 0x0e DW_FORM_strp
    0, // 0x0f DW_FORM_udata
    0, // 0x10 DW_FORM_ref_addr (addr size for DWARF2 and earlier, 4 bytes for
       // DWARF32, 8 bytes for DWARF32 in DWARF 3 and later
    1, // 0x11 DW_FORM_ref1
    2, // 0x12 DW_FORM_ref2
    4, // 0x13 DW_FORM_ref4
    8, // 0x14 DW_FORM_ref8
    0, // 0x15 DW_FORM_ref_udata
    0, // 0x16 DW_FORM_indirect
    4, // 0x17 DW_FORM_sec_offset
    0, // 0x18 DW_FORM_exprloc
    0, // 0x19 DW_FORM_flag_present
    0, // 0x1a
    0, // 0x1b
    0, // 0x1c
    0, // 0x1d
    0, // 0x1e
    0, // 0x1f
    8, // 0x20 DW_FORM_ref_sig8
};

// Difference with g_form_sizes_addr8:
// DW_FORM_strp and DW_FORM_sec_offset are 8 instead of 4
static uint8_t g_form_sizes_addr8_dwarf64[] = {
    0, // 0x00 unused
    8, // 0x01 DW_FORM_addr
    0, // 0x02 unused
    0, // 0x03 DW_FORM_block2
    0, // 0x04 DW_FORM_block4
    2, // 0x05 DW_FORM_data2
    4, // 0x06 DW_FORM_data4
    8, // 0x07 DW_FORM_data8
    0, // 0x08 DW_FORM_string
    0, // 0x09 DW_FORM_block
    0, // 0x0a DW_FORM_block1
    1, // 0x0b DW_FORM_data1
    1, // 0x0c DW_FORM_flag
    0, // 0x0d DW_FORM_sdata
    8, // 0x0e DW_FORM_strp
    0, // 0x0f DW_FORM_udata
    0, // 0x10 DW_FORM_ref_addr (addr size for DWARF2 and earlier, 4 bytes for
       // DWARF32, 8 bytes for DWARF32 in DWARF 3 and later
    1, // 0x11 DW_FORM_ref1
    2, // 0x12 DW_FORM_ref2
    4, // 0x13 DW_FORM_ref4
    8, // 0x14 DW_FORM_ref8
    0, // 0x15 DW_FORM_ref_udata
    0, // 0x16 DW_FORM_indirect
    8, // 0x17 DW_FORM_sec_offset
    0, // 0x18 DW_FORM_exprloc
    0, // 0x19 DW_FORM_flag_present
    0, // 0x1a
    0, // 0x1b
    0, // 0x1c
    0, // 0x1d
    0, // 0x1e
    0, // 0x1f
    8, // 0x20 DW_FORM_ref_sig8
};

DWARFFormValue::FixedFormSizes
DWARFFormValue::GetFixedFormSizesForAddressSize(uint8_t addr_size,
                                                bool is_dwarf64) {
  if (!is_dwarf64) {
    switch (addr_size) {
    case 4:
      return FixedFormSizes(g_form_sizes_addr4, sizeof(g_form_sizes_addr4));
    case 8:
      return FixedFormSizes(g_form_sizes_addr8, sizeof(g_form_sizes_addr8));
    }
  } else {
    if (addr_size == 8)
      return FixedFormSizes(g_form_sizes_addr8_dwarf64,
                            sizeof(g_form_sizes_addr8_dwarf64));
    // is_dwarf64 && addr_size == 4 : no provider does this.
  }
  return FixedFormSizes();
}

DWARFFormValue::DWARFFormValue() : m_cu(NULL), m_form(0), m_value() {}

DWARFFormValue::DWARFFormValue(const DWARFUnit *cu)
    : m_cu(cu), m_form(0), m_value() {}

DWARFFormValue::DWARFFormValue(const DWARFUnit *cu, dw_form_t form)
    : m_cu(cu), m_form(form), m_value() {}

void DWARFFormValue::Clear() {
  m_cu = nullptr;
  m_form = 0;
  memset(&m_value, 0, sizeof(m_value));
}

bool DWARFFormValue::ExtractValue(const DWARFDataExtractor &data,
                                  lldb::offset_t *offset_ptr) {
  if (m_form == DW_FORM_implicit_const)
    return true;

  bool indirect = false;
  bool is_block = false;
  m_value.data = NULL;
  uint8_t ref_addr_size;
  // Read the value for the form into value and follow and DW_FORM_indirect
  // instances we run into
  do {
    indirect = false;
    switch (m_form) {
    case DW_FORM_addr:
      assert(m_cu);
      m_value.value.uval =
          data.GetMaxU64(offset_ptr, DWARFUnit::GetAddressByteSize(m_cu));
      break;
    case DW_FORM_block1:
      m_value.value.uval = data.GetU8(offset_ptr);
      is_block = true;
      break;
    case DW_FORM_block2:
      m_value.value.uval = data.GetU16(offset_ptr);
      is_block = true;
      break;
    case DW_FORM_block4:
      m_value.value.uval = data.GetU32(offset_ptr);
      is_block = true;
      break;
    case DW_FORM_data16:
      m_value.value.uval = 16;
      is_block = true;
      break;
    case DW_FORM_exprloc:
    case DW_FORM_block:
      m_value.value.uval = data.GetULEB128(offset_ptr);
      is_block = true;
      break;
    case DW_FORM_string:
      m_value.value.cstr = data.GetCStr(offset_ptr);
      break;
    case DW_FORM_sdata:
      m_value.value.sval = data.GetSLEB128(offset_ptr);
      break;
    case DW_FORM_strp:
    case DW_FORM_line_strp:
    case DW_FORM_sec_offset:
      assert(m_cu);
      m_value.value.uval =
          data.GetMaxU64(offset_ptr, DWARFUnit::IsDWARF64(m_cu) ? 8 : 4);
      break;
    case DW_FORM_addrx1:
    case DW_FORM_strx1:
    case DW_FORM_ref1:
    case DW_FORM_data1:
    case DW_FORM_flag:
      m_value.value.uval = data.GetU8(offset_ptr);
      break;
    case DW_FORM_addrx2:
    case DW_FORM_strx2:
    case DW_FORM_ref2:
    case DW_FORM_data2:
      m_value.value.uval = data.GetU16(offset_ptr);
      break;
    case DW_FORM_addrx3:
    case DW_FORM_strx3:
      m_value.value.uval = data.GetMaxU64(offset_ptr, 3);
      break;
    case DW_FORM_addrx4:
    case DW_FORM_strx4:
    case DW_FORM_ref4:
    case DW_FORM_data4:
      m_value.value.uval = data.GetU32(offset_ptr);
      break;
    case DW_FORM_data8:
    case DW_FORM_ref8:
    case DW_FORM_ref_sig8:
      m_value.value.uval = data.GetU64(offset_ptr);
      break;
    case DW_FORM_addrx:
    case DW_FORM_rnglistx:
    case DW_FORM_strx:
    case DW_FORM_udata:
    case DW_FORM_ref_udata:
    case DW_FORM_GNU_str_index:
    case DW_FORM_GNU_addr_index:
      m_value.value.uval = data.GetULEB128(offset_ptr);
      break;
    case DW_FORM_ref_addr:
      assert(m_cu);
      if (m_cu->GetVersion() <= 2)
        ref_addr_size = m_cu->GetAddressByteSize();
      else
        ref_addr_size = m_cu->IsDWARF64() ? 8 : 4;
      m_value.value.uval = data.GetMaxU64(offset_ptr, ref_addr_size);
      break;
    case DW_FORM_indirect:
      m_form = data.GetULEB128(offset_ptr);
      indirect = true;
      break;
    case DW_FORM_flag_present:
      m_value.value.uval = 1;
      break;
    default:
      return false;
    }
  } while (indirect);

  if (is_block) {
    m_value.data = data.PeekData(*offset_ptr, m_value.value.uval);
    if (m_value.data != NULL) {
      *offset_ptr += m_value.value.uval;
    }
  }

  return true;
}

bool DWARFFormValue::SkipValue(const DWARFDataExtractor &debug_info_data,
                               lldb::offset_t *offset_ptr) const {
  return DWARFFormValue::SkipValue(m_form, debug_info_data, offset_ptr, m_cu);
}

bool DWARFFormValue::SkipValue(dw_form_t form,
                               const DWARFDataExtractor &debug_info_data,
                               lldb::offset_t *offset_ptr,
                               const DWARFUnit *cu) {
  uint8_t ref_addr_size;
  switch (form) {
  // Blocks if inlined data that have a length field and the data bytes inlined
  // in the .debug_info
  case DW_FORM_exprloc:
  case DW_FORM_block: {
    dw_uleb128_t size = debug_info_data.GetULEB128(offset_ptr);
    *offset_ptr += size;
  }
    return true;
  case DW_FORM_block1: {
    dw_uleb128_t size = debug_info_data.GetU8(offset_ptr);
    *offset_ptr += size;
  }
    return true;
  case DW_FORM_block2: {
    dw_uleb128_t size = debug_info_data.GetU16(offset_ptr);
    *offset_ptr += size;
  }
    return true;
  case DW_FORM_block4: {
    dw_uleb128_t size = debug_info_data.GetU32(offset_ptr);
    *offset_ptr += size;
  }
    return true;

  // Inlined NULL terminated C-strings
  case DW_FORM_string:
    debug_info_data.GetCStr(offset_ptr);
    return true;

  // Compile unit address sized values
  case DW_FORM_addr:
    *offset_ptr += DWARFUnit::GetAddressByteSize(cu);
    return true;

  case DW_FORM_ref_addr:
    ref_addr_size = 4;
    assert(cu); // CU must be valid for DW_FORM_ref_addr objects or we will get
                // this wrong
    if (cu->GetVersion() <= 2)
      ref_addr_size = cu->GetAddressByteSize();
    else
      ref_addr_size = cu->IsDWARF64() ? 8 : 4;
    *offset_ptr += ref_addr_size;
    return true;

  // 0 bytes values (implied from DW_FORM)
  case DW_FORM_flag_present:
  case DW_FORM_implicit_const:
    return true;

    // 1 byte values
    case DW_FORM_addrx1:
    case DW_FORM_data1:
    case DW_FORM_flag:
    case DW_FORM_ref1:
    case DW_FORM_strx1:
      *offset_ptr += 1;
      return true;

    // 2 byte values
    case DW_FORM_addrx2:
    case DW_FORM_data2:
    case DW_FORM_ref2:
    case DW_FORM_strx2:
      *offset_ptr += 2;
      return true;

    // 3 byte values
    case DW_FORM_addrx3:
    case DW_FORM_strx3:
      *offset_ptr += 3;
      return true;

    // 32 bit for DWARF 32, 64 for DWARF 64
    case DW_FORM_sec_offset:
    case DW_FORM_strp:
      assert(cu);
      *offset_ptr += (cu->IsDWARF64() ? 8 : 4);
      return true;

    // 4 byte values
    case DW_FORM_addrx4:
    case DW_FORM_data4:
    case DW_FORM_ref4:
    case DW_FORM_strx4:
      *offset_ptr += 4;
      return true;

    // 8 byte values
    case DW_FORM_data8:
    case DW_FORM_ref8:
    case DW_FORM_ref_sig8:
      *offset_ptr += 8;
      return true;

    // signed or unsigned LEB 128 values
    case DW_FORM_addrx:
    case DW_FORM_rnglistx:
    case DW_FORM_sdata:
    case DW_FORM_udata:
    case DW_FORM_ref_udata:
    case DW_FORM_GNU_addr_index:
    case DW_FORM_GNU_str_index:
    case DW_FORM_strx:
      debug_info_data.Skip_LEB128(offset_ptr);
      return true;

  case DW_FORM_indirect: {
    dw_form_t indirect_form = debug_info_data.GetULEB128(offset_ptr);
    return DWARFFormValue::SkipValue(indirect_form, debug_info_data, offset_ptr,
                                     cu);
  }

  default:
    break;
  }
  return false;
}

void DWARFFormValue::Dump(Stream &s) const {
  uint64_t uvalue = Unsigned();
  bool cu_relative_offset = false;

  switch (m_form) {
  case DW_FORM_addr:
    s.Address(uvalue, sizeof(uint64_t));
    break;
  case DW_FORM_flag:
  case DW_FORM_data1:
    s.PutHex8(uvalue);
    break;
  case DW_FORM_data2:
    s.PutHex16(uvalue);
    break;
  case DW_FORM_sec_offset:
  case DW_FORM_data4:
    s.PutHex32(uvalue);
    break;
  case DW_FORM_ref_sig8:
  case DW_FORM_data8:
    s.PutHex64(uvalue);
    break;
  case DW_FORM_string:
    s.QuotedCString(AsCString());
    break;
  case DW_FORM_exprloc:
  case DW_FORM_block:
  case DW_FORM_block1:
  case DW_FORM_block2:
  case DW_FORM_block4:
    if (uvalue > 0) {
      switch (m_form) {
      case DW_FORM_exprloc:
      case DW_FORM_block:
        s.Printf("<0x%" PRIx64 "> ", uvalue);
        break;
      case DW_FORM_block1:
        s.Printf("<0x%2.2x> ", (uint8_t)uvalue);
        break;
      case DW_FORM_block2:
        s.Printf("<0x%4.4x> ", (uint16_t)uvalue);
        break;
      case DW_FORM_block4:
        s.Printf("<0x%8.8x> ", (uint32_t)uvalue);
        break;
      default:
        break;
      }

      const uint8_t *data_ptr = m_value.data;
      if (data_ptr) {
        const uint8_t *end_data_ptr =
            data_ptr + uvalue; // uvalue contains size of block
        while (data_ptr < end_data_ptr) {
          s.Printf("%2.2x ", *data_ptr);
          ++data_ptr;
        }
      } else
        s.PutCString("NULL");
    }
    break;

  case DW_FORM_sdata:
    s.PutSLEB128(uvalue);
    break;
  case DW_FORM_udata:
    s.PutULEB128(uvalue);
    break;
  case DW_FORM_strp: {
    const char *dbg_str = AsCString();
    if (dbg_str) {
      s.QuotedCString(dbg_str);
    } else {
      s.PutHex32(uvalue);
    }
  } break;

  case DW_FORM_ref_addr: {
    assert(m_cu); // CU must be valid for DW_FORM_ref_addr objects or we will
                  // get this wrong
    if (m_cu->GetVersion() <= 2)
      s.Address(uvalue, sizeof(uint64_t) * 2);
    else
      s.Address(uvalue, 4 * 2); // 4 for DWARF32, 8 for DWARF64, but we don't
                                // support DWARF64 yet
    break;
  }
  case DW_FORM_ref1:
    cu_relative_offset = true;
    break;
  case DW_FORM_ref2:
    cu_relative_offset = true;
    break;
  case DW_FORM_ref4:
    cu_relative_offset = true;
    break;
  case DW_FORM_ref8:
    cu_relative_offset = true;
    break;
  case DW_FORM_ref_udata:
    cu_relative_offset = true;
    break;

  // All DW_FORM_indirect attributes should be resolved prior to calling this
  // function
  case DW_FORM_indirect:
    s.PutCString("DW_FORM_indirect");
    break;
  case DW_FORM_flag_present:
    break;
  default:
    s.Printf("DW_FORM(0x%4.4x)", m_form);
    break;
  }

  if (cu_relative_offset) {
    assert(m_cu); // CU must be valid for DW_FORM_ref forms that are compile
                  // unit relative or we will get this wrong
    s.Printf("{0x%8.8" PRIx64 "}", uvalue + m_cu->GetOffset());
  }
}

const char *DWARFFormValue::AsCString() const {
  SymbolFileDWARF *symbol_file = m_cu->GetSymbolFileDWARF();

  if (m_form == DW_FORM_string) {
    return m_value.value.cstr;
  } else if (m_form == DW_FORM_strp) {
    if (!symbol_file)
      return nullptr;

    return symbol_file->get_debug_str_data().PeekCStr(m_value.value.uval);
  } else if (m_form == DW_FORM_GNU_str_index) {
    if (!symbol_file)
      return nullptr;

    uint32_t index_size = m_cu->IsDWARF64() ? 8 : 4;
    lldb::offset_t offset = m_value.value.uval * index_size;
    dw_offset_t str_offset =
        symbol_file->get_debug_str_offsets_data().GetMaxU64(&offset,
                                                            index_size);
    return symbol_file->get_debug_str_data().PeekCStr(str_offset);
  }

  if (m_form == DW_FORM_strx || m_form == DW_FORM_strx1 ||
      m_form == DW_FORM_strx2 || m_form == DW_FORM_strx3 ||
      m_form == DW_FORM_strx4) {

    // The same code as above.
    if (!symbol_file)
      return nullptr;

    uint32_t indexSize = m_cu->IsDWARF64() ? 8 : 4;
    lldb::offset_t offset =
        m_cu->GetStrOffsetsBase() + m_value.value.uval * indexSize;
    dw_offset_t strOffset =
        symbol_file->get_debug_str_offsets_data().GetMaxU64(&offset, indexSize);
    return symbol_file->get_debug_str_data().PeekCStr(strOffset);
  }

  if (m_form == DW_FORM_line_strp)
    return symbol_file->get_debug_line_str_data().PeekCStr(m_value.value.uval);

  return nullptr;
}

dw_addr_t DWARFFormValue::Address() const {
  SymbolFileDWARF *symbol_file = m_cu->GetSymbolFileDWARF();

  if (m_form == DW_FORM_addr)
    return Unsigned();

  assert(m_cu);
  assert(m_form == DW_FORM_GNU_addr_index || m_form == DW_FORM_addrx ||
         m_form == DW_FORM_addrx1 || m_form == DW_FORM_addrx2 ||
         m_form == DW_FORM_addrx3 || m_form == DW_FORM_addrx4);

  if (!symbol_file)
    return 0;

  uint32_t index_size = m_cu->GetAddressByteSize();
  dw_offset_t addr_base = m_cu->GetAddrBase();
  lldb::offset_t offset = addr_base + m_value.value.uval * index_size;
  return symbol_file->get_debug_addr_data().GetMaxU64(&offset, index_size);
}

uint64_t DWARFFormValue::Reference() const {
  uint64_t value = m_value.value.uval;
  switch (m_form) {
  case DW_FORM_ref1:
  case DW_FORM_ref2:
  case DW_FORM_ref4:
  case DW_FORM_ref8:
  case DW_FORM_ref_udata:
    assert(m_cu); // CU must be valid for DW_FORM_ref forms that are compile
                  // unit relative or we will get this wrong
    return value + m_cu->GetOffset();

  case DW_FORM_ref_addr:
  case DW_FORM_ref_sig8:
  case DW_FORM_GNU_ref_alt:
    return value;

  default:
    return DW_INVALID_OFFSET;
  }
}

uint64_t DWARFFormValue::Reference(dw_offset_t base_offset) const {
  uint64_t value = m_value.value.uval;
  switch (m_form) {
  case DW_FORM_ref1:
  case DW_FORM_ref2:
  case DW_FORM_ref4:
  case DW_FORM_ref8:
  case DW_FORM_ref_udata:
    return value + base_offset;

  case DW_FORM_ref_addr:
  case DW_FORM_ref_sig8:
  case DW_FORM_GNU_ref_alt:
    return value;

  default:
    return DW_INVALID_OFFSET;
  }
}

const uint8_t *DWARFFormValue::BlockData() const { return m_value.data; }

bool DWARFFormValue::IsBlockForm(const dw_form_t form) {
  switch (form) {
  case DW_FORM_exprloc:
  case DW_FORM_block:
  case DW_FORM_block1:
  case DW_FORM_block2:
  case DW_FORM_block4:
    return true;
  }
  return false;
}

bool DWARFFormValue::IsDataForm(const dw_form_t form) {
  switch (form) {
  case DW_FORM_sdata:
  case DW_FORM_udata:
  case DW_FORM_data1:
  case DW_FORM_data2:
  case DW_FORM_data4:
  case DW_FORM_data8:
    return true;
  }
  return false;
}

int DWARFFormValue::Compare(const DWARFFormValue &a_value,
                            const DWARFFormValue &b_value) {
  dw_form_t a_form = a_value.Form();
  dw_form_t b_form = b_value.Form();
  if (a_form < b_form)
    return -1;
  if (a_form > b_form)
    return 1;
  switch (a_form) {
  case DW_FORM_addr:
  case DW_FORM_flag:
  case DW_FORM_data1:
  case DW_FORM_data2:
  case DW_FORM_data4:
  case DW_FORM_data8:
  case DW_FORM_udata:
  case DW_FORM_ref_addr:
  case DW_FORM_sec_offset:
  case DW_FORM_flag_present:
  case DW_FORM_ref_sig8:
  case DW_FORM_GNU_addr_index: {
    uint64_t a = a_value.Unsigned();
    uint64_t b = b_value.Unsigned();
    if (a < b)
      return -1;
    if (a > b)
      return 1;
    return 0;
  }

  case DW_FORM_sdata: {
    int64_t a = a_value.Signed();
    int64_t b = b_value.Signed();
    if (a < b)
      return -1;
    if (a > b)
      return 1;
    return 0;
  }

  case DW_FORM_string:
  case DW_FORM_strp:
  case DW_FORM_GNU_str_index: {
    const char *a_string = a_value.AsCString();
    const char *b_string = b_value.AsCString();
    if (a_string == b_string)
      return 0;
    else if (a_string && b_string)
      return strcmp(a_string, b_string);
    else if (a_string == NULL)
      return -1; // A string is NULL, and B is valid
    else
      return 1; // A string valid, and B is NULL
  }

  case DW_FORM_block:
  case DW_FORM_block1:
  case DW_FORM_block2:
  case DW_FORM_block4:
  case DW_FORM_exprloc: {
    uint64_t a_len = a_value.Unsigned();
    uint64_t b_len = b_value.Unsigned();
    if (a_len < b_len)
      return -1;
    if (a_len > b_len)
      return 1;
    // The block lengths are the same
    return memcmp(a_value.BlockData(), b_value.BlockData(), a_value.Unsigned());
  } break;

  case DW_FORM_ref1:
  case DW_FORM_ref2:
  case DW_FORM_ref4:
  case DW_FORM_ref8:
  case DW_FORM_ref_udata: {
    uint64_t a = a_value.Reference();
    uint64_t b = b_value.Reference();
    if (a < b)
      return -1;
    if (a > b)
      return 1;
    return 0;
  }

  case DW_FORM_indirect:
    llvm_unreachable(
        "This shouldn't happen after the form has been extracted...");

  default:
    llvm_unreachable("Unhandled DW_FORM");
  }
  return -1;
}

bool DWARFFormValue::FormIsSupported(dw_form_t form) {
  switch (form) {
    case DW_FORM_addr:
    case DW_FORM_addrx:
    case DW_FORM_rnglistx:
    case DW_FORM_block2:
    case DW_FORM_block4:
    case DW_FORM_data2:
    case DW_FORM_data4:
    case DW_FORM_data8:
    case DW_FORM_string:
    case DW_FORM_block:
    case DW_FORM_block1:
    case DW_FORM_data1:
    case DW_FORM_flag:
    case DW_FORM_sdata:
    case DW_FORM_strp:
    case DW_FORM_strx:
    case DW_FORM_strx1:
    case DW_FORM_strx2:
    case DW_FORM_strx3:
    case DW_FORM_strx4:
    case DW_FORM_udata:
    case DW_FORM_ref_addr:
    case DW_FORM_ref1:
    case DW_FORM_ref2:
    case DW_FORM_ref4:
    case DW_FORM_ref8:
    case DW_FORM_ref_udata:
    case DW_FORM_indirect:
    case DW_FORM_sec_offset:
    case DW_FORM_exprloc:
    case DW_FORM_flag_present:
    case DW_FORM_ref_sig8:
    case DW_FORM_GNU_str_index:
    case DW_FORM_GNU_addr_index:
    case DW_FORM_implicit_const:
      return true;
    default:
      break;
  }
  return false;
}
