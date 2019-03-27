//===-- DebugMacros.h -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_DebugMacros_h_
#define liblldb_DebugMacros_h_

#include <memory>
#include <vector>

#include "lldb/Utility/ConstString.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class CompileUnit;
class DebugMacros;
typedef std::shared_ptr<DebugMacros> DebugMacrosSP;

class DebugMacroEntry {
public:
  enum EntryType { INVALID, DEFINE, UNDEF, START_FILE, END_FILE, INDIRECT };

public:
  static DebugMacroEntry CreateDefineEntry(uint32_t line, const char *str);

  static DebugMacroEntry CreateUndefEntry(uint32_t line, const char *str);

  static DebugMacroEntry CreateStartFileEntry(uint32_t line,
                                              uint32_t debug_line_file_idx);

  static DebugMacroEntry CreateEndFileEntry();

  static DebugMacroEntry
  CreateIndirectEntry(const DebugMacrosSP &debug_macros_sp);

  DebugMacroEntry() : m_type(INVALID) {}

  ~DebugMacroEntry() = default;

  EntryType GetType() const { return m_type; }

  uint64_t GetLineNumber() const { return m_line; }

  ConstString GetMacroString() const { return m_str; }

  const FileSpec &GetFileSpec(CompileUnit *comp_unit) const;

  DebugMacros *GetIndirectDebugMacros() const {
    return m_debug_macros_sp.get();
  }

private:
  DebugMacroEntry(EntryType type, uint32_t line, uint32_t debug_line_file_idx,
                  const char *str);

  DebugMacroEntry(EntryType type, const DebugMacrosSP &debug_macros_sp);

  EntryType m_type : 3;
  uint32_t m_line : 29;
  uint32_t m_debug_line_file_idx;
  ConstString m_str;
  DebugMacrosSP m_debug_macros_sp;
};

class DebugMacros {
public:
  DebugMacros() = default;

  ~DebugMacros() = default;

  void AddMacroEntry(const DebugMacroEntry &entry) {
    m_macro_entries.push_back(entry);
  }

  size_t GetNumMacroEntries() const { return m_macro_entries.size(); }

  DebugMacroEntry GetMacroEntryAtIndex(const size_t index) const {
    if (index < m_macro_entries.size())
      return m_macro_entries[index];
    else
      return DebugMacroEntry();
  }

private:
  DISALLOW_COPY_AND_ASSIGN(DebugMacros);

  std::vector<DebugMacroEntry> m_macro_entries;
};

} // namespace lldb_private

#endif // liblldb_DebugMacros_h_
