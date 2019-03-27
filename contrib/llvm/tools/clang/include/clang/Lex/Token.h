//===--- Token.h - Token interface ------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the Token interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LEX_TOKEN_H
#define LLVM_CLANG_LEX_TOKEN_H

#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/TokenKinds.h"
#include "llvm/ADT/StringRef.h"
#include <cassert>

namespace clang {

class IdentifierInfo;

/// Token - This structure provides full information about a lexed token.
/// It is not intended to be space efficient, it is intended to return as much
/// information as possible about each returned token.  This is expected to be
/// compressed into a smaller form if memory footprint is important.
///
/// The parser can create a special "annotation token" representing a stream of
/// tokens that were parsed and semantically resolved, e.g.: "foo::MyClass<int>"
/// can be represented by a single typename annotation token that carries
/// information about the SourceRange of the tokens and the type object.
class Token {
  /// The location of the token. This is actually a SourceLocation.
  unsigned Loc;

  // Conceptually these next two fields could be in a union.  However, this
  // causes gcc 4.2 to pessimize LexTokenInternal, a very performance critical
  // routine. Keeping as separate members with casts until a more beautiful fix
  // presents itself.

  /// UintData - This holds either the length of the token text, when
  /// a normal token, or the end of the SourceRange when an annotation
  /// token.
  unsigned UintData;

  /// PtrData - This is a union of four different pointer types, which depends
  /// on what type of token this is:
  ///  Identifiers, keywords, etc:
  ///    This is an IdentifierInfo*, which contains the uniqued identifier
  ///    spelling.
  ///  Literals:  isLiteral() returns true.
  ///    This is a pointer to the start of the token in a text buffer, which
  ///    may be dirty (have trigraphs / escaped newlines).
  ///  Annotations (resolved type names, C++ scopes, etc): isAnnotation().
  ///    This is a pointer to sema-specific data for the annotation token.
  ///  Eof:
  //     This is a pointer to a Decl.
  ///  Other:
  ///    This is null.
  void *PtrData;

  /// Kind - The actual flavor of token this is.
  tok::TokenKind Kind;

  /// Flags - Bits we track about this token, members of the TokenFlags enum.
  unsigned short Flags;

public:
  // Various flags set per token:
  enum TokenFlags {
    StartOfLine   = 0x01,  // At start of line or only after whitespace
                           // (considering the line after macro expansion).
    LeadingSpace  = 0x02,  // Whitespace exists before this token (considering
                           // whitespace after macro expansion).
    DisableExpand = 0x04,  // This identifier may never be macro expanded.
    NeedsCleaning = 0x08,  // Contained an escaped newline or trigraph.
    LeadingEmptyMacro = 0x10, // Empty macro exists before this token.
    HasUDSuffix = 0x20,    // This string or character literal has a ud-suffix.
    HasUCN = 0x40,         // This identifier contains a UCN.
    IgnoredComma = 0x80,   // This comma is not a macro argument separator (MS).
    StringifiedInMacro = 0x100, // This string or character literal is formed by
                                // macro stringizing or charizing operator.
    CommaAfterElided = 0x200, // The comma following this token was elided (MS).
    IsEditorPlaceholder = 0x400, // This identifier is a placeholder.
  };

  tok::TokenKind getKind() const { return Kind; }
  void setKind(tok::TokenKind K) { Kind = K; }

  /// is/isNot - Predicates to check if this token is a specific kind, as in
  /// "if (Tok.is(tok::l_brace)) {...}".
  bool is(tok::TokenKind K) const { return Kind == K; }
  bool isNot(tok::TokenKind K) const { return Kind != K; }
  bool isOneOf(tok::TokenKind K1, tok::TokenKind K2) const {
    return is(K1) || is(K2);
  }
  template <typename... Ts>
  bool isOneOf(tok::TokenKind K1, tok::TokenKind K2, Ts... Ks) const {
    return is(K1) || isOneOf(K2, Ks...);
  }

  /// Return true if this is a raw identifier (when lexing
  /// in raw mode) or a non-keyword identifier (when lexing in non-raw mode).
  bool isAnyIdentifier() const {
    return tok::isAnyIdentifier(getKind());
  }

  /// Return true if this is a "literal", like a numeric
  /// constant, string, etc.
  bool isLiteral() const {
    return tok::isLiteral(getKind());
  }

  /// Return true if this is any of tok::annot_* kind tokens.
  bool isAnnotation() const {
    return tok::isAnnotation(getKind());
  }

  /// Return a source location identifier for the specified
  /// offset in the current file.
  SourceLocation getLocation() const {
    return SourceLocation::getFromRawEncoding(Loc);
  }
  unsigned getLength() const {
    assert(!isAnnotation() && "Annotation tokens have no length field");
    return UintData;
  }

  void setLocation(SourceLocation L) { Loc = L.getRawEncoding(); }
  void setLength(unsigned Len) {
    assert(!isAnnotation() && "Annotation tokens have no length field");
    UintData = Len;
  }

  SourceLocation getAnnotationEndLoc() const {
    assert(isAnnotation() && "Used AnnotEndLocID on non-annotation token");
    return SourceLocation::getFromRawEncoding(UintData ? UintData : Loc);
  }
  void setAnnotationEndLoc(SourceLocation L) {
    assert(isAnnotation() && "Used AnnotEndLocID on non-annotation token");
    UintData = L.getRawEncoding();
  }

  SourceLocation getLastLoc() const {
    return isAnnotation() ? getAnnotationEndLoc() : getLocation();
  }

  SourceLocation getEndLoc() const {
    return isAnnotation() ? getAnnotationEndLoc()
                          : getLocation().getLocWithOffset(getLength());
  }

  /// SourceRange of the group of tokens that this annotation token
  /// represents.
  SourceRange getAnnotationRange() const {
    return SourceRange(getLocation(), getAnnotationEndLoc());
  }
  void setAnnotationRange(SourceRange R) {
    setLocation(R.getBegin());
    setAnnotationEndLoc(R.getEnd());
  }

  const char *getName() const { return tok::getTokenName(Kind); }

  /// Reset all flags to cleared.
  void startToken() {
    Kind = tok::unknown;
    Flags = 0;
    PtrData = nullptr;
    UintData = 0;
    Loc = SourceLocation().getRawEncoding();
  }

  IdentifierInfo *getIdentifierInfo() const {
    assert(isNot(tok::raw_identifier) &&
           "getIdentifierInfo() on a tok::raw_identifier token!");
    assert(!isAnnotation() &&
           "getIdentifierInfo() on an annotation token!");
    if (isLiteral()) return nullptr;
    if (is(tok::eof)) return nullptr;
    return (IdentifierInfo*) PtrData;
  }
  void setIdentifierInfo(IdentifierInfo *II) {
    PtrData = (void*) II;
  }

  const void *getEofData() const {
    assert(is(tok::eof));
    return reinterpret_cast<const void *>(PtrData);
  }
  void setEofData(const void *D) {
    assert(is(tok::eof));
    assert(!PtrData);
    PtrData = const_cast<void *>(D);
  }

  /// getRawIdentifier - For a raw identifier token (i.e., an identifier
  /// lexed in raw mode), returns a reference to the text substring in the
  /// buffer if known.
  StringRef getRawIdentifier() const {
    assert(is(tok::raw_identifier));
    return StringRef(reinterpret_cast<const char *>(PtrData), getLength());
  }
  void setRawIdentifierData(const char *Ptr) {
    assert(is(tok::raw_identifier));
    PtrData = const_cast<char*>(Ptr);
  }

  /// getLiteralData - For a literal token (numeric constant, string, etc), this
  /// returns a pointer to the start of it in the text buffer if known, null
  /// otherwise.
  const char *getLiteralData() const {
    assert(isLiteral() && "Cannot get literal data of non-literal");
    return reinterpret_cast<const char*>(PtrData);
  }
  void setLiteralData(const char *Ptr) {
    assert(isLiteral() && "Cannot set literal data of non-literal");
    PtrData = const_cast<char*>(Ptr);
  }

  void *getAnnotationValue() const {
    assert(isAnnotation() && "Used AnnotVal on non-annotation token");
    return PtrData;
  }
  void setAnnotationValue(void *val) {
    assert(isAnnotation() && "Used AnnotVal on non-annotation token");
    PtrData = val;
  }

  /// Set the specified flag.
  void setFlag(TokenFlags Flag) {
    Flags |= Flag;
  }

  /// Get the specified flag.
  bool getFlag(TokenFlags Flag) const {
    return (Flags & Flag) != 0;
  }

  /// Unset the specified flag.
  void clearFlag(TokenFlags Flag) {
    Flags &= ~Flag;
  }

  /// Return the internal represtation of the flags.
  ///
  /// This is only intended for low-level operations such as writing tokens to
  /// disk.
  unsigned getFlags() const {
    return Flags;
  }

  /// Set a flag to either true or false.
  void setFlagValue(TokenFlags Flag, bool Val) {
    if (Val)
      setFlag(Flag);
    else
      clearFlag(Flag);
  }

  /// isAtStartOfLine - Return true if this token is at the start of a line.
  ///
  bool isAtStartOfLine() const { return getFlag(StartOfLine); }

  /// Return true if this token has whitespace before it.
  ///
  bool hasLeadingSpace() const { return getFlag(LeadingSpace); }

  /// Return true if this identifier token should never
  /// be expanded in the future, due to C99 6.10.3.4p2.
  bool isExpandDisabled() const { return getFlag(DisableExpand); }

  /// Return true if we have an ObjC keyword identifier.
  bool isObjCAtKeyword(tok::ObjCKeywordKind objcKey) const;

  /// Return the ObjC keyword kind.
  tok::ObjCKeywordKind getObjCKeywordID() const;

  /// Return true if this token has trigraphs or escaped newlines in it.
  bool needsCleaning() const { return getFlag(NeedsCleaning); }

  /// Return true if this token has an empty macro before it.
  ///
  bool hasLeadingEmptyMacro() const { return getFlag(LeadingEmptyMacro); }

  /// Return true if this token is a string or character literal which
  /// has a ud-suffix.
  bool hasUDSuffix() const { return getFlag(HasUDSuffix); }

  /// Returns true if this token contains a universal character name.
  bool hasUCN() const { return getFlag(HasUCN); }

  /// Returns true if this token is formed by macro by stringizing or charizing
  /// operator.
  bool stringifiedInMacro() const { return getFlag(StringifiedInMacro); }

  /// Returns true if the comma after this token was elided.
  bool commaAfterElided() const { return getFlag(CommaAfterElided); }

  /// Returns true if this token is an editor placeholder.
  ///
  /// Editor placeholders are produced by the code-completion engine and are
  /// represented as characters between '<#' and '#>' in the source code. The
  /// lexer uses identifier tokens to represent placeholders.
  bool isEditorPlaceholder() const { return getFlag(IsEditorPlaceholder); }
};

/// Information about the conditional stack (\#if directives)
/// currently active.
struct PPConditionalInfo {
  /// Location where the conditional started.
  SourceLocation IfLoc;

  /// True if this was contained in a skipping directive, e.g.,
  /// in a "\#if 0" block.
  bool WasSkipping;

  /// True if we have emitted tokens already, and now we're in
  /// an \#else block or something.  Only useful in Skipping blocks.
  bool FoundNonSkip;

  /// True if we've seen a \#else in this block.  If so,
  /// \#elif/\#else directives are not allowed.
  bool FoundElse;
};

} // end namespace clang

namespace llvm {
  template <>
  struct isPodLike<clang::Token> { static const bool value = true; };
} // end namespace llvm

#endif // LLVM_CLANG_LEX_TOKEN_H
