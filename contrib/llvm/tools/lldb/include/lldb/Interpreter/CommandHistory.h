//===-- CommandHistory.h ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_CommandHistory_h_
#define liblldb_CommandHistory_h_

#include <mutex>
#include <string>
#include <vector>

#include "lldb/Utility/Stream.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class CommandHistory {
public:
  CommandHistory();

  ~CommandHistory();

  size_t GetSize() const;

  bool IsEmpty() const;

  llvm::Optional<llvm::StringRef> FindString(llvm::StringRef input_str) const;

  llvm::StringRef GetStringAtIndex(size_t idx) const;

  llvm::StringRef operator[](size_t idx) const;

  llvm::StringRef GetRecentmostString() const;

  void AppendString(llvm::StringRef str, bool reject_if_dupe = true);

  void Clear();

  void Dump(Stream &stream, size_t start_idx = 0,
            size_t stop_idx = SIZE_MAX) const;

  static const char g_repeat_char = '!';

private:
  DISALLOW_COPY_AND_ASSIGN(CommandHistory);

  typedef std::vector<std::string> History;
  mutable std::recursive_mutex m_mutex;
  History m_history;
};

} // namespace lldb_private

#endif // liblldb_CommandHistory_h_
