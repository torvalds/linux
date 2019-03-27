//===-- LineEntry.h ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_LineEntry_h_
#define liblldb_LineEntry_h_

#include "lldb/Core/AddressRange.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

//----------------------------------------------------------------------
/// @class LineEntry LineEntry.h "lldb/Symbol/LineEntry.h"
/// A line table entry class.
//----------------------------------------------------------------------
struct LineEntry {
  //------------------------------------------------------------------
  /// Default constructor.
  ///
  /// Initialize all member variables to invalid values.
  //------------------------------------------------------------------
  LineEntry();

  LineEntry(const lldb::SectionSP &section_sp, lldb::addr_t section_offset,
            lldb::addr_t byte_size, const FileSpec &file, uint32_t _line,
            uint16_t _column, bool _is_start_of_statement,
            bool _is_start_of_basic_block, bool _is_prologue_end,
            bool _is_epilogue_begin, bool _is_terminal_entry);

  //------------------------------------------------------------------
  /// Clear the object's state.
  ///
  /// Clears all member variables to invalid values.
  //------------------------------------------------------------------
  void Clear();

  //------------------------------------------------------------------
  /// Dump a description of this object to a Stream.
  ///
  /// Dump a description of the contents of this object to the supplied stream
  /// \a s.
  ///
  /// @param[in] s
  ///     The stream to which to dump the object description.
  ///
  /// @param[in] comp_unit
  ///     The compile unit object that contains the support file
  ///     list so the line entry can dump the file name (since this
  ///     object contains a file index into the support file list).
  ///
  /// @param[in] show_file
  ///     If \b true, display the filename with the line entry which
  ///     requires that the compile unit object \a comp_unit be a
  ///     valid pointer.
  ///
  /// @param[in] style
  ///     The display style for the section offset address.
  ///
  /// @return
  ///     Returns \b true if the address was able to be displayed
  ///     using \a style. File and load addresses may be unresolved
  ///     and it may not be possible to display a valid address value.
  ///     Returns \b false if the address was not able to be properly
  ///     dumped.
  ///
  /// @see Address::DumpStyle
  //------------------------------------------------------------------
  bool Dump(Stream *s, Target *target, bool show_file, Address::DumpStyle style,
            Address::DumpStyle fallback_style, bool show_range) const;

  bool GetDescription(Stream *s, lldb::DescriptionLevel level, CompileUnit *cu,
                      Target *target, bool show_address_only) const;

  //------------------------------------------------------------------
  /// Dumps information specific to a process that stops at this line entry to
  /// the supplied stream \a s.
  ///
  /// @param[in] s
  ///     The stream to which to dump the object description.
  ///
  /// @param[in] comp_unit
  ///     The compile unit object that contains the support file
  ///     list so the line entry can dump the file name (since this
  ///     object contains a file index into the support file list).
  ///
  /// @return
  ///     Returns \b true if the file and line were properly dumped,
  ///     \b false otherwise.
  //------------------------------------------------------------------
  bool DumpStopContext(Stream *s, bool show_fullpaths) const;

  //------------------------------------------------------------------
  /// Check if a line entry object is valid.
  ///
  /// @return
  ///     Returns \b true if the line entry contains a valid section
  ///     offset address, file index, and line number, \b false
  ///     otherwise.
  //------------------------------------------------------------------
  bool IsValid() const;

  //------------------------------------------------------------------
  /// Compare two LineEntry objects.
  ///
  /// @param[in] lhs
  ///     The Left Hand Side const LineEntry object reference.
  ///
  /// @param[in] rhs
  ///     The Right Hand Side const LineEntry object reference.
  ///
  /// @return
  ///     @li -1 if lhs < rhs
  ///     @li 0 if lhs == rhs
  ///     @li 1 if lhs > rhs
  //------------------------------------------------------------------
  static int Compare(const LineEntry &lhs, const LineEntry &rhs);

  //------------------------------------------------------------------
  /// Give the range for this LineEntry + any additional LineEntries for this
  /// same source line that are contiguous.
  ///
  /// A compiler may emit multiple line entries for a single source line,
  /// e.g. to indicate subexpressions at different columns.  This method will
  /// get the AddressRange for all of the LineEntries for this source line
  /// that are contiguous.
  //
  /// Line entries with a line number of 0 are treated specially - these are
  /// compiler-generated line table entries that the user did not write in
  /// their source code, and we want to skip past in the debugger. If this
  /// LineEntry is for line 32, and the following LineEntry is for line 0, we
  /// will extend the range to include the AddressRange of the line 0
  /// LineEntry (and it will include the range of the following LineEntries
  /// that match either 32 or 0.)
  ///
  /// If the initial LineEntry this method is called on is a line #0, only the
  /// range of contiuous LineEntries with line #0 will be included in the
  /// complete range.
  ///
  /// @return
  ///     The contiguous AddressRange for this source line.
  //------------------------------------------------------------------
  AddressRange GetSameLineContiguousAddressRange() const;

  //------------------------------------------------------------------
  /// Apply file mappings from target.source-map to the LineEntry's file.
  ///
  /// @param[in] target_sp
  ///     Shared pointer to the target this LineEntry belongs to.
  //------------------------------------------------------------------

  void ApplyFileMappings(lldb::TargetSP target_sp);

  //------------------------------------------------------------------
  // Member variables.
  //------------------------------------------------------------------
  AddressRange range; ///< The section offset address range for this line entry.
  FileSpec file; ///< The source file, possibly mapped by the target.source-map
                 ///setting
  FileSpec original_file; ///< The original source file, from debug info.
  uint32_t line; ///< The source line number, or zero if there is no line number
                 ///information.
  uint16_t column; ///< The column number of the source line, or zero if there
                   ///is no column information.
  uint16_t is_start_of_statement : 1, ///< Indicates this entry is the beginning
                                      ///of a statement.
      is_start_of_basic_block : 1, ///< Indicates this entry is the beginning of
                                   ///a basic block.
      is_prologue_end : 1,   ///< Indicates this entry is one (of possibly many)
                             ///where execution should be suspended for an entry
                             ///breakpoint of a function.
      is_epilogue_begin : 1, ///< Indicates this entry is one (of possibly many)
                             ///where execution should be suspended for an exit
                             ///breakpoint of a function.
      is_terminal_entry : 1; ///< Indicates this entry is that of the first byte
                             ///after the end of a sequence of target machine
                             ///instructions.
};

//------------------------------------------------------------------
/// Less than operator.
///
/// @param[in] lhs
///     The Left Hand Side const LineEntry object reference.
///
/// @param[in] rhs
///     The Right Hand Side const LineEntry object reference.
///
/// @return
///     Returns \b true if lhs < rhs, false otherwise.
//------------------------------------------------------------------
bool operator<(const LineEntry &lhs, const LineEntry &rhs);

} // namespace lldb_private

#endif // liblldb_LineEntry_h_
