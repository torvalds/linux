//===-- DynamicRegisterInfo.cpp ----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "DynamicRegisterInfo.h"

#include "lldb/Core/StreamFile.h"
#include "lldb/DataFormatters/FormatManager.h"
#include "lldb/Host/StringConvert.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/Utility/StringExtractor.h"
#include "lldb/Utility/StructuredData.h"

using namespace lldb;
using namespace lldb_private;

DynamicRegisterInfo::DynamicRegisterInfo(
    const lldb_private::StructuredData::Dictionary &dict,
    const lldb_private::ArchSpec &arch) {
  SetRegisterInfo(dict, arch);
}

DynamicRegisterInfo::DynamicRegisterInfo(DynamicRegisterInfo &&info) {
  MoveFrom(std::move(info));
}

DynamicRegisterInfo &
DynamicRegisterInfo::operator=(DynamicRegisterInfo &&info) {
  MoveFrom(std::move(info));
  return *this;
}

void DynamicRegisterInfo::MoveFrom(DynamicRegisterInfo &&info) {
  m_regs = std::move(info.m_regs);
  m_sets = std::move(info.m_sets);
  m_set_reg_nums = std::move(info.m_set_reg_nums);
  m_set_names = std::move(info.m_set_names);
  m_value_regs_map = std::move(info.m_value_regs_map);
  m_invalidate_regs_map = std::move(info.m_invalidate_regs_map);
  m_dynamic_reg_size_map = std::move(info.m_dynamic_reg_size_map);

  m_reg_data_byte_size = info.m_reg_data_byte_size;
  m_finalized = info.m_finalized;

  if (m_finalized) {
    const size_t num_sets = m_sets.size();
    for (size_t set = 0; set < num_sets; ++set)
      m_sets[set].registers = m_set_reg_nums[set].data();
  }

  info.Clear();
}

size_t
DynamicRegisterInfo::SetRegisterInfo(const StructuredData::Dictionary &dict,
                                     const ArchSpec &arch) {
  assert(!m_finalized);
  StructuredData::Array *sets = nullptr;
  if (dict.GetValueForKeyAsArray("sets", sets)) {
    const uint32_t num_sets = sets->GetSize();
    for (uint32_t i = 0; i < num_sets; ++i) {
      ConstString set_name;
      if (sets->GetItemAtIndexAsString(i, set_name) && !set_name.IsEmpty()) {
        m_sets.push_back({ set_name.AsCString(), NULL, 0, NULL });
      } else {
        Clear();
        printf("error: register sets must have valid names\n");
        return 0;
      }
    }
    m_set_reg_nums.resize(m_sets.size());
  }

  StructuredData::Array *regs = nullptr;
  if (!dict.GetValueForKeyAsArray("registers", regs))
    return 0;

  const uint32_t num_regs = regs->GetSize();
  //        typedef std::map<std::string, std::vector<std::string> >
  //        InvalidateNameMap;
  //        InvalidateNameMap invalidate_map;
  for (uint32_t i = 0; i < num_regs; ++i) {
    StructuredData::Dictionary *reg_info_dict = nullptr;
    if (!regs->GetItemAtIndexAsDictionary(i, reg_info_dict)) {
      Clear();
      printf("error: items in the 'registers' array must be dictionaries\n");
      regs->DumpToStdout();
      return 0;
    }

    // { 'name':'rcx'       , 'bitsize' :  64, 'offset' :  16,
    // 'encoding':'uint' , 'format':'hex'         , 'set': 0, 'ehframe' : 2,
    // 'dwarf' : 2, 'generic':'arg4', 'alt-name':'arg4', },
    RegisterInfo reg_info;
    std::vector<uint32_t> value_regs;
    std::vector<uint32_t> invalidate_regs;
    memset(&reg_info, 0, sizeof(reg_info));

    ConstString name_val;
    ConstString alt_name_val;
    if (!reg_info_dict->GetValueForKeyAsString("name", name_val, nullptr)) {
      Clear();
      printf("error: registers must have valid names and offsets\n");
      reg_info_dict->DumpToStdout();
      return 0;
    }
    reg_info.name = name_val.GetCString();
    reg_info_dict->GetValueForKeyAsString("alt-name", alt_name_val, nullptr);
    reg_info.alt_name = alt_name_val.GetCString();

    reg_info_dict->GetValueForKeyAsInteger("offset", reg_info.byte_offset,
                                           UINT32_MAX);

    const ByteOrder byte_order = arch.GetByteOrder();

    if (reg_info.byte_offset == UINT32_MAX) {
      // No offset for this register, see if the register has a value
      // expression which indicates this register is part of another register.
      // Value expressions are things like "rax[31:0]" which state that the
      // current register's value is in a concrete register "rax" in bits 31:0.
      // If there is a value expression we can calculate the offset
      bool success = false;
      llvm::StringRef slice_str;
      if (reg_info_dict->GetValueForKeyAsString("slice", slice_str, nullptr)) {
        // Slices use the following format:
        //  REGNAME[MSBIT:LSBIT]
        // REGNAME - name of the register to grab a slice of
        // MSBIT - the most significant bit at which the current register value
        // starts at
        // LSBIT - the least significant bit at which the current register value
        // ends at
        static RegularExpression g_bitfield_regex(
            llvm::StringRef("([A-Za-z_][A-Za-z0-9_]*)\\[([0-9]+):([0-9]+)\\]"));
        RegularExpression::Match regex_match(3);
        if (g_bitfield_regex.Execute(slice_str, &regex_match)) {
          llvm::StringRef reg_name_str;
          std::string msbit_str;
          std::string lsbit_str;
          if (regex_match.GetMatchAtIndex(slice_str, 1, reg_name_str) &&
              regex_match.GetMatchAtIndex(slice_str, 2, msbit_str) &&
              regex_match.GetMatchAtIndex(slice_str, 3, lsbit_str)) {
            const uint32_t msbit =
                StringConvert::ToUInt32(msbit_str.c_str(), UINT32_MAX);
            const uint32_t lsbit =
                StringConvert::ToUInt32(lsbit_str.c_str(), UINT32_MAX);
            if (msbit != UINT32_MAX && lsbit != UINT32_MAX) {
              if (msbit > lsbit) {
                const uint32_t msbyte = msbit / 8;
                const uint32_t lsbyte = lsbit / 8;

                ConstString containing_reg_name(reg_name_str);

                const RegisterInfo *containing_reg_info =
                    GetRegisterInfo(containing_reg_name);
                if (containing_reg_info) {
                  const uint32_t max_bit = containing_reg_info->byte_size * 8;
                  if (msbit < max_bit && lsbit < max_bit) {
                    m_invalidate_regs_map[containing_reg_info
                                              ->kinds[eRegisterKindLLDB]]
                        .push_back(i);
                    m_value_regs_map[i].push_back(
                        containing_reg_info->kinds[eRegisterKindLLDB]);
                    m_invalidate_regs_map[i].push_back(
                        containing_reg_info->kinds[eRegisterKindLLDB]);

                    if (byte_order == eByteOrderLittle) {
                      success = true;
                      reg_info.byte_offset =
                          containing_reg_info->byte_offset + lsbyte;
                    } else if (byte_order == eByteOrderBig) {
                      success = true;
                      reg_info.byte_offset =
                          containing_reg_info->byte_offset + msbyte;
                    } else {
                      llvm_unreachable("Invalid byte order");
                    }
                  } else {
                    if (msbit > max_bit)
                      printf("error: msbit (%u) must be less than the bitsize "
                             "of the register (%u)\n",
                             msbit, max_bit);
                    else
                      printf("error: lsbit (%u) must be less than the bitsize "
                             "of the register (%u)\n",
                             lsbit, max_bit);
                  }
                } else {
                  printf("error: invalid concrete register \"%s\"\n",
                         containing_reg_name.GetCString());
                }
              } else {
                printf("error: msbit (%u) must be greater than lsbit (%u)\n",
                       msbit, lsbit);
              }
            } else {
              printf("error: msbit (%u) and lsbit (%u) must be valid\n", msbit,
                     lsbit);
            }
          } else {
            // TODO: print error invalid slice string that doesn't follow the
            // format
            printf("error: failed to extract regex matches for parsing the "
                   "register bitfield regex\n");
          }
        } else {
          // TODO: print error invalid slice string that doesn't follow the
          // format
          printf("error: failed to match against register bitfield regex\n");
        }
      } else {
        StructuredData::Array *composite_reg_list = nullptr;
        if (reg_info_dict->GetValueForKeyAsArray("composite",
                                                 composite_reg_list)) {
          const size_t num_composite_regs = composite_reg_list->GetSize();
          if (num_composite_regs > 0) {
            uint32_t composite_offset = UINT32_MAX;
            for (uint32_t composite_idx = 0; composite_idx < num_composite_regs;
                 ++composite_idx) {
              ConstString composite_reg_name;
              if (composite_reg_list->GetItemAtIndexAsString(
                      composite_idx, composite_reg_name, nullptr)) {
                const RegisterInfo *composite_reg_info =
                    GetRegisterInfo(composite_reg_name);
                if (composite_reg_info) {
                  composite_offset = std::min(composite_offset,
                                              composite_reg_info->byte_offset);
                  m_value_regs_map[i].push_back(
                      composite_reg_info->kinds[eRegisterKindLLDB]);
                  m_invalidate_regs_map[composite_reg_info
                                            ->kinds[eRegisterKindLLDB]]
                      .push_back(i);
                  m_invalidate_regs_map[i].push_back(
                      composite_reg_info->kinds[eRegisterKindLLDB]);
                } else {
                  // TODO: print error invalid slice string that doesn't follow
                  // the format
                  printf("error: failed to find composite register by name: "
                         "\"%s\"\n",
                         composite_reg_name.GetCString());
                }
              } else {
                printf(
                    "error: 'composite' list value wasn't a python string\n");
              }
            }
            if (composite_offset != UINT32_MAX) {
              reg_info.byte_offset = composite_offset;
              success = m_value_regs_map.find(i) != m_value_regs_map.end();
            } else {
              printf("error: 'composite' registers must specify at least one "
                     "real register\n");
            }
          } else {
            printf("error: 'composite' list was empty\n");
          }
        }
      }

      if (!success) {
        Clear();
        reg_info_dict->DumpToStdout();
        return 0;
      }
    }

    int64_t bitsize = 0;
    if (!reg_info_dict->GetValueForKeyAsInteger("bitsize", bitsize)) {
      Clear();
      printf("error: invalid or missing 'bitsize' key/value pair in register "
             "dictionary\n");
      reg_info_dict->DumpToStdout();
      return 0;
    }

    reg_info.byte_size = bitsize / 8;

    llvm::StringRef dwarf_opcode_string;
    if (reg_info_dict->GetValueForKeyAsString("dynamic_size_dwarf_expr_bytes",
                                              dwarf_opcode_string)) {
      reg_info.dynamic_size_dwarf_len = dwarf_opcode_string.size() / 2;
      assert(reg_info.dynamic_size_dwarf_len > 0);

      std::vector<uint8_t> dwarf_opcode_bytes(reg_info.dynamic_size_dwarf_len);
      uint32_t j;
      StringExtractor opcode_extractor(dwarf_opcode_string);
      uint32_t ret_val = opcode_extractor.GetHexBytesAvail(dwarf_opcode_bytes);
      UNUSED_IF_ASSERT_DISABLED(ret_val);
      assert(ret_val == reg_info.dynamic_size_dwarf_len);

      for (j = 0; j < reg_info.dynamic_size_dwarf_len; ++j)
        m_dynamic_reg_size_map[i].push_back(dwarf_opcode_bytes[j]);

      reg_info.dynamic_size_dwarf_expr_bytes = m_dynamic_reg_size_map[i].data();
    }

    llvm::StringRef format_str;
    if (reg_info_dict->GetValueForKeyAsString("format", format_str, nullptr)) {
      if (OptionArgParser::ToFormat(format_str.str().c_str(), reg_info.format,
                                    NULL)
              .Fail()) {
        Clear();
        printf("error: invalid 'format' value in register dictionary\n");
        reg_info_dict->DumpToStdout();
        return 0;
      }
    } else {
      reg_info_dict->GetValueForKeyAsInteger("format", reg_info.format,
                                             eFormatHex);
    }

    llvm::StringRef encoding_str;
    if (reg_info_dict->GetValueForKeyAsString("encoding", encoding_str))
      reg_info.encoding = Args::StringToEncoding(encoding_str, eEncodingUint);
    else
      reg_info_dict->GetValueForKeyAsInteger("encoding", reg_info.encoding,
                                             eEncodingUint);

    size_t set = 0;
    if (!reg_info_dict->GetValueForKeyAsInteger<size_t>("set", set, -1) ||
        set >= m_sets.size()) {
      Clear();
      printf("error: invalid 'set' value in register dictionary, valid values "
             "are 0 - %i\n",
             (int)set);
      reg_info_dict->DumpToStdout();
      return 0;
    }

    // Fill in the register numbers
    reg_info.kinds[lldb::eRegisterKindLLDB] = i;
    reg_info.kinds[lldb::eRegisterKindProcessPlugin] = i;
    uint32_t eh_frame_regno = LLDB_INVALID_REGNUM;
    reg_info_dict->GetValueForKeyAsInteger("gcc", eh_frame_regno,
                                           LLDB_INVALID_REGNUM);
    if (eh_frame_regno == LLDB_INVALID_REGNUM)
      reg_info_dict->GetValueForKeyAsInteger("ehframe", eh_frame_regno,
                                             LLDB_INVALID_REGNUM);
    reg_info.kinds[lldb::eRegisterKindEHFrame] = eh_frame_regno;
    reg_info_dict->GetValueForKeyAsInteger(
        "dwarf", reg_info.kinds[lldb::eRegisterKindDWARF], LLDB_INVALID_REGNUM);
    llvm::StringRef generic_str;
    if (reg_info_dict->GetValueForKeyAsString("generic", generic_str))
      reg_info.kinds[lldb::eRegisterKindGeneric] =
          Args::StringToGenericRegister(generic_str);
    else
      reg_info_dict->GetValueForKeyAsInteger(
          "generic", reg_info.kinds[lldb::eRegisterKindGeneric],
          LLDB_INVALID_REGNUM);

    // Check if this register invalidates any other register values when it is
    // modified
    StructuredData::Array *invalidate_reg_list = nullptr;
    if (reg_info_dict->GetValueForKeyAsArray("invalidate-regs",
                                             invalidate_reg_list)) {
      const size_t num_regs = invalidate_reg_list->GetSize();
      if (num_regs > 0) {
        for (uint32_t idx = 0; idx < num_regs; ++idx) {
          ConstString invalidate_reg_name;
          uint64_t invalidate_reg_num;
          if (invalidate_reg_list->GetItemAtIndexAsString(
                  idx, invalidate_reg_name)) {
            const RegisterInfo *invalidate_reg_info =
                GetRegisterInfo(invalidate_reg_name);
            if (invalidate_reg_info) {
              m_invalidate_regs_map[i].push_back(
                  invalidate_reg_info->kinds[eRegisterKindLLDB]);
            } else {
              // TODO: print error invalid slice string that doesn't follow the
              // format
              printf("error: failed to find a 'invalidate-regs' register for "
                     "\"%s\" while parsing register \"%s\"\n",
                     invalidate_reg_name.GetCString(), reg_info.name);
            }
          } else if (invalidate_reg_list->GetItemAtIndexAsInteger(
                         idx, invalidate_reg_num)) {
            if (invalidate_reg_num != UINT64_MAX)
              m_invalidate_regs_map[i].push_back(invalidate_reg_num);
            else
              printf("error: 'invalidate-regs' list value wasn't a valid "
                     "integer\n");
          } else {
            printf("error: 'invalidate-regs' list value wasn't a python string "
                   "or integer\n");
          }
        }
      } else {
        printf("error: 'invalidate-regs' contained an empty list\n");
      }
    }

    // Calculate the register offset
    const size_t end_reg_offset = reg_info.byte_offset + reg_info.byte_size;
    if (m_reg_data_byte_size < end_reg_offset)
      m_reg_data_byte_size = end_reg_offset;

    m_regs.push_back(reg_info);
    m_set_reg_nums[set].push_back(i);
  }
  Finalize(arch);
  return m_regs.size();
}

void DynamicRegisterInfo::AddRegister(RegisterInfo &reg_info,
                                      ConstString &reg_name,
                                      ConstString &reg_alt_name,
                                      ConstString &set_name) {
  assert(!m_finalized);
  const uint32_t reg_num = m_regs.size();
  reg_info.name = reg_name.AsCString();
  assert(reg_info.name);
  reg_info.alt_name = reg_alt_name.AsCString(NULL);
  uint32_t i;
  if (reg_info.value_regs) {
    for (i = 0; reg_info.value_regs[i] != LLDB_INVALID_REGNUM; ++i)
      m_value_regs_map[reg_num].push_back(reg_info.value_regs[i]);
  }
  if (reg_info.invalidate_regs) {
    for (i = 0; reg_info.invalidate_regs[i] != LLDB_INVALID_REGNUM; ++i)
      m_invalidate_regs_map[reg_num].push_back(reg_info.invalidate_regs[i]);
  }
  if (reg_info.dynamic_size_dwarf_expr_bytes) {
    for (i = 0; i < reg_info.dynamic_size_dwarf_len; ++i)
      m_dynamic_reg_size_map[reg_num].push_back(
          reg_info.dynamic_size_dwarf_expr_bytes[i]);

    reg_info.dynamic_size_dwarf_expr_bytes =
        m_dynamic_reg_size_map[reg_num].data();
  }

  m_regs.push_back(reg_info);
  uint32_t set = GetRegisterSetIndexByName(set_name, true);
  assert(set < m_sets.size());
  assert(set < m_set_reg_nums.size());
  assert(set < m_set_names.size());
  m_set_reg_nums[set].push_back(reg_num);
  size_t end_reg_offset = reg_info.byte_offset + reg_info.byte_size;
  if (m_reg_data_byte_size < end_reg_offset)
    m_reg_data_byte_size = end_reg_offset;
}

void DynamicRegisterInfo::Finalize(const ArchSpec &arch) {
  if (m_finalized)
    return;

  m_finalized = true;
  const size_t num_sets = m_sets.size();
  for (size_t set = 0; set < num_sets; ++set) {
    assert(m_sets.size() == m_set_reg_nums.size());
    m_sets[set].num_registers = m_set_reg_nums[set].size();
    m_sets[set].registers = m_set_reg_nums[set].data();
  }

  // sort and unique all value registers and make sure each is terminated with
  // LLDB_INVALID_REGNUM

  for (reg_to_regs_map::iterator pos = m_value_regs_map.begin(),
                                 end = m_value_regs_map.end();
       pos != end; ++pos) {
    if (pos->second.size() > 1) {
      llvm::sort(pos->second.begin(), pos->second.end());
      reg_num_collection::iterator unique_end =
          std::unique(pos->second.begin(), pos->second.end());
      if (unique_end != pos->second.end())
        pos->second.erase(unique_end, pos->second.end());
    }
    assert(!pos->second.empty());
    if (pos->second.back() != LLDB_INVALID_REGNUM)
      pos->second.push_back(LLDB_INVALID_REGNUM);
  }

  // Now update all value_regs with each register info as needed
  const size_t num_regs = m_regs.size();
  for (size_t i = 0; i < num_regs; ++i) {
    if (m_value_regs_map.find(i) != m_value_regs_map.end())
      m_regs[i].value_regs = m_value_regs_map[i].data();
    else
      m_regs[i].value_regs = NULL;
  }

  // Expand all invalidation dependencies
  for (reg_to_regs_map::iterator pos = m_invalidate_regs_map.begin(),
                                 end = m_invalidate_regs_map.end();
       pos != end; ++pos) {
    const uint32_t reg_num = pos->first;

    if (m_regs[reg_num].value_regs) {
      reg_num_collection extra_invalid_regs;
      for (const uint32_t invalidate_reg_num : pos->second) {
        reg_to_regs_map::iterator invalidate_pos =
            m_invalidate_regs_map.find(invalidate_reg_num);
        if (invalidate_pos != m_invalidate_regs_map.end()) {
          for (const uint32_t concrete_invalidate_reg_num :
               invalidate_pos->second) {
            if (concrete_invalidate_reg_num != reg_num)
              extra_invalid_regs.push_back(concrete_invalidate_reg_num);
          }
        }
      }
      pos->second.insert(pos->second.end(), extra_invalid_regs.begin(),
                         extra_invalid_regs.end());
    }
  }

  // sort and unique all invalidate registers and make sure each is terminated
  // with LLDB_INVALID_REGNUM
  for (reg_to_regs_map::iterator pos = m_invalidate_regs_map.begin(),
                                 end = m_invalidate_regs_map.end();
       pos != end; ++pos) {
    if (pos->second.size() > 1) {
      llvm::sort(pos->second.begin(), pos->second.end());
      reg_num_collection::iterator unique_end =
          std::unique(pos->second.begin(), pos->second.end());
      if (unique_end != pos->second.end())
        pos->second.erase(unique_end, pos->second.end());
    }
    assert(!pos->second.empty());
    if (pos->second.back() != LLDB_INVALID_REGNUM)
      pos->second.push_back(LLDB_INVALID_REGNUM);
  }

  // Now update all invalidate_regs with each register info as needed
  for (size_t i = 0; i < num_regs; ++i) {
    if (m_invalidate_regs_map.find(i) != m_invalidate_regs_map.end())
      m_regs[i].invalidate_regs = m_invalidate_regs_map[i].data();
    else
      m_regs[i].invalidate_regs = NULL;
  }

  // Check if we need to automatically set the generic registers in case they
  // weren't set
  bool generic_regs_specified = false;
  for (const auto &reg : m_regs) {
    if (reg.kinds[eRegisterKindGeneric] != LLDB_INVALID_REGNUM) {
      generic_regs_specified = true;
      break;
    }
  }

  if (!generic_regs_specified) {
    switch (arch.GetMachine()) {
    case llvm::Triple::aarch64:
    case llvm::Triple::aarch64_be:
      for (auto &reg : m_regs) {
        if (strcmp(reg.name, "pc") == 0)
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_PC;
        else if ((strcmp(reg.name, "fp") == 0) ||
                 (strcmp(reg.name, "x29") == 0))
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_FP;
        else if ((strcmp(reg.name, "lr") == 0) ||
                 (strcmp(reg.name, "x30") == 0))
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_RA;
        else if ((strcmp(reg.name, "sp") == 0) ||
                 (strcmp(reg.name, "x31") == 0))
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_SP;
        else if (strcmp(reg.name, "cpsr") == 0)
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_FLAGS;
      }
      break;

    case llvm::Triple::arm:
    case llvm::Triple::armeb:
    case llvm::Triple::thumb:
    case llvm::Triple::thumbeb:
      for (auto &reg : m_regs) {
        if ((strcmp(reg.name, "pc") == 0) || (strcmp(reg.name, "r15") == 0))
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_PC;
        else if ((strcmp(reg.name, "sp") == 0) ||
                 (strcmp(reg.name, "r13") == 0))
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_SP;
        else if ((strcmp(reg.name, "lr") == 0) ||
                 (strcmp(reg.name, "r14") == 0))
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_RA;
        else if ((strcmp(reg.name, "r7") == 0) &&
                 arch.GetTriple().getVendor() == llvm::Triple::Apple)
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_FP;
        else if ((strcmp(reg.name, "r11") == 0) &&
                 arch.GetTriple().getVendor() != llvm::Triple::Apple)
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_FP;
        else if (strcmp(reg.name, "fp") == 0)
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_FP;
        else if (strcmp(reg.name, "cpsr") == 0)
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_FLAGS;
      }
      break;

    case llvm::Triple::x86:
      for (auto &reg : m_regs) {
        if ((strcmp(reg.name, "eip") == 0) || (strcmp(reg.name, "pc") == 0))
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_PC;
        else if ((strcmp(reg.name, "esp") == 0) ||
                 (strcmp(reg.name, "sp") == 0))
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_SP;
        else if ((strcmp(reg.name, "ebp") == 0) ||
                 (strcmp(reg.name, "fp") == 0))
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_FP;
        else if ((strcmp(reg.name, "eflags") == 0) ||
                 (strcmp(reg.name, "flags") == 0))
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_FLAGS;
      }
      break;

    case llvm::Triple::x86_64:
      for (auto &reg : m_regs) {
        if ((strcmp(reg.name, "rip") == 0) || (strcmp(reg.name, "pc") == 0))
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_PC;
        else if ((strcmp(reg.name, "rsp") == 0) ||
                 (strcmp(reg.name, "sp") == 0))
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_SP;
        else if ((strcmp(reg.name, "rbp") == 0) ||
                 (strcmp(reg.name, "fp") == 0))
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_FP;
        else if ((strcmp(reg.name, "rflags") == 0) ||
                 (strcmp(reg.name, "flags") == 0))
          reg.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_FLAGS;
      }
      break;

    default:
      break;
    }
  }
}

size_t DynamicRegisterInfo::GetNumRegisters() const { return m_regs.size(); }

size_t DynamicRegisterInfo::GetNumRegisterSets() const { return m_sets.size(); }

size_t DynamicRegisterInfo::GetRegisterDataByteSize() const {
  return m_reg_data_byte_size;
}

const RegisterInfo *
DynamicRegisterInfo::GetRegisterInfoAtIndex(uint32_t i) const {
  if (i < m_regs.size())
    return &m_regs[i];
  return NULL;
}

RegisterInfo *DynamicRegisterInfo::GetRegisterInfoAtIndex(uint32_t i) {
  if (i < m_regs.size())
    return &m_regs[i];
  return NULL;
}

const RegisterSet *DynamicRegisterInfo::GetRegisterSet(uint32_t i) const {
  if (i < m_sets.size())
    return &m_sets[i];
  return NULL;
}

uint32_t DynamicRegisterInfo::GetRegisterSetIndexByName(ConstString &set_name,
                                                        bool can_create) {
  name_collection::iterator pos, end = m_set_names.end();
  for (pos = m_set_names.begin(); pos != end; ++pos) {
    if (*pos == set_name)
      return std::distance(m_set_names.begin(), pos);
  }

  m_set_names.push_back(set_name);
  m_set_reg_nums.resize(m_set_reg_nums.size() + 1);
  RegisterSet new_set = {set_name.AsCString(), NULL, 0, NULL};
  m_sets.push_back(new_set);
  return m_sets.size() - 1;
}

uint32_t
DynamicRegisterInfo::ConvertRegisterKindToRegisterNumber(uint32_t kind,
                                                         uint32_t num) const {
  reg_collection::const_iterator pos, end = m_regs.end();
  for (pos = m_regs.begin(); pos != end; ++pos) {
    if (pos->kinds[kind] == num)
      return std::distance(m_regs.begin(), pos);
  }

  return LLDB_INVALID_REGNUM;
}

void DynamicRegisterInfo::Clear() {
  m_regs.clear();
  m_sets.clear();
  m_set_reg_nums.clear();
  m_set_names.clear();
  m_value_regs_map.clear();
  m_invalidate_regs_map.clear();
  m_dynamic_reg_size_map.clear();
  m_reg_data_byte_size = 0;
  m_finalized = false;
}

void DynamicRegisterInfo::Dump() const {
  StreamFile s(stdout, false);
  const size_t num_regs = m_regs.size();
  s.Printf("%p: DynamicRegisterInfo contains %" PRIu64 " registers:\n",
           static_cast<const void *>(this), static_cast<uint64_t>(num_regs));
  for (size_t i = 0; i < num_regs; ++i) {
    s.Printf("[%3" PRIu64 "] name = %-10s", (uint64_t)i, m_regs[i].name);
    s.Printf(", size = %2u, offset = %4u, encoding = %u, format = %-10s",
             m_regs[i].byte_size, m_regs[i].byte_offset, m_regs[i].encoding,
             FormatManager::GetFormatAsCString(m_regs[i].format));
    if (m_regs[i].kinds[eRegisterKindProcessPlugin] != LLDB_INVALID_REGNUM)
      s.Printf(", process plugin = %3u",
               m_regs[i].kinds[eRegisterKindProcessPlugin]);
    if (m_regs[i].kinds[eRegisterKindDWARF] != LLDB_INVALID_REGNUM)
      s.Printf(", dwarf = %3u", m_regs[i].kinds[eRegisterKindDWARF]);
    if (m_regs[i].kinds[eRegisterKindEHFrame] != LLDB_INVALID_REGNUM)
      s.Printf(", ehframe = %3u", m_regs[i].kinds[eRegisterKindEHFrame]);
    if (m_regs[i].kinds[eRegisterKindGeneric] != LLDB_INVALID_REGNUM)
      s.Printf(", generic = %3u", m_regs[i].kinds[eRegisterKindGeneric]);
    if (m_regs[i].alt_name)
      s.Printf(", alt-name = %s", m_regs[i].alt_name);
    if (m_regs[i].value_regs) {
      s.Printf(", value_regs = [ ");
      for (size_t j = 0; m_regs[i].value_regs[j] != LLDB_INVALID_REGNUM; ++j) {
        s.Printf("%s ", m_regs[m_regs[i].value_regs[j]].name);
      }
      s.Printf("]");
    }
    if (m_regs[i].invalidate_regs) {
      s.Printf(", invalidate_regs = [ ");
      for (size_t j = 0; m_regs[i].invalidate_regs[j] != LLDB_INVALID_REGNUM;
           ++j) {
        s.Printf("%s ", m_regs[m_regs[i].invalidate_regs[j]].name);
      }
      s.Printf("]");
    }
    s.EOL();
  }

  const size_t num_sets = m_sets.size();
  s.Printf("%p: DynamicRegisterInfo contains %" PRIu64 " register sets:\n",
           static_cast<const void *>(this), static_cast<uint64_t>(num_sets));
  for (size_t i = 0; i < num_sets; ++i) {
    s.Printf("set[%" PRIu64 "] name = %s, regs = [", (uint64_t)i,
             m_sets[i].name);
    for (size_t idx = 0; idx < m_sets[i].num_registers; ++idx) {
      s.Printf("%s ", m_regs[m_sets[i].registers[idx]].name);
    }
    s.Printf("]\n");
  }
}

const lldb_private::RegisterInfo *DynamicRegisterInfo::GetRegisterInfo(
    const lldb_private::ConstString &reg_name) const {
  for (auto &reg_info : m_regs) {
    // We can use pointer comparison since we used a ConstString to set the
    // "name" member in AddRegister()
    if (reg_info.name == reg_name.GetCString()) {
      return &reg_info;
    }
  }
  return NULL;
}
