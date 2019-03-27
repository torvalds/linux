//===- MILexer.cpp - Machine instructions lexer implementation ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the lexing of machine instructions.
//
//===----------------------------------------------------------------------===//

#include "MILexer.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <string>

using namespace llvm;

namespace {

using ErrorCallbackType =
    function_ref<void(StringRef::iterator Loc, const Twine &)>;

/// This class provides a way to iterate and get characters from the source
/// string.
class Cursor {
  const char *Ptr = nullptr;
  const char *End = nullptr;

public:
  Cursor(NoneType) {}

  explicit Cursor(StringRef Str) {
    Ptr = Str.data();
    End = Ptr + Str.size();
  }

  bool isEOF() const { return Ptr == End; }

  char peek(int I = 0) const { return End - Ptr <= I ? 0 : Ptr[I]; }

  void advance(unsigned I = 1) { Ptr += I; }

  StringRef remaining() const { return StringRef(Ptr, End - Ptr); }

  StringRef upto(Cursor C) const {
    assert(C.Ptr >= Ptr && C.Ptr <= End);
    return StringRef(Ptr, C.Ptr - Ptr);
  }

  StringRef::iterator location() const { return Ptr; }

  operator bool() const { return Ptr != nullptr; }
};

} // end anonymous namespace

MIToken &MIToken::reset(TokenKind Kind, StringRef Range) {
  this->Kind = Kind;
  this->Range = Range;
  return *this;
}

MIToken &MIToken::setStringValue(StringRef StrVal) {
  StringValue = StrVal;
  return *this;
}

MIToken &MIToken::setOwnedStringValue(std::string StrVal) {
  StringValueStorage = std::move(StrVal);
  StringValue = StringValueStorage;
  return *this;
}

MIToken &MIToken::setIntegerValue(APSInt IntVal) {
  this->IntVal = std::move(IntVal);
  return *this;
}

/// Skip the leading whitespace characters and return the updated cursor.
static Cursor skipWhitespace(Cursor C) {
  while (isblank(C.peek()))
    C.advance();
  return C;
}

static bool isNewlineChar(char C) { return C == '\n' || C == '\r'; }

/// Skip a line comment and return the updated cursor.
static Cursor skipComment(Cursor C) {
  if (C.peek() != ';')
    return C;
  while (!isNewlineChar(C.peek()) && !C.isEOF())
    C.advance();
  return C;
}

/// Return true if the given character satisfies the following regular
/// expression: [-a-zA-Z$._0-9]
static bool isIdentifierChar(char C) {
  return isalpha(C) || isdigit(C) || C == '_' || C == '-' || C == '.' ||
         C == '$';
}

/// Unescapes the given string value.
///
/// Expects the string value to be quoted.
static std::string unescapeQuotedString(StringRef Value) {
  assert(Value.front() == '"' && Value.back() == '"');
  Cursor C = Cursor(Value.substr(1, Value.size() - 2));

  std::string Str;
  Str.reserve(C.remaining().size());
  while (!C.isEOF()) {
    char Char = C.peek();
    if (Char == '\\') {
      if (C.peek(1) == '\\') {
        // Two '\' become one
        Str += '\\';
        C.advance(2);
        continue;
      }
      if (isxdigit(C.peek(1)) && isxdigit(C.peek(2))) {
        Str += hexDigitValue(C.peek(1)) * 16 + hexDigitValue(C.peek(2));
        C.advance(3);
        continue;
      }
    }
    Str += Char;
    C.advance();
  }
  return Str;
}

/// Lex a string constant using the following regular expression: \"[^\"]*\"
static Cursor lexStringConstant(Cursor C, ErrorCallbackType ErrorCallback) {
  assert(C.peek() == '"');
  for (C.advance(); C.peek() != '"'; C.advance()) {
    if (C.isEOF() || isNewlineChar(C.peek())) {
      ErrorCallback(
          C.location(),
          "end of machine instruction reached before the closing '\"'");
      return None;
    }
  }
  C.advance();
  return C;
}

static Cursor lexName(Cursor C, MIToken &Token, MIToken::TokenKind Type,
                      unsigned PrefixLength, ErrorCallbackType ErrorCallback) {
  auto Range = C;
  C.advance(PrefixLength);
  if (C.peek() == '"') {
    if (Cursor R = lexStringConstant(C, ErrorCallback)) {
      StringRef String = Range.upto(R);
      Token.reset(Type, String)
          .setOwnedStringValue(
              unescapeQuotedString(String.drop_front(PrefixLength)));
      return R;
    }
    Token.reset(MIToken::Error, Range.remaining());
    return Range;
  }
  while (isIdentifierChar(C.peek()))
    C.advance();
  Token.reset(Type, Range.upto(C))
      .setStringValue(Range.upto(C).drop_front(PrefixLength));
  return C;
}

static MIToken::TokenKind getIdentifierKind(StringRef Identifier) {
  return StringSwitch<MIToken::TokenKind>(Identifier)
      .Case("_", MIToken::underscore)
      .Case("implicit", MIToken::kw_implicit)
      .Case("implicit-def", MIToken::kw_implicit_define)
      .Case("def", MIToken::kw_def)
      .Case("dead", MIToken::kw_dead)
      .Case("killed", MIToken::kw_killed)
      .Case("undef", MIToken::kw_undef)
      .Case("internal", MIToken::kw_internal)
      .Case("early-clobber", MIToken::kw_early_clobber)
      .Case("debug-use", MIToken::kw_debug_use)
      .Case("renamable", MIToken::kw_renamable)
      .Case("tied-def", MIToken::kw_tied_def)
      .Case("frame-setup", MIToken::kw_frame_setup)
      .Case("frame-destroy", MIToken::kw_frame_destroy)
      .Case("nnan", MIToken::kw_nnan)
      .Case("ninf", MIToken::kw_ninf)
      .Case("nsz", MIToken::kw_nsz)
      .Case("arcp", MIToken::kw_arcp)
      .Case("contract", MIToken::kw_contract)
      .Case("afn", MIToken::kw_afn)
      .Case("reassoc", MIToken::kw_reassoc)
      .Case("nuw" , MIToken::kw_nuw)
      .Case("nsw" , MIToken::kw_nsw)
      .Case("exact" , MIToken::kw_exact)
      .Case("debug-location", MIToken::kw_debug_location)
      .Case("same_value", MIToken::kw_cfi_same_value)
      .Case("offset", MIToken::kw_cfi_offset)
      .Case("rel_offset", MIToken::kw_cfi_rel_offset)
      .Case("def_cfa_register", MIToken::kw_cfi_def_cfa_register)
      .Case("def_cfa_offset", MIToken::kw_cfi_def_cfa_offset)
      .Case("adjust_cfa_offset", MIToken::kw_cfi_adjust_cfa_offset)
      .Case("escape", MIToken::kw_cfi_escape)
      .Case("def_cfa", MIToken::kw_cfi_def_cfa)
      .Case("remember_state", MIToken::kw_cfi_remember_state)
      .Case("restore", MIToken::kw_cfi_restore)
      .Case("restore_state", MIToken::kw_cfi_restore_state)
      .Case("undefined", MIToken::kw_cfi_undefined)
      .Case("register", MIToken::kw_cfi_register)
      .Case("window_save", MIToken::kw_cfi_window_save)
      .Case("negate_ra_sign_state", MIToken::kw_cfi_aarch64_negate_ra_sign_state)
      .Case("blockaddress", MIToken::kw_blockaddress)
      .Case("intrinsic", MIToken::kw_intrinsic)
      .Case("target-index", MIToken::kw_target_index)
      .Case("half", MIToken::kw_half)
      .Case("float", MIToken::kw_float)
      .Case("double", MIToken::kw_double)
      .Case("x86_fp80", MIToken::kw_x86_fp80)
      .Case("fp128", MIToken::kw_fp128)
      .Case("ppc_fp128", MIToken::kw_ppc_fp128)
      .Case("target-flags", MIToken::kw_target_flags)
      .Case("volatile", MIToken::kw_volatile)
      .Case("non-temporal", MIToken::kw_non_temporal)
      .Case("dereferenceable", MIToken::kw_dereferenceable)
      .Case("invariant", MIToken::kw_invariant)
      .Case("align", MIToken::kw_align)
      .Case("addrspace", MIToken::kw_addrspace)
      .Case("stack", MIToken::kw_stack)
      .Case("got", MIToken::kw_got)
      .Case("jump-table", MIToken::kw_jump_table)
      .Case("constant-pool", MIToken::kw_constant_pool)
      .Case("call-entry", MIToken::kw_call_entry)
      .Case("liveout", MIToken::kw_liveout)
      .Case("address-taken", MIToken::kw_address_taken)
      .Case("landing-pad", MIToken::kw_landing_pad)
      .Case("liveins", MIToken::kw_liveins)
      .Case("successors", MIToken::kw_successors)
      .Case("floatpred", MIToken::kw_floatpred)
      .Case("intpred", MIToken::kw_intpred)
      .Case("pre-instr-symbol", MIToken::kw_pre_instr_symbol)
      .Case("post-instr-symbol", MIToken::kw_post_instr_symbol)
      .Case("unknown-size", MIToken::kw_unknown_size)
      .Default(MIToken::Identifier);
}

static Cursor maybeLexIdentifier(Cursor C, MIToken &Token) {
  if (!isalpha(C.peek()) && C.peek() != '_')
    return None;
  auto Range = C;
  while (isIdentifierChar(C.peek()))
    C.advance();
  auto Identifier = Range.upto(C);
  Token.reset(getIdentifierKind(Identifier), Identifier)
      .setStringValue(Identifier);
  return C;
}

static Cursor maybeLexMachineBasicBlock(Cursor C, MIToken &Token,
                                        ErrorCallbackType ErrorCallback) {
  bool IsReference = C.remaining().startswith("%bb.");
  if (!IsReference && !C.remaining().startswith("bb."))
    return None;
  auto Range = C;
  unsigned PrefixLength = IsReference ? 4 : 3;
  C.advance(PrefixLength); // Skip '%bb.' or 'bb.'
  if (!isdigit(C.peek())) {
    Token.reset(MIToken::Error, C.remaining());
    ErrorCallback(C.location(), "expected a number after '%bb.'");
    return C;
  }
  auto NumberRange = C;
  while (isdigit(C.peek()))
    C.advance();
  StringRef Number = NumberRange.upto(C);
  unsigned StringOffset = PrefixLength + Number.size(); // Drop '%bb.<id>'
  // TODO: The format bb.<id>.<irname> is supported only when it's not a
  // reference. Once we deprecate the format where the irname shows up, we
  // should only lex forward if it is a reference.
  if (C.peek() == '.') {
    C.advance(); // Skip '.'
    ++StringOffset;
    while (isIdentifierChar(C.peek()))
      C.advance();
  }
  Token.reset(IsReference ? MIToken::MachineBasicBlock
                          : MIToken::MachineBasicBlockLabel,
              Range.upto(C))
      .setIntegerValue(APSInt(Number))
      .setStringValue(Range.upto(C).drop_front(StringOffset));
  return C;
}

static Cursor maybeLexIndex(Cursor C, MIToken &Token, StringRef Rule,
                            MIToken::TokenKind Kind) {
  if (!C.remaining().startswith(Rule) || !isdigit(C.peek(Rule.size())))
    return None;
  auto Range = C;
  C.advance(Rule.size());
  auto NumberRange = C;
  while (isdigit(C.peek()))
    C.advance();
  Token.reset(Kind, Range.upto(C)).setIntegerValue(APSInt(NumberRange.upto(C)));
  return C;
}

static Cursor maybeLexIndexAndName(Cursor C, MIToken &Token, StringRef Rule,
                                   MIToken::TokenKind Kind) {
  if (!C.remaining().startswith(Rule) || !isdigit(C.peek(Rule.size())))
    return None;
  auto Range = C;
  C.advance(Rule.size());
  auto NumberRange = C;
  while (isdigit(C.peek()))
    C.advance();
  StringRef Number = NumberRange.upto(C);
  unsigned StringOffset = Rule.size() + Number.size();
  if (C.peek() == '.') {
    C.advance();
    ++StringOffset;
    while (isIdentifierChar(C.peek()))
      C.advance();
  }
  Token.reset(Kind, Range.upto(C))
      .setIntegerValue(APSInt(Number))
      .setStringValue(Range.upto(C).drop_front(StringOffset));
  return C;
}

static Cursor maybeLexJumpTableIndex(Cursor C, MIToken &Token) {
  return maybeLexIndex(C, Token, "%jump-table.", MIToken::JumpTableIndex);
}

static Cursor maybeLexStackObject(Cursor C, MIToken &Token) {
  return maybeLexIndexAndName(C, Token, "%stack.", MIToken::StackObject);
}

static Cursor maybeLexFixedStackObject(Cursor C, MIToken &Token) {
  return maybeLexIndex(C, Token, "%fixed-stack.", MIToken::FixedStackObject);
}

static Cursor maybeLexConstantPoolItem(Cursor C, MIToken &Token) {
  return maybeLexIndex(C, Token, "%const.", MIToken::ConstantPoolItem);
}

static Cursor maybeLexSubRegisterIndex(Cursor C, MIToken &Token,
                                       ErrorCallbackType ErrorCallback) {
  const StringRef Rule = "%subreg.";
  if (!C.remaining().startswith(Rule))
    return None;
  return lexName(C, Token, MIToken::SubRegisterIndex, Rule.size(),
                 ErrorCallback);
}

static Cursor maybeLexIRBlock(Cursor C, MIToken &Token,
                              ErrorCallbackType ErrorCallback) {
  const StringRef Rule = "%ir-block.";
  if (!C.remaining().startswith(Rule))
    return None;
  if (isdigit(C.peek(Rule.size())))
    return maybeLexIndex(C, Token, Rule, MIToken::IRBlock);
  return lexName(C, Token, MIToken::NamedIRBlock, Rule.size(), ErrorCallback);
}

static Cursor maybeLexIRValue(Cursor C, MIToken &Token,
                              ErrorCallbackType ErrorCallback) {
  const StringRef Rule = "%ir.";
  if (!C.remaining().startswith(Rule))
    return None;
  if (isdigit(C.peek(Rule.size())))
    return maybeLexIndex(C, Token, Rule, MIToken::IRValue);
  return lexName(C, Token, MIToken::NamedIRValue, Rule.size(), ErrorCallback);
}

static Cursor maybeLexStringConstant(Cursor C, MIToken &Token,
                                     ErrorCallbackType ErrorCallback) {
  if (C.peek() != '"')
    return None;
  return lexName(C, Token, MIToken::StringConstant, /*PrefixLength=*/0,
                 ErrorCallback);
}

static Cursor lexVirtualRegister(Cursor C, MIToken &Token) {
  auto Range = C;
  C.advance(); // Skip '%'
  auto NumberRange = C;
  while (isdigit(C.peek()))
    C.advance();
  Token.reset(MIToken::VirtualRegister, Range.upto(C))
      .setIntegerValue(APSInt(NumberRange.upto(C)));
  return C;
}

/// Returns true for a character allowed in a register name.
static bool isRegisterChar(char C) {
  return isIdentifierChar(C) && C != '.';
}

static Cursor lexNamedVirtualRegister(Cursor C, MIToken &Token) {
  Cursor Range = C;
  C.advance(); // Skip '%'
  while (isRegisterChar(C.peek()))
    C.advance();
  Token.reset(MIToken::NamedVirtualRegister, Range.upto(C))
      .setStringValue(Range.upto(C).drop_front(1)); // Drop the '%'
  return C;
}

static Cursor maybeLexRegister(Cursor C, MIToken &Token,
                               ErrorCallbackType ErrorCallback) {
  if (C.peek() != '%' && C.peek() != '$')
    return None;

  if (C.peek() == '%') {
    if (isdigit(C.peek(1)))
      return lexVirtualRegister(C, Token);

    if (isRegisterChar(C.peek(1)))
      return lexNamedVirtualRegister(C, Token);

    return None;
  }

  assert(C.peek() == '$');
  auto Range = C;
  C.advance(); // Skip '$'
  while (isRegisterChar(C.peek()))
    C.advance();
  Token.reset(MIToken::NamedRegister, Range.upto(C))
      .setStringValue(Range.upto(C).drop_front(1)); // Drop the '$'
  return C;
}

static Cursor maybeLexGlobalValue(Cursor C, MIToken &Token,
                                  ErrorCallbackType ErrorCallback) {
  if (C.peek() != '@')
    return None;
  if (!isdigit(C.peek(1)))
    return lexName(C, Token, MIToken::NamedGlobalValue, /*PrefixLength=*/1,
                   ErrorCallback);
  auto Range = C;
  C.advance(1); // Skip the '@'
  auto NumberRange = C;
  while (isdigit(C.peek()))
    C.advance();
  Token.reset(MIToken::GlobalValue, Range.upto(C))
      .setIntegerValue(APSInt(NumberRange.upto(C)));
  return C;
}

static Cursor maybeLexExternalSymbol(Cursor C, MIToken &Token,
                                     ErrorCallbackType ErrorCallback) {
  if (C.peek() != '&')
    return None;
  return lexName(C, Token, MIToken::ExternalSymbol, /*PrefixLength=*/1,
                 ErrorCallback);
}

static Cursor maybeLexMCSymbol(Cursor C, MIToken &Token,
                               ErrorCallbackType ErrorCallback) {
  const StringRef Rule = "<mcsymbol ";
  if (!C.remaining().startswith(Rule))
    return None;
  auto Start = C;
  C.advance(Rule.size());

  // Try a simple unquoted name.
  if (C.peek() != '"') {
    while (isIdentifierChar(C.peek()))
      C.advance();
    StringRef String = Start.upto(C).drop_front(Rule.size());
    if (C.peek() != '>') {
      ErrorCallback(C.location(),
                    "expected the '<mcsymbol ...' to be closed by a '>'");
      Token.reset(MIToken::Error, Start.remaining());
      return Start;
    }
    C.advance();

    Token.reset(MIToken::MCSymbol, Start.upto(C)).setStringValue(String);
    return C;
  }

  // Otherwise lex out a quoted name.
  Cursor R = lexStringConstant(C, ErrorCallback);
  if (!R) {
    ErrorCallback(C.location(),
                  "unable to parse quoted string from opening quote");
    Token.reset(MIToken::Error, Start.remaining());
    return Start;
  }
  StringRef String = Start.upto(R).drop_front(Rule.size());
  if (R.peek() != '>') {
    ErrorCallback(R.location(),
                  "expected the '<mcsymbol ...' to be closed by a '>'");
    Token.reset(MIToken::Error, Start.remaining());
    return Start;
  }
  R.advance();

  Token.reset(MIToken::MCSymbol, Start.upto(R))
      .setOwnedStringValue(unescapeQuotedString(String));
  return R;
}

static bool isValidHexFloatingPointPrefix(char C) {
  return C == 'H' || C == 'K' || C == 'L' || C == 'M';
}

static Cursor lexFloatingPointLiteral(Cursor Range, Cursor C, MIToken &Token) {
  C.advance();
  // Skip over [0-9]*([eE][-+]?[0-9]+)?
  while (isdigit(C.peek()))
    C.advance();
  if ((C.peek() == 'e' || C.peek() == 'E') &&
      (isdigit(C.peek(1)) ||
       ((C.peek(1) == '-' || C.peek(1) == '+') && isdigit(C.peek(2))))) {
    C.advance(2);
    while (isdigit(C.peek()))
      C.advance();
  }
  Token.reset(MIToken::FloatingPointLiteral, Range.upto(C));
  return C;
}

static Cursor maybeLexHexadecimalLiteral(Cursor C, MIToken &Token) {
  if (C.peek() != '0' || (C.peek(1) != 'x' && C.peek(1) != 'X'))
    return None;
  Cursor Range = C;
  C.advance(2);
  unsigned PrefLen = 2;
  if (isValidHexFloatingPointPrefix(C.peek())) {
    C.advance();
    PrefLen++;
  }
  while (isxdigit(C.peek()))
    C.advance();
  StringRef StrVal = Range.upto(C);
  if (StrVal.size() <= PrefLen)
    return None;
  if (PrefLen == 2)
    Token.reset(MIToken::HexLiteral, Range.upto(C));
  else // It must be 3, which means that there was a floating-point prefix.
    Token.reset(MIToken::FloatingPointLiteral, Range.upto(C));
  return C;
}

static Cursor maybeLexNumericalLiteral(Cursor C, MIToken &Token) {
  if (!isdigit(C.peek()) && (C.peek() != '-' || !isdigit(C.peek(1))))
    return None;
  auto Range = C;
  C.advance();
  while (isdigit(C.peek()))
    C.advance();
  if (C.peek() == '.')
    return lexFloatingPointLiteral(Range, C, Token);
  StringRef StrVal = Range.upto(C);
  Token.reset(MIToken::IntegerLiteral, StrVal).setIntegerValue(APSInt(StrVal));
  return C;
}

static MIToken::TokenKind getMetadataKeywordKind(StringRef Identifier) {
  return StringSwitch<MIToken::TokenKind>(Identifier)
      .Case("!tbaa", MIToken::md_tbaa)
      .Case("!alias.scope", MIToken::md_alias_scope)
      .Case("!noalias", MIToken::md_noalias)
      .Case("!range", MIToken::md_range)
      .Case("!DIExpression", MIToken::md_diexpr)
      .Case("!DILocation", MIToken::md_dilocation)
      .Default(MIToken::Error);
}

static Cursor maybeLexExlaim(Cursor C, MIToken &Token,
                             ErrorCallbackType ErrorCallback) {
  if (C.peek() != '!')
    return None;
  auto Range = C;
  C.advance(1);
  if (isdigit(C.peek()) || !isIdentifierChar(C.peek())) {
    Token.reset(MIToken::exclaim, Range.upto(C));
    return C;
  }
  while (isIdentifierChar(C.peek()))
    C.advance();
  StringRef StrVal = Range.upto(C);
  Token.reset(getMetadataKeywordKind(StrVal), StrVal);
  if (Token.isError())
    ErrorCallback(Token.location(),
                  "use of unknown metadata keyword '" + StrVal + "'");
  return C;
}

static MIToken::TokenKind symbolToken(char C) {
  switch (C) {
  case ',':
    return MIToken::comma;
  case '.':
    return MIToken::dot;
  case '=':
    return MIToken::equal;
  case ':':
    return MIToken::colon;
  case '(':
    return MIToken::lparen;
  case ')':
    return MIToken::rparen;
  case '{':
    return MIToken::lbrace;
  case '}':
    return MIToken::rbrace;
  case '+':
    return MIToken::plus;
  case '-':
    return MIToken::minus;
  case '<':
    return MIToken::less;
  case '>':
    return MIToken::greater;
  default:
    return MIToken::Error;
  }
}

static Cursor maybeLexSymbol(Cursor C, MIToken &Token) {
  MIToken::TokenKind Kind;
  unsigned Length = 1;
  if (C.peek() == ':' && C.peek(1) == ':') {
    Kind = MIToken::coloncolon;
    Length = 2;
  } else
    Kind = symbolToken(C.peek());
  if (Kind == MIToken::Error)
    return None;
  auto Range = C;
  C.advance(Length);
  Token.reset(Kind, Range.upto(C));
  return C;
}

static Cursor maybeLexNewline(Cursor C, MIToken &Token) {
  if (!isNewlineChar(C.peek()))
    return None;
  auto Range = C;
  C.advance();
  Token.reset(MIToken::Newline, Range.upto(C));
  return C;
}

static Cursor maybeLexEscapedIRValue(Cursor C, MIToken &Token,
                                     ErrorCallbackType ErrorCallback) {
  if (C.peek() != '`')
    return None;
  auto Range = C;
  C.advance();
  auto StrRange = C;
  while (C.peek() != '`') {
    if (C.isEOF() || isNewlineChar(C.peek())) {
      ErrorCallback(
          C.location(),
          "end of machine instruction reached before the closing '`'");
      Token.reset(MIToken::Error, Range.remaining());
      return C;
    }
    C.advance();
  }
  StringRef Value = StrRange.upto(C);
  C.advance();
  Token.reset(MIToken::QuotedIRValue, Range.upto(C)).setStringValue(Value);
  return C;
}

StringRef llvm::lexMIToken(StringRef Source, MIToken &Token,
                           ErrorCallbackType ErrorCallback) {
  auto C = skipComment(skipWhitespace(Cursor(Source)));
  if (C.isEOF()) {
    Token.reset(MIToken::Eof, C.remaining());
    return C.remaining();
  }

  if (Cursor R = maybeLexMachineBasicBlock(C, Token, ErrorCallback))
    return R.remaining();
  if (Cursor R = maybeLexIdentifier(C, Token))
    return R.remaining();
  if (Cursor R = maybeLexJumpTableIndex(C, Token))
    return R.remaining();
  if (Cursor R = maybeLexStackObject(C, Token))
    return R.remaining();
  if (Cursor R = maybeLexFixedStackObject(C, Token))
    return R.remaining();
  if (Cursor R = maybeLexConstantPoolItem(C, Token))
    return R.remaining();
  if (Cursor R = maybeLexSubRegisterIndex(C, Token, ErrorCallback))
    return R.remaining();
  if (Cursor R = maybeLexIRBlock(C, Token, ErrorCallback))
    return R.remaining();
  if (Cursor R = maybeLexIRValue(C, Token, ErrorCallback))
    return R.remaining();
  if (Cursor R = maybeLexRegister(C, Token, ErrorCallback))
    return R.remaining();
  if (Cursor R = maybeLexGlobalValue(C, Token, ErrorCallback))
    return R.remaining();
  if (Cursor R = maybeLexExternalSymbol(C, Token, ErrorCallback))
    return R.remaining();
  if (Cursor R = maybeLexMCSymbol(C, Token, ErrorCallback))
    return R.remaining();
  if (Cursor R = maybeLexHexadecimalLiteral(C, Token))
    return R.remaining();
  if (Cursor R = maybeLexNumericalLiteral(C, Token))
    return R.remaining();
  if (Cursor R = maybeLexExlaim(C, Token, ErrorCallback))
    return R.remaining();
  if (Cursor R = maybeLexSymbol(C, Token))
    return R.remaining();
  if (Cursor R = maybeLexNewline(C, Token))
    return R.remaining();
  if (Cursor R = maybeLexEscapedIRValue(C, Token, ErrorCallback))
    return R.remaining();
  if (Cursor R = maybeLexStringConstant(C, Token, ErrorCallback))
    return R.remaining();

  Token.reset(MIToken::Error, C.remaining());
  ErrorCallback(C.location(),
                Twine("unexpected character '") + Twine(C.peek()) + "'");
  return C.remaining();
}
