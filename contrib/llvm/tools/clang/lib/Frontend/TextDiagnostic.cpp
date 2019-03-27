//===--- TextDiagnostic.cpp - Text Diagnostic Pretty-Printing -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/TextDiagnostic.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Locale.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>

using namespace clang;

static const enum raw_ostream::Colors noteColor =
  raw_ostream::BLACK;
static const enum raw_ostream::Colors remarkColor =
  raw_ostream::BLUE;
static const enum raw_ostream::Colors fixitColor =
  raw_ostream::GREEN;
static const enum raw_ostream::Colors caretColor =
  raw_ostream::GREEN;
static const enum raw_ostream::Colors warningColor =
  raw_ostream::MAGENTA;
static const enum raw_ostream::Colors templateColor =
  raw_ostream::CYAN;
static const enum raw_ostream::Colors errorColor = raw_ostream::RED;
static const enum raw_ostream::Colors fatalColor = raw_ostream::RED;
// Used for changing only the bold attribute.
static const enum raw_ostream::Colors savedColor =
  raw_ostream::SAVEDCOLOR;

/// Add highlights to differences in template strings.
static void applyTemplateHighlighting(raw_ostream &OS, StringRef Str,
                                      bool &Normal, bool Bold) {
  while (1) {
    size_t Pos = Str.find(ToggleHighlight);
    OS << Str.slice(0, Pos);
    if (Pos == StringRef::npos)
      break;

    Str = Str.substr(Pos + 1);
    if (Normal)
      OS.changeColor(templateColor, true);
    else {
      OS.resetColor();
      if (Bold)
        OS.changeColor(savedColor, true);
    }
    Normal = !Normal;
  }
}

/// Number of spaces to indent when word-wrapping.
const unsigned WordWrapIndentation = 6;

static int bytesSincePreviousTabOrLineBegin(StringRef SourceLine, size_t i) {
  int bytes = 0;
  while (0<i) {
    if (SourceLine[--i]=='\t')
      break;
    ++bytes;
  }
  return bytes;
}

/// returns a printable representation of first item from input range
///
/// This function returns a printable representation of the next item in a line
///  of source. If the next byte begins a valid and printable character, that
///  character is returned along with 'true'.
///
/// Otherwise, if the next byte begins a valid, but unprintable character, a
///  printable, escaped representation of the character is returned, along with
///  'false'. Otherwise a printable, escaped representation of the next byte
///  is returned along with 'false'.
///
/// \note The index is updated to be used with a subsequent call to
///        printableTextForNextCharacter.
///
/// \param SourceLine The line of source
/// \param i Pointer to byte index,
/// \param TabStop used to expand tabs
/// \return pair(printable text, 'true' iff original text was printable)
///
static std::pair<SmallString<16>, bool>
printableTextForNextCharacter(StringRef SourceLine, size_t *i,
                              unsigned TabStop) {
  assert(i && "i must not be null");
  assert(*i<SourceLine.size() && "must point to a valid index");

  if (SourceLine[*i]=='\t') {
    assert(0 < TabStop && TabStop <= DiagnosticOptions::MaxTabStop &&
           "Invalid -ftabstop value");
    unsigned col = bytesSincePreviousTabOrLineBegin(SourceLine, *i);
    unsigned NumSpaces = TabStop - col%TabStop;
    assert(0 < NumSpaces && NumSpaces <= TabStop
           && "Invalid computation of space amt");
    ++(*i);

    SmallString<16> expandedTab;
    expandedTab.assign(NumSpaces, ' ');
    return std::make_pair(expandedTab, true);
  }

  unsigned char const *begin, *end;
  begin = reinterpret_cast<unsigned char const *>(&*(SourceLine.begin() + *i));
  end = begin + (SourceLine.size() - *i);

  if (llvm::isLegalUTF8Sequence(begin, end)) {
    llvm::UTF32 c;
    llvm::UTF32 *cptr = &c;
    unsigned char const *original_begin = begin;
    unsigned char const *cp_end =
        begin + llvm::getNumBytesForUTF8(SourceLine[*i]);

    llvm::ConversionResult res = llvm::ConvertUTF8toUTF32(
        &begin, cp_end, &cptr, cptr + 1, llvm::strictConversion);
    (void)res;
    assert(llvm::conversionOK == res);
    assert(0 < begin-original_begin
           && "we must be further along in the string now");
    *i += begin-original_begin;

    if (!llvm::sys::locale::isPrint(c)) {
      // If next character is valid UTF-8, but not printable
      SmallString<16> expandedCP("<U+>");
      while (c) {
        expandedCP.insert(expandedCP.begin()+3, llvm::hexdigit(c%16));
        c/=16;
      }
      while (expandedCP.size() < 8)
        expandedCP.insert(expandedCP.begin()+3, llvm::hexdigit(0));
      return std::make_pair(expandedCP, false);
    }

    // If next character is valid UTF-8, and printable
    return std::make_pair(SmallString<16>(original_begin, cp_end), true);

  }

  // If next byte is not valid UTF-8 (and therefore not printable)
  SmallString<16> expandedByte("<XX>");
  unsigned char byte = SourceLine[*i];
  expandedByte[1] = llvm::hexdigit(byte / 16);
  expandedByte[2] = llvm::hexdigit(byte % 16);
  ++(*i);
  return std::make_pair(expandedByte, false);
}

static void expandTabs(std::string &SourceLine, unsigned TabStop) {
  size_t i = SourceLine.size();
  while (i>0) {
    i--;
    if (SourceLine[i]!='\t')
      continue;
    size_t tmp_i = i;
    std::pair<SmallString<16>,bool> res
      = printableTextForNextCharacter(SourceLine, &tmp_i, TabStop);
    SourceLine.replace(i, 1, res.first.c_str());
  }
}

/// This function takes a raw source line and produces a mapping from the bytes
///  of the printable representation of the line to the columns those printable
///  characters will appear at (numbering the first column as 0).
///
/// If a byte 'i' corresponds to multiple columns (e.g. the byte contains a tab
///  character) then the array will map that byte to the first column the
///  tab appears at and the next value in the map will have been incremented
///  more than once.
///
/// If a byte is the first in a sequence of bytes that together map to a single
///  entity in the output, then the array will map that byte to the appropriate
///  column while the subsequent bytes will be -1.
///
/// The last element in the array does not correspond to any byte in the input
///  and instead is the number of columns needed to display the source
///
/// example: (given a tabstop of 8)
///
///    "a \t \u3042" -> {0,1,2,8,9,-1,-1,11}
///
///  (\\u3042 is represented in UTF-8 by three bytes and takes two columns to
///   display)
static void byteToColumn(StringRef SourceLine, unsigned TabStop,
                         SmallVectorImpl<int> &out) {
  out.clear();

  if (SourceLine.empty()) {
    out.resize(1u,0);
    return;
  }

  out.resize(SourceLine.size()+1, -1);

  int columns = 0;
  size_t i = 0;
  while (i<SourceLine.size()) {
    out[i] = columns;
    std::pair<SmallString<16>,bool> res
      = printableTextForNextCharacter(SourceLine, &i, TabStop);
    columns += llvm::sys::locale::columnWidth(res.first);
  }
  out.back() = columns;
}

/// This function takes a raw source line and produces a mapping from columns
///  to the byte of the source line that produced the character displaying at
///  that column. This is the inverse of the mapping produced by byteToColumn()
///
/// The last element in the array is the number of bytes in the source string
///
/// example: (given a tabstop of 8)
///
///    "a \t \u3042" -> {0,1,2,-1,-1,-1,-1,-1,3,4,-1,7}
///
///  (\\u3042 is represented in UTF-8 by three bytes and takes two columns to
///   display)
static void columnToByte(StringRef SourceLine, unsigned TabStop,
                         SmallVectorImpl<int> &out) {
  out.clear();

  if (SourceLine.empty()) {
    out.resize(1u, 0);
    return;
  }

  int columns = 0;
  size_t i = 0;
  while (i<SourceLine.size()) {
    out.resize(columns+1, -1);
    out.back() = i;
    std::pair<SmallString<16>,bool> res
      = printableTextForNextCharacter(SourceLine, &i, TabStop);
    columns += llvm::sys::locale::columnWidth(res.first);
  }
  out.resize(columns+1, -1);
  out.back() = i;
}

namespace {
struct SourceColumnMap {
  SourceColumnMap(StringRef SourceLine, unsigned TabStop)
  : m_SourceLine(SourceLine) {

    ::byteToColumn(SourceLine, TabStop, m_byteToColumn);
    ::columnToByte(SourceLine, TabStop, m_columnToByte);

    assert(m_byteToColumn.size()==SourceLine.size()+1);
    assert(0 < m_byteToColumn.size() && 0 < m_columnToByte.size());
    assert(m_byteToColumn.size()
           == static_cast<unsigned>(m_columnToByte.back()+1));
    assert(static_cast<unsigned>(m_byteToColumn.back()+1)
           == m_columnToByte.size());
  }
  int columns() const { return m_byteToColumn.back(); }
  int bytes() const { return m_columnToByte.back(); }

  /// Map a byte to the column which it is at the start of, or return -1
  /// if it is not at the start of a column (for a UTF-8 trailing byte).
  int byteToColumn(int n) const {
    assert(0<=n && n<static_cast<int>(m_byteToColumn.size()));
    return m_byteToColumn[n];
  }

  /// Map a byte to the first column which contains it.
  int byteToContainingColumn(int N) const {
    assert(0 <= N && N < static_cast<int>(m_byteToColumn.size()));
    while (m_byteToColumn[N] == -1)
      --N;
    return m_byteToColumn[N];
  }

  /// Map a column to the byte which starts the column, or return -1 if
  /// the column the second or subsequent column of an expanded tab or similar
  /// multi-column entity.
  int columnToByte(int n) const {
    assert(0<=n && n<static_cast<int>(m_columnToByte.size()));
    return m_columnToByte[n];
  }

  /// Map from a byte index to the next byte which starts a column.
  int startOfNextColumn(int N) const {
    assert(0 <= N && N < static_cast<int>(m_byteToColumn.size() - 1));
    while (byteToColumn(++N) == -1) {}
    return N;
  }

  /// Map from a byte index to the previous byte which starts a column.
  int startOfPreviousColumn(int N) const {
    assert(0 < N && N < static_cast<int>(m_byteToColumn.size()));
    while (byteToColumn(--N) == -1) {}
    return N;
  }

  StringRef getSourceLine() const {
    return m_SourceLine;
  }

private:
  const std::string m_SourceLine;
  SmallVector<int,200> m_byteToColumn;
  SmallVector<int,200> m_columnToByte;
};
} // end anonymous namespace

/// When the source code line we want to print is too long for
/// the terminal, select the "interesting" region.
static void selectInterestingSourceRegion(std::string &SourceLine,
                                          std::string &CaretLine,
                                          std::string &FixItInsertionLine,
                                          unsigned Columns,
                                          const SourceColumnMap &map) {
  unsigned CaretColumns = CaretLine.size();
  unsigned FixItColumns = llvm::sys::locale::columnWidth(FixItInsertionLine);
  unsigned MaxColumns = std::max(static_cast<unsigned>(map.columns()),
                                 std::max(CaretColumns, FixItColumns));
  // if the number of columns is less than the desired number we're done
  if (MaxColumns <= Columns)
    return;

  // No special characters are allowed in CaretLine.
  assert(CaretLine.end() ==
         std::find_if(CaretLine.begin(), CaretLine.end(),
                      [](char c) { return c < ' ' || '~' < c; }));

  // Find the slice that we need to display the full caret line
  // correctly.
  unsigned CaretStart = 0, CaretEnd = CaretLine.size();
  for (; CaretStart != CaretEnd; ++CaretStart)
    if (!isWhitespace(CaretLine[CaretStart]))
      break;

  for (; CaretEnd != CaretStart; --CaretEnd)
    if (!isWhitespace(CaretLine[CaretEnd - 1]))
      break;

  // caret has already been inserted into CaretLine so the above whitespace
  // check is guaranteed to include the caret

  // If we have a fix-it line, make sure the slice includes all of the
  // fix-it information.
  if (!FixItInsertionLine.empty()) {
    unsigned FixItStart = 0, FixItEnd = FixItInsertionLine.size();
    for (; FixItStart != FixItEnd; ++FixItStart)
      if (!isWhitespace(FixItInsertionLine[FixItStart]))
        break;

    for (; FixItEnd != FixItStart; --FixItEnd)
      if (!isWhitespace(FixItInsertionLine[FixItEnd - 1]))
        break;

    // We can safely use the byte offset FixItStart as the column offset
    // because the characters up until FixItStart are all ASCII whitespace
    // characters.
    unsigned FixItStartCol = FixItStart;
    unsigned FixItEndCol
      = llvm::sys::locale::columnWidth(FixItInsertionLine.substr(0, FixItEnd));

    CaretStart = std::min(FixItStartCol, CaretStart);
    CaretEnd = std::max(FixItEndCol, CaretEnd);
  }

  // CaretEnd may have been set at the middle of a character
  // If it's not at a character's first column then advance it past the current
  //   character.
  while (static_cast<int>(CaretEnd) < map.columns() &&
         -1 == map.columnToByte(CaretEnd))
    ++CaretEnd;

  assert((static_cast<int>(CaretStart) > map.columns() ||
          -1!=map.columnToByte(CaretStart)) &&
         "CaretStart must not point to a column in the middle of a source"
         " line character");
  assert((static_cast<int>(CaretEnd) > map.columns() ||
          -1!=map.columnToByte(CaretEnd)) &&
         "CaretEnd must not point to a column in the middle of a source line"
         " character");

  // CaretLine[CaretStart, CaretEnd) contains all of the interesting
  // parts of the caret line. While this slice is smaller than the
  // number of columns we have, try to grow the slice to encompass
  // more context.

  unsigned SourceStart = map.columnToByte(std::min<unsigned>(CaretStart,
                                                             map.columns()));
  unsigned SourceEnd = map.columnToByte(std::min<unsigned>(CaretEnd,
                                                           map.columns()));

  unsigned CaretColumnsOutsideSource = CaretEnd-CaretStart
    - (map.byteToColumn(SourceEnd)-map.byteToColumn(SourceStart));

  char const *front_ellipse = "  ...";
  char const *front_space   = "     ";
  char const *back_ellipse = "...";
  unsigned ellipses_space = strlen(front_ellipse) + strlen(back_ellipse);

  unsigned TargetColumns = Columns;
  // Give us extra room for the ellipses
  //  and any of the caret line that extends past the source
  if (TargetColumns > ellipses_space+CaretColumnsOutsideSource)
    TargetColumns -= ellipses_space+CaretColumnsOutsideSource;

  while (SourceStart>0 || SourceEnd<SourceLine.size()) {
    bool ExpandedRegion = false;

    if (SourceStart>0) {
      unsigned NewStart = map.startOfPreviousColumn(SourceStart);

      // Skip over any whitespace we see here; we're looking for
      // another bit of interesting text.
      // FIXME: Detect non-ASCII whitespace characters too.
      while (NewStart && isWhitespace(SourceLine[NewStart]))
        NewStart = map.startOfPreviousColumn(NewStart);

      // Skip over this bit of "interesting" text.
      while (NewStart) {
        unsigned Prev = map.startOfPreviousColumn(NewStart);
        if (isWhitespace(SourceLine[Prev]))
          break;
        NewStart = Prev;
      }

      assert(map.byteToColumn(NewStart) != -1);
      unsigned NewColumns = map.byteToColumn(SourceEnd) -
                              map.byteToColumn(NewStart);
      if (NewColumns <= TargetColumns) {
        SourceStart = NewStart;
        ExpandedRegion = true;
      }
    }

    if (SourceEnd<SourceLine.size()) {
      unsigned NewEnd = map.startOfNextColumn(SourceEnd);

      // Skip over any whitespace we see here; we're looking for
      // another bit of interesting text.
      // FIXME: Detect non-ASCII whitespace characters too.
      while (NewEnd < SourceLine.size() && isWhitespace(SourceLine[NewEnd]))
        NewEnd = map.startOfNextColumn(NewEnd);

      // Skip over this bit of "interesting" text.
      while (NewEnd < SourceLine.size() && isWhitespace(SourceLine[NewEnd]))
        NewEnd = map.startOfNextColumn(NewEnd);

      assert(map.byteToColumn(NewEnd) != -1);
      unsigned NewColumns = map.byteToColumn(NewEnd) -
                              map.byteToColumn(SourceStart);
      if (NewColumns <= TargetColumns) {
        SourceEnd = NewEnd;
        ExpandedRegion = true;
      }
    }

    if (!ExpandedRegion)
      break;
  }

  CaretStart = map.byteToColumn(SourceStart);
  CaretEnd = map.byteToColumn(SourceEnd) + CaretColumnsOutsideSource;

  // [CaretStart, CaretEnd) is the slice we want. Update the various
  // output lines to show only this slice, with two-space padding
  // before the lines so that it looks nicer.

  assert(CaretStart!=(unsigned)-1 && CaretEnd!=(unsigned)-1 &&
         SourceStart!=(unsigned)-1 && SourceEnd!=(unsigned)-1);
  assert(SourceStart <= SourceEnd);
  assert(CaretStart <= CaretEnd);

  unsigned BackColumnsRemoved
    = map.byteToColumn(SourceLine.size())-map.byteToColumn(SourceEnd);
  unsigned FrontColumnsRemoved = CaretStart;
  unsigned ColumnsKept = CaretEnd-CaretStart;

  // We checked up front that the line needed truncation
  assert(FrontColumnsRemoved+ColumnsKept+BackColumnsRemoved > Columns);

  // The line needs some truncation, and we'd prefer to keep the front
  //  if possible, so remove the back
  if (BackColumnsRemoved > strlen(back_ellipse))
    SourceLine.replace(SourceEnd, std::string::npos, back_ellipse);

  // If that's enough then we're done
  if (FrontColumnsRemoved+ColumnsKept <= Columns)
    return;

  // Otherwise remove the front as well
  if (FrontColumnsRemoved > strlen(front_ellipse)) {
    SourceLine.replace(0, SourceStart, front_ellipse);
    CaretLine.replace(0, CaretStart, front_space);
    if (!FixItInsertionLine.empty())
      FixItInsertionLine.replace(0, CaretStart, front_space);
  }
}

/// Skip over whitespace in the string, starting at the given
/// index.
///
/// \returns The index of the first non-whitespace character that is
/// greater than or equal to Idx or, if no such character exists,
/// returns the end of the string.
static unsigned skipWhitespace(unsigned Idx, StringRef Str, unsigned Length) {
  while (Idx < Length && isWhitespace(Str[Idx]))
    ++Idx;
  return Idx;
}

/// If the given character is the start of some kind of
/// balanced punctuation (e.g., quotes or parentheses), return the
/// character that will terminate the punctuation.
///
/// \returns The ending punctuation character, if any, or the NULL
/// character if the input character does not start any punctuation.
static inline char findMatchingPunctuation(char c) {
  switch (c) {
  case '\'': return '\'';
  case '`': return '\'';
  case '"':  return '"';
  case '(':  return ')';
  case '[': return ']';
  case '{': return '}';
  default: break;
  }

  return 0;
}

/// Find the end of the word starting at the given offset
/// within a string.
///
/// \returns the index pointing one character past the end of the
/// word.
static unsigned findEndOfWord(unsigned Start, StringRef Str,
                              unsigned Length, unsigned Column,
                              unsigned Columns) {
  assert(Start < Str.size() && "Invalid start position!");
  unsigned End = Start + 1;

  // If we are already at the end of the string, take that as the word.
  if (End == Str.size())
    return End;

  // Determine if the start of the string is actually opening
  // punctuation, e.g., a quote or parentheses.
  char EndPunct = findMatchingPunctuation(Str[Start]);
  if (!EndPunct) {
    // This is a normal word. Just find the first space character.
    while (End < Length && !isWhitespace(Str[End]))
      ++End;
    return End;
  }

  // We have the start of a balanced punctuation sequence (quotes,
  // parentheses, etc.). Determine the full sequence is.
  SmallString<16> PunctuationEndStack;
  PunctuationEndStack.push_back(EndPunct);
  while (End < Length && !PunctuationEndStack.empty()) {
    if (Str[End] == PunctuationEndStack.back())
      PunctuationEndStack.pop_back();
    else if (char SubEndPunct = findMatchingPunctuation(Str[End]))
      PunctuationEndStack.push_back(SubEndPunct);

    ++End;
  }

  // Find the first space character after the punctuation ended.
  while (End < Length && !isWhitespace(Str[End]))
    ++End;

  unsigned PunctWordLength = End - Start;
  if (// If the word fits on this line
      Column + PunctWordLength <= Columns ||
      // ... or the word is "short enough" to take up the next line
      // without too much ugly white space
      PunctWordLength < Columns/3)
    return End; // Take the whole thing as a single "word".

  // The whole quoted/parenthesized string is too long to print as a
  // single "word". Instead, find the "word" that starts just after
  // the punctuation and use that end-point instead. This will recurse
  // until it finds something small enough to consider a word.
  return findEndOfWord(Start + 1, Str, Length, Column + 1, Columns);
}

/// Print the given string to a stream, word-wrapping it to
/// some number of columns in the process.
///
/// \param OS the stream to which the word-wrapping string will be
/// emitted.
/// \param Str the string to word-wrap and output.
/// \param Columns the number of columns to word-wrap to.
/// \param Column the column number at which the first character of \p
/// Str will be printed. This will be non-zero when part of the first
/// line has already been printed.
/// \param Bold if the current text should be bold
/// \param Indentation the number of spaces to indent any lines beyond
/// the first line.
/// \returns true if word-wrapping was required, or false if the
/// string fit on the first line.
static bool printWordWrapped(raw_ostream &OS, StringRef Str,
                             unsigned Columns,
                             unsigned Column = 0,
                             bool Bold = false,
                             unsigned Indentation = WordWrapIndentation) {
  const unsigned Length = std::min(Str.find('\n'), Str.size());
  bool TextNormal = true;

  // The string used to indent each line.
  SmallString<16> IndentStr;
  IndentStr.assign(Indentation, ' ');
  bool Wrapped = false;
  for (unsigned WordStart = 0, WordEnd; WordStart < Length;
       WordStart = WordEnd) {
    // Find the beginning of the next word.
    WordStart = skipWhitespace(WordStart, Str, Length);
    if (WordStart == Length)
      break;

    // Find the end of this word.
    WordEnd = findEndOfWord(WordStart, Str, Length, Column, Columns);

    // Does this word fit on the current line?
    unsigned WordLength = WordEnd - WordStart;
    if (Column + WordLength < Columns) {
      // This word fits on the current line; print it there.
      if (WordStart) {
        OS << ' ';
        Column += 1;
      }
      applyTemplateHighlighting(OS, Str.substr(WordStart, WordLength),
                                TextNormal, Bold);
      Column += WordLength;
      continue;
    }

    // This word does not fit on the current line, so wrap to the next
    // line.
    OS << '\n';
    OS.write(&IndentStr[0], Indentation);
    applyTemplateHighlighting(OS, Str.substr(WordStart, WordLength),
                              TextNormal, Bold);
    Column = Indentation + WordLength;
    Wrapped = true;
  }

  // Append any remaning text from the message with its existing formatting.
  applyTemplateHighlighting(OS, Str.substr(Length), TextNormal, Bold);

  assert(TextNormal && "Text highlighted at end of diagnostic message.");

  return Wrapped;
}

TextDiagnostic::TextDiagnostic(raw_ostream &OS,
                               const LangOptions &LangOpts,
                               DiagnosticOptions *DiagOpts)
  : DiagnosticRenderer(LangOpts, DiagOpts), OS(OS) {}

TextDiagnostic::~TextDiagnostic() {}

void TextDiagnostic::emitDiagnosticMessage(
    FullSourceLoc Loc, PresumedLoc PLoc, DiagnosticsEngine::Level Level,
    StringRef Message, ArrayRef<clang::CharSourceRange> Ranges,
    DiagOrStoredDiag D) {
  uint64_t StartOfLocationInfo = OS.tell();

  // Emit the location of this particular diagnostic.
  if (Loc.isValid())
    emitDiagnosticLoc(Loc, PLoc, Level, Ranges);

  if (DiagOpts->ShowColors)
    OS.resetColor();

  printDiagnosticLevel(OS, Level, DiagOpts->ShowColors,
                       DiagOpts->CLFallbackMode);
  printDiagnosticMessage(OS,
                         /*IsSupplemental*/ Level == DiagnosticsEngine::Note,
                         Message, OS.tell() - StartOfLocationInfo,
                         DiagOpts->MessageLength, DiagOpts->ShowColors);
}

/*static*/ void
TextDiagnostic::printDiagnosticLevel(raw_ostream &OS,
                                     DiagnosticsEngine::Level Level,
                                     bool ShowColors,
                                     bool CLFallbackMode) {
  if (ShowColors) {
    // Print diagnostic category in bold and color
    switch (Level) {
    case DiagnosticsEngine::Ignored:
      llvm_unreachable("Invalid diagnostic type");
    case DiagnosticsEngine::Note:    OS.changeColor(noteColor, true); break;
    case DiagnosticsEngine::Remark:  OS.changeColor(remarkColor, true); break;
    case DiagnosticsEngine::Warning: OS.changeColor(warningColor, true); break;
    case DiagnosticsEngine::Error:   OS.changeColor(errorColor, true); break;
    case DiagnosticsEngine::Fatal:   OS.changeColor(fatalColor, true); break;
    }
  }

  switch (Level) {
  case DiagnosticsEngine::Ignored:
    llvm_unreachable("Invalid diagnostic type");
  case DiagnosticsEngine::Note:    OS << "note"; break;
  case DiagnosticsEngine::Remark:  OS << "remark"; break;
  case DiagnosticsEngine::Warning: OS << "warning"; break;
  case DiagnosticsEngine::Error:   OS << "error"; break;
  case DiagnosticsEngine::Fatal:   OS << "fatal error"; break;
  }

  // In clang-cl /fallback mode, print diagnostics as "error(clang):". This
  // makes it more clear whether a message is coming from clang or cl.exe,
  // and it prevents MSBuild from concluding that the build failed just because
  // there is an "error:" in the output.
  if (CLFallbackMode)
    OS << "(clang)";

  OS << ": ";

  if (ShowColors)
    OS.resetColor();
}

/*static*/
void TextDiagnostic::printDiagnosticMessage(raw_ostream &OS,
                                            bool IsSupplemental,
                                            StringRef Message,
                                            unsigned CurrentColumn,
                                            unsigned Columns, bool ShowColors) {
  bool Bold = false;
  if (ShowColors && !IsSupplemental) {
    // Print primary diagnostic messages in bold and without color, to visually
    // indicate the transition from continuation notes and other output.
    OS.changeColor(savedColor, true);
    Bold = true;
  }

  if (Columns)
    printWordWrapped(OS, Message, Columns, CurrentColumn, Bold);
  else {
    bool Normal = true;
    applyTemplateHighlighting(OS, Message, Normal, Bold);
    assert(Normal && "Formatting should have returned to normal");
  }

  if (ShowColors)
    OS.resetColor();
  OS << '\n';
}

void TextDiagnostic::emitFilename(StringRef Filename, const SourceManager &SM) {
  SmallVector<char, 128> AbsoluteFilename;
  if (DiagOpts->AbsolutePath) {
    const DirectoryEntry *Dir = SM.getFileManager().getDirectory(
        llvm::sys::path::parent_path(Filename));
    if (Dir) {
      StringRef DirName = SM.getFileManager().getCanonicalName(Dir);
      llvm::sys::path::append(AbsoluteFilename, DirName,
                              llvm::sys::path::filename(Filename));
      Filename = StringRef(AbsoluteFilename.data(), AbsoluteFilename.size());
    }
  }

  OS << Filename;
}

/// Print out the file/line/column information and include trace.
///
/// This method handlen the emission of the diagnostic location information.
/// This includes extracting as much location information as is present for
/// the diagnostic and printing it, as well as any include stack or source
/// ranges necessary.
void TextDiagnostic::emitDiagnosticLoc(FullSourceLoc Loc, PresumedLoc PLoc,
                                       DiagnosticsEngine::Level Level,
                                       ArrayRef<CharSourceRange> Ranges) {
  if (PLoc.isInvalid()) {
    // At least print the file name if available:
    FileID FID = Loc.getFileID();
    if (FID.isValid()) {
      const FileEntry *FE = Loc.getFileEntry();
      if (FE && FE->isValid()) {
        emitFilename(FE->getName(), Loc.getManager());
        if (FE->isInPCH())
          OS << " (in PCH)";
        OS << ": ";
      }
    }
    return;
  }
  unsigned LineNo = PLoc.getLine();

  if (!DiagOpts->ShowLocation)
    return;

  if (DiagOpts->ShowColors)
    OS.changeColor(savedColor, true);

  emitFilename(PLoc.getFilename(), Loc.getManager());
  switch (DiagOpts->getFormat()) {
  case DiagnosticOptions::Clang: OS << ':'  << LineNo; break;
  case DiagnosticOptions::MSVC:  OS << '('  << LineNo; break;
  case DiagnosticOptions::Vi:    OS << " +" << LineNo; break;
  }

  if (DiagOpts->ShowColumn)
    // Compute the column number.
    if (unsigned ColNo = PLoc.getColumn()) {
      if (DiagOpts->getFormat() == DiagnosticOptions::MSVC) {
        OS << ',';
        // Visual Studio 2010 or earlier expects column number to be off by one
        if (LangOpts.MSCompatibilityVersion &&
            !LangOpts.isCompatibleWithMSVC(LangOptions::MSVC2012))
          ColNo--;
      } else
        OS << ':';
      OS << ColNo;
    }
  switch (DiagOpts->getFormat()) {
  case DiagnosticOptions::Clang:
  case DiagnosticOptions::Vi:    OS << ':';    break;
  case DiagnosticOptions::MSVC:
    // MSVC2013 and before print 'file(4) : error'. MSVC2015 gets rid of the
    // space and prints 'file(4): error'.
    OS << ')';
    if (LangOpts.MSCompatibilityVersion &&
        !LangOpts.isCompatibleWithMSVC(LangOptions::MSVC2015))
      OS << ' ';
    OS << ':';
    break;
  }

  if (DiagOpts->ShowSourceRanges && !Ranges.empty()) {
    FileID CaretFileID = Loc.getExpansionLoc().getFileID();
    bool PrintedRange = false;

    for (ArrayRef<CharSourceRange>::const_iterator RI = Ranges.begin(),
         RE = Ranges.end();
         RI != RE; ++RI) {
      // Ignore invalid ranges.
      if (!RI->isValid()) continue;

      auto &SM = Loc.getManager();
      SourceLocation B = SM.getExpansionLoc(RI->getBegin());
      CharSourceRange ERange = SM.getExpansionRange(RI->getEnd());
      SourceLocation E = ERange.getEnd();
      bool IsTokenRange = ERange.isTokenRange();

      std::pair<FileID, unsigned> BInfo = SM.getDecomposedLoc(B);
      std::pair<FileID, unsigned> EInfo = SM.getDecomposedLoc(E);

      // If the start or end of the range is in another file, just discard
      // it.
      if (BInfo.first != CaretFileID || EInfo.first != CaretFileID)
        continue;

      // Add in the length of the token, so that we cover multi-char
      // tokens.
      unsigned TokSize = 0;
      if (IsTokenRange)
        TokSize = Lexer::MeasureTokenLength(E, SM, LangOpts);

      FullSourceLoc BF(B, SM), EF(E, SM);
      OS << '{'
         << BF.getLineNumber() << ':' << BF.getColumnNumber() << '-'
         << EF.getLineNumber() << ':' << (EF.getColumnNumber() + TokSize)
         << '}';
      PrintedRange = true;
    }

    if (PrintedRange)
      OS << ':';
  }
  OS << ' ';
}

void TextDiagnostic::emitIncludeLocation(FullSourceLoc Loc, PresumedLoc PLoc) {
  if (DiagOpts->ShowLocation && PLoc.isValid())
    OS << "In file included from " << PLoc.getFilename() << ':'
       << PLoc.getLine() << ":\n";
  else
    OS << "In included file:\n";
}

void TextDiagnostic::emitImportLocation(FullSourceLoc Loc, PresumedLoc PLoc,
                                        StringRef ModuleName) {
  if (DiagOpts->ShowLocation && PLoc.isValid())
    OS << "In module '" << ModuleName << "' imported from "
       << PLoc.getFilename() << ':' << PLoc.getLine() << ":\n";
  else
    OS << "In module '" << ModuleName << "':\n";
}

void TextDiagnostic::emitBuildingModuleLocation(FullSourceLoc Loc,
                                                PresumedLoc PLoc,
                                                StringRef ModuleName) {
  if (DiagOpts->ShowLocation && PLoc.isValid())
    OS << "While building module '" << ModuleName << "' imported from "
      << PLoc.getFilename() << ':' << PLoc.getLine() << ":\n";
  else
    OS << "While building module '" << ModuleName << "':\n";
}

/// Find the suitable set of lines to show to include a set of ranges.
static llvm::Optional<std::pair<unsigned, unsigned>>
findLinesForRange(const CharSourceRange &R, FileID FID,
                  const SourceManager &SM) {
  if (!R.isValid()) return None;

  SourceLocation Begin = R.getBegin();
  SourceLocation End = R.getEnd();
  if (SM.getFileID(Begin) != FID || SM.getFileID(End) != FID)
    return None;

  return std::make_pair(SM.getExpansionLineNumber(Begin),
                        SM.getExpansionLineNumber(End));
}

/// Add as much of range B into range A as possible without exceeding a maximum
/// size of MaxRange. Ranges are inclusive.
static std::pair<unsigned, unsigned>
maybeAddRange(std::pair<unsigned, unsigned> A, std::pair<unsigned, unsigned> B,
              unsigned MaxRange) {
  // If A is already the maximum size, we're done.
  unsigned Slack = MaxRange - (A.second - A.first + 1);
  if (Slack == 0)
    return A;

  // Easy case: merge succeeds within MaxRange.
  unsigned Min = std::min(A.first, B.first);
  unsigned Max = std::max(A.second, B.second);
  if (Max - Min + 1 <= MaxRange)
    return {Min, Max};

  // If we can't reach B from A within MaxRange, there's nothing to do.
  // Don't add lines to the range that contain nothing interesting.
  if ((B.first > A.first && B.first - A.first + 1 > MaxRange) ||
      (B.second < A.second && A.second - B.second + 1 > MaxRange))
    return A;

  // Otherwise, expand A towards B to produce a range of size MaxRange. We
  // attempt to expand by the same amount in both directions if B strictly
  // contains A.

  // Expand downwards by up to half the available amount, then upwards as
  // much as possible, then downwards as much as possible.
  A.second = std::min(A.second + (Slack + 1) / 2, Max);
  Slack = MaxRange - (A.second - A.first + 1);
  A.first = std::max(Min + Slack, A.first) - Slack;
  A.second = std::min(A.first + MaxRange - 1, Max);
  return A;
}

/// Highlight a SourceRange (with ~'s) for any characters on LineNo.
static void highlightRange(const CharSourceRange &R,
                           unsigned LineNo, FileID FID,
                           const SourceColumnMap &map,
                           std::string &CaretLine,
                           const SourceManager &SM,
                           const LangOptions &LangOpts) {
  if (!R.isValid()) return;

  SourceLocation Begin = R.getBegin();
  SourceLocation End = R.getEnd();

  unsigned StartLineNo = SM.getExpansionLineNumber(Begin);
  if (StartLineNo > LineNo || SM.getFileID(Begin) != FID)
    return;  // No intersection.

  unsigned EndLineNo = SM.getExpansionLineNumber(End);
  if (EndLineNo < LineNo || SM.getFileID(End) != FID)
    return;  // No intersection.

  // Compute the column number of the start.
  unsigned StartColNo = 0;
  if (StartLineNo == LineNo) {
    StartColNo = SM.getExpansionColumnNumber(Begin);
    if (StartColNo) --StartColNo;  // Zero base the col #.
  }

  // Compute the column number of the end.
  unsigned EndColNo = map.getSourceLine().size();
  if (EndLineNo == LineNo) {
    EndColNo = SM.getExpansionColumnNumber(End);
    if (EndColNo) {
      --EndColNo;  // Zero base the col #.

      // Add in the length of the token, so that we cover multi-char tokens if
      // this is a token range.
      if (R.isTokenRange())
        EndColNo += Lexer::MeasureTokenLength(End, SM, LangOpts);
    } else {
      EndColNo = CaretLine.size();
    }
  }

  assert(StartColNo <= EndColNo && "Invalid range!");

  // Check that a token range does not highlight only whitespace.
  if (R.isTokenRange()) {
    // Pick the first non-whitespace column.
    while (StartColNo < map.getSourceLine().size() &&
           (map.getSourceLine()[StartColNo] == ' ' ||
            map.getSourceLine()[StartColNo] == '\t'))
      StartColNo = map.startOfNextColumn(StartColNo);

    // Pick the last non-whitespace column.
    if (EndColNo > map.getSourceLine().size())
      EndColNo = map.getSourceLine().size();
    while (EndColNo &&
           (map.getSourceLine()[EndColNo-1] == ' ' ||
            map.getSourceLine()[EndColNo-1] == '\t'))
      EndColNo = map.startOfPreviousColumn(EndColNo);

    // If the start/end passed each other, then we are trying to highlight a
    // range that just exists in whitespace. That most likely means we have
    // a multi-line highlighting range that covers a blank line.
    if (StartColNo > EndColNo) {
      assert(StartLineNo != EndLineNo && "trying to highlight whitespace");
      StartColNo = EndColNo;
    }
  }

  assert(StartColNo <= map.getSourceLine().size() && "Invalid range!");
  assert(EndColNo <= map.getSourceLine().size() && "Invalid range!");

  // Fill the range with ~'s.
  StartColNo = map.byteToContainingColumn(StartColNo);
  EndColNo = map.byteToContainingColumn(EndColNo);

  assert(StartColNo <= EndColNo && "Invalid range!");
  if (CaretLine.size() < EndColNo)
    CaretLine.resize(EndColNo,' ');
  std::fill(CaretLine.begin()+StartColNo,CaretLine.begin()+EndColNo,'~');
}

static std::string buildFixItInsertionLine(FileID FID,
                                           unsigned LineNo,
                                           const SourceColumnMap &map,
                                           ArrayRef<FixItHint> Hints,
                                           const SourceManager &SM,
                                           const DiagnosticOptions *DiagOpts) {
  std::string FixItInsertionLine;
  if (Hints.empty() || !DiagOpts->ShowFixits)
    return FixItInsertionLine;
  unsigned PrevHintEndCol = 0;

  for (ArrayRef<FixItHint>::iterator I = Hints.begin(), E = Hints.end();
       I != E; ++I) {
    if (!I->CodeToInsert.empty()) {
      // We have an insertion hint. Determine whether the inserted
      // code contains no newlines and is on the same line as the caret.
      std::pair<FileID, unsigned> HintLocInfo
        = SM.getDecomposedExpansionLoc(I->RemoveRange.getBegin());
      if (FID == HintLocInfo.first &&
          LineNo == SM.getLineNumber(HintLocInfo.first, HintLocInfo.second) &&
          StringRef(I->CodeToInsert).find_first_of("\n\r") == StringRef::npos) {
        // Insert the new code into the line just below the code
        // that the user wrote.
        // Note: When modifying this function, be very careful about what is a
        // "column" (printed width, platform-dependent) and what is a
        // "byte offset" (SourceManager "column").
        unsigned HintByteOffset
          = SM.getColumnNumber(HintLocInfo.first, HintLocInfo.second) - 1;

        // The hint must start inside the source or right at the end
        assert(HintByteOffset < static_cast<unsigned>(map.bytes())+1);
        unsigned HintCol = map.byteToContainingColumn(HintByteOffset);

        // If we inserted a long previous hint, push this one forwards, and add
        // an extra space to show that this is not part of the previous
        // completion. This is sort of the best we can do when two hints appear
        // to overlap.
        //
        // Note that if this hint is located immediately after the previous
        // hint, no space will be added, since the location is more important.
        if (HintCol < PrevHintEndCol)
          HintCol = PrevHintEndCol + 1;

        // This should NOT use HintByteOffset, because the source might have
        // Unicode characters in earlier columns.
        unsigned NewFixItLineSize = FixItInsertionLine.size() +
          (HintCol - PrevHintEndCol) + I->CodeToInsert.size();
        if (NewFixItLineSize > FixItInsertionLine.size())
          FixItInsertionLine.resize(NewFixItLineSize, ' ');

        std::copy(I->CodeToInsert.begin(), I->CodeToInsert.end(),
                  FixItInsertionLine.end() - I->CodeToInsert.size());

        PrevHintEndCol =
          HintCol + llvm::sys::locale::columnWidth(I->CodeToInsert);
      }
    }
  }

  expandTabs(FixItInsertionLine, DiagOpts->TabStop);

  return FixItInsertionLine;
}

/// Emit a code snippet and caret line.
///
/// This routine emits a single line's code snippet and caret line..
///
/// \param Loc The location for the caret.
/// \param Ranges The underlined ranges for this code snippet.
/// \param Hints The FixIt hints active for this diagnostic.
void TextDiagnostic::emitSnippetAndCaret(
    FullSourceLoc Loc, DiagnosticsEngine::Level Level,
    SmallVectorImpl<CharSourceRange> &Ranges, ArrayRef<FixItHint> Hints) {
  assert(Loc.isValid() && "must have a valid source location here");
  assert(Loc.isFileID() && "must have a file location here");

  // If caret diagnostics are enabled and we have location, we want to
  // emit the caret.  However, we only do this if the location moved
  // from the last diagnostic, if the last diagnostic was a note that
  // was part of a different warning or error diagnostic, or if the
  // diagnostic has ranges.  We don't want to emit the same caret
  // multiple times if one loc has multiple diagnostics.
  if (!DiagOpts->ShowCarets)
    return;
  if (Loc == LastLoc && Ranges.empty() && Hints.empty() &&
      (LastLevel != DiagnosticsEngine::Note || Level == LastLevel))
    return;

  // Decompose the location into a FID/Offset pair.
  std::pair<FileID, unsigned> LocInfo = Loc.getDecomposedLoc();
  FileID FID = LocInfo.first;
  const SourceManager &SM = Loc.getManager();

  // Get information about the buffer it points into.
  bool Invalid = false;
  StringRef BufData = Loc.getBufferData(&Invalid);
  if (Invalid)
    return;

  unsigned CaretLineNo = Loc.getLineNumber();
  unsigned CaretColNo = Loc.getColumnNumber();

  // Arbitrarily stop showing snippets when the line is too long.
  static const size_t MaxLineLengthToPrint = 4096;
  if (CaretColNo > MaxLineLengthToPrint)
    return;

  // Find the set of lines to include.
  const unsigned MaxLines = DiagOpts->SnippetLineLimit;
  std::pair<unsigned, unsigned> Lines = {CaretLineNo, CaretLineNo};
  for (SmallVectorImpl<CharSourceRange>::iterator I = Ranges.begin(),
                                                  E = Ranges.end();
       I != E; ++I)
    if (auto OptionalRange = findLinesForRange(*I, FID, SM))
      Lines = maybeAddRange(Lines, *OptionalRange, MaxLines);

  for (unsigned LineNo = Lines.first; LineNo != Lines.second + 1; ++LineNo) {
    const char *BufStart = BufData.data();
    const char *BufEnd = BufStart + BufData.size();

    // Rewind from the current position to the start of the line.
    const char *LineStart =
        BufStart +
        SM.getDecomposedLoc(SM.translateLineCol(FID, LineNo, 1)).second;
    if (LineStart == BufEnd)
      break;

    // Compute the line end.
    const char *LineEnd = LineStart;
    while (*LineEnd != '\n' && *LineEnd != '\r' && LineEnd != BufEnd)
      ++LineEnd;

    // Arbitrarily stop showing snippets when the line is too long.
    // FIXME: Don't print any lines in this case.
    if (size_t(LineEnd - LineStart) > MaxLineLengthToPrint)
      return;

    // Trim trailing null-bytes.
    StringRef Line(LineStart, LineEnd - LineStart);
    while (!Line.empty() && Line.back() == '\0' &&
           (LineNo != CaretLineNo || Line.size() > CaretColNo))
      Line = Line.drop_back();

    // Copy the line of code into an std::string for ease of manipulation.
    std::string SourceLine(Line.begin(), Line.end());

    // Build the byte to column map.
    const SourceColumnMap sourceColMap(SourceLine, DiagOpts->TabStop);

    // Create a line for the caret that is filled with spaces that is the same
    // number of columns as the line of source code.
    std::string CaretLine(sourceColMap.columns(), ' ');

    // Highlight all of the characters covered by Ranges with ~ characters.
    for (SmallVectorImpl<CharSourceRange>::iterator I = Ranges.begin(),
                                                    E = Ranges.end();
         I != E; ++I)
      highlightRange(*I, LineNo, FID, sourceColMap, CaretLine, SM, LangOpts);

    // Next, insert the caret itself.
    if (CaretLineNo == LineNo) {
      CaretColNo = sourceColMap.byteToContainingColumn(CaretColNo - 1);
      if (CaretLine.size() < CaretColNo + 1)
        CaretLine.resize(CaretColNo + 1, ' ');
      CaretLine[CaretColNo] = '^';
    }

    std::string FixItInsertionLine = buildFixItInsertionLine(
        FID, LineNo, sourceColMap, Hints, SM, DiagOpts.get());

    // If the source line is too long for our terminal, select only the
    // "interesting" source region within that line.
    unsigned Columns = DiagOpts->MessageLength;
    if (Columns)
      selectInterestingSourceRegion(SourceLine, CaretLine, FixItInsertionLine,
                                    Columns, sourceColMap);

    // If we are in -fdiagnostics-print-source-range-info mode, we are trying
    // to produce easily machine parsable output.  Add a space before the
    // source line and the caret to make it trivial to tell the main diagnostic
    // line from what the user is intended to see.
    if (DiagOpts->ShowSourceRanges) {
      SourceLine = ' ' + SourceLine;
      CaretLine = ' ' + CaretLine;
    }

    // Finally, remove any blank spaces from the end of CaretLine.
    while (!CaretLine.empty() && CaretLine[CaretLine.size() - 1] == ' ')
      CaretLine.erase(CaretLine.end() - 1);

    // Emit what we have computed.
    emitSnippet(SourceLine);

    if (!CaretLine.empty()) {
      if (DiagOpts->ShowColors)
        OS.changeColor(caretColor, true);
      OS << CaretLine << '\n';
      if (DiagOpts->ShowColors)
        OS.resetColor();
    }

    if (!FixItInsertionLine.empty()) {
      if (DiagOpts->ShowColors)
        // Print fixit line in color
        OS.changeColor(fixitColor, false);
      if (DiagOpts->ShowSourceRanges)
        OS << ' ';
      OS << FixItInsertionLine << '\n';
      if (DiagOpts->ShowColors)
        OS.resetColor();
    }
  }

  // Print out any parseable fixit information requested by the options.
  emitParseableFixits(Hints, SM);
}

void TextDiagnostic::emitSnippet(StringRef line) {
  if (line.empty())
    return;

  size_t i = 0;

  std::string to_print;
  bool print_reversed = false;

  while (i<line.size()) {
    std::pair<SmallString<16>,bool> res
        = printableTextForNextCharacter(line, &i, DiagOpts->TabStop);
    bool was_printable = res.second;

    if (DiagOpts->ShowColors && was_printable == print_reversed) {
      if (print_reversed)
        OS.reverseColor();
      OS << to_print;
      to_print.clear();
      if (DiagOpts->ShowColors)
        OS.resetColor();
    }

    print_reversed = !was_printable;
    to_print += res.first.str();
  }

  if (print_reversed && DiagOpts->ShowColors)
    OS.reverseColor();
  OS << to_print;
  if (print_reversed && DiagOpts->ShowColors)
    OS.resetColor();

  OS << '\n';
}

void TextDiagnostic::emitParseableFixits(ArrayRef<FixItHint> Hints,
                                         const SourceManager &SM) {
  if (!DiagOpts->ShowParseableFixits)
    return;

  // We follow FixItRewriter's example in not (yet) handling
  // fix-its in macros.
  for (ArrayRef<FixItHint>::iterator I = Hints.begin(), E = Hints.end();
       I != E; ++I) {
    if (I->RemoveRange.isInvalid() ||
        I->RemoveRange.getBegin().isMacroID() ||
        I->RemoveRange.getEnd().isMacroID())
      return;
  }

  for (ArrayRef<FixItHint>::iterator I = Hints.begin(), E = Hints.end();
       I != E; ++I) {
    SourceLocation BLoc = I->RemoveRange.getBegin();
    SourceLocation ELoc = I->RemoveRange.getEnd();

    std::pair<FileID, unsigned> BInfo = SM.getDecomposedLoc(BLoc);
    std::pair<FileID, unsigned> EInfo = SM.getDecomposedLoc(ELoc);

    // Adjust for token ranges.
    if (I->RemoveRange.isTokenRange())
      EInfo.second += Lexer::MeasureTokenLength(ELoc, SM, LangOpts);

    // We specifically do not do word-wrapping or tab-expansion here,
    // because this is supposed to be easy to parse.
    PresumedLoc PLoc = SM.getPresumedLoc(BLoc);
    if (PLoc.isInvalid())
      break;

    OS << "fix-it:\"";
    OS.write_escaped(PLoc.getFilename());
    OS << "\":{" << SM.getLineNumber(BInfo.first, BInfo.second)
      << ':' << SM.getColumnNumber(BInfo.first, BInfo.second)
      << '-' << SM.getLineNumber(EInfo.first, EInfo.second)
      << ':' << SM.getColumnNumber(EInfo.first, EInfo.second)
      << "}:\"";
    OS.write_escaped(I->CodeToInsert);
    OS << "\"\n";
  }
}
