//===-- Highlighter.h -------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Highlighter_h_
#define liblldb_Highlighter_h_

#include <utility>
#include <vector>

#include "lldb/Utility/Stream.h"
#include "lldb/lldb-enumerations.h"
#include "llvm/ADT/StringRef.h"

namespace lldb_private {

//----------------------------------------------------------------------
/// Represents style that the highlighter should apply to the given source code.
/// Stores information about how every kind of token should be annotated.
//----------------------------------------------------------------------
struct HighlightStyle {

  //----------------------------------------------------------------------
  /// A pair of strings that should be placed around a certain token. Usually
  /// stores color codes in these strings (the suffix string is often used for
  /// resetting the terminal attributes back to normal).
  //----------------------------------------------------------------------
  class ColorStyle {
    std::string m_prefix;
    std::string m_suffix;

  public:
    ColorStyle() = default;
    ColorStyle(llvm::StringRef prefix, llvm::StringRef suffix) {
      Set(prefix, suffix);
    }

    /// Applies this style to the given value.
    /// \param s
    ///     The stream to which the result should be appended.
    /// \param value
    ///     The value that we should place our strings around.
    void Apply(Stream &s, llvm::StringRef value) const;

    /// Sets the prefix and suffix strings.
    /// @param prefix
    /// @param suffix
    void Set(llvm::StringRef prefix, llvm::StringRef suffix);
  };

  /// The style for the token which is below the cursor of the user. Note that
  /// this style is overwritten by the SourceManager with the values of
  /// stop-show-column-ansi-prefix/stop-show-column-ansi-suffix.
  ColorStyle selected;

  /// Matches identifiers to variable or functions.
  ColorStyle identifier;
  /// Matches any string or character literals in the language: "foo" or 'f'
  ColorStyle string_literal;
  /// Matches scalar value literals like '42' or '0.1'.
  ColorStyle scalar_literal;
  /// Matches all reserved keywords in the language.
  ColorStyle keyword;
  /// Matches any comments in the language.
  ColorStyle comment;
  /// Matches commas: ','
  ColorStyle comma;
  /// Matches one colon: ':'
  ColorStyle colon;
  /// Matches any semicolon: ';'
  ColorStyle semicolons;
  /// Matches operators like '+', '-', '%', '&', '='
  ColorStyle operators;

  /// Matches '{' or '}'
  ColorStyle braces;
  /// Matches '[' or ']'
  ColorStyle square_brackets;
  /// Matches '(' or ')'
  ColorStyle parentheses;

  //-----------------------------------------------------------------------
  // C language specific options
  //-----------------------------------------------------------------------

  /// Matches directives to a preprocessor (if the language has any).
  ColorStyle pp_directive;

  /// Returns a HighlightStyle that is based on vim's default highlight style.
  static HighlightStyle MakeVimStyle();
};

//----------------------------------------------------------------------
/// Annotates source code with color attributes.
//----------------------------------------------------------------------
class Highlighter {
public:
  Highlighter() = default;
  virtual ~Highlighter() = default;
  DISALLOW_COPY_AND_ASSIGN(Highlighter);

  /// Returns a human readable name for the selected highlighter.
  virtual llvm::StringRef GetName() const = 0;

  /// Highlights the given line
  /// \param options
  /// \param line
  ///     The user supplied line that needs to be highlighted.
  /// \param cursor_pos
  ///     The cursor position of the user in this line, starting at 0 (which
  ///     means the cursor is on the first character in 'line').
  /// \param previous_lines
  ///     Any previous lines the user has written which we should only use
  ///     for getting the context of the Highlighting right.
  /// \param s
  ///     The stream to which the highlighted version of the user string should
  ///     be written.
  virtual void Highlight(const HighlightStyle &options, llvm::StringRef line,
                         llvm::Optional<size_t> cursor_pos,
                         llvm::StringRef previous_lines, Stream &s) const = 0;

  /// Utility method for calling Highlight without a stream.
  std::string Highlight(const HighlightStyle &options, llvm::StringRef line,
                        llvm::Optional<size_t> cursor_pos,
                        llvm::StringRef previous_lines = "") const;
};

/// A default highlighter that only highlights the user cursor, but doesn't
/// do any other highlighting.
class DefaultHighlighter : public Highlighter {
public:
  llvm::StringRef GetName() const override { return "none"; }

  void Highlight(const HighlightStyle &options, llvm::StringRef line,
                 llvm::Optional<size_t> cursor_pos,
                 llvm::StringRef previous_lines, Stream &s) const override;
};

/// Manages the available highlighters.
class HighlighterManager {
  DefaultHighlighter m_default;

public:
  /// Queries all known highlighter for one that can highlight some source code.
  /// \param language_type
  ///     The language type that the caller thinks the source code was given in.
  /// \param path
  ///     The path to the file the source code is from. Used as a fallback when
  ///     the user can't provide a language.
  /// \return
  ///     The highlighter that wants to highlight the source code. Could be an
  ///     empty highlighter that does nothing.
  const Highlighter &getHighlighterFor(lldb::LanguageType language_type,
                                       llvm::StringRef path) const;
  const Highlighter &getDefaultHighlighter() const { return m_default; }
};

} // namespace lldb_private

#endif // liblldb_Highlighter_h_
