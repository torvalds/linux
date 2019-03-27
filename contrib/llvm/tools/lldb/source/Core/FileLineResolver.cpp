//===-- FileLineResolver.cpp ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/FileLineResolver.h"

#include "lldb/Core/FileSpecList.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/LineTable.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Stream.h"

#include <string>

namespace lldb_private {
class Address;
}

using namespace lldb;
using namespace lldb_private;

//----------------------------------------------------------------------
// FileLineResolver:
//----------------------------------------------------------------------
FileLineResolver::FileLineResolver(const FileSpec &file_spec, uint32_t line_no,
                                   bool check_inlines)
    : Searcher(), m_file_spec(file_spec), m_line_number(line_no),
      m_inlines(check_inlines) {}

FileLineResolver::~FileLineResolver() {}

Searcher::CallbackReturn
FileLineResolver::SearchCallback(SearchFilter &filter, SymbolContext &context,
                                 Address *addr, bool containing) {
  CompileUnit *cu = context.comp_unit;

  if (m_inlines ||
      m_file_spec.Compare(*cu, m_file_spec, (bool)m_file_spec.GetDirectory())) {
    uint32_t start_file_idx = 0;
    uint32_t file_idx =
        cu->GetSupportFiles().FindFileIndex(start_file_idx, m_file_spec, false);
    if (file_idx != UINT32_MAX) {
      LineTable *line_table = cu->GetLineTable();
      if (line_table) {
        if (m_line_number == 0) {
          // Match all lines in a file...
          const bool append = true;
          while (file_idx != UINT32_MAX) {
            line_table->FineLineEntriesForFileIndex(file_idx, append,
                                                    m_sc_list);
            // Get the next file index in case we have multiple file entries
            // for the same file
            file_idx = cu->GetSupportFiles().FindFileIndex(file_idx + 1,
                                                           m_file_spec, false);
          }
        } else {
          // Match a specific line in a file...
        }
      }
    }
  }
  return Searcher::eCallbackReturnContinue;
}

lldb::SearchDepth FileLineResolver::GetDepth() {
  return lldb::eSearchDepthCompUnit;
}

void FileLineResolver::GetDescription(Stream *s) {
  s->Printf("File and line resolver for file: \"%s\" line: %u",
            m_file_spec.GetPath().c_str(), m_line_number);
}

void FileLineResolver::Clear() {
  m_file_spec.Clear();
  m_line_number = UINT32_MAX;
  m_sc_list.Clear();
  m_inlines = true;
}

void FileLineResolver::Reset(const FileSpec &file_spec, uint32_t line,
                             bool check_inlines) {
  m_file_spec = file_spec;
  m_line_number = line;
  m_sc_list.Clear();
  m_inlines = check_inlines;
}
