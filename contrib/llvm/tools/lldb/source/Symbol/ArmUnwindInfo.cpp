//===-- ArmUnwindInfo.cpp ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <vector>

#include "Utility/ARM_DWARF_Registers.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/Section.h"
#include "lldb/Symbol/ArmUnwindInfo.h"
#include "lldb/Symbol/SymbolVendor.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Utility/Endian.h"

/*
 * Unwind information reader and parser for the ARM exception handling ABI
 *
 * Implemented based on:
 *     Exception Handling ABI for the ARM Architecture
 *     Document number: ARM IHI 0038A (current through ABI r2.09)
 *     Date of Issue: 25th January 2007, reissued 30th November 2012
 *     http://infocenter.arm.com/help/topic/com.arm.doc.ihi0038a/IHI0038A_ehabi.pdf
 */

using namespace lldb;
using namespace lldb_private;

// Converts a prel31 avlue to lldb::addr_t with sign extension
static addr_t Prel31ToAddr(uint32_t prel31) {
  addr_t res = prel31;
  if (prel31 & (1 << 30))
    res |= 0xffffffff80000000ULL;
  return res;
}

ArmUnwindInfo::ArmExidxEntry::ArmExidxEntry(uint32_t f, lldb::addr_t a,
                                            uint32_t d)
    : file_address(f), address(a), data(d) {}

bool ArmUnwindInfo::ArmExidxEntry::operator<(const ArmExidxEntry &other) const {
  return address < other.address;
}

ArmUnwindInfo::ArmUnwindInfo(ObjectFile &objfile, SectionSP &arm_exidx,
                             SectionSP &arm_extab)
    : m_byte_order(objfile.GetByteOrder()), m_arm_exidx_sp(arm_exidx),
      m_arm_extab_sp(arm_extab) {
  objfile.ReadSectionData(arm_exidx.get(), m_arm_exidx_data);
  objfile.ReadSectionData(arm_extab.get(), m_arm_extab_data);

  addr_t exidx_base_addr = m_arm_exidx_sp->GetFileAddress();

  offset_t offset = 0;
  while (m_arm_exidx_data.ValidOffset(offset)) {
    lldb::addr_t file_addr = exidx_base_addr + offset;
    lldb::addr_t addr = exidx_base_addr + (addr_t)offset +
                        Prel31ToAddr(m_arm_exidx_data.GetU32(&offset));
    uint32_t data = m_arm_exidx_data.GetU32(&offset);
    m_exidx_entries.emplace_back(file_addr, addr, data);
  }

  // Sort the entries in the exidx section. The entries should be sorted inside
  // the section but some old compiler isn't sorted them.
  llvm::sort(m_exidx_entries.begin(), m_exidx_entries.end());
}

ArmUnwindInfo::~ArmUnwindInfo() {}

// Read a byte from the unwind instruction stream with the given offset. Custom
// function is required because have to red in order of significance within
// their containing word (most significant byte first) and in increasing word
// address order.
uint8_t ArmUnwindInfo::GetByteAtOffset(const uint32_t *data,
                                       uint16_t offset) const {
  uint32_t value = data[offset / 4];
  if (m_byte_order != endian::InlHostByteOrder())
    value = llvm::ByteSwap_32(value);
  return (value >> ((3 - (offset % 4)) * 8)) & 0xff;
}

uint64_t ArmUnwindInfo::GetULEB128(const uint32_t *data, uint16_t &offset,
                                   uint16_t max_offset) const {
  uint64_t result = 0;
  uint8_t shift = 0;
  while (offset < max_offset) {
    uint8_t byte = GetByteAtOffset(data, offset++);
    result |= (uint64_t)(byte & 0x7f) << shift;
    if ((byte & 0x80) == 0)
      break;
    shift += 7;
  }
  return result;
}

bool ArmUnwindInfo::GetUnwindPlan(Target &target, const Address &addr,
                                  UnwindPlan &unwind_plan) {
  const uint32_t *data = (const uint32_t *)GetExceptionHandlingTableEntry(addr);
  if (data == nullptr)
    return false; // No unwind information for the function

  if (data[0] == 0x1)
    return false; // EXIDX_CANTUNWIND

  uint16_t byte_count = 0;
  uint16_t byte_offset = 0;
  if (data[0] & 0x80000000) {
    switch ((data[0] >> 24) & 0x0f) {
    case 0:
      byte_count = 4;
      byte_offset = 1;
      break;
    case 1:
    case 2:
      byte_count = 4 * ((data[0] >> 16) & 0xff) + 4;
      byte_offset = 2;
      break;
    default:
      // Unhandled personality routine index
      return false;
    }
  } else {
    byte_count = 4 * ((data[1] >> 24) & 0xff) + 8;
    byte_offset = 5;
  }

  uint8_t vsp_reg = dwarf_sp;
  int32_t vsp = 0;
  std::vector<std::pair<uint32_t, int32_t>>
      register_offsets; // register -> (offset from vsp_reg)

  while (byte_offset < byte_count) {
    uint8_t byte1 = GetByteAtOffset(data, byte_offset++);
    if ((byte1 & 0xc0) == 0x00) {
      // 00xxxxxx
      // vsp = vsp + (xxxxxx << 2) + 4. Covers range 0x04-0x100 inclusive
      vsp += ((byte1 & 0x3f) << 2) + 4;
    } else if ((byte1 & 0xc0) == 0x40) {
      // 01xxxxxx
      // vsp = vsp – (xxxxxx << 2) - 4. Covers range 0x04-0x100 inclusive
      vsp -= ((byte1 & 0x3f) << 2) + 4;
    } else if ((byte1 & 0xf0) == 0x80) {
      if (byte_offset >= byte_count)
        return false;

      uint8_t byte2 = GetByteAtOffset(data, byte_offset++);
      if (byte1 == 0x80 && byte2 == 0) {
        // 10000000 00000000
        // Refuse to unwind (for example, out of a cleanup) (see remark a)
        return false;
      } else {
        // 1000iiii iiiiiiii (i not all 0)
        // Pop up to 12 integer registers under masks {r15-r12}, {r11-r4} (see
        // remark b)
        uint16_t regs = ((byte1 & 0x0f) << 8) | byte2;
        for (uint8_t i = 0; i < 12; ++i) {
          if (regs & (1 << i)) {
            register_offsets.emplace_back(dwarf_r4 + i, vsp);
            vsp += 4;
          }
        }
      }
    } else if ((byte1 & 0xff) == 0x9d) {
      // 10011101
      // Reserved as prefix for ARM register to register moves
      return false;
    } else if ((byte1 & 0xff) == 0x9f) {
      // 10011111
      // Reserved as prefix for Intel Wireless MMX register to register moves
      return false;
    } else if ((byte1 & 0xf0) == 0x90) {
      // 1001nnnn (nnnn != 13,15)
      // Set vsp = r[nnnn]
      vsp_reg = dwarf_r0 + (byte1 & 0x0f);
    } else if ((byte1 & 0xf8) == 0xa0) {
      // 10100nnn
      // Pop r4-r[4+nnn]
      uint8_t n = byte1 & 0x7;
      for (uint8_t i = 0; i <= n; ++i) {
        register_offsets.emplace_back(dwarf_r4 + i, vsp);
        vsp += 4;
      }
    } else if ((byte1 & 0xf8) == 0xa8) {
      // 10101nnn
      // Pop r4-r[4+nnn], r14
      uint8_t n = byte1 & 0x7;
      for (uint8_t i = 0; i <= n; ++i) {
        register_offsets.emplace_back(dwarf_r4 + i, vsp);
        vsp += 4;
      }

      register_offsets.emplace_back(dwarf_lr, vsp);
      vsp += 4;
    } else if ((byte1 & 0xff) == 0xb0) {
      // 10110000
      // Finish (see remark c)
      break;
    } else if ((byte1 & 0xff) == 0xb1) {
      if (byte_offset >= byte_count)
        return false;

      uint8_t byte2 = GetByteAtOffset(data, byte_offset++);
      if ((byte2 & 0xff) == 0x00) {
        // 10110001 00000000
        // Spare (see remark f)
        return false;
      } else if ((byte2 & 0xf0) == 0x00) {
        // 10110001 0000iiii (i not all 0)
        // Pop integer registers under mask {r3, r2, r1, r0}
        for (uint8_t i = 0; i < 4; ++i) {
          if (byte2 & (1 << i)) {
            register_offsets.emplace_back(dwarf_r0 + i, vsp);
            vsp += 4;
          }
        }
      } else {
        // 10110001 xxxxyyyy
        // Spare (xxxx != 0000)
        return false;
      }
    } else if ((byte1 & 0xff) == 0xb2) {
      // 10110010 uleb128
      // vsp = vsp + 0x204+ (uleb128 << 2)
      uint64_t uleb128 = GetULEB128(data, byte_offset, byte_count);
      vsp += 0x204 + (uleb128 << 2);
    } else if ((byte1 & 0xff) == 0xb3) {
      // 10110011 sssscccc
      // Pop VFP double-precision registers D[ssss]-D[ssss+cccc] saved (as if)
      // by FSTMFDX (see remark d)
      if (byte_offset >= byte_count)
        return false;

      uint8_t byte2 = GetByteAtOffset(data, byte_offset++);
      uint8_t s = (byte2 & 0xf0) >> 4;
      uint8_t c = (byte2 & 0x0f) >> 0;
      for (uint8_t i = 0; i <= c; ++i) {
        register_offsets.emplace_back(dwarf_d0 + s + i, vsp);
        vsp += 8;
      }
      vsp += 4;
    } else if ((byte1 & 0xfc) == 0xb4) {
      // 101101nn
      // Spare (was Pop FPA)
      return false;
    } else if ((byte1 & 0xf8) == 0xb8) {
      // 10111nnn
      // Pop VFP double-precision registers D[8]-D[8+nnn] saved (as if) by
      // FSTMFDX (see remark d)
      uint8_t n = byte1 & 0x07;
      for (uint8_t i = 0; i <= n; ++i) {
        register_offsets.emplace_back(dwarf_d8 + i, vsp);
        vsp += 8;
      }
      vsp += 4;
    } else if ((byte1 & 0xf8) == 0xc0) {
      // 11000nnn (nnn != 6,7)
      // Intel Wireless MMX pop wR[10]-wR[10+nnn]

      // 11000110 sssscccc
      // Intel Wireless MMX pop wR[ssss]-wR[ssss+cccc] (see remark e)

      // 11000111 00000000
      // Spare

      // 11000111 0000iiii
      // Intel Wireless MMX pop wCGR registers under mask {wCGR3,2,1,0}

      // 11000111 xxxxyyyy
      // Spare (xxxx != 0000)

      return false;
    } else if ((byte1 & 0xff) == 0xc8) {
      // 11001000 sssscccc
      // Pop VFP double precision registers D[16+ssss]-D[16+ssss+cccc] saved
      // (as if) by FSTMFDD (see remarks d,e)
      if (byte_offset >= byte_count)
        return false;

      uint8_t byte2 = GetByteAtOffset(data, byte_offset++);
      uint8_t s = (byte2 & 0xf0) >> 4;
      uint8_t c = (byte2 & 0x0f) >> 0;
      for (uint8_t i = 0; i <= c; ++i) {
        register_offsets.emplace_back(dwarf_d16 + s + i, vsp);
        vsp += 8;
      }
    } else if ((byte1 & 0xff) == 0xc9) {
      // 11001001 sssscccc
      // Pop VFP double precision registers D[ssss]-D[ssss+cccc] saved (as if)
      // by FSTMFDD (see remark d)
      if (byte_offset >= byte_count)
        return false;

      uint8_t byte2 = GetByteAtOffset(data, byte_offset++);
      uint8_t s = (byte2 & 0xf0) >> 4;
      uint8_t c = (byte2 & 0x0f) >> 0;
      for (uint8_t i = 0; i <= c; ++i) {
        register_offsets.emplace_back(dwarf_d0 + s + i, vsp);
        vsp += 8;
      }
    } else if ((byte1 & 0xf8) == 0xc8) {
      // 11001yyy
      // Spare (yyy != 000, 001)
      return false;
    } else if ((byte1 & 0xf8) == 0xc0) {
      // 11010nnn
      // Pop VFP double-precision registers D[8]-D[8+nnn] saved (as if) by
      // FSTMFDD (see remark d)
      uint8_t n = byte1 & 0x07;
      for (uint8_t i = 0; i <= n; ++i) {
        register_offsets.emplace_back(dwarf_d8 + i, vsp);
        vsp += 8;
      }
    } else if ((byte1 & 0xc0) == 0xc0) {
      // 11xxxyyy Spare (xxx != 000, 001, 010)
      return false;
    } else {
      return false;
    }
  }

  UnwindPlan::RowSP row = std::make_shared<UnwindPlan::Row>();
  row->SetOffset(0);
  row->GetCFAValue().SetIsRegisterPlusOffset(vsp_reg, vsp);

  bool have_location_for_pc = false;
  for (const auto &offset : register_offsets) {
    have_location_for_pc |= offset.first == dwarf_pc;
    row->SetRegisterLocationToAtCFAPlusOffset(offset.first, offset.second - vsp,
                                              true);
  }

  if (!have_location_for_pc) {
    UnwindPlan::Row::RegisterLocation lr_location;
    if (row->GetRegisterInfo(dwarf_lr, lr_location))
      row->SetRegisterInfo(dwarf_pc, lr_location);
    else
      row->SetRegisterLocationToRegister(dwarf_pc, dwarf_lr, false);
  }

  unwind_plan.AppendRow(row);
  unwind_plan.SetSourceName("ARM.exidx unwind info");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolYes);
  unwind_plan.SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);
  unwind_plan.SetRegisterKind(eRegisterKindDWARF);

  return true;
}

const uint8_t *
ArmUnwindInfo::GetExceptionHandlingTableEntry(const Address &addr) {
  auto it = std::upper_bound(m_exidx_entries.begin(), m_exidx_entries.end(),
                             ArmExidxEntry{0, addr.GetFileAddress(), 0});
  if (it == m_exidx_entries.begin())
    return nullptr;
  --it;

  if (it->data == 0x1)
    return nullptr; // EXIDX_CANTUNWIND

  if (it->data & 0x80000000)
    return (const uint8_t *)&it->data;

  addr_t data_file_addr = it->file_address + 4 + Prel31ToAddr(it->data);
  return m_arm_extab_data.GetDataStart() +
         (data_file_addr - m_arm_extab_sp->GetFileAddress());
}
