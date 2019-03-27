//===- TGLexer.h - Lexer for TableGen Files ---------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This class represents the Lexer for tablegen files.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TABLEGEN_TGLEXER_H
#define LLVM_LIB_TABLEGEN_TGLEXER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/SMLoc.h"
#include <cassert>
#include <map>
#include <memory>
#include <string>

namespace llvm {
class SourceMgr;
class SMLoc;
class Twine;

namespace tgtok {
  enum TokKind {
    // Markers
    Eof, Error,

    // Tokens with no info.
    minus, plus,        // - +
    l_square, r_square, // [ ]
    l_brace, r_brace,   // { }
    l_paren, r_paren,   // ( )
    less, greater,      // < >
    colon, semi,        // : ;
    comma, period,      // , .
    equal, question,    // = ?
    paste,              // #

    // Keywords.
    Bit, Bits, Class, Code, Dag, Def, Foreach, Defm, Field, In, Int, Let, List,
    MultiClass, String, Defset,

    // !keywords.
    XConcat, XADD, XAND, XOR, XSRA, XSRL, XSHL, XListConcat, XStrConcat, XCast,
    XSubst, XForEach, XFoldl, XHead, XTail, XSize, XEmpty, XIf, XEq, XIsA, XDag,
    XNe, XLe, XLt, XGe, XGt,

    // Integer value.
    IntVal,

    // Binary constant.  Note that these are sized according to the number of
    // bits given.
    BinaryIntVal,

    // String valued tokens.
    Id, StrVal, VarName, CodeFragment,

    // Preprocessing tokens for internal usage by the lexer.
    // They are never returned as a result of Lex().
    Ifdef, Else, Endif, Define
  };
}

/// TGLexer - TableGen Lexer class.
class TGLexer {
  SourceMgr &SrcMgr;

  const char *CurPtr;
  StringRef CurBuf;

  // Information about the current token.
  const char *TokStart;
  tgtok::TokKind CurCode;
  std::string CurStrVal;  // This is valid for ID, STRVAL, VARNAME, CODEFRAGMENT
  int64_t CurIntVal;      // This is valid for INTVAL.

  /// CurBuffer - This is the current buffer index we're lexing from as managed
  /// by the SourceMgr object.
  unsigned CurBuffer;

public:
  typedef std::map<std::string, SMLoc> DependenciesMapTy;
private:
  /// Dependencies - This is the list of all included files.
  DependenciesMapTy Dependencies;

public:
  TGLexer(SourceMgr &SrcMgr, ArrayRef<std::string> Macros);

  tgtok::TokKind Lex() {
    return CurCode = LexToken(CurPtr == CurBuf.begin());
  }

  const DependenciesMapTy &getDependencies() const {
    return Dependencies;
  }

  tgtok::TokKind getCode() const { return CurCode; }

  const std::string &getCurStrVal() const {
    assert((CurCode == tgtok::Id || CurCode == tgtok::StrVal ||
            CurCode == tgtok::VarName || CurCode == tgtok::CodeFragment) &&
           "This token doesn't have a string value");
    return CurStrVal;
  }
  int64_t getCurIntVal() const {
    assert(CurCode == tgtok::IntVal && "This token isn't an integer");
    return CurIntVal;
  }
  std::pair<int64_t, unsigned> getCurBinaryIntVal() const {
    assert(CurCode == tgtok::BinaryIntVal &&
           "This token isn't a binary integer");
    return std::make_pair(CurIntVal, (CurPtr - TokStart)-2);
  }

  SMLoc getLoc() const;

private:
  /// LexToken - Read the next token and return its code.
  tgtok::TokKind LexToken(bool FileOrLineStart = false);

  tgtok::TokKind ReturnError(SMLoc Loc, const Twine &Msg);
  tgtok::TokKind ReturnError(const char *Loc, const Twine &Msg);

  int getNextChar();
  int peekNextChar(int Index) const;
  void SkipBCPLComment();
  bool SkipCComment();
  tgtok::TokKind LexIdentifier();
  bool LexInclude();
  tgtok::TokKind LexString();
  tgtok::TokKind LexVarName();
  tgtok::TokKind LexNumber();
  tgtok::TokKind LexBracket();
  tgtok::TokKind LexExclaim();

  // Process EOF encountered in LexToken().
  // If EOF is met in an include file, then the method will update
  // CurPtr, CurBuf and preprocessing include stack, and return true.
  // If EOF is met in the top-level file, then the method will
  // update and check the preprocessing include stack, and return false.
  bool processEOF();

  // *** Structures and methods for preprocessing support ***

  // A set of macro names that are defined either via command line or
  // by using:
  //     #define NAME
  StringSet<> DefinedMacros;

  // Each of #ifdef and #else directives has a descriptor associated
  // with it.
  //
  // An ordered list of preprocessing controls defined by #ifdef/#else
  // directives that are in effect currently is called preprocessing
  // control stack.  It is represented as a vector of PreprocessorControlDesc's.
  //
  // The control stack is updated according to the following rules:
  //
  // For each #ifdef we add an element to the control stack.
  // For each #else we replace the top element with a descriptor
  // with an inverted IsDefined value.
  // For each #endif we pop the top element from the control stack.
  //
  // When CurPtr reaches the current buffer's end, the control stack
  // must be empty, i.e. #ifdef and the corresponding #endif
  // must be located in the same file.
  struct PreprocessorControlDesc {
    // Either tgtok::Ifdef or tgtok::Else.
    tgtok::TokKind Kind;

    // True, if the condition for this directive is true, false - otherwise.
    // Examples:
    //     #ifdef NAME       : true, if NAME is defined, false - otherwise.
    //     ...
    //     #else             : false, if NAME is defined, true - otherwise.
    bool IsDefined;

    // Pointer into CurBuf to the beginning of the preprocessing directive
    // word, e.g.:
    //     #ifdef NAME
    //      ^ - SrcPos
    SMLoc SrcPos;
  };

  // We want to disallow code like this:
  //     file1.td:
  //         #define NAME
  //         #ifdef NAME
  //         include "file2.td"
  //     EOF
  //     file2.td:
  //         #endif
  //     EOF
  //
  // To do this, we clear the preprocessing control stack on entry
  // to each of the included file.  PrepIncludeStack is used to store
  // preprocessing control stacks for the current file and all its
  // parent files.  The back() element is the preprocessing control
  // stack for the current file.
  std::vector<std::unique_ptr<std::vector<PreprocessorControlDesc>>>
      PrepIncludeStack;

  // Validate that the current preprocessing control stack is empty,
  // since we are about to exit a file, and pop the include stack.
  //
  // If IncludeStackMustBeEmpty is true, the include stack must be empty
  // after the popping, otherwise, the include stack must not be empty
  // after the popping.  Basically, the include stack must be empty
  // only if we exit the "top-level" file (i.e. finish lexing).
  //
  // The method returns false, if the current preprocessing control stack
  // is not empty (e.g. there is an unterminated #ifdef/#else),
  // true - otherwise.
  bool prepExitInclude(bool IncludeStackMustBeEmpty);

  // Look ahead for a preprocessing directive starting from CurPtr.  The caller
  // must only call this method, if *(CurPtr - 1) is '#'.  If the method matches
  // a preprocessing directive word followed by a whitespace, then it returns
  // one of the internal token kinds, i.e. Ifdef, Else, Endif, Define.
  //
  // CurPtr is not adjusted by this method.
  tgtok::TokKind prepIsDirective() const;

  // Given a preprocessing token kind, adjusts CurPtr to the end
  // of the preprocessing directive word.  Returns true, unless
  // an unsupported token kind is passed in.
  //
  // We use look-ahead prepIsDirective() and prepEatPreprocessorDirective()
  // to avoid adjusting CurPtr before we are sure that '#' is followed
  // by a preprocessing directive.  If it is not, then we fall back to
  // tgtok::paste interpretation of '#'.
  bool prepEatPreprocessorDirective(tgtok::TokKind Kind);

  // The main "exit" point from the token parsing to preprocessor.
  //
  // The method is called for CurPtr, when prepIsDirective() returns
  // true.  The first parameter matches the result of prepIsDirective(),
  // denoting the actual preprocessor directive to be processed.
  //
  // If the preprocessing directive disables the tokens processing, e.g.:
  //     #ifdef NAME // NAME is undefined
  // then lexPreprocessor() enters the lines-skipping mode.
  // In this mode, it does not parse any tokens, because the code under
  // the #ifdef may not even be a correct tablegen code.  The preprocessor
  // looks for lines containing other preprocessing directives, which
  // may be prepended with whitespaces and C-style comments.  If the line
  // does not contain a preprocessing directive, it is skipped completely.
  // Otherwise, the preprocessing directive is processed by recursively
  // calling lexPreprocessor().  The processing of the encountered
  // preprocessing directives includes updating preprocessing control stack
  // and adding new macros into DefinedMacros set.
  //
  // The second parameter controls whether lexPreprocessor() is called from
  // LexToken() (true) or recursively from lexPreprocessor() (false).
  //
  // If ReturnNextLiveToken is true, the method returns the next
  // LEX token following the current directive or following the end
  // of the disabled preprocessing region corresponding to this directive.
  // If ReturnNextLiveToken is false, the method returns the first parameter,
  // unless there were errors encountered in the disabled preprocessing
  // region - in this case, it returns tgtok::Error.
  tgtok::TokKind lexPreprocessor(tgtok::TokKind Kind,
                                 bool ReturnNextLiveToken = true);

  // Worker method for lexPreprocessor() to skip lines after some
  // preprocessing directive up to the buffer end or to the directive
  // that re-enables token processing.  The method returns true
  // upon processing the next directive that re-enables tokens
  // processing.  False is returned if an error was encountered.
  //
  // Note that prepSkipRegion() calls lexPreprocessor() to process
  // encountered preprocessing directives.  In this case, the second
  // parameter to lexPreprocessor() is set to false.  Being passed
  // false ReturnNextLiveToken, lexPreprocessor() must never call
  // prepSkipRegion().  We assert this by passing ReturnNextLiveToken
  // to prepSkipRegion() and checking that it is never set to false.
  bool prepSkipRegion(bool MustNeverBeFalse);

  // Lex name of the macro after either #ifdef or #define.  We could have used
  // LexIdentifier(), but it has special handling of "include" word, which
  // could result in awkward diagnostic errors.  Consider:
  // ----
  // #ifdef include
  // class ...
  // ----
  // LexIdentifier() will engage LexInclude(), which will complain about
  // missing file with name "class".  Instead, prepLexMacroName() will treat
  // "include" as a normal macro name.
  //
  // On entry, CurPtr points to the end of a preprocessing directive word.
  // The method allows for whitespaces between the preprocessing directive
  // and the macro name.  The allowed whitespaces are ' ' and '\t'.
  //
  // If the first non-whitespace symbol after the preprocessing directive
  // is a valid start symbol for an identifier (i.e. [a-zA-Z_]), then
  // the method updates TokStart to the position of the first non-whitespace
  // symbol, sets CurPtr to the position of the macro name's last symbol,
  // and returns a string reference to the macro name.  Otherwise,
  // TokStart is set to the first non-whitespace symbol after the preprocessing
  // directive, and the method returns an empty string reference.
  //
  // In all cases, TokStart may be used to point to the word following
  // the preprocessing directive.
  StringRef prepLexMacroName();

  // Skip any whitespaces starting from CurPtr.  The method is used
  // only in the lines-skipping mode to find the first non-whitespace
  // symbol after or at CurPtr.  Allowed whitespaces are ' ', '\t', '\n'
  // and '\r'.  The method skips C-style comments as well, because
  // it is used to find the beginning of the preprocessing directive.
  // If we do not handle C-style comments the following code would
  // result in incorrect detection of a preprocessing directive:
  //     /*
  //     #ifdef NAME
  //     */
  // As long as we skip C-style comments, the following code is correctly
  // recognized as a preprocessing directive:
  //     /* first line comment
  //        second line comment */ #ifdef NAME
  //
  // The method returns true upon reaching the first non-whitespace symbol
  // or EOF, CurPtr is set to point to this symbol.  The method returns false,
  // if an error occured during skipping of a C-style comment.
  bool prepSkipLineBegin();

  // Skip any whitespaces or comments after a preprocessing directive.
  // The method returns true upon reaching either end of the line
  // or end of the file.  If there is a multiline C-style comment
  // after the preprocessing directive, the method skips
  // the comment, so the final CurPtr may point to one of the next lines.
  // The method returns false, if an error occured during skipping
  // C- or C++-style comment, or a non-whitespace symbol appears
  // after the preprocessing directive.
  //
  // The method maybe called both during lines-skipping and tokens
  // processing.  It actually verifies that only whitespaces or/and
  // comments follow a preprocessing directive.
  //
  // After the execution of this mehod, CurPtr points either to new line
  // symbol, buffer end or non-whitespace symbol following the preprocesing
  // directive.
  bool prepSkipDirectiveEnd();

  // Skip all symbols to the end of the line/file.
  // The method adjusts CurPtr, so that it points to either new line
  // symbol in the current line or the buffer end.
  void prepSkipToLineEnd();

  // Return true, if the current preprocessor control stack is such that
  // we should allow lexer to process the next token, false - otherwise.
  //
  // In particular, the method returns true, if all the #ifdef/#else
  // controls on the stack have their IsDefined member set to true.
  bool prepIsProcessingEnabled();

  // Report an error, if we reach EOF with non-empty preprocessing control
  // stack.  This means there is no matching #endif for the previous
  // #ifdef/#else.
  void prepReportPreprocessorStackError();
};

} // end namespace llvm

#endif
