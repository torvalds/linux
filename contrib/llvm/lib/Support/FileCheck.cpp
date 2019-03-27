//===- FileCheck.cpp - Check that File's Contents match what is expected --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// FileCheck does a line-by line check of a file that validates whether it
// contains the expected content.  This is useful for regression tests etc.
//
// This file implements most of the API that will be used by the FileCheck utility
// as well as various unittests.
//===----------------------------------------------------------------------===//

#include "llvm/Support/FileCheck.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/FormatVariadic.h"
#include <cstdint>
#include <list>
#include <map>
#include <tuple>
#include <utility>

using namespace llvm;

/// Parses the given string into the Pattern.
///
/// \p Prefix provides which prefix is being matched, \p SM provides the
/// SourceMgr used for error reports, and \p LineNumber is the line number in
/// the input file from which the pattern string was read. Returns true in
/// case of an error, false otherwise.
bool FileCheckPattern::ParsePattern(StringRef PatternStr, StringRef Prefix,
                           SourceMgr &SM, unsigned LineNumber,
                           const FileCheckRequest &Req) {
  bool MatchFullLinesHere = Req.MatchFullLines && CheckTy != Check::CheckNot;

  this->LineNumber = LineNumber;
  PatternLoc = SMLoc::getFromPointer(PatternStr.data());

  if (!(Req.NoCanonicalizeWhiteSpace && Req.MatchFullLines))
    // Ignore trailing whitespace.
    while (!PatternStr.empty() &&
           (PatternStr.back() == ' ' || PatternStr.back() == '\t'))
      PatternStr = PatternStr.substr(0, PatternStr.size() - 1);

  // Check that there is something on the line.
  if (PatternStr.empty() && CheckTy != Check::CheckEmpty) {
    SM.PrintMessage(PatternLoc, SourceMgr::DK_Error,
                    "found empty check string with prefix '" + Prefix + ":'");
    return true;
  }

  if (!PatternStr.empty() && CheckTy == Check::CheckEmpty) {
    SM.PrintMessage(
        PatternLoc, SourceMgr::DK_Error,
        "found non-empty check string for empty check with prefix '" + Prefix +
            ":'");
    return true;
  }

  if (CheckTy == Check::CheckEmpty) {
    RegExStr = "(\n$)";
    return false;
  }

  // Check to see if this is a fixed string, or if it has regex pieces.
  if (!MatchFullLinesHere &&
      (PatternStr.size() < 2 || (PatternStr.find("{{") == StringRef::npos &&
                                 PatternStr.find("[[") == StringRef::npos))) {
    FixedStr = PatternStr;
    return false;
  }

  if (MatchFullLinesHere) {
    RegExStr += '^';
    if (!Req.NoCanonicalizeWhiteSpace)
      RegExStr += " *";
  }

  // Paren value #0 is for the fully matched string.  Any new parenthesized
  // values add from there.
  unsigned CurParen = 1;

  // Otherwise, there is at least one regex piece.  Build up the regex pattern
  // by escaping scary characters in fixed strings, building up one big regex.
  while (!PatternStr.empty()) {
    // RegEx matches.
    if (PatternStr.startswith("{{")) {
      // This is the start of a regex match.  Scan for the }}.
      size_t End = PatternStr.find("}}");
      if (End == StringRef::npos) {
        SM.PrintMessage(SMLoc::getFromPointer(PatternStr.data()),
                        SourceMgr::DK_Error,
                        "found start of regex string with no end '}}'");
        return true;
      }

      // Enclose {{}} patterns in parens just like [[]] even though we're not
      // capturing the result for any purpose.  This is required in case the
      // expression contains an alternation like: CHECK:  abc{{x|z}}def.  We
      // want this to turn into: "abc(x|z)def" not "abcx|zdef".
      RegExStr += '(';
      ++CurParen;

      if (AddRegExToRegEx(PatternStr.substr(2, End - 2), CurParen, SM))
        return true;
      RegExStr += ')';

      PatternStr = PatternStr.substr(End + 2);
      continue;
    }

    // Named RegEx matches.  These are of two forms: [[foo:.*]] which matches .*
    // (or some other regex) and assigns it to the FileCheck variable 'foo'. The
    // second form is [[foo]] which is a reference to foo.  The variable name
    // itself must be of the form "[a-zA-Z_][0-9a-zA-Z_]*", otherwise we reject
    // it.  This is to catch some common errors.
    if (PatternStr.startswith("[[")) {
      // Find the closing bracket pair ending the match.  End is going to be an
      // offset relative to the beginning of the match string.
      size_t End = FindRegexVarEnd(PatternStr.substr(2), SM);

      if (End == StringRef::npos) {
        SM.PrintMessage(SMLoc::getFromPointer(PatternStr.data()),
                        SourceMgr::DK_Error,
                        "invalid named regex reference, no ]] found");
        return true;
      }

      StringRef MatchStr = PatternStr.substr(2, End);
      PatternStr = PatternStr.substr(End + 4);

      // Get the regex name (e.g. "foo").
      size_t NameEnd = MatchStr.find(':');
      StringRef Name = MatchStr.substr(0, NameEnd);

      if (Name.empty()) {
        SM.PrintMessage(SMLoc::getFromPointer(Name.data()), SourceMgr::DK_Error,
                        "invalid name in named regex: empty name");
        return true;
      }

      // Verify that the name/expression is well formed. FileCheck currently
      // supports @LINE, @LINE+number, @LINE-number expressions. The check here
      // is relaxed, more strict check is performed in \c EvaluateExpression.
      bool IsExpression = false;
      for (unsigned i = 0, e = Name.size(); i != e; ++i) {
        if (i == 0) {
          if (Name[i] == '$')  // Global vars start with '$'
            continue;
          if (Name[i] == '@') {
            if (NameEnd != StringRef::npos) {
              SM.PrintMessage(SMLoc::getFromPointer(Name.data()),
                              SourceMgr::DK_Error,
                              "invalid name in named regex definition");
              return true;
            }
            IsExpression = true;
            continue;
          }
        }
        if (Name[i] != '_' && !isalnum(Name[i]) &&
            (!IsExpression || (Name[i] != '+' && Name[i] != '-'))) {
          SM.PrintMessage(SMLoc::getFromPointer(Name.data() + i),
                          SourceMgr::DK_Error, "invalid name in named regex");
          return true;
        }
      }

      // Name can't start with a digit.
      if (isdigit(static_cast<unsigned char>(Name[0]))) {
        SM.PrintMessage(SMLoc::getFromPointer(Name.data()), SourceMgr::DK_Error,
                        "invalid name in named regex");
        return true;
      }

      // Handle [[foo]].
      if (NameEnd == StringRef::npos) {
        // Handle variables that were defined earlier on the same line by
        // emitting a backreference.
        if (VariableDefs.find(Name) != VariableDefs.end()) {
          unsigned VarParenNum = VariableDefs[Name];
          if (VarParenNum < 1 || VarParenNum > 9) {
            SM.PrintMessage(SMLoc::getFromPointer(Name.data()),
                            SourceMgr::DK_Error,
                            "Can't back-reference more than 9 variables");
            return true;
          }
          AddBackrefToRegEx(VarParenNum);
        } else {
          VariableUses.push_back(std::make_pair(Name, RegExStr.size()));
        }
        continue;
      }

      // Handle [[foo:.*]].
      VariableDefs[Name] = CurParen;
      RegExStr += '(';
      ++CurParen;

      if (AddRegExToRegEx(MatchStr.substr(NameEnd + 1), CurParen, SM))
        return true;

      RegExStr += ')';
    }

    // Handle fixed string matches.
    // Find the end, which is the start of the next regex.
    size_t FixedMatchEnd = PatternStr.find("{{");
    FixedMatchEnd = std::min(FixedMatchEnd, PatternStr.find("[["));
    RegExStr += Regex::escape(PatternStr.substr(0, FixedMatchEnd));
    PatternStr = PatternStr.substr(FixedMatchEnd);
  }

  if (MatchFullLinesHere) {
    if (!Req.NoCanonicalizeWhiteSpace)
      RegExStr += " *";
    RegExStr += '$';
  }

  return false;
}

bool FileCheckPattern::AddRegExToRegEx(StringRef RS, unsigned &CurParen, SourceMgr &SM) {
  Regex R(RS);
  std::string Error;
  if (!R.isValid(Error)) {
    SM.PrintMessage(SMLoc::getFromPointer(RS.data()), SourceMgr::DK_Error,
                    "invalid regex: " + Error);
    return true;
  }

  RegExStr += RS.str();
  CurParen += R.getNumMatches();
  return false;
}

void FileCheckPattern::AddBackrefToRegEx(unsigned BackrefNum) {
  assert(BackrefNum >= 1 && BackrefNum <= 9 && "Invalid backref number");
  std::string Backref = std::string("\\") + std::string(1, '0' + BackrefNum);
  RegExStr += Backref;
}

/// Evaluates expression and stores the result to \p Value.
///
/// Returns true on success and false when the expression has invalid syntax.
bool FileCheckPattern::EvaluateExpression(StringRef Expr, std::string &Value) const {
  // The only supported expression is @LINE([\+-]\d+)?
  if (!Expr.startswith("@LINE"))
    return false;
  Expr = Expr.substr(StringRef("@LINE").size());
  int Offset = 0;
  if (!Expr.empty()) {
    if (Expr[0] == '+')
      Expr = Expr.substr(1);
    else if (Expr[0] != '-')
      return false;
    if (Expr.getAsInteger(10, Offset))
      return false;
  }
  Value = llvm::itostr(LineNumber + Offset);
  return true;
}

/// Matches the pattern string against the input buffer \p Buffer
///
/// This returns the position that is matched or npos if there is no match. If
/// there is a match, the size of the matched string is returned in \p
/// MatchLen.
///
/// The \p VariableTable StringMap provides the current values of filecheck
/// variables and is updated if this match defines new values.
size_t FileCheckPattern::Match(StringRef Buffer, size_t &MatchLen,
                      StringMap<StringRef> &VariableTable) const {
  // If this is the EOF pattern, match it immediately.
  if (CheckTy == Check::CheckEOF) {
    MatchLen = 0;
    return Buffer.size();
  }

  // If this is a fixed string pattern, just match it now.
  if (!FixedStr.empty()) {
    MatchLen = FixedStr.size();
    return Buffer.find(FixedStr);
  }

  // Regex match.

  // If there are variable uses, we need to create a temporary string with the
  // actual value.
  StringRef RegExToMatch = RegExStr;
  std::string TmpStr;
  if (!VariableUses.empty()) {
    TmpStr = RegExStr;

    unsigned InsertOffset = 0;
    for (const auto &VariableUse : VariableUses) {
      std::string Value;

      if (VariableUse.first[0] == '@') {
        if (!EvaluateExpression(VariableUse.first, Value))
          return StringRef::npos;
      } else {
        StringMap<StringRef>::iterator it =
            VariableTable.find(VariableUse.first);
        // If the variable is undefined, return an error.
        if (it == VariableTable.end())
          return StringRef::npos;

        // Look up the value and escape it so that we can put it into the regex.
        Value += Regex::escape(it->second);
      }

      // Plop it into the regex at the adjusted offset.
      TmpStr.insert(TmpStr.begin() + VariableUse.second + InsertOffset,
                    Value.begin(), Value.end());
      InsertOffset += Value.size();
    }

    // Match the newly constructed regex.
    RegExToMatch = TmpStr;
  }

  SmallVector<StringRef, 4> MatchInfo;
  if (!Regex(RegExToMatch, Regex::Newline).match(Buffer, &MatchInfo))
    return StringRef::npos;

  // Successful regex match.
  assert(!MatchInfo.empty() && "Didn't get any match");
  StringRef FullMatch = MatchInfo[0];

  // If this defines any variables, remember their values.
  for (const auto &VariableDef : VariableDefs) {
    assert(VariableDef.second < MatchInfo.size() && "Internal paren error");
    VariableTable[VariableDef.first] = MatchInfo[VariableDef.second];
  }

  // Like CHECK-NEXT, CHECK-EMPTY's match range is considered to start after
  // the required preceding newline, which is consumed by the pattern in the
  // case of CHECK-EMPTY but not CHECK-NEXT.
  size_t MatchStartSkip = CheckTy == Check::CheckEmpty;
  MatchLen = FullMatch.size() - MatchStartSkip;
  return FullMatch.data() - Buffer.data() + MatchStartSkip;
}


/// Computes an arbitrary estimate for the quality of matching this pattern at
/// the start of \p Buffer; a distance of zero should correspond to a perfect
/// match.
unsigned
FileCheckPattern::ComputeMatchDistance(StringRef Buffer,
                              const StringMap<StringRef> &VariableTable) const {
  // Just compute the number of matching characters. For regular expressions, we
  // just compare against the regex itself and hope for the best.
  //
  // FIXME: One easy improvement here is have the regex lib generate a single
  // example regular expression which matches, and use that as the example
  // string.
  StringRef ExampleString(FixedStr);
  if (ExampleString.empty())
    ExampleString = RegExStr;

  // Only compare up to the first line in the buffer, or the string size.
  StringRef BufferPrefix = Buffer.substr(0, ExampleString.size());
  BufferPrefix = BufferPrefix.split('\n').first;
  return BufferPrefix.edit_distance(ExampleString);
}

void FileCheckPattern::PrintVariableUses(const SourceMgr &SM, StringRef Buffer,
                                const StringMap<StringRef> &VariableTable,
                                SMRange MatchRange) const {
  // If this was a regular expression using variables, print the current
  // variable values.
  if (!VariableUses.empty()) {
    for (const auto &VariableUse : VariableUses) {
      SmallString<256> Msg;
      raw_svector_ostream OS(Msg);
      StringRef Var = VariableUse.first;
      if (Var[0] == '@') {
        std::string Value;
        if (EvaluateExpression(Var, Value)) {
          OS << "with expression \"";
          OS.write_escaped(Var) << "\" equal to \"";
          OS.write_escaped(Value) << "\"";
        } else {
          OS << "uses incorrect expression \"";
          OS.write_escaped(Var) << "\"";
        }
      } else {
        StringMap<StringRef>::const_iterator it = VariableTable.find(Var);

        // Check for undefined variable references.
        if (it == VariableTable.end()) {
          OS << "uses undefined variable \"";
          OS.write_escaped(Var) << "\"";
        } else {
          OS << "with variable \"";
          OS.write_escaped(Var) << "\" equal to \"";
          OS.write_escaped(it->second) << "\"";
        }
      }

      if (MatchRange.isValid())
        SM.PrintMessage(MatchRange.Start, SourceMgr::DK_Note, OS.str(),
                        {MatchRange});
      else
        SM.PrintMessage(SMLoc::getFromPointer(Buffer.data()),
                        SourceMgr::DK_Note, OS.str());
    }
  }
}

static SMRange ProcessMatchResult(FileCheckDiag::MatchType MatchTy,
                                  const SourceMgr &SM, SMLoc Loc,
                                  Check::FileCheckType CheckTy,
                                  StringRef Buffer, size_t Pos, size_t Len,
                                  std::vector<FileCheckDiag> *Diags,
                                  bool AdjustPrevDiag = false) {
  SMLoc Start = SMLoc::getFromPointer(Buffer.data() + Pos);
  SMLoc End = SMLoc::getFromPointer(Buffer.data() + Pos + Len);
  SMRange Range(Start, End);
  if (Diags) {
    if (AdjustPrevDiag)
      Diags->rbegin()->MatchTy = MatchTy;
    else
      Diags->emplace_back(SM, CheckTy, Loc, MatchTy, Range);
  }
  return Range;
}

void FileCheckPattern::PrintFuzzyMatch(
    const SourceMgr &SM, StringRef Buffer,
    const StringMap<StringRef> &VariableTable,
    std::vector<FileCheckDiag> *Diags) const {
  // Attempt to find the closest/best fuzzy match.  Usually an error happens
  // because some string in the output didn't exactly match. In these cases, we
  // would like to show the user a best guess at what "should have" matched, to
  // save them having to actually check the input manually.
  size_t NumLinesForward = 0;
  size_t Best = StringRef::npos;
  double BestQuality = 0;

  // Use an arbitrary 4k limit on how far we will search.
  for (size_t i = 0, e = std::min(size_t(4096), Buffer.size()); i != e; ++i) {
    if (Buffer[i] == '\n')
      ++NumLinesForward;

    // Patterns have leading whitespace stripped, so skip whitespace when
    // looking for something which looks like a pattern.
    if (Buffer[i] == ' ' || Buffer[i] == '\t')
      continue;

    // Compute the "quality" of this match as an arbitrary combination of the
    // match distance and the number of lines skipped to get to this match.
    unsigned Distance = ComputeMatchDistance(Buffer.substr(i), VariableTable);
    double Quality = Distance + (NumLinesForward / 100.);

    if (Quality < BestQuality || Best == StringRef::npos) {
      Best = i;
      BestQuality = Quality;
    }
  }

  // Print the "possible intended match here" line if we found something
  // reasonable and not equal to what we showed in the "scanning from here"
  // line.
  if (Best && Best != StringRef::npos && BestQuality < 50) {
    SMRange MatchRange =
        ProcessMatchResult(FileCheckDiag::MatchFuzzy, SM, getLoc(),
                           getCheckTy(), Buffer, Best, 0, Diags);
    SM.PrintMessage(MatchRange.Start, SourceMgr::DK_Note,
                    "possible intended match here");

    // FIXME: If we wanted to be really friendly we would show why the match
    // failed, as it can be hard to spot simple one character differences.
  }
}

/// Finds the closing sequence of a regex variable usage or definition.
///
/// \p Str has to point in the beginning of the definition (right after the
/// opening sequence). Returns the offset of the closing sequence within Str,
/// or npos if it was not found.
size_t FileCheckPattern::FindRegexVarEnd(StringRef Str, SourceMgr &SM) {
  // Offset keeps track of the current offset within the input Str
  size_t Offset = 0;
  // [...] Nesting depth
  size_t BracketDepth = 0;

  while (!Str.empty()) {
    if (Str.startswith("]]") && BracketDepth == 0)
      return Offset;
    if (Str[0] == '\\') {
      // Backslash escapes the next char within regexes, so skip them both.
      Str = Str.substr(2);
      Offset += 2;
    } else {
      switch (Str[0]) {
      default:
        break;
      case '[':
        BracketDepth++;
        break;
      case ']':
        if (BracketDepth == 0) {
          SM.PrintMessage(SMLoc::getFromPointer(Str.data()),
                          SourceMgr::DK_Error,
                          "missing closing \"]\" for regex variable");
          exit(1);
        }
        BracketDepth--;
        break;
      }
      Str = Str.substr(1);
      Offset++;
    }
  }

  return StringRef::npos;
}

/// Canonicalize whitespaces in the file. Line endings are replaced with
/// UNIX-style '\n'.
StringRef
llvm::FileCheck::CanonicalizeFile(MemoryBuffer &MB,
                                  SmallVectorImpl<char> &OutputBuffer) {
  OutputBuffer.reserve(MB.getBufferSize());

  for (const char *Ptr = MB.getBufferStart(), *End = MB.getBufferEnd();
       Ptr != End; ++Ptr) {
    // Eliminate trailing dosish \r.
    if (Ptr <= End - 2 && Ptr[0] == '\r' && Ptr[1] == '\n') {
      continue;
    }

    // If current char is not a horizontal whitespace or if horizontal
    // whitespace canonicalization is disabled, dump it to output as is.
    if (Req.NoCanonicalizeWhiteSpace || (*Ptr != ' ' && *Ptr != '\t')) {
      OutputBuffer.push_back(*Ptr);
      continue;
    }

    // Otherwise, add one space and advance over neighboring space.
    OutputBuffer.push_back(' ');
    while (Ptr + 1 != End && (Ptr[1] == ' ' || Ptr[1] == '\t'))
      ++Ptr;
  }

  // Add a null byte and then return all but that byte.
  OutputBuffer.push_back('\0');
  return StringRef(OutputBuffer.data(), OutputBuffer.size() - 1);
}

FileCheckDiag::FileCheckDiag(const SourceMgr &SM,
                             const Check::FileCheckType &CheckTy,
                             SMLoc CheckLoc, MatchType MatchTy,
                             SMRange InputRange)
    : CheckTy(CheckTy), MatchTy(MatchTy) {
  auto Start = SM.getLineAndColumn(InputRange.Start);
  auto End = SM.getLineAndColumn(InputRange.End);
  InputStartLine = Start.first;
  InputStartCol = Start.second;
  InputEndLine = End.first;
  InputEndCol = End.second;
  Start = SM.getLineAndColumn(CheckLoc);
  CheckLine = Start.first;
  CheckCol = Start.second;
}

static bool IsPartOfWord(char c) {
  return (isalnum(c) || c == '-' || c == '_');
}

Check::FileCheckType &Check::FileCheckType::setCount(int C) {
  assert(Count > 0 && "zero and negative counts are not supported");
  assert((C == 1 || Kind == CheckPlain) &&
         "count supported only for plain CHECK directives");
  Count = C;
  return *this;
}

// Get a description of the type.
std::string Check::FileCheckType::getDescription(StringRef Prefix) const {
  switch (Kind) {
  case Check::CheckNone:
    return "invalid";
  case Check::CheckPlain:
    if (Count > 1)
      return Prefix.str() + "-COUNT";
    return Prefix;
  case Check::CheckNext:
    return Prefix.str() + "-NEXT";
  case Check::CheckSame:
    return Prefix.str() + "-SAME";
  case Check::CheckNot:
    return Prefix.str() + "-NOT";
  case Check::CheckDAG:
    return Prefix.str() + "-DAG";
  case Check::CheckLabel:
    return Prefix.str() + "-LABEL";
  case Check::CheckEmpty:
    return Prefix.str() + "-EMPTY";
  case Check::CheckEOF:
    return "implicit EOF";
  case Check::CheckBadNot:
    return "bad NOT";
  case Check::CheckBadCount:
    return "bad COUNT";
  }
  llvm_unreachable("unknown FileCheckType");
}

static std::pair<Check::FileCheckType, StringRef>
FindCheckType(StringRef Buffer, StringRef Prefix) {
  if (Buffer.size() <= Prefix.size())
    return {Check::CheckNone, StringRef()};

  char NextChar = Buffer[Prefix.size()];

  StringRef Rest = Buffer.drop_front(Prefix.size() + 1);
  // Verify that the : is present after the prefix.
  if (NextChar == ':')
    return {Check::CheckPlain, Rest};

  if (NextChar != '-')
    return {Check::CheckNone, StringRef()};

  if (Rest.consume_front("COUNT-")) {
    int64_t Count;
    if (Rest.consumeInteger(10, Count))
      // Error happened in parsing integer.
      return {Check::CheckBadCount, Rest};
    if (Count <= 0 || Count > INT32_MAX)
      return {Check::CheckBadCount, Rest};
    if (!Rest.consume_front(":"))
      return {Check::CheckBadCount, Rest};
    return {Check::FileCheckType(Check::CheckPlain).setCount(Count), Rest};
  }

  if (Rest.consume_front("NEXT:"))
    return {Check::CheckNext, Rest};

  if (Rest.consume_front("SAME:"))
    return {Check::CheckSame, Rest};

  if (Rest.consume_front("NOT:"))
    return {Check::CheckNot, Rest};

  if (Rest.consume_front("DAG:"))
    return {Check::CheckDAG, Rest};

  if (Rest.consume_front("LABEL:"))
    return {Check::CheckLabel, Rest};

  if (Rest.consume_front("EMPTY:"))
    return {Check::CheckEmpty, Rest};

  // You can't combine -NOT with another suffix.
  if (Rest.startswith("DAG-NOT:") || Rest.startswith("NOT-DAG:") ||
      Rest.startswith("NEXT-NOT:") || Rest.startswith("NOT-NEXT:") ||
      Rest.startswith("SAME-NOT:") || Rest.startswith("NOT-SAME:") ||
      Rest.startswith("EMPTY-NOT:") || Rest.startswith("NOT-EMPTY:"))
    return {Check::CheckBadNot, Rest};

  return {Check::CheckNone, Rest};
}

// From the given position, find the next character after the word.
static size_t SkipWord(StringRef Str, size_t Loc) {
  while (Loc < Str.size() && IsPartOfWord(Str[Loc]))
    ++Loc;
  return Loc;
}

/// Search the buffer for the first prefix in the prefix regular expression.
///
/// This searches the buffer using the provided regular expression, however it
/// enforces constraints beyond that:
/// 1) The found prefix must not be a suffix of something that looks like
///    a valid prefix.
/// 2) The found prefix must be followed by a valid check type suffix using \c
///    FindCheckType above.
///
/// Returns a pair of StringRefs into the Buffer, which combines:
///   - the first match of the regular expression to satisfy these two is
///   returned,
///     otherwise an empty StringRef is returned to indicate failure.
///   - buffer rewound to the location right after parsed suffix, for parsing
///     to continue from
///
/// If this routine returns a valid prefix, it will also shrink \p Buffer to
/// start at the beginning of the returned prefix, increment \p LineNumber for
/// each new line consumed from \p Buffer, and set \p CheckTy to the type of
/// check found by examining the suffix.
///
/// If no valid prefix is found, the state of Buffer, LineNumber, and CheckTy
/// is unspecified.
static std::pair<StringRef, StringRef>
FindFirstMatchingPrefix(Regex &PrefixRE, StringRef &Buffer,
                        unsigned &LineNumber, Check::FileCheckType &CheckTy) {
  SmallVector<StringRef, 2> Matches;

  while (!Buffer.empty()) {
    // Find the first (longest) match using the RE.
    if (!PrefixRE.match(Buffer, &Matches))
      // No match at all, bail.
      return {StringRef(), StringRef()};

    StringRef Prefix = Matches[0];
    Matches.clear();

    assert(Prefix.data() >= Buffer.data() &&
           Prefix.data() < Buffer.data() + Buffer.size() &&
           "Prefix doesn't start inside of buffer!");
    size_t Loc = Prefix.data() - Buffer.data();
    StringRef Skipped = Buffer.substr(0, Loc);
    Buffer = Buffer.drop_front(Loc);
    LineNumber += Skipped.count('\n');

    // Check that the matched prefix isn't a suffix of some other check-like
    // word.
    // FIXME: This is a very ad-hoc check. it would be better handled in some
    // other way. Among other things it seems hard to distinguish between
    // intentional and unintentional uses of this feature.
    if (Skipped.empty() || !IsPartOfWord(Skipped.back())) {
      // Now extract the type.
      StringRef AfterSuffix;
      std::tie(CheckTy, AfterSuffix) = FindCheckType(Buffer, Prefix);

      // If we've found a valid check type for this prefix, we're done.
      if (CheckTy != Check::CheckNone)
        return {Prefix, AfterSuffix};
    }

    // If we didn't successfully find a prefix, we need to skip this invalid
    // prefix and continue scanning. We directly skip the prefix that was
    // matched and any additional parts of that check-like word.
    Buffer = Buffer.drop_front(SkipWord(Buffer, Prefix.size()));
  }

  // We ran out of buffer while skipping partial matches so give up.
  return {StringRef(), StringRef()};
}

/// Read the check file, which specifies the sequence of expected strings.
///
/// The strings are added to the CheckStrings vector. Returns true in case of
/// an error, false otherwise.
bool llvm::FileCheck::ReadCheckFile(SourceMgr &SM, StringRef Buffer,
                                    Regex &PrefixRE,
                                    std::vector<FileCheckString> &CheckStrings) {
  std::vector<FileCheckPattern> ImplicitNegativeChecks;
  for (const auto &PatternString : Req.ImplicitCheckNot) {
    // Create a buffer with fake command line content in order to display the
    // command line option responsible for the specific implicit CHECK-NOT.
    std::string Prefix = "-implicit-check-not='";
    std::string Suffix = "'";
    std::unique_ptr<MemoryBuffer> CmdLine = MemoryBuffer::getMemBufferCopy(
        Prefix + PatternString + Suffix, "command line");

    StringRef PatternInBuffer =
        CmdLine->getBuffer().substr(Prefix.size(), PatternString.size());
    SM.AddNewSourceBuffer(std::move(CmdLine), SMLoc());

    ImplicitNegativeChecks.push_back(FileCheckPattern(Check::CheckNot));
    ImplicitNegativeChecks.back().ParsePattern(PatternInBuffer,
                                               "IMPLICIT-CHECK", SM, 0, Req);
  }

  std::vector<FileCheckPattern> DagNotMatches = ImplicitNegativeChecks;

  // LineNumber keeps track of the line on which CheckPrefix instances are
  // found.
  unsigned LineNumber = 1;

  while (1) {
    Check::FileCheckType CheckTy;

    // See if a prefix occurs in the memory buffer.
    StringRef UsedPrefix;
    StringRef AfterSuffix;
    std::tie(UsedPrefix, AfterSuffix) =
        FindFirstMatchingPrefix(PrefixRE, Buffer, LineNumber, CheckTy);
    if (UsedPrefix.empty())
      break;
    assert(UsedPrefix.data() == Buffer.data() &&
           "Failed to move Buffer's start forward, or pointed prefix outside "
           "of the buffer!");
    assert(AfterSuffix.data() >= Buffer.data() &&
           AfterSuffix.data() < Buffer.data() + Buffer.size() &&
           "Parsing after suffix doesn't start inside of buffer!");

    // Location to use for error messages.
    const char *UsedPrefixStart = UsedPrefix.data();

    // Skip the buffer to the end of parsed suffix (or just prefix, if no good
    // suffix was processed).
    Buffer = AfterSuffix.empty() ? Buffer.drop_front(UsedPrefix.size())
                                 : AfterSuffix;

    // Complain about useful-looking but unsupported suffixes.
    if (CheckTy == Check::CheckBadNot) {
      SM.PrintMessage(SMLoc::getFromPointer(Buffer.data()), SourceMgr::DK_Error,
                      "unsupported -NOT combo on prefix '" + UsedPrefix + "'");
      return true;
    }

    // Complain about invalid count specification.
    if (CheckTy == Check::CheckBadCount) {
      SM.PrintMessage(SMLoc::getFromPointer(Buffer.data()), SourceMgr::DK_Error,
                      "invalid count in -COUNT specification on prefix '" +
                          UsedPrefix + "'");
      return true;
    }

    // Okay, we found the prefix, yay. Remember the rest of the line, but ignore
    // leading whitespace.
    if (!(Req.NoCanonicalizeWhiteSpace && Req.MatchFullLines))
      Buffer = Buffer.substr(Buffer.find_first_not_of(" \t"));

    // Scan ahead to the end of line.
    size_t EOL = Buffer.find_first_of("\n\r");

    // Remember the location of the start of the pattern, for diagnostics.
    SMLoc PatternLoc = SMLoc::getFromPointer(Buffer.data());

    // Parse the pattern.
    FileCheckPattern P(CheckTy);
    if (P.ParsePattern(Buffer.substr(0, EOL), UsedPrefix, SM, LineNumber, Req))
      return true;

    // Verify that CHECK-LABEL lines do not define or use variables
    if ((CheckTy == Check::CheckLabel) && P.hasVariable()) {
      SM.PrintMessage(
          SMLoc::getFromPointer(UsedPrefixStart), SourceMgr::DK_Error,
          "found '" + UsedPrefix + "-LABEL:'"
                                   " with variable definition or use");
      return true;
    }

    Buffer = Buffer.substr(EOL);

    // Verify that CHECK-NEXT/SAME/EMPTY lines have at least one CHECK line before them.
    if ((CheckTy == Check::CheckNext || CheckTy == Check::CheckSame ||
         CheckTy == Check::CheckEmpty) &&
        CheckStrings.empty()) {
      StringRef Type = CheckTy == Check::CheckNext
                           ? "NEXT"
                           : CheckTy == Check::CheckEmpty ? "EMPTY" : "SAME";
      SM.PrintMessage(SMLoc::getFromPointer(UsedPrefixStart),
                      SourceMgr::DK_Error,
                      "found '" + UsedPrefix + "-" + Type +
                          "' without previous '" + UsedPrefix + ": line");
      return true;
    }

    // Handle CHECK-DAG/-NOT.
    if (CheckTy == Check::CheckDAG || CheckTy == Check::CheckNot) {
      DagNotMatches.push_back(P);
      continue;
    }

    // Okay, add the string we captured to the output vector and move on.
    CheckStrings.emplace_back(P, UsedPrefix, PatternLoc);
    std::swap(DagNotMatches, CheckStrings.back().DagNotStrings);
    DagNotMatches = ImplicitNegativeChecks;
  }

  // Add an EOF pattern for any trailing CHECK-DAG/-NOTs, and use the first
  // prefix as a filler for the error message.
  if (!DagNotMatches.empty()) {
    CheckStrings.emplace_back(FileCheckPattern(Check::CheckEOF), *Req.CheckPrefixes.begin(),
                              SMLoc::getFromPointer(Buffer.data()));
    std::swap(DagNotMatches, CheckStrings.back().DagNotStrings);
  }

  if (CheckStrings.empty()) {
    errs() << "error: no check strings found with prefix"
           << (Req.CheckPrefixes.size() > 1 ? "es " : " ");
    auto I = Req.CheckPrefixes.begin();
    auto E = Req.CheckPrefixes.end();
    if (I != E) {
      errs() << "\'" << *I << ":'";
      ++I;
    }
    for (; I != E; ++I)
      errs() << ", \'" << *I << ":'";

    errs() << '\n';
    return true;
  }

  return false;
}

static void PrintMatch(bool ExpectedMatch, const SourceMgr &SM,
                       StringRef Prefix, SMLoc Loc, const FileCheckPattern &Pat,
                       int MatchedCount, StringRef Buffer,
                       StringMap<StringRef> &VariableTable, size_t MatchPos,
                       size_t MatchLen, const FileCheckRequest &Req,
                       std::vector<FileCheckDiag> *Diags) {
  if (ExpectedMatch) {
    if (!Req.Verbose)
      return;
    if (!Req.VerboseVerbose && Pat.getCheckTy() == Check::CheckEOF)
      return;
  }
  SMRange MatchRange = ProcessMatchResult(
      ExpectedMatch ? FileCheckDiag::MatchFoundAndExpected
                    : FileCheckDiag::MatchFoundButExcluded,
      SM, Loc, Pat.getCheckTy(), Buffer, MatchPos, MatchLen, Diags);
  std::string Message = formatv("{0}: {1} string found in input",
                                Pat.getCheckTy().getDescription(Prefix),
                                (ExpectedMatch ? "expected" : "excluded"))
                            .str();
  if (Pat.getCount() > 1)
    Message += formatv(" ({0} out of {1})", MatchedCount, Pat.getCount()).str();

  SM.PrintMessage(
      Loc, ExpectedMatch ? SourceMgr::DK_Remark : SourceMgr::DK_Error, Message);
  SM.PrintMessage(MatchRange.Start, SourceMgr::DK_Note, "found here",
                  {MatchRange});
  Pat.PrintVariableUses(SM, Buffer, VariableTable, MatchRange);
}

static void PrintMatch(bool ExpectedMatch, const SourceMgr &SM,
                       const FileCheckString &CheckStr, int MatchedCount,
                       StringRef Buffer, StringMap<StringRef> &VariableTable,
                       size_t MatchPos, size_t MatchLen, FileCheckRequest &Req,
                       std::vector<FileCheckDiag> *Diags) {
  PrintMatch(ExpectedMatch, SM, CheckStr.Prefix, CheckStr.Loc, CheckStr.Pat,
             MatchedCount, Buffer, VariableTable, MatchPos, MatchLen, Req,
             Diags);
}

static void PrintNoMatch(bool ExpectedMatch, const SourceMgr &SM,
                         StringRef Prefix, SMLoc Loc,
                         const FileCheckPattern &Pat, int MatchedCount,
                         StringRef Buffer, StringMap<StringRef> &VariableTable,
                         bool VerboseVerbose,
                         std::vector<FileCheckDiag> *Diags) {
  if (!ExpectedMatch && !VerboseVerbose)
    return;

  // Otherwise, we have an error, emit an error message.
  std::string Message = formatv("{0}: {1} string not found in input",
                                Pat.getCheckTy().getDescription(Prefix),
                                (ExpectedMatch ? "expected" : "excluded"))
                            .str();
  if (Pat.getCount() > 1)
    Message += formatv(" ({0} out of {1})", MatchedCount, Pat.getCount()).str();

  SM.PrintMessage(
      Loc, ExpectedMatch ? SourceMgr::DK_Error : SourceMgr::DK_Remark, Message);

  // Print the "scanning from here" line.  If the current position is at the
  // end of a line, advance to the start of the next line.
  Buffer = Buffer.substr(Buffer.find_first_not_of(" \t\n\r"));
  SMRange SearchRange = ProcessMatchResult(
      ExpectedMatch ? FileCheckDiag::MatchNoneButExpected
                    : FileCheckDiag::MatchNoneAndExcluded,
      SM, Loc, Pat.getCheckTy(), Buffer, 0, Buffer.size(), Diags);
  SM.PrintMessage(SearchRange.Start, SourceMgr::DK_Note, "scanning from here");

  // Allow the pattern to print additional information if desired.
  Pat.PrintVariableUses(SM, Buffer, VariableTable);

  if (ExpectedMatch)
    Pat.PrintFuzzyMatch(SM, Buffer, VariableTable, Diags);
}

static void PrintNoMatch(bool ExpectedMatch, const SourceMgr &SM,
                         const FileCheckString &CheckStr, int MatchedCount,
                         StringRef Buffer, StringMap<StringRef> &VariableTable,
                         bool VerboseVerbose,
                         std::vector<FileCheckDiag> *Diags) {
  PrintNoMatch(ExpectedMatch, SM, CheckStr.Prefix, CheckStr.Loc, CheckStr.Pat,
               MatchedCount, Buffer, VariableTable, VerboseVerbose, Diags);
}

/// Count the number of newlines in the specified range.
static unsigned CountNumNewlinesBetween(StringRef Range,
                                        const char *&FirstNewLine) {
  unsigned NumNewLines = 0;
  while (1) {
    // Scan for newline.
    Range = Range.substr(Range.find_first_of("\n\r"));
    if (Range.empty())
      return NumNewLines;

    ++NumNewLines;

    // Handle \n\r and \r\n as a single newline.
    if (Range.size() > 1 && (Range[1] == '\n' || Range[1] == '\r') &&
        (Range[0] != Range[1]))
      Range = Range.substr(1);
    Range = Range.substr(1);

    if (NumNewLines == 1)
      FirstNewLine = Range.begin();
  }
}

/// Match check string and its "not strings" and/or "dag strings".
size_t FileCheckString::Check(const SourceMgr &SM, StringRef Buffer,
                              bool IsLabelScanMode, size_t &MatchLen,
                              StringMap<StringRef> &VariableTable,
                              FileCheckRequest &Req,
                              std::vector<FileCheckDiag> *Diags) const {
  size_t LastPos = 0;
  std::vector<const FileCheckPattern *> NotStrings;

  // IsLabelScanMode is true when we are scanning forward to find CHECK-LABEL
  // bounds; we have not processed variable definitions within the bounded block
  // yet so cannot handle any final CHECK-DAG yet; this is handled when going
  // over the block again (including the last CHECK-LABEL) in normal mode.
  if (!IsLabelScanMode) {
    // Match "dag strings" (with mixed "not strings" if any).
    LastPos = CheckDag(SM, Buffer, NotStrings, VariableTable, Req, Diags);
    if (LastPos == StringRef::npos)
      return StringRef::npos;
  }

  // Match itself from the last position after matching CHECK-DAG.
  size_t LastMatchEnd = LastPos;
  size_t FirstMatchPos = 0;
  // Go match the pattern Count times. Majority of patterns only match with
  // count 1 though.
  assert(Pat.getCount() != 0 && "pattern count can not be zero");
  for (int i = 1; i <= Pat.getCount(); i++) {
    StringRef MatchBuffer = Buffer.substr(LastMatchEnd);
    size_t CurrentMatchLen;
    // get a match at current start point
    size_t MatchPos = Pat.Match(MatchBuffer, CurrentMatchLen, VariableTable);
    if (i == 1)
      FirstMatchPos = LastPos + MatchPos;

    // report
    if (MatchPos == StringRef::npos) {
      PrintNoMatch(true, SM, *this, i, MatchBuffer, VariableTable,
                   Req.VerboseVerbose, Diags);
      return StringRef::npos;
    }
    PrintMatch(true, SM, *this, i, MatchBuffer, VariableTable, MatchPos,
               CurrentMatchLen, Req, Diags);

    // move start point after the match
    LastMatchEnd += MatchPos + CurrentMatchLen;
  }
  // Full match len counts from first match pos.
  MatchLen = LastMatchEnd - FirstMatchPos;

  // Similar to the above, in "label-scan mode" we can't yet handle CHECK-NEXT
  // or CHECK-NOT
  if (!IsLabelScanMode) {
    size_t MatchPos = FirstMatchPos - LastPos;
    StringRef MatchBuffer = Buffer.substr(LastPos);
    StringRef SkippedRegion = Buffer.substr(LastPos, MatchPos);

    // If this check is a "CHECK-NEXT", verify that the previous match was on
    // the previous line (i.e. that there is one newline between them).
    if (CheckNext(SM, SkippedRegion)) {
      ProcessMatchResult(FileCheckDiag::MatchFoundButWrongLine, SM, Loc,
                         Pat.getCheckTy(), MatchBuffer, MatchPos, MatchLen,
                         Diags, Req.Verbose);
      return StringRef::npos;
    }

    // If this check is a "CHECK-SAME", verify that the previous match was on
    // the same line (i.e. that there is no newline between them).
    if (CheckSame(SM, SkippedRegion)) {
      ProcessMatchResult(FileCheckDiag::MatchFoundButWrongLine, SM, Loc,
                         Pat.getCheckTy(), MatchBuffer, MatchPos, MatchLen,
                         Diags, Req.Verbose);
      return StringRef::npos;
    }

    // If this match had "not strings", verify that they don't exist in the
    // skipped region.
    if (CheckNot(SM, SkippedRegion, NotStrings, VariableTable, Req, Diags))
      return StringRef::npos;
  }

  return FirstMatchPos;
}

/// Verify there is a single line in the given buffer.
bool FileCheckString::CheckNext(const SourceMgr &SM, StringRef Buffer) const {
  if (Pat.getCheckTy() != Check::CheckNext &&
      Pat.getCheckTy() != Check::CheckEmpty)
    return false;

  Twine CheckName =
      Prefix +
      Twine(Pat.getCheckTy() == Check::CheckEmpty ? "-EMPTY" : "-NEXT");

  // Count the number of newlines between the previous match and this one.
  assert(Buffer.data() !=
             SM.getMemoryBuffer(SM.FindBufferContainingLoc(
                                    SMLoc::getFromPointer(Buffer.data())))
                 ->getBufferStart() &&
         "CHECK-NEXT and CHECK-EMPTY can't be the first check in a file");

  const char *FirstNewLine = nullptr;
  unsigned NumNewLines = CountNumNewlinesBetween(Buffer, FirstNewLine);

  if (NumNewLines == 0) {
    SM.PrintMessage(Loc, SourceMgr::DK_Error,
                    CheckName + ": is on the same line as previous match");
    SM.PrintMessage(SMLoc::getFromPointer(Buffer.end()), SourceMgr::DK_Note,
                    "'next' match was here");
    SM.PrintMessage(SMLoc::getFromPointer(Buffer.data()), SourceMgr::DK_Note,
                    "previous match ended here");
    return true;
  }

  if (NumNewLines != 1) {
    SM.PrintMessage(Loc, SourceMgr::DK_Error,
                    CheckName +
                        ": is not on the line after the previous match");
    SM.PrintMessage(SMLoc::getFromPointer(Buffer.end()), SourceMgr::DK_Note,
                    "'next' match was here");
    SM.PrintMessage(SMLoc::getFromPointer(Buffer.data()), SourceMgr::DK_Note,
                    "previous match ended here");
    SM.PrintMessage(SMLoc::getFromPointer(FirstNewLine), SourceMgr::DK_Note,
                    "non-matching line after previous match is here");
    return true;
  }

  return false;
}

/// Verify there is no newline in the given buffer.
bool FileCheckString::CheckSame(const SourceMgr &SM, StringRef Buffer) const {
  if (Pat.getCheckTy() != Check::CheckSame)
    return false;

  // Count the number of newlines between the previous match and this one.
  assert(Buffer.data() !=
             SM.getMemoryBuffer(SM.FindBufferContainingLoc(
                                    SMLoc::getFromPointer(Buffer.data())))
                 ->getBufferStart() &&
         "CHECK-SAME can't be the first check in a file");

  const char *FirstNewLine = nullptr;
  unsigned NumNewLines = CountNumNewlinesBetween(Buffer, FirstNewLine);

  if (NumNewLines != 0) {
    SM.PrintMessage(Loc, SourceMgr::DK_Error,
                    Prefix +
                        "-SAME: is not on the same line as the previous match");
    SM.PrintMessage(SMLoc::getFromPointer(Buffer.end()), SourceMgr::DK_Note,
                    "'next' match was here");
    SM.PrintMessage(SMLoc::getFromPointer(Buffer.data()), SourceMgr::DK_Note,
                    "previous match ended here");
    return true;
  }

  return false;
}

/// Verify there's no "not strings" in the given buffer.
bool FileCheckString::CheckNot(
    const SourceMgr &SM, StringRef Buffer,
    const std::vector<const FileCheckPattern *> &NotStrings,
    StringMap<StringRef> &VariableTable, const FileCheckRequest &Req,
    std::vector<FileCheckDiag> *Diags) const {
  for (const FileCheckPattern *Pat : NotStrings) {
    assert((Pat->getCheckTy() == Check::CheckNot) && "Expect CHECK-NOT!");

    size_t MatchLen = 0;
    size_t Pos = Pat->Match(Buffer, MatchLen, VariableTable);

    if (Pos == StringRef::npos) {
      PrintNoMatch(false, SM, Prefix, Pat->getLoc(), *Pat, 1, Buffer,
                   VariableTable, Req.VerboseVerbose, Diags);
      continue;
    }

    PrintMatch(false, SM, Prefix, Pat->getLoc(), *Pat, 1, Buffer, VariableTable,
               Pos, MatchLen, Req, Diags);

    return true;
  }

  return false;
}

/// Match "dag strings" and their mixed "not strings".
size_t
FileCheckString::CheckDag(const SourceMgr &SM, StringRef Buffer,
                          std::vector<const FileCheckPattern *> &NotStrings,
                          StringMap<StringRef> &VariableTable,
                          const FileCheckRequest &Req,
                          std::vector<FileCheckDiag> *Diags) const {
  if (DagNotStrings.empty())
    return 0;

  // The start of the search range.
  size_t StartPos = 0;

  struct MatchRange {
    size_t Pos;
    size_t End;
  };
  // A sorted list of ranges for non-overlapping CHECK-DAG matches.  Match
  // ranges are erased from this list once they are no longer in the search
  // range.
  std::list<MatchRange> MatchRanges;

  // We need PatItr and PatEnd later for detecting the end of a CHECK-DAG
  // group, so we don't use a range-based for loop here.
  for (auto PatItr = DagNotStrings.begin(), PatEnd = DagNotStrings.end();
       PatItr != PatEnd; ++PatItr) {
    const FileCheckPattern &Pat = *PatItr;
    assert((Pat.getCheckTy() == Check::CheckDAG ||
            Pat.getCheckTy() == Check::CheckNot) &&
           "Invalid CHECK-DAG or CHECK-NOT!");

    if (Pat.getCheckTy() == Check::CheckNot) {
      NotStrings.push_back(&Pat);
      continue;
    }

    assert((Pat.getCheckTy() == Check::CheckDAG) && "Expect CHECK-DAG!");

    // CHECK-DAG always matches from the start.
    size_t MatchLen = 0, MatchPos = StartPos;

    // Search for a match that doesn't overlap a previous match in this
    // CHECK-DAG group.
    for (auto MI = MatchRanges.begin(), ME = MatchRanges.end(); true; ++MI) {
      StringRef MatchBuffer = Buffer.substr(MatchPos);
      size_t MatchPosBuf = Pat.Match(MatchBuffer, MatchLen, VariableTable);
      // With a group of CHECK-DAGs, a single mismatching means the match on
      // that group of CHECK-DAGs fails immediately.
      if (MatchPosBuf == StringRef::npos) {
        PrintNoMatch(true, SM, Prefix, Pat.getLoc(), Pat, 1, MatchBuffer,
                     VariableTable, Req.VerboseVerbose, Diags);
        return StringRef::npos;
      }
      // Re-calc it as the offset relative to the start of the original string.
      MatchPos += MatchPosBuf;
      if (Req.VerboseVerbose)
        PrintMatch(true, SM, Prefix, Pat.getLoc(), Pat, 1, Buffer,
                   VariableTable, MatchPos, MatchLen, Req, Diags);
      MatchRange M{MatchPos, MatchPos + MatchLen};
      if (Req.AllowDeprecatedDagOverlap) {
        // We don't need to track all matches in this mode, so we just maintain
        // one match range that encompasses the current CHECK-DAG group's
        // matches.
        if (MatchRanges.empty())
          MatchRanges.insert(MatchRanges.end(), M);
        else {
          auto Block = MatchRanges.begin();
          Block->Pos = std::min(Block->Pos, M.Pos);
          Block->End = std::max(Block->End, M.End);
        }
        break;
      }
      // Iterate previous matches until overlapping match or insertion point.
      bool Overlap = false;
      for (; MI != ME; ++MI) {
        if (M.Pos < MI->End) {
          // !Overlap => New match has no overlap and is before this old match.
          // Overlap => New match overlaps this old match.
          Overlap = MI->Pos < M.End;
          break;
        }
      }
      if (!Overlap) {
        // Insert non-overlapping match into list.
        MatchRanges.insert(MI, M);
        break;
      }
      if (Req.VerboseVerbose) {
        SMLoc OldStart = SMLoc::getFromPointer(Buffer.data() + MI->Pos);
        SMLoc OldEnd = SMLoc::getFromPointer(Buffer.data() + MI->End);
        SMRange OldRange(OldStart, OldEnd);
        SM.PrintMessage(OldStart, SourceMgr::DK_Note,
                        "match discarded, overlaps earlier DAG match here",
                        {OldRange});
        if (Diags)
          Diags->rbegin()->MatchTy = FileCheckDiag::MatchFoundButDiscarded;
      }
      MatchPos = MI->End;
    }
    if (!Req.VerboseVerbose)
      PrintMatch(true, SM, Prefix, Pat.getLoc(), Pat, 1, Buffer, VariableTable,
                 MatchPos, MatchLen, Req, Diags);

    // Handle the end of a CHECK-DAG group.
    if (std::next(PatItr) == PatEnd ||
        std::next(PatItr)->getCheckTy() == Check::CheckNot) {
      if (!NotStrings.empty()) {
        // If there are CHECK-NOTs between two CHECK-DAGs or from CHECK to
        // CHECK-DAG, verify that there are no 'not' strings occurred in that
        // region.
        StringRef SkippedRegion =
            Buffer.slice(StartPos, MatchRanges.begin()->Pos);
        if (CheckNot(SM, SkippedRegion, NotStrings, VariableTable, Req, Diags))
          return StringRef::npos;
        // Clear "not strings".
        NotStrings.clear();
      }
      // All subsequent CHECK-DAGs and CHECK-NOTs should be matched from the
      // end of this CHECK-DAG group's match range.
      StartPos = MatchRanges.rbegin()->End;
      // Don't waste time checking for (impossible) overlaps before that.
      MatchRanges.clear();
    }
  }

  return StartPos;
}

// A check prefix must contain only alphanumeric, hyphens and underscores.
static bool ValidateCheckPrefix(StringRef CheckPrefix) {
  Regex Validator("^[a-zA-Z0-9_-]*$");
  return Validator.match(CheckPrefix);
}

bool llvm::FileCheck::ValidateCheckPrefixes() {
  StringSet<> PrefixSet;

  for (StringRef Prefix : Req.CheckPrefixes) {
    // Reject empty prefixes.
    if (Prefix == "")
      return false;

    if (!PrefixSet.insert(Prefix).second)
      return false;

    if (!ValidateCheckPrefix(Prefix))
      return false;
  }

  return true;
}

// Combines the check prefixes into a single regex so that we can efficiently
// scan for any of the set.
//
// The semantics are that the longest-match wins which matches our regex
// library.
Regex llvm::FileCheck::buildCheckPrefixRegex() {
  // I don't think there's a way to specify an initial value for cl::list,
  // so if nothing was specified, add the default
  if (Req.CheckPrefixes.empty())
    Req.CheckPrefixes.push_back("CHECK");

  // We already validated the contents of CheckPrefixes so just concatenate
  // them as alternatives.
  SmallString<32> PrefixRegexStr;
  for (StringRef Prefix : Req.CheckPrefixes) {
    if (Prefix != Req.CheckPrefixes.front())
      PrefixRegexStr.push_back('|');

    PrefixRegexStr.append(Prefix);
  }

  return Regex(PrefixRegexStr);
}

// Remove local variables from \p VariableTable. Global variables
// (start with '$') are preserved.
static void ClearLocalVars(StringMap<StringRef> &VariableTable) {
  SmallVector<StringRef, 16> LocalVars;
  for (const auto &Var : VariableTable)
    if (Var.first()[0] != '$')
      LocalVars.push_back(Var.first());

  for (const auto &Var : LocalVars)
    VariableTable.erase(Var);
}

/// Check the input to FileCheck provided in the \p Buffer against the \p
/// CheckStrings read from the check file.
///
/// Returns false if the input fails to satisfy the checks.
bool llvm::FileCheck::CheckInput(SourceMgr &SM, StringRef Buffer,
                                 ArrayRef<FileCheckString> CheckStrings,
                                 std::vector<FileCheckDiag> *Diags) {
  bool ChecksFailed = false;

  /// VariableTable - This holds all the current filecheck variables.
  StringMap<StringRef> VariableTable;

  for (const auto& Def : Req.GlobalDefines)
    VariableTable.insert(StringRef(Def).split('='));

  unsigned i = 0, j = 0, e = CheckStrings.size();
  while (true) {
    StringRef CheckRegion;
    if (j == e) {
      CheckRegion = Buffer;
    } else {
      const FileCheckString &CheckLabelStr = CheckStrings[j];
      if (CheckLabelStr.Pat.getCheckTy() != Check::CheckLabel) {
        ++j;
        continue;
      }

      // Scan to next CHECK-LABEL match, ignoring CHECK-NOT and CHECK-DAG
      size_t MatchLabelLen = 0;
      size_t MatchLabelPos = CheckLabelStr.Check(
          SM, Buffer, true, MatchLabelLen, VariableTable, Req, Diags);
      if (MatchLabelPos == StringRef::npos)
        // Immediately bail of CHECK-LABEL fails, nothing else we can do.
        return false;

      CheckRegion = Buffer.substr(0, MatchLabelPos + MatchLabelLen);
      Buffer = Buffer.substr(MatchLabelPos + MatchLabelLen);
      ++j;
    }

    if (Req.EnableVarScope)
      ClearLocalVars(VariableTable);

    for (; i != j; ++i) {
      const FileCheckString &CheckStr = CheckStrings[i];

      // Check each string within the scanned region, including a second check
      // of any final CHECK-LABEL (to verify CHECK-NOT and CHECK-DAG)
      size_t MatchLen = 0;
      size_t MatchPos = CheckStr.Check(SM, CheckRegion, false, MatchLen,
                                       VariableTable, Req, Diags);

      if (MatchPos == StringRef::npos) {
        ChecksFailed = true;
        i = j;
        break;
      }

      CheckRegion = CheckRegion.substr(MatchPos + MatchLen);
    }

    if (j == e)
      break;
  }

  // Success if no checks failed.
  return !ChecksFailed;
}
