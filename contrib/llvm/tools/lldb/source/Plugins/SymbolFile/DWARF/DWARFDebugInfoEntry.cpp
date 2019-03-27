//===-- DWARFDebugInfoEntry.cpp ---------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "DWARFDebugInfoEntry.h"

#include <assert.h>

#include <algorithm>

#include "lldb/Core/Module.h"
#include "lldb/Expression/DWARFExpression.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Utility/Stream.h"

#include "DWARFUnit.h"
#include "DWARFDIECollection.h"
#include "DWARFDebugAbbrev.h"
#include "DWARFDebugAranges.h"
#include "DWARFDebugInfo.h"
#include "DWARFDebugRanges.h"
#include "DWARFDeclContext.h"
#include "DWARFFormValue.h"
#include "SymbolFileDWARF.h"
#include "SymbolFileDWARFDwo.h"

using namespace lldb_private;
using namespace std;
extern int g_verbose;

bool DWARFDebugInfoEntry::FastExtract(
    const DWARFDataExtractor &debug_info_data, const DWARFUnit *cu,
    const DWARFFormValue::FixedFormSizes &fixed_form_sizes,
    lldb::offset_t *offset_ptr) {
  m_offset = *offset_ptr;
  m_parent_idx = 0;
  m_sibling_idx = 0;
  const uint64_t abbr_idx = debug_info_data.GetULEB128(offset_ptr);
  lldbassert(abbr_idx <= UINT16_MAX);
  m_abbr_idx = abbr_idx;

  // assert (fixed_form_sizes);  // For best performance this should be
  // specified!

  if (m_abbr_idx) {
    lldb::offset_t offset = *offset_ptr;

    const DWARFAbbreviationDeclaration *abbrevDecl =
        cu->GetAbbreviations()->GetAbbreviationDeclaration(m_abbr_idx);

    if (abbrevDecl == NULL) {
      cu->GetSymbolFileDWARF()->GetObjectFile()->GetModule()->ReportError(
          "{0x%8.8x}: invalid abbreviation code %u, please file a bug and "
          "attach the file at the start of this error message",
          m_offset, (unsigned)abbr_idx);
      // WE can't parse anymore if the DWARF is borked...
      *offset_ptr = UINT32_MAX;
      return false;
    }
    m_tag = abbrevDecl->Tag();
    m_has_children = abbrevDecl->HasChildren();
    // Skip all data in the .debug_info for the attributes
    const uint32_t numAttributes = abbrevDecl->NumAttributes();
    uint32_t i;
    dw_form_t form;
    for (i = 0; i < numAttributes; ++i) {
      form = abbrevDecl->GetFormByIndexUnchecked(i);

      const uint8_t fixed_skip_size = fixed_form_sizes.GetSize(form);
      if (fixed_skip_size)
        offset += fixed_skip_size;
      else {
        bool form_is_indirect = false;
        do {
          form_is_indirect = false;
          uint32_t form_size = 0;
          switch (form) {
          // Blocks if inlined data that have a length field and the data bytes
          // inlined in the .debug_info
          case DW_FORM_exprloc:
          case DW_FORM_block:
            form_size = debug_info_data.GetULEB128(&offset);
            break;
          case DW_FORM_block1:
            form_size = debug_info_data.GetU8_unchecked(&offset);
            break;
          case DW_FORM_block2:
            form_size = debug_info_data.GetU16_unchecked(&offset);
            break;
          case DW_FORM_block4:
            form_size = debug_info_data.GetU32_unchecked(&offset);
            break;

          // Inlined NULL terminated C-strings
          case DW_FORM_string:
            debug_info_data.GetCStr(&offset);
            break;

          // Compile unit address sized values
          case DW_FORM_addr:
            form_size = cu->GetAddressByteSize();
            break;
          case DW_FORM_ref_addr:
            if (cu->GetVersion() <= 2)
              form_size = cu->GetAddressByteSize();
            else
              form_size = cu->IsDWARF64() ? 8 : 4;
            break;

          // 0 sized form
          case DW_FORM_flag_present:
            form_size = 0;
            break;

          // 1 byte values
          case DW_FORM_addrx1:
          case DW_FORM_data1:
          case DW_FORM_flag:
          case DW_FORM_ref1:
          case DW_FORM_strx1:
            form_size = 1;
            break;

          // 2 byte values
          case DW_FORM_addrx2:
          case DW_FORM_data2:
          case DW_FORM_ref2:
          case DW_FORM_strx2:
            form_size = 2;
            break;

          // 3 byte values
          case DW_FORM_addrx3:
          case DW_FORM_strx3:
            form_size = 3;
            break;

          // 4 byte values
          case DW_FORM_addrx4:
          case DW_FORM_data4:
          case DW_FORM_ref4:
          case DW_FORM_strx4:
            form_size = 4;
            break;

          // 8 byte values
          case DW_FORM_data8:
          case DW_FORM_ref8:
          case DW_FORM_ref_sig8:
            form_size = 8;
            break;

          // signed or unsigned LEB 128 values
          case DW_FORM_addrx:
          case DW_FORM_rnglistx:
          case DW_FORM_sdata:
          case DW_FORM_udata:
          case DW_FORM_ref_udata:
          case DW_FORM_GNU_addr_index:
          case DW_FORM_GNU_str_index:
          case DW_FORM_strx:
            debug_info_data.Skip_LEB128(&offset);
            break;

          case DW_FORM_indirect:
            form_is_indirect = true;
            form = debug_info_data.GetULEB128(&offset);
            break;

          case DW_FORM_strp:
          case DW_FORM_sec_offset:
            if (cu->IsDWARF64())
              debug_info_data.GetU64(&offset);
            else
              debug_info_data.GetU32(&offset);
            break;

          case DW_FORM_implicit_const:
            form_size = 0;
            break;

          default:
            *offset_ptr = m_offset;
            return false;
          }
          offset += form_size;

        } while (form_is_indirect);
      }
    }
    *offset_ptr = offset;
    return true;
  } else {
    m_tag = 0;
    m_has_children = false;
    return true; // NULL debug tag entry
  }

  return false;
}

//----------------------------------------------------------------------
// Extract
//
// Extract a debug info entry for a given compile unit from the .debug_info and
// .debug_abbrev data within the SymbolFileDWARF class starting at the given
// offset
//----------------------------------------------------------------------
bool DWARFDebugInfoEntry::Extract(SymbolFileDWARF *dwarf2Data,
                                  const DWARFUnit *cu,
                                  lldb::offset_t *offset_ptr) {
  const DWARFDataExtractor &debug_info_data = cu->GetData();
  //    const DWARFDataExtractor& debug_str_data =
  //    dwarf2Data->get_debug_str_data();
  const uint32_t cu_end_offset = cu->GetNextCompileUnitOffset();
  lldb::offset_t offset = *offset_ptr;
  //  if (offset >= cu_end_offset)
  //      Log::Status("DIE at offset 0x%8.8x is beyond the end of the current
  //      compile unit (0x%8.8x)", m_offset, cu_end_offset);
  if ((offset < cu_end_offset) && debug_info_data.ValidOffset(offset)) {
    m_offset = offset;

    const uint64_t abbr_idx = debug_info_data.GetULEB128(&offset);
    lldbassert(abbr_idx <= UINT16_MAX);
    m_abbr_idx = abbr_idx;
    if (abbr_idx) {
      const DWARFAbbreviationDeclaration *abbrevDecl =
          cu->GetAbbreviations()->GetAbbreviationDeclaration(abbr_idx);

      if (abbrevDecl) {
        m_tag = abbrevDecl->Tag();
        m_has_children = abbrevDecl->HasChildren();

        bool isCompileUnitTag = (m_tag == DW_TAG_compile_unit ||
                                 m_tag == DW_TAG_partial_unit);
        if (cu && isCompileUnitTag)
          const_cast<DWARFUnit *>(cu)->SetBaseAddress(0);

        // Skip all data in the .debug_info for the attributes
        const uint32_t numAttributes = abbrevDecl->NumAttributes();
        for (uint32_t i = 0; i < numAttributes; ++i) {
          DWARFFormValue form_value(cu);
          dw_attr_t attr;
          abbrevDecl->GetAttrAndFormValueByIndex(i, attr, form_value);
          dw_form_t form = form_value.Form();

          if (isCompileUnitTag &&
              ((attr == DW_AT_entry_pc) || (attr == DW_AT_low_pc))) {
            if (form_value.ExtractValue(debug_info_data, &offset)) {
              if (attr == DW_AT_low_pc || attr == DW_AT_entry_pc)
                const_cast<DWARFUnit *>(cu)->SetBaseAddress(
                    form_value.Address());
            }
          } else {
            bool form_is_indirect = false;
            do {
              form_is_indirect = false;
              uint32_t form_size = 0;
              switch (form) {
              // Blocks if inlined data that have a length field and the data
              // bytes inlined in the .debug_info
              case DW_FORM_exprloc:
              case DW_FORM_block:
                form_size = debug_info_data.GetULEB128(&offset);
                break;
              case DW_FORM_block1:
                form_size = debug_info_data.GetU8(&offset);
                break;
              case DW_FORM_block2:
                form_size = debug_info_data.GetU16(&offset);
                break;
              case DW_FORM_block4:
                form_size = debug_info_data.GetU32(&offset);
                break;

              // Inlined NULL terminated C-strings
              case DW_FORM_string:
                debug_info_data.GetCStr(&offset);
                break;

              // Compile unit address sized values
              case DW_FORM_addr:
                form_size = cu->GetAddressByteSize();
                break;
              case DW_FORM_ref_addr:
                if (cu->GetVersion() <= 2)
                  form_size = cu->GetAddressByteSize();
                else
                  form_size = cu->IsDWARF64() ? 8 : 4;
                break;

              // 0 sized form
              case DW_FORM_flag_present:
              case DW_FORM_implicit_const:
                form_size = 0;
                break;

              // 1 byte values
              case DW_FORM_data1:
              case DW_FORM_flag:
              case DW_FORM_ref1:
                form_size = 1;
                break;

              // 2 byte values
              case DW_FORM_data2:
              case DW_FORM_ref2:
                form_size = 2;
                break;

              // 4 byte values
              case DW_FORM_data4:
              case DW_FORM_ref4:
                form_size = 4;
                break;

              // 8 byte values
              case DW_FORM_data8:
              case DW_FORM_ref8:
              case DW_FORM_ref_sig8:
                form_size = 8;
                break;

              // signed or unsigned LEB 128 values
              case DW_FORM_sdata:
              case DW_FORM_udata:
              case DW_FORM_ref_udata:
              case DW_FORM_GNU_addr_index:
              case DW_FORM_GNU_str_index:
                debug_info_data.Skip_LEB128(&offset);
                break;

              case DW_FORM_indirect:
                form = debug_info_data.GetULEB128(&offset);
                form_is_indirect = true;
                break;

              case DW_FORM_strp:
              case DW_FORM_sec_offset:
                if (cu->IsDWARF64())
                  debug_info_data.GetU64(&offset);
                else
                  debug_info_data.GetU32(&offset);
                break;

              default:
                *offset_ptr = offset;
                return false;
              }

              offset += form_size;
            } while (form_is_indirect);
          }
        }
        *offset_ptr = offset;
        return true;
      }
    } else {
      m_tag = 0;
      m_has_children = false;
      *offset_ptr = offset;
      return true; // NULL debug tag entry
    }
  }

  return false;
}

//----------------------------------------------------------------------
// DumpAncestry
//
// Dumps all of a debug information entries parents up until oldest and all of
// it's attributes to the specified stream.
//----------------------------------------------------------------------
void DWARFDebugInfoEntry::DumpAncestry(SymbolFileDWARF *dwarf2Data,
                                       const DWARFUnit *cu,
                                       const DWARFDebugInfoEntry *oldest,
                                       Stream &s,
                                       uint32_t recurse_depth) const {
  const DWARFDebugInfoEntry *parent = GetParent();
  if (parent && parent != oldest)
    parent->DumpAncestry(dwarf2Data, cu, oldest, s, 0);
  Dump(dwarf2Data, cu, s, recurse_depth);
}

static dw_offset_t GetRangesOffset(const DWARFDebugRangesBase *debug_ranges,
                                   DWARFFormValue &form_value) {
  if (form_value.Form() == DW_FORM_rnglistx)
    return debug_ranges->GetOffset(form_value.Unsigned());
  return form_value.Unsigned();
}

//----------------------------------------------------------------------
// GetDIENamesAndRanges
//
// Gets the valid address ranges for a given DIE by looking for a
// DW_AT_low_pc/DW_AT_high_pc pair, DW_AT_entry_pc, or DW_AT_ranges attributes.
//----------------------------------------------------------------------
bool DWARFDebugInfoEntry::GetDIENamesAndRanges(
    SymbolFileDWARF *dwarf2Data, const DWARFUnit *cu, const char *&name,
    const char *&mangled, DWARFRangeList &ranges, int &decl_file,
    int &decl_line, int &decl_column, int &call_file, int &call_line,
    int &call_column, DWARFExpression *frame_base) const {
  if (dwarf2Data == nullptr)
    return false;

  SymbolFileDWARFDwo *dwo_symbol_file = cu->GetDwoSymbolFile();
  if (dwo_symbol_file)
    return GetDIENamesAndRanges(
        dwo_symbol_file, dwo_symbol_file->GetCompileUnit(), name, mangled,
        ranges, decl_file, decl_line, decl_column, call_file, call_line,
        call_column, frame_base);

  dw_addr_t lo_pc = LLDB_INVALID_ADDRESS;
  dw_addr_t hi_pc = LLDB_INVALID_ADDRESS;
  std::vector<DIERef> die_refs;
  bool set_frame_base_loclist_addr = false;

  lldb::offset_t offset;
  const DWARFAbbreviationDeclaration *abbrevDecl =
      GetAbbreviationDeclarationPtr(dwarf2Data, cu, offset);

  lldb::ModuleSP module = dwarf2Data->GetObjectFile()->GetModule();

  if (abbrevDecl) {
    const DWARFDataExtractor &debug_info_data = cu->GetData();

    if (!debug_info_data.ValidOffset(offset))
      return false;

    const uint32_t numAttributes = abbrevDecl->NumAttributes();
    bool do_offset = false;

    for (uint32_t i = 0; i < numAttributes; ++i) {
      DWARFFormValue form_value(cu);
      dw_attr_t attr;
      abbrevDecl->GetAttrAndFormValueByIndex(i, attr, form_value);

      if (form_value.ExtractValue(debug_info_data, &offset)) {
        switch (attr) {
        case DW_AT_low_pc:
          lo_pc = form_value.Address();

          if (do_offset)
            hi_pc += lo_pc;
          do_offset = false;
          break;

        case DW_AT_entry_pc:
          lo_pc = form_value.Address();
          break;

        case DW_AT_high_pc:
          if (form_value.Form() == DW_FORM_addr ||
              form_value.Form() == DW_FORM_GNU_addr_index) {
            hi_pc = form_value.Address();
          } else {
            hi_pc = form_value.Unsigned();
            if (lo_pc == LLDB_INVALID_ADDRESS)
              do_offset = hi_pc != LLDB_INVALID_ADDRESS;
            else
              hi_pc += lo_pc; // DWARF 4 introduces <offset-from-lo-pc> to save
                              // on relocations
          }
          break;

        case DW_AT_ranges: {
          const DWARFDebugRangesBase *debug_ranges = dwarf2Data->DebugRanges();
          if (debug_ranges)
            debug_ranges->FindRanges(cu, GetRangesOffset(debug_ranges, form_value), ranges);
          else
            cu->GetSymbolFileDWARF()->GetObjectFile()->GetModule()->ReportError(
                "{0x%8.8x}: DIE has DW_AT_ranges(0x%" PRIx64
                ") attribute yet DWARF has no .debug_ranges, please file a bug "
                "and attach the file at the start of this error message",
                m_offset, form_value.Unsigned());
        } break;

        case DW_AT_name:
          if (name == NULL)
            name = form_value.AsCString();
          break;

        case DW_AT_MIPS_linkage_name:
        case DW_AT_linkage_name:
          if (mangled == NULL)
            mangled = form_value.AsCString();
          break;

        case DW_AT_abstract_origin:
          die_refs.emplace_back(form_value);
          break;

        case DW_AT_specification:
          die_refs.emplace_back(form_value);
          break;

        case DW_AT_decl_file:
          if (decl_file == 0)
            decl_file = form_value.Unsigned();
          break;

        case DW_AT_decl_line:
          if (decl_line == 0)
            decl_line = form_value.Unsigned();
          break;

        case DW_AT_decl_column:
          if (decl_column == 0)
            decl_column = form_value.Unsigned();
          break;

        case DW_AT_call_file:
          if (call_file == 0)
            call_file = form_value.Unsigned();
          break;

        case DW_AT_call_line:
          if (call_line == 0)
            call_line = form_value.Unsigned();
          break;

        case DW_AT_call_column:
          if (call_column == 0)
            call_column = form_value.Unsigned();
          break;

        case DW_AT_frame_base:
          if (frame_base) {
            if (form_value.BlockData()) {
              uint32_t block_offset =
                  form_value.BlockData() - debug_info_data.GetDataStart();
              uint32_t block_length = form_value.Unsigned();
              frame_base->SetOpcodeData(module, debug_info_data, block_offset,
                                        block_length);
            } else {
              const DWARFDataExtractor &debug_loc_data =
                  dwarf2Data->DebugLocData();
              const dw_offset_t debug_loc_offset = form_value.Unsigned();

              size_t loc_list_length = DWARFExpression::LocationListSize(
                  cu, debug_loc_data, debug_loc_offset);
              if (loc_list_length > 0) {
                frame_base->SetOpcodeData(module, debug_loc_data,
                                          debug_loc_offset, loc_list_length);
                if (lo_pc != LLDB_INVALID_ADDRESS) {
                  assert(lo_pc >= cu->GetBaseAddress());
                  frame_base->SetLocationListSlide(lo_pc -
                                                   cu->GetBaseAddress());
                } else {
                  set_frame_base_loclist_addr = true;
                }
              }
            }
          }
          break;

        default:
          break;
        }
      }
    }
  }

  if (ranges.IsEmpty()) {
    if (lo_pc != LLDB_INVALID_ADDRESS) {
      if (hi_pc != LLDB_INVALID_ADDRESS && hi_pc > lo_pc)
        ranges.Append(DWARFRangeList::Entry(lo_pc, hi_pc - lo_pc));
      else
        ranges.Append(DWARFRangeList::Entry(lo_pc, 0));
    }
  }

  if (set_frame_base_loclist_addr) {
    dw_addr_t lowest_range_pc = ranges.GetMinRangeBase(0);
    assert(lowest_range_pc >= cu->GetBaseAddress());
    frame_base->SetLocationListSlide(lowest_range_pc - cu->GetBaseAddress());
  }

  if (ranges.IsEmpty() || name == NULL || mangled == NULL) {
    for (const DIERef &die_ref : die_refs) {
      if (die_ref.die_offset != DW_INVALID_OFFSET) {
        DWARFDIE die = dwarf2Data->GetDIE(die_ref);
        if (die)
          die.GetDIE()->GetDIENamesAndRanges(
              die.GetDWARF(), die.GetCU(), name, mangled, ranges, decl_file,
              decl_line, decl_column, call_file, call_line, call_column);
      }
    }
  }
  return !ranges.IsEmpty();
}

//----------------------------------------------------------------------
// Dump
//
// Dumps a debug information entry and all of it's attributes to the specified
// stream.
//----------------------------------------------------------------------
void DWARFDebugInfoEntry::Dump(SymbolFileDWARF *dwarf2Data,
                               const DWARFUnit *cu, Stream &s,
                               uint32_t recurse_depth) const {
  const DWARFDataExtractor &debug_info_data = cu->GetData();
  lldb::offset_t offset = m_offset;

  if (debug_info_data.ValidOffset(offset)) {
    dw_uleb128_t abbrCode = debug_info_data.GetULEB128(&offset);

    s.Printf("\n0x%8.8x: ", m_offset);
    s.Indent();
    if (abbrCode != m_abbr_idx) {
      s.Printf("error: DWARF has been modified\n");
    } else if (abbrCode) {
      const DWARFAbbreviationDeclaration *abbrevDecl =
          cu->GetAbbreviations()->GetAbbreviationDeclaration(abbrCode);

      if (abbrevDecl) {
        s.PutCString(DW_TAG_value_to_name(abbrevDecl->Tag()));
        s.Printf(" [%u] %c\n", abbrCode, abbrevDecl->HasChildren() ? '*' : ' ');

        // Dump all data in the .debug_info for the attributes
        const uint32_t numAttributes = abbrevDecl->NumAttributes();
        for (uint32_t i = 0; i < numAttributes; ++i) {
          DWARFFormValue form_value(cu);
          dw_attr_t attr;
          abbrevDecl->GetAttrAndFormValueByIndex(i, attr, form_value);

          DumpAttribute(dwarf2Data, cu, debug_info_data, &offset, s, attr,
                        form_value);
        }

        const DWARFDebugInfoEntry *child = GetFirstChild();
        if (recurse_depth > 0 && child) {
          s.IndentMore();

          while (child) {
            child->Dump(dwarf2Data, cu, s, recurse_depth - 1);
            child = child->GetSibling();
          }
          s.IndentLess();
        }
      } else
        s.Printf("Abbreviation code note found in 'debug_abbrev' class for "
                 "code: %u\n",
                 abbrCode);
    } else {
      s.Printf("NULL\n");
    }
  }
}

void DWARFDebugInfoEntry::DumpLocation(SymbolFileDWARF *dwarf2Data,
                                       DWARFUnit *cu, Stream &s) const {
  const DWARFBaseDIE cu_die = cu->GetUnitDIEOnly();
  const char *cu_name = NULL;
  if (cu_die)
    cu_name = cu_die.GetName();
  const char *obj_file_name = NULL;
  ObjectFile *obj_file = dwarf2Data->GetObjectFile();
  if (obj_file)
    obj_file_name =
        obj_file->GetFileSpec().GetFilename().AsCString("<Unknown>");
  const char *die_name = GetName(dwarf2Data, cu);
  s.Printf("0x%8.8x/0x%8.8x: %-30s (from %s in %s)", cu->GetOffset(),
           GetOffset(), die_name ? die_name : "", cu_name ? cu_name : "<NULL>",
           obj_file_name ? obj_file_name : "<NULL>");
}

//----------------------------------------------------------------------
// DumpAttribute
//
// Dumps a debug information entry attribute along with it's form. Any special
// display of attributes is done (disassemble location lists, show enumeration
// values for attributes, etc).
//----------------------------------------------------------------------
void DWARFDebugInfoEntry::DumpAttribute(
    SymbolFileDWARF *dwarf2Data, const DWARFUnit *cu,
    const DWARFDataExtractor &debug_info_data, lldb::offset_t *offset_ptr,
    Stream &s, dw_attr_t attr, DWARFFormValue &form_value) {
  bool show_form = s.GetFlags().Test(DWARFDebugInfo::eDumpFlag_ShowForm);

  s.Printf("            ");
  s.Indent(DW_AT_value_to_name(attr));

  if (show_form) {
    s.Printf("[%s", DW_FORM_value_to_name(form_value.Form()));
  }

  if (!form_value.ExtractValue(debug_info_data, offset_ptr))
    return;

  if (show_form) {
    if (form_value.Form() == DW_FORM_indirect) {
      s.Printf(" [%s]", DW_FORM_value_to_name(form_value.Form()));
    }

    s.PutCString("] ");
  }

  s.PutCString("( ");

  // Check to see if we have any special attribute formatters
  switch (attr) {
  case DW_AT_stmt_list:
    s.Printf("0x%8.8" PRIx64, form_value.Unsigned());
    break;

  case DW_AT_language:
    s.PutCString(DW_LANG_value_to_name(form_value.Unsigned()));
    break;

  case DW_AT_encoding:
    s.PutCString(DW_ATE_value_to_name(form_value.Unsigned()));
    break;

  case DW_AT_frame_base:
  case DW_AT_location:
  case DW_AT_data_member_location: {
    const uint8_t *blockData = form_value.BlockData();
    if (blockData) {
      // Location description is inlined in data in the form value
      DWARFDataExtractor locationData(debug_info_data,
                                      (*offset_ptr) - form_value.Unsigned(),
                                      form_value.Unsigned());
      DWARFExpression::PrintDWARFExpression(
          s, locationData, DWARFUnit::GetAddressByteSize(cu), 4, false);
    } else {
      // We have a location list offset as the value that is the offset into
      // the .debug_loc section that describes the value over it's lifetime
      uint64_t debug_loc_offset = form_value.Unsigned();
      if (dwarf2Data) {
        DWARFExpression::PrintDWARFLocationList(
            s, cu, dwarf2Data->DebugLocData(), debug_loc_offset);
      }
    }
  } break;

  case DW_AT_abstract_origin:
  case DW_AT_specification: {
    uint64_t abstract_die_offset = form_value.Reference();
    form_value.Dump(s);
    //  *ostrm_ptr << HEX32 << abstract_die_offset << " ( ";
    GetName(dwarf2Data, cu, abstract_die_offset, s);
  } break;

  case DW_AT_type: {
    uint64_t type_die_offset = form_value.Reference();
    s.PutCString(" ( ");
    AppendTypeName(dwarf2Data, cu, type_die_offset, s);
    s.PutCString(" )");
  } break;

  case DW_AT_ranges: {
    if (!dwarf2Data)
      break;
    lldb::offset_t ranges_offset =
        GetRangesOffset(dwarf2Data->DebugRanges(), form_value);
    dw_addr_t base_addr = cu ? cu->GetBaseAddress() : 0;
    DWARFDebugRanges::Dump(s, dwarf2Data->get_debug_ranges_data(),
                           &ranges_offset, base_addr);
  } break;

  default:
    break;
  }

  s.PutCString(" )\n");
}

//----------------------------------------------------------------------
// Get all attribute values for a given DIE, including following any
// specification or abstract origin attributes and including those in the
// results. Any duplicate attributes will have the first instance take
// precedence (this can happen for declaration attributes).
//----------------------------------------------------------------------
size_t DWARFDebugInfoEntry::GetAttributes(
    const DWARFUnit *cu, DWARFFormValue::FixedFormSizes fixed_form_sizes,
    DWARFAttributes &attributes, uint32_t curr_depth) const {
  SymbolFileDWARF *dwarf2Data = nullptr;
  const DWARFAbbreviationDeclaration *abbrevDecl = nullptr;
  lldb::offset_t offset = 0;
  if (cu) {
    if (m_tag != DW_TAG_compile_unit && m_tag != DW_TAG_partial_unit) {
      SymbolFileDWARFDwo *dwo_symbol_file = cu->GetDwoSymbolFile();
      if (dwo_symbol_file)
        return GetAttributes(dwo_symbol_file->GetCompileUnit(),
                             fixed_form_sizes, attributes, curr_depth);
    }

    dwarf2Data = cu->GetSymbolFileDWARF();
    abbrevDecl = GetAbbreviationDeclarationPtr(dwarf2Data, cu, offset);
  }

  if (abbrevDecl) {
    const DWARFDataExtractor &debug_info_data = cu->GetData();

    if (fixed_form_sizes.Empty())
      fixed_form_sizes = DWARFFormValue::GetFixedFormSizesForAddressSize(
          cu->GetAddressByteSize(), cu->IsDWARF64());

    const uint32_t num_attributes = abbrevDecl->NumAttributes();
    for (uint32_t i = 0; i < num_attributes; ++i) {
      DWARFFormValue form_value(cu);
      dw_attr_t attr;
      abbrevDecl->GetAttrAndFormValueByIndex(i, attr, form_value);
      const dw_form_t form = form_value.Form();

      // If we are tracking down DW_AT_specification or DW_AT_abstract_origin
      // attributes, the depth will be non-zero. We need to omit certain
      // attributes that don't make sense.
      switch (attr) {
      case DW_AT_sibling:
      case DW_AT_declaration:
        if (curr_depth > 0) {
          // This attribute doesn't make sense when combined with the DIE that
          // references this DIE. We know a DIE is referencing this DIE because
          // curr_depth is not zero
          break;
        }
        LLVM_FALLTHROUGH;
      default:
        attributes.Append(cu, offset, attr, form);
        break;
      }

      if ((attr == DW_AT_specification) || (attr == DW_AT_abstract_origin)) {
        if (form_value.ExtractValue(debug_info_data, &offset)) {
          dw_offset_t die_offset = form_value.Reference();
          DWARFDIE spec_die =
              const_cast<DWARFUnit *>(cu)->GetDIE(die_offset);
          if (spec_die)
            spec_die.GetAttributes(attributes, curr_depth + 1);
        }
      } else {
        const uint8_t fixed_skip_size = fixed_form_sizes.GetSize(form);
        if (fixed_skip_size)
          offset += fixed_skip_size;
        else
          DWARFFormValue::SkipValue(form, debug_info_data, &offset, cu);
      }
    }
  } else {
    attributes.Clear();
  }
  return attributes.Size();
}

//----------------------------------------------------------------------
// GetAttributeValue
//
// Get the value of an attribute and return the .debug_info offset of the
// attribute if it was properly extracted into form_value, or zero if we fail
// since an offset of zero is invalid for an attribute (it would be a compile
// unit header).
//----------------------------------------------------------------------
dw_offset_t DWARFDebugInfoEntry::GetAttributeValue(
    SymbolFileDWARF *dwarf2Data, const DWARFUnit *cu,
    const dw_attr_t attr, DWARFFormValue &form_value,
    dw_offset_t *end_attr_offset_ptr,
    bool check_specification_or_abstract_origin) const {
  SymbolFileDWARFDwo *dwo_symbol_file = cu->GetDwoSymbolFile();
  if (dwo_symbol_file && m_tag != DW_TAG_compile_unit &&
                         m_tag != DW_TAG_partial_unit)
    return GetAttributeValue(dwo_symbol_file, dwo_symbol_file->GetCompileUnit(),
                             attr, form_value, end_attr_offset_ptr,
                             check_specification_or_abstract_origin);

  lldb::offset_t offset;
  const DWARFAbbreviationDeclaration *abbrevDecl =
      GetAbbreviationDeclarationPtr(dwarf2Data, cu, offset);

  if (abbrevDecl) {
    uint32_t attr_idx = abbrevDecl->FindAttributeIndex(attr);

    if (attr_idx != DW_INVALID_INDEX) {
      const DWARFDataExtractor &debug_info_data = cu->GetData();

      uint32_t idx = 0;
      while (idx < attr_idx)
        DWARFFormValue::SkipValue(abbrevDecl->GetFormByIndex(idx++),
                                  debug_info_data, &offset, cu);

      const dw_offset_t attr_offset = offset;
      form_value.SetCompileUnit(cu);
      form_value.SetForm(abbrevDecl->GetFormByIndex(idx));
      if (form_value.ExtractValue(debug_info_data, &offset)) {
        if (end_attr_offset_ptr)
          *end_attr_offset_ptr = offset;
        return attr_offset;
      }
    }
  }

  if (check_specification_or_abstract_origin) {
    if (GetAttributeValue(dwarf2Data, cu, DW_AT_specification, form_value)) {
      DWARFDIE die =
          const_cast<DWARFUnit *>(cu)->GetDIE(form_value.Reference());
      if (die) {
        dw_offset_t die_offset = die.GetDIE()->GetAttributeValue(
            die.GetDWARF(), die.GetCU(), attr, form_value, end_attr_offset_ptr,
            false);
        if (die_offset)
          return die_offset;
      }
    }

    if (GetAttributeValue(dwarf2Data, cu, DW_AT_abstract_origin, form_value)) {
      DWARFDIE die =
          const_cast<DWARFUnit *>(cu)->GetDIE(form_value.Reference());
      if (die) {
        dw_offset_t die_offset = die.GetDIE()->GetAttributeValue(
            die.GetDWARF(), die.GetCU(), attr, form_value, end_attr_offset_ptr,
            false);
        if (die_offset)
          return die_offset;
      }
    }
  }

  if (!dwo_symbol_file)
    return 0;

  DWARFUnit *dwo_cu = dwo_symbol_file->GetCompileUnit();
  if (!dwo_cu)
    return 0;

  DWARFBaseDIE dwo_cu_die = dwo_cu->GetUnitDIEOnly();
  if (!dwo_cu_die.IsValid())
    return 0;

  return dwo_cu_die.GetDIE()->GetAttributeValue(
      dwo_symbol_file, dwo_cu, attr, form_value, end_attr_offset_ptr,
      check_specification_or_abstract_origin);
}

//----------------------------------------------------------------------
// GetAttributeValueAsString
//
// Get the value of an attribute as a string return it. The resulting pointer
// to the string data exists within the supplied SymbolFileDWARF and will only
// be available as long as the SymbolFileDWARF is still around and it's content
// doesn't change.
//----------------------------------------------------------------------
const char *DWARFDebugInfoEntry::GetAttributeValueAsString(
    SymbolFileDWARF *dwarf2Data, const DWARFUnit *cu,
    const dw_attr_t attr, const char *fail_value,
    bool check_specification_or_abstract_origin) const {
  DWARFFormValue form_value;
  if (GetAttributeValue(dwarf2Data, cu, attr, form_value, nullptr,
                        check_specification_or_abstract_origin))
    return form_value.AsCString();
  return fail_value;
}

//----------------------------------------------------------------------
// GetAttributeValueAsUnsigned
//
// Get the value of an attribute as unsigned and return it.
//----------------------------------------------------------------------
uint64_t DWARFDebugInfoEntry::GetAttributeValueAsUnsigned(
    SymbolFileDWARF *dwarf2Data, const DWARFUnit *cu,
    const dw_attr_t attr, uint64_t fail_value,
    bool check_specification_or_abstract_origin) const {
  DWARFFormValue form_value;
  if (GetAttributeValue(dwarf2Data, cu, attr, form_value, nullptr,
                        check_specification_or_abstract_origin))
    return form_value.Unsigned();
  return fail_value;
}

//----------------------------------------------------------------------
// GetAttributeValueAsSigned
//
// Get the value of an attribute a signed value and return it.
//----------------------------------------------------------------------
int64_t DWARFDebugInfoEntry::GetAttributeValueAsSigned(
    SymbolFileDWARF *dwarf2Data, const DWARFUnit *cu,
    const dw_attr_t attr, int64_t fail_value,
    bool check_specification_or_abstract_origin) const {
  DWARFFormValue form_value;
  if (GetAttributeValue(dwarf2Data, cu, attr, form_value, nullptr,
                        check_specification_or_abstract_origin))
    return form_value.Signed();
  return fail_value;
}

//----------------------------------------------------------------------
// GetAttributeValueAsReference
//
// Get the value of an attribute as reference and fix up and compile unit
// relative offsets as needed.
//----------------------------------------------------------------------
uint64_t DWARFDebugInfoEntry::GetAttributeValueAsReference(
    SymbolFileDWARF *dwarf2Data, const DWARFUnit *cu,
    const dw_attr_t attr, uint64_t fail_value,
    bool check_specification_or_abstract_origin) const {
  DWARFFormValue form_value;
  if (GetAttributeValue(dwarf2Data, cu, attr, form_value, nullptr,
                        check_specification_or_abstract_origin))
    return form_value.Reference();
  return fail_value;
}

uint64_t DWARFDebugInfoEntry::GetAttributeValueAsAddress(
    SymbolFileDWARF *dwarf2Data, const DWARFUnit *cu,
    const dw_attr_t attr, uint64_t fail_value,
    bool check_specification_or_abstract_origin) const {
  DWARFFormValue form_value;
  if (GetAttributeValue(dwarf2Data, cu, attr, form_value, nullptr,
                        check_specification_or_abstract_origin))
    return form_value.Address();
  return fail_value;
}

//----------------------------------------------------------------------
// GetAttributeHighPC
//
// Get the hi_pc, adding hi_pc to lo_pc when specified as an <offset-from-low-
// pc>.
//
// Returns the hi_pc or fail_value.
//----------------------------------------------------------------------
dw_addr_t DWARFDebugInfoEntry::GetAttributeHighPC(
    SymbolFileDWARF *dwarf2Data, const DWARFUnit *cu, dw_addr_t lo_pc,
    uint64_t fail_value, bool check_specification_or_abstract_origin) const {
  DWARFFormValue form_value;
  if (GetAttributeValue(dwarf2Data, cu, DW_AT_high_pc, form_value, nullptr,
                        check_specification_or_abstract_origin)) {
    dw_form_t form = form_value.Form();
    if (form == DW_FORM_addr || form == DW_FORM_GNU_addr_index)
      return form_value.Address();

    // DWARF4 can specify the hi_pc as an <offset-from-lowpc>
    return lo_pc + form_value.Unsigned();
  }
  return fail_value;
}

//----------------------------------------------------------------------
// GetAttributeAddressRange
//
// Get the lo_pc and hi_pc, adding hi_pc to lo_pc when specified as an <offset-
// from-low-pc>.
//
// Returns true or sets lo_pc and hi_pc to fail_value.
//----------------------------------------------------------------------
bool DWARFDebugInfoEntry::GetAttributeAddressRange(
    SymbolFileDWARF *dwarf2Data, const DWARFUnit *cu, dw_addr_t &lo_pc,
    dw_addr_t &hi_pc, uint64_t fail_value,
    bool check_specification_or_abstract_origin) const {
  lo_pc = GetAttributeValueAsAddress(dwarf2Data, cu, DW_AT_low_pc, fail_value,
                                     check_specification_or_abstract_origin);
  if (lo_pc != fail_value) {
    hi_pc = GetAttributeHighPC(dwarf2Data, cu, lo_pc, fail_value,
                               check_specification_or_abstract_origin);
    if (hi_pc != fail_value)
      return true;
  }
  lo_pc = fail_value;
  hi_pc = fail_value;
  return false;
}

size_t DWARFDebugInfoEntry::GetAttributeAddressRanges(
    SymbolFileDWARF *dwarf2Data, const DWARFUnit *cu,
    DWARFRangeList &ranges, bool check_hi_lo_pc,
    bool check_specification_or_abstract_origin) const {
  ranges.Clear();

  DWARFFormValue form_value;
  if (GetAttributeValue(dwarf2Data, cu, DW_AT_ranges, form_value)) {
    if (DWARFDebugRangesBase *debug_ranges = dwarf2Data->DebugRanges())
      debug_ranges->FindRanges(cu, GetRangesOffset(debug_ranges, form_value),
                               ranges);
  } else if (check_hi_lo_pc) {
    dw_addr_t lo_pc = LLDB_INVALID_ADDRESS;
    dw_addr_t hi_pc = LLDB_INVALID_ADDRESS;
    if (GetAttributeAddressRange(dwarf2Data, cu, lo_pc, hi_pc,
                                 LLDB_INVALID_ADDRESS,
                                 check_specification_or_abstract_origin)) {
      if (lo_pc < hi_pc)
        ranges.Append(DWARFRangeList::Entry(lo_pc, hi_pc - lo_pc));
    }
  }
  return ranges.GetSize();
}

//----------------------------------------------------------------------
// GetName
//
// Get value of the DW_AT_name attribute and return it if one exists, else
// return NULL.
//----------------------------------------------------------------------
const char *DWARFDebugInfoEntry::GetName(SymbolFileDWARF *dwarf2Data,
                                         const DWARFUnit *cu) const {
  return GetAttributeValueAsString(dwarf2Data, cu, DW_AT_name, nullptr, true);
}

//----------------------------------------------------------------------
// GetMangledName
//
// Get value of the DW_AT_MIPS_linkage_name attribute and return it if one
// exists, else return the value of the DW_AT_name attribute
//----------------------------------------------------------------------
const char *
DWARFDebugInfoEntry::GetMangledName(SymbolFileDWARF *dwarf2Data,
                                    const DWARFUnit *cu,
                                    bool substitute_name_allowed) const {
  const char *name = nullptr;

  name = GetAttributeValueAsString(dwarf2Data, cu, DW_AT_MIPS_linkage_name,
                                   nullptr, true);
  if (name)
    return name;

  name = GetAttributeValueAsString(dwarf2Data, cu, DW_AT_linkage_name, nullptr,
                                   true);
  if (name)
    return name;

  if (!substitute_name_allowed)
    return nullptr;

  name = GetAttributeValueAsString(dwarf2Data, cu, DW_AT_name, nullptr, true);
  return name;
}

//----------------------------------------------------------------------
// GetPubname
//
// Get value the name for a DIE as it should appear for a .debug_pubnames or
// .debug_pubtypes section.
//----------------------------------------------------------------------
const char *DWARFDebugInfoEntry::GetPubname(SymbolFileDWARF *dwarf2Data,
                                            const DWARFUnit *cu) const {
  const char *name = nullptr;
  if (!dwarf2Data)
    return name;

  name = GetAttributeValueAsString(dwarf2Data, cu, DW_AT_MIPS_linkage_name,
                                   nullptr, true);
  if (name)
    return name;

  name = GetAttributeValueAsString(dwarf2Data, cu, DW_AT_linkage_name, nullptr,
                                   true);
  if (name)
    return name;

  name = GetAttributeValueAsString(dwarf2Data, cu, DW_AT_name, nullptr, true);
  return name;
}

//----------------------------------------------------------------------
// GetName
//
// Get value of the DW_AT_name attribute for a debug information entry that
// exists at offset "die_offset" and place that value into the supplied stream
// object. If the DIE is a NULL object "NULL" is placed into the stream, and if
// no DW_AT_name attribute exists for the DIE then nothing is printed.
//----------------------------------------------------------------------
bool DWARFDebugInfoEntry::GetName(SymbolFileDWARF *dwarf2Data,
                                  const DWARFUnit *cu,
                                  const dw_offset_t die_offset, Stream &s) {
  if (dwarf2Data == NULL) {
    s.PutCString("NULL");
    return false;
  }

  DWARFDebugInfoEntry die;
  lldb::offset_t offset = die_offset;
  if (die.Extract(dwarf2Data, cu, &offset)) {
    if (die.IsNULL()) {
      s.PutCString("NULL");
      return true;
    } else {
      const char *name = die.GetAttributeValueAsString(
          dwarf2Data, cu, DW_AT_name, nullptr, true);
      if (name) {
        s.PutCString(name);
        return true;
      }
    }
  }
  return false;
}

//----------------------------------------------------------------------
// AppendTypeName
//
// Follows the type name definition down through all needed tags to end up with
// a fully qualified type name and dump the results to the supplied stream.
// This is used to show the name of types given a type identifier.
//----------------------------------------------------------------------
bool DWARFDebugInfoEntry::AppendTypeName(SymbolFileDWARF *dwarf2Data,
                                         const DWARFUnit *cu,
                                         const dw_offset_t die_offset,
                                         Stream &s) {
  if (dwarf2Data == NULL) {
    s.PutCString("NULL");
    return false;
  }

  DWARFDebugInfoEntry die;
  lldb::offset_t offset = die_offset;
  if (die.Extract(dwarf2Data, cu, &offset)) {
    if (die.IsNULL()) {
      s.PutCString("NULL");
      return true;
    } else {
      const char *name = die.GetPubname(dwarf2Data, cu);
      if (name)
        s.PutCString(name);
      else {
        bool result = true;
        const DWARFAbbreviationDeclaration *abbrevDecl =
            die.GetAbbreviationDeclarationPtr(dwarf2Data, cu, offset);

        if (abbrevDecl == NULL)
          return false;

        switch (abbrevDecl->Tag()) {
        case DW_TAG_array_type:
          break; // print out a "[]" after printing the full type of the element
                 // below
        case DW_TAG_base_type:
          s.PutCString("base ");
          break;
        case DW_TAG_class_type:
          s.PutCString("class ");
          break;
        case DW_TAG_const_type:
          s.PutCString("const ");
          break;
        case DW_TAG_enumeration_type:
          s.PutCString("enum ");
          break;
        case DW_TAG_file_type:
          s.PutCString("file ");
          break;
        case DW_TAG_interface_type:
          s.PutCString("interface ");
          break;
        case DW_TAG_packed_type:
          s.PutCString("packed ");
          break;
        case DW_TAG_pointer_type:
          break; // print out a '*' after printing the full type below
        case DW_TAG_ptr_to_member_type:
          break; // print out a '*' after printing the full type below
        case DW_TAG_reference_type:
          break; // print out a '&' after printing the full type below
        case DW_TAG_restrict_type:
          s.PutCString("restrict ");
          break;
        case DW_TAG_set_type:
          s.PutCString("set ");
          break;
        case DW_TAG_shared_type:
          s.PutCString("shared ");
          break;
        case DW_TAG_string_type:
          s.PutCString("string ");
          break;
        case DW_TAG_structure_type:
          s.PutCString("struct ");
          break;
        case DW_TAG_subrange_type:
          s.PutCString("subrange ");
          break;
        case DW_TAG_subroutine_type:
          s.PutCString("function ");
          break;
        case DW_TAG_thrown_type:
          s.PutCString("thrown ");
          break;
        case DW_TAG_union_type:
          s.PutCString("union ");
          break;
        case DW_TAG_unspecified_type:
          s.PutCString("unspecified ");
          break;
        case DW_TAG_volatile_type:
          s.PutCString("volatile ");
          break;
        default:
          return false;
        }

        // Follow the DW_AT_type if possible
        DWARFFormValue form_value;
        if (die.GetAttributeValue(dwarf2Data, cu, DW_AT_type, form_value)) {
          uint64_t next_die_offset = form_value.Reference();
          result = AppendTypeName(dwarf2Data, cu, next_die_offset, s);
        }

        switch (abbrevDecl->Tag()) {
        case DW_TAG_array_type:
          s.PutCString("[]");
          break;
        case DW_TAG_pointer_type:
          s.PutChar('*');
          break;
        case DW_TAG_ptr_to_member_type:
          s.PutChar('*');
          break;
        case DW_TAG_reference_type:
          s.PutChar('&');
          break;
        default:
          break;
        }
        return result;
      }
    }
  }
  return false;
}

//----------------------------------------------------------------------
// BuildAddressRangeTable
//----------------------------------------------------------------------
void DWARFDebugInfoEntry::BuildAddressRangeTable(
    SymbolFileDWARF *dwarf2Data, const DWARFUnit *cu,
    DWARFDebugAranges *debug_aranges) const {
  if (m_tag) {
    if (m_tag == DW_TAG_subprogram) {
      dw_addr_t lo_pc = LLDB_INVALID_ADDRESS;
      dw_addr_t hi_pc = LLDB_INVALID_ADDRESS;
      if (GetAttributeAddressRange(dwarf2Data, cu, lo_pc, hi_pc,
                                   LLDB_INVALID_ADDRESS)) {
        /// printf("BuildAddressRangeTable() 0x%8.8x: %30s: [0x%8.8x -
        /// 0x%8.8x)\n", m_offset, DW_TAG_value_to_name(tag), lo_pc, hi_pc);
        debug_aranges->AppendRange(cu->GetOffset(), lo_pc, hi_pc);
      }
    }

    const DWARFDebugInfoEntry *child = GetFirstChild();
    while (child) {
      child->BuildAddressRangeTable(dwarf2Data, cu, debug_aranges);
      child = child->GetSibling();
    }
  }
}

//----------------------------------------------------------------------
// BuildFunctionAddressRangeTable
//
// This function is very similar to the BuildAddressRangeTable function except
// that the actual DIE offset for the function is placed in the table instead
// of the compile unit offset (which is the way the standard .debug_aranges
// section does it).
//----------------------------------------------------------------------
void DWARFDebugInfoEntry::BuildFunctionAddressRangeTable(
    SymbolFileDWARF *dwarf2Data, const DWARFUnit *cu,
    DWARFDebugAranges *debug_aranges) const {
  if (m_tag) {
    if (m_tag == DW_TAG_subprogram) {
      dw_addr_t lo_pc = LLDB_INVALID_ADDRESS;
      dw_addr_t hi_pc = LLDB_INVALID_ADDRESS;
      if (GetAttributeAddressRange(dwarf2Data, cu, lo_pc, hi_pc,
                                   LLDB_INVALID_ADDRESS)) {
        //  printf("BuildAddressRangeTable() 0x%8.8x: [0x%16.16" PRIx64 " -
        //  0x%16.16" PRIx64 ")\n", m_offset, lo_pc, hi_pc); // DEBUG ONLY
        debug_aranges->AppendRange(GetOffset(), lo_pc, hi_pc);
      }
    }

    const DWARFDebugInfoEntry *child = GetFirstChild();
    while (child) {
      child->BuildFunctionAddressRangeTable(dwarf2Data, cu, debug_aranges);
      child = child->GetSibling();
    }
  }
}

void DWARFDebugInfoEntry::GetDeclContextDIEs(
    DWARFUnit *cu, DWARFDIECollection &decl_context_dies) const {

  DWARFDIE die(cu, const_cast<DWARFDebugInfoEntry *>(this));
  die.GetDeclContextDIEs(decl_context_dies);
}

void DWARFDebugInfoEntry::GetDWARFDeclContext(
    SymbolFileDWARF *dwarf2Data, DWARFUnit *cu,
    DWARFDeclContext &dwarf_decl_ctx) const {
  const dw_tag_t tag = Tag();
  if (tag != DW_TAG_compile_unit && tag != DW_TAG_partial_unit) {
    dwarf_decl_ctx.AppendDeclContext(tag, GetName(dwarf2Data, cu));
    DWARFDIE parent_decl_ctx_die = GetParentDeclContextDIE(dwarf2Data, cu);
    if (parent_decl_ctx_die && parent_decl_ctx_die.GetDIE() != this) {
      if (parent_decl_ctx_die.Tag() != DW_TAG_compile_unit &&
          parent_decl_ctx_die.Tag() != DW_TAG_partial_unit)
        parent_decl_ctx_die.GetDIE()->GetDWARFDeclContext(
            parent_decl_ctx_die.GetDWARF(), parent_decl_ctx_die.GetCU(),
            dwarf_decl_ctx);
    }
  }
}

bool DWARFDebugInfoEntry::MatchesDWARFDeclContext(
    SymbolFileDWARF *dwarf2Data, DWARFUnit *cu,
    const DWARFDeclContext &dwarf_decl_ctx) const {

  DWARFDeclContext this_dwarf_decl_ctx;
  GetDWARFDeclContext(dwarf2Data, cu, this_dwarf_decl_ctx);
  return this_dwarf_decl_ctx == dwarf_decl_ctx;
}

DWARFDIE
DWARFDebugInfoEntry::GetParentDeclContextDIE(SymbolFileDWARF *dwarf2Data,
                                             DWARFUnit *cu) const {
  DWARFAttributes attributes;
  GetAttributes(cu, DWARFFormValue::FixedFormSizes(), attributes);
  return GetParentDeclContextDIE(dwarf2Data, cu, attributes);
}

DWARFDIE
DWARFDebugInfoEntry::GetParentDeclContextDIE(
    SymbolFileDWARF *dwarf2Data, DWARFUnit *cu,
    const DWARFAttributes &attributes) const {
  DWARFDIE die(cu, const_cast<DWARFDebugInfoEntry *>(this));

  while (die) {
    // If this is the original DIE that we are searching for a declaration for,
    // then don't look in the cache as we don't want our own decl context to be
    // our decl context...
    if (die.GetDIE() != this) {
      switch (die.Tag()) {
      case DW_TAG_compile_unit:
      case DW_TAG_partial_unit:
      case DW_TAG_namespace:
      case DW_TAG_structure_type:
      case DW_TAG_union_type:
      case DW_TAG_class_type:
        return die;

      default:
        break;
      }
    }

    dw_offset_t die_offset;

    die_offset =
        attributes.FormValueAsUnsigned(DW_AT_specification, DW_INVALID_OFFSET);
    if (die_offset != DW_INVALID_OFFSET) {
      DWARFDIE spec_die = cu->GetDIE(die_offset);
      if (spec_die) {
        DWARFDIE decl_ctx_die = spec_die.GetParentDeclContextDIE();
        if (decl_ctx_die)
          return decl_ctx_die;
      }
    }

    die_offset = attributes.FormValueAsUnsigned(DW_AT_abstract_origin,
                                                DW_INVALID_OFFSET);
    if (die_offset != DW_INVALID_OFFSET) {
      DWARFDIE abs_die = cu->GetDIE(die_offset);
      if (abs_die) {
        DWARFDIE decl_ctx_die = abs_die.GetParentDeclContextDIE();
        if (decl_ctx_die)
          return decl_ctx_die;
      }
    }

    die = die.GetParent();
  }
  return DWARFDIE();
}

const char *DWARFDebugInfoEntry::GetQualifiedName(SymbolFileDWARF *dwarf2Data,
                                                  DWARFUnit *cu,
                                                  std::string &storage) const {
  DWARFAttributes attributes;
  GetAttributes(cu, DWARFFormValue::FixedFormSizes(), attributes);
  return GetQualifiedName(dwarf2Data, cu, attributes, storage);
}

const char *DWARFDebugInfoEntry::GetQualifiedName(
    SymbolFileDWARF *dwarf2Data, DWARFUnit *cu,
    const DWARFAttributes &attributes, std::string &storage) const {

  const char *name = GetName(dwarf2Data, cu);

  if (name) {
    DWARFDIE parent_decl_ctx_die = GetParentDeclContextDIE(dwarf2Data, cu);
    storage.clear();
    // TODO: change this to get the correct decl context parent....
    while (parent_decl_ctx_die) {
      const dw_tag_t parent_tag = parent_decl_ctx_die.Tag();
      switch (parent_tag) {
      case DW_TAG_namespace: {
        const char *namespace_name = parent_decl_ctx_die.GetName();
        if (namespace_name) {
          storage.insert(0, "::");
          storage.insert(0, namespace_name);
        } else {
          storage.insert(0, "(anonymous namespace)::");
        }
        parent_decl_ctx_die = parent_decl_ctx_die.GetParentDeclContextDIE();
      } break;

      case DW_TAG_class_type:
      case DW_TAG_structure_type:
      case DW_TAG_union_type: {
        const char *class_union_struct_name = parent_decl_ctx_die.GetName();

        if (class_union_struct_name) {
          storage.insert(0, "::");
          storage.insert(0, class_union_struct_name);
        }
        parent_decl_ctx_die = parent_decl_ctx_die.GetParentDeclContextDIE();
      } break;

      default:
        parent_decl_ctx_die.Clear();
        break;
      }
    }

    if (storage.empty())
      storage.append("::");

    storage.append(name);
  }
  if (storage.empty())
    return NULL;
  return storage.c_str();
}

//----------------------------------------------------------------------
// LookupAddress
//----------------------------------------------------------------------
bool DWARFDebugInfoEntry::LookupAddress(const dw_addr_t address,
                                        SymbolFileDWARF *dwarf2Data,
                                        const DWARFUnit *cu,
                                        DWARFDebugInfoEntry **function_die,
                                        DWARFDebugInfoEntry **block_die) {
  bool found_address = false;
  if (m_tag) {
    bool check_children = false;
    bool match_addr_range = false;
    //  printf("0x%8.8x: %30s: address = 0x%8.8x - ", m_offset,
    //  DW_TAG_value_to_name(tag), address);
    switch (m_tag) {
    case DW_TAG_array_type:
      break;
    case DW_TAG_class_type:
      check_children = true;
      break;
    case DW_TAG_entry_point:
      break;
    case DW_TAG_enumeration_type:
      break;
    case DW_TAG_formal_parameter:
      break;
    case DW_TAG_imported_declaration:
      break;
    case DW_TAG_label:
      break;
    case DW_TAG_lexical_block:
      check_children = true;
      match_addr_range = true;
      break;
    case DW_TAG_member:
      break;
    case DW_TAG_pointer_type:
      break;
    case DW_TAG_reference_type:
      break;
    case DW_TAG_compile_unit:
      match_addr_range = true;
      break;
    case DW_TAG_string_type:
      break;
    case DW_TAG_structure_type:
      check_children = true;
      break;
    case DW_TAG_subroutine_type:
      break;
    case DW_TAG_typedef:
      break;
    case DW_TAG_union_type:
      break;
    case DW_TAG_unspecified_parameters:
      break;
    case DW_TAG_variant:
      break;
    case DW_TAG_common_block:
      check_children = true;
      break;
    case DW_TAG_common_inclusion:
      break;
    case DW_TAG_inheritance:
      break;
    case DW_TAG_inlined_subroutine:
      check_children = true;
      match_addr_range = true;
      break;
    case DW_TAG_module:
      match_addr_range = true;
      break;
    case DW_TAG_ptr_to_member_type:
      break;
    case DW_TAG_set_type:
      break;
    case DW_TAG_subrange_type:
      break;
    case DW_TAG_with_stmt:
      break;
    case DW_TAG_access_declaration:
      break;
    case DW_TAG_base_type:
      break;
    case DW_TAG_catch_block:
      match_addr_range = true;
      break;
    case DW_TAG_const_type:
      break;
    case DW_TAG_constant:
      break;
    case DW_TAG_enumerator:
      break;
    case DW_TAG_file_type:
      break;
    case DW_TAG_friend:
      break;
    case DW_TAG_namelist:
      break;
    case DW_TAG_namelist_item:
      break;
    case DW_TAG_packed_type:
      break;
    case DW_TAG_subprogram:
      match_addr_range = true;
      break;
    case DW_TAG_template_type_parameter:
      break;
    case DW_TAG_template_value_parameter:
      break;
    case DW_TAG_GNU_template_parameter_pack:
      break;
    case DW_TAG_thrown_type:
      break;
    case DW_TAG_try_block:
      match_addr_range = true;
      break;
    case DW_TAG_variant_part:
      break;
    case DW_TAG_variable:
      break;
    case DW_TAG_volatile_type:
      break;
    case DW_TAG_dwarf_procedure:
      break;
    case DW_TAG_restrict_type:
      break;
    case DW_TAG_interface_type:
      break;
    case DW_TAG_namespace:
      check_children = true;
      break;
    case DW_TAG_imported_module:
      break;
    case DW_TAG_unspecified_type:
      break;
    case DW_TAG_partial_unit:
      match_addr_range = true;
      break;
    case DW_TAG_imported_unit:
      break;
    case DW_TAG_shared_type:
      break;
    default:
      break;
    }

    if (match_addr_range) {
      dw_addr_t lo_pc = GetAttributeValueAsAddress(dwarf2Data, cu, DW_AT_low_pc,
                                                   LLDB_INVALID_ADDRESS);
      if (lo_pc != LLDB_INVALID_ADDRESS) {
        dw_addr_t hi_pc =
            GetAttributeHighPC(dwarf2Data, cu, lo_pc, LLDB_INVALID_ADDRESS);
        if (hi_pc != LLDB_INVALID_ADDRESS) {
          //  printf("\n0x%8.8x: %30s: address = 0x%8.8x  [0x%8.8x - 0x%8.8x) ",
          //  m_offset, DW_TAG_value_to_name(tag), address, lo_pc, hi_pc);
          if ((lo_pc <= address) && (address < hi_pc)) {
            found_address = true;
            //  puts("***MATCH***");
            switch (m_tag) {
            case DW_TAG_compile_unit: // File
            case DW_TAG_partial_unit: // File
              check_children = ((function_die != NULL) || (block_die != NULL));
              break;

            case DW_TAG_subprogram: // Function
              if (function_die)
                *function_die = this;
              check_children = (block_die != NULL);
              break;

            case DW_TAG_inlined_subroutine: // Inlined Function
            case DW_TAG_lexical_block:      // Block { } in code
              if (block_die) {
                *block_die = this;
                check_children = true;
              }
              break;

            default:
              check_children = true;
              break;
            }
          }
        } else {
          // Compile units may not have a valid high/low pc when there
          // are address gaps in subroutines so we must always search
          // if there is no valid high and low PC.
          check_children = (m_tag == DW_TAG_compile_unit ||
                            m_tag == DW_TAG_partial_unit) &&
                           ((function_die != NULL) || (block_die != NULL));
        }
      } else {
        DWARFFormValue form_value;
        if (GetAttributeValue(dwarf2Data, cu, DW_AT_ranges, form_value)) {
          DWARFRangeList ranges;
          DWARFDebugRangesBase *debug_ranges = dwarf2Data->DebugRanges();
          debug_ranges->FindRanges(
              cu, GetRangesOffset(debug_ranges, form_value), ranges);

          if (ranges.FindEntryThatContains(address)) {
            found_address = true;
            //  puts("***MATCH***");
            switch (m_tag) {
            case DW_TAG_compile_unit: // File
            case DW_TAG_partial_unit: // File
              check_children = ((function_die != NULL) || (block_die != NULL));
              break;

            case DW_TAG_subprogram: // Function
              if (function_die)
                *function_die = this;
              check_children = (block_die != NULL);
              break;

            case DW_TAG_inlined_subroutine: // Inlined Function
            case DW_TAG_lexical_block:      // Block { } in code
              if (block_die) {
                *block_die = this;
                check_children = true;
              }
              break;

            default:
              check_children = true;
              break;
            }
          } else {
            check_children = false;
          }
        }
      }
    }

    if (check_children) {
      //  printf("checking children\n");
      DWARFDebugInfoEntry *child = GetFirstChild();
      while (child) {
        if (child->LookupAddress(address, dwarf2Data, cu, function_die,
                                 block_die))
          return true;
        child = child->GetSibling();
      }
    }
  }
  return found_address;
}

const DWARFAbbreviationDeclaration *
DWARFDebugInfoEntry::GetAbbreviationDeclarationPtr(
    SymbolFileDWARF *dwarf2Data, const DWARFUnit *cu,
    lldb::offset_t &offset) const {
  if (dwarf2Data) {
    offset = GetOffset();

    const DWARFAbbreviationDeclarationSet *abbrev_set = cu->GetAbbreviations();
    if (abbrev_set) {
      const DWARFAbbreviationDeclaration *abbrev_decl =
          abbrev_set->GetAbbreviationDeclaration(m_abbr_idx);
      if (abbrev_decl) {
        // Make sure the abbreviation code still matches. If it doesn't and the
        // DWARF data was mmap'ed, the backing file might have been modified
        // which is bad news.
        const uint64_t abbrev_code = cu->GetData().GetULEB128(&offset);

        if (abbrev_decl->Code() == abbrev_code)
          return abbrev_decl;

        dwarf2Data->GetObjectFile()->GetModule()->ReportErrorIfModifyDetected(
            "0x%8.8x: the DWARF debug information has been modified (abbrev "
            "code was %u, and is now %u)",
            GetOffset(), (uint32_t)abbrev_decl->Code(), (uint32_t)abbrev_code);
      }
    }
  }
  offset = DW_INVALID_OFFSET;
  return NULL;
}

bool DWARFDebugInfoEntry::OffsetLessThan(const DWARFDebugInfoEntry &a,
                                         const DWARFDebugInfoEntry &b) {
  return a.GetOffset() < b.GetOffset();
}

void DWARFDebugInfoEntry::DumpDIECollection(
    Stream &strm, DWARFDebugInfoEntry::collection &die_collection) {
  DWARFDebugInfoEntry::const_iterator pos;
  DWARFDebugInfoEntry::const_iterator end = die_collection.end();
  strm.PutCString("\noffset    parent   sibling  child\n");
  strm.PutCString("--------  -------- -------- --------\n");
  for (pos = die_collection.begin(); pos != end; ++pos) {
    const DWARFDebugInfoEntry &die_ref = *pos;
    const DWARFDebugInfoEntry *p = die_ref.GetParent();
    const DWARFDebugInfoEntry *s = die_ref.GetSibling();
    const DWARFDebugInfoEntry *c = die_ref.GetFirstChild();
    strm.Printf("%.8x: %.8x %.8x %.8x 0x%4.4x %s%s\n", die_ref.GetOffset(),
                p ? p->GetOffset() : 0, s ? s->GetOffset() : 0,
                c ? c->GetOffset() : 0, die_ref.Tag(),
                DW_TAG_value_to_name(die_ref.Tag()),
                die_ref.HasChildren() ? " *" : "");
  }
}

bool DWARFDebugInfoEntry::operator==(const DWARFDebugInfoEntry &rhs) const {
  return m_offset == rhs.m_offset && m_parent_idx == rhs.m_parent_idx &&
         m_sibling_idx == rhs.m_sibling_idx &&
         m_abbr_idx == rhs.m_abbr_idx && m_has_children == rhs.m_has_children &&
         m_tag == rhs.m_tag;
}

bool DWARFDebugInfoEntry::operator!=(const DWARFDebugInfoEntry &rhs) const {
  return !(*this == rhs);
}
