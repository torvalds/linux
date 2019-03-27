//=== JSON.cpp - JSON value, parsing and serialization - C++ -----------*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===---------------------------------------------------------------------===//

#include "llvm/Support/JSON.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Format.h"
#include <cctype>

namespace llvm {
namespace json {

Value &Object::operator[](const ObjectKey &K) {
  return try_emplace(K, nullptr).first->getSecond();
}
Value &Object::operator[](ObjectKey &&K) {
  return try_emplace(std::move(K), nullptr).first->getSecond();
}
Value *Object::get(StringRef K) {
  auto I = find(K);
  if (I == end())
    return nullptr;
  return &I->second;
}
const Value *Object::get(StringRef K) const {
  auto I = find(K);
  if (I == end())
    return nullptr;
  return &I->second;
}
llvm::Optional<std::nullptr_t> Object::getNull(StringRef K) const {
  if (auto *V = get(K))
    return V->getAsNull();
  return llvm::None;
}
llvm::Optional<bool> Object::getBoolean(StringRef K) const {
  if (auto *V = get(K))
    return V->getAsBoolean();
  return llvm::None;
}
llvm::Optional<double> Object::getNumber(StringRef K) const {
  if (auto *V = get(K))
    return V->getAsNumber();
  return llvm::None;
}
llvm::Optional<int64_t> Object::getInteger(StringRef K) const {
  if (auto *V = get(K))
    return V->getAsInteger();
  return llvm::None;
}
llvm::Optional<llvm::StringRef> Object::getString(StringRef K) const {
  if (auto *V = get(K))
    return V->getAsString();
  return llvm::None;
}
const json::Object *Object::getObject(StringRef K) const {
  if (auto *V = get(K))
    return V->getAsObject();
  return nullptr;
}
json::Object *Object::getObject(StringRef K) {
  if (auto *V = get(K))
    return V->getAsObject();
  return nullptr;
}
const json::Array *Object::getArray(StringRef K) const {
  if (auto *V = get(K))
    return V->getAsArray();
  return nullptr;
}
json::Array *Object::getArray(StringRef K) {
  if (auto *V = get(K))
    return V->getAsArray();
  return nullptr;
}
bool operator==(const Object &LHS, const Object &RHS) {
  if (LHS.size() != RHS.size())
    return false;
  for (const auto &L : LHS) {
    auto R = RHS.find(L.first);
    if (R == RHS.end() || L.second != R->second)
      return false;
  }
  return true;
}

Array::Array(std::initializer_list<Value> Elements) {
  V.reserve(Elements.size());
  for (const Value &V : Elements) {
    emplace_back(nullptr);
    back().moveFrom(std::move(V));
  }
}

Value::Value(std::initializer_list<Value> Elements)
    : Value(json::Array(Elements)) {}

void Value::copyFrom(const Value &M) {
  Type = M.Type;
  switch (Type) {
  case T_Null:
  case T_Boolean:
  case T_Double:
  case T_Integer:
    memcpy(Union.buffer, M.Union.buffer, sizeof(Union.buffer));
    break;
  case T_StringRef:
    create<StringRef>(M.as<StringRef>());
    break;
  case T_String:
    create<std::string>(M.as<std::string>());
    break;
  case T_Object:
    create<json::Object>(M.as<json::Object>());
    break;
  case T_Array:
    create<json::Array>(M.as<json::Array>());
    break;
  }
}

void Value::moveFrom(const Value &&M) {
  Type = M.Type;
  switch (Type) {
  case T_Null:
  case T_Boolean:
  case T_Double:
  case T_Integer:
    memcpy(Union.buffer, M.Union.buffer, sizeof(Union.buffer));
    break;
  case T_StringRef:
    create<StringRef>(M.as<StringRef>());
    break;
  case T_String:
    create<std::string>(std::move(M.as<std::string>()));
    M.Type = T_Null;
    break;
  case T_Object:
    create<json::Object>(std::move(M.as<json::Object>()));
    M.Type = T_Null;
    break;
  case T_Array:
    create<json::Array>(std::move(M.as<json::Array>()));
    M.Type = T_Null;
    break;
  }
}

void Value::destroy() {
  switch (Type) {
  case T_Null:
  case T_Boolean:
  case T_Double:
  case T_Integer:
    break;
  case T_StringRef:
    as<StringRef>().~StringRef();
    break;
  case T_String:
    as<std::string>().~basic_string();
    break;
  case T_Object:
    as<json::Object>().~Object();
    break;
  case T_Array:
    as<json::Array>().~Array();
    break;
  }
}

bool operator==(const Value &L, const Value &R) {
  if (L.kind() != R.kind())
    return false;
  switch (L.kind()) {
  case Value::Null:
    return *L.getAsNull() == *R.getAsNull();
  case Value::Boolean:
    return *L.getAsBoolean() == *R.getAsBoolean();
  case Value::Number:
    // Workaround for https://gcc.gnu.org/bugzilla/show_bug.cgi?id=323
    // The same integer must convert to the same double, per the standard.
    // However we see 64-vs-80-bit precision comparisons with gcc-7 -O3 -m32.
    // So we avoid floating point promotion for exact comparisons.
    if (L.Type == Value::T_Integer || R.Type == Value::T_Integer)
      return L.getAsInteger() == R.getAsInteger();
    return *L.getAsNumber() == *R.getAsNumber();
  case Value::String:
    return *L.getAsString() == *R.getAsString();
  case Value::Array:
    return *L.getAsArray() == *R.getAsArray();
  case Value::Object:
    return *L.getAsObject() == *R.getAsObject();
  }
  llvm_unreachable("Unknown value kind");
}

namespace {
// Simple recursive-descent JSON parser.
class Parser {
public:
  Parser(StringRef JSON)
      : Start(JSON.begin()), P(JSON.begin()), End(JSON.end()) {}

  bool checkUTF8() {
    size_t ErrOffset;
    if (isUTF8(StringRef(Start, End - Start), &ErrOffset))
      return true;
    P = Start + ErrOffset; // For line/column calculation.
    return parseError("Invalid UTF-8 sequence");
  }

  bool parseValue(Value &Out);

  bool assertEnd() {
    eatWhitespace();
    if (P == End)
      return true;
    return parseError("Text after end of document");
  }

  Error takeError() {
    assert(Err);
    return std::move(*Err);
  }

private:
  void eatWhitespace() {
    while (P != End && (*P == ' ' || *P == '\r' || *P == '\n' || *P == '\t'))
      ++P;
  }

  // On invalid syntax, parseX() functions return false and set Err.
  bool parseNumber(char First, Value &Out);
  bool parseString(std::string &Out);
  bool parseUnicode(std::string &Out);
  bool parseError(const char *Msg); // always returns false

  char next() { return P == End ? 0 : *P++; }
  char peek() { return P == End ? 0 : *P; }
  static bool isNumber(char C) {
    return C == '0' || C == '1' || C == '2' || C == '3' || C == '4' ||
           C == '5' || C == '6' || C == '7' || C == '8' || C == '9' ||
           C == 'e' || C == 'E' || C == '+' || C == '-' || C == '.';
  }

  Optional<Error> Err;
  const char *Start, *P, *End;
};

bool Parser::parseValue(Value &Out) {
  eatWhitespace();
  if (P == End)
    return parseError("Unexpected EOF");
  switch (char C = next()) {
  // Bare null/true/false are easy - first char identifies them.
  case 'n':
    Out = nullptr;
    return (next() == 'u' && next() == 'l' && next() == 'l') ||
           parseError("Invalid JSON value (null?)");
  case 't':
    Out = true;
    return (next() == 'r' && next() == 'u' && next() == 'e') ||
           parseError("Invalid JSON value (true?)");
  case 'f':
    Out = false;
    return (next() == 'a' && next() == 'l' && next() == 's' && next() == 'e') ||
           parseError("Invalid JSON value (false?)");
  case '"': {
    std::string S;
    if (parseString(S)) {
      Out = std::move(S);
      return true;
    }
    return false;
  }
  case '[': {
    Out = Array{};
    Array &A = *Out.getAsArray();
    eatWhitespace();
    if (peek() == ']') {
      ++P;
      return true;
    }
    for (;;) {
      A.emplace_back(nullptr);
      if (!parseValue(A.back()))
        return false;
      eatWhitespace();
      switch (next()) {
      case ',':
        eatWhitespace();
        continue;
      case ']':
        return true;
      default:
        return parseError("Expected , or ] after array element");
      }
    }
  }
  case '{': {
    Out = Object{};
    Object &O = *Out.getAsObject();
    eatWhitespace();
    if (peek() == '}') {
      ++P;
      return true;
    }
    for (;;) {
      if (next() != '"')
        return parseError("Expected object key");
      std::string K;
      if (!parseString(K))
        return false;
      eatWhitespace();
      if (next() != ':')
        return parseError("Expected : after object key");
      eatWhitespace();
      if (!parseValue(O[std::move(K)]))
        return false;
      eatWhitespace();
      switch (next()) {
      case ',':
        eatWhitespace();
        continue;
      case '}':
        return true;
      default:
        return parseError("Expected , or } after object property");
      }
    }
  }
  default:
    if (isNumber(C))
      return parseNumber(C, Out);
    return parseError("Invalid JSON value");
  }
}

bool Parser::parseNumber(char First, Value &Out) {
  // Read the number into a string. (Must be null-terminated for strto*).
  SmallString<24> S;
  S.push_back(First);
  while (isNumber(peek()))
    S.push_back(next());
  char *End;
  // Try first to parse as integer, and if so preserve full 64 bits.
  // strtoll returns long long >= 64 bits, so check it's in range too.
  auto I = std::strtoll(S.c_str(), &End, 10);
  if (End == S.end() && I >= std::numeric_limits<int64_t>::min() &&
      I <= std::numeric_limits<int64_t>::max()) {
    Out = int64_t(I);
    return true;
  }
  // If it's not an integer
  Out = std::strtod(S.c_str(), &End);
  return End == S.end() || parseError("Invalid JSON value (number?)");
}

bool Parser::parseString(std::string &Out) {
  // leading quote was already consumed.
  for (char C = next(); C != '"'; C = next()) {
    if (LLVM_UNLIKELY(P == End))
      return parseError("Unterminated string");
    if (LLVM_UNLIKELY((C & 0x1f) == C))
      return parseError("Control character in string");
    if (LLVM_LIKELY(C != '\\')) {
      Out.push_back(C);
      continue;
    }
    // Handle escape sequence.
    switch (C = next()) {
    case '"':
    case '\\':
    case '/':
      Out.push_back(C);
      break;
    case 'b':
      Out.push_back('\b');
      break;
    case 'f':
      Out.push_back('\f');
      break;
    case 'n':
      Out.push_back('\n');
      break;
    case 'r':
      Out.push_back('\r');
      break;
    case 't':
      Out.push_back('\t');
      break;
    case 'u':
      if (!parseUnicode(Out))
        return false;
      break;
    default:
      return parseError("Invalid escape sequence");
    }
  }
  return true;
}

static void encodeUtf8(uint32_t Rune, std::string &Out) {
  if (Rune < 0x80) {
    Out.push_back(Rune & 0x7F);
  } else if (Rune < 0x800) {
    uint8_t FirstByte = 0xC0 | ((Rune & 0x7C0) >> 6);
    uint8_t SecondByte = 0x80 | (Rune & 0x3F);
    Out.push_back(FirstByte);
    Out.push_back(SecondByte);
  } else if (Rune < 0x10000) {
    uint8_t FirstByte = 0xE0 | ((Rune & 0xF000) >> 12);
    uint8_t SecondByte = 0x80 | ((Rune & 0xFC0) >> 6);
    uint8_t ThirdByte = 0x80 | (Rune & 0x3F);
    Out.push_back(FirstByte);
    Out.push_back(SecondByte);
    Out.push_back(ThirdByte);
  } else if (Rune < 0x110000) {
    uint8_t FirstByte = 0xF0 | ((Rune & 0x1F0000) >> 18);
    uint8_t SecondByte = 0x80 | ((Rune & 0x3F000) >> 12);
    uint8_t ThirdByte = 0x80 | ((Rune & 0xFC0) >> 6);
    uint8_t FourthByte = 0x80 | (Rune & 0x3F);
    Out.push_back(FirstByte);
    Out.push_back(SecondByte);
    Out.push_back(ThirdByte);
    Out.push_back(FourthByte);
  } else {
    llvm_unreachable("Invalid codepoint");
  }
}

// Parse a UTF-16 \uNNNN escape sequence. "\u" has already been consumed.
// May parse several sequential escapes to ensure proper surrogate handling.
// We do not use ConvertUTF.h, it can't accept and replace unpaired surrogates.
// These are invalid Unicode but valid JSON (RFC 8259, section 8.2).
bool Parser::parseUnicode(std::string &Out) {
  // Invalid UTF is not a JSON error (RFC 8529§8.2). It gets replaced by U+FFFD.
  auto Invalid = [&] { Out.append(/* UTF-8 */ {'\xef', '\xbf', '\xbd'}); };
  // Decodes 4 hex digits from the stream into Out, returns false on error.
  auto Parse4Hex = [this](uint16_t &Out) -> bool {
    Out = 0;
    char Bytes[] = {next(), next(), next(), next()};
    for (unsigned char C : Bytes) {
      if (!std::isxdigit(C))
        return parseError("Invalid \\u escape sequence");
      Out <<= 4;
      Out |= (C > '9') ? (C & ~0x20) - 'A' + 10 : (C - '0');
    }
    return true;
  };
  uint16_t First; // UTF-16 code unit from the first \u escape.
  if (!Parse4Hex(First))
    return false;

  // We loop to allow proper surrogate-pair error handling.
  while (true) {
    // Case 1: the UTF-16 code unit is already a codepoint in the BMP.
    if (LLVM_LIKELY(First < 0xD800 || First >= 0xE000)) {
      encodeUtf8(First, Out);
      return true;
    }

    // Case 2: it's an (unpaired) trailing surrogate.
    if (LLVM_UNLIKELY(First >= 0xDC00)) {
      Invalid();
      return true;
    }

    // Case 3: it's a leading surrogate. We expect a trailing one next.
    // Case 3a: there's no trailing \u escape. Don't advance in the stream.
    if (LLVM_UNLIKELY(P + 2 > End || *P != '\\' || *(P + 1) != 'u')) {
      Invalid(); // Leading surrogate was unpaired.
      return true;
    }
    P += 2;
    uint16_t Second;
    if (!Parse4Hex(Second))
      return false;
    // Case 3b: there was another \u escape, but it wasn't a trailing surrogate.
    if (LLVM_UNLIKELY(Second < 0xDC00 || Second >= 0xE000)) {
      Invalid();      // Leading surrogate was unpaired.
      First = Second; // Second escape still needs to be processed.
      continue;
    }
    // Case 3c: a valid surrogate pair encoding an astral codepoint.
    encodeUtf8(0x10000 | ((First - 0xD800) << 10) | (Second - 0xDC00), Out);
    return true;
  }
}

bool Parser::parseError(const char *Msg) {
  int Line = 1;
  const char *StartOfLine = Start;
  for (const char *X = Start; X < P; ++X) {
    if (*X == 0x0A) {
      ++Line;
      StartOfLine = X + 1;
    }
  }
  Err.emplace(
      llvm::make_unique<ParseError>(Msg, Line, P - StartOfLine, P - Start));
  return false;
}
} // namespace

Expected<Value> parse(StringRef JSON) {
  Parser P(JSON);
  Value E = nullptr;
  if (P.checkUTF8())
    if (P.parseValue(E))
      if (P.assertEnd())
        return std::move(E);
  return P.takeError();
}
char ParseError::ID = 0;

static std::vector<const Object::value_type *> sortedElements(const Object &O) {
  std::vector<const Object::value_type *> Elements;
  for (const auto &E : O)
    Elements.push_back(&E);
  llvm::sort(Elements,
             [](const Object::value_type *L, const Object::value_type *R) {
               return L->first < R->first;
             });
  return Elements;
}

bool isUTF8(llvm::StringRef S, size_t *ErrOffset) {
  // Fast-path for ASCII, which is valid UTF-8.
  if (LLVM_LIKELY(isASCII(S)))
    return true;

  const UTF8 *Data = reinterpret_cast<const UTF8 *>(S.data()), *Rest = Data;
  if (LLVM_LIKELY(isLegalUTF8String(&Rest, Data + S.size())))
    return true;

  if (ErrOffset)
    *ErrOffset = Rest - Data;
  return false;
}

std::string fixUTF8(llvm::StringRef S) {
  // This isn't particularly efficient, but is only for error-recovery.
  std::vector<UTF32> Codepoints(S.size()); // 1 codepoint per byte suffices.
  const UTF8 *In8 = reinterpret_cast<const UTF8 *>(S.data());
  UTF32 *Out32 = Codepoints.data();
  ConvertUTF8toUTF32(&In8, In8 + S.size(), &Out32, Out32 + Codepoints.size(),
                     lenientConversion);
  Codepoints.resize(Out32 - Codepoints.data());
  std::string Res(4 * Codepoints.size(), 0); // 4 bytes per codepoint suffice
  const UTF32 *In32 = Codepoints.data();
  UTF8 *Out8 = reinterpret_cast<UTF8 *>(&Res[0]);
  ConvertUTF32toUTF8(&In32, In32 + Codepoints.size(), &Out8, Out8 + Res.size(),
                     strictConversion);
  Res.resize(reinterpret_cast<char *>(Out8) - Res.data());
  return Res;
}

} // namespace json
} // namespace llvm

static void quote(llvm::raw_ostream &OS, llvm::StringRef S) {
  OS << '\"';
  for (unsigned char C : S) {
    if (C == 0x22 || C == 0x5C)
      OS << '\\';
    if (C >= 0x20) {
      OS << C;
      continue;
    }
    OS << '\\';
    switch (C) {
    // A few characters are common enough to make short escapes worthwhile.
    case '\t':
      OS << 't';
      break;
    case '\n':
      OS << 'n';
      break;
    case '\r':
      OS << 'r';
      break;
    default:
      OS << 'u';
      llvm::write_hex(OS, C, llvm::HexPrintStyle::Lower, 4);
      break;
    }
  }
  OS << '\"';
}

enum IndenterAction {
  Indent,
  Outdent,
  Newline,
  Space,
};

// Prints JSON. The indenter can be used to control formatting.
template <typename Indenter>
void llvm::json::Value::print(raw_ostream &OS, const Indenter &I) const {
  switch (Type) {
  case T_Null:
    OS << "null";
    break;
  case T_Boolean:
    OS << (as<bool>() ? "true" : "false");
    break;
  case T_Double:
    OS << format("%.*g", std::numeric_limits<double>::max_digits10,
                 as<double>());
    break;
  case T_Integer:
    OS << as<int64_t>();
    break;
  case T_StringRef:
    quote(OS, as<StringRef>());
    break;
  case T_String:
    quote(OS, as<std::string>());
    break;
  case T_Object: {
    bool Comma = false;
    OS << '{';
    I(Indent);
    for (const auto *P : sortedElements(as<json::Object>())) {
      if (Comma)
        OS << ',';
      Comma = true;
      I(Newline);
      quote(OS, P->first);
      OS << ':';
      I(Space);
      P->second.print(OS, I);
    }
    I(Outdent);
    if (Comma)
      I(Newline);
    OS << '}';
    break;
  }
  case T_Array: {
    bool Comma = false;
    OS << '[';
    I(Indent);
    for (const auto &E : as<json::Array>()) {
      if (Comma)
        OS << ',';
      Comma = true;
      I(Newline);
      E.print(OS, I);
    }
    I(Outdent);
    if (Comma)
      I(Newline);
    OS << ']';
    break;
  }
  }
}

void llvm::format_provider<llvm::json::Value>::format(
    const llvm::json::Value &E, raw_ostream &OS, StringRef Options) {
  if (Options.empty()) {
    OS << E;
    return;
  }
  unsigned IndentAmount = 0;
  if (Options.getAsInteger(/*Radix=*/10, IndentAmount))
    llvm_unreachable("json::Value format options should be an integer");
  unsigned IndentLevel = 0;
  E.print(OS, [&](IndenterAction A) {
    switch (A) {
    case Newline:
      OS << '\n';
      OS.indent(IndentLevel);
      break;
    case Space:
      OS << ' ';
      break;
    case Indent:
      IndentLevel += IndentAmount;
      break;
    case Outdent:
      IndentLevel -= IndentAmount;
      break;
    };
  });
}

llvm::raw_ostream &llvm::json::operator<<(raw_ostream &OS, const Value &E) {
  E.print(OS, [](IndenterAction A) { /*ignore*/ });
  return OS;
}
