//===-- StringList.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_StringList_h_
#define liblldb_StringList_h_

#include "llvm/ADT/StringRef.h"

#include <stddef.h>
#include <string>
#include <vector>

namespace lldb_private {
class Log;
}
namespace lldb_private {
class Stream;
}

namespace lldb_private {

class StringList {
public:
  StringList();

  explicit StringList(const char *str);

  StringList(const char **strv, int strc);

  virtual ~StringList();

  void AppendString(const std::string &s);

  void AppendString(std::string &&s);

  void AppendString(const char *str);

  void AppendString(const char *str, size_t str_len);

  void AppendString(llvm::StringRef str);

  void AppendList(const char **strv, int strc);

  void AppendList(StringList strings);

  size_t GetSize() const;

  void SetSize(size_t n) { m_strings.resize(n); }

  size_t GetMaxStringLength() const;

  std::string &operator[](size_t idx) {
    // No bounds checking, verify "idx" is good prior to calling this function
    return m_strings[idx];
  }

  const std::string &operator[](size_t idx) const {
    // No bounds checking, verify "idx" is good prior to calling this function
    return m_strings[idx];
  }

  void PopBack() { m_strings.pop_back(); }
  const char *GetStringAtIndex(size_t idx) const;

  void Join(const char *separator, Stream &strm);

  void Clear();

  void LongestCommonPrefix(std::string &common_prefix);

  void InsertStringAtIndex(size_t idx, const std::string &str);

  void InsertStringAtIndex(size_t idx, std::string &&str);

  void InsertStringAtIndex(size_t id, const char *str);

  void DeleteStringAtIndex(size_t id);

  void RemoveBlankLines();

  size_t SplitIntoLines(const std::string &lines);

  size_t SplitIntoLines(const char *lines, size_t len);

  std::string CopyList(const char *item_preamble = nullptr,
                       const char *items_sep = "\n") const;

  StringList &operator<<(const char *str);

  StringList &operator<<(const std::string &s);

  StringList &operator<<(StringList strings);

  // Copy assignment for a vector of strings
  StringList &operator=(const std::vector<std::string> &rhs);

  // This string list contains a list of valid auto completion strings, and the
  // "s" is passed in. "matches" is filled in with zero or more string values
  // that start with "s", and the first string to exactly match one of the
  // string values in this collection, will have "exact_matches_idx" filled in
  // to match the index, or "exact_matches_idx" will have SIZE_MAX
  size_t AutoComplete(llvm::StringRef s, StringList &matches,
                      size_t &exact_matches_idx) const;

  // Dump the StringList to the given lldb_private::Log, `log`, one item per
  // line. If given, `name` will be used to identify the start and end of the
  // list in the output.
  virtual void LogDump(Log *log, const char *name = nullptr);

  // Static helper to convert an iterable of strings to a StringList, and then
  // dump it with the semantics of the `LogDump` method.
  template <typename T>
  static void LogDump(Log *log, T s_iterable, const char *name = nullptr) {
    if (!log)
      return;
    // Make a copy of the iterable as a StringList
    StringList l{};
    for (const auto &s : s_iterable)
      l << s;

    l.LogDump(log, name);
  }

private:
  std::vector<std::string> m_strings;
};

} // namespace lldb_private

#endif // liblldb_StringList_h_
