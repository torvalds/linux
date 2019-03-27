//===-- CompletionRequest.cpp -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/CompletionRequest.h"

using namespace lldb;
using namespace lldb_private;

CompletionRequest::CompletionRequest(llvm::StringRef command_line,
                                     unsigned raw_cursor_pos,
                                     int match_start_point,
                                     int max_return_elements,
                                     CompletionResult &result)
    : m_command(command_line), m_raw_cursor_pos(raw_cursor_pos),
      m_match_start_point(match_start_point),
      m_max_return_elements(max_return_elements), m_result(result) {

  // We parse the argument up to the cursor, so the last argument in
  // parsed_line is the one containing the cursor, and the cursor is after the
  // last character.
  m_parsed_line = Args(command_line);
  m_partial_parsed_line = Args(command_line.substr(0, raw_cursor_pos));

  m_cursor_index = m_partial_parsed_line.GetArgumentCount() - 1;

  if (m_cursor_index == -1)
    m_cursor_char_position = 0;
  else
    m_cursor_char_position =
        strlen(m_partial_parsed_line.GetArgumentAtIndex(m_cursor_index));

  const char *cursor = command_line.data() + raw_cursor_pos;
  if (raw_cursor_pos > 0 && cursor[-1] == ' ') {
    // We are just after a space.  If we are in an argument, then we will
    // continue parsing, but if we are between arguments, then we have to
    // complete whatever the next element would be. We can distinguish the two
    // cases because if we are in an argument (e.g. because the space is
    // protected by a quote) then the space will also be in the parsed
    // argument...

    const char *current_elem =
        m_partial_parsed_line.GetArgumentAtIndex(m_cursor_index);
    if (m_cursor_char_position == 0 ||
        current_elem[m_cursor_char_position - 1] != ' ') {
      m_parsed_line.InsertArgumentAtIndex(m_cursor_index + 1, llvm::StringRef(),
                                          '\0');
      m_cursor_index++;
      m_cursor_char_position = 0;
    }
  }
}

std::string CompletionResult::Completion::GetUniqueKey() const {

  // We build a unique key for this pair of completion:description. We
  // prefix the key with the length of the completion string. This prevents
  // that we could get any collisions from completions pairs such as these:
  // "foo:", "bar" would be "foo:bar", but will now be: "4foo:bar"
  // "foo", ":bar" would be "foo:bar", but will now be: "3foo:bar"

  std::string result;
  result.append(std::to_string(m_completion.size()));
  result.append(m_completion);
  result.append(m_descripton);
  return result;
}

void CompletionResult::AddResult(llvm::StringRef completion,
                                 llvm::StringRef description) {
  Completion r(completion, description);

  // Add the completion if we haven't seen the same value before.
  if (m_added_values.insert(r.GetUniqueKey()).second)
    m_results.push_back(r);
}

void CompletionResult::GetMatches(StringList &matches) const {
  matches.Clear();
  for (const Completion &completion : m_results)
    matches.AppendString(completion.m_completion);
}

void CompletionResult::GetDescriptions(StringList &descriptions) const {
  descriptions.Clear();
  for (const Completion &completion : m_results)
    descriptions.AppendString(completion.m_descripton);
}
