//===-- CommandHistory.cpp --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <inttypes.h>

#include "lldb/Interpreter/CommandHistory.h"

using namespace lldb;
using namespace lldb_private;

CommandHistory::CommandHistory() : m_mutex(), m_history() {}

CommandHistory::~CommandHistory() {}

size_t CommandHistory::GetSize() const {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  return m_history.size();
}

bool CommandHistory::IsEmpty() const {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  return m_history.empty();
}

llvm::Optional<llvm::StringRef>
CommandHistory::FindString(llvm::StringRef input_str) const {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if (input_str.size() < 2)
    return llvm::None;

  if (input_str[0] != g_repeat_char)
    return llvm::None;

  if (input_str[1] == g_repeat_char) {
    if (m_history.empty())
      return llvm::None;
    return llvm::StringRef(m_history.back());
  }

  input_str = input_str.drop_front();

  size_t idx = 0;
  if (input_str.front() == '-') {
    if (input_str.drop_front(1).getAsInteger(0, idx))
      return llvm::None;
    if (idx >= m_history.size())
      return llvm::None;
    idx = m_history.size() - idx;
  } else {
    if (input_str.getAsInteger(0, idx))
      return llvm::None;
    if (idx >= m_history.size())
      return llvm::None;
  }

  return llvm::StringRef(m_history[idx]);
}

llvm::StringRef CommandHistory::GetStringAtIndex(size_t idx) const {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if (idx < m_history.size())
    return m_history[idx];
  return "";
}

llvm::StringRef CommandHistory::operator[](size_t idx) const {
  return GetStringAtIndex(idx);
}

llvm::StringRef CommandHistory::GetRecentmostString() const {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if (m_history.empty())
    return "";
  return m_history.back();
}

void CommandHistory::AppendString(llvm::StringRef str, bool reject_if_dupe) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if (reject_if_dupe) {
    if (!m_history.empty()) {
      if (str == m_history.back())
        return;
    }
  }
  m_history.push_back(str);
}

void CommandHistory::Clear() {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  m_history.clear();
}

void CommandHistory::Dump(Stream &stream, size_t start_idx,
                          size_t stop_idx) const {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  stop_idx = std::min(stop_idx + 1, m_history.size());
  for (size_t counter = start_idx; counter < stop_idx; counter++) {
    const std::string hist_item = m_history[counter];
    if (!hist_item.empty()) {
      stream.Indent();
      stream.Printf("%4" PRIu64 ": %s\n", (uint64_t)counter, hist_item.c_str());
    }
  }
}
