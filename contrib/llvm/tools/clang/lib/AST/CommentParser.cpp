//===--- CommentParser.cpp - Doxygen comment parser -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/CommentParser.h"
#include "clang/AST/CommentCommandTraits.h"
#include "clang/AST/CommentDiagnostic.h"
#include "clang/AST/CommentSema.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/Support/ErrorHandling.h"

namespace clang {

static inline bool isWhitespace(llvm::StringRef S) {
  for (StringRef::const_iterator I = S.begin(), E = S.end(); I != E; ++I) {
    if (!isWhitespace(*I))
      return false;
  }
  return true;
}

namespace comments {

/// Re-lexes a sequence of tok::text tokens.
class TextTokenRetokenizer {
  llvm::BumpPtrAllocator &Allocator;
  Parser &P;

  /// This flag is set when there are no more tokens we can fetch from lexer.
  bool NoMoreInterestingTokens;

  /// Token buffer: tokens we have processed and lookahead.
  SmallVector<Token, 16> Toks;

  /// A position in \c Toks.
  struct Position {
    const char *BufferStart;
    const char *BufferEnd;
    const char *BufferPtr;
    SourceLocation BufferStartLoc;
    unsigned CurToken;
  };

  /// Current position in Toks.
  Position Pos;

  bool isEnd() const {
    return Pos.CurToken >= Toks.size();
  }

  /// Sets up the buffer pointers to point to current token.
  void setupBuffer() {
    assert(!isEnd());
    const Token &Tok = Toks[Pos.CurToken];

    Pos.BufferStart = Tok.getText().begin();
    Pos.BufferEnd = Tok.getText().end();
    Pos.BufferPtr = Pos.BufferStart;
    Pos.BufferStartLoc = Tok.getLocation();
  }

  SourceLocation getSourceLocation() const {
    const unsigned CharNo = Pos.BufferPtr - Pos.BufferStart;
    return Pos.BufferStartLoc.getLocWithOffset(CharNo);
  }

  char peek() const {
    assert(!isEnd());
    assert(Pos.BufferPtr != Pos.BufferEnd);
    return *Pos.BufferPtr;
  }

  void consumeChar() {
    assert(!isEnd());
    assert(Pos.BufferPtr != Pos.BufferEnd);
    Pos.BufferPtr++;
    if (Pos.BufferPtr == Pos.BufferEnd) {
      Pos.CurToken++;
      if (isEnd() && !addToken())
        return;

      assert(!isEnd());
      setupBuffer();
    }
  }

  /// Add a token.
  /// Returns true on success, false if there are no interesting tokens to
  /// fetch from lexer.
  bool addToken() {
    if (NoMoreInterestingTokens)
      return false;

    if (P.Tok.is(tok::newline)) {
      // If we see a single newline token between text tokens, skip it.
      Token Newline = P.Tok;
      P.consumeToken();
      if (P.Tok.isNot(tok::text)) {
        P.putBack(Newline);
        NoMoreInterestingTokens = true;
        return false;
      }
    }
    if (P.Tok.isNot(tok::text)) {
      NoMoreInterestingTokens = true;
      return false;
    }

    Toks.push_back(P.Tok);
    P.consumeToken();
    if (Toks.size() == 1)
      setupBuffer();
    return true;
  }

  void consumeWhitespace() {
    while (!isEnd()) {
      if (isWhitespace(peek()))
        consumeChar();
      else
        break;
    }
  }

  void formTokenWithChars(Token &Result,
                          SourceLocation Loc,
                          const char *TokBegin,
                          unsigned TokLength,
                          StringRef Text) {
    Result.setLocation(Loc);
    Result.setKind(tok::text);
    Result.setLength(TokLength);
#ifndef NDEBUG
    Result.TextPtr = "<UNSET>";
    Result.IntVal = 7;
#endif
    Result.setText(Text);
  }

public:
  TextTokenRetokenizer(llvm::BumpPtrAllocator &Allocator, Parser &P):
      Allocator(Allocator), P(P), NoMoreInterestingTokens(false) {
    Pos.CurToken = 0;
    addToken();
  }

  /// Extract a word -- sequence of non-whitespace characters.
  bool lexWord(Token &Tok) {
    if (isEnd())
      return false;

    Position SavedPos = Pos;

    consumeWhitespace();
    SmallString<32> WordText;
    const char *WordBegin = Pos.BufferPtr;
    SourceLocation Loc = getSourceLocation();
    while (!isEnd()) {
      const char C = peek();
      if (!isWhitespace(C)) {
        WordText.push_back(C);
        consumeChar();
      } else
        break;
    }
    const unsigned Length = WordText.size();
    if (Length == 0) {
      Pos = SavedPos;
      return false;
    }

    char *TextPtr = Allocator.Allocate<char>(Length + 1);

    memcpy(TextPtr, WordText.c_str(), Length + 1);
    StringRef Text = StringRef(TextPtr, Length);

    formTokenWithChars(Tok, Loc, WordBegin, Length, Text);
    return true;
  }

  bool lexDelimitedSeq(Token &Tok, char OpenDelim, char CloseDelim) {
    if (isEnd())
      return false;

    Position SavedPos = Pos;

    consumeWhitespace();
    SmallString<32> WordText;
    const char *WordBegin = Pos.BufferPtr;
    SourceLocation Loc = getSourceLocation();
    bool Error = false;
    if (!isEnd()) {
      const char C = peek();
      if (C == OpenDelim) {
        WordText.push_back(C);
        consumeChar();
      } else
        Error = true;
    }
    char C = '\0';
    while (!Error && !isEnd()) {
      C = peek();
      WordText.push_back(C);
      consumeChar();
      if (C == CloseDelim)
        break;
    }
    if (!Error && C != CloseDelim)
      Error = true;

    if (Error) {
      Pos = SavedPos;
      return false;
    }

    const unsigned Length = WordText.size();
    char *TextPtr = Allocator.Allocate<char>(Length + 1);

    memcpy(TextPtr, WordText.c_str(), Length + 1);
    StringRef Text = StringRef(TextPtr, Length);

    formTokenWithChars(Tok, Loc, WordBegin,
                       Pos.BufferPtr - WordBegin, Text);
    return true;
  }

  /// Put back tokens that we didn't consume.
  void putBackLeftoverTokens() {
    if (isEnd())
      return;

    bool HavePartialTok = false;
    Token PartialTok;
    if (Pos.BufferPtr != Pos.BufferStart) {
      formTokenWithChars(PartialTok, getSourceLocation(),
                         Pos.BufferPtr, Pos.BufferEnd - Pos.BufferPtr,
                         StringRef(Pos.BufferPtr,
                                   Pos.BufferEnd - Pos.BufferPtr));
      HavePartialTok = true;
      Pos.CurToken++;
    }

    P.putBack(llvm::makeArrayRef(Toks.begin() + Pos.CurToken, Toks.end()));
    Pos.CurToken = Toks.size();

    if (HavePartialTok)
      P.putBack(PartialTok);
  }
};

Parser::Parser(Lexer &L, Sema &S, llvm::BumpPtrAllocator &Allocator,
               const SourceManager &SourceMgr, DiagnosticsEngine &Diags,
               const CommandTraits &Traits):
    L(L), S(S), Allocator(Allocator), SourceMgr(SourceMgr), Diags(Diags),
    Traits(Traits) {
  consumeToken();
}

void Parser::parseParamCommandArgs(ParamCommandComment *PC,
                                   TextTokenRetokenizer &Retokenizer) {
  Token Arg;
  // Check if argument looks like direction specification: [dir]
  // e.g., [in], [out], [in,out]
  if (Retokenizer.lexDelimitedSeq(Arg, '[', ']'))
    S.actOnParamCommandDirectionArg(PC,
                                    Arg.getLocation(),
                                    Arg.getEndLocation(),
                                    Arg.getText());

  if (Retokenizer.lexWord(Arg))
    S.actOnParamCommandParamNameArg(PC,
                                    Arg.getLocation(),
                                    Arg.getEndLocation(),
                                    Arg.getText());
}

void Parser::parseTParamCommandArgs(TParamCommandComment *TPC,
                                    TextTokenRetokenizer &Retokenizer) {
  Token Arg;
  if (Retokenizer.lexWord(Arg))
    S.actOnTParamCommandParamNameArg(TPC,
                                     Arg.getLocation(),
                                     Arg.getEndLocation(),
                                     Arg.getText());
}

void Parser::parseBlockCommandArgs(BlockCommandComment *BC,
                                   TextTokenRetokenizer &Retokenizer,
                                   unsigned NumArgs) {
  typedef BlockCommandComment::Argument Argument;
  Argument *Args =
      new (Allocator.Allocate<Argument>(NumArgs)) Argument[NumArgs];
  unsigned ParsedArgs = 0;
  Token Arg;
  while (ParsedArgs < NumArgs && Retokenizer.lexWord(Arg)) {
    Args[ParsedArgs] = Argument(SourceRange(Arg.getLocation(),
                                            Arg.getEndLocation()),
                                Arg.getText());
    ParsedArgs++;
  }

  S.actOnBlockCommandArgs(BC, llvm::makeArrayRef(Args, ParsedArgs));
}

BlockCommandComment *Parser::parseBlockCommand() {
  assert(Tok.is(tok::backslash_command) || Tok.is(tok::at_command));

  ParamCommandComment *PC = nullptr;
  TParamCommandComment *TPC = nullptr;
  BlockCommandComment *BC = nullptr;
  const CommandInfo *Info = Traits.getCommandInfo(Tok.getCommandID());
  CommandMarkerKind CommandMarker =
      Tok.is(tok::backslash_command) ? CMK_Backslash : CMK_At;
  if (Info->IsParamCommand) {
    PC = S.actOnParamCommandStart(Tok.getLocation(),
                                  Tok.getEndLocation(),
                                  Tok.getCommandID(),
                                  CommandMarker);
  } else if (Info->IsTParamCommand) {
    TPC = S.actOnTParamCommandStart(Tok.getLocation(),
                                    Tok.getEndLocation(),
                                    Tok.getCommandID(),
                                    CommandMarker);
  } else {
    BC = S.actOnBlockCommandStart(Tok.getLocation(),
                                  Tok.getEndLocation(),
                                  Tok.getCommandID(),
                                  CommandMarker);
  }
  consumeToken();

  if (isTokBlockCommand()) {
    // Block command ahead.  We can't nest block commands, so pretend that this
    // command has an empty argument.
    ParagraphComment *Paragraph = S.actOnParagraphComment(None);
    if (PC) {
      S.actOnParamCommandFinish(PC, Paragraph);
      return PC;
    } else if (TPC) {
      S.actOnTParamCommandFinish(TPC, Paragraph);
      return TPC;
    } else {
      S.actOnBlockCommandFinish(BC, Paragraph);
      return BC;
    }
  }

  if (PC || TPC || Info->NumArgs > 0) {
    // In order to parse command arguments we need to retokenize a few
    // following text tokens.
    TextTokenRetokenizer Retokenizer(Allocator, *this);

    if (PC)
      parseParamCommandArgs(PC, Retokenizer);
    else if (TPC)
      parseTParamCommandArgs(TPC, Retokenizer);
    else
      parseBlockCommandArgs(BC, Retokenizer, Info->NumArgs);

    Retokenizer.putBackLeftoverTokens();
  }

  // If there's a block command ahead, we will attach an empty paragraph to
  // this command.
  bool EmptyParagraph = false;
  if (isTokBlockCommand())
    EmptyParagraph = true;
  else if (Tok.is(tok::newline)) {
    Token PrevTok = Tok;
    consumeToken();
    EmptyParagraph = isTokBlockCommand();
    putBack(PrevTok);
  }

  ParagraphComment *Paragraph;
  if (EmptyParagraph)
    Paragraph = S.actOnParagraphComment(None);
  else {
    BlockContentComment *Block = parseParagraphOrBlockCommand();
    // Since we have checked for a block command, we should have parsed a
    // paragraph.
    Paragraph = cast<ParagraphComment>(Block);
  }

  if (PC) {
    S.actOnParamCommandFinish(PC, Paragraph);
    return PC;
  } else if (TPC) {
    S.actOnTParamCommandFinish(TPC, Paragraph);
    return TPC;
  } else {
    S.actOnBlockCommandFinish(BC, Paragraph);
    return BC;
  }
}

InlineCommandComment *Parser::parseInlineCommand() {
  assert(Tok.is(tok::backslash_command) || Tok.is(tok::at_command));

  const Token CommandTok = Tok;
  consumeToken();

  TextTokenRetokenizer Retokenizer(Allocator, *this);

  Token ArgTok;
  bool ArgTokValid = Retokenizer.lexWord(ArgTok);

  InlineCommandComment *IC;
  if (ArgTokValid) {
    IC = S.actOnInlineCommand(CommandTok.getLocation(),
                              CommandTok.getEndLocation(),
                              CommandTok.getCommandID(),
                              ArgTok.getLocation(),
                              ArgTok.getEndLocation(),
                              ArgTok.getText());
  } else {
    IC = S.actOnInlineCommand(CommandTok.getLocation(),
                              CommandTok.getEndLocation(),
                              CommandTok.getCommandID());
  }

  Retokenizer.putBackLeftoverTokens();

  return IC;
}

HTMLStartTagComment *Parser::parseHTMLStartTag() {
  assert(Tok.is(tok::html_start_tag));
  HTMLStartTagComment *HST =
      S.actOnHTMLStartTagStart(Tok.getLocation(),
                               Tok.getHTMLTagStartName());
  consumeToken();

  SmallVector<HTMLStartTagComment::Attribute, 2> Attrs;
  while (true) {
    switch (Tok.getKind()) {
    case tok::html_ident: {
      Token Ident = Tok;
      consumeToken();
      if (Tok.isNot(tok::html_equals)) {
        Attrs.push_back(HTMLStartTagComment::Attribute(Ident.getLocation(),
                                                       Ident.getHTMLIdent()));
        continue;
      }
      Token Equals = Tok;
      consumeToken();
      if (Tok.isNot(tok::html_quoted_string)) {
        Diag(Tok.getLocation(),
             diag::warn_doc_html_start_tag_expected_quoted_string)
          << SourceRange(Equals.getLocation());
        Attrs.push_back(HTMLStartTagComment::Attribute(Ident.getLocation(),
                                                       Ident.getHTMLIdent()));
        while (Tok.is(tok::html_equals) ||
               Tok.is(tok::html_quoted_string))
          consumeToken();
        continue;
      }
      Attrs.push_back(HTMLStartTagComment::Attribute(
                              Ident.getLocation(),
                              Ident.getHTMLIdent(),
                              Equals.getLocation(),
                              SourceRange(Tok.getLocation(),
                                          Tok.getEndLocation()),
                              Tok.getHTMLQuotedString()));
      consumeToken();
      continue;
    }

    case tok::html_greater:
      S.actOnHTMLStartTagFinish(HST,
                                S.copyArray(llvm::makeArrayRef(Attrs)),
                                Tok.getLocation(),
                                /* IsSelfClosing = */ false);
      consumeToken();
      return HST;

    case tok::html_slash_greater:
      S.actOnHTMLStartTagFinish(HST,
                                S.copyArray(llvm::makeArrayRef(Attrs)),
                                Tok.getLocation(),
                                /* IsSelfClosing = */ true);
      consumeToken();
      return HST;

    case tok::html_equals:
    case tok::html_quoted_string:
      Diag(Tok.getLocation(),
           diag::warn_doc_html_start_tag_expected_ident_or_greater);
      while (Tok.is(tok::html_equals) ||
             Tok.is(tok::html_quoted_string))
        consumeToken();
      if (Tok.is(tok::html_ident) ||
          Tok.is(tok::html_greater) ||
          Tok.is(tok::html_slash_greater))
        continue;

      S.actOnHTMLStartTagFinish(HST,
                                S.copyArray(llvm::makeArrayRef(Attrs)),
                                SourceLocation(),
                                /* IsSelfClosing = */ false);
      return HST;

    default:
      // Not a token from an HTML start tag.  Thus HTML tag prematurely ended.
      S.actOnHTMLStartTagFinish(HST,
                                S.copyArray(llvm::makeArrayRef(Attrs)),
                                SourceLocation(),
                                /* IsSelfClosing = */ false);
      bool StartLineInvalid;
      const unsigned StartLine = SourceMgr.getPresumedLineNumber(
                                                  HST->getLocation(),
                                                  &StartLineInvalid);
      bool EndLineInvalid;
      const unsigned EndLine = SourceMgr.getPresumedLineNumber(
                                                  Tok.getLocation(),
                                                  &EndLineInvalid);
      if (StartLineInvalid || EndLineInvalid || StartLine == EndLine)
        Diag(Tok.getLocation(),
             diag::warn_doc_html_start_tag_expected_ident_or_greater)
          << HST->getSourceRange();
      else {
        Diag(Tok.getLocation(),
             diag::warn_doc_html_start_tag_expected_ident_or_greater);
        Diag(HST->getLocation(), diag::note_doc_html_tag_started_here)
          << HST->getSourceRange();
      }
      return HST;
    }
  }
}

HTMLEndTagComment *Parser::parseHTMLEndTag() {
  assert(Tok.is(tok::html_end_tag));
  Token TokEndTag = Tok;
  consumeToken();
  SourceLocation Loc;
  if (Tok.is(tok::html_greater)) {
    Loc = Tok.getLocation();
    consumeToken();
  }

  return S.actOnHTMLEndTag(TokEndTag.getLocation(),
                           Loc,
                           TokEndTag.getHTMLTagEndName());
}

BlockContentComment *Parser::parseParagraphOrBlockCommand() {
  SmallVector<InlineContentComment *, 8> Content;

  while (true) {
    switch (Tok.getKind()) {
    case tok::verbatim_block_begin:
    case tok::verbatim_line_name:
    case tok::eof:
      break; // Block content or EOF ahead, finish this parapgaph.

    case tok::unknown_command:
      Content.push_back(S.actOnUnknownCommand(Tok.getLocation(),
                                              Tok.getEndLocation(),
                                              Tok.getUnknownCommandName()));
      consumeToken();
      continue;

    case tok::backslash_command:
    case tok::at_command: {
      const CommandInfo *Info = Traits.getCommandInfo(Tok.getCommandID());
      if (Info->IsBlockCommand) {
        if (Content.size() == 0)
          return parseBlockCommand();
        break; // Block command ahead, finish this parapgaph.
      }
      if (Info->IsVerbatimBlockEndCommand) {
        Diag(Tok.getLocation(),
             diag::warn_verbatim_block_end_without_start)
          << Tok.is(tok::at_command)
          << Info->Name
          << SourceRange(Tok.getLocation(), Tok.getEndLocation());
        consumeToken();
        continue;
      }
      if (Info->IsUnknownCommand) {
        Content.push_back(S.actOnUnknownCommand(Tok.getLocation(),
                                                Tok.getEndLocation(),
                                                Info->getID()));
        consumeToken();
        continue;
      }
      assert(Info->IsInlineCommand);
      Content.push_back(parseInlineCommand());
      continue;
    }

    case tok::newline: {
      consumeToken();
      if (Tok.is(tok::newline) || Tok.is(tok::eof)) {
        consumeToken();
        break; // Two newlines -- end of paragraph.
      }
      // Also allow [tok::newline, tok::text, tok::newline] if the middle
      // tok::text is just whitespace.
      if (Tok.is(tok::text) && isWhitespace(Tok.getText())) {
        Token WhitespaceTok = Tok;
        consumeToken();
        if (Tok.is(tok::newline) || Tok.is(tok::eof)) {
          consumeToken();
          break;
        }
        // We have [tok::newline, tok::text, non-newline].  Put back tok::text.
        putBack(WhitespaceTok);
      }
      if (Content.size() > 0)
        Content.back()->addTrailingNewline();
      continue;
    }

    // Don't deal with HTML tag soup now.
    case tok::html_start_tag:
      Content.push_back(parseHTMLStartTag());
      continue;

    case tok::html_end_tag:
      Content.push_back(parseHTMLEndTag());
      continue;

    case tok::text:
      Content.push_back(S.actOnText(Tok.getLocation(),
                                    Tok.getEndLocation(),
                                    Tok.getText()));
      consumeToken();
      continue;

    case tok::verbatim_block_line:
    case tok::verbatim_block_end:
    case tok::verbatim_line_text:
    case tok::html_ident:
    case tok::html_equals:
    case tok::html_quoted_string:
    case tok::html_greater:
    case tok::html_slash_greater:
      llvm_unreachable("should not see this token");
    }
    break;
  }

  return S.actOnParagraphComment(S.copyArray(llvm::makeArrayRef(Content)));
}

VerbatimBlockComment *Parser::parseVerbatimBlock() {
  assert(Tok.is(tok::verbatim_block_begin));

  VerbatimBlockComment *VB =
      S.actOnVerbatimBlockStart(Tok.getLocation(),
                                Tok.getVerbatimBlockID());
  consumeToken();

  // Don't create an empty line if verbatim opening command is followed
  // by a newline.
  if (Tok.is(tok::newline))
    consumeToken();

  SmallVector<VerbatimBlockLineComment *, 8> Lines;
  while (Tok.is(tok::verbatim_block_line) ||
         Tok.is(tok::newline)) {
    VerbatimBlockLineComment *Line;
    if (Tok.is(tok::verbatim_block_line)) {
      Line = S.actOnVerbatimBlockLine(Tok.getLocation(),
                                      Tok.getVerbatimBlockText());
      consumeToken();
      if (Tok.is(tok::newline)) {
        consumeToken();
      }
    } else {
      // Empty line, just a tok::newline.
      Line = S.actOnVerbatimBlockLine(Tok.getLocation(), "");
      consumeToken();
    }
    Lines.push_back(Line);
  }

  if (Tok.is(tok::verbatim_block_end)) {
    const CommandInfo *Info = Traits.getCommandInfo(Tok.getVerbatimBlockID());
    S.actOnVerbatimBlockFinish(VB, Tok.getLocation(),
                               Info->Name,
                               S.copyArray(llvm::makeArrayRef(Lines)));
    consumeToken();
  } else {
    // Unterminated \\verbatim block
    S.actOnVerbatimBlockFinish(VB, SourceLocation(), "",
                               S.copyArray(llvm::makeArrayRef(Lines)));
  }

  return VB;
}

VerbatimLineComment *Parser::parseVerbatimLine() {
  assert(Tok.is(tok::verbatim_line_name));

  Token NameTok = Tok;
  consumeToken();

  SourceLocation TextBegin;
  StringRef Text;
  // Next token might not be a tok::verbatim_line_text if verbatim line
  // starting command comes just before a newline or comment end.
  if (Tok.is(tok::verbatim_line_text)) {
    TextBegin = Tok.getLocation();
    Text = Tok.getVerbatimLineText();
  } else {
    TextBegin = NameTok.getEndLocation();
    Text = "";
  }

  VerbatimLineComment *VL = S.actOnVerbatimLine(NameTok.getLocation(),
                                                NameTok.getVerbatimLineID(),
                                                TextBegin,
                                                Text);
  consumeToken();
  return VL;
}

BlockContentComment *Parser::parseBlockContent() {
  switch (Tok.getKind()) {
  case tok::text:
  case tok::unknown_command:
  case tok::backslash_command:
  case tok::at_command:
  case tok::html_start_tag:
  case tok::html_end_tag:
    return parseParagraphOrBlockCommand();

  case tok::verbatim_block_begin:
    return parseVerbatimBlock();

  case tok::verbatim_line_name:
    return parseVerbatimLine();

  case tok::eof:
  case tok::newline:
  case tok::verbatim_block_line:
  case tok::verbatim_block_end:
  case tok::verbatim_line_text:
  case tok::html_ident:
  case tok::html_equals:
  case tok::html_quoted_string:
  case tok::html_greater:
  case tok::html_slash_greater:
    llvm_unreachable("should not see this token");
  }
  llvm_unreachable("bogus token kind");
}

FullComment *Parser::parseFullComment() {
  // Skip newlines at the beginning of the comment.
  while (Tok.is(tok::newline))
    consumeToken();

  SmallVector<BlockContentComment *, 8> Blocks;
  while (Tok.isNot(tok::eof)) {
    Blocks.push_back(parseBlockContent());

    // Skip extra newlines after paragraph end.
    while (Tok.is(tok::newline))
      consumeToken();
  }
  return S.actOnFullComment(S.copyArray(llvm::makeArrayRef(Blocks)));
}

} // end namespace comments
} // end namespace clang
