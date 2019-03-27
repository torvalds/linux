//===-- LineTable.h ---------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_LineTable_h_
#define liblldb_LineTable_h_

#include <vector>

#include "lldb/Core/ModuleChild.h"
#include "lldb/Core/RangeMap.h"
#include "lldb/Core/Section.h"
#include "lldb/Symbol/LineEntry.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

//----------------------------------------------------------------------
/// @class LineSequence LineTable.h "lldb/Symbol/LineTable.h" An abstract base
/// class used during symbol table creation.
//----------------------------------------------------------------------
class LineSequence {
public:
  LineSequence();

  virtual ~LineSequence() = default;

  virtual void Clear() = 0;

private:
  DISALLOW_COPY_AND_ASSIGN(LineSequence);
};

//----------------------------------------------------------------------
/// @class LineTable LineTable.h "lldb/Symbol/LineTable.h"
/// A line table class.
//----------------------------------------------------------------------
class LineTable {
public:
  //------------------------------------------------------------------
  /// Construct with compile unit.
  ///
  /// @param[in] comp_unit
  ///     The compile unit to which this line table belongs.
  //------------------------------------------------------------------
  LineTable(CompileUnit *comp_unit);

  //------------------------------------------------------------------
  /// Destructor.
  //------------------------------------------------------------------
  ~LineTable();

  //------------------------------------------------------------------
  /// Adds a new line entry to this line table.
  ///
  /// All line entries are maintained in file address order.
  ///
  /// @param[in] line_entry
  ///     A const reference to a new line_entry to add to this line
  ///     table.
  ///
  /// @see Address::DumpStyle
  //------------------------------------------------------------------
  //  void
  //  AddLineEntry (const LineEntry& line_entry);

  // Called when you can't guarantee the addresses are in increasing order
  void InsertLineEntry(lldb::addr_t file_addr, uint32_t line, uint16_t column,
                       uint16_t file_idx, bool is_start_of_statement,
                       bool is_start_of_basic_block, bool is_prologue_end,
                       bool is_epilogue_begin, bool is_terminal_entry);

  // Used to instantiate the LineSequence helper class
  LineSequence *CreateLineSequenceContainer();

  // Append an entry to a caller-provided collection that will later be
  // inserted in this line table.
  void AppendLineEntryToSequence(LineSequence *sequence, lldb::addr_t file_addr,
                                 uint32_t line, uint16_t column,
                                 uint16_t file_idx, bool is_start_of_statement,
                                 bool is_start_of_basic_block,
                                 bool is_prologue_end, bool is_epilogue_begin,
                                 bool is_terminal_entry);

  // Insert a sequence of entries into this line table.
  void InsertSequence(LineSequence *sequence);

  //------------------------------------------------------------------
  /// Dump all line entries in this line table to the stream \a s.
  ///
  /// @param[in] s
  ///     The stream to which to dump the object description.
  ///
  /// @param[in] style
  ///     The display style for the address.
  ///
  /// @see Address::DumpStyle
  //------------------------------------------------------------------
  void Dump(Stream *s, Target *target, Address::DumpStyle style,
            Address::DumpStyle fallback_style, bool show_line_ranges);

  void GetDescription(Stream *s, Target *target, lldb::DescriptionLevel level);

  //------------------------------------------------------------------
  /// Find a line entry that contains the section offset address \a so_addr.
  ///
  /// @param[in] so_addr
  ///     A section offset address object containing the address we
  ///     are searching for.
  ///
  /// @param[out] line_entry
  ///     A copy of the line entry that was found if \b true is
  ///     returned, otherwise \a entry is left unmodified.
  ///
  /// @param[out] index_ptr
  ///     A pointer to a 32 bit integer that will get the actual line
  ///     entry index if it is not nullptr.
  ///
  /// @return
  ///     Returns \b true if \a so_addr is contained in a line entry
  ///     in this line table, \b false otherwise.
  //------------------------------------------------------------------
  bool FindLineEntryByAddress(const Address &so_addr, LineEntry &line_entry,
                              uint32_t *index_ptr = nullptr);

  //------------------------------------------------------------------
  /// Find a line entry index that has a matching file index and source line
  /// number.
  ///
  /// Finds the next line entry that has a matching \a file_idx and source
  /// line number \a line starting at the \a start_idx entries into the line
  /// entry collection.
  ///
  /// @param[in] start_idx
  ///     The number of entries to skip when starting the search.
  ///
  /// @param[out] file_idx
  ///     The file index to search for that should be found prior
  ///     to calling this function using the following functions:
  ///     CompileUnit::GetSupportFiles()
  ///     FileSpecList::FindFileIndex (uint32_t, const FileSpec &) const
  ///
  /// @param[in] line
  ///     The source line to match.
  ///
  /// @param[in] exact
  ///     If true, match only if you find a line entry exactly matching \a line.
  ///     If false, return the closest line entry greater than \a line.
  ///
  /// @param[out] line_entry
  ///     A reference to a line entry object that will get a copy of
  ///     the line entry if \b true is returned, otherwise \a
  ///     line_entry is left untouched.
  ///
  /// @return
  ///     Returns \b true if a matching line entry is found in this
  ///     line table, \b false otherwise.
  ///
  /// @see CompileUnit::GetSupportFiles()
  /// @see FileSpecList::FindFileIndex (uint32_t, const FileSpec &) const
  //------------------------------------------------------------------
  uint32_t FindLineEntryIndexByFileIndex(uint32_t start_idx, uint32_t file_idx,
                                         uint32_t line, bool exact,
                                         LineEntry *line_entry_ptr);

  uint32_t FindLineEntryIndexByFileIndex(
      uint32_t start_idx, const std::vector<uint32_t> &file_indexes,
      uint32_t line, bool exact, LineEntry *line_entry_ptr);

  size_t FineLineEntriesForFileIndex(uint32_t file_idx, bool append,
                                     SymbolContextList &sc_list);

  //------------------------------------------------------------------
  /// Get the line entry from the line table at index \a idx.
  ///
  /// @param[in] idx
  ///     An index into the line table entry collection.
  ///
  /// @return
  ///     A valid line entry if \a idx is a valid index, or an invalid
  ///     line entry if \a idx is not valid.
  ///
  /// @see LineTable::GetSize()
  /// @see LineEntry::IsValid() const
  //------------------------------------------------------------------
  bool GetLineEntryAtIndex(uint32_t idx, LineEntry &line_entry);

  //------------------------------------------------------------------
  /// Gets the size of the line table in number of line table entries.
  ///
  /// @return
  ///     The number of line table entries in this line table.
  //------------------------------------------------------------------
  uint32_t GetSize() const;

  typedef lldb_private::RangeArray<lldb::addr_t, lldb::addr_t, 32>
      FileAddressRanges;

  //------------------------------------------------------------------
  /// Gets all contiguous file address ranges for the entire line table.
  ///
  /// @param[out] file_ranges
  ///     A collection of file address ranges that will be filled in
  ///     by this function.
  ///
  /// @param[out] append
  ///     If \b true, then append to \a file_ranges, otherwise clear
  ///     \a file_ranges prior to adding any ranges.
  ///
  /// @return
  ///     The number of address ranges added to \a file_ranges
  //------------------------------------------------------------------
  size_t GetContiguousFileAddressRanges(FileAddressRanges &file_ranges,
                                        bool append);

  //------------------------------------------------------------------
  /// Given a file range link map, relink the current line table and return a
  /// fixed up line table.
  ///
  /// @param[out] file_range_map
  ///     A collection of file ranges that maps to new file ranges
  ///     that will be used when linking the line table.
  ///
  /// @return
  ///     A new line table if at least one line table entry was able
  ///     to be mapped.
  //------------------------------------------------------------------
  typedef RangeDataVector<lldb::addr_t, lldb::addr_t, lldb::addr_t>
      FileRangeMap;

  LineTable *LinkLineTable(const FileRangeMap &file_range_map);

protected:
  struct Entry {
    Entry()
        : file_addr(LLDB_INVALID_ADDRESS), line(0),
          is_start_of_statement(false), is_start_of_basic_block(false),
          is_prologue_end(false), is_epilogue_begin(false),
          is_terminal_entry(false), column(0), file_idx(0) {}

    Entry(lldb::addr_t _file_addr, uint32_t _line, uint16_t _column,
          uint16_t _file_idx, bool _is_start_of_statement,
          bool _is_start_of_basic_block, bool _is_prologue_end,
          bool _is_epilogue_begin, bool _is_terminal_entry)
        : file_addr(_file_addr), line(_line),
          is_start_of_statement(_is_start_of_statement),
          is_start_of_basic_block(_is_start_of_basic_block),
          is_prologue_end(_is_prologue_end),
          is_epilogue_begin(_is_epilogue_begin),
          is_terminal_entry(_is_terminal_entry), column(_column),
          file_idx(_file_idx) {}

    int bsearch_compare(const void *key, const void *arrmem);

    void Clear() {
      file_addr = LLDB_INVALID_ADDRESS;
      line = 0;
      column = 0;
      file_idx = 0;
      is_start_of_statement = false;
      is_start_of_basic_block = false;
      is_prologue_end = false;
      is_epilogue_begin = false;
      is_terminal_entry = false;
    }

    static int Compare(const Entry &lhs, const Entry &rhs) {
// Compare the sections before calling
#define SCALAR_COMPARE(a, b)                                                   \
  if (a < b)                                                                   \
    return -1;                                                                 \
  if (a > b)                                                                   \
  return +1
      SCALAR_COMPARE(lhs.file_addr, rhs.file_addr);
      SCALAR_COMPARE(lhs.line, rhs.line);
      SCALAR_COMPARE(lhs.column, rhs.column);
      SCALAR_COMPARE(lhs.is_start_of_statement, rhs.is_start_of_statement);
      SCALAR_COMPARE(lhs.is_start_of_basic_block, rhs.is_start_of_basic_block);
      // rhs and lhs reversed on purpose below.
      SCALAR_COMPARE(rhs.is_prologue_end, lhs.is_prologue_end);
      SCALAR_COMPARE(lhs.is_epilogue_begin, rhs.is_epilogue_begin);
      // rhs and lhs reversed on purpose below.
      SCALAR_COMPARE(rhs.is_terminal_entry, lhs.is_terminal_entry);
      SCALAR_COMPARE(lhs.file_idx, rhs.file_idx);
#undef SCALAR_COMPARE
      return 0;
    }

    class LessThanBinaryPredicate {
    public:
      LessThanBinaryPredicate(LineTable *line_table);
      bool operator()(const LineTable::Entry &, const LineTable::Entry &) const;

    protected:
      LineTable *m_line_table;
    };

    static bool EntryAddressLessThan(const Entry &lhs, const Entry &rhs) {
      return lhs.file_addr < rhs.file_addr;
    }

    //------------------------------------------------------------------
    // Member variables.
    //------------------------------------------------------------------
    /// The file address for this line entry.
    lldb::addr_t file_addr;
    /// The source line number, or zero if there is no line number
    /// information.
    uint32_t line : 27;
    /// Indicates this entry is the beginning of a statement.
    uint32_t is_start_of_statement : 1;
    /// Indicates this entry is the beginning of a basic block.
    uint32_t is_start_of_basic_block : 1;
    /// Indicates this entry is one (of possibly many) where execution
    /// should be suspended for an entry breakpoint of a function.
    uint32_t is_prologue_end : 1;
    /// Indicates this entry is one (of possibly many) where execution
    /// should be suspended for an exit breakpoint of a function.
    uint32_t is_epilogue_begin : 1;
    /// Indicates this entry is that of the first byte after the end
    /// of a sequence of target machine instructions.
    uint32_t is_terminal_entry : 1;
    /// The column number of the source line, or zero if there is no
    /// column information.
    uint16_t column;
    /// The file index into CompileUnit's file table, or zero if there
    /// is no file information.
    uint16_t file_idx;
  };

  struct EntrySearchInfo {
    LineTable *line_table;
    lldb_private::Section *a_section;
    Entry *a_entry;
  };

  //------------------------------------------------------------------
  // Types
  //------------------------------------------------------------------
  typedef std::vector<lldb_private::Section *>
      section_collection; ///< The collection type for the sections.
  typedef std::vector<Entry>
      entry_collection; ///< The collection type for the line entries.
  //------------------------------------------------------------------
  // Member variables.
  //------------------------------------------------------------------
  CompileUnit
      *m_comp_unit; ///< The compile unit that this line table belongs to.
  entry_collection
      m_entries; ///< The collection of line entries in this line table.

  //------------------------------------------------------------------
  // Helper class
  //------------------------------------------------------------------
  class LineSequenceImpl : public LineSequence {
  public:
    LineSequenceImpl() = default;

    ~LineSequenceImpl() override = default;

    void Clear() override;

    entry_collection
        m_entries; ///< The collection of line entries in this sequence.
  };

  bool ConvertEntryAtIndexToLineEntry(uint32_t idx, LineEntry &line_entry);

private:
  DISALLOW_COPY_AND_ASSIGN(LineTable);
};

} // namespace lldb_private

#endif // liblldb_LineTable_h_
