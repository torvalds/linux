//===-- Declaration.cpp -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/Declaration.h"
#include "lldb/Utility/Stream.h"

using namespace lldb_private;

void Declaration::Dump(Stream *s, bool show_fullpaths) const {
  if (m_file) {
    *s << ", decl = ";
    if (show_fullpaths)
      *s << m_file;
    else
      *s << m_file.GetFilename();
    if (m_line > 0)
      s->Printf(":%u", m_line);
#ifdef LLDB_ENABLE_DECLARATION_COLUMNS
    if (m_column > 0)
      s->Printf(":%u", m_column);
#endif
  } else {
    if (m_line > 0) {
      s->Printf(", line = %u", m_line);
#ifdef LLDB_ENABLE_DECLARATION_COLUMNS
      if (m_column > 0)
        s->Printf(":%u", m_column);
#endif
    }
#ifdef LLDB_ENABLE_DECLARATION_COLUMNS
    else if (m_column > 0)
      s->Printf(", column = %u", m_column);
#endif
  }
}

bool Declaration::DumpStopContext(Stream *s, bool show_fullpaths) const {
  if (m_file) {
    if (show_fullpaths)
      *s << m_file;
    else
      m_file.GetFilename().Dump(s);

    if (m_line > 0)
      s->Printf(":%u", m_line);
#ifdef LLDB_ENABLE_DECLARATION_COLUMNS
    if (m_column > 0)
      s->Printf(":%u", m_column);
#endif
    return true;
  } else if (m_line > 0) {
    s->Printf(" line %u", m_line);
#ifdef LLDB_ENABLE_DECLARATION_COLUMNS
    if (m_column > 0)
      s->Printf(":%u", m_column);
#endif
    return true;
  }
  return false;
}

size_t Declaration::MemorySize() const { return sizeof(Declaration); }

int Declaration::Compare(const Declaration &a, const Declaration &b) {
  int result = FileSpec::Compare(a.m_file, b.m_file, true);
  if (result)
    return result;
  if (a.m_line < b.m_line)
    return -1;
  else if (a.m_line > b.m_line)
    return 1;
#ifdef LLDB_ENABLE_DECLARATION_COLUMNS
  if (a.m_column < b.m_column)
    return -1;
  else if (a.m_column > b.m_column)
    return 1;
#endif
  return 0;
}

bool lldb_private::operator==(const Declaration &lhs, const Declaration &rhs) {
#ifdef LLDB_ENABLE_DECLARATION_COLUMNS
  if (lhs.GetColumn() == rhs.GetColumn())
    if (lhs.GetLine() == rhs.GetLine())
      return lhs.GetFile() == rhs.GetFile();
#else
  if (lhs.GetLine() == rhs.GetLine())
    return FileSpec::Equal(lhs.GetFile(), rhs.GetFile(), true);
#endif
  return false;
}
