//===-- SBSourceManager.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SBSourceManager_h_
#define LLDB_SBSourceManager_h_

#include "lldb/API/SBDefines.h"

#include <stdio.h>

namespace lldb {

class LLDB_API SBSourceManager {
public:
  SBSourceManager(const SBDebugger &debugger);
  SBSourceManager(const SBTarget &target);
  SBSourceManager(const SBSourceManager &rhs);

  ~SBSourceManager();

  const lldb::SBSourceManager &operator=(const lldb::SBSourceManager &rhs);

  size_t DisplaySourceLinesWithLineNumbers(
      const lldb::SBFileSpec &file, uint32_t line, uint32_t context_before,
      uint32_t context_after, const char *current_line_cstr, lldb::SBStream &s);

  size_t DisplaySourceLinesWithLineNumbersAndColumn(
      const lldb::SBFileSpec &file, uint32_t line, uint32_t column,
      uint32_t context_before, uint32_t context_after,
      const char *current_line_cstr, lldb::SBStream &s);

protected:
  friend class SBCommandInterpreter;
  friend class SBDebugger;

  SBSourceManager(lldb_private::SourceManager *source_manager);

private:
  std::unique_ptr<lldb_private::SourceManagerImpl> m_opaque_ap;
};

} // namespace lldb

#endif // LLDB_SBSourceManager_h_
