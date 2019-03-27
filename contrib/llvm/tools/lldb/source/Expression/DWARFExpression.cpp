//===-- DWARFExpression.cpp -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Expression/DWARFExpression.h"

#include <inttypes.h>

#include <vector>

#include "lldb/Core/Module.h"
#include "lldb/Core/Value.h"
#include "lldb/Core/dwarf.h"
#include "lldb/Utility/DataEncoder.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/VMRange.h"

#include "lldb/Host/Host.h"
#include "lldb/Utility/Endian.h"

#include "lldb/Symbol/Function.h"

#include "lldb/Target/ABI.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/StackID.h"
#include "lldb/Target/Thread.h"

#include "Plugins/SymbolFile/DWARF/DWARFUnit.h"

using namespace lldb;
using namespace lldb_private;

static lldb::addr_t
ReadAddressFromDebugAddrSection(const DWARFUnit *dwarf_cu,
                                uint32_t index) {
  uint32_t index_size = dwarf_cu->GetAddressByteSize();
  dw_offset_t addr_base = dwarf_cu->GetAddrBase();
  lldb::offset_t offset = addr_base + index * index_size;
  return dwarf_cu->GetSymbolFileDWARF()->get_debug_addr_data().GetMaxU64(
      &offset, index_size);
}

//----------------------------------------------------------------------
// DWARFExpression constructor
//----------------------------------------------------------------------
DWARFExpression::DWARFExpression(DWARFUnit *dwarf_cu)
    : m_module_wp(), m_data(), m_dwarf_cu(dwarf_cu),
      m_reg_kind(eRegisterKindDWARF), m_loclist_slide(LLDB_INVALID_ADDRESS) {}

DWARFExpression::DWARFExpression(const DWARFExpression &rhs)
    : m_module_wp(rhs.m_module_wp), m_data(rhs.m_data),
      m_dwarf_cu(rhs.m_dwarf_cu), m_reg_kind(rhs.m_reg_kind),
      m_loclist_slide(rhs.m_loclist_slide) {}

DWARFExpression::DWARFExpression(lldb::ModuleSP module_sp,
                                 const DataExtractor &data,
                                 DWARFUnit *dwarf_cu,
                                 lldb::offset_t data_offset,
                                 lldb::offset_t data_length)
    : m_module_wp(), m_data(data, data_offset, data_length),
      m_dwarf_cu(dwarf_cu), m_reg_kind(eRegisterKindDWARF),
      m_loclist_slide(LLDB_INVALID_ADDRESS) {
  if (module_sp)
    m_module_wp = module_sp;
}

//----------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------
DWARFExpression::~DWARFExpression() {}

bool DWARFExpression::IsValid() const { return m_data.GetByteSize() > 0; }

void DWARFExpression::SetOpcodeData(const DataExtractor &data) {
  m_data = data;
}

void DWARFExpression::CopyOpcodeData(lldb::ModuleSP module_sp,
                                     const DataExtractor &data,
                                     lldb::offset_t data_offset,
                                     lldb::offset_t data_length) {
  const uint8_t *bytes = data.PeekData(data_offset, data_length);
  if (bytes) {
    m_module_wp = module_sp;
    m_data.SetData(DataBufferSP(new DataBufferHeap(bytes, data_length)));
    m_data.SetByteOrder(data.GetByteOrder());
    m_data.SetAddressByteSize(data.GetAddressByteSize());
  }
}

void DWARFExpression::CopyOpcodeData(const void *data,
                                     lldb::offset_t data_length,
                                     ByteOrder byte_order,
                                     uint8_t addr_byte_size) {
  if (data && data_length) {
    m_data.SetData(DataBufferSP(new DataBufferHeap(data, data_length)));
    m_data.SetByteOrder(byte_order);
    m_data.SetAddressByteSize(addr_byte_size);
  }
}

void DWARFExpression::CopyOpcodeData(uint64_t const_value,
                                     lldb::offset_t const_value_byte_size,
                                     uint8_t addr_byte_size) {
  if (const_value_byte_size) {
    m_data.SetData(
        DataBufferSP(new DataBufferHeap(&const_value, const_value_byte_size)));
    m_data.SetByteOrder(endian::InlHostByteOrder());
    m_data.SetAddressByteSize(addr_byte_size);
  }
}

void DWARFExpression::SetOpcodeData(lldb::ModuleSP module_sp,
                                    const DataExtractor &data,
                                    lldb::offset_t data_offset,
                                    lldb::offset_t data_length) {
  m_module_wp = module_sp;
  m_data.SetData(data, data_offset, data_length);
}

void DWARFExpression::DumpLocation(Stream *s, lldb::offset_t offset,
                                   lldb::offset_t length,
                                   lldb::DescriptionLevel level,
                                   ABI *abi) const {
  if (!m_data.ValidOffsetForDataOfSize(offset, length))
    return;
  const lldb::offset_t start_offset = offset;
  const lldb::offset_t end_offset = offset + length;
  while (m_data.ValidOffset(offset) && offset < end_offset) {
    const lldb::offset_t op_offset = offset;
    const uint8_t op = m_data.GetU8(&offset);

    switch (level) {
    default:
      break;

    case lldb::eDescriptionLevelBrief:
      if (op_offset > start_offset)
        s->PutChar(' ');
      break;

    case lldb::eDescriptionLevelFull:
    case lldb::eDescriptionLevelVerbose:
      if (op_offset > start_offset)
        s->EOL();
      s->Indent();
      if (level == lldb::eDescriptionLevelFull)
        break;
      // Fall through for verbose and print offset and DW_OP prefix..
      s->Printf("0x%8.8" PRIx64 ": %s", op_offset,
                op >= DW_OP_APPLE_uninit ? "DW_OP_APPLE_" : "DW_OP_");
      break;
    }

    switch (op) {
    case DW_OP_addr:
      *s << "DW_OP_addr(" << m_data.GetAddress(&offset) << ") ";
      break; // 0x03 1 address
    case DW_OP_deref:
      *s << "DW_OP_deref";
      break; // 0x06
    case DW_OP_const1u:
      s->Printf("DW_OP_const1u(0x%2.2x)", m_data.GetU8(&offset));
      break; // 0x08 1 1-byte constant
    case DW_OP_const1s:
      s->Printf("DW_OP_const1s(0x%2.2x)", m_data.GetU8(&offset));
      break; // 0x09 1 1-byte constant
    case DW_OP_const2u:
      s->Printf("DW_OP_const2u(0x%4.4x)", m_data.GetU16(&offset));
      break; // 0x0a 1 2-byte constant
    case DW_OP_const2s:
      s->Printf("DW_OP_const2s(0x%4.4x)", m_data.GetU16(&offset));
      break; // 0x0b 1 2-byte constant
    case DW_OP_const4u:
      s->Printf("DW_OP_const4u(0x%8.8x)", m_data.GetU32(&offset));
      break; // 0x0c 1 4-byte constant
    case DW_OP_const4s:
      s->Printf("DW_OP_const4s(0x%8.8x)", m_data.GetU32(&offset));
      break; // 0x0d 1 4-byte constant
    case DW_OP_const8u:
      s->Printf("DW_OP_const8u(0x%16.16" PRIx64 ")", m_data.GetU64(&offset));
      break; // 0x0e 1 8-byte constant
    case DW_OP_const8s:
      s->Printf("DW_OP_const8s(0x%16.16" PRIx64 ")", m_data.GetU64(&offset));
      break; // 0x0f 1 8-byte constant
    case DW_OP_constu:
      s->Printf("DW_OP_constu(0x%" PRIx64 ")", m_data.GetULEB128(&offset));
      break; // 0x10 1 ULEB128 constant
    case DW_OP_consts:
      s->Printf("DW_OP_consts(0x%" PRId64 ")", m_data.GetSLEB128(&offset));
      break; // 0x11 1 SLEB128 constant
    case DW_OP_dup:
      s->PutCString("DW_OP_dup");
      break; // 0x12
    case DW_OP_drop:
      s->PutCString("DW_OP_drop");
      break; // 0x13
    case DW_OP_over:
      s->PutCString("DW_OP_over");
      break; // 0x14
    case DW_OP_pick:
      s->Printf("DW_OP_pick(0x%2.2x)", m_data.GetU8(&offset));
      break; // 0x15 1 1-byte stack index
    case DW_OP_swap:
      s->PutCString("DW_OP_swap");
      break; // 0x16
    case DW_OP_rot:
      s->PutCString("DW_OP_rot");
      break; // 0x17
    case DW_OP_xderef:
      s->PutCString("DW_OP_xderef");
      break; // 0x18
    case DW_OP_abs:
      s->PutCString("DW_OP_abs");
      break; // 0x19
    case DW_OP_and:
      s->PutCString("DW_OP_and");
      break; // 0x1a
    case DW_OP_div:
      s->PutCString("DW_OP_div");
      break; // 0x1b
    case DW_OP_minus:
      s->PutCString("DW_OP_minus");
      break; // 0x1c
    case DW_OP_mod:
      s->PutCString("DW_OP_mod");
      break; // 0x1d
    case DW_OP_mul:
      s->PutCString("DW_OP_mul");
      break; // 0x1e
    case DW_OP_neg:
      s->PutCString("DW_OP_neg");
      break; // 0x1f
    case DW_OP_not:
      s->PutCString("DW_OP_not");
      break; // 0x20
    case DW_OP_or:
      s->PutCString("DW_OP_or");
      break; // 0x21
    case DW_OP_plus:
      s->PutCString("DW_OP_plus");
      break;                // 0x22
    case DW_OP_plus_uconst: // 0x23 1 ULEB128 addend
      s->Printf("DW_OP_plus_uconst(0x%" PRIx64 ")",
                m_data.GetULEB128(&offset));
      break;

    case DW_OP_shl:
      s->PutCString("DW_OP_shl");
      break; // 0x24
    case DW_OP_shr:
      s->PutCString("DW_OP_shr");
      break; // 0x25
    case DW_OP_shra:
      s->PutCString("DW_OP_shra");
      break; // 0x26
    case DW_OP_xor:
      s->PutCString("DW_OP_xor");
      break; // 0x27
    case DW_OP_skip:
      s->Printf("DW_OP_skip(0x%4.4x)", m_data.GetU16(&offset));
      break; // 0x2f 1 signed 2-byte constant
    case DW_OP_bra:
      s->Printf("DW_OP_bra(0x%4.4x)", m_data.GetU16(&offset));
      break; // 0x28 1 signed 2-byte constant
    case DW_OP_eq:
      s->PutCString("DW_OP_eq");
      break; // 0x29
    case DW_OP_ge:
      s->PutCString("DW_OP_ge");
      break; // 0x2a
    case DW_OP_gt:
      s->PutCString("DW_OP_gt");
      break; // 0x2b
    case DW_OP_le:
      s->PutCString("DW_OP_le");
      break; // 0x2c
    case DW_OP_lt:
      s->PutCString("DW_OP_lt");
      break; // 0x2d
    case DW_OP_ne:
      s->PutCString("DW_OP_ne");
      break; // 0x2e

    case DW_OP_lit0:  // 0x30
    case DW_OP_lit1:  // 0x31
    case DW_OP_lit2:  // 0x32
    case DW_OP_lit3:  // 0x33
    case DW_OP_lit4:  // 0x34
    case DW_OP_lit5:  // 0x35
    case DW_OP_lit6:  // 0x36
    case DW_OP_lit7:  // 0x37
    case DW_OP_lit8:  // 0x38
    case DW_OP_lit9:  // 0x39
    case DW_OP_lit10: // 0x3A
    case DW_OP_lit11: // 0x3B
    case DW_OP_lit12: // 0x3C
    case DW_OP_lit13: // 0x3D
    case DW_OP_lit14: // 0x3E
    case DW_OP_lit15: // 0x3F
    case DW_OP_lit16: // 0x40
    case DW_OP_lit17: // 0x41
    case DW_OP_lit18: // 0x42
    case DW_OP_lit19: // 0x43
    case DW_OP_lit20: // 0x44
    case DW_OP_lit21: // 0x45
    case DW_OP_lit22: // 0x46
    case DW_OP_lit23: // 0x47
    case DW_OP_lit24: // 0x48
    case DW_OP_lit25: // 0x49
    case DW_OP_lit26: // 0x4A
    case DW_OP_lit27: // 0x4B
    case DW_OP_lit28: // 0x4C
    case DW_OP_lit29: // 0x4D
    case DW_OP_lit30: // 0x4E
    case DW_OP_lit31:
      s->Printf("DW_OP_lit%i", op - DW_OP_lit0);
      break; // 0x4f

    case DW_OP_reg0:  // 0x50
    case DW_OP_reg1:  // 0x51
    case DW_OP_reg2:  // 0x52
    case DW_OP_reg3:  // 0x53
    case DW_OP_reg4:  // 0x54
    case DW_OP_reg5:  // 0x55
    case DW_OP_reg6:  // 0x56
    case DW_OP_reg7:  // 0x57
    case DW_OP_reg8:  // 0x58
    case DW_OP_reg9:  // 0x59
    case DW_OP_reg10: // 0x5A
    case DW_OP_reg11: // 0x5B
    case DW_OP_reg12: // 0x5C
    case DW_OP_reg13: // 0x5D
    case DW_OP_reg14: // 0x5E
    case DW_OP_reg15: // 0x5F
    case DW_OP_reg16: // 0x60
    case DW_OP_reg17: // 0x61
    case DW_OP_reg18: // 0x62
    case DW_OP_reg19: // 0x63
    case DW_OP_reg20: // 0x64
    case DW_OP_reg21: // 0x65
    case DW_OP_reg22: // 0x66
    case DW_OP_reg23: // 0x67
    case DW_OP_reg24: // 0x68
    case DW_OP_reg25: // 0x69
    case DW_OP_reg26: // 0x6A
    case DW_OP_reg27: // 0x6B
    case DW_OP_reg28: // 0x6C
    case DW_OP_reg29: // 0x6D
    case DW_OP_reg30: // 0x6E
    case DW_OP_reg31: // 0x6F
    {
      uint32_t reg_num = op - DW_OP_reg0;
      if (abi) {
        RegisterInfo reg_info;
        if (abi->GetRegisterInfoByKind(m_reg_kind, reg_num, reg_info)) {
          if (reg_info.name) {
            s->PutCString(reg_info.name);
            break;
          } else if (reg_info.alt_name) {
            s->PutCString(reg_info.alt_name);
            break;
          }
        }
      }
      s->Printf("DW_OP_reg%u", reg_num);
      break;
    } break;

    case DW_OP_breg0:
    case DW_OP_breg1:
    case DW_OP_breg2:
    case DW_OP_breg3:
    case DW_OP_breg4:
    case DW_OP_breg5:
    case DW_OP_breg6:
    case DW_OP_breg7:
    case DW_OP_breg8:
    case DW_OP_breg9:
    case DW_OP_breg10:
    case DW_OP_breg11:
    case DW_OP_breg12:
    case DW_OP_breg13:
    case DW_OP_breg14:
    case DW_OP_breg15:
    case DW_OP_breg16:
    case DW_OP_breg17:
    case DW_OP_breg18:
    case DW_OP_breg19:
    case DW_OP_breg20:
    case DW_OP_breg21:
    case DW_OP_breg22:
    case DW_OP_breg23:
    case DW_OP_breg24:
    case DW_OP_breg25:
    case DW_OP_breg26:
    case DW_OP_breg27:
    case DW_OP_breg28:
    case DW_OP_breg29:
    case DW_OP_breg30:
    case DW_OP_breg31: {
      uint32_t reg_num = op - DW_OP_breg0;
      int64_t reg_offset = m_data.GetSLEB128(&offset);
      if (abi) {
        RegisterInfo reg_info;
        if (abi->GetRegisterInfoByKind(m_reg_kind, reg_num, reg_info)) {
          if (reg_info.name) {
            s->Printf("[%s%+" PRIi64 "]", reg_info.name, reg_offset);
            break;
          } else if (reg_info.alt_name) {
            s->Printf("[%s%+" PRIi64 "]", reg_info.alt_name, reg_offset);
            break;
          }
        }
      }
      s->Printf("DW_OP_breg%i(0x%" PRIx64 ")", reg_num, reg_offset);
    } break;

    case DW_OP_regx: // 0x90 1 ULEB128 register
    {
      uint32_t reg_num = m_data.GetULEB128(&offset);
      if (abi) {
        RegisterInfo reg_info;
        if (abi->GetRegisterInfoByKind(m_reg_kind, reg_num, reg_info)) {
          if (reg_info.name) {
            s->PutCString(reg_info.name);
            break;
          } else if (reg_info.alt_name) {
            s->PutCString(reg_info.alt_name);
            break;
          }
        }
      }
      s->Printf("DW_OP_regx(%" PRIu32 ")", reg_num);
      break;
    } break;
    case DW_OP_fbreg: // 0x91 1 SLEB128 offset
      s->Printf("DW_OP_fbreg(%" PRIi64 ")", m_data.GetSLEB128(&offset));
      break;
    case DW_OP_bregx: // 0x92 2 ULEB128 register followed by SLEB128 offset
    {
      uint32_t reg_num = m_data.GetULEB128(&offset);
      int64_t reg_offset = m_data.GetSLEB128(&offset);
      if (abi) {
        RegisterInfo reg_info;
        if (abi->GetRegisterInfoByKind(m_reg_kind, reg_num, reg_info)) {
          if (reg_info.name) {
            s->Printf("[%s%+" PRIi64 "]", reg_info.name, reg_offset);
            break;
          } else if (reg_info.alt_name) {
            s->Printf("[%s%+" PRIi64 "]", reg_info.alt_name, reg_offset);
            break;
          }
        }
      }
      s->Printf("DW_OP_bregx(reg=%" PRIu32 ",offset=%" PRIi64 ")", reg_num,
                reg_offset);
    } break;
    case DW_OP_piece: // 0x93 1 ULEB128 size of piece addressed
      s->Printf("DW_OP_piece(0x%" PRIx64 ")", m_data.GetULEB128(&offset));
      break;
    case DW_OP_deref_size: // 0x94 1 1-byte size of data retrieved
      s->Printf("DW_OP_deref_size(0x%2.2x)", m_data.GetU8(&offset));
      break;
    case DW_OP_xderef_size: // 0x95 1 1-byte size of data retrieved
      s->Printf("DW_OP_xderef_size(0x%2.2x)", m_data.GetU8(&offset));
      break;
    case DW_OP_nop:
      s->PutCString("DW_OP_nop");
      break; // 0x96
    case DW_OP_push_object_address:
      s->PutCString("DW_OP_push_object_address");
      break;          // 0x97 DWARF3
    case DW_OP_call2: // 0x98 DWARF3 1 2-byte offset of DIE
      s->Printf("DW_OP_call2(0x%4.4x)", m_data.GetU16(&offset));
      break;
    case DW_OP_call4: // 0x99 DWARF3 1 4-byte offset of DIE
      s->Printf("DW_OP_call4(0x%8.8x)", m_data.GetU32(&offset));
      break;
    case DW_OP_call_ref: // 0x9a DWARF3 1 4- or 8-byte offset of DIE
      s->Printf("DW_OP_call_ref(0x%8.8" PRIx64 ")", m_data.GetAddress(&offset));
      break;
    //      case DW_OP_call_frame_cfa: s << "call_frame_cfa"; break;
    //      // 0x9c DWARF3
    //      case DW_OP_bit_piece: // 0x9d DWARF3 2
    //          s->Printf("DW_OP_bit_piece(0x%x, 0x%x)",
    //          m_data.GetULEB128(&offset), m_data.GetULEB128(&offset));
    //          break;
    //      case DW_OP_lo_user:     s->PutCString("DW_OP_lo_user"); break;
    //      // 0xe0
    //      case DW_OP_hi_user:     s->PutCString("DW_OP_hi_user"); break;
    //      // 0xff
    //        case DW_OP_APPLE_extern:
    //            s->Printf("DW_OP_APPLE_extern(%" PRIu64 ")",
    //            m_data.GetULEB128(&offset));
    //            break;
    //        case DW_OP_APPLE_array_ref:
    //            s->PutCString("DW_OP_APPLE_array_ref");
    //            break;
    case DW_OP_form_tls_address:
      s->PutCString("DW_OP_form_tls_address"); // 0x9b
      break;
    case DW_OP_GNU_addr_index: // 0xfb
      s->Printf("DW_OP_GNU_addr_index(0x%" PRIx64 ")",
                m_data.GetULEB128(&offset));
      break;
    case DW_OP_GNU_const_index: // 0xfc
      s->Printf("DW_OP_GNU_const_index(0x%" PRIx64 ")",
                m_data.GetULEB128(&offset));
      break;
    case DW_OP_GNU_push_tls_address:
      s->PutCString("DW_OP_GNU_push_tls_address"); // 0xe0
      break;
    case DW_OP_APPLE_uninit:
      s->PutCString("DW_OP_APPLE_uninit"); // 0xF0
      break;
      //        case DW_OP_APPLE_assign:        // 0xF1 - pops value off and
      //        assigns it to second item on stack (2nd item must have
      //        assignable context)
      //            s->PutCString("DW_OP_APPLE_assign");
      //            break;
      //        case DW_OP_APPLE_address_of:    // 0xF2 - gets the address of
      //        the top stack item (top item must be a variable, or have
      //        value_type that is an address already)
      //            s->PutCString("DW_OP_APPLE_address_of");
      //            break;
      //        case DW_OP_APPLE_value_of:      // 0xF3 - pops the value off the
      //        stack and pushes the value of that object (top item must be a
      //        variable, or expression local)
      //            s->PutCString("DW_OP_APPLE_value_of");
      //            break;
      //        case DW_OP_APPLE_deref_type:    // 0xF4 - gets the address of
      //        the top stack item (top item must be a variable, or a clang
      //        type)
      //            s->PutCString("DW_OP_APPLE_deref_type");
      //            break;
      //        case DW_OP_APPLE_expr_local:    // 0xF5 - ULEB128 expression
      //        local index
      //            s->Printf("DW_OP_APPLE_expr_local(%" PRIu64 ")",
      //            m_data.GetULEB128(&offset));
      //            break;
      //        case DW_OP_APPLE_constf:        // 0xF6 - 1 byte float size,
      //        followed by constant float data
      //            {
      //                uint8_t float_length = m_data.GetU8(&offset);
      //                s->Printf("DW_OP_APPLE_constf(<%u> ", float_length);
      //                m_data.Dump(s, offset, eFormatHex, float_length, 1,
      //                UINT32_MAX, DW_INVALID_ADDRESS, 0, 0);
      //                s->PutChar(')');
      //                // Consume the float data
      //                m_data.GetData(&offset, float_length);
      //            }
      //            break;
      //        case DW_OP_APPLE_scalar_cast:
      //            s->Printf("DW_OP_APPLE_scalar_cast(%s)",
      //            Scalar::GetValueTypeAsCString
      //            ((Scalar::Type)m_data.GetU8(&offset)));
      //            break;
      //        case DW_OP_APPLE_clang_cast:
      //            {
      //                clang::Type *clang_type = (clang::Type
      //                *)m_data.GetMaxU64(&offset, sizeof(void*));
      //                s->Printf("DW_OP_APPLE_clang_cast(%p)", clang_type);
      //            }
      //            break;
      //        case DW_OP_APPLE_clear:
      //            s->PutCString("DW_OP_APPLE_clear");
      //            break;
      //        case DW_OP_APPLE_error:         // 0xFF - Stops expression
      //        evaluation and returns an error (no args)
      //            s->PutCString("DW_OP_APPLE_error");
      //            break;
    }
  }
}

void DWARFExpression::SetLocationListSlide(addr_t slide) {
  m_loclist_slide = slide;
}

int DWARFExpression::GetRegisterKind() { return m_reg_kind; }

void DWARFExpression::SetRegisterKind(RegisterKind reg_kind) {
  m_reg_kind = reg_kind;
}

bool DWARFExpression::IsLocationList() const {
  return m_loclist_slide != LLDB_INVALID_ADDRESS;
}

void DWARFExpression::GetDescription(Stream *s, lldb::DescriptionLevel level,
                                     addr_t location_list_base_addr,
                                     ABI *abi) const {
  if (IsLocationList()) {
    // We have a location list
    lldb::offset_t offset = 0;
    uint32_t count = 0;
    addr_t curr_base_addr = location_list_base_addr;
    while (m_data.ValidOffset(offset)) {
      addr_t begin_addr_offset = LLDB_INVALID_ADDRESS;
      addr_t end_addr_offset = LLDB_INVALID_ADDRESS;
      if (!AddressRangeForLocationListEntry(m_dwarf_cu, m_data, &offset,
                                            begin_addr_offset, end_addr_offset))
        break;

      if (begin_addr_offset == 0 && end_addr_offset == 0)
        break;

      if (begin_addr_offset < end_addr_offset) {
        if (count > 0)
          s->PutCString(", ");
        VMRange addr_range(curr_base_addr + begin_addr_offset,
                           curr_base_addr + end_addr_offset);
        addr_range.Dump(s, 0, 8);
        s->PutChar('{');
        lldb::offset_t location_length = m_data.GetU16(&offset);
        DumpLocation(s, offset, location_length, level, abi);
        s->PutChar('}');
        offset += location_length;
      } else {
        if ((m_data.GetAddressByteSize() == 4 &&
             (begin_addr_offset == UINT32_MAX)) ||
            (m_data.GetAddressByteSize() == 8 &&
             (begin_addr_offset == UINT64_MAX))) {
          curr_base_addr = end_addr_offset + location_list_base_addr;
          // We have a new base address
          if (count > 0)
            s->PutCString(", ");
          *s << "base_addr = " << end_addr_offset;
        }
      }

      count++;
    }
  } else {
    // We have a normal location that contains DW_OP location opcodes
    DumpLocation(s, 0, m_data.GetByteSize(), level, abi);
  }
}

static bool ReadRegisterValueAsScalar(RegisterContext *reg_ctx,
                                      lldb::RegisterKind reg_kind,
                                      uint32_t reg_num, Status *error_ptr,
                                      Value &value) {
  if (reg_ctx == NULL) {
    if (error_ptr)
      error_ptr->SetErrorStringWithFormat("No register context in frame.\n");
  } else {
    uint32_t native_reg =
        reg_ctx->ConvertRegisterKindToRegisterNumber(reg_kind, reg_num);
    if (native_reg == LLDB_INVALID_REGNUM) {
      if (error_ptr)
        error_ptr->SetErrorStringWithFormat("Unable to convert register "
                                            "kind=%u reg_num=%u to a native "
                                            "register number.\n",
                                            reg_kind, reg_num);
    } else {
      const RegisterInfo *reg_info =
          reg_ctx->GetRegisterInfoAtIndex(native_reg);
      RegisterValue reg_value;
      if (reg_ctx->ReadRegister(reg_info, reg_value)) {
        if (reg_value.GetScalarValue(value.GetScalar())) {
          value.SetValueType(Value::eValueTypeScalar);
          value.SetContext(Value::eContextTypeRegisterInfo,
                           const_cast<RegisterInfo *>(reg_info));
          if (error_ptr)
            error_ptr->Clear();
          return true;
        } else {
          // If we get this error, then we need to implement a value buffer in
          // the dwarf expression evaluation function...
          if (error_ptr)
            error_ptr->SetErrorStringWithFormat(
                "register %s can't be converted to a scalar value",
                reg_info->name);
        }
      } else {
        if (error_ptr)
          error_ptr->SetErrorStringWithFormat("register %s is not available",
                                              reg_info->name);
      }
    }
  }
  return false;
}

// bool
// DWARFExpression::LocationListContainsLoadAddress (Process* process, const
// Address &addr) const
//{
//    return LocationListContainsLoadAddress(process,
//    addr.GetLoadAddress(process));
//}
//
// bool
// DWARFExpression::LocationListContainsLoadAddress (Process* process, addr_t
// load_addr) const
//{
//    if (load_addr == LLDB_INVALID_ADDRESS)
//        return false;
//
//    if (IsLocationList())
//    {
//        lldb::offset_t offset = 0;
//
//        addr_t loc_list_base_addr = m_loclist_slide.GetLoadAddress(process);
//
//        if (loc_list_base_addr == LLDB_INVALID_ADDRESS)
//            return false;
//
//        while (m_data.ValidOffset(offset))
//        {
//            // We need to figure out what the value is for the location.
//            addr_t lo_pc = m_data.GetAddress(&offset);
//            addr_t hi_pc = m_data.GetAddress(&offset);
//            if (lo_pc == 0 && hi_pc == 0)
//                break;
//            else
//            {
//                lo_pc += loc_list_base_addr;
//                hi_pc += loc_list_base_addr;
//
//                if (lo_pc <= load_addr && load_addr < hi_pc)
//                    return true;
//
//                offset += m_data.GetU16(&offset);
//            }
//        }
//    }
//    return false;
//}

static offset_t GetOpcodeDataSize(const DataExtractor &data,
                                  const lldb::offset_t data_offset,
                                  const uint8_t op) {
  lldb::offset_t offset = data_offset;
  switch (op) {
  case DW_OP_addr:
  case DW_OP_call_ref: // 0x9a 1 address sized offset of DIE (DWARF3)
    return data.GetAddressByteSize();

  // Opcodes with no arguments
  case DW_OP_deref:                // 0x06
  case DW_OP_dup:                  // 0x12
  case DW_OP_drop:                 // 0x13
  case DW_OP_over:                 // 0x14
  case DW_OP_swap:                 // 0x16
  case DW_OP_rot:                  // 0x17
  case DW_OP_xderef:               // 0x18
  case DW_OP_abs:                  // 0x19
  case DW_OP_and:                  // 0x1a
  case DW_OP_div:                  // 0x1b
  case DW_OP_minus:                // 0x1c
  case DW_OP_mod:                  // 0x1d
  case DW_OP_mul:                  // 0x1e
  case DW_OP_neg:                  // 0x1f
  case DW_OP_not:                  // 0x20
  case DW_OP_or:                   // 0x21
  case DW_OP_plus:                 // 0x22
  case DW_OP_shl:                  // 0x24
  case DW_OP_shr:                  // 0x25
  case DW_OP_shra:                 // 0x26
  case DW_OP_xor:                  // 0x27
  case DW_OP_eq:                   // 0x29
  case DW_OP_ge:                   // 0x2a
  case DW_OP_gt:                   // 0x2b
  case DW_OP_le:                   // 0x2c
  case DW_OP_lt:                   // 0x2d
  case DW_OP_ne:                   // 0x2e
  case DW_OP_lit0:                 // 0x30
  case DW_OP_lit1:                 // 0x31
  case DW_OP_lit2:                 // 0x32
  case DW_OP_lit3:                 // 0x33
  case DW_OP_lit4:                 // 0x34
  case DW_OP_lit5:                 // 0x35
  case DW_OP_lit6:                 // 0x36
  case DW_OP_lit7:                 // 0x37
  case DW_OP_lit8:                 // 0x38
  case DW_OP_lit9:                 // 0x39
  case DW_OP_lit10:                // 0x3A
  case DW_OP_lit11:                // 0x3B
  case DW_OP_lit12:                // 0x3C
  case DW_OP_lit13:                // 0x3D
  case DW_OP_lit14:                // 0x3E
  case DW_OP_lit15:                // 0x3F
  case DW_OP_lit16:                // 0x40
  case DW_OP_lit17:                // 0x41
  case DW_OP_lit18:                // 0x42
  case DW_OP_lit19:                // 0x43
  case DW_OP_lit20:                // 0x44
  case DW_OP_lit21:                // 0x45
  case DW_OP_lit22:                // 0x46
  case DW_OP_lit23:                // 0x47
  case DW_OP_lit24:                // 0x48
  case DW_OP_lit25:                // 0x49
  case DW_OP_lit26:                // 0x4A
  case DW_OP_lit27:                // 0x4B
  case DW_OP_lit28:                // 0x4C
  case DW_OP_lit29:                // 0x4D
  case DW_OP_lit30:                // 0x4E
  case DW_OP_lit31:                // 0x4f
  case DW_OP_reg0:                 // 0x50
  case DW_OP_reg1:                 // 0x51
  case DW_OP_reg2:                 // 0x52
  case DW_OP_reg3:                 // 0x53
  case DW_OP_reg4:                 // 0x54
  case DW_OP_reg5:                 // 0x55
  case DW_OP_reg6:                 // 0x56
  case DW_OP_reg7:                 // 0x57
  case DW_OP_reg8:                 // 0x58
  case DW_OP_reg9:                 // 0x59
  case DW_OP_reg10:                // 0x5A
  case DW_OP_reg11:                // 0x5B
  case DW_OP_reg12:                // 0x5C
  case DW_OP_reg13:                // 0x5D
  case DW_OP_reg14:                // 0x5E
  case DW_OP_reg15:                // 0x5F
  case DW_OP_reg16:                // 0x60
  case DW_OP_reg17:                // 0x61
  case DW_OP_reg18:                // 0x62
  case DW_OP_reg19:                // 0x63
  case DW_OP_reg20:                // 0x64
  case DW_OP_reg21:                // 0x65
  case DW_OP_reg22:                // 0x66
  case DW_OP_reg23:                // 0x67
  case DW_OP_reg24:                // 0x68
  case DW_OP_reg25:                // 0x69
  case DW_OP_reg26:                // 0x6A
  case DW_OP_reg27:                // 0x6B
  case DW_OP_reg28:                // 0x6C
  case DW_OP_reg29:                // 0x6D
  case DW_OP_reg30:                // 0x6E
  case DW_OP_reg31:                // 0x6F
  case DW_OP_nop:                  // 0x96
  case DW_OP_push_object_address:  // 0x97 DWARF3
  case DW_OP_form_tls_address:     // 0x9b DWARF3
  case DW_OP_call_frame_cfa:       // 0x9c DWARF3
  case DW_OP_stack_value:          // 0x9f DWARF4
  case DW_OP_GNU_push_tls_address: // 0xe0 GNU extension
    return 0;

  // Opcodes with a single 1 byte arguments
  case DW_OP_const1u:     // 0x08 1 1-byte constant
  case DW_OP_const1s:     // 0x09 1 1-byte constant
  case DW_OP_pick:        // 0x15 1 1-byte stack index
  case DW_OP_deref_size:  // 0x94 1 1-byte size of data retrieved
  case DW_OP_xderef_size: // 0x95 1 1-byte size of data retrieved
    return 1;

  // Opcodes with a single 2 byte arguments
  case DW_OP_const2u: // 0x0a 1 2-byte constant
  case DW_OP_const2s: // 0x0b 1 2-byte constant
  case DW_OP_skip:    // 0x2f 1 signed 2-byte constant
  case DW_OP_bra:     // 0x28 1 signed 2-byte constant
  case DW_OP_call2:   // 0x98 1 2-byte offset of DIE (DWARF3)
    return 2;

  // Opcodes with a single 4 byte arguments
  case DW_OP_const4u: // 0x0c 1 4-byte constant
  case DW_OP_const4s: // 0x0d 1 4-byte constant
  case DW_OP_call4:   // 0x99 1 4-byte offset of DIE (DWARF3)
    return 4;

  // Opcodes with a single 8 byte arguments
  case DW_OP_const8u: // 0x0e 1 8-byte constant
  case DW_OP_const8s: // 0x0f 1 8-byte constant
    return 8;

  // All opcodes that have a single ULEB (signed or unsigned) argument
  case DW_OP_constu:          // 0x10 1 ULEB128 constant
  case DW_OP_consts:          // 0x11 1 SLEB128 constant
  case DW_OP_plus_uconst:     // 0x23 1 ULEB128 addend
  case DW_OP_breg0:           // 0x70 1 ULEB128 register
  case DW_OP_breg1:           // 0x71 1 ULEB128 register
  case DW_OP_breg2:           // 0x72 1 ULEB128 register
  case DW_OP_breg3:           // 0x73 1 ULEB128 register
  case DW_OP_breg4:           // 0x74 1 ULEB128 register
  case DW_OP_breg5:           // 0x75 1 ULEB128 register
  case DW_OP_breg6:           // 0x76 1 ULEB128 register
  case DW_OP_breg7:           // 0x77 1 ULEB128 register
  case DW_OP_breg8:           // 0x78 1 ULEB128 register
  case DW_OP_breg9:           // 0x79 1 ULEB128 register
  case DW_OP_breg10:          // 0x7a 1 ULEB128 register
  case DW_OP_breg11:          // 0x7b 1 ULEB128 register
  case DW_OP_breg12:          // 0x7c 1 ULEB128 register
  case DW_OP_breg13:          // 0x7d 1 ULEB128 register
  case DW_OP_breg14:          // 0x7e 1 ULEB128 register
  case DW_OP_breg15:          // 0x7f 1 ULEB128 register
  case DW_OP_breg16:          // 0x80 1 ULEB128 register
  case DW_OP_breg17:          // 0x81 1 ULEB128 register
  case DW_OP_breg18:          // 0x82 1 ULEB128 register
  case DW_OP_breg19:          // 0x83 1 ULEB128 register
  case DW_OP_breg20:          // 0x84 1 ULEB128 register
  case DW_OP_breg21:          // 0x85 1 ULEB128 register
  case DW_OP_breg22:          // 0x86 1 ULEB128 register
  case DW_OP_breg23:          // 0x87 1 ULEB128 register
  case DW_OP_breg24:          // 0x88 1 ULEB128 register
  case DW_OP_breg25:          // 0x89 1 ULEB128 register
  case DW_OP_breg26:          // 0x8a 1 ULEB128 register
  case DW_OP_breg27:          // 0x8b 1 ULEB128 register
  case DW_OP_breg28:          // 0x8c 1 ULEB128 register
  case DW_OP_breg29:          // 0x8d 1 ULEB128 register
  case DW_OP_breg30:          // 0x8e 1 ULEB128 register
  case DW_OP_breg31:          // 0x8f 1 ULEB128 register
  case DW_OP_regx:            // 0x90 1 ULEB128 register
  case DW_OP_fbreg:           // 0x91 1 SLEB128 offset
  case DW_OP_piece:           // 0x93 1 ULEB128 size of piece addressed
  case DW_OP_GNU_addr_index:  // 0xfb 1 ULEB128 index
  case DW_OP_GNU_const_index: // 0xfc 1 ULEB128 index
    data.Skip_LEB128(&offset);
    return offset - data_offset;

  // All opcodes that have a 2 ULEB (signed or unsigned) arguments
  case DW_OP_bregx:     // 0x92 2 ULEB128 register followed by SLEB128 offset
  case DW_OP_bit_piece: // 0x9d ULEB128 bit size, ULEB128 bit offset (DWARF3);
    data.Skip_LEB128(&offset);
    data.Skip_LEB128(&offset);
    return offset - data_offset;

  case DW_OP_implicit_value: // 0x9e ULEB128 size followed by block of that size
                             // (DWARF4)
  {
    uint64_t block_len = data.Skip_LEB128(&offset);
    offset += block_len;
    return offset - data_offset;
  }

  default:
    break;
  }
  return LLDB_INVALID_OFFSET;
}

lldb::addr_t DWARFExpression::GetLocation_DW_OP_addr(uint32_t op_addr_idx,
                                                     bool &error) const {
  error = false;
  if (IsLocationList())
    return LLDB_INVALID_ADDRESS;
  lldb::offset_t offset = 0;
  uint32_t curr_op_addr_idx = 0;
  while (m_data.ValidOffset(offset)) {
    const uint8_t op = m_data.GetU8(&offset);

    if (op == DW_OP_addr) {
      const lldb::addr_t op_file_addr = m_data.GetAddress(&offset);
      if (curr_op_addr_idx == op_addr_idx)
        return op_file_addr;
      else
        ++curr_op_addr_idx;
    } else if (op == DW_OP_GNU_addr_index) {
      uint64_t index = m_data.GetULEB128(&offset);
      if (curr_op_addr_idx == op_addr_idx) {
        if (!m_dwarf_cu) {
          error = true;
          break;
        }

        return ReadAddressFromDebugAddrSection(m_dwarf_cu, index);
      } else
        ++curr_op_addr_idx;
    } else {
      const offset_t op_arg_size = GetOpcodeDataSize(m_data, offset, op);
      if (op_arg_size == LLDB_INVALID_OFFSET) {
        error = true;
        break;
      }
      offset += op_arg_size;
    }
  }
  return LLDB_INVALID_ADDRESS;
}

bool DWARFExpression::Update_DW_OP_addr(lldb::addr_t file_addr) {
  if (IsLocationList())
    return false;
  lldb::offset_t offset = 0;
  while (m_data.ValidOffset(offset)) {
    const uint8_t op = m_data.GetU8(&offset);

    if (op == DW_OP_addr) {
      const uint32_t addr_byte_size = m_data.GetAddressByteSize();
      // We have to make a copy of the data as we don't know if this data is
      // from a read only memory mapped buffer, so we duplicate all of the data
      // first, then modify it, and if all goes well, we then replace the data
      // for this expression

      // So first we copy the data into a heap buffer
      std::unique_ptr<DataBufferHeap> head_data_ap(
          new DataBufferHeap(m_data.GetDataStart(), m_data.GetByteSize()));

      // Make en encoder so we can write the address into the buffer using the
      // correct byte order (endianness)
      DataEncoder encoder(head_data_ap->GetBytes(), head_data_ap->GetByteSize(),
                          m_data.GetByteOrder(), addr_byte_size);

      // Replace the address in the new buffer
      if (encoder.PutMaxU64(offset, addr_byte_size, file_addr) == UINT32_MAX)
        return false;

      // All went well, so now we can reset the data using a shared pointer to
      // the heap data so "m_data" will now correctly manage the heap data.
      m_data.SetData(DataBufferSP(head_data_ap.release()));
      return true;
    } else {
      const offset_t op_arg_size = GetOpcodeDataSize(m_data, offset, op);
      if (op_arg_size == LLDB_INVALID_OFFSET)
        break;
      offset += op_arg_size;
    }
  }
  return false;
}

bool DWARFExpression::ContainsThreadLocalStorage() const {
  // We are assuming for now that any thread local variable will not have a
  // location list. This has been true for all thread local variables we have
  // seen so far produced by any compiler.
  if (IsLocationList())
    return false;
  lldb::offset_t offset = 0;
  while (m_data.ValidOffset(offset)) {
    const uint8_t op = m_data.GetU8(&offset);

    if (op == DW_OP_form_tls_address || op == DW_OP_GNU_push_tls_address)
      return true;
    const offset_t op_arg_size = GetOpcodeDataSize(m_data, offset, op);
    if (op_arg_size == LLDB_INVALID_OFFSET)
      return false;
    else
      offset += op_arg_size;
  }
  return false;
}
bool DWARFExpression::LinkThreadLocalStorage(
    lldb::ModuleSP new_module_sp,
    std::function<lldb::addr_t(lldb::addr_t file_addr)> const
        &link_address_callback) {
  // We are assuming for now that any thread local variable will not have a
  // location list. This has been true for all thread local variables we have
  // seen so far produced by any compiler.
  if (IsLocationList())
    return false;

  const uint32_t addr_byte_size = m_data.GetAddressByteSize();
  // We have to make a copy of the data as we don't know if this data is from a
  // read only memory mapped buffer, so we duplicate all of the data first,
  // then modify it, and if all goes well, we then replace the data for this
  // expression

  // So first we copy the data into a heap buffer
  std::shared_ptr<DataBufferHeap> heap_data_sp(
      new DataBufferHeap(m_data.GetDataStart(), m_data.GetByteSize()));

  // Make en encoder so we can write the address into the buffer using the
  // correct byte order (endianness)
  DataEncoder encoder(heap_data_sp->GetBytes(), heap_data_sp->GetByteSize(),
                      m_data.GetByteOrder(), addr_byte_size);

  lldb::offset_t offset = 0;
  lldb::offset_t const_offset = 0;
  lldb::addr_t const_value = 0;
  size_t const_byte_size = 0;
  while (m_data.ValidOffset(offset)) {
    const uint8_t op = m_data.GetU8(&offset);

    bool decoded_data = false;
    switch (op) {
    case DW_OP_const4u:
      // Remember the const offset in case we later have a
      // DW_OP_form_tls_address or DW_OP_GNU_push_tls_address
      const_offset = offset;
      const_value = m_data.GetU32(&offset);
      decoded_data = true;
      const_byte_size = 4;
      break;

    case DW_OP_const8u:
      // Remember the const offset in case we later have a
      // DW_OP_form_tls_address or DW_OP_GNU_push_tls_address
      const_offset = offset;
      const_value = m_data.GetU64(&offset);
      decoded_data = true;
      const_byte_size = 8;
      break;

    case DW_OP_form_tls_address:
    case DW_OP_GNU_push_tls_address:
      // DW_OP_form_tls_address and DW_OP_GNU_push_tls_address must be preceded
      // by a file address on the stack. We assume that DW_OP_const4u or
      // DW_OP_const8u is used for these values, and we check that the last
      // opcode we got before either of these was DW_OP_const4u or
      // DW_OP_const8u. If so, then we can link the value accodingly. For
      // Darwin, the value in the DW_OP_const4u or DW_OP_const8u is the file
      // address of a structure that contains a function pointer, the pthread
      // key and the offset into the data pointed to by the pthread key. So we
      // must link this address and also set the module of this expression to
      // the new_module_sp so we can resolve the file address correctly
      if (const_byte_size > 0) {
        lldb::addr_t linked_file_addr = link_address_callback(const_value);
        if (linked_file_addr == LLDB_INVALID_ADDRESS)
          return false;
        // Replace the address in the new buffer
        if (encoder.PutMaxU64(const_offset, const_byte_size,
                              linked_file_addr) == UINT32_MAX)
          return false;
      }
      break;

    default:
      const_offset = 0;
      const_value = 0;
      const_byte_size = 0;
      break;
    }

    if (!decoded_data) {
      const offset_t op_arg_size = GetOpcodeDataSize(m_data, offset, op);
      if (op_arg_size == LLDB_INVALID_OFFSET)
        return false;
      else
        offset += op_arg_size;
    }
  }

  // If we linked the TLS address correctly, update the module so that when the
  // expression is evaluated it can resolve the file address to a load address
  // and read the
  // TLS data
  m_module_wp = new_module_sp;
  m_data.SetData(heap_data_sp);
  return true;
}

bool DWARFExpression::LocationListContainsAddress(
    lldb::addr_t loclist_base_addr, lldb::addr_t addr) const {
  if (addr == LLDB_INVALID_ADDRESS)
    return false;

  if (IsLocationList()) {
    lldb::offset_t offset = 0;

    if (loclist_base_addr == LLDB_INVALID_ADDRESS)
      return false;

    while (m_data.ValidOffset(offset)) {
      // We need to figure out what the value is for the location.
      addr_t lo_pc = LLDB_INVALID_ADDRESS;
      addr_t hi_pc = LLDB_INVALID_ADDRESS;
      if (!AddressRangeForLocationListEntry(m_dwarf_cu, m_data, &offset, lo_pc,
                                            hi_pc))
        break;

      if (lo_pc == 0 && hi_pc == 0)
        break;

      lo_pc += loclist_base_addr - m_loclist_slide;
      hi_pc += loclist_base_addr - m_loclist_slide;

      if (lo_pc <= addr && addr < hi_pc)
        return true;

      offset += m_data.GetU16(&offset);
    }
  }
  return false;
}

bool DWARFExpression::GetLocation(addr_t base_addr, addr_t pc,
                                  lldb::offset_t &offset,
                                  lldb::offset_t &length) {
  offset = 0;
  if (!IsLocationList()) {
    length = m_data.GetByteSize();
    return true;
  }

  if (base_addr != LLDB_INVALID_ADDRESS && pc != LLDB_INVALID_ADDRESS) {
    addr_t curr_base_addr = base_addr;

    while (m_data.ValidOffset(offset)) {
      // We need to figure out what the value is for the location.
      addr_t lo_pc = LLDB_INVALID_ADDRESS;
      addr_t hi_pc = LLDB_INVALID_ADDRESS;
      if (!AddressRangeForLocationListEntry(m_dwarf_cu, m_data, &offset, lo_pc,
                                            hi_pc))
        break;

      if (lo_pc == 0 && hi_pc == 0)
        break;

      lo_pc += curr_base_addr - m_loclist_slide;
      hi_pc += curr_base_addr - m_loclist_slide;

      length = m_data.GetU16(&offset);

      if (length > 0 && lo_pc <= pc && pc < hi_pc)
        return true;

      offset += length;
    }
  }
  offset = LLDB_INVALID_OFFSET;
  length = 0;
  return false;
}

bool DWARFExpression::DumpLocationForAddress(Stream *s,
                                             lldb::DescriptionLevel level,
                                             addr_t base_addr, addr_t address,
                                             ABI *abi) {
  lldb::offset_t offset = 0;
  lldb::offset_t length = 0;

  if (GetLocation(base_addr, address, offset, length)) {
    if (length > 0) {
      DumpLocation(s, offset, length, level, abi);
      return true;
    }
  }
  return false;
}

bool DWARFExpression::Evaluate(ExecutionContextScope *exe_scope,
                               lldb::addr_t loclist_base_load_addr,
                               const Value *initial_value_ptr,
                               const Value *object_address_ptr, Value &result,
                               Status *error_ptr) const {
  ExecutionContext exe_ctx(exe_scope);
  return Evaluate(&exe_ctx, nullptr, loclist_base_load_addr, initial_value_ptr,
                  object_address_ptr, result, error_ptr);
}

bool DWARFExpression::Evaluate(ExecutionContext *exe_ctx,
                               RegisterContext *reg_ctx,
                               lldb::addr_t loclist_base_load_addr,
                               const Value *initial_value_ptr,
                               const Value *object_address_ptr, Value &result,
                               Status *error_ptr) const {
  ModuleSP module_sp = m_module_wp.lock();

  if (IsLocationList()) {
    lldb::offset_t offset = 0;
    addr_t pc;
    StackFrame *frame = NULL;
    if (reg_ctx)
      pc = reg_ctx->GetPC();
    else {
      frame = exe_ctx->GetFramePtr();
      if (!frame)
        return false;
      RegisterContextSP reg_ctx_sp = frame->GetRegisterContext();
      if (!reg_ctx_sp)
        return false;
      pc = reg_ctx_sp->GetPC();
    }

    if (loclist_base_load_addr != LLDB_INVALID_ADDRESS) {
      if (pc == LLDB_INVALID_ADDRESS) {
        if (error_ptr)
          error_ptr->SetErrorString("Invalid PC in frame.");
        return false;
      }

      addr_t curr_loclist_base_load_addr = loclist_base_load_addr;

      while (m_data.ValidOffset(offset)) {
        // We need to figure out what the value is for the location.
        addr_t lo_pc = LLDB_INVALID_ADDRESS;
        addr_t hi_pc = LLDB_INVALID_ADDRESS;
        if (!AddressRangeForLocationListEntry(m_dwarf_cu, m_data, &offset,
                                              lo_pc, hi_pc))
          break;

        if (lo_pc == 0 && hi_pc == 0)
          break;

        lo_pc += curr_loclist_base_load_addr - m_loclist_slide;
        hi_pc += curr_loclist_base_load_addr - m_loclist_slide;

        uint16_t length = m_data.GetU16(&offset);

        if (length > 0 && lo_pc <= pc && pc < hi_pc) {
          return DWARFExpression::Evaluate(
              exe_ctx, reg_ctx, module_sp, m_data, m_dwarf_cu, offset, length,
              m_reg_kind, initial_value_ptr, object_address_ptr, result,
              error_ptr);
        }
        offset += length;
      }
    }
    if (error_ptr)
      error_ptr->SetErrorString("variable not available");
    return false;
  }

  // Not a location list, just a single expression.
  return DWARFExpression::Evaluate(
      exe_ctx, reg_ctx, module_sp, m_data, m_dwarf_cu, 0, m_data.GetByteSize(),
      m_reg_kind, initial_value_ptr, object_address_ptr, result, error_ptr);
}

bool DWARFExpression::Evaluate(
    ExecutionContext *exe_ctx, RegisterContext *reg_ctx,
    lldb::ModuleSP module_sp, const DataExtractor &opcodes,
    DWARFUnit *dwarf_cu, const lldb::offset_t opcodes_offset,
    const lldb::offset_t opcodes_length, const lldb::RegisterKind reg_kind,
    const Value *initial_value_ptr, const Value *object_address_ptr,
    Value &result, Status *error_ptr) {

  if (opcodes_length == 0) {
    if (error_ptr)
      error_ptr->SetErrorString(
          "no location, value may have been optimized out");
    return false;
  }
  std::vector<Value> stack;

  Process *process = NULL;
  StackFrame *frame = NULL;

  if (exe_ctx) {
    process = exe_ctx->GetProcessPtr();
    frame = exe_ctx->GetFramePtr();
  }
  if (reg_ctx == NULL && frame)
    reg_ctx = frame->GetRegisterContext().get();

  if (initial_value_ptr)
    stack.push_back(*initial_value_ptr);

  lldb::offset_t offset = opcodes_offset;
  const lldb::offset_t end_offset = opcodes_offset + opcodes_length;
  Value tmp;
  uint32_t reg_num;

  /// Insertion point for evaluating multi-piece expression.
  uint64_t op_piece_offset = 0;
  Value pieces; // Used for DW_OP_piece

  // Make sure all of the data is available in opcodes.
  if (!opcodes.ValidOffsetForDataOfSize(opcodes_offset, opcodes_length)) {
    if (error_ptr)
      error_ptr->SetErrorString(
          "invalid offset and/or length for opcodes buffer.");
    return false;
  }
  Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS));

  while (opcodes.ValidOffset(offset) && offset < end_offset) {
    const lldb::offset_t op_offset = offset;
    const uint8_t op = opcodes.GetU8(&offset);

    if (log && log->GetVerbose()) {
      size_t count = stack.size();
      log->Printf("Stack before operation has %" PRIu64 " values:",
                  (uint64_t)count);
      for (size_t i = 0; i < count; ++i) {
        StreamString new_value;
        new_value.Printf("[%" PRIu64 "]", (uint64_t)i);
        stack[i].Dump(&new_value);
        log->Printf("  %s", new_value.GetData());
      }
      log->Printf("0x%8.8" PRIx64 ": %s", op_offset, DW_OP_value_to_name(op));
    }

    switch (op) {
    //----------------------------------------------------------------------
    // The DW_OP_addr operation has a single operand that encodes a machine
    // address and whose size is the size of an address on the target machine.
    //----------------------------------------------------------------------
    case DW_OP_addr:
      stack.push_back(Scalar(opcodes.GetAddress(&offset)));
      stack.back().SetValueType(Value::eValueTypeFileAddress);
      // Convert the file address to a load address, so subsequent
      // DWARF operators can operate on it.
      if (frame)
        stack.back().ConvertToLoadAddress(module_sp.get(),
                                          frame->CalculateTarget().get());
      break;

    //----------------------------------------------------------------------
    // The DW_OP_addr_sect_offset4 is used for any location expressions in
    // shared libraries that have a location like:
    //  DW_OP_addr(0x1000)
    // If this address resides in a shared library, then this virtual address
    // won't make sense when it is evaluated in the context of a running
    // process where shared libraries have been slid. To account for this, this
    // new address type where we can store the section pointer and a 4 byte
    // offset.
    //----------------------------------------------------------------------
    //      case DW_OP_addr_sect_offset4:
    //          {
    //              result_type = eResultTypeFileAddress;
    //              lldb::Section *sect = (lldb::Section
    //              *)opcodes.GetMaxU64(&offset, sizeof(void *));
    //              lldb::addr_t sect_offset = opcodes.GetU32(&offset);
    //
    //              Address so_addr (sect, sect_offset);
    //              lldb::addr_t load_addr = so_addr.GetLoadAddress();
    //              if (load_addr != LLDB_INVALID_ADDRESS)
    //              {
    //                  // We successfully resolve a file address to a load
    //                  // address.
    //                  stack.push_back(load_addr);
    //                  break;
    //              }
    //              else
    //              {
    //                  // We were able
    //                  if (error_ptr)
    //                      error_ptr->SetErrorStringWithFormat ("Section %s in
    //                      %s is not currently loaded.\n",
    //                      sect->GetName().AsCString(),
    //                      sect->GetModule()->GetFileSpec().GetFilename().AsCString());
    //                  return false;
    //              }
    //          }
    //          break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_deref
    // OPERANDS: none
    // DESCRIPTION: Pops the top stack entry and treats it as an address.
    // The value retrieved from that address is pushed. The size of the data
    // retrieved from the dereferenced address is the size of an address on the
    // target machine.
    //----------------------------------------------------------------------
    case DW_OP_deref: {
      if (stack.empty()) {
        if (error_ptr)
          error_ptr->SetErrorString("Expression stack empty for DW_OP_deref.");
        return false;
      }
      Value::ValueType value_type = stack.back().GetValueType();
      switch (value_type) {
      case Value::eValueTypeHostAddress: {
        void *src = (void *)stack.back().GetScalar().ULongLong();
        intptr_t ptr;
        ::memcpy(&ptr, src, sizeof(void *));
        stack.back().GetScalar() = ptr;
        stack.back().ClearContext();
      } break;
      case Value::eValueTypeFileAddress: {
        auto file_addr = stack.back().GetScalar().ULongLong(
            LLDB_INVALID_ADDRESS);
        if (!module_sp) {
          if (error_ptr)
            error_ptr->SetErrorStringWithFormat(
                "need module to resolve file address for DW_OP_deref");
          return false;
        }
        Address so_addr;
        if (!module_sp->ResolveFileAddress(file_addr, so_addr)) {
          if (error_ptr)
            error_ptr->SetErrorStringWithFormat(
                "failed to resolve file address in module");
          return false;
        }
        addr_t load_Addr = so_addr.GetLoadAddress(exe_ctx->GetTargetPtr());
        if (load_Addr == LLDB_INVALID_ADDRESS) {
          if (error_ptr)
            error_ptr->SetErrorStringWithFormat(
                "failed to resolve load address");
          return false;
        }
        stack.back().GetScalar() = load_Addr;
        stack.back().SetValueType(Value::eValueTypeLoadAddress);
        // Fall through to load address code below...
      } LLVM_FALLTHROUGH;
      case Value::eValueTypeLoadAddress:
        if (exe_ctx) {
          if (process) {
            lldb::addr_t pointer_addr =
                stack.back().GetScalar().ULongLong(LLDB_INVALID_ADDRESS);
            Status error;
            lldb::addr_t pointer_value =
                process->ReadPointerFromMemory(pointer_addr, error);
            if (pointer_value != LLDB_INVALID_ADDRESS) {
              stack.back().GetScalar() = pointer_value;
              stack.back().ClearContext();
            } else {
              if (error_ptr)
                error_ptr->SetErrorStringWithFormat(
                    "Failed to dereference pointer from 0x%" PRIx64
                    " for DW_OP_deref: %s\n",
                    pointer_addr, error.AsCString());
              return false;
            }
          } else {
            if (error_ptr)
              error_ptr->SetErrorStringWithFormat(
                  "NULL process for DW_OP_deref.\n");
            return false;
          }
        } else {
          if (error_ptr)
            error_ptr->SetErrorStringWithFormat(
                "NULL execution context for DW_OP_deref.\n");
          return false;
        }
        break;

      default:
        break;
      }

    } break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_deref_size
    // OPERANDS: 1
    //  1 - uint8_t that specifies the size of the data to dereference.
    // DESCRIPTION: Behaves like the DW_OP_deref operation: it pops the top
    // stack entry and treats it as an address. The value retrieved from that
    // address is pushed. In the DW_OP_deref_size operation, however, the size
    // in bytes of the data retrieved from the dereferenced address is
    // specified by the single operand. This operand is a 1-byte unsigned
    // integral constant whose value may not be larger than the size of an
    // address on the target machine. The data retrieved is zero extended to
    // the size of an address on the target machine before being pushed on the
    // expression stack.
    //----------------------------------------------------------------------
    case DW_OP_deref_size: {
      if (stack.empty()) {
        if (error_ptr)
          error_ptr->SetErrorString(
              "Expression stack empty for DW_OP_deref_size.");
        return false;
      }
      uint8_t size = opcodes.GetU8(&offset);
      Value::ValueType value_type = stack.back().GetValueType();
      switch (value_type) {
      case Value::eValueTypeHostAddress: {
        void *src = (void *)stack.back().GetScalar().ULongLong();
        intptr_t ptr;
        ::memcpy(&ptr, src, sizeof(void *));
        // I can't decide whether the size operand should apply to the bytes in
        // their
        // lldb-host endianness or the target endianness.. I doubt this'll ever
        // come up but I'll opt for assuming big endian regardless.
        switch (size) {
        case 1:
          ptr = ptr & 0xff;
          break;
        case 2:
          ptr = ptr & 0xffff;
          break;
        case 3:
          ptr = ptr & 0xffffff;
          break;
        case 4:
          ptr = ptr & 0xffffffff;
          break;
        // the casts are added to work around the case where intptr_t is a 32
        // bit quantity;
        // presumably we won't hit the 5..7 cases if (void*) is 32-bits in this
        // program.
        case 5:
          ptr = (intptr_t)ptr & 0xffffffffffULL;
          break;
        case 6:
          ptr = (intptr_t)ptr & 0xffffffffffffULL;
          break;
        case 7:
          ptr = (intptr_t)ptr & 0xffffffffffffffULL;
          break;
        default:
          break;
        }
        stack.back().GetScalar() = ptr;
        stack.back().ClearContext();
      } break;
      case Value::eValueTypeLoadAddress:
        if (exe_ctx) {
          if (process) {
            lldb::addr_t pointer_addr =
                stack.back().GetScalar().ULongLong(LLDB_INVALID_ADDRESS);
            uint8_t addr_bytes[sizeof(lldb::addr_t)];
            Status error;
            if (process->ReadMemory(pointer_addr, &addr_bytes, size, error) ==
                size) {
              DataExtractor addr_data(addr_bytes, sizeof(addr_bytes),
                                      process->GetByteOrder(), size);
              lldb::offset_t addr_data_offset = 0;
              switch (size) {
              case 1:
                stack.back().GetScalar() = addr_data.GetU8(&addr_data_offset);
                break;
              case 2:
                stack.back().GetScalar() = addr_data.GetU16(&addr_data_offset);
                break;
              case 4:
                stack.back().GetScalar() = addr_data.GetU32(&addr_data_offset);
                break;
              case 8:
                stack.back().GetScalar() = addr_data.GetU64(&addr_data_offset);
                break;
              default:
                stack.back().GetScalar() =
                    addr_data.GetPointer(&addr_data_offset);
              }
              stack.back().ClearContext();
            } else {
              if (error_ptr)
                error_ptr->SetErrorStringWithFormat(
                    "Failed to dereference pointer from 0x%" PRIx64
                    " for DW_OP_deref: %s\n",
                    pointer_addr, error.AsCString());
              return false;
            }
          } else {
            if (error_ptr)
              error_ptr->SetErrorStringWithFormat(
                  "NULL process for DW_OP_deref.\n");
            return false;
          }
        } else {
          if (error_ptr)
            error_ptr->SetErrorStringWithFormat(
                "NULL execution context for DW_OP_deref.\n");
          return false;
        }
        break;

      default:
        break;
      }

    } break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_xderef_size
    // OPERANDS: 1
    //  1 - uint8_t that specifies the size of the data to dereference.
    // DESCRIPTION: Behaves like the DW_OP_xderef operation: the entry at
    // the top of the stack is treated as an address. The second stack entry is
    // treated as an "address space identifier" for those architectures that
    // support multiple address spaces. The top two stack elements are popped,
    // a data item is retrieved through an implementation-defined address
    // calculation and pushed as the new stack top. In the DW_OP_xderef_size
    // operation, however, the size in bytes of the data retrieved from the
    // dereferenced address is specified by the single operand. This operand is
    // a 1-byte unsigned integral constant whose value may not be larger than
    // the size of an address on the target machine. The data retrieved is zero
    // extended to the size of an address on the target machine before being
    // pushed on the expression stack.
    //----------------------------------------------------------------------
    case DW_OP_xderef_size:
      if (error_ptr)
        error_ptr->SetErrorString("Unimplemented opcode: DW_OP_xderef_size.");
      return false;
    //----------------------------------------------------------------------
    // OPCODE: DW_OP_xderef
    // OPERANDS: none
    // DESCRIPTION: Provides an extended dereference mechanism. The entry at
    // the top of the stack is treated as an address. The second stack entry is
    // treated as an "address space identifier" for those architectures that
    // support multiple address spaces. The top two stack elements are popped,
    // a data item is retrieved through an implementation-defined address
    // calculation and pushed as the new stack top. The size of the data
    // retrieved from the dereferenced address is the size of an address on the
    // target machine.
    //----------------------------------------------------------------------
    case DW_OP_xderef:
      if (error_ptr)
        error_ptr->SetErrorString("Unimplemented opcode: DW_OP_xderef.");
      return false;

    //----------------------------------------------------------------------
    // All DW_OP_constXXX opcodes have a single operand as noted below:
    //
    // Opcode           Operand 1
    // ---------------  ----------------------------------------------------
    // DW_OP_const1u    1-byte unsigned integer constant DW_OP_const1s
    // 1-byte signed integer constant DW_OP_const2u    2-byte unsigned integer
    // constant DW_OP_const2s    2-byte signed integer constant DW_OP_const4u
    // 4-byte unsigned integer constant DW_OP_const4s    4-byte signed integer
    // constant DW_OP_const8u    8-byte unsigned integer constant DW_OP_const8s
    // 8-byte signed integer constant DW_OP_constu     unsigned LEB128 integer
    // constant DW_OP_consts     signed LEB128 integer constant
    //----------------------------------------------------------------------
    case DW_OP_const1u:
      stack.push_back(Scalar((uint8_t)opcodes.GetU8(&offset)));
      break;
    case DW_OP_const1s:
      stack.push_back(Scalar((int8_t)opcodes.GetU8(&offset)));
      break;
    case DW_OP_const2u:
      stack.push_back(Scalar((uint16_t)opcodes.GetU16(&offset)));
      break;
    case DW_OP_const2s:
      stack.push_back(Scalar((int16_t)opcodes.GetU16(&offset)));
      break;
    case DW_OP_const4u:
      stack.push_back(Scalar((uint32_t)opcodes.GetU32(&offset)));
      break;
    case DW_OP_const4s:
      stack.push_back(Scalar((int32_t)opcodes.GetU32(&offset)));
      break;
    case DW_OP_const8u:
      stack.push_back(Scalar((uint64_t)opcodes.GetU64(&offset)));
      break;
    case DW_OP_const8s:
      stack.push_back(Scalar((int64_t)opcodes.GetU64(&offset)));
      break;
    case DW_OP_constu:
      stack.push_back(Scalar(opcodes.GetULEB128(&offset)));
      break;
    case DW_OP_consts:
      stack.push_back(Scalar(opcodes.GetSLEB128(&offset)));
      break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_dup
    // OPERANDS: none
    // DESCRIPTION: duplicates the value at the top of the stack
    //----------------------------------------------------------------------
    case DW_OP_dup:
      if (stack.empty()) {
        if (error_ptr)
          error_ptr->SetErrorString("Expression stack empty for DW_OP_dup.");
        return false;
      } else
        stack.push_back(stack.back());
      break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_drop
    // OPERANDS: none
    // DESCRIPTION: pops the value at the top of the stack
    //----------------------------------------------------------------------
    case DW_OP_drop:
      if (stack.empty()) {
        if (error_ptr)
          error_ptr->SetErrorString("Expression stack empty for DW_OP_drop.");
        return false;
      } else
        stack.pop_back();
      break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_over
    // OPERANDS: none
    // DESCRIPTION: Duplicates the entry currently second in the stack at
    // the top of the stack.
    //----------------------------------------------------------------------
    case DW_OP_over:
      if (stack.size() < 2) {
        if (error_ptr)
          error_ptr->SetErrorString(
              "Expression stack needs at least 2 items for DW_OP_over.");
        return false;
      } else
        stack.push_back(stack[stack.size() - 2]);
      break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_pick
    // OPERANDS: uint8_t index into the current stack
    // DESCRIPTION: The stack entry with the specified index (0 through 255,
    // inclusive) is pushed on the stack
    //----------------------------------------------------------------------
    case DW_OP_pick: {
      uint8_t pick_idx = opcodes.GetU8(&offset);
      if (pick_idx < stack.size())
        stack.push_back(stack[pick_idx]);
      else {
        if (error_ptr)
          error_ptr->SetErrorStringWithFormat(
              "Index %u out of range for DW_OP_pick.\n", pick_idx);
        return false;
      }
    } break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_swap
    // OPERANDS: none
    // DESCRIPTION: swaps the top two stack entries. The entry at the top
    // of the stack becomes the second stack entry, and the second entry
    // becomes the top of the stack
    //----------------------------------------------------------------------
    case DW_OP_swap:
      if (stack.size() < 2) {
        if (error_ptr)
          error_ptr->SetErrorString(
              "Expression stack needs at least 2 items for DW_OP_swap.");
        return false;
      } else {
        tmp = stack.back();
        stack.back() = stack[stack.size() - 2];
        stack[stack.size() - 2] = tmp;
      }
      break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_rot
    // OPERANDS: none
    // DESCRIPTION: Rotates the first three stack entries. The entry at
    // the top of the stack becomes the third stack entry, the second entry
    // becomes the top of the stack, and the third entry becomes the second
    // entry.
    //----------------------------------------------------------------------
    case DW_OP_rot:
      if (stack.size() < 3) {
        if (error_ptr)
          error_ptr->SetErrorString(
              "Expression stack needs at least 3 items for DW_OP_rot.");
        return false;
      } else {
        size_t last_idx = stack.size() - 1;
        Value old_top = stack[last_idx];
        stack[last_idx] = stack[last_idx - 1];
        stack[last_idx - 1] = stack[last_idx - 2];
        stack[last_idx - 2] = old_top;
      }
      break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_abs
    // OPERANDS: none
    // DESCRIPTION: pops the top stack entry, interprets it as a signed
    // value and pushes its absolute value. If the absolute value can not be
    // represented, the result is undefined.
    //----------------------------------------------------------------------
    case DW_OP_abs:
      if (stack.empty()) {
        if (error_ptr)
          error_ptr->SetErrorString(
              "Expression stack needs at least 1 item for DW_OP_abs.");
        return false;
      } else if (!stack.back().ResolveValue(exe_ctx).AbsoluteValue()) {
        if (error_ptr)
          error_ptr->SetErrorString(
              "Failed to take the absolute value of the first stack item.");
        return false;
      }
      break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_and
    // OPERANDS: none
    // DESCRIPTION: pops the top two stack values, performs a bitwise and
    // operation on the two, and pushes the result.
    //----------------------------------------------------------------------
    case DW_OP_and:
      if (stack.size() < 2) {
        if (error_ptr)
          error_ptr->SetErrorString(
              "Expression stack needs at least 2 items for DW_OP_and.");
        return false;
      } else {
        tmp = stack.back();
        stack.pop_back();
        stack.back().ResolveValue(exe_ctx) =
            stack.back().ResolveValue(exe_ctx) & tmp.ResolveValue(exe_ctx);
      }
      break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_div
    // OPERANDS: none
    // DESCRIPTION: pops the top two stack values, divides the former second
    // entry by the former top of the stack using signed division, and pushes
    // the result.
    //----------------------------------------------------------------------
    case DW_OP_div:
      if (stack.size() < 2) {
        if (error_ptr)
          error_ptr->SetErrorString(
              "Expression stack needs at least 2 items for DW_OP_div.");
        return false;
      } else {
        tmp = stack.back();
        if (tmp.ResolveValue(exe_ctx).IsZero()) {
          if (error_ptr)
            error_ptr->SetErrorString("Divide by zero.");
          return false;
        } else {
          stack.pop_back();
          stack.back() =
              stack.back().ResolveValue(exe_ctx) / tmp.ResolveValue(exe_ctx);
          if (!stack.back().ResolveValue(exe_ctx).IsValid()) {
            if (error_ptr)
              error_ptr->SetErrorString("Divide failed.");
            return false;
          }
        }
      }
      break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_minus
    // OPERANDS: none
    // DESCRIPTION: pops the top two stack values, subtracts the former top
    // of the stack from the former second entry, and pushes the result.
    //----------------------------------------------------------------------
    case DW_OP_minus:
      if (stack.size() < 2) {
        if (error_ptr)
          error_ptr->SetErrorString(
              "Expression stack needs at least 2 items for DW_OP_minus.");
        return false;
      } else {
        tmp = stack.back();
        stack.pop_back();
        stack.back().ResolveValue(exe_ctx) =
            stack.back().ResolveValue(exe_ctx) - tmp.ResolveValue(exe_ctx);
      }
      break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_mod
    // OPERANDS: none
    // DESCRIPTION: pops the top two stack values and pushes the result of
    // the calculation: former second stack entry modulo the former top of the
    // stack.
    //----------------------------------------------------------------------
    case DW_OP_mod:
      if (stack.size() < 2) {
        if (error_ptr)
          error_ptr->SetErrorString(
              "Expression stack needs at least 2 items for DW_OP_mod.");
        return false;
      } else {
        tmp = stack.back();
        stack.pop_back();
        stack.back().ResolveValue(exe_ctx) =
            stack.back().ResolveValue(exe_ctx) % tmp.ResolveValue(exe_ctx);
      }
      break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_mul
    // OPERANDS: none
    // DESCRIPTION: pops the top two stack entries, multiplies them
    // together, and pushes the result.
    //----------------------------------------------------------------------
    case DW_OP_mul:
      if (stack.size() < 2) {
        if (error_ptr)
          error_ptr->SetErrorString(
              "Expression stack needs at least 2 items for DW_OP_mul.");
        return false;
      } else {
        tmp = stack.back();
        stack.pop_back();
        stack.back().ResolveValue(exe_ctx) =
            stack.back().ResolveValue(exe_ctx) * tmp.ResolveValue(exe_ctx);
      }
      break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_neg
    // OPERANDS: none
    // DESCRIPTION: pops the top stack entry, and pushes its negation.
    //----------------------------------------------------------------------
    case DW_OP_neg:
      if (stack.empty()) {
        if (error_ptr)
          error_ptr->SetErrorString(
              "Expression stack needs at least 1 item for DW_OP_neg.");
        return false;
      } else {
        if (!stack.back().ResolveValue(exe_ctx).UnaryNegate()) {
          if (error_ptr)
            error_ptr->SetErrorString("Unary negate failed.");
          return false;
        }
      }
      break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_not
    // OPERANDS: none
    // DESCRIPTION: pops the top stack entry, and pushes its bitwise
    // complement
    //----------------------------------------------------------------------
    case DW_OP_not:
      if (stack.empty()) {
        if (error_ptr)
          error_ptr->SetErrorString(
              "Expression stack needs at least 1 item for DW_OP_not.");
        return false;
      } else {
        if (!stack.back().ResolveValue(exe_ctx).OnesComplement()) {
          if (error_ptr)
            error_ptr->SetErrorString("Logical NOT failed.");
          return false;
        }
      }
      break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_or
    // OPERANDS: none
    // DESCRIPTION: pops the top two stack entries, performs a bitwise or
    // operation on the two, and pushes the result.
    //----------------------------------------------------------------------
    case DW_OP_or:
      if (stack.size() < 2) {
        if (error_ptr)
          error_ptr->SetErrorString(
              "Expression stack needs at least 2 items for DW_OP_or.");
        return false;
      } else {
        tmp = stack.back();
        stack.pop_back();
        stack.back().ResolveValue(exe_ctx) =
            stack.back().ResolveValue(exe_ctx) | tmp.ResolveValue(exe_ctx);
      }
      break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_plus
    // OPERANDS: none
    // DESCRIPTION: pops the top two stack entries, adds them together, and
    // pushes the result.
    //----------------------------------------------------------------------
    case DW_OP_plus:
      if (stack.size() < 2) {
        if (error_ptr)
          error_ptr->SetErrorString(
              "Expression stack needs at least 2 items for DW_OP_plus.");
        return false;
      } else {
        tmp = stack.back();
        stack.pop_back();
        stack.back().GetScalar() += tmp.GetScalar();
      }
      break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_plus_uconst
    // OPERANDS: none
    // DESCRIPTION: pops the top stack entry, adds it to the unsigned LEB128
    // constant operand and pushes the result.
    //----------------------------------------------------------------------
    case DW_OP_plus_uconst:
      if (stack.empty()) {
        if (error_ptr)
          error_ptr->SetErrorString(
              "Expression stack needs at least 1 item for DW_OP_plus_uconst.");
        return false;
      } else {
        const uint64_t uconst_value = opcodes.GetULEB128(&offset);
        // Implicit conversion from a UINT to a Scalar...
        stack.back().GetScalar() += uconst_value;
        if (!stack.back().GetScalar().IsValid()) {
          if (error_ptr)
            error_ptr->SetErrorString("DW_OP_plus_uconst failed.");
          return false;
        }
      }
      break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_shl
    // OPERANDS: none
    // DESCRIPTION:  pops the top two stack entries, shifts the former
    // second entry left by the number of bits specified by the former top of
    // the stack, and pushes the result.
    //----------------------------------------------------------------------
    case DW_OP_shl:
      if (stack.size() < 2) {
        if (error_ptr)
          error_ptr->SetErrorString(
              "Expression stack needs at least 2 items for DW_OP_shl.");
        return false;
      } else {
        tmp = stack.back();
        stack.pop_back();
        stack.back().ResolveValue(exe_ctx) <<= tmp.ResolveValue(exe_ctx);
      }
      break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_shr
    // OPERANDS: none
    // DESCRIPTION: pops the top two stack entries, shifts the former second
    // entry right logically (filling with zero bits) by the number of bits
    // specified by the former top of the stack, and pushes the result.
    //----------------------------------------------------------------------
    case DW_OP_shr:
      if (stack.size() < 2) {
        if (error_ptr)
          error_ptr->SetErrorString(
              "Expression stack needs at least 2 items for DW_OP_shr.");
        return false;
      } else {
        tmp = stack.back();
        stack.pop_back();
        if (!stack.back().ResolveValue(exe_ctx).ShiftRightLogical(
                tmp.ResolveValue(exe_ctx))) {
          if (error_ptr)
            error_ptr->SetErrorString("DW_OP_shr failed.");
          return false;
        }
      }
      break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_shra
    // OPERANDS: none
    // DESCRIPTION: pops the top two stack entries, shifts the former second
    // entry right arithmetically (divide the magnitude by 2, keep the same
    // sign for the result) by the number of bits specified by the former top
    // of the stack, and pushes the result.
    //----------------------------------------------------------------------
    case DW_OP_shra:
      if (stack.size() < 2) {
        if (error_ptr)
          error_ptr->SetErrorString(
              "Expression stack needs at least 2 items for DW_OP_shra.");
        return false;
      } else {
        tmp = stack.back();
        stack.pop_back();
        stack.back().ResolveValue(exe_ctx) >>= tmp.ResolveValue(exe_ctx);
      }
      break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_xor
    // OPERANDS: none
    // DESCRIPTION: pops the top two stack entries, performs the bitwise
    // exclusive-or operation on the two, and pushes the result.
    //----------------------------------------------------------------------
    case DW_OP_xor:
      if (stack.size() < 2) {
        if (error_ptr)
          error_ptr->SetErrorString(
              "Expression stack needs at least 2 items for DW_OP_xor.");
        return false;
      } else {
        tmp = stack.back();
        stack.pop_back();
        stack.back().ResolveValue(exe_ctx) =
            stack.back().ResolveValue(exe_ctx) ^ tmp.ResolveValue(exe_ctx);
      }
      break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_skip
    // OPERANDS: int16_t
    // DESCRIPTION:  An unconditional branch. Its single operand is a 2-byte
    // signed integer constant. The 2-byte constant is the number of bytes of
    // the DWARF expression to skip forward or backward from the current
    // operation, beginning after the 2-byte constant.
    //----------------------------------------------------------------------
    case DW_OP_skip: {
      int16_t skip_offset = (int16_t)opcodes.GetU16(&offset);
      lldb::offset_t new_offset = offset + skip_offset;
      if (new_offset >= opcodes_offset && new_offset < end_offset)
        offset = new_offset;
      else {
        if (error_ptr)
          error_ptr->SetErrorString("Invalid opcode offset in DW_OP_skip.");
        return false;
      }
    } break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_bra
    // OPERANDS: int16_t
    // DESCRIPTION: A conditional branch. Its single operand is a 2-byte
    // signed integer constant. This operation pops the top of stack. If the
    // value popped is not the constant 0, the 2-byte constant operand is the
    // number of bytes of the DWARF expression to skip forward or backward from
    // the current operation, beginning after the 2-byte constant.
    //----------------------------------------------------------------------
    case DW_OP_bra:
      if (stack.empty()) {
        if (error_ptr)
          error_ptr->SetErrorString(
              "Expression stack needs at least 1 item for DW_OP_bra.");
        return false;
      } else {
        tmp = stack.back();
        stack.pop_back();
        int16_t bra_offset = (int16_t)opcodes.GetU16(&offset);
        Scalar zero(0);
        if (tmp.ResolveValue(exe_ctx) != zero) {
          lldb::offset_t new_offset = offset + bra_offset;
          if (new_offset >= opcodes_offset && new_offset < end_offset)
            offset = new_offset;
          else {
            if (error_ptr)
              error_ptr->SetErrorString("Invalid opcode offset in DW_OP_bra.");
            return false;
          }
        }
      }
      break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_eq
    // OPERANDS: none
    // DESCRIPTION: pops the top two stack values, compares using the
    // equals (==) operator.
    // STACK RESULT: push the constant value 1 onto the stack if the result
    // of the operation is true or the constant value 0 if the result of the
    // operation is false.
    //----------------------------------------------------------------------
    case DW_OP_eq:
      if (stack.size() < 2) {
        if (error_ptr)
          error_ptr->SetErrorString(
              "Expression stack needs at least 2 items for DW_OP_eq.");
        return false;
      } else {
        tmp = stack.back();
        stack.pop_back();
        stack.back().ResolveValue(exe_ctx) =
            stack.back().ResolveValue(exe_ctx) == tmp.ResolveValue(exe_ctx);
      }
      break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_ge
    // OPERANDS: none
    // DESCRIPTION: pops the top two stack values, compares using the
    // greater than or equal to (>=) operator.
    // STACK RESULT: push the constant value 1 onto the stack if the result
    // of the operation is true or the constant value 0 if the result of the
    // operation is false.
    //----------------------------------------------------------------------
    case DW_OP_ge:
      if (stack.size() < 2) {
        if (error_ptr)
          error_ptr->SetErrorString(
              "Expression stack needs at least 2 items for DW_OP_ge.");
        return false;
      } else {
        tmp = stack.back();
        stack.pop_back();
        stack.back().ResolveValue(exe_ctx) =
            stack.back().ResolveValue(exe_ctx) >= tmp.ResolveValue(exe_ctx);
      }
      break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_gt
    // OPERANDS: none
    // DESCRIPTION: pops the top two stack values, compares using the
    // greater than (>) operator.
    // STACK RESULT: push the constant value 1 onto the stack if the result
    // of the operation is true or the constant value 0 if the result of the
    // operation is false.
    //----------------------------------------------------------------------
    case DW_OP_gt:
      if (stack.size() < 2) {
        if (error_ptr)
          error_ptr->SetErrorString(
              "Expression stack needs at least 2 items for DW_OP_gt.");
        return false;
      } else {
        tmp = stack.back();
        stack.pop_back();
        stack.back().ResolveValue(exe_ctx) =
            stack.back().ResolveValue(exe_ctx) > tmp.ResolveValue(exe_ctx);
      }
      break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_le
    // OPERANDS: none
    // DESCRIPTION: pops the top two stack values, compares using the
    // less than or equal to (<=) operator.
    // STACK RESULT: push the constant value 1 onto the stack if the result
    // of the operation is true or the constant value 0 if the result of the
    // operation is false.
    //----------------------------------------------------------------------
    case DW_OP_le:
      if (stack.size() < 2) {
        if (error_ptr)
          error_ptr->SetErrorString(
              "Expression stack needs at least 2 items for DW_OP_le.");
        return false;
      } else {
        tmp = stack.back();
        stack.pop_back();
        stack.back().ResolveValue(exe_ctx) =
            stack.back().ResolveValue(exe_ctx) <= tmp.ResolveValue(exe_ctx);
      }
      break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_lt
    // OPERANDS: none
    // DESCRIPTION: pops the top two stack values, compares using the
    // less than (<) operator.
    // STACK RESULT: push the constant value 1 onto the stack if the result
    // of the operation is true or the constant value 0 if the result of the
    // operation is false.
    //----------------------------------------------------------------------
    case DW_OP_lt:
      if (stack.size() < 2) {
        if (error_ptr)
          error_ptr->SetErrorString(
              "Expression stack needs at least 2 items for DW_OP_lt.");
        return false;
      } else {
        tmp = stack.back();
        stack.pop_back();
        stack.back().ResolveValue(exe_ctx) =
            stack.back().ResolveValue(exe_ctx) < tmp.ResolveValue(exe_ctx);
      }
      break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_ne
    // OPERANDS: none
    // DESCRIPTION: pops the top two stack values, compares using the
    // not equal (!=) operator.
    // STACK RESULT: push the constant value 1 onto the stack if the result
    // of the operation is true or the constant value 0 if the result of the
    // operation is false.
    //----------------------------------------------------------------------
    case DW_OP_ne:
      if (stack.size() < 2) {
        if (error_ptr)
          error_ptr->SetErrorString(
              "Expression stack needs at least 2 items for DW_OP_ne.");
        return false;
      } else {
        tmp = stack.back();
        stack.pop_back();
        stack.back().ResolveValue(exe_ctx) =
            stack.back().ResolveValue(exe_ctx) != tmp.ResolveValue(exe_ctx);
      }
      break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_litn
    // OPERANDS: none
    // DESCRIPTION: encode the unsigned literal values from 0 through 31.
    // STACK RESULT: push the unsigned literal constant value onto the top
    // of the stack.
    //----------------------------------------------------------------------
    case DW_OP_lit0:
    case DW_OP_lit1:
    case DW_OP_lit2:
    case DW_OP_lit3:
    case DW_OP_lit4:
    case DW_OP_lit5:
    case DW_OP_lit6:
    case DW_OP_lit7:
    case DW_OP_lit8:
    case DW_OP_lit9:
    case DW_OP_lit10:
    case DW_OP_lit11:
    case DW_OP_lit12:
    case DW_OP_lit13:
    case DW_OP_lit14:
    case DW_OP_lit15:
    case DW_OP_lit16:
    case DW_OP_lit17:
    case DW_OP_lit18:
    case DW_OP_lit19:
    case DW_OP_lit20:
    case DW_OP_lit21:
    case DW_OP_lit22:
    case DW_OP_lit23:
    case DW_OP_lit24:
    case DW_OP_lit25:
    case DW_OP_lit26:
    case DW_OP_lit27:
    case DW_OP_lit28:
    case DW_OP_lit29:
    case DW_OP_lit30:
    case DW_OP_lit31:
      stack.push_back(Scalar((uint64_t)(op - DW_OP_lit0)));
      break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_regN
    // OPERANDS: none
    // DESCRIPTION: Push the value in register n on the top of the stack.
    //----------------------------------------------------------------------
    case DW_OP_reg0:
    case DW_OP_reg1:
    case DW_OP_reg2:
    case DW_OP_reg3:
    case DW_OP_reg4:
    case DW_OP_reg5:
    case DW_OP_reg6:
    case DW_OP_reg7:
    case DW_OP_reg8:
    case DW_OP_reg9:
    case DW_OP_reg10:
    case DW_OP_reg11:
    case DW_OP_reg12:
    case DW_OP_reg13:
    case DW_OP_reg14:
    case DW_OP_reg15:
    case DW_OP_reg16:
    case DW_OP_reg17:
    case DW_OP_reg18:
    case DW_OP_reg19:
    case DW_OP_reg20:
    case DW_OP_reg21:
    case DW_OP_reg22:
    case DW_OP_reg23:
    case DW_OP_reg24:
    case DW_OP_reg25:
    case DW_OP_reg26:
    case DW_OP_reg27:
    case DW_OP_reg28:
    case DW_OP_reg29:
    case DW_OP_reg30:
    case DW_OP_reg31: {
      reg_num = op - DW_OP_reg0;

      if (ReadRegisterValueAsScalar(reg_ctx, reg_kind, reg_num, error_ptr, tmp))
        stack.push_back(tmp);
      else
        return false;
    } break;
    //----------------------------------------------------------------------
    // OPCODE: DW_OP_regx
    // OPERANDS:
    //      ULEB128 literal operand that encodes the register.
    // DESCRIPTION: Push the value in register on the top of the stack.
    //----------------------------------------------------------------------
    case DW_OP_regx: {
      reg_num = opcodes.GetULEB128(&offset);
      if (ReadRegisterValueAsScalar(reg_ctx, reg_kind, reg_num, error_ptr, tmp))
        stack.push_back(tmp);
      else
        return false;
    } break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_bregN
    // OPERANDS:
    //      SLEB128 offset from register N
    // DESCRIPTION: Value is in memory at the address specified by register
    // N plus an offset.
    //----------------------------------------------------------------------
    case DW_OP_breg0:
    case DW_OP_breg1:
    case DW_OP_breg2:
    case DW_OP_breg3:
    case DW_OP_breg4:
    case DW_OP_breg5:
    case DW_OP_breg6:
    case DW_OP_breg7:
    case DW_OP_breg8:
    case DW_OP_breg9:
    case DW_OP_breg10:
    case DW_OP_breg11:
    case DW_OP_breg12:
    case DW_OP_breg13:
    case DW_OP_breg14:
    case DW_OP_breg15:
    case DW_OP_breg16:
    case DW_OP_breg17:
    case DW_OP_breg18:
    case DW_OP_breg19:
    case DW_OP_breg20:
    case DW_OP_breg21:
    case DW_OP_breg22:
    case DW_OP_breg23:
    case DW_OP_breg24:
    case DW_OP_breg25:
    case DW_OP_breg26:
    case DW_OP_breg27:
    case DW_OP_breg28:
    case DW_OP_breg29:
    case DW_OP_breg30:
    case DW_OP_breg31: {
      reg_num = op - DW_OP_breg0;

      if (ReadRegisterValueAsScalar(reg_ctx, reg_kind, reg_num, error_ptr,
                                    tmp)) {
        int64_t breg_offset = opcodes.GetSLEB128(&offset);
        tmp.ResolveValue(exe_ctx) += (uint64_t)breg_offset;
        tmp.ClearContext();
        stack.push_back(tmp);
        stack.back().SetValueType(Value::eValueTypeLoadAddress);
      } else
        return false;
    } break;
    //----------------------------------------------------------------------
    // OPCODE: DW_OP_bregx
    // OPERANDS: 2
    //      ULEB128 literal operand that encodes the register.
    //      SLEB128 offset from register N
    // DESCRIPTION: Value is in memory at the address specified by register
    // N plus an offset.
    //----------------------------------------------------------------------
    case DW_OP_bregx: {
      reg_num = opcodes.GetULEB128(&offset);

      if (ReadRegisterValueAsScalar(reg_ctx, reg_kind, reg_num, error_ptr,
                                    tmp)) {
        int64_t breg_offset = opcodes.GetSLEB128(&offset);
        tmp.ResolveValue(exe_ctx) += (uint64_t)breg_offset;
        tmp.ClearContext();
        stack.push_back(tmp);
        stack.back().SetValueType(Value::eValueTypeLoadAddress);
      } else
        return false;
    } break;

    case DW_OP_fbreg:
      if (exe_ctx) {
        if (frame) {
          Scalar value;
          if (frame->GetFrameBaseValue(value, error_ptr)) {
            int64_t fbreg_offset = opcodes.GetSLEB128(&offset);
            value += fbreg_offset;
            stack.push_back(value);
            stack.back().SetValueType(Value::eValueTypeLoadAddress);
          } else
            return false;
        } else {
          if (error_ptr)
            error_ptr->SetErrorString(
                "Invalid stack frame in context for DW_OP_fbreg opcode.");
          return false;
        }
      } else {
        if (error_ptr)
          error_ptr->SetErrorStringWithFormat(
              "NULL execution context for DW_OP_fbreg.\n");
        return false;
      }

      break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_nop
    // OPERANDS: none
    // DESCRIPTION: A place holder. It has no effect on the location stack
    // or any of its values.
    //----------------------------------------------------------------------
    case DW_OP_nop:
      break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_piece
    // OPERANDS: 1
    //      ULEB128: byte size of the piece
    // DESCRIPTION: The operand describes the size in bytes of the piece of
    // the object referenced by the DWARF expression whose result is at the top
    // of the stack. If the piece is located in a register, but does not occupy
    // the entire register, the placement of the piece within that register is
    // defined by the ABI.
    //
    // Many compilers store a single variable in sets of registers, or store a
    // variable partially in memory and partially in registers. DW_OP_piece
    // provides a way of describing how large a part of a variable a particular
    // DWARF expression refers to.
    //----------------------------------------------------------------------
    case DW_OP_piece: {
      const uint64_t piece_byte_size = opcodes.GetULEB128(&offset);

      if (piece_byte_size > 0) {
        Value curr_piece;

        if (stack.empty()) {
          // In a multi-piece expression, this means that the current piece is
          // not available. Fill with zeros for now by resizing the data and
          // appending it
          curr_piece.ResizeData(piece_byte_size);
          ::memset(curr_piece.GetBuffer().GetBytes(), 0, piece_byte_size);
          pieces.AppendDataToHostBuffer(curr_piece);
        } else {
          Status error;
          // Extract the current piece into "curr_piece"
          Value curr_piece_source_value(stack.back());
          stack.pop_back();

          const Value::ValueType curr_piece_source_value_type =
              curr_piece_source_value.GetValueType();
          switch (curr_piece_source_value_type) {
          case Value::eValueTypeLoadAddress:
            if (process) {
              if (curr_piece.ResizeData(piece_byte_size) == piece_byte_size) {
                lldb::addr_t load_addr =
                    curr_piece_source_value.GetScalar().ULongLong(
                        LLDB_INVALID_ADDRESS);
                if (process->ReadMemory(
                        load_addr, curr_piece.GetBuffer().GetBytes(),
                        piece_byte_size, error) != piece_byte_size) {
                  if (error_ptr)
                    error_ptr->SetErrorStringWithFormat(
                        "failed to read memory DW_OP_piece(%" PRIu64
                        ") from 0x%" PRIx64,
                        piece_byte_size, load_addr);
                  return false;
                }
              } else {
                if (error_ptr)
                  error_ptr->SetErrorStringWithFormat(
                      "failed to resize the piece memory buffer for "
                      "DW_OP_piece(%" PRIu64 ")",
                      piece_byte_size);
                return false;
              }
            }
            break;

          case Value::eValueTypeFileAddress:
          case Value::eValueTypeHostAddress:
            if (error_ptr) {
              lldb::addr_t addr = curr_piece_source_value.GetScalar().ULongLong(
                  LLDB_INVALID_ADDRESS);
              error_ptr->SetErrorStringWithFormat(
                  "failed to read memory DW_OP_piece(%" PRIu64
                  ") from %s address 0x%" PRIx64,
                  piece_byte_size, curr_piece_source_value.GetValueType() ==
                                           Value::eValueTypeFileAddress
                                       ? "file"
                                       : "host",
                  addr);
            }
            return false;

          case Value::eValueTypeScalar: {
            uint32_t bit_size = piece_byte_size * 8;
            uint32_t bit_offset = 0;
            if (!curr_piece_source_value.GetScalar().ExtractBitfield(
                    bit_size, bit_offset)) {
              if (error_ptr)
                error_ptr->SetErrorStringWithFormat(
                    "unable to extract %" PRIu64 " bytes from a %" PRIu64
                    " byte scalar value.",
                    piece_byte_size,
                    (uint64_t)curr_piece_source_value.GetScalar()
                        .GetByteSize());
              return false;
            }
            curr_piece = curr_piece_source_value;
          } break;

          case Value::eValueTypeVector: {
            if (curr_piece_source_value.GetVector().length >= piece_byte_size)
              curr_piece_source_value.GetVector().length = piece_byte_size;
            else {
              if (error_ptr)
                error_ptr->SetErrorStringWithFormat(
                    "unable to extract %" PRIu64 " bytes from a %" PRIu64
                    " byte vector value.",
                    piece_byte_size,
                    (uint64_t)curr_piece_source_value.GetVector().length);
              return false;
            }
          } break;
          }

          // Check if this is the first piece?
          if (op_piece_offset == 0) {
            // This is the first piece, we should push it back onto the stack
            // so subsequent pieces will be able to access this piece and add
            // to it
            if (pieces.AppendDataToHostBuffer(curr_piece) == 0) {
              if (error_ptr)
                error_ptr->SetErrorString("failed to append piece data");
              return false;
            }
          } else {
            // If this is the second or later piece there should be a value on
            // the stack
            if (pieces.GetBuffer().GetByteSize() != op_piece_offset) {
              if (error_ptr)
                error_ptr->SetErrorStringWithFormat(
                    "DW_OP_piece for offset %" PRIu64
                    " but top of stack is of size %" PRIu64,
                    op_piece_offset, pieces.GetBuffer().GetByteSize());
              return false;
            }

            if (pieces.AppendDataToHostBuffer(curr_piece) == 0) {
              if (error_ptr)
                error_ptr->SetErrorString("failed to append piece data");
              return false;
            }
          }
          op_piece_offset += piece_byte_size;
        }
      }
    } break;

    case DW_OP_bit_piece: // 0x9d ULEB128 bit size, ULEB128 bit offset (DWARF3);
      if (stack.size() < 1) {
        if (error_ptr)
          error_ptr->SetErrorString(
              "Expression stack needs at least 1 item for DW_OP_bit_piece.");
        return false;
      } else {
        const uint64_t piece_bit_size = opcodes.GetULEB128(&offset);
        const uint64_t piece_bit_offset = opcodes.GetULEB128(&offset);
        switch (stack.back().GetValueType()) {
        case Value::eValueTypeScalar: {
          if (!stack.back().GetScalar().ExtractBitfield(piece_bit_size,
                                                        piece_bit_offset)) {
            if (error_ptr)
              error_ptr->SetErrorStringWithFormat(
                  "unable to extract %" PRIu64 " bit value with %" PRIu64
                  " bit offset from a %" PRIu64 " bit scalar value.",
                  piece_bit_size, piece_bit_offset,
                  (uint64_t)(stack.back().GetScalar().GetByteSize() * 8));
            return false;
          }
        } break;

        case Value::eValueTypeFileAddress:
        case Value::eValueTypeLoadAddress:
        case Value::eValueTypeHostAddress:
          if (error_ptr) {
            error_ptr->SetErrorStringWithFormat(
                "unable to extract DW_OP_bit_piece(bit_size = %" PRIu64
                ", bit_offset = %" PRIu64 ") from an address value.",
                piece_bit_size, piece_bit_offset);
          }
          return false;

        case Value::eValueTypeVector:
          if (error_ptr) {
            error_ptr->SetErrorStringWithFormat(
                "unable to extract DW_OP_bit_piece(bit_size = %" PRIu64
                ", bit_offset = %" PRIu64 ") from a vector value.",
                piece_bit_size, piece_bit_offset);
          }
          return false;
        }
      }
      break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_push_object_address
    // OPERANDS: none
    // DESCRIPTION: Pushes the address of the object currently being
    // evaluated as part of evaluation of a user presented expression. This
    // object may correspond to an independent variable described by its own
    // DIE or it may be a component of an array, structure, or class whose
    // address has been dynamically determined by an earlier step during user
    // expression evaluation.
    //----------------------------------------------------------------------
    case DW_OP_push_object_address:
      if (object_address_ptr)
        stack.push_back(*object_address_ptr);
      else {
        if (error_ptr)
          error_ptr->SetErrorString("DW_OP_push_object_address used without "
                                    "specifying an object address");
        return false;
      }
      break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_call2
    // OPERANDS:
    //      uint16_t compile unit relative offset of a DIE
    // DESCRIPTION: Performs subroutine calls during evaluation
    // of a DWARF expression. The operand is the 2-byte unsigned offset of a
    // debugging information entry in the current compilation unit.
    //
    // Operand interpretation is exactly like that for DW_FORM_ref2.
    //
    // This operation transfers control of DWARF expression evaluation to the
    // DW_AT_location attribute of the referenced DIE. If there is no such
    // attribute, then there is no effect. Execution of the DWARF expression of
    // a DW_AT_location attribute may add to and/or remove from values on the
    // stack. Execution returns to the point following the call when the end of
    // the attribute is reached. Values on the stack at the time of the call
    // may be used as parameters by the called expression and values left on
    // the stack by the called expression may be used as return values by prior
    // agreement between the calling and called expressions.
    //----------------------------------------------------------------------
    case DW_OP_call2:
      if (error_ptr)
        error_ptr->SetErrorString("Unimplemented opcode DW_OP_call2.");
      return false;
    //----------------------------------------------------------------------
    // OPCODE: DW_OP_call4
    // OPERANDS: 1
    //      uint32_t compile unit relative offset of a DIE
    // DESCRIPTION: Performs a subroutine call during evaluation of a DWARF
    // expression. For DW_OP_call4, the operand is a 4-byte unsigned offset of
    // a debugging information entry in  the current compilation unit.
    //
    // Operand interpretation DW_OP_call4 is exactly like that for
    // DW_FORM_ref4.
    //
    // This operation transfers control of DWARF expression evaluation to the
    // DW_AT_location attribute of the referenced DIE. If there is no such
    // attribute, then there is no effect. Execution of the DWARF expression of
    // a DW_AT_location attribute may add to and/or remove from values on the
    // stack. Execution returns to the point following the call when the end of
    // the attribute is reached. Values on the stack at the time of the call
    // may be used as parameters by the called expression and values left on
    // the stack by the called expression may be used as return values by prior
    // agreement between the calling and called expressions.
    //----------------------------------------------------------------------
    case DW_OP_call4:
      if (error_ptr)
        error_ptr->SetErrorString("Unimplemented opcode DW_OP_call4.");
      return false;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_stack_value
    // OPERANDS: None
    // DESCRIPTION: Specifies that the object does not exist in memory but
    // rather is a constant value.  The value from the top of the stack is the
    // value to be used.  This is the actual object value and not the location.
    //----------------------------------------------------------------------
    case DW_OP_stack_value:
      stack.back().SetValueType(Value::eValueTypeScalar);
      break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_call_frame_cfa
    // OPERANDS: None
    // DESCRIPTION: Specifies a DWARF expression that pushes the value of
    // the canonical frame address consistent with the call frame information
    // located in .debug_frame (or in the FDEs of the eh_frame section).
    //----------------------------------------------------------------------
    case DW_OP_call_frame_cfa:
      if (frame) {
        // Note that we don't have to parse FDEs because this DWARF expression
        // is commonly evaluated with a valid stack frame.
        StackID id = frame->GetStackID();
        addr_t cfa = id.GetCallFrameAddress();
        if (cfa != LLDB_INVALID_ADDRESS) {
          stack.push_back(Scalar(cfa));
          stack.back().SetValueType(Value::eValueTypeLoadAddress);
        } else if (error_ptr)
          error_ptr->SetErrorString("Stack frame does not include a canonical "
                                    "frame address for DW_OP_call_frame_cfa "
                                    "opcode.");
      } else {
        if (error_ptr)
          error_ptr->SetErrorString("Invalid stack frame in context for "
                                    "DW_OP_call_frame_cfa opcode.");
        return false;
      }
      break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_form_tls_address (or the old pre-DWARFv3 vendor extension
    // opcode, DW_OP_GNU_push_tls_address)
    // OPERANDS: none
    // DESCRIPTION: Pops a TLS offset from the stack, converts it to
    // an address in the current thread's thread-local storage block, and
    // pushes it on the stack.
    //----------------------------------------------------------------------
    case DW_OP_form_tls_address:
    case DW_OP_GNU_push_tls_address: {
      if (stack.size() < 1) {
        if (error_ptr) {
          if (op == DW_OP_form_tls_address)
            error_ptr->SetErrorString(
                "DW_OP_form_tls_address needs an argument.");
          else
            error_ptr->SetErrorString(
                "DW_OP_GNU_push_tls_address needs an argument.");
        }
        return false;
      }

      if (!exe_ctx || !module_sp) {
        if (error_ptr)
          error_ptr->SetErrorString("No context to evaluate TLS within.");
        return false;
      }

      Thread *thread = exe_ctx->GetThreadPtr();
      if (!thread) {
        if (error_ptr)
          error_ptr->SetErrorString("No thread to evaluate TLS within.");
        return false;
      }

      // Lookup the TLS block address for this thread and module.
      const addr_t tls_file_addr =
          stack.back().GetScalar().ULongLong(LLDB_INVALID_ADDRESS);
      const addr_t tls_load_addr =
          thread->GetThreadLocalData(module_sp, tls_file_addr);

      if (tls_load_addr == LLDB_INVALID_ADDRESS) {
        if (error_ptr)
          error_ptr->SetErrorString(
              "No TLS data currently exists for this thread.");
        return false;
      }

      stack.back().GetScalar() = tls_load_addr;
      stack.back().SetValueType(Value::eValueTypeLoadAddress);
    } break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_GNU_addr_index
    // OPERANDS: 1
    //      ULEB128: index to the .debug_addr section
    // DESCRIPTION: Pushes an address to the stack from the .debug_addr
    // section with the base address specified by the DW_AT_addr_base attribute
    // and the 0 based index is the ULEB128 encoded index.
    //----------------------------------------------------------------------
    case DW_OP_GNU_addr_index: {
      if (!dwarf_cu) {
        if (error_ptr)
          error_ptr->SetErrorString("DW_OP_GNU_addr_index found without a "
                                    "compile unit being specified");
        return false;
      }
      uint64_t index = opcodes.GetULEB128(&offset);
      uint32_t index_size = dwarf_cu->GetAddressByteSize();
      dw_offset_t addr_base = dwarf_cu->GetAddrBase();
      lldb::offset_t offset = addr_base + index * index_size;
      uint64_t value =
          dwarf_cu->GetSymbolFileDWARF()->get_debug_addr_data().GetMaxU64(
              &offset, index_size);
      stack.push_back(Scalar(value));
      stack.back().SetValueType(Value::eValueTypeFileAddress);
    } break;

    //----------------------------------------------------------------------
    // OPCODE: DW_OP_GNU_const_index
    // OPERANDS: 1
    //      ULEB128: index to the .debug_addr section
    // DESCRIPTION: Pushes an constant with the size of a machine address to
    // the stack from the .debug_addr section with the base address specified
    // by the DW_AT_addr_base attribute and the 0 based index is the ULEB128
    // encoded index.
    //----------------------------------------------------------------------
    case DW_OP_GNU_const_index: {
      if (!dwarf_cu) {
        if (error_ptr)
          error_ptr->SetErrorString("DW_OP_GNU_const_index found without a "
                                    "compile unit being specified");
        return false;
      }
      uint64_t index = opcodes.GetULEB128(&offset);
      uint32_t index_size = dwarf_cu->GetAddressByteSize();
      dw_offset_t addr_base = dwarf_cu->GetAddrBase();
      lldb::offset_t offset = addr_base + index * index_size;
      const DWARFDataExtractor &debug_addr =
          dwarf_cu->GetSymbolFileDWARF()->get_debug_addr_data();
      switch (index_size) {
      case 4:
        stack.push_back(Scalar(debug_addr.GetU32(&offset)));
        break;
      case 8:
        stack.push_back(Scalar(debug_addr.GetU64(&offset)));
        break;
      default:
        assert(false && "Unhandled index size");
        return false;
      }
    } break;

    default:
      if (log)
        log->Printf("Unhandled opcode %s in DWARFExpression.",
                    DW_OP_value_to_name(op));
      break;
    }
  }

  if (stack.empty()) {
    // Nothing on the stack, check if we created a piece value from DW_OP_piece
    // or DW_OP_bit_piece opcodes
    if (pieces.GetBuffer().GetByteSize()) {
      result = pieces;
    } else {
      if (error_ptr)
        error_ptr->SetErrorString("Stack empty after evaluation.");
      return false;
    }
  } else {
    if (log && log->GetVerbose()) {
      size_t count = stack.size();
      log->Printf("Stack after operation has %" PRIu64 " values:",
                  (uint64_t)count);
      for (size_t i = 0; i < count; ++i) {
        StreamString new_value;
        new_value.Printf("[%" PRIu64 "]", (uint64_t)i);
        stack[i].Dump(&new_value);
        log->Printf("  %s", new_value.GetData());
      }
    }
    result = stack.back();
  }
  return true; // Return true on success
}

size_t DWARFExpression::LocationListSize(const DWARFUnit *dwarf_cu,
                                         const DataExtractor &debug_loc_data,
                                         lldb::offset_t offset) {
  const lldb::offset_t debug_loc_offset = offset;
  while (debug_loc_data.ValidOffset(offset)) {
    lldb::addr_t start_addr = LLDB_INVALID_ADDRESS;
    lldb::addr_t end_addr = LLDB_INVALID_ADDRESS;
    if (!AddressRangeForLocationListEntry(dwarf_cu, debug_loc_data, &offset,
                                          start_addr, end_addr))
      break;

    if (start_addr == 0 && end_addr == 0)
      break;

    uint16_t loc_length = debug_loc_data.GetU16(&offset);
    offset += loc_length;
  }

  if (offset > debug_loc_offset)
    return offset - debug_loc_offset;
  return 0;
}

bool DWARFExpression::AddressRangeForLocationListEntry(
    const DWARFUnit *dwarf_cu, const DataExtractor &debug_loc_data,
    lldb::offset_t *offset_ptr, lldb::addr_t &low_pc, lldb::addr_t &high_pc) {
  if (!debug_loc_data.ValidOffset(*offset_ptr))
    return false;

  DWARFExpression::LocationListFormat format =
      dwarf_cu->GetSymbolFileDWARF()->GetLocationListFormat();
  switch (format) {
  case NonLocationList:
    return false;
  case RegularLocationList:
    low_pc = debug_loc_data.GetAddress(offset_ptr);
    high_pc = debug_loc_data.GetAddress(offset_ptr);
    return true;
  case SplitDwarfLocationList:
  case LocLists:
    switch (debug_loc_data.GetU8(offset_ptr)) {
    case DW_LLE_end_of_list:
      return false;
    case DW_LLE_startx_endx: {
      uint64_t index = debug_loc_data.GetULEB128(offset_ptr);
      low_pc = ReadAddressFromDebugAddrSection(dwarf_cu, index);
      index = debug_loc_data.GetULEB128(offset_ptr);
      high_pc = ReadAddressFromDebugAddrSection(dwarf_cu, index);
      return true;
    }
    case DW_LLE_startx_length: {
      uint64_t index = debug_loc_data.GetULEB128(offset_ptr);
      low_pc = ReadAddressFromDebugAddrSection(dwarf_cu, index);
      uint64_t length = (format == LocLists)
                            ? debug_loc_data.GetULEB128(offset_ptr)
                            : debug_loc_data.GetU32(offset_ptr);
      high_pc = low_pc + length;
      return true;
    }
    case DW_LLE_start_length: {
      low_pc = debug_loc_data.GetAddress(offset_ptr);
      high_pc = low_pc + debug_loc_data.GetULEB128(offset_ptr);
      return true;
    }
    case DW_LLE_start_end: {
      low_pc = debug_loc_data.GetAddress(offset_ptr);
      high_pc = debug_loc_data.GetAddress(offset_ptr);
      return true;
    }
    default:
      // Not supported entry type
      lldbassert(false && "Not supported location list type");
      return false;
    }
  }
  assert(false && "Not supported location list type");
  return false;
}

static bool print_dwarf_exp_op(Stream &s, const DataExtractor &data,
                               lldb::offset_t *offset_ptr, int address_size,
                               int dwarf_ref_size) {
  uint8_t opcode = data.GetU8(offset_ptr);
  DRC_class opcode_class;
  uint64_t uint;
  int64_t sint;

  int size;

  opcode_class = DW_OP_value_to_class(opcode) & (~DRC_DWARFv3);

  s.Printf("%s ", DW_OP_value_to_name(opcode));

  /* Does this take zero parameters?  If so we can shortcut this function.  */
  if (opcode_class == DRC_ZEROOPERANDS)
    return true;

  if (opcode_class == DRC_TWOOPERANDS && opcode == DW_OP_bregx) {
    uint = data.GetULEB128(offset_ptr);
    sint = data.GetSLEB128(offset_ptr);
    s.Printf("%" PRIu64 " %" PRIi64, uint, sint);
    return true;
  }
  if (opcode_class != DRC_ONEOPERAND) {
    s.Printf("UNKNOWN OP %u", opcode);
    return false;
  }

  switch (opcode) {
  case DW_OP_addr:
    size = address_size;
    break;
  case DW_OP_const1u:
    size = 1;
    break;
  case DW_OP_const1s:
    size = -1;
    break;
  case DW_OP_const2u:
    size = 2;
    break;
  case DW_OP_const2s:
    size = -2;
    break;
  case DW_OP_const4u:
    size = 4;
    break;
  case DW_OP_const4s:
    size = -4;
    break;
  case DW_OP_const8u:
    size = 8;
    break;
  case DW_OP_const8s:
    size = -8;
    break;
  case DW_OP_constu:
    size = 128;
    break;
  case DW_OP_consts:
    size = -128;
    break;
  case DW_OP_fbreg:
    size = -128;
    break;
  case DW_OP_breg0:
  case DW_OP_breg1:
  case DW_OP_breg2:
  case DW_OP_breg3:
  case DW_OP_breg4:
  case DW_OP_breg5:
  case DW_OP_breg6:
  case DW_OP_breg7:
  case DW_OP_breg8:
  case DW_OP_breg9:
  case DW_OP_breg10:
  case DW_OP_breg11:
  case DW_OP_breg12:
  case DW_OP_breg13:
  case DW_OP_breg14:
  case DW_OP_breg15:
  case DW_OP_breg16:
  case DW_OP_breg17:
  case DW_OP_breg18:
  case DW_OP_breg19:
  case DW_OP_breg20:
  case DW_OP_breg21:
  case DW_OP_breg22:
  case DW_OP_breg23:
  case DW_OP_breg24:
  case DW_OP_breg25:
  case DW_OP_breg26:
  case DW_OP_breg27:
  case DW_OP_breg28:
  case DW_OP_breg29:
  case DW_OP_breg30:
  case DW_OP_breg31:
    size = -128;
    break;
  case DW_OP_pick:
  case DW_OP_deref_size:
  case DW_OP_xderef_size:
    size = 1;
    break;
  case DW_OP_skip:
  case DW_OP_bra:
    size = -2;
    break;
  case DW_OP_call2:
    size = 2;
    break;
  case DW_OP_call4:
    size = 4;
    break;
  case DW_OP_call_ref:
    size = dwarf_ref_size;
    break;
  case DW_OP_piece:
  case DW_OP_plus_uconst:
  case DW_OP_regx:
  case DW_OP_GNU_addr_index:
  case DW_OP_GNU_const_index:
    size = 128;
    break;
  default:
    s.Printf("UNKNOWN ONE-OPERAND OPCODE, #%u", opcode);
    return true;
  }

  switch (size) {
  case -1:
    sint = (int8_t)data.GetU8(offset_ptr);
    s.Printf("%+" PRIi64, sint);
    break;
  case -2:
    sint = (int16_t)data.GetU16(offset_ptr);
    s.Printf("%+" PRIi64, sint);
    break;
  case -4:
    sint = (int32_t)data.GetU32(offset_ptr);
    s.Printf("%+" PRIi64, sint);
    break;
  case -8:
    sint = (int64_t)data.GetU64(offset_ptr);
    s.Printf("%+" PRIi64, sint);
    break;
  case -128:
    sint = data.GetSLEB128(offset_ptr);
    s.Printf("%+" PRIi64, sint);
    break;
  case 1:
    uint = data.GetU8(offset_ptr);
    s.Printf("0x%2.2" PRIx64, uint);
    break;
  case 2:
    uint = data.GetU16(offset_ptr);
    s.Printf("0x%4.4" PRIx64, uint);
    break;
  case 4:
    uint = data.GetU32(offset_ptr);
    s.Printf("0x%8.8" PRIx64, uint);
    break;
  case 8:
    uint = data.GetU64(offset_ptr);
    s.Printf("0x%16.16" PRIx64, uint);
    break;
  case 128:
    uint = data.GetULEB128(offset_ptr);
    s.Printf("0x%" PRIx64, uint);
    break;
  }

  return false;
}

bool DWARFExpression::PrintDWARFExpression(Stream &s, const DataExtractor &data,
                                           int address_size, int dwarf_ref_size,
                                           bool location_expression) {
  int op_count = 0;
  lldb::offset_t offset = 0;
  while (data.ValidOffset(offset)) {
    if (location_expression && op_count > 0)
      return false;
    if (op_count > 0)
      s.PutCString(", ");
    if (!print_dwarf_exp_op(s, data, &offset, address_size, dwarf_ref_size))
      return false;
    op_count++;
  }

  return true;
}

void DWARFExpression::PrintDWARFLocationList(
    Stream &s, const DWARFUnit *cu, const DataExtractor &debug_loc_data,
    lldb::offset_t offset) {
  uint64_t start_addr, end_addr;
  uint32_t addr_size = DWARFUnit::GetAddressByteSize(cu);
  s.SetAddressByteSize(DWARFUnit::GetAddressByteSize(cu));
  dw_addr_t base_addr = cu ? cu->GetBaseAddress() : 0;
  while (debug_loc_data.ValidOffset(offset)) {
    start_addr = debug_loc_data.GetMaxU64(&offset, addr_size);
    end_addr = debug_loc_data.GetMaxU64(&offset, addr_size);

    if (start_addr == 0 && end_addr == 0)
      break;

    s.PutCString("\n            ");
    s.Indent();
    if (cu)
      s.AddressRange(start_addr + base_addr, end_addr + base_addr,
                     cu->GetAddressByteSize(), NULL, ": ");
    uint32_t loc_length = debug_loc_data.GetU16(&offset);

    DataExtractor locationData(debug_loc_data, offset, loc_length);
    PrintDWARFExpression(s, locationData, addr_size, 4, false);
    offset += loc_length;
  }
}

bool DWARFExpression::GetOpAndEndOffsets(StackFrame &frame,
                                         lldb::offset_t &op_offset,
                                         lldb::offset_t &end_offset) {
  SymbolContext sc = frame.GetSymbolContext(eSymbolContextFunction);
  if (!sc.function) {
    return false;
  }

  addr_t loclist_base_file_addr =
      sc.function->GetAddressRange().GetBaseAddress().GetFileAddress();
  if (loclist_base_file_addr == LLDB_INVALID_ADDRESS) {
    return false;
  }

  addr_t pc_file_addr = frame.GetFrameCodeAddress().GetFileAddress();
  lldb::offset_t opcodes_offset, opcodes_length;
  if (!GetLocation(loclist_base_file_addr, pc_file_addr, opcodes_offset,
                   opcodes_length)) {
    return false;
  }

  if (opcodes_length == 0) {
    return false;
  }

  op_offset = opcodes_offset;
  end_offset = opcodes_offset + opcodes_length;
  return true;
}

bool DWARFExpression::MatchesOperand(StackFrame &frame,
                                     const Instruction::Operand &operand) {
  using namespace OperandMatchers;

  lldb::offset_t op_offset;
  lldb::offset_t end_offset;
  if (!GetOpAndEndOffsets(frame, op_offset, end_offset)) {
    return false;
  }

  if (!m_data.ValidOffset(op_offset) || op_offset >= end_offset) {
    return false;
  }

  RegisterContextSP reg_ctx_sp = frame.GetRegisterContext();
  if (!reg_ctx_sp) {
    return false;
  }

  DataExtractor opcodes = m_data;
  uint8_t opcode = opcodes.GetU8(&op_offset);

  if (opcode == DW_OP_fbreg) {
    int64_t offset = opcodes.GetSLEB128(&op_offset);

    DWARFExpression *fb_expr = frame.GetFrameBaseExpression(nullptr);
    if (!fb_expr) {
      return false;
    }

    auto recurse = [&frame, fb_expr](const Instruction::Operand &child) {
      return fb_expr->MatchesOperand(frame, child);
    };

    if (!offset &&
        MatchUnaryOp(MatchOpType(Instruction::Operand::Type::Dereference),
                     recurse)(operand)) {
      return true;
    }

    return MatchUnaryOp(
        MatchOpType(Instruction::Operand::Type::Dereference),
        MatchBinaryOp(MatchOpType(Instruction::Operand::Type::Sum),
                      MatchImmOp(offset), recurse))(operand);
  }

  bool dereference = false;
  const RegisterInfo *reg = nullptr;
  int64_t offset = 0;

  if (opcode >= DW_OP_reg0 && opcode <= DW_OP_reg31) {
    reg = reg_ctx_sp->GetRegisterInfo(m_reg_kind, opcode - DW_OP_reg0);
  } else if (opcode >= DW_OP_breg0 && opcode <= DW_OP_breg31) {
    offset = opcodes.GetSLEB128(&op_offset);
    reg = reg_ctx_sp->GetRegisterInfo(m_reg_kind, opcode - DW_OP_breg0);
  } else if (opcode == DW_OP_regx) {
    uint32_t reg_num = static_cast<uint32_t>(opcodes.GetULEB128(&op_offset));
    reg = reg_ctx_sp->GetRegisterInfo(m_reg_kind, reg_num);
  } else if (opcode == DW_OP_bregx) {
    uint32_t reg_num = static_cast<uint32_t>(opcodes.GetULEB128(&op_offset));
    offset = opcodes.GetSLEB128(&op_offset);
    reg = reg_ctx_sp->GetRegisterInfo(m_reg_kind, reg_num);
  } else {
    return false;
  }

  if (!reg) {
    return false;
  }

  if (dereference) {
    if (!offset &&
        MatchUnaryOp(MatchOpType(Instruction::Operand::Type::Dereference),
                     MatchRegOp(*reg))(operand)) {
      return true;
    }

    return MatchUnaryOp(
        MatchOpType(Instruction::Operand::Type::Dereference),
        MatchBinaryOp(MatchOpType(Instruction::Operand::Type::Sum),
                      MatchRegOp(*reg),
                      MatchImmOp(offset)))(operand);
  } else {
    return MatchRegOp(*reg)(operand);
  }
}

