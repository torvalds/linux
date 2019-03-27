//===-- LineEntry.cpp -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/LineEntry.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"

using namespace lldb_private;

LineEntry::LineEntry()
    : range(), file(), line(LLDB_INVALID_LINE_NUMBER), column(0),
      is_start_of_statement(0), is_start_of_basic_block(0), is_prologue_end(0),
      is_epilogue_begin(0), is_terminal_entry(0) {}

LineEntry::LineEntry(const lldb::SectionSP &section_sp,
                     lldb::addr_t section_offset, lldb::addr_t byte_size,
                     const FileSpec &_file, uint32_t _line, uint16_t _column,
                     bool _is_start_of_statement, bool _is_start_of_basic_block,
                     bool _is_prologue_end, bool _is_epilogue_begin,
                     bool _is_terminal_entry)
    : range(section_sp, section_offset, byte_size), file(_file),
      original_file(_file), line(_line), column(_column),
      is_start_of_statement(_is_start_of_statement),
      is_start_of_basic_block(_is_start_of_basic_block),
      is_prologue_end(_is_prologue_end), is_epilogue_begin(_is_epilogue_begin),
      is_terminal_entry(_is_terminal_entry) {}

void LineEntry::Clear() {
  range.Clear();
  file.Clear();
  original_file.Clear();
  line = LLDB_INVALID_LINE_NUMBER;
  column = 0;
  is_start_of_statement = 0;
  is_start_of_basic_block = 0;
  is_prologue_end = 0;
  is_epilogue_begin = 0;
  is_terminal_entry = 0;
}

bool LineEntry::IsValid() const {
  return range.GetBaseAddress().IsValid() && line != LLDB_INVALID_LINE_NUMBER;
}

bool LineEntry::DumpStopContext(Stream *s, bool show_fullpaths) const {
  if (file) {
    if (show_fullpaths)
      file.Dump(s);
    else
      file.GetFilename().Dump(s);

    if (line)
      s->PutChar(':');
  }
  if (line) {
    s->Printf("%u", line);
    if (column) {
      s->PutChar(':');
      s->Printf("%u", column);
    }
  }
  return file || line;
}

bool LineEntry::Dump(Stream *s, Target *target, bool show_file,
                     Address::DumpStyle style,
                     Address::DumpStyle fallback_style, bool show_range) const {
  if (show_range) {
    // Show address range
    if (!range.Dump(s, target, style, fallback_style))
      return false;
  } else {
    // Show address only
    if (!range.GetBaseAddress().Dump(s, target, style, fallback_style))
      return false;
  }
  if (show_file)
    *s << ", file = " << file;
  if (line)
    s->Printf(", line = %u", line);
  if (column)
    s->Printf(", column = %u", column);
  if (is_start_of_statement)
    *s << ", is_start_of_statement = TRUE";

  if (is_start_of_basic_block)
    *s << ", is_start_of_basic_block = TRUE";

  if (is_prologue_end)
    *s << ", is_prologue_end = TRUE";

  if (is_epilogue_begin)
    *s << ", is_epilogue_begin = TRUE";

  if (is_terminal_entry)
    *s << ", is_terminal_entry = TRUE";
  return true;
}

bool LineEntry::GetDescription(Stream *s, lldb::DescriptionLevel level,
                               CompileUnit *cu, Target *target,
                               bool show_address_only) const {

  if (level == lldb::eDescriptionLevelBrief ||
      level == lldb::eDescriptionLevelFull) {
    if (show_address_only) {
      range.GetBaseAddress().Dump(s, target, Address::DumpStyleLoadAddress,
                                  Address::DumpStyleFileAddress);
    } else {
      range.Dump(s, target, Address::DumpStyleLoadAddress,
                 Address::DumpStyleFileAddress);
    }

    *s << ": " << file;

    if (line) {
      s->Printf(":%u", line);
      if (column)
        s->Printf(":%u", column);
    }

    if (level == lldb::eDescriptionLevelFull) {
      if (is_start_of_statement)
        *s << ", is_start_of_statement = TRUE";

      if (is_start_of_basic_block)
        *s << ", is_start_of_basic_block = TRUE";

      if (is_prologue_end)
        *s << ", is_prologue_end = TRUE";

      if (is_epilogue_begin)
        *s << ", is_epilogue_begin = TRUE";

      if (is_terminal_entry)
        *s << ", is_terminal_entry = TRUE";
    } else {
      if (is_terminal_entry)
        s->EOL();
    }
  } else {
    return Dump(s, target, true, Address::DumpStyleLoadAddress,
                Address::DumpStyleModuleWithFileAddress, true);
  }
  return true;
}

bool lldb_private::operator<(const LineEntry &a, const LineEntry &b) {
  return LineEntry::Compare(a, b) < 0;
}

int LineEntry::Compare(const LineEntry &a, const LineEntry &b) {
  int result = Address::CompareFileAddress(a.range.GetBaseAddress(),
                                           b.range.GetBaseAddress());
  if (result != 0)
    return result;

  const lldb::addr_t a_byte_size = a.range.GetByteSize();
  const lldb::addr_t b_byte_size = b.range.GetByteSize();

  if (a_byte_size < b_byte_size)
    return -1;
  if (a_byte_size > b_byte_size)
    return +1;

  // Check for an end sequence entry mismatch after we have determined that the
  // address values are equal. If one of the items is an end sequence, we don't
  // care about the line, file, or column info.
  if (a.is_terminal_entry > b.is_terminal_entry)
    return -1;
  if (a.is_terminal_entry < b.is_terminal_entry)
    return +1;

  if (a.line < b.line)
    return -1;
  if (a.line > b.line)
    return +1;

  if (a.column < b.column)
    return -1;
  if (a.column > b.column)
    return +1;

  return FileSpec::Compare(a.file, b.file, true);
}

AddressRange LineEntry::GetSameLineContiguousAddressRange() const {
  // Add each LineEntry's range to complete_line_range until we find a
  // different file / line number.
  AddressRange complete_line_range = range;

  while (true) {
    SymbolContext next_line_sc;
    Address range_end(complete_line_range.GetBaseAddress());
    range_end.Slide(complete_line_range.GetByteSize());
    range_end.CalculateSymbolContext(&next_line_sc,
                                     lldb::eSymbolContextLineEntry);

    if (next_line_sc.line_entry.IsValid() &&
        next_line_sc.line_entry.range.GetByteSize() > 0 &&
        original_file == next_line_sc.line_entry.original_file) {
      // Include any line 0 entries - they indicate that this is compiler-
      // generated code that does not correspond to user source code.
      if (next_line_sc.line_entry.line == 0) {
        complete_line_range.SetByteSize(
            complete_line_range.GetByteSize() +
            next_line_sc.line_entry.range.GetByteSize());
        continue;
      }

      if (line == next_line_sc.line_entry.line) {
        // next_line_sc is the same file & line as this LineEntry, so extend
        // our AddressRange by its size and continue to see if there are more
        // LineEntries that we can combine.
        complete_line_range.SetByteSize(
            complete_line_range.GetByteSize() +
            next_line_sc.line_entry.range.GetByteSize());
        continue;
      }
    }
    break;
  }
  return complete_line_range;
}

void LineEntry::ApplyFileMappings(lldb::TargetSP target_sp) {
  if (target_sp) {
    // Apply any file remappings to our file
    FileSpec new_file_spec;
    if (target_sp->GetSourcePathMap().FindFile(original_file, new_file_spec))
      file = new_file_spec;
  }
}
