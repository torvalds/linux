//===-- CompactUnwindInfo.cpp -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/CompactUnwindInfo.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/Section.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"
#include <algorithm>

#include "llvm/Support/MathExtras.h"

using namespace lldb;
using namespace lldb_private;

namespace lldb_private {

// Constants from <mach-o/compact_unwind_encoding.h>

FLAGS_ANONYMOUS_ENUM(){
    UNWIND_IS_NOT_FUNCTION_START = 0x80000000, UNWIND_HAS_LSDA = 0x40000000,
    UNWIND_PERSONALITY_MASK = 0x30000000,
};

FLAGS_ANONYMOUS_ENUM(){
    UNWIND_X86_MODE_MASK = 0x0F000000,
    UNWIND_X86_MODE_EBP_FRAME = 0x01000000,
    UNWIND_X86_MODE_STACK_IMMD = 0x02000000,
    UNWIND_X86_MODE_STACK_IND = 0x03000000,
    UNWIND_X86_MODE_DWARF = 0x04000000,

    UNWIND_X86_EBP_FRAME_REGISTERS = 0x00007FFF,
    UNWIND_X86_EBP_FRAME_OFFSET = 0x00FF0000,

    UNWIND_X86_FRAMELESS_STACK_SIZE = 0x00FF0000,
    UNWIND_X86_FRAMELESS_STACK_ADJUST = 0x0000E000,
    UNWIND_X86_FRAMELESS_STACK_REG_COUNT = 0x00001C00,
    UNWIND_X86_FRAMELESS_STACK_REG_PERMUTATION = 0x000003FF,

    UNWIND_X86_DWARF_SECTION_OFFSET = 0x00FFFFFF,
};

enum {
  UNWIND_X86_REG_NONE = 0,
  UNWIND_X86_REG_EBX = 1,
  UNWIND_X86_REG_ECX = 2,
  UNWIND_X86_REG_EDX = 3,
  UNWIND_X86_REG_EDI = 4,
  UNWIND_X86_REG_ESI = 5,
  UNWIND_X86_REG_EBP = 6,
};

FLAGS_ANONYMOUS_ENUM(){
    UNWIND_X86_64_MODE_MASK = 0x0F000000,
    UNWIND_X86_64_MODE_RBP_FRAME = 0x01000000,
    UNWIND_X86_64_MODE_STACK_IMMD = 0x02000000,
    UNWIND_X86_64_MODE_STACK_IND = 0x03000000,
    UNWIND_X86_64_MODE_DWARF = 0x04000000,

    UNWIND_X86_64_RBP_FRAME_REGISTERS = 0x00007FFF,
    UNWIND_X86_64_RBP_FRAME_OFFSET = 0x00FF0000,

    UNWIND_X86_64_FRAMELESS_STACK_SIZE = 0x00FF0000,
    UNWIND_X86_64_FRAMELESS_STACK_ADJUST = 0x0000E000,
    UNWIND_X86_64_FRAMELESS_STACK_REG_COUNT = 0x00001C00,
    UNWIND_X86_64_FRAMELESS_STACK_REG_PERMUTATION = 0x000003FF,

    UNWIND_X86_64_DWARF_SECTION_OFFSET = 0x00FFFFFF,
};

enum {
  UNWIND_X86_64_REG_NONE = 0,
  UNWIND_X86_64_REG_RBX = 1,
  UNWIND_X86_64_REG_R12 = 2,
  UNWIND_X86_64_REG_R13 = 3,
  UNWIND_X86_64_REG_R14 = 4,
  UNWIND_X86_64_REG_R15 = 5,
  UNWIND_X86_64_REG_RBP = 6,
};

FLAGS_ANONYMOUS_ENUM(){
    UNWIND_ARM64_MODE_MASK = 0x0F000000,
    UNWIND_ARM64_MODE_FRAMELESS = 0x02000000,
    UNWIND_ARM64_MODE_DWARF = 0x03000000,
    UNWIND_ARM64_MODE_FRAME = 0x04000000,

    UNWIND_ARM64_FRAME_X19_X20_PAIR = 0x00000001,
    UNWIND_ARM64_FRAME_X21_X22_PAIR = 0x00000002,
    UNWIND_ARM64_FRAME_X23_X24_PAIR = 0x00000004,
    UNWIND_ARM64_FRAME_X25_X26_PAIR = 0x00000008,
    UNWIND_ARM64_FRAME_X27_X28_PAIR = 0x00000010,
    UNWIND_ARM64_FRAME_D8_D9_PAIR = 0x00000100,
    UNWIND_ARM64_FRAME_D10_D11_PAIR = 0x00000200,
    UNWIND_ARM64_FRAME_D12_D13_PAIR = 0x00000400,
    UNWIND_ARM64_FRAME_D14_D15_PAIR = 0x00000800,

    UNWIND_ARM64_FRAMELESS_STACK_SIZE_MASK = 0x00FFF000,
    UNWIND_ARM64_DWARF_SECTION_OFFSET = 0x00FFFFFF,
};

FLAGS_ANONYMOUS_ENUM(){
    UNWIND_ARM_MODE_MASK = 0x0F000000,
    UNWIND_ARM_MODE_FRAME = 0x01000000,
    UNWIND_ARM_MODE_FRAME_D = 0x02000000,
    UNWIND_ARM_MODE_DWARF = 0x04000000,

    UNWIND_ARM_FRAME_STACK_ADJUST_MASK = 0x00C00000,

    UNWIND_ARM_FRAME_FIRST_PUSH_R4 = 0x00000001,
    UNWIND_ARM_FRAME_FIRST_PUSH_R5 = 0x00000002,
    UNWIND_ARM_FRAME_FIRST_PUSH_R6 = 0x00000004,

    UNWIND_ARM_FRAME_SECOND_PUSH_R8 = 0x00000008,
    UNWIND_ARM_FRAME_SECOND_PUSH_R9 = 0x00000010,
    UNWIND_ARM_FRAME_SECOND_PUSH_R10 = 0x00000020,
    UNWIND_ARM_FRAME_SECOND_PUSH_R11 = 0x00000040,
    UNWIND_ARM_FRAME_SECOND_PUSH_R12 = 0x00000080,

    UNWIND_ARM_FRAME_D_REG_COUNT_MASK = 0x00000700,

    UNWIND_ARM_DWARF_SECTION_OFFSET = 0x00FFFFFF,
};
}

#ifndef UNWIND_SECOND_LEVEL_REGULAR
#define UNWIND_SECOND_LEVEL_REGULAR 2
#endif

#ifndef UNWIND_SECOND_LEVEL_COMPRESSED
#define UNWIND_SECOND_LEVEL_COMPRESSED 3
#endif

#ifndef UNWIND_INFO_COMPRESSED_ENTRY_FUNC_OFFSET
#define UNWIND_INFO_COMPRESSED_ENTRY_FUNC_OFFSET(entry) (entry & 0x00FFFFFF)
#endif

#ifndef UNWIND_INFO_COMPRESSED_ENTRY_ENCODING_INDEX
#define UNWIND_INFO_COMPRESSED_ENTRY_ENCODING_INDEX(entry)                     \
  ((entry >> 24) & 0xFF)
#endif

#define EXTRACT_BITS(value, mask)                                              \
  ((value >>                                                                   \
    llvm::countTrailingZeros(static_cast<uint32_t>(mask), llvm::ZB_Width)) &   \
   (((1 << llvm::countPopulation(static_cast<uint32_t>(mask)))) - 1))

//----------------------
// constructor
//----------------------

CompactUnwindInfo::CompactUnwindInfo(ObjectFile &objfile, SectionSP &section_sp)
    : m_objfile(objfile), m_section_sp(section_sp),
      m_section_contents_if_encrypted(), m_mutex(), m_indexes(),
      m_indexes_computed(eLazyBoolCalculate), m_unwindinfo_data(),
      m_unwindinfo_data_computed(false), m_unwind_header() {}

//----------------------
// destructor
//----------------------

CompactUnwindInfo::~CompactUnwindInfo() {}

bool CompactUnwindInfo::GetUnwindPlan(Target &target, Address addr,
                                      UnwindPlan &unwind_plan) {
  if (!IsValid(target.GetProcessSP())) {
    return false;
  }
  FunctionInfo function_info;
  if (GetCompactUnwindInfoForFunction(target, addr, function_info)) {
    // shortcut return for functions that have no compact unwind
    if (function_info.encoding == 0)
      return false;

    if (ArchSpec arch = m_objfile.GetArchitecture()) {

      Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_UNWIND));
      if (log && log->GetVerbose()) {
        StreamString strm;
        addr.Dump(
            &strm, NULL,
            Address::DumpStyle::DumpStyleResolvedDescriptionNoFunctionArguments,
            Address::DumpStyle::DumpStyleFileAddress,
            arch.GetAddressByteSize());
        log->Printf("Got compact unwind encoding 0x%x for function %s",
                    function_info.encoding, strm.GetData());
      }

      if (function_info.valid_range_offset_start != 0 &&
          function_info.valid_range_offset_end != 0) {
        SectionList *sl = m_objfile.GetSectionList();
        if (sl) {
          addr_t func_range_start_file_addr =
              function_info.valid_range_offset_start +
              m_objfile.GetBaseAddress().GetFileAddress();
          AddressRange func_range(func_range_start_file_addr,
                                  function_info.valid_range_offset_end -
                                      function_info.valid_range_offset_start,
                                  sl);
          unwind_plan.SetPlanValidAddressRange(func_range);
        }
      }

      if (arch.GetTriple().getArch() == llvm::Triple::x86_64) {
        return CreateUnwindPlan_x86_64(target, function_info, unwind_plan,
                                       addr);
      }
      if (arch.GetTriple().getArch() == llvm::Triple::aarch64) {
        return CreateUnwindPlan_arm64(target, function_info, unwind_plan, addr);
      }
      if (arch.GetTriple().getArch() == llvm::Triple::x86) {
        return CreateUnwindPlan_i386(target, function_info, unwind_plan, addr);
      }
      if (arch.GetTriple().getArch() == llvm::Triple::arm ||
          arch.GetTriple().getArch() == llvm::Triple::thumb) {
        return CreateUnwindPlan_armv7(target, function_info, unwind_plan, addr);
      }
    }
  }
  return false;
}

bool CompactUnwindInfo::IsValid(const ProcessSP &process_sp) {
  if (m_section_sp.get() == nullptr)
    return false;

  if (m_indexes_computed == eLazyBoolYes && m_unwindinfo_data_computed)
    return true;

  ScanIndex(process_sp);

  return m_indexes_computed == eLazyBoolYes && m_unwindinfo_data_computed;
}

void CompactUnwindInfo::ScanIndex(const ProcessSP &process_sp) {
  std::lock_guard<std::mutex> guard(m_mutex);
  if (m_indexes_computed == eLazyBoolYes && m_unwindinfo_data_computed)
    return;

  // We can't read the index for some reason.
  if (m_indexes_computed == eLazyBoolNo) {
    return;
  }

  Log *log(GetLogIfAllCategoriesSet(LIBLLDB_LOG_UNWIND));
  if (log)
    m_objfile.GetModule()->LogMessage(
        log, "Reading compact unwind first-level indexes");

  if (!m_unwindinfo_data_computed) {
    if (m_section_sp->IsEncrypted()) {
      // Can't get section contents of a protected/encrypted section until we
      // have a live process and can read them out of memory.
      if (process_sp.get() == nullptr)
        return;
      m_section_contents_if_encrypted.reset(
          new DataBufferHeap(m_section_sp->GetByteSize(), 0));
      Status error;
      if (process_sp->ReadMemory(
              m_section_sp->GetLoadBaseAddress(&process_sp->GetTarget()),
              m_section_contents_if_encrypted->GetBytes(),
              m_section_sp->GetByteSize(),
              error) == m_section_sp->GetByteSize() &&
          error.Success()) {
        m_unwindinfo_data.SetAddressByteSize(
            process_sp->GetTarget().GetArchitecture().GetAddressByteSize());
        m_unwindinfo_data.SetByteOrder(
            process_sp->GetTarget().GetArchitecture().GetByteOrder());
        m_unwindinfo_data.SetData(m_section_contents_if_encrypted, 0);
      }
    } else {
      m_objfile.ReadSectionData(m_section_sp.get(), m_unwindinfo_data);
    }
    if (m_unwindinfo_data.GetByteSize() != m_section_sp->GetByteSize())
      return;
    m_unwindinfo_data_computed = true;
  }

  if (m_unwindinfo_data.GetByteSize() > 0) {
    offset_t offset = 0;

    // struct unwind_info_section_header
    // {
    // uint32_t    version;            // UNWIND_SECTION_VERSION
    // uint32_t    commonEncodingsArraySectionOffset;
    // uint32_t    commonEncodingsArrayCount;
    // uint32_t    personalityArraySectionOffset;
    // uint32_t    personalityArrayCount;
    // uint32_t    indexSectionOffset;
    // uint32_t    indexCount;

    m_unwind_header.version = m_unwindinfo_data.GetU32(&offset);
    m_unwind_header.common_encodings_array_offset =
        m_unwindinfo_data.GetU32(&offset);
    m_unwind_header.common_encodings_array_count =
        m_unwindinfo_data.GetU32(&offset);
    m_unwind_header.personality_array_offset =
        m_unwindinfo_data.GetU32(&offset);
    m_unwind_header.personality_array_count = m_unwindinfo_data.GetU32(&offset);
    uint32_t indexSectionOffset = m_unwindinfo_data.GetU32(&offset);

    uint32_t indexCount = m_unwindinfo_data.GetU32(&offset);

    if (m_unwind_header.common_encodings_array_offset >
            m_unwindinfo_data.GetByteSize() ||
        m_unwind_header.personality_array_offset >
            m_unwindinfo_data.GetByteSize() ||
        indexSectionOffset > m_unwindinfo_data.GetByteSize() ||
        offset > m_unwindinfo_data.GetByteSize()) {
      Host::SystemLog(Host::eSystemLogError, "error: Invalid offset "
                                             "encountered in compact unwind "
                                             "info, skipping\n");
      // don't trust anything from this compact_unwind section if it looks
      // blatantly invalid data in the header.
      m_indexes_computed = eLazyBoolNo;
      return;
    }

    // Parse the basic information from the indexes We wait to scan the second
    // level page info until it's needed

    // struct unwind_info_section_header_index_entry {
    //     uint32_t        functionOffset;
    //     uint32_t        secondLevelPagesSectionOffset;
    //     uint32_t        lsdaIndexArraySectionOffset;
    // };

    bool clear_address_zeroth_bit = false;
    if (ArchSpec arch = m_objfile.GetArchitecture()) {
      if (arch.GetTriple().getArch() == llvm::Triple::arm ||
          arch.GetTriple().getArch() == llvm::Triple::thumb)
        clear_address_zeroth_bit = true;
    }

    offset = indexSectionOffset;
    for (uint32_t idx = 0; idx < indexCount; idx++) {
      uint32_t function_offset =
          m_unwindinfo_data.GetU32(&offset); // functionOffset
      uint32_t second_level_offset =
          m_unwindinfo_data.GetU32(&offset); // secondLevelPagesSectionOffset
      uint32_t lsda_offset =
          m_unwindinfo_data.GetU32(&offset); // lsdaIndexArraySectionOffset

      if (second_level_offset > m_section_sp->GetByteSize() ||
          lsda_offset > m_section_sp->GetByteSize()) {
        m_indexes_computed = eLazyBoolNo;
      }

      if (clear_address_zeroth_bit)
        function_offset &= ~1ull;

      UnwindIndex this_index;
      this_index.function_offset = function_offset;
      this_index.second_level = second_level_offset;
      this_index.lsda_array_start = lsda_offset;

      if (m_indexes.size() > 0) {
        m_indexes[m_indexes.size() - 1].lsda_array_end = lsda_offset;
      }

      if (second_level_offset == 0) {
        this_index.sentinal_entry = true;
      }

      m_indexes.push_back(this_index);
    }
    m_indexes_computed = eLazyBoolYes;
  } else {
    m_indexes_computed = eLazyBoolNo;
  }
}

uint32_t CompactUnwindInfo::GetLSDAForFunctionOffset(uint32_t lsda_offset,
                                                     uint32_t lsda_count,
                                                     uint32_t function_offset) {
  // struct unwind_info_section_header_lsda_index_entry {
  //         uint32_t        functionOffset;
  //         uint32_t        lsdaOffset;
  // };

  offset_t first_entry = lsda_offset;
  uint32_t low = 0;
  uint32_t high = lsda_count;
  while (low < high) {
    uint32_t mid = (low + high) / 2;
    offset_t offset = first_entry + (mid * 8);
    uint32_t mid_func_offset =
        m_unwindinfo_data.GetU32(&offset); // functionOffset
    uint32_t mid_lsda_offset = m_unwindinfo_data.GetU32(&offset); // lsdaOffset
    if (mid_func_offset == function_offset) {
      return mid_lsda_offset;
    }
    if (mid_func_offset < function_offset) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }
  return 0;
}

lldb::offset_t CompactUnwindInfo::BinarySearchRegularSecondPage(
    uint32_t entry_page_offset, uint32_t entry_count, uint32_t function_offset,
    uint32_t *entry_func_start_offset, uint32_t *entry_func_end_offset) {
  // typedef uint32_t compact_unwind_encoding_t;
  // struct unwind_info_regular_second_level_entry {
  //     uint32_t                    functionOffset;
  //     compact_unwind_encoding_t    encoding;

  offset_t first_entry = entry_page_offset;

  uint32_t low = 0;
  uint32_t high = entry_count;
  uint32_t last = high - 1;
  while (low < high) {
    uint32_t mid = (low + high) / 2;
    offset_t offset = first_entry + (mid * 8);
    uint32_t mid_func_offset =
        m_unwindinfo_data.GetU32(&offset); // functionOffset
    uint32_t next_func_offset = 0;
    if (mid < last) {
      offset = first_entry + ((mid + 1) * 8);
      next_func_offset = m_unwindinfo_data.GetU32(&offset); // functionOffset
    }
    if (mid_func_offset <= function_offset) {
      if (mid == last || (next_func_offset > function_offset)) {
        if (entry_func_start_offset)
          *entry_func_start_offset = mid_func_offset;
        if (mid != last && entry_func_end_offset)
          *entry_func_end_offset = next_func_offset;
        return first_entry + (mid * 8);
      } else {
        low = mid + 1;
      }
    } else {
      high = mid;
    }
  }
  return LLDB_INVALID_OFFSET;
}

uint32_t CompactUnwindInfo::BinarySearchCompressedSecondPage(
    uint32_t entry_page_offset, uint32_t entry_count,
    uint32_t function_offset_to_find, uint32_t function_offset_base,
    uint32_t *entry_func_start_offset, uint32_t *entry_func_end_offset) {
  offset_t first_entry = entry_page_offset;

  uint32_t low = 0;
  uint32_t high = entry_count;
  uint32_t last = high - 1;
  while (low < high) {
    uint32_t mid = (low + high) / 2;
    offset_t offset = first_entry + (mid * 4);
    uint32_t entry = m_unwindinfo_data.GetU32(&offset); // entry
    uint32_t mid_func_offset = UNWIND_INFO_COMPRESSED_ENTRY_FUNC_OFFSET(entry);
    mid_func_offset += function_offset_base;
    uint32_t next_func_offset = 0;
    if (mid < last) {
      offset = first_entry + ((mid + 1) * 4);
      uint32_t next_entry = m_unwindinfo_data.GetU32(&offset); // entry
      next_func_offset = UNWIND_INFO_COMPRESSED_ENTRY_FUNC_OFFSET(next_entry);
      next_func_offset += function_offset_base;
    }
    if (mid_func_offset <= function_offset_to_find) {
      if (mid == last || (next_func_offset > function_offset_to_find)) {
        if (entry_func_start_offset)
          *entry_func_start_offset = mid_func_offset;
        if (mid != last && entry_func_end_offset)
          *entry_func_end_offset = next_func_offset;
        return UNWIND_INFO_COMPRESSED_ENTRY_ENCODING_INDEX(entry);
      } else {
        low = mid + 1;
      }
    } else {
      high = mid;
    }
  }

  return UINT32_MAX;
}

bool CompactUnwindInfo::GetCompactUnwindInfoForFunction(
    Target &target, Address address, FunctionInfo &unwind_info) {
  unwind_info.encoding = 0;
  unwind_info.lsda_address.Clear();
  unwind_info.personality_ptr_address.Clear();

  if (!IsValid(target.GetProcessSP()))
    return false;

  addr_t text_section_file_address = LLDB_INVALID_ADDRESS;
  SectionList *sl = m_objfile.GetSectionList();
  if (sl) {
    SectionSP text_sect = sl->FindSectionByType(eSectionTypeCode, true);
    if (text_sect.get()) {
      text_section_file_address = text_sect->GetFileAddress();
    }
  }
  if (text_section_file_address == LLDB_INVALID_ADDRESS)
    return false;

  addr_t function_offset =
      address.GetFileAddress() - m_objfile.GetBaseAddress().GetFileAddress();

  UnwindIndex key;
  key.function_offset = function_offset;

  std::vector<UnwindIndex>::const_iterator it;
  it = std::lower_bound(m_indexes.begin(), m_indexes.end(), key);
  if (it == m_indexes.end()) {
    return false;
  }

  if (it->function_offset != key.function_offset) {
    if (it != m_indexes.begin())
      --it;
  }

  if (it->sentinal_entry) {
    return false;
  }

  auto next_it = it + 1;
  if (next_it != m_indexes.end()) {
    // initialize the function offset end range to be the start of the next
    // index offset.  If we find an entry which is at the end of the index
    // table, this will establish the range end.
    unwind_info.valid_range_offset_end = next_it->function_offset;
  }

  offset_t second_page_offset = it->second_level;
  offset_t lsda_array_start = it->lsda_array_start;
  offset_t lsda_array_count = (it->lsda_array_end - it->lsda_array_start) / 8;

  offset_t offset = second_page_offset;
  uint32_t kind = m_unwindinfo_data.GetU32(
      &offset); // UNWIND_SECOND_LEVEL_REGULAR or UNWIND_SECOND_LEVEL_COMPRESSED

  if (kind == UNWIND_SECOND_LEVEL_REGULAR) {
    // struct unwind_info_regular_second_level_page_header {
    //     uint32_t    kind;    // UNWIND_SECOND_LEVEL_REGULAR
    //     uint16_t    entryPageOffset;
    //     uint16_t    entryCount;

    // typedef uint32_t compact_unwind_encoding_t;
    // struct unwind_info_regular_second_level_entry {
    //     uint32_t                    functionOffset;
    //     compact_unwind_encoding_t    encoding;

    uint16_t entry_page_offset =
        m_unwindinfo_data.GetU16(&offset);                    // entryPageOffset
    uint16_t entry_count = m_unwindinfo_data.GetU16(&offset); // entryCount

    offset_t entry_offset = BinarySearchRegularSecondPage(
        second_page_offset + entry_page_offset, entry_count, function_offset,
        &unwind_info.valid_range_offset_start,
        &unwind_info.valid_range_offset_end);
    if (entry_offset == LLDB_INVALID_OFFSET) {
      return false;
    }
    entry_offset += 4; // skip over functionOffset
    unwind_info.encoding = m_unwindinfo_data.GetU32(&entry_offset); // encoding
    if (unwind_info.encoding & UNWIND_HAS_LSDA) {
      SectionList *sl = m_objfile.GetSectionList();
      if (sl) {
        uint32_t lsda_offset = GetLSDAForFunctionOffset(
            lsda_array_start, lsda_array_count, function_offset);
        addr_t objfile_base_address =
            m_objfile.GetBaseAddress().GetFileAddress();
        unwind_info.lsda_address.ResolveAddressUsingFileSections(
            objfile_base_address + lsda_offset, sl);
      }
    }
    if (unwind_info.encoding & UNWIND_PERSONALITY_MASK) {
      uint32_t personality_index =
          EXTRACT_BITS(unwind_info.encoding, UNWIND_PERSONALITY_MASK);

      if (personality_index > 0) {
        personality_index--;
        if (personality_index < m_unwind_header.personality_array_count) {
          offset_t offset = m_unwind_header.personality_array_offset;
          offset += 4 * personality_index;
          SectionList *sl = m_objfile.GetSectionList();
          if (sl) {
            uint32_t personality_offset = m_unwindinfo_data.GetU32(&offset);
            addr_t objfile_base_address =
                m_objfile.GetBaseAddress().GetFileAddress();
            unwind_info.personality_ptr_address.ResolveAddressUsingFileSections(
                objfile_base_address + personality_offset, sl);
          }
        }
      }
    }
    return true;
  } else if (kind == UNWIND_SECOND_LEVEL_COMPRESSED) {
    // struct unwind_info_compressed_second_level_page_header {
    //     uint32_t    kind;    // UNWIND_SECOND_LEVEL_COMPRESSED
    //     uint16_t    entryPageOffset;         // offset from this 2nd lvl page
    //     idx to array of entries
    //                                          // (an entry has a function
    //                                          offset and index into the
    //                                          encodings)
    //                                          // NB function offset from the
    //                                          entry in the compressed page
    //                                          // must be added to the index's
    //                                          functionOffset value.
    //     uint16_t    entryCount;
    //     uint16_t    encodingsPageOffset;     // offset from this 2nd lvl page
    //     idx to array of encodings
    //     uint16_t    encodingsCount;

    uint16_t entry_page_offset =
        m_unwindinfo_data.GetU16(&offset);                    // entryPageOffset
    uint16_t entry_count = m_unwindinfo_data.GetU16(&offset); // entryCount
    uint16_t encodings_page_offset =
        m_unwindinfo_data.GetU16(&offset); // encodingsPageOffset
    uint16_t encodings_count =
        m_unwindinfo_data.GetU16(&offset); // encodingsCount

    uint32_t encoding_index = BinarySearchCompressedSecondPage(
        second_page_offset + entry_page_offset, entry_count, function_offset,
        it->function_offset, &unwind_info.valid_range_offset_start,
        &unwind_info.valid_range_offset_end);
    if (encoding_index == UINT32_MAX ||
        encoding_index >=
            encodings_count + m_unwind_header.common_encodings_array_count) {
      return false;
    }
    uint32_t encoding = 0;
    if (encoding_index < m_unwind_header.common_encodings_array_count) {
      offset = m_unwind_header.common_encodings_array_offset +
               (encoding_index * sizeof(uint32_t));
      encoding = m_unwindinfo_data.GetU32(
          &offset); // encoding entry from the commonEncodingsArray
    } else {
      uint32_t page_specific_entry_index =
          encoding_index - m_unwind_header.common_encodings_array_count;
      offset = second_page_offset + encodings_page_offset +
               (page_specific_entry_index * sizeof(uint32_t));
      encoding = m_unwindinfo_data.GetU32(
          &offset); // encoding entry from the page-specific encoding array
    }
    if (encoding == 0)
      return false;

    unwind_info.encoding = encoding;
    if (unwind_info.encoding & UNWIND_HAS_LSDA) {
      SectionList *sl = m_objfile.GetSectionList();
      if (sl) {
        uint32_t lsda_offset = GetLSDAForFunctionOffset(
            lsda_array_start, lsda_array_count, function_offset);
        addr_t objfile_base_address =
            m_objfile.GetBaseAddress().GetFileAddress();
        unwind_info.lsda_address.ResolveAddressUsingFileSections(
            objfile_base_address + lsda_offset, sl);
      }
    }
    if (unwind_info.encoding & UNWIND_PERSONALITY_MASK) {
      uint32_t personality_index =
          EXTRACT_BITS(unwind_info.encoding, UNWIND_PERSONALITY_MASK);

      if (personality_index > 0) {
        personality_index--;
        if (personality_index < m_unwind_header.personality_array_count) {
          offset_t offset = m_unwind_header.personality_array_offset;
          offset += 4 * personality_index;
          SectionList *sl = m_objfile.GetSectionList();
          if (sl) {
            uint32_t personality_offset = m_unwindinfo_data.GetU32(&offset);
            addr_t objfile_base_address =
                m_objfile.GetBaseAddress().GetFileAddress();
            unwind_info.personality_ptr_address.ResolveAddressUsingFileSections(
                objfile_base_address + personality_offset, sl);
          }
        }
      }
    }
    return true;
  }
  return false;
}

enum x86_64_eh_regnum {
  rax = 0,
  rdx = 1,
  rcx = 2,
  rbx = 3,
  rsi = 4,
  rdi = 5,
  rbp = 6,
  rsp = 7,
  r8 = 8,
  r9 = 9,
  r10 = 10,
  r11 = 11,
  r12 = 12,
  r13 = 13,
  r14 = 14,
  r15 = 15,
  rip = 16 // this is officially the Return Address register number, but close
           // enough
};

// Convert the compact_unwind_info.h register numbering scheme to
// eRegisterKindEHFrame (eh_frame) register numbering scheme.
uint32_t translate_to_eh_frame_regnum_x86_64(uint32_t unwind_regno) {
  switch (unwind_regno) {
  case UNWIND_X86_64_REG_RBX:
    return x86_64_eh_regnum::rbx;
  case UNWIND_X86_64_REG_R12:
    return x86_64_eh_regnum::r12;
  case UNWIND_X86_64_REG_R13:
    return x86_64_eh_regnum::r13;
  case UNWIND_X86_64_REG_R14:
    return x86_64_eh_regnum::r14;
  case UNWIND_X86_64_REG_R15:
    return x86_64_eh_regnum::r15;
  case UNWIND_X86_64_REG_RBP:
    return x86_64_eh_regnum::rbp;
  default:
    return LLDB_INVALID_REGNUM;
  }
}

bool CompactUnwindInfo::CreateUnwindPlan_x86_64(Target &target,
                                                FunctionInfo &function_info,
                                                UnwindPlan &unwind_plan,
                                                Address pc_or_function_start) {
  unwind_plan.SetSourceName("compact unwind info");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolYes);
  unwind_plan.SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);
  unwind_plan.SetRegisterKind(eRegisterKindEHFrame);

  unwind_plan.SetLSDAAddress(function_info.lsda_address);
  unwind_plan.SetPersonalityFunctionPtr(function_info.personality_ptr_address);

  UnwindPlan::RowSP row(new UnwindPlan::Row);

  const int wordsize = 8;
  int mode = function_info.encoding & UNWIND_X86_64_MODE_MASK;
  switch (mode) {
  case UNWIND_X86_64_MODE_RBP_FRAME: {
    row->GetCFAValue().SetIsRegisterPlusOffset(
        translate_to_eh_frame_regnum_x86_64(UNWIND_X86_64_REG_RBP),
        2 * wordsize);
    row->SetOffset(0);
    row->SetRegisterLocationToAtCFAPlusOffset(x86_64_eh_regnum::rbp,
                                              wordsize * -2, true);
    row->SetRegisterLocationToAtCFAPlusOffset(x86_64_eh_regnum::rip,
                                              wordsize * -1, true);
    row->SetRegisterLocationToIsCFAPlusOffset(x86_64_eh_regnum::rsp, 0, true);

    uint32_t saved_registers_offset =
        EXTRACT_BITS(function_info.encoding, UNWIND_X86_64_RBP_FRAME_OFFSET);

    uint32_t saved_registers_locations =
        EXTRACT_BITS(function_info.encoding, UNWIND_X86_64_RBP_FRAME_REGISTERS);

    saved_registers_offset += 2;

    for (int i = 0; i < 5; i++) {
      uint32_t regnum = saved_registers_locations & 0x7;
      switch (regnum) {
      case UNWIND_X86_64_REG_NONE:
        break;
      case UNWIND_X86_64_REG_RBX:
      case UNWIND_X86_64_REG_R12:
      case UNWIND_X86_64_REG_R13:
      case UNWIND_X86_64_REG_R14:
      case UNWIND_X86_64_REG_R15:
        row->SetRegisterLocationToAtCFAPlusOffset(
            translate_to_eh_frame_regnum_x86_64(regnum),
            wordsize * -saved_registers_offset, true);
        break;
      }
      saved_registers_offset--;
      saved_registers_locations >>= 3;
    }
    unwind_plan.AppendRow(row);
    return true;
  } break;

  case UNWIND_X86_64_MODE_STACK_IND: {
    // The clang in Xcode 6 is emitting incorrect compact unwind encodings for
    // this style of unwind.  It was fixed in llvm r217020. The clang in Xcode
    // 7 has this fixed.
    return false;
  } break;

  case UNWIND_X86_64_MODE_STACK_IMMD: {
    uint32_t stack_size = EXTRACT_BITS(function_info.encoding,
                                       UNWIND_X86_64_FRAMELESS_STACK_SIZE);
    uint32_t register_count = EXTRACT_BITS(
        function_info.encoding, UNWIND_X86_64_FRAMELESS_STACK_REG_COUNT);
    uint32_t permutation = EXTRACT_BITS(
        function_info.encoding, UNWIND_X86_64_FRAMELESS_STACK_REG_PERMUTATION);

    if (mode == UNWIND_X86_64_MODE_STACK_IND &&
        function_info.valid_range_offset_start != 0) {
      uint32_t stack_adjust = EXTRACT_BITS(
          function_info.encoding, UNWIND_X86_64_FRAMELESS_STACK_ADJUST);

      // offset into the function instructions; 0 == beginning of first
      // instruction
      uint32_t offset_to_subl_insn = EXTRACT_BITS(
          function_info.encoding, UNWIND_X86_64_FRAMELESS_STACK_SIZE);

      SectionList *sl = m_objfile.GetSectionList();
      if (sl) {
        ProcessSP process_sp = target.GetProcessSP();
        if (process_sp) {
          Address subl_payload_addr(function_info.valid_range_offset_start, sl);
          subl_payload_addr.Slide(offset_to_subl_insn);
          Status error;
          uint64_t large_stack_size = process_sp->ReadUnsignedIntegerFromMemory(
              subl_payload_addr.GetLoadAddress(&target), 4, 0, error);
          if (large_stack_size != 0 && error.Success()) {
            // Got the large stack frame size correctly - use it
            stack_size = large_stack_size + (stack_adjust * wordsize);
          } else {
            return false;
          }
        } else {
          return false;
        }
      } else {
        return false;
      }
    }

    int32_t offset = mode == UNWIND_X86_64_MODE_STACK_IND
                         ? stack_size
                         : stack_size * wordsize;
    row->GetCFAValue().SetIsRegisterPlusOffset(x86_64_eh_regnum::rsp, offset);

    row->SetOffset(0);
    row->SetRegisterLocationToAtCFAPlusOffset(x86_64_eh_regnum::rip,
                                              wordsize * -1, true);
    row->SetRegisterLocationToIsCFAPlusOffset(x86_64_eh_regnum::rsp, 0, true);

    if (register_count > 0) {

      // We need to include (up to) 6 registers in 10 bits. That would be 18
      // bits if we just used 3 bits per reg to indicate the order they're
      // saved on the stack.
      //
      // This is done with Lehmer code permutation, e.g. see
      // http://stackoverflow.com/questions/1506078/fast-permutation-number-
      // permutation-mapping-algorithms
      int permunreg[6] = {0, 0, 0, 0, 0, 0};

      // This decodes the variable-base number in the 10 bits and gives us the
      // Lehmer code sequence which can then be decoded.

      switch (register_count) {
      case 6:
        permunreg[0] = permutation / 120; // 120 == 5!
        permutation -= (permunreg[0] * 120);
        permunreg[1] = permutation / 24; // 24 == 4!
        permutation -= (permunreg[1] * 24);
        permunreg[2] = permutation / 6; // 6 == 3!
        permutation -= (permunreg[2] * 6);
        permunreg[3] = permutation / 2; // 2 == 2!
        permutation -= (permunreg[3] * 2);
        permunreg[4] = permutation; // 1 == 1!
        permunreg[5] = 0;
        break;
      case 5:
        permunreg[0] = permutation / 120;
        permutation -= (permunreg[0] * 120);
        permunreg[1] = permutation / 24;
        permutation -= (permunreg[1] * 24);
        permunreg[2] = permutation / 6;
        permutation -= (permunreg[2] * 6);
        permunreg[3] = permutation / 2;
        permutation -= (permunreg[3] * 2);
        permunreg[4] = permutation;
        break;
      case 4:
        permunreg[0] = permutation / 60;
        permutation -= (permunreg[0] * 60);
        permunreg[1] = permutation / 12;
        permutation -= (permunreg[1] * 12);
        permunreg[2] = permutation / 3;
        permutation -= (permunreg[2] * 3);
        permunreg[3] = permutation;
        break;
      case 3:
        permunreg[0] = permutation / 20;
        permutation -= (permunreg[0] * 20);
        permunreg[1] = permutation / 4;
        permutation -= (permunreg[1] * 4);
        permunreg[2] = permutation;
        break;
      case 2:
        permunreg[0] = permutation / 5;
        permutation -= (permunreg[0] * 5);
        permunreg[1] = permutation;
        break;
      case 1:
        permunreg[0] = permutation;
        break;
      }

      // Decode the Lehmer code for this permutation of the registers v.
      // http://en.wikipedia.org/wiki/Lehmer_code

      int registers[6] = {UNWIND_X86_64_REG_NONE, UNWIND_X86_64_REG_NONE,
                          UNWIND_X86_64_REG_NONE, UNWIND_X86_64_REG_NONE,
                          UNWIND_X86_64_REG_NONE, UNWIND_X86_64_REG_NONE};
      bool used[7] = {false, false, false, false, false, false, false};
      for (uint32_t i = 0; i < register_count; i++) {
        int renum = 0;
        for (int j = 1; j < 7; j++) {
          if (!used[j]) {
            if (renum == permunreg[i]) {
              registers[i] = j;
              used[j] = true;
              break;
            }
            renum++;
          }
        }
      }

      uint32_t saved_registers_offset = 1;
      saved_registers_offset++;

      for (int i = (sizeof(registers) / sizeof(int)) - 1; i >= 0; i--) {
        switch (registers[i]) {
        case UNWIND_X86_64_REG_NONE:
          break;
        case UNWIND_X86_64_REG_RBX:
        case UNWIND_X86_64_REG_R12:
        case UNWIND_X86_64_REG_R13:
        case UNWIND_X86_64_REG_R14:
        case UNWIND_X86_64_REG_R15:
        case UNWIND_X86_64_REG_RBP:
          row->SetRegisterLocationToAtCFAPlusOffset(
              translate_to_eh_frame_regnum_x86_64(registers[i]),
              wordsize * -saved_registers_offset, true);
          saved_registers_offset++;
          break;
        }
      }
    }
    unwind_plan.AppendRow(row);
    return true;
  } break;

  case UNWIND_X86_64_MODE_DWARF: {
    return false;
  } break;

  case 0: {
    return false;
  } break;
  }
  return false;
}

enum i386_eh_regnum {
  eax = 0,
  ecx = 1,
  edx = 2,
  ebx = 3,
  ebp = 4,
  esp = 5,
  esi = 6,
  edi = 7,
  eip = 8 // this is officially the Return Address register number, but close
          // enough
};

// Convert the compact_unwind_info.h register numbering scheme to
// eRegisterKindEHFrame (eh_frame) register numbering scheme.
uint32_t translate_to_eh_frame_regnum_i386(uint32_t unwind_regno) {
  switch (unwind_regno) {
  case UNWIND_X86_REG_EBX:
    return i386_eh_regnum::ebx;
  case UNWIND_X86_REG_ECX:
    return i386_eh_regnum::ecx;
  case UNWIND_X86_REG_EDX:
    return i386_eh_regnum::edx;
  case UNWIND_X86_REG_EDI:
    return i386_eh_regnum::edi;
  case UNWIND_X86_REG_ESI:
    return i386_eh_regnum::esi;
  case UNWIND_X86_REG_EBP:
    return i386_eh_regnum::ebp;
  default:
    return LLDB_INVALID_REGNUM;
  }
}

bool CompactUnwindInfo::CreateUnwindPlan_i386(Target &target,
                                              FunctionInfo &function_info,
                                              UnwindPlan &unwind_plan,
                                              Address pc_or_function_start) {
  unwind_plan.SetSourceName("compact unwind info");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolYes);
  unwind_plan.SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);
  unwind_plan.SetRegisterKind(eRegisterKindEHFrame);

  unwind_plan.SetLSDAAddress(function_info.lsda_address);
  unwind_plan.SetPersonalityFunctionPtr(function_info.personality_ptr_address);

  UnwindPlan::RowSP row(new UnwindPlan::Row);

  const int wordsize = 4;
  int mode = function_info.encoding & UNWIND_X86_MODE_MASK;
  switch (mode) {
  case UNWIND_X86_MODE_EBP_FRAME: {
    row->GetCFAValue().SetIsRegisterPlusOffset(
        translate_to_eh_frame_regnum_i386(UNWIND_X86_REG_EBP), 2 * wordsize);
    row->SetOffset(0);
    row->SetRegisterLocationToAtCFAPlusOffset(i386_eh_regnum::ebp,
                                              wordsize * -2, true);
    row->SetRegisterLocationToAtCFAPlusOffset(i386_eh_regnum::eip,
                                              wordsize * -1, true);
    row->SetRegisterLocationToIsCFAPlusOffset(i386_eh_regnum::esp, 0, true);

    uint32_t saved_registers_offset =
        EXTRACT_BITS(function_info.encoding, UNWIND_X86_EBP_FRAME_OFFSET);

    uint32_t saved_registers_locations =
        EXTRACT_BITS(function_info.encoding, UNWIND_X86_EBP_FRAME_REGISTERS);

    saved_registers_offset += 2;

    for (int i = 0; i < 5; i++) {
      uint32_t regnum = saved_registers_locations & 0x7;
      switch (regnum) {
      case UNWIND_X86_REG_NONE:
        break;
      case UNWIND_X86_REG_EBX:
      case UNWIND_X86_REG_ECX:
      case UNWIND_X86_REG_EDX:
      case UNWIND_X86_REG_EDI:
      case UNWIND_X86_REG_ESI:
        row->SetRegisterLocationToAtCFAPlusOffset(
            translate_to_eh_frame_regnum_i386(regnum),
            wordsize * -saved_registers_offset, true);
        break;
      }
      saved_registers_offset--;
      saved_registers_locations >>= 3;
    }
    unwind_plan.AppendRow(row);
    return true;
  } break;

  case UNWIND_X86_MODE_STACK_IND:
  case UNWIND_X86_MODE_STACK_IMMD: {
    uint32_t stack_size =
        EXTRACT_BITS(function_info.encoding, UNWIND_X86_FRAMELESS_STACK_SIZE);
    uint32_t register_count = EXTRACT_BITS(
        function_info.encoding, UNWIND_X86_FRAMELESS_STACK_REG_COUNT);
    uint32_t permutation = EXTRACT_BITS(
        function_info.encoding, UNWIND_X86_FRAMELESS_STACK_REG_PERMUTATION);

    if (mode == UNWIND_X86_MODE_STACK_IND &&
        function_info.valid_range_offset_start != 0) {
      uint32_t stack_adjust = EXTRACT_BITS(function_info.encoding,
                                           UNWIND_X86_FRAMELESS_STACK_ADJUST);

      // offset into the function instructions; 0 == beginning of first
      // instruction
      uint32_t offset_to_subl_insn =
          EXTRACT_BITS(function_info.encoding, UNWIND_X86_FRAMELESS_STACK_SIZE);

      SectionList *sl = m_objfile.GetSectionList();
      if (sl) {
        ProcessSP process_sp = target.GetProcessSP();
        if (process_sp) {
          Address subl_payload_addr(function_info.valid_range_offset_start, sl);
          subl_payload_addr.Slide(offset_to_subl_insn);
          Status error;
          uint64_t large_stack_size = process_sp->ReadUnsignedIntegerFromMemory(
              subl_payload_addr.GetLoadAddress(&target), 4, 0, error);
          if (large_stack_size != 0 && error.Success()) {
            // Got the large stack frame size correctly - use it
            stack_size = large_stack_size + (stack_adjust * wordsize);
          } else {
            return false;
          }
        } else {
          return false;
        }
      } else {
        return false;
      }
    }

    int32_t offset =
        mode == UNWIND_X86_MODE_STACK_IND ? stack_size : stack_size * wordsize;
    row->GetCFAValue().SetIsRegisterPlusOffset(i386_eh_regnum::esp, offset);
    row->SetOffset(0);
    row->SetRegisterLocationToAtCFAPlusOffset(i386_eh_regnum::eip,
                                              wordsize * -1, true);
    row->SetRegisterLocationToIsCFAPlusOffset(i386_eh_regnum::esp, 0, true);

    if (register_count > 0) {

      // We need to include (up to) 6 registers in 10 bits. That would be 18
      // bits if we just used 3 bits per reg to indicate the order they're
      // saved on the stack.
      //
      // This is done with Lehmer code permutation, e.g. see
      // http://stackoverflow.com/questions/1506078/fast-permutation-number-
      // permutation-mapping-algorithms
      int permunreg[6] = {0, 0, 0, 0, 0, 0};

      // This decodes the variable-base number in the 10 bits and gives us the
      // Lehmer code sequence which can then be decoded.

      switch (register_count) {
      case 6:
        permunreg[0] = permutation / 120; // 120 == 5!
        permutation -= (permunreg[0] * 120);
        permunreg[1] = permutation / 24; // 24 == 4!
        permutation -= (permunreg[1] * 24);
        permunreg[2] = permutation / 6; // 6 == 3!
        permutation -= (permunreg[2] * 6);
        permunreg[3] = permutation / 2; // 2 == 2!
        permutation -= (permunreg[3] * 2);
        permunreg[4] = permutation; // 1 == 1!
        permunreg[5] = 0;
        break;
      case 5:
        permunreg[0] = permutation / 120;
        permutation -= (permunreg[0] * 120);
        permunreg[1] = permutation / 24;
        permutation -= (permunreg[1] * 24);
        permunreg[2] = permutation / 6;
        permutation -= (permunreg[2] * 6);
        permunreg[3] = permutation / 2;
        permutation -= (permunreg[3] * 2);
        permunreg[4] = permutation;
        break;
      case 4:
        permunreg[0] = permutation / 60;
        permutation -= (permunreg[0] * 60);
        permunreg[1] = permutation / 12;
        permutation -= (permunreg[1] * 12);
        permunreg[2] = permutation / 3;
        permutation -= (permunreg[2] * 3);
        permunreg[3] = permutation;
        break;
      case 3:
        permunreg[0] = permutation / 20;
        permutation -= (permunreg[0] * 20);
        permunreg[1] = permutation / 4;
        permutation -= (permunreg[1] * 4);
        permunreg[2] = permutation;
        break;
      case 2:
        permunreg[0] = permutation / 5;
        permutation -= (permunreg[0] * 5);
        permunreg[1] = permutation;
        break;
      case 1:
        permunreg[0] = permutation;
        break;
      }

      // Decode the Lehmer code for this permutation of the registers v.
      // http://en.wikipedia.org/wiki/Lehmer_code

      int registers[6] = {UNWIND_X86_REG_NONE, UNWIND_X86_REG_NONE,
                          UNWIND_X86_REG_NONE, UNWIND_X86_REG_NONE,
                          UNWIND_X86_REG_NONE, UNWIND_X86_REG_NONE};
      bool used[7] = {false, false, false, false, false, false, false};
      for (uint32_t i = 0; i < register_count; i++) {
        int renum = 0;
        for (int j = 1; j < 7; j++) {
          if (!used[j]) {
            if (renum == permunreg[i]) {
              registers[i] = j;
              used[j] = true;
              break;
            }
            renum++;
          }
        }
      }

      uint32_t saved_registers_offset = 1;
      saved_registers_offset++;

      for (int i = (sizeof(registers) / sizeof(int)) - 1; i >= 0; i--) {
        switch (registers[i]) {
        case UNWIND_X86_REG_NONE:
          break;
        case UNWIND_X86_REG_EBX:
        case UNWIND_X86_REG_ECX:
        case UNWIND_X86_REG_EDX:
        case UNWIND_X86_REG_EDI:
        case UNWIND_X86_REG_ESI:
        case UNWIND_X86_REG_EBP:
          row->SetRegisterLocationToAtCFAPlusOffset(
              translate_to_eh_frame_regnum_i386(registers[i]),
              wordsize * -saved_registers_offset, true);
          saved_registers_offset++;
          break;
        }
      }
    }

    unwind_plan.AppendRow(row);
    return true;
  } break;

  case UNWIND_X86_MODE_DWARF: {
    return false;
  } break;
  }
  return false;
}

// DWARF register numbers from "DWARF for the ARM 64-bit Architecture (AArch64)"
// doc by ARM

enum arm64_eh_regnum {
  x19 = 19,
  x20 = 20,
  x21 = 21,
  x22 = 22,
  x23 = 23,
  x24 = 24,
  x25 = 25,
  x26 = 26,
  x27 = 27,
  x28 = 28,

  fp = 29,
  ra = 30,
  sp = 31,
  pc = 32,

  // Compact unwind encodes d8-d15 but we don't have eh_frame / dwarf reg #'s
  // for the 64-bit fp regs.  Normally in DWARF it's context sensitive - so it
  // knows it is fetching a 32- or 64-bit quantity from reg v8 to indicate s0
  // or d0 - but the unwinder is operating at a lower level and we'd try to
  // fetch 128 bits if we were told that v8 were stored on the stack...
  v8 = 72,
  v9 = 73,
  v10 = 74,
  v11 = 75,
  v12 = 76,
  v13 = 77,
  v14 = 78,
  v15 = 79,
};

enum arm_eh_regnum {
  arm_r0 = 0,
  arm_r1 = 1,
  arm_r2 = 2,
  arm_r3 = 3,
  arm_r4 = 4,
  arm_r5 = 5,
  arm_r6 = 6,
  arm_r7 = 7,
  arm_r8 = 8,
  arm_r9 = 9,
  arm_r10 = 10,
  arm_r11 = 11,
  arm_r12 = 12,

  arm_sp = 13,
  arm_lr = 14,
  arm_pc = 15,

  arm_d0 = 256,
  arm_d1 = 257,
  arm_d2 = 258,
  arm_d3 = 259,
  arm_d4 = 260,
  arm_d5 = 261,
  arm_d6 = 262,
  arm_d7 = 263,
  arm_d8 = 264,
  arm_d9 = 265,
  arm_d10 = 266,
  arm_d11 = 267,
  arm_d12 = 268,
  arm_d13 = 269,
  arm_d14 = 270,
};

bool CompactUnwindInfo::CreateUnwindPlan_arm64(Target &target,
                                               FunctionInfo &function_info,
                                               UnwindPlan &unwind_plan,
                                               Address pc_or_function_start) {
  unwind_plan.SetSourceName("compact unwind info");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolYes);
  unwind_plan.SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);
  unwind_plan.SetRegisterKind(eRegisterKindEHFrame);

  unwind_plan.SetLSDAAddress(function_info.lsda_address);
  unwind_plan.SetPersonalityFunctionPtr(function_info.personality_ptr_address);

  UnwindPlan::RowSP row(new UnwindPlan::Row);

  const int wordsize = 8;
  int mode = function_info.encoding & UNWIND_ARM64_MODE_MASK;

  if (mode == UNWIND_ARM64_MODE_DWARF)
    return false;

  if (mode == UNWIND_ARM64_MODE_FRAMELESS) {
    row->SetOffset(0);

    uint32_t stack_size =
        (EXTRACT_BITS(function_info.encoding,
                      UNWIND_ARM64_FRAMELESS_STACK_SIZE_MASK)) *
        16;

    // Our previous Call Frame Address is the stack pointer plus the stack size
    row->GetCFAValue().SetIsRegisterPlusOffset(arm64_eh_regnum::sp, stack_size);

    // Our previous PC is in the LR
    row->SetRegisterLocationToRegister(arm64_eh_regnum::pc, arm64_eh_regnum::ra,
                                       true);

    unwind_plan.AppendRow(row);
    return true;
  }

  // Should not be possible
  if (mode != UNWIND_ARM64_MODE_FRAME)
    return false;

  // mode == UNWIND_ARM64_MODE_FRAME

  row->GetCFAValue().SetIsRegisterPlusOffset(arm64_eh_regnum::fp, 2 * wordsize);
  row->SetOffset(0);
  row->SetRegisterLocationToAtCFAPlusOffset(arm64_eh_regnum::fp, wordsize * -2,
                                            true);
  row->SetRegisterLocationToAtCFAPlusOffset(arm64_eh_regnum::pc, wordsize * -1,
                                            true);
  row->SetRegisterLocationToIsCFAPlusOffset(arm64_eh_regnum::sp, 0, true);

  int reg_pairs_saved_count = 1;

  uint32_t saved_register_bits = function_info.encoding & 0xfff;

  if (saved_register_bits & UNWIND_ARM64_FRAME_X19_X20_PAIR) {
    int cfa_offset = reg_pairs_saved_count * -2 * wordsize;
    cfa_offset -= wordsize;
    row->SetRegisterLocationToAtCFAPlusOffset(arm64_eh_regnum::x19, cfa_offset,
                                              true);
    cfa_offset -= wordsize;
    row->SetRegisterLocationToAtCFAPlusOffset(arm64_eh_regnum::x20, cfa_offset,
                                              true);
    reg_pairs_saved_count++;
  }

  if (saved_register_bits & UNWIND_ARM64_FRAME_X21_X22_PAIR) {
    int cfa_offset = reg_pairs_saved_count * -2 * wordsize;
    cfa_offset -= wordsize;
    row->SetRegisterLocationToAtCFAPlusOffset(arm64_eh_regnum::x21, cfa_offset,
                                              true);
    cfa_offset -= wordsize;
    row->SetRegisterLocationToAtCFAPlusOffset(arm64_eh_regnum::x22, cfa_offset,
                                              true);
    reg_pairs_saved_count++;
  }

  if (saved_register_bits & UNWIND_ARM64_FRAME_X23_X24_PAIR) {
    int cfa_offset = reg_pairs_saved_count * -2 * wordsize;
    cfa_offset -= wordsize;
    row->SetRegisterLocationToAtCFAPlusOffset(arm64_eh_regnum::x23, cfa_offset,
                                              true);
    cfa_offset -= wordsize;
    row->SetRegisterLocationToAtCFAPlusOffset(arm64_eh_regnum::x24, cfa_offset,
                                              true);
    reg_pairs_saved_count++;
  }

  if (saved_register_bits & UNWIND_ARM64_FRAME_X25_X26_PAIR) {
    int cfa_offset = reg_pairs_saved_count * -2 * wordsize;
    cfa_offset -= wordsize;
    row->SetRegisterLocationToAtCFAPlusOffset(arm64_eh_regnum::x25, cfa_offset,
                                              true);
    cfa_offset -= wordsize;
    row->SetRegisterLocationToAtCFAPlusOffset(arm64_eh_regnum::x26, cfa_offset,
                                              true);
    reg_pairs_saved_count++;
  }

  if (saved_register_bits & UNWIND_ARM64_FRAME_X27_X28_PAIR) {
    int cfa_offset = reg_pairs_saved_count * -2 * wordsize;
    cfa_offset -= wordsize;
    row->SetRegisterLocationToAtCFAPlusOffset(arm64_eh_regnum::x27, cfa_offset,
                                              true);
    cfa_offset -= wordsize;
    row->SetRegisterLocationToAtCFAPlusOffset(arm64_eh_regnum::x28, cfa_offset,
                                              true);
    reg_pairs_saved_count++;
  }

  // If we use the v8-v15 regnums here, the unwinder will try to grab 128 bits
  // off the stack;
  // not sure if we have a good way to represent the 64-bitness of these saves.

  if (saved_register_bits & UNWIND_ARM64_FRAME_D8_D9_PAIR) {
    reg_pairs_saved_count++;
  }
  if (saved_register_bits & UNWIND_ARM64_FRAME_D10_D11_PAIR) {
    reg_pairs_saved_count++;
  }
  if (saved_register_bits & UNWIND_ARM64_FRAME_D12_D13_PAIR) {
    reg_pairs_saved_count++;
  }
  if (saved_register_bits & UNWIND_ARM64_FRAME_D14_D15_PAIR) {
    reg_pairs_saved_count++;
  }

  unwind_plan.AppendRow(row);
  return true;
}

bool CompactUnwindInfo::CreateUnwindPlan_armv7(Target &target,
                                               FunctionInfo &function_info,
                                               UnwindPlan &unwind_plan,
                                               Address pc_or_function_start) {
  unwind_plan.SetSourceName("compact unwind info");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolYes);
  unwind_plan.SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);
  unwind_plan.SetRegisterKind(eRegisterKindEHFrame);

  unwind_plan.SetLSDAAddress(function_info.lsda_address);
  unwind_plan.SetPersonalityFunctionPtr(function_info.personality_ptr_address);

  UnwindPlan::RowSP row(new UnwindPlan::Row);

  const int wordsize = 4;
  int mode = function_info.encoding & UNWIND_ARM_MODE_MASK;

  if (mode == UNWIND_ARM_MODE_DWARF)
    return false;

  uint32_t stack_adjust = (EXTRACT_BITS(function_info.encoding,
                                        UNWIND_ARM_FRAME_STACK_ADJUST_MASK)) *
                          wordsize;

  row->GetCFAValue().SetIsRegisterPlusOffset(arm_r7,
                                             (2 * wordsize) + stack_adjust);
  row->SetOffset(0);
  row->SetRegisterLocationToAtCFAPlusOffset(
      arm_r7, (wordsize * -2) - stack_adjust, true);
  row->SetRegisterLocationToAtCFAPlusOffset(
      arm_pc, (wordsize * -1) - stack_adjust, true);
  row->SetRegisterLocationToIsCFAPlusOffset(arm_sp, 0, true);

  int cfa_offset = -stack_adjust - (2 * wordsize);

  uint32_t saved_register_bits = function_info.encoding & 0xff;

  if (saved_register_bits & UNWIND_ARM_FRAME_FIRST_PUSH_R6) {
    cfa_offset -= wordsize;
    row->SetRegisterLocationToAtCFAPlusOffset(arm_r6, cfa_offset, true);
  }

  if (saved_register_bits & UNWIND_ARM_FRAME_FIRST_PUSH_R5) {
    cfa_offset -= wordsize;
    row->SetRegisterLocationToAtCFAPlusOffset(arm_r5, cfa_offset, true);
  }

  if (saved_register_bits & UNWIND_ARM_FRAME_FIRST_PUSH_R4) {
    cfa_offset -= wordsize;
    row->SetRegisterLocationToAtCFAPlusOffset(arm_r4, cfa_offset, true);
  }

  if (saved_register_bits & UNWIND_ARM_FRAME_SECOND_PUSH_R12) {
    cfa_offset -= wordsize;
    row->SetRegisterLocationToAtCFAPlusOffset(arm_r12, cfa_offset, true);
  }

  if (saved_register_bits & UNWIND_ARM_FRAME_SECOND_PUSH_R11) {
    cfa_offset -= wordsize;
    row->SetRegisterLocationToAtCFAPlusOffset(arm_r11, cfa_offset, true);
  }

  if (saved_register_bits & UNWIND_ARM_FRAME_SECOND_PUSH_R10) {
    cfa_offset -= wordsize;
    row->SetRegisterLocationToAtCFAPlusOffset(arm_r10, cfa_offset, true);
  }

  if (saved_register_bits & UNWIND_ARM_FRAME_SECOND_PUSH_R9) {
    cfa_offset -= wordsize;
    row->SetRegisterLocationToAtCFAPlusOffset(arm_r9, cfa_offset, true);
  }

  if (saved_register_bits & UNWIND_ARM_FRAME_SECOND_PUSH_R8) {
    cfa_offset -= wordsize;
    row->SetRegisterLocationToAtCFAPlusOffset(arm_r8, cfa_offset, true);
  }

  if (mode == UNWIND_ARM_MODE_FRAME_D) {
    uint32_t d_reg_bits =
        EXTRACT_BITS(function_info.encoding, UNWIND_ARM_FRAME_D_REG_COUNT_MASK);
    switch (d_reg_bits) {
    case 0:
      // vpush {d8}
      cfa_offset -= 8;
      row->SetRegisterLocationToAtCFAPlusOffset(arm_d8, cfa_offset, true);
      break;
    case 1:
      // vpush {d10}
      // vpush {d8}
      cfa_offset -= 8;
      row->SetRegisterLocationToAtCFAPlusOffset(arm_d10, cfa_offset, true);
      cfa_offset -= 8;
      row->SetRegisterLocationToAtCFAPlusOffset(arm_d8, cfa_offset, true);
      break;
    case 2:
      // vpush {d12}
      // vpush {d10}
      // vpush {d8}
      cfa_offset -= 8;
      row->SetRegisterLocationToAtCFAPlusOffset(arm_d12, cfa_offset, true);
      cfa_offset -= 8;
      row->SetRegisterLocationToAtCFAPlusOffset(arm_d10, cfa_offset, true);
      cfa_offset -= 8;
      row->SetRegisterLocationToAtCFAPlusOffset(arm_d8, cfa_offset, true);
      break;
    case 3:
      // vpush {d14}
      // vpush {d12}
      // vpush {d10}
      // vpush {d8}
      cfa_offset -= 8;
      row->SetRegisterLocationToAtCFAPlusOffset(arm_d14, cfa_offset, true);
      cfa_offset -= 8;
      row->SetRegisterLocationToAtCFAPlusOffset(arm_d12, cfa_offset, true);
      cfa_offset -= 8;
      row->SetRegisterLocationToAtCFAPlusOffset(arm_d10, cfa_offset, true);
      cfa_offset -= 8;
      row->SetRegisterLocationToAtCFAPlusOffset(arm_d8, cfa_offset, true);
      break;
    case 4:
      // vpush {d14}
      // vpush {d12}
      // sp = (sp - 24) & (-16);
      // vst   {d8, d9, d10}
      cfa_offset -= 8;
      row->SetRegisterLocationToAtCFAPlusOffset(arm_d14, cfa_offset, true);
      cfa_offset -= 8;
      row->SetRegisterLocationToAtCFAPlusOffset(arm_d12, cfa_offset, true);

      // FIXME we don't have a way to represent reg saves at an specific
      // alignment short of
      // coming up with some DWARF location description.

      break;
    case 5:
      // vpush {d14}
      // sp = (sp - 40) & (-16);
      // vst   {d8, d9, d10, d11}
      // vst   {d12}

      cfa_offset -= 8;
      row->SetRegisterLocationToAtCFAPlusOffset(arm_d14, cfa_offset, true);

      // FIXME we don't have a way to represent reg saves at an specific
      // alignment short of
      // coming up with some DWARF location description.

      break;
    case 6:
      // sp = (sp - 56) & (-16);
      // vst   {d8, d9, d10, d11}
      // vst   {d12, d13, d14}

      // FIXME we don't have a way to represent reg saves at an specific
      // alignment short of
      // coming up with some DWARF location description.

      break;
    case 7:
      // sp = (sp - 64) & (-16);
      // vst   {d8, d9, d10, d11}
      // vst   {d12, d13, d14, d15}

      // FIXME we don't have a way to represent reg saves at an specific
      // alignment short of
      // coming up with some DWARF location description.

      break;
    }
  }

  unwind_plan.AppendRow(row);
  return true;
}
