//===-- Highlighter.cpp -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/Highlighter.h"

#include "lldb/Target/Language.h"
#include "lldb/Utility/AnsiTerminal.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb_private;

void HighlightStyle::ColorStyle::Apply(Stream &s, llvm::StringRef value) const {
  s << m_prefix << value << m_suffix;
}

void HighlightStyle::ColorStyle::Set(llvm::StringRef prefix,
                                     llvm::StringRef suffix) {
  m_prefix = lldb_utility::ansi::FormatAnsiTerminalCodes(prefix);
  m_suffix = lldb_utility::ansi::FormatAnsiTerminalCodes(suffix);
}

void DefaultHighlighter::Highlight(const HighlightStyle &options,
                                   llvm::StringRef line,
                                   llvm::Optional<size_t> cursor_pos,
                                   llvm::StringRef previous_lines,
                                   Stream &s) const {
  // If we don't have a valid cursor, then we just print the line as-is.
  if (!cursor_pos || *cursor_pos >= line.size()) {
    s << line;
    return;
  }

  // If we have a valid cursor, we have to apply the 'selected' style around
  // the character below the cursor.

  // Split the line around the character which is below the cursor.
  size_t column = *cursor_pos;
  // Print the characters before the cursor.
  s << line.substr(0, column);
  // Print the selected character with the defined color codes.
  options.selected.Apply(s, line.substr(column, 1));
  // Print the rest of the line.
  s << line.substr(column + 1U);
}

static HighlightStyle::ColorStyle GetColor(const char *c) {
  return HighlightStyle::ColorStyle(c, "${ansi.normal}");
}

HighlightStyle HighlightStyle::MakeVimStyle() {
  HighlightStyle result;
  result.comment = GetColor("${ansi.fg.purple}");
  result.scalar_literal = GetColor("${ansi.fg.red}");
  result.keyword = GetColor("${ansi.fg.green}");
  return result;
}

const Highlighter &
HighlighterManager::getHighlighterFor(lldb::LanguageType language_type,
                                      llvm::StringRef path) const {
  Language *language = lldb_private::Language::FindPlugin(language_type, path);
  if (language && language->GetHighlighter())
    return *language->GetHighlighter();
  return m_default;
}

std::string Highlighter::Highlight(const HighlightStyle &options,
                                   llvm::StringRef line,
                                   llvm::Optional<size_t> cursor_pos,
                                   llvm::StringRef previous_lines) const {
  StreamString s;
  Highlight(options, line, cursor_pos, previous_lines, s);
  s.Flush();
  return s.GetString().str();
}
