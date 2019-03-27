//===-- FileLineResolver.h --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_FileLineResolver_h_
#define liblldb_FileLineResolver_h_

#include "lldb/Core/SearchFilter.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/lldb-defines.h"

#include <stdint.h>

namespace lldb_private {
class Address;
}
namespace lldb_private {
class Stream;
}

namespace lldb_private {

//----------------------------------------------------------------------
/// @class FileLineResolver FileLineResolver.h "lldb/Core/FileLineResolver.h"
/// This class finds address for source file and line.  Optionally, it will
/// look for inlined instances of the file and line specification.
//----------------------------------------------------------------------

class FileLineResolver : public Searcher {
public:
  FileLineResolver()
      : m_file_spec(),
        m_line_number(UINT32_MAX), // Set this to zero for all lines in a file
        m_sc_list(), m_inlines(true) {}

  FileLineResolver(const FileSpec &resolver, uint32_t line_no,
                   bool check_inlines);

  ~FileLineResolver() override;

  Searcher::CallbackReturn SearchCallback(SearchFilter &filter,
                                          SymbolContext &context, Address *addr,
                                          bool containing) override;

  lldb::SearchDepth GetDepth() override;

  void GetDescription(Stream *s) override;

  const SymbolContextList &GetFileLineMatches() { return m_sc_list; }

  void Clear();

  void Reset(const FileSpec &file_spec, uint32_t line, bool check_inlines);

protected:
  FileSpec m_file_spec;   // This is the file spec we are looking for.
  uint32_t m_line_number; // This is the line number that we are looking for.
  SymbolContextList m_sc_list;
  bool m_inlines; // This determines whether the resolver looks for inlined
                  // functions or not.

private:
  DISALLOW_COPY_AND_ASSIGN(FileLineResolver);
};

} // namespace lldb_private

#endif // liblldb_FileLineResolver_h_
