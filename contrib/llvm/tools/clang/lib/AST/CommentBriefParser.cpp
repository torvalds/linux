//===--- CommentBriefParser.cpp - Dumb comment parser ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/CommentBriefParser.h"
#include "clang/AST/CommentCommandTraits.h"

namespace clang {
namespace comments {

namespace {
inline bool isWhitespace(char C) {
  return C == ' ' || C == '\n' || C == '\r' ||
         C == '\t' || C == '\f' || C == '\v';
}

/// Convert all whitespace into spaces, remove leading and trailing spaces,
/// compress multiple spaces into one.
void cleanupBrief(std::string &S) {
  bool PrevWasSpace = true;
  std::string::iterator O = S.begin();
  for (std::string::iterator I = S.begin(), E = S.end();
       I != E; ++I) {
    const char C = *I;
    if (isWhitespace(C)) {
      if (!PrevWasSpace) {
        *O++ = ' ';
        PrevWasSpace = true;
      }
      continue;
    } else {
      *O++ = C;
      PrevWasSpace = false;
    }
  }
  if (O != S.begin() && *(O - 1) == ' ')
    --O;

  S.resize(O - S.begin());
}

bool isWhitespace(StringRef Text) {
  for (StringRef::const_iterator I = Text.begin(), E = Text.end();
       I != E; ++I) {
    if (!isWhitespace(*I))
      return false;
  }
  return true;
}
} // unnamed namespace

BriefParser::BriefParser(Lexer &L, const CommandTraits &Traits) :
    L(L), Traits(Traits) {
  // Get lookahead token.
  ConsumeToken();
}

std::string BriefParser::Parse() {
  std::string FirstParagraphOrBrief;
  std::string ReturnsParagraph;
  bool InFirstParagraph = true;
  bool InBrief = false;
  bool InReturns = false;

  while (Tok.isNot(tok::eof)) {
    if (Tok.is(tok::text)) {
      if (InFirstParagraph || InBrief)
        FirstParagraphOrBrief += Tok.getText();
      else if (InReturns)
        ReturnsParagraph += Tok.getText();
      ConsumeToken();
      continue;
    }

    if (Tok.is(tok::backslash_command) || Tok.is(tok::at_command)) {
      const CommandInfo *Info = Traits.getCommandInfo(Tok.getCommandID());
      if (Info->IsBriefCommand) {
        FirstParagraphOrBrief.clear();
        InBrief = true;
        ConsumeToken();
        continue;
      }
      if (Info->IsReturnsCommand) {
        InReturns = true;
        InBrief = false;
        InFirstParagraph = false;
        ReturnsParagraph += "Returns ";
        ConsumeToken();
        continue;
      }
      // Block commands implicitly start a new paragraph.
      if (Info->IsBlockCommand) {
        // We found an implicit paragraph end.
        InFirstParagraph = false;
        if (InBrief)
          break;
      }
    }

    if (Tok.is(tok::newline)) {
      if (InFirstParagraph || InBrief)
        FirstParagraphOrBrief += ' ';
      else if (InReturns)
        ReturnsParagraph += ' ';
      ConsumeToken();

      // If the next token is a whitespace only text, ignore it.  Thus we allow
      // two paragraphs to be separated by line that has only whitespace in it.
      //
      // We don't need to add a space to the parsed text because we just added
      // a space for the newline.
      if (Tok.is(tok::text)) {
        if (isWhitespace(Tok.getText()))
          ConsumeToken();
      }

      if (Tok.is(tok::newline)) {
        ConsumeToken();
        // We found a paragraph end.  This ends the brief description if
        // \command or its equivalent was explicitly used.
        // Stop scanning text because an explicit \paragraph is the
        // preffered one.
        if (InBrief)
          break;
        // End first paragraph if we found some non-whitespace text.
        if (InFirstParagraph && !isWhitespace(FirstParagraphOrBrief))
          InFirstParagraph = false;
        // End the \\returns paragraph because we found the paragraph end.
        InReturns = false;
      }
      continue;
    }

    // We didn't handle this token, so just drop it.
    ConsumeToken();
  }

  cleanupBrief(FirstParagraphOrBrief);
  if (!FirstParagraphOrBrief.empty())
    return FirstParagraphOrBrief;

  cleanupBrief(ReturnsParagraph);
  return ReturnsParagraph;
}

} // end namespace comments
} // end namespace clang


