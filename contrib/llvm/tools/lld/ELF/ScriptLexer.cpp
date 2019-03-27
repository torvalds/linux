//===- ScriptLexer.cpp ----------------------------------------------------===//
//
//                             The LLVM Linker
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a lexer for the linker script.
//
// The linker script's grammar is not complex but ambiguous due to the
// lack of the formal specification of the language. What we are trying to
// do in this and other files in LLD is to make a "reasonable" linker
// script processor.
//
// Among simplicity, compatibility and efficiency, we put the most
// emphasis on simplicity when we wrote this lexer. Compatibility with the
// GNU linkers is important, but we did not try to clone every tiny corner
// case of their lexers, as even ld.bfd and ld.gold are subtly different
// in various corner cases. We do not care much about efficiency because
// the time spent in parsing linker scripts is usually negligible.
//
// Our grammar of the linker script is LL(2), meaning that it needs at
// most two-token lookahead to parse. The only place we need two-token
// lookahead is labels in version scripts, where we need to parse "local :"
// as if "local:".
//
// Overall, this lexer works fine for most linker scripts. There might
// be room for improving compatibility, but that's probably not at the
// top of our todo list.
//
//===----------------------------------------------------------------------===//

#include "ScriptLexer.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/ADT/Twine.h"

using namespace llvm;
using namespace lld;
using namespace lld::elf;

// Returns a whole line containing the current token.
StringRef ScriptLexer::getLine() {
  StringRef S = getCurrentMB().getBuffer();
  StringRef Tok = Tokens[Pos - 1];

  size_t Pos = S.rfind('\n', Tok.data() - S.data());
  if (Pos != StringRef::npos)
    S = S.substr(Pos + 1);
  return S.substr(0, S.find_first_of("\r\n"));
}

// Returns 1-based line number of the current token.
size_t ScriptLexer::getLineNumber() {
  StringRef S = getCurrentMB().getBuffer();
  StringRef Tok = Tokens[Pos - 1];
  return S.substr(0, Tok.data() - S.data()).count('\n') + 1;
}

// Returns 0-based column number of the current token.
size_t ScriptLexer::getColumnNumber() {
  StringRef Tok = Tokens[Pos - 1];
  return Tok.data() - getLine().data();
}

std::string ScriptLexer::getCurrentLocation() {
  std::string Filename = getCurrentMB().getBufferIdentifier();
  return (Filename + ":" + Twine(getLineNumber())).str();
}

ScriptLexer::ScriptLexer(MemoryBufferRef MB) { tokenize(MB); }

// We don't want to record cascading errors. Keep only the first one.
void ScriptLexer::setError(const Twine &Msg) {
  if (errorCount())
    return;

  std::string S = (getCurrentLocation() + ": " + Msg).str();
  if (Pos)
    S += "\n>>> " + getLine().str() + "\n>>> " +
         std::string(getColumnNumber(), ' ') + "^";
  error(S);
}

// Split S into linker script tokens.
void ScriptLexer::tokenize(MemoryBufferRef MB) {
  std::vector<StringRef> Vec;
  MBs.push_back(MB);
  StringRef S = MB.getBuffer();
  StringRef Begin = S;

  for (;;) {
    S = skipSpace(S);
    if (S.empty())
      break;

    // Quoted token. Note that double-quote characters are parts of a token
    // because, in a glob match context, only unquoted tokens are interpreted
    // as glob patterns. Double-quoted tokens are literal patterns in that
    // context.
    if (S.startswith("\"")) {
      size_t E = S.find("\"", 1);
      if (E == StringRef::npos) {
        StringRef Filename = MB.getBufferIdentifier();
        size_t Lineno = Begin.substr(0, S.data() - Begin.data()).count('\n');
        error(Filename + ":" + Twine(Lineno + 1) + ": unclosed quote");
        return;
      }

      Vec.push_back(S.take_front(E + 1));
      S = S.substr(E + 1);
      continue;
    }

    // ">foo" is parsed to ">" and "foo", but ">>" is parsed to ">>".
    // "|", "||", "&" and "&&" are different operators.
    if (S.startswith("<<") || S.startswith("<=") || S.startswith(">>") ||
        S.startswith(">=") || S.startswith("||") || S.startswith("&&")) {
      Vec.push_back(S.substr(0, 2));
      S = S.substr(2);
      continue;
    }

    // Unquoted token. This is more relaxed than tokens in C-like language,
    // so that you can write "file-name.cpp" as one bare token, for example.
    size_t Pos = S.find_first_not_of(
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
        "0123456789_.$/\\~=+[]*?-!^:");

    // A character that cannot start a word (which is usually a
    // punctuation) forms a single character token.
    if (Pos == 0)
      Pos = 1;
    Vec.push_back(S.substr(0, Pos));
    S = S.substr(Pos);
  }

  Tokens.insert(Tokens.begin() + Pos, Vec.begin(), Vec.end());
}

// Skip leading whitespace characters or comments.
StringRef ScriptLexer::skipSpace(StringRef S) {
  for (;;) {
    if (S.startswith("/*")) {
      size_t E = S.find("*/", 2);
      if (E == StringRef::npos) {
        error("unclosed comment in a linker script");
        return "";
      }
      S = S.substr(E + 2);
      continue;
    }
    if (S.startswith("#")) {
      size_t E = S.find('\n', 1);
      if (E == StringRef::npos)
        E = S.size() - 1;
      S = S.substr(E + 1);
      continue;
    }
    size_t Size = S.size();
    S = S.ltrim();
    if (S.size() == Size)
      return S;
  }
}

// An erroneous token is handled as if it were the last token before EOF.
bool ScriptLexer::atEOF() { return errorCount() || Tokens.size() == Pos; }

// Split a given string as an expression.
// This function returns "3", "*" and "5" for "3*5" for example.
static std::vector<StringRef> tokenizeExpr(StringRef S) {
  StringRef Ops = "+-*/:!~"; // List of operators

  // Quoted strings are literal strings, so we don't want to split it.
  if (S.startswith("\""))
    return {S};

  // Split S with operators as separators.
  std::vector<StringRef> Ret;
  while (!S.empty()) {
    size_t E = S.find_first_of(Ops);

    // No need to split if there is no operator.
    if (E == StringRef::npos) {
      Ret.push_back(S);
      break;
    }

    // Get a token before the opreator.
    if (E != 0)
      Ret.push_back(S.substr(0, E));

    // Get the operator as a token. Keep != as one token.
    if (S.substr(E).startswith("!=")) {
      Ret.push_back(S.substr(E, 2));
      S = S.substr(E + 2);
    } else {
      Ret.push_back(S.substr(E, 1));
      S = S.substr(E + 1);
    }
  }
  return Ret;
}

// In contexts where expressions are expected, the lexer should apply
// different tokenization rules than the default one. By default,
// arithmetic operator characters are regular characters, but in the
// expression context, they should be independent tokens.
//
// For example, "foo*3" should be tokenized to "foo", "*" and "3" only
// in the expression context.
//
// This function may split the current token into multiple tokens.
void ScriptLexer::maybeSplitExpr() {
  if (!InExpr || errorCount() || atEOF())
    return;

  std::vector<StringRef> V = tokenizeExpr(Tokens[Pos]);
  if (V.size() == 1)
    return;
  Tokens.erase(Tokens.begin() + Pos);
  Tokens.insert(Tokens.begin() + Pos, V.begin(), V.end());
}

StringRef ScriptLexer::next() {
  maybeSplitExpr();

  if (errorCount())
    return "";
  if (atEOF()) {
    setError("unexpected EOF");
    return "";
  }
  return Tokens[Pos++];
}

StringRef ScriptLexer::peek() {
  StringRef Tok = next();
  if (errorCount())
    return "";
  Pos = Pos - 1;
  return Tok;
}

StringRef ScriptLexer::peek2() {
  skip();
  StringRef Tok = next();
  if (errorCount())
    return "";
  Pos = Pos - 2;
  return Tok;
}

bool ScriptLexer::consume(StringRef Tok) {
  if (peek() == Tok) {
    skip();
    return true;
  }
  return false;
}

// Consumes Tok followed by ":". Space is allowed between Tok and ":".
bool ScriptLexer::consumeLabel(StringRef Tok) {
  if (consume((Tok + ":").str()))
    return true;
  if (Tokens.size() >= Pos + 2 && Tokens[Pos] == Tok &&
      Tokens[Pos + 1] == ":") {
    Pos += 2;
    return true;
  }
  return false;
}

void ScriptLexer::skip() { (void)next(); }

void ScriptLexer::expect(StringRef Expect) {
  if (errorCount())
    return;
  StringRef Tok = next();
  if (Tok != Expect)
    setError(Expect + " expected, but got " + Tok);
}

// Returns true if S encloses T.
static bool encloses(StringRef S, StringRef T) {
  return S.bytes_begin() <= T.bytes_begin() && T.bytes_end() <= S.bytes_end();
}

MemoryBufferRef ScriptLexer::getCurrentMB() {
  // Find input buffer containing the current token.
  assert(!MBs.empty() && Pos > 0);
  for (MemoryBufferRef MB : MBs)
    if (encloses(MB.getBuffer(), Tokens[Pos - 1]))
      return MB;
  llvm_unreachable("getCurrentMB: failed to find a token");
}
