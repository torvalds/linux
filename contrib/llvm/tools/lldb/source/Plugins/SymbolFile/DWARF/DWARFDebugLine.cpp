//===-- DWARFDebugLine.cpp --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "DWARFDebugLine.h"

//#define ENABLE_DEBUG_PRINTF   // DO NOT LEAVE THIS DEFINED: DEBUG ONLY!!!
#include <assert.h>

#include "lldb/Core/FileSpecList.h"
#include "lldb/Core/Module.h"
#include "lldb/Host/Host.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Timer.h"

#include "LogChannelDWARF.h"
#include "SymbolFileDWARF.h"

using namespace lldb;
using namespace lldb_private;
using namespace std;

//----------------------------------------------------------------------
// Parse
//
// Parse all information in the debug_line_data into an internal
// representation.
//----------------------------------------------------------------------
void DWARFDebugLine::Parse(const DWARFDataExtractor &debug_line_data) {
  m_lineTableMap.clear();
  lldb::offset_t offset = 0;
  LineTable::shared_ptr line_table_sp(new LineTable);
  while (debug_line_data.ValidOffset(offset)) {
    const lldb::offset_t debug_line_offset = offset;

    if (line_table_sp.get() == NULL)
      break;

    if (ParseStatementTable(debug_line_data, &offset, line_table_sp.get(), nullptr)) {
      // Make sure we don't don't loop infinitely
      if (offset <= debug_line_offset)
        break;
      // DEBUG_PRINTF("m_lineTableMap[0x%8.8x] = line_table_sp\n",
      // debug_line_offset);
      m_lineTableMap[debug_line_offset] = line_table_sp;
      line_table_sp.reset(new LineTable);
    } else
      ++offset; // Try next byte in line table
  }
}

void DWARFDebugLine::ParseIfNeeded(const DWARFDataExtractor &debug_line_data) {
  if (m_lineTableMap.empty())
    Parse(debug_line_data);
}

//----------------------------------------------------------------------
// DWARFDebugLine::GetLineTable
//----------------------------------------------------------------------
DWARFDebugLine::LineTable::shared_ptr
DWARFDebugLine::GetLineTable(const dw_offset_t offset) const {
  DWARFDebugLine::LineTable::shared_ptr line_table_shared_ptr;
  LineTableConstIter pos = m_lineTableMap.find(offset);
  if (pos != m_lineTableMap.end())
    line_table_shared_ptr = pos->second;
  return line_table_shared_ptr;
}

//----------------------------------------------------------------------
// DumpStateToFile
//----------------------------------------------------------------------
static void DumpStateToFile(dw_offset_t offset,
                            const DWARFDebugLine::State &state,
                            void *userData) {
  Log *log = (Log *)userData;
  if (state.row == DWARFDebugLine::State::StartParsingLineTable) {
    // If the row is zero we are being called with the prologue only
    state.prologue->Dump(log);
    log->PutCString("Address            Line   Column File");
    log->PutCString("------------------ ------ ------ ------");
  } else if (state.row == DWARFDebugLine::State::DoneParsingLineTable) {
    // Done parsing line table
  } else {
    log->Printf("0x%16.16" PRIx64 " %6u %6u %6u%s\n", state.address, state.line,
                state.column, state.file, state.end_sequence ? " END" : "");
  }
}

//----------------------------------------------------------------------
// DWARFDebugLine::DumpLineTableRows
//----------------------------------------------------------------------
bool DWARFDebugLine::DumpLineTableRows(Log *log, SymbolFileDWARF *dwarf2Data,
                                       dw_offset_t debug_line_offset) {
  const DWARFDataExtractor &debug_line_data = dwarf2Data->get_debug_line_data();

  if (debug_line_offset == DW_INVALID_OFFSET) {
    // Dump line table to a single file only
    debug_line_offset = 0;
    while (debug_line_data.ValidOffset(debug_line_offset))
      debug_line_offset =
          DumpStatementTable(log, debug_line_data, debug_line_offset);
  } else {
    // Dump line table to a single file only
    DumpStatementTable(log, debug_line_data, debug_line_offset);
  }
  return false;
}

//----------------------------------------------------------------------
// DWARFDebugLine::DumpStatementTable
//----------------------------------------------------------------------
dw_offset_t
DWARFDebugLine::DumpStatementTable(Log *log,
                                   const DWARFDataExtractor &debug_line_data,
                                   const dw_offset_t debug_line_offset) {
  if (debug_line_data.ValidOffset(debug_line_offset)) {
    lldb::offset_t offset = debug_line_offset;
    log->Printf("--------------------------------------------------------------"
                "--------\n"
                "debug_line[0x%8.8x]\n"
                "--------------------------------------------------------------"
                "--------\n",
                debug_line_offset);

    if (ParseStatementTable(debug_line_data, &offset, DumpStateToFile, log, nullptr))
      return offset;
    else
      return debug_line_offset + 1; // Skip to next byte in .debug_line section
  }

  return DW_INVALID_OFFSET;
}

//----------------------------------------------------------------------
// DumpOpcodes
//----------------------------------------------------------------------
bool DWARFDebugLine::DumpOpcodes(Log *log, SymbolFileDWARF *dwarf2Data,
                                 dw_offset_t debug_line_offset,
                                 uint32_t dump_flags) {
  const DWARFDataExtractor &debug_line_data = dwarf2Data->get_debug_line_data();

  if (debug_line_data.GetByteSize() == 0) {
    log->Printf("< EMPTY >\n");
    return false;
  }

  if (debug_line_offset == DW_INVALID_OFFSET) {
    // Dump line table to a single file only
    debug_line_offset = 0;
    while (debug_line_data.ValidOffset(debug_line_offset))
      debug_line_offset = DumpStatementOpcodes(log, debug_line_data,
                                               debug_line_offset, dump_flags);
  } else {
    // Dump line table to a single file only
    DumpStatementOpcodes(log, debug_line_data, debug_line_offset, dump_flags);
  }
  return false;
}

//----------------------------------------------------------------------
// DumpStatementOpcodes
//----------------------------------------------------------------------
dw_offset_t DWARFDebugLine::DumpStatementOpcodes(
    Log *log, const DWARFDataExtractor &debug_line_data,
    const dw_offset_t debug_line_offset, uint32_t flags) {
  lldb::offset_t offset = debug_line_offset;
  if (debug_line_data.ValidOffset(offset)) {
    Prologue prologue;

    if (ParsePrologue(debug_line_data, &offset, &prologue)) {
      log->PutCString("--------------------------------------------------------"
                      "--------------");
      log->Printf("debug_line[0x%8.8x]", debug_line_offset);
      log->PutCString("--------------------------------------------------------"
                      "--------------\n");
      prologue.Dump(log);
    } else {
      offset = debug_line_offset;
      log->Printf("0x%8.8" PRIx64 ": skipping pad byte %2.2x", offset,
                  debug_line_data.GetU8(&offset));
      return offset;
    }

    Row row(prologue.default_is_stmt);
    const dw_offset_t end_offset = debug_line_offset + prologue.total_length +
                                   sizeof(prologue.total_length);

    assert(debug_line_data.ValidOffset(end_offset - 1));

    while (offset < end_offset) {
      const uint32_t op_offset = offset;
      uint8_t opcode = debug_line_data.GetU8(&offset);
      switch (opcode) {
      case 0: // Extended Opcodes always start with a zero opcode followed by
      {       // a uleb128 length so you can skip ones you don't know about

        dw_offset_t ext_offset = offset;
        dw_uleb128_t len = debug_line_data.GetULEB128(&offset);
        dw_offset_t arg_size = len - (offset - ext_offset);
        uint8_t sub_opcode = debug_line_data.GetU8(&offset);
        //                    if (verbose)
        //                        log->Printf( "Extended: <%u> %2.2x ", len,
        //                        sub_opcode);

        switch (sub_opcode) {
        case DW_LNE_end_sequence:
          log->Printf("0x%8.8x: DW_LNE_end_sequence", op_offset);
          row.Dump(log);
          row.Reset(prologue.default_is_stmt);
          break;

        case DW_LNE_set_address: {
          row.address = debug_line_data.GetMaxU64(&offset, arg_size);
          log->Printf("0x%8.8x: DW_LNE_set_address (0x%" PRIx64 ")", op_offset,
                      row.address);
        } break;

        case DW_LNE_define_file: {
          FileNameEntry fileEntry;
          fileEntry.name = debug_line_data.GetCStr(&offset);
          fileEntry.dir_idx = debug_line_data.GetULEB128(&offset);
          fileEntry.mod_time = debug_line_data.GetULEB128(&offset);
          fileEntry.length = debug_line_data.GetULEB128(&offset);
          log->Printf("0x%8.8x: DW_LNE_define_file('%s', dir=%i, "
                      "mod_time=0x%8.8x, length=%i )",
                      op_offset, fileEntry.name, fileEntry.dir_idx,
                      fileEntry.mod_time, fileEntry.length);
          prologue.file_names.push_back(fileEntry);
        } break;

        case DW_LNE_set_discriminator: {
          uint64_t discriminator = debug_line_data.GetULEB128(&offset);
          log->Printf("0x%8.8x: DW_LNE_set_discriminator (0x%" PRIx64 ")",
                      op_offset, discriminator);
        } break;
        default:
          log->Printf("0x%8.8x: DW_LNE_??? (%2.2x) - Skipping unknown upcode",
                      op_offset, opcode);
          // Length doesn't include the zero opcode byte or the length itself,
          // but it does include the sub_opcode, so we have to adjust for that
          // below
          offset += arg_size;
          break;
        }
      } break;

      // Standard Opcodes
      case DW_LNS_copy:
        log->Printf("0x%8.8x: DW_LNS_copy", op_offset);
        row.Dump(log);
        break;

      case DW_LNS_advance_pc: {
        dw_uleb128_t addr_offset_n = debug_line_data.GetULEB128(&offset);
        dw_uleb128_t addr_offset = addr_offset_n * prologue.min_inst_length;
        log->Printf("0x%8.8x: DW_LNS_advance_pc (0x%x)", op_offset,
                    addr_offset);
        row.address += addr_offset;
      } break;

      case DW_LNS_advance_line: {
        dw_sleb128_t line_offset = debug_line_data.GetSLEB128(&offset);
        log->Printf("0x%8.8x: DW_LNS_advance_line (%i)", op_offset,
                    line_offset);
        row.line += line_offset;
      } break;

      case DW_LNS_set_file:
        row.file = debug_line_data.GetULEB128(&offset);
        log->Printf("0x%8.8x: DW_LNS_set_file (%u)", op_offset, row.file);
        break;

      case DW_LNS_set_column:
        row.column = debug_line_data.GetULEB128(&offset);
        log->Printf("0x%8.8x: DW_LNS_set_column (%u)", op_offset, row.column);
        break;

      case DW_LNS_negate_stmt:
        row.is_stmt = !row.is_stmt;
        log->Printf("0x%8.8x: DW_LNS_negate_stmt", op_offset);
        break;

      case DW_LNS_set_basic_block:
        row.basic_block = true;
        log->Printf("0x%8.8x: DW_LNS_set_basic_block", op_offset);
        break;

      case DW_LNS_const_add_pc: {
        uint8_t adjust_opcode = 255 - prologue.opcode_base;
        dw_addr_t addr_offset =
            (adjust_opcode / prologue.line_range) * prologue.min_inst_length;
        log->Printf("0x%8.8x: DW_LNS_const_add_pc (0x%8.8" PRIx64 ")",
                    op_offset, addr_offset);
        row.address += addr_offset;
      } break;

      case DW_LNS_fixed_advance_pc: {
        uint16_t pc_offset = debug_line_data.GetU16(&offset);
        log->Printf("0x%8.8x: DW_LNS_fixed_advance_pc (0x%4.4x)", op_offset,
                    pc_offset);
        row.address += pc_offset;
      } break;

      case DW_LNS_set_prologue_end:
        row.prologue_end = true;
        log->Printf("0x%8.8x: DW_LNS_set_prologue_end", op_offset);
        break;

      case DW_LNS_set_epilogue_begin:
        row.epilogue_begin = true;
        log->Printf("0x%8.8x: DW_LNS_set_epilogue_begin", op_offset);
        break;

      case DW_LNS_set_isa:
        row.isa = debug_line_data.GetULEB128(&offset);
        log->Printf("0x%8.8x: DW_LNS_set_isa (%u)", op_offset, row.isa);
        break;

      // Special Opcodes
      default:
        if (opcode < prologue.opcode_base) {
          // We have an opcode that this parser doesn't know about, skip the
          // number of ULEB128 numbers that is says to skip in the prologue's
          // standard_opcode_lengths array
          uint8_t n = prologue.standard_opcode_lengths[opcode - 1];
          log->Printf("0x%8.8x: Special : Unknown skipping %u ULEB128 values.",
                      op_offset, n);
          while (n > 0) {
            debug_line_data.GetULEB128(&offset);
            --n;
          }
        } else {
          uint8_t adjust_opcode = opcode - prologue.opcode_base;
          dw_addr_t addr_offset =
              (adjust_opcode / prologue.line_range) * prologue.min_inst_length;
          int32_t line_offset =
              prologue.line_base + (adjust_opcode % prologue.line_range);
          log->Printf("0x%8.8x: address += 0x%" PRIx64 ",  line += %i\n",
                      op_offset, (uint64_t)addr_offset, line_offset);
          row.address += addr_offset;
          row.line += line_offset;
          row.Dump(log);
        }
        break;
      }
    }
    return end_offset;
  }
  return DW_INVALID_OFFSET;
}

//----------------------------------------------------------------------
// Parse
//
// Parse the entire line table contents calling callback each time a new
// prologue is parsed and every time a new row is to be added to the line
// table.
//----------------------------------------------------------------------
void DWARFDebugLine::Parse(const DWARFDataExtractor &debug_line_data,
                           DWARFDebugLine::State::Callback callback,
                           void *userData) {
  lldb::offset_t offset = 0;
  if (debug_line_data.ValidOffset(offset)) {
    if (!ParseStatementTable(debug_line_data, &offset, callback, userData, nullptr))
      ++offset; // Skip to next byte in .debug_line section
  }
}

namespace {
struct EntryDescriptor {
  dw_sleb128_t code;
  dw_sleb128_t form;
};

static std::vector<EntryDescriptor>
ReadDescriptors(const DWARFDataExtractor &debug_line_data,
                lldb::offset_t *offset_ptr) {
  std::vector<EntryDescriptor> ret;
  uint8_t n = debug_line_data.GetU8(offset_ptr);
  for (uint8_t i = 0; i < n; ++i) {
    EntryDescriptor ent;
    ent.code = debug_line_data.GetULEB128(offset_ptr);
    ent.form = debug_line_data.GetULEB128(offset_ptr);
    ret.push_back(ent);
  }
  return ret;
}
} // namespace

//----------------------------------------------------------------------
// DWARFDebugLine::ParsePrologue
//----------------------------------------------------------------------
bool DWARFDebugLine::ParsePrologue(const DWARFDataExtractor &debug_line_data,
                                   lldb::offset_t *offset_ptr,
                                   Prologue *prologue, DWARFUnit *dwarf_cu) {
  const lldb::offset_t prologue_offset = *offset_ptr;

  // DEBUG_PRINTF("0x%8.8x: ParsePrologue()\n", *offset_ptr);

  prologue->Clear();
  uint32_t i;
  const char *s;
  prologue->total_length = debug_line_data.GetDWARFInitialLength(offset_ptr);
  prologue->version = debug_line_data.GetU16(offset_ptr);
  if (prologue->version < 2 || prologue->version > 5)
    return false;

  if (prologue->version >= 5) {
    prologue->address_size = debug_line_data.GetU8(offset_ptr);
    prologue->segment_selector_size = debug_line_data.GetU8(offset_ptr);
  }

  prologue->prologue_length = debug_line_data.GetDWARFOffset(offset_ptr);
  const lldb::offset_t end_prologue_offset =
      prologue->prologue_length + *offset_ptr;
  prologue->min_inst_length = debug_line_data.GetU8(offset_ptr);
  if (prologue->version >= 4)
    prologue->maximum_operations_per_instruction =
        debug_line_data.GetU8(offset_ptr);
  else
    prologue->maximum_operations_per_instruction = 1;
  prologue->default_is_stmt = debug_line_data.GetU8(offset_ptr);
  prologue->line_base = debug_line_data.GetU8(offset_ptr);
  prologue->line_range = debug_line_data.GetU8(offset_ptr);
  prologue->opcode_base = debug_line_data.GetU8(offset_ptr);

  prologue->standard_opcode_lengths.reserve(prologue->opcode_base - 1);

  for (i = 1; i < prologue->opcode_base; ++i) {
    uint8_t op_len = debug_line_data.GetU8(offset_ptr);
    prologue->standard_opcode_lengths.push_back(op_len);
  }

  if (prologue->version >= 5) {
    std::vector<EntryDescriptor> dirEntryFormatV =
        ReadDescriptors(debug_line_data, offset_ptr);
    uint8_t dirCount = debug_line_data.GetULEB128(offset_ptr);
    for (int i = 0; i < dirCount; ++i) {
      for (EntryDescriptor &ent : dirEntryFormatV) {
        DWARFFormValue value(dwarf_cu, ent.form);
        if (ent.code != DW_LNCT_path) {
          if (!value.SkipValue(debug_line_data, offset_ptr))
            return false;
          continue;
        }

        if (!value.ExtractValue(debug_line_data, offset_ptr))
          return false;
        prologue->include_directories.push_back(value.AsCString());
      }
    }

    std::vector<EntryDescriptor> filesEntryFormatV =
        ReadDescriptors(debug_line_data, offset_ptr);
    llvm::DenseSet<std::pair<uint64_t, uint64_t>> seen;
    uint8_t n = debug_line_data.GetULEB128(offset_ptr);
    for (int i = 0; i < n; ++i) {
      FileNameEntry entry;
      for (EntryDescriptor &ent : filesEntryFormatV) {
        DWARFFormValue value(dwarf_cu, ent.form);
        if (!value.ExtractValue(debug_line_data, offset_ptr))
          return false;

        switch (ent.code) {
        case DW_LNCT_path:
          entry.name = value.AsCString();
          break;
        case DW_LNCT_directory_index:
          entry.dir_idx = value.Unsigned();
          break;
        case DW_LNCT_timestamp:
          entry.mod_time = value.Unsigned();
          break;
        case DW_LNCT_size:
          entry.length = value.Unsigned();
          break;
        case DW_LNCT_MD5:
          assert(value.Unsigned() == 16);
          std::uninitialized_copy_n(value.BlockData(), 16,
                                    entry.checksum.Bytes.begin());
          break;
        default:
          break;
        }
      }

      if (seen.insert(entry.checksum.words()).second)
        prologue->file_names.push_back(entry);
    }
  } else {
    while (*offset_ptr < end_prologue_offset) {
      s = debug_line_data.GetCStr(offset_ptr);
      if (s && s[0])
        prologue->include_directories.push_back(s);
      else
        break;
    }

    while (*offset_ptr < end_prologue_offset) {
      const char *name = debug_line_data.GetCStr(offset_ptr);
      if (name && name[0]) {
        FileNameEntry fileEntry;
        fileEntry.name = name;
        fileEntry.dir_idx = debug_line_data.GetULEB128(offset_ptr);
        fileEntry.mod_time = debug_line_data.GetULEB128(offset_ptr);
        fileEntry.length = debug_line_data.GetULEB128(offset_ptr);
        prologue->file_names.push_back(fileEntry);
      } else
        break;
    }
  }

  // XXX GNU as is broken for 64-Bit DWARF
  if (*offset_ptr != end_prologue_offset) {
    Host::SystemLog(Host::eSystemLogWarning,
                    "warning: parsing line table prologue at 0x%8.8" PRIx64
                    " should have ended at 0x%8.8" PRIx64
                    " but it ended at 0x%8.8" PRIx64 "\n",
                    prologue_offset, end_prologue_offset, *offset_ptr);
  }
  return end_prologue_offset;
}

bool DWARFDebugLine::ParseSupportFiles(
    const lldb::ModuleSP &module_sp, const DWARFDataExtractor &debug_line_data,
    const lldb_private::FileSpec &cu_comp_dir, dw_offset_t stmt_list,
    FileSpecList &support_files, DWARFUnit *dwarf_cu) {
  lldb::offset_t offset = stmt_list;

  Prologue prologue;
  if (!ParsePrologue(debug_line_data, &offset, &prologue, dwarf_cu)) {
    Host::SystemLog(Host::eSystemLogError, "error: parsing line table prologue "
                                           "at 0x%8.8x (parsing ended around "
                                           "0x%8.8" PRIx64 "\n",
                    stmt_list, offset);
    return false;
  }

  FileSpec file_spec;
  std::string remapped_file;

  for (uint32_t file_idx = 1;
       prologue.GetFile(file_idx, cu_comp_dir, file_spec); ++file_idx) {
    if (module_sp->RemapSourceFile(file_spec.GetPath(), remapped_file))
      file_spec.SetFile(remapped_file, FileSpec::Style::native);
    support_files.Append(file_spec);
  }
  return true;
}

//----------------------------------------------------------------------
// ParseStatementTable
//
// Parse a single line table (prologue and all rows) and call the callback
// function once for the prologue (row in state will be zero) and each time a
// row is to be added to the line table.
//----------------------------------------------------------------------
bool DWARFDebugLine::ParseStatementTable(
    const DWARFDataExtractor &debug_line_data, lldb::offset_t *offset_ptr,
    DWARFDebugLine::State::Callback callback, void *userData, DWARFUnit *dwarf_cu) {
  Log *log(LogChannelDWARF::GetLogIfAll(DWARF_LOG_DEBUG_LINE));
  Prologue::shared_ptr prologue(new Prologue());

  const dw_offset_t debug_line_offset = *offset_ptr;

  static Timer::Category func_cat(LLVM_PRETTY_FUNCTION);
  Timer scoped_timer(
      func_cat, "DWARFDebugLine::ParseStatementTable (.debug_line[0x%8.8x])",
      debug_line_offset);

  if (!ParsePrologue(debug_line_data, offset_ptr, prologue.get(), dwarf_cu)) {
    if (log)
      log->Error("failed to parse DWARF line table prologue");
    // Restore our offset and return false to indicate failure!
    *offset_ptr = debug_line_offset;
    return false;
  }

  if (log)
    prologue->Dump(log);

  const dw_offset_t end_offset =
      debug_line_offset + prologue->total_length +
      (debug_line_data.GetDWARFSizeofInitialLength());

  State state(prologue, log, callback, userData);

  while (*offset_ptr < end_offset) {
    // DEBUG_PRINTF("0x%8.8x: ", *offset_ptr);
    uint8_t opcode = debug_line_data.GetU8(offset_ptr);

    if (opcode == 0) {
      // Extended Opcodes always start with a zero opcode followed by a uleb128
      // length so you can skip ones you don't know about
      lldb::offset_t ext_offset = *offset_ptr;
      dw_uleb128_t len = debug_line_data.GetULEB128(offset_ptr);
      dw_offset_t arg_size = len - (*offset_ptr - ext_offset);

      // DEBUG_PRINTF("Extended: <%2u> ", len);
      uint8_t sub_opcode = debug_line_data.GetU8(offset_ptr);
      switch (sub_opcode) {
      case DW_LNE_end_sequence:
        // Set the end_sequence register of the state machine to true and
        // append a row to the matrix using the current values of the state-
        // machine registers. Then reset the registers to the initial values
        // specified above. Every statement program sequence must end with a
        // DW_LNE_end_sequence instruction which creates a row whose address is
        // that of the byte after the last target machine instruction of the
        // sequence.
        state.end_sequence = true;
        state.AppendRowToMatrix(*offset_ptr);
        state.Reset();
        break;

      case DW_LNE_set_address:
        // Takes a single relocatable address as an operand. The size of the
        // operand is the size appropriate to hold an address on the target
        // machine. Set the address register to the value given by the
        // relocatable address. All of the other statement program opcodes that
        // affect the address register add a delta to it. This instruction
        // stores a relocatable value into it instead.
        if (arg_size == 4)
          state.address = debug_line_data.GetU32(offset_ptr);
        else // arg_size == 8
          state.address = debug_line_data.GetU64(offset_ptr);
        break;

      case DW_LNE_define_file:
        // Takes 4 arguments. The first is a null terminated string containing
        // a source file name. The second is an unsigned LEB128 number
        // representing the directory index of the directory in which the file
        // was found. The third is an unsigned LEB128 number representing the
        // time of last modification of the file. The fourth is an unsigned
        // LEB128 number representing the length in bytes of the file. The time
        // and length fields may contain LEB128(0) if the information is not
        // available.
        //
        // The directory index represents an entry in the include_directories
        // section of the statement program prologue. The index is LEB128(0) if
        // the file was found in the current directory of the compilation,
        // LEB128(1) if it was found in the first directory in the
        // include_directories section, and so on. The directory index is
        // ignored for file names that represent full path names.
        //
        // The files are numbered, starting at 1, in the order in which they
        // appear; the names in the prologue come before names defined by the
        // DW_LNE_define_file instruction. These numbers are used in the file
        // register of the state machine.
        {
          FileNameEntry fileEntry;
          fileEntry.name = debug_line_data.GetCStr(offset_ptr);
          fileEntry.dir_idx = debug_line_data.GetULEB128(offset_ptr);
          fileEntry.mod_time = debug_line_data.GetULEB128(offset_ptr);
          fileEntry.length = debug_line_data.GetULEB128(offset_ptr);
          state.prologue->file_names.push_back(fileEntry);
        }
        break;

      default:
        // Length doesn't include the zero opcode byte or the length itself,
        // but it does include the sub_opcode, so we have to adjust for that
        // below
        (*offset_ptr) += arg_size;
        break;
      }
    } else if (opcode < prologue->opcode_base) {
      switch (opcode) {
      // Standard Opcodes
      case DW_LNS_copy:
        // Takes no arguments. Append a row to the matrix using the current
        // values of the state-machine registers. Then set the basic_block
        // register to false.
        state.AppendRowToMatrix(*offset_ptr);
        break;

      case DW_LNS_advance_pc:
        // Takes a single unsigned LEB128 operand, multiplies it by the
        // min_inst_length field of the prologue, and adds the result to the
        // address register of the state machine.
        state.address +=
            debug_line_data.GetULEB128(offset_ptr) * prologue->min_inst_length;
        break;

      case DW_LNS_advance_line:
        // Takes a single signed LEB128 operand and adds that value to the line
        // register of the state machine.
        state.line += debug_line_data.GetSLEB128(offset_ptr);
        break;

      case DW_LNS_set_file:
        // Takes a single unsigned LEB128 operand and stores it in the file
        // register of the state machine.
        state.file = debug_line_data.GetULEB128(offset_ptr);
        break;

      case DW_LNS_set_column:
        // Takes a single unsigned LEB128 operand and stores it in the column
        // register of the state machine.
        state.column = debug_line_data.GetULEB128(offset_ptr);
        break;

      case DW_LNS_negate_stmt:
        // Takes no arguments. Set the is_stmt register of the state machine to
        // the logical negation of its current value.
        state.is_stmt = !state.is_stmt;
        break;

      case DW_LNS_set_basic_block:
        // Takes no arguments. Set the basic_block register of the state
        // machine to true
        state.basic_block = true;
        break;

      case DW_LNS_const_add_pc:
        // Takes no arguments. Add to the address register of the state machine
        // the address increment value corresponding to special opcode 255. The
        // motivation for DW_LNS_const_add_pc is this: when the statement
        // program needs to advance the address by a small amount, it can use a
        // single special opcode, which occupies a single byte. When it needs
        // to advance the address by up to twice the range of the last special
        // opcode, it can use DW_LNS_const_add_pc followed by a special opcode,
        // for a total of two bytes. Only if it needs to advance the address by
        // more than twice that range will it need to use both
        // DW_LNS_advance_pc and a special opcode, requiring three or more
        // bytes.
        {
          uint8_t adjust_opcode = 255 - prologue->opcode_base;
          dw_addr_t addr_offset = (adjust_opcode / prologue->line_range) *
                                  prologue->min_inst_length;
          state.address += addr_offset;
        }
        break;

      case DW_LNS_fixed_advance_pc:
        // Takes a single uhalf operand. Add to the address register of the
        // state machine the value of the (unencoded) operand. This is the only
        // extended opcode that takes an argument that is not a variable length
        // number. The motivation for DW_LNS_fixed_advance_pc is this: existing
        // assemblers cannot emit DW_LNS_advance_pc or special opcodes because
        // they cannot encode LEB128 numbers or judge when the computation of a
        // special opcode overflows and requires the use of DW_LNS_advance_pc.
        // Such assemblers, however, can use DW_LNS_fixed_advance_pc instead,
        // sacrificing compression.
        state.address += debug_line_data.GetU16(offset_ptr);
        break;

      case DW_LNS_set_prologue_end:
        // Takes no arguments. Set the prologue_end register of the state
        // machine to true
        state.prologue_end = true;
        break;

      case DW_LNS_set_epilogue_begin:
        // Takes no arguments. Set the basic_block register of the state
        // machine to true
        state.epilogue_begin = true;
        break;

      case DW_LNS_set_isa:
        // Takes a single unsigned LEB128 operand and stores it in the column
        // register of the state machine.
        state.isa = debug_line_data.GetULEB128(offset_ptr);
        break;

      default:
        // Handle any unknown standard opcodes here. We know the lengths of
        // such opcodes because they are specified in the prologue as a
        // multiple of LEB128 operands for each opcode.
        {
          uint8_t i;
          assert(static_cast<size_t>(opcode - 1) <
                 prologue->standard_opcode_lengths.size());
          const uint8_t opcode_length =
              prologue->standard_opcode_lengths[opcode - 1];
          for (i = 0; i < opcode_length; ++i)
            debug_line_data.Skip_LEB128(offset_ptr);
        }
        break;
      }
    } else {
      // Special Opcodes

      // A special opcode value is chosen based on the amount that needs
      // to be added to the line and address registers. The maximum line
      // increment for a special opcode is the value of the line_base field in
      // the header, plus the value of the line_range field, minus 1 (line base
      // + line range - 1). If the desired line increment is greater than the
      // maximum line increment, a standard opcode must be used instead of a
      // special opcode. The "address advance" is calculated by dividing the
      // desired address increment by the minimum_instruction_length field from
      // the header. The special opcode is then calculated using the following
      // formula:
      //
      //  opcode = (desired line increment - line_base) + (line_range * address
      //  advance) + opcode_base
      //
      // If the resulting opcode is greater than 255, a standard opcode must be
      // used instead.
      //
      // To decode a special opcode, subtract the opcode_base from the opcode
      // itself to give the adjusted opcode. The amount to increment the
      // address register is the result of the adjusted opcode divided by the
      // line_range multiplied by the minimum_instruction_length field from the
      // header. That is:
      //
      //  address increment = (adjusted opcode / line_range) *
      //  minimum_instruction_length
      //
      // The amount to increment the line register is the line_base plus the
      // result of the adjusted opcode modulo the line_range. That is:
      //
      // line increment = line_base + (adjusted opcode % line_range)

      uint8_t adjust_opcode = opcode - prologue->opcode_base;
      dw_addr_t addr_offset =
          (adjust_opcode / prologue->line_range) * prologue->min_inst_length;
      int32_t line_offset =
          prologue->line_base + (adjust_opcode % prologue->line_range);
      state.line += line_offset;
      state.address += addr_offset;
      state.AppendRowToMatrix(*offset_ptr);
    }
  }

  state.Finalize(*offset_ptr);

  return end_offset;
}

//----------------------------------------------------------------------
// ParseStatementTableCallback
//----------------------------------------------------------------------
static void ParseStatementTableCallback(dw_offset_t offset,
                                        const DWARFDebugLine::State &state,
                                        void *userData) {
  DWARFDebugLine::LineTable *line_table = (DWARFDebugLine::LineTable *)userData;
  if (state.row == DWARFDebugLine::State::StartParsingLineTable) {
    // Just started parsing the line table, so lets keep a reference to the
    // prologue using the supplied shared pointer
    line_table->prologue = state.prologue;
  } else if (state.row == DWARFDebugLine::State::DoneParsingLineTable) {
    // Done parsing line table, nothing to do for the cleanup
  } else {
    // We have a new row, lets append it
    line_table->AppendRow(state);
  }
}

//----------------------------------------------------------------------
// ParseStatementTable
//
// Parse a line table at offset and populate the LineTable class with the
// prologue and all rows.
//----------------------------------------------------------------------
bool DWARFDebugLine::ParseStatementTable(
    const DWARFDataExtractor &debug_line_data, lldb::offset_t *offset_ptr,
    LineTable *line_table, DWARFUnit *dwarf_cu) {
  return ParseStatementTable(debug_line_data, offset_ptr,
                             ParseStatementTableCallback, line_table, dwarf_cu);
}

inline bool DWARFDebugLine::Prologue::IsValid() const {
  return SymbolFileDWARF::SupportedVersion(version);
}

//----------------------------------------------------------------------
// DWARFDebugLine::Prologue::Dump
//----------------------------------------------------------------------
void DWARFDebugLine::Prologue::Dump(Log *log) {
  uint32_t i;

  log->Printf("Line table prologue:");
  log->Printf("   total_length: 0x%8.8x", total_length);
  log->Printf("        version: %u", version);
  log->Printf("prologue_length: 0x%8.8x", prologue_length);
  log->Printf("min_inst_length: %u", min_inst_length);
  log->Printf("default_is_stmt: %u", default_is_stmt);
  log->Printf("      line_base: %i", line_base);
  log->Printf("     line_range: %u", line_range);
  log->Printf("    opcode_base: %u", opcode_base);

  for (i = 0; i < standard_opcode_lengths.size(); ++i) {
    log->Printf("standard_opcode_lengths[%s] = %u", DW_LNS_value_to_name(i + 1),
                standard_opcode_lengths[i]);
  }

  if (!include_directories.empty()) {
    for (i = 0; i < include_directories.size(); ++i) {
      log->Printf("include_directories[%3u] = '%s'", i + 1,
                  include_directories[i]);
    }
  }

  if (!file_names.empty()) {
    log->PutCString("                Dir  Mod Time   File Len   File Name");
    log->PutCString("                ---- ---------- ---------- "
                    "---------------------------");
    for (i = 0; i < file_names.size(); ++i) {
      const FileNameEntry &fileEntry = file_names[i];
      log->Printf("file_names[%3u] %4u 0x%8.8x 0x%8.8x %s", i + 1,
                  fileEntry.dir_idx, fileEntry.mod_time, fileEntry.length,
                  fileEntry.name);
    }
  }
}

//----------------------------------------------------------------------
// DWARFDebugLine::ParsePrologue::Append
//
// Append the contents of the prologue to the binary stream buffer
//----------------------------------------------------------------------
// void
// DWARFDebugLine::Prologue::Append(BinaryStreamBuf& buff) const
//{
//  uint32_t i;
//
//  buff.Append32(total_length);
//  buff.Append16(version);
//  buff.Append32(prologue_length);
//  buff.Append8(min_inst_length);
//  buff.Append8(default_is_stmt);
//  buff.Append8(line_base);
//  buff.Append8(line_range);
//  buff.Append8(opcode_base);
//
//  for (i=0; i<standard_opcode_lengths.size(); ++i)
//      buff.Append8(standard_opcode_lengths[i]);
//
//  for (i=0; i<include_directories.size(); ++i)
//      buff.AppendCStr(include_directories[i].c_str());
//  buff.Append8(0);    // Terminate the include directory section with empty
//  string
//
//  for (i=0; i<file_names.size(); ++i)
//  {
//      buff.AppendCStr(file_names[i].name.c_str());
//      buff.Append32_as_ULEB128(file_names[i].dir_idx);
//      buff.Append32_as_ULEB128(file_names[i].mod_time);
//      buff.Append32_as_ULEB128(file_names[i].length);
//  }
//  buff.Append8(0);    // Terminate the file names section with empty string
//}

bool DWARFDebugLine::Prologue::GetFile(uint32_t file_idx,
    const lldb_private::FileSpec &comp_dir, FileSpec &file) const {
  uint32_t idx = file_idx - 1; // File indexes are 1 based...
  if (idx < file_names.size()) {
    file.SetFile(file_names[idx].name, FileSpec::Style::native);
    if (file.IsRelative()) {
      if (file_names[idx].dir_idx > 0) {
        const uint32_t dir_idx = file_names[idx].dir_idx - 1;
        if (dir_idx < include_directories.size()) {
          file.PrependPathComponent(include_directories[dir_idx]);
          if (!file.IsRelative())
            return true;
        }
      }

      if (comp_dir)
        file.PrependPathComponent(comp_dir);
    }
    return true;
  }
  return false;
}

//----------------------------------------------------------------------
// DWARFDebugLine::LineTable::Dump
//----------------------------------------------------------------------
void DWARFDebugLine::LineTable::Dump(Log *log) const {
  if (prologue.get())
    prologue->Dump(log);

  if (!rows.empty()) {
    log->PutCString("Address            Line   Column File   ISA Flags");
    log->PutCString(
        "------------------ ------ ------ ------ --- -------------");
    Row::const_iterator pos = rows.begin();
    Row::const_iterator end = rows.end();
    while (pos != end) {
      (*pos).Dump(log);
      ++pos;
    }
  }
}

void DWARFDebugLine::LineTable::AppendRow(const DWARFDebugLine::Row &state) {
  rows.push_back(state);
}

//----------------------------------------------------------------------
// Compare function for the binary search in
// DWARFDebugLine::LineTable::LookupAddress()
//----------------------------------------------------------------------
static bool FindMatchingAddress(const DWARFDebugLine::Row &row1,
                                const DWARFDebugLine::Row &row2) {
  return row1.address < row2.address;
}

//----------------------------------------------------------------------
// DWARFDebugLine::LineTable::LookupAddress
//----------------------------------------------------------------------
uint32_t DWARFDebugLine::LineTable::LookupAddress(dw_addr_t address,
                                                  dw_addr_t cu_high_pc) const {
  uint32_t index = UINT32_MAX;
  if (!rows.empty()) {
    // Use the lower_bound algorithm to perform a binary search since we know
    // that our line table data is ordered by address.
    DWARFDebugLine::Row row;
    row.address = address;
    Row::const_iterator begin_pos = rows.begin();
    Row::const_iterator end_pos = rows.end();
    Row::const_iterator pos =
        lower_bound(begin_pos, end_pos, row, FindMatchingAddress);
    if (pos == end_pos) {
      if (address < cu_high_pc)
        return rows.size() - 1;
    } else {
      // Rely on fact that we are using a std::vector and we can do pointer
      // arithmetic to find the row index (which will be one less that what we
      // found since it will find the first position after the current address)
      // since std::vector iterators are just pointers to the container type.
      index = pos - begin_pos;
      if (pos->address > address) {
        if (index > 0)
          --index;
        else
          index = UINT32_MAX;
      }
    }
  }
  return index; // Failed to find address
}

//----------------------------------------------------------------------
// DWARFDebugLine::Row::Row
//----------------------------------------------------------------------
DWARFDebugLine::Row::Row(bool default_is_stmt)
    : address(0), line(1), column(0), file(1), is_stmt(default_is_stmt),
      basic_block(false), end_sequence(false), prologue_end(false),
      epilogue_begin(false), isa(0) {}

//----------------------------------------------------------------------
// Called after a row is appended to the matrix
//----------------------------------------------------------------------
void DWARFDebugLine::Row::PostAppend() {
  basic_block = false;
  prologue_end = false;
  epilogue_begin = false;
}

//----------------------------------------------------------------------
// DWARFDebugLine::Row::Reset
//----------------------------------------------------------------------
void DWARFDebugLine::Row::Reset(bool default_is_stmt) {
  address = 0;
  line = 1;
  column = 0;
  file = 1;
  is_stmt = default_is_stmt;
  basic_block = false;
  end_sequence = false;
  prologue_end = false;
  epilogue_begin = false;
  isa = 0;
}
//----------------------------------------------------------------------
// DWARFDebugLine::Row::Dump
//----------------------------------------------------------------------
void DWARFDebugLine::Row::Dump(Log *log) const {
  log->Printf("0x%16.16" PRIx64 " %6u %6u %6u %3u %s%s%s%s%s", address, line,
              column, file, isa, is_stmt ? " is_stmt" : "",
              basic_block ? " basic_block" : "",
              prologue_end ? " prologue_end" : "",
              epilogue_begin ? " epilogue_begin" : "",
              end_sequence ? " end_sequence" : "");
}

//----------------------------------------------------------------------
// Compare function LineTable structures
//----------------------------------------------------------------------
static bool AddressLessThan(const DWARFDebugLine::Row &a,
                            const DWARFDebugLine::Row &b) {
  return a.address < b.address;
}

// Insert a row at the correct address if the addresses can be out of order
// which can only happen when we are linking a line table that may have had
// it's contents rearranged.
void DWARFDebugLine::Row::Insert(Row::collection &state_coll,
                                 const Row &state) {
  // If we don't have anything yet, or if the address of the last state in our
  // line table is less than the current one, just append the current state
  if (state_coll.empty() || AddressLessThan(state_coll.back(), state)) {
    state_coll.push_back(state);
  } else {
    // Do a binary search for the correct entry
    pair<Row::iterator, Row::iterator> range(equal_range(
        state_coll.begin(), state_coll.end(), state, AddressLessThan));

    // If the addresses are equal, we can safely replace the previous entry
    // with the current one if the one it is replacing is an end_sequence
    // entry. We currently always place an extra end sequence when ever we exit
    // a valid address range for a function in case the functions get
    // rearranged by optimizations or by order specifications. These extra end
    // sequences will disappear by getting replaced with valid consecutive
    // entries within a compile unit if there are no gaps.
    if (range.first == range.second) {
      state_coll.insert(range.first, state);
    } else {
      if ((distance(range.first, range.second) == 1) &&
          range.first->end_sequence == true) {
        *range.first = state;
      } else {
        state_coll.insert(range.second, state);
      }
    }
  }
}

void DWARFDebugLine::Row::Dump(Log *log, const Row::collection &state_coll) {
  std::for_each(state_coll.begin(), state_coll.end(),
                bind2nd(std::mem_fun_ref(&Row::Dump), log));
}

//----------------------------------------------------------------------
// DWARFDebugLine::State::State
//----------------------------------------------------------------------
DWARFDebugLine::State::State(Prologue::shared_ptr &p, Log *l,
                             DWARFDebugLine::State::Callback cb, void *userData)
    : Row(p->default_is_stmt), prologue(p), log(l), callback(cb),
      callbackUserData(userData), row(StartParsingLineTable) {
  // Call the callback with the initial row state of zero for the prologue
  if (callback)
    callback(0, *this, callbackUserData);
}

//----------------------------------------------------------------------
// DWARFDebugLine::State::Reset
//----------------------------------------------------------------------
void DWARFDebugLine::State::Reset() { Row::Reset(prologue->default_is_stmt); }

//----------------------------------------------------------------------
// DWARFDebugLine::State::AppendRowToMatrix
//----------------------------------------------------------------------
void DWARFDebugLine::State::AppendRowToMatrix(dw_offset_t offset) {
  // Each time we are to add an entry into the line table matrix call the
  // callback function so that someone can do something with the current state
  // of the state machine (like build a line table or dump the line table!)
  if (log) {
    if (row == 0) {
      log->PutCString("Address            Line   Column File   ISA Flags");
      log->PutCString(
          "------------------ ------ ------ ------ --- -------------");
    }
    Dump(log);
  }

  ++row; // Increase the row number before we call our callback for a real row
  if (callback)
    callback(offset, *this, callbackUserData);
  PostAppend();
}

//----------------------------------------------------------------------
// DWARFDebugLine::State::Finalize
//----------------------------------------------------------------------
void DWARFDebugLine::State::Finalize(dw_offset_t offset) {
  // Call the callback with a special row state when we are done parsing a line
  // table
  row = DoneParsingLineTable;
  if (callback)
    callback(offset, *this, callbackUserData);
}

// void
// DWARFDebugLine::AppendLineTableData
//(
//  const DWARFDebugLine::Prologue* prologue,
//  const DWARFDebugLine::Row::collection& state_coll,
//  const uint32_t addr_size,
//  BinaryStreamBuf &debug_line_data
//)
//{
//  if (state_coll.empty())
//  {
//      // We have no entries, just make an empty line table
//      debug_line_data.Append8(0);
//      debug_line_data.Append8(1);
//      debug_line_data.Append8(DW_LNE_end_sequence);
//  }
//  else
//  {
//      DWARFDebugLine::Row::const_iterator pos;
//      Row::const_iterator end = state_coll.end();
//      bool default_is_stmt = prologue->default_is_stmt;
//      const DWARFDebugLine::Row reset_state(default_is_stmt);
//      const DWARFDebugLine::Row* prev_state = &reset_state;
//      const int32_t max_line_increment_for_special_opcode =
//      prologue->MaxLineIncrementForSpecialOpcode();
//      for (pos = state_coll.begin(); pos != end; ++pos)
//      {
//          const DWARFDebugLine::Row& curr_state = *pos;
//          int32_t line_increment  = 0;
//          dw_addr_t addr_offset   = curr_state.address - prev_state->address;
//          dw_addr_t addr_advance  = (addr_offset) / prologue->min_inst_length;
//          line_increment = (int32_t)(curr_state.line - prev_state->line);
//
//          // If our previous state was the reset state, then let's emit the
//          // address to keep GDB's DWARF parser happy. If we don't start each
//          // sequence with a DW_LNE_set_address opcode, the line table won't
//          // get slid properly in GDB.
//
//          if (prev_state == &reset_state)
//          {
//              debug_line_data.Append8(0); // Extended opcode
//              debug_line_data.Append32_as_ULEB128(addr_size + 1); // Length of
//              opcode bytes
//              debug_line_data.Append8(DW_LNE_set_address);
//              debug_line_data.AppendMax64(curr_state.address, addr_size);
//              addr_advance = 0;
//          }
//
//          if (prev_state->file != curr_state.file)
//          {
//              debug_line_data.Append8(DW_LNS_set_file);
//              debug_line_data.Append32_as_ULEB128(curr_state.file);
//          }
//
//          if (prev_state->column != curr_state.column)
//          {
//              debug_line_data.Append8(DW_LNS_set_column);
//              debug_line_data.Append32_as_ULEB128(curr_state.column);
//          }
//
//          // Don't do anything fancy if we are at the end of a sequence
//          // as we don't want to push any extra rows since the
//          DW_LNE_end_sequence
//          // will push a row itself!
//          if (curr_state.end_sequence)
//          {
//              if (line_increment != 0)
//              {
//                  debug_line_data.Append8(DW_LNS_advance_line);
//                  debug_line_data.Append32_as_SLEB128(line_increment);
//              }
//
//              if (addr_advance > 0)
//              {
//                  debug_line_data.Append8(DW_LNS_advance_pc);
//                  debug_line_data.Append32_as_ULEB128(addr_advance);
//              }
//
//              // Now push the end sequence on!
//              debug_line_data.Append8(0);
//              debug_line_data.Append8(1);
//              debug_line_data.Append8(DW_LNE_end_sequence);
//
//              prev_state = &reset_state;
//          }
//          else
//          {
//              if (line_increment || addr_advance)
//              {
//                  if (line_increment > max_line_increment_for_special_opcode)
//                  {
//                      debug_line_data.Append8(DW_LNS_advance_line);
//                      debug_line_data.Append32_as_SLEB128(line_increment);
//                      line_increment = 0;
//                  }
//
//                  uint32_t special_opcode = (line_increment >=
//                  prologue->line_base) ? ((line_increment -
//                  prologue->line_base) + (prologue->line_range * addr_advance)
//                  + prologue->opcode_base) : 256;
//                  if (special_opcode > 255)
//                  {
//                      // Both the address and line won't fit in one special
//                      opcode
//                      // check to see if just the line advance will?
//                      uint32_t special_opcode_line = ((line_increment >=
//                      prologue->line_base) && (line_increment != 0)) ?
//                              ((line_increment - prologue->line_base) +
//                              prologue->opcode_base) : 256;
//
//
//                      if (special_opcode_line > 255)
//                      {
//                          // Nope, the line advance won't fit by itself, check
//                          the address increment by itself
//                          uint32_t special_opcode_addr = addr_advance ?
//                              ((0 - prologue->line_base) +
//                              (prologue->line_range * addr_advance) +
//                              prologue->opcode_base) : 256;
//
//                          if (special_opcode_addr > 255)
//                          {
//                              // Neither the address nor the line will fit in
//                              a
//                              // special opcode, we must manually enter both
//                              then
//                              // do a DW_LNS_copy to push a row (special
//                              opcode
//                              // automatically imply a new row is pushed)
//                              if (line_increment != 0)
//                              {
//                                  debug_line_data.Append8(DW_LNS_advance_line);
//                                  debug_line_data.Append32_as_SLEB128(line_increment);
//                              }
//
//                              if (addr_advance > 0)
//                              {
//                                  debug_line_data.Append8(DW_LNS_advance_pc);
//                                  debug_line_data.Append32_as_ULEB128(addr_advance);
//                              }
//
//                              // Now push a row onto the line table manually
//                              debug_line_data.Append8(DW_LNS_copy);
//
//                          }
//                          else
//                          {
//                              // The address increment alone will fit into a
//                              special opcode
//                              // so modify our line change, then issue a
//                              special opcode
//                              // for the address increment and it will push a
//                              row into the
//                              // line table
//                              if (line_increment != 0)
//                              {
//                                  debug_line_data.Append8(DW_LNS_advance_line);
//                                  debug_line_data.Append32_as_SLEB128(line_increment);
//                              }
//
//                              // Advance of line and address will fit into a
//                              single byte special opcode
//                              // and this will also push a row onto the line
//                              table
//                              debug_line_data.Append8(special_opcode_addr);
//                          }
//                      }
//                      else
//                      {
//                          // The line change alone will fit into a special
//                          opcode
//                          // so modify our address increment first, then issue
//                          a
//                          // special opcode for the line change and it will
//                          push
//                          // a row into the line table
//                          if (addr_advance > 0)
//                          {
//                              debug_line_data.Append8(DW_LNS_advance_pc);
//                              debug_line_data.Append32_as_ULEB128(addr_advance);
//                          }
//
//                          // Advance of line and address will fit into a
//                          single byte special opcode
//                          // and this will also push a row onto the line table
//                          debug_line_data.Append8(special_opcode_line);
//                      }
//                  }
//                  else
//                  {
//                      // Advance of line and address will fit into a single
//                      byte special opcode
//                      // and this will also push a row onto the line table
//                      debug_line_data.Append8(special_opcode);
//                  }
//              }
//              prev_state = &curr_state;
//          }
//      }
//  }
//}
