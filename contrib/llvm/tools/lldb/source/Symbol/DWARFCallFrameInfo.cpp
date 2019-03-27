//===-- DWARFCallFrameInfo.cpp ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/DWARFCallFrameInfo.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/Section.h"
#include "lldb/Core/dwarf.h"
#include "lldb/Host/Host.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Timer.h"
#include <list>

using namespace lldb;
using namespace lldb_private;

//----------------------------------------------------------------------
// GetDwarfEHPtr
//
// Used for calls when the value type is specified by a DWARF EH Frame pointer
// encoding.
//----------------------------------------------------------------------
static uint64_t
GetGNUEHPointer(const DataExtractor &DE, offset_t *offset_ptr,
                uint32_t eh_ptr_enc, addr_t pc_rel_addr, addr_t text_addr,
                addr_t data_addr) //, BSDRelocs *data_relocs) const
{
  if (eh_ptr_enc == DW_EH_PE_omit)
    return ULLONG_MAX; // Value isn't in the buffer...

  uint64_t baseAddress = 0;
  uint64_t addressValue = 0;
  const uint32_t addr_size = DE.GetAddressByteSize();
#ifdef LLDB_CONFIGURATION_DEBUG
  assert(addr_size == 4 || addr_size == 8);
#endif

  bool signExtendValue = false;
  // Decode the base part or adjust our offset
  switch (eh_ptr_enc & 0x70) {
  case DW_EH_PE_pcrel:
    signExtendValue = true;
    baseAddress = *offset_ptr;
    if (pc_rel_addr != LLDB_INVALID_ADDRESS)
      baseAddress += pc_rel_addr;
    //      else
    //          Log::GlobalWarning ("PC relative pointer encoding found with
    //          invalid pc relative address.");
    break;

  case DW_EH_PE_textrel:
    signExtendValue = true;
    if (text_addr != LLDB_INVALID_ADDRESS)
      baseAddress = text_addr;
    //      else
    //          Log::GlobalWarning ("text relative pointer encoding being
    //          decoded with invalid text section address, setting base address
    //          to zero.");
    break;

  case DW_EH_PE_datarel:
    signExtendValue = true;
    if (data_addr != LLDB_INVALID_ADDRESS)
      baseAddress = data_addr;
    //      else
    //          Log::GlobalWarning ("data relative pointer encoding being
    //          decoded with invalid data section address, setting base address
    //          to zero.");
    break;

  case DW_EH_PE_funcrel:
    signExtendValue = true;
    break;

  case DW_EH_PE_aligned: {
    // SetPointerSize should be called prior to extracting these so the pointer
    // size is cached
    assert(addr_size != 0);
    if (addr_size) {
      // Align to a address size boundary first
      uint32_t alignOffset = *offset_ptr % addr_size;
      if (alignOffset)
        offset_ptr += addr_size - alignOffset;
    }
  } break;

  default:
    break;
  }

  // Decode the value part
  switch (eh_ptr_enc & DW_EH_PE_MASK_ENCODING) {
  case DW_EH_PE_absptr: {
    addressValue = DE.GetAddress(offset_ptr);
    //          if (data_relocs)
    //              addressValue = data_relocs->Relocate(*offset_ptr -
    //              addr_size, *this, addressValue);
  } break;
  case DW_EH_PE_uleb128:
    addressValue = DE.GetULEB128(offset_ptr);
    break;
  case DW_EH_PE_udata2:
    addressValue = DE.GetU16(offset_ptr);
    break;
  case DW_EH_PE_udata4:
    addressValue = DE.GetU32(offset_ptr);
    break;
  case DW_EH_PE_udata8:
    addressValue = DE.GetU64(offset_ptr);
    break;
  case DW_EH_PE_sleb128:
    addressValue = DE.GetSLEB128(offset_ptr);
    break;
  case DW_EH_PE_sdata2:
    addressValue = (int16_t)DE.GetU16(offset_ptr);
    break;
  case DW_EH_PE_sdata4:
    addressValue = (int32_t)DE.GetU32(offset_ptr);
    break;
  case DW_EH_PE_sdata8:
    addressValue = (int64_t)DE.GetU64(offset_ptr);
    break;
  default:
    // Unhandled encoding type
    assert(eh_ptr_enc);
    break;
  }

  // Since we promote everything to 64 bit, we may need to sign extend
  if (signExtendValue && addr_size < sizeof(baseAddress)) {
    uint64_t sign_bit = 1ull << ((addr_size * 8ull) - 1ull);
    if (sign_bit & addressValue) {
      uint64_t mask = ~sign_bit + 1;
      addressValue |= mask;
    }
  }
  return baseAddress + addressValue;
}

DWARFCallFrameInfo::DWARFCallFrameInfo(ObjectFile &objfile,
                                       SectionSP &section_sp, Type type)
    : m_objfile(objfile), m_section_sp(section_sp), m_type(type) {}

bool DWARFCallFrameInfo::GetUnwindPlan(Address addr, UnwindPlan &unwind_plan) {
  FDEEntryMap::Entry fde_entry;

  // Make sure that the Address we're searching for is the same object file as
  // this DWARFCallFrameInfo, we only store File offsets in m_fde_index.
  ModuleSP module_sp = addr.GetModule();
  if (module_sp.get() == nullptr || module_sp->GetObjectFile() == nullptr ||
      module_sp->GetObjectFile() != &m_objfile)
    return false;

  if (!GetFDEEntryByFileAddress(addr.GetFileAddress(), fde_entry))
    return false;
  return FDEToUnwindPlan(fde_entry.data, addr, unwind_plan);
}

bool DWARFCallFrameInfo::GetAddressRange(Address addr, AddressRange &range) {

  // Make sure that the Address we're searching for is the same object file as
  // this DWARFCallFrameInfo, we only store File offsets in m_fde_index.
  ModuleSP module_sp = addr.GetModule();
  if (module_sp.get() == nullptr || module_sp->GetObjectFile() == nullptr ||
      module_sp->GetObjectFile() != &m_objfile)
    return false;

  if (m_section_sp.get() == nullptr || m_section_sp->IsEncrypted())
    return false;
  GetFDEIndex();
  FDEEntryMap::Entry *fde_entry =
      m_fde_index.FindEntryThatContains(addr.GetFileAddress());
  if (!fde_entry)
    return false;

  range = AddressRange(fde_entry->base, fde_entry->size,
                       m_objfile.GetSectionList());
  return true;
}

bool DWARFCallFrameInfo::GetFDEEntryByFileAddress(
    addr_t file_addr, FDEEntryMap::Entry &fde_entry) {
  if (m_section_sp.get() == nullptr || m_section_sp->IsEncrypted())
    return false;

  GetFDEIndex();

  if (m_fde_index.IsEmpty())
    return false;

  FDEEntryMap::Entry *fde = m_fde_index.FindEntryThatContains(file_addr);

  if (fde == nullptr)
    return false;

  fde_entry = *fde;
  return true;
}

void DWARFCallFrameInfo::GetFunctionAddressAndSizeVector(
    FunctionAddressAndSizeVector &function_info) {
  GetFDEIndex();
  const size_t count = m_fde_index.GetSize();
  function_info.Clear();
  if (count > 0)
    function_info.Reserve(count);
  for (size_t i = 0; i < count; ++i) {
    const FDEEntryMap::Entry *func_offset_data_entry =
        m_fde_index.GetEntryAtIndex(i);
    if (func_offset_data_entry) {
      FunctionAddressAndSizeVector::Entry function_offset_entry(
          func_offset_data_entry->base, func_offset_data_entry->size);
      function_info.Append(function_offset_entry);
    }
  }
}

const DWARFCallFrameInfo::CIE *
DWARFCallFrameInfo::GetCIE(dw_offset_t cie_offset) {
  cie_map_t::iterator pos = m_cie_map.find(cie_offset);

  if (pos != m_cie_map.end()) {
    // Parse and cache the CIE
    if (pos->second.get() == nullptr)
      pos->second = ParseCIE(cie_offset);

    return pos->second.get();
  }
  return nullptr;
}

DWARFCallFrameInfo::CIESP
DWARFCallFrameInfo::ParseCIE(const dw_offset_t cie_offset) {
  CIESP cie_sp(new CIE(cie_offset));
  lldb::offset_t offset = cie_offset;
  if (!m_cfi_data_initialized)
    GetCFIData();
  uint32_t length = m_cfi_data.GetU32(&offset);
  dw_offset_t cie_id, end_offset;
  bool is_64bit = (length == UINT32_MAX);
  if (is_64bit) {
    length = m_cfi_data.GetU64(&offset);
    cie_id = m_cfi_data.GetU64(&offset);
    end_offset = cie_offset + length + 12;
  } else {
    cie_id = m_cfi_data.GetU32(&offset);
    end_offset = cie_offset + length + 4;
  }
  if (length > 0 && ((m_type == DWARF && cie_id == UINT32_MAX) ||
                     (m_type == EH && cie_id == 0ul))) {
    size_t i;
    //    cie.offset = cie_offset;
    //    cie.length = length;
    //    cie.cieID = cieID;
    cie_sp->ptr_encoding = DW_EH_PE_absptr; // default
    cie_sp->version = m_cfi_data.GetU8(&offset);
    if (cie_sp->version > CFI_VERSION4) {
      Host::SystemLog(Host::eSystemLogError,
                      "CIE parse error: CFI version %d is not supported\n",
                      cie_sp->version);
      return nullptr;
    }

    for (i = 0; i < CFI_AUG_MAX_SIZE; ++i) {
      cie_sp->augmentation[i] = m_cfi_data.GetU8(&offset);
      if (cie_sp->augmentation[i] == '\0') {
        // Zero out remaining bytes in augmentation string
        for (size_t j = i + 1; j < CFI_AUG_MAX_SIZE; ++j)
          cie_sp->augmentation[j] = '\0';

        break;
      }
    }

    if (i == CFI_AUG_MAX_SIZE &&
        cie_sp->augmentation[CFI_AUG_MAX_SIZE - 1] != '\0') {
      Host::SystemLog(Host::eSystemLogError,
                      "CIE parse error: CIE augmentation string was too large "
                      "for the fixed sized buffer of %d bytes.\n",
                      CFI_AUG_MAX_SIZE);
      return nullptr;
    }

    // m_cfi_data uses address size from target architecture of the process may
    // ignore these fields?
    if (m_type == DWARF && cie_sp->version >= CFI_VERSION4) {
      cie_sp->address_size = m_cfi_data.GetU8(&offset);
      cie_sp->segment_size = m_cfi_data.GetU8(&offset);
    }

    cie_sp->code_align = (uint32_t)m_cfi_data.GetULEB128(&offset);
    cie_sp->data_align = (int32_t)m_cfi_data.GetSLEB128(&offset);

    cie_sp->return_addr_reg_num =
        m_type == DWARF && cie_sp->version >= CFI_VERSION3
            ? static_cast<uint32_t>(m_cfi_data.GetULEB128(&offset))
            : m_cfi_data.GetU8(&offset);

    if (cie_sp->augmentation[0]) {
      // Get the length of the eh_frame augmentation data which starts with a
      // ULEB128 length in bytes
      const size_t aug_data_len = (size_t)m_cfi_data.GetULEB128(&offset);
      const size_t aug_data_end = offset + aug_data_len;
      const size_t aug_str_len = strlen(cie_sp->augmentation);
      // A 'z' may be present as the first character of the string.
      // If present, the Augmentation Data field shall be present. The contents
      // of the Augmentation Data shall be interpreted according to other
      // characters in the Augmentation String.
      if (cie_sp->augmentation[0] == 'z') {
        // Extract the Augmentation Data
        size_t aug_str_idx = 0;
        for (aug_str_idx = 1; aug_str_idx < aug_str_len; aug_str_idx++) {
          char aug = cie_sp->augmentation[aug_str_idx];
          switch (aug) {
          case 'L':
            // Indicates the presence of one argument in the Augmentation Data
            // of the CIE, and a corresponding argument in the Augmentation
            // Data of the FDE. The argument in the Augmentation Data of the
            // CIE is 1-byte and represents the pointer encoding used for the
            // argument in the Augmentation Data of the FDE, which is the
            // address of a language-specific data area (LSDA). The size of the
            // LSDA pointer is specified by the pointer encoding used.
            cie_sp->lsda_addr_encoding = m_cfi_data.GetU8(&offset);
            break;

          case 'P':
            // Indicates the presence of two arguments in the Augmentation Data
            // of the CIE. The first argument is 1-byte and represents the
            // pointer encoding used for the second argument, which is the
            // address of a personality routine handler. The size of the
            // personality routine pointer is specified by the pointer encoding
            // used.
            //
            // The address of the personality function will be stored at this
            // location.  Pre-execution, it will be all zero's so don't read it
            // until we're trying to do an unwind & the reloc has been
            // resolved.
            {
              uint8_t arg_ptr_encoding = m_cfi_data.GetU8(&offset);
              const lldb::addr_t pc_rel_addr = m_section_sp->GetFileAddress();
              cie_sp->personality_loc = GetGNUEHPointer(
                  m_cfi_data, &offset, arg_ptr_encoding, pc_rel_addr,
                  LLDB_INVALID_ADDRESS, LLDB_INVALID_ADDRESS);
            }
            break;

          case 'R':
            // A 'R' may be present at any position after the
            // first character of the string. The Augmentation Data shall
            // include a 1 byte argument that represents the pointer encoding
            // for the address pointers used in the FDE. Example: 0x1B ==
            // DW_EH_PE_pcrel | DW_EH_PE_sdata4
            cie_sp->ptr_encoding = m_cfi_data.GetU8(&offset);
            break;
          }
        }
      } else if (strcmp(cie_sp->augmentation, "eh") == 0) {
        // If the Augmentation string has the value "eh", then the EH Data
        // field shall be present
      }

      // Set the offset to be the end of the augmentation data just in case we
      // didn't understand any of the data.
      offset = (uint32_t)aug_data_end;
    }

    if (end_offset > offset) {
      cie_sp->inst_offset = offset;
      cie_sp->inst_length = end_offset - offset;
    }
    while (offset < end_offset) {
      uint8_t inst = m_cfi_data.GetU8(&offset);
      uint8_t primary_opcode = inst & 0xC0;
      uint8_t extended_opcode = inst & 0x3F;

      if (!HandleCommonDwarfOpcode(primary_opcode, extended_opcode,
                                   cie_sp->data_align, offset,
                                   cie_sp->initial_row))
        break; // Stop if we hit an unrecognized opcode
    }
  }

  return cie_sp;
}

void DWARFCallFrameInfo::GetCFIData() {
  if (!m_cfi_data_initialized) {
    Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_UNWIND));
    if (log)
      m_objfile.GetModule()->LogMessage(log, "Reading EH frame info");
    m_objfile.ReadSectionData(m_section_sp.get(), m_cfi_data);
    m_cfi_data_initialized = true;
  }
}
// Scan through the eh_frame or debug_frame section looking for FDEs and noting
// the start/end addresses of the functions and a pointer back to the
// function's FDE for later expansion. Internalize CIEs as we come across them.

void DWARFCallFrameInfo::GetFDEIndex() {
  if (m_section_sp.get() == nullptr || m_section_sp->IsEncrypted())
    return;

  if (m_fde_index_initialized)
    return;

  std::lock_guard<std::mutex> guard(m_fde_index_mutex);

  if (m_fde_index_initialized) // if two threads hit the locker
    return;

  static Timer::Category func_cat(LLVM_PRETTY_FUNCTION);
  Timer scoped_timer(func_cat, "%s - %s", LLVM_PRETTY_FUNCTION,
                     m_objfile.GetFileSpec().GetFilename().AsCString(""));

  bool clear_address_zeroth_bit = false;
  if (ArchSpec arch = m_objfile.GetArchitecture()) {
    if (arch.GetTriple().getArch() == llvm::Triple::arm ||
        arch.GetTriple().getArch() == llvm::Triple::thumb)
      clear_address_zeroth_bit = true;
  }

  lldb::offset_t offset = 0;
  if (!m_cfi_data_initialized)
    GetCFIData();
  while (m_cfi_data.ValidOffsetForDataOfSize(offset, 8)) {
    const dw_offset_t current_entry = offset;
    dw_offset_t cie_id, next_entry, cie_offset;
    uint32_t len = m_cfi_data.GetU32(&offset);
    bool is_64bit = (len == UINT32_MAX);
    if (is_64bit) {
      len = m_cfi_data.GetU64(&offset);
      cie_id = m_cfi_data.GetU64(&offset);
      next_entry = current_entry + len + 12;
      cie_offset = current_entry + 12 - cie_id;
    } else {
      cie_id = m_cfi_data.GetU32(&offset);
      next_entry = current_entry + len + 4;
      cie_offset = current_entry + 4 - cie_id;
    }

    if (next_entry > m_cfi_data.GetByteSize() + 1) {
      Host::SystemLog(Host::eSystemLogError, "error: Invalid fde/cie next "
                                             "entry offset of 0x%x found in "
                                             "cie/fde at 0x%x\n",
                      next_entry, current_entry);
      // Don't trust anything in this eh_frame section if we find blatantly
      // invalid data.
      m_fde_index.Clear();
      m_fde_index_initialized = true;
      return;
    }

    // An FDE entry contains CIE_pointer in debug_frame in same place as cie_id
    // in eh_frame. CIE_pointer is an offset into the .debug_frame section. So,
    // variable cie_offset should be equal to cie_id for debug_frame.
    // FDE entries with cie_id == 0 shouldn't be ignored for it.
    if ((cie_id == 0 && m_type == EH) || cie_id == UINT32_MAX || len == 0) {
      auto cie_sp = ParseCIE(current_entry);
      if (!cie_sp) {
        // Cannot parse, the reason is already logged
        m_fde_index.Clear();
        m_fde_index_initialized = true;
        return;
      }

      m_cie_map[current_entry] = std::move(cie_sp);
      offset = next_entry;
      continue;
    }

    if (m_type == DWARF)
      cie_offset = cie_id;

    if (cie_offset > m_cfi_data.GetByteSize()) {
      Host::SystemLog(Host::eSystemLogError,
                      "error: Invalid cie offset of 0x%x "
                      "found in cie/fde at 0x%x\n",
                      cie_offset, current_entry);
      // Don't trust anything in this eh_frame section if we find blatantly
      // invalid data.
      m_fde_index.Clear();
      m_fde_index_initialized = true;
      return;
    }

    const CIE *cie = GetCIE(cie_offset);
    if (cie) {
      const lldb::addr_t pc_rel_addr = m_section_sp->GetFileAddress();
      const lldb::addr_t text_addr = LLDB_INVALID_ADDRESS;
      const lldb::addr_t data_addr = LLDB_INVALID_ADDRESS;

      lldb::addr_t addr =
          GetGNUEHPointer(m_cfi_data, &offset, cie->ptr_encoding, pc_rel_addr,
                          text_addr, data_addr);
      if (clear_address_zeroth_bit)
        addr &= ~1ull;

      lldb::addr_t length = GetGNUEHPointer(
          m_cfi_data, &offset, cie->ptr_encoding & DW_EH_PE_MASK_ENCODING,
          pc_rel_addr, text_addr, data_addr);
      FDEEntryMap::Entry fde(addr, length, current_entry);
      m_fde_index.Append(fde);
    } else {
      Host::SystemLog(Host::eSystemLogError, "error: unable to find CIE at "
                                             "0x%8.8x for cie_id = 0x%8.8x for "
                                             "entry at 0x%8.8x.\n",
                      cie_offset, cie_id, current_entry);
    }
    offset = next_entry;
  }
  m_fde_index.Sort();
  m_fde_index_initialized = true;
}

bool DWARFCallFrameInfo::FDEToUnwindPlan(dw_offset_t dwarf_offset,
                                         Address startaddr,
                                         UnwindPlan &unwind_plan) {
  Log *log = GetLogIfAllCategoriesSet(LIBLLDB_LOG_UNWIND);
  lldb::offset_t offset = dwarf_offset;
  lldb::offset_t current_entry = offset;

  if (m_section_sp.get() == nullptr || m_section_sp->IsEncrypted())
    return false;

  if (!m_cfi_data_initialized)
    GetCFIData();

  uint32_t length = m_cfi_data.GetU32(&offset);
  dw_offset_t cie_offset;
  bool is_64bit = (length == UINT32_MAX);
  if (is_64bit) {
    length = m_cfi_data.GetU64(&offset);
    cie_offset = m_cfi_data.GetU64(&offset);
  } else {
    cie_offset = m_cfi_data.GetU32(&offset);
  }

  // FDE entries with zeroth cie_offset may occur for debug_frame.
  assert(!(m_type == EH && 0 == cie_offset) && cie_offset != UINT32_MAX);

  // Translate the CIE_id from the eh_frame format, which is relative to the
  // FDE offset, into a __eh_frame section offset
  if (m_type == EH) {
    unwind_plan.SetSourceName("eh_frame CFI");
    cie_offset = current_entry + (is_64bit ? 12 : 4) - cie_offset;
    unwind_plan.SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);
  } else {
    unwind_plan.SetSourceName("DWARF CFI");
    // In theory the debug_frame info should be valid at all call sites
    // ("asynchronous unwind info" as it is sometimes called) but in practice
    // gcc et al all emit call frame info for the prologue and call sites, but
    // not for the epilogue or all the other locations during the function
    // reliably.
    unwind_plan.SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);
  }
  unwind_plan.SetSourcedFromCompiler(eLazyBoolYes);

  const CIE *cie = GetCIE(cie_offset);
  assert(cie != nullptr);

  const dw_offset_t end_offset = current_entry + length + (is_64bit ? 12 : 4);

  const lldb::addr_t pc_rel_addr = m_section_sp->GetFileAddress();
  const lldb::addr_t text_addr = LLDB_INVALID_ADDRESS;
  const lldb::addr_t data_addr = LLDB_INVALID_ADDRESS;
  lldb::addr_t range_base =
      GetGNUEHPointer(m_cfi_data, &offset, cie->ptr_encoding, pc_rel_addr,
                      text_addr, data_addr);
  lldb::addr_t range_len = GetGNUEHPointer(
      m_cfi_data, &offset, cie->ptr_encoding & DW_EH_PE_MASK_ENCODING,
      pc_rel_addr, text_addr, data_addr);
  AddressRange range(range_base, m_objfile.GetAddressByteSize(),
                     m_objfile.GetSectionList());
  range.SetByteSize(range_len);

  addr_t lsda_data_file_address = LLDB_INVALID_ADDRESS;

  if (cie->augmentation[0] == 'z') {
    uint32_t aug_data_len = (uint32_t)m_cfi_data.GetULEB128(&offset);
    if (aug_data_len != 0 && cie->lsda_addr_encoding != DW_EH_PE_omit) {
      offset_t saved_offset = offset;
      lsda_data_file_address =
          GetGNUEHPointer(m_cfi_data, &offset, cie->lsda_addr_encoding,
                          pc_rel_addr, text_addr, data_addr);
      if (offset - saved_offset != aug_data_len) {
        // There is more in the augmentation region than we know how to process;
        // don't read anything.
        lsda_data_file_address = LLDB_INVALID_ADDRESS;
      }
      offset = saved_offset;
    }
    offset += aug_data_len;
  }
  Address lsda_data;
  Address personality_function_ptr;

  if (lsda_data_file_address != LLDB_INVALID_ADDRESS &&
      cie->personality_loc != LLDB_INVALID_ADDRESS) {
    m_objfile.GetModule()->ResolveFileAddress(lsda_data_file_address,
                                              lsda_data);
    m_objfile.GetModule()->ResolveFileAddress(cie->personality_loc,
                                              personality_function_ptr);
  }

  if (lsda_data.IsValid() && personality_function_ptr.IsValid()) {
    unwind_plan.SetLSDAAddress(lsda_data);
    unwind_plan.SetPersonalityFunctionPtr(personality_function_ptr);
  }

  uint32_t code_align = cie->code_align;
  int32_t data_align = cie->data_align;

  unwind_plan.SetPlanValidAddressRange(range);
  UnwindPlan::Row *cie_initial_row = new UnwindPlan::Row;
  *cie_initial_row = cie->initial_row;
  UnwindPlan::RowSP row(cie_initial_row);

  unwind_plan.SetRegisterKind(GetRegisterKind());
  unwind_plan.SetReturnAddressRegister(cie->return_addr_reg_num);

  std::vector<UnwindPlan::RowSP> stack;

  UnwindPlan::Row::RegisterLocation reg_location;
  while (m_cfi_data.ValidOffset(offset) && offset < end_offset) {
    uint8_t inst = m_cfi_data.GetU8(&offset);
    uint8_t primary_opcode = inst & 0xC0;
    uint8_t extended_opcode = inst & 0x3F;

    if (!HandleCommonDwarfOpcode(primary_opcode, extended_opcode, data_align,
                                 offset, *row)) {
      if (primary_opcode) {
        switch (primary_opcode) {
        case DW_CFA_advance_loc: // (Row Creation Instruction)
        { // 0x40 - high 2 bits are 0x1, lower 6 bits are delta
          // takes a single argument that represents a constant delta. The
          // required action is to create a new table row with a location value
          // that is computed by taking the current entry's location value and
          // adding (delta * code_align). All other values in the new row are
          // initially identical to the current row.
          unwind_plan.AppendRow(row);
          UnwindPlan::Row *newrow = new UnwindPlan::Row;
          *newrow = *row.get();
          row.reset(newrow);
          row->SlideOffset(extended_opcode * code_align);
          break;
        }

        case DW_CFA_restore: { // 0xC0 - high 2 bits are 0x3, lower 6 bits are
                               // register
          // takes a single argument that represents a register number. The
          // required action is to change the rule for the indicated register
          // to the rule assigned it by the initial_instructions in the CIE.
          uint32_t reg_num = extended_opcode;
          // We only keep enough register locations around to unwind what is in
          // our thread, and these are organized by the register index in that
          // state, so we need to convert our eh_frame register number from the
          // EH frame info, to a register index

          if (unwind_plan.IsValidRowIndex(0) &&
              unwind_plan.GetRowAtIndex(0)->GetRegisterInfo(reg_num,
                                                            reg_location))
            row->SetRegisterInfo(reg_num, reg_location);
          break;
        }
        }
      } else {
        switch (extended_opcode) {
        case DW_CFA_set_loc: // 0x1 (Row Creation Instruction)
        {
          // DW_CFA_set_loc takes a single argument that represents an address.
          // The required action is to create a new table row using the
          // specified address as the location. All other values in the new row
          // are initially identical to the current row. The new location value
          // should always be greater than the current one.
          unwind_plan.AppendRow(row);
          UnwindPlan::Row *newrow = new UnwindPlan::Row;
          *newrow = *row.get();
          row.reset(newrow);
          row->SetOffset(m_cfi_data.GetPointer(&offset) -
                         startaddr.GetFileAddress());
          break;
        }

        case DW_CFA_advance_loc1: // 0x2 (Row Creation Instruction)
        {
          // takes a single uword argument that represents a constant delta.
          // This instruction is identical to DW_CFA_advance_loc except for the
          // encoding and size of the delta argument.
          unwind_plan.AppendRow(row);
          UnwindPlan::Row *newrow = new UnwindPlan::Row;
          *newrow = *row.get();
          row.reset(newrow);
          row->SlideOffset(m_cfi_data.GetU8(&offset) * code_align);
          break;
        }

        case DW_CFA_advance_loc2: // 0x3 (Row Creation Instruction)
        {
          // takes a single uword argument that represents a constant delta.
          // This instruction is identical to DW_CFA_advance_loc except for the
          // encoding and size of the delta argument.
          unwind_plan.AppendRow(row);
          UnwindPlan::Row *newrow = new UnwindPlan::Row;
          *newrow = *row.get();
          row.reset(newrow);
          row->SlideOffset(m_cfi_data.GetU16(&offset) * code_align);
          break;
        }

        case DW_CFA_advance_loc4: // 0x4 (Row Creation Instruction)
        {
          // takes a single uword argument that represents a constant delta.
          // This instruction is identical to DW_CFA_advance_loc except for the
          // encoding and size of the delta argument.
          unwind_plan.AppendRow(row);
          UnwindPlan::Row *newrow = new UnwindPlan::Row;
          *newrow = *row.get();
          row.reset(newrow);
          row->SlideOffset(m_cfi_data.GetU32(&offset) * code_align);
          break;
        }

        case DW_CFA_restore_extended: // 0x6
        {
          // takes a single unsigned LEB128 argument that represents a register
          // number. This instruction is identical to DW_CFA_restore except for
          // the encoding and size of the register argument.
          uint32_t reg_num = (uint32_t)m_cfi_data.GetULEB128(&offset);
          if (unwind_plan.IsValidRowIndex(0) &&
              unwind_plan.GetRowAtIndex(0)->GetRegisterInfo(reg_num,
                                                            reg_location))
            row->SetRegisterInfo(reg_num, reg_location);
          break;
        }

        case DW_CFA_remember_state: // 0xA
        {
          // These instructions define a stack of information. Encountering the
          // DW_CFA_remember_state instruction means to save the rules for
          // every register on the current row on the stack. Encountering the
          // DW_CFA_restore_state instruction means to pop the set of rules off
          // the stack and place them in the current row. (This operation is
          // useful for compilers that move epilogue code into the body of a
          // function.)
          stack.push_back(row);
          UnwindPlan::Row *newrow = new UnwindPlan::Row;
          *newrow = *row.get();
          row.reset(newrow);
          break;
        }

        case DW_CFA_restore_state: // 0xB
        {
          // These instructions define a stack of information. Encountering the
          // DW_CFA_remember_state instruction means to save the rules for
          // every register on the current row on the stack. Encountering the
          // DW_CFA_restore_state instruction means to pop the set of rules off
          // the stack and place them in the current row. (This operation is
          // useful for compilers that move epilogue code into the body of a
          // function.)
          if (stack.empty()) {
            if (log)
              log->Printf("DWARFCallFrameInfo::%s(dwarf_offset: %" PRIx32
                          ", startaddr: %" PRIx64
                          " encountered DW_CFA_restore_state but state stack "
                          "is empty. Corrupt unwind info?",
                          __FUNCTION__, dwarf_offset,
                          startaddr.GetFileAddress());
            break;
          }
          lldb::addr_t offset = row->GetOffset();
          row = stack.back();
          stack.pop_back();
          row->SetOffset(offset);
          break;
        }

        case DW_CFA_GNU_args_size: // 0x2e
        {
          // The DW_CFA_GNU_args_size instruction takes an unsigned LEB128
          // operand representing an argument size. This instruction specifies
          // the total of the size of the arguments which have been pushed onto
          // the stack.

          // TODO: Figure out how we should handle this.
          m_cfi_data.GetULEB128(&offset);
          break;
        }

        case DW_CFA_val_offset:    // 0x14
        case DW_CFA_val_offset_sf: // 0x15
        default:
          break;
        }
      }
    }
  }
  unwind_plan.AppendRow(row);

  return true;
}

bool DWARFCallFrameInfo::HandleCommonDwarfOpcode(uint8_t primary_opcode,
                                                 uint8_t extended_opcode,
                                                 int32_t data_align,
                                                 lldb::offset_t &offset,
                                                 UnwindPlan::Row &row) {
  UnwindPlan::Row::RegisterLocation reg_location;

  if (primary_opcode) {
    switch (primary_opcode) {
    case DW_CFA_offset: { // 0x80 - high 2 bits are 0x2, lower 6 bits are
                          // register
      // takes two arguments: an unsigned LEB128 constant representing a
      // factored offset and a register number. The required action is to
      // change the rule for the register indicated by the register number to
      // be an offset(N) rule with a value of (N = factored offset *
      // data_align).
      uint8_t reg_num = extended_opcode;
      int32_t op_offset = (int32_t)m_cfi_data.GetULEB128(&offset) * data_align;
      reg_location.SetAtCFAPlusOffset(op_offset);
      row.SetRegisterInfo(reg_num, reg_location);
      return true;
    }
    }
  } else {
    switch (extended_opcode) {
    case DW_CFA_nop: // 0x0
      return true;

    case DW_CFA_offset_extended: // 0x5
    {
      // takes two unsigned LEB128 arguments representing a register number and
      // a factored offset. This instruction is identical to DW_CFA_offset
      // except for the encoding and size of the register argument.
      uint32_t reg_num = (uint32_t)m_cfi_data.GetULEB128(&offset);
      int32_t op_offset = (int32_t)m_cfi_data.GetULEB128(&offset) * data_align;
      UnwindPlan::Row::RegisterLocation reg_location;
      reg_location.SetAtCFAPlusOffset(op_offset);
      row.SetRegisterInfo(reg_num, reg_location);
      return true;
    }

    case DW_CFA_undefined: // 0x7
    {
      // takes a single unsigned LEB128 argument that represents a register
      // number. The required action is to set the rule for the specified
      // register to undefined.
      uint32_t reg_num = (uint32_t)m_cfi_data.GetULEB128(&offset);
      UnwindPlan::Row::RegisterLocation reg_location;
      reg_location.SetUndefined();
      row.SetRegisterInfo(reg_num, reg_location);
      return true;
    }

    case DW_CFA_same_value: // 0x8
    {
      // takes a single unsigned LEB128 argument that represents a register
      // number. The required action is to set the rule for the specified
      // register to same value.
      uint32_t reg_num = (uint32_t)m_cfi_data.GetULEB128(&offset);
      UnwindPlan::Row::RegisterLocation reg_location;
      reg_location.SetSame();
      row.SetRegisterInfo(reg_num, reg_location);
      return true;
    }

    case DW_CFA_register: // 0x9
    {
      // takes two unsigned LEB128 arguments representing register numbers. The
      // required action is to set the rule for the first register to be the
      // second register.
      uint32_t reg_num = (uint32_t)m_cfi_data.GetULEB128(&offset);
      uint32_t other_reg_num = (uint32_t)m_cfi_data.GetULEB128(&offset);
      UnwindPlan::Row::RegisterLocation reg_location;
      reg_location.SetInRegister(other_reg_num);
      row.SetRegisterInfo(reg_num, reg_location);
      return true;
    }

    case DW_CFA_def_cfa: // 0xC    (CFA Definition Instruction)
    {
      // Takes two unsigned LEB128 operands representing a register number and
      // a (non-factored) offset. The required action is to define the current
      // CFA rule to use the provided register and offset.
      uint32_t reg_num = (uint32_t)m_cfi_data.GetULEB128(&offset);
      int32_t op_offset = (int32_t)m_cfi_data.GetULEB128(&offset);
      row.GetCFAValue().SetIsRegisterPlusOffset(reg_num, op_offset);
      return true;
    }

    case DW_CFA_def_cfa_register: // 0xD    (CFA Definition Instruction)
    {
      // takes a single unsigned LEB128 argument representing a register
      // number. The required action is to define the current CFA rule to use
      // the provided register (but to keep the old offset).
      uint32_t reg_num = (uint32_t)m_cfi_data.GetULEB128(&offset);
      row.GetCFAValue().SetIsRegisterPlusOffset(reg_num,
                                                row.GetCFAValue().GetOffset());
      return true;
    }

    case DW_CFA_def_cfa_offset: // 0xE    (CFA Definition Instruction)
    {
      // Takes a single unsigned LEB128 operand representing a (non-factored)
      // offset. The required action is to define the current CFA rule to use
      // the provided offset (but to keep the old register).
      int32_t op_offset = (int32_t)m_cfi_data.GetULEB128(&offset);
      row.GetCFAValue().SetIsRegisterPlusOffset(
          row.GetCFAValue().GetRegisterNumber(), op_offset);
      return true;
    }

    case DW_CFA_def_cfa_expression: // 0xF    (CFA Definition Instruction)
    {
      size_t block_len = (size_t)m_cfi_data.GetULEB128(&offset);
      const uint8_t *block_data =
          static_cast<const uint8_t *>(m_cfi_data.GetData(&offset, block_len));
      row.GetCFAValue().SetIsDWARFExpression(block_data, block_len);
      return true;
    }

    case DW_CFA_expression: // 0x10
    {
      // Takes two operands: an unsigned LEB128 value representing a register
      // number, and a DW_FORM_block value representing a DWARF expression. The
      // required action is to change the rule for the register indicated by
      // the register number to be an expression(E) rule where E is the DWARF
      // expression. That is, the DWARF expression computes the address. The
      // value of the CFA is pushed on the DWARF evaluation stack prior to
      // execution of the DWARF expression.
      uint32_t reg_num = (uint32_t)m_cfi_data.GetULEB128(&offset);
      uint32_t block_len = (uint32_t)m_cfi_data.GetULEB128(&offset);
      const uint8_t *block_data =
          static_cast<const uint8_t *>(m_cfi_data.GetData(&offset, block_len));
      UnwindPlan::Row::RegisterLocation reg_location;
      reg_location.SetAtDWARFExpression(block_data, block_len);
      row.SetRegisterInfo(reg_num, reg_location);
      return true;
    }

    case DW_CFA_offset_extended_sf: // 0x11
    {
      // takes two operands: an unsigned LEB128 value representing a register
      // number and a signed LEB128 factored offset. This instruction is
      // identical to DW_CFA_offset_extended except that the second operand is
      // signed and factored.
      uint32_t reg_num = (uint32_t)m_cfi_data.GetULEB128(&offset);
      int32_t op_offset = (int32_t)m_cfi_data.GetSLEB128(&offset) * data_align;
      UnwindPlan::Row::RegisterLocation reg_location;
      reg_location.SetAtCFAPlusOffset(op_offset);
      row.SetRegisterInfo(reg_num, reg_location);
      return true;
    }

    case DW_CFA_def_cfa_sf: // 0x12   (CFA Definition Instruction)
    {
      // Takes two operands: an unsigned LEB128 value representing a register
      // number and a signed LEB128 factored offset. This instruction is
      // identical to DW_CFA_def_cfa except that the second operand is signed
      // and factored.
      uint32_t reg_num = (uint32_t)m_cfi_data.GetULEB128(&offset);
      int32_t op_offset = (int32_t)m_cfi_data.GetSLEB128(&offset) * data_align;
      row.GetCFAValue().SetIsRegisterPlusOffset(reg_num, op_offset);
      return true;
    }

    case DW_CFA_def_cfa_offset_sf: // 0x13   (CFA Definition Instruction)
    {
      // takes a signed LEB128 operand representing a factored offset. This
      // instruction is identical to  DW_CFA_def_cfa_offset except that the
      // operand is signed and factored.
      int32_t op_offset = (int32_t)m_cfi_data.GetSLEB128(&offset) * data_align;
      uint32_t cfa_regnum = row.GetCFAValue().GetRegisterNumber();
      row.GetCFAValue().SetIsRegisterPlusOffset(cfa_regnum, op_offset);
      return true;
    }

    case DW_CFA_val_expression: // 0x16
    {
      // takes two operands: an unsigned LEB128 value representing a register
      // number, and a DW_FORM_block value representing a DWARF expression. The
      // required action is to change the rule for the register indicated by
      // the register number to be a val_expression(E) rule where E is the
      // DWARF expression. That is, the DWARF expression computes the value of
      // the given register. The value of the CFA is pushed on the DWARF
      // evaluation stack prior to execution of the DWARF expression.
      uint32_t reg_num = (uint32_t)m_cfi_data.GetULEB128(&offset);
      uint32_t block_len = (uint32_t)m_cfi_data.GetULEB128(&offset);
      const uint8_t *block_data =
          (const uint8_t *)m_cfi_data.GetData(&offset, block_len);
      //#if defined(__i386__) || defined(__x86_64__)
      //              // The EH frame info for EIP and RIP contains code that
      //              looks for traps to
      //              // be a specific type and increments the PC.
      //              // For i386:
      //              // DW_CFA_val_expression where:
      //              // eip = DW_OP_breg6(+28), DW_OP_deref, DW_OP_dup,
      //              DW_OP_plus_uconst(0x34),
      //              //       DW_OP_deref, DW_OP_swap, DW_OP_plus_uconst(0),
      //              DW_OP_deref,
      //              //       DW_OP_dup, DW_OP_lit3, DW_OP_ne, DW_OP_swap,
      //              DW_OP_lit4, DW_OP_ne,
      //              //       DW_OP_and, DW_OP_plus
      //              // This basically does a:
      //              // eip = ucontenxt.mcontext32->gpr.eip;
      //              // if (ucontenxt.mcontext32->exc.trapno != 3 &&
      //              ucontenxt.mcontext32->exc.trapno != 4)
      //              //   eip++;
      //              //
      //              // For x86_64:
      //              // DW_CFA_val_expression where:
      //              // rip =  DW_OP_breg3(+48), DW_OP_deref, DW_OP_dup,
      //              DW_OP_plus_uconst(0x90), DW_OP_deref,
      //              //          DW_OP_swap, DW_OP_plus_uconst(0),
      //              DW_OP_deref_size(4), DW_OP_dup, DW_OP_lit3,
      //              //          DW_OP_ne, DW_OP_swap, DW_OP_lit4, DW_OP_ne,
      //              DW_OP_and, DW_OP_plus
      //              // This basically does a:
      //              // rip = ucontenxt.mcontext64->gpr.rip;
      //              // if (ucontenxt.mcontext64->exc.trapno != 3 &&
      //              ucontenxt.mcontext64->exc.trapno != 4)
      //              //   rip++;
      //              // The trap comparisons and increments are not needed as
      //              it hoses up the unwound PC which
      //              // is expected to point at least past the instruction that
      //              causes the fault/trap. So we
      //              // take it out by trimming the expression right at the
      //              first "DW_OP_swap" opcodes
      //              if (block_data != NULL && thread->GetPCRegNum(Thread::GCC)
      //              == reg_num)
      //              {
      //                  if (thread->Is64Bit())
      //                  {
      //                      if (block_len > 9 && block_data[8] == DW_OP_swap
      //                      && block_data[9] == DW_OP_plus_uconst)
      //                          block_len = 8;
      //                  }
      //                  else
      //                  {
      //                      if (block_len > 8 && block_data[7] == DW_OP_swap
      //                      && block_data[8] == DW_OP_plus_uconst)
      //                          block_len = 7;
      //                  }
      //              }
      //#endif
      reg_location.SetIsDWARFExpression(block_data, block_len);
      row.SetRegisterInfo(reg_num, reg_location);
      return true;
    }
    }
  }
  return false;
}

void DWARFCallFrameInfo::ForEachFDEEntries(
    const std::function<bool(lldb::addr_t, uint32_t, dw_offset_t)> &callback) {
  GetFDEIndex();

  for (size_t i = 0, c = m_fde_index.GetSize(); i < c; ++i) {
    const FDEEntryMap::Entry &entry = m_fde_index.GetEntryRef(i);
    if (!callback(entry.base, entry.size, entry.data))
      break;
  }
}
