//===-- Declaration.h -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Declaration_h_
#define liblldb_Declaration_h_

#include "lldb/Utility/FileSpec.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

//----------------------------------------------------------------------
/// @class Declaration Declaration.h "lldb/Symbol/Declaration.h"
/// A class that describes the declaration location of a
///        lldb object.
///
/// The declarations include the file specification, line number, and the
/// column info and can help track where functions, blocks, inlined functions,
/// types, variables, any many other debug core objects were declared.
//----------------------------------------------------------------------
class Declaration {
public:
  //------------------------------------------------------------------
  /// Default constructor.
  //------------------------------------------------------------------
  Declaration()
      : m_file(), m_line(0)
#ifdef LLDB_ENABLE_DECLARATION_COLUMNS
        ,
        m_column(0)
#endif
  {
  }

  //------------------------------------------------------------------
  /// Construct with file specification, and optional line and column.
  ///
  /// @param[in] file_spec
  ///     The file specification that describes where this was
  ///     declared.
  ///
  /// @param[in] line
  ///     The line number that describes where this was declared. Set
  ///     to zero if there is no line number information.
  ///
  /// @param[in] column
  ///     The column number that describes where this was declared.
  ///     Set to zero if there is no column number information.
  //------------------------------------------------------------------
  Declaration(const FileSpec &file_spec, uint32_t line = 0, uint32_t column = 0)
      : m_file(file_spec), m_line(line)
#ifdef LLDB_ENABLE_DECLARATION_COLUMNS
        ,
        m_column(column)
#endif
  {
  }

  //------------------------------------------------------------------
  /// Construct with a reference to another Declaration object.
  //------------------------------------------------------------------
  Declaration(const Declaration &rhs)
      : m_file(rhs.m_file), m_line(rhs.m_line)
#ifdef LLDB_ENABLE_DECLARATION_COLUMNS
        ,
        m_column(rhs.m_column)
#endif
  {
  }

  //------------------------------------------------------------------
  /// Construct with a pointer to another Declaration object.
  //------------------------------------------------------------------
  Declaration(const Declaration *decl_ptr)
      : m_file(), m_line(0)
#ifdef LLDB_ENABLE_DECLARATION_COLUMNS
        ,
        m_column(0)
#endif
  {
    if (decl_ptr)
      *this = *decl_ptr;
  }

  //------------------------------------------------------------------
  /// Clear the object's state.
  ///
  /// Sets the file specification to be empty, and the line and column to
  /// zero.
  //------------------------------------------------------------------
  void Clear() {
    m_file.Clear();
    m_line = 0;
#ifdef LLDB_ENABLE_DECLARATION_COLUMNS
    m_column = 0;
#endif
  }

  //------------------------------------------------------------------
  /// Compare two declaration objects.
  ///
  /// Compares the two file specifications from \a lhs and \a rhs. If the file
  /// specifications are equal, then continue to compare the line number and
  /// column numbers respectively.
  ///
  /// @param[in] lhs
  ///     The Left Hand Side const Declaration object reference.
  ///
  /// @param[in] rhs
  ///     The Right Hand Side const Declaration object reference.
  ///
  /// @return
  ///     @li -1 if lhs < rhs
  ///     @li 0 if lhs == rhs
  ///     @li 1 if lhs > rhs
  //------------------------------------------------------------------
  static int Compare(const Declaration &lhs, const Declaration &rhs);

  //------------------------------------------------------------------
  /// Dump a description of this object to a Stream.
  ///
  /// Dump a description of the contents of this object to the supplied stream
  /// \a s.
  ///
  /// @param[in] s
  ///     The stream to which to dump the object description.
  //------------------------------------------------------------------
  void Dump(Stream *s, bool show_fullpaths) const;

  bool DumpStopContext(Stream *s, bool show_fullpaths) const;
  //------------------------------------------------------------------
  /// Get accessor for the declaration column number.
  ///
  /// @return
  ///     Non-zero indicates a valid column number, zero indicates no
  ///     column information is available.
  //------------------------------------------------------------------
  uint32_t GetColumn() const {
#ifdef LLDB_ENABLE_DECLARATION_COLUMNS
    return m_column;
#else
    return 0;
#endif
  }

  //------------------------------------------------------------------
  /// Get accessor for file specification.
  ///
  /// @return
  ///     A reference to the file specification object.
  //------------------------------------------------------------------
  FileSpec &GetFile() { return m_file; }

  //------------------------------------------------------------------
  /// Get const accessor for file specification.
  ///
  /// @return
  ///     A const reference to the file specification object.
  //------------------------------------------------------------------
  const FileSpec &GetFile() const { return m_file; }

  //------------------------------------------------------------------
  /// Get accessor for the declaration line number.
  ///
  /// @return
  ///     Non-zero indicates a valid line number, zero indicates no
  ///     line information is available.
  //------------------------------------------------------------------
  uint32_t GetLine() const { return m_line; }

  bool IsValid() const { return m_file && m_line != 0; }

  //------------------------------------------------------------------
  /// Get the memory cost of this object.
  ///
  /// @return
  ///     The number of bytes that this object occupies in memory.
  ///     The returned value does not include the bytes for any
  ///     shared string values.
  ///
  /// @see ConstString::StaticMemorySize ()
  //------------------------------------------------------------------
  size_t MemorySize() const;

  //------------------------------------------------------------------
  /// Set accessor for the declaration column number.
  ///
  /// @param[in] column
  ///     Non-zero indicates a valid column number, zero indicates no
  ///     column information is available.
  //------------------------------------------------------------------
  void SetColumn(uint32_t column) {
#ifdef LLDB_ENABLE_DECLARATION_COLUMNS
    m_column = col;
#endif
  }

  //------------------------------------------------------------------
  /// Set accessor for the declaration file specification.
  ///
  /// @param[in] file_spec
  ///     The new declaration file specification.
  //------------------------------------------------------------------
  void SetFile(const FileSpec &file_spec) { m_file = file_spec; }

  //------------------------------------------------------------------
  /// Set accessor for the declaration line number.
  ///
  /// @param[in] line
  ///     Non-zero indicates a valid line number, zero indicates no
  ///     line information is available.
  //------------------------------------------------------------------
  void SetLine(uint32_t line) { m_line = line; }

protected:
  //------------------------------------------------------------------
  /// Member variables.
  //------------------------------------------------------------------
  FileSpec m_file; ///< The file specification that points to the
                   ///< source file where the declaration occurred.
  uint32_t m_line; ///< Non-zero values indicates a valid line number,
                   ///< zero indicates no line number information is available.
#ifdef LLDB_ENABLE_DECLARATION_COLUMNS
  uint32_t m_column; ///< Non-zero values indicates a valid column number,
                     ///< zero indicates no column information is available.
#endif
};

bool operator==(const Declaration &lhs, const Declaration &rhs);

} // namespace lldb_private

#endif // liblldb_Declaration_h_
