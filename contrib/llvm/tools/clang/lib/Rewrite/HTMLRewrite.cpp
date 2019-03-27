//== HTMLRewrite.cpp - Translate source code into prettified HTML --*- C++ -*-//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the HTMLRewriter class, which is used to translate the
//  text of a source file into prettified HTML.
//
//===----------------------------------------------------------------------===//

#include "clang/Rewrite/Core/HTMLRewrite.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/TokenConcatenation.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
using namespace clang;


/// HighlightRange - Highlight a range in the source code with the specified
/// start/end tags.  B/E must be in the same file.  This ensures that
/// start/end tags are placed at the start/end of each line if the range is
/// multiline.
void html::HighlightRange(Rewriter &R, SourceLocation B, SourceLocation E,
                          const char *StartTag, const char *EndTag,
                          bool IsTokenRange) {
  SourceManager &SM = R.getSourceMgr();
  B = SM.getExpansionLoc(B);
  E = SM.getExpansionLoc(E);
  FileID FID = SM.getFileID(B);
  assert(SM.getFileID(E) == FID && "B/E not in the same file!");

  unsigned BOffset = SM.getFileOffset(B);
  unsigned EOffset = SM.getFileOffset(E);

  // Include the whole end token in the range.
  if (IsTokenRange)
    EOffset += Lexer::MeasureTokenLength(E, R.getSourceMgr(), R.getLangOpts());

  bool Invalid = false;
  const char *BufferStart = SM.getBufferData(FID, &Invalid).data();
  if (Invalid)
    return;

  HighlightRange(R.getEditBuffer(FID), BOffset, EOffset,
                 BufferStart, StartTag, EndTag);
}

/// HighlightRange - This is the same as the above method, but takes
/// decomposed file locations.
void html::HighlightRange(RewriteBuffer &RB, unsigned B, unsigned E,
                          const char *BufferStart,
                          const char *StartTag, const char *EndTag) {
  // Insert the tag at the absolute start/end of the range.
  RB.InsertTextAfter(B, StartTag);
  RB.InsertTextBefore(E, EndTag);

  // Scan the range to see if there is a \r or \n.  If so, and if the line is
  // not blank, insert tags on that line as well.
  bool HadOpenTag = true;

  unsigned LastNonWhiteSpace = B;
  for (unsigned i = B; i != E; ++i) {
    switch (BufferStart[i]) {
    case '\r':
    case '\n':
      // Okay, we found a newline in the range.  If we have an open tag, we need
      // to insert a close tag at the first non-whitespace before the newline.
      if (HadOpenTag)
        RB.InsertTextBefore(LastNonWhiteSpace+1, EndTag);

      // Instead of inserting an open tag immediately after the newline, we
      // wait until we see a non-whitespace character.  This prevents us from
      // inserting tags around blank lines, and also allows the open tag to
      // be put *after* whitespace on a non-blank line.
      HadOpenTag = false;
      break;
    case '\0':
    case ' ':
    case '\t':
    case '\f':
    case '\v':
      // Ignore whitespace.
      break;

    default:
      // If there is no tag open, do it now.
      if (!HadOpenTag) {
        RB.InsertTextAfter(i, StartTag);
        HadOpenTag = true;
      }

      // Remember this character.
      LastNonWhiteSpace = i;
      break;
    }
  }
}

void html::EscapeText(Rewriter &R, FileID FID,
                      bool EscapeSpaces, bool ReplaceTabs) {

  const llvm::MemoryBuffer *Buf = R.getSourceMgr().getBuffer(FID);
  const char* C = Buf->getBufferStart();
  const char* FileEnd = Buf->getBufferEnd();

  assert (C <= FileEnd);

  RewriteBuffer &RB = R.getEditBuffer(FID);

  unsigned ColNo = 0;
  for (unsigned FilePos = 0; C != FileEnd ; ++C, ++FilePos) {
    switch (*C) {
    default: ++ColNo; break;
    case '\n':
    case '\r':
      ColNo = 0;
      break;

    case ' ':
      if (EscapeSpaces)
        RB.ReplaceText(FilePos, 1, "&nbsp;");
      ++ColNo;
      break;
    case '\f':
      RB.ReplaceText(FilePos, 1, "<hr>");
      ColNo = 0;
      break;

    case '\t': {
      if (!ReplaceTabs)
        break;
      unsigned NumSpaces = 8-(ColNo&7);
      if (EscapeSpaces)
        RB.ReplaceText(FilePos, 1,
                       StringRef("&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;"
                                       "&nbsp;&nbsp;&nbsp;", 6*NumSpaces));
      else
        RB.ReplaceText(FilePos, 1, StringRef("        ", NumSpaces));
      ColNo += NumSpaces;
      break;
    }
    case '<':
      RB.ReplaceText(FilePos, 1, "&lt;");
      ++ColNo;
      break;

    case '>':
      RB.ReplaceText(FilePos, 1, "&gt;");
      ++ColNo;
      break;

    case '&':
      RB.ReplaceText(FilePos, 1, "&amp;");
      ++ColNo;
      break;
    }
  }
}

std::string html::EscapeText(StringRef s, bool EscapeSpaces, bool ReplaceTabs) {

  unsigned len = s.size();
  std::string Str;
  llvm::raw_string_ostream os(Str);

  for (unsigned i = 0 ; i < len; ++i) {

    char c = s[i];
    switch (c) {
    default:
      os << c; break;

    case ' ':
      if (EscapeSpaces) os << "&nbsp;";
      else os << ' ';
      break;

    case '\t':
      if (ReplaceTabs) {
        if (EscapeSpaces)
          for (unsigned i = 0; i < 4; ++i)
            os << "&nbsp;";
        else
          for (unsigned i = 0; i < 4; ++i)
            os << " ";
      }
      else
        os << c;

      break;

    case '<': os << "&lt;"; break;
    case '>': os << "&gt;"; break;
    case '&': os << "&amp;"; break;
    }
  }

  return os.str();
}

static void AddLineNumber(RewriteBuffer &RB, unsigned LineNo,
                          unsigned B, unsigned E) {
  SmallString<256> Str;
  llvm::raw_svector_ostream OS(Str);

  OS << "<tr class=\"codeline\" data-linenumber=\"" << LineNo << "\">"
     << "<td class=\"num\" id=\"LN" << LineNo << "\">" << LineNo
     << "</td><td class=\"line\">";

  if (B == E) { // Handle empty lines.
    OS << " </td></tr>";
    RB.InsertTextBefore(B, OS.str());
  } else {
    RB.InsertTextBefore(B, OS.str());
    RB.InsertTextBefore(E, "</td></tr>");
  }
}

void html::AddLineNumbers(Rewriter& R, FileID FID) {

  const llvm::MemoryBuffer *Buf = R.getSourceMgr().getBuffer(FID);
  const char* FileBeg = Buf->getBufferStart();
  const char* FileEnd = Buf->getBufferEnd();
  const char* C = FileBeg;
  RewriteBuffer &RB = R.getEditBuffer(FID);

  assert (C <= FileEnd);

  unsigned LineNo = 0;
  unsigned FilePos = 0;

  while (C != FileEnd) {

    ++LineNo;
    unsigned LineStartPos = FilePos;
    unsigned LineEndPos = FileEnd - FileBeg;

    assert (FilePos <= LineEndPos);
    assert (C < FileEnd);

    // Scan until the newline (or end-of-file).

    while (C != FileEnd) {
      char c = *C;
      ++C;

      if (c == '\n') {
        LineEndPos = FilePos++;
        break;
      }

      ++FilePos;
    }

    AddLineNumber(RB, LineNo, LineStartPos, LineEndPos);
  }

  // Add one big table tag that surrounds all of the code.
  std::string s;
  llvm::raw_string_ostream os(s);
  os << "<table class=\"code\" data-fileid=\"" << FID.getHashValue() << "\">\n";
  RB.InsertTextBefore(0, os.str());
  RB.InsertTextAfter(FileEnd - FileBeg, "</table>");
}

void html::AddHeaderFooterInternalBuiltinCSS(Rewriter &R, FileID FID,
                                             StringRef title) {

  const llvm::MemoryBuffer *Buf = R.getSourceMgr().getBuffer(FID);
  const char* FileStart = Buf->getBufferStart();
  const char* FileEnd = Buf->getBufferEnd();

  SourceLocation StartLoc = R.getSourceMgr().getLocForStartOfFile(FID);
  SourceLocation EndLoc = StartLoc.getLocWithOffset(FileEnd-FileStart);

  std::string s;
  llvm::raw_string_ostream os(s);
  os << "<!doctype html>\n" // Use HTML 5 doctype
        "<html>\n<head>\n";

  if (!title.empty())
    os << "<title>" << html::EscapeText(title) << "</title>\n";

  os << R"<<<(
<style type="text/css">
body { color:#000000; background-color:#ffffff }
body { font-family:Helvetica, sans-serif; font-size:10pt }
h1 { font-size:14pt }
.FileName { margin-top: 5px; margin-bottom: 5px; display: inline; }
.FileNav { margin-left: 5px; margin-right: 5px; display: inline; }
.FileNav a { text-decoration:none; font-size: larger; }
.divider { margin-top: 30px; margin-bottom: 30px; height: 15px; }
.divider { background-color: gray; }
.code { border-collapse:collapse; width:100%; }
.code { font-family: "Monospace", monospace; font-size:10pt }
.code { line-height: 1.2em }
.comment { color: green; font-style: oblique }
.keyword { color: blue }
.string_literal { color: red }
.directive { color: darkmagenta }
/* Macro expansions. */
.expansion { display: none; }
.macro:hover .expansion {
  display: block;
  border: 2px solid #FF0000;
  padding: 2px;
  background-color:#FFF0F0;
  font-weight: normal;
  -webkit-border-radius:5px;
  -webkit-box-shadow:1px 1px 7px #000;
  border-radius:5px;
  box-shadow:1px 1px 7px #000;
  position: absolute;
  top: -1em;
  left:10em;
  z-index: 1
}

#tooltiphint {
  position: fixed;
  width: 50em;
  margin-left: -25em;
  left: 50%;
  padding: 10px;
  border: 1px solid #b0b0b0;
  border-radius: 2px;
  box-shadow: 1px 1px 7px black;
  background-color: #c0c0c0;
  z-index: 2;
}
.macro {
  color: darkmagenta;
  background-color:LemonChiffon;
  /* Macros are position: relative to provide base for expansions. */
  position: relative;
}

.num { width:2.5em; padding-right:2ex; background-color:#eeeeee }
.num { text-align:right; font-size:8pt }
.num { color:#444444 }
.line { padding-left: 1ex; border-left: 3px solid #ccc }
.line { white-space: pre }
.msg { -webkit-box-shadow:1px 1px 7px #000 }
.msg { box-shadow:1px 1px 7px #000 }
.msg { -webkit-border-radius:5px }
.msg { border-radius:5px }
.msg { font-family:Helvetica, sans-serif; font-size:8pt }
.msg { float:left }
.msg { padding:0.25em 1ex 0.25em 1ex }
.msg { margin-top:10px; margin-bottom:10px }
.msg { font-weight:bold }
.msg { max-width:60em; word-wrap: break-word; white-space: pre-wrap }
.msgT { padding:0x; spacing:0x }
.msgEvent { background-color:#fff8b4; color:#000000 }
.msgControl { background-color:#bbbbbb; color:#000000 }
.msgNote { background-color:#ddeeff; color:#000000 }
.mrange { background-color:#dfddf3 }
.mrange { border-bottom:1px solid #6F9DBE }
.PathIndex { font-weight: bold; padding:0px 5px; margin-right:5px; }
.PathIndex { -webkit-border-radius:8px }
.PathIndex { border-radius:8px }
.PathIndexEvent { background-color:#bfba87 }
.PathIndexControl { background-color:#8c8c8c }
.PathNav a { text-decoration:none; font-size: larger }
.CodeInsertionHint { font-weight: bold; background-color: #10dd10 }
.CodeRemovalHint { background-color:#de1010 }
.CodeRemovalHint { border-bottom:1px solid #6F9DBE }
.selected{ background-color:orange !important; }

table.simpletable {
  padding: 5px;
  font-size:12pt;
  margin:20px;
  border-collapse: collapse; border-spacing: 0px;
}
td.rowname {
  text-align: right;
  vertical-align: top;
  font-weight: bold;
  color:#444444;
  padding-right:2ex;
}

/* Hidden text. */
input.spoilerhider + label {
  cursor: pointer;
  text-decoration: underline;
  display: block;
}
input.spoilerhider {
 display: none;
}
input.spoilerhider ~ .spoiler {
  overflow: hidden;
  margin: 10px auto 0;
  height: 0;
  opacity: 0;
}
input.spoilerhider:checked + label + .spoiler{
  height: auto;
  opacity: 1;
}
</style>
</head>
<body>)<<<";

  // Generate header
  R.InsertTextBefore(StartLoc, os.str());
  // Generate footer

  R.InsertTextAfter(EndLoc, "</body></html>\n");
}

/// SyntaxHighlight - Relex the specified FileID and annotate the HTML with
/// information about keywords, macro expansions etc.  This uses the macro
/// table state from the end of the file, so it won't be perfectly perfect,
/// but it will be reasonably close.
void html::SyntaxHighlight(Rewriter &R, FileID FID, const Preprocessor &PP) {
  RewriteBuffer &RB = R.getEditBuffer(FID);

  const SourceManager &SM = PP.getSourceManager();
  const llvm::MemoryBuffer *FromFile = SM.getBuffer(FID);
  Lexer L(FID, FromFile, SM, PP.getLangOpts());
  const char *BufferStart = L.getBuffer().data();

  // Inform the preprocessor that we want to retain comments as tokens, so we
  // can highlight them.
  L.SetCommentRetentionState(true);

  // Lex all the tokens in raw mode, to avoid entering #includes or expanding
  // macros.
  Token Tok;
  L.LexFromRawLexer(Tok);

  while (Tok.isNot(tok::eof)) {
    // Since we are lexing unexpanded tokens, all tokens are from the main
    // FileID.
    unsigned TokOffs = SM.getFileOffset(Tok.getLocation());
    unsigned TokLen = Tok.getLength();
    switch (Tok.getKind()) {
    default: break;
    case tok::identifier:
      llvm_unreachable("tok::identifier in raw lexing mode!");
    case tok::raw_identifier: {
      // Fill in Result.IdentifierInfo and update the token kind,
      // looking up the identifier in the identifier table.
      PP.LookUpIdentifierInfo(Tok);

      // If this is a pp-identifier, for a keyword, highlight it as such.
      if (Tok.isNot(tok::identifier))
        HighlightRange(RB, TokOffs, TokOffs+TokLen, BufferStart,
                       "<span class='keyword'>", "</span>");
      break;
    }
    case tok::comment:
      HighlightRange(RB, TokOffs, TokOffs+TokLen, BufferStart,
                     "<span class='comment'>", "</span>");
      break;
    case tok::utf8_string_literal:
      // Chop off the u part of u8 prefix
      ++TokOffs;
      --TokLen;
      // FALL THROUGH to chop the 8
      LLVM_FALLTHROUGH;
    case tok::wide_string_literal:
    case tok::utf16_string_literal:
    case tok::utf32_string_literal:
      // Chop off the L, u, U or 8 prefix
      ++TokOffs;
      --TokLen;
      LLVM_FALLTHROUGH;
    case tok::string_literal:
      // FIXME: Exclude the optional ud-suffix from the highlighted range.
      HighlightRange(RB, TokOffs, TokOffs+TokLen, BufferStart,
                     "<span class='string_literal'>", "</span>");
      break;
    case tok::hash: {
      // If this is a preprocessor directive, all tokens to end of line are too.
      if (!Tok.isAtStartOfLine())
        break;

      // Eat all of the tokens until we get to the next one at the start of
      // line.
      unsigned TokEnd = TokOffs+TokLen;
      L.LexFromRawLexer(Tok);
      while (!Tok.isAtStartOfLine() && Tok.isNot(tok::eof)) {
        TokEnd = SM.getFileOffset(Tok.getLocation())+Tok.getLength();
        L.LexFromRawLexer(Tok);
      }

      // Find end of line.  This is a hack.
      HighlightRange(RB, TokOffs, TokEnd, BufferStart,
                     "<span class='directive'>", "</span>");

      // Don't skip the next token.
      continue;
    }
    }

    L.LexFromRawLexer(Tok);
  }
}

/// HighlightMacros - This uses the macro table state from the end of the
/// file, to re-expand macros and insert (into the HTML) information about the
/// macro expansions.  This won't be perfectly perfect, but it will be
/// reasonably close.
void html::HighlightMacros(Rewriter &R, FileID FID, const Preprocessor& PP) {
  // Re-lex the raw token stream into a token buffer.
  const SourceManager &SM = PP.getSourceManager();
  std::vector<Token> TokenStream;

  const llvm::MemoryBuffer *FromFile = SM.getBuffer(FID);
  Lexer L(FID, FromFile, SM, PP.getLangOpts());

  // Lex all the tokens in raw mode, to avoid entering #includes or expanding
  // macros.
  while (1) {
    Token Tok;
    L.LexFromRawLexer(Tok);

    // If this is a # at the start of a line, discard it from the token stream.
    // We don't want the re-preprocess step to see #defines, #includes or other
    // preprocessor directives.
    if (Tok.is(tok::hash) && Tok.isAtStartOfLine())
      continue;

    // If this is a ## token, change its kind to unknown so that repreprocessing
    // it will not produce an error.
    if (Tok.is(tok::hashhash))
      Tok.setKind(tok::unknown);

    // If this raw token is an identifier, the raw lexer won't have looked up
    // the corresponding identifier info for it.  Do this now so that it will be
    // macro expanded when we re-preprocess it.
    if (Tok.is(tok::raw_identifier))
      PP.LookUpIdentifierInfo(Tok);

    TokenStream.push_back(Tok);

    if (Tok.is(tok::eof)) break;
  }

  // Temporarily change the diagnostics object so that we ignore any generated
  // diagnostics from this pass.
  DiagnosticsEngine TmpDiags(PP.getDiagnostics().getDiagnosticIDs(),
                             &PP.getDiagnostics().getDiagnosticOptions(),
                      new IgnoringDiagConsumer);

  // FIXME: This is a huge hack; we reuse the input preprocessor because we want
  // its state, but we aren't actually changing it (we hope). This should really
  // construct a copy of the preprocessor.
  Preprocessor &TmpPP = const_cast<Preprocessor&>(PP);
  DiagnosticsEngine *OldDiags = &TmpPP.getDiagnostics();
  TmpPP.setDiagnostics(TmpDiags);

  // Inform the preprocessor that we don't want comments.
  TmpPP.SetCommentRetentionState(false, false);

  // We don't want pragmas either. Although we filtered out #pragma, removing
  // _Pragma and __pragma is much harder.
  bool PragmasPreviouslyEnabled = TmpPP.getPragmasEnabled();
  TmpPP.setPragmasEnabled(false);

  // Enter the tokens we just lexed.  This will cause them to be macro expanded
  // but won't enter sub-files (because we removed #'s).
  TmpPP.EnterTokenStream(TokenStream, false);

  TokenConcatenation ConcatInfo(TmpPP);

  // Lex all the tokens.
  Token Tok;
  TmpPP.Lex(Tok);
  while (Tok.isNot(tok::eof)) {
    // Ignore non-macro tokens.
    if (!Tok.getLocation().isMacroID()) {
      TmpPP.Lex(Tok);
      continue;
    }

    // Okay, we have the first token of a macro expansion: highlight the
    // expansion by inserting a start tag before the macro expansion and
    // end tag after it.
    CharSourceRange LLoc = SM.getExpansionRange(Tok.getLocation());

    // Ignore tokens whose instantiation location was not the main file.
    if (SM.getFileID(LLoc.getBegin()) != FID) {
      TmpPP.Lex(Tok);
      continue;
    }

    assert(SM.getFileID(LLoc.getEnd()) == FID &&
           "Start and end of expansion must be in the same ultimate file!");

    std::string Expansion = EscapeText(TmpPP.getSpelling(Tok));
    unsigned LineLen = Expansion.size();

    Token PrevPrevTok;
    Token PrevTok = Tok;
    // Okay, eat this token, getting the next one.
    TmpPP.Lex(Tok);

    // Skip all the rest of the tokens that are part of this macro
    // instantiation.  It would be really nice to pop up a window with all the
    // spelling of the tokens or something.
    while (!Tok.is(tok::eof) &&
           SM.getExpansionLoc(Tok.getLocation()) == LLoc.getBegin()) {
      // Insert a newline if the macro expansion is getting large.
      if (LineLen > 60) {
        Expansion += "<br>";
        LineLen = 0;
      }

      LineLen -= Expansion.size();

      // If the tokens were already space separated, or if they must be to avoid
      // them being implicitly pasted, add a space between them.
      if (Tok.hasLeadingSpace() ||
          ConcatInfo.AvoidConcat(PrevPrevTok, PrevTok, Tok))
        Expansion += ' ';

      // Escape any special characters in the token text.
      Expansion += EscapeText(TmpPP.getSpelling(Tok));
      LineLen += Expansion.size();

      PrevPrevTok = PrevTok;
      PrevTok = Tok;
      TmpPP.Lex(Tok);
    }


    // Insert the expansion as the end tag, so that multi-line macros all get
    // highlighted.
    Expansion = "<span class='expansion'>" + Expansion + "</span></span>";

    HighlightRange(R, LLoc.getBegin(), LLoc.getEnd(), "<span class='macro'>",
                   Expansion.c_str(), LLoc.isTokenRange());
  }

  // Restore the preprocessor's old state.
  TmpPP.setDiagnostics(*OldDiags);
  TmpPP.setPragmasEnabled(PragmasPreviouslyEnabled);
}
